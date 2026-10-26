/* src/enemy/shoot.cpp (OLSORTIE) - enemy weapon firing.
 *   ene_shoot: called by an enemy's control function every frame (fire = the pad's fire bit); picks the current
 *     weapon's table row (edat->wep_tab[cur_wep]), derives the first muzzle vertex / count and calls ene_shoot_ex.
 *   ene_shoot_ex: the firing state machine - weapon-valid / reload (waittime) / burst (am_time) timers, a line-of-fire
 *     test against walls and characters, burst timing from edat->am_tab, and the spawn (DFene_Bullet2Ex, one spell
 *     per muzzle vertex). Returns 1 on the tick a burst ends.
 *   EneShoot_GetEstimatePos: lead-target solver (quadratic intercept, optional random jitter on the solved time).
 * The w lane of the aim point reaches the spell's target (target[3] == 1.0f tests): it is kept exactly per branch. */
#include <string.h>
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"

#define SHOOT_PI    0x1.921fb6p+1f   /* 0x40490fdb */
#define SHOOT_2PI   0x1.921fb6p+2f   /* 0x40c90fdb */
#define SHOOT_NPI  -0x1.921fb6p+1f   /* 0xc0490fdb */

/* loc = tloc + tvel * t for the intercept time t of a shot of the given speed fired from wloc (t scaled by a random
 * (100 + r2 - r1) / 100 jitter when prec > 0). No real solution or a == 0: loc = tloc (all four lanes). The solved
 * branch leaves loc[3] = tvel[3]. ang != NULL: ang = direction angles wloc -> loc. */
void EneShoot_GetEstimatePos(float *loc, float *ang, float *tloc, float *tvel, float *wloc, float speed, short prec)
{
    PS2_FRAME(0xb0);
    float d[4] __attribute__((aligned(16)));
    d[0] = tloc[0] - wloc[0];
    d[1] = tloc[1] - wloc[1];
    d[2] = tloc[2] - wloc[2];
    d[3] = tloc[3];
    int r1 = (prec > 0) ? ps2_mod32(ps2_rand(), prec << 14) >> 14 : 0;
    int r2 = (prec > 0) ? ps2_mod32(ps2_rand(), prec << 14) >> 14 : 0;
    float k = ps2_div(100.0f + (float)(r2 - r1), 100.0f);
    float vx = tvel[0], vy = tvel[1], vz = tvel[2];
    float a = vy * vy + vx * vx;
    a = vz * vz + a;
    a = a - speed * speed;
    float b = d[0] * vx + d[1] * vy;
    b = b + d[2] * vz;
    float c = d[0] * d[0] + d[1] * d[1];
    c = c + d[2] * d[2];
    float disc = b * b - a * c;
    if (disc < 0.0f) {
        memcpy(loc, tloc, 16);
        PS2_VF_MIRROR(16, d);
        PS2_VF_MIRROR(17, wloc);
    } else {
        float s = ps2_sqrt(disc);
        if (0.0f == a) {
            memcpy(loc, tloc, 16);
            PS2_VF_MIRROR(16, d);
            PS2_VF_MIRROR(17, wloc);
        } else {
            float t = ps2_div(-b - s, a) * k;
            float w = tvel[3];
            loc[0] = tvel[0] * t;
            loc[1] = tvel[1] * t;
            loc[2] = tvel[2] * t;
            loc[3] = w;
            loc[0] = loc[0] + tloc[0];
            loc[1] = loc[1] + tloc[1];
            loc[2] = loc[2] + tloc[2];
            PS2_VF_MIRROR(16, loc);
            PS2_VF_MIRROR(17, tloc);
        }
    }
    if (ang) {
        float v2[4] __attribute__((aligned(16)));
        v2[0] = loc[0] - wloc[0];
        v2[1] = loc[1] - wloc[1];
        v2[2] = loc[2] - wloc[2];
        v2[3] = loc[3];
        PS2_VF_MIRROR(16, v2);
        PS2_VF_MIRROR(17, wloc);
        vec2ang(ang, v2);
    }
}

/* wep_ext = (wep->func >> 5) & 7, passed by ene_shoot but never read (dropped from DWARF; protofix.py) */
int ene_shoot_ex(EnemySt *ep, StModel *mp, StModel *tgmp, float *vec, int type, int scd, int charid, int vtx,
                 int cnt, int spl_id, int reload, int wep_ext, SfxDstat *dstat, int fire)
{
    PS2_FRAME(0x160);
    (void)wep_ext;
    EnemyDataSt *edat = ep->edat;
    if (ep->cur_wep < 0 || ep->cur_wep >= 5) return 0;
    if (ep->wep_valid & (1 << (ep->cur_wep + 2))) {
        ep->waittime = 0;
        ep->am_time = 0;
        return 0;
    }
    if (ep->waittime != 0) {
        ep->waittime = (unsigned short)(ep->waittime - 1);
    } else if (ep->am_time == 0 && fire) {
        /* fire requested, no timers running: state gates */
        if (type == 2 && ep->bst != 0xb) return 0;
        if ((type == 7 || type == 10) && ep->bst != 0x15) return 0;
        if (type == 0xb && ep->bst != 0x18) return 0;
        if (type == 0xc && ep->bst != 0x1a) return 0;
        if (type != 1 && type != 7 && type != 10 && tgmp) {
            /* line of fire from the shooter's model (ep->mp, not the mp argument) at 3/4 height */
            float loc0[4] __attribute__((aligned(16)));
            float loc1[4] __attribute__((aligned(16)));
            float hv[4] __attribute__((aligned(16)));
            StModel *emp = ep->mp;
            memcpy(loc0, emp->loc, 16);
            memcpy(loc1, tgmp->loc, 16);
            loc0[1] = loc0[1] - 0.75f * emp->tall;
            loc1[1] = loc1[1] - 0.75f * tgmp->tall;
            unsigned short cf = emp->cflag;
            int rm = (cf == 1) ? 6 : (cf == 2) ? 5 : 3;
            if (HitChrIsHitLineChr(loc0, loc1, ep->id, rm)) {
                ep->waittime = (unsigned short)reload;
                return 0;
            }
            if (!ep->sw2.blind && !ene_blindshot_enable(ep, ep->cur_wep)) {
                if (HetProcPoint2Point(loc0, loc1, hv, 3)) {
                    ep->waittime = (unsigned short)reload;
                    return 0;
                }
            }
        }
        struct struct_4c966133 *am = &edat->am_tab[(int)ep->cur_wep];
        if ((am->min == 0 && am->max == 0) || am->intvl == 0) {
            ep->am_time = 1;
        } else {
            int n = (int)am->max - (int)am->min;
            if (n < 0) n = -n;
            n = n + 1;
            int r = (n > 0) ? ps2_mod32(ps2_rand(), n << 14) >> 14 : 0;
            ep->am_time = (unsigned short)(am->intvl * (am->min + r));
        }
        ep->am_cnt = 0;
    }

    /* L840 */
    if (ep->am_time == 0) {
        ep->eff.shoot = 0;
        return 0;
    }
    unsigned int iv = edat->am_tab[(int)ep->cur_wep].intvl;
    int doshot = (iv > 0) ? (((unsigned int)ep->am_time % iv) == 0) : 1;
    if (doshot) {
        if (type == 9) {
            /* nothing spawned */
        } else if (type == 10) {
            if (tgmp) {
                vtx += ep->am_cnt;
                while (cnt--) {
                    if (ep->child) DFene_Bullet2Ex(ep, ep->child->id, spl_id, vtx, 0, tgmp, dstat);
                    if (ep->sibling) DFene_Bullet2Ex(ep, ep->sibling->id, spl_id, vtx, 0, tgmp, dstat);
                    vtx++;
                }
            }
        } else if (type == 7 || type == 1) {
            if (tgmp) {
                while (cnt--) DFene_Bullet2Ex(ep, charid, spl_id, vtx++, 0, tgmp, dstat);
            }
        } else {
            float loc[4] __attribute__((aligned(16)));
            if (tgmp && type != 3) {
                if (vec == 0) {
                    DF_GetModelCenter(loc, tgmp);
                } else {
                    float tloc[4] __attribute__((aligned(16)));
                    float wloc[4] __attribute__((aligned(16)));
                    float speed = SpellData2[spl_id].spell.speed;
                    DF_GetModelCenter(tloc, tgmp);
                    DF_GetModelCenter(wloc, mp);
                    EneShoot_GetEstimatePos(loc, 0, tloc, vec, wloc, speed, ep->fake_value_prec);
                }
            } else {
                /* straight ahead: 10000 along the view vector from the shooter's center (loc.w = center w) */
                float dir[4] __attribute__((aligned(16)));
                DF_GetModelCenter(loc, mp);
                const float *vv = ep->view.vec;
                dir[0] = vv[0] * 10000.0f;
                dir[1] = vv[1] * 10000.0f;
                dir[2] = vv[2] * 10000.0f;
                dir[3] = vv[3];
                loc[0] = loc[0] + dir[0];
                loc[1] = loc[1] + dir[1];
                loc[2] = loc[2] + dir[2];
                PS2_VF_MIRROR(16, loc);
                PS2_VF_MIRROR(17, dir);
            }
            while (cnt--) DFene_Bullet2Ex(ep, charid, spl_id, vtx++, loc, 0, dstat);
        }
        if (scd) {
            DFene_Kill(ep, reload);
            return 0;
        }
        ep->am_cnt = (unsigned char)(ep->am_cnt + 1);
    }
    ep->eff.shoot = doshot & 1;
    ep->am_time = (unsigned short)(ep->am_time - 1);
    if (ep->am_time == 0) {
        ep->waittime = (unsigned short)reload;
        return 1;
    }
    return 0;
}

int ene_shoot(EnemySt *ep, StModel *mp, StModel *tgmp, float *vec, int fire)
{
    PS2_FRAME(0xc0);   /* stack arguments 9-14 of ene_shoot_ex at sp+0..0x28 */
    EneWepSt *wep = &ep->edat->wep_tab[(int)ep->cur_wep];
    int type = wep->_type & 0x3f;
    int vtx, cnt;
    if (type == 7 || type == 10) {
        vtx = wep->vtx_id & 0xf;
        cnt = ((wep->vtx_id >> 4) & 0xf) >> 1;
        if (ep->wep_valid & 2) vtx += cnt;
    } else {
        vtx = wep->vtx_id & 0xf;
        cnt = (wep->vtx_id >> 4) & 0xf;
        int f = wep->func & 0x1f;
        if (f == 5) {
            cnt >>= 1;
            if (ep->wep_valid & 2) vtx += cnt;
            ep->wep_valid ^= 2;
        } else if (f == 1) {
            cnt >>= 1;
            if (tgmp) {
                float a = ps2_atan2f(tgmp->loc[0] - mp->loc[0], tgmp->loc[2] - mp->loc[2]) - ep->view.angy;
                while (!(a <= SHOOT_PI)) a = a - SHOOT_2PI;
                while (a < SHOOT_NPI) a = a + SHOOT_2PI;
                if (!(a <= 0.0f)) vtx += cnt;
            } else if (ps2_mod32(ps2_rand(), 100) < 0x32) {
                vtx += cnt;
            }
        }
    }
    return ene_shoot_ex(ep, mp, tgmp, vec, type, wep->_type & 0x40, ep->id, vtx, cnt, wep->spl_id, wep->reload,
                        (wep->func >> 5) & 7, &wep->dstat, fire);
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper (gen_bridge stops at 8 int args); cnt / spl_id / reload / wep_ext / dstat / fire are
 * the caller's stack args at sp+0 .. sp+0x28 (8-byte slots) */
static void bw__ene_shoot_ex(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    int r = ene_shoot_ex((EnemySt *)bridge_ptr(c->gpr[A0].uw[0]), (StModel *)bridge_ptr(c->gpr[A1].uw[0]),
                         (StModel *)bridge_ptr(c->gpr[A2].uw[0]), (float *)bridge_ptr(c->gpr[A3].uw[0]),
                         c->gpr[T0].sw[0], c->gpr[T1].sw[0], c->gpr[T2].sw[0], c->gpr[T3].sw[0],
                         (int)rd32(sp), (int)rd32(sp + 8), (int)rd32(sp + 0x10), (int)rd32(sp + 0x18),
                         (SfxDstat *)bridge_ptr(rd32(sp + 0x20)), (int)rd32(sp + 0x28));
    c->gpr[V0].sd[0] = r;
}
#endif
