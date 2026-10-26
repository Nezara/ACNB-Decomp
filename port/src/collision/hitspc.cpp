/* src/collision/hitspc.cpp - statistics of the stage's hit models (whole file): bounding box of the map, centre
 * and average model size, and the spread of the models around the centre (s_HitSpaceInfo; the octree in het.cpp
 * sizes its cells from them). Hit models = HitMap[0..HitMapNum) then HitObj[0..HitObjNum). */
#include "acnb/sdk.h"
#include "hitwk.h"

static const float FMAX = ps2_u2f(0x7f7fffff);

/* mean squared distance of the model centres from the centre per axis, over half the map size */
static void __GetDispersion(void)
{
    struct_b07f55e5 *s = &s_HitSpaceInfo;
    int n = HitMapNum + HitObjNum;
    for (int i = 0; i < n; i++) {
        StModel *m = hit_model(i);
        for (int k = 0; k < 3; k++) {
            float d = m->mlist->sphere[k] - s->Center[k];
            s->Dispersion[k] = s->Dispersion[k] + d * d;
        }
    }
    float fn = (float)n;
    for (int k = 0; k < 3; k++) {
        s->Dispersion[k] = ps2_div(s->Dispersion[k], fn);
        s->Dispersion[k] = ps2_div(s->Dispersion[k], 0.5f * (s->BBMax[k] - s->BBMin[k]));
    }
}

static void __GetSpaceInfo(void)
{
    struct_b07f55e5 *s = &s_HitSpaceInfo;
    s->BBMin[0] = s->BBMin[1] = s->BBMin[2] = FMAX;
    s->BBMax[0] = s->BBMax[1] = s->BBMax[2] = -FMAX;
    s->BBMax[3] = s->BBMin[3] = 1.0f;
    int n = HitMapNum + HitObjNum;
    for (int i = 0; i < n; i++) {
        StModel *m = hit_model(i);
        if (m) {
            const float *sp = m->mlist->sphere;
            float v = sp[0] - sp[3];
            if (!(s->BBMin[0] <= v)) s->BBMin[0] = v;
            if (!(s->BBMin[1] <= m->Ytop)) s->BBMin[1] = m->Ytop;
            v = sp[2] - sp[3];
            if (!(s->BBMin[2] <= v)) s->BBMin[2] = v;
            v = sp[0] + sp[3];
            if (s->BBMax[0] < v) s->BBMax[0] = v;
            if (s->BBMax[1] < m->Ybottom) s->BBMax[1] = m->Ybottom;
            v = sp[2] + sp[3];
            if (s->BBMax[2] < v) s->BBMax[2] = v;
        }
        /* (a null entry would be dereferenced here in the original too) */
        const float *sp = m->mlist->sphere;
        s->Center[0] = s->Center[0] + sp[0];
        s->Center[1] = s->Center[1] + sp[1];
        s->Center[2] = s->Center[2] + sp[2];
        s->MdlAvgSize[0] = s->MdlAvgSize[0] + 2.0f * sp[3];
        s->MdlAvgSize[1] = s->MdlAvgSize[1] + (m->Ybottom - m->Ytop);
        s->MdlAvgSize[2] = s->MdlAvgSize[2] + 2.0f * sp[3];
    }
    float fn = (float)n;
    for (int k = 0; k < 3; k++) {
        s->Center[k] = ps2_div(s->Center[k], fn);
        s->MdlAvgSize[k] = ps2_div(s->MdlAvgSize[k], fn);
    }
    __GetDispersion();
}

/* the model's hit mesh through matrix2 into HIT_WK->vtx, and its bounding box into HIT_WK->bb */
void HitSpcGetHitModelBoundBox(StModel *pMdl)
{
    HitSprWk *w = HIT_WK;
    HmdHeadSt *h = pMdl->hmd;
    const float (*vp)[4] = h->vp;
    float (*out)[4] = w->vtx;
    w->bb.max[0] = w->bb.max[1] = w->bb.max[2] = -FMAX;
    w->bb.min[0] = w->bb.min[1] = w->bb.min[2] = FMAX;
    const float (*m)[4] = pMdl->matrix2;
    unsigned int n = h->v_num;
    for (unsigned int g = n >> 2; g; g--, vp += 4, out += 4) {
        for (int j = 0; j < 4; j++) vu_apply(out[j], m, vp[j]);
        for (int j = 0; j < 4; j++)
            for (int k = 0; k < 3; k++) {
                if (!(out[j][k] <= w->bb.max[k])) w->bb.max[k] = out[j][k];
                if (out[j][k] < w->bb.min[k]) w->bb.min[k] = out[j][k];
            }
        for (int j = 0; j < 4; j++) PS2_VF_MIRROR(16 + j, out[j]);
    }
    for (unsigned int r = n & 3; r; r--, vp++, out++) {
        vu_apply(*out, m, *vp);
        for (int k = 0; k < 3; k++) {
            if (!(out[0][k] <= w->bb.max[k])) w->bb.max[k] = out[0][k];
            if (out[0][k] < w->bb.min[k]) w->bb.min[k] = out[0][k];
        }
        PS2_VF_MIRROR(16, *out);
    }
    for (int j = 0; j < 4; j++) PS2_VF_MIRROR(1 + j, m[j]);
}

void HitSpcGetMdlAvgSize(float *MdlAvgSize) { memcpy(MdlAvgSize, s_HitSpaceInfo.MdlAvgSize, 16); }

void HitSpcGetCenter(float *Center) { memcpy(Center, s_HitSpaceInfo.Center, 16); }

void HitSpcGetMapBB(float *BBMin, float *BBMax)
{
    memcpy(BBMin, s_HitSpaceInfo.BBMin, 16);
    memcpy(BBMax, s_HitSpaceInfo.BBMax, 16);
}

void HitSpcInit(void)
{
    ZeroMemory32b(&s_HitSpaceInfo, 0x20);
    __GetSpaceInfo();
}
