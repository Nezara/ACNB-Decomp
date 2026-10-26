/* src/menu/mpaint.cpp - the garage "Paint" screen (OLMENU, item 3 of the garage top menu; single-player and Versus
 * garage). Three levels of stMobj state machines run by DF_MOBJ_Play (play_func per frame):
 *   - PaintSystem (DF_MPAINT_NewSystem, struct_a18fe7e2 dt): loads the paint data file (LSN 0xbbd) into MenuWorkp,
 *     shows the 3-row top list (Colour / Emblem / Weapon colour) and on decide spawns the chosen sub-system (or the
 *     weapon-colour selector of mwepcolor1); the two error dialogs (incomplete AC / no colourable weapon);
 *   - PaintSubSystem (__NewPaintSubSystem, same dt): the sub-list of Colour (3 rows) or Emblem (2 rows); decide spawns
 *     the editor of another file (DF_MCOLOR_NewSmplColSystem / NewEditColSystem, DF_MSEMB_NewSystem /
 *     DF_MEMBE_NewSystem), cancel backs out into the top list;
 *   - the label-list objects (g2: Emb / Color / plain, sliding fade-in / fade-out and the blinking row cursor).
 * In this file +5 (cross) decides and +7 (circle) cancels.
 * The label lists: Emb = layout 0x45a (sprites 0-3, strings 4-5), Color / plain = layout 0x45b (sprites 0-5, strings
 * 6-8); they slide in from -320 with a trailing offset per column and slide out again, the row pitch is 56 px. */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU 0x1ee5eb0 / 0x1ee5ee0: the label layout parts ("ita_paint0..2" OLMENU 0x1edfa40, "mark0..2" / "string0..2" /
 * "cursor" main sdata 0x27bc78..0x27bca8); the last word is the blink cursor name */
char *sGaragePaint3Layout[10] = {
    (char *)"ita_paint0", (char *)"ita_paint1", (char *)"ita_paint2", (char *)"mark0",   (char *)"mark1",
    (char *)"mark2",      (char *)"string0",    (char *)"string1",    (char *)"string2", (char *)"cursor",
};
char *sGaragePaint2Layout[7] = {
    (char *)"ita_paint0", (char *)"ita_paint1", (char *)"mark0", (char *)"mark1",
    (char *)"string0",    (char *)"string1",    (char *)"cursor",
};
#endif

#define MPAINT_SYS(o) ((struct struct_a18fe7e2 *)&(o)->dt)
#define MPAINT_LIST3(o) ((struct _mobj_menu_garage_paint3_st *)&(o)->dt)
#define MPAINT_LIST2(o) ((struct _mobj_menu_garage_paint2_st *)&(o)->dt)
#define MPAINT_PLAY(f) ((void (*)(void))GFN(f))

/* DF_MBGUIDE_Change / DF_MBACK_ChangeTag are void(void) in DWARF but read a0..t0 / a0 */
static inline void mpaint_guide2(int a, int b)
{
    gcall_as<int(int, int)>(GFN(DF_MBGUIDE_Change), a, b);
}

static inline void mpaint_guide3(int a, int b, int c)
{
    gcall_as<int(int, int, int)>(GFN(DF_MBGUIDE_Change), a, b, c);   /* a3 / t0 stale in the original */
}

static inline void mpaint_guide5(int a, int b, int c, int d, int e)
{
    gcall_as<int(int, int, int, int, int)>(GFN(DF_MBGUIDE_Change), a, b, c, d, e);
}

static inline void mpaint_change_tag(int tag)
{
    gcall_as<void(int)>(GFN(DF_MBACK_ChangeTag), tag);
}

/* child->f_draw = 0; child->f_erase = 1; slot = NULL (two byte RMWs, the slot re-read between them) */
static inline void mpaint_erase(struct stMobj **slot)
{
    (*slot)->f_draw = 0;
    (*slot)->f_erase = 1;
    *slot = 0;
}

/* stMenuAC connect.cur.flag.incomplete (byte 0x186 bit 4): the AC may not be repainted */
static inline int mpaint_ac_incomplete(struct stMenuAC *ac)
{
    return (((unsigned char *)ac)[0x186] >> 4) & 1;
}

/* the top label list: play_func = __MenuGarage_PaintFadeOut, sprites 0-5 / strings 6-8 / cursor fade out */
static void mpaint_top_label_fade_out(struct stMobj *label)
{
    struct _mobj_menu_garage_paint3_st *w = MPAINT_LIST3(label);
    int i;

    label->play_func = MPAINT_PLAY(__MenuGarage_PaintFadeOut);
    for (i = 0; i < 6; i++) UnitSprite_SetFadeOut(w->label[i]);
    for (; i < 9; i++) DF_MOBJ_SetFadeOut(w->label[i]);
    UnitSprite_SetFadeOut(w->cursor);
}

/* shared tail of the +7 / +5 arms of __PlayPaintSystem */
static void mpaint_sys_fade_out(struct stMobj *ioObjp)
{
    struct struct_a18fe7e2 *dt = MPAINT_SYS(ioObjp);

    mpaint_top_label_fade_out(dt->label);
    if (dt->idx != 2) DF_M1HELP_FadeOut();
    if (dt->csysp == 0 && dt->pAC == 0) {
        mpaint_change_tag(0);
        mpaint_guide5(4, 1, 2, 3, 0x63);
    }
    ioObjp->stat = 2;
    ioObjp->play_func = MPAINT_PLAY(__PlayPaintSystemFadeOut);
}

/* error dialog of the +5 arm (ctgr 1 message), then wait for it in __PlayPaintSystemDispatch */
static void mpaint_sys_error(struct stMobj *ioObjp, unsigned int msg)
{
    struct struct_a18fe7e2 *dt = MPAINT_SYS(ioObjp);

    mpaint_guide2(0xe, 0x63);
    DF_MDIALOG_Open(1, DF_MSTR_GetInfo(MenuMng.language, 1, msg), 0, 0, 0, 0, 0, 0);
    dt->dispatch = 1;
    ioObjp->play_func = MPAINT_PLAY(__PlayPaintSystemDispatch);
    ioObjp->stat = 3;
}

/* ---------------------------------------------------------------- PaintSystem (top list) */

static void __DrawPaintSystem(struct stMobj *ioObjp)
{
    struct struct_a18fe7e2 *dt = MPAINT_SYS(ioObjp);

    PS2_FRAME(0x20);
    if (dt->drawtexstring) {
        int buf[3] = {0x1959d, 0x1959f, 0x1959c};   /* OLMENU 0x1edfa88 */
        dt->drawtexstring = 0;
        MenuUtil_RenderTexStringAll(buf, 3);
    }
}

static void __PlayPaintSystemDispatch(struct stMobj *ioObjp)
{
    struct struct_a18fe7e2 *dt = MPAINT_SYS(ioObjp);

    PS2_FRAME(0x30);
    if (dt->dispatch == 0) {
        /* child stat read before the null test (original order) */
        if (dt->csysp->stat == 5) {
            if (dt->csysp) mpaint_erase(&dt->csysp);
            dt->step = 2;
            ioObjp->stat = 1;
            ioObjp->count = 0xf;
            ioObjp->play_func = MPAINT_PLAY(__PlayPaintSystemFadeIn);
            DF_MVIEW_MotionStart(MViewp, 0x21);
            dt->drawtexstring = 1;
        }
    } else {
        int r = DF_MDIALOG_Sync();
        if ((r & 4) && (r == 4 || r == 0x24 || r == 0x14)) {
            ioObjp->play_func = MPAINT_PLAY(__PlayPaintSystem);
            ioObjp->stat = 0;
            mpaint_guide3(1, 2, 0x63);
        }
    }
}

static void __PlayPaintSystemFadeOut(struct stMobj *ioObjp)
{
    struct struct_a18fe7e2 *dt = MPAINT_SYS(ioObjp);

    PS2_FRAME(0x20);
    if (DF_MVIEW_MotionSync(MViewp)) {
        if (dt->csysp == 0) {
            DF_MBACK_PopSceneLabel();
            MenuWorkp = (unsigned char *)MenuMng.dataTbl[4];   /* saved by __PlayPaintSystemFadeIn step 0 */
            ioObjp->stat = 5;
            ioObjp->play_func = 0;
        } else {
            dt->dispatch = 0;
            ioObjp->stat = 3;
            ioObjp->play_func = MPAINT_PLAY(__PlayPaintSystemDispatch);
        }
    }
}

static void __PlayPaintSystem(struct stMobj *ioObjp)
{
    struct struct_a18fe7e2 *dt = MPAINT_SYS(ioObjp);
    struct struct_2d84b07b *pad;

    PS2_FRAME(0x60);
    if (MenuKeyGuide_IsShown()) return;
    pad = &MPad[MPadUser];
    if (pad->start == 1) {
        MenuKeyGuide_Show(0x19);
        return;
    }
    if (pad->Rrt == 1) {   /* +7: cancel */
        Voice_Call(0x3eb, 0x7f);
        mpaint_sys_fade_out(ioObjp);
        return;
    }
    if (pad->repeat.up || pad->repeat.dw) {
        Voice_Call(0x3e9, 0x7f);
        if (MPad[MPadUser].repeat.up) dt->idx = (dt->idx + 2) % 3;
        else dt->idx = (dt->idx + 1) % 3;
        if (dt->label) ((struct _mobj_menu_garage_paint_st *)&dt->label->dt)->id = dt->idx;
        DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, dt->idx + 0x18812));
        return;
    }
    if (pad->Rdw == 1) {   /* +5: decide */
        Voice_Call(0x3ea, 0x7f);
        if (dt->idx == 0) {
            struct stMenuAC *ac = dt->pAC ? dt->pAC : M_ACp[dt->player][0];
            if (mpaint_ac_incomplete(ac)) {
                mpaint_sys_error(ioObjp, 0x30db0);
                return;
            }
        }
        if (dt->idx == 2) {
            struct stMenuAC *ac = dt->pAC ? dt->pAC : M_ACp[dt->player][0];
            if (!DF_M_AC_IsEquipColorChangableWep(ac)) {
                mpaint_sys_error(ioObjp, 0x30dcf);
                return;
            }
        }
        if (dt->idx != 2)
            dt->csysp = __NewPaintSubSystem(ioObjp, dt->idx, dt->player, dt->pAC);
        else
            dt->csysp = MenuWepColorSelPos_New(ioObjp, M_ACp[dt->player][0], dt->player);
        mpaint_sys_fade_out(ioObjp);
        return;
    }
    if (dt->label) ((struct _mobj_menu_garage_paint_st *)&dt->label->dt)->id = dt->idx;
}

static void __PlayPaintSystemFadeIn(struct stMobj *ioObjp)
{
    struct struct_a18fe7e2 *dt = MPAINT_SYS(ioObjp);

    PS2_FRAME(0x20);
    ioObjp->count++;
    switch (dt->step) {
    case 0:
        if (!DF_MMD_ReadSync(0)) break;
        if (Cdvd_CdSync() == 1) break;   /* the original passes a0 = 0 */
        MenuMng.dataTbl[4] = MenuWorkp;
        Cdvd_ReadLsnBlind(0xbbd, (char *)MenuWorkp, 0, 0);
        dt->step++;
        break;
    case 1:
        if (Cdvd_CdSync() == 1) break;
        MenuWorkp = MenuWorkp + DF_MDATA_Remap(MenuMng.dataTbl[4]);
        dt->step++;
        break;
    case 2:
        /* count is bumped at entry too: two per frame here */
        if (ioObjp->count++ < 0x1e) break;
        dt->label = __MenuGarage_PaintLabelInit(ioObjp, dt->idx);
        DF_M1HELP_SetString(DF_MSTR_GetInfo(MenuMng.language, 0, dt->idx + 0x18812));
        DF_M1HELP_FadeIn();
        dt->step++;
        break;
    case 3:
        if (dt->label->stat == 0) {
            ioObjp->play_func = MPAINT_PLAY(__PlayPaintSystem);
            ioObjp->stat = 0;
        }
        break;
    }
}

struct stMobj *DF_MPAINT_NewSystem(struct stMobj *inObjp, int inPlayer, struct stMenuAC *pAC)
{
    struct stMobj *obj;
    struct struct_a18fe7e2 *dt;

    PS2_FRAME(0x50);
    obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    dt = MPAINT_SYS(obj);
    obj->type = 0;
    obj->stat = 1;
    dt->player = inPlayer;
    dt->pAC = pAC;
    if (!pAC) {
        DF_M_AC_SetFadeOut(M_ACp[dt->player][1], 5, 0.0f);
        DF_M_AC_SetFadeOut(M_ACp[dt->player][2], 5, 0.0f);
    }
    mpaint_change_tag(8);
    mpaint_guide5(0, 1, 2, 0, 0x63);
    DF_MVIEW_MotionStart(MViewp, 0x21);
    obj->play_func = MPAINT_PLAY(__PlayPaintSystemFadeIn);
    obj->draw_func = MPAINT_PLAY(__DrawPaintSystem);
    obj->count = 0;
    dt->drawtexstring = 1;
    DF_MBACK_PushSceneLabel((enum_5124d09c)0x23);
    return obj;
}

/* ---------------------------------------------------------------- PaintSubSystem (Colour / Emblem sub-list) */

static void __DrawPaintSubSystem(struct stMobj *ioObjp)
{
    struct struct_a18fe7e2 *dt = MPAINT_SYS(ioObjp);

    PS2_FRAME(0x30);
    /* never raised for the sub-list: GetNewObject clears it and nothing in the file sets it (original) */
    if (dt->drawtexstring) {
        int ids[2][3] = {{0x19514, 0x1959e, 0x19516}, {0x1880d, 0x1880e, 0x1880f}};   /* OLMENU 0x1edfa70 */
        dt->drawtexstring = 0;
        MenuUtil_RenderTexStringAll(ids[(int)dt->func], 3);
    }
}

static void __PlayPaintSubSystemDispatch(struct stMobj *ioObjp)
{
    struct struct_a18fe7e2 *dt = MPAINT_SYS(ioObjp);

    /* leaf, no frame; child stat read before the null test (original order) */
    if (dt->csysp->stat == 5) {
        if (dt->csysp) mpaint_erase(&dt->csysp);
        dt->step = 0;
        ioObjp->stat = 1;
        ioObjp->count = 0xf;
        ioObjp->play_func = MPAINT_PLAY(__PlayPaintSubSystemFadeIn);
    }
}

static void __PlayPaintSubSystemFadeOut(struct stMobj *ioObjp)
{
    struct struct_a18fe7e2 *dt = MPAINT_SYS(ioObjp);

    PS2_FRAME(0x30);
    if (!DF_MVIEW_MotionSync(MViewp)) return;
    if (dt->mode == 0) {
        ioObjp->stat = 5;
        ioObjp->play_func = 0;
        DF_MBACK_PopSceneLabel();
        return;
    }
    switch (dt->func) {
    case 0:
        switch (dt->idx) {
        case 0:
            dt->csysp = DF_MCOLOR_NewSmplColSystem(ioObjp, dt->player, dt->pAC);
            break;
        case 1:
            dt->csysp = DF_MCOLOR_NewEditColSystem(ioObjp, dt->player, 0, dt->pAC);
            break;
        case 2:
            dt->csysp = DF_MCOLOR_NewEditColSystem(ioObjp, dt->player, 1, dt->pAC);
            break;
        }
        break;
    case 1:
        if (dt->idx == 0)
            dt->csysp = DF_MSEMB_NewSystem(ioObjp, dt->player, dt->pAC);
        else
            dt->csysp = DF_MEMBE_NewSystem(ioObjp, dt->player, dt->pAC);
        break;
    }
    ioObjp->stat = 3;
    ioObjp->play_func = MPAINT_PLAY(__PlayPaintSubSystemDispatch);
}

static void __PlayPaintSubSystem(struct stMobj *ioObjp)
{
    struct struct_a18fe7e2 *dt = MPAINT_SYS(ioObjp);
    struct struct_2d84b07b *pad;
    int count[2] = {3, 2};   /* sdata gp-0x6f98: rows per sub-list */

    PS2_FRAME(0x40);
    if (MenuKeyGuide_IsShown()) return;
    pad = &MPad[MPadUser];
    if (pad->start == 1) {
        MenuKeyGuide_Show(dt->func == 0 ? 0x1a : 0x21);
        return;
    }
    if (pad->Rrt == 1) {   /* +7: cancel */
        Voice_Call(0x3eb, 0x7f);
        dt->mode = 0;
        __SetPaintSubSystemFadeOut(ioObjp);
        return;
    }
    if (pad->repeat.up) {
        Voice_Call(0x3e9, 0x7f);
        dt->idx--;
        if (dt->idx < 0) dt->idx = count[(int)dt->func] - 1;
        DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, dt->mstr_top_idx + dt->idx));
        return;
    }
    if (pad->repeat.dw) {
        Voice_Call(0x3e9, 0x7f);
        dt->idx++;
        if (!(dt->idx < count[(int)dt->func])) dt->idx = 0;
        DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, dt->mstr_top_idx + dt->idx));
        return;
    }
    if (pad->Rdw == 1) {   /* +5: decide (falls through to the id mirror) */
        Voice_Call(0x3ea, 0x7f);
        dt->mode = 1;
        __SetPaintSubSystemFadeOut(ioObjp);
    }
    switch (dt->func) {
    case 0:
        if (dt->label) ((struct _mobj_menu_garage_paint_color_st *)&dt->label->dt)->id = dt->idx;
        break;
    case 1:
        if (dt->label) ((struct _mobj_menu_garage_paint_emb_st *)&dt->label->dt)->id = dt->idx;
        break;
    }
}

static void __PlayPaintSubSystemFadeIn(struct stMobj *ioObjp)
{
    struct struct_a18fe7e2 *dt = MPAINT_SYS(ioObjp);

    PS2_FRAME(0x20);
    switch (dt->step) {
    case 0:
        if (ioObjp->count++ < 0xf) break;
        switch (dt->func) {
        case 0:
            dt->label = __MenuGarage_PaintColorLabelInit(ioObjp, dt->idx);
            break;
        case 1:
            dt->label = __MenuGarage_PaintEmbLabelInit(ioObjp, dt->idx);
            break;
        }
        DF_M1HELP_SetString(DF_MSTR_GetInfo(MenuMng.language, 0, dt->mstr_top_idx + dt->idx));
        DF_M1HELP_FadeIn();
        dt->step++;
        break;
    case 1:
        if (dt->label->stat == 0) {
            ioObjp->play_func = MPAINT_PLAY(__PlayPaintSubSystem);
            ioObjp->stat = 0;
        }
        break;
    }
}

static void __SetPaintSubSystemFadeOut(struct stMobj *ioObjp)
{
    struct struct_a18fe7e2 *dt = MPAINT_SYS(ioObjp);
    int i;

    PS2_FRAME(0x60);
    switch (dt->func) {
    case 0: {
        struct _mobj_menu_garage_paint3_st *w;
        dt->label->play_func = MPAINT_PLAY(__MenuGarage_PaintColorFadeOut);
        w = MPAINT_LIST3(dt->label);
        for (i = 0; i < 6; i++) UnitSprite_SetFadeOut(w->label[i]);
        for (; i < 9; i++) DF_MOBJ_SetFadeOut(w->label[i]);
        UnitSprite_SetFadeOut(w->cursor);
        break;
    }
    case 1: {
        struct _mobj_menu_garage_paint2_st *w;
        dt->label->play_func = MPAINT_PLAY(__MenuGarage_PaintEmbFadeOut);
        w = MPAINT_LIST2(dt->label);
        for (i = 0; i < 4; i++) UnitSprite_SetFadeOut(w->label[i]);
        for (; i < 6; i++) DF_MOBJ_SetFadeOut(w->label[i]);
        UnitSprite_SetFadeOut(w->cursor);
        break;
    }
    }
    DF_M1HELP_FadeOut();
    if (dt->mode == 0) mpaint_change_tag(8);   /* cancel only */
    ioObjp->stat = 2;
    ioObjp->play_func = MPAINT_PLAY(__PlayPaintSubSystemFadeOut);
}

static struct stMobj *__NewPaintSubSystem(struct stMobj *inObjp, int inFunc, int inPlayer, struct stMenuAC *pAC)
{
    struct stMobj *obj;
    struct struct_a18fe7e2 *dt;

    PS2_FRAME(0x60);
    obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    dt = MPAINT_SYS(obj);
    obj->type = 0;
    obj->stat = 1;
    dt->player = inPlayer;
    dt->func = (char)inFunc;
    dt->pAC = pAC;
    switch (dt->func) {
    case 0:
        mpaint_change_tag(9);
        DF_MBACK_PushSceneLabel((enum_5124d09c)0x25);
        dt->mstr_top_idx = 0x19514;
        break;
    case 1:
        mpaint_change_tag(0xb);
        DF_MBACK_PushSceneLabel((enum_5124d09c)0x27);
        dt->mstr_top_idx = 0x1880d;
        break;
    }
    mpaint_guide5(0, 1, 2, 0, 0x63);
    obj->play_func = MPAINT_PLAY(__PlayPaintSubSystemFadeIn);
    obj->draw_func = MPAINT_PLAY(__DrawPaintSubSystem);
    obj->count = 0;
    return obj;
}

/* ---- g2: the label-list objects ---- */

/* obj->stat = 5, no callbacks, f_draw = 0 / f_erase = 1 (the original's `if (obj)` after the stores is dead) */
static inline void mpaint_obj_end(struct stMobj *obj)
{
    obj->stat = 5;
    obj->play_func = 0;
    obj->draw_func = 0;
    obj->pkt_func = 0;
    obj->f_draw = 0;
    obj->f_erase = 1;
}

/* the old cursor's carrier object: deletes the sprite on the 7th frame (once it has faded out) */
static void __MenuGaragePaint_DeleteCursor(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    unsigned char old = pMobj->count;

    pMobj->count = old + 1;
    if (old < 6) return;
    UnitSprite_Delete(((struct _mobj_menu_garage_paint_delete_cursor_st *)&pMobj->dt)->cursor);
    mpaint_obj_end(pMobj);
}

/* Emb list: sprites 0-3, strings label + 0..1, blink cursor (hidden, draw_prio 3); the layout is re-fetched per part */
static void __MenuGaragePaint2LabelMake(struct stMobj *pMobj, struct _mobj_menu_garage_paint2_st *pWork, int label)
{
    PS2_FRAME(0x90);
    int i, k;

    for (i = 0; i < 4; i++) {
        pWork->label[i] = UnitSprite_New(pMobj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x45a), sGaragePaint2Layout[i]);
        UnitSprite_SetFadeIn(pWork->label[i]);
    }
    for (k = 0; i < 6; i++, k++) {
        char *lo = (char *)LAYOUT_GetLayoutData(0x45a);
        pWork->label[i] = DF_MOBJ_NewStringFromLayoutName(pMobj, 0, label + k, 0, lo, sGaragePaint2Layout[i], NULL, 0, 0);
        DF_MOBJ_SetFadeIn(pWork->label[i]);
    }
    pWork->cursor = UnitSpriteBlink_New(pMobj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x45a), sGaragePaint2Layout[6]);
    UnitSprite_SetFadeIn(pWork->cursor);
    DF_MOBJ_SetDrawEnable(pWork->cursor, 0);
    pWork->cursor->draw_prio = 3;
}

/* Color / plain list: sprites 0-5, strings label + 0..2, blink cursor */
static void __MenuGaragePaint3LabelMake(struct stMobj *pMobj, struct _mobj_menu_garage_paint3_st *pWork, int label)
{
    PS2_FRAME(0x90);
    int i, k;

    for (i = 0; i < 6; i++) {
        pWork->label[i] = UnitSprite_New(pMobj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x45b), sGaragePaint3Layout[i]);
        UnitSprite_SetFadeIn(pWork->label[i]);
    }
    for (k = 0; i < 9; i++, k++) {
        char *lo = (char *)LAYOUT_GetLayoutData(0x45b);
        pWork->label[i] = DF_MOBJ_NewStringFromLayoutName(pMobj, 0, label + k, 0, lo, sGaragePaint3Layout[i], NULL, 0, 0);
        DF_MOBJ_SetFadeIn(pWork->label[i]);
    }
    pWork->cursor = UnitSpriteBlink_New(pMobj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x45b), sGaragePaint3Layout[9]);
    UnitSprite_SetFadeIn(pWork->cursor);
    DF_MOBJ_SetDrawEnable(pWork->cursor, 0);
    pWork->cursor->draw_prio = 3;
}

/* slides the Emb labels out to -320 (column i trails by i % 2 frames); returns 0 once settled after 8+ frames */
static int __MenuGarage_Paint2FadeOut(struct _mobj_menu_garage_paint2_st *pWork)
{
    PS2_FRAME(0x50);
    struct struct_214acd28 pos;
    int i, old;

    MoveMng_GetNextMoveValue(&pWork->move, -0x140, 8, 1);
    pWork->moveOffset[1] = pWork->moveOffset[0];
    pWork->moveOffset[0] = ps2_ftoi(pWork->move.vMove);
    if (pWork->moveOffset[0] < -0x140) pWork->moveOffset[0] = -0x140;
    pos.y = 0;
    for (i = 0; i < 4; i++) {
        pos.x = pWork->moveOffset[i % 2];
        UnitSprite_SetMove(pWork->label[i], &pos, 0);
    }
    for (; i < 6; i++) {
        pos.x = pWork->moveOffset[i % 2];
        pWork->label[i]->dt.string.x = (short)ps2_ftoi((float)pos.x + pWork->strOffset);   /* no NULL test */
    }
    old = pWork->count++;
    if (old < 8) return 1;
    return pWork->moveOffset[0] == pWork->moveOffset[1] ? 0 : 1;
}

/* Color / plain: three-column trail (i % 3) */
static int __MenuGarage_Paint3FadeOut(struct _mobj_menu_garage_paint3_st *pWork)
{
    PS2_FRAME(0x50);
    struct struct_214acd28 pos;
    int i, old;

    MoveMng_GetNextMoveValue(&pWork->move, -0x140, 8, 1);
    pWork->moveOffset[2] = pWork->moveOffset[1];
    pWork->moveOffset[1] = pWork->moveOffset[0];
    pWork->moveOffset[0] = ps2_ftoi(pWork->move.vMove);
    if (pWork->moveOffset[0] < -0x140) pWork->moveOffset[0] = -0x140;
    pos.y = 0;
    for (i = 0; i < 6; i++) {
        pos.x = pWork->moveOffset[i % 3];
        UnitSprite_SetMove(pWork->label[i], &pos, 0);
    }
    for (; i < 9; i++) {
        pos.x = pWork->moveOffset[i % 3];
        pWork->label[i]->dt.string.x = (short)ps2_ftoi((float)pos.x + pWork->strOffset);
    }
    old = pWork->count++;
    if (old < 8) return 1;
    return pWork->moveOffset[0] == pWork->moveOffset[2] ? 0 : 1;
}

/* slides the Emb labels in from -320; returns 1 (count reset) once settled after 9+ frames, else 0 */
static int __MenuGaragePaint2LabelInit(struct _mobj_menu_garage_paint2_st *pWork)
{
    PS2_FRAME(0x50);
    struct struct_214acd28 pos;
    int i, old;

    MoveMng_GetNextMoveValue(&pWork->move, 0x140, 8, 1);
    pWork->moveOffset[1] = pWork->moveOffset[0];
    pWork->moveOffset[0] = ps2_ftoi(pWork->move.vMove - 320.0f);
    if (pWork->moveOffset[0] > 0) pWork->moveOffset[0] = 0;
    pos.y = 0;
    for (i = 0; i < 4; i++) {
        pos.x = pWork->moveOffset[i % 2];
        UnitSprite_SetMove(pWork->label[i], &pos, 0);
    }
    for (; i < 6; i++) {
        pos.x = pWork->moveOffset[i % 2];
        pWork->label[i]->dt.string.x = (short)ps2_ftoi((float)pos.x + pWork->strOffset);
    }
    old = pWork->count++;
    if (old < 9 || pWork->moveOffset[0] != pWork->moveOffset[1]) return 0;
    pWork->count = 0;
    return 1;
}

static int __MenuGaragePaint3LabelInit(struct _mobj_menu_garage_paint3_st *pWork)
{
    PS2_FRAME(0x50);
    struct struct_214acd28 pos;
    int i, old;

    MoveMng_GetNextMoveValue(&pWork->move, 0x140, 8, 1);
    pWork->moveOffset[2] = pWork->moveOffset[1];
    pWork->moveOffset[1] = pWork->moveOffset[0];
    pWork->moveOffset[0] = ps2_ftoi(pWork->move.vMove - 320.0f);
    if (pWork->moveOffset[0] > 0) pWork->moveOffset[0] = 0;
    pos.y = 0;
    for (i = 0; i < 6; i++) {
        pos.x = pWork->moveOffset[i % 3];
        UnitSprite_SetMove(pWork->label[i], &pos, 0);
    }
    for (; i < 9; i++) {
        pos.x = pWork->moveOffset[i % 3];
        pWork->label[i]->dt.string.x = (short)ps2_ftoi((float)pos.x + pWork->strOffset);
    }
    old = pWork->count++;
    if (old < 9 || pWork->moveOffset[0] != pWork->moveOffset[2]) return 0;
    pWork->count = 0;
    return 1;
}

/* FadeOut tail of all three families: delete the sprites, erase the strings, delete the cursor, end the object */
static void mpaint_label_delete(struct stMobj *pMobj, struct stMobj **label, int nsprite, int nlabel,
                                struct stMobj **cursor)
{
    int i;

    for (i = 0; i < nsprite; i++) UnitSprite_Delete(label[i]);
    for (; i < nlabel; i++)
        if (label[i]) mpaint_erase(&label[i]);
    UnitSprite_Delete(*cursor);
    mpaint_obj_end(pMobj);
}

/* Play body of all three families: when the row changed, hand the old cursor to a DeleteCursor carrier object and
 * blink a new one at row id (a failed GetNewObject skips everything incl. prev_id: retried next frame) */
static void mpaint_cursor_play(struct stMobj *pMobj, struct stMobj **cursor, int *id, int *prev_id, int layout,
                               char *name)
{
    struct struct_214acd28 pos = {0, 0x38};   /* main sdata gp-0x6fa8 (Emb) / -0x6fa0 */

    if (*id == *prev_id) return;
    pos.y = (int)((unsigned int)pos.y * (unsigned int)*id);
    if (*cursor) {
        struct stMobj *tmp = DF_MOBJ_GetNewObject(pMobj);
        if (!tmp) return;
        UnitSprite_SetFadeOut(*cursor);
        ((struct _mobj_menu_garage_paint_delete_cursor_st *)&tmp->dt)->cursor = *cursor;
        tmp->play_func = MPAINT_PLAY(__MenuGaragePaint_DeleteCursor);
    }
    *cursor = UnitSpriteBlink_New(pMobj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(layout), name);
    UnitSprite_SetFadeIn(*cursor);
    UnitSprite_SetMove(*cursor, &pos, 0);
    (*cursor)->draw_prio = 3;
    *prev_id = *id;
}

/* FadeIn tail: once the labels settled show the cursor at row id and switch to the Play function */
static void mpaint_fade_in_done(struct stMobj *pMobj, struct stMobj *cursor, int id, struct struct_d46b729f *move,
                                void (*play)(void))
{
    struct struct_214acd28 pos;

    UnitSprite_SetFadeIn(cursor);
    pos.x = 0;
    pos.y = (int)((unsigned int)id * 56u);
    UnitSprite_SetMove(cursor, &pos, 0);
    DF_MOBJ_SetDrawEnable(cursor, 1);
    pMobj->play_func = play;
    MoveMng_Init(move);
    pMobj->stat = 0;
}

static void __MenuGarage_PaintEmbFadeOut(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct _mobj_menu_garage_paint2_st *w = MPAINT_LIST2(pMobj);

    if (__MenuGarage_Paint2FadeOut(w)) return;
    mpaint_label_delete(pMobj, w->label, 4, 6, &w->cursor);
}

static void __MenuGarage_PaintEmbPlay(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct _mobj_menu_garage_paint_emb_st *w = (struct _mobj_menu_garage_paint_emb_st *)&pMobj->dt;

    mpaint_cursor_play(pMobj, &w->emb.cursor, &w->id, &w->prev_id, 0x45a, sGaragePaint2Layout[6]);
}

static void __MenuGarage_PaintEmbFadeIn(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct _mobj_menu_garage_paint_emb_st *w = (struct _mobj_menu_garage_paint_emb_st *)&pMobj->dt;

    pMobj->stat = 1;
    if (!__MenuGaragePaint2LabelInit(&w->emb)) return;
    mpaint_fade_in_done(pMobj, w->emb.cursor, w->id, &w->emb.move, MPAINT_PLAY(__MenuGarage_PaintEmbPlay));
}

/* the Emblem sub-list (2 rows) label object at row id (strings 0xdcb5e..); pMobj = the owner system */
struct stMobj *__MenuGarage_PaintEmbLabelInit(struct stMobj *pMobj, int id)
{
    PS2_FRAME(0x50);
    struct stMobj *obj = DF_MOBJ_GetNewObject(pMobj);
    struct _mobj_menu_garage_paint_emb_st *w;
    struct struct_214acd28 pos = {0, 0x38};   /* main sdata gp-0x6fb0 */

    if (!obj) return 0;
    w = (struct _mobj_menu_garage_paint_emb_st *)&obj->dt;
    obj->type = 0;
    obj->stat = 1;
    obj->count = 0;
    obj->play_func = MPAINT_PLAY(__MenuGarage_PaintEmbFadeIn);
    obj->draw_func = 0;
    obj->pkt_func = 0;
    w->prev_id = id;
    w->id = id;
    __MenuGaragePaint2LabelMake(obj, &w->emb, 0xdcb5e);
    MoveMng_Init(&w->emb.move);
    w->emb.strOffset = (float)w->emb.label[4]->dt.string.x;   /* the first string's x */
    w->emb.moveOffset[0] = -0x140;
    w->emb.moveOffset[1] = -0x140;
    pos.y = (int)((unsigned int)pos.y * (unsigned int)id);
    UnitSprite_SetMove(w->emb.cursor, &pos, 0);
    return obj;
}

static void __MenuGarage_PaintColorFadeOut(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct _mobj_menu_garage_paint3_st *w = MPAINT_LIST3(pMobj);

    if (__MenuGarage_Paint3FadeOut(w)) return;
    mpaint_label_delete(pMobj, w->label, 6, 9, &w->cursor);
}

static void __MenuGarage_PaintColorFadeIn(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct _mobj_menu_garage_paint_color_st *w = (struct _mobj_menu_garage_paint_color_st *)&pMobj->dt;

    pMobj->stat = 1;
    if (!__MenuGaragePaint3LabelInit(&w->color)) return;
    mpaint_fade_in_done(pMobj, w->color.cursor, w->id, &w->color.move, MPAINT_PLAY(__MenuGarage_PaintColorPlay));
}

static void __MenuGarage_PaintColorPlay(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct _mobj_menu_garage_paint_color_st *w = (struct _mobj_menu_garage_paint_color_st *)&pMobj->dt;

    mpaint_cursor_play(pMobj, &w->color.cursor, &w->id, &w->prev_id, 0x45b, sGaragePaint3Layout[9]);
}

/* the Colour sub-list (3 rows) label object at row id (strings 0xdcb54..) */
struct stMobj *__MenuGarage_PaintColorLabelInit(struct stMobj *pMobj, int id)
{
    PS2_FRAME(0x50);
    struct stMobj *obj = DF_MOBJ_GetNewObject(pMobj);
    struct _mobj_menu_garage_paint_color_st *w;
    struct struct_214acd28 pos = {0, 0x38};   /* main sdata gp-0x6fb8 */

    if (!obj) return 0;
    w = (struct _mobj_menu_garage_paint_color_st *)&obj->dt;
    obj->type = 0;
    obj->stat = 1;
    obj->count = 0;
    obj->play_func = MPAINT_PLAY(__MenuGarage_PaintColorFadeIn);
    obj->draw_func = 0;
    obj->pkt_func = 0;
    w->prev_id = id;
    w->id = id;
    __MenuGaragePaint3LabelMake(obj, &w->color, 0xdcb54);
    MoveMng_Init(&w->color.move);
    w->color.strOffset = (float)w->color.label[6]->dt.string.x;
    w->color.moveOffset[0] = -0x140;
    w->color.moveOffset[1] = -0x140;
    w->color.moveOffset[2] = -0x140;
    pos.y = (int)((unsigned int)pos.y * (unsigned int)id);
    UnitSprite_SetMove(w->color.cursor, &pos, 0);
    return obj;
}

static void __MenuGarage_PaintFadeOut(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct _mobj_menu_garage_paint3_st *w = MPAINT_LIST3(pMobj);

    if (__MenuGarage_Paint3FadeOut(w)) return;
    mpaint_label_delete(pMobj, w->label, 6, 9, &w->cursor);
}

static void __MenuGarage_PaintPlay(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct _mobj_menu_garage_paint_st *w = (struct _mobj_menu_garage_paint_st *)&pMobj->dt;

    mpaint_cursor_play(pMobj, &w->cate.cursor, &w->id, &w->prev_id, 0x45b, sGaragePaint3Layout[9]);
}

static void __MenuGarage_PaintFadeIn(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct _mobj_menu_garage_paint_st *w = (struct _mobj_menu_garage_paint_st *)&pMobj->dt;

    pMobj->stat = 1;
    if (!__MenuGaragePaint3LabelInit(&w->cate)) return;
    mpaint_fade_in_done(pMobj, w->cate.cursor, w->id, &w->cate.move, MPAINT_PLAY(__MenuGarage_PaintPlay));
}

/* the top Paint list (3 rows) label object at row id (strings 0xdcb4a..) */
struct stMobj *__MenuGarage_PaintLabelInit(struct stMobj *pMobj, int id)
{
    PS2_FRAME(0x50);
    struct stMobj *obj = DF_MOBJ_GetNewObject(pMobj);
    struct _mobj_menu_garage_paint_st *w;
    struct struct_214acd28 pos = {0, 0x38};   /* main sdata gp-0x6fc0 */

    if (!obj) return 0;
    w = (struct _mobj_menu_garage_paint_st *)&obj->dt;
    obj->type = 0;
    obj->stat = 1;
    obj->count = 0;
    obj->play_func = MPAINT_PLAY(__MenuGarage_PaintFadeIn);
    obj->draw_func = 0;
    obj->pkt_func = 0;
    w->id = id;
    w->prev_id = id;
    __MenuGaragePaint3LabelMake(obj, &w->cate, 0xdcb4a);
    MoveMng_Init(&w->cate.move);
    w->cate.strOffset = (float)w->cate.label[6]->dt.string.x;
    w->cate.moveOffset[0] = -0x140;
    w->cate.moveOffset[1] = -0x140;
    w->cate.moveOffset[2] = -0x140;
    pos.y = (int)((unsigned int)pos.y * (unsigned int)id);
    UnitSprite_SetMove(w->cate.cursor, &pos, 0);
    return obj;
}
