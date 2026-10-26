/* hsyn_state.h: module-internal state of the native modhsyn (Sony "SPU2_Synthesizer_Module" v0105, IRX.BIN@03a000,
 * crc 8ae08f7c; .. + Part 3). Shared by every hsyn cluster
 * file (H1 hsyn.cpp, H2 hsyn_sweep.cpp, H3 hsyn_bank.cpp / hsyn_query.cpp, then H3a note on / off / H4 voice writer /
 * H5 MS input).
 *
 * Memory model: everything keeps the original layout in iopk::Ram. The module .bss (image offsets
 * 0xb910 .. 0xf152) sits at g_base + offset; the CSL ctx, the 16 port blocks (channels, bank slots) and the stream
 * buffers live in SYTH's blob and are reached through the IOP addresses the ctx holds; the voice-stat buffer is SYTH's.
 * Every pointer stored in that memory (list links, ctx / block / slot / HD pointers) is a u32 IOP address, as in the
 * original, so the image stays byte-comparable with the hybrid. The structs below are views at those addresses
 * (V(addr), Ch(addr), Blk(addr), S()); list words go through Lw(addr). Little-endian, no padding: the field offsets are
 * the original's (static_asserts).
 *
 * Field names marked (u) are unverified; widths are the asm load / store widths. Fields no map has read yet are pads
 * (pNN = offset): name them when a cluster reads them. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "iopk.h"

namespace hsyn {

/* ---------------------------------------------------------------- memory access */

extern uint32_t g_base;   /* module image base (IOP address); 0 = not started */

inline uint8_t *Mem(uint32_t a) { return iopk::Ram() + (a & (iopk::RAM_SIZE - 1)); }
inline uint32_t &Lw(uint32_t a) { return *reinterpret_cast<uint32_t *>(Mem(a)); }   /* a list / pointer word */
inline uint8_t R8(uint32_t a) { return *Mem(a); }
inline uint16_t R16(uint32_t a) { return *reinterpret_cast<uint16_t *>(Mem(a)); }
inline uint32_t R32(uint32_t a) { return *reinterpret_cast<uint32_t *>(Mem(a)); }
inline void W8(uint32_t a, uint8_t v) { *Mem(a) = v; }
inline void W16(uint32_t a, uint16_t v) { *reinterpret_cast<uint16_t *>(Mem(a)) = v; }
inline void W32(uint32_t a, uint32_t v) { *reinterpret_cast<uint32_t *>(Mem(a)) = v; }

/* ---------------------------------------------------------------- CSL interface */

/* ctx: +0 group count (only tested != 0), +4 group pointer. group: +0 entry count (even), +4 entry array. entry (8 B):
 * +0 unused word, +4 pointer; entry 2k = stream buffer of port k, entry 2k+1 = its port block. */
constexpr uint32_t CTX_NGROUP = 0x0, CTX_GROUP = 0x4;
constexpr uint32_t GRP_N = 0x0, GRP_ENTRIES = 0x4;
constexpr uint32_t ENTRY_SIZE = 8, ENTRY_PTR = 4;
/* stream buffer: +0 capacity (0x408, not read by hsyn), +4 bytes used, data at +8 */
constexpr uint32_t SBUF_CAP = 0x0, SBUF_USED = 0x4, SBUF_DATA = 0x8;

/* ---------------------------------------------------------------- structures */

/* bank slot (0x1c, blk + 0x398 + 0x1c * slot; written by Load, M2): chunk pointers + the BD SPU base */
struct BankSlot {
    uint32_t head;   /* +0x00 hd + 0x10 */
    uint32_t prog;   /* +0x04 Prog chunk */
    uint32_t sset;   /* +0x08 */
    uint32_t smpl;   /* +0x0c */
    uint32_t vagi;   /* +0x10 */
    uint32_t setb;   /* +0x14 only if hd[0xe] >= 2 */
    uint32_t bd;     /* +0x18 BD SPU base */
};
static_assert(sizeof(BankSlot) == 0x1c, "BankSlot");

/* channel (0x38, blk + 0x18 + 0x38 * i; init f_0770). Three type-B voice lists (links at voice +8 / +0xc): the head
 * words are list[n][0] = first, [1] = last; the pseudo-node P of list n (the "node" whose +8 / +0xc alias the head
 * words) is ch - 8, ch, ch + 8: empty when list[n][0] == P. Roles: list 1 keys down, list 2 note-off
 * under the hold pedal, list 3 released. */
struct Channel {
    uint32_t list1[2];      /* +0x00 P = ch - 8 */
    uint32_t list2[2];      /* +0x08 P = ch */
    uint32_t list3[2];      /* +0x10 P = ch + 8 */
    uint32_t prog;          /* +0x18 current Prog entry (0 = none) */
    uint32_t blk;           /* +0x1c port block */
    uint32_t slot;          /* +0x20 bank slot of the program */
    uint16_t portaTime;     /* +0x24 CC 5 * 20 */
    int16_t bend;           /* +0x26 MIDI pitch bend -0x2000..0x1fff */
    int16_t seBend;         /* +0x28 SE pitch-bend override (f9 02), -1 = unset */
    uint8_t bank;           /* +0x2a CC 0 */
    uint8_t vol;            /* +0x2b CC 7 (v << 7) / 127, init 0x80 */
    uint8_t expr;           /* +0x2c CC 11, init 0x80 */
    uint8_t cc2;            /* +0x2d */
    uint8_t cc1;            /* +0x2e */
    uint8_t seVol;          /* +0x2f SE volume override (f9 00), 0xff = unset */
    int8_t sePan;           /* +0x30 SE pan override (f9 01), 0xff = unset */
    int8_t pan;             /* +0x31 CC 10 (v - 0x40) */
    uint8_t hold;           /* +0x32 CC 0x40 */
    uint8_t portaState;     /* +0x33 0xff off, 0x80 armed, else last released key */
    uint8_t portaCtl;       /* +0x34 CC 0x54 (not written by f_0770) */
    uint8_t nrpnMsb;        /* +0x35 0xff idle */
    uint8_t nrpnLsb;        /* +0x36 */
    uint8_t nrpnData;       /* +0x37 */
};
static_assert(sizeof(Channel) == 0x38, "Channel");
static_assert(offsetof(Channel, seBend) == 0x28 && offsetof(Channel, nrpnData) == 0x37, "Channel fields");

constexpr uint32_t CH_LIST1 = 0x00, CH_LIST2 = 0x08, CH_LIST3 = 0x10;   /* head offsets; pseudo-node = head - 8 */

/* port block (0x558, ctx + 0x11c + 0x558 * k in SYTH's blob; init sceHSyn_Init) */
struct PortBlock {
    uint8_t prio;            /* +0x00 SYTH SetPriority */
    uint8_t maxVoices;       /* +0x01 init 0x30 */
    uint8_t flags;           /* +0x02 bit 0 = SE-stream ("fe") parser */
    uint8_t singleBank;      /* +0x03 1 forces slot 0 (never set: dead, M2) */
    uint32_t w4[4];          /* +0x04 .. +0x13 zeroed; +0xc / +0x10 read as curve selectors (M4 / F1) */
    uint16_t vol;            /* +0x14 port volume, init 0x100 ("blk14" = the address of this field) */
    uint8_t live;            /* +0x16 live voices of the port */
    uint8_t p17;
    Channel ch[16];          /* +0x18 */
    BankSlot slot[16];       /* +0x398 */
};
static_assert(sizeof(PortBlock) == 0x558 && offsetof(PortBlock, vol) == 0x14 && offsetof(PortBlock, ch) == 0x18 &&
              offsetof(PortBlock, slot) == 0x398, "PortBlock");
constexpr uint32_t BLK_VOL = 0x14, BLK_CH = 0x18, BLK_SLOT = 0x398;
constexpr uint32_t CH_SIZE = 0x38, SLOT_SIZE = 0x1c;

/* envelope / LFO step object (0x28, voice + 0x78 / + 0xa0; stepped by f_922c, set up by M3 f_9434) */
struct StepObj {
    uint8_t state;     /* +0x00 0 idle, 1..5 handlers (D_0b5b0) */
    uint8_t enable;    /* +0x01 (u) */
    uint16_t delay;    /* +0x02 delay in ticks (f_9434 tail; LfoStart / LfoRelease) */
    uint16_t counter;  /* +0x04 */
    uint16_t p06;
    int32_t level;     /* +0x08 16.16 */
    int32_t rate;      /* +0x0c */
    int16_t out;       /* +0x10 output (stored as u16 by the common tail) */
    uint16_t p12;
    uint32_t shape;    /* +0x14 LFO shape function (g_base + 0x90f0 / 0x9108 / 0x9128 / 0x9164 / 0x9184 / 0x91f8, f_9434) */
    uint32_t phase;    /* +0x18 */
    uint32_t step;     /* +0x1c phase step */
    uint32_t table;    /* +0x20 wave table (u) */
    uint32_t tableLen; /* +0x24 (u) u16 stores / loads (f_9434); +0x26 untouched */
};
static_assert(sizeof(StepObj) == 0x28, "StepObj");

/* SE-stream ramp object (0x14, voice + 0xc8 / + 0xdc; init f_a724 / f_a89c, stepped by f_a6a0 / f_a810; unreached) */
struct SeRamp {
    uint8_t state;     /* +0x00 1 = running, 2 = done */
    uint8_t p01;
    uint16_t out;      /* +0x02 */
    uint16_t base;     /* +0x04 */
    uint16_t delta;    /* +0x06 */
    uint32_t slope;    /* +0x08 */
    int32_t len;       /* +0x0c */
    int32_t count;     /* +0x10 */
};
static_assert(sizeof(SeRamp) == 0x14, "SeRamp");

/* SE-stream segment stepper (0x38, voice + 0xf0 .. the voice end; init f_abf4, stepped by f_aaac; unreached).
 * Names , all (u). */
struct SeSeg {
    uint32_t state;    /* +0x00 (v + 0xf0) */
    int32_t w04;       /* +0x04 init -1 */
    int32_t acc;       /* +0x08 */
    int32_t base;      /* +0x0c */
    int32_t target;    /* +0x10 */
    int32_t slope;     /* +0x14 */
    int32_t len;       /* +0x18 */
    int32_t rep;       /* +0x1c */
    int32_t flag;      /* +0x20 */
    int32_t pos;       /* +0x24 */
    int32_t reload;    /* +0x28 */
    uint32_t panL;     /* +0x2c (v + 0x11c) init 0x80; f_a9e0 output */
    uint32_t panR;     /* +0x30 (v + 0x120) init 0x80 */
    uint32_t w34;      /* +0x34 (v + 0x124) init 1 */
};
static_assert(sizeof(SeSeg) == 0x38, "SeSeg");

/* voice (0x128; voices[core][idx] at g_base + B_VOICES + core * 0x1bc0 + idx * 0x128). Links: +0 / +4 global lists
 * (layout A: free / reserved / pending / playing), +8 / +0xc channel lists (layout B). */
struct Voice {
    uint32_t next, prev;     /* +0x00 / +0x04 layout A */
    uint32_t cnext, cprev;   /* +0x08 / +0x0c layout B */
    uint16_t flags;          /* +0x10 0x208 at note-on: 8 pending, 0x200 key held, 0x100 off under hold, 1 / 2 / 4 */
    uint16_t env;            /* +0x12 ENVX snapshot (f_509c) */
    uint16_t prio;           /* +0x14 smpl[0xf] + blk[0] */
    uint16_t volTerm;        /* +0x16 */
    uint16_t volScaled;      /* +0x18 */
    uint16_t lastVolL;       /* +0x1a VOLL / VOLR write cache */
    uint16_t lastVolR;       /* +0x1c */
    int16_t panPos;          /* +0x1e */
    int16_t vibDepth[2];     /* +0x20 / +0x22 */
    int16_t vibBase[2];      /* +0x24 / +0x26 */
    int16_t seBend;          /* +0x28 SE bend override, -1 = unset */
    uint16_t portaStep;      /* +0x2a step of the +0x38 countdown */
    uint16_t fixedPitch;     /* +0x2c 0 = none */
    uint16_t p2e;
    int32_t pitchCache;      /* +0x30 */
    int32_t bendTerm;        /* +0x34 */
    int32_t porta;           /* +0x38 portamento offset */
    uint32_t rateRatio;      /* +0x3c */
    uint8_t core;            /* +0x40 */
    uint8_t idx;             /* +0x41 */
    uint8_t progIdx;         /* +0x42 SE voices: the stream's program index (f_7954 / f_8004; matched by f_718c) */
    uint8_t key;             /* +0x43 */
    uint32_t id;             /* +0x44 init 0xff */
    uint8_t vel;             /* +0x48 */
    uint8_t group;           /* +0x49 exclusion group (smpl[0xe]) */
    uint8_t velCurve;        /* +0x4a sset[0] */
    uint8_t endxCount;       /* +0x4b ENDX-hit countdown (f_3f70 = 3) */
    uint8_t panGainL;        /* +0x4c */
    uint8_t panGainR;        /* +0x4d */
    uint8_t panMode;         /* +0x4e */
    int8_t tremDepth[2];     /* +0x4f / +0x50 */
    int8_t tremBase[2];      /* +0x51 / +0x52 */
    uint8_t volPhase;        /* +0x53 */
    uint8_t expr;            /* +0x54 */
    uint8_t seVol;           /* +0x55 init 0xff */
    uint8_t sePan;           /* +0x56 init 0x80 */
    uint8_t kind;            /* +0x57 0 MS / fd, 1 SE-stream, 0xff free */
    uint8_t b58;             /* +0x58 init 0xff (u) */
    uint8_t blkKind;         /* +0x59 blk[3] copy (f_37c4 switch) */
    uint8_t panLow;          /* +0x5a */
    uint8_t p5b;
    uint32_t blk14;          /* +0x5c port block + 0x14 */
    uint32_t ch;             /* +0x60 */
    uint32_t prog;           /* +0x64 Prog entry (SE: parameter record) */
    uint32_t tone;           /* +0x68 */
    uint32_t smpl;           /* +0x6c */
    uint32_t vagi;           /* +0x70 0 = no sample */
    uint32_t bdBase;         /* +0x74 */
    StepObj lfo1;            /* +0x78 */
    StepObj lfo2;            /* +0xa0 */
    SeRamp ramp1;            /* +0xc8 */
    SeRamp ramp2;            /* +0xdc */
    SeSeg seg;               /* +0xf0 */
};
static_assert(sizeof(Voice) == 0x128, "Voice");
static_assert(offsetof(Voice, core) == 0x40 && offsetof(Voice, id) == 0x44 && offsetof(Voice, blk14) == 0x5c &&
              offsetof(Voice, lfo1) == 0x78 && offsetof(Voice, ramp1) == 0xc8 && offsetof(Voice, seg) == 0xf0, "Voice fields");

/* the voice-stat buffer SYTH hands over */
struct VoiceStat {
    uint32_t pend;           /* +0x00 f_37c4 result */
    uint32_t work;           /* +0x04 f_4de0 result */
    uint8_t flag[2][24];     /* +0x08 */
    uint16_t env[2][24];     /* +0x38 */
};
static_assert(sizeof(VoiceStat) == 0x98, "VoiceStat");

/* ---------------------------------------------------------------- module .bss */

constexpr uint32_t B_BSS = 0xb910;
constexpr uint32_t B_FREE = 0xb910, B_TICKLEN = 0xb920, B_MASKC = 0xb928, B_MASKKON = 0xb930, B_KONUS = 0xb938,
                   B_RESERVE = 0xb940, B_PLAYING = 0xb950, B_STATBUF = 0xb960, B_REAP = 0xb964, B_MASKKOFF = 0xb968,
                   B_VMIX = 0xb970, B_RESERVED = 0xb980, B_KONCORE = 0xb990, B_KONIDX = 0xb991, B_SEMAX = 0xb992,
                   B_VOICES = 0xb9a0, B_PENDING = 0xf120, B_LIVESE = 0xf130, B_KONSEC = 0xf134, B_FREECNT = 0xf138,
                   B_VMIXE = 0xf140, B_MONO = 0xf150, B_LIVEMS = 0xf151;
constexpr uint32_t D_SEED = 0xb8b0;           /* .data: f_9b80 LCG seed, 0x02cee9d7 at load (ModuleStart writes it) */
constexpr uint32_t D_LOGRING = 0xb8c0;        /* .data: sd log ring pointer, 0 at load; written only by SetLogRing (f_9bb0) */
/* The phantom image has no .data: D_0b8b0 is the only initialised word the module stores to (D_0b8c0, the log ring, is
 * 0 and never set). Everything else in .data 0xb650..0xb910 is read-only and must be a constant in its cluster:
 * D_0b670 (byte 0), D_0b678 effect-mode table {0..10, 0 x5} (SetEffectAttr), D_0b6c0 (byte 0x10), D_0b6d0..D_0b71c
 * shape-fn pointers (f_89d0 .. f_903c), D_0b720 s16 wave table (to 0xb8ac), D_0b8d0.. word table. */
constexpr uint32_t VOICE_SIZE = 0x128, CORE_STRIDE = 0x1bc0;

struct ListHead { uint32_t first, last; };   /* layout A head: empty = both words hold the head's own address */
struct Mix2 { uint32_t l, r; };

struct Bss {
    ListHead free[2];        /* 0xb910 the allocator (M3 f_6b8c) pops the tail; kills push at the front */
    uint32_t tickLen;        /* 0xb920 usec * 256 / 1000 */
    uint32_t p924;
    uint32_t maskC[2];       /* 0xb928 third per-core mask (u) */
    uint32_t maskKon[2];     /* 0xb930 pending KON (flushed by f_37c4 after each core's voices) */
    uint32_t konUs;          /* 0xb938 last KON stamp, microseconds (f_3f70) */
    uint32_t p93c;
    uint32_t reserve[2];     /* 0xb940 fssd's reserve mask per core */
    uint32_t p948[2];
    ListHead playing[2];     /* 0xb950 started voices */
    uint32_t statBuf;        /* 0xb960 voice-stat buffer (IOP address, 0 = unset) */
    uint32_t reapPending;    /* 0xb964 1 = f_4de0 processes ENVX this tick (cleared by note-on when the pool is empty) */
    uint32_t maskKoff[2];    /* 0xb968 pending KOFF (flushed at the start of f_37c4) */
    Mix2 vmix[2];            /* 0xb970 VMIXL / VMIXR shadow per core */
    ListHead reserved[2];    /* 0xb980 */
    uint8_t konCore;         /* 0xb990 last KON core (no reader) */
    uint8_t konIdx;          /* 0xb991 last KON voice; 0xff = KON guard idle */
    uint8_t seMaxVoices;     /* 0xb992 */
    uint8_t p993[13];
    Voice voices[2][24];     /* 0xb9a0 */
    ListHead pending[2];     /* 0xf120 note-on voices waiting for KON */
    uint8_t liveSe;          /* 0xf130 live SE-stream voices */
    uint8_t p131[3];
    uint32_t konSec;         /* 0xf134 last KON stamp, seconds */
    uint8_t freeCnt[2];      /* 0xf138 */
    uint8_t p13a[6];
    Mix2 vmixE[2];           /* 0xf140 VMIXEL / VMIXER shadow per core */
    uint8_t mono;            /* 0xf150 SetOutputMode(0) -> 1 */
    uint8_t liveMs;          /* 0xf151 live MS voices */
};
#define HSYN_BSS_AT(f, off) static_assert(offsetof(Bss, f) == (off) - B_BSS, #f)
HSYN_BSS_AT(tickLen, B_TICKLEN); HSYN_BSS_AT(maskC, B_MASKC); HSYN_BSS_AT(maskKon, B_MASKKON); HSYN_BSS_AT(konUs, B_KONUS);
HSYN_BSS_AT(reserve, B_RESERVE); HSYN_BSS_AT(playing, B_PLAYING); HSYN_BSS_AT(statBuf, B_STATBUF); HSYN_BSS_AT(reapPending, B_REAP);
HSYN_BSS_AT(maskKoff, B_MASKKOFF); HSYN_BSS_AT(vmix, B_VMIX); HSYN_BSS_AT(reserved, B_RESERVED); HSYN_BSS_AT(konCore, B_KONCORE);
HSYN_BSS_AT(konIdx, B_KONIDX); HSYN_BSS_AT(seMaxVoices, B_SEMAX); HSYN_BSS_AT(voices, B_VOICES); HSYN_BSS_AT(pending, B_PENDING);
HSYN_BSS_AT(liveSe, B_LIVESE); HSYN_BSS_AT(konSec, B_KONSEC); HSYN_BSS_AT(freeCnt, B_FREECNT); HSYN_BSS_AT(vmixE, B_VMIXE);
HSYN_BSS_AT(mono, B_MONO); HSYN_BSS_AT(liveMs, B_LIVEMS);
#undef HSYN_BSS_AT

/* ---------------------------------------------------------------- views and addresses */

inline Bss &S() { return *reinterpret_cast<Bss *>(Mem(g_base + B_BSS)); }
inline Voice *V(uint32_t a) { return reinterpret_cast<Voice *>(Mem(a)); }
inline Channel *Ch(uint32_t a) { return reinterpret_cast<Channel *>(Mem(a)); }
inline PortBlock *Blk(uint32_t a) { return reinterpret_cast<PortBlock *>(Mem(a)); }
inline BankSlot *Slot(uint32_t a) { return reinterpret_cast<BankSlot *>(Mem(a)); }
inline VoiceStat *Stat(uint32_t a) { return reinterpret_cast<VoiceStat *>(Mem(a)); }

/* IOP addresses of the list heads (what the links store) and of the voices */
inline uint32_t FreeHead(uint32_t c) { return g_base + B_FREE + 8 * c; }
inline uint32_t ReservedHead(uint32_t c) { return g_base + B_RESERVED + 8 * c; }
inline uint32_t PlayingHead(uint32_t c) { return g_base + B_PLAYING + 8 * c; }
inline uint32_t PendingHead(uint32_t c) { return g_base + B_PENDING + 8 * c; }
inline uint32_t VoiceAddr(uint32_t c, uint32_t i) { return g_base + B_VOICES + c * CORE_STRIDE + i * VOICE_SIZE; }
inline uint32_t ChannelAddr(uint32_t blk, uint32_t i) { return blk + BLK_CH + CH_SIZE * i; }
inline uint32_t SlotAddr(uint32_t blk, uint32_t s) { return blk + BLK_SLOT + SLOT_SIZE * s; }

/* voice-stat bytes of voice (core, idx); only valid when S().statBuf != 0 (every writer tests it) */
inline uint8_t &StatFlag(uint32_t c, uint32_t i) { return Stat(S().statBuf)->flag[c][i]; }
inline uint16_t &StatEnv(uint32_t c, uint32_t i) { return Stat(S().statBuf)->env[c][i]; }

/* ---------------------------------------------------------------- list helpers */

/* layout A (next +0, prev +4); a head h is a node too (h[0] first, h[1] last) */
inline void ListA_Init(uint32_t h) { Lw(h + 4) = h; Lw(h) = h; }
inline void ListA_PushFront(uint32_t n, uint32_t h)   /* f_370c */
{
    Lw(n + 4) = h;
    uint32_t f = Lw(h);
    Lw(n) = f;
    Lw(h) = n;
    Lw(f + 4) = n;
}
inline void ListA_PushBack(uint32_t n, uint32_t h)    /* f_3728 */
{
    Lw(n) = h;
    uint32_t l = Lw(h + 4);
    Lw(n + 4) = l;
    Lw(h + 4) = n;
    Lw(l) = n;
}
inline void ListA_Unlink(uint32_t n)                  /* f_3744 (n's own links stay) */
{
    uint32_t nx = Lw(n), pv = Lw(n + 4);
    Lw(nx + 4) = pv;
    pv = Lw(n + 4);
    nx = Lw(n);
    Lw(pv) = nx;
}
/* layout B (next +8, prev +0xc); h = the head words, its pseudo-node is h - 8 */
inline void ListB_PushFront(uint32_t n, uint32_t h)   /* f_3764 */
{
    Lw(n + 0xc) = h - 8;
    uint32_t f = Lw(h);
    Lw(n + 8) = f;
    Lw(h) = n;
    Lw(f + 0xc) = n;
}
inline void ListB_PushBack(uint32_t n, uint32_t h)    /* f_3784: dead in the original (no caller) */
{
    Lw(n + 8) = h - 8;
    uint32_t l = Lw(h + 4);
    Lw(n + 0xc) = l;
    Lw(h + 4) = n;
    Lw(l + 8) = n;
}
inline void ListB_Unlink(uint32_t n)                  /* f_37a4 */
{
    uint32_t nx = Lw(n + 8), pv = Lw(n + 0xc);
    Lw(nx + 0xc) = pv;
    pv = Lw(n + 0xc);
    nx = Lw(n + 8);
    Lw(pv + 8) = nx;
}

/* ---------------------------------------------------------------- module functions by cluster
 * (addresses are IOP addresses; "v" = voice, "ch" = channel, "blk" = port block, "blk14" = blk + 0x14).
 * All native since H5R (hsyn_stub.cpp keeps only the unit-test hooks). */

/* M1 / H1 (hsyn.cpp) */
void ChannelInit(uint32_t ch, uint32_t blk);                                   /* f_0770 */
void PoolInit();                                                               /* f_3510 */
void StreamDispatch(uint32_t entries, int32_t count);                          /* f_174c */
int PortLookup(uint32_t ctx, uint32_t port, uint32_t *blk, uint32_t *blk14);   /* f_1b84: 1 = ok */
int KonGuard();                                                                /* f_5c30: 1 = >= 21 us since the KON stamp */
int Ramp1Init(uint32_t v);                                                     /* f_a724 (returns 0) */
int Ramp2Init(uint32_t v);                                                     /* f_a89c */
int SegInit(uint32_t v);                                                       /* f_abf4 */
/* M1 / H2 */
uint32_t PendingSweep();                                                       /* f_37c4: KOFF / KON flush; -> pend */
uint32_t PlayingSweep();                                                       /* f_4de0: ENVX / kill / update; -> work */
int Reap();                                                                    /* f_509c: 1 = a voice was killed */
void Kill(uint32_t v);                                                         /* f_6314 */
int StealChannel(uint32_t blk14, uint32_t thr);                                /* f_6458: 1 = stolen */
void AllSoundOffCh(uint32_t ch);                                               /* f_6234 */
int16_t StepObjStep(uint32_t obj);                                             /* f_922c + handlers f_92cc .. f_93cc */
void SeRamp1Step(uint32_t obj);                                                /* f_a6a0 (obj = v + 0xc8) */
int SeRamp2Step(uint32_t v);                                                   /* f_a810 (v + 0xdc) */
void SePanLaw(uint32_t out, uint32_t flag, int32_t pan);                       /* f_a9e0 */
void SeSegStep(uint32_t v);                                                    /* f_aaac */
/* M2 / H3 (hsyn_bank.cpp, hsyn_query.cpp) */
void NrpnEffectType(uint8_t sel, uint8_t val);                                 /* f_1e70 */
void NrpnEffectParam(uint8_t sel, uint8_t val);                                /* f_1ee0 */
void SeReleaseAllCh(uint32_t ch0);                                             /* f_7358 (+ f_7338 = ReleaseVoice) */
void SeKillAllCh(uint32_t ch0);                                                /* f_7750 (+ f_782c = Kill) */
/* M3 / H3a (hsyn_note.cpp) */
void NoteOn(uint32_t blk, uint32_t ch, uint32_t port, uint32_t key, uint32_t vel, uint32_t id);   /* f_65dc (blk, not blk14) */
void VoiceSetup(uint32_t v);                                                   /* f_3bb4 */
uint32_t AllocVoice(uint32_t sel);                                             /* f_6b8c: sel = Smpl +0x29; 0 = none */
uint32_t StealPlaying(uint32_t prio, uint32_t sel);                            /* f_6c30 */
/* f_6c30's body with the candidate kind filter of M6's twin f_84d8 (STEAL_MS_ONLY = f_6c30 itself) */
enum { STEAL_ANY = 0, STEAL_SE_ONLY = 1, STEAL_NOT_SE = 2, STEAL_MS_ONLY = 3 };
uint32_t StealPlayingKind(uint32_t prio, uint32_t sel, int filter);
void NoteOff(uint32_t ch, uint32_t key, uint32_t id);                          /* f_5cd4 (id 0xff = any) */
void ReleaseVoice(uint32_t v);                                                 /* f_5e80 */
void HoldPedal(uint32_t ch, uint32_t val);                                     /* f_5fb4 */
void AllNotesOff(uint32_t ch);                                                 /* f_60f4 */
uint32_t VelCurve(uint32_t vel, uint32_t id, uint32_t tbl, int32_t n);         /* f_9064 */
void LfoSetup(uint32_t obj, uint32_t prog, uint32_t tbl, int32_t n, uint32_t mode, uint32_t a14, uint32_t a18,
              uint32_t tick, uint32_t which);                                  /* f_9434 */
void LfoStart(uint32_t obj);                                                   /* f_93ec */
void LfoRelease(uint32_t obj);                                                 /* f_9778 */
int16_t ClampAdd16(int32_t a0, int32_t a1, int32_t a2);                        /* f_3988 (M4's, pure) */
int8_t ClampAdd8(int32_t a0, int32_t a1, int32_t a2);                          /* f_39cc */
/* M3 / H3b (hsyn_note.cpp; REV, unit tests only) */
uint32_t StealPending(uint32_t prio, uint32_t sel);                            /* f_6e34 */
uint32_t StealPendingKind(uint32_t prio, uint32_t sel, bool msOnly);           /* f_6e34 (msOnly) / M6 f_875c (not) */
void KillGroup(uint32_t ch, uint32_t group);                                   /* f_6f94 */
void Portamento(uint32_t v);                                                   /* f_5248 */
void LfoSetupSe(uint32_t obj, uint32_t prog, uint32_t tbl, int32_t n, uint32_t mode, uint32_t a14, uint32_t a18,
                uint32_t tick, uint32_t which);                                /* f_9830 (+ tail f_9990) */
int32_t Rand();                                                                /* f_9b80 */
/* the shape function of a step object (the original's jalr obj[0x14](obj, carry); carry = the phase wrapped) */
int32_t LfoShapeEval(uint32_t obj, uint32_t carry);
/* unit tests (hsyn_stub.cpp, StubSetShape): while set, LfoShapeEval returns the mock's value instead */
extern bool g_shapeMock;
int32_t ShapeMockEval(uint32_t obj, uint32_t carry);
/* M4 / H4a (hsyn_voice.cpp; called from H2's sweeps) */
void VoiceStart(uint32_t v);                                                   /* f_3f70 */
void VoiceStartSe(uint32_t v);                                                 /* f_5520 */
/* M4 / H4b (hsyn_voice.cpp; REV: the SE-stream voice setup, called by M6's f_8004) */
void VoiceSetupSe(uint32_t v);                                                 /* f_52d0 */
void SetTremoloDepthSe(uint32_t v, uint32_t cc2);                              /* f_59a0 */
void SetVibratoDepthSe(uint32_t v, uint32_t cc1);                              /* f_5a44 */
void SetVibratoDepthSe16(uint32_t v, uint32_t cc1);                            /* 0x5af0 (dead, no caller) */
void PanBaseSe(uint32_t v);                                                    /* f_5b9c */
void PitchWrite(uint32_t v);                                                   /* f_4408 */
void VolumeWrite(uint32_t v);                                                  /* f_4664 */
/* M4 / H4a, called by VoiceSetup / VelCurve / the M5 controllers (they report to M4Log while g_sdMock is set) */
int32_t VelDepth(uint32_t v, int32_t depth, uint32_t p1, uint32_t p2);         /* f_48a4 */
void SetTremoloDepth(uint32_t v, uint32_t cc2);                                /* f_4908 */
void SetVibratoDepth(uint32_t v, uint32_t cc1);                                /* f_49b0 */
uint32_t CrossFade(uint32_t v);                                                /* f_4c88 */
void PanBase(uint32_t v, int32_t kf);                                          /* f_4a60 */
void PanLaw(uint32_t v, int32_t pan);                                          /* f_4b34 (s8 pan) */
void UpdateVolume(uint32_t v);                                                 /* f_486c */
void SetBend(uint32_t v, int32_t bend);                                        /* f_4610 (s16 bend) */
uint32_t VelShape(uint32_t sel, uint32_t x);                                   /* D_0b6f8[sel](x), sel 0..10 (10 = the f_9064 quirk) */
int32_t VelCurveDepth(uint32_t vel, uint32_t p1, uint32_t p2, uint32_t tbl, int32_t n);   /* f_8edc */
uint32_t KeyFollowField(uint32_t a0, uint32_t depth, int32_t diff, uint32_t mask, uint32_t shift);   /* f_3a0c */
uint32_t Adsr1(uint32_t key, uint32_t smpl);                                   /* f_3a94 */
uint32_t Adsr2(uint32_t key, uint32_t smpl);                                   /* f_3b38 */
void M4Log(uint32_t fn, uint32_t v, int32_t a, int32_t b = 0, int32_t c = 0);   /* hsyn_stub.cpp: the setter log */
/* M6 / H6 (hsyn_se.cpp; REV: the SE-stream ("fe") voice engine, called only by H5R's ParseSe).
 * blk = port block; the SE engine uses channel 0 only (ch0 = blk + 0x18). */
int SeNoteOn(uint32_t blk, uint32_t port, uint32_t bank, uint32_t prog, uint32_t key, uint32_t vel, int32_t pan,
             uint32_t bend, uint32_t id);                                      /* f_840c (+ f_7954 / f_8004): 0 / -1 */
void SeNoteOff(uint32_t blk, uint32_t bank, uint32_t prog, uint32_t key, uint32_t id);   /* f_718c (id 0xff = any) */
void SeReleaseById(uint32_t blk, uint32_t id);                                 /* f_75b4 -> f_7468 */
void SeReleaseMasked(uint32_t blk, uint32_t x, uint32_t y);                    /* f_7730 -> f_75d4: (id & y) == (x & y) */
uint32_t SeKillOne(uint32_t v, uint32_t flag);                                 /* f_70dc: returns the old v.cnext */
int SeStealChannel(uint32_t blk14, uint32_t prio);                             /* f_784c: 1 = stolen */
uint32_t StealPlayingSe(uint32_t prio, uint32_t sel, uint32_t filt, uint32_t seOnly);   /* f_84d8 */
uint32_t SeStealPlaying(uint32_t prio, uint32_t sel);                          /* f_8700 */
uint32_t StealPendingSe(uint32_t prio, uint32_t sel);                          /* f_875c */
int SeGroupLimit(uint32_t ch0, uint32_t group, uint32_t limit);                /* f_88ac: 1 = the group was killed */
/* M6 / H6S (hsyn_se.cpp): the f_ac40 pan-segment switch on v + 0xf0 (c1, c2 bytes); 0, -1 for kinds 6..11 / >= 14 */
int SeSegCmd(uint32_t v, uint32_t kind, uint32_t val, uint32_t c1, uint32_t c2);   /* f_ac40 .. f_af74 */
/* M5 / H5 */
void ParseMs(uint32_t blk, uint32_t port, uint32_t p, uint32_t end);           /* f_15dc */
void ParseSe(uint32_t blk, uint32_t port, uint32_t p, uint32_t end);           /* f_278c (H5R, hsyn_fe.cpp; REV) */
void ResetControllers(uint32_t ch);                                            /* f_10c0 */

}  // namespace hsyn
