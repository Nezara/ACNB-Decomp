/* src/main/synclnk.cpp - sync_link: the "wait until every console is in sync" step at sortie start. For an i.LINK
 * (modeSortie 4) or modem (5) match with at least two consoles connected it (re)starts the link, sends a 128-byte
 * handshake string, waits for every other console's, compares, then starts the sync clock, showing the now-loading
 * screen meanwhile; a failure shows the link error. Every other mode (all local play) only switches the now-loading
 * mode and returns 0. Also terminate_link (main loop: the link shutdown after a link error). The network arm runs
 * only in a linked source port (host link, port/platform/net); in the hybrid it is asm-verified only. */
#include "acnb/sdk.h"
#include <string.h>

/* the two handshake buffers' initial contents (main rodata 0x25fbb0 / 0x25fc30, copied onto the stack) */
static const char s_syncSendInit[128] = "ZZZYYYXXXWWWVVVUUU";
static const char s_syncRecvInit[128] = "010101010101010101";

/* stop and shut the link down (main loop, after a link error); an i.LINK match also leaves the link menu */
void terminate_link(void)
{
    PS2_FRAME(0x10);
    fslnk_LinkSyncStop();
    fslnk_LinkSyncTerm();
    fslnk_LinkStop();
    fslnk_LinkTerm();
    if (g_sortie.pconstant->modeSortie == 4) fslnk_LinkDestroy();
}

static int sync_link_fail(int err, int saved)
{
    fslnk_LinkSyncStop();
    fslnk_LinkSyncTerm();
    fslnk_LinkStop();
    fslnk_LinkTerm();
    if (g_sortie.pconstant->modeSortie == 4) fslnk_LinkDestroy();
    LOOP_LinkError(err);
    NL_SetMode(saved);
    return -1;
}

/* 0 = in sync (or no link match), -1 = link error.
 * Quirks (kept): a modem match ignores a failed first wait and still sends; its LinkSyncInit size argument is the
 * constant 4 left in a register; a negative LinkRecvDataSize counts as a full packet (unsigned compare); every packet
 * received overwrites the same buffer, so only the last one is compared. */
int sync_link(int dispmode)
{
    PS2_FRAME(0x190);
    PS2_STACK(char[128], recvbuf, 0x90);    /* read / written by the interpreted fslnk_* calls */
    PS2_STACK(char[128], sendbuf, 0x110);
    int err = 2, ok = 1;
    int nlmode = dispmode == 1 ? 2 : 0;
    int saved = NL_GetMode();
    NL_SetMode(nlmode & 0xff);
    unsigned char mode = g_sortie.pconstant->modeSortie;
    if ((mode != 4 && mode != 5) || MyI.connected < 2) {
        NL_SetMode(saved);
        return 0;
    }
    int div = play_info.play_mode != 0 ? 1 : 2;
    int size = 8 / div;
    int player = play_info.connected_num - play_info.live_monitor;
    if (mode == 4) {
        fslnk_LinkSyncTerm();
        fslnk_LinkStop();
        fslnk_SetCheckAlive(0);
        fslnk_LinkStart();
    }
    memcpy(sendbuf, s_syncSendInit, 128);
    memcpy(recvbuf, s_syncRecvInit, 128);
    DF_DrawColorScreen_NoSemi(0, 0, 0, 0x80);
    int wait = 0x960;
    for (;;) {   /* link up */
        Draw_FlipBufferTearing();
        int r = fslnk_LinkProc();
        if (r == 3) break;
        if (r < 0) {
            ok = 0;
            if (r == -0x10) err = 4;
            break;
        }
        if (wait-- <= 0) {
            ok = 0;
            break;
        }
        DF_KickPreDraw();
        sceGsSyncPath(0, 0);
        DF_DrawColorScreen_NoSemi(0, 0, 0, 0x80);
        sceGsSyncPath(0, 0);
        NL_DrawNowLoading();
        sceGsSyncPath(0, 0);
    }
    if (g_sortie.pconstant->modeSortie == 4) {
        if (!ok) return sync_link_fail(err, saved);
        fslnk_LinkSyncInit((char)player, (short)size, 0x3c, 0, 2);
        fslnk_SetLiveMonitor(play_info.live_monitor);
    }
    fslnk_LinkSendData(sendbuf, 0x80);
    DF_DrawColorScreen_NoSemi(0, 0, 0, 0x80);
    wait = 0x960;
    for (;;) {   /* exchange the handshake */
        Draw_FlipBufferTearing();
        int r = fslnk_LinkProc();
        if (r < 0) {
            ok = 0;
            if (r == -0x10) err = 4;
            break;
        }
        if (wait-- <= 0) {
            ok = 0;
            break;
        }
        int cnt = play_info.connected_num - 1, n = 0;
        for (int b = 0; b < cnt; b++) {
            if ((unsigned int)fslnk_LinkRecvDataSize(b) >= 0x80) n++;
            cnt = play_info.connected_num - 1;
        }
        if (n == cnt && fslnk_LinkSendDataSize() <= 0) {
            for (int b = 0; b < play_info.connected_num - 1; b++) fslnk_LinkRecvData(recvbuf, b, 0x80);
            break;
        }
        DF_DrawColorScreen_NoSemi(0, 0, 0, 0x80);
        sceGsSyncPath(0, 0);
        DF_KickPreDraw();
        sceGsSyncPath(0, 0);
        NL_DrawNowLoading();
        sceGsSyncPath(0, 0);
    }
    if (!ok) return sync_link_fail(err, saved);
    if (strcmp(sendbuf, recvbuf) != 0) ok = 0;
    if (!ok) return sync_link_fail(err, saved);
    if (g_sortie.pconstant->modeSortie == 4) {
        fslnk_LinkSyncInit((char)player, (short)size, 0x3c, 0, 2);
        fslnk_SetLiveMonitor(play_info.live_monitor);
    } else {
        fslnk_LinkSyncInit(2, 4, 0x3c, 0, 2);
    }
    if (fslnk_LinkSyncStart() == -1) ok = 0;
    if (!ok) return sync_link_fail(err, saved);
    DF_LOOP_BlackMask();
    NL_SetMode(saved);
    return 0;
}
