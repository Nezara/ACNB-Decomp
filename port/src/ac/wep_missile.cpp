/* src/ac/wep_missile.cpp - missile launchers (ac_weaponctrl_init's weapon type 1). AcWepMissile_Init fills the
 * _ac_weaponctrl callback table; missile_manage (per frame) sizes the lock count from the remaining ammunition and
 * fires on the trigger; ontrigger_missile starts a lock chain over acp->lockmng and switches to missile_shotloop,
 * which fires shotcnt missiles per volley at the current lock target (alternating between the two launcher muzzles),
 * then walks to the next lock or ends with the reload time. */
#include "acnb/ps2fp.h"

/* purge: jcon program 5 launchers give both models' first object back to the animation */
static void onfree_missile(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x30);
    (void)acp;
    if (wctrlp->wp->jcon_progno != 5) return;
    StModel *mp = wctrlp->mp[0];
    for (int i = 0;;) {
        mp->op->noanim = 0;
        Anim_UpdateChildObjMatrix(mp->op, mp->matrix2);
        mp = wctrlp->mp[1];
        if (!mp) break;
        if (++i >= 2) break;
    }
}

/* shot callback (not reached in Versus): the AC's missile-link hook */
static void shotcb_missile(AcPlayerSt *acp)
{
    PS2_FRAME(0x10);
    if (acp->mislink_func) gcall(acp->mislink_func, acp);
}

/* exec function while firing: one volley per wait (wk[1]) at the current lock (nextlock), wk[0] volleys done */
static void missile_shotloop(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0xc0);
    _weapon_st *w0 = wctrlp->wp;
    if (wctrlp->actstep == 0) {
        wctrlp->wep_wk[0] = 0;
        wctrlp->wep_wk[1] = 1;
        wctrlp->actstep++;
    }
    if (--wctrlp->wep_wk[1] > 0) return;
    void *tgt = wctrlp->nextlock.lkp->ofp;
    if (tgt) {
        int i;
        for (i = 0; i < w0->shotcnt; i++) {
            int m = wctrlp->mp[1] ? i + wctrlp->wep_wk[0] : i;
            _weapon_st *wp = wctrlp->wp;
            unsigned short flag = AcWep_GetAmmoFlag(wp->statid);
            if (!AcWep_DecBullet(acp, wp)) break;
            DF_SPELL2_AppearSpellPtr(wp->spelp, 2, (char)acp->id, &wp->dstat, wp->actp, wp->combp, nullptr, nullptr,
                                     nullptr, &wctrlp->ocmats[m & 1], tgt, flag, (unsigned short)wp->id);
            AbVal_AddShootRangeCount(acp, wp);
            CombatRecord_OnShoot(acp, (unsigned short)wp->id);
        }
        if (i != 0 && acp->mislink_func) gcall(acp->mislink_func, acp);
    }
    wctrlp->wep_wk[0]++;
    if (wctrlp->wep_wk[0] < w0->autocnt && w0->autocnt >= 2) {
        wctrlp->wep_wk[1] = w0->autotime;
        wctrlp->actstep = 1;
        return;
    }
    DF_discard_lock(wctrlp->nextlock.lkp);
    DF_init_next_lock(&wctrlp->nextlock, &acp->lockmng);
    if (DF_get_next_lock(&wctrlp->nextlock)) {
        wctrlp->wep_wk[1] = w0->autotime;
    } else {
        acwep_SetFunc(wctrlp, (void *)GFN(missile_manage));
        wctrlp->busytime = w0->reload_time;
    }
}

static int ontrigger_missile(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x20);
    if (wctrlp->actstep == 0) {
        DF_init_next_lock(&wctrlp->nextlock, &acp->lockmng);
        if (!DF_get_next_lock(&wctrlp->nextlock)) return 0;
    }
    wctrlp->busytime = 0xff;
    acwep_SetFunc(wctrlp, (void *)GFN(missile_shotloop));
    return 1;
}

/* manage: lock count from the remaining ammunition, lock, trigger */
static int missile_manage(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x30);
    if (AcWep_IsAnimEnd(wctrlp, wctrlp->wp->equip_anmid) && wctrlp->busytime == 0) {
        _weapon_st *wp = wctrlp->wp;
        if (wp->sight.lock_max * wp->shotcnt < wp->blt_cnt) {
            wp->sight.lock_max = wp->sight.org_lock_max;
        } else {
            wp->sight.lock_max = (short)ps2_div32(wp->blt_cnt, wp->shotcnt);   /* shotcnt 0: MIPS div result */
            if (wctrlp->wp->sight.lock_max <= 0) wctrlp->wp->sight.lock_max = 1;
        }
        if (acp->wep_release == 0) AcWep_GetStdLock(acp, wctrlp, 1);
    }
    if (AcWep_IsShotKeyOn(acp, wctrlp)) gcall(wctrlp->ontrigfunc, acp, wctrlp);
    return 1;
}

static int onrelease_missile(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x20);
    wctrlp->mp[0]->op->noanim = 0;
    if (wctrlp->mp[1]) wctrlp->mp[1]->op->noanim = 0;
    AcWep_EndNormalJcon(acp, wctrlp);
    weapon_changeanim(wctrlp, wctrlp->wp->release_anmid, 10, 0);
    return 1;
}

/* DWARF void, stored in the int-returning onequipfunc slot (v0 is left over from weapon_changeanim; the callers
 * ignore it) */
static void onequip_missile(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x30);
    Lock_SetSight(&acp->lockmng, 0, &wctrlp->wp->sight);
    Lock_ClearMng(&acp->lockmng, 0);
    acwep_SetFunc(wctrlp, (void *)GFN(missile_manage));
    AcWep_StartNormalJcon(acp, wctrlp);
    weapon_changeanim(wctrlp, wctrlp->wp->equip_anmid, 10, 0);
}

void AcWepMissile_Init(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    (void)acp;
    wctrlp->onequipfunc = (decltype(wctrlp->onequipfunc))GFN(onequip_missile);
    wctrlp->onreleasefunc = GFN(onrelease_missile);
    wctrlp->managefunc = GFN(missile_manage);
    wctrlp->shotcbfunc = (decltype(wctrlp->shotcbfunc))GFN(shotcb_missile);
    wctrlp->ontrigfunc = GFN(ontrigger_missile);
    wctrlp->onfreefunc = GFN(onfree_missile);
}
