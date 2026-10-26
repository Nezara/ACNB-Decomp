/* src/main/game.cpp - the "game data" half of the save (g_gamedata[4], StAcrtGameData): name, money, sortie /
 * clear counts, the system option block (volumes, key config), the 16 temporary secret-item slots a mission
 * hands out (settled by Game_Sitmp_ResultMission at the end of a sortie), training records and the boot-time
 * initialisers (Game_InitAllData: game data + player info + emblems + sortie block). Arena / mission-rank hooks
 * are empty stubs in this build. */
#include "acnb/port.h"

static int __Sitmp_AddSecretParts(int player, StSecretItemTemporary *psitItem);
static void __InitGameDataWOparts(void);
static void __InitGameDataWO(void);
static void __InitGameData(void);

void Game_ResultExArena(void) {}
void Game_AddExArenaSortieCount(void) {}
void Game_ResultArena(void) {}

/* A slot with flagB bit 1 is settled: bit 0 = only on success. The slot is cleared even when it was skipped, so a
 * failed mission discards a success-only item. */
void Game_Sitmp_ResultMission(int player, int isSuccess)
{
    PS2_FRAME(0x50);
    StSecretItemTemporary *slot = g_gamedata[player].sitmp;
    for (int i = 0; i < 16; i++, slot++) {
        if (slot->type == 0 || !(slot->flagB & 2)) continue;
        if (isSuccess || !(slot->flagB & 1)) {
            if (slot->type == 1) __Sitmp_AddSecretParts(player, slot);
        }
        memset(slot, 0, 8);
        slot->type = 0;
    }
}

/* Game_IsAchievement is inlined in both Push functions; it always returns 0, so the early "already achieved"
 * return never fires. */
int Game_Sitmp_PushDefeatEnemy(int player, unsigned char idAchv)
{
    PS2_FRAME(0x40);
    if (idAchv != 0 && Game_IsAchievement(player, idAchv)) return 1;
    PS2_STACK(StSecretItemTemporary, item, 0x38);
    memset(&item, 0, 8);
    item.type = 2;
    item.idAchv = idAchv;
    item.flagB |= 1;
    item.flagB |= 2;
    StSecretItemTemporary *slot = g_gamedata[player].sitmp;
    for (int i = 0; i < 16; i++, slot++) {
        if (slot->type == 0) {
            CopyMemory(&g_gamedata[player].sitmp[i], &item, 8);
            return 1;
        }
    }
    return 0;
}

int Game_Sitmp_PushSecretParts(int player, unsigned char kindParts, unsigned char idParts, unsigned char idAchv,
                               int isOnlyClear, int isNotify)
{
    PS2_FRAME(0x80);
    if (idAchv != 0 && Game_IsAchievement(player, idAchv)) return 1;
    PS2_STACK(StSecretItemTemporary, item, 0x78);
    memset(&item, 0, 8);
    item.idAchv = idAchv;
    item.type = 1;
    item.param[0] = kindParts;
    item.param[1] = idParts;
    item.flagB |= (isOnlyClear != 0);
    item.flagB |= (isNotify == 0) ? 2 : 0;
    item.disctype = (unsigned char)Ac_GetDiscType();
    StSecretItemTemporary *slot = g_gamedata[player].sitmp;
    for (int i = 0; i < 16; i++, slot++) {
        if (slot->type == 0) {
            CopyMemory(&g_gamedata[player].sitmp[i], &item, 8);
            return 1;
        }
    }
    return 0;
}

unsigned char Game_SetMissionClearRank(void) { return 0; }
unsigned char Game_GetMissionClearRank(void) { return 0; }

void Game_AddMissionSortieCount(int player) { g_gamedata[player].numSortie++; }

void Game_ResultMission(void) {}

/* Both range branches return 0. */
int Game_IsAchievement(int player, int id)
{
    (void)player;
    (void)id;
    return 0;
}

int Game_IsStopoverSave(int player) { return g_gamedata[player].isStopoverSave != 0; }
int Game_GetRavenPointEx(StAcigGameData *pgd) { return *(int *)((char *)pgd + 0x590); }

void Game_IncClearCount(int player)
{
    short n = g_gamedata[player].clearnum;
    if (n < 0x7fff) g_gamedata[player].clearnum = n + 1;
}

short Game_GetClearCount(int player) { return g_gamedata[player].clearnum; }

void Game_SetKeyDefaultAnalog(int player)
{
    PS2_FRAME(0x20);
    Game_SetDefaultKeyConfig(&g_gamedata[player].option.keyconf);
    g_gamedata[player].option.keyconf.mode = 0;
}

void Game_SetKeyDefault(int player) { Game_SetDefaultKeyConfig(&g_gamedata[player].option.keyconf); }
int Game_IsEnableCockpitView(int player) { return g_gamedata[player].isEnableCockpit; }
void Game_SetMoney(int player, int money) { g_gamedata[player].money = money; }
int Game_GetMoney(int player) { return g_gamedata[player].money; }
char *Game_GetName(int player) { return g_gamedata[player].name; }

void Game_SetBgmVolume(int player, int vol)
{
    if (vol >= 0x80) vol = 0x7f;
    if (vol < 0) vol = 0;
    g_gamedata[player].option.bgmvol = (unsigned char)vol;
    DF_SND_SetBGMVolume(vol);
}

void Game_SetSeVolume(int player, int vol)
{
    if (vol >= 0x80) vol = 0x7f;
    if (vol < 0) vol = 0;
    g_gamedata[player].option.sevol = (unsigned char)vol;
    DF_SND_SetSEVolume(vol);
}

/* The range test is `type >= 0 || type < 8` (always true): any type reads / writes the byte at option + type. */
int Game_GetOptionValue(int player, int type)
{
    return ((unsigned char *)&g_gamedata[player].option)[type];
}

void Game_SetOptionValue(int player, int type, int val)
{
    ((unsigned char *)&g_gamedata[player].option)[type] = (unsigned char)val;
}

/* keymode -1 = the config's current mode; keyid -1 = no-op. */
void Game_SetConfigKeyIdAct(StKeyConfig *pkeyconf, int keymode, int act, int keyid)
{
    if (keymode == -1) keymode = pkeyconf->mode;
    if (keyid != -1) pkeyconf->assign[keymode][keyid] = (char)act;
}

int Game_GetConfigKeyIdAct(StKeyConfig *pkeyconf, int keymode, int act)
{
    if (keymode == -1) keymode = pkeyconf->mode;
    for (int i = 0; i < 22; i++)
        if (pkeyconf->assign[keymode][i] == act) return i;
    return -1;
}

void Game_SetDefaultKeyConfig(StKeyConfig *pkeyconf)
{
    PS2_FRAME(0x30);
    static const signed char def[2][22] = {
        {-1, -1, -1, -1, 4, 5, 13, 0, 1, 8, 9, 14, 15, 16, -1, 7, 6, 12, 10, 11, 2, 3},
        {0, 1, 2, 3, 8, 10, 13, -1, -1, -1, -1, 6, 5, 7, 4, 9, 11, 12, -1, -1, -1, -1},
    };
    pkeyconf->mode = 1;   /* bytes 1-3 (_reserved01) are left alone */
    for (int m = 0; m < 2; m++)
        for (int i = 0; i < 22; i++) pkeyconf->assign[m][i] = def[m][i];
}

void Game_InitAllDataWOparts(void)
{
    PS2_FRAME(0x10);
    ZeroMemory32b(&MyI, 0x80);
    __InitGameDataWOparts();
    DF_NPCTAB_Reset();
}

void Game_InitAllData(void)
{
    PS2_FRAME(0x10);
    ZeroMemory32b(&MyI, 0x80);
    __InitGameData();
    DF_NPCTAB_Reset();
}

/* param[0] = part kind (0-13 -> category; kind 10 unlocks categories 10 and 11, 13 -> 16), param[1] = part id
 * (0 = none). */
static int __Sitmp_AddSecretParts(int player, StSecretItemTemporary *psitItem)
{
    PS2_FRAME(0x40);
    unsigned char kind = psitItem->param[0];
    unsigned char id = psitItem->param[1];
    int acpeq = -1, acpeq2 = -1;
    if (kind < 14) {
        switch (kind) {
        case 10: acpeq = 10; acpeq2 = 11; break;
        case 11: acpeq = 12; break;
        case 12: acpeq = 13; break;
        case 13: acpeq = 16; break;
        default: acpeq = kind; break;
        }
    }
    if (acpeq != -1 && id != 0) PlayerInfo_SetExistPartsGarage(player, acpeq, id, 1);
    if (acpeq2 != -1 && id != 0) PlayerInfo_SetExistPartsGarage(player, acpeq2, id, 1);
    return 1;
}

/* Re-initialises the game data but keeps player 0's part inventory. */
static void __InitGameDataWOparts(void)
{
    PS2_FRAME(0x4810);
    PS2_STACK(StPlayerInfo, pi, 0x10);
    PlayerInfo_Save(0, &pi);
    __InitGameDataWO();
    PlayerInfo_Load(0, &pi);
}

/* Training records: the inner loops index from g_gamedata (player 0) on every pass of the player loop, so only
 * player 0's records get their MaxLv flags; players 1-3 keep the memset's zeros (same in __InitGameData). */
static void init_training_records(void)
{
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 5; j++) {
            g_gamedata[0].trainingRecord[i][j].point = 0;
            g_gamedata[0].trainingRecord[i][j].MaxLv = (i < 3) ? (j < 3) : 0;
        }
}

static void init_player_tail(StAcrtGameData *gd)
{
    memset(&gd->asmSearchFlag, 0, 0x10);
    gd->asmSearchFlag.Class.Data = (char)((gd->asmSearchFlag.Class.Data & ~4) | 4);
    gd->arenaBattle.rank = 0x66;
    gd->PasscodeInfo.keycode.data[0] = -1;
}

/* Keeps player 0's name, option block and cockpit settings across the reset (volumes from the saved option). */
static void __InitGameDataWO(void)
{
    PS2_FRAME(0x100);
    PlayerInfo_Init();
    DF_EMB_MakeTm2Header(EmblemTM2[0], 0, 0x2100, 0x3d5c);
    DF_EMB_MakeSmallEmblem(EmblemSTM2[0], EmblemTM2[0]);
    DF_EMB_Init(EmblemTM2[1], EmblemSTM2[1]);
    PS2_STACK(char[0x30], name, 0xd0);
    PS2_STACK(StSystemOption, option, 0x50);
    CopyMemory8b(name, g_gamedata[0].name, 0x30);
    option = g_gamedata[0].option;
    unsigned int cockpit = g_gamedata[0].cockpit;
    char cockpitcol = g_gamedata[0].cockpitcol;
    memset(g_gamedata, 0, 0x4f800);
    DF_SND_SetBGMVolume(option.bgmvol);
    DF_SND_SetSEVolume(option.sevol);
    for (int p = 0; p < 4; p++) {
        init_training_records();
        init_player_tail(&g_gamedata[p]);
    }
    CopyMemory8b(g_gamedata[0].name, name, 0x30);
    CopyMemory8b(&g_gamedata[0].option, &option, 0x80);
    g_gamedata[0].cockpit = cockpit;
    g_gamedata[0].cockpitcol = cockpitcol;
    DF_SND_SetMVolume(0x3fff);
    Sortie_Init();
}

static void __InitGameData(void)
{
    PS2_FRAME(0x30);
    PlayerInfo_Init();
    DF_EMB_Init(EmblemTM2[0], EmblemSTM2[0]);
    DF_EMB_Init(EmblemTM2[1], EmblemSTM2[1]);
    memset(g_gamedata, 0, 0x4f800);
    for (int p = 0; p < 4; p++) {
        StAcrtGameData *gd = &g_gamedata[p];
        gd->option.mastervol = 0x7f;
        gd->option.bgmvol = 0x7f;
        gd->option.sevol = 0x7f;
        gd->option.language = 0;
        gd->option.textmode = 1;
        gd->option.vibration = 0;
        gd->option.stereo = 1;
        gd->option.level = 0;
        DF_SND_SetBGMVolume(gd->option.bgmvol);
        DF_SND_SetSEVolume(gd->option.sevol);
        Game_SetDefaultKeyConfig(&gd->option.keyconf);
        gd->money = 0;
        init_training_records();
        init_player_tail(gd);
    }
    DF_SND_SetMVolume(0x3fff);
    Sortie_Init();
}
