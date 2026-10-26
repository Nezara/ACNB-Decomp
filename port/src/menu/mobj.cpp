/* src/menu/mobj.cpp — the menu's object system (OLMENU): sprites, strings, numbers, windows and 3D models in a fixed
 * array of 1280 stMobj slots (MenuObjTopp .. MenuObjEndp, 0x140 bytes each). Every menu frame DF_MOBJ_Play runs the
 * play function of each used slot (and erases the slots flagged f_erase), DF_MOBJ_MakeVu1Paket(2ND) the packet
 * function of the model slots and DF_MOBJ_Draw / Draw_2ND the draw function of each slot in four priority passes
 * (types 7 / 9 / 10 - the mobj2.c variants - are drawn by the 2ND pass). The New* constructors take a slot with
 * DF_MOBJ_GetNewObject (children after their parent, so they play / draw after it) and store the type's callbacks;
 * SetFadeIn / SetFadeOut start the 6-frame fade tweens the play functions run.
 *
 * Models (types 5 / 8) load their MMD in __MOBJ_PlayModel, are set up by __MOBJ_SetModelInfo (AC parts: colours,
 * animations, visible sub-models, the hand model of arm parts) and attach the hand to the arm every frame
 * (__MOBJ_PlayModelSub*Arm, inline VU0 matrix products in the original). */
#include "acnb/hooks.h"
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/vu0m.h"

typedef void (*mobj_fn)(void);

/* the object's callbacks get the object (the DWARF type of the stored pointers is void (*)(void)) */
static inline void mobj_call(mobj_fn fp, struct stMobj *obj)
{
    gcall_as<void(struct stMobj *)>(fp, obj);
}

void DF_MOBJ_MakeVu1Paket2ND(void)
{
    PS2_FRAME(0x20);
    for (struct stMobj *p = MenuObjTopp; p != MenuObjEndp; p++)
        if (p->f_use && p->type == 8 && p->f_draw == 1 && p->pkt_func) mobj_call(p->pkt_func, p);
}

void DF_MOBJ_MakeVu1Paket(void)
{
    PS2_FRAME(0x20);
    for (struct stMobj *p = MenuObjTopp; p != MenuObjEndp; p++)
        if (p->f_use && p->type != 8 && p->f_draw == 1 && p->pkt_func) mobj_call(p->pkt_func, p);
}

/* GS state for the menu draw passes: TEST_1 = 0x3000f, ALPHA_1 = 0x44, CLAMP_1 = 0 (A+D through the scratchpad) */
static void mobj_kick_draw_state(void)
{
    unsigned int *p = DF_MDRAW_GetSPRBuff();
    p[0] = 0x8003; p[1] = 0x10000000; p[2] = 0xe; p[3] = 0;
    unsigned long long *ad = (unsigned long long *)&p[4];
    ad[0] = 0x3000f; ad[1] = 0x47;
    ad[2] = 0x44; ad[3] = 0x42;
    ad[4] = 0; ad[5] = 8;
    DF_MDRAW_GIFKickSPR(p + 16);
}

static inline int mobj_is_2nd(const struct stMobj *p)
{
    return p->type == 7 || p->type == 9 || p->type == 10;
}

void DF_MOBJ_Draw_2ND(void)
{
    PS2_FRAME(0x40);
    mobj_kick_draw_state();
    for (unsigned char prio = 0; prio < 4; prio++)
        for (struct stMobj *p = MenuObjTopp; p != MenuObjEndp; p++)
            if (p->f_use && mobj_is_2nd(p) && p->draw_prio == prio && p->f_draw == 1 && p->draw_func)
                mobj_call(p->draw_func, p);
}

void DF_MOBJ_Draw(void)
{
    PS2_FRAME(0x40);
    PS2_RQ_SCOPE(RQ_K_MENU, 0, 0);   /* render tag (host only) */
    mobj_kick_draw_state();
    for (unsigned char prio = 0; prio < 4; prio++)
        for (struct stMobj *p = MenuObjTopp; p != MenuObjEndp; p++)
            if (p->f_use && !mobj_is_2nd(p) && p->draw_prio == prio && p->f_draw == 1 && p->draw_func)
                mobj_call(p->draw_func, p);
}

/* returns the number of live (used, not erased) objects - counted whether or not they have a play function (the
 * increment sits in the branch delay slot of the play_func null test) */
int DF_MOBJ_Play(void)
{
    int n = 0;

    PS2_FRAME(0x30);
    for (struct stMobj *p = MenuObjTopp; p != MenuObjEndp; p++) {
        if (!p->f_use) continue;
        if (p->f_erase == 1) {
            if (p->type == 5 || p->type == 8) __MOBJ_DeleteModel(p);
            DF_MFUNC_ClearMemory(p, 0x50);   /* words: the whole slot */
        } else {
            n++;
            if (p->play_func) mobj_call(p->play_func, p);
        }
    }
    return n;
}

/* ---------------------------------------------------------------- draw functions (the draw_func callbacks) */
/* They only compute colours / rects / corners / font records; the GS work is in the interpreted DF_MDRAW_* (mdraw.c)
 * and the native DF_MFONT_Draw. Every local handed to a callee sits at its original stack slot (PS2_STACK). */

/* menu help screen: objects without f_help are drawn at half alpha */
static inline void mobj_help_dim(const struct stMobj *obj, struct struct_76b810ec *col)
{
    if (MenuMng.f_help == 1 && !obj->f_help) col->a >>= 1;
}

/* per-byte (c * mul) >> 7, stored as a byte without clamping */
static inline void mobj_col_mul(struct struct_76b810ec *col, const struct struct_76b810ec *mul)
{
    col->r = (unsigned char)((col->r * mul->r) >> 7);
    col->g = (unsigned char)((col->g * mul->g) >> 7);
    col->b = (unsigned char)((col->b * mul->b) >> 7);
    col->a = (unsigned char)((col->a * mul->a) >> 7);
}

/* f_linear: TEX1 MMAG = MMIN = 1 (bilinear) on the stack copy of the texinfo */
static inline void mobj_tex_linear(struct struct_13efd7f4 *tex)
{
    tex->tex1.mmag = 1;
    tex->tex1.mmin = 1;
}

/* CLAMP_1 = REGION_CLAMP to the sprite's uv rect (MINU uv0, MAXU uv2 - 1, MINV uv1, MAXV uv3 - 1; sign-extended) */
static inline unsigned long long mobj_clamp(const int *uv)
{
    return (unsigned long long)(long long)uv[0] << 4 | 0xa | (unsigned long long)(long long)(uv[2] - 1) << 14 |
           (unsigned long long)(long long)uv[1] << 24 | (unsigned long long)(long long)(uv[3] - 1) << 34;
}

#define MOBJ_K_ALPHA 0x42
#define MOBJ_K_TEST 0x47
#define MOBJ_TEST 0x3000f    /* ZTST GEQUAL */
#define MOBJ_TESTL 0x7000f   /* ZTST GREATER */

/* uv rects of window types 4-7 (OLMENU rodata 0x1edf5c0 .. 0x1edf610) */
static const int mobj_win_uv[6][4] = {
    {100, 54, 136, 72}, {177, 101, 233, 127}, {2, 56, 66, 88}, {136, 71, 154, 89}, {151, 101, 177, 127},
    {68, 56, 100, 88},
};

static void __MOBJ_DrawWindow(struct stMobj *inObjp)
{
    PS2_FRAME(0x100);
    PS2_RQ_SCOPE(RQ_K_MENU, RQ_OBJID(RQ_OC_MENU, PS2PTR(inObjp)), 1);   /* render tag (host only) */
    PS2_STACK(struct struct_13efd7f4, tex, 0xe0);
    PS2_STACK(struct struct_13efd7f4, tex_vs, 0x50);
    PS2_STACK(struct struct_13efd7f4, tex_lvs, 0x40);
    PS2_STACK(struct struct_76b810ec, col, 0xfc);
    PS2_STACK(struct stMenuRect, rect4, 0xc0);
    PS2_STACK(struct stMenuRect, rect6, 0x80);
    PS2_STACK(int[4], uv4a, 0xd0);
    PS2_STACK(int[4], uv4b, 0xb0);
    PS2_STACK(int[4], uv5, 0xa0);
    PS2_STACK(int[4], uv6a, 0x90);
    PS2_STACK(int[4], uv6b, 0x70);
    PS2_STACK(int[4], uv7, 0x60);
    struct struct_c6a67b40 *w = &inObjp->dt.window;
    struct stMenuRect *r = &w->cur_rect;

    MenuTex_StandbyTexture(0x3ec, &tex);   /* the window parts texture; result ignored */
    col = w->cur_col;
    mobj_help_dim(inObjp, &col);
    if (!col.a) return;
    switch (w->type) {
    case 0: case 10: case 11: case 12:
        DF_MDRAW_KickReg(MOBJ_K_ALPHA, 0x44);
        DF_MDRAW_KickReg(MOBJ_K_TEST, MOBJ_TEST);
        DF_MDRAW_DrawBlackWindow(w->shape, r, &col, w->depth);
        break;
    case 13:
        DF_MDRAW_KickReg(MOBJ_K_ALPHA, 0x48);
        DF_MDRAW_KickReg(MOBJ_K_TEST, MOBJ_TESTL);
        DF_MDRAW_DrawLightWindow2(r, &col, w->depth);
        DF_MDRAW_KickReg(MOBJ_K_TEST, MOBJ_TEST);
        break;
    case 1: case 18: case 19:
        DF_MDRAW_KickReg(MOBJ_K_TEST, MOBJ_TEST);
        DF_MDRAW_NexusDrawTagVertex4(w->shape, r, &col, w->depth, w->uv, w->tex_index, w->blend_value);
        break;
    case 2:   /* the undimmed cur_col */
        DF_MDRAW_KickReg(MOBJ_K_ALPHA, 0x44);
        DF_MDRAW_KickReg(MOBJ_K_TEST, MOBJ_TEST);
        DF_MDRAW_NexusDrawGeneralWindow(r, &w->cur_col, w->depth, w->uv);
        break;
    case 3:
        DF_MDRAW_KickReg(MOBJ_K_ALPHA, 0x48);
        DF_MDRAW_KickReg(MOBJ_K_TEST, MOBJ_TESTL);
        DF_MDRAW_DrawLightWindow(r, &col, w->depth);
        DF_MDRAW_KickReg(MOBJ_K_TEST, MOBJ_TEST);
        break;
    case 4:
        DF_MDRAW_KickReg(MOBJ_K_TEST, MOBJ_TEST);
        DF_MDRAW_KickReg(MOBJ_K_ALPHA, MOBJ_K_ALPHA);
        memcpy(uv4a, mobj_win_uv[0], 16);
        rect4 = *r;
        rect4.w = 0x3c;
        rect4.h = 0x1e;
        rect4.x -= 2;
        rect4.y -= 2;
        DF_MDRAW_DrawTexSprite(&tex, &rect4, uv4a, &col, w->depth);
        DF_MDRAW_KickReg(MOBJ_K_ALPHA, 0x44);
        memcpy(uv4b, mobj_win_uv[1], 16);
        DF_MDRAW_DrawTexSprite(&tex, r, uv4b, &col, w->depth);
        break;
    case 5:
        DF_MDRAW_KickReg(MOBJ_K_TEST, MOBJ_TESTL);
        DF_MDRAW_KickReg(MOBJ_K_ALPHA, 0x48);
        memcpy(uv5, mobj_win_uv[2], 16);
        DF_MDRAW_DrawTexSprite(&tex, r, uv5, &col, w->depth);
        break;
    case 6:
        DF_MDRAW_KickReg(MOBJ_K_TEST, MOBJ_TEST);
        DF_MDRAW_KickReg(MOBJ_K_ALPHA, MOBJ_K_ALPHA);
        memcpy(uv6a, mobj_win_uv[3], 16);
        rect6 = *r;
        rect6.w = 0x1e;
        rect6.h = 0x1e;
        rect6.x -= 2;
        rect6.y -= 2;
        DF_MDRAW_DrawTexSprite(&tex, &rect6, uv6a, &col, w->depth);
        DF_MDRAW_KickReg(MOBJ_K_ALPHA, 0x44);
        memcpy(uv6b, mobj_win_uv[4], 16);
        DF_MDRAW_DrawTexSprite(&tex, r, uv6b, &col, w->depth);
        break;
    case 7:
        DF_MDRAW_KickReg(MOBJ_K_ALPHA, 0x48);
        DF_MDRAW_KickReg(MOBJ_K_TEST, MOBJ_TESTL);
        memcpy(uv7, mobj_win_uv[5], 16);
        DF_MDRAW_DrawTexSprite(&tex, r, uv7, &col, w->depth);
        break;
    case 8:
        DF_MDRAW_KickReg(MOBJ_K_ALPHA, 0x44);
        DF_MDRAW_KickReg(MOBJ_K_TEST, MOBJ_TEST);
        DF_MDRAW_DrawGeneralWindowRE(r, &col, w->depth);
        break;
    case 9:
        DF_MDRAW_KickReg(MOBJ_K_ALPHA, 0x48);
        DF_MDRAW_KickReg(MOBJ_K_TEST, MOBJ_TESTL);
        DF_MDRAW_DrawLightWindowRE(r, &col, w->depth);
        DF_MDRAW_KickReg(MOBJ_K_TEST, MOBJ_TEST);
        break;
    case 14:
        DF_MDRAW_KickReg(MOBJ_K_ALPHA, 0x44);
        DF_MDRAW_KickReg(MOBJ_K_TEST, MOBJ_TEST);
        DF_MDRAW_DrawGeneralWindowRE2(r, &col, w->depth);
        break;
    case 15:
        DF_MDRAW_KickReg(MOBJ_K_ALPHA, 0x48);
        DF_MDRAW_KickReg(MOBJ_K_TEST, MOBJ_TESTL);
        DF_MDRAW_DrawLightWindowRE2(r, &col, w->depth);
        DF_MDRAW_KickReg(MOBJ_K_TEST, MOBJ_TEST);
        break;
    case 16:   /* a missing texture draws nothing */
        DF_MDRAW_KickReg(MOBJ_K_ALPHA, 0x44);
        DF_MDRAW_KickReg(MOBJ_K_TEST, MOBJ_TEST);
        if (w->tex_info) DF_MDRAW_DrawGeneralWindowVS(w->tex_info, r, &col, w->depth);
        else if (MenuTex_StandbyTexture(w->tex_index, &tex_vs)) DF_MDRAW_DrawGeneralWindowVS(&tex_vs, r, &col, w->depth);
        break;
    case 17:   /* the TEST reset follows even without a texture */
        DF_MDRAW_KickReg(MOBJ_K_ALPHA, 0x48);
        DF_MDRAW_KickReg(MOBJ_K_TEST, MOBJ_TESTL);
        if (w->tex_info) DF_MDRAW_DrawLightWindowVS(w->tex_info, r, &col, w->depth);
        else if (MenuTex_StandbyTexture(w->tex_index, &tex_lvs)) DF_MDRAW_DrawLightWindowVS(&tex_lvs, r, &col, w->depth);
        DF_MDRAW_KickReg(MOBJ_K_TEST, MOBJ_TEST);
        break;
    case 0x65:
        DF_MDRAW_KickReg(MOBJ_K_ALPHA, 0x44);
        DF_MDRAW_DrawSimpleLabelR(r, &col, 0);
        break;
    case 0x66:
        DF_MDRAW_KickReg(MOBJ_K_ALPHA, 0x44);
        DF_MDIALOG_DrawDialogWnd(r, &col);
        break;
    case 0x6d:
        DF_MDRAW_KickReg(MOBJ_K_ALPHA, 0x44);
        DF_MDRAW_DrawM1Help(r, &col, 0);
        break;
    }
}

/* the number is formatted into a static buffer; h_per (the fade) squeezes the glyph height around the centre line
 * (y moves down by half the lost height) */
static void __MOBJ_DrawNumber(struct stMobj *inObjp)
{
    PS2_FRAME(0x280);
    PS2_STACK(struct struct_acb9a91f, font, 0x50);
    struct struct_fd14677b *n = &inObjp->dt.number;
    int half, hp;

    DF_MFUNC_ClearMemory(&font, 0x8a);   /* words */
    DF_MFUNC_Itoa(__MOBJ_DrawNumber__wk, n->num, n->sign, n->place);
    font.strp = (unsigned char *)__MOBJ_DrawNumber__wk;
    font.x = (short)n->x;
    font.y = (short)n->y;
    font.col = n->cur_col;
    if (!font.col.a) return;
    font.depth = n->depth;
    font.flag = n->flag;
    DF_MFONT_SetFontAttr(&font, n->char_type);
    half = font.h / 2;
    hp = n->h_per;
    font.y = (short)(font.y + half * (100 - hp) / 100);
    font.space_w = (char)font.off_x;
    font.h = (short)(hp * font.h / 100);
    mobj_help_dim(inObjp, &font.col);
    DF_MFONT_Draw(&font, 1);
}

/* fades (stat 1 / 2) use the font's typewriter window fade_cnt .. fade_cnt + 3; flag 0x10000 right-aligns at x,
 * 0x20000 centres */
static void __MOBJ_DrawString(struct stMobj *inObjp)
{
    PS2_FRAME(0x290);
    PS2_STACK(struct struct_acb9a91f, font, 0x60);
    struct struct_d2c56d2a *s = &inObjp->dt.string;
    unsigned char *str;

    DF_MFUNC_ClearMemory(&font, 0x8a);   /* words */
    if (s->ref_type == 0) str = DF_MSTR_GetName(MenuMng.language, s->category, s->id);
    else if (s->ref_type == 1) str = DF_MSTR_GetInfo(MenuMng.language, s->category, s->id);
    else if (s->ref_type == 2) str = (unsigned char *)s->strp;
    else str = 0;
    font.strp = str;
    if (!font.strp) return;
    font.x = s->x;
    font.y = s->y;
    font.col = s->cur_col;
    if (inObjp->stat == 1 || inObjp->stat == 2) {
        font.fadebegin = s->fade_cnt;
        font.fadeend = s->fade_cnt + 3;
    }
    if (!font.col.a) return;
    font.depth = s->depth;
    font.flag = s->flag;   /* the original then ORs in 0x800 when flag & 0x800: no-op */
    DF_MFONT_SetFontAttr(&font, s->char_type);
    mobj_help_dim(inObjp, &font.col);
    if (s->flag & 0x10000)
        font.x -= (short)DF_MFONT_GetStringWSize(font.strp, font.type, font.flag);
    else if (s->flag & 0x20000)
        font.x -= (short)(DF_MFONT_GetStringWSize(font.strp, font.type, font.flag) / 2);
    DF_MFONT_Draw(&font, 1);
}

void __MOBJ_DrawSpriteOld(struct stMobj *inObjp)
{
    PS2_FRAME(0x50);
    PS2_STACK(struct struct_13efd7f4, tex, 0x30);
    PS2_STACK(struct struct_76b810ec, col, 0x4c);
    struct struct_37ce77ad *sp = &inObjp->dt.sprite;

    DF_MDRAW_KickReg(MOBJ_K_ALPHA, sp->reg_alpha);
    col = sp->cur_col;
    mobj_help_dim(inObjp, &col);
    if (!sp->texp) {
        DF_MDRAW_DrawColSprite(&sp->rect, 1, &col, sp->depth);
        return;
    }
    tex = *sp->texp;
    if (sp->f_linear == 1) mobj_tex_linear(&tex);
    if (sp->uv_dir == 0) DF_MDRAW_DrawTexSpriteOld(&tex, &sp->rect, sp->uv, &col, sp->depth);
    else DF_MDRAW_DrawTexSprite2(&tex, &sp->rect, sp->uv, &col, sp->depth);
}

/* the sprite draw: rect moved by poffset, colour scaled by pmul (optional); draw_mode 0 = plain (uv_dir != 0: the
 * DrawTexSprite2 variant), 1 = gradient cur_col -> sub_col (sub_col takes cur_col's alpha), others draw nothing */
static void __MOBJ_DrawSpriteOffset(struct stMobj *pMobj, struct struct_214acd28 *poffset,
                                    struct struct_76b810ec *pmul)
{
    PS2_FRAME(0x90);
    PS2_STACK(struct struct_76b810ec[4], cols, 0x50);
    PS2_STACK(struct stMenuRect, rect, 0x60);
    PS2_STACK(struct struct_13efd7f4, tex, 0x70);
    PS2_STACK(struct struct_76b810ec, col, 0x8c);
    struct struct_37ce77ad *sp = &pMobj->dt.sprite;

    DF_MDRAW_KickReg(MOBJ_K_ALPHA, sp->reg_alpha);
    col = sp->cur_col;
    if (pmul) mobj_col_mul(&col, pmul);
    mobj_help_dim(pMobj, &col);
    if (!sp->texp) {
        DF_MDRAW_DrawColSprite(&sp->rect, 1, &col, sp->depth);
        return;
    }
    rect = sp->rect;
    rect.x += poffset->x;
    rect.y += poffset->y;
    tex = *sp->texp;
    if (sp->f_linear == 1) mobj_tex_linear(&tex);
    if (sp->flag & 1) DF_MDRAW_KickReg(8, mobj_clamp(sp->uv));
    if (sp->draw_mode == 0) {
        if (sp->uv_dir == 0) DF_MDRAW_DrawTexSprite(&tex, &rect, sp->uv, &col, sp->depth);
        else DF_MDRAW_DrawTexSprite2(&tex, &rect, sp->uv, &col, sp->depth);
    } else if (sp->draw_mode == 1) {
        cols[0] = col;
        cols[1] = col;
        cols[2] = sp->sub_col;
        cols[3] = sp->sub_col;
        cols[3].a = cols[0].a;
        cols[2].a = cols[0].a;
        DF_MDRAW_DrawTexSprite4Colors(&tex, &rect, sp->uv, cols, sp->depth);
    }
    if (sp->flag & 1) DF_MDRAW_KickReg(8, 0);
}

/* rotated / scaled sprite (DrawTexSprite4 with four corner points around the rect centre) */
void __MOBJ_DrawSpriteOffsetSclAng(struct stMobj *pMobj, struct struct_214acd28 *poffset,
                                   struct struct_76b810ec *pmul, float scl, float ang)
{
    PS2_FRAME(0xd0);
    PS2_STACK(struct struct_0d65143e[4], pos, 0x60);
    PS2_STACK(struct struct_839bfa8e[4], uv, 0x80);
    PS2_STACK(struct stMenuRect, rect, 0xa0);
    PS2_STACK(struct struct_13efd7f4, tex, 0xb0);
    PS2_STACK(struct struct_76b810ec, col, 0xcc);
    struct struct_37ce77ad *sp = &pMobj->dt.sprite;
    int hw, hh, nw, nh;
    float cx, cy, sn, cs;

    DF_MDRAW_KickReg(MOBJ_K_ALPHA, sp->reg_alpha);
    col = sp->cur_col;
    if (pmul) mobj_col_mul(&col, pmul);
    mobj_help_dim(pMobj, &col);
    if (!sp->texp) {
        DF_MDRAW_DrawColSprite(&sp->rect, 1, &col, sp->depth);
        return;
    }
    rect = sp->rect;
    rect.x += poffset->x;
    rect.y += poffset->y;
    tex = *sp->texp;
    if (sp->f_linear == 1) mobj_tex_linear(&tex);
    if (sp->flag & 1) DF_MDRAW_KickReg(8, mobj_clamp(sp->uv));
    hw = rect.w / 2;
    hh = rect.h / 2;
    nw = -rect.w / 2;
    nh = -rect.h / 2;
    cx = (float)(rect.x + hw);
    cy = (float)(rect.y + hh);
    pos[0].x = (float)nw; pos[0].y = (float)nh;
    pos[1].x = (float)hw; pos[1].y = (float)nh;
    pos[2].x = (float)nw; pos[2].y = (float)hh;
    pos[3].x = (float)hw; pos[3].y = (float)hh;
    sn = ps2_sinf(ang);
    cs = ps2_cosf(ang);
    for (int i = 0; i < 4; i++) {
        float x, y, a, b;
        pos[i].x = pos[i].x * scl;
        pos[i].y = pos[i].y * scl;
        x = pos[i].x;
        y = pos[i].y;
        a = cs * x;   /* mula.s / msub.s: products rounded, then subtracted / added */
        b = sn * y;
        pos[i].x = (a - b) + cx;
        a = sn * x;
        b = cs * y;
        pos[i].y = (a + b) + cy;
    }
    uv[0].u = sp->uv[0]; uv[0].v = sp->uv[1];
    uv[1].u = sp->uv[2]; uv[1].v = sp->uv[1];
    uv[2].u = sp->uv[0]; uv[2].v = sp->uv[3];
    uv[3].u = sp->uv[2]; uv[3].v = sp->uv[3];
    DF_MDRAW_DrawTexSprite4(&tex, &col, uv, pos, sp->depth, 3);
    if (sp->flag & 1) DF_MDRAW_KickReg(8, 0);
}

/* the texid wrappers: sprites without a texinfo pointer look their texture up for this draw only (texp points at the
 * stack copy during the call); a missing texture draws nothing */
void DF_MOBJ_DrawSpriteOffset(struct stMobj *pMobj, struct struct_214acd28 *poffset, struct struct_76b810ec *pmul)
{
    PS2_FRAME(0x60);
    PS2_STACK(struct struct_13efd7f4, tex, 0x50);
    struct struct_37ce77ad *sp = &pMobj->dt.sprite;

    if (sp->texp) {
        __MOBJ_DrawSpriteOffset(pMobj, poffset, pmul);
        return;
    }
    if (sp->texid != -1) {
        if (!MenuTex_StandbyTexture(sp->texid, &tex)) return;
        sp->texp = &tex;
    } else {
        sp->texp = 0;
    }
    __MOBJ_DrawSpriteOffset(pMobj, poffset, pmul);
    sp->texp = 0;
}

void __MOBJ_DrawSpriteOffsetScaleAng(struct stMobj *pMobj, struct struct_214acd28 *poffset,
                                     struct struct_76b810ec *pmul, float scl, float ang)
{
    PS2_FRAME(0x70);
    PS2_STACK(struct struct_13efd7f4, tex, 0x60);
    struct struct_37ce77ad *sp = &pMobj->dt.sprite;

    if (sp->texp) {
        __MOBJ_DrawSpriteOffsetSclAng(pMobj, poffset, pmul, scl, ang);
        return;
    }
    if (sp->texid != -1) {
        if (!MenuTex_StandbyTexture(sp->texid, &tex)) return;
        sp->texp = &tex;
    } else {
        sp->texp = 0;
    }
    __MOBJ_DrawSpriteOffsetSclAng(pMobj, poffset, pmul, scl, ang);
    sp->texp = 0;
}

/* DrawSprite (NewSprite) and DrawSpriteNX (NXNewSprite) have identical bodies: zero offset, no colour scale
 * (called inside the caller's 0x50 frame) */
static void mobj_draw_sprite_plain(struct stMobj *inObjp)
{
    PS2_STACK(struct struct_13efd7f4, tex, 0x30);
    PS2_STACK(struct struct_214acd28, zero, 0x48);
    struct struct_37ce77ad *sp = &inObjp->dt.sprite;

    memset(&zero, 0, sizeof zero);
    if (sp->texp) {
        __MOBJ_DrawSpriteOffset(inObjp, &zero, 0);
        return;
    }
    if (sp->texid != -1) {
        if (!MenuTex_StandbyTexture(sp->texid, &tex)) return;
        sp->texp = &tex;
    } else {
        sp->texp = 0;
    }
    __MOBJ_DrawSpriteOffset(inObjp, &zero, 0);
    sp->texp = 0;
}

void __MOBJ_DrawSprite(struct stMobj *inObjp)
{
    PS2_FRAME(0x50);
    mobj_draw_sprite_plain(inObjp);
}

static void __MOBJ_DrawSpriteNX(struct stMobj *inObjp)
{
    PS2_FRAME(0x50);
    mobj_draw_sprite_plain(inObjp);
}

static void __MOBJ_SetWindowFadeOut(struct stMobj *ioObjp)
{
    struct struct_c6a67b40 *w = &ioObjp->dt.window;

    PS2_FRAME(0x20);
    if (w->type == 0x66) {
        w->prev_rect = w->cur_rect;
        DF_MFUNC_GetFadeOutRect(&w->next_rect, &w->prev_rect);
    } else if (w->type == 0x65) {
        w->next_rect = w->cur_rect;
        w->prev_rect = w->next_rect;
        w->next_rect.x -= 400;
    } else {
        w->cur_rect = w->init_rect;
        w->next_rect = w->cur_rect;
        w->prev_rect = w->next_rect;
    }
    w->prev_col = w->cur_col;
    w->next_col = w->cur_col;
    w->next_col.a = 0;
    w->fade_max = ioObjp->stat == 1 ? w->fade_cc : 6;
    w->fade_cc = 0;
    ioObjp->stat = 2;
    ioObjp->play_func = (mobj_fn)GFN(__MOBJ_PlayWindow);
}

static void __MOBJ_SetWindowFadeIn(struct stMobj *ioObjp)
{
    struct struct_c6a67b40 *w = &ioObjp->dt.window;

    PS2_FRAME(0x20);
    w->fade_max = 6;
    if (w->type == 0x66) {
        if (w->cur_rect.h < 3) {
            w->next_rect = w->init_rect;
        } else {
            w->next_rect = w->cur_rect;
            w->init_rect = w->cur_rect;
        }
        DF_MFUNC_GetFadeInRect(&w->prev_rect, &w->next_rect);
    } else if (w->type == 0x65) {
        w->next_rect = w->init_rect;
        w->cur_rect = w->next_rect;
        w->cur_rect.x -= 300;
        w->prev_rect = w->cur_rect;
    } else {
        w->cur_rect = w->init_rect;
        w->next_rect = w->cur_rect;
        w->prev_rect = w->next_rect;
    }
    w->prev_col = w->cur_col;
    w->next_col = w->init_col;
    w->prev_col.a = 0;
    w->cur_col.a = 0;
    w->fade_cc = 0;
    ioObjp->stat = 1;
    ioObjp->play_func = (mobj_fn)GFN(__MOBJ_PlayWindow);
}

/* the string lookups of ref_type 0 / 1 are dead calls (the result is dropped) */
static void __MOBJ_SetStringFadeIn(struct stMobj *inObjp)
{
    struct struct_d2c56d2a *s = &inObjp->dt.string;

    PS2_FRAME(0x20);
    s->fade_max = 6;
    s->prev_col = s->cur_col;
    s->next_col = s->init_col;
    s->prev_col.a = 0;
    s->cur_col = s->prev_col;
    s->fade_cnt = 0;
    if (s->ref_type == 0) DF_MSTR_GetName(MenuMng.language, s->category, s->id);
    else if (s->ref_type == 1) DF_MSTR_GetInfo(MenuMng.language, s->category, s->id);
    s->fade_cc = 0;
    inObjp->stat = 1;
    inObjp->play_func = (mobj_fn)GFN(__MOBJ_PlayString);
}

/* ---------------------------------------------------------------- play functions (the tweens) */

/* stat 1 / 2 (fade in / out): the rect interpolates with the count before the increment, the colour with the one
 * after; stat 4 (move): types 0x12 / 0x13 step by 1, every other type by 2 while interpolating with the
 * pre-increment count */
static void __MOBJ_PlayWindow(struct stMobj *inObjp)
{
    struct struct_c6a67b40 *w = &inObjp->dt.window;
    int cc = w->fade_cc, max = w->fade_max;

    PS2_FRAME(0x20);
    switch (inObjp->stat) {
    case 1:
    case 2:
        w->fade_cc = cc + 1;
        if (cc < max) {
            DF_MFUNC_CalcInterRect(&w->cur_rect, &w->prev_rect, &w->next_rect, cc, max);
            DF_MFUNC_CalcInterColor(&w->cur_col, &w->prev_col, &w->next_col, w->fade_cc, w->fade_max);
        } else {
            w->cur_rect = w->next_rect;
            w->cur_col = w->next_col;
            inObjp->stat = inObjp->stat == 1 ? 0 : 5;
        }
        break;
    case 4:
        w->fade_cc = cc + 1;
        if (cc >= max) {
            inObjp->stat = 0;
        } else if (w->type == 0x12) {
            DF_MFUNC_CalcInterRectAcWinMdl(&w->cur_rect, &w->prev_rect, &w->next_rect, w->fade_cc, w->fade_max);
        } else if (w->type == 0x13) {
            DF_MFUNC_CalcInterRectAcWinBtm(&w->cur_rect, &w->prev_rect, &w->next_rect, w->fade_cc, w->fade_max);
        } else {
            w->fade_cc = w->fade_cc + 1;
            DF_MFUNC_CalcInterRect(&w->cur_rect, &w->prev_rect, &w->next_rect, cc, max);
        }
        break;
    }
}

/* h_per (the drawn width in percent) follows the fade; any stat but 1 / 2 removes the play function */
static void __MOBJ_PlayNumber(struct stMobj *inObjp)
{
    struct struct_fd14677b *n = &inObjp->dt.number;

    PS2_FRAME(0x30);
    if (inObjp->stat == 1 || inObjp->stat == 2) {
        int cc = n->fade_cc;
        n->fade_cc = cc + 1;
        if (cc < n->fade_max) DF_MFUNC_CalcInterColor(&n->cur_col, &n->prev_col, &n->next_col, n->fade_cc, n->fade_max);
        else n->cur_col = n->next_col;
    }
    if (inObjp->stat == 2) {
        int max = n->fade_max;
        n->h_per = (max - n->fade_cc) * 100 / (max ? max : 1);
        if (n->fade_max < n->fade_cc) {
            n->h_per = 0;
            inObjp->stat = 5;
        }
    } else if (inObjp->stat == 1) {
        int max = n->fade_max;
        n->h_per = n->fade_cc * 100 / (max ? max : 1);
        if (n->fade_max < n->fade_cc) {
            n->h_per = 100;
            inObjp->stat = 0;
        }
    } else {
        inObjp->play_func = 0;
    }
}

/* fade in: colour tween plus the typewriter count (fade_cnt += 2 per frame until the string is revealed) */
static void __MOBJ_PlayString(struct stMobj *inObjp)
{
    struct struct_d2c56d2a *s = &inObjp->dt.string;

    PS2_FRAME(0x40);
    if (inObjp->stat == 1 || inObjp->stat == 2) {
        if (s->fade_cc < s->fade_max) {
            s->fade_cc = s->fade_cc + 1;
            DF_MFUNC_CalcInterColor(&s->cur_col, &s->prev_col, &s->next_col, s->fade_cc, s->fade_max);
        } else {
            s->cur_col = s->next_col;
        }
    }
    if (inObjp->stat == 2) {
        s->h_per = 100;
        if (!(s->fade_cc < s->fade_max)) {
            s->h_per = 0;
            inObjp->stat = 5;
        }
    } else if (inObjp->stat == 1) {
        unsigned char *p;
        s->h_per = 100;
        if (s->ref_type == 2) p = (unsigned char *)s->strp;
        else if (s->ref_type == 1) p = DF_MSTR_GetInfo(MenuMng.language, s->category, s->id);
        else if (s->ref_type == 0) p = DF_MSTR_GetName(MenuMng.language, s->category, s->id);
        else p = 0;
        if (p && (strlen((char *)p) >= (unsigned int)s->fade_cnt || s->fade_cnt < s->fade_max)) s->fade_cnt += 2;
        else if (!(s->fade_cc < s->fade_max)) inObjp->stat = 0;
    } else {
        inObjp->play_func = 0;
    }
}

/* f_exfade: 0 colour only, 1 linear rect move, 2 quick-slow rect move; at the end a moving sprite snaps to next_rect
 * (fade in) or prev_rect (fade out) */
static void __MOBJ_PlaySprite(struct stMobj *inObjp)
{
    struct struct_37ce77ad *sp = &inObjp->dt.sprite;
    int cc;

    PS2_FRAME(0x20);
    if (inObjp->stat != 1 && inObjp->stat != 2) {
        inObjp->play_func = 0;
        return;
    }
    cc = sp->fade_cc;
    sp->fade_cc = cc + 1;
    if (cc < sp->fade_max) {
        if (sp->f_exfade == 2)
            DF_MFUNC_CalcInterRectQuickSlow(&sp->rect, &sp->prev_rect, &sp->next_rect, sp->fade_cc, sp->fade_max);
        else if (sp->f_exfade == 1)
            DF_MFUNC_CalcInterRect(&sp->rect, &sp->prev_rect, &sp->next_rect, sp->fade_cc, sp->fade_max);
        DF_MFUNC_CalcInterColor(&sp->cur_col, &sp->prev_col, &sp->next_col, sp->fade_cc, sp->fade_max);
    } else {
        if (sp->f_exfade) sp->rect = inObjp->stat == 1 ? sp->next_rect : sp->prev_rect;
        sp->cur_col = sp->next_col;
        inObjp->stat = inObjp->stat == 1 ? 0 : 5;
    }
}

/* ---------------------------------------------------------------- strings (type 2) */

/* small-data colour words (gp-0x7188 .. gp-0x7170) */
static inline struct struct_76b810ec mobj_col(unsigned int w)
{
    struct struct_76b810ec c;
    memcpy(&c, &w, 4);
    return c;
}
#define MOBJ_COL_WHITE 0x80808080u        /* default of NewStringBase / Direct / SetStringColor(All) */
#define MOBJ_COL_LAYOUT 0x808d8672u       /* *FromLayout*: multiplied by the layout colour */
#define MOBJ_COL_STRING 0x8092928cu       /* DF_MOBJ_NewString */
#define MOBJ_COL_STRING_SYS 0x80777774u   /* DF_MOBJ_NewString, cur_col of char types 3 / 7 / 8 */

/* puts the string's right edge at inX (ref_type 1 (info) strings are left alone) */
void DF_MOBJ_SetStringXFromRX(struct stMobj *ioObjp, int inX)
{
    struct struct_d2c56d2a *s = &ioObjp->dt.string;
    unsigned char *p;

    PS2_FRAME(0x30);
    if (s->ref_type == 1) return;
    if (s->ref_type == 0) p = DF_MSTR_GetName(MenuMng.language, s->category, s->id);
    else p = (unsigned char *)s->strp;
    s->x = inX - DF_MFONT_GetStringWSize(p, s->char_type, s->flag);
}

/* a new child of pMobj (which must be a string object) holding a whole copy of pCopyObj; a copy whose text is in
 * its own buffer points at the new buffer */
struct stMobj *DF_MOBJ_NewStringClone(struct stMobj *pMobj, struct stMobj *pCopyObj)
{
    struct stMobj *obj;

    PS2_FRAME(0x30);
    if (!pMobj || !pCopyObj || pMobj->type != 2) return 0;
    obj = DF_MOBJ_GetNewObject(pMobj);
    if (!obj) return 0;
    memcpy(obj, pCopyObj, 0x140);
    if (pCopyObj->dt.string.strp == (char *)pCopyObj->dt.string.strbuff)
        obj->dt.string.strp = (char *)obj->dt.string.strbuff;
    return obj;
}

/* without pcolor: (default byte * layout colour byte) >> 7 per channel, no clamp */
static void mobj_layout_col(struct struct_76b810ec *out, const struct struct_76b810ec *def,
                            const struct _LAYOUT_RET_DATA *lo, const struct struct_76b810ec *pcolor)
{
    if (pcolor) {
        *out = *pcolor;
        return;
    }
    out->r = (unsigned char)((def->r * lo->Col.r) >> 7);
    out->g = (unsigned char)((def->g * lo->Col.g) >> 7);
    out->b = (unsigned char)((def->b * lo->Col.b) >> 7);
    out->a = (unsigned char)((def->a * lo->Col.a) >> 7);
}

static void mobj_clear_layout(struct _LAYOUT_RET_DATA *lo)
{
    unsigned char *b = (unsigned char *)lo;
    for (int i = 0; i < 0x50; i++) b[i] = 0;
}

static int __GetFontTypeFromLayout(int id)
{
    static const unsigned char tbl[9] = {3, 5, 4, 2, 1, 0, 3, 7, 8};

    return (unsigned int)id < 9 ? tbl[id] : 3;
}

struct stMobj *DF_MOBJ_NewStringDirectFromLayout(struct stMobj *pMobj, char *pstr, struct _LAYOUT_RET_DATA *pLayout,
                                                 struct struct_76b810ec *pcolor, unsigned int depth, unsigned int flag)
{
    PS2_FRAME(0x70);
    PS2_STACK(struct struct_76b810ec, def, 0x68);
    PS2_STACK(struct struct_76b810ec, col, 0x6c);
    struct stMobj *obj;

    def = mobj_col(MOBJ_COL_LAYOUT);
    mobj_layout_col(&col, &def, pLayout, pcolor);
    obj = DF_MOBJ_NewStringBase(pMobj, __GetFontTypeFromLayout(pLayout->CommonID[1]), pLayout->Pos.x, pLayout->Pos.y,
                                &col, depth, flag);
    if (obj && obj->type == 2) {
        obj->dt.string.ref_type = 2;
        obj->dt.string.strp = pstr;
    }
    return obj;
}

struct stMobj *DF_MOBJ_NewStringDirectFromLayoutName(struct stMobj *pMobj, char *str, char *pLayoutHeader,
                                                     char *pPartsName, struct struct_76b810ec *pcolor,
                                                     unsigned int depth, unsigned int flag)
{
    PS2_FRAME(0xc0);
    PS2_STACK(struct _LAYOUT_RET_DATA, lo, 0x60);
    PS2_STACK(struct struct_76b810ec, def, 0xb8);
    PS2_STACK(struct struct_76b810ec, col, 0xbc);
    struct stMobj *obj;

    mobj_clear_layout(&lo);
    DF_LO_GetLayoutData((unsigned char *)pLayoutHeader, (unsigned char *)pPartsName, 0, &lo);
    def = mobj_col(MOBJ_COL_LAYOUT);
    mobj_layout_col(&col, &def, &lo, pcolor);
    obj = DF_MOBJ_NewStringBase(pMobj, __GetFontTypeFromLayout(lo.CommonID[1]), lo.Pos.x, lo.Pos.y, &col, depth, flag);
    if (obj && obj->type == 2) {
        obj->dt.string.ref_type = 2;
        obj->dt.string.strp = str;
    }
    return obj;
}

/* the font type is looked up twice (the first result is dropped) */
struct stMobj *DF_MOBJ_NewStringFromLayout(struct stMobj *pMobj, int inCategory, int inID, int inRefType,
                                           struct _LAYOUT_RET_DATA *pLayout, struct struct_76b810ec *pcolor,
                                           unsigned int depth, unsigned int flag)
{
    PS2_FRAME(0xa0);
    PS2_STACK(struct struct_76b810ec, def, 0x98);
    PS2_STACK(struct struct_76b810ec, col, 0x9c);
    struct stMobj *obj;

    __GetFontTypeFromLayout(pLayout->CommonID[1]);
    def = mobj_col(MOBJ_COL_LAYOUT);
    mobj_layout_col(&col, &def, pLayout, pcolor);
    obj = DF_MOBJ_NewStringBase(pMobj, __GetFontTypeFromLayout(pLayout->CommonID[1]), pLayout->Pos.x, pLayout->Pos.y,
                                &col, depth, flag);
    if (obj && obj->type == 2) {
        obj->dt.string.ref_type = inRefType;
        obj->dt.string.category = inCategory;
        obj->dt.string.id = inID;
    }
    return obj;
}

struct stMobj *DF_MOBJ_NewStringBaseFromLayout(struct stMobj *pMobj, struct _LAYOUT_RET_DATA *pLayout,
                                               struct struct_76b810ec *pcolor, unsigned int depth, unsigned int flag)
{
    PS2_FRAME(0x60);
    PS2_STACK(struct struct_76b810ec, def, 0x58);
    PS2_STACK(struct struct_76b810ec, col, 0x5c);

    def = mobj_col(MOBJ_COL_LAYOUT);
    mobj_layout_col(&col, &def, pLayout, pcolor);
    return DF_MOBJ_NewStringBase(pMobj, __GetFontTypeFromLayout(pLayout->CommonID[1]), pLayout->Pos.x, pLayout->Pos.y,
                                 &col, depth, flag);
}

struct stMobj *DF_MOBJ_NewStringBaseFromLayoutName(struct stMobj *pMobj, char *pLayoutHeader, char *pPartsName,
                                                   struct struct_76b810ec *color, unsigned int depth, unsigned int flag)
{
    PS2_FRAME(0xb0);
    PS2_STACK(struct _LAYOUT_RET_DATA, lo, 0x50);
    PS2_STACK(struct struct_76b810ec, def, 0xa8);
    PS2_STACK(struct struct_76b810ec, col, 0xac);

    mobj_clear_layout(&lo);
    DF_LO_GetLayoutData((unsigned char *)pLayoutHeader, (unsigned char *)pPartsName, 0, &lo);
    def = mobj_col(MOBJ_COL_LAYOUT);
    mobj_layout_col(&col, &def, &lo, color);
    return DF_MOBJ_NewStringBase(pMobj, __GetFontTypeFromLayout(lo.CommonID[1]), lo.Pos.x, lo.Pos.y, &col, depth, flag);
}

/* also zeroes a second, unused layout buffer (sp+0xc0) */
struct stMobj *DF_MOBJ_NewStringFromLayoutName(struct stMobj *pMobj, int inCategory, int inID, int inRefType,
                                               char *pLayoutHeader, char *pPartsName, struct struct_76b810ec *pcolor,
                                               unsigned int depth, unsigned int flag)
{
    PS2_FRAME(0x120);
    PS2_STACK(struct _LAYOUT_RET_DATA, dead, 0xc0);
    PS2_STACK(struct _LAYOUT_RET_DATA, lo, 0x70);
    PS2_STACK(struct struct_76b810ec, def, 0x118);
    PS2_STACK(struct struct_76b810ec, col, 0x11c);
    struct stMobj *obj;

    if (hook_mobj_string_layout_id) inID = hook_mobj_string_layout_id(inID);   /* port hook (game.versus_garage) */
    mobj_clear_layout(&dead);
    mobj_clear_layout(&lo);
    DF_LO_GetLayoutData((unsigned char *)pLayoutHeader, (unsigned char *)pPartsName, 0, &lo);
    def = mobj_col(MOBJ_COL_LAYOUT);
    mobj_layout_col(&col, &def, &lo, pcolor);
    obj = DF_MOBJ_NewStringBase(pMobj, __GetFontTypeFromLayout(lo.CommonID[1]), lo.Pos.x, lo.Pos.y, &col, depth, flag);
    if (obj && obj->type == 2) {
        obj->dt.string.ref_type = inRefType;
        obj->dt.string.category = inCategory;
        obj->dt.string.id = inID;
    }
    return obj;
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper (gen_bridge stops at 8 int args); flag is the caller's stack arg at sp+0 */
static void bw__DF_MOBJ_NewStringFromLayoutName(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    struct stMobj *r = DF_MOBJ_NewStringFromLayoutName(
        (struct stMobj *)bridge_ptr(c->gpr[A0].uw[0]), c->gpr[A1].sw[0], c->gpr[A2].sw[0], c->gpr[A3].sw[0],
        (char *)bridge_ptr(c->gpr[T0].uw[0]), (char *)bridge_ptr(c->gpr[T1].uw[0]),
        (struct struct_76b810ec *)bridge_ptr(c->gpr[T2].uw[0]), c->gpr[T3].uw[0], rd32(sp));
    c->gpr[V0].sd[0] = (s32)PS2PTR(r);
}
#endif

void DF_MOBJ_SetStringID(struct stMobj *inObjp, int inCategory, int inID, int inRefType)
{
    if (!inObjp || inObjp->type != 2) return;
    inObjp->dt.string.ref_type = inRefType;
    inObjp->dt.string.category = inCategory;
    inObjp->dt.string.id = inID;
}

void DF_MOBJ_SetStringCopy(struct stMobj *inObjp, char *inStrp)
{
    struct struct_d2c56d2a *s;

    PS2_FRAME(0x20);
    if (!inObjp || inObjp->type != 2) return;
    s = &inObjp->dt.string;
    strncpy((char *)s->strbuff, inStrp, 0xbf);
    s->strbuff[0xbf] = 0;
    s->ref_type = 2;
    s->strp = (char *)s->strbuff;
}

void DF_MOBJ_SetStringDirect(struct stMobj *inObjp, char *inStrp)
{
    if (!inObjp || inObjp->type != 2) return;
    inObjp->dt.string.ref_type = 2;
    inObjp->dt.string.strp = inStrp;
}

/* char-type flag bits: 3 (and 6, except in NewString) -> 1, 7 / 8 -> 0x401 */
static void mobj_string_type_flag(struct struct_d2c56d2a *s, int with6)
{
    if (s->char_type == 3) s->flag |= 1;
    else if (s->char_type == 7 || s->char_type == 8) s->flag |= 0x401;
    else if (with6 && s->char_type == 6) s->flag |= 1;
}

static void mobj_string_cols(struct struct_d2c56d2a *s, const struct struct_76b810ec *c)
{
    s->cur_col = *c;
    s->prev_col = s->cur_col;
    s->next_col = s->cur_col;
    s->init_col = s->cur_col;
}

/* an empty direct string (ref_type 2, strp -> "" in the main ELF); no play function */
struct stMobj *DF_MOBJ_NewStringBase(struct stMobj *inObjp, int inCharType, int inX, int inY,
                                     struct struct_76b810ec *inColp, unsigned int inDepth, unsigned int inFlag)
{
    PS2_FRAME(0x80);
    PS2_STACK(struct struct_76b810ec, def, 0x7c);
    struct stMobj *obj;
    struct struct_d2c56d2a *s;

    def = mobj_col(MOBJ_COL_WHITE);
    obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    s = &obj->dt.string;
    obj->type = 2;
    s->ref_type = 2;
    mobj_string_cols(s, inColp ? inColp : &def);
    s->x = inX;
    s->y = inY;
    s->depth = inDepth;
    s->char_type = inCharType;
    s->flag = inFlag;
    s->h_per = 100;
    s->strp = (char *)mobj_empty_str;
    mobj_string_type_flag(s, 1);
    obj->draw_func = (mobj_fn)GFN(__MOBJ_DrawString);
    return obj;
}

/* no NULL test (only the type) */
void DF_MOBJ_SetStringColorAll(struct stMobj *pMobj, struct struct_76b810ec *pCol)
{
    if (hook_mobj_skip_string_color_all && hook_mobj_skip_string_color_all()) return;   /* port hook */

    PS2_FRAME(0x10);
    PS2_STACK(struct struct_76b810ec, def, 0xc);
    struct struct_d2c56d2a *s = &pMobj->dt.string;

    def = mobj_col(MOBJ_COL_WHITE);
    if (pMobj->type != 2) return;
    s->init_col = pCol ? *pCol : def;
    s->next_col = s->init_col;
    s->prev_col = s->next_col;
    s->cur_col = s->prev_col;
}

void DF_MOBJ_SetStringColor(struct stMobj *pMobj, struct struct_76b810ec *pCol)
{
    PS2_FRAME(0x10);
    PS2_STACK(struct struct_76b810ec, def, 0xc);

    def = mobj_col(MOBJ_COL_WHITE);
    if (pMobj->type != 2) return;
    pMobj->dt.string.cur_col = pCol ? *pCol : def;
}

void DF_MOBJ_SetStringPos(struct stMobj *pMobj, int x, int y)
{
    if (pMobj->type != 2) return;
    pMobj->dt.string.x = x;
    pMobj->dt.string.y = y;
}

struct stMobj *DF_MOBJ_NewStringCopy(struct stMobj *inObjp, char *pstr, int inCharType, int inX, int inY,
                                     struct struct_76b810ec *inColp, unsigned int inDepth, unsigned int inFlag)
{
    struct stMobj *obj;

    PS2_FRAME(0x30);
    obj = DF_MOBJ_NewStringBase(inObjp, inCharType, inX, inY, inColp, inDepth, inFlag);
    if (obj && obj->type == 2) {
        struct struct_d2c56d2a *s = &obj->dt.string;
        strncpy((char *)s->strbuff, pstr, 0xbf);
        s->strbuff[0xbf] = 0;
        s->ref_type = 2;
        s->strp = (char *)s->strbuff;
    }
    return obj;
}

struct stMobj *DF_MOBJ_NewStringDirect(struct stMobj *inObjp, char *inStrp, int inCharType, int inX, int inY,
                                       struct struct_76b810ec *inColp, unsigned int inDepth, unsigned int inFlag)
{
    PS2_FRAME(0x90);
    PS2_STACK(struct struct_76b810ec, def, 0x8c);
    struct stMobj *obj;
    struct struct_d2c56d2a *s;

    def = mobj_col(MOBJ_COL_WHITE);
    obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    s = &obj->dt.string;
    obj->type = 2;
    s->ref_type = 2;
    mobj_string_cols(s, inColp ? inColp : &def);
    s->x = inX;
    s->y = inY;
    s->depth = inDepth;
    s->char_type = inCharType;
    s->flag = inFlag;
    s->h_per = 100;
    s->strp = inStrp;
    mobj_string_type_flag(s, 1);
    obj->draw_func = (mobj_fn)GFN(__MOBJ_DrawString);
    return obj;
}

/* a string-table string; char type 6 gets no flag bit here, and without a colour types 3 / 7 / 8 start (cur_col
 * only) in 0x80777774 */
struct stMobj *DF_MOBJ_NewString(struct stMobj *inObjp, int inCategory, int inID, int inRefType, int inCharType,
                                 int inX, int inY, struct struct_76b810ec *inColp, unsigned int inDepth,
                                 unsigned int inFlag)
{
    PS2_FRAME(0x90);
    PS2_STACK(struct struct_76b810ec, def, 0x8c);
    PS2_STACK(struct struct_76b810ec, def_sys, 0x88);
    struct stMobj *obj;
    struct struct_d2c56d2a *s;

    def = mobj_col(MOBJ_COL_STRING);
    def_sys = mobj_col(MOBJ_COL_STRING_SYS);
    obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    s = &obj->dt.string;
    obj->type = 2;
    s->category = inCategory;
    s->ref_type = inRefType;
    s->id = inID;
    mobj_string_cols(s, inColp ? inColp : &def);
    s->x = inX;
    s->y = inY;
    s->depth = inDepth;
    s->char_type = inCharType;
    s->flag = inFlag;
    s->h_per = 100;
    mobj_string_type_flag(s, 0);
    if (!inColp && (s->char_type == 3 || s->char_type == 7 || s->char_type == 8)) s->cur_col = def_sys;
    obj->draw_func = (mobj_fn)GFN(__MOBJ_DrawString);
    return obj;
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper; depth / flag are the caller's stack args at sp+0 / sp+8 */
static void bw__DF_MOBJ_NewString(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    struct stMobj *r = DF_MOBJ_NewString(
        (struct stMobj *)bridge_ptr(c->gpr[A0].uw[0]), c->gpr[A1].sw[0], c->gpr[A2].sw[0], c->gpr[A3].sw[0],
        c->gpr[T0].sw[0], c->gpr[T1].sw[0], c->gpr[T2].sw[0], (struct struct_76b810ec *)bridge_ptr(c->gpr[T3].uw[0]),
        rd32(sp), rd32(sp + 8));
    c->gpr[V0].sd[0] = (s32)PS2PTR(r);
}
#endif

/* ---------------------------------------------------------------- windows (type 4) and numbers (type 3) */

#define MOBJ_COL_NUMBER 0x80909090u   /* DF_MOBJ_NewNumber */

static void mobj_window_init(struct stMobj *obj, int type, int shape, const struct stMenuRect *rect,
                             const struct struct_76b810ec *col, unsigned int depth)
{
    struct struct_c6a67b40 *w = &obj->dt.window;

    obj->type = 4;
    obj->stat = 0;
    w->tex_info = 0;
    w->cur_rect = *rect;
    w->prev_rect = *rect;
    w->next_rect = *rect;
    w->init_rect = *rect;
    w->cur_col = *col;
    w->prev_col = w->cur_col;
    w->next_col = w->cur_col;
    w->init_col = w->cur_col;
    w->depth = depth;
    w->f_select = 0;
    w->type = type;
    w->shape = shape;
}

/* a textured window: uv rect, texture index and the 5 blend bytes (a NULL uv / blend reads PS2 address 0, as the
 * original) */
struct stMobj *DF_MOBJ_NXNewWindow(struct stMobj *inObjp, int inType, int inShape, struct stMenuRect *inRectp,
                                   struct struct_76b810ec *inColp, unsigned int inDepth, int *InUV, int InTexID,
                                   unsigned char *pInBlend)
{
    PS2_FRAME(0xa0);
    PS2_STACK(struct struct_76b810ec, def, 0x9c);
    struct stMobj *obj;
    const int *uv = InUV ? InUV : (const int *)PS2_NULL_MEM();

    def = mobj_col(MOBJ_COL_WHITE);
    obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    mobj_window_init(obj, inType, inShape, inRectp, inColp ? inColp : &def, inDepth);
    for (int i = 0; i < 4; i++) obj->dt.window.uv[i] = uv[i];
    obj->dt.window.tex_index = InTexID;
    memcpy(obj->dt.window.blend_value, pInBlend ? (const void *)pInBlend : PS2_NULL_MEM(), 5);
    obj->play_func = (mobj_fn)GFN(__MOBJ_PlayWindow);
    obj->draw_func = (mobj_fn)GFN(__MOBJ_DrawWindow);
    return obj;
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper (gen_bridge stops at 8 int args); pInBlend is the caller's stack arg at sp+0 */
static void bw__DF_MOBJ_NXNewWindow(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    struct stMobj *r = DF_MOBJ_NXNewWindow(
        (struct stMobj *)bridge_ptr(c->gpr[A0].uw[0]), c->gpr[A1].sw[0], c->gpr[A2].sw[0],
        (struct stMenuRect *)bridge_ptr(c->gpr[A3].uw[0]), (struct struct_76b810ec *)bridge_ptr(c->gpr[T0].uw[0]),
        c->gpr[T1].uw[0], (int *)bridge_ptr(c->gpr[T2].uw[0]), c->gpr[T3].sw[0],
        (unsigned char *)bridge_ptr(rd32(sp)));
    c->gpr[V0].sd[0] = (s32)PS2PTR(r);
}
#endif

/* tex_index / uv / blend stay as the cleared slot left them */
struct stMobj *DF_MOBJ_NewWindow(struct stMobj *inObjp, int inType, int inShape, struct stMenuRect *inRectp,
                                 struct struct_76b810ec *inColp, unsigned int inDepth)
{
    PS2_FRAME(0x70);
    PS2_STACK(struct struct_76b810ec, def, 0x6c);
    struct stMobj *obj;

    def = mobj_col(MOBJ_COL_WHITE);
    obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    mobj_window_init(obj, inType, inShape, inRectp, inColp ? inColp : &def, inDepth);
    obj->play_func = (mobj_fn)GFN(__MOBJ_PlayWindow);
    obj->draw_func = (mobj_fn)GFN(__MOBJ_DrawWindow);
    return obj;
}

/* no play function, stat not written, h_per 0 (DrawNumber draws zero width until a fade sets it); char types
 * 3 / 7 / 8 get flag bits 1 | 0x400 */
struct stMobj *DF_MOBJ_NewNumber(struct stMobj *inObjp, int inNum, int inPlace, int inSign, int inCharType, int inX,
                                 int inY, struct struct_76b810ec *inColp, unsigned int inDepth)
{
    PS2_FRAME(0x90);
    PS2_STACK(struct struct_76b810ec, def, 0x8c);
    struct stMobj *obj;
    struct struct_fd14677b *n;

    def = mobj_col(MOBJ_COL_NUMBER);
    obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    n = &obj->dt.number;
    obj->type = 3;
    n->num = inNum;
    n->place = inPlace;
    n->sign = inSign;
    n->x = inX;
    n->y = inY;
    n->cur_col = inColp ? *inColp : def;
    n->prev_col = n->cur_col;
    n->next_col = n->cur_col;
    n->init_col = n->cur_col;
    n->depth = inDepth;
    n->char_type = inCharType;
    n->h_per = 0;
    if (n->char_type == 3 || n->char_type == 7 || n->char_type == 8) {
        n->flag |= 1;
        n->flag |= 0x400;
    }
    obj->draw_func = (mobj_fn)GFN(__MOBJ_DrawNumber);
    return obj;
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper; inDepth is the caller's stack arg at sp+0 */
static void bw__DF_MOBJ_NewNumber(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    struct stMobj *r = DF_MOBJ_NewNumber(
        (struct stMobj *)bridge_ptr(c->gpr[A0].uw[0]), c->gpr[A1].sw[0], c->gpr[A2].sw[0], c->gpr[A3].sw[0],
        c->gpr[T0].sw[0], c->gpr[T1].sw[0], c->gpr[T2].sw[0], (struct struct_76b810ec *)bridge_ptr(c->gpr[T3].uw[0]),
        rd32(sp));
    c->gpr[V0].sd[0] = (s32)PS2PTR(r);
}
#endif

/* ---------------------------------------------------------------- sprites (type 1) */

/* the uv rect is copied with lq / sq (aligned down to 16 bytes, as the original) */
static void mobj_sprite_init(struct stMobj *obj, const struct stMenuRect *rect, const int *uv,
                             const struct struct_76b810ec *col, struct struct_13efd7f4 *texp, unsigned int depth)
{
    struct struct_37ce77ad *sp = &obj->dt.sprite;

    obj->type = 1;
    obj->stat = 0;
    sp->rect = *rect;
    memcpy(sp->uv, PS2_LQ_SRC(uv), 16);
    sp->texp = texp;
    sp->cur_col = *col;
    sp->prev_col = sp->cur_col;
    sp->next_col = sp->cur_col;
    sp->init_col = sp->cur_col;
    sp->depth = depth;
}

/* blend 0 / 1 / 2 -> ALPHA register 0x44 / 0x48 / 0x42 (other values leave reg_alpha as the cleared slot has it) */
struct stMobj *DF_MOBJ_NXNewSprite(struct stMobj *inObjp, int texid, struct stMenuRect *inRectp, int *inUV,
                                   struct struct_76b810ec *inColp, unsigned int inDepth, int inDir, int inF_Linear,
                                   int inBlend)
{
    PS2_FRAME(0x90);
    PS2_STACK(struct struct_76b810ec, def, 0x8c);
    struct stMobj *obj;
    struct struct_37ce77ad *sp;

    def = mobj_col(MOBJ_COL_WHITE);
    obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    sp = &obj->dt.sprite;
    mobj_sprite_init(obj, inRectp, inUV ? inUV : (const int *)PS2_NULL_MEM(), inColp ? inColp : &def, 0, inDepth);
    if (inBlend == 0) sp->reg_alpha = 0x44;
    else if (inBlend == 1) sp->reg_alpha = 0x48;
    else if (inBlend == 2) sp->reg_alpha = 0x42;
    sp->uv_dir = inDir;
    sp->f_linear = inF_Linear;
    sp->texid = texid;
    obj->draw_func = (mobj_fn)GFN(__MOBJ_DrawSpriteNX);
    return obj;
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper; inBlend is the caller's stack arg at sp+0 */
static void bw__DF_MOBJ_NXNewSprite(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    struct stMobj *r = DF_MOBJ_NXNewSprite(
        (struct stMobj *)bridge_ptr(c->gpr[A0].uw[0]), c->gpr[A1].sw[0],
        (struct stMenuRect *)bridge_ptr(c->gpr[A2].uw[0]), (int *)bridge_ptr(c->gpr[A3].uw[0]),
        (struct struct_76b810ec *)bridge_ptr(c->gpr[T0].uw[0]), c->gpr[T1].uw[0], c->gpr[T2].sw[0],
        c->gpr[T3].sw[0], (int)rd32(sp));
    c->gpr[V0].sd[0] = (s32)PS2PTR(r);
}
#endif

struct stMobj *DF_MOBJ_NewSprite(struct stMobj *inObjp, struct struct_13efd7f4 *inTexp, struct stMenuRect *inRectp,
                                 int *inUV, struct struct_76b810ec *inColp, unsigned int inDepth, int inDir,
                                 int inF_Linear)
{
    PS2_FRAME(0x90);
    PS2_STACK(struct struct_76b810ec, def, 0x8c);
    struct stMobj *obj;
    struct struct_37ce77ad *sp;

    def = mobj_col(MOBJ_COL_WHITE);
    obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    sp = &obj->dt.sprite;
    mobj_sprite_init(obj, inRectp, inUV ? inUV : (const int *)PS2_NULL_MEM(), inColp ? inColp : &def, inTexp,
                     inDepth);
    sp->reg_alpha = 0x44;
    sp->uv_dir = inDir;
    sp->f_linear = inF_Linear;
    obj->draw_func = (mobj_fn)GFN(__MOBJ_DrawSprite);
    return obj;
}

/* the original stores draw_func without a NULL test (a full slot array writes PS2 address 8); the port skips it */
struct stMobj *DF_MOBJ_NewSpriteOld(struct stMobj *inObjp, struct struct_13efd7f4 *inTexp, struct stMenuRect *inRectp,
                                    int *inUV, struct struct_76b810ec *inColp, unsigned int inDepth, int inDir,
                                    int inF_Linear)
{
    struct stMobj *obj;

    PS2_FRAME(0x10);
    obj = DF_MOBJ_NewSprite(inObjp, inTexp, inRectp, inUV, inColp, inDepth, inDir, inF_Linear);
    if (obj) obj->draw_func = (mobj_fn)GFN(__MOBJ_DrawSpriteOld);
    return obj;
}

/* a blend-0 NX sprite from a layout record; reg_alpha then = the layout's ALPHA fields (A, B, C, D, FIX) */
struct stMobj *DF_MOBJ_NewSpriteFromLayout(struct stMobj *pParent, struct _LAYOUT_RET_DATA *pLayout)
{
    PS2_FRAME(0x60);
    PS2_STACK(struct stMenuRect, rect, 0x40);
    PS2_STACK(struct stMenuRect, uv, 0x30);   /* the int[4] uv rect */
    PS2_STACK(struct struct_76b810ec, col, 0x5c);
    struct stMobj *obj;
    const unsigned char *b = pLayout->Blend;

    memset(&rect, 0, sizeof rect);
    memcpy(&rect, &pLayout->Pos, 16);
    memset(&uv, 0, sizeof uv);
    memcpy(&uv, pLayout->UV, 16);
    col = pLayout->Col;
    obj = DF_MOBJ_NXNewSprite(pParent, pLayout->TexID, &rect, (int *)&uv, &col, 1, 0, 1, 0);
    if (!obj) return 0;
    obj->dt.sprite.reg_alpha = (unsigned long long)b[0] | (unsigned long long)b[1] << 2 |
                               (unsigned long long)b[2] << 4 | (unsigned long long)b[3] << 6 |
                               (unsigned long long)b[4] << 32;
    return obj;
}

void DF_MOBJ_SetSpriteFlag(struct stMobj *pMobj, unsigned char flag)
{
    if (pMobj->type == 1 || pMobj->type == 7) pMobj->dt.sprite.flag = flag;
}

void DF_MOBJ_SetSpriteMove(struct stMobj *pMobj, struct stMenuRect *rect, enum_fb591092 type, int count)
{
    struct struct_37ce77ad *sp = &pMobj->dt.sprite;

    if (pMobj->type != 1 && pMobj->type != 7) return;
    sp->prev_rect = sp->rect;
    sp->next_rect = *rect;
    sp->fade_max = count < 2 ? 1 : count;
    sp->fade_cc = 0;
    sp->f_exfade = type;
    sp->next_col = sp->cur_col;
    pMobj->stat = 1;
    pMobj->play_func = (mobj_fn)GFN(__MOBJ_PlaySprite);
}

void DF_MOBJ_SetSpriteDrawMode(struct stMobj *pMobj, int DrawMode)
{
    pMobj->dt.sprite.draw_mode = DrawMode;
}

void DF_MOBJ_SetSpriteSubColor(struct stMobj *pMobj, struct struct_76b810ec *pCol)
{
    memcpy(&pMobj->dt.sprite.sub_col, pCol, 4);
}

/* position for the help cursor; pos[2] / pos[3] are never written, other types leave pos as it was */
typedef int mobj_pos4[4];   /* int (*)[4] return spelled through a typedef: gen_bridge's definition scan */

mobj_pos4 *DF_MOBJ_GetHelpPos(struct stMobj *inObjp)
{
    int (*pos)[4] = &DF_MOBJ_GetHelpPos__pos;

    switch (inObjp->type) {
    case 10: case 4: case 3: case 7: case 1:   /* window cur_rect / number / sprite rect: x, y at dt+0 */
        (*pos)[0] = inObjp->dt.number.x;
        (*pos)[1] = inObjp->dt.number.y;
        break;
    case 9: case 2:
        (*pos)[0] = inObjp->dt.string.x;
        (*pos)[1] = inObjp->dt.string.y;
        break;
    }
    return pos;
}

void DF_MOBJ_SetModelScale(struct stMobj *pMobj, float *scl)
{
    if (!pMobj || (pMobj->type != 5 && pMobj->type != 8)) return;
    for (int i = 0; i < 4; i++) pMobj->dt.model.scale[i] = scl[i];
}

void DF_MOBJ_SetModelPos(struct stMobj *pMobj, float *pos)
{
    if (!pMobj || (pMobj->type != 5 && pMobj->type != 8)) return;
    for (int i = 0; i < 4; i++) pMobj->dt.model.pos[i] = pos[i];
}

void DF_MOBJ_SetDrawPrio(struct stMobj *pmobj, enum_fedbfd96 prio)
{
    pmobj->draw_prio = prio & 3;
}

void DF_MOBJ_SetDrawEnable(struct stMobj *pObj, int bEnable)
{
    if (pObj) pObj->f_draw = bEnable & 1;
}

static void mobj_sprite_fade_out(struct stMobj *o)
{
    struct struct_37ce77ad *sp = &o->dt.sprite;

    sp->prev_col = sp->cur_col;
    sp->next_col = sp->cur_col;
    sp->next_col.a = 0;
    sp->prev_rect = sp->rect;
    DF_MFUNC_GetFadeOutRect(&sp->next_rect, &sp->prev_rect);
    sp->fade_max = o->stat == 1 ? sp->fade_cc : 6;
    sp->fade_cc = 0;
    o->stat = 2;
    o->play_func = (mobj_fn)GFN(__MOBJ_PlaySprite);
}

static void mobj_string_fade_out(struct stMobj *o)
{
    struct struct_d2c56d2a *s = &o->dt.string;

    s->prev_col = s->cur_col;
    s->next_col = s->cur_col;
    s->next_col.a = 0;
    s->fade_max = o->stat == 1 ? s->fade_cc : 6;
    s->fade_cc = 0;
    o->stat = 2;
    o->play_func = (mobj_fn)GFN(__MOBJ_PlayString);
}

static void mobj_model_fade_out(struct stMobj *o)
{
    struct struct_9db062e8 *m = &o->dt.model;

    m->prev_alpha = m->cur_alpha;
    m->next_alpha = 0;
    if (m->kind == 3 && m->acp) DF_M_AC_SetFadeOut(m->acp, 6, 0.0f);
    m->p_show_rect = m->show_rect;
    DF_MFUNC_GetFadeOutRect(&m->n_show_rect, &m->p_show_rect);
    o->stat = 2;
    o->count = 0;
}

void DF_MOBJ_SetFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x20);
    if (!ioObjp) return;
    switch (ioObjp->type) {
    case 1: case 7:
        mobj_sprite_fade_out(ioObjp);
        break;
    case 2: case 9:
        mobj_string_fade_out(ioObjp);
        break;
    case 3: {
        struct struct_fd14677b *n = &ioObjp->dt.number;
        n->prev_col = n->cur_col;
        n->next_col = n->cur_col;
        n->next_col.a = 0;
        n->fade_max = ioObjp->stat == 1 ? n->fade_cc : 6;
        n->fade_cc = 0;
        ioObjp->stat = 2;
        ioObjp->play_func = (mobj_fn)GFN(__MOBJ_PlayNumber);
        break;
    }
    case 4: case 10:
        __MOBJ_SetWindowFadeOut(ioObjp);
        break;
    case 5: case 8:
        mobj_model_fade_out(ioObjp);
        break;
    }
}

/* the cur_col.a test reads the alpha it has just cleared: next_col keeps init_col's alpha */
static void mobj_sprite_fade_in(struct stMobj *o)
{
    struct struct_37ce77ad *sp = &o->dt.sprite;

    sp->next_rect = sp->rect;
    DF_MFUNC_GetFadeInRect(&sp->prev_rect, &sp->next_rect);
    sp->fade_max = 6;
    sp->prev_col = sp->cur_col;
    sp->prev_col.a = 0;
    sp->cur_col.a = 0;
    sp->next_col = sp->init_col;
    if (sp->cur_col.a) sp->next_col.a = sp->cur_col.a;
    sp->fade_cc = 0;
    o->stat = 1;
    o->play_func = (mobj_fn)GFN(__MOBJ_PlaySprite);
}

static void mobj_model_fade_in(struct stMobj *o)
{
    struct struct_9db062e8 *m = &o->dt.model;

    m->cur_alpha = 0;
    m->prev_alpha = 0;
    m->next_alpha = 0x80;
    if (m->kind == 3 && m->acp) DF_M_AC_SetFadeIn(m->acp, 6, 0.0f);
    m->n_show_rect = m->show_rect;
    DF_MFUNC_GetFadeInRect(&m->p_show_rect, &m->n_show_rect);
    o->stat = 1;
    o->count = 0;
}

void DF_MOBJ_SetFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x20);
    if (!ioObjp) return;
    switch (ioObjp->type) {
    case 1: case 7:
        mobj_sprite_fade_in(ioObjp);
        break;
    case 2: case 9:
        __MOBJ_SetStringFadeIn(ioObjp);
        break;
    case 3: {
        struct struct_fd14677b *n = &ioObjp->dt.number;
        n->fade_max = 6;
        n->prev_col = n->cur_col;
        n->next_col = n->init_col;
        n->prev_col.a = 0;
        n->fade_cc = 0;
        ioObjp->stat = 1;
        ioObjp->play_func = (mobj_fn)GFN(__MOBJ_PlayNumber);
        break;
    }
    case 4: case 10:
        __MOBJ_SetWindowFadeIn(ioObjp);
        break;
    case 5: case 8:
        mobj_model_fade_in(ioObjp);
        break;
    }
}

int DF_MOBJ_IsFinished(struct stMobj *pMobj)
{
    if (!pMobj) return 1;
    return pMobj->stat == 5;
}

/* marks the object finished (stat 5) and drops its callbacks; it does not set f_erase */
void DF_MOBJ_Finish(struct stMobj *pMobj)
{
    pMobj->stat = 5;
    pMobj->pkt_func = 0;
    pMobj->play_func = 0;
    pMobj->draw_func = 0;
}

/* first free slot after the parent (from the top without one), cleared, f_use + f_draw set */
struct stMobj *DF_MOBJ_GetNewObject(struct stMobj *inObjp)
{
    struct stMobj *p;

    PS2_FRAME(0x20);
    for (p = inObjp ? inObjp + 1 : MenuObjTopp; p < MenuObjEndp; p++) {
        if (p->f_use) continue;
        DF_MFUNC_ClearMemory(p, 0x50);
        p->f_use = 1;
        p->f_draw = 1;
        p->stat = 0;
        break;
    }
    return p == MenuObjEndp ? 0 : p;
}

unsigned char *DF_MOBJ_Init(unsigned char *ioBuff)
{
    PS2_FRAME(0x20);
    MenuObjTopp = (struct stMobj *)ioBuff;
    MenuObjEndp = (struct stMobj *)(ioBuff + 0x64000);
    DF_MFUNC_ClearMemory(ioBuff, 0x19000);
    return ioBuff + 0x64000;
}

/* ---------------------------------------------------------------- 3D models (types 5 / 8) */
/* A model object holds either one menu model (mp + add_mp[], built from an MMD file; __MOBJ_PlayModel /
 * __MOBJ_MakeVu1PacketModel) or, for kind 3, a private copy of a menu AC (acp; __MOBJ_PlayModelAC /
 * __MOBJ_MakeVu1PacketModelAC). The packet functions build their VIF / GS packets through the interpreted
 * DF_MPKT_* / DF_MVIEW_* / DF_MMODEL_* / DF_M_AC_* (phase 2). */

#define MOBJ_TEX_MODEL_BG 0x3ec   /* menu texture of the model's backdrop sprite */
#define MOBJ_K_SCISSOR1 0x40
#define MOBJ_K_SCISSOR2 0x41
#define MOBJ_SCISSOR_FULL (0x27full << 16 | 0x1bfull << 48)   /* SCAX1 = 639, SCAY1 = 447 */
#define MOBJ_VIF_FLUSHA 0x13000000u

/* SCISSOR_x of the show rect: SCAX0 = x, SCAX1 = x + w - 1, SCAY0 = y, SCAY1 = y + h - 1 (x and x + w - 1 are
 * sign-extended like the original's dsra32, so a negative x sets the upper bits) */
static inline unsigned long long mobj_scissor(const struct stMenuRect *r)
{
    return (unsigned long long)(long long)r->x | (unsigned long long)(long long)(r->x + r->w - 1) << 16 |
           (unsigned long long)(unsigned int)r->y << 32 | (unsigned long long)(r->y + r->h - 1) << 48;
}

void DF_MOBJ_SetPatternModelAC(struct stMobj *inObjp, char *flag, int mode)
{
    struct struct_9db062e8 *m = &inObjp->dt.model;

    if (m->kind != 3) return;
    struct stMenuAC *acp = m->acp;
    if (!acp || !acp->connect.cur.mi2p) return;
    for (int i = 0; i < 4; i++)
        if (flag[i]) acp->camouflage[i] = (unsigned char)mode;
}

static void __MOBJ_MakeVu1PacketModelAC(struct stMobj *inObjp)
{
    struct struct_9db062e8 *m = &inObjp->dt.model;

    PS2_FRAME(0x1c0);
    PS2_STACK(struct struct_af7eae9d, view, 0x20);
    PS2_STACK(int[4], uv, 0x1a0);
    PS2_STACK(struct struct_76b810ec, col, 0x1bc);
    uv[0] = 0; uv[1] = 0; uv[2] = 1; uv[3] = 1;
    col = mobj_col(0x80000000u);

    unsigned int *pkt = MPktCurrentp;
    struct stMenuAC *acp = m->acp;
    if (!acp) return;
    if (!inObjp->f_use || inObjp->f_erase == 1) return;
    if (m->kind == 3 && !acp->connect.cur.mi2p) return;
    for (int i = 0; i < m->add_num; i++)
        if (!m->add_mp[i]) return;

    /* hide parts: flag.draw = 0 */
    if (m->dispflag & 0x100000) {
        static const int real[] = {0, 1, 2, 3, 4, 5, 6, 12, 13};
        for (int i : real)
            if (acp->real_part[i]) acp->real_part[i]->flag.draw = 0;
    } else {
        /* cur_part[13] is hidden only when [12] is set, and without a NULL test */
        if (m->acp->cur_part[12]) {
            m->acp->cur_part[12]->flag.draw = 0;
            m->acp->cur_part[13]->flag.draw = 0;
        }
        for (int i = 14; i < 18; i++)
            if (m->acp->cur_part[i]) m->acp->cur_part[i]->flag.draw = 0;
    }

    pkt = DF_MPKT_MakeVIFCode(pkt, 0, 0, 0, MOBJ_VIF_FLUSHA);
    pkt = DF_MPKT_MakeGSRegister(pkt, MOBJ_K_TEST, 0x32001);
    pkt = DF_MPKT_MakeTexSpriteI(pkt, MOBJ_TEX_MODEL_BG, &m->show_rect, uv, &col, 0);
    if (!(m->dispflag & 0x40000)) {
        MWorkViewp->ox = (m->cx + m->off_x) - 320.0f;
        MWorkViewp->oy = ((m->cy + m->off_y) - 224.0f) / 2.0f;
        MPktCurrentp = pkt;
        DF_MVIEW_MakeWorkVU1Packet();
    } else {
        memcpy(&view, MViewp, sizeof(view));
        view.cam.pos[0] = view.cam.pos[0] - m->pos[0];   /* camera moved by the object's position */
        view.cam.pos[1] = view.cam.pos[1] - m->pos[1];
        view.cam.pos[2] = view.cam.pos[2] - m->pos[2];
        MPktCurrentp = pkt;
        DF_MVIEW_MakeVU1PacketMyView(&view);
    }
    pkt = DF_MPKT_MakeGSRegister(MPktCurrentp, MOBJ_K_SCISSOR1, mobj_scissor(&m->show_rect));
    pkt = DF_MPKT_MakeGSRegister(pkt, MOBJ_K_SCISSOR2, mobj_scissor(&m->show_rect));
    MPktCurrentp = pkt;
    DF_M_AC_OneACMakeVu1Packet(m->acp, 5);
    pkt = DF_MPKT_MakeVIFCode(MPktCurrentp, 0, 0, 0, MOBJ_VIF_FLUSHA);
    pkt = DF_MPKT_MakeGSRegister(pkt, MOBJ_K_SCISSOR1, MOBJ_SCISSOR_FULL);
    pkt = DF_MPKT_MakeGSRegister(pkt, MOBJ_K_SCISSOR2, MOBJ_SCISSOR_FULL);
    MPktCurrentp = pkt;
    DF_MVIEW_MakeVU1Packet();
}

/* drops the packet built so far (MPktCurrentp not advanced) while the model is still loading (read_step < 3) */
static void __MOBJ_MakeVu1PacketModel(struct stMobj *inObjp)
{
    struct struct_9db062e8 *m = &inObjp->dt.model;

    PS2_FRAME(0x70);
    PS2_STACK(int[4], uv, 0x50);
    PS2_STACK(struct struct_76b810ec, col, 0x6c);
    uv[0] = 0; uv[1] = 0; uv[2] = 1; uv[3] = 1;
    col = mobj_col(0x80000000u);

    unsigned int *pkt = MPktCurrentp;
    if (!m->mp) return;
    for (int i = 0; i < m->add_num; i++)
        if (!m->add_mp[i]) return;

    pkt = DF_MPKT_MakeVIFCode(pkt, 0, 0, 0, MOBJ_VIF_FLUSHA);
    pkt = DF_MPKT_MakeGSRegister(pkt, MOBJ_K_TEST, 0x32001);
    pkt = DF_MPKT_MakeTexSpriteI(pkt, MOBJ_TEX_MODEL_BG, &m->show_rect, uv, &col, 0);
    if (m->read_step < 3) return;
    if (m->mp) {
        pkt = DF_MPKT_MakeGSRegister(pkt, MOBJ_K_SCISSOR1, mobj_scissor(&m->show_rect));
        pkt = DF_MPKT_MakeGSRegister(pkt, MOBJ_K_SCISSOR2, mobj_scissor(&m->show_rect));
        switch (m->scene_type) {   /* model position from the object's centre + offset */
        case 4: case 3:
            m->mp->pos[0] = (-9.0f + m->cx) + m->off_x;
            m->mp->pos[1] = (-5.0f + m->cy) + m->off_y;
            m->mp->pos[2] = -0.0f;
            break;
        case 2:
            m->mp->pos[0] = m->cx + m->off_x;
            m->mp->pos[1] = (-0x1.866666p+2f /* -6.1f */ + m->cy) + m->off_y;
            m->mp->pos[2] = 0.0f;
            break;
        case 1:
            m->mp->pos[0] = (-9.0f + m->cx) + m->off_x;
            m->mp->pos[1] = (-0x1.a66666p+2f /* -6.6f */ + m->cy) + m->off_y;
            m->mp->pos[2] = -2.0f;
            break;
        }
        if (m->kind == 1) {   /* AC part: camouflage pattern = the part id's thousands digit */
            int q = m->mp->id / 1000;
            struct stMenuAC *pAC = m->pAC;
            if (q == 3 || q == 2 || q == 1 || q == 0) DF_M_AC_SetCamoPettern(pAC, m->mp, q);
            pkt = DF_MMODEL_MakeVu1PacketAC(pkt, m->mp, pAC, 4);
            for (int i = 0; i < m->add_num; i++)
                if (m->add_mp[i]) pkt = DF_MMODEL_MakeVu1PacketAC(pkt, m->add_mp[i], pAC, 4);
        } else {
            pkt = DF_MMODEL_MakeVu1PacketAC(pkt, m->mp, 0, 3);
            for (int i = 0; i < m->add_num; i++)
                if (m->add_mp[i]) pkt = DF_MMODEL_MakeVu1PacketAC(pkt, m->add_mp[i], 0, 3);
        }
        pkt = DF_MPKT_MakeVIFCode(pkt, 0, 0, 0, MOBJ_VIF_FLUSHA);
    }
    pkt = DF_MPKT_MakeGSRegister(pkt, MOBJ_K_SCISSOR1, MOBJ_SCISSOR_FULL);
    pkt = DF_MPKT_MakeGSRegister(pkt, MOBJ_K_SCISSOR2, MOBJ_SCISSOR_FULL);
    MPktCurrentp = pkt;
    DF_MVIEW_MakeVU1Packet();
}

/* private copy of the menu AC (the allocation is not NULL-tested) without the parts at real_part[12..19] unless
 * dispflag bit 20 is set; scale 0.4 */
static void __MOBJ_InitACInfo(struct stMobj *ioObjp)
{
    struct struct_9db062e8 *m = &ioObjp->dt.model;

    PS2_FRAME(0x40);
    PS2_STACK(float[4], scl, 0x30);
    scl[0] = 0x1.99999ap-2f;   /* 0.4f */
    scl[1] = 0x1.99999ap-2f;
    scl[2] = 0x1.99999ap-2f;
    scl[3] = 1.0f;

    struct stMenuAC *pAC = m->pAC;
    m->acp = (struct stMenuAC *)DF_MMM_Alloc(2, 0x3e0);
    DF_M_AC_Copy(m->acp, pAC);
    if (!(m->dispflag & 0x100000)) {
        for (int i = 12; i < 20; i++) {
            if (m->acp->real_part[i]) {
                DF_MMODEL_DeleteModelAndMMD(m->acp->real_part[i]);
                m->acp->real_part[i] = 0;
            }
        }
    }
    memcpy(m->acp->scale, scl, 16);
}

/* the AC copy is made on the first call after the menu AC has finished loading; fades alpha + show rect over 6
 * frames and turns the AC by 0.5 degree per frame, wrapping at pi */
static void __MOBJ_PlayModelAC(struct stMobj *ioObjp)
{
    struct struct_9db062e8 *m = &ioObjp->dt.model;

    PS2_FRAME(0x30);
    if (m->read_step == 0) {
        if (m->pAC->flag.reading == 1) return;
        __MOBJ_InitACInfo(ioObjp);
        m->read_step = 1;
    }
    if (ioObjp->stat == 1 || ioObjp->stat == 2) {
        unsigned char old = ioObjp->count++;
        if (old < 6) {
            int c = ioObjp->count;
            m->cur_alpha = (unsigned char)(((6 - c) * m->prev_alpha + m->next_alpha * c) / 6);
            DF_MFUNC_CalcInterRect(&m->show_rect, &m->p_show_rect, &m->n_show_rect, ioObjp->count, 6);
        } else {
            m->cur_alpha = m->next_alpha;
            ioObjp->stat = ioObjp->stat == 1 ? 0 : 5;
        }
    }
    float ay = m->ay + 0x1.1df46ap-7f;   /* 0.0087266f */
    m->ay = ay;
    if (!(ay < 0x1.921fb6p+1f)) m->ay = -0x1.921fb6p+1f;   /* pi */
    m->acp->ang[1] = m->ay;
    if (m->acp->play_func) gcall_as<void(struct stMenuAC *)>(m->acp->play_func, m->acp);
}

/* ---- menu models (MMD): set-up after loading, per-frame play, the arm-part attachments */

/* the model's info record (mp->prm.part.infop): size reference +0xc0 (float), x / y offset +0xc4 / +0xc6 and x
 * angle in degrees +0xc8 (shorts) */
static inline float mobj_info_f(const void *info, int off) { return *(const float *)((const char *)info + off); }
static inline short mobj_info_s(const void *info, int off) { return *(const short *)((const char *)info + off); }

/* model placement: parts 0xa0..0xa2 of model kind 2 get a fixed pose; otherwise the scale fits the model's size
 * reference to 20 (at most 2.0, also handed to the add models), the info record shifts off_x / off_y and tilts the
 * model about x; ids 9xxx are tilted by another 90 degrees */
static void __MOBJ_SetModelVec(struct stMobj *ioObjp)
{
    struct struct_9db062e8 *m = &ioObjp->dt.model;
    struct _stMModel *mp = m->mp;

    PS2_FRAME(0x20);
    int id = mp->id;
    if (id >= 0xa0 && id <= 0xa2 && mp->kind == 2) {
        mp->pos[0] = -9.0f;
        mp->pos[1] = -0x1.733334p+2f;   /* -5.8f */
        mp->pos[2] = -2.0f;
        mp->scale[0] = 0x1.666666p-1f;  /* 0.7f */
        mp->scale[1] = 0x1.666666p-1f;
        mp->scale[2] = 0x1.666666p-1f;
        mp->alpha_reg = 0x48;
        return;
    }
    mp->pos[0] = 0.0f;
    mp->pos[1] = 0.0f;
    mp->pos[2] = 0.0f;
    mp->pos[3] = 1.0f;
    void *info = mp->prm.part.infop;
    if (info) {
        mp->scale[0] = ps2_div(20.0f, mobj_info_f(info, 0xc0));
        mp->scale[1] = ps2_div(20.0f, mobj_info_f(info, 0xc0));
        mp->scale[2] = ps2_div(20.0f, mobj_info_f(info, 0xc0));
        if (!(mp->scale[0] <= 2.0f)) mp->scale[0] = 2.0f;
        if (!(mp->scale[1] <= 2.0f)) mp->scale[1] = 2.0f;
        if (!(mp->scale[2] <= 2.0f)) mp->scale[2] = 2.0f;
        for (int i = 0; i < 3; i++)
            if (m->add_mp[i]) memcpy(m->add_mp[i]->scale, mp->scale, 16);
        m->off_x = m->off_x + 0x1.99999ap-5f * (float)mobj_info_s(info, 0xc4);   /* 0.05f */
        m->off_y = m->off_y + 0x1.99999ap-5f * (float)mobj_info_s(info, 0xc6);
        mp->ang[0] = DF_MFUNC_AngForm(0x1.1df46ap-6f * (float)mobj_info_s(info, 0xc8));   /* pi / 180 */
    } else {
        mp->pos[2] = 10.0f;
        mp->ang[0] = 0.0f;
    }
    if (mp->id / 1000 == 9) mp->ang[0] = mp->ang[0] + 0x1.921fb6p+0f;   /* pi / 2 */
}

/* after the MMD is loaded: place the model (rotation y = init_ay), then for AC parts (model kind 1, part class =
 * id / 1000) colours, start animations and pick the visible sub-models; arm parts load their hand model as
 * add_mp[0] (gun arms: MMD 0x3a99 + category / 2; category 7: a copy of the arm's own MMD) */
static void __MOBJ_SetModelInfo(struct stMobj *ioObjp)
{
    struct struct_9db062e8 *m = &ioObjp->dt.model;

    PS2_FRAME(0x90);
    PS2_STACK(float[4], pos, 0x80);
    PS2_STACK(float[4], rot, 0x70);
    PS2_STACK(float[4], scl, 0x60);
    pos[0] = 0.0f; pos[1] = 0.0f; pos[2] = 0.0f; pos[3] = 1.0f;
    rot[0] = 0.0f; rot[1] = m->init_ay; rot[2] = 0.0f; rot[3] = 1.0f;
    scl[0] = 1.0f; scl[1] = 1.0f; scl[2] = 1.0f; scl[3] = 1.0f;

    struct _stMModel *mp = m->mp;
    DF_MMODEL_SetData(mp, mp->mmdp, &pos, &rot, &scl);
    if (DF_MMODEL_IsWBackWeapon(m->mp) == 1) m->f_model = 2;
    if (mp->kind == 1) {
        int cls = mp->id / 1000;
        if (cls >= 0 && cls <= 3) {
            if (m->p_colp || m->p_colp2) DF_MMODEL_SetPartsCluts(mp, m->p_colp, m->p_colp2);
        } else if (cls >= 10 && cls <= 12) {
            if (m->p_colp) DF_MMODEL_SetWepColor(mp, m->p_colp);
        }
        switch (cls) {
        case 0:
        case 4:
        case 11:
        case 12:
            DF_MMODEL_SetAnim(mp, 0, 0, 1, 0);
            break;
        case 1:
            for (int i = 0; i < mp->mdl_num; i++) DF_MMODEL_SetAnim(mp, i, i, 1, 1);
            break;
        case 2: {   /* arms */
            DF_MMODEL_SetAnim(mp, 0, m->f_model ? 1 : 0, 1, 0);
            int cat = DF_MPART_GetCategory(mp->id);
            if (cat < 0) break;
            if (DF_MPART_GetCategory(mp->id) != 7) {
                struct stMmdData *mmd = DF_MMD_GetMMD(1, cat / 2 + 0x3a99);
                if (!mmd) break;
                if (!(m->add_mp[0] = DF_MMODEL_GetNewModel(mp))) break;
                DF_MMODEL_SetData(m->add_mp[0], mmd, 0, 0, &scl);
                m->add_num = 1;
                if (m->mp->mdl_num == 0) break;
                struct struct_c4910010 *col;
                if (m->f_model == 0) {
                    m->add_mp[0]->mdlp[1].f_draw = 0;
                    col = m->p_colp;
                    mp->mdlp[0].objp[3].f_draw = 0;
                    mp->mdlp[2].f_draw = 0;
                } else {
                    m->add_mp[0]->mdlp[0].f_draw = 0;
                    col = m->p_colp2;
                    mp->mdlp[0].objp[7].f_draw = 0;
                    mp->mdlp[1].f_draw = 0;
                }
                DF_MMODEL_SetPartsCluts(m->add_mp[0], col, 0);
                break;
            }
            /* category 7: the copy shows only sub-model 3 / 4; subcategories 11..20 (blade arms) skip that with no
             * sub-models and colour the copy with clut 1 only */
            int blade = DF_MPART_GetCategory(mp->id) == 7 && DF_MPART_GetSubCategory(mp->id) >= 11 &&
                        DF_MPART_GetSubCategory(mp->id) < 21;
            for (int i = 1; i < mp->mdl_num; i++) mp->mdlp[i].f_draw = 0;
            struct stMmdData *mmd = mp->mmdp;
            if (!(m->add_mp[0] = DF_MMODEL_GetNewModel(mp))) break;
            DF_MMODEL_SetData(m->add_mp[0], mmd, 0, 0, &scl);
            m->add_num = 1;
            if (blade && m->mp->mdl_num == 0) break;
            int show;
            if (m->f_model == 0) {
                mp->mdlp[1].f_draw = 1;
                show = 3;
            } else {
                mp->mdlp[2].f_draw = 1;
                show = 4;
            }
            for (int i = 0; i < m->add_mp[0]->mdl_num; i++) m->add_mp[0]->mdlp[i].f_draw = i == show;
            struct struct_c4910010 *col;
            if (m->f_model == 0) {
                m->add_mp[0]->prm.part.connect_dp_id = 8;
                col = m->p_colp;
            } else {
                m->add_mp[0]->prm.part.connect_dp_id = 0xa;
                col = m->p_colp2;
            }
            DF_MMODEL_SetPartsCluts(m->add_mp[0], col, blade ? 0 : col);
            break;
        }
        case 3:
            if (DF_MPART_GetCategory(mp->id) == 0xd || DF_MPART_GetCategory(mp->id) == 0xf) {
                for (int i = 0; i < mp->mdl_num; i++) DF_MMODEL_SetAnim(mp, i, i, 1, 1);
            } else {
                DF_MMODEL_SetAnim(mp, 0, 0, 1, 0);
            }
            break;
        case 5:
        case 6:
        case 7:
        case 8:
            mp->mdlp[0].f_draw = 0;
            break;
        case 9:
            if (mp->mdlp && mp->mmdp->OMD.num >= 2) mp->mdlp[1].f_draw = 0;
            DF_MMODEL_SetAnim(mp, 0, 0, 1, 0);
            break;
        case 10:
            if (m->f_model == 2) {
                /* W-back weapon: a copy of the model draws sub-model 1 */
                if (!(m->add_mp[0] = DF_MMODEL_GetNewModel(mp))) break;
                DF_MMODEL_SetData(m->add_mp[0], mp->mmdp, 0, 0, &m->mp->scale);
                DF_MMODEL_SetWepColor(m->mp, m->p_colp2);
                DF_MMODEL_SetWepColor(m->add_mp[0], m->p_colp2);
                m->add_num = 1;
                mp->mdlp[1].f_draw = 0;
                mp->f_tm2off[1] = 1;
                m->add_mp[0]->mdlp[0].f_draw = 0;
                m->add_mp[0]->f_tm2off[0] = 1;
                DF_MMODEL_SetAnim(mp, 0, 0, 1, 0);
                DF_MMODEL_SetAnim(m->add_mp[0], 1, 1, 1, 0);
            } else {
                int s = m->f_model == 1 ? 1 : 0;
                for (int i = 0; i < mp->mmdp->OMD.num; i++) {
                    if (i == s) {
                        mp->mdlp[i].f_draw = 1;
                        mp->f_tm2off[i] = 0;
                    } else {
                        mp->mdlp[i].f_draw = 0;
                        mp->f_tm2off[i] = 1;
                    }
                }
                DF_MMODEL_SetAnim(mp, s, s, 1, 0);
            }
            break;
        default:
            break;
        }
    }
    __MOBJ_SetModelVec(ioObjp);
    m->scaleTemp[0] = mp->scale[0];
    m->scaleTemp[1] = mp->scale[1];
    m->scaleTemp[2] = mp->scale[2];
}

/* reads the MMD (read_step 0 -> 3), fades alpha + show rect, turns the model by 0.5 degree per frame (not while
 * the help is up) and plays it; arm parts then attach their hand. The type moves the model between the main (5)
 * and the 2ND (8) draw pass every frame. */
static void __MOBJ_PlayModel(struct stMobj *ioObjp)
{
    struct struct_9db062e8 *m = &ioObjp->dt.model;
    struct _stMModel *mp = m->mp;

    PS2_FRAME(0x50);
    ioObjp->type = MenuMng.f_asem_detail_on ? 5 : 8;
    switch (m->read_step) {
    case 0:
        if (DF_MMD_ReadStart(mp->mmdp, mp->kind, mp->id, 0x10000) >= 0) m->read_step++;
        break;
    case 1: {
        int r = DF_MMD_ReadSync(0);
        if (r == 0) return;
        if (r == -1) {
            DF_MENU_NotifyHddError(0);
            m->read_step = 3;
            return;
        }
        m->read_step++;
    }
        /* fall through */
    case 2:
        if (mp->mmdp->f_set) {
            __MOBJ_SetModelInfo(ioObjp);
            m->read_step++;
        }
        break;
    }
    if (m->read_step < 3) return;

    if (ioObjp->stat == 1 || ioObjp->stat == 2) {
        unsigned char old = ioObjp->count++;
        if (old < 6) {
            int c = ioObjp->count;
            m->cur_alpha = (unsigned char)(((6 - c) * m->prev_alpha + m->next_alpha * c) / 6);
            DF_MFUNC_CalcInterRect(&m->show_rect, &m->p_show_rect, &m->n_show_rect, ioObjp->count, 6);
        } else {
            m->cur_alpha = m->next_alpha;
            ioObjp->stat = ioObjp->stat == 1 ? 0 : 5;
        }
    }
    mp->mulalpha = (float)m->cur_alpha / 128.0f;
    if (MenuMng.f_help == 0) {
        float ay = m->ay + 0x1.1df46ap-7f;   /* 0.0087266f */
        m->ay = ay;
        if (!(ay < 0x1.921fb6p+1f)) m->ay = -0x1.921fb6p+1f;   /* pi */
        mp->ang[1] = mp->kind == 2 ? 0x1.e5e65p+1f : DF_MFUNC_AngForm(m->ay);   /* 3.7960911f (0x4072f328) */
    }
    if (m->dispflag & 0x40000) {
        m->mp->pos[0] = m->pos[0] + m->off_x;
        m->mp->pos[1] = m->pos[1] + m->off_y;
        m->mp->pos[2] = m->pos[2];
    }
    if (m->dispflag & 0x80000) {
        m->mp->scale[0] = m->scale[0];
        m->mp->scale[1] = m->scale[1];
        m->mp->scale[2] = m->scale[2];
    }
    if (m->dispflag & 0x200000) {
        m->mp->scale[0] = m->scaleTemp[0] * m->scale[0];
        m->mp->scale[1] = m->scaleTemp[1] * m->scale[1];
        m->mp->scale[2] = m->scaleTemp[2] * m->scale[2];
    }
    DF_MMODEL_Play(mp);
    if (mp->id / 1000 == 2) {
        DF_MPART_GetSubCategory(mp->id);   /* result unused */
        if (DF_MPART_GetCategory(mp->id) != 7) {
            __MOBJ_PlayModelSubArm(ioObjp);
            return;
        }
        int blade = DF_MPART_GetCategory(mp->id) == 7 && DF_MPART_GetSubCategory(mp->id) >= 11 &&
                    DF_MPART_GetSubCategory(mp->id) < 21;
        if (blade)
            __MOBJ_PlayModelSubSwdArm(ioObjp);
        else
            __MOBJ_PlayModelSubWepArm(ioObjp);
    } else if (m->f_model == 2) {
        m->add_mp[0]->mulalpha = (float)m->cur_alpha / 128.0f;
        memcpy(m->add_mp[0]->pos, mp->pos, 16);
        memcpy(m->add_mp[0]->ang, mp->ang, 16);
        memcpy(m->add_mp[0]->scale, mp->scale, 16);
        DF_MMODEL_Play(m->add_mp[0]);
    }
}

/* M = the matrix of object idx of sub-model 0 with its translation row replaced by the data polygon's vertex (the
 * attachment point); every object of sub-model f_model + 1 (the hand) gets local_world_mat = M * (rotation(ang) +
 * translation(pos)): the original's inline VU0 multiply, = sceVu0MulMatrix(lwm, M, lwm) */
static void mobj_arm_attach(struct _stMModel *mp, int f_model, int idx, float (*M)[4], const float *vert)
{
    sceVu0CopyMatrix(M, mp->mdlp[0].objp[idx].local_world_mat);
    memcpy(M[3], vert, 16);
    struct struct_45c47ec5 *e = &mp->mdlp[f_model + 1];
    for (int j = 0; j < e->nobj; j++) {
        struct __mmodel_object_st *node = &e->objp[j];
        DF_MFUNC_RotMatrix(node->local_world_mat, IdentityMtx, node->ang);
        sceVu0TransMatrix(node->local_world_mat, node->local_world_mat, node->pos);
        vu_mul_matrix(node->local_world_mat, (const float (*)[4])M, (const float (*)[4])node->local_world_mat);
    }
}

/* blade arms (category 7, subcategories 11..20): the hand follows data polygon 0x28 / 0x29, the blade model
 * add_mp[0] (if any) is connected to the arm */
static void __MOBJ_PlayModelSubSwdArm(struct stMobj *ioObjp)
{
    struct struct_9db062e8 *m = &ioObjp->dt.model;
    struct _stMModel *mp = m->mp;

    PS2_FRAME(0xc0);
    PS2_STACK(float[4][4], M, 0x80);
    PS2_STACK(float[4], vert, 0x70);
    PS2_STACK(float[4], norm, 0x60);
    int idx = DF_MMODEL_GetDataPoly(mp, 0, m->f_model ? 0x29 : 0x28, vert, norm);
    if (idx >= 0) mobj_arm_attach(mp, m->f_model, idx, M, vert);
    struct _stMModel *add = m->add_mp[0];
    if (add) {
        DF_M_AC_ConnectPartByID(add, mp, 0);
        add->mulalpha = mp->mulalpha;
        DF_MMODEL_Play(add);
    }
}

/* other category-7 arms: object 0 / 4 first gets the arm's matrix times an x rotation by AngForm(0) (its own
 * translation row kept), then the hand follows data polygon 0x28 / 0x29 */
static void __MOBJ_PlayModelSubWepArm(struct stMobj *ioObjp)
{
    struct struct_9db062e8 *m = &ioObjp->dt.model;
    struct _stMModel *mp = m->mp;

    PS2_FRAME(0xc0);
    PS2_STACK(float[4][4], M, 0x80);
    PS2_STACK(float[4], vert, 0x70);
    PS2_STACK(float[4], norm, 0x60);
    struct __mmodel_object_st *base = m->f_model ? &mp->mdlp[0].objp[4] : mp->mdlp[0].objp;
    int dp = m->f_model ? 0x29 : 0x28;
    memcpy(vert, base->local_world_mat[3], 16);
    sceVu0RotMatrixX(M, IdentityMtx, DF_MFUNC_AngForm(0.0f));
    vu_mul_matrix(base->local_world_mat, (const float (*)[4])mp->local_world_mat, (const float (*)[4])M);
    memcpy(base->local_world_mat[3], vert, 16);
    int idx = DF_MMODEL_GetDataPoly(mp, 0, dp, vert, norm);
    if (idx >= 0) mobj_arm_attach(mp, m->f_model, idx, M, vert);
    struct _stMModel *add = m->add_mp[0];
    if (add) {
        DF_M_AC_ConnectPartByID(add, mp, 0);
        add->mulalpha = mp->mulalpha;
        DF_MMODEL_Play(add);
    }
}

/* gun arms: the hand objects follow data polygon 0x28 / 0x29; the hand model add_mp[0] takes the matrix of the
 * arm's object 3 / 7 into the first object of its sub-model f_model */
static void __MOBJ_PlayModelSubArm(struct stMobj *ioObjp)
{
    struct struct_9db062e8 *m = &ioObjp->dt.model;
    struct _stMModel *mp = m->mp;

    PS2_FRAME(0xd0);
    PS2_STACK(float[4][4], M, 0x90);
    PS2_STACK(float[4], vert, 0x80);
    PS2_STACK(float[4], norm, 0x70);
    int obj = m->f_model ? 7 : 3;
    int idx = DF_MMODEL_GetDataPoly(mp, 0, m->f_model ? 0x29 : 0x28, vert, norm);
    if (idx >= 0) mobj_arm_attach(mp, m->f_model, idx, M, vert);
    if (!m->mp->mdl_num) return;
    struct _stMModel *add = m->add_mp[0];
    if (!add) return;
    struct __mmodel_object_st *src = &mp->mdlp[0].objp[obj];
    struct struct_45c47ec5 *dst = &add->mdlp[(int)m->f_model];
    if (!src || !dst) return;
    sceVu0CopyMatrix(dst->objp->local_world_mat, src->local_world_mat);
    add->flag.calc_objmat = 1;
    add->mulalpha = mp->mulalpha;
    DF_MMODEL_Play(add);
}

/* kind 3: the AC copy goes back to the menu memory (acp itself is not cleared); models: mp / add_mp, AC parts
 * (model kind 1) with their MMD */
static void __MOBJ_DeleteModel(struct stMobj *ioObjp)
{
    struct struct_9db062e8 *m = &ioObjp->dt.model;

    PS2_FRAME(0x40);
    if (m->kind == 3) {
        struct stMenuAC *acp = m->acp;
        if (acp) {
            acp->connect.cur.mi2p = 0;
            DF_M_AC_Reset(acp);
            DF_MMM_Free(2, acp);
        }
        return;
    }
    for (int i = 0; i < m->add_num; i++) {
        struct _stMModel *mp = m->add_mp[i];
        if (!mp) continue;
        if (mp->kind == 1) DF_MMODEL_DeleteModelAndMMD(mp);
        else DF_MMODEL_DeleteModel(mp);
        m->add_mp[i] = 0;
    }
    if (m->mp) {
        if (m->mp->kind == 1) DF_MMODEL_DeleteModelAndMMD(m->mp);
        else DF_MMODEL_DeleteModel(m->mp);
        m->mp = 0;
    }
}

void DF_MOBJ_SetModelACColor(struct stMobj *ioObjp, struct struct_098975f3 *inACColp)
{
    struct struct_9db062e8 *m = &ioObjp->dt.model;

    PS2_FRAME(0x10);
    if (m->kind == 3 && m->acp) DF_M_AC_UpdateClut(m->acp, inACColp, 1);
}

/* AC: the first five part colours all set to *inPColp (the other six entries of the table are the stale stack the
 * original hands over); models: the cluts of mp and every add_mp */
void DF_MOBJ_SetModelColor(struct stMobj *ioObjp, struct struct_c4910010 *inPColp)
{
    struct struct_9db062e8 *m = &ioObjp->dt.model;

    PS2_FRAME(0x100);
    PS2_STACK(struct struct_098975f3, col, 0x50);
    if (m->kind == 3) {
        if (!m->acp) return;
        for (int i = 0; i < 5; i++) memcpy(&col.p_col[i], inPColp, sizeof(col.p_col[i]));
        DF_M_AC_UpdateClut(m->acp, &col, 0);
        return;
    }
    if (!m->mp) return;
    DF_MMODEL_SetPartsCluts(m->mp, inPColp, inPColp);
    for (int i = 0; i < m->add_num; i++)
        if (m->add_mp[i]) DF_MMODEL_SetPartsCluts(m->add_mp[i], inPColp, inPColp);
}

/* 1 once the model can be drawn: AC copies from read_step 1, models from read_step 3 */
int DF_MOBJ_ModelReadSync(struct stMobj *inObjp)
{
    if (!inObjp) return 1;
    if (inObjp->dt.model.kind == 3) return inObjp->dt.model.read_step > 0;
    return inObjp->dt.model.read_step >= 3;
}

/* kind 3 = a menu AC (pAC, else the player's menu AC), other kinds = a model loaded from the MMD (kind, id); an AC
 * part (kind 1) with id % 1000 == 0 becomes the kind 2 model 0xa0..0xa2 picked by the low half of inDispFlag. The
 * low half of inDispFlag is the scene type (model offset in the packet), the high half the display flags. */
struct stMobj *DF_MOBJ_NewModelAC(struct stMobj *inObjp, int inKind, int inID, int inFlag, struct stMenuRect *inRectp,
                                  struct struct_c4910010 *inPColp, struct struct_c4910010 *inPColp2, int inAlpha,
                                  float inAY, struct stMenuAC *pAC, int player, int inDispFlag, float offsetX,
                                  float offsetY)
{
    PS2_FRAME(0xb0);
    unsigned short scene = (unsigned short)inDispFlag;
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    struct struct_9db062e8 *m = &obj->dt.model;

    obj->type = 8;
    if (inID % 1000 == 0 && inKind == 1) {
        inID = scene == 4 ? 0xa2 : scene == 3 ? 0xa1 : 0xa0;
        inKind = 2;
    }
    m->kind = (short)inKind;
    m->id = (short)inID;
    m->pAC = pAC ? pAC : M_ACp[player][0];
    m->rect = *inRectp;
    m->show_rect = *inRectp;
    m->cur_alpha = (unsigned char)inAlpha;
    m->prev_alpha = m->cur_alpha;
    m->next_alpha = m->cur_alpha;
    m->mdl_num = 0;
    m->f_model = (char)inFlag;
    m->init_ay = inAY;
    m->ay = inAY;
    m->p_colp = inPColp;
    m->p_colp2 = inPColp2;
    m->cx = 0;
    m->cy = 0;
    m->off_x = offsetX;
    m->off_y = offsetY;
    m->add_num = 0;
    m->dispflag = inDispFlag & 0xffff0000;
    for (int i = 0; i < 4; i++) MWorkViewp->cam.pos[i] = i == 3 ? 1.0f : 0.0f;
    for (int i = 0; i < 4; i++) MWorkViewp->cam.ang[i] = i == 3 ? 1.0f : 0.0f;

    if (m->kind == 3) {
        if (!m->pAC->flag.reading) {
            __MOBJ_InitACInfo(obj);
            m->read_step = 1;
        }
        obj->play_func = (mobj_fn)GFN(__MOBJ_PlayModelAC);
        obj->pkt_func = (mobj_fn)GFN(__MOBJ_MakeVu1PacketModelAC);
        return obj;
    }
    struct stMmdData *mmd = DF_MMD_GetMMD(m->kind, m->id);
    if (!mmd) {
        mmd = DF_MMD_GetNewMMD(0);
        mmd->kind = (unsigned char)m->kind;
        mmd->id = m->id;
        m->read_step = 0;
    } else {
        m->read_step = 2;
    }
    m->mp = DF_MMODEL_GetNewModel(0);
    m->scene_type = scene;
    m->ay = -0x1.709a48p+1f;   /* -2.8797083f (0xc0384d24): overrides inAY */
    m->mp->mmdp = mmd;
    m->mp->kind = mmd->kind;
    m->mp->id = mmd->id;
    obj->play_func = (mobj_fn)GFN(__MOBJ_PlayModel);
    obj->pkt_func = (mobj_fn)GFN(__MOBJ_MakeVu1PacketModel);
    return obj;
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper; pAC / player / inDispFlag are the caller's stack args at sp+0 / 8 / 0x10, the
 * floats come in $f12..$f14 */
static void bw__DF_MOBJ_NewModelAC(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    struct stMobj *r = DF_MOBJ_NewModelAC(
        (struct stMobj *)bridge_ptr(c->gpr[A0].uw[0]), c->gpr[A1].sw[0], c->gpr[A2].sw[0], c->gpr[A3].sw[0],
        (struct stMenuRect *)bridge_ptr(c->gpr[T0].uw[0]), (struct struct_c4910010 *)bridge_ptr(c->gpr[T1].uw[0]),
        (struct struct_c4910010 *)bridge_ptr(c->gpr[T2].uw[0]), c->gpr[T3].sw[0], c->fpr[12].f,
        (struct stMenuAC *)bridge_ptr(rd32(sp)), (int)rd32(sp + 8), (int)rd32(sp + 0x10), c->fpr[13].f,
        c->fpr[14].f);
    c->gpr[V0].sd[0] = (s32)PS2PTR(r);
}
#endif
