/* src/menu/mop.cpp - the garage's Option Parts screen (OLMENU; top-menu item 2, created by
 * __PlayGarageSys_PadCheckWait2 through DF_MOP_NewSystem): a scrolling list (12 rows) of the option parts the player
 * has, a cursor light, a parts window (name, OP slot cost, maker logo, "equipped" mark, 3D preview) and a slot gauge
 * window (used / capacity of the core's option slots, one gauge cell per core slot). Cross toggles the highlighted
 * part's bit in pAc->option.equip[0] (if it fits the core's slot count, else an error dialog), Square clears them all,
 * Up / Down move and scroll, Circle leaves, Start opens the key guide, Select the help. Writes game state
 * (pAc->option.equip[0]).
 * One stMobj whose dt union holds the INLINE struct_30feb6b3 (obj+0x10..0x33; unlike mperf's heap pointer); run by
 * DF_MOBJ_Play / DF_MOBJ_Draw through play_func (__Play) / draw_func (__Draw, empty).
 * Whole file native: the state machine (__Play, __ProcInit / Main / Exit / Dialog / Help / KeyGuide / WaitReadData /
 * PartsListScroll), the fades (__SetFadeAll), the slot window (__Create / __Update / __DeleteSlotWindow), the parts
 * window (__Create / __DeletePartsWindow, __UpdatePartsWindowAfterReadData), the list (__Create / __DeletePartsList,
 * __UpdatePartsListItem, __ChangeSelectPartsList, __SetPartsListScroll) and DF_MOP_NewSystem.
 * menu/mperf.c has same-named statics (__SetFadeAll, __Play, ...): the bridge prelude maps this file's statics by its own
 * local header (addresses 0x1e7a310..), so the names do not clash. */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

#ifdef ACNB_SOURCEPORT
/* main sdata 0x27bbe0.. / OLMENU rodata 0x1edf7f0.. (nodwarf.h): newlib sprintf formats and the stored empty string
 * must be PS2 memory */
char mop_str_empty[8] = "";
char mop_fmt_sp_d_sl[5] = " %d/";
char mop_fmt_d_sl[4] = "%d/";
char mop_fmt_s_sp_d[6] = "%s %d";
char mop_fmt_s_d[5] = "%s%d";
char mop_fmt_sp_d[4] = " %d";
char mop_fmt_d[3] = "%d";
char mop_fmt_parts13[11] = "parts13_%d";
char mop_fmt_parts10[11] = "parts10_%d";
char mop_fmt_parts14[11] = "parts14_%d";
char mop_name_parts13_12[11] = "parts13_12";
char mop_name_parts13_13[11] = "parts13_13";
char mop_name_parts04_0[10] = "parts04_0";
char mop_fmt_parts13_0[10] = "parts13_0";
#endif

/* obj+0x10 is the inline pParam (DWARF has no union member for it) */
#define MOP_P(o) ((struct struct_30feb6b3 *)&(o)->dt)

/* bit of an option part in pAc->option.equip[0]: sllv masks the shift to 5 bits */
static inline unsigned int mop_bit(int id) { return 1u << ((id % 1000 - 1) & 31); }

/* core option slot count: pAc->real_part[1]->prm (PartsParamCoreSt)->slot */
static inline int mop_core_slot(struct struct_30feb6b3 *p)
{
    return ((struct PartsParamCoreSt *)p->pAc->real_part[1]->prm.part.infop)->slot;
}

/* id of the highlighted part; % num is a MIPS div (no trap on 0) */
static inline int mop_cur_id(struct struct_30feb6b3 *p)
{
    struct struct_60554078 *l = p->pPartsList;
    struct stMPartIdTable *t = p->pPartsIdTable;
    return t->cell[ps2_mod32(l->TopListItemIndex + l->CurListItemIndex, t->num)].id;
}

/* sum of the OP slot costs of all equipped parts (count read once, table / AC re-read per entry) */
static inline int mop_used_slots(struct struct_30feb6b3 *p)
{
    int num = p->pPartsIdTable->num;
    int used = 0;
    for (int i = 0; i < num; i++) {
        int id = p->pPartsIdTable->cell[i].id;
        if (p->pAc->option.equip[0] & mop_bit(id))
            used += DF_MPART_GetOPSlot(id);
    }
    return used;
}

/* inlined DF_MOBJ_Delete + clear: f_draw = 0, then f_erase = 1 (byte RMW pair), slot zeroed; null-tested */
static inline void mop_delete(struct stMobj **pp)
{
    if (*pp != 0) {
        (*pp)->f_draw = 0;
        (*pp)->f_erase = 1;
        *pp = 0;
    }
}

static void __Draw(void)
{
}

static void __SetFadeAll(struct stMobj *pObj, int FadeIn)
{
    PS2_FRAME(0xb0);
    struct struct_30feb6b3 *p = MOP_P(pObj);
    struct struct_60554078 *pList = p->pPartsList;
    struct struct_5d9e776d *pWnd = p->pPartsWindow;
    struct struct_f2ea5cb1 *pSlot = p->pSlotWindow;
    /* main sdata 0x27bbc0 / c8 / d0 / d8: {In, Out} pairs, picked by FadeIn != 1 */
    int out = FadeIn != 1;
    void (*const win)(struct stMobj *) = out ? WindowEx_SetFadeOut : WindowEx_SetFadeIn;
    void (*const unit)(struct stMobj *) = out ? UnitSprite_SetFadeOut : UnitSprite_SetFadeIn;
    void (*const str)(struct stMobj *) = out ? DF_MOBJ_SetFadeOut : DF_MOBJ_SetFadeIn;
    void (*const mdl)(struct stMobj *) = out ? DF_MOBJ2_SetMdlWinFadeOut : DF_MOBJ2_SetMdlWinFadeIn;
    int i;

    for (i = 0; i < pList->ListItemNum; i++)
        win(pList->aPartsListItem[i].pWindow);
    if (pList->apScrollBtn[0] != 0) {
        unit(pList->apScrollBtn[0]);
        unit(pList->apScrollBtn[1]);
    }
    win(pList->pCursorLight);
    for (i = 0; i < 2; i++)
        win(pList->aDummyListItem[i].pWindow);
    win(pWnd->apWindowEx[0]);
    for (i = 0; i < 6; i++)
        unit(pWnd->apUnitSprite[i]);
    for (i = 0; i < 5; i++)
        str(pWnd->apString[i]);
    mdl(pWnd->pPartsModelWnd);
    str(pWnd->pHelpDummy);
    unit(pSlot->pWindow);
    for (i = 0; i < mop_core_slot(p); i++)
        win(pSlot->apGauge[i]);
    for (i = 0; i < 4; i++)
        str(pSlot->apString[i]);
}

/* mode 7: the list scroll animation (6 frames), then the rows snap back to their layout places */
static void __ProcPartsListScroll(struct stMobj *pObj)
{
    PS2_FRAME(0xd0);
    PS2_STACK(char[32], aName, 0xa0);
    PS2_STACK(char[32], aCursor, 0x80);
    struct struct_30feb6b3 *p = MOP_P(pObj);
    struct struct_60554078 *pList = p->pPartsList;
    /* main sdata 0x27bbb8 (copied to sp+0xc8): names of the two off-list rows (only read by the layout lookup) */
    char *const apName[2] = {(char *)"parts13_12", (char *)"parts13_13"};
    int i;

    pList->ScrollCount++;
    if (pList->ScrollCount < 6)
        return;
    for (i = 0; i < pList->ListItemNum; i++) {
        ps2_sprintf_d(aName, mop_fmt_parts13, i);
        WindowEx_MoveToLayout(pList->aPartsListItem[i].pWindow, (char *)LAYOUT_GetLayoutData(0x41c), aName, 0);
        DF_MOBJ_SetDrawEnable(pList->aPartsListItem[i].pWindow, 1);
    }
    for (i = 0; i < 2; i++) {
        char *lo = (char *)LAYOUT_GetLayoutData(0x41c);
        WindowEx_MoveToLayout(pList->aDummyListItem[i].pWindow, lo, apName[i], 0);
        WindowEx_SetFadeIn(pList->aDummyListItem[i].pWindow);
        DF_MOBJ_SetDrawEnable(pList->aDummyListItem[i].pWindow, 0);
    }
    pList->ScrollCount = 0;
    pList = p->pPartsList;
    __UpdatePartsListItem(pObj);
    ps2_sprintf_d(aCursor, mop_fmt_parts10, pList->CurListItemIndex);
    WindowEx_MoveToLayout(pList->pCursorLight, (char *)LAYOUT_GetLayoutData(0x426), aCursor, 0);
    p->ProcMode = 6;
}

/* mode 6: wait for the preview model, then fill the parts window and the help line */
static void __ProcWaitReadData(struct stMobj *pObj)
{
    PS2_FRAME(0x30);
    struct struct_30feb6b3 *p = MOP_P(pObj);

    if (DF_MOBJ2_MdlWinReadSync(p->pPartsWindow->pPartsModelWnd) == 1) {
        __UpdatePartsWindowAfterReadData(pObj);
        /* MdlWin obj +0x50 = cur.modelp (stMobj, not null-tested) +0x50 = dt.model.mp (tested) -> prm word +0xd8 */
        struct stMobj *cur = (struct stMobj *)p->pPartsWindow->pPartsModelWnd->dt.model.mp;
        struct _stMModel *mp = cur->dt.model.mp;
        unsigned char *spec = mp != 0 ? (unsigned char *)mp->prm.part.infop : 0;
        /* PartsParamBasicSt.explain[lang] (0x29 + lang * 73) */
        DF_M1HELP_ChangeString(spec != 0 ? spec + 0x29 + MenuMng.language * 73 : 0);
        p->ProcMode = 2;
    }
}

/* mode 5 */
static void __ProcKeyGuide(struct stMobj *pObj)
{
    PS2_FRAME(0x20);
    if (!MenuKeyGuide_IsShown())
        MOP_P(pObj)->ProcMode = 2;
}

/* mode 4 (leaf, no frame) */
static void __ProcHelp(struct stMobj *pObj)
{
    if (MenuMng.f_help == 0)
        MOP_P(pObj)->ProcMode = 2;
}

/* mode 3: the over-capacity dialog */
static void __ProcDialog(struct stMobj *pObj)
{
    PS2_FRAME(0x20);
    if (DF_MDIALOG_Sync() == 4)
        MOP_P(pObj)->ProcMode = 2;
}

/* after an equip change: list rows, cursor light, the "equipped" mark of the current part, slot window */
static inline void mop_after_equip(struct stMobj *pObj, char *buf)
{
    struct struct_30feb6b3 *p = MOP_P(pObj);
    struct struct_60554078 *pList = p->pPartsList;

    __UpdatePartsListItem(pObj);
    ps2_sprintf_d(buf, mop_fmt_parts10, pList->CurListItemIndex);
    WindowEx_MoveToLayout(pList->pCursorLight, (char *)LAYOUT_GetLayoutData(0x426), buf, 0);
    struct stMobj *mark = p->pPartsWindow->apUnitSprite[3];
    if (p->pAc->option.equip[0] & mop_bit(mop_cur_id(p))) {
        DF_MOBJ_SetDrawEnable(mark, 1);
        UnitSprite_SetFadeIn(mark);
    } else if (mark->f_draw == 1) {
        UnitSprite_SetFadeOut(mark);
    }
    __UpdateSlotWindow(pObj);
}

/* mode 2: pad handling */
static void __ProcMain(struct stMobj *pObj)
{
    PS2_FRAME(0x130);
    struct struct_30feb6b3 *p = MOP_P(pObj);
    struct struct_2d84b07b *pad = &MPad[MPadUser];
    int Step = 0;

    if (pad->Rdw == 1) {
        int used = mop_used_slots(p);
        int cur = mop_cur_id(p);
        unsigned int equip = p->pAc->option.equip[0];
        int core = mop_core_slot(p);
        unsigned int bit = mop_bit(cur);

        if (equip & bit) {
            PS2_STACK(char[32], buf, 0xf0);
            p->pAc->option.equip[0] = equip ^ bit;
            mop_after_equip(pObj, buf);
            Voice_Call(0x3ea, 0x7f);
        } else if (core < used + DF_MPART_GetOPSlot(cur)) {
            DF_MDIALOG_Open(4, DF_MSTR_GetInfo(MenuMng.language, 1, 0x30db2), 0, 0, 0, 0, 0, 0);
            p->ProcMode = 3;
            Voice_Call(0x3f1, 0x7f);
        } else {
            PS2_STACK(char[32], buf, 0x110);
            p->pAc->option.equip[0] ^= bit;
            mop_after_equip(pObj, buf);
            Voice_Call(0x3ea, 0x7f);
        }
    } else if (pad->Rlt == 1) {
        PS2_STACK(char[32], buf, 0xd0);
        p->pAc->option.equip[0] = 0;
        mop_after_equip(pObj, buf);
        Voice_Call(0x3f0, 0x7f);
    } else if (pad->Rrt == 1) {
        p->ProcMode = 1;
        pObj->count = 0;
    } else if (pad->repeat.up == 1) {
        Step = -1;
    } else if (pad->repeat.dw == 1) {
        Step = 1;
    } else if (pad->start == 1) {
        MenuKeyGuide_Show(0x13);
        p->ProcMode = 5;
    } else if (pad->select == 1) {
        DF_MHELP_Mode(1);
        p->ProcMode = 4;
    }

    if (Step != 0) {
        __ChangeSelectPartsList(pObj, Step);
        struct struct_c4910010 *col = (struct struct_c4910010 *)((unsigned char *)p->pAc->colp + 0xd0);
        int id = mop_cur_id(p);
        PS2_STACK(unsigned long long, arg9, 0);   /* 9th arg (pAC) at sp+0 */
        arg9 = 0;
        DF_MOBJ2_SetMdlWinChange(p->pPartsWindow->pPartsModelWnd, 1, id, col, col, 1, (unsigned char)(Step >= 0 ? 1 : 2),
                                 2, 0);
        if (p->ProcMode != 7) {
            PS2_STACK(char[32], buf, 0xb0);
            struct struct_60554078 *pList = p->pPartsList;
            __UpdatePartsListItem(pObj);
            ps2_sprintf_d(buf, mop_fmt_parts10, pList->CurListItemIndex);
            WindowEx_MoveToLayout(pList->pCursorLight, (char *)LAYOUT_GetLayoutData(0x426), buf, 0);
        }
        struct struct_5d9e776d *pWnd = p->pPartsWindow;
        for (int i = 0; i < 4; i++)
            UnitSprite_SetFadeOut(pWnd->apUnitSprite[i]);
        for (int i = 0; i < 5; i++)
            DF_MOBJ_SetFadeOut(pWnd->apString[i]);
        __UpdateSlotWindow(pObj);
        DF_M1HELP_ChangeString((unsigned char *)mop_str_empty);
        Voice_Call(0x3ee, 0x7f);
    }
}

/* mode 1: fade out (frame 0), tear everything down (frame 6) */
static void __ProcExit(struct stMobj *pObj)
{
    PS2_FRAME(0x30);
    struct struct_30feb6b3 *p = MOP_P(pObj);

    if (pObj->count == 0) {
        Voice_Call(0x3eb, 0x7f);
        __SetFadeAll(pObj, 0);
        DF_M1HELP_ChangeString((unsigned char *)mop_str_empty);
        DF_MBACK_PopSceneLabel();   /* original passes a0 = 0x22; DWARF void (unused) */
    } else if (pObj->count == 6) {
        __DeletePartsList(pObj);
        __DeletePartsWindow(pObj);
        __DeleteSlotWindow(pObj);
        DF_MHELP_DeleteList(p->Help.apHelpList[1]);
        DF_MMM_Free(0, p->pPartsIdTable);
        p->pPartsIdTable = 0;
        pObj->stat = 5;
        pObj->play_func = 0;
        pObj->draw_func = 0;
    }
    pObj->count++;
}

/* mode 0 */
static void __ProcInit(struct stMobj *pObj)
{
    PS2_FRAME(0x20);
    struct struct_30feb6b3 *p = MOP_P(pObj);

    __SetFadeAll(pObj, 1);
    DF_M_AC_SetFadeOut(M_ACp[p->PlayerNo][0], 5, 0.0f);
    DF_M_AC_SetFadeOut(M_ACp[p->PlayerNo][1], 5, 0.0f);
    DF_M_AC_SetFadeOut(M_ACp[p->PlayerNo][2], 5, 0.0f);
    gcall_as<int(int, int, int)>(GFN(DF_MBGUIDE_Change), 1, 2, 0x63);   /* DWARF int(void); key list ends with 0x63 */
    DF_MVIEW_MotionStart(MViewp, 0x90);
    DF_MBACK_PushSceneLabel((enum_5124d09c)0x22);
    DF_M1HELP_ChangeString((unsigned char *)mop_str_empty);
    p->ProcMode = 6;
    Voice_Call(0x3ec, 0x7f);
}

static void __Play(struct stMobj *pObj)
{
    PS2_FRAME(0x50);
    struct struct_30feb6b3 *p = MOP_P(pObj);
    /* OLMENU 0x1edf7d0, copied to sp+0x30; ProcMode not bound-checked */
    void (*const tbl[8])(struct stMobj *) = {GFN(__ProcInit),   GFN(__ProcExit),        GFN(__ProcMain),
                                             GFN(__ProcDialog), GFN(__ProcHelp),        GFN(__ProcKeyGuide),
                                             GFN(__ProcWaitReadData), GFN(__ProcPartsListScroll)};
#ifndef ACNB_SOURCEPORT
    PS2_STACK(unsigned int[8], apProc, 0x30);
    for (int i = 0; i < 8; i++)
        apProc[i] = PS2PTR(tbl[i]);
#endif
    gcall(tbl[p->ProcMode], pObj);
    if (MenuMng.stat == 0xc && p->ProcMode != 1) {
        p->ProcMode = 1;
        pObj->count = 0;
    }
}

static void __UpdateSlotWindow(struct stMobj *pObj)
{
    PS2_FRAME(0xd0);
    PS2_STACK(char[32], aTempStr, 0x90);
    PS2_STACK(char[32], aString, 0xb0);
    struct struct_30feb6b3 *p = MOP_P(pObj);
    struct struct_f2ea5cb1 *pSlot = p->pSlotWindow;
    int used = mop_used_slots(p);
    int core = mop_core_slot(p);
    int i;

    ps2_sprintf_d(aTempStr, used / 10 == 0 ? mop_fmt_sp_d_sl : mop_fmt_d_sl, used);
    ps2_sprintf_sd(aString, core / 10 == 0 ? mop_fmt_s_sp_d : mop_fmt_s_d, aTempStr, core);
    DF_MOBJ_SetStringCopy(pSlot->apString[1], aString);
    for (i = 0; i < core; i++) {
        if (i < used)
            WindowEx_SetFlag(pSlot->apGauge[i], 2);
        else
            WindowEx_SetFlag(pSlot->apGauge[i], WindowEx_GetFlag(pSlot->apGauge[i]) & ~2u);
    }
    if (used >= 2) {
        DF_MOBJ_SetDrawEnable(pSlot->apString[2], 0);
        DF_MOBJ_SetDrawEnable(pSlot->apString[3], 1);
    } else {
        DF_MOBJ_SetDrawEnable(pSlot->apString[2], 1);
        DF_MOBJ_SetDrawEnable(pSlot->apString[3], 0);
    }
}

static void __DeleteSlotWindow(struct stMobj *pObj)
{
    PS2_FRAME(0x60);
    struct struct_30feb6b3 *p = MOP_P(pObj);
    struct struct_f2ea5cb1 *pSlot = p->pSlotWindow;
    int i;

    UnitSprite_Delete(pSlot->pWindow);
    for (i = 0; i < mop_core_slot(p); i++)
        WindowEx_Delete(pSlot->apGauge[i]);
    for (i = 0; i < 4; i++)
        mop_delete(&pSlot->apString[i]);
    DF_MMM_Free(0, pSlot);
    p->pSlotWindow = 0;
}

static void __CreateSlotWindow(struct stMobj *pObj)
{
    PS2_FRAME(0x90);
    PS2_STACK(char[32], aName, 0x70);
    PS2_STACK(unsigned long long, arg9, 0);   /* 9th arg of NewStringFromLayoutName (flag) at sp+0 */
    struct struct_30feb6b3 *p = MOP_P(pObj);
    struct struct_f2ea5cb1 *pSlot = (struct struct_f2ea5cb1 *)DF_MMM_Alloc(0, 0x64);
    int i;

    memset(pSlot, 0, 0x64);
    p->pSlotWindow = pSlot;
    pSlot->pWindow = UnitSprite_New(pObj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x41c), (char *)"slot_0");
    for (i = 0; i < mop_core_slot(p); i++) {
        ps2_sprintf_d(aName, mop_fmt_parts14, 0x13 - i);
        pSlot->apGauge[i] = WindowEx_NewFromLayout(pObj, (_WINDOWEX_TYPE)0, (char *)LAYOUT_GetLayoutData(0x426), aName);
    }
    arg9 = 0;
    pSlot->apString[0] = DF_MOBJ_NewStringFromLayoutName(pObj, 0, 0x1925a, 0, (char *)LAYOUT_GetLayoutData(0x41c),
                                                         (char *)"parts20e_0", 0, 0, 0);
    pSlot->apString[2] = DF_MOBJ_NewStringFromLayoutName(pObj, 0, 0x1925c, 0, (char *)LAYOUT_GetLayoutData(0x41c),
                                                         (char *)"parts20d_2", 0, 0, 0);
    pSlot->apString[3] = DF_MOBJ_NewStringFromLayoutName(pObj, 0, 0x1925b, 0, (char *)LAYOUT_GetLayoutData(0x41c),
                                                         (char *)"parts20d_2", 0, 0, 0);
    pSlot->apString[1] = DF_MOBJ_NewStringDirectFromLayoutName(pObj, mop_str_empty, (char *)LAYOUT_GetLayoutData(0x41c),
                                                               (char *)"parts20c_12", 0, 0, 0);
}

/* mode 6 done: name, OP slot cost, unit text, "equipped" mark and maker logo of the highlighted part */
static void __UpdatePartsWindowAfterReadData(struct stMobj *pObj)
{
    PS2_FRAME(0x80);
    PS2_STACK(char[32], aString, 0x60);
    struct struct_30feb6b3 *p = MOP_P(pObj);
    struct struct_5d9e776d *pWnd = p->pPartsWindow;
    struct struct_60554078 *pList = p->pPartsList;
    int idx = ps2_mod32(pList->TopListItemIndex + pList->CurListItemIndex, p->pPartsIdTable->num);
    int i;

    DF_MOBJ_SetStringDirect(pWnd->apString[0], DF_MPART_GetNamePointer(p->pPartsIdTable->cell[idx].id));
    int slots = DF_MPART_GetOPSlot(p->pPartsIdTable->cell[idx].id);
    ps2_sprintf_d(aString, slots / 10 == 0 ? mop_fmt_sp_d : mop_fmt_d, slots);
    DF_MOBJ_SetStringCopy(pWnd->apString[1], aString);
    if (slots >= 2) {
        DF_MOBJ_SetDrawEnable(pWnd->apString[2], 0);
        DF_MOBJ_SetDrawEnable(pWnd->apString[3], 1);
    } else {
        DF_MOBJ_SetDrawEnable(pWnd->apString[2], 1);
        DF_MOBJ_SetDrawEnable(pWnd->apString[3], 0);
    }
    DF_MOBJ_SetDrawEnable(pWnd->apString[4], 1);
    if (p->pAc->option.equip[0] & mop_bit(p->pPartsIdTable->cell[idx].id)) {
        DF_MOBJ_SetDrawEnable(pWnd->apUnitSprite[3], 1);
        UnitSprite_SetFadeIn(pWnd->apUnitSprite[3]);
    }
    /* MdlWin obj +0x50 = cur.modelp (stMobj) +0x50 = dt.model.mp (tested) -> part spec (prm word +0xd8); a NULL spec
     * is still read (lb 0x28($zero), unreached) */
    struct stMobj *cur = (struct stMobj *)pWnd->pPartsModelWnd->dt.model.mp;
    struct _stMModel *mp = cur->dt.model.mp;
    const struct PartsParamBasicSt *spec = mp != 0 ? (const struct PartsParamBasicSt *)mp->prm.part.infop
                                                   : (const struct PartsParamBasicSt *)PS2_NULL_MEM();
    for (i = 0; i < 3; i++) {
        if (i == spec->maker) {
            DF_MOBJ_SetDrawEnable(pWnd->apUnitSprite[i], 1);
            UnitSprite_SetFadeIn(pWnd->apUnitSprite[i]);
        } else {
            DF_MOBJ_SetDrawEnable(pWnd->apUnitSprite[i], 0);
        }
    }
    for (i = 0; i < 5; i++)
        DF_MOBJ_SetFadeIn(pWnd->apString[i]);
}

static void __DeletePartsWindow(struct stMobj *pObj)
{
    PS2_FRAME(0x50);
    struct struct_30feb6b3 *p = MOP_P(pObj);
    struct struct_5d9e776d *pWnd = p->pPartsWindow;
    int i;

    WindowEx_Delete(pWnd->apWindowEx[0]);
    for (i = 0; i < 6; i++)
        UnitSprite_Delete(pWnd->apUnitSprite[i]);
    for (i = 0; i < 5; i++)
        mop_delete(&pWnd->apString[i]);
    DF_MOBJ2_DeleteMdlWin(pWnd->pPartsModelWnd);
    mop_delete(&pWnd->pHelpDummy);
    DF_MMM_Free(0, pWnd);
    p->pPartsWindow = 0;
}

static void __CreatePartsWindow(struct stMobj *pObj)
{
    PS2_FRAME(0xc0);
    PS2_STACK(unsigned long long[8], aArg, 0);   /* stack args (sp+0..0x38) of the 9+ argument calls */
    PS2_STACK(struct stMenuRect, DummyRect, 0x90);
    PS2_STACK(struct stMenuRect, ModelRect, 0xa0);
    struct struct_30feb6b3 *p = MOP_P(pObj);
    /* main sdata 0x27bbb0: the "parts03_0" pointer (moved to sp+0xbc through lwc1 / swc1; layout lookup only) */
    char *const apWndExPartsName[1] = {(char *)"parts03_0"};
    struct struct_5d9e776d *pWnd = (struct struct_5d9e776d *)DF_MMM_Alloc(0, 0x38);
    int i;

    memset(pWnd, 0, 0x38);
    p->pPartsWindow = pWnd;
    pWnd->apWindowEx[0] =
        WindowEx_NewFromLayout(pObj, (_WINDOWEX_TYPE)0, (char *)LAYOUT_GetLayoutData(0x41c), apWndExPartsName[0]);
    pWnd->apUnitSprite[0] = UnitSprite_New(pObj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x426), (char *)"kigyou_mir_0");
    pWnd->apUnitSprite[0]->type = 7;
    pWnd->apUnitSprite[1] = UnitSprite_New(pObj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x426), (char *)"kigyou_cre_0");
    pWnd->apUnitSprite[1]->type = 7;
    pWnd->apUnitSprite[2] = UnitSprite_New(pObj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x426), (char *)"kigyou_kis_0");
    pWnd->apUnitSprite[2]->type = 7;
    pWnd->apUnitSprite[3] = UnitSprite_New(pObj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x41c), (char *)"equipped_0");
    pWnd->apUnitSprite[3]->type = 7;
    pWnd->apUnitSprite[3]->draw_prio = 3;
    pWnd->apUnitSprite[4] = UnitSprite_New(pObj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x41c), (char *)"parts04_0");
    pWnd->apUnitSprite[5] = UnitSprite_New(pObj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x41c), (char *)"parts04_1");
    for (i = 0; i < 4; i++)
        DF_MOBJ_SetDrawEnable(pWnd->apUnitSprite[i], 0);
    DF_MOBJ_SetDrawEnable(pWnd->apUnitSprite[4], 1);
    DF_MOBJ_SetDrawEnable(pWnd->apUnitSprite[5], 1);
    pWnd->apString[0] = DF_MOBJ_NewStringDirectFromLayoutName(pObj, mop_str_empty, (char *)LAYOUT_GetLayoutData(0x41c),
                                                              (char *)"parts20c_13", 0, 0, 0);
    pWnd->apString[1] = DF_MOBJ_NewStringDirectFromLayoutName(pObj, mop_str_empty, (char *)LAYOUT_GetLayoutData(0x41c),
                                                              (char *)"parts20b_0", 0, 0, 0x10000);
    aArg[0] = 0;   /* 9th arg (flag) */
    pWnd->apString[2] = DF_MOBJ_NewStringFromLayoutName(pObj, 0, 0x19255, 0, (char *)LAYOUT_GetLayoutData(0x41c),
                                                        (char *)"parts20d_0", 0, 0, 0);
    aArg[0] = 0;
    pWnd->apString[3] = DF_MOBJ_NewStringFromLayoutName(pObj, 0, 0x19256, 0, (char *)LAYOUT_GetLayoutData(0x41c),
                                                        (char *)"parts20d_0", 0, 0, 0);
    aArg[0] = 0;
    pWnd->apString[4] = DF_MOBJ_NewStringFromLayoutName(pObj, 0, 0x19257, 0, (char *)LAYOUT_GetLayoutData(0x41c),
                                                        (char *)"parts20d_1", 0, 0, 0);
    for (i = 2; i < 5; i++)
        DF_MOBJ_SetDrawEnable(pWnd->apString[i], 0);

    /* OLMENU 0x1edf7c0 (lq copy); DummyRect zeroed by a byte loop */
    ModelRect.x = 0x160;
    ModelRect.y = 0x58;
    ModelRect.w = 0x100;
    ModelRect.h = 0xa2;
    memset(&DummyRect, 0, sizeof(DummyRect));
    int id = p->pPartsIdTable->cell[0].id;
    aArg[0] = (unsigned long long)id;
    aArg[1] = 0;
    aArg[2] = 0;
    aArg[3] = (unsigned long long)(long long)p->PlayerNo;
    aArg[4] = 3;
    aArg[5] = PS2PTR(mop_name_parts04_0);
    aArg[6] = 2;
    aArg[7] = 0;
    struct struct_c4910010 *col = (struct struct_c4910010 *)((unsigned char *)p->pAc->colp + 0xd0);
    /* inUnused3 = address of "parts04_0" (ignored); offset_cx / cy and scale = 0.0f (f12-f14) */
    pWnd->pPartsModelWnd = DF_MOBJ2_NexusNewMdlWin(pObj, 2, &DummyRect, &ModelRect, &DummyRect, col, 0, 1, id, 0, 0,
                                                   p->PlayerNo, 3, (int)PS2PTR(mop_name_parts04_0), 2, 0.0f, 0.0f, 0,
                                                   0.0f);
    pWnd->pHelpDummy = DF_MOBJ_NewStringDirectFromLayoutName(pObj, mop_str_empty, (char *)LAYOUT_GetLayoutData(0x41c),
                                                             (char *)"parts20b_1", 0, 0, 0);
}

/* equipped highlight (window flag 2) of a list row */
static inline void mop_row_flag(struct struct_30feb6b3 *p, struct stMobj *win, int id)
{
    if (p->pAc->option.equip[0] & mop_bit(id))
        WindowEx_SetFlag(win, 2);
    else
        WindowEx_SetFlag(win, WindowEx_GetFlag(win) & ~2u);
}

/* start of the list scroll animation (mode 7): the rows slide by one place, two off-list rows slide in / out.
 * Step > 0: the list moves up (the only path garage1 runs); Step <= 0: down (unreached). The original uses the fixed
 * names as sprintf FORMATS (a2 unset: a dummy int, they have no conversion). */
static void __SetPartsListScroll(struct stMobj *pObj, int Step)
{
    PS2_FRAME(0x150);
    PS2_STACK(char[32], aName, 0xb0);
    PS2_STACK(char[32], aTgt0, 0xd0);   /* aaFadeTargetName[0] */
    PS2_STACK(char[32], aTgt1, 0xf0);   /* aaFadeTargetName[1] */
    PS2_STACK(char[32], aPos0, 0x110);  /* aaFadePositionName[0] */
    PS2_STACK(char[32], aPos1, 0x130);  /* aaFadePositionName[1] */
    struct struct_30feb6b3 *p = MOP_P(pObj);
    struct stMPartIdTable *ids = p->pPartsIdTable;
    struct struct_60554078 *pList = p->pPartsList;
    int num = ids->num;
    int idA, idB, dir, first, last, a, b, ListNo;

    if (Step > 0) {
        idA = ids->cell[ps2_mod32(pList->TopListItemIndex - 1 + num, num)].id;
        ps2_sprintf_d(aPos0, mop_fmt_parts13_0, 0);
        ps2_sprintf_d(aTgt0, mop_name_parts13_12, 0);
        idB = ids->cell[ps2_mod32(pList->TopListItemIndex - 1 + pList->ListItemNum, num)].id;
        ps2_sprintf_d(aPos1, mop_name_parts13_13, 0);
        ps2_sprintf_d(aTgt1, mop_fmt_parts13, pList->ListItemNum - 1);
        DF_MOBJ_SetDrawEnable(pList->aPartsListItem[0].pWindow, 0);
        dir = -1;
        first = 1;
        last = pList->ListItemNum - 1;
        a = 0;
        b = 1;
    } else {
        int n = pList->ListItemNum;
        idA = ids->cell[ps2_mod32(pList->TopListItemIndex + n, num)].id;
        ps2_sprintf_d(aPos0, mop_fmt_parts13, n - 1);
        ps2_sprintf_d(aTgt0, mop_name_parts13_13, 0);
        idB = ids->cell[pList->TopListItemIndex].id;   /* plain index, no % num */
        ps2_sprintf_d(aPos1, mop_name_parts13_12, 0);
        ps2_sprintf_d(aTgt1, mop_fmt_parts13_0, 0);
        DF_MOBJ_SetDrawEnable(pList->aPartsListItem[pList->ListItemNum - 1].pWindow, 0);
        dir = 1;
        first = 0;
        last = pList->ListItemNum - 2;
        a = 1;
        b = 0;
    }

    struct stMobj **pw = &pList->aDummyListItem[a].pWindow;
    DF_MOBJ_SetDrawEnable(*pw, 1);
    WindowEx_MoveToLayout(*pw, (char *)LAYOUT_GetLayoutData(0x41c), aPos0, 0);
    WindowEx_MoveToLayout(*pw, (char *)LAYOUT_GetLayoutData(0x41c), aTgt0, 6);
    WindowEx_SetString(*pw, DF_MPART_GetNamePointer(idA));
    WindowEx_SetFadeOut(*pw);
    mop_row_flag(p, *pw, idA);

    pw = &pList->aDummyListItem[b].pWindow;
    DF_MOBJ_SetDrawEnable(*pw, 1);
    WindowEx_MoveToLayout(*pw, (char *)LAYOUT_GetLayoutData(0x41c), aPos1, 0);
    WindowEx_MoveToLayout(*pw, (char *)LAYOUT_GetLayoutData(0x41c), aTgt1, 6);
    WindowEx_SetString(*pw, DF_MPART_GetNamePointer(idB));
    WindowEx_SetFadeIn(*pw);
    mop_row_flag(p, *pw, idB);

    for (ListNo = first; ListNo <= last; ListNo++) {
        ps2_sprintf_d(aName, mop_fmt_parts13, ListNo + dir);
        WindowEx_MoveToLayout(pList->aPartsListItem[ListNo].pWindow, (char *)LAYOUT_GetLayoutData(0x41c), aName, 6);
    }
}

/* cursor move by Step rows: wraps inside the page with <= 12 parts, else scrolls the list (mode 7) */
static void __ChangeSelectPartsList(struct stMobj *pObj, int Step)
{
    PS2_FRAME(0x20);
    struct struct_30feb6b3 *p = MOP_P(pObj);
    struct struct_60554078 *pList = p->pPartsList;
    int num = p->pPartsIdTable->num;
    int vis = num < 12 ? num : 12;
    int cur = pList->CurListItemIndex + Step;
    int top = pList->TopListItemIndex;

    if (num < 13) {
        if (cur < 0)
            cur += vis;
        else if (cur >= vis)
            cur -= vis;
    } else if (Step < 0) {
        if (cur < 0) {
            top = ps2_mod32(num + (top + Step), num);
            cur = 0;
        }
    } else if (Step > 0 && cur >= vis) {
        top = ps2_mod32(top + Step, num);
        cur = vis - 1;
    }
    pList->CurListItemIndex = cur;
    if (pList->TopListItemIndex != top) {
        pList->TopListItemIndex = top;
        __SetPartsListScroll(pObj, Step);
        p->ProcMode = 7;
    } else {
        p->ProcMode = 6;
    }
}

/* row names and equipped highlights of the visible rows */
static void __UpdatePartsListItem(struct stMobj *pObj)
{
    PS2_FRAME(0x90);
    struct struct_30feb6b3 *p = MOP_P(pObj);
    struct struct_60554078 *pList = p->pPartsList;
    int num = p->pPartsIdTable->num;
    int top = pList->TopListItemIndex;

    for (int i = 0; i < pList->ListItemNum; i++) {
        int id = p->pPartsIdTable->cell[ps2_mod32(top + i, num)].id;
        mop_row_flag(p, pList->aPartsListItem[i].pWindow, id);
        WindowEx_SetString(pList->aPartsListItem[i].pWindow, DF_MPART_GetNamePointer(id));
    }
}

static void __DeletePartsList(struct stMobj *pObj)
{
    PS2_FRAME(0x50);
    struct struct_30feb6b3 *p = MOP_P(pObj);
    struct struct_60554078 *pList = p->pPartsList;
    int i;

    for (i = 0; i < pList->ListItemNum; i++)
        WindowEx_Delete(pList->aPartsListItem[i].pWindow);
    if (pList->apScrollBtn[0] != 0) {
        UnitSprite_Delete(pList->apScrollBtn[0]);
        UnitSprite_Delete(pList->apScrollBtn[1]);
    }
    WindowEx_Delete(pList->pCursorLight);
    for (i = 0; i < 2; i++)
        WindowEx_Delete(pList->aDummyListItem[i].pWindow);
    DF_MMM_Free(0, pList);
    p->pPartsList = 0;
}

static void __CreatePartsList(struct stMobj *pObj)
{
    PS2_FRAME(0xb0);
    PS2_STACK(char[32], aName, 0x80);
    struct struct_30feb6b3 *p = MOP_P(pObj);
    /* main sdata 0x27bba0 {x, y} (sp+0xa8) and 0x27bba8 names of the two off-list rows (sp+0xa0, layout lookup only) */
    const int StringOffset[2] = {0x23, 5};
    char *const apWindowPartsName[2] = {(char *)"parts13_12", (char *)"parts13_13"};
    struct struct_60554078 *pList = (struct struct_60554078 *)DF_MMM_Alloc(0, 0x54);
    int i;

    memset(pList, 0, 0x54);
    p->pPartsList = pList;
    pList->CurListItemIndex = 0;
    pList->TopListItemIndex = 0;
    pList->ListItemNum = p->pPartsIdTable->num < 12 ? p->pPartsIdTable->num : 12;
    pList->ScrollCount = 0;
    for (i = 0; i < pList->ListItemNum; i++) {
        ps2_sprintf_d(aName, mop_fmt_parts13, i);
        pList->aPartsListItem[i].pWindow =
            WindowEx_NewFromLayout(pObj, (_WINDOWEX_TYPE)0, (char *)LAYOUT_GetLayoutData(0x41c), aName);
        WindowEx_SetStringPos(pList->aPartsListItem[i].pWindow, StringOffset[0], StringOffset[1]);
    }
    if (p->pPartsIdTable->num > 12) {
        pList->apScrollBtn[0] =
            UnitSpriteBlink_New(pObj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x41c), (char *)"up_0");
        pList->apScrollBtn[1] =
            UnitSpriteBlink_New(pObj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x41c), (char *)"down_0");
    }
    pList->pCursorLight =
        WindowEx_NewFromLayout(pObj, (_WINDOWEX_TYPE)0, (char *)LAYOUT_GetLayoutData(0x426), (char *)"parts10_0");
    for (i = 0; i < 2; i++) {
        pList->aDummyListItem[i].pWindow =
            WindowEx_NewFromLayout(pObj, (_WINDOWEX_TYPE)0, (char *)LAYOUT_GetLayoutData(0x41c), apWindowPartsName[i]);
        WindowEx_SetStringPos(pList->aDummyListItem[i].pWindow, StringOffset[0], StringOffset[1]);
        DF_MOBJ_SetDrawEnable(pList->aDummyListItem[i].pWindow, 0);
    }
}

struct stMobj *DF_MOP_NewSystem(struct stMobj *pParentObj, int PlayerNo)
{
    PS2_FRAME(0x70);
    PS2_STACK(char[32], aName, 0x50);
    struct stMobj *obj = DF_MOBJ_GetNewObject(pParentObj);
    int i;

    if (obj == 0)
        return 0;
    struct struct_30feb6b3 *p = MOP_P(obj);
    obj->type = 0;
    obj->stat = 1;
    obj->play_func = (void (*)(void))GFN(__Play);
    obj->draw_func = (void (*)(void))GFN(__Draw);
    obj->count = 0;
    p->pAc = M_ACp[PlayerNo][0];
    p->PlayerNo = PlayerNo;
    p->ProcMode = 0;
    p->pPartsIdTable = (struct stMPartIdTable *)DF_MMM_Alloc(0, 0x410);
    DF_MPART_MakeIDTable(p->pPartsIdTable, 0, PlayerNo, 13, -1);
    __CreatePartsList(obj);
    struct struct_60554078 *pList = p->pPartsList;
    __UpdatePartsListItem(obj);
    ps2_sprintf_d(aName, mop_fmt_parts10, pList->CurListItemIndex);
    WindowEx_MoveToLayout(pList->pCursorLight, (char *)LAYOUT_GetLayoutData(0x426), aName, 0);
    __CreatePartsWindow(obj);
    struct struct_5d9e776d *pWnd = p->pPartsWindow;
    for (i = 0; i < 4; i++)
        UnitSprite_SetFadeOut(pWnd->apUnitSprite[i]);
    for (i = 0; i < 5; i++)
        DF_MOBJ_SetFadeOut(pWnd->apString[i]);
    __CreateSlotWindow(obj);
    __UpdateSlotWindow(obj);
    pWnd = p->pPartsWindow;
    struct struct_f2ea5cb1 *pSlot = p->pSlotWindow;
    p->Help.apHelpList[1] = DF_MHELP_NewList(0);
    DF_MGEN_MHELP_Add(p->Help.apHelpList[1], 0x18a89, pWnd->apString[0]);
    DF_MGEN_MHELP_Add(p->Help.apHelpList[1], 0x19280, pWnd->pHelpDummy);
    for (i = 1; i < 5; i++)
        pWnd->apString[i]->f_help = 1;
    DF_MGEN_MHELP_Add(p->Help.apHelpList[1], 0x19281, pSlot->apString[1]);
    pSlot->apString[2]->f_help = 1;
    pSlot->apString[3]->f_help = 1;
    return obj;
}
