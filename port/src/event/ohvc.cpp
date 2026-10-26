/* src/event/ohvc.cpp - "overhead" ambience voices (up to 16): looped voices at a fixed height whose volume follows
 * the camera's vertical distance, 127 * (range - |height - camY|) / range (not clamped below 0 in split view);
 * split view pans between the two viewers' volumes. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"

int OHVCS_Set(int hVoice, float height, float dist)
{
    if (OHVCSnum >= 0x10) return 0;
    struct_beedc79e *p = &Ohvcs[OHVCSnum];
    p->vh = hVoice;
    p->height = height;
    p->maxdist = dist;
    OHVCSnum++;
    return 1;
}

static void _df_Split_OHVCS_Proc(void)
{
    PS2_FRAME(0x70);
    PS2_STACK(float[2], camy, 0x60);
    PS2_STACK(float[2], view, 0x68);
    memset(camy, 0, 8);
    camy[0] = CAM[(&ViewCamID[0])[0]].vec[1];
    camy[1] = CAM[(&ViewCamID[0])[1]].vec[1];
    struct_beedc79e *p = Ohvcs;
    for (int i = 0; i < OHVCSnum; i++, p++) {
        struct_186b36a4 *erv = &g_erv[p->vh];
        float vol = 0.0f;   /* not reset per view: view[1] is the sum of both */
        for (int k = 0; k < 2; k++) {
            float d = p->height - camy[k];
            if (d < 0.0f) d = -d;
            float r = p->maxdist;
            vol = vol + ps2_div(127.0f * (r - d), r);
            view[k] = vol;
        }
        vol = vol * 0.75f;
        float pan = view[1] == 0.0f ? 64.0f : 127.0f * (1.0f - view[0] / view[1]);
        int ivol = ps2_ftoi(vol), ipan = ps2_ftoi(pan);
        if (ivol > 0x7f) ivol = 0x7f;
        if (ivol < 0) ivol = 0;
        if (ipan > 0x6c) ipan = 0x6c;
        if (ipan < 0x14) ipan = 0x14;
        int v = DF_SND2_VolumeFilter_SE(ivol);
        fssdsynthSeVolume(erv->vid, v);
        fssdsynthSePan(erv->vid, ipan);
    }
}

static void _df_Full_OHVCS_Proc(void)
{
    PS2_FRAME(0x50);
    float camy = CAM[ViewCamID[0]].vec[1];
    struct_beedc79e *p = Ohvcs;
    for (int i = 0; i < OHVCSnum; i++, p++) {
        struct_186b36a4 *erv = &g_erv[p->vh];
        float d = p->height - camy;
        if (d <= 0.0f) d = -d;
        float r = p->maxdist;
        float vol = ps2_div(127.0f * (r - d), r);
        if (!(vol <= 127.0f)) vol = 127.0f;
        if (vol < 0.0f) vol = 0.0f;
        int v = DF_SND2_VolumeFilter_SE(ps2_ftoi(vol));
        erv->vol = (short)v;
        fssdsynthSeVolume(erv->vid, v);
    }
}

void DF_OHVCS_Stop(void)
{
    PS2_FRAME(0x30);
    for (int i = 0; i < OHVCSnum; i++) Voice_Stop(Ohvcs[i].vh, 0);
    OHVCSnum = 0;
}

void DF_OHVCS_Proc(void)
{
    PS2_FRAME(0x10);
    if (ViewNum == 1) _df_Full_OHVCS_Proc();
    else _df_Split_OHVCS_Proc();
}

void DF_OHVC_Init(OhvcsInfoSt *pInfo)
{
    PS2_FRAME(0x20);
    OHVCSnum = 0;
    ZeroMemory32b(Ohvcs, 0x40);
    if (!pInfo->ohvcs_id) return;
    Ohvcs[0].vh = Voice_CallLoop(pInfo->ohvcs_id, pInfo->ohvcs_tone << 16 | pInfo->ohvcs_vol);
    Ohvcs[0].height = pInfo->ohvcs_height;
    OHVCSnum++;
    Ohvcs[0].maxdist = pInfo->ohvcs_range;
}
