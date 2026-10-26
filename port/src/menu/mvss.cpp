/* src/menu/mvss.cpp - MVSS, the Versus "situation setup" screen (OLMENU overlay, scene label 0x54): five rows the
 * local player edits with left / right (map, time limit, battle form, number of COMs, "next"; a sixth "START" row
 * only when the screen was opened with mode != 0), drawn as a window, unit sprites, the map picture, the time bar,
 * 14 layout strings and per-row cursor windows. One stMobj owns everything; its dt is a struct_5a81ba8d (p_csysp).
 * Group 1 native: the item functions (__MVSS_ItemSetCursor / FadeOut / FadeIn / Delete / Create) and
 * __MVSS_ChangeComWindow (COM-count limits for the chosen map / form / player count, the COM row colours and the
 * link message on a change). Group 2: the shared "commit the setup" helpers DF_MVS_TransTextureVSAC / TransModelVSAC
 * / InitTransVSAC / SetVSForm (also called by mvsg / mvst / mvsw), the unreferenced __MVS_GetAIIndex, the draw_func
 * __DrawVsSetUpSystem, __PlayVsSetUpDispatch (child screen / dialog / link-lost states) and
 * __PlayVsSetUpSystemFadeOut (child launch, emblem exchange, commit). Group 3: the input loop __PlayVsSetUpSystem,
 * __PlayVsSetUpSystemFadeIn (link handshake, item creation) and the constructor DF_MVSS_NewSystem. The sprintf calls are newlib's (ps2_sprintf_*: buffer, format and every %s argument in
 * PS2 memory, nodwarf strings); the buffer handed to the interpreted fslnk_LinkSendData sits at its stack slot. */
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include <string.h>

#ifdef ACNB_SOURCEPORT
char mvss_live_monitor[13] = "LIVE MONITOR";
char mvss_fmt_parts10[11] = "parts10_%d";
char mvss_fmt_parts10a[12] = "parts10a_%d";
char mvss_no_limit[9] = "NO LIMIT";
char mvss_fmt_parts20d[12] = "parts20d_%d";
char mvss_player_0[9] = "player_0";
char mvss_p1[3] = "1P";
char mvss_p2[3] = "2P";
char mvss_p3[3] = "3P";
char mvss_p4[3] = "4P";
char mvss_amp[2] = "&";
char mvss_empty[1] = "";
char mvss_fmt_time[8] = "%02d:00";
char mvss_fmt_s[3] = "%s";
char mvss_fmt_sss[7] = "%s%s%s";
/* mvs_map_id is mvsg.c's size-0 DWARF array (mvsg.cpp keeps its own file-local copy): same 23 ids + the 0 after */
static const unsigned char mvss_map_id[24] = {1,  2,  4,  5,  6,  12, 13, 14, 15, 16, 63, 64,
                                              66, 68, 69, 72, 73, 74, 75, 76, 77, 78, 79, 0};
#define MVSS_MAP_ID mvss_map_id
#else
#define MVSS_MAP_ID mvs_map_id
#endif

typedef struct struct_5a81ba8d VssSys; /* p_csysp: dt of the screen object */

static inline VssSys *mvss_sys(struct stMobj *pMobj) { return (VssSys *)&pMobj->dt; }
static inline struct struct_37ce77ad *mvss_spr(struct stMobj *p) { return (struct struct_37ce77ad *)&p->dt; }
static inline struct struct_d2c56d2a *mvss_str(struct stMobj *p) { return (struct struct_d2c56d2a *)&p->dt; }

/* the inline "delete" of a sprite / string object (f_draw = 0, then f_erase = 1: two separate flag-byte writes) */
static inline void mvss_erase(struct stMobj *p)
{
    p->f_draw = 0;
    p->f_erase = 1;
}

static inline unsigned char *mvss_help(unsigned int id) { return DF_MSTR_GetInfo(MenuMng.language, 0, id); }

/* hide every arrow / cursor, then show the ones of row cs->idx and refresh its help line (unless the row did not
 * change) and its value (map sprite, time bar + text, form / COM cursor position) */
static void __MVSS_ItemSetCursor(struct stMobj *pMobj, char index_no_change)
{
    PS2_FRAME(0x1c0);
    PS2_STACK(char[256], name, 0xb0);
    VssSys *cs = mvss_sys(pMobj);

    for (int i = 0; i < 4; i++) DF_MOBJ_SetDrawEnable(cs->apArrow[i], 0);
    for (int i = 0; i < 4; i++) {
        DF_MOBJ_SetDrawEnable(cs->apCursorTrue[i], 0);
        DF_MOBJ_SetDrawEnable(cs->apCursorFalse[i], 0);
    }
    DF_MOBJ_SetDrawEnable(cs->apCursorFalse[0], 1);
    DF_MOBJ_SetDrawEnable(cs->apCursorFalse[1], 1);

    switch ((unsigned int)cs->idx) {
    case 0:
        DF_MOBJ_SetDrawEnable(cs->apArrow[0], 1);
        DF_MOBJ_SetDrawEnable(cs->apArrow[1], 1);
        /* the sprite id store sits in the delay slot of the index_no_change test: always runs */
        mvss_spr(cs->mapp[0])->texid = cs->sidx[0] + 0x1388;
        if (!index_no_change) DF_M1HELP_ChangeString(mvss_help(0x18fcf));
        break;
    case 1: {
        DF_MOBJ_SetDrawEnable(cs->apArrow[2], 1);
        DF_MOBJ_SetDrawEnable(cs->apArrow[3], 1);
        char bar[6] = "bar_0";
        struct _LAYOUT_RET_DATA ret;
        memset(&ret, 0, sizeof(ret));
        DF_LO_GetLayoutData((unsigned char *)LAYOUT_GetLayoutData(0x40a), (unsigned char *)bar, 0, &ret);
        int t = cs->sidx[1];
        int x;
        if (t == 0x10) x = ret.Pos.w;
        else x = (int)((float)t * (float)ret.Pos.w / 16.0f);
        struct struct_37ce77ad *b = mvss_spr(cs->pBar);
        b->rect.w = x;
        b->prev_rect.w = x;
        b->next_rect.w = x;
        if (!index_no_change) DF_M1HELP_ChangeString(mvss_help(0x18fd0));
        if (cs->sidx[1] == 0x10) ps2_sprintf_d(cs->time_str, mvss_no_limit, 0x10);
        else ps2_sprintf_d(cs->time_str, mvss_fmt_time, cs->sidx[1]);
        DF_MOBJ_SetStringDirect(cs->apParts20d[2], cs->time_str);
        break;
    }
    case 2:
        DF_MOBJ_SetDrawEnable(cs->apCursorTrue[0], 1);
        DF_MOBJ_SetDrawEnable(cs->apCursorFalse[0], 0);
        WindowEx_BlinkBegin(cs->apCursorTrue[0]);
        ps2_sprintf_d(name, mvss_fmt_parts10, cs->sidx[(int)cs->idx]);
        WindowEx_MoveToLayout(cs->apCursorTrue[0], (char *)LAYOUT_GetLayoutData(0x40a), name, 0);
        WindowEx_MoveToLayout(cs->apCursorFalse[0], (char *)LAYOUT_GetLayoutData(0x40a), name, 0);
        if (!index_no_change) DF_M1HELP_ChangeString(mvss_help(0x18fd2));
        break;
    case 3:
        DF_MOBJ_SetDrawEnable(cs->apCursorTrue[1], 1);
        DF_MOBJ_SetDrawEnable(cs->apCursorFalse[1], 0);
        WindowEx_BlinkBegin(cs->apCursorTrue[1]);
        ps2_sprintf_d(name, mvss_fmt_parts10a, cs->sidx[(int)cs->idx]);
        WindowEx_MoveToLayout(cs->apCursorTrue[1], (char *)LAYOUT_GetLayoutData(0x40a), name, 0);
        WindowEx_MoveToLayout(cs->apCursorFalse[1], (char *)LAYOUT_GetLayoutData(0x40a), name, 0);
        if (!index_no_change) DF_M1HELP_ChangeString(mvss_help(0x18fd6));
        break;
    case 4:
        DF_MOBJ_SetDrawEnable(cs->apCursorTrue[2], 1);
        DF_MOBJ_SetDrawEnable(cs->apCursorFalse[2], 0);
        WindowEx_BlinkBegin(cs->apCursorTrue[2]);
        if (!index_no_change) DF_M1HELP_ChangeString(mvss_help(0x18fdb));
        break;
    case 5:
        DF_MOBJ_SetDrawEnable(cs->apCursorTrue[3], 1);
        DF_MOBJ_SetDrawEnable(cs->apCursorFalse[3], 0);
        WindowEx_BlinkBegin(cs->apCursorTrue[3]);
        if (!index_no_change) DF_M1HELP_ChangeString(mvss_help(0x18fdc));
        break;
    default:
        break;
    }
}

/* start the fade-out of every item (kind is the callers' 5, unused); the help line fades out in any case */
static void __MVSS_ItemFadeOut(struct stMobj *pMobj, int kind, int control)
{
    PS2_FRAME(0x40);
    (void)kind;
    VssSys *cs = mvss_sys(pMobj);

    if (control) {
        if (cs->apParts01[0]) WindowEx_SetFadeOut(cs->apParts01[0]);
        for (int i = 0; i < 5; i++)
            if (cs->apIta[i]) UnitSprite_SetFadeOut(cs->apIta[i]);
        if (cs->mapp[0]) DF_MOBJ_SetFadeOut(cs->mapp[0]);
        if (cs->pBar) DF_MOBJ_SetFadeOut(cs->pBar);
        for (int i = 0; i < 14; i++)
            if (cs->apParts20d[i]) DF_MOBJ_SetFadeOut(cs->apParts20d[i]);
        for (int i = 0; i < 4; i++)
            if (cs->apArrow[i]) UnitSprite_SetFadeOut(cs->apArrow[i]);
        for (int i = 0; i < 4; i++) {
            if (cs->apCursorTrue[i]) {
                WindowEx_SetFadeOut(cs->apCursorTrue[i]);
                WindowEx_BlinkEnd(cs->apCursorTrue[i]);
                WindowEx_SetFadeOut(cs->apCursorFalse[i]); /* not null-tested */
            }
        }
        if (cs->pPlayerID) DF_MOBJ_SetFadeOut(cs->pPlayerID);
    }
    DF_M1HELP_FadeOut();
}

/* show and fade in every item (no null tests except the player label; mapp[0] is only faded in) */
static void __MVSS_ItemFadeIn(struct stMobj *pMobj, int kind, int control)
{
    PS2_FRAME(0x40);
    (void)kind;
    VssSys *cs = mvss_sys(pMobj);

    if (cs->apIta[0] == 0 || control == 0) return;
    DF_MOBJ_SetDrawEnable(cs->apParts01[0], 1);
    WindowEx_SetFadeIn(cs->apParts01[0]);
    for (int i = 0; i < 5; i++) {
        DF_MOBJ_SetDrawEnable(cs->apIta[i], 1);
        UnitSprite_SetFadeIn(cs->apIta[i]);
    }
    DF_MOBJ_SetFadeIn(cs->mapp[0]);
    DF_MOBJ_SetDrawEnable(cs->pBar, 1);
    DF_MOBJ_SetFadeIn(cs->pBar);
    for (int i = 0; i < 14; i++) {
        DF_MOBJ_SetDrawEnable(cs->apParts20d[i], 1);
        DF_MOBJ_SetFadeIn(cs->apParts20d[i]);
    }
    for (int i = 0; i < 4; i++) {
        DF_MOBJ_SetDrawEnable(cs->apArrow[i], 1);
        UnitSprite_SetFadeIn(cs->apArrow[i]);
    }
    for (int i = 0; i < 4; i++) {
        DF_MOBJ_SetDrawEnable(cs->apCursorTrue[i], 1);
        WindowEx_SetFadeIn(cs->apCursorTrue[i]);
        WindowEx_BlinkBegin(cs->apCursorTrue[i]);
    }
    if (cs->pPlayerID) {
        DF_MOBJ_SetDrawEnable(cs->pPlayerID, 1);
        DF_MOBJ_SetFadeIn(cs->pPlayerID);
    }
}

/* delete every item (pops the scene label ItemCreate pushed) */
static void __MVSS_ItemDelete(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    VssSys *cs = mvss_sys(pMobj);

    if (cs->apParts01[0]) DF_MBACK_PopSceneLabel();
    WindowEx_Delete(cs->apParts01[0]);
    cs->apParts01[0] = 0;
    for (int i = 0; i < 5; i++) {
        UnitSprite_Delete(cs->apIta[i]);
        cs->apIta[i] = 0;
    }
    if (cs->mapp[0]) mvss_erase(cs->mapp[0]);
    cs->mapp[0] = 0;
    if (cs->pBar) mvss_erase(cs->pBar);
    cs->pBar = 0;
    for (int i = 0; i < 14; i++) {
        if (cs->apParts20d[i]) {
            mvss_erase(cs->apParts20d[i]);
            cs->apParts20d[i] = 0;
        }
    }
    for (int i = 0; i < 4; i++) {
        UnitSprite_Delete(cs->apArrow[i]);
        cs->apArrow[i] = 0;
    }
    for (int i = 0; i < 4; i++) {
        WindowEx_Delete(cs->apCursorTrue[i]);
        cs->apCursorTrue[i] = 0;
        WindowEx_Delete(cs->apCursorFalse[i]);
        cs->apCursorFalse[i] = 0;
    }
    if (cs->pPlayerID) {
        mvss_erase(cs->pPlayerID);
        cs->pPlayerID = 0;
    }
}

/* create every item once (hidden, except the map picture), the local player's label and the help line */
static void __MVSS_ItemCreate(struct stMobj *pMobj)
{
    PS2_FRAME(0x390);
    PS2_STACK(char[256], name, 0x1c0);
    PS2_STACK(char[256], name2, 0xa0);
    VssSys *cs = mvss_sys(pMobj);
    static const char *const ita_names[5] = {"time_0", "target_0", "4taku_0", "banner_0", "banner_1"};
    static const char *const arrow_names[4] = {"left_0", "right_0", "left_1", "right_1"};
    static const char *const cursor_names[4] = {"parts10_0", "parts10a_0", "parts10b_0", "parts10b_1"};

    if (cs->apIta[0] != 0) return;
    DF_MBACK_PushSceneLabel((enum_5124d09c)0x54);
    cs->apParts01[0] = WindowEx_NewFromLayout(pMobj, (_WINDOWEX_TYPE)0, (char *)LAYOUT_GetLayoutData(0x409),
                                              (char *)"parts01_0");
    DF_MOBJ_SetDrawEnable(cs->apParts01[0], 0);
    for (int i = 0; i < 5; i++) {
        cs->apIta[i] = UnitSprite_New(pMobj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x409), (char *)ita_names[i]);
        DF_MOBJ_SetDrawEnable(cs->apIta[i], 0);
    }

    /* map picture: texture 0x1388 + map index */
    {
        struct stMenuRect win_rect = {0x58, 0x40, 0x1d0, 0x40};
        int uv[4] = {0, 0, 0x1d0, 0x40};
        struct struct_76b810ec col = {0x80, 0x80, 0x80, 0x80};
        cs->mapp[0] = DF_MOBJ_NXNewSprite(pMobj, cs->sidx[0] + 0x1388, &win_rect, uv, &col, 0, 0, 0, 0);
    }

    /* time bar: everything from the layout part "bar_0" (the rodata defaults are overwritten) */
    {
        char bar[6] = "bar_0";
        struct _LAYOUT_RET_DATA ret;
        struct stMenuRect pos;
        int uv[4];
        struct struct_76b810ec col;
        memset(&ret, 0, sizeof(ret));
        DF_LO_GetLayoutData((unsigned char *)LAYOUT_GetLayoutData(0x40a), (unsigned char *)bar, 0, &ret);
        col = ret.Col;
        pos.x = ret.Pos.x;
        pos.y = ret.Pos.y;
        pos.w = ret.Pos.w;
        pos.h = ret.Pos.h;
        memcpy(uv, ret.UV, 16);
        cs->pBar = DF_MOBJ_NXNewSprite(pMobj, ret.TexID, &pos, uv, &col, 0, 0, 1, 0);
        DF_MOBJ_SetDrawEnable(cs->pBar, 0);
    }

    for (int i = 0; i < 14; i++) {
        ps2_sprintf_d(name, mvss_fmt_parts20d, i);
        cs->apParts20d[i] = DF_MOBJ_NewStringFromLayoutName(pMobj, 0, 0x18fcf + i, 0, (char *)LAYOUT_GetLayoutData(0x40a),
                                                            name, 0, 0, 0);
        DF_MOBJ_SetDrawEnable(cs->apParts20d[i], 0);
    }
    if (cs->sidx[1] == 0x10) ps2_sprintf_d(cs->time_str, mvss_no_limit, 0x10);
    else ps2_sprintf_d(cs->time_str, mvss_fmt_time, cs->sidx[1]);
    DF_MOBJ_SetStringDirect(cs->apParts20d[2], cs->time_str);

    for (int i = 0; i < 4; i++) {
        cs->apArrow[i] =
            UnitSpriteBlink_New(pMobj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x40a), (char *)arrow_names[i]);
        DF_MOBJ_SetDrawEnable(cs->apArrow[i], 0);
    }
    for (int i = 0; i < 4; i++) {
        cs->apCursorTrue[i] = WindowEx_NewFromLayout(pMobj, (_WINDOWEX_TYPE)0xb, (char *)LAYOUT_GetLayoutData(0x40a),
                                                     (char *)cursor_names[i]);
        DF_MOBJ_SetDrawEnable(cs->apCursorTrue[i], 0);
    }
    for (int i = 0; i < 4; i++) {
        cs->apCursorFalse[i] = WindowEx_NewFromLayout(pMobj, (_WINDOWEX_TYPE)0x24, (char *)LAYOUT_GetLayoutData(0x40a),
                                                      (char *)cursor_names[i]);
        DF_MOBJ_SetDrawEnable(cs->apCursorFalse[i], 0);
    }

    /* the local player's label: "1P" offline; "1P&2P" style for two local players (play_mode 1); "LIVE MONITOR"
     * for a spectator console */
    if (cs->pPlayerID == 0) {
        unsigned int str_col = 0x80c6c077u;
        ps2_sprintf_d(name2, mvss_player_0, 0);
        ZeroMemory(cs->sPlayerString, 16);
        char *ps[7] = {mvss_p1, mvss_p2, mvss_p3, mvss_p4, mvss_amp, mvss_live_monitor, mvss_empty};
        int nl[3] = {0, 4, 1};
        char player_num = play_info.player_num;
        char play_mode = play_info.play_mode;
        int t = 0;
        if (play_mode == 1 && play_info.self_id > 0) t = 1;
        char self_id = play_info.self_id;
        nl[0] = self_id + t;
        if (play_mode == 0) nl[2] = nl[1] = 6;
        if (play_mode == 1) {
            if (self_id == 1 && play_info.connected_num == 2 && player_num == 3) nl[2] = nl[1] = 6;
            else nl[2] = nl[0] + 1;
        }
        char cn = play_info.connected_num;
        if (self_id == cn - 1 && cn >= 3 && player_num < (play_mode + 1) * (self_id + 1) - play_mode) nl[0] = 5;
        if (nl[0] == 5) ps2_sprintf_s(cs->sPlayerString, mvss_fmt_s, ps[5]);
        else ps2_sprintf_sss(cs->sPlayerString, mvss_fmt_sss, ps[nl[0]], ps[nl[1]], ps[nl[2]]);
        cs->pPlayerID = DF_MOBJ_NewStringDirectFromLayoutName(pMobj, cs->sPlayerString, (char *)LAYOUT_GetLayoutData(0x40a),
                                                              name2, (struct struct_76b810ec *)&str_col, 0, 0x10000);
        DF_MOBJ_SetDrawEnable(cs->pPlayerID, 0);
    }
    DF_M1HELP_SetWindow(); /* empty stub; the original passes (0, 0x280) */
    DF_M1HELP_SetString(mvss_help(0x18fcf));
}

/* COM-count limits for the chosen map / form / player count (a no-COM map allows none), clamp the COM count, colour
 * the four COM row strings, tell the link peers about a change and move the COM cursor */
static void __MVSS_ChangeComWindow(struct struct_5a81ba8d *p_csysp, char init)
{
    PS2_FRAME(0x140);
    PS2_STACK(char[256], name, 0x20);
    PS2_STACK(char[8], send_data, 0x138);
    static const unsigned char col[2][4] = {{0x72, 0x86, 0x8d, 0x80}, {0x40, 0x60, 0x63, 0x80}};
    VssSys *cs = p_csysp;
    char prev = cs->sidx[3];

    int noc = 0;
    if (cs->sidx[0] != 0x17) {
        unsigned char m = MVSS_MAP_ID[(int)cs->sidx[0]];
        for (unsigned int i = 0; i < 7; i++) {
            if (m == mvs_noc_id[i]) {
                noc = 1;
                break;
            }
        }
    }
    if (noc) {
        cs->min_sidx = 0;
        cs->max_sidx = 0;
    } else if (cs->sidx[2] == 0) {
        switch (play_info.player_num) {
        case 4: cs->min_sidx = 0; cs->max_sidx = 0; break;
        case 3: cs->min_sidx = 0; cs->max_sidx = 1; break;
        case 2: cs->min_sidx = 0; cs->max_sidx = 2; break;
        case 1: cs->min_sidx = 1; cs->max_sidx = 3; break;
        default: break;
        }
    } else {
        switch (play_info.player_num) {
        case 4: cs->min_sidx = 0; cs->max_sidx = 0; break;
        case 3: cs->min_sidx = 0; cs->max_sidx = 1; break;
        case 2: cs->min_sidx = 1; cs->max_sidx = 2; break;
        case 1: cs->min_sidx = 2; cs->max_sidx = 3; break;
        default: break;
        }
    }

    if (cs->min_sidx < 0) cs->sidx[3] = 0;
    else if (cs->sidx[3] < cs->min_sidx || cs->max_sidx < cs->sidx[3] || (init && !cs->dont_init_com_sidx))
        cs->sidx[3] = cs->min_sidx;

    for (int i = 0; i < 4; i++) {
        struct stMobj *p = cs->apParts20d[8 + i];
        if (p == 0) return; /* a missing string ends the whole function (asm 0x1eccd04) */
        const unsigned char *c = (i < cs->min_sidx || cs->max_sidx < i) ? col[1] : col[0];
        struct struct_d2c56d2a *s = mvss_str(p);
        s->cur_col.r = c[0];
        s->cur_col.g = c[1];
        s->cur_col.b = c[2];
    }

    if (cs->sidx[3] != prev) {
        memset(send_data, 0, 8);
        send_data[0] = 1;
        send_data[1] = 0xe;
        send_data[2] = cs->sidx[3];
        fslnk_LinkSendData(send_data, 8);
    }
    ps2_sprintf_d(name, mvss_fmt_parts10a, cs->sidx[3]);
    WindowEx_MoveToLayout(cs->apCursorTrue[1], (char *)LAYOUT_GetLayoutData(0x40a), name, 0);
    WindowEx_MoveToLayout(cs->apCursorFalse[1], (char *)LAYOUT_GetLayoutData(0x40a), name, 0);
}

/* ---- group 2: shared Versus commit helpers (DF_MVS_*), draw, dispatch, fade-out ---- */

#ifdef ACNB_SOURCEPORT
unsigned char mvs_map_id_2div[23] = {31, 32, 34, 35, 36, 42, 43, 44, 45, 46, 83, 84,
                                     86, 88, 89, 92, 93, 94, 95, 96, 97, 98, 99};
unsigned char mvs_rand_map_id[17] = {3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 18, 19, 20, 21};
#endif

#define MVSS_FN(f) ((void (*)(void))GFN(f))

/* DF_MBGUIDE_Change / DF_MBACK_ChangeTag / DF_MBACK_SetFadeOutNX are void(void) in DWARF but read a0..t0 / a0 */
static inline void mvss_guide(int a, int b, int c, int d, int e)
{
    gcall_as<int(int, int, int, int, int)>(GFN(DF_MBGUIDE_Change), a, b, c, d, e);
}
static inline void mvss_tag(int tag) { gcall_as<void(int)>(GFN(DF_MBACK_ChangeTag), tag); }
static inline void mvss_fadeout_nx(int a) { gcall_as<void(int)>(GFN(DF_MBACK_SetFadeOutNX), a); }

/* link shutdown: five fslnk calls, each followed by one vblank wait */
static inline void mvss_link_teardown(void)
{
    fslnk_LinkSyncStop();
    sceGsSyncV(0);
    fslnk_LinkSyncTerm();
    sceGsSyncV(0);
    fslnk_LinkStop();
    sceGsSyncV(0);
    fslnk_LinkTerm();
    sceGsSyncV(0);
    fslnk_LinkDestroy();
    sceGsSyncV(0);
}

/* the screen is done: give the menu work area back and stop */
static inline void mvss_finish(struct stMobj *obj, VssSys *cs)
{
    MenuWorkp = cs->defTBP;
    obj->play_func = 0;
    obj->draw_func = 0;
    obj->stat = 5;
}

/* link lost / timed out: hand over to the dispatcher's error dialog */
static inline void mvss_link_error(struct stMobj *obj, VssSys *cs, int func)
{
    cs->step = 0;
    cs->func = func;
    obj->play_func = MVSS_FN(__PlayVsSetUpDispatch);
    obj->draw_func = 0;
    obj->stat = 3;
}

/* fade the screen out towards __PlayVsSetUpSystemFadeOut (func is cleared before the help fade-out runs) */
static inline void mvss_leave(struct stMobj *obj, VssSys *cs)
{
    cs->func = 0;
    obj->draw_func = 0; /* delay slot of jal DF_M1HELP_FadeOut */
    DF_M1HELP_FadeOut();
    __MVSS_ItemFadeOut(obj, 5, 1);
    cs->step = 0;
    obj->play_func = MVSS_FN(__PlayVsSetUpSystemFadeOut);
    obj->stat = 2;
}

/* index of an AI slot relative to this console's local players (no callers in the game) */
int __MVS_GetAIIndex(int index)
{
    int m = play_info.mode;
    if (m == 0) {
        int cn = play_info.connected_num;
        int sid = play_info.self_id;
        if (sid == cn - 1 && cn >= 3) {
            int pm = play_info.play_mode;
            if (play_info.player_num < (sid + 1) * (pm + 1) - pm) return index;
        }
    }
    int local = play_info.local_player_num;
    int base;
    if (m == 0) base = play_info.self_id + (play_info.play_mode == 1 ? (0 < play_info.self_id) : 0);
    else base = play_info.self_id == 0 ? 0 : play_info.player_num - local;
    if (index >= base && index < base + local) return index - base;
    if (index < base) return local + index;
    return index;
}

/* copy every human player's emblem into EmblemSTM2 (one blocking GS path wait per entry, COMs too) */
int DF_MVS_TransTextureVSAC(void)
{
    PS2_FRAME(0x50);
    for (int ii = 0; ii < play_info.player_num + play_info.com_num; ii++) {
        if (ii < play_info.player_num && !(play_info.ai_mode[ii] & 1))
            CopyMemory(EmblemSTM2[ii], vs_ac_info[ii].embp, 0x1420);
        sceGsSyncPath(0, 0);
    }
    return 1;
}

/* team of every entry into g_nextstage.ac_belong and the human players' parts into the mission data. The COM arm
 * leaves the argument 0 whatever com_id is, so DF_MVS_SetMissionPartsImmediate never runs for a COM (quirk). */
int DF_MVS_TransModelVSAC(void)
{
    PS2_FRAME(0x60);
    int n = 0;
    for (int ii = 0; ii < play_info.player_num + play_info.com_num; ii++, n++) {
        int pn = play_info.player_num;
        int k = ii < pn ? ii : ii - pn;
        if (play_info.vs_form == 0) g_nextstage.ac_belong[ii] = ii + 1;
        else g_nextstage.ac_belong[ii] = play_info.team_condition[ii] & 3;
        struct AcAssemblyPartsSt *dtp = 0;
        if (ii < play_info.player_num) {
            if (!(play_info.ai_mode[ii] & 1)) dtp = &vs_ac_info[ii].ware;
        } else if (play_info.com_id[k] < 100) {
            dtp = 0;
        }
        if (dtp) DF_MVS_SetMissionPartsImmediate(dtp, ii);
    }
    return !(n < play_info.player_num);
}

int DF_MVS_InitTransVSAC(void) { return 1; }

/* commit the battle form: AI counts, compacted team table, NPC table, time limit and stage (random map roll) */
int DF_MVS_SetVSForm(void)
{
    PS2_FRAME(0x30);
    char *pi = (char *)&play_info;

    play_info.player_ai_num = 0;
    for (int ii = 0; ii < play_info.player_num; ii++)
        if ((play_info.ai_mode[ii] & 1) == 1) play_info.player_ai_num++;
    play_info.com_ai_num = 0;
    for (int ii = 0; ii < play_info.com_num; ii++)
        if (play_info.com_id[ii] >= 100) play_info.com_ai_num++;

    /* team_condition_true: humans from index 0, friend-table COMs from player_num - player_ai_num (unchecked) */
    {
        int pn = play_info.player_num;
        char *pp = pi;
        char *cp = pi + (pn - play_info.player_ai_num);
        for (int ii = 0; ii < pn + play_info.com_num;) {
            if (ii < pn) {
                if (!(pi[0x28 + ii] & 1)) *(pp++ + 0x14) = pi[0x10 + ii];
            } else if (pi[0x24 + ii - pn] < 100) {
                *(cp++ + 0x14) = pi[0x10 + ii];
            }
            ii++;
            pn = play_info.player_num;
        }
    }

    DF_NPCTAB_Reset();
    for (int i = 0; i < play_info.com_num; i++) {
        int id = play_info.com_id[i];
        if (id > 0 && id < 100) DF_NPCTAB_Add(id, 4, 0, 0);
        else DF_NPCTAB_Add(5, 4, 0, 0);
    }

    if (play_info.time_limit == 0x10) g_nextstage.vstime = -1;
    else g_nextstage.vstime = play_info.time_limit * 3600;

    int k;
    if (play_info.map_id == 0x17) {
        if (play_info.player_ai_num != 0 || play_info.com_num != 0) {
            ps2_srand(play_info.rand_seed);
            int again;
            do {
                again = 0;
                unsigned char cand = mvs_rand_map_id[(unsigned int)ps2_rand() % 17];
                unsigned char m = MVSS_MAP_ID[cand];
                for (unsigned int i = 0; i < 7; i++) {
                    if (m == mvs_noc_id[i]) {
                        again = 1;
                        break;
                    }
                }
                play_info.rand_map_id = cand; /* bnez delay slot: stored on every pass */
            } while (again);
        }
        k = play_info.rand_map_id;
    } else {
        k = play_info.map_id;
    }
    g_nextstage.stage = MVSS_MAP_ID[k];
    g_nextstage.ev_situ = 1;
    g_nextstage.chr_situ = 1;
    g_nextstage.dobj_situ = 1;
    if (play_info.play_mode == 1) g_nextstage.stage = mvs_map_id_2div[k];
    g_nextstage.is_split = play_info.play_mode == 1;
    play_info.f_ready = 1;
    return 1;
}

/* draw_func: START colour (selectable only with mode != 0 and unchanged form / COM count), COM window, link poll */
static void __DrawVsSetUpSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x70);
    PS2_STACK(char[8], wkd, 0x68);
    static const unsigned char col[2][4] = {{0x72, 0x86, 0x8d, 0x80}, {0x40, 0x60, 0x63, 0x80}};
    VssSys *cs = mvss_sys(ioObjp);

    cs->f_selectable_start = 0;
    int v = cs->mode == 0 || cs->f_forward != 0 || cs->sidx[2] != cs->prev_form || cs->sidx[3] != cs->prev_com;
    cs->f_selectable_start = v ? 0 : -1;
    if (struct stMobj *p = cs->apParts20d[13]) {
        const unsigned char *c = col[cs->f_selectable_start ? 0 : 1];
        struct struct_d2c56d2a *s = mvss_str(p);
        s->cur_col.r = c[0];
        s->cur_col.g = c[1];
        s->cur_col.b = c[2];
    }
    __MVSS_ChangeComWindow(cs, 0);
    if (play_info.connected_num == 1) return;

    /* link poll (asm-only: no replay links) */
    int r = fslnk_LinkProc();
    if (r < 0 && r != -0x10) {
        fslnk_GetConnectedNum();
        mvss_link_error(ioObjp, cs, 0x63);
        return;
    }
    int cn = play_info.connected_num;
    if (cn != fslnk_GetConnectedNum() + 1 || r == -0x10) {
        fslnk_GetConnectedNum();
        mvss_link_error(ioObjp, cs, 0x64);
        return;
    }
    if (ioObjp->stat != 0 && ioObjp->stat != 3) return;
    for (int k = 0; k < cn - 1; k = k + 1, cn = play_info.connected_num) {
        if ((unsigned int)fslnk_LinkRecvDataSize(k) < 8) continue;
        fslnk_LinkRecvData(wkd, k, 8);
        if (wkd[0] != 1 || wkd[1] != 2) continue;
        cs->func = 0;
        DF_M1HELP_FadeOut();
        __MVSS_ItemFadeOut(ioObjp, 5, 1);
        cs->step = 0;
        ioObjp->play_func = MVSS_FN(__PlayVsSetUpSystemFadeOut);
        ioObjp->stat = 2;
    }
}

/* play_func while a child screen (mvsg / mvst) or a dialog is up */
static void __PlayVsSetUpDispatch(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    VssSys *cs = mvss_sys(ioObjp);
    int func = cs->func;

    if (func == 0x64 || func == 0x63) {
        /* link lost (asm-only) */
        if (cs->step == 0) {
            if (DF_MDIALOG_Type() == 0) {
                mvss_guide(0, 0xe, 0, 0, 0x63);
                DF_MDIALOG_Open(5, DF_MSTR_GetInfo(MenuMng.language, 1, func == 0x63 ? 0x30ed0 : 0x30ed3), 0, 0, 0, 0,
                                0, 0);
                mvss_link_teardown();
                cs->step = 1;
            } else if (DF_MDIALOG_Sync() == 2) {
                DF_MDIALOG_Close();
            }
        } else if (cs->step == 1) {
            if (!(DF_MDIALOG_Sync() & 4)) return;
            mvss_guide(0, 0, 0, 0, 0x63);
            play_info.f_ready = -1;
            cs->f_error = -1;
            mvss_leave(ioObjp, cs);
        }
        return;
    }
    if (func == 2) {
        int r = DF_MDIALOG_Sync();
        if ((r & 4) && r == 4) {
            ioObjp->play_func = MVSS_FN(__PlayVsSetUpSystem);
            ioObjp->stat = 0;
        }
        return;
    }
    if (func == 1) {
        int r = DF_MDIALOG_Sync();
        if ((r & 4) && r == 4) {
            mvss_guide(0, 0, 0, 0, 0x63);
            Voice_Call(0x3eb, 0x7f);
            play_info.f_ready = -1;
            mvss_leave(ioObjp, cs);
        }
        return;
    }
    if (func != 0) return;

    /* the original reads csysp->stat before its NULL test */
    struct stMobj *c = cs->csysp;
    unsigned char st = c ? c->stat : ((const unsigned char *)PS2_NULL_MEM())[2];
    if (st != 5) return;
    if (c) {
        c->f_draw = 0;
        cs->csysp->f_erase = 1;
        cs->csysp = 0;
    }
    if (play_info.f_ready != 0 || play_info.f_back != 0) {
        cs->func = 0;
        cs->step = 0;
        play_info.f_back = 0;
        ioObjp->stat = 2;
        ioObjp->play_func = MVSS_FN(__PlayVsSetUpSystemFadeOut);
    } else {
        cs->dont_init_com_sidx = 1;
        cs->step = 2;
        ioObjp->stat = 1;
        ioObjp->count = 0xf;
        ioObjp->play_func = MVSS_FN(__PlayVsSetUpSystemFadeIn);
        ioObjp->draw_func = MVSS_FN(__DrawVsSetUpSystem);
    }
}

/* func 1 / 2 (identical in the asm): commit the rows and open the AC setup (mvsg, vs_form 0) or the team setup
 * (mvst) */
static inline void mvss_open_child(struct stMobj *obj, VssSys *cs)
{
    play_info.map_id = cs->sidx[0];
    play_info.time_limit = cs->sidx[1];
    play_info.vs_form = cs->sidx[2];
    play_info.com_num = cs->sidx[3];
    if (play_info.vs_form == 0) {
        play_info.team_condition[3] = 0;
        play_info.team_condition[2] = 0;
        play_info.team_condition[1] = 0;
        play_info.team_condition[0] = 0;
        cs->csysp = DF_MVSG_NewSystem(obj, &play_info, cs->whsp);
    } else {
        cs->csysp = DF_MVST_NewSystem(obj, &play_info, cs->whsp);
    }
    cs->func = 0;
    obj->play_func = MVSS_FN(__PlayVsSetUpDispatch);
    obj->draw_func = 0;
    obj->stat = 3;
}

/* play_func after ItemFadeOut: step 0 deletes the items and launches the child / leaves; step 1 waits for the scene
 * transition, exchanges emblems (link) and commits the setup into play_info / g_nextstage */
static void __PlayVsSetUpSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x5160);
    VssSys *cs = mvss_sys(ioObjp);

    if (cs->step != 1) {
        if (DF_MDIALOG_Type()) {
            if (DF_MDIALOG_Sync() == 2) DF_MDIALOG_Close();
            return;
        }
        __MVSS_ItemDelete(ioObjp);
        if (cs->func == 0) {
            if (play_info.f_ready == 0) {
                mvss_tag(0x21);
                mvss_guide(0, 1, 2, 0, 0x63);
            }
            if (cs->f_start || cs->f_error) {
                if (!cs->f_faded) {
                    mvss_guide(0, 0, 0, 0, 0x63);
                    mvss_fadeout_nx(0);
                    Bgm_FadeOutStart();
                    cs->f_faded = -1;
                }
                cs->f_onload = -1;
                cs->step = 1;
            } else {
                mvss_finish(ioObjp, cs);
            }
        } else if (cs->func == 1 || cs->func == 2) {
            mvss_open_child(ioObjp, cs);
        }
        return;
    }

    if (!DF_MBACK_Sync()) return;
    if (cs->f_error && !cs->f_onload) {
        mvss_finish(ioObjp, cs);
        return;
    }
    if (!DF_MVS_TransModelVSAC()) return;
    cs->f_onload = 0;
    if (cs->f_error) {
        mvss_finish(ioObjp, cs);
        return;
    }

    if (play_info.connected_num == 1) {
        DF_EMB_MakeSmallEmblem(vs_ac_info[0].embp, EmblemTM2[0]);
        DF_EMB_MakeSmallEmblem(vs_ac_info[1].embp, EmblemTM2[1]);
    } else {
        /* network emblem exchange (asm-only: no replay links) */
        PS2_STACK(char[5152], dast0, 0xd0);
        PS2_STACK(char[5152], dast1, 0x14f0);
        PS2_STACK(char[5152], dast2, 0x2910);
        PS2_STACK(char[5152], dast3, 0x3d30);
        PS2_STACK(char[8], wkd, 0x5150);
        PS2_STACK(char[8], send_data, 0x5158);
        if (cs->sync == 0) {
            int cn = play_info.connected_num;
            int sid = play_info.self_id;
            int idx;
            int pm = play_info.play_mode;
            if (sid == cn - 1 && cn >= 3 && play_info.player_num < (sid + 1) * (pm + 1) - pm) idx = 0x63;
            else idx = sid + (play_info.play_mode == 1 ? (0 < sid) : 0);
            memset(send_data, 0, 8);
            send_data[0] = 4;
            send_data[2] = idx;
            send_data[1] = 1;
            fslnk_LinkSendData(send_data, 8);
            if (idx < 0x63) {
                DF_EMB_MakeSmallEmblem(vs_ac_info[idx].embp, EmblemTM2[0]);
                fslnk_LinkSendData(vs_ac_info[idx].embp, 0x1420);
            } else {
                fslnk_LinkSendData(dast3, 0x1420); /* uninitialised stack block (quirk) */
            }
            if (play_info.play_mode == 1) {
                if (idx < 0x63) {
                    DF_EMB_MakeSmallEmblem(vs_ac_info[idx + 1].embp, EmblemTM2[1]);
                    fslnk_LinkSendData(vs_ac_info[idx + 1].embp, 0x1420);
                } else {
                    fslnk_LinkSendData(dast2, 0x1420);
                }
            }
            cs->sync = 1;
            ioObjp->draw_func = 0;
            return;
        }
        if (fslnk_LinkProc() < 0) {
            mvss_link_error(ioObjp, cs, 0x63);
            return;
        }
        int t = cs->timeout;
        cs->timeout = t + 1;
        if (t >= 0x4b1) {
            mvss_link_error(ioObjp, cs, 0x63);
            return;
        }
        if (fslnk_LinkSendDataSize() > 0) return;
        int ready = 0;
        int peers = play_info.connected_num - 1;
        for (int k = 0; k < peers; k = k + 1, peers = play_info.connected_num - 1) {
            if (play_info.play_mode == 0) {
                if ((unsigned int)fslnk_LinkRecvDataSize(k) >= 0x1428) ready++;
            } else {
                if ((unsigned int)fslnk_LinkRecvDataSize(k) >= 0x2848) ready++;
            }
        }
        if (ready < peers) return;
        for (int k = 0; k < play_info.connected_num - 1; k++) {
            fslnk_LinkRecvData(wkd, k, 8);
            int i = wkd[2];
            if (i < 0x63) fslnk_LinkRecvData(vs_ac_info[i].embp, k, 0x1420);
            else fslnk_LinkRecvData(dast1, k, 0x1420);
            if (play_info.play_mode == 1) {
                i = wkd[2];
                if (i + 1 < 0x63) fslnk_LinkRecvData(vs_ac_info[i + 1].embp, k, 0x1420);
                else fslnk_LinkRecvData(dast0, k, 0x1420);
            }
        }
    }

    /* drain whatever the peers still have queued (offline: no peers) */
    if (play_info.connected_num - 1 > 0) {
        PS2_STACK(char[128], dbox, 0x50);
        for (int k = 0; k < play_info.connected_num - 1; k++) {
            int n = fslnk_LinkRecvDataSize(k);
            while (n != 0) {
                if (n > 0x80) n = 0x80;
                fslnk_LinkRecvData(dbox, k, n);
                n = fslnk_LinkRecvDataSize(k);
            }
        }
    }

    play_info.map_id = cs->sidx[0];
    play_info.time_limit = cs->sidx[1];
    play_info.vs_form = cs->sidx[2];
    play_info.com_num = cs->sidx[3]; /* delay slot of jal DF_MVS_TransTextureVSAC: the callee sees it */
    if (!DF_MVS_TransTextureVSAC()) return;
    DF_MVS_SetVSForm();
    mvss_finish(ioObjp, cs);
}

/* ---- group 3: input loop, fade-in, constructor ---- */

/* stage id in the no-COM map list */
static inline int mvss_is_noc(unsigned char stage)
{
    for (unsigned int i = 0; i < 7; i++)
        if (stage == mvs_noc_id[i]) return 1;
    return 0;
}

/* the map row's stage takes no COM (the random entry 0x17 never counts) */
static inline int mvss_noc_map(int map) { return map != 0x17 && mvss_is_noc(MVSS_MAP_ID[map]); }

/* not enough players for the chosen battle form (form = the form row, sidx[2]) */
static inline int mvss_form_short(int form)
{
    return (form == 1 && play_info.player_num < 3) || (form == 2 && play_info.player_num < 3) ||
           (form == 0 && play_info.player_num < 2);
}

/* "not possible on this map" dialog; the dispatcher returns to the input loop when it closes */
static inline void mvss_noc_dialog(struct stMobj *obj, VssSys *cs)
{
    DF_MDIALOG_Open(5, DF_MSTR_GetInfo(MenuMng.language, 1, 0x30e78), 0, 0, 0, 0, 1, 0);
    cs->func = 2;
    obj->stat = 3;
    obj->play_func = MVSS_FN(__PlayVsSetUpDispatch);
}

/* the shared tail of the leave arms: items fade out, then __PlayVsSetUpSystemFadeOut */
static inline void mvss_fade_items(struct stMobj *obj, VssSys *cs)
{
    __MVSS_ItemFadeOut(obj, 5, 1);
    cs->step = 0;
    obj->play_func = MVSS_FN(__PlayVsSetUpSystemFadeOut);
    obj->stat = 2;
}

/* play_func: the input loop (back / forward / START, left / right on the value rows, up / down between rows); every
 * changed value is also sent to the link peers (fslnk_LinkSendData runs offline too) */
static void __PlayVsSetUpSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0xb0);
    PS2_STACK(int[4], sidxTable, 0x70);
    PS2_STACK(char[8], send_val, 0x80);
    PS2_STACK(char[8], send_start, 0x88);
    PS2_STACK(char[8], send_fwd1, 0x90);
    PS2_STACK(char[8], send_fwd2, 0x98);
    PS2_STACK(char[8], send_back, 0xa0);
    PS2_STACK(char[6], prev_sidx, 0xa8);
    static const int sidx_table[4] = {24, 16, 3, 3};
    VssSys *cs = mvss_sys(ioObjp);

    memcpy(sidxTable, sidx_table, sizeof(sidx_table));
    char prev_idx = cs->idx;
    memcpy(prev_sidx, cs->sidx, 6); /* 6 bytes into the 5-byte prev_sidx of the original (quirk) */

    int back = MenuMng.stat == 0xc; /* forced back: no voice */
    if (!back) {
        if (MenuKeyGuide_IsShown()) return;
        if (MPad[MPadUser].start == 1) {
            MenuKeyGuide_Show(0x32);
            return;
        }
        if (MPad[MPadUser].Rrt == 1) {
            Voice_Call(0x3eb, 0x7f);
            back = 1;
        }
    }
    if (back) {
        cs->func = 0; /* delay slot of jal DF_M1HELP_FadeOut */
        DF_M1HELP_FadeOut();
        mvss_fade_items(ioObjp, cs);
        memset(send_back, 0, 8);
        send_back[0] = 1;
        send_back[1] = 2;
        fslnk_LinkSendData(send_back, 8);
        return;
    }

    struct struct_2d84b07b *pad = &MPad[MPadUser];
    if (pad->Rdw == 1) {
        int noc = mvss_noc_map(cs->sidx[0]);
        if (cs->idx == 4) {
            /* next: the AC setup (mvsg) or, with 4 players, func 2 */
            if (mvss_form_short(cs->sidx[2]) && noc) {
                mvss_noc_dialog(ioObjp, cs);
                return;
            }
            Voice_Call(0x3ea, 0x7f);
            if (play_info.player_num == 4) {
                cs->func = 2;
                memset(send_fwd2, 0, 8);
                send_fwd2[0] = 1;
                send_fwd2[1] = 1;
                fslnk_LinkSendData(send_fwd2, 8);
            } else {
                memset(send_fwd1, 0, 8);
                send_fwd1[0] = 1;
                send_fwd1[1] = 1;
                fslnk_LinkSendData(send_fwd1, 8);
                cs->func = 1;
            }
            DF_M1HELP_FadeOut();
            mvss_fade_items(ioObjp, cs);
            cs->f_forward = -1;
        } else if (cs->idx == 5) {
            /* START (asm-only: needs mode != 0) */
            if (!cs->f_selectable_start) {
                Voice_Call(0x3f1, 0x7f);
                return;
            }
            int sh = mvss_form_short(cs->sidx[2]) || play_info.com_num > 0 || play_info.player_ai_num > 0;
            if (sh && noc) {
                mvss_guide(0, 0xe, 0, 0, 0x63);
                mvss_noc_dialog(ioObjp, cs);
                return;
            }
            if (cs->sidx[0] == 0x17) {
                ps2_srand(MenuMng.frame);
                unsigned char cand;
                int again;
                do {
                    cand = mvs_rand_map_id[(unsigned int)ps2_rand() % 17];
                    again = 0;
                    /* dead: this branch has sidx[0] == 0x17, so a no-COM map is never re-rolled (quirk) */
                    if (cs->sidx[0] != 0x17) again = mvss_is_noc(MVSS_MAP_ID[cand]);
                } while ((mvss_form_short(cs->sidx[2]) || play_info.com_num > 0) && again);
                play_info.rand_map_id = cand; /* delay slot of jal rand: before the seed roll */
                play_info.rand_seed = (unsigned char)(ps2_rand() % 256);
            }
            memset(send_start, 0, 8);
            send_start[0] = 1;
            send_start[1] = 3;
            send_start[2] = play_info.rand_map_id;
            send_start[3] = play_info.rand_seed;
            fslnk_LinkSendData(send_start, 8);
            DF_MBGUIDE_Hide();
            play_info.f_ready = 1;
            cs->f_start = -1;
            cs->func = 0; /* delay slot of jal DF_M1HELP_FadeOut */
            DF_M1HELP_FadeOut();
            mvss_fade_items(ioObjp, cs);
        }
        return;
    }

    /* left */
    if (pad->repeat.lt && cs->idx < 5) {
        int i = cs->idx;
        if (i == 1) {
            if (cs->sidx[1] >= 2) {
                Voice_Call(0x3f2, 0x7f);
                cs->sidx[(int)cs->idx]--;
            }
        } else if (i == 3) {
            Voice_Call(0x3e9, 0x7f);
            cs->sidx[3]--;
            if (cs->sidx[3] < cs->min_sidx) cs->sidx[3] = cs->max_sidx;
        } else if (i != 4 && i != 5) {
            Voice_Call(0x3e9, 0x7f);
            cs->sidx[(int)cs->idx]--;
            int j = cs->idx;
            if (cs->sidx[j] < 0) cs->sidx[j] = sidxTable[j] - 1;
        }
        if (cs->idx == 2) __MVSS_ChangeComWindow(cs, 0);
    }
    /* right */
    if (pad->repeat.rt && cs->idx < 5) {
        int i = cs->idx;
        if (i == 1) {
            if (cs->sidx[i] < sidxTable[i]) {
                Voice_Call(0x3f2, 0x7f);
                cs->sidx[(int)cs->idx]++;
            }
        } else if (i == 3) {
            Voice_Call(0x3e9, 0x7f);
            cs->sidx[3]++;
            if (cs->max_sidx < cs->sidx[3]) cs->sidx[3] = cs->min_sidx;
        } else if (i != 4 && i != 5) {
            Voice_Call(0x3e9, 0x7f);
            cs->sidx[(int)cs->idx]++;
            int j = cs->idx;
            if (sidxTable[j] - 1 < cs->sidx[j]) cs->sidx[j] = 0;
        }
        if (cs->idx == 2) __MVSS_ChangeComWindow(cs, 0);
    }
    /* L1 / R1 on the time row: 1 minute / NO LIMIT */
    if (MPad[MPadUser].repeat.l1 && cs->idx == 1) {
        Voice_Call(0x3f2, 0x7f);
        cs->sidx[(int)cs->idx] = 1;
    }
    if (MPad[MPadUser].repeat.r1 && cs->idx == 1) {
        Voice_Call(0x3f2, 0x7f);
        cs->sidx[(int)cs->idx] = 0x10;
    }

    int idx = cs->idx;
    if (prev_sidx[idx] != cs->sidx[idx]) {
        int kind;
        switch (idx) {
        case 3: kind = 0xe; break;
        case 2: kind = 0xc; break;
        case 1: kind = 0xb; break;
        case 0: kind = 0xa; break;
        default: kind = 0; break;
        }
        memset(send_val, 0, 8);
        send_val[0] = 1;
        send_val[1] = kind;
        send_val[2] = cs->sidx[(int)cs->idx];
        fslnk_LinkSendData(send_val, 8);
        if (cs->idx == 2) {
            /* the form change may have clamped the COM count */
            memset(send_val, 0, 8);
            send_val[0] = 1;
            send_val[1] = 0xe;
            send_val[2] = cs->sidx[3];
            fslnk_LinkSendData(send_val, 8);
        }
        __MVSS_ItemSetCursor(ioObjp, prev_idx == cs->idx);
        return;
    }

    pad = &MPad[MPadUser];
    if (pad->repeat.up) {
        Voice_Call(0x3e9, 0x7f);
        cs->idx--;
        if (cs->f_selectable_start) {
            if (cs->idx < 0) cs->idx = 5;
        } else if (cs->idx < 0) {
            cs->idx = 4;
        }
    } else if (pad->repeat.dw) {
        Voice_Call(0x3e9, 0x7f);
        cs->idx++;
        if (cs->f_selectable_start) {
            if (cs->idx >= 6) cs->idx = 0;
        } else if (cs->idx >= 5) {
            cs->idx = 0;
        }
    }
    __MVSS_ItemSetCursor(ioObjp, prev_idx == cs->idx);
}

/* link teardown after a handshake failure: the dispatcher shows the dialog and leaves */
static inline void mvss_handshake_fail(struct stMobj *obj, VssSys *cs, unsigned int msg)
{
    DF_MDIALOG_Open(5, DF_MSTR_GetInfo(MenuMng.language, 1, msg), 0, 0, 0, 0, 1, 0);
    mvss_link_teardown();
    cs->func = 1;
    obj->stat = 3;
    obj->play_func = MVSS_FN(__PlayVsSetUpDispatch);
}

/* play_func while the screen comes up: step 0 / 1 link handshake (offline: skipped), step 2 creates the items once
 * the camera motion is done, step 3 hands over to the input loop */
static void __PlayVsSetUpSystemFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x2c0);
    PS2_STACK(struct _LAYOUT_RET_DATA, ret, 0x60);
    PS2_STACK(char[256], name, 0xb0);
    PS2_STACK(char[128], recv_data, 0x1b0);
    PS2_STACK(char[128], send_data, 0x230);
    PS2_STACK(char[6], bar, 0x2b8);
    static const char send_init[128] = "A-AC3SLCHECK";
    static const char recv_init[128] = "010101010101010101";
    VssSys *cs = mvss_sys(ioObjp);

    /* both blocks are copied in on every call */
    memcpy(send_data, send_init, 128);
    memcpy(recv_data, recv_init, 128);

    switch (cs->step) {
    case 0:
        if (play_info.connected_num != 1) {
            send_data[0] = play_info.play_mode;
            send_data[1] = play_info.player_num;
            fslnk_LinkSendData(send_data, 0x80);
            cs->count = 0;
        }
        cs->step++;
        break;
    case 1: {
        if (play_info.connected_num == 1) {
            cs->step++;
            break;
        }
        /* link handshake (asm-only: no replay links) */
        int c = cs->count;
        cs->count = c + 1;
        if (c >= 0x259) {
            mvss_handshake_fail(ioObjp, cs, 0x30ed1);
            break;
        }
        int n = 0;
        int peers = play_info.connected_num - 1;
        for (int k = 0; k < peers; k++, peers = play_info.connected_num - 1)
            if ((unsigned int)fslnk_LinkRecvDataSize(k) >= 0x80) n++;
        if (n != peers) break;
        if (fslnk_LinkSendDataSize() > 0) break;
        for (int k = play_info.connected_num - 2; k >= 0; k--) {
            fslnk_LinkRecvData(recv_data, k, 0x80);
            if (strcmp(send_data + 2, recv_data + 2) != 0) {
                /* four arguments here (t0 left as it is) */
                gcall_as<int(int, int, int, int)>(GFN(DF_MBGUIDE_Change), 0, 0xe, 0, 0x63);
                mvss_handshake_fail(ioObjp, cs, 0x30ed0);
                return;
            }
        }
        cs->step++;
        break;
    }
    case 2: {
        if (!DF_MVIEW_MotionSync(MViewp)) break;
        play_info.local_player_num = 1;
        if (play_info.play_mode == 1 &&
            !(play_info.self_id == 1 && play_info.connected_num == 2 && play_info.player_num == 3))
            play_info.local_player_num = 2;
        __MVSS_ItemCreate(ioObjp);
        __MVSS_ItemFadeIn(ioObjp, 5, 1);
        __MVSS_ItemSetCursor(ioObjp, 0);
        for (int i = 0; i < 4; i++) DF_MOBJ_SetDrawEnable(cs->apArrow[i], 0);
        for (int i = 0; i < 4; i++) {
            DF_MOBJ_SetDrawEnable(cs->apCursorTrue[i], 0);
            DF_MOBJ_SetDrawEnable(cs->apCursorFalse[i], 0);
        }
        DF_MOBJ_SetDrawEnable(cs->apCursorFalse[0], 1);
        DF_MOBJ_SetDrawEnable(cs->apCursorFalse[1], 1);
        ps2_sprintf_d(name, mvss_fmt_parts10, cs->sidx[2]);
        WindowEx_MoveToLayout(cs->apCursorTrue[0], (char *)LAYOUT_GetLayoutData(0x40a), name, 0);
        WindowEx_MoveToLayout(cs->apCursorFalse[0], (char *)LAYOUT_GetLayoutData(0x40a), name, 0);
        ps2_sprintf_d(name, mvss_fmt_parts10a, cs->sidx[3]);
        WindowEx_MoveToLayout(cs->apCursorTrue[1], (char *)LAYOUT_GetLayoutData(0x40a), name, 0);
        WindowEx_MoveToLayout(cs->apCursorFalse[1], (char *)LAYOUT_GetLayoutData(0x40a), name, 0);
        memcpy(bar, "bar_0", 6);
        memset(&ret, 0, sizeof(ret));
        DF_LO_GetLayoutData((unsigned char *)LAYOUT_GetLayoutData(0x40a), (unsigned char *)bar, 0, &ret);
        int t = cs->sidx[1];
        int x;
        if (t == 0x10) x = ret.Pos.w;
        else x = (int)((float)t * (float)ret.Pos.w / 16.0f);
        struct struct_37ce77ad *b = mvss_spr(cs->pBar);
        b->rect.w = x;
        b->prev_rect.w = x;
        b->next_rect.w = x;
        __MVSS_ChangeComWindow(cs, 1);
        DF_M1HELP_FadeIn();
        cs->step++;
        break;
    }
    case 3:
        ioObjp->play_func = MVSS_FN(__PlayVsSetUpSystem);
        ioObjp->stat = 0;
        break;
    default:
        break;
    }
}

/* constructor (mvse): rows from play_info, flags cleared, scene tag 0x22, key guide, camera motion, and the
 * situation window's eight 16-byte string buffers carved from MenuWorkp. inPlayinfo (&play_info) is unused. */
struct stMobj *DF_MVSS_NewSystem(struct stMobj *inObjp, struct struct_c774638e *inPlayinfo, int inMode)
{
    PS2_FRAME(0x50);
    (void)inPlayinfo;
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    VssSys *cs = mvss_sys(obj);

    obj->type = 0;
    obj->stat = 1;
    cs->mode = inMode;
    if (cs->mode) {
        /* asm-only: mvse always passes 0 */
        cs->prev_form = play_info.vs_form;
        cs->prev_com = play_info.com_num;
        if (play_info.self_id == 0) DF_MBGUIDE_Show();
    }
    cs->mstr_top_idx = 0x18f38;
    cs->idx = 0;
    cs->sync = 0;
    cs->sidx[0] = play_info.map_id;
    cs->sidx[1] = play_info.time_limit;
    cs->sidx[2] = play_info.vs_form;
    cs->sidx[3] = play_info.com_num;
    cs->dont_init_com_sidx = cs->mode == 1;
    cs->f_control = play_info.self_id == 0 ? -1 : 0;
    cs->f_start = 0;
    cs->f_selectable_start = 0;
    cs->f_selchange = 0;
    cs->f_forward = 0;
    play_info.f_ready = 0;
    play_info.f_error = 0;
    play_info.f_back = 0;
    play_info.f_mmdlinkerror = 0;
    play_info.f_setuped = 0;
    play_info.now_page = 0;
    mvss_tag(0x22);
    mvss_guide(0, 1, 2, 0, 0x63);
    DF_MVIEW_MotionStart(MViewp, play_info.connected_num + 0x3b);

    cs->defTBP = MenuWorkp;
    cs->whsp = (struct struct_9fc9058b *)(((uintptr_t)MenuWorkp + 0xf) & ~(uintptr_t)0xf);
    memset(cs->whsp, 0, 0x58);
    cs->whsp->strBuff[0] = (char *)cs->whsp + 0x58;
    memset(cs->whsp->strBuff[0], 0, 0x10);
    for (int i = 1; i < 8; i++) {
        cs->whsp->strBuff[i] = cs->whsp->strBuff[i - 1] + 0x10;
        memset(cs->whsp->strBuff[i], 0, 0x10);
    }
    MenuWorkp = (unsigned char *)cs->whsp->strBuff[7] + 0x10;
    obj->play_func = MVSS_FN(__PlayVsSetUpSystemFadeIn);
    obj->draw_func = MVSS_FN(__DrawVsSetUpSystem);
    obj->count = 0;
    return obj;
}
