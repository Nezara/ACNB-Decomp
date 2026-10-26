/* src/menu/mvsr.cpp - MenuVsr, the entry screen of Versus mode (OLMENU): three rows ("ita" unit sprites with the
 * parts20b labels), a cursor window, the scene-name string and the i.LINK logo sprite with a 1-pixel shadow. Row 0
 * starts the local / i.LINK Versus entry child (DF_MVSE_NewSystem, mvse.cpp), rows 1 / 2 re-init the IOP for the
 * Ethernet modes and enter MenuEth / the linked MVSE. One stMobj (type 0, dt = struct_83c59660) runs the phase machine
 * MVSR_PHASE_ENUM (1 main, 2 proceed, 3 cancel / fade-out, 4 dispatch, 5 wait for the error dialog) from its play_func
 * __MenuVsr_Play; the parent (mmanage) waits for stat 5. Draw / Pkt are empty.
 * DF_M1HELP / MBGUIDE / MBACK, MenuKeyGuide, MenuEth, fslnk and the Iop / SDK calls stay interpreted. The sprintf calls
 * are newlib's (ps2_sprintf_d: buffer PS2_STACK, formats nodwarf).
 * Original quirks kept: three DF_MVSE_NewSystem calls pass mode 1 only through a register left by a delay slot (written
 * as an explicit 1 here); MenuVsr_NewSystem's mode-1 arm falls into the flow tests (Proceed may run twice);
 * apParts20b[2] is never created; row 1 sets flagIOP only when the IOP lacks 0x800, row 2 always sets 0x400. */
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include <string.h>

#ifdef ACNB_SOURCEPORT
char mvsr_fmt_parts20b[12] = "parts20b_%d";
char mvsr_parts10_0[10] = "parts10_0";
char mvsr_parts20b_2[11] = "parts20b_2";
char mvsr_fmt_parts10[11] = "parts10_%d";
char mvsr_fmt_ita[7] = "ita_%d";
#endif

static inline struct struct_83c59660 *mvsr_dt(struct stMobj *o) { return (struct struct_83c59660 *)&o->dt; }

/* the DWARF bit-field "delete" pair: two read-modify-write byte stores */
static inline void mvsr_erase(struct stMobj *o)
{
    o->f_draw = 0;
    o->f_erase = 1;
}

/* DF_MBGUIDE_Change is int(void) in DWARF but reads a0..t0 */
static inline void mvsr_guide(int a, int b, int c, int d, int e)
{
    gcall_as<int(int, int, int, int, int)>(GFN(DF_MBGUIDE_Change), a, b, c, d, e);
}

/* the inline fade-out block (5 copies in the original) */
static inline void mvsr_fade_out_all(struct struct_83c59660 *dt)
{
    for (int i = 0; i < 3; i++) {
        UnitSprite_SetFadeOut(dt->apIta[i]);
        if (i < 2) DF_MOBJ_SetFadeOut(dt->apParts20b[i]);
    }
    WindowEx_SetFadeOut(dt->pCursor);
    WindowEx_BlinkEnd(dt->pCursor);
    DF_MOBJ_SetFadeOut(dt->pSceneStr);
    DF_MOBJ_SetFadeOut(dt->pIlinkTexShadow);
    DF_MOBJ_SetFadeOut(dt->pIlinkTex);
    mvsr_guide(0, 0, 0, 0, 0x63);
}

/* BG / BGM fade when no Versus game is ready (Back, row 1 / 2) */
static inline void mvsr_back_fade(void)
{
    if (!play_info.f_ready) {
        gcall_as<void(int)>(GFN(DF_MBACK_SetFadeOutNX), 0);
        Bgm_FadeOutStart();
        DF_MBGUIDE_Hide();
    }
}

static inline void mvsr_delete_child(struct struct_83c59660 *dt)
{
    if (dt->pChild) {
        mvsr_erase(dt->pChild);
        dt->pChild = 0;
    }
}

/* back to the main screen (cursor 0): child cancelled, error dialog closed, fresh entry */
static inline void mvsr_main_enter(struct stMobj *pMobj, struct struct_83c59660 *dt)
{
    pMobj->stat = 1;
    pMobj->count = 0;
    dt->phase = 1;
    dt->cursor = 0;
    Bgm_Play();
    DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x18fa7 + dt->cursor));
    DF_M1HELP_FadeIn();
    mvsr_guide(0, 1, 2, 0, 0x63);
    __MVSR_ItemFadeIn(pMobj);
}

static void __MVSR_ItemFadeIn(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_83c59660 *dt = mvsr_dt(pMobj);
    for (int i = 0; i < 3; i++) {
        DF_MOBJ_SetDrawEnable(dt->apIta[i], 1);
        UnitSprite_SetFadeIn(dt->apIta[i]);
        if (i < 2) {
            DF_MOBJ_SetDrawEnable(dt->apParts20b[i], 1);
            DF_MOBJ_SetFadeIn(dt->apParts20b[i]);
        }
    }
    DF_MOBJ_SetDrawEnable(dt->pCursor, 1);
    WindowEx_SetFadeIn(dt->pCursor);
    WindowEx_BlinkBegin(dt->pCursor);
    DF_MOBJ_SetDrawEnable(dt->pSceneStr, 1);
    DF_MOBJ_SetFadeIn(dt->pSceneStr);
    DF_MOBJ_SetDrawEnable(dt->pIlinkTexShadow, 1);
    DF_MOBJ_SetFadeIn(dt->pIlinkTexShadow);
    DF_MOBJ_SetDrawEnable(dt->pIlinkTex, 1);
    DF_MOBJ_SetFadeIn(dt->pIlinkTex);
}

static void __MVSR_ItemDelete(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_83c59660 *dt = mvsr_dt(pMobj);
    for (int i = 0; i < 3; i++) {
        UnitSprite_Delete(dt->apIta[i]);
        dt->apIta[i] = 0;
        if (i < 2 && dt->apParts20b[i]) {
            mvsr_erase(dt->apParts20b[i]);
            dt->apParts20b[i] = 0;
        }
    }
    WindowEx_Delete(dt->pCursor);
    dt->pCursor = 0;
    if (dt->pSceneStr) {
        mvsr_erase(dt->pSceneStr);
        dt->pSceneStr = 0;
    }
    if (dt->pIlinkTexShadow) {
        mvsr_erase(dt->pIlinkTexShadow);
        dt->pIlinkTexShadow = 0;
    }
    if (dt->pIlinkTex) {
        mvsr_erase(dt->pIlinkTex);
        dt->pIlinkTex = 0;
    }
}

static void __MVSR_ItemCreate(struct stMobj *pMobj, int mode)
{
    PS2_FRAME(0x1e0);
    PS2_STACK(char[0x100], name, 0xd0);
    PS2_STACK(int[4], uv, 0xc0);                          /* OLMENU 0x1ee4280 */
    PS2_STACK(struct _LAYOUT_RET_DATA, lo, 0x70);         /* layout record, reused for both i.LINK sprites */
    PS2_STACK(struct struct_76b810ec, col, 0x1dc);        /* word 0x808d8672 (main sdata 0x27c360) */
    struct struct_83c59660 *dt = mvsr_dt(pMobj);

    for (int i = 0; i < 3; i++) {
        ps2_sprintf_d(name, mvsr_fmt_ita, i);
        dt->apIta[i] = UnitSprite_New(pMobj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x406), name);
        DF_MOBJ_SetDrawEnable(dt->apIta[i], 0);
        if (i < 2) {
            ps2_sprintf_d(name, mvsr_fmt_parts20b, i);
            dt->apParts20b[i] = DF_MOBJ_NewStringFromLayoutName(pMobj, 0, 0x18fa7 + i, 0,
                                                                (char *)LAYOUT_GetLayoutData(0x406), name, 0, 0, 0);
            DF_MOBJ_SetDrawEnable(dt->apParts20b[i], 0);
        }
    }
    ps2_sprintf_d(name, mvsr_parts10_0, 0); /* format without '%': the original's a2 is stale and unread */
    dt->pCursor = WindowEx_NewFromLayout(pMobj, (_WINDOWEX_TYPE)0xb, (char *)LAYOUT_GetLayoutData(0x406), name);
    DF_MOBJ_SetDrawEnable(dt->pCursor, 0);
    dt->pSceneStr = MenuUtil_NewSceneNameString(pMobj, 0x195c8);
    DF_MOBJ_SetDrawEnable(dt->pSceneStr, 0);

    uv[0] = 0x5e;
    uv[1] = 0xa0;
    uv[2] = 0xb2;
    uv[3] = 0xae;
    col.r = 0x72;
    col.g = 0x86;
    col.b = 0x8d;
    col.a = 0x80;
    memset(&lo, 0, 0x50);
    ps2_sprintf_d(name, mvsr_parts20b_2, 0); /* no '%' either */
    DF_LO_GetLayoutData((unsigned char *)LAYOUT_GetLayoutData(0x407), (unsigned char *)name, 0, &lo);
    lo.Pos.w = 0x54;
    lo.Pos.h = 0xe;
    lo.Pos.x++;
    lo.Pos.y++;
    dt->pIlinkTexShadow = DF_MOBJ_NXNewSprite(pMobj, 0x3ec, (struct stMenuRect *)&lo.Pos, uv, &col, 0, 0, 0, 2);
    DF_MOBJ_SetDrawEnable(dt->pIlinkTexShadow, 0);
    lo.Pos.x--;
    lo.Pos.y--;
    dt->pIlinkTex = DF_MOBJ_NXNewSprite(pMobj, 0x3ec, (struct stMenuRect *)&lo.Pos, uv, &col, 0, 0, 0, 1);
    DF_MOBJ_SetDrawEnable(dt->pIlinkTex, 0);

    gcall_as<void(int, int)>(GFN(DF_M1HELP_SetWindow), 0, 0x280);
    if (mode == 1 || play_info.vsmenu_flow_id == 1 || play_info.vsmenu_flow_id == 2)
        DF_M1HELP_SetString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x18fdd));
    else
        DF_M1HELP_SetString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x18fa7));
}

/* the original zeroes ___ILINKMNG / ___ILINKSYNC before every MVSE launch */
static inline void mvsr_link_clear(void)
{
    ZeroMemory(&__ILINKMNG, 0x37780);
    ZeroMemory(&__ILINKSYNC, 0x80);
}

static void __MenuVsr_PlayProceed(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_83c59660 *dt = mvsr_dt(pMobj);
    int c = pMobj->count;
    pMobj->count = c + 1;
    if (c < 6 && play_info.vsmenu_flow_id == 0) return;
    pMobj->stat = 3;
    dt->phase = 4;
    switch (dt->cursor) {
    case 0:
        if (play_info.vsmenu_flow_id == 0) {
            fslnk_InitLinkMode();
            mvsr_link_clear();
            play_info.vsmenu_flow_id = 3;
            dt->pChild = DF_MVSE_NewSystem(pMobj, 0);
        } else if (play_info.vsmenu_flow_id == 3) {
            dt->pChild = DF_MVSE_NewSystem(pMobj, 1); /* a1 = 1 left by the delay slot at 0x1ecacbc */
        }
        break;
    case 1: {
        int flow = play_info.vsmenu_flow_id;
        if (flow == 0) {
            fslnk_InitLinkMode();
            Bgm_Stop();
            DF_MBGUIDE_Hide();
            if (!(Iop_GetInitFlag() & 0x800)) dt->flagIOP = 0x800;
            play_info.vsmenu_flow_id = 2;
            dt->phase = 3;
            pMobj->count = 0;
            DF_M1HELP_FadeOut();
            mvsr_back_fade();
            mvsr_fade_out_all(dt);
        } else if (flow == 2) {
            play_info.vsmenu_flow_id = 5;
            dt->pChild = MenuEth_NewSystem(pMobj);
        } else if (flow == 6) {
            mvsr_link_clear();
            play_info.vsmenu_flow_id = 7;
            dt->pChild = DF_MVSE_NewSystem(pMobj, 0);
        } else if (flow == 7) {
            dt->pChild = DF_MVSE_NewSystem(pMobj, 1); /* a1 = 1 left by the delay slot at 0x1ecacbc */
        }
        break;
    }
    case 2:
        play_info.ether_id = 0;
        if (play_info.vsmenu_flow_id == 0) {
            fslnk_InitLinkMode();
            Bgm_Stop();
            DF_MBGUIDE_Hide();
            dt->flagIOP = 0x400;
            play_info.vsmenu_flow_id = 1;
            dt->phase = 3;
            pMobj->count = 0;
            DF_M1HELP_FadeOut();
            mvsr_back_fade();
            mvsr_fade_out_all(dt);
        } else if (play_info.vsmenu_flow_id == 1) {
            mvsr_link_clear();
            play_info.vsmenu_flow_id = 4;
            dt->pChild = DF_MVSE_NewSystem(pMobj, 0);
        } else if (play_info.vsmenu_flow_id == 4) {
            dt->pChild = DF_MVSE_NewSystem(pMobj, 1); /* a1 = 1 = the flow == 1 compare constant (0x1ecaff8) */
        }
        break;
    }
}

static void __MenuVsr_PlayMain(struct stMobj *pMobj)
{
    PS2_FRAME(0x240);
    struct struct_83c59660 *dt = mvsr_dt(pMobj);
    if (pMobj->stat == 1) {
        int c = pMobj->count;
        pMobj->count = c + 1;
        if (c < 6) return;
        pMobj->stat = 0;
    }
    if (MenuKeyGuide_IsShown()) return;
    if (MPad[MPadUser].start == 1) {
        MenuKeyGuide_Show(0x30);
        return;
    }
    if (MPad[MPadUser].dw == 1 && !WindowEx_IsMoving(dt->pCursor)) {
        Voice_Call(0x3e9, 0x7f);
        dt->cursor = (dt->cursor + 1) % 3;
        if (dt->phase == 1) {
            PS2_STACK(char[0x100], name, 0x140);
            ps2_sprintf_d(name, mvsr_fmt_parts10, dt->cursor);
            WindowEx_MoveToLayout(dt->pCursor, (char *)LAYOUT_GetLayoutData(0x406), name, 0);
            DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x18fa7 + dt->cursor));
        }
    } else if (MPad[MPadUser].up == 1 && !WindowEx_IsMoving(dt->pCursor)) {
        Voice_Call(0x3e9, 0x7f);
        dt->cursor = (dt->cursor + 2) % 3;
        if (dt->phase == 1) {
            PS2_STACK(char[0x100], name, 0x40);
            ps2_sprintf_d(name, mvsr_fmt_parts10, dt->cursor);
            WindowEx_MoveToLayout(dt->pCursor, (char *)LAYOUT_GetLayoutData(0x406), name, 0);
            DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x18fa7 + dt->cursor));
        }
    }
    if (MPad[MPadUser].Rrt == 1) { /* circle: back */
        Voice_Call(0x3eb, 0x7f);
        dt->phase = 3;
        pMobj->count = 0;
        DF_M1HELP_FadeOut();
        mvsr_back_fade();
        mvsr_fade_out_all(dt);
    } else if (MPad[MPadUser].Rdw == 1) { /* cross: proceed (no BG fade) */
        Voice_Call(0x3ea, 0x7f);
        dt->phase = 2;
        pMobj->count = 0;
        DF_M1HELP_FadeOut();
        mvsr_fade_out_all(dt);
    }
}

static void __MenuVsr_PlayDispatch(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_83c59660 *dt = mvsr_dt(pMobj);
    struct stMobj *child = dt->pChild;
    if (!child || child->stat != 5) return;
    if (play_info.f_ready) {
        mvsr_delete_child(dt);
        dt->phase = 3;
        pMobj->count = 0;
        DF_M1HELP_FadeOut();
        mvsr_back_fade(); /* dead: f_ready is non-zero here (re-read as in the original) */
        mvsr_fade_out_all(dt);
    } else if (play_info.vsmenu_flow_id == 6) {
        mvsr_delete_child(dt);
        dt->cursor = 1;
        __MenuVsr_PlayProceed(pMobj);
    } else if (DF_MVIEW_MotionSync(MViewp)) { /* child cancelled: back to the main screen */
        dt->idMViewMot = 0;
        DF_MBGUIDE_Show();
        play_info.vsmenu_flow_id = 0;
        mvsr_delete_child(dt);
        mvsr_main_enter(pMobj, dt);
    }
}

static void __MenuVsr_Pkt(void) {}

static void __MenuVsr_Draw(void) {}

static void __MenuVsr_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_83c59660 *dt = mvsr_dt(pMobj);
    switch (dt->phase) {
    case 1:
        __MenuVsr_PlayMain(pMobj);
        break;
    case 2:
        __MenuVsr_PlayProceed(pMobj);
        break;
    case 3: {
        int c = pMobj->count;
        pMobj->count = c + 1;
        if (c < 6) break;
        if (!DF_MBACK_Sync()) break;
        int flag = dt->flagIOP;
        if (flag) {
            Snd_Cleanup();
            Iop_Init(flag | 1);
            sceSifInitIopHeap();
            Snd_Init();
            Ac2_Mcf_Init();
            Pad_Init();
            sceGsSyncV(0);
            dt->flagIOP = 0;
        }
        __MVSR_ItemDelete(pMobj);
        pMobj->stat = 5;
        pMobj->play_func = 0;
        pMobj->draw_func = 0;
        pMobj->pkt_func = 0;
        break;
    }
    case 4:
        __MenuVsr_PlayDispatch(pMobj);
        break;
    case 5:
        if ((DF_MDIALOG_Sync() & 4) == 4) {
            mvsr_main_enter(pMobj, dt);
            play_info.vsmenu_flow_id = 0;
        }
        break;
    }
}

struct stMobj *MenuVsr_NewSystem(struct stMobj *pParentMobj, int mode)
{
    PS2_FRAME(0x40);
    struct stMobj *obj = DF_MOBJ_GetNewObject(pParentMobj);
    if (!obj) return 0;
    struct struct_83c59660 *dt = mvsr_dt(obj);
    memset(dt, 0, 0x3c);
    obj->type = 0;
    obj->stat = 1;
    __MVSR_ItemCreate(obj, mode);
    obj->count = 0;
    obj->play_func = (void (*)(void))GFN(__MenuVsr_Play);
    obj->draw_func = (void (*)(void))GFN(__MenuVsr_Draw);
    obj->pkt_func = (void (*)(void))GFN(__MenuVsr_Pkt);
    if (mode == 1) { /* return from a battle: resume the link flow */
        if (play_info.vsmenu_flow_id == 3) {
            dt->cursor = 0;
            __MenuVsr_PlayProceed(obj);
        } else if (play_info.vsmenu_flow_id == 4) {
            dt->cursor = 2;
            __MenuVsr_PlayProceed(obj);
        } else if (play_info.vsmenu_flow_id == 7) {
            dt->cursor = 1;
            __MenuVsr_PlayProceed(obj);
        }
    }
    /* every mode falls through here; flow is re-read (Proceed may have changed it) */
    if (play_info.vsmenu_flow_id == 0) {
        DF_MBGUIDE_Show();
        mvsr_main_enter(obj, dt);
    } else if (play_info.vsmenu_flow_id == 1) {
        DF_MBGUIDE_Show();
        dt->cursor = 2;
        __MenuVsr_PlayProceed(obj);
    } else if (play_info.vsmenu_flow_id == 2) {
        DF_MBGUIDE_Show();
        if (!Iop_IsLoadDev9()) {
            dt->phase = 5;
            DF_MDIALOG_Open(5, DF_MSTR_GetInfo(MenuMng.language, 1, 0x30e5e), 0, 0, 0, 0, 0, 0);
        } else {
            dt->cursor = 1;
            __MenuVsr_PlayProceed(obj);
        }
    }
    return obj;
}
