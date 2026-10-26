/* src/ac/wep_cannon.cpp - shoulder cannon weapons (ac_weaponctrl_init's weapon type 2).
 * AcWepCannon_Init fills the _ac_weaponctrl callback table and resolves the back-part effect / magic data;
 * onequip_cannon (walk-cannon ACs) or waiting_cannon (cannon-set key) unfolds the arm groups, installs the sight jcon
 * and cannon_manage as the exec function; cannon_manage keeps the cannon models in their rest pose, handles release /
 * charge effect / burst window and the trigger; ontrigger_cannon switches to cannon_shotloop (charge, then shotcnt
 * shells per volley, autocnt volleys, recoil push + arm kick), then back to cannon_manage. */
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"

/* arm pose loop of onfree / waiting / manage: x angle to target0, y / z to 0, on mp[0] and (if set) mp[1] */
static void cannon_arm_pose(_ac_weaponctrl *wctrlp, float target0)
{
    StModel *m = wctrlp->mp[0];
    int i = 0;
    do {
        m->op->noanim = 1;
        Angle_Approach(&m->op->angle[0], target0, 10.0f);
        Angle_Approach(&m->op->angle[1], 0.0f, 10.0f);
        Angle_Approach(&m->op->angle[2], 0.0f, 10.0f);
        Anim_UpdateChildObjMatrix(m->op, m->matrix2);
        m = wctrlp->mp[1];
        if (!m) break;
    } while (++i < 2);
}

/* purge: fold the cannon back to the equip angle */
static void onfree_cannon(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x50);
    float ang = acp->param.cannon_equipang;
    cannon_arm_pose(wctrlp, ang);
}

/* object group bits the cannon takes from the arms (8 = sword arm, 4 = gun arm); 0 for non hand-attached cannons */
static int get_canoarmgrp(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    _weapon_st *wp = wctrlp->wp;
    int r;
    if (!wp->hand_attach) return 0;
    switch (wp->statid) {
    case 5:
    case 4:
        r = 8;
        if (acp->param.swordp && acp->param.swordp->type == 3) r = 0;
        if (!acp->param.gunp) r |= 4;
        break;
    case 2:
        r = acp->param.gunp ? 0 : 4;
        break;
    case 1:
        if (acp->param.swordp && acp->param.swordp->type == 3) return 0;
        r = 8;
        break;
    default:
        r = 0;
        break;
    }
    if (acp->param.armsp->type) r = 0;
    return r;
}

static int onrelease_cannon(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x60);
    unsigned int g = wctrlp->mp[0]->grptodid[0];
    if (g != 0 && (int)g != wctrlp->wp->release_anmid) {   /* u8 vs sign-extended s8 */
        if (get_canoarmgrp(acp, wctrlp)) {
            ObjectSt *o5 = &acp->op[5];
            ObjectSt *o9 = &acp->op[9];
            for (int i = 0; i < 4; i++, o5++, o9++) {
                if (o5->group == 0x40) Anim_MakeGroup(acp->mp, 5 + i, 9);
                if (o9->group == 0x40) Anim_MakeGroup(acp->mp, 9 + i, 5);
            }
        }
        AcWep_EndNormalJcon(acp, wctrlp);
        wctrlp->mp[0]->op->noanim = 0;
        if (wctrlp->mp[1]) wctrlp->mp[1]->op->noanim = 0;
        weapon_changeanim(wctrlp, wctrlp->wp->release_anmid, 10, 0);
        if (acp->actno == 0x10) AC_SetAction(acp, 0x11);
    }
    acwep_SetFunc(wctrlp, (void *)GFN(waiting_cannon));
    _weapon_st *wp = wctrlp->wp;
    if (wp->combp && wp->magcp && wp->magicid == -1 && wctrlp->subspells) {
        DF_MAGIC2_Kill_ID((int)(intptr_t)wctrlp->subspells);
        wctrlp->subspells = 0;
    }
    return 1;
}

/* arm kick (joint reaction slot 3). Only stored in the table (no caller reaches it); cannon_shotloop has an
 * identical inlined copy, ported as a call. */
static void shotcb_cannon(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    float f = 1.5f * (float)wctrlp->wp->reaction;
    f = f / 64.0f;
    if (!(f <= 1.0f)) f = 1.0f;
    if (acp->param.plus != 0) f = ps2_u2f(0x3e99999a);
    struct_38955935 *jr = &acp->jointreact[3];
    jr->reaction_time = 0;
    jr->reaction_startframe = 0;
    jr->reaction_nframe = (short)((wctrlp->busytime * 2) / 3);
    jr->reactdamper = 1.0f;
    jr->angdif[0] = -30.0f * f;
}

static _spell2 *cannon_std_aprspell(AcPlayerSt *acp, _ac_weaponctrl *wctrlp, int muzzle)
{
    PS2_FRAME(0xa0);
    PS2_STACK(float[4], target, 0x90);
    unsigned short flag = AcWep_GetAmmoFlag(wctrlp->wp->statid);
    _weapon_st *wp = wctrlp->wp;
    acwep_GetTargetPos(target, acp);
    AbVal_AddShootRangeCount(acp, wp);
    CombatRecord_OnShoot(acp, (unsigned short)wp->id);
    return DF_SPELL2_AppearSpellPtr(wp->spelp, 2, (char)acp->id, &wp->dstat, wp->actp, wp->combp, nullptr, nullptr,
                                    target, &wctrlp->ocmats[muzzle], nullptr, flag, (unsigned short)wp->id);
}

/* exec function while firing: actstep 0 init -> 0x63 charge -> 1 fire (falls into 2) -> 2 recoil / next volley.
 * wk[0] volleys done, wk[1] muzzle index carried, wk[2] countdown, wk[5] charge counter, wk[3 + shot] spell pointers
 * (overlapping wk[5] / nextlock: original behaviour). */
static void cannon_shotloop(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x110);
    PS2_STACK(float[4][4], M, 0xc0);
    PS2_STACK(float[4], accvec, 0x100);
    _weapon_st *wp = wctrlp->wp;
    int s1 = 0;
    if (acp->wep_release == 0) AcWep_GetStdLock(acp, wctrlp, 1);
    switch (wctrlp->actstep) {
    case 0:
        wctrlp->wep_wk[0] = 0;
        wctrlp->wep_wk[1] = 0;
        wctrlp->wep_wk[2] = 0;
        wctrlp->wep_wk[5] = 0;
        wctrlp->actstep = 0x63;
        return;
    case 0x63:
        if (wctrlp->wep_wk[5] < (int)wp->chargetime) {
            if (wctrlp->wep_wk[5] == 0) Voice_CallPoint(0x53, 0x7f, &acp->loc, ps2_u2f(0x451c4000));
            _weapon_st *w = wctrlp->wp;
            if (w->blt_cnt && w->combp && w->magcp && w->magicid == -1 && !wctrlp->subspells)
                wctrlp->subspells = (_spell2 *)(intptr_t)DF_MAGIC2_AppearMagicOCMPPtr(w->magcp, w->combp, wctrlp->ocmats, 4);
            wctrlp->wep_wk[5]++;
        } else {
            _weapon_st *w = wctrlp->wp;
            if (w->combp && w->magcp && w->magicid == -1 && wctrlp->subspells) {
                DF_MAGIC2_Kill_ID((int)(intptr_t)wctrlp->subspells);
                wctrlp->subspells = 0;
            }
            wctrlp->actstep = 1;
        }
        return;
    case 1: {
        if (--wctrlp->wep_wk[2] > 0) return;
        s1 = wctrlp->wep_wk[1];
        weapon_setanim(wctrlp, wp->shot_anmid, 0);
        for (int s2 = 0; s2 < wp->shotcnt; s2++) {
            if (!AcWep_DecBullet(acp, wp)) {
                wctrlp->busytime = 0;
                acwep_SetFunc(wctrlp, (void *)GFN(cannon_manage));
                return;
            }
            wctrlp->wep_wk[3 + s2] = (int)(intptr_t)gcall(wctrlp->apr_splfunc, acp, wctrlp, s1);
            if (wp->magcp && wp->magicid != -1) AcWep_AppearShell(acp, wctrlp, wp->magcp, wp->combp);
            if (!(++s1 < wctrlp->nmuzzle)) s1 = 0;
        }
        wctrlp->actstep = wctrlp->actstep + 1;
    }
        /* fall through */
    case 2:
        if (acp->param.plus == 0) {
            sceVu0RotMatrixY(M, IdentityMtx, acp->ang[1]);
            float cap = acp->R - ps2_u2f(0x3c23d70a);
            float lim = g_asmparam.canoreact_maxxzspd;
            float k = g_asmparam.canoreactspdstep * (float)wctrlp->wp->reaction;
            if (cap <= lim) lim = cap;
            accvec[3] = 1.0f;
            accvec[1] = 0.0f;
            accvec[0] = M[2][0] * k;
            accvec[2] = M[2][2] * k;
            AcCtrl_AccelXz(acp, accvec, lim);
        }
        wctrlp->wep_wk[0]++;
        if (wctrlp->wep_wk[0] < (int)wp->autocnt) {
            wctrlp->wep_wk[2] = wp->autotime;
            wctrlp->wep_wk[1] = s1;
            wctrlp->actstep = 1;
            return;
        }
        wctrlp->cntBurstShot++;
        acwep_SetFunc(wctrlp, (void *)GFN(cannon_manage));
        shotcb_cannon(acp, wctrlp);   /* inlined copy in the original */
        if (wctrlp->cntBurstShot < wp->burst_shotcnt) {
            wctrlp->busytime = wp->breload_time;
            wctrlp->fcntAcptBurst = wp->reload_time;
        } else {
            wctrlp->busytime = wctrlp->wp->reload_time;
            wctrlp->cntBurstShot = 0;
            wctrlp->fcntAcptBurst = 0;
        }
        return;
    default:
        return;
    }
}

static int ontrigger_cannon(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x10);
    (void)acp;
    wctrlp->busytime = 0xff;
    acwep_SetFunc(wctrlp, (void *)GFN(cannon_shotloop));
    return 1;
}

/* exec function while deployed */
static int cannon_manage(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x50);
    cannon_arm_pose(wctrlp, 0.0f);
    _weapon_st *wp = wctrlp->wp;
    if (wp->chargetime > 0) {
        aceff_ControlWeapon(acp->frdp->effp, wctrlp->iregEff, wctrlp->actstep == 0x63);
    } else if (wp->blt_cnt && wp->combp && wp->magcp && wp->magicid == -1 && !wctrlp->subspells) {
        wctrlp->subspells = (_spell2 *)(intptr_t)DF_MAGIC2_AppearMagicOCMPPtr(wp->magcp, wp->combp, wctrlp->ocmats, 4);
    }
    if (acp->actno == 0x10) {
        if (!Anim_IsEnd(acp->mp)) return 0;
        if (wctrlp->busytime == 0 &&
            (AcUidev_IsMoving(acp->frdp->uidevp) || AcUidev_IsPadOnConf(acp->frdp->uidevp, 5))) {
            AcWep_ReleaseWeapon(acp, acp->equip_weapon);
            return 0;
        }
    }
    if ((acp->actstat & 1) && !acp->param.abl_flycannon) {
        Lock_ClearMng(&acp->lockmng, 0);
        return 0;
    }
    if (wctrlp->actstep == 0) {
        if (wctrlp->mp[0]->grptodid[0] == 0) weapon_changeanim(wctrlp, wctrlp->wp->equip_anmid, 10, 0);
        if (!AcWep_IsAnimEnd(wctrlp, wctrlp->wp->equip_anmid)) return 0;
        wctrlp->actstep++;
    }
    if (acp->wep_release == 0) AcWep_GetStdLock(acp, wctrlp, 1);
    if (wctrlp->cntBurstShot > 0 && wctrlp->cntBurstShot < wctrlp->wp->burst_shotcnt) {
        wctrlp->fcntAcptBurst--;
        if (wctrlp->fcntAcptBurst == 0) wctrlp->cntBurstShot = 0;
    }
    if (AcWep_IsAnimEnd(wctrlp, wctrlp->wp->equip_anmid) && AcWep_IsShotKeyOn(acp, wctrlp))
        gcall(wctrlp->ontrigfunc, acp, wctrlp);
    return 1;
}

/* deploy: arm groups -> cannon (0x40), sight jcon, equip anim, cannon_manage (shared by onequip / waiting) */
static void cannon_deploy(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    int m = get_canoarmgrp(acp, wctrlp);
    if (m & 8) ACsetact_shieldoff(acp);
    ObjectSt *op = acp->op;
    for (int i = 0; i < acp->mp->nobj; i++, op++) {
        if (op->group & m) {
            Anim_MakeGroup(acp->mp, i, 0x40);
            op->noanim = 0;
        }
    }
    acjcon_ResetReaction(acp, 2, 0x1e);
    acjcon_SetControl(&acp->Rarm_ctrl, (void *)GFN(AcWep_SightOnCannon), wctrlp);
    weapon_changeanim(wctrlp, wctrlp->wp->equip_anmid, 10, 0);
    acwep_SetFunc(wctrlp, (void *)GFN(cannon_manage));
}

/* exec function while folded: wait for the cannon-set key in a standing / walking action */
static void waiting_cannon(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x70);
    float ang = acp->param.cannon_equipang;
    cannon_arm_pose(wctrlp, ang);
    if (!acp->key_enable) return;
    if (!AcUidev_IsPadTrgConf(acp->frdp->uidevp, 6)) return;
    switch (acp->actno) {
    case 0x11: case 0xa: case 9: case 7: case 4: case 3: case 2: case 1: case 0:
        break;
    default:
        return;
    }
    acp->wep_release = 0;
    AC_SetAction(acp, 0x10);
    cannon_deploy(acp, wctrlp);
    Lock_SetSight(&acp->lockmng, 0, &wctrlp->wp->sight);
    Lock_ClearMng(&acp->lockmng, 0);
}

static int onequip_cannon(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x60);
    if (!acp->param.abl_walkcannon) {
        acwep_SetFunc(wctrlp, (void *)GFN(waiting_cannon));
        return 0;
    }
    Lock_SetSight(&acp->lockmng, 0, &wctrlp->wp->sight);
    Lock_ClearMng(&acp->lockmng, 0);
    cannon_deploy(acp, wctrlp);
    return 1;
}

void AcWepCannon_Init(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x50);
    wctrlp->onequipfunc = GFN(onequip_cannon);
    wctrlp->onreleasefunc = GFN(onrelease_cannon);
    wctrlp->managefunc = GFN(cannon_manage);
    wctrlp->ontrigfunc = GFN(ontrigger_cannon);
    wctrlp->shotcbfunc = GFN(shotcb_cannon);
    wctrlp->onfreefunc = GFN(onfree_cannon);
    wctrlp->apr_splfunc = GFN(cannon_std_aprspell);
    _weapon_st *wp = wctrlp->wp;
    if (wp->magcp) return;
    int psr = -1;
    void *p = nullptr;
    switch (wp->jpos) {
    case 6:
    case 5:
        p = acp->param.Lbackp;
        psr = 8;
        break;
    case 4:
        p = acp->param.Rbackp;
        psr = 7;
        break;
    }
    if (!p) return;
    wp->combp = AcPtsRes_GetSfxCombHead(acp->frdp->ptsresp, psr);
    wp->magcp = AcPtsRes_GetMagicPtr(acp->frdp->ptsresp, psr, 1);
    if (wp->chargetime > 0) wctrlp->iregEff = aceff_RegisterWeapon(acp->frdp->effp, psr, 1, wctrlp->ocmats);
}
