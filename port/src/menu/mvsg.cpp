/* src/menu/mvsg.cpp - MVSG, the Versus "AC setup" screen (OLMENU overlay, scene label MBACK_LABEL_AC_SETUP): one cell
 * per participant (players first, then COMs: unit sprite "name_N", "NP/RED LEADER" / "COMn" label, win / lose
 * counters, name) and a cursor over the cells the local player may act on; from here the player goes to the garage,
 * the situation window, the arena ranking, or starts the match (DF_MVS_CheckConpose checks the AC). One stMobj owns
 * everything; its dt is a struct_68a6f471 (p_csysp), the cell records are struct_85b3ad9a (pinfo_wp[]).
 * Group 1 native: the per-cell item functions (__MVSG_ItemSetCursor / FadeOut / FadeIn / Delete / Create),
 * DF_MVS_CheckConpose and the empty __MVSG_SetWindowStat. The sprintf calls are newlib's (ps2_sprintf_*: buffer and
 * format in PS2 memory, nodwarf strings).
 * Group 2 native: __DrawAcSetupSystem (despite its name the per-frame network-link poll, kept in the draw_func slot;
 * returns at once offline, connected_num == 1), __PlayACSetupDispatch (link-error / link-sync dialog state machine),
 * and the leaves __MVS_GetControlEnable / __MVS_GetDirection. Buffers handed to the interpreted fslnk_* (liblnk) code
 * sit at their original stack slots (PS2_STACK).
 * Group 3a native: __PlayACSetupSystemStay (link wait), __SetACSetupSystemFadeOut / __PlayACSetupSystemFadeOut
 * (fade-out and hand-off to the garage, arena ranking, situation window or the match).
 * Group 3b native: __PlayACSetupSystem (idle input: cancel, cell confirm, Start, ready toggle, cursor),
 * __PlayACSetupSystemFadeIn and the constructor DF_MVSG_NewSystem (random map / seed via newlib rand, cell records
 * carved out of MenuWorkp). */
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include <string.h>

#ifdef ACNB_SOURCEPORT
char mvsg_parts10a_0[11] = "parts10a_0";
char mvsg_fmt_parts10[11] = "parts10_%d";
char mvsg_fmt_parts20t[12] = "parts20t_%d";
char mvsg_fmt_com[10] = "COM%d%s%s";
char mvsg_fmt_parts20f[12] = "parts20f_%d";
char mvsg_fmt_parts20w[12] = "parts20w_%d";
char mvsg_fmt_parts20l[12] = "parts20l_%d";
char mvsg_fmt_parts20c[12] = "parts20c_%d";
char mvsg_fmt_parts20wc[13] = "parts20wc_%d";
char mvsg_fmt_parts20lc[13] = "parts20lc_%d";
char mvsg_fmt_parts20b[12] = "parts20b_%d";
char mvsg_banner_0[9] = "banner_0";
char mvsg_parts20d_22[12] = "parts20d_22";
char mvsg_parts10_0[10] = "parts10_0";
char mvsg_fmt_name[8] = "name_%d";
char mvsg_leader[8] = " LEADER";
char mvsg_empty[1] = "";
char mvsg_fmt_player[8] = "%dP%s%s";
char mvsg_red[5] = "/RED";
char mvsg_blue[6] = "/BLUE";
char mvsg_space[2] = " ";
char mvsg_fmt_s[3] = "%s";
char mvsg_fmt_d[3] = "%d";
unsigned char mvs_noc_id[7] = {1, 2, 4, 6, 0x48, 0x4f, 0};
/* mvs_map_id is a size-0 DWARF array; the original indexes it with map_id 0x17 (random map) too and reads the byte
 * after the 23 ids (0): the copy keeps that byte */
static const unsigned char mvsg_map_id[24] = {1,  2,  4,  5,  6,  12, 13, 14, 15, 16, 63, 64,
                                              66, 68, 69, 72, 73, 74, 75, 76, 77, 78, 79, 0};
#define MVSG_MAP_ID mvsg_map_id
#else
#define MVSG_MAP_ID mvs_map_id
#endif

typedef struct struct_68a6f471 VsgSys;  /* p_csysp: dt of the screen object */
typedef struct struct_85b3ad9a VsgCell; /* one participant cell */

static inline VsgSys *mvsg_sys(struct stMobj *pMobj) { return (VsgSys *)&pMobj->dt; }

/* the inline "delete" of a string object (f_draw = 0, then f_erase = 1: two separate flag-byte writes) */
static inline void mvsg_erase(struct stMobj *p)
{
    p->f_draw = 0;
    p->f_erase = 1;
}

/* help line + cursor window for cell cs->idx (the "Start" entry when idx == max_idx) */
static void __MVSG_ItemSetCursor(struct stMobj *pMobj)
{
    PS2_FRAME(0x130);
    PS2_STACK(char[256], name, 0x30);
    VsgSys *cs = mvsg_sys(pMobj);

    if (cs->idx == cs->max_idx) {
        ps2_sprintf_d(name, mvsg_parts10a_0, 0);
        DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x18f8a));
    } else {
        ps2_sprintf_d(name, mvsg_fmt_parts10, cs->dirTable[(int)cs->idx]);
        char flag = 0;
        char d = cs->dirTable[(int)cs->idx];
        for (char i = 0; i < 4; i++) {
            if (cs->is_com[(int)i] == d) {
                DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x18f89));
                flag = 1;
                break;
            }
        }
        if (!flag) DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x18f88));
    }
    WindowEx_MoveToLayout(cs->pCursor, (char *)LAYOUT_GetLayoutData(0x411), name, 0);
}

static void __MVSG_ItemFadeOut(struct stMobj *pMobj)
{
    PS2_FRAME(0x60);
    VsgSys *cs = mvsg_sys(pMobj);

    for (int i = 0; i < play_info.player_num + play_info.com_num; i++) {
        if (cs->pinfo_wp[i]->winlp) UnitSprite_SetFadeOut(cs->pinfo_wp[i]->winlp);
        if (cs->pinfo_wp[i]->pStrPlayer) {
            DF_MOBJ_SetFadeOut(cs->pinfo_wp[i]->pStrPlayer);
            DF_MOBJ_SetFadeOut(cs->pinfo_wp[i]->namep);
            DF_MOBJ_SetFadeOut(cs->pinfo_wp[i]->winp);
            DF_MOBJ_SetFadeOut(cs->pinfo_wp[i]->losep);
            for (int k = 0; k < 2; k++) DF_MOBJ_SetFadeOut(cs->pinfo_wp[i]->colp[k]);
            DF_MOBJ_SetFadeOut(cs->pinfo_wp[i]->win_nump);
            DF_MOBJ_SetFadeOut(cs->pinfo_wp[i]->lose_nump);
            DF_MOBJ_SetFadeOut(cs->pinfo_wp[i]->pStrButton);
        }
    }
    if (cs->bfcp) UnitSprite_SetFadeOut(cs->bfcp);
    if (cs->pParts20d) DF_MOBJ_SetFadeOut(cs->pParts20d);
    if (cs->pCursor) {
        WindowEx_SetFadeOut(cs->pCursor);
        WindowEx_BlinkEnd(cs->pCursor);
        WindowEx_SetFadeOut(cs->pCursor);
    }
    DF_M1HELP_FadeOut();
}

static inline void mvsg_fade_in(struct stMobj *o)
{
    DF_MOBJ_SetDrawEnable(o, 1);
    DF_MOBJ_SetFadeIn(o);
}

static void __MVSG_ItemFadeIn(struct stMobj *pMobj)
{
    PS2_FRAME(0x60);
    VsgSys *cs = mvsg_sys(pMobj);

    for (int i = 0; i < play_info.player_num + play_info.com_num; i++) {
        if (cs->pinfo_wp[i]->winlp) {
            DF_MOBJ_SetDrawEnable(cs->pinfo_wp[i]->winlp, 1);
            UnitSprite_SetFadeIn(cs->pinfo_wp[i]->winlp);
        }
        if (cs->pinfo_wp[i]->pStrPlayer) {
            mvsg_fade_in(cs->pinfo_wp[i]->pStrPlayer);
            mvsg_fade_in(cs->pinfo_wp[i]->namep);
            mvsg_fade_in(cs->pinfo_wp[i]->winp);
            mvsg_fade_in(cs->pinfo_wp[i]->losep);
            for (int k = 0; k < 2; k++) mvsg_fade_in(cs->pinfo_wp[i]->colp[k]);
            mvsg_fade_in(cs->pinfo_wp[i]->win_nump);
            mvsg_fade_in(cs->pinfo_wp[i]->lose_nump);
            mvsg_fade_in(cs->pinfo_wp[i]->pStrButton);
        }
    }
    if (cs->bfcp) {
        DF_MOBJ_SetDrawEnable(cs->bfcp, 1);
        UnitSprite_SetFadeIn(cs->bfcp);
    }
    if (cs->pParts20d) mvsg_fade_in(cs->pParts20d);
    if (cs->pCursor) {
        DF_MOBJ_SetDrawEnable(cs->pCursor, 1);
        WindowEx_SetFadeIn(cs->pCursor);
        WindowEx_BlinkBegin(cs->pCursor);
    }
    DF_M1HELP_FadeIn();
}

static inline void mvsg_erase_field(struct stMobj **pp)
{
    if (*pp) {
        mvsg_erase(*pp);
        *pp = 0;
    }
}

static void __MVSG_ItemDelete(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    VsgSys *cs = mvsg_sys(pMobj);

    /* gated on cell 0's unit sprite only (ItemCreate always pushes) */
    if (cs->pinfo_wp[0]->winlp) DF_MBACK_PopSceneLabel();
    for (int i = 0; i < play_info.player_num + play_info.com_num; i++) {
        VsgCell *rec = cs->pinfo_wp[i];
        if (rec->winlp) {
            UnitSprite_Delete(rec->winlp);
            rec->winlp = 0;
        }
        mvsg_erase_field(&rec->pStrPlayer);
        mvsg_erase_field(&rec->namep);
        mvsg_erase_field(&rec->winp);
        mvsg_erase_field(&rec->losep);
        for (int k = 0; k < 2; k++) mvsg_erase_field(&rec->colp[k]);
        mvsg_erase_field(&rec->win_nump);
        mvsg_erase_field(&rec->lose_nump);
        mvsg_erase_field(&rec->pStrButton);
    }
    if (cs->bfcp) {
        UnitSprite_Delete(cs->bfcp);
        cs->bfcp = 0;
    }
    mvsg_erase_field(&cs->pParts20d);
    WindowEx_Delete(cs->pCursor);
    cs->pCursor = 0;
}

/* DF_MOBJ_NewStringFromLayoutName with the arguments every call site of ItemCreate shares */
static inline struct stMobj *mvsg_new_string(struct stMobj *pMobj, int id, char *name, unsigned int flag)
{
    char *lo = (char *)LAYOUT_GetLayoutData(0x411);
    return DF_MOBJ_NewStringFromLayoutName(pMobj, 0, id, 0, lo, name, 0, 0, flag);
}

static inline int mvsg_direction(int d)
{
    return __MVS_GetDirection(play_info.team_condition, play_info.player_num + play_info.com_num, d,
                              play_info.vs_form == 2);
}

static void __MVSG_ItemCreate(struct stMobj *pMobj)
{
    PS2_FRAME(0x1c0);
    PS2_STACK(char[256], name, 0xb0);
    /* team colours; only 3 words are written but the index is team_condition & 3: index 3 reads the caller frame's
     * word at sp+0x1bc (stale in the oracle, 0 in the source port) */
    PS2_STACK(unsigned int[4], col, 0x1b0);
    VsgSys *cs = mvsg_sys(pMobj);

    col[0] = 0x80808080u;
    col[1] = 0x801e1ec8u;
    col[2] = 0x80968080u;
    DF_MBACK_PushSceneLabel((enum_5124d09c)MBACK_LABEL_AC_SETUP);
    cs->max_idx = 0;
    memset(cs->dirTable, -1, 4);
    memset(cs->is_com, -1, 4);

    for (int i = 0; i < play_info.player_num + play_info.com_num; i++) {
        VsgCell *rec;
        int j = mvsg_direction(i);
        if (cs->pinfo_wp[i]->winlp == 0) {
            ps2_sprintf_d(name, mvsg_fmt_name, j);
            cs->pinfo_wp[i]->winlp = UnitSprite_New(pMobj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x410), name);
            DF_MOBJ_SetDrawEnable(cs->pinfo_wp[i]->winlp, 0);
            UnitSprite_SetMultiply(cs->pinfo_wp[i]->winlp,
                                   (struct struct_76b810ec *)&col[play_info.team_condition[i] & 3]);
        }
        if (cs->pinfo_wp[i]->pStrPlayer == 0) {
            ps2_sprintf_d(name, mvsg_fmt_parts20t, j);
            int id = i < play_info.player_num ? 0x18f88 : 0x18f89;
            cs->pinfo_wp[i]->pStrPlayer = mvsg_new_string(pMobj, id, name, 0);
            rec = cs->pinfo_wp[i];
            char tc = play_info.team_condition[i];
            const char *team = (tc & 3) == 0 ? mvsg_empty : (tc & 3) == 1 ? mvsg_red : mvsg_blue;
            if (i < play_info.player_num) {
                ps2_sprintf_dss(rec->strBuff, mvsg_fmt_player, i + 1, team, (tc & 0x10) == 0x10 ? mvsg_leader : mvsg_empty);
            } else {
                ps2_sprintf_dss(rec->strBuff, mvsg_fmt_com, i - play_info.player_num + 1, team,
                                (tc & 0x10) == 0x10 ? mvsg_leader : mvsg_space);
            }
            DF_MOBJ_SetStringDirect(cs->pinfo_wp[i]->pStrPlayer, cs->pinfo_wp[i]->strBuff);
        }
        if (cs->pinfo_wp[i]->namep == 0) {
            ps2_sprintf_d(name, mvsg_fmt_parts20f, j);
            cs->pinfo_wp[i]->namep = mvsg_new_string(pMobj, 0x18f7e + i, name, 0);
            DF_MOBJ_SetStringDirect(cs->pinfo_wp[i]->namep, cs->pinfo_wp[i]->name_strb);
        }
        if (cs->pinfo_wp[i]->winp == 0 && i < play_info.player_num) {
            ps2_sprintf_d(name, mvsg_fmt_parts20w, j);
            cs->pinfo_wp[i]->winp = mvsg_new_string(pMobj, 0x18f85, name, 0);
        }
        if (cs->pinfo_wp[i]->losep == 0 && i < play_info.player_num) {
            ps2_sprintf_d(name, mvsg_fmt_parts20l, j);
            cs->pinfo_wp[i]->losep = mvsg_new_string(pMobj, 0x18f86, name, 0);
        }
        for (int k = 0; k < 2; k++) {
            if (cs->pinfo_wp[i]->colp[k] == 0 && i < play_info.player_num) {
                ps2_sprintf_d(name, mvsg_fmt_parts20c, j * 2 + k);
                cs->pinfo_wp[i]->colp[k] = mvsg_new_string(pMobj, 0x186d7, name, 0);
            }
        }
        if (cs->pinfo_wp[i]->win_nump == 0 && i < play_info.player_num) {
            ps2_sprintf_d(name, mvsg_fmt_parts20wc, j);
            cs->pinfo_wp[i]->win_nump = mvsg_new_string(pMobj, 0x18f85, name, 0x10000);
            DF_MOBJ_SetStringDirect(cs->pinfo_wp[i]->win_nump, cs->pinfo_wp[i]->win_strb);
        }
        if (cs->pinfo_wp[i]->lose_nump == 0 && i < play_info.player_num) {
            ps2_sprintf_d(name, mvsg_fmt_parts20lc, j);
            cs->pinfo_wp[i]->lose_nump = mvsg_new_string(pMobj, 0x18f86, name, 0x10000);
            DF_MOBJ_SetStringDirect(cs->pinfo_wp[i]->lose_nump, cs->pinfo_wp[i]->lose_strb);
        }
    }

    /* the local player's button(s) */
    cs->max_idx = 0;
    char play_mode = play_info.play_mode;
    char self_id = play_info.self_id;
    int nloc = play_mode ? 2 : 1;
    if (self_id == 1 && play_info.connected_num == 2 && play_info.player_num == 3) nloc = 1;
    int base = self_id + (play_mode == 1 ? (self_id > 0) : 0);
    for (int k = 0; k < nloc; k++) {
        int d = mvsg_direction(base + k);
        ps2_sprintf_d(name, mvsg_fmt_parts20b, d);
        cs->pinfo_wp[d]->pStrButton = mvsg_new_string(pMobj, 0x18f88, name, 0);
        cs->max_idx++;
    }

    /* the COMs the local player controls */
    if (play_info.com_num > 0) {
        int kk = 0;
        for (int jj = 0; jj < play_info.com_num; jj++) {
            if (__MVS_GetControlEnable(jj, 0) == 1) {
                int d = mvsg_direction(play_info.player_num + jj);
                ps2_sprintf_d(name, mvsg_fmt_parts20b, d);
                cs->pinfo_wp[d]->pStrButton = mvsg_new_string(pMobj, 0x18f89, name, 0);
                cs->is_com[kk++] = (char)d;
                cs->max_idx++;
            }
        }
    }

    int n2 = 0;
    for (int i = 0; i < 4; i++)
        if (cs->pinfo_wp[i]->pStrButton) cs->dirTable[n2++] = (char)i;

    if (!cs->bfcp) {
        ps2_sprintf_d(name, mvsg_banner_0, 0);
        cs->bfcp = UnitSprite_New(pMobj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x410), name);
        DF_MOBJ_SetDrawEnable(cs->bfcp, 0);
    }
    if (!cs->pParts20d) {
        ps2_sprintf_d(name, mvsg_parts20d_22, 0);
        cs->pParts20d = mvsg_new_string(pMobj, 0x18f61, name, 0);
    }
    if (!cs->pCursor) {
        ps2_sprintf_d(name, mvsg_parts10_0, 0);
        cs->pCursor = WindowEx_NewFromLayout(pMobj, WINDOWEX_TYPE_LIGHTCURSOR_SPOT, (char *)LAYOUT_GetLayoutData(0x411),
                                             name);
        DF_MOBJ_SetDrawEnable(cs->pCursor, 0);
    }
    DF_M1HELP_SetWindow(); /* empty stub; the original passes (0, 0x280) */
    DF_M1HELP_SetString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x18f88));
}

/* 1 when AC inIdx of player inPlayer is complete: parts 0-3 and 5-7 set, and a booster (4) unless the legs are of
 * category 0xf / 0xd */
int DF_MVS_CheckConpose(int inPlayer, int inIdx)
{
    PS2_FRAME(0x40);
    int jj;
    for (jj = 0; jj < 4; jj++)
        if (PlayerInfo_GetAssemblyPartsId(inPlayer, inIdx, jj) == 0) return 0;
    for (jj = 5; jj < 8; jj++)
        if (PlayerInfo_GetAssemblyPartsId(inPlayer, inIdx, jj) == 0) return 0;
    int leg = DF_MPART_TrueID2Index(3, PlayerInfo_GetAssemblyPartsId(inPlayer, inIdx, 3));
    if (PlayerInfo_GetAssemblyPartsId(inPlayer, inIdx, 4) != 0) return 1;
    if (DF_MPART_GetCategory(leg) == 0xf) return 1;
    if (DF_MPART_GetCategory(leg) == 0xd) return 1;
    return 0;
}

static void __MVSG_SetWindowStat(void) {}

/* ---- group 2: link poll, control / direction leaves, dispatch ---- */

#define MVSG_FN(f) ((void (*)(void))GFN(f))

/* DF_MBGUIDE_Change / DF_MBACK_ChangeTag are void(void) in DWARF but read a0..t0 / a0 */
static inline void mvsg_guide(int a, int b, int c, int d, int e)
{
    gcall_as<int(int, int, int, int, int)>(GFN(DF_MBGUIDE_Change), a, b, c, d, e);
}
static inline void mvsg_tag(int tag)
{
    gcall_as<void(int)>(GFN(DF_MBACK_ChangeTag), tag);
}

/* link shutdown: five fslnk calls, each followed by one vblank wait */
static inline void mvsg_link_teardown(void)
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

/* names of the friend-table COMs (com_id < 100) of cells player_num..3 */
static inline void mvsg_names_refresh(VsgSys *cs)
{
    for (int k = 0; k < 4; k++) {
        int pn = play_info.player_num;
        if (k < pn) continue;
        int cid = play_info.com_id[k - pn];
        if (cid < 100) ps2_sprintf_s(cs->pinfo_wp[k]->name_strb, mvsg_fmt_s, (char *)FriendTab + (cid << 8));
    }
}

/* packet "3/4, 5, sub, k, id": a peer changed COM k (sub 1) or announces a data packet (sub 0) */
static inline void mvsg_recv_com(VsgSys *cs, char *hdr, int i)
{
    if (hdr[2] == 0) {
        play_info.stay[i] = 1;
    } else if (hdr[2] == 1) {
        play_info.com_id[(int)hdr[3]] = hdr[4];
        int k = hdr[3];
        if (play_info.com_id[k] >= 100) {
            play_info.ai_mode[play_info.player_num + k] |= 1;
        } else {
            char *dst = cs->pinfo_wp[play_info.player_num + k]->name_strb;
            if (hdr[4] != 0)
                ps2_sprintf_s(dst, mvsg_fmt_s, (char *)DF_MSTR_GetName(MenuMng.language, 0, hdr[4] + 0x7a183));
        }
    }
}

/* a peer reported an error (packet cmd 0x63): error dialog func unless already idle in the error state */
static inline void mvsg_peer_error(struct stMobj *obj, VsgSys *cs, int func)
{
    if (obj->stat != 3 || cs->func != 0) {
        if (DF_MDIALOG_Type() != 0) DF_MDIALOG_Close();
        cs->step = 0;
        cs->func = func;
        obj->play_func = MVSG_FN(__PlayACSetupDispatch);
        obj->stat = 3;
    }
    cs->f_error = 1;
}

/* per-frame link poll (draw_func slot): link loss / peer drop -> error state; otherwise read each peer's packets */
static void __DrawAcSetupSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x260);
    PS2_STACK(char[0x180], wkd, 0xc0);
    PS2_STACK(char[8], send9, 0x240);
    PS2_STACK(char[8], send7, 0x248);
    PS2_STACK(char[8], send6, 0x250);
    PS2_STACK(char[8], hdr, 0x258);
    VsgSys *cs = mvsg_sys(ioObjp);

    if (play_info.connected_num == 1) return;

    int r = fslnk_LinkProc();
    if (r < 0) {
        fslnk_GetConnectedNum();
        mvsg_link_teardown();
        if (ioObjp->stat == 3) {
            ioObjp->draw_func = 0;
            cs->f_error = (r == -0x10) ? 2 : 1;
        } else {
            cs->step = 0;
            cs->func = (r == -0x10) ? 0x64 : 0x63;
            ioObjp->play_func = MVSG_FN(__PlayACSetupDispatch);
            ioObjp->draw_func = 0;
            ioObjp->stat = 3;
        }
        return;
    }
    int cn = play_info.connected_num;
    if (cn != fslnk_GetConnectedNum() + 1) {
        fslnk_GetConnectedNum();
        mvsg_link_teardown();
        if (ioObjp->stat == 3) {
            ioObjp->draw_func = 0;
            cs->f_error = 2;
        } else {
            cs->step = 0;
            cs->func = 0x64;
            ioObjp->play_func = MVSG_FN(__PlayACSetupDispatch);
            ioObjp->draw_func = 0;
            ioObjp->stat = 3;
        }
        return;
    }
    if (ioObjp->stat != 0 && ioObjp->stat != 3) return;
    if (cn - 1 <= 0) return;

    int i = 0;
    do {
        if (play_info.stay[i] == 1) {
            /* data packet: AC index, win / lose counters, name at +0x40 */
            if (fslnk_LinkRecvDataSize(i) >= 0x180) {
                fslnk_LinkRecvData(wkd, i, 0x180);
                int idx = ((int *)wkd)[0];   /* off the wire, unchecked */
                struct struct_aa25ad39 *ac = &vs_ac_info[idx];
                ac->winnum = (unsigned short)((int *)wkd)[1];
                ac->losenum = (unsigned short)((int *)wkd)[2];
                memcpy(ac->name, wkd + 0x40, 0x30);
                if (!(play_info.ai_mode[idx] & 1)) {
                    ps2_sprintf_s(cs->pinfo_wp[idx]->name_strb, mvsg_fmt_s, ac->name);
                    ps2_sprintf_d(cs->pinfo_wp[idx]->win_strb, mvsg_fmt_d, ac->winnum);
                    ps2_sprintf_d(cs->pinfo_wp[idx]->lose_strb, mvsg_fmt_d, ac->losenum);
                }
                play_info.stay[i] = 0;
            }
            continue;
        }
        if ((unsigned int)fslnk_LinkRecvDataSize(i) < 8) continue;
        fslnk_LinkRecvData(hdr, i, 8);
        if (hdr[0] == 3) {
            switch (hdr[1]) {
            case 1:
                cs->sstp[(int)hdr[2]] = 1;
                break;
            case 2:
                if (play_info.self_id != 0) DF_MBGUIDE_Hide();
                cs->func = 0;
                __SetACSetupSystemFadeOut(ioObjp);
                break;
            case 5:
                mvsg_recv_com(cs, hdr, i);
                play_info.lock[i] = 0;
                break;
            case 6:
                play_info.lock[i] = 1;
                memset(send6, 0, 8);
                send6[0] = 3;
                send6[1] = 8;
                fslnk_LinkSendData(send6, 8);
                break;
            case 7:
                play_info.lock[i] = 2;
                memset(send7, 0, 8);
                send7[0] = 3;
                send7[1] = 8;
                fslnk_LinkSendData(send7, 8);
                break;
            case 8:
                if (cs->gStay == 1) {
                    cs->col[i] = 1;
                    int n = play_info.connected_num - 1;
                    int c = 0;
                    for (int j = 0; j < n; j++)
                        if (cs->col[j] == 1) c++;
                    if (c >= n) {
                        for (int j = 0; j < play_info.connected_num - 1; j++) cs->col[j] = 0;
                        cs->gStay = 2;
                    }
                }
                break;
            case 9: {
                /* a peer released COM hdr[2]: take back every COM slot it held that this side may control */
                int sent = 0;
                for (int j = 0; j < play_info.com_num; j++) {
                    if (play_info.com_id[j] == hdr[2] + 100 && __MVS_GetControlEnable(j, 0) == 1) {
                        play_info.com_id[j] = 5;
                        play_info.ai_mode[play_info.player_num + j] &= ~1;
                        memset(send9, 0, 8);
                        send9[0] = 3;
                        send9[1] = 5;
                        send9[2] = 1;
                        send9[3] = (char)j;
                        send9[4] = play_info.com_id[j];
                        fslnk_LinkSendData(send9, 8);
                        sent = 1;
                    }
                }
                if (sent) {
                    mvsg_names_refresh(cs);
                    /* the original refreshes with the poll loop's own counter (s4): the outer loop ends after it */
                    i = 4;
                    if (ioObjp->stat != 3 || cs->func != 0) {
                        cs->step = 0;
                        cs->func = 0x61;
                        ioObjp->play_func = MVSG_FN(__PlayACSetupDispatch);
                        ioObjp->stat = 3;
                    } else {
                        cs->f_changed = 1;
                    }
                }
                break;
            }
            case 0x12:
                play_info.lock[i] = 0;
                break;
            default:
                break;
            }
        } else if (hdr[0] == 4) {
            if (hdr[1] == 5)
                mvsg_recv_com(cs, hdr, i);
            else if (hdr[1] == 0x63)
                mvsg_peer_error(ioObjp, cs, 0x65);
        } else if (hdr[1] == 0x63) {
            mvsg_peer_error(ioObjp, cs, 0x63);
        }
    } while (++i < play_info.connected_num - 1);
}

/* 1 when the local side controls slot inIdx: mode 0 = COM slot player_num + inIdx, mode 1 = player slot inIdx */
int __MVS_GetControlEnable(int dir, int mode)
{
    char *tc = play_info.team_condition;
    int self = play_info.self_id;
    int me;
    if (mode == 0) {
        int add = 0;
        if (play_info.play_mode == 1) add = play_info.self_id > 0;
        me = play_info.self_id + add;
    } else {
        me = self * 2;
    }
    int pn = play_info.player_num;
    if (pn == 1) return 1;
    if (play_info.connected_num == 1) return 1;
    switch (play_info.vs_form) {
    case 1:
    case 2:
        if (mode != 0) return (tc[dir] & 3) == (tc[me] & 3);
        {
            int a = tc[pn + dir] & 3;
            if ((tc[me] & 3) == a) return 1;
            if (play_info.local_player_num == 2 && (tc[me + 1] & 3) == a) return 1;
            if (pn == 2 && self == 0 && (tc[0] & 3) == (tc[1] & 3)) return 1;
            return 0;
        }
    case 0:
        return self == 0;
    default:
        return 0;
    }
}

/* display slot ("rank") of participant d among num: lower teams first, then by index (mode 1) or leader first
 * (mode 0) */
int __MVS_GetDirection(char *cond, int num, int d, int mode)
{
    int rank = 0;
    for (int j = 0; j < num; j++) {
        if (j == d) continue;
        if (mode != 0) {
            int ti = cond[d] & 3;
            int tj = cond[j] & 3;
            if (tj < ti) rank++;
            else if (j < d && ti == tj) rank++;
        } else {
            int tj = cond[j] & 3;
            int c = cond[d];
            int ti = c & 3;
            if (tj < ti) rank++;
            else if (ti != 0 && ti == tj && !(c & 0x10)) rank++;
            else if (ti == 0 && ti == tj && j < d) rank++;
        }
    }
    return rank;
}

/* DF_MBGUIDE_Change(0, 0xe, 0, 0, 0x63) + MDIALOG type 5 with category-1 string 0x30000 | id */
static inline void mvsg_dialog_open(int id)
{
    mvsg_guide(0, 0xe, 0, 0, 0x63);
    DF_MDIALOG_Open(5, DF_MSTR_GetInfo(MenuMng.language, 1, 0x30000 | id), 0, 0, 0, 0, 0, 0);
}

/* key guide back to the screen's list */
static inline void mvsg_guide_restore(void)
{
    mvsg_guide(0, 1, 2, 0, 0x63);
}

/* leave the screen with the error latched (draw_func is cleared before the fade-out setter runs) */
static inline void mvsg_finish(struct stMobj *obj, VsgSys *cs)
{
    play_info.f_ready = -1;
    cs->f_error = 1;
    cs->func = 0;
    obj->draw_func = 0;
    __SetACSetupSystemFadeOut(obj);
}

/* step 0 of a dialog state: open it once (Type() == 0), else close it when Sync() says 2; returns 1 when opened */
static inline int mvsg_dialog_step0(int id)
{
    if (DF_MDIALOG_Type() == 0) {
        mvsg_dialog_open(id);
        return 1;
    }
    if (DF_MDIALOG_Sync() == 2) DF_MDIALOG_Close();
    return 0;
}

/* link error dialogs 0x63 / 0x64 / 0x65: open + tear the link down, then leave when the dialog is done */
static inline void mvsg_error_dialog(struct stMobj *obj, VsgSys *cs, int id)
{
    if (cs->step == 1) {
        if (DF_MDIALOG_Sync() & 4) mvsg_finish(obj, cs);
    } else if (cs->step == 0) {
        if (mvsg_dialog_step0(id)) {
            mvsg_link_teardown();
            cs->step = 1;
        }
    }
}

/* link-sync / error state machine (play_func), state cs->func, sub-state cs->step */
static void __PlayACSetupDispatch(struct stMobj *ioObjp)
{
    PS2_FRAME(0x220);
    PS2_STACK(char[0x180], wkd, 0x80);
    PS2_STACK(char[8], send62, 0x208);
    PS2_STACK(char[8], send_ac, 0x210);
    PS2_STACK(char[8], send_com, 0x218);
    VsgSys *cs = mvsg_sys(ioObjp);

    switch (cs->func) {
    case 0x65:
        mvsg_error_dialog(ioObjp, cs, 0xed8);
        break;
    case 0x64:
        mvsg_error_dialog(ioObjp, cs, 0xed3);
        break;
    case 0x63:
        mvsg_error_dialog(ioObjp, cs, 0xed0);
        break;
    case 0x62:
        if (cs->step == 1) {
            if (DF_MDIALOG_Sync() & 4) {
                mvsg_guide_restore();
                mvsg_names_refresh(cs);
                cs->step = 2;
                ioObjp->stat = 1;
                ioObjp->count = 0xf;
                ioObjp->play_func = MVSG_FN(__PlayACSetupSystemFadeIn);
                ioObjp->draw_func = MVSG_FN(__DrawAcSetupSystem);
            }
        } else if (cs->step == 0) {
            if (mvsg_dialog_step0(0xed4)) {
                memset(send62, 0, 8);
                send62[0] = 3;
                send62[1] = 0x12;
                fslnk_LinkSendData(send62, 8);
                cs->step = 1;
            }
        }
        break;
    case 0x61:
        if (cs->step == 1) {
            if (DF_MDIALOG_Sync() & 4) {
                mvsg_guide_restore();
                ioObjp->play_func = MVSG_FN(__PlayACSetupSystem);
                ioObjp->stat = 0;
            }
        } else if (cs->step == 0) {
            if (mvsg_dialog_step0(0xed6)) cs->step = 1;
        }
        break;
    case 1: {
        int s = DF_MDIALOG_Sync();
        if ((s & 4) && s == 4) {
            mvsg_guide_restore();
            if (cs->f_error == 1) {
                cs->func = 0x63;
                cs->step = 0;
            } else if (cs->f_error == 2) {
                cs->func = 0x64;
                cs->step = 0;
            } else {
                ioObjp->play_func = MVSG_FN(__PlayACSetupSystem);
                ioObjp->stat = 0;
            }
        }
        break;
    }
    case 0: {
        /* the original reads csysp->stat before its NULL test */
        struct stMobj *c = cs->csysp;
        unsigned char st = c ? c->stat : ((const unsigned char *)PS2_NULL_MEM())[2];
        if (st != 5) return;
        if (c) {
            c->f_draw = 0;
            cs->csysp->f_erase = 1;
            cs->csysp = 0;
        }
        if (MenuMng.stat == 3) {
            mvsg_finish(ioObjp, cs);
            return;
        }
        if (cs->f_error == 1) {
            cs->func = 0x63;
            cs->step = 0;
            return;
        }
        if (cs->f_error == 2) {
            cs->func = 0x64;
            cs->step = 0;
            return;
        }
        if (play_info.f_ready != 0) {
            cs->func = 0;
            ioObjp->play_func = MVSG_FN(__PlayACSetupSystemFadeOut);
            ioObjp->stat = 2;
            return;
        }
        if (cs->step == -1) {
            cs->step = 0;
            cs->func = 0x62;
            ioObjp->play_func = MVSG_FN(__PlayACSetupDispatch);
            ioObjp->draw_func = MVSG_FN(__DrawAcSetupSystem);
            return;
        }
        if (cs->step >= 0x64) {
            /* local COM choice of slot s1 done: sync its id with the AC record and tell the peers */
            int s1 = cs->step - 0x64;
            struct struct_aa25ad39 *ac = &vs_ac_info[play_info.player_num + s1];
            int s0;
            if (ac->com_id != 0) {
                s0 = ac->com_id;
                play_info.com_id[s1] = ac->com_id;
            } else {
                s0 = play_info.com_id[s1];
                ac->com_id = (char)s0;
            }
            mvsg_tag(0x24);
            mvsg_guide_restore();
            if (s0 < 100)
                ps2_sprintf_s(cs->pinfo_wp[play_info.player_num + s1]->name_strb, mvsg_fmt_s,
                              vs_ac_info[play_info.player_num + s1].name_com);
            if (play_info.self_id == play_info.connected_num - 1) {
                if (play_info.live_monitor == 0) DF_MBGUIDE_Show();
            } else if (play_info.live_monitor == 1 || play_info.live_monitor == 0) {
                DF_MBGUIDE_Show();
            }
            memset(send_com, 0, 8);
            send_com[0] = 3;
            send_com[3] = (char)s1;
            send_com[1] = 5;
            send_com[4] = (char)s0;
            send_com[2] = 1;
            fslnk_LinkSendData(send_com, 8);
        } else {
            /* local AC (save slot step) chosen: copy name / record into the AC slot and send it to the peers */
            int s2 = cs->stid + cs->step;
            struct struct_aa25ad39 *ac = &vs_ac_info[s2];
            mvsg_tag(0x24);
            mvsg_guide_restore();
            memcpy(ac->name, g_gamedata[(int)cs->step].name, 0x30);
            ac->winnum = g_gamedata[(int)cs->step].vswin;
            ac->losenum = g_gamedata[(int)cs->step].vsnum - g_gamedata[(int)cs->step].vswin;
            ps2_sprintf_s(cs->pinfo_wp[s2]->name_strb, mvsg_fmt_s, ac->name);
            ps2_sprintf_d(cs->pinfo_wp[s2]->win_strb, mvsg_fmt_d, ac->winnum);
            ps2_sprintf_d(cs->pinfo_wp[s2]->lose_strb, mvsg_fmt_d, ac->losenum);
            memset(send_ac, 0, 8);
            send_ac[0] = 3;
            send_ac[2] = 0;
            send_ac[1] = 5;
            fslnk_LinkSendData(send_ac, 8);
            memset(wkd, 0, 0x180);
            ((int *)wkd)[0] = s2;
            ((int *)wkd)[1] = ac->winnum;
            ((int *)wkd)[2] = ac->losenum;
            memcpy(wkd + 0x40, ac->name, 0x30);
            fslnk_LinkSendData(wkd, 0x180);
        }
        cs->step = 2;
        ioObjp->stat = 1;
        ioObjp->count = 0xf;
        ioObjp->play_func = MVSG_FN(__PlayACSetupSystemFadeIn);
        break;
    }
    default:
        break;
    }
}

/* ---- group 3a: link stay, fade-out hand-off ---- */

/* the situation window goes with this screen (same predicate in the fade-out setter and the fade-out) */
static inline int mvsg_sit_del(VsgSys *cs)
{
    int f;
    if (play_info.vs_form == 0 && play_info.self_id == 0) return 1;
    f = cs->func;
    if (f == 0) {
        if (cs->f_error == 1) return 1;
        if (play_info.player_num == 4 && play_info.self_id == 0) return 1;
    }
    if (f == 1) return 1;
    return f == 2 && play_info.connected_num == 1;
}

/* cell index whose direction is want, -1 if none */
static inline int mvsg_find_cell(int want)
{
    int i;
    for (i = 0; i < play_info.player_num + play_info.com_num; i++) {
        if (__MVS_GetDirection(play_info.team_condition, play_info.player_num + play_info.com_num, i,
                               play_info.vs_form == 2) == want)
            return i;
    }
    return -1;
}

/* local start: copy player pl's current AC + cockpit options into vs_ac_info[pl], small emblem into EmblemTM2[pl] */
static inline void mvsg_copy_local_ac(int pl)
{
    struct struct_aa25ad39 *ac = &vs_ac_info[pl];
    struct StAcrtGameData *gd = &g_gamedata[pl];
    CopyMemory(ac, PlayerInfo_GetAssemblyInfo(pl, PlayerInfo_GetCurrentAcNo(pl)), 0x120);
    ac->option.cockpit = gd->cockpit;
    ac->option.cockpitcol = gd->cockpitcol;
    ac->option.plus = gd->plus;
    memcpy(&ac->option.keyconf, &gd->option.keyconf, 0x30);
    DF_EMB_MakeSmallEmblem(ac->embp, EmblemTM2[pl]);
}

/* play_func while a link game waits for the peers after the local confirm (301-frame timeout) */
static void __PlayACSetupSystemStay(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    VsgSys *cs = mvsg_sys(ioObjp);
    int t = cs->timeout;

    cs->timeout = t + 1;
    if (t >= 0x12d) {
        mvsg_guide(0, 0xe, 0, 0, 0x63);
        DF_MDIALOG_Open(5, DF_MSTR_GetInfo(MenuMng.language, 1, 0x30ed0), 0, 0, 0, 0, 1, 0);
        mvsg_link_teardown();
        cs->step = 1;
        cs->func = 0x63;
        ioObjp->play_func = MVSG_FN(__PlayACSetupDispatch);
        ioObjp->stat = 3;
    } else if (cs->gStay == 2) {
        __SetACSetupSystemFadeOut(ioObjp);
        cs->gStay = 0;
    }
}

/* play_func after the fade-out setter: delete the cells and hand off by cs->func (0 back, 1 garage / ranking,
 * 2 start); step 1 waits for the transition */
static void __PlayACSetupSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    VsgSys *cs = mvsg_sys(ioObjp);

    if (cs->step == 1) {
        if (play_info.f_ready < 0) {
            if (DF_MBACK_Sync()) {
                ioObjp->play_func = 0;
                ioObjp->stat = 5;
            }
        } else if (DF_MVS_TransModelVSAC() && DF_MBACK_Sync() && DF_MVS_TransTextureVSAC()) {
            DF_MVS_SetVSForm();
            ioObjp->play_func = 0;
            ioObjp->stat = 5;
        }
        return;
    }
    if (cs->whsp && mvsg_sit_del(cs)) DF_MVS_DeleteSituationWindow(cs->whsp);
    __MVSG_ItemDelete(ioObjp);

    switch (cs->func) {
    case 0:
        if (play_info.f_ready == 0) {
            mvsg_tag(0x22);
            if (play_info.self_id == 0)
                mvsg_guide(0, 1, 2, 0, 0x63);
            else
                mvsg_guide(0, 0, 2, 0, 0x63);
        }
        if (cs->f_start != 0 || cs->f_error != 0) {
            mvsg_guide(0, 0, 0, 0, 0x63);
            gcall_as<void(int)>(GFN(DF_MBACK_SetFadeOutNX), 0);
            Bgm_FadeOutStart();
            cs->step++;
        } else {
            MenuWorkp = cs->defTBP;
            ioObjp->play_func = 0;
            ioObjp->draw_func = 0;
            ioObjp->stat = 5;
        }
        break;
    case 1: {
        int s1 = mvsg_find_cell(cs->dirTable[(int)cs->idx]);
        if (s1 >= 0 && s1 < play_info.player_num) {
            int p = play_info.play_mode == 1 ? (s1 & 1) : 0;
            g_gamedata[p].vswin = vs_ac_info[cs->stid + p].winnum;
            g_gamedata[p].vsnum = vs_ac_info[cs->stid + p].winnum + vs_ac_info[cs->stid + p].losenum;
            MenuMng.stat = 0;
            cs->csysp = DF_MGARAGE_NewSystem(ioObjp, 1, p, p);
            cs->step = p;
        } else {
            /* s1 == -1 (no cell matched) hands over the record before vs_ac_info[0] (original, unchecked) */
            cs->csysp = MArenaRanking_NewSystem(ioObjp, 2, (char *)vs_ac_info + s1 * (int)sizeof(vs_ac_info[0]));
            cs->step = s1 - play_info.player_num + 0x64;
        }
        cs->func = 0;
        ioObjp->play_func = MVSG_FN(__PlayACSetupDispatch);
        ioObjp->stat = 3;
        break;
    }
    case 2:
        if (play_info.connected_num == 1) {
            mvsg_copy_local_ac(0);
            if (play_info.play_mode == 1) mvsg_copy_local_ac(1);
            MenuWorkp = cs->defTBP;
            mvsg_guide(0, 0, 0, 0, 0x63);
            gcall_as<void(int)>(GFN(DF_MBACK_SetFadeOutNX), 0);
            Bgm_FadeOutStart();
            cs->step++;
        } else {
            cs->csysp = DF_MVSW2_NewSystem(ioObjp, cs->whsp);
            cs->func = 0;
            cs->step = -1;
            ioObjp->play_func = MVSG_FN(__PlayACSetupDispatch);
            ioObjp->draw_func = 0;
            ioObjp->stat = 3;
        }
        break;
    default:
        break;
    }
}

/* start the fade-out: close a dialog, fade the situation window and the cells, play_func = fade-out
 * (func 1 = garage / ranking keeps the link poll in draw_func) */
static void __SetACSetupSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    VsgSys *cs = mvsg_sys(ioObjp);

    if (DF_MDIALOG_Type() != 0) DF_MDIALOG_Close();
    if (cs->whsp && mvsg_sit_del(cs)) DF_MVS_SetSituationWindowFadeOut(cs->whsp);
    __MVSG_ItemFadeOut(ioObjp);
    cs->step = 0;
    ioObjp->play_func = MVSG_FN(__PlayACSetupSystemFadeOut);
    if (cs->func != 1) ioObjp->draw_func = 0;
    ioObjp->stat = 2;
}

/* ---- group 3b: idle input, fade-in, constructor ---- */

typedef unsigned char struct_2d84b07b::*MvsgPadField;

/* button newly pressed on pad 0 (if a local player) or pad 1 (if two local players) */
static inline int mvsg_pressed(MvsgPadField f)
{
    int lp = play_info.local_player_num;
    if ((lp > 0 ? MPad[0].*f : 0) == 1) return 1;
    return (lp >= 2 ? MPad[1].*f : 0) == 1;
}

/* the button on the pad that owns player cell s (s >= 0) */
static inline int mvsg_pad_owner(int s, MvsgPadField f)
{
    int a;
    if (play_info.play_mode == 0) return play_info.local_player_num > 0 ? MPad[0].*f : 0;
    a = s % 2;
    return a < play_info.local_player_num ? MPad[a].*f : 0;
}

/* dialog with category-1 string strid (button-off 1), then hand the screen to the dispatcher (func 1) */
static inline void mvsg_dialog_dispatch(struct stMobj *obj, VsgSys *cs, unsigned int strid)
{
    mvsg_guide(0, 0xe, 0, 0, 0x63);
    DF_MDIALOG_Open(5, DF_MSTR_GetInfo(MenuMng.language, 1, strid), 0, 0, 0, 0, 1, 0);
    cs->func = 1;
    obj->play_func = MVSG_FN(__PlayACSetupDispatch);
    obj->stat = 3;
}

/* local start: player pl's current AC + cockpit options into vs_ac_info[pl] (player 0 reads the cockpit word with
 * lh: low half sign-extended), small emblem into EmblemTM2[pl] */
static inline void mvsg_copy_start_ac(int pl)
{
    struct struct_aa25ad39 *ac = &vs_ac_info[pl];
    struct StAcrtGameData *gd = &g_gamedata[pl];
    CopyMemory(ac, PlayerInfo_GetAssemblyInfo(pl, PlayerInfo_GetCurrentAcNo(pl)), 0x120);
    ac->option.cockpit = pl == 0 ? (unsigned int)(int)(short)gd->cockpit : gd->cockpit;
    ac->option.cockpitcol = gd->cockpitcol;
    ac->option.plus = gd->plus;
    ac->option.level = gd->option.level;
    ac->option.view_mode = gd->isUseCockpit;
    memcpy(&ac->option.keyconf, &gd->option.keyconf, 0x30);
    DF_EMB_MakeSmallEmblem(ac->embp, EmblemTM2[pl]);
}

/* ready toggle on a player cell (ai_mode bit 1 = toggleable): ai_mode = 2 | !bit0; returns the cell or -1 */
static inline int mvsg_ready_toggle(VsgSys *cs, MvsgPadField f)
{
    int s = mvsg_find_cell(cs->dirTable[(int)cs->idx]);
    char *q;
    int a;
    if (s < 0 || s >= play_info.player_num) return -1;
    q = &play_info.ai_mode[s];
    a = *q & 2;
    if (a == 0 || mvsg_pad_owner(s, f) == 0) return -1;
    *q = (char)(a | (1 - (*q & 1)));
    __MVSG_SetWindowStat();
    return s;
}

/* play_func of the idle screen: cancel, confirm (cell -> garage / ranking, Start entry -> match), ready toggle,
 * cursor */
static void __PlayACSetupSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x80);
    PS2_STACK(char[8], send_tg, 0x60);
    PS2_STACK(char[8], send_cf, 0x68);
    PS2_STACK(char[8], send_go, 0x70);
    PS2_STACK(char[8], send_cn, 0x78);
    VsgSys *cs = mvsg_sys(ioObjp);
    char old = cs->idx;
    int i, n, s, lock, noc, p, lp;
    unsigned char m;
    char *q;

    if (MenuMng.stat == 0xc) {
        play_info.f_ready = -1;
        cs->f_error = 1;
        goto cancel;
    }
    if (MenuKeyGuide_IsShown()) return;
    if (MPad[MPadUser].start == 1) {
        MenuKeyGuide_Show(0x31);
        return;
    }
    /* cancel (circle, pad 0 only); a peer lock opens its dialog instead */
    if ((play_info.local_player_num > 0 ? MPad[0].Rrt : 0) == 1) {
        n = play_info.connected_num - 1;
        lock = 0;
        for (i = 0; i < n; i++) {
            if (play_info.lock[i] != 0) {
                lock = play_info.lock[i];
                break;
            }
        }
        if (lock != 0) {
            mvsg_dialog_dispatch(ioObjp, cs, 0x30e75 + lock);
            return;
        }
    cancel:
        Voice_Call(0x3eb, 0x7f);
        if (play_info.self_id != 0) DF_MBGUIDE_Hide();
        cs->func = 0;
        __SetACSetupSystemFadeOut(ioObjp);
        memset(send_cn, 0, 8);
        send_cn[0] = 3;
        send_cn[1] = 2;
        fslnk_LinkSendData(send_cn, 8);
        return;
    }
    /* confirm (cross) */
    if (mvsg_pressed(&struct_2d84b07b::Rdw)) {
        if (cs->idx == cs->max_idx && MPad[0].Rdw == 1) {
            /* Start entry (pad 0 only) */
            Voice_Call(0x3ea, 0x7f);
            p = play_info.self_id + (play_info.play_mode == 1 ? play_info.self_id > 0 : 0);
            q = &play_info.ai_mode[p];
            if (((q[0] & 1) != 1 && !DF_MVS_CheckConpose(0, PlayerInfo_GetCurrentAcNo(0))) ||
                (play_info.local_player_num == 2 && (q[1] & 1) != 1 &&
                 !DF_MVS_CheckConpose(1, PlayerInfo_GetCurrentAcNo(1)))) {
                mvsg_dialog_dispatch(ioObjp, cs, 0x30ed2);
                return;
            }
            /* unchecked index: map_id 0x17 (random) reads the 0 after the table, which is in mvs_noc_id */
            m = MVSG_MAP_ID[(int)play_info.map_id];
            noc = 0;
            for (i = 0; i < 7; i++) {
                if (m == mvs_noc_id[i]) {
                    noc = 1;
                    break;
                }
            }
            if (noc && ((q[0] & 1) == 1 || (play_info.local_player_num == 2 && (q[1] & 1) == 1))) {
                mvsg_dialog_dispatch(ioObjp, cs, 0x30ed5);
                return;
            }
            cs->func = 2;
            if (play_info.connected_num == 1) {
                mvsg_copy_start_ac(0);
                if (play_info.play_mode == 1) mvsg_copy_start_ac(1);
                play_info.player_ai_num = 0;
                for (i = 0; i < play_info.player_num; i++)
                    if ((play_info.ai_mode[i] & 1) == 1) play_info.player_ai_num++;
                play_info.com_ai_num = 0;
                for (i = 0; i < play_info.com_num; i++)
                    if (play_info.com_id[i] >= 100) play_info.com_ai_num++;
                play_info.f_ready = 1;
                cs->f_start = 1;
                play_info.f_setuped = 1;
                DF_MBGUIDE_Hide();
                __SetACSetupSystemFadeOut(ioObjp);
            } else {
                play_info.f_setuped = 1;
                DF_MBGUIDE_Hide();
                cs->gStay = 1;
                cs->timeout = 0;
                ioObjp->play_func = MVSG_FN(__PlayACSetupSystemStay);
                memset(send_go, 0, 8);
                send_go[0] = 3;
                send_go[1] = 7;
                fslnk_LinkSendData(send_go, 8);
            }
            return;
        }
        /* cell confirm (also a pad-1 cross on the Start entry: dirTable[max_idx]) */
        s = mvsg_find_cell(cs->dirTable[(int)cs->idx]);
        if (s >= 0) {
            int ok;
            if (s < play_info.player_num)
                ok = (play_info.play_mode == 0 ? MPad[0].Rdw : MPad[s % 2].Rdw) != 0;
            else
                ok = MPad[0].Rdw != 0;
            if (ok) {
                Voice_Call(0x3ea, 0x7f);
                cs->func = 1;
                if (play_info.connected_num == 1) {
                    __SetACSetupSystemFadeOut(ioObjp);
                } else {
                    cs->gStay = 1;
                    cs->timeout = 0;
                    ioObjp->play_func = MVSG_FN(__PlayACSetupSystemStay);
                    memset(send_cf, 0, 8);
                    send_cf[0] = 3;
                    send_cf[1] = 6;
                    fslnk_LinkSendData(send_cf, 8);
                }
                return;
            }
        }
    }
    /* ready toggle: rt is sent to the peers, lt is not */
    if (mvsg_pressed(&struct_2d84b07b::rt)) {
        s = mvsg_ready_toggle(cs, &struct_2d84b07b::rt);
        if (s >= 0) {
            memset(send_tg, 0, 8);
            send_tg[2] = (char)s;
            send_tg[0] = 3;
            send_tg[1] = 0x11;
            send_tg[3] = play_info.ai_mode[s];
            fslnk_LinkSendData(send_tg, 8);
        }
    }
    if (mvsg_pressed(&struct_2d84b07b::lt)) mvsg_ready_toggle(cs, &struct_2d84b07b::lt);
    /* cursor (key repeat) */
    lp = play_info.local_player_num;
    if ((lp > 0 && MPad[0].repeat.up) || (lp >= 2 && MPad[1].repeat.up)) {
        Voice_Call(0x3e9, 0x7f);
        cs->idx--;
        if (cs->idx < 0) cs->idx = cs->max_idx;
    }
    lp = play_info.local_player_num;
    if ((lp > 0 && MPad[0].repeat.dw) || (lp >= 2 && MPad[1].repeat.dw)) {
        Voice_Call(0x3e9, 0x7f);
        cs->idx++;
        if (cs->idx > cs->max_idx) cs->idx = 0;
    }
    if (old != cs->idx) __MVSG_ItemSetCursor(ioObjp);
}

/* play_func after the constructor: two idle frames, then (once the 3D model motion is done) situation window, cells,
 * cursor; step 3 hands over to the idle screen (or the dispatcher when a link change was latched). Re-entered at
 * step 2 by the dispatcher to rebuild the cells. */
static void __PlayACSetupSystemFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    VsgSys *cs = mvsg_sys(ioObjp);

    MPadUser = 0;
    switch (cs->step) {
    case 0:
    case 1:
        cs->step = cs->step + 1;
        break;
    case 2: {
        int cn, sid, pm;
        if (DF_MVIEW_MotionSync(MViewp) == 0) break;
        DF_MVS_SetSituationWindow(ioObjp, &cs->whsp);
        cs->stid = play_info.self_id + (play_info.play_mode == 1 ? play_info.self_id > 0 : 0);
        cs->f_enable2p = play_info.local_player_num == 2;
        cn = play_info.connected_num;
        sid = play_info.self_id;
        if (sid == cn - 1 && cn >= 3) {
            pm = play_info.play_mode;
            if (play_info.player_num < (pm + 1) * (sid + 1) - pm) cs->stid = 4;
        }
        __MVSG_ItemCreate(ioObjp);
        __MVSG_ItemFadeIn(ioObjp);
        __MVSG_ItemSetCursor(ioObjp);
        cs->step++;
        break;
    }
    case 3:
        if (cs->f_changed == 1) {
            cs->step = 0;
            cs->func = 0x61;
            ioObjp->play_func = MVSG_FN(__PlayACSetupDispatch);
            ioObjp->stat = 3;
            cs->f_changed = 0;
        } else {
            ioObjp->play_func = MVSG_FN(__PlayACSetupSystem);
            ioObjp->stat = 0;
        }
        break;
    default:
        break;
    }
}

/* the random map may not be a "noc" map unless the match is big enough (or has COMs) */
static inline int mvsg_noc_reroll(void)
{
    int vf = play_info.vs_form, pn = play_info.player_num;
    return (vf == 1 && pn < 3) || (vf == 2 && pn < 3) || (vf == 0 && pn < 2) || play_info.com_num > 0;
}

/* constructor (called by the situation / team / wait screens): screen object, random map + battle seed on the host,
 * key guide, and the four cell records carved out of MenuWorkp (0xc8 bytes each: record + strBuff / name / win /
 * lose buffers) with the participants' names and win / lose counts */
struct stMobj *DF_MVSG_NewSystem(struct stMobj *inObjp, struct struct_c774638e *inPlayinfo,
                                 struct struct_9fc9058b *inWhp)
{
    PS2_FRAME(0x80);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    VsgSys *dt;
    unsigned char *w;
    int k, i, noc;

    if (!obj) return 0;
    dt = mvsg_sys(obj);
    obj->type = 0;
    obj->stat = 1;
    dt->mstr_top_idx = 0x18f75;
    dt->idx = 0;
    dt->play_info = inPlayinfo;
    dt->f_start = 0;
    dt->whsp = inWhp;
    dt->sstp[0] = 0;
    dt->sstp[1] = 0;
    dt->sstp[2] = 0;
    dt->sstp[3] = 0;
    dt->arrDir[1] = -1;
    dt->arrDir[0] = -1;
    memset(play_info.stay, 0, 4);
    memset(play_info.lock, 0, 4);
    if (play_info.self_id == 0) {
        if (play_info.map_id == 0x17) {
            ps2_srand(MenuMng.frame);
            do {
                k = ps2_rand() % 23;
                noc = 0;
                for (i = 0; i < 7; i++) {
                    if (MVSG_MAP_ID[k] == mvs_noc_id[i]) {
                        noc = 1;
                        break;
                    }
                }
            } while (mvsg_noc_reroll() && noc);
            play_info.rand_map_id = (char)k; /* the table index, not the map id */
        }
        play_info.rand_seed = (unsigned char)ps2_rand();
    }
    mvsg_tag(0x24);
    mvsg_guide(0, 1, 2, 0, 0x63);
    DF_MBGUIDE_Show();

    dt->defTBP = MenuWorkp;
    w = (unsigned char *)(((uintptr_t)MenuWorkp + 0xf) & ~(uintptr_t)0xf);
    for (k = 0; k < 4; k++) {
        VsgCell *rec;
        dt->pinfo_wp[k] = (VsgCell *)w;
        memset(dt->pinfo_wp[k], 0, 0x3c);
        rec = dt->pinfo_wp[k];
        rec->strBuff = (char *)rec + 0x3c;
        rec->name_strb = rec->strBuff + 0x20;
        rec->win_strb = rec->name_strb + 0x40;
        rec->lose_strb = rec->win_strb + 4;
        w = (unsigned char *)rec->lose_strb + 4;
        memset(rec->strBuff, 0, 0x20);
        memset(rec->name_strb, 0, 0x40);
        memset(rec->win_strb, 0, 4);
        memset(rec->lose_strb, 0, 4);
        if (k < play_info.player_num) {
            memcpy(rec->name_strb, vs_ac_info[k].name, 0x30);
            ps2_sprintf_d(rec->win_strb, mvsg_fmt_d, vs_ac_info[k].winnum);
            ps2_sprintf_d(rec->lose_strb, mvsg_fmt_d, vs_ac_info[k].losenum);
        } else {
            int c = play_info.com_id[k - play_info.player_num];
            if (c < 100) {
                /* the name string is fetched twice */
                if (c != 0)
                    ps2_sprintf_s(rec->name_strb, mvsg_fmt_s,
                                  (char *)DF_MSTR_GetName(MenuMng.language, 0, c + 0x7a183));
                if (c != 0)
                    ps2_sprintf_s(vs_ac_info[k].name_com, mvsg_fmt_s,
                                  (char *)DF_MSTR_GetName(MenuMng.language, 0, c + 0x7a183));
            }
        }
    }
    MenuWorkp = w;
    obj->play_func = MVSG_FN(__PlayACSetupSystemFadeIn);
    obj->draw_func = MVSG_FN(__DrawAcSetupSystem);
    obj->count = 0;
    return obj;
}
