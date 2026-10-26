/* src/menu/mwepcolor2.cpp - the weapon-colour picker box of the garage Paint menu (OLMENU, layout 0x405): a 4-row
 * scrolling list of the 6 weapon colours (UnitSprite "banner_0" + string label per row, cursor window "parts10_%d",
 * up / down arrows), a frame window "parts09_0" and two preview models (colour box 0x1cc and its shadow 0x1cd).
 * MenuWepColorSelColor_New (from mwepcolor1's __WepSel_Play) builds it for one colour category (ctgr 0 = all weapon
 * slots, 1..7 = one slot); Up / Down write the colour index into the working copy of the AC's colour block and update
 * the AC and the preview at once, Cross commits, Circle restores the copy taken on entry (Finish).
 * Everything here is reached through stored function pointers: the object's play / draw / pkt (DF_MOBJ_Play / Draw /
 * Pkt), the five MenuScroll callbacks (mscroll.cpp) and each label's play_func.
 * Whole file native. The vec2i / colour / sprintf locals handed to callees live at their original PS2 stack slots;
 * the model position / angle / scale / colour tables are the original data (guest addresses: the interpreted
 * DF_MGEN_NewModelEasy and DF_MMODEL_SetVertColor read them). */
#include <string.h>
#include "acnb/pf.h"
#include "acnb/nodwarf.h"

#ifdef ACNB_SOURCEPORT
/* main sdata 0x27c4e0, OLMENU rodata 0x1ee49d0..0x1ee4a14, bss 0x1efc4d0 */
struct struct_76b810ec HAKOKAGEMDL_COLOR = {0x80, 0x80, 0x80, 0x40};
float HAKOMDL_POS[4] = {-0x1.79999ap+3f, -2.0f, -2.0f, 1.0f};
float HAKOMDL_SCL[4] = {1.0f, 1.0f, 1.0f, 1.0f};
float HAKOKAGEMDL_POS[4] = {-0x1.79999ap+3f, -0x1.0624dep-10f, -2.0f, 1.0f};
struct struct_76b810ec HAKOMDL_COLOR[6] = {
    {0x32, 0x32, 0x32, 0x80}, {0xbe, 0x22, 0x28, 0x80}, {0xc8, 0xa0, 0x28, 0x80},
    {0x3c, 0x96, 0x28, 0x80}, {0x28, 0x5a, 0xb4, 0x80}, {0xdc, 0xdc, 0xdc, 0x80},
};
float HAKOMDL_ANG[4] = {0.0f, 0.0f, 0.0f, 0.0f};
/* OLMENU 0x1ee4a28 (nodwarf.h) */
char mwepcolor2_fmt_parts10[11] = "parts10_%d";
#endif

#define WC_LABEL(o) ((struct StSelectLabel *)(o))
#define WC_LAYOUT() ((char *)LAYOUT_GetLayoutData(0x405))
/* label row y: 0x111 + 28 * row (the x is the 0x63 of the sdata vec2i {0x63, 0}) */
#define WC_ROW_Y(n) (0x111 + (n) * 28)

/* "f_draw = 0, f_erase = 1" (two read-modify-writes of byte 0 in the original) */
static inline void wc_erase(struct stMobj *o)
{
    o->f_draw = 0;
    o->f_erase = 1;
}

/* erase a label's string, delete its sprite, erase the label (each null-guarded; the caller clears its slot) */
static inline void wc_delete_label(struct stMobj *o)
{
    struct StSelectLabel *l = WC_LABEL(o);
    if (l->pString) {
        wc_erase(l->pString);
        l->pString = 0;
    }
    if (l->pSprite) {
        UnitSprite_Delete(l->pSprite);
        l->pSprite = 0;
    }
    if (o) wc_erase(o);
}

/* the label sprite sits at its mover position minus the list origin {0x63, 0x111}, the string at +{0x11, 5} */
static inline void wc_place_label(struct StSelectLabel *l, struct struct_214acd28 &tmp)
{
    tmp.x = 0x63;
    tmp.y = 0x111;
    tmp.x = l->Pos.PosCur.x - tmp.x;
    tmp.y = l->Pos.PosCur.y - tmp.y;
    UnitSprite_SetPosition(l->pSprite, &tmp);
    DF_MOBJ_SetStringPos(l->pString, l->Pos.PosCur.x + 0x11, l->Pos.PosCur.y + 5);
}

static void __SelLabel_Play(struct StSelectLabel *pLabel)
{
    PS2_FRAME(0x30);
    PS2_STACK(struct struct_214acd28, tmp, 0x28);
    Mover_Update(&pLabel->Pos);
    wc_place_label(pLabel, tmp);
}

/* the row leaving the page: drop the previous leaving label, slide this one to the next row's place and fade it out */
static void __WepColor_OnMoveItemFadeOut(struct stMenuWepColorSelColor *pWepColor, int pageindex, int nextindex)
{
    PS2_FRAME(0x60);
    PS2_STACK(struct struct_214acd28, nextpos, 0x58); /* dead local: zeroed, .y = nextindex * 28 */
    PS2_STACK(struct struct_214acd28, pos, 0x50);
    memset(&nextpos, 0, sizeof(nextpos));
    nextpos.y = nextindex * 28;
    if (pWepColor->pLabelFO) {
        wc_delete_label(pWepColor->pLabelFO);
        pWepColor->pLabelFO = 0;
    }
    pWepColor->pLabelFO = pWepColor->apLabel[pageindex];
    pWepColor->apLabel[pageindex] = 0;
    pos.x = 0x63;
    pos.y = 0;
    pos.y = WC_ROW_Y(nextindex);
    Mover_Move(&WC_LABEL(pWepColor->pLabelFO)->Pos, &pos, 6);
    struct StSelectLabel *l = WC_LABEL(pWepColor->pLabelFO);
    DF_MOBJ_SetFadeOut(l->pString);
    UnitSprite_SetFadeOut(l->pSprite);
}

/* the row entering the page: start at the previous row's place, slide in and fade in */
static void __WepColor_OnMoveItemFadeIn(struct stMenuWepColorSelColor *pWepColor, int pageindex, int previndex)
{
    PS2_FRAME(0x40);
    PS2_STACK(struct struct_214acd28, prev, 0x38);
    PS2_STACK(struct struct_214acd28, pos, 0x30);
    struct stMobj **slot = &pWepColor->apLabel[pageindex];
    prev.x = 0x63;
    prev.y = 0;
    prev.y = WC_ROW_Y(previndex);
    Mover_SetPos(&WC_LABEL(*slot)->Pos, &prev);
    pos.x = 0x63;
    pos.y = 0;
    pos.y = WC_ROW_Y(pageindex);
    Mover_Move(&WC_LABEL(*slot)->Pos, &pos, 6);
    struct StSelectLabel *l = WC_LABEL(*slot);
    DF_MOBJ_SetFadeIn(l->pString);
    UnitSprite_SetFadeIn(l->pSprite);
}

static void __WepColor_OnMoveItem(struct stMenuWepColorSelColor *pWepColor, int pageindex, int nextpageindex)
{
    PS2_FRAME(0x20);
    PS2_STACK(struct struct_214acd28, pos, 0x18);
    pWepColor->apLabel[nextpageindex] = pWepColor->apLabel[pageindex];
    pWepColor->apLabel[pageindex] = 0;
    pos.x = 0x63;
    pos.y = 0;
    pos.y = WC_ROW_Y(nextpageindex);
    Mover_Move(&WC_LABEL(pWepColor->apLabel[nextpageindex])->Pos, &pos, 6);
}

static void __WepColor_OnMoveCursor(struct stMenuWepColorSelColor *pWepColor, int pageindex)
{
    PS2_FRAME(0x120);
    PS2_STACK(char[0x100], buff, 0x20);
    pf_sprintf_d(buff, mwepcolor2_fmt_parts10, pageindex);
    WindowEx_MoveToLayout(pWepColor->pCursor, WC_LAYOUT(), buff, 0);
}

/* list row `index` (colour name string 0x19528 + index) at page row `pageindex` */
static void __WepColor_OnCreateItem(struct stMenuWepColorSelColor *pWepColor, int index, int pageindex)
{
    PS2_FRAME(0x70);
    PS2_STACK(struct struct_214acd28, pos, 0x60);
    PS2_STACK(struct struct_214acd28, tmp, 0x58);
    PS2_STACK(unsigned int, col, 0x6c); /* sdata colour word 0x8092928c (an lwc1 / swc1 copy: keep the bits) */
    unsigned char *name = DF_MSTR_GetName(MenuMng.language, 0, index + 0x19528);
    pos.x = 0x63;
    pos.y = 0;
    pos.y = WC_ROW_Y(pageindex);
    col = 0x8092928cu;
    struct StSelectLabel *l = WC_LABEL(DF_MOBJ_GetNewObject((struct stMobj *)pWepColor));
    Mover_SetPos(&l->Pos, &pos);
    l->pSprite = UnitSprite_New((struct stMobj *)l, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x405),
                                (char *)"banner_0");
    l->pString = DF_MOBJ_NewStringDirect((struct stMobj *)l, (char *)name, 2, 0, 0, (struct struct_76b810ec *)&col, 0,
                                         0);
    l->Base.play_func = (void (*)(struct stMobj *))GFN(__SelLabel_Play);
    wc_place_label(l, tmp);
    pWepColor->apLabel[pageindex] = (struct stMobj *)l;
    l = WC_LABEL(pWepColor->apLabel[pageindex]);
    DF_MOBJ_SetFadeIn(l->pString);
    UnitSprite_SetFadeIn(l->pSprite);
}

/* arrows: fade in (and enable drawing) when the page can scroll that way, fade out (drawing stays on) when not */
static void __WepColor_CheckYaji(struct stMenuWepColorSelColor *pWepColor)
{
    PS2_FRAME(0x20);
    if (MenuScroll_IsPageUpEnable(&pWepColor->Scroll)) {
        if (!pWepColor->bYajiUp) {
            pWepColor->bYajiUp = 1;
            DF_MOBJ2_SetBlinkSpriteFadeIn(pWepColor->pYajiUp);
            DF_MOBJ_SetDrawEnable(pWepColor->pYajiUp, 1);
        }
    } else if (pWepColor->bYajiUp) {
        pWepColor->bYajiUp = 0;
        DF_MOBJ2_SetBlinkSpriteFadeOut(pWepColor->pYajiUp);
    }
    if (MenuScroll_IsPageDownEnable(&pWepColor->Scroll)) {
        if (!pWepColor->bYajiDown) {
            pWepColor->bYajiDown = 1;
            DF_MOBJ2_SetBlinkSpriteFadeIn(pWepColor->pYajiDown);
            DF_MOBJ_SetDrawEnable(pWepColor->pYajiDown, 1);
        }
    } else if (pWepColor->bYajiDown) {
        pWepColor->bYajiDown = 0;
        DF_MOBJ2_SetBlinkSpriteFadeOut(pWepColor->pYajiDown);
    }
}

/* weapon slot -> AC colour record (ctgr 0 "all" and 1 share record 5; 7 shares 5's record 9) */
static _M_AC_COLORCTGR __GetColorCtgr(_WEPCOLOR_CTGR ctgr)
{
    switch ((unsigned char)ctgr) {
    case 1: return 5;
    case 2: return 6;
    case 3: return 7;
    case 4: return 8;
    case 5: return 9;
    case 6: return 10;
    case 7: return 9;
    default: return 5;
    }
}

/* teardown; ExitMode 1 commits the working colours, anything else restores (colp NULL for the AC, the entry copy for
 * a single slot's weapon models) */
static void __WepColor_Finish(struct stMenuWepColorSelColor *pWepColor)
{
    PS2_FRAME(0x50);
    if (pWepColor->pLabelFO) {
        wc_delete_label(pWepColor->pLabelFO);
        pWepColor->pLabelFO = 0;
    }
    for (unsigned int i = 0; i < 4; i++) {
        if (pWepColor->apLabel[i]) {
            wc_delete_label(pWepColor->apLabel[i]);
            pWepColor->apLabel[i] = 0;
        }
    }
    if (pWepColor->pBoxWnd) {
        WindowEx_Delete(pWepColor->pBoxWnd);
        pWepColor->pBoxWnd = 0;
    }
    if (pWepColor->pCursor) {
        WindowEx_Delete(pWepColor->pCursor);
        pWepColor->pCursor = 0;
    }
    if (pWepColor->pYajiUp) {
        wc_erase(pWepColor->pYajiUp);
        pWepColor->pYajiUp = 0;
    }
    if (pWepColor->pYajiDown) {
        wc_erase(pWepColor->pYajiDown);
        pWepColor->pYajiDown = 0;
    }
    DF_MMODEL_DeleteModelAndMMD(pWepColor->pHakoMdl);
    pWepColor->pHakoMdl = 0;
    DF_MMODEL_DeleteModelAndMMD(pWepColor->pHakoKageMdl);
    pWepColor->pHakoKageMdl = 0;
    if ((unsigned char)pWepColor->ExitMode == 1) {
        DF_M_AC_UpdateWepColor(pWepColor->pAC, pWepColor->pAcColor, 1);
        if (pWepColor->ctgr == 0)
            DF_M_AC_UpdateWepColor(pWepColor->pAcModel->dt.model.acp, pWepColor->pAcColor, 1);
    } else {
        DF_M_AC_UpdateWepColor(pWepColor->pAC, 0, 1);
        if (pWepColor->ctgr == 0) {
            DF_M_AC_UpdateWepColor(pWepColor->pAcModel->dt.model.acp, 0, 1);
        } else {
            unsigned int rec = (unsigned char)__GetColorCtgr(pWepColor->ctgr);
            DF_MMODEL_SetWepColor(pWepColor->pAcModel->dt.model.mp, &pWepColor->pAcColorRestore->p_col[rec]);
            if ((unsigned char)pWepColor->ctgr == 7)
                DF_MMODEL_SetWepColor(pWepColor->pAcModel->dt.model.add_mp[0], &pWepColor->pAcColorRestore->p_col[rec]);
        }
    }
    DF_MMM_Free(0, pWepColor->pAcColor);
    pWepColor->pAcColor = 0;
    DF_MMM_Free(0, pWepColor->pAcColorRestore);
    pWepColor->pAcColorRestore = 0;
    DF_MOBJ_Finish((struct stMobj *)pWepColor);
}

static void __WepColor_Enter(struct stMenuWepColorSelColor *pWepColor)
{
    PS2_FRAME(0x120);
    PS2_STACK(char[0x100], buff, 0x20);
    pWepColor->pBoxWnd = WindowEx_NewFromLayout((struct stMobj *)pWepColor, 0, WC_LAYOUT(), (char *)"parts09_0");
    WindowEx_SetFadeIn(pWepColor->pBoxWnd);
    pWepColor->pYajiUp = DF_MOBJ2_NewBlinkSpriteFromLayoutName((struct stMobj *)pWepColor, WC_LAYOUT(), (char *)"up_0", 0, 0);
    pWepColor->pYajiDown =
        DF_MOBJ2_NewBlinkSpriteFromLayoutName((struct stMobj *)pWepColor, WC_LAYOUT(), (char *)"down_0", 0, 0);
    DF_MOBJ_SetDrawEnable(pWepColor->pYajiUp, 0);
    DF_MOBJ_SetDrawEnable(pWepColor->pYajiDown, 0);
    /* colour index = the nCurSel New set (read before MenuScroll_Init) */
    pWepColor->pHakoMdl = DF_MGEN_NewModelEasy(0x1cc, &HAKOMDL_POS, &HAKOMDL_ANG, &HAKOMDL_SCL,
                                               &HAKOMDL_COLOR[pWepColor->Scroll.nCurSel]);
    pWepColor->pHakoMdl->test_reg = 0;
    pWepColor->pHakoMdl->alpha_reg = 0x44;
    pWepColor->pHakoMdl->mulalpha = 1.0f;
    pWepColor->pHakoKageMdl =
        DF_MGEN_NewModelEasy(0x1cd, &HAKOKAGEMDL_POS, &HAKOMDL_ANG, &HAKOMDL_SCL, &HAKOKAGEMDL_COLOR);
    pWepColor->pHakoKageMdl->test_reg = 0;
    pWepColor->pHakoKageMdl->alpha_reg = 0x42;
    pWepColor->pHakoKageMdl->mulalpha = 1.0f;
    MenuScroll_Init(&pWepColor->Scroll);
    pf_sprintf_d(buff, mwepcolor2_fmt_parts10, MenuScroll_GetCurIndexInPage(&pWepColor->Scroll));
    pWepColor->pCursor = WindowEx_NewFromLayout((struct stMobj *)pWepColor, 0xb, WC_LAYOUT(), buff);
    WindowEx_BlinkBegin(pWepColor->pCursor);
    WindowEx_SetFadeIn(pWepColor->pCursor);
    DF_MOBJ_SetDrawPrio(pWepColor->pCursor, 1);
    MoverI_Move(&pWepColor->ModelAlpha, 0x80, 6);
    __WepColor_CheckYaji(pWepColor);
    pWepColor->Base.stat = 1;
    pWepColor->Base.count = 0;
}

/* Cross / Circle: fade everything out, ExitMode says commit (1) or restore (0); Play runs Finish 7 frames later */
static inline void wc_leave(struct stMenuWepColorSelColor *pWepColor, int voice, int mode)
{
    Voice_Call(voice, 0x7f);
    for (unsigned int i = 0; i < 4; i++) {
        struct StSelectLabel *l = WC_LABEL(pWepColor->apLabel[i]);
        if (l) {
            DF_MOBJ_SetFadeOut(l->pString);
            UnitSprite_SetFadeOut(l->pSprite);
        }
    }
    WindowEx_SetFadeOut(pWepColor->pBoxWnd);
    WindowEx_SetFadeOut(pWepColor->pCursor);
    DF_MOBJ2_SetBlinkSpriteFadeOut(pWepColor->pYajiUp);
    DF_MOBJ2_SetBlinkSpriteFadeOut(pWepColor->pYajiDown);
    MoverI_Move(&pWepColor->ModelAlpha, 0, 6);
    pWepColor->ExitMode = (enum_1784990f)mode;
    pWepColor->Base.stat = 2;
    pWepColor->Base.count = 0;
}

static void __WepColor_PlayOnStable(struct stMenuWepColorSelColor *pWepColor)
{
    PS2_FRAME(0x50);
    int ret = 0;
    if (MenuKeyGuide_IsShown()) return;
    struct struct_2d84b07b *pad = &MPad[MPadUser];
    if (pad->start == 1) {
        MenuKeyGuide_Show(0x3f);
        return;
    }
    if (pad->Rrt == 1) {
        wc_leave(pWepColor, 0x3eb, 0);
        return;
    }
    if (pad->Rdw == 1) {
        wc_leave(pWepColor, 0x3ea, 1);
        return;
    }
    if (pad->repeat.up)
        ret = MenuScroll_MoveUp(&pWepColor->Scroll);
    else if (pad->repeat.dw)
        ret = MenuScroll_MoveDown(&pWepColor->Scroll);
    ret &= 0xff;
    if (ret == 0) return;
    Voice_Call(0x3e9, 0x7f);
    if (pWepColor->ctgr == 0) {
        for (int k = 5; k <= 10; k++) pWepColor->pAcColor->p_col[k].wepcol = (unsigned char)pWepColor->Scroll.nCurSel;
        DF_M_AC_UpdateWepColor(pWepColor->pAC, pWepColor->pAcColor, 0);
        DF_M_AC_UpdateWepColor(pWepColor->pAcModel->dt.model.acp, pWepColor->pAcColor, 0);
    } else {
        unsigned int rec = (unsigned char)__GetColorCtgr(pWepColor->ctgr);
        pWepColor->pAcColor->p_col[rec].wepcol = (unsigned char)pWepColor->Scroll.nCurSel;
        DF_M_AC_UpdateWepColor(pWepColor->pAC, pWepColor->pAcColor, 0);
        DF_MMODEL_SetWepColor(pWepColor->pAcModel->dt.model.mp, &pWepColor->pAcColor->p_col[rec]);
        if ((unsigned char)pWepColor->ctgr == 7)
            DF_MMODEL_SetWepColor(pWepColor->pAcModel->dt.model.add_mp[0], &pWepColor->pAcColor->p_col[rec]);
    }
    DF_MMODEL_SetVertColor(pWepColor->pHakoMdl, 0, &HAKOMDL_COLOR[pWepColor->Scroll.nCurSel]);
    if (ret == 2) { /* the page scrolled: hide the cursor until the rows have moved (stat 4) */
        WindowEx_SetFadeOut(pWepColor->pCursor);
        __WepColor_CheckYaji(pWepColor);
        pWepColor->Base.stat = 4;
        pWepColor->Base.count = 0;
    }
}

static void __WepColor_Pkt(struct stMenuWepColorSelColor *pWepColor)
{
    PS2_FRAME(0x20);
    unsigned int *dma = DF_MPKT_MakeVIFCode(MPktCurrentp, 0, 0, 0, 0x13000000);
    dma = DF_MMODEL_MakeVu1PacketAC(dma, pWepColor->pHakoMdl, 0, 6);
    dma = DF_MMODEL_MakeVu1PacketAC(dma, pWepColor->pHakoKageMdl, 0, 2);
    MPktCurrentp = dma;
}

static void __WepColor_Draw(void)
{
}

/* stat 0 input, 1 entering (7 frames), 2 leaving (Finish after 7 frames), 4 cursor hidden while the page scrolls */
static void __WepColor_Play(struct stMenuWepColorSelColor *pWepColor)
{
    PS2_FRAME(0x20);
    MoverI_Update(&pWepColor->ModelAlpha);
    float alpha = (float)pWepColor->ModelAlpha.Cur / 128.0f;
    pWepColor->pHakoMdl->mulalpha = alpha;
    pWepColor->pHakoKageMdl->mulalpha = alpha;
    DF_MMODEL_Play(pWepColor->pHakoMdl);
    DF_MMODEL_Play(pWepColor->pHakoKageMdl);
    switch (pWepColor->Base.stat) {
    case 0:
        __WepColor_PlayOnStable(pWepColor);
        break;
    case 1:
        pWepColor->Base.count++;
        if (pWepColor->Base.count >= 7) pWepColor->Base.stat = 0;
        break;
    case 2:
        pWepColor->Base.count++;
        if (pWepColor->Base.count >= 7) __WepColor_Finish(pWepColor);
        break;
    case 4:
        pWepColor->Base.count++;
        if (pWepColor->Base.count >= 7) {
            WindowEx_SetFadeIn(pWepColor->pCursor);
            pWepColor->Base.stat = 0;
            pWepColor->Base.count = 0;
        }
        break;
    }
}

struct stMobj *MenuWepColorSelColor_New(struct stMobj *pParent, _WEPCOLOR_CTGR ctgr, struct stMenuAC *pAc,
                                        struct stMobj *pModel)
{
    PS2_FRAME(0x50);
    struct stMenuWepColorSelColor *p = (struct stMenuWepColorSelColor *)DF_MOBJ_GetNewObject(pParent);
    p->Base.play_func = (void (*)(struct stMobj *))GFN(__WepColor_Play);
    p->Base.draw_func = (void (*)(struct stMobj *))GFN(__WepColor_Draw);
    p->Base.pkt_func = (void (*)(struct stMobj *))GFN(__WepColor_Pkt);
    p->pAcModel = pModel;
    p->ctgr = ctgr;
    p->pAC = pAc;
    p->pAcColor = (struct struct_098975f3 *)DF_MMM_Alloc(0, 0xb0);
    p->pAcColorRestore = (struct struct_098975f3 *)DF_MMM_Alloc(0, 0xb0);
    memcpy(p->pAcColor, &p->pAC->real_col, 0xb0);
    memcpy(p->pAcColorRestore, p->pAcColor, 0xb0);
    DF_M_AC_UpdateWepColor(p->pAC, p->pAcColor, 0);
    p->Scroll.CbInfo.pCbParam = p;
    p->Scroll.CbInfo.OnCreateItem = (void (*)(void *, int, int))GFN(__WepColor_OnCreateItem);
    p->Scroll.CbInfo.OnMoveCursor = (void (*)(void *, int))GFN(__WepColor_OnMoveCursor);
    p->Scroll.CbInfo.OnMoveItem = (void (*)(void *, int, int))GFN(__WepColor_OnMoveItem);
    p->Scroll.CbInfo.OnMoveItemFadeIn = (void (*)(void *, int, int))GFN(__WepColor_OnMoveItemFadeIn);
    p->Scroll.CbInfo.OnMoveItemFadeOut = (void (*)(void *, int, int))GFN(__WepColor_OnMoveItemFadeOut);
    p->Scroll.nMaxItem = 6;
    p->Scroll.nMaxItemInPage = 4;
    struct struct_c4910010 *rec;
    switch ((unsigned char)p->ctgr) { /* the same record as __GetColorCtgr */
    case 1: rec = &p->pAcColor->p_col[5]; break;
    case 2: rec = &p->pAcColor->p_col[6]; break;
    case 3: rec = &p->pAcColor->p_col[7]; break;
    case 4: rec = &p->pAcColor->p_col[8]; break;
    case 5: rec = &p->pAcColor->p_col[9]; break;
    case 6: rec = &p->pAcColor->p_col[10]; break;
    case 7: rec = &p->pAcColor->p_col[9]; break;
    default: rec = &p->pAcColor->p_col[5]; break;
    }
    p->Scroll.nCurSel = rec->wepcol;
    DF_M1HELP_ChangeStringId(0x18aaa);
    MoverI_Init(&p->ModelAlpha);
    MoverI_SetValue(&p->ModelAlpha, 0);
    if (p->ctgr == 0) {
        for (int k = 5; k <= 10; k++) p->pAcColor->p_col[k].wepcol = (unsigned char)p->Scroll.nCurSel;
        DF_M_AC_UpdateWepColor(p->pAC, p->pAcColor, 0);
        DF_M_AC_UpdateWepColor(p->pAcModel->dt.model.acp, p->pAcColor, 0);
        /* pHakoMdl is still NULL here (Enter builds it): DF_MMODEL_SetVertColor returns at once */
        DF_MMODEL_SetVertColor(p->pHakoMdl, 0, &HAKOMDL_COLOR[p->Scroll.nCurSel]);
    }
    __WepColor_Enter(p);
    return (struct stMobj *)p;
}
