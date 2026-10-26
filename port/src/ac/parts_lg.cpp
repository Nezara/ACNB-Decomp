/* src/ac/parts_lg.cpp - the laser-gun left arm (Larm_type 3): a weapon of its own (_weapon_st + _ac_weaponctrl in
 * the private part), the arm's joint controller and a small state machine run each frame by AcLgun_Control: idle ->
 * (trigger from the left arm key) equip animation -> firing (__LGunOnTrigger: random recoil, one spell per shot aimed
 * from the muzzle at the lock-on target or the far point) -> release animation. __LGunSightOn aims the arm. */
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"
#include "acnb/vu0m.h"

static const float PI_F = ps2_u2f(0x40490fdb);

_weapon_st *AcLgun_GetWeaponData(AcPartsLgunSt *lgp) { return &lgp->prvp->wep; }
AcDptsSt *AcLgun_GetDestroyInfo(AcPartsLgunSt *lgp) { return &lgp->prvp->dpts; }
_ac_jointctrl *AcLgun_GetJointCtrl(AcPartsLgunSt *lgp) { return &lgp->prvp->jctrl; }
_ac_weaponctrl *AcLgun_GetWeaponCtrl(AcPartsLgunSt *lgp) { return &lgp->prvp->wctrl; }

/* release animation, arm back to the AC's animation (use / equip stay set) */
void AcLgun_ReleaseWeapon(AcPartsLgunSt *lgp)
{
    PS2_FRAME(0x30);
    if (!lgp->flag.use) return;
    AcPartsLgunPrivateSt *p = lgp->prvp;
    AcPlayerSt *acp = p->acp;
    p->cc = 0;
    lgp->prvp->cc_max = 0xf;
    weapon_changeanim(&p->wctrl, p->wctrl.wp->release_anmid, 0xa, 0);
    for (int obj = 5; obj <= 8; obj++) Anim_MakeGroup(acp->mp, obj, 9);
    acjcon_SetControl(&p->jctrl, 0, 0);
    AcAnim_SynchroAnim(acp, 3, 8, 0x3c);
    p->step = 0;
}

int AcLgun_IsRemovePossible(AcPartsLgunSt *lgp)
{
    if (!lgp) return 0;
    if (!lgp->flag.use || !lgp->flag.equip) return 0;
    return lgp->prvp->step == 0;
}

/* the lock-on target (laser-gun lock slot 12) or the far point along the view */
static void lgun_target(float *trg, AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    StWepLock *lk = &acp->lockmng.lock[12];
    if (wctrlp->wp->prog_no != 1 && lk->ofp && (lk->lock_stat & 2)) {
        memcpy(trg, lk->dloc, 16);
        if (trg[3] == 0.0f) acwep_GetFarPos(trg, acp, -acp->view.ang[0], acp->ang[1]);
    } else {
        acwep_GetFarPos(trg, acp, -acp->view.ang[0], acp->ang[1]);
    }
}

/* joint callback: aim the arm joints at the target */
static void __LGunSightOn(AcPlayerSt *acp, _ac_jointctrl *jctrlp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x50);
    PS2_STACK(float[4], trg, 0x40);
    if (jctrlp->actstep == 0) {
        acjcon_ResetReaction(acp, 8, 0x1e);
        jctrlp->actstep++;
    }
    lgun_target(trg, acp, wctrlp);
    acjcon_Play(acp, trg, &JconFileHeadp->jconheadtbl[wctrlp->wp->jcon_datano]);
}

/* recoil reaction of the arm joints after a volley (scaled by the arm damper); the release wait covers the longest
 * reaction */
static void __LGunShotCallBack(AcPlayerSt *acp, AcPartsLgunSt *lgp)
{
    _weapon_st *wp = lgp->prvp->wctrl.wp;
    int n = wp->jcon_datano;
    if (n == 0) return;
    JconHeadSt *head = &JconFileHeadp->jconheadtbl[n];
    short maxn = 0;
    struct_38f86b4e *d = head->jcondattbl;
    for (int i = 0; i < head->nobj; i++, d++) {
        int k = d->control_objid;
        if (k >= 0x10) k -= 0xc;
        struct_38955935 *jr = &acp->jointreact[k];
        if (acp->op[k].group & 8) jr->reactdamper = acp->param.arm_damper;
        else jr->reactdamper = 1.0f;
        jr->reaction_time = 0;
        jr->reaction_startframe = (short)ps2_ftoi((float)wp->react_startframe * acp->param.arm_damper);
        jr->reaction_nframe = (short)ps2_ftoi((float)wp->react_nframe * acp->param.arm_damper);
        jr->angdif[0] = d->react_xang;
        if (maxn < jr->reaction_nframe) maxn = jr->reaction_nframe;
    }
    lgp->prvp->cc = 0;
    lgp->prvp->cc_max = (char)(maxn + 0xf);
}

/* (0x7fff - (u16)rand()) / 32767 */
static inline float lgun_rand1(void) { return (float)(0x7fff - (unsigned short)ps2_rand()) / 32767.0f; }

/* one volley (once per wait): random recoil (pitch by the arm's recoil gap, random roll), then per shot a spell from
 * the muzzle toward the target, deflected by the recoil; afterwards the recoil / burst / reload counters. Returns the
 * shots fired. */
static int __LGunOnTrigger(AcPlayerSt *acp, AcPartsLgunSt *lgp)
{
    PS2_FRAME(0x1a0);
    PS2_STACK(float[4], trg2, 0x190);
    AcPartsLgunPrivateSt *p = lgp->prvp;
    _weapon_st *wp = p->wctrl.wp;
    _ac_weaponctrl *wc = &p->wctrl;
    p->wait_time--;
    if (p->wait_time > 0) return 0;
    float v;
    if (ps2_rand() % 10 < 3) {
        v = lgun_rand1();
    } else {
        float a = lgun_rand1();
        float b = lgun_rand1();
        v = a * b;
    }
    float rot[3];
    if (wc->wp->jpos == 1) rot[0] = v * acp->param.gap_angle_by_recoil_r;
    else if (wc->wp->jpos == 2) rot[0] = v * acp->param.gap_angle_by_recoil_l;
    else rot[0] = 0.0f;
    rot[1] = 0.0f;
    rot[2] = PI_F * lgun_rand1();
    float m[4][4] __attribute__((aligned(16))), m2[4][4] __attribute__((aligned(16)));
    sceVu0RotMatrixX(m, IdentityMtx, rot[0]);
    sceVu0RotMatrixZ(m, m, rot[2]);
    int n;
    for (n = 0; n < wp->shotcnt; n++) {
        int idx = n < wc->nmuzzle ? n : 0;
        if (!AcWep_DecBullet(acp, wp)) break;
        lgun_target(trg2, acp, wc);
        ObjCMatSt *ocm = &wc->ocmats[idx];
        float mpos[4], vec[4], ang[4];
        vu_apply(mpos, (const float (*)[4])*ocm->parentmatp, ocm->mat[3]);
        vec[0] = trg2[0] - mpos[0];
        vec[1] = trg2[1] - mpos[1];
        vec[2] = trg2[2] - mpos[2];
        vec[3] = trg2[3];
        vec2ang(ang, vec);
        ang[0] = -ang[0];
        sceVu0RotMatrixZ(m2, IdentityMtx, ang[2]);
        sceVu0RotMatrixX(m2, m2, -ang[0]);
        sceVu0RotMatrixY(m2, m2, ang[1]);
        float z = vec[2];
        vec[0] = 0.0f;
        vec[1] = 0.0f;
        vec[2] = ps2_sqrt(z * z + (vec[0] * vec[0] + vec[1] * vec[1]));   /* the shot length is |dz| */
        vu_apply(vec, (const float (*)[4])m, vec);
        vu_apply(vec, (const float (*)[4])m2, vec);
        trg2[0] = vec[0] + mpos[0];
        trg2[1] = vec[1] + mpos[1];
        trg2[2] = vec[2] + mpos[2];
        trg2[3] = vec[3];
        DF_SPELL2_AppearSpellPtr(wp->spelp, 2, (char)acp->id, &wp->dstat, wp->actp, wp->combp, nullptr, nullptr, trg2,
                                 ocm, nullptr, 0x200, (unsigned short)wp->id);
        AbVal_AddShootRangeCount(acp, wp);
        CombatRecord_OnShoot(acp, (unsigned short)wp->id);
        AcWep_AppearShell(acp, wc, wp->magcp, wp->combp);
    }
    if (n > 0) {
        weapon_setanim(wc, wp->shot_anmid, 0);
        __LGunShotCallBack(acp, lgp);
    }
    if (wc->wp->jpos == 2) acp->param.gap_angle_by_recoil_l = wc->wp->recoil;
    else if (wc->wp->jpos == 1) acp->param.gap_angle_by_recoil_r = wc->wp->recoil;
    acp->param.recoil_size = wc->wp->recoil;
    acp->param.recoil_timecnt = acp->param.recoilctrl_time;
    p->burst_cnt++;
    if (p->burst_cnt < wp->autocnt) {
        p->wait_time = wp->autotime;
        return n;
    }
    wc->cntBurstShot++;
    if (wc->cntBurstShot < wp->burst_shotcnt) {
        wc->busytime = wp->breload_time;
        wc->fcntAcptBurst = wp->reload_time;
    } else {
        wc->busytime = wp->reload_time;
        wc->cntBurstShot = 0;
        wc->fcntAcptBurst = 0;
    }
    p->burst_cnt = 0;
    p->wait_time = 1;
    p->first = 0;
    return n;
}

/* the left arm key fires: keys on, ammunition, not in cannon stance, not reloading, energy (energy weapons); trigger
 * weapons on the press, the others while held */
static int __CheckTriggerEnable(AcPlayerSt *acp, AcPartsLgunSt *lgp)
{
    PS2_FRAME(0x30);
    AcPartsLgunPrivateSt *p = lgp->prvp;
    int ok = 0;
    if (acp->key_enable && p->wep.blt_cnt != 0 && acp->actno != 0x10 && p->wctrl.busytime == 0) {
        if (!p || !(p->wep.dstat.effect & 0x20)) ok = 1;
        else if (AcCtrl_IsCharging(acp)) ok = 0;
        else ok = !(acp->energyReal < p->wep.blt_price);
    }
    if (!ok) return 0;
    if (p->wep.trigshot) return AcUidev_IsPadTrgConf(acp->frdp->uidevp, 7) != 0;
    return AcUidev_IsPadOnConf(acp->frdp->uidevp, 7) != 0;
}

/* idle -> equip (arm groups to the gun, aim) -> fire -> release after the key is let go / the wait */
static int __LGunManage(AcPlayerSt *acp, AcPartsLgunSt *lgp)
{
    PS2_FRAME(0x60);
    AcPartsLgunPrivateSt *p = lgp->prvp;
    _ac_weaponctrl *wc = &p->wctrl;
    switch (p->step) {
    case 0: {
        unsigned int cnt = wc->cntBurstShot;
        if (cnt > 0 && (int)cnt < wc->wp->burst_shotcnt) {
            wc->fcntAcptBurst--;
            if (wc->fcntAcptBurst == 0) wc->cntBurstShot = 0;
        }
        if (!__CheckTriggerEnable(acp, lgp)) break;
        if (lgp->flag.trigger != 1) break;
        lgp->prvp->wctrl.actstep = 0;
        lgp->prvp->wctrl.acttime = 0;
        _ac_weaponctrl *w = &lgp->prvp->wctrl;
        lgp->prvp->cc = 0;
        lgp->prvp->cc_max = 0x19;
        for (int obj = 5; obj <= 8; obj++) Anim_MakeGroup(acp->mp, obj, 8);
        acjcon_SetControl(&lgp->prvp->jctrl, (void *)GFN(__LGunSightOn), w);
        weapon_changeanim(w, w->wp->equip_anmid, 0xa, 0);
        p->step = (char)(p->step + 1);
        lgp->flag.trigger = 0;
        break;
    }
    case 1:
        if (!(p->cc_max < p->cc)) {
            p->cc = (char)(p->cc + 1);
        } else {
            p->wait_time = 1;
            p->burst_cnt = 0;
            p->first = 1;
            p->step = 2;
        }
        break;
    case 2: {
        if (p->first != 0 || __CheckTriggerEnable(acp, lgp)) {
            __LGunOnTrigger(acp, lgp);
            break;
        }
        int lim = lgp->prvp->cc_max;
        if (acp->actno == 0x10) lim -= 0x11;
        else if (!wc->wp->trigshot && AcUidev_IsPadOnConf(acp->frdp->uidevp, 7)) break;
        if (p->cc < lim) {
            p->cc = (char)(p->cc + 1);
            break;
        }
        AcPartsLgunPrivateSt *q = lgp->prvp;
        q->cc = 0;
        lgp->prvp->cc_max = 0xf;
        weapon_changeanim(&q->wctrl, q->wctrl.wp->release_anmid, 0xa, 0);
        p->step = 3;
        break;
    }
    case 3:
        for (int obj = 5; obj <= 8; obj++) Anim_MakeGroup(acp->mp, obj, 9);
        acjcon_SetControl(&p->jctrl, 0, 0);
        if (acp->actno == 0x10) AcAnim_SynchroAnim(acp, 1, 8, 0x3c);
        else AcAnim_SynchroAnim(acp, 3, 8, 0x3c);
        p->step = 0;
        break;
    }
    return 1;
}

/* the laser gun is lost (single player part removal) */
void AcLgun_Remove(AcPartsLgunSt *lgp)
{
    PS2_FRAME(0x20);
    if (!lgp || !lgp->flag.use || !lgp->flag.equip) return;
    lgp->flag.equip = 0;
    AcPartsLgunPrivateSt *p = lgp->prvp;
    p->cc = 0;
    lgp->prvp->cc_max = 0xf;
    weapon_changeanim(&p->wctrl, p->wctrl.wp->release_anmid, 0xa, 0);
    lgp->flag.use = 0;
    Lock_SetSight(&lgp->prvp->acp->lockmng, 1, 0);
    Lock_ClearMng(&lgp->prvp->acp->lockmng, 1);
}

void AcLgun_Trigger(AcPartsLgunSt *lgp, int trig_flag)
{
    if (lgp->flag.use && lgp->flag.equip && lgp->flag.trigger != 1) lgp->flag.trigger = trig_flag & 1;
}

/* per frame: lock-on request, the gun model's animation, then the state machine (pad on) and the weapon counters */
void AcLgun_Control(AcPartsLgunSt *lgp)
{
    PS2_FRAME(0x40);
    if (!lgp->flag.use) return;
    _ac_weaponctrl *wc = &lgp->prvp->wctrl;
    AcWep_GetStdLock(lgp->prvp->acp, wc, 0);
    StModel *m = lgp->prvp->wctrl.mp[0];
    AcPlayerSt *acp = lgp->prvp->acp;
    if (m && m != acp->mp) {
        Anim_Update(m);
        Anim_UpdateChildObjMatrix(m->op, m->matrix2);
    }
    if (!lgp->flag.equip || MyI.padoff == 1) return;
    short step = wc->actstep;
    __LGunManage(lgp->prvp->acp, lgp);
    if (wc->actstep == step && wc->acttime < 0x4000) wc->acttime++;
    if (wc->busytime > 0) wc->busytime--;
}

/* set up the laser gun from the arm part (type 3); other arms leave it unused. The wipe is the original's
 * ZeroMemory8b(lgp, 0x380): it runs from the public half, so the private part's last 0x10 bytes (step, cc, cc_max,
 * burst_cnt, wait_time, first, acp) are not cleared. */
void AcLgun_Init(AcPartsLgunSt *lgp, PartsParamSwordSt *paramp, AcPlayerSt *acp)
{
    PS2_FRAME(0x50);
    if (!lgp) return;
    AcPartsLgunPrivateSt *p = lgp->prvp;
    ZeroMemory8b(lgp, 8);
    ZeroMemory8b(lgp, 0x380);
    lgp->prvp = p;
    lgp->prvp->acp = acp;
    if (!paramp || paramp->type != 3) return;
    lgp->flag.use = 1;
    lgp->flag.equip = 1;
    StModel *mdl = acp->modeltbl[4];
    AcAsm_GetWeaponParamEx(&lgp->prvp->wep, 6, 0, paramp->weapon_id, 5, 0, 0, &acp->param);
    lgp->prvp->wep.jpos = 2;
    p = lgp->prvp;
    p->wctrl.wp = &p->wep;
    _ac_weaponctrl *wc = &p->wctrl;
    p->wctrl.mp[0] = mdl;
    p->wctrl.subspells = 0;
    if (mdl && mdl != acp->mp) init_weaponmodel(mdl);
    get_allmuzzle(wc);
    GAcUtil_GetObjCmat(&wc->ocmatsCartridge[0], wc->mp[0], 22.0f, 0);
    weapon_setanim(wc, 0, 0);
    Lock_SetSight(&acp->lockmng, 1, &wc->wp->sight);
    Lock_ClearMng(&acp->lockmng, 1);
}
