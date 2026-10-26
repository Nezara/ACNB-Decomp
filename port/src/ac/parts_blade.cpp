/* src/ac/parts_blade.cpp - left-hand melee weapon of an AC (OLSORTIE overlay): the laser blade (sword part,
 * param.swordp) or the arm-mounted "W blade" (param.abl_Wblade, blade fields of param.armsp; wb_kind 1 = the double
 * swing triggered by the weapon-arm button through the _ac_weaponctrl callbacks below). Owns the actions "ground
 * blade" (0xb / 0xe) and "fly blade" (0xd / 0xf) and the blade freeze, the blade's hit collision (DF_COLL,
 * HitBladeCallback), the blade wave projectile (magic sword spell), the blade parameters (damage, heat, reload,
 * range, hit effect, lock sight) and the blade tracer (a ribbon history kept in a Magic_GetModel buffer).
 */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"
#include "acnb/vu0m.h"

static const float BLADE_PI = ps2_u2f(0x40490fdb);

/* weapon-control slot store of a function with a different (but register-compatible) prototype */
#define WSLOT(slot, f) ((slot) = (decltype(slot))(void *)GFN(f))

static inline AcPartsBladePrivateSt *blade_prv(AcPlayerSt *acp) { return AcCtrl_GetPartsBlade(acp)->prvp; }

/* cvt.w.s of a float that MWCC converts to unsigned (values >= 2^31 go through the subtract-and-or path) */
static inline unsigned int blade_ftou(float f)
{
    if (f >= 2147483648.0f) return (unsigned int)ps2_ftoi(f - 2147483648.0f) | 0x80000000u;
    return (unsigned int)ps2_ftoi(f);
}

/* blade lock sight from the FCS (inlined twice: acblade_Init / acblade_Pickup) */
static inline void blade_sight_setup(AcPartsBladeSt *bladep)
{
    AcPartsBladePrivateSt *prv = bladep->prvp;
    WepSightSt *s = &prv->blade_sight;
    PartsParamFcsSt *fcsp = prv->acp->param.fcsp;
    memset(s, 0, 0x30);
    s->type = 2;
    s->sight = 4;
    s->flag = 0;
    s->range = fcsp->bladelock_range;
    float a = BLADE_PI * (float)(int)fcsp->bladelock_angx / 180.0f;
    s->lock_angx = a;
    s->angx = a;
    a = BLADE_PI * (float)(int)fcsp->bladelock_angy / 180.0f;
    s->lock_angy = a;
    s->angy = a;
    s->lock_time = 0;
    s->lock_max = 1;
    s->lock_speed = 0;
    s->system_update_speed = 0;
}

/* nextact request for the W blade's second swing (Info: count the blade use for player 0) */
static inline int blade_request_next(AcPlayerSt *acp, unsigned short act)
{
    AcCtrl_GetPartsBlade(acp);   /* result unused */
    unsigned short old = acp->nextact;
    acp->nextact = act;
    if (old != act && acp->id == 0) Info_IncData(0, 2, 0, 0);
    return 1;
}

/* per-frame hook (acsfx_Control) */
void AcBlade_UpdateBladeTracer(AcPlayerSt *acp)
{
    __UpdateBladeTracer(acp);
}

/* blade action exit: stop the blade model animations, tracer off */
void AcBlade_ExitAct(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    AcPartsBladePrivateSt *prv = AcCtrl_GetPartsBlade(acp)->prvp;
    AcPlayerSt *a = prv->acp;
    if (a->modeltbl[4]) Anim_ChangeAnimNoInc(a->modeltbl[4], 0, 0x78, 0);
    if (a->param.abl_Wblade && a->modeltbl[5]) Anim_ChangeAnimNoInc(a->modeltbl[5], 0, 0x78, 0);
    prv->isTrace = 0;
}

/* freeze after a blade swing: brake, keys locked for 8 frames, stop at frame 40 */
int acblade_ActBladeFreeze(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    AcCtrl_BrakeXz(acp, acp->param.dash_brake);
    AcAnim_ChangeAnim(acp, 0, 500, 0);
    if (acp->acttime < 8) {
        acp->key_enable = 0;
    } else if (acp->acttime >= 0x28) {
        AcCtrl_GetPartsBlade(acp)->prvp->wb_kind = 0;
        ACsetact_stop(acp);
    }
    return 1;
}

/* swing state 0: set up the swing (the magic-sword pad test's result is unused) */
static inline void blade_swing_init(AcPlayerSt *acp, AcPartsBladePrivateSt *prv, int anim, unsigned short nextact)
{
    prv->key_enable = acp->param.abl_Wblade ? 1 : 0;
    prv->anim = anim;
    prv->wb_lr = 0;
    acp->nextact = nextact;
    acp->actstep = 1;
    prv->wb_cnt = 0;
    prv->bacttime = 1;
    prv->isTrace = 1;
    prv->appeartime = 0xb;
    prv->erasetime = 0x23;
    prv->curbwvp = (prv->wb_kind == 1 && acp->aw_type == 1) ? &prv->bwv[1] : &prv->bwv[0];
    if (acp->param.abl_magicsword) AcUidev_IsPadOnConf(acp->frdp->uidevp, 5);   /* result unused */
}

/* first frame of a swing: weapon released, collision off, blade effect on, blade lock from the AC's centre height,
 * combat record "shot" with the blade's weapon id (gnd also clears the hit count) */
static inline void blade_swing_start(AcPlayerSt *acp, AcPartsBladeSt *bladep, AcPartsBladePrivateSt *prv,
                                     AcEffSt *effp, int gnd)
{
    if (!acp->wep_release) AcWep_ReleaseWeapon(acp, acp->equip_weapon);
    SetHitBlade(acp, prv->wb_lr, 0);
    if (gnd) prv->hitcnt = 0;
    prv->magic_enable = 1;
    aceff_AppearBlade(effp, prv->wb_lr);
    AcCtrl_GetPartsBlade(acp);   /* result unused */
    PS2_STACK(float[4], pos, 0x90);
    PS2_STACK(float[4], ang, 0x80);   /* ang[2], ang[3] are never written (stale stack) */
    memcpy(pos, acp->loc, 16);
    pos[1] = pos[1] - 0.5f * acp->mp->tall;
    ang[0] = acp->view.ang[0];
    ang[1] = Add_Angle(acp->ang[1], BLADE_PI);
    Lock_GetLockBld(&acp->lockmng, pos, ang, &bladep->prvp->blade_sight);
    unsigned short wid;
    if (prv->acp->param.abl_Wblade) {
        if (prv->wb_kind == 1) wid = acp->aw_type ? prv->armsp->weapon2_id : prv->armsp->weapon_id;
        else wid = prv->armsp->weapon_id;
    } else {
        wid = prv->swordp->weapon_id;
    }
    CombatRecord_OnShoot(acp, wid);
}

static inline void blade_model_anim(AcPartsBladeSt *bladep)
{
    AcPlayerSt *a = bladep->prvp->acp;
    if (a->modeltbl[4]) Anim_ChangeAnimNoInc(a->modeltbl[4], 1, 0x3c, 0);
    if (a->param.abl_Wblade && a->modeltbl[5]) Anim_ChangeAnimNoInc(a->modeltbl[5], 1, 0x3c, 0);
}

/* tracer history: previous / current blade tip */
static inline void blade_push_pos(AcPlayerSt *acp, int lr)
{
    AcPartsBladePrivateSt *p = AcCtrl_GetPartsBlade(acp)->prvp;
    struct_3956eba2 *b = aceff_GetBlade(AcCtrl_GetEffect(acp), lr);
    memcpy(p->bladepos[1], p->bladepos[0], 16);
    memcpy(p->bladepos[0], b->mat[3], 16);
}

static inline void blade_swing_se(AcPlayerSt *acp)
{
    acse_PutSeVolDist(acp, acp->param.partsid[13] == 5 ? 0x50 : 0x1c, AC_SEparam.blade_vol,
                      (float)AC_SEparam.blade_dist);
}

/* fly blade swing (actions 0xd / 0xf): states 0-1 wind-up, 2 hit frames (collision, blade wave), 3 follow-through
 * (second W blade swing if queued), 4 wait for the animation; turns towards a locked target (deflection lead).
 * `frame` is the op frame, 0 in states 0 / 1. */
int acblade_ActFlyBlade(AcPlayerSt *acp)
{
    PS2_FRAME(0xe0);
    AcPartsBladeSt *bladep = AcCtrl_GetPartsBlade(acp);
    AcPartsBladePrivateSt *prv = bladep->prvp;
    AcEffSt *effp = AcCtrl_GetEffect(acp);
    blade_push_pos(acp, prv->wb_lr);
    int frame = acp->op[1].frame;
    blade_model_anim(bladep);
    switch (acp->actstep) {
    case 0:
        blade_swing_init(acp, prv, 0x32, 8);
        /* fall through */
    case 1:
        frame = 0;
        if (prv->bacttime == 1) blade_swing_start(acp, bladep, prv, effp, 0);
        AcAnim_ChangeAnimFrameStop(acp, prv->anim, 0, 0x3c, 0);
        if ((int)prv->flybld_wait < prv->bacttime) {
            prv->wb_cnt++;
            AcAnim_ChangeAnim(acp, prv->anim, 0x1e, 0);
            acp->actstep = 2;
            prv->blade_busytime = prv->blade_reloadtime;
            if (prv->wb_lr == 0 && acp->param.abl_Wblade) prv->blade_busytime = 0;
        }
        break;
    case 2:
        if (frame >= acp->param.typedatap->flybldhit_startfrm) {
            if (prv->wb_kind != 1) SetHitBlade(acp, prv->wb_lr, 1);
            if (prv->magic_enable && (acp->param.Larm_type == 1 || prv->wb_kind == 1) && prv->curbwvp->spelp &&
                AppearBladeWave(bladep, prv->wb_lr))
                prv->magic_enable = 0;
        }
        if (frame >= acp->param.typedatap->flybldhit_endfrm + 1) {
            Lock_ClearMng(&acp->lockmng, 0);
            SetHitBlade(acp, prv->wb_lr, 0);
            acp->actstep = 3;
        }
        break;
    case 3:
        if (frame >= 0x13) {
            prv->key_enable = 0;
            if (prv->wb_lr == 0 && acp->nextact == 0xf && AcCtrl_EnergyDrain(acp, prv->boost_blade_sub)) {
                AcAnim_ChangeAnimFrameStop(acp, 0, 0, 0x3c, 0);
                prv->wb_lr = 1;
                prv->anim = 0x43;
                acp->actstep = 1;
                acp->acttime = 0;
                acp->nextact = 8;
                prv->bacttime = -1;
                prv->appeartime = 10;
                prv->erasetime = 0x1e;
            } else {
                acp->actstep = 4;
            }
        }
        break;
    case 4:
        if (Anim_IsEndAnim(acp->mp, prv->anim)) AC_SetAction(acp, 8);
        prv->bacttime = 0;
        break;
    }
    acp->key_enable = prv->key_enable;
    if (frame == 8) blade_swing_se(acp);
    if (prv->appeartime < prv->bacttime && prv->bacttime < prv->erasetime && prv->wb_kind != 1)
        AcCtrl_DispBlade(acp, prv->wb_lr, prv->erasetime - prv->bacttime + 1);
    StWepLock *lk = &acp->lockmng.lock[0];
    if (lk->ofp && (lk->lock_stat & 2)) {
        PS2_STACK(float[4], ploc, 0xd0);
        PS2_STACK(float[4], eloc, 0xc0);
        PS2_STACK(float[4], evec, 0xb0);
        PS2_STACK(float[4], dloc, 0xa0);
        memcpy(ploc, acp->loc, 16);
        ploc[1] = ploc[1] - 0.5f * acp->tall;
        StModel *ofp = lk->ofp;
        memcpy(eloc, ofp->loc, 16);
        eloc[1] = eloc[1] - (AC_TempParam.flyblade_yofs + 0.5f * ofp->tall);
        evec[0] = eloc[0] - lk->loc[0];   /* vsub.xyz + full store: w = eloc.w */
        evec[1] = eloc[1] - lk->loc[1];
        evec[2] = eloc[2] - lk->loc[2];
        evec[3] = eloc[3];
        DF_get_deflection(dloc, NULL, eloc, evec, ploc, acp->param.flymv_maxspd, 5);
        memcpy(lk->loc, eloc, 16);
        float a = BLADE_PI + ps2_atan2f(dloc[0] - acp->loc[0], dloc[2] - acp->loc[2]);
        while (!(a <= BLADE_PI)) a = a - ps2_u2f(0x40c90fdb);
        acp->ang[1] = go_angf(acp->ang[1], a, acp->param.turn_maxspd * AcAsm_GetArmOwRate(&acp->param));
    }
    if (!AcCtrl_IsCharging(acp) && AcUidev_IsPadOnConf(acp->frdp->uidevp, 5)) {
        AcCtrl_DispBackBoostFire(acp);
        AcCtrl_DispLegBoostFire(acp);
        AddF_min(&acp->velocity[1], -acp->param.flyup_accel, -acp->param.flyup_maxspd);
    } else {
        AcCtrl_AccelFall(acp, acp->param.gravity, acp->param.fall_maxspd);
    }
    AcCtrl_BrakeXz(acp, acp->param.flymv_brk);
    prv->bacttime++;
    return 1;
}

/* ground blade swing (actions 0xb / 0xe): like the fly swing, plus a forward lunge (blade_spd0) at the end of the
 * wind-up and braking while the forward key is not held; turns towards a locked target */
int acblade_ActGndBlade(AcPlayerSt *acp)
{
    PS2_FRAME(0xf0);
    AcPartsBladeSt *bladep = AcCtrl_GetPartsBlade(acp);
    AcPartsBladePrivateSt *prv = bladep->prvp;
    AcEffSt *effp = AcCtrl_GetEffect(acp);
    blade_push_pos(acp, prv->wb_lr);
    int frame = acp->op[1].frame;
    switch (acp->actstep) {
    case 0:
        blade_swing_init(acp, prv, 0x31, 0xc);
        /* fall through */
    case 1:
        AcCtrl_BrakeXz(acp, acp->param.dash_brake);
        if (prv->bacttime == 1) blade_swing_start(acp, bladep, prv, effp, 1);
        AcAnim_ChangeAnimFrameStop(acp, prv->anim, 0, 0x3c, 0);
        frame = 0;
        if ((int)prv->gndbld_wait < prv->bacttime) {
            prv->wb_cnt++;
            AcAnim_ChangeAnim(acp, prv->anim, 0x1e, 0);
            PS2_STACK(float[4][4], rmat, 0xb0);
            sceVu0RotMatrixY(rmat, IdentityMtx, acp->ang[1]);
            acp->velocity[0] = -rmat[2][0] * acp->param.blade_spd0;
            acp->velocity[1] = 0.0f;
            acp->velocity[2] = -rmat[2][2] * acp->param.blade_spd0;
            acp->velocity[3] = 1.0f;
            float rate = AcAsm_GetSpeedOwRate(&acp->param);
            acp->velocity[0] = acp->velocity[0] * rate;   /* vmulx.xyz: w keeps 1.0f */
            acp->velocity[1] = acp->velocity[1] * rate;
            acp->velocity[2] = acp->velocity[2] * rate;
            acp->actstep = 2;
            prv->blade_busytime = prv->blade_reloadtime;
            if (prv->wb_lr == 0 && acp->param.abl_Wblade) prv->blade_busytime = 0;
        }
        break;
    case 2:
        if (frame >= acp->param.typedatap->gndbldhit_startfrm) {
            if (prv->wb_kind != 1) SetHitBlade(acp, prv->wb_lr, 1);
            if (prv->magic_enable &&
                ((acp->param.abl_magicsword && AcUidev_IsPadOnConf(acp->frdp->uidevp, 5)) ||
                 acp->param.Larm_type == 1 || prv->wb_kind == 1) &&
                prv->curbwvp->spelp && AppearBladeWave(bladep, prv->wb_lr))
                prv->magic_enable = 0;
        }
        if (frame >= acp->param.typedatap->gndbldhit_endfrm + 1) {
            Lock_ClearMng(&acp->lockmng, 0);
            SetHitBlade(acp, prv->wb_lr, 0);
            acp->actstep = 3;
        } else if (frame >= acp->param.typedatap->gndbldhit_startfrm &&
                   !AcUidev_IsPadOnConf(acp->frdp->uidevp, 0)) {
            AcCtrl_BrakeXz(acp, acp->param.blade_brk);
        }
        break;
    case 3:
        if (!AcUidev_IsPadOnConf(acp->frdp->uidevp, 0)) AcCtrl_BrakeXz(acp, acp->param.blade_brk);
        if (frame >= 0x13) {
            acp->velocity[0] = 0.0f;
            acp->velocity[1] = 0.0f;
            acp->velocity[2] = 0.0f;
            acp->velocity[3] = 1.0f;
            prv->key_enable = 0;
            if (prv->wb_lr == 0 && acp->nextact == 0xe &&
                ((prv->wb_kind == 1 && prv->bwv[1].boostsub < acp->energyReal) ||
                 (prv->wb_kind != 1 && AcCtrl_EnergyDrain(acp, prv->boost_blade_sub)))) {
                prv->wb_lr = 1;
                prv->anim = 0x42;
                acp->actstep = 1;
                acp->nextact = 0;
                prv->bacttime = -1;
                prv->appeartime = 10;
                prv->erasetime = 0x1e;
            } else {
                acp->actstep = 4;
            }
            acp->acttime = 0;
        }
        break;
    case 4:
        prv->bacttime = 0;
        break;
    }
    blade_model_anim(bladep);
    acp->key_enable = prv->key_enable;
    if (acp->actstep < 3) {
        AcCtrl_DispBackBoostFire(acp);
        AcCtrl_DispLegBoostFire(acp);
    }
    if (frame == 8) blade_swing_se(acp);
    if (prv->appeartime < prv->bacttime && prv->bacttime < prv->erasetime && prv->wb_kind != 1)
        AcCtrl_DispBlade(acp, prv->wb_lr, prv->erasetime - (prv->bacttime - 2));
    StWepLock *lk = &acp->lockmng.lock[0];
    if (lk->ofp && (lk->lock_stat & 2)) {
        PS2_STACK(float[4], trgvec, 0xa0);
        trgvec[0] = lk->ofp->loc[0] - acp->loc[0];
        trgvec[2] = lk->ofp->loc[2] - acp->loc[2];
        float a = ps2_atan2f(trgvec[0], trgvec[2]) + BLADE_PI;
        while (!(a <= BLADE_PI)) a = a - ps2_u2f(0x40c90fdb);
        acp->ang[1] = go_angf(acp->ang[1], a, acp->param.turn_maxspd * AcAsm_GetArmOwRate(&acp->param));
        while (!(acp->ang[1] <= BLADE_PI)) acp->ang[1] = acp->ang[1] - ps2_u2f(0x40c90fdb);
        while (acp->ang[1] < -BLADE_PI) acp->ang[1] = acp->ang[1] + ps2_u2f(0x40c90fdb);
    }
    if (acp->actstep == 4 && (prv->bacttime > 0x78 || Anim_IsEndAnim(acp->mp, prv->anim))) AC_SetAction(acp, 0xc);
    prv->bacttime++;
    return 1;
}

int acblade_ActSetFlyWblade(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    if (AcCtrl_GetPartsBlade(acp)->prvp->wb_kind != 0) return 0;
    return blade_request_next(acp, 0xf);
}

int acblade_ActSetFlyBlade(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    AcCtrl_GetPartsBlade(acp)->prvp->wb_kind = 0;
    return SetActFlyBlade(acp);
}

int acblade_ActSetGndWblade(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    if (AcCtrl_GetPartsBlade(acp)->prvp->wb_kind != 0) return 0;
    return blade_request_next(acp, 0xe);
}

int acblade_ActSetGndBlade(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    AcCtrl_GetPartsBlade(acp)->prvp->wb_kind = 0;
    return SetactGndBlade(acp);
}

/* W blade as an arm weapon: callbacks of ac_weaponctrl_init (acp unused) */
void acblade_WbladeInit(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    (void)acp;
    WSLOT(wctrlp->onequipfunc, onequip_wblade);
    WSLOT(wctrlp->onreleasefunc, dummyfunc_wblade);
    WSLOT(wctrlp->managefunc, manage_wblade);
    WSLOT(wctrlp->ontrigfunc, ontrigger_wblade);
    WSLOT(wctrlp->shotcbfunc, dummyfunc_wblade);
    WSLOT(wctrlp->onfreefunc, dummyfunc_wblade);
    wctrlp->wp->blt_cnt = 9999;
}

static int dummyfunc_wblade(void)
{
    return 1;
}

static int onequip_wblade(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    (void)acp;
    PS2_FRAME(0x10);
    acwep_SetFunc(wctrlp, (void *)GFN(manage_wblade));
    return 1;
}

/* weapon-arm button: start a W blade swing (wb_kind 1), or queue the second swing during one */
static int ontrigger_wblade(AcPlayerSt *acp)
{
    PS2_FRAME(0x50);
    AcPartsBladeSt *bladep = AcCtrl_GetPartsBlade(acp);
    AcPartsBladePrivateSt *prv = bladep->prvp;
    AcPlayerSt *a = prv->acp;
    int ready = 0;
    if (prv->blade_dstat.damage != 0 && !AcCtrl_IsCharging(a) && bladep->prvp->boost_blade_sub < a->energyReal &&
        bladep->prvp->blade_busytime == 0 && a->rb_stat == 0 && a->actno != 0x10)
        ready = 1;
    if (!ready) return 0;
    if (!(acp->actstat & 1)) {
        if (acp->actno != 0xb) {
            prv->wb_kind = 1;
            SetactGndBlade(acp);
        } else if (prv->wb_kind == 1) {
            blade_request_next(acp, 0xe);
        }
    } else {
        if (acp->actno != 0xd) {
            prv->wb_kind = 1;
            SetActFlyBlade(acp);
        } else if (prv->wb_kind == 1) {
            blade_request_next(acp, 0xf);
        }
    }
    return 1;
}

static int manage_wblade(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x30);
    if (AcUidev_IsPadTrgConf(acp->frdp->uidevp, 6)) gcall(wctrlp->ontrigfunc, acp, wctrlp);
    return 1;
}

AcDptsSt *acblade_GetDestroyInfo(AcPartsBladeSt *bladep)
{
    return &bladep->prvp->dpts;
}

/* blade part picked up as an item: re-attach the sword model and its effect */
int acblade_Pickup(AcPartsBladeSt *bladep)
{
    PS2_FRAME(0x50);
    AcPartsBladePrivateSt *prv = bladep->prvp;
    AcPlayerSt *acp = prv->acp;
    AcEffSt *effp = AcCtrl_GetEffect(acp);
    aceff_GetBlade(effp, 0);   /* result unused */
    GAcUtil_GetObjCmat(&acp->ocmats[0x1a], acp->modeltbl[4], 12.0f, 1);
    memcpy(acp->ocmats[0x1a].mat, IdentityMtx, 0x30);   /* rows 0..2; row 3 keeps GetObjCmat's */
    prv->swordp = acp->param.swordp;
    if (acp->param.Larm_type == 2) {
        AcAsm_GetLarmParamEx(&acp->param);
        AcAsm_CalcDefenseEx(&acp->param);
    }
    GetBladeParameter(bladep);
    blade_sight_setup(bladep);
    if (acp->param.Larm_type == 2) aceff_RegisterShield(effp, 0, 6, 2, &acp->ocmats[0x1a]);
    else if (acp->param.Larm_type == 0) aceff_RegisterBlade(effp, 0, 6, 2, &acp->ocmats[0x1a]);
    return 1;
}

int acblade_Remove(AcPartsBladeSt *bladep)
{
    bladep->prvp->swordp = NULL;
    return 1;
}

int acblade_IsReady(AcPartsBladeSt *bladep)
{
    PS2_FRAME(0x30);
    AcPlayerSt *a = bladep->prvp->acp;
    if (bladep->prvp->blade_dstat.damage != 0 && !AcCtrl_IsCharging(a) && bladep->prvp->boost_blade_sub < a->energyReal &&
        bladep->prvp->blade_busytime == 0 && a->rb_stat == 0 && a->actno != 0x10)
        return 1;
    return 0;
}

/* per-frame reload timer (AcCtrl_Exec) */
void acblade_UpdateTime(AcPartsBladeSt *bladep)
{
    AcPartsBladePrivateSt *prv = bladep->prvp;
    if (prv->blade_busytime > 0) prv->blade_busytime--;
}

void acblade_Animation(AcPartsBladeSt *bladep)
{
    PS2_FRAME(0x20);
    AcPlayerSt *a = bladep->prvp->acp;
    if (a->modeltbl[4]) Anim_Update(a->modeltbl[4]);
    if (a->param.abl_Wblade && a->modeltbl[5]) Anim_Update(a->modeltbl[5]);
}

int acblade_Init(AcPartsBladeSt *bladep, AcPlayerSt *acp)
{
    PS2_FRAME(0x40);
    AcPartsBladePrivateSt *prv = bladep->prvp;
    ZeroMemory8b(bladep, 4);
    ZeroMemory8b(prv, 0x120);
    bladep->prvp = prv;
    prv->acp = acp;
    prv->swordp = acp->param.swordp;
    prv->armsp = acp->param.armsp;
    if (acp->param.abl_Wblade || prv->swordp) {
        GetBladeParameter(bladep);
        blade_sight_setup(bladep);
    }
    return 1;
}

/* blade numbers from the sword / arm part, its weapon variants (blade wave spells) and the option parts */
static void GetBladeParameter(AcPartsBladeSt *bladep)
{
    PS2_FRAME(0x60);
    AcPartsBladePrivateSt *prv = bladep->prvp;
    AcPlayerSt *acp = prv->acp;
    PartsParamArmsSt *armsp = prv->armsp;
    AcPtsResSt *ptsresp = acp->frdp->ptsresp;
    PartsParamSwordSt *swordp = prv->swordp;
    float k = (acp->param.option_parts & 0x400) ? g_asmparam.opt_bladeup : 1.0f;
    if (acp->param.abl_Wblade) {
        prv->boost_blade_sub = armsp->blade_bsub;
        prv->blade_reloadtime = armsp->blade_reload;
        prv->blade_range = armsp->blade_range;
        prv->blade_heat = (float)(int)armsp->blade_heat * g_asmparam.heat_to_cqheat;
        prv->combp = AcPtsRes_GetSfxCombHead(ptsresp, 2);
        prv->hitmagcp = AcPtsRes_GetMagicPtr(ptsresp, 2, 5);
    } else {
        prv->boost_blade_sub = swordp->boost_sub;
        prv->blade_reloadtime = swordp->blade_reload;
        prv->blade_range = swordp->blade_range;
        prv->blade_heat = (float)(int)swordp->blade_heat * g_asmparam.heat_to_cqheat;
        prv->combp = AcPtsRes_GetSfxCombHead(ptsresp, 6);
        prv->hitmagcp = AcPtsRes_GetMagicPtr(ptsresp, 6, 4);
    }
    prv->gndbld_wait = armsp->gndbld_wait;   /* armsp also for the sword */
    prv->flybld_wait = armsp->flybld_wait;
    if (prv->acp->param.plus & 0x400) prv->blade_range = prv->blade_range * 1.5f;
    if (prv->acp->param.abl_Wblade) {
        for (int i = 0; i < 2; i++) {
            PartsParamWeaponSt *wp = AcPtsRes_GetWeaponPtr(ptsresp, 2, i);
            if (!wp) continue;
            prv->bwv[i].heat = (float)(int)wp->heat;
            prv->bwv[i].boostsub = wp->ep;
            prv->bwv[i].dstat = wp->dstat;
            prv->bwv[i].spelp = AcPtsRes_GetWeaponSpellPtr(ptsresp, 2, wp->spelldata_id);
        }
    } else {
        PartsParamWeaponSt *wp = AcPtsRes_GetWeaponPtr(ptsresp, 6, 0);
        if (wp) {
            prv->bwv[0].heat = (float)(int)wp->heat;
            prv->bwv[0].boostsub = wp->ep;
            prv->bwv[0].dstat = wp->dstat;
            prv->bwv[0].spelp = AcPtsRes_GetWeaponSpellPtr(ptsresp, 6, wp->spelldata_id);
            prv->bwv[1].spelp = NULL;
        }
    }
    prv->curbwvp = &prv->bwv[0];
    prv->blade_dstat.effect = 0;
    prv->blade_dstat.damage = 0;
    prv->blade_dstat.react = 0;
    prv->blade_dstat = prv->acp->param.abl_Wblade ? armsp->blade_dstat : swordp->blade_dstat;
    prv->blade_dstat.damage = (unsigned short)blade_ftou((float)(int)prv->blade_dstat.damage * k);
    prv->bwv[0].dstat.damage = (unsigned short)blade_ftou((float)(int)prv->bwv[0].dstat.damage * k);
    prv->bwv[1].dstat.damage = (unsigned short)blade_ftou((float)(int)prv->bwv[1].dstat.damage * k);
    prv->blade_dstat.damage = (unsigned short)(((int)prv->blade_dstat.damage * (int)armsp->ene_supply) / 100);
    prv->flyblade_dstat = prv->blade_dstat;
    int m = g_sortie.pconstant->modeSortie;
    float rate = (m == 0 || m == 1) ? g_asmparam.p1_flyblade_dmgrate : g_asmparam.vs_flyblade_dmgrate;
    prv->flyblade_dstat.damage = (unsigned short)blade_ftou((float)(int)prv->flyblade_dstat.damage * rate);
}

/* start a fly blade swing (action 0xd): energy, boost-up while held, OB closed, heat */
static int SetActFlyBlade(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    AcPartsBladePrivateSt *prv = AcCtrl_GetPartsBlade(acp)->prvp;
    if (!AcCtrl_EnergyDrain(acp, prv->boost_blade_sub)) return 0;
    if (acp->rb_stat) return 0;
    if (AcUidev_IsPadOnConf(acp->frdp->uidevp, 5)) {
        AcCtrl_DispBackBoostFire(acp);
        AcCtrl_DispLegBoostFire(acp);
        AddF_min(&acp->velocity[1], -acp->param.flyup_accel, -acp->param.flyup_maxspd);
    }
    if (acp->param.core_type == 1) acob_Close(AcCtrl_GetPartsOb(acp));
    AcCtrl_AddHeat(acp, prv->blade_heat);
    AC_SetAction(acp, 0xd);
    if (prv->wb_kind == 0) {
        if (acp->id == 0) Info_IncData(0, 2, 0, 0);
    } else {
        Info_IncData(1, 4, acp->id, 0);
    }
    return 1;
}

/* start a ground blade swing (action 0xb); the W blade's swing only tests the wave's energy (no drain) */
static int SetactGndBlade(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    AcPartsBladePrivateSt *prv = AcCtrl_GetPartsBlade(acp)->prvp;
    if (prv->wb_kind == 1) {
        if (acp->energyReal < prv->bwv[0].boostsub) return 0;
    } else if (!AcCtrl_EnergyDrain(acp, prv->boost_blade_sub)) {
        return 0;
    }
    if (acp->rb_stat) return 0;
    if (acp->param.core_type == 1) acob_Close(AcCtrl_GetPartsOb(acp));
    AcCtrl_AddHeat(acp, prv->blade_heat);
    AC_SetAction(acp, 0xb);
    if (prv->wb_kind == 0) {
        if (acp->id == 0) Info_IncData(0, 2, 0, 0);
    } else {
        Info_IncData(1, 4, acp->id, 0);
    }
    return 1;
}

/* blade wave (magic sword spell of the current blade variant) from the blade tip; QUIRK: the tip position buffer
 * on the stack is also passed as the spell's argp */
static int AppearBladeWave(AcPartsBladeSt *bladep, int wb_lr)
{
    PS2_FRAME(0xf0);
    AcPartsBladePrivateSt *prv = bladep->prvp;
    int sub = prv->curbwvp->boostsub;
    AcPlayerSt *acp = prv->acp;
    if (sub > 0 && !AcCtrl_EnergyDrain(acp, sub)) return 0;
    AcCtrl_AddHeat(acp, prv->curbwvp->heat);
    ObjCMatSt *oc = wb_lr ? &acp->ocmats[0x1b] : &acp->ocmats[0x1a];
    PS2_STACK(float[4], pos, 0x70);
    PS2_STACK(float[4], ang, 0x80);
    PS2_STACK(float[4], scl, 0x90);   /* written, never passed */
    PS2_STACK(float[4], col, 0xa0);
    vu_apply(pos, (const float (*)[4])*oc->parentmatp, oc->mat[3]);
    ang[0] = 0.0f;
    ang[1] = Add_Angle(acp->ang[1], BLADE_PI);
    ang[2] = 0.0f;
    ang[3] = 1.0f;
    scl[0] = scl[1] = scl[2] = scl[3] = 1.0f;
    col[0] = col[1] = col[2] = col[3] = 128.0f;
    unsigned short wid = prv->acp->param.abl_Wblade ? prv->armsp->weapon_id : prv->swordp->weapon_id;
    prv->wv_dstat = prv->curbwvp->dstat;
    prv->wv_dstat.damage = (unsigned short)blade_ftou((float)(int)prv->wv_dstat.damage * AcAsm_GetArmOwRate(&acp->param));
    return DF_SPELL2_AppearSpellPtr(prv->curbwvp->spelp, 2, (char)acp->id, &prv->wv_dstat, NULL, prv->combp, pos, ang,
                                    NULL, NULL, pos, 0x200, wid) != NULL;
}

/* blade collision hit (DF_COLL callback; result = kind << 28 | id): map models get the map hit, enemies / ACs the
 * damage (an AC guarding with a shield bounces the attacker back), first hit of a swing plays the hit voice and
 * counts the hit; every hit spawns the hit magic along the blade's motion */
static void HitBladeCallback(AcPlayerSt *acp, void *tgt, int result)
{
    (void)tgt;
    PS2_FRAME(0x140);
    int kind = (result >> 28) & 0xf;
    int id = result & 0xffff;
    AcPartsBladePrivateSt *prv = AcCtrl_GetPartsBlade(acp)->prvp;
    if (kind == 4 && id == 0) TrainingTrendIncreaseDamage();
    switch (acp->actno) {
    case 0xb:
    case 0xe:
        prv->wk_dstat = prv->blade_dstat;
        break;
    case 0xd:
    case 0xf:
        prv->wk_dstat = prv->flyblade_dstat;
        break;
    default:
        return;
    }
    prv->wk_dstat.damage = (unsigned short)blade_ftou((float)(int)prv->wk_dstat.damage * AcAsm_GetArmOwRate(&acp->param));
    struct_3956eba2 *b = aceff_GetBlade(AcCtrl_GetEffect(acp), prv->wb_lr == 0 ? 0 : 1);
    prv->hitcnt++;
    PS2_STACK(float[4], pos, 0x130);
    PS2_STACK(float[4], vec, 0x120);
    if (kind == 1 || kind == 2 || kind == 8) {
        DF_COLL_GetHitLoc(b->hcoll, pos);
        DF_SPELL2_MapHitProc_Param(kind, id, pos, 2, acp->id, &prv->wk_dstat);
        StModel *m = STAGE_GetModelPointer(kind, id);
        unsigned short rel = m->array->relation;
        if (rel != 0 && !(DFene_GetEnemy(rel & 0x3ff)->use & 0x80) && !(b->flag & 1)) {
            Voice_CallPoint(0x1d, AC_SEparam.bladehit_vol, (float (*)[4])m->loc, (float)AC_SEparam.bladehit_dist);
            b->flag |= 1;
            Info_IncData(1, 5, acp->id, 0);
        }
    } else {
        DF_COLL_GetPos(b->hcoll, pos);
        DF_COLL_GetVel(b->hcoll, vec, NULL);
        if (kind == 3) {
            unsigned int before = CombatRecord_GetRecord()->killCnt[(int)Enemy[id].edat->kind];
            Enemy_SFXHit(&Enemy[id], &prv->wk_dstat, vec, pos, 2, acp->id);
            unsigned int after = CombatRecord_GetRecord()->killCnt[(int)Enemy[id].edat->kind];
            if (before < after) SetBladeKillIncrease();
        } else if (kind == 4) {
            AcPlayerSt *t = &g_ac[id];
            float f = 0.0f;
            if (acshield_IsGuardEnable(t, pos) == 1) {
                if (t->param.shield_def.useene != 0) f = 10.0f * ((float)t->param.shield_def.react / 255.0f);
            } else if (acsideshld_IsGuardEnable(t, pos) != 0 && t->param.exshield_def.useene != 0) {
                f = 10.0f * ((float)t->param.exshield_def.react / 255.0f);
            }
            if (f > 0.0f) {
                /* QUIRK: the guard bounce reaction is applied to the attacker (acp) */
                PS2_STACK(SfxDstat, tmp_dstat, 0x110);
                PS2_STACK(float[4], tmp_vec, 0x100);
                PS2_STACK(float[4][4], rotmt, 0xc0);
                static const unsigned int dstat_init[4] = {7, 0, 0, 0};
                static const float vec_init[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                memcpy(&tmp_dstat, dstat_init, 16);
                memcpy(tmp_vec, vec_init, 16);
                tmp_dstat.react = (unsigned char)blade_ftou((float)(int)prv->wk_dstat.react * f);
                if (tmp_dstat.react >= 0x33) tmp_dstat.react = 0x32;
                tmp_vec[2] = 1.0f;
                sceVu0RotMatrixY(rotmt, IdentityMtx, acp->ang[1]);
                vu_apply(tmp_vec, (const float (*)[4])rotmt, tmp_vec);
                int was = (acp->stat & 2) != 0;
                AcCtrl_DamageReaction(acp, &tmp_dstat, tmp_vec, pos, 0);
                if (!was) acp->stat &= 0xfffd;
            }
            if (prv->hitcnt != 2) prv->wk_dstat.pierce = 0;
            AcCtrl_HitAttack(t, &prv->wk_dstat, vec, pos, 2, acp->id);
        }
        if (!(b->flag & 1)) {
            b->flag |= 1;
            Voice_CallPoint(0x1d, AC_SEparam.bladehit_vol, (float (*)[4])pos, (float)AC_SEparam.bladehit_dist);
            acp->stat |= 1;
            if (prv->wb_kind == 0) {
                if (acp->id == 0) Info_IncData(0, 0xb, 0, 0);
            } else {
                Info_IncData(1, 5, acp->id, 0);
            }
            if (!(kind == 4 && id == acp->id))
                CombatRecord_OnHit(acp, prv->acp->param.abl_Wblade ? prv->armsp->weapon_id : prv->swordp->weapon_id,
                                   &prv->wk_dstat);
        }
    }
    PS2_STACK(float[4], dvec, 0xb0);
    PS2_STACK(float[4], ang, 0xa0);
    dvec[0] = prv->bladepos[0][0] - prv->bladepos[1][0];   /* vsub.xyz + full store: w = bladepos[0].w */
    dvec[1] = prv->bladepos[0][1] - prv->bladepos[1][1];
    dvec[2] = prv->bladepos[0][2] - prv->bladepos[1][2];
    dvec[3] = prv->bladepos[0][3];
    vec2ang(ang, dvec);
    DF_MAGIC2_AppearMagicPtr(prv->hitmagcp, prv->combp, pos, ang, NULL, NULL, NULL, NULL, 0);
}

/* blade hit collision on / off: a moving point from the blade tip down the blade axis (length 2 * blade_range),
 * shortened to end 0.05 below the AC's loc.y when the tip is below it and the segment would rise above it; no
 * collision while stiff or without blade range */
static void SetHitBlade(AcPlayerSt *acp, int id, int hit)
{
    PS2_FRAME(0x80);
    struct_3956eba2 *b = aceff_GetBlade(AcCtrl_GetEffect(acp), id);
    AcPartsBladePrivateSt *prv = AcCtrl_GetPartsBlade(acp)->prvp;
    if (prv->blade_range < 0.001f) hit = 0;
    if (acp->stifftime > 0) hit = 0;
    if (hit) {
        if (b->hcoll < 0) {
            b->hcoll = DF_COLL_AddList(acp, (void (*)(void *, void *, int))(void *)GFN(HitBladeCallback), 1);
            DF_COLL_SetSelf(b->hcoll, 4, acp->id);
        }
        if (b->hcoll >= 0) {
            float spd = 2.0f * prv->blade_range;
            PS2_STACK(float[4], loc, 0x70);
            PS2_STACK(float[4], n_speed, 0x60);
            memcpy(loc, b->mat[3], 16);
            sceVu0Normalize(n_speed, b->mat[2]);
            n_speed[0] = n_speed[0] * -1.0f;   /* vmulx.xyz: w stays 0 */
            n_speed[1] = n_speed[1] * -1.0f;
            n_speed[2] = n_speed[2] * -1.0f;
            {
                /* VU0 footprint (as parts_rb set_blade_hit): vf16 = n_speed, vf31 = qmtc2 of the sign-extended 0xbf800000 */
                float vf31[4] = {-1.0f, ps2_u2f(0xffffffffu), PS2_STALE_VF(31, 2), PS2_STALE_VF(31, 3)};
                PS2_VF_MIRROR(16, n_speed);
                PS2_VF_MIRROR(31, vf31);
                (void)vf31;
            }
            if (loc[1] < acp->loc[1]) {
                float t;
                if (n_speed[1] > 0.001f && loc[1] + (t = n_speed[1] * spd) > acp->loc[1])
                    spd = spd * (((acp->loc[1] - 0.05f) - loc[1]) / t);
            }
            n_speed[3] = 1.0f;   /* sw 0x6c(sp): the velocity's w, not loc's (loc.w stays mat[3].w) */
            DF_COLL_SetPos(b->hcoll, loc);
            DF_COLL_SetVel(b->hcoll, n_speed, spd);
        }
    } else if (b->hcoll >= 0) {
        DF_COLL_Delete(b->hcoll);
        b->hcoll = -1;
    }
}

/* blade tracer ribbon: shift the 11-entry history (tip A, base B; 0x60 apart) in the AC's magic model buffer and
 * store the current blade tip and the point blade_range down the blade axis */
static void __UpdateBladeTracer(AcPlayerSt *acp)
{
    PS2_FRAME(0x80);
    AcPartsBladePrivateSt *prv = AcCtrl_GetPartsBlade(acp)->prvp;
    ObjCMatSt *oc = &acp->ocmats[prv->wb_lr ? 0x1b : 0x1a];
    if (prv->isTrace == 0) return;
    char *m = Magic_GetModel(acp->id + 0x8a);
    for (int j = 9; j >= 0; j--) {
        memcpy(m + 0xc0 + 0x60 * (j + 1), m + 0xc0 + 0x60 * j, 16);
        memcpy(m + 0xf0 + 0x60 * (j + 1), m + 0xf0 + 0x60 * j, 16);
    }
    float *a0 = (float *)(m + 0xc0), *b0 = (float *)(m + 0xf0);
    PS2_STACK(float[4][4], tmp, 0x40);
    vu_mul_matrix(tmp, (const float (*)[4])*oc->parentmatp, (const float (*)[4])oc->mat);
    memcpy(a0, tmp[3], 16);
    float d[4];
    vu_normalize(d, tmp[2]);   /* w = 0 */
    float r = -prv->blade_range;
    d[0] = d[0] * r; d[1] = d[1] * r; d[2] = d[2] * r;
    b0[0] = a0[0] + d[0];
    b0[1] = a0[1] + d[1];
    b0[2] = a0[2] + d[2];
    b0[3] = a0[3];
}
