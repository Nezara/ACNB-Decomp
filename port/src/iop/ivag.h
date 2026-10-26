/* ivag: FromSoftware's IOP multi-track streamed-VAG player "fsiopsdivag" v0102 (cdrom0:\IOP\FSSDIVAG.IRX, crc 13235eff,
 * fssd module 10) decompiled onto the native IOP kernel iopk (phase 3 step 5 NI8 part 2; map:
 * , names from the sibling build FSSDIVAG_BIN). The SPU half of the Arena-return BGM: up to 8
 * tracks, each one FsMFStrm stream played through a double-buffered ring in SPU RAM (core 1) whose SPU IRQ address marks
 * the half already played; the EE drives it with `a0......` RPCs on fssd's SID 0x101a (fno >> 28 == 10).
 *
 * Sources of the original: ivag.c (ivag* = the RPC-facing API) and an unnamed helper layer (_Operate, _AfterTransProc,
 * SpuMmng_* ...). Files: ivag.cpp (module start, RPC ladder, thread / sema helpers, command queue, the two threads,
 * ivagInit and the G1 setters), ivag_spumem.cpp (SPU RAM allocator), ivag_engine.cpp (the SPU-IRQ / DMA-end ring
 * engine), ivag_setup.cpp (setup / teardown, ReserveSetup), ivag_selftest.cpp (unit tests); internal state:
 * ivag_state.h; ivag_op.cpp (G3: _Operate, the CommandProc handlers, the play / stop / volume API). Fully native
 * since G3 (landing B: the test-only stand-ins are gone).
 *
 * Nothing of the module is EE-visible in IOP RAM: the replies are copied out by fssd (s_iRpcRet, 4 bytes; the 0x48-byte
 * state pack for GetTrackStat), so the state is host statics, reset at module start like the zeroed image + the IRX
 * .data values. The packet buffer is an AllocSysMemory block in iopk::Ram() (its address is the trace's DMA madr). */
#pragma once
#include <stdint.h>
#include "../../platform/snd/iop/iopk.h"

namespace ivag {

constexpr uint32_t IMAGE_SIZE = 0x6070;
constexpr int MODULE = 10;   /* fssd module index */

int ModuleStart(const iopk::ModuleArgs *m);   /* `start` (0x5d8): fsiopsdRegister(10, RpcCallBack), returns 0 (not resident) */
uint32_t Base();                              /* image base (0 = not started) */

/* checks: the map 14.6 vectors on a fresh iopk (Reset; the caller re-initialises iopk afterwards), returns the number of
 * failures */
int UnitTest();

uint32_t PacketBuffer();                      /* s_IVagMng.packetBuf (IOP address, 0 = none; base of the stream compare) */

}  // namespace ivag
