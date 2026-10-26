/* midi: Sony's IOP MIDI sequencer library (modmidi "Midi_Sequencer_Module" v0202, IRX.BIN@034800, crc f8266c0e, image
 * 0x3ef4) decompiled into native C++ over iopk. Generic (no game
 * addresses): the caller (FromSoftware's SYTH) passes its CSL ctx as an IOP address; the port structs (S2, 0x234 bytes),
 * their sequencer state (env = S2 + 0x5c) and the output stream buffers live in the caller's memory and keep the
 * original layout, so SYTH reads / writes the same words as in the original (S2 +0x0c flags, +0x10 route masks, +0x34
 * channel-message callback). No threads, semaphores, timers or kernel calls: everything runs from ATick (SYTH's 240 Hz
 * thread, interrupts held by the caller) and the exports. One instance per process.
 *
 * State of the port (NI7 W1 + W2): module start, Init, Load (chunk tag checks), Select, PlaySwitch, SetLocation, the
 * two tempo exports, the per-tick time loop Step, the event player PlayEnv (Meta, sysex, the NRPN loop table, the
 * poly-aftertouch extension table), the message writer Send, AllNotesOff, ResendState (+ SendVolume / VolScale), the VLQ
 * / delta readers and the tick-length recalc are native. NI7 W3 (midi_song.cpp): Song mode (exports 7 .. 13, the song
 * op interpreter, SongStep from ATick) and the exports no module imports (1 .. 3, 17 .. 19, 22 .. 25); dead in this
 * game, unit-tested only. Meta / sysex / loop / NRPN-user callbacks (IOP code; SYTH sets none) are logged once and
 * treated as unset. The whole module is native. */
#pragma once
#include <stdint.h>
#include "iopk.h"

namespace midi {

constexpr uint32_t IMAGE_SIZE = 0x3ef4;

int ModuleStart(const iopk::ModuleArgs *m);   /* f_0000: `debug=` / `verbose=` args, no RegisterLibraryEntries */
uint32_t Base();                              /* image base (0 = not started) */

/* the 8 exports SYTH imports (index in the original's table); ctx = the CSL ctx, port = 0 .. 3 */
int Init(uint32_t ctx, uint32_t usec);                             /* 4  0 / -1 */
int ATick(uint32_t ctx);                                           /* 5  0 */
int Load(uint32_t ctx, uint32_t port);                             /* 6  0 / -1 (SQ chunk checks) */
int SelectMidi(uint32_t ctx, uint32_t port, uint32_t idx);         /* 14 0 / -1 */
int MidiPlaySwitch(uint32_t ctx, uint32_t port, uint32_t on);      /* 15 0 / -1 */
int MidiSetLocation(uint32_t ctx, uint32_t port, uint32_t tick);   /* 16 0 / -1 */
int MidiGetAbsoluteTempo(uint32_t ctx, uint32_t port);             /* 20 bpm 20 .. 255, 0 = none */
int MidiSetRelativeTempo(uint32_t ctx, uint32_t port, uint32_t t); /* 21 0 / -1 (t & 0xffff, 0x100 = 1.0) */

/* the exports nothing imports (NI7 W3, midi_song.cpp) */
int Reserved();                                                                 /* 1 .. 3  0 (`jr ra`) */
int SongSelect(uint32_t ctx, uint32_t port, uint32_t idx);                      /* 7  0 / -1 */
int SongPlaySwitch(uint32_t ctx, uint32_t port, uint32_t mode);                 /* 8  0 stop, 1 pause, 2 go */
int SongSetVolumeFactor(uint32_t ctx, uint32_t port, uint32_t v);               /* 9 */
int SongSetVolume(uint32_t ctx, uint32_t port, uint32_t v);                     /* 10 */
int SongSetTempo(uint32_t ctx, uint32_t port, uint32_t v);                      /* 11 bpm */
int SongSetRelTempo(uint32_t ctx, uint32_t port, uint32_t v);                   /* 12 */
int SongSearchPlay(uint32_t ctx, uint32_t port, uint32_t n, uint32_t mode);     /* 13 */
int SetChannelVolumeFactor(uint32_t ctx, uint32_t port, uint32_t ch, uint32_t v);   /* 17 (ch 0xff = master) */
int SetChannelVolume(uint32_t ctx, uint32_t port, uint32_t ch, uint32_t v);     /* 18 (ch 0xff = all) */
int SetTempoBpm(uint32_t ctx, uint32_t port, uint32_t bpm);                     /* 19 */
int GetRelativeTempo(uint32_t ctx, uint32_t port);                              /* 22 */
int SetTempoRaw(uint32_t ctx, uint32_t port, uint32_t us);                      /* 23 */
int GetTempoRaw(uint32_t ctx, uint32_t port);                                   /* 24 */
int Unload(uint32_t ctx, uint32_t port);                                        /* 25 (Q-W3a: stops port 2 port + 1) */

/* the channel-message callback (S2 +0x34, an IOP code address the caller stored): the host maps it to native code.
 * cb = the stored address, arg = S2 +0x38; returns the (possibly rewritten) message, 0 = drop. Unset or an unknown
 * address: the message passes unchanged (logged once). */
typedef uint32_t (*CallbackFn)(void *u, uint32_t cb, uint32_t msg, uint32_t arg);
void SetChMsgResolver(CallbackFn fn, void *u);

/* unit tests of the native part (scratch image in iopk::Ram(); run before iopk::Reset, they leave Base() = 0):
 * failures are printed with the prefix "midi-unit:"; returns the number of failures */
int UnitTest();
/* the time loop + event player on a recorded SQ (sq = its bytes, n = size): Load / Select header words, then Play and
 * ATick until the sequence end; the tick counter after every ATick and (golden test) the port-0 output bytes of every
 * ATick against midi_model.py (port/tools/midi_tickref.py: counts, crc32s, advance histogram, end tick; on a byte
 * mismatch the first differing 1024-ATick block is named and the native stream is written to
 * build/midi_golden_native.bin for `midi_tickref.py --cmp`). *aticks = ATicks run, *end_tick = the final tick. Same
 * scratch image and prefix as UnitTest; returns the number of failures. */
int TickTest(const uint8_t *sq, uint32_t n, uint32_t *aticks, uint32_t *end_tick);

}  // namespace midi
