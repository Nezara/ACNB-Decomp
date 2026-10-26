/* src/lib/vu0.cpp + vu0.h — the game's VU0 helpers (MW inline asm; outlined copies of the vu0.h inlines plus the
 * "_ni" non-inline versions in vu0.c). Same operation order as the VU0 code (see acnb/vu0m.h); several are plain
 * tail calls into libvu0. "xyz" variants keep v1.w. */
#include "acnb/sdk.h"
#include "acnb/vu0m.h"

void acvu0_MulMatrix(float (*m0)[4], float (*m1)[4], float (*m2)[4])
{
    vu_mul_matrix(m0, (const float (*)[4])m1, (const float (*)[4])m2);
}

void acvu0_TransMatrix(float (*m0)[4], float (*m1)[4], float *tv) { sceVu0TransMatrix(m0, m1, tv); }

void acvu0_ApplyMatrix(float *v0, float (*m0)[4], float *v1) { vu_apply(v0, (const float (*)[4])m0, v1); }
void acvu0_ApplyMatrix_ni(float *v0, float (*m0)[4], float *v1) { vu_apply(v0, (const float (*)[4])m0, v1); }

void acvu0_RotMatrixZ(float (*m0)[4], float (*m1)[4], float rz) { sceVu0RotMatrixZ(m0, m1, rz); }
void acvu0_RotMatrixX(float (*m0)[4], float (*m1)[4], float rx) { sceVu0RotMatrixX(m0, m1, rx); }
void acvu0_RotMatrixY(float (*m0)[4], float (*m1)[4], float ry) { sceVu0RotMatrixY(m0, m1, ry); }
void acvu0_RotMatrixY_ni(float (*m0)[4], float (*m1)[4], float ry) { sceVu0RotMatrixY(m0, m1, ry); }

void acvu0_Normalize(float *v0, float *v1) { sceVu0Normalize(v0, v1); }
void acvu0_Normalize_ni(float *v0, float *v1) { sceVu0Normalize(v0, v1); }

/* (z² + x²) + y², scale = 1 / (1 * q); w kept */
void acvu0_Normalize_xyz(float *v0, float *v1)
{
    float x = v1[0] * v1[0], y = v1[1] * v1[1], z = v1[2] * v1[2];
    float q = ps2_sqrt((z + x) + y);
    q = ps2_div(1.0f, 1.0f * q);
    float r0 = v1[0] * q, r1 = v1[1] * q, r2 = v1[2] * q, w = v1[3];
    v0[0] = r0; v0[1] = r1; v0[2] = r2; v0[3] = w;
}

void acvu0_SubVector(float *v0, float *v1, float *v2)
{
    float r0 = v1[0] - v2[0], r1 = v1[1] - v2[1], r2 = v1[2] - v2[2], w = v1[3];
    v0[0] = r0; v0[1] = r1; v0[2] = r2; v0[3] = w;
}

void acvu0_SubVector_ni(float *v0, float *v1, float *v2) { acvu0_SubVector(v0, v1, v2); }

void acvu0_AddVector(float *v0, float *v1, float *v2)
{
    float r0 = v1[0] + v2[0], r1 = v1[1] + v2[1], r2 = v1[2] + v2[2], w = v1[3];
    v0[0] = r0; v0[1] = r1; v0[2] = r2; v0[3] = w;
}

void acvu0_ScaleVector(float *v0, float *v1, float s)
{
    float r0 = v1[0] * s, r1 = v1[1] * s, r2 = v1[2] * s, w = v1[3];
    v0[0] = r0; v0[1] = r1; v0[2] = r2; v0[3] = w;
}

void acvu0_ScaleVector_ni(float *v0, float *v1, float s) { acvu0_ScaleVector(v0, v1, s); }

void acvu0_CopyVectorF(float *v0, float *v1) { memmove(v0, v1, 16); }
void acvu0_CopyVectorF_ni(float *v0, float *v1) { memmove(v0, v1, 16); }
void acvu0_CopyMatrix(float (*dest)[4], float (*src)[4]) { memmove(dest, src, 64); }
void acvu0_CopyMatrix_ni(float (*dest)[4], float (*src)[4]) { memmove(dest, src, 64); }
void DF_VU0_Copy4QW(void *dest, void *src) { memmove(dest, src, 64); }
