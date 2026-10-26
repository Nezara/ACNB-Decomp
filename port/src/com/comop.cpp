/* src/com/comop.cpp — COM "operations" (movement behaviours, whole file). ComTree_AnalyzeComTree calls
 * COMOP_InitManage per tree op: it copies the program's 7 callbacks from opfunctable[progno] (0 fight the enemy,
 * 1 return to the region, 2/3 roam, 4 avoid missiles, 5 avoid obstacles, 6 follow markers, 7 follow the player,
 * 8 escape the enemy's view, 9 move along a route against the enemy, 10 seek a route) and runs init. The tree scores
 * ops with val_non / val_sel + COMOP_CalcValue; DF_ComInKey runs the selected op's main, which sets comp->move_loc /
 * move_ang or presses keys itself (f_op_pad). Programs 2-10 run on mission / event stages only (asm-verified).
 * "airborne" = com_acp ? actstat & 1 : !enemy landing. Markers: Array->marker[128] (loc, type, link bytes cdmy[],
 * wk20 / wk30 = box min / max offsets). */
#include <bit>
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"

#define HIT_INFO ((HitInfoWkSt *)PS2_SPR(0))   /* collision result in scratchpad (collision/hitwk.h) */

static const float COMOP_PI = 3.14159274f;     /* 0x40490fdb */
static const float COMOP_PI2 = 6.28318548f;    /* 0x40c90fdb */
static const float COMOP_HPI = 1.57079637f;    /* 0x3fc90fdb */

static inline float comop_wrap(float a)
{
    while (!(a <= COMOP_PI)) a = a - COMOP_PI2;
    while (a < -COMOP_PI) a = a + COMOP_PI2;
    return a;
}

static inline float comop_abs(float x) { return x < 0.0f ? -x : x; }
static inline int comop_airborne(void) { return com_acp ? (com_acp->actstat & 1) : !com_enp->sw.landing; }
static inline MarkerSt *comop_marker(int i) { return &Array->marker[i]; }
static inline StModel *comop_player_mp(void) { return g_ac[0].mp; }

/* vsub.xyz into a full store: d.w = a.w */
static inline void comop_vsub(float *d, const float *a, const float *b)
{
    float w = a[3];
    d[0] = a[0] - b[0];
    d[1] = a[1] - b[1];
    d[2] = a[2] - b[2];
    d[3] = w;
}

/* z*z + (x*x + y*y) */
static inline float comop_dist3sq(const float *v)
{
    float x2 = v[0] * v[0];
    float y2 = v[1] * v[1];
    float z2 = v[2] * v[2];
    return z2 + (x2 + y2);
}

/* sqrt(x*x + z*z) (also the mula / madd form: both products rounded, then summed) */
static inline float comop_dist2(const float *v)
{
    float x2 = v[0] * v[0];
    float z2 = v[2] * v[2];
    return ps2_sqrt(x2 + z2);
}

/* the COM has not moved this frame (> 150 frames in a row = stuck) */
static int comop_stuck(void)
{
    stCom *c = comp;
    float d[4];
    comop_vsub(d, c->loc, c->loc_old);
    if (comop_dist3sq(d) < std::bit_cast<float>(0x3c23d70bu)) {   /* 0.01 */
        unsigned char n = c->cant_move;
        c->cant_move = n + 1;
        return !(n < 0x97);
    }
    c->cant_move = 0;
    return 0;
}

/* --- value terms (stOpData.value_calc[], COMOP_CalcValue) */

/* fuzzy term of x against baserate (center) / ratediff (width), scaled by result (all percent) */
static float comop_calc_term(stComCalcValue *v, float x, float m)
{
    float d = (float)v->ratediff / 100.0f;
    float f = v->updown == 0 ? fz_large(x, m, d) : fz_small(x, m, d);
    return ((float)v->result / 100.0f) * f;
}

static float calcval_React(stComCalcValue *valdatap)
{
    PS2_FRAME(0x30);
    stCom *c = comp;
    _spell2 *b = c->nearest_bltp;
    if (!b) return 0.0f;
    float v[4];
    comop_vsub(v, b->pos, c->loc);
    float x2 = v[0] * v[0];
    float z2 = v[2] * v[2];
    float x = ps2_sqrt(x2 + z2);
    float m = (float)comp->datap->blt_reactdist * ((float)valdatap->baserate / 100.0f);
    return comop_calc_term(valdatap, x, m);
}

static float calcval_Bullet(stComCalcValue *valdatap)
{
    PS2_FRAME(0x30);
    float x;
    if (com_acp) {
        _weapon_st *w = &com_acp->param.weapons[(int)comp->wep_sel];
        x = ps2_div((float)w->blt_cnt, (float)w->blt_initcnt);
    } else {
        x = 1.0f;
    }
    return comop_calc_term(valdatap, x, (float)valdatap->baserate / 100.0f);
}

static float calcval_Energy(stComCalcValue *valdatap)
{
    PS2_FRAME(0x30);
    return comop_calc_term(valdatap, comp->energy_rate, (float)valdatap->baserate / 100.0f);
}

static float calcval_Heat(stComCalcValue *valdatap)
{
    PS2_FRAME(0x30);
    float t = com_acp ? com_acp->temperature : 0.0f;
    float x = ps2_div(t, (float)comp->datap->alert_heat);
    return comop_calc_term(valdatap, x, (float)valdatap->baserate / 100.0f);
}

static float calcval_EneAP(stComCalcValue *valdatap)
{
    PS2_FRAME(0x30);
    StModel *em = comp->ene_mp;
    if (!em) return 0.0f;
    int num = 1, den = 1;
    if (em->type == 3) {
        EnemySt *e = (EnemySt *)em->arg0p;
        num = e->nowhp;
        den = e->edat->hp;
    } else if (em->type == 4) {
        AcPlayerSt *a = (AcPlayerSt *)em->arg0p;
        den = a->param.hp;
        num = a->hp;
    }
    float x = ps2_div((float)num, (float)den);
    return comop_calc_term(valdatap, x, (float)valdatap->baserate / 100.0f);
}

static float calcval_AP(stComCalcValue *valdatap)
{
    PS2_FRAME(0x30);
    int den = com_acp ? com_acp->param.hp : com_enp->edat->hp;
    int num = com_acp ? com_acp->hp : com_enp->nowhp;
    float x = ps2_div((float)num, (float)den);
    return comop_calc_term(valdatap, x, (float)valdatap->baserate / 100.0f);
}

/* sum of the set value terms ([0] AP, [1] enemy AP, [2] heat, [3] energy, [4] ammo, [5] incoming bullet) */
float COMOP_CalcValue(stComCalcValue *valdatap)
{
    PS2_FRAME(0x50);
    float sum = 0.0f;
    for (int i = 0; i < 8; i++) {
        auto f = COMOP_CalcValue__op_calcval_funcs[i];
        if (valdatap[i].result && f) sum = sum + gcall(f, &valdatap[i]);
    }
    return sum;
}

/* --- 10: seek a route (SeekRoute_*, enemy/eneop.c) */
static void op_seek_route(stComOpMng *opmngp, struct_884098f6 *wkp)
{
    PS2_FRAME(0x80);
    stComOpParamMoveNearRoute *p = &opmngp->datap->param.move_near_root;
    if (p->aimType == 0) {
        if (!comp->ene_mp) return;
    } else if (p->aimType == 1) {
        if (!comop_player_mp()) return;
    }
    struct_ff0e57fb *sr = &wkp->seek_route;
    if (sr->seek_step == 2 || sr->seek_step == 4) return;
    char near = SeekRoute_OP(sr, comp->ene_mp, comp->mp, p->vlDist, p->vlNearOrFar);
    PS2_STACK(float[4], t, 0x70);
    memcpy(t, sr->dest_pos, 12);
    t[3] = 1.0f;
    memcpy(comp->move_loc, t, 16);
    stCom *c = comp;
    if (c->ene_mp) {
        PS2_STACK(float[4], dest, 0x60);
        if (HetProcPoint2Point(c->mp->loc, c->ene_mp->loc, dest, 3) != 0) {
            comop_vsub(comp->move_ang, t, comp->mp->loc);
        } else {
            comop_vsub(comp->move_ang, comp->ene_mp->loc, comp->loc);
        }
    }
    MarkerSt *m = comop_marker(sr->route_table[(int)sr->next_table_id]);
    PS2_STACK(float[4], ofs, 0x50);
    PS2_STACK(float[4], top, 0x40);
    memcpy(ofs, m->wk + 0x10, 16);
    top[0] = m->loc[0] + ofs[0];
    top[1] = m->loc[1] + ofs[1];
    top[2] = m->loc[2] + ofs[2];
    top[3] = m->loc[3];
    if (top[1] < comp->mp->loc[1] || near != 0) {
        comp->f_op_pad = 1;
        compad_turn_to_vec(comp->move_ang);
        compad_loc_move(comp->move_loc);
        compad_fly();
    }
}

static float onselvl_seek_route(stComOpMng *opmngp, struct_884098f6 *wkp)
{
    PS2_FRAME(0x40);
    stComOpParamMoveNearRoute *p = &opmngp->datap->param.move_near_root;
    StModel *tgt = 0;
    if (p->aimType == 0) tgt = comp->ene_mp;
    else if (p->aimType == 1) tgt = comop_player_mp();
    if (!tgt) return 0.0f;
    struct_ff0e57fb *sr = &wkp->seek_route;
    if (sr->seek_step == 0) {
        SeekRoute_Init(sr);
        sr->seek_step = 1;
        SeekRoute_OP(sr, comp->ene_mp, comp->mp, p->vlDist, p->vlNearOrFar);
        return -1.0f;
    }
    if (sr->seek_step == 2) return 0.0f;
    if (sr->seek_step == 4) {
        memset(sr->route_table, 0, 15);
        sr->seek_step = 0;
        return 0.0f;
    }
    return comop_stuck() ? 0.0f : -1.0f;
}

static float noselvl_seek_route(stComOpMng *opmngp, struct_884098f6 *wkp)
{
    PS2_FRAME(0x10);
    stComOpParamMoveNearRoute *p = &opmngp->datap->param.move_near_root;
    StModel *tgt = 0;
    if (p->aimType == 0) tgt = comp->ene_mp;
    else if (p->aimType == 1) tgt = comop_player_mp();
    if (!tgt) return 0.0f;
    float r = SeekRoute_VL(&wkp->seek_route, comp->ene_mp, comp->mp, p->vlDist, p->vlNearOrFar);
    return r == 1.0f ? -1.0f : 0.0f;
}

/* --- 9: move along a marker route (type-2 markers, linked by cdmy[]) */
static void op_vsene_move_route(stComOpMng *opmngp, struct_c1608a23 *wkp)
{
    PS2_FRAME(0x40);
    stComOpParamMoveNearRoute *p = &opmngp->datap->param.move_near_root;
    if (p->flagEventUse && !wkp->use_flag) return;
    if (!wkp->id_dest_route) return;
    StModel *tgt = 0;
    if (p->aimType == 0 || p->aimType == 1) tgt = comp->ene_mp;
    memcpy(comp->move_loc, wkp->pos_dest, 16);
    if (tgt) comop_vsub(comp->move_ang, tgt->loc, comp->loc);
    if (wkp->pos_dest[1] < comp->mp->loc[1]) {
        comp->f_op_pad = 1;
        compad_turn_to_vec(comp->move_ang);
        compad_loc_move(comp->move_loc);
        compad_select_move(opmngp);
        compad_fly();
    }
    if (comop_stuck()) {
        wkp->id_dest_route = 0;
        wkp->is_in_route = 0;
        comp->cant_move = 0;
    }
}

static float onselvl_vsene_move_route(stComOpMng *opmngp, struct_c1608a23 *wkp)
{
    PS2_FRAME(0x80);
    stComOpParamMoveNearRoute *p = &opmngp->datap->param.move_near_root;
    float ret = p->vlRet;
    unsigned char nearest = 0, farthest = 0;
    float dmin = std::bit_cast<float>(0x7f7fffffu), dmax = 0.0f;
    if (p->flagEventUse && !wkp->use_flag) return 0.0f;
    StModel *tgt = 0;
    if (p->aimType == 0) tgt = comp->ene_mp;
    else if (p->aimType == 1) tgt = comop_player_mp();
    if (!tgt) return 0.0f;
    PS2_STACK(float[4], v, 0x70);
    comop_vsub(v, tgt->loc, comp->loc);
    float dT = comop_dist3sq(v);
    if (!wkp->id_dest_route && !wkp->is_in_route) {
        /* pick the nearest route marker (vlNearOrFar 0: only if nearer than the target, 1: only if the target is
         * nearer than the farthest marker) */
        for (int i = 0; i < 0x80; i++) {
            if (comop_marker(i)->type != 2) continue;
            comop_vsub(v, comop_marker(i)->loc, comp->loc);
            float d = comop_dist3sq(v);
            if (d < dmin) {
                dmin = d;
                nearest = (unsigned char)i;
            }
            if (dmax < d) dmax = d;
        }
        if (p->vlNearOrFar == 0) {
            if (dT < dmin) return 0.0f;
            wkp->id_dest_route = nearest;
        } else if (p->vlNearOrFar == 1) {
            if (dmax < dT) return 0.0f;
            wkp->id_dest_route = nearest;
        }
        memcpy(wkp->pos_dest, comop_marker(wkp->id_dest_route)->loc, 16);
        return ret;
    }
    if (wkp->id_dest_route && !wkp->is_in_route) {
        /* heading for the route marker: entered when the model centre is inside its box */
        unsigned char id = wkp->id_dest_route;
        MarkerSt *m = comop_marker(id);
        comop_vsub(v, m->loc, comp->loc);
        float d = comop_dist3sq(v);
        if (p->vlNearOrFar == 0 && dT < d) return 0.0f;
        DF_GetModelCenter(v, comp->mp);
        PS2_STACK(float[2][4], rgn, 0x50);
        memcpy(rgn[0], m->loc, 16);
        memcpy(rgn[1], m->loc, 16);
        const float *w20 = (const float *)(m->wk + 0x00), *w30 = (const float *)(m->wk + 0x10);
        rgn[1][0] = rgn[1][0] + w20[0];
        rgn[0][0] = rgn[0][0] + w30[0];
        rgn[1][1] = rgn[1][1] + w20[1];
        rgn[0][1] = rgn[0][1] + w30[1];
        rgn[1][2] = rgn[1][2] + w20[2];
        rgn[0][2] = rgn[0][2] + w30[2];
        if (com_check_in_region_xyz(v, rgn, 0.0f) == 0) wkp->is_in_route = 1;
        return ret;
    }
    if (wkp->id_dest_route && wkp->is_in_route) {
        /* on the route: continue to the linked marker nearest to / farthest from the target */
        for (int i = 0; i < 0x10; i++) {
            char l = comop_marker(wkp->id_dest_route)->cdmy[i];
            if (!l) break;
            comop_vsub(v, comop_marker(l)->loc, tgt->loc);
            float d = comop_dist3sq(v);
            if (d < dmin) {
                dmin = d;
                nearest = (unsigned char)l;
            }
            if (dmax < d) {
                dmax = d;
                farthest = (unsigned char)l;
            }
        }
        if (!nearest || !farthest) return 0.0f;
        if (p->vlNearOrFar == 0) wkp->id_dest_route = nearest;
        else if (p->vlNearOrFar == 1) wkp->id_dest_route = farthest;
        wkp->is_in_route = 0;
        memcpy(wkp->pos_dest, comop_marker(wkp->id_dest_route)->loc, 16);
        return ret;
    }
    return ret;
}

/* (sic) aimType / vlNearOrFar outside 0..1 leave the vector / the compared value stale (stale stack / FPU register in
 * the original; the port compares 0 for the latter) */
static float noselvl_vsene_move_route(stComOpMng *opmngp, struct_c1608a23 *wkp)
{
    PS2_FRAME(0x10);
    stComOpParamMoveNearRoute *p = &opmngp->datap->param.move_near_root;
    float ret = p->vlRet;
    if (p->flagEventUse && !wkp->use_flag) return 0.0f;
    PS2_STACK(float[4], v, 0);
    if (p->aimType == 0) {
        if (!comp->ene_mp) return 0.0f;
        comop_vsub(v, comp->ene_mp->loc, comp->loc);
    } else if (p->aimType == 1) {
        if (!comop_player_mp()) return 0.0f;
        comop_vsub(v, comop_player_mp()->loc, comp->loc);
    }
    float x2 = v[0] * v[0];
    float y2 = v[1] * v[1];
    float s = x2 + y2;
    float d2 = s + v[2] * v[2];
    float e = 0.0f;
    if (p->vlNearOrFar == 0) e = d2 - p->vlDist * p->vlDist;
    else if (p->vlNearOrFar == 1) e = p->vlDist * p->vlDist - d2;
    if (0.0f < e) {
        wkp->id_dest_route = 0;
        wkp->is_in_route = 0;
        return ret;
    }
    return 0.0f;
}

/* --- 8: leave the enemy's lock-on cone */

/* the enemy AC has locked on to us (priority lock on our model, lock_stat 8) */
int COMOP_IsTgtLockON(void *pComSelf)
{
    stCom *self = (stCom *)pComSelf;
    StModel *em = self->ene_mp;
    if (!em || em->type != 4) return 0;
    StWepLock *l = ((AcPlayerSt *)em->arg0p)->lockmng.pri_lock;
    if (!l) return 0;
    if (l->ofp != self->mp) return 0;
    return (l->lock_stat & 8) != 0;
}

/* Picks a point 500 out of the enemy's sight cone (the cone edges at its yaw +- lock_angy / 2, range long): the edge
 * point nearer to us, if it lies within 10-100% along the edge, in the half plane between the edges. The rotated
 * matrices and the unit vector at the start are computed and never used. */
static void __GetEscapeViewPos(AcPlayerSt *pMyAC, AcPlayerSt *pTgtAC, stComOpParamEscapeView *param, struct_9ace27f3 *wkp)
{
    PS2_FRAME(0x1e0);
    if (!pTgtAC->lockmng.pri_lock) return;
    WepSightSt *sight = pTgtAC->lockmng.psight[0];
    if (!sight) return;
    if (!COMOP_IsTgtLockON(comp)) return;
    PS2_STACK(float[4], tcen0, 0x190);
    PS2_STACK(float[4], mcen0, 0x180);
    PS2_STACK(float[4], dir, 0x1d0);
    DF_GetModelCenter(tcen0, pTgtAC->mp);
    DF_GetModelCenter(mcen0, pMyAC->mp);
    comop_vsub(dir, mcen0, tcen0);
    {
        float s = ps2_div(1.0f, ps2_sqrt(comop_dist3sq(dir)));
        dir[0] = dir[0] * s;
        dir[1] = dir[1] * s;
        dir[2] = dir[2] * s;
    }
    PS2_STACK(float[4], tc, 0x70);
    PS2_STACK(float[4], mc, 0x60);
    float h = sight->lock_angy / 2.0f;
    DF_GetModelCenter(tc, pTgtAC->mp);
    DF_GetModelCenter(mc, pMyAC->mp);
    PS2_STACK(float[4][4], m1, 0x140);
    PS2_STACK(float[4][4], m2, 0x100);
    sceVu0CopyMatrix(m1, IdentityMtx);
    sceVu0CopyMatrix(m2, IdentityMtx);
    acvu0_RotMatrixY_ni(m1, m1, comop_wrap(pTgtAC->mp->ang[1] + h));
    acvu0_RotMatrixY_ni(m2, m2, comop_wrap(pTgtAC->mp->ang[1] - h));
    float a = comop_wrap(COMOP_HPI - pTgtAC->mp->ang[1]);
    PS2_STACK(float[4], fwd, 0xf0);
    fwd[0] = fsmath_CosVU0(a);
    fwd[2] = fsmath_SinVU0(a);
    fwd[3] = 0.0f;
    fwd[1] = 0.0f;
    fwd[0] = fwd[0] * sight->range;
    fwd[1] = fwd[1] * sight->range;
    fwd[2] = fwd[2] * sight->range;
    PS2_STACK(float[4], u1, 0x1c0);
    PS2_STACK(float[4], e1, 0xe0);
    PS2_STACK(float[4], e1c, 0xc0);
    a = comop_wrap(COMOP_HPI - (pTgtAC->mp->ang[1] + h));
    u1[0] = fsmath_CosVU0(a);
    u1[2] = fsmath_SinVU0(a);
    u1[3] = 0.0f;
    u1[1] = 0.0f;
    e1[0] = u1[0] * sight->range;
    e1[1] = u1[1] * sight->range;
    e1[2] = u1[2] * sight->range;
    e1[3] = u1[3];
    memcpy(e1c, e1, 16);
    PS2_STACK(float[4], u2, 0x1b0);
    PS2_STACK(float[4], e2, 0xd0);
    PS2_STACK(float[4], e2c, 0xb0);
    a = comop_wrap(COMOP_HPI - (pTgtAC->mp->ang[1] - h));
    u2[0] = fsmath_CosVU0(a);
    u2[2] = fsmath_SinVU0(a);
    u2[3] = 0.0f;
    u2[1] = 0.0f;
    e2[0] = u2[0] * sight->range;
    e2[1] = u2[1] * sight->range;
    e2[2] = u2[2] * sight->range;
    e2[3] = u2[3];
    memcpy(e2c, e2, 16);
    float r1 = GetNearRatePointLine(mc, tc, e1);
    float r2 = GetNearRatePointLine(mc, tc, e2);
    if ((r1 < 0.1f || 1.0f < r1) && (r2 < 0.1f || 1.0f < r2)) return;
    for (int i = 0; i < 3; i++) e1[i] = e1[i] * r1;
    for (int i = 0; i < 3; i++) e2[i] = e2[i] * r2;
    for (int i = 0; i < 3; i++) e1[i] = e1[i] + tc[i];
    for (int i = 0; i < 3; i++) e2[i] = e2[i] + tc[i];
    PS2_STACK(float[4], va, 0xa0);
    PS2_STACK(float[4], vb, 0x90);
    comop_vsub(va, e1, mc);
    comop_vsub(vb, e2, mc);
    float p1 = va[0] * vb[0];
    float p2 = va[2] * vb[2];
    if (!(p1 + p2 <= 0.0f)) return;
    float ax2 = va[0] * va[0], ay2 = va[1] * va[1];
    float la = ps2_sqrt((ax2 + ay2) + va[2] * va[2]);
    float bx2 = vb[0] * vb[0], by2 = vb[1] * vb[1];
    float lb = ps2_sqrt((bx2 + by2) + vb[2] * vb[2]);
    PS2_STACK(float[4], n, 0x80);
    if (la < lb) {
        if (r1 < 0.1f || 1.0f < r1) return;
        vu_normalize(n, va);
    } else {
        if (r2 < 0.1f || 1.0f < r2) return;
        vu_normalize(n, vb);
    }
    PS2_STACK(float[4], dest, 0x1a0);
    dest[0] = n[0] * 500.0f;
    dest[1] = n[1] * 500.0f;
    dest[2] = n[2] * 500.0f;
    dest[3] = n[3];
    dest[0] = dest[0] + mc[0];
    dest[1] = dest[1] + mc[1];
    dest[2] = dest[2] + mc[2];
    memcpy(comp->move_loc, dest, 16);
    memcpy(wkp->pos_dest, dest, 16);
    wkp->frame_cnt = 300;
}

static void op_escape_view(stComOpMng *opmngp, struct_9ace27f3 *wkp)
{
    PS2_FRAME(0x10);
    comop_vsub(comp->move_ang, comp->ene_info.loc, comp->loc);
    StModel *em = comp->ene_mp;
    if (!em || em->type != 4 || !com_acp) return;
    AcPlayerSt *e = (AcPlayerSt *)em->arg0p;
    if (wkp->frame_cnt) {
        memcpy(comp->move_loc, wkp->pos_dest, 16);
        wkp->frame_cnt--;
    } else {
        __GetEscapeViewPos(com_acp, e, &opmngp->datap->param.escape_view, wkp);
    }
}

static float onselvl_escape_view(stComOpMng *opmngp, struct_9ace27f3 *wkp)
{
    PS2_FRAME(0x20);
    stComOpParamEscapeView *p = &opmngp->datap->param.escape_view;
    stCom *c = comp;
    if (!c->ene_mp || c->ene_mp->type != 4) return 0.0f;
    float f;
    if (p->valType == 2) {
        float h = 0.5f * p->valDistRange;
        f = fz_large(c->elenxz, p->valDist + h, h);
    } else if (p->valType == 1) {
        float h = 0.5f * p->valDistRange;
        f = fz_small(c->elenxz, p->valDist - h, h);
    } else {
        f = fz_equal(c->elenxz, p->valDist, p->valDistRange);
    }
    if (COMOP_IsTgtLockON(comp) || wkp->frame_cnt) f = f + 0.5f;
    return 0.5f * f;
}

/* --- 6: follow markers */

/* depth-first search for a route hid -> tid over the relative marker links; rg[] records the path */
static int __OP_SerchRoute(MarkerSt *dhd, char hid, char tid, char *rg, int num)
{
    PS2_FRAME(0x90);
    MarkerSt *m = &dhd[(int)hid];
    if (!(num < 0x10)) return -2;
    char id = (char)(m - dhd);
    rg[num] = id;
    for (int i = 0; i < num; i++) {
        if (id == rg[i]) {
            rg[num] = -1;
            return -1;
        }
    }
    if (id == tid) return tid;
    for (int j = 0; j < 10; j++) {
        char l = m->cdmy[j];
        if (!l) {
            rg[num] = -1;
            return -1;
        }
        if (__OP_SerchRoute(dhd, (char)(hid + l), tid, rg, num + 1) >= 0) return rg[num];
    }
    rg[num] = -1;
    return -1;
}

static inline int comop_in_box(const MarkerSt *m, const float *p)
{
    const float *w20 = (const float *)(m->wk + 0x00), *w30 = (const float *)(m->wk + 0x10);
    for (int i = 0; i < 3; i++) {
        if (p[i] < m->loc[i] + w20[i]) return 0;
        if (!(p[i] <= m->loc[i] + w30[i])) return 0;
    }
    return 1;
}

/* (sic) The stuck re-pick scans from the old target marker while storing loop indices, never picks anything when
 * the first scanned marker fails the height test, and the move below uses the pointer past the scan. */
static void op_follow_marker(stComOpMng *opmngp, struct_d1805a41 *wkp)
{
    PS2_FRAME(0x10);
    stComOpParamFollowMaker *p = &opmngp->datap->param.fl_marker;
    MarkerSt *m = wkp->marker_top + wkp->trgId;
    if (wkp->trgId < 0) return;
    if (!wkp->marker_top || !wkp->marker_num) return;
    if (p->cannot_mvtime > 0 && p->cannot_mvtime < comp->cannot_move_time) {
        float best = -1.0f;
        wkp->trgId = -1;
        PS2_STACK(float[4], v, 0);
        for (int i = 0; i < wkp->marker_num; i++, m++) {
            const float *w20 = (const float *)(m->wk + 0x00), *w30 = (const float *)(m->wk + 0x10);
            float y = comp->loc[1];
            if (y < m->loc[1] + w20[1]) continue;
            if (!(y <= m->loc[1] + w30[1])) continue;
            comop_vsub(v, m->loc, comp->loc);
            float d = comop_dist2(v);
            if (i == 0 || d < best) {
                wkp->trgId = (char)i;
                best = d;
            }
        }
    }
    if (comop_in_box(m, comp->loc)) {
        if (!m->cdmy[0] || wkp->trgId == wkp->lstId) {
            if (comp->order_table[1].cwork[2] != 1) {
                wkp->trgId = -1;
                wkp->lstId = -1;
                comp->order_table[1].cwork[0] = 0;
                return;
            }
        }
        wkp->trgId = (char)(wkp->trgId + m->cdmy[0]);
        m = wkp->marker_top + wkp->trgId;
    }
    comop_vsub(comp->move_ang, m->loc, comp->loc);
    memcpy(comp->move_loc, m->loc, 16);
}

/* on selection: nearest marker at our height, then a route to the goal marker (order_table[1].cwork[1]) */
static int si_follow_marker(stComOpMng *opmngp, stOpData *datap, struct_d1805a41 *wkp)
{
    PS2_FRAME(0x30);
    stComOpParamFollowMaker *p = &opmngp->datap->param.fl_marker;
    float best = -1.0f;
    MarkerSt *m = wkp->marker_top;
    if (!m) return 0;
    wkp->delay = p->op_delay;
    wkp->trgId = -1;
    PS2_STACK(float[4], v, 0x20);
    for (int i = 0; i < wkp->marker_num; i++, m++) {
        const float *w20 = (const float *)(m->wk + 0x00), *w30 = (const float *)(m->wk + 0x10);
        float y = comp->loc[1];
        if (y < m->loc[1] + w20[1]) continue;
        if (!(y <= m->loc[1] + w30[1])) continue;
        comop_vsub(v, m->loc, comp->loc);
        int first = best < 0.0f;
        float d = comop_dist2(v);
        if (first || d < best) {
            wkp->trgId = (char)i;
            best = d;
        }
    }
    if (comp->order_table[1].cwork[0] == 2) return 1;
    if (!wkp->marker_top) return 0;
    if (best < 0.0f) return 0;
    memset(wkp->route_guide, -1, 16);
    wkp->lstId = comp->order_table[1].cwork[1];
    if (__OP_SerchRoute(wkp->marker_top, wkp->trgId, wkp->lstId, wkp->route_guide, 0) >= 0) {
        comp->order_table[1].cwork[0] = 2;
        return 1;
    }
    wkp->trgId = -1;
    comp->order_table[1].cwork[0] = 0;
    return 0;
}

/* 0 while the player is nearer the goal and the enemy is close, or the player fell behind, or the enemy is close and
 * visible; else the tail value */
static int comop_follow_marker_hold(stComOpParamFollowMaker *p, struct_d1805a41 *wkp)
{
    PS2_STACK(float[4], a, 0x20);
    PS2_STACK(float[4], b, 0x10);
    PS2_STACK(float[4], c, 0);
    MarkerSt *goal = &wkp->marker_top[(int)wkp->lstId];
    comop_vsub(a, goal->loc, comop_player_mp()->loc);
    comop_vsub(b, goal->loc, comp->loc);
    comop_vsub(c, comop_player_mp()->loc, comp->loc);
    float dA = comop_dist2(a);
    float dB = comop_dist2(b);
    stCom *cc = comp;
    if (dA <= dB) {
        if (cc->ene_mp && !(p->ene_dist <= 0.0f) && cc->elenxz < p->ene_dist) return 1;
        return 0;
    }
    if (!(p->player_dist <= 0.0f) && !(comop_dist2(c) <= p->player_dist)) return 1;
    cc = comp;
    if (cc->ene_mp && !(p->ene_dist <= 0.0f) && cc->elenxz < p->ene_dist && cc->f_can_see_enemy == 1) return 1;
    return 0;
}

static float onselvl_follow_marker(stComOpMng *opmngp, struct_d1805a41 *wkp)
{
    PS2_FRAME(0x30);
    stComOpParamFollowMaker *p = &opmngp->datap->param.fl_marker;
    if (!wkp->marker_top || !wkp->marker_num) return 0.0f;
    if (comp->order_table[1].cwork[0] != 2) return 0.0f;
    if (comop_follow_marker_hold(p, wkp)) return 0.0f;
    return wkp->trgId < 0 ? 0.0f : -1.0f;
}

static float noselvl_follow_marker(stComOpMng *opmngp, struct_d1805a41 *wkp)
{
    PS2_FRAME(0x30);
    stComOpParamFollowMaker *p = &opmngp->datap->param.fl_marker;
    if (!wkp->marker_top || !wkp->marker_num) return 0.0f;
    if (wkp->delay > 0) {
        wkp->delay--;
        return 0.0f;
    }
    if (!wkp->marker_top) return 0.0f;
    if (!comp->order_table[1].cwork[0]) return 0.0f;
    if (comop_follow_marker_hold(p, wkp)) return 0.0f;
    return -1.0f;
}

/* first contiguous run of type-2 markers. (sic) The original's countdown underflows when the run reaches marker
 * 127 and the scan runs on past the array; the port stops there with the run found. */
static int init_follow_marker(stComOpMng *opmngp, stOpData *datap, struct_d1805a41 *wkp)
{
    wkp->trgId = -1;
    wkp->lstId = -1;
    MarkerSt *m = Array->marker;
    for (int n = 0x80; n > 0; n--, m++) {
        if (m->type != 2) continue;
        wkp->marker_top = m;
        wkp->marker_num = 1;
        for (n--, m++; n > 0; n--, m++) {
            if (m->type != 2) return 1;
            wkp->marker_num++;
        }
        return 1;
    }
    wkp->marker_top = 0;
    wkp->marker_num = 0;
    return 1;
}

/* --- 7: follow the player: keep player_min_dist away (random points around the last obstacle's sphere), look at
 * the enemy when near, climb obstacles (type 1: jump then fly over; 2: go around). (sic) The distance tests after
 * the normalise compare a unit vector; type 2 has the near / far choice inverted. */
static void op_follow_player(stComOpMng *opmngp, struct_217def59 *wkp)
{
    PS2_FRAME(0x80);
    stComOpParamFollowPlayer *p = &opmngp->datap->param.fl_player;
    comp->f_op_pad = 1;
    PS2_STACK(float[4], v, 0x70);
    comop_vsub(v, comop_player_mp()->loc, comp->loc);
    if (comop_dist2(v) < p->player_min_dist && FRAME % 30 == 0) {
        int r = ps2_rand();
        float a = (COMOP_PI * (float)(ps2_mod32(r, 0x169) - 0xb4)) / 180.0f;
        float rf = randf();
        float h = p->player_min_dist / 2.0f;
        float rad = h + h * rf;
        wkp->trgPoint[0] = wkp->center[0] + rad * fsmath_CosVU0(a);
        wkp->trgPoint[2] = wkp->center[2] + rad * fsmath_SinVU0(a);
    }
    if (FRAME % 30 == 0) {
        PS2_STACK(float[4], pos, 0x60);
        PS2_STACK(float[4], dir, 0x50);
        memcpy(pos, comp->loc, 16);
        float len = ps2_sqrt(comop_dist3sq(v));
        float s = ps2_div(1.0f, len);
        pos[1] = pos[1] - 0.5f * comp->tall;
        dir[0] = v[0] * s;
        dir[1] = v[1] * s;
        dir[2] = v[2] * s;
        dir[3] = v[3];
        if (HitBoxHitProcLine(pos, dir, len, 0, 1)) {
            StModel *hm = HIT_INFO->pHitMdl;
            memcpy(wkp->center, hm->mlist->sphere, 16);
            wkp->center[3] = 1.0f;
            wkp->r = 5.0f + hm->mlist->R;
            wkp->high = hm->Ytop - 1.0f;
            float dh = comp->loc[1] - (hm->Ytop - hm->Ybottom);
            wkp->type = (!(dh <= 1.0f) && dh < 4.0f * comp->tall) ? 1 : 2;
        }
    }
    vu_normalize(v, v);
    if (!(comop_dist2(v) <= p->player_dist) || !comp->ene_mp) {
        compad_turn_ang(comop_wrap(ps2_atan2f(v[0], v[2])));
    } else {
        compad_turn_to_ene();
    }
    if (comop_airborne()) opmngp->step = 2;
    /* the player-relative point: player_min_dist back along v from the player */
    auto loc_move_player = [&]() {
        float k = -1.0f * p->player_min_dist;
        v[0] = v[0] * k;
        v[1] = v[1] * k;
        v[2] = v[2] * k;
        const float *pl = comop_player_mp()->loc;
        v[0] = pl[0] + v[0];
        v[1] = pl[1] + v[1];
        v[2] = pl[2] + v[2];
        v[3] = pl[3];
        compad_loc_move(v);
    };
    if (wkp->type == 0) {
        if (comop_dist2(v) < p->player_min_dist) loc_move_player();
        else compad_loc_move(wkp->trgPoint);
        compad_select_move(opmngp);
        opmngp->step = 0;
    } else if (wkp->type == 1) {
        switch (opmngp->step) {
        case 0: {
            float j = com_acp ? (float)com_acp->param.abl_jump : com_enp->edat->jump;
            if (j == 0.0f) {
                opmngp->step = 2;
                break;
            }
            int act = com_acp ? (int)com_acp->actno : (int)com_enp->bst;
            if (act == 7) opmngp->step = 1;
            else compad_jump();
            break;
        }
        case 2:
            if (!(comp->loc[1] - wkp->high <= 0.0f)) compad_fly();
            if (com_is_dossun_speed() && !(comp->energy_rate <= (float)comp->datap->alert_energy / 100.0f))
                compad_fly();
            if (comop_dist2(v) < p->player_min_dist) loc_move_player();
            else compad_loc_move(wkp->trgPoint);
            break;
        default:
            break;
        }
    } else if (wkp->type == 2) {
        if (comop_dist2(v) < p->player_min_dist) compad_loc_move(wkp->trgPoint);
        else loc_move_player();
        compad_select_move(opmngp);
        opmngp->step = 0;
    }
}

static float onselvl_follow_player(stComOpMng *opmngp)
{
    PS2_FRAME(0x10);
    stComOpParamFollowPlayer *p = &opmngp->datap->param.fl_player;
    stCom *c = comp;
    if (c->ene_mp && c->elenxz < p->ene_dist && c->f_can_see_enemy == 1) return 0.0f;
    PS2_STACK(float[4], v, 0);
    comop_vsub(v, comop_player_mp()->loc, comp->loc);
    return comop_dist2(v) <= p->player_min_dist ? 0.0f : 1.0f;
}

static float noselvl_follow_player(stComOpMng *opmngp)
{
    PS2_FRAME(0x10);
    stComOpParamFollowPlayer *p = &opmngp->datap->param.fl_player;
    stCom *c = comp;
    if (c->ene_mp && c->elenxz < p->ene_dist && c->f_can_see_enemy == 1) return 0.0f;
    PS2_STACK(float[4], v, 0);
    comop_vsub(v, comop_player_mp()->loc, comp->loc);
    float lim = p->player_dist;
    if (0.0f == lim) lim = 200.0f;
    return comop_dist2(v) <= lim ? 0.0f : 1.0f;
}

/* --- 5: avoid obstacles: jump, then fly up past min_flyhigh while moving 30 along firstVec */
static void op_avoid_obstacle(stComOpMng *opmngp, struct_b34cdffe *wkp)
{
    PS2_FRAME(0x30);
    comp->f_op_pad = 1;
    compad_turn_to_ene();
    PS2_STACK(float[4], tgt, 0x20);
    tgt[0] = wkp->firstVec[0] * 30.0f;
    tgt[1] = wkp->firstVec[1] * 30.0f;
    tgt[2] = wkp->firstVec[2] * 30.0f;
    tgt[3] = wkp->firstVec[3];
    const float *l = comp->loc;
    tgt[0] = l[0] + tgt[0];
    tgt[1] = l[1] + tgt[1];
    tgt[2] = l[2] + tgt[2];
    tgt[3] = l[3];
    if (com_acp && (com_acp->actstat & 1)) wkp->step = 2;
    else if (com_enp && !com_enp->sw.landing) wkp->step = 2;
    switch (wkp->step) {
    case 0:
        if (com_acp) {
            if (!com_acp->param.abl_jump) wkp->step = 2;
            else if (com_acp->actno == 7) wkp->step = 1;
            else compad_jump();
        } else {
            /* (sic) the enemy branch waits for bst 2 where op_follow_player tests 7 */
            if (com_enp->edat->jump == 0.0f) wkp->step = 2;
            else if (com_enp->bst == 2) wkp->step = 1;
            else compad_jump();
        }
        break;
    case 2:
        if (!(comp->loc[1] - wkp->min_flyhigh <= 0.0f)) {
            compad_fly();
        } else if (com_is_dossun_speed() && !(comp->energy_rate <= (float)comp->datap->skl_data[1].lvl / 100.0f)) {
            compad_fly();
        }
        compad_loc_move(tgt);
        break;
    default:
        break;
    }
}

static int init_avoid_obstacle(stComOpMng *opmngp, stOpData *datap, struct_b34cdffe *wkp)
{
    wkp->step = 0;
    int t = opmngp->datap->param.av_obstruct.time;
    if (!t) t = 60;
    if (wkp->min_flyhigh <= 0.0f) wkp->min_flyhigh = comp->param.std_fly_height;
    wkp->count_ct = (int)((unsigned int)ps2_ftoi(comp->loc[1] - wkp->min_flyhigh) * (unsigned int)t);
    if (wkp->count_ct < 0) wkp->count_ct = -wkp->count_ct;
    if (wkp->firstVec[0] == 0.0f && wkp->firstVec[2] == 0.0f && comp->ene_mp) {
        comop_vsub(wkp->firstVec, comp->ene_mp->loc, comp->loc);
        vu_normalize(wkp->firstVec, wkp->firstVec);
    }
    return 1;
}

/* (sic) the dot product of firstVec and the move direction is wrapped as if it were an angle */
static float onselvl_avoid_obstacle(stComOpMng *opmngp, struct_b34cdffe *wkp)
{
    PS2_FRAME(0x20);
    if (wkp->count_ct < opmngp->time_sel) return 0.0f;
    if (!(wkp->firstVec[0] == 0.0f && wkp->firstVec[2] == 0.0f)) {
        PS2_STACK(float[4], u, 0x10);
        comop_vsub(u, comp->loc, comp->loc_old);
        vu_normalize(u, u);
        float p1 = wkp->firstVec[0] * u[0];
        float p2 = wkp->firstVec[2] * u[2];
        float d = p1 + p2;
        d = d - COMOP_PI2 * (float)ps2_ftoi(d / COMOP_PI2);
        if (!(d <= COMOP_PI)) d = d - COMOP_PI2;
        else if (d < -COMOP_PI) d = d + COMOP_PI2;
        if (!(d <= COMOP_HPI)) return 0.0f;
    }
    if (!(opmngp->time_sel < 0x79) && !comop_airborne()) return 0.0f;
    float h = ((float)comp->datap->alert_energy / 100.0f) / 2.0f;
    float h2 = ((float)comp->datap->alert_energy / 100.0f) / 2.0f;
    return fz_large(comp->energy_rate, h, h2);
}

/* every param.frame frames: a wall ahead within the dash distance (rate roll) or a type-11 marker box around us
 * (same group / layer) starts the avoidance */
static float noselvl_avoid_obstacle(stComOpMng *opmngp, struct_b34cdffe *wkp)
{
    PS2_FRAME(0x60);
    stComOpParamAvoidObstacle *p = &opmngp->datap->param.av_obstruct;
    float dist = 0.0f;
    if (ps2_modu32(FRAME, p->frame) != 0) return 0.0f;
    PS2_STACK(float[4], d, 0x50);
    comop_vsub(d, comp->loc, comp->loc_old);
    float s = comop_dist2(d);
    unsigned short vision = p->vision;
    for (int i = 0; i < vision; i++) {
        s = s + (com_acp ? com_acp->param.dash_accel : com_enp->edat->d_accel);
        if (!(s <= (com_acp ? com_acp->param.dash_maxspd : com_enp->edat->d_max))) break;
        dist = dist + s;
    }
    d[0] = d[0] * dist;
    d[1] = d[1] * dist;
    d[2] = d[2] * dist;
    d[0] = comp->loc[0] + d[0];
    d[1] = comp->loc[1] + d[1];
    d[2] = comp->loc[2] + d[2];
    d[3] = comp->loc[3];
    PS2_STACK(float[4], iv, 0x40);
    if (HitBoxHitProcKabe(comp->mp, d, comp->tall, comp->R, iv)) {
        StModel *hm = HIT_INFO->pHitMdl;
        if (hm->mflag & 0x1000) {
            if (ps2_mod32(ps2_rand(), 100) < p->rate) {
                wkp->min_flyhigh = 10.0f + comop_abs(hm->Ytop);
                comop_vsub(d, comp->loc, comp->loc_old);
                vu_normalize(wkp->firstVec, d);
                return 1.0f;
            }
        }
    }
    /* comp->mp->nowparent is not checked (PS2 address 0 in the original) */
    StModel *np = comp->mp->nowparent;
    unsigned char g = np ? np->groupid : *(const unsigned char *)((const char *)PS2_NULL_MEM() + 0x88);
    for (int i = 0; i < 0x80; i++) {
        MarkerSt *m = comop_marker(i);
        if (m->type != 0xb || m->cdmy[1] != 1) continue;
        np = comp->mp->nowparent;
        unsigned char g2 = np ? np->groupid : *(const unsigned char *)((const char *)PS2_NULL_MEM() + 0x88);
        if (m->cdmy[0] != g2 && !(m->cdmy[0] < Array->group[g].layer[63])) continue;
        const float *w20 = (const float *)(m->wk + 0x00), *w30 = (const float *)(m->wk + 0x10);
        const float *l = comp->loc;
        if (l[0] < m->loc[0] + w20[0] || !(l[0] <= m->loc[0] + w30[0])) continue;
        if (l[1] < m->loc[1] + w20[1]) continue;
        float top = m->loc[1] + w30[1];
        if (!(l[1] <= top)) continue;
        if (l[2] < m->loc[2] + w20[2] || !(l[2] <= m->loc[2] + w30[2])) continue;
        wkp->min_flyhigh = 5.0f + comop_abs(top);
        wkp->firstVec[0] = 0.0f;
        wkp->firstVec[1] = 1.0f;
        wkp->firstVec[2] = 0.0f;
        return 1.0f;
    }
    return 0.0f;
}

/* --- 4: avoid missiles: sidestep 50 across the nearest missile's heading (steps 0 / 1 / 2) */
static void comop_sidestep(float a)
{
    memcpy(comp->move_loc, comp->loc, 16);
    float s = fsmath_SinVU0(a);
    comp->move_loc[0] = comp->move_loc[0] + 50.0f * s;
    float c = fsmath_CosVU0(a);
    comp->move_loc[2] = comp->move_loc[2] + 50.0f * c;
}

static void exec_avoid_missile_step2(void)
{
    PS2_FRAME(0x20);
    comop_sidestep(comop_wrap(comp->nearest_misp->ang[1] - COMOP_HPI));
}

static void exec_avoid_missile_step1(stComOpMng *opmngp)
{
    PS2_FRAME(0x30);
    stCom *c = comp;
    _spell2 *m = c->nearest_misp;
    float dx = m->pos[0] - c->loc[0];
    float dy = m->pos[1] - c->loc[1];
    float dz = m->pos[2] - c->loc[2];
    float y2 = dy * dy, x2 = dx * dx;
    float s = x2 + y2;
    if (s + dz * dz < 40000.0f) opmngp->step++;
    comop_sidestep(comop_wrap(COMOP_HPI + m->ang[1]));
}

static void exec_avoid_missile_step0(stComOpMng *opmngp)
{
    PS2_FRAME(0x30);
    comop_sidestep(comop_wrap(comp->nearest_misp->ang[1] - COMOP_HPI));
    opmngp->step++;
}

static void end_avoid_missile(stComOpMng *opmngp, struct_3ce15115 *wkp)
{
    wkp->last_frame = FRAME;
}

/* a random point within r around the COM ((sic) the angle is 2 PI r - 1, not - PI); randf order a, k, a2, k2 */
static void comop_wander(float r)
{
    float a = 2.0f * (COMOP_PI * randf()) - 1.0f;
    float k = randf();
    float s = fsmath_SinVU0(a);
    comp->move_ang[0] = k * (r * s);
    comp->move_ang[1] = 0.0f;
    float c = fsmath_CosVU0(a);
    comp->move_ang[2] = k * (r * c);
    float a2 = 2.0f * (COMOP_PI * randf()) - 1.0f;
    float k2 = randf();
    memcpy(comp->move_loc, comp->loc, 16);
    s = fsmath_SinVU0(a2);
    comp->move_loc[0] = comp->move_loc[0] + k2 * (r * s);
    c = fsmath_CosVU0(a2);
    comp->move_loc[2] = comp->move_loc[2] + k2 * (r * c);
}

static void op_avoid_missile(stComOpMng *opmngp, struct_3ce15115 *wkp)
{
    PS2_FRAME(0x20);
    _spell2 *m = comp->nearest_misp;
    if (!com_acp) return;
    wkp->last_frame = FRAME;
    if (m) gcall((void (*)(stComOpMng *, struct_3ce15115 *))op_avoid_missile__funcs[opmngp->step], opmngp, wkp);
    else comop_wander(50.0f);
}

static float onsel_avoid_missile(void)
{
    return comp->nearest_misp ? -1.0f : 0.0f;
}

static float nosel_avoid_missile(stComOpMng *opmngp, struct_3ce15115 *wkp)
{
    if (FRAME - wkp->last_frame < opmngp->datap->param.av_missile.delay) return 0.0f;
    return comp->nearest_misp ? -1.0f : 0.0f;
}

/* --- 2 / 3: roam while there is no enemy */
static float vl_roming(void)
{
    return comp->ene_mp ? 0.0f : 1.0f;
}

static void op_roming(stComOpMng *opmngp, void *wkp)
{
    PS2_FRAME(0x30);
    stComOpParamRoming *p = &opmngp->datap->param.roming;
    stCom *c = comp;
    float dx = c->move_loc[0] - c->loc[0];
    float dz = c->move_loc[2] - c->loc[2];
    if (ps2_mod32(opmngp->time_sel, p->mv_time + 1) != 0 && c->cannot_move_time < 0x3d) {
        float x2 = dx * dx, z2 = dz * dz;
        if (!(x2 + z2 < 9.0f)) return;
    }
    comop_wander(p->r);
}

/* --- 1: return into the region (rgnalert box, or marker rgn_dmy's box) */
static void op_returnregion(stComOpMng *opmngp)
{
    PS2_FRAME(0x30);
    stComOpParamReturnRegion *p = &opmngp->datap->param.rregion;
    comop_vsub(comp->move_ang, comp->ene_info.loc, comp->loc);
    if (p->rgn_dmy) {
        MarkerSt *m = comop_marker(p->rgn_dmy);
        comp->move_loc[0] = m->loc[0];
        comp->move_loc[1] = m->loc[1];
        comp->move_loc[2] = m->loc[2];
    } else {
        float (*rg)[4] = CopyOfSysInfo.rgnalert;
        comp->move_loc[0] = 0.5f * (rg[0][0] + rg[1][0]);
        comp->move_loc[1] = 0.5f * (rg[0][1] + rg[1][1]);
        comp->move_loc[2] = 0.5f * (rg[0][2] + rg[1][2]);
    }
    if (p->chk_y) {
        MarkerSt *m = comop_marker(p->rgn_dmy);
        comp->f_op_pad = 1;
        compad_turn_to_vec(comp->move_ang);
        compad_loc_move_xyz(comp->move_loc);
        compad_select_move(opmngp);
        if (!(comp->loc[1] <= m->loc[1])) comp->jump_ct = 60;
    }
}

/* -1 outside a marker box; otherwise a negative fuzzy pull by how far the COM is past the rgnalert edge toward the
 * rgnover edge (30 tighter in x / z while the AC is in action 0x12). (sic) The y cases (never returned by
 * com_check_in_region) have min / max swapped, and unhandled bit combinations return -0.0. */
static float vl_returnregion(stComOpMng *opmngp)
{
    PS2_FRAME(0xa0);
    float f = 0.0f;
    unsigned char rd = opmngp->datap->param.rregion.rgn_dmy;
    if (rd) {
        MarkerSt *m = comop_marker(rd);
        PS2_STACK(float[4], lo, 0x50);
        PS2_STACK(float[4], hi, 0x40);
        memcpy(lo, m->wk + 0x00, 16);
        memcpy(hi, m->wk + 0x10, 16);
        for (int i = 0; i < 3; i++) lo[i] = lo[i] + m->loc[i];
        for (int i = 0; i < 3; i++) hi[i] = hi[i] + m->loc[i];
        const float *l = comp->loc;
        for (int i = 0; i < 3; i++) {
            if (!(lo[i] <= l[i])) return -1.0f;
            if (hi[i] < l[i]) return -1.0f;
        }
        return f;
    }
    PS2_STACK(float[2][4], A, 0x80);   /* [0] max corner, [1] min corner */
    PS2_STACK(float[2][4], O, 0x60);
    memcpy(A, CopyOfSysInfo.rgnalert, 32);
    memcpy(O, CopyOfSysInfo.rgnover, 32);
    if (com_acp && com_acp->actno == 0x12) {
        A[1][0] = A[1][0] + 30.0f;
        A[0][0] = A[0][0] - 30.0f;
        A[1][2] = A[1][2] + 30.0f;
        A[0][2] = A[0][2] - 30.0f;
        O[1][0] = O[1][0] + 30.0f;
        O[0][0] = O[0][0] - 30.0f;
        O[1][2] = O[1][2] + 30.0f;
        O[0][2] = O[0][2] - 30.0f;
    }
    int bits = com_check_in_region(comp->loc, A, 0.0f);
    if (!bits) return 0.0f;
    const float *l = comp->loc;
    switch (bits) {
    case 5: bits = (A[1][0] - l[0] <= A[1][2] - l[2]) ? 4 : 1; break;
    case 9: bits = (A[1][0] - l[0] <= l[2] - A[0][2]) ? 8 : 1; break;
    case 6: bits = (l[0] - A[0][0] <= A[1][2] - l[2]) ? 4 : 2; break;
    case 10: bits = (l[0] - A[0][0] <= l[2] - A[0][2]) ? 8 : 2; break;
    default: break;
    }
    float m;
    switch (bits) {
    case 1:
        m = 0.5f * (A[1][0] + O[1][0]);
        f = 2.0f * fz_small(comp->loc[0], m, m - O[1][0]);
        break;
    case 2:
        m = 0.5f * (A[0][0] + O[0][0]);
        f = 2.0f * fz_large(comp->loc[0], m, m - A[0][0]);
        break;
    case 0x10:
        m = 0.5f * (A[1][1] + O[1][1]);
        f = 2.0f * fz_small(comp->loc[1], m, m - O[1][1]);
        break;
    case 0x20:
        m = 0.5f * (A[0][1] + O[0][1]);
        f = 2.0f * fz_large(comp->loc[1], m, m - A[0][1]);
        break;
    case 4:
        m = 0.5f * (A[1][2] + O[1][2]);
        f = 2.0f * fz_small(comp->loc[2], m, m - O[1][2]);
        break;
    case 8:
        m = 0.5f * (A[0][2] + O[0][2]);
        f = 2.0f * fz_large(comp->loc[2], m, m - A[0][2]);
        break;
    default:
        break;
    }
    return -f;
}

/* --- 0: fight the enemy (Versus) */

/* side-to-side weave around the enemy: a random bearing offset, dist[0] out, 20 sideways (side flips every 45-89
 * frames or when stuck). (sic) move_loc is stored relative to the COM (pos - loc) and its w is stale stack. */
static void mv_stdvse_stitch(stComOpMng *opmngp, stComOpParamVsEnemy *param, struct_27448a9f *wkp)
{
    PS2_FRAME(0x50);
    PS2_STACK(float[4], pos, 0x40);
    if (wkp->counter <= 0 || wkp->sideway == 0) {
        wkp->counter = (short)(ps2_mod32(ps2_rand(), 0x2d) + 0x2d);
        wkp->sideway = wkp->sideway == 1 ? -1 : 1;
    }
    float r = randf();
    float a = param->ang[3] * (r - 0.5f);
    a = 2.0f * a;
    a = param->ang[1] + a;
    a = comop_wrap((COMOP_PI * a) / 180.0f);
    a = add_angf(comp->ene_info.ang[1], a);
    pos[0] = param->dist[0] * fsmath_SinVU0(a);
    pos[1] = param->dist[2];
    pos[2] = param->dist[0] * fsmath_CosVU0(a);
    float b = comop_wrap(add_angf(a, COMOP_HPI * (float)wkp->sideway));
    pos[0] = pos[0] + 20.0f * fsmath_SinVU0(b);
    pos[2] = pos[2] + 20.0f * fsmath_CosVU0(b);
    const float *e = comp->ene_info.loc;
    pos[0] = pos[0] + e[0];
    pos[1] = pos[1] + e[1];
    pos[2] = pos[2] + e[2];
    comop_vsub(comp->move_loc, pos, comp->loc);
    if (!(comp->cannot_move_time < 0x3d)) wkp->sideway = wkp->sideway == 1 ? -1 : 1;
}

/* circle the enemy: when off the wanted distance / bearing, step coef (or halfway toward ang[1]) around it on the
 * current side; the side flips on a roll when nearly head-on, and when stuck */
static void mv_stdvse_curve(stComOpMng *opmngp, stComOpParamVsEnemy *param, struct_27448a9f *wkp, float coef)
{
    PS2_FRAME(0x40);
    stCom *c = comp;
    if (comop_abs(param->dist[0] - c->elenxz) <= comop_abs(param->dist[1]) &&
        comop_abs((COMOP_PI * param->ang[1]) / 180.0f - c->etoc_ang_y) <= comop_abs((COMOP_PI * param->ang[3]) / 180.0f)) {
        /* in place: only the stuck flip */
    } else {
        float et = c->etoc_ang_y;
        if (comop_abs(et) < std::bit_cast<float>(0x3db2b8c3u)) {   /* 5 degrees */
            float k = 1.0f - fz_equal(et, 0.0f, std::bit_cast<float>(0x3db2b8c3u));
            int r = ps2_rand();
            if (ps2_ftoi(100.0f * k) < ps2_mod32(r, 100)) wkp->sideway = (char)-wkp->sideway;
        }
        c = comp;
        float t;
        if (comop_abs(c->etoc_ang_y) < coef) {
            float sgn = wkp->sideway == 1 ? 1.0f : -1.0f;
            t = add_angf(c->ene_info.ang[1], coef * sgn);
        } else {
            float w = comop_abs((COMOP_PI * param->ang[1]) / 180.0f);
            float sgn = wkp->sideway == 1 ? 1.0f : -1.0f;
            float r = add_angf(c->ene_info.ang[1], sgn * w);
            float ae = comop_abs(comp->etoc_ang_y);
            float ar = comop_abs(r);
            float s = r <= 0.0f ? -1.0f : 1.0f;
            t = s * ((ar + ae) / 2.0f);
        }
        memcpy(comp->move_loc, comp->ene_info.loc, 16);
        comp->move_loc[0] = comp->move_loc[0] + param->dist[0] * fsmath_SinVU0(t);
        comp->move_loc[2] = comp->move_loc[2] + param->dist[0] * fsmath_CosVU0(t);
        if (param->dist[2] != 0.0f) {
            comp->move_loc[1] = comp->move_loc[1] + param->dist[2];
        } else {
            float r = randf();
            float b = param->ang[2] * (r - 0.5f);
            b = 2.0f * b;
            b = param->ang[0] + b;
            b = (COMOP_PI * b) / 180.0f;
            b = add_angf(comp->ene_info.ang[0], b);
            stCom *s1 = comp;
            float sn = fsmath_SinVU0(b);
            s1->move_loc[1] = s1->move_loc[1] - s1->elenxz * sn;
        }
    }
    if (!(comp->cannot_move_time < 0x3d)) wkp->sideway = wkp->sideway == 1 ? -1 : 1;
}

/* every mv_time + 1 frames (or when stuck): a new move target by mv_type (0 direct, 1 / 2 circle 15 / 45 degree
 * steps, 3 weave) */
static void op_stdvsene(stComOpMng *opmngp, struct_27448a9f *wkp)
{
    PS2_FRAME(0x30);
    stComOpParamVsEnemy *p = &opmngp->datap->param.vsene;
    if (wkp->counter >= 0) wkp->counter--;
    comop_vsub(comp->move_ang, comp->ene_info.loc, comp->loc);
    if (ps2_mod32(opmngp->time_sel, p->mv_time + 1) != 0 && comp->cannot_move_time < 0x3c) return;
    switch (p->mv_type) {
    case 3:
        mv_stdvse_stitch(opmngp, p, wkp);
        break;
    case 2:
        mv_stdvse_curve(opmngp, p, wkp, std::bit_cast<float>(0x3f490fdbu));
        break;
    case 1:
        mv_stdvse_curve(opmngp, p, wkp, std::bit_cast<float>(0x3e860a92u));
        break;
    case 0: {
        memcpy(comp->move_loc, comp->ene_info.loc, 16);
        float r = randf();
        float a = p->ang[3] * (r - 0.5f);
        a = 2.0f * a;
        a = p->ang[1] + a;
        a = (COMOP_PI * a) / 180.0f;
        a = add_angf(comp->ene_info.ang[1], a);
        comp->move_loc[0] = comp->move_loc[0] + p->dist[0] * fsmath_SinVU0(a);
        comp->move_loc[2] = comp->move_loc[2] + p->dist[0] * fsmath_CosVU0(a);
        if (p->dist[2] != 0.0f) {
            comp->move_loc[1] = comp->move_loc[1] + p->dist[2];
        } else {
            r = randf();
            float b = p->ang[2] * (r - 0.5f);
            b = 2.0f * b;
            b = p->ang[0] + b;
            b = (COMOP_PI * b) / 180.0f;
            b = add_angf(comp->ene_info.ang[0], b);
            float sn = fsmath_SinVU0(b);
            stCom *c = comp;
            c->move_loc[1] = c->move_loc[1] - c->elenxz * sn;
        }
        break;
    }
    default:
        break;
    }
    comp->f_no_avoid = 0;
}

/* circling ops start on the side away from the enemy's facing (flipped on a roll when nearly head-on) */
static int init_stdvsene(stComOpMng *opmngp, stOpData *opdatap, struct_27448a9f *wkp)
{
    PS2_FRAME(0x30);
    unsigned char t = opdatap->param.vsene.mv_type;
    if (t != 1 && t != 2) return 1;
    wkp->sideway = comp->etoc_ang_y <= 0.0f ? -1 : 1;
    float et = comp->etoc_ang_y;
    if (comop_abs(et) < std::bit_cast<float>(0x3f490fdbu)) {   /* 45 degrees */
        float k = 1.0f - fz_equal(et, 0.0f, std::bit_cast<float>(0x3e32b8c3u));   /* 10 degrees */
        int r = ps2_rand();
        if (ps2_ftoi(100.0f * k) < ps2_mod32(r, 100)) wkp->sideway = (char)-wkp->sideway;
    }
    return 1;
}

/* distance fit (calc[0] / calc[1] by dist_type) and facing fit (calc[2] / calc[3]) */
static float vl_stdvsene(stComOpMng *opmngp)
{
    PS2_FRAME(0x30);
    stComOpParamVsEnemy *p = &opmngp->datap->param.vsene;
    float f;
    if (p->dist_type == 2) {
        float h = p->calc[1] / 2.0f;
        f = fz_large(comp->elenxz, p->calc[0] + h, h);
    } else if (p->dist_type == 1) {
        float h = p->calc[1] / 2.0f;
        f = fz_small(comp->elenxz, p->calc[0] - h, h);
    } else {
        f = fz_equal(comp->elenxz, p->calc[0], p->calc[1]);
    }
    f = f + fz_equal(comop_abs(comp->ctoe_ang_y), comop_abs(p->calc[2]), p->calc[3]);
    return 0.5f * f;
}

int COMOP_InitManage(stComOpMng *opmngp, stOpData *opdatap)
{
    PS2_FRAME(0x30);
    if (opdatap->progno < 0 || opdatap->progno >= 11) return 0;
    memset(opmngp, 0, 0x80);
    struct_68d17979 *t = &opfunctable[(int)opdatap->progno];
    opmngp->datap = opdatap;
    opmngp->value = opmngp->datap->value_init;
    opmngp->init = t->initfunc;
    opmngp->val_non = t->nosel_calcvalfunc;
    opmngp->val_sel = t->onsel_calcvalfunc;
    opmngp->sel_init = t->onselfunc;
    opmngp->main = t->opfunc;
    opmngp->exit = t->endfunc;
    opmngp->mvchk = t->mvchkfunc;
    if (opmngp->init && gcall(opmngp->init, opmngp, opdatap, (void *)&opmngp->wk) == 0) opmngp->value = 0;
    return 1;
}

/* the COM moved at least 0.5 in x / z */
static int xz_movechk(stComOpMng *opmngp, float *movevec)
{
    float x2 = movevec[0] * movevec[0];
    float z2 = movevec[2] * movevec[2];
    return ps2_sqrt(x2 + z2) < 0.5f ? 0 : 1;
}

#ifdef ACNB_SOURCEPORT
struct_68d17979 opfunctable[11];
void *op_avoid_missile__funcs[3] = {(void *)exec_avoid_missile_step0, (void *)exec_avoid_missile_step1,
                                    (void *)exec_avoid_missile_step2};
float (*COMOP_CalcValue__op_calcval_funcs[8])(stComCalcValue *) = {calcval_AP, calcval_EneAP, calcval_Heat,
                                                                    calcval_Energy, calcval_Bullet, calcval_React, 0, 0};
static struct comop_table_init {
    comop_table_init()
    {
        auto set = [](int i, void *in, void *vn, void *vs, void *si, void *op, void *ex) {
            struct_68d17979 &e = opfunctable[i];
            e.initfunc = (decltype(e.initfunc))in;
            e.nosel_calcvalfunc = (decltype(e.nosel_calcvalfunc))vn;
            e.onsel_calcvalfunc = (decltype(e.onsel_calcvalfunc))vs;
            e.onselfunc = (decltype(e.onselfunc))si;
            e.opfunc = (decltype(e.opfunc))op;
            e.endfunc = (decltype(e.endfunc))ex;
            e.mvchkfunc = (decltype(e.mvchkfunc))(void *)xz_movechk;
        };
        set(0, 0, (void *)vl_stdvsene, (void *)vl_stdvsene, (void *)init_stdvsene, (void *)op_stdvsene, 0);
        set(1, 0, (void *)vl_returnregion, (void *)vl_returnregion, 0, (void *)op_returnregion, 0);
        set(2, 0, (void *)vl_roming, (void *)vl_roming, 0, (void *)op_roming, 0);
        set(3, 0, (void *)vl_roming, (void *)vl_roming, 0, (void *)op_roming, 0);
        set(4, 0, (void *)nosel_avoid_missile, (void *)onsel_avoid_missile, 0, (void *)op_avoid_missile,
            (void *)end_avoid_missile);
        set(5, 0, (void *)noselvl_avoid_obstacle, (void *)onselvl_avoid_obstacle, (void *)init_avoid_obstacle,
            (void *)op_avoid_obstacle, 0);
        set(6, (void *)init_follow_marker, (void *)noselvl_follow_marker, (void *)onselvl_follow_marker,
            (void *)si_follow_marker, (void *)op_follow_marker, 0);
        set(7, 0, (void *)noselvl_follow_player, (void *)onselvl_follow_player, 0, (void *)op_follow_player, 0);
        set(8, 0, (void *)onselvl_escape_view, (void *)onselvl_escape_view, 0, (void *)op_escape_view, 0);
        set(9, 0, (void *)noselvl_vsene_move_route, (void *)onselvl_vsene_move_route, 0, (void *)op_vsene_move_route, 0);
        set(10, 0, (void *)noselvl_seek_route, (void *)onselvl_seek_route, 0, (void *)op_seek_route, 0);
    }
} s_comop_table_init;
#endif
