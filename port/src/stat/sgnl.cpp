/* src/stat/sgnl.cpp - match start / end signals per view (whole file): "READY" / "GO" fading out at the start,
 * WIN / LOSE / DRAW (ready_state 3..6) shown for 6 seconds at the end, after which the view is done (state 7).
 * The gauges are hidden once a result is shown (gauge_off).
 * Quirk: every view range check is written so that it never rejects (view < 0 && view > 3). */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

#define A16 __attribute__((aligned(16)))

#pragma clang diagnostic ignored "-Wtautological-overlap-compare"

static int view_out(int view) { return view < 0 && 3 < view; }   /* never true (original quirk) */
static int view_in(int view) { return -1 < view || view < 4; }   /* always true (original quirk) */

int DF_SGNL_IsGaugeOff(int view)
{
    if (view_out(view)) return 0;
    return sgnl[view].gauge_off;
}

int DF_SGNL_IsEndAll(void)
{
    for (int i = 0; i < NowPlayerNum; i++) {
        int end = view_out(i) ? 0 : sgnl[i].ready_state == 7;
        if (!end) return 0;
    }
    return 1;
}

/* win: 0 win, 1 lose, 2 lose (second form), 3 draw; only from idle / READY-GO done / draw */
int DF_SGNL_StartWinLose(int view, int win)
{
    if (view_out(view)) return 0;
    int st = sgnl[view].ready_state;
    if (st == DF_SGNL_StartWinLose__win_stat[win]) return 0;
    if (st != 0 && st != 2 && st != 6) return 0;
    if (win == 3) sgnl[view].ready_state = 6;
    else if (win == 2) sgnl[view].ready_state = 5;
    else if (win == 1) sgnl[view].ready_state = 4;
    else if (win == 0) sgnl[view].ready_state = 3;
    sgnl[view].ready_frm = 0;
    sgnl[view].gauge_off = 1;
    return 1;
}

void DF_SGNL_SetControl(int isEnable) { s_isControlOff = isEnable == 0; }

void DF_SGNL_StartReadGo(int player)
{
    if (view_in(player)) sgnl[player].ready_state = 1;
}

/* fade over 2 s after 4 s on screen */
static int result_alpha(int frm)
{
    if (frm < 0xf0) return 0x80;
    if (frm < 0x168) return 0x80 - (int)((unsigned int)(frm - 0xf0) * 0x80) / 0x78;
    return 0;
}

static void set_tex0(int i)
{
    struct_f386fb29 t;
    memcpy(&t, &STA2_TEXTURE[i], 8);
    GS_SetTEX0_1(t);
}

/* result word: shadow (STA2_RGBA[3], subtractive) then the coloured word, from the digit-coded sprite table */
static void draw_result(int x, int y, const char *str, int (*tbl)[4], int max, int col, int alpha)
{
    int c[4] A16;
    memcpy(c, STA2_RGBA[3], 16);
    c[3] = alpha;
    GS_SetRGBA(c);
    GS_SetALPHA_1(2, 0, 0, 1, 0);
    DF_ACSTAT_DrawNumber2(x, y, (char *)str, tbl, max, 0x30);
    memcpy(c, STA2_RGBA[col], 16);
    c[3] = alpha;
    GS_SetRGBA(c);
    GS_SetALPHA_1(0, 2, 0, 1, 0);
    DF_ACSTAT_DrawNumber2(x, y, (char *)str, tbl, max, 0x30);
}

void DF_SGNL_Draw(int view)
{
    PS2_FRAME(0xb0);
    PS2_RQ_SCOPE(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 5), 5);   /* render tag (host only): signals / caution */
    if (!view_in(view) || s_isControlOff) return;
    struct_3c170e89 *s = &sgnl[view];
    if (!s->ready_state) return;
    int loaded = 0;
    int c[4] A16;
    ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 5), 5); GS_NewList();
    GS_SetTEST_1(0, 0, 0, 0, 0, 0, 1, 1);
    GS_SetALPHA_1(0, 1, 0, 1, 0);
    GS_SetCLAMP_1(1, 1, 0, 0, 0, 0);
    switch (s->ready_state) {
    case 1:   /* READY */
    case 2:   /* GO */
        memcpy(c, STA2_RGBA[7], 16);
        c[3] = ps2_ftoi(((float)STA2_RGBA[7][3] * (float)(0x78 - s->ready_frm)) / 120.0f);
        GS_SetRGBA(c);
        set_tex0(3);
        if (s->ready_state == 1) DF_ACSTAT_DrawSprite(s->ulx - 0x3c, s->uly - 0xc, 0.0f, 39.0f, 0x78, 0x18);
        else DF_ACSTAT_DrawSprite(s->ulx - 0x40, s->uly - 0x14, 0.0f, 0.0f, 0x80, 0x28);
        break;
    case 3:   /* WIN */
        StatTex_LoadWinLoseTexture();
        loaded = 1;
        set_tex0(15);
        draw_result(s->ulx - 0x60, s->uly - 0x20, "012", win_spr, 3, 7, result_alpha(s->ready_frm));
        break;
    case 4:   /* LOSE */
        StatTex_LoadWinLoseTexture();
        loaded = 1;
        set_tex0(15);
        draw_result(s->ulx - 0x60, s->uly - 0x18, "0123", lose_spr, 0x10, 8, result_alpha(s->ready_frm));
        break;
    case 5:
        StatTex_LoadWinLoseTexture();
        loaded = 1;
        set_tex0(15);
        draw_result(s->ulx - 0x60, s->uly - 0x18, "4567", lose_spr, 0x10, 9, result_alpha(s->ready_frm));
        break;
    case 6: { /* DRAW */
        int a = result_alpha(s->ready_frm);
        if (a > 0) {
            StatTex_LoadWinLoseTexture();
            loaded = 1;
            set_tex0(15);
            GS_SetTEST_1(0, 0, 0, 0, 0, 0, 0, 1);
            draw_result(s->ulx - 0x78, s->uly - 0x18, "8592:", lose_spr, 0x10, 8, a);
        }
        break;
    }
    }
    GS_FlushList(); ps2_rq_tag_end();
    if (loaded) DF_ACST2_ReloadTMX(1);
}

void DF_SGNL_Proc(int view)
{
    if (!view_in(view) || s_isControlOff) return;
    struct_3c170e89 *s = &sgnl[view];
    switch (s->ready_state) {
    case 0:
        s->ready_state = 0;
        s->ready_frm = 0;
        break;
    case 1:
    case 2:
        if (++s->ready_frm >= 0x78) {
            s->ready_state = s->ready_state == 1 ? 2 : 0;
            s->ready_frm = 0;
        }
        break;
    case 3:
    case 4:
    case 5:
        if (++s->ready_frm > 0x168) s->ready_state = 7;
        break;
    case 6:
        if (s->ready_frm <= 0x168) s->ready_frm++;
        break;
    }
}

/* one sprite of the view's signal set (Spr[view * 24 + idx]) */
static void set_spr(int idx, int x, int y, const int *uvwh, int tex)
{
    unsigned long long t = STA2_TEXTURE[tex];
    struct_b00e0e3a *s = &Spr[idx];
    memset(s, 0, 0x60);
    s->prog = -1;
    s->ulx = x;
    s->uly = y;
    s->lrx = x + uvwh[2];
    s->lry = y + uvwh[3];
    s->uls = (float)uvwh[0];
    s->ult = (float)uvwh[1];
    s->lrs = s->uls + (float)uvwh[2];
    s->lrt = s->ult + (float)uvwh[3];
    memcpy(&s->tex0, &t, 8);
    s->alpha = 0;
}

/* sprite layout of the signals around (ulx, uly): READY letters (0..4), WIN (6..8), LOSE / DRAW (9..21).
 * Quirk: the sprites' y comes from ulx, not uly. */
void DF_SGNL_SetCenter(int view, int ulx, int uly)
{
    PS2_FRAME(0x140);
    if (!view_in(view)) return;
    struct_3c170e89 *s = &sgnl[view];
    s->ulx = ulx;
    s->uly = uly;
    int b = view * 0x18;
    static const int ready_x[5] = {-0x50, -0x30, -0x10, 0x10, 0x30};
    for (int i = 0; i < 5; i++) set_spr(b + i, s->ulx + ready_x[i], s->ulx - 0x18, ready_spr[0][i], 2);
    for (int i = 0; i < 3; i++) set_spr(b + 6 + i, s->ulx - 0x60 + 0x20 * i, s->ulx - 0x20, win_spr[i], 1);
    static const int lose_x[13] = {-0x60, -0x30, 0, 0, -0x60, -0x30, 0, 0, -0x78, -0x48, -0x18, 0x18, 0x48};
    for (int i = 0; i < 13; i++) set_spr(b + 9 + i, s->ulx + lose_x[i], s->ulx - 0x18, lose_spr[i], 1);
}

void DF_SGNL_Init(int mode)
{
    if (mode >= 2) mode = 1;
    else if (mode < 0) mode = 0;
    NextSpr = 0;
    s_isControlOff = 0;
    for (int i = 0; i < 4; i++) {
        sgnl[i].ulx = 0;
        sgnl[i].uly = 0;
        sgnl[i].ready_frm = 0;
        sgnl[i].ready_state = 0;
        sgnl[i].gauge_off = 0;
        sgnl[i].mode = (unsigned char)mode;
    }
}
