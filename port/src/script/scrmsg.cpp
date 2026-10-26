/* src/script/scrmsg.cpp - "msg" command group of the stage scripts: message window and COM voice messages.
 * SCRMSG_CallScript (a 40-byte jump table trampoline) dispatches through the original table (gcall), so the
 * interpreted handlers stay reachable. Native: PlaybackComMessage (garage3); the other 4 handlers stay interpreted. */
#include "acnb/ps2fp.h"

/* Slot 2: shows COM message arg0 unless the sortie message window is busy. Quirk: when busy the command is still
 * finished, so the message is dropped. */
static void __scrmsg_PlaybackComMessage(__scr_fifo_st *ss)
{
    PS2_FRAME(0x20);
    if (!MsgWnd_IsBusy(1)) MsgWnd_ShowCom((short)ps2_ftoi(ps2_u2f(ss->arg[0].val)), 0);
    ss->kind = 0;
}

/* The group's dispatcher: handler s_pJobMsgScrFunc[type] (a tail jump in the original; type is not range-checked:
 * 6 / 7 hit null padding words). */
void SCRMSG_CallScript(unsigned char type, __scr_fifo_st *ss)
{
    gcall(s_pJobMsgScrFunc[type], ss);
}

#ifdef ACNB_SOURCEPORT
/* the original's initialized table */
void (*s_pJobMsgScrFunc[6])(struct __scr_fifo_st *) = {   /* OLSORTIE 0x1e6c590 */
    ScrNoUse, __scrmsg_PlaybackMessage, __scrmsg_PlaybackComMessage, __scrmsg_PlaybackSSS,
    __scrmsg_PlaybackComMessageEvent, __scrmsg_CancelMessage
};
#endif
