/* src/enemy/eneact.cpp (OLSORTIE) - shared per-frame "action" helpers of the enemy controllers (e001..e126).
 * ene_set_dir turns the pad bits into the 8-way direction ep->dir (8 = none); ene_change_anim / FlyMT_ChangeAnim /
 * flyer_change_anim pick the animation for the new (bst, dir) from small static tables ({id, frm, loop, chganim})
 * and call Anim_ChangeAnim / Anim_ChangeAnimSameFrame through the stored function pointer; ene_turn_speed /
 * ene_change_view_ex are the angle-rate integrators, ene_change_wep cycles cur_wep; DFene_Falling (ep->chkfloor of
 * every ground enemy) / DFene_SimpleFalling do gravity, the floor / water probes and the landing state; ene_dash /
 * ene_blade_dash accelerate along the model's facing (VU0 macro code in the original); ene_shoot_wave fires a
 * shot at the lock target or straight ahead; ene_boost_rate = booster fill.
 * Whole file native (G-F1). garage3 reaches ene_set_dir, ene_change_anim (bst 0 / 6 lookups only), DFene_Falling
 * (land arm only), ene_turn_speed, ene_change_wep, ene_change_view_ex; the rest is asm-reviewed only. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"
#include "acnb/sdk.h"

/* float bit patterns of the original's lui / ori constants */
#define EACT_01        0x1.99999ap-4f      /* 0x3dcccccd 0.1f */
#define EACT_NOWATER   0x1.869fe6p+16f     /* 0x47c34ff3 99999.8984375f */
#define EACT_FLTMAX    0x1.fffffep+127f    /* 0x7f7fffff */

typedef struct struct_65bcf60d EneAnimEnt;

#ifdef ACNB_SOURCEPORT
#define CA Anim_ChangeAnim
#define SF Anim_ChangeAnimSameFrame
float ene_dash__dir2vec[8][4] = {
    {0.0f, 0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, -1.0f, 0.0f},
};
float ene_blade_dash__vec[4] = {0.0f, 0.0f, -1.0f, 0.0f};
EneAnimEnt ene_change_anim___turn_anim[2] = {{1, 20, 1, CA}, {1, 20, 1, CA}};
EneAnimEnt ene_change_anim___turn2_anim[2] = {{1, 80, 1, CA}, {1, 80, 1, CA}};
EneAnimEnt ene_change_anim___open_anim[4] = {{24, 10, 0, CA}, {25, 0, 0, CA}, {26, 10, 0, CA}, {27, 0, 0, CA}};
EneAnimEnt ene_change_anim___breath_anim[4] = {{24, 15, 0, CA}, {25, 0, 0, CA}, {0, 0, 0, 0}, {0, 0, 0, 0}};
EneAnimEnt flyer_change_anim___turn_anim[2] = {{1, 40, 0, CA}, {1, 40, 0, CA}};
EneAnimEnt FlyMT_ChangeAnim__s_TurnAnim[2] = {{20, 40, 0, CA}, {19, 40, 0, CA}};
#define EACT_WALK(f) \
    {{2, f, 1, SF}, {3, f, 1, SF}, {5, f, 1, SF}, {8, f, 1, SF}, {7, f, 1, SF}, {9, f, 1, SF}, {6, f, 1, SF}, \
     {4, f, 1, SF}, {0, f, 1, CA}}
EneAnimEnt ene_change_anim___walk_anim[9] = EACT_WALK(20);
EneAnimEnt ene_change_anim___walk_anim_agile[9] = {
    {32, 20, 1, SF}, {33, 20, 1, SF}, {35, 20, 1, SF}, {38, 20, 1, SF}, {37, 20, 1, SF},
    {39, 20, 1, SF}, {36, 20, 1, SF}, {34, 20, 1, SF}, {0, 20, 1, CA}};
EneAnimEnt ene_change_anim___walk2_anim[9] = EACT_WALK(80);
EneAnimEnt ene_change_anim___walk3_anim[9] = EACT_WALK(20);
#define EACT_ROW9(a, b, c, d, e, f, g, h, i, frm, l8) \
    {{a, frm, 0, CA}, {b, frm, 0, CA}, {c, frm, 0, CA}, {d, frm, 0, CA}, {e, frm, 0, CA}, {f, frm, 0, CA}, \
     {g, frm, 0, CA}, {h, frm, 0, CA}, {i, frm, l8, CA}}
EneAnimEnt ene_change_anim___dash_anim[9] = EACT_ROW9(10, 12, 12, 11, 11, 11, 13, 13, 0, 30, 0);
EneAnimEnt ene_change_anim___brake_anim[9] = EACT_ROW9(11, 11, 13, 10, 10, 10, 12, 11, 0, 30, 0);
EneAnimEnt ene_change_anim___fly_anim[9] = EACT_ROW9(18, 19, 19, 21, 21, 21, 20, 20, 0, 30, 0);
EneAnimEnt ene_change_anim___jump_anim = {14, 6, 0, CA};
EneAnimEnt ene_change_anim___land_anim = {17, 10, 0, CA};
EneAnimEnt ene_change_anim___dfront_anim = {30, 5, 0, CA};
EneAnimEnt ene_change_anim___dback_anim = {31, 5, 0, CA};
EneAnimEnt ene_change_anim___fall_anim = {16, 30, 0, CA};
EneAnimEnt ene_change_anim___rise_anim = {15, 30, 0, CA};
EneAnimEnt ene_change_anim___blade_anim[4] = {{22, 5, 0, CA}, {0, 15, 0, CA}, {24, 5, 0, CA}, {0, 15, 0, CA}};
EneAnimEnt ene_change_anim___flyblade_anim = {23, 5, 0, CA};
EneAnimEnt ene_change_anim___cannon_anim[4] = {{24, 20, 0, CA}, {25, 20, 0, CA}, {26, 45, 0, CA}, {27, 3, 0, CA}};
EneAnimEnt ene_change_anim___aero_anim[3] = {{10, 5, 0, CA}, {11, 0, 0, CA}, {12, 0, 0, CA}};
EneAnimEnt flyer_change_anim___fly_anim[9] = EACT_ROW9(10, 12, 12, 11, 11, 11, 13, 13, 0, 25, 1);
EneAnimEnt flyer_change_anim___cannon_anim[4] = {{24, 20, 0, CA}, {25, 20, 0, CA}, {26, 45, 0, CA}, {27, 3, 0, CA}};
EneAnimEnt FlyMT_ChangeAnim__s_FlyAnim[9] = EACT_ROW9(18, 19, 19, 21, 21, 21, 20, 20, 11, 25, 0);
#undef EACT_WALK
#undef EACT_ROW9
#undef CA
#undef SF
#endif

/* the table tail of every change_anim variant: jalr chganim(mp, id, frm, loop) */
static inline void eact_play(const EneAnimEnt *e, StModel *mp)
{
    if (e && e->chganim) gcall(e->chganim, mp, e->id, e->frm, e->loop);
}

/* `!(v <= max) -> max, v < -max -> -max` (neg.s max) */
static inline float eact_clamp(float v, float max)
{
    if (!(v <= max)) return max;
    float n = -max;
    if (v < n) return n;
    return v;
}

/* sw.shadow set -> clear the model's on-ground draw flag, else set it */
static inline void eact_shadow_use(EnemySt *ep, StModel *mp)
{
    if (ep->sw.shadow) mp->use &= 0xff7f;
    else mp->use |= 0x80;
}

/* gravity + fall clamp of the non-fliers (vel1 reloaded after the store) */
static inline void eact_gravity(EnemySt *ep)
{
    if (!ep->sw.flier) {
        ep->vel[1] = ep->vel[1] + ep->gravity;
        float fm = ep->fallmax;
        if (!(ep->vel[1] <= fm)) ep->vel[1] = fm;
    }
}

float ene_boost_rate(EnemySt *ep)
{
    int m = ep->edat->booster;
    if (m <= 0) return 0.0f;
    return ps2_div((float)ep->booster, (float)(m << 3));
}

void ene_set_dir(EnemySt *ep)
{
    unsigned int p = ep->new_pad;
    if (p & 0x10) ep->dir = (p & 0x400) ? 1 : (p & 0x800) ? 7 : 0;
    else if (p & 0x40) ep->dir = (p & 0x400) ? 3 : (p & 0x800) ? 5 : 4;
    else ep->dir = (p & 0x400) ? 2 : (p & 0x800) ? 6 : 8;
}

void FlyMT_ChangeAnim(EnemySt *pEne, StModel *pMdl, int oldBst, int oldDir, float turnCW)
{
    PS2_FRAME(0x10);
    int bst = pEne->bst, dir = pEne->dir;
    const EneAnimEnt *e = 0;
    if (bst == oldBst && dir == oldDir) return;
    if (bst == 6) {
        if (!(turnCW <= 0.0f)) e = &FlyMT_ChangeAnim__s_TurnAnim[0];
        else if (turnCW < 0.0f) e = &FlyMT_ChangeAnim__s_TurnAnim[1];
    } else if (bst == 0) e = &FlyMT_ChangeAnim__s_FlyAnim[dir];
    eact_play(e, pMdl);
}

void flyer_change_anim(EnemySt *ep, StModel *mp, int old_bst, int old_dir, float turn_cw)
{
    PS2_FRAME(0x10);
    int dir = ep->dir, bst = ep->bst;
    const EneAnimEnt *e = 0;
    if (bst == old_bst && dir == old_dir) return;
    switch (bst) {
    case 13: e = &flyer_change_anim___cannon_anim[3]; break;
    case 11: e = &flyer_change_anim___cannon_anim[2]; break;
    case 12: e = &flyer_change_anim___cannon_anim[1]; break;
    case 10: e = &flyer_change_anim___cannon_anim[0]; break;
    case 6:
        if (!(turn_cw <= 0.0f)) e = &flyer_change_anim___turn_anim[0];
        else if (turn_cw < 0.0f) e = &flyer_change_anim___turn_anim[1];
        break;
    case 0: e = &flyer_change_anim___fly_anim[dir]; break;
    default: break;
    }
    eact_play(e, mp);
}

void ene_change_anim(EnemySt *ep, StModel *mp, int old_bst, int old_dir, float turn_cw)
{
    PS2_FRAME(0x20);
    /* bst 3 (fall / fly) re-runs the lookup and re-issues the change every frame (quirk) */
    if (ep->bst == old_bst && ep->dir == old_dir && ep->bst != 3) return;
    int bst = ep->bst, dir = ep->dir;
    const EneAnimEnt *e = 0;
    EneAnimEnt copy;   /* sp+0x10 in the original; only read by the table tail */
    switch (bst) {
    case 0: {
        unsigned int lt = ep->edat->leg_type;
        if (lt == 5) e = &ene_change_anim___walk3_anim[dir];
        else if (lt == 2) e = &ene_change_anim___walk2_anim[dir];
        else if (lt == 3 || lt == 0)
            e = (ep->sw2.walk_mode == 1) ? &ene_change_anim___walk_anim_agile[dir] : &ene_change_anim___walk_anim[dir];
        else e = &ene_change_anim___dash_anim[dir];
        break;
    }
    case 1: e = &ene_change_anim___dash_anim[ep->dsh_time > 0 ? ep->dsh_dir : dir]; break;
    case 2: e = &ene_change_anim___jump_anim; break;
    case 3:
        if (dir != 8) e = &ene_change_anim___fly_anim[dir];
        else e = (ep->vel[1] <= 0.0f) ? &ene_change_anim___rise_anim : &ene_change_anim___fall_anim;
        break;
    case 4: e = &ene_change_anim___land_anim; break;
    case 5: e = &ene_change_anim___brake_anim[(int)ep->dsh_dir]; break;
    case 6:
        if (ep->edat->leg_type == 2) {
            if (!(turn_cw <= 0.0f)) e = &ene_change_anim___turn2_anim[0];
            else if (turn_cw < 0.0f) e = &ene_change_anim___turn2_anim[1];
        } else {
            if (!(turn_cw <= 0.0f)) e = &ene_change_anim___turn_anim[0];
            else if (turn_cw < 0.0f) e = &ene_change_anim___turn_anim[1];
        }
        break;
    case 7: e = &ene_change_anim___blade_anim[0]; break;
    case 8: e = &ene_change_anim___flyblade_anim; break;
    case 10: e = &ene_change_anim___cannon_anim[0]; break;
    case 11: e = &ene_change_anim___cannon_anim[2]; break;
    case 12: e = &ene_change_anim___cannon_anim[1]; break;
    case 13: e = &ene_change_anim___cannon_anim[3]; break;
    case 14: e = &ene_change_anim___aero_anim[0]; break;
    case 15: e = &ene_change_anim___aero_anim[2]; break;
    case 16: e = &ene_change_anim___aero_anim[1]; break;
    case 17:
        copy = ene_change_anim___blade_anim[1];
        copy.frm = copy.frm + (int)ep->edat->wep_tab[4].reload;
        e = &copy;
        break;
    case 18: e = &ene_change_anim___blade_anim[2]; break;
    case 19:
        copy = ene_change_anim___blade_anim[3];
        copy.frm = copy.frm + (int)ep->edat->wep_tab[4].reload;
        e = &copy;
        break;
    case 20: e = &ene_change_anim___open_anim[(ep->wep_valid & 2) ? 2 : 0]; break;
    case 22: e = &ene_change_anim___open_anim[(ep->wep_valid & 2) ? 3 : 1]; break;
    case 25: e = &ene_change_anim___breath_anim[0]; break;
    case 27: e = &ene_change_anim___breath_anim[1]; break;
    case 28: e = &ene_change_anim___dfront_anim; break;
    case 29: e = &ene_change_anim___dback_anim; break;
    default: break;   /* 9, 21, 23, 24, 26, 30, >= 31 / negative */
    }
    eact_play(e, mp);
}

void DFene_SimpleFalling(EnemySt *ep, StModel *mp)
{
    PS2_FRAME(0x60);
    EnemyDataSt *edat = ep->edat;
    alignas(16) float pos[4], dv[4];
    pos[0] = mp->loc[0]; pos[1] = mp->loc[1]; pos[2] = mp->loc[2]; pos[3] = mp->loc[3];
    float h = 0.5f * mp->tall;
    dv[0] = 0.0f; dv[1] = 1.0f; dv[3] = 1.0f; dv[2] = 0.0f;
    pos[1] = pos[1] - h;
    if (HitBoxHitProcLine(pos, dv, mp->tall, 0, 0)) {
        mp->nowparent = *(StModel **)PS2_SPR(0xc);
        ep->floor_Y = *(float *)PS2_SPR(0x14);
    } else {
        float s = fsmath_SinVU0(mp->ang[1]);
        pos[0] = pos[0] + mp->R * s;
        float c = fsmath_CosVU0(mp->ang[1]);
        pos[2] = pos[2] + mp->R * c;
        if (HitBoxHitProcLine(pos, dv, mp->tall, 0, 0)) {
            mp->nowparent = *(StModel **)PS2_SPR(0xc);
            ep->floor_Y = *(float *)PS2_SPR(0x14);
        }
    }
    float fl = ep->floor_Y;
    float dist = fl - mp->loc[1];
    float vel1 = ep->vel[1];
    float thr = (vel1 < 0.0f) ? 0.0f : edat->fit_height;
    if (dist <= thr || dist <= vel1) {
        mp->loc[1] = fl - EACT_01;
        ep->sw.landing = (!(ep->vel[1] <= 0.5f)) ? 2 : 1;
        ep->vel[1] = 0.0f;
        eact_shadow_use(ep, mp);
    } else {
        ep->sw.landing = 0;
        mp->use |= 0x80;
        mp->loc[1] = mp->loc[1] + ep->vel[1];
    }
    eact_gravity(ep);
    mp->yGround = ep->floor_Y;
}

void DFene_Falling(EnemySt *ep, StModel *mp)
{
    PS2_FRAME(0xd0);
    int onchr = 0;
    EnemyDataSt *edat = ep->edat;
    float R1 = mp->R - EACT_01;   /* f25 */
    float tall = mp->tall;        /* f26 */
    alignas(16) float pos[4];     /* sp+0xb0 */
    float yhit;                   /* sp+0xcc: floor probe, water probe and the dist temp */
    pos[0] = mp->loc[0]; pos[1] = mp->loc[1]; pos[2] = mp->loc[2]; pos[3] = mp->loc[3];
    float c6 = 6.0f + tall;       /* f22 */
    pos[1] = pos[1] + 3.0f;
    HitBoxHitProcYuka(mp, pos, c6, R1, &yhit);
    float fy = yhit;              /* f24 */
    float py = pos[1];
    if (!(fy <= py - (3.0f + 0.5f * tall))) c6 = py - c6;
    else { c6 = fy; fy = py; }    /* swap arm: f22 / f24 exchange roles (quirk) */
    if (ep->sw.water) {
        if (HitBoxHitProcWater(mp, pos, tall, R1, &yhit)) {
            ep->water_ht = yhit;
            mp->WatY = yhit;
        } else {
            ep->water_ht = EACT_NOWATER;
            mp->WatY = EACT_NOWATER;
        }
    }
    float dist = fy - mp->loc[1];
    float vel1 = ep->vel[1];
    float thr = (vel1 < 0.0f) ? 0.0f : edat->fit_height;
    if (dist <= thr || dist <= vel1) {
        if (!onchr) mp->loc[1] = fy - EACT_01;
        ep->sw.landing = (!(ep->vel[1] <= 0.5f)) ? 2 : 1;
        ep->vel[1] = 0.0f;
        ep->floor_Y = fy;
        eact_shadow_use(ep, mp);
    } else {
        alignas(16) float lpos[4], dv[4], iv[4] = {};   /* sp+0xa0, 0x90, 0x80 */
        lpos[0] = mp->loc[0]; lpos[1] = mp->loc[1]; lpos[2] = mp->loc[2]; lpos[3] = mp->loc[3];
        lpos[1] = lpos[1] - 0.5f * tall;
        dv[1] = 1.0f; dv[3] = 1.0f; dv[0] = 0.0f; dv[2] = 0.0f;
        if (HitBoxHitProcLine(lpos, dv, 150.0f, 0, 0)) {
            float hy = *(float *)PS2_SPR(0x14);
            if (!(hy <= 3.0f + mp->loc[1])) {
                ep->floor_Y = hy;
                mp->nowparent = *(StModel **)PS2_SPR(0xc);
            }
        } else {
            ep->floor_Y = lpos[1] + 150.0f;
        }
        ep->sw.landing = 0;
        mp->use |= 0x80;
        float v1 = ep->vel[1];
        float k = (v1 <= 0.0f) ? edat->fit_height : 0.0f;
        float ly = mp->loc[1];
        if (ly - tall < c6 + k) {
            mp->loc[1] = EACT_01 + (c6 + tall);
            ep->vel[1] = 0.0f;
        } else {
            mp->loc[1] = ly + v1;
        }
        if (HitChrHitProcChr(mp, mp->loc, tall, R1, iv)) {
            float y2 = iv[1] * iv[1];
            float xz = iv[0] * iv[0] + iv[2] * iv[2];   /* mula x*x, madd z*z */
            if (!(xz < y2)) {
                mp->loc[1] = mp->loc[1] + iv[1];
                onchr = 1;
                ep->vel[1] = 0.0f;
            }
        }
    }
    if (onchr == 1) {
        ep->sw.landing = (!(ep->vel[1] <= 0.5f)) ? 2 : 1;
        ep->vel[1] = 0.0f;
        ep->floor_Y = fy;
        eact_shadow_use(ep, mp);
    }
    eact_gravity(ep);
    if (ep->eff.fixy) {
        ep->vel[1] = 0.0f;
        mp->loc[1] = ep->initpos[1];
    }
    mp->yGround = (onchr == 1) ? EACT_FLTMAX : ep->floor_Y;
}

/* d = normalize(matrix2 * v) * acc (w = 0); vel.xyz += d (vel.w kept); then the zx speed limit */
static inline void eact_dash_vec(EnemySt *ep, StModel *mp, const float *v, float acc, float max)
{
    alignas(16) float d[4];
    vu_apply(d, (const float (*)[4])mp->matrix2, v);
    vu_normalize(d, d);
    d[0] = d[0] * acc; d[1] = d[1] * acc; d[2] = d[2] * acc;
    ep->vel[0] = ep->vel[0] + d[0];
    ep->vel[1] = ep->vel[1] + d[1];
    ep->vel[2] = ep->vel[2] + d[2];
    vector_limit_zx(ep->vel, max);
}

void ene_blade_dash(EnemySt *ep, StModel *mp, float accel, float max)
{
    PS2_FRAME(0x50);
    DFene_UpdateMatrix(mp);
    eact_dash_vec(ep, mp, ene_blade_dash__vec, accel, max);
}

int ene_shoot_wave(EnemySt *ep, StModel *mp, EneWepSt *wep)
{
    PS2_FRAME(0x80);
    PS2_STACK(float[4], loc, 0x70);   /* loc[3] stays whatever the stack held on the lock arm; Bullet3 interpreted */
    PS2_STACK(float[4], tc, 0x60);
    PS2_STACK(float[4], mc, 0x50);
    int id = wep->spl_id;
    if (id <= 0) return 0;
    if (ep->lkmp) {
        float spd = SpellData2[id].spell.speed;
        DF_GetModelCenter(tc, ep->lkmp);
        DF_GetModelCenter(mc, mp);
        DF_get_deflection(loc, 0, tc, ep->lkvec, mc, spd, 0);
    } else {
        DF_GetModelCenter(loc, mp);
        const float k = 10000.0f;   /* 0x461c4000 */
        loc[0] = loc[0] + k * ep->view.vec[0];
        loc[1] = loc[1] + k * ep->view.vec[1];
        loc[2] = loc[2] + k * ep->view.vec[2];
    }
    DFene_Bullet3(ep, wep, loc, 0);
    return 1;
}

void ene_dash(EnemySt *ep, StModel *mp, float acc, float max, float brk, int dir, int flag)
{
    PS2_FRAME(0x20);
    if (dir == 8) {
        ep->vel[0] = ep->vel[0] * brk;
        ep->vel[2] = ep->vel[2] * brk;
        return;
    }
    if (flag) {
        ep->vel[0] = ep->vel[0] * brk;
        ep->vel[2] = ep->vel[2] * brk;
    }
    eact_dash_vec(ep, mp, ene_dash__dir2vec[dir], acc, max);
}

void ene_turn_speed(float *spdp, float acc, float max, float brake, float cw)
{
    float v = *spdp;
    if (!(cw == 0.0f)) v = eact_clamp(v + acc * cw, max);   /* c.eq.s 0, cw: NaN integrates */
    else v = v * brake;
    *spdp = v;
}

void ene_change_wep(EnemySt *ep)
{
    /* cycles 0..3 only, never the blade slot 4 (quirk) */
    if (((unsigned int)ep->new_pad & ~(unsigned int)ep->old_pad & 0x1000) && ep->am_time == 0)
        ep->cur_wep = (ep->cur_wep < 3) ? (char)(ep->cur_wep + 1) : 0;
}

void ene_change_view_ex(EnemySt *ep, float acc, float spd, float brake)
{
    PS2_FRAME(0x30);
    EnemyDataSt *edat = ep->edat;
    float a = ep->view.angx + ep->look_spd;
    float mx = edat->max_angx, mn = edat->min_angx;
    ep->view.angx = a;
    if (!(a <= mx)) ep->view.angx = mx;
    else if (a < mn) ep->view.angx = mn;
    ang2vec2(ep->view.vec, ep->view.angy, ep->view.angx);
    unsigned int p = ep->new_pad;
    float k = (p & 0x100) ? 1.0f : (p & 0x200) ? -1.0f : 0.0f;
    float v = ep->look_spd;
    if (!(k == 0.0f)) v = eact_clamp(v + acc * k, spd);
    else v = v * brake;
    ep->look_spd = v;
}
