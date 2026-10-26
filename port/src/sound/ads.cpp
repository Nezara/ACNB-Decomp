/* src/sound/ads.cpp - ADS: ADX BGM streamed from the disc (\ADS\ADSnnnn.ADX, 32 slots), Versus' battle music.
 * Ads_Init (boot) looks the files up, Ads_Play asks the IOP stream module (lib/fs fslibsdads, interpreted) to set the
 * stream up, Ads_Exec syncs with it every frame, starts the playback once the setup is done and pushes volume
 * changes (BGM option volume x the rate adsvc.c derives from the camera). The PCM-filter branches hang off s_pFilter,
 * which nothing in the game ever sets. */
#include <string.h>
#include "acnb/sdk.h"
#include "acnb/pf.h"
#include "acnb/ps2fp.h"

void Ads_SetVolumeRate(short rate)
{
    if (!(rate < 0x1000)) rate = 0xfff;
    if (rate < 0) rate = 0;
    s_ads.vol_rate = rate;
    s_ads.vol_chg = 1;
}

void Ads_Stop(void)
{
    PS2_FRAME(0x10);
    s_ads.on = 0;
    fssdadsStop(1);
    fssdadsClose(1);
    if (s_pFilter) s_pFilter->PcmReceive = 0;
}

/* (index is not range-checked) */
int Ads_Play(int index, int isLoop)
{
    PS2_FRAME(0x30);
    if (!s_ads.enable) return 0;
    float v = ps2_sqrt((float)SoundManageData.bgmvol / 127.0f);
    struct_9b06e75c *f = &s_cdfileAds[index];
    int l = ps2_ftoi(20480.0f * v);
    if (f->lsn == 0) return 0;
    fssdadsVolume(1, (unsigned short)l, (unsigned short)l);
    fssdadsSetupWithLsn(1, f->lsn, 0x10000);
    if (s_ads.on == 0) s_ads.on = isLoop ? 1 : 2;
    if (s_pFilter) {
        s_pFilter->PcmReceive = 1;
        for (int i = 0; i < 10; i++) IIR_FilterInitialize(&s_pFilter->aFilter[i]);
    }
    return 1;
}

void Ads_Exec(void)
{
    PS2_FRAME(0x10);
    if (!s_ads.enable) return;
    if (!fssdadsSync()) return;
    if (s_ads.on == 2) {
        fssdadsPlay(1, 0);
        s_ads.on = 0;
        s_ads.vol_chg = 1;
    } else if (s_ads.on == 1) {
        fssdadsPlay(1, 1);
        s_ads.on = 0;
        s_ads.vol_chg = 1;
    }
    if (s_ads.vol_chg) {
        float f = 32767.0f * ((float)SoundManageData.bgmvol / 127.0f);
        f = f * ((float)s_ads.vol_rate / 4095.0f);
        unsigned int l = !(2147483648.0f <= f) ? (unsigned int)ps2_ftoi(f)
                                                : (unsigned int)ps2_ftoi(f - 2147483648.0f) | 0x80000000u;
        fssdadsVolume(1, (unsigned short)l, (unsigned short)l);
        s_ads.vol_chg = 0;
    }
    if (s_pFilter) {
        unsigned int u = s_pFilter->uiPos + 0x600;
        if (u < 0x4000) s_pFilter->uiPos = u;
        if (s_pFilter->PcmReceive == 1 && fsadsGetPcm(s_pFilter->aPcmReceiveBuffer[(int)s_pFilter->PcmReceiveWhich]) >= 0) {
            s_pFilter->PcmReceiveWhich = s_pFilter->PcmReceiveWhich == 0;
            s_pFilter->uiPos = 0;
        }
    }
}

void Ads_Reset(void)
{
    PS2_FRAME(0x10);
    fssdadsStop(1);
    s_ads.on = 0;
    s_ads.vol_rate = 0xfff;
    s_ads.vol_chg = 0;
}

void Ads_Enable(int isEnable)
{
    s_ads.enable = (unsigned char)isEnable;
}

int Ads_InitStage(void)
{
    return 1;
}

/* look up the 32 ADS files (retrying while the drive reports an error); song_num = last found + 1 */
int Ads_Init(void)
{
    PS2_FRAME(0x90);
    PS2_STACK(char[80], buf, 0x40);
    memset(&s_ads, 0, 0x10);
    memset(s_cdfileAds, 0, 0x480);
    s_ads.enable = 1;
    int last = -1;
    for (int i = 0; i < 32; i++) {
        pf_sprintf_sd(buf, "%s\\ADS%04d.ADX;1", "\\ADS", i);
        if (!sceCdSearchFile(&s_cdfileAds[i], buf)) {
            for (;;) {
                if (!sceCdGetError()) {
                    memset(&s_cdfileAds[i], 0, 0x24);
                    break;
                }
                if (sceCdSearchFile(&s_cdfileAds[i], buf)) break;
            }
        }
        if (s_cdfileAds[i].lsn) last = i;
        FlushCache(0);
    }
    s_ads.song_num = (unsigned char)(last + 1);
    return 1;
}
