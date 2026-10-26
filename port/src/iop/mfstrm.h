/* mfstrm: FromSoftware's IOP multi-file stream reader "FsMFStrm" v0101 (cdrom0:\IOP\FSMFSTRM.IRX, crc 9f6e69b8)
 * decompiled onto the native IOP kernel iopk (phase 3 step 5 NI8; , names from the
 * sibling build FSMFSTRM_BIN). The lower half of the BGM-return music path: the EE drives it over SID 0x4010 (init,
 * stream allocation, attach / poll), FSSDIVAG pulls the stream bytes with the in-IOP exports below.
 *
 * Sources of the original: mstream.c (mstrm_*: 8 streams, the update thread), stream.c (strm_*: one stream = a file or a
 * disc sector run), sbuf.c (sbuf_*: 8 ring buffers), dma.c (dma_*: an arbiter pushing stream data to the EE by SIF DMA,
 * never used in Versus), the RPC front (_EtoI_*) and an IOP -> EE client (SID 0x4020, never fires).
 * Files: mfstrm.cpp (module start, RPC server + ladder, IOP -> EE bind), mfstrm_stream.cpp (mstrm_ / strm_),
 * mfstrm_read.cpp (update pass, read engine, data out), mfstrm_sbuf.cpp, mfstrm_dma.cpp, mfstrm_selftest.cpp;
 * internal state: mfstrm_state.h.
 *
 * EE-visible data lives in the phantom image (iopk::Ram() at the module base + the original .bss offsets): the reply
 * buffer g_ucRet_RpcBuff (+0x6e50, ONE static buffer: only word 0 is ever written, 8 commands and unknown fnos leave it
 * as the previous storing command did - hazard H1), the receive buffer g_ucEtoI_RpcBuff (+0x6f50) and the IOP -> EE
 * buffer g_ucItoE_RpcBuff (+0x7050). The stream / ring / arbiter records are not EE-visible: host statics, reset at
 * module start like the zeroed image + the IRX .data values.
 *
 * Exports (the original's 19 fsimfstrm_* stubs are `j mstrm_*` tail jumps: they vanish, IVAG calls these directly).
 * IOP addresses (stream buffers, IVAG's destination buffers) are uint32_t IOP addresses in iopk::Ram(). Signatures
 * read from the asm (prologue + return paths; verifier-checked). Native: Init / AllocStream (G1), AttachSector ..
 * GetValidDataSize, SetLoopPoint (G2); Sync (G3); GetData / GetDataSync / ReleaseData / DebugOut (G4,
 * mfstrm_read.cpp); AllocStreamSetBuffer / FreeStream / AttachFile (G5). */
#pragma once
#include <stdint.h>
#include "../../platform/snd/iop/iopk.h"

namespace mfstrm {

constexpr uint32_t IMAGE_SIZE = 0x7620;
constexpr uint32_t OFS_RET = 0x6e50, OFS_ETOI = 0x6f50, OFS_ITOE = 0x7050;   /* 0x100 bytes each */
constexpr uint32_t SID = 0x4010, SID_IOP2EE = 0x4020;

int ModuleStart(const iopk::ModuleArgs *m);   /* `start` (0x1f38) without RegisterLibraryEntries: returns 2 (resident) */
int ModuleStop();                             /* module_stop (0x1edc): `start` with a0 < 0, reached only from the unit
                                                 test; ReleaseLibraryEntries vanishes (0) -> 1 */
uint32_t Base();                              /* image base (0 = not started) */

/* fsimfstrm exports (index = export table entry) */
int mstrm_Init();                                            /* 4: 1 = ok, 0 = already initialised / failure */
int mstrm_AllocStream(int size);                             /* 5: stream index or -1 */
int mstrm_AllocStreamSetBuffer(uint32_t buf, int size);      /* 6 (G5; nobody imports it): caller's buffer (IOP address),
                                                              size & ~0x7ff, not owned; stream index or -1 */
int mstrm_FreeStream(int idx);                               /* 7 (G5; RPC 0x2000): 1 / 0 (bad idx) */
int mstrm_AttachFile(int idx, const char *name, uint16_t prio);   /* 8 (G5; RPC 0x3000): strm_OpenFile's result / 0 */
int mstrm_AttachSector(int idx, int lsn, int size, uint16_t prio);   /* 9 (G2) */
int mstrm_IsAttached(int idx);                               /* 10 (G2; IVAG) */
int mstrm_DetachFileSector(int idx);                         /* 11 (G2) */
int mstrm_Seek(int idx, int pos);                            /* 19 (G2; IVAG) */
int mstrm_GetStreamBufferSize(int idx);                      /* 21 (G2/G4; IVAG) */
int mstrm_GetAttachDataSize(int idx);                        /* 12 GetFileSize (G2; IVAG, 0 call sites) */
int mstrm_GetRemainSize(int idx);                            /* 13 (G2) */
int mstrm_GetValidDataSize(int idx);                         /* 14 (G2; IVAG) */
int mstrm_GetData(int idx, uint32_t dst, int n);             /* 15 (G4; IVAG): bytes copied to IOP address dst, -1 = dst 0 / bad idx */
int mstrm_GetDataSync(int idx, uint32_t dst, int n);         /* 16 (G4; IVAG): total bytes copied */
int mstrm_Sync(int idx);                                     /* 17 (G3): strm_Sync's result, 0 = bad idx */
int mstrm_ReleaseData(int idx, int n);                       /* 18 RemoveData (G4; IVAG): sbuf_RemoveDataBlock's result, -1 = bad idx */
int mstrm_SetLoopPoint(int idx, int start, int end, int loop);   /* 20 (G2; IVAG): start / end rounded up to 0x800 -> +0x14 /
                                                              +0x18, loop != 0 sets +0x2c bit 5; no return value in the
                                                              asm (here SignalSema's result / 0; the RPC arm ignores it) */
void mstrm_DebugOut();                                       /* 22 DebugOut = f_3ef4 (G4; IVAG): walks the alloc list */

/* checks: the map 11.5 .. 17.5 vectors on a fresh iopk (Reset; the caller re-initialises iopk afterwards), returns the
 * number of failures */
int UnitTest();

}  // namespace mfstrm
