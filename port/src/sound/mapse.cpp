/* src/sound/mapse.cpp - the stage's sound-effect bank (bank port 6, Sm->semap; Stage_Load builds it with
 * MixHdBd_Mix) and the end-of-sortie teardown: voices off, reverb and banks removed, the BGM / bank bookkeeping of
 * SoundManageData reset, with vblank waits between the IOP steps (their order is the contract). */
#include "acnb/sdk.h"

void DF_SND_RemoveStageSound(void)
{
    PS2_FRAME(0x10);
    DF_SND2_AllVoiceQueryOff();
    fssdsynthSeFlush();
    sceGsSyncV(0);
    sceGsSyncV(0);
    DF_SND_RemoveReveb();
    fssdsynthPortSoundOff(6);
    fssdsynthPortSoundOff(4);
    fssdsynthSeFlush();
    sceGsSyncV(0);
    sceGsSyncV(0);
    int b = Sm->semap;
    if (0 <= b && b < 16) {
        fssdsynthRemoveBank(b);
        Sm->semap = -1;
    }
    sceGsSyncV(0);
    sceGsSyncV(0);
    DF_SND_Remove_SECOM();
    sceGsSyncV(0);
    sceGsSyncV(0);
    DF_SND2_ClearVoiceData();
    sceGsSyncV(0);
    sceGsSyncV(0);
    sceGsSyncV(0);
    sceGsSyncV(0);
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
    Sm->flm = 0;
    sceGsSyncV(0);
    sceGsSyncV(0);
}

void DF_SND_Setting_MAPSE(void *hd, unsigned int hd_size, void *bd, unsigned int bd_size)
{
    PS2_FRAME(0x10);
    if (bd_size == 0) return;
    Sm->semap = fssdsynthAddBankFromEE2(6, (char *)hd, hd_size, (char *)bd, bd_size, 0);
    while (!fssdsynthSifTransSync()) {
    }
    Sm->porttbl[Sm->semap] = 6;
}
