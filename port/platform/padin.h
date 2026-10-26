/* padin - live pad input for a PS2 pad (phase 3 step 3 P4): an SDL3 gamepad (SDL_Gamepad API, standard mapping) and a
 * keyboard read through a host-supplied key provider, merged into one PadFrame (ps2_pad.h: active-high libpad buttons,
 * sticks 0..255 with centre 0x80). Generic (no ACNB addresses, no emulator headers; Last Raven reusable).
 *
 * Determinism: padin is a LIVE source only. The host samples it synchronously once per game frame (the hybrid: video.c
 * poll_input, emulation thread, once per vblank) and must not call it at all while a replay / script / injected input
 * drives the pad. Hot-plug lives here only: the game-facing ps2_pad stays an always-connected pad (no controller =
 * a neutral frame).
 *
 * SDL3 is loaded at run time (SDL3.dll / libSDL3.so.0) on the first padin_poll with the gamepad on, on the calling
 * thread (which then is the thread that pumps SDL events); a missing library = keyboard only (one log line). Nothing is
 * loaded while the gamepad is off, so headless check runs never touch SDL.
 *
 * Gamepad mapping: SOUTH / EAST / WEST / NORTH = cross / circle / square / triangle (by position), shoulders = L1 / R1,
 * triggers = L2 / R2 (digital, threshold + hysteresis), stick clicks = L3 / R3, Back = select, Start, D-pad; sticks
 * (v >> 8) + 128 (SDL's Y is already down-positive = the PS2's). Radial dead zone with hysteresis per stick: inside it
 * the stick is exactly 0x80, outside the raw value passes unchanged (no curve).
 * Players (P4b): player 1 = the first gamepad SDL lists (cfg.gamepad N > 1: the first from the N-th on) + the keyboard; player 2 (cfg.pad2) = the next gamepad. A
 * player keeps its gamepad until that one is removed (unplugging player 1's pad does not move player 2's pad over);
 * an empty player slot takes the first listed gamepad no other player holds, player 1 first. The keyboard is always
 * player 1 (no keyboard binding for player 2).
 * Keyboard: up to 2 keys per action (padin_keys.h; HID usage codes), read only while the host says it is focused.
 * Buttons are OR-ed; a held stick key overrides that stick axis (0 / 255).
 * Rumble (P5): output only - nothing in padin's input path reads it back. padin_rumble(player, small, large) is called
 * by the host once per sample (after padin_poll2, same thread, same live-only gate) with the pad's current motor
 * command (DualShock 2: small motor on / off, large motor 0..255) -> SDL_RumbleGamepad(low = large * 257, high = small
 * ? 0xffff : 0) scaled by cfg.rumble percent. Sent on change, refreshed every PADIN_RUMBLE_REFRESH samples while
 * non-zero with a PADIN_RUMBLE_MS duration, so a stalled host (pause, hitch, crash) stops by SDL's own expiry. It
 * follows input focus: a gamepad that is not read now (unfocused with cfg.background off) is stopped. Stopped on
 * unplug (slot emptied), pad2 off, cfg.rumble 0, padin_rumble_off and padin_shutdown. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "ps2_pad.h"
#include "padin_keys.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct PadinConfig {
    int gamepad;        /* 0 = keyboard only (SDL never loaded); N = player 1 takes the first free gamepad from SDL's N-th
                         * listed one on (1 = any; 2 = skips the first: a second instance on the same PC, host link) */
    int keyboard;       /* 1 = the keyboard is player 1 while the host is focused; 0 = no keyboard */
    int background;     /* 1 = gamepad read while the host window is not focused */
    int deadzone;       /* radial stick dead zone, percent of full deflection (0..90) */
    int trigger;        /* L2 / R2 threshold, percent of the trigger travel (1..100) */
    int rumble;         /* rumble strength percent (0..100), 0 = off (SDL rumble never called) */
    int pad2;           /* 1 = a second gamepad is player 2 (libpad port 1); 0 = one player only */
    int virtual_only;   /* tests: only SDL virtual joysticks are used (real pads ignored) */
    uint16_t keys[PADIN_NACT][PADIN_KEYS_PER];   /* HID usage codes, 0 = none */
} PadinConfig;

/* nonzero if the key (HID usage = SDL_Scancode) is down; called only while the host is focused */
typedef int (*padin_key_fn)(void *user, int hid);

#define PADIN_PLAYERS 2

/* defaults: gamepad 1 (on, first listed), keyboard on, background on, dead zone 13 %, trigger 25 % (the old XInput > 64 of 255), rumble 100 %,
 * pad2 on, keys = padin_actions[].def */
void padin_config_default(PadinConfig *c);
/* new configuration / key provider (may be called again; SDL stays loaded). keys may be 0 (no keyboard). */
void padin_configure(const PadinConfig *c, padin_key_fn keys, void *user);
/* one sample: pumps SDL events (hot-plug), reads the gamepad (if on and focused || background) and the keys (if
 * focused) -> out. Neutral frame = {0, 0x80 x4}. */
void padin_poll(int focused, PadFrame *out);
/* the same for both players: out[0] = player 1 (gamepad + keyboard), out[1] = player 2's gamepad (neutral if none).
 * Returns 1 if player 2 has a gamepad (cfg.pad2 and a second gamepad open) - the host's "port 1 connected" for this
 * sample (a live source only: replays carry their own connection state). */
int padin_poll2(int focused, PadFrame out[PADIN_PLAYERS]);
/* 1 if player 1 / player N (0-based) has a gamepad open; name (may be 0) gets its SDL name */
int padin_gamepad(char *name, size_t n);
int padin_gamepad_n(int player, char *name, size_t n);
#define PADIN_RUMBLE_MS 500       /* SDL rumble duration per command */
#define PADIN_RUMBLE_REFRESH 15   /* keep-alive: re-sent after this many unchanged non-zero samples (~250 ms) */
/* player (0-based) motor command for this sample: small = small motor on (nonzero), large = large motor 0..255.
 * No-op while SDL is not loaded / the player has no gamepad. */
void padin_rumble(int player, int small, int large);
/* stop every gamepad's motors now (exit paths; may be called from another thread; no-op if SDL is not loaded) */
void padin_rumble_off(void);
/* close the gamepad and SDL (padin_poll reopens); stops rumble first */
void padin_shutdown(void);

/* ---- test support (--pad-test): an SDL virtual gamepad (standard layout: SDL_GamepadButton / SDL_GamepadAxis
 * numbers). attach loads SDL if needed; returns 0 on failure. */
int padin_vpad_attach(void);
void padin_vpad_detach(void);
void padin_vpad_button(int sdl_button, int down);
void padin_vpad_axis(int sdl_axis, int value);   /* -32768..32767; triggers 0..32767 as SDL_GetGamepadAxis reads them */
/* the same for virtual gamepad i (0 = the one above, 1 = a second one; attach order = SDL listing order) */
int padin_vpad_attach_n(int i);
void padin_vpad_detach_n(int i);
void padin_vpad_button_n(int i, int sdl_button, int down);
void padin_vpad_axis_n(int i, int sdl_axis, int value);
/* virtual gamepad i's rumble callback: number of calls since attach, last (low, high) received */
int padin_vpad_rumble_n(int i, uint16_t *low, uint16_t *high);
/* number of SDL_RumbleGamepad calls padin made for player (0-based) since start */
unsigned padin_rumble_calls(int player);

#ifdef __cplusplus
}
#endif
