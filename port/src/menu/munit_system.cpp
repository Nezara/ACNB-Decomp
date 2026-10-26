/* src/menu/munit_system.cpp - the item list of the SYSTEM menu.
 * One stMobj whose MOBJManager_Create (layout 0x45c, 4-entry table) builds up to 7 board rows "ita_system%d",
 * their "mark%d" icons, the help strings "string%d" and a "cursor" sprite. Created by msys __PlaySystemFadeIn
 * (mode 1 = normal: 6 rows, mode 2 = Versus: 3 rows); CreateItem writes the help text ids into the string mobjs.
 * FadeInStart / FadeOutStart only store the phase; Play runs the slide: phase 1 / 5 put the list 0x140 px to the
 * left and release one row per frame, each row slides right by 0x140 px over 8 frames (phase 2); phase 9 / 0xd /
 * 0xe do the reverse (rows fade out one per frame and slide left). ChangeCursor puts the cursor at y = index * 28.
 * The table's pLayoutHeader is rewritten before every MOBJManager_* call, so the hybrid keeps it at its PS2 address
 * (nodwarf munit_system_mng). Original quirks kept (marked (sic)). */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU 0x1ee2e80 "ita_system%d", main sdata 0x27c1e0 "mark%d", OLMENU 0x1ee2e90 "string%d", main sdata 0x27c1e8
 * "cursor", table OLMENU 0x1ee2ea0 */
static char munit_system_name_ita[] = "ita_system%d";
static char munit_system_name_mark[] = "mark%d";
static char munit_system_name_string[] = "string%d";
static char munit_system_name_cursor[] = "cursor";
struct stMobjManager munit_system_mng[4] = {
    {.pLayoutName = munit_system_name_ita, .pLayoutHeader = 0, .numContinue = 7, .typeMObj = 1},
    {.pLayoutName = munit_system_name_mark, .pLayoutHeader = 0, .numContinue = 7, .typeMObj = 1},
    {.pLayoutName = munit_system_name_string, .pLayoutHeader = 0, .numContinue = 7, .typeMObj = 2,
     .flag = 0, .dummy_c0 = {0}, .exString = {.category = 0, .strid = 0xdbcb8}},
    {.pLayoutName = munit_system_name_cursor, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 4},
};
/* help text ids per row: msys's DWARF globals in the hybrid (OLMENU 0x1ee6310 / 0x1ee6328) */
static int munit_system_strid_normal[6] = {0x192e5, 0x192e6, 0x192e7, 0x192e8, 0x192e9, 0x192ea};
static int munit_system_strid_vs[3] = {0x192e5, 0x192e6, 0x192e9};
#define MUSYS_STRID_NORMAL munit_system_strid_normal
#define MUSYS_STRID_VS munit_system_strid_vs
#else
#define MUSYS_STRID_NORMAL g_aMUnitSystemStridForNormal
#define MUSYS_STRID_VS g_aMUnitSystemStridForVs
#endif

#define MUSYS_MNG munit_system_mng
#define MUSYS_END (-99999)   /* MoveMng_GetNextMoveValue: move finished */

/* the mobj's dt (0xd8, NewSystem clears it): apMobj [0..6] ita_system, [7..13] mark, [14..20] string, [21] cursor;
 * stMoveMng[i] per row; keyguide 0x3e / 0x3d (mode 1 / 2, not read here); phase; count = rows released so far;
 * mode; num = rows; cursor_index */
typedef struct struct_004ee324 MUSysWork;
#define SD(o) ((MUSysWork *)&(o)->dt)

static inline void musys_set_header(void)
{
    MUSYS_MNG[0].pLayoutHeader = (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x45c);
}

static void __MUnitSystem_CreateItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    MUSysWork *d = SD(pMobj);
    unsigned int i;

    musys_set_header();
    MOBJManager_Create(pMobj, &d->apMobj[0], 4, MUSYS_MNG);
    if (d->mode == 2) {
        for (i = 0; i < 3; i++) d->apMobj[14 + i]->dt.string.id = MUSYS_STRID_VS[i];
    } else if (d->mode == 1) {
        for (i = 0; i < 6; i++) d->apMobj[14 + i]->dt.string.id = MUSYS_STRID_NORMAL[i];
    }
}

static void __MUnitSystem_Pkt(void)
{
}

static void __MUnitSystem_Draw(void)
{
}

/* the cursor back at its row (x = 0, y = cursor * 28) after a ResetPos */
static inline void musys_place_cursor(MUSysWork *d)
{
    signed char cur = d->cursor_index;
    struct struct_214acd28 pos;
    pos.x = 0;
    pos.y = cur * 28;
    UnitSprite_SetPosition(d->apMobj[21], &pos);
    d->cursor_index = cur;
}

/* setup block: restart every row's move, reset the group, cursor to its row */
static inline void musys_setup(MUSysWork *d)
{
    for (int i = 0; i < d->num; i++) MoveMng_Init(&d->stMoveMng[i]);
    musys_set_header();
    MOBJManager_ResetPos(MUSYS_MNG, 4, &d->apMobj[0]);
    musys_place_cursor(d);
}

/* one row's step: x += v on the board, the mark and the string */
static inline void musys_step_row(MUSysWork *d, int i, int v)
{
    struct struct_214acd28 t;
    UnitSprite_GetPosition(d->apMobj[i], &t);
    t.x += v;
    UnitSprite_SetPosition(d->apMobj[i], &t);
    UnitSprite_GetPosition(d->apMobj[7 + i], &t);
    t.x += v;
    UnitSprite_SetPosition(d->apMobj[7 + i], &t);
    d->apMobj[14 + i]->dt.string.x += (short)v;
}

/* a chain of independent `if (phase == X)` blocks, each re-reading phase: a setup block falls through into its
 * slide block in the same call */
static void __MUnitSystem_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0xd0);
    MUSysWork *d = SD(pMobj);
    int i;

    if (d->phase == 0) return;

    /* A: slide-in setup */
    if (d->phase == 1 || d->phase == 5) {
        musys_setup(d);
        musys_set_header();
        MOBJManager_Move(MUSYS_MNG, 4, &d->apMobj[0], -0x140, 0, 0);
        DF_MOBJ_SetDrawEnable(d->apMobj[21], 0);
        d->count = 0;
        d->phase = 2;
    }

    /* B: slide in, one row released per frame */
    if (d->phase == 2) {
        for (i = 0; i < d->num; i++) {
            signed char c = d->count;
            if (c < i) continue;
            if (c == i) {
                DF_MOBJ_SetDrawEnable(d->apMobj[i], 1);
                DF_MOBJ_SetDrawEnable(d->apMobj[7 + i], 1);
                DF_MOBJ_SetDrawEnable(d->apMobj[14 + i], 1);
                UnitSprite_SetFadeIn(d->apMobj[i]);
                UnitSprite_SetFadeIn(d->apMobj[7 + i]);
                DF_MOBJ_SetFadeIn(d->apMobj[14 + i]);
            }
            int v = MoveMng_GetNextMoveValue(&d->stMoveMng[i], 0x140, 8, 1);
            if (v == MUSYS_END) {
                if (i == d->num - 1) {
                    musys_set_header();
                    MOBJManager_ResetPos(MUSYS_MNG, 4, &d->apMobj[0]);
                    musys_place_cursor(d);
                    DF_MOBJ_SetDrawEnable(d->apMobj[21], 1);
                    UnitSprite_SetFadeIn(d->apMobj[21]);
                    d->phase = 0;
                }
            } else {
                musys_step_row(d, i, v);
            }
        }
        d->count++;
    }

    /* C: slide-out setup */
    if (d->phase == 0xd || d->phase == 9) {
        musys_setup(d);
        UnitSprite_SetFadeOut(d->apMobj[21]);
        d->count = 0;
        d->phase = 0xe;
    }

    /* D: slide out, one row per frame */
    if (d->phase == 0xe) {
        for (i = 0; i < d->num; i++) {
            signed char c = d->count;
            if (c < i) continue;
            if (c == i) {
                UnitSprite_SetFadeOut(d->apMobj[i]);
                UnitSprite_SetFadeOut(d->apMobj[7 + i]);
                DF_MOBJ_SetFadeOut(d->apMobj[14 + i]);
                if (i == d->num - 1) d->phase = 0;   /* (sic) cleared when the last row STARTS fading */
            }
            int v = MoveMng_GetNextMoveValue(&d->stMoveMng[i], -0x140, 8, 1);
            if (v == MUSYS_END) {
                if (i == d->num - 1) d->phase = 0;   /* (sic) dead: the phase is already 0 */
            } else {
                musys_step_row(d, i, v);
            }
        }
        d->count++;
    }
}

void MUnitSystem_ChangeCursor(struct stMobj *pMobj, int indexNew)
{
    PS2_FRAME(0x40);
    struct struct_214acd28 pos;
    pos.x = 0;
    pos.y = indexNew * 28;
    UnitSprite_SetPosition(SD(pMobj)->apMobj[21], &pos);
    SD(pMobj)->cursor_index = (char)indexNew;
}

int MUnitSystem_IsMove(struct stMobj *pMobj)
{
    return SD(pMobj)->phase != 0;
}

void MUnitSystem_FadeOutStart(struct stMobj *pMobj, char phase)
{
    SD(pMobj)->phase = phase;
}

void MUnitSystem_FadeInStart(struct stMobj *pMobj, char phase)
{
    SD(pMobj)->phase = phase;
}

void MUnitSystem_Cleanup(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    musys_set_header();
    MOBJManager_Delete(&SD(pMobj)->apMobj[0], 4, MUSYS_MNG);
}

struct stMobj *MUnitSystem_NewSystem(struct stMobj *pParentMobj, char mode)
{
    PS2_FRAME(0x40);
    struct stMobj *p = DF_MOBJ_GetNewObject(pParentMobj);
    if (!p) return 0;
    MUSysWork *d = SD(p);

    memset(d, 0, 0xd8);
    d->mode = mode;
    if (d->mode == 2) {
        d->keyguide = 0x3d;
        d->num = 3;
    } else if (d->mode == 1) {
        d->keyguide = 0x3e;
        d->num = 6;
    }
    p->type = 0;
    p->stat = 1;
    p->count = 0;
    p->play_func = (decltype(p->play_func))GFN(__MUnitSystem_Play);
    p->draw_func = (decltype(p->draw_func))GFN(__MUnitSystem_Draw);
    p->pkt_func = (decltype(p->pkt_func))GFN(__MUnitSystem_Pkt);
    __MUnitSystem_CreateItem(p);
    return p;
}
