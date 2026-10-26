/* src/main/save.cpp - one player's memory-card image (StAcrtSaveData, 0x1e000 bytes): the game data, emblem image,
 * player info (parts ownership) and destroyed-object memory, copied in (Save_DistributeSaveData, after a load) or
 * out (Save_MakeSaveData, before a save). */
#include "acnb/sdk.h"
#include "acnb/hooks.h"

void Save_DistributeSaveData(StAcrtSaveData *psd, int player)
{
    PS2_FRAME(0x30);
    FlushCache(0);
    CopyMemory8b(&g_gamedata[player], psd, 0x13e00);
    CopyMemory8b(EmblemTM2[player], psd->emblemdata, 0x4420);
    PlayerInfo_Load(player, &psd->playerinfo);
    DestObjMem_Load(&psd->destobj);
    FlushCache(0);
    if (hook_distribute_save) hook_distribute_save();   /* port hook (game.unlock_parts) */
}

void Save_MakeSaveData(StAcrtSaveData *psd, int player, int isStopover)
{
    PS2_FRAME(0x40);
    FlushCache(0);
    ZeroMemory(psd, 0x1e000);
    CopyMemory8b(psd, &g_gamedata[player], 0x13e00);
    CopyMemory8b(psd->emblemdata, EmblemTM2[player], 0x4420);
    psd->gamedata.isStopoverSave = isStopover != 0;
    PlayerInfo_Save(player, &psd->playerinfo);
    DestObjMem_Save(&psd->destobj);
    FlushCache(0);
}
