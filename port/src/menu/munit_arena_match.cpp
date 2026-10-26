/* src/menu/munit_arena_match.cpp - the cursor unit of the ARENA MATCH list page (child of menu/marena_match): one
 * cursor plate (apMobj[0], y = 58 * list index, set by ChangeCursor) and two scroll arrows (apMobj[1] up,
 * apMobj[2] down). marena_match fades it in / out (FadeInStart(phase, fadeArrow) / FadeOutStart(phase)); Play has
 * no motion of its own: every phase it knows ends at 0 in the same call (independent `if`s that fall through).
 * The table's record 0 pLayoutHeader is rewritten before every MOBJManager_* call, so the hybrid keeps the table
 * at its PS2 address (nodwarf munit_arena_match_mng). */
#include <string.h>
#include "acnb/nodwarf.h"

#ifdef ACNB_SOURCEPORT
/* main sdata 0x27bf68 / 0x27bf70: the table's layout names */
static char munit_arena_match_name_cursor[] = "cursor";
static char munit_arena_match_name_arrow[] = "arrow%d";
/* OLMENU 0x1ee11e0 (2 x 0x40) */
struct stMobjManager munit_arena_match_mng[2] = {
    {.pLayoutName = munit_arena_match_name_cursor, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 4},
    {.pLayoutName = munit_arena_match_name_arrow, .pLayoutHeader = 0, .numContinue = 2, .typeMObj = 4},
};
#endif

#define MUAM_MNG munit_arena_match_mng
#define MUAM(o) ((struct struct_5683f8b3 *)&(o)->dt)

static inline void muam_set_header(void)
{
    MUAM_MNG[0].pLayoutHeader = (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x450);
}

static void __MUnitArenaMatch_Pkt(void)
{
}

static void __MUnitArenaMatch_Draw(void)
{
}

static void __MUnitArenaMatch_Play(struct stMobj *pMobj)
{
    struct struct_5683f8b3 *pWork = MUAM(pMobj);

    if (pWork->phase == 0) return;
    if (pWork->phase == 1) pWork->phase = 2;
    if (pWork->phase == 2) pWork->phase = 0;
    if (pWork->phase == 5) pWork->phase = 6;
    if (pWork->phase == 6) pWork->phase = 0;
    if (pWork->phase == 9) pWork->phase = 10;
    if (pWork->phase == 10) pWork->phase = 0;
    if (pWork->phase == 13) pWork->phase = 14;
    if (pWork->phase == 14) pWork->phase = 0;
}

void MUnitArenaMatch_ChangeCursor(struct stMobj *pMobj, int indexNew)
{
    PS2_FRAME(0x20);
    struct struct_214acd28 pos;

    pos.x = 0;
    pos.y = indexNew * 58;
    UnitSprite_SetPosition(MUAM(pMobj)->apMobj[0], &pos);
}

void MUnitArenaMatch_FadeOutStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x20);
    MUAM(pMobj)->phase = phase;
    muam_set_header();
    MOBJManager_FadeOut(&MUAM(pMobj)->apMobj[0], 0, 2, MUAM_MNG);
}

void MUnitArenaMatch_FadeInStart(struct stMobj *pMobj, char phase, char fadeArrow)
{
    PS2_FRAME(0x40);
    struct struct_5683f8b3 *pWork = MUAM(pMobj);

    pWork->phase = phase;
    muam_set_header();
    MOBJManager_FadeIn(&pWork->apMobj[0], 0, 2, MUAM_MNG);
    if (fadeArrow == 0) {
        DF_MOBJ_SetDrawEnable(pWork->apMobj[1], 0);
        DF_MOBJ_SetDrawEnable(pWork->apMobj[2], 0);
    }
}

void MUnitArenaMatch_Cleanup(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    muam_set_header();
    MOBJManager_Delete(&MUAM(pMobj)->apMobj[0], 2, MUAM_MNG);
}

struct stMobj *MUnitArenaMatch_NewSystem(struct stMobj *pParentMobj)
{
    PS2_FRAME(0x20);
    struct stMobj *m = DF_MOBJ_GetNewObject(pParentMobj);

    if (m == 0) return 0;
    memset(&m->dt, 0, 0x14);
    m->type = 0;
    m->stat = 1;
    m->count = 0;
    m->play_func = (void (*)(void))GFN(__MUnitArenaMatch_Play);
    m->draw_func = (void (*)(void))GFN(__MUnitArenaMatch_Draw);
    m->pkt_func = (void (*)(void))GFN(__MUnitArenaMatch_Pkt);
    muam_set_header();
    MOBJManager_Create(m, &MUAM(m)->apMobj[0], 2, MUAM_MNG);
    return m;
}
