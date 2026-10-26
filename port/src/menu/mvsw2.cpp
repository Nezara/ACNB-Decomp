/* src/menu/mvsw2.cpp (OLMENU) - MVSW2, the Versus "AC setup wait" screen. A console that has finished its own AC
 * setup (mvsg / mvst), or has none to do (the last of >= 3 consoles without an AC slot, straight from mvsw), waits
 * here for the others: the situation window is shown again with the wait string / blink circle, and the draw_func
 * (__DrawVsWaitSystem2) runs the assembly exchange between all consoles: an intro {4, 1, self, map, seed}, then a
 * burst of four blocks (assembly / option / emblem / dummy, each after a doubled {4, 5, slot, 1} header) that every
 * peer walks through a per-peer state dt.sstp[] 0..5, plus the COM-slot records of mvsg. When everyone
 * is ready __PlayVsWaitSystem2FadeOut transfers the models, swaps an 8-byte ready marker with the peers, drains the
 * link, transfers the textures and ends the menu (the Versus sortie starts); cancel / link errors go through
 * __PlayVsWaitSystem2Dispatch (dialogs + link teardown). One stMobj, dt = struct_ca622da0.
 * DF_MVS_SetMissionPartsImmediate (battle start) copies the human players' assemblies into the mission data. */
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include <string.h>

#ifdef ACNB_SOURCEPORT
char mvsw2_parts20a_0[11] = "parts20a_0"; /* 0x01ee4960 */
#endif

typedef struct struct_ca622da0 Vsw2Sys; /* p_csysp: dt of the screen object */

static inline Vsw2Sys *mvsw2_sys(struct stMobj *pMobj) { return (Vsw2Sys *)&pMobj->dt; }

#define MVSW2_FN(f) ((void (*)(void))GFN(f))

/* Copies assembly inDtp into g_nextstage.asmbly[inIdx]; always returns 1. */
int DF_MVS_SetMissionPartsImmediate(struct AcAssemblyPartsSt *inDtp, int inIdx)
{
    PS2_FRAME(0x10);
    memcpy(&g_nextstage.asmbly[inIdx], inDtp, 0x120);
    return 1;
}

/* DF_MBGUIDE_Change / DF_MBACK_ChangeTag / DF_MBACK_SetFadeOutNX are void(void) in DWARF but read a0..t0 / a0 */
static inline void mvsw2_guide(int a, int b, int c, int d, int e)
{
    gcall_as<int(int, int, int, int, int)>(GFN(DF_MBGUIDE_Change), a, b, c, d, e);
}
static inline void mvsw2_fadeout_nx(int a) { gcall_as<void(int)>(GFN(DF_MBACK_SetFadeOutNX), a); }

/* link shutdown: five fslnk calls, each followed by one vblank wait */
static inline void mvsw2_link_teardown(void)
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

/* restore the work area and end the object */
static inline void mvsw2_finish(struct stMobj *obj, Vsw2Sys *ws)
{
    MenuWorkp = ws->defTBP;
    obj->play_func = 0;
    obj->draw_func = 0;
    obj->stat = 5;
}

/* error exit into Dispatch's link-lost dialog */
static inline void mvsw2_to_link_lost(struct stMobj *obj, Vsw2Sys *ws)
{
    ws->step = 0;
    ws->func = 0x63;
    obj->play_func = MVSW2_FN(__PlayVsWaitSystem2Dispatch);
    obj->draw_func = 0;
    obj->stat = 3;
}

/* "leave via FadeOut2": the situation window fades when f_error (stale unless just set) or f_ready is set */
static inline void mvsw2_leave(struct stMobj *obj, Vsw2Sys *ws)
{
    if (ws->f_error != 0 || play_info.f_ready != 0) DF_MVS_SetSituationWindowFadeOut(ws->whsp);
    if (ws->pWaitString) DF_MOBJ_SetFadeOut(ws->pWaitString);
    if (ws->pWaitCircle) DF_MOBJ2_SetBlinkSpriteFadeOut(ws->pWaitCircle);
    ws->step = 0;
    obj->play_func = MVSW2_FN(__PlayVsWaitSystem2FadeOut);
    obj->stat = 2;
}

/* dialogs selected by dt.func: 0 = cancel / error message (string id dt.error_no, default 0x30ed1), 0x63 link lost,
 * 0x64 partner left, 0x62 (set nowhere: dead) */
static void __PlayVsWaitSystem2Dispatch(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    Vsw2Sys *ws = mvsw2_sys(ioObjp);

    switch (ws->func) {
    case 0:
        if (ws->step == 0) {
            /* a 4-argument call in the original: t0 still holds 0x63 from the func compare */
            mvsw2_guide(0, 0, 0xe, 0, 0x63);
            if (ws->error_no == 0) ws->error_no = 0x30ed1;
            DF_MDIALOG_Open(5, DF_MSTR_GetInfo(MenuMng.language, 1, ws->error_no), 0, 0, 0, 0, 1, 0);
            ws->count = 0; /* in the LinkSyncStop delay slot */
            mvsw2_link_teardown();
            ws->step++;
        } else {
            int r = DF_MDIALOG_Sync();
            if (!(r & 4)) return;
            if (r != 0x24 && r != 0x14 && r != 4) return;
            play_info.f_ready = -1;
            ws->func = 0;
            ws->f_error = 1;
            ioObjp->draw_func = 0;
            DF_M1HELP_FadeOut();
            mvsw2_leave(ioObjp, ws);
        }
        break;
    case 0x62:
        if (ws->step == 0) {
            if (DF_MDIALOG_Type() == 0) {
                mvsw2_guide(0, 0xe, 0, 0, 0x63);
                DF_MDIALOG_Open(5, DF_MSTR_GetInfo(MenuMng.language, 1, 0x30ed4), 0, 0, 0, 0, 0, 0);
                ws->step = 1; /* no teardown here */
            } else if (DF_MDIALOG_Sync() == 2) {
                DF_MDIALOG_Close();
            }
        } else if (ws->step == 1) {
            if (!(DF_MDIALOG_Sync() & 4)) return;
            ws->func = 0;
            DF_M1HELP_FadeOut();
            mvsw2_leave(ioObjp, ws);
        }
        break;
    case 0x63:   /* link lost */
    case 0x64: { /* partner left */
        if (ws->step == 0) {
            if (DF_MDIALOG_Type() == 0) {
                mvsw2_guide(0, 0xe, 0, 0, 0x63);
                DF_MDIALOG_Open(5, DF_MSTR_GetInfo(MenuMng.language, 1, ws->func == 0x63 ? 0x30ed0 : 0x30ed3), 0, 0,
                                0, 0, 0, 0);
                mvsw2_link_teardown();
                ws->step = 1;
            } else if (DF_MDIALOG_Sync() == 2) {
                DF_MDIALOG_Close();
            }
        } else if (ws->step == 1) {
            if (!(DF_MDIALOG_Sync() & 4)) return;
            play_info.f_ready = -1;
            ws->func = 0;
            ws->f_error = 1;
            ioObjp->draw_func = 0;
            DF_M1HELP_FadeOut();
            mvsw2_leave(ioObjp, ws);
        }
        break;
    }
    default:
        break;
    }
}

/* Draw2's stack from sp+0xa0 (comments = sp offsets). The two 0x10a0-byte dummy sends read 0xa0 bytes past their
 * 0x1000 blocks: dummy2's runs over the eight header slots and 0x60 bytes of the caller's frame (`caller`, never
 * written; in the hybrid these are the real PS2 bytes above Draw2's frame, the source port sends zeros), dummy1's
 * into dummy2. The st 4 receive writes 0x10a0 bytes into dummy0, over rcid and tmpd (dead there). */
struct Vsw2DrawFrame {
    unsigned char dummy0[0x1000]; /* 0x00a0 dummyAIData[0]: st 4 receive target */
    int rcid[4];                  /* 0x10a0 COM slots taken by the kind-9 handler */
    int tmpd[0x60];               /* 0x10b0 0x180-byte record: idx, winnum, losenum, name at +0x40 */
    unsigned char dummy1[0x1000]; /* 0x1230 dummyAIData[1]: second-AC dummy send (play_mode 1) */
    unsigned char dummy2[0x1000]; /* 0x2230 dummyAIData[2]: dummy send */
    char send_grab[8];            /* 0x3230 kind-9 {4,2} / {4,5,1,slot,com_id} */
    char send7[8];                /* 0x3238 {3,8} reply to kind 7 */
    char send6[8];                /* 0x3240 {3,8} reply to kind 6 */
    signed char wkd[8];           /* 0x3248 received header */
    char hdr[8];                  /* 0x3250 stage headers */
    char intro[8];                /* 0x3258 {4,1,self,rand_map_id,rand_seed} */
    char idle[8];                 /* 0x3260 idle-timeout cancel {4,0x63} */
    char cancel[8];               /* 0x3268 circle cancel {4,0x63,-1} */
    unsigned char caller[0x60];   /* 0x3270 the caller's frame (read by the dummy2 send only) */
};
static_assert(sizeof(Vsw2DrawFrame) == 0x3230, "Draw2 frame layout");

/* cancel / error into Dispatch2's message dialog (func 0); draw_func stays installed. The callers set error_no. */
static inline void mvsw2_to_dispatch(struct stMobj *obj, Vsw2Sys *ws)
{
    play_info.f_ready = -1;
    ws->count = 0;
    ws->step = 0;
    ws->func = 0;
    obj->play_func = MVSW2_FN(__PlayVsWaitSystem2Dispatch);
    obj->stat = 3;
}

/* a peer's cancel record: m2 == -1 (circle on that console) -> "cancelled" string, else the generic one */
static inline void mvsw2_peer_cancel(struct stMobj *obj, Vsw2Sys *ws, int m2)
{
    mvsw2_to_dispatch(obj, ws);
    ws->error_no = (m2 == -1) ? 0x30ed9 : 0x30ed1;
}

/* the last of >= 3 consoles without an AC slot (same rule as the constructor) */
static inline int mvsw2_no_slot(void)
{
    int cn = play_info.connected_num;
    int self = play_info.self_id;
    return self == cn - 1 && cn >= 3 &&
           play_info.player_num < (play_info.play_mode + 1) * (self + 1) - play_info.play_mode;
}

/* stage header {4, 5, slot, 1}: the second AC (play_mode 1) uses slot + 1; the first AC's b3 is 0 in mode 1 */
static inline void mvsw2_stage_hdr(char *h, int slot, int second)
{
    memset(h, 0, 8);
    h[0] = 4;
    h[1] = 5;
    if (second) {
        h[2] = (char)(slot + 1);
        h[3] = 1;
    } else {
        h[2] = (char)slot;
        if (play_info.play_mode != 1) h[3] = 1;
    }
}

/* record {x, 5, sub, k, id}: sub 0 = a 0x180 record follows (stay), else COM k now belongs to id (assignment, not
 * mvsg's |=); unchecked k */
static inline void mvsw2_recv_com(int ii, const signed char *m)
{
    if (m[2] == 0) {
        play_info.stay[ii] = 1;
    } else {
        char *cid = (char *)play_info.com_id + m[3];
        *cid = m[4];
        ((char *)play_info.ai_mode)[play_info.player_num + m[3]] = (*cid >= 0x64) ? 1 : 0;
    }
    play_info.lock[ii] = 0;
}

/* per-peer states 2..4 (option 0x40 / emblem 0x1420 / dummy 0x10a0): returns 1 when Draw2 must return (cancel) */
static int mvsw2_recv_block(struct stMobj *obj, Vsw2Sys *ws, Vsw2DrawFrame &fr, int ii, int stage)
{
    char *st = &ws->sstp[ii];
    char *stay = &play_info.stay[ii];
    const signed char *m = fr.wkd;
    if (*stay == 1) {
        unsigned int thr = stage == 2 ? 0x48u : stage == 3 ? 0x1428u : 0x10a8u;
        if ((unsigned int)fslnk_LinkRecvDataSize(ii) < thr) return 0;
        ws->wait = 0;
        fslnk_LinkRecvData(fr.wkd, ii, 8);
        if (m[0] != 4) return 0;
        switch (m[1]) {
        case 0x63:
            mvsw2_peer_cancel(obj, ws, m[2]);
            return 1;
        case 2:
            *st = 0;
            break;
        case 5:
            if (stage == 2) {
                fslnk_LinkRecvData(&vs_ac_info[m[2]].option, ii, 0x40);
            } else if (stage == 3) {
                fslnk_LinkRecvData(vs_ac_info[m[2]].embp, ii, 0x1420);
            } else {
                __MVS_GetAIIndex(m[2]); /* result unused */
                ZeroMemory(fr.dummy0, 0x1000);
                fslnk_LinkRecvData(fr.dummy0, ii, 0x10a0);
            }
            if (m[3] != 0) *st = (char)(stage + 1);
            *stay = 0;
            break;
        default:
            break;
        }
        return 0;
    }
    if ((unsigned int)fslnk_LinkRecvDataSize(ii) < 8) return 0;
    ws->wait = 0;
    fslnk_LinkRecvData(fr.wkd, ii, 8);
    if (m[0] == 4) {
        switch (m[1]) {
        case 0x63:
            mvsw2_peer_cancel(obj, ws, m[2]);
            return 1;
        case 2:
            *st = 0;
            break;
        case 5:
            if (m[2] < 0x63) {
                *stay = 1;
            } else {
                if (m[3] != 0) *st = (char)(stage + 1);
                *stay = 0;
            }
            break;
        default:
            break;
        }
    } else if (m[1] == 0x63) { /* any type cancels here (not in state 1) */
        mvsw2_peer_cancel(obj, ws, m[2]);
        return 1;
    }
    return 0;
}

/* draw_func, every frame while installed (also during FadeOut2 / Dispatch2: the stat gate below lets only the circle
 * and link checks run there). Circle cancels; link loss / a dropped console go to Dispatch2's link dialogs; then
 * (stat 0 / 1) the idle timeout, the intro, the stage burst once every peer has introduced itself, the per-peer
 * receive states, and when every peer has delivered all four blocks the AI counts, f_ready = 1 and FadeOut2. */
static void __DrawVsWaitSystem2(struct stMobj *ioObjp)
{
    PS2_FRAME(0x3270);
    PS2_STACK(Vsw2DrawFrame, fr, 0xa0);
    Vsw2Sys *ws = mvsw2_sys(ioObjp);

    if (MPad[MPadUser].Rrt == 1) {
        mvsw2_to_dispatch(ioObjp, ws);
        ws->error_no = 0x30ed9;
        memset(fr.cancel, 0, 8);
        fr.cancel[0] = 4;
        fr.cancel[1] = 0x63;
        fr.cancel[2] = -1;
        fslnk_LinkSendData(fr.cancel, 8);
        fslnk_LinkProc();
        return;
    }
    if (fslnk_LinkProc() < 0) {
        fslnk_GetConnectedNum();
        mvsw2_to_link_lost(ioObjp, ws);
        return;
    }
    int cn = play_info.connected_num;
    if (cn != fslnk_GetConnectedNum() + 1) {
        fslnk_GetConnectedNum();
        ws->step = 0;
        ws->func = 0x64;
        ioObjp->play_func = MVSW2_FN(__PlayVsWaitSystem2Dispatch);
        ioObjp->draw_func = 0;
        ioObjp->stat = 3;
        return;
    }
    if (ioObjp->stat != 0 && ioObjp->stat != 1 && ws->proc == 0) return; /* proc: always 0 */

    int c = ws->wait;
    ws->wait = c + 1;
    if (c >= 0x3841) {
        mvsw2_to_dispatch(ioObjp, ws);
        ws->error_no = 0x30ed1;
        memset(fr.idle, 0, 8);
        fr.idle[0] = 4;
        fr.idle[1] = 0x63;
        fslnk_LinkSendData(fr.idle, 8);
        return;
    }

    int mn = 0x3e7;
    int npeer = play_info.connected_num - 1;
    for (int k = 0; k < npeer; k++)
        if (ws->sstp[k] < mn) mn = ws->sstp[k];
    if (mn == 0x3e7) { /* no peers: error_no stays (Dispatch2 substitutes 0x30ed1) */
        mvsw2_to_dispatch(ioObjp, ws);
        return;
    }

    if (mn == 0 || ws->first == 0) {
        if (MenuMng.frame % 3u == 0) {
            memset(fr.intro, 0, 8);
            fr.intro[0] = 4;
            fr.intro[1] = 1;
            fr.intro[2] = play_info.self_id;
            fr.intro[3] = play_info.rand_map_id;
            fr.intro[4] = (char)play_info.rand_seed;
            fslnk_LinkSendData(fr.intro, 8);
            ws->first = 1;
        }
    } else if (mn == 1 && ws->data_send == 0) {
        /* the burst: every block after its header twice (slot 0x63 = no AC: the header once, no data); send return
         * values ignored */
        int slot;
        if (mvsw2_no_slot())
            slot = 0x63;
        else
            slot = play_info.self_id + (play_info.play_mode == 1 ? (0 < play_info.self_id) : 0);
        for (int pl = 0; pl < 2; pl++) { /* assembly 0x120 (+ 16 discarded parts-id reads) */
            if (pl == 1 && play_info.play_mode != 1) break;
            mvsw2_stage_hdr(fr.hdr, slot, pl);
            fslnk_LinkSendData(fr.hdr, 8);
            if (slot == 0x63) continue;
            fslnk_LinkSendData(fr.hdr, 8);
            fslnk_LinkSendData(PlayerInfo_GetAssemblyInfo(pl, PlayerInfo_GetCurrentAcNo(pl)), 0x120);
            for (int i = 0; i < 16; i++) PlayerInfo_GetAssemblyPartsId(pl, PlayerInfo_GetCurrentAcNo(pl), i);
        }
        for (int pl = 0; pl < 2; pl++) { /* option block 0x40 */
            if (pl == 1 && play_info.play_mode != 1) break;
            mvsw2_stage_hdr(fr.hdr, slot, pl);
            fslnk_LinkSendData(fr.hdr, 8);
            if (slot == 0x63) continue;
            fslnk_LinkSendData(fr.hdr, 8);
            fslnk_LinkSendData(&vs_ac_info[slot + pl].option, 0x40);
        }
        for (int pl = 0; pl < 2; pl++) { /* emblem 0x1420 */
            if (pl == 1 && play_info.play_mode != 1) break;
            mvsw2_stage_hdr(fr.hdr, slot, pl);
            fslnk_LinkSendData(fr.hdr, 8);
            if (slot == 0x63) continue;
            fslnk_LinkSendData(fr.hdr, 8);
            fslnk_LinkSendData(vs_ac_info[slot + pl].embp, 0x1420);
        }
        for (int pl = 0; pl < 2; pl++) { /* dummy 0x10a0 from a 0x1000 zeroed block (reads 0xa0 past it) */
            if (pl == 1 && play_info.play_mode != 1) break;
            mvsw2_stage_hdr(fr.hdr, slot, pl);
            fslnk_LinkSendData(fr.hdr, 8);
            if (slot == 0x63) continue;
            fslnk_LinkSendData(fr.hdr, 8);
            unsigned char *blk = pl == 0 ? fr.dummy2 : fr.dummy1;
            ZeroMemory(blk, 0x1000);
            fslnk_LinkSendData(blk, 0x10a0);
        }
        ws->data_send = 1;
    } else if (mn >= 5) {
        /* everyone delivered: count the AI-controlled players / COMs, ready, leave via FadeOut2 */
        const char *ai = (const char *)play_info.ai_mode;
        play_info.player_ai_num = 0;
        for (int i = 0; i < play_info.player_num; i++)
            if (ai[i] != 0) play_info.player_ai_num++;
        play_info.com_ai_num = 0;
        for (int i = 0; i < play_info.com_num; i++)
            if (play_info.com_id[i] >= 0x64) play_info.com_ai_num++;
        play_info.f_ready = 1;
        DF_M1HELP_FadeOut();
        mvsw2_leave(ioObjp, ws);
        return;
    }

    for (int ii = 0; ii < play_info.connected_num - 1; ii++) {
        char *st = &ws->sstp[ii];
        char *stay = &play_info.stay[ii];
        const signed char *m = fr.wkd;
        switch (*st) {
        case 0: /* waiting for the peer's intro (and COM-slot records) */
            if (*stay == 1) { /* a 0x180 record (idx, winnum, losenum, name) announced by {x, 5, 0} */
                if (fslnk_LinkRecvDataSize(ii) < 0x180) break;
                ws->wait = 0;
                fslnk_LinkRecvData(fr.tmpd, ii, 0x180);
                struct struct_aa25ad39 *ac = &vs_ac_info[fr.tmpd[0]]; /* unchecked */
                ac->winnum = (unsigned short)fr.tmpd[1];
                ac->losenum = (unsigned short)fr.tmpd[2];
                memcpy(ac->name, (char *)fr.tmpd + 0x40, 0x30);
                *stay = 0;
                break;
            }
            if ((unsigned int)fslnk_LinkRecvDataSize(ii) < 8) break;
            ws->wait = 0;
            fslnk_LinkRecvData(fr.wkd, ii, 8);
            if (m[0] == 3) {
                switch (m[1]) {
                case 9: { /* a peer released COM m2: take back the COM slots this console may control */
                    if (mvsw2_no_slot()) break;
                    int n = 0, any = 0;
                    int *rp = fr.rcid;
                    for (int j = 0; j < play_info.com_num; j++) {
                        if (play_info.com_id[j] == m[2] + 0x64 && __MVS_GetControlEnable(j, 0) == 1) {
                            any = 1;
                            play_info.com_id[j] = 5;
                            n++;
                            ((char *)play_info.ai_mode)[play_info.player_num + j] &= ~1;
                            *rp++ = j;
                        }
                    }
                    if (!any) break;
                    memset(fr.send_grab, 0, 8);
                    fr.send_grab[0] = 4;
                    fr.send_grab[1] = 2;
                    fslnk_LinkSendData(fr.send_grab, 8);
                    memset(fr.send_grab, 0, 8);
                    fr.send_grab[0] = 4;
                    fr.send_grab[1] = 5;
                    fr.send_grab[2] = 1;
                    for (int k = 0; k < n; k++) {
                        fr.send_grab[3] = (char)fr.rcid[k];
                        fr.send_grab[4] = play_info.com_id[fr.rcid[k]];
                        fslnk_LinkSendData(fr.send_grab, 8);
                    }
                    ws->func = 0;
                    DF_M1HELP_FadeOut();
                    mvsw2_leave(ioObjp, ws);
                    return;
                }
                case 0x63:
                    mvsw2_peer_cancel(ioObjp, ws, m[2]);
                    return;
                case 2: /* fade out without f_ready, and the loop goes on */
                    ws->func = 0;
                    DF_M1HELP_FadeOut();
                    mvsw2_leave(ioObjp, ws);
                    break;
                case 5:
                    mvsw2_recv_com(ii, m);
                    break;
                case 7:
                    play_info.lock[ii] = 2;
                    memset(fr.send7, 0, 8);
                    fr.send7[0] = 3;
                    fr.send7[1] = 8;
                    fslnk_LinkSendData(fr.send7, 8);
                    break;
                case 6:
                    play_info.lock[ii] = 1;
                    memset(fr.send6, 0, 8);
                    fr.send6[0] = 3;
                    fr.send6[1] = 8;
                    fslnk_LinkSendData(fr.send6, 8);
                    break;
                default:
                    break;
                }
            } else if (m[0] == 4) {
                switch (m[1]) {
                case 0x63:
                    mvsw2_peer_cancel(ioObjp, ws, m[2]);
                    return;
                case 2:
                    *st = 0;
                    break;
                case 1: /* intro: the join takes the host's random map / seed */
                    if (play_info.self_id != 0 && ii == 0) {
                        play_info.rand_map_id = m[3];
                        play_info.rand_seed = (unsigned char)m[4];
                    }
                    *st = 1;
                    break;
                case 5:
                    mvsw2_recv_com(ii, m);
                    break;
                default:
                    break;
                }
            }
            break;
        case 1: /* waiting for the assembly 0x120 */
            if (*stay == 1) {
                if ((unsigned int)fslnk_LinkRecvDataSize(ii) < 0x128) break;
                ws->wait = 0;
                fslnk_LinkRecvData(fr.wkd, ii, 8);
                /* no type check, kind 2 ignored */
                if (m[1] == 0x63) {
                    mvsw2_peer_cancel(ioObjp, ws, m[2]);
                    return;
                }
                if (m[1] == 5) {
                    fslnk_LinkRecvData(&vs_ac_info[m[2]], ii, 0x120);
                    if (m[3] != 0) *st = 2;
                    *stay = 0;
                }
                break;
            }
            if ((unsigned int)fslnk_LinkRecvDataSize(ii) < 8) break;
            ws->wait = 0;
            fslnk_LinkRecvData(fr.wkd, ii, 8);
            if (m[0] != 4) break;
            switch (m[1]) {
            case 2:
                *st = 0;
                break;
            case 0x63:
                mvsw2_peer_cancel(ioObjp, ws, m[2]);
                return;
            case 5:
                if (m[2] < 0x63) {
                    *stay = 1;
                } else {
                    if (m[3] != 0) *st = 2;
                    *stay = 0;
                }
                break;
            default:
                break;
            }
            break;
        case 2:
        case 3:
        case 4:
            if (mvsw2_recv_block(ioObjp, ws, fr, ii, *st)) return;
            break;
        default:
            break;
        }
    }
}

/* entry (step != 1): drop the situation window and the wait string / circle; f_ready != 0 (1 = all ready, -1 =
 * cancel / error) fades out into step 1, else the screen just ends. step 1: model transfer, then the {4, 1} ready
 * marker to every peer and a wait for theirs (1,201-frame timeout), drain, texture transfer, finish. */
static void __PlayVsWaitSystem2FadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x1f0);
    PS2_STACK(char[0x80], dbox, 0x60);
    PS2_STACK(char[0x100], wkd, 0xe0);
    PS2_STACK(char[8], send_data, 0x1e8);
    Vsw2Sys *ws = mvsw2_sys(ioObjp);

    if (ws->step != 1) {
        if (ws->f_error != 0 || play_info.f_ready != 0) DF_MVS_DeleteSituationWindow(ws->whsp);
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
        if (play_info.f_ready != 0) {
            mvsw2_guide(0, 0, 0, 0, 0x63);
            if (ws->faded == 0) {
                mvsw2_fadeout_nx(0);
                Bgm_FadeOutStart();
                ws->faded = 1;
            }
            ws->f_onload = 1;
            ws->timeout = 0;
            ws->step++;
        } else {
            mvsw2_finish(ioObjp, ws);
        }
        return;
    }

    if (!DF_MBACK_Sync()) return;
    if (!((play_info.f_ready == 1 || ws->f_onload == 1) && play_info.f_error != 1)) {
        mvsw2_finish(ioObjp, ws);
        return;
    }
    if (!DF_MVS_TransModelVSAC()) return;
    if (play_info.f_mmdlinkerror == 1) {
        mvsw2_to_link_lost(ioObjp, ws);
        play_info.f_mmdlinkerror = 0;
        return;
    }
    ws->f_onload = 0;
    if (ws->f_error != 0) { /* stale unless Dispatch set it */
        mvsw2_finish(ioObjp, ws);
        return;
    }
    if (ws->sync == 0) {
        memset(send_data, 0, 8);
        send_data[0] = 4;
        send_data[1] = 1;
        fslnk_LinkSendData(send_data, 8);
        ws->sync = 1;
        return;
    }

    int ret = fslnk_LinkProc();
    int n = 0;
    for (int k = 0; k < play_info.connected_num - 1; k++)
        if ((unsigned int)fslnk_LinkRecvDataSize(k) >= 8) n++;
    if (fslnk_LinkSendDataSize() != 0 || n < play_info.connected_num - 1) {
        if (ret >= 0) {
            int c = ws->timeout;
            ws->timeout = c + 1;
            if (c < 0x4b1) return;
        }
        mvsw2_to_link_lost(ioObjp, ws);
        return;
    }
    /* the peer index runs cn-1 .. 1 (peer 0 is never read here: its marker goes into the drain below) */
    for (int ii = 0; ii < play_info.connected_num - 1; ii++)
        fslnk_LinkRecvData(wkd, (play_info.connected_num - 1) - ii, 8);
    for (int ii = 0; ii < play_info.connected_num - 1; ii++) {
        int sz = fslnk_LinkRecvDataSize(ii);
        while (sz != 0) {
            fslnk_LinkRecvData(dbox, ii, sz < 0x81 ? sz : 0x80);
            sz = fslnk_LinkRecvDataSize(ii);
        }
    }
    if (!DF_MVS_TransTextureVSAC()) return;
    DF_MVS_SetVSForm();
    mvsw2_finish(ioObjp, ws);
}

/* idle play_func: the draw_func does the work */
static void __PlayVsWaitSystem2(void) {}

/* step 0 -> 1; step 1 (camera motion done): situation window + wait string / blink circle (created
 * unconditionally); step 2: hand over to the idle play_func */
static void __PlayVsWaitSystem2FadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x180);
    PS2_STACK(int[4], rect, 0x50);
    PS2_STACK(int[4], uv, 0x60);
    PS2_STACK(char[256], name, 0x70);
    PS2_STACK(unsigned int, col, 0x17c);
    Vsw2Sys *ws = mvsw2_sys(ioObjp);

    switch (ws->step) {
    case 0:
        ws->step = 1;
        break;
    case 1: {
        if (!DF_MVIEW_MotionSync(MViewp)) return;
        if (play_info.vs_form == 0) {
            if (play_info.player_num == 4) ws->idx = 1;
            ws->max_sidx = play_info.player_num == 3 ? 2 : 3;
        } else {
            if (play_info.player_num == 4) ws->idx = 1;
            ws->max_sidx = 2;
        }
        DF_MVS_SetSituationWindow(ioObjp, &ws->whsp);
        ps2_sprintf_d(name, mvsw2_parts20a_0, 0);
        char *layout = (char *)LAYOUT_GetLayoutData(0x40b);
        ws->pWaitString = DF_MOBJ_NewStringFromLayoutName(ioObjp, 0, 0x18fc6, 0, layout, name, 0, 0, 0);
        DF_MOBJ_SetDrawEnable(ws->pWaitString, 0);
        uv[0] = 0xd0;
        uv[1] = 0x22;
        uv[2] = 0x100;
        uv[3] = 0x52;
        rect[0] = 0x6e;
        rect[1] = 0xf5;
        rect[2] = 0x1a8;
        rect[3] = 0x19;
        col = 0x3c808080u; /* 0x27c4d8, bit copy through lwc1 / swc1 */
        ws->pWaitCircle = DF_MOBJ2_NewBlinkSpriteNX(ioObjp, 0x3ec, (struct stMenuRect *)rect, uv,
                                                    (struct struct_76b810ec *)&col, 0, 0);
        DF_MOBJ2_SetBlinkSpriteAlphaReg(ws->pWaitCircle, 0x48);
        DF_MOBJ_SetDrawEnable(ws->pWaitCircle, 0);
        DF_MOBJ_SetDrawEnable(ws->pWaitString, 1);
        DF_MOBJ_SetFadeIn(ws->pWaitString);
        DF_MOBJ_SetDrawEnable(ws->pWaitCircle, 1);
        DF_MOBJ2_SetBlinkSpriteFadeIn(ws->pWaitCircle);
        ws->step++;
        break;
    }
    case 2:
        ioObjp->play_func = MVSW2_FN(__PlayVsWaitSystem2);
        ioObjp->stat = 0;
        break;
    default:
        break;
    }
}

/* copy one own AC (assembly, option block from g_gamedata[pl], small emblem) into vs_ac_info[slot] */
static void mvsw2_copy_own_ac(int pl, int slot)
{
    struct struct_aa25ad39 *e = &vs_ac_info[slot];
    CopyMemory(e, PlayerInfo_GetAssemblyInfo(pl, PlayerInfo_GetCurrentAcNo(pl)), 0x120);
    const unsigned char *gd = (const unsigned char *)&g_gamedata[pl];
    e->option.cockpit = g_gamedata[pl].cockpit;
    e->option.cockpitcol = g_gamedata[pl].cockpitcol;
    e->option.plus = (short)g_gamedata[pl].plus;
    e->option.level = (char)gd[0x57];                 /* option + 7 */
    e->option.view_mode = (char)g_gamedata[pl].isUseCockpit; /* before the memcpy (delay slot) */
    memcpy(&e->option.keyconf, gd + 0x60, 0x30);      /* option + 0x10 */
    DF_EMB_MakeSmallEmblem(e->embp, EmblemTM2[pl]);
}

/* constructor (mvsg / mvst after the AC setup on every console, or mvsw for a console without an AC slot): copies
 * this console's current AC(s) into its vs_ac_info slot(s) for the exchange */
struct stMobj *DF_MVSW2_NewSystem(struct stMobj *inObjp, struct struct_9fc9058b *inWhsp)
{
    PS2_FRAME(0x50);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (obj == 0) return 0;
    Vsw2Sys *ws = mvsw2_sys(obj);

    obj->type = 0;
    obj->stat = 1;
    ws->mstr_top_idx = 0x18f75;
    ws->idx = 0;
    ws->whsp = inWhsp;
    ws->data_send = 0;
    ws->stid = 0;
    ws->wait = 0;
    ws->sync = 0;
    ws->proc = 0;
    ws->faded = 0;
    ws->f_onload = 0;
    ws->sstp[3] = 0;
    ws->sstp[2] = 0;
    ws->sstp[1] = 0;
    ws->sstp[0] = 0; /* in the ChangeTag delay slot */
    gcall_as<void(int)>(GFN(DF_MBACK_ChangeTag), 0x24);
    mvsw2_guide(0, 0, 0, 0, 0x63);
    ws->defTBP = MenuWorkp;

    int cn = play_info.connected_num;
    int self = play_info.self_id;
    if (!(self == cn - 1 && cn >= 3 &&
          play_info.player_num < (play_info.play_mode + 1) * (self + 1) - play_info.play_mode)) {
        int slot = self + (play_info.play_mode == 1 ? (0 < self) : 0);
        mvsw2_copy_own_ac(0, slot);
        if (play_info.play_mode == 1 &&
            !(play_info.self_id == 1 && play_info.connected_num == 2 && play_info.player_num == 3))
            mvsw2_copy_own_ac(1, slot + 1);
    }

    obj->play_func = MVSW2_FN(__PlayVsWaitSystem2FadeIn);
    obj->draw_func = MVSW2_FN(__DrawVsWaitSystem2);
    obj->count = 0;
    DF_M1HELP_SetWindow(); /* empty stub; the original passes (0, 0x280) */
    DF_M1HELP_SetString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x18f92));
    DF_M1HELP_FadeIn();
    return obj;
}
