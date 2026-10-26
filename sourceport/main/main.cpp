/* Stage 0 source port entry point: launch checks (disc, identity, ELF_DATA tables, overlay snapshots: sp_boot.cpp),
 * then the game's own main() (port/src/main/main.cpp). Stage 0 = the development version's source port with our additions
 * stripped: no hybrid, no self-tests, no host link, no script authoring.
 * Options: `--headless` (no window, null rq backend, no audio device), `--shot N` (repeatable), `--exit-after N`,
 * `--fast` (no 59.94 Hz pacing), `--mute`, `--set video.scale=N / video.aspect=W:H / video.window=WxH /
 * audio.enable=0 / audio.volume=0..200 / input.*` (host_vsync.cpp). Input drivers (host_vsync.cpp, reader
 * port/platform/replay): `--replay F` (format v2, or v3 = sandbox replay with its poke / freeze events,
 * port/platform/sandbox/events.cpp), `--record F` (format v2).
 * User tweaks baked in (no switches): even field at every field store (no interlace shimmer), no copy blur / flicker
 * filter (host_rq.cpp), every part unlocked (host_opt.cpp). */
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdint.h>
#include <windows.h>
#include "sp_boot.h"
#include "sp_missing.h"
#include "../../port/platform/video/video.h"
#include "../../port/platform/audio/audiodev.h"
#include "../../port/platform/kern/ps2_kern.h"
#include "sp_vsync.h"
#include "../../port/include/acnb/hooks.h"

/* the game's main(void); the host entry is wmain, so the name is free for the game */
extern "C" int acnb_game_main(void) __asm__("_main");
extern "C" void sp_opt_install(void);   /* host_opt.cpp: the Stage 0 tweaks' hooks */

/* crash report: exception code, faulting address as acnb.exe+offset (symbolize with llvm-symbolizer --obj=acnb.exe
 * 0x400000+offset: the image is DYNAMIC_BASE), and the stack words that point into the image (return-address
 * candidates, innermost first) - an i686 game under WoW64 is not debuggable with the x86_64 lldb */
static LONG WINAPI sp_crash(EXCEPTION_POINTERS *ep)
{
    uintptr_t base = (uintptr_t)GetModuleHandleW(0);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    uintptr_t end = base + nt->OptionalHeader.SizeOfImage;
    uintptr_t pc = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
    fflush(stdout);
    fprintf(stderr, "acnb: crash %08lx at %p (acnb.exe+%#lx) vblank %u\n", (unsigned long)ep->ExceptionRecord->ExceptionCode,
            (void *)pc, (unsigned long)(pc - base), sp_vblank());
#ifdef _M_IX86
    const uintptr_t *sp = (const uintptr_t *)ep->ContextRecord->Esp;
    int n = 0;
    for (int i = 0; i < 4096 && n < 24; i++) {
        MEMORY_BASIC_INFORMATION mi;
        if (!(i & 1023) && (!VirtualQuery(sp + i, &mi, sizeof mi) || mi.State != MEM_COMMIT)) break;
        if (sp[i] > base + 0x1000 && sp[i] < end) { fprintf(stderr, "acnb:   stack +%#x: acnb.exe+%#lx\n", i * 4, (unsigned long)(sp[i] - base)); n++; }
    }
#endif
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

void sp_missing(const SpMissing *fn)
{
    fflush(stdout);
    fprintf(stderr, "acnb: reached %s (%s), which is not decompiled yet - see build/missing.md\n", fn->name, fn->module);
    exit(70);
}

extern "C" int wmain(int argc, wchar_t **argv)
{
    printf("Armored Core: Nine Breaker - source port (Stage 0)\n");
    SetUnhandledExceptionFilter(sp_crash);
    int headless = 0, mute = 0;
    VideoConfig *vc = video_config();
    static char replay[600], record[600];
    for (int i = 1; i < argc; i++) {
        char a[600] = "";
        if (i + 1 < argc) snprintf(a, sizeof a, "%ls", argv[i + 1]);
        if (!wcscmp(argv[i], L"--headless")) headless = 1;
        else if (!wcscmp(argv[i], L"--mute")) mute = 1;
        else if (!wcscmp(argv[i], L"--fast")) vc->fast = 1;   /* no 59.94 Hz pacing (tests) */
        else if (!wcscmp(argv[i], L"--shot") && i + 1 < argc) {
            if (vc->nshots < VIDEO_MAX_SHOTS) vc->shots[vc->nshots++] = (unsigned)strtoul(a, 0, 0);
            i++;
        } else if (!wcscmp(argv[i], L"--exit-after") && i + 1 < argc) {
            vc->exit_after = (unsigned)strtoul(a, 0, 0);
            i++;
        } else if (!wcscmp(argv[i], L"--replay") && i + 1 < argc) {
            snprintf(replay, sizeof replay, "%s", a);
            i++;
        } else if (!wcscmp(argv[i], L"--record") && i + 1 < argc) {
            snprintf(record, sizeof record, "%s", a);
            i++;
        } else if (!wcscmp(argv[i], L"--set") && i + 1 < argc) {
            if (!video_set(a) && !audiodev_set(a) && !sp_input_set(a))
                fprintf(stderr, "acnb: --set %s ignored (video.scale / video.aspect / video.window / audio.enable / audio.volume / "
                                "input.* only)\n", a);
            i++;
        } else fprintf(stderr, "acnb: option %ls ignored\n", argv[i]);
    }
    int r = sp_boot();
    if (r) return r;
    /* the window before the game creates the Rq (GL backend); no window = headless, null backend */
    if (!headless && !video_open("Armored Core: Nine Breaker")) fprintf(stderr, "acnb: no window - running headless\n");
    if (replay[0] && (r = sp_input_replay(replay)) != 0) return r;
    if (record[0]) sp_input_record(record);
    sp_opt_install();   /* even field, every part unlocked (fixed in Stage 0) */
    ps2k_init();   /* the host thread becomes EE thread 1 (port/platform/kern) */
    /* N9: SDL3 audio device + the mixer tap (no window = no device; the device never feeds back into the sim) */
    audiodev_open(headless || mute || !video_gl_current(), sp_vblank_ptr());
    sp_vsync_init(headless);   /* the vblank step: virtual EE time, present, sound host, pad (host_vsync.cpp) */
    int rc = acnb_game_main();
    audiodev_close();
    return rc;
}
