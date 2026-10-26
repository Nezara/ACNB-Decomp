/* src/hitlib/hit_info.cpp - hit tests with hit information (whole file): cylinder / Y-cylinder / sphere against
 * polygons and each other, line against cylinders. A cylinder is CyP + h CyN CyH (h in 0..1, radius CyR); a
 * Y-cylinder hangs from its top centre YCyP down by YCyH. Polygons as in hit_basic.cpp (P1, P2, N, edges V12,
 * V13, V23). Vector temporaries live at their original stack slots (see hit_basic.cpp). */
#include "acnb/port.h"
#include "acnb/ps2fp.h"

static const float EPS = 0.0001f;
static const float FMAX = ps2_u2f(0x7f7fffff);

/* GetHitLineSphere's body without its frame (GetHitLineCylinder inlines it) */
static inline int line_sphere_clamped(float *LP, float *LV, float *SP, float SR, float *pT1, float *pT2)
{
    if (!GetHitRateLineSphere(LP, LV, SP, SR, pT1, pT2)) return 0;
    if (!(*pT1 <= 1.0f) || *pT2 < 0.0f) return 0;
    if (*pT1 < 0.0f) *pT1 = 0.0f;
    if (!(*pT2 <= 1.0f)) *pT2 = 1.0f;
    return 1;
}

/* cylinder against the polygon's three edges: Pos = the edge point nearest to the axis (hit_in: report the nearest
 * one even if no edge hits - the distances are then stale; every caller passes 0) */
static int __GetHitCylinderPoligonLine(float *CyP, float *CyN, float CyH, float CyR, float *P1, float *P2, float *V12,
                                       float *V13, float *V23, int hit_in, float *Pos)
{
    PS2_FRAME(0xf0);
    PS2_STACK(float, d12, 0xec);
    PS2_STACK(float, d13, 0xe8);
    PS2_STACK(float, d23, 0xe4);
    PS2_STACK(float[4], p12, 0xd0);
    PS2_STACK(float[4], p13, 0xc0);
    PS2_STACK(float[4], p23, 0xb0);
    unsigned mask = 0;
    if (GetHitPosCylinderLine(P1, V12, CyP, CyN, CyH, CyR, &d12, p12)) mask |= 2;
    if (GetHitPosCylinderLine(P1, V13, CyP, CyN, CyH, CyR, &d13, p13)) mask |= 4;
    if (GetHitPosCylinderLine(P2, V23, CyP, CyN, CyH, CyR, &d23, p23)) mask |= 8;
    if (!mask && !hit_in) return 0;
    if (!hit_in) {
        if (!(mask & 2)) d12 = FMAX;
        if (!(mask & 4)) d13 = FMAX;
        if (!(mask & 8)) d23 = FMAX;
    }
    if (d12 < d13)
        CopyVector(Pos, d12 < d23 ? p12 : p23);
    else
        CopyVector(Pos, d13 < d23 ? p13 : p23);
    return 1;
}

#ifndef ACNB_SOURCEPORT
/* MIPS -> native wrapper: a0 CyP, a1 CyN, a2 P1, a3 P2, t0 V12, t1 V13, t2 V23, t3 hit_in, Pos at the caller's
 * sp+0; f12 CyH, f13 CyR */
static void bw____GetHitCylinderPoligonLine(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    float *CyP = (float *)bridge_ptr(c->gpr[A0].uw[0]), *CyN = (float *)bridge_ptr(c->gpr[A1].uw[0]);
    float *P1 = (float *)bridge_ptr(c->gpr[A2].uw[0]), *P2 = (float *)bridge_ptr(c->gpr[A3].uw[0]);
    float *V12 = (float *)bridge_ptr(c->gpr[T0].uw[0]), *V13 = (float *)bridge_ptr(c->gpr[T1].uw[0]);
    float *V23 = (float *)bridge_ptr(c->gpr[T2].uw[0]);
    int hit_in = c->gpr[T3].sw[0];
    float *Pos = (float *)bridge_ptr(rd32(sp));
    float CyH = c->fpr[12].f, CyR = c->fpr[13].f;
    int r = __GetHitCylinderPoligonLine(CyP, CyN, CyH, CyR, P1, P2, V12, V13, V23, hit_in, Pos);
    c->gpr[V0].sd[0] = r;
}

/* a0 CyP, a1 CyN, a2 P1, a3 P2, t0 N, t1 V12, t2 V13, t3 V23, HitPos at the caller's sp+0; f12 CyH, f13 CyR */
static void bw__GetHitCylinderPolygon(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    float *CyP = (float *)bridge_ptr(c->gpr[A0].uw[0]), *CyN = (float *)bridge_ptr(c->gpr[A1].uw[0]);
    float *P1 = (float *)bridge_ptr(c->gpr[A2].uw[0]), *P2 = (float *)bridge_ptr(c->gpr[A3].uw[0]);
    float *N = (float *)bridge_ptr(c->gpr[T0].uw[0]), *V12 = (float *)bridge_ptr(c->gpr[T1].uw[0]);
    float *V13 = (float *)bridge_ptr(c->gpr[T2].uw[0]), *V23 = (float *)bridge_ptr(c->gpr[T3].uw[0]);
    float *HitPos = (float *)bridge_ptr(rd32(sp));
    float CyH = c->fpr[12].f, CyR = c->fpr[13].f;
    int r = GetHitCylinderPolygon(CyP, CyN, CyH, CyR, P1, P2, N, V12, V13, V23, HitPos);
    c->gpr[V0].sd[0] = r;
}
#endif

/* Original quirk kept: the rate t of the axis through the polygon's plane is measured along CyN but then used as a
 * rate along CyN CyH (clamped to 0..1, i.e. one unit of CyN instead of the cylinder's height). */
int GetHitCylinderPolygon(float *CyP, float *CyN, float CyH, float CyR, float *P1, float *P2, float *N, float *V12,
                          float *V13, float *V23, float *HitPos)
{
    PS2_FRAME(0x110);
    PS2_STACK(float[4], H, 0xf0);
    PS2_STACK(float, t, 0x10c);
    PS2_STACK(float[4], e, 0xe0);
    PS2_STACK(float[4], v, 0xd0);
    PS2_STACK(float[4], c, 0xc0);
    ScaleVector(H, CyN, CyH);
    float D = -InnerProduct(N, P1);
    if (!GetHitRateLinePlane(CyP, CyN, N, D, &t)) {
        /* axis parallel to the plane: perpendicular foot of CyP */
        SetVector(v, -N[0], -N[1], -N[2], 1.0f);
        GetHitRateLinePlane(CyP, v, N, D, &t);
        ScaleVector(v, v, t);
        if (!(Dist2Vector(v) <= CyR * CyR)) return 0;
        AddVector(c, CyP, v);
        if (IsInPointTriangle(c, P1, V12, V13)) {
            CopyVector(HitPos, c);
            return 1;
        }
        return __GetHitCylinderPoligonLine(CyP, CyN, CyH, CyR, P1, P2, V12, V13, V23, 0, HitPos) != 0;
    }
    SetLineVector(e, CyP, H, t);
    if (!(t <= 1.0f)) {
        if (GetHitPosCirclePolygon(e, CyN, CyR, P1, P2, V12, V13, V23, HitPos)) return 1;
    } else if (t < 0.0f) {
        if (GetHitPosCirclePolygon(CyP, CyN, CyR, P1, P2, V12, V13, V23, HitPos)) return 1;
    } else if (GetHitRateLinePolygon(CyP, H, P1, V12, V13, &t) && !(t < 0.0f) && t <= 1.0f) {
        SetLineVector(HitPos, CyP, H, t);
        return 1;
    }
    return __GetHitCylinderPoligonLine(CyP, CyN, CyH, CyR, P1, P2, V12, V13, V23, 0, HitPos) != 0;
}

/* Y-cylinder standing on a floor polygon: *pY = the floor height under it; returns 1 = floor near the top (lift
 * the cylinder onto it), 2 = near the bottom, 0 = no contact. */
int GetHitYCylinderPolygonYuka(float *YCyP, float YCyH, float YCyR, float *P1, float *P2, float *N, float *V12,
                               float *V13, float *V23, float *pY)
{
    PS2_FRAME(0x120);
    PS2_STACK(float[4], axis, 0x100);
    PS2_STACK(float, t, 0x11c);
    PS2_STACK(float[4], n, 0xf0);
    PS2_STACK(float[4], ph, 0xe0);      /* rim point on the high side of the slope */
    PS2_STACK(float[4], pl, 0xd0);      /* rim point on the low side */
    PS2_STACK(float, t1, 0x118);
    PS2_STACK(float, t2, 0x114);
    SetVector(axis, 0.0f, -YCyH, 0.0f, 1.0f);
    if (1.0f - fabsf(N[1]) < EPS) {
        /* flat floor */
        if ((GetHitRateLinePolygon(YCyP, axis, P1, V12, V13, &t) && !(t < 0.0f) && t <= 1.0f) ||
            IsHitLineYCylinder(P1, V12, YCyP, YCyH, YCyR) || IsHitLineYCylinder(P1, V13, YCyP, YCyH, YCyR) ||
            IsHitLineYCylinder(P2, V23, YCyP, YCyH, YCyR)) {
            *pY = P1[1];
            return fabsf(P1[1] - YCyP[1]) < 0.5f * YCyH ? 2 : 1;
        }
        return 0;
    }
    int in_h = 0, in_l = 0;
    GetHitRateLinePlane(YCyP, axis, N, -InnerProduct(N, P1), &t);
    float len = NormalizeXZ(n, N);
    float slope = YCyR * ps2_div(len, fabsf(N[1]));
    float yc = YCyP[1] - YCyH * t;      /* plane height on the axis */
    float lo = yc - slope, hi = yc + slope;
    if (!(lo <= YCyP[1])) return 0;
    if (hi < YCyP[1] - YCyH) return 0;
    ph[1] = hi;
    pl[1] = lo;
    if (!(N[1] <= 0.0f)) {
        float dx = n[0] * YCyR, dz = n[2] * YCyR;
        ph[0] = YCyP[0] - dx;
        ph[2] = YCyP[2] - dz;
        pl[0] = YCyP[0] + dx;
        pl[2] = YCyP[2] + dz;
    } else {
        float dx = n[0] * YCyR, dz = n[2] * YCyR;
        ph[0] = YCyP[0] + dx;
        ph[2] = YCyP[2] + dz;
        pl[0] = YCyP[0] - dx;
        pl[2] = YCyP[2] - dz;
    }
    if (IsInPointTriangle(ph, P1, V12, V13)) in_h = 1;
    if (IsInPointTriangle(pl, P1, V12, V13)) in_l = 1;
    if (!in_h || !in_l) {
        /* a rim point is off the polygon: height range of the edges inside the cylinder instead */
        int any = 0;
        float ymax = 0.0f, ymin = 0.0f;
        float *eP[3] = {P1, P1, P2}, *eV[3] = {V12, V13, V23};
        for (int i = 0; i < 3; i++) {
            if (!GetHitLineYCylinder(eP[i], eV[i], YCyP, YCyH, YCyR, &t1, &t2)) continue;
            float vy = eV[i][1], a, b;
            if (vy < 0.0f) {
                a = eP[i][1] + vy * t1;
                b = eP[i][1] + vy * t2;
            } else {
                a = eP[i][1] + vy * t2;
                b = eP[i][1] + vy * t1;
            }
            if (any) {
                if (!(a <= ymax)) ymax = a;
                if (b < ymin) ymin = b;
            } else {
                ymax = a;
                ymin = b;
            }
            any = 1;
        }
        if (!any) {
            if (in_h) {
                *pY = hi;
                return 1;
            }
            if (!in_l) return 0;
            *pY = lo;
            return 2;
        }
        if (!in_h) hi = ymax;
        if (!in_l) lo = ymin;
    }
    float mid = YCyP[1] - 0.5f * YCyH;
    if (fabsf(mid - lo) < fabsf(mid - hi)) {
        *pY = lo;
        return 2;
    }
    *pY = hi;
    return 1;
}

int GetHitYCylinderPolygon(float *YCyP, float YCyH, float YCyR, float *P1, float *P2, float *N, float *V12, float *V13,
                           float *V23, float *HitPos)
{
    PS2_FRAME(0xc0);
    PS2_STACK(float[4], down, 0xb0);
    SetVector(down, 0.0f, -1.0f, 0.0f, 1.0f);
    return GetHitCylinderPolygon(YCyP, down, YCyH, YCyR, P1, P2, N, V12, V13, V23, HitPos);
}

/* HitVec = push-out of cylinder 1 from cylinder 2: xz along the centre line by the overlap, y by the smaller
 * vertical overlap */
int GetHitYCylinderYCylinder(float *YCyP1, float YCyH1, float YCyR1, float *YCyP2, float YCyH2, float YCyR2,
                             float *HitVec)
{
    PS2_FRAME(0x80);
    PS2_STACK(float[4], d, 0x70);
    PS2_STACK(float[4], n, 0x60);
    SubVector(d, YCyP1, YCyP2);
    float rs = YCyR1 + YCyR2;
    if (!(Dist2VectorXZ(d) <= rs * rs)) return 0;
    float bot2 = YCyP2[1] - YCyH2;
    float bot1 = YCyP1[1] - YCyH1;
    if (YCyP1[1] < bot2) return 0;
    if (!(bot1 <= YCyP2[1])) return 0;
    float k = rs - NormalizeXZ(n, d);
    HitVec[0] = n[0] * k;
    HitVec[2] = n[2] * k;
    if (bot1 < bot2) {
        float top2 = YCyP2[1], top1 = YCyP1[1];
        if (top1 < top2) {
            HitVec[1] = bot2 - top1;
        } else {
            float a = top1 - bot2, b = top2 - bot1;
            HitVec[1] = b <= a ? bot1 - top2 : a;
        }
    } else {
        float top2 = YCyP2[1], top1 = YCyP1[1];
        if (!(top1 <= top2)) {
            HitVec[1] = top2 - bot1;
        } else {
            float a = top2 - bot1, b = top1 - bot2;
            HitVec[1] = b <= a ? bot2 - top1 : a;
        }
    }
    return 1;
}

/* sphere against a polygon: HitPos = the plane foot of the sphere's centre line if inside, else the nearest edge
 * point within SR */
int GetHitSpherePolygon(float *SP, float SR, float *P1, float *P2, float *N, float *V12, float *V13, float *V23,
                        float *HitPos)
{
    PS2_FRAME(0x170);
    PS2_STACK(float[4], a, 0x130);
    PS2_STACK(float[4], b, 0x150);
    PS2_STACK(float[4], c, 0x140);
    PS2_STACK(float, t, 0x16c);
    PS2_STACK(float[4], v, 0x110);
    PS2_STACK(float[4], p, 0x120);
    PS2_STACK(float[4], q12, 0x100);
    PS2_STACK(float[4], r12, 0xf0);
    PS2_STACK(float[4], q13, 0xe0);
    PS2_STACK(float[4], r13, 0xd0);
    PS2_STACK(float[4], q23, 0xc0);
    PS2_STACK(float[4], r23, 0xb0);
    ScaleVector(a, N, SR);
    AddVector(b, SP, a);
    ScaleVector(c, a, -2.0f);
    GetHitRateLinePlane(b, c, N, -InnerProduct(N, P1), &t);
    if (t < 0.0f || !(t <= 1.0f)) return 0;
    ScaleVector(v, c, t);
    AddVector(p, b, v);
    if (IsInPointTriangle(p, P1, V12, V13)) {
        CopyVector(HitPos, p);
        return 1;
    }
    int hit = 0;
    float rr = SR * SR;
    SetLineVector(q12, P1, V12, GetNearRatePointLine(SP, P1, V12));
    SubVector(r12, q12, SP);
    float best = Dist2Vector(r12);
    if (best <= rr) {
        CopyVector(HitPos, q12);
        hit = 1;
    }
    SetLineVector(q13, P1, V13, GetNearRatePointLine(SP, P1, V13));
    SubVector(r13, q13, SP);
    float d = Dist2Vector(r13);
    if (d <= rr) {
        CopyVector(p, q13);
        if (!hit || d < best) {
            CopyVector(HitPos, p);
            best = d;
            hit = 1;
        }
    }
    SetLineVector(q23, P2, V23, GetNearRatePointLine(SP, P2, V23));
    SubVector(r23, q23, SP);
    d = Dist2Vector(r23);
    if (d <= rr) {
        CopyVector(p, q23);
        if (!hit || d < best) {
            CopyVector(HitPos, p);
            hit = 1;
        }
    }
    return hit;
}

/* returns 1 = touching the bottom disc, 2 = the top disc, 3 = the side; HitPos on the cylinder's surface */
int GetHitSphereYCylinder(float *SP, float SR, float *YCyP, float YCyH, float YCyR, float *HitPos)
{
    PS2_FRAME(0x60);
    PS2_STACK(float[4], d, 0x50);
    SubVector(d, YCyP, SP);
    if (!(d[1] <= SR + YCyH)) return 0;
    if (d[1] < -SR) return 0;
    float bot = YCyP[1] - YCyH;
    if (!(bot <= SP[1])) {
        float h = bot - SP[1];
        SR = ps2_sqrt(SR * SR - h * h);     /* radius of the sphere's cut at the bottom plane */
    } else if (YCyP[1] < SP[1]) {
        float h = SP[1] - YCyP[1];
        SR = ps2_sqrt(SR * SR - h * h);
    }
    float dist = ps2_sqrt(d[0] * d[0] + d[2] * d[2]);
    if (!(dist <= YCyR + SR)) return 0;
    if (SP[1] < YCyP[1] - YCyH) {
        if (dist < YCyR) {
            HitPos[0] = SP[0];
            HitPos[2] = SP[2];
        } else {
            HitPos[0] = YCyP[0] - YCyR * ps2_div(d[0], dist);
            HitPos[2] = YCyP[2] - YCyR * ps2_div(d[2], dist);
        }
        HitPos[1] = YCyP[1] - YCyH;
        return 1;
    }
    if (!(SP[1] <= YCyP[1])) {
        if (dist < YCyR) {
            HitPos[0] = SP[0];
            HitPos[2] = SP[2];
        } else {
            HitPos[0] = YCyP[0] - YCyR * ps2_div(d[0], dist);
            HitPos[2] = YCyP[2] - YCyR * ps2_div(d[2], dist);
        }
        HitPos[1] = YCyP[1];
        return 2;
    }
    if (dist < EPS) {
        HitPos[0] = SP[0];
        HitPos[1] = SP[1];
        HitPos[2] = SP[2] + YCyR;
    } else {
        HitPos[0] = YCyP[0] - YCyR * (d[0] / dist);
        HitPos[1] = SP[1];
        HitPos[2] = YCyP[2] - YCyR * (d[2] / dist);
    }
    return 3;
}

/* rates where the line enters / leaves the cylinder, clamped to 0..1 */
int GetHitLineCylinder(float *LP, float *LV, float *CyP, float *CyN, float CyH, float CyR, float *pT1, float *pT2)
{
    PS2_FRAME(0x130);
    PS2_STACK(float[4], H, 0xf0);
    PS2_STACK(float[4], top, 0xe0);
    PS2_STACK(float, t, 0x12c);
    PS2_STACK(float[4], v, 0xd0);
    PS2_STACK(float[4], A, 0x110);
    PS2_STACK(float[4], B, 0x100);
    PS2_STACK(float[4], C, 0xc0);
    PS2_STACK(float[4], E, 0xb0);
    PS2_STACK(float, t1, 0x128);
    PS2_STACK(float, t2, 0x124);
    PS2_STACK(float[4], a, 0xa0);
    PS2_STACK(float[4], b, 0x90);
    PS2_STACK(float[4], lend, 0x80);
    ScaleVector(H, CyN, CyH);
    AddVector(top, CyP, H);
    float dtop = -InnerProduct(CyN, top);
    float dbot = -InnerProduct(CyN, CyP);
    if (GetHitRateLinePlane(LP, LV, CyN, dtop, &t)) {
        /* project the line between the cap planes along the axis onto the bottom plane, intersect the circle */
        ScaleVector(v, LV, t);
        AddVector(A, LP, v);
        GetHitRateLinePlane(LP, LV, CyN, dbot, &t);
        ScaleVector(v, LV, t);
        AddVector(B, LP, v);
        SubVector(C, A, H);
        SubVector(E, B, C);
        if (!line_sphere_clamped(C, E, CyP, CyR, &t1, &t2)) return 0;
        /* back to rates on the original line */
        SubVector(E, B, A);
        SetLineVector(v, A, E, t1);
        SubVector(a, v, LP);
        SetLineVector(v, A, E, t2);
        SubVector(b, v, LP);
        float l = Dist2Vector(LV);
        t1 = ps2_sqrt(ps2_div(Dist2Vector(a), l));
        t2 = ps2_sqrt(ps2_div(Dist2Vector(b), l));
        if (InnerProduct(a, LV) < 0.0f) t1 = -t1;
        if (InnerProduct(b, LV) < 0.0f) t2 = -t2;
        if (!(t1 <= t2)) {
            float s = t1;
            t1 = t2;
            t2 = s;
        }
        if (!(t1 <= 1.0f) || t2 < 0.0f) return 0;
        *pT1 = t1 < 0.0f ? 0.0f : t1;
        *pT2 = !(t2 <= 1.0f) ? 1.0f : t2;
        return 1;
    }
    /* line parallel to the caps: both ends' heights, then the circle test in the bottom plane */
    AddVector(lend, LP, LV);
    float p = InnerProduct(CyN, LP);
    float q = InnerProduct(CyN, lend);
    float first = dtop < dbot ? -dtop : -dbot, second = dtop < dbot ? -dbot : -dtop;
    if (!(p <= first) && !(q <= first)) return 0;
    if (p < second && q < second) return 0;
    GetHitRateLinePlane(LP, CyN, CyN, dbot, &t);
    ScaleVector(v, CyN, t);
    AddVector(A, LP, v);
    GetHitRateLinePlane(lend, CyN, CyN, dbot, &t);
    ScaleVector(v, CyN, t);
    AddVector(B, lend, v);
    SubVector(v, B, A);
    return line_sphere_clamped(A, v, CyP, CyR, pT1, pT2);
}

int GetHitLineYCylinder(float *LP, float *LV, float *YCyP, float YCyH, float YCyR, float *pT1, float *pT2)
{
    PS2_FRAME(0x60);
    PS2_STACK(float[4], d, 0x50);
    SubVector(d, LP, YCyP);
    float a = Dist2VectorXZ(LV);
    float c = Dist2VectorXZ(d) - YCyR * YCyR;
    float tmin, tmax;
    if (a < EPS) {
        tmin = 0.0f;
        if (!(c <= 0.0f)) return 0;
        tmax = 1.0f;
    } else {
        float b = d[0] * LV[0] + d[2] * LV[2];
        float disc = b * b - a * c;
        if (disc < 0.0f) return 0;
        float sq = ps2_sqrt(disc);
        float nb = -b;
        tmin = (nb - sq) / a;
        tmax = (nb + sq) / a;
        if (!(tmin <= 1.0f) || tmax < 0.0f) return 0;
        if (tmin < 0.0f) tmin = 0.0f;
        if (!(tmax <= 1.0f)) tmax = 1.0f;
    }
    if (fabsf(LV[1]) < EPS) {
        if (!(d[1] <= 0.0f)) return 0;
        if (d[1] < -YCyH) return 0;
    } else {
        float lo = -(YCyH + d[1]) / LV[1];
        float hi = -d[1] / LV[1];
        if (!(lo <= hi)) {
            float s = lo;
            lo = hi;
            hi = s;
        }
        if (tmin < lo) tmin = lo;
        if (!(tmax <= hi)) tmax = hi;
        if (tmax < tmin) return 0;
    }
    *pT1 = tmin;
    *pT2 = tmax;
    return 1;
}

int GetHitLineSphere(float *LP, float *LV, float *SP, float SR, float *pT1, float *pT2)
{
    PS2_FRAME(0x30);
    return line_sphere_clamped(LP, LV, SP, SR, pT1, pT2);
}
