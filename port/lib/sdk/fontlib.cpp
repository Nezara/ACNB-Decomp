/* lib/sdk/fontlib.cpp - fslibfont, FromSoftware's DWARF-less text renderer (main ELF 0x1001e0-0x104ab0; map
 * ). UTF-16 / SJIS strings are drawn glyph by glyph as GS sprites from a CCM glyph map
 * and TM2 pages that come from the MMO movie blob; glyph quads are collected as GIF packets in a DMA buffer and kicked
 * to D2 at the outermost End. Only user: lib/fs/fslibmmo (prologue + staff roll).
 *
 * Group F1a: init / select (FsFontInit 0x1007f0 and its helpers), the per-frame state setters, Begin / End, the packet
 * header (0x1020a0) and the D2 kick (0x101d70). Group F1b: the text path FsFontDrawText calls - the vsprintf front end
 * (0x1025e0, UTF-16 <-> UTF-8 conversion around newlib vsprintf), FsFontDraw (0x1032a0: measure, align, draw lines),
 * the glyph loop down to the drawFunc call (0x102690), the measure pass (0x103ea0), the glyph lookup, the char readers
 * and the small accessors. Group F2: the drawFunc (0x100b40) and the GS packet writers below it - glyph quad, page
 * TEX state, TM2 page / CLUT upload chain (CNT + REF into the guest texture), buffer-room check. Group F3 (unreached,
 * asm-verified only): the measure entry 0x1034d0, the '#' tag ops r / c / a / b / i and their colour helpers. With
 * F3 the whole library is native. Names stay fontlib_<addr> (sdk.h); the map's proposed names are in the comments.
 *
 * All state lives in guest memory: the six gp sbss words below point into the current _FS_FONT_ST (s_Font) and are
 * read by still-interpreted fontlib code, so they are nodwarf globals at their PS2 addresses (runtime/nodwarf_abs.s),
 * as is the 256-byte character class table. Stored function pointers (charNextFP / charCheckFP / drawFunc / tag
 * functions) are the original code addresses (GFN). Registered at the PS2 addresses by runtime/bridge_sdk.c. */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/types.h"
#include "acnb/nodwarf.h"
#include "acnb/ps2fp.h"

#ifndef ACNB_SOURCEPORT
/* DWARF-less code addresses stored into guest state (no PS2ADDR_ from functions.json for SDK regions) */
#define PS2ADDR_fontlib_00100b40 0x00100b40u
#define PS2ADDR_fontlib_00103040 0x00103040u
#define PS2ADDR_fontlib_00103090 0x00103090u
#define PS2ADDR_fontlib_001030a0 0x001030a0u
#define PS2ADDR_fontlib_00103110 0x00103110u
#define PS2ADDR_fontlib_00104370 0x00104370u
#define PS2ADDR_fontlib_001043b0 0x001043b0u
#define PS2ADDR_fontlib_001043f0 0x001043f0u
#define PS2ADDR_fontlib_00104630 0x00104630u
#define PS2ADDR_fontlib_001047d0 0x001047d0u
#else
_FS_FONT_DATA_ST *fsfont_pFontData;
struct_c5a57ef2 *fsfont_pPs2Info;
_FS_FONT_DRAW_STATE_ST *fsfont_pDrawState;
_FS_FONT_FORM_ST *fsfont_pStdForm;
_FS_FONT_DEFTAG_MNG_ST *fsfont_pDefTagMng;
_FS_FONT_TAG_MNG_ST *fsfont_pTagMng;
unsigned char fsfont_charclass[256];
#endif

/* ------------------------------------------------------------------ size helpers and tiny accessors */

/* fsfont_SizeFontData (0x100760): bytes of _FS_FONT_DATA_ST + n texture pointers when flags b0 */
unsigned int fontlib_00100760(unsigned int flags, int n)
{
    if (!(flags & 0xff & 1)) return 0;
    return (unsigned int)n * 4 + 0x14;
}

/* fsfont_SizeDrawState (0x103020): drawState + stdForm when flags & 3 */
unsigned int fontlib_00103020(unsigned int flags) { return (flags & 0xff & 3) ? 0x54u : 0u; }

/* fsfont_SizePs2Info (0x103a00): defTagMng + tagMng when flags & 7 */
unsigned int fontlib_00103a00(unsigned int flags) { return (flags & 0xff & 7) ? 0xc4u : 0u; }

/* fsfont_GetTexNum (0x1004c0): no null check */
int fontlib_001004c0(void) { return fsfont_pFontData->texNum; }

/* fsfont_SetFontData (0x1006a0): returns the previous fontData */
_FS_FONT_DATA_ST *fontlib_001006a0(_FS_FONT_DATA_ST *fd)
{
    _FS_FONT_DATA_ST *old = fsfont_pFontData;
    fsfont_pFontData = fd;
    return old;
}

/* fsfont_SetDrawFunc (0x102cc0) */
int fontlib_00102cc0(void (*fn)(_FS_FONT_DRAW_ST *))
{
    fsfont_pDrawState->drawFunc = fn;
    return 0;
}

/* fsfont_GetFontHeight (0x100590): -5 without a CCM; out (if any) = (float)fontHeight */
int fontlib_00100590(float *out)
{
    _FS_CCM_HEADER_ST *ccm = fsfont_pFontData->pCCM;
    if (!ccm) return -5;
    if (out) *out = (float)ccm->fontHeight;
    return 0;
}

/* fsfont_GetCharCode (0x1005e0): ccm->charCode (0 = byte / SJIS, else UTF-16), -5 without a CCM */
int fontlib_001005e0(void)
{
    _FS_CCM_HEADER_ST *ccm = fsfont_pFontData->pCCM;
    if (!ccm) return -5;
    return ccm->charCode;
}

/* charNextFP / charCheckFP by the CCM's char code (inline in 0x102cd0 / 0x102d60 / 0x102e10) */
static void fsfont_set_char_funcs(int code)
{
    if (code) {
        fsfont_pDrawState->charNextFP = GFN(fontlib_00103110);
        fsfont_pDrawState->charCheckFP = GFN(fontlib_00103090);
    } else {
        fsfont_pDrawState->charNextFP = GFN(fontlib_001030a0);
        fsfont_pDrawState->charCheckFP = GFN(fontlib_00103040);
    }
}

/* pCCM / pCCMBlock / pCCMGlyph from a CCM header (the header's pBlock / pGlyph words are offsets; gp re-read per
 * store, inline in 0x100650 / 0x1006b0) */
static void fsfont_set_ccm(_FS_CCM_HEADER_ST *ccm)
{
    fsfont_pFontData->pCCM = ccm;
    _FS_CCM_HEADER_ST *h = fsfont_pFontData->pCCM;
    fsfont_pFontData->pCCMBlock = (_FS_CCM_BLOCK_ST *)((char *)h + PS2PTR(h->pBlock));
    h = fsfont_pFontData->pCCM;
    fsfont_pFontData->pCCMGlyph = (_FS_CCM_GLYPH_ST *)((char *)h + PS2PTR(h->pGlyph));
}

/* ------------------------------------------------------------------ init / select */

/* fsfont_InitFontData (0x1006b0): makes fd current, clears it; -1 without a CCM (texNum / ppTexBuff set anyway) */
int fontlib_001006b0(_FS_FONT_DATA_ST *fd, _FS_CCM_HEADER_ST *ccm, int texNum)
{
    PS2_FRAME(0x40);
    fsfont_pFontData = fd;
    ps2_memset(fsfont_pFontData, 0, 0x14);
    fsfont_pFontData->texNum = texNum;
    fsfont_pFontData->ppTexBuff = (unsigned char **)((char *)fd + 0x14);
    if (!ccm) return -1;
    fsfont_set_ccm(ccm);
    return 0;
}

/* fsfont_SetRect (0x103280): F1b's accessor, here because InitDrawState calls it; v0 is left untouched (void) */
void fontlib_00103280(_FS_FONT_RECT_ST *rect, float left, float top, float right, float bottom)
{
    if (!rect) return;
    rect->left = left;
    rect->top = top;
    rect->right = right;
    rect->bottom = bottom;
}

/* fsfont_InitCharClass (0x103130): fsfont_charclass = 0x10, digits / hex letters -> value, |= 0x20 on the SJIS lead
 * bytes 0x80-0x9f / 0xe0-0xff (same byte order as the original) */
void fontlib_00103130(void)
{
    PS2_FRAME(0x10);
    ps2_memset(fsfont_charclass, 0x10, 0x100);
    for (int i = 0; i < 10; i++) fsfont_charclass[0x30 + i] = (unsigned char)i;
    for (int i = 0; i < 6; i++) {
        fsfont_charclass[0x61 + i] = (unsigned char)(0xa + i);
        fsfont_charclass[0x41 + i] = (unsigned char)(0xa + i);
    }
    for (int i = 0; i < 0x100; i++)
        if ((i >= 0x80 && i < 0xa0) || i >= 0xe0) fsfont_charclass[i] |= 0x20;
}

/* fsfont_InitDrawState (0x102e10): fontData + drawState / stdForm defaults; returns 0x1006b0's result */
int fontlib_00102e10(_FS_FONT_ST *font, _FS_CCM_HEADER_ST *ccm, int texNum)
{
    PS2_FRAME(0x40);
    int r = fontlib_001006b0(&font->fontData, ccm, texNum);
    fontlib_001006a0(&font->fontData);
    int n = fontlib_001004c0();
    fsfont_pDrawState = (_FS_FONT_DRAW_STATE_ST *)((char *)font + fontlib_00100760(3, n));
    fsfont_pStdForm = (_FS_FONT_FORM_ST *)((char *)fsfont_pDrawState + 0x3c);
    int code = fontlib_001005e0();
    if (code >= 0) fsfont_set_char_funcs(code);
    fsfont_pDrawState->x = 0.0f;
    fsfont_pDrawState->y = 0.0f;
    fsfont_pDrawState->x = 0.0f;
    fsfont_pDrawState->y = 0.0f;
    fontlib_00103280(&fsfont_pDrawState->rect, 0.0f, 0.0f, 0.0f, 0.0f);
    fsfont_pStdForm->scaleX = 1.0f;
    fsfont_pStdForm->scaleY = 1.0f;
    PS2_STACK(float, h, 0x3c);
    if (fontlib_00100590(&h) >= 0) {
        _FS_FONT_FORM_ST *f = fsfont_pStdForm;
        f->totalHeight = f->spaceY + h * f->scaleY;
    }
    for (int i = 0; i < 4; i++) fsfont_pDrawState->color[i] = 0x7f7f7f7fu;
    fsfont_pStdForm->alignX = 0;
    fsfont_pStdForm->alignY = 0;
    fsfont_pDrawState->dummyCode = 0x3f;
    fontlib_00103130();
    code = fontlib_001005e0();
    if (code >= 0) fsfont_set_char_funcs(code);
    return r;
}

/* fsfont_SelectDrawState (0x102d60): makes font current up to drawState / stdForm; returns the previous fontData */
_FS_FONT_DATA_ST *fontlib_00102d60(_FS_FONT_ST *font)
{
    PS2_FRAME(0x30);
    _FS_FONT_DATA_ST *old = fontlib_001006a0(&font->fontData);
    int n = fontlib_001004c0();
    fsfont_pDrawState = (_FS_FONT_DRAW_STATE_ST *)((char *)font + fontlib_00100760(3, n));
    fsfont_pStdForm = (_FS_FONT_FORM_ST *)((char *)fsfont_pDrawState + 0x3c);
    int code = fontlib_001005e0();
    if (code >= 0) fsfont_set_char_funcs(code);
    return old;
}

/* fsfont_SelectDefTag (0x103840): + defTagMng / tagMng; returns the previous fontData */
_FS_FONT_DATA_ST *fontlib_00103840(_FS_FONT_ST *font)
{
    PS2_FRAME(0x30);
    _FS_FONT_DATA_ST *old = fontlib_00102d60(font);
    int n = fontlib_001004c0();
    char *p = (char *)font + fontlib_00100760(7, n);
    fsfont_pDefTagMng = (_FS_FONT_DEFTAG_MNG_ST *)(p + fontlib_00103020(7));
    fsfont_pTagMng = (_FS_FONT_TAG_MNG_ST *)((char *)fsfont_pDefTagMng + 0x40);
    return old;
}

/* fsfont_Setup (0x1038b0): InitDrawState + defTagMng defaults (ruby scale, italic 3.0) + empty tag list. A failed
 * 0x100590 (no CCM) leaves the height slot sp+0x38 unwritten: the original multiplies the stale word by the ruby
 * scale (0 here), hence PS2_STACK. */
int fontlib_001038b0(_FS_FONT_ST *font, _FS_CCM_HEADER_ST *ccm, int texNum)
{
    PS2_FRAME(0x40);
    int r = fontlib_00102e10(font, ccm, texNum);
    fontlib_00102d60(font);
    int n = fontlib_001004c0();
    char *p = (char *)font + fontlib_00100760(7, n);
    fsfont_pDefTagMng = (_FS_FONT_DEFTAG_MNG_ST *)(p + fontlib_00103020(7));
    _FS_FONT_DEFTAG_MNG_ST *d = fsfont_pDefTagMng;
    fsfont_pTagMng = (_FS_FONT_TAG_MNG_ST *)((char *)d + 0x40);
    if (d) {
        ps2_memset(d, 0, 4);
        PS2_STACK(unsigned int, mask, 0x3c);
        mask = 0x00ffffffu;   /* bytes ff ff ff 00 built at sp+0x3c */
        fsfont_pDefTagMng->colorMask = mask;
    }
    fsfont_pDefTagMng->rubyForm.alignX = 0;
    fsfont_pDefTagMng->saveScaleX = 0.0f;
    fsfont_pDefTagMng->saveScaleY = 0.0f;
    PS2_STACK(float, h, 0x38);
    PS2_STACK(float, sx, 0x34);
    PS2_STACK(float, sy, 0x30);
    fontlib_00100590(&h);
    fontlib_00102950(&sx, &sy);
    d = fsfont_pDefTagMng;
    d->rubyForm.scaleX = sx * d->saveScaleX;
    d = fsfont_pDefTagMng;
    d->rubyForm.scaleY = sy * d->saveScaleY;
    d = fsfont_pDefTagMng;
    d->rubyForm.totalHeight = d->rubyForm.spaceY + h * d->rubyForm.scaleY;
    fsfont_pDefTagMng->italic = 3.0f;
    if (fsfont_pTagMng) ps2_memset(fsfont_pTagMng, 0, 4);
    return r;
}

/* FsFontInit (0x1007f0): font context + ps2Info cleared + drawFunc 0x100b40; 0x1038b0's result is ignored */
int fontlib_001007f0(_FS_FONT_ST *font, _FS_CCM_HEADER_ST *ccm, int texNum)
{
    PS2_FRAME(0x20);
    fontlib_001038b0(font, ccm, texNum);
    fontlib_00103840(font);
    char *p = (char *)font + fontlib_00100760(0x15, fontlib_001004c0());
    p += fontlib_00103020(0x15);
    fsfont_pPs2Info = (struct_c5a57ef2 *)(p + fontlib_00103a00(0x15));
    if (fsfont_pPs2Info) ps2_memset(fsfont_pPs2Info, 0, 0x40);
    fontlib_00102cc0(GFN(fontlib_00100b40));
    return 0;
}

/* FsFontSelect (0x100780): makes font current (every state pointer re-derived); returns the previous font */
void *fontlib_00100780(void *font)
{
    PS2_FRAME(0x30);
    _FS_FONT_DATA_ST *old = fontlib_00103840((_FS_FONT_ST *)font);
    char *p = (char *)font + fontlib_00100760(0x15, fontlib_001004c0());
    p += fontlib_00103020(0x15);
    fsfont_pPs2Info = (struct_c5a57ef2 *)(p + fontlib_00103a00(0x15));
    return old;
}

/* FsFontAddTagFunc (0x103670): registers the '#' tags by mask (b0 r, b1 c, b2 a, b3 b); -0x20 when the list is (or
 * becomes) full. Quirk: 'i' is gated by mask & 1 (register reuse in the original), not b4. */
static int fsfont_add_tag(_FS_FONT_TAG_INFO_ST *p, unsigned int code, int (*fn)(unsigned char **, int))
{
    p->code = code;
    p->analyzeTagFunc = fn;
    fsfont_pTagMng->nListedTag++;
    return fsfont_pTagMng->nListedTag == 16;
}

int fontlib_00103670(unsigned int mask)
{
    _FS_FONT_TAG_MNG_ST *o = fsfont_pTagMng;
    unsigned int n = o->nListedTag;
    if (n >= 16) return -0x20;
    _FS_FONT_TAG_INFO_ST *p = &o->tagList[n];
    if (mask & 1) {
        if (fsfont_add_tag(p, 0x72, GFN(fontlib_001043f0))) return -0x20;
        p++;
    }
    if (mask & 2) {
        if (fsfont_add_tag(p, 0x63, GFN(fontlib_001047d0))) return -0x20;
        p++;
    }
    if (mask & 4) {
        if (fsfont_add_tag(p, 0x61, GFN(fontlib_00104630))) return -0x20;
        p++;
    }
    if (mask & 8) {
        if (fsfont_add_tag(p, 0x62, GFN(fontlib_001043b0))) return -0x20;
        p++;
    }
    if (mask & 1) {
        if (fsfont_add_tag(p, 0x69, GFN(fontlib_00104370))) return -0x20;
    }
    return 0;
}

/* FsFontSetOption (0x100c10): ps2Info option bit opt (0 bfMode, 1 bfConText, 2 bfDmaOverDraw, 3 bfFfmd, 4 bfTransTex,
 * 5 bfUseSPR) = on & 1 */
int fontlib_00100c10(unsigned int opt, int on)
{
    struct_c5a57ef2 *p = fsfont_pPs2Info;
    unsigned char v = (unsigned char)(on & 1);
    switch (opt) {
    case 0: p->bfMode = v; break;
    case 1: p->bfConText = v; break;
    case 2: p->bfDmaOverDraw = v; break;
    case 3: p->bfFfmd = v; break;
    case 4: p->bfTransTex = v; break;
    case 5: p->bfUseSPR = v; break;
    default: break;
    }
    return 0;
}

/* FsFontGetDmaBufferSize (0x100d90): bytes for n strings ((qwords per string * n + 5) * 16; 32-bit multiply) */
unsigned int fontlib_00100d90(int n, int flagA)
{
    if (n == 0) return 0;
    unsigned int a2 = flagA == 0 ? 7u : 9u, a3 = 0xb;
    if (!fsfont_pPs2Info->bfMode) {
        a2 += 1;
        a3 += 2;
    }
    if (fsfont_pPs2Info->bfTransTex == 1) a2 += a3;
    a2 = a2 * (unsigned int)n;
    return (a2 + 5) << 4;
}

/* FsFontSetDmaBuffer (0x100d30): a buffer smaller than one string's packet is dropped */
int fontlib_00100d30(void *buf, unsigned int size)
{
    PS2_FRAME(0x30);
    if (size < fontlib_00100d90(1, 0)) {
        buf = 0;
        size = 0;
    }
    fsfont_pPs2Info->pPacketCurrent = buf;
    fsfont_pPs2Info->uiPacketBuffSize = size;
    return 0;
}

/* ------------------------------------------------------------------ per-frame state */

/* FsFontSetColor (0x102b30) */
int fontlib_00102b30(unsigned int rgba)
{
    fsfont_pDrawState->color[0] = rgba;
    fsfont_pDrawState->color[1] = rgba;
    fsfont_pDrawState->color[2] = rgba;
    fsfont_pDrawState->color[3] = rgba;
    return 0;
}

/* FsFontSetAlign (0x102b60): alignX = flag b4-5, alignY = b6-7 (two byte read-modify-writes) */
int fontlib_00102b60(unsigned int flag)
{
    fsfont_pStdForm->alignX = (flag & 0x30) >> 4;
    fsfont_pStdForm->alignY = (flag & 0xc0) >> 6;
    return 0;
}

/* FsFontSetRubyAlign (0x103640) */
int fontlib_00103640(int a)
{
    fsfont_pDefTagMng->rubyForm.alignX = a & 3;
    return 0;
}

/* FsFontSetCCM (0x100650) */
int fontlib_00100650(void *ccm)
{
    if (!ccm) return -1;
    fsfont_set_ccm((_FS_CCM_HEADER_ST *)ccm);
    return 0;
}

/* FsFontSetCharFunc (0x102cd0): char reader pair by the current CCM; returns 0x1005e0's error (-5) */
int fontlib_00102cd0(void)
{
    PS2_FRAME(0x10);
    int code = fontlib_001005e0();
    if (code < 0) return code;
    fsfont_set_char_funcs(code);
    return 0;
}

/* FsFontSetTex (0x100630): no bounds check */
int fontlib_00100630(int slot, void *tex)
{
    fsfont_pFontData->ppTexBuff[slot] = (unsigned char *)tex;
    return 0;
}

/* FsFontSetScale (0x102bc0): stdForm scale + totalHeight (spaceY + fontHeight * scaleY) when a CCM is set */
int fontlib_00102bc0(float sx, float sy)
{
    PS2_FRAME(0x20);
    fsfont_pStdForm->scaleX = sx;
    fsfont_pStdForm->scaleY = sy;
    PS2_STACK(float, h, 0x1c);
    if (fontlib_00100590(&h) >= 0) {
        _FS_FONT_FORM_ST *f = fsfont_pStdForm;
        f->totalHeight = f->spaceY + h * f->scaleY;
    }
    return 0;
}

/* FsFontSetPos (0x102c60): mode b0 = relative to the current pos, b1 set x, b2 set y */
int fontlib_00102c60(unsigned int mode, float x, float y)
{
    unsigned int m = mode & 0xff;
    if (m & 1) {
        _FS_FONT_DRAW_STATE_ST *d = fsfont_pDrawState;
        x = d->x + x;
        y = d->y + y;
    }
    if (m & 2) fsfont_pDrawState->x = x;
    if (m & 4) fsfont_pDrawState->y = y;
    return 0;
}

/* fsfont_SetLineHeight (0x102a00) */
int fontlib_00102a00(float h)
{
    fsfont_pDrawState->lineHeight = h;
    return 0;
}

/* fsfont_GetScale (0x102950) */
int fontlib_00102950(float *sx, float *sy)
{
    if (sx) *sx = fsfont_pStdForm->scaleX;
    if (sy) *sy = fsfont_pStdForm->scaleY;
    return 0;
}

/* fsfont_GetPos (0x102990) */
int fontlib_00102990(float *x, float *y)
{
    if (x) *x = fsfont_pDrawState->x;
    if (y) *y = fsfont_pDrawState->y;
    return 0;
}

/* ------------------------------------------------------------------ packet frame */

/* SPR double buffer flip (bfUseSPR; never set by the game: FsFontSetOption 5 has no caller) */
static void fsfont_spr_flip(void)
{
    struct_c5a57ef2 *p = fsfont_pPs2Info;
    unsigned int i = p->SprBuffIndex ^ 1;
    p->SprBuffIndex = i;
    p = fsfont_pPs2Info;
    p->pDmaBuff = (char *)p->pSprBuff + ((p->uiSprBuffSize * i) >> 1);
}

/* cursor back to the buffer start + an open CNT tag {0x10000000, 0, 0, 0} (inline in 0x1009c0 / 0x101d70) */
static void fsfont_open_cnt(void)
{
    struct_c5a57ef2 *p = fsfont_pPs2Info;
    p->pDmaCurrent = p->pDmaBuff;
    p = fsfont_pPs2Info;
    p->pDmaLatest = p->pDmaCurrent;
    unsigned int *t = (unsigned int *)fsfont_pPs2Info->pDmaCurrent;
    if (t) {
        t[0] = 0x10000000u;
        t[1] = 0;
        *(unsigned long long *)(t + 2) = 0;
    }
    fsfont_pPs2Info->usEntryStrNum = 0;
    fsfont_pPs2Info->ucBfTransTexId = 0xff;
}

/* fsfont_BeginPacket (0x1020a0): CNT tag (qwc 4, 5 on the VIF path) + GIF tag (NLOOP 3, EOP, NREG 1, REGS A+D) +
 * CLAMP / TEST / ALPHA of the selected context. The GIF tag's low word is built by MWCC bitfield read-modify-writes
 * over the buffer's previous contents; the masks are copied as they are (every bit ends up rewritten). */
void fontlib_001020a0(void)
{
    PS2_FRAME(0x70);
    struct_c5a57ef2 *p = fsfont_pPs2Info;
    if (!p->pDmaBuff) return;
    unsigned char *t = (unsigned char *)p->pDmaCurrent;
    if (!t) return;
    unsigned int qwc = 4;
    if (!p->bfMode) qwc += 1;
    p->pDmaLatest = t;
    unsigned int *w = (unsigned int *)t;
    w[0] = (qwc & 0xffff) | 0x10000000u;
    w[1] = qwc;
    *(unsigned long long *)(t + 8) = 0;
    unsigned char *g = t + 16;
    if (!fsfont_pPs2Info->bfMode) {   /* VIF1 DIRECTHL 4 (unreached) */
        w = (unsigned int *)g;
        w[0] = 0;
        w[1] = 0;
        w[2] = 0;
        w[3] = 0x51000004u;
        g += 16;
    }
    unsigned long long *q = (unsigned long long *)g;
    *(unsigned short *)g = (unsigned short)((*(unsigned short *)g & 0x8000) | 3);   /* NLOOP 3 */
    g[1] = (unsigned char)((g[1] & 0x7f) | 0x80);                                    /* EOP */
    q[0] = (q[0] & 0xffffc0000000ffffull) | 0;                                       /* bits 16-45 */
    g[5] = (unsigned char)((g[5] & 0xbf) | 0);                                       /* PRE */
    q[0] = (q[0] & 0xfc007fffffffffffull) | 0;                                       /* PRIM */
    g[7] = (unsigned char)((g[7] & 0xf3) | 0);                                       /* FLG */
    g[7] = (unsigned char)((g[7] & 0x0f) | 0x10);                                    /* NREG 1 */
    q[1] = 0xe;                                                                      /* REGS A+D */
    p = fsfont_pPs2Info;
    int ctx2 = p->bfConText;
    q[2] = 0;
    q[3] = ctx2 ? 9 : 8;          /* CLAMP_1 / _2 */
    q[4] = 0x31001;
    q[5] = ctx2 ? 0x48 : 0x47;    /* TEST_1 / _2 */
    q[6] = 0x44;
    q[7] = ctx2 ? 0x43 : 0x42;    /* ALPHA_1 / _2 */
    p->pDmaCurrent = g + 0x40;
}

/* fsfont_KickDma (0x101d70): sends the packet chain from pDmaBuff on D2 (QWC 0, MADR 0, TADR, FlushCache, CHCR 0x145,
 * wait) and reopens the buffer. bfUseSPR arms (fromSPR copy-back on D8 / SPR-side TADR) are unreached: the game
 * never sets the option. */
void fontlib_00101d70(void)
{
    PS2_FRAME(0x30);
    struct_c5a57ef2 *p = fsfont_pPs2Info;
    void *buf = p->pDmaBuff;
    if (!buf) return;
    if (p->bfUseSPR && !p->bfMode) {
        /* unreached: copy the SPR packet back to the packet buffer (D8 fromSPR, normal mode) */
        unsigned int qwc = (PS2PTR(p->pDmaCurrent) - PS2PTR(buf)) >> 4;
        FlushCache(0);
        void *d8 = (void *)(uintptr_t)0x1000d000u;
        while (ps2_dma_chan_rd32(d8, 0) & 0x100) {}
        ps2_dma_chan_wr32(d8, 0x10, PS2PTR(fsfont_pPs2Info->pPacketCurrent));
        ps2_dma_chan_wr32(d8, 0x20, qwc);
        ps2_dma_chan_wr32(d8, 0x80, PS2PTR(buf));
        ps2_dma_chan_wr32(d8, 0, 0x100);
        p = fsfont_pPs2Info;
        p->pPacketCurrent = (char *)p->pPacketCurrent + (qwc << 4);
    } else {
        ps2_gif_wait_idle();
        if (fsfont_pPs2Info->bfUseSPR) {
            ps2_dma_kick_tadr_madr(2, 0x145, PS2PTR(buf) | 0x80000000u, 0);   /* unreached */
        } else {
            unsigned int tadr = PS2PTR(fsfont_pPs2Info->pDmaBuff);
            FlushCache(0);
            ps2_dma_kick_tadr_madr(2, 0x145, tadr, 0);
            ps2_gif_wait_idle();
        }
    }
    if (fsfont_pPs2Info->bfUseSPR) fsfont_spr_flip();
    fsfont_open_cnt();
}

/* FsFontBegin (0x1009c0): nesting counter; the outermost call resets the packet buffer and writes the header */
int fontlib_001009c0(void)
{
    PS2_FRAME(0x10);
    struct_c5a57ef2 *p = fsfont_pPs2Info;
    unsigned short d = p->usBegin;
    p->usBegin = (unsigned short)(d + 1);
    if (d) return 0;
    fsfont_pPs2Info->ucBfTransTexId = 0xff;
    p = fsfont_pPs2Info;
    if (p->bfUseSPR) {   /* unreached */
        if (p->uiSprBuffSize == 0 || p->pSprBuff == 0) {
            p->bfUseSPR = 0;
            fontlib_001009c0();   /* usBegin is 2 now: returns at once */
            return 0;
        }
        p->uiDmaBuffSize = p->uiSprBuffSize >> 1;
    } else {
        p->pDmaBuff = p->pPacketCurrent;
        p = fsfont_pPs2Info;
        p->uiDmaBuffSize = p->uiPacketBuffSize;
    }
    p = fsfont_pPs2Info;
    p->pCodeCurrent = p->pCodeBuff;
    fsfont_pPs2Info->usCodeEntryNum = 0;
    if (fsfont_pPs2Info->bfUseSPR) fsfont_spr_flip();
    fsfont_open_cnt();
    fsfont_pPs2Info->usStrNum = 0;
    fontlib_001020a0();
    return 0;
}

/* FsFontEnd (0x100880): closes the outermost Begin: last tag -> END (GIF direct) / RET (VIF), kick when strings were
 * entered; returns the packet cursor */
void *fontlib_00100880(void)
{
    PS2_FRAME(0x10);
    struct_c5a57ef2 *p = fsfont_pPs2Info;
    if (p->usBegin == 0) return 0;
    p->usBegin = (unsigned short)(p->usBegin - 1);
    if (p->usBegin != 0) return 0;
    p = fsfont_pPs2Info;
    if (p->pDmaBuff && p->pDmaCurrent && p->pDmaLatest) {
        unsigned int *t = (unsigned int *)p->pDmaLatest;
        t[0] &= 0x8fffffffu;
        if (!fsfont_pPs2Info->bfMode) t[0] |= 0x60000000u;
        else t[0] |= 0x70000000u;
        t[1] = 0;
    }
    p = fsfont_pPs2Info;
    if ((p->bfUseSPR || p->bfMode == 1) && p->usEntryStrNum > 0) fontlib_00101d70();
    p = fsfont_pPs2Info;
    return p->bfUseSPR ? p->pPacketCurrent : p->pDmaCurrent;
}

/* ================================================================== F1b: text path */

/* ------------------------------------------------------------------ char readers (stored as charNextFP / charCheckFP) */

/* fsfont_CharCheck8 (0x103040): byte / SJIS peek (lead byte = charclass bit 0x20 -> two bytes, big endian) */
unsigned short fontlib_00103040(unsigned char *p)
{
    unsigned int b = p[0];
    if (fsfont_charclass[b] & 0x20) return (unsigned short)(((b << 8) & 0xffff) | p[1]);
    return (unsigned short)b;
}

/* fsfont_CharCheck16 (0x103090): UTF-16 peek */
unsigned short fontlib_00103090(unsigned char *p) { return *(unsigned short *)p; }

/* fsfont_CharNext8 (0x1030a0): byte / SJIS read + advance (the lead byte is re-read after the first advance) */
unsigned short fontlib_001030a0(unsigned char **pp)
{
    unsigned char *s = *pp;
    if (fsfont_charclass[s[0]] & 0x20) {
        *pp = s + 1;
        unsigned int v = ((unsigned int)s[0] << 8) & 0xffff;
        s = *pp;
        *pp = s + 1;
        return (unsigned short)(v | s[0]);
    }
    *pp = s + 1;
    return s[0];
}

/* fsfont_CharNext16 (0x103110): UTF-16 read + advance */
unsigned short fontlib_00103110(unsigned char **pp)
{
    unsigned char *s = *pp;
    unsigned short v = *(unsigned short *)s;
    *pp = s + 2;
    return v;
}

/* fsfont_CharCheck (0x102670) / fsfont_CharNext (0x102680): tail jumps through the drawState's reader pair. The
 * cursor must be PS2 memory in the hybrid (callers keep it in a PS2_STACK slot). */
unsigned short fontlib_00102670(unsigned char *p) { return gcall(fsfont_pDrawState->charCheckFP, p); }
unsigned short fontlib_00102680(unsigned char **pp) { return gcall(fsfont_pDrawState->charNextFP, pp); }

/* ------------------------------------------------------------------ small accessors */

/* fsfont_GetStdForm (0x102860) */
_FS_FONT_FORM_ST *fontlib_00102860(void) { return fsfont_pStdForm; }

/* fsfont_GetRect (0x102870) */
_FS_FONT_RECT_ST *fontlib_00102870(void) { return &fsfont_pDrawState->rect; }

/* fsfont_GetDummyCode (0x102880) */
unsigned short fontlib_00102880(void) { return fsfont_pDrawState->dummyCode; }

/* fsfont_GetTotalHeight (0x102890) */
int fontlib_00102890(float *out)
{
    if (out) *out = fsfont_pStdForm->totalHeight;
    return 0;
}

/* fsfont_GetLineHeight (0x1028b0) */
int fontlib_001028b0(float *out)
{
    if (out) *out = fsfont_pDrawState->lineHeight;
    return 0;
}

/* fsfont_GetAlign (0x102910) */
int fontlib_00102910(unsigned char *ax, unsigned char *ay)
{
    if (ax) *ax = fsfont_pStdForm->alignX;
    if (ay) *ay = fsfont_pStdForm->alignY;
    return 0;
}

/* fsfont_SetPosRect (0x102c20): pos = (left, top), rect = all four (f12-f15 passed on unchanged) */
int fontlib_00102c20(float left, float top, float right, float bottom)
{
    PS2_FRAME(0x10);
    fsfont_pDrawState->x = left;
    fsfont_pDrawState->y = top;
    fontlib_00103280(&fsfont_pDrawState->rect, left, top, right, bottom);
    return 0;
}

/* fsfont_GetGlyph (0x1004d0): binary search of the CCM block table (no null checks) */
const _FS_CCM_GLYPH_ST *fontlib_001004d0(unsigned int code)
{
    _FS_FONT_DATA_ST *fd = fsfont_pFontData;
    _FS_CCM_GLYPH_ST *glyph = fd->pCCMGlyph;
    _FS_CCM_BLOCK_ST *blk = fd->pCCMBlock;
    int hi = fd->pCCM->nBlock - 1, lo = 0;
    if (hi < 0) return 0;
    unsigned int c = code & 0xffff;
    do {
        int mid = (lo + hi) / 2;
        _FS_CCM_BLOCK_ST *b = &blk[mid];
        if (b->high < c) lo = mid + 1;
        else if (c < b->low) hi = mid - 1;
        else return &glyph[b->iGlyph + (c - b->low)];
    } while (lo <= hi);
    return 0;
}

/* ------------------------------------------------------------------ vsprintf front end */

/* fsfont_Utf8ToUtf16 (0x1001e0): returns the bytes written incl. the terminator; -2 on a continuation lead byte (or
 * class >= 7), -3 on a 4-byte lead. The lead-nibble class table (rodata 0x255880) is copied to the frame first. */
static const int fsfont_utf8_class[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 4};

int fontlib_001001e0(unsigned short *dst, const unsigned char *src)
{
    PS2_FRAME(0x40);
    PS2_STACK(int[16], cls, 0);
    for (int i = 0; i < 16; i++) cls[i] = fsfont_utf8_class[i];
    unsigned short *d = dst;
    if (*src) {
        do {
            unsigned int b = *src;
            int k = cls[(b >> 4) & 0xf];
            if (k == 3) {
                d[0] = (unsigned short)((b & 0xf) << 12);
                d[0] = (unsigned short)(d[0] + (((src[1] & 0x3f) << 6) & 0xffff));
                src += 2;
                d[0] = (unsigned short)(d[0] + (src[0] & 0x3f));
                src += 1;
            } else if (k == 2) {
                d[0] = (unsigned short)((b & 0x1f) << 6);
                d[0] = (unsigned short)(d[0] + (src[1] & 0x3f));
                src += 2;
            } else if (k == 0) {
                d[0] = (unsigned short)b;
                src += 1;
            } else {
                if (k == 1 || k >= 7) return -2;
                return -3;
            }
            d++;
        } while (*src);
    }
    int n = (int)((char *)d - (char *)dst);
    *d = 0;
    return (n / 2 + 1) << 1;
}

/* fsfont_Utf16ToUtf8 (0x100340): returns the bytes written incl. the terminator */
int fontlib_00100340(unsigned char *dst, const unsigned short *src)
{
    unsigned char *d = dst;
    while (*src) {
        unsigned int c = *src;
        if (c & 0xf800) {
            d[0] = (unsigned char)(((c >> 12) & 0xf) | 0xe0);
            d[1] = (unsigned char)(((*src >> 6) & 0x3f) | 0x80);
            d[2] = (unsigned char)((*src & 0x3f) | 0x80);
            d += 3;
        } else if (c & 0x780) {
            d[0] = (unsigned char)(((c >> 6) & 0x1f) | 0xc0);
            d[1] = (unsigned char)((*src & 0x3f) | 0x80);
            d += 2;
        } else {
            d[0] = (unsigned char)c;
            d += 1;
        }
        src++;
    }
    int n = (int)(d - dst);
    *d = 0;
    return n + 1;
}

/* fsfont_VsprintfWide (0x102300): UTF-16 fmt -> UTF-8 (sp+0x380), %s / %S / %c / %C arguments converted to UTF-8
 * behind it with the vararg slot overwritten by the new pointer, newlib vsprintf into sp+0x80, back to UTF-16.
 * Original quirks: only %c / %C get the letter rewritten to 's' (%S stays); %c / %C convert the UTF-16 chars stored
 * in the slot itself; %lc / %ls -> -0x31; no bound on either buffer. Errors -0x32 (conversion), -0x30 (vsprintf). */
int fontlib_00102300(unsigned short *out, const unsigned short *fmt, char *ap)
{
    PS2_FRAME(0x680);
    PS2_STACK(char[0x300], u8out, 0x80);
    PS2_STACK(char[0x300], fmt8, 0x380);
    char *slot = ap;      /* s5 */
    int inSpec = 0;       /* s1 */
    int len = 0;          /* s0: b0 'h', b1 'l' / 'L' */
    int n = fontlib_00100340((unsigned char *)fmt8, fmt);
    if (n < 0) return -0x32;
    char *conv = fmt8 + n;   /* s3: converted string arguments */
    char *p = fmt8;          /* s4 */
    if (*p) {
        do {
            signed char ch = (signed char)*p;
            if (ch == '%') {
                inSpec = !inSpec;
                len = 0;
            } else if (!inSpec) {
                len = 0;
            } else if (ch == 'h') {
                len = 1;
            } else if (ch == 'L' || ch == 'l') {
                len = 2;
            } else if (ch == 'G' || ch == 'g' || ch == 'f' || ch == 'E' || ch == 'e' || ch == 'X' || ch == 'x' ||
                       ch == 'o' || ch == 'i' || ch == 'd' || ch == 'u') {
                slot += 8;
                len = 0;
                inSpec = 0;
            } else if (ch == 'S' || ch == 's' || ch == 'C' || ch == 'c') {
                if (len & 2) return -0x31;
                slot += 8;
                char *s = slot - 8;
                const unsigned short *src;
                if (ch == 'S' || ch == 's') {
                    src = (const unsigned short *)HOSTPTR(*(unsigned int *)s);
                } else {
                    *p = 's';
                    src = (const unsigned short *)s;
                }
                n = fontlib_00100340((unsigned char *)conv, src);
                if (n < 0) return -0x32;
                *(unsigned int *)s = PS2PTR(conv);
                conv += n;
                len = 0;
                inSpec = 0;
            } else {
                len = 0;   /* other spec characters keep inSpec */
            }
            p++;
        } while (*p);
    }
    if (ps2_native_vsprintf(u8out, fmt8, ap) < 0) return -0x30;
    if (fontlib_001001e0(out, (const unsigned char *)u8out) < 0) return -0x32;
    return 0;
}

/* FsFontVsprintf (0x1025e0): wide fonts (ccm->charCode != 0) through 0x102300 (0 / negative), byte fonts straight
 * into newlib vsprintf (its count, or -0x30) */
int fontlib_001025e0(char *buf, const char *fmt, void *ap)
{
    PS2_FRAME(0x40);
    int c = fontlib_001005e0();
    if (c < 0) return c;
    if (c) return fontlib_00102300((unsigned short *)buf, (const unsigned short *)fmt, (char *)ap);
    int r = ps2_native_vsprintf(buf, fmt, (char *)ap);
    if (r < 0) r = -0x30;
    return r;
}

/* ------------------------------------------------------------------ measure / draw */

/* '#' tag dispatch (inline in 0x103b80 / 0x103ea0): first entry with this code and a function; -0x21 = unknown */
static int fsfont_call_tag(unsigned char **pcur, unsigned int op, int draw)
{
    _FS_FONT_TAG_MNG_ST *t = fsfont_pTagMng;
    unsigned int n = t->nListedTag;
    _FS_FONT_TAG_INFO_ST *e = t->tagList;
    for (unsigned int i = 0; i < n; i++, e++)
        if (op == e->code && e->analyzeTagFunc) return gcall(e->analyzeTagFunc, pcur, draw);
    return -0x21;
}

/* fsfont_Measure (0x103ea0): width of the widest line, total height, per-line widths (no bound) of str up to endCode
 * [+ endCode2] or 0; returns the bytes consumed or the last error (-3 missing glyph, -8 rect overflow, tag errors).
 * Original quirks kept: the letter spacing of the last glyph (f21, callee-saved) is subtracted at each line end, so a
 * text that starts with the end code / a newline subtracts the CALLER's stale f21 (PS2_STALE_FPR; garage3 text always
 * has a glyph first); an unknown '#' tag leaves -0x21 in the error slot, so the measure returns -0x21 unless a later
 * tag succeeds; the rect wrap (unreached) neither counts the wrapping glyph nor resets the line width. */
int fontlib_00103ea0(float *maxWidth, float *totalHeight, float *lineWidths, unsigned char *str,
                     unsigned int endCode, unsigned int endCode2, const _FS_FONT_RECT_ST *rect)
{
    PS2_FRAME(0xd0);
    PS2_STACK(unsigned char *, str0, 0xc0);
    PS2_STACK(float *, pth, 0xc4);
    PS2_STACK(float, lh, 0xc8);
    PS2_STACK(unsigned char *, cur, 0xcc);
    int err = 0;            /* s0 */
    pth = totalHeight;
    float w = 0.0f;         /* f22: current line */
    str0 = str;
    _FS_FONT_FORM_ST *S = fontlib_00102860();
    _FS_FONT_DEFTAG_MNG_ST *D = fsfont_pDefTagMng;
    float italic = (float)D->flag.isItalic;
    float skew = S->scaleX * (D->italic * italic);   /* f20 */
    float maxw = 0.0f;      /* f24 */
    cur = str0;
    fontlib_001028b0(&lh);
    float th = lh;          /* f23 */
    unsigned int e1 = endCode & 0xffff, e2 = endCode2 & 0xffff;
    float space = PS2_STALE_FPR(21);   /* f21: uninitialised until the first glyph (see above) */
    for (;;) {
        unsigned int c = fontlib_00102680(&cur) & 0xffff;
        unsigned int nx = fontlib_00102670(cur) & 0xffff;
        if ((c == e1 && (endCode2 == 0 || nx == e2)) || c == 0) {
            w = w - (space + skew);
            if (!(w <= maxw)) maxw = w;
            if (lineWidths) *lineWidths = w;
            if (maxWidth) *maxWidth = maxw;
            if (pth) *pth = th;
            if (err < 0) return err;
            return (int)(PS2PTR(cur) - PS2PTR(str0));
        }
        if (c == 0x23) {   /* '#' */
            if (nx == 0x23) {
                fontlib_00102680(&cur);   /* "##" = a literal '#' */
            } else {
                unsigned int op = fontlib_00102680(&cur) & 0xffff;
                int r = fsfont_call_tag(&cur, op, 0);
                err = r;
                if (r < 0 && r != -0x21) return r;
                if (r > 0) {
                    space = 0.0f;
                    w = w + (float)r;
                }
                continue;
            }
        } else if (c == 0xa) {
            w = w - (space + skew);
            if (!(w <= maxw)) maxw = w;
            if (lineWidths) *lineWidths++ = w;
            th = th + lh;
            if (rect && !(rect->top + th <= rect->bottom)) return -8;
            w = 0.0f;
            continue;
        }
        const _FS_CCM_GLYPH_ST *g = fontlib_001004d0(c);
        if (!g) {
            g = fontlib_001004d0(fontlib_00102880());
            if (!g) {
                err = -3;
                continue;
            }
        }
        D = fsfont_pDefTagMng;
        unsigned int ruby = D->flag.ruby;
        float sc = ruby == 2 ? D->rubyForm.scaleX : S->scaleX;
        int adv = g->offset + g->advance;
        int wid = g->width;
        float px;
        if (adv < wid) px = (float)D->flag.isBold + (float)wid * sc;
        else px = (float)D->flag.isBold + sc * (float)adv;
        if (rect && !S->alignX) {
            if (!(px + (rect->left + w) <= rect->right)) {   /* wrap (unreached) */
                w = w - (space + skew);
                if (!(w <= maxw)) maxw = w;
                if (lineWidths) *lineWidths++ = w;
                th = th + lh;
                if (!(rect->top + th <= rect->bottom)) err = -8;
                continue;
            }
        }
        space = ruby == 2 ? D->rubyForm.spaceX : S->spaceX;
        w = w + (px + space);
    }
}

/* fsfont_DrawGlyph (0x102690): one glyph at the current pos (wrap to the next line unless the form's alignX bits are
 * set: -8), handed to drawFunc in a _FS_FONT_DRAW_ST at sp+0x40 (PS2_STACK: drawFunc 0x100b40 is interpreted until
 * F2), then the pos advances. The dummy-code fallback reads drawState.dummyCode inline (not via 0x102880). */
int fontlib_00102690(unsigned int code, const _FS_FONT_FORM_ST *form)
{
    PS2_FRAME(0x70);
    PS2_STACK(_FS_FONT_DRAW_ST, draw, 0x40);
    int r = 0;
    const _FS_CCM_GLYPH_ST *g = fontlib_001004d0(code);
    if (!g) {
        g = fontlib_001004d0(fsfont_pDrawState->dummyCode);
        if (!g) return -3;
    }
    _FS_FONT_DRAW_STATE_ST *t = fsfont_pDrawState;
    float gw = (float)g->width * form->scaleX;
    if (!(t->x + gw <= t->rect.right)) {
        if (form->alignX) return -8;
        t->x = t->rect.left;
        t = fsfont_pDrawState;
        t->y = t->y + t->lineHeight;
        t = fsfont_pDrawState;
        if (!(t->y + t->lineHeight <= t->rect.bottom)) r = -8;
    }
    t = fsfont_pDrawState;
    draw.texInfo.texID = g->texID;
    draw.texInfo.uv[0] = g->left;
    draw.texInfo.uv[1] = g->top;
    draw.texInfo.uv[2] = g->right;
    draw.texInfo.uv[3] = g->bottom;
    draw.pos[0] = t->x - 0.0625f;
    draw.pos[1] = t->y - 0.0625f;
    draw.pos[2] = (t->x - 0.0625f) + (float)g->width * form->scaleX;
    draw.color = &t->color[0];
    draw.pos[3] = (t->y - 0.0625f) + form->totalHeight;
    gcall(t->drawFunc, &draw);
    t = fsfont_pDrawState;
    t->x = form->spaceX + (t->x + form->scaleX * (float)(g->offset + g->advance));
    return r;
}

/* fsfont_DrawChar (0x103a20): ruby mode 2 draws with the ruby form; otherwise drop by the ruby line height, draw with
 * the std form and come back up. Bold = a second draw 1 px to the right. Ends with a relative move (0, f20). */
int fontlib_00103a20(unsigned int code)
{
    PS2_FRAME(0x50);
    PS2_STACK(float, x, 0x4c);
    PS2_STACK(float, y, 0x48);
    float back = 0.0f;   /* f20 */
    int r;
    fontlib_00102990(&x, &y);
    _FS_FONT_DEFTAG_MNG_ST *d = fsfont_pDefTagMng;
    if (d->flag.ruby == 2) {
        r = fontlib_00102690(code, &d->rubyForm);
        if (fsfont_pDefTagMng->flag.isBold) {
            fontlib_00102c60(6, 1.0f + x, y);
            r = fontlib_00102690(code, &fsfont_pDefTagMng->rubyForm);
        }
    } else {
        fontlib_00102c60(4, back, y + d->rubyForm.totalHeight);
        r = fontlib_00102690(code, fontlib_00102860());
        d = fsfont_pDefTagMng;
        float rh = d->rubyForm.totalHeight;
        back = -rh;
        if (d->flag.isBold) {
            fontlib_00102c60(6, 1.0f + x, y + rh);
            r = fontlib_00102690(code, fontlib_00102860());
        }
    }
    fontlib_00102c60(7, 0.0f, back);
    return r;
}

/* fsfont_DrawLines (0x103b80): draws str line by line inside drawState.rect (x per line from lineWidths when
 * alignX != 0; the line index only advances on that path), ']' closes a ruby (mode 2 -> 1, x back to savePosX), '#'
 * tags run with draw = 1, '\r' is skipped. Returns 0 at the terminator, -8 when the next line leaves the rect, or a
 * draw / tag error (-3 / -8 from a glyph and -0x21 unknown tag are not errors). */
int fontlib_00103b80(unsigned char *str, const float *lineWidths)
{
    PS2_FRAME(0x80);
    PS2_STACK(float, x, 0x74);
    PS2_STACK(float, y, 0x70);
    PS2_STACK(float, lh, 0x6c);
    PS2_STACK(unsigned char, ax, 0x7f);
    PS2_STACK(unsigned char *, cur, 0x78);
    fontlib_00102990(&x, &y);
    fontlib_001028b0(&lh);
    fontlib_00102910(&ax, 0);
    cur = str;
    int line = 0;   /* s0 */
    _FS_FONT_RECT_ST *rc = fontlib_00102870();
    float nx = rc->left;
    float w = rc->right - rc->left;   /* f20 */
    if (ax) nx = nx + ps2_div(w - lineWidths[0], (float)ax);
    x = nx;
    fontlib_00102c60(2, nx, 0.0f);
    for (;;) {
        unsigned int c = fontlib_00102680(&cur) & 0xffff;
        if (c == 0) return 0;
        if (c == 0x5d) {   /* ']' */
            _FS_FONT_DEFTAG_MNG_ST *d = fsfont_pDefTagMng;
            if (d->flag.ruby == 2) {
                d->flag.ruby = 1;
                fontlib_00102c60(2, fsfont_pDefTagMng->savePosX, 0.0f);
                continue;
            }
        } else if (c == 0x23) {   /* '#' */
            if ((fontlib_00102670(cur) & 0xffff) == 0x23) {
                fontlib_00102680(&cur);   /* "##": draw a '#' */
            } else {
                unsigned int op = fontlib_00102680(&cur) & 0xffff;
                int r = fsfont_call_tag(&cur, op, 1);
                if (r >= 0 || r == -0x21) continue;
                return r;
            }
        } else if (c == 0xa) {
            if (ax) {
                line++;
                float q = ps2_div(w - lineWidths[line], (float)ax);
                nx = rc->left + q;
            } else {
                nx = rc->left;
            }
            x = nx;
            float ny = y + lh;
            y = ny;
            if (!(ny + lh <= rc->bottom)) return -8;
            fontlib_00102c60(6, nx, ny);
            continue;
        } else if (c == 0xd) {
            continue;
        }
        int r = fontlib_00103a20(c);
        if (r >= 0 || r == -3 || r == -8) {
            fontlib_00102990(&x, &y);
            continue;
        }
        return r;
    }
}

/* FsFontDraw (0x1032a0): line height = std + ruby total height, measure, then either (fitRect) shift y inside the
 * current rect by alignY or (game: fitRect 0) centre on the pos by align 2 and set pos + rect to the text box; draw.
 * The measure's result is ignored (w / h / lineWidths stay stale after an error: PS2_STACK). Quirk (fitRect, alignY
 * 0, unreached): y += rect.left (offset 0) instead of rect.top. */
int fontlib_001032a0(int fitRect, char *str)
{
    PS2_FRAME(0xd0);
    PS2_STACK(float[32], lw, 0x30);
    PS2_STACK(float, th, 0xb8);
    PS2_STACK(float, y, 0xbc);
    PS2_STACK(float, x, 0xc0);
    PS2_STACK(float, h, 0xc4);
    PS2_STACK(float, w, 0xc8);
    PS2_STACK(unsigned char, ay, 0xce);
    PS2_STACK(unsigned char, ax, 0xcf);
    unsigned char *s = (unsigned char *)str;
    fontlib_00102990(&x, &y);
    fontlib_00102910(&ax, &ay);
    fontlib_00102890(&th);
    fontlib_00102a00(th + fsfont_pDefTagMng->rubyForm.totalHeight);
    if (fitRect) {
        if (ax || ay) {
            _FS_FONT_RECT_ST *rc = fontlib_00102870();
            fontlib_00103ea0(&w, &h, lw, s, 0, 0, rc);
            float dy;
            if (ay) dy = ps2_div((rc->bottom - rc->top) - h, (float)ay);
            else dy = rc->left;
            float ny = y + dy;
            y = ny;
            fontlib_00102c60(4, 0.0f, ny);
        }
    } else {
        fontlib_00103ea0(&w, &h, lw, s, 0, 0, 0);
        float l = x, t = y;
        if (ax == 2) l = l - ps2_div(w, (float)ax);
        if (ay == 2) t = t - ps2_div(h, (float)ay);
        fontlib_00102c20(l, t, l + w, t + h);
    }
    return fontlib_00103b80(s, lw);
}

/* ================================================================== F2: glyph emit + texture upload */

/* MWCC u32 -> float (cvt.s.w, halved + doubled when bit 31 is set) and float -> u32 (2^31 compare) */
static float fsfont_u2f(unsigned int v)
{
    if ((int)v >= 0) return (float)(int)v;
    float f = (float)(int)((v >> 1) | (v & 1));
    return f + f;
}

static unsigned int fsfont_f2u(float f)
{
    if (2147483648.0f <= f) return (unsigned int)ps2_ftoi(f - 2147483648.0f) | 0x80000000u;
    return (unsigned int)ps2_ftoi(f);
}

/* DMA CNT tag {0x10000000 | qwc, qwc, 0} at t; returns the qword after it. The original skips the stores when the
 * cursor is NULL and goes on writing at address 0 (unreachable: every caller checked pDmaCurrent first). */
static unsigned char *fsfont_put_cnt(unsigned char *t, unsigned int qwc)
{
    if (!t) return 0;
    unsigned int *w = (unsigned int *)t;
    w[0] = (qwc & 0xffff) | 0x10000000u;
    w[1] = qwc;
    *(unsigned long long *)(t + 8) = 0;
    return t + 16;
}

/* VIF1 DIRECTHL n qword {0, 0, 0, 0x51000000 | n} (the !bfMode path; the game sets bfMode, unreached) */
static unsigned char *fsfont_put_vif(unsigned char *t, unsigned int n)
{
    unsigned int *w = (unsigned int *)t;
    w[0] = 0;
    w[1] = 0;
    w[2] = 0;
    w[3] = 0x51000000u | n;
    return t + 16;
}

/* fsfont_GetTex (0x100610): no bounds check */
unsigned char *fontlib_00100610(int slot) { return fsfont_pFontData->ppTexBuff[slot]; }

/* fsfont_ReserveQwords (0x101f80): 0 when n qwords fit (unsigned room: an overrun wraps and passes), else with
 * bfDmaOverDraw the last tag becomes END and the chain is kicked + reopened (0), -1 without it; on the VIF path only
 * bfUseSPR flushes. */
int fontlib_00101f80(unsigned int n)
{
    PS2_FRAME(0x10);
    struct_c5a57ef2 *P = fsfont_pPs2Info;
    unsigned int buf = PS2PTR(P->pDmaBuff), cur = PS2PTR(P->pDmaCurrent);
    unsigned int room = P->uiDmaBuffSize - (cur - buf);
    if (!(room < n * 16)) return 0;
    if (P->bfMode == 1) {
        if (!P->bfDmaOverDraw) return -1;
        if (buf && cur) {
            unsigned int *t = (unsigned int *)P->pDmaLatest;
            if (t) {
                t[0] &= 0x8fffffffu;
                if (!fsfont_pPs2Info->bfMode) t[0] |= 0x60000000u;   /* bfMode re-read: always 1 here */
                else t[0] |= 0x70000000u;
                t[1] = 0;
            }
        }
        fontlib_00101d70();
        return 0;
    }
    if (P->bfUseSPR) {
        fontlib_00101d70();
        return 0;
    }
    return -1;
}

/* fsfont_UploadImage (0x100e20): one image upload per chunk of up to 0x7fff0 bytes: CNT {GIF A+D x4: BITBLTBUF,
 * TRXPOS 0, TRXREG (w, rows), TRXDIR 0; GIF IMAGE tag}, REF to the image bytes in guest memory (src as is), CNT {GIF
 * A+D TEXFLUSH}; no kick (FsFontEnd / ReserveQwords do that). Both GIF tags are built by MWCC bitfield RMWs on the
 * buffer's old words, but every bit is rewritten (map F2), so the constants are exact. Original quirk kept:
 * tbp0 += chunk >> 7 per chunk (128-byte steps; GS blocks are 256 bytes - only a > 0x7fff0-byte image would see it).
 * Early outs (no buffer, src NULL, unknown psm) leave a garbage v0 in the original: 0 here (unreached). */
int fontlib_00100e20(const void *src, unsigned int tbp0, int tbw, int psm, int w, int h)
{
    PS2_FRAME(0x100);
    unsigned int nA = 6, nB = 2;   /* s6 / s5: qwc of the two CNT packets */
    const unsigned char *s = (const unsigned char *)src;
    struct_c5a57ef2 *P = fsfont_pPs2Info;
    unsigned int wh = (unsigned int)w * (unsigned int)h;   /* mult: low word */
    if (!P->pDmaBuff || !P->pDmaCurrent || !s) return 0;
    float bpp;   /* f20: bytes per pixel */
    switch (psm) {
    case 0x24: case 0x2c: case 0x14: bpp = 0.5f; break;
    case 0x1b: case 0x13: bpp = 1.0f; break;
    case 2: bpp = 2.0f; break;
    case 1: bpp = 3.0f; break;
    case 0: bpp = 4.0f; break;
    default: return 0;
    }
    unsigned int total = fsfont_f2u(fsfont_u2f(wh) * bpp);
    if (!P->bfMode) {
        nA++;
        nB++;
    }
    unsigned long long bbHi = ((unsigned long long)(unsigned int)tbw << 48) | ((unsigned long long)(unsigned int)psm << 56);
    unsigned int need = nA + 3 + nB;
    while (total) {   /* v0 = 0x101f80's 0 at the end (0 too when total starts at 0) */
        unsigned int chunk = total < 0x7fff0u ? total : 0x7fff0u;
        unsigned int cols = (unsigned int)w ? chunk / (unsigned int)w : 0xffffffffu;   /* divu, no zero check */
        total -= chunk;
        unsigned int rows = fsfont_f2u(ps2_div(fsfont_u2f(cols), bpp));
        int r = fontlib_00101f80(need);
        if (r) return r;
        unsigned char *t = fsfont_put_cnt((unsigned char *)fsfont_pPs2Info->pDmaCurrent, nA);
        if (!fsfont_pPs2Info->bfMode) t = fsfont_put_vif(t, 6);
        unsigned long long *q = (unsigned long long *)t;
        q[0] = 0x1000000000000004ull;   /* NLOOP 4, packed, NREG 1 */
        q[1] = 0xe;                     /* A+D */
        q[2] = ((unsigned long long)tbp0 << 32) | bbHi;
        q[3] = 0x50;   /* BITBLTBUF */
        q[4] = 0;
        q[5] = 0x51;   /* TRXPOS */
        q[6] = (unsigned long long)(unsigned int)w | ((unsigned long long)rows << 32);
        q[7] = 0x52;   /* TRXREG */
        q[8] = 0;
        q[9] = 0x53;   /* TRXDIR */
        q[10] = 0x0800000000008000ull | ((chunk >> 4) & 0x7fff);   /* IMAGE, EOP */
        q[11] = 0;
        unsigned int *ref = (unsigned int *)(q + 12);
        ref[0] = 0x30000000u | ((chunk >> 4) & 0xffff);
        ref[1] = PS2_DMA_ADDR(s, PS2PTR(s));
        q[13] = 0;
        unsigned char *c2 = (unsigned char *)(q + 14);
        fsfont_pPs2Info->pDmaLatest = c2;
        t = fsfont_put_cnt(c2, nB);
        if (!fsfont_pPs2Info->bfMode) t = fsfont_put_vif(t, 2);
        q = (unsigned long long *)t;
        q[0] = 0x1000000000008001ull;   /* NLOOP 1, EOP, NREG 1 */
        q[1] = 0xe;
        q[2] = 0;
        q[3] = 0x3f;   /* TEXFLUSH */
        tbp0 += (chunk >> 1) >> 6;
        s += chunk;
        fsfont_pPs2Info->pDmaCurrent = q + 4;
        struct_c5a57ef2 *p = fsfont_pPs2Info;
        p->usEntryStrNum = (unsigned short)(p->usEntryStrNum + 1);
    }
    return 0;
}

/* fsfont_UploadTex (0x101550): the page image (tex + 0x20, TEX0 at tex + 0x10) and its CLUT (image + size word at
 * tex + 4; 8 x 2 for 4-bit, 16 x 16 for 8-bit pages). Returns the last upload's result; tex NULL returns the
 * caller's v0 (= tex = 0). */
int fontlib_00101550(const unsigned char *tex)
{
    PS2_FRAME(0x40);
    if (!tex) return 0;
    const unsigned char *img = tex + 0x20;
    unsigned long long tex0 = *(const unsigned long long *)(tex + 0x10);
    unsigned int tbp0 = (unsigned int)tex0 & 0x3fff;
    int tbw = (int)((tex0 >> 14) & 0x3f);
    int psm = (int)((tex0 >> 20) & 0x3f);
    int tw = 1 << ((tex0 >> 26) & 0xf);
    int th = 1 << ((tex0 >> 30) & 0xf);
    int r = fontlib_00100e20(img, tbp0, tbw, psm, tw, th);
    tex0 = *(const unsigned long long *)(tex + 0x10);
    psm = (int)((tex0 >> 20) & 0x3f);
    const unsigned char *clut = img + *(const unsigned int *)(tex + 4);
    unsigned int cbp = (unsigned int)((tex0 >> 37) & 0x3fff);
    int cpsm = (int)((tex0 >> 51) & 0xf);
    switch (psm) {
    case 0x24: case 0x2c: case 0x14: r = fontlib_00100e20(clut, cbp, 1, cpsm, 8, 2); break;
    case 0x1b: case 0x13: r = fontlib_00100e20(clut, cbp, 1, cpsm, 0x10, 0x10); break;
    default: break;
    }
    return r;
}

/* fsfont_PutTexState (0x101b90): CNT + GIF A+D x3: TEXFLUSH, TEX1, TEX0 of the page (context by bfConText). Patches
 * the page header in guest memory first: TEX0.CLD (byte tex + 0x17 bits 5-7) = 2, every call. Early out returns the
 * tex pointer (v0 of 0x100610). usStrNum / usEntryStrNum are not counted. */
int fontlib_00101b90(int texId)
{
    PS2_FRAME(0x40);
    unsigned int n = 4;
    unsigned char *tex = fontlib_00100610(texId);
    struct_c5a57ef2 *P = fsfont_pPs2Info;
    if (!P->pDmaBuff || !P->pDmaCurrent || !tex) return (int)PS2PTR(tex);
    n += P->bfMode == 0;
    int r = fontlib_00101f80(n + 1);
    if (r) return r;
    P = fsfont_pPs2Info;
    unsigned char *C = (unsigned char *)P->pDmaCurrent;
    P->pDmaLatest = C;
    unsigned char *g = fsfont_put_cnt(C, n);
    P = fsfont_pPs2Info;
    if (!P->bfMode) g = fsfont_put_vif(g, 4);
    unsigned long long *t = (unsigned long long *)g;
    t[0] = 0x3000000000008001ull;   /* NLOOP 1, EOP, NREG 3 (both bfConText arms) */
    t[1] = 0xeee;
    t[2] = 0;
    t[3] = 0x3f;   /* TEXFLUSH */
    t[4] = *(unsigned long long *)(tex + 0x18);
    t[5] = P->bfConText ? 0x15 : 0x14;   /* TEX1_2 / _1 */
    tex[0x17] = (unsigned char)((tex[0x17] & 0x1f) | 0x40);
    P = fsfont_pPs2Info;
    t[6] = *(unsigned long long *)(tex + 0x10);
    t[7] = P->bfConText ? 7 : 6;         /* TEX0_2 / _1 */
    P->pDmaCurrent = g + 0x40;
    return 0;
}

/* glyph quad coordinates (0x101690): X = (1728 + x) * 16, Y = (2048 - y') * 16 with y' = 224 - y (halved under
 * bfFfmd, re-read per vertex), both truncated (cvt.w.s) to the low 16 bits */
static int fsfont_glyph_x(float x) { return ps2_ftoi((1728.0f + x) * 16.0f); }

static int fsfont_glyph_y(float p)
{
    float y = 224.0f - p;
    if (fsfont_pPs2Info->bfFfmd == 1) y = ps2_div(y, 2.0f);
    return ps2_ftoi((2048.0f - y) * 16.0f);
}

/* one REGLIST vertex: ST {s, t}, RGBAQ {rgba, 1.0f}, XYZ2 {X, Y, Z 0x01000000} */
static unsigned char *fsfont_put_vtx(unsigned char *d, float s, float t, unsigned int rgba, int X, int Y)
{
    float *f = (float *)d;
    f[0] = s;
    f[1] = t;
    unsigned int *w = (unsigned int *)(d + 8);
    w[0] = rgba;
    w[1] = 0x3f800000u;
    unsigned short *xy = (unsigned short *)(d + 16);
    xy[0] = (unsigned short)X;
    xy[1] = (unsigned short)Y;
    *(unsigned int *)(d + 20) = 0x01000000u;
    return d + 24;
}

/* fsfont_PutGlyph (0x101690): one textured sprite as a 4-vertex strip: CNT + GIF {PRIM 0x5c | ctx << 9 via PRE, one
 * NOP register qword} + GIF REGLIST (ST RGBAQ XYZ2 x4). Vertex order top-left, top-right, bottom-left,
 * bottom-right; skew (italic) only on the top two: ((x + skew) + 1728) * 16. Counts usStrNum and usEntryStrNum.
 * Early out returns 0x100610's v0 (the tex pointer). */
int fontlib_00101690(int texId, const float *pos, const unsigned int *color, const float *uv, float skew)
{
    PS2_FRAME(0x80);
    unsigned int q = 9;
    unsigned char *tex = fontlib_00100610(texId);
    struct_c5a57ef2 *P = fsfont_pPs2Info;
    if (!P->pDmaBuff || !P->pDmaCurrent || !tex) return (int)PS2PTR(tex);
    if (!P->bfMode) q += 1;
    int r = fontlib_00101f80(q + 1);
    if (r) return r;
    P = fsfont_pPs2Info;
    unsigned char *C = (unsigned char *)P->pDmaCurrent;
    P->pDmaLatest = C;
    unsigned char *g = fsfont_put_cnt(C, q);
    P = fsfont_pPs2Info;
    if (!P->bfMode) g = fsfont_put_vif(g, 9);
    unsigned long long prim = 0x5cull | ((unsigned long long)P->bfConText << 9);
    unsigned long long *t = (unsigned long long *)g;
    t[0] = (prim << 47) | 0x1000400000000001ull;   /* NLOOP 1, PRE, NREG 1 */
    t[1] = 0xf;                                    /* NOP */
    t[2] = 0;
    t[3] = 0;
    t[4] = 0xc400000000008001ull;   /* NLOOP 1, EOP, REGLIST, NREG 12 */
    t[5] = 0x0000512512512512ull;
    unsigned char *d = g + 0x30;
    float x0 = pos[0] + skew, x1 = pos[2] + skew;
    d = fsfont_put_vtx(d, uv[0], uv[1], color[0], fsfont_glyph_x(x0), fsfont_glyph_y(pos[1]));
    d = fsfont_put_vtx(d, uv[2], uv[1], color[1], fsfont_glyph_x(x1), fsfont_glyph_y(pos[1]));
    d = fsfont_put_vtx(d, uv[0], uv[3], color[2], fsfont_glyph_x(pos[0]), fsfont_glyph_y(pos[3]));
    d = fsfont_put_vtx(d, uv[2], uv[3], color[3], fsfont_glyph_x(pos[2]), fsfont_glyph_y(pos[3]));
    fsfont_pPs2Info->pDmaCurrent = d;
    P = fsfont_pPs2Info;
    P->usStrNum = (unsigned short)(P->usStrNum + 1);
    P = fsfont_pPs2Info;
    P->usEntryStrNum = (unsigned short)(P->usEntryStrNum + 1);
    return 0;
}

/* fsfont_GetItalicSkew (0x103580): scaleX (ruby form in ruby mode 2, else std) * (italic * isItalic) */
int fontlib_00103580(float *out)
{
    PS2_FRAME(0x30);
    PS2_STACK(float, sx, 0x2c);
    _FS_FONT_DEFTAG_MNG_ST *D = fsfont_pDefTagMng;
    if (D->flag.ruby == 2) sx = D->rubyForm.scaleX;
    else fontlib_00102950(&sx, 0);
    if (out) {
        D = fsfont_pDefTagMng;
        float it = (float)D->flag.isItalic;
        *out = sx * (D->italic * it);
    }
    return 0;
}

/* fsfont_DrawFunc (0x100b40, stored as drawState.drawFunc by FsFontInit): page change -> upload (bfTransTex) + TEX
 * state packet, then the glyph quad. The page compare is u8 ucBfTransTexId vs the s16 texID (an id >= 0x80 never
 * matches). Returns 0 in v0 (bw wrapper). */
void fontlib_00100b40(_FS_FONT_DRAW_ST *d)
{
    PS2_FRAME(0x60);
    PS2_STACK(float, skew, 0x5c);
    fontlib_00103580(&skew);
    struct_c5a57ef2 *P = fsfont_pPs2Info;
    unsigned int *color = d->color;
    int id = d->texInfo.texID;
    float f20 = skew;
    if (!P->pDmaBuff || !P->pDmaCurrent) return;
    unsigned char *tex = fontlib_00100610(id);
    if ((int)fsfont_pPs2Info->ucBfTransTexId != id) {
        if (fsfont_pPs2Info->bfTransTex == 1) fontlib_00101550(tex);
        fsfont_pPs2Info->ucBfTransTexId = (unsigned char)id;
        fontlib_00101b90(id);
    }
    fontlib_00101690(id, &d->pos[0], color, &d->texInfo.uv[0], f20);
}

/* ================================================================== F3: measure entry + '#' tag ops (unreached) */

/* FsFontCalcSize (0x1034d0): line height = std + ruby total height, then the measure pass with the same args */
int fontlib_001034d0(float *maxWidth, float *totalHeight, float *lineWidths, unsigned char *str,
                     unsigned int endCode, unsigned int endCode2, const _FS_FONT_RECT_ST *rect)
{
    PS2_FRAME(0x90);
    PS2_STACK(float, th, 0x8c);
    fontlib_00102890(&th);
    fontlib_00102a00(th + fsfont_pDefTagMng->rubyForm.totalHeight);
    return fontlib_00103ea0(maxWidth, totalHeight, lineWidths, str, endCode, endCode2, rect);
}

/* fsfont_GetColor (0x1028d0) */
int fontlib_001028d0(unsigned int *out)
{
    if (out) {
        out[0] = fsfont_pDrawState->color[0];
        out[1] = fsfont_pDrawState->color[1];
        out[2] = fsfont_pDrawState->color[2];
        out[3] = fsfont_pDrawState->color[3];
    }
    return 0;
}

/* fsfont_GetCharClass (0x1029d0): charclass[ch & 0xffff] - the original indexes the 256-byte table with the full
 * 16-bit code, so a wide char reads whatever guest memory follows it (hybrid: same bytes). The source port has no
 * memory behind the table: out-of-table codes read 0x10 (not a hex digit, what the tag parsers test). */
int fontlib_001029d0(unsigned int ch, unsigned char *out)
{
    if (out) {
        unsigned int i = ch & 0xffff;
#ifdef ACNB_SOURCEPORT
        *out = i < 0x100 ? fsfont_charclass[i] : (unsigned char)0x10;
#else
        *out = ((const unsigned char *)fsfont_charclass)[i];
#endif
    }
    return 0;
}

/* fsfont_SetColorMasked (0x102a10): color[i] = (color[i] & ~mask) + (src[i] & mask) */
int fontlib_00102a10(const unsigned int *src, unsigned int mask)
{
    for (int i = 0; i < 4; i++) {
        _FS_FONT_DRAW_STATE_ST *t = fsfont_pDrawState;
        t->color[i] = (t->color[i] & ~mask) + (src[i] & mask);
    }
    return 0;
}

/* fsfont_SetColorAll (0x102a90): color[i] = (color[i] & ~mask) + (v & mask) */
int fontlib_00102a90(unsigned int v, unsigned int mask)
{
    unsigned int m = v & mask;
    for (int i = 0; i < 4; i++) {
        _FS_FONT_DRAW_STATE_ST *t = fsfont_pDrawState;
        t->color[i] = (t->color[i] & ~mask) + m;
    }
    return 0;
}

/* fsfont_SetColor4 (0x102af0) */
int fontlib_00102af0(const unsigned int *src)
{
    for (int i = 0; i < 4; i++) fsfont_pDrawState->color[i] = src[i];
    return 0;
}

/* skip to just past the next ']' (or the terminator) through charNextFP; slot = the original's cursor stack slot */
static void fsfont_tag_skip(unsigned char **pp, unsigned char **slot)
{
    *slot = *pp;
    unsigned int c;
    do c = fontlib_00102680(slot) & 0xffff;
    while (c != 0 && c != 0x5d);
    *pp = *slot;
}

/* two hex digits through charNextFP + charclass; -6 on a non-digit (class bit 0x10), else *val = the byte */
static int fsfont_tag_hex2(unsigned char **cur, unsigned char *cls, unsigned int *val)
{
    unsigned int v = 0;
    for (int n = 0; n < 2; n++) {
        fontlib_001029d0(fontlib_00102680(cur) & 0xffff, cls);
        if (*cls & 0x10) return -6;
        v = (v << 4) + (*cls & 0xf);
    }
    *val = v;
    return 0;
}

/* tag 'i' (0x104370, pointer-only): toggles italic */
int fontlib_00104370(unsigned char **s, int draw)
{
    (void)s;
    (void)draw;
    _FS_FONT_DEFTAG_MNG_ST *D = fsfont_pDefTagMng;
    D->flag.isItalic = !D->flag.isItalic;
    return 0;
}

/* tag 'b' (0x1043b0, pointer-only): toggles bold */
int fontlib_001043b0(unsigned char **s, int draw)
{
    (void)s;
    (void)draw;
    _FS_FONT_DEFTAG_MNG_ST *D = fsfont_pDefTagMng;
    D->flag.isBold = !D->flag.isBold;
    return 0;
}

/* tag 'r' (0x1043f0): `#r[ruby]base#r`. Second #r closes (ruby = 0). Measure pass: skip "[ruby]", ruby = 1. Draw
 * pass: skip the delimiter, ruby = 2, savePosX = x, measure the ruby text (to ']') and, with ruby 0 and the flags /
 * colours saved around it, the base text (to "#r"); with a ruby align the pos moves to savePosX + (base - ruby) / 2.
 * Original quirk kept: an error from either measure returns at once, leaving ruby at 2 / 0 and nothing restored. */
int fontlib_001043f0(unsigned char **pp, int draw)
{
    PS2_FRAME(0x50);
    PS2_STACK(unsigned char *, cur, 0x40);
    PS2_STACK(unsigned char[4], fl, 0x44);
    PS2_STACK(unsigned int[4], col, 0x30);
    PS2_STACK(float, baseW, 0x48);
    PS2_STACK(float, rubyW, 0x4c);
    _FS_FONT_DEFTAG_MNG_ST *D = fsfont_pDefTagMng;
    if (D->flag.ruby) {
        D->flag.ruby = 0;
        return 0;
    }
    if (draw != 1) {
        fsfont_tag_skip(pp, &cur);
        fsfont_pDefTagMng->flag.ruby = 1;
        return 0;
    }
    *pp += fontlib_001005e0() + 1;
    fsfont_pDefTagMng->flag.ruby = 2;
    fontlib_00102990(&fsfont_pDefTagMng->savePosX, 0);
    int r = fontlib_00103ea0(&rubyW, 0, 0, *pp, 0x5d, 0, 0);
    if (r < 0) return r;
    unsigned char *f = (unsigned char *)&fsfont_pDefTagMng->flag;
    fl[0] = f[0];
    fl[1] = f[1];
    fl[2] = f[2];
    fl[3] = f[3];
    fontlib_001028d0(col);
    fsfont_pDefTagMng->flag.ruby = 0;
    int r2 = fontlib_00103ea0(&baseW, 0, 0, *pp + r, 0x23, 0x72, 0);
    if (r2 < 0) return r2;
    f = (unsigned char *)&fsfont_pDefTagMng->flag;
    f[0] = fl[0];
    f[1] = fl[1];
    f[2] = fl[2];
    f[3] = fl[3];
    fontlib_00102af0(col);
    D = fsfont_pDefTagMng;
    if (D->rubyForm.alignX) fontlib_00102c60(2, D->savePosX + ps2_div(baseW - rubyW, 2.0f), 0.0f);
    return 0;
}

/* tag 'a' (0x104630): `#a[xx]...#a`, alpha byte xx (hex). Draw-pass open: alpha = xx in every colour (mask
 * ~colorMask); draw-pass close: alpha back from saveColor. Original quirks kept: the open does not save the colours
 * (only #c's open writes saveColor); the draw-pass open scans from the first digit; a bad digit returns -6 untoggled. */
int fontlib_00104630(unsigned char **pp, int draw)
{
    PS2_FRAME(0x60);
    PS2_STACK(unsigned char *, scan, 0x4c);
    PS2_STACK(unsigned char *, cur, 0x50);
    PS2_STACK(unsigned int, col, 0x54);
    PS2_STACK(unsigned int, val, 0x58);
    PS2_STACK(unsigned char, cls, 0x5f);
    _FS_FONT_DEFTAG_MNG_ST *D = fsfont_pDefTagMng;
    if (D->flag.isAlpha) {
        if (draw == 1) fontlib_00102a10(D->saveColor, ~D->colorMask);
    } else {
        if (draw == 1) {
            *pp += fontlib_001005e0() + 1;
            cur = *pp;
            if (fsfont_tag_hex2(&cur, &cls, &val) < 0) return -6;
            col = 0;
            ((unsigned char *)&col)[3] = (unsigned char)val;
            fontlib_00102a90(col, ~fsfont_pDefTagMng->colorMask);
        }
        fsfont_tag_skip(pp, &scan);
    }
    D = fsfont_pDefTagMng;
    D->flag.isAlpha = !D->flag.isAlpha;
    return 0;
}

/* tag 'c' (0x1047d0): `#c[RRGGBB]...#c`. Draw-pass open: saveColor = colours, then RGB = the value (colorMask);
 * draw-pass close: colours back from saveColor (same mask). Each hex pair advances *pp by 2 * (char size); a bad digit
 * returns -6 untoggled with *pp already moved. */
int fontlib_001047d0(unsigned char **pp, int draw)
{
    PS2_FRAME(0x70);
    PS2_STACK(unsigned char *, scan, 0x54);
    PS2_STACK(unsigned char *, cur2, 0x58);
    PS2_STACK(unsigned char *, cur1, 0x5c);
    PS2_STACK(unsigned char *, cur0, 0x60);
    PS2_STACK(unsigned char[4], col, 0x64);
    PS2_STACK(unsigned int, val, 0x68);
    PS2_STACK(unsigned char, cls2, 0x6d);
    PS2_STACK(unsigned char, cls1, 0x6e);
    PS2_STACK(unsigned char, cls0, 0x6f);
    _FS_FONT_DEFTAG_MNG_ST *D = fsfont_pDefTagMng;
    if (D->flag.isColor) {
        if (draw == 1) fontlib_00102a10(D->saveColor, D->colorMask);
    } else {
        if (draw == 1) {
            int step = fontlib_001005e0() + 1;
            *pp += step;
            cur0 = *pp;
            if (fsfont_tag_hex2(&cur0, &cls0, &val) < 0) return -6;
            int pair = step << 1;
            col[0] = (unsigned char)val;
            *pp += pair;
            cur1 = *pp;
            if (fsfont_tag_hex2(&cur1, &cls1, &val) < 0) return -6;
            col[1] = (unsigned char)val;
            *pp += pair;
            cur2 = *pp;
            if (fsfont_tag_hex2(&cur2, &cls2, &val) < 0) return -6;
            col[2] = (unsigned char)val;
            *pp += pair;
            col[3] = 0;
            fontlib_001028d0(fsfont_pDefTagMng->saveColor);
            unsigned int cv;
            memcpy(&cv, col, 4);
            fontlib_00102a90(cv, fsfont_pDefTagMng->colorMask);
        }
        fsfont_tag_skip(pp, &scan);
    }
    D = fsfont_pDefTagMng;
    D->flag.isColor = !D->flag.isColor;
    return 0;
}
