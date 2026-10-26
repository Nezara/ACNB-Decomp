/* src/collision/hfunc.cpp - floor test primitives of the older hit system (whole file; scratchpad layout
 * HitOldWk in hitwk.h). The Y-cylinder under test hangs from HIT_OWK->loc down by len (radius R); y grows
 * downwards. */
#include "acnb/sdk.h"
#include "hitwk.h"

static const float NEG_ZERO = -0.0f;

/* triangle box (xz grown by R) misses the cylinder: 1 = no overlap */
int DF_HIT_RectHitCheck_YUKA(float *v1, float *v2, float *v3)
{
    HitOldWk *w = HIT_OWK;
    float R = w->R;
    for (int i = 0; i < 3; i++) {
        w->min[i] = vu_min(vu_min(v1[i], v2[i]), v3[i]);
        w->max[i] = vu_max(vu_max(v1[i], v2[i]), v3[i]);
    }
    w->min[0] = w->min[0] - R;
    w->min[2] = w->min[2] - R;
    w->max[0] = w->max[0] + R;
    w->max[2] = w->max[2] + R;
    w->min[3] = PS2_STALE_VF(19, 3);
    w->max[3] = PS2_STALE_VF(20, 3);
    PS2_VF_MIRROR(16, v1);
    PS2_VF_MIRROR(17, v2);
    PS2_VF_MIRROR(18, v3);
    PS2_VF_MIRROR(19, w->min);
    PS2_VF_MIRROR(20, w->max);
    float r21[4] = {R, PS2_STALE_VF(21, 1), PS2_STALE_VF(21, 2), PS2_STALE_VF(21, 3)};
    PS2_VF_MIRROR(21, r21);
    (void)r21;
    const float *loc = w->loc;
    if (loc[0] < w->min[0]) return 1;
    if (loc[2] < w->min[2]) return 1;
    if (!(loc[0] <= w->max[0])) return 1;
    if (!(loc[2] <= w->max[2])) return 1;
    if (w->lim < w->min[1]) return 1;
    if (!(loc[1] <= w->max[1])) return 1;
    return 0;
}

/* point v1 inside the Y-cylinder at loc (RR = radius squared) */
void DF_HIT_CylinderPoint(float *v1, float *loc, float RR, float tall)
{
    PS2_FRAME(0x10);
    PS2_STACK(float[4], d, 0x0);
    if (!(v1[1] <= loc[1]) || v1[1] < loc[1] - tall) return;
    for (int i = 0; i < 3; i++) d[i] = v1[i] - loc[i];
    d[3] = v1[3];
    PS2_VF_MIRROR(16, d);
    PS2_VF_MIRROR(17, loc);
    if (d[0] * d[0] + d[2] * d[2] < RR) HIT_OWK->hit = 1;
}

/* Y-cylinder against Y-cylinder: pos.xz = the point on cylinder 2's surface towards cylinder 1, pos.y = the top of
 * the overlap */
void DF_HIT_CylinderCylinder(float *loc, float R, float tall, float *loc2, float R2, float tall2)
{
    PS2_FRAME(0x60);
    PS2_STACK(float[4], d, 0x50);
    HitOldWk *w = HIT_OWK;
    float rr = (R + R2) * (R + R2);
    w->hit = 0;
    for (int i = 0; i < 3; i++) d[i] = loc2[i] - loc[i];
    d[3] = loc2[3];
    PS2_VF_MIRROR(16, d);
    PS2_VF_MIRROR(17, loc);
    if (!(d[0] * d[0] + d[2] * d[2] < rr)) return;
    d[1] = 0.0f;
    sceVu0Normalize(d, d);
    w->pos[0] = loc2[0] - d[0] * R2;
    w->pos[2] = loc2[2] - d[2] * R2;
    float b1 = loc[1] - tall;
    if (!(b1 <= loc2[1])) {
        w->pos[1] = b1;
        return;
    }
    float b2 = loc2[1] - tall2;
    if (!(b2 <= loc[1])) {
        w->pos[1] = b2;
        return;
    }
    w->hit = 1;
    w->pos[1] = loc[1];
}

/* the hit position of edge `edge` (0 = v1-v2, 1 = v3-v2, 2 = v3-v1): E = edge direction, C / B = its two ends
 * relative to loc (xyzw) */
static inline void edge_setup(int edge, float *v1, float *v2, float *v3, const float *loc, const float *e1,
                              const float *e2, const float *e3, float *E, float *C, float *B)
{
    const float *a = edge == 0 ? v1 : v3, *b = edge == 2 ? v1 : v2, *e = edge == 0 ? e1 : edge == 1 ? e2 : e3;
    for (int i = 0; i < 4; i++) {
        C[i] = a[i] - loc[i];
        B[i] = b[i] - loc[i];
    }
    memcpy(E, e, 16);
    PS2_VF_MIRROR(16, loc);
    PS2_VF_MIRROR(17, a);
    PS2_VF_MIRROR(18, b);
    PS2_VF_MIRROR(19, e);
    PS2_VF_MIRROR(20, C);
    PS2_VF_MIRROR(21, B);
    sceVu0Normalize(E, E);
}

/* Y-cylinder (HIT_OWK->loc, R, len) standing on triangle v1 v2 v3: hit + pos.y = floor height under it. loc is
 * HIT_OWK->loc in every caller. */
void DF_HIT_CylinderPolygon_YUKA(float *v1, float *v2, float *v3, float *loc)
{
    PS2_FRAME(0x100);
    PS2_STACK(float[4], e1, 0xf0);
    PS2_STACK(float[4], e2, 0xe0);
    PS2_STACK(float[4], e3, 0xd0);
    PS2_STACK(float[4], n, 0x90);
    PS2_STACK(float[4], E, 0xa0);
    PS2_STACK(float[4], C, 0xc0);
    PS2_STACK(float[4], B, 0xb0);
    PS2_STACK(float[4], dir, 0x80);
    HitOldWk *w = HIT_OWK;
    w->hit = 0;
    for (int i = 0; i < 3; i++) {
        e1[i] = v1[i] - v2[i];
        e2[i] = v3[i] - v2[i];
        e3[i] = v3[i] - v1[i];
    }
    e1[3] = PS2_STALE_VF(19, 3);
    e2[3] = PS2_STALE_VF(20, 3);
    e3[3] = PS2_STALE_VF(21, 3);
    vu_cross(n, e1, e2);
    n[3] = v1[3];
    PS2_VF_MIRROR(16, n);
    PS2_VF_MIRROR(17, v2);
    PS2_VF_MIRROR(18, v3);
    PS2_VF_MIRROR(19, e1);
    PS2_VF_MIRROR(20, e2);
    PS2_VF_MIRROR(21, e3);
    if (n[1] * n[1] < 1e-05f) return;
    float d = (n[0] * v1[0] + n[1] * v1[1]) + n[2] * v1[2];
    float dl = (-n[0] * loc[0] - n[1] * loc[1]) - n[2] * loc[2];
    d = d + dl;                     /* plane equation at loc (unnormalised) */
    w->dist = d / n[1];
    if (w->dist < NEG_ZERO) return;
    sceVu0Normalize(n, n);
    if (!(w->dist <= w->len)) {
        if (!(n[1] <= 0.9998f)) return;
        if (n[1] < -0.9998f) return;
    }
    float ny = n[1];
    if (!(ny <= -0.9998f) && ny < 0.9998f) {
        /* slope: probe straight down from the cylinder rim on the uphill side */
        float hxz = ps2_sqrt(n[0] * n[0] + n[2] * n[2]);
        float nyn = !(ny <= 0.0f) ? -ny : ny;
        E[0] = loc[0];
        E[1] = (loc[1] + w->dist) + ps2_div(w->R * hxz, nyn);
        E[2] = loc[2];
        if (d < 0.0f) {
            dir[0] = -n[0];
            dir[2] = -n[2];
        } else {
            dir[0] = n[0];
            dir[2] = n[2];
        }
        dir[3] = 1.0f;
        dir[1] = 0.0f;
        sceVu0Normalize(dir, dir);
        DF_HIT_LinePolygon(v1, v2, v3, E, dir, 1e+06f);
        if (w->hit) return;
        /* edges crossing the cylinder: their lowest point inside it */
        float best = 1e+07f;
        for (int k = 0; k < 3; k++) {
            edge_setup(k, v1, v2, v3, loc, e1, e2, e3, E, C, B);
            float y1, y2;
            if (E[0] < 0.001f && !(E[0] <= -0.001f)) {
                float c2 = C[0] * C[0];
                if (!(c2 <= w->RR)) continue;
                float h = ps2_sqrt(w->RR - c2);
                if (!(C[2] <= h) && !(B[2] <= h)) continue;
                if (C[2] < -h && B[2] < -h) continue;
                w->hit = 1;
                float s = ps2_div(E[1], E[2]);
                y1 = B[1] + s * (h - B[2]);
                y2 = B[1] + s * (-h - B[2]);
            } else if (E[2] < 0.001f && !(E[2] <= -0.001f)) {
                float c2 = C[2] * C[2];
                if (!(c2 <= w->RR)) continue;
                float h = ps2_sqrt(w->RR - c2);
                if (!(C[0] <= h) && !(B[0] <= h)) continue;
                if (C[0] < -h && B[0] < -h) continue;
                w->hit = 1;
                float s = E[1] / E[0];
                y1 = B[1] + s * (h - B[0]);
                y2 = B[1] + s * (-h - B[0]);
            } else {
                float s = E[2] / E[0];
                float k0 = s * B[0] - B[2];
                float q = 1.0f + s * s;
                if (!(ps2_div(k0 * k0, q) <= w->RR)) continue;
                float m = s * k0;
                float sq = ps2_sqrt(m * m - (q * (k0 + w->R)) * (k0 - w->R));
                float t1 = ps2_div(m - sq, q), t2 = ps2_div(m + sq, q);
                float hi = !(t2 <= t1) ? t2 : t1, lo = !(t2 <= t1) ? t1 : t2;
                if (!(C[0] <= hi) && !(B[0] <= hi)) continue;
                if (C[0] < lo && B[0] < lo) continue;
                w->hit = 1;
                float r = E[1] / E[0];
                y1 = B[1] + r * (t2 - B[0]);
                y2 = B[1] + r * (t1 - B[0]);
            }
            if (y2 < y1) y1 = y2;
            float ye = C[1] < B[1] ? C[1] : B[1];
            if (y1 < ye) y1 = ye;
            y1 = y1 + loc[1];
            if (y1 < best) best = y1;
        }
        w->pos[1] = best;
        return;
    }
    /* flat: barycentrics of loc in the xz projection (from v2 along e1 / e2) */
    for (int i = 0; i < 3; i++) E[i] = loc[i] - v2[i];
    E[3] = loc[3];
    PS2_VF_MIRROR(16, E);
    PS2_VF_MIRROR(17, v2);
    float inv = ps2_div(1.0f, e2[0] * e1[2] - e2[2] * e1[0]);
    float u = (E[0] * e1[2] - E[2] * e1[0]) * inv;
    float v = (E[2] * e2[0] - E[0] * e2[2]) * inv;
    int edge;
    if (u < 0.0f)
        edge = 0;
    else if (v < 0.0f)
        edge = 1;
    else if (u + v <= 1.0f)
        goto hit;
    else
        edge = 2;
    /* outside: the nearest edge's line through the circle, between its ends */
    edge_setup(edge, v1, v2, v3, loc, e1, e2, e3, E, C, B);
    if (E[0] < 0.001f && !(E[0] <= -0.001f)) {
        if (C[2] * B[2] < NEG_ZERO && C[0] < w->R && !(C[0] <= -w->R)) goto hit;
    } else if (E[2] < 0.001f && !(E[2] <= -0.001f)) {
        if (C[0] * B[0] < NEG_ZERO && C[2] < w->R && !(C[2] <= -w->R)) goto hit;
    } else if (C[0] * B[0] < NEG_ZERO && C[2] * B[2] < NEG_ZERO) {
        float s = E[2] / E[0];
        float dz = C[2] + -s * C[0];
        float dx = C[0] + -C[2] / s;
        dz = dz * dz;
        dx = dx * dx;
        if (ps2_div(dx * dz, dx + dz) < w->RR) goto hit;
    }
    /* a corner inside the circle */
    {
        for (int i = 0; i < 4; i++) {
            e1[i] = loc[i] - v1[i];
            e2[i] = loc[i] - v2[i];
            e3[i] = loc[i] - v3[i];
        }
        PS2_VF_MIRROR(16, loc);
        PS2_VF_MIRROR(17, v1);
        PS2_VF_MIRROR(18, v2);
        PS2_VF_MIRROR(19, v3);
        PS2_VF_MIRROR(20, e1);
        PS2_VF_MIRROR(21, e2);
        PS2_VF_MIRROR(22, e3);
        float rr = w->RR;
        if (!(e1[0] * e1[0] + e1[2] * e1[2] < rr) && !(e2[0] * e2[0] + e2[2] * e2[2] < rr) &&
            !(e3[0] * e3[0] + e3[2] * e3[2] < rr))
            return;
    }
hit:
    w->hit = 1;
    w->pos[1] = loc[1] + w->dist;
}

/* the line loc + t v (t in 0..L) against triangle v1 v2 v3: hit, pos = the point, dist = t; norm gets the
 * normal's squared components (original quirk, kept) */
void DF_HIT_LinePolygon(float *v1, float *v2, float *v3, float *loc, float *v, float L)
{
    PS2_FRAME(0x50);
    PS2_STACK(float[4], e1, 0x30);
    PS2_STACK(float[4], e2, 0x20);
    PS2_STACK(float[4], n, 0x0);
    PS2_STACK(float[4], d, 0x10);
    PS2_STACK(float[4], p, 0x40);
    HitOldWk *w = HIT_OWK;
    w->hit = 0;
    for (int i = 0; i < 3; i++) {
        e1[i] = v1[i] - v2[i];
        e2[i] = v3[i] - v2[i];
    }
    e1[3] = PS2_STALE_VF(19, 3);
    e2[3] = PS2_STALE_VF(20, 3);
    vu_cross(n, e1, e2);
    n[3] = v1[3];
    PS2_VF_MIRROR(16, n);
    PS2_VF_MIRROR(17, v2);
    PS2_VF_MIRROR(18, v3);
    PS2_VF_MIRROR(19, e1);
    PS2_VF_MIRROR(20, e2);
    float nv = n[2] * v[2] + (n[0] * v[0] + n[1] * v[1]);
    if (!(nv <= -1e-05f) && nv < 1e-05f) return;
    float t = (n[0] * v1[0] + n[1] * v1[1]) + n[2] * v1[2];
    float tl = (-n[0] * loc[0] - n[1] * loc[1]) - n[2] * loc[2];
    t = (t + tl) / nv;
    if (t < NEG_ZERO) return;
    if (!(t <= L)) return;
    for (int i = 0; i < 3; i++) p[i] = loc[i] + v[i] * t;
    w->dist = t;
    for (int i = 0; i < 3; i++) {
        n[i] = n[i] * n[i];
        d[i] = p[i] - v2[i];
    }
    d[3] = e1[3];
    PS2_VF_MIRROR(16, n);
    PS2_VF_MIRROR(17, p);
    PS2_VF_MIRROR(18, v2);
    PS2_VF_MIRROR(19, d);
    float den, a, b;
    if (!(n[0] <= n[1]) && !(n[0] <= n[2])) {          /* x dominant: yz projection */
        den = e2[2] * e1[1] - e2[1] * e1[2];
        a = d[2] * e1[1] - d[1] * e1[2];
        b = d[1] * e2[2] - d[2] * e2[1];
    } else if ((n[0] <= n[1]) && !(n[1] <= n[2])) {    /* y dominant: xz projection */
        den = e2[0] * e1[2] - e2[2] * e1[0];
        a = d[0] * e1[2] - d[2] * e1[0];
        b = d[2] * e2[0] - d[0] * e2[2];
    } else {                                            /* z dominant: xy projection */
        den = e2[0] * e1[1] - e2[1] * e1[0];
        a = d[0] * e1[1] - d[1] * e1[0];
        b = d[1] * e2[0] - d[0] * e2[1];
    }
    float inv = ps2_div(1.0f, den);
    a = a * inv;
    if (a < NEG_ZERO) return;
    b = b * inv;
    if (b < NEG_ZERO) return;
    if (!(a + b <= 1.0f)) return;
    w->hit = 1;
    memcpy(w->pos, p, 16);
    memcpy(w->norm, n, 16);
}

/* sphere (loc, R) against the segment v + t vec (t in 0..L): 0 = hit (pos = the end it touches), 1 = miss.
 * Original quirk kept: the far end is taken as vec + vec L instead of v + vec L. */
int DF_HIT_SphereLine_Check(float *loc, float R, float *v, float *vec, float L)
{
    PS2_FRAME(0x30);
    PS2_STACK(float[4], d0, 0x0);
    PS2_STACK(float[4], d1, 0x10);
    PS2_STACK(float[4], d2, 0x20);
    float proj = (vec[0] * loc[0] + vec[1] * loc[1]) + vec[2] * loc[2];
    float pv = (-vec[0] * v[0] - vec[1] * v[1]) - vec[2] * v[2];
    proj = proj + pv;
    float rr = R * R;
    if (proj < 0.0f) {
        for (int i = 0; i < 3; i++) d1[i] = loc[i] - v[i];
        d1[3] = loc[3];
        if (d1[2] * d1[2] + (d1[0] * d1[0] + d1[1] * d1[1]) < rr) {
            memcpy(HIT_OWK->pos, v, 16);
            return 0;
        }
    } else if (!(proj <= L)) {
        for (int i = 0; i < 3; i++) d2[i] = vec[i] * L;
        d2[3] = vec[3];
        for (int i = 0; i < 3; i++) d2[i] = vec[i] + d2[i];
        for (int i = 0; i < 3; i++) d2[i] = loc[i] - d2[i];
        d2[3] = loc[3];
        if (d2[2] * d2[2] + (d2[0] * d2[0] + d2[1] * d2[1]) < rr) {
            memcpy(HIT_OWK->pos, d2, 16);
            return 0;
        }
    }
    for (int i = 0; i < 3; i++) d0[i] = loc[i] - v[i];
    d0[3] = loc[3];
    return !((d0[2] * d0[2] + (d0[0] * d0[0] + d0[1] * d0[1])) - proj * proj < rr);
}
