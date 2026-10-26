/* mfstrm mstream.c + stream.c, G1 part: mstrm_Init (tables, strm_Init, sbuf_Init, the lock + update semaphores, the
 * update thread), the stream-record pool, AllocStream and the three setters of the 17-call init; strm_Init (host-read
 * thread + its semaphores), strm_InitInfo, strm_AttachStreamBuffer. G2: stream control (AttachSector, IsAttached,
 * DetachFileSector, Seek, the size / priority / read-start / loop / error getters and setters, _mstrm_SortInfo) and
 * strm_OpenSector / Close / Set- / GetPriority / SetReadStart. G5 (map section 17, all REV): mstrm_Quit / strm_Quit,
 * FreeStream / AllocStreamSetBuffer / strm_DetachStreamBuffer, AttachFile / AttachFileOffset and the name opens
 * (strm_OpenFile*, _strm_OpenCdvd*, _strm_OpenHost*). The update pass (mstrm_Update), the read engine, data-out and
 * the host read / seek are in mfstrm_read.cpp (G3 / G4 / G5).
 *
 * Preemption (H2): the host-read thread (0x18) and the update thread (0x19) are above the RPC thread (0x28): each
 * StartThread here runs the new thread to its first WaitSema before the handler continues (iopk does that). */
#include "mfstrm_state.h"
#include <string.h>

namespace mfstrm {

/* ---------------------------------------------------------------- stream.c */

/* 0x4b34: THPRI semaphore */
static int _CreateSema(int init, int max)
{
    iopk::SemaParam p;
    p.attr = 1;
    p.option = 0;
    p.init_count = init;
    p.max_count = max;
    return iopk::CreateSema(&p);
}

/* 0x4b68 */
static int _CreateThread(int prio, uint32_t stack, iopk::ThreadEntry entry)
{
    iopk::ThreadParam p;
    p.attr = 0x02000000;
    p.option = 0;
    p.entry = entry;
    p.stack_size = stack;
    p.priority = prio;
    return iopk::CreateThread(&p);
}

/* 0x4ba0: host / HDD read thread (never past its first WaitSema in Versus: s_iSemaHost is only signalled by the host
 * read variant, G5). buf is an IOP address. */
static void _strm_Thread_Host(void *)
{
    for (;;) {
        iopk::WaitSema(s_iSemaHost);
        iopk::WaitSema(s_iSyncSemaHost);
        int n = iopk::Read(s_HostRead.fd, iopk::Ptr(s_HostRead.buf), s_HostRead.size);
        s_HostRead.result = n;
        if (n < 0) iopk::Printf("read() is failed...ret=%d\n", n);
        iopk::SignalSema(s_iSyncSemaHost);
        iopk::SignalSema(g_iUpdateSema);
    }
}

/* 0x5980: creates what is missing (ids < 0 / thread id <= 0); the second error line prints the stored (failed) id */
int strm_Init()
{
    if (s_iSemaHost < 0) {
        s_iSemaHost = _CreateSema(0, 1);
        if (s_iSemaHost < 0) {
            iopk::Printf("%s : ERROR!!! ", "strm_Init");
            iopk::Printf("Cannot create semaphore... ret=%d\n", s_iSemaHost);
            return 0;
        }
    }
    if (s_iSyncSemaHost < 0) {
        s_iSyncSemaHost = _CreateSema(1, 1);
        if (s_iSyncSemaHost < 0) {
            iopk::Printf("%s : ERROR!!! ", "strm_Init");
            iopk::Printf("Cannot create semaphore... ret=%d\n", s_iSyncSemaHost);
            return 0;
        }
    }
    if (s_iHostThreadId <= 0) {
        s_iHostThreadId = _CreateThread(0x18, 0x1000, _strm_Thread_Host);
        if (s_iHostThreadId <= 0) {
            iopk::Printf("%s : ERROR!!! ", "strm_Init");
            iopk::Printf("Cannot create Host/HDD read thread... ret=%d\n", s_iHostThreadId);
            return 0;
        }
        iopk::StartThread(s_iHostThreadId, 0);
    }
    return 1;
}

/* 0x5b64: word 0x20 and byte 0x37 are not written; next / prev / sema are mstrm_Init's */
void strm_InitInfo(StrmInfo *p)
{
    if (!p) return;
    p->handle = -1;
    p->size = 0;
    p->w08 = 0;
    p->pos = 0;
    p->loop_start = 0;
    p->loop_end = 0;
    p->loop_count = 0;
    p->prio = 0x4000;
    p->state = 0;
    p->flags = 0;
    p->hSBuf = 0;
    p->reload = 0;
    p->w10 = 0;
    p->cdmode[0] = 0;
    p->cdmode[1] = 0;
    p->cdmode[2] = 0;
}

/* 0x6080: refused (0, nothing written) in states 1, 2, 3 and 8 (Q4). reload = 75 % of the buffer: size * 75 in 32 bits,
 * then the signed multiply-high division by 100 (truncating toward zero; 0x20000 -> 0x18000). Sets the read-start
 * bit; the state becomes 1 only if the stream is already attached (bit3). */
int strm_AttachStreamBuffer(StrmInfo *p, StrmBuffInfo *h)
{
    if (!p) return 0;
    if ((uint32_t)(p->state - 1) < 3 || p->state == 8) return 0;
    p->hSBuf = h;
    int32_t x = (int32_t)((uint32_t)sbuf_GetBufferSize(p->hSBuf) * 75u);
    int32_t hi = (int32_t)(((int64_t)0x51eb851f * x) >> 32);
    p->reload = (hi >> 5) + (int32_t)((uint32_t)x >> 31);
    p->flags = (p->flags & ~0x10u) | 0x10u;
    if (p->flags & 0x08) p->state = 1;
    return 1;
}

/* 0x6984 */
int strm_SetReloadSize(StrmInfo *p, int n)
{
    if (!p) return 0;
    p->reload = n;
    return 1;
}

/* 0x6dd0: the second line of strm_OpenSector's busy error (Shift-JIS from the image, no newline) */
static const char s_OpenSectorBusy[] =
    "\x8f\x88\x97\x9d\x92\x86\x82\xc8\x82\xcc\x82\xc5\x83\x5a\x83\x4e\x83\x5e\x82\xf0\x8a\x84\x82\xe8\x93\x96\x82\xc4\x82\xe7"
    "\x82\xea\x82\xdc\x82\xb9\x82\xf1\xa5\xa5\xa5";

/* 0x5e54 (mstrm_AttachSector): refused in states 2, 3, 8 (two Kprintf lines, nothing written). size is stored as is;
 * the loop fields, read-start bit 4, error bits 6..22, reload and the cd mode stay (Q-G2a). The flags byte gets three
 * read-modify-writes: type 2, attached, EOF clear. State 0, or 1 when a ring is attached (its data cleared). */
int strm_OpenSector(StrmInfo *p, int lsn, int size)
{
    if (!p) return 0;
    int32_t s = p->state;
    if ((uint32_t)(s - 2) < 2 || s == 8) {
        iopk::Printf("%s : ERROR!!! ", "strm_OpenSector");
        iopk::Printf(s_OpenSectorBusy);
        return 0;
    }
    p->handle = lsn;
    p->size = size;
    p->w08 = 0;
    p->pos = 0;
    p->w10 = 0;
    p->flags = (p->flags & ~3u) | 2u;
    p->flags = (p->flags & ~8u) | 8u;
    p->flags = p->flags & ~4u;
    p->state = 0;
    if (p->hSBuf) {
        sbuf_ClearData(p->hSBuf);
        p->state = 1;
    }
    return 1;
}

/* 0x5f5c (mstrm_DetachFileSector, mstrm_Quit, the host opens): a read in flight (state 2 / 3 / 8) is waited for
 * (strm_Sync, result unused); a host / HDD stream (type != 2) that is not already closed (state 7) closes its fd. Then
 * the position fields are cleared in asm order, the flags byte loses type (-> 0), EOF, loop and attached (four byte
 * read-modify-writes) and the state becomes 7 = closed. hSBuf, bit4, the error bits, rsize, prio, reload, the cd mode
 * and the ring contents stay (Q-G2b). */
int strm_Close(StrmInfo *p)
{
    if (!p) return 0;
    int32_t s = p->state;
    if ((uint32_t)(s - 2) < 2 || s == 8) strm_Sync(p);
    if ((p->flags & 3) != 2 && p->state != 7) iopk::Close(p->handle);
    p->handle = -1;
    p->size = 0;
    p->w08 = 0;
    p->pos = 0;
    p->loop_start = 0;
    p->loop_end = 0;
    p->loop_count = 0;
    p->w10 = 0;
    p->flags = p->flags & ~3u;
    p->flags = p->flags & ~4u;
    p->flags = p->flags & ~0x20u;
    p->flags = p->flags & ~8u;
    p->state = 7;
    return 1;
}

/* 0x6944: the low 16 bits of the caller's register are stored (sh) */
int strm_SetPriority(StrmInfo *p, uint16_t prio)
{
    if (!p) return 0;
    p->prio = prio;
    return 1;
}

/* 0x6964: zero-extended (lhu) */
int strm_GetPriority(StrmInfo *p)
{
    if (!p) return -1;
    return p->prio;
}

/* 0x69a4: flags bit4 = (on != 0) */
int strm_SetReadStart(StrmInfo *p, int on)
{
    if (!p) return 0;
    p->flags = (p->flags & ~0x10u) | ((on != 0 ? 1u : 0u) << 4);
    return 1;
}

/* ---- stream.c, G5 */

/* 0x5ac4 (mstrm_Quit): what strm_Init created (thread id > 0, sema ids >= 0); ids back to -1 */
void strm_Quit()
{
    if (s_iHostThreadId > 0) {
        iopk::TerminateThread(s_iHostThreadId);
        iopk::DeleteThread(s_iHostThreadId);
        s_iHostThreadId = -1;
    }
    if (s_iSemaHost >= 0) {
        iopk::DeleteSema(s_iSemaHost);
        s_iSemaHost = -1;
    }
    if (s_iSyncSemaHost >= 0) {
        iopk::DeleteSema(s_iSyncSemaHost);
        s_iSyncSemaHost = -1;
    }
}

/* 0x615c (mstrm_FreeStream): skipped in states 1, 2 and 8 (state 3 is NOT guarded, unlike strm_AttachStreamBuffer's
 * 1 / 2 / 3 / 8); else hSBuf 0, state 0. Q-G5a: an attached stream with a ring (state 1) keeps a dangling hSBuf. */
void strm_DetachStreamBuffer(StrmInfo *p)
{
    if (!p) return;
    int32_t s = p->state;
    if ((uint32_t)(s - 1) < 2 || s == 8) return;
    p->hSBuf = 0;
    p->state = 0;
}

/* 0x4b2c: `j sceCdGetError` (the result is dropped by both callers) */
static int _PrintCdError() { return iopk::CdGetError(); }

/* 0x6c98: the busy error's second line of the name opens (Shift-JIS from the image, "cannot attach: read in
 * progress", with newline) */
static const char s_OpenCdvdBusy[] =
    "\x93\xc7\x82\xdd\x8d\x9e\x82\xdd\x8f\x88\x97\x9d\x92\x86\x82\xc8\x82\xcc\x82\xc5\x83\x41\x83\x5e\x83\x62\x83\x60\x82\xc5"
    "\x82\xab\x82\xdc\x82\xb9\x82\xf1\x0a";

/* 0x5088 (strm_OpenFile, a name after the cdrom prefix): strm_OpenSector with the LSN / size of sceCdSearchFile.
 * Refused in states 2 / 3 / 8 (nothing written); the attached bit is cleared BEFORE the search (a failed search leaves
 * it cleared); then the OpenSector tail (handle = lsn, size, w08 / pos / w10 0, type 2, attached, EOF clear, state 0 /
 * 1 with the ring cleared). */
int _strm_OpenCdvd(StrmInfo *p, const char *name)
{
    int32_t s = p->state;
    if ((uint32_t)(s - 2) < 2 || s == 8) {
        iopk::Printf("%s : ERROR!!! ", "_strm_OpenCdvd");
        iopk::Printf(s_OpenCdvdBusy);
        return 0;
    }
    iopk::CdlFILE fp;
    p->flags = p->flags & ~8u;
    if (!iopk::CdSearchFile(&fp, name)) {
        iopk::Printf("%s : ERROR!!! ", "_strm_OpenCdvd");
        iopk::Printf("File not found... fname = %s\n", name);
        _PrintCdError();
        return 0;
    }
    p->handle = (int32_t)fp.lsn;
    p->size = (int32_t)fp.size;
    p->w08 = 0;
    p->pos = 0;
    p->w10 = 0;
    p->flags = (p->flags & ~3u) | 2u;
    p->flags = (p->flags & ~8u) | 8u;
    p->flags = p->flags & ~4u;
    p->state = 0;
    if (p->hSBuf) {
        sbuf_ClearData(p->hSBuf);
        p->state = 1;
    }
    return 1;
}

/* 0x51f4 (strm_OpenFileOffset): as _strm_OpenCdvd; len clamped to size - off (signed; off > size gives a negative
 * size, no check); handle = lsn + (off >> 11) (arithmetic: off & 0x7ff is dropped for the LSN, Q-G5f), w10 = the full
 * off (only _strm_SeekCdvd reads it) */
int _strm_OpenCdvdOffset(StrmInfo *p, const char *name, int off, int len)
{
    int32_t s = p->state;
    if ((uint32_t)(s - 2) < 2 || s == 8) {
        iopk::Printf("%s : ERROR!!! ", "_strm_OpenCdvdOffset");
        iopk::Printf(s_OpenCdvdBusy);
        return 0;
    }
    iopk::CdlFILE fp;
    p->flags = p->flags & ~8u;
    if (!iopk::CdSearchFile(&fp, name)) {
        iopk::Printf("%s : ERROR!!! ", "_strm_OpenCdvdOffset");
        iopk::Printf("File not found... fname = %s\n", name);
        _PrintCdError();
        return 0;
    }
    if ((int32_t)fp.size < (int32_t)((uint32_t)off + (uint32_t)len)) len = (int32_t)(fp.size - (uint32_t)off);
    p->handle = (int32_t)(fp.lsn + (uint32_t)(off >> 11));
    p->size = len;
    p->w08 = 0;
    p->pos = 0;
    p->w10 = off;
    p->flags = (p->flags & ~3u) | 2u;
    p->flags = (p->flags & ~8u) | 8u;
    p->flags = p->flags & ~4u;
    p->state = 0;
    if (p->hSBuf) {
        sbuf_ClearData(p->hSBuf);
        p->state = 1;
    }
    return 1;
}

/* the type bits of a host open: "pfs0" / "hdd0" (4 bytes, the caller's name) -> type 1; any other name keeps the
 * bits as they were (never set to 2 here) */
static void host_type(StrmInfo *p, const char *name)
{
    if (strncmp(name, "pfs0", 4) == 0) p->flags = (p->flags & ~3u) | 1u;
    if (strncmp(name, "hdd0", 4) == 0) p->flags = (p->flags & ~3u) | 1u;
}

/* 0x4d34 (strm_OpenFile, a name without a cdrom prefix): no state refusal (Q-G5g); an attached stream is closed first;
 * the name is copied with strncpy(buf, name, 0xff) into a 0x100 stack buffer whose last byte the original never writes
 * (zeroed here: identical for names < 255 bytes); attached bit cleared before open(buf, O_RDONLY, 0x124) (iopk drops
 * the mode; only "cdrom*:" names resolve, pfs0: / hdd0: give -19). handle stored before the SEEK_END lseek (delay
 * slot), size = its result, state 0 before the SEEK_SET lseek. */
int _strm_OpenHost(StrmInfo *p, const char *name)
{
    char buf[0x100];
    if (p->flags & 8) strm_Close(p);
    memset(buf, 0, sizeof buf);
    strncpy(buf, name, 0xff);
    p->flags = p->flags & ~8u;
    int fd = iopk::Open(buf, 1);
    if (fd < 0) return 0;
    p->handle = fd;
    p->size = iopk::Lseek(fd, 0, 2);
    p->w08 = 0;
    p->pos = 0;
    p->w10 = 0;
    p->flags = (p->flags & ~8u) | 8u;
    p->flags = p->flags & ~4u;
    p->state = 0;
    iopk::Lseek(fd, 0, 0);
    host_type(p, name);
    if (p->hSBuf) {
        sbuf_ClearData(p->hSBuf);
        p->state = 1;
    }
    return 1;
}

/* 0x4ec8 (strm_OpenFileOffset): as _strm_OpenHost; size = lseek END, len clamped to size - off (signed), handle /
 * size / w08 / pos stored after the lseek, w10 = off, then lseek(fd, off, SEEK_SET); the pfs0 / hdd0 tests read the
 * caller's name, not the copy */
int _strm_OpenHostOffset(StrmInfo *p, const char *name, int off, int len)
{
    char buf[0x100];
    if (p->flags & 8) strm_Close(p);
    memset(buf, 0, sizeof buf);
    strncpy(buf, name, 0xff);
    p->flags = p->flags & ~8u;
    int fd = iopk::Open(buf, 1);
    if (fd < 0) return 0;
    int size = iopk::Lseek(fd, 0, 2);
    if (size < (int32_t)((uint32_t)off + (uint32_t)len)) len = (int32_t)((uint32_t)size - (uint32_t)off);
    p->handle = fd;
    p->size = len;
    p->w08 = 0;
    p->pos = 0;
    p->flags = (p->flags & ~8u) | 8u;
    p->flags = p->flags & ~4u;
    p->state = 0;
    p->w10 = off;
    iopk::Lseek(fd, off, 0);
    host_type(p, name);
    if (p->hSBuf) {
        sbuf_ClearData(p->hSBuf);
        p->state = 1;
    }
    return 1;
}

/* .data 0x6e38 / 0x6e44: two identical copies of the prefix table (each copied to the caller's stack) */
static const char *const s_CdPrefix[3] = {"cdrom:", "cdrom0:", "cdrom1:"};

/* strips the cdrom prefixes: three sequential tests, not an else-if chain (Q-G5e: "cdrom:cdrom0:X" strips both;
 * "cdrom0:X" fails the first test and matches the second); case sensitive. Returns whether one matched. */
static int strip_cd_prefix(const char **name)
{
    int cd = 0;
    for (int k = 0; k < 3; k++) {
        if (strncmp(*name, s_CdPrefix[k], strlen(s_CdPrefix[k])) == 0) {
            *name += strlen(s_CdPrefix[k]);
            cd = 1;
        }
    }
    return cd;
}

/* 0x5bbc (mstrm_AttachFile): p == 0 -> 0 before any strlen; a cdrom prefix -> the disc search, else the host open */
int strm_OpenFile(StrmInfo *p, const char *name)
{
    if (!p) return 0;
    int cd = strip_cd_prefix(&name);
    return cd ? _strm_OpenCdvd(p, name) : _strm_OpenHost(p, name);
}

/* 0x5cf4 (mstrm_AttachFileOffset) */
int strm_OpenFileOffset(StrmInfo *p, const char *name, int off, int len)
{
    if (!p) return 0;
    int cd = strip_cd_prefix(&name);
    return cd ? _strm_OpenCdvdOffset(p, name, off, len) : _strm_OpenHostOffset(p, name, off, len);
}

/* ---------------------------------------------------------------- mstream.c */

/* 0x1f6c: per-stream FIFO semaphore {0, 1, 1} (option word uninitialised stack in the original: 0) */
static int _mstrm_CreateSema()
{
    iopk::SemaParam p;
    p.attr = 0;
    p.option = 0;
    p.init_count = 1;
    p.max_count = 1;
    return iopk::CreateSema(&p);
}

/* 0x1fd4: pop the free-list head, push it on the alloc list, create its semaphore; the index as a sign-extended short,
 * -1 when the free list is empty (free.num == 0; the record's own prev is not written) */
int _mstrm_GetInfo()
{
    StrmMng &m = s_StrmMng;
    if (m.free.num == 0) return -1;
    int16_t i = m.free.top;
    StrmInfo *p = &s_StrmInfo[i];
    m.free.top = p->next;
    m.free.num--;
    if (p->next != -1) s_StrmInfo[p->next].prev = -1;
    int16_t a = m.alloc.top;
    if (a != -1) s_StrmInfo[a].prev = i;
    p->next = a;
    m.alloc.top = i;
    m.alloc.num++;
    p->sema = _mstrm_CreateSema();
    return i;
}

/* 0x20e0 (AllocStream / AllocStreamSetBuffer failure paths, FreeStream): unlink from the alloc list, push on the free list, delete the
 * semaphore. The record body (hSBuf, state, flags, prio ...) is not reset (Q2). */
void _mstrm_ReleaseInfo(int idx)
{
    int16_t i = (int16_t)idx;
    if (i < 0 || i >= 8) return;
    StrmMng &m = s_StrmMng;
    StrmInfo *p = &s_StrmInfo[i];
    if (p->prev != -1) s_StrmInfo[p->prev].next = p->next;
    if (p->next != -1) s_StrmInfo[p->next].prev = p->prev;
    if (m.alloc.top == i) m.alloc.top = p->next;
    m.alloc.num--;
    int16_t f = m.free.top;
    if (f != -1) s_StrmInfo[f].prev = (int16_t)idx;
    p->next = f;
    p->prev = -1;
    m.free.top = (int16_t)idx;
    m.free.num++;
    iopk::DeleteSema(p->sema);
    p->sema = -1;
}

/* 0x24c4: the update thread (prio 0x19): one mstrm_Update pass per g_iUpdateSema signal; error field 0x31 of the
 * status word (bits 1..17, Q6) -> sceCdDiskReady(0) */
static void _mstrm_Thread(void *)
{
    for (;;) {
        iopk::WaitSema(g_iUpdateSema);
        mstrm_Update();
        if (((s_StrmMng.status >> 1) & 0x1ffff) == 0x31) iopk::CdDiskReady(0);
    }
}

/* 0x2510 (RPC 0x100). Second call -> 0 without new kernel objects; failure paths do not unwind strm_Init / sbuf_Init
 * (Q5, H5). Both sema parameter blocks share one stack slot (option word uninitialised: 0 here); the second sema is
 * the first block with init rewritten to 0. */
int mstrm_Init()
{
    StrmMng &m = s_StrmMng;
    if (m.status & 1) return 0;
    for (int i = 0; i < 8; i++) {
        StrmInfo *p = &s_StrmInfo[i];
        strm_InitInfo(p);
        p->next = (int16_t)(i + 1);
        p->prev = (int16_t)(i - 1);
        p->sema = -1;
    }
    s_StrmInfo[7].next = -1;
    m.status = 0;
    m.w00 = 0;
    m.alloc.top = -1;
    m.last_idx = -1;
    m.alloc.num = 0;
    m.free.top = 0;
    m.packet_size = 0x10000;
    m.free.num = 8;
    m.last_prio = 0xffff;
    if (!strm_Init()) return 0;
    sbuf_Init();
    iopk::SemaParam sp;
    sp.attr = 0;
    sp.option = 0;
    sp.init_count = 1;
    sp.max_count = 1;
    s_SemaID = iopk::CreateSema(&sp);
    if (s_SemaID < 0) return 0;
    sp.init_count = 0;
    g_iUpdateSema = iopk::CreateSema(&sp);
    if (g_iUpdateSema < 0) {
        iopk::DeleteSema(s_SemaID);
        return 0;
    }
    iopk::ThreadParam tp;
    tp.attr = 0x02000000;
    tp.option = 0;
    tp.entry = _mstrm_Thread;
    tp.stack_size = 0x800;
    tp.priority = 0x19;
    int tid = iopk::CreateThread(&tp);
    if (tid <= 0) {
        iopk::DeleteSema(s_SemaID);
        iopk::DeleteSema(g_iUpdateSema);
        return 0;
    }
    if (iopk::StartThread(tid, 0) < 0) {
        iopk::DeleteThread(tid);
        iopk::DeleteSema(s_SemaID);
        iopk::DeleteSema(g_iUpdateSema);
        return 0;
    }
    s_MstrmThID = tid;
    m.status = (m.status & ~1u) | 1;
    return 1;
}

/* 0x2b90 (RPC 0x1000): size <= 0 -> -1 before the lock; rounded up to 0x800, no upper bound */
int mstrm_AllocStream(int size)
{
    if (size <= 0) return -1;
    iopk::WaitSema(s_SemaID);
    size = (int)(((uint32_t)size + 0x7ff) & ~0x7ffu);
    int i = (int16_t)_mstrm_GetInfo();
    if (i < 0) {
        iopk::SignalSema(s_SemaID);
        return -1;
    }
    StrmBuffInfo *h = sbuf_AllocStreamBuffer(size);
    if (!h) {
        _mstrm_ReleaseInfo(i);
        iopk::SignalSema(s_SemaID);
        return -1;
    }
    strm_AttachStreamBuffer(&s_StrmInfo[i], h);
    iopk::SignalSema(s_SemaID);
    return (int16_t)i;
}

/* 0x2c7c (export 6, nobody imports it): size <= 0 -> -1 before the lock; the caller's buffer (IOP address) with
 * size & ~0x7ff (0 accepted), not owned (FreeStream does not free it, sbuf_Quit does: Q-G5b). buf 0 / no ring record
 * -> the stream record is released, -1. */
int mstrm_AllocStreamSetBuffer(uint32_t buf, int size)
{
    if (size <= 0) return -1;
    iopk::WaitSema(s_SemaID);
    int i = (int16_t)_mstrm_GetInfo();
    if (i < 0) {
        iopk::SignalSema(s_SemaID);
        return -1;
    }
    StrmInfo *p = &s_StrmInfo[i];
    StrmBuffInfo *r = sbuf_SetStreamBuffer(buf, (int)((uint32_t)size & ~0x7ffu));
    if (!r) {
        _mstrm_ReleaseInfo(i);
        iopk::SignalSema(s_SemaID);
        return -1;
    }
    strm_AttachStreamBuffer(p, r);
    iopk::SignalSema(s_SemaID);
    return (int16_t)i;
}

/* 0x2d70 (RPC 0x2000, export 7, mstrm_Quit): bad index -> 0 without the lock. No allocated / attached test and no
 * update wake-up: the ring is freed (owned only), the record detached (strm_DetachStreamBuffer's state guard, Q-G5a)
 * and put back on the free list. */
int mstrm_FreeStream(int idx)
{
    if (idx < 0 || idx >= 8) return 0;
    iopk::WaitSema(s_SemaID);
    StrmInfo *p = &s_StrmInfo[idx];
    sbuf_FreeStreamBuffer(p->hSBuf);
    strm_DetachStreamBuffer(p);
    _mstrm_ReleaseInfo((int16_t)idx);
    iopk::SignalSema(s_SemaID);
    return 1;
}

/* 0x2740 (RPC 0x200): walks the alloc list from its head: strm_Close (unlocked), next read BEFORE mstrm_FreeStream
 * relinks the record (GH6-1), FreeStream. Then takes the lock for good, terminates + deletes the update thread
 * (parked in WaitSema / blocked on the lock: GH6-2), deletes the lock and the update semaphore, sbuf_Quit, strm_Quit,
 * clears the inited bit (byte store: the error field stays). Lists, records and the dma state stay (Q-G5i); a later
 * 0x100 re-runs mstrm_Init fully. */
void mstrm_Quit()
{
    if (!(s_StrmMng.status & 1)) return;
    for (int16_t i = s_StrmMng.alloc.top; i != -1;) {
        StrmInfo *p = &s_StrmInfo[i];
        strm_Close(p);
        int16_t n = p->next;
        mstrm_FreeStream(i);
        i = n;
    }
    iopk::WaitSema(s_SemaID);
    iopk::TerminateThread(s_MstrmThID);
    iopk::DeleteThread(s_MstrmThID);
    s_MstrmThID = -1;
    iopk::DeleteSema(s_SemaID);
    s_SemaID = -1;
    iopk::DeleteSema(g_iUpdateSema);
    g_iUpdateSema = -1;
    sbuf_Quit();
    strm_Quit();
    s_StrmMng.status &= ~1u;
}

/* 0x3ad8 (RPC 0xa000): bad index -> -1 (Q1); the stream need not be allocated */
int mstrm_SetReloadSize(int idx, int n)
{
    if (idx < 0 || idx >= 8) return -1;
    iopk::WaitSema(s_SemaID);
    int r = strm_SetReloadSize(&s_StrmInfo[idx], n);
    iopk::SignalSema(s_SemaID);
    return r;
}

/* 0x3c08 (RPC 0x11000: no reply store) */
void mstrm_SetReadPacketSize(int n) { s_StrmMng.packet_size = n; }

/* 0x3dbc (RPC 0xf000, mode = receive buffer + 4): bad index -> 0, not -1 (Q1) */
int mstrm_SetCdRMode(int idx, const uint8_t *mode)
{
    if (idx < 0 || idx >= 8) return 0;
    iopk::WaitSema(s_SemaID);
    StrmInfo *p = &s_StrmInfo[idx];
    p->cdmode[0] = mode[0];
    p->cdmode[1] = mode[1];
    p->cdmode[2] = mode[2];
    p->cdmode[3] = mode[3];
    iopk::SignalSema(s_SemaID);
    return 1;
}

/* ---------------------------------------------------------------- mstream.c, G2: stream control
 * Every entry tests idx < 0 || idx >= 8 first; the failure value is per function (map 13.1, Q-G2e / f / g). The stream
 * need not be allocated (nothing checks hSBuf or the alloc list). Held-lock wake-ups (GH2, map 13.5): AttachSector
 * (success only), Seek (always) and SetReadStart (on != 0) signal g_iUpdateSema while s_SemaID is held: the update
 * thread (0x19) preempts the RPC thread, blocks in mstrm_Update's WaitSema(s_SemaID), and the handler's
 * SignalSema(s_SemaID) hands it the lock, so the whole update pass runs before the handler returns. */

/* 0x2234 (the Attach* calls): re-sorts record idx inside the alloc list by (prio u16, handle / lsn signed) ascending.
 * Not an insertion: pass 1 scans the whole tail after idx and exchanges idx with every record that sorts before it
 * (restarting right after idx), pass 2 is the mirror image towards the head. Equal keys are not swapped; only links
 * and alloc.top change. Stores in asm order (GH4, map 13.3); a record outside the alloc list is not guarded. */
void _mstrm_SortInfo(int idx)
{
    StrmInfo *const s = s_StrmInfo;
    StrmInfo *p = &s[idx];
    int16_t x = p->next;   /* idx's next, refreshed after every exchange (the asm's adjacency register) */
    int16_t j = x;
    while (j != -1) {
        StrmInfo *q = &s[j];
        if (q->prio < p->prio || (q->prio == p->prio && q->handle < p->handle)) {
            int16_t a, b;
            if (x == j) {
                a = j;
                b = (int16_t)idx;
            } else {
                a = q->prev;
                b = x;
                s[a].next = (int16_t)idx;
                s[x].prev = j;
            }
            q->prev = p->prev;
            p->next = q->next;
            if (p->next != -1) s[q->next].prev = (int16_t)idx;
            int16_t t = p->prev;
            if (t != -1) s[t].next = j;
            else s_StrmMng.alloc.top = j;
            p->prev = a;
            q->next = b;
            x = p->next;
            j = x;
        } else {
            j = q->next;
        }
    }
    int16_t y = p->prev;
    j = y;
    while (j != -1) {
        StrmInfo *q = &s[j];
        if (p->prio < q->prio || (q->prio == p->prio && p->handle < q->handle)) {
            int16_t a, b;
            if (y == j) {
                a = j;
                b = (int16_t)idx;
            } else {
                a = q->next;
                b = y;
                s[a].prev = (int16_t)idx;
                s[y].next = j;
            }
            q->next = p->next;
            p->prev = q->prev;
            if (p->next != -1) s[p->next].prev = j;
            int16_t t = q->prev;
            if (t != -1) s[t].next = (int16_t)idx;
            else s_StrmMng.alloc.top = (int16_t)idx;
            p->next = a;
            q->prev = b;
            y = p->prev;
            j = y;
        } else {
            j = q->prev;
        }
    }
}

/* 0x2fc4 (RPC 0x4000, export 9): bad index -> 0. The EE's 4th word (2) becomes the stream priority. */
int mstrm_AttachSector(int idx, int lsn, int size, uint16_t prio)
{
    if (idx < 0 || idx >= 8) return 0;
    iopk::WaitSema(s_SemaID);
    StrmInfo *p = &s_StrmInfo[idx];
    int ok = strm_OpenSector(p, lsn, size);
    if (ok) {
        strm_SetPriority(p, prio);
        _mstrm_SortInfo(idx);
        iopk::SignalSema(g_iUpdateSema);   /* held lock (GH2) */
    }
    iopk::SignalSema(s_SemaID);
    return ok;
}

/* 0x2e10 (RPC 0x3000, export 8): as mstrm_AttachSector with strm_OpenFile; the 3rd argument (the dispatcher's "mode",
 * the u16 after the name) is the stream priority. The update thread is signalled with the lock held, success only
 * (GH6-3). */
int mstrm_AttachFile(int idx, const char *name, uint16_t prio)
{
    if (idx < 0 || idx >= 8) return 0;
    iopk::WaitSema(s_SemaID);
    StrmInfo *p = &s_StrmInfo[idx];
    int ok = strm_OpenFile(p, name);
    if (ok) {
        strm_SetPriority(p, prio);
        _mstrm_SortInfo(idx);
        iopk::SignalSema(g_iUpdateSema);   /* held lock (GH2) */
    }
    iopk::SignalSema(s_SemaID);
    return ok;
}

/* 0x2ee4 (RPC 0x13000; len is the 5th argument, the caller's sp+0x10) */
int mstrm_AttachFileOffset(int idx, const char *name, uint16_t prio, int off, int len)
{
    if (idx < 0 || idx >= 8) return 0;
    iopk::WaitSema(s_SemaID);
    StrmInfo *p = &s_StrmInfo[idx];
    int ok = strm_OpenFileOffset(p, name, off, len);
    if (ok) {
        strm_SetPriority(p, prio);
        _mstrm_SortInfo(idx);
        iopk::SignalSema(g_iUpdateSema);   /* held lock (GH2) */
    }
    iopk::SignalSema(s_SemaID);
    return ok;
}

/* 0x30a0 (RPC 0xe000, export 10): flags bit3; bad index -> 0 */
int mstrm_IsAttached(int idx)
{
    if (idx < 0 || idx >= 8) return 0;
    iopk::WaitSema(s_SemaID);
    int r = (int)((s_StrmInfo[idx].flags >> 3) & 1);
    iopk::SignalSema(s_SemaID);
    return r;
}

/* 0x3124 (RPC 0x5000, export 11): strm_Close's result; bad index -> 0 */
int mstrm_DetachFileSector(int idx)
{
    if (idx < 0 || idx >= 8) return 0;
    iopk::WaitSema(s_SemaID);
    int r = strm_Close(&s_StrmInfo[idx]);
    iopk::SignalSema(s_SemaID);
    return r;
}

/* 0x31a4 (RPC 0x12000, export 19): bad index -> 0 without a signal; else the update thread is woken unconditionally,
 * lock held (GH2) */
int mstrm_Seek(int idx, int pos)
{
    if (idx < 0 || idx >= 8) return 0;
    iopk::WaitSema(s_SemaID);
    int r = strm_Seek(&s_StrmInfo[idx], pos);
    iopk::SignalSema(g_iUpdateSema);
    iopk::SignalSema(s_SemaID);
    return r;
}

/* 0x3240 (export 21) / 0x32e4 (RPC 0x8000, export 14): the PollSema idiom (GH1): only KE_SEMA_ZERO falls back to
 * WaitSema; a successful poll holds the lock, any other error skips the wait; the SignalSema always follows.
 * Bad index -> -1. */
int mstrm_GetStreamBufferSize(int idx)
{
    if (idx < 0 || idx >= 8) return -1;
    if (iopk::PollSema(s_SemaID) == iopk::KE_SEMA_ZERO) iopk::WaitSema(s_SemaID);
    int r = sbuf_GetBufferSize(s_StrmInfo[idx].hSBuf);
    iopk::SignalSema(s_SemaID);
    return r;
}

int mstrm_GetValidDataSize(int idx)
{
    if (idx < 0 || idx >= 8) return -1;
    if (iopk::PollSema(s_SemaID) == iopk::KE_SEMA_ZERO) iopk::WaitSema(s_SemaID);
    int r = sbuf_GetValidDataSize(s_StrmInfo[idx].hSBuf);
    iopk::SignalSema(s_SemaID);
    return r;
}

/* 0x3388 (RPC 0x9000, the EE's poll): no lock, no kernel call (GH3: a semaphore here would move the poll timing) */
int mstrm_GetValidDataSizeF(int idx)
{
    if (idx < 0 || idx >= 8) return -1;
    return sbuf_GetValidDataSize(s_StrmInfo[idx].hSBuf);
}

/* 0x33e0 (RPC 0x6000, export 12): info.size; bad index -> -1 */
int mstrm_GetAttachDataSize(int idx)
{
    if (idx < 0 || idx >= 8) return -1;
    iopk::WaitSema(s_SemaID);
    int r = s_StrmInfo[idx].size;
    iopk::SignalSema(s_SemaID);
    return r;
}

/* 0x3460 (RPC 0x7000, export 13): size - pos (32-bit), read before the unlock; bad index -> -1 */
int mstrm_GetRemainSize(int idx)
{
    if (idx < 0 || idx >= 8) return -1;
    iopk::WaitSema(s_SemaID);
    int r = (int)((uint32_t)s_StrmInfo[idx].size - (uint32_t)s_StrmInfo[idx].pos);
    iopk::SignalSema(s_SemaID);
    return r;
}

/* 0x39cc (RPC 0xc000): bad index -> 0; no re-sort */
int mstrm_SetPriority(int idx, uint16_t prio)
{
    if (idx < 0 || idx >= 8) return 0;
    iopk::WaitSema(s_SemaID);
    int r = strm_SetPriority(&s_StrmInfo[idx], prio);
    iopk::SignalSema(s_SemaID);
    return r;
}

/* 0x3a58 (dma_RequestTransBuffer): bad index -> -1 */
int mstrm_GetPriority(int idx)
{
    if (idx < 0 || idx >= 8) return -1;
    iopk::WaitSema(s_SemaID);
    int r = strm_GetPriority(&s_StrmInfo[idx]);
    iopk::SignalSema(s_SemaID);
    return r;
}

/* 0x3b64 (RPC 0xb000: no reply store): bad index -> -1; on != 0 wakes the update thread, lock held (GH2) */
int mstrm_SetReadStart(int idx, int on)
{
    if (idx < 0 || idx >= 8) return -1;
    iopk::WaitSema(s_SemaID);
    int r = strm_SetReadStart(&s_StrmInfo[idx], on);
    if (on) iopk::SignalSema(g_iUpdateSema);
    iopk::SignalSema(s_SemaID);
    return r;
}

/* 0x3cc8 (RPC 0xd000: no reply store, export 20): start / end rounded up to 0x800, count stored raw, flags bit5 =
 * (count != 0); no wake-up. Bad index: returns at once, no lock. The asm has no return value (v0 = SignalSema's
 * result in range, the caller's leftover out of range): this returns SignalSema's result / 0. */
int mstrm_SetLoopPoint(int idx, int start, int end, int loop)
{
    if (idx < 0 || idx >= 8) return 0;
    iopk::WaitSema(s_SemaID);
    StrmInfo *p = &s_StrmInfo[idx];
    p->loop_start = (int32_t)(((uint32_t)start + 0x7ff) & ~0x7ffu);
    p->loop_end = (int32_t)(((uint32_t)end + 0x7ff) & ~0x7ffu);
    p->loop_count = loop;
    p->flags = (p->flags & ~0x20u) | (loop != 0 ? 0x20u : 0u);
    return iopk::SignalSema(s_SemaID);
}

/* 0x3e60 (RPC 0x10000): flags bits 6..22; bad index -> the global status field (bits 1..17), no lock (Q-G2g) */
int mstrm_GetReadError(int idx)
{
    if (idx < 0 || idx >= 8) return (int)((s_StrmMng.status << 14) >> 15);
    iopk::WaitSema(s_SemaID);
    int r = (int)((s_StrmInfo[idx].flags << 9) >> 15);
    iopk::SignalSema(s_SemaID);
    return r;
}

}  // namespace mfstrm
