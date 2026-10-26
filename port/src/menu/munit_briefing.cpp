/* src/menu/munit_briefing.cpp - the briefing plate of the training program screen: one window mobj
 * ("win_briefing") from a 1-record table (layout 0x440). Play slides it in from the right (phase 1 puts it at
 * x + 0x130, phase 2 moves it back by -0x130 in 8 frames) or out (0xd -> 0xe: +0x130 in 8 frames); phases 5 / 6 /
 * 0x11 / 0x12 are leftover stubs (6 steps the move manager without moving the window, 0x11 / 0x12 just clear the
 * phase). FadeInStart / FadeOutStart store the phase and fade the window. Created by MUnitMng_Init for every top
 * menu, driven by the interpreted MTrainingProgram / MTrainingResult screens.
 * Unlike the sibling unit files, Play rewrites the table's pLayoutHeader once at its entry (every call), not before
 * each MOBJManager_* call. The hybrid keeps the table at its PS2 address (nodwarf munit_briefing_mng); the source
 * port defines it below. */
#include <string.h>
#include "acnb/nodwarf.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU 0x1ee15e0: the table's layout name */
static char munit_briefing_name_win[] = "win_briefing";
/* OLMENU 0x1ee15f0 (1 x 0x40) */
struct stMobjManager munit_briefing_mng[1] = {
    {.pLayoutName = munit_briefing_name_win, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 3},
};
#endif

/* the unit's work area = mobj->dt (no DWARF struct; NewSystem clears 0x1c bytes) */
struct MUnitBriefingWork {
    int pad0;                     /* d+0 */
    struct stMobj *apMobj[1];     /* d+4: win_briefing */
    signed char phase;            /* d+8 */
    char pad_9[3];
    struct struct_d46b729f mng;   /* d+0xc: slide move manager */
};

#define MUB_MNG munit_briefing_mng
#define MUB(o) ((struct MUnitBriefingWork *)&(o)->dt)
#define MUB_FINISHED (-99999)   /* MoveMng_GetNextMoveValue: move finished */

static inline void mub_set_header(void)
{
    MUB_MNG[0].pLayoutHeader = (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x440);
}

static void __MUnitBriefing_Pkt(void)
{
}

static void __MUnitBriefing_Draw(void)
{
}

static void __MUnitBriefing_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct MUnitBriefingWork *d = MUB(pMobj);
    int v;

    mub_set_header();
    /* start slide-in */
    if (d->phase == 1) {
        MOBJManager_ResetPos(MUB_MNG, 1, &d->apMobj[0]);
        MOBJManager_Move(MUB_MNG, 1, &d->apMobj[0], 0x130, 0, 0);
        MoveMng_Init(&d->mng);
        d->phase = 2;
    }
    /* slide-in */
    if (d->phase == 2) {
        v = MoveMng_GetNextMoveValue(&d->mng, -0x130, 8, 1);
        if (v == MUB_FINISHED)
            d->phase = 0;
        else
            MOBJManager_Move(MUB_MNG, 1, &d->apMobj[0], v, 0, 0);
    }
    /* start slide-out */
    if (d->phase == 0xd) {
        MOBJManager_ResetPos(MUB_MNG, 1, &d->apMobj[0]);
        MoveMng_Init(&d->mng);
        d->phase = 0xe;
    }
    /* slide-out */
    if (d->phase == 0xe) {
        v = MoveMng_GetNextMoveValue(&d->mng, 0x130, 8, 1);
        if (v == MUB_FINISHED)
            d->phase = 0;
        else
            MOBJManager_Move(MUB_MNG, 1, &d->apMobj[0], v, 0, 0);
    }
    /* leftover stubs (no replay reaches them) */
    if (d->phase == 5)
        d->phase = 6;
    if (d->phase == 6) {
        /* steps the (never initialised) move manager but never moves the window */
        if (MoveMng_GetNextMoveValue(&d->mng, -0x130, 8, 1) == MUB_FINISHED)
            d->phase = 0;
    } else {
        if (d->phase == 0x11)
            d->phase = 0;
        if (d->phase == 0x12)
            d->phase = 0;
    }
}

int MUnitBriefing_IsMove(struct stMobj *pMobj)
{
    return MUB(pMobj)->phase != 0;
}

void MUnitBriefing_FadeOutStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x20);
    MUB(pMobj)->phase = phase;
    mub_set_header();
    MOBJManager_FadeOut(&MUB(pMobj)->apMobj[0], 0, 1, MUB_MNG);
}

void MUnitBriefing_FadeInStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x20);
    MUB(pMobj)->phase = phase;
    mub_set_header();
    MOBJManager_FadeIn(&MUB(pMobj)->apMobj[0], 0, 1, MUB_MNG);
}

void MUnitBriefing_Cleanup(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    mub_set_header();
    MOBJManager_Delete(&MUB(pMobj)->apMobj[0], 1, MUB_MNG);
}

struct stMobj *MUnitBriefing_NewSystem(struct stMobj *pParentMobj)
{
    PS2_FRAME(0x20);
    struct stMobj *m = DF_MOBJ_GetNewObject(pParentMobj);
    if (!m)
        return 0;
    memset(&m->dt, 0, 0x1c);
    m->type = 0;
    m->stat = 1;
    m->count = 0;
    m->play_func = (void (*)(void))GFN(__MUnitBriefing_Play);
    m->draw_func = (void (*)(void))GFN(__MUnitBriefing_Draw);
    m->pkt_func = (void (*)(void))GFN(__MUnitBriefing_Pkt);
    mub_set_header();
    MOBJManager_Create(m, &MUB(m)->apMobj[0], 1, MUB_MNG);
    return m;
}
