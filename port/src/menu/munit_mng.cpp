/* src/menu/munit_mng.cpp - registry of the "unit" mobjs that make up the top menu screens. s_stMUnitMng holds one
 * pointer per unit: MUnitMng_Init(parent) creates Program [3], Briefing [4], Colorbar [5], Tab [6], TabInfo [7] and
 * RankingFrame [9] as children of the top scene; MUnitMng_Cleanup cleans them up in the same order, erases each
 * mobj and clears its slot (no NULL guard before the unit Cleanup calls: a second Cleanup would pass NULL);
 * MUnitMng_GetUnit(id) returns a slot (no bounds check). Slots 0-2 and 8 are never touched here. */
#include "acnb/port.h"

/* f_draw = 0, then f_erase = 1, then clear the slot; the original re-loads the slot before each step */
static inline void mum_erase(struct stMobj **pp)
{
    (*pp)->f_draw = 0;
    (*pp)->f_erase = 1;
    *pp = 0;
}

struct stMobj *MUnitMng_GetUnit(int idUnit)
{
    return s_stMUnitMng.apMobjUnit[idUnit];
}

void MUnitMng_Cleanup(void)
{
    PS2_FRAME(0x10);
    struct stMobj **u = s_stMUnitMng.apMobjUnit;

    MUnitProgram_Cleanup(u[3]);
    if (u[3]) mum_erase(&u[3]);
    MUnitBriefing_Cleanup(u[4]);
    if (u[4]) mum_erase(&u[4]);
    MUnitColorbar_Cleanup(u[5]);
    if (u[5]) mum_erase(&u[5]);
    MUnitTab_Cleanup(u[6]);
    if (u[6]) mum_erase(&u[6]);
    MUnitTabInfo_Cleanup(u[7]);
    if (u[7]) mum_erase(&u[7]);
    MUnitRankingFrame_Cleanup(u[9]);
    if (u[9]) mum_erase(&u[9]);
}

void MUnitMng_Init(struct stMobj *pParentMobj)
{
    PS2_FRAME(0x20);
    struct stMobj **u = s_stMUnitMng.apMobjUnit;

    u[3] = MUnitProgram_NewSystem(pParentMobj, 1);
    u[4] = MUnitBriefing_NewSystem(pParentMobj);
    u[5] = MUnitColorbar_NewSystem(pParentMobj);
    u[6] = MUnitTab_NewSystem(pParentMobj);
    u[7] = MUnitTabInfo_NewSystem(pParentMobj);
    u[9] = MUnitRankingFrame_NewSystem(pParentMobj);
}
