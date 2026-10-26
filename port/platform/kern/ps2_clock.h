/* Virtual EE clock + EE timer 0 for the source port.
 * Source port only (the hybrid has runtime/hw.c, whose timer / vblank-grid arithmetic this copies line for line).
 *
 * EE time (EE cycles, 294.912 MHz) moves only at vblank steps: the vsync wait (sourceport/main/host_vsync.cpp) and the
 * kernel's idle step, each to the start of the next vblank (n * PS2C_VBLANK_CYCLES). That is the hybrid at
 * CYC_PER_INSN 0 (hw.h:9): there EE time also moves only when the EE waits. So get_timer0 sees the same value
 * sequence in both builds; host time only paces the present and never reaches game code.
 *
 * Timer 0 (the only timer the game programs: lib/timer.cpp init_timer0, mode 0x1c1 = BUSCLK/16, zero return,
 * compare interrupt, target 60000): COUNT / MODE / COMP writes, the 16-bit count with the zero-return wrap at
 * COMP + 1, the EQUF / OVFF flags and the compare event times are hw.c's. Interrupt delivery copies the hybrid's two
 * ways of reaching a vblank:
 *   idle (kernel.c schedule: no thread ready, and the I_STAT poll of sceGsSyncV's wait, hw.c 0x1000f000): time
 *         steps event by event, each compare raises INTC 9 at its own time (both source-port vblank paths, B7)
 *   busy (a GS CSR poll, hw.c gs_priv_read; the game's waits do not take it): one skip to the vblank, then one
 *         hw_update: at most one compare interrupt however many compares the skip passed, at the vblank time. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS2C_VBLANK_CYCLES 4920115ull   /* NTSC 59.94 Hz in EE cycles (hw.h VBLANK_CYCLES) */
#define PS2C_HBLANK_CYCLES 18743ull     /* 15734 Hz (hw.h HBLANK_CYCLES) */
#define PS2C_INTC_TIM0 9

uint64_t ps2c_now(void);                        /* EE cycles since boot (virtual) */
/* timer 0 registers: reg 0x00 COUNT, 0x10 MODE, 0x20 COMP (the T0_* offsets) */
unsigned ps2c_timer0_read(unsigned reg);
void ps2c_timer0_write(unsigned reg, unsigned v);
/* move EE time to t (never backwards). raise(cause) = an interrupt at the current ps2c_now() (the handlers run in it,
 * and may write the timer). busy = 1: a CSR poll's single skip (unused); 0: event-by-event steps */
void ps2c_advance_to(uint64_t t, int busy, void (*raise)(int cause));
/* GS CSR.FIELD: toggled at every vblank start (hw.c hw_vblank_start) */
unsigned ps2c_gs_field(void);
void ps2c_toggle_field(void);
void ps2c_reset(void);                          /* boot state (time 0, timer stopped, field 0); tests */

#ifdef __cplusplus
}
#endif
