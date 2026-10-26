/* audiodev - the source port's audio output (phase 3 step 5 N9): the SPU2 mixer's output (sndhost::SetMixTap, 48 kHz
 * stereo s16, produced on the sim thread inside the IOP clock = EE virtual time) -> a bounded ring -> an SDL3 audio
 * stream on SDL's device thread. One way only: nothing the device does reaches the sim (no state, no timing; pacing
 * stays on the video vblank pacer). The hybrid's runtime/audio.c (waveOut) is the behaviour model: production
 * follows game time, a faster-than-real-time run (--fast) drops the oldest samples, a stall plays silence.
 *
 * Ring policy (frames = stereo samples; one vblank = 48000 / 59.94 = 801):
 *   start / after an underrun: silence until PRIME (1.5 vblanks) is queued, then play;
 *   underrun: the device gets what is there + silence (counted), then re-primes;
 *   overrun: more than HIGH (4 vblanks) queued -> the oldest are dropped down to TRIM (2 vblanks) (counted).
 *   Device buffer: SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES 480 (10 ms). Latency ~ 1.5-2.5 vblanks queued + 10 ms.
 * Settings (fixed defaults; `--set` / the hybrid's acnb.ini [audio] key names):
 *   audio.enable = 1 / 0 (alias audio.enabled; default 1), audio.volume = 0..200 (default 100, device side only)
 *   --headless / --mute: no device. No SDL3 / no device: silent, one log line, the game runs on.
 * Checks: ACNB_AUDIO_DUMP=<wav> = every frame the tap queues (after volume; = the mixer output at volume 100,
 * deterministic, also headless); ACNB_AUDIO_DUMP_DEV=<wav> = what the SDL stream was given (with drops / silence; host
 * timing). Summary line at close: queued / played / silence / dropped frames, underruns, overruns, first audible vblank. */
#pragma once
#include <stdint.h>

int audiodev_set(const char *kv);           /* "audio.enable=0" / "audio.volume=80": 1 = an audio key (taken) */
/* after the video window (or headless): opens the device unless disabled / headless, installs the mixer tap when
 * the device or a dump is on. vblank = the trace clock for the summary's first-audible line (may be 0) */
void audiodev_open(int headless, const uint32_t *vblank);
void audiodev_close(void);                  /* summary, dumps finalised, device closed; idempotent */
/* --audio-test FILE [VBLANKS]: plays a mixer WAV through the device path at game rate (no disc, no sim); 0 = ran */
int audiodev_test(const char *path, unsigned vblanks);
