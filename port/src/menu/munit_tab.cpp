/* src/menu/munit_tab.cpp - the tab strip of the arena / training result and select screens ("Tab"): one mobj group
 * from a 4-record table (layout 0x441): two tab sprites (apMobj[0..1]), two caption strings (apMobj[2..3]), a cursor
 * sprite (apMobj[4]) and two arrow sprites (apMobj[5..6]). ChangeMode re-lays the strip out (mode 2 = shifted by
 * (-0x14, +6)), ChangeCursor puts the cursor at x = index * 124, FadeInStart / FadeOutStart fade the whole group
 * (FadeInStart also writes the captions for the current mode). Play only clears the busy byte, so IsMove is 1 for
 * exactly one frame after a fade start. Created by MUnitMng_Init, driven by the interpreted MGenTab.
 * The table's record 0 pLayoutHeader is rewritten before every MOBJManager_* call, so the hybrid keeps the table at
 * its PS2 address (nodwarf munit_tab_mng); the source port defines it below. */
#include <string.h>
#include "acnb/nodwarf.h"

#ifdef ACNB_SOURCEPORT
/* main sdata 0x27c1f0 / OLMENU 0x1ee2fa0 / main sdata 0x27c1f8 / 0x27c200: the table's layout names */
static char munit_tab_name_tab[] = "tab%d";
static char munit_tab_name_string[] = "string_tab%d";
static char munit_tab_name_cursor[] = "cursor";
static char munit_tab_name_arrow[] = "arrow%d";
/* OLMENU 0x1ee2fb0 (4 x 0x40) */
struct stMobjManager munit_tab_mng[4] = {
    {.pLayoutName = munit_tab_name_tab, .pLayoutHeader = 0, .numContinue = 2, .typeMObj = 1},
    {.pLayoutName = munit_tab_name_string, .pLayoutHeader = 0, .numContinue = 2, .typeMObj = 2,
     .flag = 0, .dummy_c0 = {0}, .exString = {.category = 0, .strid = 0xdbccc}},
    {.pLayoutName = munit_tab_name_cursor, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 4},
    {.pLayoutName = munit_tab_name_arrow, .pLayoutHeader = 0, .numContinue = 2, .typeMObj = 4},
};
#endif

/* the unit's work area = mobj->dt (no DWARF struct; NewSystem clears 0x24 bytes) */
struct MUnitTabWork {
    int pad0;                    /* d+0 */
    struct stMobj *apMobj[7];    /* d+4: tab0-1, string_tab0-1, cursor, arrow0-1 */
    char busy;                   /* d+0x20: set by Fade*Start, cleared by Play */
    char mode;                   /* d+0x21: set by ChangeMode */
    char pad_22[2];
};

#define MUTAB_MNG munit_tab_mng
#define MUTAB(o) ((struct MUnitTabWork *)&(o)->dt)

static inline void mutab_set_header(void)
{
    MUTAB_MNG[0].pLayoutHeader = (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x441);
}

static void __MUnitTab_FadeInItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    struct MUnitTabWork *d = MUTAB(pMobj);

    mutab_set_header();
    MOBJManager_FadeIn(&d->apMobj[0], 0, 4, MUTAB_MNG);
    switch (d->mode) {
    case 0:
    case 1:
    case 3:
        d->apMobj[2]->dt.string.x = 0x52;
        d->apMobj[2]->dt.string.y = 0x4d;
        d->apMobj[3]->dt.string.x = 0xd4;
        d->apMobj[3]->dt.string.y = 0x4d;
        d->apMobj[2]->dt.string.id = 0xdbc36;
        d->apMobj[3]->dt.string.id = 0xdbc37;
        break;
    case 2:
        d->apMobj[2]->dt.string.x = 0x3e;
        d->apMobj[2]->dt.string.y = 0x53;
        d->apMobj[3]->dt.string.x = 0xa8;
        d->apMobj[3]->dt.string.y = 0x53;
        d->apMobj[2]->dt.string.id = 0xdbc5e;
        d->apMobj[3]->dt.string.id = 0xdbc5f;
        break;
    }
}

static void __MUnitTab_Pkt(void)
{
}

static void __MUnitTab_Draw(void)
{
}

static void __MUnitTab_Play(struct stMobj *pMobj)
{
    MUTAB(pMobj)->busy = 0;
}

void MUnitTab_ChangeMode(struct stMobj *pMobj, char mode)
{
    PS2_FRAME(0x40);
    struct MUnitTabWork *d = MUTAB(pMobj);

    mutab_set_header();
    d->mode = mode;
    MOBJManager_ResetPos(MUTAB_MNG, 4, &d->apMobj[0]);
    if (d->mode == 2) MOBJManager_Move(MUTAB_MNG, 4, &d->apMobj[0], -0x14, 6, 0);
}

void MUnitTab_ChangeCursor(struct stMobj *pMobj, int indexNew)
{
    PS2_FRAME(0x20);
    struct struct_214acd28 pos;

    pos.x = indexNew * 124;
    pos.y = 0;
    if (MUTAB(pMobj)->mode == 2) {
        pos.x -= 0x14;
        pos.y += 6;
    }
    UnitSprite_SetPosition(MUTAB(pMobj)->apMobj[4], &pos);
}

int MUnitTab_IsMove(struct stMobj *pMobj)
{
    return MUTAB(pMobj)->busy != 0;
}

void MUnitTab_FadeOutStart(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    MUTAB(pMobj)->busy = 1;
    mutab_set_header();
    MOBJManager_FadeOut(&MUTAB(pMobj)->apMobj[0], 0, 4, MUTAB_MNG);
}

void MUnitTab_FadeInStart(struct stMobj *pMobj)
{
    MUTAB(pMobj)->busy = 1;
    __MUnitTab_FadeInItem(pMobj);
}

void MUnitTab_Cleanup(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    mutab_set_header();
    MOBJManager_Delete(&MUTAB(pMobj)->apMobj[0], 4, MUTAB_MNG);
}

struct stMobj *MUnitTab_NewSystem(struct stMobj *pParentMobj)
{
    PS2_FRAME(0x20);
    struct stMobj *m = DF_MOBJ_GetNewObject(pParentMobj);

    if (m == 0) return 0;
    memset(&m->dt, 0, 0x24);
    m->type = 0;
    m->stat = 1;
    m->count = 0;
    m->play_func = (void (*)(void))GFN(__MUnitTab_Play);
    m->draw_func = (void (*)(void))GFN(__MUnitTab_Draw);
    m->pkt_func = (void (*)(void))GFN(__MUnitTab_Pkt);
    mutab_set_header();
    MOBJManager_Create(m, &MUTAB(m)->apMobj[0], 4, MUTAB_MNG);
    return m;
}
