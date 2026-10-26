/* src/link/liblnk.cpp - the Versus "link" front end (two-console / LAN lobby + lock-step sync), groups L1 - L3 (whole file).
 * fslnk_* dispatch on the file static linkMode (initial -1; fslnk_InitLinkMode sets -1 on every Versus start):
 * 0 = i.LINK (fsilk_*, src/link/ilink.c), 3 = network adaptor LAN (Ether_*, src/link/ether.c), 1 / 2 = the in-file
 * PPP modem / serial path ("Mdm": fslib_MdmProc + RX/TX statics over fscom_*, src/link/fslibcom.c), anything else =
 * the generic arms (same state words, no transport). The dispatch is NOT uniform: every function tests its own set of
 * values (`== 3`, `== 0`, `== 1 / 2`, default) and they are kept per function as in the asm.
 * Offline (no peer, linkMode -1, __ILINKMNG zeroed by mvsr): LinkStop clears enable, so fslib_MdmProc returns at its
 * first test; LinkSyncInit's generic arm runs for real (ILINKSYNC set-up, sync ring cleared); LinkSendData fills
 * param_send_buf, nothing drains it.
 * L2 = the Mdm RX / TX helpers, MdmInit / MdmTerm, fslnk_LinkInit and fslnk_LinkValidInterface: none of them runs in
 * this build (LinkInit is only ever called with 0 or 3, driverID is never written, MdmProc quirk 1 below keeps the RX
 * update off). A Mdm record is 12 bytes: 0-1 u16 size, 2 = low byte of MyTranPacket, 6 = sender MySyncStatus,
 * 7 = kind (1 hello, 2 hello-ack, 3 sync, 4 param, 5 ready); a kind-3 record IS the 12-byte sync frame.
 * fscom_* / Ether_* / fsilk_* / init_timer1 are not liblnk (interpreted in the hybrid, stubs in the source port).
 * Original quirks kept:
 *  - fslib_MdmProc socket states 2 / 3 use the same destination selector (linkMode 1: &scid, else &rcid) with the
 *    opposite Init: the second socket overwrites the first id and Init gets the never-written other id, so rcid and
 *    scid never both become > 0 and the RX update is never entered (the modem link looks broken in this build).
 *    Both sockets pick the remote IP by `linkMode == 1` (.101 for mode 1, .102 for mode 2).
 *  - fslib_MdmProc retry arm: `&& tx[7] == 4` is dead (kind != 4 there); sync_tx_frame only moves in the syncFlip
 *    arm and the kind-4 arm. p[2] = low byte of MyTranPacket (the original's signed `% 256`). The eventFlag error
 *    print passes one int (a2 stale in the original). In the syncFlip arm TxData is not cleared.
 *  - LinkCheckConnect(block != 0) spins forever without a wait while not connected.
 *  - LinkSyncInit's generic arm ignores size / late (late = the constant 1), clears only the first 0x20 bytes of
 *    ILINKSYNC (sync_rx_frame_max, send_data_size, cnt1-4, dummy_ct keep the last session's values), returns 1.
 *  - LinkSyncStop: mode 3 has the default body (no Ether call). LinkSyncTerm with set != tx and a mode without
 *    transport returns -1 and leaves init set.
 *  - LinkSendData copies with a do-while (size 0 = 2^32 bytes; callers never pass 0).
 *  - LinkStart drops the fsilk_IlinkStart / Ether_Start results (returns 1); LinkTerm clears lnkInit before
 *    fslib_MdmTerm runs, mode 3 Term is a no-op returning 1. LinkReset forwards a0 to fsilk_IlinkReset (DWARF void).
 *  - SetPlayerNum writes ILINKSYNC +0x7e / +0x7f (DWARF param / param_set), not .player.
 *  - MdmUpdateRecv: the "Data recv error" print shows the header's recv result (always 12), not the payload's; the
 *    peer's u16 size is used unchecked (param_recv_buf has no bound, dustbox is 128 B); the inner loop does not re-test
 *    nRxBuf < 0x40 (up to 64 more headers per call); kind-4 headers and any header with sender status 2 are consumed
 *    without being queued (a kind-3 record from a status-2 sender is dropped).
 *  - MdmUpdateRxData: the packet-number check compares the low byte but prints the full int; on a mismatch nRxBuf is
 *    already decremented and prev_* stay; rel * framesize is not wrapped to the frame ring; the whole record (header
 *    bytes included) lands in the peer slot, so frame bytes 0-1 / 2 / 6 / 7 are the Mdm header on both ends (the
 *    game word, bytes 8-11, and the tuner bytes 3-5 that LinkSyncSetData writes survive).
 *  - _fslink_set_param_data always writes TxData (+12 / [0..1] / [7]) even when MdmProc's record is TxData + 12;
 *    it consumes the bytes only when paramFlap == 1 (one kind-4 packet every second tick).
 *  - MdmTerm does not reset rcid / scid / mdmStat.
 *  - LinkValidInterface: vendor 3 returns -1 on the first non-matching interface (0-2 keep searching); vendor > 3
 *    builds the mask and returns -1; the GetIfList count is not clamped to the 16-entry list.
 *  - LinkInit: linkMode is stored before any check (also for unknown modes and the lnkInit == 1 early return); mode 3
 *    returns the caller's s0 on every path that is not an IsBusy error (no assignment); mode 1 does not clear env_data
 *    (strcat grows outside_delay by ",,,," per call) and clears eventFlag[0] up front, mode 2 memsets it; pri1 is sent
 *    to CnfIfCtl uninitialised (stale stack); driverID is never written, so modes 1 / 2 always return -3.
 * L3 = the sync front end (LinkSyncProc / SetData / GetData / Start / Index) and the remaining dispatchers: nothing
 * runs offline (KeySyncProc / the lobby readers are gated on connected peers; SyncProc / SetData / GetData only have
 * the interpreted DF_SyncLink). Sync slot (12 bytes, one per player per frame): 0-1 / 2 / 6 / 7 Mdm header, 3 / 4 / 5
 * = low bytes of my rx / set / get counters at SetData time (the tuner's inputs), 8-11 = the game's pad word.
 *  - LinkSyncProc calls the sync-index function of the mode first in every mode (result unused for 0 / 3); bumps
 *    sync_pc_frame before the init test; rebuilds stat from 2 every call (returns 0 / 1 / 2 | 4 | 8 | 0x20); the
 *    ready handshake completes inside the same call; the window slides by one frame per call; the timeout sets 0x20
 *    before its print (one int, a2 stale). Tuner: reg / 10, byte counters compared as plain ints (misreads across
 *    the 256 wrap), reg has no lower clamp; the early returns (cnt2 avail >= 8, cnt1 rx compare, sff <= 0) skip the
 *    cnt decrements below them and the submit bit. The cnt1 gather writes `player` ints into the int rx_frame[2] at
 *    sp+0x28 (player > 2 overruns into the caller's frame; player == 1 compares one stale entry).
 *  - LinkSyncSetData's generic arm re-dispatches the index on the same linkMode (dead 3 / 0 arms); SetData / GetData
 *    do not check the window bounds; GetData has no availability test.
 *  - Default returns differ per function: SyncIndex = MyNodeID, GetIndex = 0, GetConnectedNum / LinkProc = -1,
 *    LinkDestroy = 0 (no Mdm teardown; Ether_Term only runs here). LinkSyncStart mode 3 = the default body.
 *  - LinkRecvData's default arm ignores bno and returns -1 (nothing consumed) when size > param_recv_size[0]; the
 *    Ether arm passes (buf, size, bno) = Ether_RecvData's own order. */
#include "acnb/port.h"
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
int linkMode = -1;
short fslnk_LinkSyncProc__sff = 1;
int fslnk_LinkSyncProc__cnt1;
int fslnk_LinkSyncProc__cnt2;
int fslnk_LinkSyncProc__rBack;
char liblnk_msg_modem_error[18] = "Modem Error : %d\n";
char liblnk_ip_101[15] = "172.16.255.101";
char liblnk_ip_102[15] = "172.16.255.102";
char liblnk_msg_recv_error[41] = "Data recv error, ret = %d( size = %d )\n\n";
char liblnk_msg_packet_error[50] = ">< >< >< [ Packet No. Error, %d != %d ] >< >< ><\n";
char liblnk_msg_iop_mem[14] = "IOP MEM = %d\n";
char liblnk_msg_vendor_error[22] = "  vendor = error[%d]\n";
char liblnk_msg_prio_start[20] = "Set Priority start\n";
char liblnk_msg_prio_end[18] = "Set Priority end\n";
char liblnk_msg_get_over_set[43] = "ERROR GET FRAME OVER SET FRAME !!!! %d %d\n";
char liblnk_msg_sync_timeout[43] = "IlinkSyncProc timeout.( MySyncStat = %d )\n";
#endif

#define LNK_M __ILINKMNG
#define LNK_S __ILINKSYNC

static void fslib_MdmUpdateRecv(void)
{
    PS2_FRAME(0x30);
    int queued = 0;
    int st = LNK_M.MySyncStatus;
    if (st == -1) {
        LNK_M.MySyncStatus = 0;
        return;
    }
    if (st == 0) {
        if (rcid <= 0 || scid <= 0) return;
        if (fscom_LeftRecvSize(&mcd) < 12) return;
        int r = fscom_Recv(&mcd, LNK_M.RxBuf[LNK_M.pRxBuf], 12, 0, 0);
        FlushCache(0);
        if (r != 12) return;
        LNK_M.MySyncStatus = 1;   /* header not inspected here */
        LNK_M.nRxBuf++;
        LNK_M.pRxBuf = (LNK_M.pRxBuf + 1) & 0x3f;
        return;
    }
    while (LNK_M.nRxBuf >= 0x40) {
        if (fslib_MdmUpdateRxData() < 0) LNK_M.MySyncStatus = -1;
    }
    if (fscom_LeftRecvSize(&mcd) < 12) return;
    do {
        int hr = fscom_Recv(&mcd, LNK_M.RxBuf[LNK_M.pRxBuf], 12, 0, 0);
        if (hr != 12) {
            LNK_M.MySyncStatus = -1;
            continue;
        }
        unsigned char *e = (unsigned char *)LNK_M.RxBuf[LNK_M.pRxBuf];
        if (e[7] == 4) {
            /* param payload header: not queued */
            int size = *(unsigned short *)e;
            int prev = prev_async_tran;
            LNK_M.p[0].RxSyncStatus = e[6];
            LNK_M.p[0].RxTranPacket = e[2];
            if (LNK_M.p[0].RxTranPacket == prev) {
                fscom_Recv(&mcd, dustbox, size, 0, 0);   /* duplicate: swallow the payload (size unchecked) */
                FlushCache(0);
                return;
            }
            int r = fscom_Recv(&mcd, LNK_M.param_recv_buf[0] + LNK_M.param_recv_size[0], size, 0, 0);
            if (size != r) ps2_scePrintf_dd(liblnk_msg_recv_error, hr, size);   /* the header result (quirk) */
            FlushCache(0);
            int total = LNK_M.param_recv_size[0] + size;
            prev_async_tran = LNK_M.p[0].RxTranPacket;
            LNK_M.param_recv_size[0] = total;
            return;
        }
        if (e[6] == 2) {
            LNK_M.MySyncStatus = 3;   /* consumed, not queued */
            continue;
        }
        LNK_M.pRxBuf = (LNK_M.pRxBuf + 1) & 0x3f;
        LNK_M.nRxBuf++;
        if (++queued >= 0x40) return;
    } while (fscom_LeftRecvSize(&mcd) >= 12);   /* no nRxBuf < 0x40 re-test (quirk) */
}

static int fslib_MdmUpdateRxData(void)
{
    PS2_FRAME(0x40);
    unsigned char *rx = (unsigned char *)LNK_M.RxData;
    FlushCache(0);
    ps2_memcpy(rx, LNK_M.RxBuf[LNK_M.tRxBuf], 12);
    LNK_M.nRxBuf--;
    LNK_M.p[0].RxSyncStatus = rx[6];
    LNK_M.tRxBuf = (LNK_M.tRxBuf + 1) & 0x3f;
    LNK_M.p[0].RxTranPacket = rx[2];
    if ((LNK_S.stat & 2) && prev_rxmode_m == 3 && rx[7] == 3) {
        int exp = prev_rxtran_packet_m + 1;
        int e8 = exp & 0xff;
        if (exp < 0 && e8 != 0) e8 -= 0x100;   /* signed exp % 256 */
        if ((LNK_M.p[0].RxTranPacket & 0xff) != e8) {
            ps2_scePrintf_dd(liblnk_msg_packet_error, LNK_M.p[0].RxTranPacket, exp);
            return -1;
        }
    }
    prev_rxtran_packet_m = LNK_M.p[0].RxTranPacket;
    prev_rxmode_m = rx[7];
    if (rx[7] == 3) {
        int n = LNK_S.sync_data_size;
        int rel = (int)(LNK_S.sync_rx_frame[0] - LNK_S.sync_data_first_frame);
        int idx;
        if (linkMode == 3) idx = Ether_GetSyncIndex();
        else if (linkMode == 0) idx = fsilk_IlinkSyncIndex();
        else idx = LNK_M.MyNodeID;
        char *dst = LNK_S.sync_datap + LNK_S.sync_data_framesize * rel + LNK_S.sync_data_size * (1 - idx);
        const char *src = (const char *)rx;   /* the whole record, header bytes included */
        for (; n != 0; n--) *dst++ = *src++;
        LNK_S.sync_rx_frame[0]++;
        LNK_M.p[0].MyRecvPacket++;
        return 1;
    }
    if (rx[7] == 5) LNK_M.p[0].RcvTxRxReady = 1;
    return 1;
}

static int fslib_MdmProc(void)
{
    PS2_FRAME(0x30);
    char *tx = LNK_M.TxData;
    char *p;
    FlushCache(0);
    if (LNK_M.enable != 1) return LNK_M.MySyncStatus;
    if (eventFlag[0] == 4 || eventFlag[0] == 2 || eventFlag[0] == 5) {
        ps2_scePrintf_dd(liblnk_msg_modem_error, eventFlag[0], 0);   /* one int; a2 is stale in the original */
        int ev = eventFlag[0];
        LNK_M.MySyncStatus = -1;
        LNK_M.errorCode = ev;
        return LNK_M.MySyncStatus;
    }
    if (get_timer1() < 0x1312d) return LNK_M.MySyncStatus;
    clear_timer1();
    if (syncFlip == 1 && (LNK_S.stat & 2)) {
        p = tx + 12;
    } else {
        ps2_memset(tx, 0, 0x18);
        p = tx;
    }
    LNK_M.MyProcFrame++;
    p[7] = 1;
    *(unsigned short *)p = 0;
    unsigned char *s0 = (unsigned char *)&p[7];
    if (LNK_M.MySyncStatus == 3) {
        if (LNK_S.stat & 2) {
            if (LNK_S.sync_tx_frame < LNK_S.sync_set_frame) _fslink_set_sync_data();
            else *s0 = 2;
        }
        if ((*s0 == 1 || *s0 == 2) && LNK_M.param_send_size > 0) _fslink_set_param_data();
        if ((*s0 == 1 || *s0 == 2) && LNK_M.MyTxRxReady > 0) *s0 = 5;
    }
    int st = LNK_M.MySyncStatus;
    if (st == 1 && LNK_M.p[0].RxSyncStatus == 1) {
        LNK_M.MySyncStatus = 3;
        p[6] = 2;
    } else {
        p[6] = (char)st;
    }
    p[2] = (char)(LNK_M.MyTranPacket % 256);
    st = LNK_M.MySyncStatus;
    if (st == -1) return -1;
    if (st != 0) {
        if (LNK_S.stat & 2) {
            /* sync running */
            if (*s0 != 1 && *s0 != 2) {
                if (syncFlip == 1) {
                    syncFlip = 0;
                    FlushCache(0);
                    fscom_Send(&mcd, tx, 0x18, 0, 1, 1);
                } else {
                    syncFlip = 1;
                }
                if (LNK_S.sync_tx_frame < LNK_S.sync_set_frame) {
                    LNK_S.sync_tx_frame++;
                    LNK_M.MyTranPacket++;
                }
            }
        } else {
            s0 = (unsigned char *)&tx[7];
            unsigned char k = *s0;
            if (k == 4) {
                if (paramFlap == 1) {
                    fscom_Send(&mcd, tx, *(unsigned short *)tx + 0xc, 0, 1, 1);
                    paramFlap = 0;
                    LNK_M.MyTranPacket++;
                    LNK_S.sync_tx_frame++;
                } else {
                    paramFlap = 1;
                }
            } else if ((unsigned char)((unsigned char)tx[6] - 1) < 2 || k == 5) {
                LNK_M.sendsynctx_retry = 0;
                fscom_Send(&mcd, tx, 0xc, 0, 1, 1);
                if (LNK_S.sync_tx_frame < LNK_S.sync_set_frame && *s0 == 4) LNK_S.sync_tx_frame++;   /* dead */
                LNK_M.MyTranPacket++;
            }
        }
        if (LNK_M.p[0].RxSyncStatus == 0) {
            LNK_M.connect_timeout++;
            if (LNK_M.connect_timeout >= 0x96) {
                LNK_M.connect_timeout = 0;
                LNK_M.MySyncStatus = 0;
            }
        }
    } else {
        FlushCache(0);
        int *sp;
        char *ip;
        switch (mdmStat) {
        case 0:
            fscom_CnfDownInterface(&mcd, 0);
            fscom_CnfUpInterface(&mcd, interfaceID);
            mdmStat = 1;
            break;
        case 1:
            if (eventFlag[0] == 3) {
                mdmStat = 2;
                eventFlag[0] = 0;
            } else if (eventFlag[0] != 0) {
                eventFlag[0] = 0;
            }
            break;
        case 2:
            sp = (linkMode == 1) ? &scid : &rcid;
            ip = (linkMode == 1) ? liblnk_ip_101 : liblnk_ip_102;
            *sp = fscom_CreateSocket(&mcd, 0, 0x15b3, 0x15b3, ip, -1);
            FlushCache(0);
            if (linkMode == 1) fscom_InitSend(&mcd, scid, 0x12);
            else fscom_InitRecv(&mcd, rcid, 0x11);
            mdmStat = 3;
            break;
        case 3:
            sp = (linkMode == 1) ? &scid : &rcid;   /* same selector as case 2 (quirk) */
            ip = (linkMode == 1) ? liblnk_ip_101 : liblnk_ip_102;
            *sp = fscom_CreateSocket(&mcd, 0, 0x15b4, 0x15b4, ip, -1);
            FlushCache(0);
            if (linkMode == 1) fscom_InitRecv(&mcd, rcid, 0x11);
            else fscom_InitSend(&mcd, scid, 0x12);
            mdmStat = 4;
            break;
        default:
            if (LNK_M.MyProcFrame % 30 == 0) {
                FlushCache(0);
                fscom_Send(&mcd, tx, 0xc, 0, 1, 1);
            }
            break;
        }
    }
    if (rcid > 0 && scid > 0) {
        fslib_MdmUpdateRecv();
        fscom_Proc(&mcd);
    }
    while (LNK_M.nRxBuf != 0) {
        if (fslib_MdmUpdateRxData() < 0) return -1;
    }
    return LNK_M.MySyncStatus;
}

static int fslib_MdmTerm(void)
{
    PS2_FRAME(0x10);
    if (rcid > 0 && scid > 0) {
        fscom_AbortSocket(&mcd, rcid);
        fscom_CloseSocket(&mcd, rcid, -1);
        fscom_AbortSocket(&mcd, scid);
        fscom_CloseSocket(&mcd, scid, -1);
        fscom_TermRecv(&mcd);
        fscom_TermSend();   /* DWARF void (the original's a0 = &mcd is ignored) */
    }
    fscom_CnfDeleteEvent(&mcd);
    ifevf = 0;
    eventFlag[0] = 0;   /* jal delay slot: before CnfDownInterface */
    fscom_CnfDownInterface(&mcd, interfaceID);
    ps2_memset(&env_data, 0, 0xa9c);
    fscom_CnfIfCtl(&mcd, interfaceID, 2, VendorName, 0x100);
    fscom_CnfIfCtl(&mcd, interfaceID, 3, DeviceName, 0x100);
    ps2_strcpy(env_data.vendor, VendorName);
    ps2_strcpy(env_data.product, DeviceName);
    FlushCache(0);
    fscom_CnfSetConf(&mcd, &env_data, 0, 0, 0, 0);
    ps2_scePrintf_dd(liblnk_msg_iop_mem, (int)fscom_GetLeftIopMem(&mcd), 0);   /* one int */
    return 1;
}

static int fslib_MdmInit(int mode)
{
    PS2_FRAME(0x10);
    LNK_M.enable = 0;
    LNK_M.MyProcFrame = 0;
    LNK_M.MyTranPacket = 0;
    LNK_M.p[0].MyRecvPacket = 0;
    LNK_M.param_send_size = 0;
    LNK_M.param_recv_size[0] = 0;
    LNK_M.MyTxRxReady = 0;
    LNK_M.p[0].RcvTxRxReady = 0;
    LNK_M.connect_timeout = 0;
    LNK_M.connect_retry = 0;
    LNK_M.sendsynctx_retry = 0;
    LNK_M.sendsyncbc_retry = 0;
    LNK_M.SendAsyncRet = 0;
    LNK_M.pRxBuf = 0;
    LNK_M.tRxBuf = 0;
    LNK_M.nRxBuf = 0;
    LNK_M.MySyncStatus = 0;
    LNK_M.MyNodeID = mode;   /* the argument is the node id */
    ps2_memset(&LNK_S, 0, 0x20);
    syncFlip = 0;
    LNK_S.stat = 0;
    LNK_S.init = 0;
    LNK_S.start = 0;
    LNK_S.sync_timeout = 0;   /* jal delay slot: before init_timer1 */
    init_timer1();
    return 1;
}

static void _fslink_set_param_data(void)
{
    /* frameless leaf, tail call to FlushCache */
    unsigned int n = (unsigned int)LNK_M.param_send_size;
    if (n >= 0x34) n = 0x34;   /* sltiu: negative clamps too */
    const char *src = LNK_M.param_send_buf;
    char *dst = LNK_M.TxData + 12;
    for (unsigned int c = n; c != 0; c--) *dst++ = *src++;
    *(unsigned short *)LNK_M.TxData = (unsigned short)n;
    LNK_M.TxData[7] = 4;
    if (paramFlap == 1) {
        char *d = LNK_M.param_send_buf;
        const char *s2 = d + n;
        for (unsigned int c = 0x8000 - n; c != 0; c--) *d++ = *s2++;
        LNK_M.param_send_size -= (int)n;
    }
    FlushCache(0);
}

static void _fslink_set_sync_data(void)
{
    PS2_FRAME(0x20);
    int rel = LNK_S.sync_tx_frame - LNK_S.sync_data_first_frame;
    int idx;
    if (linkMode == 3) idx = Ether_GetSyncIndex();
    else if (linkMode == 0) idx = fsilk_IlinkSyncIndex();
    else idx = LNK_M.MyNodeID;
    int n = LNK_S.sync_data_size;   /* loaded after the index call */
    const char *src = LNK_S.sync_datap + LNK_S.sync_data_framesize * rel + n * idx;
    char *dst = syncFlip ? LNK_M.TxData + 12 : LNK_M.TxData;
    for (; n != 0; n--) *dst++ = *src++;
    char *rec = syncFlip ? LNK_M.TxData + 12 : LNK_M.TxData;
    rec[7] = 3;
    *(unsigned short *)rec = (unsigned short)LNK_S.sync_data_size;   /* jal delay slot: before FlushCache */
    FlushCache(0);
}

static inline int liblnk_connected(void)
{
    return LNK_M.MySyncStatus == 3 && (LNK_M.p[0].RxSyncStatus == 1 || LNK_M.p[0].RxSyncStatus == 3);
}

int fslnk_LinkCheckConnect(int block, int connect_no)
{
    PS2_FRAME(0x20);
    if (linkMode == 3) return Ether_CheckConnect(block, connect_no);
    if (linkMode == 0) return fsilk_IlinkCheckConnect(block, connect_no);
    do {
        FlushCache(0);
        if (fslib_MdmProc() < 0) return -1;
        if (liblnk_connected()) return 1;
    } while (block != 0);   /* no wait, no timeout (quirk) */
    int st = LNK_M.MySyncStatus;
    if (st == 0 || st == -1) return 0;
    for (int n = 10; n != 0; n--) {
        FlushCache(0);
        sceGsSyncV(0);
        if (fslib_MdmProc() < 0) return -1;
        if (liblnk_connected()) return 1;
    }
    return 0;
}

int fslnk_LinkSyncStop(void)
{
    PS2_FRAME(0x10);
    if (linkMode == 3) {
        LNK_S.start = 0;
        return 1;
    }
    if (linkMode == 0) return fsilk_IlinkSyncStop();
    LNK_S.start = 0;
    return 1;
}

int fslnk_LinkSyncTerm(void)
{
    PS2_FRAME(0x10);
    int mode = linkMode;
    if (mode == 3) return Ether_SyncTerm();
    if (mode == 0) return fsilk_IlinkSyncTerm();
    if (LNK_S.sync_set_frame != LNK_S.sync_tx_frame) {
        int r;
        if (mode == 3) r = Ether_Proc();                    /* dead: mode != 3 here */
        else if (mode == 2 || mode == 1) r = fslib_MdmProc();
        else if (mode == 0) r = fsilk_IlinkProc();          /* dead: mode != 0 here */
        else r = -1;
        if (r < 0) return r;
    }
    LNK_S.stat = 0;
    LNK_S.init = 0;
    return 1;
}

int fslnk_LinkSyncInit(char player, short size, char fps, char dup, char late)
{
    PS2_FRAME(0x40);
    (void)late;
    if (linkMode == 3) return Ether_SyncInit(player, size, fps, dup);
    if (linkMode == 0) return fsilk_IlinkSyncInit(player, size, fps, dup);
    ps2_memset(&LNK_S, 0, 0x20);
    ps2_memset(sync_data_buf, 0, 0x14000);
    ps2_memset(sync_flag_buf, 0, 5);
    if (player >= 6) player = 5;
    LNK_S.fps = fps;
    LNK_S.dup = dup;
    LNK_S.late = 1;
    LNK_S.sync_data_size = 12;
    LNK_S.player = player;
    LNK_S.sync_data_first_frame = 0;
    LNK_S.sync_tx_frame = 0;
    LNK_S.sync_data_framesize = player * 12;
    LNK_S.sync_datap = sync_data_buf;
    LNK_S.sync_flagp = sync_flag_buf;
    LNK_S.sync_rx_frame[0] = 0;
    LNK_S.sync_set_frame = 0;
    LNK_S.sync_get_frame = 0;
    LNK_S.sync_pc_frame = 0;
    LNK_S.sync_data_fullsize = LNK_S.sync_data_framesize << 6;
    if (LNK_M.MySyncStatus == 3 && LNK_M.p[0].RxSyncStatus == 3) LNK_S.stat = 2;
    else LNK_S.stat = 1;
    LNK_S.start = 0;
    reg = 0x32;
    LNK_S.sync_timeout = 0;
    LNK_S.init = 1;
    prev_rxproc_packet = 0;
    LNK_S.first_frame = 1;
    paramFlap = 0;
    LNK_M.pRxBuf = 0;
    syncFlip = 0;
    LNK_M.tRxBuf = 0;
    prev_rxtran_packet_m = -1;
    LNK_M.nRxBuf = 0;
    prev_rxmode_m = -1;
    return 1;
}

int fslnk_LinkSendData(void *send_data, int size)
{
    PS2_FRAME(0x10);
    if (linkMode == 3) return Ether_SendData(send_data, size);
    if (linkMode == 0) return fsilk_IlinkSendData(send_data, size);
    int n = LNK_M.param_send_size;
    if (n + size >= 0x8001) return -1;
    LNK_M.param_send_size = n + size;
    char *dst = LNK_M.param_send_buf + n;
    const char *src = (const char *)send_data;
    unsigned int cnt = (unsigned int)size;
    do {
        *dst++ = *src++;
    } while (--cnt);   /* do-while: size 0 wraps (quirk) */
    return 1;
}

int fslnk_LinkStop(void)
{
    PS2_FRAME(0x10);
    if (linkMode == 3) return Ether_Stop();
    if (linkMode == 0) return fsilk_IlinkStop();
    LNK_M.enable = 0;
    clear_timer1();
    return 1;
}

int fslnk_LinkStart(void)
{
    PS2_FRAME(0x10);
    int mode = linkMode;
    if (mode == 3) {
        Ether_Start();
    } else if (mode == 2 || mode == 1) {
        LNK_M.enable = 1;
        clear_timer1();
    } else if (mode == 0) {
        fsilk_IlinkStart();
    } else {
        return -1;
    }
    return 1;   /* the callee results are dropped */
}

int fslnk_LinkTerm(void)
{
    PS2_FRAME(0x10);
    int mode = linkMode;
    if (mode == 3) return 1;   /* no Ether_Term */
    if (mode == 2 || mode == 1) {
        if (lnkInit == 0) return -1;
        lnkInit = 0;   /* before MdmTerm (jal delay slot) */
        return fslib_MdmTerm();
    }
    if (mode == 0) return fsilk_IlinkTerm();
    return -1;
}

int fslnk_LinkValidInterface(int vendor)
{
    PS2_FRAME(0x2a0);
    PS2_STACK(char[0x100], name, 0x60);
    PS2_STACK(char[0x100], vend, 0x160);
    PS2_STACK(int[16], ifl, 0x260);
    static const char *const pat[4] = {"ppp,ostmdm_s", "ppp,OMModem", "ppp,cxtmdm", "ppp,spduart"};
    int mask = 0;
    ps2_memset(ifl, 0, 0x40);
    int n = fscom_GetIfList(&mcd, ifl, 0x10);
    for (int i = 0; i < n; i++) {   /* n not clamped to 16 (quirk) */
        if (ifl[i] <= 0) continue;
        ps2_memset(name, 0, 0x100);
        fscom_CnfIfCtl(&mcd, ifl[i], 0, name, 9);
        ps2_memset(vend, 0, 0x100);
        int r = fscom_CnfIfCtl(&mcd, ifl[i], 1, vend, 0x100);
        if (r < 0) {
            ps2_scePrintf_dd(liblnk_msg_vendor_error, r, 0);   /* one int */
            continue;
        }
        if (vendor == 3 || vendor == 2 || vendor == 1 || vendor == 0) {
            const char *s = pat[vendor];
            if (ps2_strncmp(vend, s, ps2_strlen(s)) != 0) {
                if (vendor == 3) return -1;   /* vendor 3 aborts the search (quirk) */
                continue;
            }
            fscom_CnfIfCtl(&mcd, ifl[i], 2, VendorName, 0x100);
            fscom_CnfIfCtl(&mcd, ifl[i], 3, DeviceName, 0x100);
            return ifl[i];
        }
        if (ps2_strncmp(vend, pat[0], ps2_strlen(pat[0])) == 0) mask |= 1;
        else if (ps2_strncmp(vend, pat[1], ps2_strlen(pat[1])) == 0) mask |= 2;
        else if (ps2_strncmp(vend, pat[2], ps2_strlen(pat[2])) == 0) mask |= 4;
        else if (ps2_strncmp(vend, pat[3], ps2_strlen(pat[3])) == 0) mask |= 8;
    }
    if (vendor >= 0) mask = -1;
    return mask;
}

int fslnk_GetForceConnect(void)
{
    return LNK_M.force_connect;
}

int fslnk_SetPlayerNum(char player)
{
    LNK_S.param = player;
    LNK_S.param_set = 1;
    return 1;
}

int fslnk_SetLiveMonitor(int sw)
{
    LNK_M.live_monitor = sw;
    return LNK_M.live_monitor;
}

int fslnk_SetOnLook(int onlook)
{
    LNK_M.on_look = onlook;
    return LNK_M.on_look;
}

int fslnk_SetCheckAlive(int mode)
{
    PS2_FRAME(0x10);
    if (linkMode == 3) {
        LNK_M.no_timeout = 1 - (mode == 1);
        return LNK_M.no_timeout;
    }
    if (linkMode == 0) return fsilk_IlinkSetCheckAlive(mode);
    return 0;
}

int fslnk_LinkReset(int flag)
{
    PS2_FRAME(0x10);
    if (linkMode == 0) return gcall_as<int(int)>(GFN(fsilk_IlinkReset), flag);   /* DWARF void: a0 forwarded */
    return 0;
}

/* modem bring-up shared by LinkInit modes 1 / 2 (pri1 / drain are the mode's own stack slots) */
static inline int liblnk_mdm_bringup(int *pri1, int *pri2, char *drain, int node)
{
    if (driverID == -1) {   /* always: driverID is never written */
        lnkInit = 0;
        return -3;
    }
    interfaceID = -1;   /* jal delay slot */
    interfaceID = fslnk_LinkValidInterface(driverID);
    if (interfaceID < 0) {
        lnkInit = 0;
        return -4;
    }
    ps2_strcpy(env_data.vendor, VendorName);
    ps2_strcpy(env_data.product, DeviceName);
    fscom_CnfSetConf(&mcd, &env_data, 0, 0, 0, 0);
    fscom_CnfIfCtl(&mcd, interfaceID, (int)0xc0000000u, pri1, 4);   /* pri1 uninitialised (quirk) */
    ps2_scePrintf(liblnk_msg_prio_start);
    fscom_CnfIfCtl(&mcd, interfaceID, (int)0xc1000000u, pri2, 4);
    ps2_scePrintf(liblnk_msg_prio_end);
    if (ifevf == 0) {
        FlushCache(0);
        eventFlag[0] = 0;   /* jal delay slot */
        fscom_CnfWaitEvent(&mcd, interfaceID, -1, &eventFlag[0], -1);
        ifevf = 1;
    }
    int n = (int)fscom_LeftRecvSize(&mcd);
    while (n > 0) {
        if (n >= 0x200) n = 0x200;
        fscom_Recv(&mcd, drain, n, 0, 0);
        n = (int)fscom_LeftRecvSize(&mcd);
    }
    return fslib_MdmInit(node);
}

int fslnk_LinkInit(int mode)
{
    int rv = (int)PS2_STALE_GPR(16);   /* mode 3 returns the caller's s0 (never assigned there) */
    PS2_FRAME(0x430);
    PS2_STACK(int, pri2, 0x42c);
    linkMode = mode;
    pri2 = 0xd;
    if (mode == 3) {
        if (Ether_Init() != 1) return rv;
        int id = play_info.ether_id;
        if (id >= 7) return rv;
        if (Ether_BootNetwork(id) == 0) return rv;
        int r;
        while ((r = Ether_IsBusy()) == 1) Draw_FlipBufferStandard();
        if (r == -1) return -1;
        if (Ether_Connect() == 0) return rv;
        while ((r = Ether_IsBusy()) == 1) Draw_FlipBufferStandard();
        if (r == -1) return -1;
        return rv;
    }
    if (mode == 2) {
        PS2_STACK(int, pri1, 0x424);   /* never written (quirk) */
        PS2_STACK(char[0x200], drain, 0x20);
        if (lnkInit == 1) return -1;
        lnkInit = 1;
        scid = -1;
        rcid = -1;
        mdmStat = 0;   /* jal delay slot: before the memset */
        ps2_memset(&env_data, 0, 0xa9c);
        env_data.dhcp = 0;
        env_data.type = 2;
        env_data.answer_mode = 1;
        env_data.want.auth = 3;
        env_data.allow.auth = 3;
        env_data.want.f_auth = 1;
        env_data.allow.f_auth = 1;
        ps2_strcpy(env_data.auth_name, "ac2aa_name");
        ps2_strcpy(env_data.auth_key, "ac2aa_key");
        ps2_strcpy(env_data.peer_name, "ac2aa_name");
        ps2_strcpy(env_data.peer_key, "ac2aa_key");
        ps2_strcpy(env_data.address, liblnk_ip_101);
        ps2_strcpy(env_data.netmask, "255.255.255.0");
        ps2_strcpy(env_data.gateway, "");
        ps2_strcpy(env_data.want.ip_address, liblnk_ip_101);
        ps2_strcpy(env_data.allow.ip_address, liblnk_ip_102);
        return liblnk_mdm_bringup(&pri1, &pri2, drain, 1);
    }
    if (mode == 1) {
        PS2_STACK(int, pri1, 0x428);   /* never written (quirk) */
        PS2_STACK(char[0x200], drain, 0x220);
        if (lnkInit == 1) return -1;
        env_data.type = 2;   /* no memset of env_data (quirk) */
        eventFlag[0] = 0;
        lnkInit = 1;
        env_data.dhcp = 0;
        scid = -1;
        env_data.answer_mode = 0;
        rcid = -1;
        env_data.want.auth = 3;
        mdmStat = 0;
        env_data.allow.auth = 3;
        env_data.want.f_auth = 1;
        env_data.allow.f_auth = 1;
        ps2_strcpy(env_data.attach_ifc, "");
        ps2_strcpy(env_data.attach_dev, "");
        ps2_strcpy(env_data.dns1_address, "");
        ps2_strcpy(env_data.dns2_address, "");
        ps2_strcpy(env_data.address, liblnk_ip_102);
        ps2_strcpy(env_data.netmask, "255.255.255.0");
        ps2_strcpy(env_data.gateway, "");
        ps2_strcpy(env_data.auth_name, "ac2aa_name");
        ps2_strcpy(env_data.auth_key, "ac2aa_key");
        ps2_strcpy(env_data.peer_name, "ac2aa_name");
        ps2_strcpy(env_data.peer_key, "ac2aa_key");
        ps2_strcpy(env_data.want.ip_address, liblnk_ip_102);
        ps2_strcpy(env_data.allow.ip_address, liblnk_ip_101);
        /* strcat (0x118ed8; no ps2_strcat): outside_delay grows by ",,,," per call (quirk) */
        ps2_strcpy(env_data.outside_delay + ps2_strlen(env_data.outside_delay), ",,,,");
        if (ps2_strcmp(env_data.phone_numbers, "") == 0) {
            lnkInit = 0;
            return -2;
        }
        return liblnk_mdm_bringup(&pri1, &pri2, drain, 0);
    }
    if (mode == 0) return fsilk_IlinkInit();
    return -1;
}

int fslnk_GetLinkMode(void)
{
    return linkMode;
}

void fslnk_InitLinkMode(void)
{
    linkMode = -1;
}

/* ---- L3: sync front end + remaining dispatchers (none reached offline) ---- */

int fslnk_LinkSyncIndex(void)
{
    PS2_FRAME(0x10);
    if (linkMode == 3) return Ether_GetSyncIndex();
    if (linkMode == 0) return fsilk_IlinkSyncIndex();
    return LNK_M.MyNodeID;
}

int fslnk_LinkSyncGetData(void *sync_data)
{
    PS2_FRAME(0x10);
    if (linkMode == 3) return Ether_SyncGetData(sync_data);
    if (linkMode == 0) return fsilk_IlinkSyncGetData(sync_data);
    if (!LNK_S.init) return -1;
    char *dst = (char *)sync_data;
    const char *src = LNK_S.sync_datap +
                      LNK_S.sync_data_framesize * (LNK_S.sync_get_frame - LNK_S.sync_data_first_frame);
    /* the 4-byte game word (bytes 8-11) of each slot, packed; stride is the literal 12; no availability test */
    for (unsigned int n = (unsigned int)(int)LNK_S.player; n != 0; n--) {
        src += 8;
        for (int k = 0; k < 4; k++) *dst++ = *src++;
    }
    LNK_S.sync_get_frame++;
    return 1;
}

int fslnk_LinkSyncSetData(void *sync_data)
{
    PS2_FRAME(0x30);
    int mode = linkMode;
    if (mode == 3) return Ether_SyncSetData(sync_data);
    if (mode == 0) return fsilk_IlinkSyncSetData(sync_data);
    if (!LNK_S.init) return -1;
    int rel = LNK_S.sync_set_frame - LNK_S.sync_data_first_frame;   /* no window bound check */
    int idx;
    if (mode == 3) idx = Ether_GetSyncIndex();          /* dead: same linkMode as above */
    else if (mode == 0) idx = fsilk_IlinkSyncIndex();   /* dead */
    else idx = LNK_M.MyNodeID;
    unsigned char *slot =
        (unsigned char *)(LNK_S.sync_datap + LNK_S.sync_data_framesize * rel) + LNK_S.sync_data_size * idx;
    slot[3] = (unsigned char)LNK_S.sync_rx_frame[0];
    slot[4] = (unsigned char)LNK_S.sync_set_frame;
    slot[5] = (unsigned char)LNK_S.sync_get_frame;
    const unsigned short *w = (const unsigned short *)sync_data;
    *(unsigned short *)(slot + 8) = w[0];
    *(unsigned short *)(slot + 10) = w[1];
    LNK_S.sync_set_frame++;
    return 1;
}

/* the frames-available test LinkSyncProc expands three times (operands reloaded each time; late is a signed byte) */
static inline int liblnk_sync_avail(void)
{
    int get = LNK_S.sync_get_frame;
    int d = (int)(LNK_S.sync_rx_frame[0] - (unsigned int)get);
    int late = LNK_S.late;
    if (d > late && get < LNK_S.sync_set_frame) return (int)((unsigned int)d - (unsigned int)late);
    return -1;
}

#ifdef ACNB_SOURCEPORT
#define LNK_RXF_N 5   /* host array: SyncInit clamps player to 5 (the original's int[2] overruns its frame) */
#else
#define LNK_RXF_N 2   /* the original's slot; player > 2 writes on into the caller's PS2 frame as on the PS2 */
#endif

int fslnk_LinkSyncProc(void)
{
    PS2_FRAME(0x30);
    PS2_STACK(int[LNK_RXF_N], rx_frame, 0x28);
    int index;
    if (linkMode == 3) index = Ether_GetSyncIndex();   /* every mode, before the dispatch */
    else if (linkMode == 0) index = fsilk_IlinkSyncIndex();
    else index = LNK_M.MyNodeID;
    if (linkMode == 3) return Ether_SyncProc();
    if (linkMode == 0) return fsilk_IlinkSyncProc();
    LNK_S.sync_pc_frame++;   /* before the init test */
    if (!LNK_S.init) return 0;
    if (liblnk_connected()) {
        LNK_S.stat = 2;
    } else {
        LNK_S.stat = 1;   /* not connected */
        return LNK_S.stat;
    }
    if (LNK_S.first_frame) {   /* ready handshake */
        if (LNK_M.MyTxRxReady == 0) {
            LNK_M.p[0].RcvTxRxReady = 0;
            LNK_M.MyTxRxReady = 1;
        }
        if (LNK_M.MyTxRxReady == 1 && LNK_M.p[0].RcvTxRxReady == 1) {
            LNK_M.MyTxRxReady = 0;
            LNK_M.p[0].RcvTxRxReady = 0;
            LNK_S.first_frame = 0;   /* and this call goes on */
        } else {
            return LNK_S.stat;
        }
    }
    if (LNK_S.sync_set_frame < LNK_S.sync_get_frame)
        ps2_scePrintf_dd(liblnk_msg_get_over_set, LNK_S.sync_get_frame, LNK_S.sync_set_frame);
    if (LNK_S.sync_data_first_frame < LNK_S.sync_get_frame) {
        if (LNK_S.init) {   /* always true here */
            for (int i = 0; i < 63; i++) {   /* slide the 64-frame window down by one frame */
                int fs = LNK_S.sync_data_framesize;
                const char *src = LNK_S.sync_datap + fs * (i + 1);
                char *dst = LNK_S.sync_datap + fs * i;
                for (unsigned int n = (unsigned int)fs; n != 0; n--) *dst++ = *src++;
            }
        }
        LNK_S.sync_data_first_frame++;
    }
    if (liblnk_sync_avail() > 0) {
        LNK_S.sync_timeout = 0;
        LNK_S.stat |= 8;   /* frames ready */
    } else if (LNK_S.start == 1) {
        LNK_S.sync_timeout++;
        if (LNK_S.sync_timeout >= 0x96) {
            LNK_S.sync_timeout = 0;
            LNK_S.stat |= 0x20;   /* jal delay slot: stored before the print */
            ps2_scePrintf_dd(liblnk_msg_sync_timeout, LNK_M.MySyncStatus, 0);   /* one int; a2 stale */
        }
    }
    if (fslnk_LinkSyncProc__cnt2 == 0) {
        fslnk_LinkSyncProc__cnt2 = 0x78;
        if (liblnk_sync_avail() >= 8) return LNK_S.stat;   /* skips the decrements and the submit bit */
    }
    fslnk_LinkSyncProc__cnt2--;
    if (fslnk_LinkSyncProc__cnt1 == 0) {
        fslnk_LinkSyncProc__cnt1 = 0x12c;
        if (liblnk_sync_avail() > 0) {
            int *rxf = rx_frame;
            const unsigned char *fr = (const unsigned char *)(LNK_S.sync_datap +
                LNK_S.sync_data_framesize * (LNK_S.sync_get_frame - LNK_S.sync_data_first_frame));
            int sz = LNK_S.sync_data_size;
            for (unsigned int n = (unsigned int)(int)LNK_S.player; n != 0; n--) {   /* player > 2 overruns (quirk) */
                *rxf++ = fr[3];
                fr += sz;
            }
            int node = LNK_M.MyNodeID;
            int *rb = rx_frame;   /* player == 1: [1 - node] is stale stack (quirk) */
            if (rb[node] < rb[1 - node]) return LNK_S.stat;
        }
    }
    fslnk_LinkSyncProc__cnt1--;
    if (LNK_S.sync_rx_frame[0] != 0) {   /* lag tuner */
        int player = LNK_S.player;
        unsigned char *slot = (unsigned char *)(LNK_S.sync_datap +
            LNK_S.sync_data_framesize * (LNK_S.sync_get_frame - LNK_S.sync_data_first_frame));
        if (player != 0) {
            int t2 = reg / 10;
            int sz = LNK_S.sync_data_size;
            int i = 0;
            for (unsigned int n = (unsigned int)player; n != 0; n--, i++, slot += sz) {
                if (index != i) continue;   /* only my slot */
                /* byte counters compared as plain ints (quirk) */
                if ((int)(slot[5] + (unsigned int)t2 + 6u) < (int)slot[4] && fslnk_LinkSyncProc__sff >= 2)
                    fslnk_LinkSyncProc__sff = 1;
                if ((int)(slot[5] + (unsigned int)t2 + 4u) < (int)slot[4] && fslnk_LinkSyncProc__sff >= 3)
                    fslnk_LinkSyncProc__sff = 2;
                if ((int)(slot[5] + (unsigned int)t2 + 2u) < (int)slot[4] && fslnk_LinkSyncProc__sff >= 4)
                    fslnk_LinkSyncProc__sff = 3;
                if ((int)(slot[5] + (unsigned int)t2) < (int)slot[4] && fslnk_LinkSyncProc__sff >= 5)
                    fslnk_LinkSyncProc__sff = 4;
                short s = fslnk_LinkSyncProc__sff;
                if (s <= 0) {
                    fslnk_LinkSyncProc__sff = 999;
                    reg++;
                    fslnk_LinkSyncProc__rBack = 0;   /* delay slot: both outcomes */
                    if (reg >= 0x4c) reg = 0x4b;     /* no lower clamp anywhere (quirk) */
                    return LNK_S.stat;               /* skips the other slots and the submit bit */
                }
                int lim = (int)(300u * (16u - (unsigned int)t2));   /* t2 before the decrement */
                fslnk_LinkSyncProc__rBack = fslnk_LinkSyncProc__rBack + 1;
                if (lim < fslnk_LinkSyncProc__rBack) {
                    t2--;
                    fslnk_LinkSyncProc__rBack = 0;
                    reg = reg - 5;
                }
                if (s != 999) fslnk_LinkSyncProc__sff = (short)(fslnk_LinkSyncProc__sff - 1);
            }
        }
    }
    if (LNK_S.sync_data_first_frame + 0x3f >= LNK_S.sync_set_frame &&
        LNK_S.sync_set_frame - LNK_S.sync_get_frame < 0x40)
        LNK_S.stat |= 4;   /* may submit */
    return LNK_S.stat;
}

int fslnk_LinkSyncStart(void)
{
    PS2_FRAME(0x10);
    if (linkMode == 3) {   /* same body as the default, no Ether call */
        LNK_S.sync_timeout = 0;
        LNK_S.start = 1;
        LNK_S.first_frame = 1;
        return 1;
    }
    if (linkMode == 0) return fsilk_IlinkSyncStart();
    LNK_S.sync_timeout = 0;
    LNK_S.start = 1;
    LNK_S.first_frame = 1;
    return 1;
}

int fslnk_LinkSendDataSize(void)
{
    PS2_FRAME(0x10);
    if (linkMode == 3) return Ether_SendDataSize();
    if (linkMode == 0) return fsilk_IlinkSendDataSize();
    return LNK_M.param_send_size;
}

int fslnk_LinkRecvDataSize(int bno)
{
    PS2_FRAME(0x10);
    if (linkMode == 3) return Ether_RecvDataSize(bno);
    if (linkMode == 0) return fsilk_IlinkRecvDataSize(bno);
    return LNK_M.param_recv_size[0];   /* bno ignored */
}

int fslnk_LinkRecvData(void *copy_buf, int bno, int size)
{
    PS2_FRAME(0x10);
    if (linkMode == 3) return Ether_RecvData(copy_buf, size, bno);   /* its DWARF order (pBuff, iRecvSize, iID) */
    if (linkMode == 0) return fsilk_IlinkRecvData(copy_buf, bno, size);
    int n = LNK_M.param_recv_size[0];   /* bno ignored */
    if (n < size) return -1;            /* nothing consumed */
    LNK_M.param_recv_size[0] = n - size;
    char *buf = LNK_M.param_recv_buf[0];
    const char *rest = buf + size;
    char *dst = (char *)copy_buf;
    const char *src = buf;
    for (unsigned int c = (unsigned int)size; c != 0; c--) *dst++ = *src++;
    char *d2 = buf;
    for (unsigned int c = (unsigned int)LNK_M.param_recv_size[0]; c != 0; c--) *d2++ = *rest++;   /* shift down */
    return 1;
}

int fslnk_LinkProc(void)
{
    PS2_FRAME(0x10);
    int mode = linkMode;
    if (mode == 3) return Ether_Proc();
    if (mode == 2 || mode == 1) return fslib_MdmProc();
    if (mode == 0) return fsilk_IlinkProc();
    return -1;
}

int fslnk_GetConnectedNum(void)
{
    PS2_FRAME(0x10);
    if (linkMode == 3) return Ether_GetConnectedNum();
    if (linkMode == 0) return fsilk_GetConnectedNum();
    return -1;
}

int fslnk_GetIndex(void)
{
    PS2_FRAME(0x10);
    if (linkMode == 3) return Ether_GetSyncIndex();
    if (linkMode == 0) return fsilk_IlinkSyncIndex();
    return 0;   /* not MyNodeID (unlike LinkSyncIndex) */
}

int fslnk_SetForceConnect(void)
{
    if (LNK_M.force_connect == 0) LNK_M.force_connect = 1;
    return LNK_M.force_connect;
}

int fslnk_GetPlayerNum(void)
{
    return LNK_S.param;   /* the byte SetPlayerNum writes (+0x7e), signed */
}

int fslnk_LinkDestroy(void)
{
    PS2_FRAME(0x10);
    if (linkMode == 3) return Ether_Term();
    if (linkMode == 0) return fsilk_IlinkDestroy();
    return 0;
}
