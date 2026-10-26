/* src/ac/parts_rb.cpp - right-hand blade weapon of an AC (OLSORTIE overlay): a sword carried as the right-hand gun
 * (param.gunp with blade_heat / blade_range), driven through the _ac_weaponctrl callbacks. A swing is a small state
 * machine in acp->rb_stat (0 idle, 1 wind-up until rb_*hit_startfrm, 2 hit window until rb_*hit_endfrm + 1,
 * 3 recovery until the swing animation ends); the hit is a DF_COLL moving point along the blade (blade effect 6,
 * blade_hit_callback deals the damage), acrb_Draw follows the blade effect's matrix while a swing runs.
 */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"
#include "acnb/vu0m.h"

/* weapon-control slot store of a function with a different (but register-compatible) prototype */
#define WSLOT(slot, f) ((slot) = (decltype(slot))(void *)GFN(f))

AcDptsSt *acrb_GetDestroyInfo(AcPartsRbladeSt *rbp)
{
    return &rbp->prvp->dpts;
}

void acrb_HitForcedTermination(AcPartsRbladeSt *rbp)
{
    PS2_FRAME(0x10);
    if (!rbp) return;
    AcPlayerSt *acp = rbp->prvp->acp;
    if (acp) set_blade_hit(acp, 0);
}

void acrb_NoRbAnimGroup(AcPartsRbladeSt *rbp)
{
    PS2_FRAME(0x30);
    if (!rbp) return;
    AcPlayerSt *acp = rbp->prvp->acp;
    if (!acp) return;
    StModel *mp = acp->mp;
    if (!mp) return;
    Anim_MakeGroup(mp, 4, 3);
    Anim_MakeGroup(mp, 3, 3);
    acshield_SetAnimGroup(acp, acp->shieldstat == 1);
    Anim_MakeGroup(mp, 9, 5);
    Anim_MakeGroup(mp, 10, 5);
    Anim_MakeGroup(mp, 11, 5);
    Anim_MakeGroup(mp, 12, 5);
}

/* blade effect matrix = its object matrix in the parent's space (VU0 product, every frame of a swing) */
void acrb_Draw(AcPartsRbladeSt *rbp)
{
    PS2_FRAME(0x10);
    if (!rbp) return;
    AcPlayerSt *acp = rbp->prvp->acp;
    if (acp->rb_stat == 0) return;
    struct_3956eba2 *bladep = aceff_GetBlade(AcCtrl_GetEffect(acp), 6);
    ObjCMatSt *ocmp = bladep->ocmp;
    const float (*par)[4] = (const float (*)[4])*ocmp->parentmatp;
    vu_mul_matrix(bladep->mat, par, (const float (*)[4])ocmp->mat);
    /* VU0 footprint: vf1-4 = parent rows, vf22-25 = product rows, ACC = row 3 without the w term */
    for (int i = 0; i < 4; i++) {
        PS2_VF_MIRROR(1 + i, par[i]);
        PS2_VF_MIRROR(22 + i, bladep->mat[i]);
    }
#ifndef ACNB_SOURCEPORT
    {
        const float *r = ocmp->mat[3];
        float acc[4];
        for (int i = 0; i < 4; i++) {
            float a = par[0][i] * r[0];
            a = a + par[1][i] * r[1];
            acc[i] = a + par[2][i] * r[2];
        }
        PS2_ACC_MIRROR(acc);
    }
#endif
}

void acrb_Init(AcPartsRbladeSt *rbp, _ac_weaponctrl *wctrlp, AcPlayerSt *acp)
{
    PS2_FRAME(0x50);
    if (!rbp) return;
    AcPartsRbladePrivateSt *prv = rbp->prvp;
    ZeroMemory8b(rbp, 4);
    ZeroMemory8b(prv, 0x2c);
    rbp->prvp = prv;
    prv->acp = acp;
    prv->wctrlp = wctrlp;
    prv->prmp = acp->param.gunp;
    if (!wctrlp) return;
    AcEffSt *effp = AcCtrl_GetEffect(acp);
    GAcUtil_GetObjCmat(&acp->ocmats[0x1b], acp->modeltbl[5], 12.0f, 1);
    aceff_RegisterBlade(effp, 6, 5, 1, &acp->ocmats[0x1b]);
    prv->combp = AcPtsRes_GetSfxCombHead(acp->frdp->ptsresp, 5);
    prv->swordmagp = AcPtsRes_GetMagicPtr(acp->frdp->ptsresp, 5, 1);
    prv->hitmagp = AcPtsRes_GetMagicPtr(acp->frdp->ptsresp, 5, 3);
    WSLOT(wctrlp->onequipfunc, onequip_rb);
    WSLOT(wctrlp->onreleasefunc, onrelease_rb);
    WSLOT(wctrlp->managefunc, rb_manage);
    WSLOT(wctrlp->ontrigfunc, ontrigger_rb);
    WSLOT(wctrlp->shotcbfunc, shotcb_rb);
    WSLOT(wctrlp->onfreefunc, onfree_rb);
    weapon_setanim(wctrlp, 0, 0);
    acp->rb_stat = 0;
}

/* blade collision hit (DF_COLL callback; result = kind << 28 | id): map models / gimmicks get the map hit, enemies /
 * ACs the weapon's damage; the first hit of a swing plays the hit voice and counts the hit; every hit spawns the hit
 * magic */
static void blade_hit_callback(AcPlayerSt *acp, void *tgt, int inResult)
{
    (void)tgt;
    PS2_FRAME(0xc0);
    int type = (inResult >> 28) & 0xf;
    int id = inResult & 0xffff;
    AcPartsRbladeSt *rbp = AcCtrl_GetPartsRblade(acp);
    AcEffSt *effp = AcCtrl_GetEffect(acp);
    if (!rbp) return;
    SfxDstat *dstat = &rbp->prvp->wctrlp->wp->dstat;
    struct_3956eba2 *bladep = aceff_GetBlade(effp, 6);
    PS2_STACK(float[4], pos, 0xb0);
    PS2_STACK(float[4], vec, 0xa0);
    if (type == 1 || type == 2 || type == 8) {
        DF_COLL_GetHitLoc(bladep->hcoll, pos);
        DF_SPELL2_MapHitProc_Param(type, id, pos, 2, acp->id, dstat);
    } else {
        DF_COLL_GetPos(bladep->hcoll, pos);
        DF_COLL_GetVel(bladep->hcoll, vec, NULL);
        if (type == 3) {
            unsigned int before = CombatRecord_GetRecord()->killCnt[(int)Enemy[id].edat->kind];
            Enemy_SFXHit(&Enemy[id], dstat, vec, pos, 2, acp->id);
            unsigned int after = CombatRecord_GetRecord()->killCnt[(int)Enemy[id].edat->kind];
            if (before < after) SetBladeKillIncrease();
        } else if (type == 4) {
            AcCtrl_HitAttack(&g_ac[id], dstat, vec, pos, 2, acp->id);
        }
        if (!(bladep->flag & 1)) {
            bladep->flag |= 1;
            Voice_CallPoint(0x40, AC_SEparam.rb_hit_vol, (float (*)[4])pos, 2500.0f);
            acp->stat |= 1;
            if (acp->id == 0) Info_IncData(0, 9, 0, 0);
            if (!(type == 4 && id == acp->id))
                CombatRecord_OnHit(acp, (unsigned short)rbp->prvp->wctrlp->wp->id, dstat);
        }
    }
    DF_MAGIC2_AppearMagicPtr(rbp->prvp->hitmagp, rbp->prvp->combp, pos, NULL, NULL, NULL, NULL, NULL, 0);
}

/* blade hit collision on / off: a moving point from the blade tip down the blade axis (length 2 * blade_range),
 * shortened so it ends at the AC's loc.y when the tip is below it and the segment would rise above it */
static void set_blade_hit(AcPlayerSt *acp, int ioFlag)
{
    PS2_FRAME(0x80);
    AcPartsRbladeSt *rbp = AcCtrl_GetPartsRblade(acp);
    AcEffSt *effp = AcCtrl_GetEffect(acp);
    if (!rbp) return;
    struct_3956eba2 *bladep = aceff_GetBlade(effp, 6);
    if (rbp->prvp->prmp->blade_range < 0.001f) ioFlag = 0;
    if (ioFlag == 1) {
        if (bladep->hcoll < 0) {
            bladep->hcoll = DF_COLL_AddList(acp, (void (*)(void *, void *, int))(void *)GFN(blade_hit_callback), 1);
            DF_COLL_SetSelf(bladep->hcoll, 4, acp->id);
        }
        if (bladep->hcoll < 0) return;
        float spd = 2.0f * rbp->prvp->prmp->blade_range;
        PS2_STACK(float[4], loc, 0x70);
        PS2_STACK(float[4], n_speed, 0x60);
        memcpy(loc, bladep->mat[3], 16);
        sceVu0Normalize(n_speed, bladep->mat[2]);
        n_speed[0] = n_speed[0] * -1.0f;   /* vmulx.xyz: w stays */
        n_speed[1] = n_speed[1] * -1.0f;
        n_speed[2] = n_speed[2] * -1.0f;
        {
            /* VU0 footprint: vf16 = n_speed, vf31 = qmtc2 of the sign-extended 0xbf800000 */
            float vf31[4] = {-1.0f, ps2_u2f(0xffffffffu), PS2_STALE_VF(31, 2), PS2_STALE_VF(31, 3)};
            PS2_VF_MIRROR(16, n_speed);
            PS2_VF_MIRROR(31, vf31);
            (void)vf31;
        }
        float ay = acp->loc[1];
        if (loc[1] < ay) {
            float t = n_speed[1] * spd;
            if (n_speed[1] > 0.001f && loc[1] + t > ay) spd = spd * ((ay - loc[1]) / t);
        }
        n_speed[3] = 1.0f;   /* sw 0x6c(sp): the velocity's w, not loc's (loc.w stays mat[3].w) */
        DF_COLL_SetPos(bladep->hcoll, loc);
        DF_COLL_SetVel(bladep->hcoll, n_speed, spd);
    } else if (bladep->hcoll >= 0) {
        DF_COLL_Delete(bladep->hcoll);
        bladep->hcoll = -1;
        bladep->flag = 0;   /* QUIRK vs parts_blade SetHitBlade: the hit flag is reset here */
    }
}

static void onfree_rb(void)
{
}

static void shotcb_rb(void)
{
}

/* fire: consume a "bullet", pick the ground / fly swing animation, add the blade heat, start the swing animation */
static int ontrigger_rb(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x50);
    AcPartsRbladeSt *rbp = AcCtrl_GetPartsRblade(acp);
    if (!rbp) return 0;
    _weapon_st *wp = wctrlp->wp;
    if (!AcWep_DecBullet(acp, wp)) return 0;
    if (acp->actno == 8) {
        rbp->prvp->f_rb_fly = 1;
        rbp->prvp->anm_id = 0x48;
    } else {
        rbp->prvp->f_rb_fly = 0;
        rbp->prvp->anm_id = 0x47;
    }
    AcCtrl_AddHeat(acp, (float)(int)rbp->prvp->prmp->blade_heat);
    wctrlp->busytime = wp->reload_time;
    if (acp->shieldstat == 1) {
        Anim_MakeGroup(acp->mp, 4, 0x20);
        Anim_MakeGroup(acp->mp, 3, 0x20);
        Anim_MakeGroups(acp->mp, 9, 12, 0x20);
    } else {
        Anim_MakeGroup(acp->mp, 4, 0x20);
        Anim_MakeGroup(acp->mp, 3, 0x20);
        Anim_MakeGroups(acp->mp, 9, 12, 0x20);
        Anim_MakeGroups(acp->mp, 5, 8, 0x20);
    }
    AcAnim_ChangeAnimGroup(acp, 0x20, rbp->prvp->anm_id, 0x14, 0);
    weapon_setanim(wctrlp, wp->shot_anmid, 0);
    gcall(wctrlp->shotcbfunc, acp, wctrlp);
    return 1;
}

/* per-frame swing state machine (acp->rb_stat) */
static int rb_manage(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x60);
    AcPartsRbladeSt *rbp = AcCtrl_GetPartsRblade(acp);
    if (!rbp) return 0;
    AcPartsRbladePrivateSt *prv = rbp->prvp;
    AcLegTypeDataSt *td = acp->param.typedatap;
    int start, end;
    if (prv->f_rb_fly == 0) {
        start = td->rb_gndhit_startfrm;
        end = (short)(td->rb_gndhit_endfrm + 1);
    } else {
        start = td->rb_flyhit_startfrm;
        end = (short)(td->rb_flyhit_endfrm + 1);
    }
    StModel *mp = acp->mp;
    int frame = mp->op[3].frame;
    int st = acp->rb_stat;
    if (st == 3) {
        if (Anim_IsEndGroupAnim(mp, 5, prv->anm_id)) {
            StModel *m = acp->mp;
            Anim_MakeGroup(m, 4, 3);
            Anim_MakeGroup(m, 3, 3);
            Anim_MakeGroups(m, 9, 12, 3);
            Anim_MakeGroups(m, 5, 8, acp->shieldstat == 1 ? 8 : 9);
            AcAnim_ChangeAnim(acp, 0, 500, 0);
            weapon_changeanim(wctrlp, wctrlp->wp->release_anmid, 10, 0);
            acp->rb_stat = 0;
        }
    } else if (st == 2) {
        set_blade_hit(acp, 1);
        if (frame >= end) {
            set_blade_hit(acp, 0);
            acp->rb_stat = 3;
        }
    } else if (st == 1) {
        if (frame >= start) {
            set_blade_hit(acp, 1);
            acp->rb_stat = 2;
            acse_PutSeVolDist(acp, 0x3e, AC_SEparam.rb_shot_vol, (float)AC_SEparam.rb_shot_dist);
            DF_MAGIC2_AppearMagicOCMPPtr(rbp->prvp->swordmagp, rbp->prvp->combp, &acp->ocmats[0x1b], 4);
        }
    } else if (st == 0) {
        if (AcUidev_IsPadTrgConf(acp->frdp->uidevp, 7) == 0 && acp->reserve_weapon == 0 &&
            AcWep_IsShotKeyOn(acp, wctrlp) && gcall(wctrlp->ontrigfunc, acp, wctrlp) == 1) {
            acp->rb_stat = 1;
            set_blade_hit(acp, 0);
            acse_PutSeVolDist(acp, 0x3f, AC_SEparam.rb_charge_vol, (float)AC_SEparam.rb_charge_dist);
            if (acp->id == 0) Info_IncData(0, 0, 0, 0);
            CombatRecord_OnShoot(acp, (unsigned short)wctrlp->wp->id);
        }
    }
    return 1;
}

static int onrelease_rb(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x10);
    if (acp->rb_stat != 0) return 0;
    weapon_changeanim(wctrlp, wctrlp->wp->release_anmid, 10, 0);
    return 1;
}

static int onequip_rb(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    (void)acp;
    PS2_FRAME(0x20);
    acwep_SetFunc(wctrlp, (void *)GFN(rb_manage));
    weapon_changeanim(wctrlp, wctrlp->wp->equip_anmid, 10, 0);
    return 1;
}
