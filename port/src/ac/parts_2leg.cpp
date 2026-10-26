/* src/ac/parts_2leg.cpp - bipedal legs (whole file, OLSORTIE overlay; prog_no 0, the default leg type).
 * ac2leg_Control (acp->managefunc) only sets the footstep effect flags (turning / moving on the spot); the leg2ac_*
 * functions are the leg-type action handlers (walk, stop -> walk, first step, jump / dash) reached through the
 * interpreted action table g_ac2leg_Interface. The bodies are byte-identical to parts_4leg.cpp's apart from the
 * animation tables and the footstep SE. */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

/* footstep effect: 0x1000 = moving, 0x2000 = turning on the spot (skipped while jumping / boosting) */
static void leg2ac_set_footefect(AcPlayerSt *acp)
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

void ac2leg_Control(AcPlayerSt *acp)
{
    leg2ac_set_footefect(acp);
}

static int leg2ac_setact_1ststep(AcPlayerSt *acp)
{
    PS2_FRAME(0x10);
    AC_SetAction(acp, 1);
    return 1;
}

static int leg2ac_act_walk(AcPlayerSt *acp, unsigned short dir)
{
    PS2_FRAME(0x50);
    int slow = 0;
    const unsigned char *tab = leg2ac_act_walk__dir2animid_tab[0];
    if (AcUidev_IsAnalog(acp->frdp->uidevp)) {
        char alh = AcUidev_GetPadStickConf(acp->frdp->uidevp, 0);
        char alv = AcUidev_GetPadStickConf(acp->frdp->uidevp, 1);
        slow = AcUidev_IsAnalogSlowlyLeg(dir, alh, alv);
    }
    if (acp->param.leg_ow_rate <= g_asmparam.ow_trekanim_threshold || slow)
        tab = leg2ac_act_walk__dir2animid_tab[1];
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
    /* footstep SE on the animation key frames (leg4ac_put_animSE inlined) */
    unsigned int id = acp->semng_objid;
    ObjectSt *op = &acp->op[id];
    StMixAnimationTable *t = &acp->mp->tod->animtable[op->todid][id];
    int n = t->nkey_tod - 1;
    for (int k = 1; k < n; k++) {
        if (op->frame == t->tod_data[k].key) {
            acse_PutSeVolDist(acp, 1, AC_SEparam.walk_vol, (float)AC_SEparam.walk_dist);
            acp->effectstat |= (t->tod_data[k].position[0] <= 0.0f) ? 0x100 : 0x80;
            acp->soundstat |= 0x10;
            break;
        }
    }
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

static int leg2ac_act_stop2walk_nokey(AcPlayerSt *acp)
{
    PS2_FRAME(0x10);
    AC_SetAction(acp, acp->nextact ? acp->nextact : 0);
    return 1;
}

static int leg2ac_setact_stop2walk2dash(AcPlayerSt *acp)
{
    acp->nextact = 5;
    return 1;
}

static int leg2ac_act_stop2walk(AcPlayerSt *acp, unsigned short dir)
{
    PS2_FRAME(0x50);
    const unsigned char *tab = leg2ac_act_stop2walk__dir2animid_tab[0];
    if (acp->param.leg_ow_rate <= g_asmparam.ow_trekanim_threshold) tab = leg2ac_act_stop2walk__dir2animid_tab[1];
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
struct AcActFuncSt sp_g_ac2leg_Interface[23] __asm__("_g_ac2leg_Interface") = {   /* OLSORTIE 0x1e69a20 */
    {0, (int (*)(struct AcPlayerSt *, unsigned short))ACact_stop, 0, leg2ac_setact_1ststep, ACsetact_gndturn,
     ACsetact_jump_or_dash, ACsetact_jumpcheck, acact_SetGndBlade, ACsetact_shield, ACsetact_stop_nokey},
    {0, leg2ac_act_stop2walk, 0, 0, ACsetact_gndturn, leg2ac_setact_stop2walk2dash, 0, acact_SetGndBlade,
     ACsetact_shield, leg2ac_act_stop2walk_nokey},
    {0, (int (*)(struct AcPlayerSt *, unsigned short))ACact_longwait, (int (*)(struct AcPlayerSt *,
     int))ACexitact_longwait, ACsetact_stop, ACsetact_gndturn, ACsetact_jump_or_dash, 0, acact_SetGndBlade,
     ACsetact_shield, 0},
    {0, leg2ac_act_walk, 0, 0, ACsetact_stop, ACsetact_dash, 0, acact_SetGndBlade, ACsetact_shield, ACsetact_stop},
    {0, (int (*)(struct AcPlayerSt *, unsigned short))ACact_turn, 0, leg2ac_setact_1ststep, 0, 0, 0,
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
    {0, acact_ActBladeFreeze, acact_ExitBlade, leg2ac_setact_1ststep, ACsetact_gndturn, ACsetact_jump_or_dash,
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
