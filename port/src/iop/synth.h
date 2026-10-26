/* synth: FromSoftware's synth front ("fssdsyth", IRX.BIN@052800, crc 054c5145, 77 named functions) decompiled onto the
 * native IOP kernel iopk + native sd. fssd module 4:
 * one RPC callback (the 50-command ladder), the 240 Hz "ATick" thread (prio 0x17) woken by hard timer 1 through IntFunc,
 * a few direct sd calls (SPU init, master volume, VoiceTrans, the SetReverb MVOLX poll) and the fronts of the two Sony
 * libraries it drives: modhsyn (sceHSyn_*: voices, SE events, banks) and modmidi (sceMidi_*: the SQ sequencer).
 *
 * Memory model: the whole image range keeps the original layout in iopk::Ram() (base + the original offsets): the CSL
 * blob g_iemData (+0x2bb0, 0xa0d0 bytes: modmidi ctx + 4 midi port structs, hsyn ctx at +0x3520 + 16 port blocks, 16
 * stream buffers at +0x8bc0), the EE DMA packet buffer (+0xcc80, 0x4000), the voice-stat buffer hsyn fills (+0x11a00,
 * the 40810200 reply) and the SE stream buffer the EE DMAs into (+0x11ac0). hsyn / modmidi get IOP addresses into the
 * blob as in the original, so their pointers stay byte-identical to the hybrid image. The plain scalars (.data / .bss
 * words no other module reads) are host statics, zero at module start like the zeroed image (_fdata = -1 is set there).
 *
 * hsyn and modmidi are not native yet: their calls go through SetHsyn / SetMidi (unset = not called, returns 0).
 * sceHSyn_Init / SetReservVoice stay on fssd::SetHsyn (SYTH's hsynInit calls fssd::HsynInit as the original).
 *
 * State of the port (NI6b S1 + the S2 fronts + the asm-only batch): all 77 functions native. SetCslSeStream 0x159c and
 * GeEeVolumePack 0x16f8 are unreached in Versus (unit-tested: UnitTest); SetCslSeStream calls the native modsein
 * (platform/snd/iop/sein.h) directly. */
#pragma once
#include <stdint.h>
#include "../../platform/snd/iop/iopk.h"

namespace synth {

constexpr uint32_t IMAGE_SIZE = 0x11ec0;
constexpr int MODULE = 4;   /* fssd module number (fno >> 28) */
/* image offsets (IOP address = Base() + offset) */
constexpr uint32_t OFS_IEM = 0x2bb0;         /* g_iemData, 0xa0d0 bytes; the modmidi ctx is its start */
constexpr uint32_t OFS_HSYN_CTX = 0x3520;    /* the hsyn CSL ctx (g_iemData + 0x970) */
constexpr uint32_t OFS_PACKET = 0xcc80;      /* s_ucDmaPacket, 0x4000: EE DMA target (RPC 0xa00 returns it) */
constexpr uint32_t OFS_VOICESTAT = 0x11a00;  /* s_hsvsVoiceStat: the 0x98-byte 40810200 reply hsyn fills */
constexpr uint32_t OFS_SESTREAM = 0x11ac0;   /* s_ucSeStreamBuf, 0x400: EE DMA target of the SE byte stream */
constexpr uint32_t VOICESTAT_SIZE = 0x98;

/* modhsyn entry points SYTH calls (ctx = Base() + OFS_HSYN_CTX; all pointers are IOP addresses); u = Hsyn::user */
struct Hsyn {
    void *user;
    int (*ATick)(void *u, uint32_t ctx);
    int (*Load)(void *u, uint32_t ctx, uint32_t port, uint32_t bd_spu, uint32_t hd, uint32_t slot);
    int (*VoiceTrans)(void *u, int ch, uint32_t a1, uint32_t a2, uint32_t a3);   /* RPC 0x10000 words, unreached */
    int (*SetEffectAttr)(void *u, uint32_t rec);   /* the 0x18-byte rev_tbl record (the RPC receive buffer) */
    int (*SetVolume)(void *u, uint32_t ctx, uint32_t port, uint16_t vol);
    int (*AllNoteOff)(void *u, uint32_t ctx, uint32_t port);
    int (*ResetAllControler)(void *u, uint32_t ctx, uint32_t port);
    int (*SetVoiceStatBuffer)(void *u, uint32_t buf);
    int (*GetChStat)(void *u, uint32_t ctx, uint32_t port, uint32_t buf);
    int (*SetOutputMode)(void *u, int mode);
    int (*SESetMaxVoices)(void *u, int n);
    int (*SERetrieveVoiceNumberByID)(void *u, uint32_t ctx, uint32_t port, uint32_t id, uint32_t buf, int n);
    int (*MSGetVoiceStateByID)(void *u, uint32_t ctx, uint32_t a1, uint32_t a2, uint32_t buf, int n);
};
void SetHsyn(const Hsyn *h);   /* copied; 0 = none */

/* modmidi entry points (ctx = Base() + OFS_IEM); u = Midi::user */
struct Midi {
    void *user;
    int (*Init)(void *u, uint32_t ctx, uint32_t usec);
    int (*ATick)(void *u, uint32_t ctx);
    int (*Load)(void *u, uint32_t ctx, uint32_t port);
    int (*SelectMidi)(void *u, uint32_t ctx, uint32_t port, uint32_t n);
    int (*PlaySwitch)(void *u, uint32_t ctx, uint32_t port, uint32_t on);
    int (*SetLocation)(void *u, uint32_t ctx, uint32_t port, uint32_t loc);
    int (*GetAbsoluteTempo)(void *u, uint32_t ctx, uint32_t port);
    int (*SetRelativeTempo)(void *u, uint32_t ctx, uint32_t port, uint16_t tempo);
};
void SetMidi(const Midi *m);   /* copied; 0 = none */

int ModuleStart(const iopk::ModuleArgs *m);   /* `start` (0xa54): fsiopsdRegister(4, RpcCallBack), returns 0 */
uint32_t Base();                              /* image base (0 = not started) */

/* modmidi channel-message filter n (chMsgCB0..3, 0x1ff4 + 0x5c * n). midiInit stores their IOP addresses
 * (Base() + 0x1ff4 + 0x5c * n) in the midi port structs (+0x34) as the original; a native modmidi maps such an address
 * to ChMsgCB((addr - Base() - 0x1ff4) / 0x5c, msg). */
uint32_t ChMsgCB(int n, uint32_t msg);

/* test view (selftest summary; no behaviour) */
struct View {
    int tick_tid, timer_id, state;
    uint32_t clk;          /* the timer period IntFunc returns (USec2SysClock(4167) = 153,612) */
    uint32_t ticks;        /* ATick passes (wake-ups served) */
    uint32_t wakes;        /* IntFunc calls that woke the thread */
    int is_init, is_start;
};
void GetView(View *v);

/* unit tests (snd-selftest, before iopk::Reset; scratch at IOP 0x100000; module statics saved / restored; failures
 * printed with the prefix "synth-unit:", the number returned): FlushSe records / bad byte / overflow / the 0x408 room
 * edge, SetCslSeStream (incl. the shared group pointer quirk), GeEeVolumePack scaling and slot cap */
int UnitTest();
/* MakeMonoHd against a bounds-checked host walk of a captured HD (every centred pan, nothing else), GetHdAdsr per prog
 * against the walk's last sample, SetHdAdsr read back */
int HdTest(const uint8_t *hd, uint32_t n);

}  // namespace synth
