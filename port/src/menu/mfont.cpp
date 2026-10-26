/* src/menu/mfont.cpp — menu text renderer (OLMENU): ASCII glyphs from the menu font texture, SJIS
 * glyphs blitted per character from the kanji sheets in menu data into MFontBuff, inline control codes
 * (<cr>, <col>RRGGBB, <alp>AA ...). Builds GIF packets in the scratchpad (DF_MDRAW_GetSPRBuff). */
#include <string.h>
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
/* SJIS first codes of the 10 font ranges + 0 (OLMENU 0x1eddf70 / 0x1eddfa0); MFontTopIdx starts as a copy and is
 * turned into glyph indices at init */
unsigned int MFontTopTblIdx[11] = {0x889f, 0x829f, 0x8340, 0x824f, 0x8281, 0x8260, 0x8754, 0x8140, 0x83bf, 0x839f, 0};
unsigned int MFontTopIdx[11] = {0x889f, 0x829f, 0x8340, 0x824f, 0x8281, 0x8260, 0x8754, 0x8140, 0x83bf, 0x839f, 0};
/* OLMENU data read at the first menu text (were zero-filled stand-ins: s_aFontInfo cw 0 -> divide by zero in
 * __MFONT_DrawChar at the main menu, phase 3 replay compare). Values = the hybrid's memory right after the OLMENU load. */
/* ASCII sign -> SJIS full-width code, {0} ends (0x1eddeb0). Signs as numbers (sign char literals with parens /
 * quotes made gen_bridge.py lose the file's functions: every mfont native turned missing) */
struct struct_00343825 MFontEncode[26] = {
    {{0x3f, 0}, 0x8148}, {{0x21, 0}, 0x8149}, {{0x26, 0}, 0x8195}, {{0x2e, 0}, 0x8144}, {{0xa5, 0}, 0x8145},
    {{0x3a, 0}, 0x8146}, {{0x2f, 0}, 0x815e}, {{0x5f, 0}, 0x8151}, {{0x2b, 0}, 0x817b}, {{0x2d, 0}, 0x817c},
    {{0x40, 0}, 0x817e}, {{0x25, 0}, 0x8193}, {{0x23, 0}, 0x8194}, {{0x2a, 0}, 0x8196}, {{0x28, 0}, 0x8169},
    {{0x29, 0}, 0x816a}, {{0x24, 0}, 0x8180}, {{0x3d, 0}, 0x8181}, {{0x2c, 0}, 0x8143}, {{0x27, 0}, 0x8166},
    {{0x3b, 0}, 0x8147}, {{0x7e, 0}, 0x8160}, {{0x22, 0}, 0x8168}, {{0x5b, 0}, 0x8175}, {{0x5d, 0}, 0x8176}, {{0, 0}, 0}};
/* inline control-code names, empty name ends (0x1eddf20) */
unsigned char MFontCtrl[5][16] = {"@", "BR", "COLOR", "ALPHA", ""};
/* half-width SJIS sign characters (the unnamed string at 0x1eddea0; 0x2e twice as in the original) */
static unsigned char s_sjis_han_kigou[16] = {0x21, 0x3f, 0x28, 0x29, 0x2c, 0x2e, 0x3a, 0x2d, 0x2f, 0x26, 0x27, 0x22, 0x2e, 0x20, 0};
unsigned char *g_acSjisHanKigouChar = s_sjis_han_kigou;
/* function statics of the SJIS packet builders (main ELF sdata 0x27b7e8 / 0x27b7ec): GIF tag PRIM bits = sprite,
 * textured, alpha-blended (0x00ab0000; zero drew a POINT) */
unsigned int __MFONT_MakeSJISCharPkt__prim = 0xab0000;
unsigned int __MFONT_MakeSJISHanCharPkt__prim = 0xab0000;
/* per font type: beginu beginv w h cw space drawoffsetx drawoffsety (0x1ee58e0) */
struct __MFONT_FONTINFO s_aFontInfo[9] = {
    {0, 0xd2, 0xc, 0xa, 0x15, 6, 0, 0}, {0, 0xae, 0xe, 0xc, 0x12, 6, 0, 0}, {0, 0x76, 0x10, 0xe, 0x10, 6, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},           {0, 0x30, 0x14, 0xe, 0xc, 8, 0, -1}, {0, 0, 0x1c, 0x10, 9, 8, 0, 0},
    {0, 0, 0xa, 0x14, 0x19, 0, 0, 0},   {0, 0, 0, 0, 0, 0, 0, 0},           {0, 0, 0, 0, 0, 0, 0, 0}};
#endif

/* newlib _ctype_ + 1 (main ELF data): _U 1, _L 2, _N 4, _P 0x10 */
#define CTYPE(c) ((ps2_ctype_ + 1)[(unsigned char)(c)])

/* Glyph pairs drawn with the narrow advance (local array in the original). */
static const char s_narrow[16] = "!;:Iil'()";

static int is_narrow(unsigned char c)
{
    for (const char *p = s_narrow; *p; p++)
        if (*p == c) return 1;
    return 0;
}

/* SJIS lead byte (0x81-0xa0, 0xe0-0xff). */
static int is_sjis_lead(unsigned char c)
{
    return !((c < 0x80 || c > 0xa0) && c < 0xe0);
}

/* Fade-in alpha of character pInIndex between fadebegin and fadeend (0x80 = opaque). */
static int fade_scale(const struct_acb9a91f *a, int index)
{
    if (a->fadebegin >= a->fadeend) return 0x80;
    if (index <= a->fadebegin) return 0x80;
    if (index >= a->fadeend) return 0;
    return 0x80 - ((index - a->fadebegin) * 0x80) / (a->fadeend - a->fadebegin);
}

int DF_MFONT_GetStringWSize(unsigned char *inCp, int inType, int inFlag)
{
    struct_acb9a91f attr;
    int offset = 0;
    unsigned char *cp;

    /* The original calls memset(&attr, 0x228, 0) (size and value swapped: clears nothing); only fields
     * DF_MFONT_SetFontAttr fills are read below. */
    memset(&attr, 0, sizeof attr);
    attr.flag = inFlag;
    DF_MFONT_SetFontAttr(&attr, inType);
    for (cp = inCp; cp && *cp;) {
        if (*cp == ' ')
            offset += attr.space_w;
        else if ((inFlag & 0x400) && __MFONT_IsSjisHan(cp))
            offset += attr.off_x / 2;
        else if (is_narrow(*cp))
            offset += attr.narrow_offset + attr.narrow_w;
        else
            offset += attr.off_x;
        if (is_sjis_lead(*cp))
            cp += cp[1] ? 2 : 1;
        else
            cp++;
    }
    return offset;
}

void DF_MFONT_SetFontAttr(struct_acb9a91f *ioAttr, int inType)
{
    static const struct { short w, h, off_x, off_y; char space_w, narrow_w; } tab[9] = {
        {12, 10, 12, 10, 6, 6},   {14, 12, 14, 12, 6, 6},   {16, 14, 16, 14, 6, 6},
        {18, 18, 18, 18, 10, 20}, {20, 14, 20, 14, 8, 8},   {28, 14, 28, 14, 8, 8},
        {9, 18, 9, 18, 9, 6},     {16, 16, 16, 18, 8, 16},  {12, 12, 12, 18, 6, 12},
    };
    ioAttr->type = (char)inType;
    ioAttr->narrow_offset = 0;
    if (inType >= 0 && inType < 9) {
        ioAttr->w = tab[inType].w;
        ioAttr->h = tab[inType].h;
        ioAttr->off_x = tab[inType].off_x;
        ioAttr->off_y = tab[inType].off_y;
        ioAttr->space_w = tab[inType].space_w;
        ioAttr->narrow_w = tab[inType].narrow_w;
        if (!(ioAttr->flag & 0x800)) ioAttr->flag |= 0x100;   /* drop shadow */
    }
    if (ioAttr->flag & 0x1000) {   /* monospace */
        ioAttr->space_w = (char)ioAttr->w;
        ioAttr->narrow_offset = (char)ioAttr->w - ioAttr->narrow_w;
    }
}

/* Number of lines (1 + <cr> codes); 0 for NULL. */
int DF_MFONT_GetHeight(unsigned char *inBuff)
{
    int h = 0, ctrlID;
    unsigned char *cp = inBuff;

    if (!cp) return 0;
    while (*cp) {
        if (__MFONT_IsDrawChar(*cp) == 1 || *cp == ' ')
            cp++;
        else if (*cp == '<') {
            cp = __MFONT_ProcCtrlCode(cp, &ctrlID, 0, 0);
            if (ctrlID == 1) h++;
        } else
            cp++;
    }
    return h + 1;
}

int DF_MFONT_GetCharCount(unsigned char *str)
{
    int count = 0, ctrlID = 0;

    while (*str) {
        if (__MFONT_IsDrawChar(*str) == 1 || *str == ' ') {
            str++;
            count++;
        } else if (*str == '<')
            str = __MFONT_ProcCtrlCode(str, &ctrlID, 0, 0);
        else
            str++;
    }
    return count;
}

/* Widest line in characters. */
int DF_MFONT_GetWidth(unsigned char *inBuff)
{
    int max_w = 0, w = 0, ctrlID;
    unsigned char *cp = inBuff;

    if (!cp) return 0;
    while (*cp) {
        if (__MFONT_IsDrawChar(*cp) == 1 || *cp == ' ') {
            cp++;
            w++;
        } else if (*cp == '<') {
            cp = __MFONT_ProcCtrlCode(cp, &ctrlID, 0, 0);
            if (ctrlID == 1) {
                if (max_w < w) max_w = w;
                w = 0;
            }
        } else
            cp++;
    }
    if (max_w < w) max_w = w;
    return max_w;
}

/* Control code name (up to ' ', '>' or end) -> index into MFontCtrl, 0 if unknown. */
static unsigned char *__MFONT_GetCtrlID(unsigned char *inCp, int *outIDp)
{
    unsigned char cmd[32], *cp = cmd;
    int i;

    while (*inCp != ' ' && *inCp && *inCp != '>') *cp++ = *inCp++;
    *cp = 0;
    *outIDp = 0;
    for (i = 0; MFontCtrl[i][0]; i++) {
        if (!strcmp((char *)MFontCtrl[i], (char *)cmd)) {
            *outIDp = i;
            return inCp;
        }
    }
    return inCp;
}

/* Parses "<name>" at inCp (and the argument of col/alp codes); applies it to ioCurAttr. */
static unsigned char *__MFONT_ProcCtrlCode(unsigned char *inCp, int *outCtrlIDp, struct_acb9a91f *ioCurAttr, struct_acb9a91f *inOriAttr)
{
    unsigned char wk[256], *w = wk;
    unsigned char *cp = inCp + 1;
    struct_acb9a91f dummy;

    memset(&dummy, 0, sizeof dummy);
    if (!ioCurAttr) ioCurAttr = &dummy;
    if (!inOriAttr) inOriAttr = &dummy;
    while (*cp != '>') {
        if (!*cp) return cp;
        if (w >= wk + sizeof wk) return cp;
        *w++ = *cp++;
    }
    *w = 0;
    cp++;
    __MFONT_GetCtrlID(wk, outCtrlIDp);
    switch (*outCtrlIDp) {
    case 3: cp = __ParseAlphaCode(cp, &ioCurAttr->col); break;
    case 2: cp = __ParseColorCode(cp, &ioCurAttr->col); break;
    case 1:   /* line feed */
        ioCurAttr->y += ioCurAttr->off_y;
        ioCurAttr->x = inOriAttr->x;
        break;
    }
    return cp;
}

static int hexdigit(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Up to 2 hex digits -> alpha (missing digits count as 0). */
static unsigned char *__ParseAlphaCode(unsigned char *str, struct_76b810ec *pcolor)
{
    int i, color = 0, d;

    for (i = 0; i < 2 && (d = hexdigit(*str)) >= 0; i++, str++) color = color * 16 + d;
    for (; i < 2; i++) color <<= 4;
    pcolor->a = (unsigned char)color;
    return str;
}

/* Up to 6 hex digits RRGGBB (missing digits count as 0). */
static unsigned char *__ParseColorCode(unsigned char *str, struct_76b810ec *pcolor)
{
    int i, color = 0, d;

    for (i = 0; i < 6 && (d = hexdigit(*str)) >= 0; i++, str++) color = color * 16 + d;
    for (; i < 6; i++) color <<= 4;
    pcolor->r = (unsigned char)(color >> 16);
    pcolor->g = (unsigned char)(color >> 8);
    pcolor->b = (unsigned char)color;
    return str;
}

/* GIF packet for one glyph from MFontBuff (TEX0 set by the caller): ALPHA/TEST, then a sprite
 * (UV, RGBAQ, XYZ2 x2). inFlag bit 0: main pass (TEST 0x48 instead of 0x42). */
static unsigned int *__MFONT_MakeSJISHanCharPkt(unsigned int *ioDmap, struct_acb9a91f *inAttr, int inOffX, int inOffY, int inOffU, int inOffV, int inFlag)
{
    int Mag = MenuMng.f_movie_brief ? 2 : 1;
    int base = 0x800 - Mag * 0xe0;
    unsigned int *d = ioDmap;

    d[0] = 0x8001; d[1] = 0x10000000; d[2] = 0xe; d[3] = 0;
    d[4] = (inFlag & 1) ? 0x48 : 0x42; d[5] = 0;
    d[6] = 0x42; d[7] = 0;
    d[8] = 0x8001; d[9] = __MFONT_MakeSJISHanCharPkt__prim | 0x50004000; d[10] = 0x43431; d[11] = 0;
    d[12] = inAttr->col.r; d[13] = inAttr->col.g; d[14] = inAttr->col.b; d[15] = inAttr->col.a * 0x60 / 0x80;
    d[16] = MDraw_GetOffsetLeftU(inOffU << 4);
    d[17] = MDraw_GetOffsetTopV(inOffV << 4);
    d[18] = 0; d[19] = 0;
    d[20] = MDraw_GetOffsetLeftX((inAttr->x + inOffX + 0x6c0) * 16);
    d[21] = MDraw_GetOffsetTopY((base + Mag * (inAttr->y + inOffY)) * 16);
    d[22] = inAttr->depth; d[23] = 0;
    d[24] = MDraw_GetOffsetRightU((inOffU + 9) * 16);
    d[25] = MDraw_GetOffsetBottomV((inOffV + 18) * 16);
    d[26] = 0; d[27] = 0;
    d[28] = MDraw_GetOffsetRightX((inAttr->w + inAttr->x + inOffX + 0x6c0) * 16);
    d[29] = MDraw_GetOffsetBottomY((base + Mag * (inAttr->h + inAttr->y + inOffY)) * 16);
    d[30] = inAttr->depth; d[31] = 0;
    return ioDmap + 32;
}

/* Same for a full-width (18x18) glyph. */
static unsigned int *__MFONT_MakeSJISCharPkt(unsigned int *ioDmap, struct_acb9a91f *inAttr, int inOffX, int inOffY, int inFlag)
{
    int Mag = MenuMng.f_movie_brief ? 2 : 1;
    int base = 0x800 - Mag * 0xe0;
    unsigned int *d = ioDmap;

    d[0] = 0x8001; d[1] = 0x10000000; d[2] = 0xe; d[3] = 0;
    d[4] = (inFlag & 1) ? 0x48 : 0x42; d[5] = 0;
    d[6] = 0x42; d[7] = 0;
    d[8] = 0x8001; d[9] = __MFONT_MakeSJISCharPkt__prim | 0x50004000; d[10] = 0x43431; d[11] = 0;
    d[12] = inAttr->col.r; d[13] = inAttr->col.g; d[14] = inAttr->col.b; d[15] = inAttr->col.a * 0x60 / 0x80;
    d[16] = MDraw_GetOffsetLeftU(0);
    d[17] = MDraw_GetOffsetTopV(0);
    d[18] = 0; d[19] = 0;
    d[20] = MDraw_GetOffsetLeftX((inAttr->x + inOffX + 0x6c0) * 16);
    d[21] = MDraw_GetOffsetTopY((base + Mag * (inAttr->y + inOffY)) * 16);
    d[22] = inAttr->depth; d[23] = 0;
    d[24] = MDraw_GetOffsetRightU(0x120);
    d[25] = MDraw_GetOffsetBottomV(0x120);
    d[26] = 0; d[27] = 0;
    d[28] = MDraw_GetOffsetRightX((inAttr->w + inAttr->x + inOffX + 0x6c0) * 16);
    d[29] = MDraw_GetOffsetBottomY((base + Mag * (inAttr->h + inAttr->y + inOffY)) * 16);
    d[30] = inAttr->depth; d[31] = 0;
    return ioDmap + 32;
}

/* TEXFLUSH-free texture setup shared by the SJIS paths: TEX1 + TEX0 of the glyph buffer page. */
static void kick_tex(const struct_13efd7f4 *t)
{
    unsigned int *dmap = DF_MDRAW_GetSPRBuff();
    dmap[0] = 0x8002; dmap[1] = 0x10000000; dmap[2] = 0xe; dmap[3] = 0;
    *(struct_68d02bc7 *)&dmap[4] = t->tex1;
    dmap[6] = 0x14; dmap[7] = 0;
    *(struct_1f802d8d *)&dmap[8] = t->tex0;
    dmap[10] = 6; dmap[11] = 0;
    DF_MDRAW_GIFKickSPR(dmap + 12);
}

/* Copies an 18-row glyph (16 bytes/row, sheet stride 0x80) into MFontBuff and uploads it. */
static void load_glyph(const unsigned char *src, const struct_13efd7f4 *t)
{
    unsigned char *dst = MFontBuff;
    for (int i = 0; i < 18; i++, src += 0x80, dst += 16) DF_MFUNC_CopyMemoryC(dst, (void *)src, 16);
    /* SyncDCache(MFontBuff, MFontBuff + 512): no-op on the host */
    DF_MDRAW_LoadImage(MFontBuff, t->tex0.tbp0, 0x14, 0, 0, 0x20, 0x20);
}

static unsigned int *glyph_header(void)
{
    unsigned int *dmap = DF_MDRAW_GetSPRBuff();
    dmap[0] = 0x8001; dmap[1] = 0x10000000; dmap[2] = 0xe; dmap[3] = 0;
    dmap[4] = 0xffffffff; dmap[5] = 0;   /* TEXFLUSH */
    dmap[6] = 0x3f; dmap[7] = 0;
    return dmap + 8;
}

/* Draws a run of full-width SJIS characters (ASCII drawable chars are mapped to their SJIS forms). */
static unsigned char *__MFONT_DrawSJIS(unsigned char *ioCp, struct_acb9a91f *pInAttr, int *pInIndex, int InColF)
{
    PS2_FRAME(0x510);
    struct_13efd7f4 texinfo;
    struct_acb9a91f AttrS, tmpattr;
    unsigned int *dmap;
    unsigned char *src;
    int offset, half_space;

    MenuTex_StandbyTexture(0x10000, &texinfo);
    kick_tex(&texinfo);
    for (;;) {
        offset = 0;
        half_space = !(pInAttr->flag & 2) && ioCp[0] == 0xa1 && ioCp[1] == 0xa1;
        ioCp = __MFONT_SJIS2Offset(ioCp, &offset);
        if (offset == -1 || !(src = (decltype(src))DF_MDATA_GetData(MenuMng.tm2Tbl[1], offset / 0xc4))) break;
        offset %= 0xc4;
        load_glyph(src + (offset / 14) * 0x900 + (offset % 14) * 9 + 0x20, &texinfo);
        dmap = glyph_header();
        if (pInAttr->flag & 0x100) {   /* shadow */
            AttrS = *pInAttr;
            AttrS.col.r = AttrS.col.g = AttrS.col.b = 0x50;
            AttrS.col.a = pInAttr->col.a * fade_scale(pInAttr, *pInIndex) >> 7;
            dmap = __MFONT_MakeSJISCharPkt(dmap, &AttrS, 1, 1, half_space << 1);
        }
        tmpattr = *pInAttr;
        if (InColF) tmpattr.col.a = pInAttr->col.a * fade_scale(pInAttr, *pInIndex) >> 7;
        dmap = __MFONT_MakeSJISCharPkt(dmap, &tmpattr, 0, 0, half_space << 1 | 1);
        DF_MDRAW_GIFKickSPR(dmap);
        pInAttr->x += pInAttr->off_x;
        (*pInIndex)++;
        if ((pInAttr->flag & 0x400) && __MFONT_IsSjisHan(ioCp)) break;
        if (!*ioCp) break;
        if (!is_sjis_lead(*ioCp) && __MFONT_IsDrawChar(*ioCp) != 1) break;
    }
    DF_MDRAW_KickReg(0x42, 0x44);
    return ioCp;
}

/* Draws one half-width character from the half-width SJIS sheet (page 0x12). */
static unsigned char *__MFONT_DrawSJISHan(unsigned char *ioCp, struct_acb9a91f *pInAttr, int *pInIndex, int InColF)
{
    PS2_FRAME(0x510);
    struct_13efd7f4 texinfo;
    struct_acb9a91f AttrS, tmpattr;
    unsigned int *dmap;
    unsigned char *src, *next;
    int offset = 0, fx;
    unsigned char half_space;

    MenuTex_StandbyTexture(0x10000, &texinfo);
    kick_tex(&texinfo);
    half_space = *ioCp == ' ';
    next = __MFONT_SJISHan2Offset(ioCp, &offset);
    if (offset != -1 && (src = (decltype(src))DF_MDATA_GetData(MenuMng.tm2Tbl[1], 0x12))) {
        offset %= 0xc4;
        fx = offset % 0x1c;
        load_glyph(src + (offset / 0x1c) * 0x900 + (fx / 2) * 9 + 0x20, &texinfo);
        dmap = glyph_header();
        if (pInAttr->flag & 0x100) {
            AttrS = *pInAttr;
            AttrS.col.r = AttrS.col.g = AttrS.col.b = 0x50;
            AttrS.col.a = pInAttr->col.a * fade_scale(pInAttr, *pInIndex) >> 7;
            AttrS.w = pInAttr->w / 2;
            dmap = __MFONT_MakeSJISHanCharPkt(dmap, &AttrS, 1, 1, fx % 2 * 9, 0, half_space << 1);
        }
        tmpattr = *pInAttr;
        if (InColF) tmpattr.col.a = pInAttr->col.a * fade_scale(pInAttr, *pInIndex) >> 7;
        tmpattr.w = pInAttr->w / 2;
        dmap = __MFONT_MakeSJISHanCharPkt(dmap, &tmpattr, 0, 0, fx % 2 * 9, 0, half_space << 1 | 1);
        DF_MDRAW_GIFKickSPR(dmap);
        pInAttr->x += pInAttr->off_x / 2;
        (*pInIndex)++;
    }
    DF_MDRAW_KickReg(0x42, 0x44);
    return next;
}

/* Half-width character -> index in the half-width sheet (ASCII order from '!'). */
static unsigned char *__MFONT_SJISHan2Offset(unsigned char *ioCp, int *outOffset)
{
    unsigned int c = *ioCp;

    *outOffset = -1;
    if (c == '|') c = '~';
    if (c == 0xae) c = '~';
    if (c == 0x91) c = '`';
    if (c == 0x92) c = '\'';
    if (c == 0x93) c = '"';
    if (c == 0x94) c = '"';
    if (c == ' ') c = 0x7f;
    if (c == 0x96) c = '-';
    *outOffset = c - 0x21;
    return ioCp + 1;
}

/* Draws a run of drawable ASCII characters with the menu font of inAttr->type. */
static unsigned char *__MFONT_DrawChar(unsigned char *inCp, struct_acb9a91f *inAttr, int *pInIndex, int InColF)
{
    PS2_FRAME(0x3b0);
    struct_13efd7f4 texinfo, *texp = &texinfo;
    struct_acb9a91f tmpAttr;
    const __MFONT_FONTINFO *pFontInfo;
    unsigned int *dmap;
    int Mag = MenuMng.f_movie_brief ? 2 : 1;
    int base, w, h, cw, top_u, top_v, u0, u1, v0, v1, wk_w, off_x;
    unsigned int c;
    unsigned char alpha;

    MenuTex_StandbyTexture(0x3f0, &texinfo);
    pFontInfo = &s_aFontInfo[(int)inAttr->type];
    w = pFontInfo->w;
    h = pFontInfo->h;
    cw = pFontInfo->cw;
    top_u = pFontInfo->beginu;
    top_v = pFontInfo->beginv;
    dmap = DF_MDRAW_GetSPRBuff();
    if (inAttr->type == 6) {   /* digits font lives in the menu data sheet; patches its TEX0 in place */
        texp = DF_MDATA_GetTm2TexInfo(MenuMng.tm2Tbl[1], 0);
        texp->tex0.tbp0 = 0x3cc0;
        texp->tex0.cbp = 0x3da0;
    }
    base = 0x800 - Mag * 0xe0;
    dmap[0] = 0x8002; dmap[1] = 0x10000000; dmap[2] = 0xe; dmap[3] = 0;
    *(struct_68d02bc7 *)&dmap[4] = texp->tex1;
    dmap[6] = 0x14; dmap[7] = 0;
    *(struct_1f802d8d *)&dmap[8] = texp->tex0;
    dmap[10] = 6; dmap[11] = 0;
    dmap += 12;
    do {
        alpha = InColF ? inAttr->col.a * fade_scale(inAttr, *pInIndex) >> 7 : inAttr->col.a;
        if (inAttr->type == 6) {
            c = *inCp;
            c = c < 0x21 || c > 0x7e ? 0 : (c - 0x21) & 0xff;
        } else {
            c = *inCp;
            if (c >= 'a' && c <= 'z') c -= 0x20;   /* toupper */
            if (CTYPE(c) & 3)
                c = (c - 'A') & 0xff;
            else if (CTYPE(c) & 4)
                c = (c - 0x16) & 0xff;   /* digits follow the 26 letters */
            else {
                const struct_00343825 *ep;
                for (ep = MFontEncode; ep->c[0]; ep++)
                    if (ep->c[0] == c) break;
                c = ep->c[0] ? (unsigned)(ep - MFontEncode + 0x24) & 0xff : 0x1a;
            }
        }
        u0 = top_u + w * ((int)c % cw);
        u1 = top_u + w * ((int)c % cw + 1);
        v0 = top_v + h * ((int)c / cw);
        v1 = top_v + h * ((int)c / cw + 1);
        if (!(inAttr->flag & 0x200) && (is_narrow(*inCp) || (inAttr->type == 6 && *inCp == '1'))) {
            inAttr->x += inAttr->narrow_offset;
            wk_w = inAttr->narrow_w;
            off_x = inAttr->narrow_w;
            u1 = u0 + off_x;
        } else {
            wk_w = inAttr->w;
            off_x = inAttr->off_x;
        }
        if (inAttr->flag & 0x100) {   /* shadow, 1 px right and (except type 4) 1 px down */
            int sy = inAttr->type != 4;
            tmpAttr = *inAttr;
            dmap[0] = 0x8002; dmap[1] = 0x10000000; dmap[2] = 0xe; dmap[3] = 0;
            dmap[4] = 0x60; dmap[5] = 0;
            dmap[6] = 0x14; dmap[7] = 0;
            MDraw_SetLinearOffset(1);
            tmpAttr.col.r = tmpAttr.col.g = tmpAttr.col.b = 0x50;
            dmap[8] = 0x42; dmap[9] = 0;
            dmap[10] = 0x42; dmap[11] = 0;
            dmap[12] = 0x8001; dmap[13] = 0x50ab4000; dmap[14] = 0x43431; dmap[15] = 0;
            dmap[16] = tmpAttr.col.r; dmap[17] = tmpAttr.col.g; dmap[18] = tmpAttr.col.b; dmap[19] = alpha;
            dmap[20] = MDraw_GetOffsetLeftU(u0 * 16);
            dmap[21] = MDraw_GetOffsetTopV(v0 * 16);
            dmap[22] = 0; dmap[23] = 0;
            dmap[24] = MDraw_GetOffsetLeftX((inAttr->x + 0x6c1) * 16);
            dmap[25] = MDraw_GetOffsetTopY((base + Mag * (inAttr->y + sy)) * 16);
            dmap[26] = inAttr->depth; dmap[27] = 0;
            dmap[28] = MDraw_GetOffsetRightU(u1 << 4);
            dmap[29] = MDraw_GetOffsetBottomV(v1 * 16);
            dmap[30] = 0; dmap[31] = 0;
            dmap[32] = MDraw_GetOffsetRightX((inAttr->x + wk_w + 0x6c1) * 16);
            dmap[33] = MDraw_GetOffsetBottomY((base + Mag * (sy + inAttr->y + inAttr->h)) * 16);
            dmap[34] = inAttr->depth; dmap[35] = 0;
            dmap += 36;
        }
        dmap[0] = 0x8002; dmap[1] = 0x10000000; dmap[2] = 0xe; dmap[3] = 0;
        dmap[4] = 0x60; dmap[5] = 0;
        dmap[6] = 0x14; dmap[7] = 0;
        MDraw_SetLinearOffset(1);
        dmap[8] = 0x48; dmap[9] = 0;
        dmap[10] = 0x42; dmap[11] = 0;
        dmap[12] = 0x8001; dmap[13] = 0x50ab4000; dmap[14] = 0x43431; dmap[15] = 0;
        dmap[16] = inAttr->col.r; dmap[17] = inAttr->col.g; dmap[18] = inAttr->col.b; dmap[19] = alpha;
        dmap[20] = MDraw_GetOffsetLeftU(u0 * 16);
        dmap[21] = MDraw_GetOffsetTopV(v0 * 16);
        dmap[22] = 0; dmap[23] = 0;
        dmap[24] = MDraw_GetOffsetLeftX((inAttr->x + pFontInfo->drawoffsetx + 0x6c0) * 16);
        dmap[25] = MDraw_GetOffsetTopY((base + Mag * (inAttr->y + pFontInfo->drawoffsety)) * 16);
        dmap[26] = inAttr->depth; dmap[27] = 0;
        dmap[28] = MDraw_GetOffsetRightU(u1 << 4);
        dmap[29] = MDraw_GetOffsetBottomV(v1 * 16);
        dmap[30] = 0; dmap[31] = 0;
        dmap[32] = MDraw_GetOffsetRightX((pFontInfo->drawoffsetx + inAttr->x + wk_w + 0x6c0) * 16);
        dmap[33] = MDraw_GetOffsetBottomY((base + Mag * (pFontInfo->drawoffsety + inAttr->y + inAttr->h)) * 16);
        dmap[34] = inAttr->depth; dmap[35] = 0;
        inCp++;
        dmap += 36;
        inAttr->x += off_x;
        if (DF_MDRAW_IsSPRBuffOver(dmap, 0x10) == 1) break;
        (*pInIndex)++;
    } while (__MFONT_IsDrawChar(*inCp) == 1);
    DF_MDRAW_GIFKickSPR(dmap);
    return inCp;
}

/* SJIS (or ASCII mapped to SJIS) -> glyph index in the kanji sheets, -1 if not in the font. */
static unsigned char *__MFONT_SJIS2Offset(unsigned char *ioCp, int *outOffset)
{
    static const struct { unsigned short lo, hi; int base, top; } ranges[] = {
        {0x889f, 0x9872, 0x000, 0}, {0x829f, 0x82f1, 0xb9c, 1}, {0x8340, 0x8396, 0xbf6, 2},
        {0x824f, 0x8258, 0xc8c, 3}, {0x8281, 0x829a, 0xc6e, 4}, {0x8260, 0x8279, 0xc50, 5},
        {0x8754, 0x875d, 0xc96, 6}, {0x8140, 0x81f4, 0xce7, 7}, {0x83bf, 0x83d6, 0xcbe, 8},
        {0x839f, 0x83b6, 0xca0, 9},
    };
    unsigned char c[2], *next;
    int idx, code;

    next = __MFONT_Ascii2SJIS(ioCp, c);
    idx = (c[0] - 0x81) * 0xbc + (c[1] >= 0x40 && c[1] <= 0x7e ? c[1] - 0x40 : c[1] - 0x41);
    code = c[0] << 8 | c[1];
    *outOffset = -1;
    for (unsigned i = 0; i < sizeof ranges / sizeof *ranges; i++)
        if (code >= ranges[i].lo && code <= ranges[i].hi) {
            *outOffset = ranges[i].base + (idx - (int)MFontTopIdx[ranges[i].top]);
            break;
        }
    return next;
}

/* Converts the character at ioCp to a 2-byte SJIS code; returns the next character. */
unsigned char *__MFONT_Ascii2SJIS(unsigned char *ioCp, unsigned char *outDstp)
{
    const struct_00343825 *ep;
    unsigned char c = *ioCp;

    outDstp[0] = ioCp[0];
    outDstp[1] = ioCp[1];
    if (CTYPE(c) & 3) {   /* letter: Ａ-Ｚ 0x8260.., ａ-ｚ 0x8281.. */
        outDstp[0] = 0x82;
        outDstp[1] = c + ((CTYPE(c) & 1) ? 0x1f : 0x20);
        return ioCp + 1;
    }
    if (CTYPE(c) & 4) {   /* digit: ０-９ 0x824f.. */
        outDstp[0] = 0x82;
        outDstp[1] = c + 0x1f;
        return ioCp + 1;
    }
    if (!(CTYPE(c) & 0x17)) return ioCp + 2;   /* already SJIS */
    for (ep = MFontEncode; ep->c[0]; ep++)
        if (ep->c[0] == c) {
            outDstp[0] = ep->code >> 8;
            outDstp[1] = (unsigned char)ep->code;
            return ioCp + 1;
        }
    outDstp[0] = 0x81;   /* unknown punctuation -> full-width space glyph slot */
    outDstp[1] = 0xa1;
    return ioCp + 1;
}

static int __MFONT_IsSjisHan(unsigned char *pc)
{
    unsigned char c = *pc;
    const unsigned char *k;

    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c >= 0x91 && c <= 0x94) ||
        c == 0xae || c == '|')
        return 1;
    for (k = g_acSjisHanKigouChar; *k; k++)
        if (c == *k) return 1;
    return 0;
}

static int __MFONT_IsDrawChar(unsigned char inC)
{
    if (CTYPE(inC) & 7) return 1;
    if (inC == '<' || inC == '>') return 0;
    return inC == '|' || inC == 0xae || (inC >= 0x91 && inC <= 0x94) || inC == 0x96 || (inC >= 0x21 && inC <= 0x7e);
}

void DF_MFONT_Draw(struct_acb9a91f *inAttr, int InColF)
{
    PS2_FRAME(0x50);
    struct_acb9a91f *const attr = &DF_MFONT_Draw__attr;   /* function-static in the original */
    unsigned char *cp;
    int ctrlID, Index = 0;

    if (!inAttr || !(cp = inAttr->strp) || inAttr->type < 0 || inAttr->type >= 9) return;
    PS2_RQ_SCOPE(RQ_K_TEXT, RQ_OBJID(RQ_OC_MENU, PS2PTR(inAttr->strp)), inAttr->type);   /* render tag (host only) */
    DF_MDRAW_KickReg(0x42, 0x44);
    DF_MDRAW_KickReg(0x47, 0x3000f);
    *attr = *inAttr;
    /* SyncDCache(&attr, ...): no-op on the host */
    while (*cp) {
        if (attr->flag & 1) {   /* SJIS text */
            if ((attr->flag & 0x400) && __MFONT_IsSjisHan(cp)) {
                cp = __MFONT_DrawSJISHan(cp, attr, &Index, InColF);
                continue;
            }
            if (is_sjis_lead(*cp) || __MFONT_IsDrawChar(*cp) == 1) {
                cp = __MFONT_DrawSJIS(cp, attr, &Index, InColF);
                continue;
            }
        }
        if (*cp == ' ') {
            cp++;
            attr->x += attr->space_w;
        } else if (__MFONT_IsDrawChar(*cp) == 1)
            cp = __MFONT_DrawChar(cp, attr, &Index, InColF);
        else if (*cp == '<')
            cp = __MFONT_ProcCtrlCode(cp, &ctrlID, attr, inAttr);
        else
            cp++;
    }
    ps2_dma_wait_gif();
}

void DF_MFONT_Init(void)
{
    for (int i = 0; i < 11; i++) {
        unsigned int lo = MFontTopTblIdx[i] & 0xff;
        MFontTopIdx[i] = (((MFontTopTblIdx[i] & 0xff00) >> 8) - 0x81) * 0xbc + (lo >= 0x40 && lo <= 0x7e ? lo - 0x40 : lo - 0x41);
    }
    MFontInit = 1;
}
