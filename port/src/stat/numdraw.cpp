/* src/stat/numdraw.cpp - HUD numbers and the status font (whole file).
 * Numbers: one sprite per character from a {u, v, w, h} table (s_tblNumStrToUvwh, 16 styles), optionally drawing
 * leading zeros in a dimmer colour (ACSTAT_SetDrawColor_AlphaDiv). Status font: letters / digits / space from
 * s_tblAbcStrToUvwh (two sets), drawn as sprites or blitted into a texture (flag 0x200), with a background of space
 * glyphs and left / right / centre alignment. */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

#define A16 __attribute__((aligned(16)))

#ifdef ACNB_SOURCEPORT
/* -1 = no image loaded yet (s_tblNumStrToUvwh / s_tblAbcStrToUvwh come from the disc: sp_missing ELF_DATA) */
int s_tbpLoadImage = -1;      /* 0x27b22c */
int s_tbwLoadImage A16 = -1;  /* 0x27b230 */
#endif

/* the glyph of c in font set tbl: A-Z and a-z share the letter cells, then 0-9, then space (cell 56) */
static StStatFontData *abc_glyph(StStatFontData *tbl, char c)
{
    if (c >= 'A' && c <= 'Z') return tbl + (c - 'A');
    if (c >= 'a' && c <= 'z') return tbl + (c - 'a');
    if (c >= '0' && c <= '9') return tbl + (c - 0x16);
    if (c == ' ') return tbl + 56;
    return 0;
}

/* the GS_BitBlt formats of a flag-0x200 string: destination PSM, source TBP (bw 4), source PSM */
struct AbcBlit {
    int dpsm, stbp, spsm;
};

static void abc_blit(const AbcBlit &b, int x, int y, int u, int v, int w, int h)
{
    ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 1), 1); GS_NewList();
    GS_BitBlt(s_tbpLoadImage, s_tbwLoadImage, b.dpsm, x, y, b.stbp, 4, b.spsm, u, v, w, h);
    GS_FlushList(); ps2_rq_tag_end();
}

/* flag: 2 right-align, 4 centre in w, 0x20 second font set, 0x100 no background, 0x200 blit into the load-image
 * texture (StatFont_SetupLoadImage) instead of drawing; returns the x after the text.
 * Quirk: alignment measures the text from x but subtracts its end position from x + w, so right / centre
 * alignment is only right for x == 0. Shared body of StatFont_DrawString / Ex (no frame of its own). */
static int statfont_draw(int x, int y, int w, int h, char *pstr, unsigned short flag, const AbcBlit &blit);

int StatFont_DrawString(int x, int y, int w, int h, char *pstr, unsigned short flag)
{
    PS2_FRAME(0x120);
    return statfont_draw(x, y, w, h, pstr, flag, AbcBlit{0x14, 0x3940, 0x14});
}

/* StatFont_DrawString with the blit formats as arguments (weapon-name textures, src/stat/wepicon.cpp) */
int StatFont_DrawStringEx(int x, int y, int w, int h, char *pstr, unsigned short flag, int dest_psm, int source_tbp,
                          int source_psm)
{
    PS2_FRAME(0x150);
    return statfont_draw(x, y, w, h, pstr, flag, AbcBlit{dest_psm, source_tbp, source_psm});
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper (gen_bridge stops at 8 int args); source_psm is the caller's stack arg at sp+0 */
static void bw__StatFont_DrawStringEx(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    int r = StatFont_DrawStringEx(c->gpr[A0].sw[0], c->gpr[A1].sw[0], c->gpr[A2].sw[0], c->gpr[A3].sw[0],
                                  (char *)bridge_ptr(c->gpr[T0].uw[0]), (unsigned short)c->gpr[T1].uw[0],
                                  c->gpr[T2].sw[0], c->gpr[T3].sw[0], (int)rd32(sp));
    c->gpr[V0].sd[0] = r;
}
#endif

static int statfont_draw(int x, int y, int w, int h, char *pstr, unsigned short flag, const AbcBlit &blit)
{
    int pass = !(flag & 2) && !(flag & 4);
    StStatFontData *tbl = s_tblAbcStrToUvwh[(flag & 0x20) == 0];
    StStatFontData *space = tbl + 56;
    int cx = x, end = 0;
    if (!(flag & 0x100)) {
        if (flag & 0x200) {
            int rest = w, bx = x;
            if (!(rest < space->w)) {
                int sw;
                do {
                    abc_blit(blit, bx, y, space->x, space->y, space->w, space->h);
                    sw = space->w;
                    rest -= sw;
                    bx += sw;
                } while (!(rest < sw));
            }
            if (rest > 0) abc_blit(blit, bx, y, space->x, space->y, rest, space->h);
        } else {
            GS_DrawSpriteDP(x, y, x + w, y + h, space->x, space->y, space->x + space->w, space->y + space->h, 0);
        }
    }
    for (; pass < 2; pass++) {
        for (char *p = pstr; *p; p++) {
            StStatFontData *g = abc_glyph(tbl, *p);
            if (!g) continue;
            if (pass == 1) {
                if (flag & 0x200) abc_blit(blit, cx, y, g->x, g->y, g->w, g->h);
                else GS_DrawSprite(cx, y, g->x, g->y, g->w, g->h);
            }
            cx += g->w;
        }
        end = cx;
        if (pass == 0) {
            if (flag & 2) cx = (x + w) - cx;
            else if (flag & 4) cx = x + ((w - cx) >> 1);
        }
    }
    return end;
}

void StatFont_SetupLoadImage(int tbp, int tbw)
{
    s_tbpLoadImage = tbp;
    s_tbwLoadImage = tbw;
}

void StatFont_Init(void)
{
    s_tbpLoadImage = -1;
    s_tbwLoadImage = -1;
}

void DF_ACSTAT_DrawNumberOffset(int x, int y, char *str, int table, int offset)
{
    DF_ACSTAT_DrawNumber2Offset(x, y, str, s_tblNumStrToUvwh[table], 0x10, 0x30, offset);
}

void DF_ACSTAT_DrawNumber(int x, int y, char *str, int table)
{
    DF_ACSTAT_DrawNumber2(x, y, str, s_tblNumStrToUvwh[table], 0x10, 0x30);
}

/* characters top..top + max from table (others advance by the width of entry 0); with the zero colour on, leading
 * '0's (entry 0) use s_vecZeroDrawColor until the first other digit. advance: fixed (offsetx) or the glyph width */
static void draw_number(int x, int y, const char *str, int (*table)[4], int max, int top, int offsetx, int fixed)
{
    int use_zero = s_bZeroDrawColorUse != 0, leading = 1, zero_set = 0;
    for (; *str; str++) {
        int i = *str - top;
        if (i < 0 || max < i) {
            x += table[0][2];
            continue;
        }
        if (use_zero && leading) {
            if (i == 0) {
                if (!zero_set) {
                    GS_SetRGBA(s_vecZeroDrawColor);
                    zero_set = 1;
                }
            } else {
                GS_SetRGBA(s_vecDrawColor);
                leading = 0;
            }
        }
        int w = table[i][2];
        DF_ACSTAT_DrawSprite(x, y, (float)table[i][0], (float)table[i][1], w, table[i][3]);
        x += fixed ? offsetx : w;
    }
    if (use_zero) GS_SetRGBA(s_vecDrawColor);
}

void DF_ACSTAT_DrawNumber2Offset(int x, int y, char *str, int (*table)[4], int max, int top, int offsetx)
{
    PS2_FRAME(0xb0);
    draw_number(x, y, str, table, max, top, offsetx, 1);
}

void DF_ACSTAT_DrawNumber2(int x, int y, char *str, int (*table)[4], int max, int top)
{
    PS2_FRAME(0xa0);
    draw_number(x, y, str, table, max, top, 0, 0);
}

/* bUse 1: draw in vecColor, leading zeros with alpha / nAlphaDivValue; 0: off */
void ACSTAT_SetDrawColor_AlphaDiv(int bUse, int *vecColor, int nAlphaDivValue)
{
    s_bZeroDrawColorUse = bUse;
    if (bUse == 1) {
        memcpy(s_vecDrawColor, vecColor, 16);
        memcpy(s_vecZeroDrawColor, vecColor, 16);
        s_vecZeroDrawColor[3] = ps2_div32(s_vecZeroDrawColor[3], nAlphaDivValue);
    } else {
        memset(s_vecDrawColor, 0, 16);
        memset(s_vecZeroDrawColor, 0, 16);
    }
}
