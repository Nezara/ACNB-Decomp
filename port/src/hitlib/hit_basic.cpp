/* src/hitlib/hit_basic.cpp - basic hit tests of the hit library (whole file): points on lines, line rates against
 * planes / spheres / polygons, point-in-triangle. A line is LP + t LV, t in 0..1; a polygon is P1 with edges
 * V12 = P2 - P1, V13 = P3 - P1, V23 = P3 - P2.
 * Vector temporaries live at their original stack slots (PS2_STACK): results copied with CopyVector carry the w of
 * a temporary that only got xyz written, i.e. stale stack, into the caller's output. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"

static const float EPS = 0.0001f;
static const float FLT_EPS = 1.1920929e-07f;

/* ans = LP + t LV with t clamped to 0..1; returns the clamped t */
float SetLineVector(float *ans, float *LP, float *LV, float t)
{
    PS2_FRAME(0x50);
    PS2_STACK(float[4], v, 0x40);
    if (!(t <= 1.0f)) {
        AddVector(ans, LP, LV);
        return 1.0f;
    }
    if (t < 0.0f) {
        CopyVector(ans, LP);
        return 0.0f;
    }
    ScaleVector(v, LV, t);
    AddVector(ans, LP, v);
    return t;
}

/* Where the line meets the cylinder CyP + h CyN CyH (radius CyR): Pos = the point of the line's inside segment
 * nearest to the axis, *pDist2 = its squared distance from the axis segment. */
int GetHitPosCylinderLine(float *LP, float *LV, float *CyP, float *CyN, float CyH, float CyR, float *pDist2, float *Pos)
{
    PS2_FRAME(0x120);
    PS2_STACK(float, t1, 0x11c);
    PS2_STACK(float, t2, 0x118);
    PS2_STACK(float[4], A, 0x100);      /* entry point */
    PS2_STACK(float[4], D, 0xf0);       /* inside segment */
    PS2_STACK(float[4], H, 0xe0);       /* axis */
    PS2_STACK(float[4], v, 0xd0);
    PS2_STACK(float[4], W, 0xc0);
    PS2_STACK(float[4], s, 0xb0);
    PS2_STACK(float[4], s2, 0xa0);
    PS2_STACK(float[4], r, 0x90);
    if (!GetHitLineCylinder(LP, LV, CyP, CyN, CyH, CyR, &t1, &t2)) return 0;
    ScaleVector(v, LV, t1);
    AddVector(A, LP, v);
    ScaleVector(D, LV, t2);
    SubVector(D, D, v);
    ScaleVector(H, CyN, CyH);
    float hh = Dist2Vector(H);
    float hd = InnerProduct(H, D);
    float dd = Dist2Vector(D);
    SubVector(W, CyP, A);
    float wh = InnerProduct(W, H);
    float wd = InnerProduct(W, D);
    float det = hh * dd - hd * hd;
    if (fabsf(det) < EPS) {
        /* segment parallel to the axis: its middle */
        ScaleVector(v, D, 0.5f * (t1 + t2));
        AddVector(Pos, A, v);
        SubVector(r, Pos, CyP);
        float k = InnerProduct(r, H);
        float l = Dist2Vector(H);
        k = l < FLT_EPS ? 0.0f : k / l;
        ScaleVector(v, H, k);
        AddVector(v, CyP, v);
        SubVector(v, Pos, v);
        *pDist2 = Dist2Vector(v);
        return 1;
    }
    float sa = (wd * hd - wh * dd) / det;   /* rate on the axis */
    float sd = (wd * hh - wh * hd) / det;   /* rate on the segment */
    if (!(sd <= 1.0f)) {
        AddVector(Pos, A, D);
    } else if (sd < 0.0f) {
        CopyVector(Pos, A);
    } else {
        ScaleVector(s, D, sd);
        AddVector(Pos, A, s);
    }
    if (!(sa <= 1.0f)) {
        AddVector(v, CyP, H);
    } else if (sa < 0.0f) {
        CopyVector(v, CyP);
    } else {
        ScaleVector(s2, H, sa);
        AddVector(v, CyP, s2);
    }
    SubVector(v, Pos, v);
    *pDist2 = Dist2Vector(v);
    return 1;
}

/* Circle (centre CrP, normal CrN, radius CrR) against a polygon: the chord of the polygon in the circle's plane must
 * reach into the circle; Pos = the chord point nearest to the centre. */
int GetHitPosCirclePolygon(float *CrP, float *CrN, float CrR, float *P1, float *P2, float *V12, float *V13, float *V23,
                           float *Pos)
{
    PS2_FRAME(0xf0);
    PS2_STACK(float[4], LP, 0xe0);
    PS2_STACK(float[4], LV, 0xd0);
    PS2_STACK(float[4], d, 0xc0);
    PS2_STACK(float[4], c, 0xb0);
    PS2_STACK(float[4], v, 0xa0);
    if (!GetHitLinePlanePolygon(CrN, -InnerProduct(CrN, CrP), P1, P2, V12, V13, V23, LP, LV)) return 0;
    float a = Dist2Vector(LV);
    SubVector(d, LP, CrP);
    float b = InnerProduct(LV, d);
    float disc = b * b - a * (Dist2Vector(d) - CrR * CrR);
    if (disc < 0.0f) return 0;
    float sq = ps2_sqrt(disc);
    float nb = -b;
    float tb = ps2_div(nb + sq, a);
    float ta = ps2_div(nb - sq, a);
    if (!(ta <= 1.0f) || tb < 0.0f) return 0;
    SubVector(c, CrP, LP);
    float k = InnerProduct(c, LV);
    float l = Dist2Vector(LV);
    k = l < FLT_EPS ? 0.0f : k / l;
    if (!(k <= 1.0f)) {
        AddVector(Pos, LP, LV);
    } else if (k < 0.0f) {
        CopyVector(Pos, LP);
    } else {
        ScaleVector(v, LV, k);
        AddVector(Pos, LP, v);
    }
    return 1;
}

/* The chord where the plane PN.x + D = 0 cuts the polygon: LP = first crossing edge point, LV = to the second.
 * Original quirk kept: with one crossing on V12 / V13 and none on V23 the end point is never written (stale). */
int GetHitLinePlanePolygon(float *PN, float D, float *P1, float *P2, float *V12, float *V13, float *V23, float *LP, float *LV)
{
    PS2_FRAME(0xe0);
    PS2_STACK(float[4], v, 0xb0);
    PS2_STACK(float[4], e, 0xc0);       /* second point */
    PS2_STACK(float[4], s, 0xd0);       /* first point */
    int n = 0;
    float p = InnerProduct(PN, P1);
    float q = InnerProduct(PN, V12);
    if (!(fabsf(q) < EPS)) {
        float t = -(p + D) / q;
        if (!(t < 0.0f) && t <= 1.0f) {
            ScaleVector(v, V12, t);
            AddVector(s, P1, v);
            n++;
        }
    }
    p = InnerProduct(PN, P1);
    q = InnerProduct(PN, V13);
    if (!(fabsf(q) < EPS)) {
        float t = -(p + D) / q;
        if (!(t < 0.0f) && t <= 1.0f) {
            if (n) {
                ScaleVector(v, V13, t);
                AddVector(e, P1, v);
                CopyVector(LP, s);
                SubVector(LV, e, s);
                return 1;
            }
            ScaleVector(v, V13, t);
            AddVector(s, P1, v);
            n++;
        }
    }
    if (!n) return 0;
    p = InnerProduct(PN, P2);
    q = InnerProduct(PN, V23);
    if (!(fabsf(q) < EPS)) {
        float t = -(p + D) / q;
        if (!(t < 0.0f) && t <= 1.0f) {
            ScaleVector(v, V23, t);
            AddVector(e, P2, v);
        }
    }
    CopyVector(LP, s);
    SubVector(LV, e, s);
    return 1;
}

/* Moller-Trumbore: line rate *pT where the line crosses the triangle (rate itself not range-checked) */
int GetHitRateLinePolygon(float *LP, float *LV, float *P1, float *V12, float *V13, float *pT)
{
    PS2_FRAME(0x90);
    PS2_STACK(float[4], s, 0x80);
    PS2_STACK(float[4], pv, 0x70);
    PS2_STACK(float[4], qv, 0x60);
    SubVector(s, LP, P1);
    OuterProduct(pv, LV, V13);
    OuterProduct(qv, s, V12);
    float det = InnerProduct(pv, V12);
    if (fabsf(det) < EPS) return 0;
    float inv = 1.0f / det;
    float u = inv * InnerProduct(pv, s);
    float v = inv * InnerProduct(qv, LV);
    if (u < 0.0f || v < 0.0f || !(u + v <= 1.0f)) return 0;
    *pT = inv * InnerProduct(qv, V13);
    return 1;
}

/* entry / exit rates of the infinite line through the sphere (a near-zero LV counts when LP is inside: 0..1) */
int GetHitRateLineSphere(float *LP, float *LV, float *SP, float SR, float *pT1, float *pT2)
{
    PS2_FRAME(0x60);
    PS2_STACK(float[4], d, 0x50);
    SubVector(d, LP, SP);
    float a = Dist2Vector(LV);
    float b = InnerProduct(LV, d);
    float c = Dist2Vector(d) - SR * SR;
    float disc = b * b - a * c;
    if (disc < 0.0f) return 0;
    if (a < EPS) {
        if (!(c <= 0.0f)) return 0;
        *pT1 = 0.0f;
        *pT2 = 1.0f;
        return 1;
    }
    float sq = ps2_sqrt(disc);
    float nb = -b;
    *pT1 = (nb - sq) / a;
    *pT2 = (nb + sq) / a;
    return 1;
}

int GetHitRateLineYPlane(float *LP, float *LV, float Y, float *pT)
{
    if (fabsf(LV[1]) < EPS) return 0;
    *pT = (Y - LP[1]) / LV[1];
    return 1;
}

int GetHitRateLinePlane(float *LP, float *LV, float *PN, float D, float *pT)
{
    float p = InnerProduct(PN, LP);
    float q = InnerProduct(PN, LV);
    if (fabsf(q) < EPS) return 0;
    *pT = -(p + D) / q;
    return 1;
}

/* rate of the line point nearest to CP (unclamped) */
float GetNearRatePointLine(float *CP, float *LP, float *LV)
{
    PS2_FRAME(0x40);
    PS2_STACK(float[4], d, 0x30);
    SubVector(d, CP, LP);
    float k = InnerProduct(d, LV);
    float l = Dist2Vector(LV);
    return l < FLT_EPS ? 0.0f : k / l;
}

/* P (in the triangle's plane) inside the triangle: barycentrics from the 2D projection along the normal's largest
 * axis */
int IsInPointTriangle(float *P, float *P1, float *V12, float *V13)
{
    PS2_FRAME(0x80);
    PS2_STACK(float[4], d, 0x70);
    PS2_STACK(float[4], n, 0x60);
    SubVector(d, P, P1);
    OuterProduct(n, V12, V13);
    float u, v;
    int axis;
    if (!(fabsf(n[0]) <= fabsf(n[1])))
        axis = !(fabsf(n[0]) <= fabsf(n[2])) ? 0 : 2;
    else
        axis = !(fabsf(n[1]) <= fabsf(n[2])) ? 1 : 2;
    if (axis == 0) {
        u = ps2_div(d[1] * V13[2] - V13[1] * d[2], n[0]);
        v = ps2_div(-(d[1] * V12[2] - V12[1] * d[2]), n[0]);
    } else if (axis == 1) {
        u = ps2_div(d[2] * V13[0] - V13[2] * d[0], n[1]);
        v = ps2_div(-(d[2] * V12[0] - V12[2] * d[0]), n[1]);
    } else {
        u = ps2_div(d[0] * V13[1] - V13[0] * d[1], n[2]);
        v = ps2_div(-(d[0] * V12[1] - V12[0] * d[1]), n[2]);
    }
    if (u < 0.0f || v < 0.0f || !(u + v <= 1.0f)) return 0;
    return 1;
}
