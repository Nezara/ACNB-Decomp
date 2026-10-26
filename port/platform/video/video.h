/* video - the source port's window + presenter (phase 3 boot slice B1): an SDL3 window with a GL 4.5 core context for
 * the render module (port/render, GL backend), the per-vblank present, 59.94 Hz pacing, screenshots and exit-after.
 * Source port only (the hybrid has runtime/video.c + gl.c; this folder is not in its build).
 *
 * Use (one thread, the one that runs the sim and pumps SDL events):
 *   video_config() / video_set()   before video_open: milestone-1 defaults (4:3, scale 1, windowed 1280x960, swap
 *                                  interval 0 + deadline pacing); `--set` reaches video_set for video.scale / video.aspect
 *   video_open()                   window + context current; rq_gl_proc / rq_gl_current (host_rq.cpp) forward to
 *                                  video_gl_proc / video_gl_current, so an Rq created after it gets the GL backend
 *   video_vblank(rq, vblank, kind) ONCE PER VBLANK (the vsync step, ps2_vsync_wait): pump events, rq_present, widescreen
 *                                  shape, blit the module's output (or clear), shot, swap, pace. Without a window it
 *                                  does rq_present only (headless runs, null backend, no pacing). Returns VIDEO_* flags;
 *                                  the host decides what to do with them (video never exits the process).
 *   video_summary(rq) / video_close()  exit path (--exit-after, window closed)
 *
 * Time: host time only paces the present (the deadline grid restarts after a stall); it never feeds the sim. */
#pragma once
#include <stdint.h>
#include "../../render/rq.h"
#ifdef __cplusplus
extern "C" {
#endif

#define VIDEO_MAX_SHOTS 16
typedef struct {
    int scale;              /* render module internal resolution 1..4 (RqOptions.scale; read when the Rq is created) */
    float aspect;           /* video.aspect: presented shape of battle frames (4:3 = original; wider = Hor+) */
    int vsync;              /* 1 = swap interval 1, the 59.94 grid a ceiling only; 0 = swap interval 0 + deadline pacing */
    int fast;               /* 1 = no pacing (tests) */
    int fullscreen;         /* start fullscreen (F11 / Alt+Enter toggle at run time) */
    int nearest;            /* nearest-neighbour present filter */
    int win_w, win_h;       /* initial window size (logical units) */
    uint32_t clear_rgb;     /* window colour while the module has no output (0xRRGGBB) */
    uint32_t exit_after;    /* VIDEO_EXIT_AFTER from this vblank on (0 = off) */
    uint32_t shots[VIDEO_MAX_SHOTS]; int nshots;   /* --shot N: shot_<N>.png into ACNB_SHOT_DIR (default .) */
    int win_pos, win_x, win_y;   /* video.window=X,Y[,W,H]: win_pos 1 = initial position X, Y (else SDL's default) */
} VideoConfig;

VideoConfig *video_config(void);
/* "video.scale=N" / "video.aspect=W:H" / "video.window=X,Y[,W,H]" (two linked windows side by side); 1 = taken, 0 = not a video key or a bad value (logged) */
int video_set(const char *key_value);
int video_open(const char *title);   /* 1 = window + GL 4.5 core context current; 0 = failed (logged, stays headless) */
void video_close(void);
int video_gl_current(void);          /* 1 while the context exists (rq_gl_current) */
void *video_gl_proc(const char *name);   /* SDL_GL_GetProcAddress (rq_gl_proc) */

enum { VIDEO_FRAME_MENU = 0, VIDEO_FRAME_BATTLE = 1 };   /* kind of the frame being drawn next (widescreen: Hor+ only in battle) */
enum { VIDEO_QUIT = 1, VIDEO_EXIT_AFTER = 2 };
int video_vblank(Rq *rq, uint32_t vblank, int kind);
const RqFrameResult *video_last_frame(void);   /* rq_present's result of the last video_vblank */
void video_summary(Rq *rq);                    /* rq_summary to stdout */

/* events only, no present (the host link barrier while it waits for the peer): 1 = the user closed the window */
int video_pump(void);
int video_focused(void);                       /* window has input focus (padin_poll2 `focused`) */
int video_key_down(void *user, int hid);       /* padin_key_fn: HID usage = SDL_Scancode, from the SDL keyboard state */

#ifdef __cplusplus
}
#endif
