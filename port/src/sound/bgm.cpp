/* src/sound/bgm.cpp - menu / system BGM on the IOP sequencer (SQ + HD/BD bank, reverb preset) with a software fade
 * kept in SoundManageData: sq_now (-1 = none), sq_fade (> 0 fading out, growing; < 0 fading in), sq_vol (0..0x7f).
 * Battle music is ADS (ads.cpp); in a match Bgm_Exec only sees sq_now == -1. */
#include "acnb/sdk.h"

/* the fade step + volume update the original inlines at every use */
static inline void bgm_update_volume(void)
{
    if (Sm->sq_now == -1) return;
    if (Sm->sq_fade != 0) Sm->sq_vol = Sm->sq_vol - Sm->sq_fade * 4;
    if (Sm->sq_vol < 0) {
        Sm->sq_vol = 0;
        if (Sm->sq_fade > 0) Sm->sq_fade = 0;
    }
    if (!(Sm->sq_vol < 0x80)) {
        Sm->sq_vol = 0x7f;
        if (Sm->sq_fade < 0) Sm->sq_fade = 0;
    }
    int v = Sm->sq_vol * (Sm->bgmvol * 127) / 127 / 127;
    fssdsynthSqVolume(Sm->sid[Sm->sq_now], (short)v);
}

static inline void bgm_remove_reverb(void)
{
    int r = Sm->rid[0];
    if (0 <= r && r < 4) {
        fssdsynthRemoveReverb(r);
        Sm->rid[0] = -1;
    }
}

void Bgm_FadeOutStart(void)
{
    PS2_FRAME(0x10);
    if (Sm->sq_now != -1) Sm->sq_fade = 1;
    bgm_update_volume();
}

void Bgm_Stop(void)
{
    PS2_FRAME(0x10);
    fssdsynthStopSq(Sm->sid[0]);
    Sm->sq_now = -1;
}

/* fade the current sequence back in when it is still the one registered (and not at its end), else restart it */
int Bgm_Play(void)
{
    PS2_FRAME(0x10);
    if (Sm->sq_now == 0 && fssdsynthCheckSqEos(Sm->sid[Sm->sq_now])) {
        if (Sm->sq_now != -1) Sm->sq_fade = -1;
        bgm_update_volume();
    } else {
        Sm->SQstat[0] = 1;
        Sm->sq_now = 0;
        Sm->sq_fade = 0;
        bgm_update_volume();
        Sm->sq_vol = 0x7f;
        fssdsynthPlaySq(Sm->sid[0], 1);
        Sm->sq_vol = 0x7f;
        bgm_update_volume();
    }
    return 0;
}

void Bgm_RemoveReverb(void)
{
    PS2_FRAME(0x10);
    bgm_remove_reverb();
}

/* (bid[] is reset to 0, sid[] to -1) */
void Bgm_RemoveSeq(void)
{
    PS2_FRAME(0x10);
    int n = Sm->sq_now;
    if (0 <= n && n < 4 && Sm->sid[n] >= 0) fssdsynthStopSq(Sm->sid[n]);
    if (Sm->sid[0] >= 0) {
        fssdsynthRemoveSq(Sm->sid[0]);
        sceGsSyncV(0);
        sceGsSyncV(0);
    }
    if (Sm->bid[0] >= 0) {
        fssdsynthRemoveBank(Sm->bid[0]);
        sceGsSyncV(0);
        sceGsSyncV(0);
    }
    Sm->sq_vol = 0;
    Sm->sq_now = -1;
    Sm->bid[0] = 0;
    Sm->bid[1] = 0;
    Sm->bid[2] = 0;
    Sm->bid[3] = 0;
    Sm->sid[0] = -1;
    Sm->sid[1] = -1;
    Sm->sid[2] = -1;
    Sm->sid[3] = -1;
    Sm->SQstat[0] = 0;
    Sm->SQstat[1] = 0;
    Sm->SQstat[2] = 0;
    Sm->SQstat[3] = 0;
    Sm->sq_fade = 0;
}

/* load BGM file id + 0xa28 to tp (header: HD / BD / SQ offsets at 8 / 0xc / 0x10, sizes at 0x28 / 0x2c / 0x30),
 * register bank and sequence on port 0 and set the BGM reverb */
void Bgm_RegisterSeq(int id, char *tp)
{
    PS2_FRAME(0x20);
    StBgmHead *bp = (StBgmHead *)tp;
    File_Read(id + 0xa28, -1, tp, 1);
    FlushCache(0);
    Sm->bid[0] = fssdsynthAddBankFromEE2(0, tp + bp->boffs[0], bp->bsize[0], tp + bp->boffs[1], bp->bsize[1], 0);
    while (!fssdsynthSifTransSync()) {
    }
    while (!fssdsynthGetTransStatus(0)) {
    }
    Sm->sid[0] = fssdsynthAddSqFromEE(0, Sm->bid[0], tp + bp->boffs[2], bp->bsize[2]);
    while (!fssdsynthSifTransSync()) {
    }
    while (!fssdsynthGetTransStatus(0)) {
    }
    Sm->sq_now = -1;
    bgm_remove_reverb();
    Sm->rid[0] = (char)fssdsynthAddReverb(2, 0x3000, 0x3000, 0x28, 0x28, 0x3800, 0x3800, 0);
    fssdsynthSetReverb(Sm->rid[0]);
}

/* every frame: fade step; a sequence the IOP reports as not at its end is (re)started */
void Bgm_Exec(void)
{
    PS2_FRAME(0x10);
    if (Sm->sq_now == -1) return;
    if (Sm->sq_fade > 0) Sm->sq_fade++;
    bgm_update_volume();
    if (fssdsynthCheckSqEos(Sm->sid[Sm->sq_now]) == 0) {
        Sm->SQstat[Sm->sq_now] = 1;
        fssdsynthPlaySq(Sm->sid[Sm->sq_now], 1);
        bgm_update_volume();
    }
}
