/* lib/sdk/libvu0.cpp — Sony libvu0 (main ELF 0x139738-0x13a480, hand-written VU0 macro code, no DWARF).
 * Each function performs the IEEE operations of its VU0 sequence in the same order (see vu0m.h), so results are
 * bit-identical to the interpreter; registered at the SDK addresses (runtime/bridge_sdk.c) so interpreted callers use them as well.
 * sceVu0ViewScreenMatrix (0x139f48) is plain FPU code around UnitMatrix / MulMatrix. */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/vu0m.h"

void sceVu0ApplyMatrix(float *v0, float (*m0)[4], float *v1)
{
    vu_apply(v0, (const float (*)[4])m0, v1);
}

void sceVu0MulMatrix(float (*m0)[4], float (*m1)[4], float (*m2)[4])
{
    vu_mul_matrix(m0, (const float (*)[4])m1, (const float (*)[4])m2);
}

/* vopmula.xyz / vopmsub.xyz; w = vf6.w - vf6.w = 0 */
void sceVu0OuterProduct(float *v0, float *v1, float *v2)
{
    float a0 = v1[1] * v2[2], a1 = v1[2] * v2[0], a2 = v1[0] * v2[1];
    float r0 = a0 - v2[1] * v1[2], r1 = a1 - v2[2] * v1[0], r2 = a2 - v2[0] * v1[1];
    v0[0] = r0; v0[1] = r1; v0[2] = r2; v0[3] = 0.0f;
}

float sceVu0InnerProduct(float *v0, float *v1)
{
    return vu_dot3(v0, v1);
}

/* xyz scaled by 1/|v|, w = 0 */
void sceVu0Normalize(float *v0, float *v1)
{
    float q = vu_rcp_len3(v1);
    float x = v1[0] * q, y = v1[1] * q, z = v1[2] * q;
    v0[0] = x; v0[1] = y; v0[2] = z; v0[3] = 0.0f;
}

void sceVu0TransposeMatrix(float (*m0)[4], float (*m1)[4])
{
    float t[4][4];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) t[i][j] = m1[j][i];
    memcpy(m0, t, sizeof t);
}

/* Inverse of a rigid transform: rows 0-2 = transposed 3x3 (w 0), row 3 = (-(R^T t), m1[3].w). */
void sceVu0InversMatrix(float (*m0)[4], float (*m1)[4])
{
    float t[3][4], tr[4];
    memcpy(tr, m1[3], sizeof tr);
    for (int i = 0; i < 3; i++) {
        t[i][0] = m1[0][i]; t[i][1] = m1[1][i]; t[i][2] = m1[2][i]; t[i][3] = 0.0f;
    }
    float r[4];
    for (int i = 0; i < 3; i++) {
        float a = t[0][i] * tr[0];
        a = a + t[1][i] * tr[1];
        r[i] = 0.0f - (a + t[2][i] * tr[2]);
    }
    r[3] = tr[3];
    memcpy(m0, t, sizeof t);
    memcpy(m0[3], r, sizeof r);
}

void sceVu0DivVector(float *v0, float *v1, float q)
{
    float s = ps2_div(1.0f, q);
    for (int i = 0; i < 4; i++) v0[i] = v1[i] * s;
}

void sceVu0DivVectorXYZ(float *v0, float *v1, float q)
{
    float s = ps2_div(1.0f, q), w = v1[3];
    for (int i = 0; i < 3; i++) v0[i] = v1[i] * s;
    v0[3] = w;
}

/* v1*t + v2*(1-t) */
void sceVu0InterVector(float *v0, float *v1, float *v2, float t)
{
    float u = (0.0f + 1.0f) - t, r[4];
    for (int i = 0; i < 4; i++) r[i] = v1[i] * t + v2[i] * u;
    memcpy(v0, r, sizeof r);
}

void sceVu0AddVector(float *v0, float *v1, float *v2)
{
    float r[4];
    for (int i = 0; i < 4; i++) r[i] = v1[i] + v2[i];
    memcpy(v0, r, sizeof r);
}

void sceVu0SubVector(float *v0, float *v1, float *v2)
{
    float r[4];
    for (int i = 0; i < 4; i++) r[i] = v1[i] - v2[i];
    memcpy(v0, r, sizeof r);
}

void sceVu0MulVector(float *v0, float *v1, float *v2)
{
    float r[4];
    for (int i = 0; i < 4; i++) r[i] = v1[i] * v2[i];
    memcpy(v0, r, sizeof r);
}

void sceVu0ScaleVector(float *v0, float *v1, float s)
{
    float r[4];
    for (int i = 0; i < 4; i++) r[i] = v1[i] * s;
    memcpy(v0, r, sizeof r);
}

void sceVu0TransMatrix(float (*m0)[4], float (*m1)[4], float *tv)
{
    float t[4][4];
    memcpy(t, m1, sizeof t);
    for (int i = 0; i < 3; i++) t[3][i] = t[3][i] + tv[i];
    memcpy(m0, t, sizeof t);
}

void sceVu0CopyVector(float *v0, float *v1) { memmove(v0, v1, 16); }
void sceVu0CopyMatrix(float (*m0)[4], float (*m1)[4]) { memmove(m0, m1, 64); }

static void ftoi(int *v0, const float *v1, float sc)
{
    int r[4];
    for (int i = 0; i < 4; i++) r[i] = ps2_ftoi(v1[i] * sc);
    memcpy(v0, r, sizeof r);
}

void sceVu0FTOI4Vector(int *v0, float *v1) { ftoi(v0, v1, 16.0f); }
void sceVu0FTOI0Vector(int *v0, float *v1) { ftoi(v0, v1, 1.0f); }

void sceVu0ITOF4Vector(float *v0, int *v1)
{
    float r[4];
    for (int i = 0; i < 4; i++) r[i] = (float)v1[i] * (1.0f / 16);
    memcpy(v0, r, sizeof r);
}

void sceVu0ITOF0Vector(float *v0, int *v1)
{
    float r[4];
    for (int i = 0; i < 4; i++) r[i] = (float)v1[i];
    memcpy(v0, r, sizeof r);
}

void sceVu0UnitMatrix(float (*m0)[4])
{
    static const float u[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    memcpy(m0, u, sizeof u);
}

/* _sceVu0ecossin (0x139a88): t = pi/2 -+ r, s = odd polynomial in t (= cos r), sin r = +-sqrt(1 - s^2)
 * with the sign of r. Coefficients at 0x2596a0. */
static void ecossin(float r, float *sn, float *cs)
{
    const float k[4] = {ps2_u2f(0x362e9c14), ps2_u2f(0xb94fb21f), ps2_u2f(0x3c08873e), ps2_u2f(0xbe2aaaa4)};
    const float half_pi = ps2_u2f(0x3fc90fdb);
    int neg = r < 0.0f;
    float t = neg ? half_pi + r : half_pi - r;
    float acc = 0.0f + t, t2 = t * t;
    float c0 = k[0] * t, c1 = k[1] * t, c2 = k[2] * t, c3 = k[3] * t;
    c0 = c0 * t2; c1 = c1 * t2; c2 = c2 * t2; c3 = c3 * t2;
    c0 = c0 * t2; c1 = c1 * t2; c2 = c2 * t2;
    acc = acc + c3;
    c0 = c0 * t2; c1 = c1 * t2;
    acc = acc + c2;
    c0 = c0 * t2;
    acc = acc + c1;
    acc = acc + c0;
    float s = 0.0f + acc;
    float q = ps2_sqrt(1.0f - s * s);
    q = 0.0f + q;
    *sn = neg ? 0.0f - q : 0.0f + q;
    *cs = s;
}

/* m0[i] = R applied to m1[i] (row by row, m0 may alias m1) */
static void rot_apply(float (*m0)[4], const float (*R)[4], float (*m1)[4])
{
    for (int i = 0; i < 4; i++) vu_apply(m0[i], R, m1[i]);
}

void sceVu0RotMatrixZ(float (*m0)[4], float (*m1)[4], float rz)
{
    float s, c;
    ecossin(rz, &s, &c);
    const float R[4][4] = {{0.0f + c, 0.0f + s, 0, 0}, {0.0f - s, 0.0f + c, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    rot_apply(m0, R, m1);
}

void sceVu0RotMatrixX(float (*m0)[4], float (*m1)[4], float rx)
{
    float s, c;
    ecossin(rx, &s, &c);
    const float R[4][4] = {{1, 0, 0, 0}, {0, 0.0f + c, 0.0f + s, 0}, {0, 0.0f - s, 0.0f + c, 0}, {0, 0, 0, 1}};
    rot_apply(m0, R, m1);
}

void sceVu0RotMatrixY(float (*m0)[4], float (*m1)[4], float ry)
{
    float s, c;
    ecossin(ry, &s, &c);
    const float R[4][4] = {{0.0f + c, 0, 0.0f - s, 0}, {0, 1, 0, 0}, {0.0f + s, 0, 0.0f + c, 0}, {0, 0, 0, 1}};
    rot_apply(m0, R, m1);
}

void sceVu0RotMatrix(float (*m0)[4], float (*m1)[4], float *rot)
{
    sceVu0RotMatrixZ(m0, m1, rot[2]);
    sceVu0RotMatrixY(m0, m0, rot[1]);
    sceVu0RotMatrixX(m0, m0, rot[0]);
}

/* v0 = ftoi4(xyz / w, w) of m0 * v1; mode != 0: z and w as ftoi0 */
void sceVu0RotTransPers(int *v0, float (*m0)[4], float *v1, int mode)
{
    float p[4];
    vu_apply(p, (const float (*)[4])m0, v1);
    float q = ps2_div(1.0f, p[3]);
    p[0] = p[0] * q; p[1] = p[1] * q; p[2] = p[2] * q;
    int r[4];
    for (int i = 0; i < 4; i++) r[i] = ps2_ftoi(p[i] * 16.0f);
    if (mode) { r[2] = ps2_ftoi(p[2]); r[3] = ps2_ftoi(p[3]); }
    memcpy(v0, r, sizeof r);
}

/* sceVu0ViewScreenMatrix (0x139f48): m = Unit with scrz on [0][0] / [1][1], [2][2] = [3][3] = 0, [2][3] = [3][2] = 1,
 * times the screen matrix (ax, ay scale; cx, cy offset; z mapped from [nearz, farz] to [zmin, zmax]). MIPS FPU, single
 * precision, in the original's order (ps2_div for the two div.s). MW EABI passes farz (the 9th float) in the caller's
 * outgoing slot at sp + 0: the hybrid stores it there for native callers like their MIPS twins (the bw_ entry reads it
 * from there). The 0xa0 frame is kept: tmp at sp + 0 (UnitMatrix fills all of it). The prologue's saves of the
 * caller's s0 / ra / f20..f26 are written by the bridge entry (bw_vsm) only, where the registers are the interpreted
 * caller's; native callers get the slots untouched and mirror the ra slot themselves. */
void sceVu0ViewScreenMatrix(float (*m)[4], float scrz, float ax, float ay, float cx, float cy, float zmin, float zmax,
                            float nearz, float farz)
{
#ifndef ACNB_SOURCEPORT
    *(float *)(uintptr_t)ee_cur->gpr[29].uw[0] = farz;
#endif
    PS2_FRAME(0xa0);
    float zr = -zmin + zmax;
    float t21 = farz * nearz;
    float t20 = -zmax * nearz;
    float t17 = zmin * farz;
    t20 = t20 + t17;
    float d = -nearz + farz;
    t21 = t21 * zr;
    t21 = ps2_div(t21, d);
    t20 = ps2_div(t20, d);
    sceVu0UnitMatrix(m);
    m[1][1] = scrz;
    m[0][0] = scrz;
    m[2][2] = 0.0f;
    m[3][3] = 0.0f;
    m[2][3] = 1.0f;
    m[3][2] = 1.0f;
    PS2_STACK(float[4][4], tmp, 0);
    sceVu0UnitMatrix(tmp);
    tmp[0][0] = ax;
    tmp[1][1] = ay;
    tmp[2][2] = t21;
    tmp[3][0] = cx;
    tmp[3][1] = cy;
    tmp[3][2] = t20;
    sceVu0MulMatrix(m, tmp, m);
}
