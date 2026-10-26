/* src/main/loop.cpp - main-loop helpers. Native: DF_LOOP_BlackMask (blanks both display buffers to black with the GS
 * tests set to "always pass", used at the start / end of a sortie and at boot) and DF_LOOP_PadWait (boot: 25 black
 * frames while the pad library settles) and LOOP_LinkError (the link error screen: the quit message window over a
 * black screen for 251 frames, then black; reached only from a broken i.LINK / modem match or sync_link). */
#include "acnb/sdk.h"

void DF_LOOP_BlackMask(void)
{
    PS2_FRAME(0x30);   /* the 9th DF_SetTestRegister argument goes to sp+0 */
    DF_SetTestRegister(0, 0, 0xff, 1, 0, 0, 1, 1, 1);
    DF_SetTestRegister(0, 0, 0xff, 1, 0, 0, 1, 1, 2);
    DF_DrawColorScreen_NoSemi(0, 0, 0, 0x80);
    sceGsSyncPath(0, 0);
    for (int i = 0; i < 2; i++) {
        Draw_FlipBufferTearing();
        FlushCache(0);
        DF_DrawColorScreen_NoSemi(0, 0, 0, 0x80);
        ps2_dma_wait_gif();
        sceGsSyncPath(0, 0);
        FlushCache(0);
    }
}

/* __InitPs2: 25 black frames, polling the pads each frame */
void DF_LOOP_PadWait(void)
{
    PS2_FRAME(0x30);   /* the 9th DF_SetTestRegister argument goes to sp+0 */
    DF_SetTestRegister(0, 0, 0xff, 1, 0, 0, 1, 1, 1);
    DF_SetTestRegister(0, 0, 0xff, 1, 0, 0, 1, 1, 2);
    DF_DrawColorScreen_NoSemi(0, 0, 0, 0x80);
    sceGsSyncPath(0, 0);
    for (int i = 0; i < 0x19; i++) {
        Draw_FlipBufferTearing();
        Pad_UpdateData();
        FlushCache(0);
        DF_DrawColorScreen_NoSemi(0, 0, 0, 0x80);
        ps2_dma_wait_gif();
        sceGsSyncPath(0, 0);
        FlushCache(0);
    }
}

/* link error: show message mesid for 251 frames (frame 0..250), then black both buffers */
void LOOP_LinkError(int mesid)
{
    PS2_FRAME(0x40);   /* the 9th DF_SetTestRegister argument goes to sp+0 */
    Draw_SetupFrameBuffer(1, 0);
    GSX_SetWindowSize(0x100, 0xe0, 4, 3);
    MsgWnd_ClearQuit();
    MsgWnd_ShowQuit(mesid);
    for (int frame = 0;; frame++) {
        Draw_FlipBufferTearing();
        DF_KickPreDraw();
        sceGsSyncPath(0, 0);
        DF_SetTestRegister(0, 0, 0xff, 0, 0, 0, 1, 1, 1);
        DF_SetTestRegister(0, 0, 0xff, 0, 0, 0, 1, 1, 2);
        sceGsSyncPath(0, 0);
        DF_DrawColorScreen_NoSemi(0, 0, 0, 0x80);
        sceGsSyncPath(0, 0);
        MsgWnd_DrawQuit();
        sceGsSyncPath(0, 0);
        if (frame >= 0xfa) break;
    }
    DF_LOOP_BlackMask();
    Draw_SetupFrameBuffer(0, 0);
}
