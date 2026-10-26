/* src/menu/marena_official.cpp - the OFFICIAL page of the ARENA top menu: up to two invitation cards
 * (g_gamedata[0].officialBattle[0/1], idRanker != 0 = card present). NewSystem (called by marena_top.cpp's
 * __MArenaTop_Play) builds the page mobj with an inline payload (obj+0x10, struct_d0a47ef8): two MUnitOfficial card
 * units, one MUnitArenaFrame (the 3D view of the highlighted opponent's AC) and, once loading is done, an
 * MRankingMng. PlayPhaseMain (phase 1) runs the load pipeline (`step`: asem data -> arena map picture -> the two
 * emblem textures -> ranking manager) and the pad: Down / Up move the cursor between the cards, triangle switches the
 * frame / emblem view, circle leaves, cross opens the confirm dialog (phase 2); "yes" starts the official match
 * (phase 3: MArenaStarter_CreateNextStageInfo(2, cursor)), then phase 4 tears the page down and sets stat = 5, which
 * __MArenaTop_Play polls.
 * Unreached (REV, asm-only): PlayPhaseDispatch (dead: nothing stores phase 5 or pChild), the START and circle arms,
 * the `Class == 0` and `idRanker == 0` frame arms, cross with no card, step 4's alloc failure, the NewSystem
 * one-card-A / no-card states and alloc failure.
 * Original quirks kept (marked (sic)). */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU 0x1edaaa0: sprintf format (no arguments) */
char marena_official_fmt_dummy_emblem[13] = "dummy_emblem";
#endif

/* obj+0x10: the page payload */
#define AO(o) ((struct struct_d0a47ef8 *)&(o)->dt)

/* the inlined "f_draw = 0; f_erase = 1", then the slot is cleared */
static inline void mofficial_erase(struct stMobj **slot)
{
    if (*slot) {
        (*slot)->f_draw = 0;
        (*slot)->f_erase = 1;
        *slot = NULL;
    }
}

/* "show the AC of card cursor_index in the frame unit" (pasted 4x in the original: 0x1e01340, 0x1e01568, 0x1e016f8,
 * 0x1e023c8) */
static inline void mofficial_show_card(struct struct_d0a47ef8 *w)
{
    struct _arena_official_card_st *ob = &g_gamedata[0].officialBattle[(int)w->cursor_index];

    if (ob->idRanker != 0) {
        int rank;
        if (ob->flagSearch.Class.Data != 0) rank = (ob->flagSearch.Class.Data >> 1) + 100;
        else rank = ob->flagSearch.Rank;
        MUnitArenaFrame_SetData(w->pMobjUnitArenaFrame, ob->nameAC, rank);
    } else {
        MUnitArenaFrame_SetData(w->pMobjUnitArenaFrame, NULL, 100);
    }
}

/* the help line (pasted 3x), skipped while MenuMng.stat is 2..6 */
static inline void mofficial_help(int cursor)
{
    if ((unsigned int)(MenuMng.stat - 2) >= 5)
        DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, cursor + 0xdbf6a));
}

/* (sic) dead: called for phase 5, which nothing stores, and pChild is never set in this file */
static void __MArenaOfficial_PlayPhaseDispatch(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct struct_d0a47ef8 *w = AO(pMobj);
    int i;

    if (w->pChild == NULL || w->pChild->stat != 5) return;
    if (!DF_MVIEW_MotionSync(MViewp)) return;
    mofficial_erase(&w->pChild);
    w->phase = 1;
    pMobj->stat = 1;
    pMobj->count = 0;
    for (i = 0; i < 2; i++) MUnitOfficial_FadeInStart(w->apMobjUnitOfficial[i], 5);
    MUnitArenaFrame_FadeInStart(w->pMobjUnitArenaFrame, 5);
    mofficial_show_card(w);
    DF_MVIEW_MotionStart(MViewp, 0x54);
}

static void __MArenaOfficial_PlayPhaseMain(struct stMobj *pMobj)
{
    PS2_FRAME(0x1d0);
    PS2_STACK(char[256], buff, 0xc0);
    struct struct_d0a47ef8 *w = AO(pMobj);
    struct _LAYOUT_RET_DATA layout;   /* sp+0x70 */
    int uv[4] __attribute__((aligned(16))) = {0, 0, 0x80, 0x80};   /* sp+0x1c0, OLMENU 0x1edaa90 */
    int i, k;

    if (pMobj->stat == 1) {
        unsigned char c = pMobj->count;
        pMobj->count = c + 1;
        if (c < 6) return;
        pMobj->stat = 0;
    }
    if (MenuKeyGuide_IsShown()) return;
    if (MPad[MPadUser].start == 1) {
        MenuKeyGuide_Show(0x4b);
        return;
    }
    if (MPad[MPadUser].repeat.dw == 1) {
        Voice_Call(0x3e9, 0x7f);
        w->cursor_index++;
        if (w->cursor_max < w->cursor_index) w->cursor_index = w->cursor_min;
        k = w->cursor_index;
        MUnitOfficial_ChangeCursor(w->apMobjUnitOfficial[0], k);   /* (sic) always unit 0; a2 = 0 in the original */
        mofficial_help(k);
        mofficial_show_card(w);
        if (w->pMobjRankingMng) {
            MRankingMng_SetNextReadID(w->pMobjRankingMng, g_gamedata[0].officialBattle[(int)w->cursor_index].idRanker);
            return;
        }
        /* (sic) no ranking manager yet: falls through into the next key tests */
    }
    if (MPad[MPadUser].repeat.up == 1) {
        Voice_Call(0x3e9, 0x7f);
        w->cursor_index--;
        if (w->cursor_index < w->cursor_min) w->cursor_index = w->cursor_max;
        k = w->cursor_index;
        MUnitOfficial_ChangeCursor(w->apMobjUnitOfficial[0], k);
        mofficial_help(k);
        mofficial_show_card(w);
        if (w->pMobjRankingMng) {
            MRankingMng_SetNextReadID(w->pMobjRankingMng, g_gamedata[0].officialBattle[(int)w->cursor_index].idRanker);
            return;
        }
        /* (sic) falls through */
    }
    if (MPad[MPadUser].Rup == 1) {
        if (w->pMobjRankingMng) {
            MUnitArenaFrame_ChangeMode(w->pMobjUnitArenaFrame, -1);
            MRankingMng_ChangeEmblemMode(w->pMobjRankingMng, -1);
        }
        return;
    }

    switch (w->step) {   /* jump table OLMENU 0x1ee4d20; sltiu: steps < 0 or >= 7 take the case-6 code (default) */
    case 0:
        MArenaAsemMng_SetUp();
        MArenaBridge_Init();
        w->step = 1;
        return;
    case 1:
        if (MArenaAsemMng_Play() == 3 && (signed char)MArenaBridge_Loading() == 6) {
            char *asem = MArenaAsemMng_GetAsemData();
            for (i = 0; i < 2; i++) {
                int id = g_gamedata[0].officialBattle[i].idRanker;
                /* the player's colours for this card go INTO the asem record (memcpy dst = asem) */
                if (id != 0 && id < 0x190)
                    memcpy(asem + id * 0x120 + 0x20, g_gamedata[0].colorAutoGen[i], 0xd0);
            }
            w->step = 2;
        }
        return;
    case 2: {
        int size = Cdvd_GetSizeByte(0x1079);
        /* (sic) a full extra 0x800 when the size is already aligned */
        w->pArenaMap = (char *)DF_MMM_Alloc(0, size + (0x800 - size % 0x800));
        if (w->pArenaMap == NULL) return;
        Cdvd_ReadLsnBlind(0x1079, w->pArenaMap, 0, 0);
        w->step = 3;
        return;
    }
    case 3:
        if (File_IsAsyncBusy()) return;
        MenuTex_RegisterBndTexture(w->pArenaMap);
        for (i = 0; i < 2; i++) {
            struct _arena_official_card_st *ob = &g_gamedata[0].officialBattle[i];
            if (ob->idRanker != 0) {
                char idx = MArenaStarter_MapId2ArenaMapIndex(ob->map);
                MUnitOfficial_SetArenaMapID(w->apMobjUnitOfficial[i], idx + 0x2ee0);
                MUnitOfficial_SetArenaMapFadeIn(w->apMobjUnitOfficial[i]);
            }
        }
        w->step = 4;
        return;
    case 4: {
        int id = g_gamedata[0].officialBattle[(int)w->indexLoadEmblem].idRanker;
        if (id >= 0x190) {
            w->apEmblemData[(int)w->indexLoadEmblem] = (char *)DF_MMM_Alloc(0, 0x8840);
            if (w->apEmblemData[(int)w->indexLoadEmblem] == NULL) return;
            Cdvd_ReadLsnBlind(id - 0x18f + 0x1d4c, w->apEmblemData[(int)w->indexLoadEmblem], 0, 0);
        }
        w->step = 5;
        return;
    }
    case 5:
        MArenaAsemMng_GetAsemData();   /* (sic) result unused */
        if (File_IsAsyncBusy()) return;
        if (w->apEmblemData[(int)w->indexLoadEmblem]) {
            w->aTexID[(int)w->indexLoadEmblem] =
                MenuTex_RegisterSingleTex((struct struct_c53f69de *)w->apEmblemData[(int)w->indexLoadEmblem]);
            ps2_sprintf_d(buff, marena_official_fmt_dummy_emblem, 0);   /* newlib sprintf 0x118cd8, no arguments */
            if (!DF_LO_GetLayoutData((unsigned char *)LAYOUT_GetLayoutData(0x452), (unsigned char *)buff, 0, &layout))
                return;
            layout.Pos.y += w->indexLoadEmblem * 190;
            w->apMobjEmblem[(int)w->indexLoadEmblem] =
                DF_MOBJ_NXNewSprite(pMobj, w->aTexID[(int)w->indexLoadEmblem], (struct stMenuRect *)&layout.Pos, uv,
                                    &layout.Col, 0, 0, 0, 0);
            DF_MOBJ_SetDrawEnable(w->apMobjEmblem[(int)w->indexLoadEmblem], 0);
        }
        w->indexLoadEmblem++;
        if (w->indexLoadEmblem < 2) {
            w->step = 4;
            return;
        }
        if (g_gamedata[0].officialBattle[0].idRanker != 0 || g_gamedata[0].officialBattle[1].idRanker != 0) {
            w->pMobjRankingMng = MRankingMng_NewSystem(pMobj, 2, MArenaAsemMng_GetAsemData());
            MRankingMng_SetNextReadID(w->pMobjRankingMng, g_gamedata[0].officialBattle[(int)w->cursor_index].idRanker);
            MRankingMng_SetShow(w->pMobjRankingMng, 1);
        }
        w->step = 6;
        return;
    default:   /* 6 */
        for (i = 0; i < 2; i++)
            if (MUnitOfficial_IsMove(w->apMobjUnitOfficial[i])) return;
        if (w->flagFadeInEmblem == 0) {
            for (i = 0; i < 2; i++)
                if (w->apMobjEmblem[i]) DF_MOBJ_SetFadeIn(w->apMobjEmblem[i]);
            w->flagFadeInEmblem = 1;
        }
        if (MPad[MPadUser].Rrt == 1) {
            Voice_Call(0x3eb, 0x7f);
            w->phase = 4;
            pMobj->count = 0;
            __MArenaOfficial_FadeOutItem(pMobj, 0xd);
        } else if (MPad[MPadUser].Rdw == 1 &&
                   g_gamedata[0].officialBattle[(int)w->cursor_index].idRanker != 0) {
            Voice_Call(0x3ea, 0x7f);
            DF_MDIALOG_Open(2, DF_MSTR_GetName(MenuMng.language, 1, 0xdbf7e), NULL, 0, 0, 0, 0, 0);
            w->phase = 2;
        }
        return;
    }
}

static void __MArenaOfficial_DeleteItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_d0a47ef8 *w = AO(pMobj);
    int i;

    for (i = 0; i < 2; i++) {
        MUnitOfficial_Cleanup(w->apMobjUnitOfficial[i]);
        mofficial_erase(&w->apMobjUnitOfficial[i]);
    }
    MUnitArenaFrame_Cleanup(w->pMobjUnitArenaFrame);
    mofficial_erase(&w->pMobjUnitArenaFrame);
    if (w->pMobjRankingMng) {
        MRankingMng_Cleanup(w->pMobjRankingMng);
        mofficial_erase(&w->pMobjRankingMng);
    }
    MArenaAsemMng_Cleanup();
    MArenaBridge_Cleanup();
    if (w->pArenaMap) {
        MenuTex_UnRegisterBndTexture(w->pArenaMap);
        DF_MMM_Free(0, w->pArenaMap);   /* (sic) pointer not cleared */
    }
    for (i = 0; i < 2; i++) {
        if (w->apEmblemData[i]) {
            MenuTex_UnRegisterSingleTex(w->aTexID[i]);
            DF_MMM_Free(0, w->apEmblemData[i]);   /* (sic) pointer not cleared */
            mofficial_erase(&w->apMobjEmblem[i]);   /* (sic) only when the data pointer is set */
        }
    }
}

static void __MArenaOfficial_FadeOutItem(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x50);
    struct struct_d0a47ef8 *w = AO(pMobj);
    int i;

    for (i = 0; i < 2; i++) MUnitOfficial_FadeOutStart(w->apMobjUnitOfficial[i], phase);
    MUnitArenaFrame_FadeOutStart(w->pMobjUnitArenaFrame, phase);
    for (i = 0; i < 2; i++)
        if (w->apMobjEmblem[i]) DF_MOBJ_SetFadeOut(w->apMobjEmblem[i]);
    if (w->pMobjRankingMng) MRankingMng_SetShow(w->pMobjRankingMng, 0);
}

static void __MArenaOfficial_Pkt(void)
{
}

static void __MArenaOfficial_Draw(void)
{
}

static void __MArenaOfficial_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_d0a47ef8 *w = AO(pMobj);

    switch (w->phase) {
    case 5:
        __MArenaOfficial_PlayPhaseDispatch(pMobj);   /* (sic) dead */
        break;
    case 4: {
        unsigned char c = pMobj->count;
        pMobj->count = c + 1;
        if (c < 0xc) return;
        __MArenaOfficial_DeleteItem(pMobj);
        pMobj->stat = 5;
        pMobj->play_func = NULL;
        pMobj->draw_func = NULL;
        pMobj->pkt_func = NULL;
        break;
    }
    case 3:
        MArenaStarter_CreateNextStageInfo(2, w->cursor_index);
        DF_MENU_SetFadeOut(6);
        DF_MBACK_SetFadeOutNX();   /* the original passes 0 in a0 */
        Bgm_FadeOutStart();
        DF_MBACK_PushSceneLabel((enum_5124d09c)0);
        DF_MBGUIDE_Hide();
        DF_M1HELP_FadeOut();
        w->phase = 4;
        pMobj->count = 0;
        __MArenaOfficial_FadeOutItem(pMobj, 0xd);
        break;
    case 2:
        if ((DF_MDIALOG_Sync() & 4) == 4) {
            if (DF_MDIALOG_Sync() & 0x10) {
                w->phase = 3;
                pMobj->count = 0;
                __MArenaOfficial_FadeOutItem(pMobj, 9);
            } else {
                w->phase = 1;   /* (sic) back to Main */
            }
        }
        break;
    case 1:
        __MArenaOfficial_PlayPhaseMain(pMobj);
        break;
    }
}

struct stMobj *MArenaOfficial_NewSystem(struct stMobj *pParentMobj)
{
    PS2_FRAME(0x50);
    struct stMobj *m = DF_MOBJ_GetNewObject(pParentMobj);
    struct struct_d0a47ef8 *w;
    int i;

    if (m == NULL) return NULL;
    w = AO(m);
    memset(w, 0, 0x38);
    if (g_gamedata[0].officialBattle[0].idRanker != 0 && g_gamedata[0].officialBattle[1].idRanker != 0) {
        w->cursor_index = 0;
        w->cursor_min = 0;
        w->cursor_max = 1;
    } else if (g_gamedata[0].officialBattle[0].idRanker != 0) {
        w->cursor_index = 0;
        w->cursor_min = 0;
        w->cursor_max = 0;
    } else if (g_gamedata[0].officialBattle[1].idRanker != 0) {
        w->cursor_index = 1;
        w->cursor_min = 1;
        w->cursor_max = 1;
    }
    m->type = 0;
    m->stat = 1;
    m->count = 0;
    m->play_func = (void (*)(void))GFN(__MArenaOfficial_Play);
    m->draw_func = (void (*)(void))GFN(__MArenaOfficial_Draw);
    m->pkt_func = (void (*)(void))GFN(__MArenaOfficial_Pkt);
    for (i = 0; i < 2; i++) w->apMobjUnitOfficial[i] = MUnitOfficial_NewSystem(m, (char)i);
    w->pMobjUnitArenaFrame = MUnitArenaFrame_NewSystem(m, 2);
    MUnitOfficial_ChangeCursor(w->apMobjUnitOfficial[0], w->cursor_index);
    mofficial_help(w->cursor_index);
    w->phase = 1;
    m->stat = 1;
    m->count = 0;
    for (i = 0; i < 2; i++) MUnitOfficial_FadeInStart(w->apMobjUnitOfficial[i], 1);
    MUnitArenaFrame_FadeInStart(w->pMobjUnitArenaFrame, 1);
    mofficial_show_card(w);
    DF_MVIEW_MotionStart(MViewp, 0x54);
    return m;
}
