/* src/main/sortie.cpp - the per-sortie game state block. s_sortie holds the stage constants (map, mode, players,
 * mission ids), the work flags, the mission result and the settlement, plus pointers to the four ACs' assemblies /
 * options; g_sortie points into it. Sortie_BeginStage copies the menu -> battle hand-over (g_nextstage) into it,
 * Sortie_EndStage settles the result, updates the save data and hands it back to the menus (g_nextmenu). */
#include "acnb/port.h"

unsigned int Sortie_GetCombatTime(void)
{
    PS2_FRAME(0x10);
    return CombatRecord_GetRecord()->combatTime;
}

void Sortie_SetEscapeSortie(int isEscape)
{
    g_sortie.psettle->isEscape = (isEscape != 0);
}

void Sortie_SetVsTimeOver(int isTimeOver)
{
    g_sortie.psettle->isVsTimerOver = (isTimeOver != 0);
}

void Sortie_SetVsWinner(int player, int isWin, int isAreaOver)
{
    g_sortie.psettle->playerVs[player].win = (isWin != 0);
    g_sortie.psettle->playerVs[player].areaover = (isAreaOver != 0);
}

MyOptionSt *Sortie_GetAcOptionForInitializer(int player) { return s_sortie.pacopt[player]; }
AcAssemblyPartsSt *Sortie_GetAcAssemblyForInitializer(int player) { return s_sortie.pasmbly[player]; }
MyOptionSt *Sortie_GetAcOption(int player) { return s_sortie.pacopt[player]; }
AcAssemblyPartsSt *Sortie_GetAcAssembly(int player) { return s_sortie.pasmbly[player]; }

void Sortie_EndStage(void)
{
    PS2_FRAME(0x20);
    int ok = (g_sortie.pwork->isSuccess != 0);
    unsigned char mode = g_sortie.pconstant->modeSortie;
    int isArena = (mode == 2 || mode == 3);
    MyI.replaymode = 0;
    if (MyI.vsnext == 0) {
        MissRes_Settle(g_sortie.pmisres, g_sortie.psettle, isArena, ok);
        __UpdataSaveDataAtEndStage(ok);
        mode = g_sortie.pconstant->modeSortie;
        if (mode == 2) {
            char w = g_sortie.pmisres->winnerVs;
            if (w == 0) CombatRecord_OnWin(1);
            else if (w != -1) CombatRecord_OnWin(0);
        } else if (mode == 0) {
            if (ok) CombatRecord_OnWin(1);
        }
        CombatRecord_Save();
        StSortieConstant *c = g_sortie.pconstant;
        mode = c->modeSortie;
        if (mode == 3 || mode == 2 || mode == 0) {
            if (c->flagFree) MyI.mainproc = 2;
            else if (g_sortie.pmisres->mapCnt != 0) MyI.mainproc = 1;
        }
    }
    ZeroMemory(&g_nextmenu, 0xc0);
    g_nextmenu.miss_id = g_sortie.pconstant->idMission;
    g_nextmenu.miss_stat = g_sortie.pwork->statMission;
    CopyMemory(&g_nextmenu.misres, g_sortie.pmisres, 0x80);
}

void Sortie_BeginStage(void)
{
    PS2_FRAME(0x20);
    if (MyI.vsnext == 0) {
        StNextStage *ns = &g_nextstage;
        StSortieConstant *c = &s_sortie.constant;
        ZeroMemory(&s_sortie, 0x1f0);
        g_sortie.pwork->isEnablePause = 1;
        for (int i = 0; i < 4; i++) {
            s_sortie.pasmbly[i] = &ns->asmbly[i];
            s_sortie.pacopt[i] = &ns->acopt[i];
        }
        unsigned int dobj = ns->dobj_situ;
        ns->ev_situ = dobj;
        c->map = ns->stage;
        c->qtime = (unsigned char)ns->qtime;
        c->modeSortie = ns->sortie_mode;
        c->isArrangeBgm = ns->arrange_bgm;
        ns->chr_situ = dobj;
        c->situDobj = (unsigned char)dobj;
        c->situChr = (unsigned char)ns->chr_situ;
        c->situEv = (unsigned char)ns->ev_situ;
        for (int i = 0; i < 4; i++) {
            c->player[i].belonging = ns->ac_belong[i];
            c->player[i].price = ns->price[i];
        }
        c->player[0].money = Game_GetMoney(0);
        c->idMission = ns->miss_id;
        c->idClient = ns->client_id;
        for (int i = 0; i < 4; i++) c->idRanker[i] = ns->ranker_id[i];
        c->isSplit = ns->is_split;
        c->rateRescue = (float)ns->rescue_rate / 100.0f;
        c->lvFake = ns->fake_rate;
        c->flagFree = ns->free_mode;
        for (int i = 0; i < 4; i++)
            if (c->player[i].belonging == 0) c->player[i].belonging = (unsigned char)(i + 1);
        s_sortie.work.statMission = 0;
        StSettlement *st = &s_sortie.settle;
        for (int i = 0; i < 4; i++) {
            st->rrvApRate[i] = ns->rrvApRate[i];
            st->rrvAmm[i] = ns->rrvAmm[i];
            st->rrvTime[i] = ns->rrvTime[i];
        }
        st->paySpPartsKind = ns->parts_kind_sppay;
        st->paySpPartsId = ns->parts_id_sppay;
        st->payBase = ns->base_pay;
        st->paySuccess = ns->success_pay;
        st->paySpecialAdd = ns->spadd_pay;
        st->paySpecialSub = ns->spsub_pay;
        st->rpntSuccess = ns->rpntSuccess;
        st->rpntFail = ns->rpntFail;
        st->isFreeExpend = ns->free_expend;
        CombatRecord_Init();
        CombatRecord_OnSortie();
        Game_AddMissionSortieCount(0);
    } else {
        g_sortie.psettle->timeMission = 0;
    }
    SoundManageData.scene_bgmvol = g_nextstage.bgm_volume == 0 ? 0x7f : g_nextstage.bgm_volume;
    Pad_SetSplit(g_nextstage.is_split);
}

int Sortie_Init(void)
{
    PS2_FRAME(0x10);
    ZeroMemory(&s_sortie, 0x10);
    g_sortie.pconstant = &s_sortie.constant;
    g_sortie.pwork = &s_sortie.work;
    g_sortie.pmisres = &s_sortie.misres;
    g_sortie.psettle = &s_sortie.settle;
    return 1;
}

/* marks the player's (AC 0) parts as used: the 16 part ids (kinds 0xe / 0xf are stored as 0xc / 0xd) and the
 * option bits 1..31 (kind 0x10) */
static void __BecomeUsedParts(void)
{
    PS2_FRAME(0x60);
    AcAssemblyPartsSt *a = s_sortie.pasmbly[0];
    for (int k = 0; k <= 0x10; k++) {
        if (k == 0x10) {
            for (int b = 1; b < 0x20; b++)
                if (a->option & (1u << (b - 1))) PlayerInfo_SetPartsUsed(0, 0x10, b, 1);
        } else {
            int kind = k;
            if (k == 0xf) kind = 0xd;
            else if (k == 0xe) kind = 0xc;
            int id = a->idParts[k];
            if (id != 0) PlayerInfo_SetPartsUsed(0, kind, id, 1);
        }
    }
}

/* Game_ResultMission / Game_Get/SetMissionClearRank are empty stubs in this build (DWARF (void), the original passes
 * (0, idMission, isSuccess / rank) and they ignore it): Get returns 0, so Set is always called */
static void __UpdataSaveDataAtEndStage(int isSuccess)
{
    PS2_FRAME(0x40);
    int bad = !isSuccess;
    if (bad) bad = (g_sortie.psettle->idGameOver != 0);
    if (g_sortie.pconstant->flagFree) bad = 0;
    if (g_sortie.pconstant->modeSortie == 0) {
        Game_ResultMission();
        if (!g_sortie.pconstant->flagFree) {
            unsigned char r = g_sortie.pmisres->rrankTotal;
            if (!(r < Game_GetMissionClearRank())) Game_SetMissionClearRank();
        }
    }
    Game_Sitmp_ResultMission(0, isSuccess);
    unsigned char mode = g_sortie.pconstant->modeSortie;
    if (mode == 3 || mode == 2 || mode == 0) {
        if (Ac_GetDiscType() == 1 && !bad) Game_SetMoney(0, g_sortie.pmisres->moneyAfter);
        __BecomeUsedParts();
    }
}
