/* ivag internal state. The comments give the original's image offsets
 * (.data = IRX initial values, .bss = zero at load); the records keep the original field order and widths (`short` list
 * links with -1 terminators, GH6). Track.vp are host pointers to the voice slots (not EE-visible). Field names of the
 * records G1 does not read are the map's inferred roles; the G2 .. G4 ports may rename them. Shared by the ivag*.cpp
 * files and the unit test; not for other modules (they use ivag.h). */
#pragma once
#include "ivag.h"
#include "../../platform/snd/iop/sd.h"

namespace ivag {

/* allocator list head {top, num} (s_SMemMng +8 / +0xc / +0x10) */
struct ListHead { int16_t top, num; };

/* s_SMemMng (0x5970, 0x20) */
struct SMemMng {
    int32_t base;            /* 0x00 */
    int32_t size;            /* 0x04 */
    ListHead free;           /* 0x08 free list, sorted by addr, coalesced */
    ListHead alloc;          /* 0x0c alloc list, newest first */
    ListHead pool;           /* 0x10 unused records */
    uint8_t pad14[0xc];
};

/* s_SMemInfo[16] (0x5990, 0x10 each) */
struct SMemInfo {
    int32_t addr;            /* 0x00 */
    int32_t size;            /* 0x04 */
    int16_t next, prev;      /* 0x08 / 0x0a */
    uint8_t pad0c[4];
};

/* s_IvagCmdQueue[8] (0x5ac0, 12 bytes each): a ring of 2; 1 setup, 2 play, 3 pause, 4 close */
struct CmdQueue {
    int32_t cmd[2];          /* 0x00 */
    int16_t head, count;     /* 0x08 / 0x0a */
};

/* s_IVagStatePack (0x5b20, 0x48; the a00e0000 reply) */
struct StatePack {
    struct { int32_t w0, w1; } req[8];   /* 0x00 w0 = pending setup stream index or -1, w1 = last setup result */
    uint8_t state[8];                    /* 0x40 track state mirror */
};

/* s_IVagVoice[16] (0x5b70, 0x2c each); roles per map 18.2 */
struct IVagVoice {
    int32_t spu;             /* 0x00 ring base (byte address); the ring is 2 * track.half */
    int32_t ringSize;        /* 0x04 2 * track.half (ivagSetup; no reader found) */
    int32_t writePos;        /* 0x08 offset inside the half being written: (writePos + chunk) % half */
    int32_t fileSize;        /* 0x0c bytes of this channel */
    int32_t off;             /* 0x10 played offset, += half per IRQ (fileSize - off = remaining) */
    int32_t streamPos;       /* 0x14 bytes read from FsMFStrm for this channel */
    int32_t consumed;        /* 0x18 bytes DMA'd */
    int32_t chunk;           /* 0x1c bytes of the DMA in flight */
    uint8_t core;            /* 0x20 0xff = none */
    uint8_t voice;           /* 0x21 0xff = none */
    uint8_t b22[2];
    uint16_t volL;           /* 0x24 0x3fff after ivagSetup (read by _Operate) */
    uint16_t volR;           /* 0x26 */
    uint8_t flags;           /* 0x28 bit0 = slot in use (_GetFreeVoice / _ReleaseVoice), bit1 = loop end crossed (set
                              * by _ReadTrackData arm 2, cleared by ivagSetup) */
    uint8_t b29[3];
};

/* s_IVagTrack[8] (0x5e30, 0x40 each; host pointers are 32-bit in both builds, so the layout is the original's) */
struct IVagTrack {
    int32_t stream;          /* 0x00 FsMFStrm stream index */
    IVagVoice *vp[2];        /* 0x04 / 0x08 (stereo = 2; the original has no room for more, Q-G2f) */
    int32_t nch;             /* 0x0c */
    int32_t cur;             /* 0x10 channel being served (round robin) */
    int32_t dataSize;        /* 0x14 total bytes of all channels (end test, deficit test) */
    int32_t played;          /* 0x18 bytes played, += half * nch per IRQ */
    int32_t sent;            /* 0x1c bytes DMA'd, all channels */
    int32_t rate;            /* 0x20 sampling rate (a multiple of 12000) */
    int32_t interleave;      /* 0x24 0x400 per channel per read */
    int32_t half;            /* 0x28 size of one ring half per channel (stereo 0x1000, mono 0x800 in arenaret1) */
    int32_t loopStart;       /* 0x2c in 16-byte units */
    int32_t loopEnd;         /* 0x30 in 16-byte units */
    uint8_t state;           /* 0x34 0 free, 1 reserved (ReserveSetup), 2 set up / first fill running, 3 filled = ready to
                              * start, 4 playing, 5 paused (_Operate pause arm), 6 data end reached (_IRQProc; the
                              * slots are released by the next _Operate, the state stays 6 until a Close) */
    uint8_t needTrans;       /* 0x35 1 = this half is filled, wait for the IRQ (the IRQ clears it) */
    uint16_t stopState;      /* 0x36 1 = stop requested (_CommandClose / Quit / ForceReset), 2 = data end reached
                              * (_IRQProc), 3 = pause requested (_CommandPause, state 4 only), 4 = start / resume
                              * requested (_CommandPlay), 5 = first fill deferred (ivagSetup) */
    uint8_t flags;           /* 0x38 bit0 active, bit1 half select (1: the voice plays the 2nd half, the DMA writes the
                              * 1st; toggled per IRQ by _FlipDBuff), bit2 looping stream (no end accounting, loop reads),
                              * bit3 track reverb on (ivagSetup sets it, ivagSetTrackReverb rewrites it) */
    uint8_t b39[3];
    int32_t irqCount;        /* 0x3c */
};

/* s_IVagMng (0x6030, 0x40; ivagInit memsets the first 0x38 bytes) */
struct IVagMng {
    uint32_t packetBuf;      /* 0x00 IOP address (AllocSysMemory), 0 = none */
    int32_t packetSize;      /* 0x04 */
    int32_t spuBase;         /* 0x08 */
    int32_t spuSize;         /* 0x0c */
    int32_t mode;            /* 0x10 0 idle, 1 DMA in flight (_TransTrackProc), 2 IRQ armed (_AfterTransProc) */
    int32_t sec, usec;       /* 0x14 / 0x18 */
    int16_t dmaCh;           /* 0x1c */
    int16_t masterL;         /* 0x1e */
    int16_t masterR;         /* 0x20 */
    uint8_t irqTrack;        /* 0x22 0xff = none */
    uint8_t transTrack;      /* 0x23 0xff = none */
    int32_t tid;             /* 0x24 _ThreadProc */
    int32_t semIsr;          /* 0x28 */
    int32_t semOp;           /* 0x2c */
    int32_t semCmd;          /* 0x30 */
    int32_t semQ;            /* 0x34 */
    uint8_t pad38[8];
};
static_assert(sizeof(SMemMng) == 0x20 && sizeof(SMemInfo) == 0x10 && sizeof(CmdQueue) == 0xc, "ivag layout");
static_assert(sizeof(StatePack) == 0x48 && sizeof(IVagVoice) == 0x2c && sizeof(IVagMng) == 0x40, "ivag layout");
static_assert(sizeof(void *) != 4 || sizeof(IVagTrack) == 0x40, "ivag layout");

/* ---- module state (ivag.cpp) */
extern uint32_t g_base;
extern int32_t s_iRpcRet;              /* 0x5960 */
extern SMemMng s_SMemMng;              /* 0x5970 */
extern SMemInfo s_SMemInfo[16];        /* 0x5990 */
extern int32_t s_iInitialize;          /* 0x5a90 */
extern uint32_t s_KeyOn[2];            /* 0x5a94 */
extern uint8_t s_ucTransFlag;          /* 0x5a9c */
extern uint8_t s_ucIrqFlag;            /* 0x5a9d */
extern int32_t s_IVagSema[8];          /* 0x5aa0 */
extern CmdQueue s_IvagCmdQueue[8];     /* 0x5ac0 */
extern StatePack s_IVagStatePack;      /* 0x5b20 */
extern IVagVoice s_IVagVoice[16];      /* 0x5b70 */
extern IVagTrack s_IVagTrack[8];       /* 0x5e30 */
extern IVagMng s_IVagMng;              /* 0x6030 */
extern int32_t s_iSetupThreadID;       /* .data 0x54e0 = -1 */
void ResetState();

/* ---- G1 (ivag.cpp, ivag_spumem.cpp) */
void *RpcCallBack(uint32_t cmd, void *buf, int size, int *outsize);
int module_init();
int MakeThread(iopk::ThreadEntry entry, int prio);
int MakeSema(int32_t *id, int max);    /* f_0638 */
int _GetInfo();
void _LinkInfo(ListHead *list, int idx);
void _UnlinkInfo(ListHead *list, int idx);
void _SortInsertInfo(ListHead *list, int idx);
void SpuMmng_Init(int addr, int size);
int SpuMmng_Alloc(int size);
void SpuMmng_Free(int addr);
int _AddCommand(int track, int cmd);
int _RemoveCommand(int track);
void _ThreadProc(void *);
void _CommandProc(void *);
int ivagInit();
int ivagSetPacketBuffer(int size);
int ivagSetSpuMemory(int addr, int size);
void *ivagGetTrackStat(int *outsize);
void ivagSetCoreVoice(int base);
void ivagSetDmaChannel(int ch);

/* ---- G2 ring engine (ivag_engine.cpp, map 18) */
int _SpuTransIntrHandler(int core, void *arg);   /* sd::IntrHandler (interrupt context) */
int _Spu2IntrHandler(int mask, void *arg);       /* sd::IntrHandler (interrupt context; mask ignored) */
int _WaitDmaTrans();                             /* blocks in sd (thread context only) */
void _GetSystemTime();
void _Wait2Ticks();                              /* WaitNextEvent loop: thread context only */
void _SetADPCMLoop(uint32_t buf, int n);         /* buf = IOP address */
void _AfterTransProc();
int _NextTransProc();
void _IRQProc();
void _FlipDBuff(IVagTrack *t);
int _ReadTrackData(IVagTrack *t);
int _TransTrackProc(int track);
int _StartTransTrackProc(int track);
void _FinishTransTrackProc(int track);
void _UpdateMasterTrack(int withReady);
void _SetIntrMasterTrack();

/* the engine's sd / FsMFStrm calls go through a unit-test seam (ivag::UnitTest only; 0 = the native sd / mfstrm). Under
 * a mock a 0 hook drops the call (result 0). iop / dst = IOP addresses. G3 / G4 add their calls here. */
struct EngineIo {
    void *user;
    void (*setTransIntrHandler)(void *u, int ch, sd::IntrHandler fn, void *arg);
    void (*setSpu2IntrHandler)(void *u, sd::IntrHandler fn, void *arg);
    void (*setCoreAttr)(void *u, uint32_t entry, uint32_t val);
    void (*setAddr)(void *u, uint32_t entry, uint32_t val);
    uint32_t (*getAddr)(void *u, uint32_t entry);
    int (*voiceTrans)(void *u, int ch, uint32_t mode, uint32_t iop, uint32_t spu, uint32_t size);
    int (*voiceTransStatus)(void *u, int ch, int wait);
    int (*isAttached)(void *u, int idx);
    int (*getValidDataSize)(void *u, int idx);
    int (*getData)(void *u, int idx, uint32_t dst, int n);
    int (*releaseData)(void *u, int idx, int n);
    void (*debugOut)(void *u);
    /* G4 (ivagSetup) */
    int (*getDataSync)(void *u, int idx, uint32_t dst, int n);
    int (*getStreamBufferSize)(void *u, int idx);
    int (*seek)(void *u, int idx, int pos);
    int (*setLoopPoint)(void *u, int idx, int start, int end, int loop);
    /* G3 (_Operate and the volume / reverb API) */
    void (*setParam)(void *u, uint32_t entry, uint32_t val);
    void (*setSwitch)(void *u, uint32_t entry, uint32_t val);
    uint32_t (*getSwitch)(void *u, uint32_t entry);
    void (*callRpc)(void *u, const uint32_t w[8]);   /* fssd::CallRpc(10, no-wait, 0x01000000, w) */
};
void SetEngineIo(const EngineIo *io);   /* not copied: the caller keeps it alive; 0 = native */
void IoSetTransIntrHandler(int ch, sd::IntrHandler fn);
void IoSetSpu2IntrHandler(sd::IntrHandler fn);
void IoSetCoreAttr(uint16_t entry, uint16_t val);
void IoSetAddr(uint16_t entry, uint32_t val);
uint32_t IoGetAddr(uint16_t entry);
int IoVoiceTrans(int ch, uint16_t mode, uint32_t iop, uint32_t spu, uint32_t size);
int IoVoiceTransStatus(int ch, int wait);
int IoIsAttached(int idx);
int IoGetValidDataSize(int idx);
int IoGetData(int idx, uint32_t dst, int n);
int IoReleaseData(int idx, int n);
void IoDebugOut();
int IoGetDataSync(int idx, uint32_t dst, int n);
int IoGetStreamBufferSize(int idx);
int IoSeek(int idx, int pos);
int IoSetLoopPoint(int idx, int start, int end, int loop);
void IoSetParam(uint16_t entry, uint16_t val);
void IoSetSwitch(uint16_t entry, uint32_t val);
uint32_t IoGetSwitch(uint16_t entry);
void IoCallRpc(const uint32_t w[8]);

/* ---- G4 setup / teardown + the reserve pair and voice-slot helpers (ivag_setup.cpp, map 19) */
constexpr uint32_t HDR_OFS = 0x5960;   /* ivagSetup's 0x20-byte header scratch in the image (the original's stack, H19.5-4) */
IVagVoice *_GetFreeVoice();
void _ReleaseVoice(IVagVoice *v);
int ivagReserveSetup(int track, int stream);
int ivagGetReserveSetupResult(int track);
int ivagSetup(int track, int stream);         /* CommandProc (cmd 1) or the ladder's 0x30000 */
int ivagQuit();
void ivagForceReset();                         /* the asm returns garbage in v0; the ladder drops it */
int ivagSetChannelCoreVoice(int track, int ch, int base);
int ivagSetTrackCoreVoice(int track, int base);

/* ---- G3 voice engine + play / stop / volume (ivag_op.cpp, map 20) */
/* the CallRpc payload words 2 .. 7 = the original's _Operate frame residue (map 20.3 R): each caller passes its
 * recorded constants; 0 = zeros (Quit / ForceReset) */
struct OpRes { uint32_t w[6]; };
extern int g_op_calls;                           /* _Operate calls since load (unit test) */
void _Operate(const OpRes *res = 0);
uint16_t _Change15bitVolume(int v);
int32_t VolScale(int32_t vol, int32_t master);   /* (low 32 bits of vol * master) / 0x3fff, signed */
int32_t PitchOf(int32_t rate);                   /* (rate << 12) / 48000, signed */
void _CommandPlay(int track);
void _CommandPause(int track);
void _CommandClose(int track);
int ivagPlay(int track);
int ivagClose(int track);
int ivagPause(int track);
int ivagGetChNum(int track);
int ivagSetChVolume(int track, int ch, uint16_t l, uint16_t r);
int ivagSetMasterVolume(uint16_t l, uint16_t r);
int ivagSetVolumePack(const uint8_t *p, int n);   /* p = &buf[1] (the fssd receive buffer), n = buf[0] (clamped by the ladder) */
void ivagSetTrackReverb(int track, int on);      /* the asm leaves garbage in v0; the ladder drops it */

}  // namespace ivag
