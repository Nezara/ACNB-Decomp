/* mfstrm sbuf.c: the 8 ring-buffer records and their free / alloc lists (G1 = init, pool, allocation, size; G5 = Quit,
 * caller-supplied buffers, FreeStreamBuffer; G2
 * adds ClearData and GetValidDataSize, G3 the read blocks, G4 the data blocks). Buffers are AllocSysMemory blocks in iopk::Ram() (H6: CD data, the ring
 * arithmetic and IVAG's copies see one memory); the records hold their IOP addresses. */
#include "mfstrm_state.h"

namespace mfstrm {

/* 0x4160: the flags byte at +0x1e is not cleared (zero at load) */
void sbuf_Init()
{
    if (s_StrmBuffMng.flags & 1) return;
    for (int i = 0; i < 8; i++) {
        StrmBuffInfo *r = &s_StrmBuffInfo[i];
        r->pBuf = r->pBufTop = r->pBufEnd = 0;
        r->iBufSize = r->iReadSize = 0;
        r->pBufRead = 0;
        r->next = (int16_t)(i + 1);
        r->prev = (int16_t)(i - 1);
        r->index = (int16_t)i;
    }
    s_StrmBuffMng.free.top = 0;
    s_StrmBuffMng.alloc.num = 0;
    s_StrmBuffInfo[7].next = -1;
    s_StrmBuffMng.alloc.top = -1;
    s_StrmBuffMng.free.num = 8;
    s_StrmBuffMng.flags = (s_StrmBuffMng.flags & ~1u) | 1;
}

/* 0x3f68: pop the free-list head and push it on the alloc list (head's own prev is not written: -1 for a list head) */
StrmBuffInfo *_sbuf_GetStrmBufInfo()
{
    if (!(s_StrmBuffMng.flags & 1)) return 0;
    if (s_StrmBuffMng.free.num == 0) return 0;
    int16_t t = s_StrmBuffMng.free.top;
    StrmBuffInfo *r = &s_StrmBuffInfo[t];
    s_StrmBuffMng.free.top = r->next;
    if (s_StrmBuffMng.free.top != -1) s_StrmBuffInfo[s_StrmBuffMng.free.top].prev = r->prev;
    s_StrmBuffMng.free.num--;
    int16_t a = s_StrmBuffMng.alloc.top;
    if (a != -1) s_StrmBuffInfo[a].prev = t;
    r->next = a;
    s_StrmBuffMng.alloc.top = t;
    s_StrmBuffMng.alloc.num++;
    return r;
}

/* 0x405c: unlink from the alloc list, push on the free list head; the record body stays as it was (Q2) */
void _sbuf_ReleaseStrmBufInfo(StrmBuffInfo *r)
{
    if (!(s_StrmBuffMng.flags & 1)) return;
    if (!r) return;
    if (r->prev != -1) s_StrmBuffInfo[r->prev].next = r->next;
    if (r->next != -1) s_StrmBuffInfo[r->next].prev = r->prev;
    if (r->index == s_StrmBuffMng.alloc.top) s_StrmBuffMng.alloc.top = r->next;
    s_StrmBuffMng.alloc.num--;
    int16_t f = s_StrmBuffMng.free.top;
    if (f != -1) s_StrmBuffInfo[f].prev = r->index;
    r->next = f;
    r->prev = -1;
    s_StrmBuffMng.free.top = r->index;
    s_StrmBuffMng.free.num++;
}

/* 0x42ac: size rounded up to 0x800; returns &rec[r->index] */
StrmBuffInfo *sbuf_AllocStreamBuffer(int size)
{
    if (!(s_StrmBuffMng.flags & 1)) return 0;
    size = (int)(((uint32_t)size + 0x7ff) & ~0x7ffu);
    StrmBuffInfo *r = _sbuf_GetStrmBufInfo();
    if (!r) return 0;
    r->pBuf = iopk::AllocSysMemory(0, (uint32_t)size, 0);
    if (!r->pBuf) {
        _sbuf_ReleaseStrmBufInfo(r);
        return 0;
    }
    r->pBufEnd = r->pBuf;
    r->pBufTop = r->pBuf;
    r->iBufSize = size;
    r->iReadSize = 0;
    r->pBufRead = r->pBuf;
    r->flags = (uint8_t)((r->flags & ~1u) | 1);
    return &s_StrmBuffInfo[r->index];
}

/* 0x4ad4 (also mstrm_Update / mstrm_GetStreamBufferSize) */
int sbuf_GetBufferSize(const StrmBuffInfo *r)
{
    if (!(s_StrmBuffMng.flags & 1)) return -1;
    if (!r) return -1;
    if (!r->pBuf) return -1;
    return r->iBufSize;
}

/* 0x4928 (strm_OpenSector, the host / file opens): empties the ring; returns the OLD iReadSize (a leaked load,
 * Q-G2d), 0 when not initialised / no record / no buffer. The owned flag stays. */
int sbuf_ClearData(StrmBuffInfo *r)
{
    if (!(s_StrmBuffMng.flags & 1)) return 0;
    if (!r) return 0;
    if (!r->pBuf) return 0;
    int v = r->iReadSize;
    r->pBufEnd = r->pBuf;
    r->pBufTop = r->pBuf;
    r->iReadSize = 0;
    r->pBufRead = r->pBuf;
    return v;
}

/* 0x4a7c (the three mstrm getters, mstrm_GetData): valid bytes, -1 when not initialised / no record / no buffer */
int sbuf_GetValidDataSize(const StrmBuffInfo *r)
{
    if (!(s_StrmBuffMng.flags & 1)) return -1;
    if (!r) return -1;
    if (!r->pBuf) return -1;
    return r->iReadSize;
}

/* ---- G3: read blocks. pBufTop = read head, pBufEnd = write head, pBufRead = end of the block being read (== pBufEnd:
 * nothing pending). top == end means EMPTY before any read and FULL after one (Q-G3c: sbuf_FreeReadBlock's equal arm);
 * the free-run arithmetic uses unsigned compares of the heads, signed compares of the sizes. */

/* contiguous free run from the write head: to the ring end (top < end, or top == end), else up to the read head;
 * clamped to the total free bytes */
static int32_t free_run(const StrmBuffInfo *r)
{
    int32_t f;
    if (r->pBufTop < r->pBufEnd) f = r->iBufSize - (int32_t)(r->pBufEnd - r->pBuf);
    else if (r->pBufEnd < r->pBufTop) f = (int32_t)(r->pBufTop - r->pBufEnd);
    else f = r->iBufSize - (int32_t)(r->pBufTop - r->pBuf);
    if (r->iBufSize - r->iReadSize < f) f = r->iBufSize - r->iReadSize;
    return f;
}

/* 0x44f4 (strm_Read): reserves the next block at the write head; refused while a block is pending, or when the free
 * run (rounded down to 0x800) is under 0x800. rq->len is rounded up to 0x800 and clamped to the run (two stores);
 * pBufRead = block end, wrapped to pBuf at the ring end. Returns the block's IOP address. */
uint32_t sbuf_AllocReadBlock(StrmBuffInfo *r, BlockReq *rq)
{
    if (!(s_StrmBuffMng.flags & 1)) return 0;
    if (!rq) return 0;
    if (!r) return 0;
    if (!r->pBuf) return 0;
    if (r->pBufEnd != r->pBufRead) return 0;
    int32_t f = free_run(r) & ~0x7ff;
    if (f == 0) return 0;
    if (f < 0x800) return 0;
    rq->len = (int32_t)(((uint32_t)rq->len + 0x7ff) & ~0x7ffu);
    if (f < rq->len) rq->len = f;
    rq->ptr = r->pBufEnd;
    r->pBufRead = r->pBufEnd + (uint32_t)rq->len;
    if (!((int32_t)(r->pBufRead - r->pBuf) < r->iBufSize)) r->pBufRead = r->pBuf;
    return rq->ptr;
}

/* 0x4678 (strm_Polling / strm_Sync / the sync read arm): the pending block becomes valid data; iReadSize recomputed
 * from the heads (equal = FULL) */
int sbuf_FreeReadBlock(StrmBuffInfo *r)
{
    if (!(s_StrmBuffMng.flags & 1)) return 0;
    if (!r) return 0;
    if (!r->pBuf) return 0;
    if (!r->pBufRead) return 0;
    r->pBufEnd = r->pBufRead;
    if (r->pBufTop < r->pBufEnd) r->iReadSize = (int32_t)(r->pBufEnd - r->pBufTop);
    else if (r->pBufEnd < r->pBufTop) r->iReadSize = r->iBufSize - (int32_t)(r->pBufTop - r->pBufEnd);
    else r->iReadSize = r->iBufSize;
    return 1;
}

/* 0x473c (strm_Read after a failed read; REV): drops the pending block */
int sbuf_CancelReadBlock(StrmBuffInfo *r)
{
    if (!(s_StrmBuffMng.flags & 1)) return 0;
    if (!r) return 0;
    if (!r->pBuf) return 0;
    if (!r->pBufRead) return 0;
    r->pBufRead = r->pBufEnd;
    return 1;
}

/* 0x4994 (strm_RequiredReading): 1 when the valid bytes are at or below reload (signed) and a free run of 0x800
 * exists. Ignores a pending block (Q-G3h: AllocReadBlock refuses it, so a candidate's read can still return 0). */
int sbuf_RequiredRead(const StrmBuffInfo *r, int reload)
{
    if (!(s_StrmBuffMng.flags & 1)) return 0;
    if (!r) return 0;
    if (!r->pBuf) return 0;
    if (reload < r->iReadSize) return 0;
    return free_run(r) >= 0x800;
}

/* ---- G4 live: data blocks */

/* 0x47b0 (mstrm_GetData): the block at the read head, rq->len clamped to the valid bytes (NOT to the ring end:
 * GH4-3); empty ring -> rq->ptr = 0 and 0 (Q-G4d). Guards: not initialised / rq 0 / len <= 0 / r 0 / no buffer. */
uint32_t sbuf_GetDataBlock(const StrmBuffInfo *r, BlockReq *rq)
{
    if (!(s_StrmBuffMng.flags & 1)) return 0;
    if (!rq) return 0;
    int32_t n = rq->len;
    if (n <= 0) return 0;
    if (!r) return 0;
    if (!r->pBuf) return 0;
    if (r->iReadSize < n) rq->len = r->iReadSize;
    if (rq->len == 0) {
        rq->ptr = 0;
    } else {
        rq->ptr = r->pBufTop;
    }
    return rq->ptr;
}

/* 0x486c (mstrm_GetData / ReleaseData / _strm_SeekCdvd): consumes min(n, valid) bytes; the read head wraps at the
 * ring end (unsigned compare). -1 when not initialised / r 0 / n < 0 / no buffer. Stores: top, wrapped top,
 * iReadSize. */
int sbuf_RemoveDataBlock(StrmBuffInfo *r, int n)
{
    if (!(s_StrmBuffMng.flags & 1)) return -1;
    if (!r) return -1;
    if (n < 0) return -1;
    if (!r->pBuf) return -1;
    if (r->iReadSize < n) n = r->iReadSize;
    r->pBufTop += (uint32_t)n;
    if (!(r->pBufTop < r->pBuf + (uint32_t)r->iBufSize)) r->pBufTop -= (uint32_t)r->iBufSize;
    r->iReadSize -= n;
    return n;
}

/* ---- G5: teardown and caller-supplied buffers */

/* 0x4218 (mstrm_Quit): frees every record's buffer WITHOUT the owned test (Q-G5b: a caller-supplied buffer of
 * AllocStreamSetBuffer is handed to FreeSysMemory too) and clears pBuf; the other fields and both lists stay; then the
 * inited bit is cleared (byte store) */
void sbuf_Quit()
{
    if (!(s_StrmBuffMng.flags & 1)) return;
    for (int i = 0; i < 8; i++) {
        StrmBuffInfo *r = &s_StrmBuffInfo[i];
        if (r->pBuf) {
            iopk::FreeSysMemory(r->pBuf);
            r->pBuf = 0;
        }
    }
    s_StrmBuffMng.flags &= ~1u;
}

/* 0x4394 (mstrm_AllocStreamSetBuffer): a record over the caller's buffer (IOP address); the size is masked to 0x800
 * again (Q-G5d: the caller already did) and may be 0; the owned bit is cleared. Returns &rec[r->index]. */
StrmBuffInfo *sbuf_SetStreamBuffer(uint32_t buf, int size)
{
    if (!(s_StrmBuffMng.flags & 1)) return 0;
    if (!buf) return 0;
    StrmBuffInfo *r = _sbuf_GetStrmBufInfo();
    if (!r) return 0;
    r->pBuf = buf;
    r->pBufEnd = r->pBuf;
    r->pBufTop = r->pBuf;
    r->iBufSize = (int32_t)((uint32_t)size & ~0x7ffu);
    r->iReadSize = 0;
    r->pBufRead = r->pBuf;
    r->flags = (uint8_t)(r->flags & ~1u);
    return &s_StrmBuffInfo[r->index];
}

/* 0x4450 (mstrm_FreeStream): only an owned buffer is freed; the heads / sizes are cleared in asm order, the owned bit
 * cleared, the record goes back on the free list */
void sbuf_FreeStreamBuffer(StrmBuffInfo *r)
{
    if (!(s_StrmBuffMng.flags & 1)) return;
    if (!r) return;
    if (r->pBuf && (r->flags & 1)) iopk::FreeSysMemory(r->pBuf);
    r->pBufEnd = 0;
    r->pBufTop = 0;
    r->pBuf = 0;
    r->iBufSize = 0;
    r->iReadSize = 0;
    r->pBufRead = 0;
    r->flags = (uint8_t)(r->flags & ~1u);
    _sbuf_ReleaseStrmBufInfo(r);
}

}  // namespace mfstrm
