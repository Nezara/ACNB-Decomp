/* src/menu/msemb.cpp - MSEMB, the "select emblem" screen of the garage Paint menu (Paint -> Emblem -> row 0;
 * OLMENU overlay). 44 preset emblems in 11 rows x 4 columns, two rows visible at a time: one list object
 * (dt = struct_89753a80) owns 44 cell objects (dt = struct_e8817f02, a window "ita_<col>", a blinking select frame,
 * a dark pad sprite and either the emblem sprite or, for a locked emblem, a "?" string). The list loads the binder
 * file 0x1015 (the emblem TM2 pack) into a DF_MMM_Alloc buffer, registers it as a BndTexture and, on decide +
 * dialog yes, copies the chosen preset into the player's emblem TM2 (DF_EMB_MakeTm2). Everything runs off
 * DF_MOBJ_Play through the play_func chain FadeIn -> PlaySEmbSystem -> Load -> FadeOut (+ the dead Dispatch);
 * rows scroll by moving all 44 cells (MoverI). The only caller is mpaint's __PlayPaintSubSystemFadeOut.
 *
 * The dt structs are not members of union_f74ccaac: &obj->dt is cast to struct_89753a80 (list) / struct_e8817f02
 * (cell). The __EMBDATA_New sprintf buffer lives at its original PS2 stack slot (newlib sprintf, format nodwarf).
 * Original quirks kept: csysp (list dt+0xcc) is never written by this file but read in FadeOut step 1 and
 * dereferenced by __PlaySEmbSystemDispatch, which reads csysp->stat before its NULL test (dead code);
 * y_select (cell dt+0x42) is never written or read; the idx clamps (< 0, >= 44) and several NULL tests can never
 * fire; `i < 0x2f` is always true in the 44-cell loop; the fade-out block is duplicated in Load and
 * __PlaySEmbSystem. */
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
/* main ELF sdata gp-0x6ed8 / OLMENU 0x1ee00e0 / 0x1ee00f0 / 0x1ee0100 (nodwarf.h) */
char msemb_fmt_ita[8] = "ita_%d";
char msemb_select_0[16] = "select_0";
char msemb_yaji_ue_0[16] = "yaji_ue_0";
char msemb_yaji_shita_0[16] = "yaji_shita_0";
#endif

#define MSEMB_PLAY(f) ((void (*)(void))GFN(f))

static inline struct struct_89753a80 *msemb_dt(struct stMobj *o) { return (struct struct_89753a80 *)&o->dt; }
static inline struct struct_e8817f02 *msemb_cell(struct stMobj *o) { return (struct struct_e8817f02 *)&o->dt; }

/* the DWARF bit-field "delete" pair: two read-modify-write byte stores of obj+0 */
static inline void msemb_erase(struct stMobj *o)
{
    o->f_draw = 0;
    o->f_erase = 1;
}

static inline struct _LO_HEADER *msemb_layout(int id)
{
    return (struct _LO_HEADER *)LAYOUT_GetLayoutData(id);
}

/* ------------------------------------------------------------------ the emblem cells */

/* show / hide one cell: the emblem sprite and the "?" string are exclusive (bSecret) */
static void __EMBDATA_Visible(struct stMobj *ioObjp, int bVisible)
{
    PS2_FRAME(0x30);
    if (!ioObjp)
        return;
    struct struct_e8817f02 *p = msemb_cell(ioObjp);
    p->bVisible = bVisible;
    if (!bVisible) {
        DF_MOBJ_SetDrawEnable(p->pWnd, bVisible);
        DF_MOBJ_SetDrawEnable(p->pSelect, bVisible);
        DF_MOBJ_SetDrawEnable(p->pPad, bVisible);
        DF_MOBJ_SetDrawEnable(p->pEmblem, bVisible);
        DF_MOBJ_SetDrawEnable(p->pSecret, bVisible);
    } else {
        DF_MOBJ_SetDrawEnable(p->pWnd, bVisible);
        DF_MOBJ_SetDrawEnable(p->pPad, bVisible);
        DF_MOBJ_SetDrawEnable(p->pEmblem, !(p->bSecret != 0));
        DF_MOBJ_SetDrawEnable(p->pSecret, p->bSecret);
    }
}

/* scroll one cell by Num rows (one row = 152 pixels); fade it in / out when it enters / leaves the two visible rows */
static void __EMBDATA_Move(struct stMobj *ioObjp, int Num)
{
    PS2_FRAME(0x40);
    if (!ioObjp)
        return;
    struct struct_e8817f02 *p = msemb_cell(ioObjp);
    int before = (unsigned int)p->PosY < 2;
    p->PosY += Num;
    MoverI_Move(&p->Move, p->PosY * 152, 5);
    int after = (unsigned int)p->PosY < 2;
    if (!before && !after) {
        __EMBDATA_Visible(ioObjp, 0);
        return;
    }
    if (before == after)
        return;
    if (after) {
        __EMBDATA_Visible(ioObjp, 1);
        WindowEx_SetFadeIn(p->pWnd);
        DF_MOBJ_SetFadeIn(p->pPad);
        DF_MOBJ_SetFadeIn(p->pEmblem);
        DF_MOBJ_SetFadeIn(p->pSecret);
    } else {
        WindowEx_SetFadeOut(p->pWnd);
        DF_MOBJ_SetDrawEnable(p->pSelect, 0);
        DF_MOBJ_SetFadeOut(p->pPad);
        DF_MOBJ_SetFadeOut(p->pEmblem);
        DF_MOBJ_SetFadeOut(p->pSecret);
    }
}

/* the cell's play_func: follow the scroll mover (window, select frame, pad / emblem sprite, "?" string) */
static void __EMBDATA_Update(struct stMobj *ioObjp)
{
    PS2_FRAME(0x60);
    if (!ioObjp)
        return;
    struct struct_e8817f02 *p = msemb_cell(ioObjp);
    struct stMenuRect rec;
    struct struct_214acd28 iv;

    p->bMove = MoverI_Update(&p->Move);
    WindowEx_GetRect(p->pWnd, &rec);
    iv.x = rec.x;
    iv.y = p->y_wnd + p->Move.Cur;
    WindowEx_MovePos(p->pWnd, &iv, 1);
    iv.x = 144 * (p->Pos % 4);
    iv.y = p->Move.Cur;
    UnitSprite_SetPosition(p->pSelect, &iv);
    if (p->pPad)
        p->pPad->dt.sprite.rect.y = p->y_emblem + p->Move.Cur;
    if (p->pEmblem)
        p->pEmblem->dt.sprite.rect.y = p->y_emblem + p->Move.Cur;
    if (p->pSecret)
        DF_MOBJ_SetStringPos(p->pSecret, p->pSecret->dt.string.x, p->y_secret + p->Move.Cur);
}

/* delete one cell and all its children (pSelect is not zeroed) */
static void __EMBDATA_Delete(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    if (!ioObjp)
        return;
    struct struct_e8817f02 *p = msemb_cell(ioObjp);
    WindowEx_Delete(p->pWnd);
    UnitSprite_Delete(p->pSelect);
    if (p->pPad) {
        msemb_erase(p->pPad);
        p->pPad = 0;
    }
    if (p->pEmblem) {
        msemb_erase(p->pEmblem);
        p->pEmblem = 0;
    }
    if (p->pSecret) {
        msemb_erase(p->pSecret);
        p->pSecret = 0;
    }
    msemb_erase(ioObjp);
}

/* build cell Pos (0..43) of the list: window, select frame, dark pad and either the emblem sprite (TexId) or the
 * locked-emblem "?" string */
static struct stMobj *__EMBDATA_New(struct stMobj *inObjp, int Pos, int TexId)
{
    PS2_FRAME(0xe0);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj)
        return 0;
    struct struct_e8817f02 *p = msemb_cell(obj);

    obj->type = 0;
    obj->stat = 0;
    obj->play_func = MSEMB_PLAY(__EMBDATA_Update);
    p->Pos = Pos;
    p->PosY = Pos / 4;
    p->bSecret = 1;
    p->texId = -1;
    MoverI_Init(&p->Move);
    MoverI_SetValue(&p->Move, p->PosY * 152);

    /* the constants the original copies out of rodata before the first call */
    struct struct_76b810ec col_pad = {0, 0, 0, 0x40};
    struct stMenuRect rec_emb = {48, 84, 112, 112};
    struct struct_76b810ec col_txt = {0x6e, 0x82, 0x8c, 0x80};
    struct struct_214acd28 pos_txt = {62, 134};
    int uv[4] __attribute__((aligned(16))) = {0, 0, 128, 128};   /* lq source of DF_MOBJ_NXNewSprite */

    int col = Pos % 4;
    int ofsx = col * 144;
    PS2_STACK(char[32], buff, 0x70);   /* newlib sprintf destination and WindowEx_NewFromLayout part name */
    ps2_sprintf_d(buff, msemb_fmt_ita, col);
    p->pWnd = WindowEx_NewFromLayout(obj, (_WINDOWEX_TYPE)0, (char *)msemb_layout(0x424), buff);

    struct stMenuRect rec;
    struct struct_214acd28 iv;
    WindowEx_GetRect(p->pWnd, &rec);
    p->y_wnd = (short)rec.y;
    iv.x = rec.x;
    iv.y = rec.y;
    WindowEx_MovePos(p->pWnd, &iv, 1);

    struct struct_214acd28 pos;
    pos.x = ofsx;
    pos.y = p->PosY * 152;
    p->pSelect = UnitSpriteBlink_New(obj, msemb_layout(0x424), msemb_select_0);
    UnitSprite_SetMove(p->pSelect, &pos, 0);
    p->pSelect->draw_prio = 3;
    DF_MOBJ_SetDrawEnable(p->pSelect, 0);

    p->y_emblem = (short)rec_emb.y;
    p->y_secret = (short)pos_txt.y;
    rec_emb.x += ofsx;
    p->pPad = DF_MOBJ_NXNewSprite(obj, -1, &rec_emb, uv, &col_pad, 0, 0, 0, 0);
    if (TexId != -1) {
        p->texId = TexId;
        p->pEmblem = DF_MOBJ_NXNewSprite(obj, TexId, &rec_emb, uv, 0, 1, 0, 1, 0);
        p->bSecret = 0;
    } else {
        p->pSecret = DF_MOBJ_NewString(obj, 0, 0x192f8, 0, 1, pos_txt.x + ofsx, pos_txt.y, &col_txt, 0, 0);
    }
    __EMBDATA_Visible(obj, (unsigned int)p->PosY < 2);
    return obj;
}

/* ------------------------------------------------------------------ the list */

/* 1 while the list is sliding (input blocked): run the pending scroll, at most 2 rows per frame */
static int __ChkSEmbSystemMove(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    struct struct_89753a80 *p = msemb_dt(ioObjp);
    int mov0 = p->pEmb[0] ? msemb_cell(p->pEmb[0])->bMove : 0;
    if (mov0)
        return 1;
    if (p->move == 0) {
        struct stMobj *e = p->pEmb[p->idx];
        if (e)
            DF_MOBJ_SetDrawEnable(msemb_cell(e)->pSelect, 1);
        return 0;
    }
    int mov = p->move > 0 ? (p->move < 2 ? 1 : 2) : (p->move < -1 ? -2 : -1);
    p->move -= mov;
    for (int i = 0; i < 44; i++)
        __EMBDATA_Move(p->pEmb[i], mov);
    return 1;
}

/* move the cursor by Add rows (wrapping) and start a scroll when the new row is off screen */
static void __SelectSEmbSystemY(struct stMobj *ioObjp, int Add)
{
    PS2_FRAME(0x20);
    struct struct_89753a80 *p = msemb_dt(ioObjp);
    int old = p->idx;
    int nx = old % 4;
    int ny = old / 4 + Add;
    if (ny < 0)
        ny = 10;
    if (ny >= 11)
        ny = 0;
    p->idx = ny * 4 + nx;
    if (p->idx < 0)
        p->idx = 0;
    if (p->idx >= 44)
        p->idx = 44;
    if (old != p->idx && p->pEmb[old])
        DF_MOBJ_SetDrawEnable(msemb_cell(p->pEmb[old])->pSelect, 0);
    int row = p->idx / 4;
    int d = row - p->top;
    if (d >= 2) {
        p->top += d - 1;
        p->move = -(d - 1);
    }
    if (row < p->top) {
        int dd = p->top - row;
        p->top -= dd;
        p->move = dd;
    }
}

/* move the cursor by Add columns (wrapping inside the row); the select frame moves at once */
static void __SelectSEmbSystemX(struct stMobj *ioObjp, int Add)
{
    PS2_FRAME(0x20);
    struct struct_89753a80 *p = msemb_dt(ioObjp);
    int old = p->idx;
    int row = old / 4;
    int nx = old % 4 + Add;
    if (nx < 0)
        nx = 3;
    if (nx >= 4)
        nx = 0;
    p->idx = row * 4 + nx;
    if (p->idx < 0)
        p->idx = 0;
    if (p->idx >= 44)
        p->idx = 44;
    if (old != p->idx) {
        if (p->pEmb[old])
            DF_MOBJ_SetDrawEnable(msemb_cell(p->pEmb[old])->pSelect, 0);
        if (p->pEmb[p->idx])
            DF_MOBJ_SetDrawEnable(msemb_cell(p->pEmb[p->idx])->pSelect, 1);
    }
}

/* the fade-out block of the leave path (duplicated in the original: Load's yes arm and PlaySEmbSystem's cancel arm) */
static inline void msemb_fade_out_all(struct stMobj *ioObjp)
{
    struct struct_89753a80 *p = msemb_dt(ioObjp);
    DF_MOBJ2_SetBlinkSpriteFadeOut(p->pUp);
    DF_MOBJ2_SetBlinkSpriteFadeOut(p->pDown);
    for (int i = 0; i < 44; i++) {
        struct stMobj *e = p->pEmb[i];
        if (e) {
            struct struct_e8817f02 *c = msemb_cell(e);
            WindowEx_SetFadeOut(c->pWnd);
            DF_MOBJ_SetDrawEnable(c->pSelect, 0);
            DF_MOBJ_SetFadeOut(c->pPad);
            DF_MOBJ_SetFadeOut(c->pEmblem);
            DF_MOBJ_SetFadeOut(c->pSecret);
        }
    }
    DF_M1HELP_FadeOut();
    DF_MVIEW_MotionStart(MViewp, 0x21);
    p->step = 0;
    ioObjp->count = 0;
    ioObjp->play_func = MSEMB_PLAY(__PlaySEmbSystemFadeOut);
    ioObjp->stat = 2;
}

/* dead code: restarts the list after a child system closed (csysp is never set by this file, see the header) */
static void __PlaySEmbSystemDispatch(struct stMobj *ioObjp)
{
    struct struct_89753a80 *p = msemb_dt(ioObjp);
    struct stMobj *c = p->csysp;
    if (c->stat != 5)   /* original: read before the NULL test below */
        return;
    if (c) {
        c->f_draw = 0;
        p->csysp->f_erase = 1;
        p->csysp = 0;
    }
    ioObjp->stat = 1;
    ioObjp->play_func = MSEMB_PLAY(__PlaySEmbSystemFadeIn);
    ioObjp->count = 0;
    p->step = 0;
}

/* leave the screen: 5 frames, then delete every cell, free the texture pack and fade the AC back in */
static void __PlaySEmbSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    struct struct_89753a80 *p = msemb_dt(ioObjp);
    switch (p->step) {
    case 0:
        ioObjp->count++;
        if (ioObjp->count < 5)
            return;
        if (p->pUp) {
            msemb_erase(p->pUp);
            p->pUp = 0;
        }
        if (p->pDown) {
            msemb_erase(p->pDown);
            p->pDown = 0;
        }
        for (int i = 0; i < 44; i++)
            __EMBDATA_Delete(p->pEmb[i]);
        p->step++;
        break;
    case 1:
        if (p->csysp == 0) {
            MenuTex_UnRegisterBndTexture(p->pData);
            if (p->pData)
                DF_MMM_Free(0, p->pData);
            p->pData = 0;   /* original: stored in the delay slot, before PopSceneLabel runs */
            DF_MBACK_PopSceneLabel();
            if (p->pAC)
                DF_M_AC_SetFadeIn(p->pAC, 0xf, 0.0f);
            else
                DF_M_AC_SetFadeIn(M_ACp[p->player][0], 0xf, 0.0f);
            ioObjp->stat = 5;
            ioObjp->play_func = 0;
        } else {
            ioObjp->stat = 3;
            ioObjp->play_func = MSEMB_PLAY(__PlaySEmbSystemDispatch);
        }
        break;
    }
}

/* decide: locked emblem -> back to the list, otherwise the confirm dialog and, on yes, DF_EMB_MakeTm2 + leave */
static void __PlaySEmbSystemLoad(struct stMobj *ioObjp)
{
    PS2_FRAME(0x60);
    struct struct_89753a80 *p = msemb_dt(ioObjp);
    switch (p->step) {
    case 0: {
        struct stMobj *e = p->pEmb[p->idx];
        int secret = e ? msemb_cell(e)->bSecret : 0;
        if (secret) {
            Voice_Call(0x3f1, 0x7f);
            ioObjp->play_func = MSEMB_PLAY(__PlaySEmbSystem);
            return;
        }
        Voice_Call(0x3ea, 0x7f);
        p->step++;
        break;
    }
    case 1: {
        unsigned char *info = DF_MSTR_GetInfo(MenuMng.language, 1, 0x30d4a);
        DF_MDIALOG_Open(2, info, 0, 0, 0, 0, 1, 0);
        p->step++;
        break;
    }
    case 2: {
        int r = DF_MDIALOG_Sync();
        if (!(r & 4))
            return;
        if (r == 4 || r == 0x24) {   /* no / cancel */
            ioObjp->play_func = MSEMB_PLAY(__PlaySEmbSystem);
            return;
        }
        if (r != 0x14)   /* yes */
            return;
        struct stMobj *e = p->pEmb[p->idx];
        int off = Binder_GetOffset(&p->Bind, e ? msemb_cell(e)->texId : 0);
        DF_EMB_MakeTm2(p->embp, (char *)p->pData + off + 0x20, (char *)p->pData + off + 0x4020);
        msemb_fade_out_all(ioObjp);
        break;
    }
    }
}

/* the main loop: key guide, scroll, circle (leave), cross (decide) and the four cursor keys */
static void __PlaySEmbSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x60);
    struct struct_89753a80 *p = msemb_dt(ioObjp);
    if (MenuKeyGuide_IsShown())
        return;
    int user = MPadUser;
    if (MPad[user].start == 1) {
        MenuKeyGuide_Show(0x23);
        return;
    }
    if (__ChkSEmbSystemMove(ioObjp))
        return;
    user = MPadUser;
    if (MPad[user].Rrt == 1) {
        Voice_Call(0x3eb, 0x7f);
        msemb_fade_out_all(ioObjp);
        return;
    }
    if (MPad[user].Rdw == 1) {
        p->step = 0;
        ioObjp->play_func = MSEMB_PLAY(__PlaySEmbSystemLoad);
        return;
    }
    /* X and Y repeat are tested independently in the same frame */
    struct struct_81b11733 rep = MPad[user].repeat;
    if (rep.rt) {
        Voice_Call(0x3e9, 0x7f);
        __SelectSEmbSystemX(ioObjp, 1);
    } else if (rep.lt) {
        Voice_Call(0x3e9, 0x7f);
        __SelectSEmbSystemX(ioObjp, -1);
    }
    rep = MPad[MPadUser].repeat;
    if (rep.up) {
        Voice_Call(0x3e9, 0x7f);
        __SelectSEmbSystemY(ioObjp, -1);
    } else if (rep.dw) {
        Voice_Call(0x3e9, 0x7f);
        __SelectSEmbSystemY(ioObjp, 1);
    }
}

/* enter the screen: 5 frames, read the emblem pack (file 0x1015), build the 44 cells and the two arrows */
static void __PlaySEmbSystemFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x70);
    struct struct_89753a80 *p = msemb_dt(ioObjp);
    switch (p->step) {
    case 0:
        ioObjp->count++;
        if (ioObjp->count < 5)
            return;
        DF_MVIEW_MotionStart(MViewp, 0x33);
        DF_M1HELP_SetStringId(0x192dc);
        DF_M1HELP_FadeIn();
        p->step++;
        break;
    case 1:
        if (File_IsAsyncBusy())
            return;
        p->pData = DF_MMM_Alloc(0, File_GetSize(0x1015));
        File_Read(0x1015, -1, (char *)p->pData, 0);
        p->step++;
        break;
    case 2:
        if (File_IsAsyncBusy())
            return;
        MenuTex_RegisterBndTexture(p->pData);
        Binder_Init(&p->Bind, p->pData, 0);
        for (int i = 0; i < 44; i++) {
            int texid;
            if (i + 1 == 12)   /* emblem 12 is the top-appellation reward */
                texid = MAppellationMng_IsTopAppellation() ? i + 0x2af9 : -1;
            else
                texid = i < 0x2f ? i + 0x2af9 : -1;   /* always true for 44 cells */
            p->pEmb[i] = __EMBDATA_New(ioObjp, i, texid);
            if (p->pEmb[i]) {
                struct struct_e8817f02 *c = msemb_cell(p->pEmb[i]);
                WindowEx_SetFadeIn(c->pWnd);
                DF_MOBJ_SetFadeIn(c->pPad);
                DF_MOBJ_SetFadeIn(c->pEmblem);
                DF_MOBJ_SetFadeIn(c->pSecret);
            }
        }
        if (p->pEmb[0])
            DF_MOBJ_SetDrawEnable(msemb_cell(p->pEmb[0])->pSelect, 1);
        p->pUp = DF_MOBJ2_NewBlinkSpriteFromLayoutName(ioObjp, (char *)msemb_layout(0x424), msemb_yaji_ue_0, 0, 0);
        DF_MOBJ2_SetBlinkSpriteFadeIn(p->pUp);
        p->pDown =
            DF_MOBJ2_NewBlinkSpriteFromLayoutName(ioObjp, (char *)msemb_layout(0x424), msemb_yaji_shita_0, 0, 0);
        DF_MOBJ2_SetBlinkSpriteFadeIn(p->pDown);
        p->step++;
        break;
    case 3:
        if (DF_MVIEW_MotionSync(MViewp)) {
            ioObjp->stat = 0;
            ioObjp->play_func = MSEMB_PLAY(__PlaySEmbSystem);
        }
        break;
    }
}

/* create the list object: cursor at 0, the edited emblem TM2 is the AC's own or EmblemTM2[player] */
static struct stMobj *__NewSEmbSystem(struct stMobj *inObjp, int inPlayer, struct stMenuAC *pAC)
{
    PS2_FRAME(0x50);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj)
        return 0;
    struct struct_89753a80 *p = msemb_dt(obj);

    obj->type = 0;
    obj->stat = 1;
    obj->play_func = MSEMB_PLAY(__PlaySEmbSystemFadeIn);
    obj->count = 0;
    p->player = inPlayer;
    p->idx = 0;
    p->top = 0;
    p->pAC = pAC;
    p->pData = 0;   /* original: stored in the delay slot, before PushSceneLabel runs */
    DF_MBACK_PushSceneLabel((enum_5124d09c)0x28);
    if (p->pAC) {
        DF_M_AC_SetFadeOut(p->pAC, 5, 0.0f);
        p->embp = (char *)pAC->emblem;
    } else {
        DF_M_AC_SetFadeOut(M_ACp[p->player][0], 5, 0.0f);
        p->embp = (char *)EmblemTM2 + p->player * 17440;
    }
    return obj;
}

struct stMobj *DF_MSEMB_NewSystem(struct stMobj *inObjp, int inPlayer, struct stMenuAC *pAC)
{
    return __NewSEmbSystem(inObjp, inPlayer, pAC);
}
