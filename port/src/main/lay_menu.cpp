/* src/main/lay_menu.cpp — the eight cockpit / HUD colours (RGBA bytes) used by the area map compass, the pause screen
 * and the enemy info gauge.
 * (sic) An index >= 8 is reduced modulo 7 (so entry 7 is only reachable directly), and a negative index becomes
 * 7 - idx (>= 8): it reads past the stack copy of the table into the caller's frame. All known callers pass 0..7. */
#include "acnb/sdk.h"

static const unsigned int lay_menu_cockpit_col[8] = {0x80295ca6, 0x80673f89, 0x809e3772, 0x80a25d2b,
                                                     0x80778d2a, 0x80397b2d, 0x802c8488, 0x80000000};

void DF_MFUNC_GetCockpitColor(struct_76b810ec *ioCol, int inIdx)
{
    PS2_FRAME(0x30);
    PS2_STACK(unsigned int[8], table, 0x10);
    memcpy(table, lay_menu_cockpit_col, sizeof lay_menu_cockpit_col);
    if (inIdx >= 8) inIdx = inIdx % 7;
    if (inIdx < 0) inIdx = 7 - inIdx;
    memcpy(ioCol, (unsigned int *)table + inIdx, 4);
}
