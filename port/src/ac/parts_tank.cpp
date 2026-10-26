/* src/ac/parts_tank.cpp - tank (caterpillar) legs (whole file, OLSORTIE overlay; prog_no 1). actank_Control
 * (acp->managefunc) tilts the body to the ground every frame - four ground points from AcCtrl_GetGndHigh, the plane
 * of the three that are not the highest one (y offsets clamped to 2.2) blended into acp->tank_norm, the waist object's
 * world matrix rebuilt from it - and sets the footstep effect flags. The tankac_* statics are the action handlers
 * reached through the interpreted table g_actank_Interface. Same algorithm as parts_4leg.cpp; act_move / act_stop
 * return 0 and keep the 4-leg hokan fix (prog_no == 2 arm, dead for a tank). */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"

void actank_Control(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    tankac_norm_update(acp);
    tankac_set_footefect(acp);
}

/* footstep effect: 0x1000 = moving, 0x2000 = turning on the spot (no jump / boost skip, unlike the leg types) */
static void tankac_set_footefect(AcPlayerSt *acp)
{
    float d = acp->ang[1] - acp->oang[1];
    while (!(d <= 3.1415927f)) d = d - 6.2831855f;
    while (d < -3.1415927f) d = d + 6.2831855f;
    float vx = acp->loc[0] - acp->oloc[0];   /* vsub.xyz (only x / z are read) */
    float vz = acp->loc[2] - acp->oloc[2];
    if (acp->actstat & 1) return;
    if (d < 0.0f) d = -d;
    if (d < 0.01f) {
        float x2 = vx * vx, z2 = vz * vz;
        if (ps2_sqrt(x2 + z2) < 0.01f) return;   /* mula.s / madd.s */
    }
    float x2 = vx * vx, z2 = vz * vz;
    if (!(ps2_sqrt(x2 + z2) <= 0.01f)) acp->effectstat |= 0x1000;
    else acp->effectstat |= 0x2000;
}

/* ground normal (op[13] = body space) from the three ground points other than the highest one; 0 if the highest
 * point is 8 or more above loc */
static int tankac_getnorm_from4verts(float *norm, AcPlayerSt *acp, float (*lvec)[4])
{
    PS2_FRAME(0xb0);
    int best = 0;
    float by = lvec[0][1];
    for (int i = 1; i < 4; i++) {
        if (1.0e-4f + by < lvec[i][1]) {   /* 0x38d1b717 */
            by = lvec[i][1];
            best = i;
        }
    }
    if (!(by - acp->loc[1] < 8.0f)) return 0;
    float v[3][4] __attribute__((aligned(16)));   /* a, b, c: full 128-bit copies */
    int ia, ib, ic;
    switch (best) {
    case 0: ia = 1; ib = 2; ic = 3; break;
    case 1: ia = 0; ib = 2; ic = 3; break;
    case 2: ia = 0; ib = 3; ic = 1; break;
    default: ia = 0; ib = 2; ic = 1; break;   /* 3 (best is always 0..3) */
    }
    memcpy(v[0], lvec[ia], 16);
    memcpy(v[1], lvec[ib], 16);
    memcpy(v[2], lvec[ic], 16);
    float my = v[0][1];
    for (int i = 1; i < 3; i++)
        if (v[i][1] < my) my = v[i][1];
    for (int i = 0; i < 3; i++) {
        v[i][1] = v[i][1] - my;
        if (!(v[i][1] <= 2.2f)) v[i][1] = 2.2f;   /* 0x400ccccd */
    }
    float d1[4] __attribute__((aligned(16)));
    float d2[4] __attribute__((aligned(16)));
    vu_sub_xyz(d1, v[1], v[0]);
    vu_sub_xyz(d2, v[2], v[1]);
    vu_cross(norm, d2, d1);   /* vopmula d2, d1 ; vopmsub d1, d2 */
    norm[3] = d2[3];
    vu_normalize(norm, norm);
    float t[4][4] __attribute__((aligned(16)));
    sceVu0TransposeMatrix(t, *acp->op[13].wrmatp);
    vu_apply(norm, t, norm);
    return 1;
}

static void tankac_norm_update(AcPlayerSt *acp)
{
    PS2_FRAME(0x140);
    float lvec[4][4] __attribute__((aligned(16)));
    float norm[4] __attribute__((aligned(16)));
    float t[4][4] __attribute__((aligned(16)));
    float z[4][4] __attribute__((aligned(16)));
    float m[4][4] __attribute__((aligned(16)));
    float zv[4] __attribute__((aligned(16)));
    for (int i = 0; i < 4; i++) AcCtrl_GetGndHigh(lvec[i], acp, i);
    if (!tankac_getnorm_from4verts(norm, acp, lvec)) {
        sceVu0TransposeMatrix(t, *acp->op[13].wrmatp);
        vu_apply(norm, t, (*acp->op[14].wrmatp)[1]);
    }
    norm_interpolate(acp->tank_norm, norm, 0.2f);
    sceVu0TransposeMatrix(t, *acp->op[13].wrmatp);
    vu_apply(zv, t, (*acp->op[14].wrmatp)[2]);
    GAcUtil_ZYvec2matrix(z, zv, acp->tank_norm);   /* row 3 of z stays unset: only rows 0-2 of m are used */
    vu_mul_matrix(m, *acp->op[13].wrmatp, z);
    memcpy(*acp->op[14].wrmatp, m, 0x30);
    Anim_UpdateChildObjMatrix(&acp->op[15], *acp->op[14].wrmatp);
}

static int tankac_setact_jump_or_fly(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    if (ACsetact_jumpcheck(acp)) return 1;
    if (AcCtrl_IsCharging(acp)) return 0;
    acp->velocity[1] = -acp->param.flyup_accel;
    AC_SetAction(acp, 8);
    return 1;
}

static int tankac_setact_move(AcPlayerSt *acp)
{
    PS2_FRAME(0x10);
    AC_SetAction(acp, 3);
    return 1;
}

/* while the leg animation blends in, the core / waist objects use the walk blend times (the prog_no == 2 arm is
 * the 4-leg program's and never runs for a tank) */
static void tankac_hokan_fix(AcPlayerSt *acp)
{
    if (acp->op[2].hokanframe == acp->op[2].hokantime) return;
    acp->op[3].hokantime = acp->param.hokanparamp->walk_core;
    acp->op[14].hokantime = acp->param.hokanparamp->walk_waist;
    acp->op[1].hokantime = acp->param.hokanparamp->walk_center;
    if (acp->param.prog_no != 2) return;
    acp->op[13].hokantime = acp->param.hokanparamp->walk_center;
    for (int i = 15; i < acp->mp->nobj - 1; i++) acp->op[i].hokantime = acp->param.hokanparamp->walk_center;
}

static int tankac_act_move(AcPlayerSt *acp, unsigned short dir)
{
    PS2_FRAME(0x50);
    AcAnim_ChangeAnim(acp, tankac_act_move__dir2animid[dir], 0xb4, 1);
    tankac_hokan_fix(acp);
    AcCtrl_TurnPad(acp, acp->param.turn_accel, acp->param.turn_maxspd);
    if (dir == 8) {
        AcCtrl_BrakeXz(acp, acp->param.dash_brake);
    } else {
        float maxspd = acp->param.dash_maxspd;
        float accel = acp->param.dash_accel;
        if (AcUidev_IsAnalog(acp->frdp->uidevp)) {
            char alh = AcUidev_GetPadStickConf(acp->frdp->uidevp, 0);
            char alv = AcUidev_GetPadStickConf(acp->frdp->uidevp, 1);
            float r = AcUidev_ApplyAnalogLegRate(dir, alh, alv, 0.1f, NULL);
            accel = accel * r;
            maxspd = maxspd * r;
        }
        AcCtrl_Accel8Dir(acp, dir, accel, maxspd);
        acp->soundstat |= 1;
    }
    return 0;
}

static int tankac_act_stop(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    AcAnim_ChangeAnim(acp, 0, 0x78, 0);
    tankac_hokan_fix(acp);
    AcCtrl_BrakeXz(acp, acp->param.dash_brake);
    return 0;
}

#ifdef ACNB_SOURCEPORT
int tankac_act_move__dir2animid[9] = {2, 4, 6, 8, 10, 12, 14, 16, 0};
#endif

#ifdef ACNB_SOURCEPORT
/* the original's initialized tables (scripts/sp_tables.py _g_actank_Interface + the flag words from OLSORTIE.BIN) */
struct AcActFuncSt sp_g_actank_Interface[23] __asm__("_g_actank_Interface") = {   /* OLSORTIE 0x1e6a6c0 */
    {0, (int (*)(struct AcPlayerSt *, unsigned short))tankac_act_stop, 0, tankac_setact_move, ACsetact_gndturn,
     tankac_setact_jump_or_fly, ACsetact_jumpcheck, acact_SetGndBlade, ACsetact_shield, ACsetact_stop_nokey},
    {0, tankac_act_move, 0, 0, ACsetact_gndturn, tankac_setact_jump_or_fly, 0, acact_SetGndBlade, ACsetact_shield,
     ACsetact_stop},
    {0, (int (*)(struct AcPlayerSt *, unsigned short))ACact_longwait, (int (*)(struct AcPlayerSt *,
     int))ACexitact_longwait, ACsetact_stop, ACsetact_gndturn, tankac_setact_jump_or_fly, 0, acact_SetGndBlade,
     ACsetact_shield, 0},
    {0, tankac_act_move, 0, 0, ACsetact_stop, tankac_setact_jump_or_fly, 0, acact_SetGndBlade, ACsetact_shield,
     ACsetact_stop},
    {0, (int (*)(struct AcPlayerSt *, unsigned short))ACact_turn, 0, tankac_setact_move, 0, 0, 0,
     acact_SetGndBlade, ACsetact_shield, ACsetact_stop},
    {0, tankac_act_move, 0, 0, 0, 0, 0, acact_SetGndBlade, ACsetact_shield, 0},
    {0, (int (*)(struct AcPlayerSt *, unsigned short))ACact_dashbrake, 0, 0, 0, 0, 0, acact_SetGndBlade,
     ACsetact_shield, 0},
    {0, (int (*)(struct AcPlayerSt *, unsigned short))ACact_jumpcharge, 0, 0, 0, 0, 0, ACsetact_jumpcancel_gndblade,
     ACsetact_shield, 0},
    {1, ACact_fly, 0, ACsetact_fly_noboost, ACsetact_fly_noboost, ACsetact_fly_boost, 0, acact_SetFlyBlade,
     ACsetact_shield, ACsetact_fly_noboost},
    {0, (int (*)(struct AcPlayerSt *, unsigned short))ACact_land, 0, tankac_setact_move, 0,
     tankac_setact_jump_or_fly, ACsetact_jumpcheck, acact_SetGndBlade, ACsetact_shield, 0},
    {0, (int (*)(struct AcPlayerSt *, unsigned short))ACact_dmgland, 0, 0, 0, tankac_setact_jump_or_fly,
     ACsetact_jumpcheck, 0, 0, 0},
    {0, acact_ActGndBlade, acact_ExitBlade, 0, 0, 0, 0, acact_SetGndWblade, 0, 0},
    {0, acact_ActBladeFreeze, acact_ExitBlade, tankac_setact_move, ACsetact_gndturn, tankac_setact_jump_or_fly,
     ACsetact_jumpcheck, 0, 0, 0},
    {1, acact_ActFlyBlade, acact_ExitBlade, 0, 0, 0, 0, acact_SetFlyWblade, 0, 0},
    {0, acact_ActGndWblade, acact_ExitBlade, 0, 0, 0, 0, acact_SetGndWblade, 0, 0},
    {1, acact_ActFlyWblade, acact_ExitBlade, 0, 0, 0, 0, acact_SetFlyWblade, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},   /* no cannon_set / cannon_reset for a tank */
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, acact_ActOb, (int (*)(struct AcPlayerSt *, int))acact_ExitOb, 0, 0, acact_BoostOb, 0, ACsetact_Obooblade,
     ACsetact_shield, 0},
    {0, (int (*)(struct AcPlayerSt *, unsigned short))ACact_gnddamage, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, (int (*)(struct AcPlayerSt *, unsigned short))ACact_airdamage, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, (int (*)(struct AcPlayerSt *, unsigned short))ACact_dead, 0, 0, 0, 0, 0, 0, 0, 0},
    {-1, (int (*)(struct AcPlayerSt *, unsigned short))AcAct_ActChangeSpare, (int (*)(struct AcPlayerSt *,
     int))AcAct_ExitChangeSpare, 0, 0, 0, 0, 0, 0, 0},
};
#endif
