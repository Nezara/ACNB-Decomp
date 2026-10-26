/* src/ac/parts_4leg.cpp - quadruped legs (whole file, OLSORTIE overlay; prog_no 2). ac4leg_Control (acp->managefunc)
 * tilts the body to the ground every frame - four foot points probed with DF_HIT_HitProc_YUKA, the plane normal of
 * the highest three blended into acp->tank_norm, the waist object's world matrix rebuilt from it - and sets the
 * footstep effect flags. leg4ac_put_animSE plays the footstep SE on animation key frames (also used by ACact_turn);
 * the leg4ac_* statics are the action handlers reached through the interpreted table g_ac4leg_Interface. The
 * action / footstep bodies are byte-identical to parts_2leg.cpp's apart from the animation tables and the SE. */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"

/* ground normal (op[13] = body space) from the probed foot points; 0 if the highest point is more than 6 above loc */
static int leg4ac_getnorm(float *norm, AcPlayerSt *acp, float (*wvec)[4])
{
    PS2_FRAME(0x90);
    int best = 0;
    for (int i = 1; i < 4; i++)
        if (wvec[best][1] < wvec[i][1]) best = i;
    if (!(wvec[best][1] - acp->loc[1] <= 6.0f)) return 0;
    float mid[4], a[4], b[4];
    mid[0] = wvec[2][0] + wvec[3][0]; mid[1] = wvec[2][1] + wvec[3][1]; mid[2] = wvec[2][2] + wvec[3][2];
    mid[0] = mid[0] * 0.5f; mid[1] = mid[1] * 0.5f; mid[2] = mid[2] * 0.5f;   /* vmulx.xyz by vf31 = 0.5 */
    a[0] = wvec[0][0] - mid[0]; a[1] = wvec[0][1] - mid[1]; a[2] = wvec[0][2] - mid[2];
    b[0] = wvec[1][0] - mid[0]; b[1] = wvec[1][1] - mid[1]; b[2] = wvec[1][2] - mid[2];
    vu_cross(norm, a, b);
    vu_normalize(norm, norm);
    float t[4][4] __attribute__((aligned(16)));
    sceVu0TransposeMatrix(t, *acp->op[13].wrmatp);
    vu_apply(norm, t, norm);
    return 1;
}

static void leg4ac_norm_update(AcPlayerSt *acp)
{
    PS2_FRAME(0x1f0);
    float wvec[4][4] __attribute__((aligned(16)));
    float norm[4] __attribute__((aligned(16)));
    float t[4][4] __attribute__((aligned(16)));
    float z[4][4] __attribute__((aligned(16)));
    float m[4][4] __attribute__((aligned(16)));
    float zv[4] __attribute__((aligned(16)));
    for (int i = 0; i < 4; i++) {
        float pos[4] __attribute__((aligned(16)));
        vu_apply(pos, *acp->op[14].wrmatp, acp->tank_verts[i]);
        pos[1] = pos[1] - acp->tall;
        *(unsigned char *)PS2_SPR(0x67) |= 1;   /* hit: keep the parent fallback */
        if (!DF_HIT_HitProc_YUKA(acp->mp, pos, 15.0f + acp->tall, 0.001f, wvec[i])) {
            wvec[i][0] = pos[0];
            wvec[i][1] = 3.4028235e38f;   /* FLT_MAX */
            wvec[i][2] = pos[2];
            wvec[i][3] = 1.0f;
        }
    }
    if (!leg4ac_getnorm(norm, acp, wvec)) {
        sceVu0TransposeMatrix(t, *acp->op[13].wrmatp);
        vu_apply(norm, t, (*acp->op[14].wrmatp)[1]);
    }
    norm_interpolate(acp->tank_norm, norm, 0.15f);
    sceVu0TransposeMatrix(t, *acp->op[13].wrmatp);
    vu_apply(zv, t, (*acp->op[14].wrmatp)[2]);
    GAcUtil_ZYvec2matrix(z, zv, acp->tank_norm);   /* row 3 of z stays unset: only rows 0-2 of m are used */
    vu_mul_matrix(m, *acp->op[13].wrmatp, z);
    memcpy(*acp->op[14].wrmatp, m, 0x30);
    Anim_UpdateChildObjMatrix(&acp->op[15], *acp->op[14].wrmatp);
}

/* footstep SE on the key frames of the semng object's animation; 1 if one was played */
int leg4ac_put_animSE(AcPlayerSt *acp, int seid, int vol, float dist)
{
    PS2_FRAME(0x40);
    unsigned int id = acp->semng_objid;
    ObjectSt *op = &acp->op[id];
    StMixAnimationTable *tb = &acp->mp->tod->animtable[op->todid][id];
    int n = tb->nkey_tod - 1;
    for (int k = 1; k < n; k++) {
        if (op->frame == tb->tod_data[k].key) {
            acse_PutSeVolDist(acp, seid, vol, dist);
            acp->effectstat |= (tb->tod_data[k].position[0] <= 0.0f) ? 0x100 : 0x80;
            acp->soundstat |= 0x10;
            return 1;
        }
    }
    return 0;
}

/* footstep effect: 0x1000 = moving, 0x2000 = turning on the spot (skipped while jumping / boosting) */
static void leg4ac_set_footefect(AcPlayerSt *acp)
{
    if (acp->actno == 1 || acp->actno == 3 || (acp->effectstat & 0x183)) return;
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

void ac4leg_Control(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    leg4ac_norm_update(acp);
    leg4ac_set_footefect(acp);
}

static int leg4ac_setact_1ststep(AcPlayerSt *acp)
{
    PS2_FRAME(0x10);
    AC_SetAction(acp, 1);
    return 1;
}

static int leg4ac_act_walk(AcPlayerSt *acp, unsigned short dir)
{
    PS2_FRAME(0x50);
    int slow = 0;
    const unsigned char *tab = leg4ac_act_walk__dir2animid_tab[0];
    if (AcUidev_IsAnalog(acp->frdp->uidevp)) {
        char alh = AcUidev_GetPadStickConf(acp->frdp->uidevp, 0);
        char alv = AcUidev_GetPadStickConf(acp->frdp->uidevp, 1);
        slow = AcUidev_IsAnalogSlowlyLeg(dir, alh, alv);
    }
    if (acp->param.leg_ow_rate <= g_asmparam.ow_trekanim_threshold || slow)
        tab = leg4ac_act_walk__dir2animid_tab[1];
    if (dir != 8) acp->soundstat |= 1;
    unsigned char anim = tab[dir];
    unsigned char old = acp->op[14].todid;
    AcAnim_ChangeAnimFrameSame(acp, anim, 0x14, 1);
    if (anim != old || acp->op[14].hokantime != acp->param.hokanparamp->walk_waist) {
        acp->op[0].hokantime = acp->param.hokanparamp->walk_core;
        acp->op[3].hokantime = acp->param.hokanparamp->walk_core;
        acp->op[14].hokantime = acp->param.hokanparamp->walk_waist;
    }
    AcCtrl_TurnPad(acp, acp->param.turn_accel, acp->param.turn_maxspd);
    AcCtrl_WalkAsParameter(acp, dir, 1.0f);
    leg4ac_put_animSE(acp, 0x39, AC_SEparam.walk_vol_4legs, (float)AC_SEparam.walk_dist_4legs);   /* inlined */
    return 1;
}

static int ACsetact_jump_or_dash(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    if (ACsetact_jumpcheck(acp)) return 1;
    if (AcUidev_IsMoving(acp->frdp->uidevp) && !AcCtrl_IsCharging(acp)) {
        AC_SetAction(acp, 5);
        return 1;
    }
    return 0;
}

static int leg4ac_act_stop2walk_nokey(AcPlayerSt *acp)
{
    PS2_FRAME(0x10);
    AC_SetAction(acp, acp->nextact ? acp->nextact : 0);
    return 1;
}

static int leg4ac_setact_stop2walk2dash(AcPlayerSt *acp)
{
    acp->nextact = 5;
    return 1;
}

static int leg4ac_act_stop2walk(AcPlayerSt *acp, unsigned short dir)
{
    PS2_FRAME(0x50);
    const unsigned char *tab = leg4ac_act_stop2walk__dir2animid_tab[0];
    if (acp->param.leg_ow_rate <= g_asmparam.ow_trekanim_threshold) tab = leg4ac_act_stop2walk__dir2animid_tab[1];
    unsigned char old = acp->op[14].todid;
    unsigned char anim = tab[dir];
    if (dir != 8) acp->soundstat |= 1;
    acp->key_enable = 0;
    if (acp->actstep == 0) {
        acp->frdp->actp->idAnim = anim;
        AcAnim_ChangeAnim(acp, anim, 8, 0);
        acp->actstep++;
        return 1;
    }
    AcAnim_ChangeAnimFrame(acp, anim, acp->acttime, 0xf, 0);
    if (anim != old || acp->op[14].hokantime != acp->param.hokanparamp->walk_waist) {
        acp->op[0].hokantime = acp->param.hokanparamp->walk_core;
        acp->op[3].hokantime = acp->param.hokanparamp->walk_core;
        acp->op[14].hokantime = acp->param.hokanparamp->walk_waist;
    }
    if (acp->acttime >= 7) acp->key_enable = 1;
    if (acp->acttime < acp->op[14].nframe - 1) AcCtrl_WalkAsParameter(acp, dir, 0.5f);
    else AC_SetAction(acp, acp->nextact ? acp->nextact : 3);
    return 1;
}

#ifdef ACNB_SOURCEPORT
/* the original's initialized tables (generated by scripts/sp_datagen.py from the PS2 image) */
struct AcActFuncSt sp_g_ac4leg_Interface[23] __asm__("_g_ac4leg_Interface") = {   /* OLSORTIE 0x1e69e00 */
    {0, (int (*)(struct AcPlayerSt *, unsigned short))ACact_stop, 0, leg4ac_setact_1ststep, ACsetact_gndturn,
     ACsetact_jump_or_dash, ACsetact_jumpcheck, acact_SetGndBlade, ACsetact_shield, ACsetact_stop_nokey},
    {0, leg4ac_act_stop2walk, 0, 0, ACsetact_gndturn, leg4ac_setact_stop2walk2dash, 0, acact_SetGndBlade,
     ACsetact_shield, leg4ac_act_stop2walk_nokey},
    {0, (int (*)(struct AcPlayerSt *, unsigned short))ACact_longwait, (int (*)(struct AcPlayerSt *,
     int))ACexitact_longwait, ACsetact_stop, ACsetact_gndturn, ACsetact_jump_or_dash, 0, acact_SetGndBlade,
     ACsetact_shield, 0},
    {0, leg4ac_act_walk, 0, 0, ACsetact_stop, ACsetact_dash, 0, acact_SetGndBlade, ACsetact_shield, ACsetact_stop},
    {0, (int (*)(struct AcPlayerSt *, unsigned short))ACact_turn, 0, leg4ac_setact_1ststep, 0, 0, 0,
     acact_SetGndBlade, ACsetact_shield, ACsetact_stop},
    {0, ACact_dash, 0, ACsetact_dash_noboost, ACsetact_dash_noboost, ACsetact_dash_boost, 0, acact_SetGndBlade,
     ACsetact_shield, ACsetact_dash_noboost},
    {0, (int (*)(struct AcPlayerSt *, unsigned short))ACact_dashbrake, 0, 0, 0, 0, 0, acact_SetGndBlade,
     ACsetact_shield, 0},
    {0, (int (*)(struct AcPlayerSt *, unsigned short))ACact_jumpcharge, 0, 0, 0, 0, 0, ACsetact_jumpcancel_gndblade,
     ACsetact_shield, 0},
    {1, ACact_fly, 0, ACsetact_fly_noboost, ACsetact_fly_noboost, ACsetact_fly_boost, 0, acact_SetFlyBlade,
     ACsetact_shield, ACsetact_fly_noboost},
    {0, (int (*)(struct AcPlayerSt *, unsigned short))ACact_land, 0, ACsetact_walk, 0, 0, 0, 0, 0, 0},
    {0, (int (*)(struct AcPlayerSt *, unsigned short))ACact_dmgland, 0, 0, 0, ACsetact_jump_or_dash,
     ACsetact_jumpcheck, 0, 0, 0},
    {0, acact_ActGndBlade, acact_ExitBlade, 0, 0, 0, 0, acact_SetGndWblade, 0, 0},
    {0, acact_ActBladeFreeze, acact_ExitBlade, leg4ac_setact_1ststep, ACsetact_gndturn, ACsetact_jump_or_dash,
     ACsetact_jumpcheck, 0, 0, 0},
    {1, acact_ActFlyBlade, acact_ExitBlade, 0, 0, 0, 0, acact_SetFlyWblade, 0, 0},
    {0, acact_ActGndWblade, acact_ExitBlade, 0, 0, 0, 0, acact_SetGndWblade, 0, 0},
    {1, acact_ActFlyWblade, acact_ExitBlade, 0, 0, 0, 0, acact_SetFlyWblade, 0, 0},
    {0, (int (*)(struct AcPlayerSt *, unsigned short))ACact_cannon_set, 0, 0, 0, 0, 0, 0, ACsetact_shield, 0},
    {0, (int (*)(struct AcPlayerSt *, unsigned short))ACact_cannon_reset, 0, 0, 0, 0, 0, 0, ACsetact_shield, 0},
    {1, acact_ActOb, (int (*)(struct AcPlayerSt *, int))acact_ExitOb, 0, 0, acact_BoostOb, 0, ACsetact_Obooblade,
     ACsetact_shield, 0},
    {0, (int (*)(struct AcPlayerSt *, unsigned short))ACact_gnddamage, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, (int (*)(struct AcPlayerSt *, unsigned short))ACact_airdamage, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, (int (*)(struct AcPlayerSt *, unsigned short))ACact_dead, 0, 0, 0, 0, 0, 0, 0, 0},
    {-1, (int (*)(struct AcPlayerSt *, unsigned short))AcAct_ActChangeSpare, (int (*)(struct AcPlayerSt *,
     int))AcAct_ExitChangeSpare, 0, 0, 0, 0, 0, 0, 0},
};
#endif
