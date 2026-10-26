/* mvag internal state: the track / file records at their disc layout, the module statics (host statics: no MVAG data
 * is EE-visible; image offsets in comments), the sd / fssd / CD / ioman / SIF unit-test seam (Io) and the function list
 * of all four groups. */
#pragma once
#include "mvag.h"
#include "../../platform/snd/iop/sd.h"
#include <stddef.h>

namespace mvag {

/* File record (0x98, at track +0x30). In mode 1 sceCdSearchFile writes a CdlFILE (0x20: lsn, size, name[16],
 * date[8]) over +0x00 .. +0x1f. */
struct File {
    int32_t fd;            /* +0x00 fd (mode 3) or lsn (mode 1; CdlFILE.lsn) */
    int32_t w4;            /* +0x04 CdlFILE.size / File_OpenSector's size */
    uint8_t cdl_rest[0x1c];/* +0x08 CdlFILE name / date */
    char name[0x60];       /* +0x24 unbounded strcpy target (Q-V5: long names run into +0x84.. and the next track) */
    int32_t size;          /* +0x84 bytes (<= 0 = unknown) */
    int32_t sector;        /* +0x88 start sector (File_OpenSector) */
    int32_t off;           /* +0x8c offset in sectors */
    int32_t sectors;       /* +0x90 ceil(size / 0x800) */
    int32_t rem;           /* +0x94 signed size % 0x800 */
};
static_assert(sizeof(File) == 0x98, "File layout");
static_assert(offsetof(File, name) == 0x24 && offsetof(File, size) == 0x84, "File layout");

/* Track record s_tr[i] (0xd0); field roles: map section 12.1. */
struct Track {
    int32_t voice;         /* +0x00 0..23 */
    uint16_t volL, volR;   /* +0x04 / +0x06 */
    uint8_t loop;          /* +0x08 */
    uint8_t autoplay;      /* +0x09 */
    uint8_t master;        /* +0x0a RPC flags & 1 != 0 */
    uint8_t b0b;           /* +0x0b (never written by G1) */
    uint8_t key;           /* +0x0c pending key command (1 play, 2 stop, 3 pause) */
    uint8_t b0d[3];        /* +0x0d */
    int32_t pos;           /* +0x10 next file sector to read */
    int32_t time;          /* +0x14 elapsed half-buffer ticks (TrackAdvancePlayTime) */
    int32_t timeEnd;       /* +0x18 total ticks = ceil(sectors / s_iReadSector) */
    int32_t head;          /* +0x1c VAG header bytes to blank (0x40 / 0) */
    int32_t tail;          /* +0x20 trailer bytes to cut (0x10 / 0) */
    int32_t lastBytes;     /* +0x24 bytes delivered by the last ReadHalfBufferFile */
    int32_t spuAddr;       /* +0x28 */
    uint8_t onMem;         /* +0x2c 1 = whole file resident in one half-buffer (not a master track) */
    uint8_t state;         /* +0x2d 0 empty, 1 set, 2 ready, 3 playing (stream), 4 playing (resident), 5 linked,
                              6 paused, 7 ended */
    uint8_t b2e[2];        /* +0x2e */
    File file;             /* +0x30 */
    int32_t eeDest;        /* +0xc8 RPC +0x8c / w[4]: EE address LoadTrack DMAs to (master tracks, non-zero) */
    int32_t syncTr;        /* +0xcc RPC +0x90 / w[5]: partner track (-1 none; unchecked index, Q-V8) */
};
static_assert(sizeof(Track) == 0xd0, "Track layout");
static_assert(offsetof(Track, state) == 0x2d && offsetof(Track, file) == 0x30 && offsetof(Track, eeDest) == 0xc8 &&
              offsetof(Track, syncTr) == 0xcc,
              "Track layout");

/* s_tr 0x596c .. 0x666c plus the image tail up to IMAGE_SIZE (a long name in track 15 runs into it) */
struct TrackTable { Track tr[16]; uint8_t tail[4]; };
extern TrackTable s_trTable;
extern Track (&s_tr)[16];

/* .data (restored by ModuleStart) */
extern uint32_t s_rmode;          /* 0x51a4 0x01000000: the sceCdRMode of sceCdRead */
extern int32_t s_iMaxBuffSize;    /* 0x51d8 0x20000 */
extern int32_t s_iBuffSize;       /* 0x51dc 0x20000 */
extern int32_t s_iHalfBuffSize;   /* 0x51e0 0x10000 */
extern int32_t s_iReadSector;     /* 0x51e4 0x20 */
extern int32_t s_iMaxTr;          /* 0x51e8 0x10 */
extern int32_t s_iPlaySem;        /* 0x51ec -1 */
extern int32_t s_iBkreadSem;      /* 0x51f0 -1 (never created) */
extern int32_t s_iMstrmStatus;    /* 0x51f4 1: 1 idle, 2 started, 3 preload, 4 running, 5 terminate */
extern int32_t s_iMode;           /* 0x51f8 1: 1 master-track, 2 dynamic master */
extern int32_t s_iMasterTr;       /* 0x51fc 0xf */
extern int32_t s_iMstTrTransSize; /* 0x5200 0x10000 */
/* .bss */
extern uint8_t s_fmCur;           /* 0x5910 file mode: 0 unset, 1 CD, 3 host file */
extern uint32_t s_pucFileBuf;     /* 0x5914 IOP address (AllocSysMemory result, unchecked) */
extern uint32_t s_pucSendBuf;     /* 0x5918 = s_pucFileBuf + 0x50 */
extern uint16_t s_usCore;         /* 0x591c */
extern int32_t s_iPlayThreadId;   /* 0x5920 */
extern uint32_t s_uiVoice[2];     /* 0x5924 reserve masks per core */
extern int32_t s_boMstrmKeyOn;    /* 0x592c */
extern int32_t s_boMstrmKeyOff;   /* 0x5930 */
extern int32_t s_boMstrmSetVolume;/* 0x5934 */
extern int32_t s_boMstrmEntryLoad;/* 0x5938 */
extern int32_t s_boCommandStop;   /* 0x593c */
extern int32_t s_boDmaToEe;       /* 0x5940 */
extern int32_t s_boMstrmActive;   /* 0x5944 */
extern uint32_t s_uiSubMode;      /* 0x5948 bit 0x10000 debug prints, bit 1 no SE tracks */
extern int32_t s_iTransTr;        /* 0x594c */
extern int32_t s_iBusSide;        /* 0x5950 */
extern int32_t s_iBuffTime;       /* 0x5954 */
extern int32_t g_iDispatchResult; /* 0x5958 the reply word (Q-V1: stale on unknown cmds) */
extern iopk::SifDmaData s_sdd;    /* 0x595c (0x10 on the IOP) */

/* ---- unit-test seam: every sd / fssd / CD / ioman / SIF-DMA call goes through Io* (0 = native). Under a mock a 0 hook
 * drops the call (result 0). Threads, semas, sysmem, clock stay real iopk. iop = IOP address. */
struct Io {
    void *user;
    int (*sdInit)(void *u, int flag);
    void (*setParam)(void *u, uint32_t entry, uint32_t val);
    uint32_t (*getParam)(void *u, uint32_t entry);
    void (*setSwitch)(void *u, uint32_t entry, uint32_t val);
    void (*setAddr)(void *u, uint32_t entry, uint32_t val);
    void (*setCoreAttr)(void *u, uint32_t entry, uint32_t val);
    int (*voiceTrans)(void *u, int ch, uint32_t mode, uint32_t iop, uint32_t spu, uint32_t size);
    void (*setTransIntrHandler)(void *u, int ch, sd::IntrHandler fn, void *arg);
    void (*setSpu2IntrHandler)(void *u, sd::IntrHandler fn, void *arg);
    uint32_t (*callRpc)(void *u, uint32_t cmd, const uint32_t w[8]);   /* fssd::CallRpc(6, 1, cmd, w) */
    uint32_t (*sifSetDma)(void *u, const iopk::SifDmaData *d, int n);
    int (*sifDmaStat)(void *u, uint32_t id);
    int (*ioOpen)(void *u, const char *name, int flags);
    int (*ioClose)(void *u, int fd);
    int (*ioRead)(void *u, int fd, void *buf, int n);
    int (*ioLseek)(void *u, int fd, int off, int whence);
    int (*cdSearchFile)(void *u, iopk::CdlFILE *fp, const char *name);
    int (*cdRead)(void *u, uint32_t lsn, uint32_t n, void *buf);
    int (*cdSync)(void *u, int mode);
    int (*devctl)(void *u, const char *name, int cmd);   /* ioman devctl(name, cmd, 0, 0, 0, 0); native: no-op, 0 (V3) */
    void (*flushDcache)(void *u);
    void (*print)(void *u, const char *text);            /* Kprintf / printf text (native: iopk::Printf) */
    void (*hsynReserve)(void *u, const uint32_t mask[2]); /* fsiopsdHsynSetReservVoice (native: fssd) */
};
void SetIo(const Io *io);   /* not copied: the caller keeps it alive; 0 = native */
int IoSdInit(int flag);
void IoSetParam(uint16_t entry, uint16_t val);
uint16_t IoGetParam(uint16_t entry);
void IoSetSwitch(uint16_t entry, uint32_t val);
void IoSetAddr(uint16_t entry, uint32_t val);
void IoSetCoreAttr(uint16_t entry, uint16_t val);
int IoVoiceTrans(int ch, uint16_t mode, uint32_t iop, uint32_t spu, uint32_t size);
void IoSetTransIntrHandler(int ch, sd::IntrHandler fn, void *arg);
void IoSetSpu2IntrHandler(sd::IntrHandler fn, void *arg);
uint32_t IoCallRpc(uint32_t cmd, const uint32_t w[8]);
uint32_t IoSifSetDma(const iopk::SifDmaData *d, int n);
int IoSifDmaStat(uint32_t id);
int IoOpen(const char *name, int flags);
int IoClose(int fd);
int IoRead(int fd, void *buf, int n);
int IoLseek(int fd, int off, int whence);
int IoCdSearchFile(iopk::CdlFILE *fp, const char *name);
int IoCdRead(uint32_t lsn, uint32_t n, void *buf, const void *mode);
int IoCdSync(int mode);
int IoDevctl(const char *name, int cmd);
void IoFlushDcache();
void IoHsynReserve(const uint32_t mask[2]);
void Log(const char *fmt, ...);   /* Kprintf */

/* G1 (mvag.cpp) */
void *RpcCallBack(uint32_t cmd, void *buf, int size, int *outsize);   /* static in the original; visible for tests */
int interruptMstrmDma(int core, void *arg);
int interruptMstrmSpu2(int core, void *arg);
int __CdSync();
void __SetFileSize(File *f, int size);
void File_SetMode(uint8_t m);
uint8_t File_GetMode();
int File_Open(File *f, const char *name, int offset, int size);
int File_OpenSector(File *f, int sector, int offset, int size);
int File_Close(File *f);
int File_ReadSector(File *f, void *buf, int sector, int count);
int File_GetLocation(File *f);
int CommandInit(const uint32_t *w);
int CommandQuit();
int CommandStart(const uint32_t *w);
int CommandEnd();
int CommandSetFile(int idx, const uint8_t *buf);
int CommandSetSector(int idx, const uint32_t *w);
int CommandPlay();
int CommandStop();
int CommandForceStop();
void Assert(int cond, const char *file, int line);
void InitPlayTrack(int idx);

/* G2 (mvag_track.cpp) */
int CommandTrPlay(int idx);
int CommandTrStop(int idx);
int CommandTrPause(int idx);
int CommandSetVoice(int idx, uint32_t voice, int reserve);
int CommandSetLoop(int idx, uint8_t v);
int CommandSetVoidAutoPlay(int idx, uint8_t v);
int CommandSetVolumeLeft(int idx, uint16_t v);
int CommandSetVolumeRight(int idx, uint16_t v);
int CommandSetVolumeLeftNow(int idx, uint16_t v);
int CommandSetVolumeRightNow(int idx, uint16_t v);
int CommandSetMasterVolumeLeftNow(int core, uint16_t v);
int CommandSetMasterVolumeRightNow(int core, uint16_t v);
int CommandGetStatus();
int CommandGetTrStatus(int idx);
int CommandIsEnableSeTr(int idx);
int CommandGetVolumeLeft(int idx);
int CommandGetVolumeRight(int idx);
int CommandGetMasterVolumeLeft(int core);
int CommandGetMasterVolumeRight(int core);
/* G2 internal (mvag_track.cpp) */
void SetLoopFlag(int part, int size, int loop);
int GetNextValidTrack(int start, int mode);
void TrackAdvancePlayTime();
void TrackCheckOnMemory();
void TrackSetVoice();
void TrackSetVolume();
void KeyOn(int force);
void KeyOff(int force);
/* G3 (mvag_play.cpp) */
void SetupBuffer(int maxTr, int buffSize, int spuStart, int fromStart);
void JudgeDynamicMasterTrack();
void MakeSilentBuffer();
int ReadPreloadFile(int idx);
int ReadHalfBufferFile(int idx);
int LoadTrack(int tr, int half, int size);
int PreLoad();
int EntryLoad();
void ProcPlayThread(void *arg);
/* G4 (mvag_exec.cpp): `out` = ProcPlayThread's sp+0x1c slot = the sub-state (thread lifetime, Q-V18); the entry
 * points return 1 on every path (Q-V17); the subs return 0 next sub-state / 1 rescan / 2 leave */
int ExecStatusPreLoad(int *out);
int ExecStatusRunning(int *out);
int ExecStatusTerminate(int *out);
int ExecStatusPreLoadSubFileLocation(int *out);
int ExecStatusPreLoadSubFixTrackLoad(int *out);
int ExecStatusPreLoadSubPreFullOnMemoryLoad(int *out);
int ExecStatusPreLoadSubFullOnMemoryLoad(int *out);
int ExecStatusPreLoadSubPreTrackLoad(int *out);
int ExecStatusPreLoadSubTrackLoad(int *out);
int ExecStatusPreLoadSubStartSpu2Interrupt(int *out);
int ExecStatusRunninSubSetup(int *out);
int ExecStatusRunninTrackLoad(int *out);
int ExecStatusRunninTrackEntryLoad(int *out);
int ExecStatusRunninStartSpu2Interrupt(int *out);
/* D-V2 (host-only guard, mvag_exec.cpp): s_tr[i] for 0 <= i < 16, else a zeroed scratch record (logged once) */
Track &TrAt(int i);

}  // namespace mvag
