/* src/menu/mefunc.cpp - MEFUNC, the tool half of the emblem editor ("EME", OLMENU overlay; the editor shell is
 * membe.cpp): the drawing primitives on the 128x128 index image, the USB-mouse / pad refresh and the Func-panel
 * systems (Grid, Gradation, RGB bars, Text stamp, Rot, Clear, Shift, Mirror).
 * Group 1 native: the RGB colour-bar widget (__CreateRgbCtrl / __UpdateRgbCtrl / __DeleteRgbCtrl: window, three bars
 * whose width is the channel value, RED / GREEN / BLUE labels, value strings, blinking arrow cursor), the input
 * refresh (Ac2_Emb_MouseInit / Ac2_Emb_MouseUpdate: USB mouse via the fsusb / fsums SDK; DF_EME_PadUpdate: ePad
 * direction bits with key repeat and diagonal sync) and __EME_GetTM2Index (alpha test of a 4-bit stamp texture
 * texel).
 * Group 2a native: the image / CLUT operations __EME_Func_Gradate (CLUT gradient), Rotate, Mirror, Shift (wrap-around
 * one-pixel shift), Paint (scanline flood fill on a MenuWorkp seed stack) and CAP (copy and paste).
 * Group 2b native: the pen tools __EME_Func_DrawPoint (brush into the image or a layer), Eraser, DrawLine
 * (Bresenham), DrawBox and DrawCircle (midpoint ellipse, 32-bit wraparound decision terms).
 * Group 3 native: the Grid overlay system (__EME_DrawGrid SPR line packets, Play / fades, __SetNewGridSystem) and the
 * Gradation system (__SetNewGradSystem, __PlayEmbGradSystem: head / tail CLUT pick -> __EME_Func_Gradate, fades).
 * Group 4 native: the RGB colour-bar system and RGBPrev (palette pick before the bars).
 * Group 5a native: the Text stamp system's support (glyph plate grid build / fades, Move view scroll, Dispatch,
 * __SetNewTextSystem; the Play function is group 5b), the one-shot Rot / Clear constructors and the Shift system. */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

#ifdef ACNB_SOURCEPORT
/* main ELF sdata 0x27b718 / 0x27b720 / 0x27b728 / 0x27b734 (nodwarf.h): the bar labels (kept by
 * DF_MOBJ_NewStringDirect) and the "%d" value format */
char mefunc_str_red[4] = "RED";
char mefunc_str_green[6] = "GREEN";
char mefunc_str_blue[5] = "BLUE";
char mefunc_fmt_d[3] = "%d";
/* __CreateRgbCtrl's value strings keep a pointer to its sprintf buffer (a stack slot in the original, read until the
 * first __UpdateRgbCtrl copies the strings): the source port gives the shared buffer a lifetime and empties it after
 * the create, as the original's slot reads by then. */
static char mefunc_rgb_valbuf[0x20];
#endif

/* OLMENU 0x1edd268 / 0x1edd280: layout part names (only read during the UnitSprite_New call) */
static char mefunc_parts_waku[] = "rgb_waku_0";
static char mefunc_parts_yajirushi[] = "rgb_yajirushi_0";

static inline struct _LO_HEADER *mefunc_layout(int id)
{
    return (struct _LO_HEADER *)LAYOUT_GetLayoutData(id);
}

/* The inlined erase macro: f_draw = 0, f_erase = 1 on stMobj byte 0 (DF_MOBJ_Play frees it), then the slot = NULL. */
static inline void mefunc_erase(struct stMobj **pp)
{
    if (*pp) {
        (*pp)->f_draw = 0;
        (*pp)->f_erase = 1;
        *pp = 0;
    }
}

/* Per channel: up / dw (press only, == 1) move the cursor with wrap, lt / rt (every frame while held) step the value
 * clamped to 0..255; then every bar width, value string and cursor blink is refreshed. */
static void __UpdateRgbCtrl(struct struct_a697a0ff *p_rgb_ctrl)
{
    PS2_FRAME(0x90);
    PS2_STACK(char[0x20], buf, 0x60);
    unsigned char *chan[3];
    chan[0] = (unsigned char *)p_rgb_ctrl->p_color + 0;
    chan[1] = (unsigned char *)p_rgb_ctrl->p_color + 1;
    chan[2] = (unsigned char *)p_rgb_ctrl->p_color + 2;
    struct struct_2d84b07b *pad = &MPad[MPadUser];

    if (pad->up == 1) {
        Voice_Call(0x3e9, 0x7f);
        p_rgb_ctrl->cur_color--;
        if (p_rgb_ctrl->cur_color < 0) p_rgb_ctrl->cur_color = 2;
    } else if (pad->dw == 1) {
        Voice_Call(0x3e9, 0x7f);
        p_rgb_ctrl->cur_color++;
        if (p_rgb_ctrl->cur_color >= 3) p_rgb_ctrl->cur_color = 0;
    } else if (pad->lt > 0) {
        Voice_Call(0x3f2, 0x7f);
        unsigned char *p = chan[p_rgb_ctrl->cur_color];
        int v = *p - 1;
        if (v < 0) v = 0;
        *p = (unsigned char)v;
    } else if (pad->rt > 0) {
        Voice_Call(0x3f2, 0x7f);
        unsigned char *p = chan[p_rgb_ctrl->cur_color];
        int v = *p + 1;
        if (v >= 0x100) v = 0xff;
        *p = (unsigned char)v;
    }

    for (int i = 0; i < 3; i++) {
        struct struct_0068c5e3 *bar = &p_rgb_ctrl->a_color_bar[i];
        bar->p_bar->dt.sprite.rect.w = *chan[i];
        ps2_sprintf_d(buf, mefunc_fmt_d, *chan[i]);
        DF_MOBJ_SetStringCopy(bar->p_value, buf);
        if (i == p_rgb_ctrl->cur_color)
            DF_MOBJ_SetDrawEnable(bar->p_scroll, 1);
        else
            DF_MOBJ_SetDrawEnable(bar->p_scroll, 0);
    }
}

static void __DeleteRgbCtrl(struct struct_a697a0ff *p_rgb_ctrl)
{
    PS2_FRAME(0x30);
    UnitSprite_Delete(p_rgb_ctrl->p_window);
    for (int i = 0; i < 3; i++) {
        struct struct_0068c5e3 *bar = &p_rgb_ctrl->a_color_bar[i];
        mefunc_erase(&bar->p_bar);
        mefunc_erase(&bar->p_name);
        mefunc_erase(&bar->p_value);
        UnitSprite_Delete(bar->p_scroll);
    }
}

/* Builds the RGB window: per channel a bar sprite (texture 0x3ec, width = channel value, height 8), a label, a value
 * string and a blinking arrow; only channel 0's arrow starts visible. Quirk: all three value strings point at the same
 * sprintf buffer on this function's stack (holding the blue value at return), until __UpdateRgbCtrl copies them. */
static void __CreateRgbCtrl(struct stMobj *p_obj, struct struct_a697a0ff *p_rgb_ctrl, struct struct_76b810ec *p_color)
{
    PS2_FRAME(0x190);
#ifdef ACNB_SOURCEPORT
    char *valbuf = mefunc_rgb_valbuf;
#else
    PS2_STACK(char[0x20], valbuf, 0xe0);
#endif
    /* main ELF sdata 0x27b700 / 0x27b708 / 0x27b710: bar / label / value base positions */
    const struct struct_214acd28 bar_pos = {41, 258}, name_pos = {40, 244}, value_pos = {297, 244};
    int a_rgb_value[3];
    a_rgb_value[0] = p_color->r;
    a_rgb_value[1] = p_color->g;
    a_rgb_value[2] = p_color->b;
    /* OLMENU 0x1edd230: bar texcoord; 0x1edd240: bar colour + sub colour per channel (RGBA words) */
    alignas(16) int texcoord[4] = {152, 34, 184, 42};
    alignas(16) unsigned int aa_bar_color[3][2] = {
        {0x803830cd, 0x80367fd1}, {0x805a962a, 0x804fba86}, {0x80b73847, 0x80b57545},
    };
    /* OLMENU 0x1edd258: label pointers; main ELF sdata 0x27b730: string colour */
    char *a_name[3] = {mefunc_str_red, mefunc_str_green, mefunc_str_blue};
    struct struct_76b810ec str_col = {0x90, 0x90, 0x90, 0x80};

    p_rgb_ctrl->p_window = UnitSprite_New(p_obj, mefunc_layout(0x414), mefunc_parts_waku);
    for (int i = 0; i < 3; i++) {
        struct struct_0068c5e3 *bar = &p_rgb_ctrl->a_color_bar[i];
        alignas(16) struct stMenuRect rect;
        rect.x = bar_pos.x;
        rect.y = bar_pos.y + 0x1e * i;
        rect.w = a_rgb_value[i];
        rect.h = 8;
        bar->p_bar = DF_MOBJ_NXNewSprite(p_obj, 0x3ec, &rect, texcoord, (struct struct_76b810ec *)&aa_bar_color[i][0],
                                         0, 0, 0, 0);
        DF_MOBJ_SetSpriteDrawMode(bar->p_bar, 1);
        DF_MOBJ_SetSpriteSubColor(bar->p_bar, (struct struct_76b810ec *)&aa_bar_color[i][1]);
        bar->p_name = DF_MOBJ_NewStringDirect(p_obj, a_name[i], 1, name_pos.x, name_pos.y + 0x1e * i, &str_col, 0, 0);
        ps2_sprintf_d(valbuf, mefunc_fmt_d, a_rgb_value[i]);
        bar->p_value = DF_MOBJ_NewStringDirect(p_obj, valbuf, 1, value_pos.x, value_pos.y + 0x1e * i, &str_col, 0,
                                               0x10000);
        bar->p_scroll = UnitSpriteBlink_New(p_obj, mefunc_layout(0x414), mefunc_parts_yajirushi);
        struct struct_214acd28 pos;
        UnitSprite_GetPosition(bar->p_scroll, &pos);
        pos.y += 0x1e * i;
        UnitSprite_SetPosition(bar->p_scroll, &pos);
    }
#ifdef ACNB_SOURCEPORT
    /* original: the dead stack slot is zeroed by __MOBJ_DrawString's cleared font local before it reads strp, so the
     * values draw "" until __UpdateRgbCtrl copies them */
    valbuf[0] = 0;
#endif
    DF_MOBJ_SetDrawEnable(p_rgb_ctrl->a_color_bar[1].p_scroll, 0);
    DF_MOBJ_SetDrawEnable(p_rgb_ctrl->a_color_bar[2].p_scroll, 0);
    p_rgb_ctrl->p_color = p_color;
    p_rgb_ctrl->cur_color = 0;
}

/* "No mouse" state (inlined three times in MouseUpdate and once in MouseInit). */
static inline void mefunc_mouse_reset(void)
{
    eMouse->x = 0;
    eMouse->y = 0;
    eMouse->w = 0;
    eMouse->btnbit = 0;
    eMouse->no = -1;
    eMouse->port = 1;
    eMouse->lbtn = 0;
    eMouse->rbtn = 0;
    eMouse->wbtn = 0;
    eMouse->f_update = 0;
    eMouse->speed = 0x100;
}

/* Saturating hold counter of one mouse button. */
static inline void mefunc_mouse_btn(unsigned char *cnt, unsigned int bit)
{
    if (eMouse->btnbit & bit)
        *cnt = (unsigned char)(*cnt + (*cnt < 0xff));
    else
        *cnt = 0;
}

/* Polls the USB mouse: finds a device on port 1..2, reads it, copies buttons / motion (only when the read reported
 * data) and updates the button hold counters. Any failure resets the mouse state. Quirks: the port-exhausted reset does
 * not call fsusb_UsbSync, the read-error reset calls fsusb_UsbSync(0, 1) (a1 left from the port store), the
 * GetInfo-error reset fsusb_UsbSync(0, 0). */
void Ac2_Emb_MouseUpdate(void)
{
    PS2_FRAME(0x20);
    int found = 0;
    eMouse->f_update = 0;
    do {
        if (fsusb_UsbGetInfo(eMouse->port, 0, &eMouse->usbinfo)) {
            mefunc_mouse_reset();
            fsusb_UsbSync(0, 0);
            return;
        }
        fsusb_UsbSync(0, 0);
        if (eMouse->usbinfo.DeviceID >= 0) {
            eMouse->no = (char)eMouse->usbinfo.DeviceID;
            found = 1;
        } else {
            eMouse->port++;
            if (eMouse->port >= 3) {
                mefunc_mouse_reset();
                return;
            }
        }
    } while (!found);

    if (fsums_UsbMouseRead(eMouse->no, &eMouse->usbinfo)) {
        mefunc_mouse_reset();
        fsusb_UsbSync(0, 1);
        return;
    }
    fsusb_UsbSync(0, 1);
    if (eMouse->usbinfo.DEVICE_DATA.UMS.DataSize > 0) eMouse->f_update = 1;
    if (eMouse->f_update) {
        eMouse->btnbit = eMouse->usbinfo.DEVICE_DATA.UMS.Button;
        eMouse->x = (short)eMouse->usbinfo.DEVICE_DATA.UMS.X;
        eMouse->y = (short)eMouse->usbinfo.DEVICE_DATA.UMS.Y;
        eMouse->w = (short)eMouse->usbinfo.DEVICE_DATA.UMS.Wheel;
    } else {
        eMouse->x = 0;
        eMouse->y = 0;
        eMouse->w = 0;
    }
    mefunc_mouse_btn(&eMouse->lbtn, 1);
    mefunc_mouse_btn(&eMouse->rbtn, 2);
    mefunc_mouse_btn(&eMouse->wbtn, 4);
}

void Ac2_Emb_MouseInit(void)
{
    mefunc_mouse_reset();
}

/* Key repeat of one direction hold counter: the press frame, then hold 0x12 and every padRepeatIntv frames after. */
static inline unsigned char mefunc_pad_bit(int c)
{
    if (c >= 0x12 && (c - 0x12) % padRepeatIntv == 0) return 1;
    return c == 1;
}

/* ePad direction bits from MPad's hold counters; on a diagonal hold the longer-held (older) key's bit wins for both
 * keys (UL, UR, DL, DR in that order, each seeing the previous ones' result). */
void DF_EME_PadUpdate(void)
{
    struct struct_2d84b07b *pad = &MPad[MPadUser];
    int u = pad->up;
    ePad.up = mefunc_pad_bit(u);
    int d = pad->dw;
    ePad.dw = mefunc_pad_bit(d);
    int l = pad->lt;
    ePad.lt = mefunc_pad_bit(l);
    int r = pad->rt;
    ePad.rt = mefunc_pad_bit(r);

    if (u > 0 && l > 0) {
        if (u >= l) ePad.lt = ePad.up;
        else ePad.up = ePad.lt;
    }
    if (u > 0 && r > 0) {
        if (u >= r) ePad.rt = ePad.up;
        else ePad.up = ePad.rt;
    }
    if (d > 0 && l > 0) {
        if (d >= l) ePad.lt = ePad.dw;
        else ePad.dw = ePad.lt;
    }
    if (d > 0 && r > 0) {
        if (d >= r) ePad.rt = ePad.dw;
        else ePad.dw = ePad.rt;
    }
}

/* 1 when texel (inX, inY) of 4-bit menu texture idx has a non-zero CLUT alpha (stamp mask). No bounds check. */
static unsigned char __EME_GetTM2Index(int idx, int inX, int inY)
{
    PS2_FRAME(0x30);
    struct struct_c53f69de *hd = MenuTex_GetTexHeadSt(idx);
    int lg = (int)hd->tex.tex0.tw;
    int w = 1;
    for (int k = 0; k < lg; k++) w <<= 1;
    unsigned char *pix = (unsigned char *)hd + 0x20;
    unsigned char *clut = pix + hd->wnum;
    int n = inX + inY * w;
    unsigned char b = pix[n / 2];
    if (n & 1) b = (unsigned char)(b >> 4);
    return clut[(b & 0xf) * 4 + 3] != 0;
}

/* ---- group 2a: byte-image / CLUT operations ---- */

/* The CSM1 CLUT swizzle of palette index i: within each 32-entry block, entries 8-15 and 16-23 swap
 * (C remainder, so a negative i is left alone unless its remainder falls in the ranges). */
static inline int mefunc_clut_swizzle(int i)
{
    int m = i % 32;
    if (m >= 8 && m < 16) return i + 8;
    if (m >= 16 && m < 24) return i - 8;
    return i;
}

/* The compiler's float -> unsigned idiom (c.ole.s against 2^31, cvt.w.s truncates). */
static inline int mefunc_f2u(float f)
{
    if (2147483648.0f <= f) return (int)(f - 2147483648.0f) | (int)0x80000000;
    return (int)f;
}

/* Linear gradient on the edit CLUT between palette cell swork[0..1] and (inX, inY) (16 cells per row; corners sorted
 * per axis). Quirk: the end cell b is never written (loop stops at b - 1); b < a does nothing. Alpha is 0x80. */
void __EME_Func_Gradate(int inX, int inY, struct struct_fedcf029 *dlp)
{
    PS2_FRAME(0xb0);
    unsigned char tlCol[4], hdCol[4], difCol[4];
    int x0, x1, y0, y1;
    if (inX < dlp->swork[0]) { x0 = inX; x1 = dlp->swork[0]; }
    else { x0 = dlp->swork[0]; x1 = inX; }
    if (inY < dlp->swork[1]) { y0 = inY; y1 = dlp->swork[1]; }
    else { y0 = dlp->swork[1]; y1 = inY; }
    int a = x0 + y0 * 16;
    memcpy(hdCol, dlp->edt_cbb[0] + mefunc_clut_swizzle(a) * 4, 4);
    int b = x1 + y1 * 16;
    memcpy(tlCol, dlp->edt_cbb[0] + mefunc_clut_swizzle(b) * 4, 4);
    int n = b - a;
    int dr = (int)tlCol[0] - (int)hdCol[0];
    int dg = (int)tlCol[1] - (int)hdCol[1];
    int db = (int)tlCol[2] - (int)hdCol[2];
    for (int ii = 0; ii < n; ii++) {
        int idx = mefunc_clut_swizzle(a + ii);
        int v;
        v = mefunc_f2u(((float)dr * (float)ii) / (float)n);
        difCol[0] = (unsigned char)(hdCol[0] + (v & 0xff));
        v = mefunc_f2u(((float)dg * (float)ii) / (float)n);
        difCol[1] = (unsigned char)(hdCol[1] + (v & 0xff));
        v = mefunc_f2u(((float)db * (float)ii) / (float)n);
        difCol[2] = (unsigned char)(hdCol[2] + (v & 0xff));
        difCol[3] = 0x80;
        memcpy(dlp->edt_cbb[0] + idx * 4, difCol, 4);
    }
}

/* Rotate the edit image 90 degrees (out[r][c] = in[127-c][r]) once (mode 0), three times (1) or twice (2), through
 * tmp_tbb; other modes do nothing. */
void __EME_Func_Rotate(struct struct_fedcf029 *dlp, int mode)
{
    PS2_FRAME(0x40);
    int passes = 0;
    if (mode == 0) passes = 1;
    else if (mode == 1) passes = 3;
    else if (mode == 2) passes = 2;
    for (int p = 0; p < passes; p++) {
        memcpy(dlp->tmp_tbb, dlp, 0x4000);
        for (int r = 0; r < 0x80; r++)
            for (int c = 0; c < 0x80; c++)
                dlp->edt_tbb[0][r * 0x80 + c] = dlp->tmp_tbb[(0x7f - c) * 0x80 + r];
    }
}

/* Mirror tool, driven by dlp->step: 0 = first corner, 1 = second corner (swork[2..5] = rect x, y, w, h), 2 = flip the
 * rect left-right (mode 0, column strips through tmp_tbb) or top-bottom (other modes, rows via memcpy). */
void __EME_Func_Mirror(int inX, int inY, struct struct_fedcf029 *dlp, int mode)
{
    PS2_FRAME(0x60);
    switch (dlp->step) {
    case 0:
        dlp->swork[0] = (short)inX;
        dlp->swork[1] = (short)inY;
        dlp->f_layer = 1;
        dlp->step = 1;
        break;
    case 1: {
        int x0, x1, y0, y1;
        if (inX < dlp->swork[0]) { x0 = inX; x1 = dlp->swork[0]; }
        else { x0 = dlp->swork[0]; x1 = inX; }
        if (inY < dlp->swork[1]) { y0 = inY; y1 = dlp->swork[1]; }
        else { y0 = dlp->swork[1]; y1 = inY; }
        dlp->swork[2] = (short)x0;
        dlp->swork[3] = (short)y0;
        dlp->swork[4] = (short)(x1 - x0 + 1);
        dlp->swork[5] = (short)(y1 - y0 + 1);
        dlp->step = 2;
        break;
    }
    case 2: {
        unsigned char *tmp = dlp->tmp_tbb;
        if (mode != 0) {
            for (int i = 0; i < dlp->swork[5] / 2; i++) {
                int h = dlp->swork[5];
                unsigned char *ra = dlp->edt_tbb[0] + dlp->swork[2] + (dlp->swork[3] + i) * 0x80;
                unsigned char *rb = dlp->edt_tbb[0] + dlp->swork[2] + (dlp->swork[3] + h - 1 - i) * 0x80;
                memcpy(tmp, ra, (size_t)(int)dlp->swork[4]);
                memcpy(ra, rb, (size_t)(int)dlp->swork[4]);
                memcpy(rb, tmp, (size_t)(int)dlp->swork[4]);
            }
        } else {
            for (int i = 0; i < dlp->swork[4] / 2; i++) {
                int w = dlp->swork[4];
                int row = dlp->swork[3] * 0x80;
                unsigned char *ca = dlp->edt_tbb[0] + dlp->swork[2] + i + row;
                unsigned char *cb = dlp->edt_tbb[0] + (dlp->swork[2] + w - 1 - i) + row;
                for (int k = 0; k < dlp->swork[5]; k++) tmp[k] = ca[k * 0x80];
                for (int k = 0; k < dlp->swork[5]; k++) ca[k * 0x80] = cb[k * 0x80];
                for (int k = 0; k < dlp->swork[5]; k++) cb[k * 0x80] = tmp[k];
            }
        }
        break;
    }
    }
}

/* Wrap-around shift of the 128x128 image by one pixel: mode 0 right, 1 left, 2 down, 3 up. tmp (tmp_tbb) keeps the
 * wrapped column / row; mode 0 stores column 127 reversed (tmp[127 - row]), mode 1 column 0 (tmp[row]). The original
 * is 8x unrolled; the results (incl. tmp contents) are the same. */
void __EME_Func_Shift(unsigned char *tbb, unsigned char *tmp, int mode)
{
    PS2_FRAME(0x40);
    switch (mode) {
    case 0:
        for (int k = 0; k < 0x80; k++) tmp[0x7f - k] = tbb[k * 0x80 + 0x7f];
        for (int j = 0; j < 0x80; j++) {
            unsigned char *row = tbb + (0x7f - j) * 0x80;
            for (int x = 0x7f; x > 0; x--) row[x] = row[x - 1];
            row[0] = tmp[j];
        }
        break;
    case 1:
        for (int k = 0; k < 0x80; k++) tmp[k] = tbb[k * 0x80];
        for (int r = 0; r < 0x80; r++) {
            unsigned char *row = tbb + r * 0x80;
            for (int x = 0; x < 0x7f; x++) row[x] = row[x + 1];
            row[0x7f] = tmp[r];
        }
        break;
    case 2:
        memcpy(tmp, tbb + 0x3f80, 0x80);
        for (int k = 0x3f7f; k >= 0; k--) tbb[k + 0x80] = tbb[k];
        memcpy(tbb, tmp, 0x80);
        break;
    case 3:
        memcpy(tmp, tbb, 0x80);
        for (int k = 0; k < 0x3f80; k++) tbb[k] = tbb[k + 0x80];
        memcpy(tbb + 0x3f80, tmp, 0x80);
        break;
    }
}

/* Scanline flood fill of the target colour at (inX, inY) with palette index curx[2] + cury[2]*16, using MenuWorkp as
 * an (x, y) byte-pair stack (no overflow check). Quirk: the seed scan of the rows above / below toggles its run flag
 * on every matching pixel (a matching pixel after a pushed one clears it), so long runs push every other pixel; the
 * extra seeds are harmless. */
void __EME_Func_Paint(int inX, int inY, struct struct_fedcf029 *dlp)
{
    unsigned char *sp = MenuWorkp;
    sp[0] = (unsigned char)inX;
    sp[1] = (unsigned char)inY;
    int target = dlp->edt_tbb[0][inY * 0x80 + inX];
    int fill = dlp->curx[2] + dlp->cury[2] * 16;
    sp += 2;
    if (target == fill) return;
    while (sp != MenuWorkp) {
        sp -= 2;
        int y = sp[1];
        int xr = sp[0];
        unsigned char *row = dlp->edt_tbb[0] + y * 0x80;
        int xl = xr;
        while (xl > 0 && row[xl - 1] == target) xl--;
        while (xr < 0x7f && row[xr + 1] == target) xr++;
        for (int x = xl; x <= xr; x++) row[x] = (unsigned char)fill;
        if (y != 0) {
            int run = 0;
            for (int x = xr; x >= xl; x--) {
                if (row[x - 0x80] == target && !run) {
                    sp[0] = (unsigned char)x;
                    sp[1] = (unsigned char)(y - 1);
                    sp += 2;
                    run = 1;
                } else {
                    run = 0;
                }
            }
        }
        if (y != 0x7f) {
            int run = 0;
            for (int x = xr; x >= xl; x--) {
                if (row[x + 0x80] == target && !run) {
                    sp[0] = (unsigned char)x;
                    sp[1] = (unsigned char)(y + 1);
                    sp += 2;
                    run = 1;
                } else {
                    run = 0;
                }
            }
        }
    }
}

/* Copy-and-paste tool: step 1 copies the rect swork[0..1]-(inX, inY) into tmp_tbb (rows packed, swork[2] = w,
 * swork[3] = h); step 2 pastes it at (inX, inY) clipped at 127. The paste loops test `<= w` / `<= h` (one too many)
 * but the clipped x / y bounds end them first. */
void __EME_Func_CAP(int inX, int inY, struct struct_fedcf029 *dlp)
{
    if (dlp->step == 1) {
        int x0, x1, y0, y1;
        if (inX < dlp->swork[0]) { x0 = inX; x1 = dlp->swork[0]; }
        else { x0 = dlp->swork[0]; x1 = inX; }
        if (inY < dlp->swork[1]) { y0 = inY; y1 = dlp->swork[1]; }
        else { y0 = dlp->swork[1]; y1 = inY; }
        dlp->swork[2] = (short)(x1 - x0 + 1);
        dlp->swork[3] = (short)(y1 - y0 + 1);
        unsigned char *dst = dlp->tmp_tbb;
        for (int y = y0; y <= y1; y++) {
            unsigned char *src = dlp->edt_tbb[0] + x0 + y * 0x80;
            for (int x = x0; x <= x1; x++) *dst++ = *src++;
        }
    } else if (dlp->step == 2) {
        int xe = inX + dlp->swork[2] - 1;
        int ye = inY + dlp->swork[3] - 1;
        if (xe >= 0x80) xe = 0x7f;
        if (ye >= 0x80) ye = 0x7f;
        int off = inX + inY * 0x80;
        for (int jj = 0, y = inY; jj <= dlp->swork[3] && y <= ye; jj++, y++, off += 0x80) {
            unsigned char *dst = dlp->edt_tbb[0] + off;
            unsigned char *src = dlp->tmp_tbb + jj * dlp->swork[2];
            for (int ii = 0, x = inX; ii <= dlp->swork[2] && x <= xe; ii++, x++) *dst++ = *src++;
        }
    }
}

/* ---- group 2b: the pen tools (point / eraser / line / box / circle) ---- */

/* One brush pixel of __EME_Func_DrawPoint, clipped to the 128x128 image. ext selects the target: 0 = edt_tbb[0]
 * (colour c), 1 = lyr_tbb (1), 3 = lyr_tbb2 (2), 2 = ply_tbb (2) - QUIRK: ply_tbb is 64 wide but the clip is 128, so
 * x >= 64 wraps into the next row and y >= 64 writes past ply_tbb (callers keep ext 2 inside 0..63; kept as is). */
static inline void mefunc_point_put(struct struct_fedcf029 *dlp, int x, int y, int ext, unsigned char c)
{
    if (x < 0 || x >= 0x80 || y < 0 || y >= 0x80) return;
    unsigned char *base = (unsigned char *)dlp;
    if (ext == 3) base[0x10000 + y * 0x80 + x] = 2;
    else if (ext == 2) base[0x15000 + y * 0x40 + x] = 2;
    else if (ext == 1) base[0xc000 + y * 0x80 + x] = 1;
    else if (ext == 0) base[y * 0x80 + x] = c;
}

/* Pen: an n x n brush (n = 2 * inSize + 1) centred on (inX, inY); n == 5 is the rounded 5x5 (corners left out). */
void __EME_Func_DrawPoint(int inX, int inY, int inSize, struct struct_fedcf029 *dlp, int ext)
{
    PS2_FRAME(0x30);
    int n = inSize * 2 + 1;
    unsigned char c = (unsigned char)(dlp->curx[2] + (dlp->cury[2] << 4));
    int oy = inY - n / 2;
    int ox = inX - n / 2;
    if (n != 5) {
        for (int j = 0; j < n; j++)
            for (int i = 0; i < n; i++) mefunc_point_put(dlp, ox + i, oy + j, ext, c);
        return;
    }
    for (int i = 1; i < n - 1; i++) mefunc_point_put(dlp, ox + i, oy, ext, c);
    int j = 1;
    for (; j < n - 1; j++)
        for (int i = 0; i < n; i++) mefunc_point_put(dlp, ox + i, oy + j, ext, c);
    for (int i = 1; i < n - 1; i++) mefunc_point_put(dlp, ox + i, oy + j, ext, c);
}

/* Midpoint ellipse in the box swork[0..1]-(inX, inY) (inMode != 0: filled). Degenerate boxes (height or width < 3)
 * become 2-pixel-thick runs. The decision terms are MIPS mult / mult1 / maddu: plain 32-bit wraparound. */
void __EME_Func_DrawCircle(int inX, int inY, int inMode, struct struct_fedcf029 *dlp, int ext)
{
    PS2_FRAME(0x190);
    int cx = (dlp->swork[0] + inX) / 2;
    int cy = (dlp->swork[1] + inY) / 2;
    int oddx = 0, oddy = 0;
    int t = dlp->swork[0] - inX;
    int rx = (t < 0 ? -t : t) + 1; /* SDK abs */
    t = dlp->swork[1] - inY;
    int ry = (t < 0 ? -t : t) + 1;
    if (ry < 3) {
        int h = rx / 2;
        for (int i = 0; i <= rx; i++) {
            int x = cx + i - h;
            __EME_Func_DrawPoint(x, cy, 0, dlp, ext);
            __EME_Func_DrawPoint(x, cy + 1, 0, dlp, ext);
        }
        return;
    }
    if (rx < 3) {
        int h = ry / 2;
        for (int i = 0; i <= ry; i++) {
            int y = cy + i - h;
            __EME_Func_DrawPoint(cx, y, 0, dlp, ext);
            __EME_Func_DrawPoint(cx + 1, y, 0, dlp, ext);
        }
        return;
    }
    if (rx % 2 == 0) oddx = 1;
    if (ry % 2 == 0) oddy = 1;
    int a = (rx - 1) / 2;
    int b = (ry - 1) / 2;
    unsigned int ua = (unsigned int)a, ub = (unsigned int)b;
    unsigned int a2x2 = ua * (ua * 2);       /* sp+0x100 */
    unsigned int m4b = (0u - ub) * 4;        /* sp+0xa0 */
    unsigned int ab = ub * ua;
    unsigned int b2x2 = ub * (ub * 2);       /* sp+0xd0 */
    unsigned int d1 = ub * ub + (ab * ((0u - ub) * 2) + a2x2);
    unsigned int d2 = ua * ua + (b2x2 + ab * m4b);
    int x1 = cx + oddx;
    int ytop = cy - b;
    int ybot = b + (cy + oddy);
    __EME_Func_DrawPoint(cx, ytop, 0, dlp, ext);
    __EME_Func_DrawPoint(cx, ybot, 0, dlp, ext);
    __EME_Func_DrawPoint(x1, ytop, 0, dlp, ext);
    __EME_Func_DrawPoint(x1, ybot, 0, dlp, ext);
    if (inMode != 0) {
        for (int k = 1; k <= b; k++) {
            int y0 = ytop + k, y1 = ybot - k;
            __EME_Func_DrawPoint(cx, y0, 0, dlp, ext);
            __EME_Func_DrawPoint(cx, y1, 0, dlp, ext);
            __EME_Func_DrawPoint(x1, y0, 0, dlp, ext);
            __EME_Func_DrawPoint(x1, y1, 0, dlp, ext);
        }
    }
    unsigned int a2x4 = ua * (ua * 4);  /* sp+0x110 */
    unsigned int b2x4 = ub * (ub * 4);  /* sp+0xe0 */
    unsigned int mb2x4 = ub * m4b;      /* sp+0xc0 */
    int j = 0;
    while (a > 0) {
        int yb = oddy + (cy + j);
        int xr = oddx + (cx + a);
        int xl = cx - a;
        int yt = cy - j;
        __EME_Func_DrawPoint(xr, yb, 0, dlp, ext);
        __EME_Func_DrawPoint(xl, yb, 0, dlp, ext);
        __EME_Func_DrawPoint(xr, yt, 0, dlp, ext);
        __EME_Func_DrawPoint(xl, yt, 0, dlp, ext);
        if (inMode != 0) {
            for (int k = 1; k <= j; k++) {
                int y0 = yb - k, y1 = yt + k;
                __EME_Func_DrawPoint(xr, y0, 0, dlp, ext);
                __EME_Func_DrawPoint(xl, y0, 0, dlp, ext);
                __EME_Func_DrawPoint(xr, y1, 0, dlp, ext);
                __EME_Func_DrawPoint(xl, y1, 0, dlp, ext);
            }
        }
        if ((int)d1 < 0) {
            j++;
            unsigned int s = (unsigned int)j * a2x4;
            d1 += a2x2 + s;
            d2 += s;
        } else if ((int)d2 < 0) {
            j++;
            a--;
            unsigned int s = (unsigned int)j * a2x4 - (unsigned int)a * b2x4;
            d1 += a2x2 + s;
            d2 += b2x2 + s;
        } else {
            a--;
            d1 -= (unsigned int)a * b2x4;
            d2 += b2x2 + (unsigned int)a * mb2x4;
        }
        if (b < j) j = b;
    }
}

/* Box from swork[0..1] to (inX, inY): outline (inMode == 0) or filled. */
void __EME_Func_DrawBox(int inX, int inY, int inMode, struct struct_fedcf029 *dlp, int ext)
{
    PS2_FRAME(0xc0);
    int x0 = dlp->swork[0];
    int y = dlp->swork[1];
    int dx, dy, sx, sy;
    if (x0 < inX) { dx = inX - x0; sx = 1; } else { dx = x0 - inX; sx = -1; }
    if (y < inY) { dy = inY - y; sy = 1; } else { dy = y - inY; sy = -1; }
    for (int j = 0; j <= dy; j++, y += sy) {
        int x = x0;
        for (int i = 0; i <= dx; i++, x += sx)
            if (inMode != 0 || i == 0 || i == dx || j == 0 || j == dy) __EME_Func_DrawPoint(x, y, 0, dlp, ext);
    }
}

/* Bresenham line from swork[0..1] to (inX, inY) with the inSize brush (point drawn before the error step). */
void __EME_Func_DrawLine(int inX, int inY, int inSize, struct struct_fedcf029 *dlp, int ext)
{
    PS2_FRAME(0xb0);
    int x = dlp->swork[0];
    int y = dlp->swork[1];
    int dx, dy, sx, sy;
    if (x < inX) { dx = inX - x; sx = 1; } else { dx = x - inX; sx = -1; }
    if (y < inY) { dy = inY - y; sy = 1; } else { dy = y - inY; sy = -1; }
    if (dx < dy) {
        int err = -dy;
        for (int k = 0; k <= dy; k++) {
            __EME_Func_DrawPoint(x, y, inSize, dlp, ext);
            err += dx * 2;
            y += sy;
            if (err >= 0) { x += sx; err -= dy * 2; }
        }
    } else {
        int err = -dx;
        for (int k = 0; k <= dx; k++) {
            __EME_Func_DrawPoint(x, y, inSize, dlp, ext);
            err += dy * 2;
            x += sx;
            if (err >= 0) { y += sy; err -= dx * 2; }
        }
    }
}

/* Eraser: DrawPoint's brush shapes, clearing edt_tbb[0] only. */
static inline void mefunc_erase_px(unsigned char *img, int x, int y)
{
    if (x >= 0 && x < 0x80 && y >= 0 && y < 0x80) img[y * 0x80 + x] = 0;
}

void __EME_Func_Eraser(int inX, int inY, int inSize, struct struct_fedcf029 *dlp)
{
    int n = inSize * 2 + 1;
    int oy = inY - n / 2;
    int ox = inX - n / 2;
    unsigned char *img = dlp->edt_tbb[0];
    if (n != 5) {
        for (int j = 0; j < n; j++)
            for (int i = 0; i < n; i++) mefunc_erase_px(img, ox + i, oy + j);
        return;
    }
    for (int i = 1; i < n - 1; i++) mefunc_erase_px(img, ox + i, oy);
    int j = 1;
    for (; j < n - 1; j++)
        for (int i = 0; i < n; i++) mefunc_erase_px(img, ox + i, oy + j);
    for (int i = 1; i < n - 1; i++) mefunc_erase_px(img, ox + i, oy + j);
}

/* ---- group 3: Grid overlay system, Gradation (Grad) system ---- */

static inline struct struct_f5e63d6f *mefunc_grid(struct stMobj *obj)
{
    return (struct struct_f5e63d6f *)((char *)obj + 0x10);
}

static inline struct struct_1649f954 *mefunc_grad(struct stMobj *obj)
{
    return (struct struct_1649f954 *)((char *)obj + 0x10);
}

/* GIF tag of a grid packet: NLOOP 0x8000 + 2 per line (EOP clear), the PRIM word, REGS 0x41 (RGBAQ, XYZ2) */
static inline unsigned int *mefunc_grid_head(unsigned int *buf, unsigned int prim)
{
    buf[0] = 0x8000;
    buf[1] = prim;
    buf[2] = 0x41;
    buf[3] = 0;
    return buf + 4;
}

/* one vertex: the RGBA template quad (lq / sq), then XYZ2 {x, y, z = 0x10, 0} */
static inline unsigned int *mefunc_grid_vtx(unsigned int *p, const unsigned int *rgba, int x, int y)
{
    p[0] = rgba[0];
    p[1] = rgba[1];
    p[2] = rgba[2];
    p[3] = rgba[3];
    p[4] = (unsigned int)x;
    p[5] = (unsigned int)y;
    p[6] = 0x10;
    p[7] = 0;
    return p + 8;
}

static inline unsigned int mefunc_grid_prim(struct struct_f5e63d6f *g)
{
    return (g->alpha == 0x80 ? 0x40u : 0u) | 0x2000c000u;
}

/* Draws the grid lines over the edit image / palette rectangle (topx, topy)-(btmx, btmy): nx + 1 vertical lines
 * (each two 1-pixel columns, x and x - 1), then ny + 1 horizontal lines (even field: two rows y - 2 and y with a
 * different PRIM word and ALPHA_1 when not fading; odd field: one row y - 1). Mode 0 cells follow rate_zoom /
 * rate_grid (no grid when a cell is under 8 pixels), mode 1 (palette) is 16 x 16. Quirks kept: the vertical loop
 * flushes the SPR packet at 0xc0 qwords, the horizontal loops at 0xc0 words; the odd-field path does not restore
 * ALPHA_1 = 0x44; the divides are unchecked (MIPS div results). */
static void __EME_DrawGrid(struct stMobj *ioObjp)
{
    PS2_FRAME(0x130);
    PS2_STACK(unsigned int, s_obj, 0xdc);
    PS2_STACK(int, s_ww, 0xc0);
    PS2_STACK(int, s_hh, 0xb0);
    PS2_STACK(int, s_topy, 0xa0);
    PS2_STACK(unsigned int[4], rgba, 0x120);
    PS2_STACK(int, s_e4, 0xe4);
    PS2_STACK(int, s_f8, 0xf8);
    PS2_STACK(int, s_10c, 0x10c);
    PS2_STACK(int, s_110, 0x110);
    s_obj = PS2PTR(ioObjp);
    struct struct_f5e63d6f *g = mefunc_grid(ioObjp);
    struct struct_fedcf029 *dlp = g->dispListp;
    int nx, ny;

    if (g->draw == 0 || ioObjp->stat == 5) return;
    if (g->mode == 0) {
        if (dlp->rate_grid == 0) dlp->rate_grid = 1;
        int rz = dlp->rate_zoom;
        int rg = dlp->rate_grid;
        int cx = ps2_div32(g->btmx - g->topx, rz);
        int cy = ps2_div32(g->btmy - g->topy, rz);
        nx = ps2_div32(cx, rg);
        ny = ps2_div32(cy, rg);
    } else {
        nx = 0x10;
        ny = 0x10;
    }
    int topx = g->topx;
    int topy = g->topy;
    int ww = ps2_div32(g->btmx - topx, nx);
    s_ww = ww;
    int hh = ps2_div32(g->btmy - topy, ny);
    s_hh = hh;
    if (g->mode == 0 && ww < 8) return;
    s_topy = topy;
    rgba[0] = 0x40;
    rgba[1] = 0x40;
    rgba[2] = 0x40;
    rgba[3] = g->alpha;
    s_e4 = 0x87;
    s_110 = 0xff;
    s_f8 = 0;
    s_10c = g->alpha;
    (void)s_obj, (void)s_ww, (void)s_hh, (void)s_topy, (void)s_e4, (void)s_f8, (void)s_10c, (void)s_110;
    if (ioObjp->stat == 1 || ioObjp->stat == 2)
        DF_MDRAW_KickReg(0x42, ((unsigned long long)g->alpha << 32) | 0x21);
    else
        DF_MDRAW_KickReg(0x42, 0x44);
    DF_MDRAW_KickReg(0x47, 0x30003);

    /* vertical lines */
    unsigned int *hdr = DF_MDRAW_GetSPRBuff();
    unsigned int *start = mefunc_grid_head(hdr, mefunc_grid_prim(g));
    unsigned int *p = start;
    for (int i = 0; i <= nx; i++) {
        int x = topx + (int)((unsigned int)i * (unsigned int)ww);
        int y0 = topy * 16 + 0x7200;
        for (int k = 0; k < 2; k++) {
            p = mefunc_grid_vtx(p, rgba, (x - k + 0x6c0) * 16, y0 + g_odevCur * 8);
            p = mefunc_grid_vtx(p, rgba, (x - k + 0x6c0) * 16,
                                (topy + g->btmy - g->topy) * 16 + 0x7200 + g_odevCur * 8);
            hdr[0] += 2;
        }
        int used = (int)((char *)p - (char *)DF_MDRAW_GetSPRBuff());
        if (used / 16 >= 0xc0) {
            DF_MDRAW_GIFKickSPR(p);
            hdr = DF_MDRAW_GetSPRBuff();
            start = p = mefunc_grid_head(hdr, mefunc_grid_prim(g));
        }
    }
    if (p != start) DF_MDRAW_GIFKickSPR(p);

    if (g_odevCur != 0) {
        /* odd field: one row per horizontal line */
        hdr = DF_MDRAW_GetSPRBuff();
        start = p = mefunc_grid_head(hdr, mefunc_grid_prim(g));
        for (int j = 0; j <= ny; j++) {
            int y = (topy + (int)((unsigned int)j * (unsigned int)hh) - 1) * 16 + 0x7200;
            p = mefunc_grid_vtx(p, rgba, (topx + 0x6c0) * 16, y);
            p = mefunc_grid_vtx(p, rgba, (topx + g->btmx - g->topx + 0x6c0) * 16, y);
            hdr[0] += 2;
            int used = (int)((char *)p - (char *)DF_MDRAW_GetSPRBuff());
            if (used / 4 >= 0xc0) {
                DF_MDRAW_GIFKickSPR(p);
                hdr = DF_MDRAW_GetSPRBuff();
                start = p = mefunc_grid_head(hdr, mefunc_grid_prim(g));
            }
        }
        if (p != start) DF_MDRAW_GIFKickSPR(p);
        return;
    }

    /* even field: two rows per horizontal line */
    if (ioObjp->stat == 0) DF_MDRAW_KickReg(0x42, (0x40ull << 32) | 0x21);
    hdr = DF_MDRAW_GetSPRBuff();
    start = p = mefunc_grid_head(hdr, 0x20a0c000);
    for (int j = 0; j <= ny; j++) {
        int y = topy + (int)((unsigned int)j * (unsigned int)hh);
        for (int k = 0; k < 2; k++) {
            int d = k == 0 ? -2 : 0;
            p = mefunc_grid_vtx(p, rgba, (topx + 0x6c0) * 16, (d + y) * 16 + 0x7200);
            p = mefunc_grid_vtx(p, rgba, (topx + g->btmx - g->topx + 0x6c0) * 16, (d + y) * 16 + 0x7200);
            hdr[0] += 2;
        }
        int used = (int)((char *)p - (char *)DF_MDRAW_GetSPRBuff());
        if (used / 4 >= 0xc0) {
            DF_MDRAW_GIFKickSPR(p);
            hdr = DF_MDRAW_GetSPRBuff();
            start = p = mefunc_grid_head(hdr, 0x20a0c000);
        }
    }
    if (p != start) DF_MDRAW_GIFKickSPR(p);
    DF_MDRAW_KickReg(0x42, 0x44);
}

/* Mode != 0 (palette grid): follows the palette window mobj (dlp->plt_wmp, dt.window.cur_rect x / y), keeping the
 * rectangle's size. */
static void __EME_PlayGrid(struct stMobj *ioObjp)
{
    struct struct_f5e63d6f *g = mefunc_grid(ioObjp);
    if (g->mode == 0) return;
    int w = g->btmx - g->topx;
    int h = g->btmy - g->topy;
    g->topx = (short)g->dispListp->plt_wmp->dt.window.cur_rect.x;
    g->btmx = (short)(g->topx + w);
    g->topy = (short)g->dispListp->plt_wmp->dt.window.cur_rect.y;
    g->btmy = (short)(g->topy + h);
}

/* alpha -= 42 for three frames, then hidden (stat 5) */
static void __EME_DrawGridFadeOut(struct stMobj *ioObjp)
{
    struct struct_f5e63d6f *g = mefunc_grid(ioObjp);
    signed char c = g->step;
    g->step = (char)(c + 1);
    if (c < 3) {
        g->alpha = (unsigned char)(g->alpha - 0x2a);
    } else {
        g->alpha = 0;
        ioObjp->stat = 5;
        ioObjp->play_func = (void (*)(void))GFN(__EME_PlayGrid);
    }
}

/* alpha += 42 for three frames, then 0x80 and visible (stat 0) */
static void __EME_DrawGridFadeIn(struct stMobj *ioObjp)
{
    struct struct_f5e63d6f *g = mefunc_grid(ioObjp);
    signed char c = g->step;
    g->step = (char)(c + 1);
    if (c < 3) {
        g->alpha = (unsigned char)(g->alpha + 0x2a);
    } else {
        g->alpha = 0x80;
        ioObjp->stat = 0;
        ioObjp->play_func = (void (*)(void))GFN(__EME_PlayGrid);
    }
}

void __SetGridSystemFadeOut(struct stMobj *ioObjp)
{
    struct struct_f5e63d6f *g = mefunc_grid(ioObjp);
    g->step = 0;
    g->alpha = 0x80;
    ioObjp->play_func = (void (*)(void))GFN(__EME_DrawGridFadeOut);
    ioObjp->stat = 2;
}

void __SetGridSystemFadeIn(struct stMobj *ioObjp)
{
    struct struct_f5e63d6f *g = mefunc_grid(ioObjp);
    g->step = 0;
    g->alpha = 0;
    ioObjp->play_func = (void (*)(void))GFN(__EME_DrawGridFadeIn);
    ioObjp->stat = 1;
}

/* Grid over the rectangle (hx, hy)-(tx, ty); mode 0 = edit image (zoom / grid rates), 1 = palette (16 x 16). */
struct stMobj *__SetNewGridSystem(struct stMobj *inObjp, struct struct_fedcf029 *dlp, int hx, int hy, int tx, int ty,
                                  int mode)
{
    PS2_FRAME(0x70);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    struct struct_f5e63d6f *g = mefunc_grid(obj);
    obj->type = 0;
    obj->stat = 0;
    g->dispListp = dlp;
    g->topx = (short)hx;
    g->topy = (short)hy;
    g->btmx = (short)tx;
    g->btmy = (short)ty;
    g->alpha = 0x80;
    g->draw = 1;
    g->mode = (char)mode;
    obj->play_func = (void (*)(void))GFN(__EME_PlayGrid);
    obj->draw_func = (void (*)(void))GFN(__EME_DrawGrid);
    obj->count = 0;
    return obj;
}

/* After the view motion: frame 0 -> 1, frame 1 restores the palette cursor saved at open and ends (stat 5). */
static void __PlayEmbGradSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x20);
    if (!DF_MVIEW_MotionSync(MViewp)) return;
    struct struct_1649f954 *gd = mefunc_grad(ioObjp);
    struct struct_fedcf029 *dlp = gd->dispListp;
    if (gd->step == 1) {
        dlp->curx[2] = gd->pcurx;
        dlp->cury[2] = gd->pcury;
        ioObjp->stat = 5;
        ioObjp->play_func = 0;
    } else if (gd->step == 0) {
        gd->step = 1;
    }
}

/* DF_MBGUIDE_Change takes a 0x63-terminated kind list (DWARF: no parameters); 8 register arguments */
static inline void mefunc_guide8(int a, int b, int c, int d, int e, int f, int g, int h)
{
    gcall_as<int(int, int, int, int, int, int, int, int)>(GFN(DF_MBGUIDE_Change), a, b, c, d, e, f, g, h);
}

/* Gradation: pick the head colour (decide), move the palette cursor, decide again -> CLUT gradient between the two
 * entries (CLUT undo snapshot first). Cancel drops the head pick or closes the system. Palette cursor: no f_lockPlt
 * test, entry (0, 0) is always skipped. Also undo swap and the grid / zoom rate keys. */
static void __PlayEmbGradSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    struct struct_1649f954 *gd = mefunc_grad(ioObjp);
    struct struct_fedcf029 *dlp = gd->dispListp;

    /* cancel */
    if (MPad[MPadUser].Rrt == 1 || eMouse->rbtn == 1) {
        Voice_Call(0x3eb, 0x7f);
        if (dlp->step == 1) {
            dlp->f_layer2 = 0;
            dlp->step = 0;
        } else {
            gd->func = 1;
            mefunc_guide8(8, 0xa, 9, 1, 2, 0, 0xd, 0x63);
            gd->dispListp->f_pltFrame = 0;
            gd->step = 0;
            gd->mct = 0;
            ioObjp->stat = 2;
            ioObjp->play_func = (void (*)(void))GFN(__PlayEmbGradSystemFadeOut);
        }
        return;
    }
    /* decide: first the head entry, then the gradient */
    if (MPad[MPadUser].Rdw == 1 || eMouse->lbtn == 1) {
        Voice_Call(0x3ea, 0x7f);
        if (dlp->step == 0) {
            dlp->f_layer2 = 1;
            dlp->step = 1;
            dlp->swork[0] = dlp->curx[2];
            dlp->swork[1] = dlp->cury[2];
        } else {
            memcpy(dlp->edt_cbb[1], dlp->edt_cbb[0], 0x400);
            dlp->f_undo = 1;
            __EME_Func_Gradate(dlp->curx[2], dlp->cury[2], dlp);
            dlp->f_layer2 = 0;
            dlp->step = 0;
        }
    }
    /* undo: swap the two CLUTs (f_undo) or the two images, MenuWorkp as scratch */
    if (MPad[MPadUser].Rlt == 1) {
        Voice_Call(0x3eb, 0x7f);
        if (dlp->f_undo) {
            memcpy(MenuWorkp, dlp->edt_cbb[0], 0x400);
            memcpy(dlp->edt_cbb[0], dlp->edt_cbb[1], 0x400);
            memcpy(dlp->edt_cbb[1], MenuWorkp, 0x400);
        } else {
            memcpy(MenuWorkp, dlp->edt_tbb[0], 0x4000);
            memcpy(dlp->edt_tbb[0], dlp->edt_tbb[1], 0x4000);
            memcpy(dlp->edt_tbb[1], MenuWorkp, 0x4000);
        }
    }

    /* right */
    if (MPad[MPadUser].repeat.rt) {
        Voice_Call(0x3e9, 0x7f);
        dlp->curx[2]++;
        if (dlp->curx[2] >= 16) dlp->curx[2] = 0;
        if (dlp->curx[2] == 0 && dlp->cury[2] == 0) dlp->curx[2] = 1;
    }
    if (dlp->old_x / 4 > 0) {
        Voice_Call(0x3e9, 0x7f);
        dlp->old_x = 0;
        dlp->curx[2]++;
        if (dlp->curx[2] >= 16) dlp->curx[2] = 15;
    }
    /* left */
    if (MPad[MPadUser].repeat.lt) {
        Voice_Call(0x3e9, 0x7f);
        dlp->curx[2]--;
        if (dlp->curx[2] < 0) dlp->curx[2] = 15;
        if (dlp->curx[2] == 0 && dlp->cury[2] == 0) dlp->curx[2] = 15;
    }
    if (dlp->old_x / 4 < 0) {
        Voice_Call(0x3e9, 0x7f);
        dlp->old_x = 0;
        dlp->curx[2]--;
        if (dlp->curx[2] < 0) dlp->curx[2] = 0;
        if (dlp->curx[2] == 0 && dlp->cury[2] == 0) dlp->curx[2] = 1;
    }
    /* up */
    if (MPad[MPadUser].repeat.up) {
        Voice_Call(0x3e9, 0x7f);
        dlp->cury[2]--;
        if (dlp->cury[2] < 0) dlp->cury[2] = 15;
        if (dlp->curx[2] == 0 && dlp->cury[2] == 0) dlp->cury[2] = 15;
    }
    if (dlp->old_y / 4 < 0) {
        Voice_Call(0x3e9, 0x7f);
        dlp->old_y = 0;
        dlp->cury[2]--;
        if (dlp->cury[2] < 0) dlp->cury[2] = 0;
        if (dlp->curx[2] == 0 && dlp->cury[2] == 0) dlp->cury[2] = 1;
    }
    /* down */
    if (MPad[MPadUser].repeat.dw) {
        Voice_Call(0x3e9, 0x7f);
        dlp->cury[2]++;
        if (dlp->cury[2] >= 16) dlp->cury[2] = 0;
        if (dlp->curx[2] == 0 && dlp->cury[2] == 0) dlp->cury[2] = 1;
    }
    if (dlp->old_y / 4 > 0) {
        Voice_Call(0x3e9, 0x7f);
        dlp->old_y = 0;
        dlp->cury[2]++;
        if (dlp->cury[2] >= 16) dlp->cury[2] = 15;
    }

    /* grid rate: L1 / middle button + wheel back doubles (1..8), middle button + wheel forward halves */
    if (MPad[MPadUser].repeat.l1 || (eMouse->wbtn > 0 && eMouse->w < 0)) {
        Voice_Call(0x3e9, 0x7f);
        gd->dispListp->rate_grid = (char)(gd->dispListp->rate_grid * 2);
        if (gd->dispListp->rate_grid >= 9) gd->dispListp->rate_grid = 1;
    }
    if (eMouse->wbtn > 0 && eMouse->w > 0) {
        Voice_Call(0x3e9, 0x7f);
        gd->dispListp->rate_grid = (char)(gd->dispListp->rate_grid / 2);
        if (gd->dispListp->rate_grid <= 0) gd->dispListp->rate_grid = 8;
    }
    /* zoom rate: R2 / wheel forward doubles (2..16), L2 / wheel back halves */
    if (MPad[MPadUser].repeat.r2 || (eMouse->wbtn == 0 && eMouse->w > 0)) {
        Voice_Call(0x3e9, 0x7f);
        gd->dispListp->rate_zoom = (char)(gd->dispListp->rate_zoom * 2);
        if (gd->dispListp->rate_zoom >= 17) gd->dispListp->rate_zoom = 2;
    }
    if (MPad[MPadUser].repeat.l2 || (eMouse->wbtn == 0 && eMouse->w < 0)) {
        Voice_Call(0x3e9, 0x7f);
        gd->dispListp->rate_zoom = (char)(gd->dispListp->rate_zoom / 2);
        if (gd->dispListp->rate_zoom < 2) gd->dispListp->rate_zoom = 16;
    }
}

/* one idle frame, then the Grad system runs */
static void __PlayEmbGradSystemFadeIn(struct stMobj *ioObjp)
{
    struct struct_1649f954 *gd = mefunc_grad(ioObjp);
    if (gd->step == 1) {
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbGradSystem);
        ioObjp->stat = 0;
    } else if (gd->step == 0) {
        gd->step = 1;
    }
}

/* Opens the Gradation system (Func row 0 col 1): palette frame on, cursor off entry (0, 0), the cursor saved for
 * the close, key guide. NULL if no mobj is free. */
struct stMobj *__SetNewGradSystem(struct stMobj *inObjp, struct struct_fedcf029 *dlp)
{
    PS2_FRAME(0x30);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    struct struct_1649f954 *gd = mefunc_grad(obj);
    obj->type = 0;
    obj->stat = 1;
    gd->dispListp = dlp;
    dlp->f_pltFrame = 1;
    if (dlp->curx[2] + dlp->cury[2] * 16 == 0) {
        dlp->curx[2] = 1;
        dlp->cury[2] = 0;
    }
    gd->mct = 0;
    gd->idx_head = 0;
    gd->idx_tail = 0;
    gd->pcurx = dlp->curx[2];
    gd->pcury = dlp->cury[2];
    mefunc_guide8(8, 0xa, 0, 1, 2, 0, 0xd, 0x63);
    dlp->f_canSpuit = 0;
    dlp->step = 0;
    obj->play_func = (void (*)(void))GFN(__PlayEmbGradSystemFadeIn);
    obj->count = 0;
    return obj;
}

/* ---- group 4: RGB colour-bar system, RGBPrev (palette pick before the RGB bars) ---- */

static inline struct struct_ab8fd377 *mefunc_rgb(struct stMobj *obj)
{
    return (struct struct_ab8fd377 *)((char *)obj + 0x10);
}

/* Shared input block (B): undo on Rlt swaps the two CLUTs (f_undo) or the two images, MenuWorkp as scratch. */
static inline void mefunc_undo_swap(struct struct_fedcf029 *dlp)
{
    if (MPad[MPadUser].Rlt == 1) {
        Voice_Call(0x3eb, 0x7f);
        if (dlp->f_undo) {
            memcpy(MenuWorkp, dlp->edt_cbb[0], 0x400);
            memcpy(dlp->edt_cbb[0], dlp->edt_cbb[1], 0x400);
            memcpy(dlp->edt_cbb[1], MenuWorkp, 0x400);
        } else {
            memcpy(MenuWorkp, dlp->edt_tbb[0], 0x4000);
            memcpy(dlp->edt_tbb[0], dlp->edt_tbb[1], 0x4000);
            memcpy(dlp->edt_tbb[1], MenuWorkp, 0x4000);
        }
    }
}

/* Shared input block (A): grid rate (L1 / middle button + wheel back doubles 1..8, middle button + wheel forward
 * halves) and zoom rate (R2 / wheel forward doubles 2..16, L2 / wheel back halves). */
static inline void mefunc_rate_keys(struct struct_fedcf029 *dlp)
{
    if (MPad[MPadUser].repeat.l1 || (eMouse->wbtn > 0 && eMouse->w < 0)) {
        Voice_Call(0x3e9, 0x7f);
        dlp->rate_grid = (char)(dlp->rate_grid * 2);
        if (dlp->rate_grid >= 9) dlp->rate_grid = 1;
    }
    if (eMouse->wbtn > 0 && eMouse->w > 0) {
        Voice_Call(0x3e9, 0x7f);
        dlp->rate_grid = (char)(dlp->rate_grid / 2);
        if (dlp->rate_grid <= 0) dlp->rate_grid = 8;
    }
    if (MPad[MPadUser].repeat.r2 || (eMouse->wbtn == 0 && eMouse->w > 0)) {
        Voice_Call(0x3e9, 0x7f);
        dlp->rate_zoom = (char)(dlp->rate_zoom * 2);
        if (dlp->rate_zoom >= 17) dlp->rate_zoom = 2;
    }
    if (MPad[MPadUser].repeat.l2 || (eMouse->wbtn == 0 && eMouse->w < 0)) {
        Voice_Call(0x3e9, 0x7f);
        dlp->rate_zoom = (char)(dlp->rate_zoom / 2);
        if (dlp->rate_zoom < 2) dlp->rate_zoom = 16;
    }
}

/* After the view motion: frame 0 deletes the bars, then (same call) writes the edited colour back on decide and
 * ends (stat 5). */
static void __PlayEmbRGBSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    if (!DF_MVIEW_MotionSync(MViewp)) return;
    struct struct_ab8fd377 *rd = mefunc_rgb(ioObjp);
    if (rd->step == 0) {
        __DeleteRgbCtrl(&rd->rgb_ctrl);
        rd->mct = 0;
        rd->step++;
    }
    if (rd->step == 1) {
        if (rd->func) memcpy(rd->p_col, &rd->col, 4);
        ioObjp->stat = 5;
        ioObjp->play_func = 0;
    }
}

/* Close the RGB bars: key guide, fade the window and the three channel rows out, the sub-icons back in. */
static void __SetEmbRGBSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x70);
    struct struct_ab8fd377 *rd = mefunc_rgb(ioObjp);
    struct struct_fedcf029 *dlp = rd->dispListp;
    mefunc_guide8(8, 0xa, 0, 1, 2, 0, 0xd, 0x63);
    UnitSprite_SetFadeOut(rd->rgb_ctrl.p_window);
    for (int i = 0; i < 3; i++) {
        struct struct_0068c5e3 *bar = &rd->rgb_ctrl.a_color_bar[i];
        DF_MOBJ_SetFadeOut(bar->p_bar);
        DF_MOBJ_SetFadeOut(bar->p_name);
        DF_MOBJ_SetFadeOut(bar->p_value);
        UnitSprite_SetFadeOut(bar->p_scroll);
    }
    for (int j = 0; j < 6; j++) {
        struct struct_7186d2e3 *ic = &dlp->a_sub_icon[j];
        if (ic->p_button) {
            UnitSprite_SetFadeIn(ic->p_button);
            UnitSprite_SetFadeIn(ic->p_shadow);
            DF_MOBJ_SetFadeIn(ic->p_image);
        }
    }
    DF_MOBJ_SetDrawEnable(dlp->p_sub_icon_light, 1);
    rd->step = 0;
    ioObjp->stat = 2;
    ioObjp->play_func = (void (*)(void))GFN(__PlayEmbRGBSystemFadeOut);
}

/* RGB bars: Start shows the key guide; otherwise the bars run, Rrt / right button closes keeping the edit
 * (func = 1), Rdw / left button closes reverting (func = 0) - no return after it, the undo / rate keys still run. */
static void __PlayEmbRGBSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    struct struct_ab8fd377 *rd = mefunc_rgb(ioObjp);
    struct struct_fedcf029 *dlp = rd->dispListp;
    if (MenuKeyGuide_IsShown() == 1) return;
    if (MPad[MPadUser].start == 1) {
        MenuKeyGuide_Show(0x38);
        return;
    }
    __UpdateRgbCtrl(&rd->rgb_ctrl);
    if (MPad[MPadUser].Rrt == 1 || eMouse->rbtn == 1) {
        Voice_Call(0x3eb, 0x7f);
        rd->func = 1;
        __SetEmbRGBSystemFadeOut(ioObjp);
        return;
    }
    if (MPad[MPadUser].Rdw == 1 || eMouse->lbtn == 1) {
        Voice_Call(0x3ea, 0x7f);
        rd->func = 0;
        __SetEmbRGBSystemFadeOut(ioObjp);
    }
    mefunc_undo_swap(dlp);
    mefunc_rate_keys(rd->dispListp);
}

/* frame 0 idle, frame 1 builds the bars and fades them in, frame 2 the RGB system runs */
static void __PlayEmbRGBSystemFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    struct struct_ab8fd377 *rd = mefunc_rgb(ioObjp);
    if (rd->step == 2) {
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbRGBSystem);
        ioObjp->stat = 0;
    } else if (rd->step == 1) {
        rd->step = 2;
        __CreateRgbCtrl(ioObjp, &rd->rgb_ctrl, rd->p_col);
        UnitSprite_SetFadeIn(rd->rgb_ctrl.p_window);
        for (int i = 0; i < 3; i++) {
            struct struct_0068c5e3 *bar = &rd->rgb_ctrl.a_color_bar[i];
            DF_MOBJ_SetFadeIn(bar->p_bar);
            DF_MOBJ_SetFadeIn(bar->p_name);
            DF_MOBJ_SetFadeIn(bar->p_value);
            UnitSprite_SetFadeIn(bar->p_scroll);
        }
    } else if (rd->step == 0) {
        rd->mct = 0;
        rd->step++;
    }
}

/* Palette cursor -> CLUT entry (un-swizzled; entry 0 moves the cursor to 1), shared by both constructors: CLUT undo
 * snapshot, working colour = entry rgb + alpha 0x80, p_col = the entry. */
static inline void mefunc_rgb_pick(struct struct_ab8fd377 *rd, struct struct_fedcf029 *dlp)
{
    int ci = mefunc_clut_swizzle(dlp->curx[2] + dlp->cury[2] * 16);
    if (ci == 0) {
        ci = 1;
        dlp->curx[2] = 1;
        dlp->cury[2] = 0;
    }
    dlp->f_pltFrame = 1;
    memcpy(dlp->edt_cbb[1], dlp->edt_cbb[0], 0x400);
    dlp->f_undo = 1;
    rd->idx = 0;
    rd->col.r = dlp->edt_cbb[0][ci * 4 + 0];
    rd->col.g = dlp->edt_cbb[0][ci * 4 + 1];
    rd->col.b = dlp->edt_cbb[0][ci * 4 + 2];
    rd->col.a = 0x80;
    rd->p_col = (struct struct_76b810ec *)&dlp->edt_cbb[0][ci * 4];
    dlp->step = 0;
}

/* Opens the RGB bars on the picked palette entry (from RGBPrev's decide): sub-icons fade out, key guide.
 * NULL if no mobj is free. */
struct stMobj *__SetNewRGBSystem(struct stMobj *inObjp, struct struct_fedcf029 *dlp)
{
    PS2_FRAME(0x60);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    struct struct_ab8fd377 *rd = mefunc_rgb(obj);
    obj->type = 0;
    obj->stat = 1;
    rd->dispListp = dlp;
    mefunc_rgb_pick(rd, dlp);
    for (int j = 0; j < 6; j++) {
        struct struct_7186d2e3 *ic = &dlp->a_sub_icon[j];
        if (ic->p_button) {
            UnitSprite_SetFadeOut(ic->p_button);
            UnitSprite_SetFadeOut(ic->p_shadow);
            DF_MOBJ_SetFadeOut(ic->p_image);
        }
    }
    DF_MOBJ_SetDrawEnable(dlp->p_sub_icon_light, 0);
    mefunc_guide8(8, 0xa, 9, 1, 2, 0, 0xd, 0x63);
    dlp->f_canSpuit = 0;
    obj->play_func = (void (*)(void))GFN(__PlayEmbRGBSystemFadeIn);
    obj->count = 0;
    return obj;
}

/* Waits for the RGB child to finish (stat 5), erases it and the palette pick runs again. The child's stat is read
 * before the NULL test (SetNewRGBSystem may have failed). */
static void __PlayEmbRGBSystemPrevDispatch(struct stMobj *ioObjp)
{
    struct struct_ab8fd377 *rd = mefunc_rgb(ioObjp);
    struct struct_fedcf029 *dlp = rd->dispListp;
    struct stMobj *child = rd->csysp;
    unsigned char st = child ? child->stat : ((const unsigned char *)PS2_NULL_MEM())[2];
    if (st != 5) return;
    if (child) {
        child->f_draw = 0;
        rd->csysp->f_erase = 1;
        rd->csysp = 0;
    }
    dlp->step = 0;
    ioObjp->play_func = (void (*)(void))GFN(__PlayEmbRGBSystemPrev);
    ioObjp->stat = 0;
}

/* After the view motion: decide (func) opens the RGB bars as a child and waits in Dispatch; cancel takes one more
 * frame and ends (stat 5). */
static void __PlayEmbRGBSystemPrevFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    if (!DF_MVIEW_MotionSync(MViewp)) return;
    struct struct_ab8fd377 *rd = mefunc_rgb(ioObjp);
    struct struct_fedcf029 *dlp = rd->dispListp;
    if (rd->step == 1) {
        ioObjp->stat = 5;
        ioObjp->play_func = 0;
    } else if (rd->step == 0) {
        if (rd->func) {
            rd->csysp = __SetNewRGBSystem(ioObjp, dlp);
            ioObjp->stat = 3;
            ioObjp->play_func = (void (*)(void))GFN(__PlayEmbRGBSystemPrevDispatch);
        } else {
            rd->step++;
        }
    }
}

/* Palette pick for the RGB bars (Func row 0 col 0). Rrt / right button cancels (returns), Rdw / left button decides
 * (no return: the remaining keys still run this frame). The func store + "func == 0" test is dead logic (always true
 * on cancel, always false on decide). Palette cursor with the f_lockPlt rule (entry (0, 0) skipped while locked);
 * the chosen index goes to idx. */
static void __PlayEmbRGBSystemPrev(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    struct struct_ab8fd377 *rd = mefunc_rgb(ioObjp);
    struct struct_fedcf029 *dlp = rd->dispListp;

    if (MPad[MPadUser].Rrt == 1 || eMouse->rbtn == 1) {
        Voice_Call(0x3eb, 0x7f);
        rd->func = 0;
        if (rd->func == 0) {
            rd->dispListp->f_pltFrame = 0;
            rd->mct = 0;
        }
        rd->step = 0;
        ioObjp->stat = 2;
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbRGBSystemPrevFadeOut);
        return;
    }
    if (MPad[MPadUser].Rdw == 1 || eMouse->lbtn == 1) {
        Voice_Call(0x3ea, 0x7f);
        rd->func = 1;
        if (rd->func == 0) {
            rd->dispListp->f_pltFrame = 0;
            rd->mct = 0;
        }
        rd->step = 0;
        ioObjp->stat = 2;
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbRGBSystemPrevFadeOut);
    }
    mefunc_undo_swap(dlp);

    /* right */
    if (MPad[MPadUser].repeat.rt) {
        Voice_Call(0x3e9, 0x7f);
        dlp->curx[2]++;
        if (dlp->curx[2] >= 16) dlp->curx[2] = 0;
        if (dlp->f_lockPlt && dlp->curx[2] == 0 && dlp->cury[2] == 0) dlp->curx[2] = 1;
    }
    if (dlp->old_x / 4 > 0) {
        Voice_Call(0x3e9, 0x7f);
        dlp->old_x = 0;
        dlp->curx[2]++;
        if (dlp->curx[2] >= 16) dlp->curx[2] = 15;
    }
    /* left */
    if (MPad[MPadUser].repeat.lt) {
        Voice_Call(0x3e9, 0x7f);
        dlp->curx[2]--;
        if (dlp->curx[2] < 0) dlp->curx[2] = 15;
        if (dlp->f_lockPlt && dlp->curx[2] == 0 && dlp->cury[2] == 0) dlp->curx[2] = 15;
    }
    if (dlp->old_x / 4 < 0) {
        Voice_Call(0x3e9, 0x7f);
        dlp->old_x = 0;
        dlp->curx[2]--;
        if (dlp->curx[2] < 0) dlp->curx[2] = 0;
        if (dlp->f_lockPlt && dlp->curx[2] == 0 && dlp->cury[2] == 0) dlp->curx[2] = 1;
    }
    /* up */
    if (MPad[MPadUser].repeat.up) {
        Voice_Call(0x3e9, 0x7f);
        dlp->cury[2]--;
        if (dlp->cury[2] < 0) dlp->cury[2] = 15;
        if (dlp->f_lockPlt && dlp->curx[2] == 0 && dlp->cury[2] == 0) dlp->cury[2] = 15;
    }
    if (dlp->old_y / 4 < 0) {
        Voice_Call(0x3e9, 0x7f);
        dlp->old_y = 0;
        dlp->cury[2]--;
        if (dlp->cury[2] < 0) dlp->cury[2] = 0;
        if (dlp->f_lockPlt && dlp->curx[2] == 0 && dlp->cury[2] == 0) dlp->cury[2] = 1;
    }
    /* down */
    if (MPad[MPadUser].repeat.dw) {
        Voice_Call(0x3e9, 0x7f);
        dlp->cury[2]++;
        if (dlp->cury[2] >= 16) dlp->cury[2] = 0;
        if (dlp->f_lockPlt && dlp->curx[2] == 0 && dlp->cury[2] == 0) dlp->cury[2] = 1;
    }
    if (dlp->old_y / 4 > 0) {
        Voice_Call(0x3e9, 0x7f);
        dlp->old_y = 0;
        dlp->cury[2]++;
        if (dlp->cury[2] >= 16) dlp->cury[2] = 15;
    }

    mefunc_rate_keys(rd->dispListp);
    rd->idx = (short)(dlp->curx[2] + dlp->cury[2] * 16);
}

/* frame 0 idle, frame 1: palette called in pick mode (f_callPalette 2), palette locked, the pick runs */
static void __PlayEmbRGBSystemPrevFadeIn(struct stMobj *ioObjp)
{
    struct struct_ab8fd377 *rd = mefunc_rgb(ioObjp);
    struct struct_fedcf029 *dlp = rd->dispListp;
    if (rd->step == 1) {
        dlp->f_callPalette = 2;
        dlp->f_lockPlt = 1;
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbRGBSystemPrev);
        ioObjp->stat = 0;
    } else if (rd->step == 0) {
        rd->mct = 0;
        rd->step++;
    }
}

/* Opens the palette pick for the RGB bars (Func row 0 col 0): as __SetNewRGBSystem without the sub-icon fade.
 * NULL if no mobj is free. */
struct stMobj *__SetNewRGBSystemPrev(struct stMobj *inObjp, struct struct_fedcf029 *dlp)
{
    PS2_FRAME(0x50);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    struct struct_ab8fd377 *rd = mefunc_rgb(obj);
    obj->type = 0;
    obj->stat = 1;
    rd->dispListp = dlp;
    mefunc_rgb_pick(rd, dlp);
    mefunc_guide8(8, 0xa, 0, 1, 2, 0, 0xd, 0x63);
    dlp->f_canSpuit = 0;
    obj->play_func = (void (*)(void))GFN(__PlayEmbRGBSystemPrevFadeIn);
    obj->count = 0;
    return obj;
}

/* ---- group 5a: Text stamp support, Rot / Clear, Shift system ---- */

static inline struct struct_4ece179d *mefunc_text(struct stMobj *obj)
{
    return (struct struct_4ece179d *)((char *)obj + 0x10);
}

/* OLMENU 0x1edd290 / 0x1edd2a0 / 0x1edd2b0: layout part names (only read during the UnitSprite_New call) */
static char mefunc_parts_moji_kage[] = "moji_kage_0";
static char mefunc_parts_moji_ita[] = "moji_ita_0";
static char mefunc_parts_hikari_moji[] = "hikari_moji_0";

/* Scrolls the zoomed view one step per frame towards the side(s) the stamp cursor left the image on (f_edtFrame bits
 * 1 / 2 / 4 / 8 = left / right / top / bottom), for 16 frames or until both axes hit their limit. */
static void __PlayEmbTextSystemMove(struct stMobj *ioObjp)
{
    struct struct_4ece179d *td = mefunc_text(ioObjp);
    int moved = 0;
    if (td->func == 0) {
        td->func = 1;
        return;
    }
    if (td->func != 1) return;
    td->step++;
    if (td->step >= 0x11) {
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbTextSystem);
        ioObjp->stat = 0;
        return;
    }
    char f = td->dispListp->f_edtFrame;
    if (f & 1) {
        td->dispListp->top_curx--;
        if (td->dispListp->top_curx < 0) {
            td->dispListp->top_curx = 0;
            moved++;
        }
    } else if (f & 2) {
        td->dispListp->top_curx++;
        int lim = 0x80 - 0x100 / td->dispListp->rate_zoom;
        if (lim < td->dispListp->top_curx) {
            td->dispListp->top_curx = (short)lim;
            moved++;
        }
    }
    f = td->dispListp->f_edtFrame;
    if (f & 4) {
        td->dispListp->top_cury--;
        if (td->dispListp->top_cury < 0) {
            td->dispListp->top_cury = 0;
            moved++;
        }
    } else if (f & 8) {
        td->dispListp->top_cury++;
        int lim = 0x80 - 0x100 / td->dispListp->rate_zoom;
        if (lim < td->dispListp->top_cury) {
            td->dispListp->top_cury = (short)lim;
            moved++;
        }
    }
    if (moved == 2) {
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbTextSystem);
        ioObjp->stat = 0;
    }
}

/* Waits for the palette child to finish (stat 5), erases it and the stamp system runs again. The child's stat is
 * read before the NULL test. */
static void __PlayEmbTextSystemDispatch(struct stMobj *ioObjp)
{
    struct struct_4ece179d *td = mefunc_text(ioObjp);
    struct stMobj *child = td->csysp;
    unsigned char st = child ? child->stat : ((const unsigned char *)PS2_NULL_MEM())[2];
    if (st != 5) return;
    if (child) {
        child->f_draw = 0;
        td->csysp->f_erase = 1;
        td->csysp = 0;
    }
    ioObjp->play_func = (void (*)(void))GFN(__PlayEmbTextSystem);
    ioObjp->stat = 0;
}

/* After the view motion: delete the glyph plates and the light, sub-icons back in, free the plate array, end. */
static void __PlayEmbTextSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    if (!DF_MVIEW_MotionSync(MViewp)) return;
    struct struct_4ece179d *td = mefunc_text(ioObjp);
    for (int i = 0; i < td->max_y; i++) {
        for (int j = 0; j < td->max_x; j++) {
            UnitSprite_Delete(td->p_icon[j + i * td->max_x].p_button);
            UnitSprite_Delete(td->p_icon[j + i * td->max_x].p_shadow);
            struct stMobj *img = td->p_icon[j + i * td->max_x].p_image;
            if (img) {
                img->f_draw = 0;
                td->p_icon[j + i * td->max_x].p_image->f_erase = 1;
                td->p_icon[j + i * td->max_x].p_image = 0;
            }
        }
    }
    UnitSprite_Delete(td->p_icon_light);
    for (int k = 0; k < 6; k++) {
        if (td->dispListp->a_sub_icon[k].p_button) {
            UnitSprite_SetFadeIn(td->dispListp->a_sub_icon[k].p_button);
            UnitSprite_SetFadeIn(td->dispListp->a_sub_icon[k].p_shadow);
            DF_MOBJ_SetFadeIn(td->dispListp->a_sub_icon[k].p_image);
        }
    }
    DF_MOBJ_SetDrawEnable(td->dispListp->p_sub_icon_light, 1);
    td->step++;
    ioObjp->stat = 5;
    ioObjp->play_func = 0;
    DF_MMM_Free(0, td->p_icon);
    td->p_icon = 0;
}

/* Close the stamp system: fade the glyph plates out, hide the light, key guide. */
static void __SetEmbTextSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct struct_4ece179d *td = mefunc_text(ioObjp);
    for (int i = 0; i < td->max_y; i++) {
        for (int j = 0; j < td->max_x; j++) {
            UnitSprite_SetFadeOut(td->p_icon[j + i * td->max_x].p_button);
            UnitSprite_SetFadeOut(td->p_icon[j + i * td->max_x].p_shadow);
            DF_MOBJ_SetFadeOut(td->p_icon[j + i * td->max_x].p_image);
        }
    }
    DF_MOBJ_SetDrawEnable(td->p_icon_light, 0);
    mefunc_guide8(8, 0xa, 9, 1, 2, 0, 0xd, 0x63);
    td->step = 0;
    ioObjp->stat = 2;
    ioObjp->play_func = (void (*)(void))GFN(__PlayEmbTextSystemFadeOut);
}

/* ---- group 5b: Text stamp play ---- */

/* Stamp cursor (dlp->step != 0) moves one image pixel per key / mouse step while it is inside the image; outside
 * (f_edtCur != 0) it moves the screen cursor curx[3] / cury[3] by 2 up to the clamps. */
static inline void mefunc_text_right(struct struct_fedcf029 *dlp)
{
    Voice_Call(0x3e9, 0x7f);
    if (dlp->f_edtCur) {
        dlp->curx[3] += 2;
        if (dlp->curx[3] >= 0x261) dlp->curx[3] = 0x260;
    } else {
        short x = dlp->curx[0];
        if (x < dlp->top_curx + 0xff / dlp->rate_zoom)
            dlp->curx[0] = x + 1;
        else
            dlp->curx[3] = 0x260;
    }
}

static inline void mefunc_text_left(struct struct_fedcf029 *dlp)
{
    Voice_Call(0x3e9, 0x7f);
    if (dlp->f_edtCur) {
        dlp->curx[3] -= 2;
        if (dlp->curx[3] < 0x15f) dlp->curx[3] = 0x15f;
    } else {
        short x = dlp->curx[0];
        if (dlp->top_curx < x)
            dlp->curx[0] = x - 1;
        else
            dlp->curx[3] = 0x15f;
    }
}

static inline void mefunc_text_down(struct struct_fedcf029 *dlp)
{
    Voice_Call(0x3e9, 0x7f);
    if (dlp->f_edtCur) {
        dlp->cury[3] += 2;
        if (dlp->cury[3] >= 0x139) dlp->cury[3] = 0x138;
    } else {
        short y = dlp->cury[0];
        if (y < dlp->top_cury + 0xff / dlp->rate_zoom)
            dlp->cury[0] = y + 1;
        else
            dlp->cury[3] = 0x138;
    }
}

static inline void mefunc_text_up(struct struct_fedcf029 *dlp)
{
    Voice_Call(0x3e9, 0x7f);
    if (dlp->f_edtCur) {
        dlp->cury[3] -= 2;
        if (dlp->cury[3] < 0x37) dlp->cury[3] = 0x37;
    } else {
        short y = dlp->cury[0];
        if (dlp->top_cury < y)
            dlp->cury[0] = y - 1;
        else
            dlp->cury[3] = 0x37;
    }
}

/* f_edtCur = which side(s) of the image rectangle the screen cursor curx[3] / cury[3] is outside of: bits
 * 1 / 2 / 4 / 8 = left / right / top / bottom (Text and Mirror Play). */
static inline void mefunc_edtcur_update(struct struct_fedcf029 *dlp)
{
    if (dlp->curx[3] < 0x160) dlp->f_edtCur |= 1; else dlp->f_edtCur &= ~1;
    if (dlp->curx[3] >= 0x260) dlp->f_edtCur |= 2; else dlp->f_edtCur &= ~2;
    if (dlp->cury[3] < 0x38) dlp->f_edtCur |= 4; else dlp->f_edtCur &= ~4;
    if (dlp->cury[3] >= 0x138) dlp->f_edtCur |= 8; else dlp->f_edtCur &= ~8;
}

/* Image <-> screen cursor sync by the f_edtCur bits from frame start (old_cur), Text and Mirror Play tails. */
static inline void mefunc_cursor_sync(struct struct_fedcf029 *dlp, char old_cur)
{
    if (old_cur == 0) {
        /* the cursor left the image this frame: screen cursor from the image cursor, pinned past the crossed edge */
        if (dlp->f_edtCur) {
            dlp->curx[3] = (short)(dlp->rate_zoom * (dlp->curx[0] - dlp->top_curx) + 0x160);
            if (dlp->f_edtCur & 1) dlp->curx[3] = 0x15f;
            if (dlp->f_edtCur & 2) dlp->curx[3] = 0x261;
            dlp->cury[3] = (short)(dlp->rate_zoom * (dlp->cury[0] - dlp->top_cury) + 0x38);
            if (dlp->f_edtCur & 4) dlp->cury[3] = 0x37;
            if (dlp->f_edtCur & 8) dlp->cury[3] = 0x139;
        }
        return;
    }
    /* the cursor was outside at frame start: image cursor from the screen cursor, clamped to the visible part */
    dlp->curx[0] = (short)(dlp->top_curx + (dlp->curx[3] - 0x160) / dlp->rate_zoom);
    if (old_cur & 1) dlp->curx[0] = dlp->top_curx;
    if (old_cur & 2) dlp->curx[0] = (short)(dlp->top_curx + 0xff / dlp->rate_zoom);
    dlp->cury[0] = (short)(dlp->top_cury + (dlp->cury[3] - 0x38) / dlp->rate_zoom);
    if (old_cur & 4) dlp->cury[0] = dlp->top_cury;
    if (old_cur & 8) dlp->cury[0] = (short)(dlp->top_cury + 0xff / dlp->rate_zoom);
    if (dlp->curx[0] < 0) dlp->curx[0] = 0;
    {
        short x = dlp->curx[0];
        int lim = dlp->top_curx + 0xff / dlp->rate_zoom;
        if (lim < x) dlp->curx[0] = (short)lim;
    }
    if (dlp->cury[0] < 0) dlp->cury[0] = 0;
    {
        short y = dlp->cury[0];
        int lim = dlp->top_cury + 0xff / dlp->rate_zoom;
        if (lim < y) dlp->cury[0] = (short)lim;
    }
}

/* Stamps glyph (td->curx, td->cury) of the stamp texture of coidx at image cursor curx[0] / cury[0] in palette
 * colour curx[2] + 16 * cury[2] (non-zero texels only, clipped at the image's right / bottom edge); undo = old image.
 * Tables = OLMENU 0x1edd1a0 (cells per row), 0x1edd1c0 (texcoord origin), 0x1edd1f0 (glyph size), 0x1edd210 (tm2). */
static inline void mefunc_text_stamp(struct struct_4ece179d *td, struct struct_fedcf029 *dlp)
{
    const int cells[6] = {0x20, 0x10, 0xa, 0x20, 0x10, 0xa};
    const struct struct_214acd28 org[6] = {{0, 0xa8}, {0, 0x78}, {0, 0}, {0, 0xa8}, {0, 0x78}, {0, 0}};
    const int size[6] = {8, 0x10, 0x18, 8, 0x10, 0x18};
    const int texid[6] = {0x452, 0x452, 0x452, 0x451, 0x451, 0x451};

    memcpy(dlp->edt_tbb[1], dlp->edt_tbb[0], 0x4000);
    dlp->f_undo = 0;
    int glyph = td->curx + td->cury * td->max_x;
    unsigned char col = (unsigned char)(dlp->curx[2] + dlp->cury[2] * 16);
    int c = dlp->coidx;
    int sx = org[c].x + (glyph % cells[c]) * size[c];
    int sy = org[c].y + (glyph / cells[c]) * size[c];
    for (int j = 0; j < size[(int)dlp->coidx] && dlp->cury[0] + j < 0x80; j++) {
        for (int i = 0; i < size[(int)dlp->coidx] && dlp->curx[0] + i < 0x80; i++) {
            if (__EME_GetTM2Index(texid[(int)dlp->coidx], sx + i, sy + j))
                dlp->edt_tbb[0][(dlp->cury[0] + j) * 0x80 + dlp->curx[0] + i] = col;
        }
    }
}

/* Text stamp system. Cancel (Rrt / right button): leaves stamp mode, or closes the system when not stamping.
 * Decide (Rdw / left button): enters stamp mode (cursor box = glyph size), stamps, or scrolls the view (Move) when
 * the cursor is outside the image. Rup = eyedropper while stamping; Rlt = undo. Not stamping: the pad / mouse steps
 * pick the glyph (wrap on keys, clamp on mouse); stamping: they move the stamp cursor. R1 / f_callPalette 1 opens
 * the palette (Dispatch waits for it). Then the shared grid / zoom keys, the light over the picked glyph, and the
 * image <-> screen cursor sync (by the f_edtCur bits from frame start). */
static void __PlayEmbTextSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x160);
    struct struct_4ece179d *td = mefunc_text(ioObjp);
    struct struct_fedcf029 *dlp = td->dispListp;
    short old_idx = td->idx;
    char old_cur = dlp->f_edtCur;

    if (MPad[MPadUser].Rrt == 1 || eMouse->rbtn == 1) {
        Voice_Call(0x3eb, 0x7f);
        if (dlp->step == 0) {
            td->func = 0;
            __SetEmbTextSystemFadeOut(ioObjp);
            return;
        }
        mefunc_guide8(8, 0xa, 9, 1, 2, 0, 0xd, 0x63);
        dlp->f_canSpuit = 0;
        dlp->step = 0;
        dlp->f_layer = 0;
    }
    if (MPad[MPadUser].Rdw == 1 || eMouse->lbtn == 1) {
        Voice_Call(0x3ea, 0x7f);
        if (dlp->step == 0) {
            const short box[6] = {8, 0x10, 0x18, 8, 0x10, 0x18}; /* OLMENU 0x1edd190 */
            mefunc_guide8(8, 0xa, 9, 0xb, 2, 0xc, 0xd, 0x63);
            dlp->f_canSpuit = 1;
            dlp->swork[2] = box[(int)dlp->coidx];
            dlp->swork[3] = box[(int)dlp->coidx];
            dlp->step = 1;
            dlp->f_layer = 1;
        } else if (dlp->f_edtCur) {
            td->step = 0;
            td->func = 0;
            ioObjp->stat = 1;
            ioObjp->play_func = (void (*)(void))GFN(__PlayEmbTextSystemMove);
            return;
        } else {
            mefunc_text_stamp(td, dlp);
            return;
        }
    }
    if (MPad[MPadUser].Rup == 1 && dlp->f_canSpuit) {
        Voice_Call(0x3ea, 0x7f);
        __EME_ChangeColor2Idx(dlp->curx[0] + dlp->cury[0] * 0x80, dlp);
        return;
    }
    mefunc_undo_swap(dlp);

    if (dlp->step == 0) {
        /* glyph pick */
        if (MPad[MPadUser].repeat.rt) {
            Voice_Call(0x3e9, 0x7f);
            td->curx++;
            if (!(td->curx < td->max_x)) td->curx = 0;
        }
        if (dlp->old_x / 4 > 0) {
            Voice_Call(0x3e9, 0x7f);
            dlp->old_x = 0;
            td->curx++;
            if (!(td->curx < td->max_x)) td->curx = td->max_x - 1;
        }
        if (MPad[MPadUser].repeat.lt) {
            Voice_Call(0x3e9, 0x7f);
            td->curx--;
            if (td->curx < 0) td->curx = td->max_x - 1;
        }
        if (dlp->old_x / 4 < 0) {
            Voice_Call(0x3e9, 0x7f);
            dlp->old_x = 0;
            td->curx--;
            if (td->curx < 0) td->curx = 0;
        }
        if (MPad[MPadUser].repeat.dw) {
            Voice_Call(0x3e9, 0x7f);
            td->cury++;
            if (!(td->cury < td->max_y)) td->cury = 0;
        }
        if (dlp->old_y / 4 > 0) {
            Voice_Call(0x3e9, 0x7f);
            dlp->old_y = 0;
            td->cury++;
            if (!(td->cury < td->max_y)) td->cury = td->max_y - 1;
        }
        if (MPad[MPadUser].repeat.up) {
            Voice_Call(0x3e9, 0x7f);
            td->cury--;
            if (td->cury < 0) td->cury = td->max_y - 1;
        }
        if (dlp->old_y / 4 < 0) {
            Voice_Call(0x3e9, 0x7f);
            dlp->old_y = 0;
            td->cury--;
            if (td->cury < 0) td->cury = 0;
        }
    } else {
        /* stamp cursor */
        if (ePad.rt) mefunc_text_right(dlp);
        if (eMouse->x > 0) mefunc_text_right(dlp);
        if (ePad.lt) mefunc_text_left(dlp);
        if (eMouse->x < 0) mefunc_text_left(dlp);
        if (ePad.dw) mefunc_text_down(dlp);
        if (eMouse->y > 0) mefunc_text_down(dlp);
        if (ePad.up) mefunc_text_up(dlp);
        if (eMouse->y < 0) mefunc_text_up(dlp);
    }

    mefunc_edtcur_update(dlp);

    if (MPad[MPadUser].repeat.r1 || dlp->f_callPalette == 1) {
        Voice_Call(0x3ea, 0x7f);
        dlp->f_callPalette = 2;
        td->csysp = __SetNewPaletteSystem(ioObjp, td->dispListp);
        td->func = 1;
        ioObjp->stat = 3;
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbTextSystemDispatch);
        return;
    }
    mefunc_rate_keys(td->dispListp);

    td->idx = (short)(td->curx + td->cury * td->max_x);
    if (td->idx != old_idx) {
        const struct struct_214acd28 off = {-4, -4}; /* main-ELF sdata 0x27b6f8 */
        struct struct_214acd28 pos;
        UnitSprite_GetPosition(td->p_icon[td->idx].p_button, &pos);
        pos.x = pos.x + off.x;
        pos.y = pos.y + off.y;
        UnitSprite_SetPosition(td->p_icon_light, &pos);
    }

    mefunc_cursor_sync(dlp, old_cur);
}

/* step 0 -> 1 -> 2; step 2 builds the max_x x max_y glyph grid (shadow + plate unit sprites, glyph = NX sprite cut
 * from the 12-per-row 0x12 x 0x12 cells of the stamp texture of char_type) and the blinking light over plate 0;
 * step 3 runs the stamp system. Geometry = main-ELF sdata 0x27b6a8..0x27b6f4 (copied to the stack). */
static void __PlayEmbTextSystemFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x190);
    struct struct_4ece179d *td = mefunc_text(ioObjp);
    if (td->step == 3) {
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbTextSystem);
        ioObjp->stat = 0;
        return;
    }
    if (td->step == 1 || td->step == 0) {
        td->step = td->step + 1;
        return;
    }
    if (td->step != 2) return;

    const struct struct_214acd28 icon_base_pos = {0x16, 0xcf};
    const struct struct_214acd28 icon_offset = {0x26, 0x20};
    const struct struct_214acd28 char_offst = {4, 4};
    const struct struct_214acd28 shadow_offset = {-5, -2};
    const struct struct_214acd28 texcoord_base = {0, 0xb8};
    const struct struct_214acd28 tex_size = {0x12, 0x12};
    const struct struct_214acd28 tex_num = {0xc, 4};
    struct struct_76b810ec char_color = {0x80, 0x80, 0x80, 0x80};
    const struct struct_214acd28 icon_light_offset = {-4, -4};
    const int a_char_type_tex_id[2] = {0x452, 0x451};
    struct struct_214acd28 pos, spos;
    alignas(16) struct stMenuRect rect;
    alignas(16) int uv[4];

    int n = 0;
    for (int i = 0; i < td->max_y; i++) {
        for (int j = 0; j < td->max_x; j++) {
            struct struct_7186d2e3 *ic = &td->p_icon[j + i * td->max_x];
            pos.x = icon_base_pos.x + j * icon_offset.x;
            pos.y = icon_base_pos.y + i * icon_offset.y;
            ic->p_shadow = UnitSprite_New(ioObjp, mefunc_layout(0x413), mefunc_parts_moji_kage);
            spos.x = pos.x + shadow_offset.x;
            spos.y = pos.y + shadow_offset.y;
            UnitSprite_SetPosition(ic->p_shadow, &spos);
            UnitSprite_SetFadeIn(ic->p_shadow);
            ic->p_button = UnitSprite_New(ioObjp, mefunc_layout(0x413), mefunc_parts_moji_ita);
            UnitSprite_SetPosition(ic->p_button, &pos);
            UnitSprite_SetFadeIn(ic->p_button);
            rect.x = pos.x + char_offst.x;
            rect.y = pos.y + char_offst.y;
            rect.w = tex_size.x;
            rect.h = tex_size.y;
            uv[0] = texcoord_base.x + tex_size.x * (n % tex_num.x);
            uv[1] = texcoord_base.y + tex_size.y * (n / tex_num.x);
            uv[2] = uv[0] + tex_size.x;
            uv[3] = uv[1] + tex_size.y;
            ic->p_image = DF_MOBJ_NXNewSprite(ioObjp, a_char_type_tex_id[(int)td->char_type], &rect, uv, &char_color, 0, 0,
                                              0, 1);
            DF_MOBJ_SetFadeIn(ic->p_image);
            n++;
        }
    }
    td->p_icon_light = UnitSpriteBlink_New(ioObjp, mefunc_layout(0x413), mefunc_parts_hikari_moji);
    UnitSprite_GetPosition(td->p_icon[0].p_button, &pos);
    pos.x = pos.x + icon_light_offset.x;
    pos.y = pos.y + icon_light_offset.y;
    UnitSprite_SetPosition(td->p_icon_light, &pos);
    td->step++;
}

/* Opens the Text stamp system (Func row 7, coidx 0-5 = glyph size 8 / 16 / 24 x two stamp sets): 8 x 5 glyph grid
 * (8 x 6 for the 24-pixel size), plate array, sub-icons fade out. NULL if no mobj is free. stat stays as
 * DF_MOBJ_GetNewObject set it. */
struct stMobj *__SetNewTextSystem(struct stMobj *inObjp, struct struct_fedcf029 *dlp)
{
    PS2_FRAME(0x50);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    struct struct_4ece179d *td = mefunc_text(obj);
    obj->type = 0;
    td->dispListp = dlp;
    td->max_x = 8;
    td->max_y = 5;
    if (dlp->coidx == 2 || dlp->coidx == 5) td->max_y++;
    td->char_type = dlp->coidx < 3 ? 0 : 1;
    dlp->f_canSpuit = 0;
    td->p_icon = (struct struct_7186d2e3 *)DF_MMM_Alloc(0, 0x240);
    for (int k = 0; k < 6; k++) {
        if (td->dispListp->a_sub_icon[k].p_button) {
            UnitSprite_SetFadeOut(td->dispListp->a_sub_icon[k].p_button);
            UnitSprite_SetFadeOut(td->dispListp->a_sub_icon[k].p_shadow);
            DF_MOBJ_SetFadeOut(td->dispListp->a_sub_icon[k].p_image);
        }
    }
    DF_MOBJ_SetDrawEnable(td->dispListp->p_sub_icon_light, 0);
    obj->play_func = (void (*)(void))GFN(__PlayEmbTextSystemFadeIn);
    obj->count = 0;
    return obj;
}

/* One-shot Rot (Func row 8, coidx 0-2 = 90 / 270 / 180): undo snapshot, rotate, finished at once (stat 5). */
struct stMobj *__SetNewRotSystem(struct stMobj *inObjp, struct struct_fedcf029 *dlp)
{
    PS2_FRAME(0x30);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    obj->type = 0;
    *(struct struct_fedcf029 **)((char *)obj + 0x10) = dlp;
    dlp->step = 0;
    memcpy(dlp->edt_tbb[1], dlp->edt_tbb[0], 0x4000);
    dlp->f_undo = 0;
    __EME_Func_Rotate(dlp, dlp->coidx);
    obj->stat = 5;
    obj->play_func = 0;
    obj->count = 0;
    return obj;
}

/* One-shot Clear (Func row 11): undo snapshot, image cleared to index 0, finished at once (stat 5). */
struct stMobj *__SetNewClearSystem(struct stMobj *inObjp, struct struct_fedcf029 *dlp)
{
    PS2_FRAME(0x30);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    obj->type = 0;
    *(struct struct_fedcf029 **)((char *)obj + 0x10) = dlp;
    memcpy(dlp->edt_tbb[1], dlp->edt_tbb[0], 0x4000);
    dlp->f_undo = 0;
    memset(dlp->edt_tbb[0], 0, 0x4000);
    obj->stat = 5;
    obj->play_func = 0;
    obj->count = 0;
    return obj;
}

/* Shift system dt: +0 dlp, +0xc short (cleared), +0x10 fade-in counter, +0x11 cleared on close. */
static inline struct struct_fedcf029 *mefunc_shift_dlp(struct stMobj *obj)
{
    return *(struct struct_fedcf029 **)((char *)obj + 0x10);
}

static inline unsigned char *mefunc_shift_b(struct stMobj *obj, int off)
{
    return (unsigned char *)obj + 0x10 + off;
}

static void __PlayEmbShiftSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x20);
    if (DF_MVIEW_MotionSync(MViewp)) {
        ioObjp->stat = 5;
        ioObjp->play_func = 0;
    }
}

/* Shift (Func row 10): Rrt / right button closes (returns); undo; every frame each pad direction / mouse delta
 * shifts the image one pixel with wrap-around (right, left, up, down in that order); grid / zoom rate keys. */
static void __PlayEmbShiftSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    struct struct_fedcf029 *dlp = mefunc_shift_dlp(ioObjp);
    if (MPad[MPadUser].Rrt == 1 || eMouse->rbtn == 1) {
        Voice_Call(0x3eb, 0x7f);
        *mefunc_shift_b(ioObjp, 0x11) = 0;
        mefunc_guide8(8, 0xa, 9, 1, 2, 0, 0xd, 0x63);
        *mefunc_shift_b(ioObjp, 0x10) = 0;
        ioObjp->stat = 2;
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbShiftSystemFadeOut);
        return;
    }
    mefunc_undo_swap(dlp);
    if (ePad.rt || eMouse->x > 0) {
        Voice_Call(0x3e9, 0x7f);
        __EME_Func_Shift(dlp->edt_tbb[0], (unsigned char *)dlp + 0x8000, 0);
    }
    if (ePad.lt || eMouse->x < 0) {
        Voice_Call(0x3e9, 0x7f);
        __EME_Func_Shift(dlp->edt_tbb[0], (unsigned char *)dlp + 0x8000, 1);
    }
    if (ePad.up || eMouse->y < 0) {
        Voice_Call(0x3e9, 0x7f);
        __EME_Func_Shift(dlp->edt_tbb[0], (unsigned char *)dlp + 0x8000, 3);
    }
    if (ePad.dw || eMouse->y > 0) {
        Voice_Call(0x3e9, 0x7f);
        __EME_Func_Shift(dlp->edt_tbb[0], (unsigned char *)dlp + 0x8000, 2);
    }
    mefunc_rate_keys(dlp);
}

/* frames 0 and 1 idle, frame 2 the Shift system runs */
static void __PlayEmbShiftSystemFadeIn(struct stMobj *ioObjp)
{
    unsigned char *cnt = mefunc_shift_b(ioObjp, 0x10);
    signed char c = (signed char)*cnt;
    if (c == 2) {
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbShiftSystem);
        ioObjp->stat = 0;
    } else if (c == 1 || c == 0) {
        *cnt = (unsigned char)(c + 1);
    }
}

/* Opens the Shift system (Func row 10): key guide, undo snapshot of the image. NULL if no mobj is free. */
struct stMobj *__SetNewShiftSystem(struct stMobj *inObjp, struct struct_fedcf029 *dlp)
{
    PS2_FRAME(0x30);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    obj->type = 0;
    obj->stat = 1;
    *(struct struct_fedcf029 **)((char *)obj + 0x10) = dlp;
    *(short *)((char *)obj + 0x1c) = 0;
    dlp->step = 0;
    mefunc_guide8(8, 0xa, 0, 0, 2, 0, 0xd, 0x63);
    dlp->f_canSpuit = 0;
    memcpy(dlp->edt_tbb[1], dlp->edt_tbb[0], 0x4000);
    dlp->f_undo = 0;
    obj->play_func = (void (*)(void))GFN(__PlayEmbShiftSystemFadeIn);
    obj->count = 0;
    return obj;
}

/* ---- group 6: Mirror system ---- */

/* Mirror system dt (same layout as Shift): +0 dlp, +0xc short (cleared), +0x10 counter (fade-in frames / Move
 * frames), +0x11 Move state, +0x12 / +0x13 x / y direction latch of the mirror-follow mode (1 / -1, 0 = none). */
static inline signed char *mefunc_mirror_s(struct stMobj *obj, int off)
{
    return (signed char *)obj + 0x10 + off;
}

/* Mouse delta -> mirror-follow step: 0 inside the -1..1 dead zone, else delta / 8 + sign (asm 0x1e3c0f0..0x1e3c134,
 * same sequence for every direction). */
static inline int mefunc_mirror_step(int m)
{
    int s = m < 0 ? -1 : 1;
    int q = m / 8;
    int k = 1 - (m >= -1 && m < 2);
    return k * (q + s);
}

/* Scrolls the zoomed view one step per frame towards the side(s) the cursor left the image on (f_edtFrame bits
 * 1 / 2 / 4 / 8), for 16 frames or until both axes hit their limit (twin of the Text Move). */
static void __PlayEmbMirrorSystemMove(struct stMobj *ioObjp)
{
    struct struct_fedcf029 *dlp;
    signed char *cnt = mefunc_mirror_s(ioObjp, 0x10);
    signed char *func = mefunc_mirror_s(ioObjp, 0x11);
    int moved = 0;
    if (*func == 0) {
        *func = 1;
        return;
    }
    if (*func != 1) return;
    (*cnt)++;
    if (*cnt >= 0x11) {
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbMirrorSystem);
        ioObjp->stat = 0;
        return;
    }
    dlp = mefunc_shift_dlp(ioObjp);
    char f = dlp->f_edtFrame;
    if (f & 1) {
        dlp->top_curx--;
        if (dlp->top_curx < 0) {
            dlp->top_curx = 0;
            moved++;
        }
    } else if (f & 2) {
        dlp->top_curx++;
        int lim = 0x80 - 0x100 / dlp->rate_zoom;
        if (lim < dlp->top_curx) {
            dlp->top_curx = (short)lim;
            moved++;
        }
    }
    f = dlp->f_edtFrame;
    if (f & 4) {
        dlp->top_cury--;
        if (dlp->top_cury < 0) {
            dlp->top_cury = 0;
            moved++;
        }
    } else if (f & 8) {
        dlp->top_cury++;
        int lim = 0x80 - 0x100 / dlp->rate_zoom;
        if (lim < dlp->top_cury) {
            dlp->top_cury = (short)lim;
            moved++;
        }
    }
    if (moved == 2) {
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbMirrorSystem);
        ioObjp->stat = 0;
    }
}

static void __PlayEmbMirrorSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x20);
    if (DF_MVIEW_MotionSync(MViewp)) {
        ioObjp->stat = 5;
        ioObjp->play_func = 0;
    }
}

/* Mirror (Func row 9). Rrt / right button closes. Decide (Rdw / left button): cursor outside the image -> scroll
 * (Move); dlp->step 0 / 1 -> __EME_Func_Mirror mode 0 (step 1 first snapshots the undo image; Func_Mirror advances
 * the step), step 2 -> back to step 0. Rlt = undo. Directions: in step 2 the pad key (MPad, not the repeat) or a
 * mouse step past the dead zone mirrors the image again (x: mode 0, y: mode 1) and latches the direction; otherwise
 * they move the cursor as the Text stamp cursor. Then f_edtCur, the grid / zoom keys and the cursor sync. */
static void __PlayEmbMirrorSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct struct_fedcf029 *dlp = mefunc_shift_dlp(ioObjp);
    char old_cur = dlp->f_edtCur;

    if (MPad[MPadUser].Rrt == 1 || eMouse->rbtn == 1) {
        Voice_Call(0x3eb, 0x7f);
        *mefunc_mirror_s(ioObjp, 0x11) = 0;
        mefunc_guide8(8, 0xa, 9, 1, 2, 0, 0xd, 0x63);
        *mefunc_mirror_s(ioObjp, 0x10) = 0;
        mefunc_shift_dlp(ioObjp)->f_layer = 0;
        ioObjp->stat = 2;
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbMirrorSystemFadeOut);
        return;
    }
    if (MPad[MPadUser].Rdw == 1 || eMouse->lbtn == 1) {
        Voice_Call(0x3ea, 0x7f);
        if (dlp->f_edtCur) {
            *mefunc_mirror_s(ioObjp, 0x10) = 0;
            *mefunc_mirror_s(ioObjp, 0x11) = 0;
            ioObjp->stat = 1;
            ioObjp->play_func = (void (*)(void))GFN(__PlayEmbMirrorSystemMove);
            return;
        }
        if (dlp->step < 2) {
            if (dlp->step == 1) {
                memcpy(dlp->edt_tbb[1], dlp->edt_tbb[0], 0x4000);
                dlp->f_undo = 0;
            }
            __EME_Func_Mirror(dlp->curx[0], dlp->cury[0], dlp, 0);
        } else {
            dlp->f_layer = 0;
            dlp->step = 0;
        }
    }
    mefunc_undo_swap(dlp);

    signed char *lx = mefunc_mirror_s(ioObjp, 0x12);
    signed char *ly = mefunc_mirror_s(ioObjp, 0x13);
    if (ePad.rt || eMouse->x > 0) {
        if (dlp->step == 2) {
            Voice_Call(0x3e9, 0x7f);
            if (MPad[MPadUser].rt || (*lx <= 0 && mefunc_mirror_step(eMouse->x) >= 2)) {
                *lx = 1;
                __EME_Func_Mirror(dlp->curx[0], dlp->cury[0], dlp, 0);
            }
        } else {
            mefunc_text_right(dlp);
        }
    }
    if (ePad.lt || eMouse->x < 0) {
        if (dlp->step == 2) {
            Voice_Call(0x3e9, 0x7f);
            if (MPad[MPadUser].lt || (*lx >= 0 && mefunc_mirror_step(eMouse->x) < -1)) {
                *lx = -1;
                __EME_Func_Mirror(dlp->curx[0], dlp->cury[0], dlp, 0);
            }
        } else {
            mefunc_text_left(dlp);
        }
    }
    if (ePad.up || eMouse->y < 0) {
        if (dlp->step == 2) {
            Voice_Call(0x3e9, 0x7f);
            if (MPad[MPadUser].up || (*ly >= 0 && mefunc_mirror_step(eMouse->y) < -1)) {
                *ly = -1;
                __EME_Func_Mirror(dlp->curx[0], dlp->cury[0], dlp, 1);
            }
        } else {
            mefunc_text_up(dlp);
        }
    }
    if (ePad.dw || eMouse->y > 0) {
        if (dlp->step == 2) {
            Voice_Call(0x3e9, 0x7f);
            if (MPad[MPadUser].dw || (*ly <= 0 && mefunc_mirror_step(eMouse->y) >= 2)) {
                *ly = 1;
                __EME_Func_Mirror(dlp->curx[0], dlp->cury[0], dlp, 1);
            }
        } else {
            mefunc_text_down(dlp);
        }
    }

    mefunc_edtcur_update(dlp);
    mefunc_rate_keys(mefunc_shift_dlp(ioObjp));
    mefunc_cursor_sync(dlp, old_cur);
}

/* frames 0 and 1 idle, frame 2 the Mirror system runs */
static void __PlayEmbMirrorSystemFadeIn(struct stMobj *ioObjp)
{
    signed char *cnt = mefunc_mirror_s(ioObjp, 0x10);
    signed char c = *cnt;
    if (c == 2) {
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbMirrorSystem);
        ioObjp->stat = 0;
    } else if (c == 1 || c == 0) {
        *cnt = (signed char)(c + 1);
    }
}

/* Opens the Mirror system (Func row 9): direction latches cleared, step 0, key guide. NULL if no mobj is free. */
struct stMobj *__SetNewMirrorSystem(struct stMobj *inObjp, struct struct_fedcf029 *dlp)
{
    PS2_FRAME(0x30);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    obj->type = 0;
    obj->stat = 1;
    *(struct struct_fedcf029 **)((char *)obj + 0x10) = dlp;
    *(short *)((char *)obj + 0x1c) = 0;
    *mefunc_mirror_s(obj, 0x12) = 0;
    *mefunc_mirror_s(obj, 0x13) = 0;
    dlp->step = 0;
    dlp->f_layer = 0;
    mefunc_guide8(8, 0xa, 0, 1, 2, 0, 0xd, 0x63);
    dlp->f_canSpuit = 0;
    obj->play_func = (void (*)(void))GFN(__PlayEmbMirrorSystemFadeIn);
    obj->count = 0;
    return obj;
}
