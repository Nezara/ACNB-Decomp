/* src/autogen/AutoGenLogic.cpp - COM (computer opponent) behaviour data for an auto-generated Arena opponent.
 * AutoGenLogic_CreateComLogic (called by LoopSortie_Begin at the Arena match start) maps the pilot class to a logic
 * class 0..2 (s_PilotClass) and fills a stComData (s_pComData): op-data templates from the ComOpData files, then
 * random numbers (alert levels, reaction delays, weapon shot timing, ranges) drawn with newlib rand().
 *
 * Every value is a rand() draw: the CALL ORDER must match the original (the native rand shares newlib's state), and
 * draws whose result is unused are kept. RANDN-style values are `(rand() % M) >> 14` with M an immediate; the
 * original's `(rand() % 0x4000) >> 14` (written as RANDN(1)) is always 0 but still consumes a draw. Double maths is
 * libgcc soft double (round toward zero) through the acnb/sdk.h wrappers, in the asm's operation order.
 *
 * All reached functions are native (incl. _LauGun, reached by ACArenaWin, and _ExMultiBooster, sbx_arena_class); the
 * three never-run helpers _LauEnergyShield / _LauShield / _ExSideShield stay interpreted and SetBasisParam calls them
 * through the bridge.
 * After the op-data / weapon data are made, CreateComTree also writes the "ComEdit" tree blob (the COM editor's
 * format: header, node list, op data, weapon data) into tree_buff, and the public CreateComLogic copies the whole
 * stComData into the menu overlay's ComData[0]. */
#include "acnb/port.h"
#include "acnb/nodwarf.h"
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"

/* (rand() % m) >> 14, MWCC's signed remainder (rand is never negative). */
static inline int agl_rnd(int m) { return ps2_mod32(ps2_rand(), m) >> 14; }

static const double AGL_0_01 = 0.01;   /* 0x3f847ae147ae147b */
static const double AGL_0_1 = 0.1;     /* 0x3fb999999999999a */

/* (int)((double)range * (0.1 * (double)x)) - __floatsidf, __muldf3(0.1, x), __extendsfdf2(range), __muldf3, __fixdfsi. */
static inline int agl_range_part(float range, int x)
{
    double a = ps2_dmul(AGL_0_1, ps2_i2d(x));
    return ps2_d2i(ps2_dmul(ps2_f2d(range), a));
}

/* (int)((double)u * (k * (double)x)) - __floatsidf(x), __muldf3(k, x), u16 -> double (0x100460), __muldf3, __fixdfsi. */
static inline int agl_scale_u16(double k, int x, unsigned short u)
{
    double a = ps2_dmul(k, ps2_i2d(x));
    return ps2_d2i(ps2_dmul(ps2_i2d(u), a));   /* u16: the unsigned wrapper's result is exact, = __floatsidf */
}

/* float -> unsigned as MWCC emits it: c.ole.s 2^31, cvt.w.s (minus 2^31, | 0x80000000 above). */
static inline unsigned int agl_f2u(float f)
{
    const float two31 = 2147483648.0f;   /* 0x4f000000 */
    if (two31 <= f) return (unsigned int)ps2_ftoi(f - two31) | 0x80000000u;
    return (unsigned int)ps2_ftoi(f);
}

/* Which weapon range class gets the raise value: a random range type allowed by the search flags (the menu's
 * g_flagSearch, else g_nextSearchFlag); no flag at all -> slot 0. */
static void __AutoGenLogic_CalcRaiseValue(void)
{
    PS2_FRAME(0x30);
    stAsmSearchFlag *pSearchFlag = &g_nextSearchFlag;
    int rangeType, trueFlag;
    ps2_memset(s_RaiseValue, 0, 5);
    do {
        trueFlag = 0;
        rangeType = agl_rnd(0xc000);
        signed char range = g_flagSearch.Range.Data;   /* 0x16df2c: OLMENU global */
        if (range == 0) {
            range = pSearchFlag->Range.Data;
            if (range == 0) {                          /* 0x16e028 (reached) */
                s_RaiseValue[0] = 0x1e;
                return;
            }
        }
        switch (rangeType) {                           /* bit 0 / 1 / 2 = Long / Middle / Short */
        case 0: if (range & 1) trueFlag = 1; break;
        case 1: if (range & 2) trueFlag = 1; break;
        case 2: if (range & 4) trueFlag = 1; break;
        }
    } while (!trueFlag);
    switch (rangeType) {                               /* 0x16e048 */
    case 0: s_RaiseValue[(int)pSearchFlag->MainWepLong] = 0x1e; break;
    case 1: s_RaiseValue[(int)pSearchFlag->MainWepMiddle] = 0x1e; break;
    case 2: s_RaiseValue[(int)pSearchFlag->MainWepShort] = 0x1e; break;
    }
}

/* Lock range of a weapon; with a left arm unit weapon the unified two-sight range when it is shorter.
 * Original quirk: the second sight is also read from pWep (pArmUnitLWep was surely meant). */
static float __AutoGenLogic_GetWepLockRange(AcPlayerParamSt *pParam, PartsParamWeaponSt *pWep,
                                            PartsParamWeaponSt *pArmUnitLWep)
{
    PS2_FRAME(0x6e0);
    PS2_STACK(WepSightSt, originalWepSight, 0x6b0);
    PS2_STACK(WepSightSt, armUnitLWepSight, 0x680);
    PS2_STACK(StWeaponLockManage, wepLockMng, 0x50);   /* its psight[] gets the two sights' addresses */
    AcAsm_GetWeaponSight(pParam, &originalWepSight, pWep);
    AcAsm_GetWeaponSight(pParam, &armUnitLWepSight, pWep);
    float originalRange = originalWepSight.range;
    if (!pArmUnitLWep) return originalRange;
    ps2_memcpy(&wepLockMng, &s_pAutoGenCom->lockmng, sizeof(StWeaponLockManage));
    wepLockMng.psight[0] = &originalWepSight;
    wepLockMng.psight[1] = &armUnitLWepSight;
    Lock_UnifyTwoSight(&wepLockMng);
    float unifyRange = wepLockMng.sightUni.range;
    if (unifyRange < originalRange) return unifyRange;
    return originalRange;
}

/* Blade (left arm unit) launcher parameters; the three classes store the same fields in the same order. */
static void __AutoGenLogic_SetBasisParam_LauBlade(stComLau *pLauData)
{
    PS2_FRAME(0x20);
    stComBlade *pBlade = (stComBlade *)pLauData;
    int m5, k5, m8, m10, k10, m13, m18, k18, m20, k20;
    switch (s_PilotClass) {
    case 2:   /* 0x16f550 */
        m5 = 0xa0000; k5 = 0x14; m8 = 0xf0000; m10 = 0x3c000; k10 = 5; m13 = 0xa0000;
        m18 = 0x1b8000; k18 = 0xa; m20 = 0x3c000; k20 = 5;
        break;
    case 1:   /* 0x16f438 */
        m5 = 0x50000; k5 = 0x14; m8 = 0x78000; m10 = 0x14000; k10 = 5; m13 = 0x50000;
        m18 = 0x118000; k18 = 0xa; m20 = 0x14000; k20 = 5;
        break;
    case 0:   /* 0x16f318 */
        m5 = 0x28000; k5 = 0x14; m8 = 0x28000; m10 = 0x14000; k10 = 2; m13 = 0x28000;
        m18 = 0x78000; k18 = 0x14; m20 = 0x14000; k20 = 2;
        break;
    default:
        return;
    }
    pBlade->progno = 0;
    pBlade->flag = 0;
    pBlade->bld_energy_min = 0x14;
    pBlade->bld_wait = agl_rnd(m5) + k5;
    pBlade->bld_ang_maxY = 0x19;
    pBlade->bld_dist_max = agl_rnd(m8) + 0x32;
    pBlade->bld_height_max = agl_rnd(m10) + k10;
    pBlade->wav_energy_min = 0x14;
    pBlade->wav_wait = agl_rnd(m13) + 0x14;
    pBlade->wav_ang_maxY = 0x19;
    pBlade->wav_dist_min = 0;
    pBlade->wav_dist_max = agl_rnd(m18) + k18;
    pBlade->wav_height_max = agl_rnd(m20) + k20;
}

/* Gun (left arm unit) launcher parameters: 8 draws in the same order for every class. (sic) the first draw is a
 * RANDN(1) sign test (always 0), so the negative dist_max arm never runs. */
static void __AutoGenLogic_SetBasisParam_LauGun(stComLau *pLauData)
{
    PS2_FRAME(0x30);
    stComLGun *p = (stComLGun *)pLauData;
    AcPtsResSt *pRes = AcCtrl_GetPartsRes(s_pAutoGenCom);
    PartsParamWeaponSt *w = AcPtsRes_GetWeaponPtr(pRes, 6, 0);
    int negM, negK, posM, posK, m3, k3, m4, k4, m6, m7, k7, m8, k8;
    switch (s_PilotClass) {
    case 2:   /* 0x16eda8 */
        negM = 0xa0000; negK = 0x14; posM = 0x50000; posK = 0; m3 = 0xc8000; k3 = 0x32; m4 = 0x28000; k4 = 5;
        m6 = 0xc8000; m7 = 0x28000; k7 = 0xa; m8 = 0x3c000; k8 = 0xa;
        break;
    case 1:   /* 0x16ebd8 */
        negM = 0x50000; negK = 0x14; posM = 0x50000; posK = 0x14; m3 = 0x50000; k3 = 0x50; m4 = 0x28000; k4 = 0;
        m6 = 0x64000; m7 = 0x1c000; k7 = 8; m8 = 0x28000; k8 = 8;
        break;
    case 0:   /* 0x16ea08; m8 0x20000: sign fix `addiu v1,v1,0`, dead */
        negM = 0x50000; negK = 0; posM = 0x50000; posK = 0; m3 = 0x14000; k3 = 0x5f; m4 = 0x14000; k4 = 0;
        m6 = 0x28000; m7 = 0x14000; k7 = 5; m8 = 0x20000; k8 = 5;
        break;
    default:
        return;
    }
    p->progno = 2;
    p->flag = 0;
    p->dist_min = 0;
    int v;
    if (agl_rnd(0x4000)) v = -(agl_rnd(negM) + negK);   /* RANDN(1): never */
    else v = agl_rnd(posM) + posK;
    p->dist_max = (unsigned short)agl_f2u(w->act.range + (float)v);
    p->shot_rate = agl_rnd(m3) + k3;
    p->shot_minLTime = agl_rnd(m4) + k4;
    p->shot_maxLTime = p->shot_minLTime + agl_rnd(0x14000);
    p->shot_maxSTime = 0;
    p->shot_nearRange = 0;
    p->errs_rate = agl_rnd(m6);
    p->errs_angX = agl_rnd(m7) + k7;
    p->errs_angY = agl_rnd(m8) + k8;
}

/* 12 key reaction bytes, drawn in the original's store order. */
static inline void agl_keys(stComData *d, int m)
{
    d->move_keydata.delay = agl_rnd(m);
    d->move_keydata.frame = agl_rnd(m);
    d->turn_keydata.delay = agl_rnd(m);
    d->turn_keydata.frame = agl_rnd(m);
    d->view_keydata.delay = agl_rnd(m);
    d->view_keydata.frame = agl_rnd(m);
    d->boost_keydata.delay = agl_rnd(m);
    d->boost_keydata.frame = agl_rnd(m);
    d->shot_keydata.delay = agl_rnd(m);
    d->shot_keydata.frame = agl_rnd(m);
    d->blade_keydata.delay = agl_rnd(m);
    d->blade_keydata.frame = agl_rnd(m);
}

/* EO (orbit weapon) block of a core with func_type 0 (asm-only: class 0 0x16f9bc, class 1 0x16ff44, class 2 0x1704d4). */
static void agl_eo_data(stComData *d, AcPlayerParamSt *pParam, int cls)
{
    stEoData *pEoData = &d->eo_data;
    AcPtsResSt *pPartsRes = AcCtrl_GetPartsRes(s_pAutoGenCom);
    AcPartsEoSt *pEoParts = AcCtrl_GetPartsEo(s_pAutoGenCom);
    int isShell = aceo_IsShellType(pEoParts);
    PartsParamWeaponSt *pWep = AcPtsRes_GetWeaponPtr(pPartsRes, 0, 0);
    pEoData->onuse = 0;
    if (cls == 2) {
        pEoData->canuse = agl_rnd(0x4000);                    /* RANDN(1): always 0 */
        pEoData->sight = agl_rnd(0xf0000) + 0x3c;
    } else {
        pEoData->canuse = 1;
        pEoData->sight = agl_rnd(0x78000) + 0x1e;
    }
    pEoData->onsight = 0;
    int r = agl_rnd(cls == 1 ? 0xc8000 : 0x50000);
    pEoData->dist_max = (unsigned short)agl_f2u(pWep->act.range - (float)r);
    if (isShell) {
        pEoData->ammo = 0;
        pEoData->heat = 0;
        pEoData->energy = 0;
        return;
    }
    if (cls == 2) r = agl_rnd(0x78000) + 0x14;
    else r = agl_rnd(0xc8000) + 0x32;
    pEoData->ammo = agl_scale_u16(AGL_0_01, r, pWep->blt_sta);
    r = agl_rnd(0x28000) + 0x4b;
    pEoData->heat = agl_scale_u16(AGL_0_01, r, pParam->tmprtLimit);
    pEoData->energy = 0x14;
}

/* Multi-booster extension parameters: enemy distance limit (one draw per class), view angle 0..60, boost rate
 * (second draw), energy rate 20. Classes other than 0..2 return before any draw. 60.0f is stored before the second
 * draw (delay slot), harmless. Classes 0 / 1 asm-only (REV). */
static void __AutoGenLogic_SetBasisParam_ExMultiBooster(stExtData *pExData)
{
    PS2_FRAME(0x20);
    _ext_mb_param *p = (_ext_mb_param *)pExData;
    int m, k;
    switch (s_PilotClass) {
    case 2: m = 0x280000; k = 0x78; break;   /* 0x16e928 */
    case 1: m = 0x140000; k = 0xa0; break;   /* 0x16e8b0 */
    case 0: m = 0xa0000; k = 0xb4; break;    /* 0x16e838 */
    default: return;
    }
    p->ene_dist_max = (float)(agl_rnd(m) + k);
    p->view_ang_min = 0.0f;
    p->view_ang_max = 60.0f;
    p->rate = (unsigned short)(agl_rnd(0xc8000) + 0x19);
    p->energy_rate = 0x14;
}

/* Basic COM numbers (alert levels, sense / reaction delays, key delays, EO use), then the left arm unit (Lau)
 * and extension (Ex) parameters. The Lau / Ex helpers other than LauBlade / LauGun / ExMultiBooster are never run (interpreted). */
static void __AutoGenLogic_SetBasisParam(int pilotClass, PartsParamSwordSt *pArmUnitLParam,
                                         PartsParamExtensionSt *pExParam)
{
    PS2_FRAME(0x80);
    (void)pilotClass;   /* passed on in a1 to the Lau / Ex helpers, which DWARF shows without it (unused) */
    AcPlayerParamSt *pParam = &s_pAutoGenCom->param;
    PartsParamCoreSt *pCore = pParam->corep;
    stComData *d = s_pComData;
    int cls = s_PilotClass;
    if (cls == 0 || cls == 1 || cls == 2) {
        if (cls == 0) {                                       /* 0x16f6d8 */
            ps2_rand();                                       /* result unused */
            d->alert_energy = agl_rnd(0x28000) + 10;
            ps2_rand();                                       /* result unused */
            d->alert_bullet = agl_rnd(0x28000) + 10;
            int r = agl_rnd(0x28000) + 0x4b;
            d->alert_heat = agl_scale_u16(AGL_0_01, r, pParam->tmprtLimit);
            d->sense_delay = agl_rnd(0x4000);                 /* always 0 */
            d->blt_reactdist = agl_rnd(0xa0000) + 0x8c;
            agl_keys(d, 0x8000);
        } else {                                              /* class 1 0x16fb98, class 2 0x170128 */
            if (agl_rnd(0x4000)) d->alert_energy = agl_rnd(0x28000);   /* RANDN(1): never */
            else d->alert_energy = agl_rnd(0x50000) + 0x1e;
            if (agl_rnd(0x4000)) d->alert_bullet = agl_rnd(0x28000);
            else d->alert_bullet = agl_rnd(0x50000) + 0x1e;
            int r = agl_rnd(0x18000) + 6;
            d->alert_heat = agl_scale_u16(AGL_0_1, r, pParam->tmprtLimit);
            d->sense_delay = agl_rnd(0x20000) + 2;           /* sign fix `addiu v1,v1,0`: dead */
            d->blt_reactdist = agl_rnd(0xa0000) + 0x28;
            agl_keys(d, cls == 1 ? 0x18000 : 0x28000);
        }
        if (pCore->func_type == 0) agl_eo_data(d, pParam, cls);
    }

    /* 0x1706c8 */
    d->skl_data[6].lvl = 0;
    d->skl_data[6].rate = 0x64;
    stComLau *pLau = &d->lau_data.common;
    if (pArmUnitLParam) {
        switch (pArmUnitLParam->type) {
        case 0:
        case 1:
            __AutoGenLogic_SetBasisParam_LauBlade(pLau);
            break;
        case 2:
            if (pArmUnitLParam->boost_sub) __AutoGenLogic_SetBasisParam_LauEnergyShield(pLau);
            else __AutoGenLogic_SetBasisParam_LauShield(pLau);
            break;
        case 3:
            __AutoGenLogic_SetBasisParam_LauGun(pLau);
            break;
        }
    } else {                                                  /* 0x170788 (reached) */
        __AutoGenLogic_SetBasisParam_LauBlade(pLau);
        d->lau_data.common.progno = 0;
    }

    if (!pExParam) return;                                    /* reached */
    stExtData *e = &d->ext_data;
    switch (pExParam->prog_no) {                              /* jump table 0x272110 */
    case 0:                                                   /* 0x1707d0 */
        e->fparam[0] = 360.0f;
        e->fparam[1] = 360.0f;
        e->fparam[2] = 200.0f;
        e->sparam[0] = agl_rnd(0xc8000) + 0x19;
        e->sparam[1] = 0x14;
        break;
    case 5:                                                   /* 0x170820 */
        e->fparam[0] = (float)(agl_rnd(0x78000) + 0x5a);
        e->fparam[1] = (float)(agl_rnd(0xa0000) + 0x3c);
        e->sparam[0] = 0x14;
        break;
    case 7:
        __AutoGenLogic_SetBasisParam_ExMultiBooster(e);
        break;
    case 8:                                                   /* 0x1708b0 */
        switch (s_PilotClass) {
        case 0: e->sparam[1] = agl_rnd(0x28000) + 0x14; break;
        case 1: e->sparam[1] = agl_rnd(0x78000) + 0x14; break;
        case 2: e->sparam[1] = agl_rnd(0xc8000); break;
        default: return;
        }
        e->sparam[0] = (unsigned short)e->sparam[1] + agl_rnd(0x28000) + 0x14;
        break;
    case 9:
        __AutoGenLogic_SetBasisParam_ExSideShield(e);
        break;
    case 12:                                                  /* 0x170a18 */
        switch (s_PilotClass) {
        case 0: e->fparam[0] = 15.0f; break;
        case 1: e->fparam[0] = (float)(agl_rnd(0x28000) + 10); break;
        case 2: e->fparam[0] = (float)(agl_rnd(0x64000) + 0xf); break;
        }
        break;
    case 16:                                                  /* 0x170ad8 */
        switch (s_PilotClass) {
        case 0: e->fparam[0] = (float)agl_rnd(0x14000); break;
        case 1: e->fparam[0] = (float)(agl_rnd(0x14000) + 5); break;
        case 2: e->fparam[0] = (float)(agl_rnd(0x28000) + 10); break;
        }
        break;
    default:
        break;
    }
}

/* Tactics op: value / time limits and the vs-enemy distance (range1 scaled by a random 0.5..1.x). */
static void __AutoGenLogic_UpdateTacticsOpData(stOpData *pTacticsOpData, int range1, int range2)
{
    PS2_FRAME(0x50);
    int m, k;
    switch (s_PilotClass) {
    case 2: m = 0x3c000; k = 0x5a; break;   /* 0x170dc0 */
    case 1: m = 0x50000; k = 0x50; break;   /* 0x170cf8 */
    case 0: m = 0x28000; k = 0x50; break;   /* 0x170c30 */
    default: return;
    }
    stComOpParamVsEnemy *pOpParam = &pTacticsOpData->param.vsene;
    pTacticsOpData->value_init = 0x32;
    pTacticsOpData->value_min = 0;
    pTacticsOpData->value_max = 0x64;
    ps2_rand();                                   /* result unused */
    pTacticsOpData->time_min = 0x258;
    ps2_rand();                                   /* result unused */
    pTacticsOpData->time_max = pTacticsOpData->time_min + 0x258;
    int r = agl_rnd(m) + k;
    double a = ps2_dmul(AGL_0_01, ps2_i2d(r));
    pOpParam->dist[0] = ps2_d2f(ps2_dmul(ps2_f2d((float)range1), a));
    pOpParam->calc[0] = (float)range1;
    pOpParam->calc[1] = (float)(range1 - range2);
}

/* Weapon op: shot timing, distances (fractions of the weapon's range), error angles. */
static void __AutoGenLogic_SetWeaponOpData(stComWepData *pWepData, PartsParamWeaponSt *pWepParam, int wepType)
{
    PS2_FRAME(0x50);
    const int timeTable[4] = {300, 360, 420, 480};   /* rodata 0x25acd0 */
    float range = pWepParam->act.range;
    int cls = s_PilotClass;
    if (cls != 0 && cls != 1 && cls != 2) return;

    pWepData->progno = wepType;
    pWepData->wep_id = wepType;
    pWepData->value_init = 0x32;
    pWepData->value_min = s_RaiseValue[wepType];
    pWepData->value_max = 0x64;

    pWepData->dist_target = agl_range_part(range, agl_rnd(0x4000) + 5);
    pWepData->dist_max = agl_range_part(range, agl_rnd(0x4000) + (cls == 0 ? 8 : 9));
    pWepData->dist_min = 0;
    pWepData->time_min = timeTable[agl_rnd(0xc000)];
    pWepData->time_max = pWepData->time_min + timeTable[agl_rnd(0xc000)];

    if (cls == 2) {                               /* 0x1714b8 */
        pWepData->shot_rate = agl_rnd(0x3c000) + 0x46;
        pWepData->shot_delay = agl_rnd(0xc000) + 7;
        pWepData->shot_minLTime = agl_rnd(0x28000);
        pWepData->shot_maxLTime = pWepData->shot_minLTime + agl_rnd(0x14000);
        pWepData->shot_maxSTime = 0;
        pWepData->shot_nearRange = agl_range_part(range, agl_rnd(0x4000) + 4);
        pWepData->errs_rate = agl_rnd(0x3c000) + 0x46;
        pWepData->errs_angX = agl_rnd(0x3c000) + 5;
        pWepData->errs_angY = agl_rnd(0x3c000) + 5;
    } else if (cls == 1) {                        /* 0x1711c8 */
        pWepData->shot_rate = agl_rnd(0x14000) + 0x55;
        pWepData->shot_delay = agl_rnd(0x8000) + 4;
        pWepData->shot_minLTime = agl_rnd(0x14000);
        pWepData->shot_maxLTime = pWepData->shot_minLTime + agl_rnd(0x14000);
        pWepData->shot_maxSTime = 0;
        pWepData->shot_nearRange = agl_range_part(range, agl_rnd(0x4000) + 4);
        pWepData->errs_rate = agl_rnd(0x14000) + 0x55;
        pWepData->errs_angX = agl_rnd(0x28000) + 5;
        pWepData->errs_angY = agl_rnd(0x28000) + 5;
    } else {                                      /* 0x170f08 */
        pWepData->shot_rate = agl_rnd(0x28000) + 0x5a;
        pWepData->shot_delay = agl_rnd(0xc000);
        pWepData->shot_minLTime = 0;
        pWepData->shot_maxLTime = pWepData->shot_minLTime + agl_rnd(0x14000);
        pWepData->shot_maxSTime = 0;
        pWepData->shot_nearRange = agl_range_part(range, agl_rnd(0x4000) + 4);
        pWepData->errs_rate = agl_rnd(0x3c000) + 0x55;
        pWepData->errs_angX = agl_rnd(0x14000);
        pWepData->errs_angY = agl_rnd(0x14000);
    }
}

/* ---- g3: tree blob, weapon list, the CreateComLogic worker, public API ---- */

#ifdef ACNB_SOURCEPORT
/* ComOpData file ids (small constants owned by this file: main .data / .sdata) */
unsigned short s_TacticsOpBindId[5] = {0x7d1, 0x7d2, 0x7d3, 0x7d4, 0x7d5};   /* 0x25acc0 */
unsigned short s_CommonOpBindId[3] = {0x7e0, 0x7e1, 0x7e2};                 /* 0x27ace0 */
unsigned short s_SpChrOpBindId[2] = {0x7f8, 0x7f9};                         /* 0x27ace8 */
char agl_fmt_s[4] = "%s";                                                   /* 0x27acec (nodwarf.h) */
char agl_comedit[8] = "ComEdit";                                            /* 0x27acf0 (nodwarf.h) */
#endif

/* One 16-byte tagNode of the tree blob (pNodeNext / pNodeChild hold node numbers, not pointers). */
struct agl_node {
    int data;
    int next;
    int child;
    unsigned char flags;   /* bit 0 = isParent */
    unsigned char type;
    unsigned char idNode;
    unsigned char idData;
};

/* Node list of the tree: a root (child = node 1), one op node per op (type 1, chained by next), then one weapon
 * node per weapon (type 2; the last one's next = 0). */
static void __AutoGenLogic_CreateComTreeNode(char *pTreeBuf)
{
    PS2_FRAME(0xe0);
    char *dst = pTreeBuf + 0x30;
    unsigned int idNode = 1;
    int link = 2;
    int opNo = 0;
    agl_node node;

    ps2_memset(&node, 0, 0x10);
    node.data = 0;
    node.next = 0;
    node.child = 1;
    node.flags = (unsigned char)((node.flags & ~1) | (idNode & 1));
    node.type = 0;
    node.idNode = 0;
    node.idData = 0;
    ps2_memcpy(dst, &node, 0x10);
    dst += 0x10;

    for (int i = 0; i < ((unsigned char *)pTreeBuf)[0x22]; i++) {
        int next = link++;
        unsigned char idData = (unsigned char)opNo++;
        unsigned char id = (unsigned char)idNode;
        idNode = (idNode + 1) & 0xff;
        ps2_memset(&node, 0, 0x10);
        node.data = 0;
        node.next = next;
        node.child = 0;
        node.flags = (unsigned char)(node.flags & ~1);
        node.type = 1;
        node.idNode = id;
        node.idData = idData;
        ps2_memcpy(dst, &node, 0x10);
        dst += 0x10;
    }

    for (int j = 0; j < s_WepNum; j++) {
        int next;
        unsigned char id = (unsigned char)idNode;
        idNode = (idNode + 1) & 0xff;
        if (j == s_WepNum - 1) {
            next = 0;
        } else {
            next = link++;
        }
        ps2_memset(&node, 0, 0x10);
        node.data = 0;
        node.next = next;
        node.child = 0;
        node.flags = (unsigned char)(node.flags & ~1);
        node.type = 2;
        node.idNode = id;
        node.idData = (unsigned char)j;
        ps2_memcpy(dst, &node, 0x10);
        dst += 0x10;
    }
}

/* The "ComEdit" tree blob in tree_buff: header (counts and section offsets), nodes, op data, weapon data. */
static void __AutoGenLogic_CreateComTree(void)
{
    PS2_FRAME(0x30);
    char *buf = s_pComData->tree_buff;
    struct_dbcc1f63 *h = (struct_dbcc1f63 *)buf;
    ps2_memset(buf, 0, 0x1400);
    ps2_memset(buf, 0, 8);
    ps2_sprintf_s(buf, agl_fmt_s, agl_comedit);
    h->numOperation = (unsigned char)(s_WepNum + 4 + s_SpCharNum);
    h->numWeapon = (unsigned char)s_WepNum;
    h->numNode = (short)(h->numOperation + 1 + h->numWeapon);
    h->numIf = 0;
    h->numCommand = 0;
    h->offsetNode = 0x30;
    h->offsetOperation = h->offsetNode + h->numNode * 16;
    h->offsetWeapon = h->offsetOperation + h->numOperation * 0x80;
    h->offsetIf = h->offsetWeapon + h->numWeapon * 0x80;
    h->offsetCommand = h->offsetIf + h->numIf * 16;
    __AutoGenLogic_CreateComTreeNode(buf);
    unsigned int o = h->offsetOperation;
    ps2_memcpy(buf + o, s_CommonOpData, 0x180);
    o += 0x180;
    ps2_memcpy(buf + o, s_TacticsOpData, s_WepNum << 7);
    o += s_WepNum << 7;
    ps2_memcpy(buf + o, &s_EmergencyEscOpData, 0x80);
    if (s_SpCharNum)                                       /* dead: s_SpCharNum is always 0 */
        ps2_memcpy(buf + o + 0x80, s_SpChrOpData, s_SpCharNum << 7);
    ps2_memcpy(buf + h->offsetWeapon, s_WepData, s_WepNum << 7);
}

static void __AutoGenLogic_SetComData(void)
{
    __AutoGenLogic_CreateComTree();
}

/* Appends one weapon: its lock range and type to s_WepInfo, its op data to s_WepData. */
static void agl_add_weapon(AcPlayerParamSt *p, PartsParamWeaponSt *pWep, PartsParamWeaponSt *pLWep, int wepType)
{
    float range = __AutoGenLogic_GetWepLockRange(p, pWep, pLWep);
    int n = s_WepNum;
    s_WepInfo[n].Range = ps2_ftoi(range);
    s_WepInfo[n].WepType = wepType;
    __AutoGenLogic_SetWeaponOpData(&s_WepData[n], pWep, wepType);
    s_WepNum = (char)(s_WepNum + 1);
}

/* The COM's weapon list (types 0 arm / hand gun, 1 left arm unit, 2 / 3 back weapons, 4 inside), then s_WepInfo
 * sorted by ascending lock range (s_WepData stays in insertion order). */
static void __AutoGenLogic_SetWeaponInfo(void)
{
    PS2_FRAME(0xb0);
    AcPlayerParamSt *p = &s_pAutoGenCom->param;
    AcPtsResSt *res = AcCtrl_GetPartsRes(s_pAutoGenCom);
    PartsParamSwordSt *swordp = p->swordp;
    PartsParamArmsSt *armsp = p->armsp;
    PartsParamInsideSt *insidep = p->insidep;
    PartsParamBackSt *Lbackp = p->Lbackp;
    PartsParamBackSt *Rbackp = p->Rbackp;
    PartsParamGunSt *gunp = p->gunp;
    PartsParamWeaponSt *lwep = 0, *ptr;

    if (swordp && swordp->type == 3)
        lwep = AcPtsRes_GetWeaponPtr(res, 6, 0);
    if (armsp && armsp->type) {
        ptr = AcPtsRes_GetWeaponPtr(res, 2, 0);
        agl_add_weapon(p, ptr, lwep, 0);
    } else if (gunp) {                                       /* 0x1718d0 (reached) */
        ptr = AcPtsRes_GetWeaponPtr(res, 5, 0);
        agl_add_weapon(p, ptr, lwep, 0);
    }
    if (swordp && swordp->type == 3) {
        ptr = AcPtsRes_GetWeaponPtr(res, 6, 0);
        agl_add_weapon(p, ptr, lwep, 1);
    }
    if (Lbackp && Lbackp->w_use != 2) {
        ptr = AcPtsRes_GetWeaponPtr(res, 8, 0);
        if (ptr) agl_add_weapon(p, ptr, lwep, 2);
    }
    if (Rbackp && Rbackp->w_use == 0) {                      /* 0x171a78 (reached) */
        ptr = AcPtsRes_GetWeaponPtr(res, 7, 0);
        if (ptr) agl_add_weapon(p, ptr, lwep, 3);
    }
    if (insidep) {
        ptr = AcPtsRes_GetWeaponPtr(res, 10, 0);
        agl_add_weapon(p, ptr, lwep, 4);
    }

    int last = s_WepNum - 1;
    for (int i = 0; i < last; i++) {
        for (int j = 0; j < last; j++) {
            if (s_WepInfo[j + 1].Range < s_WepInfo[j].Range) {   /* 0x171bc0: slt next, cur */
                stAutoGenWepInfo tmp = s_WepInfo[j];
                s_WepInfo[j] = s_WepInfo[j + 1];
                s_WepInfo[j + 1] = tmp;
            }
        }
    }
}

/* Random template index into a group file: (rand() % (n << 14)) >> 14, no draw for an empty group. */
static inline const stOpData *agl_pick(stAutoGenLogicDataHead *grp)
{
    int n = grp->DataNum;
    int k = (n > 0) ? agl_rnd(n << 14) : 0;
    return (const stOpData *)((char *)grp + 0x10) + k;
}

static void __AutoGenLogic_CreateComLogic(AcPlayerSt *pAutoGenCom, stAutoGenLogic *pPilotLogic, int pilotClass)
{
    PS2_FRAME(0x90);
    s_PilotClass = (char)pilotClass;
    s_pAutoGenCom = pAutoGenCom;
    __AutoGenLogic_CalcRaiseValue();
    for (int i = 0; i < 3; i++)
        ps2_memcpy(&s_CommonOpData[i], agl_pick(s_pCommonGroupData[i]), 0x80);
    __AutoGenLogic_SetWeaponInfo();
    for (int i = 0; i < s_WepNum; i++) {
        stAutoGenLogicDataHead *grp = s_pTacticsGroupData[(int)pPilotLogic->TacticGroup[s_WepInfo[i].WepType]];
        ps2_memcpy(&s_TacticsOpData[i], agl_pick(grp), 0x80);
        if (i == 0)
            __AutoGenLogic_UpdateTacticsOpData(&s_TacticsOpData[i], s_WepInfo[i].Range, 0);
        else
            __AutoGenLogic_UpdateTacticsOpData(&s_TacticsOpData[i], s_WepInfo[i].Range, s_WepInfo[i - 1].Range);
        s_TacticsOpData[i].value_min =
            (unsigned char)(s_TacticsOpData[i].value_min + (unsigned char)s_RaiseValue[s_WepInfo[i].WepType]);
    }
    ps2_memcpy(&s_EmergencyEscOpData, agl_pick(s_pEmergencyEscGroupData), 0x80);
    __AutoGenLogic_SetBasisParam(pilotClass, pAutoGenCom->param.swordp, pAutoGenCom->param.extp);
    __AutoGenLogic_SetComData();
}

/* Loads the ComOpData group files (never freed: Finish is empty). */
static inline stAutoGenLogicDataHead *agl_load(int id)
{
    int size = File_GetSize(id);
    stAutoGenLogicDataHead *p = (stAutoGenLogicDataHead *)Memm_Alloc(1, size, 0x10);
    ZeroMemory32b(p, (unsigned int)size >> 2);
    File_Read(id, size, (char *)p, 1);
    return p;
}

static void __AutoGenLogic_ReadComOpData(void)
{
    PS2_FRAME(0x50);
    for (int i = 0; i < 3; i++) s_pCommonGroupData[i] = agl_load(s_CommonOpBindId[i]);
    for (int i = 0; i < 5; i++) s_pTacticsGroupData[i] = agl_load(s_TacticsOpBindId[i]);
    for (int i = 0; i < 2; i++) s_pSpChrGroupData[i] = agl_load(s_SpChrOpBindId[i]);
    s_pEmergencyEscGroupData = agl_load(0x7fa);
}

/* pilotClass 0x66 / 0x65 -> logic class 2 / 1, anything else (0x64) -> 0; the result goes to the menu overlay's
 * ComData[0]. */
void AutoGenLogic_CreateComLogic(AcPlayerSt *pAutoGenCom, stAutoGenLogic *pPilotLogic, int pilotClass)
{
    PS2_FRAME(0x10);
    int logic = 0;
    if (pilotClass == 0x66) logic = 2;
    else if (pilotClass == 0x65) logic = 1;
    __AutoGenLogic_CreateComLogic(pAutoGenCom, pPilotLogic, logic);
    ps2_memcpy(&ComData[0], s_pComData, 0x2800);
}

void AutoGenLogic_Finish(void)
{
}

void AutoGenLogic_Init(void)
{
    PS2_FRAME(0x10);
    s_pComData = &g_nextComData;
    s_PilotClass = 2;
    s_pAutoGenCom = 0;
    ps2_memset(s_CommonOpData, 0, 0x180);
    ps2_memset(s_TacticsOpData, 0, 0x280);
    /* memset(.., 0, 0xc / 0x14 / 8 / 4) of the pointer tables: assignments (host pointers are 8 bytes) */
    for (int i = 0; i < 3; i++) s_pCommonGroupData[i] = 0;
    for (int i = 0; i < 5; i++) s_pTacticsGroupData[i] = 0;
    for (int i = 0; i < 2; i++) s_pSpChrGroupData[i] = 0;
    s_pEmergencyEscGroupData = 0;
    s_SpCharNum = 0;
    s_WepNum = 0;
    ps2_memset(s_WepData, 0, 0x280);
    ps2_memset(s_WepInfo, 0, 0x28);
    ps2_memset(s_pComData, 0, 0x2800);
    __AutoGenLogic_ReadComOpData();
}
