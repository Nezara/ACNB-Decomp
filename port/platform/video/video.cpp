/* video - SDL3 window + GL 4.5 core context + per-vblank presenter for the render module (see video.h).
 * Present logic after runtime/video.c video_vblank (rq part only) + widescreen.c widescreen_vblank + pace / pace_frame. */
#include "video.h"
#include "png.h"
#include "../sdl3.h"
#include "../plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define VGL __stdcall
#else
#define VGL
#endif

enum {
    V_GL_COLOR_BUFFER_BIT = 0x4000, V_GL_SCISSOR_TEST = 0x0C11, V_GL_FRAMEBUFFER = 0x8D40, V_GL_BACK = 0x0405,
    V_GL_BGRA = 0x80E1, V_GL_UNSIGNED_BYTE = 0x1401, V_GL_PACK_ALIGNMENT = 0x0D05, V_GL_VENDOR = 0x1F00,
    V_GL_RENDERER = 0x1F01, V_GL_VERSION = 0x1F02
};
#define VIDEO_GL(X) \
    X(void, glClearColor, (float, float, float, float)) X(void, glClear, (unsigned)) \
    X(void, glViewport, (int, int, int, int)) X(void, glBindFramebuffer, (unsigned, unsigned)) \
    X(void, glDisable, (unsigned)) X(void, glColorMask, (unsigned char, unsigned char, unsigned char, unsigned char)) \
    X(void, glReadBuffer, (unsigned)) X(void, glPixelStorei, (unsigned, int)) \
    X(void, glReadPixels, (int, int, int, int, unsigned, unsigned, void *)) X(const unsigned char *, glGetString, (unsigned))
#define X(R, n, A) R (VGL *n) A;
static struct { VIDEO_GL(X) } gl;
#undef X

static VideoConfig cfg = {1, 4.0f / 3.0f, 0, 0, 0, 0, 1280, 960, 0, 0, {0}, 0};
static SDL_Window *win;
static SDL_GLContext ctx;
static int focused = 1, quit;
static float shown = 4.0f / 3.0f;   /* shape the frame being finished was drawn for (widescreen.c `shown`) */
static RqFrameResult last;

VideoConfig *video_config(void) { return &cfg; }

int video_set(const char *kv)
{
    const char *eq = strchr(kv, '=');
    if (!eq) return 0;
    size_t n = (size_t)(eq - kv);
    const char *v = eq + 1;
    if (n == 11 && !strncmp(kv, "video.scale", n)) {
        int s = atoi(v);
        if (s < 1 || s > 4) { plat_log("video: video.scale=%s out of range 1..4 - ignored", v); return 0; }
        cfg.scale = s;
        return 1;
    }
    if (n == 12 && !strncmp(kv, "video.aspect", n)) {
        float w = 0, h = 0;
        if (sscanf(v, "%f:%f", &w, &h) != 2 || w <= 0 || h <= 0) { plat_log("video: video.aspect=%s is not W:H - ignored", v); return 0; }
        cfg.aspect = w / h < 4.0f / 3.0f ? 4.0f / 3.0f : w / h;   /* narrower than 4:3 = 4:3 (settings.c) */
        return 1;
    }
    if (n == 12 && !strncmp(kv, "video.window", n)) {
        int x, y, w = cfg.win_w, h = cfg.win_h, k = sscanf(v, "%d,%d,%d,%d", &x, &y, &w, &h);
        if ((k != 2 && k != 4) || w < 160 || h < 120) { plat_log("video: video.window=%s is not X,Y[,W,H] - ignored", v); return 0; }
        cfg.win_pos = 1; cfg.win_x = x; cfg.win_y = y; cfg.win_w = w; cfg.win_h = h;
        return 1;
    }
    return 0;
}

int video_gl_current(void) { return ctx != 0; }
void *video_gl_proc(const char *name) { return ctx ? (void *)sdl3.SDL_GL_GetProcAddress(name) : nullptr; }
int video_focused(void) { return focused; }
const RqFrameResult *video_last_frame(void) { return &last; }

int video_key_down(void *user, int hid)
{
    (void)user;
    if (!win) return 0;
    int n = 0;
    const bool *k = sdl3.SDL_GetKeyboardState(&n);
    return k && hid >= 0 && hid < n && k[hid];
}

static void toggle_fullscreen(void)
{
    int fs = (sdl3.SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0;
    if (!sdl3.SDL_SetWindowFullscreen(win, !fs)) plat_log("video: fullscreen toggle failed: %s", sdl3.SDL_GetError());
}

static void on_event(const SDL_Event *e)
{
    if (!win) return;
    switch (e->type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: quit = 1; break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED: focused = 1; break;
    case SDL_EVENT_WINDOW_FOCUS_LOST: focused = 0; break;
    case SDL_EVENT_KEY_DOWN:
        if (!e->key.repeat && (e->key.key == SDLK_F11 || (e->key.key == SDLK_RETURN && (e->key.mod & SDL_KMOD_ALT))))
            toggle_fullscreen();
        break;
    default: break;   /* resize / DPI change: the output rect is re-read every present */
    }
}

int video_open(const char *title)
{
    if (win) return 1;
    if (!sdl3_load()) { plat_log("video: SDL3 unavailable - no window"); return 0; }
    if (!sdl3.SDL_InitSubSystem(SDL_INIT_VIDEO)) { plat_log("video: SDL_InitSubSystem(VIDEO) failed: %s", sdl3.SDL_GetError()); return 0; }
    sdl3.SDL_GL_ResetAttributes();
    sdl3.SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    sdl3.SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    sdl3.SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    sdl3.SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    sdl3.SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);   /* the module renders into its own targets (gl.c: cDepthBits 0) */
    sdl3.SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_WindowFlags fl = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    if (cfg.fullscreen) fl |= SDL_WINDOW_FULLSCREEN;   /* borderless desktop fullscreen (no display mode set) */
    win = sdl3.SDL_CreateWindow(title, cfg.win_w, cfg.win_h, fl);
    if (!win) { plat_log("video: SDL_CreateWindow failed: %s", sdl3.SDL_GetError()); sdl3.SDL_QuitSubSystem(SDL_INIT_VIDEO); return 0; }
    if (cfg.win_pos) sdl3.SDL_SetWindowPosition(win, cfg.win_x, cfg.win_y);
    ctx = sdl3.SDL_GL_CreateContext(win);
    if (!ctx || !sdl3.SDL_GL_MakeCurrent(win, ctx)) {
        plat_log("video: no OpenGL 4.5 core context: %s", sdl3.SDL_GetError());
        video_close();
        return 0;
    }
    int ok = 1;
#define X(R, n, A) if (!(*(void **)&gl.n = (void *)sdl3.SDL_GL_GetProcAddress(#n))) { plat_log("video: GL lacks " #n); ok = 0; }
    VIDEO_GL(X)
#undef X
    if (!ok) { video_close(); return 0; }
    if (!sdl3.SDL_GL_SetSwapInterval(cfg.vsync ? 1 : 0)) plat_log("video: swap interval %d: %s", cfg.vsync, sdl3.SDL_GetError());
    sdl3.SDL_SetWindowMinimumSize(win, 320, 240);
    sdl3.SDL_ShowWindow(win);
    sdl3_add_handler(on_event);
    int pw = 0, ph = 0;
    sdl3.SDL_GetWindowSizeInPixels(win, &pw, &ph);
    plat_log("video: OpenGL %s | %s | %s, window %dx%d px", (const char *)gl.glGetString(V_GL_VERSION),
             (const char *)gl.glGetString(V_GL_RENDERER), (const char *)gl.glGetString(V_GL_VENDOR), pw, ph);
    quit = 0;
    focused = 1;
    return 1;
}

void video_close(void)
{
    if (ctx) { sdl3.SDL_GL_MakeCurrent(win, nullptr); sdl3.SDL_GL_DestroyContext(ctx); ctx = nullptr; }
    if (win) { sdl3.SDL_DestroyWindow(win); win = nullptr; sdl3.SDL_QuitSubSystem(SDL_INIT_VIDEO); }
}

/* ---- pacing: 59.94 Hz deadline grid from the vblank number (runtime/video.c pace / pace_frame) */
static const uint64_t VB_NUM = 1001000000ull, VB_DEN = 60;   /* ns per vblank = 1001e9 / 60000 */
static void pace(uint32_t vb)
{
    static uint64_t t0;
    static uint32_t base;
    static int init;
    uint64_t now = sdl3.SDL_GetTicksNS();
    if (!init || vb < base) { t0 = now; base = vb; init = 1; return; }
    uint64_t deadline = t0 + (uint64_t)(vb - base) * VB_NUM / VB_DEN;
    if (cfg.vsync) {   /* the swap is the clock; the grid only holds frames that end > 1 ms early */
        if (now >= deadline) { t0 = now; base = vb; return; }
        uint64_t hold = deadline - 1000000;
        if (now < hold) sdl3.SDL_DelayPrecise(hold - now);
        return;
    }
    if (now < deadline) sdl3.SDL_DelayPrecise(deadline - now);
    else if (now - deadline > 250000000ull) { t0 = now; base = vb; }   /* far behind (loading): no catch-up burst */
}

/* ---- shots */
static void shot(Rq *rq, uint32_t vb, int cw, int ch)
{
    char path[600];
    const char *dir = getenv("ACNB_SHOT_DIR");
    snprintf(path, sizeof path, "%s/shot_%u.png", dir && *dir ? dir : ".", vb);
    int w = 0, h = 0;
    const char *src = "module";
    uint32_t *px = nullptr;
    if (rq && rq_output(rq, &w, &h) && w > 0 && h > 0) {   /* the module's picture at its internal resolution */
        px = (uint32_t *)malloc((size_t)w * h * 4);
        if (px) rq_read_output(rq, px, w, h, w);
    } else if (win && cw > 0 && ch > 0) {   /* no output (display off / empty frame): the window's back buffer */
        w = cw; h = ch; src = "window";
        px = (uint32_t *)malloc((size_t)w * h * 4);
        if (px) {
            gl.glBindFramebuffer(V_GL_FRAMEBUFFER, 0);
            gl.glReadBuffer(V_GL_BACK);
            gl.glPixelStorei(V_GL_PACK_ALIGNMENT, 4);
            gl.glReadPixels(0, 0, w, h, V_GL_BGRA, V_GL_UNSIGNED_BYTE, px);
            for (int y = 0; y < h / 2; y++)   /* GL rows are bottom-up */
                for (int x = 0; x < w; x++) {
                    uint32_t t = px[(size_t)y * w + x];
                    px[(size_t)y * w + x] = px[(size_t)(h - 1 - y) * w + x];
                    px[(size_t)(h - 1 - y) * w + x] = t;
                }
        }
    }
    if (!px) { printf("video: vblank %u shot: no picture (display off, no window)\n", vb); return; }
    int ok = png_write_bgra(path, px, w, h, w);
    free(px);
    printf("video: vblank %u -> %s (%dx%d, %s)%s\n", vb, path, w, h, src, ok ? "" : " WRITE FAILED");
}

int video_vblank(Rq *rq, uint32_t vblank, int kind)
{
    if (win) sdl3_pump();
    if (rq) rq_present(rq, vblank, nullptr, &last);
    /* widescreen (widescreen_vblank): present the finished frame at the shape it was drawn for, set the next one's */
    float frame_aspect = shown;
    float ws = (4.0f / 3.0f) / cfg.aspect;
    if (ws != 1.0f) {
        int wide = kind == VIDEO_FRAME_BATTLE && rq && win;   /* module picture only; menus stay 4:3 (video.menus) */
        shown = wide ? cfg.aspect : 4.0f / 3.0f;
        if (rq) rq_set_widescreen(rq, wide ? ws : 1.0f, 1.0f, 2 /* hud anchored */, kind == VIDEO_FRAME_BATTLE);
    }
    int want_shot = 0;
    for (int i = 0; i < cfg.nshots; i++) want_shot |= cfg.shots[i] == vblank;
    if (win) {
        int cw = 0, ch = 0;
        sdl3.SDL_GetWindowSizeInPixels(win, &cw, &ch);
        int dw = cw, dh = ch, rw = 0, rh = 0;
        if (cw > ch * frame_aspect) dw = (int)(ch * frame_aspect + 0.5f); else dh = (int)(cw / frame_aspect + 0.5f);
        if (rq && rq_output(rq, &rw, &rh) && cw > 0 && ch > 0)
            rq_blit_output(rq, rw, rh, (cw - dw) / 2, (ch - dh) / 2, dw, dh, cw, ch, cfg.nearest);
        else if (cw > 0 && ch > 0) {
            gl.glBindFramebuffer(V_GL_FRAMEBUFFER, 0);
            gl.glDisable(V_GL_SCISSOR_TEST);
            gl.glColorMask(1, 1, 1, 1);
            gl.glViewport(0, 0, cw, ch);
            gl.glClearColor((cfg.clear_rgb >> 16 & 255) / 255.0f, (cfg.clear_rgb >> 8 & 255) / 255.0f, (cfg.clear_rgb & 255) / 255.0f, 1.0f);
            gl.glClear(V_GL_COLOR_BUFFER_BIT);
        }
        if (want_shot) shot(rq, vblank, cw, ch);
        sdl3.SDL_GL_SwapWindow(win);
        if (!cfg.fast) pace(vblank);
    } else if (want_shot) shot(rq, vblank, 0, 0);
    int r = 0;
    if (quit) r |= VIDEO_QUIT;
    if (cfg.exit_after && vblank >= cfg.exit_after) r |= VIDEO_EXIT_AFTER;
    return r;
}

int video_pump(void)
{
    if (win) sdl3_pump();
    return quit;
}

void video_summary(Rq *rq)
{
    if (!rq) return;
    static char b[4096];
    rq_summary(rq, b, sizeof b);
    fputs(b, stdout);
    if (*b && b[strlen(b) - 1] != '\n') fputc('\n', stdout);
    fflush(stdout);
}
