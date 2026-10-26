/* src/ac/acptsdstry.cpp - per-AC "parts destroy": hit points of the destructible attachments (right hand gun / blade,
 * left hand blade / shield / laser gun, the back weapons, the extension) live in the sub-modules' AcDptsSt
 * {hp, maxhp, price, cnt}. A piercing hit (SfxDstat.pierce > 0) picks the attachment from the hit direction in the
 * AC's frame (or from the guard: shield / extension), takes sqrt(consrate * P^2 - P * hp) / leg-type defence off
 * its hp, and removes the part with a voice and spark effects when the hp reaches 0. */
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"
#include "acnb/sdk.h"

struct AcDptsSt *AcPtsDstry_GetExtentionDestroyInfo(struct AcPlayerSt *acp)
{
    PS2_FRAME(0x10);
    if (acp->param.extp) {
        AcPartsExtSt *e = AcCtrl_GetPartsExt(acp);
        if (e) return acext_GetDestroyInfo(e);
    }
    return 0;
}

struct AcDptsSt *AcPtsDstry_GetLeftBackDestroyInfo(struct AcPlayerSt *acp)
{
    PS2_FRAME(0x10);
    return acp->param.Lbackp ? acbkwep_GetDestroyInfo(acp, 0) : 0;
}

struct AcDptsSt *AcPtsDstry_GetRightBackDestroyInfo(struct AcPlayerSt *acp)
{
    PS2_FRAME(0x10);
    return acp->param.Rbackp ? acbkwep_GetDestroyInfo(acp, 1) : 0;
}

struct AcDptsSt *AcPtsDstry_GetLeftHandDestroyInfo(struct AcPlayerSt *acp)
{
    PS2_FRAME(0x10);
    if (!acp->param.swordp) return 0;
    switch (acp->param.swordp->type) {
    case 2: return acshield_GetDestroyInfo(acp);
    case 3: return AcLgun_GetDestroyInfo(AcCtrl_GetPartsLgun(acp));
    case 1: case 0: return acblade_GetDestroyInfo(AcCtrl_GetPartsBlade(acp));
    default: return 0;
    }
}

struct AcDptsSt *AcPtsDstry_GetRightHandDestroyInfo(struct AcPlayerSt *acp)
{
    PS2_FRAME(0x10);
    if (!acp->param.gunp) return 0;
    switch (acp->param.gunp->type) {
    case 3: return acrb_GetDestroyInfo(AcCtrl_GetPartsRblade(acp));
    case 2: case 1: case 0: return acrgun_GetDestroyInfo(acp);
    default: return 0;
    }
}

int AcPtsDstry_HitAttack(struct AcPartsDestroySt *ptsdstryp, struct SfxDstat *dstp, float *hitang, float *hitpos, int guard)
{
    PS2_FRAME(0x10);
    if (ptsdstryp->prvp->acp->spflag & 1) return 0;
    return PartsDestroy(ptsdstryp, dstp, hitang, hitpos, guard);
}

int AcPtsDstry_Init(struct AcPartsDestroySt *ptsdstryp, struct AcPlayerSt *acp)
{
    PS2_FRAME(0x40);
    if (!ptsdstryp) return 0;
    AcPartsDestroyPrivateSt *prv = ptsdstryp->prvp;
    ZeroMemory8b(ptsdstryp, 4);
    ZeroMemory8b(prv, 4);
    ptsdstryp->prvp = prv;
    ptsdstryp->prvp->acp = acp;
    InitPartsDestroy(ptsdstryp);
    return 1;
}

/* region of the hit seen from the AC (n = hit direction in the AC's frame): 0 front, 1 / 2 front left / right,
 * 3 / 4 left / right, 5 rear, 6 / 7 rear left / right; 8 = below 0.4 * tall (no part) */
static int PartsDestroy(struct AcPartsDestroySt *ptsdstryp, struct SfxDstat *dstp, float *hitang, float *hitpos, int guard)
{
    PS2_FRAME(0x110);
    PS2_STACK(float[4][4], m, 0x90);
    PS2_STACK(float[4], dn, 0xd0);
    PS2_STACK(float[4], n, 0xe0);
    PS2_STACK(float[4], d, 0xf0);
    PS2_STACK(int[2], slot, 0x108);   /* ocmats[] anchors of the spark effects */
    AcPlayerSt *acp = ptsdstryp->prvp->acp;
    AcDptsSt *info = 0;
    int kind = 0;
    slot[0] = -1;
    slot[1] = -1;
    if (dstp->pierce == 0) return 0;
    int region = 8;
    if (guard == 0) {
        sceVu0RotMatrix(m, IdentityMtx, acp->ang);
        sceVu0InversMatrix(m, m);
        vu_normalize(n, hitang);
        vu_apply(n, (const float (*)[4])m, n);
        d[0] = hitpos[0] - acp->loc[0];
        d[1] = hitpos[1] - acp->loc[1];
        d[2] = hitpos[2] - acp->loc[2];
        d[3] = hitpos[3];   /* vsub.xyz keeps the loaded hitpos.w */
        if (!(-d[1] < 0.4f * acp->tall)) {
            vu_normalize(dn, d);   /* never read */
            float nz = n[2], nx = n[0];
            if (nz <= 0.1736f && !(nz < -0.1736f)) {
                region = nx < 0.0f ? 3 : 4;
            } else if (!(nz < 0.0f)) {
                region = 0;
                if (nz < 0.9848f) region = nx < 0.0f ? 1 : 2;
            } else {
                region = 5;
                if (!(nz <= -0.9848f)) region = nx < 0.0f ? 6 : 7;
            }
        }
    }
    if (guard == 2) {   /* extension guard: one spark only (region 3 / 4 below sets two) */
        info = AcPtsDstry_GetExtentionDestroyInfo(acp);
        kind = 8;
        slot[0] = 3;
    } else if (guard == 1) {
        info = acshield_GetDestroyInfo(acp);
        kind = 2;
        slot[0] = 4;
    } else if (guard == 0) {
        switch (region) {
        case 1: info = AcPtsDstry_GetLeftHandDestroyInfo(acp); kind = 2; slot[0] = 4; break;
        case 2: info = AcPtsDstry_GetRightHandDestroyInfo(acp); kind = 1; slot[0] = 5; break;
        case 3: case 4:
            info = AcPtsDstry_GetExtentionDestroyInfo(acp);
            kind = 8;
            slot[0] = 3;
            slot[1] = 2;
            break;
        case 6: info = AcPtsDstry_GetLeftBackDestroyInfo(acp); slot[0] = 0; kind = 5; break;
        case 7: info = AcPtsDstry_GetRightBackDestroyInfo(acp); kind = 4; slot[0] = 1; break;
        default: break;
        }
    }
    if (!info || slot[0] == -1) return 0;
    if (info->hp <= 0) return 1;
    short hp = (short)info->hp;
    float legmul = 1.0f;
    switch (acp->param.prog_no) {
    case 3: legmul = g_asmparam.wepdst_def_leg_float; break;
    case 1: legmul = g_asmparam.wepdst_def_leg_tank; break;
    case 2: legmul = g_asmparam.wepdst_def_leg_4; break;
    case 0:
        switch (acp->param.legsp->basic.category_id) {
        case 5: legmul = g_asmparam.wepdst_def_leg_2v; break;
        case 9: case 1: legmul = g_asmparam.wepdst_def_leg_2m; break;
        case 7: case 3: legmul = g_asmparam.wepdst_def_leg_2l; break;
        default: break;
        }
        break;
    default: break;
    }
    float P = (float)dstp->pierce * 10.0f;
    float h = (float)hp;
    float acc = g_asmparam.wepdst_consrate * (P * P);   /* mula.s */
    float v = acc - P * h;                              /* msub.s: P * h rounded, then subtracted */
    int dmg = ps2_ftoi(ps2_div(ps2_sqrt(v), legmul));
    short nhp = (short)(hp - (short)dmg);
    if (nhp < 0) nhp = 0;
    int store = 1;
    if (nhp <= 0) {   /* destroyed: remove the part; a refused removal keeps the old hp */
        int removed = 0;
        switch (kind) {
        case 2:
            removed = AcWep_RemoveLeftHandParts(acp, 1);
            if (!removed) store = 0;
            break;
        case 1: case 4: case 5: case 6: {
            _ac_weaponctrl *wc = 0;
            for (int i = 0; i < 4; i++) {
                if (!acp->weaponctrl[i].wp) continue;
                if (acp->weaponctrl[i].wp->jpos == kind ||
                    ((kind == 4 || kind == 5) && acp->weaponctrl[i].wp->jpos == 6)) {
                    wc = &acp->weaponctrl[i];
                    break;
                }
            }
            if (wc) {
                removed = AcWep_RemoveWeapon(acp, wc, 1);
                if (!removed) store = 0;
            }
            break;
        }
        case 8: {
            AcPartsExtSt *e = AcCtrl_GetPartsExt(acp);
            if (e) {
                acext_Close(e);
                removed = acext_Remove(e, 1);
                if (!removed) store = 0;
            }
            break;
        }
        default: break;
        }
        if (removed) {
            Voice_CallPoint(0x51, 0x7f, &acp->loc, 2500.0f);
            for (int i = 0; i < 2; i++) {
                int k = slot[i];
                if (k != -1) DF_MAGIC2_AppearMagicOCMP(0x83, &acp->ocmats[k], 4);
            }
        }
    }
    if (store) info->hp = nhp;
    return 1;
}

/* fills one attachment's AcDptsSt from its part parameters; cnt gets the struct's own (PS2) address */
static inline void ptsdstry_init_info(AcDptsSt *info, PartsParamBasicSt *basic)
{
    info->hp = basic->hp;
    info->maxhp = info->hp;
    info->price = basic->price;
    info->cnt = (int)PS2PTR(info);
}

static void InitPartsDestroy(struct AcPartsDestroySt *ptsdstryp)
{
    PS2_FRAME(0x20);
    AcPlayerSt *acp = ptsdstryp->prvp->acp;
    AcDptsSt *info;
    info = AcPtsDstry_GetRightHandDestroyInfo(acp);
    if (info) ptsdstry_init_info(info, &acp->param.gunp->basic);
    info = AcPtsDstry_GetLeftHandDestroyInfo(acp);
    if (info) ptsdstry_init_info(info, &acp->param.swordp->basic);
    info = AcPtsDstry_GetRightBackDestroyInfo(acp);
    if (info) ptsdstry_init_info(info, &acp->param.Rbackp->basic);
    info = AcPtsDstry_GetLeftBackDestroyInfo(acp);
    if (info) ptsdstry_init_info(info, &acp->param.Lbackp->basic);
    info = AcPtsDstry_GetExtentionDestroyInfo(acp);
    if (info) ptsdstry_init_info(info, &acp->param.extp->basic);
}
