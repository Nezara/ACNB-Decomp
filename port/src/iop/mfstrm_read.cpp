/* mfstrm mstream.c + stream.c, G3 (read engine) and G4 live (data out): the update pass mstrm_Update (one stream read
 * at a time, chosen by priority with a round robin among equals), the CD callback, strm_Read / _strm_ReadCdvd /
 * strm_Polling (issue a sector read into the ring, complete it on the next pass), strm_Sync / strm_Seek /
 * _strm_SeekCdvd, mstrm_Sync / IsDevBusy, and the IVAG-facing data exports GetData / GetDataSync / ReleaseData /
 * DebugOut, and the host-file read / seek of G5 (_read, _read_sync,
 * _strm_ReadHost, _strm_SeekHost; section 17, never reached in Versus). The ring-block helpers are in mfstrm_sbuf.cpp.
 *
 * Zero-time cascade (14.4): the update thread (0x19) preempts whoever signals g_iUpdateSema; a pass that issues a
 * read re-installs the CD callback and clears s_iCdvdEndReading BEFORE strm_Read (iopk::CdRead copies at once and
 * defers the callback to the next slice boundary, GH3-1); the callback's iSignalSema starts the completing pass, whose
 * own SignalSema starts the next one. GetData / ReleaseData signal g_iUpdateSema after the unlock (GH4-2); AttachSector
 * / Seek / SetReadStart signal it with the lock held (mfstrm_stream.cpp, GH2). */
#include "mfstrm_state.h"
#include <string.h>

namespace mfstrm {

/* unit-test seam (mfstrm_selftest.cpp): update passes since module start, and whether s_SemaID was held when the
 * last pass was entered (sampled with ReferSemaStatus: no scheduling effect) */
int g_update_calls;
int g_update_locked;

/* flags word +0x2c: the error / result field (bits 6..22) */
static inline uint32_t err_field(const StrmInfo *p) { return (p->flags << 9) >> 15; }
static inline uint32_t src_type(const StrmInfo *p) { return p->flags & 3; }

/* the loop wrap after a completed read (strm_Polling / strm_Sync / the sync arm of _strm_ReadCdvd): pos at or past
 * loop_end -> loop_start; a positive count is decremented, a count of 0 clears the loop bit (-1 loops forever).
 * Returns whether it wrapped (the host arms then lseek). */
static bool loop_wrap(StrmInfo *p)
{
    if (!(p->flags & 0x20) || p->pos < p->loop_end) return false;
    p->pos = p->loop_start;
    if (p->loop_count > 0) p->loop_count--;
    if (p->loop_count == 0) p->flags &= ~0x20u;
    return true;
}

/* ---------------------------------------------------------------- stream.c */

/* 0x68d8 (mstrm_Update): a ring, attached, and the ring wants data */
int strm_RequiredReading(StrmInfo *p)
{
    if (!p) return 0;
    if (!p->hSBuf) return 0;
    if (!(p->flags & 8)) return 0;
    return sbuf_RequiredRead(p->hSBuf, p->reload);
}

/* 0x5584 (strm_Read): refused while a read is issued / in flight (state 2 / 3, or the never-stored 8). The sector count
 * is the length rounded up to 0x800 and stored as rsize, but the ring block is exactly len (Q-G3a: the CD copy can
 * run up to 0x7ff bytes past it). Success: state 3, error field cleared; sync = 1 (never in Versus) waits, advances
 * pos, wraps, frees the block, state 4. Failure (REV): error field from sceCdGetError, forced to 0x31 when it is 0 and
 * the drive reports status 1; rsize 0, state 4, two Kprintf lines, 0. dst is an IOP address. */
int _strm_ReadCdvd(StrmInfo *p, uint32_t dst, int len, int sync)
{
    int s = p->state;
    if ((uint32_t)(s - 2) < 2 || s == 8) return 0;
    len = (int)(((uint32_t)len + 0x7ff) & ~0x7ffu);
    p->rsize = len;
    p->state = 2;
    uint32_t lsn = (uint32_t)(p->handle + (p->pos >> 11));
    iopk::CdSync(0);
    if (iopk::CdRead(lsn, (uint32_t)(len >> 11), iopk::Ptr(dst), p->cdmode) != 0) {
        p->state = 3;
        p->flags = (p->flags & 0xff80003fu) | 0;
        if (sync == 0) return 1;
        iopk::CdSync(0);   /* REV */
        p->pos += len;
        loop_wrap(p);
        p->state = 4;
        sbuf_FreeReadBlock(p->hSBuf);
        return 1;
    }
    uint32_t e = (uint32_t)iopk::CdGetError();
    p->flags = (p->flags & 0xff80003fu) | ((e & 0x1ffff) << 6);
    if (err_field(p) == 0 && iopk::CdStatus() == 1) p->flags = (p->flags & 0xff80003fu) | 0xc40;
    p->rsize = 0;
    p->state = 4;
    iopk::Printf("%s : ERROR!!! ", "_strm_ReadCdvd");
    iopk::Printf("sceCdRead() is failed...err=%d\n", (int)err_field(p));
    return 0;
}

/* 0x61a0 (mstrm_Update, sync 0): clamps n to the bytes left in the file and (loop bit) to loop_end, reserves a ring
 * block (BlockReq at the original sp+0x28: AllocReadBlock rounds len up and may shrink it), reads min(n, block) into
 * it. A refused block returns 0 without CancelReadBlock; a failing read cancels the block. */
int strm_Read(StrmInfo *p, int n, int sync)
{
    int n_in = n;
    if (!p) return 0;
    if (n <= 0) return 0;
    if (!p->hSBuf) return 0;
    if (p->flags & 4) return 0;
    if (p->state == 3 || p->state == 2) return 0;
    int pos = p->pos;
    if (p->size < n + pos) n = p->size - pos;
    if ((p->flags & 0x20) && p->loop_end < pos + n) n = p->loop_end - pos;
    BlockReq rq;
    rq.ptr = 0;
    rq.len = n;
    if (!sbuf_AllocReadBlock(p->hSBuf, &rq)) return 0;
    int len = n < rq.len ? n : rq.len;
    int r;
    switch (src_type(p)) {
    case 2: r = _strm_ReadCdvd(p, rq.ptr, len, sync); break;
    case 0:
    case 1: r = _strm_ReadHost(p, rq.ptr, len, sync); break;
    default: r = n_in; break;   /* type 3 is never stored: the asm returns its caller's s0 = mstrm_Update's n */
    }
    if (r == 0) sbuf_CancelReadBlock(p->hSBuf);
    return r;
}

/* 0x635c (mstrm_Update, the in-flight stream after the CD callback): 1 = still busy, 0 = done / nothing to do.
 * Sector arm: pos advances by the ROUNDED rsize (Q-G3b: can overshoot size), loop wrap, EOF (pos >= size: flags bit2
 * byte store, then state 6) or state 4, the block becomes valid data, the dma thread is released from its
 * DelayThread (result ignored). Host arm (REV): _read_sync(0) busy -> 1; a negative read result -> state
 * 4 + error field -result, 1; else the same completion with an lseek on the loop wrap. */
int strm_Polling(StrmInfo *p)
{
    if (!p) return 0;
    if (src_type(p) == 2) {
        if (p->state != 3) return 0;
        if (iopk::CdSync(1) != 0) return 1;
        p->pos += p->rsize;
        loop_wrap(p);
        if (p->pos < p->size) {
            p->state = 4;
        } else {
            p->flags |= 4;
            p->state = 6;
        }
        sbuf_FreeReadBlock(p->hSBuf);
        if (g_DmaThID >= 0) iopk::ReleaseWaitThread(g_DmaThID);
        return 0;
    }
    if (p->state != 3) return 0;
    if (_read_sync(0) != 0) return 1;
    if (s_HostRead.result < 0) {
        p->state = 4;
        p->flags = (p->flags & 0xff80003fu) | (((uint32_t)-s_HostRead.result & 0x1ffff) << 6);
        return 1;
    }
    p->pos += p->rsize;
    if (loop_wrap(p)) iopk::Lseek(p->handle, p->loop_start + p->w10, 0);
    if (p->pos < p->size) {
        p->state = 4;
    } else {
        p->flags |= 4;
        p->state = 6;
    }
    sbuf_FreeReadBlock(p->hSBuf);
    if (g_DmaThID >= 0) iopk::ReleaseWaitThread(g_DmaThID);
    return 0;
}

/* 0x6670 (mstrm_Sync, strm_Close; REV): sector arm only waits for the drive - no pos advance, no free, state stays 3
 * (Q-G3g; the next update pass completes the read). Host arm: waits for the host read (_read_sync(1), result
 * ignored), then completes it like strm_Polling but without the dma-thread release; returns 1. */
int strm_Sync(StrmInfo *p)
{
    if (!p) return 0;
    if (src_type(p) == 2) {
        if (p->state != 3) return 0;
        iopk::CdSync(0);
        return 1;
    }
    if (p->state != 3) return 0;
    _read_sync(1);
    if (s_HostRead.result < 0) {
        p->state = 4;
        p->flags = (p->flags & 0xff80003fu) | (((uint32_t)-s_HostRead.result & 0x1ffff) << 6);
        return 1;
    }
    p->pos += p->rsize;
    if (loop_wrap(p)) iopk::Lseek(p->handle, p->loop_start + p->w10, 0);
    if (p->pos < p->size) {
        p->state = 4;
    } else {
        p->flags |= 4;
        p->state = 6;
    }
    sbuf_FreeReadBlock(p->hSBuf);
    return 1;
}

/* 0x5890 (strm_Seek; REV): pos rounded up to 0x800; sceCdSeek retried until nonzero (iopk's returns 1: one call), the
 * ring drained by consuming the whole buffer size (RemoveDataBlock clamps to the valid bytes; pBufEnd stays), EOF bit
 * cleared when pos < size, state 1 */
int _strm_SeekCdvd(StrmInfo *p, int pos)
{
    int s = p->state;
    if ((uint32_t)(s - 2) < 2 || s == 8) return 0;
    pos = (int)(((uint32_t)pos + 0x7ff) & ~0x7ffu);
    while (iopk::CdSeek((uint32_t)((p->w10 >> 11) + p->handle + (pos >> 11))) == 0) {}
    iopk::CdSync(0);
    p->pos = pos;
    sbuf_RemoveDataBlock(p->hSBuf, sbuf_GetBufferSize(p->hSBuf));
    if (p->pos < p->size) p->flags &= ~4u;
    p->state = 1;
    return 1;
}

/* 0x6848 (mstrm_Seek; REV): by source type; type 3 -> 0 */
int strm_Seek(StrmInfo *p, int pos)
{
    if (!p) return 0;
    if (!p->hSBuf) return 0;
    switch (src_type(p)) {
    case 2: return _strm_SeekCdvd(p, pos);
    case 0:
    case 1: return _strm_SeekHost(p, pos);
    default: return 0;
    }
}

/* ---- G5: the host / HDD read path (source type 0 / 1). Protocol with _strm_Thread_Host (prio 0x18, mfstrm_stream.cpp):
 * _read posts the job in s_HostRead and signals s_iSemaHost; the thread takes s_iSyncSemaHost ("busy"), runs the
 * synchronous read into s_HostRead.result, gives it back ("done") and wakes the update thread. Called from an IOP thread
 * below 0x18 the whole read has run before _read returns (g_iUpdateSema left at 1). */

/* 0x4c2c: a zero-byte read probes the descriptor (Q-G5j: the only validity test; the original reads into a stack
 * scratch at sp+0x20); dst is an IOP address */
int _read(int fd, uint32_t dst, int len)
{
    uint8_t scratch[0x40];
    int r = iopk::Read(fd, scratch, 0);
    if (r < 0) return r;
    s_HostRead.fd = fd;
    s_HostRead.buf = dst;
    s_HostRead.size = len;
    s_HostRead.result = 0;
    iopk::SignalSema(s_iSemaHost);
    return 1;
}

/* 0x4cb4: wait != 0 -> wait for the host read to finish (take + give back the sync sema), 0. Else poll: taken -> give it
 * back, 0 (done); KE_SEMA_ZERO -> 1 (busy); any other poll error -> 0. */
int _read_sync(int wait)
{
    if (wait) {
        iopk::WaitSema(s_iSyncSemaHost);
        iopk::SignalSema(s_iSyncSemaHost);
        return 0;
    }
    int r = iopk::PollSema(s_iSyncSemaHost);
    if (r < 0) return r == iopk::KE_SEMA_ZERO;
    iopk::SignalSema(s_iSyncSemaHost);
    return 0;
}

/* 0x538c (strm_Read): refused in states 2 / 3 / 8 (two Kprintf lines). rsize = len (not rounded: unlike the CD path),
 * state 2, job posted. Post failure: error field = -rc, rsize 0, state stays 2 (Q-G5c: the stream refuses every later
 * read / CD open until closed; strm_Read cancels the block). Success: state 3, error field cleared; sync = 1 waits,
 * advances pos by the requested len (the thread's result is not checked), loop wrap with an lseek to
 * loop_start + w10 BEFORE the pos store, state 4, block committed. dst is an IOP address. */
int _strm_ReadHost(StrmInfo *p, uint32_t dst, int len, int sync)
{
    int s = p->state;
    if ((uint32_t)(s - 2) < 2 || s == 8) {
        iopk::Printf("%s : ERROR!!! ", "_strm_ReadHost");
        iopk::Printf("status == %d\n", p->state);
        return 0;
    }
    p->rsize = len;
    p->state = 2;
    int rc = _read(p->handle, dst, len);
    if (rc < 0) {
        iopk::Printf("%s : ERROR!!! ", "_strm_ReadHost");
        iopk::Printf("read() is failed...ret=%d\n", rc);
        p->flags = (p->flags & 0xff80003fu) | ((((uint32_t)-rc) & 0x1ffff) << 6);
        p->rsize = 0;
        return 0;
    }
    p->state = 3;
    p->flags = (p->flags & 0xff80003fu) | 0;
    if (!sync) return 1;
    _read_sync(1);
    p->pos += len;
    if ((p->flags & 0x20) && p->pos >= p->loop_end) {
        iopk::Lseek(p->handle, p->loop_start + p->w10, 0);
        p->pos = p->loop_start;
        if (p->loop_count > 0) p->loop_count--;
        if (p->loop_count == 0) p->flags &= ~0x20u;
    }
    p->state = 4;
    sbuf_FreeReadBlock(p->hSBuf);
    return 1;
}

/* 0x57d4 (strm_Seek): refused in states 2 / 3 / 8; lseek to pos + w10 (result ignored), pos stored unrounded (unlike
 * _strm_SeekCdvd), the ring drained like the CD arm, EOF cleared when pos < size, state 1 */
int _strm_SeekHost(StrmInfo *p, int pos)
{
    int s = p->state;
    if ((uint32_t)(s - 2) < 2 || s == 8) return 0;
    iopk::Lseek(p->handle, pos + p->w10, 0);
    p->pos = pos;
    sbuf_RemoveDataBlock(p->hSBuf, sbuf_GetBufferSize(p->hSBuf));
    if (p->pos < p->size) p->flags &= ~4u;
    p->state = 1;
    return 1;
}

/* ---------------------------------------------------------------- mstream.c */

/* 0x1f9c: sceCdCallback target (installed by mstrm_Update at every read issue; iopk runs it as a deferred call with
 * the interrupt context set) */
void _mstrm_CdSyncCB(int cause)
{
    if (cause == 1) {
        s_iCdvdEndReading = 1;
        iopk::iSignalSema(g_iUpdateSema);
    }
}

/* 0x2868 (_mstrm_Thread, once per g_iUpdateSema): walks the alloc list (sorted by priority, _mstrm_SortInfo). A stream
 * in state 2 / 3 stops the walk (one read at a time, GH3-2) and is polled once the CD callback has set
 * s_iCdvdEndReading; done -> SignalSema(g_iUpdateSema) for the next pass. Otherwise the best candidate (attached, read
 * start bit, state 1 / 4, the ring wants data) by smaller priority; an equal priority wins only as the round-robin
 * successor: last served priority equal, the previous list node (attached or not) has the same priority and the
 * last served index is this record's prev (Q-G3d). The chosen stream reads min(buffer size, packet size); the status
 * word's bits 1..17 always take its error field afterwards (Q6, Q-G3e). */
void mstrm_Update()
{
    {
        iopk::SemaInfo si;
        g_update_locked = iopk::ReferSemaStatus(s_SemaID, &si) == 0 && si.current_count == 0;
        g_update_calls++;
    }
    int best = -1, busy = -1, best_prio = 0x10000;
    StrmInfo *prev = 0;
    iopk::WaitSema(s_SemaID);
    for (int i = s_StrmMng.alloc.top; i != -1;) {
        StrmInfo *p = &s_StrmInfo[i];
        if (p->flags & 8) {
            if (p->state == 2 || p->state == 3) {
                busy = i;
                break;
            }
            if ((p->flags & 0x10) && (p->state == 1 || p->state == 4) && strm_RequiredReading(p)) {
                int pr = p->prio;
                if (pr < best_prio) {
                    best_prio = pr;
                    best = i;
                } else if (best_prio == pr && s_StrmMng.last_prio == best_prio && prev && prev->prio == pr &&
                           s_StrmMng.last_idx == p->prev) {
                    best = i;
                }
            }
        }
        i = p->next;
        prev = p;
    }
    if (busy == -1) {
        if (best != -1) {
            StrmInfo *p = &s_StrmInfo[best];
            int n = sbuf_GetBufferSize(p->hSBuf);
            s_StrmMng.last_prio = (uint16_t)best_prio;   /* GH3-5: after sbuf_GetBufferSize */
            s_StrmMng.last_idx = (int16_t)best;
            if (s_StrmMng.packet_size < n) n = s_StrmMng.packet_size;
            if (src_type(p) == 2) iopk::CdCallback(_mstrm_CdSyncCB);   /* re-installed at every read issue */
            s_iCdvdEndReading = src_type(p) == 2 ? 0 : 1;
            if (strm_Read(p, n, 0) == 0 && err_field(p) != 0) fsmfstrm_CallRpc(0x10000200, 0, best, (int)err_field(p));   /* REV */
            s_StrmMng.status = (s_StrmMng.status & 0xfffc0001u) | ((err_field(p) & 0x1ffff) << 1);
        }
    } else if (s_iCdvdEndReading != 0) {
        StrmInfo *q = &s_StrmInfo[busy];
        if (src_type(q) == 2) s_iCdvdEndReading = 0;
        if (strm_Polling(q) == 0) iopk::SignalSema(g_iUpdateSema);
    }
    iopk::SignalSema(s_SemaID);
}

/* 0x3c14 (export 17 Sync, mstrm_GetDataSync; REV): bad index -> 0; plain WaitSema, no update signal */
int mstrm_Sync(int idx)
{
    if (idx < 0 || idx >= 8) return 0;
    iopk::WaitSema(s_SemaID);
    int r = strm_Sync(&s_StrmInfo[idx]);
    iopk::SignalSema(s_SemaID);
    return r;
}

/* 0x3c94 (RPC 0x1000000; REV): arg != 0 -> sceCdSync(0x10) and 0 (the `move v0, zero` sits in the branch delay
 * slot after the call: the result is dropped, Q-G3f); arg 0 -> sceCdSync(0x11)'s result */
int mstrm_IsDevBusy(int arg)
{
    if (arg != 0) {
        iopk::CdSync(0x10);
        return 0;
    }
    return iopk::CdSync(0x11);
}

/* IOP memcpy between two IOP addresses (the ring and IVAG's buffer, both in iopk::Ram()) */
static void iop_memcpy(uint32_t dst, uint32_t src, int n)
{
    if (n <= 0) return;
    uint8_t *d = iopk::Ptr(dst), *s = iopk::Ptr(src);
    if (!d || !s || !iopk::Ptr(dst + (uint32_t)n - 1) || !iopk::Ptr(src + (uint32_t)n - 1)) {
        iopk::Printf("mfstrm: memcpy %08x <- %08x (%x) outside IOP RAM\n", dst, src, n);
        return;
    }
    memmove(d, s, (size_t)n);
}

/* 0x34f0 (dma_Update without a packet buffer): the ring block at the read head, *plen clamped to the valid bytes;
 * nothing is removed (dma_Update releases after the DMA), no attached check, plain WaitSema, no g_iUpdateSema signal.
 * 0 for plen 0 / bad index (no lock) / no data (*plen untouched). The BlockReq's ptr word is stale in the original
 * (sbuf_GetDataBlock writes it before every read). */
uint32_t mstrm_GetBufferData(int idx, int *plen)
{
    if (!plen) return 0;
    if (idx < 0 || idx >= 8) return 0;
    iopk::WaitSema(s_SemaID);
    BlockReq rq;
    rq.ptr = 0;
    rq.len = *plen;
    uint32_t p = sbuf_GetDataBlock(s_StrmInfo[idx].hSBuf, &rq);
    if (p) *plen = rq.len;
    iopk::SignalSema(s_SemaID);
    return p;
}

/* 0x365c (export 15 GetData; IVAG): copies min(n, valid) bytes from the read head to the IOP address dst and consumes
 * them. dst 0 / bad index -> -1 without a lock; PollSema-then-WaitSema lock (GH1); not attached -> -1, lock released,
 * no update signal; no ring -> -1 with both signals. sbuf_GetDataBlock is not wrap-aware: a request straddling the
 * ring end copies the bytes after the ring (GH4-3, kept); the second-block arm is dead (rq.len < n implies valid < n,
 * kept literally). Both signals after the copy: unlock, then g_iUpdateSema (the update thread preempts here and a
 * refill cascade runs before the return). */
int mstrm_GetData(int idx, uint32_t dst, int n)
{
    if (dst == 0) return -1;
    if (idx < 0 || idx >= 8) return -1;
    if (iopk::PollSema(s_SemaID) == iopk::KE_SEMA_ZERO) iopk::WaitSema(s_SemaID);
    if (!(s_StrmInfo[idx].flags & 8)) {
        iopk::SignalSema(s_SemaID);
        return -1;
    }
    StrmBuffInfo *const *h = &s_StrmInfo[idx].hSBuf;
    int valid = sbuf_GetValidDataSize(*h);
    if (valid < 0) {
        iopk::SignalSema(s_SemaID);
        iopk::SignalSema(g_iUpdateSema);
        return -1;
    }
    BlockReq rq;
    rq.ptr = 0;
    rq.len = n;
    uint32_t src = sbuf_GetDataBlock(*h, &rq);
    if (src == 0) {
        n = 0;
    } else {
        iop_memcpy(dst, src, rq.len);
        sbuf_RemoveDataBlock(*h, rq.len);
        if (rq.len < n && !(valid < n)) {   /* dead (0x37c0 .. 0x3820) */
            dst += (uint32_t)rq.len;
            rq.len = n - rq.len;
            src = sbuf_GetDataBlock(*h, &rq);
            if (src != 0) {
                iop_memcpy(dst, src, rq.len);
                sbuf_RemoveDataBlock(*h, rq.len);
                rq.len = n;
            } else {
                rq.len = n - rq.len;
            }
        }
        n = rq.len;
    }
    iopk::SignalSema(s_SemaID);
    iopk::SignalSema(g_iUpdateSema);
    return n;
}

/* 0x35bc (export 18 RemoveData; IVAG): bad index -> -1; plain WaitSema; no attached check, both signals even when
 * sbuf_RemoveDataBlock fails (Q-G4b) */
int mstrm_ReleaseData(int idx, int n)
{
    if (idx < 0 || idx >= 8) return -1;
    iopk::WaitSema(s_SemaID);
    int r = sbuf_RemoveDataBlock(s_StrmInfo[idx].hSBuf, n);
    iopk::SignalSema(s_SemaID);
    iopk::SignalSema(g_iUpdateSema);
    return r;
}

/* 0x3ef4 f_3ef4 (export 22 DebugOut; REV): lock, walk the alloc list, unlock - no output (the sibling build printed
 * the list) */
void mstrm_DebugOut()
{
    iopk::WaitSema(s_SemaID);
    for (int i = s_StrmMng.alloc.top; i != -1; i = s_StrmInfo[i].next) {}
    iopk::SignalSema(s_SemaID);
}

/* 0x6c20: the second line of GetDataSync's error (Shift-JIS from the image, ends with a newline) */
static const char s_GetDataSyncNone[] =
    "\x91\x53\x82\xad\x93\xc7\x82\xdf\x82\xc4\x82\xa2\x82\xdc\x82\xb9\x82\xf1\xa5\xa5\xa5\n";

/* 0x3878 (export 16 GetDataSync; IVAG's header read): GetData, then while bytes are missing and the stream is not
 * exhausted: DebugOut, mstrm_Sync, wait in DelayThread(8000) steps until data is valid, GetData again at dst + got.
 * A GetData result <= 0 loops again with n unchanged (Q-G4c: spins without a kernel call if it keeps failing while
 * GetRemainSize > 0; no Versus path reaches it). got == 0 -> four Kprintf lines. */
int mstrm_GetDataSync(int idx, uint32_t dst, int n)
{
    int got = 0;
    int r = mstrm_GetData(idx, dst, n);
    if (r > 0) {
        n -= r;
        got += r;
    }
    while (n > 0) {
        if (mstrm_GetRemainSize(idx) <= 0 && mstrm_GetValidDataSize(idx) == 0) break;
        mstrm_DebugOut();
        mstrm_Sync(idx);
        if (mstrm_GetValidDataSize(idx) == 0)
            do iopk::DelayThread(8000); while (mstrm_GetValidDataSize(idx) == 0);
        r = mstrm_GetData(idx, dst + (uint32_t)got, n);
        if (r > 0) {
            n -= r;
            got += r;
        }
    }
    if (got == 0) {
        iopk::Printf("%s : ERROR!!! ", "mstrm_GetDataSync");
        iopk::Printf(s_GetDataSyncNone);
        iopk::Printf("%s : ERROR!!! ", "mstrm_GetDataSync");
        iopk::Printf("Size=%d, Now=%d\n", s_StrmInfo[idx].size, s_StrmInfo[idx].pos);
    }
    return got;
}

}  // namespace mfstrm
