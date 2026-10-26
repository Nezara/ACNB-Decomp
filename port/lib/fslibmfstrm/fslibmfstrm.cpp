/* lib/fslibmfstrm/fslibmfstrm.cpp - EE client of the IOP module FSMFSTRM (multi-stream file reader behind the IVAG BGM /
 * voice tracks; whole file). One SIF client (s_scdRpc) bound to IOP server SID 0x4010 and one shared send / receive
 * buffer g_ucEtoI_RpcBuff: _CallRpc waits for the previous call (bounded sceSifCheckStatRpc spin), rounds the send /
 * receive sizes up to 16 (clamped to 0x100), calls the bare command fno blocking (iIsBlock != 0) or NOWAIT and returns
 * word 0 of the reply. The fsmfstrm_* wrappers are the one-line public API sound/multistm.cpp calls; the stream table
 * lives in mstream.cpp. The IOP -> EE direction (end-of-read / read-error notifications, never seen in any run) is a
 * second EE thread serving SID 0x4020 (_ItoE_RpcThread -> _ItoE_RpcDispatch -> mstrm_EndReading / CheckReadError).
 *
 * Hybrid: the SDK calls are acnb/sdk.h thunks into the HLE-hooked originals (hle.c); _ItoE_RpcThread is a native kernel
 * thread entry (runtime/kernel.c fiber, as lib/fs/fslibsd.cpp IopToEeRpcThread) that blocks for good in sceSifRpcLoop.
 * fsmfstrm_CallRpc really takes 10 int arguments (cmd, block, w0..w7; protofix.py): the payload words callers do not
 * name are register / stack residue in the original; MIPS callers (stream.c) keep theirs exactly through the hand
 * bw__ (a2..t3 + the caller's sp + 0 / + 8), native callers pass 0. The ssize-0x10 struct sends carry the previous IOP
 * reply in buffer words 1-3: the native writes the same words of the same DWARF buffer, so those stay byte-identical.
 * Quirks kept: 10,000,001 polls before the timeout message (dead: CheckStatRpc returns 0); CallRpcStruct clamps (with
 * the message) already at iSend == 0x100 and only copies when pStruct != NULL; _CreateRpcThread treats tid 0 as a
 * failure; SetCdRMode reads *pRMode before its NULL check, keeps a dead copy at sp+0x1c and sends the EE index (not
 * the IOP index); fsmfstrm_Quit deletes the thread / RPC server before mstrm_Quit; the BindRpc retry is a pure busy
 * delay (10000 empty iterations). Debug printfs are all kept. */
#include "acnb/port.h"
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/local/lib/fslibmfstrm/fslibmfstrm.h"

#ifdef ACNB_SOURCEPORT
int s_iRpcThrdID = -1;
char fsmfstrm_msg_bind[44] = "!!!!!!!! sceSifBindRpc is failed. !!!!!!!!\n";
char fsmfstrm_msg_thread[30] = "Starting RPC thread on EE...\n";
char fsmfstrm_msg_create_thread[63] = "_CreateRpcThread : ERROR!!! CreateThread is failed. - id = %d\n";
char fsmfstrm_msg_sync_wait[51] = "CallRpcSyncWait : ERROR sceSifCheckStatRpc() == 1\n";
char fsmfstrm_msg_send_over[58] = "CallRpc : ERROR!!! RPC send size is over. iSend=%d -> %d\n";
char fsmfstrm_msg_recv_over[61] = "CallRpc : ERROR!!! RPC receive size is over. iRecv=%d -> %d\n";
char fsmfstrm_msg_struct_over[72] = "fsmfstrm_CallRpcStruct : ERROR!!! RPC send size is over.iSend=%d -> %d\n";
char fsmfstrm_msg_alloc_info[68] = "fsmfstrm_AllocStream : ERROR!!! can not find stream information...\n";
char fsmfstrm_msg_alloc_iop[64] = "fsmfstrm_AllocStream : ERROR!!! can not allocate IOP memory...\n";
#endif

#define MFSTRM_BUF g_ucEtoI_RpcBuff

/* bind to SID 0x4010; hle.c's BindRpc runs the IOP until the server exists, so the retry delay is never taken */
static int _EtoI_BindRpc(void)
{
    PS2_FRAME(0x20);
    for (;;) {
        if (sceSifBindRpc(&s_scdRpc, 0x4010, 0) < 0) ps2_printf(fsmfstrm_msg_bind);
        if (*(void *volatile *)&s_scdRpc.serve != nullptr) return 1;
        for (int n = 0; n < 10000; n++) {   /* busy delay (no effect natively) */
        }
    }
}

/* the 0x4020 server function: IOP -> EE notifications (fno has the module nibble 1); the reply buffer is never written */
static void *_ItoE_RpcDispatch(unsigned int fno, void *data)
{
    PS2_FRAME(0x50);
    if (fno == 0x10000200u)
        mstrm_CheckReadError(((int *)data)[0], ((int *)data)[1]);
    else if (fno == 0x10000100u)
        mstrm_EndReading(((int *)data)[0]);
    return g_ucRet_RpcBuff;
}

static void _ItoE_RpcThread(void)
{
    PS2_FRAME(0x20);
    ps2_printf(fsmfstrm_msg_thread);
    sceSifInitRpc(0);
    sceSifSetRpcQueue(&s_qdIopToEeRpc, ps2_GetThreadId());
    sceSifRegisterRpc(&s_sdIopToEeRpc, 0x4020, (void *(*)(unsigned int, void *, int))GFN(_ItoE_RpcDispatch),
                      g_ucItoE_RpcBuff, nullptr, nullptr, &s_qdIopToEeRpc);
    sceSifRpcLoop(&s_qdIopToEeRpc);   /* never returns */
}

static int _CreateRpcThread(void)
{
    PS2_FRAME(0x40);
    /* status / currentPriority / attr / option / wait words are never written: the kernel reads the stale stack */
    PS2_STACK(ThreadParam, param, 0x10);
    param.entry = (void (*)(void *))GFN(_ItoE_RpcThread);
    param.initPriority = 0x28;
    param.stackSize = 0x2000;
    param.stack = s_stack;
    param.gpReg = &_gp;
    s_iRpcThrdID = ps2_CreateThread(&param);
    if (s_iRpcThrdID > 0) {   /* blez: tid 0 counts as a failure */
        ps2_StartThread(s_iRpcThrdID, nullptr);
        return 1;
    }
    ps2_printf_d(fsmfstrm_msg_create_thread, s_iRpcThrdID);
    return 0;
}

/* poll until the previous call is done: 0 (after the message) once 10,000,001 polls returned busy, else 1 */
static int _CallRpcSyncWait(void)
{
    PS2_FRAME(0x20);
    for (int n = 10000000; sceSifCheckStatRpc(&s_scdRpc) == 1; n--) {
        if (n < 0) {
            ps2_printf(fsmfstrm_msg_sync_wait);
            return 0;
        }
    }
    return 1;
}

static int _CallRpc(int iCmd, int iIsBlock, int iSend, int iRecv)
{
    PS2_FRAME(0x80);
    unsigned int mode = iIsBlock == 0;   /* 1 = SIF_RPCM_NOWAIT */
    iSend = (iSend + 15) & -16;
    if (iSend > 0x100) {
        ps2_printf_dd(fsmfstrm_msg_send_over, iSend, 0x100);
        iSend = 0x100;
    }
    iRecv = (iRecv + 15) & -16;
    if (iRecv > 0x100) {
        ps2_printf_dd(fsmfstrm_msg_recv_over, iRecv, 0x100);
        iRecv = 0x100;
    }
    if (!_CallRpcSyncWait()) return -1;
    FlushCache(0);
    sceSifCallRpc(&s_scdRpc, (unsigned int)iCmd, mode, MFSTRM_BUF, iSend, MFSTRM_BUF, iRecv, nullptr, nullptr);
    FlushCache(0);
    return *(int *)MFSTRM_BUF;   /* read after the call also for NOWAIT (hle: already filled) */
}

int fsmfstrm_CallRpcStruct(int iCmd, int iIsBlock, int iSend, int iRecv, void *pStruct)
{
    PS2_FRAME(0x70);
    if (pStruct) {
        if (iSend >= 0x100) {
            ps2_printf_dd(fsmfstrm_msg_struct_over, iSend, 0x100);
            iSend = 0x100;
        }
        const unsigned char *s = (const unsigned char *)pStruct;
        for (int i = 0; i < iSend; i++) MFSTRM_BUF[i] = s[i];
    }
    return _CallRpc(iCmd, iIsBlock, iSend, iRecv);
}

/* 8 payload words (ssize 0x20, rsize 4 -> 0x10) */
int fsmfstrm_CallRpc(int iCmd, int iIsBlock, int w0, int w1, int w2, int w3, int w4, int w5, int w6, int w7)
{
    PS2_FRAME(0xa0);
#ifndef ACNB_SOURCEPORT
    {
        /* the prologue's argument spills + varargs home area: a0 0x50, a1 0x60, a2..t3 (sd) 0x70..0x98 */
        PS2_STACK(int, home_cmd, 0x50);
        PS2_STACK(int, home_block, 0x60);
        PS2_STACK(long long, home_w0, 0x70);
        PS2_STACK(long long, home_w1, 0x78);
        PS2_STACK(long long, home_w2, 0x80);
        PS2_STACK(long long, home_w3, 0x88);
        PS2_STACK(long long, home_w4, 0x90);
        PS2_STACK(long long, home_w5, 0x98);
        home_cmd = iCmd;
        home_block = iIsBlock;
        home_w0 = w0;
        home_w1 = w1;
        home_w2 = w2;
        home_w3 = w3;
        home_w4 = w4;
        home_w5 = w5;
    }
#endif
    int *b = (int *)MFSTRM_BUF;
    b[0] = w0;
    b[1] = w1;
    b[2] = w2;
    b[3] = w3;
    b[4] = w4;
    b[5] = w5;
    b[6] = w6;
    b[7] = w7;
    return _CallRpc(iCmd, iIsBlock, 0x20, 4);
}

#ifndef ACNB_SOURCEPORT
/* MIPS callers (stream.c strm_Close / strm_SetReloadSize, interpreted): 10 int arguments, w6 / w7 from the caller's
 * stack. Every register is read before the native runs (its bridge calls clobber a0..t3). */
static void bw__fsmfstrm_CallRpc(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    int r = fsmfstrm_CallRpc(c->gpr[A0].sw[0], c->gpr[A1].sw[0], c->gpr[A2].sw[0], c->gpr[A3].sw[0], c->gpr[T0].sw[0],
                             c->gpr[T1].sw[0], c->gpr[T2].sw[0], c->gpr[T3].sw[0], (int)rd32(sp), (int)rd32(sp + 8));
    c->gpr[V0].sd[0] = r;
}
#endif

int fsmfstrm_CallRpcVoid(int iCmd, int iIsBlock)
{
    PS2_FRAME(0x30);
    return _CallRpc(iCmd, iIsBlock, 0, 0);
}

void fsmfstrm_Init(void)
{
    PS2_FRAME(0x10);
    if (s_iInited != 0) return;
    _EtoI_BindRpc();
    _CallRpc(0x100, 1, 0, 0);
    _CreateRpcThread();
    _CallRpc(0x300, 1, 0, 0);
    mstrm_Init();
    s_iInited = 1;
}

void fsmfstrm_Quit(void)
{
    PS2_FRAME(0x10);
    if (s_iRpcThrdID >= 0) {
        ps2_TerminateThread(s_iRpcThrdID);
        ps2_DeleteThread(s_iRpcThrdID);
        sceSifRemoveRpc(&s_sdIopToEeRpc, &s_qdIopToEeRpc);
        sceSifRemoveRpcQueue(&s_qdIopToEeRpc);
        s_iRpcThrdID = -1;
    }
    mstrm_Quit();
    _CallRpc(0x200, 1, 0, 0);
    s_iInited = 0;
}

void fsmfstrm_DmaInit(void)
{
    PS2_FRAME(0x10);
    fsmfstrm_CallRpcVoid(0x400, 1);
}

void fsmfstrm_DmaQuit(void)
{
    PS2_FRAME(0x10);
    fsmfstrm_CallRpcVoid(0x500, 1);
}

int fsmfstrm_AllocDmaBuffer(int iSize)
{
    PS2_FRAME(0x20);
    PS2_STACK(int, size, 0x10);   /* the argument's spill slot, byte-copied into the send buffer */
    size = iSize;
    return fsmfstrm_CallRpcStruct(0x600, 1, 4, 4, &size);
}

int fsmfstrm_AllocStream(int iSize)
{
    PS2_FRAME(0x40);
    int idx = mstrm_CreateStream();
    if (idx < 0) {
        ps2_printf(fsmfstrm_msg_alloc_info);
        return idx;
    }
    if (mstrm_AllocIopStream(idx, iSize) == 0) {
        ps2_printf(fsmfstrm_msg_alloc_iop);
        mstrm_DeleteStream(idx);
        return -1;
    }
    return idx;
}

int fsmfstrm_AttachSector(int index, int lsn, int size, unsigned short prio)
{
    PS2_FRAME(0x50);
    return mstrm_AttachSector(index, lsn, size, prio);
}

int fsmfstrm_DetachFileSector(int index)
{
    PS2_FRAME(0x20);
    return mstrm_DetachFileSector(index);
}

/* the IOP's remaining read size (time-dependent on the IOP clock): one 0x9000 RPC per call, never more */
int fsmfstrm_GetIopReadSizeF(int index)
{
    PS2_FRAME(0x30);
    PS2_STACK(int, iop, 0x1c);
    iop = mstrm_GetIopIndex(index);
    if (iop < 0) return -1;
    return fsmfstrm_CallRpcStruct(0x9000, 1, 4, 4, &iop);
}

int fsmfstrm_SetReloadSize(int index, int iSize)
{
    PS2_FRAME(0x30);
    return mstrm_SetReloadSize(index, iSize);
}

/* NOWAIT (block 0); payload words 1..7 are the caller's residue in the original (0 here) */
void fsmfstrm_SetReadPacketSize(int iSize)
{
    PS2_FRAME(0x20);
    fsmfstrm_CallRpc(0x11000, 0, iSize, 0, 0, 0, 0, 0, 0, 0);
}

int fsmfstrm_SetCdRMode(int index, struct_f853a5d6 *pRMode)
{
    PS2_FRAME(0x40);
    PS2_STACK(struct_5383eaa5, loc, 0x10);
    PS2_STACK(struct_f853a5d6, dead, 0x1c);   /* dead second copy of the mode */
    unsigned char *z = (unsigned char *)&loc;
    for (int n = 0; n < 8; n++) z[n] = 0;
    loc.index = index;
    loc.mode = *pRMode;   /* read before the NULL check below */
    dead = loc.mode;
    if (index < 0 || index >= 8) return 0;
    if (!pRMode) return 0;
    return fsmfstrm_CallRpcStruct(0xf000, 1, 8, 4, &loc);   /* the EE index, not mstrm_GetIopIndex(index) */
}

int fsmfstrm_GetIopIndex(int index)
{
    PS2_FRAME(0x20);
    return mstrm_GetIopIndex(index);
}
