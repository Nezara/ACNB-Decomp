/* midi_state.h: module-internal state of the native modmidi. Shared by
 * midi.cpp and midi_selftest.cpp.
 *
 * Memory model: everything keeps the original layout in iopk::Ram(). The module's mutable words (the debug / verbose
 * module args, the per-ATick time step B_3ef0) sit at g_base + their image offsets; the CSL ctx, the port structs
 * (S2) with their sequencer state (env = S2 + 0x5c) and the stream buffers live in the caller's memory and are reached
 * through the IOP addresses the ctx holds. All pointers stored there are u32 IOP addresses, as in the original.
 * Read-only .data (the channel bit table D_3eb0, the tag strings) are constants in the code. */
#pragma once
#include <stdint.h>
#include "iopk.h"
#include "midi.h"

namespace midi {

/* ---------------------------------------------------------------- memory access */

extern uint32_t g_base;   /* module image base (IOP address); 0 = not started */

inline uint8_t *Mem(uint32_t a) { return iopk::Ram() + (a & (iopk::RAM_SIZE - 1)); }
inline uint8_t R8(uint32_t a) { return *Mem(a); }
inline uint16_t R16(uint32_t a) { return *reinterpret_cast<uint16_t *>(Mem(a)); }
inline uint32_t R32(uint32_t a) { return *reinterpret_cast<uint32_t *>(Mem(a)); }
inline void W8(uint32_t a, uint8_t v) { *Mem(a) = v; }
inline void W16(uint32_t a, uint16_t v) { *reinterpret_cast<uint16_t *>(Mem(a)) = v; }
inline void W32(uint32_t a, uint32_t v) { *reinterpret_cast<uint32_t *>(Mem(a)) = v; }

/* module words (image offsets) */
constexpr uint32_t D_DEBUG = 0x3e68;     /* `debug=N`: the error printf's */
constexpr uint32_t D_VERBOSE = 0x3e6c;   /* `verbose=N`: the tag / tempo printf's */
constexpr uint32_t B_ATICK = 0x3ef0;     /* Init: usec << 7, the accumulator step of one ATick */

/* ---------------------------------------------------------------- CSL ctx */

/* ctx: +0 group count (>= 2), +4 group array. group (8 B): +0 entry count, +4 entry array. entry (8 B): +0 unused,
 * +4 pointer. Group 0 (input): entry 2n = the SQ data of port n, 2n + 1 = its port struct S2. Group 1 (output): entry
 * k = stream buffer k (+0 capacity, +4 bytes used, data at +8). */
constexpr uint32_t CTX_NGROUP = 0x0, CTX_GROUP = 0x4;
constexpr uint32_t GRP_N = 0x0, GRP_ENTRIES = 0x4, GRP_SIZE = 8;
constexpr uint32_t SBUF_CAP = 0x0, SBUF_USED = 0x4, SBUF_DATA = 0x8;

/* ---------------------------------------------------------------- port struct S2 */

constexpr uint32_t S2_SONG = 0x00;      /* s32 song number, -1 none */
constexpr uint32_t S2_SEQ = 0x04;       /* s32 selected sequence, -1 none */
constexpr uint32_t S2_TICK = 0x08;      /* u32 current tick */
constexpr uint32_t S2_FLAGS = 0x0c;     /* 1 loaded, 2 playing, 4 ended, 8 (never set), 0x100 Song mode */
constexpr uint32_t S2_ROUTE = 0x10;     /* u16[16] output buffer mask per channel */
constexpr uint32_t S2_SYSROUTE = 0x30;  /* u16 output mask of sysex / system messages */
constexpr uint32_t S2_CHCB = 0x34, S2_CHCB_ARG = 0x38;       /* channel-message filter cb(msg, arg) */
constexpr uint32_t S2_METACB = 0x3c, S2_SYSEXCB = 0x44, S2_LOOPCB = 0x4c, S2_NRPNCB = 0x54;   /* (+4 = arg) */
constexpr uint32_t S2_ENV = 0x5c;
constexpr uint32_t S2_SIZE = 0x234;

constexpr uint32_t FL_LOADED = 1, FL_PLAY = 2, FL_ENDED = 4, FL_SONG = 0x100;

/* ---------------------------------------------------------------- env = S2 + 0x5c (sequencer state) */

constexpr uint32_t E_SEQU = 0x00, E_MIDI = 0x04, E_SONGCH = 0x08;   /* chunk pointers (0 = none) */
constexpr uint32_t E_EXT = 0x0c;        /* poly-aftertouch extension header or 0 */
constexpr uint32_t E_EV0 = 0x10;        /* event data start */
constexpr uint32_t E_PTR = 0x14;        /* read pointer */
constexpr uint32_t E_NEXT = 0x18;       /* u32 absolute tick of the next event */
constexpr uint32_t E_ACC = 0x1c;        /* u32 fractional accumulator */
constexpr uint32_t E_TLEN = 0x20;       /* u32 tick length, (us << 7) units */
constexpr uint32_t E_DIV = 0x24;        /* u32 ticks per quarter */
constexpr uint32_t E_TEMPO = 0x28;      /* u32 us per quarter */
constexpr uint32_t E_REL = 0x2c;        /* u32 relative tempo, 0x100 = 1.0 */
constexpr uint32_t E_RUN = 0x30;        /* u8 running status */
constexpr uint32_t E_DFLAG = 0x31;      /* u8 delta-omission flag */
constexpr uint32_t E_MASTER = 0x32;     /* u8 master volume factor */
constexpr uint32_t E_NRPN_MSB = 0x33, E_DATA_MSB = 0x34, E_NRPN_LSB = 0x35, E_DATA_LSB = 0x36;   /* u8, 0xff unset */
constexpr uint32_t E_USED = 0x3a;       /* u16 channels that sent anything */
constexpr uint32_t E_VOL = 0x3c;        /* u8[16] channel volume (CC 7 raw) */
constexpr uint32_t E_VOLF = 0x4c;       /* u8[16] channel volume factor */
constexpr uint32_t E_REC = 0x5c, REC_SIZE = 12;   /* controller record per channel */
constexpr uint32_t REC_PROG = 0, REC_CC0 = 1, REC_CC1 = 2, REC_CC2 = 3, REC_CC5 = 4, REC_CC7 = 5, REC_CC10 = 6,
                   REC_CC11 = 7, REC_CC64 = 8, REC_CC65 = 9, REC_BEND = 0xa;   /* u8 (0xff never), bend u16 (0xffff) */
constexpr uint32_t E_LOOP = 0x178, LOOP_SIZE = 12, LOOP_N = 8;   /* NRPN loop table: +0 id, +1 count (0xff free) */

/* Song area */
constexpr uint32_t E_SG_START = 0x11c;  /* u32 song data start (jump targets are relative to it) */
constexpr uint32_t E_SG_PTR = 0x120;    /* u32 song read pointer */
constexpr uint32_t E_FADE_LEN = 0x124;  /* u32 fade length, ATicks */
constexpr uint32_t E_FADE_REM = 0x128;  /* u32 fade ATicks left (0 = none) */
constexpr uint32_t E_FADE_TGT = 0x12c;  /* u8 fade target volume (raw) */
constexpr uint32_t E_SG_VOL = 0x12d;    /* u8 song volume 0 .. 0x80 */
constexpr uint32_t E_SG_VOLF = 0x12e;   /* u8 song volume factor */
constexpr uint32_t E_SG_LAST = 0x12f;   /* u8 last master factor sent (0xff none) */
constexpr uint32_t E_SG_STATE = 0x130;  /* u8 bit 0 deferred search, bit 1 its "after" flavour, bit 2 search running */
constexpr uint32_t E_SG_SRCH = 0x131;   /* u8 deferred search target */
constexpr uint32_t E_SG_PAN = 0x132;    /* u8 song pan 0 .. 0x7f */
constexpr uint32_t E_SG_TEMPO = 0x133;  /* u8 song tempo bpm 10 .. 255 */
constexpr uint32_t E_SG_REL = 0x134;    /* u16 song relative tempo */
constexpr uint32_t E_SG_REP = 0x138;    /* 8 x {u32 key (0 free), u8 count at +4} repeat table */
constexpr uint32_t FL_SONGACT = 0x200, FL_SONGEND = 0x400;   /* song started a sequence / song finished */

/* ---------------------------------------------------------------- internal functions (module offset) */

int TagEq(uint32_t w0, uint32_t w1, const char *a, const char *b);        /* 0x02ac */
void Recalc(uint32_t env);                                                /* 0x0c20 */
uint32_t Vlq(uint32_t env);                                               /* 0x0c5c */
uint32_t NextDelta(uint32_t env);                                         /* 0x0c90 */
uint32_t OutEntry(uint32_t idx, uint32_t group);                          /* 0x0ebc */
void Send(uint32_t s2, uint32_t env, uint32_t group, uint32_t msg, int n);   /* 0x1084 */
uint32_t VolScale(uint32_t env, uint32_t ch);                             /* 0x1218 */
uint32_t FindLoop(uint32_t table, uint32_t id);                           /* 0x0790 */
void Nrpn(uint32_t s2, uint32_t env, uint32_t status, uint32_t cc, uint32_t val, uint32_t group);   /* 0x07c4 */
void SysexCont(uint32_t env);                                             /* 0x0ce8 */
int Meta(uint32_t s2, uint32_t env, uint32_t type);                       /* 0x0d2c */
void Sysex(uint32_t s2, uint32_t env, uint32_t group);                    /* 0x0ee4 */
int PlayEnv(uint32_t s2, uint32_t env, uint32_t group);                   /* 0x1260 .. 0x17ac */
void AllNotesOff(uint32_t s2, uint32_t group);                            /* 0x17ac */
void Step(uint32_t s2, uint32_t group);                                   /* 0x1860 */
void ResendState(uint32_t s2, uint32_t group);                            /* 0x1910 */
int RewindEnv(uint32_t s2);                                               /* 0x1c90 */
void SendVolume(uint32_t s2, uint32_t env, uint32_t group, uint32_t ch);  /* 0x1fec */
int PortOf(uint32_t ctx, uint32_t port, uint32_t *s2, uint32_t *env);     /* 0x2614 */
/* Song mode (midi_song.cpp, NI7 W3) */
void SendAll(uint32_t ctx, uint32_t port, uint32_t msg, int n);           /* 0x24a4 */
void SongVolUpdate(uint32_t ctx, uint32_t port, uint32_t env);            /* 0x2550 */
void SongReset(uint32_t s2, uint32_t env);                                /* 0x2670 */
void ClampVol(uint32_t env, int32_t v);                                   /* 0x27f4 */
void ClampPan(uint32_t env, int32_t v);                                   /* 0x2818 */
void ClampTempo(uint32_t env, int32_t v);                                 /* 0x283c */
int SongStart(uint32_t ctx, uint32_t port, uint32_t s2, uint32_t idx);    /* 0x2864 */
int SongRepeat(uint32_t s2, uint32_t count);                              /* 0x292c */
void PlaySong(uint32_t ctx, uint32_t port, uint32_t s2);                  /* 0x2b6c .. 0x2f44 */
void SongStep(uint32_t ctx, uint32_t port, uint32_t s2);                  /* 0x30f0 */
int SongSearch(uint32_t s2, uint32_t env, uint32_t target, uint32_t play);   /* 0x33ec */

/* the channel-message resolver (SetChMsgResolver) */
extern CallbackFn g_cbFn;
extern void *g_cbUser;

}  // namespace midi
