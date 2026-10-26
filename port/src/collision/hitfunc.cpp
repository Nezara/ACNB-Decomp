/* src/collision/hitfunc.cpp - collision primitives on the scratchpad work area (whole file, see hitwk.h): each
 * tests the shape at HIT_WK + 0x40 against one polygon / cylinder / sphere with the hitlib routines and sets
 * HIT_WK->info.hit (plus hit position, or the line rate tt1 / tt2 for line tests). */
#include "acnb/sdk.h"
#include "hitwk.h"

void HitFuncCylinderPolygonKabeSPR(float *P1, float *P2, float *P3)
{
    PS2_FRAME(0x10);
    HitSprWk *w = HIT_WK;
    w->info.hit = 0;
    hit_poly_prep(P1, P2, P3);
    if (GetHitYCylinderPolygon(w->cyl.P, w->cyl.H, w->cyl.R, P1, P2, w->poly.N, w->poly.V12, w->poly.V13,
                               w->poly.V23, w->info.hitpos))
        w->info.hit = 1;
}

void HitFuncCylinderPolygonYukaSPR(float *P1, float *P2, float *P3)
{
    PS2_FRAME(0x10);
    HitSprWk *w = HIT_WK;
    w->info.hit = 0;
    hit_poly_prep(P1, P2, P3);
    if (GetHitYCylinderPolygonYuka(w->cyl.P, w->cyl.H, w->cyl.R, P1, P2, w->poly.N, w->poly.V12, w->poly.V13,
                                   w->poly.V23, &w->info.hitpos[1]))
        w->info.hit = 1;
}

void HitFuncCylinderCylinderSPR(float *CyP, float CyH, float CyR)
{
    PS2_FRAME(0x10);
    HitSprWk *w = HIT_WK;
    w->info.hit = 0;
    if (GetHitYCylinderYCylinder(w->cyl.P, w->cyl.H, w->cyl.R, CyP, CyH, CyR, w->info.hitpos)) w->info.hit = 1;
}

void HitFuncSpherePolygonSPR(float *P1, float *P2, float *P3)
{
    PS2_FRAME(0x10);
    HitSprWk *w = HIT_WK;
    w->info.hit = 0;
    hit_poly_prep(P1, P2, P3);
    if (GetHitSpherePolygon(w->sphere.P, w->sphere.R, P1, P2, w->poly.N, w->poly.V12, w->poly.V13, w->poly.V23,
                            w->info.hitpos))
        w->info.hit = 1;
}

void HitFuncSphereCylinderSPR(float *CyP, float CyH, float CyR)
{
    PS2_FRAME(0x10);
    HitSprWk *w = HIT_WK;
    w->info.hit = 0;
    if (GetHitSphereYCylinder(w->sphere.P, w->sphere.R, CyP, CyH, CyR, w->info.hitpos)) w->info.hit = 1;
}

/* hit position = the point on the given sphere's surface towards the work sphere */
void HitFuncSphereSphereSPR(float *SP, float SR)
{
    HitSprWk *w = HIT_WK;
    w->info.hit = 0;
    float d[4];
    for (int i = 0; i < 3; i++) d[i] = w->sphere.P[i] - SP[i];
    float r = SR + w->sphere.R;
    float x = d[0] * d[0], y = d[1] * d[1], z = d[2] * d[2];
    if (!(z + (x + y) <= r * r)) return;
    float q = vu_rcp_len3(d);
    float *hp = w->info.hitpos;
    for (int i = 0; i < 3; i++) hp[i] = SP[i] + d[i] * q * SR;
    hp[3] = SP[3];
    w->info.hit = 1;
}

/* Moller-Trumbore against the work line P + t V: tt1 = t (0..1) */
void HitFuncLinePolygonSPR(float *P1, float *P2, float *P3)
{
    HitSprWk *w = HIT_WK;
    w->info.hit = 0;
    const float *P = w->line.P, *V = w->line.V;
    float e1[4], e2[4], s[3], p[3], q[3];
    for (int i = 0; i < 3; i++) {
        e1[i] = P2[i] - P1[i];
        e2[i] = P3[i] - P1[i];
        s[i] = P[i] - P1[i];
    }
    e1[3] = P2[3];
    e2[3] = P3[3];
    vu_cross(p, V, e2);
    memcpy(w->poly.V12, e1, 16);
    memcpy(w->poly.V13, e2, 16);
    PS2_VF_MIRROR(19, e1);
    PS2_VF_MIRROR(20, e2);
    vu_cross(q, s, e1);
    const float *E1 = w->poly.V12, *E2 = w->poly.V13;
    float det = p[2] * E1[2] + (p[0] * E1[0] + p[1] * E1[1]);
    float u = (p[0] * s[0] + p[1] * s[1]) + p[2] * s[2], v, t;
    if (!(det <= 0.0f)) {
        if (det < ps2_u2f(0x38d1b717)) return;           /* 1e-4 */
        if (u < 0.0f || !(u <= det)) return;
        v = (q[0] * V[0] + q[1] * V[1]) + q[2] * V[2];
        if (v < 0.0f || !(u + v <= det)) return;
        t = (q[0] * E2[0] + q[1] * E2[1]) + q[2] * E2[2];
        if (t < 0.0f || !(t <= det)) return;
    } else {
        if (!(det <= ps2_u2f(0xb8d1b717))) return;       /* -1e-4 */
        if (!(u <= 0.0f) || u < det) return;
        v = (q[0] * V[0] + q[1] * V[1]) + q[2] * V[2];
        if (!(v <= 0.0f) || u + v < det) return;
        t = (q[0] * E2[0] + q[1] * E2[1]) + q[2] * E2[2];
        if (!(t <= 0.0f) || t < det) return;
    }
    w->info.hit = 1;
    w->info.tt1 = t / det;
}

void HitFuncLineCylinderSPR(float *CyP, float CyH, float CyR)
{
    PS2_FRAME(0x10);
    HitSprWk *w = HIT_WK;
    w->info.hit = 0;
    if (GetHitLineYCylinder(w->line.P, w->line.V, CyP, CyH, CyR, &w->info.tt1, &w->info.tt2)) w->info.hit = 1;
}

void HitFuncLineSphereSPR(float *SP, float SR)
{
    PS2_FRAME(0x10);
    HitSprWk *w = HIT_WK;
    w->info.hit = 0;
    if (GetHitLineSphere(w->line.P, w->line.V, SP, SR, &w->info.tt1, &w->info.tt2)) w->info.hit = 1;
}

/* the triangle's bounding box overlaps Min..Max */
int HitFuncPolygonBBCheck(float *P1, float *P2, float *P3, float *Min, float *Max)
{
    float mn[3], mx[3];
    for (int i = 0; i < 3; i++) {
        mn[i] = vu_min(vu_min(P1[i], P2[i]), P3[i]);
        mx[i] = vu_max(vu_max(P1[i], P2[i]), P3[i]);
    }
    for (int i = 0; i < 3; i++)
        if (!(Min[i] <= mx[i])) return 0;
    for (int i = 0; i < 3; i++)
        if (Max[i] < mn[i]) return 0;
    return 1;
}

void HitFuncLineCylinderCheckSPR(float *CyP, float CyH, float CyR)
{
    PS2_FRAME(0x10);
    HitSprWk *w = HIT_WK;
    w->info.hit = 0;
    if (IsHitLineYCylinder(w->line.P, w->line.V, CyP, CyH, CyR)) w->info.hit = 1;
}

void HitFuncSphereLineCheckSPR(float *SP, float SR)
{
    PS2_FRAME(0x10);
    HitSprWk *w = HIT_WK;
    w->info.hit = 0;
    if (IsHitLineSphere(w->line.P, w->line.V, SP, SR)) w->info.hit = 1;
}

/* records the hit model in the result (mdl_type only for stage / object / AC types 1, 2, 8) */
void HitFuncSetHitMdl(StModel *pHitMdl)
{
    HitSprWk *w = HIT_WK;
    if (pHitMdl->type == 8) w->info.mdl_type = 8;
    else if (pHitMdl->type == 2) w->info.mdl_type = 2;
    else if (pHitMdl->type == 1) w->info.mdl_type = 1;
    w->info.mdl_id = pHitMdl->aid;
    w->info.pHitMdl = pHitMdl;
}
