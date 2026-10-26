/* src/menu/m1help.cpp - the one-line help bar at the bottom of the menu screens.
 * One manager struct *M1HelpMngP (carved out of the menu work memory by DF_M1HELP_Init) holds the text (str), a copy
 * for the line fading out (fo_str), a dummy parent mobj, the current / fading-out string mobjs and the window sprite
 * (UnitSprite of layout 0x3e9, part "1g_help"). Menus call SetString* (immediate), FadeIn / FadeOut (window + text),
 * ChangeString* (cross-fade to a new text) and Finish; Play runs every frame and advances stat:
 * 0 idle, 1 faded in, 2 fading out, 3 finished, 4 text change in progress.
 * Every access re-reads M1HelpMngP, as the original does. strncpy is newlib's (no state): host strncpy on PS2 memory. */
#include <string.h>
#include "acnb/sdk.h"

/* main sdata 0x27b3d0 (only read during the calls) */
static char m1help_part_name[] = "1g_help";

/* text colour (main sdata 0x27b3c0 / 0x27b3c4 / 0x27b3c8, all 0x809b917d), copied into a stack word */
static inline void m1help_col(struct struct_76b810ec &c)
{
    c.r = 0x7d;
    c.g = 0x91;
    c.b = 0x9b;
    c.a = 0x80;
}

/* f_draw = 0, then f_erase = 1 (two byte read-modify-writes) */
static inline void m1help_erase(struct stMobj *m)
{
    m->f_draw = 0;
    m->f_erase = 1;
}

/* mobj + 0x2c (the string mobj's strp); the original stores through it even when the mobj is NULL (PS2 address 0x2c) */
static inline char **m1help_strp_slot(struct stMobj *m)
{
    if (m) return &m->dt.string.strp;
#ifdef ACNB_SOURCEPORT
    static char *null_strp;   /* stands in for PS2 address 0x2c */
    return &null_strp;
#else
    return (char **)((char *)PS2_NULL_MEM() + 0x2c);
#endif
}

/* empty stub (callers pass various arguments) */
void DF_M1HELP_SetWindow(void)
{
}

void DF_M1HELP_Finish(void)
{
    PS2_FRAME(0x10);
    if (!M1HelpMngP) return;
    DF_LO_GetPartsNumInUnitFromName((unsigned char *)LAYOUT_GetLayoutData(0x3e9), (unsigned char *)m1help_part_name);
    UnitSprite_Delete(M1HelpMngP->pMobjWindow);   /* (sic) pMobjWindow is not nulled */
    if (M1HelpMngP->pMobjString) {
        m1help_erase(M1HelpMngP->pMobjString);
        M1HelpMngP->pMobjString = 0;
    }
    if (M1HelpMngP->pMobjStringFadeOut) {
        m1help_erase(M1HelpMngP->pMobjStringFadeOut);
        M1HelpMngP->pMobjStringFadeOut = 0;
    }
    if (M1HelpMngP->pMobjDummy) {
        m1help_erase(M1HelpMngP->pMobjDummy);
        M1HelpMngP->pMobjDummy = 0;
    }
    M1HelpMngP->stat = 3;
}

void DF_M1HELP_FadeOut(void)
{
    PS2_FRAME(0x10);
    if (!M1HelpMngP || M1HelpMngP->stat == 2) return;
    if (M1HelpMngP->pMobjStringFadeOut) {
        m1help_erase(M1HelpMngP->pMobjStringFadeOut);
        M1HelpMngP->pMobjStringFadeOut = 0;
    }
    UnitSprite_SetFadeOut(M1HelpMngP->pMobjWindow);   /* both tolerate NULL */
    DF_MOBJ_SetFadeOut(M1HelpMngP->pMobjString);
    M1HelpMngP->stat = 2;
}

/* re-creates the window on every call (UnitSprite_Delete of the old one, possibly stale after Finish) */
void DF_M1HELP_FadeIn(void)
{
    PS2_FRAME(0x30);
    PS2_STACK(struct struct_76b810ec, col, 0x2c);
    m1help_col(col);
    struct struct_77b9f58c *p = M1HelpMngP;
    if (!p || p->stat == 1) return;
    if (M1HelpMngP->pMobjStringFadeOut) {
        m1help_erase(M1HelpMngP->pMobjStringFadeOut);
        M1HelpMngP->pMobjStringFadeOut = 0;
    }
    if (M1HelpMngP->pMobjString) {
        m1help_erase(M1HelpMngP->pMobjString);
        M1HelpMngP->pMobjString = 0;
    }
    UnitSprite_Delete(M1HelpMngP->pMobjWindow);
    struct _LO_HEADER *h = (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x3e9);
    M1HelpMngP->pMobjWindow = UnitSprite_New(M1HelpMngP->pMobjDummy, h, m1help_part_name);
    DF_MOBJ_SetDrawEnable(M1HelpMngP->pMobjWindow, 1);
    UnitSprite_SetFadeIn(M1HelpMngP->pMobjWindow);
    M1HelpMngP->pMobjString = DF_MOBJ_NewStringDirect(M1HelpMngP->pMobjWindow, (char *)p->str, 6, 0x1c, 0x19b, &col, 0, 0);
    DF_MOBJ_SetFadeIn(M1HelpMngP->pMobjString);
    M1HelpMngP->stat = 1;
}

void DF_M1HELP_ChangeStringId(unsigned int strId)
{
    PS2_FRAME(0x10);
    DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, strId));
}

/* no M1HelpMngP NULL test. The fade-out copy gets 0x42 of the 72 bytes (strncpy, no terminator past 0x42) */
void DF_M1HELP_ChangeString(unsigned char *inStrp)
{
    PS2_FRAME(0x30);
    PS2_STACK(struct struct_76b810ec, col, 0x2c);
    m1help_col(col);
    if (M1HelpMngP->pMobjStringFadeOut) {
        m1help_erase(M1HelpMngP->pMobjStringFadeOut);
        M1HelpMngP->pMobjStringFadeOut = 0;
    }
    M1HelpMngP->pMobjStringFadeOut = M1HelpMngP->pMobjString;
    strncpy((char *)M1HelpMngP->fo_str, (const char *)M1HelpMngP->str, 0x42);
    /* (sic) stored even when there was no string mobj (PS2 address 0x2c) */
    *m1help_strp_slot(M1HelpMngP->pMobjStringFadeOut) = (char *)M1HelpMngP->fo_str;
    DF_MOBJ_SetFadeOut(M1HelpMngP->pMobjStringFadeOut);
    if (inStrp) strncpy((char *)M1HelpMngP->str, (const char *)inStrp, 0x42);
    else M1HelpMngP->str[0] = 0;
    M1HelpMngP->pMobjString =
        DF_MOBJ_NewStringDirect(M1HelpMngP->pMobjWindow, (char *)M1HelpMngP->str, 6, 0x1c, 0x19b, &col, 0, 0);
    DF_MOBJ_SetFadeIn(M1HelpMngP->pMobjString);
    M1HelpMngP->stat = 4;
}

/* immediate text: both string mobjs erased (no fades), stat unchanged; no M1HelpMngP NULL test */
static inline void m1help_set(const unsigned char *str, struct struct_76b810ec *col)
{
    if (M1HelpMngP->pMobjStringFadeOut) {
        m1help_erase(M1HelpMngP->pMobjStringFadeOut);
        M1HelpMngP->pMobjStringFadeOut = 0;
    }
    if (M1HelpMngP->pMobjString) {
        m1help_erase(M1HelpMngP->pMobjString);
        M1HelpMngP->pMobjString = 0;
    }
    if (str) strncpy((char *)M1HelpMngP->str, (const char *)str, 0x42);
    else M1HelpMngP->str[0] = 0;
    M1HelpMngP->pMobjString =
        DF_MOBJ_NewStringDirect(M1HelpMngP->pMobjWindow, (char *)M1HelpMngP->str, 6, 0x1c, 0x19b, col, 0, 0);
}

void DF_M1HELP_SetStringId(unsigned int strId)
{
    PS2_FRAME(0x20);
    unsigned char *str = DF_MSTR_GetInfo(MenuMng.language, 0, strId);
    PS2_STACK(struct struct_76b810ec, col, 0x1c);
    m1help_col(col);
    m1help_set(str, &col);
}

void DF_M1HELP_SetString(unsigned char *inStrp)
{
    PS2_FRAME(0x20);
    PS2_STACK(struct struct_76b810ec, col, 0x1c);
    m1help_col(col);
    m1help_set(inStrp, &col);
}

/* text only, no mobj work; no NULL test of the string (a NULL would copy from PS2 address 0) */
void DF_M1HELP_SetStringIdCopy(unsigned int strId)
{
    PS2_FRAME(0x10);
    const unsigned char *s = DF_MSTR_GetInfo(MenuMng.language, 0, strId);
    if (!s) s = (const unsigned char *)PS2_NULL_MEM();
    strncpy((char *)M1HelpMngP->str, (const char *)s, 0x42);
}

int DF_M1HELP_GetStat(void)
{
    if (!M1HelpMngP) return 0;
    return M1HelpMngP->stat;
}

void DF_M1HELP_Play(void)
{
    struct struct_77b9f58c *p = M1HelpMngP;
    if (!p) return;
    struct stMobj *s = p->pMobjString;
    if (!s) return;
    if (p->stat == 2) {
        if (s->stat == 5) p->stat = 3;
    } else if (p->stat == 4 || p->stat == 1) {
        if (s->stat == 0) p->stat = 0;
    }
}

/* returns the work pointer advanced past the manager; DF_MOBJ_GetNewObject NULL is not tested */
unsigned char *DF_M1HELP_Init(unsigned char *ioBuff)
{
    PS2_FRAME(0x30);
    PS2_STACK(struct stMenuRect, rect, 0x20);   /* OLMENU 0x1eda9d0, copied with lq / sq */
    rect.x = 0;
    rect.y = 0x196;
    rect.w = 0x280;
    rect.h = 0x1c;
    M1HelpMngP = (struct struct_77b9f58c *)ioBuff;
    DF_MFUNC_ClearMemory(M1HelpMngP, 0x30);   /* 0x30 words = the whole 0xc0-byte struct */
    M1HelpMngP->pMobjDummy = DF_MOBJ_GetNewObject((struct stMobj *)((char *)MenuObjTopp + 0x62700));
    M1HelpMngP->shape = 0;
    M1HelpMngP->rect = rect;
    M1HelpMngP->stat = 0;
    return ioBuff + 0xc0;
}
