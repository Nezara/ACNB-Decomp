/* mvag G3 (map section 13): ring buffer setup, the two file readers (first sector / one half-buffer), the loader that
 * ships the send buffer to SPU2 RAM (or to the EE), PreLoad / EntryLoad and the play thread (fsiopsdmtvag.c
 * 0x1d38 .. 0x37e0, 0x4bb4). All arithmetic is the R3000's (32-bit, signed compares where the asm uses slt / blez /
 * bgez). The one deliberate deviation from the original is D-V1 (SetupBuffer clamps the track count to the 16-record
 * table); everything else, including the sector bookkeeping quirks Q-V12 / Q-V13, is kept. */
#include "mvag_state.h"
#include "../../platform/plat.h"
#include "../../platform/snd/iop/iopdiv.h"
#include <limits.h>
#include <string.h>

namespace mvag {

static bool dbgOn() { return (s_uiSubMode & 0x10000) != 0; }
static uint8_t *sendp() { return iopk::Ram() + s_pucSendBuf; }   /* as SetLoopFlag: host view of s_pucSendBuf */
static int32_t shl11(int32_t v) { return (int32_t)((uint32_t)v << 11); }
static void zeroBytes(uint8_t *base, int32_t start, int32_t count)   /* byte loop `for (k < count)`, signed */
{
    for (int32_t k = 0; k < count; k++) base[start + k] = 0;
}

/* ReadPreloadFile's div / mfhi (`bnez; break 7`, Q-V16): IOP divide-by-zero rule (iopdiv.h) */

/* the linked partner of a master track (master == 1 && syncTr != -1); host guard as G2: an index outside the table
 * counts as "no partner" (one plat_log; the original reads / writes beyond s_tr, Q-V8) */
static Track *Partner(const Track &t)
{
    if (t.master != 1 || t.syncTr == -1) return 0;
    if (t.syncTr < 0 || t.syncTr >= 16) {
        static int logged;
        if (!logged++)
            plat_log("mvag: ReadHalfBufferFile: syncTr %d outside the track table, treated as none (host guard)", t.syncTr);
        return 0;
    }
    return &s_tr[t.syncTr];
}

/* ---- SetupBuffer (0x1d38) */
void SetupBuffer(int maxTr, int buffSize, int spuStart, int fromStart)
{
    /* D-V1 (deliberate deviation, host safety): the original stores maxTr unclamped and writes maxTr records into the
     * 16-record table (Q-V11). The addresses below still use the unclamped maxTr, as the original computes them. */
    int n = maxTr;
    if (n > 16) {
        plat_log("mvag: SetupBuffer: maxTr %d clamped to 16 (D-V1; the original overruns s_tr)", maxTr);
        n = 16;
    }
    s_iMaxTr = n;
    if (buffSize < 0x1000) buffSize = 0x1000;   /* signed */
    const int32_t b = buffSize & -0x1000;
    s_iBuffSize = b;
    const int32_t h = b / 2;   /* sra with the negative round-up = C division */
    s_iHalfBuffSize = h;
    s_iReadSector = h / 0x800;
    s_iMstTrTransSize = h;
    for (int i = 0; i < n; i++) {
        Track &t = s_tr[i];
        t.voice = i;
        t.volL = 0;
        t.volR = 0;
        t.loop = 0;
        t.autoplay = 0;
        t.key = 0;
        t.pos = 0;
        t.time = 0;
        t.timeEnd = 0;
        t.head = 0;
        t.tail = 0;
        t.lastBytes = 0;
        t.state = 0;
        t.onMem = 0;
        if (fromStart)
            t.spuAddr = (int32_t)((uint32_t)spuStart + (uint32_t)b * (uint32_t)i);
        else
            t.spuAddr = (int32_t)((uint32_t)spuStart - (uint32_t)b * (uint32_t)(maxTr - i));
    }
    if (!dbgOn()) return;
    Log("*** fssdmtvag buffer setup info ********************************************\n");
    Log("   spu2 memory : 0x%x - 0x%x (%d (%4d K))\n", 0, 0x1fffff, 0x1fffff, 0x7ff);
    if (fromStart) Log("   start address : 0x%x (%d (%4d K))\n", spuStart, spuStart, spuStart / 1024);
    for (int i = 0; i < s_iMaxTr; i++) {
        uint32_t a = (uint32_t)s_tr[i].spuAddr;
        Log("   +-- 0x%06x (%8d (%4d K)) ------+\n", a, (int)a, (int)(a >> 10));
        Log("   | %2d                                   |\n", i);
    }
    Log("   +--------------------------------------+\n");
    /* the second argument is register residue in the original */
    if (!fromStart) Log("   end address : 0x%x (%d)\n", spuStart, 0);
    Log("****************************************************************************\n");
}

/* ---- JudgeDynamicMasterTrack (0x1f84): mode 2 only - the streaming track (state 3, not master) with the largest
 * last read becomes the master; the first wins ties; none -> 99 / 0 */
void JudgeDynamicMasterTrack()
{
    if (s_iMode == 1) return;
    s_iMstTrTransSize = 0;
    s_iMasterTr = 99;
    const int32_t maxTr = s_iMaxTr;
    int32_t best = 0;
    for (int i = 0; i < maxTr; i++) {
        const Track &t = s_tr[i];
        if (t.state != 3 || t.master == 1) continue;
        if (best < t.lastBytes) {
            s_iMasterTr = i;
            s_iMstTrTransSize = t.lastBytes;
            best = t.lastBytes;
        }
    }
    if (dbgOn())
        Log(">>> master track is %d (%d byte read). buff_side=%d\n", s_iMasterTr, s_iMstTrTransSize, s_iBusSide);
}

/* ---- MakeSilentBuffer (0x2090) */
void MakeSilentBuffer()
{
    for (int32_t k = 0; k < s_iHalfBuffSize; k++) sendp()[k] = 0;
}

/* ---- ReadPreloadFile (0x20d0): the first sector -> header / tail, sector bookkeeping, timeEnd; 1 ok / 0 fail */
int ReadPreloadFile(int idx)
{
    Track &t = s_tr[idx];
    File *f = &t.file;
    uint8_t *send = sendp();
    if (File_GetLocation(f) <= 0) {
        if (dbgOn()) Log("<ReadPreloadFile> [%d] get location failed.\n", idx);
        return 0;
    }
    if (File_ReadSector(f, send, 0, 1) != 1) {
        if (dbgOn()) Log("<ReadPreloadFile> [%d] read failed.\n", idx);
        return 0;
    }
    if (send[0] == 'V' && send[1] == 'A' && send[2] == 'G') {
        t.head = 0x40;
        t.tail = 0x10;
    } else {
        t.head = 0;
        t.tail = 0;
    }
    if (!(t.tail < f->rem)) {   /* Q-V12: the trailer lies in the last sector (also rem 0 without a header) */
        f->sectors--;
        f->size -= t.tail;
        int32_t s = f->size;
        int32_t r = s & 0x7ff;
        if (s < 0 && r != 0) r -= 0x800;
        f->rem = r;
        t.tail = 0;
    }
    int32_t lo, hi;
    lo = IOP_DIV(f->sectors, s_iReadSector, 0x22d8, "mvag ReadPreloadFile s_iReadSector 0");
    t.timeEnd = lo;
    hi = IOP_REM(f->sectors, s_iReadSector, 0x22f0, "mvag ReadPreloadFile s_iReadSector 0");
    if (hi > 0) t.timeEnd++;
    if (dbgOn())   /* "sector=" receives the fd as well (a3 = a2 in the original) */
        Log("<ReadPreloadFile> [%d] fd=%d, sector=%d, filesize=%d, head=%d(%02x %02x %02x), tail=%d\n", idx, f->fd, f->fd,
            f->size, t.head, send[0], send[1], send[2], t.tail);
    return 1;
}

/* ---- ReadHalfBufferFile (0x23bc): one half-buffer of file data into the send buffer; returns its byte count */
int ReadHalfBufferFile(int idx)
{
    Track &t = TrAt(idx);   /* D-V2 */
    File *f = &t.file;
    uint8_t *send = sendp();
    Track *pt = Partner(t);
    int32_t pos = t.pos;
    if (pt) {   /* linked: follow the partner's read position; the only end-of-file exit (Q-V13) */
        pos = pt->pos;
        t.pos = pos;
        if (!(t.pos < f->sectors)) return 0;
    }
    File_ReadSector(f, send, pos, s_iReadSector);   /* result ignored; mode 1 never adds f->off */
    const int32_t rs = s_iReadSector;
    t.pos = pos + rs;
    int32_t n = shl11(rs);
    if (!(t.pos < f->sectors)) {   /* last chunk (also when rs overshoots) */
        if (dbgOn())
            Log("! read tail ! [%d] sec=%d/%d, byte=%d/%d, size=%d\n", idx, t.pos, f->sectors, n, s_iHalfBuffSize, f->size);
        const int32_t half = s_iHalfBuffSize;
        n = f->rem + shl11(f->sectors - pos - 1) - t.tail;   /* Q-V12: rem 0 counts as no bytes */
        if (!(half < f->size) && t.master != 1) t.onMem = 1;
        if (t.onMem != 1 && half < f->size) {
            const int32_t pad = shl11(s_iReadSector) - n;
            if (t.loop == 0) {
                zeroBytes(send, n, pad);
                n += pad;
            } else {
                t.pos = 0;   /* before the s2 test (Q-V13) */
                const int32_t s2 = pad / 2048;
                if (s2 <= 0) {
                    zeroBytes(send, n, pad);
                    n += pad;
                } else {   /* loop wrap: restart the file inside the pad */
                    int32_t r = pad & 0x7ff;
                    if (pad < 0 && r != 0) r -= 0x800;
                    const int32_t h = t.head;
                    const int32_t a1 = r + h;
                    const int32_t p = n + a1 - h;
                    zeroBytes(send, n, a1);
                    n += a1;
                    /* 0x40-byte stack buffer in the original (head is only ever 0 or 0x40); host guard on larger heads */
                    uint8_t save[0x40];
                    int32_t hs = t.head;
                    if (hs > 0x40) {
                        static int logged;
                        if (!logged++) plat_log("mvag: ReadHalfBufferFile: head %d > 0x40, header save clamped (host guard)", hs);
                        hs = 0x40;
                    }
                    for (int32_t k = 0; k < hs; k++) save[k] = send[p + k];
                    File_ReadSector(f, send + p, t.pos, s2);
                    t.pos += s2;
                    hs = t.head > 0x40 ? 0x40 : t.head;
                    for (int32_t k = 0; k < hs; k++) send[p + k] = save[k];   /* blanks the VAG header again */
                    n += shl11(s2) - t.head;
                    if (dbgOn())
                        Log("... repeat - read_byte=%d/%d, read_tail=%d/%d\n", n, s_iHalfBuffSize, p + shl11(s2),
                            s_iHalfBuffSize);
                }
            }
        }
    }
    if (pos == 0 && t.master != 1) zeroBytes(send, 0, t.head);   /* first chunk of the file: blank the header */
    if (pt) pt->pos = t.pos;
    if (dbgOn())
        Log("read file ... side=%d [%d] sec=%d/%d, byte=%d/%d, size=%d, voice=%d, time=%d/%d, loop=%d\n", s_iBusSide, idx,
            t.pos, f->sectors, n, s_iHalfBuffSize, f->size, t.voice, t.time, t.timeEnd, t.loop);
    return n;
}

/* ---- LoadTrack (0x2a3c): ship `size` bytes of the send buffer to the track's SPU2 half (`half` 0 / 1) or, for a
 * master track with an EE destination, to the EE by SIF DMA. Returns 0 on the EE path, 1 otherwise (Q-V14). */
int LoadTrack(int tr, int half, int size)
{
    Track &t = TrAt(tr);   /* D-V2 */
    if (t.master == 1 && t.eeDest != 0) {
        int st = 0;
        IoFlushDcache();
        const uint32_t cmd = (uint32_t)tr | 0x100;
        s_sdd.src = iopk::Ptr(s_pucSendBuf);
        s_sdd.dest = (uint32_t)t.eeDest;
        s_sdd.size = (uint32_t)size;
        s_sdd.attr = 0;
        /* w = {a3, sp+0x10, sp+0x14, sp+0x18, ..}: w[4] is an unwritten frame slot, w[5..7] the caller's saved s-regs */
        uint32_t w[8] = {0, (uint32_t)t.eeDest, (uint32_t)size, 0, 0, 0, 0, 0};
        IoCallRpc(cmd, w);
        iopk::CpuSuspendIntr(&st);
        const uint32_t id = IoSifSetDma(&s_sdd, 1);
        if (id == 0) Log("sceSifSetDma failed.\n");   /* unconditional */
        while (IoSifDmaStat(id) >= 0) {
        }   /* spins with interrupts suspended; iopk's SifDmaStat is always < 0 */
        iopk::CpuResumeIntr(st);
        w[0] = 1;
        w[1] = (uint32_t)t.eeDest;
        IoCallRpc(cmd, w);
        return 0;
    }
    if (!(s_iHalfBuffSize < t.file.size) && !(s_iMode == 1 && tr == s_iMasterTr))
        SetLoopFlag(2, size, t.loop);   /* fits one chunk: start + end flags */
    else if (t.onMem == 1)
        SetLoopFlag(half, size, t.loop);
    else if (!(t.pos < t.file.sectors))
        SetLoopFlag(half, size, t.loop);   /* last chunk */
    else
        SetLoopFlag(half, size, 1);   /* register residue 1 (`li a2, 1`): a mid-stream chunk loops */
    IoVoiceTrans(0, 0, s_pucSendBuf, (uint32_t)t.spuAddr + (uint32_t)s_iHalfBuffSize * (uint32_t)half, (uint32_t)size);
    return 1;
}

/* ---- PreLoad (0x3628): silence into the master (last) track's first half; status 3 */
int PreLoad()
{
    MakeSilentBuffer();
    s_iMstTrTransSize = s_iHalfBuffSize;
    s_iMasterTr = s_iMaxTr - 1;
    Assert(TrAt(s_iMasterTr).master != 1, "fsiopsdmtvag.c", 0x78e);   /* D-V2: s_iMaxTr <= 0 -> -1 */
    LoadTrack(s_iMasterTr, 0, s_iMstTrTransSize);
    s_iMstrmStatus = 3;
    return 1;
}

/* ---- EntryLoad (0x36d4): the first set track (state 1) whose first sector reads becomes ready; index or 99 */
int EntryLoad()
{
    for (int i = 0; i < s_iMaxTr; i++) {
        if (s_tr[i].state != 1) continue;
        if (!ReadPreloadFile(i)) continue;   /* a failing track stays state 1 */
        InitPlayTrack(i);
        Track &t = s_tr[i];
        t.state = 2;
        if (dbgOn()) Log("<EntryLoad> [%d] stat=%d, time=%d/%d\n", i, t.state, t.time, t.timeEnd);
        return i;
    }
    return 99;
}

/* ---- ProcPlayThread (0x4bb4): the play thread. V2: the loop head waits only when the previous pass's v0 is non-zero;
 * the ExecStatus* handlers always return 1 (Q-V17), so in practice every pass waits; status 3 falls through to Running
 * in the same pass when PreLoad reached status 4. s_boDmaToEe is never set (Q-V15), its EE callback is kept. */
void ProcPlayThread(void *)
{
    int out = 0;   /* sp+0x1c, the handlers' output slot */
    int again = 1;
    for (;;) {
        if (again != 0) iopk::WaitSema(s_iPlaySem);
        if (s_boMstrmSetVolume) {
            s_boMstrmSetVolume = 0;
            TrackSetVolume();
        }
        if (s_boDmaToEe) {
            s_boDmaToEe = 0;
            /* w = {1, sp+0x10 (saved ra), sp+0x14, sp+0x18, sp+0x1c = out, ..} */
            const uint32_t w[8] = {1, 0, 0, 0, (uint32_t)out, 0, 0, 0};
            IoCallRpc(0x100, w);
        }
        switch (s_iMstrmStatus) {
        case 5:
            again = ExecStatusTerminate(&out);
            break;
        case 4:
            again = ExecStatusRunning(&out);
            break;
        case 3:
            again = ExecStatusPreLoad(&out);
            if (s_iMstrmStatus != 4) break;
            again = ExecStatusRunning(&out);
            break;
        default:
            again = 1;
            break;
        }
    }
}

}  // namespace mvag
