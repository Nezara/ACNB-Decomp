/* src/lib/matrix.cpp - model matrix helpers (whole file). DF_MakeModelLocalMatrix_Model / ..ZXY_Model build
 * StModel.matrix2 from scale, Euler angles (each wrapped by a truncating fmod into roughly [-PI, PI)) and location,
 * or a camera-facing billboard (mflag 4: Z rotation, then CAMERA_MATRIX2) or a camera-attached matrix (mflag 0x4000,
 * Model variant only); the temporaries live in the scratchpad. DF_MakeModelLightMatrix_Chr blends a character's
 * light direction / colour matrices between two map light sets by ylfrm (or copies the parent's / the map's) and
 * divides the direction rows by the model scale. DF_GetSRTXZY decomposes a matrix into location, XZY angles and
 * scale; DF_RotateMatrix applies a matrix without its translation. */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/vu0m.h"

#ifdef ACNB_SOURCEPORT
/* [0][0] and [2][2] are 1.00015f (0x3f8004ea) in the original data; both users overwrite the diagonal */
alignas(16) float identity2[4][4] = {
    {ps2_u2f(0x3f8004eau), 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, ps2_u2f(0x3f8004eau), 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
};
#endif

/* t = PI + a; (t - TWO_PI * trunc(t / TWO_PI)) - PI: truncates toward zero, so angles below -PI stay below */
static inline float matrix_wrap(float a)
{
    const float pi = ps2_u2f(0x40490fdbu), two_pi = ps2_u2f(0x40c90fdbu);
    float t = pi + a;
    float n = (float)ps2_ftoi(ps2_div(t, two_pi));
    return (t - two_pi * n) - pi;
}

/* identity2 with the scale on the diagonal */
static inline void matrix_scale(float (*m)[4], const float *scale)
{
    sceVu0CopyMatrix(m, identity2);
    m[0][0] = scale[0];
    m[1][1] = scale[1];
    m[2][2] = scale[2];
}

/* billboard: scale, Z rotation, then the camera's rotation (inline VU0: m[i] = CAMERA_MATRIX2 * m[i]) */
static void matrix_billboard(StModel *mp, float *loc, float *ang, float *scale)
{
    float (*m)[4] = (float (*)[4])PS2_SPR(0);
    matrix_scale(m, scale);
    sceVu0RotMatrixZ(m, m, matrix_wrap(ang[2]));
    vu_mul_matrix(m, (const float (*)[4])CAMERA_MATRIX2, (const float (*)[4])m);
    sceVu0TransMatrix(mp->matrix2, m, loc);
}

void DF_GetSRTXZY(float (*mat)[4], float *loc, float *ang, float *scl)
{
    if (loc) memcpy(loc, mat[3], 16);   /* lq / sq */
    if (ang) {
        float h = ps2_sqrt(mat[0][0] * mat[0][0] + mat[0][2] * mat[0][2]);
        ang[2] = ps2_atan2f(mat[0][1], h);
        ang[1] = -ps2_atan2f(mat[0][2], mat[0][0]);
        float v[4] __attribute__((aligned(16)));
        v[0] = -mat[0][2];
        v[1] = 0.0f;
        v[2] = mat[0][0];
        v[3] = 1.0f;
        sceVu0Normalize(v, v);
        float d = v[0] * mat[2][0] + v[1] * mat[2][1];
        ang[0] = fsmath_AcosVU0(d + v[2] * mat[2][2]);
        if (!(mat[2][1] <= 0.0f)) ang[0] = ang[0] * -1.0f;
        ang[3] = 1.0f;
    }
    if (scl) {
        for (int i = 0; i < 3; i++) {
            float s = mat[i][0] * mat[i][0] + mat[i][1] * mat[i][1];
            scl[i] = ps2_sqrt(s + mat[i][2] * mat[i][2]);
        }
    }
}

/* vec = mat * vec2 without the translation (all four lanes; mat[3][3] stays in the product) */
void DF_RotateMatrix(float *vec, float (*mat)[4], float *vec2)
{
    float (*m)[4] = (float (*)[4])PS2_SPR(0);
    sceVu0CopyMatrix(m, mat);
    m[3][0] = 0.0f;
    m[3][1] = 0.0f;
    m[3][2] = 0.0f;
    vu_apply(vec, (const float (*)[4])m, vec2);
}

void DF_MakeModelLocalMatrixZXY_Model(StModel *mp, float *loc, float *ang, float *scale)
{
    if (mp->mflag & 4) {
        matrix_billboard(mp, loc, ang, scale);
        return;
    }
    float (*m)[4] = (float (*)[4])PS2_SPR(0xc0);
    matrix_scale(m, scale);
    sceVu0RotMatrixZ(m, m, matrix_wrap(ang[2]));
    sceVu0RotMatrixX(m, m, matrix_wrap(ang[0]));
    sceVu0RotMatrixY(m, m, matrix_wrap(ang[1]));
    sceVu0TransMatrix(mp->matrix2, m, loc);
}

/* mflag 0x4000 (camera-attached) uses mp->loc, not loc */
void DF_MakeModelLocalMatrix_Model(StModel *mp, float *loc, float *ang, float *scale)
{
    unsigned int mflag = mp->mflag;
    if (mflag & 4) {
        matrix_billboard(mp, loc, ang, scale);
    } else if (mflag & 0x4000) {
        sceVu0TransMatrix(mp->matrix2, CAMERA_MATRIX3, mp->loc);
    } else {
        float (*m)[4] = (float (*)[4])PS2_SPR(0);
        matrix_scale(m, scale);
        sceVu0RotMatrixX(m, m, matrix_wrap(ang[0]));
        sceVu0RotMatrixY(m, m, matrix_wrap(ang[1]));
        sceVu0RotMatrixZ(m, m, matrix_wrap(ang[2]));
        sceVu0TransMatrix(mp->matrix2, m, loc);
    }
}

/* llist is not NULL-checked in the blend; Map->mlist is not checked in the fallback */
void DF_MakeModelLightMatrix_Chr(StModel *mp, float (*mat)[4], float (*matc)[4])
{
    float t = ps2_div((float)mp->ylfrm, 12.0f);
    float u = 1.0f - t;
    if (mp->llistbk && !(u <= 0.0f)) {
        /* vmulx + vmulx + vadd per row */
        _map_light_st *a = mp->llist, *b = mp->llistbk;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) mat[i][j] = a->dir[i][j] * t + b->dir[i][j] * u;
        a = mp->llist;
        b = mp->llistbk;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) matc[i][j] = a->col[i][j] * t + b->col[i][j] * u;
    } else if (mp->nowparent && mp->nowparent->mlist) {
        sceVu0CopyMatrix(mat, mp->nowparent->mlist->lp_chrlight->dir);
        sceVu0CopyMatrix(matc, mp->nowparent->mlist->lp_chrlight->col);
    } else {
        sceVu0CopyMatrix(mat, Map->mlist->lp_chrlight->dir);
        sceVu0CopyMatrix(matc, Map->mlist->lp_chrlight->col);
    }
    sceVu0DivVectorXYZ(mat[0], mat[0], mp->scale[0]);
    sceVu0DivVectorXYZ(mat[1], mat[1], mp->scale[1]);
    sceVu0DivVectorXYZ(mat[2], mat[2], mp->scale[2]);
}
