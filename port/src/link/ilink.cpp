/* src/link/ilink.cpp - i.LINK (IEEE1394) link back end (fsilk_*, sceSocket RPC to the iLINK IRX set) and the EE
 * timer 1 helpers the link code shares. Only clear_timer1 is native so far (reached offline through liblnk's
 * fslnk_LinkStop / LinkStart); the rest of ilink.c (fsilk_*, receiver threads, init_timer1 / get_timer1 /
 * timer1_handler) is the later network step and stays interpreted / stubbed.
 * Source port only: the 26 fsilk_* entries the game reaches (liblnk linkMode 0, mmanage, loader) forward to hook_ilink
 * (acnb/hooks.h; the host link back end, port/platform/net) and report "not decompiled" + exit 70 like the sp_missing
 * stub while the hook is NULL. The original bodies replace the fallback once ilink.c is ported. The entries are made by
 * a macro so gen_bridge (which matches `name(..) {`) does not count them as hybrid natives.
 * clear_timer1 zeroes the timer 1 interrupt count and T1_COUNT and returns 1 (get_timer1 = COUNT + cnt * 60000). */
#include "acnb/port.h"
#ifdef ACNB_SOURCEPORT
#include "acnb/hooks.h"
#include "../../../sourceport/main/sp_missing.h"
#endif

int clear_timer1(void)
{
    timer1_intr_cnt = 0;
    ps2_timer1_set_count(0);
    return 1;
}

#ifdef ACNB_SOURCEPORT
static const SpMissing ilink_unported[] = {
    {"fsilk_IlinkInit", "src/link/ilink"}, {"fsilk_IlinkTerm", "src/link/ilink"},
    {"fsilk_IlinkDestroy", "src/link/ilink"}, {"fsilk_IlinkReset", "src/link/ilink"},
    {"fsilk_IlinkStart", "src/link/ilink"}, {"fsilk_IlinkStop", "src/link/ilink"},
    {"fsilk_IlinkSetCheckAlive", "src/link/ilink"}, {"fsilk_IlinkProc", "src/link/ilink"},
    {"fsilk_IlinkCheckConnect", "src/link/ilink"}, {"fsilk_GetConnectedNum", "src/link/ilink"},
    {"fsilk_IlinkSyncIndex", "src/link/ilink"}, {"fsilk_IlinkSyncInit", "src/link/ilink"},
    {"fsilk_IlinkSyncStart", "src/link/ilink"}, {"fsilk_IlinkSyncStop", "src/link/ilink"},
    {"fsilk_IlinkSyncTerm", "src/link/ilink"}, {"fsilk_IlinkSyncProc", "src/link/ilink"},
    {"fsilk_IlinkSyncSetData", "src/link/ilink"}, {"fsilk_IlinkSyncGetData", "src/link/ilink"},
    {"fsilk_IlinkSendData", "src/link/ilink"}, {"fsilk_IlinkSendDataSize", "src/link/ilink"},
    {"fsilk_IlinkRecvDataSize", "src/link/ilink"}, {"fsilk_IlinkRecvData", "src/link/ilink"},
    {"fsilk_socketSendDataL", "src/link/ilink"}, {"fsilk_socketRecvDataL", "src/link/ilink"},
    {"fsilk_GetStatSendDataL", "src/link/ilink"}, {"fsilk_GetStatRecvDataL", "src/link/ilink"},
};
/* entry n: hook set -> the back end, else the unported report (sp_missing, exit 70) */
#define ILK_ENTRY(n, ret, name, params, args)     ret fsilk_##name params { if (hook_ilink) return hook_ilink->name args; sp_missing(&ilink_unported[n]); }
ILK_ENTRY(0, int, IlinkInit, (void), ())
ILK_ENTRY(1, int, IlinkTerm, (void), ())
ILK_ENTRY(2, int, IlinkDestroy, (void), ())
ILK_ENTRY(3, int, IlinkReset, (void), ())
ILK_ENTRY(4, int, IlinkStart, (void), ())
ILK_ENTRY(5, int, IlinkStop, (void), ())
ILK_ENTRY(6, int, IlinkSetCheckAlive, (int mode), (mode))
ILK_ENTRY(7, int, IlinkProc, (void), ())
ILK_ENTRY(8, int, IlinkCheckConnect, (int block, int connect_no), (block, connect_no))
ILK_ENTRY(9, int, GetConnectedNum, (void), ())
ILK_ENTRY(10, int, IlinkSyncIndex, (void), ())
ILK_ENTRY(11, int, IlinkSyncInit, (char player, short size, char fps, char dup), (player, size, fps, dup))
ILK_ENTRY(12, int, IlinkSyncStart, (void), ())
ILK_ENTRY(13, int, IlinkSyncStop, (void), ())
ILK_ENTRY(14, int, IlinkSyncTerm, (void), ())
ILK_ENTRY(15, int, IlinkSyncProc, (void), ())
ILK_ENTRY(16, int, IlinkSyncSetData, (void *sync_data), (sync_data))
ILK_ENTRY(17, int, IlinkSyncGetData, (void *sync_data), (sync_data))
ILK_ENTRY(18, int, IlinkSendData, (void *send_data, int size), (send_data, size))
ILK_ENTRY(19, int, IlinkSendDataSize, (void), ())
ILK_ENTRY(20, int, IlinkRecvDataSize, (int bno), (bno))
ILK_ENTRY(21, int, IlinkRecvData, (void *copy_buf, int bno, int size), (copy_buf, bno, size))
ILK_ENTRY(22, int, socketSendDataL, (char *buf, int size), (buf, size))
ILK_ENTRY(23, int, socketRecvDataL, (char *buf, int size), (buf, size))
ILK_ENTRY(24, unsigned int, GetStatSendDataL, (void), ())
ILK_ENTRY(25, unsigned int, GetStatRecvDataL, (void), ())
#undef ILK_ENTRY
#endif
