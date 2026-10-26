/* ivag - see ivag.h. Module start, the module-10 RPC ladder, the thread / sema helpers, the command queue, the two
 * threads, ivagInit and the G1 setters. Function comments give the
 * original's image offset. */
#include "ivag_state.h"
#include "../../platform/plat.h"
#include "fssd.h"
#include <string.h>

namespace ivag {

/* ---------------------------------------------------------------- module state */

uint32_t g_base;
int32_t s_iRpcRet;
SMemMng s_SMemMng;
SMemInfo s_SMemInfo[16];
int32_t s_iInitialize;
uint32_t s_KeyOn[2];
uint8_t s_ucTransFlag, s_ucIrqFlag;
int32_t s_IVagSema[8];
CmdQueue s_IvagCmdQueue[8];
StatePack s_IVagStatePack;
IVagVoice s_IVagVoice[16];
IVagTrack s_IVagTrack[8];
IVagMng s_IVagMng;
int32_t s_iSetupThreadID = -1;

void ResetState()
{
    g_base = 0;
    s_iRpcRet = 0;
    memset(&s_SMemMng, 0, sizeof s_SMemMng);
    memset(s_SMemInfo, 0, sizeof s_SMemInfo);
    s_iInitialize = 0;
    s_KeyOn[0] = s_KeyOn[1] = 0;
    s_ucTransFlag = s_ucIrqFlag = 0;
    memset(s_IVagSema, 0, sizeof s_IVagSema);
    memset(s_IvagCmdQueue, 0, sizeof s_IvagCmdQueue);
    memset(&s_IVagStatePack, 0, sizeof s_IVagStatePack);
    memset(s_IVagVoice, 0, sizeof s_IVagVoice);
    memset(s_IVagTrack, 0, sizeof s_IVagTrack);
    memset(&s_IVagMng, 0, sizeof s_IVagMng);
    s_iSetupThreadID = -1;
}

uint32_t Base() { return g_base; }
uint32_t PacketBuffer() { return s_IVagMng.packetBuf; }

static int32_t rdw(const void *buf, int i) { int32_t v; memcpy(&v, (const uint8_t *)buf + 4 * i, 4); return v; }
static uint16_t rdh(const void *buf, int off) { uint16_t v; memcpy(&v, (const uint8_t *)buf + off, 2); return v; }

/* ---------------------------------------------------------------- RPC ladder */

/* 0x274: cmd = fno & 0x0fffffff, dispatched on cmd & 0x0fff0000 (low 16 bits ignored; Q-7: size unused, the words are
 * read whatever the EE sent). s_iRpcRet is zeroed per call, so the arms that drop their callee's value (SetCoreVoice,
 * SetDmaChannel, SetTrackReverb, ForceReset) and unknown commands reply 0 (GH1). GetTrackStat replies the live state
 * pack (0x48 bytes, GH5). */
void *RpcCallBack(uint32_t cmd, void *buf, int size, int *outsize)
{
    (void)size;
    s_iRpcRet = 0;
    *outsize = 4;
    void *ret = &s_iRpcRet;
    const void *w = buf;
    switch (cmd & 0x0fff0000u) {
    case 0x140000: ivagSetTrackReverb(rdw(w, 0), rdw(w, 1)); break;
    case 0x100000: ivagSetDmaChannel(rdw(w, 0)); break;
    case 0x130000: s_iRpcRet = ivagSetTrackCoreVoice(rdw(w, 0), rdw(w, 1)); break;
    case 0x120000: s_iRpcRet = ivagSetChannelCoreVoice(rdw(w, 0), rdw(w, 1), rdw(w, 2)); break;
    case 0x0f0000: ivagSetCoreVoice(rdw(w, 0)); break;
    case 0x0e0000: ret = ivagGetTrackStat(outsize); break;
    case 0x0d0000: {   /* Q-G3f: the original trusts buf[0] (reads past the buffer); clamped to the records received */
        int n = (int)rdw(w, 0), mx = size >= 4 ? (size - 4) / 8 : 0;
        if (n > mx) {
            plat_log("ivag: ivagSetVolumePack: %d records, %d received", n, mx);
            n = mx;
        }
        s_iRpcRet = ivagSetVolumePack((const uint8_t *)w + 4, n);
        break;
    }
    case 0x0b0000: s_iRpcRet = ivagSetMasterVolume(rdh(w, 0), rdh(w, 4)); break;
    case 0x0a0000: s_iRpcRet = ivagSetChVolume(rdw(w, 0), rdw(w, 1), rdh(w, 8), rdh(w, 0xc)); break;
    case 0x0c0000: s_iRpcRet = ivagGetChNum(rdw(w, 0)); break;
    case 0x090000: s_iRpcRet = ivagPause(rdw(w, 0)); break;
    case 0x080000: s_iRpcRet = ivagClose(rdw(w, 0)); break;
    case 0x070000: s_iRpcRet = ivagPlay(rdw(w, 0)); break;
    case 0x060000: s_iRpcRet = ivagSetSpuMemory(rdw(w, 0), rdw(w, 1)); break;
    case 0x050000: s_iRpcRet = ivagSetPacketBuffer(rdw(w, 0)); break;
    case 0x110000: s_iRpcRet = ivagGetReserveSetupResult(rdw(w, 0)); break;
    case 0x040000: s_iRpcRet = ivagReserveSetup(rdw(w, 0), rdw(w, 1)); break;
    case 0x030000: s_iRpcRet = ivagSetup(rdw(w, 0), rdw(w, 1)); break;
    case 0x150000: ivagForceReset(); break;
    case 0x020000: s_iRpcRet = ivagQuit(); break;
    case 0x010000: s_iRpcRet = ivagInit(); break;
    default: break;
    }
    return ret;
}

/* 0x5b0 */
int module_init()
{
    fssd::Register(MODULE, RpcCallBack);
    return 1;
}

/* 0x5d8 `start`: !module_init() = 0 (not resident; no library entries, no thread until the Init RPC) */
int ModuleStart(const iopk::ModuleArgs *m)
{
    ResetState();
    g_base = m ? m->base : 0;
    return module_init() == 0;
}

/* ---------------------------------------------------------------- thread / sema helpers */

/* 0x5fc: param block at sp+0x1c; CreateThread's result unchanged (id or negative) */
int MakeThread(iopk::ThreadEntry entry, int prio)
{
    iopk::ThreadParam p;
    p.attr = 0x02000000;
    p.option = 0;
    p.entry = entry;
    p.stack_size = 0x800;
    p.priority = prio;
    return iopk::CreateThread(&p);
}

/* 0x638 (f_0638): attr 1, init 0, the id stored through the pointer and returned; 0 pointer -> -1 */
int MakeSema(int32_t *id, int max)
{
    if (!id) return -1;
    iopk::SemaParam p;
    p.attr = 1;
    p.option = 0;
    p.init_count = 0;
    p.max_count = max;
    *id = iopk::CreateSema(&p);
    return *id;
}

/* ---------------------------------------------------------------- command queue */

/* 0x153c: the full test is outside the lock (Q-1); slot = (head + count) % 2 (signed; never negative in practice) */
int _AddCommand(int track, int cmd)
{
    if (track < 0 || track >= 8) return 0;
    CmdQueue *q = &s_IvagCmdQueue[track];
    if (q->count >= 2) return 0;
    iopk::WaitSema(s_IVagMng.semQ);
    int i = (q->head + q->count) % 2;
    q->cmd[i] = cmd;
    q->count++;
    iopk::SignalSema(s_IVagMng.semQ);
    iopk::SignalSema(s_IVagMng.semCmd);
    return 1;
}

/* 0x1620: 0 for an empty queue and for a bad track alike (Q-2) */
int _RemoveCommand(int track)
{
    if (track < 0 || track >= 8) return 0;
    iopk::WaitSema(s_IVagMng.semQ);
    CmdQueue *q = &s_IvagCmdQueue[track];
    int r = 0;
    if (q->count > 0) {
        r = q->cmd[q->head];
        q->cmd[q->head] = 0;
        q->count--;
        q->head++;
        if (q->head >= 2) q->head = 0;
    }
    iopk::SignalSema(s_IVagMng.semQ);
    return r;
}

/* ---------------------------------------------------------------- threads */

/* 0x29f0 (prio 0x55): woken by the sd handlers' iSignalSema; the flag bytes are cleared by the callees */
void _ThreadProc(void *)
{
    for (;;) {
        iopk::WaitSema(s_IVagMng.semIsr);
        iopk::WaitSema(s_IVagMng.semOp);
        switch (s_IVagMng.mode) {
        case 1:
            if (s_ucTransFlag) _AfterTransProc();
            break;
        case 2:
            if (s_ucIrqFlag) _IRQProc();
            break;
        default: break;
        }
        iopk::SignalSema(s_IVagMng.semOp);
    }
}

/* 0x2aa4 (prio 0x1a): one command per track per wake, tracks 0..7; the setup runs here, after its RPC returned (H2) */
void _CommandProc(void *)
{
    for (;;) {
        iopk::WaitSema(s_IVagMng.semCmd);
        for (int t = 0; t < 8; t++) {
            switch (_RemoveCommand(t)) {
            case 4: _CommandClose(t); break;
            case 3: _CommandPause(t); break;
            case 2: _CommandPlay(t); break;
            case 1: {
                auto &p = s_IVagStatePack.req[t];
                if (p.w0 != -1) {
                    p.w1 = ivagSetup(t, p.w0);
                    p.w0 = -1;
                }
                break;
            }
            default: break;
            }
        }
    }
}

/* ---------------------------------------------------------------- init + G1 setters */

static const char ERR[] = "%s : ERROR!!! ";

/* 0x2f28: a second call returns 0 and touches nothing (GH2). The voice slots are reserved as global voices 0..15
 * (ivagSetCoreVoice(0), H8) before the threads exist. Both threads are below the RPC thread (0x19), so StartThread
 * does not preempt (GH3). A failed _ThreadProc creation returns without cleanup; later failures clean up (REV). */
int ivagInit()
{
    if (s_iInitialize) return 0;
    IVagMng &m = s_IVagMng;
    memset(&m, 0, 0x38);
    memset(s_IVagVoice, 0, sizeof s_IVagVoice);
    memset(s_IVagTrack, 0, sizeof s_IVagTrack);
    memset(&s_IVagStatePack, 0, sizeof s_IVagStatePack);
    memset(s_IvagCmdQueue, 0, sizeof s_IvagCmdQueue);
    m.dmaCh = 0;
    m.spuBase = 0x5010;
    m.spuSize = 0x40000;
    m.masterL = 0x3fff;
    m.masterR = 0x3fff;
    m.irqTrack = 0xff;
    m.transTrack = 0xff;
    for (int i = 0; i < 16; i++) {
        s_IVagVoice[i].core = 0xff;
        s_IVagVoice[i].voice = 0xff;
    }
    ivagSetCoreVoice(0);
    for (int i = 0; i < 8; i++) {
        s_IVagStatePack.req[i].w1 = 0;
        s_IVagStatePack.req[i].w0 = -1;
    }
    m.tid = -1;
    s_iSetupThreadID = -1;
    m.semIsr = m.semOp = m.semCmd = m.semQ = -1;
    for (int i = 7; i >= 0; i--) s_IVagSema[i] = -1;

    int r;
    m.tid = MakeThread(_ThreadProc, 0x55);
    if (m.tid < 0) {
        iopk::Printf(ERR, "ivagInit");
        iopk::Printf("Can't creaet thread... ret=%d\n", m.tid);
        return 0;
    }
    s_iSetupThreadID = MakeThread(_CommandProc, 0x1a);
    if (s_iSetupThreadID < 0) {
        iopk::Printf(ERR, "ivagInit");
        iopk::Printf("Can't creaet setup thread... ret=%d\n", s_iSetupThreadID);
        goto cleanup;
    }
    if ((r = MakeSema(&m.semIsr, 1)) < 0) {
        iopk::Printf(ERR, "ivagInit");
        iopk::Printf("Can't create semaphore... ret=%d\n", r);
        goto cleanup;
    }
    if ((r = MakeSema(&m.semOp, 1)) < 0) {
        iopk::Printf(ERR, "ivagInit");
        iopk::Printf("Can't create semaphore for operation... ret=%d\n", r);
        goto cleanup;
    }
    iopk::SignalSema(m.semOp);
    if ((r = MakeSema(&m.semCmd, 0x10)) < 0) {
        iopk::Printf(ERR, "ivagInit");
        iopk::Printf("Can't create semaphore for command thread... ret=%d\n", r);
        goto cleanup;
    }
    if ((r = MakeSema(&m.semQ, 1)) < 0) {
        iopk::Printf(ERR, "ivagInit");
        iopk::Printf("Can't create semaphore for command queue... ret=%d\n", r);
        goto cleanup;
    }
    iopk::SignalSema(m.semQ);
    for (int i = 0; i < 8; i++) {
        if ((r = MakeSema(&s_IVagSema[i], 1)) < 0) {
            iopk::Printf(ERR, "ivagInit");
            iopk::Printf("Can't create semaphore for track ctrl... ret=%d\n", r);
            goto cleanup;
        }
        iopk::SignalSema(s_IVagSema[i]);
    }
    if ((r = iopk::StartThread(m.tid, 0)) < 0) {
        iopk::Printf(ERR, "ivagInit");
        iopk::Printf("Can't start thread... ret=%d\n", r);
        goto cleanup;
    }
    if ((r = iopk::StartThread(s_iSetupThreadID, 0)) < 0) {
        iopk::Printf(ERR, "ivagInit");
        iopk::Printf("Can't start setup thread... ret=%d\n", r);
        goto cleanup;
    }
    SpuMmng_Init(m.spuBase, m.spuSize);
    ivagSetPacketBuffer(0x2000);   /* result ignored */
    m.mode = 0;
    s_iInitialize = 1;
    return 1;

cleanup:
    if (m.tid >= 0) {
        iopk::TerminateThread(m.tid);
        iopk::DeleteThread(m.tid);
    }
    if (s_iSetupThreadID >= 0) {
        iopk::TerminateThread(s_iSetupThreadID);
        iopk::DeleteThread(s_iSetupThreadID);
    }
    if (m.semIsr >= 0) iopk::DeleteSema(m.semIsr);
    if (m.semOp >= 0) iopk::DeleteSema(m.semOp);
    if (m.semCmd >= 0) iopk::DeleteSema(m.semCmd);
    if (m.semQ >= 0) iopk::DeleteSema(m.semQ);
    for (int i = 0; i < 8; i++)
        if (s_IVagSema[i] >= 0) iopk::DeleteSema(s_IVagSema[i]);
    return 0;
}

/* 0x3db0: frees the old buffer first; size <= 0 leaves none; a failed alloc leaves packetBuf 0 and packetSize as it
 * was (Q-5). Init's 0x2000 then the EE's 0x1000 land at the same first-fit address (GH4 / H7: the trace's madr). */
int ivagSetPacketBuffer(int size)
{
    IVagMng &m = s_IVagMng;
    if (m.mode != 0) {
        iopk::Printf(ERR, "ivagSetPacketBuffer");
        iopk::Printf("VAG stream system is busy...stat=%d\n", m.mode);
        return 0;
    }
    if (m.packetBuf) {
        iopk::FreeSysMemory(m.packetBuf);
        m.packetBuf = 0;
    }
    if (size > 0) {
        m.packetBuf = iopk::AllocSysMemory(0, (uint32_t)size, 0);
        if (!m.packetBuf) {
            iopk::Printf(ERR, "ivagSetPacketBuffer");
            iopk::Printf("Can't allocate IOP memory...size=%d\n", size);
            return 0;
        }
    }
    m.packetSize = size;
    return 1;
}

/* 0x3e90: signed addr + size < 0x200000 (Q-4); resets the allocator even with tracks alive (GH7) */
int ivagSetSpuMemory(int addr, int size)
{
    IVagMng &m = s_IVagMng;
    if (m.mode != 0) {
        iopk::Printf(ERR, "ivagSetSpuMemory");
        iopk::Printf("VAG stream system is busy...stat=%d\n", m.mode);
        return 0;
    }
    if (!((int32_t)((uint32_t)addr + (uint32_t)size) < 0x200000)) {
        iopk::Printf(ERR, "ivagSetSpuMemory");
        iopk::Printf("Invalid spu memory size...iSpuAddr=%d, iSize=%d\n", addr, size);
        return 0;
    }
    m.spuBase = addr;
    m.spuSize = size;
    SpuMmng_Init(addr, size);
    return 1;
}

/* 0x4fb4 */
void *ivagGetTrackStat(int *outsize)
{
    *outsize = 0x48;
    return &s_IVagStatePack;
}

/* 0x4fc8: slot i takes global voice base + i (core = g / 24, voice = g % 24, signed div, stored as bytes; fssd ignores
 * g >= 0x30 but the slot still records core 2: Q-3); the slot's previous voice is released first when both bytes are
 * set; one Validate (the push into hsyn) at the end */
void ivagSetCoreVoice(int base)
{
    for (int i = 0; i < 16; i++) {
        IVagVoice *v = &s_IVagVoice[i];
        int g = (int)((uint32_t)base + (uint32_t)i);
        if (v->core != 0xff && v->voice != 0xff) fssd::HsynSetReservVoiceFlag(v->voice + v->core * 24, 0);
        v->core = (uint8_t)(g / 24);
        v->voice = (uint8_t)(g % 24);
        fssd::HsynSetReservVoiceFlag(g, 1);
    }
    fssd::HsynValidateReservVoice();
}

/* 0x5290 */
void ivagSetDmaChannel(int ch)
{
    s_IVagMng.dmaCh = (int16_t)(ch & 1);
}

}  // namespace ivag
