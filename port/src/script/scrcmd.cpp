/* src/script/scrcmd.cpp - "cmd" command group of the stage scripts: start / stop / delete event manager commands.
 * SCRCMD_CallScript (a 40-byte jump table trampoline) dispatches through the original table (gcall), so the
 * interpreted handlers stay reachable. Native: EventEntry, EventDelete (garage3); the other 7 handlers stay
 * interpreted. */
#include "acnb/ps2fp.h"

/* Slot 3: starts event command arg0. */
static void __scrcmd_EventEntry(__scr_fifo_st *ss)
{
    PS2_FRAME(0x20);
    EVMNG_Entry(ps2_ftoi(ps2_u2f(ss->arg[0].val)));
    ss->kind = 0;
}

/* Slot 2: deletes event command arg0. */
static void __scrcmd_EventDelete(__scr_fifo_st *ss)
{
    PS2_FRAME(0x20);
    EVMNG_Delete(ps2_ftoi(ps2_u2f(ss->arg[0].val)));
    ss->kind = 0;
}

/* The group's dispatcher: handler s_pJobCmdScrFunc[type] (a tail jump in the original; type is not range-checked:
 * 10 / 11 hit the null padding words after the table). */
void SCRCMD_CallScript(unsigned char type, __scr_fifo_st *ss)
{
    gcall(s_pJobCmdScrFunc[type], ss);
}

#ifdef ACNB_SOURCEPORT
/* the original's initialized table */
void (*s_pJobCmdScrFunc[10])(struct __scr_fifo_st *) = {   /* OLSORTIE 0x1e6c460 */
    ScrNoUse, __scrcmd_EventStop, __scrcmd_EventDelete, __scrcmd_EventEntry, __scrcmd_EventReferDelete,
    __scrcmd_EventExec, __scrcmd_EventRandEntry, __scrcmd_PolygExec, __scrcmd_PolygStart,
    __scrcmd_EventMultiRandEntry
};
#endif
