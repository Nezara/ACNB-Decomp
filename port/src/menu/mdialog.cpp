/* src/menu/mdialog.cpp — the menu's modal message / confirm dialog (OLMENU): "OK", "OK / CANCEL", shop buy / sell and
 * BGM select boxes (HDD error screen, Versus setup, arena, MenuEth, memory-card loaders ...). One global DlgMng holds
 * the dialog: DF_MDIALOG_Open fills it and starts the open tween, DF_MDIALOG_Play runs the current state function
 * (__Play_Opening -> __Play_Stable (pad input) -> __Play_Closing, which leaves the result in DlgMng.sync: 4 = done,
 * 0x14 / 0x24 = done with button 0 / 1), DF_MDIALOG_Draw draws it every frame. Accepting a sell / buy dialog with
 * button 0 performs the trade (__Sell / BuyNx) before the close.
 * Drawing: DF_MDIALOG_DrawDialogWnd (9-slice window from texture 0x3f4, also mobj's window type 0x66), DF_MDIALOG_Draw
 * (title, underline, text, then the OK / CANCEL, BGM select or shop rows), all through DF_MFONT_Draw / DF_MDRAW_*.
 * The fonts and rects live at their original PS2 stack offsets (the OK / CANCEL fonts are only partly set: the rest
 * is stale stack). */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
struct struct_b8befac7 DlgMng;
char mdialog_chara_line[30] = "_____________________________";
char mdialog_fmt_ld[4] = "%ld";
#endif

/* the shop / sell record the caller hangs on DlgMng.datap after Open (DMM kind 2 block, freed by __Play_Closing) */
struct __attribute__((packed, aligned(4))) mdlg_shop_rec {
    unsigned char *label[6]; /* 0x00: row labels (0, 1, 2) and three extra strings (sell dialog) */
    int price;               /* 0x18 */
    int value;               /* 0x1c */
    int player;              /* 0x20 */
    int acpeq;               /* 0x24 */
    int part_id;             /* 0x28 */
    int unused;              /* 0x2c */
    short x, y;              /* 0x30 */
    unsigned int result;     /* 0x34: __Sell's flags, or-ed into DlgMng.sync by __Play_Closing */
};
static_assert(__builtin_offsetof(mdlg_shop_rec, result) == 0x34, "mdlg_shop_rec");

/* the close sequence (DF_MDIALOG_Close and __Play_Stable write it out in full) */
static inline void mdialog_begin_close(void)
{
    DlgMng.prev_rect = DlgMng.cur_rect;
    DF_MFUNC_GetFadeOutRect(&DlgMng.next_rect, &DlgMng.prev_rect);
    DlgMng.count = 0;
    DlgMng.sync = 3;
    DlgMng.play_func = (void (*)(void))GFN(__Play_Closing);
    Pad_ClearData();
}

void DF_MDIALOG_SetBtnOff(char flagOff)
{
    DlgMng.f_btnoff = flagOff;
}

/* a second Open while a dialog is up does nothing; inRectp NULL = size the box from the text; inX / inY <= 0 = centre
 * the text horizontally / put it 0x19 below the top */
void DF_MDIALOG_Open(int inType, unsigned char *inBuff, struct stMenuRect *inRectp, int inX, int inY, int inPos,
                     int inIdx, int inFBtnOff)
{
    PS2_FRAME(0x50);
    if (DlgMng.type != 0) return;
    DlgMng.type = inType;
    DlgMng.str_textp = inBuff;
    DlgMng.pos_type = (char)inPos;
    DlgMng.f_btnoff = inFBtnOff & 1;
    DlgMng.chara_linep = (unsigned char *)mdialog_chara_line;
    switch (DlgMng.type) {
    case 1: DlgMng.str_typep = DF_MSTR_GetName(MenuMng.language, 0, 0x186aa); break;
    case 2: DlgMng.str_typep = DF_MSTR_GetName(MenuMng.language, 0, 0x186ab); break;
    case 3: DlgMng.str_typep = DF_MSTR_GetName(MenuMng.language, 0, 0x186ac); break;
    case 4: DlgMng.str_typep = DF_MSTR_GetName(MenuMng.language, 0, 0x186ad); break;
    case 5: DlgMng.str_typep = DF_MSTR_GetName(MenuMng.language, 0, 0x186ae); break;
    case 9: DlgMng.str_typep = DF_MSTR_GetName(MenuMng.language, 0, 0x186ab); break;
    }
    if (inRectp) DlgMng.next_rect = *inRectp;
    else __CalcRectFormText(&DlgMng.next_rect);
    DF_MFUNC_GetFadeInRect(&DlgMng.prev_rect, &DlgMng.next_rect);
    if (inX <= 0) inX = DlgMng.next_rect.x + (DlgMng.next_rect.w - 9 * DF_MFONT_GetWidth(DlgMng.str_textp)) / 2;
    if (inY <= 0) inY = DlgMng.next_rect.y + 0x19;
    DlgMng.text_x = (short)inX;
    DlgMng.text_y = (short)inY;
    DlgMng.idx = (char)inIdx;
    DlgMng.sync = 1;
    DlgMng.play_func = (void (*)(void))GFN(__Play_Opening);
    DlgMng.count = 0;
    DlgMng.step = 0;
    DlgMng.type_x = (short)(DlgMng.next_rect.x + 8);
    DlgMng.type_y = (short)(DlgMng.next_rect.y + 5);
    Voice_Call(DlgMng.type == 5 ? 0x3f6 : 0x3f3, 0x7f);
}

/* only from the stable state */
void DF_MDIALOG_Close(void)
{
    PS2_FRAME(0x10);
    if (DlgMng.sync == 2) mdialog_begin_close();
}

void DF_MDIALOG_Play(void)
{
    PS2_FRAME(0x10);
    if (DlgMng.type != 0 && DlgMng.play_func) gcall(DlgMng.play_func);
}

/* replaces the text of an open (stable) dialog and re-sizes it at once */
void DF_MDIALOG_Set(unsigned char *inBuff, int inPos)
{
    PS2_FRAME(0x10);
    if (DlgMng.sync != 2) return;
    DlgMng.pos_type = (char)inPos;
    DlgMng.str_textp = inBuff;
    __CalcRectFormText(&DlgMng.cur_rect);
    DlgMng.type_x = (short)(DlgMng.cur_rect.x + 8);
    DlgMng.type_y = (short)(DlgMng.cur_rect.y + 5);
    DlgMng.text_x = (short)(DlgMng.cur_rect.x + 0x12);
    DlgMng.text_y = (short)(DlgMng.cur_rect.y + 0x19);
}

/* opening / stable / closing; a result sync (4, 0x13 while closing after an accept, 0x14 ...) is not "playing" */
int DF_MDIALOG_IsPlay(void)
{
    return DlgMng.sync == 1 || DlgMng.sync == 2 || DlgMng.sync == 3;
}

int DF_MDIALOG_Type(void)
{
    return DlgMng.type;
}

int DF_MDIALOG_Sync(void)
{
    return DlgMng.sync;
}

void DF_MDIALOG_MakeVu1Paket(void)
{
}

/* sells the record's part: money (capped at 99,999,999), stock, and the part is taken off each of the player's three
 * menu ACs that still equip it; result flags: 0x800 << slot (RemovePart bit 2), 0x100 << ac->num (bit 9) */
static void __Sell(void)
{
    PS2_FRAME(0x80);
    struct mdlg_shop_rec *d = (struct mdlg_shop_rec *)DlgMng.datap;
    unsigned int r = 0; /* $s0: only assigned when a removal runs, a skipped slot re-applies the previous flags */
    int q = d->part_id / 1000, rem = d->part_id % 1000;

    g_gamedata[d->player].money += d->price;
    if (g_gamedata[d->player].money >= 100000000) g_gamedata[d->player].money = 99999999;
    DF_MPART_SellNx(d->player, d->part_id, d->acpeq);
    int full = DF_MPART_Index2TrueID(q, rem) + q * 1000;
    int cnt = DF_MPART_GetPartCountNx(0, d->player, full, d->acpeq);
    int back_r = d->acpeq == 0xb;
    d->result = 0;
    for (int slot = 0; slot < 3; slot++) {
        struct stMenuAC *ac = M_ACp[d->player][slot];
        if (!DF_M_AC_IsEquipPart3(ac, d->part_id)) continue;
        if (d->part_id / 1000 == 10) {
            /* back weapon: only when the last one is sold and the slot holds exactly this id */
            int hi = back_r ? 0xf : 0xe, flag = back_r;
            if (ac->connect.cur.flag.wback) { hi = 0xe; flag = 0; }
            if (cnt == 0 && ac->part_id[hi] == d->part_id) r = DF_M_AC_RemovePart(ac, d->part_id, flag);
        } else {
            r = DF_M_AC_RemovePart(ac, d->part_id, back_r);
        }
        if (r) {
            if (r & 4) d->result |= 0x800u << slot;
            if (r & 0x200) d->result |= 0x100u << M_ACp[d->player][slot]->num;
        }
    }
}

/* 3-frame close tween, then the result: OK / CANCEL, sell and buy dialogs report the button (0x14 / 0x24, 4 when
 * cancelled), a sell adds __Sell's flags; the shop record is freed */
static void __Play_Closing(void)
{
    PS2_FRAME(0x10);
    if (DlgMng.count++ < 3) {
        DF_MFUNC_CalcInterRect(&DlgMng.cur_rect, &DlgMng.prev_rect, &DlgMng.next_rect, DlgMng.count, 3);
        return;
    }
    DlgMng.cur_rect = DlgMng.next_rect;
    switch (DlgMng.type) {
    case 7:
    case 6:
        DlgMng.sync = DlgMng.idx == 1 ? 0x24 : DlgMng.idx == 0 ? 0x14 : 4;
        if (DlgMng.type == 6) DlgMng.sync |= ((struct mdlg_shop_rec *)DlgMng.datap)->result;
        DF_MMM_Free(2, DlgMng.datap);
        DlgMng.datap = 0;
        break;
    case 9:
    case 2:
        DlgMng.sync = DlgMng.idx == 1 ? 0x24 : DlgMng.idx == 0 ? 0x14 : 4;
        break;
    default:
        DlgMng.sync = 4;
        break;
    }
    DlgMng.type = 0;
    DlgMng.play_func = 0;
}

/* pad input of the open dialog (MPad[MPadUser]: Rdw = accept, Rrt = cancel, left / right repeat = other button) */
static void __Play_Stable(void)
{
    PS2_FRAME(0x10);
    if (DlgMng.f_btnoff == 1) return;
    struct struct_2d84b07b *pad = &MPad[MPadUser];
    switch (DlgMng.type) {
    case 3:
        if (pad->Rrt == 1) {
            Voice_Call(0x3eb, 0x7f);
            mdialog_begin_close();
        }
        break;
    case 9:
    case 7:
    case 6:
    case 2:
        if (pad->Rrt == 1) {
            Voice_Call(0x3eb, 0x7f);
            DlgMng.idx = -1;
            mdialog_begin_close();
        } else if (pad->Rdw == 1) {
            Voice_Call(0x3ea, 0x7f);
            mdialog_begin_close();
            if (DlgMng.idx == 0) {
                if (DlgMng.type == 6) {
                    __Sell();
                } else if (DlgMng.type == 7) {
                    struct mdlg_shop_rec *d = (struct mdlg_shop_rec *)DlgMng.datap;
                    g_gamedata[d->player].money -= d->price;
                    DF_MPART_BuyNx(d->player, d->part_id, d->acpeq, 1);
                }
                DlgMng.sync |= 0x10; /* 0x13 while closing; __Play_Closing assigns the result over it */
            }
        } else if (pad->repeat.rt || pad->repeat.lt) {
            Voice_Call(0x3e9, 0x7f);
            DlgMng.idx = 1 - DlgMng.idx;
        }
        break;
    default:
        if (pad->Rdw == 1 || pad->Rrt == 1) {
            Voice_Call(pad->Rdw == 1 ? 0x3ea : 0x3eb, 0x7f);
            mdialog_begin_close();
        }
        break;
    }
}

/* 3-frame open tween, then stable */
static void __Play_Opening(void)
{
    PS2_FRAME(0x10);
    if (DlgMng.count++ < 3) {
        DF_MFUNC_CalcInterRect(&DlgMng.cur_rect, &DlgMng.prev_rect, &DlgMng.next_rect, DlgMng.count, 3);
        return;
    }
    DlgMng.cur_rect = DlgMng.next_rect;
    DlgMng.sync = 2;
    DlgMng.play_func = (void (*)(void))GFN(__Play_Stable);
    DlgMng.count = 0;
}

/* box size from the text (9 x 18 px cells + borders, OK / CANCEL row for types 2 / 9), clamped to 0x118..0x258 x
 * 0x3b..0x190 (BGM select at least 0x168 wide), centred horizontally, vertical position by pos_type */
static void __CalcRectFormText(struct stMenuRect *outRectp)
{
    PS2_FRAME(0x30);
    int w = 9 * DF_MFONT_GetWidth(DlgMng.str_textp) + 0x24;
    int h = 18 * DF_MFONT_GetHeight(DlgMng.str_textp) + 0x29;
    int type = DlgMng.type;
    if (type == 2 || type == 9) h += 0x1c;
    if (w > 0x258) w = 0x258;
    if (h > 0x190) h = 0x190;
    if (w < 0x118) w = 0x118;
    if (h < 0x3b) h = 0x3b;
    if (type == 9 && w < 0x168) w = 0x168;
    outRectp->x = 0x140 - w / 2;
    outRectp->y = 0;
    outRectp->w = w;
    outRectp->h = h;
    switch (DlgMng.pos_type) {
    case 3: outRectp->y = 0xe0; break;
    case 2: outRectp->y = 0x190 - h; break;
    case 1: outRectp->y = 0x22; break;
    case 0: outRectp->y = 0xe0 - h / 2; break;
    }
}

void DF_MDIALOG_Init(void)
{
    PS2_FRAME(0x10);
    DF_MFUNC_ClearMemory(&DlgMng, 0x18);
    DlgMng.play_func = 0;
    DlgMng.pkt_func = 0;
}

/* ---- drawing ---- */

static inline struct_76b810ec mdlg_rgba(unsigned int w)   /* colour word as stored in sdata (r in the low byte) */
{
    struct_76b810ec c;
    memcpy(&c, &w, 4);
    return c;
}

/* the non-zero words of a 0x228-byte stMFont template in OLMENU rodata */
struct mdlg_word { unsigned short off; unsigned int w; };
static void mdlg_font_from_template(struct_acb9a91f *f, const struct mdlg_word *t, int n)
{
    memset(f, 0, sizeof(*f));
    for (int i = 0; i < n; i++) memcpy((char *)f + t[i].off, &t[i].w, 4);
}
/* 0x1edc4c0: Draw's text font (w / off_x 10, h / off_y 20, grey, flag 0x800, space 10, narrow 6, type 6) */
static const struct mdlg_word mdlg_tmpl_text[] = {
    {0x4, 0x0014000a}, {0x8, 0x0014000a}, {0xc, 0x80909090}, {0x10, 0x80909090}, {0x218, 0x800}, {0x21c, 0x0006060a}};
/* 0x1edc290: __DrawShopInfo's font (col_nx[0] / [1] = {0, 0, 0, 10} / {6, 6, 0, 0}, flag / space / type 0) */
static const struct mdlg_word mdlg_tmpl_shop[] = {
    {0x4, 0x0014000a}, {0x8, 0x0014000a}, {0xc, 0x80909090}, {0x10, 0x0a000000}, {0x14, 0x606}};
static_assert(sizeof(struct_acb9a91f) == 0x228, "stMFont");

/* 9-slice window from texture 0x3f4 (8 px borders, halved when the rect is under 16 px); the centre is filled in
 * strips of the texture's 0x44 px centre height */
void DF_MDIALOG_DrawDialogWnd(struct stMenuRect *pRect, struct struct_76b810ec *pColor)
{
    PS2_FRAME(0x1c0);
    PS2_STACK(struct stMenuRect, rect, 0x140);
    PS2_STACK(struct_13efd7f4, texinfo, 0x150);
    PS2_STACK(int[3], w, 0x1b0);
    PS2_STACK(int[3], h, 0x1a0);
    PS2_STACK(int[3], u, 0x190);
    PS2_STACK(int[3], v, 0x180);
    PS2_STACK(int[3], uw, 0x170);
    PS2_STACK(int[3], vh, 0x160);
    PS2_STACK(struct stMenuRect, r, 0x130);
    PS2_STACK(int[4], uv, 0x120);
    PS2_STACK(struct stMenuRect, r2, 0x110);
    PS2_STACK(int[4], uv2, 0x100);

    rect = *pRect;
    u[0] = 2; u[1] = 6; u[2] = 0xfa;
    v[0] = 2; v[1] = 6; v[2] = 0x4a;
    uw[0] = 4; uw[1] = 0xf4; uw[2] = 4;
    vh[0] = 4; vh[1] = 0x44; vh[2] = 4;
    if (!MenuTex_StandbyTexture(0x3f4, &texinfo)) return;
    if (rect.w < 0x10) {
        w[0] = rect.w / 2;
        w[1] = 0;
        w[2] = (rect.w + 1) / 2;
    } else {
        w[0] = 8;
        w[1] = rect.w - 0x10;
        w[2] = 8;
    }
    if (rect.h < 0x10) {
        h[0] = rect.h / 2;
        h[1] = 0;
        h[2] = (rect.h + 1) / 2;
    } else {
        h[0] = 8;
        h[1] = rect.h - 0x10;
        h[2] = 8;
    }
    int y = rect.y;
    for (int by = 0; by < 3; by++) {
        int x = rect.x;
        for (int bx = 0; bx < 3; bx++) {
            r.x = x;
            r.y = y;
            r.w = w[bx];
            r.h = h[by];
            uv[0] = u[bx];
            uv[1] = v[by];
            uv[2] = u[bx] + uw[bx];
            uv[3] = v[by] + vh[by];
            if (by != 1 || bx != 1) DF_MDRAW_DrawTexSprite(&texinfo, &r, uv, pColor, 0);
            x += w[bx];
        }
        y += h[by];
    }
    int x = rect.x + w[0];
    y = rect.y + h[0];
    while (h[1] > 0) {
        int draw_h = vh[1];
        if (h[1] < draw_h) draw_h = h[1];
        r2.x = x;
        r2.y = y;
        r2.w = w[1];
        r2.h = draw_h;
        uv2[0] = u[1];
        uv2[1] = v[1];
        uv2[2] = u[1] + uw[1];
        uv2[3] = v[1] + draw_h;
        DF_MDRAW_DrawTexSprite(&texinfo, &r2, uv2, pColor, 0);
        h[1] -= draw_h;
        y += draw_h;
    }
}

/* every frame from the menu main loop; the fonts / colours / line points are set up before the "no dialog" return.
 * Alpha fades over the open / close tween and the title and text shrink vertically with it. */
void DF_MDIALOG_Draw(void)
{
    PS2_FRAME(0x4f0);
    PS2_STACK(struct_acb9a91f, type_attr, 0x2b0);
    PS2_STACK(struct_acb9a91f, text_attr, 0x80);
    PS2_STACK(struct_76b810ec, col, 0x4ec);
    PS2_STACK(struct_76b810ec, wndcol, 0x4e8);
    PS2_STACK(struct_76b810ec, title_col, 0x4e4);
    PS2_STACK(struct_76b810ec, JFontcol, 0x4e0);
    PS2_STACK(struct_76b810ec[2], line_col, 0x4d8);
    PS2_STACK(int[2][4], line_pos, 0x60);
    PS2_STACK(struct stMenuRect, rect, 0x50);
    PS2_STACK(int[4], UV, 0x40);
    int alpha;

    memset(&type_attr, 0, sizeof(type_attr));
    mdlg_font_from_template(&text_attr, mdlg_tmpl_text, 6);
    text_attr.x = DlgMng.text_x;
    text_attr.y = DlgMng.text_y;
    text_attr.strp = DlgMng.str_textp;
    col = mdlg_rgba(0x60808080);
    wndcol = mdlg_rgba(0x64505050);
    title_col = mdlg_rgba(0x80656565);
    JFontcol = mdlg_rgba(0x809b917d);
    line_col[0] = mdlg_rgba(0x80494949);
    line_col[1] = mdlg_rgba(0x80494949);
    memset(line_pos, 0, sizeof(line_pos));
    if (DlgMng.type == 0) return;

    rect = DlgMng.cur_rect;
    DF_MFONT_SetFontAttr(&type_attr, 0);
    type_attr.x = (short)(DlgMng.cur_rect.x + 8);
    type_attr.y = (short)(DlgMng.cur_rect.y + 5);
    type_attr.col = title_col;
    type_attr.strp = DlgMng.str_typep;
    type_attr.depth = 0;
    type_attr.flag = 0;
    line_pos[0][0] = DlgMng.cur_rect.x + 8;
    line_pos[0][1] = DlgMng.cur_rect.y + 0xe;
    line_pos[1][0] = DlgMng.cur_rect.x + DlgMng.cur_rect.w - 8;
    line_pos[1][1] = line_pos[0][1];
    text_attr.x = DlgMng.text_x;
    text_attr.y = DlgMng.text_y;
    text_attr.w = 0x12;
    text_attr.h = 0x12;
    text_attr.off_x = 0x12;
    text_attr.off_y = 0x12;
    text_attr.col = JFontcol;
    text_attr.strp = DlgMng.str_textp;
    text_attr.flag = 0x501;
    text_attr.depth = 0;
    text_attr.space_w = 0xa;
    text_attr.narrow_w = 0xa;

    switch (DlgMng.sync & 0xf) {
    case 1: alpha = (DlgMng.count << 7) / 3; break;
    case 3: alpha = 0x80 - (DlgMng.count << 7) / 3; break;
    default: alpha = 0x80; break;
    }
    type_attr.col.a = (unsigned char)alpha;
    text_attr.col.a = (unsigned char)alpha;
    line_col[0].a = (unsigned char)alpha;
    line_col[1].a = (unsigned char)alpha;
    type_attr.y = (short)(type_attr.y + type_attr.h / 2 * (0x80 - alpha) / 128);
    type_attr.h = (short)(type_attr.h * alpha / 128);
    text_attr.y = (short)(text_attr.y + text_attr.h / 2 * (0x80 - alpha) / 128);
    text_attr.h = (short)(text_attr.h * alpha / 128);

    DF_MDRAW_KickReg(0x47, 0x30003);
    DF_MDRAW_KickReg(0x42, 0x44);
    if (MenuMng.f_movie_brief) {   /* dim backdrop */
        col.a = (unsigned char)(alpha * 3 * 32 / 128);
        UV[0] = 0;
        UV[1] = 0;
        UV[2] = 0x20;
        UV[3] = 0x20;
        DF_MDRAW_NexusDrawGeneralWindow(&rect, &col, 0, UV);
    }
    DF_MDIALOG_DrawDialogWnd(&rect, &wndcol);
    DF_MFONT_Draw(&type_attr, 0);
    DF_MDRAW_DrawPrim(2, 2, line_pos, line_col, 1.0f, 1);   /* the line under the title */
    DF_MFONT_Draw(&text_attr, 0);
    switch (DlgMng.type) {
    case 9: __DrawSelectBGM(alpha); break;
    case 7:
    case 6:
        __DrawShopInfo(alpha);
        __DrawOkCancel(alpha);
        break;
    case 2: __DrawOkCancel(alpha); break;
    }
}

/* shop rows: label + price and label + second value right-aligned in 9 cells, and (sell dialog) a third label with
 * three strings 4 cells apart */
static void __DrawShopInfo(int inAlpha)
{
    PS2_FRAME(0x2a0);
    PS2_STACK(char[32], buff, 0x280);
    PS2_STACK(struct_acb9a91f, text_attr, 0x50);
    struct mdlg_shop_rec *sip;

    memset(buff, 0, sizeof(buff));
    mdlg_font_from_template(&text_attr, mdlg_tmpl_shop, 5);
    text_attr.x = DlgMng.text_x;
    text_attr.y = DlgMng.text_y;
    text_attr.w = 0x12;
    text_attr.h = 0x12;
    text_attr.off_x = 0x12;
    text_attr.off_y = 0x12;
    text_attr.col = mdlg_rgba(0x80909090);
    text_attr.strp = 0;
    text_attr.col.a = (unsigned char)inAlpha;
    sip = (struct mdlg_shop_rec *)DlgMng.datap;
    text_attr.flag = 0x101;
    text_attr.space_w = 0xa;
    text_attr.narrow_w = 0xa;
    text_attr.depth = 0;
    /* SyncDCache(&text_attr, &text_attr + sizeof(text_attr)) (0x228 structs = 300 KB): no-op on the host */

    text_attr.x = sip->x;
    text_attr.y = sip->y;
    text_attr.strp = sip->label[0];
    DF_MFONT_Draw(&text_attr, 0);
    ps2_sprintf_d(buff, mdialog_fmt_ld, sip->price);
    text_attr.x = (short)(sip->x + 0xa0 + text_attr.w * (9 - (short)strlen(buff)));
    text_attr.y = sip->y;
    text_attr.strp = (unsigned char *)buff;
    DF_MFONT_Draw(&text_attr, 0);

    text_attr.x = sip->x;
    text_attr.y = (short)(sip->y + 0x12);
    text_attr.strp = sip->label[1];
    DF_MFONT_Draw(&text_attr, 0);
    ps2_sprintf_d(buff, mdialog_fmt_ld, sip->value);
    text_attr.x = (short)(sip->x + 0xa0 + text_attr.w * (9 - (short)strlen(buff)));
    text_attr.y = (short)(sip->y + 0x12);
    text_attr.strp = (unsigned char *)buff;
    DF_MFONT_Draw(&text_attr, 0);

    if (DlgMng.type == 7) return;
    text_attr.x = sip->x;
    text_attr.y = (short)(sip->y + 0x24);
    text_attr.strp = sip->label[2];
    DF_MFONT_Draw(&text_attr, 0);
    text_attr.x = (short)(sip->x + 0x7c);
    for (int i = 0; i < 3; i++) {
        text_attr.strp = sip->label[3 + i];
        DF_MFONT_Draw(&text_attr, 0);
        text_attr.x = (short)(text_attr.x + (short)(text_attr.w * 4));   /* x as DF_MFONT_Draw left it */
    }
}

/* two buttons (__DrawOkCancel / __DrawSelectBGM): the selected one blinks in its own colour, the other is grey at
 * 90 / 128 of the dialog alpha. Only the fields set here are initialised: the rest of both fonts (narrow_offset,
 * col_nx, fade range) is stale stack. Runs in the caller's PS2 frame. */
static void mdlg_draw_buttons(int inAlpha, short x0, short x1, int id0, unsigned int c0, unsigned int c1,
                              unsigned int dc)
{
    PS2_STACK(struct_acb9a91f, yes_attr, 0x2b0);
    PS2_STACK(struct_acb9a91f, no_attr, 0x80);
    PS2_STACK(struct_76b810ec[2], col, 0x4e0);
    PS2_STACK(struct_76b810ec, d_col, 0x4ec);
    PS2_STACK(struct_76b810ec, tmp_col, 0x4e8);
    int sel_a, other_a;

    col[0] = mdlg_rgba(c0);
    col[1] = mdlg_rgba(c1);
    d_col = mdlg_rgba(dc);
    yes_attr.x = x0;
    yes_attr.y = (short)(DlgMng.cur_rect.y + DlgMng.cur_rect.h - 0x1c);
    yes_attr.w = 0x14;
    yes_attr.off_x = 0x14;
    yes_attr.h = 0xe;
    yes_attr.off_y = 0xe;
    yes_attr.col = col[0];
    yes_attr.strp = DF_MSTR_GetName(MenuMng.language, 0, id0);
    yes_attr.depth = 0;
    yes_attr.flag = 0x100;
    yes_attr.space_w = 8;
    yes_attr.type = 4;
    yes_attr.narrow_w = 8;
    no_attr.x = x1;
    no_attr.y = yes_attr.y;
    no_attr.w = 0x14;
    no_attr.off_x = 0x14;
    no_attr.h = 0xe;
    no_attr.off_y = 0xe;
    no_attr.col = col[1];
    no_attr.strp = DF_MSTR_GetName(MenuMng.language, 0, id0 + 1);
    no_attr.space_w = 8;
    no_attr.flag = 0x100;
    no_attr.narrow_w = 8;
    no_attr.type = 4;
    no_attr.depth = 0;
    sel_a = DF_MFUNC_GetBlinkAlpha() * inAlpha / 128;
    d_col.a = (unsigned char)inAlpha;
    other_a = inAlpha * 90 / 128;
    for (int i = 0; i < 2; i++) {
        if (i == DlgMng.idx) {
            tmp_col = col[i];
            tmp_col.a = (unsigned char)sel_a;
        } else {
            tmp_col = d_col;
            tmp_col.a = (unsigned char)other_a;
        }
        DF_MDRAW_KickReg(0x42, 0x48);
        if (i == 0) {
            yes_attr.col = tmp_col;
            DF_MFONT_Draw(&yes_attr, 0);
        } else {
            no_attr.col = tmp_col;
            DF_MFONT_Draw(&no_attr, 0);
        }
    }
}

static void __DrawOkCancel(int inAlpha)
{
    PS2_FRAME(0x4f0);
    mdlg_draw_buttons(inAlpha, 0xdc, 0x12c, 0x19302, 0x805a64d0, 0x80afaa70, 0x50808080);
}

static void __DrawSelectBGM(int inAlpha)
{
    PS2_FRAME(0x4f0);
    mdlg_draw_buttons(inAlpha, 0xa0, 0x154, 0x19304, 0x80afaa70, 0x80afaa70, 0x50808080);
}
