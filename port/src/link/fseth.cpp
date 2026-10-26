/* src/link/fseth.cpp - Ethernet (network adaptor, dev9) link layer; only the power-off cluster is native so far.
 * fseth_PreparePowerOff (Iop_Init, after the IOP modules are loaded with dev9) creates a semaphore (max 1, init 0) and
 * the 8 KB power-off thread (priority 2, arg = the sema id) and registers __fseth_PowerOffHandler as the libcdvd
 * power-off callback: on a power-button event the handler signals the sema from interrupt context and the thread
 * shuts the network down (interpreted; it never returns). fseth_RemovePowerOff (Iop_Init, before the IOP reboot)
 * undoes it: unregister the callback, terminate + delete the thread, delete the sema, ids back to -1 (the statics'
 * initial value, so the boot call does nothing). Prepare does nothing while either id is set.
 * Original quirks kept: the kernel calls are checked by "return value != id" (TerminateThread / DeleteThread /
 * DeleteSema return the id on success), the failures only print, and the ids are reset either way; the Delete
 * Thread message gets a stale third argument (the line number of the "%s(%04d)" print before it); Prepare checks no
 * result. __fseth_PowerOffThread (0x1c6bc0) blocks in WaitSema at boot (priority 2, so it runs at once); the rest of
 * its body is the Sony power-off sample (close pfs files, dev9 off, CD power off, spin) and is unreached.
 * The rest of fseth.c (partners, send / receive threads, ring buffers, libnet setup) is the later
 * i.LINK / network step and stays interpreted / stubbed.
 * Hybrid: the SemaParam / ThreadParam blocks are PS2_STACK at the original offsets (memset by the original). */
#include "acnb/port.h"
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
int s_isOk = 1;
int s_iPowerOffThreadID = -1;
int s_iPowerOffSemaID = -1;
unsigned char s_stackPowerOff[8192];
char fseth_msg_where[11] = "%s(%04d): ";
char fseth_msg_terminate[65] = "***** FSETH : failed Terminate Thread : return %d : ID %d *****\n";
char fseth_msg_delthread[54] = "***** FSETH : failed Delete Thread : return %d *****\n";
char fseth_msg_delsema[57] = "***** FSETH : failed Delete Semaphore : return %d *****\n";
char fseth_file[8] = "fseth.c";
char fseth_msg_downif[59] = "***** FSETH : sceInetCtlDownInterface() failed.(%d) *****\n";
char fseth_msg_noupif[60] = "***** FSETH : sceInetCtlUpInterface is not executed. *****\n";
char fseth_dev_pfs[5] = "pfs:";
char fseth_dev_dev9x[7] = "dev9x:";
#endif

#define FSETH_POWEROFF_PRIORITY 2

void fseth_RemovePowerOff(void)
{
    PS2_FRAME(0x20);
    int ret;
    if (s_iPowerOffThreadID != -1) {
        sceCdPOffCallback(0, 0);
        ret = ps2_TerminateThread(s_iPowerOffThreadID);
        if (ret != s_iPowerOffThreadID) {
            ps2_scePrintf_sd(fseth_msg_where, fseth_file, 0xccd);
            ps2_scePrintf_dd(fseth_msg_terminate, ret, s_iPowerOffThreadID);
        }
        ret = ps2_DeleteThread(s_iPowerOffThreadID);
        if (ret != s_iPowerOffThreadID) {
            ps2_scePrintf_sd(fseth_msg_where, fseth_file, 0xcd4);
            ps2_scePrintf_dd(fseth_msg_delthread, ret, 0xcd4);   /* a2 still holds the line number */
        }
        s_iPowerOffThreadID = -1;
    }
    if (s_iPowerOffSemaID != -1) {
        ret = ps2_DeleteSema(s_iPowerOffSemaID);
        if (ret != s_iPowerOffSemaID) {
            ps2_scePrintf_sd(fseth_msg_where, fseth_file, 0xce0);
            ps2_scePrintf_dd(fseth_msg_delsema, ret, 0xce0);
        }
        s_iPowerOffSemaID = -1;
    }
}

void fseth_PreparePowerOff(void)
{
    PS2_FRAME(0x60);
    if (s_iPowerOffThreadID != -1 || s_iPowerOffSemaID != -1) return;
    PS2_STACK(struct ThreadParam, thread, 0x30);
    PS2_STACK(struct SemaParam, sema, 0x10);
    ps2_memset(&thread, 0, sizeof(struct ThreadParam));
    ps2_memset(&sema, 0, sizeof(struct SemaParam));
    sema.maxCount = 1;
    sema.initCount = 0;
    sema.option = 0;
    s_iPowerOffSemaID = ps2_CreateSema(&sema);
    thread.stackSize = sizeof(s_stackPowerOff);
    thread.gpReg = &_gp;
    thread.entry = GFN(__fseth_PowerOffThread);
    thread.stack = s_stackPowerOff;
    thread.initPriority = FSETH_POWEROFF_PRIORITY;
    s_iPowerOffThreadID = ps2_CreateThread(&thread);
    ps2_StartThread(s_iPowerOffThreadID, (void *)(uintptr_t)s_iPowerOffSemaID);
    sceCdPOffCallback(GFN(__fseth_PowerOffHandler), (void *)(uintptr_t)s_iPowerOffSemaID);
}

/* libcdvd power-off callback (interrupt context): wake __fseth_PowerOffThread */
void __fseth_PowerOffHandler(void *arg)
{
    ps2_iSignalSema((int)PS2PTR(arg));   /* the sema id, passed as the callback argument */
}

/* power-off thread (entry of fseth_PreparePowerOff's thread, arg = the sema id). Unreached past the WaitSema. */
void __fseth_PowerOffThread(void *arg)
{
    PS2_FRAME(0x40);
    PS2_STACK(int, stat, 0x3c);
    int ret;
    ps2_WaitSema((int)(uintptr_t)arg);
    sceCdPOffCallback(0, 0);
    s_isOk = 0;
    fsLibnetVoidSendRecv();
    __fseth_RemoveThread(&s_stThreadRecv);
    __fseth_RemoveThread(&s_stThreadSend);
    sceCdAutoAdjustCtrl(1, (unsigned int *)&stat);
    s_ether.target[0].result = 0;   /* 0x1708 */
    if (s_ether.flagNeedTerm[5]) {  /* 0x17b9 */
        ret = fsLibnetCtlDownInterfaceForAbort(0);
        if (ret < 0) {
            ps2_scePrintf_sd(fseth_msg_where, fseth_file, 0x5e5);
            ps2_scePrintf_dd(fseth_msg_downif, ret, 0x5e5);   /* one %d; a2 is not set by the original (extra arg) */
        }
        s_ether.flagNeedTerm[5] = 0;
    } else {
        ps2_scePrintf_sd(fseth_msg_where, fseth_file, 0x5ed);
        ps2_scePrintf(fseth_msg_noupif);
    }
    sceDevctl(fseth_dev_pfs, 0x5003, 0, 0, 0, 0);              /* PDIOC_CLOSEALL */
    while (sceDevctl(fseth_dev_dev9x, 0x4402, 0, 0, 0, 0) < 0) /* DDIOC_OFF */
        ;
    sceCdAutoAdjustCtrl(1, (unsigned int *)&stat);
    do {
        while (!sceCdPowerOff(&stat))
            ;
    } while (stat);
    for (;;) {                                                  /* 0x1c6cf8: nop spin until the power goes */
        __asm__ volatile("");
    }
}
