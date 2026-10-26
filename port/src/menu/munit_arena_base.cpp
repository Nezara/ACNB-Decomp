/* src/menu/munit_arena_base.cpp - the header board of the ARENA top screen (child of menu/marena_top, in lock step
 * with munit_arena_top): a plate (waku_arena_base, apMobj[0]) and a title string (string_arena_base, apMobj[1]) that
 * names the selected tab (string id 0xdbd9e + tab index, set by ChangeCursor). Play slides the board in from
 * x = +0x130 / -0x130 or out to -0x130 / +0x130 depending on the phase handed to FadeInStart / FadeOutStart; its
 * phase blocks are independent `if`s that fall through within one call (1 -> 2, 5 -> 6, 9 -> 10, 13 -> 14).
 * The table's record 0 pLayoutHeader is rewritten before every MOBJManager_* call (Play: once per non-zero-phase
 * call, before any phase test), so the hybrid keeps the table at its PS2 address (nodwarf munit_arena_base_mng). */
#include <string.h>
#include "acnb/nodwarf.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU 0x1ee10a0 / 0x1ee10b0: the table's layout names */
static char munit_arena_base_name_waku[] = "waku_arena_base";
static char munit_arena_base_name_string[] = "string_arena_base";
/* OLMENU 0x1ee10d0 (2 x 0x40) */
struct stMobjManager munit_arena_base_mng[2] = {
    {.pLayoutName = munit_arena_base_name_waku, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 1},
    {.pLayoutName = munit_arena_base_name_string, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 2,
     .flag = 0, .dummy_c0 = {0}, .exString = {.category = 0, .strid = 0xdbd9e}},
};
#endif

#define MUAB_MNG munit_arena_base_mng
#define MUAB(o) ((struct struct_ed2f37be *)&(o)->dt)
#define MUAB_FINISHED (-99999)   /* MoveMng_GetNextMoveValue: move finished */

static inline void muab_set_header(void)
{
    MUAB_MNG[0].pLayoutHeader = (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x44a);
}

static void __MUnitArenaBase_Pkt(void)
{
}

static void __MUnitArenaBase_Draw(void)
{
}

/* one slide step: finished -> back to the layout position, phase 0; else move by v */
static inline void muab_slide(struct struct_ed2f37be *pWork, int dx)
{
    int v = MoveMng_GetNextMoveValue(&pWork->stMoveMng, dx, 8, 1);

    if (v == MUAB_FINISHED) {
        MOBJManager_ResetPos(MUAB_MNG, 2, &pWork->apMobj[0]);
        pWork->phase = 0;
    } else {
        MOBJManager_Move(MUAB_MNG, 2, &pWork->apMobj[0], v, 0, 0);
    }
}

static void __MUnitArenaBase_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct struct_ed2f37be *pWork = MUAB(pMobj);

    if (pWork->phase == 0) return;
    muab_set_header();

    if (pWork->phase == 1) {
        MoveMng_Init(&pWork->stMoveMng);
        MOBJManager_ResetPos(MUAB_MNG, 2, &pWork->apMobj[0]);
        MOBJManager_Move(MUAB_MNG, 2, &pWork->apMobj[0], 0x130, 0, 0);
        pWork->phase = 2;
    }
    if (pWork->phase == 2) muab_slide(pWork, -0x130);
    if (pWork->phase == 5) {
        MoveMng_Init(&pWork->stMoveMng);
        MOBJManager_ResetPos(MUAB_MNG, 2, &pWork->apMobj[0]);
        MOBJManager_Move(MUAB_MNG, 2, &pWork->apMobj[0], -0x130, 0, 0);
        pWork->phase = 6;
    }
    if (pWork->phase == 6) muab_slide(pWork, 0x130);
    if (pWork->phase == 9) {
        MoveMng_Init(&pWork->stMoveMng);
        MOBJManager_ResetPos(MUAB_MNG, 2, &pWork->apMobj[0]);
        pWork->phase = 10;
    }
    if (pWork->phase == 10) muab_slide(pWork, -0x130);
    if (pWork->phase == 13) {
        MoveMng_Init(&pWork->stMoveMng);
        MOBJManager_ResetPos(MUAB_MNG, 2, &pWork->apMobj[0]);
        pWork->phase = 14;
    }
    if (pWork->phase == 14) muab_slide(pWork, 0x130);
}

void MUnitArenaBase_ChangeCursor(struct stMobj *pMobj, int indexNew)
{
    MUAB(pMobj)->apMobj[1]->dt.string.id = indexNew + 0xdbd9e;
    DF_MOBJ_SetFadeIn(MUAB(pMobj)->apMobj[1]);   /* tail jump in the original */
}

void MUnitArenaBase_FadeOutStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x20);
    MUAB(pMobj)->phase = phase;
    muab_set_header();
    MOBJManager_FadeOut(&MUAB(pMobj)->apMobj[0], 0, 2, MUAB_MNG);
}

void MUnitArenaBase_FadeInStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x20);
    MUAB(pMobj)->phase = phase;
    muab_set_header();
    MOBJManager_FadeIn(&MUAB(pMobj)->apMobj[0], 0, 2, MUAB_MNG);
}

void MUnitArenaBase_Cleanup(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    muab_set_header();
    MOBJManager_Delete(&MUAB(pMobj)->apMobj[0], 2, MUAB_MNG);
}

struct stMobj *MUnitArenaBase_NewSystem(struct stMobj *pParentMobj)
{
    PS2_FRAME(0x40);
    PS2_STACK(struct struct_76b810ec, col, 0x3c);
    unsigned int colBits = 0x8061a5de;   /* main sdata 0x27bf30, copied as raw bits (lwc1 / swc1): orange */
    struct stMobj *m = DF_MOBJ_GetNewObject(pParentMobj);

    if (m == 0) return 0;
    memset(&m->dt, 0, 0x20);
    m->type = 0;
    m->stat = 1;
    m->count = 0;
    m->play_func = (void (*)(void))GFN(__MUnitArenaBase_Play);
    m->draw_func = (void (*)(void))GFN(__MUnitArenaBase_Draw);
    m->pkt_func = (void (*)(void))GFN(__MUnitArenaBase_Pkt);
    memcpy(&col, &colBits, 4);
    muab_set_header();
    MOBJManager_Create(m, &MUAB(m)->apMobj[0], 2, MUAB_MNG);
    DF_MOBJ_SetStringColorAll(MUAB(m)->apMobj[1], &col);
    return m;
}
