/* src/ac/acwep.cpp - per-AC weapon control. The state lives in acp->weaponctrl[4] (models, muzzles, the weapon's
 * callbacks) and acp->param.weapons[4] (weapon data): each frame the weapon models animate and the equipped weapon's
 * action function runs (AcWep_Exec), the weapon change key / automatic change releases one slot and equips the next
 * (AcWep_SelectWeapon); shot gating (IsShotKeyOn / IsShotEnable / DecBullet), the lock-on request (GetStdLock), the
 * weapon-arm aiming through the arm joint controllers (sighton_* callbacks), initialisation (models, muzzle matrices,
 * the per-type Init in the overlay) and dropping / picking up weapon parts (single player, spare weapons). */
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"
#include "acnb/vu0m.h"

#ifdef ACNB_SOURCEPORT
struct_38f86b4e s_weparmoff_jcondat[2] = {{3, 0x11, 3, {0}, {-70.0f, 70.0f, -30.0f, 30.0f}, 10.0f},
                                         {3, 0x15, 3, {0}, {-70.0f, 70.0f, -30.0f, 30.0f}, 10.0f}};
float AcWep_SightOnCannon__rotlimit[4] = {-90.0f, 90.0f, -20.0f, 20.0f};
float sighton_rocket__rotlimit[2][4] = {{-90.0f, 20.0f, 0.0f, 0.0f}, {-90.0f, 90.0f, -90.0f, 90.0f}};
float sighton_missile__rotlimit[4] = {-90.0f, 90.0f, -120.0f, 120.0f};
float s_farvec[4] = {0.0f, 0.0f, -1500000000.0f, 1.0f};
JconHeadSt s_weparmoff_jconhead = {2, s_weparmoff_jcondat};
#endif

static const float PI_F = ps2_u2f(0x40490fdb), TWO_PI_F = ps2_u2f(0x40c90fdb);
static const float HALF_PI_F = ps2_u2f(0x3fc90fdb), QUARTER_PI_F = ps2_u2f(0x3f490fdb);

/* item record of a dropped / picked-up part (DF_ITM_GetDataAddr, 0x24 bytes, no DWARF type) */
struct AcWepItemRec {
    unsigned char statid, jpos, nameid, _pad3;   /* 0x0 */
    short name2id;                               /* 0x4 */
    short partsid;                               /* 0x6 */
    short weapon_id;                             /* 0x8 */
    unsigned short blt_cnt, blt_initcnt;         /* 0xa */
    short _padE;
    int hp;                                      /* 0x10 */
    short hp2;                                   /* 0x14: left back hp of a double back weapon */
    short _pad16;
    PartsParamBasicSt *partsp;                   /* 0x18 */
    _stSfxCombHead *comb, *comb2;                /* 0x1c */
};
/* the left hand variant (statid 0xff, jpos 2) */
struct AcWepItemRecLH {
    unsigned char statid, jpos, larm_type, _pad3; /* 0x0 */
    short partsid;                                /* 0x4 */
    short _pad6;
    int hp;                                       /* 0x8 */
    PartsParamSwordSt *swordp;                    /* 0xc */
    unsigned short blt_cnt, blt_initcnt;          /* 0x10 */
    _stSfxCombHead *comb;                         /* 0x14 */
    int _pad18[3];
};
static_assert(sizeof(AcWepItemRec) == 0x24 && sizeof(AcWepItemRecLH) == 0x24, "item record");

/* a point far ahead along the view: (Identity rotated about x by rotx, then about y by roty) * s_farvec, plus loc
 * (w = loc.w), lowered by half the AC's height */
static void acwep_far_pos(float *trgpos, AcPlayerSt *acp, float rotx, float roty)
{
    float m[4][4] __attribute__((aligned(16)));
    sceVu0RotMatrixX(m, IdentityMtx, rotx);
    sceVu0RotMatrixY(m, m, roty);
    vu_apply(trgpos, (const float (*)[4])m, s_farvec);
    trgpos[0] = acp->loc[0] + trgpos[0];
    trgpos[1] = acp->loc[1] + trgpos[1];
    trgpos[2] = acp->loc[2] + trgpos[2];
    trgpos[3] = acp->loc[3];
    trgpos[1] = trgpos[1] - 0.5f * acp->tall;
}

/* the next slot after the equipped one that holds a weapon (n_weapon - 1 tries, wrapping at 4), -1 if none */
static int acwep_next_slot(AcPlayerSt *acp)
{
    int n = acp->param.n_weapon - 1, slot = acp->equip_weapon;
    for (int k = 0; k < n; k++) {
        if (++slot >= 4) slot = 0;
        if (acp->param.weapons[slot].statid != 5) return slot;
    }
    return -1;
}

static void acwep_equip(AcPlayerSt *acp, int wepno);

/* weapon_changeanim / weapon_setanim / InitAllWeapons: only weapons with models (statid 0..6), animid in range */
static inline int acwep_anim_ok(StModel *mp, int animid)
{
    return mp && animid >= 0 && animid < mp->tod->numoftod;
}

int AcWep_Init(AcWeaponSt *wepp)
{
    PS2_FRAME(0x30);
    AcWepPrivateSt *p = wepp->prvp;
    ZeroMemory8b(wepp, 4);
    ZeroMemory8b(p, 1);
    wepp->prvp = p;
    return 1;
}

/* sortie mode 0, player 0: charge the ammunition used of a shell-cost weapon to the settlement */
void AcWep_SettleShell(AcPlayerSt *acp, _weapon_st *pw)
{
    if (g_sortie.pconstant->modeSortie != 0 || acp->id != 0 || pw->blt_initcnt == 0 || !(pw->dstat.effect & 0x10)) return;
    g_sortie.psettle->costRemoveAmm += pw->blt_price * (pw->blt_initcnt - pw->blt_cnt);
}

/* spent cartridge effect at the weapon's cartridge matrix (acp unused) */
void AcWep_AppearShell(AcPlayerSt *acp, _ac_weaponctrl *wctrlp, _magic2_data_st *magcp, _stSfxCombHead *combp)
{
    PS2_FRAME(0x10);
    (void)acp;
    ObjCMatSt *ocm = &wctrlp->ocmatsCartridge[0];
    if (magcp && ocm->parentmatp) DF_MAGIC2_AppearMagicOCMPPtr(magcp, combp, ocm, 8);
}

unsigned short AcWep_GetAmmoFlag(int wepstatid)
{
    switch ((unsigned int)wepstatid) {
    case 0: return 0x100;
    case 1: return 0x600;
    case 2: return 0x500;
    case 3: return 0x800;
    case 4: case 5: return 0x400;
    case 6: return 0x300;
    case 7: return 0x900;
    }
    return 0;
}

/* missile launcher on the arm: both models' object 0 turn toward the far point ahead, blended in over 30 frames
 * (the object count of the first model is used for both) */
static void sighton_missile(AcPlayerSt *acp, _ac_jointctrl *jctrlp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0xd0);
    PS2_STACK(float[4], trg, 0xc0);
    float rate = (float)jctrlp->acttime / 30.0f;
    if (!(rate <= 1.0f)) rate = 1.0f;
    acwep_far_pos(trg, acp, 0.0f, acp->ang[1]);
    if (jctrlp->actstep == 0) {
        wctrlp->mp[0]->op->noanim = 1;
        if (wctrlp->mp[1]) wctrlp->mp[1]->op->noanim = 1;
        jctrlp->actstep++;
    }
    StModel *mp = wctrlp->mp[0];
    int n = mp->nobj;
    if (n >= 3) n = 2;
    if (n <= 0) return;
    for (int j = 0;;) {
        float old[4];
        memcpy(old, mp->op->angle, 16);
        ObjLookWorldTarget2_Xofs(mp->op, mp->matrix2, mp->matrix2[1], trg, sighton_missile__rotlimit, 1.0f, 0.0f);
        for (int i = 0; i < 3; i++) mp->op->angle[i] = old[i] + rate * (mp->op->angle[i] - old[i]);
        Anim_UpdateChildObjMatrix(mp->op, mp->matrix2);
        mp = wctrlp->mp[1];
        if (!mp) break;
        if (++j >= n) break;
    }
}

/* rocket: objects 0 and 1 of each model turn toward the target (object 1 relative to object 0's world matrix);
 * object 0 blends in over 30 frames, object 1 from frame 10 to 30 (then the counter is held at 30) */
static void sighton_rocket(AcPlayerSt *acp, _ac_jointctrl *jctrlp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0xc0);
    PS2_STACK(float[4], trg, 0xa0);
    if (jctrlp->actstep == 0) {
        wctrlp->mp[0]->op[0].noanim = 1;
        wctrlp->mp[0]->op[1].noanim = 1;
        jctrlp->actstep++;
    }
    int t = jctrlp->acttime;
    float blend[2];
    blend[0] = (float)t / 30.0f;
    if (!(blend[0] <= 1.0f)) blend[0] = 1.0f;
    if (t < 10) {
        blend[1] = 0.0f;
    } else {
        float x = (float)(t - 10) / 20.0f;
        if (x < 1.0f) {
            blend[1] = x;
        } else {
            blend[1] = 1.0f;
            jctrlp->acttime = 30;
        }
    }
    acwep_GetTargetPos(trg, acp);
    StModel *mp = wctrlp->mp[0];
    float (*parent)[4] = mp->matrix2;
    for (int mi = 0;;) {
        int n = mp->nobj;
        if (n >= 3) n = 2;
        for (int k = 0; k < n; k++) {
            ObjectSt *o = &mp->op[k];
            float old[4];
            memcpy(old, o->angle, 16);
            ObjLookWorldTarget2_Xofs(o, parent, parent[1], trg, sighton_rocket__rotlimit[k], 1.0f, 0.0f);
            for (int i = 0; i < 3; i++) o->angle[i] = old[i] + blend[k] * (o->angle[i] - old[i]);
            Anim_UpdateChildObjMatrix(o, parent);
            parent = *mp->op[k].wrmatp;
        }
        mp = wctrlp->mp[1];
        if (!mp) break;
        parent = mp->matrix2;
        if (++mi >= 2) break;
    }
}

/* weapon arm put away: aim the arm along object 3's x axis, made orthogonal to world up (rows: normalised x axis,
 * up minus its projection on it, their cross product, object 3's position row) */
static void sightoff_weaponarm(AcPlayerSt *acp)
{
    PS2_FRAME(0x80);
    PS2_STACK(float[4], trg, 0x10);
    static const float up[4] = {0.0f, 1.0f, 0.0f, 0.0f};   /* unnamed constant at 0x25ac80 */
    float (*r)[4] = *acp->op[3].wrmatp;
    float m[4][4] __attribute__((aligned(16)));
    vu_normalize(m[0], r[0]);
    float ax = up[0] * m[0][0], ay = up[1] * m[0][1], az = up[2] * m[0][2];
    float dot = az + (ax + ay);
    float d[4];
    d[0] = up[0] - m[0][0] * dot;
    d[1] = up[1] - m[0][1] * dot;
    d[2] = up[2] - m[0][2] * dot;
    d[3] = up[3];
    vu_normalize(m[1], d);
    vu_cross(m[2], m[0], m[1]);
    m[2][3] = m[0][3];
    memcpy(m[3], r[3], 16);
    vu_apply(trg, (const float (*)[4])m, s_farvec);
    acjcon_Play(acp, trg, &s_weparmoff_jconhead);
}

/* weapon arms: aim the arm joints at the target */
static void sighton_weaponarm(AcPlayerSt *acp, _ac_jointctrl *jctrlp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x50);
    PS2_STACK(float[4], trg, 0x40);
    acwep_GetTargetPos(trg, acp);
    if (jctrlp->actstep == 0) {
        acjcon_ResetReaction(acp, 0x20, 0x1e);
        acjcon_ResetReaction(acp, 2, 0x1e);
        jctrlp->actstep++;
    }
    acjcon_Play(acp, trg, &JconFileHeadp->jconheadtbl[wctrlp->wp->jcon_datano]);
}

/* cannon: aim the arm joints and both models' object 0 at the target (fast for the first 30 frames), then pitch the
 * group-0x40 animation by the barrel's elevation in object 3's frame (clamped to +-45 degrees) */
void AcWep_SightOnCannon(AcPlayerSt *acp, _ac_jointctrl *jctrlp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0xb0);
    PS2_STACK(float[4], trg, 0xa0);
    acwep_GetTargetPos(trg, acp);
    acjcon_Play(acp, trg, &JconFileHeadp->jconheadtbl[wctrlp->wp->jcon_datano]);
    float rotspd = jctrlp->acttime < 30 ? 20.0f : 3.0f;
    for (int j = 0; j < 2; j++) {
        StModel *mp = wctrlp->mp[j];
        if (!mp) break;
        mp->op->noanim = 1;
        ObjLookWorldTarget2_Xofs(mp->op, mp->matrix2, mp->matrix2[1], trg, AcWep_SightOnCannon__rotlimit, rotspd, 0.0f);
        Anim_UpdateChildObjMatrix(mp->op, mp->matrix2);
    }
    if (jctrlp->actstep == 0) jctrlp->actstep++;
    float t[4][4] __attribute__((aligned(16))), v[4];
    sceVu0TransposeMatrix(t, *acp->op[3].wrmatp);
    vu_apply(v, (const float (*)[4])t, (*wctrlp->mp[0]->op->wrmatp)[2]);
    float ang = ps2_atan2f(v[1], v[2]);
    int hokan = jctrlp->acttime < 30 ? 0x14 : 2;
    if (ang < -QUARTER_PI_F) ang = -QUARTER_PI_F;
    if (!(ang <= QUARTER_PI_F)) ang = QUARTER_PI_F;
    AcAnim_ChangeAnimGroupFrameStop(acp, 0x40, 0, 0, 0x1e, 0);
    float q = (QUARTER_PI_F - ang) / HALF_PI_F;
    AcAnim_ChangeAnimGroupFrameStop(acp, 0x40, 0x3c, ps2_ftoi(9000.0f * (1.0f - q)), hokan, 0);
    Anim_UpdateGroup(acp->mp, 0x40);
    Anim_UpdateChildObjMatrix(acp->op[3].child, *acp->op[3].wrmatp);
}

/* right arm weapon: aim the arm joints at the target */
static void sighton_armweapon_R(AcPlayerSt *acp, _ac_jointctrl *jctrlp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x50);
    PS2_STACK(float[4], trg, 0x40);
    if (jctrlp->actstep == 0) {
        acjcon_ResetReaction(acp, 4, 0x1e);
        jctrlp->actstep++;
    }
    acwep_GetTargetPos(trg, acp);
    acjcon_Play(acp, trg, &JconFileHeadp->jconheadtbl[wctrlp->wp->jcon_datano]);
}

int AcWep_EndNormalJcon(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x40);
    _weapon_st *wp = wctrlp->wp;
    if (wp->jcon_progno == 0) return 0;
    AcAnim_SynchroAnim(acp, 1, 2, 0x1e);
    AcAnim_SynchroAnim(acp, 1, 4, 0x1e);
    acjcon_SetControl(&acp->Rarm_ctrl, 0, 0);
    if (wp->statid == 6) {
        acjcon_ResetReaction(acp, 0x20, 0x1e);
        acjcon_SetControl(&acp->Warm_ctrl, (void *)GFN(sightoff_weaponarm), wctrlp);
    }
    return 1;
}

/* the arm joint controller of the weapon's aiming program */
static int acwep_start_jcon(AcPlayerSt *acp, int progno, _ac_weaponctrl *wctrlp)
{
    switch (progno) {
    case 1: acjcon_SetControl(&acp->Rarm_ctrl, (void *)GFN(sighton_armweapon_R), wctrlp); return 1;
    case 2: acjcon_SetControl(&acp->Rarm_ctrl, (void *)GFN(sighton_rocket), wctrlp); return 1;
    case 5: acjcon_SetControl(&acp->Rarm_ctrl, (void *)GFN(sighton_missile), wctrlp); return 1;
    case 3: acjcon_SetControl(&acp->Rarm_ctrl, (void *)GFN(AcWep_SightOnCannon), wctrlp); return 1;
    case 4: acjcon_SetControl(&acp->Warm_ctrl, (void *)GFN(sighton_weaponarm), wctrlp); return 1;
    }
    return 0;
}

int AcWep_StartNormalJcon(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x10);
    return acwep_start_jcon(acp, wctrlp->wp->jcon_progno, wctrlp);
}

void acwep_SetFuncExe(_ac_weaponctrl *wctrlp, void *funcp, AcPlayerSt *acp)
{
    PS2_FRAME(0x10);
    wctrlp->actstep = 0;
    wctrlp->acttime = 0;
    wctrlp->actionfunc = (decltype(wctrlp->actionfunc))funcp;
    if (wctrlp->actionfunc) gcall(wctrlp->actionfunc, acp, wctrlp);
}

void acwep_SetFunc(_ac_weaponctrl *wctrlp, void *funcp)
{
    wctrlp->actstep = 0;
    wctrlp->acttime = 0;
    wctrlp->actionfunc = (decltype(wctrlp->actionfunc))funcp;
}

/* per frame: animate the weapon models, run the equipped weapon's (and an analog-mode sub weapon's) action function,
 * and the free function of every other slot */
void AcWep_Exec(AcPlayerSt *acp)
{
    PS2_FRAME(0x60);
    _ac_weaponctrl *w = acp->weaponctrl;
    for (int i = 0; i < acp->param.n_weapon; i++, w++) {
        if (w->wp->statid <= 6) {
            for (int j = 0; j < 2; j++) {
                StModel *mp = w->mp[j];
                if (mp && mp != acp->mp) {
                    Anim_Update(mp);
                    Anim_UpdateChildObjMatrix(mp->op, mp->matrix2);
                }
            }
        }
        int statid = w->wp->statid;
        if (statid == 5) continue;
        int enable = i == acp->equip_weapon || (acp->sepins_weappn != -1 && statid == 3);
        if (MyI.padoff == 1 || !enable) {
            if (!enable) {
                if (w->onfreefunc) gcall(w->onfreefunc, acp, w);
            } else if (acp->wep_release != 0 && w->onfreefunc) {
                gcall(w->onfreefunc, acp, w);
            }
            continue;
        }
        auto f = w->actionfunc;
        short step = w->actstep;
        if (f) gcall(f, acp, w);
        if (f == w->actionfunc && step == w->actstep && w->acttime < 0x4000) w->acttime++;
        if (w->busytime > 0) w->busytime--;
    }
}

/* spare weapon change (single player): jpos 1 = right hand gun, 2 = left hand */
void AcWep_ChangeWeapon(AcPlayerSt *acp, int jpos)
{
    PS2_FRAME(0x30);
    int ok;
    if (jpos == 1) {
        ok = __PickupNormalParts(acp, 1, 0);
        if (ok) AcLoader_ChangeIntoSpareWepTexture(acp->id, 0xe);
    } else if (jpos == 2) {
        ok = __PickupLeftHandParts(acp, 1, 0);
        if (ok) AcLoader_ChangeIntoSpareWepTexture(acp->id, 0xf);
    } else {
        ok = 1;   /* the original tests its never-written $s0 = the caller's acp (non-zero) */
    }
    if (ok) Voice_CallPoint(0x25, 0x7f, &acp->loc, 2500.0f);
}

int AcWep_IsPickUpWeapon(AcPlayerSt *acp)
{
    if (MyI.pikwep_en == 0) return 0;
    return acp->actno != 0x10 && acp->actno != 0x11;
}

void AcWep_PickUpWeapon(AcPlayerSt *acp, int itmid)
{
    PS2_FRAME(0x30);
    unsigned char *d = (unsigned char *)DF_ITM_GetDataAddr(itmid);
    if (!d) return;
    int ok;
    if (d[1] == 8) ok = acext_PickupExtension(acp, itmid);
    else if (d[1] == 2) ok = __PickupLeftHandParts(acp, 0, itmid);
    else ok = __PickupNormalParts(acp, 0, itmid);
    if (ok) Voice_CallPoint(0x25, 0x7f, &acp->loc, 2500.0f);
}

/* weapon part from the map (src 0, item itmid) or the spare right hand gun (src 1) into its empty slot */
static int __PickupNormalParts(AcPlayerSt *acp, int src, int itmid)
{
    PS2_FRAME(0x110);
    StModel *mp1 = 0, *mp2 = 0;
    int scale = 1, weporder = 0, psr = 0;
    AcDptsSt *dst1 = 0, *dst2 = 0;
    PartsParamBasicSt *partsp = 0;
    AcCtrl_GetEffect(acp);
    AcWepItemRec rec;
    ZeroMemory(&rec, 0x24);
    if (src == 0) {
        CopyMemory(&rec, DF_ITM_GetDataAddr(itmid), 0x24);
        mp1 = (StModel *)DF_ITM_GetModel(itmid, 0);
        mp2 = (StModel *)DF_ITM_GetModel(itmid, 1);
        DF_ITM_Delete(itmid);
    } else if (src == 1) {
        if (!acp->param.spgunp) return 0;
        PartsParamWeaponSt *wpp = AcPtsRes_GetWeaponPtr(acp->frdp->ptsresp, 0xb, 0);
        PartsParamGunSt *gun = acp->param.spgunp;
        rec.jpos = 1;
        rec.statid = 0;
        rec.nameid = (unsigned char)gun->name_id;
        rec.name2id = gun->name2_id;
        rec.weapon_id = gun->weapon_id;
        rec.blt_cnt = wpp->blt_sta;
        rec.blt_initcnt = wpp->blt_sta;
        rec.hp = gun->basic.hp;
        rec.partsp = &gun->basic;
        rec.hp2 = 0;
        rec.comb = AcPtsRes_GetSfxCombHead(acp->frdp->ptsresp, 0xb);
        rec.comb2 = 0;
        rec.partsid = acp->param.partsid[14];
        acp->param.spgunp = 0;
        mp1 = &MyRSW[acp->id];
        acp->exestat |= 2;
        g_pmodelArmWepR[acp->id] = mp1;
    }
    int statid = rec.statid, jpos = rec.jpos;
    if (jpos == 1 && acp->op[9].group == 0x40) return 0;
    if (jpos == 6 && (AcAsm_StatidToModelp(1, acp) || AcAsm_StatidToModelp(2, acp))) return 0;
    if (jpos == 5 && AcAsm_StatidToModelp(1, acp)) {
        statid = 2;
        jpos = 4;
        scale = -1;
    }
    if (AcAsm_StatidToModelp(statid, acp)) return 0;
    int widx = AcAsm_StatidToWindex(statid);
    _weapon_st *wp = &acp->param.weapons[widx];
    if (wp->statid != 5) return 0;
    switch (jpos) {
    case 1:
        acp->param.gunp = (PartsParamGunSt *)rec.partsp;
        acp->param.partsid[12] = rec.partsid;
        dst1 = AcPtsDstry_GetRightHandDestroyInfo(acp);
        partsp = &acp->param.gunp->basic;
        psr = 5;
        weporder = 0;
        AcPtsRes_ReRegisterWsfx(acp->frdp->ptsresp, psr, (char *)rec.comb);
        break;
    case 4:
        acp->param.Rbackp = (PartsParamBackSt *)rec.partsp;
        acp->param.partsid[10] = rec.partsid;
        dst1 = AcPtsDstry_GetRightBackDestroyInfo(acp);
        partsp = &acp->param.Rbackp->basic;
        psr = 7;
        weporder = 0;
        AcPtsRes_ReRegisterWsfx(acp->frdp->ptsresp, psr, (char *)rec.comb);
        break;
    case 5:
        acp->param.Lbackp = (PartsParamBackSt *)rec.partsp;
        acp->param.partsid[11] = rec.partsid;
        dst1 = AcPtsDstry_GetLeftBackDestroyInfo(acp);
        partsp = &acp->param.Lbackp->basic;
        psr = 8;
        weporder = 0;
        AcPtsRes_ReRegisterWsfx(acp->frdp->ptsresp, psr, (char *)rec.comb);
        break;
    case 6:
        acp->param.Lbackp = (PartsParamBackSt *)rec.partsp;
        acp->param.Rbackp = (PartsParamBackSt *)rec.partsp;
        acp->param.partsid[11] = rec.partsid;
        acp->param.partsid[10] = rec.partsid;
        dst1 = AcPtsDstry_GetRightBackDestroyInfo(acp);
        dst2 = AcPtsDstry_GetLeftBackDestroyInfo(acp);
        partsp = &acp->param.Lbackp->basic;
        weporder = 0;
        AcPtsRes_ReRegisterWsfx(acp->frdp->ptsresp, 7, (char *)rec.comb);
        psr = 8;
        AcPtsRes_ReRegisterWsfx(acp->frdp->ptsresp, psr, (char *)rec.comb2);
        break;
    case 7:
        weporder = 0;
        psr = 0xa;
        acp->param.extp = (PartsParamExtensionSt *)rec.partsp;
        acp->param.partsid[8] = rec.partsid;
        AcPtsRes_ReRegisterWsfx(acp->frdp->ptsresp, psr, (char *)rec.comb);
        break;
    }
    AcAsm_CalcWeightEx(&acp->param);
    AcAsm_GetWeaponParamEx(wp, psr, weporder, rec.weapon_id, statid, rec.nameid, (unsigned short)rec.name2id, &acp->param);
    wp->jpos = jpos;
    wp->blt_cnt = rec.blt_cnt;
    wp->blt_initcnt = rec.blt_initcnt;
    wp->nameid = rec.nameid;
    WEPICON_SetWepName(acp->id, widx);
    switch (jpos) {
    case 1:
        if (acp->param.Rarm_equipcolor != acp->param.gunp->equip_color) {
            acp->param.Rarm_equipcolor = acp->param.gunp->equip_color;
            GAcUtil_GetObjCmat(&acp->ocmats[5], acp->mp, (float)acp->param.Rarm_equipcolor, 1);
            memcpy(acp->ocmats[5].mat, IdentityMtx, 0x30);
        }
        acp->modeltbl[5] = mp1;
        break;
    case 5: acp->modeltbl[0] = mp1; break;
    case 4: acp->modeltbl[1] = mp1; break;
    case 6:
        acp->modeltbl[0] = mp1;
        acp->modeltbl[1] = mp2;
        break;
    case 7:
        acp->inside_mp = mp1;
        mp1->use |= 1;
        break;
    }
    ac_weaponctrl_init(acp, widx);
    if (mp1) mp1->scale[0] = (float)scale;
    if (mp2) mp2->scale[0] = (float)scale;
    if (dst1 && partsp) {
        dst1->hp = rec.hp;
        dst1->maxhp = partsp->hp;
        dst1->price = partsp->price;
    }
    if (dst2 && partsp) {
        dst2->hp = rec.hp2;
        dst2->maxhp = partsp->hp;
        dst2->price = partsp->price;
    }
    if (acp->wep_release) {
        acwep_equip(acp, widx);
        acp->next_weapon = acp->equip_weapon;
    }
    return 1;
}

/* left hand part from the map (src 0) or the spare (src 1); returns the blade pickup result / 1 for a laser gun */
static int __PickupLeftHandParts(AcPlayerSt *acp, int src, int itmid)
{
    PS2_FRAME(0x90);
    int ret = 0;
    StModel *mp = 0;
    if (acp->op[5].group == 0x40) return 0;
    if (acp->param.swordp || acp->modeltbl[4]) return 0;
    AcWepItemRecLH rec;
    ZeroMemory(&rec, 0x24);
    if (src == 0) {
        CopyMemory(&rec, DF_ITM_GetDataAddr(itmid), 0x24);
        mp = (StModel *)DF_ITM_GetModel(itmid, 0);
        DF_ITM_Delete(itmid);
    } else if (src == 1) {
        if (!acp->param.spswordp) return 0;
        PartsParamWeaponSt *wpp = AcPtsRes_GetWeaponPtr(acp->frdp->ptsresp, 0xc, 0);
        PartsParamSwordSt *sw = acp->param.spswordp;
        rec.statid = 0xff;
        rec.jpos = 2;
        rec.larm_type = (unsigned char)AcAsm_GetLeftArmTypeEx(sw, acp->param.armsp->type);
        rec.hp = sw->basic.hp;
        rec.partsid = acp->param.partsid[15];
        rec.swordp = sw;
        rec.comb = AcPtsRes_GetSfxCombHead(acp->frdp->ptsresp, 0xc);
        if (!wpp) {
            rec.blt_cnt = 0;
            rec.blt_initcnt = 0;
        } else {
            rec.blt_cnt = wpp->blt_sta;
            rec.blt_initcnt = wpp->blt_sta;
        }
        acp->param.spswordp = 0;
        mp = &MyLSW[acp->id];
        acp->exestat |= 2;
        g_pmodelArmWepL[acp->id] = mp;
    }
    acp->param.partsid[13] = rec.partsid;
    acp->param.swordp = rec.swordp;
    acp->param.Larm_type = rec.larm_type;
    AcPtsRes_ReRegisterWsfx(acp->frdp->ptsresp, 6, (char *)rec.comb);
    acp->param.Larm_equipcolor = acp->param.swordp->equip_color;
    GAcUtil_GetObjCmat(&acp->ocmats[4], acp->mp, (float)acp->param.Larm_equipcolor, 1);
    memcpy(acp->ocmats[4].mat, IdentityMtx, 0x30);
    acp->modeltbl[4] = mp;
    switch (acp->param.Larm_type) {
    case 0: case 1: case 2:
        ret = acblade_Pickup(AcCtrl_GetPartsBlade(acp));
        break;
    case 3: {
        AcPartsLgunSt *lg = AcCtrl_GetPartsLgun(acp);
        AcLgun_Init(AcCtrl_GetPartsLgun(acp), acp->param.swordp, acp);
        if (lg) {
            _ac_weaponctrl *wc = AcLgun_GetWeaponCtrl(lg);
            ret = 1;
            wc->wp->blt_cnt = rec.blt_cnt;
            wc->wp->blt_initcnt = rec.blt_initcnt;
        }
        break;
    }
    }
    AcDptsSt *dst = AcPtsDstry_GetLeftHandDestroyInfo(acp);
    if (dst && acp->param.swordp) {
        dst->hp = rec.hp;
        dst->maxhp = acp->param.swordp->basic.hp;
        dst->price = acp->param.swordp->basic.price;
    }
    AcAsm_CalcWeightEx(&acp->param);
    WEPICON_SetWepName(acp->id, 4);
    return ret;
}

/* drop (lost == 0: as a map item) or lose the left hand part; a spare is then changed in */
int AcWep_RemoveLeftHandParts(AcPlayerSt *acp, int lost)
{
    PS2_FRAME(0x60);
    AcEffSt *effp = acp->frdp->effp;
    int type = acp->param.Larm_type;
    if (type == 4 || !acp->param.swordp || !acp->modeltbl[4]) return 0;
    if (type == 3 && !AcLgun_IsRemovePossible(AcCtrl_GetPartsLgun(acp))) return 0;
    int oldtype = acp->param.Larm_type;
    if (!lost) Voice_CallPoint(0x24, 0x7f, &acp->loc, 2500.0f);
    if (acp->param.Larm_equipcolor == 8) DF_MAGIC2_AppearMagicOCMP(2, &acp->ocmats[4], 4);
    if (acp->param.Larm_type == 2 && acp->shieldstat == 1) ACsetact_shieldoff(acp);
    if (!lost) {
        int id = DF_ITM_New(0, 0, acp->modeltbl[4], 0, acp->floor_high, 0);
        AcWepItemRecLH *d = (AcWepItemRecLH *)DF_ITM_GetDataAddr(id);
        if (d) {
            AcDptsSt *dst = AcPtsDstry_GetLeftHandDestroyInfo(acp);
            d->statid = 0xff;
            d->jpos = 2;
            d->larm_type = acp->param.Larm_type;
            d->hp = dst->hp;
            d->partsid = acp->param.partsid[13];
            d->swordp = acp->param.swordp;
            d->comb = AcPtsRes_GetSfxCombHead(acp->frdp->ptsresp, 6);
            if (acp->param.Larm_type == 3) {
                AcPartsLgunSt *lg = AcCtrl_GetPartsLgun(acp);
                if (lg) {
                    _weapon_st *lwp = AcLgun_GetWeaponCtrl(lg)->wp;
                    d->blt_cnt = lwp->blt_cnt;
                    d->blt_initcnt = lwp->blt_initcnt;
                    AcWep_SettleShell(acp, lwp);
                }
            }
            DF_ITM_SetString(id, 0xc);
        }
    } else {
        acp->modeltbl[4]->use |= 1;
    }
    acp->modeltbl[4] = 0;
    if (acp->param.Larm_type == 2) aceff_FreeShield(effp, 0);
    else aceff_FreeBlade(effp, 0);
    acp->param.Larm_type = 4;
    acp->param.swordp = 0;
    acp->param.partsid[13] = 0;
    AcPtsRes_RegisterWsfx(acp->frdp->ptsresp, 6, 0);
    switch (oldtype) {
    case 3: AcLgun_Remove(AcCtrl_GetPartsLgun(acp)); break;
    case 0: case 1: case 2: acblade_Remove(AcCtrl_GetPartsBlade(acp)); break;
    }
    AcAsm_CalcWeightEx(&acp->param);
    if (acp->param.spswordp) {
        AcAct_SetChangeSpare(acp);
        acp->reserve_spare = 2;
    }
    return 1;
}

/* a back unit of this type was removed: radar 1 -> recompute the radar, stealth 2 / shoulder booster 5 lost */
static void acwep_back_removed(AcPlayerSt *acp, unsigned int type)
{
    switch (type) {
    case 1: AcAsm_CalcRadarParamEx(&acp->param); break;
    case 2: acp->param.abl_stealth = 0; break;
    case 5:
        acp->param.abl_shbooster = 0;
        AcAsm_CalcWeightEx(&acp->param);
        break;
    }
}

/* drop (lost == 0: as a map item) or lose a weapon part; the equipped weapon is released first (0 if its release
 * function refuses) and the next weapon selected */
int AcWep_RemoveWeapon(AcPlayerSt *acp, _ac_weaponctrl *wctrlp, int lost)
{
    PS2_FRAME(0x70);
    _weapon_st *wp = wctrlp->wp;
    int jpos = wp->jpos;
    if (jpos == 7) return 0;
    _ac_weaponctrl *cur = &acp->weaponctrl[acp->equip_weapon];
    if (wctrlp == cur) {
        acp->wep_release = 1;
        int r = 0;
        if (cur->onreleasefunc) r = gcall(cur->onreleasefunc, acp, cur);
        Lock_SetSight(&acp->lockmng, 0, 0);
        Lock_ClearMng(&acp->lockmng, 0);
        if (!r) return 0;
        if (!__ChangeNextWeapon(acp, 0)) {
            acp->next_weapon = 0;
            acp->equip_weapon = 0;
        }
    }
    if (acp->reserve_weapon && wctrlp == &acp->weaponctrl[acp->next_weapon]) {
        acp->reserve_weapon = 0;
        acp->next_weapon = acp->equip_weapon;
    }
    if (!lost) {
        Voice_CallPoint(0x24, 0x7f, &acp->loc, 2500.0f);
        __MakeItemRemoveParts(acp, wctrlp);
    } else if (wctrlp->wp->jpos == 7) {   /* dead: jpos 7 returned above */
        if (acp->inside_mp) acp->inside_mp->use |= 1;
    } else {
        if (wctrlp->mp[0]) wctrlp->mp[0]->use |= 1;
        if (wctrlp->mp[1]) wctrlp->mp[1]->use |= 1;
    }
    if (wctrlp->iregEff != -1) aceff_FreeWeapon(acp->frdp->effp, wctrlp->iregEff);
    switch (jpos) {
    case 1:
        acp->modeltbl[5] = 0;
        acp->param.gunp = 0;
        acp->param.partsid[12] = 0;
        AcPtsRes_ReRegisterWsfx(acp->frdp->ptsresp, 5, 0);
        if (acp->param.Rarm_equipcolor == 0xb) DF_MAGIC2_AppearMagicOCMP(2, &acp->ocmats[5], 4);
        if (wp->prog_no == 4) acrb_NoRbAnimGroup(AcCtrl_GetPartsRblade(acp));
        break;
    case 4: {
        unsigned int t = acp->param.Rbackp->type;
        acp->modeltbl[1] = 0;
        acp->param.Rbackp = 0;
        acp->param.partsid[10] = 0;
        AcPtsRes_ReRegisterWsfx(acp->frdp->ptsresp, 7, 0);
        DF_MAGIC2_AppearMagicOCMP(2, &acp->ocmats[1], 4);
        acwep_back_removed(acp, t);
        break;
    }
    case 5: {
        unsigned int t = acp->param.Lbackp->type;
        acp->modeltbl[0] = 0;
        acp->param.Lbackp = 0;
        acp->param.partsid[11] = 0;
        AcPtsRes_ReRegisterWsfx(acp->frdp->ptsresp, 8, 0);
        DF_MAGIC2_AppearMagicOCMP(2, &acp->ocmats[0], 4);
        acwep_back_removed(acp, t);
        break;
    }
    case 6: {
        unsigned int t = acp->param.Rbackp->type;
        acp->modeltbl[0] = 0;
        acp->modeltbl[1] = 0;
        acp->param.Lbackp = 0;
        acp->param.Rbackp = 0;
        acp->param.partsid[11] = 0;
        acp->param.partsid[10] = 0;
        AcPtsRes_ReRegisterWsfx(acp->frdp->ptsresp, 8, 0);
        AcPtsRes_ReRegisterWsfx(acp->frdp->ptsresp, 7, 0);
        DF_MAGIC2_AppearMagicOCMP(2, &acp->ocmats[0], 4);
        DF_MAGIC2_AppearMagicOCMP(2, &acp->ocmats[1], 4);
        acwep_back_removed(acp, t);
        if (t == 5) {
            aceff_FreeBoost(acp->frdp->effp, 0xc);
            aceff_FreeBoost(acp->frdp->effp, 0xd);
        }
        break;
    }
    case 7:   /* dead */
        acp->inside_mp = 0;
        acp->param.extp = 0;
        acp->param.partsid[8] = 0;
        AcPtsRes_ReRegisterWsfx(acp->frdp->ptsresp, 0xa, 0);
        break;
    }
    AcAsm_CalcWeightEx(&acp->param);
    AcWep_SettleShell(acp, wp);
    wp->statid = 5;
    wp->jpos = 0;
    wctrlp->busytime = 0;
    wctrlp->onequipfunc = 0;
    if (jpos == 1 && acp->param.spgunp) {
        AcAct_SetChangeSpare(acp);
        acp->reserve_spare = 1;
    }
    return 1;
}

/* the removed weapon becomes a map item (an extension is moved to the inside part's model first) */
static void __MakeItemRemoveParts(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x60);
    _weapon_st *wp = wctrlp->wp;
    int id;
    if (wp->statid == 3) {
        StModel *m = acp->inside_mp;
        sceVu0CopyMatrix(m->matrix2, *acp->mp->op[4].wrmatp);
        GAcUtil_CopyParentModel(m, acp->mp);
        m->use &= 0xfffe;
        id = DF_ITM_New(0, 0, m, 0, acp->floor_high, 0);
    } else {
        id = DF_ITM_New(0, 0, wctrlp->mp[0], wctrlp->mp[1], acp->floor_high, 0);
    }
    AcWepItemRec *d = (AcWepItemRec *)DF_ITM_GetDataAddr(id);
    if (!d) return;
    if (wp->statid == 2) {
        d->statid = 1;
        d->jpos = 5;
        if (wctrlp->mp[0]) wctrlp->mp[0]->scale[0] = 1.0f;
        if (wctrlp->mp[1]) wctrlp->mp[1]->scale[0] = 1.0f;
    } else {
        d->statid = wp->statid;
        d->jpos = wp->jpos;
    }
    d->nameid = wp->nameid;
    d->name2id = wp->name2id;
    d->weapon_id = (short)wp->id;
    d->blt_cnt = wp->blt_cnt;
    d->blt_initcnt = wp->blt_initcnt;
    switch (wp->jpos) {
    case 1:
        d->hp = AcPtsDstry_GetRightHandDestroyInfo(acp)->hp;
        d->hp2 = 0;
        d->partsp = &acp->param.gunp->basic;
        d->comb = AcPtsRes_GetSfxCombHead(acp->frdp->ptsresp, 5);
        d->comb2 = 0;
        d->partsid = acp->param.partsid[12];
        DF_ITM_SetString(id, 0xb);
        break;
    case 4:
        d->hp = AcPtsDstry_GetRightBackDestroyInfo(acp)->hp;
        d->hp2 = 0;
        d->partsp = &acp->param.Rbackp->basic;
        d->comb = AcPtsRes_GetSfxCombHead(acp->frdp->ptsresp, 7);
        d->comb2 = 0;
        d->partsid = acp->param.partsid[1];
        DF_ITM_SetString(id, 0xd);
        break;
    case 5:
        d->hp = AcPtsDstry_GetLeftBackDestroyInfo(acp)->hp;
        d->hp2 = 0;
        d->partsp = &acp->param.Lbackp->basic;
        d->comb = AcPtsRes_GetSfxCombHead(acp->frdp->ptsresp, 8);
        d->comb2 = 0;
        d->partsid = acp->param.partsid[0];
        DF_ITM_SetString(id, 0xd);
        break;
    case 6: {
        AcDptsSt *r = AcPtsDstry_GetRightBackDestroyInfo(acp);
        AcDptsSt *l = AcPtsDstry_GetLeftBackDestroyInfo(acp);
        d->hp = r->hp;
        d->hp2 = (short)l->hp;
        d->partsp = &acp->param.Rbackp->basic;
        d->comb = AcPtsRes_GetSfxCombHead(acp->frdp->ptsresp, 7);
        d->comb2 = AcPtsRes_GetSfxCombHead(acp->frdp->ptsresp, 8);
        d->partsid = acp->param.partsid[1];
        DF_ITM_SetString(id, 0xd);
        break;
    }
    case 7:
        d->hp = 0;
        d->hp2 = 0;
        d->partsp = &acp->param.extp->basic;
        d->comb = AcPtsRes_GetSfxCombHead(acp->frdp->ptsresp, 0xa);
        d->comb2 = 0;
        d->partsid = acp->param.partsid[8];
        DF_ITM_SetString(id, 0xe);
        break;
    default:
        d->hp = 0;
        d->hp2 = 0;
        d->partsp = 0;
        d->comb = 0;
        d->comb2 = 0;
        d->partsid = 0;
        break;
    }
}

/* the weapon's fire key: auto-lock missiles fire on their own once nothing is left to lock; trigger weapons (and
 * the analog-mode sub weapon) fire on the press, the others while held */
int AcWep_IsShotKeyOn(AcPlayerSt *acp, _ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x40);
    _weapon_st *wp = wctrlp->wp;
    if (!AcWep_IsShotEnable(acp, (int)(wctrlp - acp->weaponctrl))) return 0;
    if (wp->prog_no == 1 && wp->auto_missile && acp->lockmng.rest_lock == 0) return 1;
    AcUidevSt *u = acp->frdp->uidevp;
    int sub = acp->sepins_weappn != -1 && wctrlp->wp->statid == 3;
    if (wp->trigshot) return AcUidev_IsPadTrgConf(u, sub ? 0xf : 6) != 0;
    if (sub) return AcUidev_IsPadTrgConf(u, 0xf) != 0;
    return AcUidev_IsPadOnConf(u, 6) != 0;
}

int acwep_GetFarPos(float *trgpos, AcPlayerSt *acp, float rotx, float roty)
{
    PS2_FRAME(0x80);
    acwep_far_pos(trgpos, acp, rotx, roty);
    return 0;
}

/* the locked target's position (1) unless the equipped weapon is a missile; else (0) the far point along the view */
int acwep_GetTargetPos(float *trgpos, AcPlayerSt *acp)
{
    PS2_FRAME(0xc0);
    StWepLock *lk = &acp->lockmng.lock[0];
    if (acp->weaponctrl[acp->equip_weapon].wp->prog_no != 1 && lk->ofp && (lk->lock_stat & 2)) {
        memcpy(trgpos, lk->dloc, 16);
        if (trgpos[3] == 0.0f) acwep_far_pos(trgpos, acp, -acp->view.ang[0], acp->ang[1]);
        return 1;
    }
    acwep_far_pos(trgpos, acp, -acp->view.ang[0], acp->ang[1]);
    return 0;
}

/* lock-on request from the AC's chest along the view (heading turned by pi into -pi..pi); weapons without lock only
 * hand their sight over (the sub weapon's sight in slot 1) */
void AcWep_GetStdLock(AcPlayerSt *acp, _ac_weaponctrl *wctrlp, int isMainWep)
{
    PS2_FRAME(0x30);
    PS2_STACK(float[4], ang, 0x10);
    PS2_STACK(float[4], pos, 0x20);
    if (acp->areaovered) return;
    if (wctrlp->wp->statid == 3) return;
    pos[0] = acp->loc[0];
    pos[1] = acp->loc[1] - 0.5f * acp->tall;
    pos[2] = acp->loc[2];
    pos[3] = 1.0f;
    ang[0] = acp->view.ang[0];
    float yaw = PI_F + acp->ang[1];
    ang[1] = yaw;
    while (!(yaw <= PI_F)) yaw = yaw - TWO_PI_F;
    ang[1] = yaw;
    while (yaw < -PI_F) yaw = yaw + TWO_PI_F;
    ang[1] = yaw;
    ang[2] = 0.0f;
    ang[3] = 1.0f;
    _weapon_st *wp = wctrlp->wp;
    if (wp->lockmax > 0) {
        if (!(acp->param.plus & 0x200) && acp->param.corep->max_wp < acp->param.core_weight) acp->lockmng.flag |= 0x10;
        else acp->lockmng.flag &= 0xffef;
        Lock_GetLockX(&acp->lockmng, pos, ang, wctrlp->wp);
    } else {
        acp->lockmng.psight[isMainWep == 0 ? 1 : 0] = &wp->sight;
        acp->lockmng.vangx = ang[0];
        acp->lockmng.vangy = ang[1];
    }
}

void weapon_changeanim(_ac_weaponctrl *wctrlp, int animid, int hokantime, int loop)
{
    PS2_FRAME(0x50);
    if (wctrlp->wp->statid > 6) return;
    if (acwep_anim_ok(wctrlp->mp[0], animid)) Anim_ChangeAnim(wctrlp->mp[0], animid, hokantime, loop);
    if (acwep_anim_ok(wctrlp->mp[1], animid)) Anim_ChangeAnim(wctrlp->mp[1], animid, hokantime, loop);
}

int AcWep_IsAnimEnd(_ac_weaponctrl *wctrlp, int animid)
{
    PS2_FRAME(0x10);
    if (wctrlp->wp->statid <= 6) return Anim_IsEndAnim(wctrlp->mp[0], animid);
    return 1;
}

void weapon_setanim(_ac_weaponctrl *wctrlp, int animid, int loop)
{
    PS2_FRAME(0x40);
    if (wctrlp->wp->statid > 6) return;
    if (acwep_anim_ok(wctrlp->mp[0], animid)) Anim_SetAnim(wctrlp->mp[0], animid, loop);
    if (acwep_anim_ok(wctrlp->mp[1], animid)) Anim_SetAnim(wctrlp->mp[1], animid, loop);
}

/* the weapon may fire now: keys enabled, not released (except the analog sub weapon), the weapon change key not held,
 * ammunition, not busy, energy (energy weapons, not while charging), a lock target (missiles), stance (cannons) */
int AcWep_IsShotEnable(AcPlayerSt *acp, int weapon_id)
{
    PS2_FRAME(0x40);
    if (weapon_id < 0 || weapon_id >= acp->param.n_weapon) return 0;
    _ac_weaponctrl *w = &acp->weaponctrl[weapon_id];
    _weapon_st *wp = w->wp;
    if (!acp->key_enable) return 0;
    if (acp->wep_release != 0 && (acp->sepins_weappn == -1 || wp->statid != 3)) return 0;
    AcUidevSt *u = acp->frdp->uidevp;
    int on = AcUidev_IsAnalog(u) ? AcUidev_IsPadOnConf(u, 0x10) : AcUidev_IsPadOnMaskedEqual(u, 0xf00, 0xf00);
    if (on && (acp->sepins_weappn == -1 || w->wp->statid != 3)) return 0;
    if (wp->blt_cnt == 0) return 0;
    if (w->busytime != 0) return 0;
    if (wp && (wp->dstat.effect & 0x20)) {
        if (AcCtrl_IsCharging(acp)) return 0;
        if (acp->energyReal < wp->blt_price) return 0;
    }
    if (wp->prog_no == 1) {
        DF_init_next_lock(&w->nextlock, &acp->lockmng);
        if (!DF_get_next_lock(&w->nextlock)) return 0;
    }
    if (wp->prog_no == 2) {
        if (!acp->param.abl_walkcannon && acp->actno != 0x10) return 0;
        if (!acp->param.abl_flycannon && (acp->actstat & 1)) return 0;
    }
    return 1;
}

/* one shot: energy weapons drain energy (and still count down); 9999 = infinite */
int AcWep_DecBullet(AcPlayerSt *acp, _weapon_st *wp)
{
    PS2_FRAME(0x20);
    if (wp && (wp->dstat.effect & 0x20) && !AcCtrl_EnergyDrain(acp, wp->blt_price)) return 0;
    unsigned int c = wp->blt_cnt;
    if (c >= 0x270f) return 1;
    if (c == 0) {
        wp->blt_cnt = 0;
        return 0;
    }
    wp->blt_cnt = (unsigned short)(c - 1);
    return 1;
}

/* release / equip bodies (MWCC inlines them into SelectWeapon / __PickupNormalParts: no frame of their own, so the
 * interpreted callbacks see the caller's stack) */
static int acwep_release(AcPlayerSt *acp, int wepno)
{
    acp->wep_release = 1;
    _ac_weaponctrl *w = &acp->weaponctrl[wepno];
    int r = 0;
    if (w->onreleasefunc) r = gcall(w->onreleasefunc, acp, w);
    Lock_SetSight(&acp->lockmng, 0, 0);
    Lock_ClearMng(&acp->lockmng, 0);
    return r;
}

/* equip slot wepno unless the AC has no weapon left (then only the lock is cleared) */
static void acwep_equip(AcPlayerSt *acp, int wepno)
{
    int cnt = 0;
    for (int i = 0; i < acp->param.n_weapon; i++)
        if (acp->param.weapons[i].statid != 5) cnt++;
    if (cnt <= 0) {
        if (MyI.padoff != 1 && acp->wep_release) Lock_ClearMng(&acp->lockmng, 0);
        return;
    }
    acp->equip_weapon = (unsigned char)wepno;
    acp->wep_release = 0;
    _ac_weaponctrl *w = &acp->weaponctrl[wepno];
    if (w->onequipfunc) gcall(w->onequipfunc, acp, w);
}

int AcWep_ReleaseWeapon(AcPlayerSt *acp, int wepno)
{
    PS2_FRAME(0x30);
    return acwep_release(acp, wepno);
}

void AcWep_EquipWeapon(AcPlayerSt *acp, int wepno)
{
    PS2_FRAME(0x10);
    acwep_equip(acp, wepno);
}

/* per frame (pad on): change key + drop key = drop the equipped weapon; change key = choose the next weapon (weapon
 * arms first toggle their arm weapon type); a pending change happens once the old weapon's equip / release animations
 * have ended outside the busy actions. Returns 1 except for the early outs. */
int AcWep_SelectWeapon(AcPlayerSt *acp)
{
    PS2_FRAME(0x70);
    int change = 0;
    if (acp->param.n_weapon == 0) return 0;
    AcUidevSt *u = acp->frdp->uidevp;
    _ac_weaponctrl *cur = &acp->weaponctrl[acp->equip_weapon];
    int key = AcUidev_IsAnalog(u) ? AcUidev_IsPadTrgConf(u, 6) : AcUidev_IsPadTrgConf(u, 4);
    if (key) {
        int on = AcUidev_IsAnalog(u) ? AcUidev_IsPadOnConf(u, 0x10) : AcUidev_IsPadOnMaskedEqual(u, 0xf00, 0xf00);
        if (on) {
            if (cur->busytime > 0) return 0;
            _ac_weaponctrl *w = &acp->weaponctrl[acp->equip_weapon];
            if ((!AcUidev_IsPadOnConf(u, 6) || AcUidev_IsAnalog(u)) && w->busytime <= 0 && (unsigned int)(w->wp->statid - 5) >= 2)
                AcWep_RemoveWeapon(acp, &acp->weaponctrl[acp->equip_weapon], 0);
            return 0;
        }
    }
    if (__ChangeNextWeaponUi(acp, 1) && AcCtrl_IsMyView(acp)) Voice_Call(0x23, 0x7f);
    _weapon_st *wp = cur->wp;
    if (wp->statid == 6 && acp->aw_type != acp->next_aw_type) {
        int a = acp->actno;
        if (a != 2 && a != 0xb && a != 0xd && a != 0xe && a != 0xf) {
            PartsParamArmsSt *arms = acp->param.armsp;
            unsigned short keep = wp->blt_cnt, init = wp->blt_initcnt;
            acp->aw_type = acp->next_aw_type;
            if (acp->aw_type == 0) {
                AcAsm_GetWeaponParamEx(wp, 2, 0, arms->weapon_id, 6, arms->name_id, arms->name2_id, &acp->param);
                wp->equip_anmid = acwep_next_slot(acp) >= 0 ? 1 : 7;
                wp->release_anmid = 2;
                wp->shot_anmid = 3;
                wp->blt_cnt = keep;
                wp->blt_initcnt = init;
                short busy = acp->weaponctrl[acp->equip_weapon].busytime;
                _ac_weaponctrl *w = &acp->weaponctrl[acp->equip_weapon];
                ac_weaponctrl_init(acp, acp->equip_weapon);
                w->busytime = busy;
                if (acp->equip_weapon == acp->next_weapon) change = 1;
            } else {
                AcAsm_GetWeaponParamEx(wp, 2, 1, arms->weapon2_id, 6, arms->name_id, arms->name2_id, &acp->param);
                wp->equip_anmid = 4;
                wp->release_anmid = 5;
                wp->shot_anmid = 6;
                wp->blt_cnt = keep;
                wp->blt_initcnt = init;
                short busy = acp->weaponctrl[acp->equip_weapon].busytime;
                _ac_weaponctrl *w = &acp->weaponctrl[acp->equip_weapon];
                ac_weaponctrl_init(acp, acp->equip_weapon);
                w->busytime = busy;
                change = 1;
            }
        }
    }
    if (acp->reserve_weapon != 0 && cur->busytime == 0) {
        int e = cur->wp->statid <= 6 ? Anim_IsEndAnim(cur->mp[0], cur->wp->equip_anmid) : 1;
        if (e) {
            int r = cur->wp->statid <= 6 ? Anim_IsEndAnim(cur->mp[0], cur->wp->release_anmid) : 1;
            if (r) {
                int wait = 0;
                if (cur->wp->prog_no == 4) {
                    if (acp->rb_stat != 0) wait = 1;
                    else wait = !Anim_IsEndAnim(cur->mp[0], cur->wp->release_anmid);
                }
                int a = acp->actno;
                if (!wait && a != 2 && a != 0xb && a != 0xc && a != 0xd && a != 0xe && a != 0xf) change = 1;
            }
        }
    }
    if (change) {
        acwep_release(acp, acp->equip_weapon);
        acwep_equip(acp, acp->next_weapon);
        acp->reserve_weapon = 0;
    }
    return 1;
}

/* weapon change key: weapon arms with ammunition (or of type 2) first switch the arm weapon type (to 1, or back to 0
 * when another weapon exists), then the next selectable slot (an empty arm weapon is skipped on wrap) is reserved */
static int __ChangeNextWeaponUi(AcPlayerSt *acp, int keychk)
{
    PS2_FRAME(0x20);
    if (keychk && !AcUidev_IsPadTrgConf(acp->frdp->uidevp, 4)) return 0;
    int armtype = acp->param.armsp->type;
    if (armtype != 0 && acp->next_weapon == 0) {
        _weapon_st *w0 = acp->weaponctrl[acp->next_weapon].wp;
        int cnt = 0x270f;
        if (armtype != 2) {
            cnt = w0->blt_cnt;
            if (cnt == 0) {
                w0->equip_anmid = 1;
                w0->release_anmid = 2;
                w0->shot_anmid = 3;
            }
        }
        if (cnt != 0) {
            if (acp->aw_type == 0) {
                acp->next_aw_type = 1;
                return 1;
            }
            acp->next_aw_type = 0;
            if (acwep_next_slot(acp) < 0) return 1;
        }
    }
    int n = acp->param.n_weapon;
    if (n < 2) return 0;
    unsigned int old = acp->next_weapon, v = old;
    for (signed char t = (signed char)n; t >= 0; t--) {
        v = (v + 1) & 0xff;
        if ((int)v >= n) {
            v = 0;
            if (acp->param.armsp->type == 1 && acp->weaponctrl[0].wp->blt_cnt == 0) continue;
        }
        int s = acp->param.weapons[v].statid;
        if (s == 5) continue;
        if (acp->sepins_weappn == -1 || s != 3) break;
    }
    if (acp->param.weapons[v].statid == 5) return 0;
    acp->next_weapon = (unsigned char)v;
    if (v == old) return 0;
    acp->reserve_weapon = 1;
    return 1;
}

/* the next selectable slot after next_weapon is reserved (with keychk: only on the change key; change + drop key
 * drops the equipped weapon instead) */
static int __ChangeNextWeapon(AcPlayerSt *acp, int keychk)
{
    PS2_FRAME(0x30);
    if (keychk) {
        AcUidevSt *u = acp->frdp->uidevp;
        int key = AcUidev_IsAnalog(u) ? AcUidev_IsPadTrgConf(u, 6) : AcUidev_IsPadTrgConf(u, 4);
        if (!key) return 0;
        int on = AcUidev_IsAnalog(u) ? AcUidev_IsPadOnConf(u, 0x10) : AcUidev_IsPadOnMaskedEqual(u, 0xf00, 0xf00);
        if (on) {
            _ac_weaponctrl *w = &acp->weaponctrl[acp->equip_weapon];
            if ((!AcUidev_IsPadOnConf(u, 6) || AcUidev_IsAnalog(u)) && w->busytime <= 0 && (unsigned int)(w->wp->statid - 5) >= 2)
                AcWep_RemoveWeapon(acp, &acp->weaponctrl[acp->equip_weapon], 0);
            return 0;
        }
    }
    int n = acp->param.n_weapon;
    if (n < 2) return 0;
    unsigned int old = acp->next_weapon, v = old;
    for (;;) {   /* the original's try counter is an unsigned char tested >= 0: it never ends the search */
        v = (v + 1) & 0xff;
        if ((int)v >= n) v = 0;
        int s = acp->param.weapons[v].statid;
        if (s != 5 && (acp->sepins_weappn == -1 || s != 3)) break;
    }
    if (acp->param.weapons[v].statid == 5) return 0;
    acp->next_weapon = (unsigned char)v;
    if (v == old) return 0;
    acp->reserve_weapon = 1;
    return 1;
}

/* the next muzzle matrix (at most 6) from mp's object with this colour id */
static int add_1muzzle(_ac_weaponctrl *wctrlp, StModel *mp, float color)
{
    PS2_FRAME(0x20);
    if (!mp) return 0;
    int n = wctrlp->nmuzzle;
    if ((unsigned int)n >= 6) return 0;
    if (GAcUtil_GetObjCmat(&wctrlp->ocmats[n], mp, color, 0) == -1) return 0;
    wctrlp->nmuzzle++;
    return 1;
}

/* muzzles = objects of colour 12..17 of both models; none: one identity muzzle on the first model */
void get_allmuzzle(_ac_weaponctrl *wctrlp)
{
    PS2_FRAME(0x40);
    wctrlp->nmuzzle = 0;
    for (unsigned int i = 0; i < 6; i++) {
        float c = 12.0f + (float)(int)i;
        add_1muzzle(wctrlp, wctrlp->mp[0], c);
        add_1muzzle(wctrlp, wctrlp->mp[1], c);
    }
    if (wctrlp->nmuzzle != 0) return;
    wctrlp->nmuzzle = 1;
    wctrlp->ocmats[0].parentmatp = wctrlp->mp[0] ? &wctrlp->mp[0]->matrix2 : &IdentityMtx;
    sceVu0CopyMatrix(wctrlp->ocmats[0].mat, IdentityMtx);
}

void init_weaponmodel(StModel *mp)
{
    static const float zero1[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    memcpy(mp->loc, zero1, 16);
    memcpy(mp->ang, zero1, 16);
    for (int k = 0; k < mp->nobj; k++) {
        mp->op[k].group = 1;
        mp->op[k].todid = 0xff;
    }
    Anim_SetAnim(mp, 0, 0);
}

/* slot weapon_index: its models by weapon kind (statid), callbacks cleared, models reset, muzzles and the cartridge
 * matrix, then the weapon type's Init (overlay) */
static int ac_weaponctrl_init(AcPlayerSt *acp, int weapon_index)
{
    PS2_FRAME(0x70);
    _ac_weaponctrl *w = &acp->weaponctrl[weapon_index];
    _weapon_st *wp = &acp->param.weapons[weapon_index];
    StModel *m1 = 0, *m2 = 0;
    switch (wp->statid) {
    case 0: m1 = acp->modeltbl[5]; break;
    case 1: m1 = acp->modeltbl[0]; break;
    case 2: m1 = acp->modeltbl[1]; break;
    case 3:
        m2 = acp->modeltbl[7];
        m1 = acp->modeltbl[6];
        if (AcUidev_IsAnalog(acp->frdp->uidevp)) acp->sepins_weappn = (char)weapon_index;
        break;
    case 4: case 5:
        m2 = acp->modeltbl[1];
        m1 = acp->modeltbl[0];
        break;
    case 6:
        m2 = acp->modeltbl[5];
        m1 = acp->modeltbl[4];
        break;
    }
    w->wp = wp;
    w->mp[0] = m1;
    w->mp[1] = m2;
    w->busytime = 0;
    w->actstep = 0;
    w->acttime = 0;
    w->actionfunc = 0;
    w->onequipfunc = 0;
    w->ontrigfunc = 0;
    w->shotcbfunc = 0;
    w->managefunc = 0;
    w->onreleasefunc = 0;
    w->subspells = 0;
    if (m1 && m1 != acp->mp) init_weaponmodel(m1);
    if (m2 && m2 != acp->mp) init_weaponmodel(m2);
    get_allmuzzle(w);
    GAcUtil_GetObjCmat(&w->ocmatsCartridge[0], w->mp[0], 22.0f, 0);
    if (acp->param.abl_Wblade && weapon_index == 0) {
        acblade_WbladeInit(acp, w);
        return 1;
    }
    switch (wp->prog_no) {
    case 4: acrb_Init(AcCtrl_GetPartsRblade(acp), w, acp); break;
    case 2: AcWepCannon_Init(acp, w); break;
    case 1: AcWepMissile_Init(acp, w); break;
    case 0: AcWepGun_Init(acp, w); break;
    }
    return 1;
}

/* all slots, the laser blade gun, the starting weapon (the first selectable slot, 0 if none; released), the weapon
 * arm's joint control and equip animation, then the starting weapon's (analog: the sub weapon's) equip function */
void AcWep_InitAllWeapons(AcPlayerSt *acp)
{
    PS2_FRAME(0x40);
    acp->aw_type = 0;
    acp->next_aw_type = acp->aw_type;
    acp->sepins_weappn = -1;
    acrb_Init(AcCtrl_GetPartsRblade(acp), 0, acp);
    for (int i = 0; i < acp->param.n_weapon; i++) ac_weaponctrl_init(acp, i);
    AcLgun_Init(AcCtrl_GetPartsLgun(acp), acp->param.swordp, acp);
    if (acp->param.swordp && acp->param.swordp->type == 3)
        AcAsm_AddMagazine(AcLgun_GetWeaponCtrl(AcCtrl_GetPartsLgun(acp))->wp, acp->param.shell_addrate, acp->param.energy_addrate);
    int start = 0;
    for (int i = 0; i < acp->param.n_weapon; i++) {
        if (AcUidev_IsAnalog(acp->frdp->uidevp) && acp->sepins_weappn != -1 && acp->param.weapons[i].statid == 3) continue;
        if (acp->param.weapons[i].statid == 5) continue;
        start = i;
        break;
    }
    acp->next_weapon = (unsigned char)start;
    acp->equip_weapon = (unsigned char)start;
    acp->reserve_weapon = 0;
    acp->wep_release = 1;
    if (acp->param.n_weapon == 0) return;
    _ac_weaponctrl *w0 = &acp->weaponctrl[0];
    _weapon_st *wp0;
    if (acp->param.armsp->type == 1 && (wp0 = w0->wp) != 0) {
        acwep_start_jcon(acp, wp0->jcon_progno, w0);
        int eq = wp0->equip_anmid;
        if (w0->wp->statid <= 6) {
            if (acwep_anim_ok(w0->mp[0], eq)) Anim_ChangeAnim(w0->mp[0], eq, 0xa, 0);
            if (acwep_anim_ok(w0->mp[1], eq)) Anim_ChangeAnim(w0->mp[1], eq, 0xa, 0);
        }
    }
    _ac_weaponctrl *w;
    if (AcUidev_IsAnalog(acp->frdp->uidevp)) {
        if (acp->sepins_weappn == -1) return;
        w = &acp->weaponctrl[(int)acp->sepins_weappn];
    } else {
        w = &acp->weaponctrl[acp->equip_weapon];
    }
    if (w->onequipfunc) gcall(w->onequipfunc, acp, w);
}
