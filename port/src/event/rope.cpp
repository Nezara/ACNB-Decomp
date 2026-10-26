/* src/event/rope.cpp - hanging ropes / chains of stage models (up to 8). A rope is a chain of point_num points
 * (Verlet style: velocity = 0.9 * (now - old)) under gravity 0.0817 per frame, relaxed 5 times per frame toward
 * segment length rope_len (36% correction per pass). Flags: 1 = first point pinned to InitPos, 2 = last point
 * follows pMat's translation, 4 = stretch each segment object along z by its length, 8 = floor at rope_Y.
 * __UpdateObject aims the model's objects fst_obj_id.. along the segments. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"

static const float ROPE_GRAVITY = 0.081668295f;   /* 0x3da741b5 */

static void rope_wrap(float *a)
{
    while (!(*a <= 3.1415927f)) *a = *a - 6.2831855f;
    while (*a < -3.1415927f) *a = *a + 6.2831855f;
}

static void __UpdateObject(RopeDataSt *pData)
{
    PS2_FRAME(0xb0);
    RopeStateSt *st = pData->pState;
    ObjectSt *op = pData->pMdl->op + pData->fst_obj_id;
    PS2_STACK(float[4][4], m, 0x50);
    PS2_STACK(float[4], ang, 0x90);
    PS2_STACK(float[4], d, 0xa0);
    for (int i = 1; i < pData->point_num; i++, op++, st++) {
        d[0] = st[0].NowPos[0] - st[1].NowPos[0];   /* vsub.xyz + sqc2: w = st[0].NowPos.w */
        d[1] = st[0].NowPos[1] - st[1].NowPos[1];
        d[2] = st[0].NowPos[2] - st[1].NowPos[2];
        d[3] = st[0].NowPos[3];
        vec2ang(ang, d);
        rope_wrap(&ang[0]);
        rope_wrap(&ang[1]);
        sceVu0CopyMatrix(m, IdentityMtx);
        if (pData->flag & 4)
            m[2][2] = m[2][2] * ps2_div(ps2_sqrt((d[0] * d[0] + d[1] * d[1]) + d[2] * d[2]), pData->rope_len);
        sceVu0RotMatrixX(m, m, ang[0]);
        sceVu0RotMatrixY(m, m, ang[1]);
        sceVu0TransMatrix(*op->wrmatp, m, st[0].NowPos);
    }
}

/* p.xyz += v.xyz (w kept) */
static void rope_add_xyz(float *p, const float *v)
{
    p[0] = p[0] + v[0];
    p[1] = p[1] + v[1];
    p[2] = p[2] + v[2];
}

static void rope_sub_xyz(float *p, const float *v)
{
    p[0] = p[0] - v[0];
    p[1] = p[1] - v[1];
    p[2] = p[2] - v[2];
}

static void __RopeCtrl(RopeDataSt *pData)
{
    PS2_FRAME(0x30);
    int n = pData->point_num;
    float floor_y = pData->rope_Y;
    float len = pData->rope_len;
    RopeStateSt *st = pData->pState;
    if (pData->flag & 1) {
        memcpy(st->NowPos, pData->InitPos, 16);
        memcpy(st->OldPos, pData->InitPos, 16);
        memcpy(st->Vel, ZeroVectorF, 16);
    } else {
        st->NowPos[1] = st->NowPos[1] + ROPE_GRAVITY;
        rope_add_xyz(st->NowPos, st->Vel);
    }
    if (pData->flag & 2) {
        RopeStateSt *last = &pData->pState[pData->point_num - 1];
        memcpy(last->NowPos, (*pData->pMat)[3], 16);
        memcpy(last->OldPos, (*pData->pMat)[3], 16);
        memcpy(last->Vel, ZeroVectorF, 16);
    }
    st = pData->pState;
    for (int i = 1; i < n; i++) {
        st[i].NowPos[1] = st[i].NowPos[1] + ROPE_GRAVITY;
        rope_add_xyz(st[i].NowPos, st[i].Vel);
    }
    PS2_STACK(float[4], d, 0x20);
    for (int pass = 0; pass < 5; pass++) {
        st = pData->pState;
        for (int i = 1; i < n; i++) {
            float *a = st[i - 1].NowPos, *b = st[i].NowPos;
            d[0] = b[0] - a[0];   /* vsub.xyz + sqc2: w = b.w */
            d[1] = b[1] - a[1];
            d[2] = b[2] - a[2];
            d[3] = b[3];
            float l = ps2_sqrt(d[2] * d[2] + (d[0] * d[0] + d[1] * d[1]));
            float k = ps2_div(0.35999998f * (len - l), l);
            d[0] = d[0] * k;   /* vmulx.xyz: w kept */
            d[1] = d[1] * k;
            d[2] = d[2] * k;
            rope_sub_xyz(a, d);
            rope_add_xyz(b, d);
        }
        if (pData->flag & 8) {
            st = pData->pState;
            for (int i = 0; i < n; i++)
                if (!(st[i].NowPos[1] <= floor_y)) st[i].NowPos[1] = floor_y + 0.5f * st[i].Vel[1];
        }
    }
    PS2_STACK(float[4], v, 0x10);
    st = pData->pState;
    for (int i = 0; i < n; i++) {
        v[0] = st[i].NowPos[0] - st[i].OldPos[0];   /* w = NowPos.w, kept by the vmulx.xyz into Vel */
        v[1] = st[i].NowPos[1] - st[i].OldPos[1];
        v[2] = st[i].NowPos[2] - st[i].OldPos[2];
        v[3] = st[i].NowPos[3];
        st[i].Vel[0] = v[0] * 0.9f;
        st[i].Vel[1] = v[1] * 0.9f;
        st[i].Vel[2] = v[2] * 0.9f;
        st[i].Vel[3] = v[3];
        memcpy(st[i].OldPos, st[i].NowPos, 16);
    }
    __UpdateObject(pData);
}

void Rope_SetFlag(int id, int flag, int act)
{
    if (id < 0 || id >= s_NowCtrlRopeNum) return;
    RopeDataSt *p = &s_RopeData[id];
    if (act == 2) p->flag = p->flag & ~flag;
    else if (act == 1) p->flag = p->flag | flag;
    else if (act == 0) p->flag = flag;
}

int Rope_Entry(StModel *pMdl, float (*pMat)[4][4], int fst_obj_id, int lst_obj_id, float rope_len)
{
    int id = s_NowCtrlRopeNum;
    if (id >= 8 || !pMdl || !pMat || lst_obj_id < fst_obj_id || rope_len < 0.0f) return -1;
    PS2_FRAME(0x30);
    RopeDataSt *p = &s_RopeData[id];
    p->pMdl = pMdl;
    p->pMat = pMat;
    p->fst_obj_id = (short)fst_obj_id;
    p->lst_obj_id = (short)lst_obj_id;
    p->point_num = lst_obj_id - fst_obj_id + 2;
    p->rope_len = rope_len;
    p->rope_Y = pMdl->loc[1];
    memcpy(p->InitPos, (*pMdl->op[fst_obj_id].wrmatp)[3], 16);
    p->flag = 2;
    int size = p->point_num * 0x30;
    p->pState = (RopeStateSt *)Memm_Alloc(1, size, 0x10);
    if (!p->pState) return -1;
    ZeroMemory32b(p->pState, size / 4);
    return s_NowCtrlRopeNum++;
}

void Rope_CtrlProc(void)
{
    PS2_FRAME(0x40);
    int n = s_NowCtrlRopeNum;
    for (int i = 0; i < n; i++) __RopeCtrl(&s_RopeData[i]);
}

void Rope_Init(void)
{
    s_NowCtrlRopeNum = 0;
    ZeroMemory32b(s_RopeData, 0x60);
}
