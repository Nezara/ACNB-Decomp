/* src/enemy/eneop.cpp (OLSORTIE) - enemy "operations": a logic level lists up to 7 operations; the selector
 * (ene_select_operation / ene_select_operation2) scores them through the EnemyOpSt table (ene_op_tab, calcfunc) and
 * stores the winner in ep->cur_op / cur_opl, and the think function runs it every frame through
 * ene_execute_operation (plus the random up / down presses of hover legs). garage3 layer 1: ene_execute_operation,
 * ene_operation_init (nearest return-region marker at stage start) and the "stay" operation; layer 2:
 * ene_select_operation2 and the back-away / charge / turn-around / standby / area-alert operations and scores.
 * The other operations and ene_select_operation stay interpreted; the op tables stay guest data in the hybrid (the
 * interpreted selector reads them too), the source port defines them below. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
#define EOP_INI(f) ((void (*)(EnemySt *, EneInfoSt *))(f))
#define EOP_VL(f) ((float (*)(EnemySt *, EneInfoSt *))(f))
#define EOP_OP(f) ((void (*)(EnemySt *, EneInfoSt *))(f))
#define EOP(name, ini, vl, op) {(char *)name, ini, EOP_VL(vl), EOP_OP(op), 0, 0, 0.0f, 0.0f, 0.0f}
EnemyOpSt ene_op_shooting[3] = {
    EOP("SHOOTING    ", NULL, enevl_shooting, eneop_stay),
    EOP("AREA ALERT  ", NULL, enevl_area_alert, eneop_area_alert),
    EOP("RET INITPOS2", NULL, enevl_ret_initpos2, eneop_ret_initpos),
};
EnemyOpSt ene_op_tab[34] = {
    EOP("NOP         ", EOP_INI(eneini_default), enevl_standby, eneop_standby),
    EOP("APPROACH    ", EOP_INI(eneini_default), enevl_approach, eneop_approach),
    EOP("BACK AWAY   ", NULL, enevl_back, eneop_back),
    EOP("TURN AROUND ", EOP_INI(eneini_turn_around), enevl_turn_around, eneop_turn_around),
    EOP("ROUND REAR  ", NULL, enevl_round_rear, eneop_round_rear),
    EOP("RET INITPOS ", NULL, enevl_ret_initpos, eneop_ret_initpos),
    EOP("TURN TO ENE ", NULL, enevl_stay, eneop_stay),
    EOP("FLY REAR    ", NULL, enevl_fly_rear, eneop_fly_rear),
    EOP("BACK AWAY 2 ", EOP_INI(eneini_back), enevl_back2, eneop_back),
    EOP("TURN TO ENE2", NULL, enevl_stay2, eneop_stay),
    EOP("CHARGE      ", NULL, enevl_charge, eneop_charge),
    EOP("ROUND APP   ", NULL, enevl_round_app, eneop_round_app),
    EOP("STEP ASIDE  ", EOP_INI(eneini_step_aside), enevl_step_aside, eneop_step_aside),
    EOP("APPROACH 2  ", EOP_INI(eneini_approach2), enevl_approach2, eneop_approach2),
    EOP("FINAL ATTACK", NULL, enevl_final_attack, eneop_final_attack),
    EOP("ROUND REAR 2", EOP_INI(eneini_default), enevl_round_rear2, eneop_round_rear2),
    EOP("TRACE ROUTE ", EOP_INI(eneini_default), enevl_trace_route, eneop_trace_route),
    EOP("ROUND BACK  ", NULL, enevl_round_back, eneop_round_back),
    EOP("ROUND REAR3 ", NULL, enevl_round_rear, eneop_round_rear3),
    EOP("RANDOM WALK ", EOP_INI(eneini_random_walk), enevl_true, eneop_random_walk),
    EOP("ATTACK", NULL, enevl_attack, eneop_attack),
    EOP("AWAY", NULL, enevl_away, eneop_away),
    EOP("AVOID WALL", NULL, enevl_avoid_wall, eneop_avoid_wall),
    EOP("ROUND REAR4", NULL, enevl_round_rear4, eneop_round_rear),
    EOP("TURN AROUND2", NULL, enevl_turn_around2, eneop_turn_around2),
    EOP("TRACE ROUTE2", EOP_INI(eneini_default), enevl_trace_routeMk2, eneop_trace_routeMk2),
    EOP("AREA ALERT  ", NULL, enevl_area_alert, eneop_area_alert),
    EOP("ROUND FRONT", EOP_INI(eneini_default), enevl_round_front, eneop_round_front),
    EOP("RUNAWAY", EOP_INI(eneini_default), enevl_round_front, eneop_runaway),
    EOP("VSENE ROUTE", NULL, enevl_vsene_route, eneop_vsene_route),
    EOP("SEEK ROUTE", NULL, enevl_seek_route, eneop_seek_route),
    EOP("UP DOWN", NULL, enevl_up_down, eneop_up_down),
    EOP("@RETURN RGN", NULL, enevl_return_rgn, eneop_return_rgn),
    EOP("JUMP TO TRG", NULL, enevl_jump_to_target, eneop_jump_to_target),
};
#undef EOP
#endif

void eneop_stay(EnemySt *ep, EneInfoSt *einfo)
{
    enepad_turn_to_ene(ep, einfo);   /* original: `j enepad_turn_to_ene` */
}

float enevl_stay(EnemySt *ep, EneInfoSt *einfo)
{
    (void)ep;
    return einfo->emp ? 1.0f : 0.0f;
}

void ene_execute_operation(EnemySt *ep, StModel *mp, EneInfoSt *eip)
{
    PS2_FRAME(0x40);
    (void)mp;
    EnemyOpSt *op = ep->cur_op;
    if (!op) return;
    if (op->mainfunc) {
        if (ep->cur_opl) {
            eip->op_arg1 = ep->cur_opl->arg1;
            eip->op_arg2 = ep->cur_opl->arg2;
        } else {
            eip->op_arg1 = 0.0f;
            eip->op_arg2 = 0.0f;
        }
        gcall(op->mainfunc, ep, eip);
    }
    if (ep->edat->leg_type != 4) return;
    if (ep->new_pad & 0x2000) return;
    if (ep->new_pad & 0x4000) return;
    float dh = get_diff_height(ep, eip);
    if (dh == 0.0f) return;                  /* c.eq: NaN goes on */
    float r = ps2_div((float)(ps2_rand() & 0xffff), ps2_u2f(0x477fff00) /* 65535 */);
    dh = dh * (0.5f + r);
    if (!(dh <= 2.0f)) ep->new_pad |= 0x2000;
    else if (dh < -2.0f) ep->new_pad |= 0x4000;
}

void ene_operation_init(EnemySt *ep)
{
    PS2_FRAME(0x10);
    StModel *mp = ep->mp;
    MarkerSt *m = Array->marker;
    float best = ps2_u2f(0x7f7fffff);
    char idx = -1;
    for (int i = 0; i < 128; i++) {          /* all 128 slots, not the stage's marker count */
        if (m[i].type != 2) continue;
        float d[4];
        vu_sub_xyz(d, m[i].loc, mp->loc);
        float x2 = d[0] * d[0], y2 = d[1] * d[1], z2 = d[2] * d[2];
        float dist2 = z2 + (x2 + y2);
        if (dist2 < best) { best = dist2; idx = (char)i; }
    }
    ep->return_rgn_id = idx;
}

/* AREA ALERT score: 1 while the enemy is outside its patrol rectangle */
float enevl_area_alert(EnemySt *ep)
{
    PS2_FRAME(0x10);
    return DFene_CheckInRegionEx(ep->mp->loc, ep->region[1][0], ep->region[0][0], ep->region[1][1],
                                 ep->region[0][1]) ? 1.0f : 0.0f;
}

/* BACK AWAY: face the target, walk backwards with a random sidestep, re-rolled every 30..105 frames */
void eneop_back(EnemySt *ep, EneInfoSt *einfo)
{
    PS2_FRAME(0x30);
    enepad_turn_to_ene(ep, einfo);
    ep->new_pad |= 0x40;
    if (ep->op_wk[0] == 2) ep->new_pad |= 0x800;
    else if (ep->op_wk[0] == 1) ep->new_pad |= 0x400;
    if (ep->op_wk[1] > 0) {
        ep->op_wk[1] = (short)(ep->op_wk[1] - 1);
    } else {
        ep->op_wk[0] = (short)((ps2_rand() % 0xc000) >> 14);              /* 0..2 */
        ep->op_wk[1] = (short)(((ps2_rand() % 0x18000) >> 14) * 15 + 30); /* 30, 45, .. 105 */
    }
    enepad_value_walk_dash_fly(ep, einfo);
}

float enevl_back(EnemySt *ep, EneInfoSt *einfo)
{
    PS2_FRAME(0x10);
    (void)ep;
    if (!einfo->emp) return 0.0f;
    return eval_near(einfo->elenxz, einfo->op_arg1, einfo->op_arg2);
}

/* CHARGE: run at the target */
void eneop_charge(EnemySt *ep, EneInfoSt *einfo)
{
    PS2_FRAME(0x30);
    if (!einfo->emp) return;
    enepad_turn_to_ene(ep, einfo);
    enepad_loc_move(ep, einfo->emp->loc, 10.0f);
    enepad_value_walk_dash_fly(ep, einfo);
}

float enevl_charge(EnemySt *ep, EneInfoSt *einfo)
{
    (void)ep;
    return einfo->emp ? 1.0f : 0.0f;
}

/* TURN AROUND: circle the target sideways; the direction flips after 2..4 damaged frames */
void eneop_turn_around(EnemySt *ep, EneInfoSt *einfo)
{
    PS2_FRAME(0x20);
    enepad_turn_to_ene(ep, einfo);
    ep->new_pad |= (ep->op_wk[0] == 0) ? 0x400 : 0x800;
    if (ep->op_wk[1] > 0) {
        if (ep->bmd_dmg > 0) ep->op_wk[1] = (short)(ep->op_wk[1] - 1);   /* counts only on frames with damage */
    } else {
        ep->op_wk[0] = (short)(ep->op_wk[0] ^ 1);
        ep->op_wk[1] = (short)(((ps2_rand() % 0xc000) >> 14) + 2);
    }
}

float enevl_turn_around(EnemySt *ep, EneInfoSt *einfo)
{
    (void)ep;
    return einfo->emp ? 1.0f : 0.0f;
}

void eneini_turn_around(EnemySt *ep)
{
    PS2_FRAME(0x20);
    ep->op_wk[0] = (short)((ps2_rand() % 0x8000) >> 14);
    ep->op_wk[1] = (short)(((ps2_rand() % 0xc000) >> 14) + 2);
}

float enevl_standby(void)
{
    return 1.0f;
}

/* ---- sandbox gE (s103): the ene004 approach / attack / rear operations ---- */

/* ATTACK score: 1 beyond op_arg2, 0 inside op_arg1, linear in between */
float enevl_attack(EnemySt *ep, EneInfoSt *eip)
{
    PS2_FRAME(0x10);
    (void)ep;
    if (!eip->emp) return 0.0f;
    return eval_far(eip->elenxz, eip->op_arg1, eip->op_arg2);
}

#ifdef ACNB_SOURCEPORT
unsigned short eneop_approach2__pad_tab[5] = {0x0010, 0x0410, 0x0810, 0x0400, 0x0800};
#endif

/* APPROACH 2: face the target and walk forward / diagonally / sideways (pad_tab[op_wk[0]]); the direction is
 * re-rolled when the 30..150 frame timer runs out (the OR uses the old direction, the new one applies next frame).
 * Quirk: op_wk[0] indexes pad_tab unchecked (op_wk is scratch shared with other operations). */
void eneop_approach2(EnemySt *ep, EneInfoSt *einfo)
{
    PS2_FRAME(0x30);
    enepad_turn_to_ene(ep, einfo);
    ep->new_pad |= eneop_approach2__pad_tab[ep->op_wk[0]];
    if (ep->op_wk[1] > 0) {
        ep->op_wk[1] = (short)(ep->op_wk[1] - 1);
    } else {
        ep->op_wk[0] = (short)((ps2_rand() % 0x14000) >> 14);              /* 0..4 */
        ep->op_wk[1] = (short)(((ps2_rand() % 0x14000) >> 14) * 30 + 30); /* 30, 60, .. 150 */
    }
    enepad_value_walk_dash_fly(ep, einfo);
}

void eneini_approach2(EnemySt *ep)
{
    PS2_FRAME(0x20);
    ep->op_wk[0] = (short)((ps2_rand() % 0x14000) >> 14);
    ep->op_wk[1] = (short)(((ps2_rand() % 0x14000) >> 14) * 30 + 30);
}

float enevl_approach2(EnemySt *ep, EneInfoSt *einfo)
{
    PS2_FRAME(0x10);
    (void)ep;
    if (!einfo->emp) return 0.0f;
    return eval_far(einfo->elenxz, einfo->op_arg1, einfo->op_arg2);
}

/* FLY REAR score: eval_near of the distance, weighted by how far the target faces away (1 behind, 0 head-on) */
float enevl_fly_rear(EnemySt *ep, EneInfoSt *einfo)
{
    PS2_FRAME(0x20);
    (void)ep;
    if (!einfo->emp) return 0.0f;
    float a = einfo->etoc_ang_y;
    if (a < 0.0f) a = -a;                    /* NaN: kept */
    float r = eval_near(einfo->elenxz, einfo->op_arg1, einfo->op_arg2);
    return (1.0f - ps2_div(a, ps2_u2f(0x40490fdb))) * r;
}

/* ROUND REAR (also ROUND REAR3 / REAR4): circle toward the target's back */
void eneop_round_rear(EnemySt *ep, EneInfoSt *einfo)
{
    PS2_FRAME(0x30);
    enepad_round_rear(ep, einfo);
    enepad_value_walk_dash_fly(ep, einfo);
}

float enevl_round_rear(EnemySt *ep, EneInfoSt *einfo)
{
    (void)ep;
    if (!einfo->emp) return 0.0f;
    float a = einfo->etoc_ang_y;
    if (a < 0.0f) a = -a;
    return 1.0f - ps2_div(a, ps2_u2f(0x40490fdb));
}

/* Pick the operation for this frame from the logic level's list: score = rate * calcfunc (randomised by 0.75..1
 * for rates < 10, the running entry weighted by enefz_time and, when stuck, by fz_small); the best positive score
 * wins, a negative score forces its op. The running op is kept until its min time is over, AREA ALERT overrides. */
void ene_select_operation2(EnemySt *ep, EneInfoSt *eip)
{
    PS2_FRAME(0xb0);
    int notyet = 0;
    EnemyLogicSt *lg = (EnemyLogicSt *)((char *)ep->logp + ((int)ep->p_lv << 8));
    EnemyOpListSt *e = lg->op_list;
    if (lg->canwep != 0 && (ep->wep_valid & 1) && ep->cur_wep == (int)lg->canwep - 1) {
        ep->cur_op = &ene_op_shooting[0];   /* forced SHOOTING op */
        return;
    }
    if (ep->cur_op == &ene_op_shooting[0] && ep->cur_opl) ep->cur_op = &ene_op_tab[ep->cur_opl->opid];   /* unchecked */
    EnemyOpSt *best_op = ep->cur_op;
    EnemyOpListSt *best_opl = ep->cur_opl;
    int min_frm = 0;
    float best = 0.0f;
    if (ep->op_time < ep->op_time_min) notyet = 1;
    for (int i = 0; i < 7; i++, e++) {
        unsigned int id = e->opid;
        if (id >= 0x22) continue;           /* skipped without the end-of-list test */
        EnemyOpSt *op = &ene_op_tab[id];
        if (!op->calcfunc) continue;
        if (notyet && op->opname[0] != '@') continue;   /* only '@' ops compete while the min time runs */
        eip->op_arg1 = e->arg1;
        eip->op_arg2 = e->arg2;
        float rate = e->rate;
        float sc = rate * gcall(op->calcfunc, ep, eip);
        if (rate < 10.0f) {
            float q = ps2_div((float)(ps2_rand() & 0xffff), ps2_u2f(0x477fff00) /* 65535 */);
            sc = sc * (0.75f + 0.25f * q);
        }
        if (ep->cur_opl == e) {             /* the entry that is running now */
            sc = sc * enefz_time(ep->op_time, ep->op_time_min);
            StModel *mp = ep->mp;
            float dy = mp->loc[1] - ep->oloc[1];
            float dz = mp->loc[2] - ep->oloc[2];
            float dx = mp->loc[0] - ep->oloc[0];
            float x2 = dx * dx, y2 = dy * dy;
            float d2 = (x2 + y2) + dz * dz;  /* adda / madd */
            int stuck;
            if (d2 < ps2_u2f(0x3c23d70b) /* 0.01 */) {
                ep->cant_move = (unsigned char)(ep->cant_move + 1);   /* a byte: wraps at 256 */
                stuck = ep->cant_move >= 0x1f;
            } else {
                ep->cant_move = 0;
                stuck = 0;
            }
            if (stuck) sc = sc * fz_small((float)ep->cant_move, 60.0f, 30.0f);
        }
        if (sc < 0.0f) {                    /* a negative score forces this op */
            min_frm = e->min_frm; best = sc; best_op = op; best_opl = e;
            break;
        }
        if (best < sc) { min_frm = e->min_frm; best = sc; best_op = op; best_opl = e; }
        if (e->opid == 0) break;            /* opid 0 ends the list after being scored */
    }
    if (0.0f < best && ep->op_time < ep->op_time_min) {
        best_opl = ep->cur_opl;             /* keep the running op until its min time is over */
        best_op = ep->cur_op;
    } else if (0.0f < best && !ep->sw.noarea && !ep->sw2.int_op) {
        EnemyOpSt *a = &ene_op_shooting[1];
        if (a->calcfunc) {
            eip->op_arg1 = a->arg1;
            eip->op_arg2 = a->arg2;
            if (!(gcall(a->calcfunc, ep, eip) <= best)) { best_op = a; min_frm = 200; }   /* cur_opl not updated */
        }
    }
    if (0.0f < best && ep->sw2.int_op) {
        EnemyOpSt *a = &ene_op_shooting[1];
        if (a->calcfunc) {
            eip->op_arg1 = a->arg1;
            eip->op_arg2 = a->arg2;
            if (!(10.0f * gcall(a->calcfunc, ep, eip) <= best)) { best_op = a; min_frm = 200; }
        }
    }
    if (ep->cur_op == best_op) {
        if (ep->op_time < 0x7fff) ep->op_time = (unsigned short)(ep->op_time + 1);
        return;
    }
    ep->cur_opl = best_opl;
    ep->cur_op = best_op;
    int half = min_frm >> 1;
    ep->op_time = 0;
    int r = 0;
    if (half > 0) r = (ps2_rand() % (half << 14)) >> 14;
    ep->op_time_min = (unsigned short)((min_frm - (min_frm >> 2)) + r);
    if (best_op->initfunc) gcall(best_op->initfunc, ep, eip);
}
