/* src/menu/mvsw.cpp (OLMENU) - MVSW, the join-side Versus "wait for the host" screen. The console that did not
 * become the i.LINK host lands here instead of the situation setup (mvss): it shows the host's situation window
 * read-only (DF_MVS_SetSituationWindow, shared with mvss), polls the link every frame (__DrawVsWaitSystem) and
 * mirrors the host's edits from 8-byte messages (at most one per peer per frame); the only local input is circle =
 * cancel. Forward / back / START from the host leave through __PlayVsWaitSystemFadeOut into the join's own AC setup
 * (mvsg / mvst / mvsw2) or, after the emblem exchange, the battle. __PlayVsWaitSystemFadeIn does the 0x80-byte handshake with the
 * host, __PlayVsWaitDispatch tracks the child screen and shows the link error dialogs. One stMobj owns everything;
 * its dt is a struct_5ed2e359. The sprintf calls are newlib's (ps2_sprintf_*: buffer, format and %s argument in PS2
 * memory, nodwarf strings); the buffers handed to fslnk_* sit at their stack slots. */
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include <string.h>

#ifdef ACNB_SOURCEPORT
char mvsw_parts20a_0[11] = "parts20a_0"; /* 0x01ee4900 */
char mvsw_possible[9] = "POSSIBLE";      /* 0x01ee4920 */
char mvsw_impossible[11] = "IMPOSSIBLE"; /* 0x01ee4930 */
char mvsw_fmt_com[7] = "COMx%d";         /* 0x0027c4d0 */
char mvsw_no_limit[9] = "NO LIMIT";      /* 0x01ee4910 */
char mvsw_fmt_time[8] = "%02d:00";       /* 0x0027c4a8 */
char mvsw_all_ac[7] = "ALL AC";          /* 0x0027c4b0 */
char mvsw_leader[7] = "LEADER";          /* 0x0027c4b8 */
char mvsw_team[5] = "TEAM";              /* 0x0027c4c0 */
char mvsw_fmt_s[3] = "%s";               /* 0x0027c4c8 */
#endif

typedef struct struct_5ed2e359 VswSys; /* p_csysp: dt of the screen object */

static inline VswSys *mvsw_sys(struct stMobj *pMobj) { return (VswSys *)&pMobj->dt; }

#define MVSW_FN(f) ((void (*)(void))GFN(f))

/* DF_MBGUIDE_Change / DF_MBACK_ChangeTag are void(void) in DWARF but read a0..t0 / a0 */
static inline void mvsw_guide(int a, int b, int c, int d, int e)
{
    gcall_as<int(int, int, int, int, int)>(GFN(DF_MBGUIDE_Change), a, b, c, d, e);
}

/* link shutdown: five fslnk calls, each followed by one vblank wait */
static inline void mvsw_link_teardown(void)
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

/* "leave via FadeOut": the situation window fades only when func == 0 (re-read after the caller's store) */
static inline void mvsw_leave(struct stMobj *obj, VswSys *ws)
{
    if (ws->func == 0) DF_MVS_SetSituationWindowFadeOut(ws->whsp);
    if (ws->pWaitString) DF_MOBJ_SetFadeOut(ws->pWaitString);
    if (ws->pWaitCircle) DF_MOBJ2_SetBlinkSpriteFadeOut(ws->pWaitCircle);
    ws->step = 0;
    obj->play_func = MVSW_FN(__PlayVsWaitSystemFadeOut);
    obj->stat = 2;
}

/* link lost: show the dialog through Dispatch (draw_func cleared: this draw_func stops) */
static inline void mvsw_link_lost(struct stMobj *obj, VswSys *ws, char func)
{
    mvsw_link_teardown();
    ws->step = 0;
    ws->func = func;
    obj->play_func = MVSW_FN(__PlayVsWaitDispatch);
    obj->draw_func = 0;
    obj->stat = 3;
}

/* draw_func: link poll every frame, then (only while the object runs) one 8-byte host message per peer */
static void __DrawVsWaitSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0xa0);
    PS2_STACK(char[8], wkd, 0x98);
    VswSys *ws = mvsw_sys(ioObjp);

    int ret = fslnk_LinkProc();
    if (ret < 0) {
        fslnk_GetConnectedNum(); /* result unused */
        mvsw_link_lost(ioObjp, ws, ret == -0x10 ? 0x64 : 0x63);
        return;
    }
    if (ws->first_sync == 1) {
        int cn = play_info.connected_num; /* read in the jal delay slot, before the call */
        if (cn != fslnk_GetConnectedNum() + 1) {
            fslnk_GetConnectedNum(); /* result unused */
            mvsw_link_lost(ioObjp, ws, 0x64);
            return;
        }
    }
    if (ioObjp->stat != 0) return;

    for (int ii = 0; ii < play_info.connected_num - 1; ii++) {
        if ((unsigned int)fslnk_LinkRecvDataSize(ii) < 8) continue;
        fslnk_LinkRecvData(wkd, ii, 8);
        if (wkd[0] != 1) continue; /* other headers are consumed and dropped */
        struct struct_9fc9058b *whsp = ws->whsp;
        switch (wkd[1]) {
        case 1: /* forward */
            ws->func = 1;
            mvsw_leave(ioObjp, ws);
            break;
        case 2: /* back */
            ws->func = 0;
            mvsw_leave(ioObjp, ws);
            break;
        case 3: /* START */
            play_info.f_ready = 1;
            ws->f_start = 1;
            ws->func = 0;
            play_info.rand_map_id = wkd[2];
            play_info.rand_seed = (unsigned char)wkd[3];
            mvsw_leave(ioObjp, ws);
            break;
        case 4:
            break;
        case 0xa: /* map */
            play_info.map_id = wkd[2];
            ((struct struct_37ce77ad *)&whsp->mapp[0]->dt)->texid = play_info.map_id + 0x1388;
            break;
        case 0xb: /* time limit */
            play_info.time_limit = wkd[2];
            if (play_info.time_limit == 0x10)
                ps2_sprintf_d(whsp->strBuff[4], mvsw_no_limit, play_info.time_limit);
            else
                ps2_sprintf_d(whsp->strBuff[4], mvsw_fmt_time, play_info.time_limit);
            break;
        case 0xc: /* battle form (a2 = 1 left from the header test is the unused vararg) */
            play_info.vs_form = wkd[2];
            if (play_info.vs_form == 2) ps2_sprintf_d(whsp->strBuff[5], mvsw_team, 1);
            else if (play_info.vs_form == 1) ps2_sprintf_d(whsp->strBuff[5], mvsw_leader, 1);
            else if (play_info.vs_form == 0) ps2_sprintf_d(whsp->strBuff[5], mvsw_all_ac, 1);
            break;
        case 0xd: /* pickup weapon: shares strBuff[6] with the COM count (original overlap) */
            play_info.pickup_weapon = wkd[2];
            ps2_sprintf_s(whsp->strBuff[6], mvsw_fmt_s,
                          play_info.pickup_weapon != 0 ? mvsw_impossible : mvsw_possible);
            break;
        case 0x10: /* destroyed weapon */
            play_info.dest_weapon = wkd[2];
            ps2_sprintf_s(whsp->strBuff[7], mvsw_fmt_s,
                          play_info.dest_weapon != 0 ? mvsw_impossible : mvsw_possible);
            break;
        case 0xe: /* COM count */
            play_info.com_num = wkd[2];
            ps2_sprintf_d(whsp->strBuff[6], mvsw_fmt_com, play_info.com_num);
            break;
        default:
            break;
        }
    }
}

/* DF_MBACK_SetFadeOutNX is void(void) in DWARF but reads a0 */
static inline void mvsw_fadeout_nx(int a) { gcall_as<void(int)>(GFN(DF_MBACK_SetFadeOutNX), a); }

/* this console is the last of >= 3 and has no AC slot of its own */
static inline int mvsw_no_ac_slot(int cn, int self)
{
    return self == cn - 1 && cn >= 3 &&
           play_info.player_num < (play_info.play_mode + 1) * (self + 1) - play_info.play_mode;
}

/* finish: restore the work area and end the object */
static inline void mvsw_finish(struct stMobj *obj, VswSys *ws)
{
    MenuWorkp = ws->defTBP;
    obj->play_func = 0;
    obj->draw_func = 0;
    obj->stat = 5;
}

/* entry (step != 1): erase the wait string / circle, then func 0 = cancel / START (START fades out and goes on to
 * step 1), func 1 = host forward (open the AC setup child, tracked by Dispatch). step 1 (START): model transfer,
 * emblem exchange with every peer (one header + one or two 0x1420 emblems each way), texture transfer, finish.
 * dt.f_error / f_onload / f_faded (dt+0x43..0x45) are initialised by nothing: stale bytes of the recycled slot. */
static void __PlayVsWaitSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50e0);
    PS2_STACK(char[0x1420], dast0, 0x50);
    PS2_STACK(char[0x1420], dast1, 0x1470);
    PS2_STACK(char[0x1420], dast2, 0x2890);
    PS2_STACK(char[0x1420], dast3, 0x3cb0);
    PS2_STACK(char[8], wkd, 0x50d0);
    PS2_STACK(char[8], send_data, 0x50d8);
    VswSys *ws = mvsw_sys(ioObjp);

    if (ws->step != 1) {
        if (ws->pWaitString) {
            ws->pWaitString->f_draw = 0;
            ws->pWaitString->f_erase = 1;
            ws->pWaitString = 0;
        }
        if (ws->pWaitCircle) {
            ws->pWaitCircle->f_draw = 0;
            ws->pWaitCircle->f_erase = 1;
            ws->pWaitCircle = 0;
        }
        if (ws->func == 0) {
            DF_MVS_DeleteSituationWindow(ws->whsp);
            if (play_info.f_ready == 0) {
                gcall_as<void(int)>(GFN(DF_MBACK_ChangeTag), 0x21);
                mvsw_guide(0, 1, 2, 0, 0x63);
            }
            if (ws->f_start != 0 || ws->f_error != 0) {
                if (ws->f_faded == 0) {
                    mvsw_guide(0, 0, 0, 0, 0x63);
                    mvsw_fadeout_nx(0);
                    Bgm_FadeOutStart();
                    ws->f_faded = 1;
                }
                ws->step = 1;
            } else {
                mvsw_finish(ioObjp, ws);
            }
        } else if (ws->func == 1) {
            if (play_info.vs_form != 0) {
                ws->csysp = DF_MVST_NewSystem(ioObjp, ws->play_info, ws->whsp);
            } else {
                play_info.team_condition[3] = 0;
                play_info.team_condition[2] = 0;
                play_info.team_condition[1] = 0;
                play_info.team_condition[0] = 0;
                if (mvsw_no_ac_slot(play_info.connected_num, play_info.self_id))
                    ws->csysp = DF_MVSW2_NewSystem(ioObjp, ws->whsp);
                else
                    ws->csysp = DF_MVSG_NewSystem(ioObjp, ws->play_info, ws->whsp);
            }
            ws->func = 0;
            ioObjp->play_func = MVSW_FN(__PlayVsWaitDispatch);
            ioObjp->draw_func = 0;
            ioObjp->stat = 3;
        }
        return;
    }

    if (!DF_MBACK_Sync()) return;
    if (!((ws->f_error == 0 && play_info.f_ready == 1) || ws->f_onload == 1)) {
        MenuWorkp = ws->defTBP;
        ioObjp->play_func = 0; /* draw_func stays */
        ioObjp->stat = 5;
        return;
    }
    ws->f_onload = 1; /* in the jal delay slot */
    if (!DF_MVS_TransModelVSAC()) return;
    ws->f_onload = 0;

    /* the same counter FadeIn step 1 used: not reset in between */
    int c = ws->count;
    ws->count = c + 1;
    if (c >= 0x259) {
        mvsw_guide(0, 0, 0xe, 0, 0x63);
        DF_MDIALOG_Open(5, DF_MSTR_GetInfo(MenuMng.language, 1, 0x30ed1), 0, 0, 0, 0, 1, 0);
        mvsw_link_teardown();
        ws->func = 1;
        ioObjp->stat = 3;
        ioObjp->play_func = MVSW_FN(__PlayVsWaitDispatch);
        return;
    }

    if (ws->sync == 0) {
        /* send: header {4, 1, slot}, then this console's emblem(s); without a slot the uninitialised stack blocks */
        int cn = play_info.connected_num;
        int self = play_info.self_id;
        int slot;
        if (mvsw_no_ac_slot(cn, self))
            slot = 0x63;
        else
            slot = self + (play_info.play_mode == 1 ? (0 < self) : 0);
        memset(send_data, 0, 8);
        send_data[0] = 4;
        send_data[2] = (char)slot;
        send_data[1] = 1;
        fslnk_LinkSendData(send_data, 8);
        if (slot < 0x63) {
            DF_EMB_MakeSmallEmblem(vs_ac_info[slot].embp, EmblemTM2[0]);
            fslnk_LinkSendData(vs_ac_info[slot].embp, 0x1420);
        } else {
            fslnk_LinkSendData(dast3, 0x1420); /* uninitialised stack block (quirk) */
        }
        if (play_info.play_mode == 1) {
            if (slot < 0x63) {
                DF_EMB_MakeSmallEmblem(vs_ac_info[slot + 1].embp, EmblemTM2[1]);
                fslnk_LinkSendData(vs_ac_info[slot + 1].embp, 0x1420);
            } else {
                fslnk_LinkSendData(dast2, 0x1420);
            }
        }
        ws->sync = 1;
        return;
    }

    /* receive: wait until every peer's header + emblem(s) are buffered (any nonzero play_mode waits for two) */
    if (fslnk_LinkSendDataSize() > 0) return;
    int ready = 0;
    for (int k = 0; k < play_info.connected_num - 1; k++) {
        unsigned int need = play_info.play_mode == 0 ? 0x1428u : 0x2848u;
        if ((unsigned int)fslnk_LinkRecvDataSize(k) >= need) ready++;
    }
    if (ready < play_info.connected_num - 1) return;
    for (int k = 0; k < play_info.connected_num - 1; k++) {
        fslnk_LinkRecvData(wkd, k, 8);
        int i = wkd[2]; /* signed slot byte, header not checked */
        if (i < 0x63)
            fslnk_LinkRecvData(vs_ac_info[i].embp, k, 0x1420);
        else
            fslnk_LinkRecvData(dast1, k, 0x1420);
        if (play_info.play_mode == 1) {
            i = wkd[2];
            if (i + 1 < 0x63)
                fslnk_LinkRecvData(vs_ac_info[i + 1].embp, k, 0x1420);
            else
                fslnk_LinkRecvData(dast0, k, 0x1420);
        }
    }
    if (!DF_MVS_TransTextureVSAC()) return;
    DF_MVS_SetVSForm();
    mvsw_finish(ioObjp, ws);
}

/* the dialog / child-screen states selected by dt.func */
static void __PlayVsWaitDispatch(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    VswSys *ws = mvsw_sys(ioObjp);

    switch (ws->func) {
    case 0: { /* child screen (mvsg / mvst / mvsw2) */
        struct stMobj *c = ws->csysp;
        /* stat is read before the NULL test */
        unsigned char st = c ? c->stat : ((const unsigned char *)PS2_NULL_MEM())[2];
        if (st != 5) return;
        if (c) {
            c->f_draw = 0;
            ws->csysp->f_erase = 1;
            ws->csysp = 0;
        }
        if (play_info.f_ready != 0) {
            ws->func = 0;
            ws->step = 0;
            ioObjp->play_func = MVSW_FN(__PlayVsWaitSystemFadeOut);
            ioObjp->stat = 2;
        } else {
            /* the host backed out of the child: rebuild the situation window */
            ws->step = 2;
            ioObjp->stat = 1;
            ioObjp->count = 0xf;
            ioObjp->play_func = MVSW_FN(__PlayVsWaitSystemFadeIn);
            ioObjp->draw_func = MVSW_FN(__DrawVsWaitSystem);
        }
        break;
    }
    case 1: { /* fatal dialog: exactly 4 */
        int r = DF_MDIALOG_Sync();
        if (!(r & 4)) return;
        if (r != 4) return;
        Voice_Call(0x3eb, 0x7f);
        mvsw_guide(0, 0, 0, 0, 0x63);
        play_info.f_ready = -1;
        ws->func = 0;
        ioObjp->draw_func = 0;
        mvsw_leave(ioObjp, ws);
        break;
    }
    case 0x63:   /* link lost */
    case 0x64: { /* partner left */
        if (ws->step == 0) {
            if (DF_MDIALOG_Type() == 0) {
                mvsw_guide(0, 0xe, 0, 0, 0x63);
                DF_MDIALOG_Open(5, DF_MSTR_GetInfo(MenuMng.language, 1, ws->func == 0x63 ? 0x30ed0 : 0x30ed3), 0, 0,
                                0, 0, 0, 0);
                mvsw_link_teardown();
                ws->step = 1;
            } else if (DF_MDIALOG_Sync() == 2) {
                DF_MDIALOG_Close();
            }
        } else if (ws->step == 1) {
            if (!(DF_MDIALOG_Sync() & 4)) return;
            mvsw_guide(0, 0, 0, 0, 0x63);
            ws->f_error = 1;
            ws->func = 0;
            play_info.f_ready = -1;
            ws->step = 0;
            ioObjp->draw_func = 0;
            mvsw_leave(ioObjp, ws);
        }
        break;
    }
    case 0x65: {
        if (!(DF_MDIALOG_Sync() & 4)) return;
        mvsw_guide(0, 0, 0, 0, 0x63);
        ws->f_error = 1;
        ws->func = 0;
        ws->step = 0;
        play_info.f_error = 1;
        play_info.f_ready = -1;
        ioObjp->draw_func = 0;
        mvsw_leave(ioObjp, ws);
        break;
    }
    default:
        break;
    }
}

/* input loop: circle (or MenuMng.stat 0xc) cancels and tells the host */
static void __PlayVsWaitSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    PS2_STACK(char[8], send_data, 0x48);
    VswSys *ws = mvsw_sys(ioObjp);

    if (MenuMng.stat != 0xc) {
        if (MPad[MPadUser].Rrt != 1) return;
        Voice_Call(0x3eb, 0x7f);
    }
    ws->func = 0; /* the leave sequence's func test is therefore always true here */
    mvsw_leave(ioObjp, ws);
    memset(send_data, 0, 8);
    send_data[0] = 1;
    send_data[1] = 2;
    fslnk_LinkSendData(send_data, 8);
}

/* handshake failure: dialog, teardown, Dispatch func 1 (draw_func stays) */
static void mvsw_handshake_fail(struct stMobj *obj, VswSys *ws, unsigned int msg)
{
    DF_MDIALOG_Open(5, DF_MSTR_GetInfo(MenuMng.language, 1, msg), 0, 0, 0, 0, 1, 0);
    if (msg == 0x30ed0) Ether_SendErrorToAll(-1);
    mvsw_link_teardown();
    ws->func = 1;
    obj->stat = 3;
    obj->play_func = MVSW_FN(__PlayVsWaitDispatch);
}

/* step 0 sends the handshake block, step 1 waits for every peer's block (601-frame timeout) and takes play_mode /
 * player_num from it, step 2 (camera motion done) builds the situation window + wait string / circle, step 3 hands
 * over to the input loop once the window runs */
static void __PlayVsWaitSystemFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x2a0);
    PS2_STACK(int[4], rect, 0x70);
    PS2_STACK(int[4], uv, 0x80);
    PS2_STACK(char[256], name, 0x90);
    PS2_STACK(char[128], recv_data, 0x190);
    PS2_STACK(char[128], send_data, 0x210);
    PS2_STACK(unsigned int, col, 0x29c);
    static const char send_init[128] = "A-AC3SLCHECK";
    static const char recv_init[128] = "010101010101010101";
    VswSys *ws = mvsw_sys(ioObjp);

    /* both blocks are copied in on every call (the join does not put play_mode / player_num in its block) */
    memcpy(send_data, send_init, 128);
    memcpy(recv_data, recv_init, 128);

    switch (ws->step) {
    case 0:
        fslnk_LinkSendData(send_data, 0x80);
        ws->count = 0;
        ws->step++;
        break;
    case 1: {
        int c = ws->count;
        ws->count = c + 1;
        if (c >= 0x259) {
            mvsw_guide(0, 0, 0xe, 0, 0x63);
            mvsw_handshake_fail(ioObjp, ws, 0x30ed1);
            return;
        }
        int n = 0;
        int peers = play_info.connected_num - 1;
        for (int k = 0; k < peers; k++, peers = play_info.connected_num - 1)
            if ((unsigned int)fslnk_LinkRecvDataSize(k) >= 0x80) n++;
        if (n != peers) return;
        if (fslnk_LinkSendDataSize() > 0) return;
        for (int k = play_info.connected_num - 2; k >= 0; k--) {
            fslnk_LinkRecvData(recv_data, k, 0x80);
            if (strcmp(send_data + 2, recv_data + 2) != 0) {
                /* four arguments here (t0 left as it is) */
                gcall_as<int(int, int, int, int)>(GFN(DF_MBGUIDE_Change), 0, 0xe, 0, 0x63);
                mvsw_handshake_fail(ioObjp, ws, 0x30ed0);
                return;
            }
        }
        /* the last block read (peer 0) */
        play_info.play_mode = (char)(unsigned char)recv_data[0];
        play_info.player_num = (char)(unsigned char)recv_data[1];
        ws->first_sync = 1;
        ws->step++;
        break;
    }
    case 2: {
        if (!DF_MVIEW_MotionSync(MViewp)) return;
        if (play_info.vs_form == 0) {
            if (play_info.player_num == 4) ws->idx = 1;
            ws->max_sidx = play_info.player_num == 3 ? 2 : 3;
        } else {
            if (play_info.player_num == 4) ws->idx = 1;
            ws->max_sidx = 2;
        }
        DF_MVS_SetSituationWindow(ioObjp, &ws->whsp);
        if (ws->pWaitString == 0) {
            ps2_sprintf_d(name, mvsw_parts20a_0, 0);
            char *layout = (char *)LAYOUT_GetLayoutData(0x40b);
            ws->pWaitString = DF_MOBJ_NewStringFromLayoutName(ioObjp, 0, 0x18fc6, 0, layout, name, 0, 0, 0);
            DF_MOBJ_SetDrawEnable(ws->pWaitString, 0);
        }
        if (ws->pWaitCircle == 0) {
            uv[0] = 0xd0;
            uv[1] = 0x22;
            uv[2] = 0x100;
            uv[3] = 0x52;
            rect[0] = 0x6e;
            rect[1] = 0xf5;
            rect[2] = 0x1a8;
            rect[3] = 0x19;
            col = 0x3c808080u; /* 0x27c4a0, bit copy through lwc1 / swc1 */
            ws->pWaitCircle = DF_MOBJ2_NewBlinkSpriteNX(ioObjp, 0x3ec, (struct stMenuRect *)rect, uv,
                                                        (struct struct_76b810ec *)&col, 0, 0);
            DF_MOBJ2_SetBlinkSpriteAlphaReg(ws->pWaitCircle, 0x48);
            DF_MOBJ_SetDrawEnable(ws->pWaitCircle, 0);
        }
        if (ws->pWaitString) {
            DF_MOBJ_SetDrawEnable(ws->pWaitString, 1);
            DF_MOBJ_SetFadeIn(ws->pWaitString);
        }
        if (ws->pWaitCircle) {
            DF_MOBJ_SetDrawEnable(ws->pWaitCircle, 1);
            DF_MOBJ2_SetBlinkSpriteFadeIn(ws->pWaitCircle);
        }
        ws->step++;
        break;
    }
    case 3:
        if (ws->whsp->swp->stat == 0) {
            ioObjp->play_func = MVSW_FN(__PlayVsWaitSystem);
            ioObjp->stat = 0;
        }
        break;
    default:
        break;
    }
}

/* constructor (called by mvse on the join console): one stMobj, the situation-window work area carved from
 * MenuWorkp (0x58 bytes + 8 x 16-byte strings) */
struct stMobj *DF_MVSW_NewSystem(struct stMobj *inObjp, struct struct_c774638e *inPlayinfo)
{
    PS2_FRAME(0x50);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (obj == 0) return 0;
    VswSys *ws = mvsw_sys(obj);

    obj->type = 0;
    obj->stat = 1;
    ws->mstr_top_idx = 0x18f75;
    ws->idx = 0;
    ws->play_info = inPlayinfo;
    ws->f_start = 0;
    ws->step = 0;
    ws->sync = 0;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 16; c++) ws->ldct[r][c] = 0;

    play_info.f_ready = 0;
    play_info.f_error = 0;
    play_info.f_back = 0;
    play_info.f_mmdlinkerror = 0;
    play_info.f_setuped = 0;
    play_info.now_page = 0; /* in the ChangeTag delay slot: before the call */
    gcall_as<void(int)>(GFN(DF_MBACK_ChangeTag), 0x22);
    mvsw_guide(0, 0, 2, 0, 0x63);
    DF_MVIEW_MotionStart(MViewp, play_info.connected_num + 0x3b);

    ws->defTBP = MenuWorkp;
    ws->whsp = (struct struct_9fc9058b *)(((uintptr_t)MenuWorkp + 0xf) & ~(uintptr_t)0xf);
    memset(ws->whsp, 0, 0x58);
    ws->whsp->strBuff[0] = (char *)ws->whsp + 0x58;
    memset(ws->whsp->strBuff[0], 0, 0x10);
    for (int i = 1; i < 8; i++) {
        ws->whsp->strBuff[i] = ws->whsp->strBuff[i - 1] + 0x10;
        memset(ws->whsp->strBuff[i], 0, 0x10);
    }
    MenuWorkp = (unsigned char *)ws->whsp->strBuff[7] + 0x10;
    obj->play_func = MVSW_FN(__PlayVsWaitSystemFadeIn);
    obj->draw_func = MVSW_FN(__DrawVsWaitSystem);
    obj->count = 0;
    return obj;
}
