/* src/menu/munit_tab_info.cpp - the info plate below the tab strip of the arena / training result screens: one mobj
 * group from a 2-record table (layout 0x442): the window win_tab_into (apMobj[0]) and two arrow sprites (apMobj[1]
 * left, apMobj[2] right). Play slides the group in from the left (phase 1 puts it at x - 0x130, phase 2 moves it
 * back by +0x130 in 8 frames) or out (0xd -> 0xe: -0x130 in 8 frames); phases 0x11 / 0x12 re-lay it out at y + 0xc
 * and fade it in / out. The phase blocks are independent `if`s that fall through within one call (1 -> 2,
 * 0xd -> 0xe). SetArrowState shows / hides the arrows with fades (bit 0 left, bit 1 right). Created by
 * MUnitMng_Init, driven by the interpreted MArenaResult / MTrainingResult / RRecord / Trend screens.
 * The table's record 0 pLayoutHeader is rewritten before every MOBJManager_* call, so the hybrid keeps the table at
 * its PS2 address (nodwarf munit_tab_info_mng); the source port defines it below. */
#include <string.h>
#include "acnb/nodwarf.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU 0x1ee30b0 / main sdata 0x27c208: the table's layout names */
static char munit_tab_info_name_win[] = "win_tab_into";
static char munit_tab_info_name_arrow[] = "arrow%d";
/* OLMENU 0x1ee30c0 (2 x 0x40) */
struct stMobjManager munit_tab_info_mng[2] = {
    {.pLayoutName = munit_tab_info_name_win, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 3},
    {.pLayoutName = munit_tab_info_name_arrow, .pLayoutHeader = 0, .numContinue = 2, .typeMObj = 4},
};
#endif

/* the unit's work area = mobj->dt (no DWARF struct; NewSystem clears 0x24 bytes) */
struct MUnitTabInfoWork {
    int pad0;                     /* d+0 */
    struct stMobj *apMobj[3];     /* d+4: win_tab_into, arrow0 (left), arrow1 (right) */
    char phase;                   /* d+0x10 */
    char arrowFlags;              /* d+0x11: bit 0 left arrow shown, bit 1 right arrow shown */
    char pad_12[2];
    struct struct_d46b729f mng;   /* d+0x14: slide move manager */
};

#define MUTI_MNG munit_tab_info_mng
#define MUTI(o) ((struct MUnitTabInfoWork *)&(o)->dt)
#define MUTI_FINISHED (-99999)   /* MoveMng_GetNextMoveValue: move finished */

static inline void muti_set_header(void)
{
    MUTI_MNG[0].pLayoutHeader = (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x442);
}

static void __MUnitTabInfo_Pkt(void)
{
}

static void __MUnitTabInfo_Draw(void)
{
}

static void __MUnitTabInfo_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct MUnitTabInfoWork *d = MUTI(pMobj);
    int v;

    /* start slide-in */
    if (d->phase == 1) {
        muti_set_header();
        MOBJManager_ResetPos(MUTI_MNG, 2, &d->apMobj[0]);
        muti_set_header();
        MOBJManager_Move(MUTI_MNG, 2, &d->apMobj[0], -0x130, 0, 0);
        MoveMng_Init(&d->mng);
        d->phase = 2;
    }
    /* slide-in */
    if (d->phase == 2) {
        v = MoveMng_GetNextMoveValue(&d->mng, 0x130, 8, 1);
        if (v == MUTI_FINISHED) {
            d->phase = 0;
        } else {
            muti_set_header();
            MOBJManager_Move(MUTI_MNG, 2, &d->apMobj[0], v, 0, 0);
        }
    }
    /* start slide-out */
    if (d->phase == 0xd) {
        MoveMng_Init(&d->mng);
        d->phase = 0xe;
    }
    /* slide-out */
    if (d->phase == 0xe) {
        v = MoveMng_GetNextMoveValue(&d->mng, -0x130, 8, 1);
        if (v == MUTI_FINISHED) {
            d->phase = 0;
        } else {
            muti_set_header();
            MOBJManager_Move(MUTI_MNG, 2, &d->apMobj[0], v, 0, 0);
        }
    }
    /* lowered layout, fade in (arrows hidden) */
    if (d->phase == 0x11) {
        muti_set_header();
        MOBJManager_ResetPos(MUTI_MNG, 2, &d->apMobj[0]);
        muti_set_header();
        MOBJManager_Move(MUTI_MNG, 2, &d->apMobj[0], 0, 0xc, 0);
        muti_set_header();
        MOBJManager_FadeIn(&d->apMobj[0], 0, 2, MUTI_MNG);
        DF_MOBJ_SetDrawEnable(d->apMobj[1], 0);
        DF_MOBJ_SetDrawEnable(d->apMobj[2], 0);
        d->phase = 0;
    }
    /* lowered layout, fade out */
    if (d->phase == 0x12) {
        muti_set_header();
        MOBJManager_ResetPos(MUTI_MNG, 2, &d->apMobj[0]);
        muti_set_header();
        MOBJManager_Move(MUTI_MNG, 2, &d->apMobj[0], 0, 0xc, 0);
        muti_set_header();
        MOBJManager_FadeOut(&d->apMobj[0], 0, 2, MUTI_MNG);
        d->phase = 0;
    }
}

void MUnitTabInfo_SetArrowState(struct stMobj *pMobj, char stateArrow)
{
    PS2_FRAME(0x40);
    struct MUnitTabInfoWork *d = MUTI(pMobj);
    int state = stateArrow;
    int left = state & 1;

    if (left && !(d->arrowFlags & 1)) {
        DF_MOBJ_SetDrawEnable(d->apMobj[1], 1);
        UnitSprite_SetFadeIn(d->apMobj[1]);
        d->arrowFlags |= 1;
    }
    if (!left && (d->arrowFlags & 1)) {
        UnitSprite_SetFadeOut(d->apMobj[1]);
        d->arrowFlags &= ~1;
    }
    state &= 2;
    if (state && !(d->arrowFlags & 2)) {
        DF_MOBJ_SetDrawEnable(d->apMobj[2], 1);
        UnitSprite_SetFadeIn(d->apMobj[2]);
        d->arrowFlags |= 2;
    }
    if (!state && (d->arrowFlags & 2)) {
        UnitSprite_SetFadeOut(d->apMobj[2]);
        d->arrowFlags &= ~2;
    }
}

int MUnitTabInfo_IsMove(struct stMobj *pMobj)
{
    return MUTI(pMobj)->phase != 0;
}

void MUnitTabInfo_FadeOutStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x20);
    MUTI(pMobj)->phase = phase;
    muti_set_header();
    MOBJManager_FadeOut(&MUTI(pMobj)->apMobj[0], 0, 2, MUTI_MNG);
}

void MUnitTabInfo_FadeInStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x20);
    struct MUnitTabInfoWork *d = MUTI(pMobj);

    d->phase = phase;
    muti_set_header();
    MOBJManager_FadeIn(&d->apMobj[0], 0, 2, MUTI_MNG);
    DF_MOBJ_SetDrawEnable(d->apMobj[1], 0);
    DF_MOBJ_SetDrawEnable(d->apMobj[2], 0);
}

void MUnitTabInfo_Cleanup(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    muti_set_header();
    MOBJManager_Delete(&MUTI(pMobj)->apMobj[0], 2, MUTI_MNG);
}

struct stMobj *MUnitTabInfo_NewSystem(struct stMobj *pParentMobj)
{
    PS2_FRAME(0x20);
    struct stMobj *m = DF_MOBJ_GetNewObject(pParentMobj);

    if (m == 0) return 0;
    memset(&m->dt, 0, 0x24);
    m->type = 0;
    m->stat = 1;
    m->count = 0;
    m->play_func = (void (*)(void))GFN(__MUnitTabInfo_Play);
    m->draw_func = (void (*)(void))GFN(__MUnitTabInfo_Draw);
    m->pkt_func = (void (*)(void))GFN(__MUnitTabInfo_Pkt);
    muti_set_header();
    MOBJManager_Create(m, &MUTI(m)->apMobj[0], 2, MUTI_MNG);
    return m;
}
