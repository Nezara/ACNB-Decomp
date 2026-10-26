/* mvag G2 (original fsiopsdmtvag.c, module offsets 0x1148 .. 0x1ce0 and
 * 0x2998 .. 0x3628): the 19 per-track EE commands (play / stop / pause, voice, loop, volumes, status queries) and the
 * track / key layer the play thread drives (SetLoopFlag, GetNextValidTrack, TrackAdvancePlayTime, TrackCheckOnMemory,
 * TrackSetVoice, TrackSetVolume, KeyOn, KeyOff). Integer code only; every sd / fssd call goes through the Io seam.
 * "ok" = signed 0 <= idx < s_iMaxTr (not 16); sd voice entries are (s_usCore | voice << 1) & 0xffff. */
#include "mvag_state.h"
#include "../../platform/plat.h"

namespace mvag {

static const char kTrackFile[] = "fsiopsdmtvag.c";   /* @62 */

static bool trk_dbg() { return (s_uiSubMode & 0x10000) != 0; }
static bool trk_ok(int idx) { return idx >= 0 && idx < s_iMaxTr; }
/* mode 1: the master track is not a per-track command target */
static bool trk_isMaster(int idx) { return s_iMode == 1 && idx == s_iMasterTr; }
/* sd voice entry: core lhu | voice lw << 1, andi 0xffff */
static uint32_t trk_e(const Track &t) { return (s_usCore | ((uint32_t)t.voice << 1)) & 0xffff; }
/* 1 << voice (sllv: low 5 bits) */
static uint32_t trk_bit(const Track &t) { return 1u << ((uint32_t)t.voice & 31); }

/* Q-V8: the track a master-flag track follows (syncTr, -1 = none). The original indexes s_tr with the unchecked RPC
 * word (syncTr * 0xd0); host-only guard: an index outside 0..15 counts as "no partner" (one plat_log). */
static Track *trk_partner(const Track &t)
{
    if (t.master != 1 || t.syncTr == -1) return 0;
    if ((uint32_t)t.syncTr >= 16) {
        static bool logged;
        if (!logged) {
            logged = true;
            plat_log("mvag: syncTr %d outside the track table, treated as none (host guard; the original reads / "
                     "writes past s_tr)", t.syncTr);
        }
        return 0;
    }
    return &s_tr[t.syncTr];
}

/* ---- per-track commands */

/* 0x1148 */
int CommandTrPlay(int idx)
{
    if (CommandIsEnableSeTr(idx)) {   /* SE track: key on at once, no state change, no flush */
        Track &t = s_tr[idx];
        IoSetParam((uint16_t)trk_e(t), t.volL);
        IoSetParam((uint16_t)(trk_e(t) | 0x100), t.volR);
        IoSetSwitch((uint16_t)(s_usCore | 0x1500), trk_bit(t));
        return 1;
    }
    if (!trk_ok(idx)) return 0;
    if (trk_isMaster(idx)) return 0;
    Track &t = s_tr[idx];
    uint8_t st = t.state;
    if (st != 6 && st != 7 && st != 2) return 0;
    if (st == 7 && t.pos == 0) {   /* Q-V9: re-queue an ended track (its file is already closed); no flush */
        InitPlayTrack(idx);
        t.state = 1;
        if (s_iMstrmStatus == 4) s_boMstrmEntryLoad = 1;
        return 1;
    }
    if (t.master == 1) {
        t.state = 5;
    } else {
        t.key = 1;
        s_boMstrmKeyOn = 1;
    }
    t.lastBytes = 0;   /* delay slot of the FlushDcache jal */
    IoFlushDcache();
    return 1;
}

/* 0x1384: jump table _fdata: states 1..6 act, 0 and >= 7 fail */
int CommandTrStop(int idx)
{
    if (!trk_ok(idx)) return 0;
    if (trk_isMaster(idx)) return 0;
    Track &t = s_tr[idx];
    if (t.state < 1 || t.state > 6) return 0;
    if (t.master == 1) {
        t.state = 7;
    } else {
        t.key = 2;
        s_boMstrmKeyOff = 1;
    }
    IoFlushDcache();
    return 1;
}

/* 0x149c: states 1..5 */
int CommandTrPause(int idx)
{
    if (!trk_ok(idx)) return 0;
    if (trk_isMaster(idx)) return 0;
    Track &t = s_tr[idx];
    uint8_t st = t.state;
    if (st != 5 && st != 4 && st != 3 && st != 2 && st != 1) return 0;
    if (t.master == 1) {
        t.state = 6;
    } else {
        t.key = 3;
        s_boMstrmKeyOff = 1;
    }
    IoFlushDcache();
    return 1;
}

/* 0x15c0: voice = voice % 24 (divu); reserve != 0 -> add the voice to this core's reserve mask and hand both words to
 * hsyn (s_usCore indexes the mask array unchecked; CommandInit keeps it 0 / 1) */
int CommandSetVoice(int idx, uint32_t voice, int reserve)
{
    int st = 0;   /* sp+0x24 */
    if (!trk_ok(idx)) return 0;
    Track &t = s_tr[idx];
    t.voice = (int32_t)(voice % 24);
    IoSetParam((uint16_t)(trk_e(t) | 0x200), 0x1000);
    IoSetParam((uint16_t)(trk_e(t) | 0x300), 0xf);
    IoSetParam((uint16_t)(trk_e(t) | 0x400), 0x1fc0);
    IoSetAddr((uint16_t)(trk_e(t) | 0x2040), (uint32_t)t.spuAddr);
    if (reserve) {
        s_uiVoice[s_usCore] |= trk_bit(t);   /* delay slot of the CpuSuspendIntr jal */
        iopk::CpuSuspendIntr(&st);
        IoHsynReserve(s_uiVoice);
        iopk::CpuResumeIntr(st);
    }
    return 1;
}

/* 0x1744 */
int CommandSetLoop(int idx, uint8_t v)
{
    if (!trk_ok(idx)) return 0;
    s_tr[idx].loop = v;
    return 1;
}

/* 0x179c */
int CommandSetVoidAutoPlay(int idx, uint8_t v)
{
    if (!trk_ok(idx)) return 0;
    s_tr[idx].autoplay = v;
    return 1;
}

/* 0x17f4: only the flag; the play thread applies it through TrackSetVolume (Q-V10) */
int CommandSetVolumeLeft(int idx, uint16_t v)
{
    if (!trk_ok(idx)) return 0;
    s_tr[idx].volL = v;
    s_boMstrmSetVolume = 1;
    return 1;
}

/* 0x1854 */
int CommandSetVolumeRight(int idx, uint16_t v)
{
    if (!trk_ok(idx)) return 0;
    s_tr[idx].volR = v;
    s_boMstrmSetVolume = 1;
    return 1;
}

/* 0x18b4: register at once, flag untouched */
int CommandSetVolumeLeftNow(int idx, uint16_t v)
{
    if (!trk_ok(idx)) return 0;
    Track &t = s_tr[idx];
    t.volL = v;
    IoSetParam((uint16_t)trk_e(t), t.volL);
    return 1;
}

/* 0x1948 */
int CommandSetVolumeRightNow(int idx, uint16_t v)
{
    if (!trk_ok(idx)) return 0;
    Track &t = s_tr[idx];
    t.volR = v;
    IoSetParam((uint16_t)(trk_e(t) | 0x100), t.volR);
    return 1;
}

/* 0x19e0: core = cmd & 0xff, not limited to 0 / 1 */
int CommandSetMasterVolumeLeftNow(int core, uint16_t v)
{
    IoSetParam((uint16_t)((uint32_t)core | 0x980), v);
    return 1;
}

/* 0x1a04 */
int CommandSetMasterVolumeRightNow(int core, uint16_t v)
{
    IoSetParam((uint16_t)((uint32_t)core | 0xa80), v);
    return 1;
}

/* 0x1a28 */
int CommandGetStatus() { return s_iMstrmStatus; }

/* 0x1a38 */
int CommandGetTrStatus(int idx)
{
    if (!trk_ok(idx)) return 0;
    return s_tr[idx].state;
}

/* 0x1a90: a resident, non-looping, idle (ready / ended) track = "SE track"; s_uiSubMode bit 1 blocks all */
int CommandIsEnableSeTr(int idx)
{
    if (s_uiSubMode & 1) return 0;
    if (!trk_ok(idx)) return 0;
    if (trk_isMaster(idx)) return 0;
    const Track &t = s_tr[idx];
    if (t.state != 7 && t.state != 2) return 0;
    if (t.onMem != 1) return 0;
    if (t.loop != 0) return 0;
    return 1;
}

/* 0x1b98 */
int CommandGetVolumeLeft(int idx)
{
    if (!trk_ok(idx)) return 0;
    return IoGetParam((uint16_t)(trk_e(s_tr[idx]) | 0x600)) & 0xffff;
}

/* 0x1c18 */
int CommandGetVolumeRight(int idx)
{
    if (!trk_ok(idx)) return 0;
    return IoGetParam((uint16_t)(trk_e(s_tr[idx]) | 0x700)) & 0xffff;
}

/* 0x1c98 */
int CommandGetMasterVolumeLeft(int core) { return IoGetParam((uint16_t)((uint32_t)core | 0x1180)) & 0xffff; }

/* 0x1cbc */
int CommandGetMasterVolumeRight(int core) { return IoGetParam((uint16_t)((uint32_t)core | 0x1280)) & 0xffff; }

/* ---- track / key layer */

/* 0x2998: rewrite the ADPCM flag byte (byte 1) of every 16-byte block of s_pucSendBuf. part 0 = first ring half (start
 * flag only), 1 = second half (end flag only), 2 = the whole buffer (both). A single block with part != 1 gets only the
 * start flag; a size that is not a multiple of 0x10 never matches the end test; size <= 0 writes nothing. */
void SetLoopFlag(int part, int size, int loop)
{
    uint8_t *p = iopk::Ram() + s_pucSendBuf;
    uint8_t f = loop ? 2 : 0;
    uint8_t fStart = (uint8_t)(f | 4), fEnd = (uint8_t)(f | 1);
    int last = (int)((uint32_t)size - 0x10);
    for (int off = 0; off < size; off += 0x10, p += 0x10) {   /* signed compare */
        if (off == 0 && part != 1)
            p[1] = fStart;
        else if (off == last && part != 0)
            p[1] = fEnd;
        else
            p[1] = f;
    }
    IoFlushDcache();
}

/* 0x2cfc: next track from start on (99 = none). mode 2 = next fully resident track; 0 / 1 = next track that still
 * needs streaming (file larger than a half-buffer, not resident, sectors left); other modes 99. */
int GetNextValidTrack(int start, int mode)
{
    mode &= 0xff;
    if (mode == 2) {
        for (int i = start; i < s_iMaxTr; i++) {
            if (trk_isMaster(i)) continue;
            const Track &t = s_tr[i];
            if (t.state != 3 && t.state != 2 && t.state != 5) continue;
            if (t.onMem == 1) return i;
        }
        return 99;
    }
    if (mode != 1 && mode != 0) return 99;
    for (int i = start; i < s_iMaxTr; i++) {
        if (trk_isMaster(i)) continue;
        const Track &t = s_tr[i];
        if (t.state != 3 && t.state != 2 && t.state != 5) continue;
        if (!(s_iHalfBuffSize < t.file.size)) continue;
        if (t.onMem != 0) continue;
        if (t.pos < t.file.sectors) return i;
    }
    return 99;
}

/* 0x2eb0: once per ring half. Playing tracks tick; a master-flag track with a partner bumps the partner's time and
 * copies it (the partner advances once more per linked track, Q-V8); a paused linked track copies the partner's time.
 * Non-looping ticked tracks that reached timeEnd are queued for key-off. */
void TrackAdvancePlayTime()
{
    if (s_iBuffTime <= 0) return;
    Assert(s_iMaxTr < 0x11, kTrackFile, 0x69b);
    int adv[16] = {0};   /* sp+0x18; host guard: the original's loops write / read [0, s_iMaxTr) (stack beyond 16) */
    int n = s_iMaxTr < 16 ? s_iMaxTr : 16;
    for (int i = 0; i < n; i++) {
        adv[i] = 0;
        Track &t = s_tr[i];
        uint8_t st = t.state;
        if (st == 3 || st == 4 || st == 5) {
            Track *p = trk_partner(t);
            if (p) {
                p->time++;
                t.time = p->time;
            } else {
                t.time++;
            }
            adv[i] = 1;
        } else if (st == 6) {
            Track *p = trk_partner(t);
            if (p) {
                t.time = p->time;
                adv[i] = 1;
            }
        }
    }
    for (int i = 0; i < s_iMaxTr && i < 16; i++) {
        if (!adv[i]) continue;
        Track &t = s_tr[i];
        if (t.loop != 0) continue;
        if (t.time < t.timeEnd) continue;   /* signed */
        t.time = 0;
        t.pos = 0;
        t.key = 2;
        s_boMstrmKeyOff = 1;
        if (trk_dbg()) Log("track end : %d\n", i);
    }
}

/* 0x3120: streaming tracks whose file turned out resident */
void TrackCheckOnMemory()
{
    for (int i = 0; i < s_iMaxTr; i++) {
        Track &t = s_tr[i];
        if (t.state == 3 && t.onMem != 0) t.state = 4;
    }
}

/* 0x3194: re-apply every used track's voice setup (reserve 1) */
void TrackSetVoice()
{
    for (int i = 0; i < s_iMaxTr; i++)
        if (s_tr[i].state != 0) CommandSetVoice(i, (uint32_t)s_tr[i].voice, 1);
}

/* 0x3214: mode 1 mutes the master track */
void TrackSetVolume()
{
    for (int i = 0; i < s_iMaxTr; i++) {
        const Track &t = s_tr[i];
        if (t.state == 0) continue;
        if (s_iMode == 1 && i == s_iMasterTr) {
            IoSetParam((uint16_t)trk_e(t), 0);
            IoSetParam((uint16_t)(trk_e(t) | 0x100), 0);
        } else {
            IoSetParam((uint16_t)trk_e(t), t.volL);
            IoSetParam((uint16_t)(trk_e(t) | 0x100), t.volR);
        }
    }
}

/* 0x3344: consume pending plays (force != 0 also keys on every ready track; the game only calls KeyOn(0)) */
void KeyOn(int force)
{
    if (!s_boMstrmKeyOn && !force) return;
    uint32_t mask = 0;
    for (int i = 0; i < s_iMaxTr; i++) {
        Track &t = s_tr[i];
        uint8_t st = t.state;
        if (st == 0) continue;
        if (!(t.key == 1 || (force && st == 2))) continue;
        if (t.master == 1) {
            t.state = 5;
        } else {
            mask |= trk_bit(t);
            t.state = t.onMem ? 4 : 3;
        }
        t.key = 0;
    }
    if (mask) {
        IoSetSwitch((uint16_t)(s_usCore | 0x1500), mask);
        if (trk_dbg()) Log("key on : %08x\n", mask);
    }
    s_boMstrmKeyOn = 0;
}

/* 0x348c: consume pending stops (close the file, state 7) / pauses (state 6); force != 0 stops every playing / paused
 * track. The key byte is cleared on every non-empty track, a pending play included (Q-V7). */
void KeyOff(int force)
{
    if (!s_boMstrmKeyOff && !force) return;
    uint32_t mask = 0;
    for (int i = 0; i < s_iMaxTr; i++) {
        Track &t = s_tr[i];
        uint8_t st = t.state;
        if (st == 0) continue;
        uint8_t k = t.key;
        if (k == 2 || (force && st >= 3 && st <= 6)) {
            if (t.master != 1) mask |= trk_bit(t);
            File_Close(&t.file);
            t.state = 7;
        } else if (k == 3) {
            if (t.master != 1) mask |= trk_bit(t);
            t.state = 6;
        }
        t.key = 0;
    }
    if (mask) {
        IoSetSwitch((uint16_t)(s_usCore | 0x1600), mask);
        if (trk_dbg()) Log("key off : %08x\n", mask);
    }
    s_boMstrmKeyOff = 0;
}

}  // namespace mvag
