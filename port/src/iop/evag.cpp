/* evag G1: module start, the RPC ladder, the lifecycle API (Init / Setup /
 * Close / Pause / Volume / SetCoreVoice / SetSpu2Addr / GetStatus / TransFinish), the play-thread / sema helpers, the
 * two sd callbacks and _SetVoiceAttr (called by G2's vagStart). Offsets = the original's module offsets. Only the init
 * path (0x001 -> vagInit -> vagSetCoreVoice) runs in Versus; the rest is REV, checked by evag::UnitTest. All
 * arithmetic is the R3000's (32-bit, signed compares where the asm uses slt / blez). */
#include "evag_state.h"
#include "fssd.h"
#include "../../platform/plat.h"
#include <string.h>

namespace evag {

uint8_t s_ucSafeIttr, s_ucEmergency, s_ucSifdmaFlag, s_ucTransFlag, s_ucIrqFlag;
VagThread s_itpData;
VagMng s_ivmData;
int32_t s_iRpcRet;

/* ---------------------------------------------------------------- sd / fssd seam */

static const Io *g_io;
void SetIo(const Io *io) { g_io = io; }

void IoSetParam(uint16_t entry, uint16_t val)
{
    if (!g_io) sd::SetParam(entry, val);
    else if (g_io->setParam) g_io->setParam(g_io->user, entry, val);
}
void IoSetSwitch(uint16_t entry, uint32_t val)
{
    if (!g_io) sd::SetSwitch(entry, val);
    else if (g_io->setSwitch) g_io->setSwitch(g_io->user, entry, val);
}
uint32_t IoGetSwitch(uint16_t entry)
{
    if (!g_io) return sd::GetSwitch(entry);
    return g_io->getSwitch ? g_io->getSwitch(g_io->user, entry) : 0;
}
void IoSetAddr(uint16_t entry, uint32_t val)
{
    if (!g_io) sd::SetAddr(entry, val);
    else if (g_io->setAddr) g_io->setAddr(g_io->user, entry, val);
}
uint32_t IoGetAddr(uint16_t entry)
{
    if (!g_io) return sd::GetAddr(entry);
    return g_io->getAddr ? g_io->getAddr(g_io->user, entry) : 0;
}
void IoSetCoreAttr(uint16_t entry, uint16_t val)
{
    if (!g_io) sd::SetCoreAttr(entry, val);
    else if (g_io->setCoreAttr) g_io->setCoreAttr(g_io->user, entry, val);
}
uint32_t IoGetCoreAttr(uint16_t entry)
{
    if (!g_io) return sd::GetCoreAttr(entry);
    return g_io->getCoreAttr ? g_io->getCoreAttr(g_io->user, entry) : 0;
}
int IoVoiceTrans(int ch, uint16_t mode, uint32_t iop, uint32_t spu, uint32_t size)
{
    if (!g_io) return sd::VoiceTrans(ch, mode, iopk::Ptr(iop), spu, size);
    return g_io->voiceTrans ? g_io->voiceTrans(g_io->user, ch, mode, iop, spu, size) : 0;
}
int IoVoiceTransStatus(int ch, int wait)
{
    if (!g_io) return sd::VoiceTransStatus(ch, wait);
    return g_io->voiceTransStatus ? g_io->voiceTransStatus(g_io->user, ch, wait) : 0;
}
void IoSetTransIntrHandler(int ch, sd::IntrHandler fn)
{
    if (!g_io) sd::SetTransIntrHandler(ch, fn, 0);
    else if (g_io->setTransIntrHandler) g_io->setTransIntrHandler(g_io->user, ch, fn, 0);
}
void IoSetSpu2IntrHandler(sd::IntrHandler fn)
{
    if (!g_io) sd::SetSpu2IntrHandler(fn, 0);
    else if (g_io->setSpu2IntrHandler) g_io->setSpu2IntrHandler(g_io->user, fn, 0);
}
/* fsiopsdCallRpc(3, wait = 1, 0x2040000, pos, size): w[2..7] are the caller's frame residue in the original (E5, never
 * recorded, the EE ignores them): 0 here */
void IoCallRpcRead(uint32_t pos, uint32_t size)
{
    uint32_t w[8] = {pos, size, 0, 0, 0, 0, 0, 0};
    if (!g_io) fssd::CallRpc(3, 1, 0x2040000u, w);
    else if (g_io->callRpc) g_io->callRpc(g_io->user, w);
}

uint16_t VoiceEntry(uint32_t add)
{
    return (uint16_t)((uint32_t)((int32_t)s_ivmData.core | ((int32_t)s_ivmData.voice << 1)) | add);
}

void Wait63us()
{
    for (;;) {
        iopk::SysClock c;
        uint32_t sec, usec;
        iopk::GetSystemTime(&c);
        iopk::SysClock2USec(&c, &sec, &usec);
        if (sec != s_ivmData.sec || (int32_t)(usec - s_ivmData.usec) >= 0x3f) break;
        iopk::WaitNextEvent();
    }
}

void StampTime()
{
    iopk::SysClock c;
    iopk::GetSystemTime(&c);
    iopk::SysClock2USec(&c, &s_ivmData.sec, &s_ivmData.usec);
}

/* ---------------------------------------------------------------- thread / sema helpers */

/* 0x1f8: attr 1, init 0, max 1; option = stack residue in the original (Q-E7), 0 here */
int MakePlaySem()
{
    iopk::SemaParam p;
    p.attr = 1;
    p.option = 0;
    p.init_count = 0;
    p.max_count = 1;
    return iopk::CreateSema(&p);
}

/* 0x228 */
int MakePlayThread(iopk::ThreadEntry entry, int prio)
{
    iopk::ThreadParam p;
    p.attr = 0x02000000;
    p.option = 0;
    p.entry = entry;
    p.stack_size = 0x800;
    p.priority = prio;
    return iopk::CreateThread(&p);
}

/* 0x264: the quit signal lets the (better-priority) play thread return before DeleteThread (E9); no exit re-check */
void DeletePlayThread(VagThread *itp)
{
    if (itp->tid > 0) {
        itp->quit = 1;
        iopk::SignalSema(itp->sema);
        iopk::DeleteThread(itp->tid);
    }
    if (itp->sema > 0) iopk::DeleteSema(itp->sema);
    if (itp->sema2 > 0) iopk::DeleteSema(itp->sema2);
    itp->tid = 0;
    itp->sema = 0;
    itp->sema2 = 0;
    itp->quit = 0;
}

/* ---------------------------------------------------------------- sd callbacks */

/* 0x2f0: SPU DMA (ch 0) done; registered by _vagAfterReadFirst, cleared by Operate / Close */
int _IntrTransCB(int core, void *arg)
{
    (void)core;
    (void)arg;
    s_ucTransFlag = 1;
    iopk::iSignalSema(s_itpData.sema);
    return 0;
}

/* 0x324: SPU IRQ: IRQ off first, then an "emergency" count before vagStart finished, else wake the play thread (Q-E3) */
int _IntrSpu2CB(int core, void *arg)
{
    (void)core;
    (void)arg;
    IoSetCoreAttr((uint16_t)(s_ivmData.core | 4), 0);
    if (s_ucSafeIttr == 0) {
        s_ucEmergency++;
        return 0;
    }
    s_ucIrqFlag = 1;
    iopk::iSignalSema(s_itpData.sema);
    return 0;
}

/* 0x3b8: voice attributes before KON (vagStart); pitch = (rate << 12) / 48000 (signed div, always positive); VMIX clear
 * without CpuSuspendIntr */
void _SetVoiceAttr()
{
    int32_t pitch = (int32_t)((uint32_t)s_ivmData.rate << 12) / 0xbb80;
    IoSetParam(VoiceEntry(0), 0);
    IoSetParam(VoiceEntry(0x100), 0);
    IoSetParam(VoiceEntry(0x200), (uint16_t)pitch);
    IoSetParam(VoiceEntry(0x300), 0x80ff);
    IoSetParam(VoiceEntry(0x400), 0xdfe0);
    IoSetAddr(VoiceEntry(0x2040), s_ivmData.spu);
    uint32_t nbit = ~(1u << (s_ivmData.voice & 31));
    IoSetSwitch((uint16_t)(s_ivmData.core | 0x1900), IoGetSwitch((uint16_t)(s_ivmData.core | 0x1900)) & nbit);
    IoSetSwitch((uint16_t)(s_ivmData.core | 0x1b00), IoGetSwitch((uint16_t)(s_ivmData.core | 0x1b00)) & nbit);
}

/* ---------------------------------------------------------------- API */

/* 0x1aec: reserve the voice in hsyn (through fssd) and take it out of the dry / wet voice mix of its core; the mask
 * index is the core read back unsigned (lbu), the bit from the signed voice (lb, sllv) */
void vagSetCoreVoice(int core, int voice)
{
    uint32_t mask[2] = {0, 0};
    s_ivmData.core = (int8_t)(core & 1);
    s_ivmData.voice = (int8_t)(voice % 24);
    uint32_t bit = 1u << (s_ivmData.voice & 31);
    mask[(uint8_t)s_ivmData.core] = bit;
    int st;
    iopk::CpuSuspendIntr(&st);
    fssd::HsynSetReservVoice(mask);
    IoSetSwitch((uint16_t)(s_ivmData.core | 0x1900), IoGetSwitch((uint16_t)(s_ivmData.core | 0x1900)) & ~bit);   /* VMIXL */
    IoSetSwitch((uint16_t)(s_ivmData.core | 0x1b00), IoGetSwitch((uint16_t)(s_ivmData.core | 0x1b00)) & ~bit);   /* VMIXR */
    iopk::CpuResumeIntr(st);
}

/* 0x1078: the thread record is cleared too, forgetting the thread / sema ids (Q-E4) */
int vagInit()
{
    memset(&s_ivmData, 0, sizeof s_ivmData);
    vagSetCoreVoice(1, 0x17);
    s_ivmData.spu = 0x1e0000;
    memset(&s_itpData, 0, sizeof s_itpData);
    return 1;
}

/* 0x10d0: on the fssd RPC thread; the play thread (0x54) first runs when this thread blocks (E1). The first EE read
 * request blocks (wait 1, E6); status 0x4002 is stored after it. A failing CreateSema / CreateThread leaves the
 * negative id stored, sets quit and replies 0 with status unchanged (Q-E7). */
int vagSetup(uint32_t buf, uint32_t total, uint32_t rate, uint32_t half)
{
    VagMng &m = s_ivmData;
    if (m.status != 0) {
        iopk::Printf("<vagSetup> vag module is busy. - pivm->stat=%d\n", (int)m.status);
        return 0;
    }
    m.buf = buf;
    if (s_itpData.sema == 0) {
        int id = MakePlaySem();
        s_itpData.sema = id;
        if (id < 0) {
            s_itpData.quit = 1;
            return 0;
        }
    }
    if (s_itpData.tid == 0) {
        int id = MakePlayThread(_vagProc, 0x54);
        s_itpData.tid = id;
        if (id < 0) {
            s_itpData.quit = 1;
            return 0;
        }
        s_itpData.quit = 0;
        iopk::StartThread(id, 0);
    }
    s_ucIrqFlag = 0;
    s_ucSifdmaFlag = 0;
    m.total = total;
    m.rate = (uint16_t)rate;
    m.half = half;
    m.xfer = 0;
    m.rd = 0;
    m.played = 0;
    m.toggle &= (uint8_t)~1;
    if (!(m.status & 0x4000)) {
        /* n = 2 * half if that is (signed) below what is left, else the rest when > 0; 2 * half <= 0 below a positive
         * rest still requests (half 0 -> a 0-byte request) */
        int32_t twoh = (int32_t)(m.half << 1);
        int32_t rem = (int32_t)(m.total - m.rd);
        if (twoh < rem) IoCallRpcRead(m.rd, (uint32_t)twoh);
        else if (rem > 0) IoCallRpcRead(m.rd, (uint32_t)rem);
    }
    m.status = 0x4002;
    return 1;
}

/* 0x1280: lh (sign-extended reply) */
int vagGetStatus() { return s_ivmData.status; }

/* 0x1290: the EE's data arrived: flag, FlushDcache, wake the play thread (thread context: SignalSema) */
void vagTransFinish()
{
    s_ucSifdmaFlag = 1;
    iopk::FlushDcache();
    iopk::SignalSema(s_itpData.sema);
}

/* 0x17bc: mute now, request the KOFF through op (done by _vagOperateProc at the next IRQ, Q-E5); states 0xa, 7, 5, 4 */
int vagPause()
{
    int st = s_ivmData.status & 0xfff;
    if (st == 0xa || st == 7 || st == 5 || st == 4) {
        IoSetParam(VoiceEntry(0), 0);
        IoSetParam(VoiceEntry(0x100), 0);
        s_ivmData.op = (int16_t)(s_ivmData.op | 2);
        s_ivmData.op = (int16_t)(s_ivmData.op | 0x1000);
        return 1;
    }
    return 0;
}

/* 0x1898: state 8 (Operate already stopped the voice) skips the sd teardown (Q-E8); the caller drops below the play
 * thread so it can see quit and return, then the priority is set back to 0x19 unconditionally */
int vagClose()
{
    VagMng &m = s_ivmData;
    if (m.status == 0) return 0;
    if (m.status != 8) {
        if (IoVoiceTransStatus(0, 0) != 1) IoVoiceTransStatus(0, 1);   /* a1 = the residue 1 (O2) */
        IoSetCoreAttr((uint16_t)(m.core | 4), 0);
        IoSetSpu2IntrHandler(0);
        IoSetTransIntrHandler(0, 0);
        Wait63us();
        IoSetParam(VoiceEntry(0), 0);
        IoSetParam(VoiceEntry(0x100), 0);
        IoSetSwitch((uint16_t)(m.core | 0x1600), 1u << (m.voice & 31));   /* KOFF */
        StampTime();
    }
    m.status = 0;
    m.op = 0;
    iopk::ChangeThreadPriority(0, 0x5c);
    DeletePlayThread(&s_itpData);
    iopk::ChangeThreadPriority(0, 0x19);
    return 1;
}

/* 0x1a60: stored and applied even when idle; the ladder does not store the result (Q-E5) */
int vagVolume(uint16_t l, uint16_t r)
{
    s_ivmData.volL = l;
    s_ivmData.volR = r;
    IoSetParam(VoiceEntry(0), s_ivmData.volL);
    IoSetParam(VoiceEntry(0x100), s_ivmData.volR);
    return 1;
}

/* 0x1bf8 */
void vagSetSpu2Addr(uint32_t a) { s_ivmData.spu = a; }

/* ---------------------------------------------------------------- front */

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

/* 0x1c04: cmd = fno & 0x0fffffff; reply = s_iRpcRet (4 bytes); payload words read unchecked (size ignored). The
 * original's if-chain order (209, 206, 208, 207, 203, 202, 205, 201, 200, 001) has no side effects: a switch. */
static void *RpcCallBack(uint32_t cmd, void *buf, int size, int *outsize)
{
    (void)size;
    const uint8_t *a = (const uint8_t *)buf;
    s_iRpcRet = 0;
    *outsize = 4;
    switch (cmd & 0x0fff0000u) {
    case 0x0010000: s_iRpcRet = vagInit(); break;
    case 0x2000000: s_iRpcRet = vagSetup(rd32(a), rd32(a + 4), rd32(a + 8), rd32(a + 12)); break;
    case 0x2010000: s_iRpcRet = vagStart(); break;
    case 0x2050000: vagTransFinish(); break;
    case 0x2020000: vagSetCoreVoice((int)rd32(a), (int)rd32(a + 4)); break;
    case 0x2030000: vagSetSpu2Addr(rd32(a)); break;
    case 0x2070000: s_iRpcRet = vagGetStatus(); break;
    case 0x2080000: vagVolume(rd16(a), rd16(a + 4)); break;   /* not stored: reply 0 */
    case 0x2060000: s_iRpcRet = vagClose(); break;
    case 0x2090000: s_iRpcRet = vagPause(); break;
    default: s_iRpcRet = -1; break;
    }
    return &s_iRpcRet;
}

/* 0x1d0 (statics cleared first: the zeroed image at load); no thread / sema here (Setup creates them) */
int ModuleStart(const iopk::ModuleArgs *m)
{
    (void)m;
    s_ucSafeIttr = s_ucEmergency = s_ucSifdmaFlag = s_ucTransFlag = s_ucIrqFlag = 0;
    memset(&s_itpData, 0, sizeof s_itpData);
    memset(&s_ivmData, 0, sizeof s_ivmData);
    s_iRpcRet = 0;
    fssd::Register(3, RpcCallBack);
    return 0;
}

}  // namespace evag
