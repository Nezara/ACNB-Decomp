/* src/ac/wep_gun.cpp - "gun" type arm weapons (rifles, machine guns: ac_weaponctrl_init's weapon type 0).
 * AcWepGun_Init fills the _ac_weaponctrl callback table; AcWepGun_Exec is the per-frame manage function (lock, burst
 * window, trigger); ontrigger_gun installs gun_shotloop as the exec function, which fires shotcnt bullets per volley
 * autocnt times (random spread, aimed from the muzzle at the lock target / far point, one spell per bullet), then
 * the arm recoil reaction (shotcb_armweapon_R) and the burst / reload counters. */
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"
#include "acnb/vu0m.h"

static const float PI_F = ps2_u2f(0x40490fdb);

/* manage: lock, burst acceptance window, trigger -> recoil */
int AcWepGun_Exec(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x30);
    if (acp->wep_release == 0) AcWep_GetStdLock(acp, wctrlp, 1);
    if (wctrlp->cntBurstShot > 0 && wctrlp->cntBurstShot < wctrlp->wp->burst_shotcnt) {
        wctrlp->fcntAcptBurst--;
        if (wctrlp->fcntAcptBurst == 0) wctrlp->cntBurstShot = 0;
    }
    if (AcWep_IsShotKeyOn(acp, wctrlp)) {
        gcall(wctrlp->ontrigfunc, acp, wctrlp);
        _weapon_st *wp = wctrlp->wp;
        if (wp->jpos == 2) acp->param.gap_angle_by_recoil_l = wp->recoil;
        else if (wp->jpos == 1) acp->param.gap_angle_by_recoil_r = wp->recoil;
        acp->param.recoil_size = wctrlp->wp->recoil;
        acp->param.recoil_timecnt = acp->param.recoilctrl_time;
    }
    return 1;
}

void AcWepGun_Init(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    (void)acp;
    wctrlp->onequipfunc = GFN(onequip_gun);
    wctrlp->onreleasefunc = GFN(onrelease_gun);
    wctrlp->managefunc = GFN(AcWepGun_Exec);
    wctrlp->ontrigfunc = GFN(ontrigger_gun);
    wctrlp->shotcbfunc = GFN(shotcb_armweapon_R);
    wctrlp->onfreefunc = GFN(onfree_gun);
    weapon_setanim(wctrlp, 0, 0);
}

/* purge: jcon program 2 guns fold the first two objects back (x to -pi/4) */
static void onfree_gun(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x20);
    (void)acp;
    if (wctrlp->wp->jcon_progno != 2) return;
    wctrlp->mp[0]->op->noanim = 1;
    Angle_Approach(&wctrlp->mp[0]->op->angle[0], ps2_u2f(0xbf490fdb), 10.0f);
    Angle_Approach(&wctrlp->mp[0]->op->angle[1], 0.0f, 10.0f);
    Angle_Approach(&wctrlp->mp[0]->op->angle[2], 0.0f, 10.0f);
    wctrlp->mp[0]->op[1].noanim = 1;
    Angle_Approach(&wctrlp->mp[0]->op[1].angle[0], 0.0f, 10.0f);
    Angle_Approach(&wctrlp->mp[0]->op[1].angle[1], 0.0f, 10.0f);
    Angle_Approach(&wctrlp->mp[0]->op[1].angle[2], 0.0f, 10.0f);
    StModel *mp = wctrlp->mp[0];
    Anim_UpdateChildObjMatrix(mp->op, mp->matrix2);
}

/* recoil reaction of the arm joints after a volley (scaled by the arm damper) */
static void shotcb_armweapon_R(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    _weapon_st *wp = wctrlp->wp;
    int n = wp->jcon_datano;
    if (n == 0) return;
    JconHeadSt *head = &JconFileHeadp->jconheadtbl[n];
    struct_38f86b4e *d = head->jcondattbl;
    for (int i = 0; i < head->nobj; i++, d++) {
        int k = d->control_objid;
        if (k >= 0x10) k -= 0xc;
        struct_38955935 *jr = &acp->jointreact[k];
        if (acp->op[k].group & 4) jr->reactdamper = acp->param.arm_damper;
        else jr->reactdamper = 1.0f;
        jr->reaction_time = 0;
        jr->reaction_startframe = (short)ps2_ftoi((float)wp->react_startframe * acp->param.arm_damper);
        jr->reaction_nframe = (short)ps2_ftoi((float)wp->react_nframe * acp->param.arm_damper);
        jr->angdif[0] = d->react_xang;
    }
}

static int ontrigger_gun(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x20);
    acwep_SetFuncExe(wctrlp, (void *)GFN(gun_shotloop), acp);
    return wctrlp->wep_wk[7];
}

static int onrelease_gun(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x20);
    AcWep_EndNormalJcon(acp, wctrlp);
    weapon_changeanim(wctrlp, wctrlp->wp->release_anmid, 10, 0);
    return 1;
}

static int onequip_gun(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x30);
    Lock_SetSight(&acp->lockmng, 0, &wctrlp->wp->sight);
    Lock_ClearMng(&acp->lockmng, 0);
    acwep_SetFunc(wctrlp, (void *)GFN(AcWepGun_Exec));
    AcWep_StartNormalJcon(acp, wctrlp);
    weapon_changeanim(wctrlp, wctrlp->wp->equip_anmid, 10, 0);
    return 1;
}

/* (0x7fff - (u16)rand()) / 32767 */
static inline float gun_rand1(void) { return (float)(0x7fff - (unsigned short)ps2_rand()) / 32767.0f; }

/* exec function while firing: one volley per wait (wk[2]); wk[0] muzzle index, wk[1] volleys done, wk[7] shots of
 * the last volley. After autocnt volleys back to AcWepGun_Exec with the burst / reload time. */
static void gun_shotloop(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x190);
    PS2_STACK(float[4], target, 0x180);
    _weapon_st *wp = wctrlp->wp;
    if (wctrlp->actstep == 0) {
        wctrlp->wep_wk[1] = 0;
        wctrlp->wep_wk[2] = 1;
        wctrlp->actstep++;
    }
    if (--wctrlp->wep_wk[2] > 0) return;
    unsigned short flag = AcWep_GetAmmoFlag(wp->statid);
    int idx = wctrlp->wep_wk[0];
    float v;
    if (ps2_rand() % 10 < 3) {
        v = gun_rand1();
    } else {
        float a = gun_rand1();
        float b = gun_rand1();
        v = a * b;
    }
    float spread[3];
    if (wctrlp->wp->jpos == 1) spread[0] = v * acp->param.gap_angle_by_recoil_r;
    else if (wctrlp->wp->jpos == 2) spread[0] = v * acp->param.gap_angle_by_recoil_l;
    else spread[0] = 0.0f;
    spread[1] = 0.0f;
    spread[2] = PI_F * gun_rand1();
    float R[4][4] __attribute__((aligned(16))), M[4][4] __attribute__((aligned(16)));
    sceVu0RotMatrixX(R, IdentityMtx, spread[0]);
    sceVu0RotMatrixZ(R, R, spread[2]);
    int i;
    for (i = 0; i < wp->shotcnt; i++) {
        idx = ps2_mod32(idx, wctrlp->nmuzzle);
        void *tgtm = nullptr;
        if (!AcWep_DecBullet(acp, wp)) break;
        if (acp->sepins_weappn != -1 && wctrlp->wp->statid == 3) {
            acwep_GetFarPos(target, acp, -acp->view.ang[0], acp->ang[1]);
        } else {
            acwep_GetTargetPos(target, acp);
            if (acp->lockmng.lock[0].ofp) tgtm = acp->lockmng.lock[0].ofp;
        }
        ObjCMatSt *ocm = &wctrlp->ocmats[idx];
        float muzzle[4], dir[4], ang3[4];
        vu_apply(muzzle, (const float (*)[4])*ocm->parentmatp, ocm->mat[3]);
        dir[0] = target[0] - muzzle[0];
        dir[1] = target[1] - muzzle[1];
        dir[2] = target[2] - muzzle[2];
        dir[3] = target[3];
        vec2ang(ang3, dir);
        ang3[0] = -ang3[0];
        sceVu0RotMatrixZ(M, IdentityMtx, ang3[2]);
        sceVu0RotMatrixX(M, M, -ang3[0]);
        sceVu0RotMatrixY(M, M, ang3[1]);
        float z = dir[2];
        dir[0] = 0.0f;
        dir[1] = 0.0f;
        dir[2] = ps2_sqrt(z * z + (dir[0] * dir[0] + dir[1] * dir[1]));   /* the shot length is |dz| */
        vu_apply(dir, (const float (*)[4])R, dir);
        vu_apply(dir, (const float (*)[4])M, dir);
        target[0] = dir[0] + muzzle[0];
        target[1] = dir[1] + muzzle[1];
        target[2] = dir[2] + muzzle[2];
        target[3] = dir[3];
        DF_SPELL2_AppearSpellPtr(wp->spelp, 2, (char)acp->id, &wp->dstat, wp->actp, wp->combp, nullptr, nullptr, target,
                                 ocm, tgtm, flag, (unsigned short)wp->id);
        AbVal_AddShootRangeCount(acp, wp);
        CombatRecord_OnShoot(acp, (unsigned short)wp->id);
        AcWep_AppearShell(acp, wctrlp, wp->magcp, wp->combp);
        idx++;
    }
    if (i != 0) {
        weapon_setanim(wctrlp, wp->shot_anmid, 0);
        shotcb_armweapon_R(acp, wctrlp);
        wctrlp->wep_wk[0] = idx;
    }
    wctrlp->wep_wk[1]++;
    if (wctrlp->wep_wk[1] < wp->autocnt) {
        wctrlp->wep_wk[2] = wp->autotime;
        wctrlp->actstep = 1;
        return;
    }
    wctrlp->cntBurstShot++;
    acwep_SetFunc(wctrlp, (void *)GFN(AcWepGun_Exec));
    if (wctrlp->cntBurstShot < wp->burst_shotcnt) {
        wctrlp->busytime = wp->breload_time;
        wctrlp->fcntAcptBurst = wp->reload_time;
    } else {
        wctrlp->busytime = wp->reload_time;
        wctrlp->cntBurstShot = 0;
        wctrlp->fcntAcptBurst = 0;
    }
    wctrlp->wep_wk[7] = i;
}
