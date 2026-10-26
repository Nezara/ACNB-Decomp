/* Platform stand-ins for SDK / player code the source port does not run (boot slice B3, /
 * 3.4). Listed in scripts/sp_missing.py HOST_DEFINED (no generated stub for them).
 *   scePrintf (libkernl debug output): stdout, like lib/sdk/newlib.cpp's printf family (the hybrid's "[tty]" lines)
 *   sceDev{Vif0,Vu0,Vif1,Vu1}Reset (libdev): no VIF / VU units here; the render module starts from its reset state
 *   sceCdPOffCallback (libcdvd, link/fseth.c power-off): nothing to register, returns 0 (hybrid: hle h_ret0)
 *   ps2_exit (newlib exit): the clean exit path (sp_exit)
 *   Movie_PlayEasy / Movie_PlayForMenu: milestone 1 skips every movie (user decision s96), as the hybrid's
 *       hle.c h_movie: log, return 1 (MOVIE_RESULT_FINISH = played to the end, no skip by pad). Real playback =
 *       phase 3 step 6 (lib/fslibmpg2_dd). */
#include <stdio.h>
#include "sp_vsync.h"

extern "C" {

int ps2_scePrintf(const char *fmt) { return printf(fmt, 0); }   /* no conversions in fmt; the 0 keeps -Wformat-security quiet */
int ps2_scePrintf_dd(const char *fmt, int v1, int v2) { return printf(fmt, v1, v2); }
int ps2_scePrintf_sd(const char *fmt, const char *str, int v) { return printf(fmt, str, v); }

/* libcdvd sceCdPOffCallback: no power button / IOP power-off event here; returns 0 like the hybrid's hle h_ret0 */
void *sceCdPOffCallback(void (*func)(void *), void *addr) { (void)func; (void)addr; return 0; }

int sceDevVif0Reset(void) { return 0; }
int sceDevVu0Reset(void) { return 0; }
int sceDevVif1Reset(void) { return 0; }
int sceDevVu1Reset(void) { return 0; }

void ps2_exit(int status) { sp_exit(status); }

int Movie_PlayEasy(char *pfnameMovie, char *pInBuf)
{
    (void)pInBuf;
    fprintf(stderr, "movie: skipping %s\n", pfnameMovie ? pfnameMovie : "?");
    return 1;
}

/* eMovieResult (signed char) Movie_PlayForMenu(path, buf, inPadF, inLoopF, inMenuCall, volume, ePlayMode) */
signed char Movie_PlayForMenu(char *pfnameMovie, char *pInBuf, unsigned int inPadF, unsigned int inLoopF,
                              unsigned int inMenuCall, unsigned int volume, int inMode)
{
    (void)pInBuf; (void)inPadF; (void)inLoopF; (void)inMenuCall; (void)volume; (void)inMode;
    fprintf(stderr, "movie: skipping %s\n", pfnameMovie ? pfnameMovie : "?");
    return 1;
}

}
