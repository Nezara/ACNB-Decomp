/* src/ac/acasm.cpp - AC "assembly": turns an AC's parts list (AcAssemblyPartsSt + AcPartsDataSt) into the
 * AcPlayerParamSt it plays with: part pointers, weight / overweight rates (loadage table), boost / turn / jump /
 * brake speeds, heat, defenses, radar, the weapon slots and the abilities. Every entry point builds a StAcAsm
 * (pparam + option-part multipliers) on its stack and runs the static workers. Tunables: g_asmparam.
 *
 * Every entry point keeps its StAcAsm at the original PS2 stack slot (dead-stack fidelity for the oracle). */
#include "acnb/port.h"
#include "acnb/ps2fp.h"

#ifdef ACNB_SOURCEPORT
/* original initializer (0x25a6f0): 16 x {0, 1} */
float AcAsm_LoadageRatioTable[16][2] = {{0, 1}, {0, 1}, {0, 1}, {0, 1}, {0, 1}, {0, 1}, {0, 1}, {0, 1},
                                     {0, 1}, {0, 1}, {0, 1}, {0, 1}, {0, 1}, {0, 1}, {0, 1}, {0, 1}};
#endif

/* "if (x < min) x = min; if (!(x <= max)) x = max" (c.olt / c.ole: NaN keeps x) */
static inline float acasm_clamp(float x, float min, float max)
{
    if (x < min) x = min;
    if (!(x <= max)) x = max;
    return x;
}

/* AcAsm_LoadageRatioTable lookup (also inlined twice into __GetTotalWeight): linear interpolation between the
 * 16 (ratio, rate) points; every segment is tested (no break); r == t[15][0] matches none and gives 1.0f */
static inline float acasm_ow_rate(float r)
{
    float (*t)[2] = AcAsm_LoadageRatioTable;
    if (r < t[0][0]) return t[0][1];
    if (!(r <= t[15][0])) return t[15][1];
    float res = 1.0f;
    for (int i = 0; i < 15; i++) {
        if (!(r < t[i][0]) && r < t[i + 1][0]) {
            float acc = t[i][1] * (t[i + 1][0] - r);              /* mula.s */
            acc = acc + t[i + 1][1] * (r - t[i][0]);              /* madd.s */
            res = acc / (t[i + 1][0] - t[i][0]);
        }
    }
    return res;
}

/* the brake factor of the overweight rate: 1 -+ (1 - ow)^2, at least ow_leg_brake_rate_min */
static inline float acasm_brake_rate(float ow)
{
    float d = 1.0f - ow;
    d = d * d;
    float b = ow <= 1.0f ? 1.0f - d : 1.0f + d;
    if (b < g_asmparam.ow_leg_brake_rate_min) b = g_asmparam.ow_leg_brake_rate_min;
    return b;
}

float AcAsm_GetJconspdOwRate(AcPlayerParamSt *pparam)
{
    return acasm_clamp(pparam->arm_ow_rate, g_asmparam.ow_jconspeed_rate_min, g_asmparam.ow_jconspeed_rate_max);
}

float AcAsm_GetArmOwRate(AcPlayerParamSt *pparam)
{
    float r = pparam->arm_ow_rate;
    return r < g_asmparam.ow_arm_rate_min ? g_asmparam.ow_arm_rate_min : r;
}

float AcAsm_GetSpeedOwRate(AcPlayerParamSt *pparam)
{
    float r = pparam->leg_ow_rate;
    return r < g_asmparam.ow_leg_speed_rate_min ? g_asmparam.ow_leg_speed_rate_min : r;
}

float AcAsm_GetOwRateFromTable(float loadage_ratio)
{
    return acasm_ow_rate(loadage_ratio);
}

/* turn rates: overweight, the legs' rotation speed (option / plus scaled) against the standard */
static void __GetTurnParam(StAcAsm *this_)
{
    AcPlayerParamSt *pp = this_->pparam;
    float o = 1.0f - 0.5f * (1.0f - pp->leg_ow_rate);
    if (o < g_asmparam.ow_leg_turn_rate_min) o = g_asmparam.ow_leg_turn_rate_min;
    int spd = ps2_ftoi((float)pp->legsp->rot_speed * this_->opt_turn_acc);
    if (pp->plus & 0x80) spd = ps2_ftoi((float)spd * 1.2f);
    float t = (ps2_div((float)spd, g_asmparam.parts_turn_stdspd) - 1.0f) * g_asmparam.turnrate_difhosei + 1.0f;
    t = acasm_clamp(t, g_asmparam.turnrate_min, g_asmparam.turnrate_max);
    float ot = o * t;
    pp->turn_maxspd = g_asmparam.prog_turn_stdspd * ot;
    this_->pparam->canoturn_maxspd = g_asmparam.prog_canoturn_stdspd * ot;
    pp = this_->pparam;
    pp->turn_accel = (pp->turn_maxspd * (float)pp->legsp->rot_accel) / 100.0f;
    pp = this_->pparam;
    pp->canoturn_accel = (pp->canoturn_maxspd * (float)pp->legsp->rot_accel2) / 100.0f;
}

/* boost / dash / brake / walk speeds from weight, overweight, boost power and leg speed */
static void __GetBoostMoveParam(StAcAsm *this_, int boost)
{
    AcPlayerParamSt *pp = this_->pparam;
    float wr = g_asmparam.bstspd_weight_rate_default
             - ((float)pp->real_total_weight * g_asmparam.decrease_bstspd_by_weight) / 1000.0f;
    wr = acasm_clamp(wr, g_asmparam.boost_weightrate_min, g_asmparam.boost_weightrate_max);
    float ow = pp->leg_ow_rate;
    if (ow < g_asmparam.ow_leg_speed_rate_min) ow = g_asmparam.ow_leg_speed_rate_min;
    float k0 = wr * ow;
    float pr = (ps2_div((float)boost, g_asmparam.parts_boost_max) - 1.0f) * g_asmparam.boost_powerrate + 1.0f;
    pr = acasm_clamp(pr, g_asmparam.boost_powerrate_min, g_asmparam.boost_powerrate_max);
    float k = k0 * pr;
    pp->boost_powerrate = pr;
    this_->pparam->flymv_accel = g_asmparam.std_flymv_acc * k;
    pp = this_->pparam;
    pp->flymv_brk = 0.25f * pp->flymv_accel;
    this_->pparam->flymv_maxspd = g_asmparam.std_flymv_maxspd * k;
    this_->pparam->flyup_accel = g_asmparam.std_flyup_acc * k;
    this_->pparam->flyup_maxspd = g_asmparam.std_flyup_maxspd * k;
    this_->pparam->dash_accel = g_asmparam.std_dash_acc * k;
    this_->pparam->dash_maxspd = g_asmparam.std_dash_maxspd * k;

    pp = this_->pparam;
    float br = ps2_div((float)pp->legsp->brake, g_asmparam.parts_std_brake) * this_->opt_brake_rate;
    br = br * acasm_brake_rate(pp->leg_ow_rate);
    pp->dash_brake = br * g_asmparam.std_brake;

    pp = this_->pparam;
    int prog = pp->prog_no;
    float m = (ps2_div((float)pp->legsp->speed, g_asmparam.parts_avr_movespd) - 1.0f) * g_asmparam.moverate_difhosei
            + 1.0f;
    m = acasm_clamp(m, g_asmparam.moverate_min, g_asmparam.moverate_max);
    float ow2 = pp->leg_ow_rate;
    if (ow2 < g_asmparam.ow_leg_speed_rate_min) ow2 = g_asmparam.ow_leg_speed_rate_min;
    if (prog == 1 || prog == 3) {
        if (prog == 3) {
            pp->flymv_accel = ow2 * (g_asmparam.prog_move_stdacc * m);
            AcPlayerParamSt *q = this_->pparam;
            q->flymv_brk = 0.25f * q->flymv_accel;
            this_->pparam->flymv_maxspd = ow2 * (g_asmparam.prog_move_stdmaxspd * m);
        } else {
            pp->dash_accel = ow2 * (g_asmparam.prog_move_stdacc * m);
            this_->pparam->dash_maxspd = ow2 * (g_asmparam.prog_move_stdmaxspd * m);
        }
    } else {
        pp->walkmv_accel = ow2 * (g_asmparam.prog_move_stdacc * m);
        this_->pparam->walkmv_maxspd = ow2 * (g_asmparam.prog_move_stdmaxspd * m);
        pp = this_->pparam;
        pp->cutback_accel = pp->walkmv_accel * pp->legsp->cbspdrate;
        pp = this_->pparam;
        pp->cutback_maxspd = pp->walkmv_maxspd * pp->legsp->cbspdrate;
    }

    pp = this_->pparam;
    float b = acasm_brake_rate(pp->leg_ow_rate);
    pp->flymv_brk = pp->flymv_brk * b;
}

/* heat: cooling, generator / booster / overboost / extension heat, temperature limits */
static void __GetHeatParam(StAcAsm *this_)
{
    AcPlayerParamSt *pp = this_->pparam;
    /* original: radip is not null-checked (the radiator is a mandatory part) */
    float c = (float)(int)(pp->headp->defense.cooling + pp->corep->defense.cooling + pp->legsp->defense.cooling
                           + pp->armsp->defense.cooling);
    float h;
    pp->cqheatCooldown = g_asmparam.cool_to_cqheat * ((float)pp->radip->cooling + c * this_->opt_cool_rate);
    pp = this_->pparam;
    pp->cqheatQuickCooldown = g_asmparam.cool_to_cqheat * ((float)pp->radip->recov_cooling + c * this_->opt_cool_rate);
    pp = this_->pparam;
    pp->cqheatFixed = (float)pp->genep->heat * g_asmparam.heat_to_cqheat;
    pp = this_->pparam;
    if (pp->boosterp == NULL || pp->prog_no == 1 || pp->prog_no == 3)
        h = (float)pp->legsp->boost_heat * g_asmparam.heat_to_cqheat;
    else
        h = (float)pp->boosterp->boost_heat * g_asmparam.heat_to_cqheat;
    pp->cqheatBoost = h;
    pp = this_->pparam;
    if (pp->abl_shbooster)
        pp->cqheatBoost = pp->cqheatBoost + (float)pp->Rbackp->boost_heat * g_asmparam.heat_to_cqheat;
    pp = this_->pparam;
    PartsParamCoreSt *core = pp->corep;
    if (core->func_type == 0) {
        pp->cqheatEobFirst = (float)core->heat1st * g_asmparam.heat_to_cqheat;
        pp = this_->pparam;
        pp->cqheatEobBusy = (float)pp->corep->heat * g_asmparam.heat_to_cqheat;
    } else if (core->func_type == 1) {
        pp->cqheatEobFirst = (float)core->obchrgtheat * g_asmparam.heat_to_cqheat;
        pp = this_->pparam;
        pp->cqheatEobBusy = (float)pp->corep->obheat * g_asmparam.heat_to_cqheat;
    }
    pp = this_->pparam;
    if (pp->extp) pp->cqheatExt = (float)pp->extp->boost_heat * g_asmparam.heat_to_cqheat;
    pp = this_->pparam;
    pp->tmprtLimit = pp->corep->heat_limit;
    pp = this_->pparam;
    pp->tmprtWarning = (unsigned short)(unsigned int)(0.8f * (float)pp->corep->heat_limit);
    pp = this_->pparam;
    pp->default_heatsave = (pp->plus & 0x10) ? g_asmparam.plus_heatsave : 1.0f;
}

/* total weight, core (upper) load and the leg / arm overweight rates; returns the total weight */
static int __GetTotalWeight(StAcAsm *this_)
{
    AcPlayerParamSt *pp = this_->pparam;
    int boostw = 0, rbackw = 0;
    if (pp->boosterp && pp->prog_no != 1 && pp->prog_no != 3) boostw = pp->boosterp->basic.wp;
    if (pp->Rbackp && pp->Rbackp->w_use == 0) rbackw = pp->Rbackp->basic.wp;
    int total = pp->headp->basic.wp + pp->corep->basic.wp + pp->armsp->basic.wp + pp->legsp->basic.wp
              + pp->fcsp->basic.wp + pp->genep->basic.wp;
    if (pp->gunp) total += pp->gunp->basic.wp;
    if (pp->swordp) total += pp->swordp->basic.wp;
    if (pp->Lbackp) total += pp->Lbackp->basic.wp;
    if (pp->Rbackp) total += rbackw;
    if (pp->boosterp) total += boostw;
    if (pp->radip) total += pp->radip->basic.wp;
    if (pp->insidep) total += pp->insidep->basic.wp;
    if (pp->extp) total += pp->extp->basic.wp;
    if (pp->spswordp) total += pp->spswordp->basic.wp;
    if (pp->spgunp) total += pp->spgunp->basic.wp;
    int core = 0;
    if (pp->armsp) core += pp->armsp->basic.wp;
    if (pp->insidep) core += pp->insidep->basic.wp;
    if (pp->extp) core += pp->extp->basic.wp;
    if (pp->gunp) core += pp->gunp->basic.wp;
    if (pp->swordp) core += pp->swordp->basic.wp;
    pp->core_weight = core;
    this_->pparam->real_total_weight = total;

    pp = this_->pparam;
    float legr = ps2_div((float)(int)(pp->real_total_weight - pp->legsp->basic.wp), (float)pp->legsp->max_wp);
    float armr = ps2_div((float)pp->core_weight, (float)pp->corep->max_wp);
    pp->leg_ow_rate = acasm_ow_rate(legr);
    this_->pparam->arm_ow_rate = acasm_ow_rate(armr);
    pp = this_->pparam;
    if (pp->plus & 0x800) {
        if (pp->leg_ow_rate < 1.0f) pp->leg_ow_rate = 1.0f;
        pp = this_->pparam;
        if (pp->arm_ow_rate < 1.0f) pp->arm_ow_rate = 1.0f;
    }
    return total;
}

/* abilities from the leg program (prog2ability_tbl), plus bits, back unit, arms and core */
static void __GetAbility(StAcAsm *this_, int progno)
{
    AcPlayerParamSt *pp = this_->pparam;
    pp->abl_jump = 0;
    pp->abl_walkcannon = 0;
    pp->abl_flycannon = 0;
    pp->abl_dossunoff = 0;
    pp->abl_onwater = 0;
    for (unsigned int i = 0; i < 4; i++) {
        struct_07c9adbd *e = &__GetAbility__prog2ability_tbl[i];
        if (progno == e->progno) {
            pp->abl_jump = e->abl_jump & 1;
            pp->abl_walkcannon = e->abl_walkcannon & 1;
            pp->abl_flycannon = e->abl_flycannon & 1;
            pp->abl_dossunoff = e->abl_dossunoff & 1;
            pp->abl_onwater = e->abl_onwater & 1;
            break;
        }
    }
    if (pp->plus & 0x20) {
        pp->abl_walkcannon = 1;
        pp->abl_flycannon = 1;
    }
    pp->abl_shbooster = (pp->Rbackp && pp->Rbackp->type == 5) ? 1 : 0;
    pp->abl_Wblade = pp->armsp->type == 2;
    pp->core_type = pp->corep->func_type;
    pp->abl_limitoff = (pp->corep->addon & 2) != 0;
    pp->abl_magicsword = 1;
}

/* option-part multipliers (AcAssemblyPartsSt option bits) */
static void __GetOptionParam(StAcAsm *this_, unsigned int opt_parts)
{
    const StAcAsmParam &a = g_asmparam;
    this_->opt_shelldef_add = (opt_parts & 1) ? a.opt_shelldef_add : 1.0f;
    this_->opt_enedef_add = (opt_parts & 2) ? a.opt_enedef_add : 1.0f;
    this_->opt_cond_add = (opt_parts & 8) ? a.opt_cond_add : 0.0f;
    this_->opt_dmgbal_rate = (opt_parts & 4) ? a.opt_dmgbal_rate : 1.0f;
    this_->opt_canbal_add = 0.0f;
    this_->opt_turn_acc = (opt_parts & 0x100) ? a.opt_turn_acc : 1.0f;
    this_->opt_e_power = (opt_parts & 0x800) ? a.opt_e_power : 1.0f;
    this_->opt_e_rapid = (opt_parts & 0x1000) ? a.opt_e_rapid : 1.0f;
    this_->opt_lock_acc = (opt_parts & 0x20) ? a.opt_lock_acc : 1.0f;
    this_->opt_e_saver = (opt_parts & 0x2000) ? a.opt_e_saver : 1.0f;
    this_->opt_blade_rate = (opt_parts & 0x400) ? a.opt_bladeup : 1.0f;
    this_->opt_shield_rate = (opt_parts & 0x200) ? a.opt_shieldup : 1.0f;
    this_->opt_brake_rate = (opt_parts & 0x80) ? a.opt_brakeup : 1.0f;
    this_->opt_lockang_rate = (opt_parts & 0x40) ? a.opt_widelock : 1.0f;
    this_->opt_radar_range_rate = (opt_parts & 0x20000) ? a.opt_radar_range_up : 1.0f;
    this_->opt_cool_rate = (opt_parts & 0x40000) ? a.opt_cool_up : 1.0f;
    this_->opt_eo_range_up = (opt_parts & 0x80000) ? a.opt_eo_range_up : 0.0f;
    this_->opt_shield_eco_ene_rate = (opt_parts & 0x100000) ? a.opt_shield_eco_ene : 1.0f;
    this_->opt_eccm_up = (opt_parts & 0x1000000) ? a.opt_eccm_up : 0.0f;
}

/* part pointers into the parts data; optional parts are NULL when their id is 0 */
static void __GetPartsParamPointer(AcPlayerParamSt *pparam)
{
    AcPartsDataSt *d = pparam->ppartsd;
    short *id = pparam->pasmbly->idParts;
    pparam->headp = &d->head;
    pparam->corep = &d->core;
    pparam->armsp = &d->arms;
    pparam->legsp = &d->legs;
    pparam->fcsp = &d->fcs;
    pparam->genep = &d->gene;
    pparam->Lbackp = id[11] ? &d->backL : NULL;
    pparam->Rbackp = id[10] ? &d->backR : NULL;
    pparam->swordp = id[13] ? &d->sword : NULL;
    pparam->gunp = id[12] ? &d->gun : NULL;
    pparam->boosterp = id[4] ? &d->booster : NULL;
    pparam->radip = id[7] ? &d->radi : NULL;
    pparam->insidep = id[8] ? &d->inside : NULL;
    pparam->extp = id[9] ? &d->extension : NULL;
    pparam->spswordp = id[15] ? &d->spareL : NULL;
    pparam->spgunp = id[14] ? &d->spareR : NULL;
}

float AcAsm_RateRectify(float org, float rectify, float min, float max)
{
    return acasm_clamp(1.0f + (org - 1.0f) * rectify, min, max);
}

/* ---- s56 group 2: boost drain, jump / fall, reaction, head, radar, left arm, defense ---- */

/* generator drain: total part energy use, boost consumption / recharge, overboost and extension drain */
static void __GetBoostDrainParam(StAcAsm *this_, int boostsub)
{
    AcPlayerParamSt *pp = this_->pparam;
    int ep = pp->headp->basic.ep + pp->corep->basic.ep + pp->armsp->basic.ep + pp->legsp->basic.ep
           + pp->fcsp->basic.ep + pp->genep->basic.ep;
    if (pp->gunp) ep += pp->gunp->basic.ep;
    if (pp->swordp) ep += pp->swordp->basic.ep;
    if (pp->Lbackp) ep += pp->Lbackp->basic.ep;
    if (pp->Rbackp && pp->Rbackp->w_use == 0) ep += pp->Rbackp->basic.ep;
    if (pp->boosterp && pp->legsp->type != 3 && pp->legsp->type != 4) ep += pp->boosterp->basic.ep;
    if (pp->insidep) ep += pp->insidep->basic.ep;
    if (pp->extp) ep += pp->extp->basic.ep;

    const StAcAsmParam &a = g_asmparam;
    int stop = pp->legsp->stop_ep + (ep - pp->legsp->basic.ep);
    pp->boost_sub = ps2_ftoi(a.prog_boostsub_rate * (float)boostsub);
    if (pp->boost_sub <= 0) pp->boost_sub = 1;
    int add = ps2_ftoi(a.prog_boostadd_rate * (float)(int)(pp->genep->power - stop));
    if (add < ps2_ftoi(a.min_boostadd)) add = ps2_ftoi(a.min_boostadd);
    if (add > 0x180) add = 0x180;
    pp->boost_add = add;
    int madd = ps2_ftoi(a.prog_boostadd_rate * (float)(int)(pp->genep->power - ep));
    if (madd <= 0) madd = 1;
    if (madd > 0xc0) madd = 0xc0;
    pp->move_boost_add = madd;
    if (pp->plus & 0x40) {
        pp->boost_sub >>= 1;
        pp->boost_add = (pp->boost_add * 3) / 2;
        pp->move_boost_add = (pp->move_boost_add * 3) / 2;
    }
    pp->boost_max = ps2_ftoi((float)pp->genep->charge_max + this_->opt_cond_add);
    pp->boost_redarea = pp->genep->charge_recov;
    pp->oboost_1stsub = ps2_ftoi((float)pp->corep->ob_ep1st * a.prog_boostsub_rate);
    pp->oboost_sub = ps2_ftoi((float)pp->corep->ob_ep * a.prog_boostsub_rate);
    if (pp->extp) {
        pp->eboost_sub = pp->extp->boost_sub;
        switch (pp->extp->prog_no) {
        case 0xe:
            pp->eboost_sub = ps2_ftoi((float)pp->eboost_sub * a.countermis_boostrate);
            break;
        case 8:
            pp->eboost_sub = ps2_ftoi((float)pp->eboost_sub * a.prog_boostsub_rate);
            break;
        case 9:
            pp->eboost_sub = ps2_ftoi((float)pp->eboost_sub * a.shield_boostrate);
            pp->eboost_sub = ps2_ftoi(0.5f + (float)pp->eboost_sub * this_->opt_shield_eco_ene_rate);
            break;
        }
    }
    pp->total_ep = ep;
    if (pp->radip) {
        pp->drainCool = ps2_ftoi((float)pp->radip->basic.ep * a.prog_boostadd_rate);
        pp->drainQuickCool = ps2_ftoi((float)pp->radip->recov_cooling_ep * a.prog_boostadd_rate);
    } else {
        pp->drainCool = 0;
        pp->drainQuickCool = 0;
    }
}

/* jump / fall / landing / blade-dash speeds from jump_fall_param and the overweight rate */
static void __GetJumpFallParam(StAcAsm *this_)
{
    AcPlayerParamSt *pp = this_->pparam;
    const StAcAsmParam &a = g_asmparam;
    float ow = pp->leg_ow_rate < a.ow_leg_speed_rate_min ? a.ow_leg_speed_rate_min : pp->leg_ow_rate;
    pp->jumpspd = ((float)pp->legsp->jumpspd * (jump_fall_param[0] * ow)) / 100.0f;
    ow = pp->leg_ow_rate < a.ow_leg_speed_rate_min ? a.ow_leg_speed_rate_min : pp->leg_ow_rate;
    pp->dashjumpspd = jump_fall_param[1] * ow;
    pp->land_dmgspd = jump_fall_param[2] * acasm_brake_rate(pp->leg_ow_rate);
    pp->gravity = jump_fall_param[3];
    pp->fall_maxspd = jump_fall_param[4];
    pp->dash_mintime = 13.0f;
    pp->blade_spd0 = jump_fall_param[5];
    pp->blade_brk = jump_fall_param[6] * acasm_brake_rate(pp->leg_ow_rate);
}

/* cannon recoil on this AC: legs (+ cannon balancer option) and head balance, overweight brake */
static void __GetCannonReact(StAcAsm *this_, int wep_react, SfxDstat *dstatp, char *reactp, char *yreactp)
{
    AcPlayerParamSt *pp = this_->pparam;
    const StAcAsmParam &a = g_asmparam;
    float L = ps2_div(a.parts_std_legbalance, (float)(int)(pp->legsp->balance + ps2_ftoi(this_->opt_canbal_add)));
    float H = ps2_div(a.parts_std_headbalance, (float)pp->headp->balance);
    float f = acasm_brake_rate(pp->leg_ow_rate) * (L * H);
    int react = ps2_ftoi(f * (float)wep_react);
    int yreact = ps2_ftoi(a.cannon_yreact_flyup_acc * (f * (float)dstatp->react));
    if (react <= 0) react = 1;
    else if (react > 0x40) react = 0x40;
    if (yreact < 0) yreact = 0;
    else if (yreact > 0x40) yreact = 0x40;
    if (reactp) *reactp = (char)react;
    if (yreactp) *yreactp = (char)yreact;
}

/* hit reaction (stagger) resistance from leg / head balance and overweight: 0x20..0xff */
static int __GetReactionParam(StAcAsm *this_, int balanceadd)
{
    AcPlayerParamSt *pp = this_->pparam;
    const StAcAsmParam &a = g_asmparam;
    float lb = (float)ps2_ftoi(this_->opt_dmgbal_rate * (float)(int)(pp->legsp->balance + balanceadd));
    float leg = 1.0f + (ps2_div(lb, a.parts_std_legbalance) - 1.0f) * a.react_legbalancerate;
    leg = acasm_clamp(leg, a.react_legbalancerate_min, a.react_legbalancerate_max);
    float head = 1.0f + (ps2_div((float)pp->headp->balance, a.parts_std_headbalance) - 1.0f) * a.react_headbalancerate;
    head = acasm_clamp(head, a.react_headbalancerate_min, a.react_headbalancerate_max);
    float b = acasm_brake_rate(pp->leg_ow_rate);
    int r = ps2_ftoi(64.0f * (b * (leg * head)));
    if (r < 0x20) r = 0x20;
    if (r > 0xff) r = 0xff;
    return r;
}

/* merge one radar unit into pparam (head / radar back units): stat bits, longest range (+ its type), widest
 * zoom, shortest nonzero interval; the range is rescaled by the radar option after every unit */
static inline void acasm_merge_radar(StAcAsm *this_, const PartsParamBasicRadarSt *r)
{
    AcPlayerParamSt *pp = this_->pparam;
    pp->radar_stat |= r->stat;
    if (pp->radar_range < r->rd_range) {
        pp->radar_range = r->rd_range;
        pp->radar_type = r->type;
    }
    pp->zoom_range = pp->zoom_range < r->zoom_range ? r->zoom_range : pp->zoom_range;
    if (r->rd_intvl) {
        unsigned short cur = pp->rd_intvl;
        pp->rd_intvl = (int)r->rd_intvl < (int)cur ? r->rd_intvl : cur;
    }
    pp->radar_range = pp->radar_range * this_->opt_radar_range_rate;
    pp->total_eccm += r->eccm_power;
}

static inline void acasm_reset_radar(AcPlayerParamSt *pp)
{
    pp->radar_range = 0.0f;
    pp->zoom_range = 0.0f;
    pp->rd_intvl = 999;
    pp->radar_stat = 0;
    pp->radar_type = 0;
}

/* radar from the head and the radar back units (index 0 = right back), then option / plus overrides */
static void __CalcRadarParam(StAcAsm *this_)
{
    AcPlayerParamSt *pp = this_->pparam;
    PartsParamHeadSt *head = pp->headp;
    PartsParamBackSt *back[2] = { pp->Rbackp, pp->Lbackp };
    unsigned int opt = pp->option_parts;
    acasm_reset_radar(pp);
    if (head) acasm_merge_radar(this_, &head->radar_stat);
    for (int i = 0; i < 2; i++) {
        if (back[i] && back[i]->type == 1) acasm_merge_radar(this_, &back[i]->radar_stat);
    }
    if (opt & 0x4000) pp->radar_stat |= 1;
    if (opt & 0x10) pp->abl_jammer = 1;
    if (pp->plus & 4) pp->radar_stat |= 8;
    if (pp->plus & 2) {
        if (pp->radar_range < 1e-5f) {   /* 0x3727c5ac */
            pp->radar_range = g_asmparam.plus_radar_range;
            pp->zoom_range = g_asmparam.plus_zoom_range;
            pp->radar_stat = (unsigned char)ps2_ftoi(g_asmparam.plus_radar_stat);
            pp->rd_intvl = (unsigned short)ps2_ftoi(g_asmparam.plus_radar_interval);
        }
        pp->radar_type = 3;
    }
}

/* left arm unit: blade colour, or the shield's magic / drain / guard angles / heat */
static void __GetLarmParamIn(StAcAsm *this_)
{
    PS2_FRAME(0x20);
    AcPlayerParamSt *pp = this_->pparam;
    const StAcAsmParam &a = g_asmparam;
    switch (pp->Larm_type) {
    case 3:
        pp->Larm_equipcolor = pp->swordp->equip_color;
        break;
    case 2: {
        PartsParamSwordSt *sh = pp->swordp;
        pp->shield_magid = (unsigned char)sh->sword_magid;
        pp->shield_sub = ps2_ftoi(a.shield_boostrate * (float)sh->boost_sub);
        pp->shield_angx = 0.5f * ((3.1415927f * (float)sh->def_angx) / 180.0f);
        pp->shield_angy = 0.5f * ((3.1415927f * (float)sh->def_angy) / 180.0f);
        pp->cqheatShield = (float)sh->blade_heat * a.heat_to_cqheat;
        pp->cqheatShieldCut = (float)sh->def_heat * a.shieldcool_to_cqheat;
        pp->shield_magcp = AcPtsRes_GetMagicPtr(pp->ptsresp, 6, 6);
        pp->shield_combp = AcPtsRes_GetSfxCombHead(pp->ptsresp, 6);
        pp->shield_sub = ps2_ftoi(0.5f + (float)pp->shield_sub * this_->opt_shield_eco_ene_rate);
        if ((int)sh->def_shell < (int)sh->def_energy) {
            pp->shield_angx = pp->shield_angx * this_->opt_shield_rate;
            pp->shield_angy = pp->shield_angy * this_->opt_shield_rate;
        }
        break;
    }
    }
}

/* head: FCS / balancer / CPU values, then the radar from the head alone (headp not null-checked) */
static void __GetHeadParam(StAcAsm *this_)
{
    AcPlayerParamSt *pp = this_->pparam;
    PartsParamHeadSt *h = pp->headp;
    pp->head_addon = h->addon;
    pp->fire_ctrl = h->fire_ctrl;
    pp->head_balancer = h->balance;
    pp->atlas_stat = h->atlas_stat;
    pp->cpu_grade = h->cpu_grade;
    pp->cpu_sex = h->cpu_sex;
    pp->hack_lv = h->hack_lv;
    acasm_reset_radar(pp);
    acasm_merge_radar(this_, &h->radar_stat);
}

/* clamp((int)base + 0x8ca - (int)(sum * opt), 50, 0x8ca) */
static inline int acasm_def(int sum, float opt, float base)
{
    int d = ps2_ftoi(base) + 0x8ca - ps2_ftoi((float)sum * opt);
    if (d < 50) d = 50;
    if (d > 0x8ca) d = 0x8ca;
    return d;
}

/* defenses: normal, with the left-arm shield, with an extension shield (option-scaled armour sums) */
static void __CalcDefenseIn(StAcAsm *this_)
{
    const StAcAsmParam &a = g_asmparam;
    this_->ext_shelldef_add = 0;
    this_->ext_enedef_add = 0;
    AcPlayerParamSt *pp = this_->pparam;
    int shell = pp->headp->defense.shell + pp->armsp->defense.shell + pp->legsp->defense.shell
              + pp->corep->defense.shell;
    int ene = pp->headp->defense.energy + pp->armsp->defense.energy + pp->legsp->defense.energy
            + pp->corep->defense.energy;
    pp->normal_def.shell = acasm_def(shell + this_->ext_shelldef_add, this_->opt_shelldef_add, a.min_shelldef);
    pp->normal_def.energy = acasm_def(ene + this_->ext_enedef_add, this_->opt_enedef_add, a.min_energydef);
    pp->normal_def.react = __GetReactionParam(this_, 0);
    pp->normal_def.useene = 0;
    pp->shield_def = pp->normal_def;
    if (pp->Larm_type == 2) {
        PartsParamSwordSt *sh = pp->swordp;
        pp->shield_def.shell = acasm_def(shell + sh->def_shell, this_->opt_shelldef_add, a.min_shelldef);
        pp->shield_def.energy = acasm_def(ene + sh->def_energy, this_->opt_enedef_add, a.min_energydef);
        pp->shield_def.react = __GetReactionParam(this_, sh->def_react);
        pp->shield_def.useene = sh->boost_sub > 0;
    }
    PartsParamExtensionSt *ext = pp->extp;
    if (ext && (ext->prog_no == 9 || ext->prog_no == 6)) {
        pp->exshield_def.shell = acasm_def(shell + ext->def_shell, this_->opt_shelldef_add, a.min_shelldef);
        pp->exshield_def.energy = acasm_def(ene + ext->def_energy, this_->opt_enedef_add, a.min_energydef);
        pp->exshield_def.react = __GetReactionParam(this_, ext->def_react);
        pp->exshield_def.useene = ext->boost_sub > 0;
        pp->cqheatExShieldCut = (float)ext->def_heat * a.shieldcool_to_cqheat;
    } else {
        pp->exshield_def = pp->normal_def;
        pp->cqheatExShieldCut = 0.0f;
    }
}

/* ---- s56 group 3: weapons, weight, the public wrappers ---- */

/* C (unsigned int) cast of a float as MWCC emits it (>= 2^31: subtract, convert, or the top bit) */
static inline unsigned int acasm_ftou(float f)
{
    if (2147483648.0f <= f) return (unsigned int)ps2_ftoi(f - 2147483648.0f) | 0x80000000u;
    return (unsigned int)ps2_ftoi(f);
}

/* weapon slot of a part status id (inlined as its own jump table in Interface / __AddBackWeapon) */
static inline int acasm_windex(int statid)
{
    switch (statid) {
    case 0: return 0;
    case 1: return 1;
    case 2: return 2;
    case 3: return 3;
    case 4: return 1;
    case 5: return 2;
    case 6: return 0;
    default: return -1;
    }
}

/* weapons[n] for n = 0..4 (4 = extweapon, which follows weapons[] in AcPlayerParamSt) */
static inline _weapon_st *acasm_weapon(AcPlayerParamSt *pp, int n)
{
    return (_weapon_st *)((char *)pp + offsetof(AcPlayerParamSt, weapons) + n * (int)sizeof(_weapon_st));
}

/* extension booster: weight / overweight scaled by the extension's power (extp not null-checked) */
static inline void acasm_exboost(AcPlayerParamSt *pp, PartsParamExtensionSt *ext)
{
    const StAcAsmParam &a = g_asmparam;
    float wr = a.bstspd_weight_rate_default - ((float)pp->real_total_weight * a.decrease_bstspd_by_weight) / 1000.0f;
    wr = acasm_clamp(wr, a.boost_weightrate_min, a.boost_weightrate_max);
    float ow = pp->leg_ow_rate < a.ow_leg_speed_rate_min ? a.ow_leg_speed_rate_min : pp->leg_ow_rate;
    float k = (wr * ow) * ps2_div((float)ext->power, a.parts_boost_max);
    pp->exboost_acc = a.exboost_std_acc * k;
    pp->exboost_maxspd = a.exboost_std_maxspd * k;
    pp->exboost_flyup_acc = a.exboost_std_flyup_acc * k;
    pp->exboost_flyup_maxspd = a.exboost_std_flyup_maxsp * k;
    pp->exboost_acctime = (unsigned char)ext->boost_acctime;
    pp->exboost_busytime = (unsigned char)ext->boost_busytime;
}

/* weight-dependent values: weights, overboost, boost / drain / turn / jump, extension boost, reaction, recoil */
static void __CalcWeightIn(StAcAsm *this_)
{
    const StAcAsmParam &a = g_asmparam;
    this_->pparam->total_weight = __GetTotalWeight(this_);
    AcPlayerParamSt *pp = this_->pparam;
    pp->upper_weight = pp->total_weight - pp->legsp->basic.wp;
    __GetOptionParam(this_, this_->pparam->option_parts);

    pp = this_->pparam;
    float wr = a.obstspd_weight_rate_default
             - ((float)pp->real_total_weight * a.decrease_obstspd_by_weight) / 1000.0f;
    wr = acasm_clamp(wr, a.Oboost_weightrate_min, a.Oboost_weightrate_max);
    float ow = pp->leg_ow_rate < a.ow_leg_speed_rate_min ? a.ow_leg_speed_rate_min : pp->leg_ow_rate;
    float k = wr * ow;
    float q = 1.0f + (ps2_div((float)pp->corep->ob_power, a.parts_max_obpower) - 1.0f) * a.obpower_rate;
    q = acasm_clamp(q, 0.5f, 1.0f);
    pp->oboost_maxspd = k * (a.std_ob_maxspeed * q);
    pp = this_->pparam;
    if (!(pp->oboost_maxspd <= a.std_ob_maxspeed)) pp->oboost_maxspd = a.std_ob_maxspeed;
    pp->oboost_spd0 = this_->pparam->oboost_maxspd;
    pp = this_->pparam;
    pp->oboost_acc = pp->oboost_maxspd * a.ob_accrate;

    pp = this_->pparam;
    PartsParamBoosterSt *bst = pp->boosterp;
    unsigned int boost, boostsub;
    if (!bst || pp->prog_no == 1 || pp->prog_no == 3) {
        boost = pp->legsp->boost_power;
        boostsub = pp->legsp->boost_sub;
    } else {
        boost = bst->power;
        boostsub = bst->boost_sub;
    }
    if (pp->abl_shbooster) {
        boost = (unsigned short)(boost + (unsigned short)pp->Rbackp->boost_add);
        boostsub = (unsigned short)(boostsub + pp->Rbackp->boost_ep);
    }
    __GetBoostMoveParam(this_, boost & 0xffff);
    __GetBoostDrainParam(this_, boostsub & 0xffff);
    __GetTurnParam(this_);
    __GetJumpFallParam(this_);
    pp = this_->pparam;
    if (pp->extp) acasm_exboost(pp, pp->extp);

    this_->pparam->normal_def.react = __GetReactionParam(this_, 0) & 0xff;
    pp = this_->pparam;
    float t = acasm_clamp(2.0f - pp->arm_ow_rate, a.ow_recoil_rate_min, a.ow_recoil_rate_max);
    float rc = ((3.1415927f * pp->armsp->recoilctrl_accuracy) / 180.0f) * t;
    pp->gap_angle_by_recoil_r = rc;
    this_->pparam->gap_angle_by_recoil_l = rc;
    this_->pparam->gap_angle_by_recoil_min = rc;
    pp = this_->pparam;
    pp->recoilctrl_time = pp->armsp->recoilctrl_time;

    /* original bug: the slot pointer is never advanced - weapons[n_weapon] is redone n_weapon + 1 times */
    _weapon_st *wp = acasm_weapon(this_->pparam, this_->pparam->n_weapon);
    for (int i = 0; i <= this_->pparam->n_weapon; i++) {
        if (wp->prog_no == 2) __GetCannonReact(this_, wp->react_wep, &wp->dstat, &wp->reaction, &wp->yreaction);
    }
}

/* lock-on sight of a weapon with this AC's FCS (range, angles, lock time / speed / count; option scaled) */
static void __GetWeaponSight(StAcAsm *this_, WepSightSt *sightp, PartsParamWeaponSt *wdatp)
{
    float sc = this_->pparam->lockon_scale;
    sightp->type = this_->pparam->fcsp->basic.maker;
    sightp->sight = wdatp->lock_type;
    sightp->flag = this_->pparam->fcsp->lock_type;
    float r = this_->pparam->fcsp->sight[wdatp->sight_type].range;
    sightp->range = r;
    sightp->org_range = r;
    if (!(sightp->range <= wdatp->act.range)) sightp->range = wdatp->act.range;
    if (wdatp->sight_type == 4) {
        sightp->angx = 0.0f;
        sightp->angy = 0.0f;
        sightp->lock_angx = 0.0f;
        sightp->lock_angy = 0.0f;
        sightp->lock_time = 1;
        sightp->lock_speed = 0;
    } else {
        const PartsParamBasicLockSt *ls = &this_->pparam->fcsp->sight[wdatp->sight_type];
        sightp->angx = sc * ((3.1415927f * (float)ls->angx) / 180.0f);
        sightp->angy = sc * ((3.1415927f * (float)ls->angy) / 180.0f);
        sightp->lock_angx = sc * ((3.1415927f * (float)ls->lock_angx) / 180.0f);
        sightp->lock_angy = sc * ((3.1415927f * (float)ls->lock_angy) / 180.0f);
        sightp->lock_time = this_->pparam->fcsp->response;
        sightp->lock_speed = (short)(((int)ls->lock_time * (int)wdatp->lock_time) / 100);
        sightp->system_update_speed = this_->pparam->fcsp->system_update_speed;
        sightp->angx = sightp->angx * this_->opt_lockang_rate;
        sightp->angy = sightp->angy * this_->opt_lockang_rate;
        sightp->lock_angx = sightp->lock_angx * this_->opt_lockang_rate;
        sightp->lock_angy = sightp->lock_angy * this_->opt_lockang_rate;
        sightp->lock_time = (short)ps2_ftoi((float)sightp->lock_time * this_->opt_lock_acc);
        sightp->lock_speed = (short)ps2_ftoi((float)sightp->lock_speed * this_->opt_lock_acc);
    }
    int lkmax = wdatp->lkmax;
    int fcsmax = this_->pparam->fcsp->lockmax;
    if (!(lkmax < fcsmax)) lkmax = fcsmax;
    sightp->org_lock_max = (unsigned char)lkmax;
    sightp->lock_max = (unsigned char)lkmax;
}

/* fills a weapon slot from the weapon's part data (psr = parts slot); energy weapons get the option
 * multipliers; returns 1 */
static int __GetWeaponParam(StAcAsm *this_, _weapon_st *wp, PartsParamWeaponSt *wdatp, int psr, int weaponid,
                            int statid, int nameid, int name2id)
{
    PS2_FRAME(0x60);
    AcPtsResSt *ptsresp = this_->pparam->ptsresp;
    wp->id = weaponid;
    wp->statid = (unsigned char)statid;
    wp->nameid = (unsigned char)nameid;
    wp->name2id = (short)name2id;
    wp->equip_anmid = wdatp->equip_anmid;
    wp->release_anmid = wdatp->release_anmid;
    wp->shot_anmid = wdatp->shot_anmid;
    wp->shotcnt = wdatp->shotcnt;
    wp->autocnt = wdatp->autocnt;
    wp->autotime = wdatp->autotime;
    int fcsmax = this_->pparam->fcsp->lockmax;
    wp->lockmax = (wdatp->lkmax >= fcsmax && psr != 0) ? fcsmax : wdatp->lkmax;
    wp->chargetime = wdatp->chargetime;
    wp->prog_no = wdatp->prog_no;
    wp->jcon_progno = (unsigned char)wdatp->jcon_progno;
    wp->jcon_datano = wdatp->jcon_datano;
    wp->reload_time = wdatp->reload_time;
    wp->breload_time = wdatp->breload_time;
    wp->burst_shotcnt = wdatp->burst_shotcnt;
    wp->secondlock_time = wdatp->lock_time;
    wp->auto_missile = 0;
    wp->hand_attach = wdatp->hand_attach & 1;
    if (this_->pparam->armsp->type != 0) wp->hand_attach = 0;
    wp->trigshot = wdatp->trigshot & 1;
    wp->enable_addmgzn = wdatp->enable_addmgzn & 1;
    wp->spelldata_id = (unsigned short)wdatp->spelldata_id;
    wp->blt_initcnt = wdatp->blt_sta;
    wp->blt_cnt = wdatp->blt_sta;
    wp->blt_price = wdatp->blt_price;
    wp->magicid = wdatp->magicid;
    wp->range = wdatp->act.range;
    wp->deflect = psr == 0 ? wdatp->act.deflect : this_->pparam->fcsp->precision;
    wp->shotspeed = wdatp->act.speed;
    wp->reaction = 0;
    wp->actp = &wdatp->act;
    wp->dstat = wdatp->dstat;
    wp->react_startframe = wdatp->react_startframe;
    wp->react_nframe = wdatp->react_nframe;
    wp->react_wep = wdatp->react;
    wp->recoil = (3.1415927f * g_asmparam.weapon_recoil) / 180.0f;
    if (wp->prog_no == 2) __GetCannonReact(this_, wp->react_wep, &wp->dstat, &wp->reaction, &wp->yreaction);
    if (wp->dstat.effect & 0x20) {   /* energy weapon (the original also tests wp != NULL here: dead) */
        wp->reload_time = (unsigned char)acasm_ftou((float)wp->reload_time * this_->opt_e_rapid);
        wp->dstat.damage = (unsigned short)acasm_ftou((float)wp->dstat.damage * this_->opt_e_power);
        wp->blt_price = (unsigned short)acasm_ftou((float)wp->blt_price * this_->opt_e_saver);
    }
    __GetWeaponSight(this_, &wp->sight, wdatp);
    wp->org_lock_time = wp->sight.lock_time;
    wp->sight_order = psr == 6;
    wp->combp = NULL;
    wp->magcp = NULL;
    wp->spelp = NULL;
    if (wdatp->magicid != -1) {
        wp->combp = AcPtsRes_GetSfxCombHead(ptsresp, psr);
        wp->magcp = AcPtsRes_GetWeaponMagicPtr(ptsresp, psr, wdatp->magicid);
    }
    if (wdatp->spelldata_id != -1) {
        wp->combp = AcPtsRes_GetSfxCombHead(ptsresp, psr);
        wp->spelp = AcPtsRes_GetWeaponSpellPtr(ptsresp, psr, wdatp->spelldata_id);
    }
    return 1;
}

/* a part's weapon in the slot of statid (weapon data order 0); NULL when the part has no weapon */
static _weapon_st *acasm_part_weapon(StAcAsm *this_, int weapon_id, int statid, int psr, int nameid, int name2id)
{
    _weapon_st *wp = acasm_weapon(this_->pparam, acasm_windex(statid));
    int ok = 0;
    if (weapon_id) {
        PartsParamWeaponSt *wdatp = AcPtsRes_GetWeaponPtr(this_->pparam->ptsresp, psr, 0);
        ok = __GetWeaponParam(this_, wp, wdatp, psr, weapon_id, statid, nameid, name2id);
    }
    return ok ? wp : NULL;
}

/* a back weapon in the slot of statid; NULL when the unit has no weapon */
static _weapon_st *acasm_back_weapon(StAcAsm *this_, PartsParamBackSt *backp, int statid, int psr)
{
    return acasm_part_weapon(this_, backp->weapon_id, statid, psr, backp->name_id, backp->name2_id);
}

/* a back unit (statid 2 = left, 1 = right): weapon (slot 5 / 4 when it uses the weapon slot), radar, stealth,
 * magazine bonus, ...; then the slot's jpos */
static void __AddBackWeapon(StAcAsm *this_, PartsParamBackSt *backp, int statid)
{
    PS2_FRAME(0x90);
    if (!backp) return;
    AcPlayerParamSt *pp = this_->pparam;
    _weapon_st *wp = acasm_weapon(pp, acasm_windex(statid));
    int w_use = backp->w_use;
    int psr;
    switch (backp->type) {
    case 0:
        if (statid == 2) {
            psr = 7;
            if (w_use) statid = 5;
        } else {
            psr = 8;
            if (w_use) statid = 4;
        }
        wp = acasm_back_weapon(this_, backp, statid, psr);
        break;
    case 1:
        acasm_merge_radar(this_, &backp->radar_stat);
        break;
    case 2:
        pp->abl_stealth = 1;
        if (statid == 2) {
            psr = 7;
            statid = 5;
        } else {
            psr = 8;
            statid = 4;
        }
        w_use = 1;
        wp = acasm_back_weapon(this_, backp, statid, psr);
        break;
    case 3:
        if (w_use) {
            pp->shell_addrate = (unsigned char)backp->magazine_add;
            this_->pparam->energy_addrate = (unsigned char)backp->magazine_add;
        } else {
            pp->shell_addrate += (unsigned char)backp->magazine_add;
            this_->pparam->energy_addrate += (unsigned char)backp->magazine_add;
        }
        break;
    case 4:
        pp->shell_addrate += (unsigned char)backp->magazine_add;
        break;
    case 5:
        w_use = 1;
        break;
    default:
        statid = -1;
        break;
    }
    /* wp is NULL when a weapon unit has no weapon: the original then writes jpos to PS2 address 0x2b (skipped) */
    if (w_use) {
        if (wp) wp->jpos = 6;
    } else if (statid == 2) {
        if (wp) wp->jpos = 4;
    } else if (statid == 1) {
        if (wp) wp->jpos = 5;
    }
}

void AcAsm_GetWeaponSight(AcPlayerParamSt *pPartsParam, WepSightSt *pWepSight, PartsParamWeaponSt *pPartsParamWep)
{
    PS2_FRAME(0x90);
    PS2_STACK(StAcAsm, as, 0x30);
    as.pparam = pPartsParam;
    __GetOptionParam(&as, as.pparam->option_parts);
    __GetWeaponSight(&as, pWepSight, pPartsParamWep);
}

void AcAsm_CalcDefenseEx(AcPlayerParamSt *pparam)
{
    PS2_FRAME(0x70);
    PS2_STACK(StAcAsm, as, 0x10);
    as.pparam = pparam;
    __GetOptionParam(&as, as.pparam->option_parts);
    __CalcDefenseIn(&as);
}

void AcAsm_CalcWeightEx(AcPlayerParamSt *pparam)
{
    PS2_FRAME(0x70);
    PS2_STACK(StAcAsm, as, 0x10);
    as.pparam = pparam;
    __GetOptionParam(&as, as.pparam->option_parts);
    __CalcWeightIn(&as);
}

void AcAsm_CalcRadarParamEx(AcPlayerParamSt *pparam)
{
    PS2_FRAME(0x70);
    PS2_STACK(StAcAsm, as, 0x10);
    as.pparam = pparam;
    __GetOptionParam(&as, as.pparam->option_parts);
    __CalcRadarParam(&as);
}

/* Larm_type of a left-arm unit: its type 0..3 (2 = shield), other 4; no unit: 0 with arm type 2, else 4 */
int AcAsm_GetLeftArmTypeEx(PartsParamSwordSt *ppprm, int armtype)
{
    if (!ppprm) return armtype == 2 ? 0 : 4;
    switch (ppprm->type) {
    case 3: return 3;
    case 2: return 2;
    case 1: return 1;
    case 0: return 0;
    default: return 4;
    }
}

void AcAsm_GetLarmParamEx(AcPlayerParamSt *pparam)
{
    PS2_FRAME(0x70);
    PS2_STACK(StAcAsm, as, 0x10);
    as.pparam = pparam;
    __GetOptionParam(&as, as.pparam->option_parts);
    __GetLarmParamIn(&as);
}

int AcAsm_GetWeaponParamEx(_weapon_st *wp, int psr, int weporder, int weapon_id, int statid, int nameid, int name2id,
                           AcPlayerParamSt *pparam)
{
    PS2_FRAME(0xf0);
    if (!weapon_id) return 0;
    PS2_STACK(StAcAsm, as, 0x90);
    as.pparam = pparam;
    __GetOptionParam(&as, as.pparam->option_parts);
    PartsParamWeaponSt *wdatp = AcPtsRes_GetWeaponPtr(as.pparam->ptsresp, psr, weporder);
    return __GetWeaponParam(&as, wp, wdatp, psr, weapon_id, statid, nameid, name2id);
}

void AcAsm_GetExboostParam(AcPlayerParamSt *pparam)
{
    acasm_exboost(pparam, pparam->extp);
}

float AcAsm_GetQuitCooldown(int cooling)
{
    return (float)cooling * g_asmparam.cool_to_cqheat;
}

/* magazine bonus (percent, rounded up) for shell (effect 0x10) / energy (0x20) weapons, both from the base count */
void AcAsm_AddMagazine(_weapon_st *wp, int shell_addrate, int energy_addrate)
{
    if (!shell_addrate && !energy_addrate) return;
    if (!wp) return;   /* original: reads blt_initcnt before the check and stores blt_cnt through NULL */
    int init = wp->blt_initcnt;
    if (wp->dstat.effect & 0x10)
        wp->blt_initcnt = (unsigned short)(init + (unsigned short)((init * shell_addrate + 99) / 100));
    if (wp->dstat.effect & 0x20)
        wp->blt_initcnt = (unsigned short)(wp->blt_initcnt + (unsigned short)((init * energy_addrate + 99) / 100));
    wp->blt_cnt = wp->blt_initcnt;
}

StModel *AcAsm_StatidToModelp(int statid, AcPlayerSt *acp)
{
    switch (statid) {
    case 0: return acp->modeltbl[5];
    case 1: return acp->modeltbl[0];
    case 2: return acp->modeltbl[1];
    case 3: return acp->inside_mp;
    case 4: return acp->modeltbl[0];
    case 5: return acp->modeltbl[1];
    default: return NULL;
    }
}

int AcAsm_StatidToWindex(int statid)
{
    return acasm_windex(statid);
}

/* ---- s56 group 4: the full assembly and the boot-time loadage table ---- */

/* loads the 16 (loadage ratio, overweight rate) points from file 0x5f; 1 on success */
int AcAsm_LoadLoadageRatioTable(void)
{
    PS2_FRAME(0x810);
    PS2_STACK(char[0x800], buf, 0x10);
    if (!File_Read(0x5f, 0x80, buf, 1)) return 0;
    CopyMemory(AcAsm_LoadageRatioTable, buf, 0x80);
    return 1;
}

/* the full assembly of one AC (from __InitPlayer): parts, option multipliers, weights, overboost, damper /
 * recoil, boost, landing, turn, hp, head / radar, left arm, defenses, jump, the weapon slots (arm or gun,
 * back units, inside, extension through the overlay function acext_SetParam), magazine bonuses, EO angles and
 * the option / plus radar bits */
void AcAsm_Interface(AcPlayerParamSt *pparam, AcAssemblyPartsSt *pasmbly, AcPartsDataSt *ppartsd,
                     MyOptionSt *pacopt, int idAnim)
{
    PS2_FRAME(0xe0);
    PS2_STACK(StAcAsm, as, 0x80);
    const StAcAsmParam &a = g_asmparam;
    pparam->pasmbly = pasmbly;
    pparam->pacopt = pacopt;
    pparam->ppartsd = ppartsd;
    pparam->idAnim = idAnim;
    pparam->option_parts = pparam->pasmbly->option;
    as.pparam = pparam;
    __GetOptionParam(&as, as.pparam->option_parts);
    as.pparam->lockon_scale = pparam->pacopt->level == 0 ? 1.0f : a.hardmode_lockscale;
    __GetPartsParamPointer(as.pparam);

    AcPlayerParamSt *pp = as.pparam;
    pp->shell_addrate = 0;
    pp->energy_addrate = 0;
    pp->total_eccm = 0;
    for (int i = 0; i < 14; i++) pp->partsid[i] = pparam->pasmbly->idParts[i];
    pp->combid = (unsigned char)idAnim;
    pp->tall = s_anim2tall[pp->combid];
    for (int i = 0; i < 7; i++) {   /* parts 0-3, 5, 6 default to 1 (4 may stay 0) */
        if (i != 4 && pp->partsid[i] == 0) pp->partsid[i] = 1;
    }
    pp->plus = (unsigned int)(int)pparam->pacopt->plus;

    static const unsigned char combid2prog[8] = {3, 0, 0, 0, 0, 0, 2, 1};
    pp->prog_no = combid2prog[pp->combid & 7];
    pp->motor_seid = (unsigned short)AcAsm_Interface__legtypedatatbl[pp->prog_no].motor_seinfo.id;
    pp->typedatap = &AcAsm_Interface__legtypedatatbl[pp->prog_no];
    int prog = pp->prog_no;
    pp->cannon_equipang = (prog == 3 || prog == 2 || prog == 1) ? -0.78539819f : -1.5707964f;   /* 0xbf490fdb / 0xbfc90fdb */
    pp->hokanparamp = &AcAsm_Interface__hokanspd_tbl[pp->combid & 7];

    __GetAbility(&as, pp->prog_no);
    pp->total_weight = __GetTotalWeight(&as);
    pp->upper_weight = pp->total_weight - pp->legsp->basic.wp;
    pp->fcs_paraeffic = (float)pp->fcsp->paraeffic / 100.0f;
    pp->total_eccm += pp->fcsp->eccm_power;
    __GetHeatParam(&as);

    /* vulcan (core machine gun) */
    pp->vsmgun_splid = pp->corep->vs_mgun_splid;
    pp->vs_mgun_shotrate = pp->corep->vs_mgun_sr;
    pp->vs_mgun_errrate = pp->corep->vs_mgun_er;
    int r = pp->corep->vs_mgun_range;
    pp->vs_mgun_range2J = (float)(r * r);
    pp->vs_mgun_angx = (3.1415927f * (float)pp->corep->vs_mgun_angx) / 180.0f;
    pp->vs_mgun_angy = (3.1415927f * (float)pp->corep->vs_mgun_ang) / 180.0f;
    if (pp->plus & 0x100) {
        pp->vs_mgun_shotrate += 10;   /* u8 wrap */
        if (pp->vs_mgun_shotrate > 100) pp->vs_mgun_shotrate = 100;
    }

    /* overboost */
    float wr = a.obstspd_weight_rate_default
             - ((float)pp->real_total_weight * a.decrease_obstspd_by_weight) / 1000.0f;
    wr = acasm_clamp(wr, a.Oboost_weightrate_min, a.Oboost_weightrate_max);
    float ow = pp->leg_ow_rate < a.ow_leg_speed_rate_min ? a.ow_leg_speed_rate_min : pp->leg_ow_rate;
    float k = wr * ow;
    float q = 1.0f + (ps2_div((float)pp->corep->ob_power, a.parts_max_obpower) - 1.0f) * a.obpower_rate;
    q = acasm_clamp(q, 0.5f, 1.0f);
    pp->oboost_maxspd = k * (a.std_ob_maxspeed * q);
    if (!(pp->oboost_maxspd <= a.std_ob_maxspeed)) pp->oboost_maxspd = a.std_ob_maxspeed;
    pp->oboost_spd0 = pp->oboost_maxspd;
    pp->oboost_acc = pp->oboost_maxspd * a.ob_accrate;
    pp->limitoff_time = pp->corep->limit;
    pp->limitoff_penatime = pp->corep->rejection;

    /* arm damper, recoil */
    float d = 1.0f + (ps2_div(a.armparts_std_damper, (float)pp->armsp->damper) - 1.0f) * a.armdamper_partsrate;
    pp->arm_damper = acasm_clamp(d, a.armdamper_partsrate_min, a.armdamper_partsrate_max);
    float t = acasm_clamp(2.0f - pp->arm_ow_rate, a.ow_recoil_rate_min, a.ow_recoil_rate_max);
    float rc = ((3.1415927f * pp->armsp->recoilctrl_accuracy) / 180.0f) * t;
    pp->gap_angle_by_recoil_r = rc;
    pp->gap_angle_by_recoil_l = rc;
    pp->gap_angle_by_recoil_min = rc;
    pp->recoilctrl_time = pp->armsp->recoilctrl_time;

    /* boost */
    PartsParamBoosterSt *bst = pp->boosterp;
    unsigned int boost, boostsub;
    if (!bst || pp->prog_no == 1 || pp->prog_no == 3) {
        boostsub = pp->legsp->boost_sub;
        boost = pp->legsp->boost_power;
    } else {
        boost = bst->power;
        boostsub = bst->boost_sub;
    }
    if (pp->abl_shbooster) {
        boost = (unsigned short)(boost + pp->Rbackp->boost_add);
        boostsub = (unsigned short)(boostsub + pp->Rbackp->boost_ep);
    }
    __GetBoostMoveParam(&as, boost & 0xffff);
    __GetBoostDrainParam(&as, boostsub & 0xffff);

    /* landing damage speed (dead store: __GetJumpFallParam rewrites it below) */
    float ow2 = pp->leg_ow_rate < a.ow_leg_speed_rate_min ? a.ow_leg_speed_rate_min : pp->leg_ow_rate;
    float l = 1.0f + (ps2_div((float)pp->legsp->damper, a.legparts_std_damper) - 1.0f) * a.legdamper_partsrate;
    l = acasm_clamp(l, a.legdamper_partsrate_min, a.legdamper_partsrate_max);
    pp->land_dmgspd = l * (a.std_dmgland_speed * ow2);
    __GetTurnParam(&as);

    pp->hp = (pp->headp->defense.ap + pp->corep->defense.ap + pp->legsp->defense.ap + pp->armsp->defense.ap) * 4;
    __GetHeadParam(&as);
    const unsigned char *col0 = (const unsigned char *)&pparam->pasmbly->color[0];   /* bytes 9-11 of color[0] */
    pp->colorEye[0] = col0[9];
    pp->colorEye[1] = col0[10];
    pp->colorEye[2] = col0[11];

    /* left arm */
    pp->Larm_type = (unsigned char)AcAsm_GetLeftArmTypeEx(pp->swordp, pp->armsp->type);
    pp->shield_angy = 0.0f;
    pp->shield_angx = 0.0f;
    pp->cqheatShieldCut = 0.0f;
    pp->Larm_equipcolor = 8;
    __GetLarmParamIn(&as);
    __CalcDefenseIn(&as);
    pp->backboost_magdataid = pp->boosterp ? (unsigned char)pp->boosterp->magdataid : 0;
    pp->legboost_magdataid = (unsigned char)pp->legsp->magdataid;
    pp->extboost_magdataid = 0;
    __GetJumpFallParam(&as);

    /* weapon slots */
    pp->n_weapon = 4;
    for (int i = 0; i < pp->n_weapon; i++) {
        acasm_weapon(pp, i)->statid = 5;
        acasm_weapon(pp, i)->jpos = 0;
    }
    /* wp is NULL when the part has no weapon: the original then writes jpos to PS2 address 0x2b (skipped) */
    _weapon_st *wp;
    switch (pp->armsp->type) {
    case 1:
    case 2: {   /* weapon arms */
        PartsParamArmsSt *arms = pp->armsp;
        wp = acasm_part_weapon(&as, arms->weapon_id, 6, 2, arms->name_id, arms->name2_id);
        if (wp) wp->jpos = 1;
        pp->Rarm_equipcolor = 10;
        break;
    }
    case 0:
        if (pp->gunp) {
            PartsParamGunSt *gun = pp->gunp;
            wp = acasm_part_weapon(&as, gun->weapon_id, 0, 5, gun->name_id, gun->name2_id);
            if (wp) wp->jpos = 1;
            pp->Rarm_equipcolor = pp->gunp->equip_color;
        }
        break;
    }
    __AddBackWeapon(&as, pp->Lbackp, 1);
    __AddBackWeapon(&as, pp->Rbackp, 2);
    if (pp->insidep) {
        PartsParamInsideSt *ins = pp->insidep;
        wp = acasm_part_weapon(&as, ins->weapon_id, 3, 10, ins->name_id, ins->name2_id);
        if (wp) wp->jpos = 7;
    }
    /* overlay (parts_ext.c); calls back into AcAsm_GetWeaponParamEx / AcAsm_GetExboostParam */
    if (pp->extp) acext_SetParam(pp, pp->extp);

    /* magazine bonuses (AcAsm_AddMagazine inlined) */
    for (int i = 0; i < pp->n_weapon; i++) {
        if (acasm_weapon(pp, i)->statid == 5) continue;
        AcAsm_AddMagazine(acasm_weapon(pp, i), pp->shell_addrate, pp->energy_addrate);
    }
    if (pp->extp && pp->extp->weapon_id) AcAsm_AddMagazine(&pp->extweapon, pp->shell_addrate, pp->energy_addrate);

    /* EO angles, option / plus bits */
    pp->eo_angx = (float)pp->corep->sangx;
    pp->eo_angy = (float)pp->corep->sangy;
    pp->eo_angy = pp->eo_angy + as.opt_eo_range_up;
    if (pp->option_parts & 0x4000) pp->radar_stat |= 1;
    if (pp->option_parts & 0x10) pp->abl_jammer = 1;
    if (pp->option_parts & 0x10000) pp->use_gazelock = 1;
    pp->total_eccm += ps2_ftoi(as.opt_eccm_up);
    if (pp->plus & 4) pp->radar_stat |= 8;
    if (pp->plus & 2) {
        if (pp->radar_range < 1e-5f) {   /* 0x3727c5ac */
            pp->radar_range = a.plus_radar_range;
            pp->zoom_range = a.plus_zoom_range;
            pp->radar_stat = (unsigned char)ps2_ftoi(a.plus_radar_stat);
            pp->rd_intvl = (unsigned short)ps2_ftoi(a.plus_radar_interval);
        }
        pp->radar_type = 3;
    }
}
