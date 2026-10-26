/* src/event/gfunc.cpp - helpers of the gimmick system (gimk.c, stage scripts, hit setup): model matrix / bounding
 * sphere refresh, destruction (hide the intact object chain of a model, show its destroyed one or the separate
 * "broken" model aid + 1), "standing on this gimmick" tests for ACs and enemy characters, angles from a matrix
 * (ZXY / YXZ), and predicates on a model's array eid. Only Gfunc_IsEventBtnExec runs in Versus (hit setup at
 * stage load). */
#include "acnb/sdk.h"
#include "acnb/vu0m.h"

/* event button models: map / object / dynamic object with eid 0x50, 0x38, 0x36 or 0x34 */
int Gfunc_IsEventBtnExec(StModel *pMdl)
{
    int t = pMdl->type;
    if ((unsigned int)(t - 1) >= 2 && t != 8) return 0;
    unsigned int e = pMdl->array->eid;
    return e == 0x50 || e == 0x38 || e == 0x36 || e == 0x34;
}

/* matrix2 = mat (64 bytes), child objects, location = mat row 3, bounding sphere centre and height range moved
 * along with it */
static void gfunc_set_matrix(StModel *d, float (*mat)[4], float *ctr)
{
    CopyMemory32b(d->matrix2, mat, 0x10);
    Anim_UpdateChildObjMatrix(d->op, d->matrix2);
    memcpy(d->loc, mat[3], 16);
    float oldy = d->mlist->sphere[1];
    vu_apply(ctr, (const float (*)[4])d->matrix2, d->localcenter);
    d->mlist->sphere[0] = ctr[0];
    d->mlist->sphere[1] = ctr[1];
    d->mlist->sphere[2] = ctr[2];
    float dy = d->mlist->sphere[1] - oldy;
    d->Ytop = d->Ytop + dy;
    d->Ybottom = d->Ybottom + dy;
}

/* destroyed state of a gimmick model: wk[0x20] bit 1 = the broken model is the next model (aid + 1), shown in the
 * intact one's place; otherwise the model's destroyed object chain is shown instead of the intact one */
void Gfunc_AfterDestroySetting(GimkSt *pGimk)
{
    PS2_FRAME(0x50);
    pGimk->wk[0x20] &= ~8;
    if ((pGimk->wk[0x20] >> 1) & 1) {
        StModel *mp = pGimk->mp;
        StModel *d = STAGE_GetModelPointer(mp->type, mp->aid + 1);
        PS2_STACK(float[4], ctr, 0x40);
        gfunc_set_matrix(d, mp->matrix2, ctr);
        d->groupid = pGimk->mp->groupid;
        d->use &= 0xfffc;
        if (!(d->array->mflag & 0x80)) d->mflag &= 0xff7f;
        pGimk->mp->use |= 3;
        pGimk->mp->mflag |= 0x80;
    } else {
        ObjectSt *o = DF_GIMK_GetDestObject(pGimk->mp);
        if (o) DF_GIMK_SetDrawObject(o, 1);
        StModel *mp = pGimk->mp;
        o = mp->op + 2;   /* the original tests this sum for NULL (never) */
        StMixAnimationTable *tbl = mp->tod->animtable[mp->tod->nowtodid];
        while (o && tbl[o->objid].tp2id == 0xff) o = o->sibling;
        if (o) {
            o->onoff = 1;
            if (o->child) DF_GIMK_SetDrawObject(o->child, 0);
        }
        pGimk->mp->use |= 2;
        pGimk->mp->mflag |= 0x80;
    }
    pGimk->mp->use |= 4;
}

/* destroyed in an earlier visit of the stage (destroyed-object memory) */
int Gfunc_IsNowDestroyModel(GimkSt *pGimk)
{
    PS2_FRAME(0x20);
    DestMemSt *dm = &pGimk->mp->array->destMem;
    if (!dm->destMemID) return 0;
    int s = StageInfo_RealToLogicalStageNo(g_sortie.pconstant->map);
    if (!s) return 0;
    return DestObjMem_GetDestObjCnt(s, dm->destMemID) != 0;
}

int DF_GIMK_IsDestroyModel(StModel *pMdl)
{
    if (!pMdl->array) return 0;
    unsigned int e = pMdl->array->eid;
    if (e == 0x10 || e == 0x12 || e == 0x14 || e == 0x16 || e == 0x18 || e == 0x36 || e == 0x60 || e == 0x62) return 1;
    return e - 0x70 < 3 || e == 0x73;
}

/* moves / turns the child model by the parent's offset from its array placement minus the child's own */
void DF_GIMK_ModelAttach(StModel *pParentMdl, StModel *pChildMdl)
{
    PS2_FRAME(0x60);
    PS2_STACK(float[4], p, 0x50);
    PS2_STACK(float[4], c, 0x40);
    PS2_STACK(float[4], rot, 0x20);
    PS2_STACK(float[4], mv, 0x30);
    for (int k = 0; k < 3; k++) p[k] = pParentMdl->ang[k] - pParentMdl->array->ang[k];
    memcpy(&p[3], &pParentMdl->ang[3], 4);
    for (int k = 0; k < 3; k++) c[k] = pChildMdl->ang[k] - pChildMdl->array->ang[k];
    memcpy(&c[3], &pChildMdl->ang[3], 4);
    for (int k = 0; k < 3; k++) rot[k] = p[k] - c[k];
    for (int k = 0; k < 3; k++) p[k] = pParentMdl->loc[k] - pParentMdl->array->loc[k];
    memcpy(&p[3], &pParentMdl->loc[3], 4);
    for (int k = 0; k < 3; k++) c[k] = pChildMdl->loc[k] - pChildMdl->array->loc[k];
    memcpy(&c[3], &pChildMdl->loc[3], 4);
    rot[3] = 1.0f;
    for (int k = 0; k < 3; k++) mv[k] = p[k] - c[k];
    mv[3] = 1.0f;
    DF_CALC_RotateModel(pChildMdl, rot);
    DF_CALC_MoveModel(pChildMdl, mv);
}

/* hp to 0 and message 2 to the model (not when animating / already destroyed) */
void DF_GIMK_ModelBomb(GimkSt *pGimk)
{
    PS2_FRAME(0x10);
    StModel *m = pGimk->mp;
    if ((m->use & 0x10) || (m->use & 4)) return;
    m->nowhp = 0;
    m = pGimk->mp;
    if (m->msgfunc) gcall_as<void(void *, int, int, int)>(m->msgfunc, m->arg0p, 2, 0, 0);   /* 4 arguments */
}

void DF_GIMK_ErrorMSG(void) {}

/* angles (x, y, z) of a ZXY rotation matrix; mat[3][3] < 0 flips z */
void DFvu0_Matrix2ZXYangN(float *rot, float (*mat)[4])
{
    PS2_FRAME(0x30);
    rot[0] = -fsmath_AsinVU0(mat[2][1]);
    if (0.0f == fsmath_CosVU0(rot[0])) {
        rot[1] = ps2_atan2f(-mat[0][2], mat[0][0]);
        rot[2] = 0.0f;
        return;
    }
    rot[1] = ps2_atan2f(mat[2][0], mat[2][2]);
    rot[2] = fsmath_AsinVU0(ps2_div(mat[0][1], fsmath_CosVU0(rot[0])));
    if (mat[3][3] < 0.0f) rot[2] = 3.1415927f - rot[2];
}

/* single-precision wrap into [-pi, pi] as the original loops it (value stored back each step) */
static inline void gfunc_wrap(float *x)
{
    while (!(*x <= 3.1415927f)) *x = *x - 6.2831855f;
    while (*x < -3.1415927f) *x = *x + 6.2831855f;
}

/* angles (x, y, z, 1) of a YXZ rotation: x / z from the rotated y axis, then y from the z axis of the matrix with
 * x and z undone */
void DFvu0_Matrix2YXZangN(float *rot, float (*mat)[4])
{
    PS2_FRAME(0xd0);
    PS2_STACK(float[4][4], m, 0x90);
    PS2_STACK(float[4], v, 0x80);
    PS2_STACK(float[4], r, 0x70);
    sceVu0CopyMatrix(m, mat);
    m[3][0] = m[3][1] = m[3][2] = m[3][3] = 0.0f;
    v[0] = 0.0f;
    v[1] = 1.0f;
    v[2] = 0.0f;
    v[3] = 0.0f;
    vu_apply(r, (const float (*)[4])m, v);
    rot[0] = -ps2_atan2f(-r[2], r[1]);
    float yz = r[1] * r[1] + r[2] * r[2];
    rot[2] = -ps2_atan2f(r[0], ps2_sqrt(yz));
    sceVu0RotMatrixX(m, m, -rot[0]);
    sceVu0RotMatrixZ(m, m, -rot[2]);
    v[0] = 0.0f;
    v[1] = 0.0f;
    v[2] = 1.0f;
    v[3] = 0.0f;
    vu_apply(r, (const float (*)[4])m, v);
    rot[1] = -ps2_atan2f(-r[0], r[2]);
    rot[1] = rot[1] + 3.1415927f;
    gfunc_wrap(&rot[0]);
    gfunc_wrap(&rot[1]);
    gfunc_wrap(&rot[2]);
    rot[3] = 1.0f;
}

void DF_GIMK_SetModelMatrix(StModel *mp, float (*mat)[4])
{
    PS2_FRAME(0x40);
    PS2_STACK(float[4], ctr, 0x30);
    gfunc_set_matrix(mp, mat, ctr);
}

/* height range of the transformed vertices (Ytop = lowest y, Ybottom = highest y); the vertices go to the
 * scratchpad from 0x100 */
void DF_GIMK_SetModelY(StModel *mp)
{
    float ymin = ps2_u2f(0x7f7fffff), ymax = ps2_u2f(0xff7fffff);
    HmdHeadSt *h = mp->hmd;
    if (!h) return;
    float (*vp)[4] = h->vp;
    float *spr = (float *)PS2_SPR(0x100);
    for (unsigned int n = h->v_num; n != 0; n--, vp++, spr += 4) {
        vu_apply(spr, (const float (*)[4])mp->matrix2, *vp);
        if (spr[1] < ymin) ymin = spr[1];
        if (!(spr[1] <= ymax)) ymax = spr[1];
    }
    mp->Ytop = ymin;
    mp->Ybottom = ymax;
}

/* animation at its end: SW 0 = last frame of the current tod, else frame 0 (looping animations never end) */
int DF_GIMK_AnimEnd(StModel *mp, int SW)
{
    StXtod *t = mp->tod;
    struct_e6319a72 *head = t->head[t->nowtodid];
    if (mp->animloop) return 0;
    if (SW) return t->nowframe == 0;
    return t->nowframe == head->frame;
}

/* model back to its array placement at frame 0 of its animation: matrix, bounding sphere, height range and every
 * object's matrices from the first key of its tod (rotation Z, X, Y) */
void Gimk_SetModelInitPos(StModel *pModel)
{
    PS2_FRAME(0x110);
    StXtod *t = pModel->tod;
    StMixAnimationTable *tbl = t->animtable[t->nowtodid];
    if (t->nowframe) return;
    if (pModel->array) {
        memcpy(pModel->loc, pModel->array->loc, 16);
        memcpy(pModel->ang, pModel->array->ang, 16);
    }
    DF_MakeModelLocalMatrixZXY_Model(pModel, pModel->loc, pModel->ang, pModel->scale);
    PS2_STACK(float[4], ctr, 0x100);
    vu_apply(ctr, (const float (*)[4])pModel->matrix2, pModel->localcenter);
    pModel->mlist->sphere[0] = ctr[0];
    pModel->mlist->sphere[1] = ctr[1];
    pModel->mlist->sphere[2] = ctr[2];
    DF_GIMK_SetModelY(pModel);
    PS2_STACK(float[4][4], m, 0xb0);
    PS2_STACK(float[4], rot, 0xf0);
    for (int i = 0; i < pModel->nobj; i++, tbl++) {
        struct_3c2ab559 *td = tbl->tod_data;
        ObjectSt *op = pModel->op + i;
        memcpy(op->position, td->position, 16);
        memcpy(op->angle, td->angle, 16);
        memcpy(rot, td->angle, 16);
        gfunc_wrap(&rot[0]);
        gfunc_wrap(&rot[1]);
        gfunc_wrap(&rot[2]);
        sceVu0CopyMatrix(m, IdentityMtx);
        sceVu0RotMatrixZ(m, m, rot[2]);
        sceVu0RotMatrixX(m, m, rot[0]);
        sceVu0RotMatrixY(m, m, rot[1]);
        sceVu0TransMatrix(m, m, td->position);
        float w[4][4];
        vu_mul_matrix(w, (const float (*)[4])pModel->matrix2, (const float (*)[4])m);
        memcpy(*op->wrmatp, w, 64);
        memcpy(*op->rdmatp, w, 64);
    }
}

/* onoff = 0 (SW 1) or 1 for op, its children and its siblings (the original inlines two levels per call) */
void DF_GIMK_SetDrawObject(ObjectSt *op, int SW)
{
    PS2_FRAME(0x50);
    op->onoff = SW == 1 ? 0 : 1;
    for (int i = 0; i < 2; i++) {
        ObjectSt *a = i ? op->sibling : op->child;
        if (!a) continue;
        a->onoff = SW == 1 ? 0 : 1;
        for (int j = 0; j < 2; j++) {
            ObjectSt *b = j ? a->sibling : a->child;
            if (!b) continue;
            b->onoff = SW == 1 ? 0 : 1;
            if (b->child) DF_GIMK_SetDrawObject(b->child, SW);
            if (b->sibling) DF_GIMK_SetDrawObject(b->sibling, SW);
        }
    }
}

/* the second drawn object (tp2 id != 0xff) in the sibling chain from object 2 = the destroyed form */
ObjectSt *DF_GIMK_GetDestObject(StModel *mp)
{
    ObjectSt *o = mp->op + 2;
    if (!o) return 0;
    StMixAnimationTable *tbl = mp->tod->animtable[mp->tod->nowtodid];
    while (tbl[o->objid].tp2id == 0xff) {
        o = o->sibling;
        if (!o) return 0;
    }
    o = o->sibling;
    if (!o) return 0;
    while (tbl[o->objid].tp2id == 0xff) {
        o = o->sibling;
        if (!o) return 0;
    }
    return o;
}

/* the first drawn object in the sibling chain from object 2 = the intact form */
ObjectSt *DF_GIMK_GetDrawObject(StModel *mp)
{
    ObjectSt *o = mp->op + 2;
    if (!o) return 0;
    StMixAnimationTable *tbl = mp->tod->animtable[mp->tod->nowtodid];
    while (tbl[o->objid].tp2id == 0xff) {
        o = o->sibling;
        if (!o) return 0;
    }
    return o;
}

/* hitpos = (hloc - pos) in the object's frame (rows 0..2 of its matrix), w = 1 */
void DF_GIMK_GetHitPos(ObjectSt *op, float *pos, float *hloc, float *hitpos)
{
    PS2_FRAME(0x20);
    PS2_STACK(float[4], d, 0x10);
    PS2_STACK(float[4], p, 0x00);
    for (int k = 0; k < 3; k++) d[k] = hloc[k] - pos[k];
    memcpy(&d[3], &hloc[3], 4);
    for (int i = 0; i < 3; i++) {
        float *row = (*op->wrmatp)[i];
        for (int k = 0; k < 4; k++) p[k] = d[k] * row[k];
        hitpos[i] = p[2] + (p[0] + p[1]);
    }
    hitpos[3] = 1.0f;
}

void DF_GIMK_GetObjectPos(ObjectSt *op, float *pos)
{
    pos[0] = (*op->wrmatp)[3][0];
    pos[1] = (*op->wrmatp)[3][1];
    pos[2] = (*op->wrmatp)[3][2];
    pos[3] = 1.0f;
}

/* m stands on the gimmick: its centre height within the gimmick's sphere (+ half its height) and horizontally
 * within the sum of the radii (p / d = the original's stack temporaries) */
static int gfunc_on(GimkSt *g, StModel *m, float *p, float *d)
{
    float half = 0.5f * m->tall;
    memcpy(p, m->loc, 16);
    p[1] = p[1] - half;
    float *s = g->mp->mlist->sphere;
    for (int k = 0; k < 3; k++) d[k] = p[k] - s[k];
    memcpy(&d[3], &p[3], 4);
    float r = half + g->mp->mlist->sphere[3];
    if (!(d[1] <= r) || d[1] < -r) return 0;
    float rr = m->R + g->mp->mlist->R;
    rr = rr * rr;
    return d[0] * d[0] + d[2] * d[2] - rr <= 0.0f;
}

/* the first mission character (not dead, not hidden) standing on the gimmick */
StModel *DF_GIMK_GetOnCstMT(GimkSt *pGimk)
{
    PS2_FRAME(0x50);
    if (!StgInfo.num_fmt) return 0;
    PS2_STACK(float[4], p, 0x30);
    PS2_STACK(float[4], d, 0x40);
    for (int i = StgInfo.top_fchr; i < StgInfo.top_fchr + StgInfo.num_fmt; i++) {
        EnemySt *e = DFene_GetEnemy(i);
        StModel *m = e->mp;
        if ((e->use & 0x80) || (m->use & 2)) continue;
        if (gfunc_on(pGimk, m, p, d)) return m;
    }
    return 0;
}

/* the first AC (not hidden) standing on the gimmick */
StModel *DF_GIMK_GetOnAC(GimkSt *gimk)
{
    PS2_FRAME(0x20);
    PS2_STACK(float[4], p, 0x00);
    PS2_STACK(float[4], d, 0x10);
    for (int i = 0; i < NowPlayerNum; i++) {
        StModel *m = g_ac[i].mp;
        if (m->use & 2) continue;
        if (gfunc_on(gimk, m, p, d)) return g_ac[i].mp;
    }
    return 0;
}

void DF_GIMK_ObjDestroy(GimkSt *gimk) { Event_MdlDestroyCnt(gimk->mp); }
