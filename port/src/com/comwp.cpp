/* src/com/comwp.cpp — COM weapon managers (whole file). ComTree_AnalyzeComTree calls COM_InitWeapon once per tree
 * weapon slot: it picks a program (the data's progno, or one from the weapon id / prog_no), normalises a few fields of
 * the AI data (in ComData, so the clamps persist) and runs the program's setup, which stores two callbacks:
 *   val(wm, wp, &wm->wk)            fuzzy desirability, called by __ComTree_ValueWeapon;
 *   main(wm, datap, wp, &wm->wk)    "fire now?", called by shoot_weapon; presses pad 0x8000 and returns 1.
 * (sic) The fire rates (shot_rate / errs_rate, u8) are compared raw against randf() (0..1): any non-zero rate always
 * fires, 0 never does; randf is still drawn. */
#include <bit>
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

static const float COMWP_PI = 3.14159274f;   /* 0x40490fdb */

/* MWCC's float -> unsigned int conversion (truncating) */
static inline unsigned int comwp_f2u(float x)
{
    if (2147483648.0f <= x) return (unsigned int)ps2_ftoi(x - 2147483648.0f) | 0x80000000u;
    return (unsigned int)ps2_ftoi(x);
}

static inline float comwp_abs(float x) { return x < 0.0f ? -x : x; }

/* the shared "fire at this rate" tail */
static int comwp_rate_fire(float r)
{
    if (r <= randf()) return 0;
    comp->pad |= 0x8000;
    return 1;
}

/* fuzzy "the enemy is at about 3/4 of the weapon's range" */
static float comwp_range_score(_weapon_st *wp)
{
    float t = 0.75f * wp->range;
    float a = fz_small(comp->elen, t, 50.0f);
    a = fz_half(a);
    float b = fz_equal(comp->elen, t, 100.0f);
    return fz_add(a, b);
}

static inline int comwp_locked_on(void)
{
    return com_acp->lockmng.lock[0].ofp == comp->ene_mp && (com_acp->lockmng.lock[0].lock_stat & 2);
}

/* program by weapon: inside parts (statid 3) by id, others by prog_no */
static int get_wepmng_progno(_weapon_st *wp)
{
    if (wp->statid == 3) {
        switch (wp->id) {
        case 0x197: case 0x196: case 0x18e: case 0x64: case 0x190: case 0x63: case 0x62: case 0x192: case 0x5f:
            return 10;   /* StdInside */
        case 0x195: case 0x194: case 0x193: case 0x6a: case 0x69:
            return 5;    /* NoiseMaker */
        case 0x191: case 0x61: case 0x60:
            return 8;    /* FloatingMine */
        case 0x6c: case 0x6b:
            return 6;    /* DummyMaker */
        case 0x18f: case 0x68: case 0x67:
            return 4;    /* Decoy */
        default:
            return 10;
        }
    }
    switch (wp->prog_no) {
    case 0: return 1;   /* std_Gun */
    case 1: return 2;   /* std_Missile */
    case 2: return 3;   /* std_Cannon */
    case 4: return 9;   /* PileBanker */
    default: return 1;
    }
}

static int ComWepmng_FloatingMine(stComWepMng *wmngp);
static int ComWepmng_Bom(stComWepMng *wmngp);
static int ComWepmng_StdInside(stComWepMng *wmngp);
static int ComWepmng_DummyMaker(stComWepMng *wmngp);
static int ComWepmng_NoiseMaker(stComWepMng *wmngp);
static int ComWepmng_Decoy(stComWepMng *wmngp);
static int ComWepmng_PileBanker(stComWepMng *wmngp);
static int ComWepmng_std_Cannon(stComWepMng *wmngp);
static int ComWepmng_std_Missile(stComWepMng *wmngp, stComWepData *wparamp, struct_a92245aa *wkp);
static int ComWepmng_std_Gun(stComWepMng *wmngp);
static int ComWepmng_nouse(stComWepMng *wmngp);

/* the original's ComWeaponManageFunc[11] (by program number) */
static int (*const s_wepmngFunc[11])(stComWepMng *, stComWepData *, void *) = {
    [](stComWepMng *m, stComWepData *, void *) { return ComWepmng_nouse(m); },
    [](stComWepMng *m, stComWepData *, void *) { return ComWepmng_std_Gun(m); },
    [](stComWepMng *m, stComWepData *d, void *w) { return ComWepmng_std_Missile(m, d, (struct_a92245aa *)w); },
    [](stComWepMng *m, stComWepData *, void *) { return ComWepmng_std_Cannon(m); },
    [](stComWepMng *m, stComWepData *, void *) { return ComWepmng_Decoy(m); },
    [](stComWepMng *m, stComWepData *, void *) { return ComWepmng_NoiseMaker(m); },
    [](stComWepMng *m, stComWepData *, void *) { return ComWepmng_DummyMaker(m); },
    [](stComWepMng *m, stComWepData *, void *) { return ComWepmng_Bom(m); },
    [](stComWepMng *m, stComWepData *, void *) { return ComWepmng_FloatingMine(m); },
    [](stComWepMng *m, stComWepData *, void *) { return ComWepmng_PileBanker(m); },
    [](stComWepMng *m, stComWepData *, void *) { return ComWepmng_StdInside(m); },
};

/* Sets up one weapon manager (see the header). The rate defaults compute 2 * (0 / reload_time) = 0: no effect. */
int COM_InitWeapon(stComWepMng *wmngp, stComWepData *wparamp, _weapon_st *wp, int arm_type)
{
    PS2_FRAME(0x60);
    int prog = 0;
    if (wp && wparamp) {
        prog = wparamp->progno;
        if (prog == 0) prog = get_wepmng_progno(wp);
    }
    if (prog < 0 || prog >= 11) return 0;
    wmngp->datap = wparamp;
    if (wparamp->value_max < wparamp->value_min) wparamp->value_max = wparamp->value_min;
    if (wparamp->time_max < wparamp->time_min) wparamp->time_max = wparamp->time_min;
    if (wp) {
        wmngp->wep = wp;
        wmngp->stat_id = (char)wp->statid;
        wmngp->arm_type = (char)arm_type;
    }
    wmngp->value = wparamp->value_init;
    if (!wmngp->value) wmngp->value = 50;
    if (!wparamp->errs_angX) wparamp->errs_angX = 30;
    if (!wparamp->errs_angY) wparamp->errs_angY = 30;
    memcpy(wmngp->revision_op, wparamp->revision_op, 0x10);
    if (wp) {
        if (!wmngp->datap->dist_target) wmngp->datap->dist_target = (unsigned short)comwp_f2u(0.75f * wp->range);
        if ((int)(comwp_f2u(wp->range) & 0xffff) < (int)wmngp->datap->dist_target)
            wmngp->datap->dist_target = (unsigned short)comwp_f2u(wp->range);
    } else {
        if (!wmngp->datap->dist_target) wmngp->datap->dist_target = 200;
    }
    if (wp) {
        stComWepData *d = wmngp->datap;
        if ((float)d->shot_rate < 1.0f && wp->reload_time)
            d->shot_rate = (unsigned char)comwp_f2u(2.0f * ((float)d->shot_rate / (float)wp->reload_time));
        d = wmngp->datap;
        if ((float)d->errs_rate < 1.0f && wp->reload_time)
            d->errs_rate = (unsigned char)comwp_f2u(2.0f * ((float)d->errs_rate / (float)wp->reload_time));
    }
    return s_wepmngFunc[prog](wmngp, wparamp, (void *)&wmngp->wk);
}

/* --- floating mine: drop it when the enemy is close and not above */
static float fmine_calcvalue(stComWepMng *wmngp, _weapon_st *wp);
static int fmine_onactive(stComWepMng *wmngp);

static int ComWepmng_FloatingMine(stComWepMng *wmngp)
{
    wmngp->main = (decltype(wmngp->main))GFN(fmine_onactive);
    wmngp->val = (decltype(wmngp->val))GFN(fmine_calcvalue);
    return 1;
}

static float fmine_calcvalue(stComWepMng *wmngp, _weapon_st *wp)
{
    PS2_FRAME(0x20);
    return comwp_range_score(wp);
}

static int fmine_onactive(stComWepMng *wmngp)
{
    PS2_FRAME(0x20);
    if (!(comp->elenxz <= 30.0f)) return 0;
    if (!(comp->eloc[1] <= 3.0f)) return 0;
    return comwp_rate_fire((float)wmngp->datap->shot_rate);
}

/* --- bomb: while backing away from an enemy behind and close (not straight up / down), after the lock delays */
static float bom_calcvalue(stComWepMng *wmngp, _weapon_st *wp);
static int bom_onactive(stComWepMng *wmngp, stComWepData *wparamp);

static int ComWepmng_Bom(stComWepMng *wmngp)
{
    wmngp->main = (decltype(wmngp->main))GFN(bom_onactive);
    wmngp->val = (decltype(wmngp->val))GFN(bom_calcvalue);
    return 1;
}

static float bom_calcvalue(stComWepMng *wmngp, _weapon_st *wp)
{
    PS2_FRAME(0x20);
    return comwp_range_score(wp);
}

static int bom_onactive(stComWepMng *wmngp, stComWepData *wparamp)
{
    StModel *mp = com_acp->mp;
    float p1 = mp->matrix2[2][0] * com_acp->velocity[0];   /* mula; madd */
    float p2 = mp->matrix2[2][2] * com_acp->velocity[2];
    if (!(p1 + p2 < 0.0f)) return 0;
    stCom *c = comp;
    if (comwp_abs(c->ctoe_ang_y) <= std::bit_cast<float>(0x3fdf66f3u)) return 0;   /* 100 degrees */
    if (!(c->elenxz < (float)wparamp->shot_nearRange)) return 0;
    if (c->eloc[1] <= -5.0f) return 0;
    if (!(c->eloc[1] < 200.0f)) return 0;
    unsigned short t = wmngp->time_lock;
    if (t < wparamp->shot_delay) return 0;
    if (t < wparamp->shot_maxLTime) return 0;
    c->pad |= 0x8000;
    return 1;
}

/* --- dummy maker / inside parts / noise maker / decoy: at the data's distance, fire at the shot rate */
static float dummymaker_calcvalue(stComWepMng *wmngp);
static int dummymaker_onactive(stComWepMng *wmngp);

static int ComWepmng_StdInside(stComWepMng *wmngp)
{
    wmngp->main = (decltype(wmngp->main))GFN(dummymaker_onactive);
    wmngp->val = (decltype(wmngp->val))GFN(dummymaker_calcvalue);
    return 1;
}

static int ComWepmng_DummyMaker(stComWepMng *wmngp)
{
    wmngp->main = (decltype(wmngp->main))GFN(dummymaker_onactive);
    wmngp->val = (decltype(wmngp->val))GFN(dummymaker_calcvalue);
    return 1;
}

static float dummymaker_calcvalue(stComWepMng *wmngp)
{
    return fz_equal(comp->elen, (float)wmngp->datap->dist_target, 100.0f);
}

static int dummymaker_onactive(stComWepMng *wmngp)
{
    PS2_FRAME(0x20);
    return comwp_rate_fire((float)wmngp->datap->shot_rate);
}

static float noisemaker_calcvalue(stComWepMng *wmngp, _weapon_st *wp);
static int noisemaker_onactive(stComWepMng *wmngp);

static int ComWepmng_NoiseMaker(stComWepMng *wmngp)
{
    wmngp->main = (decltype(wmngp->main))GFN(noisemaker_onactive);
    wmngp->val = (decltype(wmngp->val))GFN(noisemaker_calcvalue);
    return 1;
}

static float noisemaker_calcvalue(stComWepMng *wmngp, _weapon_st *wp)
{
    PS2_FRAME(0x20);
    return comwp_range_score(wp);
}

static int noisemaker_onactive(stComWepMng *wmngp)
{
    PS2_FRAME(0x20);
    return comwp_rate_fire((float)wmngp->datap->shot_rate);
}

static float decoy_calcvalue(stComWepMng *wmngp);
static int decoy_onactive(stComWepMng *wmngp, stComWepData *paramp, _weapon_st *wp, struct_16037fd5 *wkp);

static int ComWepmng_Decoy(stComWepMng *wmngp)
{
    wmngp->main = (decltype(wmngp->main))GFN(decoy_onactive);
    wmngp->val = (decltype(wmngp->val))GFN(decoy_calcvalue);
    return 1;
}

static float decoy_calcvalue(stComWepMng *wmngp)
{
    return fz_equal(comp->elen, (float)wmngp->datap->dist_target, 100.0f);
}

/* decoy: one launch per shot_delay frames; remembers where it was put */
static int decoy_onactive(stComWepMng *wmngp, stComWepData *paramp, _weapon_st *wp, struct_16037fd5 *wkp)
{
    PS2_FRAME(0x40);
    if (wkp->put_frame > 0) {
        wkp->put_frame--;
        return 0;
    }
    float r = (float)wmngp->datap->shot_rate;
    if (r <= randf()) return 0;
    comp->pad |= 0x8000;
    memcpy(wkp->putpos, com_acp->loc, 16);
    wkp->put_frame = paramp->shot_delay;
    return 1;
}

/* --- pile banker: close range, the enemy inside a cone that widens as it gets closer */
static float pilebanker_calcvalue(stComWepMng *wmngp, _weapon_st *wp);
static int pilebanker_onactive(stComWepMng *wmngp, stComWepData *paramp);

static int ComWepmng_PileBanker(stComWepMng *wmngp)
{
    wmngp->main = (decltype(wmngp->main))GFN(pilebanker_onactive);
    wmngp->val = (decltype(wmngp->val))GFN(pilebanker_calcvalue);
    return 1;
}

/* (sic) the original scores twice and computes a nolock term whose result it drops; the fuzzy functions are pure,
 * so only the returned score is computed here */
static float pilebanker_calcvalue(stComWepMng *wmngp, _weapon_st *wp)
{
    PS2_FRAME(0x50);
    return comwp_range_score(wp);
}

/* (sic) rewrites ComData every call: nearRange >= 30, errs_angX >= 15, errs_angY below 15 becomes 25 */
static int pilebanker_onactive(stComWepMng *wmngp, stComWepData *paramp)
{
    PS2_FRAME(0x20);
    stComWepData *d = wmngp->datap;
    if ((float)d->errs_rate < std::bit_cast<float>(0x38d1b717u)) d->errs_rate = d->shot_rate;   /* 0.0001 */
    if (paramp->shot_nearRange < 30) paramp->shot_nearRange = 30;
    if (paramp->errs_angX < 15) paramp->errs_angX = 15;
    if (paramp->errs_angY < 15) paramp->errs_angY = 25;
    stCom *c = comp;
    float n = (float)paramp->shot_nearRange;
    if (!(c->elen <= n)) return 0;
    float k = ps2_div(n - c->elen, n);
    float ax = k * ((COMWP_PI * (float)paramp->errs_angX) / 180.0f);
    float ay = k * ((COMWP_PI * (float)paramp->errs_angY) / 180.0f);
    float r = (float)wmngp->datap->errs_rate;
    if (!(comwp_abs(c->ctoe_ang_x) < ax)) return 0;
    if (!(comwp_abs(c->ctoe_ang_y) < ay)) return 0;
    if (r <= randf()) return 0;
    if (comp->pad_old & 0x8000) return 0;
    comp->pad |= 0x8000;
    return 1;
}

/* --- cannons */
static float std_walkcannon_calcvalue(stComWepMng *wmngp, _weapon_st *wp);
static int std_walkcannon_onactive(stComWepMng *wmngp, stComWepData *paramp, _weapon_st *wp);
static float std_shitcannon_calcvalue(stComWepMng *wmngp, _weapon_st *wp);
static int std_shitcannon_onactive(stComWepMng *wmngp, stComWepData *paramp, _weapon_st *wp);

/* walking cannon (legs that can fire cannons on the move) or the stance cannon */
static int ComWepmng_std_Cannon(stComWepMng *wmngp)
{
    if (com_acp->param.abl_walkcannon) {
        wmngp->main = (decltype(wmngp->main))GFN(std_walkcannon_onactive);
        wmngp->val = (decltype(wmngp->val))GFN(std_walkcannon_calcvalue);
    } else {
        wmngp->main = (decltype(wmngp->main))GFN(std_shitcannon_onactive);
        wmngp->val = (decltype(wmngp->val))GFN(std_shitcannon_calcvalue);
    }
    return 1;
}

static float std_walkcannon_calcvalue(stComWepMng *wmngp, _weapon_st *wp)
{
    PS2_FRAME(0x20);
    return comwp_range_score(wp);
}

/* the locked-on shot: close range at the shot rate, else after the lock times / delays within the distance band;
 * unlocked: an aimed shot when the enemy is inside a cone that narrows with distance (errs_rate) */
static int comwp_locked_shot(stComWepMng *wmngp, stComWepData *paramp)
{
    stCom *c = comp;
    if (c->elen <= (float)paramp->shot_nearRange) {
        float r = (float)wmngp->datap->shot_rate;
        if (!(r <= randf())) {
            comp->pad |= 0x8000;
            return 1;
        }
    }
    int t = com_acp->lockmng.lock[0].lock_lktime;
    if (t < paramp->shot_minLTime) return 0;
    if (wmngp->time_lock < paramp->shot_delay) return 0;
    stComWepData *d = wmngp->datap;
    if (d->dist_min && comp->elen < (float)d->dist_min) return 0;
    if (d->dist_max && !(comp->elen <= (float)d->dist_max)) return 0;
    if (!(paramp->shot_maxLTime < t)) return 0;
    return comwp_rate_fire((float)wmngp->datap->shot_rate);
}

/* unlocked: no-lock weapons with no error rate get 25-degree cones and the shot rate as their error rate */
static int comwp_unlocked_prep(stComWepMng *wmngp, stComWepData *paramp, _weapon_st *wp)
{
    if (wp && wp->lockmax == 0 && (float)wmngp->datap->errs_rate <= std::bit_cast<float>(0x38d1b717u)) {
        paramp->errs_angX = 25;
        paramp->errs_angY = 25;
        stComWepData *d = wmngp->datap;
        d->errs_rate = d->shot_rate;
    }
    stComWepData *d = wmngp->datap;
    if (d->dist_min && comp->elen < (float)d->dist_min) return 0;
    if (d->dist_max && !(comp->elen <= (float)d->dist_max)) return 0;
    return 1;
}

static int stdcannon_shot(stComWepMng *wmngp, stComWepData *paramp, _weapon_st *wp)
{
    PS2_FRAME(0x40);
    if (comwp_locked_on()) return comwp_locked_shot(wmngp, paramp);
    if (!comwp_unlocked_prep(wmngp, paramp, wp)) return 0;
    stCom *c = comp;
    float k = ps2_div(wp->range - c->elen, wp->range);   /* (sic) wp unchecked here */
    float ax = k * ((COMWP_PI * (float)paramp->errs_angX) / 180.0f);
    float ay = k * ((COMWP_PI * (float)paramp->errs_angY) / 180.0f);
    if (!(comwp_abs(c->ctoe_ang_x) < ax)) return 0;
    if (!(comwp_abs(c->ctoe_ang_y) < ay)) return 0;
    if (c->pad_old & 0x8000) return 0;
    return comwp_rate_fire((float)wmngp->datap->errs_rate);
}

static int std_walkcannon_onactive(stComWepMng *wmngp, stComWepData *paramp, _weapon_st *wp)
{
    PS2_FRAME(0x10);
    if (!comp->f_can_see_enemy) return 0;
    if (!(comp->elen <= wp->range)) return 0;
    return stdcannon_shot(wmngp, paramp, wp);
}

static float std_shitcannon_calcvalue(stComWepMng *wmngp, _weapon_st *wp)
{
    PS2_FRAME(0x20);
    return comwp_range_score(wp);
}

/* stance cannon: in the stance (action 0x10) shoot when the enemy is visible, in range and within 45 degrees (move /
 * boost keys released), else now and then leave the stance; out of it, press fire to take the stance when the shot
 * would be possible and the AC is well inside the region */
static int std_shitcannon_onactive(stComWepMng *wmngp, stComWepData *paramp, _weapon_st *wp)
{
    PS2_FRAME(0x20);
    const float q = std::bit_cast<float>(0x3f490fdbu);   /* PI / 4 */
    if (com_acp->actno == 0x10) {
        if (wp->blt_cnt == 0) return 0;
        stCom *c = comp;
        if (c->f_can_see_enemy && c->elen < wp->range && comwp_abs(c->ctoe_ang_y) < q && comwp_abs(c->ctoe_ang_x) < q) {
            c->pad &= 0xffffb3af;
            return stdcannon_shot(wmngp, paramp, wp);
        }
        if (com_check_in_region(com_acp->loc, CopyOfSysInfo.rgnalert, -10.0f) != 0) return 0;
        if (randf() < 0.8f) comp->pad &= 0xffffb3af;
        return 0;
    }
    if (comp->f_setcannon_dis) return 0;
    if ((float)wmngp->datap->shot_rate <= 0.0f) return 0;
    if (com_check_in_region(com_acp->loc, CopyOfSysInfo.rgnalert, -10.0f) != 0) return 0;
    stCom *c = comp;
    if (!(c->elen < wp->range)) return 0;
    if (!c->f_can_see_enemy) return 0;
    if (!(comwp_abs(c->ctoe_ang_y) < q)) return 0;
    if (!(comwp_abs(c->ctoe_ang_x) < q)) return 0;
    if (c->pad_old & 0x8000) return 0;
    c->pad |= 0x8000;
    return 1;
}

/* --- missiles */
static float stdmissile_calcvalue(stComWepMng *wmngp, _weapon_st *wp);
static int stdmissile_onactive(stComWepMng *wmngp, stComWepData *paramp, _weapon_st *wp, struct_a92245aa *wkp);

static int ComWepmng_std_Missile(stComWepMng *wmngp, stComWepData *wparamp, struct_a92245aa *wkp)
{
    wmngp->main = (decltype(wmngp->main))GFN(stdmissile_onactive);
    wmngp->val = (decltype(wmngp->val))GFN(stdmissile_calcvalue);
    wkp->max_lockcnt = 0;
    wkp->last_lockcnt = 0;
    wkp->target_lockcnt = wmngp->wep->lockmax;
    wkp->now_lockcnt = 0;
    return 1;
}

/* 0 while either side's lock is jammed off (own nolock_time, the enemy AC's unlocked_time) */
static float stdmissile_calcvalue(stComWepMng *wmngp, _weapon_st *wp)
{
    PS2_FRAME(0x10);
    stCom *c = comp;
    if (com_acp->nolock_time) return 0.0f;
    StModel *em = c->ene_mp;
    if (em && em->type == 4 && ((AcPlayerSt *)em->arg0p)->unlocked_time) return 0.0f;
    if (wp->id == 0x2f) return fz_small(c->elen, wp->range, wp->range);
    return fz_large(c->elen, 100.0f, 50.0f);
}

/* Lock bookkeeping (the lock count reached, the target count for the next volley), then a full volley (even op
 * frames only) or, against jammer ACs, a partial-lock volley. (sic) An unlocked frame with now == 0 clears last; the
 * blind shot fires when rand() % 100 < blind_shot (stdgun: <=). */
static int stdmissile_onactive(stComWepMng *wmngp, stComWepData *paramp, _weapon_st *wp, struct_a92245aa *wkp)
{
    PS2_FRAME(0x90);
    float r = (float)wmngp->datap->shot_rate;
    StModel *em = comp->ene_mp;
    if (com_acp->lockmng.lock[0].ofp == em && (com_acp->lockmng.lock[0].lock_stat & 2)) {
        wkp->now_lockcnt = com_acp->lockmng.lock_cnt;
    } else {
        if (wkp->now_lockcnt != 0 || wkp->last_lockcnt != 0) {
            wkp->last_lockcnt = wkp->now_lockcnt;
            wkp->now_lockcnt = 0;
        }
        wkp->target_lockcnt = wkp->last_lockcnt - 1;
        if (!(wkp->target_lockcnt > 0)) wkp->target_lockcnt = 1;
    }
    if (wkp->max_lockcnt < wkp->now_lockcnt) wkp->max_lockcnt = wkp->now_lockcnt;
    if (wp->id == 0x2b || wp->id == 0x3f) {
        /* vertical missiles: only with a free line straight up */
        PS2_STACK(float[4], v, 0x70);
        PS2_STACK(float[4], p, 0x80);
        v[0] = 0.0f;
        v[1] = -1.0f;
        v[2] = 0.0f;
        v[3] = 1.0f;
        memcpy(p, com_acp->mp->loc, 16);
        p[1] = p[1] - (0.5f + com_acp->mp->tall);
        if (HitBoxHitProcLine(p, v, 150.0f, 1, 0) != 0) return 0;
    } else if (comp->ene_mp->type != 2 && !comp->f_can_see_enemy) {
        unsigned char b = wmngp->datap->blind_shot;
        if (!(ps2_mod32(ps2_rand(), 100) < b)) return 0;
    }
    if (com_acp->lockmng.rest_lock && wkp->now_lockcnt < wkp->target_lockcnt) {
        /* partial lock */
        if (com_acp->lockmng.lock[0].lock_artime != 1 && com_acp->lockmng.lock_cnt != wp->blt_cnt) return 0;
        float x = ps2_div((float)com_acp->lockmng.lock_cnt, (float)wp->lockmax);
        float h = fz_half(x);
        stCom *c = comp;
        r = r * (0.25f + h);
        if (!(c->ene_mp && c->ene_mp->type == 4)) return 0;
        AcPlayerSt *e = (AcPlayerSt *)c->ene_mp->arg0p;
        if (!e->param.abl_jammer) return 0;
        stComWepData *d = wmngp->datap;
        if (d->dist_min && c->elen <= (float)d->dist_min) return 0;
        if (d->dist_max && !(c->elen < (float)d->dist_max)) return 0;
        return comwp_rate_fire(r);
    }
    stComWepData *d = wmngp->datap;
    if (d->dist_min && comp->elen <= (float)d->dist_min) return 0;
    if (d->dist_max && !(comp->elen < (float)d->dist_max)) return 0;
    if (r <= randf()) return 0;
    stCom *c = comp;
    if ((c->opmng[(int)c->op_sel].time_sel & 1) == 0) c->pad |= 0x8000;
    if (wkp->target_lockcnt < wkp->max_lockcnt) wkp->target_lockcnt = wkp->target_lockcnt + 1;
    return 1;
}

/* --- guns */
static float stdgun_calcvalue(stComWepMng *wmngp, _weapon_st *wp);
static int stdgun_onactive(stComWepMng *wmngp, stComWepData *paramp, _weapon_st *wp);

static int ComWepmng_std_Gun(stComWepMng *wmngp)
{
    wmngp->main = (decltype(wmngp->main))GFN(stdgun_onactive);
    wmngp->val = (decltype(wmngp->val))GFN(stdgun_calcvalue);
    return 1;
}

/* (sic) as pilebanker_calcvalue: the original's second score and the dropped nolock term are not repeated here */
static float stdgun_calcvalue(stComWepMng *wmngp, _weapon_st *wp)
{
    PS2_FRAME(0x50);
    return comwp_range_score(wp);
}

/* the cannon shot logic with an in-range test and a blind-shot roll first; an unlocked shot with the "no lock" weapon
 * (id -1) is scaled down while the enemy AC's lock jamming lasts */
static int stdgun_onactive(stComWepMng *wmngp, stComWepData *paramp, _weapon_st *wp)
{
    PS2_FRAME(0x70);
    stCom *c = comp;
    if (!(c->elen <= wp->range)) return 0;
    if (c->ene_mp->type != 2 && !c->f_can_see_enemy) {
        unsigned char b = wmngp->datap->blind_shot;
        if (b < ps2_mod32(ps2_rand(), 100)) return 0;
    }
    if (comwp_locked_on()) return comwp_locked_shot(wmngp, paramp);
    if (!comwp_unlocked_prep(wmngp, paramp, wp)) return 0;
    c = comp;
    stComWepData *d = wmngp->datap;
    float k = ps2_div(wp->range - c->elen, wp->range);
    float ax = k * ((COMWP_PI * (float)paramp->errs_angX) / 180.0f);
    float ay = k * ((COMWP_PI * (float)paramp->errs_angY) / 180.0f);
    float r = (float)d->errs_rate;
    if (wp->id == -1 && c->ene_mp && c->ene_mp->type == 4)
        r = r * fz_small((float)((AcPlayerSt *)c->ene_mp->arg0p)->nolock_time, 120.0f, 120.0f);
    c = comp;
    if (!(comwp_abs(c->ctoe_ang_x) < ax)) return 0;
    if (!(comwp_abs(c->ctoe_ang_y) < ay)) return 0;
    if (c->pad_old & 0x8000) return 0;
    return comwp_rate_fire(r);
}

static int ComWepmng_nouse(stComWepMng *wmngp)
{
    wmngp->main = 0;
    wmngp->val = 0;
    return 0;
}
