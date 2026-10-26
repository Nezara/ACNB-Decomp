/* src/menu/marena_result.cpp - the ARENA RESULT screen shown after an Arena match (OLMENU, the MArenaResult_NewSystem
 * mobj, phases LOAD -> MAIN -> CANCEL / fade). __MArenaResult_PlayPhaseLoad waits for the arena data, then books the
 * match once: win / lose / draw counters, the battle / AP / time points and the arena point total, the official-match
 * bookkeeping (rank, passcode, secret parts), the bonus invite (officialBattle[0]) and the rank-up invite
 * (officialBattle[1]) with their opponent search, and finally builds the result strings and shows the frame.
 * The whole file is native (g1 strings / main phase, g2 PlayPhaseLoad, g3 phase dispatch, fades, cleanup, NewSystem).
 * Original quirks kept: the win / lose / draw counter is guarded by the OLD numBattle < 9999 while the total is not;
 * the two "since last official" counters are unclamped; resultPastOfficialForRankup is read outside official matches
 * (the value of the last official match); the rank band search can end at a == 8 and then reads pArenaPoint[8] and
 * g_aRankChackTable[9] past their tables; the rank < 100 invite arm leaves officialBattle[x].logicData as it was; the
 * Class arm's rank is ((signed char)Class >> 1) + 100 of the copied search flag. */
#include <string.h>
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"
#include "acnb/nodwarf.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU data 0x1ee4d90 (owned by this file): {lo, hi} arena rank bands, best band last */
char g_aRankChackTable[8][2] = {{102, 102}, {101, 101}, {100, 100}, {16, 30}, {11, 15}, {6, 10}, {2, 5}, {1, 1}};
#endif

#define MAR_WORK(p) ((struct struct_2e64b552 *)&(p)->dt)

/* MWCC's unsigned int -> float: cvt.s.w, or for values >= 2^31 the halved (odd-sticky) value converted and doubled */
static inline float mar_u2f(unsigned int v)
{
    if ((int)v >= 0) return (float)(int)v;
    float f = (float)(int)((v >> 1) | (v & 1));
    return f + f;
}

/* (rand() % (n << 14)) >> 14 with MWCC's signed remainder */
static inline int mar_rnd(int m) { return ps2_mod32(ps2_rand(), m) >> 14; }

/* s16 counter clamp to 0..9999 (three lh / sh read-modify-writes in the original) */
static inline void mar_clamp9999(short *p)
{
    if (*p < 0) *p = 0;
    if (*p >= 10000) *p = 9999;
}

#ifdef ACNB_SOURCEPORT
/* main ELF sdata 0x27b428.. and OLMENU rodata 0x1edaab0 (nodwarf.h; the passcode table comes from ELF_DATA) */
char marena_res_fmt_s[8] = "%s";
char marena_res_dummy[8] = "dummy";
char marena_res_fmt_d[8] = "%d";
char marena_res_pts[8] = "PTS.";
char marena_res_ap_none[8] = "?" "?\x81\x93";
char marena_res_ap_pct[8] = "%d\x81\x93";
char marena_res_fmt_time[20] = "%02d\x81\x66%02d\x81\x68%02d";
#endif

/* misc_0013e740 (SDK region, no symbol; only caller PlayPhaseMain): passcode character c -> its SJIS text. The
 * original indexes the pointer table 0x2596f0 (entry i = 0x271400 + 8 * i, no bounds check) */
static const char *mar_passcode_char(signed char c)
{
    return (const char *)&marena_passcode_chars[8 * c];
}

/* "f_draw = 0, f_erase = 1" (two read-modify-writes of byte 0 in the original) */
static inline void mres_erase(struct stMobj *o)
{
    o->f_draw = 0;
    o->f_erase = 1;
}

/* Builds the "new search items" dialog text: one line per rank / point / leg search flag this match unlocked, and
 * sets the flags in g_gamedata[0].asmSearchFlag. */
static void __MArenaResult_CreateGetSearchItemMessage(struct stMobj *pMobj)
{
    PS2_FRAME(0x90);
    static const signed char aRankLimit[6] = {101, 100, 30, 15, 10, 5};   /* s_aRankLimit */
    static const int aPointLimit[2] = {5000, 20000};                      /* s_aPointLimit */
    static const signed char aWinLimit[5] = {10, 20, 30, 40, 50};         /* s_aWinLimit */
    static const signed char aBattleLimit[5] = {15, 30, 45, 60, 75};      /* s_aBattleLimit */
    static const signed char aFlagPosInit[5] = {2, 3, 1, 4, 0};
    struct struct_2e64b552 *pWork = MAR_WORK(pMobj);
    struct StAcrtGameData *gd = &g_gamedata[0];
    struct stAsmSearchFlag *sf = &gd->asmSearchFlag;
    signed char num = 0;

    if (pWork->pStrSearchFlagMessage) {
        DF_MMM_Free(0, pWork->pStrSearchFlagMessage);
        pWork->pStrSearchFlagMessage = 0;
    }
    pWork->pStrSearchFlagMessage = (char *)DF_MMM_Alloc(0, 0x800);
    ps2_sprintf_s(pWork->pStrSearchFlagMessage, marena_res_fmt_s,
                  (const char *)DF_MSTR_GetName(MenuMng.language, 0, 0xdbf42));

    /* rank flags: Class << 4 | Rank as one byte (the loop's bits 5 / 4 land in Class) */
    unsigned int flag = (unsigned int)(((int)sf->Class.Data << 4) | sf->Rank) & 0xff;
    for (unsigned int i = 0; i < 6; i++) {
        if (aRankLimit[i] < gd->arenaBattle.rank) continue;
        unsigned int sh = 5 - i;
        if ((flag >> sh) & 1) continue;
        flag |= 1u << sh;
        strcat(pWork->pStrSearchFlagMessage, (const char *)DF_MSTR_GetName(MenuMng.language, 0, 0xdbf4c + i));
        num++;
    }
    sf->Rank = (unsigned char)(flag & 0xf);
    sf->Class.Data = (char)((flag >> 4) & 7);

    /* point flags: Range, Weight */
    signed char aFlagData[2] = {0, 0};
    aFlagData[0] = sf->Range.Data;
    aFlagData[1] = sf->Weight.Data;
    for (unsigned int j = 0; j < 2; j++) {
        if (gd->arenaBattle.pointArena < aPointLimit[j]) continue;
        if (aFlagData[j] != 0) continue;
        aFlagData[j] = 7;
        strcat(pWork->pStrSearchFlagMessage, (const char *)DF_MSTR_GetName(MenuMng.language, 0, 0xdbf56 + j));
        num++;
    }
    sf->Range.Data = aFlagData[0];
    sf->Weight.Data = aFlagData[1];

    /* leg flags: tested against the LegType read before the loop */
    signed char aFlagPos[5];
    memcpy(aFlagPos, aFlagPosInit, 5);
    signed char leg = sf->LegType.Data;
    for (unsigned int k = 0; k < 5; k++) {
        if (gd->arenaBattle.rank != 1 && gd->arenaBattle.numWin < aWinLimit[k] &&
            gd->arenaBattle.numBattle < aBattleLimit[k])
            continue;
        int bit = aFlagPos[k];
        if (((int)leg >> bit) & 1) continue;
        sf->LegType.Data = (char)(sf->LegType.Data | (signed char)(1 << bit));
        strcat(pWork->pStrSearchFlagMessage, (const char *)DF_MSTR_GetName(MenuMng.language, 0, 0xdbf60 + k));
        num++;
    }

    if (num != 0) {
        unsigned int n = (unsigned int)strlen(pWork->pStrSearchFlagMessage);
        pWork->pStrSearchFlagMessage[(int)n - 4] = 0;
        pWork->flagShowGetSearchFlag = 1;
    }
}

/* Creates the result screen's 22 string cells: titles, the two pilot names, the four point rows (name, "dummy"
 * placeholder, value, "PTS."), then overwrites row 1's placeholder with the AP percentage and row 2's with the
 * play time. */
static void __MArenaResult_CreateItemString(struct stMobj *pMobj)
{
    PS2_FRAME(0xd0);
    PS2_STACK(char[64], buff, 0x80);
    struct struct_76b810ec colorYellow = {0xde, 0xa5, 0x61, 0x80};    /* sp+0xc8, word 0x27b3fc */
    struct struct_76b810ec colorDefault = {0x80, 0x80, 0x80, 0x80};   /* sp+0xcc, word 0x27b3f8 */
    struct struct_2e64b552 *pWork = MAR_WORK(pMobj);
    int aPoint[4] = {pWork->pointBattle, pWork->pointAP, pWork->pointTime, pWork->pointSum};

    pWork->apMobj[0] = DF_MOBJ_NewStringCopy(pMobj, (char *)DF_MSTR_GetName(MenuMng.language, 0, 0xdbf24), 1, 0x30,
                                             0x66, &colorYellow, 0, 0);
    ps2_sprintf_s(buff, marena_res_fmt_s,
                  g_nextmenu.misres.winnerVs == 0 ? g_gamedata[0].name : MenuMng.nameRankerPilot);
    pWork->apMobj[1] = DF_MOBJ_NewStringCopy(pMobj, buff, 3, 0x34, 0x7a, &colorDefault, 0, 0);
    pWork->apMobj[2] = DF_MOBJ_NewStringCopy(pMobj, (char *)DF_MSTR_GetName(MenuMng.language, 0, 0xdbf25), 1, 0x30,
                                             0x9e, &colorYellow, 0, 0);
    ps2_sprintf_s(buff, marena_res_fmt_s,
                  g_nextmenu.misres.winnerVs == 0 ? MenuMng.nameRankerPilot : g_gamedata[0].name);
    pWork->apMobj[3] = DF_MOBJ_NewStringCopy(pMobj, buff, 3, 0x34, 0xb2, &colorDefault, 0, 0);
    pWork->apMobj[4] = DF_MOBJ_NewStringCopy(pMobj, (char *)DF_MSTR_GetName(MenuMng.language, 0, 0xdbf27), 1, 0x30,
                                             0xe2, &colorYellow, 0, 0);

    for (int i = 0; i < 4; i++) {
        int y = 0xf6 + 0x18 * i;
        pWork->apMobj[5 + 4 * i] = DF_MOBJ_NewStringCopy(
            pMobj, (char *)DF_MSTR_GetName(MenuMng.language, 0, 0xdbf2e + i), 7, 0x34, y, &colorDefault, 0, 0);
        ps2_sprintf_d(buff, marena_res_dummy, 0);
        pWork->apMobj[6 + 4 * i] = DF_MOBJ_NewStringCopy(pMobj, buff, 7, 0xeb, y, &colorDefault, 0, 0x10000);
        ps2_sprintf_d(buff, marena_res_fmt_d, aPoint[i]);
        pWork->apMobj[7 + 4 * i] = DF_MOBJ_NewStringCopy(pMobj, buff, 7, 0x11b, y, &colorDefault, 0, 0x10000);
        ps2_sprintf_d(buff, marena_res_pts, 0);
        pWork->apMobj[8 + 4 * i] = DF_MOBJ_NewStringCopy(pMobj, buff, 7, 0x11c, y, &colorDefault, 0, 0);
    }

    /* play time (frames, 60 per second) as minutes ' seconds " centiseconds */
    unsigned int f = g_nextmenu.misres.timeCombat;
    float frac = mar_u2f(f % 60);
    frac = 0x1.aaaaaap+0f * frac;                  /* 0x3fd55555 */
    int centi = ps2_ftoi(0.5f + frac);
    ps2_sprintf_ddd(buff, marena_res_fmt_time, (int)(f / 3600), (int)((f / 60) % 60), centi);
    DF_MOBJ_SetStringCopy(pWork->apMobj[14], buff);

    struct _training_trend_st *pTrend = GetTrainingTrend();
    if (pTrend->initAP == 0)
        ps2_sprintf_d(buff, marena_res_ap_none, 0);
    else
        ps2_sprintf_d(buff, marena_res_ap_pct, (int)(pTrend->restAP * 100u / pTrend->initAP));
    DF_MOBJ_SetStringCopy(pWork->apMobj[10], buff);
}

/* Cancel phase: hide every cell (the two placeholder cells once more) */
void MArenaResult_InitPhaseCancel(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_2e64b552 *pWork = MAR_WORK(pMobj);
    pWork->phase = 4;
    pMobj->count = 0;
    for (int i = 0; i < 22; i++) DF_MOBJ_SetDrawEnable(pWork->apMobj[i], 0);
    DF_MOBJ_SetDrawEnable(pWork->apMobj[6], 0);
    DF_MOBJ_SetDrawEnable(pWork->apMobj[18], 0);
}

/* Main phase: after a 6-frame wait, opens one queued dialog per call (passcode, secret parts, bonus / rank-up
 * invite, new search flags, new appellation), else shows the key guide on START. */
static void __MArenaResult_PlayPhaseMain(struct stMobj *pMobj)
{
    PS2_FRAME(0x80);
    struct struct_2e64b552 *pWork = MAR_WORK(pMobj);

    if (pMobj->stat == 1) {
        unsigned char c = pMobj->count;
        pMobj->count = (unsigned char)(c + 1);
        if (c < 6) return;
        pMobj->stat = 0;
    }
    if (DF_MDIALOG_IsPlay()) return;

    if (pWork->flagNeedShowPasscode) {
        char buf[64];   /* sp+0x40 */
        if (pWork->pStrPasscodeMessage) {
            DF_MMM_Free(0, pWork->pStrPasscodeMessage);
            pWork->pStrPasscodeMessage = 0;
        }
        pWork->pStrPasscodeMessage = (char *)DF_MMM_Alloc(0, 0x800);
        ps2_sprintf_s(pWork->pStrPasscodeMessage, marena_res_fmt_s,
                      (const char *)DF_MSTR_GetName(MenuMng.language, 0, 0xdbf38));
        memset(buf, 0, 0x40);
        for (int i = 0; i < 16; i++)
            strcat(buf, mar_passcode_char(g_gamedata[0].PasscodeInfo.passcodeArena.data[i]));
        strcat(pWork->pStrPasscodeMessage, buf);
        DF_MDIALOG_Open(4, (unsigned char *)pWork->pStrPasscodeMessage, 0, 0, 0, 0, 1, 0);
        pWork->flagNeedShowPasscode = 0;
    } else if (pWork->flagShowSecretParts) {
        MSecretPartsMng_CreateShowMessage(0xdbf89);
        pWork->flagShowSecretParts = 0;
    } else if (pWork->flagInviteFromBonus) {
        DF_MDIALOG_Open(4, DF_MSTR_GetName(MenuMng.language, 0, (signed char)pWork->flagInviteFromBonus + 0x7a511), 0,
                        0, 0, 0, 1, 0);
        pWork->flagInviteFromBonus = 0;
    } else if (pWork->flagInviteFromRankup) {
        DF_MDIALOG_Open(4, DF_MSTR_GetName(MenuMng.language, 0, (signed char)pWork->flagInviteFromRankup + 0x7a51b), 0,
                        0, 0, 0, 1, 0);
        pWork->flagInviteFromRankup = 0;
    } else if (pWork->flagShowGetSearchFlag) {
        DF_MDIALOG_Open(4, (unsigned char *)pWork->pStrSearchFlagMessage, 0, 0, 0, 0, 1, 0);
        pWork->flagShowGetSearchFlag = 0;
    } else if (pWork->flagShowGetAppellation) {
        DF_MDIALOG_Open(4, (unsigned char *)MAppellationMng_GetMessage(), 0, 0, 0, 0, 1, 0);
        pWork->flagShowGetAppellation = 0;
    } else if (!MenuKeyGuide_IsShown() && MPad[MPadUser].start == 1) {
        MenuKeyGuide_Show(0x42);
    }
}

static void __MArenaResult_PlayPhaseLoad(struct stMobj *pMobj)
{
    PS2_FRAME(0x110);
    PS2_STACK(struct stPasscodeOutputData, stOutput, 0x80);
    PS2_STACK(struct stPasscodeInputData, stInput, 0xa0);
    PS2_STACK(struct stAsmSearchFlag, flagSearch, 0x100);
    char namePilot[26];   /* sp+0xc0 in the original, only memcpy'd */
    char nameAC[26];      /* sp+0xe0 */
    struct struct_2e64b552 *pWork = MAR_WORK(pMobj);
    struct StAcrtGameData *gd = &g_gamedata[0];
    struct _arena_battle_info_st *bat = &gd->arenaBattle;
    struct _arena_official_card_st *off = gd->officialBattle;

    struct struct_4ef7c987 *pArenaPointHead = MArenaPointMng_GetArenaPointHead();
    struct struct_f3000f24 *pArenaPoint = MArenaPointMng_GetArenaPoint();
    if (File_IsAsyncBusy()) {
        pWork->countFinishReading = 0;
        return;
    }
    pWork->countFinishReading++;
    if (pWork->countFinishReading < 11) return;
    if ((signed char)MArenaBridge_Loading() != 6) return;

    DF_MVIEW_MotionStart(MViewp, 0x51);
    DF_MBGUIDE_Show();
    DF_MBACK_SetFadeInNX();   /* a0 = 0 (DWARF: no parameter) */
    DF_M1HELP_SetWindow();    /* empty stub; the original passes (0, 0x280) */
    DF_M1HELP_SetString(DF_MSTR_GetInfo(MenuMng.language, 0, 0xdbc16));
    DF_M1HELP_FadeIn();
    DF_MENU_SetFadeIn(6);
    Game_SetBgmVolume(0, Game_GetOptionValue(0, 1));
    Game_SetSeVolume(0, Game_GetOptionValue(0, 6));
    Bgm_Play();

    /* battle counters (res: 0 = win, 1 or vsresm 3 = lose, else draw) */
    char res = g_nextmenu.misres.winnerVs;
    if (res == 0) {
        if (bat->numBattle < 9999) bat->numWin++;
        mar_clamp9999(&bat->numWin);
    } else if (res == 1 || g_nextmenu.misres.vsresm == 3) {
        if (bat->numBattle < 9999) bat->numLose++;
        mar_clamp9999(&bat->numLose);
    } else {
        if (bat->numBattle < 9999) bat->numDraw++;
        mar_clamp9999(&bat->numDraw);
    }
    bat->numBattle++;
    mar_clamp9999(&bat->numBattle);
    bat->numBattleFromBeforeOfficialForBonus++;
    bat->numBattleFromBeforeOfficialForRankup++;

    /* points */
    if (res == 0) {
        pWork->pointBattle = MenuMng.flagSearch.UserPointPlus;
    } else if (res == 1 || g_nextmenu.misres.vsresm == 3) {
        pWork->pointBattle = MenuMng.flagSearch.UserPointMinus;
    } else {
        pWork->pointBattle = ps2_ftoi(0.5f * mar_u2f(MenuMng.flagSearch.UserPointPlus));
    }
    if (g_nextmenu.misres.winnerVs == 0) {
        float f1 = mar_u2f(GetTrainingTrend()->restAP);
        f1 = f1 * pArenaPointHead->rateAPPoint;
        float f0 = mar_u2f(MenuMng.flagSearch.UserPointBase);
        pWork->pointAP = ps2_ftoi(f0 * f1);
    } else {
        pWork->pointAP = 0;
    }
    if (g_nextmenu.misres.winnerVs == 0) {
        int t = 300 - (int)(g_nextmenu.misres.timeCombat / 60u);
        if (t < 0) t = 0;
        float f1 = (float)t * pArenaPointHead->rateTimePoint;
        float f0 = mar_u2f(MenuMng.flagSearch.UserPointBase);
        pWork->pointTime = ps2_ftoi(f0 * f1);
    } else {
        pWork->pointTime = 0;
    }
    pWork->pointSum = pWork->pointTime + (pWork->pointBattle + pWork->pointAP);
    pWork->pointPast = bat->pointArena;
    bat->pointArena = bat->pointArena + pWork->pointSum;
    if (bat->pointArena < 0) bat->pointArena = 0;
    if (bat->pointArena >= 1000000) bat->pointArena = 999999;
    off[0].flagNew = 0;
    off[1].flagNew = 0;
    pWork->rankPast = (signed char)bat->rank;

    if (MenuMng.modeArena == 2) memset(&off[0], 0, 0xa8);
    if (MenuMng.modeArena == 3) {   /* official match */
        res = g_nextmenu.misres.winnerVs;
        if (res == 0)
            bat->resultPastOfficialForRankup = 0;
        else if (res == 1 || g_nextmenu.misres.vsresm == 3)
            bat->resultPastOfficialForRankup = 1;
        else
            bat->resultPastOfficialForRankup = -1;
        if (res == 0) {
            bat->rank = off[1].rank;
            memset(&off[0], 0, 0xa8);
            if (bat->rank == 1) {
                memset(&stInput, 0, 0x18);
                memset(&stOutput, 0, 0x14);
                if (!((signed char)gd->PasscodeInfo.keycode.data[0] < 0)) {
                    memcpy(stInput.keycode, gd->PasscodeInfo.keycode.data, 4);
                    stInput.detail.idMission = 0;
                    stInput.detail.point = bat->pointArena;
                    stInput.detail.numBattle = bat->numBattle;
                    stInput.detail.numWin = bat->numWin;
                    stInput.detail.numLose = bat->numLose;
                    misc_0013e760(&stInput, &stOutput);
                    memcpy(gd->PasscodeInfo.passcodeArena.data, stOutput.passcode, 0x11);
                    pWork->flagNeedShowPasscode = 1;
                }
            }
        } else if (res == 1 || g_nextmenu.misres.vsresm == 3) {
            bat->numBattleFromBeforeOfficialForRankup = 0;
        }
        memset(&off[1], 0, 0xa8);
        if (MSecretPartsMng_CreateArenaSecretPartsList()) pWork->flagShowSecretParts = 1;
    }

    /* bonus invite (officialBattle[0]) */
    if (off[0].idRanker == 0 &&
        !(bat->numBattleFromBeforeOfficialForBonus < (signed char)pArenaPointHead->numMatchNextInviteDefault)) {
        unsigned char rankRanker;
        int idRanker;
        memset(&flagSearch, 0, 0x10);
        bat->numBattleFromBeforeOfficialForBonus = 0;
        signed char r = bat->rank;
        if (r < 100) {
            if (r == 1) {
                int v = mar_rnd(0x74000) + 1;   /* ranks 1..29 */
                flagSearch.Rank = (unsigned char)v;
                rankRanker = (unsigned char)v;
            } else {
                flagSearch.Rank = (unsigned char)r;
                rankRanker = (unsigned char)r;
            }
            int k = rankRanker;
            AsmSearch_SetSearchFlag(k - 1, &flagSearch);
            idRanker = k + 0x18f;
            memcpy(nameAC, DF_MSTR_GetName(MenuMng.language, 0, k + 0x7a11f), 0x1a);
            memcpy(namePilot, DF_MSTR_GetName(MenuMng.language, 0, k + 0x7a183), 0x1a);
            MArenaPointMng_SetArenaPointOne(2, &flagSearch);
            pWork->flagInviteFromBonus = 2;
        } else {
            flagSearch.Class.Data = (char)(1 << (r - 100));
            MArenaBridge_SearchBegin();
            AsmSearchInit();
            memcpy(&g_flagSearch, &flagSearch, 0x10);
            AsmSearch(&flagSearch);
            MArenaBridge_SearchEnd();
            MArenaPointMng_SetArenaPoint(2);
            struct stAutoGenerateData *data = MArenaBridge_GetAutoGenerateData();
            int n = (signed char)MArenaBridge_GetAutoGenerateDataNum();
            int idx = (n > 0) ? mar_rnd(n << 14) : 0;
            memcpy(gd->colorAutoGen[0], MArenaBridge_GetAutoGenerateColor(idx), 0xd0);
            struct stAutoGenerateData *e = &data[idx];
            idRanker = e->id;
            memcpy(nameAC, e->nameAC, 0x1a);
            memcpy(namePilot, e->namePilot, 0x1a);
            memcpy(&flagSearch, &e->flagSearch, 0x10);
            memcpy(&off[0].logicData, &e->logicData, 8);
            pWork->flagInviteFromBonus = 1;
            rankRanker = (unsigned char)(((signed char)flagSearch.Class.Data >> 1) + 100);
        }
        memcpy(&off[0].flagSearch, &flagSearch, 0x10);
        off[0].idRanker = idRanker;
        memcpy(off[0].nameAC, nameAC, 0x1a);
        memcpy(off[0].namePilot, namePilot, 0x1a);
        off[0].rank = (char)rankRanker;
        off[0].flagNew = 1;
        off[0].map = MArenaStarter_GetRandomMap(0);
    }

    /* rank-up invite (officialBattle[1]): matches needed by the result of the last official match */
    signed char numNeed;
    signed char rpast = bat->resultPastOfficialForRankup;
    if (rpast == 0)
        numNeed = 0;
    else if (rpast == 1)
        numNeed = pArenaPointHead->numMatchNextInviteLose;
    else
        numNeed = pArenaPointHead->numMatchNextInviteDraw;
    if (off[1].idRanker == 0 && bat->rank != 1 && !(bat->numBattleFromBeforeOfficialForRankup < numNeed)) {
        memset(&flagSearch, 0, 0x10);
#ifdef ACNB_SOURCEPORT
        /* a == 8 (rank outside every band) reads 4 bytes past the table: on the PS2 the low bytes of the next OLMENU
         * word (0x01e06138, a function-pointer table). Copy with those bytes so the sourceport read stays defined. */
        signed char T[20];
        memcpy(T, g_aRankChackTable, 16);
        T[16] = 0x38; T[17] = 0x61; T[18] = (signed char)0xe0; T[19] = 0x01;
#else
        const signed char *T = (const signed char *)&g_aRankChackTable[0][0];   /* read flat: a may reach 8 */
#endif
        signed char rank = bat->rank;
        unsigned int a;
        for (a = 0; a < 8; a++) {
            if (rank < T[a * 2]) continue;
            if (T[a * 2 + 1] < rank) continue;
            break;
        }
        if (!(bat->pointArena < pArenaPoint[a].point)) {
            const signed char *pLo = &T[(a + 1) * 2];
            int n = pLo[1] - pLo[0] + 1;
            int r2 = (n > 0) ? mar_rnd(n << 14) : 0;
            int newRank = pLo[0] + r2;
            unsigned char rankRanker;
            int idRanker;
            bat->numBattleFromBeforeOfficialForRankup = 0;
            if (newRank < 100) {
                rankRanker = (unsigned char)newRank;
                flagSearch.Rank = (unsigned char)newRank;
                AsmSearch_SetSearchFlag(rankRanker - 1, &flagSearch);
                idRanker = newRank + 0x18f;
                memcpy(nameAC, DF_MSTR_GetName(MenuMng.language, 0, newRank + 0x7a11f), 0x1a);
                memcpy(namePilot, DF_MSTR_GetName(MenuMng.language, 0, newRank + 0x7a183), 0x1a);
                MArenaPointMng_SetArenaPointOne(2, &flagSearch);
                pWork->flagInviteFromRankup = 2;
            } else {
                flagSearch.Class.Data = (char)(1 << (newRank - 100));
                MArenaBridge_SearchBegin();
                AsmSearchInit();
                memcpy(&g_flagSearch, &flagSearch, 0x10);
                AsmSearch(&flagSearch);
                MArenaBridge_SearchEnd();
                MArenaPointMng_SetArenaPoint(2);
                struct stAutoGenerateData *data = MArenaBridge_GetAutoGenerateData();
                int cnt = (signed char)MArenaBridge_GetAutoGenerateDataNum();
                int idx = (cnt > 0) ? mar_rnd(cnt << 14) : 0;
                memcpy(gd->colorAutoGen[1], MArenaBridge_GetAutoGenerateColor(idx), 0xd0);
                struct stAutoGenerateData *e = &data[idx];
                idRanker = e->id;
                memcpy(nameAC, e->nameAC, 0x1a);
                memcpy(namePilot, e->namePilot, 0x1a);
                memcpy(&flagSearch, &e->flagSearch, 0x10);
                memcpy(&off[1].logicData, &e->logicData, 8);
                pWork->flagInviteFromRankup = 1;
                rankRanker = (unsigned char)(((signed char)flagSearch.Class.Data >> 1) + 100);
            }
            memcpy(&off[1].flagSearch, &flagSearch, 0x10);
            off[1].idRanker = idRanker;
            memcpy(off[1].nameAC, nameAC, 0x1a);
            memcpy(off[1].namePilot, namePilot, 0x1a);
            off[1].rank = (char)rankRanker;
            off[1].flagNew = 1;
            off[1].map = MArenaStarter_GetRandomMap(0);
        }
    }

    __MArenaResult_CreateGetSearchItemMessage(pMobj);
    if (MAppellationMng_CreateMessage()) pWork->flagShowGetAppellation = 1;
    __MArenaResult_CreateItemString(pMobj);
    char acNo = PlayerInfo_GetCurrentAcNo(0);
    char *name = PlayerInfo_GetAcName(0, acNo);
    MUnitArenaFrame_SetResultData(pWork->pMobjUnitArenaFrame, name, bat->pointArena, pWork->pointPast,
                                  (signed char)bat->rank, pWork->rankPast);
    pWork->phase = 2;
    pMobj->stat = 1;
    pMobj->count = 0;
    DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, 0xdbc16));
    for (int i = 0; i < 22; i++) DF_MOBJ_SetDrawEnable(pWork->apMobj[i], 1);
    DF_MOBJ_SetDrawEnable(pWork->apMobj[6], 0);
    DF_MOBJ_SetDrawEnable(pWork->apMobj[18], 0);
    MUnitTabInfo_SetArrowState(pWork->pMobjUnitTabInfo, 0);
    pWork->flagLoadEnd = 1;
}

/* Phase 4 cleanup: frees the arena frame, the 22 cells, the bridge / secret parts / appellation managers and the
 * three heap buffers (the ranking manager arm is never used by this screen). */
static void __MArenaResult_DeleteItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct struct_2e64b552 *pWork = MAR_WORK(pMobj);
    MUnitArenaFrame_Cleanup(pWork->pMobjUnitArenaFrame);
    if (pWork->pMobjUnitArenaFrame) {
        mres_erase(pWork->pMobjUnitArenaFrame);
        pWork->pMobjUnitArenaFrame = 0;
    }
    for (int i = 0; i < 22; i++) {
        if (pWork->apMobj[i]) {
            mres_erase(pWork->apMobj[i]);
            pWork->apMobj[i] = 0;
        }
    }
    MArenaBridge_Cleanup();
    MSecretPartsMng_Cleanup();
    if (pWork->pACWareParts) {
        DF_MMM_Free(0, pWork->pACWareParts);
        pWork->pACWareParts = 0;
    }
    if (pWork->pStrPasscodeMessage) {
        DF_MMM_Free(0, pWork->pStrPasscodeMessage);
        pWork->pStrPasscodeMessage = 0;
    }
    if (pWork->pStrSearchFlagMessage) {
        DF_MMM_Free(0, pWork->pStrSearchFlagMessage);
        pWork->pStrSearchFlagMessage = 0;
    }
    if (pWork->pMobjRankingMng) {
        MRankingMng_Cleanup(pWork->pMobjRankingMng);
        if (pWork->pMobjRankingMng) {   /* reloaded after the call */
            mres_erase(pWork->pMobjRankingMng);
            pWork->pMobjRankingMng = 0;
        }
    }
    MAppellationMng_Cleanup();
}

/* Tab manager: fade the screen out (tab info, frame, every cell) */
void MArenaResult_FadeOutItemOne(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_2e64b552 *pWork = MAR_WORK(pMobj);
    MUnitTabInfo_FadeOutStart(pWork->pMobjUnitTabInfo, 0xd);
    MUnitArenaFrame_FadeOutStart(pWork->pMobjUnitArenaFrame, 0xd);
    for (int i = 0; i < 22; i++) DF_MOBJ_SetFadeOut(pWork->apMobj[i]);
    DF_MOBJ_SetDrawEnable(pWork->apMobj[6], 0);
    DF_MOBJ_SetDrawEnable(pWork->apMobj[18], 0);
}

/* Tab manager: hide every cell */
void MArenaResult_FadeOutItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_2e64b552 *pWork = MAR_WORK(pMobj);
    for (int i = 0; i < 22; i++) DF_MOBJ_SetDrawEnable(pWork->apMobj[i], 0);
    DF_MOBJ_SetDrawEnable(pWork->apMobj[6], 0);
    DF_MOBJ_SetDrawEnable(pWork->apMobj[18], 0);
}

/* Tab manager: fade the screen in (the two placeholder cells stay hidden) */
void MArenaResult_FadeInItemOne(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_2e64b552 *pWork = MAR_WORK(pMobj);
    MUnitTabInfo_FadeInStart(pWork->pMobjUnitTabInfo, 1);
    MUnitArenaFrame_FadeInStart(pWork->pMobjUnitArenaFrame, 1);
    for (int i = 0; i < 22; i++) {
        DF_MOBJ_SetDrawEnable(pWork->apMobj[i], 1);
        DF_MOBJ_SetFadeIn(pWork->apMobj[i]);
    }
    DF_MOBJ_SetDrawEnable(pWork->apMobj[6], 0);
    DF_MOBJ_SetDrawEnable(pWork->apMobj[18], 0);
}

/* the show-all tail shared by FadeInItem and Play phase 5 */
static inline void mres_show_all(struct struct_2e64b552 *pWork)
{
    DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, 0xdbc16));
    for (int i = 0; i < 22; i++) DF_MOBJ_SetDrawEnable(pWork->apMobj[i], 1);
    DF_MOBJ_SetDrawEnable(pWork->apMobj[6], 0);
    DF_MOBJ_SetDrawEnable(pWork->apMobj[18], 0);
    MUnitTabInfo_SetArrowState(pWork->pMobjUnitTabInfo, 0);
}

/* Tab manager: show every cell again, help line + tab arrows */
void MArenaResult_FadeInItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    mres_show_all(MAR_WORK(pMobj));
}

static void __MArenaResult_Pkt(void) {}

static void __MArenaResult_Draw(void) {}

/* play_func: phase 1 load, 2 main, 3 -> 5 after 6 frames, 4 delete after 6 frames, 5 wait for the child and the
 * view motion, then back to main */
static void __MArenaResult_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct struct_2e64b552 *pWork = MAR_WORK(pMobj);
    unsigned char c;
    switch (pWork->phase) {
    case 1:
        __MArenaResult_PlayPhaseLoad(pMobj);
        break;
    case 2:
        __MArenaResult_PlayPhaseMain(pMobj);
        break;
    case 3:
        c = pMobj->count;
        pMobj->count = c + 1;
        if (c < 6) break;
        pWork->phase = 5;
        pMobj->stat = 3;
        break;
    case 4:
        c = pMobj->count;
        pMobj->count = c + 1;
        if (c < 6) break;
        __MArenaResult_DeleteItem(pMobj);
        pMobj->stat = 5;
        pMobj->play_func = 0;
        pMobj->draw_func = 0;
        pMobj->pkt_func = 0;
        break;
    case 5:
        if (!pWork->pChild) break;
        if (pWork->pChild->stat != 5) break;
        if (!DF_MVIEW_MotionSync(MViewp)) break;
        if (pWork->pChild) {
            mres_erase(pWork->pChild);
            pWork->pChild = 0;
        }
        pWork->phase = 2;
        pMobj->stat = 1;
        pMobj->count = 0;
        mres_show_all(pWork);
        break;
    default:
        break;
    }
}

/* Tab manager: the screen still busy (loading, a dialog open or queued) */
char MArenaResult_IsMove(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    struct struct_2e64b552 *pWork = MAR_WORK(pMobj);
    if (!pWork->flagLoadEnd) return 1;
    if (DF_MDIALOG_IsPlay()) return 1;
    if (pWork->flagNeedShowPasscode) return 1;
    if (pWork->flagShowSecretParts) return 1;
    if (pWork->flagInviteFromBonus) return 1;
    if (pWork->flagInviteFromRankup) return 1;
    if (pWork->flagShowGetSearchFlag) return 1;
    return pWork->flagShowGetAppellation != 0;
}

/* Creates the result mobj: tab info, arena frame, help line, a copy of the player assembly (g_nextstage.asmbly[0])
 * and, unless already shown, the menu AC model. The alloc-failure return leaks the mobj (original). */
struct stMobj *MArenaResult_NewSystem(struct stMobj *pParentMobj)
{
    PS2_FRAME(0x40);
    struct stMobj *m = DF_MOBJ_GetNewObject(pParentMobj);
    if (!m) return 0;
    struct struct_2e64b552 *pWork = MAR_WORK(m);
    memset(pWork, 0, 0x98);
    m->type = 0;
    m->stat = 1;
    m->count = 0;
    m->play_func = (void (*)(void))GFN(__MArenaResult_Play);
    m->draw_func = (void (*)(void))GFN(__MArenaResult_Draw);
    m->pkt_func = (void (*)(void))GFN(__MArenaResult_Pkt);
    pWork->pMobjUnitTabInfo = MUnitMng_GetUnit(7);
    pWork->pMobjUnitArenaFrame = MUnitArenaFrame_NewSystem(m, 1);
    DF_M1HELP_SetWindow();   /* empty stub; the original passes (0, 0x280) */
    DF_M1HELP_SetString(DF_MSTR_GetInfo(MenuMng.language, 0, 0xdbc16));
    DF_MENU_SetFadeOut(0);
    pWork->pACWareParts = (char *)DF_MMM_Alloc(0, 0x120);
    if (!pWork->pACWareParts) return 0;
    DF_MGARAGE_SetMissionAc(0);
    DF_M_AC_MissionTransStart();
    memcpy(pWork->pACWareParts, &g_nextstage.asmbly[0], 0x120);
    if (!M_ACp[0][0]->flag.draw) {
        DF_MMD_SetGeneralMMD();
        struct stMenuAC *ac = M_ACp[0][0];
        DF_M_AC_SetData(ac);
        DF_M_AC_PartsRead(ac);
        ac->alpha.cur = 0;
        ac->flag.rotate = 1;
        DF_M_AC_SetFadeIn(M_ACp[0][0], 0xf, 0.0f);
        M_ACp[0][0]->flag.draw = 1;
    }
    MArenaBridge_Init();
    pWork->phase = 1;
    m->count = 0;
    return m;
}
