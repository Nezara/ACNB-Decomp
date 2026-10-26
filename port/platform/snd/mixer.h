/* Native sound mixer (phase 3 step 5, NI2): the DSP of a PS2-style sound chip as a plain C API - 2 MB sample memory,
 * 2 cores x 24 PS-ADPCM voices (ADSR, volume sweeps, gaussian / cubic / linear interpolation, noise, pitch modulation),
 * per-core reverb at 24 kHz (39-tap resampler + comb / all-pass network), sound-data input (AutoDMA FIFO), core 0 ->
 * core 1 external input, 48 kHz interleaved s16 stereo out. It is not a register-level emulation: no register file, no
 * bus, no IOP pointers. The math is a literal copy of runtime/spu2.c (the hybrid's reference emulator): same integer
 * ops, tables, clamp points and per-sample order, so a command stream that drives both gives bit-identical output.
 *
 * Generic (no game addresses, no runtime/ headers, 64-bit clean): the producer is a native libsd (sd) layer, or the
 * test-only register adapter of --spu-selftest (spurec_selftest.cpp). Addresses are halfword sample-memory addresses
 * (20 bits); "madr" values of AutoDMA are opaque 32-bit cookies the host's ADMA reader maps to its own memory.
 * Not thread-safe: one control thread (commands, queries and mix) per instance. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SndMixer SndMixer;

#define SND_MIXER_RAM_HALFWORDS 0x100000u   /* 2 MB */
#define SND_MIXER_VOICES 24
#define SND_MIXER_RATE 48000

/* events raised while mixing / on commands (the IOP's IRQ 9 and the SPU2 DMA-done interrupts in the original) */
enum { SND_EV_IRQ = 1,          /* a voice block fetch / port write / DMA / ADMA input crossed the core's IRQ address */
       SND_EV_DMA_DONE = 2 };   /* a sample-memory DMA completed (snd_mixer_service) or an ADMA span was fully pulled */
typedef void (*SndMixerEventFn)(void *user, int ev, int core);
/* AutoDMA input: copy n (<= 1024) bytes of sound data at the host cookie madr into dst (1024 bytes, pre-zeroed:
 * 256 s16 L then 256 s16 R). Called from snd_mixer_mix / snd_mixer_adma_start whenever the core's 2-block FIFO has room. */
typedef void (*SndMixerAdmaFn)(void *user, int core, uint32_t madr, void *dst, uint32_t n);

/* voice parameters (16 bit) */
enum { SND_VP_VOLL, SND_VP_VOLR, SND_VP_PITCH, SND_VP_ADSR1, SND_VP_ADSR2,
       SND_VP_ENVX,              /* envelope level: set = force the level (s16), get = current level */
       SND_VP_VOLXL, SND_VP_VOLXR /* get only: last voice output after the volume (s16 as u16) */ };
/* voice addresses (20-bit halfword) */
enum { SND_VA_SSA, SND_VA_LSA /* setting it marks the loop address as user-set until the next key-on */, SND_VA_NAX };
/* core 24-bit voice masks */
enum { SND_CM_PMON, SND_CM_NON, SND_CM_VMIXL, SND_CM_VMIXEL, SND_CM_VMIXR, SND_CM_VMIXER };
/* core 16-bit parameters */
enum { SND_CP_MVOLL, SND_CP_MVOLR, SND_CP_EVOLL, SND_CP_EVOLR, SND_CP_AVOLL, SND_CP_AVOLR, SND_CP_BVOLL, SND_CP_BVOLR,
       SND_CP_REV_IIR, SND_CP_REV_COMB1, SND_CP_REV_COMB2, SND_CP_REV_COMB3, SND_CP_REV_COMB4, SND_CP_REV_WALL,
       SND_CP_REV_APF1, SND_CP_REV_APF2, SND_CP_REV_IN_L, SND_CP_REV_IN_R,
       SND_CP_MMIX,              /* 12-bit dry / wet routing of voices, external input, sound-data input */
       SND_CP_ATTR,              /* bit 15 enable (0 -> 1 sets STATX bit 7), 8-13 noise clock, 7 effect write,
                                    6 IRQ enable (0 clears the core's pending IRQ bit) */
       SND_CP_ADMAS,             /* AutoDMA status: bit (core + 1) clear = stop the input and drop the FIFO */
       SND_CP_MVOLXL, SND_CP_MVOLXR, /* get only: current master volume level */
       SND_CP_STATX,             /* get only: bit 7 = DMA ready */
       SND_CP_COUNT };
/* core addresses (20-bit halfword) */
enum { SND_CA_IRQA, SND_CA_TSA /* transfer address (port / DMA), advances */, SND_CA_ESA /* set = reverb position reset */,
       SND_CA_EEA,
       SND_CA_REV0 /* + 0..21: APF1_SIZE, APF2_SIZE, SAME_L_DST, SAME_R_DST, COMB1_L, COMB1_R, COMB2_L, COMB2_R,
                      SAME_L_SRC, SAME_R_SRC, DIFF_L_DST, DIFF_R_DST, COMB3_L, COMB3_R, COMB4_L, COMB4_R, DIFF_L_SRC,
                      DIFF_R_SRC, APF1_L_DST, APF1_R_DST, APF2_L_DST, APF2_R_DST (relative offsets) */ };
#define SND_CA_COUNT (SND_CA_REV0 + 22)

SndMixer *snd_mixer_create(void);          /* zeroed memory + snd_mixer_reset */
void snd_mixer_destroy(SndMixer *m);
void snd_mixer_reset(SndMixer *m);         /* cores to power-on state (STATX 0x80, EEA 0xfffff); keeps sample memory,
                                              the IRQ info word and the frame counter (as spu2_init) */
void snd_mixer_set_callbacks(SndMixer *m, SndMixerEventFn ev, SndMixerAdmaFn adma, void *user);
void snd_mixer_set_mode(SndMixer *m, int interp /* 0 gaussian, 1 cubic (float), 2 linear */, int reverb_out);

/* commands */
void snd_mixer_voice_set(SndMixer *m, int core, int voice, int param, uint16_t v);
void snd_mixer_voice_addr_set(SndMixer *m, int core, int voice, int which, uint32_t addr);
void snd_mixer_key_on(SndMixer *m, int core, uint32_t mask);    /* voices 0..23 in order */
void snd_mixer_key_off(SndMixer *m, int core, uint32_t mask);
void snd_mixer_core_mask_set(SndMixer *m, int core, int which, uint32_t mask);
void snd_mixer_core_set(SndMixer *m, int core, int param, uint16_t v);
void snd_mixer_core_addr_set(SndMixer *m, int core, int which, uint32_t addr);
void snd_mixer_endx_clear(SndMixer *m, int core, uint32_t mask);
void snd_mixer_irqinfo_set(SndMixer *m, uint16_t v);           /* raw IRQ info word (bits 2 / 3 = core 0 / 1 pending) */
/* data port at TSA (IRQ check, then write, TSA + 1 per halfword) */
void snd_mixer_port_write(SndMixer *m, int core, const uint16_t *hw, uint32_t n);
uint16_t snd_mixer_port_read(SndMixer *m, int core);
/* DMA between host memory and sample memory at TSA (TSA advances; STATX bit 7 clears until snd_mixer_service) */
void snd_mixer_dma_write(SndMixer *m, int core, const void *src, uint32_t bytes);
void snd_mixer_dma_read(SndMixer *m, int core, void *dst, uint32_t bytes);
/* AutoDMA sound-data input: bytes at host cookie madr, pulled 1 KB at a time while mixing (first blocks at once) */
void snd_mixer_adma_start(SndMixer *m, int core, uint32_t madr, uint32_t bytes);
/* completes pending sample-memory DMAs (SND_EV_DMA_DONE per core); returns the mask of completed cores */
int snd_mixer_service(SndMixer *m);
int snd_mixer_dma_pending(SndMixer *m, int core);
void snd_mixer_dma_complete(SndMixer *m, int core);           /* service one core without the event */

/* mixing: frames of interleaved L/R s16 at 48 kHz (out may be 0 to discard) */
void snd_mixer_mix(SndMixer *m, int16_t *out, int frames);
uint64_t snd_mixer_frames(const SndMixer *m);                  /* frames mixed since create */

/* queries */
uint16_t snd_mixer_voice_get(const SndMixer *m, int core, int voice, int param);
uint32_t snd_mixer_voice_addr_get(const SndMixer *m, int core, int voice, int which);
int snd_mixer_voice_phase(const SndMixer *m, int core, int voice);   /* 0 off, 1 attack .. 4 release */
uint32_t snd_mixer_core_mask_get(const SndMixer *m, int core, int which);
uint16_t snd_mixer_core_get(const SndMixer *m, int core, int param);
uint32_t snd_mixer_core_addr_get(const SndMixer *m, int core, int which);
uint32_t snd_mixer_endx(const SndMixer *m, int core);
uint16_t snd_mixer_irqinfo(const SndMixer *m);
int snd_mixer_irq_pending_mask(const SndMixer *m);   /* bit 0 core 0, bit 1 core 1 */
int snd_mixer_dma_busy(const SndMixer *m, int core);
uint32_t snd_mixer_adma_remaining(const SndMixer *m, int core);
uint16_t *snd_mixer_ram(SndMixer *m);                /* SND_MIXER_RAM_HALFWORDS halfwords */

/* shared tables / kernels (unit tests, tools) */
extern const int16_t snd_mixer_gauss[512];
extern const int16_t snd_mixer_reverb_fir[39];
/* decodes one 16-byte (8-halfword) PS-ADPCM block with history h1 / h2 (updated) into 28 samples */
void snd_mixer_adpcm_decode(const uint16_t blk[8], int32_t *h1, int32_t *h2, int16_t out[28]);
/* one envelope step (counter / level) as the ADSR / sweep unit does it */
void snd_mixer_env_step(int32_t *level, uint32_t *counter, int shift, int stepv, int exp, int dec, int neg, int all_bits);

/* --spu-selftest: replays an ACNB_SPU_REC log (W / R / D / P / T records) through a fresh mixer via a register adapter,
 * checks every R record against the mixer's answers and every event against the log's ISR ('I') records, writes the
 * WAV (wav may be 0); prints a summary to stderr;
 * returns 0 if every check matched. Unit tests (tables / ADPCM vs a reference decode) run first. */
int snd_spurec_selftest(const char *log, const char *wav);

#ifdef __cplusplus
}
#endif
