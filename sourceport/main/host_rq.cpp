/* Platform side of the render module (port/render, design sections 4.2 and 13, step 1.4): rq_host.h for the source
 * port, the one Rq instance, and the game-side entry points of acnb/port.h (tags, kick / privileged-register
 * stand-ins). GL backend when the window exists (port/platform/video, opened before the first acnb_rq): rq_gl_proc /
 * rq_gl_current forward to its SDL3 GL 4.5 context. Without a window (selftests, --headless) the null backend: the
 * module keeps records and its VRAM model only. The module is compiled with RQ_HOST_ADDR:
 * packet addresses are 32-bit host pointers (i686; PS2_SPR packets live in g_ps2_spr), rq_mem is the identity. */
#include "../../port/render/rq.h"
#include "../../port/render/rq_host.h"
#include "../../port/platform/video/video.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>   /* _mkdir: ACNB_RQ_DUMP_DIR (rq_glue.c CreateDirectoryA parity) */

extern "C" unsigned long long acnb_host_ns(void);   /* host_time.cpp */

extern "C" uint8_t *rq_mem(uint32_t addr, uint32_t len)
{
    if (addr < 0x10000u || (uint64_t)addr + len > 0x100000000ull) return nullptr;   /* null page / wrap */
    return (uint8_t *)(uintptr_t)addr;
}

extern "C" void rq_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

extern "C" uint64_t rq_clock_ns(void) { return acnb_host_ns(); }
extern "C" void *rq_gl_proc(const char *name) { return video_gl_proc(name); }
extern "C" int rq_gl_current(void) { return video_gl_current(); }
extern "C" void rq_gpu_seg(int begin) { (void)begin; }   /* phase 3: GPU timer with the GL context */
extern "C" void rq_gpu_wait_ns(uint64_t ns) { (void)ns; }

/* the source port's Rq, created on first use (no init hook in the game flow yet) */
static Rq *g_sp_rq;
static int g_sp_rq_backend;
extern "C" int acnb_rq_backend(void) { return g_sp_rq_backend; }   /* RQ_BACKEND_* the Rq was created with */
extern "C" Rq *acnb_rq(void)
{
    if (!g_sp_rq) {
        RqOptions o = {};
        o.scale = video_config()->scale;   /* video.scale (1 = the original picture, the check pin) */
        o.backend = g_sp_rq_backend = rq_gl_current() ? RQ_BACKEND_GL : RQ_BACKEND_NULL;
        o.write_readback = 1;   /* rule 11: the module delivers readbacks in the source port */
        /* Stage 0 user tweak (fixed): blur off = the battle blur passes blend nothing and the menu flip copy is an exact
         * opaque copy (no copy blur / flicker filter); consumer-side only, records / hashes / readbacks unchanged */
        o.blur_off = 1;
        const char *h = getenv("ACNB_RQ_HASH");   /* "rq frame N: C records hash H" per vblank (rq_glue.c parity) */
        o.hash = h && *h == '1';
        static unsigned dump_frames[64];   /* ACNB_RQ_DUMP=all | N,N,.. + ACNB_RQ_DUMP_DIR (rq_glue.c parity) */
        const char *d = getenv("ACNB_RQ_DUMP");
        if (d && *d) {
            if (!strcmp(d, "all")) o.dump_all = 1;
            else {
                char *e = (char *)d;
                while (*e && o.ndump < 64) { dump_frames[o.ndump++] = strtoul(e, &e, 10); if (*e == ',') e++; else if (*e) break; }
                o.dump_frames = dump_frames;
            }
            const char *dir = getenv("ACNB_RQ_DUMP_DIR");
            o.dump_dir = dir && *dir ? dir : "rq";
            _mkdir(o.dump_dir);
        }
        o.keep_records = o.hash || o.dump_all || o.ndump;
        g_sp_rq = rq_create(&o);
    }
    return g_sp_rq;
}

/* acnb/port.h tags (same forwarding as runtime/rq_glue.c) */
extern "C" void acnb_rq_tag(int op, unsigned int addr, unsigned int kind, unsigned int sub, unsigned long long objid)
{
    RqTag t;
    t.kind = (uint16_t)kind; t.sub = (uint16_t)sub; t.flags = 0; t.objid = objid;
    if (op == 0) rq_tag_packet(acnb_rq(), addr, &t);
    else if (op == 1) rq_tag_scope(acnb_rq(), &t);
    else rq_tag_end(acnb_rq());
}
