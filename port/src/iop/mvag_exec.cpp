/* mvag G4 (map section 14): the ExecStatus* state machine the play thread runs per wake (fsiopsdmtvag.c 0x37e0 ..
 * 0x4bb4). ExecStatusPreLoad (status 3) and ExecStatusRunning (status 4) are flat ladders over a sub-state that lives
 * in ProcPlayThread's frame (`*out`, sp+0x1c: set once at thread start, never reset by the thread, Q-V18); each sub
 * returns 0 = test the next sub-state with the new `*out`, 1 = rescan from sub-state 0 at once, 2 = leave and wait for
 * the next wake (DMA end / SPU IRQ, one sema: Q-V19). The three entry points return 1 on every path (Q-V17: the play
 * thread's "0 = run again" arm is dead). All arithmetic is the R3000's (32-bit, signed compares where the asm uses
 * slt / blez / bgez). Deviation D-V2: every access by s_iTransTr / s_iMasterTr goes through TrAt (host guard). */
#include "mvag_state.h"
#include "../../platform/plat.h"
#include <string.h>

namespace mvag {

static const char kExecFile[] = "fsiopsdmtvag.c";   /* @62 */
static bool execDbg() { return (s_uiSubMode & 0x10000) != 0; }
static uint16_t entryOf(const Track &t) { return (uint16_t)((s_usCore | ((uint32_t)t.voice << 1)) & 0xffff); }
static uint16_t core4() { return (uint16_t)((s_usCore | 4) & 0xffff); }

/* D-V2 (map 14.4 Q-V23, host-only): s_tr[i] for 0 <= i < 16; any other index (s_iTransTr 99 through a stale `*out`,
 * s_iMasterTr -1 with s_iMaxTr <= 0) gets a zeroed scratch record, re-zeroed on every call, logged once. The original
 * reads / writes outside s_tr there (.data tail / s_sdd, or beyond the image). Valid indexes behave exactly as before. */
Track &TrAt(int i)
{
    if (i >= 0 && i < 16) return s_tr[i];
    static Track scratch;
    static int logged;
    if (!logged++) plat_log("mvag: track index %d outside s_tr, zero scratch track used (D-V2 host guard)", i);
    memset(&scratch, 0, sizeof scratch);
    return scratch;
}

typedef int (*ExecSub)(int *out);

/* the ladder of ExecStatusPreLoad / Running: `*out` reloaded before each test, ascending */
static int Dispatch(int *out, const ExecSub *sub, int n)
{
    for (;;) {
        int k;
        for (k = 0; k < n; k++) {
            if (*out != k) continue;
            const int r = sub[k](out);
            if (r == 2) return 1;
            if (r == 1) break;
        }
        if (k == n) return 1;   /* fell off the end (also an `*out` outside 0..n-1) */
    }
}

/* ---- PreLoad sub 0 (0x37e0): preload every set track, descending; the mode-1 master is the silent track (Q-V24) */
int ExecStatusPreLoadSubFileLocation(int *out)
{
    int32_t found = 0;
    for (int32_t i = s_iMaxTr - 1; i >= 0; i--) {
        if (s_iMode == 1 && i == s_iMasterTr) {
            Track &m = TrAt(i);
            m.loop = 1;
            m.state = 2;
            found++;
            continue;
        }
        Track &t = TrAt(i);
        if (t.state != 1) continue;
        if (!ReadPreloadFile(i)) continue;   /* a failing track stays 1 and does not count */
        InitPlayTrack(i);
        t.state = 2;
        found++;
        if (!(s_iBuffSize < t.file.size) && t.master != 1) t.onMem = 1;   /* whole ring, not half (Q-V20) */
    }
    if (found == 0) {
        if (execDbg()) Log("<ExecStatusPreLoad> no setdata track. -> MTVAG_STATUS_IDLE\n");
        s_iMstrmStatus = 2;
        return 2;   /* *out untouched */
    }
    s_iBuffTime = 0;
    s_boMstrmKeyOn = 0;
    TrackSetVoice();
    TrackSetVolume();
    JudgeDynamicMasterTrack();
    MakeSilentBuffer();
    *out = 1;
    return 0;
}

/* ---- PreLoad sub 1 (0x3990): mode 1 - silence into the master ring's second half, keyed */
int ExecStatusPreLoadSubFixTrackLoad(int *out)
{
    *out = 2;
    if (s_iMode != 1) return 0;
    Assert(TrAt(s_iMasterTr).master != 1, kExecFile, 0x82a);
    LoadTrack(s_iMasterTr, 1, s_iHalfBuffSize);
    TrAt(s_iMasterTr).key = 1;
    s_boMstrmKeyOn = 1;
    return 2;
}

/* ---- PreLoad sub 2 (0x3a6c): first resident track */
int ExecStatusPreLoadSubPreFullOnMemoryLoad(int *out)
{
    const int32_t tt = GetNextValidTrack(0, 2);
    s_iTransTr = tt;
    if (tt < 0 || tt == 99) {
        *out = 4;
    } else {
        Assert((uint32_t)tt < 0x10, kExecFile, 0x84a);
        *out = 3;
    }
    s_iBusSide = 0;
    return 0;
}

/* ---- PreLoad sub 3 (0x3ae4): resident tracks; a file larger than a half goes out in two wakes (side 0, side 1) */
int ExecStatusPreLoadSubFullOnMemoryLoad(int *out)
{
    if (execDbg())
        Log("full on memory ! [%d] file_size=%d, buff_size=%d, bus_side=%d\n", s_iTransTr, TrAt(s_iTransTr).file.size,
            s_iHalfBuffSize, s_iBusSide);
    Assert((uint32_t)s_iTransTr < 0x10, kExecFile, 0x861);
    Assert(TrAt(s_iTransTr).master != 1, kExecFile, 0x862);
    const int32_t n = ReadHalfBufferFile(s_iTransTr);
    if (n > 0) {
        LoadTrack(s_iTransTr, s_iBusSide, n);   /* result ignored */
        if (s_iBusSide == 0 && s_iHalfBuffSize < TrAt(s_iTransTr).file.size) {
            TrAt(s_iTransTr).lastBytes = n;   /* side 0 of a big resident file only (Q-V21) */
            s_iBusSide = 1;
            return 2;
        }
        s_iBusSide = 0;
        Track &t = TrAt(s_iTransTr);
        if (t.autoplay == 0) {
            t.key = 1;
            s_boMstrmKeyOn = 1;
        }
        s_iTransTr = GetNextValidTrack(s_iTransTr + 1, 2);
        if (s_iTransTr == 99) *out = 4;
        return 2;
    }
    s_iBusSide = 0;   /* the read gave nothing: skip the track (never keyed) */
    s_iTransTr = GetNextValidTrack(s_iTransTr + 1, 2);
    if (s_iTransTr == 99) {
        *out = 4;
        return 0;
    }
    return 1;
}

/* ---- PreLoad sub 4 (0x3d4c): first streaming track; mode 2 without one goes idle (leaves *out 4, Q-V18) */
int ExecStatusPreLoadSubPreTrackLoad(int *out)
{
    const int32_t tt = GetNextValidTrack(0, 1);
    s_iTransTr = tt;
    if (tt < 0 || tt == 99) {
        if (s_iMode == 1) {
            *out = 6;
            return 0;
        }
        if (execDbg())
            Log("<ExecStatusPreLoad> no normal track. -> MTVAG_STATUS_IDLE (because, this mode is MTVAG_MODE_DYNAMIC)\n");
        s_iMstrmStatus = 2;
        return 2;
    }
    Assert((uint32_t)tt < 0x10, kExecFile, 0x8b7);
    *out = 5;
    return 0;
}

/* ---- PreLoad sub 5 (0x3e04): the first half of each streaming track (always side 0) */
int ExecStatusPreLoadSubTrackLoad(int *out)
{
    Assert((uint32_t)s_iTransTr < 0x10, kExecFile, 0x8ca);
    const int32_t n = ReadHalfBufferFile(s_iTransTr);
    if (n > 0) {
        const int r = LoadTrack(s_iTransTr, 0, n);   /* 1 = DMA pending, 0 = EE path */
        Track &t = TrAt(s_iTransTr);
        if (t.autoplay == 0) {
            t.key = 1;
            s_boMstrmKeyOn = 1;
            if (t.master != 1) t.lastBytes = n;   /* only here, and only for auto-keyed tracks (Q-V21) */
        }
        s_iTransTr = GetNextValidTrack(s_iTransTr + 1, 1);
        if (s_iTransTr == 99) *out = 6;
        return r ? 2 : 1;
    }
    TrAt(s_iTransTr).key = 2;   /* the read gave nothing: stop the track */
    s_boMstrmKeyOff = 1;
    s_iTransTr = GetNextValidTrack(s_iTransTr + 1, 1);
    if (s_iTransTr == 99) {
        *out = 6;
        return 0;
    }
    Assert((uint32_t)s_iTransTr < 0x10, kExecFile, 0x914);
    return 1;
}

/* ---- PreLoad sub 6 (0x3fb8): key on, status 4; nothing to play -> idle with *out left at 6 (Q-V18) */
int ExecStatusPreLoadSubStartSpu2Interrupt(int *out)
{
    s_iTransTr = 99;
    s_iBusSide = 1;
    IoSetCoreAttr(core4(), 0);   /* IRQ off; the first Running.Start programs it (Q-V25) */
    if (s_boMstrmKeyOn) {
        KeyOn(0);
        s_iMstrmStatus = 4;
        *out = 0;
        return 2;
    }
    if (execDbg()) Log("<ExecStatusPreLoad> no playable track. -> MTVAG_STATUS_IDLE\n");
    s_iMstrmStatus = 2;
    return 2;
}

/* ---- ExecStatusPreLoad (0x4070) */
int ExecStatusPreLoad(int *out)
{
    static const ExecSub sub[7] = {ExecStatusPreLoadSubFileLocation,    ExecStatusPreLoadSubFixTrackLoad,
                                   ExecStatusPreLoadSubPreFullOnMemoryLoad, ExecStatusPreLoadSubFullOnMemoryLoad,
                                   ExecStatusPreLoadSubPreTrackLoad,    ExecStatusPreLoadSubTrackLoad,
                                   ExecStatusPreLoadSubStartSpu2Interrupt};
    return Dispatch(out, sub, 7);
}

/* ---- Running sub 0 (0x4200): advance times, pending key on / off (un-reading the prefetched chunk of a track being
 * stopped / paused, Q-V22), next streaming track, master election */
int ExecStatusRunninSubSetup(int *out)
{
    Assert(s_iTransTr == 99, kExecFile, 0x9a9);
    TrackAdvancePlayTime();
    if (s_boMstrmKeyOn && s_iBusSide == 1) KeyOn(0);
    if (s_boMstrmKeyOff) {
        if (s_iBusSide == 0) {
            const int32_t maxTr = s_iMaxTr, rs = s_iReadSector;
            for (int32_t i = 0; i < maxTr; i++) {
                Track &t = TrAt(i);
                if (t.key != 2 && t.key != 3) continue;
                if (!(t.state == 3 || t.state == 4 || t.state == 5)) continue;
                if (t.pos <= 0) continue;   /* signed */
                t.time--;
                t.pos -= rs;
            }
        }
        KeyOff(0);
    }
    s_iTransTr = GetNextValidTrack(0, 0);
    *out = s_iTransTr == 99 ? 2 : 1;
    JudgeDynamicMasterTrack();
    return 0;
}

/* ---- Running sub 1 (0x43a0): refill the half that just played, track by track */
int ExecStatusRunninTrackLoad(int *out)
{
    Assert((uint32_t)s_iTransTr < 0x10, kExecFile, 0x9e9);
    const int32_t n = ReadHalfBufferFile(s_iTransTr);
    if (n > 0) {
        const int r = LoadTrack(s_iTransTr, s_iBusSide, n);
        Track &t = TrAt(s_iTransTr);
        if (t.master != 1) t.lastBytes = n;   /* whatever autoplay says (Q-V21) */
        s_iTransTr = GetNextValidTrack(s_iTransTr + 1, 0);
        if (s_iTransTr == 99) *out = 2;
        return r ? 2 : 1;
    }
    TrAt(s_iTransTr).key = 2;
    s_boMstrmKeyOff = 1;
    s_iTransTr = GetNextValidTrack(s_iTransTr + 1, 0);
    if (s_iTransTr == 99) {
        *out = 2;
        return 0;
    }
    return 1;   /* no Assert on this path */
}

/* ---- Running sub 2 (0x4514): tracks queued during play (s_boMstrmEntryLoad), on side 0 only */
int ExecStatusRunninTrackEntryLoad(int *out)
{
    if (!s_boMstrmEntryLoad || s_iBusSide != 0) {
        *out = 3;
        return 0;
    }
    s_iTransTr = EntryLoad();
    if (s_iTransTr == 99) {
        s_boMstrmEntryLoad = 0;
        *out = 3;
        return 0;
    }
    Assert((uint32_t)s_iTransTr < 0x10, kExecFile, 0xa4f);
    const int32_t n = ReadHalfBufferFile(s_iTransTr);
    if (n > 0) {
        const int r = LoadTrack(s_iTransTr, s_iBusSide, n);
        if (TrAt(s_iTransTr).master != 1) {
            CommandSetVoice(s_iTransTr, (uint32_t)TrAt(s_iTransTr).voice, 1);   /* after LoadTrack */
            const Track &v = TrAt(s_iTransTr);
            IoSetParam(entryOf(v), v.volL);
            IoSetParam((uint16_t)(entryOf(v) | 0x100), v.volR);
        }
        Track &t = TrAt(s_iTransTr);
        if (t.autoplay == 0) {
            t.key = 1;
            s_boMstrmKeyOn = 1;
        }
        return r ? 2 : 1;   /* *out unchanged: the next call takes the next queued track */
    }
    TrAt(s_iTransTr).key = 2;
    s_boMstrmKeyOff = 1;
    return 1;
}

/* ---- Running sub 3 (0x478c): pending stop -> status 5 (the IRQ is still programmed), IRQ at the next half, flip the
 * bus side; a lost master (99) reloads the silent track 15-equivalent (s_iMaxTr - 1) and terminates */
int ExecStatusRunninStartSpu2Interrupt(int *out)
{
    if (s_boCommandStop) {
        s_boCommandStop = 0;
        s_iMstrmStatus = 5;
    }
    TrackCheckOnMemory();
    if (s_iMasterTr != 99) {
        Assert(s_iMasterTr < 0x10, kExecFile, 0xab6);   /* signed slti */
        const uint32_t addr =
            (uint32_t)TrAt(s_iMasterTr).spuAddr + (uint32_t)((int32_t)((uint32_t)s_iMstTrTransSize * (uint32_t)s_iBusSide));
        IoSetAddr((uint16_t)((s_usCore | 0x1f00) & 0xffff), addr);
        IoSetCoreAttr(core4(), 1);
        s_iBusSide = s_iBusSide == 0;
    } else {
        if (execDbg()) Log(">>> lost master track.\n");
        MakeSilentBuffer();
        s_iMstTrTransSize = s_iHalfBuffSize;
        s_iMasterTr = s_iMaxTr - 1;
        Assert(TrAt(s_iMasterTr).master != 1, kExecFile, 0xaca);
        LoadTrack(s_iMasterTr, 0, s_iMstTrTransSize);
        s_iMstrmStatus = 5;   /* busSide untouched */
    }
    *out = 0;
    s_iBuffTime++;
    return 2;
}

/* ---- ExecStatusRunning (0x496c) */
int ExecStatusRunning(int *out)
{
    static const ExecSub sub[4] = {ExecStatusRunninSubSetup, ExecStatusRunninTrackLoad, ExecStatusRunninTrackEntryLoad,
                                   ExecStatusRunninStartSpu2Interrupt};
    return Dispatch(out, sub, 4);
}

/* ---- ExecStatusTerminate (0x4a6c): IRQ off, idle / started, every used track muted (master included), times
 * advanced, every playing / paused track force-stopped */
int ExecStatusTerminate(int *out)
{
    if (execDbg()) Log("<ExecStatusTerminate> play terminating.\n");
    IoSetCoreAttr(core4(), 0);
    s_iMstrmStatus = s_boMstrmActive ? 2 : 1;
    for (int32_t i = 0; i < s_iMaxTr; i++) {
        const Track &t = TrAt(i);
        if (t.state == 0) continue;
        IoSetParam(entryOf(t), 0);
        IoSetParam((uint16_t)(entryOf(t) | 0x100), 0);
    }
    TrackAdvancePlayTime();
    KeyOff(1);
    *out = 0;
    return 1;
}

}  // namespace mvag
