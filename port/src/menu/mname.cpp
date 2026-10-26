/* src/menu/mname.cpp - the menu's name-entry screens (OLMENU). The "1" set (DF_MNAMEE_NewSystem, from the garage) is a
 * two-row chooser (player name / AC name, struct_ea2d5f43 in the object's dt) that launches the actual software
 * keyboard (the "2" set, DF_MNAMEE2_NewSystem, struct_f0eacc3b) as a child object and waits for it; the keyboard's
 * confirm dialog (Dispatch2) and its fade-out / save (SetFadeOut2 / FadeOut2) are here too. Everything is a chain of
 * stMobj.play_func state machines driven by DF_MOBJ_Play.
 * Whole file native: the chooser (NewSystem, FadeIn, System, SetFadeOut, FadeOut, Dispatch), the keyboard build
 * (DF_MNAMEE2_NewSystem, FadeIn2, ChangePage, PageFadeInOut), its input loop (PlayNameEditSystem2), its exit path
 * (Dispatch2, SetFadeOut2, FadeOut2) and the page-5 row scroller / draw callback (K2PageScroll, DrawNameEditSystem;
 * page 5 is never entered).
 * The sprintf buffers, rects and colour words handed to interpreted code live at their original PS2 stack slots. */
#include <string.h>
#include "acnb/pf.h"
#include "acnb/nodwarf.h"

#ifdef ACNB_SOURCEPORT
char mname_name0[8] = "name_0";
char mname_shita0[8] = "shita_0";
char mname_empty_str[4] = "";
#endif

#define MNAME_OLD(o) ((struct_ea2d5f43 *)&(o)->dt)
#define MNAME_NEW(o) ((struct_f0eacc3b *)&(o)->dt)
#define MNAME_PLAY(f) ((void (*)(void))GFN(f))

/* DF_MBGUIDE_Change / DF_MBACK_ChangeTag are void(void) in DWARF but read a0..t0 / a0 */
static inline void mname_guide(int a, int b, int c, int d, int e)
{
    gcall_as<int(int, int, int, int, int)>(GFN(DF_MBGUIDE_Change), a, b, c, d, e);
}
static inline void mname_tag(int tag)
{
    gcall_as<void(int)>(GFN(DF_MBACK_ChangeTag), tag);
}

/* "f_draw = 0, f_erase = 1" (two read-modify-writes of byte 0 in the original) */
static inline void mname_erase(struct stMobj *o)
{
    o->f_draw = 0;
    o->f_erase = 1;
}

static void __DrawNameEditSystem(void)
{
}

/* ---- the old chooser ---- */

static void __SetNameEditSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct_ea2d5f43 *cs = MNAME_OLD(ioObjp);
    for (int i = 0; i < 2; i++) UnitSprite_SetFadeOut(cs->apNameWnd[i]);
    for (int i = 0; i < 2; i++) DF_MOBJ_SetFadeOut(cs->apStrName[i]);
    for (int i = 0; i < 2; i++) DF_MOBJ_SetFadeOut(cs->apStrLabel[i]);
    WindowEx_SetFadeOut(cs->pCursor);
    DF_M1HELP_FadeOut();
    ioObjp->count = 0;
    cs->step = 0;
    ioObjp->stat = 2;
    ioObjp->play_func = MNAME_PLAY(__PlayNameEditSystemFadeOut);
}

/* the keyboard child finished (stat 5, read before the null test): erase it and fade the chooser back in */
static void __PlayNameEditDispatch(struct stMobj *ioObjp)
{
    struct_ea2d5f43 *cs = MNAME_OLD(ioObjp);
    if (cs->csysp->stat != 5) return;
    if (cs->csysp) mname_erase(cs->csysp);
    cs->csysp = 0;
    cs->step = 0;
    ioObjp->stat = 1;
    ioObjp->count = 0xf;
    ioObjp->play_func = MNAME_PLAY(__PlayNameEditSystemFadeIn);
}

/* step 1 (cancel): wait for the camera, fade the AC hand models back in, done. Other steps: after 7 frames delete
 * the chooser's objects; cancel turns the camera back (step 1), confirm opens the keyboard and waits (Dispatch) */
static void __PlayNameEditSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct_ea2d5f43 *cs = MNAME_OLD(ioObjp);
    if (cs->step == 1) {
        if (!DF_MVIEW_MotionSync(MViewp)) return;
        if (cs->mode == 0) {
            DF_M_AC_SetFadeIn(M_ACp[cs->player][1], 0xf, 0.0f);
            DF_M_AC_SetFadeIn(M_ACp[cs->player][2], 0xf, 0.0f);
        }
        ioObjp->stat = 5;
        ioObjp->play_func = 0;
        return;
    }
    if (++ioObjp->count < 7) return;
    for (int i = 0; i < 2; i++) {
        UnitSprite_Delete(cs->apNameWnd[i]);
        cs->apNameWnd[i] = 0;
    }
    for (int i = 0; i < 2; i++) {
        if (cs->apStrName[i]) mname_erase(cs->apStrName[i]);
        cs->apStrName[i] = 0;
    }
    for (int i = 0; i < 2; i++) {
        if (cs->apStrLabel[i]) mname_erase(cs->apStrLabel[i]);
        cs->apStrLabel[i] = 0;
    }
    WindowEx_Delete(cs->pCursor);
    cs->pCursor = 0;
    DF_MBACK_PopSceneLabel();
    if (cs->func == 0) {
        if (cs->mode == 0) {
            mname_tag(0);
            DF_MVIEW_MotionStart(MViewp, 0xa);
            mname_guide(4, 1, 2, 3, 0x63);
        } else {
            mname_tag(0x17);
            DF_MVIEW_MotionStart(MViewp, 0xa);
        }
        cs->step++;
    } else {
        cs->csysp = DF_MNAMEE2_NewSystem(ioObjp, cs->player, cs->acid, cs->idx, cs->mode, 0);
        ioObjp->stat = 3;
        ioObjp->play_func = MNAME_PLAY(__PlayNameEditDispatch);
    }
}

/* one action per frame: start = key guide, circle = cancel, up / down = toggle the row, cross = open the keyboard */
static void __PlayNameEditSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x130);
    PS2_STACK(char[256], buff, 0x30);
    if (MenuKeyGuide_IsShown()) return;
    struct struct_2d84b07b *pad = &MPad[MPadUser];
    struct_ea2d5f43 *cs = MNAME_OLD(ioObjp);
    if (pad->start == 1) {
        MenuKeyGuide_Show(0x14);
    } else if (pad->Rrt == 1) {
        Voice_Call(0x3eb, 0x7f);
        cs->func = 0;
        __SetNameEditSystemFadeOut(ioObjp);
    } else if (pad->repeat.up || pad->repeat.dw) {
        Voice_Call(0x3e9, 0x7f);
        cs->idx = 1 - cs->idx;
        pf_sprintf_d(buff, "parts10_%d", cs->idx);
        WindowEx_MoveToLayout(cs->pCursor, (char *)LAYOUT_GetLayoutData(0x3fc), buff, 0);
        DF_M1HELP_ChangeString(DF_MSTR_GetName(MenuMng.language, 0, cs->idx == 0 ? 0x18e7a : cs->acid + 0x18e7b));
    } else if (pad->Rdw == 1) {
        Voice_Call(0x3ea, 0x7f);
        DF_MVIEW_MotionStart(MViewp, 0x32);
        cs->func = 1;
        __SetNameEditSystemFadeOut(ioObjp);
    }
}

/* steps 0 / 1 wait a frame each, step 2 builds the screen once the camera stops, step 3 waits 7 frames */
static void __PlayNameEditSystemFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x480);
    PS2_STACK(char[256], buff_win, 0x70);
    PS2_STACK(char[256], buff_name, 0x170);
    PS2_STACK(char[256], buff_label, 0x270);
    PS2_STACK(char[256], buff_wnd, 0x370);
    PS2_STACK(struct struct_76b810ec, str_col, 0x478);
    PS2_STACK(struct struct_76b810ec, tag_col, 0x47c);
    struct_ea2d5f43 *cs = MNAME_OLD(ioObjp);
    if (cs->step < 3) ioObjp->count++;
    switch (cs->step) {
    case 0:
    case 1:
        cs->step++;
        break;
    case 2: {
        if (!DF_MVIEW_MotionSync(MViewp)) break;
        const unsigned int tagc = 0x8089bbda, strc = 0x80909090;   /* gp-0x71e0 / gp-0x71dc */
        memcpy(&tag_col, &tagc, 4);
        memcpy(&str_col, &strc, 4);
        for (int i = 0; i < 2; i++) {
            pf_sprintf_d(buff_wnd, "name_%d", i);
            cs->apNameWnd[i] = UnitSprite_New(ioObjp, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x3fc), buff_wnd);
            UnitSprite_SetFadeIn(cs->apNameWnd[i]);
        }
        for (int i = 0; i < 2; i++) {
            int id = i == 0 ? 0x18e73 : (signed char)cs->acid + 0x18e70;
            pf_sprintf_d(buff_label, "parts20d_%d", i);
            char *layout = (char *)LAYOUT_GetLayoutData(0x3fc);
            PS2_STACK(unsigned long long, arg9, 0);   /* the 9th argument (flag) on the PS2 stack */
            arg9 = 0;
            (void)arg9;
            cs->apStrLabel[i] = DF_MOBJ_NewStringFromLayoutName(ioObjp, 0, id, 0, layout, buff_label, &tag_col, 0, 0);
            DF_MOBJ_SetFadeIn(cs->apStrLabel[i]);
        }
        for (int i = 0; i < 2; i++) {
            pf_sprintf_d(buff_name, "parts20f_%d", i);
            cs->apStrName[i] = DF_MOBJ_NewStringBaseFromLayoutName(ioObjp, (char *)LAYOUT_GetLayoutData(0x3fc), buff_name,
                                                                   &str_col, 0, 0);
            if (i == 0) DF_MOBJ_SetStringCopy(cs->apStrName[0], (char *)&g_gamedata[cs->player]);
            else DF_MOBJ_SetStringCopy(cs->apStrName[i], PlayerInfo_GetAcName(cs->player, (char)cs->acid));
            DF_MOBJ_SetFadeIn(cs->apStrName[i]);
        }
        pf_sprintf_d(buff_win, "parts10_%d", cs->idx);
        cs->pCursor = WindowEx_NewFromLayout(ioObjp, (_WINDOWEX_TYPE)0xb, (char *)LAYOUT_GetLayoutData(0x3fc), buff_win);
        WindowEx_BlinkBegin(cs->pCursor);
        DF_M1HELP_SetString(DF_MSTR_GetName(MenuMng.language, 0, cs->idx == 0 ? 0x18e7a : cs->acid + 0x18e7b));
        DF_M1HELP_FadeIn();
        DF_MBACK_PushSceneLabel((enum_5124d09c)0x2a);
        ioObjp->count = 0;
        cs->step++;
        break;
    }
    case 3:
        if (++ioObjp->count < 7) break;
        ioObjp->play_func = MNAME_PLAY(__PlayNameEditSystem);
        ioObjp->stat = 0;
        break;
    }
}

/* the chooser for player inPlayer (< 0: the AI AC, mode 1): hides the garage AC's hand models, camera motion 0x21 */
struct stMobj *DF_MNAMEE_NewSystem(struct stMobj *inObjp, int inPlayer)
{
    PS2_FRAME(0x40);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    obj->type = 0;
    obj->stat = 1;
    struct_ea2d5f43 *cs = MNAME_OLD(obj);
    if (inPlayer < 0) {
        cs->mode = 1;
        cs->player = 0;
        cs->acid = 0;
    } else {
        cs->mode = 0;
        cs->player = inPlayer;
        cs->acid = M_ACp[cs->player][0]->num;
    }
    cs->mstr_top_idx = 0x18e70 + cs->mode * 2000;
    mname_tag(0x10);
    if (cs->mode == 0) {
        DF_M_AC_SetFadeOut(M_ACp[cs->player][1], 5, 0.0f);
        DF_M_AC_SetFadeOut(M_ACp[cs->player][2], 5, 0.0f);
    }
    mname_guide(0, 1, 2, 0, 0x63);
    DF_MVIEW_MotionStart(MViewp, 0x21);
    obj->play_func = MNAME_PLAY(__PlayNameEditSystemFadeIn);
    obj->count = 0;
    return obj;
}

/* ---- the keyboard build ---- */

/* key tops (SJIS, entry = hi, lo, 0, 0; 0 = the END key): page 0 (OLMENU 0x1edf230, 0x1fd bytes copied) and page 1
 * (0x1edf430, 0x11d bytes) */
static const unsigned short mname_keys0[127] = {
    0x8250, 0x8251, 0x8252, 0x8253, 0x8254, 0x8255, 0x8256, 0x8257, 0x8258, 0x824f, 0x8754, 0x8755, 0x8756, 0x8757, 0x8758, 0x8759,
    0x875a, 0x875b, 0x8140, 0x875d, 0x8260, 0x8261, 0x8262, 0x8263, 0x8264, 0x8265, 0x8266, 0x8267, 0x8268, 0x8269, 0x826a, 0x826b,
    0x826c, 0x826d, 0x826e, 0x826f, 0x8270, 0x8271, 0x8272, 0x8273, 0x8274, 0x8275, 0x8276, 0x8279, 0x8140, 0x8140, 0x8281, 0x8282,
    0x8283, 0x8286, 0x8287, 0x8288, 0x828b, 0x828c, 0x828d, 0x8290, 0x8291, 0x8292, 0x8295, 0x8296, 0x8297, 0x829a, 0x8140, 0x8140,
    0x8277, 0x8278, 0x8140, 0x8140, 0x8284, 0x8285, 0x8289, 0x828a, 0x828e, 0x828f, 0x8293, 0x8294, 0x8298, 0x8299, 0x0000, 0x839f,
    0x83a0, 0x83a1, 0x83a2, 0x83a3, 0x83a4, 0x83a5, 0x83a6, 0x83a7, 0x83a8, 0x83a9, 0x83aa, 0x83ab, 0x83ac, 0x83ad, 0x83ae, 0x83af,
    0x83b0, 0x83b1, 0x83b2, 0x83b3, 0x83b4, 0x83b5, 0x83b6, 0x83bf, 0x83c0, 0x83c1, 0x83c2, 0x83c3, 0x83c4, 0x83c5, 0x83c6, 0x83c7,
    0x83c8, 0x83c9, 0x83ca, 0x83cb, 0x83cc, 0x83cd, 0x83ce, 0x83cf, 0x83d0, 0x83d1, 0x83d2, 0x83d3, 0x83d4, 0x83d5, 0x83d6,
};
static const unsigned short mname_keys1[71] = {
    0x8148, 0x8149, 0x8195, 0x8145, 0x8144, 0x8143, 0x8151, 0x815e, 0x815f, 0x8146, 0x8169, 0x816a, 0x8175, 0x8176, 0x816f, 0x8170,
    0x8165, 0x8166, 0x8167, 0x8168, 0x817b, 0x817c, 0x817d, 0x817e, 0x8180, 0x8181, 0x8182, 0x8183, 0x8184, 0x8185, 0x8187, 0x8188,
    0x8189, 0x818a, 0x8196, 0x8197, 0x8198, 0x81a6, 0x81a7, 0x81cd, 0x81aa, 0x81ab, 0x81a9, 0x81a8, 0x819b, 0x817e, 0x81a0, 0x81a2,
    0x819e, 0x81a4, 0x818f, 0x8190, 0x8191, 0x8192, 0x818e, 0x818b, 0x8193, 0x818c, 0x818d, 0x8140, 0x8147, 0x8163, 0x8140, 0x8140,
    0x8186, 0x81e3, 0x8194, 0x81f3, 0x8199, 0x81f4, 0x0000,
};

/* memset 0x200 + memcpy of the table (whole entries + one zero byte: the rest stays zero) */
static void mname_set_keys(unsigned char *dst, const unsigned short *keys, int n)
{
    memset(dst, 0, 0x200);
    for (int i = 0; i < n; i++) {
        dst[i * 4] = keys[i] >> 8;
        dst[i * 4 + 1] = keys[i] & 0xff;
    }
}

/* key block layout per page (OLMENU 0x1edf0a0): {cols, rows, x, y}; {1, 1} = the END key (two cells wide) */
static const short mname_blocks[6][5][4] = {
    {{5, 8, 54, 96}, {3, 8, 226, 96}, {2, 7, 322, 96}, {1, 1, 322, 320}, {6, 8, 398, 96}},
    {{10, 6, 126, 104}, {2, 5, 446, 104}, {1, 1, 446, 264}, {0, 0, 0, 0}, {0, 0, 0, 0}},
};

static inline void mname_word(void *dst, unsigned int v)
{
    memcpy(dst, &v, 4);
}

/* page-5 scroll arrow: create it (blink sprite, alpha 0x48) when wanted, else fade it out while it is idle / fading in */
static void mname_arrow(struct stMobj *ioObjp, struct stMobj **arrow, int want, struct stMenuRect *wnd, int *uv,
                        struct struct_76b810ec *col, const struct stMenuRect &wnd_v, const struct stMenuRect &uv_v)
{
    if (want) {
        if (*arrow) return;
        *wnd = wnd_v;
        mname_word(col, 0x80808080);   /* gp-0x71cc / gp-0x71c8 */
        memcpy(uv, &uv_v, 16);
        *arrow = DF_MOBJ2_NewBlinkSpriteNX(ioObjp, 0x3ec, wnd, uv, col, 0, 0);
        DF_MOBJ2_SetBlinkSpriteAlphaReg(*arrow, 0x48);
        DF_MOBJ2_SetBlinkSpriteFadeIn(*arrow);
    } else if (*arrow && ((*arrow)->stat == 1 || (*arrow)->stat == 0)) {
        DF_MOBJ2_SetBlinkSpriteFadeOut(*arrow);
    }
}

/* builds the key tag windows of the current page: charSelTable maps the 16 x 8 grid to key numbers, the END key
 * gets the "END" sprite over two cells. Pages 2-5 have no blocks; page 5 (row scroller) is unreachable */
static struct stMobj *__NameEditChangePage(struct stMobj *ioObjp)
{
    PS2_FRAME(0x2b0);
    PS2_STACK(unsigned long long, arg9, 0);   /* NXNewSprite's 9th argument slot */
    (void)arg9;
    PS2_STACK(int[4], end_uv, 0x140);
    PS2_STACK(struct stMenuRect, end_rect, 0x150);
    PS2_STACK(struct stMenuRect, win_rect, 0x160);
    PS2_STACK(int[4], dn_uv, 0x170);
    PS2_STACK(struct stMenuRect, dn_wnd, 0x180);
    PS2_STACK(int[4], up_uv, 0x190);
    PS2_STACK(struct stMenuRect, up_wnd, 0x1a0);
    PS2_STACK(short[6][5][4], charDirTable, 0x1b0);
    PS2_STACK(struct struct_76b810ec, end_col, 0x2a0);
    PS2_STACK(struct struct_76b810ec, tag_col, 0x2a4);
    PS2_STACK(struct struct_76b810ec, dn_col, 0x2a8);
    PS2_STACK(struct struct_76b810ec, up_col, 0x2ac);
    struct_f0eacc3b *cs = MNAME_NEW(ioObjp);
    memcpy(charDirTable, mname_blocks, sizeof(mname_blocks));
    cs->keymax = 0;
    int mcy = 0;
    memset(cs->charSelTable, 0xff, 0x100);
    switch (cs->page) {
    case 0:
        mname_set_keys(cs->keyTopTable, mname_keys0, 127);
        cs->keyTopTable[0x48] = 0x87;   /* key 18 becomes SJIS 0x875c */
        cs->keyTopTable[0x49] = 0x5c;
        break;
    case 1:
        mname_set_keys(cs->keyTopTable, mname_keys1, 71);
        break;
    case 5: {   /* dead: k2num takes the caller's $s0 */
        cs->k2num = (int)PS2_STALE_GPR(16);
        static const struct stMenuRect up_r = {34, 96, 32, 32}, up_t = {184, 0, 216, 32};
        static const struct stMenuRect dn_r = {34, 336, 32, 32}, dn_t = {216, 32, 184, 0};
        mname_arrow(ioObjp, &cs->scrool_up, cs->k2hd != 0, &up_wnd, up_uv, &up_col, up_r, up_t);
        mname_arrow(ioObjp, &cs->scrool_down, cs->k2hd < (cs->k2num + 15) / 16 - 8, &dn_wnd, dn_uv, &dn_col, dn_r, dn_t);
        break;
    }
    default:
        memset(cs->keyTopTable, 0, 0x200);
        break;
    }
    int ay = 0, ax = 0, bx = 0;
    cs->end_y = -1;
    cs->end_x = -1;
    for (int ii = 0; ii < 5; ii++) {
        int cols = charDirTable[(int)cs->page][ii][0];
        int rows = charDirTable[(int)cs->page][ii][1];
        int endf = 0;
        if (cols == 1 && rows == 1) {
            endf = 1;
            cs->end_x = ax - 1;
            cs->end_y = ay - 1;
        } else if (cols == 0x63 && rows == 0x63) {   /* no table row has it */
            cols = 1;
            endf = 2;
            rows = 1;
        }
        if (mcy < rows - 1) mcy = rows - 1;
        ax += cols;
        ay = mcy + 1;
        win_rect.x = charDirTable[(int)cs->page][ii][2];
        win_rect.y = charDirTable[(int)cs->page][ii][3];
        win_rect.w = endf ? 0x3a : 0x1a;
        win_rect.h = 0x1a;
        if (cs->page == 5) {
            int n = (cs->k2num + 15) / 16;
            if (n < rows) rows = n;
        }
        struct stMobj *prev = ioObjp;
        int s7 = 0;
        while (rows--) {
            int p5 = cs->page == 5;
            int cx = 0;
            while (cols--) {
                mname_word(&tag_col, 0x80909090);   /* gp-0x71c4 */
                if (!(p5 && cs->keyTopTable[cs->keymax * 4] == 0xa1)) {
                    cs->kylistp[cs->keymax] =
                        DF_MOBJ2_NewTagWindow(prev, &win_rect, endf ? 4 : 6, 4, 4, 0, 0x18e79, 3, &tag_col, 1, 0);
                    prev = cs->kylistp[cs->keymax];
                    DF_MOBJ2_SetTagWindowFadeIn(prev);
                    DF_MOBJ2_SetTagWindowStr(cs->kylistp[cs->keymax], cs->keyTopTable + cs->keymax * 4);
                }
                if (endf == 1) {
                    if (!cs->kendp) {
                        end_rect = {win_rect.x + 0xc, win_rect.y + 6, 36, 16};
                        mname_word(&end_col, 0x80909090);   /* gp-0x71c0 */
                        end_col.a = 0x5a;
                        end_uv[0] = 208;
                        end_uv[1] = 84;
                        end_uv[2] = 256;
                        end_uv[3] = 100;
                        arg9 = 0;
                        cs->kendp = DF_MOBJ_NXNewSprite(ioObjp, 0x3ec, &end_rect, end_uv, &end_col, 1, 0, 0, 0);
                        cs->kendp->dt.sprite.reg_alpha = 0x48;
                    }
                    cs->kendp->dt.sprite.rect.x = win_rect.x + 0xc;
                    cs->kendp->dt.sprite.rect.y = win_rect.y + 6;
                    DF_MOBJ_SetFadeIn(cs->kendp);
                    cs->keyTopTable[cs->keymax * 4] = 0;
                    cs->keyTopTable[cs->keymax * 4 + 1] = 0;
                    cs->charSelTable[bx + cx - 1 + mcy * 16] = cs->keymax;
                    cs->charSelTable[bx + cx - 2 + mcy * 16] = cs->keymax;
                } else if (endf == 2) {   /* dead */
                    cs->keyTopTable[cs->keymax * 4] = 1;
                    cs->keyTopTable[cs->keymax * 4 + 1] = 1;
                    cs->charSelTable[bx + cx - 1 + (mcy - 1) * 16] = cs->keymax;
                    cs->charSelTable[bx + cx - 2 + (mcy - 1) * 16] = cs->keymax;
                } else {
                    cs->charSelTable[bx + cx + s7] = cs->keymax;
                }
                cs->keymax++;
                if (endf == 0) cx++;
                win_rect.x += 0x20;
            }
            s7 += 16;
            win_rect.y += 0x20;
            cols = charDirTable[(int)cs->page][ii][0];
            win_rect.x = charDirTable[(int)cs->page][ii][2];
        }
        if (endf == 0) bx += charDirTable[(int)cs->page][ii][0];
    }
    DF_MOBJ2_SelectTagWindow(cs->kylistp[cs->charSelTable[cs->cury * 16 + cs->curx]], 1);
    cs->memcur = 0;
    return 0;
}

/* page switch: 0 fade the keys / arrows / END sprite out, 1 delete them once key 0 or 1 is gone, 2 build the new
 * page, 3 back to the input loop once key 0 or 1 is idle */
static void __PageFadeInOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct_f0eacc3b *cs = MNAME_NEW(ioObjp);
    struct stMobj **k = cs->kylistp;
    switch (cs->step) {
    case 0:
        for (int i = 0; i < cs->keymax; i++)
            if (cs->kylistp[i]) DF_MOBJ2_SetTagWindowFadeOut(cs->kylistp[i]);
        if (cs->scrool_up && cs->scrool_up->stat != 5) DF_MOBJ2_SetBlinkSpriteFadeOut(cs->scrool_up);
        if (cs->scrool_down && cs->scrool_down->stat != 5) DF_MOBJ2_SetBlinkSpriteFadeOut(cs->scrool_down);
        if (cs->kendp) DF_MOBJ_SetFadeOut(cs->kendp);
        cs->step++;
        break;
    case 1:
        if (!((k[0] && k[0]->stat == 5) || (k[1] && k[1]->stat == 5))) break;
        for (int i = 0; i < cs->keymax; i++) {
            if (cs->kylistp[i]) {
                DF_MOBJ2_DeleteTagWindow(cs->kylistp[i]);
                cs->kylistp[i] = 0;
            }
        }
        if (cs->scrool_up) {
            mname_erase(cs->scrool_up);
            cs->scrool_up = 0;
        }
        if (cs->scrool_down) {
            mname_erase(cs->scrool_down);
            cs->scrool_down = 0;
        }
        if (cs->kendp) {
            mname_erase(cs->kendp);
            cs->kendp = 0;
        }
        ioObjp->draw_func = cs->page == 5 ? MNAME_PLAY(__DrawNameEditSystem) : 0;
        cs->step++;
        break;
    case 2:
        __NameEditChangePage(ioObjp);
        cs->step++;
        break;
    case 3:
        if ((k[0] && k[0]->stat == 0) || (k[1] && k[1]->stat == 0)) {
            ioObjp->play_func = MNAME_PLAY(__PlayNameEditSystem2);
            ioObjp->stat = 0;
        }
        break;
    }
}

/* steps 0 / 1 wait a frame each, step 2 builds the screen (name field, cursor, title, page labels, keys), step 3
 * waits for the name frame to finish fading in; mode 3 (first-run player name) opens its intro dialog first */
static void __PlayNameEditSystemFadeIn2(struct stMobj *ioObjp)
{
    PS2_FRAME(0xd0);
    PS2_STACK(unsigned long long, arg9, 0);
    PS2_STACK(unsigned long long, arg10, 8);
    (void)arg9;   /* NewString's depth / flag slots */
    (void)arg10;
    PS2_STACK(int[4], curiv, 0x80);
    PS2_STACK(struct stMenuRect, mode_select, 0x90);
    PS2_STACK(struct stMenuRect, win_rect2, 0xa0);
    PS2_STACK(struct stMenuRect, win_rect, 0xb0);
    PS2_STACK(struct struct_76b810ec, str_col, 0xc4);
    PS2_STACK(struct struct_76b810ec, cur_col, 0xc8);
    PS2_STACK(struct struct_76b810ec, tag_col, 0xcc);
    struct_f0eacc3b *cs = MNAME_NEW(ioObjp);
    if (cs->step < 3) ioObjp->count++;
    switch (cs->step) {
    case 0:
    case 1:
        cs->step++;
        break;
    case 2: {
        win_rect = {213, 60, 264, 40};
        win_rect2 = {183, 361, 204, 28};
        mode_select = {159, 359, 128, 21};
        mname_word(&tag_col, 0x8089bbda);   /* gp-0x71d8 */
        mname_word(&cur_col, 0x60808080);   /* gp-0x71d4 */
        mname_word(&str_col, 0x80909090);   /* gp-0x71d0 */
        curiv[0] = 68;
        curiv[1] = 56;
        curiv[2] = 100;
        curiv[3] = 88;
        /* mode 0 or 3 ($v1 still holds the 3 of the step dispatch) */
        if (cs->mode == 0 || cs->mode == 3) DF_MBACK_PushSceneLabel((enum_5124d09c)(cs->type ? 0x2c : 0x2b));
        cs->namebase = UnitSprite_New(ioObjp, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x3fd), mname_name0);
        UnitSprite_SetFadeIn(cs->namebase);
        cs->modebase = UnitSprite_New(ioObjp, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x3fd), mname_shita0);
        UnitSprite_SetFadeIn(cs->modebase);
        cs->modeselect = WindowEx_New(cs->modebase, (_WINDOWEX_TYPE)0x24, &mode_select, &cur_col);
        WindowEx_SetAlphaReg(cs->modeselect, 0x48);
        WindowEx_SetFadeIn(cs->modeselect);
        struct stMobj *sel = cs->modeselect;
        cs->edit_string = DF_MOBJ_NewStringDirect(sel, mname_empty_str, 3, win_rect.x, win_rect.y, &str_col, 1, 0);
        DF_MOBJ_SetStringCopy(cs->edit_string, (char *)cs->tmpName);
        int c = cs->ncur < 12 ? cs->ncur : 11;
        win_rect = {18 * c + 0xd5, 0x3a, 0x16, 0x16};
        cs->ncurp = DF_MOBJ2_NewBlinkSpriteNX(sel, 0x3ec, &win_rect, curiv, &cur_col, 0, 0);
        cs->ncurp->dt.sprite.reg_alpha = 0x48;   /* stored directly (SetBlinkSpriteAlphaReg inlined) */
        DF_MOBJ2_SetBlinkSpriteFadeIn(cs->ncurp);
        int id = 0;
        switch (cs->mode) {
        case 0:
        case 1:
            id = (cs->type == 0 ? 0x18e73 : cs->acid + 0x18e70) + cs->mode * 2000;
            break;
        case 2:
            id = 0x9f6;
            break;
        case 3:
            id = cs->type == 0 ? 0x18e73 : cs->acid + 0x18e70;
            break;
        case 4:
            id = 0x19a28;
            break;
        }
        arg9 = 0;
        arg10 = 0;
        cs->nstrp = DF_MOBJ_NewString(cs->ncurp, 0, id, 0, 0, 200, 44, &tag_col, 0, 0);
        struct stMobj *parent = cs->nstrp;
        DF_MOBJ_SetFadeIn(parent);
        for (int i = 0; i < 2; i++) {
            arg9 = 0;
            arg10 = 0;
            cs->mode_list[i] = DF_MOBJ_NewString(parent, 0, 0x18e74 + i, 0, 7, win_rect2.x, win_rect2.y, &str_col, 0, 0);
            parent = cs->mode_list[i];
            DF_MOBJ_SetFadeIn(parent);
            win_rect2.x += 0xcd;
        }
        __NameEditChangePage(ioObjp);
        DF_M1HELP_SetString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x18e74 + cs->page));
        DF_M1HELP_FadeIn();
        cs->step++;
        break;
    }
    case 3:
        if (cs->namebase->stat != 0) break;
        if (cs->mode == 3) {
            DF_MDIALOG_Open(5, DF_MSTR_GetInfo(MenuMng.language, 1, 0x30d4c), 0, 0, 0, 0, 1, 0);
            mname_guide(0, 0xe, 0, 0, 0x63);
            ioObjp->play_func = MNAME_PLAY(__PlayNameEditDispatch2);
        } else {
            ioObjp->play_func = MNAME_PLAY(__PlayNameEditSystem2);
        }
        ioObjp->stat = 0;
        break;
    }
}

/* the keyboard for player inPlayer / AC inAcid: mode 0 garage name (type 0 player, 1 AC), 1 AI AC (type 1 AC name,
 * 0 AI name), 2 no source, 3 first-run player name, 4 free string from / to argp. Carves its key buffers from the
 * menu work area (given back in FadeOut2) */
struct stMobj *DF_MNAMEE2_NewSystem(struct stMobj *inObjp, int inPlayer, int inAcid, int inType, int inMode, void *argp)
{
    PS2_FRAME(0x80);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    obj->type = 0;
    obj->stat = 1;
    struct_f0eacc3b *cs = MNAME_NEW(obj);
    cs->player = inPlayer;
    cs->acid = inAcid;
    cs->type = inType;
    cs->mode = inMode;
    cs->dirty = 0;
    cs->enable = 0;
    cs->mstr_top_idx = 0x18abb;
    cs->pNameBuff = (char *)argp;
    memset(cs->tmpName, 0, 0x30);
    memset(cs->defName, 0, 0x30);
    if (cs->mode == 0 || cs->mode == 3) {
        if (cs->type) {
            memcpy(cs->tmpName, PlayerInfo_GetAcName(cs->player, cs->acid), 0x18);
            memcpy(cs->defName, PlayerInfo_GetAcName(cs->player, cs->acid), 0x18);
        } else {
            memcpy(cs->tmpName, &g_gamedata[cs->player], 0x18);
            memcpy(cs->defName, &g_gamedata[cs->player], 0x18);
        }
        mname_tag(0x11 + cs->type);
        if (cs->mode == 0) {
            DF_M_AC_SetFadeOut(M_ACp[cs->player][0], 5, 0.0f);
            DF_MBACK_SetSpotLightFadeOut();
        }
    } else if (cs->mode == 1) {
        if (cs->type) {
            memcpy(cs->tmpName, gAiAcData.name, 0x18);
            memcpy(cs->defName, gAiAcData.name, 0x18);
            mname_tag(0x1b);
        } else {
            memcpy(cs->tmpName, gAiAcData.ai_name, 0x18);
            memcpy(cs->defName, gAiAcData.ai_name, 0x18);
            mname_tag(0x1c);
        }
        DF_M_AC_SetFadeOut(gAiAcData.datp, 5, 0.0f);
    } else if (cs->mode == 2) {
        mname_tag(0x34);
    } else if (cs->mode == 4) {
        memcpy(cs->tmpName, cs->pNameBuff, 0x18);
        memcpy(cs->defName, cs->pNameBuff, 0x18);
        mname_tag(0x40);
    }
    cs->ncur = 12;
    for (int i = 0; i < 12; i++) {
        if (cs->tmpName[i * 2] == 0) {
            cs->ncur = i;
            break;
        }
    }
    mname_guide(6, 1, 2, 7, 0x63);
    cs->defTBP = MenuWorkp;
    cs->kylistp = (struct stMobj **)(((uintptr_t)MenuWorkp + 0xf) & ~(uintptr_t)0xf);
    cs->tplistp = (struct stMobj **)((unsigned char *)cs->kylistp + 0x200);
    cs->charSelTable = (short *)((unsigned char *)cs->tplistp + 0x40);
    cs->keyTopTable = (unsigned char *)cs->charSelTable + 0x100;
    cs->tKTT = cs->keyTopTable + 0x200;
    MenuWorkp = cs->tKTT + 0x40;
    memset(cs->kylistp, 0, 0x200);
    memset(cs->tplistp, 0, 0x40);
    memset(cs->charSelTable, 0, 0x100);
    memset(cs->keyTopTable, 0, 0x200);
    obj->play_func = MNAME_PLAY(__PlayNameEditSystemFadeIn2);
    obj->count = 0;
    return obj;
}

/* ---- the keyboard's exit path ---- */

/* confirm-dialog result (DF_MDIALOG_Sync: 4 closed, 0x14 yes, 0x24 no); mode 3 (first-run player name) cannot
 * decline: "no" goes back to editing, "yes" also sets MenuMng.stat = 2 */
static void __PlayNameEditDispatch2(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    int r = DF_MDIALOG_Sync();
    if (!(r & 4)) return;
    struct_f0eacc3b *cs = MNAME_NEW(ioObjp);
    if (cs->func == 0) {
        if (r == 4) {
            mname_guide(6, 1, 2, 7, 0x63);
            ioObjp->stat = 0;
            ioObjp->play_func = MNAME_PLAY(__PlayNameEditSystem2);
        } else if (r == 0x24) {
            if (cs->mode == 3) {
                mname_guide(6, 1, 2, 7, 0x63);
                ioObjp->stat = 0;
                ioObjp->play_func = MNAME_PLAY(__PlayNameEditSystem2);
            } else {
                cs->enable = 0;
                cs->func = 0;
                __SetNameEditSystemFadeOut2(ioObjp);
            }
        } else if (r == 0x14) {
            if (cs->mode == 3) {
                MenuMng.stat = 2;
                DF_MBGUIDE_Hide();
            }
            cs->enable = 1;
            cs->func = 0;
            __SetNameEditSystemFadeOut2(ioObjp);
        }
    } else if (r == 4 || r == 0x24 || r == 0x14) {   /* dead: func is 0 whenever the dialog is open */
        mname_guide(6, 1, 2, 7, 0x63);
        ioObjp->play_func = MNAME_PLAY(__PlayNameEditSystem2);
        ioObjp->stat = 0;
    }
}

/* fade every keyboard object out (mode_list / the UnitSprites / the WindowEx without null tests) */
static void __SetNameEditSystemFadeOut2(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct_f0eacc3b *cs = MNAME_NEW(ioObjp);
    if (cs->edit_string) DF_MOBJ_SetFadeOut(cs->edit_string);
    if (cs->ncurp) DF_MOBJ2_SetBlinkSpriteFadeOut(cs->ncurp);
    if (cs->nstrp) DF_MOBJ_SetFadeOut(cs->nstrp);
    if (cs->kendp) DF_MOBJ_SetFadeOut(cs->kendp);
    for (int i = 0; i < 2; i++) DF_MOBJ_SetFadeOut(cs->mode_list[i]);
    for (int i = 0; i < cs->keymax; i++)
        if (cs->kylistp[i]) DF_MOBJ2_SetTagWindowFadeOut(cs->kylistp[i]);
    DF_M1HELP_FadeOut();
    UnitSprite_SetFadeOut(cs->namebase);
    UnitSprite_SetFadeOut(cs->modebase);
    WindowEx_SetFadeOut(cs->modeselect);
    if (cs->mode != 3) DF_MVIEW_MotionStart(MViewp, 0x21);
    if (cs->func == 0) {
        if (cs->mode == 0) mname_tag(0x10);
        else if (cs->mode == 1) mname_tag(0x10);
        else if (cs->mode == 2) mname_tag(0x34);
        mname_guide(0, 1, 2, 0, 0x63);
    }
    ioObjp->stat = 2;
    ioObjp->play_func = MNAME_PLAY(__PlayNameEditSystemFadeOut2);
}

/* once the camera stopped and the name text has faded (edit_string->stat read before the null test): delete the
 * keyboard; with func 0 (always) save the name by mode when enable, then finish and give the menu work buffer back */
static void __PlayNameEditSystemFadeOut2(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct_f0eacc3b *cs = MNAME_NEW(ioObjp);
    if (!DF_MVIEW_MotionSync(MViewp)) return;
    if (cs->edit_string->stat != 5) return;
    if (cs->edit_string) mname_erase(cs->edit_string);
    cs->edit_string = 0;
    if (cs->ncurp) mname_erase(cs->ncurp);
    cs->ncurp = 0;
    if (cs->nstrp) mname_erase(cs->nstrp);
    cs->nstrp = 0;
    if (cs->kendp) mname_erase(cs->kendp);
    cs->kendp = 0;
    for (int i = 0; i < 2; i++) {
        if (cs->mode_list[i]) mname_erase(cs->mode_list[i]);
        cs->mode_list[i] = 0;
    }
    for (int i = 0; i < cs->keymax; i++) {
        if (cs->kylistp[i]) {
            DF_MOBJ2_DeleteTagWindow(cs->kylistp[i]);
            cs->kylistp[i] = 0;
        }
    }
    DF_MBACK_PopSceneLabel();
    UnitSprite_Delete(cs->namebase);
    UnitSprite_Delete(cs->modebase);
    WindowEx_Delete(cs->modeselect);
    if (cs->func != 0) return;
    switch (cs->mode) {
    case 0:
        if (cs->enable) {
            if (cs->type) PlayerInfo_SetAcName(cs->player, cs->acid, (char *)cs->tmpName);
            else memcpy(&g_gamedata[cs->player], cs->tmpName, 0x18);
        }
        DF_M_AC_SetFadeIn(M_ACp[cs->player][0], 0xf, 0.0f);
        DF_MBACK_SetSpotLightFadeIn();
        break;
    case 1:
        if (cs->enable) {
            if (cs->type) memcpy(gAiAcData.name, cs->tmpName, 0x18);
            else memcpy(gAiAcData.ai_name, cs->tmpName, 0x18);
        }
        DF_M_AC_SetFadeIn(gAiAcData.datp, 0xf, 0.0f);
        break;
    case 2:
        break;
    case 3:
        if (cs->enable) {
            if (cs->type) PlayerInfo_SetAcName(cs->player, cs->acid, (char *)cs->tmpName);
            else memcpy(&g_gamedata[cs->player], cs->tmpName, 0x30);   /* 0x30 here, 0x18 in mode 0 */
        }
        Bgm_FadeOutStart();
        break;
    case 4:
        if (cs->enable) memcpy(cs->pNameBuff, cs->tmpName, 0x20);
        break;
    }
    ioObjp->play_func = 0;
    ioObjp->stat = 5;
    mTmpHGMapp = 0;
    MenuWorkp = cs->defTBP;
}

/* ---- the input loop ---- */

/* confirm dialog for a changed name (the text depends on mode / type) */
static void mname_confirm(struct_f0eacc3b *cs)
{
    switch (cs->mode) {
    case 0:
        DF_MDIALOG_Open(2, DF_MSTR_GetInfo(MenuMng.language, 1, 0x30d43 + (cs->type ? cs->acid : 3)), 0, 0, 0, 0, 1, 0);
        break;
    case 1:
        DF_MDIALOG_Open(2, DF_MSTR_GetInfo(MenuMng.language, 1, 0x30d48 + (1 - cs->type)), 0, 0, 0, 0, 1, 0);
        break;
    case 2:
        DF_MDIALOG_Open(2, DF_MSTR_GetInfo(MenuMng.language, 1, 0x25b), 0, 0, 0, 0, 1, 0);
        break;
    case 3:   /* only from the END key */
        DF_MDIALOG_Open(2, DF_MSTR_GetInfo(MenuMng.language, 1, 0x30d4b), 0, 0, 0, 0, 1, 0);
        break;
    case 4:
        DF_MDIALOG_Open(2, DF_MSTR_GetInfo(MenuMng.language, 1, 0x310f6), 0, 0, 0, 0, 1, 0);
        break;
    }
}

/* dirty = tmpName differs from defName in the first n bytes */
static void mname_dirty(struct_f0eacc3b *cs, int n)
{
    cs->dirty = 0;
    for (int i = 0; i < n; i++) {
        if (cs->tmpName[i] != cs->defName[i]) {
            cs->dirty = 1;
            break;
        }
    }
}

static inline short mname_cell(struct_f0eacc3b *cs, int x, int y)
{
    return cs->charSelTable[y * 16 + x];
}

/* rightmost column 15..1 of row cury holding a key number > 0 (key 0 is never a landing cell) */
static void mname_scan_left(struct_f0eacc3b *cs)
{
    cs->curx = 0;
    for (int a = 1; a < 16; a++) {
        int c = 16 - a;
        if (mname_cell(cs, c, cs->cury) > 0) {
            cs->curx = c;
            break;
        }
    }
}

/* page 5 (row scroller, unreachable): rows of k2num cells */
static inline int mname_k2rows(struct_f0eacc3b *cs)
{
    return (cs->k2num + 15) / 16;
}

/* one frame of input: start = key guide, circle = leave (confirm dialog when changed), L1 / R1 = page, d-pad =
 * cursor, triangle = jump to END, cross = type the key / END, square = delete; then select the key under the cursor
 * or start the page switch. "Pressed" is counter == 1 for the face buttons, the repeat bits for L1 / R1 / d-pad */
static void __PlayNameEditSystem2(struct stMobj *ioObjp)
{
    PS2_FRAME(0x80);
    PS2_STACK(struct stMenuRect, move_r2, 0x60);
    PS2_STACK(struct stMenuRect, move_r, 0x70);
    struct_f0eacc3b *cs = MNAME_NEW(ioObjp);
    int pre_idx = cs->idx;
    int pre_page = cs->page;
    if (MenuKeyGuide_IsShown()) return;
    struct struct_2d84b07b *pad = &MPad[MPadUser];
    if (pad->start == 1) {
        MenuKeyGuide_Show(pre_page == 5 ? 0x41 : cs->mode == 0 ? 0x15 : 1);
        return;
    }
    if (pad->Rrt == 1) {
        if (cs->page == 5) {
            cs->page = 3;
            cs->memcur = 1;
            goto tail;
        }
        if (cs->mode == 3) {
            cs->enable = 0;
            cs->func = 0;
            __SetNameEditSystemFadeOut2(ioObjp);
            return;
        }
        mname_dirty(cs, 0x18);
        if (cs->dirty) {
            mname_guide(0, 1, 2, 0, 0x63);
            mname_confirm(cs);
            cs->func = 0;
            ioObjp->play_func = MNAME_PLAY(__PlayNameEditDispatch2);
            ioObjp->stat = 3;
            return;
        }
        Voice_Call(0x3eb, 0x7f);
        cs->enable = 0;
        cs->func = 0;
        __SetNameEditSystemFadeOut2(ioObjp);
        return;
    }
    if (pad->repeat.l1) {
        Voice_Call(0x3ea, 0x7f);
        if (cs->page == 5) cs->page = 3;
        cs->page--;
        if (cs->page < 0) cs->page = 1;
        goto tail;
    }
    if (pad->repeat.r1) {
        Voice_Call(0x3ea, 0x7f);
        if (cs->page == 5) cs->page = 3;
        cs->page++;
        if (cs->page >= 2) cs->page = 0;
        goto tail;
    }
    if (pad->repeat.rt) {
        short old = mname_cell(cs, cs->curx, cs->cury);
        Voice_Call(0x3e9, 0x7f);
        cs->curx++;
        if (cs->curx >= 16) cs->curx = 0;
        if (mname_cell(cs, cs->curx, cs->cury) < 0) cs->curx = 0;
        if (old == mname_cell(cs, cs->curx, cs->cury)) cs->curx++;   /* skip the rest of a wide key */
        if (mname_cell(cs, cs->curx, cs->cury) < 0) cs->curx = 0;
    }
    if (MPad[MPadUser].repeat.lt) {
        short old = mname_cell(cs, cs->curx, cs->cury);
        Voice_Call(0x3e9, 0x7f);
        cs->curx--;
        if (cs->curx < 0) mname_scan_left(cs);
        if (old == mname_cell(cs, cs->curx, cs->cury)) cs->curx--;
        if (cs->curx < 0) mname_scan_left(cs);
    }
    if (MPad[MPadUser].repeat.up) {
        Voice_Call(0x3e9, 0x7f);
        cs->cury--;
        if (cs->page == 5) {   /* dead: scroll the rows up */
            if (cs->cury == -1) {
                cs->k2hd--;
                if (cs->k2hd >= 0) {
                    DF_MOBJ2_SelectTagWindow(cs->kylistp[pre_idx], 0);
                    cs->cury = 0;
                    cs->step = 0;
                    cs->func = 0;
                    ioObjp->play_func = MNAME_PLAY(__K2PageScroll);
                    return;
                }
                cs->k2hd = mname_k2rows(cs) - 8;
                if (cs->k2hd < 0) cs->k2hd = 0;
                if (cs->k2hd != 0) {
                    DF_MOBJ2_SelectTagWindow(cs->kylistp[pre_idx], 0);
                    cs->cury = 7;
                    cs->idx = mname_cell(cs, cs->curx, cs->cury);
                    cs->step = 0;
                    cs->func = 0;
                    ioObjp->play_func = MNAME_PLAY(__PageFadeInOut);
                    return;
                }
                cs->cury = mname_k2rows(cs) - 1;
            }
        } else if (cs->cury < 0) {
            cs->cury = 0;
            for (int a = 1; a < 8; a++) {
                int r = 8 - a;
                if (mname_cell(cs, cs->curx, r) > 0) {
                    cs->cury = r;
                    break;
                }
            }
        }
    }
    if (MPad[MPadUser].repeat.dw) {
        Voice_Call(0x3e9, 0x7f);
        cs->cury++;
        if (cs->page == 5) {   /* dead: scroll the rows down */
            int n = mname_k2rows(cs);
            if (n >= 9) n = 8;
            if (cs->cury >= n) {
                cs->k2hd++;
                int rows = mname_k2rows(cs);
                if (rows - 8 < cs->k2hd || rows < 8) {
                    cs->k2hd = 0;
                    cs->cury = 0;
                    if (mname_k2rows(cs) >= 9) {
                        DF_MOBJ2_SelectTagWindow(cs->kylistp[pre_idx], 0);
                        cs->idx = mname_cell(cs, cs->curx, cs->cury);
                        cs->step = 0;
                        cs->func = 1;
                        ioObjp->play_func = MNAME_PLAY(__PageFadeInOut);
                        return;
                    }
                } else {
                    DF_MOBJ2_SelectTagWindow(cs->kylistp[pre_idx], 0);
                    cs->cury = 7;
                    cs->step = 0;
                    cs->func = 1;
                    ioObjp->play_func = MNAME_PLAY(__K2PageScroll);
                    return;
                }
            }
        } else {
            if (cs->cury >= 8) cs->cury = 0;
            if (mname_cell(cs, cs->curx, cs->cury) < 0) cs->cury = 0;
        }
    }
    if (MPad[MPadUser].Rup == 1 && cs->end_x != -1 && cs->end_y != -1) {
        Voice_Call(0x3e9, 0x7f);
        cs->curx = cs->end_x;
        cs->cury = cs->end_y;
    }
    if (MPad[MPadUser].Rdw == 1) {
        int key = mname_cell(cs, cs->curx, cs->cury) * 4;
        if (cs->keyTopTable[key] == 0) {   /* END */
            Voice_Call(0x3ea, 0x7f);
            if (cs->mode == 3) {
                DF_MDIALOG_Open(2, DF_MSTR_GetInfo(MenuMng.language, 1, 0x30d4b), 0, 0, 0, 0, 1, 0);
                cs->func = 0;
                ioObjp->play_func = MNAME_PLAY(__PlayNameEditDispatch2);
                ioObjp->stat = 3;
                return;
            }
            mname_dirty(cs, 0x30);
            if (!cs->dirty) {
                cs->enable = 0;
                cs->func = 0;
                __SetNameEditSystemFadeOut2(ioObjp);
                return;
            }
            mname_guide(0, 1, 2, 0, 0x63);
            mname_confirm(cs);
            cs->func = 0;
            ioObjp->play_func = MNAME_PLAY(__PlayNameEditDispatch2);
            ioObjp->stat = 3;
            return;
        }
        if (cs->page == 3) {   /* dead: the space key types, any other key opens page 5 */
            Voice_Call(0x3ea, 0x7f);
            if (cs->keyTopTable[key] == 0x81 && cs->keyTopTable[key + 1] == 0x40) {
                memcpy(cs->tmpName + cs->ncur * 2, cs->keyTopTable + key, 2);   /* no ncur < 12 test */
                DF_MOBJ_SetStringCopy(cs->edit_string, (char *)cs->tmpName);
                if (cs->ncur < 12) {
                    Voice_Call(0x3ea, 0x7f);
                    cs->ncur++;
                }
                DF_MOBJ2_SetBlinkSpritePos(cs->ncurp, 18 * (cs->ncur < 12 ? cs->ncur : 11) + 0xd3, 0x3a);
                cs->dirty = 1;
            } else {
                Voice_Call(0x3ea, 0x7f);
                cs->page = 5;
                cs->bcurx = cs->curx;
                cs->bcury = cs->cury;
                cs->bidx = mname_cell(cs, cs->curx, cs->cury);
            }
        } else if (cs->ncur < 12) {
            Voice_Call(0x3ea, 0x7f);
            memcpy(cs->tmpName + cs->ncur * 2, cs->keyTopTable + key, 2);
            DF_MOBJ_SetStringCopy(cs->edit_string, (char *)cs->tmpName);
            cs->ncur++;
            DF_MOBJ2_SetBlinkSpritePos(cs->ncurp, 18 * (cs->ncur < 12 ? cs->ncur : 11) + 0xd3, 0x3a);
            cs->dirty = 1;
        }
    }
    if (MPad[MPadUser].Rlt == 1) {
        if (cs->ncur > 0 && cs->ncur < 12) {
            Voice_Call(0x3e9, 0x7f);
            cs->tmpName[cs->ncur * 2 - 2] = 0;
            cs->tmpName[cs->ncur * 2 - 1] = 0;
            cs->ncur--;
            DF_MOBJ_SetStringCopy(cs->edit_string, (char *)cs->tmpName);
            DF_MOBJ2_SetBlinkSpritePos(cs->ncurp, cs->ncur * 18 + 0xd3, 0x3a);
            cs->dirty = 1;
        } else if (cs->ncur == 12) {   /* the cursor sprite stays */
            Voice_Call(0x3e9, 0x7f);
            cs->tmpName[cs->ncur * 2 - 2] = 0;
            cs->tmpName[cs->ncur * 2 - 1] = 0;
            cs->ncur--;
            DF_MOBJ_SetStringCopy(cs->edit_string, (char *)cs->tmpName);
            cs->dirty = 1;
        }
    }
tail:
    if (cs->page == pre_page) {
        cs->idx = mname_cell(cs, cs->curx, cs->cury);
        if (cs->idx != pre_idx) {
            DF_MOBJ2_SelectTagWindow(cs->kylistp[pre_idx], 0);
            DF_MOBJ2_SelectTagWindow(cs->kylistp[cs->idx], 1);
        }
        return;
    }
    /* page label cursor (OLMENU 0x1edf080 / 0x1edf090, both {159, 359, 128, 21}) */
    if (pre_page == 5) {
        move_r = {159, 359, 128, 21};
        move_r.x += cs->page * 192;
        WindowEx_Move(cs->modeselect, &move_r, 0);
    } else if (cs->page != 5) {
        move_r2 = {159, 359, 128, 21};
        move_r2.x += cs->page * 192;
        WindowEx_Move(cs->modeselect, &move_r2, 0);
    }
    cs->step = 0;
    cs->k2hd = 0;
    if (cs->memcur) {
        cs->curx = cs->bcurx;
        cs->cury = cs->bcury;
        cs->idx = cs->bidx;
    } else {
        cs->curx = 0;
        cs->cury = 0;
        cs->idx = 0;
    }
    if (!(pre_page == 5 && cs->page == 3) && cs->page != 5)
        DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x18e74 + cs->page));
    cs->pre_page = pre_page;
    ioObjp->play_func = MNAME_PLAY(__PageFadeInOut);
}

/* page 5 row scroller (unreachable: page 5 is never entered): 0 build the incoming row of 16 keys in tplistp and fade
 * the leaving row out, 1 slide all rows 6 px per frame for 6 frames, 2 delete the leaving row, shift kylistp by a row,
 * re-place / re-label every key and the scroll arrows, 3 back to the input loop once key 0 or 1 is idle.
 * func 0 = scroll up (new row on top), 1 = down */
static void __K2PageScroll(struct stMobj *ioObjp)
{
    PS2_FRAME(0x100);
    PS2_STACK(int[4], dn_uv, 0xa0);
    PS2_STACK(struct stMenuRect, dn_wnd, 0xb0);
    PS2_STACK(int[4], up_uv, 0xc0);
    PS2_STACK(struct stMenuRect, up_wnd, 0xd0);
    PS2_STACK(struct stMenuRect, win_rect, 0xe0);
    PS2_STACK(struct struct_76b810ec, dn_col, 0xf4);
    PS2_STACK(struct struct_76b810ec, up_col, 0xf8);
    PS2_STACK(struct struct_76b810ec, tag_col, 0xfc);
    struct_f0eacc3b *cs = MNAME_NEW(ioObjp);
    struct stMobj **k = cs->kylistp;
    switch (cs->step) {
    case 0: {
        win_rect.x = 0x42;
        win_rect.y = cs->func == 0 ? 0x60 : 0x140;
        win_rect.w = 0x1c;
        win_rect.h = 0x1c;
        cs->swork[2] = win_rect.x;
        cs->swork[3] = win_rect.y;
        struct stMobj *prev = ioObjp;
        for (int ii = 0; ii < 16; ii++) {
            mname_word(&tag_col, 0x80909090);   /* gp-0x71bc */
            if (!(cs->page == 5 && ii == 0 && cs->keyTopTable[cs->keymax * 4] == 0xa1)) {
                cs->tplistp[ii] = DF_MOBJ2_NewTagWindow(prev, &win_rect, 6, 4, 4, 0, 0x18e79, 3, &tag_col, 1, cs->curx == ii);
                prev = cs->tplistp[ii];
                DF_MOBJ2_SetTagWindowFadeIn(prev);
                DF_MOBJ2_SetTagWindowStr(cs->tplistp[ii], cs->tKTT + ii * 4);
            }
            win_rect.x += 0x20;
            DF_MOBJ2_SetTagWindowFadeOut(cs->kylistp[(cs->func == 0 ? 7 : 0) * 16 + ii]);
        }
        cs->swork[0] = 0x42;
        cs->swork[1] = 0x60;
        cs->ct = 0;
        cs->step++;
        break;
    }
    case 1:
        win_rect.x = cs->swork[0];
        win_rect.y = cs->swork[1];
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 16; col++) {
                if (k[row * 16 + col]) DF_MOBJ2_SetTagWindowPos(k[row * 16 + col], win_rect.x, win_rect.y);
                win_rect.x += 0x20;
            }
            win_rect.x = cs->swork[0];
            win_rect.y += 0x20;
        }
        cs->swork[1] += (short)((cs->func == 0 ? 32 : -32) / 5);
        if (++cs->ct >= 6) cs->step++;
        break;
    case 2: {
        for (int ii = 0; ii < 16; ii++) {
            int r = (cs->func == 0 ? 7 : 0) * 16 + ii;
            if (k[r]) {
                DF_MOBJ2_DeleteTagWindow(k[r]);
                k[r] = 0;
            }
        }
        for (int c = 0; c < 16; c++) {
            if (cs->func == 0) {
                for (int row = 7; row > 0; row--) k[row * 16 + c] = k[(row - 1) * 16 + c];
                k[c] = cs->tplistp[c];
            } else {
                for (int row = 0; row < 7; row++) k[row * 16 + c] = k[(row + 1) * 16 + c];
                k[0x70 + c] = cs->tplistp[c];
            }
            cs->tplistp[c] = 0;
        }
        win_rect.x = 0x42;
        win_rect.y = 0x60;
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 16; col++) {
                if (k[row * 16 + col]) DF_MOBJ2_SetTagWindowPos(k[row * 16 + col], win_rect.x, win_rect.y);
                win_rect.x += 0x20;
            }
            win_rect.x = 0x42;
            win_rect.y += 0x20;
        }
        cs->k2num = (int)PS2_STALE_GPR(22);   /* never assigned here: the caller's $s6 */
        for (int i = 0; i < 0x80; i++) DF_MOBJ2_SetTagWindowStr(k[i], cs->keyTopTable + i * 4);
        /* OLMENU 0x1edf1f0 .. 0x1edf220 (the ChangePage arrow quads again) */
        static const struct stMenuRect up_r = {34, 96, 32, 32}, up_t = {184, 0, 216, 32};
        static const struct stMenuRect dn_r = {34, 336, 32, 32}, dn_t = {216, 32, 184, 0};
        mname_arrow(ioObjp, &cs->scrool_up, cs->k2hd != 0, &up_wnd, up_uv, &up_col, up_r, up_t);
        mname_arrow(ioObjp, &cs->scrool_down, cs->k2hd < mname_k2rows(cs) - 8, &dn_wnd, dn_uv, &dn_col, dn_r, dn_t);
        cs->step++;
        break;
    }
    case 3:
        if ((k[0] && k[0]->stat == 0) || (k[1] && k[1]->stat == 0)) {
            ioObjp->play_func = MNAME_PLAY(__PlayNameEditSystem2);
            ioObjp->stat = 0;
        }
        break;
    }
}
