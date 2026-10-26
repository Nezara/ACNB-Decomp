/* src/menu/mwepcolor1.cpp - the first screen of the garage Paint -> "weapon colour" row (OLMENU, layout 0x404 / 0x405):
 * a list of which equipment slot to recolour (row 0 = all weapons, ctgr 0; then one row per equipped weapon slot,
 * ctgr 1..4, 5 / 6, or 7 for a W-back weapon), a blinking cursor window "parts10_%d", four "parts08_%d" sprites and a
 * preview model of the AC / the selected weapon (DF_MOBJ_NewModelAC) on the left. Cross spawns the colour picker
 * (MenuWepColorSelColor_New, mwepcolor2) as pChild and waits for it to finish (stat 5); Circle tears the list down
 * and pops the scene label. MenuWepColorSelPos_New is called by __PlayPaintSystem (mpaint.cpp); everything else is
 * reached through the object's play / draw / pkt pointers (DF_MOBJ_Play / Draw / MakeVu1Paket).
 * Whole file native. The rect / colour / sprintf buffers handed to callees live at their original PS2 stack slots;
 * the position / scale tables are the original data (guest addresses: the mover keeps reading them). */
#include <string.h>
#include "acnb/pf.h"
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU rodata 0x1ee6490..0x1ee64ff */
float ACPARTS_POS1[2][4] = {{-9.0f, 0.5f, -1.0f, 0.0f}, {-9.0f, -1.5f, -1.0f, 0.0f}};
float ACPARTS_POS2[2][4] = {{-0x1.366666p+3f, -2.5f, 0x1.6cccccp+2f, 0.0f}, {-0x1.366666p+3f, -5.5f, 0x1.6cccccp+2f, 0.0f}};
float ACPARTS_SCALE[4] = {1.0f, 1.0f, 1.0f, 1.0f};
int s_aCtgrTextId[8] = {0x18b50, 0x18af8, 0x18af9, 0x18afa, 0x18afb, 0x18af6, 0x18af7, 0x18afc};
/* OLMENU rodata 0x1ee4990.. (nodwarf.h) */
char mwepcolor1_parts02_0[10] = "parts02_0";
char mwepcolor1_fmt_parts10[11] = "parts10_%d";
char mwepcolor1_fmt_parts02[11] = "parts02_%d";
char mwepcolor1_fmt_parts08[11] = "parts08_%d";
#endif

#define WS_LAYOUT(id) ((char *)LAYOUT_GetLayoutData(id))
#define WS_CTGR(p) ((p)->abCtgrIndex[(p)->nCurSel])

/* "f_draw = 0, f_erase = 1" (two read-modify-writes of byte 0 in the original) */
static inline void ws_erase(struct stMobj *o)
{
    o->f_draw = 0;
    o->f_erase = 1;
}

/* Circle (0, cancel) / Cross (1, proceed): fade the list out; Play acts on ExitMode 7 frames later */
static void __WepSel_SetFadeOut(struct stMenuWepColorSelPos *pWepSel, enum_9cc3a792 ExitMode)
{
    PS2_FRAME(0x70);
    PS2_STACK(struct stMenuRect, rect, 0x60);
    if ((unsigned char)ExitMode == 1) {
        /* slide the model to the picker position, swap the two sprite pairs, move the other rows off to the left
         * (staggered) and the selected row to the top slot */
        MoverF4_Move(&pWepSel->PartsPos, ACPARTS_POS2[WS_CTGR(pWepSel) != 0], 5);
        for (int i = 0; i < 2; i++) UnitSprite_SetFadeOut(pWepSel->apWndSprite[i]);
        for (int i = 2; i < 4; i++) {
            DF_MOBJ_SetDrawEnable(pWepSel->apWndSprite[i], 1);
            UnitSprite_SetFadeIn(pWepSel->apWndSprite[i]);
        }
        for (int i = 0; i < pWepSel->nMaxSel; i++) {
            if (i == pWepSel->nCurSel) continue;
            struct stMobj *w = pWepSel->apWnd[i];
            WindowEx_GetRect(w, &rect);
            rect.x -= 300;
            WindowEx_Move(w, &rect, 6);
            WindowEx_SetCurMoveCount(w, -(pWepSel->nMaxSel - i - 1) * 2);
        }
        WindowEx_MoveToLayout(pWepSel->apWnd[pWepSel->nCurSel], WS_LAYOUT(0x404), mwepcolor1_parts02_0, 6);
    } else {
        DF_MOBJ_SetFadeOut(pWepSel->pAcMdl);
        for (int i = 0; i < 7; i++)
            if (pWepSel->apWnd[i]) WindowEx_SetFadeOut(pWepSel->apWnd[i]);
        for (int i = 0; i < 2; i++) UnitSprite_SetFadeOut(pWepSel->apWndSprite[i]);
    }
    WindowEx_SetFadeOut(pWepSel->pCursor);
    pWepSel->ExitMode = ExitMode;
    pWepSel->Base.stat = 2;
    pWepSel->Base.count = 0;
}

static void __WepSel_Exit(struct stMenuWepColorSelPos *pWepSel)
{
    PS2_FRAME(0x40);
    for (int i = 0; i < 7; i++) {
        if (pWepSel->apWnd[i]) {
            WindowEx_Delete(pWepSel->apWnd[i]);
            pWepSel->apWnd[i] = 0;
        }
    }
    if (pWepSel->pCursor) {
        WindowEx_Delete(pWepSel->pCursor);
        pWepSel->pCursor = 0;
    }
    /* original: no null test, slots not cleared */
    for (int i = 0; i < 4; i++) UnitSprite_Delete(pWepSel->apWndSprite[i]);
    if (pWepSel->pAcMdl) {
        ws_erase(pWepSel->pAcMdl);
        pWepSel->pAcMdl = 0;
    }
}

static void __WepSel_Enter(struct stMenuWepColorSelPos *pWepSel)
{
    PS2_FRAME(0x260);
    PS2_STACK(char[0x100], buff2, 0x40);
    PS2_STACK(struct stMenuRect, rect, 0x140);
    PS2_STACK(char[0x100], buff, 0x150);
    PS2_STACK(unsigned int[2], apLoHeader, 0x250); /* written, never read */
    PS2_STACK(unsigned int, col, 0x25c);
    memset(apLoHeader, 0, 8);
    apLoHeader[0] = PS2PTR(LAYOUT_GetLayoutData(0x404));
    apLoHeader[1] = PS2PTR(LAYOUT_GetLayoutData(0x405));
    for (int i = 0; i < pWepSel->nMaxSel; i++) {
        pf_sprintf_d(buff, mwepcolor1_fmt_parts02, i);
        pWepSel->apWnd[i] = WindowEx_NewFromLayout((struct stMobj *)pWepSel, 0, WS_LAYOUT(0x404), buff);
        unsigned char *str = DF_MSTR_GetName(MenuMng.language, 0, s_aCtgrTextId[pWepSel->abCtgrIndex[i]]);
        WindowEx_SetString(pWepSel->apWnd[i], (char *)str);
        WindowEx_SetStringPos(pWepSel->apWnd[i], 0x23, 5);
    }
    for (int i = 0; i < 2; i++) { /* newlib sprintf here (not Pf_SPrintf) */
        ps2_sprintf_d(buff, mwepcolor1_fmt_parts08, i);
        pWepSel->apWndSprite[i] =
            UnitSprite_New((struct stMobj *)pWepSel, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x404), buff);
        DF_MOBJ_SetDrawEnable(pWepSel->apWndSprite[i], 1);
        UnitSprite_SetFadeIn(pWepSel->apWndSprite[i]);
    }
    for (int i = 2; i < 4; i++) {
        ps2_sprintf_d(buff, mwepcolor1_fmt_parts08, i - 2);
        pWepSel->apWndSprite[i] =
            UnitSprite_New((struct stMobj *)pWepSel, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x405), buff);
        DF_MOBJ_SetDrawEnable(pWepSel->apWndSprite[i], 0);
    }
    for (unsigned int i = 0; i < 7; i++)
        if (pWepSel->apWnd[i]) WindowEx_SetFadeIn(pWepSel->apWnd[i]);
    rect.x = 0x15;
    rect.y = 0;
    rect.w = 0x110;
    rect.h = 0x14;
    rect.y = pWepSel->nCurSel * 27 + 0x21;
    col = 0x80808080u; /* sdata word (lwc1 / swc1 copy) */
    pWepSel->pCursor = WindowEx_New((struct stMobj *)pWepSel, 0xb, &rect, (struct struct_76b810ec *)&col);
    WindowEx_SetAlphaReg(pWepSel->pCursor, 0x48);
    WindowEx_BlinkBegin(pWepSel->pCursor);
    DF_MOBJ_SetDrawPrio(pWepSel->pCursor, (enum_fedbfd96)1);
    pf_sprintf_d(buff2, mwepcolor1_fmt_parts10, pWepSel->nCurSel);
    WindowEx_MoveToLayout(pWepSel->pCursor, WS_LAYOUT(0x404), buff2, 0);
    __WepSel_MakePartsModel(pWepSel);
    pWepSel->Base.count = 0;
    pWepSel->Base.stat = 1;
}

static void __WepSel_PlayOnStable(struct stMenuWepColorSelPos *pWepSel)
{
    PS2_FRAME(0x230);
    PS2_STACK(char[0x100], buf_dw, 0x30);
    PS2_STACK(char[0x100], buf_up, 0x130);
    int oldidx = pWepSel->nCurSel;
    if (MenuKeyGuide_IsShown()) return;
    struct struct_2d84b07b *pad = &MPad[MPadUser];
    if (pad->start == 1) {
        MenuKeyGuide_Show(0x20);
        return;
    }
    if (pad->Rrt == 1) {
        Voice_Call(0x3eb, 0x7f);
        __WepSel_SetFadeOut(pWepSel, 0);
        return;
    }
    if (pad->Rdw == 1) {
        Voice_Call(0x3ea, 0x7f);
        __WepSel_SetFadeOut(pWepSel, 1);
        return;
    }
    if (pad->repeat.up) {
        Voice_Call(0x3e9, 0x7f);
        pWepSel->nCurSel = (pWepSel->nCurSel + pWepSel->nMaxSel - 1) % pWepSel->nMaxSel;
        pf_sprintf_d(buf_up, mwepcolor1_fmt_parts10, pWepSel->nCurSel);
        WindowEx_MoveToLayout(pWepSel->pCursor, WS_LAYOUT(0x404), buf_up, 0);
    } else if (pad->repeat.dw) {
        Voice_Call(0x3e9, 0x7f);
        pWepSel->nCurSel = (pWepSel->nCurSel + 1) % pWepSel->nMaxSel;
        pf_sprintf_d(buf_dw, mwepcolor1_fmt_parts10, pWepSel->nCurSel);
        WindowEx_MoveToLayout(pWepSel->pCursor, WS_LAYOUT(0x404), buf_dw, 0);
    }
    if (oldidx != pWepSel->nCurSel) __WepSel_MakePartsModel(pWepSel);
}

/* stat 3: wait for the picker (mwepcolor2 sets stat 5 via DF_MOBJ_Finish), then bring the list back */
static void __WepSel_PlayOnDispatch(struct stMenuWepColorSelPos *pWepSel)
{
    PS2_FRAME(0x140);
    PS2_STACK(char[0x100], buff, 0x40);
    struct stMobj *child = pWepSel->pChild;
    if (child->stat != 5) return; /* read before the null test (pChild is never null here) */
    if (child) {
        ws_erase(child);
        pWepSel->pChild = 0;
    }
    MoverF4_Move(&pWepSel->PartsPos, ACPARTS_POS1[WS_CTGR(pWepSel) != 0], 5);
    for (int i = 0; i < 2; i++) {
        DF_MOBJ_SetDrawEnable(pWepSel->apWndSprite[i], 1);
        UnitSprite_SetFadeIn(pWepSel->apWndSprite[i]);
    }
    for (int i = 2; i < 4; i++) UnitSprite_SetFadeOut(pWepSel->apWndSprite[i]);
    for (int i = 0; i < pWepSel->nMaxSel; i++) {
        pf_sprintf_d(buff, mwepcolor1_fmt_parts02, i);
        WindowEx_MoveToLayout(pWepSel->apWnd[i], WS_LAYOUT(0x404), buff, 6);
        if (i != pWepSel->nCurSel) WindowEx_SetCurMoveCount(pWepSel->apWnd[i], -i * 2);
    }
    DF_M1HELP_ChangeStringId(0x18aa9);
    WindowEx_SetFadeIn(pWepSel->pCursor);
    pWepSel->Base.stat = 1;
    pWepSel->Base.count = 0;
}

static void __WepSel_Pkt(void)
{
}

static void __WepSel_Draw(void)
{
}

/* stat 0 input, 1 entering (17 frames), 2 leaving (7 frames, then Exit or the picker), 3 picker running */
static void __WepSel_Play(struct stMenuWepColorSelPos *pWepSel)
{
    PS2_FRAME(0x20);
    switch (pWepSel->Base.stat) {
    case 3:
        __WepSel_PlayOnDispatch(pWepSel);
        break;
    case 2:
        pWepSel->Base.count++;
        if (pWepSel->Base.count >= 7) {
            if (pWepSel->ExitMode == 0) {
                __WepSel_Exit(pWepSel);
                DF_MBACK_PopSceneLabel();
                DF_MOBJ_Finish((struct stMobj *)pWepSel);
            } else {
                pWepSel->pChild = MenuWepColorSelColor_New((struct stMobj *)pWepSel, (_WEPCOLOR_CTGR)WS_CTGR(pWepSel),
                                                           pWepSel->pAC, pWepSel->pAcMdl);
                pWepSel->Base.stat = 3;
            }
            pWepSel->Base.count = 0;
        }
        break;
    case 1:
        pWepSel->Base.count++;
        if (pWepSel->Base.count > 16) {
            pWepSel->Base.stat = 0;
            pWepSel->Base.count = 0;
        }
        break;
    case 0:
        __WepSel_PlayOnStable(pWepSel);
        break;
    }
    MoverF4_Update(&pWepSel->PartsPos);
    if (pWepSel->pAcMdl) DF_MOBJ_SetModelPos(pWepSel->pAcMdl, pWepSel->PartsPos.Cur);
}

/* (re)build the preview: the whole AC for row 0, else the selected weapon part with its colour record */
static void __WepSel_MakePartsModel(struct stMenuWepColorSelPos *pWepSel)
{
    PS2_FRAME(0x70);
    PS2_STACK(struct stMenuRect, rect, 0x60);
    rect.x = 0;
    rect.y = 0;
    rect.w = 0x140;
    rect.h = 0x1e0;
    unsigned int ctgr = WS_CTGR(pWepSel);
    unsigned int idx;
    switch (ctgr) {
    case 0: idx = 0x10; break;
    case 1: idx = 0x10; break;
    case 2: idx = 0x11; break;
    case 3: idx = 0x12; break;
    case 4: idx = 0x13; break;
    case 5: idx = 0xe; break;
    case 6: idx = 0xf; break;
    case 7: idx = 0xe; break;
    default: idx = 0x10; break;
    }
    int part_id = pWepSel->pAC->part_id[idx & 0xff];
    __WepSel_GetPartsColorFromAc(pWepSel);
    if (pWepSel->pAcMdl) {
        ws_erase(pWepSel->pAcMdl);
        pWepSel->pAcMdl = 0;
    }
    if (ctgr == 0)
        pWepSel->pAcMdl = DF_MOBJ_NewModelAC((struct stMobj *)pWepSel, 3, 0, 0, &rect, 0, 0, 0x80, 0x1.921fb6p+1f,
                                             pWepSel->pAC, pWepSel->nPlayer, 0x1c0000, 0.0f, 0.0f);
    else
        pWepSel->pAcMdl = DF_MOBJ_NewModelAC((struct stMobj *)pWepSel, 1, part_id, ctgr == 5, &rect,
                                             &pWepSel->PartsColor, &pWepSel->PartsColor2, 0x80, 0x1.921fb6p+1f,
                                             pWepSel->pAC, pWepSel->nPlayer, 0x140000, 0.0f, -0x1.666666p-1f);
    MoverF4_SetValue(&pWepSel->PartsPos, ACPARTS_POS1[ctgr != 0]);
    DF_MOBJ_SetModelPos(pWepSel->pAcMdl, pWepSel->PartsPos.Cur);
    DF_MOBJ_SetModelScale(pWepSel->pAcMdl, ACPARTS_SCALE);
    DF_MOBJ_SetFadeIn(pWepSel->pAcMdl);
}

/* the AC colour record of the selected row (the same records as mwepcolor2's __GetColorCtgr; W-back: both copies) */
static void __WepSel_GetPartsColorFromAc(struct stMenuWepColorSelPos *pWepSel)
{
    struct struct_c4910010 *col = pWepSel->pAC->real_col.p_col;
    switch (WS_CTGR(pWepSel)) {
    case 0: memcpy(&pWepSel->PartsColor, &col[5], 0x10); break;
    case 1: memcpy(&pWepSel->PartsColor, &col[5], 0x10); break;
    case 2: memcpy(&pWepSel->PartsColor, &col[6], 0x10); break;
    case 3: memcpy(&pWepSel->PartsColor, &col[7], 0x10); break;
    case 4: memcpy(&pWepSel->PartsColor, &col[8], 0x10); break;
    case 5: memcpy(&pWepSel->PartsColor, &col[9], 0x10); break;
    case 6: memcpy(&pWepSel->PartsColor, &col[10], 0x10); break;
    case 7:
        memcpy(&pWepSel->PartsColor, &col[9], 0x10);
        memcpy(&pWepSel->PartsColor2, &pWepSel->pAC->real_col.p_col[9], 0x10);
        break;
    }
}

/* the list rows: row 0 = all weapons, then each equipped weapon slot (part id % 1000 != 0) */
static void __WepSel_CreateIndex(struct stMenuWepColorSelPos *pWepSel)
{
    PS2_FRAME(0x20);
    struct stMenuAC *ac;
    pWepSel->nMaxSel = 0;
    pWepSel->abCtgrIndex[pWepSel->nMaxSel++] = 0;
    ac = pWepSel->pAC;
    if (ac->part_id[0x10] % 1000 != 0) pWepSel->abCtgrIndex[pWepSel->nMaxSel++] = 1;
    if (pWepSel->pAC->part_id[0x11] % 1000 != 0) pWepSel->abCtgrIndex[pWepSel->nMaxSel++] = 2;
    if (pWepSel->pAC->part_id[0x12] % 1000 != 0) pWepSel->abCtgrIndex[pWepSel->nMaxSel++] = 3;
    if (pWepSel->pAC->part_id[0x13] % 1000 != 0) pWepSel->abCtgrIndex[pWepSel->nMaxSel++] = 4;
    if (DF_MMODEL_IsWBackWeapon(pWepSel->pAC->cur_part[15]) == 1 ||
        DF_MMODEL_IsWBackWeapon(pWepSel->pAC->cur_part[14]) == 1) {
        pWepSel->bIsWBackWep = 1;
        pWepSel->abCtgrIndex[pWepSel->nMaxSel++] = 7;
    } else {
        if (pWepSel->pAC->part_id[0xe] % 1000 != 0) pWepSel->abCtgrIndex[pWepSel->nMaxSel++] = 5;
        if (pWepSel->pAC->part_id[0xf] % 1000 != 0) pWepSel->abCtgrIndex[pWepSel->nMaxSel++] = 6;
    }
    __WepSel_GetPartsColorFromAc(pWepSel);
}

struct stMobj *MenuWepColorSelPos_New(struct stMobj *pParent, struct stMenuAC *pAC, int player)
{
    PS2_FRAME(0x40);
    struct stMenuWepColorSelPos *p = (struct stMenuWepColorSelPos *)DF_MOBJ_GetNewObject(pParent);
    p->Base.play_func = (void (*)(struct stMobj *))GFN(__WepSel_Play);
    p->Base.draw_func = (void (*)(struct stMobj *))GFN(__WepSel_Draw);
    p->Base.pkt_func = (void (*)(struct stMobj *))GFN(__WepSel_Pkt);
    p->Base.stat = 1;
    p->nMaxSel = 8;
    p->nCurSel = 0;
    p->nPlayer = player;
    p->pAC = pAC;
    MoverF4_Init(&p->PartsPos);
    MoverF4_SetValue(&p->PartsPos, ACPARTS_POS1[WS_CTGR(p) != 0]); /* abCtgrIndex[0] is still 0 here */
    DF_M1HELP_SetWindow(); /* the original passes (0, 0x280); the function is an empty stub */
    DF_M1HELP_ChangeStringId(0x18aa9);
    DF_MVIEW_MotionStart(MViewp, 0x1e);
    DF_MBACK_PushSceneLabel((enum_5124d09c)0x24);
    __WepSel_CreateIndex(p);
    __WepSel_Enter(p);
    return (struct stMobj *)p;
}
