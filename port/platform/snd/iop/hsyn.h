/* hsyn: Sony's IOP synthesizer library (modhsyn "SPU2_Synthesizer_Module" v0105, IRX.BIN@03a000, crc 8ae08f7c, image
 * 0xf152) decompiled into native C++ over iopk + native sd (phase 3 step 5 NI6b; ..
 * , file-level part Part 3). Generic (no game addresses): the caller (FromSoftware's SYTH /
 * fssd) passes the CSL ctx and the buffers as IOP addresses, as to the original's exports. No threads, semaphores or
 * timers: everything runs from ATick (SYTH's 240 Hz thread) and the exports. One instance per process.
 *
 * State of the port (H1 + H2 + H3 of NI6b): module start, Init, the voice pool, SetReservVoice, the ATick shell (time
 * guard, VMIX shadow upkeep, the per-port stream dispatch, the voice-stat words), the per-tick pending / playing sweeps
 * (KOFF / KON flush, ENVX readback, kills), reap / kill / steal, the voice step objects, Load (HD chunk checks), the
 * effect setup, the sd wrapper layer with its log ring, and every remaining export (the SE / MS voice queries included)
 * are native, and note on / off (H3a: HD walk, allocation, steals, release, hold pedal, voice setup), and the MS input
 * (H5: the stream parser, channel messages, controllers, program change, SE f9 / fd events), and the voice register
 * writer (H4a: voice start, pitch / volume writers, the pan / velocity / cross-fade / ADSR helpers), and the REV half
 * (H3b / H4b SE setup, H6 SE-stream voice engine, H5R "fe" parser hsyn_fe.cpp + the f_ac40 switch; unreached in
 * Versus, unit-tested): the whole module is native, hsyn_stub.cpp keeps only unit-test hooks. */
#pragma once
#include <stdint.h>
#include "iopk.h"

namespace hsyn {

constexpr uint32_t IMAGE_SIZE = 0xf152;

int ModuleStart(const iopk::ModuleArgs *m);   /* f_0000 without RegisterLibraryEntries (the exports are these functions) */
uint32_t Base();                              /* image base (0 = not started) */

/* exports (index in the original's table) */
int Init(uint32_t ctx, uint32_t usec);                         /* 4  sceHSyn_Init: 0 / -1 */
int ATick(uint32_t ctx);                                       /* 5  sceHSyn_ATick: 0 */
int Load(uint32_t ctx, uint32_t port, uint32_t bd_spu, uint32_t hd, uint32_t slot);   /* 6  0 / -1 (bd_spu or hd 0 = unload) */
int VoiceTrans(uint32_t ch, uint32_t iop, uint32_t spu, uint32_t size);              /* 7  (unreached) 0 / -1 */
int SetReservVoice(const uint32_t mask[2]);                    /* 8  (the caller's mask words) */
int SetEffectAttr(uint32_t rec);                               /* 9  rec = the 0x18-byte reverb record: 0 */
int SetVolume(uint32_t ctx, uint32_t port, uint32_t vol);      /* 10 stored as u16; 0 / -1 */
int GetVolume(uint32_t ctx, uint32_t port);                    /* 11 (unreached) 0 on a bad port */
int AllNoteOff(uint32_t ctx, uint32_t port);                   /* 12 */
int AllSoundOff(uint32_t ctx, uint32_t port);                  /* 13 (unreached) */
int ResetAllControler(uint32_t ctx, uint32_t port);            /* 14 */
int SetVoiceStatBuffer(uint32_t buf);                          /* 15 */
int SetLogRing(uint32_t ring);                                 /* 16 f_9bb0 (no importer): sd call log ring, 0 = off */
int GetChStat(uint32_t ctx, uint32_t port, uint32_t buf);      /* 17 (unreached) 16 bytes: bit 0/1/2 = list 1/2/3 non-empty */
int SetOutputMode(int mode);                                   /* 18 0 = mono, 1 = stereo, else -1 */
int SESetMaxVoices(uint32_t n);                                /* 19 (n & 0xff) <= 0x30 */
int SEReleaseAll(uint32_t ctx, uint32_t port);                 /* 20 f_2f84 (no importer) SE port only */
int SEKillAll(uint32_t ctx, uint32_t port);                    /* 21 f_2fdc (no importer) SE port only */
int SERetrieveVoiceNumberByID(uint32_t ctx, uint32_t port, uint32_t id, uint32_t buf, int32_t n);   /* 22 u8 core*24+idx */
int MSGetVoiceStateByID(uint32_t ctx, uint32_t port, uint32_t id, uint32_t buf, int32_t n);         /* 23 u8 stat flags */
int MSGetVoiceEnvByID(uint32_t ctx, uint32_t port, uint32_t id, uint32_t buf, int32_t n);           /* 24 f_33b8 u16 ENVX */
int SEGetVoiceIDs(uint32_t ctx, uint32_t port, uint32_t buf, int32_t n);                            /* 25 f_3148 u32 ids */
int GetReservVoice(uint32_t out[2]);                           /* 26 (unreached) */
int GetOutputMode();                                           /* 27 f_2094 (no importer): mono != 1 */
int ResetBankSlot(uint32_t ctx, uint32_t port, uint32_t slot); /* 28 f_0658 (unreached) */

/* test view (selftests; no behaviour) */
struct View {
    uint8_t freeCnt[2];
    uint8_t konIdx;
    uint32_t reserve[2];
    uint32_t statBuf, tickLen, reapPending;
    uint32_t vmix[2][4];   /* per core: VMIXL, VMIXR, VMIXEL, VMIXER shadow */
};
void GetView(View *v);
/* voice indices of a global list of core c, head -> tail (list 0 free, 1 reserved, 2 pending, 3 playing); returns the
 * count, or -1 if the links are inconsistent (a prev word that does not point back, a foreign node, > 48 steps) */
int ListOrder(int list, int core, uint8_t *idx, int max);
/* unit-test hooks (hsyn_stub.cpp): LfoShapeEval mock calls; shape_carry = the last LfoShapeEval carry argument */
struct StubView {
    uint32_t shape, shape_carry;
    uint32_t m4;   /* the M4 setter log entries */
};
void GetStubView(StubView *v);
/* calls of the native fe SE-stream parser ParseSe (f_278c; REV, never reached in Versus) since ModuleStart */
uint32_t ParseSeCalls();
/* unit tests: LfoShapeEval returns ret (counting calls and the carry in shape / shape_carry) until StubShapeOff */
void StubSetShape(int32_t ret);
void StubShapeOff();
/* the calls into the M4 setters (VelDepth, SetTremoloDepth, SetVibratoDepth, CrossFade, PanBase, PanLaw, UpdateVolume,
 * SetBend, VelShape) while the unit tests' sd mock is installed (fn = module offset of the function; a / b / c = the
 * arguments after v, VelShape: sel, x) */
struct StubCall { uint32_t fn; int32_t a, b, c; uint32_t v; };   /* v = the voice argument */
int GetStubCalls(StubCall *out, int max);   /* count since the last clear (out gets up to max) */
void StubClearCalls();

/* H5: the MS parser's calls into note on / off, the controllers, program change and the SE events, reported right
 * before each call (selftests; fn 0 = off). kind 'N' NoteOn (a key, b vel, c id), 'F' NoteOff (a key, b id), 'C'
 * controller (a cc, b val), 'P' program change (a prog), 'B' pitch bend (a msb, b lsb), 'O' SE f9 (a kind, b v0,
 * c v1; every kind), 'D' SE fd other than kind 0x10 (a kind, b key | 0x80 any-key flag, c id, d v0 | v1 << 8), 'X'
 * the end of a sysex (ch 0); ch / blk = IOP addresses, port = the stream entry index */
struct InEvent { uint32_t kind, port, blk, ch; int32_t a, b, c, d; };
typedef void (*InTraceFn)(void *user, const InEvent *e);
void SetInputTrace(InTraceFn fn, void *user);

/* unit tests of the native part (scratch image in iopk::Ram(); run before iopk::Reset, they leave g_base = 0):
 * failures are printed with the prefix "hsyn-unit:"; returns the number of failures */
int UnitTest();
/* H3a: the HD walk of NoteOn on a recorded HD file (hd = its bytes, n = size): every program x keys {0, 12, 40, 60,
 * 119, 127} x velocities {0, 1, 64, 127} against an independent decoder of on the same bytes (voice count
 * and the (tone, smpl, vagi) of each voice, in order), then AllNotesOff restores the pool. Same scratch image and
 * prefix as UnitTest; *cases / *voices / *multi = the cases tried, voices started, cases with > 1 voice. Returns the
 * number of failures. */
int HdWalkTest(const uint8_t *hd, uint32_t n, int *cases, int *voices, int *multi);

}  // namespace hsyn
