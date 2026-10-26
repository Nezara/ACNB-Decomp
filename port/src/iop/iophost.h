/* iophost: the ACNB side of the native IOP (phase 3 step 5 NI5b): binds iopk + native sd + the native mixer together
 * (iopk's SPU2 clock hooks drive the mixer, the mixer's DMA-end / IRQ events go to sd::DmaEnd + iopk::RaiseIrq, the
 * AutoDMA input pulls from iopk::Ram()) and maps the IRX names the EE loads onto native module starts (phantom images).
 * Used by --snd-selftest now and by the game wiring later (NI5c+). Modules without a native port start as an empty
 * phantom image (or with a stand-in start function the caller supplies). */
#pragma once
#include <stdint.h>
#include "../../platform/snd/iop/iopk.h"
#include "../../platform/snd/sndhost.h"

struct SndMixer;
namespace synth { struct Hsyn; struct Midi; }
namespace fssd { struct Hsyn; }

namespace iophost {

/* sd register-write observer (after each SPU2 register store sd makes; reg = SPU2 offset 0..0x7fe) */
typedef void (*RegTrace)(void *u, uint32_t reg, uint16_t val);

/* power-on: iopk::Reset, the mixer bound to iopk's clock and to sd (sd is attached when LIBSD starts: its zero page
 * is in the LIBSD image). ee = iopk's EE-side hooks (ee_write / ee_has_server / ee_rpc_posted; the spu_* fields are
 * replaced). */
void Init(SndMixer *m, const iopk::Hooks *ee, RegTrace trace, void *trace_user);

/* sd register-read observer (after each SPU2 register load sd makes; val = the value read). 0 = none; kept across Init. */
void SetReadTrace(RegTrace fn, void *user);

/* sd register-write observer BEFORE the store (the mixer still holds the old voice / mask state; the selftest's
 * voice-index L2 formats the hybrid's KON / KOFF / VP / VMIX trace lines from it). 0 = none; kept across Init. */
void SetPreWriteTrace(RegTrace fn, void *user);

/* mixer output observer: every frame the mixer renders (interleaved L / R s16, n frames per call; the selftest's L4
 * WAV). 0 = none (the output is discarded); kept across Init. */
typedef void (*MixTap)(void *u, const int16_t *lr, int frames);
void SetMixTap(MixTap fn, void *user);

/* sd DMA-start observer (after each transfer sd starts): core, IOP address, bytes, adma = 1 for an AutoDMA span (the
 * mixer pulls it from iopk::Ram() while mixing), 0 for a sample-memory DMA; chcr = the CHCR value that started it (bit
 * 0: IOP -> SPU), data = the IOP RAM bytes (0 for AutoDMA). 0 = none; kept across Init. */
typedef void (*DmaTrace)(void *u, int core, uint32_t madr, uint32_t bytes, int adma, uint32_t chcr, const void *data);
void SetDmaTrace(DmaTrace fn, void *user);

/* AutoDMA pull observer (after each block the mixer pulls from iopk::Ram(): data = the 1024-byte block as the mixer
 * got it, n bytes copied, the rest zero). 0 = none; kept across Init. */
typedef void (*AdmaTrace)(void *u, int core, uint32_t madr, const void *data, uint32_t n);
void SetAdmaTrace(AdmaTrace fn, void *user);

/* the native start function of an IRX by its module name (iopk::IrxInfo::name), 0 = not native; *image_size = its
 * image size. "Sound_Device_Library" = sd (ResetModuleState + Attach + ModuleStart). */
iopk::ModuleStartFn NativeStart(const char *name, uint32_t *image_size);

/* the native modhsyn entry points (platform/snd/iop/hsyn, NI6b: Init / SetReservVoice for fssd, ATick / SetVolume /
 * AllNoteOff / ResetAllControler / SetVoiceStatBuffer for SYTH) into the interface structs (either may be 0); the
 * entries of exports not native yet are left as they are (the caller's stand-ins). The "SPU2_Synthesizer_Module" start
 * is native (hsyn::ModuleStart). */
void HsynNative(synth::Hsyn *sh, fssd::Hsyn *fh);

/* the native modmidi (platform/snd/iop/midi, NI7 W1) into SYTH's interface struct, and its channel-message callback
 * resolver (the port structs' +0x34 = SYTH's chMsgCB_n addresses). The "Midi_Sequencer_Module" start is native. */
void MidiNative(synth::Midi *m);

/* the module name of a known ACNB IRX by its image size (the hybrid's load sizes), 0 = unknown */
const char *NameBySize(uint32_t image_size);

/* the native hsyn / midi interfaces into fssd / SYTH (static structs: HsynNative + MidiNative + fssd::SetHsyn /
 * synth::SetHsyn / synth::SetMidi) - the product wiring; --snd-selftest installs its checking wrappers instead */
void WireNative();

/* ACNB's sndhost binding (NI9): init = Init(m, ee, 0, 0) + WireNative, resolve = NativeStart, mix tap = SetMixTap,
 * unnamed IRX = NameBySize (fssd / fssdads / fssdsyth / fssdevag have no module name) */
const sndhost::Binding *HostBinding();

}  // namespace iophost
