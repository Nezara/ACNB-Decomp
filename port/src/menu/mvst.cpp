/* src/menu/mvst.cpp - MVST, the Versus team screen (OLMENU overlay, scene label 0x55): opened from the situation
 * setup screen (mvss) when the battle form play_info.vs_form is not 0. One row per participant (player_num +
 * com_num, max 4) with a coloured "ita" sprite, two cursor windows (selected / unselected), a player label and a
 * team (form 2) or slot (form 1) value, plus a fifth "next" row. One stMobj owns everything; its dt is a
 * struct_8a01b3c1.
 * Group 1 native: the item functions (__MVST_ItemResetCursor / SetCursor / FadeOut / FadeIn / Delete / Create) and
 * __MVST_EnableTeamNext (are the picks legal for the form?). The sprintf calls are newlib's (ps2_sprintf_d: buffer
 * and format in PS2 memory, nodwarf formats, PS2_STACK buffers).
 * Group 2 native: the screen logic (__DrawVsTeamSystem = i.LINK / Ethernet mirror of the remote picks,
 * __PlayVsTeamSetupFadeIn / Setup / SetupFadeOut / Dispatch play states, DF_MVST_NewSystem). Every local action is
 * sent with the interpreted fslnk_LinkSendData (also offline); its buffers sit at their original stack slots. */
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include <string.h>

#ifdef ACNB_SOURCEPORT
char mvst_fmt_parts20e[12] = "parts20e_%d";
#endif

typedef struct struct_8a01b3c1 VstSys; /* dt of the screen object */

static inline VstSys *mvst_sys(struct stMobj *pMobj) { return (VstSys *)&pMobj->dt; }

/* OLMENU 0x1ee4798: multiply colours of the row sprites (0 = none, 1 = red, 2 = blue); copied to the stack per call */
static const struct struct_76b810ec mvst_colour[3] = {{0, 0, 0, 0}, {0xaf, 0x32, 0x32, 0x80}, {0x40, 0x50, 0xa0, 0x80}};

/* the inline "delete" of a string object (f_draw = 0, then f_erase = 1: two separate flag-byte writes) */
static inline void mvst_erase(struct stMobj *p)
{
    p->f_draw = 0;
    p->f_erase = 1;
}

/* colour index per pick: form 1 slots 0-1 red, 2-3 blue; form 2 team 0 red, team 1 blue (lanes 2 / 3 are left
 * unset by the original there; only a remote packet could index them) */
static inline void mvst_color_no(int *color_no)
{
    if (play_info.vs_form == 1) {
        color_no[1] = 1;
        color_no[0] = 1;
        color_no[3] = 2;
        color_no[2] = 2;
    } else {
        color_no[0] = 1;
        color_no[1] = 2;
    }
}

/* hide every selected cursor, show every unselected one, then move each participant's cursors to its pick and
 * recolour its sprite */
static void __MVST_ItemResetCursor(struct stMobj *pMobj)
{
    PS2_FRAME(0x190);
    PS2_STACK(char[256], name, 0x80);
    VstSys *dt = mvst_sys(pMobj);
    int color_no[4] = {0, 0, 0, 0};
    struct struct_76b810ec colour[3];

    for (int i = 0; i < 5; i++) {
        DF_MOBJ_SetDrawEnable(dt->apCursorTrue[i], 0);
        DF_MOBJ_SetDrawEnable(dt->apCursorFalse[i], 1);
    }
    mvst_color_no(color_no);
    for (int i = 0; i < 4; i++) {
        if (!(i < play_info.player_num + play_info.com_num)) continue;
        DF_MOBJ_SetDrawEnable(dt->apCursorFalse[i], 1);
        if (play_info.vs_form == 1) {
            ps2_sprintf_d(name, mvss_fmt_parts10, dt->sidx[i] + i * 4);
            WindowEx_MoveToLayout(dt->apCursorTrue[i], (char *)LAYOUT_GetLayoutData(0x40d), name, 0);
            WindowEx_MoveToLayout(dt->apCursorFalse[i], (char *)LAYOUT_GetLayoutData(0x40d), name, 0);
        } else {
            ps2_sprintf_d(name, mvss_fmt_parts10, dt->sidx[i] + i * 2);
            WindowEx_MoveToLayout(dt->apCursorTrue[i], (char *)LAYOUT_GetLayoutData(0x40f), name, 0);
            WindowEx_MoveToLayout(dt->apCursorFalse[i], (char *)LAYOUT_GetLayoutData(0x40f), name, 0);
        }
        memcpy(colour, mvst_colour, sizeof(colour));
        UnitSprite_SetMultiply(dt->apIta[i], &colour[color_no[(int)dt->sidx[i]]]);
    }
}

/* host only: select row dt->idx (rows past the participants go to the "next" row 4) and move / recolour it.
 * a1 (0 at every call site) is ignored. */
static void __MVST_ItemSetCursor(struct stMobj *pMobj)
{
    PS2_FRAME(0x180);
    PS2_STACK(char[256], name, 0x70);
    VstSys *dt = mvst_sys(pMobj);
    int color_no[4] = {0, 0, 0, 0};
    struct struct_76b810ec colour[3];
    int row;

    if (play_info.self_id != 0) return;
    for (int i = 0; i < 5; i++) {
        DF_MOBJ_SetDrawEnable(dt->apCursorTrue[i], 0);
        DF_MOBJ_SetDrawEnable(dt->apCursorFalse[i], 1);
    }
    row = dt->idx;
    if (!(row < play_info.player_num + play_info.com_num)) row = 4;
    DF_MOBJ_SetDrawEnable(dt->apCursorTrue[row], 1);
    DF_MOBJ_SetDrawEnable(dt->apCursorFalse[row], 0);
    WindowEx_BlinkBegin(dt->apCursorTrue[row]);
    if (row == 4) return;
    mvst_color_no(color_no);
    if (play_info.vs_form == 1) {
        ps2_sprintf_d(name, mvss_fmt_parts10, dt->sidx[row] + dt->idx * 4);
        WindowEx_MoveToLayout(dt->apCursorTrue[row], (char *)LAYOUT_GetLayoutData(0x40d), name, 0);
        WindowEx_MoveToLayout(dt->apCursorFalse[row], (char *)LAYOUT_GetLayoutData(0x40d), name, 0);
    } else {
        ps2_sprintf_d(name, mvss_fmt_parts10, dt->sidx[row] + dt->idx * 2);
        WindowEx_MoveToLayout(dt->apCursorTrue[row], (char *)LAYOUT_GetLayoutData(0x40f), name, 0);
        WindowEx_MoveToLayout(dt->apCursorFalse[row], (char *)LAYOUT_GetLayoutData(0x40f), name, 0);
    }
    memcpy(colour, mvst_colour, sizeof(colour));
    UnitSprite_SetMultiply(dt->apIta[row], &colour[color_no[(int)dt->sidx[row]]]);
}

/* fade out every item of the participants (the "next" row's items too; on a client the unselected "next" window
 * was never created and is passed as NULL) */
static void __MVST_ItemFadeOut(struct stMobj *pMobj)
{
    PS2_FRAME(0x60);
    VstSys *dt = mvst_sys(pMobj);
    int players = play_info.player_num + play_info.com_num;
    int nvalue, values; /* apParts20e: loop bound, live entries */

    if (play_info.vs_form == 1) {
        nvalue = 16;
        values = players * 4;
    } else {
        nvalue = 8;
        values = players * 2;
    }
    for (int i = 0; i < 5; i++) {
        if (dt->apIta[i] == 0) continue;
        if ((i == 4 && play_info.self_id == 0) || i < players) UnitSprite_SetFadeOut(dt->apIta[i]);
    }
    for (int i = 0; i < 5; i++) {
        if (dt->apParts20d[i] == 0) continue;
        if ((i == 4 && play_info.self_id == 0) || i < players) DF_MOBJ_SetFadeOut(dt->apParts20d[i]);
    }
    for (int i = 0; i < nvalue; i++) {
        if (dt->apParts20e[i] != 0 && i < values) DF_MOBJ_SetFadeOut(dt->apParts20e[i]);
    }
    for (int i = 0; i < 5; i++) {
        if (dt->apCursorTrue[i] == 0) continue;
        if (i == 4 || i < players) {
            WindowEx_SetFadeOut(dt->apCursorTrue[i]);
            WindowEx_BlinkEnd(dt->apCursorTrue[i]);
            WindowEx_SetFadeOut(dt->apCursorFalse[i]);
        }
    }
}

/* show and fade in every item of the participants (only the selected cursor windows, blinking) */
static void __MVST_ItemFadeIn(struct stMobj *pMobj)
{
    PS2_FRAME(0x60);
    VstSys *dt = mvst_sys(pMobj);
    int players, nvalue, values;

    if (dt->apIta[0] == 0) return;
    players = play_info.player_num + play_info.com_num;
    if (play_info.vs_form == 1) {
        nvalue = 16;
        values = players * 4;
    } else {
        nvalue = 8;
        values = players * 2;
    }
    for (int i = 0; i < 5; i++) {
        if ((i == 4 && play_info.self_id == 0) || i < players) {
            DF_MOBJ_SetDrawEnable(dt->apIta[i], 1);
            UnitSprite_SetFadeIn(dt->apIta[i]);
        }
    }
    for (int i = 0; i < 5; i++) {
        if ((i == 4 && play_info.self_id == 0) || i < players) {
            DF_MOBJ_SetDrawEnable(dt->apParts20d[i], 1);
            DF_MOBJ_SetFadeIn(dt->apParts20d[i]);
        }
    }
    for (int i = 0; i < nvalue; i++) {
        if (i < values) {
            DF_MOBJ_SetDrawEnable(dt->apParts20e[i], 1);
            DF_MOBJ_SetFadeIn(dt->apParts20e[i]);
        }
    }
    for (int i = 0; i < 5; i++) {
        if (i == 4 || i < players) {
            DF_MOBJ_SetDrawEnable(dt->apCursorTrue[i], 1);
            WindowEx_SetFadeIn(dt->apCursorTrue[i]);
            WindowEx_BlinkBegin(dt->apCursorTrue[i]);
        }
    }
}

/* delete every item (pops the scene label ItemCreate pushed) */
static void __MVST_ItemDelete(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    VstSys *dt = mvst_sys(pMobj);

    if (dt->apIta[0]) DF_MBACK_PopSceneLabel();
    for (int i = 0; i < 5; i++) {
        UnitSprite_Delete(dt->apIta[i]);
        dt->apIta[i] = 0;
    }
    for (int i = 0; i < 5; i++) {
        if (dt->apParts20d[i]) {
            mvst_erase(dt->apParts20d[i]);
            dt->apParts20d[i] = 0;
        }
    }
    for (int i = 0; i < 16; i++) {
        if (dt->apParts20e[i]) {
            mvst_erase(dt->apParts20e[i]);
            dt->apParts20e[i] = 0;
        }
    }
    for (int i = 0; i < 5; i++) {
        WindowEx_Delete(dt->apCursorTrue[i]);
        dt->apCursorTrue[i] = 0;
        WindowEx_Delete(dt->apCursorFalse[i]);
        dt->apCursorFalse[i] = 0;
    }
}

/* create every item once, hidden: row sprites, player labels, pick values, cursor windows (form 1 = four slots,
 * layouts 0x40c / 0x40d; else two teams, layouts 0x40e / 0x40f) */
static void __MVST_ItemCreate(struct stMobj *pMobj)
{
    PS2_FRAME(0x310);
    PS2_STACK(char[256], name1, 0x1f0);
    PS2_STACK(char[256], name2, 0xb0);
    VstSys *dt = mvst_sys(pMobj);
    static const char *const ita_names1[5] = {"4taku_0", "4taku_1", "4taku_2", "4taku_3", "banner_0"};
    static const char *const ita_names2[5] = {"2taku_0", "2taku_1", "2taku_2", "2taku_3", "banner_0"};
    static const char *const win_names1[5] = {"parts10_0", "parts10_4", "parts10_8", "parts10_12", "parts10a_0"};
    static const char *const win_names2[5] = {"parts10_0", "parts10_2", "parts10_4", "parts10_6", "parts10a_0"};
    const char *const *ita_names, *const *win_names;
    char *name;
    int players, lay_spr, lay, id;

    if (dt->apIta[0] != 0) return;
    DF_MBACK_PushSceneLabel((enum_5124d09c)0x55);
    players = play_info.player_num + play_info.com_num;
    if (play_info.vs_form == 1) {
        ita_names = ita_names1;
        win_names = win_names1;
        name = name1;
        lay_spr = 0x40c;
        lay = 0x40d;
    } else {
        ita_names = ita_names2;
        win_names = win_names2;
        name = name2;
        lay_spr = 0x40e;
        lay = 0x40f;
    }

    for (int i = 0; i < 5; i++) {
        dt->apIta[i] = UnitSprite_New(pMobj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(lay_spr), (char *)ita_names[i]);
        DF_MOBJ_SetDrawEnable(dt->apIta[i], 0);
    }
    /* player labels: players first, then COMs, then the "next" banner text */
    for (int i = 0; i < 5; i++) {
        if (i < 4) {
            if (i < play_info.player_num) id = 0x18f7e + i;
            else id = 0x18f82 + i - play_info.player_num;
        } else {
            id = 0x18f60;
        }
        ps2_sprintf_d(name, mvss_fmt_parts20d, i + 5);
        dt->apParts20d[i] =
            DF_MOBJ_NewStringFromLayoutName(pMobj, 0, id, 0, (char *)LAYOUT_GetLayoutData(lay), name, 0, 0, 0);
        DF_MOBJ_SetDrawEnable(dt->apParts20d[i], 0);
    }
    /* pick values: form 1 = slot strings 0x1a9d2.. per slot of each row, else team strings 0x1a9d3 / 0x1a9d5 */
    if (play_info.vs_form == 1) {
        for (int i = 0; i < 16; i++) {
            ps2_sprintf_d(name, mvst_fmt_parts20e, i);
            char *layp = (char *)LAYOUT_GetLayoutData(lay);
            dt->apParts20e[i] = DF_MOBJ_NewStringFromLayoutName(pMobj, 0, 0x1a9d2 + i % 4, 0, layp, name, 0, 0, 0);
            DF_MOBJ_SetDrawEnable(dt->apParts20e[i], 0);
        }
    } else {
        for (int i = 0; i < 8; i++) {
            ps2_sprintf_d(name, mvst_fmt_parts20e, i);
            id = 0x1a9d3 + (i % 2 != 0) * 2;
            char *layp = (char *)LAYOUT_GetLayoutData(lay);
            dt->apParts20e[i] = DF_MOBJ_NewStringFromLayoutName(pMobj, 0, id, 0, layp, name, 0, 0, 0);
            DF_MOBJ_SetDrawEnable(dt->apParts20e[i], 0);
        }
    }
    for (int i = 0; i < 5; i++) {
        if (i == 4 || i < players) {
            dt->apCursorTrue[i] = WindowEx_NewFromLayout(pMobj, (_WINDOWEX_TYPE)0xb, (char *)LAYOUT_GetLayoutData(lay),
                                                         (char *)win_names[i]);
            DF_MOBJ_SetDrawEnable(dt->apCursorTrue[i], 0);
        }
    }
    for (int i = 0; i < 5; i++) {
        if ((i == 4 && play_info.self_id == 0) || i < players) {
            dt->apCursorFalse[i] = WindowEx_NewFromLayout(pMobj, (_WINDOWEX_TYPE)0x24,
                                                          (char *)LAYOUT_GetLayoutData(lay), (char *)win_names[i]);
            DF_MOBJ_SetDrawEnable(dt->apCursorFalse[i], 0);
            WindowEx_BlinkEnd(dt->apCursorFalse[i]);
        }
    }
}

/* are the picks dtp[0..mn) legal? mode 1 (four slots): all distinct, slot 0 and slot 2 taken (-1 / -2 / -3);
 * mode 2 (two teams): both teams used, neither with 3+ members (-4 .. -7); 1 = ok (also for any other mode) */
int __MVST_EnableTeamNext(char *dtp, int mn, int mode)
{
    int n1 = 0, n0 = 0;

    if (mode == 2) {
        for (int i = 0; i < mn; i++) {
            if (dtp[i] == 0) n0++;
            if (dtp[i] == 1) n1++;
        }
        if (n0 == 0) return -4;
        if (n1 == 0) return -5;
        if (n0 >= 3) return -6;
        if (n1 >= 3) return -7;
        return 1;
    }
    if (mode == 1) {
        int has0 = 0, has2 = 0;
        for (int i = 0; i < mn; i++) {
            char v = dtp[i];
            if (v == 0) has0 = 1;
            if (v == 2) has2 = 1;
            for (int j = i + 1; j < mn; j++) {
                if (v == dtp[j]) return -1;
            }
        }
        if (!has0) return -2;
        if (!has2) return -3;
        return 1;
    }
    return 1;
}

/* ---- group 2: screen logic ---- */

#define MVST_FN(f) ((void (*)(void))GFN(f))

/* DF_MBGUIDE_Change / DF_MBACK_ChangeTag / DF_MBACK_SetFadeOutNX are void(void) in DWARF but read a0..t0 / a0 */
static inline void mvst_guide(int a, int b, int c, int d, int e)
{
    gcall_as<int(int, int, int, int, int)>(GFN(DF_MBGUIDE_Change), a, b, c, d, e);
}
static inline void mvst_tag(int tag) { gcall_as<void(int)>(GFN(DF_MBACK_ChangeTag), tag); }
static inline void mvst_fadeout_nx(int a) { gcall_as<void(int)>(GFN(DF_MBACK_SetFadeOutNX), a); }

/* link shutdown: five fslnk calls, each followed by one vblank wait */
static inline void mvst_link_teardown(void)
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

/* leave the screen: fade the situation window (host cancel, or after a link error) and the items, go to the
 * fade-out state. The test is compiled with func usually just stored (quirk: kept as written). */
static inline void mvst_leave(struct stMobj *obj, VstSys *dt)
{
    if ((dt->func == 0 && play_info.self_id == 0) || dt->f_error) DF_MVS_SetSituationWindowFadeOut(dt->whsp);
    __MVST_ItemFadeOut(obj);
    dt->step = 0;
    obj->play_func = MVST_FN(__PlayVsTeamSetupFadeOut);
    obj->stat = 2;
}

/* link error: go to Dispatch, which shows the error and shuts the link down */
static inline void mvst_link_error(struct stMobj *obj, VstSys *dt, int func)
{
    dt->step = 0;
    dt->func = (char)func;
    obj->play_func = MVST_FN(__PlayVsTeamDispatch);
    obj->draw_func = 0;
    obj->stat = 3;
}

/* draw_func: offline nothing; on a link, run the link and apply the peers' packets {2, kind, idx, value}
 * (kind 0xf = pick changed, 1 = host pressed "next", 2 = cancel; the idx is not bounds-checked: quirk) */
static void __DrawVsTeamSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x90);
    PS2_STACK(char[8], wkd, 0x88);
    VstSys *dt = mvst_sys(ioObjp);
    int cn, i;

    if (play_info.connected_num == 1) return;
    if (fslnk_LinkProc() < 0) {
        mvst_link_error(ioObjp, dt, 0x63);
        return;
    }
    cn = play_info.connected_num;
    if (cn != fslnk_GetConnectedNum() + 1) {
        mvst_link_error(ioObjp, dt, 0x64);
        return;
    }
    i = 0;
    if (!(0 < cn - 1)) return;
    do {
        if ((unsigned int)fslnk_LinkRecvDataSize(i) >= 8) {
            fslnk_LinkRecvData(wkd, i, 8);
            if (wkd[0] == 2) {
                switch (wkd[1]) {
                case 0xf:
                    dt->sidx[(int)wkd[2]] = wkd[3]; /* delay slot of jal __MVST_ItemSetCursor */
                    __MVST_ItemSetCursor(ioObjp);
                    __MVST_ItemResetCursor(ioObjp);
                    break;
                case 1:
                    dt->func = 1;
                    mvst_leave(ioObjp, dt);
                    break;
                case 2:
                    dt->func = 0;
                    mvst_leave(ioObjp, dt);
                    break;
                default: /* 3, 4 and anything else: ignored */
                    break;
                }
            }
        }
        i++;
    } while (i < play_info.connected_num - 1);
}

/* the shared body of the link-error states 0x63 / 0x64: step 0 opens the message and shuts the link down, step 1
 * waits for the dialog and leaves */
static inline void mvst_dispatch_link(struct stMobj *obj, VstSys *dt, unsigned int msg, int lost_peer)
{
    if (dt->step == 0) {
        if (DF_MDIALOG_Type() == 0) {
            mvst_guide(0, 0xe, 0, 0, 0x63);
            DF_MDIALOG_Open(5, DF_MSTR_GetInfo(MenuMng.language, 1, msg), 0, 0, 0, 0, 0, 0);
            mvst_link_teardown();
            dt->step = 1;
        } else if (DF_MDIALOG_Sync() == 2) {
            DF_MDIALOG_Close();
        }
        return;
    }
    if (dt->step != 1) return;
    if ((DF_MDIALOG_Sync() & 4) == 0) return;
    mvst_guide(0, 0, 0, 0, 0x63);
    dt->func = 0;
    if (lost_peer) {
        dt->f_error = 1;
        play_info.f_ready = -1;
    } else {
        play_info.f_ready = -1;
        dt->func = 0; /* stored twice in the original */
    }
    obj->draw_func = 0;
    mvst_leave(obj, dt);
}

/* play_func (stat 3): wait for the child screen (func 0), the error dialog (func 1) or a link error (0x63 / 0x64) */
static void __PlayVsTeamDispatch(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    VstSys *dt = mvst_sys(ioObjp);

    switch (dt->func) {
    case 0: {
        struct stMobj *c = dt->csysp;
        /* the original reads stat before the NULL test (quirk) */
        if ((c ? c : (struct stMobj *)PS2_NULL_MEM())->stat != 5) return;
        if (c) {
            mvst_erase(c);
            dt->csysp = 0;
        }
        if (play_info.f_ready != 0) {
            dt->func = 0;
            ioObjp->play_func = MVST_FN(__PlayVsTeamSetupFadeOut);
            ioObjp->stat = 2;
            return;
        }
        dt->step = 0;
        ioObjp->stat = 1;
        ioObjp->count = 0xf;
        ioObjp->play_func = MVST_FN(__PlayVsTeamSetupFadeIn);
        ioObjp->draw_func = MVST_FN(__DrawVsTeamSystem);
        return;
    }
    case 1: {
        int r = DF_MDIALOG_Sync();
        if ((r & 4) == 0) return;
        if (r == 4 || r == 0x24 || r == 0x14) {
            ioObjp->stat = 0;
            ioObjp->play_func = MVST_FN(__PlayVsTeamSetup);
        }
        return;
    }
    case 0x63:
        mvst_dispatch_link(ioObjp, dt, 0x30ed0, 0);
        return;
    case 0x64:
        mvst_dispatch_link(ioObjp, dt, 0x30ed3, 1);
        return;
    default:
        return;
    }
}

/* the screen is done: give the menu work area back and stop */
static inline void mvst_finish(struct stMobj *obj, VstSys *dt)
{
    MenuWorkp = dt->defTBP;
    obj->play_func = 0;
    obj->draw_func = 0;
    obj->stat = 5;
}

/* play_func (stat 2): delete the items, then back to the setup screen (func 0) or on to the AC setup (func 1, after
 * writing play_info.team_condition from the picks) */
static void __PlayVsTeamSetupFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    /* DWARF char count[2], indexed with team_condition & 2: count[2] (sp+0x3e) is never zeroed (quirk) */
    PS2_STACK(char[4], count, 0x3c);
    VstSys *dt = mvst_sys(ioObjp);
    struct stMobj *child;
    int i;

    if (DF_MDIALOG_Type()) {
        if (DF_MDIALOG_Sync() == 2) DF_MDIALOG_Close();
        return;
    }
    if (dt->step == 1) {
        if (!DF_MBACK_Sync()) return;
        mvst_finish(ioObjp, dt);
        return;
    }
    __MVST_ItemDelete(ioObjp);
    if ((dt->func == 0 && play_info.self_id == 0) || dt->f_error) DF_MVS_DeleteSituationWindow(dt->whsp);
    if (dt->func == 0) {
        if (play_info.f_ready == 0) {
            mvst_tag(0x22);
            mvst_guide(0, 1, 2, 0, 0x63);
        } else if (dt->f_error) {
            mvst_guide(0, 0, 0, 0, 0x63);
            mvst_fadeout_nx(0);
            Bgm_FadeOutStart();
            dt->step = 1;
            return;
        }
        mvst_finish(ioObjp, dt);
        return;
    }
    if (dt->func != 1) return;
    if (play_info.vs_form == 2) {
        count[0] = 0;
        count[1] = 0;
        for (i = 0; i < play_info.player_num + play_info.com_num; i++)
            play_info.team_condition[i] = dt->sidx[i] ? 2 : 1;
        for (i = 0; i < play_info.player_num + play_info.com_num; i++) {
            char *c = &count[(signed char)(play_info.team_condition[i] & 2)];
            if (*c == 0) {
                play_info.team_condition[i] = play_info.team_condition[i] | 0x10;
                *c = 1;
            }
        }
    } else {
        for (i = 0; i < play_info.player_num + play_info.com_num; i++) {
            int v = dt->sidx[i];
            play_info.team_condition[i] = (char)((v < 2 ? 1 : 2) | (v % 2 == 0 ? 0x10 : 0));
        }
    }
    {
        int cn = play_info.connected_num;
        int sid = play_info.self_id;
        if (sid == cn - 1 && !(cn < 3) &&
            play_info.player_num < (play_info.play_mode + 1) * (sid + 1) - play_info.play_mode)
            child = DF_MVSW2_NewSystem(ioObjp, dt->whsp);
        else
            child = DF_MVSG_NewSystem(ioObjp, dt->play_info, dt->whsp);
    }
    dt->csysp = child;
    dt->func = 0;
    ioObjp->play_func = MVST_FN(__PlayVsTeamDispatch);
    ioObjp->draw_func = 0;
    ioObjp->stat = 3;
}

/* send the 8-byte packet {2, kind, a, b} (zeroed first) */
static inline void mvst_send(char *buf, int kind, int a, int b, int nab)
{
    memset(buf, 0, 8);
    buf[0] = 2;
    buf[1] = (char)kind;
    if (nab) {
        buf[2] = (char)a;
        buf[3] = (char)b;
    }
    fslnk_LinkSendData(buf, 8);
}

/* play_func (stat 0): key guide, cancel, "next", pick changes (left / right) and row moves (up / down) */
static void __PlayVsTeamSetup(struct stMobj *ioObjp)
{
    PS2_FRAME(0x70);
    PS2_STACK(char[8], send_cancel, 0x60);
    PS2_STACK(char[8], send_next, 0x58);
    PS2_STACK(char[8], send_lt, 0x50);
    PS2_STACK(char[8], send_rt, 0x48);
    VstSys *dt = mvst_sys(ioObjp);
    char prev_sidx[4];
    int players, idx;

    memcpy(prev_sidx, dt->sidx, 4);
    if (MenuMng.stat == 0xc) goto cancel;
    if (play_info.self_id == 0) {
        if (MenuKeyGuide_IsShown()) return;
        if (MPad[MPadUser].start == 1) {
            MenuKeyGuide_Show(0x32);
            return;
        }
    }
    if (MPad[MPadUser].Rrt == 1) {
        Voice_Call(0x3eb, 0x7f);
    cancel:
        dt->func = 0;
        mvst_leave(ioObjp, dt);
        mvst_send(send_cancel, 2, 0, 0, 0);
        return;
    }
    if (play_info.self_id != 0) {
        __MVST_ItemSetCursor(ioObjp);
        return;
    }
    if (MPad[MPadUser].Rdw == 1) {
        players = play_info.player_num + play_info.com_num;
        if (dt->idx == players) {
            int ok = __MVST_EnableTeamNext(dt->sidx, players, play_info.vs_form);
            if (ok > 0) {
                Voice_Call(0x3ea, 0x7f);
                dt->func = 1;
                mvst_leave(ioObjp, dt);
                mvst_send(send_next, 1, 0, 0, 0);
                return;
            }
            DF_MDIALOG_Open(5, DF_MSTR_GetInfo(MenuMng.language, 1, 0x30e6b - ok), 0, 0, 0, 0, 1, 0);
            dt->func = 1;
            ioObjp->stat = 3;
            ioObjp->play_func = MVST_FN(__PlayVsTeamDispatch);
            /* falls through to the key handling (idx == players there) */
        }
    }
    if (MPad[MPadUser].repeat.lt) {
        idx = dt->idx;
        if (idx < play_info.player_num + play_info.com_num) {
            char *p;
            Voice_Call(0x3e9, 0x7f);
            p = &dt->sidx[idx];
            (*p)--;
            if (*p < 0) *p = (play_info.vs_form == 2 ? 2 : 4) - 1;
        }
        idx = dt->idx;
        if (idx < play_info.player_num + play_info.com_num && prev_sidx[idx] != dt->sidx[idx])
            mvst_send(send_lt, 0xf, dt->idx, dt->sidx[(int)dt->idx], 1);
        __MVST_ItemSetCursor(ioObjp);
        return;
    }
    if (MPad[MPadUser].repeat.rt) {
        idx = dt->idx;
        if (idx < play_info.player_num + play_info.com_num) {
            char *p;
            Voice_Call(0x3e9, 0x7f);
            p = &dt->sidx[idx];
            (*p)++;
            if (!(*p < (play_info.vs_form == 2 ? 2 : 4))) *p = 0;
        }
        idx = dt->idx;
        if (idx < play_info.player_num + play_info.com_num && prev_sidx[idx] != dt->sidx[idx])
            mvst_send(send_rt, 0xf, dt->idx, dt->sidx[(int)dt->idx], 1);
        __MVST_ItemSetCursor(ioObjp);
        return;
    }
    if (MPad[MPadUser].repeat.up) {
        Voice_Call(0x3e9, 0x7f);
        dt->idx--;
        if (dt->idx < 0) dt->idx = (char)(play_info.player_num + play_info.com_num);
        __MVST_ItemSetCursor(ioObjp);
        return;
    }
    if (MPad[MPadUser].repeat.dw) {
        Voice_Call(0x3e9, 0x7f);
        dt->idx++;
        if (play_info.player_num + play_info.com_num < dt->idx) dt->idx = 0;
        __MVST_ItemSetCursor(ioObjp);
        return;
    }
    __MVST_ItemSetCursor(ioObjp);
}

/* play_func (stat 1): two frames of wait, then (once the view motion is done) create and fade in the items; step 3
 * goes to Setup. step is not initialised by NewSystem (stale pool memory, quirk): other values do nothing. */
static void __PlayVsTeamSetupFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    VstSys *dt = mvst_sys(ioObjp);

    switch (dt->step) {
    case 0:
    case 1:
        dt->step = dt->step + 1;
        return;
    case 2:
        if (!DF_MVIEW_MotionSync(MViewp)) return;
        DF_MVS_SetSituationWindow(ioObjp, &dt->whsp);
        __MVST_ItemCreate(ioObjp);
        __MVST_ItemFadeIn(ioObjp);
        __MVST_ItemResetCursor(ioObjp);
        dt->step = dt->step + 1;
        return;
    case 3:
        ioObjp->play_func = MVST_FN(__PlayVsTeamSetup);
        ioObjp->stat = 0;
        return;
    default:
        return;
    }
}

/* build the team screen object (func / step / most of dt stay as the pool slot left them: quirk) */
struct stMobj *DF_MVST_NewSystem(struct stMobj *inObjp, struct struct_c774638e *inPlayinfo,
                                 struct struct_9fc9058b *inWhp)
{
    PS2_FRAME(0x50);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    VstSys *dt;

    if (!obj) return 0;
    dt = mvst_sys(obj);
    obj->type = 0;
    obj->stat = 1;
    dt->mstr_top_idx = 0x18f75;
    dt->idx = 0;
    dt->f_error = 0;
    dt->play_info = inPlayinfo;
    dt->whsp = inWhp;
    if (play_info.vs_form == 2) {
        dt->sidx[0] = 0;
        dt->sidx[1] = 1;
        dt->sidx[2] = 0;
        dt->sidx[3] = 1;
    } else {
        dt->sidx[0] = 0;
        dt->sidx[1] = 2;
        dt->sidx[2] = 1;
        dt->sidx[3] = 3;
    }
    dt->sstp[0] = 0;
    dt->sstp[1] = 0;
    dt->sstp[2] = 0;
    dt->sstp[3] = 0;
    mvst_tag(0x23);
    if (play_info.self_id == 0) mvst_guide(0, 1, 2, 0, 0x63);
    else mvst_guide(0, 0, 2, 0, 0x63);
    dt->defTBP = MenuWorkp;
    obj->play_func = MVST_FN(__PlayVsTeamSetupFadeIn);
    obj->draw_func = MVST_FN(__DrawVsTeamSystem);
    obj->count = 0;
    return obj;
}
