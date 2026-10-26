/* src/menu/mbguide.cpp - the menu's key-guide strip (OLMENU): the row of button prompts at the bottom of every
 * garage / Versus screen. Up to 8 "items" (struct_811c36fb: a kind -> 1-2 button icons + a text label) are drawn
 * right-aligned at y = 0x10 (label at y = 0x11) from the single global MBGMng.
 * MBGMng holds item[8] (the new row), fo_item[8] (a snapshot of the previous row, fading out), the requested
 * kind[8] (right-aligned), stat (0 idle / 1 animating) and count. __DF_MBGUIDE_Change builds the new row from the
 * 17-entry kind table, snapshots the old row, assigns the target x positions (__SetItemsNextX) and starts a
 * 5-frame slide / cross-fade; DF_MBGUIDE_Play runs the interpolation once per frame (__MENU_MainLoop),
 * DF_MBGUIDE_Draw draws fo_item with alpha (0x80 - a) and item with alpha a; DF_MBGUIDE_Init clears MBGMng.
 * In the retail build the exported DF_MBGUIDE_Change is an EMPTY STUB that returns 0, so the ~125 "key list"
 * requests from masm / mcockpit / mcolor / mname / mvss / mvsr ... do nothing: the only live entry points are
 * DF_MBGUIDE_Show (kind 0x10 = one button of kind 14, string 0x186ca) and DF_MBGUIDE_Hide (empty row), which tail
 * call the real static __DF_MBGUIDE_Change. DF_MBGUIDE_Pop is dead code (nothing ever writes kindSaveIdx).
 * Original quirks kept (marked (sic)). */
#include <string.h>
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
struct struct_e7f04089 MBGMng;
#endif

/* kind -> {kind, btn_num, btn_kind[2], str_id}: the 17-entry, 8-byte-per-entry table at OLMENU 0x1edbe50.
 * The original copies the whole table onto its stack and indexes the copy; the index is never range-checked
 * (nk > 16 read stack garbage - dead: only kinds 0 and 0x10 ever arrive). */
static const struct struct_ae9f218b s_kind_tbl[17] = {
    { 0,  0, { 0,  0},       0 },
    { 1,  1, { 6,  0}, 0x186b5 },
    { 2,  1, { 5,  0}, 0x186b6 },
    { 3,  1, { 7,  0}, 0x186b7 },
    { 4,  2, { 9, 11}, 0x186b8 },
    { 5,  2, { 9, 11}, 0x186b9 },
    { 6,  2, { 9, 11}, 0x186ba },
    { 7,  1, { 8,  0}, 0x186bb },
    { 8,  2, {10, 12}, 0x186bc },
    { 9,  1, {11,  0}, 0x186bd },
    {10,  1, { 9,  0}, 0x186be },
    {11,  1, { 6,  0}, 0x186bf },
    {12,  1, { 7,  0}, 0x186c0 },
    {13,  1, { 8,  0}, 0x186c1 },
    {14,  2, { 5,  6}, 0x186b6 },
    {15,  1, { 8,  0}, 0x186c7 },
    {16,  1, {14,  0}, 0x186ca },
};

/* button colours by button kind, OLMENU 0x1edbee0: 16 rows of 4 RGBA, only [0] of each row is set (sic) */
static const struct struct_76b810ec s_col_tbl[16][4] = {
    {{0x80, 0x80, 0x80, 0x50}},
    {{0x80, 0x80, 0x80, 0x50}},
    {{0x80, 0x80, 0x80, 0x50}},
    {{0x80, 0x80, 0x80, 0x50}},
    {{0x80, 0x80, 0x80, 0x50}},
    {{0xe0, 0x5b, 0x10, 0x50}},
    {{0x10, 0x75, 0xe2, 0x50}},
    {{0x00, 0xc5, 0x47, 0x50}},
    {{0xc1, 0x31, 0xa8, 0x50}},
    {{0x80, 0x80, 0x80, 0x50}},
    {{0x80, 0x80, 0x80, 0x50}},
    {{0x80, 0x80, 0x80, 0x50}},
    {{0x80, 0x80, 0x80, 0x50}},
    {{0x80, 0x80, 0x80, 0x50}},
    {{0x80, 0x80, 0x80, 0x50}},
    {{0x80, 0x80, 0x80, 0x50}},
};

/* the button sprite's uv quad, OLMENU 0x1edbfe0 */
static const int s_uv[4] = { 0x17, 0xa1, 0x3b, 0xaf };

/* gp words: 0x27b524 = 0x808d8672 (the label colour; a is overwritten by the alpha) and 0x27b520 = 0x80808080
 * (`col`, written to the stack and never read) */
static const struct struct_76b810ec s_f_col = { 0x72, 0x86, 0x8d, 0x80 };
static const struct struct_76b810ec s_col = { 0x80, 0x80, 0x80, 0x80 };

/* copy the requested kind row out for the callers (masm / mgarage / membe keep it in their own int[8]) */
void DF_MBGUIDE_GetKind(int *outKindp)
{
    int i;

    for (i = 0; i < 8; i++) outKindp[i] = (signed char)MBGMng.kind[i];   /* lb: sign-extended */
}

/* one strip item: its buttons (texture 0x3ec) and then its label. inAlpha is the row alpha, overridden by the
 * item's own stat (4 = hidden, 5 = settled at full alpha). */
static void __DrawItem(struct struct_811c36fb *inItemp, int inAlpha)
{
    PS2_FRAME(0x3b0);
    PS2_STACK(int[4], uvtmp, 0x40);                  /* the lq staging slot of the uv copy (never read back) */
    PS2_STACK(struct_13efd7f4, texinfo, 0x50);
    PS2_STACK(struct_76b810ec[16][4], Col, 0x60);
    PS2_STACK(struct stMenuRect, rect, 0x160);
    PS2_STACK(int[4], uv, 0x170);
    PS2_STACK(struct_acb9a91f, attr, 0x180);
    PS2_STACK(struct_76b810ec, f_col, 0x3a8);
    PS2_STACK(struct_76b810ec, col, 0x3ac);
    int i;
    int alpha;

    col = s_col;                                     /* (sic) dead: nothing ever reads `col` */
    f_col = s_f_col;
    memcpy(&Col, s_col_tbl, sizeof(Col));
    MenuTex_StandbyTexture(0x3ec, &texinfo);         /* (sic) result ignored: texinfo stays stale on failure */
    rect.x = inItemp->x;
    rect.y = 0x10;
    rect.w = 0x24;
    rect.h = 0xe;
    alpha = inAlpha;
    if (inItemp->stat == 4) alpha = 0;
    else if (inItemp->stat == 5) alpha = 0x80;
    col.a = (unsigned char)alpha;                    /* (sic) dead */
    f_col.a = (unsigned char)alpha;
    DF_MDRAW_KickReg(0x42, 0x48);                    /* GS ALPHA_1 */
    for (i = 0; i < inItemp->info.btn_num; i++) {
        memcpy(&uvtmp, s_uv, sizeof(uvtmp));
        memcpy(&uv, &uvtmp, sizeof(uv));
        DF_MDRAW_DrawTexSprite(&texinfo, &rect, uv, &Col[(int)inItemp->info.btn_kind[i]][0], 0);
        rect.x += 0x24;
    }
    DF_MFUNC_ClearMemory(&attr, 0x8a);               /* words: the whole 0x228-byte font attr */
    attr.y = 0x11;
    attr.x = (short)(rect.x + 4);
    attr.strp = DF_MSTR_GetName(MenuMng.language, 0, inItemp->info.str_id);
    attr.col = f_col;
    DF_MFONT_SetFontAttr(&attr, 0);
    DF_MFONT_Draw(&attr, 0);
}

/* every menu frame from __MENU_MainLoop: the outgoing row first (alpha 0x80 - a), then the incoming row (alpha a) */
void DF_MBGUIDE_Draw(void)
{
    PS2_FRAME(0x50);
    int i;
    int alpha;
    int fo_alpha;

    if (MBGMng.stat == 1) {
        int a = (MBGMng.count << 7) / 5;
        alpha = a;
        fo_alpha = 0x80 - a;
    } else {
        alpha = 0x80;
        fo_alpha = 0;
    }
    DF_MDRAW_KickReg(0x47, 0x30003);                 /* GS TEST_1 */
    if (fo_alpha != 0) {
        for (i = 0; i < 8; i++) {
            if (MBGMng.fo_item[i].info.kind != 0 && MBGMng.fo_item[i].stat != 4)
                __DrawItem(&MBGMng.fo_item[i], fo_alpha);
        }
    }
    for (i = 0; i < 8; i++) {
        if (MBGMng.item[i].info.kind != 0)
            __DrawItem(&MBGMng.item[i], alpha);
    }
}

/* one animation step (once per frame): counts 1..5 interpolate every slot's x, the 6th call snaps to next_x and
 * clears stat */
void DF_MBGUIDE_Play(void)
{
    PS2_FRAME(0x40);
    int i;
    int old;

    if (MBGMng.stat == 0) return;
    old = MBGMng.count;
    MBGMng.count = (short)(old + 1);
    if (old < 5) {
        for (i = 0; i < 8; i++) {
            struct struct_811c36fb *fo = &MBGMng.fo_item[i];
            if (fo->info.kind != 0 && fo->stat != 4)
                fo->x = (short)DF_MFUNC_CalcInterValue(fo->pre_x, fo->next_x, MBGMng.count, 5);
        }
        for (i = 0; i < 8; i++) {
            struct struct_811c36fb *it = &MBGMng.item[i];
            if (it->info.kind != 0)
                it->x = (short)DF_MFUNC_CalcInterValue(it->pre_x, it->next_x, MBGMng.count, 5);
        }
    } else {
        for (i = 0; i < 8; i++) {                    /* the compiler unrolled this, one register per slot */
            MBGMng.fo_item[i].x = MBGMng.fo_item[i].next_x;
            MBGMng.fo_item[i].stat = 5;
            MBGMng.item[i].x = MBGMng.item[i].next_x;
            MBGMng.item[i].stat = 0;
        }
        MBGMng.stat = 0;
    }
}

/* callers test Sync(0) == 0 as "the strip is still animating, wait"; Sync(1) busy-runs Play to the end */
int DF_MBGUIDE_Sync(int inMode)
{
    PS2_FRAME(0x10);

    if (MBGMng.stat == 0) return 1;
    if (inMode == 0) return 0;
    do {
        DF_MBGUIDE_Play();
    } while (MBGMng.stat != 0);
    return 1;
}

/* right-align the new row at x = 640 and give every slot its from / to x: an incoming item starts at the old
 * item's x (or off-screen at 640 when the slot was empty), the outgoing copy stays where it is */
static void __SetItemsNextX(void)
{
    int i;
    int x = 0x280;

    for (i = 0; i < 8; i++) {
        if (MBGMng.item[i].info.kind != 0) x -= MBGMng.item[i].w;
    }
    for (i = 0; i < 8; i++) {
        struct struct_811c36fb *it = &MBGMng.item[i];
        struct struct_811c36fb *fo = &MBGMng.fo_item[i];
        if (it->info.kind != 0) {
            if (fo->info.kind == 0) it->x = 0x280;
            else it->x = fo->x;
            it->pre_x = it->x;
            it->next_x = (short)x;
        }
        fo->pre_x = fo->x;
        fo->next_x = fo->x;
        if (it->info.kind == fo->info.kind) {
            fo->stat = 4;
            it->stat = 5;
        } else {
            it->stat = 2;
            fo->stat = 3;
        }
        x += it->w;
    }
}

/* the label width of one slot: the string width plus 36 px per button plus 0x10 padding, every step truncated
 * to 8 bits by the store into the u8 `w` (sic: a label wider than 255 px wraps) */
static void mbg_set_item_width(struct struct_811c36fb *it)
{
    it->w = (unsigned char)DF_MFONT_GetStringWSize(DF_MSTR_GetName(MenuMng.language, 0, it->info.str_id), 0, 0);
    it->w = (unsigned char)(it->w + (unsigned char)(it->info.btn_num * 36));
    it->w = (unsigned char)(it->w + 0x10);
}

/* the real key-guide change: in the retail build it is reached only by the `j` tail calls in DF_MBGUIDE_Hide
 * (inKind0 = 0x63, the terminator = an empty row) and DF_MBGUIDE_Show (inKind0 = 0x10, a1 = 0x63), so the
 * original's hand-rolled varargs list is always 0 or 1 kinds long and the DWARF signature (int inKind0) is exact. */
static int __DF_MBGUIDE_Change(int inKind0)
{
    PS2_FRAME(0x210);
    PS2_STACK(int[9], in_kind, 0x190);
    PS2_STACK(int, arg0, 0x1c8);                     /* the prologue's a0 spill (the varargs `ap` base) */
    int n;
    int i;
    int changed;

    arg0 = inKind0;
    if (MBGMng.stat == 1) return -1;                 /* (sic) a request during the 5-frame animation is dropped */
    changed = 0;
    n = 0;
    if (arg0 != 0x63) {
        in_kind[0] = arg0;
        n = 1;
        in_kind[1] = 0x63;                           /* the a1 spill slot the terminator loop reads */
    }
    for (i = 0; i < 8 - n; i++) MBGMng.kind[i] = 0;
    for (i = 0; i < n; i++) MBGMng.kind[8 - n + i] = (char)in_kind[i];
    for (i = 0; i < 8; i++) MBGMng.fo_item[i] = MBGMng.item[i];   /* snapshot, even when nothing changes (sic) */
    for (i = 0; i < 8; i++) {
        struct struct_811c36fb *it = &MBGMng.item[i];
        struct struct_811c36fb *fo = &MBGMng.fo_item[i];
        int nk = MBGMng.kind[i];
        int fo_was_used;

        if (it->info.kind == nk) continue;
        changed = 1;
        fo_was_used = (fo->info.kind != 0);
        if (!fo_was_used && nk == 0) continue;       /* empty slot stays empty */
        it->info = s_kind_tbl[nk];
        if (it->info.kind == 0) it->w = 0;
        else mbg_set_item_width(it);
        if (fo_was_used && nk != 0) {                /* the outgoing label fades out without its buttons */
            fo->info.btn_num = 0;
            fo->info.btn_kind[1] = 0;
            fo->info.btn_kind[0] = 0;
        }
    }
    if (changed == 1) {
        __SetItemsNextX();
        MBGMng.count = 0;
        MBGMng.stat = 1;
    } else {
        MBGMng.stat = 0;                             /* (sic) harmless: stat was not 1 here */
    }
    return 1;
}

/* hide the strip (an empty kind list) */
void DF_MBGUIDE_Hide(void)
{
    __DF_MBGUIDE_Change(0x63);
}

/* show the strip's single retail entry: kind 0x10 */
void DF_MBGUIDE_Show(void)
{
    __DF_MBGUIDE_Change(0x10);
}

/* the exported key-list request: an EMPTY STUB in the retail build (sic). ~125 call sites pass up to 8 kinds in
 * registers plus a terminator on the stack; all of it is ignored and the function returns 0. */
int DF_MBGUIDE_Change(void)
{
    PS2_FRAME(0x50);
    return 0;
}

/* dead code: nothing ever writes MBGMng.kindSaveIdx, so the saved-kind stack is always empty (sic) */
void DF_MBGUIDE_Pop(void)
{
    PS2_FRAME(0x20);
    int idx = MBGMng.kindSaveIdx;

    if (idx > 0) {
        MBGMng.kindSaveIdx = idx - 1;
        idx = MBGMng.kindSaveIdx;
        /* Change(kindSave[idx][0..7], 0x63) - the stub ignores every argument */
        (void)MBGMng.kindSave[idx][0];
        DF_MBGUIDE_Change();
    }
}

/* from DF_MENU_Reset: clear the whole 0x210-byte manager (ClearMemory counts words) */
void DF_MBGUIDE_Init(void)
{
    DF_MFUNC_ClearMemory(&MBGMng, 0x84);
}
