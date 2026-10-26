/* mvag: FromSoftware's FSSDMVAG ("MTVAG", fssd module 6, IRX.BIN@05e000, crc 22c01a56, source fsiopsdmtvag.c; map
 * ) - the multi-track VAG streamer: up to 16 ADPCM tracks (host files or raw CD sectors) on
 * one SPU2 core, a play thread (prio 0x1d) refilling half-buffers of one SPU2 ring, woken by SPU DMA-end / SPU IRQ
 * through one semaphore. The game never loads it (module table entry isLoad 0, no EE client); ported anyway (user
 * s95) and checked only by mvag::UnitTest.
 * Files: mvag.cpp (G1: front, file layer, lifecycle commands), mvag_state.h (records, statics, Io seam),
 * mvag_stub.cpp (test-only stand-ins for G2 - G4), mvag_selftest.cpp. */
#pragma once
#include <stdint.h>
#include "../../platform/snd/iop/iopk.h"

namespace mvag {

/* .iopmod bss size is 0: last bss symbol s_tr 0x596c + 16 * 0xd0 = 0x666c, rounded */
constexpr uint32_t IMAGE_SIZE = 0x6670;
int ModuleStart(const iopk::ModuleArgs *m);   /* start (0x5104) after restoring .data / zeroing .bss */
int UnitTest();                               /* mvag_selftest.cpp: map 8.4 vectors on a fresh iopk; failures */

}  // namespace mvag
