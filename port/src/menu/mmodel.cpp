/* src/menu/mmodel.cpp — the menu's 3D model manager (OLMENU). MModelTopp is a pool of 128 _stMModel records
 * (0x110 bytes each, DF_MMODEL_Init). A model wraps one MMD data file (mmdp) plus a per-model copy of its object
 * array (mdlp[k].objp, one group per sub-model). DF_MMODEL_Play computes the model matrix and every object matrix
 * (static pose, or the MI2 motion through __PlayAnim / __PlayAnimObject and the cubic-spline interpolator),
 * DF_MMODEL_UpdateMatrix keeps the previous frame's object matrices and clears the per-frame flags, DF_MMODEL_Update
 * frees the models / MMDs / colour tables flagged for deletion. The callers are m_ac.c (the garage AC), mobj.c
 * (model objects) and the other OLMENU screens.
 *
 * All native: the model pool and flags, the per-frame transforms, the colour-table set-up and (phase 2 group 5a)
 * the packet / display-list builders: DF_MMODEL_MakeVu1PacketAC (the VIF1 chain of one model: ALPHA_2, textures /
 * CLUTs, TEX0, light matrices, fog / alpha / test registers, then per object its matrix + a call of its display
 * list), __MakeTM2ClutPacket(AC), and DF_MMODEL_SetShadow / SetVertColor, which patch the vertex colours of a
 * private copy of the display lists made by DF_MMODEL_CopyOMDTable. */
#include <string.h>
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"

#ifdef ACNB_SOURCEPORT
float pl_pos[4];   /* never written: MakeVu1PacketAC copies four zero quads of each into every packet */
float pl_col[4];
#endif

#define MMODEL_NUM 128

/* the MMD of a model, read through without a NULL test in the original (address 0 when mmdp is NULL) */
static inline const stMmdData *mmodel_mmd_ref(const stMmdData *m)
{
    return m ? m : (const stMmdData *)PS2_NULL_MEM();
}

/* GetNewModel's / ReleaseModel's initialisation of a claimed record */
static void mmodel_reset(_stMModel *mp)
{
    DF_MFUNC_ClearMemory(mp, 0x44);
    mp->addST[0] = 0.0f;
    mp->addST[1] = 0.0f;
    mp->mulalpha = 1.0f;
    mp->flag.use = 1;
    mp->flag.draw = 1;
    mp->flag.envball = 1;
    mp->flag.tm2_trans = 1;
    mp->ld_matp = &MLightMng->dir_mat;
    mp->lc_matp = &MLightMng->col_mat;
    mp->alpha_reg = 0x44;
    mp->test_reg = 0x7000f;
}

/* frees a 12-entry colour table (SetPartsCluts' copies) and the table itself */
static void mmodel_free_cluts(stMmdImage *c)
{
    if (!c) return;
    for (int i = 0; i < 12; i++)
        if (c[i].datap) {
            DF_MMM_Free(0, c[i].datap);
            c[i].datap = 0;
        }
    DF_MMM_Free(1, c);
}

void DF_MMODEL_Play(_stMModel *ioMp)
{
    PS2_FRAME(0xa0);
    if (!ioMp) return;
    if (ioMp->flag.played == 1) return;
    ioMp->flag.played = 1;
    if (ioMp->flag.calc_objmat == 1) return;
    if (ioMp->flag.f_nocalcmat == 1) return;
    if (!ioMp->flag.calc_lwmat) {
        float (*m)[4] = ioMp->local_world_mat;
        const float *s = ioMp->scale;
        /* vmul.xyz of the identity rows by scale (w lanes stay the identity's) */
        for (int i = 0; i < 3; i++) {
            m[i][0] = IdentityMtx[i][0] * s[0];
            m[i][1] = IdentityMtx[i][1] * s[1];
            m[i][2] = IdentityMtx[i][2] * s[2];
            m[i][3] = IdentityMtx[i][3];
        }
        memcpy(m[3], IdentityMtx[3], 16);
        if (ioMp->flag.ay == 1) DF_MFUNC_RotMatrix(m, m, ioMp->ang);
        else DF_MFUNC_RotMatrixOffY(m, m, ioMp->ang);
        sceVu0TransMatrix(m, m, ioMp->pos);
        ioMp->flag.calc_lwmat = 1;
    }
    struct_45c47ec5 *g = ioMp->mdlp;
    if (!g || !ioMp->mmdp) return;
    alignas(16) float lwm[4][4];
    sceVu0CopyMatrix(lwm, ioMp->local_world_mat);
    for (int k = 0; k < ioMp->mmdp->OMD.num; k++, g++) {
        if (!g->f_draw) continue;
        __mmodel_object_st *o = g->objp;
        if (!o) continue;
        if (g->anim.f_play == 1) {
            __PlayAnim(g, lwm);
            /* the animated group's root matrix becomes the parent of the following groups */
            sceVu0CopyMatrix(lwm, g->objp->local_world_mat);
        } else {
            for (int j = 0; j < g->nobj; j++, o++) {
                DF_MFUNC_RotMatrix(o->local_world_mat, IdentityMtx, o->ang);
                sceVu0TransMatrix(o->local_world_mat, o->local_world_mat, o->pos);
                vu_mul_matrix(o->local_world_mat, (const float (*)[4])ioMp->local_world_mat,
                              (const float (*)[4])o->local_world_mat);
            }
        }
    }
    ioMp->flag.calc_objmat = 1;
}

static int __PlayAnim(struct_45c47ec5 *ioMdlp, float (*inLW_Mat)[4])
{
    PS2_FRAME(0x20);
    if (ioMdlp->anim.f_finish == 1) return 0;
    if (!ioMdlp->anim.ahp) return -1;
    _obj_motion *node = (_obj_motion *)((char *)ioMdlp->anim.ahp + 0x10);
    if (ioMdlp->anim.f_construct && node->tp2id == 0xffff) node++;
    __PlayAnimObject(node, node, ioMdlp->objp, inLW_Mat, ioMdlp->anim.frame, ioMdlp->nobj);
    unsigned frame = (unsigned)++ioMdlp->anim.frame;
    if (ioMdlp->anim.ahp->frame < frame) {
        if (ioMdlp->anim.f_loop == 1) ioMdlp->anim.frame = 0;
        else {
            ioMdlp->anim.frame = (int)ioMdlp->anim.ahp->frame;
            ioMdlp->anim.f_finish = 1;
        }
    }
    return 0;
}

static void __PlayAnimObject(_obj_motion *inTopAtp, _obj_motion *inAtp, __mmodel_object_st *ioTopObjp,
                             float (*inParentMat)[4], int inFrame, int inObjMax)
{
    PS2_FRAME(0xb0);
    if (!ioTopObjp) return;
    int idx = (int)((char *)inAtp - (char *)inTopAtp) / 0x40;
    if (idx >= inObjMax) return;
    __mmodel_object_st *o = ioTopObjp + idx;
    if (inFrame == 0) o->tod_offset = 0;
    if (inAtp->tp2id == 0xffff || inAtp->nkey_tod == 0) {
        memcpy(o->ang, (char *)o->tp2_otp + 0x10, 16);
        memcpy(o->pos, o->tp2_otp, 16);
    } else {
        struct_3c2ab559 *s2 = inAtp->tod_data + o->tod_offset, *s1;
        if (inFrame < s2->key) s1 = s2;
        else
            for (;;) {
                int next = o->tod_offset + 1;
                if (next >= inAtp->nkey_tod) { s1 = s2; break; }
                if (inFrame < s2[1].key) { s1 = s2 + 1; break; }
                o->tod_offset = (unsigned char)next;
                s2++;
            }
        float t = ps2_div((float)(inFrame - s2->key), (float)s2->nframe);
        Anim_CubicSpline(o->ang, s2->angle, s1->angle, s2->ang_outtan, s1->ang_intan, t);
        Anim_CubicSpline(o->pos, s2->position, s1->position, s2->pos_outtan, s1->pos_intan, t);
    }
    DF_MFUNC_RotMatrix(o->local_world_mat, IdentityMtx, o->ang);
    sceVu0TransMatrix(o->local_world_mat, o->local_world_mat, o->pos);
    vu_mul_matrix(o->local_world_mat, (const float (*)[4])inParentMat, (const float (*)[4])o->local_world_mat);
    if (inAtp->child)
        __PlayAnimObject(inTopAtp, inAtp->child, ioTopObjp, o->local_world_mat, inFrame, inObjMax);
    if (inAtp->sibling)
        __PlayAnimObject(inTopAtp, inAtp->sibling, ioTopObjp, inParentMat, inFrame, inObjMax);
}

/* back-mounted cannon: tests the first byte of the OTH flag data (not a field of infop) */
int DF_MMODEL_IsBackCannon(_stMModel *inMp)
{
    PS2_FRAME(0x20);
    unsigned char *d = 0;
    if (inMp) d = (unsigned char *)DF_MDATA_GetData(mmodel_mmd_ref(inMp->mmdp)->OTH.datap, 1);
    if (!inMp || !d) return 0;
    if (inMp->kind != 1) return 0;
    if (inMp->id / 1000 != 10) return 0;
    int r = 0;
    if (inMp->prm.part.infop && *d == 2) r = 1;
    return r;
}

int DF_MMODEL_IsWBackWeapon(_stMModel *inMp)
{
    PS2_FRAME(0x20);
    if (!inMp) return 0;
    if (inMp->kind != 1) return 0;
    if (inMp->id / 1000 != 10) return 0;
    int r = 0;
    if (inMp->prm.part.infop && DF_MPART_GetCategory(inMp->id) >= 0x13) r = 1;
    return r;
}

char *DF_MMODEL_GetPartsName(_stMModel *inMp)
{
    if (!inMp) return 0;
    if (inMp->kind != 1) return 0;
    return (char *)inMp->prm.part.infop;
}

void DF_MMODEL_Select(_stMModel *ioMp, int inFlag, int inLightNum)
{
    if (!ioMp || ioMp->kind != 1) return;
    ioMp->flag.select = inFlag & 1;
    ioMp->ld_matp = &MLightMng[inLightNum].dir_mat;
    ioMp->lc_matp = &MLightMng[inLightNum].col_mat;
    if (inFlag == 1) {
        ioMp->mulalpha = 0.5f;
        ioMp->flag.envball = 0;
        ioMp->flag.see_through = 1;
        ioMp->alpha_reg = 0x48;
        ioMp->test_reg = 0x5180b;
    } else {
        ioMp->mulalpha = 1.0f;
        ioMp->flag.envball = 1;
        ioMp->flag.see_through = 0;
        ioMp->alpha_reg = 0x44;
        ioMp->test_reg = 0x7000f;
    }
}

/* counts every used, non-erased model - including one that is being released (see ReleaseModel) */
int DF_MMODEL_IsMMDUsing(stMmdData *inMmdp)
{
    if (!inMmdp) return 0;
    if (inMmdp->id / 1000 == 0xf) return 1;
    for (_stMModel *mp = MModelTopp, *end = MModelTopp + MMODEL_NUM; mp < end; mp++)
        if (mp->flag.use == 1 && !mp->flag.erase && mp->mmdp == inMmdp) return 1;
    return 0;
}

/* outVert gets the data polygon's vertex in world space; its w lane is whatever DF_MMD_GetDataPoly left in the
 * stack quad (m_ac copies it into matrix row 3) */
int DF_MMODEL_GetDataPoly(_stMModel *inMp, int inNum, int inID, float *outVert, float *outNorm)
{
    PS2_FRAME(0x70);
    if (!inMp) return -1;
    if (!inMp->flag.calc_objmat) return -1;
    if (!inMp->mmdp) return -1;
    PS2_STACK(float[4], v, 0x60);
    PS2_STACK(float[4], n, 0x50);
    int r = DF_MMD_GetDataPoly(inMp->mmdp, inNum, inID, v, n);
    if (r < 0) return -2;
    const float (*M)[4] = (const float (*)[4])inMp->mdlp[inNum].objp[r].local_world_mat;
    vu_apply(outVert, M, v);
    n[3] = 0.0f;
    vu_apply(outNorm, M, n);
    return r;
}

void DF_MMODEL_SetWepColor(_stMModel *ioMp, struct_c4910010 *pColor)
{
    if (!ioMp || !pColor) return;
    if (!ioMp->mmdp || !ioMp->mdlp || !ioMp->flag.set_mdl) return;
    if (ioMp->mmdp->TM2.num <= 0) return;
    unsigned char v = pColor->wepcol;
    int k = ioMp->id / 1000;
    if (v >= 6) v = 5;
    for (int i = 0; i < 24; i++) ioMp->f_tm2off[i] = 1;
    switch (k) {
    case 12:
    case 11: ioMp->f_tm2off[v] = 0; break;
    case 10:
        ioMp->f_tm2off[v * 2] = 0;
        ioMp->f_tm2off[v * 2 + 1] = 0;
        break;
    }
}

/* copies the CLUTs of the first 12 textures and recolours them; the previous table is kept for Update to free */
void DF_MMODEL_SetPartsCluts(_stMModel *ioMp, struct_c4910010 *inPColp, struct_c4910010 *inPColp2)
{
    PS2_FRAME(0xb0);
    if (!ioMp || !inPColp) return;
    if (!ioMp->mmdp || !ioMp->mdlp || !ioMp->flag.set_mdl) return;
    if (ioMp->mmdp->TM2.num <= 0) return;
    if (ioMp->prm.part.clutp) {
        ioMp->prm.part.clutp2 = ioMp->prm.part.clutp;
        ioMp->flag.del_clut2 = 1;
    }
    ioMp->prm.part.clutp = (stMmdImage *)DF_MMM_Alloc(1, 0xc0);
    DF_MFUNC_ClearMemory(ioMp->prm.part.clutp, 0x30);
    int k = ioMp->id / 1000, fp;
    switch (k) {
    case 3: fp = 2; break;
    case 2: fp = 4; break;
    case 1:
    case 0: fp = 1; break;
    default: fp = 4; break;
    }
    for (int i = 0; i < ioMp->mmdp->TM2.num && i < 12; i++) {
        stMmdImage *src = &ioMp->mmdp->TM2.tm2datap[i].clut_img;
        stMmdImage *slot = &ioMp->prm.part.clutp[i];
        slot->size = src->size;
        slot->nloop = src->nloop;
        if (src->datap && src->size) {
            slot->datap = (unsigned int *)DF_MMM_Alloc(0, src->size);
            DF_MFUNC_CopyMemory(slot->datap, src->datap, src->size >> 2);
            slot->imagep = slot->datap + 0x20;
        }
        if (ioMp->kind == 1 && k == 2) DF_MFUNC_MakeCluts(slot->imagep, i % fp < 2 ? inPColp : inPColp2);
        else DF_MFUNC_MakeCluts(slot->imagep, inPColp);
    }
    ioMp->flag.clut = 1;
}

void DF_MMODEL_SetAnim(_stMModel *ioMp, int inMdlNum, int inAnimNum, int inLoop, int inConst)
{
    PS2_FRAME(0x40);
    if (!ioMp) return;
    stMmdData *mmd = ioMp->mmdp;
    if (!mmd) return;
    if (inMdlNum >= mmd->OMD.num || inAnimNum >= mmd->MI2.num) return;
    struct_45c47ec5 *g = &ioMp->mdlp[inMdlNum];
    g->anim.frame = 0;
    g->anim.ahp = (struct_e6319a72 *)DF_MDATA_GetData(ioMp->mmdp->MI2.datap, inAnimNum);
    g->anim.f_play = 1;
    g->anim.f_finish = 0;
    g->anim.f_loop = inLoop & 1;
    g->anim.f_construct = inConst & 1;
    __mmodel_object_st *o = g->objp;
    for (unsigned j = 0; j < g->omdp->nobj; j++, o++) o->tod_offset = 0;
}

/* builds the per-model group / object copies; set_mdl only when the MMD has sub-models */
void DF_MMODEL_SetData(_stMModel *ioMp, stMmdData *inMmdp, float (*inPos)[4], float (*inAng)[4],
                       float (*inScale)[4])
{
    PS2_FRAME(0x50);
    if (!ioMp || !inMmdp) return;
    ioMp->kind = inMmdp->kind;
    ioMp->id = inMmdp->id;
    ioMp->mmdp = inMmdp;
    if (inPos) memcpy(ioMp->pos, inPos, 16);
    if (inAng) memcpy(ioMp->ang, inAng, 16);
    if (inScale) memcpy(ioMp->scale, inScale, 16);
    if (!inMmdp->OMD.datap) return;
    int size = inMmdp->OMD.num * 0x30;
    if (size) {
        ioMp->mdlp = (struct_45c47ec5 *)DF_MMM_Alloc(1, size);
        ioMp->mdl_num = inMmdp->OMD.num;
        DF_MFUNC_ClearMemory(ioMp->mdlp, size >> 2);
        struct_45c47ec5 *g = ioMp->mdlp;
        for (int k = 0; k < ioMp->mdl_num; k++, g++) {
            g->omdp = (struct_38528994 *)DF_MDATA_GetData(ioMp->mmdp->OMD.datap, k);
            g->otp = (OmdTableSt *)((char *)g->omdp + 0x10);
            g->f_draw = 1;
            g->anim.f_finish = 1;
            unsigned nobj = g->omdp->nobj;
            g->nobj = (short)nobj;
            int osize = (int)(nobj * 0xf0);
            g->objp = (__mmodel_object_st *)DF_MMM_Alloc(1, osize);
            DF_MFUNC_ClearMemory(g->objp, osize >> 2);
            __mmodel_object_st *o = g->objp;
            char *tp = (char *)g->omdp + g->omdp->nobj * 0x10 + 0x10;   /* object records after the table */
            for (unsigned j = 0; j < g->omdp->nobj; j++, o++, tp += 0x50) {
                o->tp2_otp = (_objtbl *)tp;
                memcpy(o->pos, tp, 16);
                memcpy(o->ang, tp + 0x10, 16);
                o->f_draw = 1;
            }
        }
        ioMp->flag.set_mdl = 1;
    }
    ioMp->flag.init = 1;
    if (ioMp->kind == 2) ioMp->flag.envball = 0;
    else if (ioMp->kind == 1) __SetDataACPart(ioMp);
}

/* trims trailing blanks of the two name fields (no lower bound: an all-blank field runs before the record) */
static void __SetDataACPart(_stMModel *ioMp)
{
    PS2_FRAME(0x20);
    ioMp->prm.part.infop = DF_MDATA_GetData(ioMp->mmdp->OTH.datap, 0);
    if (ioMp->id / 1000 == 8) ioMp->f_tm2off[1] = 1;
    char *info = (char *)ioMp->prm.part.infop;
    if (!info) return;
    if (info[0x11] == ' ') {
        char *p = info + 0x11;
        do *p-- = 0; while (*p == ' ');
    }
    if (info[0x23] == ' ') {
        char *p = info + 0x23;
        do *p-- = 0; while (*p == ' ');
    }
}

/* IsMMDUsing still counts this model (use set, erase clear), so the MMD is never deleted here */
void DF_MMODEL_ReleaseModel(_stMModel *ioMp)
{
    PS2_FRAME(0x50);
    if (!ioMp) return;
    struct_45c47ec5 *g = ioMp->mdlp;
    stMmdData *mmd = ioMp->mmdp;
    if (g) {
        for (int k = 0; k < mmodel_mmd_ref(ioMp->mmdp)->OMD.num; k++, g++)
            if (g->objp) DF_MMM_Free(1, g->objp);
        DF_MMM_Free(1, ioMp->mdlp);
        ioMp->mdlp = 0;
    }
    if (!DF_MMODEL_IsMMDUsing(mmd)) {
        DF_MMD_DeleteMMD(mmd);
        ioMp->mmdp = 0;
    }
    mmodel_reset(ioMp);
}

void DF_MMODEL_DeleteModelAndMMD(_stMModel *ioMp)
{
    PS2_FRAME(0x20);
    if (!ioMp) return;
    stMmdData *mmd = ioMp->mmdp;
    ioMp->flag.erase = 1;
    ioMp->flag.draw = 0;
    if (!DF_MMODEL_IsMMDUsing(mmd)) ioMp->flag.del_mmd = 1;
}

void DF_MMODEL_DeleteModel(_stMModel *ioMp)
{
    if (!ioMp) return;
    ioMp->flag.erase = 1;
    ioMp->flag.draw = 0;
}

/* the first free record after inMp (or from the start of the pool), so the caller orders its models */
_stMModel *DF_MMODEL_GetNewModel(_stMModel *inMp)
{
    PS2_FRAME(0x30);
    _stMModel *p = inMp ? inMp + 1 : MModelTopp;
    for (_stMModel *end = MModelTopp + MMODEL_NUM; p < end; p++)
        if (!p->flag.use) {
            mmodel_reset(p);
            return p;
        }
    return 0;
}

/* frees what was flagged during the frame: old colour tables, unused MMDs, erased models, replaced display lists
 * (mmdp is cleared even when the MMD is still used) */
void DF_MMODEL_Update(void)
{
    PS2_FRAME(0x60);
    for (_stMModel *mp = MModelTopp, *end = MModelTopp + MMODEL_NUM; mp < end; mp++) {
        if (mp->flag.del_clut2 == 1) {
            mmodel_free_cluts(mp->prm.part.clutp2);
            mp->prm.part.clutp2 = 0;
            mp->flag.del_clut2 = 0;
        }
        if (mp->flag.del_mmd == 1) {
            if (!DF_MMODEL_IsMMDUsing(mp->mmdp)) DF_MMD_DeleteMMD(mp->mmdp);
            mp->mmdp = 0;
            mp->flag.del_mmd = 0;
        }
        if (mp->flag.erase == 1) __RemoveModel(mp);
        struct_45c47ec5 *g = mp->mdlp;
        if (!g) continue;
        for (int k = 0; k < mmodel_mmd_ref(mp->mmdp)->OMD.num; k++, g++) {
            if (g->f_del_otp2 == 1) {
                g->f_del_otp2 = 0;
                DF_MMM_Free(0, g->otp2);
                g->otp2 = 0;
            }
            if (g->f_del_shadow2 == 1) {
                g->f_del_shadow2 = 0;
                DF_MMM_Free(0, g->s_otp2);
                g->s_otp2 = 0;
            }
        }
    }
}

/* keeps the previous frame's object matrices (drawn models only) and clears every model's per-frame flags */
void DF_MMODEL_UpdateMatrix(void)
{
    PS2_FRAME(0x70);
    for (_stMModel *mp = MModelTopp, *end = MModelTopp + MMODEL_NUM; mp < end; mp++) {
        struct_45c47ec5 *g = mp->mdlp;
        if (mp->flag.use && g && mp->flag.draw) {
            for (int k = 0; k < mmodel_mmd_ref(mp->mmdp)->OMD.num; k++, g++) {
                __mmodel_object_st *o = g->objp;
                for (int j = 0; j < g->nobj; j++, o++) sceVu0CopyMatrix(o->local_world_mat2, o->local_world_mat);
            }
        }
        mp->flag.calc_objmat = 0;
        mp->flag.calc_lwmat = 0;
        mp->flag.ay = 0;
        mp->flag.played = 0;
    }
}

/* frees everything the model owns (the group flags are not cleared) and returns the record to the pool */
static void __RemoveModel(_stMModel *ioMp)
{
    PS2_FRAME(0x50);
    if (ioMp->flag.del_clut2 == 1) {
        mmodel_free_cluts(ioMp->prm.part.clutp2);
        ioMp->prm.part.clutp2 = 0;
        ioMp->flag.del_clut2 = 0;
    }
    if (ioMp->flag.clut == 1) {
        mmodel_free_cluts(ioMp->prm.part.clutp);
        ioMp->prm.part.clutp = 0;
        ioMp->flag.clut = 0;
    }
    struct_45c47ec5 *g = ioMp->mdlp;
    if (g) {
        for (int k = 0; k < ioMp->mdl_num; k++, g++) {
            if (g->f_otp == 1) DF_MMM_Free(0, g->otp);
            if (g->f_shadow == 1) DF_MMM_Free(0, g->s_otp);
            if (g->f_del_otp2 == 1) DF_MMM_Free(0, g->otp2);
            if (g->f_del_shadow2 == 1) DF_MMM_Free(0, g->s_otp2);
            if (g->objp) DF_MMM_Free(1, g->objp);
        }
        DF_MMM_Free(1, ioMp->mdlp);
        ioMp->mdlp = 0;
    }
    ioMp->flag.erase = 0;
    ioMp->flag.use = 0;
}

unsigned char *DF_MMODEL_Init(unsigned char *ioBuff)
{
    PS2_FRAME(0x20);
    _stMModel *top = (_stMModel *)DF_MFUNC_Aligned(ioBuff, 0x40);
    MModelTopp = top;
    DF_MFUNC_ClearMemory(MModelTopp, 0x2200);
    return (unsigned char *)top + 0x8800;
}

/* ---- packet / display-list builders (phase 2) ---- */

/* clutp read without a NULL test in the original (address 0 when NULL) */
static inline const stMmdImage *mmodel_clut_ref(const stMmdImage *c)
{
    return c ? c : (const stMmdImage *)PS2_NULL_MEM();
}

/* textures and CLUTs of an AC part: the k = id / 1000 part class picks how many slots it has (1, 1, 4, 2) and the
 * camouflage flag of that class the second set of slots (only when the MMD holds both sets) */
static unsigned int *__MakeTM2ClutPacketAC(unsigned int *ioDmap, _stMModel *inMp, stMenuAC *ACp)
{
    stMmdData *mmdp = inMp->mmdp;
    int tm2num = mmdp->TM2.num;
    int num = 0;
    if (tm2num == 0 || mmdp->f_use == 0) return ioDmap;
    int k = inMp->id / 1000;
    if (k < 0 || k >= 4) return ioDmap;
    switch (k) {
    case 0: num = 1; break;
    case 1: num = 1; break;
    case 2: num = 4; break;
    case 3: num = 2; break;
    }
    int base = 0;
    if (!(tm2num < 2 * num)) base = (ACp->camouflage[k] != 0) * num;
    for (int i = 0; i < num; i++) {
        int index = base + i;
        if (inMp->f_tm2off[index] == 1) continue;
        const stMmdImage *clut_imgp = mmodel_clut_ref(inMp->prm.part.clutp) + index;
        unsigned int *tex = mmdp->TM2.tm2datap[index].tex_img.datap;
        if (tex) ioDmap = DF_MPKT_MakeCall(ioDmap, tex);
        if (clut_imgp->datap) ioDmap = DF_MPKT_MakeCall(ioDmap, clut_imgp->datap);
    }
    return DF_MPKT_MakeGSRegister(ioDmap, 0x3f /* TEXFLUSH */, 0xffffffffULL);
}

/* textures and CLUTs of every TM2 slot that is not switched off */
static unsigned int *__MakeTM2ClutPacket(unsigned int *ioDmap, _stMModel *inMp)
{
    stMmdData *mmdp = inMp->mmdp;
    if (mmdp->TM2.num == 0 || mmdp->f_use == 0) return ioDmap;
    stMmdTm2Data *tm2datap = mmdp->TM2.tm2datap;
    const stMmdImage *clut_imgp = mmodel_clut_ref(inMp->prm.part.clutp);
    for (int i = 0; i < mmdp->TM2.num; i++, tm2datap++, clut_imgp++) {
        if (inMp->f_tm2off[i] == 1) continue;
        if (tm2datap->tex_img.datap) ioDmap = DF_MPKT_MakeCall(ioDmap, tm2datap->tex_img.datap);
        if (clut_imgp->datap) ioDmap = DF_MPKT_MakeCall(ioDmap, clut_imgp->datap);
    }
    return DF_MPKT_MakeGSRegister(ioDmap, 0x3f /* TEXFLUSH */, 0xffffffffULL);
}

static inline void mmodel_put64(unsigned int *p, unsigned long long v) { memcpy(p, &v, 8); }

/* The VIF1 chain that draws one model with light / fog record inID (MLightMng[inID] and the 0x30-byte record inID
 * of menu data table 8). The fog record is read before the NULL / flag tests; each object's draw uses its FIRST
 * matrix (local_world_mat2, the previous frame's copy); pl_pos / pl_col are never written (zero quads). */
unsigned int *DF_MMODEL_MakeVu1PacketAC(unsigned int *ioDmap, _stMModel *ioMp, stMenuAC *ACp, int inID)
{
    PS2_FRAME(0xc0);
    const unsigned char *fogp = (const unsigned char *)DF_MDATA_GetData(MenuMng.dataTbl[0], 8) + inID * 0x30;
    alignas(16) unsigned int fog_dist[4] = {0, 0, 0, 0};
    memcpy(&fog_dist[2], fogp + 0x28, 8);
    if (!ioMp) return ioDmap;
    if (!ioMp->flag.draw || !ioMp->flag.set_mdl || ioMp->flag.erase == 1 || !ioMp->flag.use) return ioDmap;

    float env_a = ioMp->flag.envball == 1 ? 64.0f * ioMp->mulalpha : 0.0f;
    if (ACp) env_a = env_a * ACp->env_alpha;
    unsigned int *dmap = DF_MPKT_MakeGSRegister(ioDmap, 0x43 /* ALPHA_2 */,
                                                (unsigned long long)(unsigned int)ps2_ftoi(env_a) << 32 | 0x68);
    if (ioMp->flag.tm2_trans == 1) {
        if (ioMp->flag.clut == 1 && ioMp->flag.see_through == 0) {
            int k = ioMp->id / 1000;
            if (ACp && k >= 0 && k < 4) dmap = __MakeTM2ClutPacketAC(dmap, ioMp, ACp);
            else dmap = __MakeTM2ClutPacket(dmap, ioMp);
        } else {
            dmap = DF_MMD_MakeTM2Packet(dmap, ioMp->mmdp, &ioMp->f_tm2off[0]);
        }
    }
    dmap = DF_MPKT_MakeGSRegister(dmap, 6 /* TEX0_1 */, 0x4000000045304000ULL);
    dmap = DF_MPKT_MakeGSRegister(dmap, 6, 0x6000000045304000ULL);
    dmap = DF_MPKT_MakeGSRegister(dmap, 7 /* TEX0_2 */, 0x4000000045304000ULL);
    dmap = DF_MPKT_MakeGSRegister(dmap, 7, 0x6000000045304000ULL);

    unsigned char kind = ioMp->mmdp->kind;
    if (kind == 1 || kind == 3) {
        float unscale = ps2_div(1.0f, ioMp->scale[0]);
        alignas(16) float mat[4][4];
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) mat[i][j] = unscale * MLightMng[inID].dir_mat[i][j];
        dmap = DF_MPKT_MakeUnpack(dmap, 0xc, 4, mat);
    } else {
        dmap = DF_MPKT_MakeUnpackRef(dmap, 0xc, 4, MLightMng[inID].dir_mat);
    }
    dmap = DF_MPKT_MakeUnpackRef(dmap, 0x10, 4, MLightMng[inID].col_mat);

    /* cnt 6: UNPACK 5 quads to 0x14 = fog quad + GIF tag (A+D x3) FOGCOL / ALPHA_1 / TEST_1 */
    dmap[0] = 0x10000006; dmap[1] = 0; dmap[2] = 0; dmap[3] = 0;
    dmap[4] = 0; dmap[5] = 0; dmap[6] = 0; dmap[7] = 0x6c050014;
    memcpy(&dmap[8], fog_dist, 16);
    dmap[12] = 0x8003; dmap[13] = 0x10004000; dmap[14] = 0xe; dmap[15] = 0;
    mmodel_put64(&dmap[16], (unsigned long long)fogp[0] | (unsigned long long)fogp[1] << 8 |
                                (unsigned long long)fogp[2] << 16);
    mmodel_put64(&dmap[18], 0x3d /* FOGCOL */);
    mmodel_put64(&dmap[20], ioMp->alpha_reg);
    mmodel_put64(&dmap[22], 0x42 /* ALPHA_1 */);
    mmodel_put64(&dmap[24], ioMp->test_reg);
    mmodel_put64(&dmap[26], 0x47 /* TEST_1 */);
    /* cnt 10: UNPACK 9 quads to 0x40 = addST / mulalpha / plightnum, pl_pos x4, pl_col x4 */
    dmap[28] = 0x1000000a; dmap[29] = 0; dmap[30] = 0; dmap[31] = 0;
    dmap[32] = 0; dmap[33] = 0; dmap[34] = 0; dmap[35] = 0x6c090040;
    memcpy(&dmap[36], PS2_LQ_SRC((const char *)ioMp + 0x70), 16);
    for (int i = 0; i < 4; i++) memcpy(&dmap[40 + 4 * i], PS2_LQ_SRC(pl_pos), 16);
    for (int i = 0; i < 4; i++) memcpy(&dmap[56 + 4 * i], PS2_LQ_SRC(pl_col), 16);
    dmap += 72;

    struct_45c47ec5 *mdlp = ioMp->mdlp;
    for (int i = 0; i < ioMp->mmdp->OMD.num; i++, mdlp++) {
        if (!mdlp->f_draw) continue;
        OmdTableSt *otp = mdlp->otp;
        __mmodel_object_st *objp = mdlp->objp;
        for (int j = 0; j < mdlp->nobj; j++, objp++, otp++) {
            if (!objp->f_draw) continue;
            dmap = DF_MPKT_MakeUnpackRef(dmap, 4, 4, objp->local_world_mat2);
            dmap = DF_MPKT_MakeCall(dmap, (void *)(uintptr_t)otp->mstart);
        }
    }
    return DF_MPKT_MakeVIFCode(dmap, 0, 0, 0, 0x11000000 /* FLUSHE */);
}

/* Shadow colours: on the model's private shadow display-list copy (made on first use), clear bits 18-25 of word
 * 0x84 of every sub-block, colour each vertex {255, 255, 255, 128} (black where its rgba.w <= 0) and put VIF code
 * 0x14000600 after the block (the 0x17000000 alternative behind `bne $t3, $t3` is dead). */
void DF_MMODEL_SetShadow(_stMModel *ioMp, int inMdlNum)
{
    PS2_FRAME(0x40);
    static const float col[2][4] = {{255.0f, 255.0f, 255.0f, 128.0f}, {0.0f, 0.0f, 0.0f, 0.0f}};   /* rodata 0x260030 */
    if (!ioMp || !ioMp->mmdp || !ioMp->mdlp || !ioMp->flag.set_mdl) return;
    if (!(inMdlNum < ioMp->mmdp->OMD.num)) return;
    struct_45c47ec5 *mdlp = ioMp->mdlp + inMdlNum;
    if (!mdlp->f_shadow) {
        DF_MMODEL_CopyOMDTable(ioMp, inMdlNum, 1);
        FlushCache(0);
    }
    OmdTableSt *otp = mdlp->s_otp;
    for (int i = 0; i < mdlp->nobj; i++, otp++) {
        unsigned char *buff = (unsigned char *)HOSTPTR(otp->mstart);
        for (int pnum = otp->pnum; pnum; pnum--) {
            unsigned int *w = (unsigned int *)buff;
            w[0x84 / 4] &= 0xfc03ffff;
            int pvnum = w[0x80 / 4] & 0x7fff;
            struct_00faa976 *opp = (struct_00faa976 *)(buff + 0xa0);
            for (; pvnum; pvnum--, opp++) memcpy(opp->rgba, opp->rgba[3] <= 0.0f ? col[1] : col[0], 16);
            ((unsigned int *)opp)[3] = 0x14000600;
            buff = (unsigned char *)opp + 0x10;
        }
    }
}

/* Vertex colours: on the model's private display-list copy (made on first use), every vertex with rgba.w > 0 (or
 * NaN) gets inColp as floats */
void DF_MMODEL_SetVertColor(_stMModel *ioMp, int inMdlNum, struct_76b810ec *inColp)
{
    PS2_FRAME(0x40);
    if (!ioMp || !ioMp->mmdp || !ioMp->mdlp || !ioMp->flag.set_mdl) return;
    if (!(inMdlNum < ioMp->mmdp->OMD.num)) return;
    struct_45c47ec5 *mdlp = ioMp->mdlp + inMdlNum;
    if (!mdlp->f_otp) {
        DF_MMODEL_CopyOMDTable(ioMp, inMdlNum, 0);
        FlushCache(0);
    }
    float col[4] = {(float)inColp->r, (float)inColp->g, (float)inColp->b, (float)inColp->a};
    OmdTableSt *otp = mdlp->otp;
    for (int i = 0; i < mdlp->nobj; i++, otp++) {
        unsigned char *buff = (unsigned char *)HOSTPTR(otp->mstart);
        for (int pnum = otp->pnum; pnum; pnum--) {
            int pvnum = ((unsigned int *)buff)[0x80 / 4] & 0x7fff;
            struct_00faa976 *opp = (struct_00faa976 *)(buff + 0xa0);
            for (; pvnum; pvnum--, opp++)
                if (!(opp->rgba[3] <= 0.0f)) memcpy(opp->rgba, col, 16);
            buff = (unsigned char *)opp + 0x10;
        }
    }
    FlushCache(0);
}

/* Private copy of sub-model inMdlNum's display lists (table + data, MMM kind 0; the table entries are relocated to
 * the copy) installed as otp (inKind 0) or s_otp (inKind 1); a previous copy moves to otp2 / s_otp2 and is flagged
 * for deletion. Other kinds: the copy is made and dropped (leak; never called that way). */
static void DF_MMODEL_CopyOMDTable(_stMModel *ioMp, int inMdlNum, int inKind)
{
    PS2_FRAME(0x90);
    if (!ioMp || !ioMp->mmdp || !ioMp->mdlp || !ioMp->flag.set_mdl) return;
    if (!(inMdlNum < ioMp->mmdp->OMD.num)) return;
    struct_38528994 *ohp = (struct_38528994 *)DF_MDATA_GetData(ioMp->mmdp->OMD.datap, inMdlNum);
    struct_45c47ec5 *mdlp = ioMp->mdlp + inMdlNum;
    int size = (int)ohp->size - (mdlp->nobj * 0x50 + 0x10);
    OmdTableSt *d_otp = (OmdTableSt *)DF_MMM_Alloc(0, size);
    OmdTableSt *s_otp = (OmdTableSt *)(ohp + 1);
    OmdTableSt *otp = d_otp;
    int otp_size = (int)(ohp->nobj << 4);
    DF_MFUNC_CopyMemory(d_otp, s_otp, otp_size >> 2);
    unsigned int s_startp = s_otp->mstart;
    unsigned char *d_startp = (unsigned char *)d_otp + (ohp->nobj << 4);
    DF_MFUNC_CopyMemory(d_startp, HOSTPTR(s_startp), (size - otp_size) >> 2);
    for (unsigned int i = 0; i < ohp->nobj; i++, s_otp++, otp++)
        otp->mstart = s_otp->mstart - s_startp + PS2PTR(d_startp);
    if (inKind == 1) {
        if (mdlp->f_shadow == 1) {
            mdlp->s_otp2 = mdlp->s_otp;
            mdlp->f_del_shadow2 = 1;
        }
        mdlp->s_otp = d_otp;
        mdlp->f_shadow = 1;
    } else if (inKind == 0) {
        if (mdlp->f_otp == 1) {
            mdlp->otp2 = mdlp->otp;
            mdlp->f_del_otp2 = 1;
        }
        mdlp->otp = d_otp;
        mdlp->f_otp = 1;
    }
}
