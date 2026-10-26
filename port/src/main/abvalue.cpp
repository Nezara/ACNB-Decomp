/* src/main/abvalue.cpp — "ability value" statistics for the end-of-mission AC evaluation (s_Result): frames spent at
 * each lock-on range band and in each movement mode, frames with the booster extension busy, frames with the target in
 * sight / behind the player, the longest stretch without AP loss, and how many shots went into which range band.
 * AbVal_Init arms it at sortie start, AbVal_Proc runs once per battle frame for g_ac[0], the weapon code calls
 * AbVal_AddShootRangeCount per shot, AbVal_End (mission end) copies the Info counters in. No effect on gameplay.
 * Counters saturate at 0x7fffffff except backSightFrame (original). */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"

#ifdef ACNB_SOURCEPORT
float __GetShootRangeIndex__shootRangeRate[6] = {0.0f, 0.125f, 0.375f, 0.625f, 0.875f, 1.0f};
float __MoveRangeValProc__moveRangeRate[3] = {0.25f, 0.75f, 1.0f};
#endif

static_assert(offsetof(AcPlayerSt, actno) == 0x18, "AcPlayerSt.actno");
static_assert(offsetof(AcPlayerSt, equip_weapon) == 0x2a, "AcPlayerSt.equip_weapon");
static_assert(offsetof(AcPlayerSt, lockmng.pri_lock) == 0xe0, "AcPlayerSt.lockmng.pri_lock");
static_assert(offsetof(AcPlayerSt, mp) == 0xc94, "AcPlayerSt.mp");
static_assert(offsetof(AcPlayerSt, param.extp) == 0x764, "AcPlayerSt.param.extp");
static_assert(offsetof(AcPlayerSt, weaponctrl[0].wp) == 0x1840, "AcPlayerSt.weaponctrl.wp");
static_assert(offsetof(_weapon_st, range) == 0x20, "_weapon_st.range");

#define ABVAL_MAX 0x7fffffff

static inline void abval_inc(int *p)
{
    if (*p != ABVAL_MAX) *p = *p + 1;
}

/* Info_GetData returns a union through a hidden pointer: the result slot is the original's stack slot */
static int abval_info(int type, int id, int arg1, unsigned int slot)
{
    PS2_STACK(union_9a8d3bef, r, slot);
    gcall_sret(GFN(Info_GetData), &r, type, id, arg1, 0);
    return r.i;
}

/* the model the AC's primary lock points at, or 0 */
static inline StModel *abval_lock_model(AcPlayerSt *pAC)
{
    StWepLock *l = pAC->lockmng.pri_lock;
    return l ? l->ofp : 0;
}

/* |center(a) - center(b)| as vsub.xyz + (x² + y²) + z² + sqrt.s */
static float abval_center_dist(StModel *a, StModel *b, unsigned int ca, unsigned int cb, unsigned int cd)
{
    PS2_STACK(float[4], c0, ca);
    PS2_STACK(float[4], c1, cb);
    PS2_STACK(float[4], d, cd);
    DF_GetModelCenter(c0, a);
    DF_GetModelCenter(c1, b);
    d[0] = c0[0] - c1[0];
    d[1] = c0[1] - c1[1];
    d[2] = c0[2] - c1[2];
    d[3] = c0[3];
    float x = d[0] * d[0], y = d[1] * d[1], z = d[2] * d[2];
    return ps2_sqrt(z + (x + y));
}

/* is the player behind the target: the horizontal direction target -> player against the target's facing
 * (sin, -cos) of ang[1]; dot > 0.5 */
static int __IsTargetRearPosition(AcPlayerSt *pAC, StModel *pTgtMdl)
{
    PS2_FRAME(0x50);
    PS2_STACK(float[4], n, 0x30);
    PS2_STACK(float[4], f, 0x40);
    float *ml = pAC->mp->loc, *tl = pTgtMdl->loc;
    n[0] = ml[0] - tl[0];
    n[1] = ml[1] - tl[1];
    n[2] = ml[2] - tl[2];
    n[3] = ml[3];
    n[1] = 0.0f;
    n[3] = 1.0f;
    vu_normalize(n, n);
    f[0] = fsmath_SinVU0(pTgtMdl->ang[1]);
    f[1] = 0.0f;
    f[2] = -fsmath_CosVU0(pTgtMdl->ang[1]);
    f[3] = 1.0f;
    float v = n[0] * f[0] + n[2] * f[2];
    return !(v <= 0.5f);
}

/* frames with a booster-type extension (prog_no 0 / 1 / 5 / 7 / 8) busy */
static void __ExtensionMoveValProc(AcPlayerSt *pAC)
{
    PS2_FRAME(0x20);
    PartsParamExtensionSt *extp = pAC->param.extp;
    if (!extp) return;
    int pn = extp->prog_no;
    if (pn != 8 && pn != 7 && pn != 5 && pn != 1 && pn != 0) return;
    AcPartsExtSt *e = AcCtrl_GetPartsExt(pAC);
    if (e && e->busy) abval_inc(&s_Result.moveVal.moveMeansFrame[5]);
}

/* frames per movement mode (by actno) */
static void __MeansRangeValProc(AcPlayerSt *pAC)
{
    int *m = s_Result.moveVal.moveMeansFrame;
    switch (pAC->actno) {
    case 0x12: abval_inc(&m[4]); break;
    case 8: abval_inc(&m[3]); break;
    case 9: case 7: abval_inc(&m[2]); break;
    case 6: case 5: abval_inc(&m[1]); break;
    case 4: case 3: case 1: abval_inc(&m[0]); break;
    }
    __ExtensionMoveValProc(pAC);
}

/* frames per lock-on distance band (distance / range of the equipped weapon; range 0 -> the PS2 divide rule) */
static void __MoveRangeValProc(AcPlayerSt *pAC, StModel *pTgtMdl)
{
    PS2_FRAME(0x80);
    _weapon_st *wp = pAC->weaponctrl[pAC->equip_weapon].wp;
    float dist = abval_center_dist(pAC->mp, pTgtMdl, 0x70, 0x60, 0x50);
    float ratio = ps2_div(dist, wp->range);
    int i = 0;
    for (; i < 2; i++)
        if (ratio < __MoveRangeValProc__moveRangeRate[i]) break;
    abval_inc(&s_Result.moveVal.moveRangeFrame[i]);
}

static int __GetShootRangeIndex(_weapon_st *pWep, float dist)
{
    float ratio = ps2_div(dist, pWep->range);
    int i = 0;
    for (; i < 5; i++)
        if (ratio < __GetShootRangeIndex__shootRangeRate[i + 1]) break;
    return i;
}

/* (sic) weapons with prog_no % 5 == 3 are not counted; a shot without a locked model goes to band 5 */
void AbVal_AddShootRangeCount(AcPlayerSt *pAC, _weapon_st *pWep)
{
    PS2_FRAME(0x70);
    if (!s_IsAbVal) return;
    if (pWep->prog_no % 5 == 3) return;
    StModel *mp = abval_lock_model(pAC);
    int *cnt = s_Result.shootVal.shootRangeCount;
    if (!mp) {
        abval_inc(&cnt[5]);
        return;
    }
    float dist = abval_center_dist(pAC->mp, mp, 0x60, 0x50, 0x40);
    abval_inc(&cnt[__GetShootRangeIndex(pWep, dist)]);
}

/* attacks on / hits taken by the other ACs and enemies that are still alive (cflag bit 0 clear) */
static void __SetAvoidValData(AbValueAvoidValSt *pAvoid)
{
    PS2_FRAME(0x80);
    int atk = 0, hit = 0;
    for (int i = 1; i < NowPlayerNum; i++) {
        if (g_ac[i].mp->cflag & 1) continue;
        atk += abval_info(1, 2, i, 0x7c);
        atk += abval_info(1, 4, i, 0x78);
        hit += abval_info(1, 3, i, 0x74);
        hit += abval_info(1, 5, i, 0x70);
    }
    for (int i = 0; i < StgInfo.cnum; i++) {
        EnemySt *e = DFene_GetEnemy(i);
        if (e->mp->cflag & 1) continue;
        atk += abval_info(2, 2, i, 0x6c);
        hit += abval_info(2, 3, i, 0x68);
    }
    pAvoid->beAttackCount = atk;
    pAvoid->beHitCount = hit;
    pAvoid->maxAvoidFrame = s_AvoidWork.maxFrame < s_AvoidWork.nowFrame ? s_AvoidWork.nowFrame : s_AvoidWork.maxFrame;
}

static void __SetAttackValData(AbValueAttackValSt *pAttack)
{
    PS2_FRAME(0x60);
    pAttack->destroyCount = abval_info(1, 6, 0, 0x5c);
    for (int i = 0; i < 9; i++) pAttack->attackCount[i] = abval_info(0, i, 0, 0x58);
    for (int j = 0; j < 9; j++) pAttack->hitCount[j] = abval_info(0, 9 + j, 0, 0x54);
}

void AbVal_End(void)
{
    PS2_FRAME(0x30);
    if (!s_IsAbVal) return;
    __SetAttackValData(&s_Result.attackVal);
    s_Result.shootVal.shootCount = abval_info(1, 2, 0, 0x2c);
    s_Result.shootVal.shootHitCount = abval_info(1, 3, 0, 0x28);
    s_Result.bladeVal.bladeCound = abval_info(1, 4, 0, 0x24);
    s_Result.bladeVal.bladeHitCount = abval_info(1, 5, 0, 0x20);
    __SetAvoidValData(&s_Result.avoidVal);
    s_Result.moveVal.chargeCount = abval_info(1, 9, 0, 0x1c);
    s_Result.moveVal.landFreezeCount = abval_info(1, 10, 0, 0x18);
    s_Result.moveVal.dashFreezeCount = abval_info(1, 11, 0, 0x14);
}

void AbVal_Proc(void)
{
    PS2_FRAME(0x40);
    if (!s_IsAbVal) return;
    int ap = abval_info(1, 1, 0, 0x3c);
    if (s_AvoidWork.nowAP == ap) {
        abval_inc(&s_AvoidWork.nowFrame);
    } else {
        if (s_AvoidWork.maxFrame < s_AvoidWork.nowFrame) s_AvoidWork.maxFrame = s_AvoidWork.nowFrame;
        s_AvoidWork.nowAP = ap;
        s_AvoidWork.nowFrame = 0;
    }
    AcPlayerSt *ac = &g_ac[0];
    StModel *mp = abval_lock_model(ac);
    if (mp) __MoveRangeValProc(ac, mp);
    __MeansRangeValProc(ac);
    mp = abval_lock_model(ac);
    if (mp) {
        abval_inc(&s_Result.sightVal.inSightFrame);
        if (__IsTargetRearPosition(ac, mp)) s_Result.sightVal.backSightFrame++;
    }
}

void AbVal_Init(int bOnOff)
{
    PS2_FRAME(0x20);
    s_IsAbVal = bOnOff;
    int ap = abval_info(1, 0, 0, 0x1c);
    s_AvoidWork.nowFrame = 0;
    s_AvoidWork.maxFrame = 0;
    memset(&s_Result, 0, 0xe0);
    s_AvoidWork.nowAP = ap;
}
