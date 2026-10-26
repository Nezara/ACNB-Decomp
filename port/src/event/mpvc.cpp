/* src/event/mpvc.cpp - "multi-point" stage ambience: one looped voice whose volume / pan follow up to 64 marker
 * points (type 9 markers, wk = range). Volume = sum over points of 127 * (range^2 - dist^2) / range^2 (squared
 * distances from the camera); pan (full view) = the volume-weighted mean of each point's direction relative to
 * the camera yaw, mapped to 20..108; split view pans between the two viewers' volumes. MPVCSTime / MPVCSPan scale
 * the result (event fades). */
#include "acnb/port.h"
#include "acnb/ps2fp.h"

static void _df_Split_MPVCS_Proc(int *i_vol, int *i_pan)
{
    PS2_FRAME(0x30);
    PS2_STACK(float[4], d, 0x0);
    PS2_STACK(float[4], cam, 0x10);
    PS2_STACK(float[2], view, 0x28);
    float vol = 0.0f;   /* not reset per view: view[1] is the sum of both */
    for (int k = 0; k < 2; k++) {
        memcpy(cam, CAM[(&ViewCamID[0])[k]].vec, 16);
        struct_9a5465d2 *p = Mpvcs;
        for (int i = 0; i < MPVCSnum; i++, p++) {
            d[0] = cam[0] - p->loc[0];   /* vsub.xyz + sqc2: w = cam.w */
            d[1] = cam[1] - p->loc[1];
            d[2] = cam[2] - p->loc[2];
            d[3] = cam[3];
            float r2 = p->maxdist * p->maxdist;
            float d2 = d[2] * d[2] + (d[0] * d[0] + d[1] * d[1]);
            if (d2 < r2) vol = vol + (127.0f * (r2 - d2)) / r2;
        }
        view[k] = vol;
    }
    vol = vol * 0.75f;
    float pan = view[1] == 0.0f ? 64.0f : 127.0f * (1.0f - view[0] / view[1]);
    if (MPVCSTime < 0.0f) MPVCSTime = 0.0f;
    if (MPVCSPan < 0.0f) MPVCSPan = 0.0f;
    *i_vol = ps2_ftoi(vol * MPVCSTime);
    *i_pan = ps2_ftoi(pan * MPVCSPan);
}

static void _df_Full_MPVCS_Proc(int *i_vol, int *i_pan)
{
    PS2_FRAME(0xa0);
    PS2_STACK(float[4], d, 0x80);
    PS2_STACK(float[4], cam, 0x90);
    CameraSt *c = &CAM[ViewCamID[0]];
    memcpy(cam, c->vec, 16);
    float yaw = c->ang[1];
    float vol = 0.0f, pan_sum = 0.0f, w_sum = 0.0f, pan = 63.0f;
    int n = 0;
    struct_9a5465d2 *p = Mpvcs;
    for (int i = 0; i < MPVCSnum; i++, p++) {
        float r2 = p->maxdist * p->maxdist;
        d[0] = cam[0] - p->loc[0];
        d[1] = cam[1] - p->loc[1];
        d[2] = cam[2] - p->loc[2];
        d[3] = cam[3];
        float d2 = d[2] * d[2] + (d[0] * d[0] + d[1] * d[1]);
        if (!(d2 < r2)) continue;
        float w = (127.0f * (r2 - d2)) / r2;
        vol = vol + w;
        float a = (DF_CALC_GetAngY(cam, p->loc) - yaw) * 57.29578f;   /* degrees */
        if (!(a <= 360.0f)) a = a - 360.0f;
        if (a < 0.0f) a = a + 360.0f;
        pan = 63.0f + (63.0f * (a - 180.0f)) / 90.0f;
        if (!(pan <= 127.0f)) pan = 255.0f - pan;
        if (!(pan <= 108.0f)) pan = 108.0f;
        if (pan < 0.0f) pan = -pan;
        if (pan < 20.0f) pan = 20.0f;
        n++;
        pan_sum = pan_sum + pan * w;
        w_sum = w_sum + w;
    }
    if (n) pan = ps2_div(pan_sum, w_sum);
    if (MPVCSTime < 0.0f) MPVCSTime = 0.0f;
    if (MPVCSPan < 0.0f) MPVCSPan = 0.0f;
    *i_vol = ps2_ftoi(vol * MPVCSTime);
    *i_pan = ps2_ftoi(pan * MPVCSPan);
}

void DF_MPVCS_Stop(void)
{
    PS2_FRAME(0x30);
    for (int i = 0; i < MPVCSnum; i++) Voice_Stop(Mpvcs[i].vh, 0);
    MPVCSnum = 0;
}

void DF_MPVCS_Proc(void)
{
    if (!MPVCSnum) return;
    PS2_FRAME(0x30);
    PS2_STACK(int, vol, 0x2c);
    PS2_STACK(int, pan, 0x28);
    if (ViewNum == 1) _df_Full_MPVCS_Proc(&vol, &pan);
    else _df_Split_MPVCS_Proc(&vol, &pan);
    if (vol > 0x7f) vol = 0x7f;
    if (vol < 0) vol = 0;
    if (pan > 0x6c) pan = 0x6c;
    if (pan < 0x14) pan = 0x14;
    vol = DF_SND2_VolumeFilter_SE(vol);
    fssdsynthSeVolume(g_erv[Mpvcs[0].vh].vid, vol);   /* all points share the one voice */
    fssdsynthSePan(g_erv[Mpvcs[0].vh].vid, pan);
}

void DF_MPVC_Init(MpvcsInfoSt *pInfo, MarkerSt *pMarker)
{
    PS2_FRAME(0x30);
    MPVCSnum = 0;
    ZeroMemory32b(Mpvcs, 0x200);
    MPVCSTime = 1.0f;
    MPVCSPan = 1.0f;
    if (!pInfo->mpvcs_id) return;
    int vh = Voice_CallLoop(pInfo->mpvcs_id, pInfo->mpvcs_tone << 16 | pInfo->mpvcs_vol);
    for (int i = 0; i < 0x80; i++, pMarker++) {
        if (pMarker->type != 9) continue;
        float range;
        memcpy(&range, pMarker->wk, 4);
        if (MPVCSnum >= 0x40) continue;
        struct_9a5465d2 *p = &Mpvcs[MPVCSnum];
        p->vh = vh;
        p->loc[0] = pMarker->loc[0];
        p->loc[1] = pMarker->loc[1];
        p->loc[2] = pMarker->loc[2];
        p->maxdist = range;
        MPVCSnum++;
    }
}
