/* lib/sdk/libsif.cpp - the EE side of the SIF (sceSif* boot / heap / module / DMA / RPC calls of main/iop.c, lib/fs
 * fssd*, fslibmfstrm, _LoadEmbeddedIrx) and libsdr's sceSdRemote / sceSdRemoteInit for the source port, on the native
 * IOP through platform/snd/sndhost (phase 3 step 5 NI9; semantics = the hybrid's HLE, runtime/hle.c + iop_kern.c).
 * Reboot / sync report done (1) and do not reset the IOP (hybrid h_ret1). The IOP heap is iopk sysmem:
 * sceSifAllocIopHeap returns an IOP address that the EE code only passes on; sceSifSetDma copies EE memory into IOP
 * RAM at once (DmaStat: always done). Module loads parse the IRX (IOP heap image or cdrom0: file) and start its native
 * module, or log it "not run" with id 1000 + n (pad / memory card / USB / network: host services in later steps). RPC
 * calls are synchronous (CheckStatRpc 0; the end function runs before sceSifCallRpc returns). EE RPC servers
 * (sceSifRegisterRpc, IOP -> EE requests) run in the game's own server threads (boot slice B6,
 * 2.5): sceSifRpcLoop = `for (;;) { sndhost::ServeQueue(qd); SleepThread(); }` in the thread named by
 * sceSifSetRpcQueue (fssd IopToEeRpcThread 0x102a, fslibmfstrm _ItoE_RpcThread 0x4020), auto-serve off. The posted
 * hook (IOP thread) only marks the sid; the vblank step (sourceport/main/host_vsync.cpp, after sndhost::AdvanceEe)
 * calls sif_vblank_wake, which iWakeupThreads the server thread of every marked sid (no switch there: the woken
 * thread preempts main at the end of the vsync wait / idle step, as the hybrid's next EE slice). The hybrid runs the
 * original SDK code (runtime/bridge_sdk.c thunks + HLE hooks); nothing here is built there. */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/nodwarf.h"

#ifdef ACNB_SOURCEPORT
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../../platform/plat.h"
#include "../../platform/snd/sndhost.h"
#include "../../platform/kern/ps2_kern.h"

namespace iophost { const sndhost::Binding *HostBinding(); }   /* src/iop/iophost.h (not included in a game unit) */
extern "C" void sp_vsync_set_ee_wake(void (*fn)(void *user), void *user);   /* sourceport/main/sp_vsync.h */

static int sif_nmods;

/* ---- EE RPC server threads (B6). Queues: sceSifSetRpcQueue's (qd, thread); servers: sceSifRegisterRpc's (sid, qd);
 * marked: the sids the IOP posted a request to since the last vblank step */
#define SIF_NQ 8
static struct { const void *qd; int tid; } sif_q[SIF_NQ];
static int sif_nq;
static struct { uint32_t sid; const void *qd; } sif_srv[16];
static int sif_nsrv;
static uint32_t sif_marked[16];
static int sif_nmarked;

static void sif_posted(void *, uint32_t sid)   /* IOP thread: mark only (sndhost.h) */
{
    for (int i = 0; i < sif_nmarked; i++) if (sif_marked[i] == sid) return;
    if (sif_nmarked < 16) sif_marked[sif_nmarked++] = sid;
}

static void sif_vblank_wake(void *)   /* the vblank step, interrupt context: wake, never switch */
{
    for (int m = 0; m < sif_nmarked; m++) {
        const void *qd = 0;
        for (int i = 0; i < sif_nsrv; i++) if (sif_srv[i].sid == sif_marked[m]) qd = sif_srv[i].qd;
        for (int i = 0; qd && i < sif_nq; i++)
            if (sif_q[i].qd == qd && sif_q[i].tid > 0) ps2k_irq_wakeup(sif_q[i].tid);
    }
    sif_nmarked = 0;   /* a server whose thread does not exist yet serves its queue when sceSifRpcLoop starts */
}

/* IOP -> EE SIF DMA: EE addresses are the 32-bit host addresses the EE code sent (PS2PTR) */
static int sif_ee_write(void *, uint32_t ee_addr, const void *src, uint32_t size)
{
    if (!ee_addr) return 0;
    memcpy((void *)(uintptr_t)ee_addr, src, size);
    return 1;
}

static void sif_host(void)
{
    if (sndhost::Ready()) return;
    sndhost::Config c = {iophost::HostBinding(), 0, sif_ee_write, 0};
    sndhost::Init(&c);
    sndhost::SetAutoServe(0);
    sndhost::SetPostedHook(sif_posted, 0);
    sif_nmarked = 0;
    sp_vsync_set_ee_wake(sif_vblank_wake, 0);
}

void sceSifInitRpc(unsigned int) { sif_host(); }
int sceSifMInitRpc(unsigned int) { return 0; }
void sceSifMExitRpc(void) {}
int sceSifInitIopHeap(void) { sif_host(); return 0; }
int sceSifLoadFileReset(void) { return 0; }
int sceSifRebootIop(const char *) { sif_host(); return 1; }
int sceSifSyncIop(void) { return 1; }

void *sceSifAllocIopHeap(unsigned int size)
{
    sif_host();
    return (void *)(uintptr_t)sndhost::AllocIopHeap(size);
}
int sceSifFreeIopHeap(void *addr)
{
    sif_host();
    sndhost::FreeIopHeap((uint32_t)(uintptr_t)addr);
    return 0;   /* hybrid h_sceSifFreeIopHeap */
}

/* sceSifDmaData[len] (struct_f14f5dcc: data, addr, size, mode): EE -> IOP copies, done inside the call */
static unsigned int sif_dma_id;
unsigned int sceSifSetDma(void *sdd, int len)
{
    sif_host();
    const struct struct_f14f5dcc *d = (const struct struct_f14f5dcc *)sdd;
    for (int i = 0; i < len; i++)
        if (d[i].data && d[i].size && !sndhost::DmaToIop((const void *)(uintptr_t)d[i].data, d[i].addr, d[i].size))
            plat_log("sif: SetDma to IOP %08x size %x out of range", (unsigned)d[i].addr, (unsigned)d[i].size);
    if (!++sif_dma_id) sif_dma_id = 1;
    return sif_dma_id;
}
int sceSifDmaStat(unsigned int) { return -1; }

/* a module the native IOP cannot take (bad image / missing file): the game's __LoadModule retries a negative id
 * forever (the hybrid's -1), so it counts as started and not run */
static int sif_module(const char *what, int *result)
{
    if (result) *result = 0;
    plat_log("sif: module %s not loaded", what);
    return 1000 + sif_nmods++;
}

int sceSifLoadStartModuleBuffer(const void *ptr, int arg_len, const char *args, int *result)
{
    sif_host();
    int r = sndhost::LoadModuleBuffer((uint32_t)(uintptr_t)ptr, args, arg_len, result);
    return r < 0 ? sif_module("(IOP heap image)", result) : r;
}
int sceSifLoadStartModule(const char *path, int arg_len, const char *args, int *result)
{
    sif_host();
    int r = path ? sndhost::LoadModuleFile(path, args, arg_len, result) : -1;
    return r < 0 ? sif_module(path ? path : "?", result) : r;
}

/* the IRX embedded in the ELF (ck01, nodwarf.h embedded_irx_ck01): DMA it into an IOP heap block, load + start it
 * from there, free the block (0x13e7c8). Returns the module id (<0 = error); *result = its start result & ~3 */
long long _LoadEmbeddedIrx(int *result)
{
    unsigned int size = sizeof embedded_irx_ck01;   /* (0x259b59 - 0x259800 + 15) & ~15 */
    *result = 0;
    void *iop = sceSifAllocIopHeap(size);
    if (!iop) return -1;
    struct struct_f14f5dcc dma = {(unsigned int)(uintptr_t)embedded_irx_ck01, (unsigned int)(uintptr_t)iop, size, 0};
    unsigned int id;
    while (!(id = sceSifSetDma(&dma, 1))) {}
    while (sceSifDmaStat(id) >= 0) {}
    int r = sceSifLoadStartModuleBuffer(iop, 0, 0, result);
    *result &= ~3;
    sceSifFreeIopHeap(iop);
    return r;
}

/* ---- SIF RPC. _sif_client_data +0x24 = serve (nonzero once bound); the sid of each bound cd is kept here (hle.c) */
#define SIF_CD_SERVE 0x24
static struct { const void *cd; uint32_t sid; } sif_binds[128];
static int sif_nbinds;

static uint32_t sif_bind_sid(const void *cd)
{
    for (int i = 0; i < sif_nbinds; i++) if (sif_binds[i].cd == cd) return sif_binds[i].sid;
    return 0;
}

int sceSifBindRpc(void *cd, unsigned int rpc_number, unsigned int)
{
    sif_host();
    int i = 0;
    while (i < sif_nbinds && sif_binds[i].cd != cd) i++;
    if (i == sif_nbinds) {
        if (sif_nbinds == 128) return -1;
        sif_nbinds++;
    }
    sif_binds[i].cd = cd;
    sif_binds[i].sid = rpc_number;
    if (!sndhost::Bind(rpc_number)) plat_log("sif: bind to unknown server %08x", rpc_number);   /* bound anyway */
    uint32_t serve = 0x00030000u | (rpc_number & 0xfff0);   /* any non-zero "server" pointer (hybrid) */
    memcpy((char *)cd + SIF_CD_SERVE, &serve, 4);
    return 0;
}

int sceSifCallRpc(void *cd, unsigned int fno, unsigned int mode, void *send, int ssize, void *recv, int rsize, void *endfunc,
                  void *endpara)
{
    sif_host();
    uint32_t sid = sif_bind_sid(cd);
    int strace = snd_trace_on && snd_trace_enabled(), srec = snd_rec_on && snd_rec_enabled();
    uint32_t scrc = 0, s0 = 0;
    if (strace && send && ssize > 0) {
        scrc = disc_crc32(send, (uint32_t)ssize);
        if (ssize >= 4) memcpy(&s0, send, 4);
    }
    if (srec) {   /* ACNB_SND_REC "RPCS" (runtime/hle.c shape) */
        uint32_t n = send && ssize > 0 ? (uint32_t)ssize : 0, h[5] = {sid, fno, mode, (uint32_t)ssize, n};
        snd_rec("RPCS", h, 5, send, n, 0, 0);
    }
    if (sndhost::RpcCall(sid, fno, send, ssize > 0 ? (uint32_t)ssize : 0, recv, rsize > 0 ? (uint32_t)rsize : 0) < 0) {
        static int n_log;
        if (n_log < 16) plat_log("sif: call to unknown server %08x fno %x (ssize %x rsize %x)", sid, fno, ssize, rsize);
        n_log++;
        if (recv && rsize > 0) memset(recv, 0, (size_t)rsize);
    }
    if (strace) {   /* runtime/hle.c "EE RPC" line */
        uint32_t r0 = 0;
        if (recv && rsize >= 4) memcpy(&r0, recv, 4);
        snd_trace("EE", "RPC sid %08x fno %08x mode %x ssize %x rsize %x s0 %08x scrc %08x -> r0 %08x rcrc %08x", sid, fno,
                  mode, ssize, rsize, s0, scrc, r0, recv && rsize > 0 ? disc_crc32(recv, (uint32_t)rsize) : 0);
    }
    if (srec) {
        uint32_t n = recv && rsize > 0 ? (uint32_t)rsize : 0, h[3] = {sid, fno, n};
        snd_rec("RPCR", h, 3, recv, n, 0, 0);
    }
    if (endfunc) ((void (*)(void *))endfunc)(endpara);
    return 0;
}

int sceSifCheckStatRpc(void *) { return 0; }   /* every call is complete when sceSifCallRpc returns */

void sceSifSetRpcQueue(void *qd, int thread_id)
{
    int i = 0;
    while (i < sif_nq && sif_q[i].qd != qd) i++;
    if (i == SIF_NQ) return;
    if (i == sif_nq) sif_nq++;
    sif_q[i].qd = qd;
    sif_q[i].tid = thread_id;
}

void sceSifRegisterRpc(void *sd, unsigned int sid, void *(*func)(unsigned int, void *, int), void *buf,
                       void *(*)(unsigned int, void *, int), void *, void *qd)
{
    sif_host();
    sndhost::RegisterEeServer(sid, func, buf, qd, sd);
    int i = 0;
    while (i < sif_nsrv && sif_srv[i].sid != sid) i++;   /* a sid registered again replaces the old entry (sndhost) */
    if (i == 16) return;
    if (i == sif_nsrv) sif_nsrv++;
    sif_srv[i].sid = sid;
    sif_srv[i].qd = qd;
}

void *sceSifRemoveRpc(void *sd, void *)
{
    sndhost::RemoveEeServer(sd);
    return 0;   /* hybrid h_ret0 */
}

void *sceSifRemoveRpcQueue(void *) { return 0; }

/* the server thread's loop (file comment): serve what is pending, sleep until the vblank step wakes it; never returns */
void sceSifRpcLoop(void *qd)
{
    sif_host();
    for (;;) {
        sndhost::ServeQueue(qd);
        ps2_SleepThread();
    }
}

/* ---- libsdr (EE): sceSdRemote(arg, command, ...) packs its arguments into a 0x40-byte buffer and calls SDRDRV (SID
 * 0x80000701, fno = command, send 0x40 / reply 0x10 in the same buffer, result = reply word 0); arg 0 = no-wait with
 * the registered callback as end function. Asm 0x139478 / sceSdRemoteInit 0x139338. */
#define SDR_SID 0x80000701u
alignas(64) static unsigned int sdr_buf[16];    /* 0x287dc0 */
alignas(16) static unsigned char sdr_cd[0x28];  /* 0x287e00 */
static void *sdr_endfunc;                       /* 0x259670 (set by 0x139468; never called in Versus) */

int sceSdRemote(int arg, int command, int a2)
{
    sif_host();
    unsigned int cmd = (unsigned int)command;
    sdr_buf[0] = PS2PTR(sdr_buf);   /* the original stores the buffer address, then the six argument words */
    sdr_buf[1] = (unsigned int)a2;
    for (int i = 2; i < 7; i++) sdr_buf[i] = 0;   /* original: the caller's stale a3 / t0..t3 (unused by 0x8000) */
    int nowait = arg == 0;
    if (cmd == 0x8130 || cmd == 0x81a0 || cmd == 0x81b0 || cmd == 0x8140 || cmd == 0x8160 || cmd == 0x8170 ||
        cmd == 0x81c0 || cmd == 0x81d0 || cmd - 0x9000u < 0xf1u) {
        /* the pointer / batch / user-data commands (asm 0x139500 .. 0x1396cc): REV, not sent by Versus */
        static int n_log;
        if (n_log++ < 4) plat_log("sceSdRemote: command %x not ported (REV)", cmd);
        return 0;
    }
    sceSifCallRpc(sdr_cd, cmd, (unsigned int)nowait, sdr_buf, 0x40, sdr_buf, 0x10, nowait ? sdr_endfunc : 0, sdr_buf);
    return (int)sdr_buf[0];
}

int sceSdRemoteInit(void)
{
    sceSifInitRpc(0);
    for (;;) {
        if (sceSifBindRpc(sdr_cd, SDR_SID, 0) < 0) {
            plat_log("sceSdRemoteInit: bind error");
            return -1;
        }
        /* original: a 10,000-iteration delay loop, then retry while the server pointer is 0 */
        uint32_t serve;
        memcpy(&serve, sdr_cd + SIF_CD_SERVE, 4);
        if (serve) break;
    }
    return 0;   /* original: FlushCache(0) first (no-op here) */
}
#endif
