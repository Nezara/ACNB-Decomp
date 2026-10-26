/* main/loop_mmo.cpp - plays one "MMO" movie-text sequence (opening prologue dvd id 0x32, ending parts 0x34 / 0x33)
 * on its own mini game loop: reads the MMO data file whole into the buffer, arms the lib/fs fslibmmo player (sound
 * callback StaffRollSE, work memory = the rest of the buffer after the file, blur 2), optionally starts an ADS
 * stream, then runs frames (flip, pad, sound, clear to black, fsmmo_Exec) until the skip button or the player's end.
 * Original quirks kept: Cdvd_GetSize2048's result is dropped; fsmmo_Initialize's -1 (bad data) is ignored (fsmmo_Exec
 * then returns -1 and the loop ends at once); the SEMENU sound work buffer and fslibmmo's work memory are the same
 * address; StaffRollSE ignores the {0x3ea, 0x7f} voice info fslibmmo passes and plays 0x3f7. */
#include "acnb/sdk.h"

void StaffRollSE(void)
{
    Voice_Call(0x3f7, 0x7f);
}

static void main_loop(int skippad)
{
    PS2_FRAME(0x40);   /* the 9th DF_SetTestRegister argument goes to sp+0 */
    int ret;
    Pad_Init();
    do {
        Draw_FlipBufferTearing();
        Pad_UpdateData();
        sceGsSyncPath(0, 0);
        Ads_Exec();
        Snd_Exec();
        DF_SetTestRegister(0, 0, 0xff, 0, 0, 0, 1, 1, 1);
        DF_SetTestRegister(0, 0, 0xff, 0, 0, 0, 1, 1, 2);
        sceGsSyncPath(0, 0);
        DF_DrawColorScreen_NoSemi(0, 0, 0, 0x80);
        sceGsSyncPath(0, 0);
        ret = fsmmo_Exec();
        sceGsSyncPath(0, 0);
    } while (!Pad_IsTrigger(0, (unsigned short)skippad) && ret == 0);
    DF_LOOP_BlackMask();
    sceGsSyncPath(0, 0);
}

void MMO_Play(int dvdid, int adsid, void *buff, int buffsize, unsigned int skippad)
{
    PS2_FRAME(0x70);
    Cdvd_GetSize2048(dvdid);
    int size = Cdvd_GetSizeByte(dvdid);
    File_Read(dvdid, -1, (char *)buff, 1);
    char *workp = (char *)buff + size;
    fsmmo_SetSoundPlayCallback(GFN(StaffRollSE));
    DF_SND_Setting_SEMENU(workp);
    fsmmo_SetWorkMemory(workp, (unsigned int)(buffsize - (int)(workp - (char *)buff) - 0x80));
    fsmmo_Initialize(buff);
    fsmmo_SetBlurNum(2);
    Ads_Enable(1);
    Ads_Reset();
    Ads_SetVolumeRate(0xfff);
    if (adsid >= 0) Ads_Play(adsid, 1);
    main_loop((int)skippad);
    DF_SND_Remove_SEMENU();
    Ads_Stop();
    Ads_Enable(0);
}
