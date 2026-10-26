/* Platform module (port/platform, phase 3): host services shared by game.dll (hybrid) and acnb.exe (sourceport).
 * Rules: C-like C++20 behind an extern "C" API, no ee.h / hw.h / mem.h, no ACNB addresses in
 * the generic parts, 64-bit clean. The host plugs in its log sink and clock; nothing here reads game state. */
#pragma once
#include <stdarg.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* log sink (hybrid: ee_log; unset = stderr) */
void plat_set_log(void (*fn)(const char *fmt, va_list ap));
void plat_log(const char *fmt, ...);

/* ACNB_DISC_TRACE=<file>: one line per disc / file access, "<vblank> <EE|IOP> <api> <args> -> <result> [crc ...]"
 *. disc_trace_on: -1 = not decided yet, 0 = off, 1 = on (decided by the first disc_trace_enabled).
 * Deterministic content only: callers print guest addresses / game-visible fds, never host pointers. */
extern int disc_trace_on;
void disc_trace_set_clock(const uint32_t *vblank);   /* counter printed as the first field */
uint32_t disc_trace_clock(void);                     /* its current value (0 before the host set one) */
int disc_trace_enabled(void);
void disc_trace(const char *side, const char *fmt, ...);
void disc_trace_flush(void);
/* in-process capture: every trace line (without '
') also goes to fn, which turns tracing on; 0 = stop capturing */
void disc_trace_set_sink(void (*fn)(const char *line));
uint32_t disc_crc32(const void *p, uint32_t n);

/* ACNB_PAD_TRACE=<file>: same clock and line shape, own file: "<vblank> L1 ..." per
 * layer-1 sample change, "<vblank> PAD <api> ..." per libpad call. disc_trace_flush flushes this file too. */
extern int pad_trace_on;
int pad_trace_enabled(void);
void pad_trace(const char *side, const char *fmt, ...);
void pad_trace_set_sink(void (*fn)(const char *line));

/* ACNB_SND_TRACE=<file>: same clock and line shape, own file: "<vblank> EE RPC ..." per
 * EE -> IOP SIF RPC, "EE DMA" per sceSifSetDma entry, "IOP RPC" / "IOP DMA" for IOP -> EE, "IOP MOD" per module load,
 * "SPU ..." per SPU2 event (KON / KOFF per voice with its parameters, DMA, ADMA, ATTR, reverb regs; IOP cycle field).
 * Callers test `snd_trace_on && snd_trace_enabled()` (-1 = undecided). disc_trace_flush flushes it. */
extern int snd_trace_on;
int snd_trace_enabled(void);
void snd_trace(const char *side, const char *fmt, ...);

/* ACNB_MC_TRACE=<file>: same clock and line shape, own file: "<vblank> MC <api> <args> ->
 * <result>" per libmc call (port/platform/ps2_mc; pointers are not printed). disc_trace_flush flushes it. */
extern int mc_trace_on;
int mc_trace_enabled(void);
void mc_trace(const char *side, const char *fmt, ...);

/* ACNB_SND_REC=<file>: full-payload record of the EE <-> IOP traffic (selftest input). File = "ACNBSREC" + u32 version 1,
 * then records { char tag[4]; u32 vblank; u32 nbytes; u32 hdr[nhdr]; u8 a[na]; u8 b[nb] } (little endian; nbytes =
 * 4 * nhdr + na + nb). */
extern int snd_rec_on;
int snd_rec_enabled(void);
void snd_rec(const char tag[4], const uint32_t *hdr, int nhdr, const void *a, uint32_t na, const void *b, uint32_t nb);

#ifdef __cplusplus
}
#endif
