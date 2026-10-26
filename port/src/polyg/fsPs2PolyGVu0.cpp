/* src/polyg/fsPs2PolyGVu0.cpp — D3DX-style maths helpers of the polygon-movie ("PolyG" / PGB cutscene) player:
 * matrix multiply / rotation (axis, pitch-yaw-roll, quaternion) / translation / identity, vec3 cross / normalize,
 * vec4 transform / Hermite / lerp. The original is VU0 macro-mode inline asm (lqc2 / vmulax / vmadd.. / sqc2): every
 * helper keeps its op order and lane masks, and the 4th float of the 16-byte _D3DXVECTOR3 is copied, not zeroed.
 * Row-vector convention (v' = v * M, translation in row 3). lqc2 ignores the low 4 address bits: sources that may
 * point into the PGB file go through PS2_LQ_SRC. VU0 registers left by the original (vf16..25, ACC) are not
 * mirrored (no reader known). */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"

#ifdef ACNB_SOURCEPORT
float gPolyGVu0_IdentityMat[4][4] = {
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
};
#endif

static_assert(sizeof(_D3DXVECTOR3) == 0x10, "_D3DXVECTOR3");
static_assert(sizeof(_D3DXMATRIX) == 0x40, "_D3DXMATRIX");

/* 16-byte quad load (lqc2): copy of the aligned-down source */
static inline void pgvu0_lq(float *d, const void *p)
{
    memcpy(d, PS2_LQ_SRC(p), 16);
}

/* lq x4 / sq x4 of the identity */
void fsPs2PolyGVu0_MatrixIdentity(_D3DXMATRIX *pMat)
{
    memcpy(pMat->m, gPolyGVu0_IdentityMat, 0x40);
}

/* vopmula.xyz / vopmsub.xyz into the V2 register: out.xyz = V1 x V2, out.w = V2.w */
void fsPs2PolyGVu0_Vec3Cross(_D3DXVECTOR3 *pOut, _D3DXVECTOR3 *pV1, _D3DXVECTOR3 *pV2)
{
    float a[4], b[4];
    pgvu0_lq(a, pV1);
    pgvu0_lq(b, pV2);
    vu_cross(b, a, b);   /* vu_cross reads all inputs before writing d.xyz; b.w stays V2.w */
    memcpy(pOut, b, 16);
}

/* len2 = (z*z + x*x) + y*y (accumulated into .z), q = sqrt, r = 1 / q (vdiv vf0w), out.xyz = v * r, out.w = v.w */
void fsPs2PolyGVu0_Vec3Normalize(_D3DXVECTOR3 *pOut, _D3DXVECTOR3 *pV)
{
    float v[4];
    pgvu0_lq(v, pV);
    float xx = v[0] * v[0], yy = v[1] * v[1], zz = v[2] * v[2];
    float len2 = (zz + xx) + yy;
    float q = ps2_sqrt(len2);
    float r = ps2_div(1.0f, 1.0f * q);   /* vmulq.xyzw vf17, vf0, q: vf17.w = 1.0 * q */
    float o[4] = {v[0] * r, v[1] * r, v[2] * r, v[3]};
    memcpy(pOut, o, 16);
}

/* out row r = ((M2[0]*r.x + M2[1]*r.y) + M2[2]*r.z) + M2[3]*r.w for each row r of M1 (pOut = M1 x M2) */
void fsPs2PolyGVu0_MatrixMultiply(_D3DXMATRIX *pOut, _D3DXMATRIX *pM1, _D3DXMATRIX *pM2)
{
    float a[4][4], b[4][4];
    for (int i = 0; i < 4; i++) {
        pgvu0_lq(a[i], &pM2->m[i]);
        pgvu0_lq(b[i], &pM1->m[i]);
    }
    vu_mul_matrix(pOut->m, (const float (*)[4])a, (const float (*)[4])b);
}

/* identity rows, row 3 lanes x/y/z = 0.0f + v (vaddx.x/.y/.z: -0.0 becomes +0.0), [3][3] = 1 */
void fsPs2PolyGVu0_MatrixTranslation(_D3DXMATRIX *pOut, float x, float y, float z)
{
    float m[4][4];
    memcpy(m, gPolyGVu0_IdentityMat, sizeof m);
    m[3][0] = m[3][0] + x;
    m[3][1] = m[3][1] + y;
    m[3][2] = m[3][2] + z;
    memcpy(pOut->m, m, sizeof m);
}

/* out = ((M[0]*v.x + M[1]*v.y) + M[2]*v.z) + M[3]*v.w, all four lanes */
void fsPs2PolyGVu0_Vec4Transform(_D3DXVECTOR4 *pOut, _D3DXVECTOR4 *pV, _D3DXMATRIX *pM)
{
    float v[4], m[4][4];
    pgvu0_lq(v, pV);
    for (int i = 0; i < 4; i++) pgvu0_lq(m[i], &pM->m[i]);
    vu_apply(v, (const float (*)[4])m, v);
    memcpy(pOut, v, 16);
}

/* scalar FPU: s = 2 / (w*w + ((x*x + y*y) + z*z)) (0 when the norm is <= 0), products and sums in the asm order */
void fsPs2PolyGVu0_MatrixRotationQuaternion(_D3DXMATRIX *pOut, _D3DXQUATERNION *pQ)
{
    float y = pQ->y, x = pQ->x, z = pQ->z, w = pQ->w;
    float yy = y * y, xx = x * x, zz = z * z;
    float sum = (xx + yy) + zz;
    float norm = w * w + sum;
    float s = 0.0f;
    if (!(norm <= 0.0f)) s = ps2_div(2.0f, norm);
    float sxy = s * (x * y);
    float sxz = s * (x * z);
    float syy = s * yy;
    float sxw = s * (x * w);
    float sxx = s * xx;
    float syz = s * (y * z);
    float swz = s * (w * z);
    float szz = s * zz;
    float syw = s * (y * w);
    float (*m)[4] = pOut->m;
    m[0][0] = 1.0f - (syy + szz);
    m[0][1] = sxy + swz;
    m[0][2] = sxz - syw;
    m[1][0] = sxy - swz;
    m[1][1] = 1.0f - (sxx + szz);
    m[1][2] = syz + sxw;
    m[2][0] = sxz + syw;
    m[2][1] = syz - sxw;
    m[2][2] = 1.0f - (sxx + syy);
    m[2][3] = 0.0f;
    m[1][3] = 0.0f;
    m[0][3] = 0.0f;
    m[3][2] = 0.0f;
    m[3][1] = 0.0f;
    m[3][0] = 0.0f;
    m[3][3] = 1.0f;
}

/* quaternion (axis * sin(a/2), cos(a/2)) at sp+0x40 (the axis is not normalised) -> RotationQuaternion */
void fsPs2PolyGVu0_MatrixRotationAxis(_D3DXMATRIX *pOut, _D3DXVECTOR3 *pV, float Angle)
{
    PS2_FRAME(0x50);
    float half = 0.5f * Angle;
    float s = fsmath_SinVU0(half);
    _D3DXQUATERNION q __attribute__((aligned(16)));
    q.x = pV->x * s;
    q.y = pV->y * s;
    q.z = pV->z * s;
    q.w = fsmath_CosVU0(half);
    fsPs2PolyGVu0_MatrixRotationQuaternion(pOut, &q);
}

/* X (Pitch) from the identity, then Y (Yaw), then Z (Roll) */
void fsPs2PolyGVu0_MatrixRotationPitchYawRoll(_D3DXMATRIX *pOut, float Yaw, float Pitch, float Roll)
{
    PS2_FRAME(0x30);
    sceVu0RotMatrixX(pOut->m, gPolyGVu0_IdentityMat, Pitch);
    sceVu0RotMatrixY(pOut->m, pOut->m, Yaw);
    sceVu0RotMatrixZ(pOut->m, pOut->m, Roll);
}

/* Hermite basis h (EE FPU, asm order; h0 uses mula.s / msub.s: products rounded), then
 * out = ((V0*h0 + V1*h1) + T0*h2) + T1*h3 over all four lanes */
void fsPs2PolyGVu0_Vec4Hermite(_D3DXVECTOR4 *pOut, _D3DXVECTOR4 *pV0, _D3DXVECTOR4 *pT0, _D3DXVECTOR4 *pV1,
                               _D3DXVECTOR4 *pT1, float s)
{
    float s2 = s * s;
    float s3 = s2 * s;
    float acc = 2.0f * s3;               /* mula.s */
    float n2s3 = -2.0f * s3;
    float h[4];
    h[2] = s + (s3 - 2.0f * s2);
    h[3] = s3 - s2;
    h[0] = 1.0f + (acc - 3.0f * s2);     /* msub.s */
    h[1] = n2s3 + 3.0f * s2;
    float m[4][4];
    pgvu0_lq(m[0], pV0);
    pgvu0_lq(m[1], pV1);
    pgvu0_lq(m[2], pT0);
    pgvu0_lq(m[3], pT1);
    vu_apply(h, (const float (*)[4])m, h);
    memcpy(pOut, h, 16);
}

/* ACC = V1*s; ACC -= V0*s; out = ACC + V0*1.0 (vmaddw vf0w), all four lanes */
void fsPs2PolyGVu0_Vec4Lerp(_D3DXVECTOR4 *pOut, _D3DXVECTOR4 *pV0, _D3DXVECTOR4 *pV1, float s)
{
    float a[4], b[4], o[4];
    pgvu0_lq(a, pV0);
    pgvu0_lq(b, pV1);
    for (int i = 0; i < 4; i++) {
        float acc = b[i] * s;
        acc = acc - a[i] * s;
        o[i] = acc + a[i] * 1.0f;
    }
    memcpy(pOut, o, 16);
}
