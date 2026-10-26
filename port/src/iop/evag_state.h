/* evag internal state: the original's .bss records at their module offsets as
 * host statics (EVAG data is not EE-visible), the function list of both groups, and the sd / fssd unit-test seam (Io).
 * Shared by evag.cpp (G1: front + lifecycle), evag_play.cpp (G2: vagStart + the play thread) and evag_selftest.cpp. */
#pragma once
#include "evag.h"
#include "../../platform/snd/iop/sd.h"
#include <stdint.h>

namespace evag {

/* s_ivmData (0x1e70, 0x40 bytes; names mine, offsets from the asm) */
struct VagMng {
    uint32_t buf;        /* +0x00 IOP buffer (Setup w0) */
    uint32_t spu;        /* +0x04 SPU2 buffer base (vagInit 0x1e0000; RPC 0x203) */
    uint32_t half;       /* +0x08 half-buffer bytes (Setup w3) */
    uint32_t total;      /* +0x0c stream bytes (Setup w1) */
    uint32_t xfer;       /* +0x10 bytes DMA'd to the SPU */
    uint32_t rd;         /* +0x14 bytes requested from the EE */
    uint32_t played;     /* +0x18 bytes of halves consumed by IRQs */
    uint32_t sec;        /* +0x1c SysClock2USec stamp (the 63 us waits measure from it) */
    uint32_t usec;       /* +0x20 */
    uint8_t pad24[0xc];  /* +0x24 never touched */
    uint16_t volL;       /* +0x30 */
    uint16_t volR;       /* +0x32 */
    int16_t status;      /* +0x34 low 12 bits = state, 0x4000 = EE read request issued */
    uint16_t rate;       /* +0x36 Setup w2 (Hz) */
    int16_t op;          /* +0x38 bit0 stop, bit1 pause, 0x1000 pending */
    int8_t core;         /* +0x3a core & 1 */
    int8_t voice;        /* +0x3b voice % 24 (signed rem) */
    int8_t tail;         /* +0x3c IRQs left before the stop */
    uint8_t toggle;      /* +0x3d bit0 = SPU half of the next fill */
    uint8_t pad3e[2];
};
static_assert(sizeof(VagMng) == 0x40, "s_ivmData layout");

/* s_itpData (0x1e60, 0x10 bytes): the play-thread record */
struct VagThread {
    int32_t tid;     /* +0x0 */
    int32_t sema;    /* +0x4 */
    int32_t sema2;   /* +0x8 never created (DeletePlayThread still deletes it when > 0) */
    int8_t quit;     /* +0xc */
    uint8_t pad[3];
};
static_assert(sizeof(VagThread) == 0x10, "s_itpData layout");

extern uint8_t s_ucSafeIttr;     /* 0x1e50 */
extern uint8_t s_ucEmergency;    /* 0x1e51 */
extern uint8_t s_ucSifdmaFlag;   /* 0x1e52 */
extern uint8_t s_ucTransFlag;    /* 0x1e53 */
extern uint8_t s_ucIrqFlag;      /* 0x1e54 */
extern VagThread s_itpData;      /* 0x1e60 */
extern VagMng s_ivmData;         /* 0x1e70 */
extern int32_t s_iRpcRet;        /* 0x1eb0 */

/* the module's sd / fssd calls go through a unit-test seam (evag::UnitTest only; 0 = native sd / fssd). Under a mock a
 * 0 hook drops the call (result 0). iop = IOP address. Threads / semas / clock stay real iopk. */
struct Io {
    void *user;
    void (*setParam)(void *u, uint32_t entry, uint32_t val);
    void (*setSwitch)(void *u, uint32_t entry, uint32_t val);
    uint32_t (*getSwitch)(void *u, uint32_t entry);
    void (*setAddr)(void *u, uint32_t entry, uint32_t val);
    uint32_t (*getAddr)(void *u, uint32_t entry);
    void (*setCoreAttr)(void *u, uint32_t entry, uint32_t val);
    uint32_t (*getCoreAttr)(void *u, uint32_t entry);
    int (*voiceTrans)(void *u, int ch, uint32_t mode, uint32_t iop, uint32_t spu, uint32_t size);
    int (*voiceTransStatus)(void *u, int ch, int wait);
    void (*setTransIntrHandler)(void *u, int ch, sd::IntrHandler fn, void *arg);
    void (*setSpu2IntrHandler)(void *u, sd::IntrHandler fn, void *arg);
    void (*callRpc)(void *u, const uint32_t w[8]);   /* fssd::CallRpc(3, wait 1, 0x2040000, w) */
};
void SetIo(const Io *io);   /* not copied: the caller keeps it alive; 0 = native */
void IoSetParam(uint16_t entry, uint16_t val);
void IoSetSwitch(uint16_t entry, uint32_t val);
uint32_t IoGetSwitch(uint16_t entry);
void IoSetAddr(uint16_t entry, uint32_t val);
uint32_t IoGetAddr(uint16_t entry);
void IoSetCoreAttr(uint16_t entry, uint16_t val);
uint32_t IoGetCoreAttr(uint16_t entry);
int IoVoiceTrans(int ch, uint16_t mode, uint32_t iop, uint32_t spu, uint32_t size);
int IoVoiceTransStatus(int ch, int wait);
void IoSetTransIntrHandler(int ch, sd::IntrHandler fn);
void IoSetSpu2IntrHandler(sd::IntrHandler fn);
void IoCallRpcRead(uint32_t pos, uint32_t size);   /* fsiopsdCallRpc(3, 1, 0x2040000, pos, size); residue words 0 (E5) */

/* the voice's sd entry: (core | voice << 1 | add) & 0xffff from the s8 fields (lb, sll, or, ori, andi) */
uint16_t VoiceEntry(uint32_t add);
/* the inline 63 us busy wait of vagClose / vagStart / _vagOperateProc (E2): sec differs, or the signed usec difference
 * from the +0x1c / +0x20 stamp is >= 63; tested first, then WaitNextEvent (Guard 1) */
void Wait63us();
/* GetSystemTime + SysClock2USec into the +0x1c / +0x20 stamp */
void StampTime();

/* G1 (evag.cpp) */
int MakePlaySem();
int MakePlayThread(iopk::ThreadEntry entry, int prio);
void DeletePlayThread(VagThread *itp);
int _IntrTransCB(int core, void *arg);
int _IntrSpu2CB(int core, void *arg);
void _SetVoiceAttr();
int vagInit();
int vagSetup(uint32_t buf, uint32_t total, uint32_t rate, uint32_t half);
int vagGetStatus();
void vagTransFinish();
int vagPause();
int vagClose();
int vagVolume(uint16_t l, uint16_t r);
void vagSetCoreVoice(int core, int voice);
void vagSetSpu2Addr(uint32_t a);

/* G2 (evag_play.cpp): vagStart + the play thread; the state handlers are visible for evag::UnitTest */
int vagStart();
void _vagProc(void *arg);
void _vagAfterReadFirst();
void _vagIRQProc();
void _vagSpu2TransProc();
void _vagOperateProc();

}  // namespace evag
