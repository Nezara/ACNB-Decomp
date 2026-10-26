// Shared VU1 HLE program table (design 4.3): C versions of known VU1 microprograms, selected by a hash of
// micro memory. Moved from runtime/vu1_hle.c (s75, phase 2 step 1.1); the frozen reference's dispatcher
// (vu1_hle.c) and the render module (rq_vu1.cpp) both call them. No mutable statics: every bit of state comes in
// through RqVu1Ctx (VU data memory, registers, TOP, kick callback, counters).
//
// --- "mmodel" (menu/garage model renderer, entries 0x200 = init+batch, 0x394 = batch) ---
// Static VU memory: qw0-3 view matrix V, qw4-7 object matrix B (row vectors, v*M), qw12-15 light
// directions, qw16-18 light colours, qw19 ambient, qw20.zw fog (f = z + w*q), qw21 GIF packet kicked at
// init, qw28-31 env matrix E (B*E -> qw32-35), qw64 = (uv scroll x, y, alpha scale, point light count),
// qw65-68 point light pos (w = falloff k), qw69-72 point light colours -> local space qw74-77 / 78-81.
// Batch at TOP: qw0/1 GIF tags (pass 1 / env pass; tag0.w > 0 enables the env pass), then NLOOP vertices
// of 4 qw: pos, normal, st (w low bits = extra flag bits), rgba. qw256.. A+D packets (256 pass 1, 259 env).
// Output at TOP+270: tag, then per vertex STQ, RGBA (int), XYZF2 (12.4 fixed; w = fog<<4 | ADC | flags).
#include "rq_vu1.h"
#include "rq_host.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t u32; typedef int32_t s32; typedef uint16_t u16; typedef int16_t s16;

#define Q(qw) ((float *)(vc->mem + ((qw) & 0x3ff) * 16))
#define QU(qw) ((u32 *)(vc->mem + ((qw) & 0x3ff) * 16))
#define KICK(a) vc->kick(vc, (a), 0)
#define KICKS(a) vc->kick(vc, (a), 1)
enum { WARN_UNLIT_A_TME = 1, WARN_FOGUNLIT_NLOOP0 = 2, WARN_AMAP_NLOOP = 4 };
static inline u32 f2u(float f) { u32 u; memcpy(&u, &f, 4); return u; }
static inline float u2f(u32 u) { float f; memcpy(&f, &u, 4); return f; }

/* VU float rules: no inf/NaN/denormals */
static inline float vin(float f)
{
    u32 u = f2u(f), e = u & 0x7f800000u;
    if (e == 0x7f800000u) return u2f((u & 0x80000000u) | 0x7f7fffffu);
    if (e == 0) return u2f(u & 0x80000000u);
    return f;
}
#define vout vin
static inline float mul(float a, float b) { return vout(vin(a) * vin(b)); }
static inline float madd(float acc, float a, float b) { return vout(vin(acc) + vin(a) * vin(b)); }
static inline float add(float a, float b) { return vout(vin(a) + vin(b)); }
static inline float sub(float a, float b) { return vout(vin(a) - vin(b)); }
static inline float fmin_(float a, float b) { a = vin(a); b = vin(b); return a < b ? a : b; }
static inline float fmax_(float a, float b) { a = vin(a); b = vin(b); return a > b ? a : b; }
static inline s32 ftoi(float x, float sc)
{
    x = vin(x) * sc;
    return x >= 2147483647.0f ? 0x7fffffff : x <= -2147483648.0f ? (s32)0x80000000 : (s32)x;
}
/* step 5.1 sideband (rq_vu1.h RqVu1Hxy): the float x, y behind the XYZ2 words just stored at o[0..1] */
static inline void hxy(RqVu1Ctx *vc, const u32 *o, float x, float y)
{
    if (!vc->hxy) return;
    RqVu1Hxy *h = &vc->hxy[(uint32_t)(((const uint8_t *)o - vc->mem) >> 4) & 0x3ff];
    h->x = vin(x) * 16.0f; h->y = vin(y) * 16.0f; h->ix = o[0]; h->iy = o[1]; h->gen = vc->hgen;
}
static inline float rcp(float t)
{
    t = vin(t);
    return t == 0 ? u2f((f2u(t) & 0x80000000u) | 0x7f7fffffu) : vin(1.0f / t);
}

/* ---------------------------------------------------------------- mmodel */

#define VF(n) (vc->r->vf[n].f)


static void mm_batch(RqVu1Ctx *vc)
{
    u32 top = vc->top;
    u32 n = QU(top)[0] & 0x7fff;
    const float *m0 = VF(8), *m1 = VF(9), *m2 = VF(10), *m3 = VF(11);
    const float *l0 = VF(12), *l1 = VF(13), *l2 = VF(14), *c0 = VF(15), *c1 = VF(16), *c2 = VF(17), *amb = VF(18);
    float fogz = mul(1.0f, VF(3)[2]), fogw = VF(3)[3], su = VF(4)[0], sv = VF(4)[1], ascale = VF(5)[2];
    float cx = VF(6)[0], cy = VF(6)[1], gb = VF(7)[3];
    u32 o1 = 0, o2 = 0;
    for (u32 k = 0; k < n; k++) {
        const float *P = Q(top + 2 + 4 * k), *N = Q(top + 3 + 4 * k), *T = Q(top + 4 + 4 * k), *C = Q(top + 5 + 4 * k);
        float cl[4];
        for (int i = 0; i < 4; i++) cl[i] = madd(madd(madd(mul(m0[i], P[0]), m1[i], P[1]), m2[i], P[2]), m3[i], 1.0f);
        float q = rcp(cl[3]);
        float x = mul(cl[0], q), y = mul(cl[1], q), z = mul(cl[2], q);
        float fog = fmax_(fmin_(madd(fogz, fogw, q), 255.0f), 0.0f);
        float sx = sub(x, cx), sy = sub(y, cy);
        u32 out = z < 0 || sx > gb || sx < -gb || sy > gb || sy < -gb;
        u32 adc = (out | o1 | o2) ? 0x8000 : 0;
        o2 = o1; o1 = out;
        float nl[3], col[4];
        for (int i = 0; i < 3; i++) nl[i] = fmax_(madd(madd(mul(l0[i], N[0]), l1[i], N[1]), l2[i], N[2]), 0.0f);
        for (int i = 0; i < 3; i++) {
            float lit = add(madd(madd(mul(c0[i], nl[0]), c1[i], nl[1]), c2[i], nl[2]), amb[i]);
            col[i] = fmin_(mul(lit, C[i]), 255.0f);
        }
        col[3] = fmin_(mul(C[3], ascale), 255.0f);
        u32 *o = QU(top + 271 + 3 * k);
        float *of = (float *)o;
        of[0] = mul(add(T[0], su), q); of[1] = mul(add(T[1], sv), q); of[2] = mul(T[2], q);
        for (int i = 0; i < 4; i++) o[4 + i] = (u32)ftoi(col[i], 1.0f);
        o[8] = (u32)ftoi(x, 16.0f); o[9] = (u32)ftoi(y, 16.0f); o[10] = (u32)ftoi(z, 16.0f); hxy(vc, o + 8, x, y);
        o[11] = ((u32)ftoi(fog, 16.0f) & 0xffff) | adc | (QU(top + 4 + 4 * k)[3] & 0xffff);
    }

    /* point lights: additive colour with 1 - k*d^2 falloff, no N.L */
    s16 npl = (s16)(QU(73)[3] & 0xffff);
    vc->st_vert += n; if (npl > 0) vc->st_pl++;
    for (int j = 0; j < npl; j++) {
        const float *lp = Q(74 + j), *lc = Q(78 + j);
        for (u32 k = 0; k < n; k++) {
            const float *P = Q(top + 2 + 4 * k);
            float d[3] = {sub(lp[0], P[0]), sub(lp[1], P[1]), sub(lp[2], P[2])};
            float p = vin(vin(d[0]) * vin(d[0]) + vin(d[1]) * vin(d[1]) + vin(d[2]) * vin(d[2]));
            float att = sub(1.0f, mul(p, lp[3]));
            s32 *rgba = (s32 *)QU(top + 272 + 3 * k);
            for (int i = 0; i < 3; i++)
                rgba[i] = ftoi(fmin_(add((float)rgba[i], fmax_(mul(lc[i], att), 0.0f)), 255.0f), 1.0f);
            rgba[3] = ftoi(fmin_((float)rgba[3], 255.0f), 1.0f);
        }
    }

    KICK(top + 256);
    memcpy(Q(top + 270), Q(top), 16);
    KICKS(top + 270);
    if ((s16)(QU(top)[3] & 0xffff) > 0) {   /* env (sphere map) pass on context 2: st = (1 + N*(B*E)) / 2 */
        float *a = VF(3), *b = VF(5), *c = VF(7);
        a[0] = Q(32)[0]; a[1] = Q(32)[1]; b[0] = Q(33)[0]; b[1] = Q(33)[1]; c[0] = Q(34)[0]; c[1] = Q(34)[1];
        vc->st_env++;
        KICK(top + 259);
        for (u32 k = 0; k <= n; k++) {   /* the microcode also writes one slot past the end */
            const float *N = Q(top + 3 + 4 * k);
            float *o = Q(top + 271 + 3 * k);
            for (int i = 0; i < 2; i++) o[i] = madd(madd(madd(1.0f, a[i], N[0]), b[i], N[1]), c[i], N[2]);
            o[2] = 2.0f; o[3] = 3.0f;
        }
        memcpy(Q(top + 270), Q(top + 1), 16);
        KICKS(top + 270);
    }
    vc->r->tpc = 0x394 * 8;
}

static void mm_init(RqVu1Ctx *vc)
{
    const float *A[4] = {Q(0), Q(1), Q(2), Q(3)}, *B[4] = {Q(4), Q(5), Q(6), Q(7)};
    memset(VF(1), 0, 16);
    for (int i = 0; i < 4; i++) VF(2)[i] = 255.0f;
    VF(6)[0] = VF(6)[1] = 2048.0f; VF(6)[2] = VF(6)[3] = 0;
    VF(7)[3] = 2047.0f;
    float M[4][4], BE[4][4];
    for (int r = 0; r < 4; r++)
        for (int i = 0; i < 4; i++) {
            M[r][i] = madd(madd(madd(mul(A[0][i], B[r][0]), A[1][i], B[r][1]), A[2][i], B[r][2]), A[3][i], B[r][3]);
            BE[r][i] = madd(madd(madd(mul(Q(28)[i], B[r][0]), Q(29)[i], B[r][1]), Q(30)[i], B[r][2]), Q(31)[i], B[r][3]);
        }
    KICK(21);
    float L[4][3];
    for (int j = 0; j < 4; j++) for (int i = 0; i < 3; i++) L[j][i] = Q(12 + j)[i];
    for (int r = 0; r < 3; r++)
        for (int i = 0; i < 3; i++)
            VF(12 + r)[i] = madd(madd(madd(mul(L[0][i], B[r][0]), L[1][i], B[r][1]), L[2][i], B[r][2]), L[3][i], B[r][3]);
    for (int r = 0; r < 4; r++) memcpy(VF(8 + r), M[r], 16);
    for (int r = 0; r < 4; r++) memcpy(Q(32 + r), BE[r], 16);
    for (int r = 0; r < 3; r++) memcpy(VF(15 + r), Q(16 + r), 12);
    memcpy(VF(18), Q(19), 12); VF(18)[3] = A[3][3];
    VF(3)[2] = Q(20)[2]; VF(3)[3] = Q(20)[3];
    VF(4)[0] = Q(64)[0]; VF(4)[1] = Q(64)[1]; VF(5)[2] = Q(64)[2];
    u32 npl = QU(64)[3] & 0xffff;
    QU(73)[3] = npl;
    if ((s16)npl > 0) {   /* point lights into object space (axis signs as the microcode does them) */
        float R[3][3] = {{B[0][0], -B[0][1], -B[0][2]}, {-B[1][0], B[1][1], -B[1][2]}, {-B[2][0], -B[2][1], B[2][2]}};
        float out[8][4];
        for (int j = 0; j < 4; j++) {
            const float *lp = Q(65 + j);
            float d[3] = {sub(lp[0], B[3][0]), sub(lp[1], B[3][1]), sub(lp[2], B[3][2])};
            for (int i = 0; i < 3; i++) out[j][i] = madd(madd(mul(R[0][i], d[0]), R[1][i], d[1]), R[2][i], d[2]);
            out[j][3] = lp[3];
            memcpy(out[4 + j], Q(69 + j), 16);
        }
        memcpy(Q(74), out, sizeof out);
    }
    mm_batch(vc);
}

/* ---------------------------------------------------------------- unlit
 * Two schedulings of one "unlit model" program, in two different microcode images:
 *   unlit_a: entry 0x600 in the mmodel image (init + batch on every MSCAL; MSCNT would resume at 0x68a).
 *   unlit_b: entry 0x200 = init + batch, MSCNT resumes at 0x2b2 (B 0x23d) = batch only.
 * Init: M = B*A (qw4-7 * qw0-3) -> qw8-11 and vf1-4; vf18 = (2048, 2048, 0, 2047) guard band; vf19 = 255; vf24 = 0.
 * Batch at TOP: same vertex layout as mmodel (pos, normal, st (w = flag bits), rgba; 4 qw each), normal unused.
 * Per vertex: clip = pos*M, q = 1/clip.w, STQ = st*q, XYZ = clip*q (12.4), ADC over the last 3 strip verts
 * (z < 0 or outside the guard band), st.w flag bits ORed in except on the last vertex (epilogue omits it).
 * a: RGBA = vertex colour, F = 255, STQ slots never written (untextured); b: RGBA = 128, F word = ftoi4(clip.w).
 * a, when every vertex is outside: no vertex kick and the last vertex's RGBA/XYZ slots keep their old contents.
 * Output: tag copy at TOP+tag, vertices from TOP+tag+1 (STQ, RGBA, XYZF2). Kicks: A+D at TOP+256, then the
 * vertex packet (a: skipped when every vertex is outside).
 * unlit_b's init also leaves B*qw24-27 in qw28-31, qw64 in qw73, and a half-built vector in qw77 from the
 * stale vf23 (the microcode reads it uninitialised) - replicated as is. */

static void unlit_batch(RqVu1Ctx *vc, u32 tag, int b)
{
    u32 top = vc->top;
    u32 n = QU(top)[0] & 0x7fff;
    const float *m0 = VF(1), *m1 = VF(2), *m2 = VF(3), *m3 = VF(4);
    float cx = VF(18)[0], cy = VF(18)[1], cz = VF(18)[2], gb = fabsf(VF(18)[3]), zc = fabsf(VF(24)[3]);
    float fogc = add(VF(24)[3], VF(19)[3]);
    u32 o1 = 0, o2 = 0, nout = 0, last[8];
    for (u32 k = 0; k < n; k++) {
        const float *P = Q(top + 2 + 4 * k), *T = Q(top + 4 + 4 * k), *C = Q(top + 5 + 4 * k);
        if (!b && k + 1 == n) memcpy(last, QU(top + tag + 1 + 3 * k) + 4, 32);
        float cl[4];
        for (int i = 0; i < 4; i++) cl[i] = madd(madd(madd(mul(m0[i], P[0]), m1[i], P[1]), m2[i], P[2]), m3[i], P[3]);
        float q = rcp(cl[3]);
        float x = mul(cl[0], q), y = mul(cl[1], q), z = mul(cl[2], q);
        float sx = sub(x, cx), sy = sub(y, cy), sz = sub(z, cz);
        u32 out = sz < -zc || sx > gb || sx < -gb || sy > gb || sy < -gb;
        u32 adc = (out | o1 | o2) ? 0x8000 : 0;
        o2 = o1; o1 = out; nout += out;
        u32 *o = QU(top + tag + 1 + 3 * k);
        float *of = (float *)o;
        if (b) for (int i = 0; i < 3; i++) of[i] = mul(T[i], q);   /* a's loop never writes STQ (untextured) */
        for (int i = 0; i < 4; i++) o[4 + i] = b ? 128 : (u32)ftoi(C[i], 1.0f);
        o[8] = (u32)ftoi(x, 16.0f); o[9] = (u32)ftoi(y, 16.0f); o[10] = (u32)ftoi(z, 16.0f); hxy(vc, o + 8, x, y);
        u32 flags = k + 1 < n ? QU(top + 4 + 4 * k)[3] & 0xffff : 0;   /* the epilogue drops the last vertex's flags */
        o[11] = ((u32)ftoi(b ? cl[3] : fogc, 16.0f) & 0xffff) | adc | flags;
    }
    vc->st_vert += n;
    if (!b) {   /* a leaves STQ stale; only harmless while the draw is untextured */
        const u32 *t = QU(top), *ad = QU(top + 256);
        int tme = (t[1] >> 14 & 1) ? (int)(t[1] >> 19 & 1) : 0;
        for (u32 i = 1; i <= (ad[0] & 0x7fff) && i < 14; i++)
            if ((QU(top + 256 + i)[2] & 0xff) == 0) tme = QU(top + 256 + i)[0] >> 4 & 1;
        if (tme && !(vc->warned & WARN_UNLIT_A_TME)) { vc->warned |= WARN_UNLIT_A_TME; rq_log("vu1hle: unlit_a draw with TME=1 (top %03x) - STQ is not computed, expect garbage", top); }
        memcpy(VF(12), Q(20), 16);   /* the prologue reloads vf12 = qw20 (unused) */
        /* every vertex outside: the epilogue branches past the last vertex's RGBA/XYZ stores and the kick */
        if (n && nout == n) memcpy(QU(top + tag + 1 + 3 * (n - 1)) + 4, last, 32);
    }
    memcpy(Q(top + tag), Q(top), 16);
    KICK(top + 256);
    if (b || nout < n) KICKS(top + tag);
}

static void mat_rows(float out[4][4], const float *const R[4], const float *const A[4])   /* out = R * A (row vectors) */
{
    for (int r = 0; r < 4; r++)
        for (int i = 0; i < 4; i++)
            out[r][i] = madd(madd(madd(mul(A[0][i], R[r][0]), A[1][i], R[r][1]), A[2][i], R[r][2]), A[3][i], R[r][3]);
}

static void unlit_consts(RqVu1Ctx *vc, float big)
{
    VF(17)[0] = VF(17)[1] = VF(17)[2] = 0; VF(17)[3] = big;
    VF(18)[0] = VF(18)[1] = 2048.0f; VF(18)[2] = 0; VF(18)[3] = 2047.0f;
    for (int i = 0; i < 4; i++) { VF(19)[i] = 255.0f; VF(24)[i] = 0; }
}

static void ua_batch(RqVu1Ctx *vc) { unlit_batch(vc, 262, 0); vc->r->tpc = 0x68a * 8; }

static void ua_init(RqVu1Ctx *vc)
{
    const float *A[4] = {Q(0), Q(1), Q(2), Q(3)}, *B[4] = {Q(4), Q(5), Q(6), Q(7)};
    float M[4][4], Bc[4][4];
    mat_rows(M, B, A);
    for (int r = 0; r < 4; r++) memcpy(Bc[r], B[r], 16);
    for (int r = 0; r < 4; r++) {
        memcpy(Q(8 + r), M[r], 16); memcpy(VF(1 + r), M[r], 16);
        memcpy(VF(5 + r), Bc[r], 16); memcpy(VF(9 + r), M[r], 16);
    }
    unlit_consts(vc, u2f(0x4b7fff28));
    ua_batch(vc);
}

static void ub_batch(RqVu1Ctx *vc) { unlit_batch(vc, 270, 1); vc->r->tpc = 0x2b2 * 8; }

static void ub_init(RqVu1Ctx *vc)
{
    const float *A[4] = {Q(0), Q(1), Q(2), Q(3)}, *B[4] = {Q(4), Q(5), Q(6), Q(7)}, *A2[4] = {Q(24), Q(25), Q(26), Q(27)};
    const float *L[4] = {Q(12), Q(13), Q(14), Q(15)};
    float M[4][4], BA2[4][4], BL[4][4], Bc[4][4], q64[4], v23[4], v28[4];
    for (int r = 0; r < 4; r++) memcpy(Bc[r], B[r], 16);
    memcpy(q64, Q(64), 16); memcpy(v23, VF(23), 16); memcpy(v28, VF(28), 16);
    mat_rows(M, B, A);
    mat_rows(BA2, B, A2);
    mat_rows(BL, B, L);
    v28[3] = mul(1.0f, v23[3]);
    v28[2] = madd(madd(mul(Bc[0][2], v23[0]), Bc[1][2], v23[1]), Bc[2][2], v23[2]);
    for (int r = 0; r < 4; r++) { memcpy(Q(8 + r), M[r], 16); memcpy(Q(28 + r), BA2[r], 16); }
    memcpy(Q(73), q64, 16);
    memcpy(Q(77), v28, 16);
    for (int r = 0; r < 4; r++) memcpy(VF(1 + r), M[r], 16);
    for (int r = 0; r < 3; r++) memcpy(VF(5 + r), BL[r], 16);
    for (int r = 0; r < 4; r++) memcpy(VF(8 + r), Q(16 + r), 16);
    for (int i = 0; i < 4; i++) VF(12)[i] = add(0, Bc[2][i]);
    memcpy(VF(13), BA2[3], 16);
    memcpy(VF(28), v28, 16);
    unlit_consts(vc, 16777216.0f);
    ub_batch(vc);
}

/* ---------------------------------------------------------------- proj (battle image A)
 * Projected-texture pass (light/shadow decal volume). Entry 0x200 = init + batch, MSCNT resumes at 0x28f (B 0x24b).
 * Init: M = B*A (qw4-7 * qw0-3) -> vf9-12 with M[3].z += 10000; BL = B*qw12-15 -> vf20-23 and qw32-35;
 * qw26 = (0.5, 0.5, 1, 0); qw75 = qw74 * qw16-19 (volume centre in object space); qw77 = (qw76.xyz^2, qw76.w)
 * (squared radii, w = projection scale); vf18 = (2048, 2048, 0, 2047) guard band; vf19 = qw27 (constant colour).
 * Batch at TOP (n = 0: no kick): vertices of 4 qw (pos, normal, st (w = flag bits), rgba) from TOP+2.
 * clip = pos*M, q = 1/clip.w, XYZ = clip*q; STQ = ((0.5, 0.5) + (pos*BL).xy * qw77.w, 1, 0) * q; RGBA = vf19.
 * ADC over the last 3 verts of (outside guard band / z < 0, or any r^2 - d^2 < 0: outside the volume), ORed with
 * the st.w flag bits; F = 0. Output: tag at TOP+270, vertices from TOP+271. */

static void pj_batch(RqVu1Ctx *vc)
{
    u32 top = vc->top;
    u32 n = QU(top)[0] & 0x7fff;
    vc->r->tpc = 0x28f * 8;
    if (!n) return;
    const float *m0 = VF(9), *m1 = VF(10), *m2 = VF(11), *m3 = VF(12);
    const float *b0 = VF(20), *b1 = VF(21), *b2 = VF(22), *b3 = VF(23);
    const float *pt = Q(75), *r2 = Q(77), *st0 = Q(26);
    float cx = VF(18)[0], cy = VF(18)[1], cz = VF(18)[2], gb = fabsf(VF(18)[3]);
    u32 col[4];
    memcpy(col, VF(19), 16);
    u32 h1 = 0, h2 = 0;
    for (u32 k = 0; k < n; k++) {
        const float *P = Q(top + 2 + 4 * k);
        float e[3];
        for (int i = 0; i < 3; i++) { float d = sub(P[i], pt[i]); e[i] = sub(r2[i], mul(d, d)); }
        u32 outside_vol = e[0] < 0 || e[1] < 0 || e[2] < 0;
        float cl[4], t[4];
        for (int i = 0; i < 4; i++) {
            cl[i] = madd(madd(madd(mul(m0[i], P[0]), m1[i], P[1]), m2[i], P[2]), m3[i], P[3]);
            t[i] = madd(madd(madd(mul(b0[i], P[0]), b1[i], P[1]), b2[i], P[2]), b3[i], P[3]);
        }
        float q = rcp(cl[3]);
        float s[4] = {add(st0[0], mul(t[0], r2[3])), add(st0[1], mul(t[1], r2[3])), st0[2], st0[3]};
        float x = mul(cl[0], q), y = mul(cl[1], q), z = mul(cl[2], q);
        float sx = sub(x, cx), sy = sub(y, cy), sz = sub(z, cz);
        u32 out = sz < 0 || sx > gb || sx < -gb || sy > gb || sy < -gb;
        u32 self = (out || outside_vol) ? 0x8000 : 0;
        u32 *o = QU(top + 271 + 3 * k);
        float *of = (float *)o;
        for (int i = 0; i < 4; i++) of[i] = mul(s[i], q);
        memcpy(o + 4, col, 16);
        o[8] = (u32)ftoi(x, 16.0f); o[9] = (u32)ftoi(y, 16.0f); o[10] = (u32)ftoi(z, 16.0f); hxy(vc, o + 8, x, y);
        o[11] = h1 | h2 | (QU(top + 4 + 4 * k)[3] & 0xffff) | self;
        h2 = h1; h1 = self;
    }
    vc->st_vert += n;
    memcpy(Q(top + 270), Q(top), 16);
    KICKS(top + 270);
}

static void pj_init(RqVu1Ctx *vc)
{
    const float *A[4] = {Q(0), Q(1), Q(2), Q(3)}, *B[4] = {Q(4), Q(5), Q(6), Q(7)}, *L[4] = {Q(12), Q(13), Q(14), Q(15)};
    const float *R[4] = {Q(16), Q(17), Q(18), Q(19)};
    float M[4][4], BL[4][4], c[4], r[4], v[4];
    mat_rows(M, B, A);
    mat_rows(BL, B, L);
    M[3][2] = add(M[3][2], 10000.0f);
    memcpy(v, Q(74), 16);
    for (int i = 0; i < 4; i++) c[i] = madd(madd(madd(mul(R[0][i], v[0]), R[1][i], v[1]), R[2][i], v[2]), R[3][i], v[3]);
    memcpy(r, Q(76), 16);
    for (int i = 0; i < 3; i++) r[i] = mul(r[i], r[i]);
    float half[4] = {0.5f, 0.5f, 1.0f, 0};
    memcpy(Q(26), half, 16);
    for (int k = 0; k < 4; k++) {
        memcpy(VF(9 + k), M[k], 16); memcpy(VF(20 + k), BL[k], 16); memcpy(Q(32 + k), BL[k], 16);
        memcpy(VF(1 + k), R[k], 16); memcpy(VF(5 + k), B[k], 16);
    }
    memcpy(Q(75), c, 16); memcpy(VF(31), c, 16);
    memcpy(Q(77), r, 16); memcpy(VF(30), r, 16);
    memcpy(VF(26), v, 16);
    memcpy(VF(19), Q(27), 16);
    VF(18)[0] = VF(18)[1] = 2048.0f; VF(18)[2] = 0; VF(18)[3] = 2047.0f;
    memset(VF(24), 0, 16);
    pj_batch(vc);
}

/* ---------------------------------------------------------------- fogunlit (battle image B)
 * Fogged unlit model. Entry 0x200 = init + batch, MSCNT resumes at 0x265 (B 0x218).
 * Init: M = B*A -> vf1-4; vf5 = qw11 (colour scale), vf12 = qw12 / vf13 = qw13 (fog f = qw12.w + qw13.w * q),
 * vf22 = qw14 (st scroll), vf11 = (0,0,0,255), vf18 guard band, vi15 = 0 (last texture key), vi7 = 0x8000.
 * Batch at TOP: the A+D packet at TOP+256 is kicked only when qw257.x & 0x3fff differs from vi15 (then vi15 = it).
 * Vertices of 3 qw (pos, st, rgba) from TOP+1. clip = pos*M, q = 1/clip.w, XYZ = clip*q, STQ = (st.xy + scroll,
 * st.z) * q (w = st.w as loaded), c = qw11 * rgba: RGB = int(c), A = int(f * c.a / 255) with f clamped to 0..255,
 * F word = ftoi4(f) | ADC (last 3 verts outside the guard band / z < 0; no vertex flag bits).
 * Output: tag at TOP+268, vertices from TOP+269. The loop is a do-while (n = 0 would run 65536 times). */

static void fu_batch(RqVu1Ctx *vc)
{
    u32 top = vc->top;
    u32 key = QU(top + 257)[0] & 0x3fff;
    if (key != vc->r->vi[15]) { vc->r->vi[15] = (u16)key; KICK(top + 256); }
    u32 n = QU(top)[0] & 0x7fff;
    if (!n) { if (!(vc->warned & WARN_FOGUNLIT_NLOOP0)) { vc->warned |= WARN_FOGUNLIT_NLOOP0; rq_log("vu1hle: fogunlit batch with NLOOP 0 (top %03x)", top); } n = 0x10000; }
    memcpy(Q(top + 268), Q(top), 16);
    const float *m0 = VF(1), *m1 = VF(2), *m2 = VF(3), *m3 = VF(4), *cs = VF(5);
    float fogz = mul(VF(12)[3], 1.0f), fogw = VF(13)[3], su = VF(22)[0], sv = VF(22)[1];
    float cx = VF(18)[0], cy = VF(18)[1], cz = VF(18)[2], gb = fabsf(VF(18)[3]), zc = fabsf(VF(20)[3]);
    float inv = mul(1.0f, rcp(VF(11)[3]));
    u32 o1 = 0, o2 = 0;
    for (u32 k = 0; k < n; k++) {
        const float *P = Q(top + 1 + 3 * k), *T = Q(top + 2 + 3 * k), *C = Q(top + 3 + 3 * k);
        float cl[4], c[4];
        for (int i = 0; i < 4; i++) {
            cl[i] = madd(madd(madd(mul(m0[i], P[0]), m1[i], P[1]), m2[i], P[2]), m3[i], P[3]);
            c[i] = mul(cs[i], C[i]);
        }
        float q = rcp(cl[3]);
        float x = mul(cl[0], q), y = mul(cl[1], q), z = mul(cl[2], q);
        float f = fmax_(fmin_(madd(fogz, fogw, q), 255.0f), 0.0f);
        float sx = sub(x, cx), sy = sub(y, cy), sz = sub(z, cz);
        u32 out = sz < -zc || sx > gb || sx < -gb || sy > gb || sy < -gb;
        u32 adc = (out | o1 | o2) ? 0x8000 : 0;
        o2 = o1; o1 = out;
        u32 *o = QU(top + 269 + 3 * k);
        float *of = (float *)o;
        of[0] = mul(add(T[0], su), q); of[1] = mul(add(T[1], sv), q); of[2] = mul(T[2], q); o[3] = QU(top + 2 + 3 * k)[3];
        for (int i = 0; i < 3; i++) o[4 + i] = (u32)ftoi(c[i], 1.0f);
        o[7] = (u32)ftoi(mul(mul(f, c[3]), inv), 1.0f);
        o[8] = (u32)ftoi(x, 16.0f); o[9] = (u32)ftoi(y, 16.0f); o[10] = (u32)ftoi(z, 16.0f); hxy(vc, o + 8, x, y);
        o[11] = ((u32)ftoi(f, 16.0f) & 0xffff) | adc;
    }
    vc->st_vert += n;
    VF(16)[3] = inv;
    KICKS(top + 268);
    vc->r->tpc = 0x265 * 8;
}

static void fu_init(RqVu1Ctx *vc)
{
    const float *A[4] = {Q(0), Q(1), Q(2), Q(3)}, *B[4] = {Q(4), Q(5), Q(6), Q(7)};
    float M[4][4];
    mat_rows(M, B, A);
    for (int k = 0; k < 4; k++) { memcpy(VF(1 + k), M[k], 16); memcpy(VF(22 + k), A[k], 16); memcpy(VF(26 + k), B[k], 16); }
    memcpy(VF(22), Q(14), 16);
    memcpy(VF(5), Q(11), 16); memcpy(VF(12), Q(12), 16); memcpy(VF(13), Q(13), 16);
    memset(VF(11), 0, 16); VF(11)[3] = 255.0f;
    memset(VF(20), 0, 16);
    VF(18)[0] = VF(18)[1] = 2048.0f; VF(18)[2] = 0; VF(18)[3] = 2047.0f;
    vc->r->vi[13] = 0; vc->r->vi[15] = 0; vc->r->vi[7] = 0x8000;
    vc->r->clip = 0;
    fu_batch(vc);
}

/* ---------------------------------------------------------------- amap (g_pmpgVu1Amap, AreaMap screen)
 * Flat-colour untextured model pass of the AreaMap (__AMAP_MakeDL builds DF_MkDL_Model / DF_MkDL_ACModel DLs, so
 * the batch layout is mmodel's). Entry 0x200 = init + batch, MSCNT resumes at 0x2d7 (B 0x25c) = batch only.
 * Init: M = B*A (qw4-7 * qw0-3) -> qw8-11 and vf1-4; qw73 = qw64; qw74-77 = R*(qw65-68 - B3).xyz (mmodel's
 * point-light axis signs, w = 1*qw.w); qw78-81 = qw69-72; vf5-7 = B*qw12-15 (rows 0-2), vf8-11 = qw16-19,
 * vf13 = B3, vf17 = (0,0,0,2^24), vf18 = (2048,2048,0,2047) guard band, vf19 = 128, vf24 = 0. The batch reads only
 * vf1-4, vf18, vf24 (and the stale vf21.w, into a slot the tag overwrites).
 * Batch at TOP: n = TOP.x & 0x7fff, vertices of 4 qw from TOP+2 (pos, normal, st, rgba); only pos (w used) and the
 * st.w flag bits are read. clip = pos*M, q = 1/clip.w, s = clip*q (4 lanes); RGBA = (84,46,20,64);
 * XYZF2 = ftoi4(s.xyz), F word = (u16)(ftoi4(s.w) | ADC | flags), ADC over the last 3 verts (outside the guard
 * band / z < 0), no flags on the last vertex. Output: tag at TOP+267 = qw TOP, .y = 0 + qw16.y (float add) when
 * (s16)TOP.y > 0; vertices from TOP+268 (STQ slot never written). Kicks: A+D at TOP+256 (before the last vertex is
 * stored), then the vertex packet unless every vertex is outside (then the last XYZF2 slot keeps old contents).
 * Software pipelined, mirrored register by register: the first loop pass writes RGBA / the stale vf21 into
 * TOP+266 / 267 (266 lies inside the A+D packet area; 267 is overwritten by the tag). The loop counter is n-2:
 * n < 3 wraps into a 65536-pass loop (replicated, warned once). */

static inline u32 am_out(const float *v, float gb, float zc)   /* FCAND 0x3e0 after CLIP vs vf18.w, vs vf24.w */
{
    return vin(v[2]) < -zc || vin(v[0]) > gb || vin(v[0]) < -gb || vin(v[1]) > gb || vin(v[1]) < -gb;
}

static void am_batch(RqVu1Ctx *vc)
{
    static const u32 rgba[4] = {84, 46, 20, 64};   /* LOI 84/46/20/64 + FTOI0 */
    RqVuRegs *R = vc->r;
    u32 top = vc->top;
    const float *m0 = VF(1), *m1 = VF(2), *m2 = VF(3), *m3 = VF(4), *g = VF(18);
    float gb = fabsf(vin(VF(18)[3])), zc = fabsf(vin(VF(24)[3]));
    float P[4], c[4], s[4], acc[4], v30[3], tag[4], q;
    u32 x21[4];
    u16 n = (u16)(QU(top)[0] & 0x7fff);
    u16 vi11 = (u16)(n - 2), vi13 = n, vi6 = 0, vi8 = 0, vi9 = 0, vi1 = 0, vi2, vi14, vi15, out = 0;
    u32 vi12 = top + 265, vi7 = top + 6;
    if (n < 3 && !(vc->warned & WARN_AMAP_NLOOP)) { vc->warned |= WARN_AMAP_NLOOP; rq_log("vu1hle: amap batch with NLOOP %u (top %03x) - 65536-pass loop", n, top); }
#define AM_CLIP(o, a) for (int i = 0; i < 4; i++) (o)[i] = madd((a)[i], m3[i], P[3])
#define AM_ACC(a) for (int i = 0; i < 4; i++) (a)[i] = madd(madd(mul(m0[i], P[0]), m1[i], P[1]), m2[i], P[2])
    memcpy(P, Q(top + 2), 16);
    AM_ACC(acc); AM_CLIP(c, acc);                  /* vf31 = clip0 */
    memcpy(P, Q(top + 6), 16);                     /* vf25 = P1 (the prologue's vf30 is recomputed before any CLIP) */
    memcpy(VF(12), Q(20), 16);                     /* LQ vf12, 20(vi1) (unused) */
    memcpy(x21, VF(21), 16);                       /* stale vf21 */
    vi14 = (u16)QU(top)[3];                        /* ILW.w -6(vi7) = TOP.w */
    do {                                           /* 0x27b..0x28f, j-th pass: vertex j's maths, vertex j-1's stores */
        memcpy(QU(vi12 + 1), rgba, 16);
        q = rcp(c[3]);
        vi2 = (u16)x21[3];
        vi1 = out;
        AM_ACC(acc);
        vi7 += 4;
        vi9 |= vi1; vi6 = (u16)(vi6 + vi1);
        vi9 = (u16)(((u16)(vi9 + 0x7fff) & 0x8000) | vi14);
        vi2 |= vi9;
        memcpy(QU(vi12 + 2), x21, 16);
        vi14 = (u16)QU(vi7 - 6)[3];
        vi9 = vi8 | vi1;                           /* | vi10 (0) */
        for (int i = 0; i < 4; i++) s[i] = mul(c[i], q);
        for (int i = 0; i < 3; i++) v30[i] = sub(s[i], g[i]);
        for (int i = 0; i < 4; i++) x21[i] = (u32)ftoi(s[i], 16.0f);
        vi8 = vi1;
        AM_CLIP(c, acc);
        vi11--;
        QU(vi12 + 2)[3] = vi2;
        out = (u16)am_out(v30, gb, zc);
        vi12 += 3;
        memcpy(P, Q(vi7), 16);                     /* delay slot: LQ vf25, 0(vi7) */
    } while (vi11);
    /* epilogue 0x290..0x2d6: vertices n-2 and n-1 */
    q = rcp(c[3]);
    memcpy(QU(vi12 + 1), rgba, 16);
    vi7 += 4;
    memcpy(QU(vi12 + 2), x21, 16);
    vi1 = out;
    vi2 = (u16)x21[3];
    vi9 |= vi1;
    for (int i = 0; i < 4; i++) s[i] = mul(c[i], q);
    vi9 = (u16)(vi9 + 0x7fff); vi6 = (u16)(vi6 + vi1); vi9 &= 0x8000; vi9 |= vi14;
    for (int i = 0; i < 3; i++) v30[i] = sub(s[i], g[i]);
    vi2 |= vi9;
    AM_ACC(acc);
    vi9 = vi1 | vi8; vi8 = vi1;
    QU(vi12 + 2)[3] = vi2;
    out = (u16)am_out(v30, gb, zc);
    vi12 += 3;
    for (int i = 0; i < 4; i++) x21[i] = (u32)ftoi(s[i], 16.0f);
    vi14 = (u16)QU(vi7 - 6)[3];
    AM_CLIP(c, acc);
    vi1 = out; vi6 = (u16)(vi6 + vi1);
    q = rcp(c[3]);
    vi9 |= vi1; vi9 = (u16)(vi9 + 0x7fff);
    memcpy(QU(vi12 + 2), x21, 16);
    vi2 = (u16)x21[3];
    memcpy(tag, Q(top), 16);                       /* vf25 = qw TOP */
    vi9 &= 0x8000; vi9 |= vi14;
    for (int i = 0; i < 4; i++) s[i] = mul(c[i], q);
    memcpy(QU(vi12 + 1), rgba, 16);
    vi2 |= vi9;
    vi9 = vi1 | vi8;
    for (int i = 0; i < 3; i++) v30[i] = sub(s[i], g[i]);
    vi8 = vi1;
    vi15 = (u16)QU(top)[1];
    if ((s16)vi15 > 0) { memcpy(VF(22), Q(16), 16); tag[1] = add(0.0f, VF(22)[1]); }   /* ADD.y vf25, vf0, vf22 */
    QU(vi12 + 2)[3] = vi2;
    memcpy(Q(top + 267), tag, 16);
    out = (u16)am_out(v30, gb, zc);
    vi12 += 3;
    vi6 = (u16)(vi6 - vi13);
    KICK(top + 256);
    memcpy(QU(vi12 + 1), rgba, 16);
    vi1 = out; vi6 = (u16)(vi6 + vi1); vi9 |= vi1;
    for (int i = 0; i < 4; i++) x21[i] = (u32)ftoi(s[i], 16.0f);   /* delay slot of IBGEZ: runs on both paths */
    vi9 = (u16)(vi9 + 0x7fff);
    if ((s16)vi6 < 0) {                            /* not every vertex outside */
        vi9 &= 0x8000;
        vi2 = (u16)x21[3];
        vi2 |= vi9;
        memcpy(QU(vi12 + 2), x21, 16);
        QU(vi12 + 2)[3] = vi2;
        KICKS(top + 267);
    }
#undef AM_CLIP
#undef AM_ACC
    vc->st_vert += n;
    memcpy(VF(20), rgba, 16); memcpy(VF(21), x21, 16); memcpy(VF(25), tag, 16);
    memcpy(VF(30), v30, 12); memcpy(VF(31), s, 16);
    memcpy(R->acc, acc, 16);
    R->q = q; R->i = 64.0f;
    u16 *vi = R->vi;
    vi[1] = vi1; vi[2] = vi2; vi[3] = 0x8000; vi[4] = (u16)(top + 267); vi[5] = (u16)top; vi[6] = vi6;
    vi[7] = (u16)vi7; vi[8] = vi8; vi[9] = vi9; vi[10] = 0; vi[11] = vi11; vi[12] = (u16)vi12; vi[13] = vi13;
    vi[14] = vi14; vi[15] = vi15;
    R->tpc = 0x2d7 * 8;
}

static void am_init(RqVu1Ctx *vc)
{
    const float *A[4] = {Q(0), Q(1), Q(2), Q(3)}, *B[4] = {Q(4), Q(5), Q(6), Q(7)}, *L[4] = {Q(12), Q(13), Q(14), Q(15)};
    float M[4][4], BL[4][4], r[3][4], b3[4], d[4][4], o[4][4];
    mat_rows(M, B, A);
    mat_rows(BL, B, L);
    for (int i = 0; i < 4; i++) { r[0][i] = add(0, B[0][i]); r[1][i] = add(0, B[1][i]); r[2][i] = add(0, B[2][i]); b3[i] = add(0, B[3][i]); }
    r[0][1] = sub(0, r[0][1]); r[0][2] = sub(0, r[0][2]);
    r[1][0] = sub(0, r[1][0]); r[1][2] = sub(0, r[1][2]);
    r[2][0] = sub(0, r[2][0]); r[2][1] = sub(0, r[2][1]);
    for (int k = 0; k < 4; k++) {
        memcpy(d[k], Q(65 + k), 16);
        for (int i = 0; i < 3; i++) d[k][i] = sub(d[k][i], b3[i]);
        for (int i = 0; i < 3; i++) o[k][i] = madd(madd(mul(r[0][i], d[k][0]), r[1][i], d[k][1]), r[2][i], d[k][2]);
        o[k][3] = mul(1.0f, d[k][3]);
    }
    for (int k = 0; k < 4; k++) memcpy(Q(8 + k), M[k], 16);
    memcpy(Q(73), Q(64), 16);
    for (int k = 0; k < 4; k++) memcpy(Q(74 + k), o[k], 16);
    for (int k = 0; k < 4; k++) memcpy(Q(78 + k), Q(69 + k), 16);
    for (int k = 0; k < 4; k++) { memcpy(VF(1 + k), M[k], 16); memcpy(VF(8 + k), Q(16 + k), 16); memcpy(VF(20 + k), d[k], 16); memcpy(VF(25 + k), o[k], 16); }
    for (int k = 0; k < 3; k++) memcpy(VF(5 + k), BL[k], 16);
    memcpy(VF(12), r[2], 16); memcpy(VF(13), b3, 16);
    VF(17)[0] = VF(17)[1] = VF(17)[2] = 0; VF(17)[3] = 16777216.0f;
    VF(18)[0] = VF(18)[1] = 2048.0f; VF(18)[2] = 0; VF(18)[3] = 2047.0f;
    for (int i = 0; i < 4; i++) { VF(19)[i] = 128.0f; VF(24)[i] = 0; }
    am_batch(vc);
}

// ---------------------------------------------------------------- table

extern "C" const RqVu1Prog rq_vu1_progs[] = {
    {"mmodel", 0x200, 0x396, 0x33e883c2, {mm_init, mm_batch}, {0x200, 0x394}, 270, 0, 1, 0, 0},
    {"unlit_a", 0x600, 0x68c, 0xb0792e71, {ua_init, ua_batch}, {0x600, 0x68a}, 262, 0x010e1ffe, 0, 0, 0},
    {"unlit_b", 0x200, 0x2b4, 0xe1c9f213, {ub_init, ub_batch}, {0x200, 0x2b2}, 270, 0x010e3ffe, 1, 0, 0},
    {"proj", 0x200, 0x290, 0x69b1d951, {pj_init, pj_batch}, {0x200, 0x28f}, 270, 0x01fc1e00, 1, 0, 0},
    {"fogunlit", 0x200, 0x266, 0x6c9460b2, {fu_init, fu_batch}, {0x200, 0x265}, 268, 0x0054383e, 1, 0x8080, 0},
    /* enabled s77: ACNB_VU1_HLE_CHECK=1 garage3 159,752 checked (= every call), 0 differ, live mask 0xfffffffe */
    {"amap", 0x200, 0x2d9, 0x82cc75fa, {am_init, am_batch}, {0x200, 0x2d7}, 267, 0xfffffffe, 0, 0, 0},
};
extern "C" const int rq_vu1_nprogs = (int)(sizeof rq_vu1_progs / sizeof rq_vu1_progs[0]);
