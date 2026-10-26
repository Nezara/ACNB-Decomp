/* src/sound/semenu.cpp - the menu sound-effect bank (bank port 5, file 0x9c5, registered as Sm->semap) with its
 * reverb, and the menu sound teardown. */
#include <stdint.h>
#include "acnb/sdk.h"

void DF_SND_Remove_SEMENU(void)
{
    PS2_FRAME(0x10);
    DF_SND2_AllVoiceQueryOff();
    fssdsynthSeFlush();
    sceGsSyncV(0);
    sceGsSyncV(0);
    Bgm_RemoveReverb();
    fssdsynthPortSoundOff(0);
    fssdsynthPortSoundOff(5);
    fssdsynthSeFlush();
    sceGsSyncV(0);
    sceGsSyncV(0);
    int b = Sm->semap;
    if (0 <= b && b < 16) {
        fssdsynthRemoveBank(b);
        Sm->semap = -1;
    }
    DF_SND2_ClearVoiceData();
    sceGsSyncV(0);
    sceGsSyncV(0);
}

/* the file is read 0x100 past the work buffer aligned down to 0x100 */
void DF_SND_Setting_SEMENU(char *workbuf)
{
    PS2_FRAME(0x30);
    char *f = (char *)(((uintptr_t)workbuf & 0xffffff00u) + 0x100);
    File_Read(0x9c5, -1, f, 1);
    StBgmHead *bp = (StBgmHead *)f;
    Sm->semap = fssdsynthAddBankFromEE2(5, f + bp->boffs[0], bp->bsize[0], f + bp->boffs[1], bp->bsize[1], 0);
    while (!fssdsynthSifTransSync()) {
    }
    while (!fssdsynthGetTransStatus(0)) {
    }
    Sm->porttbl[Sm->semap] = 5;
    Sm->rid[0] = (char)fssdsynthAddReverb(5, 5000, 5000, 0, 0, 1000, 1000, 0);
}
