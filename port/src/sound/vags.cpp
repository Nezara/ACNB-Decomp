/* src/sound/vags.cpp - message speech as raw VAG through EmsVag: finds the sample in the common voice bundle (type
 * 1), a caller's bundle (type 2) or the stage's (by the stage sound table), reads its header and plays it at the SE
 * volume (EmsVag_VagPlay applies the SE volume once more). s_vags.enable switches it per stage. */
#include <stdint.h>

int Vags_Stop(void)
{
    return EmsVag_VagStop();
}

/* (EmsVag_GetVagInfo returns 0 when the program is missing; only a negative result is rejected, so the play goes
 * ahead with the stale stack words of the info block) */
int Vags_Play(int sss_id, int type, void *ptr)
{
    PS2_FRAME(0x20);
    PS2_STACK(struct_83eb889b, info, 0x10);
    if (!s_vags.enable) return 0;
    if (sss_id < 0) return 0;
    char *hd, *bd;
    int prog;
    if (type == 1 || type == 2) {
        char *b = type == 1 ? (char *)(uintptr_t)Sm->comt : (char *)ptr;
        if (!b) return 0;
        hd = b + ((int *)b)[2];
        bd = b + ((int *)b)[3];
        prog = sss_id;
    } else {
        char *t = Sm->MAPTTopP;
        if (!t) return 0;
        hd = t + ((int *)t)[0];
        bd = t + ((int *)t)[1];
        prog = Array->stable[sss_id];
    }
    if (prog < 0) return 0;
    if (EmsVag_GetVagInfo(hd, bd, prog % 10000, &info) < 0) return 0;
    unsigned int sevol = SoundManageData.sevol;
    unsigned int lv = sevol * info.lvol / 127u;
    unsigned int rv = sevol * info.rvol / 127u;
    EmsVag_VagPlay(info.buff, info.size, info.sample, (unsigned short)lv, (unsigned short)rv);
    return 1;
}

void Vags_Enable(int isEnable)
{
    s_vags.enable = (unsigned char)isEnable;
}

void Vags_CleanupStage(void)
{
    Sm->MAPTTopP = 0;
    Sm->comt = 0;
}

int Vags_InitStage(void)
{
    return 1;
}

int Vags_Init(void)
{
    PS2_FRAME(0x10);
    s_vags.enable = 1;
    return EmsVag_Init() == 1;
}
