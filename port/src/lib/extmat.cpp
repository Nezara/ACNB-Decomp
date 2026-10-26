/* src/lib/extmat.cpp - "look at" matrices (whole file): the rotation that turns an object at localpos (in parentmat's
 * space) towards a world target, optionally clipped to pitch / yaw limits, with a given up vector.
 * Rows of the result: [2] = direction from the target to the object in the parent's frame (transpose of parentmat =
 * its inverse rotation), clipped by VectorClipXYang, normalised; [1] = the up vector in the parent's frame minus its
 * [2] part, normalised; [0] = [1] x [2]. Row 3 is not written. VU .xyz ops: the normalised rows get w = 0
 * (vf18 = vf0 - vf0), row 0 keeps row 1's w. */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/vu0m.h"

static const float EXTMAT_PI = 3.14159274f;   /* 0x40490fdb */

/* vsqrt / vdiv normalise of v.xyz into v, w = 0 */
static inline void extmat_normalize(float *v)
{
    float r = vu_rcp_len3(v);
    v[0] = v[0] * r;
    v[1] = v[1] * r;
    v[2] = v[2] * r;
    v[3] = 0.0f;
}

/* the body shared (inline in the original) by GetObjLookMatrix_Yvec and GetObjLookMatrix */
static inline void extmat_look(float (*dstmat)[4], float (*parentmat)[4], const float *yvec, float *localpos,
                               float *trgpos, float *limit)
{
    float wpos[4] __attribute__((aligned(16)));
    float vec[4] __attribute__((aligned(16)));
    float invmat[4][4] __attribute__((aligned(16)));

    vu_apply(wpos, (const float (*)[4])parentmat, localpos);
    DFvu0_TransposeMatrix(invmat, parentmat);
    vec[0] = wpos[0] - trgpos[0];   /* vsub.xyz: w stays wpos.w */
    vec[1] = wpos[1] - trgpos[1];
    vec[2] = wpos[2] - trgpos[2];
    vec[3] = wpos[3];
    vu_apply(dstmat[2], (const float (*)[4])invmat, vec);
    if (limit) VectorClipXYang(dstmat[2], dstmat[2], limit[0], limit[1], limit[2], limit[3]);
    extmat_normalize(dstmat[2]);

    float up[4];
    vu_apply(up, (const float (*)[4])invmat, yvec);
    float dot = dstmat[2][0] * up[0] + dstmat[2][1] * up[1];
    dot = dot + dstmat[2][2] * up[2];
    dstmat[1][0] = up[0] - dstmat[2][0] * dot;   /* vsub.xyz: w stays up.w */
    dstmat[1][1] = up[1] - dstmat[2][1] * dot;
    dstmat[1][2] = up[2] - dstmat[2][2] * dot;
    dstmat[1][3] = up[3];
    extmat_normalize(dstmat[1]);
    vu_cross(dstmat[0], dstmat[1], dstmat[2]);
    dstmat[0][3] = dstmat[1][3];
}

/* VU0 input rule: an exponent-255 word reads as +-FLT_MAX (sign kept) */
static inline float extmat_vu_in(float f)
{
    uint32_t u = ps2_f2u(f);
    return (u & 0x7f800000u) == 0x7f800000u ? ps2_u2f((u & 0x80000000u) | 0x7f7fffffu) : f;
}

/* Turns objp (angles, approached at rotspd by Matrix2AngleXZY_Approach) to look at target with up vector Yvec, limits
 * f_limit in degrees (pitch min/max, yaw min/max), then pitched by xofs degrees. */
void ObjLookWorldTarget2_Xofs(ObjectSt *objp, float (*parentmat)[4], float *Yvec, float *target, float *f_limit,
                              float rotspd, float xofs)
{
    PS2_FRAME(0x80);
    PS2_STACK(float[4], lim, 0x30);
    PS2_STACK(float[4][4], mat, 0x40);   /* row 3 is never written: dead stack, read by MulRotMatrixX / Approach */
    lim[0] = (EXTMAT_PI * f_limit[0]) / 180.0f;
    lim[1] = (EXTMAT_PI * f_limit[1]) / 180.0f;
    lim[2] = (EXTMAT_PI * f_limit[2]) / 180.0f;
    lim[3] = (EXTMAT_PI * f_limit[3]) / 180.0f;
    GetObjLookMatrix_Yvec(mat, parentmat, Yvec, objp->position, target, lim);
    /* VU0 input rule on the dead stack row 3 (never written; MulRotMatrixX's vmaddw reads it): exponent-255
     * words (stale 0xffffffff) clamp to +-FLT_MAX like the VU, otherwise host NaN would spread into the angles */
    for (int i = 0; i < 4; i++) mat[3][i] = extmat_vu_in(mat[3][i]);
    DFvu0_MulRotMatrixX(mat, mat, (EXTMAT_PI * xofs) / 180.0f);
    Matrix2AngleXZY_Approach(objp->angle, mat, rotspd);
}

void GetObjLookMatrix_Yvec(float (*dstmat)[4], float (*parentmat)[4], float *yvec, float *localpos, float *trgpos,
                           float *limit)
{
    PS2_FRAME(0xc0);
    extmat_look(dstmat, parentmat, yvec, localpos, trgpos, limit);
}

/* up vector = axisY (the original also fills an unused local {0, 1, 0, 0}) */
void GetObjLookMatrix(float (*dstmat)[4], float (*parentmat)[4], float *localpos, float *trgpos, float *limit)
{
    PS2_FRAME(0xb0);
    extmat_look(dstmat, parentmat, axisY, localpos, trgpos, limit);
}
