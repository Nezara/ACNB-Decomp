/* mfstrm - see mfstrm.h. Module start, the SID 0x4010 server + its command ladder, the IOP -> EE bind. Function
 * comments give the original's image offset. */
#include "mfstrm_state.h"
#include <string.h>

namespace mfstrm {

/* ---------------------------------------------------------------- module state */

uint32_t g_base;
iopk::RpcClient s_scdRpc;
int32_t s_DmaSema, g_DmaThID;
uint32_t s_dma_pkt;
int32_t s_dma_pkt_size, s_bInited, s_DmaStat;
ArbMng s_ArbMng;
ArbInfo s_ArbInfo[8];
SifDmaInfo s_sdiData;
iopk::SifDmaData s_sddPacket;
int32_t s_SemaID, s_MstrmThID, g_iUpdateSema;
StrmMng s_StrmMng;
int32_t s_iCdvdEndReading;
StrmInfo s_StrmInfo[8];
StrmBuffMng s_StrmBuffMng;
StrmBuffInfo s_StrmBuffInfo[8];
int32_t s_iHostThreadId, s_iSemaHost, s_iSyncSemaHost;
HostRead s_HostRead;

void ResetState()
{
    g_base = 0;
    memset(&s_scdRpc, 0, sizeof s_scdRpc);
    s_DmaSema = -1;
    g_DmaThID = -1;
    s_dma_pkt = 0;
    s_dma_pkt_size = s_bInited = s_DmaStat = 0;
    memset(&s_ArbMng, 0, sizeof s_ArbMng);
    memset(s_ArbInfo, 0, sizeof s_ArbInfo);
    memset(&s_sdiData, 0, sizeof s_sdiData);
    memset(&s_sddPacket, 0, sizeof s_sddPacket);
    s_SemaID = s_MstrmThID = g_iUpdateSema = -1;
    memset(&s_StrmMng, 0, sizeof s_StrmMng);
    s_iCdvdEndReading = 0;
    memset(s_StrmInfo, 0, sizeof s_StrmInfo);
    s_StrmBuffMng.free.top = 0;
    s_StrmBuffMng.free.num = 8;
    s_StrmBuffMng.alloc.top = -1;
    s_StrmBuffMng.alloc.num = 0;
    s_StrmBuffMng.flags = 0;
    memset(s_StrmBuffInfo, 0, sizeof s_StrmBuffInfo);
    s_iHostThreadId = s_iSemaHost = s_iSyncSemaHost = -1;
    memset(&s_HostRead, 0, sizeof s_HostRead);
    g_dma_update_calls = 0;
    g_dma_update_t = 0;
    g_update_calls = 0;
    g_update_locked = 0;
}

uint32_t Base() { return g_base; }

static uint8_t *g_ucRet_RpcBuff() { return iopk::Ptr(g_base + OFS_RET); }
static uint8_t *g_ucEtoI_RpcBuff() { return iopk::Ptr(g_base + OFS_ETOI); }

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static void wr32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

/* ---------------------------------------------------------------- RPC */

/* 0x37c: bind the EE server 0x4020. The original re-binds in a loop without delay until the server exists; the native
 * form waits for the next IOP event between tries (Guard 1: a bare spin at one T would hang the sim). With the EE
 * server up (all replays: the EE creates it before sending 0x100) the first try succeeds. The dispatcher ignores the
 * result. */
int _ItoE_BindRpc()
{
    for (;;) {
        iopk::SifBindRpc(&s_scdRpc, SID_IOP2EE, 0);
        if (s_scdRpc.server != 0) return 1;
        iopk::WaitNextEvent();
    }
}

/* ---------------------------------------------------------------- IOP -> EE client (SID 0x4020; never fires in Versus) */

static uint8_t *g_ucItoE_RpcBuff() { return iopk::Ptr(g_base + OFS_ITOE); }

/* 0x890: wait until the client has no request pending (SifCheckStatRpc 1 = pending). The original polls 10,000,000
 * times with no kernel call; the native form waits for the next IOP event per poll (guard 1, GH5-1: the EE side only
 * completes a request between events), same budget and message. */
int _CallRpcSyncWait()
{
    int32_t n = 0x989680;
    if (iopk::SifCheckStatRpc(&s_scdRpc) != 1) return 1;
    for (;;) {
        if (n < 0) {
            iopk::Printf("<_CallRpcSyncWait> sceSifCheckStatRpc() == 1\n");
            return 0;
        }
        n--;
        iopk::WaitNextEvent();
        if (iopk::SifCheckStatRpc(&s_scdRpc) != 1) return 1;
    }
}

/* 0x908: wait == 0 -> NOWAIT (mode 1). Sizes rounded up to 16, clamped to 0x100 (signed). Send and receive buffer are
 * both g_ucItoE_RpcBuff; word 0 is read right after the call (NOWAIT: still the word that was sent). */
int _CallRpc(uint32_t cmd, uint32_t wait, int ssize, int rsize)
{
    uint32_t mode = wait == 0;
    ssize = (ssize + 15) & -16;
    if (ssize >= 0x101) ssize = 0x100;
    rsize = (rsize + 15) & -16;
    if (rsize >= 0x101) rsize = 0x100;
    iopk::FlushDcache();
    uint8_t *b = g_ucItoE_RpcBuff();
    iopk::SifCallRpc(&s_scdRpc, cmd, mode, b, (uint32_t)ssize, b, (uint32_t)rsize, 0, 0);
    return (int)rd32(b);
}

/* 0xa34: variadic in the original - eight va_arg words from sp+0x28 (a2, a3, then the caller's out-args area = stack
 * residue) go to g_ucItoE_RpcBuff[0..7]; only words 0 / 1 reach the EE (_ItoE_RpcDispatch: 0x10000100 EndReading(w0),
 * 0x10000200 CheckReadError(w0, w1)), words 2..7 are sent as 0 here (GH5-2). -1 when the client stays busy. */
int fsmfstrm_CallRpc(uint32_t cmd, uint32_t wait, int a2, int a3)
{
    if (!_CallRpcSyncWait()) return -1;
    uint8_t *b = g_ucItoE_RpcBuff();
    wr32(b, (uint32_t)a2);
    wr32(b + 4, (uint32_t)a3);
    for (int i = 2; i < 8; i++) wr32(b + 4 * i, 0);
    return _CallRpc(cmd, wait, 0x20, 4);
}

/* ---------------------------------------------------------------- SIF DMA to the EE (dma_Update) */

/* 0xb48: the DMA end function (3 insns hidden inside fsmfstrm_CallRpc's symbol size); iopk runs it as a deferred call
 * after the slice that queued the transfer */
void _SifDmaEndFunc(void *arg)
{
    (void)arg;
    s_DmaStat = 0;
}

/* 0xb54: queue the first block of rec (src / dst / chunk). With the packet buffer present and chunk > its size the
 * block is clamped to the packet size (not rounded); else the size is rounded up to 16 (may read up to 15 bytes past
 * the valid ones, harmless). */
int _SifTransBuffer(ArbInfo *rec)
{
    int st;
    iopk::CpuSuspendIntr(&st);
    uint32_t pkt = s_dma_pkt;
    s_sdiData.src = rec->src;
    s_sdiData.dst = rec->dst;
    s_sdiData.remain = rec->chunk;
    s_sdiData.busy = 1;
    s_sdiData.abort = 0;
    int32_t n = rec->chunk;
    uint32_t size;
    if (pkt != 0 && s_dma_pkt_size < n) {
        s_sdiData.chunk = s_dma_pkt_size;
        size = (uint32_t)s_dma_pkt_size;
    } else {
        s_sdiData.chunk = n;
        size = (uint32_t)(n + 15) & ~15u;
    }
    s_sddPacket.size = size;
    s_sddPacket.attr = 0;
    s_sddPacket.src = iopk::Ptr(s_sdiData.src);
    s_sddPacket.dest = s_sdiData.dst;
    iopk::FlushDcache();
    s_DmaStat = 1;
    s_sdiData.id = iopk::SifSetDmaIntr(&s_sddPacket, 1, _SifDmaEndFunc, 0);
    iopk::CpuResumeIntr(st);
    return s_sdiData.id == 0 ? -1 : 0;
}

/* 0xc88: 1 = nothing in flight / transfer done (or aborted), 0 = DMA still running or the next chunk was queued.
 * Q-G4h: the chunk step uses s_dma_pkt_size without checking s_dma_pkt; later chunks are not rounded. */
int _SifTransSync()
{
    if (!s_sdiData.busy) return 1;
    if (s_DmaStat > 0) return 0;
    int32_t a = (int32_t)((uint32_t)s_sdiData.remain - (uint32_t)s_sdiData.chunk);
    s_sdiData.remain = a;
    if (a <= 0 || s_sdiData.abort) {
        s_sdiData.busy = 0;
        return 1;
    }
    int32_t p = s_dma_pkt_size;
    s_sdiData.src += (uint32_t)p;
    s_sdiData.dst += (uint32_t)p;
    if (p < a) {
        s_sdiData.chunk = p;
    } else {
        s_sdiData.chunk = a;
        p = a;
    }
    s_sddPacket.size = (uint32_t)p;
    s_sddPacket.attr = 0;
    s_sddPacket.src = iopk::Ptr(s_sdiData.src);
    s_sddPacket.dest = s_sdiData.dst;
    iopk::FlushDcache();
    int st;
    iopk::CpuSuspendIntr(&st);
    s_DmaStat = 1;
    s_sdiData.id = iopk::SifSetDmaIntr(&s_sddPacket, 1, _SifDmaEndFunc, 0);
    iopk::CpuResumeIntr(st);
    return 0;
}

/* 0x3bc: the SID 0x4010 server function. `fno` is compared as a whole word (the original's linear beq chain; order
 * irrelevant). "reply" arms store the handler's v0 into word 0 of the static reply buffer; the others (0x200, 0x300,
 * 0x400, 0x500, 0xb000, 0xd000, 0x11000, 0x300000) and unknown fnos leave it stale (H1). size is never read. */
void *_EtoI_RpcDispatch(int fno, void *buf, int size)
{
    (void)size;
    uint8_t *b = (uint8_t *)buf;
    uint8_t *reply = g_ucRet_RpcBuff();
    switch ((uint32_t)fno) {
    case 0x100: wr32(reply, (uint32_t)mstrm_Init()); break;
    case 0x200: mstrm_Quit(); break;
    case 0x300: _ItoE_BindRpc(); break;
    case 0x400: dma_ArbiterInit(); break;
    case 0x500: dma_ArbiterQuit(); break;
    case 0x600: wr32(reply, (uint32_t)dma_AllocPacketBuffer((int)rd32(b))); break;
    case 0x700: wr32(reply, (uint32_t)dma_FreePacketBuffer()); break;
    case 0x1000: wr32(reply, (uint32_t)mstrm_AllocStream((int)rd32(b))); break;
    case 0x2000: wr32(reply, (uint32_t)mstrm_FreeStream((int)rd32(b))); break;
    case 0x3000: {   /* name at buf + 4, the u16 mode after it, word aligned: + ((strlen + 4) & ~3) */
        int idx = (int)rd32(b);
        const char *name = (const char *)b + 4;
        uint32_t o = ((uint32_t)strlen(name) + 4) & ~3u;
        wr32(reply, (uint32_t)mstrm_AttachFile(idx, name, rd16(b + 4 + o)));
        break;
    }
    case 0x13000: {   /* p = the aligned word after the name: u16 mode, word offset (a3), word length (5th, sp+0x10) */
        int idx = (int)rd32(b);
        const char *name = (const char *)b + 4;
        const uint8_t *p = b + 4 + (((uint32_t)strlen(name) + 4) & ~3u);
        int len = (int)rd32(p + 8);
        wr32(reply, (uint32_t)mstrm_AttachFileOffset(idx, name, rd16(p), (int)rd32(p + 4), len));
        break;
    }
    case 0x4000: wr32(reply, (uint32_t)mstrm_AttachSector((int)rd32(b), (int)rd32(b + 4), (int)rd32(b + 8), rd16(b + 12))); break;
    case 0xe000: wr32(reply, (uint32_t)mstrm_IsAttached((int)rd32(b))); break;
    case 0x5000: wr32(reply, (uint32_t)mstrm_DetachFileSector((int)rd32(b))); break;
    case 0x6000: wr32(reply, (uint32_t)mstrm_GetAttachDataSize((int)rd32(b))); break;
    case 0x7000: wr32(reply, (uint32_t)mstrm_GetRemainSize((int)rd32(b))); break;
    case 0x8000: wr32(reply, (uint32_t)mstrm_GetValidDataSize((int)rd32(b))); break;
    case 0x9000: wr32(reply, (uint32_t)mstrm_GetValidDataSizeF((int)rd32(b))); break;
    case 0xa000: wr32(reply, (uint32_t)mstrm_SetReloadSize((int)rd32(b), (int)rd32(b + 4))); break;
    case 0xb000: mstrm_SetReadStart((int)rd32(b), (int)rd32(b + 4)); break;
    case 0xc000: wr32(reply, (uint32_t)mstrm_SetPriority((int)rd32(b), rd16(b + 4))); break;
    case 0xd000: mstrm_SetLoopPoint((int)rd32(b), (int)rd32(b + 4), (int)rd32(b + 8), (int)rd32(b + 12)); break;
    case 0xf000: wr32(reply, (uint32_t)mstrm_SetCdRMode((int)rd32(b), b + 4)); break;
    case 0x10000: wr32(reply, (uint32_t)mstrm_GetReadError((int)rd32(b))); break;
    case 0x11000: mstrm_SetReadPacketSize((int)rd32(b)); break;
    case 0x12000: wr32(reply, (uint32_t)mstrm_Seek((int)rd32(b), (int)rd32(b + 4))); break;
    case 0x100000: wr32(reply, (uint32_t)dma_RequestTransBuffer(rd32(b), rd32(b + 4), rd32(b + 8), -1, 1, 0)); break;
    case 0x200000:   /* buf[1] != 0: wait (DelayThread(1000) between polls, buf[0] re-read each time) until dma_Sync
                      * returns nonzero, reply 1; else reply the plain result */
        if (rd32(b + 4) != 0) {
            if (dma_Sync(rd32(b)) == 0)
                do iopk::DelayThread(1000);
                while (dma_Sync(rd32(b)) == 0);
            wr32(reply, 1);
        } else {
            wr32(reply, (uint32_t)dma_Sync(rd32(b)));
        }
        break;
    case 0x300000: dma_CancelTransBuffer(rd32(b)); break;
    case 0x1000000: wr32(reply, (uint32_t)mstrm_IsDevBusy((int)rd32(b))); break;
    default: break;
    }
    return reply;
}

/* 0x9cc: the server thread (RpcServer / RpcQueue are its stack locals; SifRpcLoop never returns) */
static void EtoI_RpcThread(void *)
{
    iopk::RpcServer sd;
    iopk::RpcQueue qd;
    iopk::SifInitRpc(0);
    iopk::SifSetRpcQueue(&qd, iopk::GetThreadId());
    iopk::SifRegisterRpc(&sd, SID, _EtoI_RpcDispatch, g_ucEtoI_RpcBuff(), 0, 0, &qd);
    iopk::SifRpcLoop(&qd);
}

/* ---------------------------------------------------------------- module start */

/* 0x1e00 */
static int module_init()
{
    iopk::ThreadParam p;
    p.attr = 0x02000000;
    p.option = 0;
    p.entry = EtoI_RpcThread;
    p.stack_size = 0x800;
    p.priority = 0x28;
    int tid = iopk::CreateThread(&p);
    if (tid > 0) {
        iopk::StartThread(tid, 0);
        return 1;
    }
    return 0;
}

/* 0x1e70 module_start (0x1f38 `start` with a0 >= 0; a0 < 0 -> module_stop below). module_init_cancel (0x1e68) is
 * `jr ra`; RegisterLibraryEntries vanishes (the hybrid's returns 0 -> result 2, REMOVABLE_RESIDENT_END) - the
 * suspend / resume pair around it stays (a slice boundary). */
int ModuleStart(const iopk::ModuleArgs *m)
{
    ResetState();
    g_base = m->base;
    if (!module_init()) return 1;
    int st;
    iopk::CpuSuspendIntr(&st);
    iopk::CpuResumeIntr(st);
    return 2;
}

/* 0x1edc module_stop (`start` with a0 < 0; the loader hook only starts modules, so only the unit test calls it):
 * ReleaseLibraryEntries vanishes like RegisterLibraryEntries (its result is 0 here), the suspend / resume pair around it
 * stays. Result 1 for 0 / -0xd5 (KE_LIBRARY_NOTFOUND), else 2. Nothing is torn down (threads, semaphores, buffers
 * stay). */
int ModuleStop()
{
    int st;
    iopk::CpuSuspendIntr(&st);
    int r = 0;
    iopk::CpuResumeIntr(st);
    return (r == 0 || r == -0xd5) ? 1 : 2;
}

}  // namespace mfstrm
