/* src/menu/moption.cpp - MenuSysOption, the system "OPTION" screens (OLMENU); the whole file is native.
 * The "NEW GAME" yes / no dialog the title menu opens (DF_MMANAGE_NewSystem case 9 -> MenuSysOption_NewGameDlg_New):
 * a WindowEx with the question string, two answer strings, a blinking cursor sprite and the shared M1HELP help line.
 * The mobj's dt is a struct_c94b52e1; stMobj.stat is the state: 0 stable (input), 1 one-frame idle, 2 fading out,
 * 3 option screen spawned (NewGameDlg_PlayOnDispatch waits for it to die), 5 dead. YES (nExit 1) spawns
 * MenuSysOption_New after the 18-frame fade-out.
 * The option screen (MenuSysOption_New, garage3 / garage2 via NEW GAME -> YES; also __MMANAGE_PlaySystem /
 * __PlaySystem): 8 items (BGM / SE volume, surround, text message, key type, vibration, key assignment, leave in
 * nMode 2), changed with left / right, X / circle to leave; __MenuSysOption_Play is the state machine (1 idle,
 * 0 stable input, 2 fading out, 3 child = key-config screen or NEW GAME dialog running -> PlayOnDispatch, 5 dead).
 * Its dt is a struct_599addfb (8 UnitSprite panels, 17 strings, 4 arrows, 4 WindowEx cursors, light cursor, two
 * volume bars = private mobjs with a struct_f2de820d dt that own a meter sprite, the Dolby sprite). */

#include "acnb/nodwarf.h"
#include "acnb/pf.h"
#include "acnb/ps2fp.h"

#ifdef ACNB_SOURCEPORT
char mopt_name_parts10_7[10] = "parts10_7";
char mopt_name_parts10_6[10] = "parts10_6";
char mopt_name_parts10_4[10] = "parts10_4";
char mopt_name_parts10_5[10] = "parts10_5";
char mopt_name_parts10_3[10] = "parts10_3";
char mopt_name_parts10_2[10] = "parts10_2";
char mopt_name_parts10_0[10] = "parts10_0";
char mopt_name_parts10_1[10] = "parts10_1";
char mopt_name_parts10a_0[11] = "parts10a_0";
char mopt_fmt_sound[9] = "sound_%d";
char mopt_fmt_stereo2[11] = "stereo2_%d";
char mopt_fmt_2sentaku[12] = "2sentaku_%d";
char mopt_fmt_parts20d[12] = "parts20d_%d";
char mopt_name_dolby2_0[9] = "dolby2_0";
char mopt_fmt_3d[4] = "%3d";
char mopt_fmt_key[7] = "key_%d";
char mopt_name_left_0[7] = "left_0";
char mopt_name_right_0[8] = "right_0";
char mopt_name_left_1[7] = "left_1";
char mopt_name_right_1[8] = "right_1";
#endif

static inline struct struct_c94b52e1 *ngdlg_dt(struct stMobj *pMObj)
{
    return (struct struct_c94b52e1 *)&pMObj->dt;
}

/* the inline "delete" of a child mobj: f_draw = 0, then f_erase = 1 (two separate flag-byte writes) */
static inline void ngdlg_erase(struct stMobj *p)
{
    p->f_draw = 0;
    p->f_erase = 1;
}

/* the nExit byte (mobj +0x1c) of a child: an option screen (struct_599addfb) / a NEW GAME dialog (struct_c94b52e1) */
static inline unsigned char mopt_child_exit(struct stMobj *c)
{
    return (unsigned char)((struct struct_599addfb *)&c->dt)->nExit;
}

static void __MenuSysOption_NewGameDlg_Exit(struct stMobj *pMObj)
{
    struct struct_c94b52e1 *dt = ngdlg_dt(pMObj);
    if (dt->pLightCursor) { ngdlg_erase(dt->pLightCursor); dt->pLightCursor = 0; }
    if (dt->pTypeBString) { ngdlg_erase(dt->pTypeBString); dt->pTypeBString = 0; }
    if (dt->pTypeAString) { ngdlg_erase(dt->pTypeAString); dt->pTypeAString = 0; }
    if (dt->pSelectString) { ngdlg_erase(dt->pSelectString); dt->pSelectString = 0; }
    WindowEx_Delete(dt->pBaseDlg);   /* tail jump */
}

static void __MenuSysOption_NewGameDlg_SetFadeOut(struct stMobj *pMObj)
{
    PS2_FRAME(0x20);
    struct struct_c94b52e1 *dt = ngdlg_dt(pMObj);
    pMObj->stat = 2;
    pMObj->count = 0;
    if ((unsigned char)dt->nExit != 2) {
        WindowEx_SetFadeOut(dt->pBaseDlg);
        DF_MOBJ_SetFadeOut(dt->pSelectString);
        DF_MOBJ_SetFadeOut(dt->pTypeAString);
        DF_MOBJ_SetFadeOut(dt->pTypeBString);
        DF_MOBJ2_SetBlinkSpriteFadeOut(dt->pLightCursor);
    }
    DF_M1HELP_FadeOut();
    unsigned char e = (unsigned char)dt->nExit;
    if (e == 0 || e == 2) {
        DF_MBACK_PopSceneLabel();
        DF_MVIEW_MotionStart(MViewp, 1);
        DF_MBACK_SetFadeOutNX();   /* a0 = 0 (DWARF: no parameter) */
    }
}

static void __MenuSysOption_NewGameDlg_PlayOnStable(struct stMobj *pMObj)
{
    PS2_FRAME(0x40);
    struct struct_c94b52e1 *dt = ngdlg_dt(pMObj);
    struct struct_2d84b07b *pad = &MPad[MPadUser];
    if (pad->Rrt) {
        dt->nExit = 0;
        __MenuSysOption_NewGameDlg_SetFadeOut(pMObj);
        Voice_Call(0x3eb, 0x7f);
    } else if (pad->Rdw) {
        dt->nExit = 1;
        MenuKeyCnfg_SetKeyType(dt->nKeyType, dt->nPlayer);
        __MenuSysOption_NewGameDlg_SetFadeOut(pMObj);
        Voice_Call(0x3ea, 0x7f);
    } else if (pad->repeat.lt || pad->repeat.rt) {
        int x, y;
        if ((unsigned char)dt->nKeyType == 0) {
            dt->nKeyType = 1;
            x = 0x12e;
            y = 0xbe;
            DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x192b5));
        } else {   /* 1, and (never) anything else */
            dt->nKeyType = 0;
            x = 0x50;
            y = 0xbe;
            DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x192b4));
        }
        DF_MOBJ2_SetBlinkSpritePos(dt->pLightCursor, x, y);
        Voice_Call(0x3e9, 0x7f);
    }
}

static void __MenuSysOption_NewGameDlg_PlayOnFadeOut(struct stMobj *pMObj)
{
    PS2_FRAME(0x30);   /* calls the interpreted MenuSysOption_New */
    struct struct_c94b52e1 *dt = ngdlg_dt(pMObj);
    unsigned char old = pMObj->count;
    pMObj->count = old + 1;
    if (old < 0x12) return;
    unsigned char e = (unsigned char)dt->nExit;
    if (e == 1) {
        dt->pChild = MenuSysOption_New(pMObj, dt->nPlayer, dt->nMode, 10);
        pMObj->stat = 3;   /* stored in the jal delay slot, before Exit runs */
        __MenuSysOption_NewGameDlg_Exit(pMObj);
    } else {
        if (e == 0) __MenuSysOption_NewGameDlg_Exit(pMObj);
        pMObj->stat = 5;
        pMObj->play_func = 0;
        pMObj->draw_func = 0;
        pMObj->pkt_func = 0;
    }
}

static void __MenuSysOption_NewGameDlg_Play(struct stMobj *pMObj)
{
    PS2_FRAME(0x10);
    switch (pMObj->stat) {
    case 3: __MenuSysOption_NewGameDlg_PlayOnDispatch(pMObj); break;
    case 2: __MenuSysOption_NewGameDlg_PlayOnFadeOut(pMObj); break;
    case 0: __MenuSysOption_NewGameDlg_PlayOnStable(pMObj); break;
    case 1: pMObj->stat = 0; pMObj->count = 0; break;
    default: break;
    }
}

static void __MenuSysOption_NewGameDlg_Enter(struct stMobj *pMObj)
{
    PS2_FRAME(0x80);
    struct struct_c94b52e1 *dt = ngdlg_dt(pMObj);
    /* main sdata gp-0x7038.. (copied word by word with lwc1 / swc1) and the overlay lq constants 0x1edf940 / 950 */
    struct struct_76b810ec stColor = {0x80, 0x80, 0x80, 0x40};
    struct struct_76b810ec stSelectStringColor = {0x80, 0x80, 0x80, 0x80};
    struct struct_76b810ec stTypeAStringColor = {0x80, 0x80, 0x80, 0x80};
    struct struct_76b810ec stTypeBStringColor = {0x80, 0x80, 0x80, 0x80};
    struct struct_76b810ec stColor2 = {0x50, 0xc8, 0xa0, 0x28};
    struct stMenuRect stRect = {0x50, 0x96, 0x1e0, 0x64};
    int vecUv[4] = {0xd0, 0x22, 0x100, 0x52};
    struct stMenuRect stRect2;

    dt->pBaseDlg = WindowEx_New(pMObj, 2, &stRect, &stColor);
    dt->pSelectString = DF_MOBJ_NewStringBase(dt->pBaseDlg, 6, 0x64, 0xa0, &stSelectStringColor, 0, 0);
    DF_MOBJ_SetStringID(dt->pSelectString, 0, 0x192b6, 0);
    dt->pTypeAString = DF_MOBJ_NewStringBase(dt->pBaseDlg, 4, 0xa0, 0xdc, &stTypeAStringColor, 0, 0);
    DF_MOBJ_SetStringID(dt->pTypeAString, 0, 0x192b4, 0);
    dt->pTypeBString = DF_MOBJ_NewStringBase(dt->pTypeAString, 4, 0x17e, 0xdc, &stTypeBStringColor, 0, 0);   /* (sic) parent = answer A */
    DF_MOBJ_SetStringID(dt->pTypeBString, 0, 0x192b5, 0);
    if (dt->nKeyType == 0) { stRect2.x = 0x50; stRect2.y = 0xbe; }
    else { stRect2.x = 0x12e; stRect2.y = 0xbe; }
    stRect2.w = 0x104;
    stRect2.h = 0x4e;
    dt->pLightCursor = DF_MOBJ2_NewBlinkSpriteNX(dt->pTypeBString, 0x3ec, &stRect2, vecUv, &stColor2, 0, 0);
    dt->pLightCursor->dt.sprite.reg_alpha = 0x48;
    DF_M1HELP_SetWindow();   /* a0 = 0, a1 = 0x280 (DWARF: no parameters) */
    DF_M1HELP_SetString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x192a8));
    DF_M1HELP_FadeIn();
    if (pMObj->stat == 3) DF_M1HELP_ChangeString(0);
    else if (dt->nKeyType == 0) DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x192b4));
    else DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x192b5));
    DF_MBACK_SetDraw(1);
    DF_MBACK_SetSideDraw(1);
    DF_MBACK_SetFadeInNX();   /* a0 = 0 (DWARF: no parameter) */
    DF_MVIEW_MotionStart(MViewp, 0x96);
}

/* stat 3: waits for the option screen child to die, then tears the dialog down */
static void __MenuSysOption_NewGameDlg_PlayOnDispatch(struct stMobj *pMObj)
{
    PS2_FRAME(0x20);
    struct struct_c94b52e1 *dt = ngdlg_dt(pMObj);
    struct stMobj *c = dt->pChild;
    if (!c) return;
    if (c->stat != 5) return;
    if ((unsigned char)mopt_child_exit(c) == 1) {   /* the option screen's nExit: CANCEL */
        if (c) {   /* (redundant null test in the asm) */
            ngdlg_erase(c);
            dt->pChild = 0;
        }
        dt->pChild = 0;
        pMObj->stat = 5;
        pMObj->play_func = 0;
        pMObj->draw_func = 0;
        pMObj->pkt_func = 0;
        return;
    }
    if (c) {
        ngdlg_erase(c);
        dt->pChild = 0;
    }
    dt->pChild = 0;
    /* (sic) Enter recreates the dialog's windows only for Exit to delete them; Enter sees stat 3 */
    __MenuSysOption_NewGameDlg_Enter(pMObj);
    DF_MBACK_PopSceneLabel();
    __MenuSysOption_NewGameDlg_Exit(pMObj);
    pMObj->stat = 5;
    pMObj->play_func = 0;
    pMObj->draw_func = 0;
    pMObj->pkt_func = 0;
}

struct stMobj *MenuSysOption_NewGameDlg_New(struct stMobj *pParentMobj, char nPlayer, int nMode, int nFlag)
{
    PS2_FRAME(0x60);
    struct stMobj *p = DF_MOBJ_GetNewObject(pParentMobj);   /* a0 passed through unchanged */
    if (!p) return 0;
    struct struct_c94b52e1 *dt = ngdlg_dt(p);
    ZeroMemory(dt, 0x30);
    p->type = 0;
    p->stat = 1;
    p->play_func = (void (*)(void))GFN(__MenuSysOption_NewGameDlg_Play);
    dt->nMode = nMode;
    dt->nFlag = nFlag;
    dt->nPlayer = nPlayer;
    dt->nKeyType = 0;
    dt->pChild = 0;
    __MenuSysOption_NewGameDlg_Enter(p);
    p->stat = 1;
    p->count = 0;
    WindowEx_SetFadeIn(dt->pBaseDlg);
    DF_MOBJ_SetFadeIn(dt->pSelectString);
    DF_MOBJ_SetFadeIn(dt->pTypeAString);
    DF_MOBJ_SetFadeIn(dt->pTypeBString);
    DF_MOBJ2_SetBlinkSpriteFadeIn(dt->pLightCursor);
    DF_MBACK_PushSceneLabel(0x4b);
    return p;
}

/* ---- the option screen (garage3 g3a) ---- */

static inline struct struct_599addfb *mopt_dt(struct stMobj *pMObj)
{
    return (struct struct_599addfb *)&pMObj->dt;
}

static inline struct struct_f2de820d *mopt_bar(struct stMobj *pBar)
{
    return (struct struct_f2de820d *)&pBar->dt;
}

/* LAYOUT_GetLayoutData(0x3fb) (panels) / (0x417) (strings, arrows, windows); re-fetched at every use like the asm */
static inline void *mopt_hdr(int id) { return LAYOUT_GetLayoutData(id); }

/* a volume bar's meter: a 400 px wide sprite scaled to inNum % (1..100); out of range -> no meter */
static void __MenuSysOption_UpdateMeter(struct stMobj *pMObj, int inNum)
{
    PS2_FRAME(0x60);
    struct struct_76b810ec col = {0x06, 0x8d, 0xcf, 0x80};   /* gp 0x27bc34 */
    if (!pMObj->f_draw) return;
    struct struct_f2de820d *pm = mopt_bar(pMObj);
    if (pm->pMeter) {
        ngdlg_erase(pm->pMeter);
        pm->pMeter = 0;
    }
    if (inNum > 0 && inNum < 0x65) {
        int len = ps2_ftoi(ps2_div((float)inNum, 100.0f) * 400.0f);
        if (len <= 0) len = 0;
        else if (len >= 0x190) len = 0x190;
        alignas(16) int uv[4] = {0x98, 0x22, 0xb8, 0x2a};              /* OLMENU 0x1edf920 */
        alignas(16) struct stMenuRect pos = {0x78, 0, 0x190, 8};       /* OLMENU 0x1edf930 */
        pos.w = len;
        pos.y = pm->nDrawY;
        pm->pMeter = DF_MOBJ_NXNewSprite(pMObj, 0x3ec, &pos, uv, &col, 1, 0, 1, 0);
    }
}

/* the help line of the selected item */
static void __MenuSysOption_SetM1Help(struct stMobj *pMObj)
{
    PS2_FRAME(0x10);
    unsigned int id;
    switch ((unsigned int)mopt_dt(pMObj)->nSelectIndex) {
    case 0: id = 0x192a8; break;
    case 1: id = 0x192a9; break;
    case 2: id = 0x192aa; break;
    case 3: id = 0x192ad; break;
    case 4: id = 0x192b3; break;
    case 5: id = 0x192ae; break;
    case 6: id = 0x192af; break;
    case 7: id = 0x192b1; break;
    default: return;
    }
    DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, id));
}

static void __MenuSysOption_SetFadeIn(struct stMobj *pMObj)
{
    PS2_FRAME(0x40);
    struct struct_599addfb *dt = mopt_dt(pMObj);
    pMObj->stat = 1;
    DF_MOBJ2_SetBlinkSpriteFadeIn(dt->pLightCursor);
    for (int i = 0; i < 8; i++)
        if (dt->apBase[i]) UnitSprite_SetFadeIn(dt->apBase[i]);
    for (int i = 0; i < 18; i++)
        if (dt->apString[i]) DF_MOBJ_SetFadeIn(dt->apString[i]);
    if (dt->nSelectIndex == 0) {
        DF_MOBJ2_SetBlinkSpriteFadeIn(dt->pBgmVolLArrow);
        DF_MOBJ2_SetBlinkSpriteFadeIn(dt->pBgmVolRArrow);
        dt->pSeVolLArrow->stat = 5;
        dt->pSeVolRArrow->stat = 5;
    } else if (dt->nSelectIndex == 1) {
        DF_MOBJ2_SetBlinkSpriteFadeIn(dt->pSeVolLArrow);
        DF_MOBJ2_SetBlinkSpriteFadeIn(dt->pSeVolRArrow);
        dt->pBgmVolLArrow->stat = 5;
        dt->pBgmVolRArrow->stat = 5;
    } else {
        dt->pSeVolLArrow->stat = 5;
        dt->pSeVolRArrow->stat = 5;
        dt->pBgmVolLArrow->stat = 5;
        dt->pBgmVolRArrow->stat = 5;
    }
    UnitSprite_SetFadeIn(dt->pDolbySurround);
    DF_MOBJ_SetFadeIn(mopt_bar(dt->pBgmVolBar)->pMeter);   /* bars not null-tested, meter may be NULL (callee guards) */
    DF_MOBJ_SetFadeIn(mopt_bar(dt->pSeVolBar)->pMeter);
    DF_M1HELP_FadeIn();
}

/* a volume bar: its meter (re-read from the bar for the second store), then the bar itself */
static inline void mopt_erase_bar(struct stMobj *bar)
{
    struct struct_f2de820d *b = mopt_bar(bar);   /* pMeter read before the bar is tested (as the asm) */
    if (b->pMeter) {
        b->pMeter->f_draw = 0;
        b->pMeter->f_erase = 1;
        b->pMeter = 0;
    }
    if (bar) ngdlg_erase(bar);
}

static void __MenuSysOption_Exit(struct stMobj *pMObj)
{
    PS2_FRAME(0x30);
    struct struct_599addfb *dt = mopt_dt(pMObj);
    mopt_erase_bar(dt->pSeVolBar);
    mopt_erase_bar(dt->pBgmVolBar);
    UnitSprite_Delete(dt->pDolbySurround);
    if (dt->pLightCursor) { ngdlg_erase(dt->pLightCursor); dt->pLightCursor = 0; }
    WindowEx_Delete(dt->pVibrationCursor);
    WindowEx_Delete(dt->pKeyTypeCursor);
    WindowEx_Delete(dt->pTextMessageCursor);
    WindowEx_Delete(dt->pSurroundCursor);
    if (dt->pSeVolRArrow) { ngdlg_erase(dt->pSeVolRArrow); dt->pSeVolRArrow = 0; }
    if (dt->pSeVolLArrow) { ngdlg_erase(dt->pSeVolLArrow); dt->pSeVolLArrow = 0; }
    if (dt->pBgmVolRArrow) { ngdlg_erase(dt->pBgmVolRArrow); dt->pBgmVolRArrow = 0; }
    if (dt->pBgmVolLArrow) { ngdlg_erase(dt->pBgmVolLArrow); dt->pBgmVolLArrow = 0; }
    for (int i = 0; i < 18; i++)
        if (dt->apString[i]) { ngdlg_erase(dt->apString[i]); dt->apString[i] = 0; }
    for (int i = 0; i < 8; i++)
        if (dt->apBase[i]) UnitSprite_Delete(dt->apBase[i]);   /* not nulled */
}

/* one UnitSprite panel named szName + its layout rect into aBaseRect[n] */
static inline void mopt_new_base(struct stMobj *pMObj, struct struct_599addfb *dt, int n, char *szName, int id,
                                 struct _LAYOUT_RET_DATA *ret)
{
    dt->apBase[n] = UnitSprite_New(pMObj, (struct _LO_HEADER *)mopt_hdr(0x3fb), szName);
    DF_LO_GetLayoutData((unsigned char *)mopt_hdr(0x3fb), (unsigned char *)szName, id, ret);
    dt->aBaseRect[n] = ret->Pos;
}

/* label ids, OLMENU 0x1edf8c0 (copied to sp+0x160; entry 5 = 0x192b2 is skipped) */
static const int mopt_option_name_index[18] = {
    0x192a8, 0x192a8, 0x192a9, 0x192a9, 0x192aa, 0x192b2, 0x192ab, 0x192ad, 0x186c5,
    0x186c6, 0x192b3, 0x192b4, 0x192b5, 0x192ae, 0x186c5, 0x186c6, 0x192af, 0,
};

static void __MenuSysOption_Enter(struct stMobj *pMObj)
{
    PS2_FRAME(0x350);
    PS2_STACK(char[256], szName, 0x240);   /* Pf_SPrintf buffers (the hybrid formats into PS2 memory) */
    PS2_STACK(char[64], szVol, 0x200);
    struct struct_599addfb *dt = mopt_dt(pMObj);
    struct _LAYOUT_RET_DATA stLayoutData;
    struct _LAYOUT_RET_DATA Layout;
    struct _LAYOUT_RET_DATA LayoutData;
    int nBase = 0, nString = 0, i;

    for (i = 0; i < 2; i++) {
        pf_sprintf_d(szName, mopt_fmt_sound, i);
        mopt_new_base(pMObj, dt, nBase++, szName, 1, &stLayoutData);
    }
    pf_sprintf_d(szName, mopt_fmt_stereo2, 0);
    mopt_new_base(pMObj, dt, nBase++, szName, 0, &stLayoutData);
    for (i = 0; i < 3; i++) {
        pf_sprintf_d(szName, mopt_fmt_2sentaku, i);
        mopt_new_base(pMObj, dt, nBase++, szName, 0, &stLayoutData);
    }
    pf_sprintf_d(szName, mopt_fmt_key, 0);
    mopt_new_base(pMObj, dt, nBase, szName, 1, &stLayoutData);   /* nBase stays 6 */
    if (dt->nMode == 2) {
        struct struct_214acd28 stOffset = {0, 0x1e};   /* gp 0x27bc28 (ld, stored before the sprintf) */
        pf_sprintf_d(szName, mopt_fmt_key, 0);
        dt->apBase[nBase + 1] = UnitSprite_New(pMObj, (struct _LO_HEADER *)mopt_hdr(0x3fb), szName);
        UnitSprite_SetPosition(dt->apBase[nBase + 1], &stOffset);
        DF_LO_GetLayoutData((unsigned char *)mopt_hdr(0x3fb), (unsigned char *)szName, 1, &stLayoutData);
        dt->aBaseRect[nBase + 1] = stLayoutData.Pos;
        dt->aBaseRect[nBase + 1].x += stOffset.x;
        dt->aBaseRect[nBase + 1].y += stOffset.y;
    }

    for (i = 0; i < 0x11; i++) {
        if (i == 5) continue;
        pf_sprintf_d(szName, mopt_fmt_parts20d, i);
        char *hdr = (char *)mopt_hdr(0x417);
        dt->apString[nString++] = DF_MOBJ_NewStringFromLayoutName(pMObj, 0, mopt_option_name_index[i], 0, hdr, szName,
                                                                  0, 0, 0);
    }
    if (dt->nMode == 2) {
        pf_sprintf_d(szName, mopt_fmt_parts20d, 0x10);
        memset(&Layout, 0, 0x50);
        DF_LO_GetLayoutData((unsigned char *)mopt_hdr(0x417), (unsigned char *)szName, 0, &Layout);
        Layout.Pos.y += 0x1e;
        dt->apString[nString] = DF_MOBJ_NewStringBaseFromLayout(pMObj, &Layout, 0, 0, 0);
        DF_MOBJ_SetStringID(dt->apString[nString], 0, 0x192b1, 0);
    }

    dt->pBgmVolLArrow = DF_MOBJ2_NewBlinkSpriteFromLayoutName(pMObj, (char *)mopt_hdr(0x417), mopt_name_left_0, 0, 0);
    dt->pBgmVolRArrow = DF_MOBJ2_NewBlinkSpriteFromLayoutName(pMObj, (char *)mopt_hdr(0x417), mopt_name_right_0, 0, 0);
    dt->pSeVolLArrow = DF_MOBJ2_NewBlinkSpriteFromLayoutName(pMObj, (char *)mopt_hdr(0x417), mopt_name_left_1, 0, 0);
    dt->pSeVolRArrow = DF_MOBJ2_NewBlinkSpriteFromLayoutName(pMObj, (char *)mopt_hdr(0x417), mopt_name_right_1, 0, 0);
    dt->pSurroundCursor = WindowEx_NewFromLayout(pMObj, 0, (char *)mopt_hdr(0x417), mopt_name_parts10_1);
    dt->pTextMessageCursor = WindowEx_NewFromLayout(pMObj, 0, (char *)mopt_hdr(0x417), mopt_name_parts10_2);
    dt->pKeyTypeCursor = WindowEx_NewFromLayout(pMObj, 0, (char *)mopt_hdr(0x417), mopt_name_parts10_4);
    dt->pVibrationCursor = WindowEx_NewFromLayout(pMObj, 0, (char *)mopt_hdr(0x417), mopt_name_parts10_7);

    struct struct_76b810ec color = {0x50, 0xc8, 0xa0, 0x28};   /* gp 0x27bc30 */
    alignas(16) int uv[4] = {0xd0, 0x22, 0x100, 0x52};          /* OLMENU 0x1edf910 */
    pf_sprintf_d(szName, mopt_fmt_sound, 0);
    if (DF_LO_GetLayoutData((unsigned char *)mopt_hdr(0x3fb), (unsigned char *)szName, 1, &LayoutData) == 1) {
        int sel = dt->nSelectIndex;
        alignas(16) struct stMenuRect stMoveRect;
        const struct struct_062d421d *r = &dt->aBaseRect[sel];
        stMoveRect.x = r->x; stMoveRect.y = r->y; stMoveRect.w = r->w; stMoveRect.h = r->h;
        if (sel < 6) {
            stMoveRect.w = 0x2d8;
            stMoveRect.h = 0x7e;
            stMoveRect.x -= 0x50;
            stMoveRect.y -= 0x28;
        } else {
            stMoveRect.w = 0x1e5;
            stMoveRect.h = 0x54;
            stMoveRect.x -= 0x35;
            stMoveRect.y -= 0x1a;
        }
        dt->pLightCursor = DF_MOBJ2_NewBlinkSpriteNX(pMObj, 0x3ec, &stMoveRect, uv, &color, 0, 0);
        dt->pLightCursor->dt.sprite.reg_alpha = 0x48;   /* sd, no null test */
    }

    struct stMobj *bar = DF_MOBJ_GetNewObject(pMObj);
    if (bar) { bar->type = 0; bar->stat = 1; mopt_bar(bar)->nDrawY = 0x45; }
    dt->pBgmVolBar = bar;
    bar = DF_MOBJ_GetNewObject(pMObj);
    if (bar) { bar->type = 0; bar->stat = 1; mopt_bar(bar)->nDrawY = 0x76; }
    dt->pSeVolBar = bar;
    dt->pDolbySurround = UnitSprite_New(pMObj, (struct _LO_HEADER *)mopt_hdr(0x417), mopt_name_dolby2_0);
    dt->pChild = 0;

    dt->nBgmVol = Game_GetOptionValue(dt->nPlayer, 1);
    dt->nSeVol = Game_GetOptionValue(dt->nPlayer, 6);
    if (dt->nBgmVol) dt->nBgmVol -= 0x1b;
    if (dt->nSeVol) dt->nSeVol -= 0x1b;
    int v = Game_GetOptionValue(dt->nPlayer, 5);
    if (v == 2) dt->nSurround = 2;
    else if (v == 1) dt->nSurround = 1;
    else if (v == 0) dt->nSurround = 0;   /* else keeps New's 1 */
    dt->bTextMessage = Game_GetOptionValue(dt->nPlayer, 3);
    MenuKeyCnfg_GetKeyType(&dt->nKeyType, dt->nPlayer);
    dt->bVibration = Game_GetOptionValue(dt->nPlayer, 4);

    pf_sprintf_d(szVol, mopt_fmt_3d, dt->nBgmVol);
    DF_MOBJ_SetStringCopy(dt->apString[1], szVol);
    pf_sprintf_d(szVol, mopt_fmt_3d, dt->nSeVol);
    DF_MOBJ_SetStringCopy(dt->apString[3], szVol);
    __MenuSysOption_UpdateMeter(dt->pBgmVolBar, dt->nBgmVol);
    __MenuSysOption_UpdateMeter(dt->pSeVolBar, dt->nSeVol);

    switch ((unsigned char)dt->nSurround) {
    case 2: WindowEx_MoveToLayout(dt->pSurroundCursor, (char *)mopt_hdr(0x417), mopt_name_parts10a_0, 0); break;
    case 1: WindowEx_MoveToLayout(dt->pSurroundCursor, (char *)mopt_hdr(0x417), mopt_name_parts10_1, 0); break;
    case 0: WindowEx_MoveToLayout(dt->pSurroundCursor, (char *)mopt_hdr(0x417), mopt_name_parts10_0, 0); break;
    default: break;
    }
    WindowEx_MoveToLayout(dt->pTextMessageCursor, (char *)mopt_hdr(0x417),
                          dt->bTextMessage ? mopt_name_parts10_2 : mopt_name_parts10_3, 0);
    if ((unsigned char)dt->nKeyType == 1)
        WindowEx_MoveToLayout(dt->pKeyTypeCursor, (char *)mopt_hdr(0x417), mopt_name_parts10_5, 0);
    else if ((unsigned char)dt->nKeyType == 0)
        WindowEx_MoveToLayout(dt->pKeyTypeCursor, (char *)mopt_hdr(0x417), mopt_name_parts10_4, 0);
    WindowEx_MoveToLayout(dt->pVibrationCursor, (char *)mopt_hdr(0x417),
                          dt->bVibration ? mopt_name_parts10_6 : mopt_name_parts10_7, 0);

    for (i = 0; i < 8; i++) dt->nAlphaFrame[i] = 0;
    DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x192a8));
    __MenuSysOption_SetM1Help(pMObj);
    DF_MVIEW_MotionStart(MViewp, 0x96);
}

struct stMobj *MenuSysOption_New(struct stMobj *pParentMobj, char nPlayer, int nMode, int nFlag)
{
    PS2_FRAME(0x60);
    struct stMobj *p = DF_MOBJ_GetNewObject(pParentMobj);
    if (!p) return 0;
    struct struct_599addfb *dt = mopt_dt(p);
    ZeroMemory(dt, 0xec);
    p->type = 0;
    p->stat = 1;
    p->play_func = (void (*)(void))GFN(__MenuSysOption_Play);
    dt->nMode = nMode;
    dt->nFlag = nFlag;
    dt->nPlayer = nPlayer;
    dt->nSelectIndex = 0;
    dt->nBgmVol = 0x64;
    dt->nSeVol = 0x64;
    dt->nSurround = 1;
    dt->nKeyType = 0;
    dt->bVibration = 0;
    dt->nChildKind = 0;
    dt->nExit = 0;
    dt->aBaseRect = (struct struct_062d421d *)DF_MMM_Alloc(0, 0x80);
    __MenuSysOption_Enter(p);
    DF_MBACK_PushSceneLabel(0x4b);
    __MenuSysOption_SetFadeIn(p);
    if (dt->nMode == 2) {
        DF_MBGUIDE_Show();
        Bgm_Play();
    }
    return p;
}

/* ---- the option screen frame loop (garage3 g3bc) ---- */

static inline struct struct_2d84b07b *mopt_pad(void) { return &MPad[MPadUser]; }

/* left / right repeat pressed this frame (repeat bits 2 / 3) */
static inline int mopt_toggle(void)
{
    struct struct_2d84b07b *pad = mopt_pad();
    return pad->repeat.lt == 1 || pad->repeat.rt == 1;
}

static inline void mopt_help_if_moved(struct struct_599addfb *dt, unsigned int id)
{
    if (dt->nPrevSelectIndex != dt->nSelectIndex) DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, id));
}

static inline void mopt_move(struct stMobj *win, char *name)
{
    WindowEx_MoveToLayout(win, (char *)mopt_hdr(0x417), name, 0);
}

/* the BGM / SE arrows fade in / out as the cursor enters / leaves items 0 / 1; the light cursor follows the item */
static void __MenuSysOption_MObjAnmMng(struct stMobj *pMObj)
{
    PS2_FRAME(0x60);
    struct struct_599addfb *dt = mopt_dt(pMObj);
    switch ((unsigned int)dt->nSelectIndex) {
    case 0:
        if (dt->nPrevSelectIndex != 0) {
            DF_MOBJ2_SetBlinkSpriteFadeIn(dt->pBgmVolLArrow);
            DF_MOBJ2_SetBlinkSpriteFadeIn(dt->pBgmVolRArrow);
        }
        if (dt->nPrevSelectIndex == 1) {
            DF_MOBJ2_SetBlinkSpriteFadeOut(dt->pSeVolLArrow);
            DF_MOBJ2_SetBlinkSpriteFadeOut(dt->pSeVolRArrow);
        }
        break;
    case 1:
        if (dt->nPrevSelectIndex != 1) {
            DF_MOBJ2_SetBlinkSpriteFadeIn(dt->pSeVolLArrow);
            DF_MOBJ2_SetBlinkSpriteFadeIn(dt->pSeVolRArrow);
        }
        if (dt->nPrevSelectIndex == 0) {
            DF_MOBJ2_SetBlinkSpriteFadeOut(dt->pBgmVolLArrow);
            DF_MOBJ2_SetBlinkSpriteFadeOut(dt->pBgmVolRArrow);
        }
        break;
    case 2: case 3: case 4: case 5: case 6: case 7:
        if (dt->nPrevSelectIndex == 0) {
            DF_MOBJ2_SetBlinkSpriteFadeOut(dt->pBgmVolLArrow);
            DF_MOBJ2_SetBlinkSpriteFadeOut(dt->pBgmVolRArrow);
        }
        if (dt->nPrevSelectIndex == 1) {
            DF_MOBJ2_SetBlinkSpriteFadeOut(dt->pSeVolLArrow);
            DF_MOBJ2_SetBlinkSpriteFadeOut(dt->pSeVolRArrow);
        }
        break;
    default: break;
    }
    int sel = dt->nSelectIndex;   /* also >= 8 (never): reads past aBaseRect like the asm */
    struct struct_062d421d r = dt->aBaseRect[sel];
    if (sel < 6) {
        r.x -= 0x50;
        r.y -= 0x28;
        r.w = 0x2d8;
        r.h = 0x7e;
    } else {
        r.x -= 0x35;
        r.y -= 0x1a;
        r.w = 0x1e5;
        r.h = 0x54;
    }
    DF_MOBJ2_SetBlinkSpritePos(dt->pLightCursor, r.x, r.y);
    DF_MOBJ2_SetBlinkSpriteSize(dt->pLightCursor, r.w, r.h);
}

static inline void mopt_surround_cursor(struct struct_599addfb *dt)
{
    switch ((unsigned char)dt->nSurround) {
    case 2: mopt_move(dt->pSurroundCursor, mopt_name_parts10a_0); break;
    case 1: mopt_move(dt->pSurroundCursor, mopt_name_parts10_1); break;
    case 0: mopt_move(dt->pSurroundCursor, mopt_name_parts10_0); break;
    default: break;
    }
}

/* item 2: left / right step the surround mode (left: 1 -> 2, else -1; right: 2 -> 1, else +1) */
static void __MenuSysOption_ChangeSurround(struct stMobj *pMObj)
{
    PS2_FRAME(0x40);
    struct struct_599addfb *dt = mopt_dt(pMObj);
    if (mopt_pad()->repeat.lt == 1) {
        if ((unsigned char)dt->nSurround == 1) dt->nSurround = (enum_0850e666)2;
        else dt->nSurround = (enum_0850e666)(dt->nSurround - 1);
        Voice_Call(0x3e9, 0x7f);
        mopt_surround_cursor(dt);
    }
    if (mopt_pad()->repeat.rt == 1) {
        if ((unsigned char)dt->nSurround == 2) dt->nSurround = (enum_0850e666)1;
        else dt->nSurround = (enum_0850e666)(dt->nSurround + 1);
        Voice_Call(0x3e9, 0x7f);
        mopt_surround_cursor(dt);
    }
    mopt_help_if_moved(dt, 0x192aa);
    switch ((unsigned char)dt->nSurround) {
    case 2: Game_SetOptionValue(dt->nPlayer, 5, 2); break;
    case 1: Game_SetOptionValue(dt->nPlayer, 5, 1); break;
    case 0: Game_SetOptionValue(dt->nPlayer, 5, 0); break;
    default: break;
    }
}

/* items 0 / 1: left / right (with the key-repeat gate bVolumeKeyDown) step a volume 0..100; the game value is
 * volume + 0x1b, 0 stays 0 */
static void __MenuSysOption_ChangeSeVolume(struct stMobj *pMObj)
{
    PS2_FRAME(0x60);
    PS2_STACK(char[64], szVol, 0x20);
    struct struct_599addfb *dt = mopt_dt(pMObj);
    if (mopt_pad()->lt && dt->bVolumeKeyDown == 1) {
        dt->nSeVol--;
        if (dt->nSeVol >= 0) Voice_Call(0x3f2, 0x7f);
        else dt->nSeVol = 0;
        __MenuSysOption_UpdateMeter(dt->pSeVolBar, dt->nSeVol);
    }
    if (mopt_pad()->rt && dt->bVolumeKeyDown == 1) {
        dt->nSeVol++;
        if (dt->nSeVol < 0x65) Voice_Call(0x3f2, 0x7f);
        else dt->nSeVol = 0x64;
        __MenuSysOption_UpdateMeter(dt->pSeVolBar, dt->nSeVol);
    }
    mopt_help_if_moved(dt, 0x192a9);
    pf_sprintf_d(szVol, mopt_fmt_3d, dt->nSeVol);
    DF_MOBJ_SetStringCopy(dt->apString[3], szVol);
    if (dt->nSeVol == 0) Game_SetSeVolume(dt->nPlayer, 0);
    else Game_SetSeVolume(dt->nPlayer, dt->nSeVol + 0x1b);
}

static void __MenuSysOption_ChangeBgmVolume(struct stMobj *pMObj)
{
    PS2_FRAME(0x60);
    PS2_STACK(char[64], szVol, 0x20);
    struct struct_599addfb *dt = mopt_dt(pMObj);
    if (mopt_pad()->lt && dt->bVolumeKeyDown == 1) {
        dt->nBgmVol--;
        if (dt->nBgmVol >= 0) Voice_Call(0x3f2, 0x7f);
        else dt->nBgmVol = 0;
        __MenuSysOption_UpdateMeter(dt->pBgmVolBar, dt->nBgmVol);
    }
    if (mopt_pad()->rt && dt->bVolumeKeyDown == 1) {
        dt->nBgmVol++;
        if (dt->nBgmVol < 0x65) Voice_Call(0x3f2, 0x7f);
        else dt->nBgmVol = 0x64;
        __MenuSysOption_UpdateMeter(dt->pBgmVolBar, dt->nBgmVol);
    }
    mopt_help_if_moved(dt, 0x192a8);
    pf_sprintf_d(szVol, mopt_fmt_3d, dt->nBgmVol);
    DF_MOBJ_SetStringCopy(dt->apString[1], szVol);
    if (dt->nBgmVol == 0) Game_SetBgmVolume(dt->nPlayer, 0);
    else Game_SetBgmVolume(dt->nPlayer, dt->nBgmVol + 0x1b);
}

/* the selected item's input handler (+ its help line when the cursor just moved) */
static void __MenuSysOption_ChangeValue(struct stMobj *pMObj)
{
    PS2_FRAME(0x40);
    struct struct_599addfb *dt = mopt_dt(pMObj);
    switch ((unsigned int)dt->nSelectIndex) {
    case 0: __MenuSysOption_ChangeBgmVolume(pMObj); break;
    case 1: __MenuSysOption_ChangeSeVolume(pMObj); break;
    case 2: __MenuSysOption_ChangeSurround(pMObj); break;
    case 3:
        if (mopt_toggle()) {
            dt->bTextMessage = !dt->bTextMessage;
            mopt_move(dt->pTextMessageCursor, dt->bTextMessage ? mopt_name_parts10_2 : mopt_name_parts10_3);
            Voice_Call(0x3e9, 0x7f);
        }
        mopt_help_if_moved(dt, 0x192ad);
        break;
    case 4:
        if (mopt_toggle()) {
            if ((unsigned char)dt->nKeyType == 1) dt->nKeyType = 0;
            else if ((unsigned char)dt->nKeyType == 0) dt->nKeyType = 1;   /* other values unchanged */
            Voice_Call(0x3e9, 0x7f);
            if ((unsigned char)dt->nKeyType == 1) mopt_move(dt->pKeyTypeCursor, mopt_name_parts10_5);
            else if ((unsigned char)dt->nKeyType == 0) mopt_move(dt->pKeyTypeCursor, mopt_name_parts10_4);
        }
        mopt_help_if_moved(dt, 0x192b3);
        break;
    case 5:
        if (mopt_toggle()) {
            dt->bVibration = !dt->bVibration;   /* stored in the Voice_Call delay slot */
            Voice_Call(0x3e9, 0x7f);
            mopt_move(dt->pVibrationCursor, dt->bVibration ? mopt_name_parts10_6 : mopt_name_parts10_7);
        }
        mopt_help_if_moved(dt, 0x192ae);
        break;
    case 6:
        if (mopt_pad()->Rdw == 1) {
            dt->nChildKind = 2;   /* KEY_ASSIGN */
            dt->nExit = 2;
            Voice_Call(0x3ea, 0x7f);
            __MenuSysOption_SetFadeOut(pMObj);
        }
        mopt_help_if_moved(dt, 0x192af);
        break;
    case 7:
        if (mopt_pad()->Rdw == 1) {
            dt->nExit = 2;
            Voice_Call(0x3ea, 0x7f);
            DF_MBACK_PopSceneLabel();
            MenuMng.stat = 2;   /* in the SetFadeOut jal delay slot: before the call */
            __MenuSysOption_SetFadeOut(pMObj);
        }
        mopt_help_if_moved(dt, 0x192b1);
        break;
    default: break;
    }
}

/* up / down move the cursor with wrap-around (8 items in nMode 2, 7 in nMode 0 / 1) */
static void __MenuSysOption_SelectItem(struct stMobj *pMObj)
{
    PS2_FRAME(0x10);
    struct struct_599addfb *dt = mopt_dt(pMObj);
    int bCursorMove = 0;
    dt->nPrevSelectIndex = dt->nSelectIndex;
    if (mopt_pad()->repeat.up == 1) {
        dt->nSelectIndex--;
        bCursorMove = 1;   /* delay slot: set whether or not it wrapped */
        if (dt->nSelectIndex < 0) {
            if (dt->nMode == 2) dt->nSelectIndex = 7;
            else if (dt->nMode == 1 || dt->nMode == 0) dt->nSelectIndex = 6;   /* other nMode: stays -1 */
        }
    }
    if (mopt_pad()->repeat.dw == 1) {
        dt->nSelectIndex++;
        bCursorMove = 1;
        if (dt->nMode == 2) {
            if (dt->nSelectIndex >= 8) dt->nSelectIndex = 0;
        } else if (dt->nMode == 1 || dt->nMode == 0) {
            if (dt->nSelectIndex >= 7) dt->nSelectIndex = 0;
        }
    }
    if (bCursorMove == 1) Voice_Call(0x3e9, 0x7f);
}

/* every visible part of the screen on / off (around the NEW GAME dialog child) */
static void __MenuSysOption_SetDrawFlag(struct stMobj *pMObj, int bDraw)
{
    PS2_FRAME(0x50);
    struct struct_599addfb *dt = mopt_dt(pMObj);
    DF_MOBJ_SetDrawEnable(mopt_bar(dt->pSeVolBar)->pMeter, bDraw);
    DF_MOBJ_SetDrawEnable(mopt_bar(dt->pBgmVolBar)->pMeter, bDraw);
    DF_MOBJ_SetDrawEnable(dt->pDolbySurround, bDraw);
    DF_MOBJ_SetDrawEnable(dt->pLightCursor, bDraw);
    DF_MOBJ_SetDrawEnable(dt->pVibrationCursor, bDraw);
    DF_MOBJ_SetDrawEnable(dt->pKeyTypeCursor, bDraw);
    DF_MOBJ_SetDrawEnable(dt->pTextMessageCursor, bDraw);
    DF_MOBJ_SetDrawEnable(dt->pSurroundCursor, bDraw);
    DF_MOBJ_SetDrawEnable(dt->pSeVolRArrow, bDraw);
    DF_MOBJ_SetDrawEnable(dt->pSeVolLArrow, bDraw);
    DF_MOBJ_SetDrawEnable(dt->pBgmVolRArrow, bDraw);
    DF_MOBJ_SetDrawEnable(dt->pBgmVolLArrow, bDraw);
    for (int i = 0; i < 18; i++)
        if (dt->apString[i]) DF_MOBJ_SetDrawEnable(dt->apString[i], bDraw);
    for (int i = 0; i < 8; i++)
        if (dt->apBase[i]) DF_MOBJ_SetDrawEnable(dt->apBase[i], bDraw);
}

/* stat 3: a child screen runs; when it is dead, come back (key config: rebuild; NEW GAME dialog: redraw or leave) */
static void __MenuSysOption_PlayOnDispatch(struct stMobj *pMObj)
{
    PS2_FRAME(0x40);
    struct struct_599addfb *dt = mopt_dt(pMObj);
    struct stMobj *c = dt->pChild;
    if (!c) return;
    if (c->stat != 5) return;
    unsigned char kind = (unsigned char)dt->nChildKind;
    if (kind == 2) {   /* KEY_ASSIGN */
        pMObj->stat = 0;
        if (dt->pChild) {
            ngdlg_erase(dt->pChild);
            dt->pChild = 0;
        }
        dt->pChild = 0;
        dt->nChildKind = 0;   /* in the Enter jal delay slot */
        __MenuSysOption_Enter(pMObj);
        __MenuSysOption_SetM1Help(pMObj);
        __MenuSysOption_SetFadeIn(pMObj);
    } else if (kind == 1) {   /* NEW_GAME_DLG */
        pMObj->stat = 0;
        if (mopt_child_exit(c) == 1) {
            __MenuSysOption_SetDrawFlag(pMObj, 1);
            __MenuSysOption_SetFadeIn(pMObj);
        } else {
            __MenuSysOption_SetFadeOut(pMObj);
        }
        MenuKeyCnfg_GetKeyType(&dt->nKeyType, dt->nPlayer);
        if ((unsigned char)dt->nKeyType == 1) mopt_move(dt->pKeyTypeCursor, mopt_name_parts10_5);
        else if ((unsigned char)dt->nKeyType == 0) mopt_move(dt->pKeyTypeCursor, mopt_name_parts10_4);
        __MenuSysOption_SetM1Help(pMObj);
        if (dt->pChild) {
            ngdlg_erase(dt->pChild);
            dt->pChild = 0;
        }
        dt->pChild = 0;
        dt->nChildKind = 0;
    }
}

/* stat 2: 18 (nMode 2) / 6 frames of fade, then die or hand over to the child */
static void __MenuSysOption_PlayOnFadeOut(struct stMobj *pMObj)
{
    PS2_FRAME(0x30);   /* calls the interpreted MenuKeyCnfg_New */
    struct struct_599addfb *dt = mopt_dt(pMObj);
    unsigned char old = pMObj->count;
    pMObj->count = old + 1;
    if (dt->nMode == 2) {
        if (old < 0x12) return;
    } else {
        if (old < 6) return;
    }
    pMObj->count = 0;
    unsigned char kind = (unsigned char)dt->nChildKind;
    if (kind == 0) {
        __MenuSysOption_Exit(pMObj);
        if (dt->aBaseRect) {
            DF_MMM_Free(0, dt->aBaseRect);
            dt->aBaseRect = 0;
        }
        pMObj->stat = 5;
        pMObj->play_func = 0;
        pMObj->draw_func = 0;
        pMObj->pkt_func = 0;
        return;
    }
    if (kind == 2) dt->pChild = MenuKeyCnfg_New(pMObj, dt->nPlayer, dt->nMode);
    pMObj->stat = 3;
    __MenuSysOption_Exit(pMObj);
}

static void __MenuSysOption_SetFadeOut(struct stMobj *pMObj)
{
    PS2_FRAME(0x40);
    struct struct_599addfb *dt = mopt_dt(pMObj);
    pMObj->stat = 2;
    pMObj->count = 0;
    DF_MOBJ2_SetBlinkSpriteFadeOut(dt->pLightCursor);
    for (int i = 0; i < 8; i++)
        if (dt->apBase[i]) UnitSprite_SetFadeOut(dt->apBase[i]);
    for (int i = 0; i < 18; i++)
        if (dt->apString[i]) DF_MOBJ_SetFadeOut(dt->apString[i]);
    if (dt->nSelectIndex == 0) {
        DF_MOBJ2_SetBlinkSpriteFadeOut(dt->pBgmVolLArrow);
        DF_MOBJ2_SetBlinkSpriteFadeOut(dt->pBgmVolRArrow);
    } else if (dt->nSelectIndex == 1) {
        DF_MOBJ2_SetBlinkSpriteFadeOut(dt->pSeVolLArrow);
        DF_MOBJ2_SetBlinkSpriteFadeOut(dt->pSeVolRArrow);
    }
    WindowEx_SetFadeOut(dt->pSurroundCursor);
    WindowEx_SetFadeOut(dt->pTextMessageCursor);
    WindowEx_SetFadeOut(dt->pKeyTypeCursor);
    WindowEx_SetFadeOut(dt->pVibrationCursor);
    UnitSprite_SetFadeOut(dt->pDolbySurround);
    DF_MOBJ_SetFadeOut(mopt_bar(dt->pBgmVolBar)->pMeter);   /* bars not null-tested, meter may be NULL */
    DF_MOBJ_SetFadeOut(mopt_bar(dt->pSeVolBar)->pMeter);
    if (dt->nMode == 2) {
        if ((unsigned char)dt->nChildKind == 0) {
            if ((unsigned char)dt->nExit == 1) {   /* CANCEL */
                DF_M1HELP_FadeOut();
                DF_MBACK_SetFadeOutNX();   /* a0 = 0 (DWARF: no parameter) */
                DF_MVIEW_MotionStart(MViewp, 1);
                DF_MBACK_PopSceneLabel();
                if (dt->nFlag == 10) DF_MBACK_PopSceneLabel();
                DF_MBGUIDE_Hide();
                Bgm_FadeOutStart();
            } else {
                DF_M1HELP_FadeOut();
            }
        } else {
            DF_M1HELP_FadeOut();
            DF_MVIEW_MotionStart(MViewp, 0x95);
        }
    } else if ((unsigned char)dt->nChildKind == 0) {
        DF_M1HELP_FadeOut();
        DF_MBACK_PopSceneLabel();
    } else {
        DF_M1HELP_FadeOut();
        DF_MVIEW_MotionStart(MViewp, 0x95);
    }
}

/* stat 0: key guide (Start), circle = cancel, left / right repeat gate, then the item handlers; text message /
 * key type / vibration are written to the game every frame */
static void __MenuSysOption_PlayOnStable(struct stMobj *pMObj)
{
    PS2_FRAME(0x30);
    struct struct_599addfb *dt = mopt_dt(pMObj);
    if (dt->nMode == 2) {
        if (MenuKeyGuide_IsShown()) return;
        if (mopt_pad()->start == 1) { MenuKeyGuide_Show(0); return; }
    } else {
        if (MenuKeyGuide_IsShown()) return;
        if (mopt_pad()->start == 1) { MenuKeyGuide_Show(10); return; }
    }
    struct struct_2d84b07b *pad = mopt_pad();
    if (pad->Rrt) {
        dt->nExit = 1;   /* CANCEL (delay slot, before SetFadeOut) */
        __MenuSysOption_SetFadeOut(pMObj);
        Voice_Call(0x3eb, 0x7f);
        return;
    }
    if (pad->lt || pad->rt) {
        if (dt->nVolumeKeyWait == 0) {
            dt->nVolumeKeyWait = 10;
            dt->bVolumeKeyDown = 1;
        } else {
            dt->nVolumeKeyWait--;
            if (dt->nVolumeKeyWait == 0) {
                dt->bVolumeKeyDown = 1;
                dt->nVolumeKeyWait++;
            } else {
                dt->bVolumeKeyDown = 0;
            }
        }
    } else {
        dt->bVolumeKeyDown = 0;
        dt->nVolumeKeyWait = 0;
    }
    __MenuSysOption_SelectItem(pMObj);
    __MenuSysOption_ChangeValue(pMObj);
    __MenuSysOption_MObjAnmMng(pMObj);
    Game_SetOptionValue(dt->nPlayer, 3, dt->bTextMessage);
    MenuKeyCnfg_SetKeyType(dt->nKeyType, dt->nPlayer);
    Game_SetOptionValue(dt->nPlayer, 4, dt->bVibration);
}

static void __MenuSysOption_Play(struct stMobj *pMObj)
{
    PS2_FRAME(0x10);
    switch (pMObj->stat) {
    case 3: __MenuSysOption_PlayOnDispatch(pMObj); break;
    case 2: __MenuSysOption_PlayOnFadeOut(pMObj); break;
    case 0: __MenuSysOption_PlayOnStable(pMObj); break;
    case 1: {
        unsigned char c = pMObj->count;
        pMObj->count = c + 1;
        if (c >= 6) {
            pMObj->count = 0;
            pMObj->stat = 0;
        }
        break;
    }
    default: break;
    }
}
