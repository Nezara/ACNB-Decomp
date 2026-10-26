/* src/menu/mobj2.cpp — the second half of the menu object library (OLMENU; mobj.cpp has the sprite / string / number
 * / window / model objects): composite objects built from those, each an stMobj slot whose play / draw / packet
 * callbacks are statics of this file run by DF_MOBJ_Play / Draw / MakeVu1Paket:
 *   BlinkSprite (a sprite whose alpha pulses with the menu frame counter), TagWindow (window + shadow + string, the
 *   name-edit keys), IconModel / IcnMdlList (1-5 MMD models drawn as an icon, a row of them), MdlWin (a model window
 *   that slides in while the previous one fades) and Item (a labelled value line "NAME ...... 123" with an animated
 *   underline, the AC spec lists).
 * MdlWin: SetMdlWinChange builds the next model and picks one of four slide play functions by direction; the
 * slide moves the new model's show rect / centre in over 8 frames while the previous model shrinks and fades.
 * Item: the object keeps its label / value child objects; the value objects are rebuilt by ChangeItemParam(Ex), the
 * old ones fading out while the new ones fade in, and the underline alpha is tweened by __PlayItem. */
#include "acnb/hooks.h"
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

typedef void (*mobj_fn)(void);

#ifdef ACNB_SOURCEPORT
unsigned char itemLineAlpha = 0x50;
#endif

/* "f_draw = 0, f_erase = 1" (two read-modify-writes of byte 0 in the original) */
static inline void mobj2_erase(struct stMobj *o)
{
    o->f_draw = 0;
    o->f_erase = 1;
}

static inline void mobj2_col(struct struct_76b810ec *c, unsigned int w)
{
    __builtin_memcpy(c, &w, 4);
}

struct mobj2_pair { struct struct_76b810ec c[2]; };

/* ---------------------------------------------------------------- BlinkSprite */

/* BlinkSprite object data = DWARF struct_5356132a (sprite layout + alpha floor 0x80 + init_rect 0x84) */
static inline struct struct_5356132a *mobj2_blink_of(struct stMobj *o)
{
    return (struct struct_5356132a *)&o->dt;
}

static void __DrawBlinkSprite(struct stMobj *ioObjp);

static void __DrawBlinkSpriteNX(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    PS2_STACK(struct struct_13efd7f4, tex, 0x30);
    struct struct_5356132a *bs = mobj2_blink_of(ioObjp);
    /* the guest address of the stack buffer goes into the object while it draws */
    if (MenuTex_StandbyTexture(bs->texid, &tex)) bs->texp = &tex;
    __DrawBlinkSprite(ioObjp);
    bs->texp = 0;
}

static void __DrawBlinkSprite(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    PS2_STACK(struct struct_13efd7f4, tex, 0x30);
    PS2_STACK(struct struct_76b810ec, col, 0x4c);
    struct struct_5356132a *bs = mobj2_blink_of(ioObjp);
    DF_MDRAW_KickReg(0x42, bs->reg_alpha);
    col = bs->cur_col;
    if (MenuMng.f_help == 1 && !ioObjp->f_help) col.a = col.a >> 1;
    if (!bs->texp) {
        DF_MDRAW_DrawColSprite(&bs->rect, 1, &col, bs->depth);
        return;
    }
    __builtin_memcpy(&tex, bs->texp, 16);
    if (bs->f_linear == 1) {
        /* TEX1 of the copy: bit 5 set, then the 3-bit field at bit 6 = 1 */
        unsigned char *t1 = (unsigned char *)&tex + 8;
        t1[0] = (t1[0] & ~0x20) | 0x20;
        unsigned short h;
        __builtin_memcpy(&h, t1, 2);
        h = (h & 0xfe3f) | (1 << 6);
        __builtin_memcpy(t1, &h, 2);
    }
    if (bs->uv_dir == 0)
        DF_MDRAW_DrawTexSprite(&tex, &bs->rect, bs->uv, &col, bs->depth);
    else
        DF_MDRAW_DrawTexSprite2(&tex, &bs->rect, bs->uv, &col, bs->depth);
}

static void __PlayBlinkSprite(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    struct struct_5356132a *bs = mobj2_blink_of(ioObjp);
    int t = (int)(MenuMng.frame % 0x50u);
    if (t / 40 == 1) t = 0x50 - t;
    int k = (116 * t) / 40;
    int s = (int)((float)bs->init_col.a * (float)(0x46 + k) / 128.0f);

    switch (ioObjp->stat) {
    case 5:
        s = 0;
        break;
    case 1: {
        unsigned int cc = bs->fade_cc;
        bs->fade_cc = (unsigned char)(cc + 1);
        if ((int)cc < 6) {
            int n = bs->fade_cc;
            s = (n * s) / 6;
            DF_MFUNC_CalcInterColor(&bs->cur_col, &bs->prev_col, &bs->next_col, n, bs->fade_max);
        } else {
            bs->rect = bs->next_rect;
            bs->cur_col = bs->next_col;
            ioObjp->stat = 0;
        }
        break;
    }
    case 2: {
        unsigned int cc = bs->fade_cc;
        bs->fade_cc = (unsigned char)(cc + 1);
        if ((int)cc < 6) {
            int n = bs->fade_cc;
            s = ((6 - n) * s) / 6;
            DF_MFUNC_CalcInterColor(&bs->cur_col, &bs->prev_col, &bs->next_col, n, bs->fade_max);
        } else {
            s = 0;
            bs->rect = bs->prev_rect;
            bs->cur_col = bs->next_col;
            ioObjp->stat = 5;
        }
        break;
    }
    default:
        break;
    }
    /* alpha floor (dt+0x80, not written in this file) decays by 4 per frame */
    int fl = bs->alpha;
    if (s < fl) {
        s = fl;
        bs->alpha = fl - 4;
    }
    bs->cur_col.a = (unsigned char)s;
}

void DF_MOBJ2_SetBlinkSpriteSize(struct stMobj *inObjp, int inW, int inH)
{
    struct struct_5356132a *bs = mobj2_blink_of(inObjp);
    bs->rect.w = inW;
    bs->rect.h = inH;
}

void DF_MOBJ2_SetBlinkSpritePos(struct stMobj *inObjp, int inX, int inY)
{
    struct struct_5356132a *bs = mobj2_blink_of(inObjp);
    bs->rect.x = inX;
    bs->rect.y = inY;
}

/* quirk: the original runs GetFadeOutRect(PS2 0x30, PS2 0x10) for a NULL object; the port skips it */
void DF_MOBJ2_SetBlinkSpriteFadeOut(struct stMobj *inObjp)
{
    if (!inObjp) return;
    struct struct_5356132a *bs = mobj2_blink_of(inObjp);
    bs->prev_rect = bs->rect;
    bs->prev_col = bs->cur_col;
    bs->next_col = bs->cur_col;
    bs->next_col.a = 0;
    bs->fade_max = (inObjp->stat == 1) ? bs->fade_cc : 6;
    bs->fade_cc = 0;
    inObjp->stat = 2;
    DF_MFUNC_GetFadeOutRect(&bs->next_rect, &bs->rect);
}

/* quirk: next_rect = init_rect is stored before the NULL test and the tail call runs for NULL too (PS2 0x20..0xa0);
 * the port skips both for a NULL object. prev / next colour rgb come from cur_col, not init_col */
void DF_MOBJ2_SetBlinkSpriteFadeIn(struct stMobj *inObjp)
{
    if (!inObjp) return;
    struct struct_5356132a *bs = mobj2_blink_of(inObjp);
    bs->next_rect = bs->init_rect;
    bs->fade_max = 6;
    bs->prev_col = bs->cur_col;
    bs->next_col = bs->cur_col;
    bs->prev_col.a = 0;
    bs->next_col.a = bs->init_col.a;
    bs->fade_cc = 0;
    inObjp->stat = 1;
    DF_MFUNC_GetFadeInRect(&bs->prev_rect, &bs->init_rect);
}

static inline unsigned long long mobj2_blend_reg(const unsigned char *b)
{
    return (unsigned long long)b[0] | ((unsigned long long)b[1] << 2) | ((unsigned long long)b[2] << 4) |
           ((unsigned long long)b[3] << 6) | ((unsigned long long)b[4] << 32);
}

/* quirk: the reg_alpha store is not NULL-tested in the original (a NULL result writes PS2 0x60); the port skips it */
struct stMobj *DF_MOBJ2_NewBlinkSpriteFromLayout(struct stMobj *pMobj, struct _LAYOUT_RET_DATA *pLayout,
                                                 unsigned int depth, int f_linear)
{
    PS2_FRAME(0x30);
    int texid = pLayout->TexID;
    struct stMobj *obj = DF_MOBJ2_NewBlinkSprite(pMobj, 0, (struct stMenuRect *)&pLayout->Pos, pLayout->UV,
                                                 &pLayout->Col, depth, f_linear);
    if (!obj) return 0;
    mobj2_blink_of(obj)->texid = texid;
    obj->draw_func = (mobj_fn)GFN(__DrawBlinkSpriteNX);
    mobj2_blink_of(obj)->reg_alpha = mobj2_blend_reg(pLayout->Blend);
    return obj;
}

struct mobj2_layout { struct _LAYOUT_RET_DATA l; } __attribute__((aligned(16)));

struct stMobj *DF_MOBJ2_NewBlinkSpriteFromLayoutName(struct stMobj *pMobj, char *pLayoutHeader, char *pname,
                                                     unsigned int depth, int f_linear)
{
    PS2_FRAME(0x90);
    PS2_STACK(struct mobj2_layout, lay, 0x40);
    __builtin_memset(&lay, 0, sizeof(lay.l));
    DF_LO_GetLayoutData((unsigned char *)pLayoutHeader, (unsigned char *)pname, 0, &lay.l);
    int texid = lay.l.TexID;
    struct stMobj *obj = DF_MOBJ2_NewBlinkSprite(pMobj, 0, (struct stMenuRect *)&lay.l.Pos, lay.l.UV, &lay.l.Col,
                                                 depth, f_linear);
    if (!obj) return 0;   /* quirk as FromLayout: NULL reg_alpha store skipped */
    mobj2_blink_of(obj)->texid = texid;
    obj->draw_func = (mobj_fn)GFN(__DrawBlinkSpriteNX);
    mobj2_blink_of(obj)->reg_alpha = mobj2_blend_reg(lay.l.Blend);
    return obj;
}

void DF_MOBJ2_SetBlinkSpriteAlphaReg(struct stMobj *inObjp, unsigned long long reg_alpha)
{
    mobj2_blink_of(inObjp)->reg_alpha = reg_alpha;
}

struct stMobj *DF_MOBJ2_NewBlinkSpriteNX(struct stMobj *inObjp, int texid, struct stMenuRect *inRectp, int *inUV,
                                         struct struct_76b810ec *inColp, unsigned int inDepth, int inF_Linear)
{
    PS2_FRAME(0x20);
    struct stMobj *obj = DF_MOBJ2_NewBlinkSprite(inObjp, 0, inRectp, inUV, inColp, inDepth, inF_Linear);
    if (!obj) return 0;
    mobj2_blink_of(obj)->texid = texid;
    obj->draw_func = (mobj_fn)GFN(__DrawBlinkSpriteNX);
    return obj;
}

struct stMobj *DF_MOBJ2_NewBlinkSprite(struct stMobj *inObjp, struct struct_13efd7f4 *inTexp,
                                       struct stMenuRect *inRectp, int *inUV, struct struct_76b810ec *inColp,
                                       unsigned int inDepth, int inF_Linear)
{
    PS2_FRAME(0x80);
    PS2_STACK(struct struct_76b810ec, def, 0x7c);
    mobj2_col(&def, 0x80808080u);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    struct struct_5356132a *bs = mobj2_blink_of(obj);
    obj->type = 0;   /* the original stores 1, then 0 */
    bs->rect = *inRectp;
    bs->prev_rect = bs->rect;
    bs->next_rect = bs->prev_rect;
    bs->init_rect = bs->next_rect;
    /* lq: the uv source is aligned down to 16 bytes */
    __builtin_memcpy(bs->uv, PS2_LQ_SRC(inUV), 16);
    bs->texp = inTexp;
    struct struct_76b810ec *c = inColp ? inColp : &def;
    bs->cur_col = *c;
    bs->next_col = bs->cur_col;
    bs->init_col = bs->next_col;
    bs->prev_col = bs->init_col;
    bs->depth = inDepth;
    bs->reg_alpha = 0x44;
    bs->uv_dir = 0;
    bs->f_linear = (unsigned char)inF_Linear;
    bs->f_exfade = 1;
    obj->draw_func = (mobj_fn)GFN(__DrawBlinkSprite);
    obj->play_func = (mobj_fn)GFN(__PlayBlinkSprite);
    return obj;
}

/* ---------------------------------------------------------------- TagWindow */

/* TagWindow object data = DWARF struct_6e725289: winp = window, swinp = second window (never created), l_winp =
 * shadow window, strp = string, l_alpha = alpha floor, x / y = raw string offsets, f_sel, f_end = delete request,
 * f_child = ext flag (always 0: the 8-child branches at the window's dt+0x64 are dead) */
static inline struct struct_6e725289 *mobj2_tag_of(struct stMobj *o)
{
    return (struct struct_6e725289 *)&o->dt;
}

/* the 8 child pointers at a window's dt+0x64 (base = &win->dt or 0 as the original computes it) */
static inline struct stMobj **mobj2_win_children(char *base)
{
    return (struct stMobj **)(base + 0x64);
}

static inline void mobj2_erase_clear(struct stMobj **pp)
{
    if (*pp) {
        mobj2_erase(*pp);
        *pp = 0;
    }
}

static void __PlayTagWindow(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    struct struct_6e725289 *tw = mobj2_tag_of(ioObjp);
    char *wdt = tw->winp ? (char *)&tw->winp->dt : 0;
    char *w2dt = tw->swinp ? (char *)&tw->swinp->dt : 0;

    if (tw->f_end == 1) {
        if (tw->f_child) {   /* dead */
            if (wdt && mobj2_win_children(wdt)[0])
                for (int k = 0; k < 8; k++) mobj2_erase_clear(&mobj2_win_children(wdt)[k]);
            if (w2dt && mobj2_win_children(w2dt)[0])
                for (int k = 0; k < 8; k++) mobj2_erase_clear(&mobj2_win_children(w2dt)[k]);
        }
        mobj2_erase_clear(&tw->strp);
        mobj2_erase_clear(&tw->l_winp);
        mobj2_erase_clear(&tw->winp);
        mobj2_erase_clear(&tw->swinp);
        mobj2_erase(ioObjp);
        return;
    }

    tw->strp->f_help = ioObjp->f_help;
    int v = DF_MFUNC_GetBlinkAlpha();
    switch (ioObjp->stat) {
    case 2:
        if ((tw->winp && tw->winp->stat == 5) ||
            (tw->f_child && mobj2_win_children(wdt)[0] && mobj2_win_children(wdt)[0]->stat == 5)) {
            if (tw->strp->stat == 5) ioObjp->stat = 5;
        }
        break;
    case 1:   /* no NULL test of winp */
        if (tw->winp->stat == 0 && tw->strp->stat == 0) ioObjp->stat = 0;
        break;
    default:
        break;
    }
    if (v < tw->l_alpha) {
        v = tw->l_alpha;
        tw->l_alpha -= 4;
    }
    struct stMobj *w = tw->winp;   /* no NULL test */
    unsigned char wa = w->dt.window.cur_col.a;
    if ((tw->f_sel & 2) || w->dt.window.type == 0xa || w->dt.window.type == 0xb || w->dt.window.type == 0xc) {
        if (ioObjp->stat == 0) {
            if (tw->l_winp) tw->l_winp->dt.window.cur_col.a = 0x50;
            return;
        }
    }
    if (tw->l_winp) tw->l_winp->dt.window.cur_col.a = (unsigned char)((v * wa) / 128);
}

void DF_MOBJ2_SetTagWindowPos(struct stMobj *ioObjp, int inX, int inY)
{
    if (!ioObjp) return;
    struct struct_6e725289 *tw = mobj2_tag_of(ioObjp);
    if (tw->winp) {
        tw->winp->dt.window.cur_rect.x = inX;
        tw->winp->dt.window.cur_rect.y = inY;
    }
    /* raw offsets (NewTagWindow placed the string with 8 / -5 for 0); no NULL test of strp */
    tw->strp->dt.string.x = (short)(inX + tw->x);
    tw->strp->dt.string.y = (short)(inY + tw->y);
    if (tw->l_winp) {
        tw->l_winp->dt.window.cur_rect.x = inX - 8;
        tw->l_winp->dt.window.cur_rect.y = inY - 8;
    }
}

void DF_MOBJ2_SetTagWindowStr(struct stMobj *ioObjp, unsigned char *inStrp)
{
    if (!ioObjp) return;
    struct struct_6e725289 *tw = mobj2_tag_of(ioObjp);
    tw->strp->dt.string.ref_type = 2;
    tw->strp->dt.string.strp = (char *)inStrp;
}

void DF_MOBJ2_DeleteTagWindow(struct stMobj *ioObjp)
{
    if (ioObjp) mobj2_tag_of(ioObjp)->f_end = 1;
}

static inline unsigned char mobj2_darken(unsigned char v)
{
    return (v > 0x20) ? (unsigned char)(v - 0x20) : 0;
}

void DF_MOBJ2_SelectTagWindow(struct stMobj *ioObjp, int inFSel)
{
    PS2_FRAME(0x20);   /* leaf; mirrors its stack colour tables */
    PS2_STACK(struct mobj2_pair, tab, 8);
    PS2_STACK(struct struct_76b810ec, colC, 0x14);
    PS2_STACK(struct struct_76b810ec, colB, 0x18);
    PS2_STACK(struct struct_76b810ec, colA, 0x1c);
    mobj2_col(&tab.c[0], 0x60808080u);
    mobj2_col(&tab.c[1], 0x30808080u);
    mobj2_col(&colA, 0x40000000u);
    mobj2_col(&colB, 0x40010494u);
    mobj2_col(&colC, 0x408f2106u);
    if (!ioObjp) return;
    struct struct_6e725289 *tw = mobj2_tag_of(ioObjp);
    if (tw->f_sel == inFSel) return;
    tw->f_sel = (char)(inFSel & 7);
    tw->l_alpha = ((tw->f_sel & 1) == 1) ? 0xa4 : 0;
    if (tw->l_winp) tw->l_winp->f_draw = tw->f_sel & 1;
    if (tw->winp) {
        /* quirk: window types 0xa-0xc keep their fixed colour whatever the selection */
        struct struct_76b810ec *c;
        switch (tw->winp->dt.window.type) {
        case 0xa: c = &colA; break;
        case 0xb: c = &colB; break;
        case 0xc: c = &colC; break;
        default: c = &tab.c[tw->f_sel & 1]; break;
        }
        tw->winp->dt.window.cur_col = *c;
        tw->winp->dt.window.next_col = *c;
    }
    if (tw->f_sel & 4) {   /* quirk: darkens again at every selection change */
        struct struct_76b810ec *sc = &tw->strp->dt.string.cur_col;
        sc->r = mobj2_darken(sc->r);
        sc->g = mobj2_darken(sc->g);
        sc->b = mobj2_darken(sc->b);
    }
}

void DF_MOBJ2_SetTagWindowFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    if (!ioObjp) return;
    struct struct_6e725289 *tw = mobj2_tag_of(ioObjp);
    if (tw->f_child) {   /* dead */
        if (tw->winp && mobj2_win_children((char *)&tw->winp->dt)[0])
            for (int k = 0; k < 8; k++) DF_MOBJ_SetFadeOut(mobj2_win_children((char *)&tw->winp->dt)[k]);
        if (tw->swinp && mobj2_win_children((char *)&tw->swinp->dt)[0])
            for (int k = 0; k < 8; k++) DF_MOBJ_SetFadeOut(mobj2_win_children((char *)&tw->swinp->dt)[k]);
    }
    DF_MOBJ_SetFadeOut(tw->winp);
    DF_MOBJ_SetFadeOut(tw->swinp);
    DF_MOBJ_SetFadeOut(tw->l_winp);
    DF_MOBJ_SetFadeOut(tw->strp);
    ioObjp->stat = 2;
    ioObjp->count = 0;
}

void DF_MOBJ2_SetTagWindowFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    if (!ioObjp) return;
    struct struct_6e725289 *tw = mobj2_tag_of(ioObjp);
    DF_MOBJ_SetFadeIn(tw->winp);
    DF_MOBJ_SetFadeIn(tw->swinp);
    DF_MOBJ_SetFadeIn(tw->l_winp);
    DF_MOBJ_SetFadeIn(tw->strp);
    ioObjp->stat = 1;
    ioObjp->count = 0;
}

struct stMobj *DF_MOBJ2_NewTagWindow(struct stMobj *inObjp, struct stMenuRect *inRectp, int inWinType, int inOffX,
                                     int inOffY, int inCtgr, int inStrID, int inCharType,
                                     struct struct_76b810ec *inTextColp, unsigned int inDepth, int inFSel)
{
    PS2_FRAME(0xf0);
    PS2_STACK(struct stMenuRect, srect, 0xc0);
    PS2_STACK(struct mobj2_pair, tab, 0xd8);
    PS2_STACK(struct struct_76b810ec, colC, 0xe0);
    PS2_STACK(struct struct_76b810ec, colB, 0xe4);
    PS2_STACK(struct struct_76b810ec, colA, 0xe8);
    PS2_STACK(struct struct_76b810ec, scol, 0xec);
    mobj2_col(&tab.c[0], 0x60808080u);
    mobj2_col(&tab.c[1], 0x30808080u);
    mobj2_col(&scol, 0x80c8c8c8u);

    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    struct struct_6e725289 *tw = mobj2_tag_of(obj);
    obj->type = 0;
    obj->stat = 0;
    tw->rect = *inRectp;
    tw->x = (short)inOffX;
    tw->y = (short)inOffY;
    tw->f_sel = (char)(inFSel & 7);
    tw->f_child = 0;
    if (inOffX == 0) inOffX = 8;
    if (inOffY == 0) inOffY = -5;

    if (inWinType == 0xa) {
        mobj2_col(&colA, 0x40000000u);
        tw->winp = DF_MOBJ_NewWindow(obj, inWinType, 3, &tw->rect, &colA, inDepth);
    } else if (inWinType == 0xb) {
        mobj2_col(&colB, 0x40010494u);
        tw->winp = DF_MOBJ_NewWindow(obj, inWinType, 3, &tw->rect, &colB, inDepth);
    } else if (inWinType == 0xc) {
        mobj2_col(&colC, 0x408f2106u);
        tw->winp = DF_MOBJ_NewWindow(obj, inWinType, 3, &tw->rect, &colC, inDepth);
    } else {
        tw->winp = DF_MOBJ_NewWindow(obj, inWinType, 0, &tw->rect, &tab.c[tw->f_sel & 1], inDepth);
    }

    int st;
    switch (inWinType) {
    case 0x10: st = 0x11; break;
    case 0xa: case 0xb: case 0xc: st = 0xd; break;
    case 0xe: st = 0xf; break;
    case 8: st = 9; break;
    case 6: st = 7; break;
    case 4: st = 5; break;
    default: st = 3; break;   /* 2 -> 3 and every unlisted type */
    }
    srect = tw->rect;
    if (st == 5 || st == 7) {
        srect.x -= 8; srect.y -= 6; srect.w += 0x10; srect.h += 0xc;
    } else if (st == 0x11) {
        srect.x -= 4; srect.y -= 3; srect.w += 8; srect.h += 6;
    } else {
        srect.x -= 8; srect.y -= 8; srect.w += 0xf; srect.h += 0xf;
    }
    tw->l_winp = DF_MOBJ_NewWindow(tw->winp, st, 0, &srect, &scol, inDepth + 1000000);
    tw->l_winp->f_draw = tw->f_sel & 1;   /* no NULL test */
    tw->strp = DF_MOBJ_NewString(tw->l_winp, inCtgr, inStrID, 0, inCharType, tw->rect.x + inOffX, tw->rect.y + inOffY,
                                 inTextColp, inDepth + 1000002, 0);
    obj->play_func = (mobj_fn)GFN(__PlayTagWindow);
    return obj;
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper; inTextColp / inDepth / inFSel are the caller's stack args at sp+0 / +8 / +0x10 */
static void bw__DF_MOBJ2_NewTagWindow(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    struct stMobj *r = DF_MOBJ2_NewTagWindow(
        (struct stMobj *)bridge_ptr(c->gpr[A0].uw[0]), (struct stMenuRect *)bridge_ptr(c->gpr[A1].uw[0]),
        c->gpr[A2].sw[0], c->gpr[A3].sw[0], c->gpr[T0].sw[0], c->gpr[T1].sw[0], c->gpr[T2].sw[0], c->gpr[T3].sw[0],
        (struct struct_76b810ec *)bridge_ptr(rd32(sp)), rd32(sp + 8), (int)rd32(sp + 0x10));
    c->gpr[V0].sd[0] = (s32)PS2PTR(r);
}
#endif

/* ---------------------------------------------------------------- IconModel */

/* IconModel object data = DWARF struct_6280830a: loc (pos / ang quads), scl[5], mp[5] model slots, id[5] MMD ids,
 * mulalpha (fade alpha copied into every model), f_sel, f_end = delete request, type */
static inline struct struct_6280830a *mobj2_icon_of(struct stMobj *o)
{
    return (struct struct_6280830a *)&o->dt;
}

/* lq / sq quad copy: the source address is aligned down to 16 bytes */
static inline void mobj2_lq(void *dst, const void *src)
{
    __builtin_memcpy(dst, PS2_LQ_SRC(src), 16);
}

/* builds model slot k from id[k] (nothing for id -1): the object's MMD, shared through the kind-2 MMD table */
static void mobj2_icon_build(struct struct_6280830a *d, int k)
{
    int id = d->id[k];
    if (id == -1) return;
    void *objmmd = DF_MMD_GetObjectMMD(id);
    d->mp[k] = DF_MMODEL_GetNewModel(0);
    struct stMmdData *mmd = DF_MMD_GetMMD(2, id);
    if (!mmd) {
        mmd = DF_MMD_GetNewMMD(0);
        DF_MMD_SetData(mmd, 2, id, objmmd);
    }
    DF_MMODEL_SetData(d->mp[k], mmd, &d->loc.pos, &d->loc.ang, &d->scl[k]);
}

/* quirk: the original stores the model's alpha / test registers without a NULL test (a missing slot writes PS2
 * 0x80 / 0x88); the port skips the store for a NULL model */
static inline void mobj2_icon_alpha(struct _stMModel *m, unsigned long long v)
{
    if (m) m->alpha_reg = v;
}

static inline void mobj2_icon_test(struct _stMModel *m, unsigned long long v)
{
    if (m) m->test_reg = v;
}

static void __PlayIconModel(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    struct struct_6280830a *d = mobj2_icon_of(ioObjp);
    if (d->f_end == 1) {
        /* slots 0 / 1 keep their MMD, 2-4 delete it with the model */
        for (int i = 0; i < 5; i++) {
            if (i < 2) {
                if (d->mp[i]) {
                    DF_MMODEL_DeleteModel(d->mp[i]);
                    d->mp[i] = 0;
                }
            } else if (d->mp[i]) {
                DF_MMODEL_DeleteModelAndMMD(d->mp[i]);
                d->mp[i] = 0;
            }
        }
        if (ioObjp) mobj2_erase(ioObjp);
        return;
    }
    switch (ioObjp->stat) {
    case 1: {
        unsigned char old = ioObjp->count;
        ioObjp->count = old + 1;
        if (old < 6) {
            d->mulalpha = (float)(int)ioObjp->count / 6.0f;
        } else {
            d->mulalpha = 1.0f;
            ioObjp->stat = 0;
        }
        break;
    }
    case 2: {
        unsigned char old = ioObjp->count;
        ioObjp->count = old + 1;
        if (old < 6) {
            d->mulalpha = 1.0f - (float)(int)ioObjp->count / 6.0f;
        } else {
            d->mulalpha = 0.0f;
            ioObjp->stat = 5;
        }
        break;
    }
    }
    for (int i = 0; i < 5; i++) {
        if (d->mp[i]) {
            d->mp[i]->mulalpha = d->mulalpha;
            DF_MMODEL_Play(d->mp[i]);
        }
    }
}

void DF_MOBJ2_SetIconModelLocate(struct stMobj *ioObjp, struct struct_9109f562 *inLocp)
{
    if (!ioObjp || !inLocp) return;
    struct struct_6280830a *d = mobj2_icon_of(ioObjp);
    mobj2_lq(&d->loc.pos, inLocp);
    mobj2_lq(&d->loc.ang, (char *)inLocp + 0x10);
    for (int i = 0; i < 5; i++) {
        if (d->mp[i]) {
            mobj2_lq(d->mp[i]->pos, d->loc.pos);
            mobj2_lq(d->mp[i]->ang, d->loc.ang);
        }
    }
}

void DF_MOBJ2_DeleteIconModel(struct stMobj *ioObjp)
{
    if (ioObjp) mobj2_icon_of(ioObjp)->f_end = 1;
}

void DF_MOBJ2_SelectIconModel(struct stMobj *ioObjp, int inFSel)
{
    PS2_FRAME(0x80);
    PS2_STACK(struct struct_76b810ec, c6c, 0x6c);
    PS2_STACK(struct struct_76b810ec, c70, 0x70);
    PS2_STACK(struct struct_76b810ec, c74, 0x74);
    PS2_STACK(struct struct_76b810ec, c78, 0x78);
    PS2_STACK(struct struct_76b810ec, c7c, 0x7c);
    mobj2_col(&c7c, 0x64808080u);
    mobj2_col(&c78, 0x4096966cu);
    mobj2_col(&c74, 0x8096966cu);
    mobj2_col(&c70, 0x28808080u);
    mobj2_col(&c6c, 0x50808080u);
    if (!ioObjp) return;
    struct struct_6280830a *d = mobj2_icon_of(ioObjp);
    if (d->f_sel == inFSel) return;
    d->f_sel = (char)inFSel;
    if (d->f_sel == 1) {
        /* selected: an extra model in slot 4 */
        mobj2_icon_build(d, 4);
        if (d->mp[4]) {
            d->mp[4]->test_reg = (d->type == 3) ? 0x50000 : 0x30000;
            d->mp[4]->alpha_reg = 0x48;
            DF_MMODEL_SetVertColor(d->mp[4], 0, &c7c);
        }
    } else if (d->mp[4]) {
        DF_MMODEL_DeleteModel(d->mp[4]);
        d->mp[4] = 0;
    }
    if ((d->type & 0xf) != 0) return;
    struct struct_76b810ec *c3, *c0;
    if (d->f_sel != 0) {
        c3 = &c74;
        c0 = &c6c;
    } else {
        c3 = &c78;
        c0 = &c70;
    }
    if (d->mp[3]) DF_MMODEL_SetVertColor(d->mp[3], 0, c3);
    if (d->mp[0]) DF_MMODEL_SetVertColor(d->mp[0], 0, c0);
}

/* no NULL test (the original writes through a NULL object) */
void DF_MOBJ2_SetIconModelFadeOut(struct stMobj *ioObjp)
{
    mobj2_icon_of(ioObjp)->mulalpha = 1.0f;
    ioObjp->stat = 2;
    ioObjp->count = 0;
}

void DF_MOBJ2_SetIconModelFadeIn(struct stMobj *ioObjp)
{
    if (!ioObjp) return;
    mobj2_icon_of(ioObjp)->mulalpha = 0.0f;
    ioObjp->stat = 1;
    ioObjp->count = 0;
}

/* quirks: types 2, 5-8, 10, 11 fall into the default block (slots 0-3); type 4 builds only slot 0 but colours
 * slots 0-3 (SetVertColor tests NULL itself); type 3 with inFSel builds slot 4 here and SelectIconModel builds a
 * second one over it (the first model leaks) */
struct stMobj *DF_MOBJ2_NewIconModel(struct stMobj *inObjp, int inType, struct struct_9109f562 *inLocp,
                                     float (*inScalep)[4], struct struct_76b810ec inCol, int *inIDp, int inFSel)
{
    PS2_FRAME(0xb0);
    PS2_STACK(struct struct_76b810ec, col, 0x98);
    PS2_STACK(struct struct_76b810ec, c9c, 0x9c);
    PS2_STACK(struct struct_76b810ec, ca0, 0xa0);
    PS2_STACK(struct struct_76b810ec, ca4, 0xa4);
    PS2_STACK(struct struct_76b810ec, ca8, 0xa8);
    PS2_STACK(struct struct_76b810ec, cac, 0xac);
    col = inCol;
    mobj2_col(&cac, 0x4096966cu);
    mobj2_col(&ca8, 0x28808080u);
    mobj2_col(&ca4, 0x809e985eu);
    mobj2_col(&ca0, 0x40808080u);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    struct struct_6280830a *d = mobj2_icon_of(obj);
    obj->type = 0;
    obj->stat = 0;
    d->mulalpha = 1.0f;
    mobj2_lq(&d->loc.pos, inLocp);
    mobj2_lq(&d->loc.ang, (char *)inLocp + 0x10);
    d->type = (char)inType;
    for (int i = 0; i < 5; i++) {
        d->id[i] = inIDp[i];
        mobj2_lq(d->scl[i], inScalep ? inScalep[i] : UnitVectorF);
    }

    switch (inType) {
    case 4:
        mobj2_icon_build(d, 0);
        break;
    case 3:
        mobj2_icon_build(d, 0);
        mobj2_icon_build(d, 1);
        if (inFSel) mobj2_icon_build(d, 4);
        break;
    case 9:
        mobj2_icon_build(d, 3);
        break;
    default:
        mobj2_icon_build(d, 0);
        mobj2_icon_build(d, 1);
        mobj2_icon_build(d, 2);
        mobj2_icon_build(d, 3);
        break;
    }

    switch (inType) {
    case 9:
        DF_MMODEL_SetVertColor(d->mp[3], 0, &cac);
        break;
    case 3:
        mobj2_col(&c9c, 0x80808080u);
        DF_MMODEL_SetVertColor(d->mp[1], 0, &c9c);
        DF_MMODEL_SetVertColor(d->mp[0], 0, &c9c);
        if (inFSel) DF_MMODEL_SetVertColor(d->mp[4], 0, &c9c);
        break;
    default:
        DF_MMODEL_SetVertColor(d->mp[3], 0, &cac);
        DF_MMODEL_SetVertColor(d->mp[0], 0, &ca8);
        DF_MMODEL_SetVertColor(d->mp[2], 0, &ca0);
        DF_MMODEL_SetVertColor(d->mp[1], 0, (inType == 1) ? &col : &ca4);
        break;
    }

    switch (inType) {
    case 9:
        mobj2_icon_alpha(d->mp[3], 0x48);
        mobj2_icon_test(d->mp[3], 0x30000);
        break;
    case 3:
        mobj2_icon_alpha(d->mp[0], 0x42);
        mobj2_icon_alpha(d->mp[1], 0x44);
        if (inFSel) mobj2_icon_alpha(d->mp[4], 0x48);
        mobj2_icon_test(d->mp[0], 0x30000);
        mobj2_icon_test(d->mp[1], 0x70000);
        if (inFSel) mobj2_icon_test(d->mp[4], 0x50000);
        break;
    case 4:
        mobj2_icon_alpha(d->mp[0], 0x48);
        break;
    default:
        mobj2_icon_alpha(d->mp[0], 0x48);
        mobj2_icon_alpha(d->mp[1], 0x44);
        mobj2_icon_alpha(d->mp[2], 0x42);
        mobj2_icon_alpha(d->mp[3], 0x48);
        mobj2_icon_test(d->mp[0], 0x30000);
        mobj2_icon_test(d->mp[1], 0x30000);
        mobj2_icon_test(d->mp[2], 0x30000);
        mobj2_icon_test(d->mp[3], 0x30000);
        break;
    }

    DF_MOBJ2_SelectIconModel(obj, inFSel);
    obj->play_func = (mobj_fn)GFN(__PlayIconModel);
    return obj;
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper; the by-value colour arrives as the low word of t0 (r = byte 0) */
static void bw__DF_MOBJ2_NewIconModel(EECore *c)
{
    struct struct_76b810ec col;
    unsigned int w = c->gpr[T0].uw[0];
    __builtin_memcpy(&col, &w, 4);
    struct stMobj *r = DF_MOBJ2_NewIconModel(
        (struct stMobj *)bridge_ptr(c->gpr[A0].uw[0]), c->gpr[A1].sw[0],
        (struct struct_9109f562 *)bridge_ptr(c->gpr[A2].uw[0]), (float (*)[4])bridge_ptr(c->gpr[A3].uw[0]), col,
        (int *)bridge_ptr(c->gpr[T1].uw[0]), c->gpr[T2].sw[0]);
    c->gpr[V0].sd[0] = (s32)PS2PTR(r);
}
#endif

/* ---------------------------------------------------------------- IcnMdlList */

/* IcnMdlList object data = DWARF struct_5160bf84: iconp[16] IconModels, idxp = shared selection index, pre_idx,
 * num, type, f_end = delete request */
static inline struct struct_5160bf84 *mobj2_icnlist_of(struct stMobj *o)
{
    return (struct struct_5160bf84 *)&o->dt;
}

static void __PktIcnMdlList(struct stMobj *ioObjp)
{
    PS2_FRAME(0x60);
    struct struct_5160bf84 *l = mobj2_icnlist_of(ioObjp);
    if (l->f_end == 1) return;
    unsigned int *dmap = DF_MPKT_MakeVIFCode(MPktCurrentp, 0, 0, 0, 0x13000000);
    /* slot-major: slot 0 of every icon, then slot 1, ... */
    for (int s = 0; s < 5; s++) {
        for (int i = 0; i < l->num; i++) {
            struct stMobj *icon = l->iconp[i];
            if (!icon) continue;
            struct _stMModel *m = mobj2_icon_of(icon)->mp[s];
            if (m) dmap = DF_MMODEL_MakeVu1PacketAC(dmap, m, 0, 3);
        }
    }
    MPktCurrentp = dmap;
}

static void __PlayIcnMdlList(struct stMobj *ioObjp)
{
    PS2_FRAME(0x20);
    struct struct_5160bf84 *l = mobj2_icnlist_of(ioObjp);
    if (l->f_end == 1) {
        for (int i = 0; i < l->num; i++)
            if (l->iconp[i]) mobj2_icon_of(l->iconp[i])->f_end = 1;
        if (ioObjp) mobj2_erase(ioObjp);
        return;
    }
    switch (ioObjp->stat) {
    case 0: {
        int cur = *l->idxp;
        if (l->pre_idx == cur) break;
        /* quirk: the entries are not NULL-tested (SelectIconModel tests NULL itself) */
        if (cur >= 0 && cur < l->num) DF_MOBJ2_SelectIconModel(l->iconp[cur], 1);
        int pre = l->pre_idx;
        if (pre >= 0 && pre < l->num) DF_MOBJ2_SelectIconModel(l->iconp[pre], 0);
        l->pre_idx = *l->idxp;
        break;
    }
    case 1:
        if (l->iconp[0] && l->iconp[0]->stat == 0) ioObjp->stat = 0;
        break;
    case 2:
        if (l->iconp[0] && l->iconp[0]->stat == 5) ioObjp->stat = 5;
        break;
    }
}

void DF_MOBJ2_DeleteIcnMdlList(struct stMobj *ioObjp)
{
    mobj2_icnlist_of(ioObjp)->f_end = 1;
}

/* quirk: FadeOut does not NULL-test the list entries (FadeIn does); the port skips NULL entries */
void DF_MOBJ2_SetIcnMdlListFadeOut(struct stMobj *ioObjp)
{
    if (!ioObjp) return;
    struct struct_5160bf84 *l = mobj2_icnlist_of(ioObjp);
    for (int i = 0; i < l->num; i++) {
        struct stMobj *icon = l->iconp[i];
        if (!icon) continue;
        mobj2_icon_of(icon)->mulalpha = 1.0f;
        icon->stat = 2;
        icon->count = 0;
    }
    ioObjp->stat = 2;
    ioObjp->count = 0;
}

/* no NULL test of the list object itself */
void DF_MOBJ2_SetIcnMdlListFadeIn(struct stMobj *ioObjp)
{
    struct struct_5160bf84 *l = mobj2_icnlist_of(ioObjp);
    for (int i = 0; i < l->num; i++) {
        struct stMobj *icon = l->iconp[i];
        if (icon) {
            mobj2_icon_of(icon)->mulalpha = 0.0f;
            icon->stat = 1;
            icon->count = 0;
        }
    }
    ioObjp->stat = 1;
    ioObjp->count = 0;
}

struct mobj2_ids { int v[5]; };

/* quirks: types 0 and 1 are the same code (each item gets the next slot-3 id); type 0xc builds type-0 icons and
 * offsets each icon's slot-3 texture (addST[1]) by i / 16 */
struct stMobj *DF_MOBJ2_NewIcnMdlList(struct stMobj *inObjp, int inNum, int inType, struct struct_9109f562 *inLocp,
                                      float (*inScalep)[4], struct struct_76b810ec *inColp, int *inBaseIDp,
                                      int *ioIdxp)
{
    PS2_FRAME(0xd0);
    PS2_STACK(struct mobj2_ids, ids, 0xb0);
    PS2_STACK(struct struct_76b810ec, def, 0xcc);
    mobj2_col(&def, 0x4096966cu);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    struct struct_5160bf84 *l = mobj2_icnlist_of(obj);
    obj->type = 0;
    obj->stat = 0;
    l->num = inNum;
    l->type = (char)inType;
    l->idxp = ioIdxp;
    l->pre_idx = *ioIdxp;
    int type = (inType == 0xc) ? 0 : inType;
    for (int k = 0; k < 5; k++) ids.v[k] = inBaseIDp[k];
    struct struct_76b810ec *colp = inColp ? inColp : &def;
    struct struct_9109f562 *locp = inLocp;
    for (int i = 0; i < l->num; i++) {
        l->iconp[i] = DF_MOBJ2_NewIconModel(obj, type, locp, inScalep, *colp, ids.v, i == *l->idxp);
        switch (l->type & 0xf) {
        case 0xc: {
            /* quirk: the icon and its slot 3 are not NULL-tested in the original; the port skips the store */
            struct stMobj *icon = l->iconp[i];
            struct _stMModel *m = icon ? mobj2_icon_of(icon)->mp[3] : 0;
            float f = 0.0625f * (float)i - 0.001953125f;
            if (m) m->addST[1] = f;
            break;
        }
        case 0:
        case 1:
            if (ids.v[3] != -1) ids.v[3]++;
            break;
        }
        locp++;
        if (inColp) colp++;
    }
    obj->play_func = (mobj_fn)GFN(__PlayIcnMdlList);
    obj->pkt_func = (mobj_fn)GFN(__PktIcnMdlList);
    return obj;
}

/* ---------------------------------------------------------------- MdlWin */

/* MdlWin object data = DWARF struct_f8cbbdcd; the model objects it owns use struct_9db062e8 */
static inline struct struct_f8cbbdcd *mobj2_mdlwin_of(struct stMobj *o)
{
    return (struct struct_f8cbbdcd *)&o->dt;
}

static inline struct struct_9db062e8 *mobj2_model_of(struct stMobj *o)
{
    return (struct struct_9db062e8 *)&o->dt;
}

#define MOBJ2_SLIDE_F 0x1.b6db6ep-1f   /* 6/7 (0x3f5b6db7) */
#define MOBJ2_PI 0x1.921fb6p+1f        /* 0x40490fdb */

static void __PlayMdlWin(struct stMobj *ioObjp);

/* common head of the four slide functions: false = skip this frame (fAddDirt, count >= 3, f_stay) */
static inline int mobj2_mdlwin_slide_head(struct stMobj *o)
{
    struct struct_f8cbbdcd *w = mobj2_mdlwin_of(o);
    if (gMManageCtr.fAddDirt != 0 && o->count >= 3 && w->f_stay != 0) return 0;
    if (w->f_play_after_cleate) {
        struct struct_9db062e8 *cm = mobj2_model_of(w->cur.modelp);
        int a = (short)(cm->cur_alpha + 0x12);
        if (cm->next_alpha < a) a = cm->next_alpha;
        cm->cur_alpha = (unsigned char)a;
        struct struct_9db062e8 *fm = mobj2_model_of(w->fade.modelp);
        a = (short)(fm->cur_alpha - 0x12);
        if (a < fm->next_alpha) a = fm->next_alpha;
        fm->cur_alpha = (unsigned char)a;
    }
    return 1;
}

/* count++; true when the slide finished (old count >= 7): drop the faded model, settle the new one */
static inline int mobj2_mdlwin_slide_end(struct stMobj *o)
{
    unsigned char old = o->count;
    o->count = old + 1;
    if (old < 7) return 0;
    struct struct_f8cbbdcd *w = mobj2_mdlwin_of(o);
    struct struct_9db062e8 *cm = mobj2_model_of(w->cur.modelp);
    if (w->fade.modelp) {
        mobj2_erase(w->fade.modelp);
        w->fade.modelp = 0;
    }
    cm->show_rect = w->rect;
    cm->cx = w->offset_cx;
    cm->cy = w->offset_cy;
    o->stat = 0;
    o->play_func = (mobj_fn)GFN(__PlayMdlWin);
    return 1;
}

static void __PlayMdlWinMoveDown(struct stMobj *ioObjp)
{
    struct struct_f8cbbdcd *w = mobj2_mdlwin_of(ioObjp);
    if (!mobj2_mdlwin_slide_head(ioObjp)) return;
    struct struct_9db062e8 *cm = mobj2_model_of(w->cur.modelp);
    struct struct_9db062e8 *fm = mobj2_model_of(w->fade.modelp);
    if (mobj2_mdlwin_slide_end(ioObjp)) return;
    int d = w->rect.h / 7;
    cm->show_rect.h += d;
    cm->cy += MOBJ2_SLIDE_F;
    if (gMManageCtr.fAddDirt) cm->cy += MOBJ2_SLIDE_F;
    fm->show_rect.h -= d;
    fm->show_rect.y += d;
    fm->cy += MOBJ2_SLIDE_F;
    if (gMManageCtr.fAddDirt) fm->cy += MOBJ2_SLIDE_F;
}

static void __PlayMdlWinMoveUp(struct stMobj *ioObjp)
{
    struct struct_f8cbbdcd *w = mobj2_mdlwin_of(ioObjp);
    if (!mobj2_mdlwin_slide_head(ioObjp)) return;
    struct struct_9db062e8 *cm = mobj2_model_of(w->cur.modelp);
    struct struct_9db062e8 *fm = mobj2_model_of(w->fade.modelp);
    if (mobj2_mdlwin_slide_end(ioObjp)) return;
    int d = w->rect.h / 7;
    cm->show_rect.h += d;
    cm->show_rect.y -= d;
    cm->cy -= MOBJ2_SLIDE_F;
    if (gMManageCtr.fAddDirt) cm->cy -= MOBJ2_SLIDE_F;
    fm->show_rect.h -= d;
    fm->cy -= MOBJ2_SLIDE_F;
    if (gMManageCtr.fAddDirt) fm->cy -= MOBJ2_SLIDE_F;
}

static void __PlayMdlWinMoveRight(struct stMobj *ioObjp)
{
    struct struct_f8cbbdcd *w = mobj2_mdlwin_of(ioObjp);
    if (!mobj2_mdlwin_slide_head(ioObjp)) return;
    struct struct_9db062e8 *cm = mobj2_model_of(w->cur.modelp);
    struct struct_9db062e8 *fm = mobj2_model_of(w->fade.modelp);
    if (mobj2_mdlwin_slide_end(ioObjp)) return;
    int d = w->rect.w / 7;
    cm->show_rect.w += d;
    cm->cx += MOBJ2_SLIDE_F;
    if (gMManageCtr.fAddDirt) cm->cx += MOBJ2_SLIDE_F;
    /* quirk: the fading rect goes through float with the 6/7 step instead of d (x stays, w loses 1 per frame) */
    fm->show_rect.x = ps2_ftoi((float)fm->show_rect.x + MOBJ2_SLIDE_F);
    fm->show_rect.w = ps2_ftoi((float)fm->show_rect.w - MOBJ2_SLIDE_F);
    fm->cx += MOBJ2_SLIDE_F;
    if (gMManageCtr.fAddDirt) fm->cx += MOBJ2_SLIDE_F;
}

static void __PlayMdlWinMoveLeft(struct stMobj *ioObjp)
{
    struct struct_f8cbbdcd *w = mobj2_mdlwin_of(ioObjp);
    if (!mobj2_mdlwin_slide_head(ioObjp)) return;
    struct struct_9db062e8 *cm = mobj2_model_of(w->cur.modelp);
    struct struct_9db062e8 *fm = mobj2_model_of(w->fade.modelp);
    if (mobj2_mdlwin_slide_end(ioObjp)) return;
    int d = w->rect.w / 7;
    cm->show_rect.x -= d;
    cm->show_rect.w += d;
    cm->cx -= MOBJ2_SLIDE_F;
    if (gMManageCtr.fAddDirt) cm->cx -= MOBJ2_SLIDE_F;
    fm->show_rect.w -= d;
    fm->cx -= MOBJ2_SLIDE_F;
    if (gMManageCtr.fAddDirt) fm->cx -= MOBJ2_SLIDE_F;
}

static void __PlayMdlWin(struct stMobj *ioObjp)
{
    struct struct_f8cbbdcd *w = mobj2_mdlwin_of(ioObjp);
    if (w->csrp[0] && w->csrp[1] && w->csrp[2] && w->csrp[3]) {
        w->csrp[1]->f_draw = w->f_csr_h & 1;
        w->csrp[0]->f_draw = w->csrp[1]->f_draw;
        w->csrp[3]->f_draw = w->f_csr_v & 1;
        w->csrp[2]->f_draw = w->csrp[3]->f_draw;
    }
    if (w->f_end == 1) {
        if (w->fade.modelp) { mobj2_erase(w->fade.modelp); w->fade.modelp = 0; }
        if (w->cur.modelp) { mobj2_erase(w->cur.modelp); w->cur.modelp = 0; }
        for (int i = 0; i < 8; i++) {
            if (w->pWindow[i]) { mobj2_erase(w->pWindow[i]); w->pWindow[i] = 0; }
        }
        for (int j = 0; j < 4; j++) {
            if (w->csrp[j]) *((signed char *)w->csrp[j] + 0x2e) = 1;
        }
        if (ioObjp) mobj2_erase(ioObjp);
        return;
    }
    switch (ioObjp->stat) {
    case 1:
    case 2:
        if (w->cur.modelp) ioObjp->stat = w->cur.modelp->stat;
        break;
    }
}

struct mobj2_dirtab { int v[4]; };

void DF_MOBJ2_SetMdlWinChange(struct stMobj *ioObjp, int inKind, int inID, struct struct_c4910010 *inPColp,
                              struct struct_c4910010 *inPColp2, int inFlag, int inDir, int inSceneType,
                              struct stMenuAC *pAC)
{
    PS2_FRAME(0x90);
    PS2_STACK(struct mobj2_dirtab, tab, 0x60);
    PS2_STACK(struct stMenuRect, nr, 0x70);   /* quirk: stale for inDir outside 1..4 */
    PS2_STACK(struct stMenuRect, r, 0x80);
    struct struct_f8cbbdcd *w = mobj2_mdlwin_of(ioObjp);
    int dx = 0, dy = 0;

    tab.v[0] = 4; tab.v[1] = 3; tab.v[2] = 2; tab.v[3] = 1;   /* OLMENU rodata 0x1edf640 */
    if (w->fade.modelp) {
        mobj2_erase(w->fade.modelp);
        w->fade.modelp = 0;
    }
    w->fade = w->cur;
    w->dir = (char)inDir;
    w->cur.p_colp = inPColp;
    w->cur.p_colp2 = inPColp2;
    ioObjp->stat = 4;
    ioObjp->count = 0;
    r = w->rect;
    switch (w->dir) {
    case 1:
        ioObjp->play_func = (mobj_fn)GFN(__PlayMdlWinMoveUp);
        dy = 6;
        r.y += r.h;
        nr = r;
        nr.h = 0;
        break;
    case 2:
        ioObjp->play_func = (mobj_fn)GFN(__PlayMdlWinMoveDown);
        dy = -6;
        nr = r;
        nr.h = 0;
        break;
    case 3:
        ioObjp->play_func = (mobj_fn)GFN(__PlayMdlWinMoveLeft);
        dx = 6;
        r.x += r.w;
        nr = r;
        nr.w = 0;
        break;
    case 4:
        ioObjp->play_func = (mobj_fn)GFN(__PlayMdlWinMoveRight);
        dx = -6;
        nr = r;
        nr.w = 0;
        break;
    }
    /* parent: the last 29 slots when the model plays at once, else winp (NULL for NexusNewMdlWin windows) */
    struct stMobj *parent = w->f_play_after_cleate ? (struct stMobj *)((char *)MenuObjTopp + 400000) : w->winp;
    w->cur.modelp = DF_MOBJ_NewModelAC(parent, inKind, inID, inFlag, &w->rect, w->cur.p_colp, w->cur.p_colp2, 0x80,
                                       MOBJ2_PI, pAC, w->player, inSceneType, 0.0f, 0.0f);
    if (w->f_play_after_cleate) {
        struct stMobj *m = w->cur.modelp;
        gcall_as<void(struct stMobj *)>(m->play_func, m);
        mobj2_model_of(w->cur.modelp)->dispflag |= 0x200000;
        mobj2_model_of(w->cur.modelp)->scale[0] = w->scale;
        mobj2_model_of(w->cur.modelp)->scale[1] = w->scale;
        mobj2_model_of(w->cur.modelp)->scale[2] = w->scale;
        mobj2_model_of(w->cur.modelp)->scale[3] = 1.0f;
    }
    struct struct_9db062e8 *cm = mobj2_model_of(w->cur.modelp);
    cm->show_rect = nr;
    cm->cx = (float)dx + w->offset_cx;
    mobj2_model_of(w->cur.modelp)->cy = (float)dy + w->offset_cy;
    w->cur.sprtp = 0;
    if (w->f_play_after_cleate) {
        mobj2_model_of(w->cur.modelp)->cur_alpha = 0;
        mobj2_model_of(w->cur.modelp)->next_alpha = 0x80;
        /* quirk: fade.modelp is not NULL-tested */
        mobj2_model_of(w->fade.modelp)->cur_alpha = 0x80;
        mobj2_model_of(w->fade.modelp)->next_alpha = 0;
    }
    /* cursor j <-> dir 4, 3, 2, 1 */
    for (int j = 0; j < 4; j++) {
        signed char v = (w->dir == tab.v[j]) ? 1 : 2;
        struct stMobj *c = w->csrp[j];
        if (c) {
            *((signed char *)c + 0x2c) = v;
            if (v == 2) *(int *)((char *)c + 0x28) = 0;
            else if (v == 1) *(int *)((char *)c + 0x28) = 0xba;
        }
    }
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper; pAC is the caller's stack arg at sp+0 */
static void bw__DF_MOBJ2_SetMdlWinChange(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    DF_MOBJ2_SetMdlWinChange((struct stMobj *)bridge_ptr(c->gpr[A0].uw[0]), c->gpr[A1].sw[0], c->gpr[A2].sw[0],
                             (struct struct_c4910010 *)bridge_ptr(c->gpr[A3].uw[0]),
                             (struct struct_c4910010 *)bridge_ptr(c->gpr[T0].uw[0]), c->gpr[T1].sw[0],
                             c->gpr[T2].sw[0], c->gpr[T3].sw[0], (struct stMenuAC *)bridge_ptr(rd32(sp)));
}
#endif

void DF_MOBJ2_DeleteMdlWin(struct stMobj *ioObjp)
{
    mobj2_mdlwin_of(ioObjp)->f_end = 1;
}

void DF_MOBJ2_SetMdlWinFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct struct_f8cbbdcd *w = mobj2_mdlwin_of(ioObjp);
    for (int i = 0; i < 8; i++) {
        if (w->pWindow[i]) DF_MOBJ_SetFadeOut(w->pWindow[i]);
    }
    DF_MOBJ_SetFadeOut(w->cur.modelp);
    if (w->fade.modelp) DF_MOBJ_SetFadeOut(w->fade.modelp);
    ioObjp->stat = 2;
    ioObjp->count = 0;
}

void DF_MOBJ2_SetMdlWinFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct struct_f8cbbdcd *w = mobj2_mdlwin_of(ioObjp);
    for (int i = 0; i < 8; i++) {
        if (w->pWindow[i]) DF_MOBJ_SetFadeIn(w->pWindow[i]);
    }
    DF_MOBJ_SetFadeIn(w->cur.modelp);
    ioObjp->stat = 1;
    ioObjp->count = 0;
}

int DF_MOBJ2_MdlWinReadSync(struct stMobj *inObjp)
{
    PS2_FRAME(0x10);
    if (!inObjp) return 1;
    return DF_MOBJ_ModelReadSync(mobj2_mdlwin_of(inObjp)->cur.modelp);
}

struct stMobj *DF_MOBJ2_GetModelObjFromNexusNewMdlWin(struct stMobj *inObjp)
{
    return mobj2_mdlwin_of(inObjp)->cur.modelp;
}

/* a1 (callers pass 1 / 2) and the stack args at sp+0x20 / sp+0x28 are unused (dropped from the DWARF; protofix) */
struct stMobj *DF_MOBJ2_NexusNewMdlWin(struct stMobj *inObjp, int inUnused1, struct stMenuRect *inWinRectp,
                                       struct stMenuRect *inRectp, struct stMenuRect *inSprtRectp,
                                       struct struct_c4910010 *inPColp, struct struct_c4910010 *inPColp2,
                                       int inMdlKind, int inID, int inFlag, struct stMenuAC *pAC, int inPlayer,
                                       int inUnused2, int inUnused3, int InSceneType, float offset_cx,
                                       float offset_cy, char f_play_after_cleate, float scale)
{
    PS2_FRAME(0xb0);
    (void)inUnused1;
    (void)inUnused2;
    (void)inUnused3;
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    struct struct_f8cbbdcd *w = mobj2_mdlwin_of(obj);
    obj->type = 0;
    obj->play_func = (mobj_fn)GFN(__PlayMdlWin);
    w->rect = *inRectp;
    w->win_rect = *inWinRectp;
    w->sprt_rect = *inSprtRectp;
    w->f_csr_v = 1;
    w->f_csr_h = 1;
    w->player = (char)inPlayer;
    w->offset_cx = offset_cx;
    w->offset_cy = offset_cy;
    w->f_play_after_cleate = f_play_after_cleate;
    w->scale = scale;
    for (int i = 0; i < 8; i++) w->pWindow[i] = 0;
    w->cur.p_colp = inPColp;
    w->cur.p_colp2 = inPColp2;
    w->cur.modelp = DF_MOBJ_NewModelAC(obj, inMdlKind, inID, inFlag, &w->rect, w->cur.p_colp, w->cur.p_colp2, 0x80,
                                       MOBJ2_PI, pAC, w->player, InSceneType, 0.0f, 0.0f);
    if (w->f_play_after_cleate) {
        /* unlike SetMdlWinChange: no play_func call here */
        mobj2_model_of(w->cur.modelp)->dispflag |= 0x200000;
        mobj2_model_of(w->cur.modelp)->scale[0] = w->scale;
        mobj2_model_of(w->cur.modelp)->scale[1] = w->scale;
        mobj2_model_of(w->cur.modelp)->scale[2] = w->scale;
        mobj2_model_of(w->cur.modelp)->scale[3] = 1.0f;
    }
    mobj2_model_of(w->cur.modelp)->cx = offset_cx;
    mobj2_model_of(w->cur.modelp)->cy = offset_cy;
    return obj;
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper; 16 ints (8 registers + caller's sp+0 .. sp+0x38), floats in f12..f14 */
static void bw__DF_MOBJ2_NexusNewMdlWin(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    struct stMobj *r = DF_MOBJ2_NexusNewMdlWin(
        (struct stMobj *)bridge_ptr(c->gpr[A0].uw[0]), c->gpr[A1].sw[0],
        (struct stMenuRect *)bridge_ptr(c->gpr[A2].uw[0]), (struct stMenuRect *)bridge_ptr(c->gpr[A3].uw[0]),
        (struct stMenuRect *)bridge_ptr(c->gpr[T0].uw[0]), (struct struct_c4910010 *)bridge_ptr(c->gpr[T1].uw[0]),
        (struct struct_c4910010 *)bridge_ptr(c->gpr[T2].uw[0]), c->gpr[T3].sw[0], (int)rd32(sp), (int)rd32(sp + 8),
        (struct stMenuAC *)bridge_ptr(rd32(sp + 0x10)), (int)rd32(sp + 0x18), (int)rd32(sp + 0x20),
        (int)rd32(sp + 0x28), (int)rd32(sp + 0x30), c->fpr[12].f, c->fpr[13].f, (char)rd32(sp + 0x38),
        c->fpr[14].f);
    c->gpr[V0].sd[0] = (s32)PS2PTR(r);
}
#endif

/* ---------------------------------------------------------------- Item */

/* Item object data (no DWARF struct; layout written by DF_MOBJ2_NewItem) */
struct __attribute__((packed, aligned(4))) mobj2_item {
    struct stMenuRect rect;          /* 0x00 */
    struct stMenuRect line;          /* 0x10 underline rect */
    struct stMobj *pLabel[2];        /* 0x20 label, second label (types 2 / 3) */
    struct stMobj *pValue[2];        /* 0x28 value 1, value 2 (types 2 / 3) */
    struct stMobj *pOld[2];          /* 0x30 previous values while they fade out */
    struct struct_76b810ec colA;     /* 0x38 (copied, never drawn) */
    struct struct_76b810ec colB;     /* 0x3c */
    unsigned char _pad40[8];
    unsigned int depth;              /* 0x48 */
    unsigned char alpha;             /* 0x4c underline alpha */
    signed char place;               /* 0x4d digit count */
    signed char charType;            /* 0x4e */
    signed char type;                /* 0x4f 0 no value, 1 one value, 2 / 3 two values */
    unsigned char _pad50;
    signed char f_delete;            /* 0x51 */
};

static inline struct mobj2_item *mobj2_item_of(struct stMobj *o)
{
    return (struct mobj2_item *)&o->dt;
}

#define MOBJ2_ITEM_NOVALUE 0x30d3f   /* value sentinel: draw string 0x186d2 instead of a number */

struct mobj2_pts { int v[2][4]; };
struct mobj2_cols { struct struct_76b810ec c[2]; };
struct mobj2_cols4 { struct struct_76b810ec c[4]; };

static void __DrawItem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x60);
    PS2_STACK(struct mobj2_pts, pts, 0x10);
    PS2_STACK(struct mobj2_cols4, dead, 0x30);
    PS2_STACK(struct stMenuRect, r, 0x40);
    PS2_STACK(struct mobj2_cols, cols, 0x58);
    struct mobj2_item *it = mobj2_item_of(ioObjp);
    signed char f_help = MenuMng.f_help;

    /* quirk: a colour array (item colours, help-dimmed alpha) that is never used */
    for (int j = 0; j < 4; j++) {
        dead.c[j] = (&it->colA)[j / 2];
        dead.c[j].a = it->alpha;
        if (f_help == 1) dead.c[j].a = dead.c[j].a >> 1;
    }
    r = it->line;
    r.x += 1;
    r.y += 1;
    __builtin_memset(&pts, 0, sizeof(pts));
    pts.v[0][0] = r.x - 7;
    pts.v[0][1] = r.y + r.h;
    pts.v[1][0] = r.x + r.w - 7;
    pts.v[1][1] = r.y + r.h;
    mobj2_col(&cols.c[0], 0x80494949u);
    mobj2_col(&cols.c[1], 0x80494949u);
    cols.c[0].a = it->alpha >> 2;
    cols.c[1].a = it->alpha >> 2;
    DF_MDRAW_KickReg(0x42, 0x48);
    DF_MDRAW_DrawPrim(2, 2, pts.v, cols.c, 1.0f, 0);
}

static void __PlayItem(struct stMobj *ioObjp)
{
    struct mobj2_item *it = mobj2_item_of(ioObjp);

    if (it->f_delete == 1) {
        for (int i = 0; i < 2; i++) {
            if (it->pLabel[i]) { mobj2_erase(it->pLabel[i]); it->pLabel[i] = 0; }
            if (it->pValue[i]) { mobj2_erase(it->pValue[i]); it->pValue[i] = 0; }
            if (it->pOld[i]) { mobj2_erase(it->pOld[i]); it->pOld[i] = 0; }
        }
        if (ioObjp) mobj2_erase(ioObjp);
        return;
    }
    switch (ioObjp->stat) {
    case 0:
        break;
    case 1: {
        unsigned char old = ioObjp->count;
        ioObjp->count = old + 1;
        if (old < 6) {
            it->alpha = (int)(itemLineAlpha * ioObjp->count) / 6;
        } else {
            it->alpha = itemLineAlpha;
            ioObjp->stat = 0;
        }
        break;
    }
    case 2: {
        unsigned char old = ioObjp->count;
        ioObjp->count = old + 1;
        if (old < 6) {
            it->alpha = itemLineAlpha - (int)(ioObjp->count * itemLineAlpha) / 6;
        } else {
            it->alpha = 0;
            ioObjp->stat = 5;
        }
        break;
    }
    }
}

void DF_MOBJ2_DeleteItem(struct stMobj *ioObjp)
{
    if (ioObjp) mobj2_item_of(ioObjp)->f_delete = 1;
}

/* digit width and right-edge extra by char type */
static inline void mobj2_item_digit(int charType, int *w, int *extra)
{
    if (charType == 1) { *w = 0xe; *extra = 0; }
    else if (charType == 7) { *w = 8; *extra = 0; }
    else { *w = 0xc; *extra = 8; }
}

static void __SetItemValue2(struct stMobj *ioObjp, int inVal1, int inVal2, struct struct_76b810ec *col1,
                            struct struct_76b810ec *col2)
{
    PS2_FRAME(0xb0);
    struct mobj2_item *it = mobj2_item_of(ioObjp);
    int charType = it->charType;
    int w, extra;
    mobj2_item_digit(charType, &w, &extra);
    if (it->type == 0) return;

    int place = it->place;
    int rx = it->rect.x + it->rect.w;
    int y = it->rect.y;
    int x = extra + (rx - w * place);
    if (inVal1 == MOBJ2_ITEM_NOVALUE) {
        x = extra + (rx - w);
        char *s = (char *)DF_MSTR_GetName(MenuMng.language, 0, 0x186d2);
        it->pValue[0] = DF_MOBJ_NewStringDirect(ioObjp, s, charType, x, y, col1, it->depth, 0);
        DF_MOBJ_SetStringXFromRX(it->pValue[0], x + w);
    } else {
        it->pValue[0] = DF_MOBJ_NewNumber(ioObjp, inVal1, place, 0, charType, x, y, col1, it->depth);
    }
    if (it->type != 2 && it->type != 3) return;

    place = it->place;
    x = x - w - w * place;
    if (inVal2 == MOBJ2_ITEM_NOVALUE) {
        char *s = (char *)DF_MSTR_GetName(MenuMng.language, 0, 0x186d2);
        it->pValue[1] = DF_MOBJ_NewStringDirect(ioObjp, s, it->charType, x - 8, y, col2, it->depth, 0);
        DF_MOBJ_SetStringXFromRX(it->pValue[1], x - 8 + w);
    } else {
        it->pValue[1] = DF_MOBJ_NewNumber(ioObjp, inVal2, place, 0, it->charType, x - 8, y, col2, it->depth);
    }
}

static void __SetItemValue(struct stMobj *ioObjp, int inVal1, int inVal2)
{
    PS2_FRAME(0xa0);
    PS2_STACK(struct struct_76b810ec, col, 0x9c);
    mobj2_col(&col, 0x80909090u);
    struct mobj2_item *it = mobj2_item_of(ioObjp);
    int charType = it->charType;
    int w, extra;
    mobj2_item_digit(charType, &w, &extra);
    if (it->type == 0) return;

    int place = it->place;
    int rx = it->rect.x + it->rect.w;
    int y = it->rect.y;
    int x = extra + (rx - w * place);
    if (inVal1 == MOBJ2_ITEM_NOVALUE) {
        x = extra + (rx - w);
        char *s = (char *)DF_MSTR_GetName(MenuMng.language, 0, 0x186d2);
        it->pValue[0] = DF_MOBJ_NewStringDirect(ioObjp, s, charType, x, y, &col, it->depth, 0);
        DF_MOBJ_SetStringXFromRX(it->pValue[0], x + w);
    } else {
        it->pValue[0] = DF_MOBJ_NewNumber(ioObjp, inVal1, place, 0, charType, x, y, &col, it->depth);
    }
    if (it->type != 2 && it->type != 3) return;

    place = it->place;
    x = x - w - w * place;
    if (inVal2 == MOBJ2_ITEM_NOVALUE) {
        char *s = (char *)DF_MSTR_GetName(MenuMng.language, 0, 0x186d2);
        it->pValue[1] = DF_MOBJ_NewStringDirect(ioObjp, s, it->charType, x - 8, y, &col, it->depth, 0);
        DF_MOBJ_SetStringXFromRX(it->pValue[1], x - 8 + w);
    } else {
        it->pValue[1] = DF_MOBJ_NewNumber(ioObjp, inVal2, place, 0, it->charType, x - 8, y, &col, it->depth);
    }
}

void DF_MOBJ2_ChangeItemParamEx(struct stMobj *ioObjp, int inVal1, int inVal2, struct struct_76b810ec *color1,
                                struct struct_76b810ec *color2)
{
    PS2_FRAME(0x90);
    if (!ioObjp) return;
    struct mobj2_item *it = mobj2_item_of(ioObjp);
    for (int i = 0; i < 2; i++) {
        if (it->pOld[i]) {
            mobj2_erase(it->pOld[i]);
            it->pOld[i] = 0;
        }
        it->pOld[i] = it->pValue[i];
        DF_MOBJ_SetFadeOut(it->pOld[i]);
    }
    __SetItemValue2(ioObjp, inVal1, inVal2, color1, color2);
    for (int i = 0; i < 2; i++) DF_MOBJ_SetFadeIn(it->pValue[i]);
}

void DF_MOBJ2_ChangeItemParam(struct stMobj *ioObjp, int inVal1, int inVal2)
{
    PS2_FRAME(0x20);
    PS2_STACK(struct struct_76b810ec, col, 0x1c);
    mobj2_col(&col, 0x80968c5fu);
    DF_MOBJ2_ChangeItemParamEx(ioObjp, inVal1, inVal2, &col, &col);
}

void DF_MOBJ2_SetItemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    if (!ioObjp) return;
    struct mobj2_item *it = mobj2_item_of(ioObjp);
    DF_MOBJ_SetFadeOut(it->pLabel[0]);
    DF_MOBJ_SetFadeOut(it->pValue[0]);
    if (it->type == 2 || it->type == 3) {
        DF_MOBJ_SetFadeOut(it->pLabel[1]);
        DF_MOBJ_SetFadeOut(it->pValue[1]);
    }
    ioObjp->count = 0;
    ioObjp->stat = 2;
}

void DF_MOBJ2_SetItemFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    if (!ioObjp) return;
    struct mobj2_item *it = mobj2_item_of(ioObjp);
    DF_MOBJ_SetFadeIn(it->pLabel[0]);
    DF_MOBJ_SetFadeIn(it->pValue[0]);
    if (it->type == 2 || it->type == 3) {
        DF_MOBJ_SetFadeIn(it->pLabel[1]);
        DF_MOBJ_SetFadeIn(it->pValue[1]);
    }
    ioObjp->count = 0;
    ioObjp->stat = 1;
}

struct stMobj *DF_MOBJ2_NewItem(struct stMobj *inObjp, struct stMenuRect *inRectp, int inType, int inDir,
                                int inCharType, int inStrID, int inPlace, int inVal1, int inVal2, unsigned int inDepth)
{
    PS2_FRAME(0xd0);
    PS2_STACK(struct mobj2_cols, lcol, 0xc0);
    PS2_STACK(struct struct_76b810ec, scol, 0xcc);
    mobj2_col(&scol, 0x80909090u);
    mobj2_col(&lcol.c[0], 0x803e64bcu);
    mobj2_col(&lcol.c[1], 0x8063596bu);

    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    struct mobj2_item *it = mobj2_item_of(obj);
    obj->type = 0;
    it->rect = *inRectp;
    it->type = (signed char)inType;
    it->depth = inDepth;
    it->place = (signed char)inPlace;
    it->charType = (signed char)inCharType;
    /* quirk: dir only swaps two colours that are never drawn */
    if (inDir == 4) {
        it->colA = lcol.c[0];
        it->colB = lcol.c[1];
    } else {
        it->colA = lcol.c[1];
        it->colB = lcol.c[0];
    }
    it->pLabel[0] = DF_MOBJ_NewString(obj, 0, inStrID, 0, inCharType, it->rect.x, it->rect.y, &scol, inDepth, 0);

    int dy, w, off;
    if (inCharType == 1) { dy = 8; w = 0xe; off = 0; }
    else if (inCharType == 7) { dy = 6; w = 8; off = -7; }
    else { dy = 6; w = 0xc; off = -4; }
    it->line = *inRectp;
    it->line.x += 8;
    it->line.y += dy;
    it->line.w -= 2;
    it->line.h = 8;
    if (it->type == 2) {
        it->pLabel[1] = DF_MOBJ_NewString(obj, 0, 0x186d3, 0, inCharType,
                                          off + ((it->rect.x + it->rect.w) - w * (inPlace + 1)), it->rect.y, &scol,
                                          inDepth, 0);
    } else if (it->type == 3) {
        it->pLabel[1] = DF_MOBJ_NewString(obj, 0, 0x186d7, 0, inCharType,
                                          off + ((it->rect.x + it->rect.w) - w * (inPlace + 1)), it->rect.y, &scol,
                                          inDepth, 0);
    }
    __SetItemValue(obj, inVal1, inVal2);
    obj->play_func = (mobj_fn)GFN(__PlayItem);
    obj->draw_func = (mobj_fn)GFN(__DrawItem);
    return obj;
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper; inVal2 / inDepth are the caller's stack args at sp+0 / sp+8 */
static void bw__DF_MOBJ2_NewItem(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    struct stMobj *r = DF_MOBJ2_NewItem(
        (struct stMobj *)bridge_ptr(c->gpr[A0].uw[0]), (struct stMenuRect *)bridge_ptr(c->gpr[A1].uw[0]),
        c->gpr[A2].sw[0], c->gpr[A3].sw[0], c->gpr[T0].sw[0], c->gpr[T1].sw[0], c->gpr[T2].sw[0], c->gpr[T3].sw[0],
        (int)rd32(sp), rd32(sp + 8));
    c->gpr[V0].sd[0] = (s32)PS2PTR(r);
}
#endif
