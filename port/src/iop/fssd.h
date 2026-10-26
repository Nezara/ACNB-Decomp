/* fssd: FromSoftware's IOP sound front ("fssd" v0301, IRX.BIN@04b800, crc d4ebc12f) decompiled onto the native IOP
 * kernel iopk. The module registry + the EE <-> IOP RPC
 * front: one IOP thread serves SID 0x101a; fno = (module << 28) | cmd; module 0 is fssd itself, the game's other IOP
 * modules (3 EVAG, 4 SYTH, 5 ADS, 10 IVAG) register a callback with Register. Also the IOP -> EE call CallRpc (SID
 * 0x102a) and the shared hsyn "reserve voice" mask (48 voices = 2 cores x 24, two words).
 *
 * The original's export table (fsiopsdRegister ... fsiopsdHsynClearReservVoice) becomes these C++ functions; the other
 * native modules call them directly. hsyn (MODHSYN, sceHSyn_*) is not native yet: its two entry points fssd calls go
 * through SetHsyn (unset = not called, Init returns 0) until NI6 plugs the native library in.
 *
 * EE-visible data lives in the phantom image (iopk::Ram() at the module base + the original offsets): the reply buffer
 * g_ucIrrBuf (+0x9f0, ONE buffer for all modules: only word 0 is cleared per call and only the callback's *outsize bytes
 * are copied in, so reply bytes past a short reply are what an earlier, longer reply left - hazard H1, kept), the
 * receive buffer (+0xaf0) and the IOP -> EE send / receive buffer (+0xbf0). Everything else is host statics, zero at
 * module start like the zeroed image. */
#pragma once
#include <stdint.h>
#include "../../platform/snd/iop/iopk.h"

namespace fssd {

constexpr uint32_t IMAGE_SIZE = 0xd60;
constexpr uint32_t OFS_IRR = 0x9f0, OFS_EE2IOP = 0xaf0, OFS_IOP2EE = 0xbf0;   /* 0x100 bytes each */
constexpr uint32_t SID = 0x101a, SID_IOP2EE = 0x102a;

/* module callback (the original's 4-argument form, private to fssd's dispatcher): cmd = fno & 0x0fffffff, buf = the
 * receive buffer, size = the EE's send size; *outsize is 0 on entry; returns the reply storage (*outsize bytes of it are
 * copied into the shared reply buffer) */
typedef void *(*ModuleFunc)(uint32_t cmd, void *buf, int size, int *outsize);

/* hsyn entry points used by fssd (sceHSyn_Init / sceHSyn_SetReservVoice); u = Hsyn::user */
struct Hsyn {
    void *user;
    int (*Init)(void *u, uint32_t ctx, uint32_t a1);            /* args forwarded untouched (SYTH: its context, 0x1047) */
    int (*SetReservVoice)(void *u, const uint32_t mask[2]);   /* mask = fssd's accumulated reserve mask */
};
void SetHsyn(const Hsyn *h);   /* copied; 0 = none */

int ModuleStart(const iopk::ModuleArgs *m);   /* `start` (0x668) without RegisterLibraryEntries: 0 = ok */
uint32_t Base();                              /* image base (0 = not started) */

/* exports */
void Register(int module, ModuleFunc cb);                            /* fsiopsdRegister (0x6c8), no bounds check */
/* fsiopsdCallRpc (0x6e4): w = the 8 words the original's vararg copy loop reads (the 4th argument and the caller's
 * following stack words, used or not - H4: callers pass the words their original left there); wait == 0 -> no-wait
 * mode; returns word 0 of the send / receive buffer read right after the call */
uint32_t CallRpc(int module, int wait, uint32_t cmd, const uint32_t w[8]);
int HsynInit(uint32_t ctx, uint32_t a1);                            /* fsiopsdHsynInit (0x7b0) */
int HsynSetReservVoice(const uint32_t mask[2]);                     /* fsiopsdHsynSetReservVoice (0x7fc) */
void HsynSetReservVoiceFlag(int voice, int on);                     /* fsiopsdHsynSetReservVoiceFlag (0x880) */
void HsynValidateReservVoice();                                     /* fsiopsdHsynValidateReservVoice (0x91c) */
void HsynClearReservVoice();                                        /* fsiopsdHsynClearReservVoice (0x970) */

/* test view: the reserve mask words */
void ReserveMask(uint32_t out[2]);

}  // namespace fssd
