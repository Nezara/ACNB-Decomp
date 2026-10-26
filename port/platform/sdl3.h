/* sdl3 - the one run-time SDL3 loader of the platform layer (padin gamepads, video window / GL context, audio device).
 * SDL3.dll / libSDL3.so.0 is loaded on first use (no import library: a missing DLL is not fatal, and in the hybrid the
 * DLL is mapped only after the PS2 address ranges are reserved). Subsystems are initialised by their users
 * (SDL_InitSubSystem); this file only maps the library, sets the process-wide hints and owns the event queue.
 *
 * Events: SDL has one queue per process, so exactly one place may drain it. sdl3_pump() polls every pending event and
 * hands each to all registered handlers (padin: gamepad hot-plug; video: window close / focus / keys); every user that
 * wants fresh events calls sdl3_pump() instead of SDL_PollEvent. Thread: the thread that created the window. */
#pragma once
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_version.h>
#include <SDL3/SDL_video.h>

#define SDL3_FUNCS(X) \
    X(SDL_SetHint) X(SDL_InitSubSystem) X(SDL_QuitSubSystem) X(SDL_GetError) X(SDL_PollEvent) X(SDL_free) \
    X(SDL_GetVersion) \
    /* padin */ \
    X(SDL_GetGamepads) X(SDL_OpenGamepad) X(SDL_CloseGamepad) X(SDL_GamepadConnected) X(SDL_GetGamepadButton) \
    X(SDL_GetGamepadAxis) X(SDL_GetGamepadName) X(SDL_GetGamepadID) X(SDL_IsJoystickVirtual) \
    X(SDL_AttachVirtualJoystick) X(SDL_DetachVirtualJoystick) X(SDL_OpenJoystick) X(SDL_CloseJoystick) \
    X(SDL_SetJoystickVirtualButton) X(SDL_SetJoystickVirtualAxis) X(SDL_RumbleGamepad) \
    /* video */ \
    X(SDL_GL_SetAttribute) X(SDL_GL_ResetAttributes) X(SDL_CreateWindow) X(SDL_DestroyWindow) X(SDL_ShowWindow) \
    X(SDL_GL_CreateContext) X(SDL_GL_DestroyContext) X(SDL_GL_MakeCurrent) X(SDL_GL_GetProcAddress) \
    X(SDL_GL_SwapWindow) X(SDL_GL_SetSwapInterval) X(SDL_GetWindowSizeInPixels) X(SDL_SetWindowFullscreen) \
    X(SDL_GetWindowFlags) X(SDL_SetWindowMinimumSize) X(SDL_SetWindowPosition) X(SDL_GetKeyboardState) \
    X(SDL_GetTicksNS) X(SDL_DelayPrecise)

/* audio (port/platform/audio, N9): resolved separately and not required - an SDL3 without them still runs padin /
 * video, audiodev then stays silent (sdl3_audio_ok() = 0) */
#define SDL3_AUDIO_FUNCS(X)     X(SDL_OpenAudioDeviceStream) X(SDL_ResumeAudioStreamDevice) X(SDL_DestroyAudioStream)     X(SDL_PutAudioStreamData) X(SDL_LockAudioStream) X(SDL_UnlockAudioStream) X(SDL_GetCurrentAudioDriver)     X(SDL_GetAudioStreamDevice) X(SDL_GetAudioDeviceFormat) X(SDL_GetAudioDeviceName)

struct Sdl3Api {
#define X(n) decltype(&::n) n;
    SDL3_FUNCS(X)
    SDL3_AUDIO_FUNCS(X)
#undef X
};
extern Sdl3Api sdl3;

/* 1 = library mapped, every entry point resolved (cached; one log line on failure, then 0 forever) */
int sdl3_load(void);
int sdl3_audio_ok(void);                   /* 1 = sdl3_load succeeded and every SDL3_AUDIO_FUNCS entry resolved */
typedef void (*sdl3_event_fn)(const SDL_Event *e);
void sdl3_add_handler(sdl3_event_fn fn);   /* idempotent */
void sdl3_pump(void);                      /* drain the queue into the handlers; no-op before sdl3_load */
