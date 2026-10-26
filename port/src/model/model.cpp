/* src/model/model.cpp - model object / matrix helpers and OMD packet patching (whole file).
 * A StModel's objects (ObjectSt) each hold position / angle and a world matrix (wrmatp): local = RotX, RotZ, RotY,
 * Trans of the object, then the parent chain (animated models) and the model matrix (matrix2) on top.
 * The OMD / VU1 functions patch the model's VU1 microprogram data in place (texture base pointers, draw flags,
 * "discern" vertices marked by a vertex colour); in the source port they belong to the render detour (phase 2).
 * VU1 data layout per primitive block (32-bit words): word 0 low 28 bits = size, (size - 10) / 4 vertices of 0x40
 * bytes after a 0xa0-byte header (a vertex: position, normal, ..., colour at +0x30 with alpha 255 = marked). */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"

static const float PI_F = ps2_u2f(0x40490fdb), TWO_PI_F = ps2_u2f(0x40c90fdb);
static const float MARK_ALPHA = ps2_u2f(0x437ee666);   /* 254.9 */

/* to [-pi, pi) with one truncating division */
static inline float norm_angle(float a)
{
    float s = PI_F + a;
    float n = (float)ps2_ftoi(s / TWO_PI_F);
    return (s - TWO_PI_F * n) - PI_F;
}

/* once by a whole turn */
static inline void wrap_once(float *a)
{
    if (!(*a <= PI_F)) *a = *a - TWO_PI_F;
    if (*a < -PI_F) *a = *a + TWO_PI_F;
}

/* type 1 / 2 (map / object) models keep a bounding sphere in their list entry */
static inline bool has_sphere(StModel *mp) { return mp->type == 1 || mp->type == 2; }
static inline bool has_sphere8(StModel *mp) { return mp->type == 1 || mp->type == 2 || mp->type == 8; }

/* VU1 data block: vertex count field ((size - 10) / 4, size = low 28 bits of word 0) */
static inline int vu1_nvert(const unsigned int *p) { return ((int)(p[0] & 0xfffffffu) - 10) / 4; }

void Model_SetCflag(StModel *pModel, unsigned short cflag, int setflag)
{
    if (setflag) pModel->cflag |= cflag;
    else pModel->cflag &= (unsigned short)~cflag;
}

char *Model_ReadAnim(StModel *pm, char *pbuf, int mode) { return (char *)Anim_ReadMixPointer(pbuf, pm, mode); }

/* objects + their world / work matrices (64-byte aligned, memory pools 2 and 3) */
ObjectSt *MDL_AllocObject(int nobj)
{
    ObjectSt *op = (ObjectSt *)Memm_Alloc(1, nobj << 7, 0x10);
    ObjectSt *o = op;
    for (; nobj != 0; nobj--, o++) {
        o->wrmatp = (float (*)[4][4])Memm_Alloc(2, 0x40, 0x40);
        o->rdmatp = (float (*)[4][4])Memm_Alloc(3, 0x40, 0x40);
    }
    return op;
}

/* children (the first child and its siblings) under op's matrix */
void DF_MakeParentChain(ObjectSt *op)
{
    if (!op->child) return;
    float p[4][4];
    memcpy(p, *op->wrmatp, sizeof p);
    ObjectSt *c = op->child;
    vu_mul_matrix(*c->wrmatp, p, *c->wrmatp);
    while (c->sibling) {
        c = c->sibling;
        vu_mul_matrix(*c->wrmatp, p, *c->wrmatp);
    }
}

void DF_MakeObjectLocalMatrix(float (*mat)[4], ObjectSt *op)
{
    sceVu0RotMatrixX(mat, IdentityMtx, norm_angle(op->angle[0]));
    sceVu0RotMatrixZ(mat, mat, norm_angle(op->angle[2]));
    sceVu0RotMatrixY(mat, mat, norm_angle(op->angle[1]));
    sceVu0TransMatrix(mat, mat, op->position);
}

void DF_CALC_RotateModel_ZXY(StModel *mp, float *mang)
{
    DF_CALC_RotateModel2_ZXY(mp, mang);
    DF_MakeModelLocalMatrixZXY_Model(mp, mp->loc, mp->ang, mp->scale);
    update_model_matrix(mp);
}

/* ang += mang; the bounding sphere centre turns about loc by mang (rotation order z, x, y / x, y, z). */
static void rotate_sphere(StModel *mp, float *mang, bool zxy, bool type8)
{
    mp->ang[0] = mp->ang[0] + mang[0];
    mp->ang[1] = mp->ang[1] + mang[1];
    mp->ang[2] = mp->ang[2] + mang[2];
    float d[4];
    for (int i = 0; i < 3; i++) d[i] = mp->mlist->sphere[i] - mp->loc[i];
    d[3] = 1.0f;
    float m[4][4] __attribute__((aligned(16)));
    if (zxy) {
        sceVu0RotMatrixZ(m, IdentityMtx, norm_angle(mang[2]));
        sceVu0RotMatrixX(m, m, norm_angle(mang[0]));
        sceVu0RotMatrixY(m, m, norm_angle(mang[1]));
    } else {
        sceVu0RotMatrixX(m, IdentityMtx, norm_angle(mang[0]));
        sceVu0RotMatrixY(m, m, norm_angle(mang[1]));
        sceVu0RotMatrixZ(m, m, norm_angle(mang[2]));
    }
    vu_apply(d, m, d);
    if (type8 ? has_sphere8(mp) : has_sphere(mp)) {
        mp->mlist->sphere[0] = mp->loc[0] + d[0];
        mp->mlist->sphere[1] = mp->loc[1] + d[1];
        mp->mlist->sphere[2] = mp->loc[2] + d[2];
    }
    for (int i = 0; i < 3; i++) wrap_once(&mp->ang[i]);
}

void DF_CALC_RotateModel2_ZXY(StModel *mp, float *mang) { rotate_sphere(mp, mang, true, false); }

void DF_CALC_RotateModel(StModel *mp, float *mang)
{
    DF_CALC_RotateModel2(mp, mang);
    DF_MakeModelLocalMatrix_Model(mp, mp->loc, mp->ang, mp->scale);
    update_model_matrix(mp);
}

void DF_CALC_RotateModel2(StModel *mp, float *mang) { rotate_sphere(mp, mang, false, true); }

/* moves by mvec in model space */
void DF_CALC_MoveModel_Local(StModel *mp, float *mvec)
{
    float v[4] __attribute__((aligned(16)));
    DF_RotateMatrix(v, mp->matrix2, mvec);
    for (int i = 0; i < 3; i++) mp->loc[i] = mp->loc[i] + v[i];
    if (has_sphere(mp)) {
        mp->mlist->sphere[0] = mp->mlist->sphere[0] + v[0];
        mp->mlist->sphere[1] = mp->mlist->sphere[1] + v[1];
        mp->mlist->sphere[2] = mp->mlist->sphere[2] + v[2];
        mp->Ytop = mp->Ytop + v[1];
        mp->Ybottom = mp->Ybottom + v[1];
    }
    DF_MakeModelLocalMatrix_Model(mp, mp->loc, mp->ang, mp->scale);
    update_model_matrix(mp);
}

void DF_CALC_MoveModel(StModel *mp, float *mvec)
{
    for (int i = 0; i < 3; i++) mp->loc[i] = mp->loc[i] + mvec[i];
    if (has_sphere8(mp)) {
        mp->mlist->sphere[0] = mp->mlist->sphere[0] + mvec[0];
        mp->mlist->sphere[1] = mp->mlist->sphere[1] + mvec[1];
        mp->mlist->sphere[2] = mp->mlist->sphere[2] + mvec[2];
        mp->Ytop = mp->Ytop + mvec[1];
        mp->Ybottom = mp->Ybottom + mvec[1];
    }
    DF_MakeModelLocalMatrix_Model(mp, mp->loc, mp->ang, mp->scale);
    update_model_matrix(mp);
}

void DF_CALC_UpdateModelMatrix_COPY(StModel *mp, float (*mat)[4])
{
    sceVu0CopyMatrix(mp->matrix2, mat);
    update_model_matrix(mp);
}

void DF_CALC_UpdateModelMatrix_ZXY(StModel *mp)
{
    DF_MakeModelLocalMatrixZXY_Model(mp, mp->loc, mp->ang, mp->scale);
    update_model_matrix(mp);
}

void DF_CALC_UpdateModelMatrix(StModel *mp)
{
    DF_MakeModelLocalMatrix_Model(mp, mp->loc, mp->ang, mp->scale);
    update_model_matrix(mp);
}

/* object world matrices from the model matrix; animated models (kind bit 0) through the object tree */
void update_model_matrix(StModel *mp)
{
    if (!(mp->kind & 1)) {
        for (int i = 0; i < mp->nobj; i++) {
            ObjectSt *o = &mp->op[i];
            DF_MakeObjectLocalMatrix(*o->wrmatp, o);
            vu_mul_matrix(*mp->op[i].wrmatp, mp->matrix2, *mp->op[i].wrmatp);
        }
        return;
    }
    for (int i = 0; i < mp->nobj; i++) DF_MakeObjectLocalMatrix(*mp->op[i].wrmatp, &mp->op[i]);
    for (int i = 0; i < mp->nobj; i++) DF_MakeParentChain(&mp->op[i]);
    for (int i = 0; i < mp->nobj; i++) vu_mul_matrix(*mp->op[i].wrmatp, mp->matrix2, *mp->op[i].wrmatp);
}

/* sphere centre (map / object / type 8 models) or loc lowered by half the height */
void DF_GetModelCenter(float *center, StModel *mp)
{
    if (has_sphere8(mp)) {
        memcpy(center, mp->mlist->sphere, 16);
        center[3] = 1.0f;
    } else {
        memcpy(center, mp->loc, 16);
        center[1] = center[1] - mp->tall * 0.5f;
    }
}

/* relocates the texture / CLUT base pointers in the model's VU1 data (TBP added as is, CBP in 32-word units) */
int DF_CALC_MyACTSS(StModel *mp, int TBP, int CBP, OmdTableSt *omdtable)
{
    unsigned int *p = mp->vu1mp;
    for (int i = 0; i < mp->nobj; i++) {
        unsigned int id = mp->tod->animtable[mp->tod->nowtodid][i].tp2id;
        if (id == 0xff) continue;
        unsigned int n = omdtable[id].pnum;
        do {
            int nv = vu1_nvert(p);
            n--;
            p[0x10] += TBP;
            p[0x11] += CBP << 5;
            p += nv * 16 + 0x2c;
        } while (n);
    }
    return -1;
}

/* For each object: the first marked block (alpha > 254.9) gets its 3 header vectors averaged; then, if its
 * flags (+0xd0 / +0xd4 > 254.9) say so, the object's other blocks with > 3 vertices are flagged (bit 21 = extra
 * draw, counts toward the object's dflag) / unflagged (bit 20). */
void DF_MDL_SetDrawFlag(StModel *mp, unsigned int *vu1mp, OmdTableSt *omdtab)
{
    (void)vu1mp;
    int nflag = 0;
    for (int i = 0; i < mp->nobj; i++) {
        unsigned int id = i;
        if (mp->kind & 1) {
            id = mp->tod->animtable[mp->tod->nowtodid][i].tp2id;
            if (id == 0xff) continue;
        }
        unsigned int fl = 0;
        unsigned char *b = (unsigned char *)HOSTPTR(omdtab[id].mstart);
        for (unsigned int n = omdtab[id].pnum; n; n--) {
            if (!(*(float *)(b + 0xdc) <= MARK_ALPHA)) {
                if (!(*(float *)(b + 0xd0) <= MARK_ALPHA)) fl |= 1;
                if (!(*(float *)(b + 0xd4) <= MARK_ALPHA)) fl |= 2;
                float *h = (float *)(b + 0xa0);
                float v[4] __attribute__((aligned(16)));
                memcpy(v, h, 16);
                for (int c = 0; c < 3; c++) v[c] = v[c] + h[0x10 + c];
                for (int c = 0; c < 3; c++) v[c] = v[c] + h[0x20 + c];
                sceVu0DivVectorXYZ(v, v, 3.0f);
                memcpy(h, v, 16);
                memcpy(h + 0x10, v, 16);
                memcpy(h + 0x20, v, 16);
                break;
            }
            b += ((*(unsigned int *)b & 0xffff) + 1) * 16;
        }
        int nset = 0;
        b = (unsigned char *)HOSTPTR(omdtab[id].mstart);
        for (unsigned int n = omdtab[id].pnum; n; n--) {
            unsigned int *w = (unsigned int *)b;
            if ((w[0x20] & 0x7fff) >= 4 && *(float *)(b + 0xdc) <= MARK_ALPHA) {
                if (fl & 1) {
                    nset++;
                    w[0x21] |= 0x200000;
                }
                if (fl & 2) w[0x21] &= 0xffefffffu;
            }
            b += ((*(unsigned int *)b & 0xffff) + 1) * 16;
        }
        if (nset > 0) {
            nflag++;
            mp->op[i].dflag = 1;
        }
    }
    if (nflag > 0) mp->mflag |= 0x200;
}

/* Marked (alpha >= 254.9) blocks: the 3 header vectors become their average and the block is hidden (bit 24). */
void Model_DeleteDiscernVertex(StModel *pmodel)
{
    if (!pmodel) return;
    int nm = pmodel->nmobj;
    unsigned char *b = (unsigned char *)pmodel->vu1mp;
    OmdTableSt *omd = pmodel->omdtable;
    for (int j = 0; j < nm; j++) {
        unsigned int n = omd[j].pnum;
        do {
            int nv = vu1_nvert((unsigned int *)b);
            unsigned int *flag = (unsigned int *)(b + 0x84);
            b += 0xa0;
            if (!(*(float *)(b + 0x3c) < MARK_ALPHA)) {
                float *h = (float *)b;
                float v[4] __attribute__((aligned(16)));
                memcpy(v, h, 16);
                for (int c = 0; c < 3; c++) v[c] = v[c] + h[0x10 + c];
                for (int c = 0; c < 3; c++) v[c] = v[c] + h[0x20 + c];
                sceVu0DivVectorXYZ(v, v, 3.0f);
                memcpy(h, v, 16);
                memcpy(h + 0x10, v, 16);
                memcpy(h + 0x20, v, 16);
                *flag |= 0x1000000;
            }
            b += nv * 0x40 + 0x10;
        } while (--n);
    }
}

/* First marked vertex (alpha > 254.9) with colour (R, G, B) +- 0.5 on a visible object: loc = the average of its
 * 3 position vectors, norm = its normal; returns the object index or -1. */
int Model_GetDiscernVertex(StModel *pmodel, float *loc, float *norm, float R, float G, float B)
{
    if (!pmodel) return -1;
    OmdTableSt *omd = pmodel->omdtable;
    unsigned char *b = (unsigned char *)pmodel->vu1mp;
    const float half = 0.5f;
    float rhi = half + R, rlo = R - half, ghi = half + G, glo = G - half, bhi = half + B, blo = B - half;
    for (int i = 0; i < pmodel->nobj; i++) {
        unsigned int id = i;
        if (pmodel->kind & 1) {
            id = pmodel->tod->animtable[pmodel->tod->nowtodid][i].tp2id;
            if (id == 0xff) continue;
        }
        unsigned int n = omd[id].pnum;
        if (pmodel->op[i].onoff) {   /* hidden object: skip its blocks */
            do {
                int nv = vu1_nvert((unsigned int *)b);
                b += 0xa0 + nv * 0x40 + 0x10;
            } while (--n);
            continue;
        }
        do {
            int nv = vu1_nvert((unsigned int *)b);
            b += 0xa0;
            for (; nv; nv--, b += 0x40) {
                float *v = (float *)b;
                if (!(v[0xf] <= MARK_ALPHA) && v[0xc] < rhi && !(v[0xc] <= rlo) && v[0xd] < ghi && !(v[0xd] <= glo) &&
                    v[0xe] < bhi && !(v[0xe] <= blo)) {
                    memcpy(loc, v, 16);
                    for (int c = 0; c < 3; c++) loc[c] = loc[c] + v[0x10 + c];
                    for (int c = 0; c < 3; c++) loc[c] = loc[c] + v[0x20 + c];
                    sceVu0DivVectorXYZ(loc, loc, 3.0f);
                    memcpy(norm, v + 4, 16);
                    return i;
                }
            }
            b += 0x10;
        } while (--n);
    }
    return -1;
}

/* models drawn with the OPC (optical camouflage) look: blend 100, alpha-blended GIF tag, bits 20/23 off */
void DF_MDL_ChangeforOPC(StModel *mp)
{
    mp->use |= 0x2000;
    mp->blend2 = 100;
    int nm = mp->nmobj;
    OmdTableSt *omd = mp->omdtable;
    for (int j = 0; j < nm; j++, omd++) {
        unsigned char *b = (unsigned char *)HOSTPTR(omd->mstart);
        for (unsigned int n = omd->pnum; n; n--) {
            unsigned int step = (*(unsigned short *)b + 1u) * 16;
            *(uint64_t *)(b + 0x60) = 0x8000000a20213b40ull;
            *(unsigned int *)(b + 0x94) &= 0xff6fffffu;
            uint64_t *t = (uint64_t *)(b + 0x88);
            if (*t & 0x100000000ull) *t = *t + 0x100000000ull;
            b += step;
        }
    }
}

/* VU1 program call words of each block: the first one gets "offset", the others 0x17000000 */
void DF_VU1ModelCallFChange(unsigned int *vu1mp, OmdTableSt *omdtable, int nobj, int offset)
{
    for (int i = 0; i < nobj; i++, omdtable++) {
        unsigned int n = omdtable->pnum;
        int first = 1;
        do {
            int nv = vu1_nvert(vu1mp);
            vu1mp[nv * 16 + 0x2b] = first ? (unsigned int)offset | 0x14000000u : 0x17000000u;
            first = 0;
            n--;
            vu1mp += nv * 16 + 0x2c;
        } while (n);
    }
}

/* PRIM bits of every block: clear cls, set set (at bit 15 of the word at +0x84) */
void DF_VU1ModelPrimChange(OmdTableSt *omdtab, int nmobj, unsigned int cls, unsigned int set)
{
    if (!omdtab || nmobj <= 0) return;
    for (int i = 0; i < nmobj; i++, omdtab++) {
        if (!omdtab) continue;
        unsigned char *b = (unsigned char *)HOSTPTR(omdtab->mstart);
        for (unsigned int n = omdtab->pnum; n; n--) {
            unsigned int *w = (unsigned int *)(b + 0x84);
            *w &= ~(cls << 15);
            *w |= set << 15;
            b += (*(unsigned short *)b + 1u) * 16;
        }
    }
}

/* places the model (any of loc / ang / scl may be 0 = keep) and moves its bounding sphere / height range along */
void DF_MDL_SetModel(StModel *mp, float *loc, float *ang, float *scl)
{
    float dy = 0.0f;
    if (loc) {
        dy = loc[1] - mp->loc[1];
        memcpy(mp->loc, loc, 16);
    }
    if (ang) memcpy(mp->ang, ang, 16);
    if (scl) memcpy(mp->scale, scl, 16);
    DF_MakeModelLocalMatrixZXY_Model(mp, mp->loc, mp->ang, mp->scale);
    update_model_matrix(mp);
    float c[4] = {mp->localcenter[0], mp->localcenter[1], mp->localcenter[2], 1.0f};   /* (w: vf0.w) */
    vu_apply(c, mp->matrix2, c);
    if (has_sphere8(mp)) {
        mp->mlist->sphere[0] = c[0];
        mp->mlist->sphere[1] = c[1];
        mp->mlist->sphere[2] = c[2];
        mp->Ytop = mp->Ytop + dy;
        mp->Ybottom = mp->Ybottom + dy;
    }
}
