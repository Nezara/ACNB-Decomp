/* src/menu/munit_top_base.cpp - the AC-name frame ("Top Base") of the top menu screens.
 * One stMobj whose MOBJManager_Create (layout 0x454, 2-entry table) builds two frame sprites "waku0" / "waku1" and
 * the AC-name string "string_ac". Created by mtop_rt (mode 1) and msys (mode 2: group shifted by -0x10 px).
 * Play is the slide-phase machine: FadeInStart / FadeOutStart set the phase, Play slides the group by +-0x130 px
 * (MoveMng mode 1) or +-0x10 px (mode 2) over 8 frames with MOBJManager_Move. ChangeFrame swaps the shown frame.
 * The table's pLayoutHeader is rewritten before every MOBJManager_* call, so the hybrid keeps it at its PS2 address
 * (nodwarf munit_top_base_mng). */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
/* main sdata 0x27c240 "waku%d", OLMENU 0x1ee32b0 "string_ac", table OLMENU 0x1ee32c0 */
static char munit_top_base_name_waku[] = "waku%d";
static char munit_top_base_name_string_ac[] = "string_ac";
struct stMobjManager munit_top_base_mng[2] = {
    {.pLayoutName = munit_top_base_name_waku, .pLayoutHeader = 0, .numContinue = 2, .typeMObj = 1},
    {.pLayoutName = munit_top_base_name_string_ac, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 2,
     .flag = 0, .dummy_c0 = {0}, .exString = {.category = 0, .strid = 0xdbcb8}},
};
#endif

#define MUTB_MNG munit_top_base_mng
#define MUTB_END (-99999)   /* MoveMng_GetNextMoveValue: move finished */

/* the mobj's dt (no DWARF struct; NewSystem clears 0x28 bytes) */
struct munit_top_base_work {
    int unused0;                    /* 0x0 never touched */
    struct stMobj *apMobj[3];       /* 0x4 [0] waku0, [1] waku1, [2] string_ac */
    struct struct_d46b729f mng;     /* 0x10 */
    int offset;                     /* 0x20 x shift restored after ResetPos */
    signed char phase;              /* 0x24 */
    signed char frame;              /* 0x25 1 = waku0 shown, 2 = waku1 shown */
    signed char mode;               /* 0x26 1 = mtop_rt, 2 = msys */
    signed char pad;                /* 0x27 */
};
#define TB(o) ((struct munit_top_base_work *)&(o)->dt)

static inline void mutb_set_header(void)
{
    MUTB_MNG[0].pLayoutHeader = (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x454);
}

static void __MUnitTopBase_Pkt(void)
{
}

static void __MUnitTopBase_Draw(void)
{
}

/* setup block: restart the move, reset the group to its layout position and re-apply the offset */
static inline void mutb_setup(struct munit_top_base_work *d)
{
    MoveMng_Init(&d->mng);
    mutb_set_header();
    MOBJManager_ResetPos(MUTB_MNG, 2, &d->apMobj[0]);
    int off = d->offset;
    mutb_set_header();
    MOBJManager_Move(MUTB_MNG, 2, &d->apMobj[0], off, 0, 0);
}

/* slide block: one step of the move; at the end the phase goes back to 0 */
static inline void mutb_slide(struct munit_top_base_work *d, int dx, char m, int set_offset, int new_offset)
{
    int v = MoveMng_GetNextMoveValue(&d->mng, dx, 8, m);
    if (v == MUTB_END) {
        if (set_offset) d->offset = new_offset;
        d->phase = 0;
    } else {
        mutb_set_header();
        MOBJManager_Move(MUTB_MNG, 2, &d->apMobj[0], v, 0, 0);
    }
}

/* a chain of independent `if (phase == X)` blocks, each re-reading phase: a setup block falls through into its
 * slide block in the same call */
static void __MUnitTopBase_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct munit_top_base_work *d = TB(pMobj);

    if (d->phase == 0) return;
    if (d->phase == 1) {
        mutb_setup(d);
        mutb_set_header();
        MOBJManager_Move(MUTB_MNG, 2, &d->apMobj[0], 0x130, 0, 0);
        d->phase = 2;
    }
    if (d->phase == 2) mutb_slide(d, -0x130, 1, 0, 0);
    if (d->phase == 5) {
        mutb_setup(d);
        mutb_set_header();
        MOBJManager_Move(MUTB_MNG, 2, &d->apMobj[0], -0x130, 0, 0);
        d->phase = 6;
    }
    if (d->phase == 6) mutb_slide(d, 0x130, 1, 0, 0);
    if (d->phase == 9) {
        mutb_setup(d);
        d->phase = 0xa;
    }
    if (d->phase == 0xa) mutb_slide(d, -0x130, 1, 0, 0);
    if (d->phase == 0xd) {
        mutb_setup(d);
        d->phase = 0xe;
    }
    if (d->phase == 0xe) mutb_slide(d, 0x130, 1, 0, 0);
    if (d->phase == 0x17) {
        mutb_setup(d);
        d->phase = 0x18;
    }
    if (d->phase == 0x18) mutb_slide(d, -0x10, 2, 1, -0x10);
    if (d->phase == 0x19) {
        mutb_setup(d);
        d->phase = 0x1a;
    }
    if (d->phase == 0x1a) mutb_slide(d, 0x10, 2, 1, 0);
    if (d->phase == 0x11) d->phase = 0;
    if (d->phase == 0x12) d->phase = 0;
}

/* 8-byte tail jump: the AC name goes into the string mobj */
void MUnitTopBase_SetData(struct stMobj *pMobj, char *pNameAC)
{
    DF_MOBJ_SetStringCopy(TB(pMobj)->apMobj[2], pNameAC);
}

/* (sic) asymmetric: the string is drawn + faded in only on 1 -> 2, faded out on 2 -> 1 and never shown again */
void MUnitTopBase_ChangeFrame(struct stMobj *pMobj, char modeShow)
{
    PS2_FRAME(0x20);
    struct munit_top_base_work *d = TB(pMobj);

    if (d->frame == 1 && (signed char)modeShow == 2) {
        DF_MOBJ_SetDrawEnable(d->apMobj[1], 1);
        UnitSprite_SetFadeIn(d->apMobj[1]);
        DF_MOBJ_SetDrawEnable(d->apMobj[2], 1);
        DF_MOBJ_SetFadeIn(d->apMobj[2]);
        UnitSprite_SetFadeOut(d->apMobj[0]);
        d->frame = 2;
    } else if (d->frame == 2 && (signed char)modeShow == 1) {
        UnitSprite_SetFadeOut(d->apMobj[1]);
        DF_MOBJ_SetFadeOut(d->apMobj[2]);
        DF_MOBJ_SetDrawEnable(d->apMobj[0], 1);
        UnitSprite_SetFadeIn(d->apMobj[0]);
        d->frame = 1;
    }
}

void MUnitTopBase_FadeOutStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x20);
    struct munit_top_base_work *d = TB(pMobj);

    d->phase = phase;
    if (d->phase == 0x17 || d->phase == 0x19) return;
    if (d->frame == 1) {
        UnitSprite_SetFadeOut(d->apMobj[0]);   /* (sic) the string is not faded out here */
    } else if (d->frame == 2) {
        UnitSprite_SetFadeOut(d->apMobj[1]);
        DF_MOBJ_SetFadeOut(d->apMobj[2]);
    }
}

void MUnitTopBase_FadeInStart(struct stMobj *pMobj, char phase, char modeShow)
{
    PS2_FRAME(0x20);
    struct munit_top_base_work *d = TB(pMobj);

    d->phase = phase;
    if (d->phase == 0x17 || d->phase == 0x19) return;
    if ((signed char)modeShow == 2) {
        DF_MOBJ_SetDrawEnable(d->apMobj[1], 1);
        UnitSprite_SetFadeIn(d->apMobj[1]);
        DF_MOBJ_SetDrawEnable(d->apMobj[2], 1);
        DF_MOBJ_SetFadeIn(d->apMobj[2]);
        d->frame = 2;
    } else if ((signed char)modeShow == 1) {
        DF_MOBJ_SetDrawEnable(d->apMobj[0], 1);
        UnitSprite_SetFadeIn(d->apMobj[0]);
        d->frame = 1;
    }
}

void MUnitTopBase_Cleanup(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    mutb_set_header();
    MOBJManager_Delete(&TB(pMobj)->apMobj[0], 2, MUTB_MNG);
}

struct stMobj *MUnitTopBase_NewSystem(struct stMobj *pParentMobj, char mode)
{
    PS2_FRAME(0x40);
    struct stMobj *p = DF_MOBJ_GetNewObject(pParentMobj);
    if (!p) return 0;
    struct munit_top_base_work *d = TB(p);

    memset(d, 0, 0x28);
    d->mode = mode;
    if (d->mode == 2) d->offset = -0x10;
    p->type = 0;
    p->stat = 1;
    p->count = 0;
    p->play_func = (decltype(p->play_func))GFN(__MUnitTopBase_Play);
    p->draw_func = (decltype(p->draw_func))GFN(__MUnitTopBase_Draw);
    p->pkt_func = (decltype(p->pkt_func))GFN(__MUnitTopBase_Pkt);
    mutb_set_header();
    MOBJManager_Create(p, &d->apMobj[0], 2, MUTB_MNG);
    return p;
}
