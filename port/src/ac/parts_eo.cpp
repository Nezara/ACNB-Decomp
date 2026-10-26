/* src/ac/parts_eo.cpp - EO (core-mounted option pods) of cores with eo_num pods (whole file, OLSORTIE overlay).
 * AcPartsEoSt is a state machine per AC driven by aceo_Control from the pad (trigger conf 13): 0 waiting (refills
 * blt_cnt), 1 open (pods leave the core, 15 frames), 2 charge (pods start seeking), 3 busy (pods follow the AC),
 * 4 close (pods fly back to the core). Each pod (struct_7d6f2ad6, eomng[i]) runs its own lock state machine from
 * eo_proc: 0 waiting, 1 opening, 2 seek, 3 check, 4 lock (fires the core weapon's spell through its muzzles),
 * 5 closing; its boost effects are switched on while it is out. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"
#include "acnb/vu0m.h"

#define EO_PI 3.14159274f    /* 0x40490fdb */
#define EO_2PI 6.28318548f   /* 0x40c90fdb */

int aceo_IsShellType(AcPartsEoSt *eop)
{
    return eop->prvp->isshell;
}

_weapon_st *aceo_GetWeapon(AcPartsEoSt *eop)
{
    return &eop->prvp->wep;
}

/* ((pi + v) - 2pi * trunc((pi + v) / 2pi)) - pi */
static inline float eo_wrap(float v)
{
    float x = EO_PI + v;
    x = x - EO_2PI * (float)ps2_ftoi(ps2_div(x, EO_2PI));
    return x - EO_PI;
}

/* cur[k] = tgt[k] + 0.9 * (cur[k] - tgt[k]) on the wrapped angles, k = 3..0 (the w lanes too) */
static void eo_blend_angle(float *cur, const float *tgt)
{
    const float rate = 1.0f - 0.1f;
    for (int k = 3; k >= 0; k--) {
        float x = eo_wrap(cur[k]);
        float y = eo_wrap(tgt[k]);
        float d = x - y;
        if (d < 0.0f) d = -d;
        if (!(d <= EO_PI)) {
            if (x <= y) x = x + EO_2PI;
            else x = x - EO_2PI;
        }
        cur[k] = tgt[k] + rate * (x - y);
    }
}

/* closing: each closing pod flies back to its mount on the core (3.0 per frame) and turns toward the AC's yaw; a pod
 * that arrives waits again. actstep advances once every pod waits.
 * original: the pod-to-mount distance is computed (sqrt) and never used */
static void eo_trace_connect(AcPartsEoSt *eop)
{
    PS2_FRAME(0xf0);
    PS2_STACK(float[4], A, 0xe0);
    PS2_STACK(float[4], B, 0xd0);
    PS2_STACK(float[2][4], P, 0xb0);
    PS2_STACK(float[2][4], T, 0x90);
    PS2_STACK(float[2][4], Aold, 0x70);
    PS2_STACK(float[2][4], Bt, 0x50);
    PS2_STACK(float[4], diff, 0x40);
    static const float c0001[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    memcpy(A, c0001, 16);
    memcpy(B, c0001, 16);
    B[1] = eop->prvp->acp->ang[1];
    int i;
    for (i = 0; i < eop->prvp->prmp->eo_num; i++) {
        struct_7d6f2ad6 *emp = &eop->prvp->eomng[i];
        memcpy(P[i], emp->objp->position, 16);
        memcpy(Aold[i], emp->objp->angle, 16);
        emp->objp->noanim = 0;
        emp->objp->flag &= 0xfffe;
    }
    Anim_Update(eop->prvp->acp->eobmp);
    for (i = 0; i < eop->prvp->prmp->eo_num; i++) {
        struct_7d6f2ad6 *emp = &eop->prvp->eomng[i];
        if (emp->stat != 5) continue;
        AcPlayerSt *acp = eop->prvp->acp;
        Anim_UpdateMyObjMatrix(emp->objp, acp->mp->op[3].wrmatp);
        const float (*m)[4] = (const float (*)[4])*emp->objp->wrmatp;
        vu_apply(T[i], m, A);
        PS2_VF_MIRROR(1, m[0]);
        PS2_VF_MIRROR(2, m[1]);
        PS2_VF_MIRROR(3, m[2]);
        PS2_VF_MIRROR(4, m[3]);
        memcpy(Bt[i], B, 16);
        float d[4] = {P[i][0] - T[i][0], P[i][1] - T[i][1], P[i][2] - T[i][2], P[i][3]};
        memcpy(diff, d, 16);
        PS2_VF_MIRROR(16, d);
        PS2_VF_MIRROR(17, T[i]);
        if (trace_spd(P[i], T[i], 3.0f)) {
            emp->stat = 0;
            emp->count = 0;
            acse_PutSeVolDist(eop->prvp->acp, 0x3d, AC_SEparam.eo_close_vol, (float)AC_SEparam.eo_close_dist);
            acse_StopLoopSe(eop->prvp->acp, 2, 1);
        } else {
            eo_blend_angle(Aold[i], Bt[i]);
            emp->objp->flag |= 1;
            emp->objp->noanim = 1;
            memcpy(emp->objp->position, P[i], 16);
            memcpy(emp->objp->angle, Aold[i], 16);
        }
    }
    int n = eop->prvp->prmp->eo_num, waiting = 0;
    for (i = 0; i < n; i++)
        if (eop->prvp->eomng[i].stat == 0) waiting++;
    if (!(waiting < n)) eop->actstep++;
}

/* pods follow the AC (3.0 to its side, 12.0 below; with two pods +-30 degrees off its yaw) and aim at their lock
 * target, else face the AC's yaw */
static void eo_trace_target(AcPartsEoSt *eop)
{
    PS2_FRAME(0x90);
    PS2_STACK(float[4], pos, 0x80);
    PS2_STACK(float[4], tang, 0x70);
    PS2_STACK(float[4], d, 0x60);
    for (int i = 0; i < eop->prvp->prmp->eo_num; i++) {
        AcPartsEoPrivateSt *prv = eop->prvp;
        struct_7d6f2ad6 *emp = &prv->eomng[i];
        int lock = 0;
        float th;
        if (prv->prmp->eo_num == 1) {
            th = prv->acp->ang[1];
        } else {
            float off = 0x1.0c1524p-1f;   /* 0x3f060a92 = pi / 6 */
            if (i == 1) off = off * -1.0f;
            th = eo_wrap(off + prv->acp->ang[1]);
        }
        memcpy(pos, prv->acp->loc, 16);
        pos[1] = pos[1] - 12.0f;
        pos[0] = pos[0] + 3.0f * fsmath_SinVU0(th);
        pos[2] = pos[2] + 3.0f * fsmath_CosVU0(th);
        if (emp->stat == 4 && emp->lockmng.pri_lock && emp->lockmng.pri_lock->ofp) lock = 1;
        if (lock == 1) {
            const float *tgt = emp->lockmng.pri_lock->dloc;
            const float *op = emp->objp->position;
            float v[4] = {tgt[0] - op[0], tgt[1] - op[1], tgt[2] - op[2], tgt[3]};
            memcpy(d, v, 16);
            PS2_VF_MIRROR(16, v);
            PS2_VF_MIRROR(17, op);
            tang[0] = ps2_atan2f(d[1], ps2_sqrt(d[0] * d[0] + d[2] * d[2]));
            tang[1] = Add_Angle(ps2_atan2f(d[0], d[2]), EO_PI);
        } else {
            tang[0] = 0.0f;
            tang[1] = eop->prvp->acp->ang[1];
        }
        tang[2] = 0.0f;
        tang[3] = 1.0f;
        trace_fv(emp->objp->position, pos, 0.5f);
        eo_blend_angle(emp->objp->angle, tang);
    }
}

/* one volley: shotcnt spells, each from the pod's next muzzle (bits of muz_flag, wrapping to muzzle 0) */
static void eomng_trigger(AcPartsEoSt *eop, struct_7d6f2ad6 *emp)
{
    PS2_FRAME(0x90);
    if (!eop) return;
    AcPartsEoPrivateSt *prv = eop->prvp;
    _weapon_st *wep = &prv->wep;
    AcPlayerSt *acp = prv->acp;
    if (!acp->key_enable || eop->blt_cnt == 0) return;
    if (prv->isshell == 0) {
        int charging = prv->isshell ? 0 : AcCtrl_IsCharging(acp);
        if (charging) return;
        if (eop->prvp->acp->energyReal < (int)eop->prvp->wep.blt_price) return;
    }
    int m = 0, n = 0;
    wep->blt_cnt = eop->blt_cnt;
    if (0 < eop->prvp->wep.shotcnt) {
        do {
            for (;;) {
                if (emp->muz_flag & (1 << m)) break;
                if (++m >= 4) { m = 0; break; }
            }
            if (!AcWep_DecBullet(eop->prvp->acp, wep)) break;
            DF_SPELL2_AppearSpellPtr(wep->spelp, 2, (char)eop->prvp->acp->id, &wep->dstat, wep->actp, wep->combp, nullptr,
                                     nullptr, emp->lockmng.lock[0].dloc, &eop->prvp->ocmats[m], nullptr, 0x700,
                                     (unsigned short)wep->id);
            AbVal_AddShootRangeCount(eop->prvp->acp, wep);
            n++;
            m++;
        } while (n < eop->prvp->wep.shotcnt);
    }
    eop->blt_cnt = wep->blt_cnt;
    if (n > 0) AcCtrl_AddHeat(eop->prvp->acp, eop->prvp->acp->param.cqheatEobBusy);
}

/* original: s_eomngfunc[emp->stat] has no bounds check */
static void eo_proc(AcPartsEoSt *eop)
{
    PS2_FRAME(0x70);
    if (!eop || !(0 < eop->prvp->prmp->eo_num)) return;
    for (int i = 0; i < eop->prvp->prmp->eo_num; i++) {
        struct_7d6f2ad6 *emp = &eop->prvp->eomng[i];
        gcall(s_eomngfunc[(int)emp->stat], eop, emp);
        if (emp->count < 0x4000) emp->count++;
        if (emp->reload_count < 0x4000) emp->reload_count++;
        int sw = (emp->stat == 0 || emp->stat == 5) ? 0 : 1;
        for (int k = 0; k < 2; k++) DF_MAGIC2_Boost(emp->mag2p[k], sw);
    }
}

static void eomng_proc_closing(void)
{
}

/* fire when the locked target is still the same, its lock is complete (lock_stat & 4), the reload time has passed
 * and the pod has held the lock for 30 frames */
static void eomng_proc_lock(AcPartsEoSt *eop, struct_7d6f2ad6 *emp)
{
    PS2_FRAME(0x30);
    if (!eop) return;
    eomng_get_lock(eop, emp);
    StWepLock *lk = emp->lockmng.pri_lock;
    if (!lk) { emp->stat = 2; emp->count = 0; return; }
    void *tgt = lk->ofp;
    if (!tgt) { emp->stat = 2; emp->count = 0; return; }
    if (emp->tgt_mp != tgt) {
        emp->stat = 3;
        emp->count = 0;
        emp->tgt_mp = lk->ofp;
        return;
    }
    emp->tgt_mp = tgt;
    if ((lk->lock_stat & 4) && emp->reload_count >= eop->prvp->wep.reload_time && emp->count >= 0x1e) {
        eomng_trigger(eop, emp);
        emp->reload_count = 0;
        emp->count = 0x1e;
    }
}

/* a target held for 6 frames becomes the lock */
static void eomng_proc_check(AcPartsEoSt *eop, struct_7d6f2ad6 *emp)
{
    PS2_FRAME(0x20);
    eomng_get_lock(eop, emp);
    if (!emp->lockmng.pri_lock || !emp->lockmng.pri_lock->ofp) {
        emp->stat = 2;
        emp->count = 0;
        return;
    }
    if (++emp->count >= 6) {
        emp->stat = 4;
        emp->count = 0;
    }
}

static void eomng_proc_seek(AcPartsEoSt *eop, struct_7d6f2ad6 *emp)
{
    PS2_FRAME(0x20);
    if (!eop) return;
    eomng_get_lock(eop, emp);
    if (emp->lockmng.pri_lock && emp->lockmng.pri_lock->ofp) {
        emp->stat = 3;
        emp->count = 0;
    }
}

/* the pod's lock search from its own position, facing the AC's pitch and its yaw turned around (+ -15 degrees per
 * pod with two pods); ECM error delays the FCS like the AC's own lock */
static void eomng_get_lock(AcPartsEoSt *eop, struct_7d6f2ad6 *emp)
{
    PS2_FRAME(0x50);
    PS2_STACK(float[4], ang, 0x40);
    static const float c0001[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float yoff = 0.0f;
    AcPlayerSt *acp = eop ? eop->prvp->acp : nullptr;
    memcpy(ang, c0001, 16);
    if (!eop) return;
    AcPartsEoPrivateSt *prv = eop->prvp;
    if (prv->prmp->eo_num == 2) {
        if (&prv->eomng[0] == emp) yoff = -(prv->wep.sight.angy - 0x1.0c1524p-2f);   /* 0x3e860a92 = pi / 12 */
        else yoff = prv->wep.sight.angy - 0x1.0c1524p-2f;
    }
    float rate = acp->rateEcmError;
    if (!(rate <= 0.0f)) {
        float v = g_asmparam.ecmerr_fcs_delay * rate;
        if (!(2147483648.0f <= v)) emp->lockmng.fcs_err_time = (unsigned short)ps2_ftoi(v);
        else emp->lockmng.fcs_err_time = (unsigned short)(ps2_ftoi(v - 2147483648.0f) | 0x80000000u);
        if (acp->nolock_time > 0 && !(emp->lockmng.flag & 4)) {
            emp->lockmng.flag |= 4;
            Lock_ClearMng(&emp->lockmng, 0);
        }
    } else {
        if (emp->lockmng.flag & 4) emp->lockmng.flag &= 0xfffb;
        emp->lockmng.fcs_err_time = 0;
    }
    float add = EO_PI + yoff;
    ang[0] = eop->prvp->acp->ang[0];
    ang[1] = Add_Angle(eop->prvp->acp->ang[1], add);
    Lock_GetLockStd(&emp->lockmng, emp->objp->position, ang, &eop->prvp->wep);
}

static void eomng_proc_opening(void)
{
}

static void eomng_proc_waiting(void)
{
}

/* weapon from the core's weapon_id (lock range / angles from the core and the AC), pod objects 1.. of eobmp, their
 * boost effects (object cmats 0x16 + i) and muzzles (object cmats 0xc..0xf).
 * original: the muzzle counter (s4) is never reset, so once eo_muzzle_num muzzles are assigned every further muzzle
 * moves to the next pod (2 pods x 2 muzzles: muzzles 0, 1 -> pod 0, 2 -> pod 1, 3 -> pod 0); mag2p[1] is never set */
void aceo_Init(AcPartsEoSt *eop, AcPlayerSt *acp)
{
    PS2_FRAME(0x70);
    if (!eop) return;
    AcPartsEoPrivateSt *prv = eop->prvp;
    ZeroMemory8b(eop, 0x10);
    ZeroMemory8b(prv, 0xf30);
    eop->prvp = prv;
    prv->acp = acp;
    if (eop) {
        eop->stat = 0;
        eop->actstep = 0;
        eop->acttime = 0;
    }
    prv->acp = acp;
    prv->prmp = acp->param.corep;
    AcAsm_GetWeaponParamEx(&prv->wep, 0, 0, prv->prmp->weapon_id, 5, 0, 0, &prv->acp->param);
    _weapon_st *wep = &prv->wep;
    eop->blt_cnt = (short)wep->blt_initcnt;
    prv->isshell = (*(unsigned short *)&wep->dstat & 0x10) != 0;
    float range = (float)prv->prmp->range;
    wep->sight.range = range;
    wep->sight.org_range = range;
    float ax = ps2_div(EO_PI * prv->acp->param.eo_angx, 180.0f);
    wep->sight.lock_angx = ax;
    wep->sight.angx = ax;
    float ay = ps2_div(EO_PI * prv->acp->param.eo_angy, 180.0f);
    wep->sight.lock_angy = ay;
    wep->sight.angy = ay;
    wep->sight.lock_time = 1;
    wep->sight.lock_speed = 0;
    wep->sight.lock_max = 1;
    wep->sight.system_update_speed = 0;
    int i;
    for (i = 0; i < prv->prmp->eo_num; i++) {
        struct_7d6f2ad6 *emp = &prv->eomng[i];
        emp->objp = (ObjectSt *)((char *)prv->acp->eobmp->op + ((i + 1) << 7));
        emp->stat = 0;
        if (GAcUtil_GetObjCmat(&emp->ocmat, prv->acp->eobmp, (float)(i + 0x16), 0) == -1) {
            emp->mag2p[0] = nullptr;
            emp->mag2p[1] = nullptr;
        } else {
            AcPtsResSt *ptsres = prv->acp->frdp->ptsresp;
            _stSfxCombHead *comb = AcPtsRes_GetSfxCombHead(ptsres, 0);
            _magic2_data_st *mdp = AcPtsRes_GetMagicPtr(ptsres, 0, 6);
            emp->mag2p[0] = DF_MAGIC2_AppearBoostPtr(mdp, comb, &emp->ocmat, nullptr, 0x824);
        }
    }
    int assigned = 0, pod = 0;
    for (int m = 0; m < 4; m++) {
        int ok = 0;
        StModel *eobmp = prv->acp->eobmp;
        if (eobmp) {
            if ((unsigned)eop->nmuzzle < 4 &&
                GAcUtil_GetObjCmat(&eop->prvp->ocmats[eop->nmuzzle], eobmp, (float)(m + 0xc), 0) != -1) {
                eop->nmuzzle++;
                ok = 1;
            }
        }
        if (ok == 1) {
            assigned++;
            prv->eomng[pod].muz_flag |= (unsigned char)(1 << m);
            if (!(assigned < prv->prmp->eo_muzzle_num)) {
                if (!(++pod < 2)) pod = 0;
            }
        }
    }
    eop->prvp->wep.shotcnt = (unsigned char)(eop->prvp->wep.shotcnt * eop->prvp->prmp->eo_muzzle_num);
}

/* original: reads eop->stat before the NULL test */
void aceo_Close(AcPartsEoSt *eop)
{
    if (eop->stat != 0 && eop) {
        eop->stat = 4;
        eop->actstep = 0;
        eop->acttime = 0;
    }
}

/* original: s_eofunc[stat] is only guarded against stat < 0 (no upper bound) */
void aceo_Control(AcPartsEoSt *eop)
{
    PS2_FRAME(0x40);
    if (!eop) return;
    short step = eop->actstep, time = eop->acttime;
    if (AcUidev_IsPadTrgConf(eop->prvp->acp->frdp->uidevp, 0xd) && eop->stat == 0 &&
        !(eop->prvp->isshell ? 0 : AcCtrl_IsCharging(eop->prvp->acp))) {
        if (eop) {
            eop->stat = 1;
            eop->actstep = 0;
            eop->acttime = 0;
        }
    } else if (eop->stat == 3 && ((eop->prvp->isshell ? 0 : AcCtrl_IsCharging(eop->prvp->acp)) ||
                                  AcUidev_IsPadTrgConf(eop->prvp->acp->frdp->uidevp, 0xd))) {
        if (eop) {
            eop->stat = 4;
            eop->actstep = 0;
            eop->acttime = 0;
        }
    }
    if (eop->stat >= 0) gcall(s_eofunc[eop->stat], eop);
    eo_proc(eop);
    if (eop->actstep == step && eop->acttime == time) eop->acttime++;
}

/* pods go back to the core (actstep 0 starts them, connect advances actstep when all arrived), then waiting */
static void eoctrl_close(AcPartsEoSt *eop)
{
    PS2_FRAME(0x10);
    if (!eop) return;
    switch (eop->actstep) {
    case 0:
        for (int i = 0; i < eop->prvp->prmp->eo_num; i++) eop->prvp->eomng[i].stat = 5;
        eop->actstep++;
        /* fall through */
    case 1:
        eo_trace_connect(eop);
        break;
    default:
        if (eop) {
            eop->stat = 0;
            eop->actstep = 0;
            eop->acttime = 0;
        }
        eop->prvp->waittime = 0;
    }
}

static void eoctrl_busy(AcPartsEoSt *eop)
{
    eo_trace_target(eop);
}

static void eoctrl_charge(AcPartsEoSt *eop)
{
    PS2_FRAME(0x10);
    if (!eop) return;
    for (int i = 0; i < eop->prvp->prmp->eo_num; i++) {
        eop->prvp->eomng[i].stat = 2;
        eop->prvp->eomng[i].count = 0;
    }
    if (eop) {
        eop->stat = 3;
        eop->actstep = 0;
        eop->acttime = 0;
    }
    acse_PutLoopSeVolToneDist(eop->prvp->acp, 2, 0x3c, AC_SEparam.eo_hover_vol, 0, (float)AC_SEparam.eo_hover_dist);
}

/* actstep 0: pods start at their mounts (object matrix row 3) facing the AC's yaw, lock managers reset to the EO
 * weapon's sight; heat and the open sound. Then 15 frames following the AC, then charge. */
static void eoctrl_open(AcPartsEoSt *eop)
{
    PS2_FRAME(0x50);
    if (!eop) return;
    if (eop->actstep == 0) {
        AcPlayerSt *acp = eop->prvp->acp;
        for (int i = 0; i < eop->prvp->prmp->eo_num; i++) {
            struct_7d6f2ad6 *emp = &eop->prvp->eomng[i];
            memcpy(emp->objp->position, (*emp->objp->wrmatp)[3], 16);
            emp->objp->angle[1] = acp->ang[1];
            emp->objp->flag |= 1;
            emp->objp->noanim = 1;
            emp->stat = 1;
            Lock_InitMng(&emp->lockmng);
            Lock_SetSight(&emp->lockmng, 0, &eop->prvp->wep.sight);
            Lock_ClearMng(&emp->lockmng, 0);
            emp->lockmng.type = 2;
            emp->lockmng.id = eop->prvp->acp->id;
            emp->lockmng.ulock_mask = eop->prvp->acp->lockmng.ulock_mask;
            emp->lockmng.option_nose = 1;
        }
        AcCtrl_AddHeat(acp, acp->param.cqheatEobFirst);
        acse_PutSeVolDist(acp, 6, AC_SEparam.eo_open_vol, (float)AC_SEparam.eo_open_dist);
    }
    eo_trace_target(eop);
    eop->actstep++;
    if (!(eop->actstep < 0xf) && eop) {
        eop->stat = 2;
        eop->actstep = 0;
        eop->acttime = 0;
    }
}

/* refill: blt_add rounds every blt_frame + 2 frames up to blt_initcnt.
 * original: compares blt_frame with the waittime before the increment */
static void eoctrl_waiting(AcPartsEoSt *eop)
{
    if (!eop) return;
    AcPartsEoPrivateSt *prv = eop->prvp;
    if (!(eop->blt_cnt < (int)prv->wep.blt_initcnt)) return;
    short old = prv->waittime;
    prv->waittime = (short)(old + 1);
    if (!((int)prv->prmp->blt_frame < old)) return;
    eop->blt_cnt = (short)(eop->blt_cnt + eop->prvp->prmp->blt_add);
    if ((int)eop->prvp->wep.blt_initcnt < eop->blt_cnt) eop->blt_cnt = (short)eop->prvp->wep.blt_initcnt;
    eop->prvp->waittime = 0;
}

#ifdef ACNB_SOURCEPORT
void (*s_eofunc[5])(AcPartsEoSt *) = {eoctrl_waiting, eoctrl_open, eoctrl_charge, eoctrl_busy, eoctrl_close};
void (*s_eomngfunc[6])(AcPartsEoSt *, struct_7d6f2ad6 *) = {
    (void (*)(AcPartsEoSt *, struct_7d6f2ad6 *))eomng_proc_waiting,
    (void (*)(AcPartsEoSt *, struct_7d6f2ad6 *))eomng_proc_opening,
    eomng_proc_seek,
    eomng_proc_check,
    eomng_proc_lock,
    (void (*)(AcPartsEoSt *, struct_7d6f2ad6 *))eomng_proc_closing,
};
#endif
