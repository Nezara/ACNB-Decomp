/* ivag setup / teardown: ivagSetup (header parse of the
 * attached FsMFStrm stream, voice slots + SPU rings, loop point, first fill), the EE's two-step setup ivagReserveSetup /
 * ivagGetReserveSetupResult (the setup itself runs in _CommandProc after the RPC returned), ivagQuit / ivagForceReset
 * (thread / sema teardown; ForceReset re-creates them) and the two per-track core-voice setters, with the voice-slot
 * helpers _GetFreeVoice / _ReleaseVoice. Function comments give the original's image offset.
 *
 * Kept original behaviour (quirks Q-G4a .. i): the setup fail tail signals the op lock even on the paths that never took
 * it, the busy arm resets a live track to state 0, a ring-alloc failure leaks the rings already allocated, Quit leaves
 * stale ids / handlers / the packet buffer, ForceReset frees the packet buffer without re-allocating and runs without
 * the init test. Host-only guards (GH4-6, each logged, unreachable in the recordings): ivagSetup refuses nch outside
 * 1 .. 2 (the original's vp[] stores run over the track record for nch >= 3), the core-voice setters refuse a missing
 * voice slot, ivagGetReserveSetupResult refuses a bad track, the irq-master test reads only real tracks. */
#include "ivag_state.h"
#include "fssd.h"
#include "../../platform/plat.h"
#include <string.h>

namespace ivag {

static const char ERR[] = "%s : ERROR!!! ";
/* D_057b8: Shift-JIS "the transfer packet buffer is not allocated" (D_054f4's text without "ADPCM") */
static const char s_NoPacketBuf[] =
    "\x83\x66\x81\x5b\x83\x5e\x93\x5d\x91\x97\x97\x70\x83\x70\x83\x50\x83\x62\x83\x67\x83\x6f\x83\x62\x83\x74\x83\x40\x82"
    "\xaa\x8a\x6d\x95\xdb\x82\xb3\x82\xea\x82\xc4\x82\xa2\x82\xdc\x82\xb9\x82\xf1\x0a";

/* ---------------------------------------------------------------- voice slots */

/* 0x2b88 (leaf): the first slot with bit0 clear, marked in use */
IVagVoice *_GetFreeVoice()
{
    for (int i = 0; i < 16; i++) {
        IVagVoice *v = &s_IVagVoice[i];
        if (!(v->flags & 1)) {
            v->flags = (uint8_t)((v->flags & ~1) | 1);
            return v;
        }
    }
    return 0;
}

/* 0x2be4 (leaf) */
void _ReleaseVoice(IVagVoice *v)
{
    if (v) v->flags = (uint8_t)(v->flags & ~1);
}

/* ---------------------------------------------------------------- reserve pair */

/* 0x3f54: the busy test is outside the lock; _AddCommand's result is ignored (a full queue leaves state 1 and w0 set
 * with no command) */
int ivagReserveSetup(int track, int stream)
{
    if (track < 0 || track >= 8) return 0;
    if (stream < 0) return 0;
    IVagTrack *t = &s_IVagTrack[track];
    if (t->state != 0) {
        iopk::Printf(ERR, "ivagReserveSetup");
        iopk::Printf("track is busy... select track id = %d, stat = %d\n", track, t->state);
        return 0;
    }
    iopk::WaitSema(s_IVagMng.semOp);
    s_IVagStatePack.req[track].w0 = stream;
    s_IVagStatePack.req[track].w1 = 0;
    t->state = 1;
    s_IVagStatePack.state[track] = 1;
    iopk::SignalSema(s_IVagMng.semOp);
    _AddCommand(track, 1);
    return 1;
}

/* 0x406c (leaf): -1 while pending (w0 != -1), else the setup's result. The original has no bounds check (Q-G4e) */
int ivagGetReserveSetupResult(int track)
{
    if (track < 0 || track >= 8) {
        plat_log("ivag: ivagGetReserveSetupResult(%d): bad track", track);
        return -1;
    }
    const auto &p = s_IVagStatePack.req[track];
    return p.w0 == -1 ? p.w1 : -1;
}

/* ---------------------------------------------------------------- setup */

static int32_t hdr_word(uint32_t hdr, int i)
{
    const uint8_t *p = iopk::Ptr(hdr + 4u * (uint32_t)i);
    int32_t v = 0;
    if (p) memcpy(&v, p, 4);
    return v;
}
static int hdr_magic(uint32_t hdr, const char *m)
{
    const uint8_t *p = iopk::Ptr(hdr);
    return p && !strncmp((const char *)p, m, 4);
}

/* 0x40b0: runs in _CommandProc (cmd 1) or on the RPC thread (0x30000). The 0x20-byte header buffer is the original's
 * stack (sp+0x30); FsMFStrm copies to IOP addresses, so it is a fixed scratch in the module image (HDR_OFS, unused by
 * the host statics; no AllocSysMemory, which would move every later first-fit block). Two lock windows (GH4-2): the
 * state test, then from the second header read to the end; the pulls block in FsMFStrm with the lock held (19.5-3).
 * The fill: direct (_StartTransTrackProc) only when no IRQ master is playing and the engine is idle, else deferred
 * (stopState 5, _Operate releases it in the next IRQ round). */
int ivagSetup(int track, int stream)
{
    IVagMng &m = s_IVagMng;
    IVagTrack *t = 0;   /* s2: set after the attach test only (Q-G4i) */
    const uint32_t hdr = g_base + HDR_OFS;
    int n, c;
    if (track < 0 || track >= 8) return 0;
    if (!m.packetBuf) {   /* no lock, the track keeps its ReserveSetup state 1 (Q-G4b) */
        iopk::Printf(ERR, "ivagSetup");
        iopk::Printf(s_NoPacketBuf);
        return 0;
    }
    if (!IoIsAttached(stream)) {
        iopk::Printf(ERR, "ivagSetup");
        iopk::Printf("this stream is not attached...stream index = %d\n", stream);
        goto fail;   /* without the lock (Q-G4a) */
    }
    t = &s_IVagTrack[track];
    iopk::WaitSema(m.semOp);
    if (t->state != 0 && t->state != 1) {
        iopk::Printf(ERR, "ivagSetup");
        iopk::Printf("track is busy... select track id = %d, stat = %d\n", track, t->state);
        goto fail;   /* resets the live track (Q-G4d) */
    }
    iopk::SignalSema(m.semOp);
    n = IoGetDataSync(stream, hdr, 0x20);
    if (n < 0x20) {
        iopk::Printf(ERR, "ivagSetup");
        iopk::Printf("can't read from stream buffer... strmID=%d, ret=%d\n", stream, n);
        goto fail;   /* the lock is not held here (Q-G4a) */
    }
    iopk::WaitSema(m.semOp);
    memset(t, 0, sizeof *t);   /* the pack is not touched: polls still show state 1, w0 = stream */
    if (hdr_magic(hdr, "SShd")) {
        if (hdr_word(hdr, 2) != 0x10) {
            iopk::Printf(ERR, "ivagSetup");
            iopk::Printf("16bit PCM data is not supported...\n");
            goto fail;
        }
        t->rate = hdr_word(hdr, 3);
        t->nch = hdr_word(hdr, 4);   /* unchecked here; the host guard below */
        t->interleave = hdr_word(hdr, 5);
        t->loopStart = hdr_word(hdr, 6);
        t->loopEnd = hdr_word(hdr, 7);
        t->flags = (uint8_t)((t->flags & ~4) | (t->loopStart != -1 && t->loopEnd != -1 ? 4 : 0));
        n = IoGetData(stream, hdr, 8);
        if (n < 8) {
            iopk::Printf(ERR, "ivagSetup");
            iopk::Printf("can't read from stream buffer...ret=%d\n", n);
            goto fail;
        }
        t->dataSize = hdr_word(hdr, 1);   /* file offset 0x24 (GH4-4) */
        IoReleaseData(stream, 0x7d8);     /* 0x20 + 8 + 0x7d8 = the 0x800-byte header sector (GH4-3) */
    } else if (hdr_magic(hdr, "VAGp")) {   /* REV */
        t->rate = hdr_word(hdr, 4);
        t->nch = 1;
        t->interleave = 0;
        t->dataSize = hdr_word(hdr, 3);   /* from the first read, before the second */
        t->loopStart = 0;
        t->loopEnd = 0;
        t->flags = (uint8_t)(t->flags & ~4);
        n = IoGetData(stream, hdr, 0x10);
        if (n < 0x10) {
            iopk::Printf(ERR, "ivagSetup");
            iopk::Printf("can't read from stream buffer...ret=%d\n", n);
            goto fail;
        }
        IoReleaseData(stream, 0x7d0);
    } else {
        const uint8_t *p = iopk::Ptr(hdr);
        iopk::Printf(ERR, "ivagSetup");
        iopk::Printf("read file is not supported... %c%c%c%c\n", p ? (int8_t)p[0] : 0, p ? (int8_t)p[1] : 0, p ? (int8_t)p[2] : 0,
                     p ? (int8_t)p[3] : 0);
        goto fail;
    }
    if (t->rate % 12000 != 0) {
        iopk::Printf(ERR, "ivagSetup");
        iopk::Printf("This vag data is not supported...sampling rate=%d\n", t->rate);
        goto fail;
    }
    if (t->nch < 1 || t->nch > 2) {   /* host guard (19.4 a): vp[] has two entries */
        plat_log("ivag: ivagSetup(%d, %d): %d channels not supported (1 or 2)", track, stream, t->nch);
        goto fail;
    }
    for (c = 0; c < t->nch; c++) {
        t->vp[c] = _GetFreeVoice();
        if (!t->vp[c]) {
            for (int k = 0; k < c; k++) _ReleaseVoice(t->vp[k]);
            iopk::Printf(ERR, "ivagSetup");
            iopk::Printf("can't allocate free voice info. slot...\n");
            goto fail;
        }
    }
    t->half = (int32_t)((uint32_t)m.packetSize * (uint32_t)t->rate) / 48000;   /* mult lo, signed div (GH4-5) */
    for (c = 0; c < t->nch; c++) {
        int32_t spu = SpuMmng_Alloc((int32_t)((uint32_t)t->half << 1));
        IVagVoice *v = t->vp[c];
        v->spu = spu;
        v->ringSize = (int32_t)((uint32_t)t->half << 1);
        v->fileSize = t->dataSize / t->nch;
        v->off = 0;
        v->streamPos = 0;
        v->consumed = 0;
        v->writePos = 0;
        v->chunk = 0;
        v->flags = (uint8_t)(v->flags & ~2);
        v->volR = 0x3fff;
        v->volL = 0x3fff;
        if (v->spu == 0) {   /* releases every channel's slot; the rings already allocated stay (Q-G4c) */
            for (int k = 0; k < t->nch; k++) _ReleaseVoice(t->vp[k]);
            iopk::Printf(ERR, "ivagSetup");
            iopk::Printf("can't allocate spu memory...\n");
            goto fail;
        }
    }
    t->played = 0;
    t->sent = 0;
    t->stream = stream;
    t->cur = 0;
    t->flags = (uint8_t)((t->flags & ~2) | 2);   /* the setup fill writes the 1st half */
    t->flags = (uint8_t)((t->flags & ~8) | 8);   /* reverb on */
    t->state = 2;
    t->needTrans = 1;
    s_IVagStatePack.state[track] = 2;
    if (t->flags & 4) {
        int32_t lim = t->vp[0]->fileSize >> 4;   /* one channel's file size, in 16-byte units */
        if (lim < t->loopEnd) t->loopEnd = lim;  /* REV */
        int32_t a = (int32_t)((((uint32_t)t->loopStart << 4) * (uint32_t)t->nch + 0xfffu) & ~0x7ffu);
        int32_t b = (int32_t)((((uint32_t)t->loopEnd << 4) * (uint32_t)t->nch + 0xfffu) & ~0x7ffu);
        IoSetLoopPoint(stream, a, b, -1);
        int32_t le = (int32_t)((uint32_t)t->loopEnd << 4);
        if (le < IoGetStreamBufferSize(stream)) IoSeek(stream, 0x800);   /* REV: the loop fits the stream buffer */
    }
    t->flags = (uint8_t)((t->flags & ~1) | 1);
    {
        int it = m.irqTrack;   /* the original reads any index (only 0..7 / 0xff are ever stored) */
        if (it != 0xff && it < 8 && s_IVagTrack[it].state == 4) t->stopState = 5;
        else if (m.mode == 0) {
            t->needTrans = 0;
            _StartTransTrackProc(track);
        } else
            t->stopState = 5;   /* REV */
    }
    iopk::SignalSema(m.semOp);
    return 1;

fail:
    /* Q-G4i: on the not-attached path the original's track pointer (s2) is still the caller's register, so its state
     * store goes to that register + 0x34; the host drops it (the track keeps its state) */
    if (t) t->state = 0;
    s_IVagStatePack.state[track] = 0;
    s_IVagStatePack.req[track].w0 = -1;
    s_IVagStatePack.req[track].w1 = 0;
    iopk::SignalSema(m.semOp);
    return 0;
}

/* ---------------------------------------------------------------- teardown */

/* 0x3504: the stop arms of _Operate run on the caller's (RPC) thread; ids, handlers, Mng and the packet buffer stay
 * (Q-G4f: a later Init re-memsets and leaks the buffer); every slot's voice is released in hsyn without a 0xff test */
int ivagQuit()
{
    if (!s_iInitialize) return 0;
    IVagMng &m = s_IVagMng;
    iopk::TerminateThread(m.tid);
    for (int i = 0; i < 8; i++)
        if (s_IVagTrack[i].flags & 1) s_IVagTrack[i].stopState = 1;
    _Operate();
    iopk::DeleteThread(m.tid);
    iopk::DeleteSema(m.semIsr);
    iopk::DeleteSema(m.semOp);
    iopk::TerminateThread(s_iSetupThreadID);
    iopk::DeleteThread(s_iSetupThreadID);
    iopk::DeleteSema(m.semCmd);
    iopk::DeleteSema(m.semQ);
    for (int i = 0; i < 8; i++) iopk::DeleteSema(s_IVagSema[i]);
    iopk::ChangeThreadPriority(0, 0x19);
    s_iInitialize = 0;
    for (int i = 0; i < 16; i++) fssd::HsynSetReservVoiceFlag(s_IVagVoice[i].voice + s_IVagVoice[i].core * 24, 0);
    fssd::HsynValidateReservVoice();
    return 1;
}

/* 0x36a8: no s_iInitialize test (Q-G4g). Stops every active track, disarms the DMA handler and both cores' IRQ
 * enable, frees the packet buffer (not re-allocated: the EE must SetPacketBuffer again), clears the engine state except
 * the slots' core / voice bytes, resets the allocator, deletes the threads / semas unconditionally (ids may be -1 or
 * stale: the kernel returns an error) and re-creates them as ivagInit does (without the memsets, SetCoreVoice,
 * SpuMmng_Init and SetPacketBuffer). A failed _ThreadProc creation returns without cleanup; later failures clean up and
 * leave the ids stale. Not touched: s_iInitialize, packetSize / spuBase / spuSize / dmaCh / master volumes, hsyn. */
void ivagForceReset()
{
    IVagMng &m = s_IVagMng;
    uint8_t savedVoice[16], savedCore[16];
    int st, r;
    iopk::TerminateThread(m.tid);
    for (int i = 0; i < 8; i++)
        if (s_IVagTrack[i].flags & 1) s_IVagTrack[i].stopState = 1;
    _Operate();
    iopk::CpuSuspendIntr(&st);
    IoSetTransIntrHandler(m.dmaCh, 0);
    IoSetCoreAttr(4, 0);   /* IRQ enable off, core 0 */
    IoSetCoreAttr(5, 0);   /* core 1 */
    iopk::CpuResumeIntr(st);
    if (m.packetBuf) {
        iopk::FreeSysMemory(m.packetBuf);
        m.packetBuf = 0;
    }
    for (int i = 15; i >= 0; i--) {
        savedCore[i] = s_IVagVoice[i].core;
        savedVoice[i] = s_IVagVoice[i].voice;
    }
    memset(s_IVagVoice, 0, sizeof s_IVagVoice);
    memset(s_IVagTrack, 0, sizeof s_IVagTrack);
    memset(&s_IVagStatePack, 0, sizeof s_IVagStatePack);
    memset(s_IvagCmdQueue, 0, sizeof s_IvagCmdQueue);
    m.mode = 0;
    s_ucTransFlag = 0;
    s_ucIrqFlag = 0;
    s_KeyOn[0] = 0;
    s_KeyOn[1] = 0;
    m.irqTrack = 0xff;
    m.transTrack = 0xff;
    for (int i = 0; i < 8; i++) {
        s_IVagStatePack.req[i].w1 = 0;
        s_IVagStatePack.req[i].w0 = -1;
    }
    for (int i = 0; i < 16; i++) {
        s_IVagVoice[i].core = savedCore[i];
        s_IVagVoice[i].voice = savedVoice[i];
    }
    SpuMmng_Init(m.spuBase, m.spuSize);
    iopk::DeleteThread(m.tid);
    iopk::DeleteSema(m.semIsr);
    iopk::DeleteSema(m.semOp);
    iopk::TerminateThread(s_iSetupThreadID);
    iopk::DeleteThread(s_iSetupThreadID);
    iopk::DeleteSema(m.semCmd);
    iopk::DeleteSema(m.semQ);
    for (int i = 0; i < 8; i++) iopk::DeleteSema(s_IVagSema[i]);
    m.tid = -1;
    s_iSetupThreadID = -1;
    m.semIsr = -1;
    m.semOp = -1;
    m.semCmd = -1;
    m.semQ = -1;
    for (int i = 7; i >= 0; i--) s_IVagSema[i] = -1;

    m.tid = MakeThread(_ThreadProc, 0x55);
    if (m.tid < 0) {
        iopk::Printf(ERR, "ivagForceReset");
        iopk::Printf("Can't creaet thread... ret=%d\n", m.tid);
        return;
    }
    s_iSetupThreadID = MakeThread(_CommandProc, 0x1a);
    if (s_iSetupThreadID < 0) {
        iopk::Printf(ERR, "ivagForceReset");
        iopk::Printf("Can't creaet setup thread... ret=%d\n", s_iSetupThreadID);
        goto cleanup;
    }
    if ((r = MakeSema(&m.semIsr, 1)) < 0) {
        iopk::Printf(ERR, "ivagForceReset");
        iopk::Printf("Can't create semaphore... ret=%d\n", r);
        goto cleanup;
    }
    if ((r = MakeSema(&m.semOp, 1)) < 0) {
        iopk::Printf(ERR, "ivagForceReset");
        iopk::Printf("Can't create semaphore for operation... ret=%d\n", r);
        goto cleanup;
    }
    iopk::SignalSema(m.semOp);
    if ((r = MakeSema(&m.semCmd, 0x10)) < 0) {
        iopk::Printf(ERR, "ivagForceReset");
        iopk::Printf("Can't create semaphore for command thread... ret=%d\n", r);
        goto cleanup;
    }
    if ((r = MakeSema(&m.semQ, 1)) < 0) {
        iopk::Printf(ERR, "ivagForceReset");
        iopk::Printf("Can't create semaphore for command queue... ret=%d\n", r);
        goto cleanup;
    }
    iopk::SignalSema(m.semQ);
    for (int i = 0; i < 8; i++) {
        if ((r = MakeSema(&s_IVagSema[i], 1)) < 0) {
            iopk::Printf(ERR, "ivagForceReset");
            iopk::Printf("Can't create semaphore for track ctrl... ret=%d\n", r);
            goto cleanup;
        }
        iopk::SignalSema(s_IVagSema[i]);
    }
    if ((r = iopk::StartThread(m.tid, 0)) < 0) {
        iopk::Printf(ERR, "ivagForceReset");
        iopk::Printf("Can't start thread... ret=%d\n", r);
        goto cleanup;
    }
    if ((r = iopk::StartThread(s_iSetupThreadID, 0)) < 0) {
        iopk::Printf(ERR, "ivagForceReset");
        iopk::Printf("Can't start setup thread... ret=%d\n", r);
        goto cleanup;
    }
    return;

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
}

/* ---------------------------------------------------------------- per-track core voices */

/* host guard (GH4-6): a failed setup can leave nch > 0 with a null slot (or nch 3 after the nch guard) */
static int have_slot(const IVagTrack *t, int ch, const char *fn, int track)
{
    if (ch >= 0 && ch < 2 && t->vp[ch]) return 1;
    plat_log("ivag: %s(%d): channel %d has no voice slot", fn, track, ch);
    return 0;
}

/* 0x50ac: base is a global voice (core = base / 24, signed); no upper bound (fssd drops >= 0x30) */
int ivagSetChannelCoreVoice(int track, int ch, int base)
{
    if (track < 0 || track >= 8) return 0;
    IVagTrack *t = &s_IVagTrack[track];
    if (ch < 0 || !(ch < t->nch)) return 0;
    if (!have_slot(t, ch, "ivagSetChannelCoreVoice", track)) return 0;
    IVagVoice *vp = t->vp[ch];
    fssd::HsynSetReservVoiceFlag(vp->voice + vp->core * 24, 0);
    vp->core = (uint8_t)(base / 24);
    vp->voice = (uint8_t)(base % 24);
    fssd::HsynSetReservVoiceFlag(base, 1);
    fssd::HsynValidateReservVoice();
    return 1;
}

/* 0x5180: channel c takes base + c; clear / set per channel; 1 also for nch 0 (Q-G4h); a negative base passes */
int ivagSetTrackCoreVoice(int track, int base)
{
    if (track < 0 || track >= 8) return 0;
    IVagTrack *t = &s_IVagTrack[track];
    if (!((int32_t)((uint32_t)base + (uint32_t)t->nch) < 0x31)) return 0;
    for (int c = 0; c < t->nch; c++)
        if (!have_slot(t, c, "ivagSetTrackCoreVoice", track)) return 0;
    for (int c = 0; c < t->nch; c++) {
        IVagVoice *vp = t->vp[c];
        int g = (int32_t)((uint32_t)base + (uint32_t)c);
        fssd::HsynSetReservVoiceFlag(vp->voice + vp->core * 24, 0);
        vp->core = (uint8_t)(g / 24);
        vp->voice = (uint8_t)(g % 24);
        fssd::HsynSetReservVoiceFlag(g, 1);
    }
    fssd::HsynValidateReservVoice();
    return 1;
}

}  // namespace ivag
