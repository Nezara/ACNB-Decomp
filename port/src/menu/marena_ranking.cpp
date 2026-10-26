/* src/menu/marena_ranking.cpp - the Arena ranking screen (OLMENU): up to 11 ranker cards (MUnitRankerName3), a
 * cursor card and the player's AC frame (MUnitArenaFrame), with the live ranking data behind it (MRankingMng).
 * Two modes: mode 1 = the read-only Arena Top ranking list (30 entries, reached from interpreted __MArenaTop_Play);
 * mode 2 = the Versus "pick the COM opponent" screen (the 10 fixed rankers of g_aVsRankerTable), where Cross writes
 * the chosen ranker id and name into the vs_ac_info record the caller passed as pOutput (mvsg
 * __PlayACSetupSystemFadeOut). Up / Down move the cursor and, once the list is longer than the 11 cards, rotate the
 * card pointers; Circle cancels, Triangle toggles the emblem / frame display, Start shows the key guide.
 * Driven by the object's play pointer; it signals the parent by setting stat 5 and clearing its three function
 * pointers. Whole file native. The dead compare chains of the rank arithmetic and the out-of-bounds 11th copy of the
 * rotate-left loop are kept as the original wrote them. */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU data 0x1ee4d80: the 10 Versus COM rankers (arena ranks), owned by this file */
char g_aVsRankerTable[10] = {5, 9, 14, 17, 18, 23, 24, 26, 28, 30};
/* main-ELF sdata "%s" at 0x27b3f4 (nodwarf.h), the format of the name_com sprintf */
char marena_fmt_s[4] = "%s";
#endif

#define RK(p) ((struct struct_feecbe04 *)&(p)->dt)

/* "f_draw = 0, f_erase = 1" (two read-modify-writes of byte 0 in the original) */
static inline void mar_erase(struct stMobj *o)
{
    o->f_draw = 0;
    o->f_erase = 1;
}

/* the cards / cursor / frame fade-out, copy-pasted in the original's cancel and decide arms */
static inline void mar_fade_out_all(struct struct_feecbe04 *w)
{
    if (w->flagFadeCursor == 1) {
        MUnitRankerName3_FadeOutCursor(w->pMobjUnitRankerNameForCursor);
        w->flagFadeCursor = 0;
    }
    for (int i = 0; i < (w->maxCard < 11 ? w->maxCard : 11); i++)
        MUnitRankerName3_FadeOutStart(w->apMobjUnitRankerName[i], 0xd);
    MUnitRankerName3_FadeOutStart(w->pMobjUnitRankerNameForCursor, 0xd);
    MUnitArenaFrame_FadeOutStart(w->pMobjUnitArenaFrame, 0xd);
    if (w->pMobjRankingMng) MRankingMng_SetShow(w->pMobjRankingMng, 0);
}

/* the ranking-data record id the list should read next: mode 1 walks the live arena list around the player's own
 * rank (-1 = the player's own card), mode 2 the fixed Versus ranker table. Goes to MRankingMng once it exists,
 * otherwise it is remembered for the MRankingMng_NewSystem in PlayPhaseMain's step 1. */
static void __MArenaRanking_SetNextReadID(struct stMobj *pMobj, char indexData)
{
    PS2_FRAME(0x10);
    struct struct_feecbe04 *w = RK(pMobj);
    struct stMobj *pRankingMng = w->pMobjRankingMng;

    if (pRankingMng != 0) {
        int id;
        if (w->mode == 1) {
            int cur = g_gamedata[0].arenaBattle.rank;
            int r = indexData + 1;
            if (cur == r) id = -1;
            else if (cur < r) id = indexData + 0x18f;
            else id = indexData + 0x190;
        } else {
            id = g_aVsRankerTable[(int)indexData] + 0x18f;
        }
        MRankingMng_SetNextReadID(pRankingMng, id);
    } else {
        short id;
        if (w->mode == 1) {
            int cur = g_gamedata[0].arenaBattle.rank;
            int r = indexData + 1;
            if (cur == r) id = -1;
            else if (cur < r) id = indexData + 0x18f;
            else id = indexData + 0x190;
        } else {
            id = g_aVsRankerTable[(int)indexData] + 0x18f;
        }
        w->idNextRead = id;
    }
}

/* the player's AC frame: AC name + rank of the entry the cursor is on (mode 1: the player's own AC when the cursor
 * is on his rank, rank 0x64 = "unranked"; mode 2: the ranker's arena rank) */
static void __MArenaRanking_SetDataToUnitFrame(struct stMobj *pMobj, char indexData)
{
    PS2_FRAME(0x30);
    struct struct_feecbe04 *w = RK(pMobj);
    char *pNameAC;
    int rank;

    if (w->mode == 1) {
        int r = indexData + 1;
        int cur = g_gamedata[0].arenaBattle.rank;
        rank = r;
        if (cur == r) {
            rank = 0x64;
            pNameAC = PlayerInfo_GetAcName(0, PlayerInfo_GetCurrentAcNo(0));
        } else if (cur < r) {
            rank = indexData;
            pNameAC = (char *)DF_MSTR_GetName(MenuMng.language, 0, indexData + 0x7a11f);
        } else {
            if (cur < 100 && !(rank < cur)) rank = indexData + 2; /* dead: rank < cur holds here */
            pNameAC = (char *)DF_MSTR_GetName(MenuMng.language, 0, indexData + 0x7a120);
        }
    } else {
        rank = g_aVsRankerTable[(int)indexData];
        pNameAC = (char *)DF_MSTR_GetName(MenuMng.language, 0, rank + 0x7a11f);
    }
    MUnitArenaFrame_SetData(w->pMobjUnitArenaFrame, pNameAC, rank);
}

/* one ranker card: its rank number and pilot name (mode 1: the player's own pilot name on his own rank) */
static void __MArenaRanking_SetDataToUnitRankerName(struct stMobj *pMobj, int indexUnit, char indexData)
{
    PS2_FRAME(0x40);
    struct struct_feecbe04 *w = RK(pMobj);
    char *pNamePilot;
    char rank;

    if (w->mode == 1) {
        int cur = g_gamedata[0].arenaBattle.rank;
        int r = indexData + 1;
        if (cur == r) {
            rank = cur;
            pNamePilot = g_gamedata[0].name;
        } else if (cur < r) {
            int rk;
            if (cur < 100) rk = (indexData < cur) ? indexData : r; /* dead movn: cur <= indexData here */
            else rk = indexData;
            rank = (char)rk;
            pNamePilot = (char *)DF_MSTR_GetName(MenuMng.language, 0, indexData + 0x7a183);
        } else {
            int rk = r;
            if (cur < 100 && !(rk < cur)) rk = indexData + 2; /* dead: rk < cur holds here */
            rank = (char)rk;
            pNamePilot = (char *)DF_MSTR_GetName(MenuMng.language, 0, indexData + 0x7a184);
        }
    } else {
        rank = 0;
        pNamePilot = (char *)DF_MSTR_GetName(MenuMng.language, 0, g_aVsRankerTable[(int)indexData] + 0x7a183);
    }
    MUnitRankerName3_SetStrings(w->apMobjUnitRankerName[indexUnit], rank, pNamePilot);
}

/* phase 1: the live screen - key guide, cursor movement (with the card rotation once the list is longer than the
 * 11 cards), the MArenaAsemMng -> MRankingMng start-up steps, and the Circle / Triangle / Cross handling */
static void __MArenaRanking_PlayPhaseMain(struct stMobj *pMobj)
{
    PS2_FRAME(0x60);
    struct struct_feecbe04 *w = RK(pMobj);

    if (pMobj->stat == 1) { /* the 7 fade-in frames; count is incremented even on the early return */
        unsigned char count = pMobj->count;
        pMobj->count = count + 1;
        if (count < 6) return;
        pMobj->stat = 0;
    }
    if (w->mode == 1) {
        if (MenuKeyGuide_IsShown()) return;
        if (MPad[MPadUser].start == 1) {
            MenuKeyGuide_Show(0x4e);
            return;
        }
    } else {
        if (MenuKeyGuide_IsShown()) return;
        if (MPad[MPadUser].start == 1) {
            MenuKeyGuide_Show(0x4c);
            return;
        }
    }
    for (int i = 0; i < (w->maxCard < 11 ? w->maxCard : 11); i++)
        if (MUnitRankerName3_IsMove(w->apMobjUnitRankerName[i])) return;
    if (w->flagFadeCursor == 0) {
        MUnitRankerName3_FadeInCursor(w->pMobjUnitRankerNameForCursor, w->maxCard - 1 >= 10);
        w->flagFadeCursor = 1;
    }
    if (w->stateMove != 0) {
        if (w->stateMove == 1) { /* scrolled down: the top card becomes the bottom one */
            struct stMobj *first = w->apMobjUnitRankerName[0];
            for (int i = 0; i < 10; i++) w->apMobjUnitRankerName[i] = w->apMobjUnitRankerName[i + 1];
            /* the original's unrolled loop runs one copy too far and reads the cursor slot (dead: overwritten) */
            w->apMobjUnitRankerName[10] = w->pMobjUnitRankerNameForCursor;
            w->apMobjUnitRankerName[10] = first;
        } else { /* scrolled up: the bottom card becomes the top one */
            struct stMobj *last = w->apMobjUnitRankerName[9];
            for (int i = 9; i >= 1; i--) w->apMobjUnitRankerName[i] = w->apMobjUnitRankerName[i - 1];
            w->apMobjUnitRankerName[0] = w->apMobjUnitRankerName[10];
            w->apMobjUnitRankerName[10] = last;
        }
        for (int i = 0; i < 11; i++) MUnitRankerName3_SetID(w->apMobjUnitRankerName[i], (char)i);
        w->stateMove = 0;
        return;
    }
    if (MPad[MPadUser].repeat.dw == 1) {
        /* the original keeps the old cursor_index here and passes it as a leftover 3rd argument to ChangeCursor,
         * which only reads (mobj, indexNew) */
        Voice_Call(0x3e9, 0x7f);
        w->data_index = w->data_index + 1;
        if (!(w->data_index < w->maxCard)) w->data_index = 0;
        __MArenaRanking_SetDataToUnitFrame(pMobj, w->data_index);
        __MArenaRanking_SetNextReadID(pMobj, w->data_index);
        w->cursor_index = w->cursor_index + 1;
        int last = w->maxCard - 1;
        if ((last < 9 ? last : 9) < w->cursor_index) {
            w->cursor_index = (last < 10) ? 0 : 9;
            if (w->maxCard - 1 >= 10) { /* more entries than cards: scroll instead of wrapping the cursor */
                __MArenaRanking_SetDataToUnitRankerName(pMobj, 10, w->data_index);
                MUnitRankerName3_SetPos(w->apMobjUnitRankerName[10], 10);
                for (int i = 0; i < 11; i++) MUnitRankerName3_FadeInStart(w->apMobjUnitRankerName[i], 0x13);
                w->stateMove = 1;
            }
        }
        MUnitRankerName3_ChangeCursor(w->pMobjUnitRankerNameForCursor, w->cursor_index);
    }
    /* a second, independent `if`: with both repeat bits set the list moves twice in one frame */
    if (MPad[MPadUser].repeat.up == 1) {
        /* same leftover old cursor_index as the Down arm */
        Voice_Call(0x3e9, 0x7f);
        w->data_index = w->data_index - 1;
        if (w->data_index < 0) w->data_index = w->maxCard - 1;
        __MArenaRanking_SetDataToUnitFrame(pMobj, w->data_index);
        __MArenaRanking_SetNextReadID(pMobj, w->data_index);
        w->cursor_index = w->cursor_index - 1;
        if (w->cursor_index < 0) {
            int last = w->maxCard - 1;
            w->cursor_index = (last < 10) ? last : 0;
            if (w->maxCard - 1 >= 10) {
                __MArenaRanking_SetDataToUnitRankerName(pMobj, 10, w->data_index);
                MUnitRankerName3_SetPos(w->apMobjUnitRankerName[10], -1);
                for (int i = 0; i < 11; i++) MUnitRankerName3_FadeInStart(w->apMobjUnitRankerName[i], 0x15);
                w->stateMove = 2;
            }
        }
        MUnitRankerName3_ChangeCursor(w->pMobjUnitRankerNameForCursor, w->cursor_index);
    }
    switch (w->step) {
    case 0:
        MArenaAsemMng_SetUp();
        w->step = 1;
        return;
    case 1:
        if (MArenaAsemMng_Play() == 3) {
            w->pMobjRankingMng = MRankingMng_NewSystem(pMobj, 4, MArenaAsemMng_GetAsemData());
            MRankingMng_SetNextReadID(w->pMobjRankingMng, w->idNextRead);
            MRankingMng_SetShow(w->pMobjRankingMng, 1);
            w->step = 2;
        }
        return;
    case 2: break;
    default: break; /* the original falls through to the input handling for any other step too */
    }
    if (MPad[MPadUser].Rrt == 1) { /* Circle: cancel, pOutput untouched */
        Voice_Call(0x3eb, 0x7f);
        w->phase = 3;
        pMobj->count = 0;
        mar_fade_out_all(w);
        return;
    } else if (MPad[MPadUser].Rup == 1) { /* Triangle: toggle the frame / emblem display (no null tests) */
        MUnitArenaFrame_ChangeMode(w->pMobjUnitArenaFrame, -1);
        MRankingMng_ChangeEmblemMode(w->pMobjRankingMng, -1);
        return;
    } else if (MPad[MPadUser].Rdw == 1 && w->mode == 2) { /* Cross: hand the chosen ranker to the caller */
        struct struct_aa25ad39 *pACInfo = (struct struct_aa25ad39 *)w->pOutput;
        pACInfo->com_id = g_aVsRankerTable[(int)w->data_index];
        ps2_sprintf_s(pACInfo->name_com, marena_fmt_s,
                      (const char *)DF_MSTR_GetName(MenuMng.language, 0,
                                                    g_aVsRankerTable[(int)w->data_index] + 0x7a183));
        Voice_Call(0x3ea, 0x7f);
        w->phase = 3;
        pMobj->count = 0;
        mar_fade_out_all(w);
    }
}

/* tear the screen down: all 11 cards, the cursor, the frame and the ranking manager (MArenaAsemMng is left alone) */
static void __MArenaRanking_DeleteItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_feecbe04 *w = RK(pMobj);

    if (w->pMobjRankingMng) {
        MRankingMng_Cleanup(w->pMobjRankingMng);
        if (w->pMobjRankingMng) {
            mar_erase(w->pMobjRankingMng);
            w->pMobjRankingMng = 0;
        }
    }
    for (int i = 0; i < 11; i++) { /* all 11 slots regardless of maxCard; Cleanup before the null test */
        MUnitRankerName3_Cleanup(w->apMobjUnitRankerName[i]);
        if (w->apMobjUnitRankerName[i]) {
            mar_erase(w->apMobjUnitRankerName[i]);
            w->apMobjUnitRankerName[i] = 0;
        }
    }
    MUnitRankerName3_Cleanup(w->pMobjUnitRankerNameForCursor);
    if (w->pMobjUnitRankerNameForCursor) {
        mar_erase(w->pMobjUnitRankerNameForCursor);
        w->pMobjUnitRankerNameForCursor = 0;
    }
    MUnitArenaFrame_Cleanup(w->pMobjUnitArenaFrame);
    if (w->pMobjUnitArenaFrame) {
        mar_erase(w->pMobjUnitArenaFrame);
        w->pMobjUnitArenaFrame = 0;
    }
    /* the scene label is popped only when the menu is not in states 2..6 (CreateItem pushes unconditionally) */
    if ((unsigned int)(MenuMng.stat - 2) >= 5) DF_MBACK_PopSceneLabel();
}

/* fade everything in and set the help line (mode 1 has no DF_M1HELP_FadeIn) */
static void __MArenaRanking_FadeInItem(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x60);
    struct struct_feecbe04 *w = RK(pMobj);

    for (int i = 0; i < (w->maxCard < 11 ? w->maxCard : 11); i++)
        MUnitRankerName3_FadeInStart(w->apMobjUnitRankerName[i], phase);
    MUnitRankerName3_FadeInStart(w->pMobjUnitRankerNameForCursor, phase);
    MUnitArenaFrame_FadeInStart(w->pMobjUnitArenaFrame, phase);
    __MArenaRanking_SetDataToUnitFrame(pMobj, w->data_index);
    DF_MVIEW_MotionStart(MViewp, 0x54);
    if (w->mode == 1) {
        DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, 0xdbf74));
    } else {
        DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x1ad31));
        DF_M1HELP_FadeIn();
    }
}

/* create the 11 cards (always all 11), the cursor card and the AC frame, and push the screen's scene label */
static void __MArenaRanking_CreateItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct struct_feecbe04 *w = RK(pMobj);

    for (int i = 0; i < 11; i++)
        w->apMobjUnitRankerName[i] = MUnitRankerName3_NewSystem(pMobj, (char)i, 10, w->mode);
    for (int i = 0; i < (w->maxCard < 11 ? w->maxCard : 11); i++)
        __MArenaRanking_SetDataToUnitRankerName(pMobj, i, (char)i);
    w->pMobjUnitRankerNameForCursor = MUnitRankerName3_NewSystem(pMobj, -1, 10, w->mode);
    w->pMobjUnitArenaFrame = MUnitArenaFrame_NewSystem(pMobj, 3);
    if (w->mode == 1) DF_MBACK_PushSceneLabel((enum_5124d09c)0xe);
    else DF_MBACK_PushSceneLabel((enum_5124d09c)0x5d);
}

static void __MArenaRanking_Pkt(void)
{
}

static void __MArenaRanking_Draw(void)
{
}

/* phase 1 = live, 2 = 13-frame wait then hand over (no writer in this file), 3 = 13-frame fade-out then delete and
 * signal the parent with stat 5, 4 = wait for a child object and the view motion, then come back (no writer either) */
static void __MArenaRanking_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_feecbe04 *w = RK(pMobj);

    switch (w->phase) {
    case 4: {
        struct stMobj *pChild = w->pChild;
        if (pChild == 0) return;
        if (pChild->stat != 5) return;
        if (DF_MVIEW_MotionSync(MViewp) == 0) return;
        if (w->pChild) {
            mar_erase(w->pChild);
            w->pChild = 0;
        }
        w->phase = 1;
        pMobj->stat = 1;
        pMobj->count = 0;
        __MArenaRanking_FadeInItem(pMobj, 5); /* a1 = 5 at 0x1e03af0 (NewSystem's call is the one that passes 1) */
        break;
    }
    case 3: {
        unsigned char count = pMobj->count;
        pMobj->count = count + 1;
        if (count >= 12) {
            __MArenaRanking_DeleteItem(pMobj);
            pMobj->stat = 5;
            pMobj->play_func = 0;
            pMobj->draw_func = 0;
            pMobj->pkt_func = 0;
        }
        break;
    }
    case 2: {
        unsigned char count = pMobj->count;
        pMobj->count = count + 1;
        if (count >= 12) {
            w->phase = 4;
            pMobj->stat = 3;
        }
        break;
    }
    case 1:
        __MArenaRanking_PlayPhaseMain(pMobj);
        break;
    default:
        break;
    }
}

struct stMobj *MArenaRanking_NewSystem(struct stMobj *pParentMobj, char mode, void *pOutput)
{
    PS2_FRAME(0x50);
    struct stMobj *pMobj = DF_MOBJ_GetNewObject(pParentMobj);
    if (pMobj == 0) return 0;

    struct struct_feecbe04 *w = RK(pMobj);
    memset(w, 0, 0x4c); /* the work area only: stMobj's own header is not cleared */
    w->mode = mode;
    w->pOutput = pOutput;
    if (w->mode == 1) {
        __MArenaRanking_SetNextReadID(pMobj, 0);
        w->maxCard = 0x1e;
    } else {
        w->idNextRead = g_aVsRankerTable[0] + 0x18f;
        w->maxCard = 10;
    }
    pMobj->type = 0;
    pMobj->stat = 1;
    pMobj->count = 0;
    pMobj->play_func = (void (*)(void))GFN(__MArenaRanking_Play);
    pMobj->draw_func = (void (*)(void))GFN(__MArenaRanking_Draw);
    pMobj->pkt_func = (void (*)(void))GFN(__MArenaRanking_Pkt);
    __MArenaRanking_CreateItem(pMobj);
    w->phase = 1;
    pMobj->stat = 1;
    pMobj->count = 0;
    __MArenaRanking_FadeInItem(pMobj, 1); /* phase 1: the caller's a1 is still 1 here (not 5) */
    return pMobj;
}
