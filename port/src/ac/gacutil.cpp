/* src/ac/gacutil.cpp - small AC model utilities (whole file): the 5-part body colour CLUT, a byte colour
 * interpolator, a Z / Y vector pair -> rotation matrix, copying the parent-model fields between models, and the
 * "discernible vertex" matrix of a marked joint (ObjCMatSt). */
#include <string.h>
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"

/* 250 CLUT words (5 parts x 50): per part a ramp {3,3,3} -> part colour + 30 (32 steps) -> + 100 more (18 steps).
 * original: lo = colour + 30 wraps as a byte, so bright parts (>= 226) turn dark; hi[3] is never written (it only
 * reaches res[3], which is not used) */
void GAcUtil_MakeBodyPartsClut(unsigned int *pbufClut, StColorU8 *colParts)
{
    PS2_FRAME(0xa0);
    PS2_STACK(StColorU8, res, 0x90);
    PS2_STACK(StColorU8, hi, 0x94);
    PS2_STACK(StColorU8, lo, 0x98);
    PS2_STACK(StColorU8, start, 0x9c);
    unsigned int startword = 0x00030303;   /* gp-0x7f98 */
    memcpy(&start, &startword, 4);
    int g = 0;
    for (int p = 0; p < 5; p++, colParts++) {
        for (int k = 0; k < 3; k++) {
            lo.rgba[k] = (unsigned char)(colParts->rgba[k] + 30);
            unsigned int t = lo.rgba[k] + 100u;
            hi.rgba[k] = t < 0xff ? (unsigned char)t : 0xff;
        }
        lo.rgba[3] = 0;
        for (int i = 0; i < 50; i++) {
            if (i < 32) GAcUtil_CalcInterColor(&res, &start, &lo, i, 32);
            else GAcUtil_CalcInterColor(&res, &lo, &hi, i - 32, 18);
            /* 8-bit CLUT swizzle: entries 8-15 <-> 16-23 of every 32 (on the running index) */
            unsigned int *d = pbufClut + ((g & 8) ? 8 : 0) - ((g & 0x10) ? 8 : 0);
            g++;
            pbufClut++;
            *d &= 0xff000000u;
            *d |= ((unsigned int)res.rgba[2] << 16) | ((unsigned int)res.rgba[1] << 8) | res.rgba[0];
        }
    }
}

void GAcUtil_CalcInterColor(StColorU8 *pcolRes, StColorU8 *pcolPrev, StColorU8 *pcolNext, int pos, int max)
{
    if (pos <= 0) {
        unsigned char c0 = pcolPrev->rgba[0], c1 = pcolPrev->rgba[1], c2 = pcolPrev->rgba[2], c3 = pcolPrev->rgba[3];
        pcolRes->rgba[0] = c0; pcolRes->rgba[1] = c1; pcolRes->rgba[2] = c2; pcolRes->rgba[3] = c3;
        return;
    }
    if (!(pos < max) || max == 0) {
        unsigned char c0 = pcolNext->rgba[0], c1 = pcolNext->rgba[1], c2 = pcolNext->rgba[2], c3 = pcolNext->rgba[3];
        pcolRes->rgba[0] = c0; pcolRes->rgba[1] = c1; pcolRes->rgba[2] = c2; pcolRes->rgba[3] = c3;
        return;
    }
    unsigned int rest = (unsigned int)(max - pos), m = (unsigned int)max, ps = (unsigned int)pos;
    unsigned int v[4];
    for (int i = 0; i < 4; i++) v[i] = (pcolPrev->rgba[i] * rest + pcolNext->rgba[i] * ps) / m;
    for (int i = 0; i < 4; i++) pcolRes->rgba[i] = (unsigned char)v[i];
}

/* rotation matrix with row 2 = zvec made orthogonal to yvec (normalised, w 0), row 1 = yvec, row 0 = row 1 x row 2
 * (w = yvec.w); row 3 untouched */
void GAcUtil_ZYvec2matrix(float (*mat)[4], float *zvec, float *yvec)
{
    float zy = zvec[1] * yvec[1];
    float zx = zvec[0] * yvec[0];
    float d = (zx + zy) + zvec[2] * yvec[2];   /* adda.s / madd.s */
    float t[3] = {yvec[0] * d, yvec[1] * d, yvec[2] * d};
    float w = zvec[3];
    mat[2][0] = zvec[0] - t[0];
    mat[2][1] = zvec[1] - t[1];
    mat[2][2] = zvec[2] - t[2];
    mat[2][3] = w;
    vu_normalize(mat[2], mat[2]);
    memcpy(mat[1], yvec, 16);
    vu_cross(mat[0], mat[1], mat[2]);
    mat[0][3] = mat[1][3];
}

void GAcUtil_CopyParentModel(StModel *dst, StModel *src)
{
    dst->nowparent = src->nowparent;
    dst->ylfrm = src->ylfrm;
    dst->llistbk = src->llistbk;
    dst->llist = src->llist;
    dst->use = src->use;
}

/* ocmatp = matrix of the model's discernible vertex (z axis = -normal, position = vertex), parent = its object.
 * original: without a model the parent is IdentityMtx, without a vertex the root object (both with dummyset only) */
int GAcUtil_GetObjCmat(ObjCMatSt *ocmatp, StModel *mp, float R, int dummyset)
{
    PS2_FRAME(0x70);
    PS2_STACK(float[4], vtx, 0x60);   /* the whole qword (w may be stale stack) goes into mat[3] */
    PS2_STACK(float[4], nrm, 0x50);
    if (!mp || mp->nobj == 0) {
        if (dummyset) {
            ocmatp->parentmatp = &IdentityMtx;
            sceVu0CopyMatrix(ocmatp->mat, IdentityMtx);
        }
        return -1;
    }
    int idx = Model_GetDiscernVertex(mp, vtx, nrm, R, 0.0f, 0.0f);
    if (idx < 0) {
        if (dummyset) {
            ocmatp->parentmatp = mp->op[0].wrmatp;
            sceVu0CopyMatrix(ocmatp->mat, IdentityMtx);
        }
        return -1;
    }
    ocmatp->parentmatp = mp->op[idx].wrmatp;
    nrm[3] = 0.0f;
    nrm[0] = nrm[0] * -1.0f;
    nrm[1] = nrm[1] * -1.0f;
    nrm[2] = nrm[2] * -1.0f;
    Zvec2Matrix(ocmatp->mat, nrm);
    memcpy(ocmatp->mat[3], vtx, 16);
    return idx;
}
