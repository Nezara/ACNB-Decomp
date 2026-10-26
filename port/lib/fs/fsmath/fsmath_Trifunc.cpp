/* lib/fs/fsmath/fsmath_Trifunc.cpp - single-precision asin / acos / sin / cos (whole file).
 * AsinVU0 / AcosVU0 are fdlibm's e_asinf / e_acosf with the rational approximation p(t) / q(t) evaluated on VU0
 * (inline asm in the original): each power is built by repeated multiplies of the coefficient (pS[i] * t * t ...),
 * the terms are summed lowest power first, and 1 / q comes from VDIV and is multiplied in (p * (1 / q), not p / q).
 * SinVU0 / CosVU0 reduce the angle to -PI/2..PI/2 (trunc(x / 2PI) turns, one more correction step, then a reflection
 * about +-PI/2) and evaluate a 4-term odd / even polynomial on VU0 the same way.
 * The VU stages only use finite, normal values, so plain IEEE ops in the same order match the VU's clamping. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"

static const float FSM_PIO2_HI = 0x1.921fb4p+0f;    /* 0x3fc90fda */
static const float FSM_PIO2_LO = 0x1.4442d0p-24f;   /* 0x33a22168 */
static const float FSM_PIO4_HI = 0x1.921fb6p-1f;    /* 0x3f490fdb */
static const float FSM_HUGE = 0x1.93e594p+99f;      /* 0x7149f2ca (1e30) */
static const float FSM_PI = 0x1.921fb6p+1f;         /* 0x40490fdb */
static const float FSM_PI2 = 0x1.921fb6p+2f;        /* 0x40c90fdb */
static const float FSM_HALF_PI = 0x1.921fb6p+0f;    /* 0x3fc90fdb */
static const float FSM_PI3_2 = 0x1.2d97c8p+2f;      /* 0x4096cbe4 */
static const float FSM_INV_PI2 = 0x1.45f306p-3f;    /* 0x3e22f983 */

/* The VU0 block shared by AsinVU0 / AcosVU0: returns p = pS0 t + pS1 t^2 + ... + pS5 t^6 and *rq = 1 / q,
 * q = 1 + qS0 t + qS1 t^2 + qS2 t^3 + qS3 t^4. (p's sum starts from vf4 * vf0.x = +-0, which does not change the
 * first term: t is never 0 here.) */
static inline float fsm_rational(const unsigned int *pS, const unsigned int *qS, float t, float *rq)
{
    float a[6], b[4];
    for (int i = 0; i < 6; i++) {
        a[i] = ps2_u2f(pS[i]) * t;
        for (int k = 0; k < i; k++) a[i] = a[i] * t;
    }
    for (int i = 0; i < 4; i++) {
        b[i] = ps2_u2f(qS[i]) * t;
        for (int k = 0; k < i; k++) b[i] = b[i] * t;
    }
    float p = a[0];
    for (int i = 1; i < 6; i++) p = p + a[i];
    float q = 1.0f + b[0];
    for (int i = 1; i < 4; i++) q = q + b[i];
    *rq = ps2_div(1.0f, q);
    return p;
}

/* the inlined turn reduction of SinVU0 / CosVU0 (cvt.w.s truncates) */
static inline float fsm_turns(float x)
{
    return x - FSM_PI2 * (float)ps2_ftoi(FSM_INV_PI2 * x);
}

float fsmath_AsinVU0(float x)
{
    static const unsigned int pS[8] = {0x3e2aaaab, 0xbea6b090, 0x3e4e0aa8, 0xbd241146,
                                       0x3a4f7f04, 0x3811ef08, 0, 0};   /* 0x002716b0 */
    static const unsigned int qS[4] = {0xc019d139, 0x4001572d, 0xbf303361, 0x3d9dc62e};   /* 0x002716d0 */
    int hx = (int)ps2_f2u(x);
    int ix = hx & 0x7fffffff;
    float t, rq;

    if (ix == 0x3f800000)   /* asin(+-1) = +-pi/2 (mula + madd: both products rounded, then added) */
        return FSM_PIO2_HI * x + FSM_PIO2_LO * x;
    if (ix > 0x3f800000)    /* |x| > 1: (x - x) / (x - x) */
        return ps2_div(x - x, x - x);
    if (ix < 0x3f000000) {  /* |x| < 0.5 */
        if (ix >= 0x32000000) {
            t = x * x;
            float p = fsm_rational(pS, qS, t, &rq);
            float w = p * rq;
            return x + x * w;
        }
        if (!(FSM_HUGE + x <= 1.0f)) return x;   /* |x| < 2^-27 */
        /* (never: the original falls through to the |x| >= 0.5 code) */
    }

    /* 1 > |x| >= 0.5 */
    float w = 1.0f - ps2_u2f((unsigned int)ix);
    t = 0.5f * w;
    float s = ps2_sqrt(t);
    float p = fsm_rational(pS, qS, t, &rq);
    if (ix >= 0x3f79999a) {   /* |x| > 0.975 */
        w = p * rq;
        t = FSM_PIO2_HI - (2.0f * (s + s * w) - FSM_PIO2_LO);
    } else {
        float r = p * rq;
        w = ps2_u2f(ps2_f2u(s) & 0xfffff000u);
        float c = (t - w * w) / (s + w);
        p = 2.0f * s * r - (FSM_PIO2_LO - 2.0f * c);
        float q = FSM_PIO4_HI - 2.0f * w;
        t = FSM_PIO4_HI - (p - q);
    }
    return hx > 0 ? t : -t;
}

float fsmath_AcosVU0(float x)
{
    static const unsigned int pS[8] = {0x3e2aaaab, 0xbea6b090, 0x3e4e0aa8, 0xbd241146,
                                       0x3a4f7f04, 0x3811ef08, 0, 0};   /* 0x00271680 */
    static const unsigned int qS[4] = {0xc019d139, 0x4001572d, 0xbf303361, 0x3d9dc62e};   /* 0x002716a0 */
    int hx = (int)ps2_f2u(x);
    int ix = hx & 0x7fffffff;
    float rq;

    if (ix == 0x3f800000) {   /* |x| == 1 */
        if (hx > 0) return 0.0f;
        return 0x1.4442d0p-23f;   /* 0x34222168 = 2 * pio2_lo: acos(-1) without the pi (original quirk) */
    }
    if (ix > 0x3f800000)      /* |x| > 1: (x - x) / (x - x) */
        return ps2_div(x - x, x - x);
    if (ix < 0x3f000000) {    /* |x| < 0.5 */
        if (ix <= 0x23000000) return 0x1.921fb6p+0f;   /* pio2_hi + pio2_lo (0x3fc90fdb) */
        float z = x * x;
        float p = fsm_rational(pS, qS, z, &rq);
        float r = p * rq;
        return FSM_PIO2_HI - (x - (FSM_PIO2_LO - x * r));
    }
    if (hx < 0) {             /* x < -0.5 */
        float z = (1.0f + x) * 0.5f;
        float s = ps2_sqrt(z);   /* VSQRT */
        float p = fsm_rational(pS, qS, z, &rq);
        float r = p * rq;
        float w = r * s - FSM_PIO2_LO;
        return FSM_PI - 2.0f * (s + w);
    }
    /* x > 0.5 */
    float z = (1.0f - x) * 0.5f;
    float s = ps2_sqrt(z);       /* sqrt.s */
    float df = ps2_u2f(ps2_f2u(s) & 0xfffff000u);
    float c = (z - df * df) / (s + df);
    float p = fsm_rational(pS, qS, z, &rq);
    float r = p * rq;
    float w = r * s + c;
    return 2.0f * (df + w);
}

float fsmath_SinVU0(float x)
{
    static const unsigned int r[4] = {0x362e9c5a, 0xb94fb222, 0x3c08873d, 0xbe2aaaa3};   /* 0x00271670 */

    if (!(x <= FSM_HALF_PI)) {
        x = fsm_turns(x);
        if (!(x <= FSM_PI3_2)) x = x - FSM_PI2;
        if (!(x <= FSM_HALF_PI)) x = FSM_PI - x;
    } else if (x < -FSM_HALF_PI) {
        x = fsm_turns(x);
        if (x < -FSM_PI3_2) x = x + FSM_PI2;
        if (x < -FSM_HALF_PI) x = -FSM_PI - x;
    }
    /* x + r3 x^3 + r2 x^5 + r1 x^7 + r0 x^9, each term r[i] * x * x2 * x2 ... */
    float x2 = x * x;
    float t3 = ps2_u2f(r[3]) * x * x2;
    float t2 = ps2_u2f(r[2]) * x * x2 * x2;
    float t1 = ps2_u2f(r[1]) * x * x2 * x2 * x2;
    float t0 = ps2_u2f(r[0]) * x * x2 * x2 * x2 * x2;
    return ((((0.0f + x) + t3) + t2) + t1) + t0;
}

float fsmath_CosVU0(float x)
{
    static const unsigned int r[4] = {0x37d00d08, 0xbab60b61, 0x3d2aaaab, 0xbf000000};   /* 0x00271660 */
    float sign = 1.0f;

    if (!(x <= FSM_HALF_PI)) {
        x = fsm_turns(x);
        if (!(x <= FSM_PI3_2)) x = x - FSM_PI2;
        if (!(x <= FSM_HALF_PI)) { x = x - FSM_PI; sign = -1.0f; }
    } else if (x < -FSM_HALF_PI) {
        x = fsm_turns(x);
        if (x < -FSM_PI3_2) x = x + FSM_PI2;
        if (x < -FSM_HALF_PI) { x = x + FSM_PI; sign = -1.0f; }
    }
    /* 1 + r3 x^2 + r2 x^4 + r1 x^6 + r0 x^8, each term r[i] * x2 * x2 ... */
    float x2 = x * x;
    float t3 = ps2_u2f(r[3]) * x2;
    float t2 = ps2_u2f(r[2]) * x2 * x2;
    float t1 = ps2_u2f(r[1]) * x2 * x2 * x2;
    float t0 = ps2_u2f(r[0]) * x2 * x2 * x2 * x2;
    return ((((1.0f + t3) + t2) + t1) + t0) * sign;
}
