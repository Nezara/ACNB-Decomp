/* src/sound/adsvc.cpp - ADS volume control: a stage script (DF_ADSVC_SetParam) can fade the BGM with the camera
 * height; DF_ADSVC_Proc runs the selected type every frame through FuncTab (type 0 = nothing, the default; type 1
 * = full volume within range_min of base_ht, silent beyond range_max, linear in between). */
#include "acnb/ps2fp.h"

/* (single view only) */
static void DF_ADSVC_Type1(void)
{
    PS2_FRAME(0x10);
    if (ViewNum != 1) return;
    float d = CAM[ViewCamID[0]].vec[1] - ADSVC_MNG.base_ht;
    if (d < 0.0f) d = -d;
    int r;
    if (d < ADSVC_MNG.range_min) {
        r = 0xfff;
    } else if (!(d < ADSVC_MNG.range_max)) {
        r = 0;
    } else {
        float t = (d - ADSVC_MNG.range_min) / ADSVC_MNG.range_diff;
        if (!(t <= 1.0f)) t = 1.0f;
        r = ps2_ftoi(4095.0f * (1.0f - t));
    }
    if (ADSVC_MNG.vol_rate != r) {
        ADSVC_MNG.vol_rate = r;
        Ads_SetVolumeRate((short)r);
    }
}

#ifdef ACNB_SOURCEPORT
void (*FuncTab[2])(void) = {0, DF_ADSVC_Type1};
#endif

void DF_ADSVC_SetParam(float base_ht, float min, float max)
{
    ADSVC_MNG.base_ht = base_ht;
    ADSVC_MNG.range_min = min;
    ADSVC_MNG.range_max = max;
    ADSVC_MNG.type = 1;
    ADSVC_MNG.range_diff = max - min;
}

/* (type is not range-checked) */
void DF_ADSVC_Proc(void)
{
    PS2_FRAME(0x10);
    void (*fn)(void) = FuncTab[ADSVC_MNG.type];
    if (fn) gcall(fn);
}

void DF_ADSVC_Init(void)
{
    PS2_FRAME(0x10);
    ZeroMemory32b(&ADSVC_MNG, 0x10);
    ADSVC_MNG.vol_rate = 0xfff;
}
