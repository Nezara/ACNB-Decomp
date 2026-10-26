/* src/menu/mscroll.cpp — scrolling list state (OLMENU): selection, first visible item, item / page counts. The
 * owning screen draws the items through the callbacks in CbInfo (create an item in a slot, move an item between
 * slots, fade one out / in, move the cursor). Used by the weapon colour screen. */

int MenuScroll_IsPageDownEnable(struct _MENUSCROOL *pScroll)
{
    return pScroll->nPageTop + pScroll->nMaxItemInPage < pScroll->nMaxItem;
}

int MenuScroll_IsPageUpEnable(struct _MENUSCROOL *pScroll)
{
    return 0 < pScroll->nPageTop;
}

int MenuScroll_GetCurIndexInPage(struct _MENUSCROOL *pScroll)
{
    return pScroll->nCurSel - pScroll->nPageTop;
}

enum_d1a03b41 MenuScroll_MoveDown(struct _MENUSCROOL *pScroll)
{
    struct _MENUSCROLL_CBINFO *cb = &pScroll->CbInfo;
    int end, moved, i;

    PS2_FRAME(0x30);
    end = pScroll->nPageTop + pScroll->nMaxItemInPage;
    if (pScroll->nCurSel != end - 1) {
        pScroll->nCurSel++;
        gcall(cb->OnMoveCursor, cb->pCbParam, pScroll->nCurSel - pScroll->nPageTop);
        return MENUSCROLL_RET_MOVE;
    }
    /* cursor on the last visible slot: scroll the page by one item */
    moved = 0;
    if (end < pScroll->nMaxItem) {
        pScroll->nPageTop++;
        gcall(cb->OnMoveItemFadeOut, cb->pCbParam, 0, -1);
        for (i = 0; i < pScroll->nMaxItemInPage - 1; i++) gcall(cb->OnMoveItem, cb->pCbParam, i + 1, i);
        gcall(cb->OnCreateItem, cb->pCbParam, pScroll->nPageTop + pScroll->nMaxItemInPage - 1, pScroll->nMaxItemInPage - 1);
        gcall(cb->OnMoveItemFadeIn, cb->pCbParam, pScroll->nMaxItemInPage - 1, pScroll->nMaxItemInPage);
        moved = 1;
    }
    pScroll->nCurSel = pScroll->nPageTop + pScroll->nMaxItemInPage - 1;
    return moved ? MENUSCROLL_RET_PAGEMOVE : MENUSCROLL_RET_NOMOVE;
}

enum_d1a03b41 MenuScroll_MoveUp(struct _MENUSCROOL *pScroll)
{
    struct _MENUSCROLL_CBINFO *cb = &pScroll->CbInfo;
    int moved, i;

    PS2_FRAME(0x30);
    if (pScroll->nCurSel != pScroll->nPageTop) {
        pScroll->nCurSel--;
        gcall(cb->OnMoveCursor, cb->pCbParam, pScroll->nCurSel - pScroll->nPageTop);
        return MENUSCROLL_RET_MOVE;
    }
    /* cursor on the first visible slot: scroll the page back by one item */
    moved = 0;
    if (pScroll->nPageTop != 0) {
        pScroll->nPageTop--;
        gcall(cb->OnMoveItemFadeOut, cb->pCbParam, pScroll->nMaxItemInPage - 1, pScroll->nMaxItemInPage);
        for (i = pScroll->nMaxItemInPage - 2; i >= 0; i--) gcall(cb->OnMoveItem, cb->pCbParam, i, i + 1);
        gcall(cb->OnCreateItem, cb->pCbParam, pScroll->nPageTop, 0);
        gcall(cb->OnMoveItemFadeIn, cb->pCbParam, 0, -1);
        moved = 1;
    }
    pScroll->nCurSel = pScroll->nPageTop;
    return moved ? MENUSCROLL_RET_PAGEMOVE : MENUSCROLL_RET_NOMOVE;
}

/* Clamps selection and page top (the page top can end up negative when there are fewer items than slots), keeps
 * the selection visible and creates the visible items. */
void MenuScroll_Init(struct _MENUSCROOL *pScroll)
{
    struct _MENUSCROLL_CBINFO *cb = &pScroll->CbInfo;
    int n, i;

    PS2_FRAME(0x40);
    n = pScroll->nMaxItemInPage;
    if (pScroll->nMaxItem < n) n = pScroll->nMaxItem;
    if (pScroll->nCurSel < 0) pScroll->nCurSel = 0;
    else if (pScroll->nMaxItem - 1 < pScroll->nCurSel) pScroll->nCurSel = pScroll->nMaxItem - 1;
    if (pScroll->nPageTop < 0) pScroll->nPageTop = 0;
    else if (pScroll->nMaxItem - pScroll->nMaxItemInPage < pScroll->nPageTop)
        pScroll->nPageTop = pScroll->nMaxItem - pScroll->nMaxItemInPage;
    if (pScroll->nPageTop < pScroll->nCurSel - pScroll->nMaxItemInPage + 1)
        pScroll->nPageTop = pScroll->nCurSel - pScroll->nMaxItemInPage + 1;
    else if (pScroll->nCurSel + pScroll->nMaxItemInPage - 1 < pScroll->nPageTop)
        pScroll->nPageTop = pScroll->nCurSel + pScroll->nMaxItemInPage - 1;
    for (i = 0; i < n; i++) gcall(cb->OnCreateItem, cb->pCbParam, pScroll->nPageTop + i, i);
}
