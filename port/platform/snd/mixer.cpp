/* Native sound mixer (phase 3 step 5, NI2), see mixer.h. Every kernel below is a literal copy of runtime/spu2.c
 * (the reference; it stays unchanged in the hybrid): keep the integer types, the operation order and the clamp points
 * exactly as there - the L1 check (--spu-selftest) compares the WAV md5 with the emulator's run. What spu2.c had as
 * register-file plumbing (hi / lo halves, KON / KOFF latches, misc registers, IOP pointers, the IRQ line) is not here:
 * the producer passes whole values and gets events through the callback. */
#include "mixer.h"
#include <stdlib.h>
#include <string.h>

typedef int16_t s16;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;
typedef int64_t s64;
typedef uint64_t u64;
typedef uint8_t u8;

#define RAM_MASK 0xfffffu

extern "C" const int16_t snd_mixer_gauss[512] = {
#include "mixer_gauss.inc"
};
extern "C" const int16_t snd_mixer_reverb_fir[39] = {-1, 0, 2, 0, -10, 0, 35, 0, -103, 0, 266, 0, -616, 0, 1332, 0, -2960, 0,
                                                     10246, 16384, 10246, 0, -2960, 0, 1332, 0, -616, 0, 266, 0, -103, 0,
                                                     35, 0, -10, 0, 2, 0, -1};
#define spu_gauss snd_mixer_gauss
#define rfir snd_mixer_reverb_fir

namespace {

struct Env { s32 level; u32 counter; };

struct Voice {
    u16 voll, volr, pitch, adsr1, adsr2;
    Env env, svl, svr;               /* ADSR envelope, volume sweeps */
    int phase;                       /* 0 off, 1 attack, 2 decay, 3 sustain, 4 release */
    u32 ssa, lsa, nax;               /* halfword addresses; nax = block being played */
    int lsa_custom, lsa_written;
    u32 counter;                     /* pitch counter: bits 12+ = sample in block, 4-11 interpolation index */
    s16 s[3 + 28];                   /* 3 samples of the previous block + current decoded block */
    s16 h1, h2;                      /* ADPCM predictor history */
    u8 flags;                        /* current block's loop flags */
    s32 out;                         /* last output (after ADSR) for pitch modulation */
    s32 vxl, vxr;
};

struct Core {
    Voice v[24];
    u32 pmon, non, vmixl, vmixel, vmixr, vmixer, endx;
    u16 mmix, attr, admas, statx;
    u32 irqa, tsa, esa, eea;
    u32 rv[22];                      /* reverb address regs (relative offsets) */
    u16 mvoll, mvolr;
    Env mvl, mvr;
    s16 evoll, evolr, avoll, avolr, bvoll, bvolr;
    s16 rc[10];                      /* IIR, COMB1-4, WALL, APF1, APF2, IN_L, IN_R */
    u32 rpos;
    s16 rin_l[64], rin_r[64];        /* reverb resamplers */
    s16 rout_l[64], rout_r[64];
    int rin_p, rout_p, rphase;
    int dma_busy, dma_pending_done;  /* DMA / AutoDMA */
    u32 adma_madr, adma_left;
    int adma_on;
    s16 fifo[2][2][256];             /* [block][L/R][sample] */
    int fifo_n, fifo_pos;            /* blocks queued, read position in the front block */
    s32 noise_timer;                 /* noise */
    u16 noise_level;
};

} // namespace

struct SndMixer {
    u16 ram[0x100000];               /* 2 MB sound RAM, halfword addressed */
    Core core[2];
    u16 irqinfo;                     /* spu2.c spdif_irqinfo (0x7c2) */
    u64 frames;
    int interp, reverb_on;
    SndMixerEventFn ev;
    SndMixerAdmaFn adma;
    void *user;
};

static void raise(SndMixer *m, int ev, int c) { if (m->ev) m->ev(m->user, ev, c); }

/* ---------------------------------------------------------------- envelopes */

static void env_tick(Env *e, int shift, int stepv, int exp, int dec, int neg, int all_bits)
{
    s32 step = 7 - stepv;
    if (dec ^ neg) step = ~step;
    step = shift < 11 ? step << (11 - shift) : step;
    u32 inc = shift > 11 ? 0x8000u >> (shift - 11) : 0x8000u;
    if (exp && !dec && e->level > 0x6000) {
        if (shift < 10) step >>= 2;
        else if (shift >= 11) inc >>= 2;
        else { step >>= 1; inc >>= 1; }
    } else if (exp && dec) step = (s32)(((s64)step * e->level) >> 15);
    if (!all_bits && inc < 1) inc = 1;
    e->counter += inc;
    if (!(e->counter & 0x8000)) return;
    e->counter = 0;
    s32 l = e->level + step;
    if (!dec) l = l < -0x8000 ? -0x8000 : l > 0x7fff ? 0x7fff : l;
    else if (neg) l = l < -0x8000 ? -0x8000 : l > 0 ? 0 : l;
    else if (l < 0) l = 0;
    e->level = l;
}

static void adsr_tick(Voice *v)
{
    Env *e = &v->env;
    switch (v->phase) {
    case 1: {
        int shift = (v->adsr1 >> 10) & 0x1f, st = (v->adsr1 >> 8) & 3;
        env_tick(e, shift, st, v->adsr1 >> 15, 0, 0, ((shift << 2) | st) == 0x7f);
        if (e->level >= 0x7fff) { e->level = 0x7fff; v->phase = 2; e->counter = 0; }
        break;
    }
    case 2: {
        s32 sl = ((v->adsr1 & 0xf) + 1) * 0x800;
        env_tick(e, (v->adsr1 >> 4) & 0xf, 0, 1, 1, 0, 0);
        if (e->level <= sl) { v->phase = 3; e->counter = 0; }
        break;
    }
    case 3: {
        int shift = (v->adsr2 >> 8) & 0x1f, st = (v->adsr2 >> 6) & 3;
        env_tick(e, shift, st, v->adsr2 >> 15, (v->adsr2 >> 14) & 1, 0, ((shift << 2) | st) == 0x7f);
        break;
    }
    case 4: {
        int shift = v->adsr2 & 0x1f;
        env_tick(e, shift, 0, (v->adsr2 >> 5) & 1, 1, 0, shift == 0x1f);
        if (e->level <= 0) { e->level = 0; v->phase = 0; }
        break;
    }
    }
}

/* Volume parameter: direct mode (bit 15 = 0) or sweep. Returns the current level. */
static s32 vol_tick(u16 reg, Env *e)
{
    if (!(reg & 0x8000)) { e->level = (s32)(s16)(reg << 1); return e->level; }
    int dec = (reg >> 13) & 1, neg = (reg >> 12) & 1, shift = (reg >> 2) & 0x1f, st = reg & 3;
    env_tick(e, shift, st, (reg >> 14) & 1, dec, neg, ((shift << 2) | st) == 0x7f);
    return e->level;
}

/* ---------------------------------------------------------------- IRQ */

static void irq_check(SndMixer *m, u32 a, u32 n)
{
    for (int c = 0; c < 2; c++) {
        Core *k = &m->core[c];
        if (!(k->attr & 0x40) || (m->irqinfo & (4 << c))) continue;
        if (((k->irqa - a) & RAM_MASK) < n) { m->irqinfo |= 4 << c; raise(m, SND_EV_IRQ, c); }
    }
}

/* ---------------------------------------------------------------- voices */

static const int fpos[5] = {0, 60, 115, 98, 122}, fneg[5] = {0, 0, -52, -55, -60};

/* the decode loop of decode_block, on a block that starts at hdr (ram index a); out = 28 samples */
static void adpcm(const u16 *ram, u32 a, s32 *ph1, s32 *ph2, s16 *out)
{
    u16 hdr = ram[a];
    int shift = hdr & 0xf, filt = (hdr >> 4) & 7;
    if (shift > 12) shift = 9;
    if (filt > 4) filt = 4;
    s32 h1 = *ph1, h2 = *ph2;
    for (int i = 0; i < 28; i++) {
        u16 w = ram[(a + 1 + i / 4) & RAM_MASK];
        s32 n = (s16)(((w >> ((i & 3) * 4)) & 0xf) << 12) >> shift;
        n += (h1 * fpos[filt] + h2 * fneg[filt] + 32) >> 6;
        n = n < -0x8000 ? -0x8000 : n > 0x7fff ? 0x7fff : n;
        h2 = h1; h1 = n;
        out[i] = (s16)n;
    }
    *ph1 = h1; *ph2 = h2;
}

static void decode_block(SndMixer *m, Voice *v)
{
    u32 a = v->nax & ~7u & RAM_MASK;
    irq_check(m, a, 8);
    u16 hdr = m->ram[a];
    v->flags = (u8)(hdr >> 8);
    if ((v->flags & 4) && !v->lsa_custom) v->lsa = a;
    v->s[0] = v->s[28]; v->s[1] = v->s[29]; v->s[2] = v->s[30];
    s32 h1 = v->h1, h2 = v->h2;
    adpcm(m->ram, a, &h1, &h2, v->s + 3);
    v->h1 = (s16)h1; v->h2 = (s16)h2;
}

static void next_block(SndMixer *m, Core *k, int vi)
{
    Voice *v = &k->v[vi];
    if (v->flags & 1) {
        k->endx |= 1u << vi;
        v->nax = v->lsa;
        if (!(v->flags & 2)) { v->phase = 4; v->env.level = 0; }
    } else v->nax = (v->nax + 8) & RAM_MASK;
    decode_block(m, v);
}

static void key_on(SndMixer *m, Core *k, int vi)
{
    Voice *v = &k->v[vi];
    v->lsa_custom = v->lsa_written;
    v->lsa_written = 0;
    v->phase = 1;
    v->env.level = 0;
    v->env.counter = 0;
    v->nax = v->ssa & ~7u;
    v->counter = 0;
    v->h1 = v->h2 = 0;
    memset(v->s, 0, sizeof v->s);
    k->endx &= ~(1u << vi);
    decode_block(m, v);
}

static s32 interp(const SndMixer *m, Voice *v)
{
    int p = (int)(v->counter >> 12) + 3, i = (v->counter >> 4) & 0xff;
    s32 s0 = v->s[p - 3], s1 = v->s[p - 2], s2 = v->s[p - 1], s3 = v->s[p];
    switch (m->interp) {
    case 1: {   /* Catmull-Rom between s2 and s3 */
        float t = i / 256.0f;
        float a = -0.5f * s0 + 1.5f * s1 - 1.5f * s2 + 0.5f * s3, b = s0 - 2.5f * s1 + 2.0f * s2 - 0.5f * s3, cc = -0.5f * s0 + 0.5f * s2;
        float r = ((a * t + b) * t + cc) * t + s1;
        return r < -32768 ? -32768 : r > 32767 ? 32767 : (s32)r;
    }
    case 2: return s1 + (((s2 - s1) * i) >> 8);
    default: {
        s32 o = (spu_gauss[0xff - i] * s0) >> 15;
        o += (spu_gauss[0x1ff - i] * s1) >> 15;
        o += (spu_gauss[0x100 + i] * s2) >> 15;
        o += (spu_gauss[i] * s3) >> 15;
        return o;
    }
    }
}

static void noise_tick(Core *k)
{
    int clk = (k->attr >> 8) & 0x3f, step = (clk & 3) + 4, shift = clk >> 2;
    u16 l = k->noise_level;
    int parity = ((l >> 15) ^ (l >> 12) ^ (l >> 11) ^ (l >> 10) ^ 1) & 1;
    k->noise_timer -= step;
    if (k->noise_timer < 0) {
        k->noise_level = (u16)(l * 2 + parity);
        k->noise_timer += 0x20000 >> shift;
        if (k->noise_timer < 0) k->noise_timer += 0x20000 >> shift;
    }
}

/* ---------------------------------------------------------------- reverb */

static inline s32 sat16(s32 x) { return x < -0x8000 ? -0x8000 : x > 0x7fff ? 0x7fff : x; }
static inline s32 mulv(s32 a, s32 v) { return (s32)(((s64)a * v) >> 15); }

static u32 radr(Core *k, u32 off)
{
    u32 lo = k->esa, hi = k->eea, size = hi - lo + 1;
    if (hi <= lo) return k->rpos;
    u32 p = k->rpos + off;
    if (p > hi) p = lo + (p - lo) % size;
    return p & RAM_MASK;
}
#define rrd(k, off) ((s32)(s16)m->ram[radr(k, off)])
#define rwr(k, off, v) do { if ((k)->attr & 0x80) m->ram[radr(k, off)] = (u16)sat16(v); } while (0)

enum { R_APF1_SIZE, R_APF2_SIZE, R_SAME_L_DST, R_SAME_R_DST, R_COMB1_L, R_COMB1_R, R_COMB2_L, R_COMB2_R, R_SAME_L_SRC,
       R_SAME_R_SRC, R_DIFF_L_DST, R_DIFF_R_DST, R_COMB3_L, R_COMB3_R, R_COMB4_L, R_COMB4_R, R_DIFF_L_SRC, R_DIFF_R_SRC,
       R_APF1_L_DST, R_APF1_R_DST, R_APF2_L_DST, R_APF2_R_DST };
enum { C_IIR, C_COMB1, C_COMB2, C_COMB3, C_COMB4, C_WALL, C_APF1, C_APF2, C_IN_L, C_IN_R };

/* spu2.c's rwr is a function: its value (all rrd reads) is computed before the store; the w temporaries keep that */
static void reverb_24k(SndMixer *m, Core *k, s32 lin, s32 rin, s32 *lout, s32 *rout)
{
    u32 *r = k->rv;
    s16 *c = k->rc;
    lin = mulv(lin, c[C_IN_L]);
    rin = mulv(rin, c[C_IN_R]);
    s32 w;
    if (k->attr & 0x80) {
        s32 iir = c[C_IIR], wall = c[C_WALL];
        w = mulv(sat16(lin + mulv(rrd(k, r[R_SAME_L_SRC]), wall) - rrd(k, r[R_SAME_L_DST] - 1)), iir) + rrd(k, r[R_SAME_L_DST] - 1);
        rwr(k, r[R_SAME_L_DST], w);
        w = mulv(sat16(rin + mulv(rrd(k, r[R_SAME_R_SRC]), wall) - rrd(k, r[R_SAME_R_DST] - 1)), iir) + rrd(k, r[R_SAME_R_DST] - 1);
        rwr(k, r[R_SAME_R_DST], w);
        w = mulv(sat16(lin + mulv(rrd(k, r[R_DIFF_R_SRC]), wall) - rrd(k, r[R_DIFF_L_DST] - 1)), iir) + rrd(k, r[R_DIFF_L_DST] - 1);
        rwr(k, r[R_DIFF_L_DST], w);
        w = mulv(sat16(rin + mulv(rrd(k, r[R_DIFF_L_SRC]), wall) - rrd(k, r[R_DIFF_R_DST] - 1)), iir) + rrd(k, r[R_DIFF_R_DST] - 1);
        rwr(k, r[R_DIFF_R_DST], w);
    }
    s32 l = sat16(mulv(c[C_COMB1], rrd(k, r[R_COMB1_L])) + mulv(c[C_COMB2], rrd(k, r[R_COMB2_L])) +
                  mulv(c[C_COMB3], rrd(k, r[R_COMB3_L])) + mulv(c[C_COMB4], rrd(k, r[R_COMB4_L])));
    s32 rr = sat16(mulv(c[C_COMB1], rrd(k, r[R_COMB1_R])) + mulv(c[C_COMB2], rrd(k, r[R_COMB2_R])) +
                   mulv(c[C_COMB3], rrd(k, r[R_COMB3_R])) + mulv(c[C_COMB4], rrd(k, r[R_COMB4_R])));
    s32 t;
    t = rrd(k, r[R_APF1_L_DST] - r[R_APF1_SIZE]); l = sat16(l - mulv(c[C_APF1], t)); rwr(k, r[R_APF1_L_DST], l); l = sat16(mulv(l, c[C_APF1]) + t);
    t = rrd(k, r[R_APF1_R_DST] - r[R_APF1_SIZE]); rr = sat16(rr - mulv(c[C_APF1], t)); rwr(k, r[R_APF1_R_DST], rr); rr = sat16(mulv(rr, c[C_APF1]) + t);
    t = rrd(k, r[R_APF2_L_DST] - r[R_APF2_SIZE]); l = sat16(l - mulv(c[C_APF2], t)); rwr(k, r[R_APF2_L_DST], l); l = sat16(mulv(l, c[C_APF2]) + t);
    t = rrd(k, r[R_APF2_R_DST] - r[R_APF2_SIZE]); rr = sat16(rr - mulv(c[C_APF2], t)); rwr(k, r[R_APF2_R_DST], rr); rr = sat16(mulv(rr, c[C_APF2]) + t);
    *lout = l;
    *rout = rr;
    if (k->eea > k->esa) { k->rpos++; if (k->rpos > k->eea || k->rpos < k->esa) k->rpos = k->esa; }
}

/* 48 kHz in -> (every other sample) 24 kHz reverb -> 48 kHz out */
static void reverb_48k(SndMixer *m, Core *k, s32 wl, s32 wr, s32 *ol, s32 *orr)
{
    k->rin_l[k->rin_p] = (s16)sat16(wl);
    k->rin_r[k->rin_p] = (s16)sat16(wr);
    k->rin_p = (k->rin_p + 1) & 63;
    k->rphase ^= 1;
    if (k->rphase) {
        s32 dl = 0, dr = 0;
        for (int i = 0; i < 39; i++) {
            int p = (k->rin_p - 39 + i) & 63;
            dl += k->rin_l[p] * rfir[i];
            dr += k->rin_r[p] * rfir[i];
        }
        s32 l, r;
        reverb_24k(m, k, sat16(dl >> 15), sat16(dr >> 15), &l, &r);
        k->rout_l[k->rout_p] = (s16)l;
        k->rout_r[k->rout_p] = (s16)r;
    } else {
        k->rout_l[k->rout_p] = 0;
        k->rout_r[k->rout_p] = 0;
    }
    k->rout_p = (k->rout_p + 1) & 63;
    s32 ul = 0, ur = 0;
    for (int i = 0; i < 39; i++) {
        int p = (k->rout_p - 39 + i) & 63;
        ul += k->rout_l[p] * rfir[i];
        ur += k->rout_r[p] * rfir[i];
    }
    *ol = sat16(ul >> 14);
    *orr = sat16(ur >> 14);
}

/* ---------------------------------------------------------------- AutoDMA sound data input */

static void adma_pull(SndMixer *m, int c)
{
    Core *k = &m->core[c];
    while (k->adma_on && k->adma_left && k->fifo_n < 2) {
        int b = k->fifo_n++;
        u32 n = k->adma_left >= 1024 ? 1024 : k->adma_left;
        memset(k->fifo[b], 0, sizeof k->fifo[b]);
        if (m->adma) m->adma(m->user, c, k->adma_madr, k->fifo[b], n);
        /* mirror into the core's input area so IRQ addresses there behave */
        u32 half = (u32)((m->frames / 256) & 1) * 0x100, base = 0x2000u + (u32)c * 0x400u + half;
        memcpy(&m->ram[base], k->fifo[b][0], 512);
        memcpy(&m->ram[base + 0x200], k->fifo[b][1], 512);
        irq_check(m, base, 0x100);
        irq_check(m, base + 0x200, 0x100);
        k->adma_madr += n;
        k->adma_left -= n;
        if (!k->adma_left) { k->adma_on = 0; k->dma_busy = 0; k->statx |= 0x80; raise(m, SND_EV_DMA_DONE, c); }
    }
}

/* ---------------------------------------------------------------- mixing */

static void core_sample(SndMixer *m, int c, s32 extl, s32 extr, s32 *outl, s32 *outr)
{
    Core *k = &m->core[c];
    s32 dl = 0, dr = 0, el = 0, er = 0, prev = 0;
    noise_tick(k);
    for (int vi = 0; vi < 24; vi++) {
        Voice *v = &k->v[vi];
        u32 bit = 1u << vi;
        if (!v->phase) { v->out = 0; prev = 0; v->vxl = v->vxr = 0; continue; }
        s32 smp = (k->non & bit) ? (s16)k->noise_level : interp(m, v);
        adsr_tick(v);
        s32 o = mulv(smp, v->env.level);
        v->out = o;
        u32 step = v->pitch;
        if ((k->pmon & bit) && vi > 0) step = (u32)(((s32)step * (prev + 0x8000)) >> 15) & 0xffff;
        if (step > 0x3fff) step = 0x4000;
        prev = o;
        v->counter += step;
        while ((v->counter >> 12) >= 28) { v->counter -= 28 << 12; next_block(m, k, vi); }
        s32 l = mulv(o, vol_tick(v->voll, &v->svl)), r = mulv(o, vol_tick(v->volr, &v->svr));
        v->vxl = l; v->vxr = r;
        if (k->vmixl & bit) dl += l;
        if (k->vmixr & bit) dr += r;
        if (k->vmixel & bit) el += l;
        if (k->vmixer & bit) er += r;
    }
    /* sound data input (AutoDMA) */
    s32 sl = 0, sr = 0;
    if (k->fifo_n) {
        sl = mulv(k->fifo[0][0][k->fifo_pos], k->bvoll);
        sr = mulv(k->fifo[0][1][k->fifo_pos], k->bvolr);
        if (++k->fifo_pos == 256) {
            k->fifo_pos = 0;
            memmove(k->fifo[0], k->fifo[1], sizeof k->fifo[0]);
            k->fifo_n--;
            adma_pull(m, c);
        }
    }
    s32 ml = mulv(extl, k->avoll), mr = mulv(extr, k->avolr);
    u16 mm = k->mmix;
    s32 dryl = ((mm & 0x800) ? dl : 0) + ((mm & 0x80) ? ml : 0) + ((mm & 0x08) ? sl : 0);
    s32 dryr = ((mm & 0x400) ? dr : 0) + ((mm & 0x40) ? mr : 0) + ((mm & 0x04) ? sr : 0);
    s32 wetl = ((mm & 0x200) ? el : 0) + ((mm & 0x20) ? ml : 0) + ((mm & 0x02) ? sl : 0);
    s32 wetr = ((mm & 0x100) ? er : 0) + ((mm & 0x10) ? mr : 0) + ((mm & 0x01) ? sr : 0);
    s32 rl = 0, rr = 0;
    reverb_48k(m, k, sat16(wetl), sat16(wetr), &rl, &rr);
    if (!m->reverb_on) rl = rr = 0;
    s32 l = sat16(dryl) + mulv(rl, k->evoll), r = sat16(dryr) + mulv(rr, k->evolr);
    l = sat16(mulv(sat16(l), vol_tick(k->mvoll, &k->mvl)));
    r = sat16(mulv(sat16(r), vol_tick(k->mvolr, &k->mvr)));
    *outl = l;
    *outr = r;
}

extern "C" void snd_mixer_mix(SndMixer *m, int16_t *out, int frames)
{
    for (int i = 0; i < frames; i++) {
        s32 l0, r0, l1, r1;
        core_sample(m, 0, 0, 0, &l0, &r0);
        core_sample(m, 1, l0, r0, &l1, &r1);
        if (out) { out[2 * i] = (s16)l1; out[2 * i + 1] = (s16)r1; }
        m->frames++;
    }
}

/* ---------------------------------------------------------------- lifetime / mode */

extern "C" SndMixer *snd_mixer_create(void)
{
    SndMixer *m = (SndMixer *)calloc(1, sizeof(SndMixer));
    if (m) { m->reverb_on = 1; snd_mixer_reset(m); }
    return m;
}
extern "C" void snd_mixer_destroy(SndMixer *m) { free(m); }
extern "C" void snd_mixer_reset(SndMixer *m)
{
    memset(m->core, 0, sizeof m->core);
    for (int c = 0; c < 2; c++) { m->core[c].statx = 0x80; m->core[c].eea = 0xfffffu; }
}
extern "C" void snd_mixer_set_callbacks(SndMixer *m, SndMixerEventFn ev, SndMixerAdmaFn adma, void *user)
{
    m->ev = ev; m->adma = adma; m->user = user;
}
extern "C" void snd_mixer_set_mode(SndMixer *m, int interp_mode, int reverb_out) { m->interp = interp_mode; m->reverb_on = reverb_out; }

/* ---------------------------------------------------------------- commands */

extern "C" void snd_mixer_voice_set(SndMixer *m, int c, int vi, int param, uint16_t val)
{
    Voice *v = &m->core[c & 1].v[vi];
    switch (param) {
    case SND_VP_VOLL: v->voll = val; break; case SND_VP_VOLR: v->volr = val; break; case SND_VP_PITCH: v->pitch = val; break;
    case SND_VP_ADSR1: v->adsr1 = val; break; case SND_VP_ADSR2: v->adsr2 = val; break;
    case SND_VP_ENVX: v->env.level = (s16)val; break;
    }
}

extern "C" void snd_mixer_voice_addr_set(SndMixer *m, int c, int vi, int which, uint32_t addr)
{
    Voice *v = &m->core[c & 1].v[vi];
    addr &= RAM_MASK;
    switch (which) {
    case SND_VA_SSA: v->ssa = addr; break;
    case SND_VA_LSA: v->lsa = addr; v->lsa_written = v->lsa_custom = 1; break;
    case SND_VA_NAX: v->nax = addr; break;
    }
}

static void keys(SndMixer *m, Core *k, u32 bits, int on)
{
    for (int i = 0; i < 24; i++) {
        if (!(bits & (1u << i))) continue;
        if (on) key_on(m, k, i);
        else if (k->v[i].phase) { k->v[i].phase = 4; k->v[i].env.counter = 0; }
    }
}
extern "C" void snd_mixer_key_on(SndMixer *m, int c, uint32_t mask) { keys(m, &m->core[c & 1], mask, 1); }
extern "C" void snd_mixer_key_off(SndMixer *m, int c, uint32_t mask) { keys(m, &m->core[c & 1], mask, 0); }

static u32 *mask_ref(Core *k, int which)
{
    switch (which) {
    case SND_CM_PMON: return &k->pmon; case SND_CM_NON: return &k->non;
    case SND_CM_VMIXL: return &k->vmixl; case SND_CM_VMIXEL: return &k->vmixel;
    case SND_CM_VMIXR: return &k->vmixr; case SND_CM_VMIXER: return &k->vmixer;
    }
    return 0;
}
extern "C" void snd_mixer_core_mask_set(SndMixer *m, int c, int which, uint32_t mask)
{
    u32 *p = mask_ref(&m->core[c & 1], which);
    if (p) *p = mask & 0xffffffu;
}

static u16 *cp_ref(Core *k, int param)
{
    switch (param) {
    case SND_CP_MVOLL: return &k->mvoll; case SND_CP_MVOLR: return &k->mvolr;
    case SND_CP_EVOLL: return (u16 *)&k->evoll; case SND_CP_EVOLR: return (u16 *)&k->evolr;
    case SND_CP_AVOLL: return (u16 *)&k->avoll; case SND_CP_AVOLR: return (u16 *)&k->avolr;
    case SND_CP_BVOLL: return (u16 *)&k->bvoll; case SND_CP_BVOLR: return (u16 *)&k->bvolr;
    case SND_CP_MMIX: return &k->mmix;
    }
    if (param >= SND_CP_REV_IIR && param <= SND_CP_REV_IN_R) return (u16 *)&k->rc[param - SND_CP_REV_IIR];
    return 0;
}
extern "C" void snd_mixer_core_set(SndMixer *m, int c, int param, uint16_t val)
{
    c &= 1;
    Core *k = &m->core[c];
    if (param == SND_CP_ATTR) {
        if (!(val & 0x40)) m->irqinfo &= ~(4 << c);
        if ((k->attr & 0x8000) == 0 && (val & 0x8000)) k->statx |= 0x80;
        k->attr = val;
        return;
    }
    if (param == SND_CP_ADMAS) {
        k->admas = val;
        if (!(val & (c + 1))) { k->fifo_n = 0; k->fifo_pos = 0; if (k->adma_on) { k->adma_on = 0; k->adma_left = 0; } }
        return;
    }
    u16 *p = cp_ref(k, param);
    if (p) *p = val;
}

extern "C" void snd_mixer_core_addr_set(SndMixer *m, int c, int which, uint32_t addr)
{
    Core *k = &m->core[c & 1];
    addr &= RAM_MASK;
    switch (which) {
    case SND_CA_IRQA: k->irqa = addr; break;
    case SND_CA_TSA: k->tsa = addr; break;
    case SND_CA_ESA: k->esa = addr; k->rpos = k->esa; break;
    case SND_CA_EEA: k->eea = addr; break;
    default: if (which >= SND_CA_REV0 && which < SND_CA_COUNT) k->rv[which - SND_CA_REV0] = addr; break;
    }
}

extern "C" void snd_mixer_endx_clear(SndMixer *m, int c, uint32_t mask) { m->core[c & 1].endx &= ~mask; }
extern "C" void snd_mixer_irqinfo_set(SndMixer *m, uint16_t v) { m->irqinfo = v; }

extern "C" void snd_mixer_port_write(SndMixer *m, int c, const uint16_t *hw, uint32_t n)
{
    Core *k = &m->core[c & 1];
    for (u32 i = 0; i < n; i++) { irq_check(m, k->tsa, 1); m->ram[k->tsa] = hw[i]; k->tsa = (k->tsa + 1) & RAM_MASK; }
}
extern "C" uint16_t snd_mixer_port_read(SndMixer *m, int c)
{
    Core *k = &m->core[c & 1];
    u16 v = m->ram[k->tsa];
    k->tsa = (k->tsa + 1) & RAM_MASK;
    return v;
}

static void dma_started(Core *k, u32 size)
{
    k->tsa = (k->tsa + size / 2) & RAM_MASK;
    k->dma_busy = 1;
    k->dma_pending_done = 1;
    k->statx &= ~0x80;
}
extern "C" void snd_mixer_dma_write(SndMixer *m, int c, const void *src, uint32_t size)
{
    Core *k = &m->core[c & 1];
    const u8 *p = (const u8 *)src;
    for (u32 i = 0; i < size / 2; i++) {
        u16 w;
        memcpy(&w, p + 2 * i, 2);
        m->ram[(k->tsa + i) & RAM_MASK] = w;
    }
    irq_check(m, k->tsa, size / 2);
    dma_started(k, size);
}
extern "C" void snd_mixer_dma_read(SndMixer *m, int c, void *dst, uint32_t size)
{
    Core *k = &m->core[c & 1];
    u8 *p = (u8 *)dst;
    for (u32 i = 0; i < size / 2; i++) memcpy(p + 2 * i, &m->ram[(k->tsa + i) & RAM_MASK], 2);
    dma_started(k, size);
}
extern "C" void snd_mixer_adma_start(SndMixer *m, int c, uint32_t madr, uint32_t size)
{
    c &= 1;
    Core *k = &m->core[c];
    k->adma_madr = madr;
    k->adma_left = size;
    k->adma_on = 1;
    k->dma_busy = 1;
    k->statx &= ~0x80;
    adma_pull(m, c);
}

extern "C" void snd_mixer_dma_complete(SndMixer *m, int c)
{
    Core *k = &m->core[c & 1];
    k->dma_pending_done = 0; k->dma_busy = 0; k->statx |= 0x80;
}
extern "C" int snd_mixer_dma_pending(SndMixer *m, int c) { return m->core[c & 1].dma_pending_done; }
extern "C" int snd_mixer_service(SndMixer *m)
{
    int done = 0;
    for (int c = 0; c < 2; c++) {
        if (m->core[c].dma_pending_done) { snd_mixer_dma_complete(m, c); raise(m, SND_EV_DMA_DONE, c); done |= 1 << c; }
    }
    return done;
}

/* ---------------------------------------------------------------- queries */

extern "C" uint64_t snd_mixer_frames(const SndMixer *m) { return m->frames; }

extern "C" uint16_t snd_mixer_voice_get(const SndMixer *m, int c, int vi, int param)
{
    const Voice *v = &m->core[c & 1].v[vi];
    switch (param) {
    case SND_VP_VOLL: return v->voll; case SND_VP_VOLR: return v->volr; case SND_VP_PITCH: return v->pitch;
    case SND_VP_ADSR1: return v->adsr1; case SND_VP_ADSR2: return v->adsr2; case SND_VP_ENVX: return (u16)v->env.level;
    case SND_VP_VOLXL: return (u16)v->vxl; case SND_VP_VOLXR: return (u16)v->vxr;
    }
    return 0;
}
extern "C" uint32_t snd_mixer_voice_addr_get(const SndMixer *m, int c, int vi, int which)
{
    const Voice *v = &m->core[c & 1].v[vi];
    return which == SND_VA_SSA ? v->ssa : which == SND_VA_LSA ? v->lsa : which == SND_VA_NAX ? v->nax : 0;
}
extern "C" int snd_mixer_voice_phase(const SndMixer *m, int c, int vi) { return m->core[c & 1].v[vi].phase; }
extern "C" uint32_t snd_mixer_core_mask_get(const SndMixer *m, int c, int which)
{
    u32 *p = mask_ref((Core *)&m->core[c & 1], which);
    return p ? *p : 0;
}
extern "C" uint16_t snd_mixer_core_get(const SndMixer *m, int c, int param)
{
    const Core *k = &m->core[c & 1];
    switch (param) {
    case SND_CP_ATTR: return k->attr;
    case SND_CP_ADMAS: return k->admas;
    case SND_CP_MVOLXL: return (u16)k->mvl.level;
    case SND_CP_MVOLXR: return (u16)k->mvr.level;
    case SND_CP_STATX: return k->statx;
    }
    u16 *p = cp_ref((Core *)k, param);
    return p ? *p : 0;
}
extern "C" uint32_t snd_mixer_core_addr_get(const SndMixer *m, int c, int which)
{
    const Core *k = &m->core[c & 1];
    switch (which) {
    case SND_CA_IRQA: return k->irqa; case SND_CA_TSA: return k->tsa;
    case SND_CA_ESA: return k->esa; case SND_CA_EEA: return k->eea;
    }
    return which >= SND_CA_REV0 && which < SND_CA_COUNT ? k->rv[which - SND_CA_REV0] : 0;
}
extern "C" uint32_t snd_mixer_endx(const SndMixer *m, int c) { return m->core[c & 1].endx; }
extern "C" uint16_t snd_mixer_irqinfo(const SndMixer *m) { return m->irqinfo; }
extern "C" int snd_mixer_irq_pending_mask(const SndMixer *m) { return (m->irqinfo >> 2) & 3; }
extern "C" int snd_mixer_dma_busy(const SndMixer *m, int c) { return m->core[c & 1].dma_busy; }
extern "C" uint32_t snd_mixer_adma_remaining(const SndMixer *m, int c) { return m->core[c & 1].adma_on ? m->core[c & 1].adma_left : 0; }
extern "C" uint16_t *snd_mixer_ram(SndMixer *m) { return m->ram; }

/* ---------------------------------------------------------------- shared kernels */

extern "C" void snd_mixer_adpcm_decode(const uint16_t blk[8], int32_t *h1, int32_t *h2, int16_t out[28])
{
    /* the block as RAM indices 0..7 (the kernel masks with RAM_MASK; 0..7 stay in range) */
    adpcm(blk, 0, h1, h2, out);
}
extern "C" void snd_mixer_env_step(int32_t *level, uint32_t *counter, int shift, int stepv, int exp, int dec, int neg, int all_bits)
{
    Env e = {*level, *counter};
    env_tick(&e, shift, stepv, exp, dec, neg, all_bits);
    *level = e.level; *counter = e.counter;
}
