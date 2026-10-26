/* sein: Sony's SE-stream input library (modsein "SE_Sequence_Input_Module" v0101, IRX.BIN@049000, crc f2cf568c, image
 * 0x1ca0) decompiled into native C++ over iopk.
 * A byte-record builder: every export appends one "fe" record `fe 01 <id u32 LE> <payload>` to an output stream buffer
 * of the caller's CSL ctx (group 1 = {buffNum, buffCtx}, entry n = {?, buffer IOP address}, buffer = {u32 cap, u32 used,
 * data}); hsyn's fe parser reads them. Generic: no globals, no threads, no kernel
 * calls besides the module start; all ctx / buffer pointers are IOP addresses (iopk::Ram()).
 *
 * Dead in Versus: only the module start runs (no replay calls sceSEIn_Init; SYTH's SetCslSeStream, RPC 0x11600, is
 * never sent). Checked by UnitTest (golden bytes from the asm).
 *
 * Names: the original is symbol-less; Init is SYTH's import name (sceSEIn_Init), the others are SDK-style names
 * inferred from the record layout and hsyn's parser (unverified); the comment of each gives the module offset and the
 * export index. Byte arguments are taken as words and narrowed (& 0xff) as the original's lbu of the argument slot. */
#pragma once
#include <stdint.h>
#include "iopk.h"

namespace sein {

constexpr uint32_t IMAGE_SIZE = 0x1ca0;

int ModuleStart(const iopk::ModuleArgs *m);   /* f_0000: CpuSuspendIntr / RegisterLibraryEntries (vanishes) / Resume */
uint32_t Base();                              /* image base (0 = not started) */

/* exports (index in the original's table; ctx = the CSL ctx, port = output buffer index, id = the 32-bit SE id).
 * All return 0 or -1 (bad ctx / port, bank >= 16, kind out of range, buffer full: nothing is written then). */
int Init(uint32_t ctx);                                                     /* 4  0x070 */
int ATick(uint32_t ctx);                                                    /* 5  0x200 returns 0 */
int Load(uint32_t ctx);                                                     /* 6  0x228 returns 0 */
int PutMsg(uint32_t ctx, uint32_t port, uint32_t id, uint32_t w0, uint32_t w1);   /* 7 0x664 raw 7 bytes (w0 & 0xf0 == 0xa0) */
int PutSeMsg(uint32_t ctx, uint32_t port, uint32_t id, uint32_t data, uint32_t len);   /* 8 0x3f0 the emitter (data = IOP addr) */
int NoteOn(uint32_t ctx, uint32_t port, uint32_t id, uint32_t bank, uint32_t prog, uint32_t note, uint32_t velo,
           uint32_t pan);                                                   /* 9  0x828 a0|bank */
int PitchOn(uint32_t ctx, uint32_t port, uint32_t id, uint32_t bank, uint32_t prog, uint32_t note, uint32_t velo,
            uint32_t pan, uint32_t bend);                                   /* 10 0x9e0 a0|bank + 14-bit bend */
int TimeVolume(uint32_t ctx, uint32_t port, uint32_t id, uint32_t bank, uint32_t a, uint32_t b, uint32_t v,
               uint32_t c);                                                 /* 11 0x1254 b0 kind 7 */
int TimePanpot(uint32_t ctx, uint32_t port, uint32_t id, uint32_t bank, uint32_t a, uint32_t b, uint32_t v,
               uint32_t c, uint32_t kind);                                  /* 12 0x1400 b0 kind 0xc / 0xd */
int TimePitch(uint32_t ctx, uint32_t port, uint32_t id, uint32_t bank, uint32_t a, uint32_t b, uint32_t v,
              uint32_t c, uint32_t kind);                                   /* 13 0x15d8 b0 kind 0xe / 0xf, c 16-bit */
int PitchLFO(uint32_t ctx, uint32_t port, uint32_t id, uint32_t bank, uint32_t a, uint32_t b, uint32_t v,
             uint32_t kind);                                                /* 14 0x17c0 b0 kind 0x10 .. 0x12 */
int AmpLFO(uint32_t ctx, uint32_t port, uint32_t id, uint32_t bank, uint32_t a, uint32_t b, uint32_t v,
           uint32_t kind);                                                  /* 15 0x19cc b0 kind 0x20 .. 0x22 */
int AllNoteOff(uint32_t ctx, uint32_t port, uint32_t id);                   /* 16 0xf38 c0 1f */
int AllNoteOffMask(uint32_t ctx, uint32_t port, uint32_t id, uint32_t x, uint32_t y);   /* 17 0xff0 c0 1e x y */
int NoteOn90(uint32_t ctx, uint32_t port, uint32_t id, uint32_t bank, uint32_t prog, uint32_t note, uint32_t velo,
             uint32_t pan);                                                 /* 18 0xbb0 90|bank */
int PitchOn90(uint32_t ctx, uint32_t port, uint32_t id, uint32_t bank, uint32_t prog, uint32_t note, uint32_t velo,
              uint32_t pan, uint32_t bend);                                 /* 19 0xd68 90|bank + bend */

/* unit test (sein_selftest.cpp): every builder's bytes, the emitter's capacity / port / ctx guards, Init's checks and
 * the VLQ writer, on a scratch ctx in iopk::Ram() (run before iopk::Reset; leaves Base() unchanged). Failures are
 * printed with the prefix "sein-unit:"; returns the number of failures. */
int UnitTest();

}  // namespace sein
