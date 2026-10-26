/* sdr: Sony's SDRDRV ("sdr_driver" 4.0.1, the libsdr RPC server SID 0x80000701; IRX.BIN@058000, crc f3727dca, stripped)
 * decompiled into native C++. The EE's sceSdRemote* calls arrive as SIF RPCs (fno & 0xfff0
 * = command, fno & 0xf = core for the effect commands); the server thread unpacks the 0x40-byte receive buffer and calls
 * the matching sd export (38 command arms), the result goes into the reply word D_012a0 (16-byte reply; 0x8140 replies
 * the EffectAttr buffer B_019c0, 0x81c0 / 0x81d0 the ProcBatch result block B_01380). Command 0xe620 starts a callback
 * thread that forwards sd's transfer-end / SPU2-IRQ interrupts (the three handlers installed by 0x8160 / 0x8170) to the
 * EE as blocking IOP -> EE RPCs on SID 0x80000704. Versus only sends 0x8000 (sceSdRemoteInit); everything else is REV
 * (unit tests in sdr_selftest.cpp). All EE-visible data lives in the phantom image (iopk::Ram() at base + the original
 * offsets). */
#pragma once
#include <stdint.h>
#include "../../platform/snd/iop/iopk.h"
#include "../../platform/snd/iop/sd.h"

namespace sdr {

constexpr uint32_t IMAGE_SIZE = 0x1a18;
constexpr uint32_t SID = 0x80000701;
constexpr uint32_t SID_IOP2EE = 0x80000704;   /* the callback thread's EE server */
/* module offsets of the original's three interrupt handlers (0x8160 / 0x8170 reply them as the previous handler) */
constexpr uint32_t OFS_TRANSINTR0 = 0xd4c, OFS_TRANSINTR1 = 0xd8c, OFS_SPU2INTR = 0xdcc;
constexpr uint32_t OFS_RET = 0x12a0, OFS_CBTAB = 0x12b0, OFS_FLAGS = 0x12f0, OFS_SEND = 0x1330, OFS_BATCHRES = 0x1380,
                   OFS_BATCHRET = 0x1384, OFS_RECV = 0x1980, OFS_EFFECT = 0x19c0, OFS_CLIENT = 0x19f0;

int ModuleStart(const iopk::ModuleArgs *m);   /* f_0000 without RegisterLibraryEntries */
int SetThreadPriority(int main, int cb);      /* f_0278 (export 4; also command 0x8f10) */
int SetCallback(uint32_t fno, uint32_t fn);   /* f_0bec (export 5): fn = IOP code address; returns the old one / -1 */

/* Host form of a 0x90x0 callback: the original jalr's the IOP code address in B_012b0; natively a slot runs only if a
 * host function was registered for exactly that address (SetCallbackFn), otherwise it logs and replies 0. No importer of
 * export 5 exists on the disc, so the table is always empty in ACNB. */
typedef uint32_t (*Callback)(uint32_t fno, void *buf);
int SetCallbackFn(uint32_t fno, uint32_t addr, Callback fn);   /* SetCallback(fno, addr) + host function for addr */

/* the module's interrupt handlers (0xd4c / 0xd8c channel 0 / 1 transfer end, 0xdcc SPU2 IRQ), for tests / hosts */
sd::IntrHandler TransIntrHandler(int ch);
sd::IntrHandler Spu2IntrHandler();

/* module globals kept on the host (not EE-visible): priorities, thread ids (test seam) */
extern int32_t B_01370, B_01374;   /* main / callback thread priority */
extern int32_t B_01378;            /* server thread id */
extern int32_t B_019e4;            /* callback thread id (0 until 0xe620) */
uint32_t Base();

/* sd unit-test seam (sdr::UnitTest only; 0 = native sd). Under a mock a 0 hook drops the call (result 0). Pointers are
 * already host pointers (iopk::Ptr of the IOP addresses the EE sent). */
struct Io {
    void *user;
    int (*init)(void *u, int flag);
    void (*setParam)(void *u, uint16_t entry, uint16_t val);
    uint16_t (*getParam)(void *u, uint16_t entry);
    void (*setSwitch)(void *u, uint16_t entry, uint32_t val);
    uint32_t (*getSwitch)(void *u, uint16_t entry);
    void (*setAddr)(void *u, uint16_t entry, uint32_t val);
    uint32_t (*getAddr)(void *u, uint16_t entry);
    int (*setCoreAttr)(void *u, uint16_t entry, uint16_t val);
    uint16_t (*getCoreAttr)(void *u, uint16_t entry);
    uint16_t (*note2Pitch)(void *u, uint16_t cn, uint16_t cf, uint16_t note, int16_t fine);
    uint16_t (*pitch2Note)(void *u, uint16_t cn, uint16_t cf, uint16_t pitch);
    int (*procBatch)(void *u, const sd::BatchEntry *b, uint32_t *ret, uint32_t num);
    int (*procBatchEx)(void *u, const sd::BatchEntry *b, uint32_t *ret, uint32_t num, uint32_t voice);
    int (*voiceTrans)(void *u, int ch, uint16_t mode, void *iop, uint32_t spu, uint32_t size);
    int (*blockTrans)(void *u, int ch, uint16_t mode, void *iop, uint32_t size, void *start);
    int (*voiceTransStatus)(void *u, int ch, int wait);
    uint32_t (*blockTransStatus)(void *u, int ch);
    int (*setEffectAttr)(void *u, int core, const sd::EffectAttr *a);
    int (*getEffectAttr)(void *u, int core, sd::EffectAttr *a);
    int (*clearEffectWorkArea)(void *u, int core, int ch, int mode);
    sd::IntrHandler (*setTransIntrHandler)(void *u, int ch, sd::IntrHandler fn, void *arg);
    sd::IntrHandler (*setSpu2IntrHandler)(void *u, sd::IntrHandler fn, void *arg);
    int (*stopTrans)(void *u, int ch);
    int (*cleanEffectWorkArea)(void *u, int core, int ch, int mode);
    int (*setEffectMode)(void *u, int core, const sd::EffectAttr *a);
    int (*setEffectModeParams)(void *u, int core, const sd::EffectAttr *a);
};
void SetIo(const Io *io);   /* not copied: the caller keeps it alive; 0 = native sd */

int UnitTest();   /* sdr_selftest.cpp: the map's vectors on a fresh iopk; returns the failure count */

}  // namespace sdr
