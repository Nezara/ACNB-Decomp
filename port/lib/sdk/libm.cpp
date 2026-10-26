/* lib/sdk/libm.cpp — newlib float math of the main ELF (fdlibm sf_atan.c / ef_atan2.c built without Inf/NaN
 * handling; sinf / cosf / tanf with their kernels and argument reduction), ported from the binary with the same
 * operation order, so results are bit-identical to the interpreter.
 * Named ps2_* to stay clear of the host <math.h>; registered at the newlib addresses (runtime/bridge_sdk.c).
 *
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ==================================================== */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

#define F(bits) ps2_u2f(bits##u)

float ps2_fabsf(float x) { return ps2_u2f(ps2_f2u(x) & 0x7fffffffu); }

float ps2_atanf(float x)
{
    static const uint32_t atanhi[4] = {0x3eed6338, 0x3f490fda, 0x3f7b985d, 0x3fc90fda};
    static const uint32_t atanlo[4] = {0x31ac3769, 0x33222167, 0x33140fb4, 0x33a22168};
    static const uint32_t aT[11] = {0x3eaaaaab, 0xbe4ccccc, 0x3e124924, 0xbde38e38, 0x3dba2e6e, 0xbd9d8795,
                                    0x3d886b34, 0xbd6ef16a, 0x3d4bda59, 0xbd15a221, 0x3c8569d7};
    const float one = 1.0f, huge = F(0x7149f2c9);
    int32_t hx = (int32_t)ps2_f2u(x), ix = hx & 0x7fffffff, id;
    float w, s1, s2, z;

    if (ix > 0x507fffff) {   /* |x| >= 2^34 */
        if (hx > 0) return ps2_u2f(atanhi[3]) + ps2_u2f(atanlo[3]);
        return -ps2_u2f(atanhi[3]) - ps2_u2f(atanlo[3]);
    }
    if (ix < 0x3ee00000) {   /* |x| < 0.4375 */
        if (ix < 0x31000000 && huge + x > one) return x;   /* |x| < 2^-29 */
        id = -1;
    } else {
        x = ps2_fabsf(x);
        if (ix < 0x3f980000) {   /* |x| < 1.1875 */
            if (ix < 0x3f300000) { id = 0; x = ((x + x) - one) / (x + 2.0f); }   /* 7/16 <= |x| < 11/16 */
            else { id = 1; x = (x - one) / (x + one); }                        /* 11/16 <= |x| < 19/16 */
        } else {
            if (ix < 0x401c0000) { id = 2; x = (x - 1.5f) / (x * 1.5f + one); }   /* |x| < 2.4375 */
            else { id = 3; x = ps2_div(-1.0f, x); }
        }
    }
    z = x * x;
    w = z * z;
#define A(i) ps2_u2f(aT[i])
    s1 = z * (A(0) + w * (A(2) + w * (A(4) + w * (A(6) + w * (A(8) + w * A(10))))));
    s2 = w * (A(1) + w * (A(3) + w * (A(5) + w * (A(7) + w * A(9)))));
#undef A
    if (id < 0) return x - x * (s1 + s2);
    z = ps2_u2f(atanhi[id]) - ((x * (s1 + s2) - ps2_u2f(atanlo[id])) - x);
    return hx < 0 ? -z : z;
}

float ps2_ieee754_atan2f(float y, float x)
{
    const float pi = F(0x40490fda), pi_o_2 = F(0x3fc90fda), pi_lo = F(0x34222168);
    int32_t hx = (int32_t)ps2_f2u(x), ix = hx & 0x7fffffff;
    int32_t hy = (int32_t)ps2_f2u(y), iy = hy & 0x7fffffff;
    float z;

    if (hx == 0x3f800000) return ps2_atanf(y);   /* x = 1.0 */
    int m = ((hy >> 31) & 1) | ((hx >> 30) & 2);
    if (iy <= 0x007fffff) {   /* y = 0 */
        if (m == 2) return pi;
        if (m == 3) return -pi;
        return y;
    }
    if (ix <= 0x007fffff) return hy < 0 ? -pi_o_2 : pi_o_2;   /* x = 0 */
    int k = (iy - ix) >> 23;
    if (k > 60) z = pi_o_2;
    else if (hx < 0 && k < -60) z = 0.0f;
    else z = ps2_atanf(ps2_fabsf(ps2_div(y, x)));
    switch (m) {
    case 0: return z;
    case 1: return ps2_u2f(ps2_f2u(z) ^ 0x80000000u);
    case 2: return pi - (z - pi_lo);
    default: return (z - pi_lo) - pi;
    }
}

float ps2_atan2f(float y, float x) { return ps2_ieee754_atan2f(y, x); }

/* ---------------------------------------------------------------- sinf / cosf / tanf (s_sinf.c, s_cosf.c, s_tanf.c,
 * k_sinf.c, k_cosf.c, k_tanf.c, e_rem_pio2f.c, k_rem_pio2f.c). All float operations in the asm's order; the sin / cos
 * kernel constants are the binary's (several are one ulp below the correctly rounded fdlibm values), so is the PIo2
 * table of __kernel_rem_pio2f. Bit-exact against the interpreted original: port\tools\libm_test.ps1. */

static inline float neg(float x) { return ps2_u2f(ps2_f2u(x) ^ 0x80000000u); }   /* neg.s */

/* __kernel_sinf 0x12c0c0: sin(x + y) on [-pi/4, pi/4], iy = 0 when y is zero */
static float kernel_sinf(float x, float y, int iy)
{
    int32_t ix = (int32_t)(ps2_f2u(x) & 0x7fffffffu);
    if (ix <= 0x31ffffff && ps2_ftoi(x) == 0) return x;   /* |x| < 2^-27 */
    float z = x * x, v = z * x, r;
    r = z * F(0x2f2ec9d2);                         /* S6 */
    r = z * (r + F(0xb2d72f34));                   /* S5 */
    r = z * (r + F(0x3638ef1a));                   /* S4 */
    r = z * (r + F(0xb9500d01));                   /* S3 */
    r = r + F(0x3c088889);                         /* S2 */
    if (iy == 0) return x + v * (z * r + F(0xbe2aaaaa));   /* S1 */
    return x - (((z * (y * 0.5f - v * r)) - y) - v * F(0xbe2aaaaa));
}

/* __kernel_cosf 0x12b708: cos(x + y) on [-pi/4, pi/4] */
static float kernel_cosf(float x, float y)
{
    int32_t ix = (int32_t)(ps2_f2u(x) & 0x7fffffffu);
    if (ix <= 0x31ffffff && ps2_ftoi(x) == 0) return 1.0f;   /* |x| < 2^-27 */
    float z = x * x, r;
    r = z * F(0xad47d74e);                         /* C6 */
    r = z * (r + F(0x310f74f5));                   /* C5 */
    r = z * (r + F(0xb493f27b));                   /* C4 */
    r = z * (r + F(0x37d00d00));                   /* C3 */
    r = z * (r + F(0xbab60b60));                   /* C2 */
    r = z * (r + F(0x3d2aaaaa));                   /* C1 */
    if (ix <= 0x3e999999) return 1.0f - (z * 0.5f - (z * r - x * y));   /* |x| < 0.3 */
    float qx = ix > 0x3f480000 ? F(0x3e900000) /* 0.28125 */ : ps2_u2f((uint32_t)ix - 0x01000000u);   /* |x| / 4 */
    float hz = z * 0.5f - qx, a = 1.0f - qx;
    return a - (hz - (z * r - x * y));
}

/* __kernel_tanf 0x12c1c8: tan(x + y) (iy = 1) or -1 / tan(x + y) (iy = -1) on [-pi/4, pi/4] */
static float kernel_tanf(float x, float y, int iy)
{
    static const uint32_t T[13] = {0x3eaaaaab, 0x3e088889, 0x3d5d0dd1, 0x3cb327a3, 0x3c11371e, 0x3b6b6916, 0x3abede47,
                                   0x3a1a26c7, 0x398137b8, 0x38a3f445, 0x3895c07a, 0xb79bae5e, 0x37d95384};   /* 0x2705d8 */
#define TT(i) ps2_u2f(T[i])
    int32_t hx = (int32_t)ps2_f2u(x), ix = hx & 0x7fffffff;
    if (ix <= 0x317fffff && ps2_ftoi(x) == 0) {   /* |x| < 2^-28 */
        if ((ix | (iy + 1)) == 0) return ps2_div(1.0f, ps2_fabsf(x));
        if (iy == 1) return x;
        return ps2_div(-1.0f, x);
    }
    int big = ix > 0x3f2ca13f;   /* |x| >= 0.6744 */
    if (big) {
        if (hx < 0) { x = neg(x); y = neg(y); }
        float zz = F(0x3f490fda) - x, ww = F(0x33222167) - y;   /* pio4 - x, pio4lo - y */
        x = zz + ww;
        y = 0.0f;
    }
    float z = x * x, w = z * z, s = z * x;
    /* odd terms T1..T11 (r) and even terms T2..T12 (v); the asm evaluates them interleaved */
    float r = TT(1) + w * (TT(3) + w * (TT(5) + w * (TT(7) + w * (TT(9) + w * TT(11)))));
    float v = z * (TT(2) + w * (TT(4) + w * (TT(6) + w * (TT(8) + w * (TT(10) + w * TT(12))))));
    r = y + z * (s * (r + v) + y);
    r = r + TT(0) * s;
    w = x + r;
#undef TT
    if (big) {
        float fv = (float)iy;
        float sgn = (float)(1 - ((hx >> 30) & 2));
        float d = x - (ps2_div(w * w, w + fv) - r);
        return sgn * (fv - (d + d));
    }
    if (iy == 1) return w;
    /* -1 / (x + r) with the high halves split off (fdlibm: "compute -1.0 / (x + r) accurately") */
    float zh = ps2_u2f(ps2_f2u(w) & 0xfffff000u);
    float vv = r - (zh - x);
    float a = ps2_div(-1.0f, w);
    float t = ps2_u2f(ps2_f2u(a) & 0xfffff000u);
    float ss = t * zh + 1.0f;
    return t + a * (ss + t * vv);
}

/* __kernel_rem_pio2f 0x12b858 (prec 2 only, as called from __ieee754_rem_pio2f): Payne-Hanek reduction of
 * x = sum tx[i] * 2^(e0 - 8i) by pi/2 with the 8-bit chunks of 2/pi. Its callees scalbnf 0x118b88 / floorf 0x12c818
 * only see normal arguments with normal results here (bit tricks below = newlib's paths for those). */
static float kscalbnf(float x, int n)
{
    uint32_t u = ps2_f2u(x);
    if ((u & 0x7fffffffu) == 0) return x;
    return ps2_u2f(u + ((uint32_t)n << 23));
}
static float kfloorf(float x)
{
    int32_t i0 = (int32_t)ps2_f2u(x), j0 = ((i0 >> 23) & 0xff) - 0x7f;
    if (j0 < 0) return i0 >= 0 ? 0.0f : ((i0 & 0x7fffffff) == 0 ? x : -1.0f);   /* |x| < 1 */
    if (j0 >= 23) return x;
    uint32_t m = 0x007fffffu >> j0;
    if ((i0 & m) == 0) return x;
    if (i0 < 0) i0 += 0x00800000 >> j0;
    return ps2_u2f((uint32_t)i0 & ~m);
}
static int kernel_rem_pio2f(const float *x, float *y, int e0, int nx)
{
    static const int32_t ipio2[198] = {   /* two_over_pi 0x2701b8 (the 0x6b at 0x2704cc is its last entry) */
        0xa2, 0xf9, 0x83, 0x6e, 0x4e, 0x44, 0x15, 0x29, 0xfc, 0x27, 0x57, 0xd1, 0xf5, 0x34, 0xdd, 0xc0, 0xdb, 0x62,
        0x95, 0x99, 0x3c, 0x43, 0x90, 0x41, 0xfe, 0x51, 0x63, 0xab, 0xde, 0xbb, 0xc5, 0x61, 0xb7, 0x24, 0x6e, 0x3a,
        0x42, 0x4d, 0xd2, 0xe0, 0x06, 0x49, 0x2e, 0xea, 0x09, 0xd1, 0x92, 0x1c, 0xfe, 0x1d, 0xeb, 0x1c, 0xb1, 0x29,
        0xa7, 0x3e, 0xe8, 0x82, 0x35, 0xf5, 0x2e, 0xbb, 0x44, 0x84, 0xe9, 0x9c, 0x70, 0x26, 0xb4, 0x5f, 0x7e, 0x41,
        0x39, 0x91, 0xd6, 0x39, 0x83, 0x53, 0x39, 0xf4, 0x9c, 0x84, 0x5f, 0x8b, 0xbd, 0xf9, 0x28, 0x3b, 0x1f, 0xf8,
        0x97, 0xff, 0xde, 0x05, 0x98, 0x0f, 0xef, 0x2f, 0x11, 0x8b, 0x5a, 0x0a, 0x6d, 0x1f, 0x6d, 0x36, 0x7e, 0xcf,
        0x27, 0xcb, 0x09, 0xb7, 0x4f, 0x46, 0x3f, 0x66, 0x9e, 0x5f, 0xea, 0x2d, 0x75, 0x27, 0xba, 0xc7, 0xeb, 0xe5,
        0xf1, 0x7b, 0x3d, 0x07, 0x39, 0xf7, 0x8a, 0x52, 0x92, 0xea, 0x6b, 0xfb, 0x5f, 0xb1, 0x1f, 0x8d, 0x5d, 0x08,
        0x56, 0x03, 0x30, 0x46, 0xfc, 0x7b, 0x6b, 0xab, 0xf0, 0xcf, 0xbc, 0x20, 0x9a, 0xf4, 0x36, 0x1d, 0xa9, 0xe3,
        0x91, 0x61, 0x5e, 0xe6, 0x1b, 0x08, 0x65, 0x99, 0x85, 0x5f, 0x14, 0xa0, 0x68, 0x40, 0x8d, 0xff, 0xd8, 0x80,
        0x4d, 0x73, 0x27, 0x31, 0x06, 0x06, 0x15, 0x56, 0xca, 0x73, 0xa8, 0xc9, 0x60, 0xe2, 0x7b, 0xc0, 0x8c, 0x6b};
    static const uint32_t PIo2[11] = {0x3fc90000, 0x39f00000, 0x37d9ffff, 0x33a20000, 0x2e83ffff, 0x2b4fffff,
                                      0x27c1ffff, 0x22cfffff, 0x1fc40000, 0x1bc5ffff, 0x17440000};   /* 0x270588 */
    const float two8 = F(0x43800000), twon8 = F(0x3b800000);
    const int jk = 9, jp = 9;   /* init_jk[2] (0x270578: 4, 7, 9) */
    int32_t jz, jx, jv, carry, n, iq[20], i, j, k, m, q0, ih;
    float z, fw, f[20], fq[20], q[20];

    jx = nx - 1;
    jv = (e0 - 3) / 8;
    if (jv < 0) jv = 0;
    q0 = e0 - 8 * (jv + 1);
    j = jv - jx;
    m = jx + jk;
    for (i = 0; i <= m; i++, j++) f[i] = j < 0 ? 0.0f : (float)ipio2[j];
    for (i = 0; i <= jk; i++) {
        for (j = 0, fw = 0.0f; j <= jx; j++) fw += x[j] * f[jx + i - j];
        q[i] = fw;
    }
    jz = jk;
recompute:
    for (i = 0, j = jz, z = q[jz]; j > 0; i++, j--) {   /* distill q[] into iq[] reversingly */
        fw = (float)ps2_ftoi(twon8 * z);
        iq[i] = ps2_ftoi(z - two8 * fw);
        z = q[j - 1] + fw;
    }
    z = kscalbnf(z, q0);
    z -= 8.0f * kfloorf(z * 0.125f);   /* trim off integer >= 8 */
    n = ps2_ftoi(z);
    z -= (float)n;
    ih = 0;
    if (q0 > 0) {   /* need iq[jz-1] to determine n */
        i = iq[jz - 1] >> (8 - q0);
        n += i;
        iq[jz - 1] -= i << (8 - q0);
        ih = iq[jz - 1] >> (7 - q0);
    } else if (q0 == 0) ih = iq[jz - 1] >> 8;   /* sic (0x12ba94 sra 8; fdlibm: >> 7): always 0, iq[] < 0x100 */
    else if (z >= 0.5f) ih = 2;
    if (ih > 0) {   /* q > 0.5 */
        n += 1;
        carry = 0;
        for (i = 0; i < jz; i++) {   /* 1 - q */
            j = iq[i];
            if (carry == 0) {
                if (j != 0) { carry = 1; iq[i] = 0x100 - j; }
            } else iq[i] = 0xff - j;
        }
        if (q0 == 1) iq[jz - 1] &= 0x7f;
        else if (q0 == 2) iq[jz - 1] &= 0x3f;
        if (ih == 2) {
            z = 1.0f - z;
            if (carry != 0) z -= kscalbnf(1.0f, q0);
        }
    }
    if (z == 0.0f) {   /* recomputation needed? */
        j = 0;
        for (i = jz - 1; i >= jk; i--) j |= iq[i];
        if (j == 0) {
            for (k = 1; iq[jk - k] == 0; k++) {}
            for (i = jz + 1; i <= jz + k; i++) {
                f[jx + i] = (float)ipio2[jv + i];
                for (j = 0, fw = 0.0f; j <= jx; j++) fw += x[j] * f[jx + i - j];
                q[i] = fw;
            }
            jz += k;
            goto recompute;
        }
    }
    if (z == 0.0f) {   /* chop off zero terms */
        jz -= 1;
        q0 -= 8;
        while (iq[jz] == 0) { jz--; q0 -= 8; }
    } else {   /* break z into 8-bit chunks if necessary */
        z = kscalbnf(z, -q0);
        if (z >= two8) {
            fw = (float)ps2_ftoi(twon8 * z);
            iq[jz] = ps2_ftoi(z - two8 * fw);
            jz += 1;
            q0 += 8;
            iq[jz] = ps2_ftoi(fw);
        } else iq[jz] = ps2_ftoi(z);
    }
    fw = kscalbnf(1.0f, q0);
    for (i = jz; i >= 0; i--) {
        q[i] = fw * (float)iq[i];
        fw *= twon8;
    }
    for (i = jz; i >= 0; i--) {   /* PIo2[0..jp] * q[jz..0] */
        for (fw = 0.0f, k = 0; k <= jp && k <= jz - i; k++) fw += ps2_u2f(PIo2[k]) * q[i + k];
        fq[jz - i] = fw;
    }
    fw = 0.0f;
    for (i = jz; i >= 0; i--) fw += fq[i];
    y[0] = ih == 0 ? fw : neg(fw);
    fw = fq[0] - fw;
    for (i = 1; i <= jz; i++) fw += fq[i];
    y[1] = ih == 0 ? fw : neg(fw);
    return n & 7;
}

/* __ieee754_rem_pio2f 0x12b358: y[0] + y[1] = x - n * pi/2, returns n */
static int ieee754_rem_pio2f(float x, float *y)
{
    static const uint32_t npio2_hw[31] = {   /* 0x2704d0, n = 1..31 */
        0x3fc90f00, 0x40490f00, 0x4096cb00, 0x40c90f00, 0x40fb5300, 0x4116cb00, 0x412fed00, 0x41490f00,
        0x41623100, 0x417b5300, 0x418a3a00, 0x4196cb00, 0x41a35c00, 0x41afed00, 0x41bc7e00, 0x41c90f00,
        0x41d5a000, 0x41e23100, 0x41eec200, 0x41fb5300, 0x4203f200, 0x420a3a00, 0x42108300, 0x4216cb00,
        0x421d1400, 0x42235c00, 0x4229a500, 0x422fed00, 0x42363600, 0x423c7e00, 0x4242c700};
    const float pio2_1 = F(0x3fc90f80), pio2_1t = F(0x37354442), pio2_2 = F(0x373543ff), pio2_2t = F(0x2e85a308),
                pio2_3 = F(0x2e85a300), pio2_3t = F(0x248d3131);
    int32_t hx = (int32_t)ps2_f2u(x), ix = hx & 0x7fffffff;
    float z, t;

    if (ix <= 0x3f490fd8) {   /* |x| <= pi/4 */
        y[0] = x;
        y[1] = 0.0f;
        return 0;
    }
    if (ix <= 0x4016cbe3) {   /* |x| < 3pi/4 */
        if (hx > 0) {
            z = x - pio2_1;
            if ((ix & 0xfffffff0) != 0x3fc90fd0) { t = pio2_1t; y[0] = z - t; }
            else { z = z - pio2_2; t = pio2_2t; y[0] = z - t; }   /* near pi/2: 33+53 bit pi */
            y[1] = (z - y[0]) - t;
            return 1;
        }
        z = x + pio2_1;
        if ((ix & 0xfffffff0) != 0x3fc90fd0) { t = pio2_1t; y[0] = z + t; }
        else { z = z + pio2_2; t = pio2_2t; y[0] = z + t; }
        y[1] = (z - y[0]) + t;
        return -1;
    }
    if (ix <= 0x43490f80) {   /* |x| ~<= 2^7 * pi/2: medium */
        t = ps2_fabsf(x);
        int n = ps2_ftoi(t * F(0x3f22f984) + 0.5f);   /* invpio2 */
        float fn = (float)n;
        float r = t - fn * pio2_1, w = fn * pio2_1t;
        if (n < 32 && (uint32_t)(ix & 0xffffff00) != npio2_hw[n - 1]) {
            y[0] = r - w;   /* quick check no cancellation */
        } else {
            int j = ix >> 23;
            y[0] = r - w;
            int i = j - (int)((ps2_f2u(y[0]) >> 23) & 0xff);
            if (i > 8) {   /* 2nd iteration */
                t = r;
                w = fn * pio2_2;
                r = t - w;
                w = fn * pio2_2t - ((t - r) - w);
                y[0] = r - w;
                i = j - (int)((ps2_f2u(y[0]) >> 23) & 0xff);
                if (i > 25) {   /* 3rd iteration */
                    t = r;
                    w = fn * pio2_3;
                    r = t - w;
                    w = fn * pio2_3t - ((t - r) - w);
                    y[0] = r - w;
                }
            }
        }
        y[1] = (r - y[0]) - w;
        if (hx < 0) { y[0] = neg(y[0]); y[1] = neg(y[1]); return -n; }
        return n;
    }
    /* large |x| (also the Inf / NaN bit patterns, ordinary huge values on the PS2): three 8-bit chunks */
    float tx[3];
    int e0 = (ix >> 23) - 134;
    z = ps2_u2f((uint32_t)(ix - (e0 << 23)));
    for (int i = 0; i < 2; i++) {
        tx[i] = (float)ps2_ftoi(z);
        z = (z - tx[i]) * F(0x43800000);   /* two8 */
    }
    tx[2] = z;
    int nx = 3;
    while (tx[nx - 1] == 0.0f) nx--;
    int n = kernel_rem_pio2f(tx, y, e0, nx);
    if (hx < 0) { y[0] = neg(y[0]); y[1] = neg(y[1]); return -n; }
    return n;
}

/* sinf 0x12c8f0 */
float ps2_native_sinf(float x)
{
    float y[2];
    int32_t ix = (int32_t)(ps2_f2u(x) & 0x7fffffffu);
    if (ix <= 0x3f490fd8) return kernel_sinf(x, 0.0f, 0);
    switch (ieee754_rem_pio2f(x, y) & 3) {
    case 0: return kernel_sinf(y[0], y[1], 1);
    case 1: return kernel_cosf(y[0], y[1]);
    case 2: return neg(kernel_sinf(y[0], y[1], 1));
    default: return neg(kernel_cosf(y[0], y[1]));
    }
}

/* cosf 0x12c718 */
float ps2_native_cosf(float x)
{
    float y[2];
    int32_t ix = (int32_t)(ps2_f2u(x) & 0x7fffffffu);
    if (ix <= 0x3f490fd8) return kernel_cosf(x, 0.0f);
    switch (ieee754_rem_pio2f(x, y) & 3) {
    case 0: return kernel_cosf(y[0], y[1]);
    case 1: return neg(kernel_sinf(y[0], y[1], 1));
    case 2: return neg(kernel_cosf(y[0], y[1]));
    default: return kernel_sinf(y[0], y[1], 1);
    }
}

/* tanf 0x12c9d8 */
float ps2_native_tanf(float x)
{
    float y[2];
    int32_t ix = (int32_t)(ps2_f2u(x) & 0x7fffffffu);
    if (ix <= 0x3f490fda) return kernel_tanf(x, 0.0f, 1);
    int n = ieee754_rem_pio2f(x, y);
    return kernel_tanf(y[0], y[1], 1 - ((n & 1) << 1));
}

#ifdef ACNB_SOURCEPORT
/* the hybrid's ps2_sinf / cosf / tanf live in runtime/bridge_sdk.c (bridge_call to the registered SDK address, so
 * -Check sees the calls; switch them to direct calls once checked) */
float ps2_sinf(float x) { return ps2_native_sinf(x); }
float ps2_cosf(float x) { return ps2_native_cosf(x); }
float ps2_tanf(float x) { return ps2_native_tanf(x); }
#endif

/* ======================================================================================================== double libm
 * pow (0x12ca50, a bare tail call of __ieee754_pow 0x129f08), __ieee754_sqrt (0x12ae58, pow's y == 0.5 tail jump),
 * fabs (0x12c450), scalbn (0x118908), copysign (0x118838): fdlibm 5.3 e_pow.c / e_sqrt.c / s_scalbn.c / s_copysign.c,
 * built with SOFT double - every double operation is a libgcc fp-bit call (round toward zero, no denormals), so they
 * are ported as raw-bit code over ps2_adddf3 / subdf3 / muldf3 / divdf3 / cmpdf2 / floatsidf, operands in the asm's
 * order. Constants are the ELF words, not textbook fdlibm values (MWCC's literal conversion chopped several by one ulp,
 * and folded huge*huge / tiny*tiny to DBL_MAX / 0). Checked by port\tools\softdouble_test.ps1 (interpreted vs native). */
typedef unsigned long long d64;
static inline int32_t dhi(d64 x) { return (int32_t)(uint32_t)(x >> 32); }
static inline d64 dset_hi(d64 x, uint32_t hi) { return (x & 0xffffffffull) | (d64)hi << 32; }
static inline d64 dzero_lo(d64 x) { return x & 0xffffffff00000000ull; }   /* SET_LOW_WORD(x, 0) */
#define DADD ps2_adddf3
#define DSUB ps2_subdf3
#define DMUL ps2_muldf3
#define DDIV ps2_divdf3

static const d64 D_ONE = 0x3ff0000000000000ull, D_NEG_ONE = 0xbff0000000000000ull, D_HALF = 0x3fe0000000000000ull,
                 D_QUARTER = 0x3fd0000000000000ull, D_TWO = 0x4000000000000000ull, D_THREE = 0x4008000000000000ull,
                 D_TWO53 = 0x4340000000000000ull, D_TWO54 = 0x4350000000000000ull, D_TWOM54 = 0x3c90000000000000ull;

/* 0x26ffe8: bp[2], dp_h[2], dp_l[2] (indexed by k) */
extern "C" const d64 ps2_pow_pool1[6] = {0x3ff0000000000000ull, 0x3ff8000000000000ull, 0, 0x3fe2b8033fffffffull,
                                         0, 0x3e4cfdeb43cfd005ull};
/* 0x2700f0..0x2701b0, in the order __ieee754_pow loads them */
extern "C" const d64 ps2_pow_pool2[25] = {
    0x7fefffffffffffffull, /* 0f0 huge*huge folded (DBL_MAX) */ 0x7e37e43c8800759bull, /* 0f8 huge */
    0x01a56e1fc2f8f358ull, /* 100 tiny */ 0x3fd5555555555555ull, /* 108 1/3 */
    0x3ff715475fffffffull, /* 110 ivln2_h */ 0x3e54ae0bf85ddf44ull, /* 118 ivln2_l */
    0x3ff71547652b82fdull, /* 120 ivln2 */ 0x3fca7e284a454eefull, /* 128 L6 */
    0x3fcd864a93c9db64ull, /* 130 L5 */ 0x3fd17460a91d4100ull, /* 138 L4 */
    0x3fd55555518f264cull, /* 140 L3 */ 0x3fdb6db6db6fabffull, /* 148 L2 */
    0x3fe3333333333302ull, /* 150 L1 */ 0x3feec709dfffffffull, /* 158 cp_h */
    0xbe3e2fe0145b01f4ull, /* 160 cp_l */ 0x3feec709dc3a03fdull, /* 168 cp */
    0x3c971547652b82feull, /* 170 ovt */ 0x3fe62e4300000000ull, /* 178 lg2_h */
    0x3fe62e42fefa39efull, /* 180 lg2 */ 0xbe205c610ca86c38ull, /* 188 lg2_l */
    0x3e66376972bea4d0ull, /* 190 P5 */ 0xbebbbd41c5d26bf1ull, /* 198 P4 */
    0x3f11566aaf25de2bull, /* 1a0 P3 */ 0xbf66c16c16bebd92ull, /* 1a8 P2 */
    0x3fc555555555553eull, /* 1b0 P1 */
};
/* 0x26f140: scalbn's tiny, huge */
extern "C" const d64 ps2_scalbn_pool[2] = {0x01a56e1fc2f8f358ull, 0x7e37e43c8800759bull};
#define PK(a) ps2_pow_pool2[((a) - 0x2700f0) / 8]
static const d64 *const pow_bp = ps2_pow_pool1, *const pow_dp_h = ps2_pow_pool1 + 2, *const pow_dp_l = ps2_pow_pool1 + 4;

d64 ps2_fabs(d64 x) { return x & 0x7fffffffffffffffull; }
d64 ps2_copysign(d64 x, d64 y) { return (x & 0x7fffffffffffffffull) | (y & 0x8000000000000000ull); }

d64 ps2_scalbn(d64 x, int n)
{
    const d64 tiny = ps2_scalbn_pool[0], huge = ps2_scalbn_pool[1];
    int32_t hx = dhi(x), k = (hx & 0x7ff00000) >> 20;
    uint32_t lx = (uint32_t)x;
    if (k == 0) {                                       /* 0 or subnormal x */
        if ((lx | (uint32_t)(hx & 0x7fffffff)) == 0) return x;
        x = DMUL(x, D_TWO54);
        hx = dhi(x);
        k = ((hx & 0x7ff00000) >> 20) - 54;
        if (n < -50000) return DMUL(x, tiny);
    }
    if (k == 0x7ff) return DADD(x, x);                  /* NaN or Inf */
    k = (int32_t)((uint32_t)k + (uint32_t)n);
    if (k > 0x7fe) return DMUL(ps2_copysign(huge, x), huge);
    if (k > 0) return dset_hi(x, ((uint32_t)hx & 0x800fffffu) | (uint32_t)k << 20);
    if (k <= -54) {
        if (n > 50000) return DMUL(ps2_copysign(huge, x), huge);
        return DMUL(ps2_copysign(tiny, x), tiny);
    }
    k += 54;
    x = dset_hi(x, ((uint32_t)hx & 0x800fffffu) | (uint32_t)k << 20);
    return DMUL(x, D_TWOM54);
}

d64 ps2_ieee754_sqrt(d64 x)
{
    const uint32_t sign = 0x80000000u;
    int32_t ix0 = dhi(x), s0, q, m, t, i;
    uint32_t r, t1, s1, ix1 = (uint32_t)x, q1;

    if ((ix0 & 0x7ff00000) == 0x7ff00000) return DADD(DMUL(x, x), x);   /* sqrt(NaN) = NaN, +Inf, -Inf -> NaN */
    if (ix0 <= 0) {
        if ((((uint32_t)ix0 & ~sign) | ix1) == 0) return x;               /* +-0 */
        if (ix0 < 0) { d64 d = DSUB(x, x); return DDIV(d, d); }
    }
    m = ix0 >> 20;
    if (m == 0) {                                                         /* subnormal */
        while (ix0 == 0) { m -= 21; ix0 |= (int32_t)(ix1 >> 11); ix1 <<= 21; }
        for (i = 0; (ix0 & 0x00100000) == 0; i++) ix0 = (int32_t)((uint32_t)ix0 << 1);
        m -= i - 1;
        ix0 |= (int32_t)(ix1 >> ((32 - i) & 31));                         /* srlv: i == 0 ors ix1 itself */
        ix1 <<= i;
    }
    m -= 1023;
    ix0 = (ix0 & 0x000fffff) | 0x00100000;
    if (m & 1) { ix0 += ix0 + (int32_t)((ix1 & sign) >> 31); ix1 += ix1; }
    m >>= 1;
    ix0 += ix0 + (int32_t)((ix1 & sign) >> 31); ix1 += ix1;
    q = s0 = 0; q1 = s1 = 0;
    r = 0x00200000;
    while (r != 0) {
        t = s0 + (int32_t)r;
        if (t <= ix0) { s0 = t + (int32_t)r; ix0 -= t; q += (int32_t)r; }
        ix0 += ix0 + (int32_t)((ix1 & sign) >> 31); ix1 += ix1;
        r >>= 1;
    }
    r = sign;
    while (r != 0) {
        t1 = s1 + r; t = s0;
        if ((t < ix0) || ((t == ix0) && (t1 <= ix1))) {
            s1 = t1 + r;
            if (((t1 & sign) == sign) && (s1 & sign) == 0) s0 += 1;
            ix0 -= t;
            if (ix1 < t1) ix0 -= 1;
            ix1 -= t1;
            q1 += r;
        }
        ix0 += ix0 + (int32_t)((ix1 & sign) >> 31); ix1 += ix1;
        r >>= 1;
    }
    if ((ix0 | (int32_t)ix1) != 0) {
        /* z = one - tiny / one + tiny: MWCC folded both to 1.0, the compares stay (0x12b0a4 / 0x12b0d0) */
        if (ps2_cmpdf2(D_ONE, D_ONE) >= 0) {
            if (q1 == 0xffffffffu) { q1 = 0; q += 1; }
            else if (ps2_cmpdf2(D_ONE, D_ONE) > 0) { if (q1 == 0xfffffffeu) q += 1; q1 += 2; }
            else q1 += (q1 & 1);
        }
    }
    ix0 = (q >> 1) + 0x3fe00000;
    ix1 = q1 >> 1;
    if ((q & 1) == 1) ix1 |= sign;
    ix0 = (int32_t)((uint32_t)ix0 + ((uint32_t)m << 20));
    return (d64)(uint32_t)ix0 << 32 | ix1;
}

d64 ps2_ieee754_pow(d64 x, d64 y)
{
    d64 z, ax, z_h, z_l, p_h, p_l, y1, t1, t2, r, s, t, u, v, w;
    int32_t i, j, k, yisint, n, hx, hy, ix, iy;
    uint32_t lx = (uint32_t)x, ly = (uint32_t)y, nsign;

    hx = dhi(x); hy = dhi(y);
    ix = hx & 0x7fffffff; iy = hy & 0x7fffffff;
    if (((uint32_t)iy | ly) == 0) return D_ONE;                           /* y == +-0 */
    if (ix > 0x7ff00000 || (ix == 0x7ff00000 && lx != 0) || iy > 0x7ff00000 || (iy == 0x7ff00000 && ly != 0))
        return DADD(x, y);                                                /* NaN */

    /* yisint = 0: y not an integer, 1: odd, 2: even (only for x < 0) */
    yisint = 0;
    if (hx < 0) {
        if (iy >= 0x43400000) yisint = 2;
        else if (iy >= 0x3ff00000) {
            k = (iy >> 20) - 0x3ff;
            if (k > 20) {
                uint32_t jj = ly >> ((52 - k) & 31);
                if ((jj << ((52 - k) & 31)) == ly) yisint = 2 - (int32_t)(jj & 1);
            } else if (ly == 0) {
                j = iy >> (20 - k);
                if ((j << (20 - k)) == iy) yisint = 2 - (j & 1);
            }
        }
    }
    if (ly == 0) {
        if (iy == 0x7ff00000) {                                           /* y = +-Inf */
            if ((((uint32_t)ix - 0x3ff00000u) | lx) == 0) return DSUB(y, y);
            else if (ix >= 0x3ff00000) return hy >= 0 ? y : 0;
            else return hy < 0 ? DSUB(0, y) : 0;
        }
        if (iy == 0x3ff00000) return hy < 0 ? DDIV(D_ONE, x) : x;        /* y = +-1 */
        if (hy == 0x40000000) return DMUL(x, x);                          /* y = 2 */
        if (hy == 0x3fe00000 && hx >= 0) return ps2_ieee754_sqrt(x);      /* y = 0.5 (0x12a190 j __ieee754_sqrt) */
    }

    ax = ps2_fabs(x);
    if (lx == 0) {
        if (ix == 0x7ff00000 || ix == 0 || ix == 0x3ff00000) {            /* x = +-0, +-Inf, +-1 */
            z = ax;
            if (hy < 0) z = DDIV(D_ONE, z);
            if (hx < 0) {
                if ((((uint32_t)ix - 0x3ff00000u) | (uint32_t)yisint) == 0) { d64 d = DSUB(z, z); z = DDIV(d, d); }
                else if (yisint == 1) z = DSUB(0, z);
            }
            return z;
        }
    }
    nsign = (uint32_t)hx >> 31;                                           /* the asm keeps hx>>31 (sp+0x18); n = it - 1 */
    if (((nsign - 1) | (uint32_t)yisint) == 0) { d64 d = DSUB(x, x); return DDIV(d, d); }   /* (x<0)**(non-int) */

    if (iy > 0x41e00000) {                                                /* |y| > 2**31 */
        if (iy > 0x43f00000) {                                            /* |y| > 2**64: must o/uflow */
            if (ix <= 0x3fefffff) return hy < 0 ? PK(0x2700f0) : 0;
            if (ix >= 0x3ff00000) return hy > 0 ? PK(0x2700f0) : 0;
        }
        if (ix < 0x3fefffff) return hy < 0 ? PK(0x2700f0) : 0;
        if (ix > 0x3ff00000) return hy > 0 ? PK(0x2700f0) : 0;
        /* |1-x| <= 2**-20: log(x) by x-x^2/2+x^3/3-x^4/4. The original subtracts from x, not ax (0x12a338 a0 = s4) */
        t = DSUB(x, D_ONE);
        w = DMUL(DMUL(t, t), DSUB(D_HALF, DMUL(t, DSUB(PK(0x270108), DMUL(t, D_QUARTER)))));
        u = DMUL(t, PK(0x270110));                                        /* ivln2_h */
        v = DSUB(DMUL(t, PK(0x270118)), DMUL(w, PK(0x270120)));           /* t*ivln2_l - w*ivln2 */
        t1 = dzero_lo(DADD(u, v));
        t2 = DSUB(v, DSUB(t1, u));
    } else {
        d64 ss, s2, s_h, s_l, t_h, t_l;
        n = 0;
        if (ix < 0x00100000) { ax = DMUL(ax, D_TWO53); n -= 53; ix = dhi(ax); }   /* subnormal x */
        n += (ix >> 20) - 0x3ff;
        j = ix & 0x000fffff;
        ix = j | 0x3ff00000;                                              /* normalize ix */
        if (j <= 0x3988e) k = 0;                                          /* |x| < sqrt(3/2) */
        else if (j < 0xbb67a) k = 1;                                      /* |x| < sqrt(3) */
        else { k = 0; n += 1; ix -= 0x00100000; }
        ax = dset_hi(ax, (uint32_t)ix);

        /* ss = s_h + s_l = (x-1)/(x+1) or (x-1.5)/(x+1.5) */
        u = DSUB(ax, pow_bp[k]);
        v = DDIV(D_ONE, DADD(ax, pow_bp[k]));
        ss = DMUL(u, v);
        s_h = dzero_lo(ss);
        t_h = (d64)(uint32_t)(((ix >> 1) | 0x20000000) + 0x00080000 + (k << 18)) << 32;   /* t_h = ax + bp[k] high */
        t_l = DSUB(ax, DSUB(t_h, pow_bp[k]));
        s_l = DMUL(v, DSUB(DSUB(u, DMUL(s_h, t_h)), DMUL(s_h, t_l)));
        /* log(ax) */
        s2 = DMUL(ss, ss);
        r = DMUL(s2, PK(0x270128));                                       /* L6 */
        r = DMUL(s2, DADD(r, PK(0x270130)));
        r = DMUL(s2, DADD(r, PK(0x270138)));
        r = DMUL(s2, DADD(r, PK(0x270140)));
        r = DMUL(s2, DADD(r, PK(0x270148)));
        r = DMUL(DMUL(s2, s2), DADD(r, PK(0x270150)));                    /* s2*s2*(L1+...) */
        r = DADD(r, DMUL(s_l, DADD(s_h, ss)));
        s2 = DMUL(s_h, s_h);
        t_h = dzero_lo(DADD(DADD(s2, D_THREE), r));
        t_l = DSUB(r, DSUB(DSUB(t_h, D_THREE), s2));
        /* u+v = ss*(1+...) */
        u = DMUL(s_h, t_h);
        v = DADD(DMUL(s_l, t_h), DMUL(t_l, ss));
        /* 2/(3log2)*(ss+...) */
        p_h = dzero_lo(DADD(u, v));
        p_l = DSUB(v, DSUB(p_h, u));
        z_h = DMUL(p_h, PK(0x270158));                                    /* cp_h */
        z_l = DADD(DADD(DMUL(p_h, PK(0x270160)), DMUL(p_l, PK(0x270168))), pow_dp_l[k]);   /* cp_l, cp */
        /* log2(ax) = (ss+..)*2/(3*log2) = n + dp_h + z_h + z_l */
        t = ps2_floatsidf(n);
        t1 = dzero_lo(DADD(DADD(DADD(z_h, z_l), pow_dp_h[k]), t));
        t2 = DSUB(z_l, DSUB(DSUB(DSUB(t1, t), pow_dp_h[k]), z_h));
    }

    s = D_ONE;                                                            /* sign of the result */
    if (((nsign - 1) | (uint32_t)(yisint - 1)) == 0) s = D_NEG_ONE;       /* (-ve)**(odd int) */

    /* split y into y1+y2, compute (y1+y2)*(t1+t2) */
    y1 = dzero_lo(y);
    p_l = DADD(DMUL(DSUB(y, y1), t1), DMUL(y, t2));
    p_h = DMUL(y1, t1);
    z = DADD(p_l, p_h);
    j = dhi(z); i = (int32_t)(uint32_t)z;
    if (j >= 0x40900000) {                                                /* z >= 1024 */
        if ((((uint32_t)j - 0x40900000u) | (uint32_t)i) != 0) return DMUL(DMUL(s, PK(0x2700f8)), PK(0x2700f8));
        if (ps2_cmpdf2(DADD(p_l, PK(0x270170)), DSUB(z, p_h)) > 0) return DMUL(DMUL(s, PK(0x2700f8)), PK(0x2700f8));
    } else if ((j & 0x7fffffff) >= 0x4090cc00) {                          /* z <= -1075 */
        if ((((uint32_t)j - 0xc090cc00u) | (uint32_t)i) != 0) return DMUL(DMUL(s, PK(0x270100)), PK(0x270100));
        if (ps2_cmpdf2(p_l, DSUB(z, p_h)) <= 0) return DMUL(DMUL(s, PK(0x270100)), PK(0x270100));
    }
    /* 2**(p_h+p_l) */
    i = j & 0x7fffffff;
    k = (i >> 20) - 0x3ff;
    n = 0;
    if (i > 0x3fe00000) {                                                 /* |z| > 0.5: n = [z+0.5] */
        n = (int32_t)((uint32_t)j + (0x00100000u >> ((k + 1) & 31)));
        k = ((n & 0x7fffffff) >> 20) - 0x3ff;
        t = (d64)((uint32_t)n & ~(0x000fffffu >> (k & 31))) << 32;
        n = ((n & 0x000fffff) | 0x00100000) >> ((20 - k) & 31);
        if (j < 0) n = -n;
        p_h = DSUB(p_h, t);
    }
    t = dzero_lo(DADD(p_l, p_h));
    u = DMUL(t, PK(0x270178));                                            /* lg2_h */
    v = DADD(DMUL(DSUB(p_l, DSUB(t, p_h)), PK(0x270180)), DMUL(t, PK(0x270188)));   /* lg2, lg2_l */
    z = DADD(u, v);
    w = DSUB(v, DSUB(z, u));
    t = DMUL(z, z);
    t1 = DMUL(t, PK(0x270190));                                           /* P5 */
    t1 = DMUL(t, DADD(t1, PK(0x270198)));
    t1 = DMUL(t, DADD(t1, PK(0x2701a0)));
    t1 = DMUL(t, DADD(t1, PK(0x2701a8)));
    t1 = DSUB(z, DMUL(t, DADD(t1, PK(0x2701b0))));                        /* z - t*(P1+...) */
    r = DSUB(DDIV(DMUL(z, t1), DSUB(t1, D_TWO)), DADD(w, DMUL(z, w)));
    z = DSUB(D_ONE, DSUB(r, z));
    j = (int32_t)((uint32_t)dhi(z) + ((uint32_t)n << 20));
    if ((j >> 20) <= 0) z = ps2_scalbn(z, n);                             /* subnormal output */
    else z = dset_hi(z, (uint32_t)j);
    return DMUL(s, z);
}

d64 ps2_pow(d64 x, d64 y) { return ps2_ieee754_pow(x, y); }   /* 0x12ca50: no errno / _LIB_VERSION wrapper */
#undef DADD
#undef DSUB
#undef DMUL
#undef DDIV
#undef PK
