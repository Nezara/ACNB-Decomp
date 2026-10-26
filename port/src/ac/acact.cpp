/* src/ac/acact.cpp - per-AC action state machine (stand, walk, turn, dash, dash brake, jump charge, fly, land, damage,
 * dead, blade actions, overboost, cannon set / reset, spare-part change). The current action is an entry of one of
 * four per-leg-type tables of AcActFuncSt (in the overlay's leg files): each frame AcAct_Exec reads the pad direction
 * and runs the entry's act function, then __GetNextAcAct looks at the buttons and calls the entry's setact function
 * for the next action (move / turn / boost / jump / blade / shield / neutral slots). This file holds the
 * leg-independent table entries (ACact_* per-frame acts, ACsetact_* transitions, acact_* blade / overboost / damage
 * helpers) and the dispatcher. */
#include "acnb/ps2fp.h"

#ifdef ACNB_SOURCEPORT
int ACact_dash__dir2animid[9] = {0x11, 0x13, 0x14, 0x15, 0x16, 0x12, 0x17, 0x18, 0x11};
int ACact_fly__dir2animid[9] = {0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x25};
unsigned short acact_ActOb__dir2animid[9] = {0x3f, 0x40, 0x41, 0x40, 0x41, 0x3f, 0x40, 0x41, 0x3f};
unsigned short acact_ActOb__toOBdirtbl[9] = {0, 3, 4, 3, 4, 0, 3, 4, 0};
#endif

/* cvt.w.s of a float that MWCC converts to unsigned (values >= 2^31 go through the subtract-and-or path) */
static inline unsigned int acact_ftou(float f)
{
    if (f >= 2147483648.0f) return (unsigned int)ps2_ftoi(f - 2147483648.0f) | 0x80000000u;
    return (unsigned int)ps2_ftoi(f);
}

/* switch to action n (MWCC inlines AC_SetAction with a constant into ~25 functions): the old entry's exit function
 * runs first (it still sees the old actno), then the entry, counters, act function and (unless -1) actstat */
static void acact_setact(AcPlayerSt *acp, int n)
{
    AcActPrivateSt *p = AcCtrl_GetAct(acp)->prvp;
    if (p->cur_actfuncp->exit) gcall(p->cur_actfuncp->exit, acp, n);
    p->cur_actfuncp = p->actfuncp + n;
    acp->actno = (unsigned short)n;
    acp->nextact = 0;
    acp->actstep = 0;
    acp->acttime = 0;
    acp->actfunc = (decltype(acp->actfunc))p->cur_actfuncp->act;
    if (p->cur_actfuncp->actstat != -1) acp->actstat = (unsigned short)p->cur_actfuncp->actstat;
}

/* re-equip the weapon released for the action (pad on, weapon released) */
static inline void acact_reequip(AcPlayerSt *acp)
{
    if (MyI.padoff != 1 && acp->wep_release != 0) AcWep_EquipWeapon(acp, acp->equip_weapon);
}

int acact_ExitOb(AcPlayerSt *acp)
{
    PS2_FRAME(0x10);
    acact_reequip(acp);
    return 1;
}

int acact_BoostOb(AcPlayerSt *acp)
{
    PS2_FRAME(0x10);
    AcCtrl_DispLegBoostFire(acp);
    return 1;
}

/* overboost: launch speed on the first frame, then acceleration, in the pad direction mapped to forward / sideways */
int acact_ActOb(AcPlayerSt *acp, unsigned short dir)
{
    PS2_FRAME(0x40);
    float accel;
    if (acp->actstep == 0) {
        accel = acp->param.oboost_spd0;
        acp->actstep = acp->actstep + 1;
    } else {
        accel = acp->param.oboost_acc;
    }
    AcCtrl_TurnPad(acp, acp->param.turn_accel, acp->param.turn_maxspd);
    AcAnim_ChangeAnim(acp, acact_ActOb__dir2animid[dir], 0x12c, 1);
    AcCtrl_Accel8Dir(acp, acact_ActOb__toOBdirtbl[dir], accel, acp->param.oboost_maxspd);
    if (AcUidev_IsPadOnConf(acp->frdp->uidevp, 5)) AddF_min(&acp->velocity[1], -acp->param.flyup_accel, -acp->param.flyup_maxspd);
    else AcCtrl_AccelFall(acp, acp->param.gravity, acp->param.fall_maxspd);
    return 1;
}

/* blade actions: tail calls into the blade part (no frame) */
int acact_ActBladeFreeze(AcPlayerSt *acp, unsigned short dir) { (void)dir; return acblade_ActBladeFreeze(acp); }

/* leaving a blade action: drop the blade's hit collision, end the blade action, re-equip (not into blade freeze) */
int acact_ExitBlade(AcPlayerSt *acp, int nextactno)
{
    PS2_FRAME(0x40);
    struct_3956eba2 *b = aceff_GetBlade(AcCtrl_GetEffect(acp), 0);
    if (b->hcoll >= 0) {
        DF_COLL_Delete(b->hcoll);
        b->hcoll = -1;
    }
    AcBlade_ExitAct(acp);
    if (nextactno != 0xc) acact_reequip(acp);
    return 1;
}

int acact_ActFlyWblade(AcPlayerSt *acp, unsigned short dir) { (void)dir; return acblade_ActFlyBlade(acp); }
int acact_ActFlyBlade(AcPlayerSt *acp, unsigned short dir) { (void)dir; return acblade_ActFlyBlade(acp); }
int acact_ActGndWblade(AcPlayerSt *acp, unsigned short dir) { (void)dir; return acblade_ActGndBlade(acp); }
int acact_ActGndBlade(AcPlayerSt *acp, unsigned short dir) { (void)dir; return acblade_ActGndBlade(acp); }
int acact_SetFlyWblade(AcPlayerSt *acp) { return acblade_ActSetFlyWblade(acp); }
int acact_SetFlyBlade(AcPlayerSt *acp) { return acblade_ActSetFlyBlade(acp); }
int acact_SetGndWblade(AcPlayerSt *acp) { return acblade_ActSetGndWblade(acp); }
int acact_SetGndBlade(AcPlayerSt *acp) { return acblade_ActSetGndBlade(acp); }

/* blade from overboost: ground or air blade by the height above the floor */
int ACsetact_Obooblade(AcPlayerSt *acp)
{
    PS2_FRAME(0x10);
    if (acp->floor_high - acp->loc[1] < 0.1f) acblade_ActSetGndBlade(acp);
    else acblade_ActSetFlyBlade(acp);
    return 1;
}

int acact_SetAirDamage(AcPlayerSt *acp, int animid, int shocktime)
{
    PS2_FRAME(0x40);
    AcAnim_ChangeAnim(acp, animid, acp->param.hokanparamp->airdamage, 0);
    acact_setact(acp, 0x14);
    acp->freezetime = (short)shocktime;
    return 1;
}

int acact_SetGndDamage(AcPlayerSt *acp, int animid, float *reactvec, int shocktime)
{
    PS2_FRAME(0x40);
    acp->velocity[0] = reactvec[0];
    acp->velocity[1] = 0.0f;
    acp->velocity[2] = reactvec[2];
    AcCtrl_BrakeXz(acp, 0.08f);
    AcAnim_ChangeAnim(acp, animid, acp->param.hokanparamp->gnddamage, 0);
    acact_setact(acp, 0x13);
    acp->freezetime = (short)shocktime;
    return 1;
}

/* shield off (shieldstat is cleared before the anim group switch) */
static void acact_shield_off(AcPlayerSt *acp)
{
    acp->shieldstat = 0;
    acshield_SetAnimGroup(acp, 0);
    AcAnim_SynchroAnim(acp, 1, 8, 0x3c);
}

int ACsetact_shieldoff(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    if (acp->shieldstat) acact_shield_off(acp);
    if (acp->param.shield_sub > 0) acse_StopLoopSe(acp, 3, 1);
    return 1;
}

/* shield toggle; switching on needs the shield's energy (more than its cost, not while charging) */
static void acact_shield_toggle(AcPlayerSt *acp)
{
    if (acp->shieldstat != 1) {
        int sub = acp->param.shield_sub;
        if (sub != 0) {
            if (AcCtrl_IsCharging(acp)) return;
            sub = acp->param.shield_sub;
            if (!(sub < acp->energyReal)) return;
        }
        if (sub > 0) acse_PutLoopSeVolToneDist(acp, 3, 0x21, AC_SEparam.shield_vol, 0, (float)AC_SEparam.shield_dist);
        acp->shieldstat = 1;
        acshield_SetAnimGroup(acp, 1);
        AcAnim_ChangeAnimGroup(acp, 8, 0x3d, 0x3c, 0);
    } else {
        acact_shield_off(acp);
        if (acp->param.shield_sub > 0) acse_StopLoopSe(acp, 3, 1);
    }
}

int ACsetact_shield(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    acact_shield_toggle(acp);
    return 1;
}

int ACsetact_dash(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    if (!AcCtrl_IsCharging(acp) && AcAnim_GetDashAnimId(acp) != 0) acact_setact(acp, 5);
    else acact_setact(acp, 0);
    return 1;
}

int ACsetact_jumpcheck(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    if (AcUidev_IsMoving(acp->frdp->uidevp) || !acp->param.abl_jump) return 0;
    acact_setact(acp, 7);
    return 1;
}

int acact_SetDamageLand(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    acp->velocity[0] = acp->velocity[0] * 0.3f;
    acp->velocity[2] = acp->velocity[2] * 0.3f;
    if (acp->floor_high < 0.1f + acp->loc[1] && !acp->dead)
        acse_PutSeVolDist(acp, 5, AC_SEparam.dmgland_vol, (float)AC_SEparam.dmgland_dist);
    acact_setact(acp, 10);
    return 1;
}

/* landing: sound by the fall speed (on the floor, alive), horizontal damping after a fast fall */
int acact_SetLand(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    if (acp->floor_high < 0.1f + acp->loc[1] && !acp->dead) {
        float r = ps2_div(2.0f * acp->velocity[1], acp->param.fall_maxspd);
        int vol = ps2_ftoi((float)AC_SEparam.land_vol * r);
        acse_PutSeVolDist(acp, 4, vol, (float)AC_SEparam.land_dist);
    }
    if (!(acp->velocity[1] <= acp->param.fall_maxspd / 3.0f)) {
        acp->velocity[0] = acp->velocity[0] * 0.7f;
        acp->velocity[2] = acp->velocity[2] * 0.7f;
    }
    acact_setact(acp, 9);
    return 1;
}

int ACsetact_gndturn(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    acact_setact(acp, 4);
    return 1;
}

int ACsetact_walk(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    acact_setact(acp, 3);
    return 1;
}

int ACsetact_dead(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    AC_SetControl(acp, 0);
    unsigned int n = acp->actno;
    if (n - 0x10 < 2 || (n == 2 && acp->param.prog_no == 3)) AcAnim_ChangeAnimFrameLast(acp, 0x30, 0x258);
    else AcAnim_ChangeAnim(acp, 0x30, 0xb4, 0);
    acact_setact(acp, 0x15);
    return 1;
}

int ACexitact_longwait(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    acse_StopLoopSe(acp, 4, 1);
    acact_reequip(acp);
    return 1;
}

int ACsetact_stop(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    acact_setact(acp, 0);
    return 1;
}

/* end of the spare change: hand / back groups, the spare comes in */
int AcAct_ExitChangeSpare(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    if (acp->reserve_spare == 2) Anim_MakeGroups(acp->mp, 5, 8, 9);
    else Anim_MakeGroups(acp->mp, 9, 0xc, 5);
    AcWep_ChangeWeapon(acp, acp->reserve_spare);
    acp->reserve_spare = 0;
    return 1;
}

int AcAct_SetChangeSpare(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    if (acp->actno == 0x12) acob_Close(AcCtrl_GetPartsOb(acp));
    else if (acp->actno == 0x10) AcWep_ReleaseWeapon(acp, acp->equip_weapon);
    acact_setact(acp, 0x16);
    return 1;
}

/* spare change: brake (with a dash brake on the ground), play the change animation of the hand group, then stand /
 * fly */
int AcAct_ActChangeSpare(AcPlayerSt *acp)
{
    PS2_FRAME(0x40);
    switch (acp->actstep) {
    case 0:
        if (acp->actstat & 1) {
            acp->actstep = 3;
            break;
        }
        AcCtrl_BrakeXz(acp, acp->param.dash_brake);
        if (AcCtrl_IsCharging(acp) || AcAnim_GetDashBrakeAnimId(acp, -1) == -1) {
            AcAnim_ChangeAnim(acp, 0, 0x78, 0);
            acp->actstep = 3;
        } else {
            acp->actstep = 1;
        }
        break;
    case 1: case 2: {
        unsigned short step = (unsigned short)(acp->actstep - 1);
        if (!__DashBrake(acp, &step, &acp->acttime)) acp->actstep = 3;
        else acp->actstep = (unsigned short)(step + 1);
        break;
    }
    case 3: {
        if (!(acp->actstat & 1)) AcCtrl_BrakeXz(acp, acp->param.dash_brake);
        int anim;
        if (acp->reserve_spare == 2) {
            Anim_MakeGroups(acp->mp, 5, 8, 0x20);
            anim = 0x49;
        } else {
            Anim_MakeGroups(acp->mp, 9, 0xc, 0x20);
            anim = 0x4a;
        }
        AcAnim_ChangeAnimGroup(acp, 0x20, anim, 0xa, 0);
        acp->actstep = 4;
        break;
    }
    case 4: {
        int anim = acp->reserve_spare == 2 ? 0x49 : 0x4a;
        if (!(acp->actstat & 1)) {
            AcCtrl_BrakeXz(acp, acp->param.dash_brake);
            if (Anim_IsEndGroupAnim(acp->mp, 5, anim)) acact_setact(acp, 0);
        } else if (Anim_IsEndGroupAnim(acp->mp, 5, anim)) {
            acact_setact(acp, 8);
        }
        break;
    }
    }
    return 1;
}

int ACact_dead(AcPlayerSt *acp)
{
    PS2_FRAME(0x10);
    AcCtrl_BrakeXz(acp, acp->param.dash_brake);
    return 1;
}

/* damage freeze: keys off until the freeze counter runs out, then fly / stand */
int ACact_airdamage(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    AcCtrl_BrakeXz(acp, acp->param.flymv_brk);
    acp->freezetime = (short)(acp->freezetime - 1);
    if (acp->freezetime < 0) acact_setact(acp, 8);
    else acp->key_enable = 0;
    return 1;
}

int ACact_gnddamage(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    AcCtrl_BrakeXz(acp, acp->param.dash_brake);
    acp->freezetime = (short)(acp->freezetime - 1);
    if (acp->freezetime < 0) acact_setact(acp, 0);
    else acp->key_enable = 0;
    return 1;
}

/* the cannon-reset animation matching the current cannon stance of object 1 (no frame, tail call) */
static void ac_set_cannonreset_anim(AcPlayerSt *acp)
{
    int anim;
    switch (acp->mp->op[1].todid) {
    case 0x38: anim = 0x39; break;
    case 0x4b: anim = 0x4c; break;
    case 0x3a: anim = 0x3b; break;
    default: anim = 0; break;
    }
    AcAnim_ChangeAnim(acp, anim, 0x1e, 0);
}

int ACact_cannon_reset(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    AcCtrl_TurnPad(acp, acp->param.turn_accel, acp->param.turn_maxspd);
    if (acp->actstep == 0) {
        ac_set_cannonreset_anim(acp);
        acp->actstep = acp->actstep + 1;
    } else if (Anim_IsEnd(acp->mp)) {
        acact_setact(acp, 0);
    }
    return 1;
}

static int ac_set_cannonset_anim(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    AcAnim_ChangeAnim(acp, 0x4b, 0x1e, 0);
    return Anim_IsEnd(acp->mp);
}

int ACact_cannon_set(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    if (AcCtrl_TurnPad(acp, acp->param.canoturn_accel, acp->param.canoturn_maxspd)) acp->soundstat |= 4;
    AcCtrl_BrakeXz(acp, acp->param.dash_brake);
    if (acp->actstep == 0 && ac_set_cannonset_anim(acp)) acp->actstep = acp->actstep + 1;
    return 1;
}

/* hard landing: vibration, the landing animation, then stand */
int ACact_dmgland(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    AcCtrl_BrakeXz(acp, acp->param.dash_brake);
    acp->key_enable = 0;
    if (acp->acttime >= 0 && acp->acttime < 0xb4) acp->soundstat |= 1;
    switch (acp->actstep) {
    case 0:
        AcCtrl_SetVibration(acp, (unsigned char)acact_ftou(AC_TempParam.dossun_vibrapow), (short)ps2_ftoi(AC_TempParam.dossun_vibratime));
        acp->actstep = acp->actstep + 1;
        /* fall through */
    case 1:
        if (AcAnim_ChangeAnim(acp, 0x33, acp->param.hokanparamp->dmgland, 0)) acp->actstep = acp->actstep + 1;
        break;
    case 2:
        if (Anim_IsEndGroupAnim(acp->mp, 2, 0x33)) acact_setact(acp, 0);
        break;
    }
    return 1;
}

int ACact_land(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    AcCtrl_BrakeXz(acp, acp->param.dash_brake);
    AcCtrl_TurnPad(acp, acp->param.turn_accel, acp->param.turn_maxspd);
    if (acp->acttime >= 0 && acp->acttime < 0x78) acp->soundstat |= 1;
    if (acp->acttime > 0) {
        AcAnim_ChangeAnim(acp, 0x26, acp->param.hokanparamp->land, 0);
        acp->op[3].hokantime = acp->param.hokanparamp->land_core;
        acp->op[1].hokantime = acp->param.hokanparamp->land_center;
        acp->op[14].hokantime = acp->param.hokanparamp->land_waist;
    }
    if (Anim_IsEnd(acp->mp)) {
        acact_setact(acp, 0);
        Info_IncData(1, 10, acp->id, 0);
    }
    return 1;
}

int ACsetact_fly_boost(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    AcCtrl_DispBackBoostFire(acp);
    AcCtrl_DispLegBoostFire(acp);
    AddF_min(&acp->velocity[1], -acp->param.flyup_accel, -acp->param.flyup_maxspd);
    return 1;
}

/* flight: move in the pad direction, or (neutral) turn / boost-up / rise / hover animation and air brake; the jump
 * rise animation holds while the AC still rises from a jump */
int ACact_fly(AcPlayerSt *acp, unsigned short dir)
{
    PS2_FRAME(0x40);
    AcActPrivateSt *p = acp->frdp->actp->prvp;
    if (acp->acttime == 0) p->isJmpRaise = acp->op[14].todid == 0x23;
    AcCtrl_TurnPad(acp, acp->param.turn_accel, acp->param.turn_maxspd);
    int anim;
    if (dir != 8) {
        anim = ACact_fly__dir2animid[dir];
        AcCtrl_Accel8Dir(acp, dir, acp->param.flymv_accel, acp->param.flymv_maxspd);
    } else {
        AcUidevSt *u = acp->frdp->uidevp;
        if (AcUidev_IsPadOnConf(u, 2)) anim = 0x45;
        else if (AcUidev_IsPadOnConf(acp->frdp->uidevp, 3)) anim = 0x46;
        else if ((!AcCtrl_IsCharging(acp) && AcUidev_IsPadOnConf(acp->frdp->uidevp, 5)) || (p->isJmpRaise && acp->velocity[1] <= 0.0f))
            anim = 0x24;
        else anim = 0x25;
        AcCtrl_BrakeXz(acp, acp->param.flymv_brk);
    }
    if (anim != 0x24) p->isJmpRaise = 0;
    if (p->isJmpRaise) AcAnim_ChangeAnim(acp, 0x24, acp->param.hokanparamp->jump, 0);
    else AcAnim_ChangeAnim(acp, anim, 0xb4, 0);
    return 1;
}

/* idling after a long wait (idle sound loop, waist sound on three animation frames) until a key */
int ACact_longwait(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    if (acp->actstep == 0) {
        acse_PutLoopSeVolToneDist(acp, 4, 0x13, AC_SEparam.idling_vol, 0, (float)AC_SEparam.idling_dist);
        acp->actstep = acp->actstep + 1;
    }
    int loop = 0;
    if (acp->param.prog_no != 3) {
        loop = 1;
        int f = acp->op[1].frame;
        if (f == 0x179 || f == 0x10b || f == 0x57)
            acse_PutSeVolDist(acp, 0x14, AC_SEparam.idling_waist_vol, (float)AC_SEparam.idling_waist_dist);
    }
    if (AcUidev_IsPadTrgConf(acp->frdp->uidevp, 4) || AcUidev_IsPadTrgConf(acp->frdp->uidevp, 6) || AcUidev_IsPadTrgConf(acp->frdp->uidevp, 7))
        acact_setact(acp, 0);
    else
        AcAnim_ChangeAnim(acp, 0x2f, 0x1e, loop);
    AcCtrl_BrakeXz(acp, acp->param.dash_brake);
    return 1;
}

int ACsetact_jumpcancel_gndblade(AcPlayerSt *acp)
{
    acp->nextact = 0xb;
    return 1;
}

/* jump charge: crouch animation, then a ground blade (if requested meanwhile) or the jump */
int ACact_jumpcharge(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    AcCtrl_DispLegBoostFire(acp);
    AcCtrl_TurnPad(acp, acp->param.turn_accel, acp->param.turn_maxspd);
    acp->soundstat |= 1;
    if (!AcAnim_ChangeAnim(acp, 0x23, 3, 0)) return 1;
    acp->key_enable = 0;
    if (acp->nextact != 0) {
        acblade_ActSetGndBlade(acp);
    } else if (Anim_IsEnd(acp->mp)) {
        acp->velocity[0] = 0.0f;
        acp->velocity[1] = -acp->param.jumpspd;
        acp->velocity[2] = 0.0f;
        acp->velocity[3] = 1.0f;
        acact_setact(acp, 8);
    }
    return 1;
}

int ACact_dashbrake(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    if (!__DashBrake(acp, &acp->actstep, &acp->acttime)) acact_setact(acp, 0);
    return 1;
}

/* boost key during a dash: jump off the dash (hover legs: fly-up acceleration) */
int ACsetact_dash_boost(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    if (acp->nextact != 6) return 1;
    if (acp->param.prog_no == 3) acp->velocity[1] = -acp->param.flyup_accel;
    else acp->velocity[1] = -acp->param.dashjumpspd;
    acact_setact(acp, 8);
    return 1;
}

int ACsetact_dash_noboost(AcPlayerSt *acp)
{
    acp->nextact = 6;
    return 1;
}

int ACsetact_fly_noboost(AcPlayerSt *acp)
{
    PS2_FRAME(0x10);
    AcCtrl_AccelFall(acp, acp->param.gravity, acp->param.fall_maxspd);
    return 1;
}

/* dash: within the minimum dash time the pad steers (neutral counts as "no key"), afterwards the animation's own
 * direction; after the minimum time (counted from frame 16) it turns into a dash brake */
int ACact_dash(AcPlayerSt *acp, unsigned short dir)
{
    PS2_FRAME(0x40);
    AcActionSt *a = acp->frdp->actp;
    AcCtrl_DispBackBoostFire(acp);
    AcCtrl_DispLegBoostFire(acp);
    AcCtrl_DispFootSpark(acp);
    if (acp->acttime == 0) {
        a->timeNoKey = 0;
        a->timeAuto = 0;
        acp->nextact = 5;
    }
    if (acp->nextact == 5 && (float)a->timeNoKey < acp->param.dash_mintime) {
        if (dir == 8) a->timeNoKey = a->timeNoKey + 1;
        else a->timeNoKey = 0;
    }
    if (dir == 8 || acp->nextact != 5 || !((float)a->timeNoKey < acp->param.dash_mintime))
        dir = (unsigned short)AcAnim_GetAnim2Dir(acp);
    AcAnim_ChangeAnim(acp, ACact_dash__dir2animid[dir], acp->param.hokanparamp->dashstart, 0);
    AcCtrl_TurnPad(acp, acp->param.turn_accel, acp->param.turn_maxspd);
    AcCtrl_Accel8Dir(acp, dir, acp->param.dash_accel, acp->param.dash_maxspd);
    if (acp->nextact == 5 && !AcCtrl_IsCharging(acp) && (float)a->timeNoKey < acp->param.dash_mintime) return 1;
    if (acp->acttime < 0x10) return 1;
    a->timeAuto = a->timeAuto + 1;
    if ((float)a->timeAuto <= acp->param.dash_mintime) return 1;
    acact_setact(acp, 6);
    acp->key_enable = 0;
    Info_IncData(1, 0xb, acp->id, 0);
    return 1;
}

/* turning on the spot (four legs: looped animation with step sounds) */
int ACact_turn(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    AcPartsExtSt *ext = AcCtrl_GetPartsExt(acp);
    if (acp->actstep == 0) {
        if (ext && ext->boostfrm == 0) acp->turnspd = 0.0f;
        acp->actstep = acp->actstep + 1;
    }
    int anim;
    if (AcUidev_IsAnalog(acp->frdp->uidevp)) anim = AcUidev_GetPadStickConf(acp->frdp->uidevp, 2) < 0 ? 0x21 : 0x22;
    else anim = AcUidev_IsPadOnConf(acp->frdp->uidevp, 2) ? 0x21 : 0x22;
    if (acp->param.legsp->type == 2) {
        AcAnim_ChangeAnim(acp, anim, 0x78, 1);
        leg4ac_put_animSE(acp, 0x39, AC_SEparam.walk_vol_4legs, (float)AC_SEparam.walk_dist_4legs);
    } else {
        AcAnim_ChangeAnim(acp, anim, 0x78, 0);
    }
    AcCtrl_TurnPad(acp, acp->param.turn_accel, acp->param.turn_maxspd);
    AcCtrl_BrakeXz(acp, acp->param.dash_brake);
    acp->soundstat |= 2;
    return 1;
}

/* neutral slot: count idle frames (any of these keys resets them; hovering legs over water never idle); after
 * 1801 frames release the weapon and start the long wait */
int ACsetact_stop_nokey(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    if (acp->hp <= 0) return 1;
    AcUidevSt *u = acp->frdp->uidevp;
    if (AcUidev_IsPadOnConf(u, 0xb) || AcUidev_IsPadOnConf(acp->frdp->uidevp, 0xa) || AcUidev_IsPadOnConf(acp->frdp->uidevp, 6) ||
        AcUidev_IsPadOnConf(acp->frdp->uidevp, 7) || AcUidev_IsPadTrgConf(acp->frdp->uidevp, 4) || MyI.padoff == 1) {
        acp->acttime = 0;
        return 1;
    }
    if (acp->param.prog_no == 3 && acp->water_high < 0.001f + acp->loc[1] && !(acp->water_high <= acp->loc[1] - acp->tall)) return 1;
    if (acp->acttime >= 0x709) {
        if (acp->wep_release == 0) AcWep_ReleaseWeapon(acp, acp->equip_weapon);
        acact_setact(acp, 2);
    }
    return 1;
}

int ACact_stop(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    AcAnim_ChangeAnim(acp, 0, 0x78, 0);
    AcCtrl_BrakeXz(acp, acp->param.dash_brake);
    return 1;
}

void AC_SetAction(AcPlayerSt *acp, int actno)
{
    PS2_FRAME(0x40);
    acact_setact(acp, actno);
}

/* one frame: pad direction (with the cutback history), the current act, then the next action; acttime counts
 * frames of an unchanged act / step */
void AcAct_Exec(AcPlayerSt *acp)
{
    PS2_FRAME(0x50);
    AcActPrivateSt *p = AcCtrl_GetAct(acp)->prvp;
    unsigned short step = acp->actstep;
    auto fn = acp->actfunc;
    acp->key_enable = 1;
    if (acp->actfunc) {
        unsigned char dir = (unsigned char)__GetPadDir(acp);
        if (acp->acttime > 0) {
            if (dir == 5 || dir == 0 || dir == 8) {
                if (p->cntCutbackNeutral > 0) {
                    p->cntCutbackNeutral--;
                } else {
                    acp->dirCutbackPrev = p->dirCutbackCur;
                    p->dirCutbackCur = 8;
                }
            } else {
                p->cntCutbackNeutral = 2;
                acp->dirCutbackPrev = p->dirCutbackCur;
                p->dirCutbackCur = dir;
            }
        }
        gcall(acp->actfunc, acp, (int)dir);
    }
    __GetNextAcAct(acp);
    if (acp->actstep == step && acp->actfunc == fn) acp->acttime++;
}

/* the leg type's action table and control function */
int AcAct_Init(AcActionSt *actp, AcPlayerSt *acp)
{
    PS2_FRAME(0x40);
    AcActPrivateSt *p = actp->prvp;
    ZeroMemory8b(actp, 0x10);
    ZeroMemory8b(p, 0xc);
    actp->prvp = p;
    switch (acp->param.prog_no) {
    case 2:
        p->actfuncp = g_ac4leg_Interface;
        acp->managefunc = (decltype(acp->managefunc))GFN(ac4leg_Control);
        break;
    case 3:
        p->actfuncp = g_acnoleg_Interface;
        acp->managefunc = (decltype(acp->managefunc))GFN(acnoleg_Control);
        break;
    case 1:
        p->actfuncp = g_actank_Interface;
        acp->managefunc = (decltype(acp->managefunc))GFN(actank_Control);
        break;
    default:
        p->actfuncp = g_ac2leg_Interface;
        acp->managefunc = (decltype(acp->managefunc))GFN(ac2leg_Control);
        break;
    }
    p->cur_actfuncp = p->actfuncp;
    return 1;
}

/* dash brake: step 0 brakes with the brake animation until the AC stands still (after frame 20), then plays the
 * rest from the start; step 1 waits 17 frames. Returns 0 when finished. */
static int __DashBrake(AcPlayerSt *acp, unsigned short *pstep, int *ptime)
{
    PS2_FRAME(0x60);
    AcCtrl_TurnPad(acp, acp->param.turn_accel, acp->param.turn_maxspd);
    AcCtrl_BrakeXz(acp, acp->param.dash_brake);
    float vx = acp->velocity[0], vz = acp->velocity[2];
    float speed = ps2_sqrt(vx * vx + vz * vz);
    int id = AcAnim_GetDashBrakeAnimId(acp, 0x19);
    switch (*pstep) {
    case 0:
        AcAnim_ChangeAnimFrameStop(acp, id, 0, acp->param.hokanparamp->dashbrake, 0);
        if (*ptime >= 0x15 && speed <= 0.001f) {
            int start = 0x14 - *ptime;   /* always negative here: the animation restarts at frame 0 */
            if (start < 0) start = 0;
            AcAnim_ChangeAnimFrame(acp, id, start, acp->param.hokanparamp->dashbrake, 0);
            *ptime = 0;
            *pstep = (unsigned short)(*pstep + 1);
        } else {
            AcCtrl_DispBackBoostFire(acp);
            AcCtrl_DispFootSpark(acp);
        }
        return 1;
    case 1:
        return *ptime < 0x11;
    }
    return 1;
}

/* buttons: the left arm key (laser gun trigger / shield toggle / blade), else the slot of the pressed movement keys;
 * the setact function runs; then the boost frame counter */
static void __GetNextAcAct(AcPlayerSt *acp)
{
    PS2_FRAME(0x40);
    void *fn = 0;
    if (AcUidev_IsPadTrgConf(acp->frdp->uidevp, 7) && !AcCtrl_PadEvent(acp) && acp->key_enable) {
        AcActFuncSt *cur = AcCtrl_GetAct(acp)->prvp->cur_actfuncp;
        switch (acp->param.Larm_type) {
        case 3:
            if (acp->actno != 0x10) AcLgun_Trigger(AcCtrl_GetPartsLgun(acp), 1);
            fn = 0;
            break;
        case 2:
            if (cur->shield && acp->op[5].group != 0x40) acact_shield_toggle(acp);
            fn = 0;
            break;
        case 0: case 1:
            fn = acblade_IsReady(AcCtrl_GetPartsBlade(acp)) ? (void *)cur->blade : 0;
            break;
        }
    }
    if (acp->key_enable && !fn) fn = __GetActFunc(acp);
    if (fn) gcall_as<int(AcPlayerSt *)>(fn, acp);
    if (AcUidev_IsPadOnConf(acp->frdp->uidevp, 5) && !AcCtrl_IsCharging(acp)) {
        switch (acp->actno) {
        case 5: case 0xf: case 0xd: case 8:
            acp->fcntBoost = acp->fcntBoost + 1;
            break;
        default:
            acp->fcntBoost = 0;
            break;
        }
    } else {
        acp->fcntBoost = 0;
    }
}

/* the setact slot for this frame: boost, jump (standing), move / turn (stick outside -59..59, or the keys), neutral */
static void *__GetActFunc(AcPlayerSt *acp)
{
    PS2_FRAME(0x60);
    AcActFuncSt *cur = AcCtrl_GetAct(acp)->prvp->cur_actfuncp;
    if (cur->boost && AcUidev_IsPadOnConf(acp->frdp->uidevp, 5) && !AcCtrl_IsCharging(acp)) return (void *)cur->boost;
    if (cur->jump && !AcUidev_IsMoving(acp->frdp->uidevp) && AcUidev_IsPadOnConf(acp->frdp->uidevp, 5)) return (void *)cur->jump;
    if (AcUidev_IsAnalog(acp->frdp->uidevp)) {
        signed char sx = AcUidev_GetPadStickConf(acp->frdp->uidevp, 0);
        signed char sy = AcUidev_GetPadStickConf(acp->frdp->uidevp, 1);
        signed char sz = AcUidev_GetPadStickConf(acp->frdp->uidevp, 2);
        AcUidev_GetPadStickConf(acp->frdp->uidevp, 3);
        if (!(sx < 0x3c) || sx < -0x3b || !(sy < 0x3c) || sy < -0x3b) return (void *)cur->move;
        if (!(sz < 0x3c) || sz < -0x3b) return (void *)cur->turn;
        return (void *)cur->neutral;
    }
    if (AcUidev_IsMoving(acp->frdp->uidevp)) return (void *)cur->move;
    if (AcUidev_IsPadOnConf(acp->frdp->uidevp, 2) || AcUidev_IsPadOnConf(acp->frdp->uidevp, 3)) return (void *)cur->turn;
    return (void *)cur->neutral;
}

/* pad direction 0..7 (forward, forward-left, forward-right, left, right, back, back-left, back-right), 8 = neutral */
static int __GetPadDir(AcPlayerSt *acp)
{
    PS2_FRAME(0x40);
    if (AcUidev_IsAnalog(acp->frdp->uidevp)) {
        signed char x = AcUidev_GetPadStickConf(acp->frdp->uidevp, 0);
        signed char y = AcUidev_GetPadStickConf(acp->frdp->uidevp, 1);
        AcUidev_GetPadStickConf(acp->frdp->uidevp, 2);
        if (y < -0x3b && x < -0x3b) return 1;
        if (y < -0x3b && !(x < 0x3c)) return 2;
        if (!(y < 0x3c) && x < -0x3b) return 6;
        if (!(y < 0x3c) && !(x < 0x3c)) return 7;
        if (y < -0x3b) return 0;
        if (!(y < 0x3c)) return 5;
        if (x < -0x3b) return 3;
        return x < 0x3c ? 8 : 4;
    }
    AcUidevSt *u = acp->frdp->uidevp;
    if (AcUidev_IsPadOnConf(u, 0) && AcUidev_IsPadOnConf(acp->frdp->uidevp, 8)) return 1;
    if (AcUidev_IsPadOnConf(acp->frdp->uidevp, 0) && AcUidev_IsPadOnConf(acp->frdp->uidevp, 9)) return 2;
    if (AcUidev_IsPadOnConf(acp->frdp->uidevp, 1) && AcUidev_IsPadOnConf(acp->frdp->uidevp, 8)) return 6;
    if (AcUidev_IsPadOnConf(acp->frdp->uidevp, 1) && AcUidev_IsPadOnConf(acp->frdp->uidevp, 9)) return 7;
    if (AcUidev_IsPadOnConf(acp->frdp->uidevp, 0)) return 0;
    if (AcUidev_IsPadOnConf(acp->frdp->uidevp, 1)) return 5;
    if (AcUidev_IsPadOnConf(acp->frdp->uidevp, 8)) return 3;
    return AcUidev_IsPadOnConf(acp->frdp->uidevp, 9) ? 4 : 8;
}
