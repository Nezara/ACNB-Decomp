/* src/com/comlau.cpp — COM left-arm "launcher" AI (whole file): laser gun, shield or blade. COMLAU_Init (from ComInit)
 * picks the program from the AI data (lau_data.progno 0 blade, 1 shield, 2 laser gun; a blade arm without one falls
 * back to the blade) and stores its Wait function in comp->laumng.actfunc, which DF_ComInKey runs every think step.
 * The Wait functions press 0x2000 (left arm), 0x10 / 0x4000 (blade lunge, boost) through the blade pad channel. */
#include <bit>
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

static const float COMLAU_PI = 3.14159274f;   /* 0x40490fdb */

static inline float comlau_abs(float x) { return x < 0.0f ? -x : x; }
static inline float comlau_deg(unsigned int deg) { return (COMLAU_PI * (float)deg) / 180.0f; }

/* Laser gun: locked on within [dist_min, dist_max]: after shot_minLTime, fire once past shot_maxLTime, within
 * shot_nearRange, or at the shot rate; not locked: fire when the enemy is inside the error cone within (dist_min,
 * dist_max) (0 = no limit). Energy guns hold fire at low energy. */
static void ComLGun_Wait(stComLauMng *laumngp)
{
    PS2_FRAME(0x20);
    stComLGun *d = (stComLGun *)laumngp->datap;
    AcPartsLgunSt *lg = AcCtrl_GetPartsLgun(com_acp);
    stCom *c = comp;
    if (!c->ene_mp) return;
    if (!c->f_can_see_enemy) return;
    _weapon_st *wp = AcLgun_GetWeaponData(lg);
    if (wp && (wp->dstat.effect & 0x20)) {
        c = comp;
        if (c->energy_rate <= (float)c->datap->alert_energy / 100.0f) return;
    }
    c = comp;
    AcPlayerSt *a = com_acp;
    if (a->lockmng.lock[0].ofp == c->ene_mp && (a->lockmng.lock[0].lock_stat & 2) &&
        !(c->elenxz < (float)d->dist_min) && c->elenxz <= (float)d->dist_max) {
        int t = a->lockmng.lock[0].lock_lktime;
        if (t < d->shot_minLTime) return;
        /* (the original repeats the range test here; it cannot fail after the one above) */
        if (!(d->shot_maxLTime < t) && !(c->elen <= (float)d->shot_nearRange)) {
            float r = randf();
            if (!(r <= (float)d->shot_rate / 100.0f)) return;
        }
        comp->pad |= 0x2000;
        return;
    }
    if (!(comlau_abs(c->ctoe_ang_x) < comlau_deg(d->errs_angX))) return;
    if (!(comlau_abs(c->ctoe_ang_y) < comlau_deg(d->errs_angY))) return;
    if (d->dist_min && c->elenxz <= (float)d->dist_min) return;
    if (d->dist_max && !(c->elenxz < (float)d->dist_max)) return;
    c->pad |= 0x2000;
}

static void ComLGun_Init(void)
{
    stCom *c = comp;
    c->laumng.step = 0;
    c->laumng.time_act = 0;
    c->laumng.actfunc = (decltype(c->laumng.actfunc))GFN(ComLGun_Wait);
}

/* the threat at vector v (from the AC, raised to its body centre) is inside the shield's angles + 20 */
static int comlau_shield_covers(float *v, float *a)
{
    v[1] = v[1] + 0.5f * com_acp->tall;
    DF_CALC_Vector2Angle(a, v);
    a[1] = add_angf(a[1], COMLAU_PI - comp->ang[1]);
    if (!(comlau_abs(a[0]) <= 20.0f + com_acp->param.shield_angx)) return 0;
    if (!(comlau_abs(a[1]) <= 20.0f + com_acp->param.shield_angy)) return 0;
    return 1;
}

/* Shield: need = (incoming bullet covered + close enemy AC without a left arm, facing us) / 2, + 1 inside dist_min.
 * Up: drop it at low energy, or after time_min once the need is gone. Down: raise it with energy, the enemy ahead
 * and some need (a low need only on a once-a-second equip_rate roll). (sic) The enemy threat only counts when the
 * enemy AC's Larm_type is 0. */
static void ComSld_Wait(stComLauMng *laumngp, stComSldWk *wkp)
{
    PS2_FRAME(0x70);
    stComShield *d = (stComShield *)laumngp->datap;
    if (com_acp->param.Larm_type != 2) return;
    float t1, t2;
    _spell2 *b = comp->nearest_bltp;
    if (!b) {
        t1 = 0.0f;
    } else {
        PS2_STACK(float[4], v, 0x50);
        PS2_STACK(float[2], a, 0x68);
        v[0] = b->pos[0] - com_acp->loc[0];
        v[1] = b->pos[1] - com_acp->loc[1];
        v[2] = b->pos[2] - com_acp->loc[2];
        v[3] = b->pos[3];
        t1 = comlau_shield_covers(v, a) ? 1.0f : 0.0f;
    }
    if (!comp->ene_mp) {
        t2 = 0.0f;
    } else {
        PS2_STACK(float[4], v, 0x40);
        PS2_STACK(float[2], a, 0x60);
        StModel *em = comp->ene_mp;
        v[0] = em->loc[0] - com_acp->loc[0];
        v[1] = em->loc[1] - com_acp->loc[1];
        v[2] = em->loc[2] - com_acp->loc[2];
        v[3] = em->loc[3];
        if (!comlau_shield_covers(v, a)) {
            t2 = 0.0f;
        } else {
            stCom *c = comp;
            em = c->ene_mp;
            if (em->type != 4 || ((AcPlayerSt *)em->arg0p)->param.Larm_type != 0) {
                t2 = 0.0f;
            } else {
                float fz = fz_small(c->elenxz, 12.0f, 3.0f);
                float e = comlau_abs(comp->etoc_ang_y) / COMLAU_PI;
                t2 = fz * (1.0f - e);
            }
        }
    }
    float need = 0.5f * (t1 + t2);
    if (comp->elen < (float)d->dist_min) need = need + 1.0f;
    if (com_acp->shieldstat == 1) {
        wkp->time_equip++;
        if (!(comp->energy_rate <= (float)d->energy_release / 100.0f)) {
            if (wkp->time_equip < d->time_min) return;
            if (!(need < 0.1f)) return;
        }
    } else {
        wkp->time_equip = 0;
        if (comp->energy_rate <= (float)d->energy_equip / 100.0f) return;
        if (!(comlau_abs(comp->ctoe_ang_y) <= comlau_deg(d->ang_maxY))) return;
        if (need < std::bit_cast<float>(0x38d1b717u)) return;   /* 0.0001 */
        if (need <= 0.8f) {
            if (FRAME % 60 != 0) return;
            if (!(ps2_mod32(ps2_rand(), 100) < d->equip_rate)) return;
        }
    }
    if (!ComPad_KeyExist(&comp->padmng.blade)) comp->pad |= 0x2000;
}

static void ComSld_Init(void)
{
    stCom *c = comp;
    c->laumng.step = 0;
    c->laumng.time_act = 0;
    c->laumng.actfunc = (decltype(c->laumng.actfunc))GFN(ComSld_Wait);
}

/* the strike window: not below the area-over box floor while airborne, enemy visible, within the height, distance
 * band and yaw cone (rgn: the original's stack copy of CopyOfSysInfo.rgnover) */
static int comlau_blade_ok(float (*rgn)[4], float angmax, float distmin, float distmax, float heightmax)
{
    int bits = com_check_in_region_xyz(comp->loc, rgn, -20.0f);
    int airborne = com_acp ? (com_acp->actstat & 1) : !com_enp->sw.landing;
    if (airborne && (bits & 0x20)) return 0;
    stCom *c = comp;
    if (!c->f_can_see_enemy) return 0;
    if (!(comlau_abs(c->eloc[1]) < heightmax)) return 0;
    if (c->elenxz < distmin) return 0;
    if (!(c->elenxz <= distmax)) return 0;
    return comlau_abs(c->ctoe_ang_y) < (COMLAU_PI * angmax) / 180.0f;
}

/* Blade: strike (0x10 + 0x2000) after bld_wait frames in the window with energy; otherwise (magic sword on the ground,
 * or a Larm_type 1 arm) the wave: boost during the blade actions 0xb / 0xe, fire the wave (0x2000 + 0x4000) after
 * wav_wait frames in its window. */
static void ComBld_Wait(stComLauMng *laumngp)
{
    PS2_FRAME(0x90);
    stComBlade *d = (stComBlade *)laumngp->datap;
    float ang = (float)d->bld_ang_maxY;
    float dmax = (float)d->bld_dist_max;
    float hmax = (float)d->bld_height_max;
    PS2_STACK(float[2][4], rgn, 0x70);
    memcpy(rgn, CopyOfSysInfo.rgnover, 32);
    if (comlau_blade_ok(rgn, ang, 0.0f, dmax, hmax)) {
        laumngp->time_wait++;
        if (d->bld_wait < laumngp->time_wait) {
            if (!(comp->energy_rate < (float)d->bld_energy_min / 100.0f) && !ComPad_KeyExist(&comp->padmng.blade)) {
                comp->pad |= 0x10;
                comp->pad |= 0x2000;
                laumngp->time_wait = 0;
                return;
            }
        }
    } else {
        laumngp->time_wait = 0;
    }
    AcPlayerSt *a = com_acp;
    if (!(a->param.abl_magicsword && !(a->actstat & 1)) && a->param.Larm_type != 1) return;
    if (a->actno == 0xb || a->actno == 0xe) {
        comp->pad |= 0x4000;
        return;
    }
    ang = (float)d->wav_ang_maxY;
    dmax = (float)d->wav_dist_max;
    float dmin = (float)d->wav_dist_min;
    hmax = (float)d->wav_height_max;
    PS2_STACK(float[2][4], rgn2, 0x50);
    memcpy(rgn2, CopyOfSysInfo.rgnover, 32);
    if (comlau_blade_ok(rgn2, ang, dmin, dmax, hmax)) {
        laumngp->time_wait_wav++;
        if (d->wav_wait < laumngp->time_wait_wav) {
            if (!(comp->energy_rate < (float)d->wav_energy_min / 100.0f) && !ComPad_KeyExist(&comp->padmng.blade)) {
                comp->pad |= 0x2000;
                comp->pad |= 0x4000;
                laumngp->time_wait_wav = 0;
            }
        }
    } else {
        laumngp->time_wait_wav = 0;
    }
}

static void ComBld_Init(void)
{
    stCom *c = comp;
    c->laumng.step = 0;
    c->laumng.time_act = 0;
    c->laumng.actfunc = (decltype(c->laumng.actfunc))GFN(ComBld_Wait);
}

/* the original's COMLAU_Init__initfunctbl: [0] blade, [1] shield, [2] laser gun */
static void (*const s_lauInit[3])(void) = {ComBld_Init, ComSld_Init, ComLGun_Init};

void COMLAU_Init(stCom *comp, void *swordp, stComData *comdatp)
{
    PS2_FRAME(0x50);
    stComLauMng *lm = &comp->laumng;
    memset(lm, 0, 0x40);
    stComLau *ld = (stComLau *)&comdatp->lau_data;
    lm->datap = ld;
    short p = ld->progno;
    if (p >= 0 && p < 3) {
        lm->type = (unsigned char)ld->progno;
        s_lauInit[lm->type]();
    } else if (com_acp->param.armsp->type == 2) {
        lm->type = 0;
        s_lauInit[lm->type]();
    } else {
        lm->type = 4;
    }
}
