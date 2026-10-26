/* evag: FromSoftware's FSSDEVAG - the
 * "external VAG" streamer: one mono ADPCM voice (default core 1, voice 23) fed by the EE through IOP -> EE requests,
 * a play thread woken by one semaphore (SIF data arrived, SPU DMA done, SPU IRQ). Versus only runs its init (EE fno
 * 0x30010000: vagInit, the core-1 voice-23 VMIX clear and the {0, 0x800000} reserve-voice push into fssd); every other
 * command is ported anyway (user s92) and checked by evag::UnitTest.
 * Files: evag.cpp (G1: front, lifecycle API, callbacks), evag_play.cpp (G2: vagStart + the play thread engine),
 * evag_state.h (records, seam), evag_selftest.cpp. */
#pragma once
#include <stdint.h>
#include "../../platform/snd/iop/iopk.h"

namespace evag {

constexpr uint32_t IMAGE_SIZE = 0x1ec0;
int ModuleStart(const iopk::ModuleArgs *m);   /* start (0x1d0) */
int UnitTest();                               /* evag_selftest.cpp: map 8.4 vectors on a fresh iopk; failures */

}  // namespace evag
