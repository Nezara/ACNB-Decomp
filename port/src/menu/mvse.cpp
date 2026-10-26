/* src/menu/mvse.cpp - MVSE, the Versus entry screen (OLMENU; scene label 0x53): pick "1PLAYER FULL SCREEN" /
 * "2PLAYERS SPLIT SCREEN" (local versus) or wait for the i.LINK / Ethernet peers, then hand over to the situation
 * setup screen (mvss) and finish with stat 5. One stMobj owns everything; its dt is a struct_e1650ca9.
 * Native:
 *   - __PktVsEnterSystem (phase 2 group 5a), the screen object's pkt_func: the AC preview models of the slots (five
 *     model pointers per slot object at its dt + 0x70, skipped while invisible) and of the cable object, between
 *     CLAMP_1 = 5 and CLAMP_1 = 0.
 *   - group 1: the item helpers (__MVSE_ItemFadeOut / FadeIn / Delete / Create, __MVSE_SetTagPos), the draw_func
 *     __DrawVsEnterSystem (despite its name the per-frame link poll), the child / dialog poll __PlayVsEnterDispatch,
 *     __SetVsEnterSystemFadeOut and the input state __PlayVsEnterSystem.
 *   - group 2: the fade-out state __PlayVsEnterSystemFadeOut (Ethernet address-list handshake, teardown, spawning
 *     the setup child mvss / mvsw), the fade-in state __PlayVsEnterSystemFadeIn (texture file loads, icon models)
 *     and the constructor DF_MVSE_NewSystem (called by the interpreted mvsr).
 *   - group 3 (i.LINK / Ethernet only, no replay reaches them): __MVSE_ShowErrorDlg, __PlayVsEnterSystemMove (seat
 *     count / own seat changed), __PlayVsEnterSystemWaitSync / __SetVsEnterSystemWaitSync (host waits for the peers).
 * The fslnk_* (liblnk) link layer and DF_MBGUIDE / M1HELP / MBACK stay interpreted. The sprintf calls are newlib's (ps2_sprintf_*: buffer and
 * format in PS2 memory, nodwarf strings); the tag strings are kept by DF_MOBJ_NewStringDirect (nodwarf too). */
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include <string.h>

#ifdef ACNB_SOURCEPORT
char mvse_fmt_parts10[11] = "parts10_%d";
char mvse_fmt_player[23] = "%1dPLAYER%s  %s SCREEN";
char mvse_fmt_parts20b[12] = "parts20b_%d";
char mvse_parts10_0[10] = "parts10_0";
char mvse_parts20a_0[11] = "parts20a_0";
char mvse_str_1p[3] = "1P";
char mvse_str_empty[1] = "";
char mvse_str_1p2p[6] = "1P&2P";
char mvse_str_2p[3] = "2P";
char mvse_str_3plive[8] = "3P&LIVE";
char mvse_str_3p4p[6] = "3P&4P";
char mvse_str_live[5] = "LIVE";
char mvse_str_3p[3] = "3P";
char mvse_str_4p[3] = "4P";
char mvse_str_full[5] = "FULL";
char mvse_str_split[6] = "SPLIT";
char mvse_str_s[2] = "S";
char mvse_fmt_ita[7] = "ita_%d";
char mvse_fmt_s[3] = "%s";
#endif

#define MVSE_FN(f) ((void (*)(void))GFN(f))

/* tag string positions {x, y, 0, 0} per player-count mode (1..5) and slot (OLMENU 0x1ee3ca0; ItemCreate's copy at
 * 0x1ee3f80 is byte-identical) */
static const int s_mvse_tag_pos[5][5][4] = {
    {{320, 48, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {{264, 50, 0, 0}, {376, 50, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {{190, 54, 0, 0}, {320, 54, 0, 0}, {430, 54, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {{164, 60, 0, 0}, {268, 60, 0, 0}, {372, 60, 0, 0}, {476, 60, 0, 0}, {0, 0, 0, 0}},
    {{120, 64, 0, 0}, {220, 64, 0, 0}, {320, 64, 0, 0}, {420, 64, 0, 0}, {520, 64, 0, 0}},
};

#define E mvse_str_empty
/* tag strings per mode (1..5), cursor row (idx 0..2) and slot (OLMENU 0x1ee3e30: main-ELF string addresses) */
static char *const s_mvse_tag_name[5][3][5] = {
    {{mvse_str_1p, E, E, E, E}, {mvse_str_1p2p, E, E, E, E}, {E, E, E, E, E}},
    {{mvse_str_1p, mvse_str_2p, E, E, E}, {mvse_str_1p2p, mvse_str_3plive, E, E, E},
     {mvse_str_1p2p, mvse_str_3p4p, E, E, E}},
    {{mvse_str_1p, mvse_str_2p, mvse_str_live, E, E}, {mvse_str_1p, mvse_str_2p, mvse_str_3p, E, E},
     {mvse_str_1p2p, mvse_str_3p4p, mvse_str_live, E, E}},
    {{mvse_str_1p, mvse_str_2p, mvse_str_3p, mvse_str_live, E}, {mvse_str_1p, mvse_str_2p, mvse_str_3p, mvse_str_4p, E},
     {E, E, E, E, E}},
    {{mvse_str_1p, mvse_str_2p, mvse_str_3p, mvse_str_4p, mvse_str_live}, {E, E, E, E, E}, {E, E, E, E, E}},
};
#undef E

/* DF_MBGUIDE_Change is int(void) in DWARF but reads a0..t0 */
static inline void mvse_guide(int a, int b, int c, int d, int e)
{
    gcall_as<int(int, int, int, int, int)>(GFN(DF_MBGUIDE_Change), a, b, c, d, e);
}

/* link shutdown: four fslnk calls, each followed by one vblank wait */
static inline void mvse_link_stop(void)
{
    fslnk_LinkSyncStop();
    sceGsSyncV(0);
    fslnk_LinkSyncTerm();
    sceGsSyncV(0);
    fslnk_LinkStop();
    sceGsSyncV(0);
    fslnk_LinkTerm();
    sceGsSyncV(0);
}

/* first player number of cursor row idx: idx + n - n / 3 (MWCC magic divide, not n % 3) */
static inline int mvse_pnum(int idx, int n) { return idx + (n - n / 3); }

static inline void mvse_erase(struct stMobj *o)
{
    o->f_draw = 0;
    o->f_erase = 1;
}

static inline struct struct_e1650ca9 *mvse_dt(struct stMobj *o) { return (struct struct_e1650ca9 *)&o->dt; }

/* model i of a slot object: an _stMModel *[5] at stMobj + 0x80 (dt + 0x70; no DWARF union member has it) */
static inline _stMModel *mvse_model(stMobj *o, int i)
{
    return *(_stMModel **)((char *)o + 0x80 + 4 * i);
}

static void __MVSE_ItemFadeOut(struct stMobj *pMobj, int idx, int control)
{
    PS2_FRAME(0x50);
    struct struct_e1650ca9 *dt = mvse_dt(pMobj);
    if (control) {
        for (int i = 0; i < idx; i++) {
            if (dt->apIta[i]) UnitSprite_SetFadeOut(dt->apIta[i]);
            if (dt->apParts20b[i]) DF_MOBJ_SetFadeOut(dt->apParts20b[i]);
        }
        if (dt->pCursor) {
            WindowEx_SetFadeOut(dt->pCursor);
            WindowEx_BlinkEnd(dt->pCursor);
        }
    } else {
        if (dt->pWaitString) DF_MOBJ_SetFadeOut(dt->pWaitString);
        if (dt->pWaitCircle && dt->f_fade_brink) {
            DF_MOBJ2_SetBlinkSpriteFadeOut(dt->pWaitCircle);
            dt->f_fade_brink = 0;
        }
    }
    for (int i = 0; i < 5; i++) {
        struct stMobj *o = dt->apTag[i];
        if (o && o->f_draw) DF_MOBJ_SetFadeOut(o);
    }
    DF_M1HELP_FadeOut();
}

static void __MVSE_ItemFadeIn(struct stMobj *pMobj, int idx, int control)
{
    PS2_FRAME(0x50);
    struct struct_e1650ca9 *dt = mvse_dt(pMobj);
    if (!dt->apIta[0]) return;
    if (control) {
        DF_MBGUIDE_Show();
        for (int i = 0; i < idx; i++) {
            DF_MOBJ_SetDrawEnable(dt->apIta[i], 1);
            UnitSprite_SetFadeIn(dt->apIta[i]);
            DF_MOBJ_SetDrawEnable(dt->apParts20b[i], 1);
            DF_MOBJ_SetFadeIn(dt->apParts20b[i]);
        }
        DF_MOBJ_SetDrawEnable(dt->pCursor, 1);
        WindowEx_SetFadeIn(dt->pCursor);
        DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x18fc5));
    } else {
        DF_MBGUIDE_Hide();
        DF_MOBJ_SetDrawEnable(dt->pWaitString, 1);
        DF_MOBJ_SetFadeIn(dt->pWaitString);
        DF_MOBJ_SetDrawEnable(dt->pWaitCircle, 1);
        DF_MOBJ2_SetBlinkSpriteFadeIn(dt->pWaitCircle);
        dt->f_fade_brink = 1;
        DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x18fc6));
    }
    WindowEx_BlinkBegin(dt->pCursor);
    DF_M1HELP_FadeIn();
}

static void __MVSE_ItemDelete(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_e1650ca9 *dt = mvse_dt(pMobj);
    if (dt->apIta[0]) DF_MBACK_PopSceneLabel();
    for (int i = 0; i < 4; i++) {
        UnitSprite_Delete(dt->apIta[i]); /* no NULL guard (quirk) */
        dt->apIta[i] = 0;
        if (dt->apParts20b[i]) {
            mvse_erase(dt->apParts20b[i]);
            dt->apParts20b[i] = 0;
        }
    }
    if (dt->pWaitString) {
        mvse_erase(dt->pWaitString);
        dt->pWaitString = 0;
    }
    if (dt->pWaitCircle) {
        mvse_erase(dt->pWaitCircle);
        dt->pWaitCircle = 0;
    }
    for (int i = 0; i < 5; i++) {
        if (dt->apTag[i]) {
            mvse_erase(dt->apTag[i]);
            dt->apTag[i] = 0;
        }
    }
    WindowEx_Delete(dt->pCursor);
    dt->pCursor = 0;
}

static void __MVSE_ItemCreate(struct stMobj *pMobj)
{
    PS2_FRAME(0x350);
    PS2_STACK(char[0x100], name, 0x240);
    struct struct_e1650ca9 *dt = mvse_dt(pMobj);
    if (dt->apIta[0]) return;
    DF_MBACK_PushSceneLabel((enum_5124d09c)0x53);
    for (int i = 0; i < 4; i++) {
        ps2_sprintf_d(name, mvse_fmt_ita, i);
        dt->apIta[i] = UnitSprite_New(pMobj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x412), name);
        DF_MOBJ_SetDrawEnable(dt->apIta[i], 0);
        ps2_sprintf_d(name, mvse_fmt_parts20b, i);
        dt->apParts20b[i] = DF_MOBJ_NewStringFromLayoutName(pMobj, 0, 0x18fc5, 0, (char *)LAYOUT_GetLayoutData(0x412),
                                                            name, 0, 0, 0);
        DF_MOBJ_SetDrawEnable(dt->apParts20b[i], 0);
    }
    ps2_sprintf_d(name, mvse_parts10_0, 0); /* format without '%': the original's a2 is stale and unread */
    dt->pCursor = WindowEx_NewFromLayout(pMobj, 0xb, (char *)LAYOUT_GetLayoutData(0x412), name);
    DF_MOBJ_SetDrawEnable(dt->pCursor, 0);
    ps2_sprintf_d(name, mvse_parts20a_0, 0);
    dt->pWaitString =
        DF_MOBJ_NewStringFromLayoutName(pMobj, 0, 0x18fc6, 0, (char *)LAYOUT_GetLayoutData(0x40b), name, 0, 0, 0);
    DF_MOBJ_SetDrawEnable(dt->pWaitString, 0);

    alignas(16) int uv[4] = {0xd0, 0x22, 0x100, 0x52};      /* sp+0x230, 0x1ee3f60 */
    alignas(16) struct stMenuRect rect = {0x6e, 0xf5, 0x1a8, 0x19}; /* sp+0x220, 0x1ee3f70 */
    struct struct_76b810ec circle_col = {0x80, 0x80, 0x80, 0x3c}; /* word 0x3c808080 (0x27c2f8) */
    dt->pWaitCircle = DF_MOBJ2_NewBlinkSpriteNX(pMobj, 0x3ec, &rect, uv, &circle_col, 0, 0);
    DF_MOBJ2_SetBlinkSpriteAlphaReg(dt->pWaitCircle, 0x48);
    DF_MOBJ_SetDrawEnable(dt->pWaitCircle, 0);

    struct struct_76b810ec tag_col = {0x6e, 0x82, 0x8c, 0x80}; /* word 0x808c826e (0x27c2fc) */
    /* 0x1ee4110: the slot names of the one-player row */
    char *const tag_name[5] = {mvse_str_1p, mvse_str_2p, mvse_str_3p, mvse_str_4p, mvse_str_live};
    for (int j = 0; j < 5; j++) {
        dt->apTag[j] = DF_MOBJ_NewStringDirect(pMobj, tag_name[j], 4, s_mvse_tag_pos[0][j][0], s_mvse_tag_pos[0][j][1],
                                               &tag_col, 1, 0x20000);
        DF_MOBJ_SetDrawEnable(dt->apTag[j], 0);
    }
    if (dt->apIta[0] && dt->max_idx > 0) {
        for (int i = 0; i < dt->max_idx; i++) {
            int n = dt->num_connect;
            int p = mvse_pnum(i, n);
            ps2_sprintf_dss(dt->strp[i], mvse_fmt_player, p, p == 1 ? mvse_str_empty : mvse_str_s,
                            n < p ? mvse_str_split : mvse_str_full);
            DF_MOBJ_SetStringDirect(dt->apParts20b[i], dt->strp[i]);
        }
    }
    DF_M1HELP_SetWindow(); /* empty stub; the original passes (0, 0x280) */
}

static void __MVSE_SetTagPos(struct stMobj *ioObjp, struct stMobj **unused, char mode, char idx)
{
    PS2_FRAME(0x310);
    (void)unused;
    struct struct_e1650ca9 *dt = mvse_dt(ioObjp);
    if (dt->f_moving == 1) return;
    for (int i = 0; i < 5; i++) {
        DF_MOBJ_SetDrawEnable(dt->apTag[i], 0);
        dt->apTag[i]->dt.string.x = (short)s_mvse_tag_pos[mode - 1][i][0];
        dt->apTag[i]->dt.string.y = (short)s_mvse_tag_pos[mode - 1][i][1];
        char *name = s_mvse_tag_name[mode - 1][(int)idx][i];
        if (*name) {
            DF_MOBJ_SetStringCopy(dt->apTag[i], name);
            DF_MOBJ_SetDrawEnable(dt->apTag[i], 1);
            DF_MOBJ_SetFadeIn(dt->apTag[i]);
        }
    }
}

static void __DrawVsEnterSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    struct struct_e1650ca9 *dt = mvse_dt(ioObjp);
    char old_idx = dt->idx;
    int r = fslnk_LinkCheckConnect(0, dt->link_state + 1);
    if (r < 0) {
        if (!dt->f_block) __MVSE_ShowErrorDlg(dt, ioObjp, r);
    } else if (r == 1 && dt->link_state == 1) {
        fslnk_SetCheckAlive(1);
        dt->f_fadeout = 1;
        return;
    }
    if (dt->f_block) return;
    if (dt->link_state == 0 && fslnk_GetForceConnect() == 2) {
        if (play_info.vsmenu_flow_id != 3 && dt->num_connect == 1) {
            __MVSE_ShowErrorDlg(dt, ioObjp, -1);
            return;
        }
        Voice_Call(0x3ea, 0x7f);
        mvse_link_stop();
        fslnk_SetCheckAlive(0);
        dt->idx = (char)fslnk_GetPlayerNum();
        dt->tpnum = (char)mvse_pnum(dt->idx, dt->num_connect);
        dt->link_state = 1;
        __SetVsEnterSystemWaitSync(ioObjp);
        return;
    }
    if (play_info.vsmenu_flow_id != 3) {
        dt->num_connect = (char)(fslnk_GetConnectedNum() + 1);
        dt->id_self = (char)fslnk_GetIndex();
        if (dt->id_self) dt->idx = (char)fslnk_GetPlayerNum();
    }
    if (old_idx != dt->idx && dt->link_state == 0)
        __MVSE_SetTagPos(ioObjp, &dt->tagimp[0], dt->num_connect, dt->idx);
    if (ioObjp->stat == 0 && (dt->prev_num_connect != dt->num_connect || dt->prev_id_self != dt->id_self)) {
        dt->prev_num_connect = dt->num_connect;
        dt->prev_id_self = dt->id_self;
        dt->step = 0;
        dt->f_moving = 1;
        ioObjp->play_func = MVSE_FN(__PlayVsEnterSystemMove);
        ioObjp->stat = 2;
    }
}

static void __PktVsEnterSystem(stMobj *inObjp)
{
    PS2_FRAME(0x60);
    struct_e1650ca9 *p_ssysp = (struct_e1650ca9 *)&inObjp->dt;
    unsigned int *dmap = DF_MPKT_MakeGSRegister(MPktCurrentp, 8 /* CLAMP_1 */, 5);
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            stMobj *o = p_ssysp->ps2imp[j];
            if (!o) continue;
            _stMModel *mp = mvse_model(o, i);
            if (!mp || mp->mulalpha == 0.0f) continue;
            dmap = DF_MMODEL_MakeVu1PacketAC(dmap, mp, nullptr, 4);
        }
        stMobj *o = p_ssysp->cblimp;
        if (o) {
            _stMModel *mp = mvse_model(o, i);
            if (mp) dmap = DF_MMODEL_MakeVu1PacketAC(dmap, mp, nullptr, 4);
        }
    }
    dmap = DF_MPKT_MakeGSRegister(dmap, 8, 0);
    MPktCurrentp = dmap;
}

static void __SetVsEnterSystemFadeOut(struct stMobj *ioObjp);

static void __PlayVsEnterDispatch(struct stMobj *ioObjp)
{
    PS2_FRAME(0xa0);
    struct struct_e1650ca9 *dt = mvse_dt(ioObjp);
    if (dt->func == 1) {
        int r = DF_MDIALOG_Sync();
        if (!(r & 4)) return;
        if (r != 4) return;
        mvse_guide(0, 0, 0, 0, 0x63);
        play_info.f_ready = -1;
        dt->f_error = 1;
        dt->func = 0;
        ioObjp->draw_func = 0;
        __SetVsEnterSystemFadeOut(ioObjp);
        return;
    }
    /* the original reads csysp->stat before its NULL test */
    struct stMobj *c = dt->csysp;
    unsigned char st = c ? c->stat : ((const unsigned char *)PS2_NULL_MEM())[2];
    if (st != 5) return;
    if (c) {
        mvse_erase(c);
        dt->csysp = 0;
    }
    if (play_info.f_ready) {
        dt->func = 0;
        dt->step = 0;
        ioObjp->play_func = MVSE_FN(__PlayVsEnterSystemFadeOut);
        ioObjp->stat = 2;
        return;
    }
    /* the child came back not ready: re-initialise the link and the versus data, back to FadeIn step 5 */
    mvse_link_stop();
    if (play_info.vsmenu_flow_id != 3) fslnk_LinkInit(play_info.ether_id > 0 ? 3 : 0);
    fslnk_LinkReset(0);
    fslnk_SetCheckAlive(1);
    fslnk_SetOnLook(0);
    fslnk_SetLiveMonitor(1);
    fslnk_LinkSyncInit(5, 8, 0x3c, 0, 2);
    fslnk_LinkStart();
    dt->mode = 0;
    dt->num_connect = 1;
    dt->prev_num_connect = 1;
    dt->id_self = 0;
    dt->prev_id_self = 0;
    dt->link_state = 0;
    dt->idx = 0;
    dt->f_fadeout = 0;
    dt->max_idx = 2;
    dt->f_control = dt->id_self == 0;
    dt->p_imposwk = 0;
    char *embp[4];
    for (int i = 0; i < 4; i++) embp[i] = vs_ac_info[i].embp;
    memset(vs_ac_info, 0, 0xd90);
    for (int i = 0; i < 4; i++) vs_ac_info[i].embp = embp[i];
    char ether_id = play_info.ether_id;
    char flow_id = play_info.vsmenu_flow_id;
    memset(&play_info, 0, 0x40);
    memset(play_info.com_id, 5, 4);
    play_info.ether_id = ether_id;
    play_info.time_limit = 5;
    play_info.vsmenu_flow_id = flow_id;
    play_info.pickup_weapon = 0;
    play_info.dest_weapon = 0;
    Game_InitAllData();
    DF_M_AC_Init((struct StAcData *)MenuMng.datAsm);
    DF_M_AC_SetAll();
    DF_MPART_ExtPartsDispose(); /* the original passes -1; the native ignores it (msys.cpp likewise) */
    DF_InitMemCard();
    dt->stepCheckEther = 0;
    dt->step = 5;
    ioObjp->stat = 1;
    ioObjp->count = 0xf;
    ioObjp->play_func = MVSE_FN(__PlayVsEnterSystemFadeIn);
    ioObjp->draw_func = MVSE_FN(__DrawVsEnterSystem);
}

static void __SetVsEnterSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct struct_e1650ca9 *dt = mvse_dt(ioObjp);
    if (dt->id_self == 0) DF_M1HELP_FadeOut();
    __MVSE_ItemFadeOut(ioObjp, 4, dt->f_control);
    for (int i = 0; i < 5; i++) {
        struct stMobj *o = dt->ps2imp[i];
        if (o && o->stat != 5 && o->stat != 2) DF_MOBJ2_SetIconModelFadeOut(o);
    }
    struct stMobj *o = dt->cblimp;
    if (o && o->stat != 5 && o->stat != 2) DF_MOBJ2_SetIconModelFadeOut(o);
    dt->count_fadeout = 0;
    dt->step = 0;
    ioObjp->play_func = MVSE_FN(__PlayVsEnterSystemFadeOut);
    ioObjp->stat = 2;
}

static void __PlayVsEnterSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x140);
    PS2_STACK(char[0x100], name, 0x40);
    struct struct_e1650ca9 *dt = mvse_dt(ioObjp);
    char old_idx = dt->idx;
    if (MenuMng.stat != 12) {
        if (dt->f_control) {
            if (MenuKeyGuide_IsShown()) return;
            if (MPad[MPadUser].start == 1) {
                MenuKeyGuide_Show(0x31);
                return;
            }
        }
        if (MPad[MPadUser].Rrt != 1) { /* not circle */
            if (MPad[MPadUser].Rdw == 1 && dt->f_control) { /* cross */
                Voice_Call(0x3ea, 0x7f);
                dt->p_num = (char)mvse_pnum(dt->idx, dt->num_connect);
                dt->tpnum = dt->p_num;
                if (dt->num_connect == 1) {
                    mvse_link_stop();
                    fslnk_SetCheckAlive(0);
                    dt->func = 1;
                    ioObjp->draw_func = 0;
                    __SetVsEnterSystemFadeOut(ioObjp);
                } else {
                    fslnk_SetPlayerNum(dt->idx);
                    fslnk_SetForceConnect();
                }
                return;
            }
            if (MPad[MPadUser].repeat.up && dt->f_control) {
                Voice_Call(0x3e9, 0x7f);
                dt->idx--;
                if (dt->idx < 0) dt->idx = dt->max_idx - 1;
            }
            if (MPad[MPadUser].repeat.dw && dt->f_control) {
                Voice_Call(0x3e9, 0x7f);
                dt->idx++;
                if (dt->max_idx - 1 < dt->idx) dt->idx = 0;
            }
            if (old_idx != dt->idx) {
                if (dt->apIta[0]) {
                    ps2_sprintf_d(name, mvse_fmt_parts10, dt->idx);
                    WindowEx_MoveToLayout(dt->pCursor, (char *)LAYOUT_GetLayoutData(0x412), name, 0);
                }
                __MVSE_SetTagPos(ioObjp, &dt->tagimp[0], dt->num_connect, dt->idx);
            }
            if (dt->f_control) fslnk_SetPlayerNum(dt->idx);
            return;
        }
        Voice_Call(0x3eb, 0x7f);
    }
    /* cancel (circle) or a menu-level back request (MenuMng.stat 12): full link teardown, leave */
    mvse_link_stop();
    fslnk_LinkDestroy();
    sceGsSyncV(0);
    ioObjp->draw_func = 0;
    dt->func = 0;
    dt->stepCheckEther = 6;
    __SetVsEnterSystemFadeOut(ioObjp);
}

/* ---- group 2 ---- */

/* Ethernet handshake strings (OLMENU 0x1ee3870 / 0x1ee3890, 32 bytes each incl. the zero padding) */
static const char s_mvse_addr_check[0x20] = "ADDRESS-LIST-CHECK";
static const char s_mvse_addr_fill[0x20] = "010101010101010101";

/* icon model locations {pos, ang} per player count (1..5) and slot (OLMENU 0x1ee34a0; Move's copy 0x1ee38d0):
 * x = -1.75 * (n - 1) + 3.5 * i, unused slots 0; ang.y = 0x40490fdb (pi) */
#define MVSE_LOC(x) {{x, 0.0f, 0.0f, 1.0f}, {0.0f, 0x1.921fb6p+1f, 0.0f, 1.0f}}
static const struct struct_9109f562 s_mvse_loc[5][5] = {
    {MVSE_LOC(0.0f), MVSE_LOC(0.0f), MVSE_LOC(0.0f), MVSE_LOC(0.0f), MVSE_LOC(0.0f)},
    {MVSE_LOC(-1.75f), MVSE_LOC(1.75f), MVSE_LOC(0.0f), MVSE_LOC(0.0f), MVSE_LOC(0.0f)},
    {MVSE_LOC(-3.5f), MVSE_LOC(0.0f), MVSE_LOC(3.5f), MVSE_LOC(0.0f), MVSE_LOC(0.0f)},
    {MVSE_LOC(-5.25f), MVSE_LOC(-1.75f), MVSE_LOC(1.75f), MVSE_LOC(5.25f), MVSE_LOC(0.0f)},
    {MVSE_LOC(-7.0f), MVSE_LOC(-3.5f), MVSE_LOC(0.0f), MVSE_LOC(3.5f), MVSE_LOC(7.0f)},
};
/* live-monitor (cable) model location (0x1ee3830) */
static const struct struct_9109f562 s_mvse_cbl_loc = MVSE_LOC(0.0f);
#undef MVSE_LOC
static const int s_mvse_ids[5] = {0x19a, 0x190, -1, -1, 0x19b}; /* 0x1ee37c0 */
static const float s_mvse_scale[5][4] = {                        /* 0x1ee37e0 */
    {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f},
    {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}};
static const int s_mvse_max_idx[5] = {2, 3, 3, 2, 1}; /* rows per player count (0x1ee3480) */

/* DF_MBACK_ChangeTag is void(void) in DWARF but reads a0 (an empty stub) */
static inline void mvse_change_tag(int tag)
{
    gcall_as<void(int)>(GFN(DF_MBACK_ChangeTag), tag);
}

/* spectator test: the last seat of a 3+ machine link with no player slot of its own */
static inline int mvse_onlook(void)
{
    int c = play_info.connected_num;
    int s = play_info.self_id;
    if (s != c - 1 || c < 3) return 0;
    int m = play_info.play_mode;
    return play_info.player_num < (s + 1) * (m + 1) - m;
}

/* free the work buffer and the two texture files (asm order) */
static void mvse_free_buffers(struct stMobj *o)
{
    struct struct_e1650ca9 *dt = mvse_dt(o);
    if (dt->pWorkBuff) {
        DF_MMM_Free(0, dt->pWorkBuff);
        dt->pWorkBuff = 0;
    }
    if (dt->pMapTexp) MenuTex_UnRegisterBndTexture(dt->pMapTexp);
    if (dt->pVSTexp) MenuTex_UnRegisterBndTexture(dt->pVSTexp);
    if (dt->pVSTexpOrg) {
        DF_MMM_Free(0, dt->pVSTexpOrg);
        dt->pVSTexpOrg = 0;
    }
    if (dt->pMapTexpOrg) {
        DF_MMM_Free(0, dt->pMapTexpOrg);
        dt->pMapTexpOrg = 0;
    }
}

static void mvse_finish(struct stMobj *o)
{
    MenuWorkp = mvse_dt(o)->defTBP;
    mvse_free_buffers(o);
    o->play_func = 0;
    o->stat = 5;
}

static void __PlayVsEnterSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x1b0);
    struct struct_e1650ca9 *dt = mvse_dt(ioObjp);
    if (!DF_MVIEW_MotionSync(MViewp)) return;
    if (dt->count_fadeout++ < 6) return;

    if (fslnk_GetLinkMode() == 3 && dt->mode == 0 && dt->stepCheckEther != 5) {
        /* Ethernet: exchange and check the address lists, then a fixed string (buffers handed to interpreted
         * fslnk_LinkSendData / RecvData: original stack slots) */
        PS2_STACK(char[0x20], chk, 0x190);
        PS2_STACK(char[0x20], rcv, 0x170);
        PS2_STACK(char[0x80], list, 0xf0);
        PS2_STACK(char[0x80], list2, 0x70);
        int n = Ether_GetConnectedNum();
        int have = 0, ok = 0;
        memcpy(chk, s_mvse_addr_check, 0x20);
        memcpy(rcv, s_mvse_addr_fill, 0x20);
        int r = Ether_Proc();
        if (dt->count_timeout++ >= 0x259) r = -1;
        if (r < 0) {
            if (dt->stepCheckEther != 5) Ether_SendErrorToAll(-0x11);
            __MVSE_ShowErrorDlg(dt, ioObjp, r);
            return;
        }
        switch (dt->stepCheckEther) {
        case 0:
            fslnk_LinkSendData(Ether_GetAddressList(), 0x80);
            dt->count_timeout = 0;
            dt->stepCheckEther = 1;
            return;
        case 1:
            for (int i = 0; i < n; i++)
                if ((unsigned int)fslnk_LinkRecvDataSize(i) >= 0x80) have++;
            if (have != n) return;
            ZeroMemory(list, 0x80);
            ZeroMemory(list2, 0x80); /* never used again */
            for (int i = 0; i < n; i++) {
                fslnk_LinkRecvData(list, i, 0x80);
                if (Ether_CheckAddressList((struct struct_5f3c7168 *)list)) ok++;
            }
            if (ok != n) {
                Ether_SendErrorToAll(-0x11);
                __MVSE_ShowErrorDlg(dt, ioObjp, -0x11);
            } else {
                dt->stepCheckEther = 2;
            }
            return;
        case 2:
            fslnk_LinkSendData(chk, 0x20);
            dt->stepCheckEther = 3;
            return;
        case 3:
            for (int i = 0; i < n; i++)
                if ((unsigned int)fslnk_LinkRecvDataSize(i) >= 0x20) have++;
            if (have != n) return;
            for (int i = 0; i < n; i++) {
                fslnk_LinkRecvData(rcv, i, 0x20);
                if (strcmp(chk, rcv) == 0) ok++; /* newlib strcmp 0x119198; only == 0 is used */
            }
            if (ok != n) {
                Ether_SendErrorToAll(-0x11);
                __MVSE_ShowErrorDlg(dt, ioObjp, -0x11);
            } else {
                dt->stepCheckEther = 4;
            }
            return;
        default: /* 4 and others: handshake done */
            break;
        }
    }

    if (dt->step == 1) {
        if (!DF_MBACK_Sync()) return;
        mvse_finish(ioObjp);
        return;
    }
    if (dt->ps2imp[0] && dt->ps2imp[0]->stat != 5) return;
    __MVSE_ItemDelete(ioObjp);
    for (int i = 0; i < 5; i++) {
        if (dt->ps2imp[i]) {
            DF_MOBJ2_DeleteIconModel(dt->ps2imp[i]);
            dt->ps2imp[i] = 0;
        }
    }
    if (dt->cblimp) {
        DF_MOBJ2_DeleteIconModel(dt->cblimp);
        dt->cblimp = 0;
    }
    if (dt->func == 0) {
        if (play_info.f_ready && !dt->f_error) {
            mvse_finish(ioObjp);
            return;
        }
        mvse_guide(0, 0, 0, 0, 0x63);
        if (dt->stepCheckEther == 5) {
            DF_MBACK_SetFadeOutNX(); /* the original passes a0 = 0, unread */
            Bgm_FadeOutStart();
        }
        dt->step++;
        return;
    }
    if (dt->func != 1) return;
    if (dt->mode == 0) {
        play_info.player_num = dt->tpnum;
        play_info.play_mode = dt->num_connect < dt->tpnum;
        play_info.connected_num = dt->num_connect;
        play_info.self_id = dt->id_self;
        fslnk_SetOnLook(mvse_onlook());
    }
    if (dt->mode == 0)
        for (int p = 0; p < 4; p++) Game_SetKeyDefaultAnalog(p);
    if (dt->id_self == 0)
        dt->csysp = DF_MVSS_NewSystem(ioObjp, &play_info, dt->mode);
    else
        dt->csysp = DF_MVSW_NewSystem(ioObjp, &play_info);
    dt->func = 0;
    ioObjp->play_func = MVSE_FN(__PlayVsEnterDispatch);
    ioObjp->stat = 3;
}

/* waits of the file-load steps: CD busy or an async read pending */
static inline int mvse_io_busy(void)
{
    return Cdvd_CdSync() == 1 || File_IsAsyncBusy();
}

static void __PlayVsEnterSystemFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x430);
    struct struct_e1650ca9 *dt = mvse_dt(ioObjp);
    if (dt->step < 6) ioObjp->count++;
    switch (dt->step) {
    case 0: /* load the map texture file */
        if (mvse_io_busy()) return;
        if (!dt->pMapTexpOrg) {
            int sz = File_GetSize(0x1013);
            dt->pMapTexpOrg = (char *)DF_MMM_Alloc(0, sz + 0x3f);
            dt->pMapTexp = (char *)DF_MFUNC_Aligned(dt->pMapTexpOrg, 0x40);
            File_Read(0x1013, -1, dt->pMapTexp, 0);
            dt->step++;
        } else {
            dt->step += 2; /* already loaded (re-entry) */
        }
        return;
    case 1:
        if (mvse_io_busy()) return;
        MenuTex_RegisterBndTexture(dt->pMapTexp);
        sceGsSyncPath(0, 0);
        dt->step++;
        return;
    case 2: /* load the VS texture file */
        if (mvse_io_busy()) return;
        if (!dt->pVSTexpOrg) {
            int sz = File_GetSize(0x1012);
            dt->pVSTexpOrg = (char *)DF_MMM_Alloc(0, sz + 0x3f);
            dt->pVSTexp = (char *)DF_MFUNC_Aligned(dt->pVSTexpOrg, 0x40);
            File_Read(0x1012, -1, dt->pVSTexp, 0);
            dt->step++;
        } else {
            dt->step += 2;
        }
        return;
    case 3:
        if (mvse_io_busy()) return;
        MenuTex_RegisterBndTexture(dt->pVSTexp);
        sceGsSyncPath(0, 0);
        dt->step++;
        return;
    case 4: { /* re-created after a link session (mode 1): wait for the peers */
        if (dt->mode != 1) {
            dt->step++;
            return;
        }
        int r = fslnk_LinkCheckConnect(0, 3);
        if ((r < 0 && r != -0x10) || dt->count >= 0x259) {
            mvse_guide(0, 0xe, 0, 0, 0x63);
            DF_MDIALOG_Open(5, DF_MSTR_GetInfo(MenuMng.language, 1, 0x30ed0), 0, 0, 0, 0, 1, 0);
            mvse_link_stop();
            fslnk_LinkDestroy();
            sceGsSyncV(0);
            dt->func = 1;
            ioObjp->stat = 3;
            ioObjp->play_func = MVSE_FN(__PlayVsEnterDispatch);
            return;
        }
        if (play_info.connected_num == 1 || r == 1) {
            if (play_info.connected_num != 1) fslnk_SetOnLook(mvse_onlook());
            if (!dt->f_seq) {
                Bgm_Play();
                dt->f_seq = 1;
            }
            dt->func = 1;
            __SetVsEnterSystemFadeOut(ioObjp);
        }
        dt->count++;
        return;
    }
    case 5: { /* models and items */
        if (!DF_MVIEW_MotionSync(MViewp)) return;
        if (ioObjp->count < 0xf) return;
        if (!dt->f_seq) {
            Bgm_Play();
            dt->f_seq = 1;
        }
        DF_MVIEW_MotionStart(MViewp, dt->num_connect + 0x3b);
        __MVSE_ItemCreate(ioObjp);
        __MVSE_ItemFadeIn(ioObjp, dt->max_idx, dt->f_control);
        struct struct_76b810ec col = {0x80, 0x80, 0x80, 0x80}; /* word 0x80808080 (0x27c2ac / 0x27c2b0) */
        /* quirk: the original sets a0 = obj only before the loop; from the second model on the parent argument is
         * the previous model (a0 reloaded with ps2imp[i] after SetIconModelFadeIn) */
        struct stMobj *parent = ioObjp;
        for (int i = 0; i < 5 && i < dt->num_connect; i++) {
            /* the original copies the three tables to its stack (sp+0x110 / 0xa0 / 0xf0) every iteration */
            alignas(16) struct struct_9109f562 loc[5][5];
            alignas(16) float scale[5][4];
            alignas(16) int ids[5];
            memcpy(loc, s_mvse_loc, sizeof(loc));
            memcpy(scale, s_mvse_scale, sizeof(scale));
            memcpy(ids, s_mvse_ids, sizeof(ids));
            dt->ps2imp[i] = DF_MOBJ2_NewIconModel(parent, 3, &loc[dt->num_connect - 1][i], scale, col, ids,
                                                  i == dt->id_self);
            DF_MOBJ2_SetIconModelFadeIn(dt->ps2imp[i]);
            parent = dt->ps2imp[i];
        }
        if (dt->num_connect - 1 > 0) {
            if (dt->cblimp) {
                DF_MOBJ2_DeleteIconModel(dt->cblimp);
                dt->cblimp = 0;
            }
            alignas(16) struct struct_9109f562 cbl_loc = s_mvse_cbl_loc;     /* sp+0x80 */
            alignas(16) int cbl_ids[5] = {0x1ac + dt->num_connect, -1, -1, -1, -1}; /* sp+0x60, 0x1ee3850 */
            dt->cblimp = DF_MOBJ2_NewIconModel(ioObjp, 4, &cbl_loc, 0, col, cbl_ids, 0);
            DF_MOBJ2_SetIconModelFadeIn(dt->cblimp);
        }
        __MVSE_SetTagPos(ioObjp, &dt->tagimp[0], dt->num_connect, dt->idx);
        dt->count = 0;
        dt->step++;
        return;
    }
    case 6:
        if (dt->count++ < 0x1e) return;
        dt->count = 0;
        dt->step++;
        return;
    case 7:
        if (dt->ps2imp[0]->stat != 0) return; /* no NULL test (quirk) */
        dt->f_block = 0;
        ioObjp->stat = 0;
        ioObjp->play_func = MVSE_FN(__PlayVsEnterSystem);
        return;
    default:
        return;
    }
}

struct stMobj *DF_MVSE_NewSystem(struct stMobj *inObjp, int inMode)
{
    PS2_FRAME(0x70);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    struct struct_e1650ca9 *dt = mvse_dt(obj);
    obj->type = 0;
    obj->stat = 1;
    ZeroMemory(dt, 0xc4);
    dt->mode = (char)inMode;
    if (dt->mode == 0) {
        dt->num_connect = 1;
        dt->prev_num_connect = 1;
        dt->id_self = 0;
        dt->prev_id_self = 0;
        memset(vs_ac_info, 0, 0xd90);
        for (int i = 0; i < 4; i++)
            ps2_sprintf_s(vs_ac_info[i].name_com, mvse_fmt_s,
                          (const char *)DF_MSTR_GetName(MenuMng.language, 0, 0x7a188));
        char ether_id = play_info.ether_id;
        char flow_id = play_info.vsmenu_flow_id;
        memset(&play_info, 0, 0x40);
        memset(play_info.com_id, 5, 4);
        play_info.ether_id = ether_id;
        play_info.time_limit = 5;
        play_info.vsmenu_flow_id = flow_id;
        play_info.pickup_weapon = 0;
        play_info.dest_weapon = 0;
        Game_InitAllData();
        DF_M_AC_Init((struct StAcData *)MenuMng.datAsm);
        DF_M_AC_SetAll();
        DF_MPART_ExtPartsDispose(); /* the original passes -1; the native ignores it */
        DF_InitMemCard();
        mvse_guide(0, 1, 2, 0, 0x63);
        mvse_change_tag(0x21);
    } else {
        dt->num_connect = play_info.connected_num;
        dt->prev_num_connect = play_info.connected_num;
        dt->id_self = play_info.self_id;
        dt->prev_id_self = play_info.self_id;
        mvse_change_tag(0x22);
    }
    dt->mstr_top_idx = 0x88e;
    dt->idx = 0;
    dt->link_state = 0;
    dt->max_idx = (char)s_mvse_max_idx[dt->num_connect - 1]; /* num_connect 1..5 */
    dt->f_control = (dt->id_self == 0 && dt->num_connect != 1) || play_info.vsmenu_flow_id == 3;
    dt->f_error = 0;
    dt->f_fadeout = 0;
    dt->f_block = 1;
    dt->p_imposwk = 0;
    dt->defTBP = MenuWorkp;
    play_info.f_ready = 0;
    play_info.f_error = 0;
    play_info.f_mmdlinkerror = 0;
    char *p = (char *)DF_MMM_Alloc(0, 0xa205);
    dt->pWorkBuff = p;
    /* four 16-aligned 0x1420-byte emblem slices, then three 0x20-byte label buffers */
    for (int i = 0; i < 4; i++) {
        vs_ac_info[i].embp = (char *)(((uintptr_t)p + 0xf) & ~(uintptr_t)0xf);
        memset(vs_ac_info[i].embp, 0, 0x1420);
        p = vs_ac_info[i].embp + 0x1420;
    }
    dt->strp[0] = (char *)(((uintptr_t)p + 0xf) & ~(uintptr_t)0xf);
    dt->strp[1] = dt->strp[0] + 0x20;
    dt->strp[2] = dt->strp[1] + 0x20;
    memset(dt->strp[0], 0, 0x20);
    memset(dt->strp[1], 0, 0x20);
    memset(dt->strp[2], 0, 0x20);
    DF_MVS_InitTransVSAC();
    if (!(dt->mode == 1 && play_info.connected_num == 1)) {
        char players = 5;
        char size = 8;
        if (dt->mode != 0) {
            mvse_link_stop();
            players = (char)(play_info.connected_num - play_info.live_monitor);
            size = (char)(8u / (play_info.play_mode == 0 ? 2u : 1u));
        }
        if (play_info.vsmenu_flow_id != 3) fslnk_LinkInit(play_info.ether_id > 0 ? 3 : 0);
        fslnk_LinkReset(0);
        fslnk_SetCheckAlive(1);
        fslnk_SetOnLook(0);
        fslnk_SetLiveMonitor(dt->mode == 0 ? 1 : play_info.live_monitor);
        fslnk_LinkSyncInit(players, size, 0x3c, 0, 2);
        fslnk_LinkStart();
    }
    obj->count = 0;
    obj->play_func = MVSE_FN(__PlayVsEnterSystemFadeIn);
    obj->draw_func = dt->mode == 0 ? MVSE_FN(__DrawVsEnterSystem) : 0;
    obj->pkt_func = MVSE_FN(__PktVsEnterSystem);
    return obj;
}

/* ---- group 3: the i.LINK / Ethernet-only statics (no replay reaches them; asm-verified) ---- */

/* link teardown after an error: the dialog is already open, Dispatch polls it (func 1) */
static void mvse_error_exit(struct struct_e1650ca9 *dt, struct stMobj *o)
{
    mvse_link_stop();
    fslnk_LinkDestroy();
    sceGsSyncV(0);
    dt->func = 1;
    o->stat = 3;
    o->play_func = MVSE_FN(__PlayVsEnterDispatch);
    dt->stepCheckEther = 5;
}

/* fade out every per-seat icon model and the live-monitor model that is not already fading / gone */
static void mvse_models_fade_out(struct struct_e1650ca9 *dt)
{
    for (int i = 0; i < 5; i++) {
        struct stMobj *o = dt->ps2imp[i];
        if (o && o->stat != 5 && o->stat != 2) DF_MOBJ2_SetIconModelFadeOut(o);
    }
    struct stMobj *o = dt->cblimp;
    if (o && o->stat != 5 && o->stat != 2) DF_MOBJ2_SetIconModelFadeOut(o);
}

/* link error dialog (once: stepCheckEther 5 = already shown); ret -0x11 picks the second message */
static void __MVSE_ShowErrorDlg(struct struct_e1650ca9 *p_csysp, struct stMobj *ioObjp, int ret)
{
    PS2_FRAME(0x30);
    if (p_csysp->stepCheckEther == 5) return;
    unsigned int id = ret == -0x11 ? 0x30ed7 : 0x30ed0;
    DF_MDIALOG_Open(5, DF_MSTR_GetInfo(MenuMng.language, 1, id), 0, 0, 0, 0, 1, 0);
    mvse_error_exit(p_csysp, ioObjp);
}

/* play_func while the number of connected machines / the own seat changes (started by __DrawVsEnterSystem):
 * fade everything out, rebuild the models and labels for the new count, back to __PlayVsEnterSystem */
static void __PlayVsEnterSystemMove(struct stMobj *ioObjp)
{
    PS2_FRAME(0x550);
    struct struct_e1650ca9 *dt = mvse_dt(ioObjp);
    switch (dt->step) {
    case 0:
        if (MenuKeyGuide_IsShown()) MenuKeyGuide_Hide();
        DF_MVIEW_MotionStart(MViewp, dt->num_connect + 0x3b);
        __MVSE_ItemFadeOut(ioObjp, 4, dt->f_control);
        mvse_models_fade_out(dt);
        if (dt->id_self == 0) {
            mvse_guide(0, 1, 2, 0, 0x63);
        } else {
            mvse_guide(0, 0, 2, 0, 0x63);
            DF_M1HELP_FadeOut();
        }
        dt->step++;
        return;
    case 1: {
        PS2_STACK(char[0x100], name, 0x60);
        dt->f_control = (dt->id_self == 0 && dt->num_connect != 1) || play_info.vsmenu_flow_id == 3;
        dt->max_idx = (char)s_mvse_max_idx[dt->num_connect - 1]; /* Move's copy 0x1ee38b0, at sp+0x530 */
        dt->idx = 0;
        if (dt->apIta[0]) {
            ps2_sprintf_d(name, mvse_fmt_parts10, 0);
            WindowEx_MoveToLayout(dt->pCursor, (char *)LAYOUT_GetLayoutData(0x412), name, 0);
        }
        dt->step++;
        return;
    }
    case 2: {
        struct struct_76b810ec col = {0x80, 0x80, 0x80, 0x80}; /* word 0x80808080 (0x27c2b4 / 0x27c2b8) */
        for (int i = 0; i < dt->num_connect; i++) {
            /* the original copies the tables to its stack (sp+0x210 / 0x1a0 / 0x1f0) every iteration */
            alignas(16) struct struct_9109f562 loc[5][5];
            memcpy(loc, s_mvse_loc, sizeof(loc));
            struct stMobj *o = dt->ps2imp[i];
            if (o) {
                if (o->stat == 0 || o->stat == 1) continue;
                DF_MOBJ2_SetIconModelLocate(o, &loc[dt->num_connect - 1][i]);
                DF_MOBJ2_SelectIconModel(dt->ps2imp[i], i == dt->id_self);
                DF_MOBJ2_SetIconModelFadeIn(dt->ps2imp[i]);
            } else {
                alignas(16) int ids[5];
                alignas(16) float scale[5][4];
                memcpy(ids, s_mvse_ids, sizeof(ids));
                memcpy(scale, s_mvse_scale, sizeof(scale));
                dt->ps2imp[i] = DF_MOBJ2_NewIconModel(ioObjp, 3, &loc[dt->num_connect - 1][i], scale, col, ids,
                                                      i == dt->id_self);
                DF_MOBJ2_SetIconModelFadeIn(dt->ps2imp[i]);
            }
        }
        if (dt->num_connect - 1 > 0) {
            if (dt->cblimp) {
                DF_MOBJ2_DeleteIconModel(dt->cblimp);
                dt->cblimp = 0;
            }
            alignas(16) struct struct_9109f562 cbl_loc = s_mvse_cbl_loc;     /* sp+0x180 */
            alignas(16) int cbl_ids[5] = {0x1ac + dt->num_connect, -1, -1, -1, -1}; /* sp+0x160 */
            dt->cblimp = DF_MOBJ2_NewIconModel(ioObjp, 4, &cbl_loc, 0, col, cbl_ids, 0);
            DF_MOBJ2_SetIconModelFadeIn(dt->cblimp);
        }
        dt->f_moving = 0;
        __MVSE_SetTagPos(ioObjp, &dt->tagimp[0], dt->num_connect, dt->idx);
        dt->count = 0;
        dt->step++;
        ioObjp->stat = 0;
        return;
    }
    case 3:
        if (dt->count++ < 0x3c) return;
        __MVSE_ItemCreate(ioObjp);
        /* quirk: the same label loop ItemCreate just ran */
        if (dt->apIta[0] && dt->max_idx > 0) {
            for (int i = 0; i < dt->max_idx; i++) {
                int n = dt->num_connect;
                int p = mvse_pnum(i, n);
                ps2_sprintf_dss(dt->strp[i], mvse_fmt_player, p, p == 1 ? mvse_str_empty : mvse_str_s,
                                n < p ? mvse_str_split : mvse_str_full);
                DF_MOBJ_SetStringDirect(dt->apParts20b[i], dt->strp[i]);
            }
        }
        __MVSE_ItemFadeIn(ioObjp, dt->max_idx, dt->f_control);
        dt->step++;
        /* fall through */
    case 4:
        if (dt->f_control) {
            ioObjp->play_func = MVSE_FN(__PlayVsEnterSystem);
        } else if (dt->ps2imp[0] && dt->ps2imp[0]->stat == 0) {
            ioObjp->play_func = MVSE_FN(__PlayVsEnterSystem);
        }
        return;
    default:
        return;
    }
}

/* host after cross with peers connected: wait for the models to fade, restart the link sync with the chosen seat
 * count, then wait for __DrawVsEnterSystem's f_fadeout (0x12d frames, then the error dialog) */
static void __PlayVsEnterSystemWaitSync(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct struct_e1650ca9 *dt = mvse_dt(ioObjp);
    if (dt->count++ >= 0x12d) {
        mvse_guide(0, 0xe, 0, 0, 0x63);
        DF_MDIALOG_Open(5, DF_MSTR_GetInfo(MenuMng.language, 1, 0x30ed0), 0, 0, 0, 0, 1, 0);
        mvse_error_exit(dt, ioObjp);
        return;
    }
    if (dt->step == 0) {
        struct stMobj *o = dt->ps2imp[0];
        if (o && o->stat != 5) return;
        int n = dt->num_connect;
        char size = 8;
        play_info.live_monitor = 0;
        int pnum = mvse_pnum(dt->idx, n);
        if (dt->num_connect >= 3 && (dt->idx + (dt->num_connect * 2 - 1)) % 2 == 1) play_info.live_monitor = 1;
        if (!(dt->num_connect < pnum)) size = (char)(size / 2);
        fslnk_LinkInit(play_info.ether_id > 0 ? 3 : 0);
        fslnk_SetLiveMonitor(play_info.live_monitor);
        fslnk_LinkSyncInit((char)(n - play_info.live_monitor), size, 0x3c, 0, 2);
        fslnk_LinkStart();
        ioObjp->draw_func = MVSE_FN(__DrawVsEnterSystem);
        dt->step++;
    } else if (dt->step == 1 && dt->f_fadeout) {
        dt->func = 1;
        ioObjp->draw_func = 0;
        __SetVsEnterSystemFadeOut(ioObjp);
    }
}

/* enter the host's sync wait: fade the items and models out, play_func = WaitSync, no draw_func */
static void __SetVsEnterSystemWaitSync(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct struct_e1650ca9 *dt = mvse_dt(ioObjp);
    DF_M1HELP_FadeOut();
    __MVSE_ItemFadeOut(ioObjp, 4, dt->f_control);
    mvse_models_fade_out(dt);
    dt->step = 0;
    dt->count = 0;
    ioObjp->play_func = MVSE_FN(__PlayVsEnterSystemWaitSync);
    ioObjp->draw_func = 0;
    ioObjp->stat = 2;
}
