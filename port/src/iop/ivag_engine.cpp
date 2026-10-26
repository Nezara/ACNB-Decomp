/* ivag ring engine: the SPU-IRQ / DMA-end double-buffered ring
 * streamer. Each track plays a ring of 2 * half bytes per channel in SPU RAM; the DMA fills one half in 0x400 chunks
 * (one per channel, round robin) while the voice plays the other, and the SPU IRQ address marks the end of the half
 * being played. Two sd interrupt handlers only set a flag byte and iSignalSema the ISR sema; _ThreadProc (ivag.cpp, prio
 * 0x55, under the op lock) then runs _AfterTransProc (the DMA-end continuation: next chunk / next track / re-arm the
 * IRQ) or _IRQProc (flip the halves, play accounting, _Operate, first DMA of the next round). _TransTrackProc reads one
 * chunk from FsMFStrm into the packet buffer (_ReadTrackData), rewrites the ADPCM loop-flag bytes (_SetADPCMLoop + the
 * ring start / end / file end fix-ups: the trace's DMA crc is of this post-processed data) and starts sceSdVoiceTrans.
 * The per-track semaphore is held from the first DMA of a round until _AfterTransProc ends the round.
 * _WaitDmaTrans / _GetSystemTime / _Wait2Ticks are _Operate's (G3) helpers.
 *
 * Live in the --snd-selftest replay since G4: the first setup's fill chain (auto_arenaret1 vb 1874); the play / IRQ
 * rounds need G3 (_CommandPlay / _Operate, map 18.6): checked by ivag::UnitTest through the EngineIo seam. All arithmetic is the R3000's: 32-bit
 * wrapping adds / mult lo, `div` + mfhi with the no-trap divide-by-zero result (rem32), signed compares. Function
 * comments give the original's image offset. */
#include "ivag_state.h"
#include "mfstrm.h"
#include "fssd.h"
#include "../../platform/snd/iop/iopdiv.h"

namespace ivag {

/* ---------------------------------------------------------------- sd / FsMFStrm seam */

static const EngineIo *g_io;
void SetEngineIo(const EngineIo *io) { g_io = io; }

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
void IoCallRpc(const uint32_t w[8])   /* fsiopsdCallRpc(10, 0 = no-wait, 0x1000000, ...) */
{
    if (!g_io) fssd::CallRpc(MODULE, 0, 0x01000000u, w);
    else if (g_io->callRpc) g_io->callRpc(g_io->user, w);
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
void IoSetCoreAttr(uint16_t entry, uint16_t val)
{
    if (!g_io) sd::SetCoreAttr(entry, val);
    else if (g_io->setCoreAttr) g_io->setCoreAttr(g_io->user, entry, val);
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
int IoIsAttached(int idx)
{
    if (!g_io) return mfstrm::mstrm_IsAttached(idx);
    return g_io->isAttached ? g_io->isAttached(g_io->user, idx) : 0;
}
int IoGetValidDataSize(int idx)
{
    if (!g_io) return mfstrm::mstrm_GetValidDataSize(idx);
    return g_io->getValidDataSize ? g_io->getValidDataSize(g_io->user, idx) : 0;
}
int IoGetData(int idx, uint32_t dst, int n)
{
    if (!g_io) return mfstrm::mstrm_GetData(idx, dst, n);
    return g_io->getData ? g_io->getData(g_io->user, idx, dst, n) : 0;
}
int IoReleaseData(int idx, int n)   /* the RemoveData import */
{
    if (!g_io) return mfstrm::mstrm_ReleaseData(idx, n);
    return g_io->releaseData ? g_io->releaseData(g_io->user, idx, n) : 0;
}
void IoDebugOut()
{
    if (!g_io) mfstrm::mstrm_DebugOut();
    else if (g_io->debugOut) g_io->debugOut(g_io->user);
}
int IoGetDataSync(int idx, uint32_t dst, int n)
{
    if (!g_io) return mfstrm::mstrm_GetDataSync(idx, dst, n);
    return g_io->getDataSync ? g_io->getDataSync(g_io->user, idx, dst, n) : 0;
}
int IoGetStreamBufferSize(int idx)
{
    if (!g_io) return mfstrm::mstrm_GetStreamBufferSize(idx);
    return g_io->getStreamBufferSize ? g_io->getStreamBufferSize(g_io->user, idx) : 0;
}
int IoSeek(int idx, int pos)
{
    if (!g_io) return mfstrm::mstrm_Seek(idx, pos);
    return g_io->seek ? g_io->seek(g_io->user, idx, pos) : 0;
}
int IoSetLoopPoint(int idx, int start, int end, int loop)
{
    if (!g_io) return mfstrm::mstrm_SetLoopPoint(idx, start, end, loop);
    return g_io->setLoopPoint ? g_io->setLoopPoint(g_io->user, idx, start, end, loop) : 0;
}

/* ---------------------------------------------------------------- R3000 arithmetic */

static int32_t add32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
static int32_t sub32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); }
static int32_t mul32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }   /* mult, mflo */
/* div + mfhi (no guard in FSSDIVAG): IOP divide-by-zero rule, iopdiv.h (b == 0 -> hi = the dividend, INT_MIN / -1 ->
 * hi 0, one plat_log per site) */

/* packet-buffer bytes by IOP address (GH2-6: the fix-up offsets are relative to Mng.packetBuf and may leave the buffer
 * on REV paths; addresses outside IOP RAM are dropped instead of faulting) */
static void poke(uint32_t a, uint8_t v)
{
    uint8_t *p = iopk::Ptr(a);
    if (p) *p = v;
}

/* t->vp[t->cur]: the original indexes the 2-slot array with cur unchecked (cur < nch; nch > 2 would read the +0x0c..
 * words as voice pointers, Q-G2f); a host pointer exists only for slots 0 / 1 */
static IVagVoice *cur_vp(const IVagTrack *t) { return t->cur == 0 || t->cur == 1 ? t->vp[t->cur] : 0; }

static const char ERR[] = "%s : ERROR!!! ";
/* D_054f4: Shift-JIS "ADPCM data transfer packet buffer is not allocated" */
static const char s_NoPacketBuffer[] =
    "\x41\x44\x50\x43\x4d\x83\x66\x81\x5b\x83\x5e\x93\x5d\x91\x97\x97\x70\x83\x70\x83\x50\x83\x62\x83\x67\x83\x6f\x83"
    "\x62\x83\x74\x83\x40\x82\xaa\x8a\x6d\x95\xdb\x82\xb3\x82\xea\x82\xc4\x82\xa2\x82\xdc\x82\xb9\x82\xf1\x0a";

/* ---------------------------------------------------------------- interrupt handlers */

/* 0xd70: sd's DmaIsr calls it as trans_handler[core](core, arg) after the DMA end. The flag is stored before the
 * handler removes itself; the ISR sema has max 1, so a second wake before ThreadProc runs is lost (original). */
int _SpuTransIntrHandler(int core, void *)
{
    if (core == s_IVagMng.dmaCh) {
        s_ucTransFlag = 1;
        IoSetTransIntrHandler(s_IVagMng.dmaCh, 0);
        iopk::iSignalSema(s_IVagMng.semIsr);
    }
    return 0;
}

/* 0xdc8: sd's Spu2Isr calls it after clearing the core's ATTR IRQ-enable bit (the trace's ATTR c081 line); the
 * SetCoreAttr(core | 4 = IRQ enable, 0) here clears the already clear bit again (Q-G2h, no trace line). Every playing
 * track's half is released for the next fill. */
int _Spu2IntrHandler(int, void *)
{
    IVagMng &m = s_IVagMng;
    if (m.irqTrack == 0xff) return 0;   /* REV */
    IoSetCoreAttr((uint16_t)((s_IVagTrack[m.irqTrack].vp[0]->core | 4) & 0xffff), 0);
    for (int i = 0; i < 8; i++) {
        IVagTrack *t = &s_IVagTrack[i];
        if ((t->flags & 1) && t->state == 4) t->needTrans = 0;
    }
    s_ucIrqFlag = 1;
    iopk::iSignalSema(m.semIsr);
    return 0;
}

/* ---------------------------------------------------------------- _Operate's helpers */

/* 0xe98: a tail jump; blocks on sd's DMA event flag (thread context only, H10) */
int _WaitDmaTrans()
{
    return IoVoiceTransStatus(s_IVagMng.dmaCh, 1);
}

/* 0xeac: the stamp _Wait2Ticks measures from */
void _GetSystemTime()
{
    iopk::SysClock c;
    iopk::GetSystemTime(&c);
    iopk::SysClock2USec(&c, (uint32_t *)&s_IVagMng.sec, (uint32_t *)&s_IVagMng.usec);
}

/* 0xee4: the 43 us busy wait from the stamp, as a WaitNextEvent loop (iopk Guard 1): the test runs first (no wait when
 * 43 us have passed); the elapsed time wraps at 32 bits (* 1000000 = the shift chain) and a negative value ends the
 * wait at once (Q-G2j). Moves T to the next event (in arenaret1: the next 16-sample chunk). */
void _Wait2Ticks()
{
    for (;;) {
        iopk::SysClock c;
        uint32_t sec, usec;
        iopk::GetSystemTime(&c);
        iopk::SysClock2USec(&c, &sec, &usec);
        int32_t e = (int32_t)((usec - (uint32_t)s_IVagMng.usec) + (sec - (uint32_t)s_IVagMng.sec) * 1000000u);
        if (e < 0 || e >= 43) break;
        iopk::WaitNextEvent();
    }
}

/* ---------------------------------------------------------------- ring helpers */

/* 0xf68: byte 1 (the loop-flag byte) of every 16-byte ADPCM frame = 2 (repeat); signed compare, n <= 0 does nothing */
void _SetADPCMLoop(uint32_t buf, int n)
{
    for (int i = 0; i < n; i += 16) poke(buf + (uint32_t)i + 1, 2);
}

/* 0x150c: toggles the half-select bit */
void _FlipDBuff(IVagTrack *t)
{
    t->flags = (uint8_t)((t->flags & ~2) | ((((t->flags >> 1) & 1) == 0) << 1));
}

/* 0x1de0: releases the track lock _StartTransTrackProc took (a second release is KE_SEMA_OVF, count unchanged) */
void _FinishTransTrackProc(int track)
{
    if (track < 0 || track >= 8) return;   /* REV */
    iopk::SignalSema(s_IVagSema[track]);
}

/* 0x1d64: takes the track lock for the round; kept while the DMA chain runs (released by _AfterTransProc at the round
 * end, GH2-3), released at once when nothing was sent */
int _StartTransTrackProc(int track)
{
    if (track < 0 || track >= 8) return 0;   /* REV */
    iopk::WaitSema(s_IVagSema[track]);
    int r = _TransTrackProc(track);
    if (r == 0) _FinishTransTrackProc(track);
    return r;
}

/* 0x2708: the IRQ master = the playing track with the most data left (strict <: ties keep the lower index; rem <= 0
 * never wins), else (withReady) the LAST set-up track (Q-G2g), else 0xff */
void _UpdateMasterTrack(int withReady)
{
    int best = 0xff, ready = 0xff;
    int32_t maxRem = 0;
    for (int i = 0; i < 8; i++) {
        IVagTrack *t = &s_IVagTrack[i];
        if (!(t->flags & 1)) continue;
        if (t->state == 4) {
            int32_t rem = sub32(t->vp[0]->fileSize, t->vp[0]->off);
            if (maxRem < rem) {
                maxRem = rem;
                best = i;
            }
        }
        if (withReady && t->state == 2) ready = i;
    }
    if (best == 0xff) best = ready;
    s_IVagMng.irqTrack = (uint8_t)best;
}

/* 0x27c0 (a0 ignored, Q-G2i): arms the SPU IRQ at the last frame of the half the master's voice is playing (or of the
 * file's last half). The voice's NAX (sd GetAddr: the mixer position at T, GH2-4) decides whether that point has already
 * passed: then the IRQ round is started by hand (thread-form SignalSema; REV in all runs). Otherwise IRQA (trace REG c1
 * 19c / 19e, change-only) and IRQ enable (ATTR c081 -> c0c1). */
void _SetIntrMasterTrack()
{
    IVagMng &m = s_IVagMng;
    int missed = 0;
    int it = m.irqTrack;
    if (it == 0xff) return;
    IVagTrack *t = &s_IVagTrack[it];
    IVagVoice *vp = t->vp[0];
    int32_t nax = (int32_t)IoGetAddr((uint16_t)((vp->core | vp->voice << 1 | 0x2240) & 0xffff));
    int32_t half = t->half;
    int h1 = (t->flags >> 1) & 1;
    int32_t irqOff;
    if (!(vp->off < sub32(vp->fileSize, half)))
        irqOff = add32(IOP_REM(sub32(vp->fileSize, 0x10), half, 0x2778, "ivag _SetIntrMasterTrack half 0"), mul32(h1, half));   /* last half of the file (REV) */
    else
        irqOff = sub32(mul32(h1 + 1, half), 0x10);
    if (h1) {
        if (nax < sub32(add32(vp->spu, half), 0x10)) missed = 1;   /* REV */
    } else {
        if (add32(vp->spu, irqOff) < nax && nax < sub32(add32(vp->spu, mul32(half, 2)), 0x10)) missed = 1;   /* REV */
    }
    if (missed) {
        for (int i = 0; i < 8; i++) {
            IVagTrack *u = &s_IVagTrack[i];
            if ((u->flags & 1) && u->state == 4) u->needTrans = 0;
        }
        s_ucIrqFlag = 1;
        iopk::SignalSema(m.semIsr);
        return;
    }
    int st;
    iopk::CpuSuspendIntr(&st);
    IoSetSpu2IntrHandler(_Spu2IntrHandler);
    IoSetAddr((uint16_t)((vp->core | 0x1f00) & 0xffff), (uint32_t)add32(vp->spu, irqOff));
    IoSetCoreAttr((uint16_t)((vp->core | 4) & 0xffff), 1);
    iopk::CpuResumeIntr(st);
}

/* ---------------------------------------------------------------- read + transfer */

/* 0x1700: reads the current channel's next chunk (interleave bytes, or up to the loop end) into the packet buffer and
 * advances its streamPos. Looping streams: arm 1 = the first read after the loop end was crossed (discards
 * loopS % interleave bytes, not loopS - streamPos, and does not count them: Q-G2b), arm 3 = a whole chunk before the
 * loop end, arm 2 = the read that reaches the loop end (short read, rest discarded, voice flag bit1 set, streamPos
 * rewound to the 0x800-aligned loop start). Plain streams: silence after EOF, else wait for a chunk (or the rest of the
 * file). Two loop paths return s3 without setting it (Q-G2a): the caller's s3 = _TransTrackProc's track index; the
 * arm-1 one is reachable only through 32-bit wrap (rem <= 0 contradicts its own range test otherwise). */
int _ReadTrackData(IVagTrack *t)
{
    IVagMng &m = s_IVagMng;
    IVagVoice *vp = cur_vp(t);
    int32_t s3 = (int32_t)(t - s_IVagTrack);   /* stale s3 (Q-G2a) */
    if (!m.packetBuf) {   /* REV; streamPos untouched */
        iopk::Printf(ERR, "_ReadTrackData");
        iopk::Printf(s_NoPacketBuffer);
        return 0;
    }
    if (t->flags & 4) {
        int32_t loopS = (int32_t)((uint32_t)t->loopStart << 4);
        int32_t loopE = (int32_t)((uint32_t)t->loopEnd << 4);
        int32_t il = t->interleave;
        int32_t rem = sub32(il, vp->writePos);
        if ((vp->flags & 2) && !(loopS < vp->streamPos) && loopS < add32(vp->streamPos, rem)) {
            /* arm 1 (trainret1) */
            if (rem > 0) {
                int32_t r = IOP_REM(loopS, il, 0x1798, "ivag _ReadTrackData interleave 0");
                if (r > 0) IoReleaseData(t->stream, r);   /* REV */
                while (IoGetValidDataSize(t->stream) < rem) iopk::DelayThread(16000);
                s3 = IoGetData(t->stream, m.packetBuf, rem);
            }
        } else if (add32(vp->streamPos, il) < loopE) {
            /* arm 3 (arenaret1); interleave re-read on each poll */
            int32_t n;
            for (;;) {
                n = t->interleave;
                if (!(IoGetValidDataSize(t->stream) < n)) break;
                iopk::DelayThread(4000);   /* REV */
                IoDebugOut();
            }
            s3 = IoGetData(t->stream, m.packetBuf, n);
        } else {
            /* arm 2 (trainret1) */
            int32_t rem2 = sub32(loopE, vp->streamPos);
            if (rem2 > 0) {
                while (IoGetValidDataSize(t->stream) < rem2) iopk::DelayThread(16000);
                s3 = IoGetData(t->stream, m.packetBuf, rem2);
                int32_t x = sub32(t->interleave, rem2);
                if (x > 0) IoReleaseData(t->stream, x);   /* REV */
                vp->flags = (uint8_t)((vp->flags & ~2) | 2);
            }
            vp->streamPos = sub32(loopS & ~0x7ff, s3);   /* + s3 below: loopS & ~0x7ff */
        }
    } else if (!(vp->streamPos < vp->fileSize)) {
        /* REV: silence after EOF (a0 is still packetBuf) */
        for (int32_t i = 0; i < t->interleave; i++) poke(m.packetBuf + (uint32_t)i, 0);
        s3 = t->interleave;
    } else {
        for (;;) {
            s3 = IoGetValidDataSize(t->stream);
            if (!(s3 < t->interleave)) break;
            IoDebugOut();   /* REV from here to the end of the loop */
            if (!(vp->streamPos < vp->fileSize) || !(s3 < sub32(vp->fileSize, vp->streamPos))) break;   /* rest valid */
            iopk::DelayThread(4000);
        }
        s3 = IoGetData(t->stream, m.packetBuf, t->interleave);
    }
    vp->streamPos = add32(vp->streamPos, s3);
    return s3;
}

/* 0x1a0c: one chunk of the current channel into its ring half: read, loop flags (byte 1 of every frame = 2, then the
 * ring end frame = 3 (end + repeat: jump back to the ring start) or the ring start frames = 6 / 2, and at the file end
 * 0 / 1 = end without repeat), then VoiceTrans on the module's DMA channel with the DMA-end handler registered.
 * 0 = nothing sent (chunk 0). A negative byte count with the stream still attached goes on (Q-G2d); a failed VoiceTrans
 * still returns 1 and leaves mode 1 (Q-G2e). */
int _TransTrackProc(int track)
{
    IVagMng &m = s_IVagMng;
    if (track < 0 || track >= 8) return 0;   /* REV */
    IVagTrack *t = &s_IVagTrack[track];
    IVagVoice *vp = cur_vp(t);
    if (!vp) return 0;
    vp->chunk = 0;   /* before the other tests */
    if (t->needTrans) return 0;
    if (t->state != 4 && t->state != 2) return 0;
    if (!(vp->consumed < vp->fileSize) && t->half < vp->fileSize) return 0;    /* REV (garage2) */
    if (!(vp->streamPos < vp->fileSize) && t->half < vp->fileSize) return 0;   /* REV */
    int32_t n = _ReadTrackData(t);
    if (n < 0 && !IoIsAttached(t->stream)) {   /* REV */
        iopk::Printf(ERR, "_TransTrackProc");
        iopk::Printf("Stream buffer is not attached...Track=%d\n", track);
        return 0;
    }
    _SetADPCMLoop(m.packetBuf, n);
    int half1 = (t->flags >> 1) & 1;
    if (!half1 && add32(vp->writePos, n) == t->half) {
        poke(m.packetBuf + (uint32_t)n - 0xf, 3);   /* last frame of the ring */
    } else if (half1 && vp->writePos == 0) {
        poke(m.packetBuf + 1, 6);                   /* first frame of the ring */
        poke(m.packetBuf + 0x11, 2);
    }
    if (!(add32(vp->consumed, n) < vp->fileSize)) {   /* the file ends in this chunk */
        if (!(vp->fileSize < t->interleave) || !(vp->streamPos < t->interleave)) {   /* 2nd test REV */
            poke(m.packetBuf + (uint32_t)n - 0x1f, 0);
            poke(m.packetBuf + (uint32_t)n - 0xf, 1);
        }
    }
    int32_t spu = add32(add32(vp->spu, mul32(t->half, 1 - half1)), vp->writePos);
    s_ucTransFlag = 0;
    vp->chunk = n;
    m.mode = 1;
    m.transTrack = (uint8_t)track;
    iopk::FlushDcache();
    int st;
    iopk::CpuSuspendIntr(&st);
    IoSetTransIntrHandler(m.dmaCh, _SpuTransIntrHandler);
    int r = IoVoiceTrans(m.dmaCh, 0, m.packetBuf, (uint32_t)spu, (uint32_t)n);
    if (r < 0) {   /* REV */
        iopk::Printf(ERR, "_TransTrackProc");
        iopk::Printf("sceSdVoiceTrans() is failed...ret=%d\n", r);
    }
    iopk::CpuResumeIntr(st);
    return 1;
}

/* ---------------------------------------------------------------- the two ThreadProc rounds */

/* 0x1268 (leaf): the next track after transTrack (wrapping, the current one never tested) that is active, not waiting
 * for its IRQ and short of data by the deficit test; transTrack itself when there is none */
int _NextTransProc()
{
    int tt = s_IVagMng.transTrack;
    int i = (tt + 1) & 7;   /* the signed % 8 fix-up for negatives is dead: tt is a byte */
    while (i != tt) {
        IVagTrack *t = &s_IVagTrack[i];
        if ((t->flags & 1) && !t->needTrans) {
            int32_t W = t->dataSize, S = t->sent;
            int32_t P = mul32(t->interleave, t->nch);
            if ((S < W || !(P < W)) && (!(P < S) || P < W)) return i;
        }
        i = (i + 1) & 7;
    }
    return tt;
}

/* 0xf94: the DMA-end continuation (ThreadProc, under the op lock): accounts the chunk of transTrack's current channel,
 * moves to the next channel; while the half is not full sends the next chunk; at the round end (the half filled, or
 * the file done) marks the track filled (state 2 -> 3 + the state pack byte), releases its lock and serves the next
 * track that needs data, or arms the IRQ (mode 2) / goes idle (mode 0). t / vp stay the entry track's for the whole
 * call, also after _StartTransTrackProc(n) returned 0 for another track (Q-G2c). */
void _AfterTransProc()
{
    IVagMng &m = s_IVagMng;
    s_ucTransFlag = 0;
    int s0 = 0;
    int tt = m.transTrack;
    if (tt >= 8) return;   /* REV (the asm's bltz on the lbu value is dead) */
    IVagTrack *t = &s_IVagTrack[tt];
    IVagVoice *vp = cur_vp(t);
    if (!(t->flags & 4)) {
        vp->consumed = add32(vp->consumed, vp->chunk);
        t->sent = add32(t->sent, vp->chunk);
    }
    vp->writePos = add32(vp->writePos, vp->chunk);
    vp->writePos = IOP_REM(vp->writePos, t->half, 0x1024, "ivag _AfterTransProc half 0");
    if (!(vp->consumed < vp->fileSize)) vp->writePos = 0;
    vp->chunk = 0;
    for (;;) {
        t->cur = add32(t->cur, 1);
        t->cur = IOP_REM(t->cur, t->nch, 0x1074, "ivag _AfterTransProc nch 0");
        int more = t->cur != 0;
        if (!more && vp->writePos != 0) {   /* every channel served once, the half not full yet */
            if (vp->consumed < vp->fileSize)
                more = !(t->interleave < vp->consumed) || t->interleave < vp->fileSize;
            else if (!(t->interleave < vp->fileSize))   /* REV */
                more = !(t->interleave < vp->consumed) || t->interleave < vp->fileSize;
        }
        if (more) {
            if (_TransTrackProc(m.transTrack) != 0) return;
            continue;
        }
        /* round end */
        t->needTrans = 1;
        if (t->state == 2) {
            t->state = 3;
            s_IVagStatePack.state[m.transTrack] = t->state;
            if (m.transTrack == m.irqTrack) s0 = 1;   /* REV */
        }
        _FinishTransTrackProc(m.transTrack);
        int n = _NextTransProc();
        if (m.transTrack == n && s_IVagTrack[n].needTrans) {   /* nobody else needs data */
            m.transTrack = (uint8_t)n;
            if (m.irqTrack != 0xff && !s0) {
                m.mode = 2;   /* stored before the call */
                _SetIntrMasterTrack();
            } else {
                m.mode = 0;
            }
            return;
        }
        m.transTrack = (uint8_t)n;
        if (_StartTransTrackProc(m.transTrack) != 0) return;
    }
}

/* 0x1368: the SPU-IRQ round (ThreadProc, under the op lock): every playing track flips its halves and, unless looping,
 * accounts the half just played (end of data -> state 6, stop state 2); then _Operate (KON / KOFF, CallRpc and
 * _Wait2Ticks: T moves one chunk), the new IRQ master, the first DMA of its round (when nothing could be sent, the
 * direct _AfterTransProc: a second Finish = KE_SEMA_OVF, garage2 only), and a CommandProc wake. */
void _IRQProc()
{
    IVagMng &m = s_IVagMng;
    s_ucIrqFlag = 0;
    for (int i = 0; i < 8; i++) {
        IVagTrack *t = &s_IVagTrack[i];
        if (!(t->flags & 1) || t->state != 4) continue;
        _FlipDBuff(t);
        if (t->flags & 4) continue;   /* looping: no accounting */
        t->played = add32(t->played, mul32(t->half, t->nch));
        t->irqCount = add32(t->irqCount, 1);
        /* Q-G2f: the original walks vp[c] for every c < nch with only 2 slots (c >= 2 reads +0x0c.. as pointers and
         * writes wherever they point); the host has slots 0 / 1 only */
        for (int c = 0; c < t->nch; c++)
            if (c < 2) t->vp[c]->off = add32(t->vp[c]->off, t->half);
        if (!(t->played < t->dataSize) && t->stopState != 1) {
            t->state = 6;
            t->needTrans = 1;
            t->stopState = 2;
        }
    }
    const OpRes res = {{1, 8, g_base + 0x6030, 0, 0, 0}};   /* frame residue (map 20.3 R): stale 1, its s0 = 8, s1 = &T[8] */
    _Operate(&res);
    _UpdateMasterTrack(1);
    if (m.irqTrack == 0xff) {
        m.mode = 0;
    } else {
        m.transTrack = m.irqTrack;
        if (_StartTransTrackProc(m.transTrack) == 0) _AfterTransProc();
    }
    iopk::SignalSema(m.semCmd);   /* CommandProc (0x1a) preempts here */
}

}  // namespace ivag
