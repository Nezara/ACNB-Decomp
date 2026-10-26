/* src/event/evpolyg.cpp - stage event animations that drive an AC, a stage model or a camera along an MI2 track
 * (s_EvPolyg[16], each with its own EVMI2 player). model_type 0 = AC (position + yaw, motion stopped), 1-4 = stage
 * models (position + angles, collision sphere / height range moved along), 5 = camera (mi2_app: 0 = position,
 * 1 = gaze point, 2 = position + angles -> gaze 100 units ahead). Angles are wrapped to [-pi, pi]. */
#include "acnb/port.h"

static void evpolyg_wrap(float *a)
{
    if (!(*a <= 3.1415927f)) do *a = *a - 6.2831855f; while (!(*a <= 3.1415927f));
    if (*a < -3.1415927f) do *a = *a + 6.2831855f; while (*a < -3.1415927f);
}

int EVPOLYG_GetEvpolygType(int type)
{
    switch ((unsigned int)type) {
    case 1: return 1;
    case 2: return 2;
    case 3: return 4;
    case 4: return 0;
    case 8: return 3;
    default: return 6;
    }
}

static void __evpolyg_AnimCtrlCam(__evpolyg_st *pEvPolyg)
{
    PS2_FRAME(0x60);
    CameraSt *cam = &CAM[pEvPolyg->model_id];
    PS2_STACK(float[4], pos, 0x50);
    EVMI2_GetMI2InterpolatePos(pos, pEvPolyg->pEvmi2);
    if (pEvPolyg->mi2_app == 2) {
        acvu0_CopyVectorF_ni(cam->ev_pos, pos);
        PS2_STACK(float[4], ang, 0x40);
        PS2_STACK(float[4], dir, 0x30);
        EVMI2_GetMI2InterpolateAng(ang, pEvPolyg->pEvmi2);
        evpolyg_wrap(&ang[0]);
        evpolyg_wrap(&ang[1]);
        evpolyg_wrap(&ang[2]);
        ang2vec2(dir, ang[1], ang[0]);
        acvu0_ScaleVector_ni(dir, dir, 100.0f);
        acvu0_SubVector_ni(cam->ev_ang, cam->ev_pos, dir);
    } else if (pEvPolyg->mi2_app == 1) {
        acvu0_CopyVectorF_ni(cam->ev_ang, pos);
    } else if (pEvPolyg->mi2_app == 0) {
        acvu0_CopyVectorF_ni(cam->ev_pos, pos);
    }
}

static void __evpolyg_AnimCtrlModel(__evpolyg_st *pEvPolyg)
{
    PS2_FRAME(0x70);
    int type;
    switch (pEvPolyg->model_type) {
    case 4: type = 3; break;
    case 3: type = 8; break;
    case 2: type = 2; break;
    case 1: type = 1; break;
    case 0: type = 4; break;
    default: type = 0; break;
    }
    StModel *mp = STAGE_GetModelPointer(type, pEvPolyg->model_id);
    PS2_STACK(float[4], old, 0x60);
    PS2_STACK(float[4], d, 0x50);
    PS2_STACK(float[4], pos, 0x40);
    PS2_STACK(float[4], ang, 0x30);
    acvu0_CopyVectorF_ni(old, mp->loc);
    EVMI2_GetMI2InterpolatePos(pos, pEvPolyg->pEvmi2);
    EVMI2_GetMI2InterpolateAng(ang, pEvPolyg->pEvmi2);
    pos[3] = 1.0f;
    evpolyg_wrap(&ang[0]);
    evpolyg_wrap(&ang[1]);
    evpolyg_wrap(&ang[2]);
    ang[3] = 1.0f;
    acvu0_CopyVectorF_ni(mp->loc, pos);
    acvu0_CopyVectorF_ni(mp->ang, ang);
    DF_CALC_UpdateModelMatrix(mp);
    acvu0_SubVector_ni(d, pos, old);
    mp->mlist->sphere[0] = mp->mlist->sphere[0] + d[0];
    mp->mlist->sphere[1] = mp->mlist->sphere[1] + d[1];
    mp->mlist->sphere[2] = mp->mlist->sphere[2] + d[2];
    mp->Ytop = mp->Ytop + d[1];
    mp->Ybottom = mp->Ybottom + d[1];
}

/* No free check: with all 16 slots busy the original writes the entry at PS2 address 0 (never in practice; the port
 * only starts the MI2 player). */
void EVPOLYG_Entry(unsigned char anim_id, unsigned char model_type, unsigned char model_id, unsigned char loop,
                   unsigned char mi2_app)
{
    PS2_FRAME(0x20);
    int i;
    for (i = 0; i < 16; i++)
        if (!s_EvPolyg[i].use) break;
    if (i == 16) {
        EVMI2_Entry(anim_id, loop);
        return;
    }
    __evpolyg_st *p = &s_EvPolyg[i];
    p->use = 1;
    p->model_type = model_type;
    p->model_id = model_id;
    p->loop = loop;
    p->mi2_app = mi2_app;
    p->pEvmi2 = EVMI2_Entry(anim_id, loop);
}

void EVPOLYG_Proc(void)
{
    PS2_FRAME(0x90);
    for (int i = 0; i < 16; i++) {
        __evpolyg_st *p = &s_EvPolyg[i];
        if (p->use != 1) continue;
        switch (p->model_type) {
        case 0: {
            AcPlayerSt *ac = &g_ac[p->model_id];
            PS2_STACK(float[4], pos, 0x80);
            PS2_STACK(float[4], ang, 0x70);
            EVMI2_GetMI2InterpolatePos(pos, p->pEvmi2);
            EVMI2_GetMI2InterpolateAng(ang, p->pEvmi2);
            pos[3] = 1.0f;
            ang[0] = 0.0f;   /* yaw only */
            ang[2] = 0.0f;
            ang[3] = 1.0f;
            ac->velocity[0] = 0.0f;
            ac->velocity[1] = 0.0f;
            ac->velocity[2] = 0.0f;
            ac->velocity[3] = 1.0f;
            acvu0_CopyVectorF_ni(ac->loc, pos);
            acvu0_CopyVectorF_ni(ac->ang, ang);
            ac->turnspd = 0.0f;
            break;
        }
        case 1: case 2: case 3: case 4:
            __evpolyg_AnimCtrlModel(p);
            break;
        case 5:
            __evpolyg_AnimCtrlCam(p);
            break;
        }
        if (EVMI2_IsMI2AnimEnd(p->pEvmi2) == 1 && p->loop != 1) ZeroMemory32b(p, 4);
    }
}

void EVPOLYG_Init(void)
{
    ZeroMemory32b(s_EvPolyg, 0x40);
}
