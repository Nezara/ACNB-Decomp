/* The vblank step: the only place the source port's time moves. One
 * function, step(), in a fixed order, reached two ways like the hybrid's vblank:
 *   ps2_vsync_wait (sceGsSyncV): the caller spins through one vblank (ps2k_busy_vblank) - the original
 *       (libkernl 0x120e30) clears I_STAT.VBON with interrupts off, re-enables them and polls I_STAT, so the timer 0
 *       compares of the wait are taken at their own times (hybrid: hw.c's I_STAT poll skips event by event); the
 *       handler readies no thread, so no other EE thread runs during the wait;
 *   kernel idle (no thread ready, port/platform/kern): ps2k's idle hook.
 * step(), = hw.c hw_update at the next vblank start + hw_vblank_start + runtime/video.c video_vblank:
 *   1. EE time -> the next vblank start (n * VBLANK_CYCLES), each timer 0 compare at its own time with its
 *      interrupt (timer0_handler) there, as in both hybrid paths (kernel.c schedule, hw.c I_STAT poll)
 *   2. sndhost::AdvanceEe(EE time), then the B6 EE RPC wake hook (hybrid: iop_advance runs first in hw_update, so
 *      the IOP's trace lines of this step still carry the old vblank number)
 *   3. vblank count + 1 (the trace clock of the disc / pad / snd traces), CSR.FIELD toggle
 *   4. video_vblank: rq_present, present, shot, swap, 59.94 Hz pacing (host time paces only)
 *   5. pad poll (= runtime/video.c poll_input): a --replay drives both pads (platform/replay, live input off);
 *      else the live pads (window only: headless runs latch nothing, like the hybrid without a driver), rumble
 *      out; --record logs the polled frames. Live pad choice: `--set input.gamepad=N / keyboard / background / pad2 /
 *      deadzone / trigger / rumble` (padin.h)
 *   6. VIDEO_QUIT / VIDEO_EXIT_AFTER -> sp_exit
 * B7: the wait used to take the hw.c GS CSR path (one skip, at most one compare interrupt at the vblank), which
 * lost a timer0 tick per waited frame: the first Draw_FlipBufferTearing read 93751 instead of 153751 (< 0x25890 ->
 * one more vsync), the boot ran +1 / +2 vblanks behind the hybrid. */
#include "sp_vsync.h"
#include <stdio.h>
#include <stdlib.h>
#include "../../port/platform/kern/ps2_kern.h"
#include "../../port/platform/kern/ps2_clock.h"
#include "../../port/platform/video/video.h"
#include "../../port/platform/snd/sndhost.h"
#include "../../port/platform/padin.h"
#include "../../port/platform/ps2_pad.h"
#include "../../port/platform/replay.h"
#include <string.h>
#include <io.h>
#include "../../port/platform/plat.h"
#include "../../port/platform/audio/audiodev.h"
#include "../../port/platform/sandbox/sandbox.h"
#include "../../port/platform/statehash/statehash.h"
extern "C" void sbx_sourceport_init(void);   /* host_sbx.cpp */
extern "C" int sp_unlock_parts;   /* host_opt.cpp: 1 by default, a replay header may set 0 */

extern "C" Rq *acnb_rq(void);   /* host_rq.cpp */

namespace {

uint32_t g_vb;              /* = the hybrid's vblank_count */
int g_live;                 /* window open and no replay: live pad input */
FILE *g_rec;                /* --record */
SpEeWakeFn g_wake;
void *g_wake_user;

void irq_now(int cause) { ps2k_intc_raise(cause); }

/* --set input.* overrides (sp_input_set), in command-line order */
typedef int *(*InField)(PadinConfig *);
const InField in_fields[] = {
    [](PadinConfig *c) { return &c->gamepad; }, [](PadinConfig *c) { return &c->keyboard; },
    [](PadinConfig *c) { return &c->background; }, [](PadinConfig *c) { return &c->pad2; },
    [](PadinConfig *c) { return &c->deadzone; }, [](PadinConfig *c) { return &c->trigger; },
    [](PadinConfig *c) { return &c->rumble; }};
struct { InField field; int v; } g_in[16];
int g_nin;

void poll_pad()   /* runtime/video.c poll_input (replay and live arms) */
{
    static const PadFrame neutral = {0, 0x80, 0x80, 0x80, 0x80};
    PadFrame p[2] = {neutral, neutral};
    int on2 = 0;
    if (g_live) {
        on2 = padin_poll2(video_focused(), p);
        for (int port = 0; port < 2; port++) {
            uint8_t act[6];
            ps2pad_get_act((uint32_t)port, act, 0);
            padin_rumble(port, act[0], act[1]);
        }
    }
    if (replay_active()) replay_frame(g_vb, p, &on2);   /* both pads (and pad 2's plug state) from the replay alone */
    sbx_vblank(g_vb, &video_config()->exit_after);   /* the poke point (= the hybrid): v3 events, then freezes */
    if (!on2) p[1] = neutral;   /* an unplugged pad 2 is neutral (the p2 line format relies on it) */
    if (g_rec) replay_rec_frame(g_rec, g_vb, p, on2);
    ps2pad_set_connected(1, on2);
    ps2pad_latch(0, &p[0]);
    ps2pad_latch(1, &p[1]);
}

/* the replay's settings header against Stage 0's fixed rules (the original game, host_opt.cpp). Keys of the hybrid's
 * simcfg.c; a different value is reported, the run keeps Stage 0's rules - except game.unlock_parts, which is played
 * as recorded (user s103: default 1, replays keep their own setting). */
int sim_header(const char *line)
{
    static const char tag[] = "#acnb-sim";
    if (strncmp(line, tag, sizeof tag - 1)) return 0;
    static const struct { const char *k, *v; } fixed[] = {
        {"video.culling", "original"}, {"game.versus_garage", "0"}, {"versus.all_maps", "0"},
        {"versus.quick", "0"}};
    const char *p = line + sizeof tag - 1;
    int ver = (int)strtol(p, (char **)&p, 10), len;
    char tok[160];
    fprintf(stderr, "replay: settings header v%d:%s", ver, p);
    while (sscanf(p, " %159s%n", tok, &len) == 1) {
        p += len;
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        if (!strcmp(tok, "game.unlock_parts")) {
            sp_unlock_parts = atoi(eq + 1) != 0;
            fprintf(stderr, "replay: game.unlock_parts=%d (played as recorded)\n", sp_unlock_parts);
            continue;
        }
        unsigned i = 0;
        while (i < sizeof fixed / sizeof *fixed && strcmp(fixed[i].k, tok)) i++;
        if (i == sizeof fixed / sizeof *fixed)
            fprintf(stderr, "replay: settings header key %s: the source port has no such setting yet (ignored)\n", tok);
        else if (strcmp(fixed[i].v, eq + 1))
            fprintf(stderr, "replay: settings header %s=%s, Stage 0 plays %s (fixed): the run may diverge\n", tok, eq + 1, fixed[i].v);
    }
    return 1;
}

void step()
{
    /* 1. every compare at its own time, handler there (idle and vsync wait alike) */
    uint64_t target = (uint64_t)(g_vb + 1) * PS2C_VBLANK_CYCLES;
    ps2c_advance_to(target, 0, irq_now);
    /* 2. */
    if (sndhost::Ready()) sndhost::AdvanceEe(ps2c_now());
    if (g_wake) g_wake(g_wake_user);
    /* 3. */
    g_vb++;
    ps2c_toggle_field();
    /* 4. */
    int f = video_vblank(acnb_rq(), g_vb, VIDEO_FRAME_MENU);   /* milestone 1: menus only (Hor+ battle shape later) */
    /* 5. */
    if (g_live || replay_active()) poll_pad();
    /* 5c. logical state hash at the poke point, unconditionally */
    if (statehash_on()) { if (!sbx_backend()) sbx_sourceport_init(); statehash_vblank(g_vb); }
    /* 6. */
    if (f & VIDEO_QUIT) {
        fprintf(stderr, "video: window closed at vblank %u\n", g_vb);
        sp_exit(0);
    }
    if (f & VIDEO_EXIT_AFTER) {
        fprintf(stderr, "video: exit after vblank %u\n", g_vb);
        sp_exit(sbx_exit_code());   /* 3 = a v3 replay event failed, 2 = sandbox replay refused (= the hybrid) */
    }
}

int idle_hook(void *) { step(); return 1; }

}  // namespace

extern "C" uint32_t sp_vblank(void) { return g_vb; }
/* Stage 0's fixed sim rules in the hybrid's simcfg_format form (replay header) */
extern "C" const char *sp_sim_settings(void)
{
    static char buf[128];
    snprintf(buf, sizeof buf, "1 video.culling=original game.unlock_parts=%d game.versus_garage=0 versus.all_maps=0 versus.quick=0",
             sp_unlock_parts);
    return buf;
}
extern "C" const uint32_t *sp_vblank_ptr(void) { return &g_vb; }
extern "C" void sp_vsync_set_ee_wake(SpEeWakeFn fn, void *user) { g_wake = fn; g_wake_user = user; }

extern "C" void sp_vsync_init(int headless)
{
    disc_trace_set_clock(&g_vb);
    ps2k_set_idle_hook(idle_hook, 0);
    g_live = !headless && video_gl_current() && !replay_active();   /* off while a replay drives */
    if (g_live) {   /* padin defaults + --set input.* (the [input] keys of acnb.ini come with the settings port) */
        PadinConfig c;
        padin_config_default(&c);
        for (int k = 0; k < g_nin; k++) *g_in[k].field(&c) = g_in[k].v;
        padin_configure(&c, video_key_down, 0);
        char gp[64];
        if (c.gamepad) snprintf(gp, sizeof gp, "gamepad %d%s", c.gamepad, c.gamepad > 1 ? " (skips the first listed)" : "");
        else snprintf(gp, sizeof gp, "no gamepad");
        fprintf(stderr, "input: player 1 = %s%s%s, player 2 %s\n", gp,
                c.gamepad ? (c.background ? " (also unfocused)" : " (focused only)") : "",
                c.keyboard ? " + keyboard (focused only)" : "", c.pad2 ? "= next gamepad" : "off");
    }
}

/* --set input.X=N (padin.h PadinConfig): applied by sp_vsync_init over the defaults */
extern "C" int sp_input_set(const char *kv)
{
    static const struct { const char *k; } names[] = {{"gamepad"}, {"keyboard"}, {"background"}, {"pad2"},
                                                      {"deadzone"}, {"trigger"}, {"rumble"}};
    if (strncmp(kv, "input.", 6)) return 0;
    const char *eq = strchr(kv, '=');
    if (!eq || g_nin >= (int)(sizeof g_in / sizeof *g_in)) return 0;
    for (unsigned i = 0; i < sizeof names / sizeof *names; i++)
        if ((size_t)(eq - kv - 6) == strlen(names[i].k) && !strncmp(kv + 6, names[i].k, eq - kv - 6)) {
            char *end;
            long v = strtol(eq + 1, &end, 10);
            if (end == eq + 1 || *end || v < 0) return 0;
            g_in[g_nin].field = in_fields[i];
            g_in[g_nin++].v = (int)v;
            return 1;
        }
    return 0;
}

extern "C" int sp_input_replay(const char *path)
{
    int hdr, r = replay_load(path, sim_header, &hdr);
    if (r == REPLAY_NO_FILE) { fprintf(stderr, "video: cannot open replay %s\n", path); return 0; }   /* hybrid: not fatal */
    if (r == REPLAY_NOT_V2) {
        fprintf(stderr, "video: replay %s is not in the 2-pad format v2 (line 1 \"#acnb-replay 2\"): convert it with "
                        "the development version's scripts/replay_v2.py\n", path);
        return 2;
    }
    if (replay_version() == 3) {   /* sandbox replay (platform/sandbox/events.cpp): refused = exit 2 */
        sbx_sourceport_init();
        if (!sbx_replay_load(path)) return 2;
    }
    uint32_t p2vb;
    int p2on;
    if (!hdr) sp_unlock_parts = 0;   /* recorded before settings headers = unlock_parts 0 (as the development version plays it) */
    fprintf(stderr, "replay: %s, %d pad-1 lines%s\n", path, replay_nrows(0), hdr ? "" : " (no settings header)");
    if (replay_p2_first(&p2vb, &p2on))
        fprintf(stderr, "replay: %d pad-2 lines (pad 2 first plugged-in state %d at vblank %u)\n", replay_nrows(1), p2on, p2vb);
    return 0;
}

extern "C" void sp_input_record(const char *f)   /* never overwrite a replay: F -> F-2, F-3.. (= the hybrid) */
{
    const char *dot = strrchr(f, '.');
    int stem = dot && !strpbrk(dot, "\\/") ? (int)(dot - f) : (int)strlen(f);
    char path[600];
    snprintf(path, sizeof path, "%s", f);
    for (int k = 2; _access(path, 0) == 0; k++) snprintf(path, sizeof path, "%.*s-%d%s", stem, f, k, f + stem);
    g_rec = fopen(path, "w");
    if (!g_rec) { fprintf(stderr, "replay: cannot write %s\n", path); return; }
    fprintf(stderr, "recording input to %s\n", path);
    /* the source port's fixed rules (sim_header) in the hybrid's simcfg_format form */
    if (sbx_rec_start(g_rec, sp_sim_settings())) fprintf(stderr, "replay: recording sandbox format 3 (header at the first poke point)\n");
    else replay_rec_header(g_rec, sp_sim_settings());
}

/* sceGsSyncV's wait (acnb/port.h): exactly one vblank step per call (survey 7: the product counts vblanks at waits,
 * the hybrid at hw events - equal only if every wait is one vblank, so a different advance is reported) */
extern "C" int ps2_vsync_wait(void)
{
    unsigned before = ps2k_vblank_count();
    ps2k_busy_vblank();
    unsigned d = ps2k_vblank_count() - before;
    if (d != 1) fprintf(stderr, "vsync: the wait at vblank %u advanced %u vblanks (expected 1)\n", g_vb, d);
    return 0;
}

extern "C" [[noreturn]] void sp_exit(int code)
{
    if (g_live) padin_rumble_off();
    if (g_rec) fclose(g_rec);
    audiodev_close();   /* device stopped before the window goes; WAV dumps finalised */
    video_summary(acnb_rq());
    disc_trace_flush();
    video_close();
    fflush(stdout);
    fflush(stderr);
    exit(code);
}
