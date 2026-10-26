/* The source port's vblank step: host_vsync.cpp. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* before the game's main(): installs the kernel idle hook, the trace clock, live pad input (window only) */
void sp_vsync_init(int headless);
/* input drivers (= the hybrid's runtime/video.c, reader in port/platform/replay): --replay F drives both pads from F
 * (live input off; 0 = ok, else the exit code: 2 = not format v2, as the hybrid), --record F writes the polled pads
 * to F (F-2, F-3.. if F exists). Call before sp_vsync_init. */
int sp_input_replay(const char *path);
void sp_input_record(const char *path);
/* `--set input.gamepad|keyboard|background|pad2|deadzone|trigger|rumble=N` (padin.h PadinConfig, live input only;
 * --link defaults: pad2 0, gamepad 1 host / 2 join). 1 = taken. Call before sp_vsync_init. */
int sp_input_set(const char *key_value);
/* the vblank count every trace line carries (disc / pad / snd traces; = the hybrid's vblank_count) */
uint32_t sp_vblank(void);
/* the source port's fixed sim rules, "1 video.culling=original .. versus.quick=0" (replay header, host link HELLO) */
const char *sp_sim_settings(void);
const uint32_t *sp_vblank_ptr(void);   /* the same counter for platform modules that keep a trace clock (audiodev) */
/* B6 hook point: EE RPC server wakeups. Called once per vblank step right after sndhost::AdvanceEe, in interrupt
 * context (ps2k_irq_wakeup only, never a fiber switch): maps the sids sndhost's posted hook recorded to the EE server
 * threads and wakes them; they run when the step returns, before the waiting thread continues if they are better. */
typedef void (*SpEeWakeFn)(void *user);
void sp_vsync_set_ee_wake(SpEeWakeFn fn, void *user);
/* the one clean exit path (exit-after, window closed, the game's exit()): rq summary, traces flushed, window closed */
[[noreturn]] void sp_exit(int code);

#ifdef __cplusplus
}
#endif
