/* src/menu/munit_arena_top.cpp - the page-title unit of the ARENA top screen (child of menu/marena_top): three tab
 * plates (ita_arena0-2, apMobj[0..2]), three tab marks (mark0-2, apMobj[3..5]), three tab labels (string_menu0-2,
 * apMobj[6..8]) and a cursor plate (apMobj[9]) at y = 28 * cursor index. marena_top drives it through
 * FadeInStart / FadeOutStart (they only store the phase byte) and ChangeCursor; Play (play_func of the mobj) then
 * slides the three rows in from x = -0x140 (row j starts 2*j frames after row 0) or out to x = -0x140.
 * Play is a chain of independent `if (phase == X)` blocks that fall through within one call (1 / 5 -> 2,
 * 9 / 13 -> 14); the layout header is rewritten before every MOBJManager_* call (twice in phases 1 / 5: kept literal).
 * The table's record 0 pLayoutHeader is rewritten, so the hybrid keeps the table at its PS2 address (nodwarf
 * munit_arena_top_mng); the source port defines it below. */
#include <string.h>
#include "acnb/nodwarf.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU 0x1ee14a8 / main sdata 0x27bfa8 / OLMENU 0x1ee14b8 / 0x1ee14c8: the table's layout names */
static char munit_arena_top_name_ita[] = "ita_arena%d";
static char munit_arena_top_name_mark[] = "mark%d";
static char munit_arena_top_name_string[] = "string_menu%d";
static char munit_arena_top_name_cursor[] = "cursor%d";
/* OLMENU 0x1ee14e0 (4 x 0x40) */
struct stMobjManager munit_arena_top_mng[4] = {
    {.pLayoutName = munit_arena_top_name_ita, .pLayoutHeader = 0, .numContinue = 3, .typeMObj = 1},
    {.pLayoutName = munit_arena_top_name_mark, .pLayoutHeader = 0, .numContinue = 3, .typeMObj = 1},
    {.pLayoutName = munit_arena_top_name_string, .pLayoutHeader = 0, .numContinue = 3, .typeMObj = 2,
     .flag = 0, .dummy_c0 = {0}, .exString = {.category = 0, .strid = 0xdbd94}},
    {.pLayoutName = munit_arena_top_name_cursor, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 4},
};
#endif

#define MUAT_MNG munit_arena_top_mng
#define MUAT(o) ((struct struct_d0523e8e *)&(o)->dt)
#define MUAT_FINISHED (-99999)   /* MoveMng_GetNextMoveValue: move finished */

static inline void muat_set_header(void)
{
    MUAT_MNG[0].pLayoutHeader = (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x449);
}

static void __MUnitArenaTop_Pkt(void)
{
}

static void __MUnitArenaTop_Draw(void)
{
}

static void __MUnitArenaTop_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x110);
    struct struct_d0523e8e *pWork = MUAT(pMobj);
    struct struct_214acd28 pos;
    struct struct_214acd28 p;
    int i, j, v;
    char idx;

    if (pWork->phase == 0) return;

    /* start slide-in */
    if (pWork->phase == 1 || pWork->phase == 5) {
        for (i = 0; i < 3; i++) MoveMng_Init(&pWork->stMoveMng[i]);
        muat_set_header();
        MOBJManager_ResetPos(MUAT_MNG, 4, &pWork->apMobj[0]);
        /* inlined ChangeCursor(cursor_index) */
        idx = pWork->cursor_index;
        pos.x = 0;
        pos.y = idx * 28;
        UnitSprite_SetPosition(pWork->apMobj[9], &pos);
        pWork->cursor_index = idx;
        muat_set_header();
        MOBJManager_Move(MUAT_MNG, 4, &pWork->apMobj[0], -0x140, 0, 0);
        pWork->count = 0;
        pWork->phase = 2;
    }

    /* slide-in */
    if (pWork->phase == 2) {
        for (j = 0; j < 3; j++) {
            if (pWork->count < 2 * j) continue;
            if (pWork->count == 2 * j) {
                DF_MOBJ_SetDrawEnable(pWork->apMobj[j], 1);
                DF_MOBJ_SetDrawEnable(pWork->apMobj[3 + j], 1);
                DF_MOBJ_SetDrawEnable(pWork->apMobj[6 + j], 1);
                UnitSprite_SetFadeIn(pWork->apMobj[j]);
                UnitSprite_SetFadeIn(pWork->apMobj[3 + j]);
                DF_MOBJ_SetFadeIn(pWork->apMobj[6 + j]);
            }
            v = MoveMng_GetNextMoveValue(&pWork->stMoveMng[j], 0x140, 8, 1);
            if (v == MUAT_FINISHED) {
                if (j == 2) {
                    muat_set_header();
                    MOBJManager_ResetPos(MUAT_MNG, 4, &pWork->apMobj[0]);
                    idx = pWork->cursor_index;
                    pos.x = 0;
                    pos.y = idx * 28;
                    UnitSprite_SetPosition(pWork->apMobj[9], &pos);
                    pWork->cursor_index = idx;
                    DF_MOBJ_SetDrawEnable(pWork->apMobj[9], 1);
                    UnitSprite_SetFadeIn(pWork->apMobj[9]);
                    pWork->phase = 0;
                }
            } else {
                UnitSprite_GetPosition(pWork->apMobj[j], &p);
                p.x += v;
                UnitSprite_SetPosition(pWork->apMobj[j], &p);
                UnitSprite_GetPosition(pWork->apMobj[3 + j], &p);
                p.x += v;
                UnitSprite_SetPosition(pWork->apMobj[3 + j], &p);
                pWork->apMobj[6 + j]->dt.string.x += (short)v;
            }
        }
        pWork->count++;
    }

    /* start slide-out */
    if (pWork->phase == 13 || pWork->phase == 9) {
        for (i = 0; i < 3; i++) MoveMng_Init(&pWork->stMoveMng[i]);
        muat_set_header();
        MOBJManager_ResetPos(MUAT_MNG, 4, &pWork->apMobj[0]);
        idx = pWork->cursor_index;
        pos.x = 0;
        pos.y = idx * 28;
        UnitSprite_SetPosition(pWork->apMobj[9], &pos);
        pWork->cursor_index = idx;
        UnitSprite_SetFadeOut(pWork->apMobj[9]);
        pWork->count = 0;
        pWork->phase = 14;
    }

    /* slide-out */
    if (pWork->phase == 14) {
        for (j = 0; j < 3; j++) {
            if (pWork->count < 2 * j) continue;
            if (pWork->count == 2 * j) {
                UnitSprite_SetFadeOut(pWork->apMobj[j]);
                UnitSprite_SetFadeOut(pWork->apMobj[3 + j]);
                DF_MOBJ_SetFadeOut(pWork->apMobj[6 + j]);
            }
            v = MoveMng_GetNextMoveValue(&pWork->stMoveMng[j], -0x140, 8, 1);
            if (v == MUAT_FINISHED) {
                if (j == 2) pWork->phase = 0;
            } else {
                UnitSprite_GetPosition(pWork->apMobj[j], &p);
                p.x += v;
                UnitSprite_SetPosition(pWork->apMobj[j], &p);
                UnitSprite_GetPosition(pWork->apMobj[3 + j], &p);
                p.x += v;
                UnitSprite_SetPosition(pWork->apMobj[3 + j], &p);
                pWork->apMobj[6 + j]->dt.string.x += (short)v;
            }
        }
        pWork->count++;
    }
}

void MUnitArenaTop_ChangeCursor(struct stMobj *pMobj, int indexNew)
{
    PS2_FRAME(0x40);
    struct struct_214acd28 pos;

    pos.x = 0;
    pos.y = indexNew * 28;
    UnitSprite_SetPosition(MUAT(pMobj)->apMobj[9], &pos);
    MUAT(pMobj)->cursor_index = (char)indexNew;
}

void MUnitArenaTop_FadeOutStart(struct stMobj *pMobj, char phase)
{
    MUAT(pMobj)->phase = phase;
}

void MUnitArenaTop_FadeInStart(struct stMobj *pMobj, char phase)
{
    MUAT(pMobj)->phase = phase;
}

void MUnitArenaTop_Cleanup(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    muat_set_header();
    MOBJManager_Delete(&MUAT(pMobj)->apMobj[0], 4, MUAT_MNG);
}

struct stMobj *MUnitArenaTop_NewSystem(struct stMobj *pParentMobj)
{
    PS2_FRAME(0x40);
    PS2_STACK(struct struct_76b810ec, col, 0x3c);
    unsigned int colBits = 0x80404040;   /* main sdata 0x27bfb0, copied as raw bits (lwc1 / swc1): grey */
    struct stMobj *m = DF_MOBJ_GetNewObject(pParentMobj);
    struct struct_d0523e8e *pWork;

    if (m == 0) return 0;
    memset(&m->dt, 0, 0x60);
    m->type = 0;
    m->stat = 1;
    m->count = 0;
    m->play_func = (void (*)(void))GFN(__MUnitArenaTop_Play);
    m->draw_func = (void (*)(void))GFN(__MUnitArenaTop_Draw);
    m->pkt_func = (void (*)(void))GFN(__MUnitArenaTop_Pkt);
    pWork = MUAT(m);
    memcpy(&col, &colBits, 4);
    muat_set_header();
    MOBJManager_Create(m, &pWork->apMobj[0], 4, MUAT_MNG);
    /* no official card yet: grey out the OFFICIAL tab's mark and label */
    if (g_gamedata[0].officialBattle[0].idRanker == 0 && g_gamedata[0].officialBattle[1].idRanker == 0) {
        UnitSprite_SetMultiply(pWork->apMobj[4], &col);
        DF_MOBJ_SetStringColorAll(pWork->apMobj[7], &col);
    }
    return m;
}
