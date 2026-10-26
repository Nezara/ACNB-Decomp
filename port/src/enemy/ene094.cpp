/* src/enemy/ene094.cpp (OLSORTIE) - ground-walker control shared by the 094-family enemies (the garage3 test-room
 * enemies run it through Ene120_Initial's ctrlfunc; e051, e067, e073-e084, Ene017, Ene009, Ene093 reuse it).
 * e094_control snapshots the old state and runs the movement state machine e094_bst_mng (switch on the old bst,
 * driven by ep->new_pad: walk / dash / jump / fall / blade / shooting / hover states; returns the turn input
 * -1 / 0 / +1), then animation, movement (e094_ctrl_move), turning and the boost / hover sounds.
 * e094_walk_put_animSE plays the footstep sound on animation key frames; e094_se_hover keeps the hover-loop sound.
 * Whole file native (G-E1 e094_bst_mng / e094_on_blade, G-E2 the rest). garage3 runs only the walk paths: most of
 * e094_bst_mng and every e094_ctrl_move arm except bst 0 / 6 with leg_type 0 are unreached (asm-reviewed only). */
#include "acnb/port.h"
#include "acnb/sdk.h"

/* float bit patterns of the original's lui / ori constants */
#define E094_HOVER_D    0x1.1df46ap-7f      /* 0x3c0efa35 pi/360 */
#define E094_HOVER_P    0x1.aceea0p-3f      /* 0x3e567750 pi/15 */
#define E094_TURN_SPD   0x1.aceea0p-6f      /* 0x3cd67750 pi/120 */
#define E094_TURN_MAX   0x1.921fb6p-1f      /* 0x3f490fdb pi/4 */
#define E094_VIEW_ACC   0x1.12843cp-10f     /* 0x3a89421e */
#define E094_VIEW_SPD   0x1.c98710p-7f      /* 0x3c64c388 */
#define E094_VIEW_BRK   0x1.4cccccp-1f      /* 0x3f266666 0.65f */
#define E094_CLIMB_D    0x1.581062p-5f      /* 0x3d2c0831 0.042f */
#define E094_CLIMB_MAX  0x1.bd70a4p+0f      /* 0x3fdeb852 1.74f */
#define E094_PI         0x1.921fb6p+1f      /* 0x40490fdb */
#define E094_2PI        0x1.921fb6p+2f      /* 0x40c90fdb */
#define E094_SLOW       0x1.b33334p-1f      /* 0x3f59999a 0.85f */

/* booster -= boostcost as 16-bit (lh, lb, subu, sh, lh reload), clamped at 0 */
static inline void e094_boost_dec(EnemySt *ep)
{
    ep->booster = (short)(ep->booster - ep->boostcost);
    if (ep->booster < 0) ep->booster = 0;
}

/* turn input from the pad (cases 10-13 / 23 / 24) */
static inline float e094_pad_lr(unsigned int pad)
{
    if (pad & 0x80) return -1.0f;
    if (pad & 0x20) return 1.0f;
    return 0.0f;
}

void e094_on_blade(EnemySt *ep, StModel *mp, EnemyDataSt *edat)
{
    PS2_FRAME(0x10);
    (void)mp;
    int t = edat->wep_tab[4]._type & 0x3f;
    if (t != 4 && t != 5 && t != 8) return;
    if (ep->bst == 3) {
        if (t == 8) return;
        ep->bst = 8;
        ep->bld_time = 15;
    } else {
        ep->bst = 7;
        ep->bld_time = (t == 8) ? 20 : 15;
        ep->wep_valid &= 0xbf;
    }
    if (t == 4 || t == 5) DFene_Bullet3(ep, &edat->wep_tab[4], NULL, NULL);
}

/* blade swing countdown of cases 7 / 18 (bullet3: case 7 fires the type-8 blade wave on the key frame) */
static inline void e094_blade_tick(EnemySt *ep, StModel *mp, EnemyDataSt *edat, int bullet3)
{
    unsigned int b = ep->bld_time;
    if (b == 0) return;
    ep->bld_time = (unsigned char)(b - 1);
    int n = ((edat->wep_tab[4]._type & 0x3f) == 8) ? 10 : 5;
    if (ep->bld_time == n) {
        ene_ac_se_blade(ep, mp);
        if (bullet3 && (edat->wep_tab[4]._type & 0x3f) == 8) DFene_Bullet3(ep, &edat->wep_tab[4], NULL, NULL);
    } else if (n < (int)ep->bld_time && (edat->wep_tab[4].func & 0x1f) == 6 && (ep->new_pad & 0x4000) &&
               !(ep->wep_valid & 0x40) && ene_shoot_wave(ep, mp, &edat->wep_tab[3])) {
        ep->wep_valid |= 0x40;
    }
}

static float e094_bst_mng(EnemySt *ep, int old_bst, int old_dir)
{
    PS2_FRAME(0x70);
    EnemyDataSt *edat = ep->edat;
    StModel *mp = ep->mp;
    float lr = 0.0f;

    unsigned int rt = ep->react_type;
    if (rt) {
        if (old_bst == 0x1c || old_bst == 0x1d) {
            ep->bst = 0;                       /* react_type kept: re-enters the react state next frame */
        } else {
            if (rt == 1) ep->bst = 0x1c;
            else if (rt == 2) ep->bst = 0x1d;
            ep->react_type = 0;
        }
    }

    switch ((unsigned int)old_bst) {
    case 0: case 1: case 3: case 6: case 30: {
        ene_set_dir(ep);
        unsigned int lt = edat->leg_type;
        if ((lt == 5 || lt == 4) && ep->dir != 8) ep->eff.nowbst = 1;
        unsigned int pad = ep->new_pad;
        if (pad & 0x80) {
            lr = -1.0f;
            if (ep->bst == 0 && ep->dir == 8) ep->bst = 6;
            if (ep->bst == 6 && ep->dir != 8) ep->bst = 0;
        } else if (pad & 0x20) {
            lr = 1.0f;
            if (ep->dir == 8 && ep->bst == 0) ep->bst = 6;
            if (ep->bst == 6 && ep->dir != 8) ep->bst = 0;
        } else {
            lr = 0.0f;
            if (old_bst == 6) ep->bst = 0;
        }
        ene_shoot(ep, mp, ep->lkmp, ep->lkvec, ep->new_pad & 0x8000);
        if ((ep->new_pad & 0x8000) && ep->cur_wep >= 0 && ep->cur_wep < 5) {
            EneWepSt *wp = &edat->wep_tab[(int)ep->cur_wep];
            int t = wp->_type & 0x3f;
            if (t == 2) {
                if (ep->bst == 0 || ep->bst == 6) ep->bst = 10;
            } else if ((t == 7 || t == 10) && ep->lkmp && ep->waittime == 0) {
                ep->bst = 0x14;
            } else if (t == 0xb && ep->waittime == 0) {
                ep->bst = 0x17;
                ep->bld_time = (unsigned char)edat->cparam[0];
                ep->eff.nodamage = 1;
                DFene_Bullet2(ep, 0x10b, wp->vtx_id & 0xf, NULL, NULL, NULL);
                ep->vh[1] = (unsigned char)_DFene_SECall(&edat->se_tab[5], &mp->loc, 1.0f, 1);
            } else if (t == 0xc && ep->waittime == 0) {
                ep->bst = 0x19;
            }
        }
        if (ep->new_pad & 0x2000) e094_on_blade(ep, mp, edat);

        if (old_bst == 0x1e || old_bst == 3) {                     /* A3: falling / boosting up (0x1e33bd0) */
            unsigned int ld = ep->sw.landing;
            if (ld) {
                unsigned int l = edat->leg_type;
                if (l == 3 || l == 2 || l == 4) {
                    ep->bst = 0;
                } else if (ld == 2) {
                    ep->bst = 4;
                    _DFene_SECall(&edat->se_tab[2], &mp->loc, 1.0f, 0);
                } else if (ld == 1) {
                    ep->bst = 0;
                }
            } else if (ep->new_pad & 0x4000) {
                if (edat->leg_type == 5) {
                    ep->bst = 0xe;
                    _DFene_SECall(&edat->se_tab[5], &mp->loc, 1.0f, 0);
                } else if (ep->booster > 0) {
                    e094_boost_dec(ep);
                    float ra = edat->r_accel;
                    if (!(ra <= 0.0f)) {
                        ep->vel[1] = ep->vel[1] - (ra + ep->gravity);
                        float m = -edat->r_max;
                        if (ep->vel[1] < m) ep->vel[1] = m;
                        ep->eff.nowbst = 1;
                    }
                }
            }
        } else if (old_bst == 1) {                                 /* A1: dash (0x1e33ac8) */
            unsigned int dt = ep->dsh_time;
            if (dt) {
                ep->dsh_time = (unsigned char)(dt - 1);
                if (ep->dsh_time == 0) {
                    ep->bst = 5;
                } else if ((ep->new_pad & 0x4000) && (!(edat->r_accel <= 0.0f) || !(edat->jump <= 0.0f))) {
                    ep->bst = 3;
                    ep->dsh_time = 0;
                }
            } else {
                if (!(ep->new_pad & 0x4000)) {
                    ep->dsh_time = 13;
                    ep->dsh_dir = (char)old_dir;
                } else {
                    e094_boost_dec(ep);
                    if (ep->booster <= 0 || ep->dir == 8) {
                        ep->dsh_dir = (char)old_dir;
                        ep->bst = 5;
                    }
                }
                ep->eff.nowbst = 1;
            }
        } else if (old_bst == 0) {                                 /* A0: standing / walking (0x1e33990) */
            if (ep->new_pad & 0x4000) {
                if (edat->leg_type == 5) {
                    ep->bst = 0xe;
                    _DFene_SECall(&edat->se_tab[5], &mp->loc, 1.0f, 0);
                } else if (ep->dir == 8) {
                    if (lr == 0.0f) {
                        if (!(edat->jump <= 0.0f)) {
                            ep->bst = 2;
                        } else if (!(edat->r_accel <= 0.0f) && ep->booster > 0) {
                            e094_boost_dec(ep);
                            ep->bst = 3;
                        }
                    }
                } else if (ep->booster > 0) {
                    e094_boost_dec(ep);
                    unsigned int l = edat->leg_type;
                    ep->bst = (l == 3 || l == 0) ? 1 : 3;
                }
            }
        }
        /* 0xe is dead here (arm runs for 0 / 1 / 3 / 6 / 30 only) */
        if (old_bst != 3 && old_bst != 0xe && old_bst != 0x1e && ep->sw.landing == 0 && ep->chkfloor) ep->bst = 3;
        break;
    }
    case 2:
        if (Anim_IsEnd(ep->mp)) ep->bst = 3;
        break;
    case 4:
    case 5:
        if (Anim_IsEnd(ep->mp)) ep->bst = 0;
        break;
    case 7:
        ep->eff.nowbst = 1;
        if (Anim_IsEnd(ep->mp)) {
            ep->bst = 0x11;
            ep->bld_time = (unsigned char)(ep->edat->wep_tab[4].reload + 15);
        } else {
            e094_blade_tick(ep, mp, edat, 1);
        }
        break;
    case 8:
        if (Anim_IsEnd(ep->mp)) {
            ep->bst = 3;
            ep->bld_time = 15;
        } else if (ep->bld_time > 0) {
            ep->bld_time = (unsigned char)(ep->bld_time - 1);
            if (ep->bld_time == 5) ene_ac_se_blade(ep, mp);
        }
        break;
    case 9:
        break;
    case 10: case 11: case 12: case 13: {
        unsigned int pad = ep->new_pad;
        lr = e094_pad_lr(pad);
        ene_shoot(ep, mp, ep->lkmp, ep->lkvec, pad & 0x8000);
        int b = ep->bst;
        if (b == 13) {
            if (!(mp->op[0].hokanframe < mp->op[0].hokantime)) ep->bst = 11;
        } else if (b == 12) {
            if (Anim_IsEnd(mp)) ep->bst = 0;
        } else if (b == 11) {
            if (ep->eff.shoot) {
                ep->bst = 13;
            } else {
                EneWepSt *wt = edat->wep_tab;          /* cur_wep not range-checked: -1 reads wep_tab[-1] */
                if ((wt[(int)ep->cur_wep]._type & 0x3f) != 2 || (ep->new_pad & 0x4c50)) ep->bst = 12;
            }
        } else if (b == 10) {
            if (Anim_IsEnd(mp)) ep->bst = 11;
        }
        break;
    }
    case 14:
        if (Anim_IsEnd(mp)) {
            ep->bst = 16;
            mp->op[1].noanim = 1;
            ep->sw.flier = 1;
            if (edat->sfx[1] > 0) {
                if (ep->h_sfx2[0] == 0) ep->h_sfx2[0] = (unsigned short)DFene_SFX(ep, edat->sfx[1], 10);
                if (ep->h_sfx2[1] == 0) ep->h_sfx2[1] = (unsigned short)DFene_SFX(ep, edat->sfx[1], 11);
            }
        }
        break;
    case 15:
        if (Anim_IsEnd(mp)) {
            ep->bst = 0;
            mp->op[1].noanim = 0;
            ep->sw.flier = 0;
        }
        break;
    case 16: {                                                     /* hover (0x1e34340) */
        if (ep->new_pad & 0x4000) {
            ep->bst = 15;
            _DFene_SECall(&edat->se_tab[6], &mp->loc, 1.0f, 0);
            if (ep->h_sfx2[0]) {
                DF_MAGIC2_Kill_ID(ep->h_sfx2[0]);
                ep->h_sfx2[0] = 0;
            }
            if (ep->h_sfx2[1]) {
                DF_MAGIC2_Kill_ID(ep->h_sfx2[1]);
                ep->h_sfx2[1] = 0;
            }
        }
        ene_shoot(ep, mp, ep->lkmp, ep->lkvec, ep->new_pad & 0x8000);
        lr = e015_turn_cw(ep, mp, 1, E094_TURN_SPD, E094_TURN_MAX);
        unsigned int pad = ep->new_pad;
        float *o = &mp->op[1].angle[0];
        if (pad & 0x200) {
            float v = *o + E094_HOVER_D;
            *o = v;
            if (!(v <= E094_HOVER_P)) *o = E094_HOVER_P;
            ep->vel[1] = ep->vel[1] + edat->r_accel;
            float m = edat->r_max;
            if (!(ep->vel[1] <= m)) ep->vel[1] = m;
        } else if (pad & 0x100) {
            float v = *o - E094_HOVER_D;
            *o = v;
            if (v < -E094_HOVER_P) *o = -E094_HOVER_P;
            if (ep->sw.landing) ep->vel[1] = ep->vel[1] - edat->fit_height;
            ep->vel[1] = ep->vel[1] - edat->r_accel;
            float m = -edat->r_max;
            if (ep->vel[1] < m) ep->vel[1] = m;
        } else {
            float a = *o;
            if (!(a <= E094_HOVER_D)) *o = a - E094_HOVER_D;
            else if (a < -E094_HOVER_D) *o = a + E094_HOVER_D;
            else *o = 0.0f;                                        /* sw zero: +0 */
            ep->vel[1] = ep->vel[1] * edat->brake;
        }
        ep->dir = 0;
        ep->eff.nowbst = 1;
        break;
    }
    case 17:
        if (ep->bld_time > 0) {
            ep->bld_time = (unsigned char)(ep->bld_time - 1);
            if ((ep->new_pad & 0x2000) && (int)ep->edat->wep_tab[4].reload < (int)ep->bld_time &&
                (edat->wep_tab[4]._type & 0x3f) == 5) {
                DFene_Bullet2(ep, edat->wep_tab[4].spl_id, 0xe, NULL, NULL, &edat->wep_tab[4].dstat);
                ep->bst = 0x12;
                ep->bld_time = 15;
                ep->wep_valid &= 0xbf;
            }
        } else {
            ep->bst = 0;
        }
        break;
    case 18:
        ep->eff.nowbst = 1;
        if (Anim_IsEnd(ep->mp)) {
            ep->bst = 0x13;
            ep->bld_time = (unsigned char)(ep->edat->wep_tab[4].reload + 15);
        } else {
            e094_blade_tick(ep, mp, edat, 0);
        }
        break;
    case 19:
        if (ep->bld_time > 0) ep->bld_time = (unsigned char)(ep->bld_time - 1);
        else ep->bst = 0;
        break;
    case 20:
        if (Anim_IsEnd(mp)) ep->bst = 0x15;
        break;
    case 21:
        if (ene_shoot(ep, mp, ep->lkmp, ep->lkvec, 1)) ep->bst = 0x16;
        break;
    case 22:
        if (Anim_IsEnd(mp)) {
            ep->bst = 0;
            ep->wep_valid ^= 2;
        }
        break;
    case 23:
        lr = e094_pad_lr(ep->new_pad);
        if (ep->bld_time > 0) {
            ep->bld_time = (unsigned char)(ep->bld_time - 1);
        } else {
            ep->bst = 0x18;
            DFene_SEStop(ep, 1);
        }
        break;
    case 24:
        lr = e094_pad_lr(ep->new_pad);
        if (ene_shoot(ep, mp, ep->lkmp, ep->lkvec, 1)) {
            ep->bst = 0;
            ep->eff.nodamage = 0;
        }
        break;
    case 25:
        if (Anim_IsEnd(mp)) ep->bst = 0x1a;
        break;
    case 26:
        if (ene_shoot(ep, mp, ep->lkmp, ep->lkvec, 1)) ep->bst = 0x1b;
        break;
    case 27:
        if (Anim_IsEnd(mp)) ep->bst = 0;
        break;
    case 28: case 29:
        if (!Anim_IsEnd(ep->mp)) {
            ep->react_time = (unsigned char)(ep->react_time - 1);
            if (ep->react_time != 0) break;
        }
        ep->react_time = 0;
        ep->bst = 0;
        break;
    default:
        break;
    }
    return lr;
}

/* hover-loop sound: volume ramps with se_cnt (0..30) while a direction is held */
static void e094_se_hover(EnemySt *ep)
{
    PS2_FRAME(0x20);
    EnemyDataSt *edat = ep->edat;
    if (ep->dir != 8) {
        if (ep->se_cnt < 30) ep->se_cnt = (short)(ep->se_cnt + 1);
    } else {
        if (ep->se_cnt > 0) ep->se_cnt = (short)(ep->se_cnt - 1);
    }
    float r = (float)ep->se_cnt / 30.0f;
    if (!(r <= 0.0f)) {
        if (ep->vh[1] >= 0x50) ep->vh[1] = (unsigned char)_DFene_SECall(&edat->se_tab[0], &ep->mp->loc, r, 1);
        else DFene_SEVScale(ep, 1, &edat->se_tab[0], r, r);
    } else {
        DFene_SEStop(ep, 1);
    }
}

/* footstep sound on the animation key frames of the last object (first and last key skipped) */
void e094_walk_put_animSE(StModel *mp, EneSeSt *se_tab)
{
    PS2_FRAME(0x70);
    if (!mp) return;
    ObjectSt *ob = &mp->op[mp->nobj - 1];
    EnemySt *ep = (EnemySt *)mp->arg0p;
    if (ob->todid == 0xff) return;
    float wh = ene_get_water_high(mp, mp->loc);
    float y = mp->loc[1];
    if (!(y <= wh - 10.0f) && !(ep->floor_Y <= wh) && !(wh <= y - 0.5f * mp->tall)) se_tab++;   /* water step */
    unsigned int key = ob->frame;
    StMixAnimationTable *t = &mp->tod->animtable[ob->todid][mp->nobj - 1];
    for (int i = 1; i < (int)t->nkey_tod - 1; i++)
        if (key == t->tod_data[i].key) _DFene_SECall(se_tab, &mp->loc, 1.0f, 0);
}

/* turn_spd toward the lock target (x / z of lkmp->loc - mp->loc; the original's vsub.xyz scratch is not kept) */
static inline void e094_turn_to_lock(EnemySt *ep, StModel *mp)
{
    float dx = ep->lkmp->loc[0] - mp->loc[0];
    float dz = ep->lkmp->loc[2] - mp->loc[2];
    float a = ps2_atan2f(dx, dz) - ep->view.angy;
    while (!(a <= E094_PI)) a = a - E094_2PI;        /* never ends for a NaN angle */
    while (a < -E094_PI) a = a + E094_2PI;
    ep->turn_spd = 0.5f * a;
}

/* movement per state: switch on the NEW bst; old_bst = bst before e094_bst_mng (used by the bst-3 arm only) */
static void e094_ctrl_move(EnemySt *ep, StModel *mp, int old_bst)
{
    PS2_FRAME(0x60);
    EnemyDataSt *edat = ep->edat;
    int dir = ep->dir;
    volatile float k0 = 0.0f, k1 = 1.0f, kneg1 = -1.0f;   /* real multiplies (FTZ / -0 / NaN), not folded */
    switch ((unsigned int)ep->bst) {
    case 0: case 6: {
        unsigned int lt = edat->leg_type;
        if (lt == 5 || lt == 4 || lt == 3) {
            ene_dash(ep, mp, edat->f_accel, edat->f_max, edat->brake, ep->dir, 0);
            DFene_MoveH(ep, mp, ep->vel);
        } else if (lt == 0) {
            DFene_AnimWalkH(ep, 0.1f, 1.0f);
            if (!ep->eff.nofstep) e094_walk_put_animSE(ep->mp, edat->se_tab);
        } else {
            ene_dash(ep, mp, edat->d_accel, edat->d_max, edat->brake, dir, 0);
            DFene_MoveH(ep, mp, ep->vel);
        }
        break;
    }
    case 1: case 16:
        ene_dash(ep, mp, edat->d_accel, edat->d_max, edat->brake, dir, 0);
        DFene_MoveH(ep, mp, ep->vel);
        break;
    case 30:
        ene_dash(ep, mp, edat->f_accel, edat->f_max, edat->brake, dir, 0);
        DFene_MoveH(ep, mp, ep->vel);
        break;
    case 5: {
        float b = edat->brake;
        ep->vel[0] = ep->vel[0] * b;
        ep->vel[2] = ep->vel[2] * b;
        DFene_MoveH(ep, mp, ep->vel);
        break;
    }
    case 2: case 4: case 10: case 11:
    case 20: case 21: case 22: case 23: case 24: case 25: case 26: case 27: case 28: case 29:
        ep->vel[0] = ep->vel[0] * E094_SLOW;
        ep->vel[2] = ep->vel[2] * E094_SLOW;
        DFene_MoveH(ep, mp, ep->vel);
        break;
    case 14: case 15: {
        float k = k1;
        ep->vel[0] = ep->vel[0] * k;
        ep->vel[2] = ep->vel[2] * k;
        DFene_MoveH(ep, mp, ep->vel);
        break;
    }
    case 3:
        if (old_bst == 2) {
            StModel *m = ep->mp;
            m->loc[1] = m->loc[1] - (edat->fit_height + ep->gravity);
            ep->vel[1] = -edat->jump;
            _DFene_SECall(&edat->se_tab[3], &mp->loc, 1.0f, 0);
        } else if (old_bst == 1) {
            if (ep->sw.landing) {
                if (!(edat->jump <= 0.0f)) ep->vel[1] = kneg1 * edat->jump;
                else if (!(edat->r_accel <= 0.0f)) ep->vel[1] = -(edat->fit_height + edat->r_accel);
            }
        } else if (old_bst == 0) {
            if (ep->sw.landing && !(edat->r_accel == 0.0f)) ep->vel[1] = -(edat->fit_height + edat->r_accel);
        } else {
            ene_dash(ep, mp, edat->f_accel, edat->f_max, edat->brake, dir, 0);
            DFene_MoveH(ep, mp, ep->vel);
        }
        break;
    case 7: case 18:                                               /* blade */
        if (ep->bld_time > 0) {
            if ((edat->wep_tab[4]._type & 0x3f) == 8) ene_blade_dash(ep, mp, 0.1f, 1.0f);
            else ene_blade_dash(ep, mp, 1.0f, 1.0f);
        } else {
            float k = k0;
            ep->vel[0] = ep->vel[0] * k;
            ep->vel[2] = ep->vel[2] * k;
        }
        DFene_MoveH(ep, mp, ep->vel);
        ene_get_lock(ep, mp, 4);
        if (ep->lkmp && ((edat->wep_tab[4]._type & 0x3f) != 8 || ep->bld_time > 0)) e094_turn_to_lock(ep, mp);
        break;
    case 8:                                                        /* climb toward the lock target */
        ene_get_lock(ep, mp, 4);
        if (ep->lkmp) {
            ene_dash(ep, mp, edat->f_accel, edat->f_max, edat->brake, 0, 0);
            DFene_MoveH(ep, mp, ep->vel);
            float dy = ep->lkmp->loc[1] - mp->loc[1];
            if (!(dy <= E094_CLIMB_D)) {
                float v = ep->vel[1] + E094_CLIMB_D;
                ep->vel[1] = v;
                if (!(dy <= E094_CLIMB_MAX)) {
                    if (!(v <= E094_CLIMB_MAX)) ep->vel[1] = E094_CLIMB_MAX;
                } else {
                    if (!(v <= dy)) ep->vel[1] = dy;
                }
            } else if (dy < -0.5f) {
                float v = ep->vel[1] - 0.5f;
                ep->vel[1] = v;
                if (v < -E094_CLIMB_MAX) ep->vel[1] = -E094_CLIMB_MAX;
            }
            e094_turn_to_lock(ep, mp);
        }
        break;
    default:                                                       /* 9, 12, 13, 17, 19, >= 31 */
        break;
    }
}

void e094_control(EnemySt *ep, StModel *mp)
{
    PS2_FRAME(0x70);
    EnemyDataSt *edp = ep->edat;
    memcpy(ep->oloc, mp->loc, sizeof(ep->oloc));                   /* lq / sq: all 4 lanes */
    ep->eff.oldbst = ep->eff.nowbst;
    ep->eff.nowbst = 0;
    int bst = ep->bst;
    int dir = ep->dir;
    if (bst == 0x10) {
        ep->view.angx = ep->view.angx * 0.1f;
        ang2vec2(ep->view.vec, ep->view.angy, ep->view.angx);
    } else {
        ene_change_view_ex(ep, E094_VIEW_ACC, E094_VIEW_SPD, E094_VIEW_BRK);
    }
    ene_change_wep(ep);
    float lr = e094_bst_mng(ep, bst, dir);
    EnemyDataSt *e = ep->edat;
    if (!(ep->new_pad & 0x4000) && ep->booster < e->booster * 8) {     /* booster regen */
        ep->booster = (short)(ep->booster + 5);
        if (e->booster * 8 < ep->booster) ep->booster = (short)(e->booster * 8);
    }
    ene_change_anim(ep, mp, bst, dir, lr);
    e094_ctrl_move(ep, mp, bst);
    e091_turn(ep, mp, edp, lr, E094_VIEW_BRK);
    ene_boost_sfx(ep, 0);
    unsigned int lt = ep->edat->leg_type;
    if (lt == 3 || lt == 1 || lt == 2) {
        if (ep->vh[2] >= 0x50) ep->vh[2] = (unsigned char)_DFene_SECall(&ep->edat->se_tab[7], &ep->mp->loc, 1.0f, 1);
        e094_se_hover(ep);
    }
}
