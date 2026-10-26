/* src/menu/membe.cpp - MEMBE, the emblem editor ("EME", the paint-the-emblem screen reached from the garage Paint
 * sub-menu; OLMENU overlay). DF_MEMBE_NewSystem carves a 0x176e0-byte work block (struct_fedcf029, dlp) out of
 * MenuWorkp: the 128x128 8-bit edit images, layer / scope / palette buffers, CLUTs, the editor's mobj pointers and
 * the cursor / zoom / grid state. The editor is a stack of small systems (Edit, Draw / DrawRot, Palette, Func) with
 * Dispatch / FadeIn / FadeOut play functions; the drawing tools themselves live in mefunc.c.
 * Group 1 native: the leaves __EME_SetTexInfo (GS TEX0 / TEX1 words of the seven editor textures),
 * __EME_ChangeColor2Idx (eyedropper: palette cell of the pixel under the cursor) and __EME_Layer_DrawBox (filled or
 * outlined box into a 64-pixel-wide byte buffer).
 * Group 2 native: __DeleteWindowList / __FadeOutWindowList (the editor's window list: icons, frames, palette / preview
 * windows, cursor / zoom / grid strings, layers) and the sub-menu icon builders __InitSubMenuIcon + __Init{Rotation,
 * Font, Shape, Draw, Palette} (reached only through the Func system's subFunc_init pointer, group 7).
 * Group 3 native: __FadeInWindowList (builds the whole editor screen at entry: buffers, GS uploads, icons, edit frames,
 * edit / palette / preview windows, cursor / zoom / grid strings) and __EME_PanelMove (slides the palette / preview
 * panel list in 6 steps).
 * Group 4 native: __EME_DoFunction (runs the selected drawing tool), the palette system (__SetNewPaletteSystem +
 * FadeIn / Play / FadeOut: palette cursor, undo swap, grid / zoom rates) and the Draw-system helpers Dispatch / Move /
 * FadeOut (shared by Draw and DrawRot).
 * Group 5 native: __PlayEmbDrawRotSystem (Rot panel cursor loop).
 * Group 6 native: the Draw system (__SetNewDrawSystem, FadeIn, __PlayEmbDrawSystem: cursor, eyedropper, palette).
 * Group 7 native: the Func system (function panel: __SetNewFuncSystem, FadeIn, __PlayEmbFuncSystem sub-icon cursor +
 * funcList dispatch, Dispatch, the fade-out pair).
 * Group 8 native: __DrawEmbEditSystem (the editor's per-frame draw: pad / mouse refresh, status digits, CLUT
 * animation, GS uploads, zoom view, scope, tool preview layer, palette range layer, edge-frame lights).
 * Group 9 native: the Edit system (DF_MEMBE_NewSystem: work block + cursor init; FadeIn: texture file read + screen
 * build; __PlayEmbEditSystem: main loop; Dispatch: child / confirm dialog wait; FadeOut: save + restore) and the empty
 * __PktEmbEditSystem. */
#include <string.h>
#include <math.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU 0x1edd2c0 / 0x1edd360 / 0x1edd3a0: the editor's status strings. DF_MOBJ_NewStringDirect keeps str, and
 * __DrawEmbEditSystem prints the cursor / zoom / grid digits into it every frame: one shared definition. */
#define MEMBE_COL_STR {0x90, 0x90, 0x90, 0x80}
struct struct_d2e73f30 windat_cstr[5] = {
    {"CURSOR", 0, 424, 374, MEMBE_COL_STR}, {"X", 0, 500, 374, MEMBE_COL_STR}, {"000", 1, 512, 372, MEMBE_COL_STR},
    {"Y", 0, 558, 374, MEMBE_COL_STR}, {"000", 1, 570, 372, MEMBE_COL_STR},
};
struct struct_d2e73f30 windat_zstr[2] = {
    {"ZOOM@", 0, 424, 354, MEMBE_COL_STR}, {"1", 1, 484, 352, MEMBE_COL_STR},
};
struct struct_d2e73f30 windat_gstr[4] = {
    {"GRID", 0, 528, 354, MEMBE_COL_STR}, {"1", 1, 572, 352, MEMBE_COL_STR}, {"@", 0, 586, 354, MEMBE_COL_STR},
    {"1", 1, 598, 352, MEMBE_COL_STR},
};
#undef MEMBE_COL_STR
/* main ELF sdata 0x27b790 / 0x27b7a0 (nodwarf.h) */
char membe_palette_str[8] = "PALETTE";
char membe_preview_str[8] = "PREVIEW";
/* main ELF sdata 0x27b7b0 / 0x27b7b8: __DrawEmbEditSystem's sprintf formats (nodwarf.h) */
char membe_fmt_03d[5] = "%03d";
char membe_fmt_1d[4] = "%1d";
/* DWARF statics (main ELF sdata 0x27b738..0x27b750) */
struct struct_214acd28 s_ButtonImageSize = {0x40, 0x30};
struct struct_76b810ec s_ButtonImageColor = {0x80, 0x80, 0x80, 0x80};
struct struct_214acd28 s_ButtonShadowOffset = {-8, -4};
struct struct_214acd28 s_ButtonLightOffset = {-6, -6};
/* OLMENU 0x1edda30: sub-menu icon positions (used only by the __Init* helpers) */
struct struct_214acd28 s_aaSubMenuIconPos[4][6] = {
    {{80, 272}, {192, 272}, {0, 0}, {0, 0}, {0, 0}, {0, 0}},
    {{48, 272}, {136, 272}, {224, 272}, {0, 0}, {0, 0}, {0, 0}},
    {{16, 272}, {96, 272}, {176, 272}, {256, 272}, {0, 0}, {0, 0}},
    {{48, 240}, {136, 240}, {224, 240}, {48, 304}, {136, 304}, {224, 304}},
};
#endif

/* OLMENU rodata 0x1eddc10 / 0x1eddc20 / 0x1eddc30: layout part names of the sub-menu icons (garage2 bcheck: the
 * constructors do not keep the pointer in guest memory) */
static char membe_parts_kage[] = "menu_kage_0";
static char membe_parts_ita[] = "menu_ita_0";
static char membe_parts_hikari[] = "hikari_menu_0";

/* Called by the five __Init* below: builds icon_num sub-menu buttons (shadow sprite, button sprite, image sprite) at a_position[i]
 * into dlp->a_sub_icon, then the blinking light on button 0 (read even when icon_num <= 0). */
static void __InitSubMenuIcon(struct stMobj *ioObjp, int icon_num, struct struct_214acd28 *a_position,
                              int (*a_texcoord)[4], int tex_id)
{
    PS2_FRAME(0xe0);
    struct struct_fedcf029 **dlpp = (struct struct_fedcf029 **)&ioObjp->dt;
    struct stMenuRect rect;
    struct struct_214acd28 spos, lpos;

    rect.w = s_ButtonImageSize.x;
    rect.h = s_ButtonImageSize.y;
    for (int i = 0; i < icon_num; i++) {
        struct struct_7186d2e3 *ic = &(*dlpp)->a_sub_icon[i];
        ic->p_shadow = UnitSprite_New(ioObjp, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x413), membe_parts_kage);
        spos.x = s_ButtonShadowOffset.x + a_position[i].x;
        spos.y = s_ButtonShadowOffset.y + a_position[i].y;
        UnitSprite_SetPosition(ic->p_shadow, &spos);
        UnitSprite_SetFadeIn(ic->p_shadow);
        ic->p_button = UnitSprite_New(ioObjp, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x413), membe_parts_ita);
        UnitSprite_SetPosition(ic->p_button, &a_position[i]);
        UnitSprite_SetFadeIn(ic->p_button);
        rect.x = a_position[i].x;
        rect.y = a_position[i].y;
        ic->p_image = DF_MOBJ_NXNewSprite(ioObjp, tex_id, &rect, a_texcoord[i], &s_ButtonImageColor, 0, 0, 0, 1);
    }
    (*dlpp)->p_sub_icon_light =
        UnitSpriteBlink_New(ioObjp, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x413), membe_parts_hikari);
    UnitSprite_GetPosition((*dlpp)->a_sub_icon[0].p_button, &lpos);
    lpos.y = lpos.y + s_ButtonLightOffset.y;
    lpos.x = lpos.x + s_ButtonLightOffset.x;
    UnitSprite_SetPosition((*dlpp)->p_sub_icon_light, &lpos);
}

/* The five sub-menus (live: __SetNewFuncSystem stores one in the Func mobj +0x18 and FadeIn case 4 calls it); each copies its texcoord table (OLMENU 0x1eddaf0..0x1eddc10) to the stack:
 * Palette 0x1eddaf0, Draw 0x1eddb10, Shape 0x1eddb40, Font 0x1eddb80, Rotation 0x1eddbe0.
 * ioObjp is passed through untouched. */
static void __InitRotation(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    alignas(16) int texcoord[3][4] = {{0, 96, 64, 144}, {64, 96, 128, 144}, {128, 96, 192, 144}};
    __InitSubMenuIcon(ioObjp, 3, s_aaSubMenuIconPos[1], texcoord, 0x44f);
}

static void __InitFont(struct stMobj *ioObjp)
{
    PS2_FRAME(0x70);
    alignas(16) int texcoord[6][4] = {{0, 144, 64, 192}, {64, 144, 128, 192}, {128, 144, 192, 192},
                                      {0, 192, 64, 240}, {64, 192, 128, 240}, {128, 192, 192, 240}};
    __InitSubMenuIcon(ioObjp, 6, s_aaSubMenuIconPos[3], texcoord, 0x44e);
}

static void __InitShape(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    alignas(16) int texcoord[4][4] = {{0, 0, 64, 48}, {64, 0, 128, 48}, {128, 0, 192, 48}, {192, 0, 256, 48}};
    __InitSubMenuIcon(ioObjp, 4, s_aaSubMenuIconPos[2], texcoord, 0x44f);
}

static void __InitDraw(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    alignas(16) int texcoord[3][4] = {{0, 48, 64, 96}, {64, 48, 128, 96}, {128, 48, 192, 96}};
    __InitSubMenuIcon(ioObjp, 3, s_aaSubMenuIconPos[1], texcoord, 0x44f);
}

static void __InitPalette(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    alignas(16) int texcoord[2][4] = {{192, 144, 256, 192}, {192, 192, 256, 240}};
    __InitSubMenuIcon(ioObjp, 2, s_aaSubMenuIconPos[0], texcoord, 0x44e);
}

/* TEX1: MWCC's bitfield code clears lcm, mxl, mmag, mmin, mtba, l and k and never touches blank1 / blank2 / dummy0 /
 * dummy1, so only those bits are written (not a whole 64-bit store). */
static inline void membe_tex1_clear(struct struct_13efd7f4 *ti)
{
    ti->tex1.lcm = 0;
    ti->tex1.mxl = 0;
    ti->tex1.mmag = 0;
    ti->tex1.mmin = 0;
    ti->tex1.mtba = 0;
    ti->tex1.l = 0;
    ti->tex1.k = 0;
}

/* TEX0 is fully written by the original (every field assigned): the packed words are the final image. */
static inline void membe_tex0_set(struct struct_13efd7f4 *ti, unsigned long long tex0)
{
    *(unsigned long long *)&ti->tex0 = tex0;
}

static void __EME_SetTexInfo(struct struct_fedcf029 *dlp)
{
    /* psm 0x13 (PSMT8), tcc 1, tfx 0, cpsm / csm / csa 0, cld 4 for all seven */
    membe_tex0_set(&dlp->edt_texInfo, 0x8007a505dd30bc40ULL);   /* tbp0 0x3c40 tbw 2 tw 7 th 7 cbp 0x3d28 */
    membe_tex1_clear(&dlp->edt_texInfo);
    membe_tex0_set(&dlp->plt_texInfo, 0x8007a50555307c80ULL);   /* tbp0 0x3c80 tbw 1 tw 5 th 5 cbp 0x3d28 */
    membe_tex1_clear(&dlp->plt_texInfo);
    membe_tex0_set(&dlp->pcr_texInfo, 0x8007a58555307ce4ULL);   /* tbp0 0x3ce4 tbw 1 tw 5 th 5 cbp 0x3d2c */
    membe_tex1_clear(&dlp->pcr_texInfo);
    membe_tex0_set(&dlp->lyr_texInfo, 0x8007a585dd30bc84ULL);   /* tbp0 0x3c84 tbw 2 tw 7 th 7 cbp 0x3d2c */
    membe_tex1_clear(&dlp->lyr_texInfo);
    membe_tex0_set(&dlp->lyr2_texInfo, 0x8007a585dd30bce8ULL);  /* tbp0 0x3ce8 tbw 2 tw 7 th 7 cbp 0x3d2c */
    membe_tex1_clear(&dlp->lyr2_texInfo);
    membe_tex0_set(&dlp->scp_texInfo, 0x8007a58599307cc4ULL);   /* tbp0 0x3cc4 tbw 1 tw 6 th 6 cbp 0x3d2c */
    membe_tex1_clear(&dlp->scp_texInfo);
    membe_tex0_set(&dlp->ply_texInfo, 0x8007a58599307cd4ULL);   /* tbp0 0x3cd4 tbw 1 tw 6 th 6 cbp 0x3d2c */
    membe_tex1_clear(&dlp->ply_texInfo);
}

/* Eyedropper: idx = pixel index into edt_tbb[0] (no range check, always layer 0); the byte is a palette index, the
 * palette cursor (curx[2], cury[2]) moves to its 16x16 cell. (MWCC's signed %16 / 16 fixups never trigger: lbu.) */
void __EME_ChangeColor2Idx(int idx, struct struct_fedcf029 *dlp)
{
    int iidx = dlp->edt_tbb[0][idx];
    dlp->curx[2] = (short)(iidx % 16);
    dlp->cury[2] = (short)(iidx / 16);
}

/* Box from (inXh, inYh) to (inXt, inYt) inclusive into a 64-byte-stride buffer: inMode != 0 fills it, 0 draws the
 * outline only. No clipping (the callers keep the coordinates inside the 64x64 scope buffer). */
static void __EME_Layer_DrawBox(int inXh, int inYh, int inXt, int inYt, unsigned char *bp, int inMode, int idx)
{
    int dx, dy, addx, addy;
    if (inXh < inXt) {
        dx = inXt - inXh;
        addx = 1;
    } else {
        dx = inXh - inXt;
        addx = -1;
    }
    if (inYh < inYt) {
        dy = inYt - inYh;
        addy = 1;
    } else {
        dy = inYh - inYt;
        addy = -1;
    }
    int row = inYh << 6;
    for (int i = 0; i <= dy; i++, row += addy << 6) {
        unsigned char *p = bp + row + inXh;
        for (int j = 0; j <= dx; j++, p += addx) {
            if (inMode || j == 0 || j == dx || i == 0 || i == dy) *p = (unsigned char)idx;
        }
    }
}

/* The inlined erase macro: f_draw = 0, f_erase = 1 on stMobj byte 0 (DF_MOBJ_Play frees it), then the slot = NULL. */
static inline void membe_erase(struct stMobj **pp)
{
    if (*pp) {
        (*pp)->f_draw = 0;
        (*pp)->f_erase = 1;
        *pp = 0;
    }
}

/* Leaving the editor: deletes the window list. Quirks kept: the icon button / shadow sprite slots and p_icon_light
 * are not cleared, a_sub_icon / p_sub_icon_light / tmp_curp / rgb_ctrlp are left alone, and only cur_strp[0..4] /
 * zom_strp[0..1] are erased (DWARF arrays of 6 / 4). */
static void __DeleteWindowList(struct struct_fedcf029 *dispListp)
{
    PS2_FRAME(0x40);
    struct struct_fedcf029 *dlp = dispListp;
    int i;

    for (i = 0; i < 12; i++) {
        UnitSprite_Delete(dlp->a_icon[i].p_button);
        UnitSprite_Delete(dlp->a_icon[i].p_shadow);
        membe_erase(&dlp->a_icon[i].p_image);
    }
    UnitSprite_Delete(dlp->p_icon_light);

    membe_erase(&dlp->edt_wmp);
    UnitSprite_Delete(dlp->edt_shadow);
    dlp->edt_wmp = 0;
    dlp->edt_shadow = 0;

    for (i = 0; i < 8; i++) {
        struct struct_98062497 *f = &dlp->a_edit_frame[i];
        UnitSprite_Delete(f->p_frame);
        membe_erase(&f->p_arrow);
        UnitSprite_Delete(f->p_light);
        f->p_frame = 0;
        f->p_arrow = 0;
        f->p_light = 0;
    }

    membe_erase(&dlp->pen_wmp);
    dlp->pen_wmp = 0;

    membe_erase(&dlp->plt_wmp);
    UnitSprite_Delete(dlp->plt_wfp);
    membe_erase(&dlp->plt_namep);
    UnitSprite_Delete(dlp->plt_shadow);
    dlp->plt_wmp = 0;
    dlp->plt_wfp = 0;
    dlp->plt_namep = 0;
    dlp->plt_shadow = 0;

    membe_erase(&dlp->pvw_wmp);
    UnitSprite_Delete(dlp->pvw_wfp);
    membe_erase(&dlp->pvw_namep);
    UnitSprite_Delete(dlp->pvw_shadow);
    dlp->pvw_wmp = 0;
    dlp->pvw_wfp = 0;
    dlp->pvw_namep = 0;
    dlp->pvw_shadow = 0;

    UnitSprite_Delete(dlp->p_info_shadow);
    UnitSprite_Delete(dlp->p_info_wnd);
    dlp->p_info_shadow = 0;
    dlp->p_info_wnd = 0;

    for (i = 0; i < 5; i++) {
        if (dlp->cur_strp[i]) {
            membe_erase(&dlp->cur_strp[i]);
            dlp->cur_strp[i] = 0;
        }
    }
    for (i = 0; i < 2; i++) {
        if (dlp->zom_strp[i]) {
            membe_erase(&dlp->zom_strp[i]);
            dlp->zom_strp[i] = 0;
        }
    }
    for (i = 0; i < 4; i++) {
        if (dlp->grd_strp[i]) {
            membe_erase(&dlp->grd_strp[i]);
            dlp->grd_strp[i] = 0;
        }
    }

    membe_erase(&dlp->pvw_curp);
    dlp->pvw_curp = 0;
    membe_erase(&dlp->pvw_lyrp);
    dlp->pvw_lyrp = 0;
    membe_erase(&dlp->plt_lyrp);
    dlp->plt_lyrp = 0;
    membe_erase(&dlp->edt_lyrp);
    dlp->edt_lyrp = 0;
    membe_erase(&dlp->edt_grdp);
    dlp->edt_grdp = 0;
    membe_erase(&dlp->plt_grdp);
    dlp->plt_grdp = 0;
}

/* Sprite mobjs that fade out blank: no texture, colour rgb 0 (alpha untouched). No null test (FadeIn made them). */
static inline void membe_fade_blank(struct stMobj *m)
{
    m->dt.sprite.texp = 0;
    m->dt.sprite.cur_col.r = 0;
    m->dt.sprite.cur_col.g = 0;
    m->dt.sprite.cur_col.b = 0;
}

/* Starts the window list's fade-out (the fade calls start timers: the order is the original's). The three layer
 * mobjs are blanked as one batch before their fade-outs. Same 5 / 2 / 4 string bounds as __DeleteWindowList. */
static void __FadeOutWindowList(struct struct_fedcf029 *dispListp)
{
    PS2_FRAME(0x40);
    struct struct_fedcf029 *dlp = dispListp;
    int i;

    for (i = 0; i < 12; i++) {
        UnitSprite_SetFadeOut(dlp->a_icon[i].p_button);
        UnitSprite_SetFadeOut(dlp->a_icon[i].p_shadow);
        DF_MOBJ_SetFadeOut(dlp->a_icon[i].p_image);
    }
    DF_MOBJ_SetDrawEnable(dlp->p_icon_light, 0);

    membe_fade_blank(dlp->edt_wmp);
    DF_MOBJ_SetFadeOut(dlp->edt_wmp);
    UnitSprite_SetFadeOut(dlp->edt_shadow);

    for (i = 0; i < 8; i++) {
        UnitSprite_SetFadeOut(dlp->a_edit_frame[i].p_frame);
        DF_MOBJ_SetFadeOut(dlp->a_edit_frame[i].p_arrow);
        UnitSprite_SetFadeOut(dlp->a_edit_frame[i].p_light);
    }

    DF_MOBJ_SetFadeOut(dlp->pen_wmp);
    membe_fade_blank(dlp->plt_wmp);
    DF_MOBJ_SetFadeOut(dlp->plt_wmp);
    UnitSprite_SetFadeOut(dlp->plt_wfp);
    DF_MOBJ_SetFadeOut(dlp->plt_namep);
    UnitSprite_SetFadeOut(dlp->plt_shadow);
    membe_fade_blank(dlp->pvw_wmp);
    DF_MOBJ_SetFadeOut(dlp->pvw_wmp);
    UnitSprite_SetFadeOut(dlp->pvw_wfp);
    DF_MOBJ_SetFadeOut(dlp->pvw_namep);
    UnitSprite_SetFadeOut(dlp->pvw_shadow);
    UnitSprite_SetFadeOut(dlp->p_info_shadow);
    UnitSprite_SetFadeOut(dlp->p_info_wnd);

    for (i = 0; i < 5; i++)
        if (dlp->cur_strp[i]) DF_MOBJ_SetFadeOut(dlp->cur_strp[i]);
    for (i = 0; i < 2; i++)
        if (dlp->zom_strp[i]) DF_MOBJ_SetFadeOut(dlp->zom_strp[i]);
    for (i = 0; i < 4; i++)
        if (dlp->grd_strp[i]) DF_MOBJ_SetFadeOut(dlp->grd_strp[i]);

    membe_fade_blank(dlp->pvw_curp);
    DF_MOBJ_SetFadeOut(dlp->pvw_curp);
    membe_fade_blank(dlp->edt_lyrp);
    membe_fade_blank(dlp->plt_lyrp);
    membe_fade_blank(dlp->pvw_lyrp);
    DF_MOBJ_SetFadeOut(dlp->pvw_lyrp);
    DF_MOBJ_SetFadeOut(dlp->plt_lyrp);
    DF_MOBJ_SetFadeOut(dlp->edt_lyrp);

    __SetGridSystemFadeOut(dlp->edt_grdp);   /* mefunc.c, interpreted */
    __SetGridSystemFadeOut(dlp->plt_grdp);
}

/* OLMENU rodata 0x1eddc40..0x1eddc70 and main ELF sdata 0x27b788 / 0x27b798 / 0x27b7a8: layout part names (only read
 * during UnitSprite_New) */
static char membe_parts_kage02[] = "kage02_0";
static char membe_parts_kage01_0[] = "kage01_0";
static char membe_parts_kage01_1[] = "kage01_1";
static char membe_parts_kage03[] = "kage03_0";
static char membe_parts_ita01_0[] = "ita01_0";
static char membe_parts_ita01_1[] = "ita01_1";
static char membe_parts_ita02[] = "ita02_0";
static char membe_frame_name[8][16] = {"edit01_0", "edit05_0", "edit02_0", "edit07_0",
                                       "edit08_0", "edit03_0", "edit06_0", "edit04_0"};
static char membe_light_name[8][16] = {"hikari_edit01_0", "hikari_edit05_0", "hikari_edit02_0", "hikari_edit06_0",
                                       "hikari_edit06_1", "hikari_edit03_0", "hikari_edit05_1", "hikari_edit04_0"};

static inline struct _LO_HEADER *membe_layout(int id)
{
    return (struct _LO_HEADER *)LAYOUT_GetLayoutData(id);
}

/* Editor entry: builds the screen. Order matters (mobj slots are allocated in call order; from the PREVIEW title on
 * every object is a child of pvw_lyrp). Quirks kept: p_icon_light, edt_shadow and the frame lights get no fade-in;
 * cur_strp[5] / zom_strp[2..3] are never created. The uv arrays are 16-aligned (the constructors read them with lq). */
static void __FadeInWindowList(struct stMobj *ioObjp, struct struct_fedcf029 *dispListp)
{
    PS2_FRAME(0x440);
    struct struct_fedcf029 *dlp = dispListp;
    /* stack copies of the OLMENU tables 0x1edd620..0x1edda30 */
    alignas(16) int pvw_uv[4] = {0, 0, 64, 64};
    alignas(16) struct struct_29a134f2 windat_editm = {{352, 56, 256, 256}, {0x80, 0x80, 0x80, 0x80}, {0}, {0, 0, 128, 128}};
    alignas(16) struct struct_29a134f2 windat_penm = {{344, 352, 64, 32}, {0, 0, 0, 0x80}, {0}, {0, 0, 1, 1}};
    alignas(16) struct struct_29a134f2 windat_palettem = {{24, 56, 128, 128}, {0x80, 0x80, 0x80, 0x80}, {0}, {0, 0, 32, 32}};
    alignas(16) struct struct_29a134f2 windat_previewm = {{183, 57, 128, 128}, {0x80, 0x80, 0x80, 0x80}, {0}, {0, 0, 127, 127}};
    static const struct struct_214acd28 a_icon_pos[12] = {
        {16, 208}, {96, 208}, {176, 208}, {256, 208}, {16, 272}, {96, 272},
        {176, 272}, {256, 272}, {16, 336}, {96, 336}, {176, 336}, {256, 336},
    };
    alignas(16) int a_image_texcoord[12][4];
    static const struct struct_214acd28 a_position[8] = {
        {340, 44}, {472, 44}, {604, 44}, {340, 176}, {604, 176}, {340, 308}, {472, 308}, {604, 308},
    };
    alignas(16) int a_texcoord[8][4] = {
        {0, 240, 16, 256}, {18, 240, 34, 256}, {16, 240, 0, 256}, {34, 240, 18, 256},
        {18, 256, 34, 240}, {0, 256, 16, 240}, {18, 256, 34, 240}, {16, 256, 0, 240},
    };
    static const int a_sprite_flag[8] = {0, 0, 0, 1, 1, 0, 0, 0};
    struct stMenuRect image_rect, rect;
    alignas(16) struct stMenuRect rect_cur;
    struct struct_214acd28 shadow_pos, icon_pos;
    struct struct_76b810ec color = {0x80, 0x80, 0x80, 0x80};   /* gp-0x7510 */
    struct struct_76b810ec plt_col = {0x65, 0x65, 0x65, 0x80};  /* gp-0x7500 */
    struct struct_76b810ec pvw_col = {0x65, 0x65, 0x65, 0x80};  /* gp-0x74f0 */
    int i, j;

    for (i = 0; i < 12; i++) {
        a_image_texcoord[i][0] = (i & 3) * 64;
        a_image_texcoord[i][1] = (i >> 2) * 48;
        a_image_texcoord[i][2] = (i & 3) * 64 + 64;
        a_image_texcoord[i][3] = (i >> 2) * 48 + 48;
    }

    /* CLUT entry 0 of the edit image, then layer 1 = layer 0 */
    dlp->edt_cbb[0][2] = 0;
    dlp->edt_cbb[0][1] = 0;
    dlp->edt_cbb[0][0] = 0;
    dlp->edt_cbb[0][3] = 0x5a;
    memcpy(dlp->edt_tbb[1], dlp->edt_tbb[0], 0x4000);

    /* palette image: 16x16 cells of 2x2 pixels, index = row * 16 + column */
    for (i = 0; i < 16; i++) {
        for (j = 0; j < 16; j++) {
            unsigned char v = (unsigned char)(i * 16 + j);
            unsigned char *p = dlp->plt_tbb + i * 0x40 + j * 2;
            p[0x21] = v;
            p[0x20] = v;
            p[1] = v;
            p[0] = v;
        }
    }

    /* palette cursor: colour-2 border around a zero interior (32x32) */
    memset(dlp->pcr_tbb, 0, 0x400);
    memset(dlp->pcr_tbb, 2, 0x80);
    for (i = 2; i < 30; i++) {
        memset(dlp->pcr_tbb + i * 0x20, 2, 8);
        memset(dlp->pcr_tbb + i * 0x20 + 0x18, 2, 8);
    }
    memset(dlp->pcr_tbb + 0x380, 2, 0x80);

    /* cursor CLUT */
    memset(dlp->cur_cbb, 0, 0x400);
    dlp->cur_cbb[0] = 0;
    dlp->cur_cbb[1] = 0;
    dlp->cur_cbb[2] = 0;
    dlp->cur_cbb[3] = 0;
    dlp->cur_cbb[4] = 0x5a;
    dlp->cur_cbb[5] = 0x2d;
    dlp->cur_cbb[6] = 0x14;
    dlp->cur_cbb[7] = 0x80;
    dlp->cur_cbb[8] = 0xff;
    dlp->cur_cbb[9] = 0x87;
    dlp->cur_cbb[10] = 0;
    dlp->cur_cbb[11] = 0x80;
    dlp->cur_cbb[12] = 0;
    dlp->cur_cbb[13] = 0;
    dlp->cur_cbb[14] = 0;
    dlp->cur_cbb[15] = 0x80;
    dlp->cur_cbb[16] = 0;
    dlp->cur_cbb[17] = 0;
    dlp->cur_cbb[18] = 0;
    dlp->cur_cbb[19] = 0x40;

    memset(dlp->lyr_tbb, 0, 0x4000);
    memset(dlp->scp_tbb, 0, 0x1000);
    __EME_Layer_DrawBox(0, 0, 63, 63, dlp->scp_tbb, 1, 2);
    __EME_Layer_DrawBox(1, 1, 62, 62, dlp->scp_tbb, 1, 0);
    memset(dlp->ply_tbb, 0, 0x1000);
    FlushCache(0);

    DF_MDRAW_LoadImage(dlp->edt_cbb[0], 0x3d28, 0, 0, 0, 16, 16);
    DF_MDRAW_LoadImage(dlp->cur_cbb, 0x3d2c, 0, 0, 0, 16, 16);
    DF_MDRAW_LoadImage(dlp->edt_tbb[0], 0x3c40, 0x13, 0, 0, 128, 128);
    DF_MDRAW_LoadImage(dlp->plt_tbb, 0x3c80, 0x13, 0, 0, 32, 32);
    DF_MDRAW_LoadImage(dlp->lyr_tbb, 0x3c84, 0x13, 0, 0, 128, 128);
    DF_MDRAW_LoadImage(dlp->scp_tbb, 0x3cc4, 0x13, 0, 0, 64, 64);
    DF_MDRAW_LoadImage(dlp->ply_tbb, 0x3cd4, 0x13, 0, 0, 64, 64);
    DF_MDRAW_LoadImage(dlp->pcr_tbb, 0x3ce4, 0x13, 0, 0, 32, 32);
    DF_MDRAW_LoadImage(dlp->lyr_tbb2, 0x3ce8, 0x13, 0, 0, 128, 128);
    DF_MDRAW_KickReg(0x3f, 0xffffffffULL);   /* ori / dsll / ori: zero-extended, not -1 */
    __EME_SetTexInfo(dlp);

    /* 12 tool icons: shadow, button, image */
    image_rect.w = s_ButtonImageSize.x;
    image_rect.h = s_ButtonImageSize.y;
    for (i = 0; i < 12; i++) {
        struct struct_7186d2e3 *ic = &dlp->a_icon[i];
        ic->p_shadow = UnitSprite_New(ioObjp, membe_layout(0x413), membe_parts_kage);
        shadow_pos.x = s_ButtonShadowOffset.x + a_icon_pos[i].x;
        shadow_pos.y = s_ButtonShadowOffset.y + a_icon_pos[i].y;
        UnitSprite_SetPosition(ic->p_shadow, &shadow_pos);
        UnitSprite_SetFadeIn(ic->p_shadow);
        ic->p_button = UnitSprite_New(ioObjp, membe_layout(0x413), membe_parts_ita);
        UnitSprite_SetPosition(ic->p_button, (struct struct_214acd28 *)&a_icon_pos[i]);
        UnitSprite_SetFadeIn(ic->p_button);
        image_rect.x = a_icon_pos[i].x;
        image_rect.y = a_icon_pos[i].y;
        ic->p_image = DF_MOBJ_NXNewSprite(ioObjp, 0x44e, &image_rect, a_image_texcoord[i], &s_ButtonImageColor, 0, 0, 0, 1);
        DF_MOBJ_SetFadeIn(ic->p_image);
    }
    dlp->p_icon_light = UnitSpriteBlink_New(ioObjp, membe_layout(0x413), membe_parts_hikari);
    UnitSprite_GetPosition(dlp->a_icon[0].p_button, &icon_pos);
    icon_pos.x = icon_pos.x + s_ButtonLightOffset.x;
    icon_pos.y = icon_pos.y + s_ButtonLightOffset.y;
    UnitSprite_SetPosition(dlp->p_icon_light, &icon_pos);

    dlp->edt_shadow = UnitSprite_New(ioObjp, membe_layout(0x413), membe_parts_kage02);

    /* 8 edit frames: frame, arrow, blinking light (drawn off) */
    rect.w = 16;   /* arrow size, gp-0x7518 */
    rect.h = 16;
    for (i = 0; i < 8; i++) {
        struct struct_98062497 *f = &dlp->a_edit_frame[i];
        f->p_frame = UnitSprite_New(ioObjp, membe_layout(0x413), membe_frame_name[i]);
        UnitSprite_SetFadeIn(f->p_frame);
        rect.x = a_position[i].x;
        rect.y = a_position[i].y;
        f->p_arrow = DF_MOBJ_NXNewSprite(ioObjp, 0x44e, &rect, a_texcoord[i], &color, 0, a_sprite_flag[i], 0, 1);
        DF_MOBJ_SetFadeIn(f->p_arrow);
        f->p_light = UnitSpriteBlink_New(ioObjp, membe_layout(0x414), membe_light_name[i]);
        DF_MOBJ_SetDrawEnable(f->p_light, 0);
    }

    /* edit window + its layer (layer alpha 0) + grid */
    dlp->edt_wmp = DF_MOBJ_NewSpriteOld(ioObjp, &dlp->edt_texInfo, &windat_editm.rect, windat_editm.uv,
                                        &windat_editm.col, 1, 0, 0);
    DF_MOBJ_SetFadeIn(dlp->edt_wmp);
    dlp->edt_lyrp = DF_MOBJ_NewSpriteOld(ioObjp, &dlp->lyr_texInfo, &windat_editm.rect, windat_editm.uv,
                                         &windat_editm.col, 1, 0, 0);
    dlp->edt_lyrp->dt.sprite.cur_col.a = 0;
    DF_MOBJ_SetFadeIn(dlp->edt_lyrp);
    dlp->edt_grdp = __SetNewGridSystem(ioObjp, dlp, windat_editm.rect.x, windat_editm.rect.y,
                                       windat_editm.rect.x + windat_editm.rect.w,
                                       windat_editm.rect.y + windat_editm.rect.h, 0);
    __SetGridSystemFadeIn(dlp->edt_grdp);   /* mefunc.c, interpreted */

    /* palette window */
    dlp->plt_shadow = UnitSprite_New(ioObjp, membe_layout(0x413), membe_parts_kage01_0);
    UnitSprite_SetFadeIn(dlp->plt_shadow);
    dlp->plt_wfp = UnitSprite_New(ioObjp, membe_layout(0x413), membe_parts_ita01_0);
    UnitSprite_SetFadeIn(dlp->plt_wfp);
    dlp->plt_wmp = DF_MOBJ_NewSpriteOld(ioObjp, &dlp->plt_texInfo, &windat_palettem.rect, windat_palettem.uv,
                                        &windat_palettem.col, 1, 0, 0);
    DF_MOBJ_SetFadeIn(dlp->plt_wmp);
    dlp->plt_lyrp = DF_MOBJ_NewSpriteOld(ioObjp, &dlp->ply_texInfo, &windat_palettem.rect, pvw_uv,
                                         &windat_palettem.col, 1, 0, 0);
    DF_MOBJ_SetFadeIn(dlp->plt_lyrp);
    dlp->plt_grdp = __SetNewGridSystem(ioObjp, dlp, windat_palettem.rect.x, windat_palettem.rect.y,
                                       windat_palettem.rect.x + windat_palettem.rect.w,
                                       windat_palettem.rect.y + windat_palettem.rect.h, 1);
    __SetGridSystemFadeIn(dlp->plt_grdp);
    dlp->plt_namep = DF_MOBJ_NewStringDirect(ioObjp, membe_palette_str, 0, 22, 44, &plt_col, 0, 0);
    DF_MOBJ_SetFadeIn(dlp->plt_namep);

    /* preview window + layer (alpha 0) */
    dlp->pvw_shadow = UnitSprite_New(ioObjp, membe_layout(0x413), membe_parts_kage01_1);
    UnitSprite_SetFadeIn(dlp->pvw_shadow);
    dlp->pvw_wfp = UnitSprite_New(ioObjp, membe_layout(0x413), membe_parts_ita01_1);
    UnitSprite_SetFadeIn(dlp->pvw_wfp);
    dlp->pvw_wmp = DF_MOBJ_NewSpriteOld(ioObjp, &dlp->edt_texInfo, &windat_previewm.rect, windat_previewm.uv,
                                        &windat_previewm.col, 1, 0, 0);
    DF_MOBJ_SetFadeIn(dlp->pvw_wmp);
    dlp->pvw_lyrp = DF_MOBJ_NewSpriteOld(ioObjp, &dlp->lyr_texInfo, &windat_previewm.rect, windat_previewm.uv,
                                         &windat_previewm.col, 1, 0, 0);
    dlp->pvw_lyrp->dt.sprite.cur_col.a = 0;
    DF_MOBJ_SetFadeIn(dlp->pvw_lyrp);

    /* from here on everything is a child of the preview layer */
    struct stMobj *lyrp = dlp->pvw_lyrp;
    dlp->pvw_namep = DF_MOBJ_NewStringDirect(lyrp, membe_preview_str, 0, 182, 44, &pvw_col, 0, 0);
    DF_MOBJ_SetFadeIn(dlp->pvw_namep);
    memcpy(&rect_cur, &windat_previewm.rect, 0x10);
    dlp->pvw_curp = DF_MOBJ_NewSpriteOld(lyrp, &dlp->scp_texInfo, &rect_cur, pvw_uv, &windat_previewm.col, 1, 0, 0);
    DF_MOBJ_SetFadeIn(dlp->pvw_curp);
    dlp->p_info_shadow = UnitSprite_New(lyrp, membe_layout(0x413), membe_parts_kage03);
    UnitSprite_SetFadeIn(dlp->p_info_shadow);
    dlp->p_info_wnd = UnitSprite_New(lyrp, membe_layout(0x413), membe_parts_ita02);
    UnitSprite_SetFadeIn(dlp->p_info_wnd);

    for (i = 0; i < 5; i++) {
        struct struct_d2e73f30 *w = &windat_cstr[i];
        dlp->cur_strp[i] = DF_MOBJ_NewStringDirect(lyrp, w->str, w->type, w->x, w->y, &w->col, 1, 0);
        DF_MOBJ_SetFadeIn(dlp->cur_strp[i]);
    }
    for (i = 0; i < 2; i++) {
        struct struct_d2e73f30 *w = &windat_zstr[i];
        dlp->zom_strp[i] = DF_MOBJ_NewStringDirect(lyrp, w->str, w->type, w->x, w->y, &w->col, 1, 0);
        DF_MOBJ_SetFadeIn(dlp->zom_strp[i]);
    }
    for (i = 0; i < 4; i++) {
        struct struct_d2e73f30 *w = &windat_gstr[i];
        dlp->grd_strp[i] = DF_MOBJ_NewStringDirect(lyrp, w->str, w->type, w->x, w->y, &w->col, 1, 0);
        DF_MOBJ_SetFadeIn(dlp->grd_strp[i]);
    }

    /* pen preview: untextured */
    dlp->pen_wmp = DF_MOBJ_NewSpriteOld(lyrp, 0, &windat_penm.rect, windat_penm.uv, &windat_penm.col, 1, 0, 0);
    DF_MOBJ_SetFadeIn(dlp->pen_wmp);
}

/* Slides inN panels (palette / preview window list; NULL slots skipped) by (inX, inY) in 6 steps; *pcc counts the
 * steps, returns 1 when done. Raw word access on the mobj at +0x10 (cur), +0x20 (prev), +0x30 (next) as sprite
 * rects: the list also holds UnitSprite / string mobjs whose dt is something else (original, kept). */
int __EME_PanelMove(struct stMobj **panelp, int inN, int inX, int inY, int *pcc)
{
    int i;
    if (*pcc == 0 && inN > 0) {
        for (i = 0; i < inN; i++) {
            if (panelp[i]) {
                unsigned int *w = (unsigned int *)((char *)panelp[i] + 0x10);
                w[4] = w[0];
                w[5] = w[1];
                w[6] = w[2];
                w[7] = w[3];
                w[8] = w[0];
                w[9] = w[1];
                w[10] = w[2];
                w[11] = w[3];
                w[8] += (unsigned int)inX;
                w[9] += (unsigned int)inY;
            }
        }
    }
    *pcc = *pcc + 1;
    if (*pcc < 6) {
        for (i = 0; i < inN; i++) {
            if (panelp[i]) {
                unsigned int *w = (unsigned int *)((char *)panelp[i] + 0x10);
                int n = 6 - *pcc;
                w[0] += (unsigned int)((int)(w[8] - w[0]) / n);
                w[1] += (unsigned int)((int)(w[9] - w[1]) / n);
            }
        }
        return 0;
    }
    for (i = 0; i < inN; i++) {
        if (panelp[i]) {
            unsigned int *w = (unsigned int *)((char *)panelp[i] + 0x10);
            w[0] = w[8];
            w[1] = w[9];
        }
    }
    return 1;
}

/* ---- group 4: tool dispatch, palette system, Draw-system helpers ---- */

/* DF_MBGUIDE_Change takes a 0x63-terminated kind list (DWARF: no parameters); 8 register arguments here */
static inline void membe_guide8(int a, int b, int c, int d, int e, int f, int g, int h)
{
    gcall_as<int(int, int, int, int, int, int, int, int)>(GFN(DF_MBGUIDE_Change), a, b, c, d, e, f, g, h);
}

/* the undo buffer takes the image before a tool draws into it */
static inline void membe_snapshot(struct struct_fedcf029 *dlp)
{
    memcpy(dlp->edt_tbb[1], dlp->edt_tbb[0], 0x4000);
    dlp->f_undo = 0;
}

/* Runs the selected drawing tool at the cursor (curx[0], cury[0]); edg = button pressed this frame. Tool =
 * curx[1] + 4 * cury[1] (jump table OLMENU 0x1ee57c0, 0 / >= 7 = nothing). Quirks kept: point / eraser draw on every
 * call but snapshot only on edg; coidx (signed) is the pen size and the box / circle mode; the step / f_layer store
 * order differs per tool (line: step before the guide call, box: after). */
static void __EME_DoFunction(struct struct_fedcf029 *dlp, int edg)
{
    PS2_FRAME(0x30);
    int co = dlp->coidx;
    unsigned int sel = (unsigned int)(dlp->curx[1] + (dlp->cury[1] << 2));
    if (sel >= 7) return;
    switch (sel) {
    case 1: /* point */
        if (edg) membe_snapshot(dlp);
        __EME_Func_DrawPoint(dlp->curx[0], dlp->cury[0], co, dlp, 0);
        break;
    case 2: /* eraser */
        if (edg) membe_snapshot(dlp);
        __EME_Func_Eraser(dlp->curx[0], dlp->cury[0], co, dlp);
        break;
    case 3: /* line: start point, then end point */
        if (!edg) return;
        if (dlp->step == 0) {
            dlp->swork[0] = dlp->curx[0];
            dlp->swork[1] = dlp->cury[0];
            dlp->step = 1;
            dlp->f_layer = 1;
            membe_guide8(8, 0xa, 9, 0xb, 2, 0xc, 0xd, 0x63);
        } else {
            membe_snapshot(dlp);
            __EME_Func_DrawLine(dlp->curx[0], dlp->cury[0], co, dlp, 0);
            membe_guide8(8, 0xa, 9, 1, 2, 0xc, 0xd, 0x63);
            dlp->f_layer = 0;
            dlp->step = 0;
        }
        break;
    case 4: /* box (coidx 0 / 1) or circle (2..): first point, then the second */
        if (!edg) return;
        if (dlp->step == 0) {
            dlp->swork[0] = dlp->curx[0];
            dlp->swork[1] = dlp->cury[0];
            dlp->f_layer = 1;
            membe_guide8(8, 0xa, 9, 0xb, 2, 0xc, 0xd, 0x63);
            dlp->step = 1;
        } else {
            membe_snapshot(dlp);
            if (co < 2)
                __EME_Func_DrawBox(dlp->curx[0], dlp->cury[0], co % 2, dlp, 0);
            else
                __EME_Func_DrawCircle(dlp->curx[0], dlp->cury[0], co % 2, dlp, 0);
            membe_guide8(8, 0xa, 9, 1, 2, 0xc, 0xd, 0x63);
            dlp->f_layer = 0;
            dlp->step = 0;
        }
        break;
    case 5: /* CAP: three clicks */
        if (!edg) return;
        if (dlp->step == 2) {
            membe_snapshot(dlp);
            __EME_Func_CAP(dlp->curx[0], dlp->cury[0], dlp);
            dlp->step = 0;
            dlp->f_layer = 0;
        } else if (dlp->step == 1) {
            __EME_Func_CAP(dlp->curx[0], dlp->cury[0], dlp);
            dlp->step = 2;
        } else if (dlp->step == 0) {
            dlp->swork[0] = dlp->curx[0];
            dlp->swork[1] = dlp->cury[0];
            dlp->f_layer = 1;
            dlp->step = 1;
        }
        break;
    case 6: /* fill */
        if (edg) {
            membe_snapshot(dlp);
            __EME_Func_Paint(dlp->curx[0], dlp->cury[0], dlp);
        }
        break;
    default:
        break;
    }
}

static inline struct struct_fedcf029 *membe_obj_dlp(struct stMobj *obj)
{
    return *(struct struct_fedcf029 **)((char *)obj + 0x10);
}

/* Palette mobj (raw offsets): +0x1c int kinds[8] (saved key guide), +0x3c s16 0, +0x42 / +0x44 s16 saved palette
 * cursor, +0x46 saved f_pltFrame, +0x47 FadeIn counter, +0x48 result (0 cancel, 1 decide). */
#define MEMBE_S8(o, off) (*(signed char *)((char *)(o) + (off)))
#define MEMBE_S16(o, off) (*(short *)((char *)(o) + (off)))

/* Waits for the menu view motion, then restores f_pltFrame (and the cursor when cancelled) and ends. */
static void __PlayEmbPaletteSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x20);
    if (!DF_MVIEW_MotionSync(MViewp)) return;
    membe_obj_dlp(ioObjp)->f_pltFrame = MEMBE_S8(ioObjp, 0x46);
    if (MEMBE_S8(ioObjp, 0x48) == 0) {
        membe_obj_dlp(ioObjp)->curx[2] = MEMBE_S16(ioObjp, 0x42);
        membe_obj_dlp(ioObjp)->cury[2] = MEMBE_S16(ioObjp, 0x44);
    }
    membe_obj_dlp(ioObjp)->f_lockPlt = 0;
    ioObjp->stat = 5;
    ioObjp->play_func = 0;
}

/* Palette cursor (curx[2] column, cury[2] row, 0..15), undo swap, grid / zoom rates. Quirks kept: the undo swap
 * (square) goes on to the cursor keys in the same frame and never toggles f_undo; pad right / down wrap, mouse right /
 * down clamp without the f_lockPlt fix; the grid rate halves by wheel only. */
static void __PlayEmbPaletteSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct struct_fedcf029 *dlp = membe_obj_dlp(ioObjp);
    int res = -1;

    if (MPad[MPadUser].Rrt == 1 || eMouse->rbtn == 1) {
        Voice_Call(0x3eb, 0x7f);
        res = 0;
    } else if (MPad[MPadUser].Rdw == 1 || eMouse->lbtn == 1) {
        Voice_Call(0x3ea, 0x7f);
        res = 1;
    }
    if (res >= 0) {
        MEMBE_S8(ioObjp, 0x48) = (signed char)res;
        if (membe_obj_dlp(ioObjp)->f_callPalette == 2) membe_obj_dlp(ioObjp)->f_callPalette = 0;
        {
            /* 9 arguments: kinds[0..7] in a0..t3, the 9th (0x63) at the caller's sp+0 */
            int *kinds = (int *)((char *)ioObjp + 0x1c);
            PS2_STACK(unsigned long long, arg9, 0);
            arg9 = 0x63;
            (void)arg9;
            gcall_as<int(int, int, int, int, int, int, int, int)>(GFN(DF_MBGUIDE_Change), kinds[0], kinds[1],
                kinds[2], kinds[3], kinds[4], kinds[5], kinds[6], kinds[7]);
        }
        MEMBE_S8(ioObjp, 0x47) = 0;
        ioObjp->stat = 2;
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbPaletteSystemFadeOut);
        return;
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

    /* grid rate: L1 / middle button + wheel back doubles (1..8), middle button + wheel forward halves */
    if (MPad[MPadUser].repeat.l1 || (eMouse->wbtn > 0 && eMouse->w < 0)) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->rate_grid = (char)(membe_obj_dlp(ioObjp)->rate_grid * 2);
        if (membe_obj_dlp(ioObjp)->rate_grid >= 9) membe_obj_dlp(ioObjp)->rate_grid = 1;
    }
    if (eMouse->wbtn > 0 && eMouse->w > 0) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->rate_grid = (char)(membe_obj_dlp(ioObjp)->rate_grid / 2);
        if (membe_obj_dlp(ioObjp)->rate_grid <= 0) membe_obj_dlp(ioObjp)->rate_grid = 8;
    }
    /* zoom rate: R2 / wheel forward doubles (2..16), L2 / wheel back halves */
    if (MPad[MPadUser].repeat.r2 || (eMouse->wbtn == 0 && eMouse->w > 0)) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->rate_zoom = (char)(membe_obj_dlp(ioObjp)->rate_zoom * 2);
        if (membe_obj_dlp(ioObjp)->rate_zoom >= 17) membe_obj_dlp(ioObjp)->rate_zoom = 2;
    }
    if (MPad[MPadUser].repeat.l2 || (eMouse->wbtn == 0 && eMouse->w < 0)) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->rate_zoom = (char)(membe_obj_dlp(ioObjp)->rate_zoom / 2);
        if (membe_obj_dlp(ioObjp)->rate_zoom < 2) membe_obj_dlp(ioObjp)->rate_zoom = 16;
    }
}

/* Two idle frames, then the palette runs. */
static void __PlayEmbPaletteSystemFadeIn(struct stMobj *ioObjp)
{
    signed char c = MEMBE_S8(ioObjp, 0x47);
    if (c == 2) {
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbPaletteSystem);
        ioObjp->stat = 0;
    } else if (c == 1 || c == 0) {
        MEMBE_S8(ioObjp, 0x47) = (signed char)(c + 1);
    }
}

/* Opens the palette system as a child of inObjp (the caller sets f_callPalette first). NULL if no mobj is free. */
struct stMobj *__SetNewPaletteSystem(struct stMobj *inObjp, struct struct_fedcf029 *dlp)
{
    PS2_FRAME(0x30);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    obj->type = 0;
    obj->stat = 1;
    *(struct struct_fedcf029 **)((char *)obj + 0x10) = dlp;
    MEMBE_S8(obj, 0x46) = dlp->f_pltFrame;
    dlp->f_pltFrame = 1;
    MEMBE_S16(obj, 0x3c) = 0;
    MEMBE_S16(obj, 0x42) = dlp->curx[2];
    MEMBE_S16(obj, 0x44) = dlp->cury[2];
    DF_MBGUIDE_GetKind((int *)((char *)obj + 0x1c));
    membe_guide8(8, 0xa, 0, 1, 2, 0, 0xd, 0x63);
    obj->play_func = (void (*)(void))GFN(__PlayEmbPaletteSystemFadeIn);
    obj->count = 0;
    return obj;
}

/* Draw / DrawRot: waits for the child palette mobj (+0x14) to finish, erases it and returns to __PlayEmbDrawSystem
 * (hard-coded, also for DrawRot). The original reads the state byte before its null test (guarded load here). */
static void __PlayEmbDrawSystemDispatch(struct stMobj *ioObjp)
{
    struct stMobj **childp = (struct stMobj **)((char *)ioObjp + 0x14);
    struct stMobj *child = *childp;
    unsigned char st = child ? child->stat : ((const unsigned char *)PS2_NULL_MEM())[2];
    if (st != 5) return;
    if (child) {
        child->f_draw = 0;
        (*childp)->f_erase = 1;
        *childp = 0;
    }
    ioObjp->play_func = (void (*)(void))GFN(__PlayEmbDrawSystem);
    ioObjp->stat = 0;
}

/* Draw / DrawRot: slides the edit view (top_curx / top_cury) one pixel per frame in the f_edtFrame directions until
 * both axes clamp on the same frame or 17 frames pass, then back to __PlayEmbDrawSystem. +0x42 = frame counter,
 * +0x43 = state (0 = one idle frame). */
static void __PlayEmbDrawSystemMove(struct stMobj *ioObjp)
{
    signed char st = MEMBE_S8(ioObjp, 0x43);
    int moved = 0;
    if (st != 1) {
        if (st == 0) MEMBE_S8(ioObjp, 0x43) = (signed char)(st + 1);
        return;
    }
    MEMBE_S8(ioObjp, 0x42) = (signed char)(MEMBE_S8(ioObjp, 0x42) + 1);
    if (MEMBE_S8(ioObjp, 0x42) > 16) {
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbDrawSystem);
        ioObjp->stat = 0;
        return;
    }
    char fr = membe_obj_dlp(ioObjp)->f_edtFrame;
    if (fr & 1) {
        membe_obj_dlp(ioObjp)->top_curx--;
        if (membe_obj_dlp(ioObjp)->top_curx < 0) {
            membe_obj_dlp(ioObjp)->top_curx = 0;
            moved++;
        }
    } else if (fr & 2) {
        membe_obj_dlp(ioObjp)->top_curx++;
        int lim = 0x80 - ps2_div32(0x100, membe_obj_dlp(ioObjp)->rate_zoom);
        if (lim < membe_obj_dlp(ioObjp)->top_curx) {
            membe_obj_dlp(ioObjp)->top_curx = (short)lim;
            moved++;
        }
    }
    fr = membe_obj_dlp(ioObjp)->f_edtFrame;
    if (fr & 4) {
        membe_obj_dlp(ioObjp)->top_cury--;
        if (membe_obj_dlp(ioObjp)->top_cury < 0) {
            membe_obj_dlp(ioObjp)->top_cury = 0;
            moved++;
        }
    } else if (fr & 8) {
        membe_obj_dlp(ioObjp)->top_cury++;
        int lim = 0x80 - ps2_div32(0x100, membe_obj_dlp(ioObjp)->rate_zoom);
        if (lim < membe_obj_dlp(ioObjp)->top_cury) {
            membe_obj_dlp(ioObjp)->top_cury = (short)lim;
            moved++;
        }
    }
    if (moved == 2) {
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbDrawSystem);
        ioObjp->stat = 0;
    }
}

/* Draw / DrawRot leaving: waits for the menu view motion, drops the layer flag and ends. */
static void __PlayEmbDrawSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x20);
    if (!DF_MVIEW_MotionSync(MViewp)) return;
    MEMBE_S8(ioObjp, 0x42) = 0;
    membe_obj_dlp(ioObjp)->f_layer = 0;
    ioObjp->stat = 5;
    ioObjp->play_func = 0;
}

/* Rot panel (function icon 8) play: cursor (ePad directions / mouse deltas), zoom / grid rates, undo swap, palette
 * call, edge scroll (Move), and the step == 1 square lock. Draws nothing (DoFunction's table covers 1..6 only).
 * Raw object offsets: +0x14 child palette mobj, +0x42 / +0x43 Move counter / state, +0x44 decide latch.
 * Quirks kept: f_edtCur is both "screen-cursor mode" and the outside-left / right / up / down bits 1 / 2 / 4 / 8; the
 * entry value (s2) gates DoFunction on the non-decide paths and drives the screen -> logical re-sync, the fresh byte the
 * logical -> screen one; the re-sync sentinels 0x15f / 0x261 / 0x37 / 0x139 differ from the move clamps (0x260 /
 * 0x138); direction voices play even when nothing moves; Move / Dispatch return to __PlayEmbDrawSystem. */
static void __PlayEmbDrawRotSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct struct_fedcf029 *dlp = membe_obj_dlp(ioObjp);
    signed char s2 = (signed char)dlp->f_edtCur; /* entry value, not re-read (0x1e41c7c) */
    signed char gate = s2;                       /* v1 at the DoFunction gate */

    /* cancel */
    if (MPad[MPadUser].Rrt == 1 || eMouse->rbtn == 1) {
        Voice_Call(0x3eb, 0x7f);
        MEMBE_S8(ioObjp, 0x43) = 0;
        membe_guide8(8, 0xa, 9, 1, 2, 0, 0xd, 0x63);
        ioObjp->stat = 2;
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbDrawSystemFadeOut);
        return;
    }
    /* decide: at the window edge (screen-cursor mode) it starts the view scroll */
    if (MPad[MPadUser].Rdw == 1 || eMouse->lbtn == 1) {
        Voice_Call(0x3ea, 0x7f);
        MEMBE_S8(ioObjp, 0x44) = 1;
        gate = (signed char)dlp->f_edtCur;
        if (gate != 0) {
            MEMBE_S8(ioObjp, 0x42) = 0;
            MEMBE_S8(ioObjp, 0x43) = 0;
            ioObjp->stat = 1;
            ioObjp->play_func = (void (*)(void))GFN(__PlayEmbDrawSystemMove);
            return;
        }
    }
    /* latch / DoFunction (every held frame, edg on the press frame) */
    {
        unsigned char p5 = MPad[MPadUser].Rdw;
        if (p5 != 0 || eMouse->lbtn != 0) {
            if (MEMBE_S8(ioObjp, 0x44) != 0) {
                if (gate == 0) {
                    int edg = (p5 == 1);
                    if (!edg) edg = (eMouse->lbtn == 1);
                    __EME_DoFunction(membe_obj_dlp(ioObjp), edg);
                }
            } else {
                MEMBE_S8(ioObjp, 0x44) = 0;
            }
        } else {
            MEMBE_S8(ioObjp, 0x44) = 0;
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
    if (ePad.rt || eMouse->x > 0) {
        Voice_Call(0x3e9, 0x7f);
        if (dlp->f_edtCur != 0) {
            dlp->curx[3] = (short)(dlp->curx[3] + 4);
            if (dlp->curx[3] >= 0x261) dlp->curx[3] = 0x260;
        } else {
            int zoom = (signed char)dlp->rate_zoom;
            int lim = dlp->top_curx + ps2_div32(0xff, zoom);
            int top = dlp->top_curx;
            int c = dlp->curx[0];
            if (c < lim) {
                int g = (signed char)dlp->rate_grid;
                if (g < 3 || zoom * g < 8) {
                    dlp->curx[0] = (short)(dlp->curx[0] + 1);
                } else {
                    int t = c - top;
                    if (t % g == g - 1) dlp->curx[0] = (short)(c + 1);
                    else dlp->curx[0] = (short)(top + (t / g + 1) * g - 1);
                }
            }
        }
    }
    /* left */
    if (ePad.lt || eMouse->x < 0) {
        Voice_Call(0x3e9, 0x7f);
        if (dlp->f_edtCur != 0) {
            dlp->curx[3] = (short)(dlp->curx[3] - 4);
            if (dlp->curx[3] < 0x15f) dlp->curx[3] = 0x15f;
        } else {
            int top = dlp->top_curx;
            int c = dlp->curx[0];
            if (top < c) {
                int g = (signed char)dlp->rate_grid;
                if (g < 3 || (signed char)dlp->rate_zoom * g < 8) {
                    dlp->curx[0] = (short)(dlp->curx[0] - 1);
                } else {
                    int t = c - top;
                    if (t % g == 0) dlp->curx[0] = (short)(c - 1);
                    else dlp->curx[0] = (short)(top + (t / g) * g);
                }
            }
        }
    }
    /* up */
    if (ePad.up || eMouse->y < 0) {
        Voice_Call(0x3e9, 0x7f);
        if (dlp->f_edtCur != 0) {
            dlp->cury[3] = (short)(dlp->cury[3] - 4);
            if (dlp->cury[3] < 0x37) dlp->cury[3] = 0x37;
        } else {
            int top = dlp->top_cury;
            int c = dlp->cury[0];
            if (top < c) {
                int g = (signed char)dlp->rate_grid;
                if (g < 3 || (signed char)dlp->rate_zoom * g < 8) {
                    dlp->cury[0] = (short)(dlp->cury[0] - 1);
                } else {
                    int t = c - top;
                    if (t % g == 0) dlp->cury[0] = (short)(c - 1);
                    else dlp->cury[0] = (short)(top + (t / g) * g);
                }
            }
        }
    }
    /* down */
    if (ePad.dw || eMouse->y > 0) {
        Voice_Call(0x3e9, 0x7f);
        if (dlp->f_edtCur != 0) {
            dlp->cury[3] = (short)(dlp->cury[3] + 4);
            if (dlp->cury[3] >= 0x139) dlp->cury[3] = 0x138;
        } else {
            int zoom = (signed char)dlp->rate_zoom;
            int lim = dlp->top_cury + ps2_div32(0xff, zoom);
            int top = dlp->top_cury;
            int c = dlp->cury[0];
            if (c < lim) {
                int g = (signed char)dlp->rate_grid;
                if (g < 3 || zoom * g < 8) {
                    dlp->cury[0] = (short)(dlp->cury[0] + 1);
                } else {
                    int t = c - top;
                    if (t % g == g - 1) dlp->cury[0] = (short)(c + 1);
                    else dlp->cury[0] = (short)(top + (t / g + 1) * g - 1);
                }
            }
        }
    }

    /* square lock: pulls the second point onto the diagonal from the stroke start (swork) */
    if ((signed char)dlp->step == 1) {
        int cx = dlp->curx[0];
        int sw0 = dlp->swork[0];
        int dx = sw0 - cx;
        if (dx < 0) dx = -dx;
        int cy = dlp->cury[0];
        int sw1 = dlp->swork[1];
        int dy = sw1 - cy;
        if (dy < 0) dy = -dy;
        int sx = (cx < sw0) ? -1 : 1;
        int sy = (cy < sw1) ? -1 : 1;
        int m = (dy < dx) ? dx : dy;
        dlp->curx[0] = (short)(sw0 + (int)((unsigned int)m * (unsigned int)sx));
        dlp->cury[0] = (short)(dlp->swork[1] + (int)((unsigned int)m * (unsigned int)sy));
    }

    /* edge flags from the screen cursor */
    if (dlp->curx[3] < 0x160) dlp->f_edtCur = (char)(dlp->f_edtCur | 1);
    else dlp->f_edtCur = (char)(dlp->f_edtCur & ~1);
    if (dlp->curx[3] >= 0x260) dlp->f_edtCur = (char)(dlp->f_edtCur | 2);
    else dlp->f_edtCur = (char)(dlp->f_edtCur & ~2);
    if (dlp->cury[3] < 0x38) dlp->f_edtCur = (char)(dlp->f_edtCur | 4);
    else dlp->f_edtCur = (char)(dlp->f_edtCur & ~4);
    if (dlp->cury[3] >= 0x138) dlp->f_edtCur = (char)(dlp->f_edtCur | 8);
    else dlp->f_edtCur = (char)(dlp->f_edtCur & ~8);

    /* palette call (r1 or a pending f_callPalette request) */
    if ((MPad[MPadUser].repeat.r1 || dlp->f_callPalette == 1) && dlp->f_canPalet) {
        Voice_Call(0x3ea, 0x7f);
        dlp->f_callPalette = 2;
        *(struct stMobj **)((char *)ioObjp + 0x14) = __SetNewPaletteSystem(ioObjp, dlp);
        MEMBE_S8(ioObjp, 0x43) = 1;
        ioObjp->stat = 3;
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbDrawSystemDispatch);
        return;
    }

    /* grid rate: L1 / middle button + wheel back doubles (1..8), middle button + wheel forward halves */
    if (MPad[MPadUser].repeat.l1 || (eMouse->wbtn > 0 && eMouse->w < 0)) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->rate_grid = (char)(membe_obj_dlp(ioObjp)->rate_grid * 2);
        if (membe_obj_dlp(ioObjp)->rate_grid >= 9) membe_obj_dlp(ioObjp)->rate_grid = 1;
    }
    if (eMouse->wbtn > 0 && eMouse->w > 0) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->rate_grid = (char)(membe_obj_dlp(ioObjp)->rate_grid / 2);
        if (membe_obj_dlp(ioObjp)->rate_grid <= 0) membe_obj_dlp(ioObjp)->rate_grid = 8;
    }
    /* zoom rate: R2 / wheel forward doubles (2..16), L2 / wheel back halves */
    if (MPad[MPadUser].repeat.r2 || (eMouse->wbtn == 0 && eMouse->w > 0)) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->rate_zoom = (char)(membe_obj_dlp(ioObjp)->rate_zoom * 2);
        if (membe_obj_dlp(ioObjp)->rate_zoom >= 17) membe_obj_dlp(ioObjp)->rate_zoom = 2;
    }
    if (MPad[MPadUser].repeat.l2 || (eMouse->wbtn == 0 && eMouse->w < 0)) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->rate_zoom = (char)(membe_obj_dlp(ioObjp)->rate_zoom / 2);
        if (membe_obj_dlp(ioObjp)->rate_zoom < 2) membe_obj_dlp(ioObjp)->rate_zoom = 16;
    }

    /* re-sync screen <-> logical cursor */
    if (s2 == 0) {
        if (dlp->f_edtCur == 0) return;
        /* logical -> screen, fresh flags */
        dlp->curx[3] = (short)((signed char)dlp->rate_zoom * (dlp->curx[0] - dlp->top_curx) + 0x160);
        if (dlp->f_edtCur & 1) dlp->curx[3] = 0x15f;
        if (dlp->f_edtCur & 2) dlp->curx[3] = 0x261;
        dlp->cury[3] = (short)((signed char)dlp->rate_zoom * (dlp->cury[0] - dlp->top_cury) + 0x38);
        if (dlp->f_edtCur & 4) dlp->cury[3] = 0x37;
        if (dlp->f_edtCur & 8) dlp->cury[3] = 0x139;
        return;
    }
    /* screen -> logical, entry flags */
    dlp->curx[0] = (short)(dlp->top_curx + ps2_div32(dlp->curx[3] - 0x160, (signed char)dlp->rate_zoom));
    if (s2 & 1) dlp->curx[0] = dlp->top_curx;
    if (s2 & 2) dlp->curx[0] = (short)(dlp->top_curx + ps2_div32(0xff, (signed char)dlp->rate_zoom));
    dlp->cury[0] = (short)(dlp->top_cury + ps2_div32(dlp->cury[3] - 0x38, (signed char)dlp->rate_zoom));
    if (s2 & 4) dlp->cury[0] = dlp->top_cury;
    if (s2 & 8) dlp->cury[0] = (short)(dlp->top_cury + ps2_div32(0xff, (signed char)dlp->rate_zoom));
    if (dlp->curx[0] < 0) dlp->curx[0] = 0;
    {
        int c = dlp->curx[0];
        int lim = dlp->top_curx + ps2_div32(0xff, (signed char)dlp->rate_zoom);
        if (lim < c) dlp->curx[0] = (short)lim;
    }
    if (dlp->cury[0] < 0) dlp->cury[0] = 0;
    {
        int c = dlp->cury[0];
        int lim = dlp->top_cury + ps2_div32(0xff, (signed char)dlp->rate_zoom);
        if (lim < c) dlp->cury[0] = (short)lim;
    }
}

/* ---- group 6: Draw system (every tool panel except Rot) ---- */

/* Draw panel play: the DrawRot loop (g5) minus the square lock, plus the eyedropper (Rup) and screen-mode entry when
 * the logical cursor sits at the visible limit. Quirks kept: the eyedropper runs after the undo swap (samples the
 * swapped image, layer 0, no range check, also in screen mode) and ends the frame; the else-arm stores the move clamp
 * (0x260 / 0x15f / 0x37 / 0x138) for one axis only and step 11 then overwrites it with 0x261 / 0x15f / 0x37 / 0x139;
 * s2 (entry f_edtCur) stays stale for the whole frame, as in g5. */
static void __PlayEmbDrawSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct struct_fedcf029 *dlp = membe_obj_dlp(ioObjp);
    signed char s2 = (signed char)dlp->f_edtCur; /* entry value, not re-read (0x1e42d98) */
    signed char gate = s2;                       /* v1 at the DoFunction gate (0x1e42eb4) */

    /* cancel */
    if (MPad[MPadUser].Rrt == 1 || eMouse->rbtn == 1) {
        Voice_Call(0x3eb, 0x7f);
        MEMBE_S8(ioObjp, 0x43) = 0;
        membe_guide8(8, 0xa, 9, 1, 2, 0, 0xd, 0x63);
        ioObjp->stat = 2;
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbDrawSystemFadeOut);
        return;
    }
    /* decide: at the window edge (screen-cursor mode) it starts the view scroll */
    if (MPad[MPadUser].Rdw == 1 || eMouse->lbtn == 1) {
        Voice_Call(0x3ea, 0x7f);
        MEMBE_S8(ioObjp, 0x44) = 1;
        gate = (signed char)dlp->f_edtCur;
        if (gate != 0) {
            MEMBE_S8(ioObjp, 0x42) = 0;
            MEMBE_S8(ioObjp, 0x43) = 0;
            ioObjp->stat = 1;
            ioObjp->play_func = (void (*)(void))GFN(__PlayEmbDrawSystemMove);
            return;
        }
    }
    /* latch / DoFunction (every held frame, edg on the press frame) */
    {
        unsigned char p5 = MPad[MPadUser].Rdw;
        if (p5 != 0 || eMouse->lbtn != 0) {
            if (MEMBE_S8(ioObjp, 0x44) != 0) {
                if (gate == 0) {
                    int edg = (p5 == 1);
                    if (!edg) edg = (eMouse->lbtn == 1);
                    __EME_DoFunction(membe_obj_dlp(ioObjp), edg);
                }
            } else {
                MEMBE_S8(ioObjp, 0x44) = 0;
            }
        } else {
            MEMBE_S8(ioObjp, 0x44) = 0;
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
    /* eyedropper: the inlined __EME_ChangeColor2Idx (lbu: the signed fixups are dead), then the frame ends */
    if (MPad[MPadUser].Rup == 1 && (signed char)dlp->f_canSpuit != 0) {
        Voice_Call(0x3ea, 0x7f);
        unsigned int idx = ((unsigned char *)dlp)[dlp->curx[0] + (dlp->cury[0] << 7)];
        dlp->curx[2] = (short)(idx & 15);
        dlp->cury[2] = (short)(idx >> 4);
        return;
    }

    /* right */
    if (ePad.rt || eMouse->x > 0) {
        Voice_Call(0x3e9, 0x7f);
        if (dlp->f_edtCur != 0) {
            dlp->curx[3] = (short)(dlp->curx[3] + 4);
            if (dlp->curx[3] >= 0x261) dlp->curx[3] = 0x260;
        } else {
            int zoom = (signed char)dlp->rate_zoom;
            int lim = dlp->top_curx + ps2_div32(0xff, zoom);
            int top = dlp->top_curx;
            int c = dlp->curx[0];
            if (c < lim) {
                int g = (signed char)dlp->rate_grid;
                if (g < 3 || zoom * g < 8) {
                    dlp->curx[0] = (short)(dlp->curx[0] + 1);
                } else {
                    int t = c - top;
                    if (t % g == g - 1) dlp->curx[0] = (short)(c + 1);
                    else dlp->curx[0] = (short)(top + (t / g + 1) * g - 1);
                }
            } else {
                dlp->curx[3] = 0x260; /* enter screen mode (0x1e43148) */
            }
        }
    }
    /* left */
    if (ePad.lt || eMouse->x < 0) {
        Voice_Call(0x3e9, 0x7f);
        if (dlp->f_edtCur != 0) {
            dlp->curx[3] = (short)(dlp->curx[3] - 4);
            if (dlp->curx[3] < 0x15f) dlp->curx[3] = 0x15f;
        } else {
            int top = dlp->top_curx;
            int c = dlp->curx[0];
            if (top < c) {
                int g = (signed char)dlp->rate_grid;
                if (g < 3 || (signed char)dlp->rate_zoom * g < 8) {
                    dlp->curx[0] = (short)(dlp->curx[0] - 1);
                } else {
                    int t = c - top;
                    if (t % g == 0) dlp->curx[0] = (short)(c - 1);
                    else dlp->curx[0] = (short)(top + (t / g) * g);
                }
            } else {
                dlp->curx[3] = 0x15f; /* 0x1e432ac */
            }
        }
    }
    /* up */
    if (ePad.up || eMouse->y < 0) {
        Voice_Call(0x3e9, 0x7f);
        if (dlp->f_edtCur != 0) {
            dlp->cury[3] = (short)(dlp->cury[3] - 4);
            if (dlp->cury[3] < 0x37) dlp->cury[3] = 0x37;
        } else {
            int top = dlp->top_cury;
            int c = dlp->cury[0];
            if (top < c) {
                int g = (signed char)dlp->rate_grid;
                if (g < 3 || (signed char)dlp->rate_zoom * g < 8) {
                    dlp->cury[0] = (short)(dlp->cury[0] - 1);
                } else {
                    int t = c - top;
                    if (t % g == 0) dlp->cury[0] = (short)(c - 1);
                    else dlp->cury[0] = (short)(top + (t / g) * g);
                }
            } else {
                dlp->cury[3] = 0x37; /* 0x1e4340c */
            }
        }
    }
    /* down */
    if (ePad.dw || eMouse->y > 0) {
        Voice_Call(0x3e9, 0x7f);
        if (dlp->f_edtCur != 0) {
            dlp->cury[3] = (short)(dlp->cury[3] + 4);
            if (dlp->cury[3] >= 0x139) dlp->cury[3] = 0x138;
        } else {
            int zoom = (signed char)dlp->rate_zoom;
            int lim = dlp->top_cury + ps2_div32(0xff, zoom);
            int top = dlp->top_cury;
            int c = dlp->cury[0];
            if (c < lim) {
                int g = (signed char)dlp->rate_grid;
                if (g < 3 || zoom * g < 8) {
                    dlp->cury[0] = (short)(dlp->cury[0] + 1);
                } else {
                    int t = c - top;
                    if (t % g == g - 1) dlp->cury[0] = (short)(c + 1);
                    else dlp->cury[0] = (short)(top + (t / g + 1) * g - 1);
                }
            } else {
                dlp->cury[3] = 0x138; /* 0x1e43590 */
            }
        }
    }

    /* edge flags from the screen cursor */
    if (dlp->curx[3] < 0x160) dlp->f_edtCur = (char)(dlp->f_edtCur | 1);
    else dlp->f_edtCur = (char)(dlp->f_edtCur & ~1);
    if (dlp->curx[3] >= 0x260) dlp->f_edtCur = (char)(dlp->f_edtCur | 2);
    else dlp->f_edtCur = (char)(dlp->f_edtCur & ~2);
    if (dlp->cury[3] < 0x38) dlp->f_edtCur = (char)(dlp->f_edtCur | 4);
    else dlp->f_edtCur = (char)(dlp->f_edtCur & ~4);
    if (dlp->cury[3] >= 0x138) dlp->f_edtCur = (char)(dlp->f_edtCur | 8);
    else dlp->f_edtCur = (char)(dlp->f_edtCur & ~8);

    /* palette call (r1 or a pending f_callPalette request) */
    if ((MPad[MPadUser].repeat.r1 || dlp->f_callPalette == 1) && dlp->f_canPalet) {
        Voice_Call(0x3ea, 0x7f);
        dlp->f_callPalette = 2;
        *(struct stMobj **)((char *)ioObjp + 0x14) = __SetNewPaletteSystem(ioObjp, dlp);
        MEMBE_S8(ioObjp, 0x43) = 1;
        ioObjp->stat = 3;
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbDrawSystemDispatch);
        return;
    }

    /* grid rate: L1 / middle button + wheel back doubles (1..8), middle button + wheel forward halves */
    if (MPad[MPadUser].repeat.l1 || (eMouse->wbtn > 0 && eMouse->w < 0)) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->rate_grid = (char)(membe_obj_dlp(ioObjp)->rate_grid * 2);
        if (membe_obj_dlp(ioObjp)->rate_grid >= 9) membe_obj_dlp(ioObjp)->rate_grid = 1;
    }
    if (eMouse->wbtn > 0 && eMouse->w > 0) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->rate_grid = (char)(membe_obj_dlp(ioObjp)->rate_grid / 2);
        if (membe_obj_dlp(ioObjp)->rate_grid <= 0) membe_obj_dlp(ioObjp)->rate_grid = 8;
    }
    /* zoom rate: R2 / wheel forward doubles (2..16), L2 / wheel back halves */
    if (MPad[MPadUser].repeat.r2 || (eMouse->wbtn == 0 && eMouse->w > 0)) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->rate_zoom = (char)(membe_obj_dlp(ioObjp)->rate_zoom * 2);
        if (membe_obj_dlp(ioObjp)->rate_zoom >= 17) membe_obj_dlp(ioObjp)->rate_zoom = 2;
    }
    if (MPad[MPadUser].repeat.l2 || (eMouse->wbtn == 0 && eMouse->w < 0)) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->rate_zoom = (char)(membe_obj_dlp(ioObjp)->rate_zoom / 2);
        if (membe_obj_dlp(ioObjp)->rate_zoom < 2) membe_obj_dlp(ioObjp)->rate_zoom = 16;
    }

    /* re-sync screen <-> logical cursor */
    if (s2 == 0) {
        if (dlp->f_edtCur == 0) return;
        /* logical -> screen, fresh flags */
        dlp->curx[3] = (short)((signed char)dlp->rate_zoom * (dlp->curx[0] - dlp->top_curx) + 0x160);
        if (dlp->f_edtCur & 1) dlp->curx[3] = 0x15f;
        if (dlp->f_edtCur & 2) dlp->curx[3] = 0x261;
        dlp->cury[3] = (short)((signed char)dlp->rate_zoom * (dlp->cury[0] - dlp->top_cury) + 0x38);
        if (dlp->f_edtCur & 4) dlp->cury[3] = 0x37;
        if (dlp->f_edtCur & 8) dlp->cury[3] = 0x139;
        return;
    }
    /* screen -> logical, entry flags */
    dlp->curx[0] = (short)(dlp->top_curx + ps2_div32(dlp->curx[3] - 0x160, (signed char)dlp->rate_zoom));
    if (s2 & 1) dlp->curx[0] = dlp->top_curx;
    if (s2 & 2) dlp->curx[0] = (short)(dlp->top_curx + ps2_div32(0xff, (signed char)dlp->rate_zoom));
    dlp->cury[0] = (short)(dlp->top_cury + ps2_div32(dlp->cury[3] - 0x38, (signed char)dlp->rate_zoom));
    if (s2 & 4) dlp->cury[0] = dlp->top_cury;
    if (s2 & 8) dlp->cury[0] = (short)(dlp->top_cury + ps2_div32(0xff, (signed char)dlp->rate_zoom));
    if (dlp->curx[0] < 0) dlp->curx[0] = 0;
    {
        int c = dlp->curx[0];
        int lim = dlp->top_curx + ps2_div32(0xff, (signed char)dlp->rate_zoom);
        if (lim < c) dlp->curx[0] = (short)lim;
    }
    if (dlp->cury[0] < 0) dlp->cury[0] = 0;
    {
        int c = dlp->cury[0];
        int lim = dlp->top_cury + ps2_div32(0xff, (signed char)dlp->rate_zoom);
        if (lim < c) dlp->cury[0] = (short)lim;
    }
}

/* Two idle frames (+0x42 counts 0 -> 1 -> 2, never reset), then Draw or, for icon 8, DrawRot. stat stays 1 until
 * the deciding call. */
static void __PlayEmbDrawSystemFadeIn(struct stMobj *ioObjp)
{
    struct struct_fedcf029 *dlp = membe_obj_dlp(ioObjp);
    signed char c = MEMBE_S8(ioObjp, 0x42);
    if (c == 2) {
        int sel = dlp->curx[1] + (dlp->cury[1] << 2);
        if (sel == 8) ioObjp->play_func = (void (*)(void))GFN(__PlayEmbDrawRotSystem);
        else ioObjp->play_func = (void (*)(void))GFN(__PlayEmbDrawSystem);
        ioObjp->stat = 0;
    } else if (c == 1 || c == 0) {
        MEMBE_S8(ioObjp, 0x42) = (signed char)(c + 1);
    }
}

/* funcList constructor for the drawing tools: a child of inObjp. Resets step / mouse deltas, enables eyedropper and
 * palette, then per tool (sel = curx[1] + 4 * cury[1]) sets the key guide: Rot (8) / CAP (5) without eyedropper,
 * eraser (2) without palette. NULL if no mobj is free. */
static struct stMobj *__SetNewDrawSystem(struct stMobj *inObjp, struct struct_fedcf029 *dlp)
{
    PS2_FRAME(0x30);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    obj->type = 0;
    obj->stat = 1;
    *(struct struct_fedcf029 **)((char *)obj + 0x10) = dlp;
    MEMBE_S16(obj, 0x3c) = 0;
    MEMBE_S8(obj, 0x44) = 0;
    dlp->step = 0;
    dlp->f_canSpuit = 1;
    dlp->f_canPalet = 1;
    dlp->old_x = 0;
    dlp->old_y = 0;
    int sel = dlp->curx[1] + (dlp->cury[1] << 2);
    if (sel == 8) {
        membe_guide8(8, 0xa, 9, 1, 2, 0, 0xd, 0x63);
        dlp->f_canSpuit = 0;
    } else if (sel == 5) {
        membe_guide8(8, 0xa, 9, 1, 2, 0, 0xd, 0x63);
        dlp->f_canSpuit = 0;
    } else if (sel == 4 || sel == 3) {
        membe_guide8(8, 0xa, 9, 1, 2, 0xc, 0xd, 0x63);
    } else if (sel == 2) {
        membe_guide8(8, 0xa, 0, 1, 2, 0xc, 0xd, 0x63);
        dlp->f_canPalet = 0;
    } else {
        membe_guide8(8, 0xa, 9, 0xb, 2, 0xc, 0xd, 0x63);
    }
    obj->play_func = (void (*)(void))GFN(__PlayEmbDrawSystemFadeIn);
    obj->count = 0;
    return obj;
}

/* ---- group 7: Func system (the function panel: sub-function icons of the selected tool row) ---- */

typedef struct stMobj *(*membe_func_t)(struct stMobj *, struct struct_fedcf029 *);
typedef void (*membe_subinit_t)(struct stMobj *);

#ifdef ACNB_SOURCEPORT
/* OLMENU 0x1edd420: sub-function constructors per panel row (columns = sub-functions; NULL = no icon). */
#define MEMBE_DRAW __SetNewDrawSystem
#define MEMBE_TEXT __SetNewTextSystem
#define MEMBE_ROT __SetNewRotSystem
struct stMobj *(*funcList[12][6])(struct stMobj *, struct struct_fedcf029 *) = {
    {__SetNewRGBSystemPrev, __SetNewGradSystem, 0, 0, 0, 0},
    {MEMBE_DRAW, MEMBE_DRAW, MEMBE_DRAW, 0, 0, 0},
    {MEMBE_DRAW, MEMBE_DRAW, MEMBE_DRAW, 0, 0, 0},
    {MEMBE_DRAW, MEMBE_DRAW, MEMBE_DRAW, 0, 0, 0},
    {MEMBE_DRAW, MEMBE_DRAW, MEMBE_DRAW, MEMBE_DRAW, 0, 0},
    {MEMBE_DRAW, 0, 0, 0, 0, 0},
    {MEMBE_DRAW, 0, 0, 0, 0, 0},
    {MEMBE_TEXT, MEMBE_TEXT, MEMBE_TEXT, MEMBE_TEXT, MEMBE_TEXT, MEMBE_TEXT},
    {MEMBE_ROT, MEMBE_ROT, MEMBE_ROT, 0, 0, 0},
    {__SetNewMirrorSystem, 0, 0, 0, 0, 0},
    {__SetNewShiftSystem, 0, 0, 0, 0, 0},
    {__SetNewClearSystem, 0, 0, 0, 0, 0},
};
#undef MEMBE_DRAW
#undef MEMBE_TEXT
#undef MEMBE_ROT
#endif

/* Func mobj (raw offsets): +0x14 child system, +0x18 subFunc_init (guest fn pointer), +0x1c menu string id, +0x20
 * PanelMove counter, +0x24 s16 0, +0x26 s16 panel row, +0x28 / +0x2a s16 icon columns / rows, +0x2c step, +0x2d func
 * (never read), +0x2e mode. */

/* Waits for the child system (Draw / Text / Rot / ... / palette) to end, erases it and resumes the panel. The original
 * reads the child's state before its null test (guarded load here); f_canPalet is forced back to 1. */
static void __PlayEmbFuncSystemDispatch(struct stMobj *ioObjp)
{
    struct stMobj **childp = (struct stMobj **)((char *)ioObjp + 0x14);
    struct struct_fedcf029 *dlp = membe_obj_dlp(ioObjp);
    struct stMobj *child = *childp;
    unsigned char st = child ? child->stat : ((const unsigned char *)PS2_NULL_MEM())[2];
    if (st != 5) return;
    if (child) {
        child->f_draw = 0;
        (*childp)->f_erase = 1;
        *childp = 0;
    }
    dlp->old_x = 0;
    dlp->old_y = 0;
    dlp->f_canPalet = 1;
    ioObjp->play_func = (void (*)(void))GFN(__PlayEmbFuncSystem);
    ioObjp->stat = 0;
}

/* Panel leaving: mode 0 deletes the sub icons and the sub light and fades the 12 main icons back in; mode 1 slides the
 * panel list up and ends as soon as PanelMove returns 0 (its first call: quirk kept, sub icons stay). */
static void __PlayEmbFuncSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    int i;
    if (!DF_MVIEW_MotionSync(MViewp)) return;
    signed char mode = MEMBE_S8(ioObjp, 0x2e);
    if (mode != 0) {
        if (mode != 1) goto finish;
        if (__EME_PanelMove((struct stMobj **)((char *)membe_obj_dlp(ioObjp) + 0x17554), 9, 0, -0x40,
                            (int *)((char *)ioObjp + 0x20)) == 0)
            goto finish;
    }
    for (i = 0; i < 6; i++) {
        if (membe_obj_dlp(ioObjp)->a_sub_icon[i].p_button) {
            UnitSprite_Delete(membe_obj_dlp(ioObjp)->a_sub_icon[i].p_button);
            UnitSprite_Delete(membe_obj_dlp(ioObjp)->a_sub_icon[i].p_shadow);
            if (membe_obj_dlp(ioObjp)->a_sub_icon[i].p_image) {
                membe_obj_dlp(ioObjp)->a_sub_icon[i].p_image->f_draw = 0;
                membe_obj_dlp(ioObjp)->a_sub_icon[i].p_image->f_erase = 1;
                membe_obj_dlp(ioObjp)->a_sub_icon[i].p_image = 0;
            }
            membe_obj_dlp(ioObjp)->a_sub_icon[i].p_button = 0;
            membe_obj_dlp(ioObjp)->a_sub_icon[i].p_shadow = 0;
            membe_obj_dlp(ioObjp)->a_sub_icon[i].p_image = 0;
        }
    }
    if (membe_obj_dlp(ioObjp)->p_sub_icon_light) {
        UnitSprite_Delete(membe_obj_dlp(ioObjp)->p_sub_icon_light);
        membe_obj_dlp(ioObjp)->p_sub_icon_light = 0;
    }
    for (i = 0; i < 12; i++) {
        UnitSprite_SetFadeIn(membe_obj_dlp(ioObjp)->a_icon[i].p_button);
        UnitSprite_SetFadeIn(membe_obj_dlp(ioObjp)->a_icon[i].p_shadow);
        DF_MOBJ_SetFadeIn(membe_obj_dlp(ioObjp)->a_icon[i].p_image);
    }
    DF_MOBJ_SetDrawEnable(membe_obj_dlp(ioObjp)->p_icon_light, 1);
    MEMBE_S8(ioObjp, 0x2c) = (signed char)(MEMBE_S8(ioObjp, 0x2c) + 1);
finish:
    ioObjp->stat = 5;
    ioObjp->play_func = 0;
}

/* Cancel: fades the sub icons out, hides the sub light, then FadeOut. */
static void __SetEmbFuncSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    for (int i = 0; i < 6; i++) {
        if (membe_obj_dlp(ioObjp)->a_sub_icon[i].p_button) {
            UnitSprite_SetFadeOut(membe_obj_dlp(ioObjp)->a_sub_icon[i].p_button);
            UnitSprite_SetFadeOut(membe_obj_dlp(ioObjp)->a_sub_icon[i].p_shadow);
            DF_MOBJ_SetFadeOut(membe_obj_dlp(ioObjp)->a_sub_icon[i].p_image);
        }
    }
    if (membe_obj_dlp(ioObjp)->p_sub_icon_light) DF_MOBJ_SetDrawEnable(membe_obj_dlp(ioObjp)->p_sub_icon_light, 0);
    *(int *)((char *)ioObjp + 0x20) = 0;
    MEMBE_S8(ioObjp, 0x2c) = 0;
    ioObjp->stat = 2;
    ioObjp->play_func = (void (*)(void))GFN(__PlayEmbFuncSystemFadeOut);
}

/* Function panel play: Start (key guide), cancel, decide (funcList[row][coidx] constructor -> Dispatch), undo swap,
 * sub-icon cursor (co_idx_x / co_idx_y: pad wraps, mouse clamps), palette (R1 / f_callPalette), grid / zoom rates,
 * coidx = x + cols * y and the light follows it. Quirks kept: undo falls through to the cursor; funcList is indexed
 * without bounds (a negative coidx reaches the previous row); cols / rows 0 give -1. */
static void __PlayEmbFuncSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x60);
    struct struct_fedcf029 *dlp = membe_obj_dlp(ioObjp);
    signed char old = (signed char)dlp->coidx; /* read before IsShown (0x1e444d8) */

    if (MenuKeyGuide_IsShown() == 1) return;
    if (MPad[MPadUser].start == 1) {
        MenuKeyGuide_Show(0x22);
        return;
    }
    /* cancel */
    if (MPad[MPadUser].Rrt == 1 || eMouse->rbtn == 1) {
        Voice_Call(0x3eb, 0x7f);
        MEMBE_S8(ioObjp, 0x2d) = 0;
        __SetEmbFuncSystemFadeOut(ioObjp);
        return;
    }
    /* decide: the selected sub-function's constructor (no bounds / NULL check in the original) */
    if (MPad[MPadUser].Rdw == 1 || eMouse->lbtn == 1) {
        Voice_Call(0x3ea, 0x7f);
        int row = MEMBE_S16(ioObjp, 0x26);
        int col = (signed char)dlp->coidx;
        membe_func_t fp = (&funcList[0][0])[row * 6 + col];
        struct stMobj *res = fp ? gcall(fp, ioObjp, membe_obj_dlp(ioObjp)) : 0;
        *(struct stMobj **)((char *)ioObjp + 0x14) = res;
        dlp->old_x = 0;
        dlp->old_y = 0;
        MEMBE_S8(ioObjp, 0x2d) = 1;
        ioObjp->stat = 3;
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbFuncSystemDispatch);
        return;
    }
    /* undo: swap the two CLUTs (f_undo) or the two images, MenuWorkp as scratch; no return */
    if (MPad[MPadUser].Rlt == 1) {
        Voice_Call(0x3eb, 0x7f);
        if ((signed char)dlp->f_undo) {
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
        dlp->co_idx_x = (char)((signed char)dlp->co_idx_x + 1);
        if (!((signed char)dlp->co_idx_x < MEMBE_S16(ioObjp, 0x28))) dlp->co_idx_x = 0;
    }
    if ((signed char)dlp->old_x / 4 > 0) {
        Voice_Call(0x3e9, 0x7f);
        dlp->old_x = 0;
        dlp->co_idx_x = (char)((signed char)dlp->co_idx_x + 1);
        int cols = MEMBE_S16(ioObjp, 0x28);
        if (!((signed char)dlp->co_idx_x < cols)) dlp->co_idx_x = (char)(cols - 1);
    }
    /* left */
    if (MPad[MPadUser].repeat.lt) {
        Voice_Call(0x3e9, 0x7f);
        dlp->co_idx_x = (char)((signed char)dlp->co_idx_x - 1);
        if ((signed char)dlp->co_idx_x < 0) dlp->co_idx_x = (char)(MEMBE_S16(ioObjp, 0x28) - 1);
    }
    if ((signed char)dlp->old_x / 4 < 0) {
        Voice_Call(0x3e9, 0x7f);
        dlp->old_x = 0;
        dlp->co_idx_x = (char)((signed char)dlp->co_idx_x - 1);
        if ((signed char)dlp->co_idx_x < 0) dlp->co_idx_x = 0;
    }
    /* up */
    if (MPad[MPadUser].repeat.up) {
        Voice_Call(0x3e9, 0x7f);
        dlp->co_idx_y = (char)((signed char)dlp->co_idx_y - 1);
        if ((signed char)dlp->co_idx_y < 0) dlp->co_idx_y = (char)(MEMBE_S16(ioObjp, 0x2a) - 1);
    }
    if ((signed char)dlp->old_y / 4 < 0) {
        Voice_Call(0x3e9, 0x7f);
        dlp->old_y = 0;
        dlp->co_idx_y = (char)((signed char)dlp->co_idx_y - 1);
        if ((signed char)dlp->co_idx_y < 0) dlp->co_idx_y = 0;
    }
    /* down */
    if (MPad[MPadUser].repeat.dw) {
        Voice_Call(0x3e9, 0x7f);
        dlp->co_idx_y = (char)((signed char)dlp->co_idx_y + 1);
        if (!((signed char)dlp->co_idx_y < MEMBE_S16(ioObjp, 0x2a))) dlp->co_idx_y = 0;
    }
    if ((signed char)dlp->old_y / 4 > 0) {
        Voice_Call(0x3e9, 0x7f);
        dlp->old_y = 0;
        dlp->co_idx_y = (char)((signed char)dlp->co_idx_y + 1);
        int rows = MEMBE_S16(ioObjp, 0x2a);
        if (!((signed char)dlp->co_idx_y < rows)) dlp->co_idx_y = (char)(rows - 1);
    }

    /* palette (R1 or a pending f_callPalette request; no f_canPalet test here) */
    if (MPad[MPadUser].repeat.r1 || (signed char)dlp->f_callPalette == 1) {
        Voice_Call(0x3ea, 0x7f);
        dlp->f_callPalette = 2;
        *(struct stMobj **)((char *)ioObjp + 0x14) = __SetNewPaletteSystem(ioObjp, membe_obj_dlp(ioObjp));
        MEMBE_S8(ioObjp, 0x2d) = 1;
        ioObjp->stat = 3;
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbFuncSystemDispatch);
        return;
    }

    /* grid rate: L1 / middle button + wheel back doubles (1..8), middle button + wheel forward halves */
    if (MPad[MPadUser].repeat.l1 || (eMouse->wbtn > 0 && eMouse->w < 0)) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->rate_grid = (char)((signed char)membe_obj_dlp(ioObjp)->rate_grid * 2);
        if ((signed char)membe_obj_dlp(ioObjp)->rate_grid >= 9) membe_obj_dlp(ioObjp)->rate_grid = 1;
    }
    if (eMouse->wbtn > 0 && eMouse->w > 0) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->rate_grid = (char)((signed char)membe_obj_dlp(ioObjp)->rate_grid / 2);
        if ((signed char)membe_obj_dlp(ioObjp)->rate_grid <= 0) membe_obj_dlp(ioObjp)->rate_grid = 8;
    }
    /* zoom rate: R2 / wheel forward doubles (2..16), L2 / wheel back halves */
    if (MPad[MPadUser].repeat.r2 || (eMouse->wbtn == 0 && eMouse->w > 0)) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->rate_zoom = (char)((signed char)membe_obj_dlp(ioObjp)->rate_zoom * 2);
        if ((signed char)membe_obj_dlp(ioObjp)->rate_zoom >= 17) membe_obj_dlp(ioObjp)->rate_zoom = 2;
    }
    if (MPad[MPadUser].repeat.l2 || (eMouse->wbtn == 0 && eMouse->w < 0)) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->rate_zoom = (char)((signed char)membe_obj_dlp(ioObjp)->rate_zoom / 2);
        if ((signed char)membe_obj_dlp(ioObjp)->rate_zoom < 2) membe_obj_dlp(ioObjp)->rate_zoom = 16;
    }

    /* selection, then the sub light follows it (null test on sub icon 0 only) */
    dlp->coidx = (char)((signed char)dlp->co_idx_x +
                        (int)((unsigned int)MEMBE_S16(ioObjp, 0x28) * (unsigned int)(signed char)dlp->co_idx_y));
    signed char co = (signed char)dlp->coidx;
    if (co == old) return;
    if (!membe_obj_dlp(ioObjp)->a_sub_icon[0].p_button) return;
    PS2_STACK(struct struct_214acd28, pos, 0x58);
    UnitSprite_GetPosition((&membe_obj_dlp(ioObjp)->a_sub_icon[0])[co].p_button, &pos);
    pos.x = pos.x + s_ButtonLightOffset.x;
    pos.y = pos.y + s_ButtonLightOffset.y;
    UnitSprite_SetPosition(membe_obj_dlp(ioObjp)->p_sub_icon_light, &pos);
}

/* Panel entry: two idle frames, mode 1 slides the panel list down, then the row's sub icons are built
 * (subFunc_init = one of the __Init* helpers, guest pointer) and the panel runs (at once when it has no icons). */
static void __PlayEmbFuncSystemFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    signed char step = MEMBE_S8(ioObjp, 0x2c);
    if ((unsigned int)(int)step >= 6) return;
    switch (step) {
    case 0:
    case 1:
        MEMBE_S8(ioObjp, 0x2c) = (signed char)(step + 1);
        break;
    case 2:
        MEMBE_S8(ioObjp, 0x2c) = (signed char)(step + 1);
        if (MEMBE_S8(ioObjp, 0x2e) == 0) MEMBE_S8(ioObjp, 0x2c) = (signed char)(MEMBE_S8(ioObjp, 0x2c) + 1);
        *(int *)((char *)ioObjp + 0x20) = 0;
        break;
    case 3:
        if (__EME_PanelMove((struct stMobj **)((char *)membe_obj_dlp(ioObjp) + 0x17554), 9, 0, 0x40,
                            (int *)((char *)ioObjp + 0x20)))
            MEMBE_S8(ioObjp, 0x2c) = (signed char)(MEMBE_S8(ioObjp, 0x2c) + 1);
        break;
    case 4: {
        membe_subinit_t init = *(membe_subinit_t *)((char *)ioObjp + 0x18);
        if (init) gcall(init, ioObjp);
        if (MEMBE_S16(ioObjp, 0x28) == 0 || MEMBE_S16(ioObjp, 0x2a) == 0) {
            ioObjp->play_func = (void (*)(void))GFN(__PlayEmbFuncSystem);
            ioObjp->stat = 0;
        }
        MEMBE_S8(ioObjp, 0x2c) = (signed char)(MEMBE_S8(ioObjp, 0x2c) + 1);
        break;
    }
    case 5:
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbFuncSystem);
        ioObjp->stat = 0;
        break;
    }
}

/* Opens the function panel for the Edit system's current row (inObjp +0x2c) as a child of inObjp; mode 1 = slide the
 * panel list. Fades the 12 main icons out and hides their light. NULL if no mobj is free. (No range check on the row.) */
static struct stMobj *__SetNewFuncSystem(struct stMobj *inObjp, int mode)
{
    PS2_FRAME(0x110);
    /* stack copies of OLMENU 0x1edd580 / 0x1edd5a0 / 0x1edd5c0 / 0x1edd5f0 */
    static const short a_icon_num_x[12] = {2, 3, 3, 3, 4, 0, 0, 3, 3, 0, 0, 0};
    static const short a_icon_num_y[12] = {1, 1, 1, 1, 1, 0, 0, 2, 1, 0, 0, 0};
    static const int hidxTop[12] = {102029, 102031, 102031, 102031, 102034, 0, 0, 102038, 102041, 0, 0, 0};
    const membe_subinit_t ap_subfunc_init[12] = {
        GFN(__InitPalette), GFN(__InitDraw), GFN(__InitDraw), GFN(__InitDraw), GFN(__InitShape), 0, 0,
        GFN(__InitFont), GFN(__InitRotation), 0, 0, 0,
    };

    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    obj->type = 0;
    obj->stat = 1;
    *(struct struct_fedcf029 **)((char *)obj + 0x10) = membe_obj_dlp(inObjp);
    MEMBE_S16(obj, 0x26) = MEMBE_S16(inObjp, 0x2c);
    MEMBE_S16(obj, 0x24) = 0;
    MEMBE_S16(obj, 0x28) = a_icon_num_x[MEMBE_S16(obj, 0x26)];
    MEMBE_S16(obj, 0x2a) = a_icon_num_y[MEMBE_S16(obj, 0x26)];
    MEMBE_S8(obj, 0x2e) = (signed char)mode;
    membe_obj_dlp(obj)->coidx = 0;
    membe_obj_dlp(obj)->co_idx_x = 0;
    membe_obj_dlp(obj)->co_idx_y = 0;
    *(int *)((char *)obj + 0x1c) = hidxTop[MEMBE_S16(obj, 0x26)];
    *(membe_subinit_t *)((char *)obj + 0x18) = ap_subfunc_init[MEMBE_S16(obj, 0x26)];
    for (int i = 0; i < 12; i++) {
        UnitSprite_SetFadeOut(membe_obj_dlp(obj)->a_icon[i].p_button);
        UnitSprite_SetFadeOut(membe_obj_dlp(obj)->a_icon[i].p_shadow);
        DF_MOBJ_SetFadeOut(membe_obj_dlp(obj)->a_icon[i].p_image);
    }
    DF_MOBJ_SetDrawEnable(membe_obj_dlp(obj)->p_icon_light, 0);
    obj->play_func = (void (*)(void))GFN(__PlayEmbFuncSystemFadeIn);
    *(unsigned int *)((char *)obj + 0xc) = 0; /* pkt_func */
    obj->count = 0;
    return obj;
}

/* mouse delta in 1/8 steps (dead zone -1..1), added to the s8 accumulator old_x / old_y */
static inline char membe_mouse_acc(char acc, int d)
{
    int sgn = (d >= 0) ? 1 : -1;
    int f = (d >= -1 && d < 2) ? 0 : 1;
    return (char)(acc + (signed char)(f * (d / 8 + sgn)));
}

/* sprite uv rectangle of the zoomed edit view */
static inline void membe_view_uv(struct stMobj *m, struct struct_fedcf029 *dlp)
{
    m->dt.sprite.uv[0] = dlp->top_curx;
    m->dt.sprite.uv[1] = dlp->top_cury;
    m->dt.sprite.uv[2] = dlp->top_curx + ps2_div32(0x100, dlp->rate_zoom);
    m->dt.sprite.uv[3] = dlp->top_cury + ps2_div32(0x100, dlp->rate_zoom);
}

/* Editor draw_func (DF_MOBJ_Draw, every frame). Refreshes pad / mouse input (mouse accumulators old_*, middle-click
 * palette request), prints the cursor / zoom / grid digits; in stat 0 / 3 also: CLUT colour animation (astep), GS
 * uploads, zoom re-centre + view uv, scope (64x64 overview frame), the tool preview layer (f_layer: per-tool mefunc
 * preview into lyr_tbb / lyr_tbb2; else the cursor box), the palette layer (f_layer2: selected range, else the cursor
 * cell + pen colour) and the edge-frame lights. Quirks kept: Rot preview (fidx 8) multiplies the cursor coordinate by
 * the direction sign; f_layer2 with no plt_lyrp writes plt_lyrp->cur_col.a through NULL; S2 / S3 call SinVU0 and
 * drop it; x clamps low-then-high, y high-then-low; fidx 5 step 2 / fidx 7 step 1 overwrite swork[0..1]. */
static void __DrawEmbEditSystem(struct stMobj *inObjp)
{
    PS2_FRAME(0xa0);
    static const int a_flag[8] = {5, 4, 6, 1, 2, 9, 8, 10};   /* OLMENU 0x1edd560: UL, U, UR, L, R, DL, D, DR */
    const float pi = 0x1.921fb6p+1f;                          /* 0x40490fdb */
    struct struct_fedcf029 *dlp = membe_obj_dlp(inObjp);
    int ii, lim;

    DF_EME_PadUpdate();
    dlp->old_wbtn = eMouse->wbtn;
    dlp->old_lbtn = eMouse->lbtn;
    dlp->old_rbtn = eMouse->rbtn;
    dlp->old_w = (char)eMouse->w;
    dlp->old_x = membe_mouse_acc(dlp->old_x, eMouse->x);
    dlp->old_y = membe_mouse_acc(dlp->old_y, eMouse->y);
    Ac2_Emb_MouseUpdate();
    /* middle button released after a short press: ask for the palette */
    if (eMouse->wbtn == 0 && dlp->f_callPalette == 0 && dlp->f_canPalet == 1 && dlp->old_wbtn != 0 &&
        dlp->old_wbtn < 0x18)
        dlp->f_callPalette = 1;

    ps2_sprintf_d(windat_cstr[2].str, membe_fmt_03d, dlp->curx[0]);
    ps2_sprintf_d(windat_cstr[4].str, membe_fmt_03d, dlp->cury[0]);
    ps2_sprintf_d(windat_zstr[1].str, membe_fmt_1d, dlp->rate_zoom / 2);
    ps2_sprintf_d(windat_gstr[1].str, membe_fmt_1d, dlp->rate_grid);
    ps2_sprintf_d(windat_gstr[3].str, membe_fmt_1d, dlp->rate_grid);

    if (inObjp->stat != 0 && inObjp->stat != 3) return;

    /* CLUT animation: colour 0 red pulse (G / B stay 0), cursor colour 4 */
    {
        float s = fsmath_SinVU0(pi * (float)(dlp->astep / 3) / 180.0f);
        float f = 128.0f * s;
        dlp->edt_cbb[0][0] = (unsigned char)ps2_d2i(ps2_dfabs(ps2_f2d(f)));
        fsmath_SinVU0(pi * (float)(dlp->astep / 3) / 180.0f);
        dlp->edt_cbb[0][1] = (unsigned char)ps2_d2i(ps2_dfabs(ps2_f2d(0.0f)));
        fsmath_SinVU0(pi * (float)(dlp->astep / 3) / 180.0f);
        dlp->edt_cbb[0][2] = (unsigned char)ps2_d2i(ps2_dfabs(ps2_f2d(0.0f)));
        dlp->edt_cbb[0][3] = 0x80;
        float c = fsmath_CosVU0(pi * (float)dlp->astep / 180.0f);
        f = 90.0f * c;
        dlp->cur_cbb[4] = (unsigned char)ps2_d2i(ps2_dadd(90.0, ps2_dfabs(ps2_f2d(f))));
        c = fsmath_CosVU0(pi * (float)dlp->astep / 180.0f);
        f = 45.0f * c;
        dlp->cur_cbb[5] = (unsigned char)ps2_d2i(ps2_dadd(45.0, ps2_dfabs(ps2_f2d(f))));
        c = fsmath_CosVU0(pi * (float)dlp->astep / 180.0f);
        f = 20.0f * c;
        dlp->cur_cbb[6] = (unsigned char)ps2_d2i(ps2_dadd(20.0, ps2_dfabs(ps2_f2d(f))));
        dlp->cur_cbb[7] = 0x80;
    }
    dlp->astep++;
    FlushCache(0);
    DF_MDRAW_LoadImage(dlp->edt_cbb[0], 0x3d28, 0, 0, 0, 16, 16);
    DF_MDRAW_LoadImage(dlp->cur_cbb, 0x3d2c, 0, 0, 0, 16, 16);
    DF_MDRAW_LoadImage(dlp->edt_tbb[0], 0x3c40, 0x13, 0, 0, 128, 128);
    DF_MDRAW_LoadImage(dlp->plt_tbb, 0x3c80, 0x13, 0, 0, 32, 32);

    /* zoom changed: re-centre the view on the cursor (x clamps low then high, y high then low) */
    if (dlp->prev_zoom != dlp->rate_zoom) {
        dlp->top_curx = (short)(dlp->curx[0] - ps2_div32(dlp->prev_curx[0], dlp->rate_zoom));
        if (dlp->top_curx < 0 || dlp->rate_zoom == 2) dlp->top_curx = 0;
        lim = 0x80 - ps2_div32(0x100, dlp->rate_zoom);
        if (lim < dlp->top_curx) dlp->top_curx = (short)lim;
        dlp->top_cury = (short)(dlp->cury[0] - ps2_div32(dlp->prev_cury[0], dlp->rate_zoom));
        lim = 0x80 - ps2_div32(0x100, dlp->rate_zoom);
        if (lim < dlp->top_cury) dlp->top_cury = (short)lim;
        if (dlp->top_cury < 0 || dlp->rate_zoom == 2) dlp->top_cury = 0;
    }
    padRepeatIntv = membe_obj_dlp(inObjp)->rate_zoom / 4 + 2;
    if (padRepeatIntv >= 7) padRepeatIntv = 6;

    dlp->prev_curx[0] = (short)(dlp->rate_zoom * (dlp->curx[0] - dlp->top_curx));
    dlp->prev_cury[0] = (short)(dlp->rate_zoom * (dlp->cury[0] - dlp->top_cury));
    membe_view_uv(dlp->edt_wmp, dlp);
    memset(dlp->scp_tbb, 0, 0x1000);

    /* scope: filled frame, interior cleared (insets 3 on x, 2 on y) */
    {
        int v = ps2_div32(0x100, dlp->rate_zoom);
        int xx = dlp->top_curx + v - 1;
        if (xx >= 0x80) xx = 0x7f;
        int yy = dlp->top_cury + v - 1;
        if (yy >= 0x80) yy = 0x7f;
        __EME_Layer_DrawBox(dlp->top_curx / 2, dlp->top_cury / 2, xx / 2, yy / 2, dlp->scp_tbb, 1, 2);
        __EME_Layer_DrawBox((dlp->top_curx + 3) / 2, (dlp->top_cury + 2) / 2, (xx - 3) / 2, (yy - 2) / 2,
                            dlp->scp_tbb, 1, 0);
    }
    FlushCache(0);
    DF_MDRAW_LoadImage(dlp->scp_tbb, 0x3cc4, 0x13, 0, 0, 64, 64);
    dlp->prev_zoom = dlp->rate_zoom;

    /* tool preview layer */
    if (dlp->f_layer != 0) {
        int fidx = dlp->curx[1] + (dlp->cury[1] << 2);
        int cfidx = dlp->coidx;
        memset(dlp->lyr_tbb, 0, 0x4000);
        memset(dlp->lyr_tbb2, 0, 0x4000);
        switch (fidx) {   /* OLMENU jump table 0x1ee5800, fidx 3..9 */
        case 3:
            __EME_Func_DrawLine(dlp->curx[0], dlp->cury[0], cfidx, dlp, 1);
            break;
        case 4:
            if (cfidx < 2)
                __EME_Func_DrawBox(dlp->curx[0], dlp->cury[0], cfidx % 2, dlp, 1);
            else
                __EME_Func_DrawCircle(dlp->curx[0], dlp->cury[0], cfidx % 2, dlp, 1);
            break;
        case 5:
            if (dlp->step == 1) {
                __EME_Func_DrawBox(dlp->curx[0], dlp->cury[0], 0, dlp, 1);
            } else if (dlp->step == 2) {
                dlp->swork[0] = dlp->curx[0];
                dlp->swork[1] = dlp->cury[0];
                __EME_Func_DrawBox(dlp->curx[0] + dlp->swork[2] - 1, dlp->cury[0] + dlp->swork[3] - 1, 0, dlp, 1);
            }
            break;
        case 7:
            if (dlp->step == 1) {
                dlp->swork[0] = dlp->curx[0];
                dlp->swork[1] = dlp->cury[0];
                __EME_Func_DrawBox(dlp->curx[0] + dlp->swork[2] - 1, dlp->cury[0] + dlp->swork[3] - 1, 0, dlp, 1);
            }
            break;
        case 8: {   /* quirk: the cursor coordinate (not the distance) times the direction sign */
            int cx = dlp->curx[0], sw0 = dlp->swork[0];
            int dx = sw0 - cx;
            if (dx < 0) dx = -dx;
            int cy = dlp->cury[0], sw1 = dlp->swork[1];
            int dy = sw1 - cy;
            if (dy < 0) dy = -dy;
            int sx = (cx < sw0) ? -1 : 1;
            int sy = (cy < sw1) ? -1 : 1;
            if (dy < dx)
                __EME_Func_DrawBox(cx * sx, cx * sy, 0, dlp, 1);
            else
                __EME_Func_DrawBox(cy * sx, cy * sy, 0, dlp, 1);
            break;
        }
        case 9:
            if (dlp->step != 0) __EME_Func_DrawBox(dlp->curx[0], dlp->cury[0], 0, dlp, 1);
            break;
        default:
            break;
        }
        membe_view_uv(dlp->edt_lyrp, dlp);
        dlp->edt_lyrp->dt.sprite.texp = &dlp->lyr_texInfo;
        FlushCache(0);
        DF_MDRAW_LoadImage(dlp->lyr_tbb, 0x3c84, 0x13, 0, 0, 128, 128);
        DF_MDRAW_LoadImage(dlp->lyr_tbb2, 0x3ce8, 0x13, 0, 0, 128, 128);
        dlp->edt_lyrp->dt.sprite.cur_col.a = 0x80;
    } else {
        memset(dlp->lyr_tbb, 0, 0x4000);
        memset(dlp->lyr_tbb2, 0, 0x4000);
        if (dlp->f_edtCur != 0) {
            dlp->edt_lyrp->dt.sprite.cur_col.a = 0;
        } else {
            /* cursor box at screen scale: swork[0..1] temporarily = view offset / 2 */
            short swx = dlp->swork[0], swy = dlp->swork[1];
            dlp->swork[0] = (short)((dlp->rate_zoom * (dlp->curx[0] - dlp->top_curx)) / 2);
            dlp->swork[1] = (short)((dlp->rate_zoom * (dlp->cury[0] - dlp->top_cury)) / 2);
            int h = dlp->rate_zoom / 2;
            __EME_Func_DrawBox(dlp->swork[0] + h - 1, dlp->swork[1] + h - 1, 0, dlp, 3);
            dlp->swork[0] = swx;
            dlp->swork[1] = swy;
            dlp->edt_lyrp->dt.sprite.uv[0] = 0;
            dlp->edt_lyrp->dt.sprite.uv[1] = 0;
            dlp->edt_lyrp->dt.sprite.uv[2] = 0x80;
            dlp->edt_lyrp->dt.sprite.uv[3] = 0x80;
            dlp->edt_lyrp->dt.sprite.texp = &dlp->lyr2_texInfo;
            FlushCache(0);
            DF_MDRAW_LoadImage(dlp->lyr_tbb, 0x3c84, 0x13, 0, 0, 128, 128);
            DF_MDRAW_LoadImage(dlp->lyr_tbb2, 0x3ce8, 0x13, 0, 0, 128, 128);
            dlp->edt_lyrp->dt.sprite.cur_col.a = 0x80;
        }
    }

    /* palette layer: selected range (16x16 cells of 4x4 px), else the cursor cell and the pen colour */
    if (dlp->f_layer2 != 0 && dlp->plt_lyrp != NULL) {
        int sw0 = dlp->swork[0], sw1 = dlp->swork[1], cx2 = dlp->curx[2], cy2 = dlp->cury[2];
        int x0, y0, x1, y1;
        if (sw0 + (sw1 << 4) < cx2 + (cy2 << 4)) {
            x0 = sw0; y0 = sw1; x1 = cx2; y1 = cy2;
        } else {
            x0 = cx2; y0 = cy2; x1 = sw0; y1 = sw1;
        }
        int xh = x0 * 4, xt = (x1 + 1) * 4 - 1, yh = y0 * 4, yt = (y0 + 1) * 4 - 1;
        memset(dlp->ply_tbb, 0, 0x1000);
        __EME_Layer_DrawBox(xh, yh, (y0 == y1) ? xt : 0x3f, yt, dlp->ply_tbb, 1, 1);
        int n = y1 - y0;
        yh += 4;
        yt += 4;
        if (n >= 2) {
            for (ii = 1; ii < n; ii++) {   /* ii = t1 (= 1, kept by __EME_Layer_DrawBox) in the original */
                __EME_Layer_DrawBox(0, yh, 0x3f, yt, dlp->ply_tbb, 1, 1);
                yh += 4;
                yt += 4;
            }
        }
        if (n > 0) __EME_Layer_DrawBox(0, yh, xt, yt, dlp->ply_tbb, 1, 1);
        FlushCache(0);
        DF_MDRAW_LoadImage(dlp->ply_tbb, 0x3cd4, 0x13, 0, 0, 64, 64);
        dlp->plt_lyrp->dt.sprite.cur_col.a = 0x80;
    } else {
        short swx = dlp->swork[0], swy = dlp->swork[1];
        dlp->swork[0] = (short)(dlp->curx[2] << 2);
        dlp->swork[1] = (short)(dlp->cury[2] << 2);
        memset(dlp->ply_tbb, 0, 0x1000);
        __EME_Func_DrawBox(dlp->swork[0] + 3, dlp->swork[1] + 3, 0, dlp, 2);
        dlp->swork[0] = swx;
        dlp->swork[1] = swy;
        /* CSM1 CLUT order: swap the 8-entry blocks 8..15 / 16..23 of every 32 */
        int idx = dlp->curx[2] + (dlp->cury[2] << 4);
        int r = idx % 32;
        if (r >= 8 && r < 16)
            idx += 8;
        else if (r >= 16 && r < 24)
            idx -= 8;
        memcpy(&dlp->pen_wmp->dt.sprite.cur_col, dlp->edt_cbb[0] + idx * 4, 4);
        FlushCache(0);
        DF_MDRAW_LoadImage(dlp->ply_tbb, 0x3cd4, 0x13, 0, 0, 64, 64);
        /* quirk: also reached with f_layer2 != 0 and plt_lyrp == NULL (stores to PS2 address 0x53) */
        struct stMobj *pl = dlp->plt_lyrp;
        if (!pl) pl = (struct stMobj *)PS2_NULL_MEM();
        pl->dt.sprite.cur_col.a = 0x80;
    }

    /* edge frames: light the side / corner the screen cursor is near */
    dlp->f_edtFrame = 0;
    if (dlp->f_edtCur != 0) {
        int v = dlp->curx[3] - 0x160;
        if (v < 0x30)
            dlp->f_edtFrame |= 1;
        else if (v >= 0xd0)
            dlp->f_edtFrame |= 2;
        v = dlp->cury[3] - 0x38;
        if (v < 0x30)
            dlp->f_edtFrame |= 4;
        else if (v >= 0xd0)
            dlp->f_edtFrame |= 8;
    }
    for (ii = 0; ii < 8; ii++) {
        if (dlp->f_edtFrame == a_flag[ii])
            DF_MOBJ_SetDrawEnable(dlp->a_edit_frame[ii].p_light, 1);
        else
            DF_MOBJ_SetDrawEnable(dlp->a_edit_frame[ii].p_light, 0);
    }
}

/* Edit mobj (raw offsets): +0x10 dlp, +0x14 child system, +0x18 defTBP (MenuWorkp at entry), +0x1c pAC, +0x20 embp
 * (TM2 being edited), +0x24 sembp (small emblem TM2 or 0), +0x28 mstr_top_idx (unused), +0x2c s16 idx (tool-panel
 * cell), +0x2e s16 player, +0x30 FadeIn step, +0x31 func (confirm dialog open), +0x32 dirty (save on exit), +0x34
 * p_texture_data (BndTexture file buffer); stMobj count (+3) = fade frame counter. */

/* DF_MBGUIDE_Change / DF_MBACK_ChangeTag are void(void) in DWARF but read a0..t0 / a0 */
static inline void membe_guide5(int a, int b, int c, int d, int e)
{
    gcall_as<int(int, int, int, int, int)>(GFN(DF_MBGUIDE_Change), a, b, c, d, e);
}
static inline void membe_tag(int tag)
{
    gcall_as<void(int)>(GFN(DF_MBACK_ChangeTag), tag);
}

static inline struct stMenuAC *membe_edit_ac(struct stMobj *obj, int ii)
{
    struct stMenuAC *pAC = *(struct stMenuAC **)((char *)obj + 0x1c);
    return pAC ? pAC : M_ACp[MEMBE_S16(obj, 0x2e)][ii];
}

/* Leaving the editor (cancel without changes / the dialog's yes or no): fade the screen out, then FadeOut. */
static inline void membe_edit_leave(struct stMobj *ioObjp)
{
    __FadeOutWindowList(membe_obj_dlp(ioObjp));
    DF_M1HELP_FadeOut();
    Voice_Call(0x3ed, 0x7f);
    membe_tag(0xb);
    membe_guide5(0, 1, 2, 0, 0x63);
    DF_MBACK_PopSceneLabel();
    DF_MVIEW_MotionStart(MViewp, 0x21);
    ioObjp->stat = 2;
    ioObjp->play_func = (void (*)(void))GFN(__PlayEmbEditSystemFadeOut);
    ioObjp->count = 0;
}

/* pkt_func of the editor mobj: empty. */
static void __PktEmbEditSystem(void)
{
}

/* Waits for the child system (Func / Palette / Draw / Mirror / ...) or for the confirm dialog. The original reads the
 * child's state before its null test (guarded load here); f_canPalet is forced back to 1. Dialog: 4 = back to
 * editing, 0x14 = yes (save), 0x24 = no (discard); both leave. */
static void __PlayEmbEditSystemDispatch(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    struct struct_fedcf029 *dlp = membe_obj_dlp(ioObjp);
    if (MEMBE_S8(ioObjp, 0x31) == 0) {
        struct stMobj **childp = (struct stMobj **)((char *)ioObjp + 0x14);
        struct stMobj *child = *childp;
        unsigned char st = child ? child->stat : ((const unsigned char *)PS2_NULL_MEM())[2];
        if (st != 5) return;
        if (child) {
            child->f_draw = 0;
            (*childp)->f_erase = 1;
            *childp = 0;
        }
        dlp->old_x = 0;
        dlp->old_y = 0;
        dlp->f_canPalet = 1;
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbEditSystem);
        ioObjp->stat = 0;
        return;
    }
    int r = DF_MDIALOG_Sync();
    if (!(r & 4)) return;
    if (r == 4) {
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbEditSystem);
        ioObjp->stat = 0;
        return;
    }
    if (r != 0x24 && r != 0x14) return;
    MEMBE_S8(ioObjp, 0x32) = (signed char)(r == 0x14);
    MEMBE_S8(ioObjp, 0x31) = 0;
    membe_edit_leave(ioObjp);
}

/* Leaving: after 7 frames and the view motion, saves the emblem when dirty, deletes the screen, fades the AC back in,
 * frees the BndTexture and restores MenuWorkp. The func != 0 arm is dead (both callers clear func). */
static void __PlayEmbEditSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    ioObjp->count = (unsigned char)(ioObjp->count + 1);
    if (!DF_MVIEW_MotionSync(MViewp)) return;
    if (ioObjp->count < 7) return;
    if (MEMBE_S8(ioObjp, 0x31) != 0) {
        MEMBE_S8(ioObjp, 0x31) = 0;
        ioObjp->stat = 3;
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbEditSystemDispatch);
        return;
    }
    if (MEMBE_S8(ioObjp, 0x32)) {
        struct struct_fedcf029 *dlp = membe_obj_dlp(ioObjp);
        DF_EMB_MakeTm2(*(void **)((char *)ioObjp + 0x20), dlp, (char *)dlp + 0x16800);
        void *sembp = *(void **)((char *)ioObjp + 0x24);
        if (sembp) DF_EMB_MakeSmallEmblem(sembp, *(void **)((char *)ioObjp + 0x20));
    }
    __DeleteWindowList(membe_obj_dlp(ioObjp));
    membe_guide5(0, 1, 2, 0, 0x63);
    DF_M_AC_SetFadeIn(membe_edit_ac(ioObjp, 0), 0xf, 0.0f);
    MenuTex_UnRegisterBndTexture(*(void **)((char *)ioObjp + 0x34));
    ioObjp->stat = 5;
    ioObjp->play_func = 0;
    ioObjp->draw_func = 0;
    MenuWorkp = *(unsigned char **)((char *)ioObjp + 0x18);
}

/* Editor main loop: Start (key guide), cancel (confirm dialog when the image changed), decide (Func panel for the
 * rows with sub-icons, the row's constructor for the others), undo swap, palette (R1 / f_callPalette), grid / zoom
 * rates, tool-panel cell cursor (4 x 3: pad wraps, mouse clamps), idx = x + 4 * y, light + help text follow it.
 * Quirks kept: undo falls through to the rates / cursor; the decide switch has no default. */
static void __PlayEmbEditSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x70);
    /* OLMENU 0x1edd540: 1 = open the Func panel, 0 = call funcList[row][0] directly */
    static const short maxIdxTable[12] = {1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0};
    struct struct_fedcf029 *dlp = membe_obj_dlp(ioObjp);
    short pre_idx = MEMBE_S16(ioObjp, 0x2c); /* read before IsShown (0x1e46b90) */

    if (MenuKeyGuide_IsShown() == 1) return;
    if (MPad[MPadUser].start == 1) {
        MenuKeyGuide_Show(0x22);
        return;
    }
    /* cancel */
    if (MPad[MPadUser].Rrt == 1 || eMouse->rbtn == 1) {
        Voice_Call(0x3eb, 0x7f);
        if (DF_EMB_Cmp(dlp, (char *)dlp + 0x16800, *(void **)((char *)ioObjp + 0x20)) != 0) {
            unsigned char *str = DF_MSTR_GetInfo((signed char)MenuMng.language, 1, 0x30d4a);
            DF_MDIALOG_Open(2, str, 0, 0, 0, 0, 1, 0);
            MEMBE_S8(ioObjp, 0x31) = 1;
            ioObjp->stat = 3;
            ioObjp->play_func = (void (*)(void))GFN(__PlayEmbEditSystemDispatch);
            return;
        }
        MEMBE_S8(ioObjp, 0x31) = 0;
        membe_edit_leave(ioObjp);
        return;
    }
    /* decide */
    if (MPad[MPadUser].Rdw == 1 || eMouse->lbtn == 1) {
        Voice_Call(0x3ea, 0x7f);
        dlp->old_x = 0;
        dlp->old_y = 0;
        int idx = MEMBE_S16(ioObjp, 0x2c);
        struct stMobj *child;
        switch (maxIdxTable[idx]) {
        case 0: {
            struct struct_fedcf029 *a1 = membe_obj_dlp(ioObjp);
            membe_func_t fp = (&funcList[0][0])[idx * 6];
            child = fp ? gcall(fp, ioObjp, a1) : 0;
            break;
        }
        case 1:
            child = __SetNewFuncSystem(ioObjp, 0);
            break;
        case 2:
            child = __SetNewFuncSystem(ioObjp, 1);
            break;
        default:
            return;
        }
        *(struct stMobj **)((char *)ioObjp + 0x14) = child;
        MEMBE_S8(ioObjp, 0x31) = 0;
        ioObjp->stat = 3;
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbEditSystemDispatch);
        return;
    }
    /* undo: swap the two CLUTs (f_undo) or the two images, MenuWorkp as scratch; no return */
    if (MPad[MPadUser].Rlt == 1) {
        Voice_Call(0x3eb, 0x7f);
        if ((signed char)dlp->f_undo) {
            memcpy(MenuWorkp, dlp->edt_cbb[0], 0x400);
            memcpy(dlp->edt_cbb[0], dlp->edt_cbb[1], 0x400);
            memcpy(dlp->edt_cbb[1], MenuWorkp, 0x400);
        } else {
            memcpy(MenuWorkp, dlp->edt_tbb[0], 0x4000);
            memcpy(dlp->edt_tbb[0], dlp->edt_tbb[1], 0x4000);
            memcpy(dlp->edt_tbb[1], MenuWorkp, 0x4000);
        }
    }
    /* palette (R1 or a pending f_callPalette request) */
    if (MPad[MPadUser].repeat.r1 || (signed char)dlp->f_callPalette == 1) {
        Voice_Call(0x3ea, 0x7f);
        dlp->f_callPalette = 2;
        *(struct stMobj **)((char *)ioObjp + 0x14) = __SetNewPaletteSystem(ioObjp, membe_obj_dlp(ioObjp));
        MEMBE_S8(ioObjp, 0x31) = 0;
        ioObjp->stat = 3;
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbEditSystemDispatch);
        return;
    }

    /* grid rate: L1 / middle button + wheel back doubles (1..8), middle button + wheel forward halves */
    if (MPad[MPadUser].repeat.l1 || (eMouse->wbtn > 0 && eMouse->w < 0)) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->rate_grid = (char)((signed char)membe_obj_dlp(ioObjp)->rate_grid * 2);
        if ((signed char)membe_obj_dlp(ioObjp)->rate_grid >= 9) membe_obj_dlp(ioObjp)->rate_grid = 1;
    }
    if (eMouse->wbtn > 0 && eMouse->w > 0) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->rate_grid = (char)((signed char)membe_obj_dlp(ioObjp)->rate_grid / 2);
        if ((signed char)membe_obj_dlp(ioObjp)->rate_grid <= 0) membe_obj_dlp(ioObjp)->rate_grid = 8;
    }
    /* zoom rate: R2 / wheel forward doubles (2..16), L2 / wheel back halves */
    if (MPad[MPadUser].repeat.r2 || (eMouse->wbtn == 0 && eMouse->w > 0)) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->rate_zoom = (char)((signed char)membe_obj_dlp(ioObjp)->rate_zoom * 2);
        if ((signed char)membe_obj_dlp(ioObjp)->rate_zoom >= 17) membe_obj_dlp(ioObjp)->rate_zoom = 2;
    }
    if (MPad[MPadUser].repeat.l2 || (eMouse->wbtn == 0 && eMouse->w < 0)) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->rate_zoom = (char)((signed char)membe_obj_dlp(ioObjp)->rate_zoom / 2);
        if ((signed char)membe_obj_dlp(ioObjp)->rate_zoom < 2) membe_obj_dlp(ioObjp)->rate_zoom = 16;
    }

    /* tool-panel cell cursor: curx[1] 0..3, cury[1] 0..2 */
    if (MPad[MPadUser].repeat.rt) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->curx[1] = (short)(membe_obj_dlp(ioObjp)->curx[1] + 1);
        if (!(membe_obj_dlp(ioObjp)->curx[1] < 4)) membe_obj_dlp(ioObjp)->curx[1] = 0;
    }
    if ((signed char)dlp->old_x / 4 > 0) {
        Voice_Call(0x3e9, 0x7f);
        dlp->old_x = 0;
        membe_obj_dlp(ioObjp)->curx[1] = (short)(membe_obj_dlp(ioObjp)->curx[1] + 1);
        if (!(membe_obj_dlp(ioObjp)->curx[1] < 4)) membe_obj_dlp(ioObjp)->curx[1] = 3;
    }
    if (MPad[MPadUser].repeat.lt) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->curx[1] = (short)(membe_obj_dlp(ioObjp)->curx[1] - 1);
        if (membe_obj_dlp(ioObjp)->curx[1] < 0) membe_obj_dlp(ioObjp)->curx[1] = 3;
    }
    if ((signed char)dlp->old_x / 4 < 0) {
        Voice_Call(0x3e9, 0x7f);
        dlp->old_x = 0;
        membe_obj_dlp(ioObjp)->curx[1] = (short)(membe_obj_dlp(ioObjp)->curx[1] - 1);
        if (membe_obj_dlp(ioObjp)->curx[1] < 0) membe_obj_dlp(ioObjp)->curx[1] = 0;
    }
    if (MPad[MPadUser].repeat.up) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->cury[1] = (short)(membe_obj_dlp(ioObjp)->cury[1] - 1);
        if (membe_obj_dlp(ioObjp)->cury[1] < 0) membe_obj_dlp(ioObjp)->cury[1] = 2;
    }
    if ((signed char)dlp->old_y / 4 < 0) {
        Voice_Call(0x3e9, 0x7f);
        dlp->old_y = 0;
        membe_obj_dlp(ioObjp)->cury[1] = (short)(membe_obj_dlp(ioObjp)->cury[1] - 1);
        if (membe_obj_dlp(ioObjp)->cury[1] < 0) membe_obj_dlp(ioObjp)->cury[1] = 0;
    }
    if (MPad[MPadUser].repeat.dw) {
        Voice_Call(0x3e9, 0x7f);
        membe_obj_dlp(ioObjp)->cury[1] = (short)(membe_obj_dlp(ioObjp)->cury[1] + 1);
        if (!(membe_obj_dlp(ioObjp)->cury[1] < 3)) membe_obj_dlp(ioObjp)->cury[1] = 0;
    }
    if ((signed char)dlp->old_y / 4 > 0) {
        Voice_Call(0x3e9, 0x7f);
        dlp->old_y = 0;
        membe_obj_dlp(ioObjp)->cury[1] = (short)(membe_obj_dlp(ioObjp)->cury[1] + 1);
        if (!(membe_obj_dlp(ioObjp)->cury[1] < 3)) membe_obj_dlp(ioObjp)->cury[1] = 2;
    }

    /* selection; the light and the help text follow a change */
    MEMBE_S16(ioObjp, 0x2c) =
        (short)(membe_obj_dlp(ioObjp)->curx[1] + (membe_obj_dlp(ioObjp)->cury[1] << 2));
    short idx = MEMBE_S16(ioObjp, 0x2c);
    if (pre_idx == idx) return;
    struct struct_214acd28 pos;
    UnitSprite_GetPosition(membe_obj_dlp(ioObjp)->a_icon[idx].p_button, &pos);
    pos.x = pos.x + s_ButtonLightOffset.x;
    pos.y = pos.y + s_ButtonLightOffset.y;
    UnitSprite_SetPosition(membe_obj_dlp(ioObjp)->p_icon_light, &pos);
    DF_M1HELP_ChangeString(DF_MSTR_GetInfo((signed char)MenuMng.language, 0, MEMBE_S16(ioObjp, 0x2c) + 0x1ae45));
}

/* Entry: step 0 starts the async read of the editor's texture file (0x1018) into the pool, step 1 polls it and
 * registers the BndTexture, step 2 (frame 15 + view motion) builds the screen, hides the AC and shows the help
 * text, step 3 starts the main loop. The pool pointer advances by the raw file size (no realign). */
static void __PlayEmbEditSystemFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    if (MEMBE_S8(ioObjp, 0x30) < 3) ioObjp->count = (unsigned char)(ioObjp->count + 1);
    signed char step = MEMBE_S8(ioObjp, 0x30);
    if (step == 3) {
        ioObjp->play_func = (void (*)(void))GFN(__PlayEmbEditSystem);
        ioObjp->stat = 0;
    } else if (step == 2) {
        if (!DF_MVIEW_MotionSync(MViewp)) return;
        if (ioObjp->count < 0xf) return;
        __FadeInWindowList(ioObjp, membe_obj_dlp(ioObjp));
        for (int ii = 0; ii < 3; ii++) membe_edit_ac(ioObjp, ii)->flag.draw = 0;
        DF_M1HELP_SetString(DF_MSTR_GetInfo((signed char)MenuMng.language, 0, MEMBE_S16(ioObjp, 0x2c) + 0x1ae45));
        DF_M1HELP_FadeIn();
        Voice_Call(0x3ec, 0x7f);
        MEMBE_S8(ioObjp, 0x30) = (signed char)(MEMBE_S8(ioObjp, 0x30) + 1);
    } else if (step == 1) {
        if (Cdvd_CdSync() == 1) return; /* the asm passes a0 = 0; the callee sets a0 = 1 itself */
        if (File_IsAsyncBusy() == 1) return;
        int size = File_GetSize(0x1018);
        MenuWorkp = MenuWorkp + size;
        MenuTex_RegisterBndTexture(*(void **)((char *)ioObjp + 0x34));
        MEMBE_S8(ioObjp, 0x30) = (signed char)(MEMBE_S8(ioObjp, 0x30) + 1);
    } else if (step == 0) {
        MenuWorkp = (unsigned char *)DF_MFUNC_Aligned(MenuWorkp, 0x40);
        *(unsigned char **)((char *)ioObjp + 0x34) = MenuWorkp;
        File_Read(0x1018, -1, *(char **)((char *)ioObjp + 0x34), 0);
        MEMBE_S8(ioObjp, 0x30) = (signed char)(MEMBE_S8(ioObjp, 0x30) + 1);
    }
}

/* Creates the emblem editor as a child of inObjp for player inPlayer (pAC: the AC being painted, else the player's
 * emblem slots), carves [eMouse 0x80][dlp 0x176e0] out of MenuWorkp, clears it and unpacks the emblem TM2. */
struct stMobj *DF_MEMBE_NewSystem(struct stMobj *inObjp, int inPlayer, struct stMenuAC *pAC)
{
    PS2_FRAME(0x50);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    obj->type = 0;
    obj->stat = 1;
    MEMBE_S16(obj, 0x2e) = (short)inPlayer;
    *(struct stMenuAC **)((char *)obj + 0x1c) = pAC;
    if (pAC) {
        *(unsigned char **)((char *)obj + 0x20) = pAC->emblem;
        *(char **)((char *)obj + 0x24) = 0;
    } else {
        *(char **)((char *)obj + 0x20) = EmblemTM2[MEMBE_S16(obj, 0x2e)];
        *(char **)((char *)obj + 0x24) = EmblemSTM2[MEMBE_S16(obj, 0x2e)];
    }
    membe_tag(0xd);
    *(int *)((char *)obj + 0x28) = 0x18ed4;
    DF_MBACK_PushSceneLabel((enum_5124d09c)0x29);
    membe_guide8(8, 0xa, 9, 1, 2, 0, 0xd, 0x63);
    DF_M_AC_SetFadeOut(membe_edit_ac(obj, 0), 5, 0.0f);
    DF_MVIEW_MotionStart(MViewp, 0x32);

    /* work block: [eMouse 0x80][dlp 0x176e0][pool] */
    *(unsigned char **)((char *)obj + 0x18) = MenuWorkp;
    eMouse = (struct struct_a703300a *)DF_MFUNC_Aligned(MenuWorkp, 0x10);
    *(struct struct_fedcf029 **)((char *)obj + 0x10) = (struct struct_fedcf029 *)((char *)eMouse + 0x80);
    MenuWorkp = (unsigned char *)membe_obj_dlp(obj) + 0x176e0;
    memset(membe_obj_dlp(obj), 0, 0x176e0);
    Ac2_Emb_MouseInit();

    membe_obj_dlp(obj)->curx[2] = 0;
    membe_obj_dlp(obj)->curx[1] = 0;
    membe_obj_dlp(obj)->curx[0] = 0;
    membe_obj_dlp(obj)->curx[3] = 0x160;
    membe_obj_dlp(obj)->cury[2] = 0;
    membe_obj_dlp(obj)->cury[1] = 0;
    membe_obj_dlp(obj)->cury[0] = 0;
    membe_obj_dlp(obj)->cury[3] = 0x38;
    membe_obj_dlp(obj)->top_curx = 0;
    membe_obj_dlp(obj)->top_cury = 0;
    membe_obj_dlp(obj)->rate_zoom = 2;
    membe_obj_dlp(obj)->prev_zoom = 2;
    membe_obj_dlp(obj)->rate_grid = 1;
    membe_obj_dlp(obj)->f_canPalet = 1;
    padRepeatIntv = (signed char)membe_obj_dlp(obj)->rate_zoom / 4 + 2;
    if (!(padRepeatIntv < 7)) padRepeatIntv = 6;

    DF_EMB_DecompTm2(membe_obj_dlp(obj), (char *)membe_obj_dlp(obj) + 0x16800, *(void **)((char *)obj + 0x20));
    obj->play_func = (void (*)(void))GFN(__PlayEmbEditSystemFadeIn);
    obj->pkt_func = (void (*)(void))GFN(__PktEmbEditSystem);
    obj->draw_func = (void (*)(void))GFN(__DrawEmbEditSystem);
    obj->count = 0;
    return obj;
}

#undef MEMBE_S8
#undef MEMBE_S16
