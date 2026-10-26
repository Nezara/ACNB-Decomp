/* src/main/sysdata.cpp - system data loads at sortie begin (sysdata.c). Native so far: SysData_ReadMagicTMX, the magic
 * (weapon effect) textures: NL data 0x11 and 1 are read into g_pbufRead and uploaded (DF_ReadTMXPointer), then the two
 * 64 x 1024 PSMCT32 pages at TBP 0x2e40 / 0x3240 are read back into a fixed high-RAM region (0x1c77a00, 0x40000 bytes
 * apart) that MagicTexP points at; Stage_Load re-uploads them from there every frame (BLTtagSFX).
 * Also native: DF_GAME_SystemDataRead (boot: the SystemData block and the table pointers into it, spell / magic /
 * lighting tables, the magic textures), SysData_LoadMagicData (magic table per stage), SysData_ReadMagicModel (TFM
 * model buffer). */
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
/* the original's fixed readback region 0x1c77a00..0x1cf7a00 (two 0x40000-byte pages) */
alignas(16) static char s_magic_tex[0x80000];
#define SYSDATA_MAGIC_TEX s_magic_tex
#else
#define SYSDATA_MAGIC_TEX ((char *)(uintptr_t)0x01c77a00u)
#endif

/* the DF_ReadTMXPointer / NL_ReadData results are ignored */
void SysData_ReadMagicTMX(void)
{
    PS2_FRAME(0x10);
    NL_ReadData(0x11, g_pbufRead, -1, 0);
    DF_ReadTMXPointer(g_pbufRead, 1);
    NL_ReadData(1, g_pbufRead, -1, 0);
    DF_ReadTMXPointer(g_pbufRead, 1);
    DF_StoreImage(SYSDATA_MAGIC_TEX, 0x2e40, 0, 0, 0, 0x40, 0x400);
    DF_StoreImage(SYSDATA_MAGIC_TEX + 0x40000, 0x3240, 0, 0, 0, 0x40, 0x400);
    MagicTexP = SYSDATA_MAGIC_TEX;
}

/* the magic model (TFM) file into tfm_buffer; the size query's result is unused */
void *SysData_ReadMagicModel(void)
{
    PS2_FRAME(0x10);
    Cdvd_GetSize2048(0xd);
    NL_ReadData(0xd, tfm_buffer, -1, 0);
    return tfm_buffer;
}

/* the magic table: file 0x17 for mode 1, else 4 */
void SysData_LoadMagicData(int mode)
{
    PS2_FRAME(0x20);
    ZeroMemory32b(MagicBuffer, 0xa000);
    if (mode == 1) NL_ReadData(0x17, MagicBuffer, -1, 0);
    else NL_ReadData(4, MagicBuffer, -1, 0);
    MagicData2 = (decltype(MagicData2))MagicBuffer;
}

/* boot: the SystemData block (file 0x1a; the table pointers are offsets into it), the joint-control data, spell /
 * magic / lighting tables and the magic textures (same as SysData_ReadMagicTMX) */
void DF_GAME_SystemDataRead(void)
{
    PS2_FRAME(0x10);
    Cdvd_GetSize2048(0x1a);
    NL_ReadData(0x1a, SystemData, -1, 0);
    char *sd = SystemData;
    WeaponData = (decltype(WeaponData))(sd + *(unsigned int *)(sd + 0x10));
    DoorData = (decltype(DoorData))(sd + *(unsigned int *)(sd + 0x30));
    NowloadMission = (decltype(NowloadMission))(sd + *(unsigned int *)(sd + 0x50));
    HitEffectData = (decltype(HitEffectData))(sd + *(unsigned int *)(sd + 0x60));
    FriendTab = (decltype(FriendTab))(sd + *(unsigned int *)(sd + 0x70));
    MissionNameP = sd + *(unsigned int *)(sd + 0x90);
    LenstexTopp = sd + *(unsigned int *)(sd + 0x40);
    Map_JconData((struct_5a3002c3 *)(sd + *(unsigned int *)(sd + 0x20)));
    FlushCache(0);
    NL_ReadData(5, SpellBuffer, -1, 0);
    SpellData2 = (decltype(SpellData2))SpellBuffer;
    ZeroMemory32b(MagicBuffer, 0xa000);
    NL_ReadData(4, MagicBuffer, -1, 0);
    MagicData2 = (decltype(MagicData2))MagicBuffer;
    NL_ReadData(0xf, LitData, -1, 0);
    sceGsSyncPath(0, 0);
    NL_ReadData(0x11, g_pbufRead, -1, 0);
    DF_ReadTMXPointer(g_pbufRead, 1);
    NL_ReadData(1, g_pbufRead, -1, 0);
    DF_ReadTMXPointer(g_pbufRead, 1);
    DF_StoreImage(SYSDATA_MAGIC_TEX, 0x2e40, 0, 0, 0, 0x40, 0x400);
    DF_StoreImage(SYSDATA_MAGIC_TEX + 0x40000, 0x3240, 0, 0, 0, 0x40, 0x400);
    MagicTexP = SYSDATA_MAGIC_TEX;
    DF_MAGIC2_ReadTFM();
}
