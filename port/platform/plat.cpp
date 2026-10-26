/* Platform log sink + disc / file trace (ACNB_DISC_TRACE, phase 3 S1a; moved here from runtime/cdvd.c in S1b) + pad trace
 * (ACNB_PAD_TRACE, phase 3 step 3 P1) + sound trace / record (ACNB_SND_TRACE / ACNB_SND_REC, phase 3 step 5 N1). */
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void (*log_fn)(const char *fmt, va_list ap);

extern "C" void plat_set_log(void (*fn)(const char *fmt, va_list ap)) { log_fn = fn; }

extern "C" void plat_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (log_fn) log_fn(fmt, ap);
    else { vfprintf(stderr, fmt, ap); fputc('\n', stderr); }
    va_end(ap);
}

/* Trace channels: ACNB_DISC_TRACE (phase 3 S1a) and ACNB_PAD_TRACE (phase 3 step 3 P1). Same clock, own file each.
 * Flushed by fflush(0) on the exit-after path, the CRT on exit(), and disc_trace_flush on WM_CLOSE. */
extern "C" { int disc_trace_on = -1; int pad_trace_on = -1; int snd_trace_on = -1; int snd_rec_on = -1; int mc_trace_on = -1; }
static const uint32_t *trace_clock;

struct TraceChan { int *on; const char *env, *what; FILE *f; void (*sink)(const char *line); };
static TraceChan disc_ch = {&disc_trace_on, "ACNB_DISC_TRACE", "disc", 0, 0};
static TraceChan pad_ch = {&pad_trace_on, "ACNB_PAD_TRACE", "pad", 0, 0};
static TraceChan snd_ch = {&snd_trace_on, "ACNB_SND_TRACE", "snd", 0, 0};
static TraceChan mc_ch = {&mc_trace_on, "ACNB_MC_TRACE", "mc", 0, 0};

static int chan_enabled(TraceChan *t)
{
    if (*t->on < 0) {
        const char *e = getenv(t->env);
        *t->on = 0;
        if (e && *e) {
            t->f = fopen(e, "w");
            if (t->f) *t->on = 1;
            else plat_log("plat: cannot open %s trace %s", t->what, e);
        }
    }
    return *t->on;
}

static void chan_vline(TraceChan *t, const char *side, const char *fmt, va_list ap)
{
    if (t->sink) {   /* in-process capture (sourceport selftests); a trace file gets the same line */
        char line[1024];
        int k = snprintf(line, sizeof line, "%u %s ", trace_clock ? *trace_clock : 0u, side);
        va_list aq;
        va_copy(aq, ap);
        if (k > 0 && k < (int)sizeof line) vsnprintf(line + k, sizeof line - k, fmt, aq);
        va_end(aq);
        t->sink(line);
    }
    if (t->f) {
        fprintf(t->f, "%u %s ", trace_clock ? *trace_clock : 0u, side);
        vfprintf(t->f, fmt, ap);
        fputc('\n', t->f);
    }
}

extern "C" void disc_trace_set_clock(const uint32_t *vblank) { trace_clock = vblank; }
extern "C" uint32_t disc_trace_clock(void) { return trace_clock ? *trace_clock : 0u; }

extern "C" int disc_trace_enabled(void) { return chan_enabled(&disc_ch); }

extern "C" void disc_trace_set_sink(void (*fn)(const char *line))
{
    disc_trace_enabled();   /* ACNB_DISC_TRACE decided first: the file keeps working alongside the sink */
    disc_ch.sink = fn;
    disc_trace_on = fn || disc_ch.f;
}

extern "C" void disc_trace(const char *side, const char *fmt, ...)
{
    if (!disc_trace_enabled()) return;
    va_list ap;
    va_start(ap, fmt);
    chan_vline(&disc_ch, side, fmt, ap);
    va_end(ap);
}

extern "C" int pad_trace_enabled(void) { return chan_enabled(&pad_ch); }

extern "C" void pad_trace_set_sink(void (*fn)(const char *line))
{
    pad_trace_enabled();
    pad_ch.sink = fn;
    pad_trace_on = fn || pad_ch.f;
}

extern "C" void pad_trace(const char *side, const char *fmt, ...)
{
    if (!pad_trace_enabled()) return;
    va_list ap;
    va_start(ap, fmt);
    chan_vline(&pad_ch, side, fmt, ap);
    va_end(ap);
}

extern "C" int snd_trace_enabled(void) { return chan_enabled(&snd_ch); }

extern "C" void snd_trace(const char *side, const char *fmt, ...)
{
    if (!snd_trace_enabled()) return;
    va_list ap;
    va_start(ap, fmt);
    chan_vline(&snd_ch, side, fmt, ap);
    va_end(ap);
}

extern "C" int mc_trace_enabled(void) { return chan_enabled(&mc_ch); }

extern "C" void mc_trace(const char *side, const char *fmt, ...)
{
    if (!mc_trace_enabled()) return;
    va_list ap;
    va_start(ap, fmt);
    chan_vline(&mc_ch, side, fmt, ap);
    va_end(ap);
    if (mc_ch.f) fflush(mc_ch.f);   /* low volume: flush per line so a killed run (sourceport boot spin) keeps it */
}

/* ACNB_SND_REC: binary record file, "ACNBSREC" + u32 version, then records (plat.h). */
static FILE *snd_rec_f;

extern "C" int snd_rec_enabled(void)
{
    if (snd_rec_on < 0) {
        const char *e = getenv("ACNB_SND_REC");
        snd_rec_on = 0;
        if (e && *e) {
            snd_rec_f = fopen(e, "wb");
            if (snd_rec_f) {
                const uint32_t ver = 1;
                fwrite("ACNBSREC", 1, 8, snd_rec_f);
                fwrite(&ver, 4, 1, snd_rec_f);
                snd_rec_on = 1;
            } else plat_log("plat: cannot open snd record %s", e);
        }
    }
    return snd_rec_on;
}

extern "C" void snd_rec(const char tag[4], const uint32_t *hdr, int nhdr, const void *a, uint32_t na, const void *b, uint32_t nb)
{
    if (!snd_rec_enabled()) return;
    uint32_t h[3];
    memcpy(&h[0], tag, 4);
    h[1] = trace_clock ? *trace_clock : 0u;
    h[2] = (uint32_t)nhdr * 4 + (a ? na : 0) + (b ? nb : 0);
    fwrite(h, 4, 3, snd_rec_f);
    if (nhdr) fwrite(hdr, 4, (size_t)nhdr, snd_rec_f);
    if (a && na) fwrite(a, 1, na, snd_rec_f);
    if (b && nb) fwrite(b, 1, nb, snd_rec_f);
}

extern "C" void disc_trace_flush(void)
{
    if (disc_ch.f) fflush(disc_ch.f);
    if (pad_ch.f) fflush(pad_ch.f);
    if (snd_ch.f) fflush(snd_ch.f);
    if (mc_ch.f) fflush(mc_ch.f);
    if (snd_rec_f) fflush(snd_rec_f);
}

extern "C" uint32_t disc_crc32(const void *p, uint32_t n)
{
    static uint32_t tab[256];
    if (!tab[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            tab[i] = c;
        }
    const uint8_t *b = (const uint8_t *)p;
    uint32_t c = 0xffffffffu;
    while (n--) c = tab[(c ^ *b++) & 0xff] ^ (c >> 8);
    return ~c;
}
