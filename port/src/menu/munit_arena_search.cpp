/* src/menu/munit_arena_search.cpp - the left-hand "element board" of the Arena SEARCH page: 18 element tiles in a
 * 5x5 grid (g_aIndexToElementTable, -1 = no tile), their shadows, a cursor (tile cursor / button-row cursor), 8 dots,
 * 6 strings and 5 horizontal guide lines. The unit's work block (struct_a465058f) is DF_MMM_Alloc'd and pMobj->dt
 * holds a POINTER to it. 56 sprite / string mobjs come from the file's 7-record stMobjManager table (layout 0x44e),
 * plus two plain child mobjs: pMobjNullIta (draws the base "null" sprite over every tile the player cannot pick) and
 * pMobjLine (the guide lines, alpha-faded by DrawLine_Play). The page slides in / out by posMove (MoveMng ease,
 * 0x130 px) through the phase byte driven from Play.
 * Driver: interpreted menu/marena_search.c (NewSystem / FadeInStart / FadeOutStart / ChangeCursor / GetSearchFlag /
 * ResetSearchFlag / FadeInDot / Cleanup).
 * The table's record 0 pLayoutHeader is rewritten before every MOBJManager_* call, so the hybrid keeps the table at
 * its PS2 address (nodwarf munit_arena_search_mng); the sprintf formats are nodwarf PS2 strings.
 * FadeOutDot is not ported yet (never reached in the replays; interpreted). Select toggles a tile (ACArenaWin).
 * Original quirks kept (ported literally, marked (sic)). */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU 0x1ee1260..0x1ee12a7 / main sdata 0x27bf78..0x27bf8f: the table's layout names */
static char munit_arena_search_name_ita[] = "ita_arena_search";
static char munit_arena_search_name_kage[] = "kage%d";
static char munit_arena_search_name_string[] = "string%d";
static char munit_arena_search_name_cursor[] = "cursor%d";
static char munit_arena_search_name_null[] = "null_%d";
static char munit_arena_search_name_dot[] = "dot%d";
char munit_arena_search_fmt_element[12] = "element%d";
char munit_arena_search_str_element0[16] = "element0";
char munit_arena_search_str_element1[16] = "element18";
/* OLMENU 0x1ee12b0 (7 x 0x40) */
struct stMobjManager munit_arena_search_mng[7] = {
    {.pLayoutName = munit_arena_search_name_ita, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 1},
    {.pLayoutName = munit_arena_search_name_kage, .pLayoutHeader = 0, .numContinue = 19, .typeMObj = 1},
    {.pLayoutName = munit_arena_search_fmt_element, .pLayoutHeader = 0, .numContinue = 19, .typeMObj = 1},
    {.pLayoutName = munit_arena_search_name_null, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 1},
    {.pLayoutName = munit_arena_search_name_string, .pLayoutHeader = 0, .numContinue = 6, .typeMObj = 2,
     .flag = 0, .dummy_c0 = {0}, .exString = {.category = 0, .strid = 0xdbdd0}},
    {.pLayoutName = munit_arena_search_name_cursor, .pLayoutHeader = 0, .numContinue = 2, .typeMObj = 4},
    {.pLayoutName = munit_arena_search_name_dot, .pLayoutHeader = 0, .numContinue = 8, .typeMObj = 1},
};
/* DWARF global owned by this file (OLMENU 0x1ee62f0), also read by munit_ez_match / _official / _ranking:
 * tile index of grid cell [y][x], -1 = no tile */
char g_aIndexToElementTable[5][5] = {
    {0x00, 0x01, 0x02, -1, -1},
    {0x03, 0x04, 0x05, 0x06, -1},
    {0x07, 0x08, 0x09, 0x0a, 0x0b},
    {0x0c, 0x0d, 0x0e, -1, -1},
    {0x0f, 0x10, 0x11, -1, -1},
};
/* DWARF global owned by menu/marena_bridge.c (OLMENU 0x1ee4cf0, not ported yet; also read by munit_ez_official):
 * defined here for the source port - the marena_bridge port must not define it again. {lo, hi} rank buckets */
char g_aRankRangeTable[4][2] = {{1, 5}, {6, 10}, {11, 15}, {16, 30}};
#endif

#define MUAS_MNG munit_arena_search_mng
#define MUAS(o) (*(struct struct_a465058f **)&(o)->dt)
#define MUAS_NULLITA(o) ((struct struct_3ca1b328 *)&(o)->dt)
#define MUAS_LINE(o) ((struct struct_519b1c81 *)&(o)->dt)
/* the work block of a child mobj's parent unit */
#define MUAS_PARENT(o) MUAS(MUAS_NULLITA(o)->pParent)

static inline void muas_set_header(void)
{
    MUAS_MNG[0].pLayoutHeader = (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x44e);
}

/* f_draw = 0, f_erase = 1 (two read-modify-writes of byte 0) */
static inline void muas_erase(struct stMobj *o)
{
    o->f_draw = 0;
    o->f_erase = 1;
}

/* line mobj draw_func (despite the name): the 5 guide lines at the current alpha, shifted by posMove */
static void __MUnitArenaSearch_DrawLine_Pkt(struct stMobj *pMobj)
{
    PS2_FRAME(0xd0);
    struct struct_a465058f *w = MUAS_PARENT(pMobj);
    struct struct_76b810ec aColor[2] = {{0x44, 0x4e, 0x4e, 0}, {0x44, 0x4e, 0x4e, 0}};
    int aPosX[2] = {29, 610};
    int aPosY[5] = {0x58, 0xb8, 0xec, 0x120, 0x154};
    int aPos[2][4];
    unsigned int i;

    aColor[1].a = MUAS_LINE(pMobj)->alpha;
    aColor[0].a = MUAS_LINE(pMobj)->alpha;
    for (i = 0; i < 5; i++) {
        memset(aPos, 0, sizeof(aPos));
        aPos[0][0] = aPosX[0] + w->posMove;
        aPos[0][1] = aPosY[i];
        aPos[1][0] = aPosX[1] + w->posMove;
        aPos[1][1] = aPosY[i];
        DF_MDRAW_DrawPrim(2, 2, aPos, aColor, 1.0f, 0);
    }
}

/* line mobj play_func while fading: stat 1 = fade in, stat 2 = fade out, 7 frames each (count 0..6) */
static void __MUnitArenaSearch_DrawLine_Play(struct stMobj *pMobj)
{
    struct struct_519b1c81 *l = MUAS_LINE(pMobj);

    if (pMobj->stat == 2) {
        if (pMobj->count >= 7) {
            pMobj->stat = 5;   /* (sic) fade-out ends in stat 5 */
            pMobj->play_func = 0;
        } else {
            float q = ps2_div((float)pMobj->count, 6.0f);
            l->alpha = (char)ps2_ftoi((float)l->alpha_max * (1.0f - q));
        }
    } else if (pMobj->stat == 1) {
        if (pMobj->count >= 7) {
            pMobj->stat = 0;
            pMobj->play_func = 0;
        } else {
            float q = ps2_div((float)pMobj->count, 6.0f);
            l->alpha = (char)ps2_ftoi((float)l->alpha_max * q);
        }
    }
    pMobj->count = pMobj->count + 1;
}

/* null-ita mobj draw_func: the base sprite (apMobj[39]) drawn over every tile the player cannot select */
static void __MUnitArenaSearch_NullIta_Draw(struct stMobj *pMobj)
{
    PS2_FRAME(0xe0);
    PS2_STACK(char[32], name, 0x70);
    struct struct_a465058f *w = MUAS_PARENT(pMobj);
    struct _LAYOUT_RET_DATA layout;
    int x, y;

    for (y = 0; y < 5; y++) {
        char *pFlagCan = (char *)&w->flagSearchCan + y;
        for (x = 0; x < 5; x++) {
            int index = g_aIndexToElementTable[y][x];
            if (index < 0) continue;
            if ((pFlagCan[0] >> x) & 1) continue;
            ps2_sprintf_d(name, munit_arena_search_fmt_element, index);
            /* (sic) a failed lookup leaves both loops and the function */
            if (!DF_LO_GetLayoutData((unsigned char *)LAYOUT_GetLayoutData(0x44e), (unsigned char *)name, 0, &layout))
                return;
            layout.Pos.x = layout.Pos.x + w->posMove;
            UnitSprite_SetPosition(w->apMobj[39], (struct struct_214acd28 *)&layout.Pos);
            UnitSprite_Draw(w->apMobj[39]);
        }
    }
}

/* pMobj = the null-ita mobj */
static void __MUnitArenaSearch_NullIta_FadeOut(struct stMobj *pMobj)
{
    PS2_FRAME(0x10);
    struct struct_a465058f *w = MUAS_PARENT(pMobj);
    char numDraw = 0;
    int x, y;

    for (y = 0; y < 5; y++) {
        for (x = 0; x < 5; x++) {
            if (g_aIndexToElementTable[y][x] >= 0 && !((((char *)&w->flagSearchCan)[y] >> x) & 1)) {
                numDraw = (char)(numDraw + 1);
                break;   /* (sic) leaves only the inner loop: numDraw counts rows */
            }
        }
    }
    if (numDraw) UnitSprite_SetFadeOut(w->apMobj[39]);
    else DF_MOBJ_SetDrawEnable(w->apMobj[39], 0);
}

/* pMobj = the null-ita mobj: hide the element sprites of the unselectable tiles, fade the base sprite in */
static void __MUnitArenaSearch_NullIta_FadeIn(struct stMobj *pMobj)
{
    PS2_FRAME(0x80);
    struct struct_a465058f *w = MUAS_PARENT(pMobj);
    char numDraw = 0;
    int x, y;

    for (y = 0; y < 5; y++) {
        for (x = 0; x < 5; x++) {
            int index = g_aIndexToElementTable[y][x];
            if (index >= 0 && !((((char *)&w->flagSearchCan)[y] >> x) & 1)) {
                DF_MOBJ_SetDrawEnable(w->apMobj[20 + index], 0);
                numDraw = (char)(numDraw + 1);
            }
        }
    }
    if (numDraw) {
        DF_MOBJ_SetDrawEnable(w->apMobj[39], 1);
        UnitSprite_SetFadeIn(w->apMobj[39]);
    } else {
        DF_MOBJ_SetDrawEnable(w->apMobj[39], 0);
    }
}

/* row 0..4: the tile cursor (apMobj[46]) moves onto tile [y][x]; row 5: the button-row cursor (apMobj[47]) */
static void __MUnitArenaSearch_ChangeCursor(struct stMobj *pMobj, int cursor_index_x, int cursor_index_y)
{
    PS2_FRAME(0xf0);
    PS2_STACK(char[32], name, 0x30);
    struct _LAYOUT_RET_DATA layout;
    struct _LAYOUT_RET_DATA layoutOffset;
    struct struct_a465058f *w = MUAS(pMobj);

    if (cursor_index_y != 5) {
        char index = g_aIndexToElementTable[cursor_index_y][cursor_index_x];
        /* no conversion in the format; a2 still holds cursor_index_y */
        ps2_sprintf_d(name, munit_arena_search_str_element0, cursor_index_y);
        if (!DF_LO_GetLayoutData((unsigned char *)LAYOUT_GetLayoutData(0x44e), (unsigned char *)name, 0,
                                 &layoutOffset))
            return;
        ps2_sprintf_d(name, munit_arena_search_fmt_element, index);
        if (!DF_LO_GetLayoutData((unsigned char *)LAYOUT_GetLayoutData(0x44e), (unsigned char *)name, 0, &layout))
            return;
        layout.Pos.x = layout.Pos.x - layoutOffset.Pos.x;
        layout.Pos.y = layout.Pos.y - layoutOffset.Pos.y;
        UnitSprite_SetPosition(w->apMobj[46], (struct struct_214acd28 *)&layout.Pos);
        DF_MOBJ_SetDrawEnable(w->apMobj[46], 1);
        DF_MOBJ_SetDrawEnable(w->apMobj[47], 0);
    } else {
        ps2_sprintf_d(name, munit_arena_search_str_element1, cursor_index_y);
        if (!DF_LO_GetLayoutData((unsigned char *)LAYOUT_GetLayoutData(0x44e), (unsigned char *)name, 0, &layout))
            return;
        DF_MOBJ_SetDrawEnable(w->apMobj[47], 1);
        DF_MOBJ_SetDrawEnable(w->apMobj[46], 0);
    }
}

static void __MUnitArenaSearch_CreateItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    PS2_STACK(struct struct_76b810ec, color, 0x4c);
    struct struct_a465058f *w = MUAS(pMobj);
    unsigned int col = 0x8061a5de;   /* main sdata 0x27bf90, copied as raw bits (lwc1 / swc1) */
    struct stMobj *o;

    memcpy(&color, &col, 4);
    muas_set_header();
    MOBJManager_Create(pMobj, w->apMobj, 7, MUAS_MNG);
    DF_MOBJ_SetStringColorAll(w->apMobj[40], &color);

    w->pMobjNullIta = DF_MOBJ_GetNewObject(pMobj);
    if (w->pMobjNullIta == 0) return;
    memset(MUAS_NULLITA(w->pMobjNullIta), 0, 4);
    MUAS_NULLITA(w->pMobjNullIta)->pParent = pMobj;
    w->pMobjNullIta->draw_func = (void (*)(void))GFN(__MUnitArenaSearch_NullIta_Draw);

    w->pMobjLine = DF_MOBJ_GetNewObject(pMobj);
    o = w->pMobjLine;
    if (o == 0) return;
    memset(MUAS_LINE(o), 0, 8);
    MUAS_LINE(o)->pParent = pMobj;
    MUAS_LINE(o)->alpha = 0;
    MUAS_LINE(o)->alpha_max = 0x20;
    w->pMobjLine->draw_func = (void (*)(void))GFN(__MUnitArenaSearch_DrawLine_Pkt);
}

static void __MUnitArenaSearch_Pkt(void)
{
}

static void __MUnitArenaSearch_Draw(void)
{
}

/* the page slide: (sic) sequential ifs, so 1 -> 2, 5 -> 6, 9 -> 10, 13 -> 14 run in the same call */
static void __MUnitArenaSearch_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct struct_a465058f *w = MUAS(pMobj);
    struct struct_a465058f *m;
    int v;

    if (w->phase == 0) return;
    muas_set_header();

    if (w->phase == 1) {
        MoveMng_Init(&w->stMoveMng);
        m = MUAS(pMobj);
        muas_set_header();
        MOBJManager_ResetPos(MUAS_MNG, 7, &m->apMobj[0]);
        m->posMove = 0;
        m = MUAS(pMobj);
        muas_set_header();
        MOBJManager_Move(MUAS_MNG, 7, &m->apMobj[0], 0x130, 0, 0);
        m->posMove = m->posMove + 0x130;
        w->phase = 2;
    }
    if (w->phase == 2) {
        v = MoveMng_GetNextMoveValue(&w->stMoveMng, -0x130, 8, 1);
        if (v == -99999) {
            w->phase = 0;
        } else {
            m = MUAS(pMobj);
            muas_set_header();
            MOBJManager_Move(MUAS_MNG, 7, &m->apMobj[0], v, 0, 0);
            m->posMove = m->posMove + v;
        }
    }
    if (w->phase == 5) {
        MoveMng_Init(&w->stMoveMng);
        m = MUAS(pMobj);
        muas_set_header();
        MOBJManager_ResetPos(MUAS_MNG, 7, &m->apMobj[0]);
        m->posMove = 0;
        m = MUAS(pMobj);
        muas_set_header();
        MOBJManager_Move(MUAS_MNG, 7, &m->apMobj[0], -0x130, 0, 0);
        m->posMove = m->posMove - 0x130;
        w->phase = 6;
    }
    if (w->phase == 6) {
        v = MoveMng_GetNextMoveValue(&w->stMoveMng, 0x130, 8, 1);
        if (v == -99999) {
            w->phase = 0;
        } else {
            m = MUAS(pMobj);
            muas_set_header();
            MOBJManager_Move(MUAS_MNG, 7, &m->apMobj[0], v, 0, 0);
            m->posMove = m->posMove + v;
        }
    }
    if (w->phase == 9) {
        MoveMng_Init(&w->stMoveMng);
        w->phase = 10;
    }
    if (w->phase == 10) {
        v = MoveMng_GetNextMoveValue(&w->stMoveMng, -0x130, 8, 1);
        if (v == -99999) {
            m = MUAS(pMobj);
            muas_set_header();
            MOBJManager_ResetPos(MUAS_MNG, 7, &m->apMobj[0]);
            m->posMove = 0;
            w->phase = 0;
        } else {
            m = MUAS(pMobj);
            muas_set_header();
            MOBJManager_Move(MUAS_MNG, 7, &m->apMobj[0], v, 0, 0);
            m->posMove = m->posMove + v;
        }
    }
    if (w->phase == 13) {
        MoveMng_Init(&w->stMoveMng);
        w->phase = 14;
    }
    if (w->phase == 14) {
        v = MoveMng_GetNextMoveValue(&w->stMoveMng, 0x130, 8, 1);
        if (v == -99999) {
            m = MUAS(pMobj);
            muas_set_header();
            MOBJManager_ResetPos(MUAS_MNG, 7, &m->apMobj[0]);
            m->posMove = 0;
            w->phase = 0;
        } else {
            m = MUAS(pMobj);
            muas_set_header();
            MOBJManager_Move(MUAS_MNG, 7, &m->apMobj[0], v, 0, 0);
            m->posMove = m->posMove + v;
        }
    }
    if (w->phase == 0x11) w->phase = 0;
    if (w->phase == 0x12) w->phase = 0;
}

/* restore the last committed flags */
void MUnitArenaSearch_ResetSearchFlag(struct stMobj *pMobj)
{
    struct struct_a465058f *w = MUAS(pMobj);

    memcpy(&w->flagSearch, &w->flagSearchTemp, 0x10);
}

/* nothing picked: default to the player's own rank bucket (rank < 100) or class (rank >= 100) */
void MUnitArenaSearch_GetSearchFlag(struct stMobj *pMobj, struct stAsmSearchFlag *pFlagSearch)
{
    struct struct_a465058f *w = MUAS(pMobj);

    if (w->flagSearch.Class.Data == 0 && w->flagSearch.Rank == 0) {
        int v = g_gamedata[0].arenaBattle.rank;
        if (v < 100) {
            unsigned int i;
            for (i = 0; i < 4; i++) {
                if (v < g_aRankRangeTable[i][0]) continue;
                if (!(g_aRankRangeTable[i][1] < v)) break;
            }
            w->flagSearch.Rank = (unsigned char)(1 << i);   /* (sic) 16 when no bucket matched */
        } else {
            w->flagSearch.Class.Data = (char)(1 << (v - 100));
        }
    }
    memcpy(pFlagSearch, &w->flagSearch, 0x10);
}

void MUnitArenaSearch_FadeInDot(struct stMobj *pMobj, int dot_index)
{
    PS2_FRAME(0x20);
    struct stMobj **pp = &MUAS(pMobj)->apMobj[48 + dot_index];

    DF_MOBJ_SetDrawEnable(*pp, 1);
    UnitSprite_SetFadeIn(*pp);
}

/* cross on tile [y][x]: toggle its search bit (tint the element sprite: picked = {0xd0, 0xf8, 0xf8}, else grey
 * 0x80) and commit the flags to flagSearchTemp; row 5 (button row) and tiles the player cannot pick do nothing */
void MUnitArenaSearch_Select(struct stMobj *pMobj, int cursor_index_x, int cursor_index_y)
{
    PS2_FRAME(0x50);
    struct struct_a465058f *w = MUAS(pMobj);
    char *pFlag = (char *)&w->flagSearch;
    char *pCan = (char *)&w->flagSearchCan;
    struct struct_76b810ec color;

    if (cursor_index_y == 5) return;
    if (!((pCan[cursor_index_y] >> cursor_index_x) & 1)) return;
    int index = g_aIndexToElementTable[cursor_index_y][cursor_index_x];
    if ((pFlag[cursor_index_y] >> cursor_index_x) & 1) {
        color.r = 0x80; color.g = 0x80; color.b = 0x80; color.a = 0x80;
        UnitSprite_SetMultiply(w->apMobj[20 + index], &color);
        pFlag[cursor_index_y] = (char)(pFlag[cursor_index_y] & ~(1 << cursor_index_x));
    } else {
        color.r = 0xd0; color.g = 0xf8; color.b = 0xf8; color.a = 0x80;
        UnitSprite_SetMultiply(w->apMobj[20 + index], &color);
        pFlag[cursor_index_y] = (char)(pFlag[cursor_index_y] | (1 << cursor_index_x));
    }
    memcpy(&w->flagSearchTemp, &w->flagSearch, 0x10);
}

void MUnitArenaSearch_ChangeCursor(struct stMobj *pMobj, int cursor_index_x, int cursor_index_y)
{
    __MUnitArenaSearch_ChangeCursor(pMobj, cursor_index_x, cursor_index_y);
}

void MUnitArenaSearch_FadeOutStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x20);
    struct struct_a465058f *w;

    MUAS(pMobj)->phase = phase;
    w = MUAS(pMobj);
    muas_set_header();
    MOBJManager_FadeOut(&w->apMobj[0], 0, 7, MUAS_MNG);
    __MUnitArenaSearch_NullIta_FadeOut(w->pMobjNullIta);
    w->pMobjLine->play_func = (void (*)(void))GFN(__MUnitArenaSearch_DrawLine_Play);
    w->pMobjLine->stat = 2;
    w->pMobjLine->count = 0;
}

void MUnitArenaSearch_FadeInStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x30);
    struct struct_a465058f *w1 = MUAS(pMobj);
    struct struct_a465058f *w;
    int i;

    w1->phase = phase;
    w = MUAS(pMobj);
    muas_set_header();
    MOBJManager_FadeIn(&w->apMobj[0], 0, 7, MUAS_MNG);
    __MUnitArenaSearch_NullIta_FadeIn(w->pMobjNullIta);
    w->pMobjLine->play_func = (void (*)(void))GFN(__MUnitArenaSearch_DrawLine_Play);
    w->pMobjLine->stat = 1;
    w->pMobjLine->count = 0;
    DF_MOBJ_SetDrawEnable(w1->apMobj[47], 0);
    for (i = 0x30; i < 0x38; i++) DF_MOBJ_SetDrawEnable(w1->apMobj[i], 0);   /* the 8 dots */
}

void MUnitArenaSearch_Cleanup(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    struct struct_a465058f *w;

#ifndef ACNB_SOURCEPORT
    DF_MMM_Free(0, MUAS(pMobj));   /* (sic) the work block is freed first and used below */
#endif
    w = MUAS(pMobj);
    muas_set_header();
    MOBJManager_Delete(&w->apMobj[0], 7, MUAS_MNG);
    if (w->pMobjNullIta != 0) {
        muas_erase(w->pMobjNullIta);
        w->pMobjNullIta = 0;
    }
    if (w->pMobjLine != 0) {
        muas_erase(w->pMobjLine);
        w->pMobjLine = 0;
    }
#ifdef ACNB_SOURCEPORT
    DF_MMM_Free(0, w);   /* source port: freed after the last use (same visible behaviour) */
#endif
}

struct stMobj *MUnitArenaSearch_NewSystem(struct stMobj *pParentMobj)
{
    PS2_FRAME(0x30);
    struct stMobj *m = DF_MOBJ_GetNewObject(pParentMobj);
    struct struct_a465058f *w;

    if (m == 0) return 0;
    MUAS(m) = (struct struct_a465058f *)DF_MMM_Alloc(0, 0x140);
    w = MUAS(m);
    memset(w, 0, 0x134);
    memcpy(&w->flagSearchCan, &g_gamedata[0].asmSearchFlag, 0x10);
    m->type = 0;
    m->stat = 1;
    m->count = 0;
    m->play_func = (void (*)(void))GFN(__MUnitArenaSearch_Play);
    m->draw_func = (void (*)(void))GFN(__MUnitArenaSearch_Draw);
    m->pkt_func = (void (*)(void))GFN(__MUnitArenaSearch_Pkt);
    __MUnitArenaSearch_CreateItem(m);
    return m;
}
