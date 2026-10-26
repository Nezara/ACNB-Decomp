/* src/main/player_info.cpp - the "player info" half of the save data (s_playerinfo[4]): the garage part inventory
 * (17 categories x 256 ids, a 4-byte StPartsState each: five tune-level nibbles, a 2-bit exist state 0 none /
 * 1 garage / 2 shop / 3 reposit and a used bit), the 3 AC assemblies per player, the AC names and the current AC
 * slot. Callers are the menus (garage / shop / name entry / assembly) and the save-data code. No range checks
 * anywhere: indices come straight from the callers. */
#include "acnb/port.h"

static StPartsState *pinfo_state(int player, int acpeq, int id)
{
    return &s_playerinfo[player].parts_stat.tbl[acpeq][id];
}

/* Copies up to 48 bytes and zero-fills the rest; a 48-character name gets no terminator. */
void PlayerInfo_SetAcName(int player, char acno, char *pname)
{
    char *dst = s_playerinfo[player].ac_name[(signed char)acno];
    int i = 0;
    for (; i < 0x30 && pname[i] != 0; i++) dst[i] = pname[i];
    for (; i < 0x30; i++) dst[i] = 0;
}

void PlayerInfo_SetCurrentAcNo(int player, char acno) { s_playerinfo[player].cur_ac_no = acno; }
char *PlayerInfo_GetAcName(int player, char acno) { return s_playerinfo[player].ac_name[(signed char)acno]; }
char PlayerInfo_GetCurrentAcNo(int player) { return s_playerinfo[player].cur_ac_no; }

void PlayerInfo_AddShopParts(int player, int acpeq, int id)
{
    StPartsState *ps = pinfo_state(player, acpeq, id);
    if (ps->exist == 0) ps->exist = 2;
}

void PlayerInfo_SetPartsUsed(int player, int acpeq, int id, int isUsed)
{
    pinfo_state(player, acpeq, id)->used = (isUsed != 0);
}

void PlayerInfo_SetExistPartsReposit(int player, int acpeq, int id, int enable)
{
    pinfo_state(player, acpeq, id)->exist = enable ? 3 : 0;
}

void PlayerInfo_SetExistPartsShop(int player, int acpeq, int id, int enable)
{
    pinfo_state(player, acpeq, id)->exist = enable ? 2 : 0;
}

void PlayerInfo_SetExistPartsGarage(int player, int acpeq, int id, int enable)
{
    pinfo_state(player, acpeq, id)->exist = (enable != 0);
}

void PlayerInfo_SetPartsTuneLv(int player, int acpeq, int id, int item, unsigned char lv)
{
    StPartsState *ps = pinfo_state(player, acpeq, id);
    switch (item) {
    case 0: ps->tlv1 = lv & 0xf; break;
    case 1: ps->tlv2 = lv & 0xf; break;
    case 2: ps->tlv3 = lv & 0xf; break;
    case 3: ps->tlv4 = lv & 0xf; break;
    case 4: ps->tlv5 = lv & 0xf; break;
    }
}

int PlayerInfo_IsPartsUsed(int player, int acpeq, int id) { return pinfo_state(player, acpeq, id)->used == 1; }
int PlayerInfo_IsExistPartsReposit(int player, int acpeq, int id) { return pinfo_state(player, acpeq, id)->exist == 3; }
int PlayerInfo_IsExistPartsShop(int player, int acpeq, int id) { return pinfo_state(player, acpeq, id)->exist == 2; }
int PlayerInfo_IsExistPartsGarage(int player, int acpeq, int id) { return pinfo_state(player, acpeq, id)->exist == 1; }
int PlayerInfo_IsExistParts(int player, int acpeq, int id) { return pinfo_state(player, acpeq, id)->exist != 0; }

unsigned char PlayerInfo_GetPartsTuneRate(int player, int acpeq, int id)
{
    StPartsState *ps = pinfo_state(player, acpeq, id);
    return (unsigned char)((ps->tlv1 + ps->tlv2 + ps->tlv3 + ps->tlv4 + ps->tlv5) * 10);
}

unsigned char PlayerInfo_GetPartsTuneLv(int player, int acpeq, int id, int item)
{
    StPartsState *ps = pinfo_state(player, acpeq, id);
    switch (item) {
    case 0: return ps->tlv1;
    case 1: return ps->tlv2;
    case 2: return ps->tlv3;
    case 3: return ps->tlv4;
    case 4: return ps->tlv5;
    }
    return 0;
}

short PlayerInfo_GetAssemblyPartsId(int player, int acno, int acpeq)
{
    return s_playerinfo[player].asmbly[acno].idParts[acpeq];
}

AcAssemblyPartsSt *PlayerInfo_GetAssemblyInfo(int player, int acno) { return &s_playerinfo[player].asmbly[acno]; }

void PlayerInfo_Load(int player, StPlayerInfo *ppi) { CopyMemory(&s_playerinfo[player], ppi, 0x4800); }
void PlayerInfo_Save(int player, StPlayerInfo *ppi) { CopyMemory(ppi, &s_playerinfo[player], 0x4800); }

int PlayerInfo_InitPlayer(int player)
{
    PS2_FRAME(0x10);
    ZeroMemory(&s_playerinfo[player], 0x4800);
    return 1;
}

int PlayerInfo_Init(void)
{
    PS2_FRAME(0x10);
    ZeroMemory(s_playerinfo, 0x12000);
    return 1;
}
