/* sdl3 - run-time SDL3 loader + the shared event pump (see sdl3.h). */
#include "sdl3.h"
#include "plat.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

Sdl3Api sdl3;
static int state;   /* 0 not tried, 1 ready, -1 unavailable */
static int audio_ok;
static sdl3_event_fn handlers[4];
static int nhandlers;

int sdl3_load(void)
{
    if (state) return state > 0;
    state = -1;
#ifdef _WIN32
    HMODULE h = LoadLibraryW(L"SDL3.dll");
#define LOADSYM(n) (void *)GetProcAddress(h, n)
#else
    void *h = dlopen("libSDL3.so.0", RTLD_NOW);
#define LOADSYM(n) dlsym(h, n)
#endif
    if (!h) { plat_log("sdl3: SDL3 library not found"); return 0; }
    int ok = 1;
#define X(n) if (!(*(void **)&sdl3.n = LOADSYM(#n))) { plat_log("sdl3: SDL3 has no " #n); ok = 0; }
    SDL3_FUNCS(X)
#undef X
    if (!ok) return 0;
    audio_ok = 1;
#define X(n) if (!(*(void **)&sdl3.n = LOADSYM(#n))) { plat_log("sdl3: SDL3 has no " #n " (no audio)"); audio_ok = 0; }
    SDL3_AUDIO_FUNCS(X)
#undef X
#undef LOADSYM
    sdl3.SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");   /* the host owns Ctrl+C */
    sdl3.SDL_SetHint(SDL_HINT_TIMER_RESOLUTION, "0");     /* the host owns the timer period (pacing) */
    state = 1;
    return 1;
}

int sdl3_audio_ok(void) { return sdl3_load() && audio_ok; }

void sdl3_add_handler(sdl3_event_fn fn)
{
    for (int i = 0; i < nhandlers; i++) if (handlers[i] == fn) return;
    if (nhandlers < (int)(sizeof handlers / sizeof *handlers)) handlers[nhandlers++] = fn;
}

void sdl3_pump(void)
{
    if (state <= 0) return;
    SDL_Event e;
    while (sdl3.SDL_PollEvent(&e))
        for (int i = 0; i < nhandlers; i++) handlers[i](&e);
}
