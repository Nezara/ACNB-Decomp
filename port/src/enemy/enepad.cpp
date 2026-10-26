/* src/enemy/enepad.cpp (OLSORTIE) - the enemy "pad" layer: think / operation code does not move an enemy directly,
 * it presses virtual buttons in ep->new_pad that the control function (e094_control, ...) turns into movement.
 * garage3 layer 1: enepad_value_walk_dash_fly (walk / dash / fly choice every 30 frames + the boost / climb bits),
 * enepad_turn_to_ene (turn / look toward the target) and enepad_loc_move (head for a location); layer 2:
 * enepad_PadManage (ECM delay of the shoot / blade presses) and enepad_ang_move (direction pad for a heading). The
 * other pad functions stay interpreted for now. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"

void enepad_value_walk_dash_fly(EnemySt *ep, EneInfoSt *eip)
{
    PS2_FRAME(0x50);
    if (FRAME % 30u == 0) {
        EnemyDataSt *edat = ep->edat;
        float boost;
        if (edat->flag & 0x2000) {
            boost = 1.0f;
        } else if (edat->booster > 0) {
            if (!(eip->mv_value[1] <= 1.0f) || !(eip->mv_value[2] <= 1.0f)) {
                boost = 1.0f;
            } else {
                if (ep->mv_type == 0) {
                    boost = ene_boost_rate(ep);
                } else {
                    boost = 4.0f * ene_boost_rate(ep);
                    if (!(boost <= 1.0f)) boost = 1.0f;
                }
                float a = eip->etoc_ang_y;
                if (a < 0.0f) a = -a;
                float f = 1.0f - ps2_div(a - ps2_u2f(0x3f490fdb) /* pi/4 */, ps2_u2f(0x4016cbe4) /* 3pi/4 */);
                if (f < 0.0f) f = 0.0f;
                else if (!(f <= 1.0f)) f = 1.0f;
                boost = boost * f;
            }
        } else {
            boost = 0.0f;
        }
        float mv[4];
        mv[0] = eip->mv_value[0];
        float r = ps2_div((float)(ps2_rand() & 0xffff), ps2_u2f(0x477fff00) /* 65535 */);
        mv[1] = boost * (eip->mv_value[1] + r);
        r = ps2_div((float)(ps2_rand() & 0xffff), ps2_u2f(0x477fff00));
        mv[2] = boost * (eip->mv_value[2] + r);
        mv[3] = eip->mv_value[3];
        int idx = 0;
        float best = 0.0f;
        for (int i = 0; i < 4; i++) {
            if (!(mv[i] <= best)) { best = mv[i]; idx = i; }
        }
        ep->mv_type = (unsigned char)idx;
    }
    switch (ep->mv_type) {
    case 1: {   /* dash */
        unsigned int leg = ep->edat->leg_type;
        if (leg == 0 || leg == 3) {
            unsigned short pad = ep->new_pad;
            if ((pad & 0xc50) && ep->bst != 3) ep->new_pad = pad | 0x4000;
        }
        break;
    }
    case 2: {   /* fly: climb to the target height */
        StModel *emp = eip->emp;
        EnemyDataSt *edat = ep->edat;
        if (emp && ((emp->loc[1] - eip->fly_height) - ep->mp->loc[1]) < 0.0f) {
            ep->new_pad |= 0x4000;
            unsigned int leg = edat->leg_type;
            if (leg == 0 || leg == 3) {
                if (ep->bst == 1) {
                    if (ep->old_pad & 0x4000) ep->new_pad &= 0xbfff;
                } else if (ep->bst != 3) {
                    ep->new_pad &= 0xf30f;
                }
            }
        }
        break;
    }
    default:
        break;
    }
}

void enepad_turn_to_ene(EnemySt *ep, EneInfoSt *einfo)
{
    const float d = ps2_u2f(0x3db2b8c2);    /* 5 degrees */
    const float nd = ps2_u2f(0xbdb2b8c2);
    float f = einfo->ctoe_ang_y - ep->turn_spd;
    if (!(f <= d)) ep->new_pad |= 0x20;
    else if (f < nd) ep->new_pad |= 0x80;
    f = einfo->ctoe_ang_x - ep->look_spd;
    if (!(f <= d)) ep->new_pad |= 0x100;
    else if (f < nd) ep->new_pad |= 0x200;
}

void enepad_loc_move(EnemySt *ep, float *loc, float range)
{
    PS2_FRAME(0x20);
    float r2 = range * range;
    StModel *mp = ep->mp;
    float dx = loc[0] - mp->loc[0];
    float dz = loc[2] - mp->loc[2];
    float dx2 = dx * dx, dz2 = dz * dz;     /* mula / madd: products rounded, then added */
    if (dx2 + dz2 < r2) return;
    enepad_ang_move(ep, ps2_atan2f(dx, dz));
}

/* float -> unsigned int as MWCC compiles it (cvt.w.s rounds to zero; values >= 2^31 go through the subtract) */
static inline unsigned int enepad_ftou(float f)
{
    const float two31 = ps2_u2f(0x4f000000);
    if (two31 <= f) return (unsigned int)ps2_ftoi(f - two31) | 0x80000000u;
    return (unsigned int)ps2_ftoi(f);
}

/* ECM delay of the shoot (0x8000) / blade (0x2000) presses: while ecm_affected != 0 a press is swallowed and fired
 * ecm_affected frames later. */
void enepad_PadManage(EnemySt *ep)
{
    struct_065707b3 *s = &ep->pad_mng_shoot, *b = &ep->pad_mng_blade;
    int sflag = 0, bflag = 0;
    if (s->wait_time == 0 && s->key_state == 1) {   /* a delayed press fires now */
        sflag = 1;
        ep->new_pad |= 0x8000;
        s->key_state = 0;
    }
    if (b->wait_time == 0 && b->key_state == 1) {
        bflag = 1;
        ep->new_pad |= 0x2000;
        b->key_state = 0;
    }
    if (ep->ecm_affected == 0.0f) return;           /* c.eq: NaN goes on */
    if (s->key_state == 1) {
        s->wait_time = (unsigned char)(s->wait_time - 1);
        ep->new_pad &= 0x7fff;
    } else if (sflag == 0 && (ep->new_pad & 0x8000)) {
        ep->new_pad &= 0x7fff;
        s->key_state = 1;
        s->wait_time = (unsigned char)enepad_ftou(ep->ecm_affected);
    }
    if (b->key_state == 1) {
        b->wait_time = (unsigned char)(b->wait_time - 1);
        ep->new_pad &= 0xdfff;
        return;
    }
    if (bflag == 0 && (ep->new_pad & 0x2000)) {
        ep->new_pad &= 0xdfff;
        b->key_state = 1;
        b->wait_time = (unsigned char)enepad_ftou(ep->ecm_affected);
    }
}

/* press the direction pad for a heading: the wrapped difference to the view angle picks one of eight sectors */
void enepad_ang_move(EnemySt *ep, float go_angle)
{
    PS2_FRAME(0x20);
    float d = sub_angf(go_angle, ep->view.angy);
    unsigned short bits;
    if (d < ps2_u2f(0xc02feddf)) bits = 0x040;        /* -2.7488 */
    else if (d < ps2_u2f(0xbffb53d1)) bits = 0x440;   /* -1.9635 */
    else if (d < ps2_u2f(0xbf96cbe4)) bits = 0x400;   /* -1.1781 */
    else if (d < ps2_u2f(0xbec90fdb)) bits = 0x410;   /* -0.3927 */
    else if (d < ps2_u2f(0x3ec90fdb)) bits = 0x010;   /* +0.3927 */
    else if (d < ps2_u2f(0x3f96cbe4)) bits = 0x810;   /* +1.1781 */
    else if (d < ps2_u2f(0x3ffb53d1)) bits = 0x800;   /* +1.9635 */
    else if (d < ps2_u2f(0x402feddf)) bits = 0x840;   /* +2.7488 */
    else bits = 0x040;                                /* NaN too */
    ep->new_pad |= bits;
}

/* ROUND REAR pad: turn toward the target (same dead zone as enepad_turn_to_ene, including its speed-minus-angle
 * oddity) and strafe / walk so as to circle round to the target's back, chosen from the target-to-enemy angle in
 * pi/8 .. 7pi/8 bands. Quirks: NaN etoc_ang_y presses no strafe bit; -0.0 takes the positive chain. */
void enepad_round_rear(EnemySt *ep, EneInfoSt *einfo)
{
    const float d = ps2_u2f(0x3db2b8c2);    /* 5 degrees */
    const float nd = ps2_u2f(0xbdb2b8c2);
    float f = einfo->ctoe_ang_y - ep->turn_spd;
    const float a = einfo->etoc_ang_y;
    if (!(f <= d)) ep->new_pad |= 0x20;
    else if (f < nd) ep->new_pad |= 0x80;
    f = einfo->ctoe_ang_x - ep->look_spd;
    if (!(f <= d)) ep->new_pad |= 0x100;
    else if (f < nd) ep->new_pad |= 0x200;
    if (!(a < 0.0f)) {
        if      (a < ps2_u2f(0x3ec90fdb)) ep->new_pad |= 0x400;
        else if (a < ps2_u2f(0x3f490fdb)) ep->new_pad |= 0x410;
        else if (a < ps2_u2f(0x3fc90fdb)) ep->new_pad |= 0x010;
        else if (a < ps2_u2f(0x3ffb53d1)) ep->new_pad |= 0x810;
        else if (a < ps2_u2f(0x402feddf)) ep->new_pad |= 0x800;
    } else {
        if      (!(a <= ps2_u2f(0xbec90fdb))) ep->new_pad |= 0x800;
        else if (!(a <= ps2_u2f(0xbf490fdb))) ep->new_pad |= 0x810;
        else if (!(a <= ps2_u2f(0xbfc90fdb))) ep->new_pad |= 0x010;
        else if (!(a <= ps2_u2f(0xbffb53d1))) ep->new_pad |= 0x410;
        else if (!(a <= ps2_u2f(0xc02feddf))) ep->new_pad |= 0x400;
    }
}
