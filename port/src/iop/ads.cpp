/* ads - see ads.h. Function comments give the original's module offset (quirks Q1-Q11
 * there). Channel = `ch` (the EE only ever uses 1); S = the channel's stream record, O = its request / event flags. */
#include "ads.h"
#include "fssd.h"
#include "../../platform/snd/iop/sd.h"
#include "../../platform/plat.h"
#include <stddef.h>
#include <string.h>

namespace ads {

/* ---------------------------------------------------------------- module state (.bss from 0x2bd0: zero at load, H2) */

/* s_iamData[ch] (0x2be8 + ch * 0x90) */
struct Stream {
    uint8_t header[0x28];   /* +0x00 copy of the ADS file header: +0x08 type word (bit 31: 0x800-byte header), +0x24 data size */
    char name[0x40];        /* +0x28 file name (file variant) */
    uint32_t buf;           /* +0x68 stream buffer (IOP address; AllocSysMemory) */
    int32_t half;           /* +0x6c half size = size >> 1 */
    int32_t consumed;       /* +0x70 bytes streamed into the buffer */
    int32_t fd;             /* +0x74 ioman fd (file variant); 0 at load (Q2) */
    int32_t lsn0;           /* +0x78 first data LSN */
    int32_t lsn;            /* +0x7c next LSN to read */
    int32_t lsnend;         /* +0x80 end LSN (exclusive) */
    int32_t state;          /* +0x84 0 closed, 1 open, 2 primed, 3 playing, 4 stopped, 5 paused, 6 setup read; | 0x10 SetProc work */
    uint16_t volL, volR;    /* +0x88 / +0x8a BVOL L / R */
    uint32_t unused8c;      /* +0x8c */
};
static_assert(sizeof(Stream) == 0x90, "s_iamData record");

/* s_iemData (0x2d08) */
struct Workers {
    int32_t proc_tid, pad04;          /* +0x00 ProcLsn thread (0 = none) */
    int32_t proc_alive, pad0c;        /* +0x08 interrupt handlers wake ProcLsn only if != 0 */
    int32_t setproc_tid, pad14;       /* +0x10 SetProcLsn thread */
    int32_t setproc_alive, pad1c;     /* +0x18 1 while SetProcLsn may still need waking */
    uint16_t mvol, mvol_set;          /* +0x20 master volume (adsSystemStart), +0x22 set to 1 by adsSystemInit (never read) */
    uint32_t pad24;
};

enum : uint32_t { OP_PAUSE = 0x01, OP_VOLUME = 0x02, OP_STOP = 0x04, OP_LOOP = 0x80, OP_EOF = 0x100, OP_DMA = 0x8000 };

static int32_t s_iRpcRet;                    /* 0x2bd0 reply word */
static uint32_t s_uiTransPcmDataToEEAddr;    /* 0x2bd4 */
static uint8_t s_ucTransPcmDataToEE;         /* 0x2bd8 0 idle, 1 armed, 2 sending, 3 DMA queued, 4 done */
static uint8_t s_cdrm[4];                    /* 0x2bdc sceCdRMode {trycount, spindlctrl, datapattern}: all zero in practice */
static uint32_t s_uiOperate[2];              /* 0x2be0 */
static Stream s_iamData[2];                  /* 0x2be8 */
static Workers s_iemData;                    /* 0x2d08 */
static uint32_t t_refills, t_last_fill;      /* test view only */

static uint32_t rd32(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const void *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static void wr32(void *p, uint32_t v) { memcpy(p, &v, 4); }
static uint8_t *ram(uint32_t a) { return iopk::Ptr(a); }
static int32_t hdr_type(const Stream *S) { return (int32_t)rd32(S->header + 0x08); }
static int32_t hdr_size(const Stream *S) { return (int32_t)rd32(S->header + 0x24); }

/* BVOL L / R of a channel (the original ORs the raw channel argument into the entry and keeps the low 16 bits) */
static void bvol(uint32_t ch, uint16_t l, uint16_t r)
{
    sd::SetParam((uint16_t)(ch | 0xf80), l);
    sd::SetParam((uint16_t)(ch | 0x1080), r);
}
static void dma_stop(uint32_t ch) { sd::BlockTrans((int16_t)ch, 2, 0, 0, 0); }

/* a CdRead that the original retries in a bare loop: never fails on the native disc layer for an in-range LSN; the
 * native form of a failing retry (a sim hang on the real IOP too) waits for the next event between tries (guard 1) */
static void cd_read_retry(int32_t lsn, int32_t sectors, uint32_t buf)
{
    while (iopk::CdRead((uint32_t)lsn, (uint32_t)sectors, ram(buf), s_cdrm) != 1) {
        iopk::CdDiskReady(0);
        iopk::WaitNextEvent();
    }
}

/* ---------------------------------------------------------------- PCM to EE (dead in the game: the EE filter is never set) */

/* 0x294c: SifSetDmaIntr end function (arg = the EE address) */
static void _PcmDataSendCallback(void *arg)
{
    if ((uint32_t)(uintptr_t)arg == s_uiTransPcmDataToEEAddr) s_ucTransPcmDataToEE = 4;
}

/* 0x28b4 */
static uint32_t _TransDmaIopToEE(uint32_t src, uint32_t ee, uint32_t n)
{
    int st;
    int r = iopk::CpuSuspendIntr(&st);
    iopk::SifDmaData d;
    d.src = ram(src);
    d.dest = ee;
    d.size = n;
    d.attr = 0;
    iopk::FlushDcache();
    uint32_t id = iopk::SifSetDmaIntr(&d, 1, _PcmDataSendCallback, (void *)(uintptr_t)s_uiTransPcmDataToEEAddr);
    s_ucTransPcmDataToEE = 3;
    if (r == 0) iopk::CpuResumeIntr(st);
    return id;
}

/* 0x2840: from the DMA-end handler; side = the half bit of BlockTransStatus. The original indexes s_iamData with the
 * raw core (0x2868, no mask); sd only ever passes core 0 / 1, so the `& 1` here changes nothing reachable and only keeps
 * the host array access in bounds */
static void _TransDataToEE(int core, int side)
{
    if (s_ucTransPcmDataToEE != 1) return;
    s_ucTransPcmDataToEE = 2;
    const Stream *S = &s_iamData[core & 1];
    _TransDmaIopToEE(S->buf + (uint32_t)(S->half * side), s_uiTransPcmDataToEEAddr, (uint32_t)S->half);
}

/* 0x27c4 */
static int adsSetReceiveEeAddr(uint32_t addr)
{
    if (addr == 0 && s_ucTransPcmDataToEE != 3) {
        s_uiTransPcmDataToEEAddr = 0;
        s_ucTransPcmDataToEE = 0;
        return -2;
    }
    if (s_ucTransPcmDataToEE != 0 && s_ucTransPcmDataToEE != 4) return -1;
    s_uiTransPcmDataToEEAddr = addr;
    s_ucTransPcmDataToEE = 1;
    return 0;
}

/* ---------------------------------------------------------------- interrupt handlers (sd's DMA-end user handler) */

/* 0x514 / 0x5a0: the flag word is fixed per handler, not derived from core (Q5); arg = 0 */
static int adsDmaIntr0(int core, void *)
{
    int side = (int)(sd::BlockTransStatus((int16_t)core) >> 24 & 1);
    if (s_iemData.proc_alive) {
        s_uiOperate[0] |= OP_DMA;
        iopk::iWakeupThread(s_iemData.proc_tid);
    }
    _TransDataToEE(core, side);
    return 0;
}
static int adsDmaIntr1(int core, void *)
{
    int side = (int)(sd::BlockTransStatus((int16_t)core) >> 24 & 1);
    if (s_iemData.proc_alive) {
        s_uiOperate[1] |= OP_DMA;
        iopk::iWakeupThread(s_iemData.proc_tid);
    }
    _TransDataToEE(core, side);
    return 0;
}

/* ---------------------------------------------------------------- worker threads */

/* 0xe78: refill worker (prio 0x52), one wake per ADMA-done interrupt */
static void adsProcLsn(void *)
{
    int last = 0;
    for (;;) {
        iopk::SleepThread();
        if (s_iamData[0].state == 0 && s_iamData[1].state == 0) {
            s_iemData.proc_alive = 0;
            iopk::ExitThread();
        }
        int c;
        if ((s_uiOperate[0] & OP_DMA) && last != 0) c = 0;
        else if ((s_uiOperate[1] & OP_DMA) && last != 1) c = 1;
        else if (s_uiOperate[0] & OP_DMA) c = 0;
        else if (!(s_uiOperate[1] & OP_DMA)) continue;   /* spurious wake */
        else c = 1;
        last = c;
        Stream *S = &s_iamData[c];
        uint32_t *O = &s_uiOperate[c];
        *O &= 0x7fff;
        /* the half sd is not playing (BlockTransStatus bit 24 = the half the re-armed ADMA now plays) */
        int fill = 1 - (int)(sd::BlockTransStatus((int16_t)c) >> 24);
        if (*O & OP_STOP) {
            dma_stop((uint32_t)c);
            bvol((uint32_t)c, 0, 0);
            S->state = 4;
            *O = 0;
            continue;
        }
        if (*O & OP_EOF) {   /* end of data: stops at the NEXT wake (Q9) */
            iopk::SleepThread();
            dma_stop((uint32_t)c);
            bvol((uint32_t)c, 0, 0);
            S->state = 4;
            *O = 0;
            continue;
        }
        if (*O & OP_VOLUME) {   /* Volume while playing is applied here (Q4) */
            bvol((uint32_t)c, S->volL, S->volR);
            *O &= ~(uint32_t)OP_VOLUME;
        }
        int32_t rem = hdr_size(S) - S->consumed;
        uint32_t dst = S->buf + (uint32_t)(S->half * fill);
        if (S->half < rem) {   /* normal refill */
            t_refills++;
            t_last_fill = dst;
            if (iopk::CdRead((uint32_t)S->lsn, (uint32_t)(S->half / 0x800), ram(dst), s_cdrm) != 1) continue;   /* no retry (Q6) */
            iopk::CdSync(0);
            S->lsn += S->half / 0x800;
            S->consumed += S->half;
        } else {               /* last partial half */
            t_refills++;
            t_last_fill = dst;
            if (iopk::CdRead((uint32_t)S->lsn, (uint32_t)((rem + 0x7ff) / 0x800), ram(dst), s_cdrm) != 1) {
                iopk::CdDiskReady(0);
                continue;
            }
            iopk::CdSync(0);
            if (*O & OP_LOOP) {
                S->lsn = S->lsn0;
                S->consumed = 0;
            }
            uint8_t *p = ram(dst + (uint32_t)rem);   /* silence after the tail, as words */
            for (int32_t n = (S->half - rem) >> 2; n != 0; n--, p += 4) wr32(p, 0);
            if (!(*O & OP_LOOP)) {
                *O |= OP_DMA;
                *O |= OP_EOF;
            }
        }
        if (*O & OP_PAUSE) {   /* pause is executed lazily here */
            dma_stop((uint32_t)c);
            bvol((uint32_t)c, 0, 0);
            S->state = 5;
            *O &= ~(uint32_t)OP_PAUSE;
        }
    }
}

/* 0x12e4: setup worker (prio 0x54), one step per wake (adsSync) */
static void adsSetProcLsn(void *)
{
    for (;;) {
        iopk::SleepThread();
        if (!(s_iamData[0].state & 0x10) && !(s_iamData[1].state & 0x10)) {
            s_iemData.setproc_alive = 0;
            iopk::ExitThread();
        }
        for (int ch = 0; ch < 2; ch++) {
            Stream *S = &s_iamData[ch];
            if (!(S->state & 0x10)) continue;
            switch (S->state & 0xf) {
            case 1: {   /* the header sector has arrived: parse it, read the first two halves */
                if (iopk::CdSync(1) == 1) break;   /* still busy: next wake */
                memcpy(S->header, ram(S->buf), 0x28);
                S->lsn0 += 1;
                S->lsn = S->lsn0;
                S->lsnend = S->lsn0 + hdr_size(S) / 0x800;
                if (hdr_size(S) % 0x800 != 0) S->lsnend++;
                int32_t both = S->half << 1;
                if (hdr_size(S) < both) {   /* short file (NR) */
                    int32_t sectors = hdr_size(S) / 0x800;
                    if (hdr_size(S) % 0x800 != 0) sectors++;
                    memset(ram(S->buf), 0, (size_t)both);
                    cd_read_retry(S->lsn, sectors, S->buf);
                } else {
                    cd_read_retry(S->lsn, both / 0x800, S->buf);
                }
                S->state = 0x16;
                break;
            }
            case 6:     /* the first two halves have arrived: primed */
                if (iopk::CdSync(1) == 1) break;
                S->state = 2;
                S->consumed = S->half << 1;
                S->lsn += (S->half << 1) / 0x800;
                break;
            default: break;
            }
        }
    }
}

/* ---------------------------------------------------------------- file variant (ioman; the EE never sends these) */

/* the ioctl status poll after a seek on the async-opened fd: `st = 0; do ioctl(fd, 1, &st); while (st)`. ioman's
 * Ioctl on the native disc layer leaves st alone (synchronous: never busy), so this runs once; a busy device would be
 * a poll loop - the native form waits for the next event per round (guard 1) */
static void ioctl_wait(int32_t fd)
{
    int32_t st = 0;
    for (;;) {
        iopk::Ioctl(fd, 1, &st);
        if (st == 0) return;
        iopk::WaitNextEvent();
    }
}

/* the read-error recovery of both file workers: lseek to 0x800 (+ the poll) if the header type has bit 31, else to
 * 0x28 (no poll) */
static void seek_data(const Stream *S)
{
    if (hdr_type(S) & (int32_t)0x80000000) {
        iopk::Lseek(S->fd, 0x800, 0);
        ioctl_wait(S->fd);
    } else {
        iopk::Lseek(S->fd, 0x28, 0);
    }
}

/* 0x62c: refill worker of the file variant (prio 0x52, shares s_iemData.proc_tid with adsProcLsn). Same selection /
 * stop / end / volume / pause logic as adsProcLsn; reads by ioman at the file position instead of CdRead by LSN, and a
 * loop wraps seamlessly: the rest of the last half is read from the data start (adsProcLsn pads it with zeros); that
 * rewind seeks to 0x800 / 0x28 by the header type without the ioctl poll (0x904..0x93c) */
static void adsProcFile(void *)
{
    int last = 0;
    for (;;) {
        iopk::SleepThread();
        if (s_iamData[0].state == 0 && s_iamData[1].state == 0) {
            s_iemData.proc_alive = 0;
            iopk::ExitThread();
        }
        int c;
        if ((s_uiOperate[0] & OP_DMA) && last != 0) c = 0;
        else if ((s_uiOperate[1] & OP_DMA) && last != 1) c = 1;
        else if (s_uiOperate[0] & OP_DMA) c = 0;
        else if (!(s_uiOperate[1] & OP_DMA)) continue;   /* spurious wake */
        else c = 1;
        last = c;
        Stream *S = &s_iamData[c];
        uint32_t *O = &s_uiOperate[c];
        *O &= 0x7fff;
        int fill = 1 - (int)(sd::BlockTransStatus((int16_t)c) >> 24);
        if (*O & OP_STOP) {
            dma_stop((uint32_t)c);
            bvol((uint32_t)c, 0, 0);
            S->state = 4;
            *O = 0;
            continue;
        }
        if (*O & OP_EOF) {   /* stops at the NEXT wake (Q9) */
            iopk::SleepThread();
            dma_stop((uint32_t)c);
            bvol((uint32_t)c, 0, 0);
            S->state = 4;
            *O = 0;
            continue;
        }
        if (*O & OP_VOLUME) {
            bvol((uint32_t)c, S->volL, S->volR);
            *O &= ~(uint32_t)OP_VOLUME;
        }
        int32_t rem = hdr_size(S) - S->consumed;
        uint32_t dst = S->buf + (uint32_t)(S->half * fill);
        t_refills++;
        t_last_fill = dst;
        if (S->half < rem) {   /* normal refill: a short read just waits for the next interrupt (the half stays stale) */
            int32_t n = iopk::Read(S->fd, ram(dst), S->half);
            if (n < S->half) continue;
            S->consumed += n;
        } else {               /* last partial half */
            if (iopk::Read(S->fd, ram(dst), rem) < rem) continue;
            if (*O & OP_LOOP) {
                /* rewind to the data start and fill the rest of the half, retried until the read is complete (a file
                 * shorter than the rest never completes: a hang on the IOP too; guard-1 form here) */
                int32_t want = S->half - rem;
                for (int tries = 0;; tries++) {
                    iopk::Lseek(S->fd, (hdr_type(S) & (int32_t)0x80000000) ? 0x800 : 0x28, 0);
                    if (iopk::Read(S->fd, ram(dst + (uint32_t)rem), want) == want) break;
                    if (!tries) plat_log("ads: adsProcFile(%d): loop re-read short, the original retries forever (kept)", c);
                    iopk::WaitNextEvent();
                }
                S->consumed = want;
            } else {
                uint8_t *p = ram(dst + (uint32_t)rem);   /* silence after the tail, as words */
                for (int32_t n = (S->half - rem) >> 2; n != 0; n--, p += 4) wr32(p, 0);
                *O |= OP_DMA;
                *O |= OP_EOF;
            }
        }
        if (*O & OP_PAUSE) {
            dma_stop((uint32_t)c);
            bvol((uint32_t)c, 0, 0);
            S->state = 5;
            *O &= ~(uint32_t)OP_PAUSE;
        }
    }
}

/* 0xa50: setup worker of the file variant (prio 0x54), one step per wake (adsSync). Quirk: the data always starts at
 * file offset 0x800 - after the header read it seeks to 0x800 unconditionally (a second time for a bit-31 header), so a
 * 0x28-byte-header file loses its first 0x7d8 data bytes; on a short header read it seeks to 0, then to 0x800, prints
 * the (partial) header and stays in state 0x11 (the next wake re-reads the "header" at 0x800) */
static void adsSetProcFile(void *)
{
    for (;;) {
        iopk::SleepThread();
        if (!(s_iamData[0].state & 0x10) && !(s_iamData[1].state & 0x10)) {
            s_iemData.setproc_alive = 0;
            iopk::ExitThread();
        }
        for (int ch = 0; ch < 2; ch++) {
            Stream *S = &s_iamData[ch];
            if (!(S->state & 0x10)) continue;
            switch (S->state & 0xf) {
            case 1: {
                if (S->fd < 0) {   /* the open in adsSetup failed: retry it, one try per wake */
                    S->fd = iopk::Open(S->name, 0x8001);
                    break;
                }
                int32_t st = 0;
                iopk::Ioctl(S->fd, 1, &st);
                if (st != 0) break;   /* device busy: next wake */
                if (iopk::Read(S->fd, S->header, 0x28) != 0x28) {
                    iopk::Lseek(S->fd, 0, 0);
                    ioctl_wait(S->fd);
                } else {
                    S->state = 0x16;
                    if (hdr_type(S) & (int32_t)0x80000000) {
                        iopk::Lseek(S->fd, 0x800, 0);
                        ioctl_wait(S->fd);
                    }
                }
                iopk::Lseek(S->fd, 0x800, 0);
                ioctl_wait(S->fd);
                const uint8_t *h = S->header;
                iopk::Printf("seek to next of header : %d\n", 0x800);
                iopk::Printf("============================================================================\n");
                iopk::Printf("=                              ADS file info                               =\n");
                iopk::Printf("============================================================================\n");
                iopk::Printf("[%c%c%c%c]\nheader iSize:                            %d\n"
                             "iType(0:PCM big, 1:PCM little, 2:ADPCM): %x\n"
                             "sampling iRate:                          %dHz\n"
                             "channels:                               %d\n"
                             "interleave iSize:                        %d\n"
                             "interleave start block address:         %d\n"
                             "interleave end block address:           %d\n",
                             (int)(int8_t)h[0], (int)(int8_t)h[1], (int)(int8_t)h[2], (int)(int8_t)h[3], (int32_t)rd32(h + 4),
                             (int32_t)rd32(h + 8), (int32_t)rd32(h + 0xc), (int32_t)rd32(h + 0x10), (int32_t)rd32(h + 0x14),
                             (int32_t)rd32(h + 0x18), (int32_t)rd32(h + 0x1c));
                iopk::Printf("[%c%c%c%c]\ndata iSize:                              %d\n", (int)(int8_t)h[0x20],
                             (int)(int8_t)h[0x21], (int)(int8_t)h[0x22], (int)(int8_t)h[0x23], (int32_t)rd32(h + 0x24));
                iopk::Printf("============================================================================\n");
                break;
            }
            case 6: {   /* the first two halves (no busy check here, unlike case 1) */
                int32_t both = S->half << 1;
                if (hdr_size(S) < both) {   /* short file */
                    memset(ram(S->buf), 0, (size_t)both);
                    int32_t n = hdr_size(S);
                    if (iopk::Read(S->fd, ram(S->buf), n) == n) {
                        S->state = 2;
                        S->consumed = hdr_size(S);
                    } else {
                        seek_data(S);   /* stays 0x16: re-read at the next wake */
                    }
                } else if (iopk::Read(S->fd, ram(S->buf), both) == both) {
                    S->state = 2;
                    S->consumed = S->half << 1;
                } else {
                    seek_data(S);
                }
                break;
            }
            default: break;
            }
        }
    }
}

/* create + start the shared refill worker as adsProcFile if there is none (0x1a64 / 0x1c70); 0 = CreateThread failed */
static int start_proc_file()
{
    if (s_iemData.proc_tid != 0) return 1;
    iopk::ThreadParam p;
    p.attr = 0x02000000;
    p.option = 0;
    p.entry = adsProcFile;
    p.priority = 0x52;
    p.stack_size = 0x800;
    s_iemData.proc_tid = iopk::CreateThread(&p);
    if (s_iemData.proc_tid < 0) return 0;
    iopk::StartThread(s_iemData.proc_tid, 0);
    s_iemData.proc_alive = 1;
    return 1;
}

/* 0x19ac: synchronous open + header read (the stream reads then come from adsPreset / adsPlay). name = the RPC buffer.
 * Both retry loops are unbounded in the original (a missing file / short header hangs the RPC thread): kept in the
 * guard-1 form with a log line. The handler test reads the header type as a halfword (lhu, unlike adsSetup's lw). */
static int adsOpen(int ch, const char *name)
{
    Stream *S = &s_iamData[ch];
    uint32_t *O = &s_uiOperate[ch];
    if (S->state != 0) return 0;
    for (int tries = 0;; tries++) {
        S->fd = iopk::Open(name, 0);
        if (S->fd >= 0) break;
        if (!tries) plat_log("ads: adsOpen(%d) %s: open fails, the original retries forever (kept)", ch, name);
        iopk::WaitNextEvent();
    }
    for (int tries = 0; iopk::Read(S->fd, S->header, 0x28) != 0x28; tries++) {
        iopk::Lseek(S->fd, 0, 0);
        if (!tries) plat_log("ads: adsOpen(%d) %s: short header, the original retries forever (kept)", ch, name);
        iopk::WaitNextEvent();
    }
    S->state = 1;
    S->consumed = 0;
    *O = 0;
    if (!start_proc_file()) return 0;
    if (rd16(S->header + 8) != 0x10) sd::SetTransIntrHandler(ch, ch ? adsDmaIntr1 : adsDmaIntr0, 0);
    return 1;
}

/* 0x1bec: async open; SetProcFile reads the header and the first two halves over the next Syncs (the buffer comes from
 * adsSetBuff first). name = the RPC buffer, strcpy'd unbounded into the record's 0x40-byte name field: a longer name
 * runs on into buf / half / ... as in the original (never sent; the copy here stops at the end of s_iamData) */
static int adsSetup(int ch, const char *name)
{
    Stream *S = &s_iamData[ch];
    uint32_t *O = &s_uiOperate[ch];
    if (S->state != 0) return 0;
    uint8_t *d = (uint8_t *)S + offsetof(Stream, name), *end = (uint8_t *)(s_iamData + 2);
    do *d++ = (uint8_t)*name; while (*name++ && d < end);
    S->fd = iopk::Open(S->name, 0x8001);   /* not checked here: SetProcFile retries a failed open */
    S->state = 0x11;
    S->consumed = 0;
    *O = 0;
    if (!start_proc_file()) return 0;
    /* the header copy is still the previous one (zero on first use, H2), compared as a word */
    if (hdr_type(S) != 0x10) sd::SetTransIntrHandler(ch, ch ? adsDmaIntr1 : adsDmaIntr0, 0);
    iopk::ThreadParam p;
    p.attr = 0x02000000;
    p.option = 0;
    p.entry = adsSetProcFile;
    p.priority = 0x54;
    p.stack_size = 0x800;
    s_iemData.setproc_tid = iopk::CreateThread(&p);
    if (s_iemData.setproc_tid < 0) return 0;
    s_iemData.setproc_alive = 1;
    iopk::StartThread(s_iemData.setproc_tid, 0);
    return 1;
}

/* ---------------------------------------------------------------- commands */

/* 0x15a4 (NR: the EE never sends 0x10000, so none of this initialisation ever happens - H2) */
static int adsSystemInit()
{
    sd::SetAddr(0x1d00, 0x1dffff);
    sd::SetAddr(0x1d01, 0x1fffff);
    sd::SetParam(0x800, 0xfc0);
    sd::SetParam(0x801, 0xfcc);
    for (uint32_t c = 0; c < 2; c++) {
        sd::SetParam((uint16_t)(c | 0xb80), 0);
        sd::SetParam((uint16_t)(c | 0xc80), 0);
        sd::SetParam((uint16_t)(c | 0xf80), 0);
        sd::SetParam((uint16_t)(c | 0x1080), 0);
    }
    sd::ClearEffectWorkArea(1, 0, 7);
    s_iemData.mvol = 0x3fff;
    s_iemData.mvol_set = 1;
    for (int c = 1; c >= 0; c--) {
        Stream *S = &s_iamData[c];
        S->buf = 0;
        S->half = 0;
        S->fd = -1;
        S->state = 0;
        S->volR = 0x3fff;
        S->volL = 0x3fff;
        S->unused8c = 0;
    }
    s_cdrm[0] = 0;
    s_cdrm[2] = 0;
    s_iemData.proc_alive = 0;
    s_iemData.proc_tid = 0;
    s_iemData.setproc_alive = 0;
    s_iemData.setproc_tid = 0;
    s_cdrm[1] = 1;
    return 1;
}

/* 0x1704 (NR): channels 1 then 0 */
static void adsSystemStart()
{
    for (int c = 1; c >= 0; c--) {
        Stream *S = &s_iamData[c];
        if (S->state == 3) {
            sd::BlockTrans((int16_t)c, 0x10, ram(S->buf), (uint32_t)(S->half << 1), 0);
            bvol((uint32_t)c, S->volL, S->volR);
        }
        sd::SetParam((uint16_t)(c | 0x980), c ? s_iemData.mvol : 0x3fff);
        sd::SetParam((uint16_t)(c | 0xa80), c ? s_iemData.mvol : 0x3fff);
    }
}

/* 0x1800 (NR): channels 1 then 0 */
static void adsSystemStop()
{
    for (int c = 1; c >= 0; c--) {
        sd::SetParam((uint16_t)(c | 0x980), 0xffff);
        sd::SetParam((uint16_t)(c | 0xa80), 0xffff);
        dma_stop((uint32_t)c);
        bvol((uint32_t)c, 0, 0);
    }
}

/* 0x1890 (NR) */
static void adsSetMasterVolume(uint16_t v)
{
    s_iemData.mvol = v;
    sd::SetParam(0x980, 0x3fff);
    sd::SetParam(0xa80, 0x3fff);
    sd::SetParam(0x981, s_iemData.mvol);
    sd::SetParam(0xa81, s_iemData.mvol);
}

/* 0x18e8 (NR): returns 1, the ladder does not store it */
static int adsSetReverb(uint32_t core, uint32_t on)
{
    sd::SetParam((uint16_t)(core | 0x800), on == 1 ? 0xffc : 0xfcc);
    return 1;
}

/* 0x1930 (NR) */
static int adsCheckEndOfStream(uint32_t ch)
{
    switch (s_iamData[ch & 1].state & 0xf) {
    case 6: case 2: case 1: case 5: case 3: return 1;
    default: return 0;
    }
}

/* 0x1b40 (NR): legal in state 0 / 1; no alignment check (unlike adsSetupWithLsn) */
static int adsSetBuff(uint32_t ch, int32_t size)
{
    Stream *S = &s_iamData[ch & 1];
    if (S->state != 1 && S->state != 0) return 0;
    S->buf = iopk::AllocSysMemory(0, (uint32_t)size, 0);
    if (S->buf == 0 || S->buf + (uint32_t)size >= 0x200000) return 0;
    S->half = size >> 1;
    return 1;
}

/* 0x1db8: the one setup call of the game (SetProcLsn finishes it over the next Syncs) */
static int adsSetupWithLsn(uint32_t ch, int32_t lsn, int32_t size)
{
    Stream *S = &s_iamData[ch & 1];
    uint32_t *O = &s_uiOperate[ch & 1];
    if (S->state != 0) return 0;
    S->buf = iopk::AllocSysMemory(0, (uint32_t)size, 0);
    if (S->buf == 0 || S->buf + (uint32_t)size >= 0x200000 || (S->buf & 0xf)) return 0;   /* the block is not freed (Q3) */
    S->half = size >> 1;
    S->lsn = lsn;
    S->lsn0 = lsn;
    iopk::CdDiskReady(0);
    cd_read_retry(S->lsn0, 1, S->buf);   /* the header sector */
    S->state = 0x11;
    S->consumed = 0;
    *O = 0;
    iopk::ThreadParam p;
    if (s_iemData.proc_tid == 0) {
        p.attr = 0x02000000;
        p.option = 0;
        p.entry = adsProcLsn;
        p.priority = 0x52;
        p.stack_size = 0x800;
        s_iemData.proc_tid = iopk::CreateThread(&p);
        if (s_iemData.proc_tid < 0) return 0;
        iopk::StartThread(s_iemData.proc_tid, 0);
        s_iemData.proc_alive = 1;
    }
    /* the header copy is still the previous one (zero on first use): the handler is always set then */
    if (hdr_type(S) != 0x10) sd::SetTransIntrHandler((int)ch, ch ? adsDmaIntr1 : adsDmaIntr0, 0);
    p.attr = 0x02000000;
    p.option = 0;
    p.entry = adsSetProcLsn;
    p.priority = 0x54;
    p.stack_size = 0x800;
    s_iemData.setproc_tid = iopk::CreateThread(&p);   /* a new one on every Setup */
    if (s_iemData.setproc_tid < 0) return 0;
    s_iemData.setproc_alive = 1;
    iopk::StartThread(s_iemData.setproc_tid, 0);
    return 1;
}

/* 0x2014 (NR; file based, Q2): after the first lseek the body loops back to its own size test (`b 0x2084` at 0x210c and
 * 0x217c) - there is no exit: once state != 0 it re-reads the file forever and never returns. Kept; the native loop
 * waits for the next event per round (the hybrid's guard 1 moves time the same way for a spinning thread). */
static int adsPreset(uint32_t ch)
{
    Stream *S = &s_iamData[ch & 1];
    if (S->state == 0) return 0;
    if (hdr_type(S) & (int32_t)0x80000000) iopk::Lseek(S->fd, 0x800, 0);
    plat_log("ads: adsPreset(%u) state %d: the original never returns from here (endless re-read loop, kept)", ch, S->state);
    for (;;) {
        int32_t both = S->half << 1;
        if (hdr_size(S) < both) {
            memset(ram(S->buf), 0, (size_t)both);
            int32_t n = hdr_size(S);
            if (n != iopk::Read(S->fd, ram(S->buf), n)) iopk::Lseek(S->fd, (hdr_type(S) & (int32_t)0x80000000) ? 0x800 : 0x28, 0);
            S->state = 2;
            S->consumed = hdr_size(S);
        } else {
            if (both != iopk::Read(S->fd, ram(S->buf), both))
                iopk::Lseek(S->fd, (hdr_type(S) & (int32_t)0x80000000) ? 0x800 : 0x28, 0);
            S->state = 2;
            S->consumed = S->half << 1;
        }
        iopk::WaitNextEvent();
    }
}

/* 0x2198: one SetProcLsn step per call while it is alive (replies 0); then deletes the exited worker and replies 1 */
static int adsSync()
{
    if (s_iemData.setproc_alive == 1 && s_iemData.setproc_tid > 0) {
        iopk::ChangeThreadPriority(0, 0x56);   /* slice boundary: the RPC thread is now the worst of the three (H3) */
        iopk::WakeupThread(s_iemData.setproc_tid);   /* 0x54 preempts, runs to its next SleepThread / ExitThread */
        iopk::ChangeThreadPriority(0, 0x19);
        return 0;
    }
    if (s_iemData.setproc_tid > 0) {
        iopk::DeleteThread(s_iemData.setproc_tid);
        s_iemData.setproc_alive = 0;
        s_iemData.setproc_tid = 0;
    }
    return 1;
}

/* 0x2240 */
static int adsPlay(uint32_t ch, uint32_t loop)
{
    Stream *S = &s_iamData[ch & 1];
    uint32_t *O = &s_uiOperate[ch & 1];
    switch (S->state) {
    case 4: case 3:   /* restart: stop, then the file-based preset (Q2, NR) */
        dma_stop(ch);
        *O = 0;
        bvol(ch, 0, 0);
        /* fall through */
    case 1:
        iopk::Lseek(S->fd, 0x28, 0);
        if (adsPreset(ch) != 0) return 0;
        break;
    case 5: case 2: break;   /* paused / primed: start */
    default: return 0;
    }
    sd::BlockTrans((int16_t)ch, 0x10, ram(S->buf), (uint32_t)(S->half << 1), 0);
    bvol(ch, S->volL, S->volR);
    S->state = 3;
    if (loop == 1) *O |= OP_LOOP;
    return 1;
}

/* 0x23bc (NR): executed lazily by ProcLsn at its next wake */
static int adsPause(uint32_t ch)
{
    if (s_iamData[ch & 1].state != 3) return 0;
    s_uiOperate[ch & 1] |= OP_PAUSE;
    return 1;
}

/* 0x2428: .data jump table for states 0..5 {0, 0, 0, stop, 0, stop}; states >= 6 return 1 untouched. Frees nothing (Q3) */
static int adsStop(uint32_t ch)
{
    Stream *S = &s_iamData[ch & 1];
    uint32_t *O = &s_uiOperate[ch & 1];
    switch ((uint32_t)S->state) {
    case 0: case 1: case 2: case 4: return 0;
    case 3:
        *O |= OP_STOP;
        /* fall through */
    case 5:
        dma_stop(ch);
        bvol(ch, 0, 0);
        S->state = 4;
        *O = 0;
        return 1;
    default: return 1;
    }
}

/* 0x2560 */
static int adsClose(uint32_t ch)
{
    Stream *S = &s_iamData[ch & 1];
    uint32_t s = ch & 0xf;
    bvol(s, 0, 0);
    if (S->state == 3) {
        /* Q1: the state is loaded once into a register (0x25cc) and `bne v1, 4, .` spins on the register: unless it
         * is already 4 this never ends (Close sets no stop flag). Kept faithful (user decision s93): a sim hang. */
        iopk::ChangeThreadPriority(0, 0x56);
        int32_t v = S->state;
        if (v != 4) plat_log("ads: adsClose(%u) while playing: the original spins forever here (Q1, kept)", ch);
        while (v != 4) iopk::WaitNextEvent();
        iopk::ChangeThreadPriority(0, 0x19);
    }
    if (S->fd >= 0) {   /* fd 0 in the LSN path (never opened): Close(0) = EBADF, harmless (Q2) */
        iopk::Close(S->fd);
        S->fd = -1;
    }
    sd::SetTransIntrHandler((int)s, 0, 0);
    if (S->buf) {
        iopk::FreeSysMemory(S->buf);
        S->buf = 0;
    }
    S->state = 0;
    if (s_iamData[0].state == 0 && s_iamData[1].state == 0) {
        iopk::ChangeThreadPriority(0, 0x56);
        if (s_iemData.setproc_tid > 0) {
            iopk::CancelWakeupThread(s_iemData.setproc_tid);
            iopk::WakeupThread(s_iemData.setproc_tid);   /* sees both states 0: clears its flag and exits */
            iopk::DeleteThread(s_iemData.setproc_tid);
            s_iemData.setproc_alive = 0;
            s_iemData.setproc_tid = 0;
        }
        if (s_iemData.proc_tid > 0) {
            iopk::CancelWakeupThread(s_iemData.proc_tid);
            iopk::WakeupThread(s_iemData.proc_tid);
            while (iopk::DeleteThread(s_iemData.proc_tid) != 0) {   /* iopk: 0 for any existing thread - runs 0 times */
                S->state = 0;
                iopk::CancelWakeupThread(s_iemData.proc_tid);
                iopk::WakeupThread(s_iemData.proc_tid);
            }
            s_iemData.proc_tid = 0;
        }
        iopk::ChangeThreadPriority(0, 0x19);
    }
    return 1;
}

/* 0x2768: only stores while not playing (adsPlay applies them); while playing ProcLsn applies them at its next wake (Q4) */
static int adsVolume(uint32_t ch, uint16_t l, uint16_t r)
{
    Stream *S = &s_iamData[ch & 1];
    S->volL = l;
    S->volR = r;
    if (S->state == 3) s_uiOperate[ch & 1] |= OP_VOLUME;
    return 1;
}

/* 0x248: fssd module-5 callback; reply = s_iRpcRet (4 bytes) */
static void *RpcCallBack(uint32_t cmd, void *buf, int, int *outsize)
{
    const uint8_t *a = (const uint8_t *)buf;
    s_iRpcRet = 0;
    *outsize = 4;
    switch (cmd & 0x0fff0000u) {
    case 0x0010000: s_iRpcRet = adsSystemInit(); break;
    case 0x0030000: adsSystemStart(); break;
    case 0x0040000: adsSystemStop(); break;
    case 0x0050000: adsSetMasterVolume(rd16(a)); break;
    case 0x3000000: s_iRpcRet = adsOpen((int)(cmd & 1), (const char *)a); break;
    case 0x3010000: s_iRpcRet = adsSetBuff(rd32(a), (int32_t)rd32(a + 4)); break;
    case 0x3020000: s_iRpcRet = adsSetupWithLsn(rd32(a), (int32_t)rd32(a + 4), (int32_t)rd32(a + 8)); break;
    case 0x3030000: s_iRpcRet = adsSync(); break;
    case 0x3040000: s_iRpcRet = adsClose(rd32(a)); break;
    case 0x3050000: s_iRpcRet = adsPreset(rd32(a)); break;
    case 0x3060000: s_iRpcRet = adsPlay(rd32(a), rd32(a + 4)); break;
    case 0x3070000: s_iRpcRet = adsStop(rd32(a)); break;
    case 0x3080000: s_iRpcRet = adsPause(rd32(a)); break;
    case 0x3090000: s_iRpcRet = adsVolume(rd32(a), rd16(a + 4), rd16(a + 8)); break;
    case 0x30a0000: adsSetReverb(cmd & 1, rd32(a)); break;
    case 0x30b0000: s_iRpcRet = adsCheckEndOfStream(rd32(a)); break;
    case 0x30c0000: s_iRpcRet = adsSetup((int)(cmd & 1), (const char *)a); break;
    case 0x3100000: s_iRpcRet = adsSetReceiveEeAddr(rd32(a)); break;
    default: s_iRpcRet = -1; break;
    }
    return &s_iRpcRet;
}

/* ---------------------------------------------------------------- module start + test view */

/* 0x220. The host statics are cleared first: the state of a freshly loaded (zeroed) image. */
int ModuleStart(const iopk::ModuleArgs *)
{
    s_iRpcRet = 0;
    s_uiTransPcmDataToEEAddr = 0;
    s_ucTransPcmDataToEE = 0;
    memset(s_cdrm, 0, sizeof s_cdrm);
    memset(s_uiOperate, 0, sizeof s_uiOperate);
    memset(s_iamData, 0, sizeof s_iamData);
    memset(&s_iemData, 0, sizeof s_iemData);
    t_refills = t_last_fill = 0;
    fssd::Register(MODULE, RpcCallBack);
    return 0;
}

void GetView(View *v)
{
    v->proc_tid = s_iemData.proc_tid;
    v->proc_alive = s_iemData.proc_alive;
    v->setproc_tid = s_iemData.setproc_tid;
    v->setproc_alive = s_iemData.setproc_alive;
    for (int c = 0; c < 2; c++) {
        v->state[c] = s_iamData[c].state;
        v->buf[c] = s_iamData[c].buf;
    }
    v->refills = t_refills;
    v->last_fill = t_last_fill;
}

}  // namespace ads
