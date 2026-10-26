/* lib/fs/fslibsd.cpp - EE half of the FromSoftware sound RPC transport (whole file). One SIF client (fssd_cd) bound to the
 * IOP FSSD server SID 0x101a and one shared send / receive buffer g_ucEeToIopRpcBuff: every fssdCallRpc* wrapper waits for
 * the previous call (bounded sceSifCheckStatRpc spin), packs its arguments into the buffer, calls fno
 * (module << 28 | cmd) blocking (wait != 0) or not, and returns word 0 of the reply. The IOP -> EE direction is an EE
 * thread (IopToEeRpcThread) serving SID 0x102a; IopToEeRpcDispatch hands each request to the handler fssdRegister put in
 * s_psrcFunc[fno >> 28] (IVAG module 10, EmsVag module 3) and copies its reply into g_ucErrBuf. fssdAllocIopMemory asks
 * the IOP how much is free before allocating on the IOP heap itself.
 *
 * Hybrid: the SDK calls are acnb/sdk.h thunks into the HLE-hooked originals (hle.c); IopToEeRpcThread is a native kernel
 * thread entry (runtime/kernel.c fiber, as main/thretask.cpp __TaskThread) that blocks for good in sceSifRpcLoop.
 * fssdCallRpc really takes 11 int arguments (module, wait, cmd, w0..w7; protofix.py): the payload words callers do not
 * name are register / stack residue in the original. MIPS callers keep theirs exactly (hand bw__ wrappers read a3..t3 and
 * the caller's sp + 0 / 8 / 0x10, also for the `j` tail-call wrapper fssdSetMasterVolume); native callers pass 0.
 * Quirks kept: 10,000,001 polls before the timeout message (then -1 without calling; dead: CheckStatRpc returns 0);
 * ...Int / ...Struct send buffer residue past the words they write; ...Struct copies iSend bytes with no bound against
 * the 256-byte buffer; fssdAllocIopMemory's timeout still allocates when iSize <= -1; fssdRequireIopToEeRpc returns 1
 * even when CreateThread failed; the dispatch reply copy is not bounded by g_ucErrBuf. */
#include "acnb/port.h"
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/local/lib/fs/fslibsd.h"

#ifdef ACNB_SOURCEPORT
int s_iIopToEeRpcThreadId = -1;   /* 0x27aca0: no IopToEeRpcThread yet */
char fssd_msg_sync_wait[45] = "<CallRpcSyncWait> sceSifCheckStatRpc() == 1\n";
#endif

#define FSSD_BUF g_ucEeToIopRpcBuff
#define FSSD_FNO(m, cmd) ((((unsigned int)(m) << 28) & 0xf0000000u) | ((unsigned int)(cmd) & 0x0fffffffu))

/* the inlined CallRpcSyncWait: poll until the previous call is done; -1 after 10,000,001 busy polls, else 1 */
static int fssd_sync_wait(void)
{
    for (int n = 10000000; sceSifCheckStatRpc(&fssd_cd) == 1; n--) {
        if (n < 0) {
            ps2_printf(fssd_msg_sync_wait);
            return -1;
        }
    }
    return 1;
}

static inline int fssd_reply(void) { return *(int *)FSSD_BUF; }

/* the prologue's callee-saved stores (sq s0..s<n-1> at 0x10, 0x20, .., sd ra at ra_off): an interpreted SE / synth
 * later function at the same depth reads them as an uninitialised local (sound/se.cpp Se_KickAssemble's PS2_STACK
 * ret[2] = Doppler bend when GetSEParam is out of range: battle1 vb 8235 SE packet bytes). Exact when entered from MIPS
 * (bw thunk: the EE registers are the caller's); from native callers they hold whatever the EE registers hold. */
static inline void fssd_saves(int n, unsigned int ra_off)
{
#ifdef ACNB_SOURCEPORT
    (void)n;
    (void)ra_off;
#else
    for (int r = 0; r < n; r++) PS2_SAVED_GPR(16 + r, 0x10 * (r + 1));
    PS2_STACK(unsigned long long, saved_ra, ra_off);
    saved_ra = PS2_STALE_GPR(31);
#endif
}

void fssdFreeIopMemory(void *pBuff)
{
    PS2_FRAME(0x10);
    if (pBuff) sceSifFreeIopHeap(pBuff);
}

void *fssdAllocIopMemory(int iSize)
{
    PS2_FRAME(0x50);
    int iFree = -1;
    if (fssd_sync_wait() >= 0) {
        sceSifCallRpc(&fssd_cd, 0x30000, 0, FSSD_BUF, 0, FSSD_BUF, 0x10, nullptr, nullptr);
        iFree = fssd_reply();
    }
    if (iFree < iSize) return nullptr;
    return sceSifAllocIopHeap((unsigned int)iSize);
}

int fssdRequireIopToEeRpc(void)
{
    PS2_FRAME(0x60);
    if (s_iIsMakeIopToEeRpc) return 0;
    /* status / currentPriority / attr / option / wait words are never written: the kernel reads the stale stack */
    PS2_STACK(ThreadParam, param, 0x30);
    param.initPriority = 0x30;
    param.entry = (void (*)(void *))GFN(IopToEeRpcThread);
    param.stackSize = 0x1000;
    param.stack = s_ucRpcStack;
    param.gpReg = &_gp;
    int ok;
    int tid = ps2_CreateThread(&param);
    if (tid >= 0) {
        s_iIopToEeRpcThreadId = tid;
        ps2_StartThread(tid, nullptr);
        ok = 1;
    } else {
        ok = 0;
    }
    if (ok) {
        if (fssd_sync_wait() >= 0)
            sceSifCallRpc(&fssd_cd, 0x20000, 0, FSSD_BUF, 0, FSSD_BUF, 0x10, nullptr, nullptr);
        s_iIsMakeIopToEeRpc = 1;
    }
    return 1;
}

void fssdRegister(int iSdModuleId, void *(*psrcFunc)(unsigned int, void *, int, int *))
{
    s_psrcFunc[iSdModuleId] = psrcFunc;
}

unsigned int *fssdGetCalledRpcResult(void)
{
    return (unsigned int *)FSSD_BUF;
}

int fssdCallRpcStruct(int iModule, int iIsBlock, int iSend, int iRecv, int iCmd, void *pStruct)
{
    PS2_FRAME(0x90);
    fssd_saves(7, 0x80);
    if (fssd_sync_wait() < 0) return -1;
    if (pStruct && iSend > 0) {
        const unsigned char *s = (const unsigned char *)pStruct;
        unsigned char *d = FSSD_BUF;
        for (int i = 0; i < iSend; i++) d[i] = s[i];
    }
    int ssize = (iSend + 15) & ~15;
    if (ssize > 0x100) ssize = 0x100;
    int rsize = (iRecv + 15) & ~15;
    if (rsize > 0x100) rsize = 0x100;
    sceSifCallRpc(&fssd_cd, FSSD_FNO(iModule, iCmd), iIsBlock == 0, FSSD_BUF, ssize, FSSD_BUF, rsize, nullptr, nullptr);
    return fssd_reply();
}

int fssdCallRpcInt(int iModule, int iIsBlock, int iCmd, int iParam)
{
    PS2_FRAME(0x70);
    fssd_saves(5, 0x60);
    if (fssd_sync_wait() < 0) return -1;
    *(int *)FSSD_BUF = iParam;   /* words 1-3 of the 16-byte send: buffer residue */
    sceSifCallRpc(&fssd_cd, FSSD_FNO(iModule, iCmd), iIsBlock == 0, FSSD_BUF, 0x10, FSSD_BUF, 0x10, nullptr, nullptr);
    return fssd_reply();
}

int fssdCallRpcVoid(int iModule, int iIsBlock, int iCmd)
{
    PS2_FRAME(0x60);
    fssd_saves(4, 0x50);
    if (fssd_sync_wait() < 0) return -1;
    sceSifCallRpc(&fssd_cd, FSSD_FNO(iModule, iCmd), iIsBlock == 0, FSSD_BUF, 0, FSSD_BUF, 0x10, nullptr, nullptr);
    return fssd_reply();
}

int fssdCallRpc(int iModule, int iIsBlock, int iCmd, int w0, int w1, int w2, int w3, int w4, int w5, int w6, int w7)
{
    PS2_FRAME(0xc0);
    fssd_saves(4, 0x50);
#ifndef ACNB_SOURCEPORT
    {
        /* the prologue's varargs home area: a2 at 0x88, a3 / t0..t3 at 0x98..0xb8 (w5..w7 are the caller's sp + 0..0x10) */
        PS2_STACK(int, home_cmd, 0x88);
        PS2_STACK(long long, home_w0, 0x98);
        PS2_STACK(long long, home_w1, 0xa0);
        PS2_STACK(long long, home_w2, 0xa8);
        PS2_STACK(long long, home_w3, 0xb0);
        PS2_STACK(long long, home_w4, 0xb8);
        home_cmd = iCmd;
        home_w0 = w0;
        home_w1 = w1;
        home_w2 = w2;
        home_w3 = w3;
        home_w4 = w4;
    }
#endif
    if (fssd_sync_wait() < 0) return -1;
    int *b = (int *)FSSD_BUF;
    b[0] = w0;
    b[1] = w1;
    b[2] = w2;
    b[3] = w3;
    b[4] = w4;
    b[5] = w5;
    b[6] = w6;
    b[7] = w7;
    sceSifCallRpc(&fssd_cd, FSSD_FNO(iModule, iCmd), iIsBlock == 0, FSSD_BUF, 0x20, FSSD_BUF, 0x10, nullptr, nullptr);
    return fssd_reply();
}

#ifndef ACNB_SOURCEPORT
/* MIPS callers (fslibsdsynth, interpreted): 11 int arguments, w5..w7 from the caller's stack. Every register is read
 * before the native runs (its bridge calls clobber a0..t3). */
static void bw__fssdCallRpc(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    int r = fssdCallRpc(c->gpr[A0].sw[0], c->gpr[A1].sw[0], c->gpr[A2].sw[0], c->gpr[A3].sw[0], c->gpr[T0].sw[0],
                        c->gpr[T1].sw[0], c->gpr[T2].sw[0], c->gpr[T3].sw[0], (int)rd32(sp), (int)rd32(sp + 8),
                        (int)rd32(sp + 0x10));
    c->gpr[V0].sd[0] = r;
}
#endif

/* `j fssdCallRpc`: w0 = core, w1 / w2 = volumes; the other payload words are the caller's (0 from native callers) */
int fssdSetMasterVolume(int iCore, unsigned short usLeftVol, unsigned short usRightVol)
{
    return fssdCallRpc(0, 1, 0x40000, iCore, usLeftVol & 0xffff, usRightVol & 0xffff, 0, 0, 0, 0, 0);
}

#ifndef ACNB_SOURCEPORT
static void bw__fssdSetMasterVolume(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    int r = fssdCallRpc(0, 1, 0x40000, c->gpr[A0].sw[0], (int)(c->gpr[A1].uw[0] & 0xffff), (int)(c->gpr[A2].uw[0] & 0xffff),
                        c->gpr[T2].sw[0], c->gpr[T3].sw[0], (int)rd32(sp), (int)rd32(sp + 8), (int)rd32(sp + 0x10));
    c->gpr[V0].sd[0] = r;
}
#endif

int fssdIsInited(void)
{
    return s_iIsInit;
}

int fssdCleanup(void)
{
    PS2_FRAME(0x10);
    if (s_iIsInit && s_iIsMakeIopToEeRpc) {
        ps2_TerminateThread(s_iIopToEeRpcThreadId);
        ps2_DeleteThread(s_iIopToEeRpcThreadId);
        sceSifRemoveRpc(&fssd_sd, &fssd_qd);
        sceSifRemoveRpcQueue(&fssd_qd);
    }
    s_iIsInit = 0;
    s_iIsMakeIopToEeRpc = 0;
    return 1;
}

int fssdInit(void)
{
    PS2_FRAME(0x30);
    if (s_iIsInit == 1) return 0;
    s_iIsMakeIopToEeRpc = 0;
    /* no wait between the binds (hle.c's BindRpc runs the IOP until the server exists, then sets serve) */
    do sceSifBindRpc(&fssd_cd, 0x101a, 0);
    while (*(void *volatile *)&fssd_cd.serve == nullptr);
    if (fssd_sync_wait() >= 0)
        sceSifCallRpc(&fssd_cd, 0x10000, 0, FSSD_BUF, 0, FSSD_BUF, 0x10, nullptr, nullptr);
    s_iIsInit = 1;
    return 1;
}

/* the 0x102a server function: (fno, receive buffer, size) -> reply; handlers get (cmd, pData, iSize, &iRetSize) */
static void *IopToEeRpcDispatch(unsigned int uiFno, void *pData, int iSize)
{
    PS2_FRAME(0x20);
    unsigned int m = (uiFno & 0xf0000000u) >> 28;
    *(int *)g_ucErrBuf = 0;
    if (m != 0) {
        void *(*f)(unsigned int, void *, int, int *) = s_psrcFunc[m];
        if (f) {
            PS2_STACK(int, iRetSize, 0x1c);   /* its address goes to the (guest) handler */
            iRetSize = 0;
            const unsigned char *p = (const unsigned char *)gcall(f, uiFno & 0x0fffffffu, pData, iSize, &iRetSize);
            int n = iRetSize;
            for (int i = 0; i < n; i++) g_ucErrBuf[i] = p[i];
        }
    }
    return g_ucErrBuf;
}

static void IopToEeRpcThread(void)
{
    PS2_FRAME(0x10);
    sceSifInitRpc(0);
    sceSifSetRpcQueue(&fssd_qd, ps2_GetThreadId());
    sceSifRegisterRpc(&fssd_sd, 0x102a, GFN(IopToEeRpcDispatch), g_ucIopToEeRpcBuff, nullptr, nullptr, &fssd_qd);
    sceSifRpcLoop(&fssd_qd);   /* never returns */
}
