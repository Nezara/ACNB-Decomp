/* src/ac/acsfx.cpp - per-AC effects driven by AcPlayerSt.effectstat each frame: boost flames (back / leg / extension /
 * OB sub boosters) with the hover light, camera shake and the hover dust / spray on the ground or water below, blade
 * trails and shield, foot splashes / dust and sparks, the shield-guard flash, lock-jam / flare / generator-down spells
 * and smoke from badly damaged parts. Talks to the effect manager (aceff.c) and the AC sounds (acse.c). */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"

#ifdef ACNB_SOURCEPORT
unsigned short s_MapMagicTableDefault[18] = {0x000, 0x17c, 0x17a, 0x18f, 0x17a, 0x17a, 0x17a, 0x18b, 0x18d,
                                             0x18c, 0x18e, 0x181, 0x184, 0x187, 0x187, 0x187, 0x180, 0x17d};
struct_ac5e2447 AC_ShakeParam = {1.375f, 76000};   /* addz 0x128e0 */
unsigned short s_MapMagicTable[18];
#endif

/* map attribute effect slots -> magic ids (the leg program can swap some, InitAcSfx) */
void acsfx_ChangeMagicTable(int index, int magicid)
{
    if (index >= 0 && index < 0x12) s_MapMagicTable[index] = (unsigned short)magicid;
}

void acsfx_InitMapMagicTable(void) { CopyMemory(s_MapMagicTable, s_MapMagicTableDefault, 0x24); }

/* the map effect of slot e appears this frame: every putframe-th frame, staggered by index (R5900 rem: no trap
 * on putframe 0) */
static inline int acsfx_putframe_due(int index, short pf)
{
    return ps2_mod32(index, pf) == (int)ps2_modu32(FRAME, (unsigned int)(int)pf);
}

/* smoke from a part below 20% of its AP, every 20th frame */
static inline int acsfx_smoke_due(AcDptsSt *di)
{
    di->cnt++;
    return ps2_mod32(di->cnt, 20) == 0 && ps2_div((float)di->hp, (float)di->maxhp) <= 0.2f;
}

void acsfx_Control(AcPlayerSt *acp, unsigned int effstat, unsigned int effstatold)
{
    PS2_FRAME(0xb0);
    AcSfxSt *sfxp = AcCtrl_GetSfx(acp);
    if (FRAME == 0) return;
    if (sfxp->cntFootStampWait > 0) sfxp->cntFootStampWait--;
    if ((effstat & ~effstatold) & 0x400) {   /* big splash when hitting water fast */
        float x = acp->velocity[0] * acp->velocity[0], y = acp->velocity[1] * acp->velocity[1];
        if (!(ps2_sqrt((x + y) + acp->velocity[2] * acp->velocity[2]) <= 0.1f)) ac_put_largesplash(acp);
    }
    if (!(effstatold & 1) && (effstat & 1)) acse_PutSeVolDist(acp, 2, AC_SEparam.jet_vol, (float)AC_SEparam.jet_dist);
    ac_draw_hover_fire(acp, effstat);
    AcPartsExtSt *ext = AcCtrl_GetPartsExt(acp);
    AcEffSt *effp = AcCtrl_GetEffect(acp);
    switch (ext->prog_no) {
    case 0: case 1: case 7: case 8:   /* extension boosters */
        for (int i = 0; i < ext->nmuzzle; i++) aceff_ControlBoost(effp, 8 + i, effstat & 0x40);
        break;
    case 5:
        ac_draw_turnboost_fire(acp, effstat & 0x40);
        break;
    case 9: case 10:   /* extension shields */
        for (int i = 0; i < ext->nmuzzle; i++) aceff_ControlShield(effp, 1 + i, effstat & 0x40);
        break;
    }
    ac_draw_OBsubboost_fire(acp, effstat & 0x20);
    /* footsteps: landing (both feet, not again within 30 frames), left / right step, the 4-leg / hover kinds */
    if (effstat & 0x200) {
        if (sfxp->cntFootStampWait == 0) {
            ac_draw_footsfx(acp, 0, 6);
            ac_draw_footsfx(acp, 1, 6);
        }
        sfxp->cntFootStampWait = 0x1e;
    } else if (effstat & 0x80) ac_draw_footsfx(acp, 0, 5);
    else if (effstat & 0x100) ac_draw_footsfx(acp, 1, 5);
    else if (effstat & 0x3000) {
        int lv = (effstat & 0x1000) ? 3 : 4;
        if (acp->param.prog_no != 3) {
            ac_draw_footsfx(acp, 0, lv);
            ac_draw_footsfx(acp, 1, lv);
            if (acp->param.prog_no != 0) {
                ac_draw_footsfx(acp, 2, lv);
                ac_draw_footsfx(acp, 3, lv);
            }
        }
    }
    if ((effstat & 4) && acp->mp->yukaattribute == 0 && !(acp->loc[1] <= acp->floor_high - 0.1f)) {
        ac_draw_footspark(acp, &acp->ocmats[9]);
        ac_draw_footspark(acp, &acp->ocmats[10]);
    }
    ac_draw_blade(acp, effstat);
    acrb_Draw(AcCtrl_GetPartsRblade(acp));
    if (--sfxp->cntGuardWait < 0) sfxp->cntGuardWait = 0;
    if (effstatold & 0x800) {   /* shield guarded a hit last frame */
        sfxp = AcCtrl_GetSfx(acp);
        struct_d6b7e912 *sh = aceff_GetShield(AcCtrl_GetEffect(acp), 0);
        if (sfxp->cntGuardWait <= 0) {
            if (acp->param.shield_magcp)
                DF_MAGIC2_AppearMagicPtr(acp->param.shield_magcp, acp->param.shield_combp, 0, 0, 0, 0, &sh->mat, 0, 4);
            sfxp->cntGuardWait = (short)(g_sortie.pconstant->isSplit ? AC_TempParam_I.spread_guardsfxwait
                                                                     : AC_TempParam_I.normal_guardsfxwait);
        }
    }
    /* lock jammed / flare / generator down / AP low or lock-jam spell: status effects on the AC */
    if (acp->nolock_time > 0) DF_SPELL2_AppearSpell(0x103, 0, 0, 0, 0, 0, 0, 0, 0, acp->mp, 0, 0);
    if (acp->flare_time > 0) DF_SPELL2_AppearSpell(0x102, 0, 0, 0, 0, 0, 0, 0, 0, acp->mp, 0, 0);
    if (acp->genedown_time > 0) DF_SPELL2_AppearSpell(0x100, 0, 0, 0, 0, 0, 0, 0, 0, acp->mp, 0, 0);
    if (((acp->stat & 8) || (effstat & 0x10000)) && acp->dead == 0)
        DF_SPELL2_AppearSpell(0x101, 0, 0, 0, 0, 0, 0, 0, 0, acp->mp, 0, 0);
    if (acp->dead == 0) {
        AcDptsSt *di;
        if ((di = AcPtsDstry_GetRightHandDestroyInfo(acp)) && acsfx_smoke_due(di))
            DF_MAGIC2_AppearMagicOCMP(0x171, &acp->ocmats[5], 4);
        if ((di = AcPtsDstry_GetLeftHandDestroyInfo(acp)) && acsfx_smoke_due(di))
            DF_MAGIC2_AppearMagicOCMP(0x171, &acp->ocmats[4], 4);
        if ((di = AcPtsDstry_GetRightBackDestroyInfo(acp)) && acsfx_smoke_due(di))
            DF_MAGIC2_AppearMagicOCMP(0x171, &acp->ocmats[1], 4);
        if ((di = AcPtsDstry_GetLeftBackDestroyInfo(acp)) && acsfx_smoke_due(di))
            DF_MAGIC2_AppearMagicOCMP(0x171, &acp->ocmats[0], 4);
        if ((di = AcPtsDstry_GetExtentionDestroyInfo(acp)) && acsfx_smoke_due(di)) {
            DF_MAGIC2_AppearMagicOCMP(0x171, &acp->ocmats[3], 4);
            DF_MAGIC2_AppearMagicOCMP(0x171, &acp->ocmats[2], 4);
        }
    }
    AcBlade_UpdateBladeTracer(acp);
}

/* left-arm blade / shield (Larm_type 0 / 2): a double blade (abl_Wblade) swings blade 0, then blade 1 */
static void ac_draw_blade(AcPlayerSt *acp, unsigned int effstat)
{
    PS2_FRAME(0x40);
    AcEffSt *effp = AcCtrl_GetEffect(acp);
    switch (acp->param.Larm_type) {
    case 2:
        aceff_ControlShield(effp, 0, acp->shieldstat == 1);
        break;
    case 0:
        if (acp->param.abl_Wblade) {
            struct_3956eba2 *b = aceff_GetBlade(effp, 0);
            if (effstat & 8) {
                aceff_ControlBlade(effp, 0, 1);
                aceff_ControlBlade(effp, 1, 0);
            } else if (effstat & 0x10) {
                aceff_ControlBlade(effp, 0, b->frame > 0);
                aceff_ControlBlade(effp, 1, 1);
            } else {
                aceff_ControlBlade(effp, 0, 0);
                aceff_ControlBlade(effp, 1, 0);
            }
            b->frame--;
        } else aceff_ControlBlade(effp, 0, effstat & 8);
        break;
    }
}

/* sparks from a foot sliding on the ground, thrown back along the move (or against the velocity when nearly still) */
static void ac_draw_footspark(AcPlayerSt *acp, ObjCMatSt *ocmatp)
{
    PS2_FRAME(0xa0);
    if (!ocmatp->parentmatp) return;
    PS2_STACK(float[4], vec, 0x10);
    PS2_STACK(float[4][4], prm, 0x20);   /* pos, ang, scl, col (scl / col filled but not passed) */
    vu_apply(prm[0], *ocmatp->parentmatp, ocmatp->mat[3]);
    vec[0] = acp->oloc[0] - acp->loc[0];
    vec[1] = acp->oloc[1] - acp->loc[1];
    vec[2] = acp->oloc[2] - acp->loc[2];
    vec[3] = acp->oloc[3];
    float xx = vec[0] * vec[0], zz = vec[2] * vec[2];
    if (ps2_sqrt(xx + zz) < 0.3f) {
        float vx = acp->velocity[0], vz = acp->velocity[2];
        if (ps2_sqrt(vx * vx + vz * vz) < 0.2f) return;
        vec[0] = -vx;
        vec[2] = -acp->velocity[2];
    }
    prm[1][0] = 0.0f;
    prm[1][1] = ps2_atan2f(vec[0], vec[2]);
    for (int k = 0; k < 4; k++) prm[2][k] = 1.0f;
    for (int k = 0; k < 4; k++) prm[3][k] = 128.0f;
    prm[1][3] = 0.0f;
    prm[1][2] = 0.0f;
    DF_MAGIC2_AppearMagic(0x17c, prm[0], prm[1], 0, 0, 0, 0, 0x400);
}

/* Footstep dust / splash of the map attribute under foot `legid`. In water deeper than the waist's half the effect
 * sits on the surface, pulled toward the waist the deeper the foot (not for leg program 0). */
static void ac_draw_footsfx(AcPlayerSt *acp, int legid, int efflv)
{
    PS2_FRAME(0xd0);
    ObjCMatSt *ocm = &acp->ocmats[9 + legid];
    if (!ocm->parentmatp) return;
    PS2_STACK(float[4], waist, 0x30);
    PS2_STACK(float[4], foot, 0x40);
    PS2_STACK(float[4][4], prm, 0x50);   /* pos (w never written: stale stack copied into the effect), ang, scl, col */
    vu_apply(foot, *ocm->parentmatp, ocm->mat[3]);
    unsigned char atr;
    if (!(acp->floor_high <= acp->water_high) && !(acp->water_high <= acp->loc[1] - 0.5f * acp->tall)) {
        memcpy(waist, (*acp->op[14].wrmatp)[3], 16);
        float h = acp->floor_high - waist[1];
        float s;
        if (!(h <= 0.0f) && acp->param.prog_no != 0) {
            s = 1.0f - 0.5f * ((acp->floor_high - acp->water_high) / h);
            if (s < 0.1f) s = 0.1f;
        } else s = 1.0f;
        prm[0][0] = waist[0] + s * (foot[0] - waist[0]);
        prm[0][1] = acp->water_high;
        prm[0][2] = waist[2] + s * (foot[2] - waist[2]);
        atr = acp->mp->mizuattribute;
    } else {
        prm[0][0] = foot[0];
        prm[0][1] = acp->floor_high;
        prm[0][2] = foot[2];
        atr = acp->mp->yukaattribute;
    }
    MapAttributeInfoSt *info = mapatrb_GetInfo(atr);
    if (!info) return;
    struct_30054149 *e = &info->sfxinfo[efflv];
    short pf = g_sortie.pconstant->isSplit ? e->vsputframe : e->p1putframe;
    if (!acsfx_putframe_due(legid, pf)) return;
    if (e->magic == 0) return;
    for (int k = 0; k < 3; k++) prm[1][k] = 0.0f;
    prm[1][3] = 1.0f;
    for (int k = 0; k < 4; k++) prm[2][k] = 1.0f;
    for (int k = 0; k < 4; k++) prm[3][k] = 128.0f;
    DF_MAGIC2_AppearMagic(s_MapMagicTable[e->magic], prm[0], prm[1], 0, 0, 0, 0, 0);
}

/* the overed-boost core's option boosters (blades 2..) and the OB / subleg sub boosters 6 / 7 */
static void ac_draw_OBsubboost_fire(AcPlayerSt *acp, int burning)
{
    PS2_FRAME(0x60);
    AcSfxSt *sfxp = AcCtrl_GetSfx(acp);
    AcEffSt *effp = AcCtrl_GetEffect(acp);
    int n = 0;
    switch (acp->param.core_type) {
    case 3: AcCtrl_GetPartsSlow(acp); break;
    case 2: AcCtrl_GetPartsSlow(acp); break;
    case 1: n = AcCtrl_GetPartsOb(acp)->obnum; break;
    }
    for (int i = 0; i < n; i++) aceff_ControlBlade(effp, 2 + i, burning);
    if (sfxp->nobsubboost) {
        aceff_ControlBoost(effp, 6, burning);
        if (sfxp->nobsubboost >= 2) aceff_ControlBoost(effp, 7, burning);
    }
}

/* extension turn boosters: the diagonal pair for the turning direction */
static void ac_draw_turnboost_fire(AcPlayerSt *acp, int burning)
{
    PS2_FRAME(0x40);
    AcPartsExtSt *ext = AcCtrl_GetPartsExt(acp);
    AcEffSt *effp = AcCtrl_GetEffect(acp);
    if (ext->nmuzzle < 4) return;
    if (ext->isTurnLr == 1) {
        aceff_ControlBoost(effp, 8, burning);
        aceff_ControlBoost(effp, 9, 0);
        aceff_ControlBoost(effp, 10, 0);
        aceff_ControlBoost(effp, 11, burning);
    } else if (ext->isTurnLr == 0) {
        aceff_ControlBoost(effp, 8, 0);
        aceff_ControlBoost(effp, 9, burning);
        aceff_ControlBoost(effp, 10, burning);
        aceff_ControlBoost(effp, 11, 0);
    }
}

/* Point light at the centre of the burning boosters and the camera shake near the back boosters. A strong leg flame
 * takes over the light's colour / scale (the back boosters still place it); a weak one restarts with the legs only. */
static void ac_put_hover_light(AcPlayerSt *acp)
{
    PS2_FRAME(0xd0);
    PS2_STACK(float[4], vec, 0xa0);
    PS2_STACK(float[4], pos, 0xb0);        /* handed to DF_SHAKE_SetPos */
    PS2_STACK(float[4], lightpos, 0xc0);
    AcSfxSt *sfxp = AcCtrl_GetSfx(acp);
    AcEffSt *effp = AcCtrl_GetEffect(acp);
    const float tiny = ps2_u2f(0x38d1b717u);   /* ~1e-4 */
    lightpos[0] = 0.0f;
    lightpos[1] = 0.0f;
    lightpos[2] = 0.0f;
    lightpos[3] = 1.0f;
    float scale = 0.0f;
    int nboost = 0;
    float (*colorp)[4] = 0;
    auto sum = [&](const _aceff_boostmng *b, int n) {
        for (int j = 0; j < n; j++, nboost++)
            for (int k = 0; k < 3; k++) lightpos[k] = lightpos[k] + b[j].mat[3][k];
    };
    if (sfxp->nbackboost) {
        _aceff_boostmng *b = aceff_GetBoost(effp, 0);
        scale = b->alpha / 128.0f;
        colorp = b->colorp;
        sum(b, sfxp->nbackboost);
    }
    if (!(scale <= tiny)) {
        float f = ps2_div(1.0f, (float)nboost);
        memcpy(pos, lightpos, 16);
        for (int k = 0; k < 3; k++) pos[k] = pos[k] * f;
        pos[3] = 1.0f;
        const float *z = acp->mp->matrix2[2];
        for (int k = 0; k < 3; k++) vec[k] = z[k] * AC_ShakeParam.ofsz;
        vec[3] = z[3];
        for (int k = 0; k < 3; k++) pos[k] = pos[k] + vec[k];
        DF_SHAKE_SetPos(sfxp->h_shake[0], pos);
        DF_SHAKE_SetSW(sfxp->h_shake[0], 1);
        DF_SHAKE_ADDz(sfxp->h_shake[0], AC_ShakeParam.addz);
    } else DF_SHAKE_SetSW(sfxp->h_shake[0], 0);
    if (sfxp->nlegboost) {
        _aceff_boostmng *b = aceff_GetBoost(effp, 2);
        colorp = b->colorp;
        scale = b->alpha / 128.0f;
        if (scale < tiny) {
            lightpos[0] = 0.0f;
            lightpos[1] = 0.0f;
            lightpos[2] = 0.0f;
            lightpos[3] = 1.0f;
            nboost = 0;
            scale = (b->alpha / 128.0f) / 2.0f;
            if (sfxp->nbackboost == 0) scale = scale * 2.0f;
        }
        sum(b, sfxp->nlegboost);
    }
    if (!colorp || !nboost) return;
    if (scale <= 0.001f) return;
    float f = 1.0f / (float)nboost;
    for (int k = 0; k < 3; k++) lightpos[k] = lightpos[k] * f;
    scale = scale * 6.0f;
    if (!(scale <= 3.0f)) scale = 3.0f;
    DF_PutPointLight(2, 0, lightpos, scale, 1, (*colorp)[0], (*colorp)[1], (*colorp)[2]);
}

/* Hover dust / spray of the map attribute under each burning booster within 30 * boost power above the ground,
 * stronger the closer. Returns the last scale computed. (maxheight is unused.) */
static float ac_draw_hover_mapsfx(AcPlayerSt *acp, int atr, int nboost, float (**matptbl)[4][4], int effectlv,
                                  float hoverscale, float gndpos, float maxheight)
{
    PS2_FRAME(0xe0);
    static const float ang0[4] __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 1.0f};   /* 0x25aba0, no DWARF name */
    float ang[4] __attribute__((aligned(16))), loc[4] __attribute__((aligned(16))), bp[4] __attribute__((aligned(16)));
    memcpy(ang, ang0, 16);
    if (acp->dead) return 0.0f;
    MapAttributeInfoSt *info = mapatrb_GetInfo((unsigned char)atr);
    if (!info) return 0.0f;
    struct_30054149 *e = &info->sfxinfo[effectlv];
    if (e->magic == 0) return 0.0f;
    float maxh = 30.0f * acp->param.boost_powerrate;
    if (maxh < 0.0f) return 0.0f;
    loc[1] = gndpos;
    loc[3] = 1.0f;
    float scale = 0.0f;
    StSortieConstant *sc = g_sortie.pconstant;
    short pf;
    if (sc->isSplit) pf = e->vsputframe;
    else if (sc->modeSortie == 0 || sc->modeSortie == 1) pf = e->p1putframe;
    else if (NowPlayerNum < 4) pf = e->p1putframe;
    else pf = e->vsputframe;
    for (int i = 0; i < nboost; i++) {
        memcpy(bp, (*matptbl[i])[3], 16);
        float h = gndpos - bp[1];
        if (h < -1.0f) continue;
        if (maxh < h) continue;
        if (h <= 0.0f) scale = 1.0f;
        else scale = (maxh - h) / maxh;
        scale = scale * hoverscale;
        if (!acsfx_putframe_due(i, pf)) continue;
        loc[0] = bp[0];
        loc[2] = bp[2];
        DF_MAGIC2_AppearMagic(s_MapMagicTable[e->magic], loc, ang, 0, 0, 0, 0, 0);
    }
    return scale;
}

/* Boost flames (leg, back, shoulder), the hover light / sound, and the hover map effect under the burning boosters
 * (back boosters while jetting, else the leg boosters; the OB boosters while the overed boost runs). */
static void ac_draw_hover_fire(AcPlayerSt *acp, unsigned int effstat)
{
    PS2_FRAME(0x160);
    PS2_STACK(float[2][4][4], oboomat, 0xe0);
    AcPartsObSt *obp = AcCtrl_GetPartsOb(acp);
    AcEffSt *effp = acp->frdp->effp;
    AcSfxSt *sfxp = acp->frdp->sfxp;
    _aceff_boostmng *lbmngp = aceff_GetBoost(effp, 2), *bbmngp = aceff_GetBoost(effp, 0);
    float (*matptbl[8])[4][4];
    int nsfxboost = 0;
    for (int i = 0; i < sfxp->nlegboost; i += 2) {
        aceff_ControlBoost(effp, 2 + i, effstat & 2);
        if (effstat & 0x4002) matptbl[nsfxboost++] = &lbmngp[i].mat;
        if (i + 1 < sfxp->nlegboost) {
            aceff_ControlBoost(effp, 3 + i, effstat & 2);
            if (effstat & 0x4002) matptbl[nsfxboost++] = &lbmngp[i + 1].mat;
        }
    }
    if (sfxp->nbackboost) {
        int ok = 1;
        if (!aceff_ControlBoost(effp, 0, effstat & 1)) ok = 0;
        if (!aceff_ControlBoost(effp, 1, effstat & 1)) ok = 0;
        if (effstat & 1) {   /* jetting: the back boosters replace the leg entries */
            nsfxboost = 0;
            if (ok) {
                matptbl[0] = &bbmngp[0].mat;
                matptbl[1] = &bbmngp[1].mat;
            } else {
                matptbl[0] = acp->op[0].wrmatp;
                matptbl[1] = acp->op[0].wrmatp;
            }
            nsfxboost += 2;
        }
        if (acp->param.abl_shbooster) {
            aceff_ControlBoost(effp, 0xc, effstat & 1);
            aceff_ControlBoost(effp, 0xd, effstat & 1);
        }
    }
    ac_put_hover_light(acp);
    float hoverscale = 0.0f;
    if (sfxp->nbackboost) hoverscale = bbmngp->alpha / 128.0f;
    if (sfxp->nlegboost && hoverscale < ps2_u2f(0x38d1b717u)) {
        hoverscale = (lbmngp->alpha / 128.0f) / 2.0f;
        if (sfxp->nbackboost == 0) hoverscale = hoverscale * 2.0f;
    }
    if ((effstat & 0x4000) && hoverscale < AC_TempParam.smallhover_scale) hoverscale = AC_TempParam.smallhover_scale;
    acse_SetLoopSeParamVolDist(acp, 1, ps2_ftoi(hoverscale * (float)AC_SEparam.hover_vol), (float)AC_SEparam.hover_dist);
    int pmin = AC_SEparam.hover_minpitch;
    acse_SetLoopSeParamPitch(acp, 1, pmin + ps2_ftoi(hoverscale * (float)(AC_SEparam.hover_maxpitch - pmin)));
    if (obp && obp->stat == 3) {   /* overed boost running */
        for (int j = 0; j < 2; j++) {
            ObjCMatSt *ocm = &acp->ocmats[20 + j];
            if (ocm->parentmatp) vu_mul_matrix(oboomat[j], *ocm->parentmatp, ocm->mat);
            matptbl[j] = &oboomat[j];   /* also without a matrix (stale stack) */
        }
        nsfxboost = 2;
        hoverscale = 1.0f;
    }
    if (acp->water_high < acp->floor_high)
        ac_draw_hover_mapsfx(acp, acp->mp->mizuattribute, nsfxboost, matptbl, acp->hover_effectlv, hoverscale,
                             acp->water_high, 30.0f);
    else
        ac_draw_hover_mapsfx(acp, acp->mp->yukaattribute, nsfxboost, matptbl, acp->hover_effectlv, hoverscale,
                             acp->floor_high, 20.0f);
}

/* landing in water: the attribute's splash and land effects and the splash sound */
static void ac_put_largesplash(AcPlayerSt *acp)
{
    PS2_FRAME(0x50);
    PS2_STACK(float[4], ang, 0x30);
    PS2_STACK(float[4], loc, 0x40);
    MapAttributeInfoSt *info = mapatrb_GetInfo(acp->mp->mizuattribute);
    unsigned short m1, m2;
    if (!info || !(m1 = info->sfxinfo[2].magic) || !(m2 = info->land_magic)) return;
    unsigned short id2 = s_MapMagicTable[m2];
    loc[0] = acp->loc[0];
    loc[1] = acp->water_high;
    loc[2] = acp->loc[2];
    loc[3] = 1.0f;
    ang[3] = 1.0f;
    ang[0] = 0.0f;
    ang[1] = 0.0f;
    ang[2] = 0.0f;
    DF_MAGIC2_AppearMagic(s_MapMagicTable[m1], loc, ang, 0, 0, 0, 0, 0);
    DF_MAGIC2_AppearMagic(id2, loc, ang, 0, 0, 0, 0, 0);
    acse_PutSeVolDist(acp, 0xbc7, AC_SEparam.water_land_vol, (float)AC_SEparam.water_land_dist);
}
