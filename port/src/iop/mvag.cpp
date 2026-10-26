/* mvag G1: module start, the fssd module-6 RPC ladder, the SPU interrupt
 * callbacks, the file layer (host file via ioman / raw CD sectors via cdvdman) and the lifecycle commands (Init, Quit,
 * Start, End, SetFile, SetSector, Play, Stop, ForceStop). Plus the module statics and the Io seam helpers.
 * All arithmetic is the R3000's (32-bit; signed compares where the asm uses slt / bltz / blez). */
#include "mvag_state.h"
#include "fssd.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace mvag {

/* ---- statics (image offsets in mvag_state.h) */
TrackTable s_trTable;
Track (&s_tr)[16] = s_trTable.tr;
uint32_t s_rmode;
int32_t s_iMaxBuffSize, s_iBuffSize, s_iHalfBuffSize, s_iReadSector, s_iMaxTr, s_iPlaySem, s_iBkreadSem, s_iMstrmStatus,
    s_iMode, s_iMasterTr, s_iMstTrTransSize;
uint8_t s_fmCur;
uint32_t s_pucFileBuf, s_pucSendBuf;
uint16_t s_usCore;
int32_t s_iPlayThreadId;
uint32_t s_uiVoice[2];
int32_t s_boMstrmKeyOn, s_boMstrmKeyOff, s_boMstrmSetVolume, s_boMstrmEntryLoad, s_boCommandStop, s_boDmaToEe,
    s_boMstrmActive;
uint32_t s_uiSubMode;
int32_t s_iTransTr, s_iBusSide, s_iBuffTime, g_iDispatchResult;
iopk::SifDmaData s_sdd;

static const char kFile[] = "fsiopsdmtvag.c";   /* @62 */

static bool dbg() { return (s_uiSubMode & 0x10000) != 0; }

/* ---- Io seam */
static const Io *g_io;
void SetIo(const Io *io) { g_io = io; }

int IoSdInit(int flag)
{
    if (!g_io) return sd::Init(flag);
    return g_io->sdInit ? g_io->sdInit(g_io->user, flag) : 0;
}
void IoSetParam(uint16_t entry, uint16_t val)
{
    if (!g_io) sd::SetParam(entry, val);
    else if (g_io->setParam) g_io->setParam(g_io->user, entry, val);
}
uint16_t IoGetParam(uint16_t entry)
{
    if (!g_io) return sd::GetParam(entry);
    return g_io->getParam ? (uint16_t)g_io->getParam(g_io->user, entry) : 0;
}
void IoSetSwitch(uint16_t entry, uint32_t val)
{
    if (!g_io) sd::SetSwitch(entry, val);
    else if (g_io->setSwitch) g_io->setSwitch(g_io->user, entry, val);
}
void IoSetAddr(uint16_t entry, uint32_t val)
{
    if (!g_io) sd::SetAddr(entry, val);
    else if (g_io->setAddr) g_io->setAddr(g_io->user, entry, val);
}
void IoSetCoreAttr(uint16_t entry, uint16_t val)
{
    if (!g_io) sd::SetCoreAttr(entry, val);
    else if (g_io->setCoreAttr) g_io->setCoreAttr(g_io->user, entry, val);
}
int IoVoiceTrans(int ch, uint16_t mode, uint32_t iop, uint32_t spu, uint32_t size)
{
    if (!g_io) return sd::VoiceTrans(ch, mode, iopk::Ptr(iop), spu, size);
    return g_io->voiceTrans ? g_io->voiceTrans(g_io->user, ch, mode, iop, spu, size) : 0;
}
void IoSetTransIntrHandler(int ch, sd::IntrHandler fn, void *arg)
{
    if (!g_io) sd::SetTransIntrHandler(ch, fn, arg);
    else if (g_io->setTransIntrHandler) g_io->setTransIntrHandler(g_io->user, ch, fn, arg);
}
void IoSetSpu2IntrHandler(sd::IntrHandler fn, void *arg)
{
    if (!g_io) sd::SetSpu2IntrHandler(fn, arg);
    else if (g_io->setSpu2IntrHandler) g_io->setSpu2IntrHandler(g_io->user, fn, arg);
}
uint32_t IoCallRpc(uint32_t cmd, const uint32_t w[8])
{
    if (!g_io) return fssd::CallRpc(6, 1, cmd, w);
    return g_io->callRpc ? g_io->callRpc(g_io->user, cmd, w) : 0;
}
uint32_t IoSifSetDma(const iopk::SifDmaData *d, int n)
{
    if (!g_io) return iopk::SifSetDma(d, n);
    return g_io->sifSetDma ? g_io->sifSetDma(g_io->user, d, n) : 0;
}
int IoSifDmaStat(uint32_t id)
{
    if (!g_io) return iopk::SifDmaStat(id);
    return g_io->sifDmaStat ? g_io->sifDmaStat(g_io->user, id) : -1;   /* null hook = done (LoadTrack spins while >= 0) */
}
int IoOpen(const char *name, int flags)
{
    if (!g_io) return iopk::Open(name, flags);
    return g_io->ioOpen ? g_io->ioOpen(g_io->user, name, flags) : 0;
}
int IoClose(int fd)
{
    if (!g_io) return iopk::Close(fd);
    return g_io->ioClose ? g_io->ioClose(g_io->user, fd) : 0;
}
int IoRead(int fd, void *buf, int n)
{
    if (!g_io) return iopk::Read(fd, buf, n);
    return g_io->ioRead ? g_io->ioRead(g_io->user, fd, buf, n) : 0;
}
int IoLseek(int fd, int off, int whence)
{
    if (!g_io) return iopk::Lseek(fd, off, whence);
    return g_io->ioLseek ? g_io->ioLseek(g_io->user, fd, off, whence) : 0;
}
int IoCdSearchFile(iopk::CdlFILE *fp, const char *name)
{
    if (!g_io) return iopk::CdSearchFile(fp, name);
    return g_io->cdSearchFile ? g_io->cdSearchFile(g_io->user, fp, name) : 0;
}
int IoCdRead(uint32_t lsn, uint32_t n, void *buf, const void *mode)
{
    if (!g_io) return iopk::CdRead(lsn, n, buf, mode);
    return g_io->cdRead ? g_io->cdRead(g_io->user, lsn, n, buf) : 0;
}
int IoCdSync(int mode)
{
    if (!g_io) return iopk::CdSync(mode);
    return g_io->cdSync ? g_io->cdSync(g_io->user, mode) : 0;
}
int IoDevctl(const char *name, int cmd)
{
    if (!g_io) return 0;   /* V3: no iopk devctl; the spin-mode switch has no native effect */
    return g_io->devctl ? g_io->devctl(g_io->user, name, cmd) : 0;
}
void IoFlushDcache()
{
    if (!g_io) iopk::FlushDcache();
    else if (g_io->flushDcache) g_io->flushDcache(g_io->user);
}
void IoHsynReserve(const uint32_t mask[2])
{
    if (!g_io) fssd::HsynSetReservVoice(mask);
    else if (g_io->hsynReserve) g_io->hsynReserve(g_io->user, mask);
}
void Log(const char *fmt, ...)
{
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (!g_io) iopk::Printf("%s", b);
    else if (g_io->print) g_io->print(g_io->user, b);
}

/* ---- start (0x5104) */
static int start()
{
    if (dbg()) Log("<start> start afs sound module. - SDMTVAG_MODULE_ID = %d\n", 6);
    fssd::Register(6, RpcCallBack);
    return 0;
}

int ModuleStart(const iopk::ModuleArgs *)
{
    /* the zeroed image + .data: a repeated start (tests) begins clean */
    memset(&s_trTable, 0, sizeof s_trTable);
    s_rmode = 0x01000000;
    s_iMaxBuffSize = 0x20000;
    s_iBuffSize = 0x20000;
    s_iHalfBuffSize = 0x10000;
    s_iReadSector = 0x20;
    s_iMaxTr = 0x10;
    s_iPlaySem = -1;
    s_iBkreadSem = -1;
    s_iMstrmStatus = 1;
    s_iMode = 1;
    s_iMasterTr = 0xf;
    s_iMstTrTransSize = 0x10000;
    s_fmCur = 0;
    s_pucFileBuf = s_pucSendBuf = 0;
    s_usCore = 0;
    s_iPlayThreadId = 0;
    s_uiVoice[0] = s_uiVoice[1] = 0;
    s_boMstrmKeyOn = s_boMstrmKeyOff = s_boMstrmSetVolume = s_boMstrmEntryLoad = s_boCommandStop = s_boDmaToEe =
        s_boMstrmActive = 0;
    s_uiSubMode = 0;
    s_iTransTr = s_iBusSide = s_iBuffTime = g_iDispatchResult = 0;
    memset(&s_sdd, 0, sizeof s_sdd);
    return start();
}

/* ---- RpcCallBack (0x4d14): dispatch on cmd & ~0xff, low byte = track / core index */
static uint32_t rd32(const void *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

void *RpcCallBack(uint32_t cmd, void *buf, int, int *outsize)
{
    uint32_t t0 = rd32(buf);
    int a0 = (int)(cmd & 0xff);
    int r;
    switch (cmd & 0xffffff00u) {
    case 0x0: r = CommandInit((const uint32_t *)buf); break;
    case 0x100: r = CommandQuit(); break;
    case 0x200: r = CommandStart((const uint32_t *)buf); break;
    case 0x300: r = CommandEnd(); break;
    case 0x400: r = CommandSetFile(a0, (const uint8_t *)buf); break;
    case 0x500: r = CommandSetSector(a0, (const uint32_t *)buf); break;
    case 0x600: r = CommandPlay(); break;
    case 0x700: r = CommandStop(); break;
    case 0x800: r = CommandForceStop(); break;
    case 0x900: r = CommandTrPlay(a0); break;
    case 0xa00: r = CommandTrStop(a0); break;
    case 0xb00: r = CommandTrPause(a0); break;
    case 0x1000: r = CommandSetVoice(a0, t0, 1); break;
    case 0x2000: r = CommandSetLoop(a0, (uint8_t)t0); break;
    case 0x4000: r = CommandSetVoidAutoPlay(a0, (uint8_t)t0); break;
    case 0x6000: r = CommandSetVolumeLeft(a0, (uint16_t)t0); break;
    case 0x7000: r = CommandSetVolumeRight(a0, (uint16_t)t0); break;
    case 0x8000: r = CommandSetVolumeLeftNow(a0, (uint16_t)t0); break;
    case 0x9000: r = CommandSetVolumeRightNow(a0, (uint16_t)t0); break;
    case 0xa000: r = CommandSetMasterVolumeLeftNow(a0, (uint16_t)t0); break;
    case 0xb000: r = CommandSetMasterVolumeRightNow(a0, (uint16_t)t0); break;
    case 0x10000: r = CommandGetStatus(); break;
    case 0x20000: r = CommandGetTrStatus(a0); break;
    case 0x40000: r = CommandIsEnableSeTr(a0); break;
    case 0x60000: r = CommandGetVolumeLeft(a0); break;
    case 0x70000: r = CommandGetVolumeRight(a0); break;
    case 0x80000: r = CommandGetMasterVolumeLeft(a0); break;
    case 0x90000: r = CommandGetMasterVolumeRight(a0); break;
    default:
        /* Q-V1: the previous result stays in g_iDispatchResult */
        if (dbg()) Log("MTVAG driver error: unknown uiCommand %x \n", cmd);
        *outsize = 4;
        return &g_iDispatchResult;
    }
    g_iDispatchResult = r;
    *outsize = 4;
    return &g_iDispatchResult;
}

/* ---- sd interrupt callbacks (arg = &s_iPlaySem) */
int interruptMstrmDma(int, void *arg)
{
    iopk::iSignalSema(*(int *)arg);
    return 0;
}

int interruptMstrmSpu2(int, void *arg)
{
    IoSetCoreAttr((uint16_t)(s_usCore | 4), 0);   /* SPU IRQ off, then wake the play thread */
    iopk::iSignalSema(*(int *)arg);
    return 0;
}

/* ---- file layer */
int __CdSync()
{
    IoCdSync(0);
    return 1;
}

void __SetFileSize(File *f, int size)
{
    f->size = size;
    int32_t s = f->size;
    f->sectors = s >= 0 ? s >> 11 : (int32_t)((uint32_t)s + 0x7ff) >> 11;
    s = f->size;
    int32_t r = (int32_t)((uint32_t)s & 0x7ff);
    if (s < 0 && r != 0) r -= 0x800;
    f->rem = r;
    if (f->rem > 0) f->sectors++;
}

void File_SetMode(uint8_t m)
{
    if (!s_fmCur) s_fmCur = m;
}

uint8_t File_GetMode() { return s_fmCur; }

/* the original's unbounded strcpy into +0x24 (Q-V5: may run into the next track record) */
static void name_copy(File *f, const char *name)
{
    uint8_t *d = (uint8_t *)f + 0x24;
    while ((*d++ = (uint8_t)*name++) != 0) {
    }
}

int File_Open(File *f, const char *name, int offset, int size)
{
    if (s_fmCur == 3) {
        name_copy(f, name);
        __SetFileSize(f, size);
        f->off = offset;
        f->fd = IoOpen(f->name, 1);
        return f->fd >= 0;
    }
    if (s_fmCur == 1) {
        name_copy(f, name);
        __SetFileSize(f, size);
        f->off = offset;
        return 1;
    }
    return 0;
}

int File_OpenSector(File *f, int sector, int offset, int size)
{
    f->sector = sector;
    f->off = offset;
    f->fd = (int32_t)((uint32_t)sector + (uint32_t)offset);
    f->w4 = size;
    __SetFileSize(f, size);
    f->name[0] = 0;
    return 1;
}

int File_Close(File *f)
{
    if (s_fmCur == 3) {
        if (f->fd >= 0) {
            IoClose(f->fd);
            f->fd = -1;
        }
        return 1;
    }
    return s_fmCur == 1 ? 1 : 0;
}

int File_ReadSector(File *f, void *buf, int sector, int count)
{
    if (s_fmCur == 3) {
        if (IoLseek(f->fd, (int)(((uint32_t)f->off + (uint32_t)sector) << 11), 0) < 0) return 0;
        return IoRead(f->fd, buf, (int)((uint32_t)count << 11)) >= 0;
    }
    if (s_fmCur == 1) {
        /* Q-V5: +0x8c is not added here; sceCdRead's result is dropped */
        IoCdRead((uint32_t)f->fd + (uint32_t)sector, (uint32_t)count, buf, &s_rmode);
        return __CdSync();
    }
    return 0;
}

int File_GetLocation(File *f)
{
    if (f->size > 0) return f->size;
    if (s_fmCur == 3) {
        IoLseek(f->fd, 0, 0);
        int sz = IoLseek(f->fd, 0, 2);
        __SetFileSize(f, sz);
        IoLseek(f->fd, 0, 0);
        return f->size;
    }
    if (s_fmCur == 1) {
        const char *p = f->name;
        for (;;) {
            char c = *p;
            if (!c) {
                p = f->name;
                break;
            }
            if (c == ':') {
                p++;
                break;
            }
            p++;
        }
        if (!IoCdSearchFile(reinterpret_cast<iopk::CdlFILE *>(f), p)) {
            Log("<File_GetLocation> ! can not find file : %s\n", f->name);
            return -1;
        }
        __SetFileSize(f, f->w4);
        return f->size;
    }
    return 0;
}

/* ---- lifecycle commands */
int CommandInit(const uint32_t *w)
{
    for (int i = 0; i < 16; i++) {
        Track &t = s_tr[i];
        t.voice = 0;
        t.volL = 0;
        t.volR = 0;
        t.loop = 0;
        t.autoplay = 0;
        t.master = 0;
        t.key = 0;
        t.pos = 0;
        t.time = 0;
        t.timeEnd = 0;
        t.head = 0;
        t.tail = 0;
        t.lastBytes = 0;
        t.state = 0;
        t.onMem = 0;
        t.spuAddr = 0;
    }
    s_uiVoice[0] = 0;
    s_uiVoice[1] = 0;
    uint32_t core = w[0];
    if (core == 1) s_usCore = 1;
    else if (core == 0) s_usCore = 0;
    uint8_t mode = ((const uint8_t *)w)[0x10];
    if (mode == 2 || mode == 1) s_iMode = mode;
    s_uiSubMode = w[5];
    int32_t mb = (int32_t)w[3];
    if (mb < 0x1000) mb = 0x1000;   /* Q-V2: signed clamp */
    s_iMaxBuffSize = mb & -0x1000;
    SetupBuffer(0x10, s_iMaxBuffSize, 0x180000, 1);
    if (dbg())
        Log("<CommandInit> core=%d, is_sdinit=%d, is_cdvd=%d, max_buffer_size=%d, mode=%d ... set core=%d, "
            "buffer_size=%d, mode=%d\n",
            (int)w[0], (int)w[1], (int)w[2], (int)w[3], ((const uint8_t *)w)[0x10], s_usCore, s_iBuffSize, s_iMode);
    if (w[1]) {
        IoSdInit(0);
        IoSetCoreAttr(0xa, 0);
    }
    File_SetMode(w[2] ? 1 : 3);
    if (s_iPlaySem == -1) {
        iopk::SemaParam sp;
        sp.attr = 0;
        sp.option = 0;   /* stack residue in the original (sp+0x48); iopk ignores it */
        sp.init_count = 0;
        sp.max_count = 1;
        s_iPlaySem = iopk::CreateSema(&sp);
    }
    if (s_iPlayThreadId == 0) {
        iopk::ThreadParam tp;
        tp.attr = 0x02000000;
        tp.option = 0;
        tp.entry = ProcPlayThread;
        tp.stack_size = 0x800;
        tp.priority = 0x1d;
        s_iPlayThreadId = iopk::CreateThread(&tp);
        iopk::StartThread(s_iPlayThreadId, 0);
    }
    if (dbg()) Log("%s(%d): >>> QueryMaxFreeMemSize=%d\n", kFile, 0x19e, (int)iopk::QueryMaxFreeMemSize());
    int st;
    iopk::CpuSuspendIntr(&st);
    s_pucFileBuf = iopk::AllocSysMemory(0, (uint32_t)s_iHalfBuffSize + 0x50, 0);   /* Q-V2: unchecked, old block leaked */
    s_pucSendBuf = s_pucFileBuf + 0x50;
    iopk::CpuResumeIntr(st);
    s_iMstrmStatus = 1;
    s_boMstrmActive = 0;
    return 1;
}

int CommandQuit()
{
    IoSetTransIntrHandler(0, 0, 0);
    IoSetSpu2IntrHandler(0, 0);
    int st;
    iopk::CpuSuspendIntr(&st);
    iopk::FreeSysMemory(s_pucFileBuf);   /* Q-V3: even 0 / already freed; the pointer is kept */
    iopk::CpuResumeIntr(st);
    if (s_iPlayThreadId) {
        iopk::TerminateThread(s_iPlayThreadId);
        iopk::DeleteThread(s_iPlayThreadId);
        s_iPlayThreadId = 0;
    }
    if (s_iPlaySem != -1) {
        iopk::DeleteSema(s_iPlaySem);
        s_iPlaySem = -1;
    }
    if (s_iBkreadSem != -1) {
        iopk::DeleteSema(s_iBkreadSem);
        s_iBkreadSem = -1;
    }
    return 1;
}

int CommandStart(const uint32_t *w)
{
    if (s_iMstrmStatus != 1) return 0;
    s_boMstrmActive = 1;
    s_iMstrmStatus = 2;
    IoSetTransIntrHandler(0, interruptMstrmDma, &s_iPlaySem);
    IoSetSpu2IntrHandler(interruptMstrmSpu2, &s_iPlaySem);
    SetupBuffer((int)w[0], (int)w[1], (int)w[2], (int)w[3]);
    if (File_GetMode() == 1) {
        int r = IoDevctl("cdrom0:", 0x4381);
        /* V4: the original passes only a1 = r to "%s(%d): ... devctl=%d" (file / line dropped) */
        if (dbg()) Log("(devctl result %d): set streaming-spin\n", r);
    }
    return 1;
}

int CommandEnd()
{
    if (File_GetMode() == 1) {
        int r = IoDevctl("cdrom0:", 0x4380);
        if (dbg()) Log("(devctl result %d): set normal-spin\n", r);   /* V4 */
    }
    if (s_iMstrmStatus == 5 || s_iMstrmStatus == 4 || s_iMstrmStatus == 3) CommandStop();
    s_boMstrmActive = 0;
    return 1;
}

int CommandSetFile(int idx, const uint8_t *buf)
{
    if (idx < 0 || idx >= s_iMaxTr) return 0;
    Track &t = s_tr[idx];
    if (t.state != 0 && t.state != 7) return 0;
    if (dbg())
        Log("<CommandSetFile>[%d] filename=%s, offset=%d, size=%d\n", idx, (const char *)buf, (int)rd32(buf + 0x80),
            (int)rd32(buf + 0x84));
    if (!File_Open(&t.file, (const char *)buf, (int)rd32(buf + 0x80), (int)rd32(buf + 0x84))) {
        if (dbg()) Log("file open failed. %s \n", (const char *)buf);
        return 0;
    }
    t.master = (rd32(buf + 0x88) & 1) != 0;
    t.eeDest = (int32_t)rd32(buf + 0x8c);
    t.syncTr = (int32_t)rd32(buf + 0x90);
    InitPlayTrack(idx);
    t.state = 1;
    if (s_iMstrmStatus == 4) s_boMstrmEntryLoad = 1;
    IoFlushDcache();
    return 1;
}

int CommandSetSector(int idx, const uint32_t *w)
{
    if (idx < 0 || idx >= s_iMaxTr) return 0;
    Track &t = s_tr[idx];
    if (t.state != 0 && t.state != 7) return 0;
    if (dbg())
        Log("<CommandSetSector>[%d] sector = %d + %d, size = %d\n", idx, (int)w[0], (int)w[1], (int)w[2]);
    if (!File_OpenSector(&t.file, (int)w[0], (int)w[1], (int)w[2])) return 0;
    t.master = (w[3] & 1) != 0;
    t.eeDest = (int32_t)w[4];
    t.syncTr = (int32_t)w[5];
    InitPlayTrack(idx);
    t.state = 1;
    if (s_iMstrmStatus == 4) s_boMstrmEntryLoad = 1;
    IoFlushDcache();
    return 1;
}

int CommandPlay()
{
    if (s_iMstrmStatus == 2) return PreLoad();
    return 0;
}

int CommandStop()
{
    if (s_iMstrmStatus == 4) {
        s_boCommandStop = 1;
        return 1;
    }
    return 0;
}

int CommandForceStop()
{
    IoFlushDcache();
    MakeSilentBuffer();
    s_iMstTrTransSize = s_iHalfBuffSize;
    s_iMasterTr = s_iMaxTr - 1;
    /* Q-V4: prints exactly when the master track's flag is 1 (inverted polarity in the original) */
    Assert(!(TrAt(s_iMasterTr).master == 1), kFile, 0x2bd);   /* D-V2: s_iMaxTr <= 0 -> -1 */
    LoadTrack(s_iMasterTr, 0, s_iMstTrTransSize);
    s_iMstrmStatus = 5;
    IoFlushDcache();
    return 1;
}

void Assert(int cond, const char *file, int line)
{
    if (!cond) Log("*** Assertion failed ! *** [%s](%d)\n", file, line);
}

void InitPlayTrack(int idx)
{
    Track &t = s_tr[idx];
    t.pos = 0;         /* +0x10 */
    t.time = 0;        /* +0x14 */
    t.lastBytes = 0;   /* +0x24 */
    t.onMem = 0;
}

}  // namespace mvag
