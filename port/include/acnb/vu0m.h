/* VU0 macro-mode idioms for decompiled code (inline asm in the original: lqc2 / vmulax / vmadda... / sqc2).
 * Each helper performs the same IEEE operations in the same order as the VU0 sequence it replaces, so results
 * match the interpreter (runtime/vu0.c) bit for bit apart from Inf/denormal clamping. */
#pragma once
#include "acnb/ps2fp.h"

/* ACC chain over the rows of m: out = ((m[0]*v.x + m[1]*v.y) + m[2]*v.z) + m[3]*v.w  (vmulax, vmadday,
 * vmaddaz, vmaddw), all four components. out may alias v. */
static inline void vu_apply(float *out, const float (*m)[4], const float *v)
{
    float x = v[0], y = v[1], z = v[2], w = v[3], r[4];
    for (int i = 0; i < 4; i++) {
        float a = m[0][i] * x;
        a = a + m[1][i] * y;
        a = a + m[2][i] * z;
        r[i] = a + m[3][i] * w;
    }
    out[0] = r[0]; out[1] = r[1]; out[2] = r[2]; out[3] = r[3];
}

/* out[i] = vu_apply(a, b[i]) for each row (sceVu0MulMatrix(out, a, b)); out may alias a or b. */
static inline void vu_mul_matrix(float (*out)[4], const float (*a)[4], const float (*b)[4])
{
    float t[4][4], r[4][4];
    memcpy(t, a, sizeof t);
    for (int i = 0; i < 4; i++) vu_apply(r[i], (const float (*)[4])t, b[i]);
    memcpy(out, r, sizeof r);
}

/* x*x' + y*y' + z*z' as vmul.xyz + vaddy.x + vaddz.x */
static inline float vu_dot3(const float *a, const float *b)
{
    float x = a[0] * b[0], y = a[1] * b[1], z = a[2] * b[2];
    return (x + y) + z;
}

/* a x b as vopmula.xyz ACC, a, b + vopmsub.xyz d, b, a (d.w keeps whatever the destination register held) */
static inline void vu_cross(float *d, const float *a, const float *b)
{
    float x = a[1] * b[2] - b[1] * a[2];
    float y = a[2] * b[0] - b[2] * a[0];
    float z = a[0] * b[1] - b[0] * a[1];
    d[0] = x; d[1] = y; d[2] = z;
}

/* lqc2 a; lqc2 b; vsub.xyz a, a, b; sqc2: d.xyz = a - b, d.w = a.w (the register held a). d may alias a or b. */
static inline void vu_sub_xyz(float *d, const float *a, const float *b)
{
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2], w = a[3];
    d[0] = x; d[1] = y; d[2] = z; d[3] = w;
}

/* VMAX / VMINI lane: s > t ? s : t, s < t ? s : t */
static inline float vu_max(float s, float t) { return s > t ? s : t; }
static inline float vu_min(float s, float t) { return s < t ? s : t; }

/* 1 / sqrt(x²+y²+z²) as vsqrt + vaddq.x (0 + q) + vdiv vf0w: the scale factor of the VU normalise idiom. */
static inline float vu_rcp_len3(const float *v)
{
    float q = ps2_sqrt(vu_dot3(v, v));
    return ps2_div(1.0f, 0.0f + q);
}

/* VU0 normalise (vsqrt, vdiv 1 / len, vmulq.xyz into a cleared register): w = 0; d may alias v */
static inline void vu_normalize(float *d, const float *v)
{
    float q = vu_rcp_len3(v);
    float x = v[0] * q, y = v[1] * q, z = v[2] * q;
    d[0] = x; d[1] = y; d[2] = z; d[3] = 0.0f;
}
