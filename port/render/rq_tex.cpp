// Record consumer (design sections 3, 10): the module's VRAM model, write stamps (lo-24 / top byte), surfaces (dirty /
// stale page rows), texture cache, CLUT buffer + deferred queue, batch building, display source resolve, readback
// delivery. Forked from runtime/gs_hw.c (all non-GL logic, same algorithms and order) and gs.c (host->local staging,
// local->local copy, local->host read, CLUT queue), gs_draw.c gs_clut_load. No GL here: GPU work goes through
// rq->be_ops (rq_backend.h); surfaces / textures hold backend ids.
#include "rq_internal.h"
#include "rq_swz.h"
#include <math.h>
#include <stdio.h>

#define BE(op, ...) rq->be_ops->op(rq->be, __VA_ARGS__)

/* Which bytes of a 32-bit word an access touches: 24-bit formats (CT24/Z24) leave the top byte alone and the
 * 8H/4HL/4HH texture formats live only in it (games park HUD textures in the top byte of a Z24 buffer). */
enum { BYTES_LO = 1, BYTES_HI = 2, BYTES_ALL = 3 };
static int psm_bytes(int psm)
{
    switch (psm) {
    case RQ_PSMCT24: case RQ_PSMZ24: return BYTES_LO;
    case RQ_PSMT8H: case RQ_PSMT4HL: case RQ_PSMT4HH: return BYTES_HI;
    default: return BYTES_ALL;
    }
}

/* transfer bits per pixel (gs.c psm_bits: 24-bit formats pack 3 bytes, 8H 1 byte) */
static int tr_bits(int psm)
{
    switch (psm) {
    case RQ_PSMCT32: case RQ_PSMZ32: return 32;
    case RQ_PSMCT24: case RQ_PSMZ24: return 24;
    case RQ_PSMCT16: case RQ_PSMCT16S: case RQ_PSMZ16: case RQ_PSMZ16S: return 16;
    case RQ_PSMT8: case RQ_PSMT8H: return 8;
    default: return 4;
    }
}

struct Surface {
    int used, depth;
    uint32_t bp, bw;
    int psm;
    int w, h, vh;
    uint32_t t;        /* backend target id */
    uint64_t dirty;    /* page rows (32 px for 32-bit, 64 px for 16-bit layouts) newer on the GPU */
    uint64_t stale;    /* page rows where the GPU copy is out of date; dirty & stale == 0, and of all surfaces holding
                          a piece of memory at most one is dirty there and that one is not stale */
    int hi_dirty;      /* some dirty row was drawn with top-byte writes (24-bit targets never touch it) */
    uint32_t prep_epoch;   /* prepare_target fast path */
    uint64_t prep_rows; int prep_bytes;
    uint32_t last_use;
};
#define MAX_SURF 48

struct TexEntry {
    int used;
    uint32_t tbp, tbw; int psm, tw, th;
    uint32_t clut_hash; int cpsm;
    uint32_t stamp, last_use;
    uint32_t t;        /* backend texture id */
};
#define MAX_TEX 512

/* batch key: canonical key fields + palette generation + backend primitive class (gs_hw Key) */
struct BKey { uint64_t f[RQ_KEY_N]; uint32_t clut_gen, cls, snap, spr; };   /* snap: step 5.1 2D layout (0 when off);
                                                                              spr: a sprite record (only with texfilter / aniso) */
#define MAXV 60000
#define CLUT_Q 16

struct TexBind { uint32_t tex; int is_target, amode, glw, glh, rs; };   /* glw/glh in GS texels, rs texels per GS texel */

struct RqTex {
    uint8_t vram[RQ_VRAM_SIZE];
    uint32_t pstamp[2][RQ_GS_BLOCKS / 32], bstamp[2][RQ_GS_BLOCKS], stamp_now;
    Surface surf[MAX_SURF];
    uint32_t use_clock, coh_epoch;
    TexEntry tcache[MAX_TEX];
    uint16_t clut_raw[512];
    uint32_t clut_gen;
    struct { uint64_t tex0, texclut; uint32_t cbp; } clut_q[CLUT_Q];
    int clut_nq;
    BKey cur;
    RqHwVert *vb; uint32_t vb_off, vb_cap; int nv;
    float bb_x0, bb_y0, bb_x1, bb_y1;
    int rs, warned_clip, warned_rb;
    struct RqWide *wide;                 /* step 5.2 HUD anchoring (NULL = off) */
    int ax_on; float ax_fp, ax_ws;       /* x squeeze of the primitive being emitted (put) */
    int ay_on; float ay_fp, ay_s;        /* y scale of the primitive being emitted (camera.fov zoom, 3D only) */
    uint32_t cursor, part_rec, part_done;   /* consumer position; DRAW record consumed up to part_done prims */
    struct {   /* host->local staging (gs.c stg) */
        uint8_t *data; uint32_t cap, len, total;
        uint32_t *px; uint32_t px_cap;
        int active, rows_done, dpsm, bits, rrw, rrh, dx, dy;
        uint32_t dbp, dbw;
    } stg;
    struct { int active, bits, nacc; uint32_t pos, n, cap; uint32_t *px; uint32_t sbp, sbw; int spsm, sx, sy, w, h; uint64_t acc; } rd;   /* local->host (gs.c rd) */
    uint8_t *rbc; uint32_t rbc_n, rbc_cap;   /* both mode: the module's bytes of the last delivery (rq_readback_compare) */
    uint32_t *cp_tmp; uint32_t cp_cap;
    uint32_t *disp_px[2];
    int out_any, out_w, out_h;
    struct { uint32_t flushes, draws, verts, uploads, downloads, decodes, partial, rt_tex, rt_copy, gpu_copies, gs_up, gs_up_bytes; } st;   /* per frame (rqt_frame_stats) */
    /* surface sync causes (run totals, rqt_summary; host only): the operation that triggered each down / up sync */
    int why;
    /* host->local write-through (upload_keep / upload_push): pages the transfer touches / writes completely, and per
     * surface the dirty page rows the transfer leaves GPU-authoritative (keep) and those of them it writes into (push) */
    uint64_t upT[8], upC[8], keep[MAX_SURF], push[MAX_SURF];
    int keep_on;
    uint64_t dn_why[8], up_why[8], dn_px, up_px, dn_ns, up_ns;
    uint32_t xfer[2048 * 1024];
};
enum { W_OTHER, W_TEX, W_CLUT, W_UPLOAD, W_COPY, W_READBACK, W_DRAW, W_DISPLAY };
static const char *const why_name[8] = {"other", "tex", "clut", "upload", "copy", "readback", "draw", "display"};
struct WhyScope { RqTex *X; int old; WhyScope(RqTex *x, int w) : X(x), old(x->why) { x->why = w; } ~WhyScope() { X->why = old; } };

// ---------------------------------------------------------------- write stamps

static void bump(RqTex *X, uint32_t lo, uint32_t hi, int bytes)
{
    X->stamp_now++;
    if (hi > RQ_GS_BLOCKS) hi = RQ_GS_BLOCKS;
    for (int k = 0; k < 2; k++) {
        if (!(bytes >> k & 1)) continue;
        for (uint32_t p = lo >> 5; p < ((hi + 31) >> 5) && p < RQ_GS_BLOCKS / 32; p++) X->pstamp[k][p] = X->stamp_now;
        for (uint32_t b = lo; b < hi; b++) X->bstamp[k][b] = X->stamp_now;
    }
}

static uint32_t max_stamp(RqTex *X, uint32_t lo, uint32_t hi, int bytes)
{
    uint32_t m = 0;
    for (int k = 0; k < 2; k++) {
        if (!(bytes >> k & 1)) continue;
        for (uint32_t p = lo >> 5; p < ((hi + 31) >> 5) && p < RQ_GS_BLOCKS / 32; p++) if (X->pstamp[k][p] > m) m = X->pstamp[k][p];
    }
    return m;
}

static inline uint32_t block_stamp(RqTex *X, uint32_t b, int bytes)
{
    uint32_t a = bytes & BYTES_LO ? X->bstamp[0][b] : 0, c = bytes & BYTES_HI ? X->bstamp[1][b] : 0;
    return a > c ? a : c;
}

static int overlap(uint32_t a0, uint32_t a1, uint32_t b0, uint32_t b1) { return a0 < b1 && b0 < a1; }

// ---------------------------------------------------------------- pixel conversion

static uint32_t to_rgba8(uint32_t c, int psm)
{
    switch (rqs_bits(psm)) {
    case 16: return ((c & 31) << 3) | (((c >> 5) & 31) << 11) | (((c >> 10) & 31) << 19) | ((c & 0x8000) ? 0x80000000u : 0);
    default: return (psm == RQ_PSMCT24 || psm == RQ_PSMZ24) ? (c & 0xffffff) : c;
    }
}

static uint32_t from_rgba8(uint32_t c, int psm)
{
    if (rqs_bits(psm) == 16)
        return ((c >> 3) & 31) | (((c >> 11) & 31) << 5) | (((c >> 19) & 31) << 10) | ((c & 0x80000000u) ? 0x8000 : 0);
    return c;
}

/* Rect of a GS buffer -> RGBA8 (through `pal` for paletted formats, raw for depth). */
static void read_rgba(RqTex *X, int psm, uint32_t bp, uint32_t bw, int x, int y, int w, int h, const uint32_t *pal, int pal_n,
                      int raw, uint32_t *out)
{
    rqs_read_rect(X->vram, psm, bp, bw, x, y, w, h, out, w);
    uint32_t n = (uint32_t)w * (uint32_t)h;
    if (raw) return;
    if (pal_n) { for (uint32_t i = 0; i < n; i++) out[i] = pal[out[i] & (pal_n - 1)]; return; }
    if (rqs_bits(psm) == 16) for (uint32_t i = 0; i < n; i++) out[i] = to_rgba8(out[i], RQ_PSMCT16);
}

// ---------------------------------------------------------------- surfaces

static int surf_wpx(const Surface *s) { int w = (int)s->bw * 64; return w > s->w ? s->w : w ? w : 64; }
static int surf_ph(const Surface *s) { return rqs_bits(s->psm) == 32 ? 32 : 64; }

static uint64_t rows_mask(int r0, int r1)
{
    if (r0 < 0) r0 = 0;
    if (r1 > 64) r1 = 64;
    if (r0 >= r1) return 0;
    return (r1 - r0 >= 64 ? ~0ull : ((1ull << (r1 - r0)) - 1)) << r0;
}

/* page rows of `s` (within its valid height) holding any of blocks [lo,hi) */
static uint64_t surf_rows_in(const Surface *s, uint32_t lo, uint32_t hi)
{
    if (hi <= s->bp || s->vh <= 0) return 0;
    uint32_t ppr = s->bw ? s->bw : 1;
    uint32_t p0 = lo > s->bp ? (lo - s->bp) / 32 : 0, p1 = (hi - s->bp + 31) / 32;
    int ph = surf_ph(s);
    return rows_mask((int)(p0 / ppr), (int)((p1 + ppr - 1) / ppr)) & rows_mask(0, (s->vh + ph - 1) / ph);
}

/* GPU -> VRAM model for the dirty page rows in `mask` */
static void surf_download(Rq *rq, Surface *s, uint64_t mask)
{
    RqTex *X = rq->tex;
    mask &= s->dirty;
    if (!mask) return;
    s->dirty &= ~mask;
    if (!s->dirty) s->hi_dirty = 0;
    X->coh_epoch++;
    int w = surf_wpx(s), ph = surf_ph(s);
    X->st.downloads++;
    X->dn_why[X->why]++;
    for (int r = 0; r < 64;) {
        if (!((mask >> r) & 1)) { r++; continue; }
        int r1 = r;
        while (r1 < 64 && ((mask >> r1) & 1)) r1++;
        int y0 = r * ph, y1 = r1 * ph;
        if (y1 > s->vh) y1 = s->vh;
        if (y1 > y0) {
            uint64_t t0 = rq_clock_ns();
            BE(target_download, s->t, y0, y1, w, X->xfer);
            X->dn_ns += rq_clock_ns() - t0;
            X->dn_px += (uint64_t)w * (uint32_t)(y1 - y0);
            if (!s->depth && rqs_bits(s->psm) == 16)
                for (int i = 0; i < w * (y1 - y0); i++) X->xfer[i] = from_rgba8(X->xfer[i], s->psm);
            rqs_write_rect(X->vram, s->psm, s->bp, s->bw, 0, y0, w, y1 - y0, X->xfer, w, 0, 0);
        }
        r = r1;
    }
}

/* VRAM model -> GPU, rows [y0, y1) */
static void surf_upload(Rq *rq, Surface *s, int y0, int y1)
{
    RqTex *X = rq->tex;
    int w = surf_wpx(s);
    if (y1 > s->h) y1 = s->h;
    if (y1 <= y0) return;
    X->st.uploads++;
    X->up_why[X->why]++; X->up_px += (uint64_t)w * (uint32_t)(y1 - y0);
    read_rgba(X, s->psm, s->bp, s->bw, 0, y0, w, y1 - y0, 0, 0, s->depth, X->xfer);
    uint64_t t0 = rq_clock_ns();
    BE(target_upload, s->t, 0, y0, y1, w, X->xfer);
    X->up_ns += rq_clock_ns() - t0;
}

static void surf_free(Rq *rq, Surface *s)
{
    surf_download(rq, s, ~0ull);
    BE(target_destroy, s->t);
    memset(s, 0, sizeof *s);
    rq->tex->coh_epoch++;
}

/* Make the VRAM model current for blocks [lo,hi) (only the bytes in `bytes` are needed). */
static void sync_vram(Rq *rq, uint32_t lo, uint32_t hi, const Surface *except, int bytes)
{
    for (int i = 0; i < MAX_SURF; i++) {
        Surface *s = &rq->tex->surf[i];
        if (!s->used || s == except || !s->dirty) continue;
        if (bytes == BYTES_HI && !s->hi_dirty) continue;   // its draws never touched the top bytes
        uint64_t m = surf_rows_in(s, lo, hi);
        if (rq->tex->keep_on) m &= ~rq->tex->keep[i];
        surf_download(rq, s, m);
    }
}

/* VRAM changes in [lo,hi) (bytes `bytes` of each word) underneath every surface except `except`. */
static void vram_written(Rq *rq, uint32_t lo, uint32_t hi, const Surface *except, int bytes)
{
    for (int i = 0; i < MAX_SURF; i++) {
        Surface *t = &rq->tex->surf[i];
        if (!t->used || t == except) continue;
        if (bytes == BYTES_HI && t->depth && psm_bytes(t->psm) == BYTES_LO) continue;   // Z24 holds no top byte
        uint64_t tr = surf_rows_in(t, lo, hi);
        if (rq->tex->keep_on) tr &= ~rq->tex->keep[i];
        if (!tr) continue;
        surf_download(rq, t, tr);   // the write may cover the rows only partly
        t->stale |= tr;
        rq->tex->coh_epoch++;
    }
}

/* Same memory, same pixel arrangement: data moves between the two on the GPU. */
static int compat(const Surface *a, const Surface *b)
{
    return a->bp == b->bp && a->bw == b->bw && rqs_layout(a->psm) == rqs_layout(b->psm);
}

/* GPU copy of page rows `rows` from `src` into the compatible surface `dst` (colour <-> depth via a pass). */
static void gpu_copy(Rq *rq, Surface *dst, Surface *src, uint64_t rows)
{
    int ph = surf_ph(dst), w = surf_wpx(dst);
    rq->tex->st.gpu_copies++;
    for (int r = 0; r < 64;) {
        if (!((rows >> r) & 1)) { r++; continue; }
        int r1 = r;
        while (r1 < 64 && ((rows >> r1) & 1)) r1++;
        int y0 = r * ph, y1 = r1 * ph;
        if (y1 > dst->h) y1 = dst->h;
        r = r1;
        if (y1 <= y0) continue;
        // zmask: colour -> Z24 keeps 24 bits; keep_a: Z24 -> colour leaves the top byte as uploaded
        BE(target_copy, dst->t, src->t, y0, y1, w, psm_bytes(dst->psm) == BYTES_LO ? 0xffffffu : 0xffffffffu,
           psm_bytes(src->psm) == BYTES_LO);
    }
}

static int copy_is_full(const Surface *dst, const Surface *src)   /* does gpu_copy replace every byte dst stores? */
{
    return !(src->depth && !dst->depth && psm_bytes(src->psm) == BYTES_LO);
}

/* Make the GPU copy of `s` current for page rows `rows`. */
static void surf_make_current(Rq *rq, Surface *s, uint64_t rows)
{
    RqTex *X = rq->tex;
    rows &= s->stale;
    if (!rows) return;
    int ph = surf_ph(s), r0 = __builtin_ctzll(rows), r1 = 64 - __builtin_clzll(rows);
    uint32_t lo, hi;
    rqs_block_range(s->bp, s->bw, s->psm, r0 * ph, r1 * ph, 0, &lo, &hi);
    Surface *cp[MAX_SURF];
    int ncp = 0;
    uint64_t covered = 0;
    for (int i = 0; i < MAX_SURF; i++) {
        Surface *t = &X->surf[i];
        if (!t->used || t == s || !t->dirty) continue;
        uint64_t tr = surf_rows_in(t, lo, hi) & t->dirty;
        if (!tr) continue;
        if (compat(t, s)) { cp[ncp++] = t; if (copy_is_full(s, t)) covered |= tr & rows; }
        else surf_download(rq, t, tr);
    }
    uint64_t up = rows & ~covered;
    for (int r = 0; r < 64;) {
        if (!((up >> r) & 1)) { r++; continue; }
        int e = r;
        while (e < 64 && ((up >> e) & 1)) e++;
        surf_upload(rq, s, r * ph, e * ph);
        r = e;
    }
    for (int i = 0; i < ncp; i++) gpu_copy(rq, s, cp[i], rows & cp[i]->dirty);
    s->stale &= ~rows;
    X->coh_epoch++;
}

/* Make the whole used area of `s` current. */
static void surf_validate(Rq *rq, Surface *s)
{
    int ph = surf_ph(s);
    surf_make_current(rq, s, rows_mask(0, (s->vh + ph - 1) / ph));
}

static Surface *surf_find(RqTex *X, uint32_t bp, uint32_t bw, int psm, int depth)
{
    for (int i = 0; i < MAX_SURF; i++) {
        Surface *s = &X->surf[i];
        if (s->used && s->bp == bp && s->depth == depth && s->bw == bw && rqs_layout(s->psm) == rqs_layout(psm)) return s;
    }
    return 0;
}

static Surface *surf_get(Rq *rq, uint32_t bp, uint32_t bw, int psm, int depth)
{
    RqTex *X = rq->tex;
    Surface *s = surf_find(X, bp, bw, psm, depth);
    if (!s) {
        for (int i = 0; i < MAX_SURF && !s; i++) if (!X->surf[i].used) s = &X->surf[i];
        if (!s) {   // evict least recently used
            s = &X->surf[0];
            for (int i = 1; i < MAX_SURF; i++) if (X->surf[i].last_use < s->last_use) s = &X->surf[i];
            surf_free(rq, s);
        }
        s->used = 1; s->depth = depth; s->bp = bp; s->bw = bw; s->psm = psm;
        s->w = bw ? (int)bw * 64 : 64; if (s->w > 2048) s->w = 2048;
        s->h = 1024;
        s->vh = 0;
        s->stale = ~0ull;
        s->t = BE(target_create, depth, s->w, s->h);
    }
    s->psm = psm;   // CT32/CT24 (or Z32/Z24) share storage
    s->last_use = ++X->use_clock;
    return s;
}

/* About to draw rows [y0, y1) of `s`, writing bytes `bytes` of each pixel. */
static void prepare_target(Rq *rq, Surface *s, int y0, int y1, int bytes)
{
    RqTex *X = rq->tex;
    WhyScope ws(X, W_DRAW);
    if (y1 > s->h) { if (!X->warned_clip++) rq_log("rq_tex: draw below row %d clipped", s->h); y1 = s->h; }
    if (y1 > s->vh) s->vh = y1;
    int ph = surf_ph(s);
    int r0 = y0 / ph, r1 = (y1 + ph - 1) / ph;
    uint64_t R = rows_mask(r0, r1);
    // same target again with nothing changed in between: everything below would be a no-op
    if (s->prep_epoch == X->coh_epoch && !(R & ~s->prep_rows) && !(bytes & ~s->prep_bytes)) return;
    surf_make_current(rq, s, R);
    uint32_t lo, hi;
    rqs_block_range(s->bp, s->bw, s->psm, r0 * ph, r1 * ph, 0, &lo, &hi);
    for (int i = 0; i < MAX_SURF; i++) {
        Surface *t = &X->surf[i];
        if (!t->used || t == s) continue;
        uint64_t tr = surf_rows_in(t, lo, hi);
        if (!tr) continue;
        uint64_t d = tr & t->dirty;
        if (d) {
            if (compat(t, s)) { t->dirty &= ~d; if (!t->dirty) t->hi_dirty = 0; X->coh_epoch++; }   // s took over that data
            else surf_download(rq, t, d);
        }
        if (tr & ~t->stale) { t->stale |= tr; X->coh_epoch++; }
    }
    s->dirty |= R;
    if (bytes & BYTES_HI) s->hi_dirty = 1;
    bump(X, lo, hi, bytes);   // whole page rows, so repeated draws inside them need no new stamp
    if (s->prep_epoch == X->coh_epoch) { s->prep_rows |= R; s->prep_bytes |= bytes; }
    else { s->prep_epoch = X->coh_epoch; s->prep_rows = R; s->prep_bytes = bytes; }
}

// ---------------------------------------------------------------- texture cache

static int is_paletted(int psm)
{
    return psm == RQ_PSMT8 || psm == RQ_PSMT4 || psm == RQ_PSMT8H || psm == RQ_PSMT4HL || psm == RQ_PSMT4HH;
}

static int amode_for(int psm)
{
    switch (psm) {
    case RQ_PSMCT24: case RQ_PSMZ24: return 1;
    case RQ_PSMCT16: case RQ_PSMCT16S: case RQ_PSMZ16: case RQ_PSMZ16S: return 2;
    default: return 0;
    }
}

/* palette (from clut_raw) expanded to RGBA8 */
static int build_palette(RqTex *X, uint64_t tex0, uint32_t *pal)
{
    int psm = (tex0 >> 20) & 0x3f, cpsm = (tex0 >> 51) & 15, csa = (tex0 >> 56) & 31;
    int n = (psm == RQ_PSMT8 || psm == RQ_PSMT8H) ? 256 : 16;
    int c32 = cpsm == RQ_PSMCT32 || cpsm == RQ_PSMCT24;
    for (int i = 0; i < n; i++) {
        if (c32) { uint32_t k = (n == 256 ? (uint32_t)i : (uint32_t)(csa & 15) * 16 + i) & 255; pal[i] = X->clut_raw[k] | ((uint32_t)X->clut_raw[k + 256] << 16); }
        else pal[i] = to_rgba8(X->clut_raw[(n == 256 ? (uint32_t)i : (uint32_t)csa * 16 + i) & 511], RQ_PSMCT16);
    }
    return n;
}

static TexBind tex_lookup(Rq *rq, uint64_t tex0, const Surface *target)
{
    RqTex *X = rq->tex;
    WhyScope ws(X, W_TEX);
    TexBind r = {};
    uint32_t tbp = (uint32_t)(tex0 & 0x3fff), tbw = (tex0 >> 14) & 0x3f;
    int psm = (tex0 >> 20) & 0x3f, tw = (tex0 >> 26) & 15, th = (tex0 >> 30) & 15, cpsm = (tex0 >> 51) & 15;
    if (tw > 10) tw = 10;
    if (th > 10) th = 10;
    int w = 1 << tw, h = 1 << th;
    // sampling a render target directly
    if (!is_paletted(psm)) {
        Surface *s = surf_find(X, tbp, tbw, psm, 0);
        if (s && s->vh > 0) {
            surf_validate(rq, s);
            s->last_use = ++X->use_clock;
            r.amode = amode_for(psm); r.glw = s->w; r.glh = s->h;
            r.rs = X->rs;
            if (s == target) {   // feedback: sample a snapshot
                r.tex = BE(target_snapshot, s->t, s->w, s->vh);
                X->st.rt_copy++;
            } else { r.tex = s->t; r.is_target = 1; }
            X->st.rt_tex++;
            return r;
        }
    }
    uint32_t lo, hi;
    rqs_block_range(tbp, tbw, psm, 0, h, w, &lo, &hi);
    int bytes = psm_bytes(psm);
    sync_vram(rq, lo, hi, 0, bytes);
    uint32_t pal[256], hash = 0;
    int pal_n = 0;
    if (is_paletted(psm)) {
        pal_n = build_palette(X, tex0, pal);
        hash = 2166136261u;
        for (int i = 0; i < pal_n; i++) hash = (hash ^ pal[i]) * 16777619u;
    }
    uint32_t ms = max_stamp(X, lo, hi, bytes);
    TexEntry *e = 0, *victim = &X->tcache[0];
    for (int i = 0; i < MAX_TEX; i++) {
        TexEntry *t = &X->tcache[i];
        if (!t->used) { if (victim->used) victim = t; continue; }
        if (t->tbp == tbp && t->tbw == tbw && t->psm == psm && t->tw == tw && t->th == th &&
            (!pal_n || (t->clut_hash == hash && t->cpsm == cpsm))) { e = t; break; }
        if (victim->used && t->last_use < victim->last_use) victim = t;
    }
    if (e && e->stamp < ms) {
        /* Stale. Games park CLUTs and other small uploads inside texture pages, so usually only a few
         * blocks changed: re-decode just those when the texture doesn't wrap its buffer width. */
        int bits = rqs_bits(psm), pw = bits >= 16 ? 64 : 128;
        uint32_t ppr = bits >= 16 ? tbw : tbw / 2;
        int nchg = 0, bwpx = 8, bhpx = 8;
        for (uint32_t b = lo; b < hi; b++) nchg += block_stamp(X, b, bytes) > e->stamp;
        int x0, y0;
        rqs_block_rect(psm, tbp, tbw, tbp, &x0, &y0, &bwpx, &bhpx);
        if (ppr && (uint32_t)w <= ppr * (uint32_t)pw && nchg * bwpx * bhpx * 4 < w * h) {
            for (uint32_t b = lo; b < hi; b++) {
                if (block_stamp(X, b, bytes) <= e->stamp) continue;
                int bx, by;
                rqs_block_rect(psm, tbp, tbw, b, &bx, &by, &bwpx, &bhpx);
                if (bx >= w || by >= h) continue;
                int rw = bx + bwpx > w ? w - bx : bwpx, rh = by + bhpx > h ? h - by : bhpx;
                read_rgba(X, psm, tbp, tbw, bx, by, rw, rh, pal, pal_n, 0, X->xfer);
                BE(tex_update, e->t, bx, by, rw, rh, X->xfer, rw);
            }
            e->stamp = X->stamp_now;
            X->st.partial++;
            X->coh_epoch++;
        } else { victim = e; e = 0; }   // re-decode fully into the same texture
    }
    if (!e) {
        e = victim;
        if (e->used && (e->tw != tw || e->th != th)) { BE(tex_destroy, e->t); e->used = 0; }
        X->st.decodes++;
        read_rgba(X, psm, tbp, tbw, 0, 0, w, h, pal, pal_n, 0, X->xfer);
        if (!e->used) e->t = BE(tex_create, w, h);
        BE(tex_update, e->t, 0, 0, w, h, X->xfer, w);
        e->used = 1; e->tbp = tbp; e->tbw = tbw; e->psm = psm; e->tw = tw; e->th = th;
        e->clut_hash = hash; e->cpsm = cpsm; e->stamp = X->stamp_now;
        X->coh_epoch++;
    }
    e->last_use = ++X->use_clock;
    r.tex = e->t;
    r.amode = pal_n ? (cpsm == RQ_PSMCT32 ? 0 : cpsm == RQ_PSMCT24 ? 1 : 2) : amode_for(psm);
    r.glw = w; r.glh = h; r.rs = 1;
    return r;
}

// ---------------------------------------------------------------- batching

static void flush(Rq *rq)
{
    RqTex *X = rq->tex;
    if (!X->nv) return;
    const BKey *k = &X->cur;
    const uint64_t *f = k->f;
    int n = X->nv;
    X->nv = 0;
    X->st.flushes++;
    uint64_t attr = f[RQ_KEY_ATTR];
    int tme = (attr >> 4) & 1, fge = (attr >> 5) & 1, fst = (attr >> 8) & 1, iip = (attr >> 3) & 1;
    int cls = (int)k->cls;
    // scissor & bbox
    uint64_t sc = f[RQ_KEY_SCISSOR];
    int sx0 = sc & 0x7ff, sx1 = (sc >> 16) & 0x7ff, sy0 = (sc >> 32) & 0x7ff, sy1 = (sc >> 48) & 0x7ff;
    int by0 = (int)X->bb_y0 - 1, by1 = (int)X->bb_y1 + 2;
    if (by0 < sy0) by0 = sy0;
    if (by1 > sy1 + 1) by1 = sy1 + 1;
    if (by1 <= by0 || sx1 < sx0) return;
    uint64_t te = f[RQ_KEY_TEST];
    int ate = te & 1, atst = (te >> 1) & 7, aref = (te >> 4) & 0xff, afail = (te >> 12) & 3;
    int zte = (te >> 16) & 1, ztst = (te >> 17) & 3;
    if (!zte) ztst = 1;
    if (zte && ztst == 0) return;
    if (ate && atst == 1) ate = 0;
    uint64_t frame = f[RQ_KEY_FRAME], zbuf = f[RQ_KEY_ZBUF];
    uint32_t fbp = (frame & 0x1ff) * 32, fbw = (frame >> 16) & 0x3f, fbmsk = (uint32_t)(frame >> 32);
    int fpsm = (frame >> 24) & 0x3f;
    uint32_t zbp = (zbuf & 0x1ff) * 32;
    int zpsm = ((zbuf >> 24) & 15) | 0x30, zmsk = (zbuf >> 32) & 1;
    int use_z = zte && (ztst >= 2 || !zmsk);
    int cmask[4] = {(fbmsk & 0xff) != 0xff, ((fbmsk >> 8) & 0xff) != 0xff, ((fbmsk >> 16) & 0xff) != 0xff, (fbmsk >> 24) != 0xff};
    if (fpsm == RQ_PSMCT24 || fpsm == RQ_PSMZ24) cmask[3] = 0;
    if (rqs_bits(fpsm) == 16) { cmask[3] = (fbmsk & 0x80000000u) == 0; }
    int any_color = cmask[0] | cmask[1] | cmask[2] | cmask[3];
    Surface *cs = any_color ? surf_get(rq, fbp, fbw, fpsm, 0) : 0;
    Surface *zs = use_z ? surf_get(rq, zbp, fbw, zpsm, 1) : 0;
    if (!cs && !zs) return;
    TexBind tb = {};
    if (tme) tb = tex_lookup(rq, f[RQ_KEY_TEX0], cs);
    if (cs) prepare_target(rq, cs, by0, by1, psm_bytes(fpsm) == BYTES_LO || !cmask[3] ? BYTES_LO : BYTES_ALL);
    if (zs) prepare_target(rq, zs, by0, by1, psm_bytes(zpsm));

    RqBatch b;
    memset(&b, 0, sizeof b);
    b.color = cs ? cs->t : 0; b.depth = zs ? zs->t : 0;
    b.tw = cs ? cs->w : zs->w; b.th = cs ? cs->h : zs->h;
    b.sx0 = sx0; b.sy0 = sy0; b.sx1 = sx1; b.sy1 = sy1; b.prim = cls;
    b.first = X->vb_off; b.n = (uint32_t)n;
    b.tme = tme; b.fst = fst; b.iip = iip; b.fge = fge;
    b.snap = (int)k->snap;
    b.zmax = zpsm == RQ_PSMZ32 ? 0xffffffffu : zpsm == RQ_PSMZ24 ? 0xffffff : 0xffff;
    if (tme) {
        uint64_t t0 = f[RQ_KEY_TEX0], cl = f[RQ_KEY_CLAMP], texa = f[RQ_KEY_TEXA];
        int tw = (t0 >> 26) & 15, th = (t0 >> 30) & 15;
        if (tw > 10) tw = 10;
        if (th > 10) th = 10;
        b.tex = tb.tex; b.tex_is_target = tb.is_target; b.amode = tb.amode; b.glw = tb.glw; b.glh = tb.glh;
        b.tex_rs = tb.rs ? tb.rs : 1;
        b.tfx = (t0 >> 35) & 3; b.tcc = (t0 >> 34) & 1; b.linear = (f[RQ_KEY_TEX1] >> 5) & 1;
        b.tsw = 1 << tw; b.tsh = 1 << th;
        // step 5 video.texfilter / video.aniso (0 = the game's filter): game textures only, render targets (post
        // effects, render-to-texture) keep theirs; forced bilinear and aniso leave sprites (2D) alone
        const RqOptions &o = rq->opt;
        if (o.texfilter == 1 && !tb.is_target) b.linear = 0;
        else if (o.texfilter == 2 && !tb.is_target && !k->spr) b.linear = 1;
        b.aniso = o.aniso && b.linear && !tb.is_target && cls == 2 && !k->spr ? o.aniso : 0;
        b.wms = cl & 3; b.wmt = (cl >> 2) & 3;
        b.minu = (cl >> 4) & 0x3ff; b.maxu = (cl >> 14) & 0x3ff; b.minv = (cl >> 24) & 0x3ff; b.maxv = (cl >> 34) & 0x3ff;
        b.ta0 = texa & 0xff; b.ta1 = (texa >> 32) & 0xff; b.aem = (texa >> 15) & 1;
    }
    b.fogcol = (uint32_t)f[RQ_KEY_FOGCOL];
    uint64_t al = f[RQ_KEY_ALPHA];
    b.abe = (attr >> 6) & 1; b.ba = al & 3; b.bb = (al >> 2) & 3; b.bc = (al >> 4) & 3; b.bd = (al >> 6) & 3;
    b.fix = (al >> 32) & 0xff; b.fba = f[RQ_KEY_FBA] & 1;
    b.ate = ate; b.atst = atst; b.aref = aref; b.afail = afail; b.zte = zte; b.ztst = ztst; b.zmsk = zmsk;
    for (int i = 0; i < 4; i++) b.cmask[i] = cmask[i];
    BE(draw, &b);
    X->st.draws++;
}

static void batch_open(Rq *rq)
{
    RqTex *X = rq->tex;
    X->vb = BE(vtx_alloc, MAXV, &X->vb_off, &X->vb_cap);
    X->bb_x0 = X->bb_y0 = 1e9f; X->bb_x1 = X->bb_y1 = -1e9f;
}

static inline void bb_add(RqTex *X, float x, float y)
{
    if (x < X->bb_x0) X->bb_x0 = x;
    if (x > X->bb_x1) X->bb_x1 = x;
    if (y < X->bb_y0) X->bb_y0 = y;
    if (y > X->bb_y1) X->bb_y1 = y;
}

static inline float fbits(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

static void put(RqTex *X, const RqVert *v, const RqVert *col, float s, float t, float q, const float *h = 0)
{
    RqHwVert o;   // built locally: the batch lives in (write-combined) mapped memory
    o.x = v->x * (1.0f / 16); o.y = v->y * (1.0f / 16);
    if (h) { o.x += h[0]; o.y += h[1]; }   // step 5.1: re-projected position (residual from the VU1 program's float)
    if (X->ax_on) o.x = X->ax_fp + (o.x - X->ax_fp) * X->ax_ws;   // step 5.2: 2D squeezed about its anchor
    if (X->ay_on) o.y = X->ay_fp + (o.y - X->ay_fp) * X->ay_s;    // s80 camera.fov: 3D scaled about the frame centre
    o.z = col->z;
    o.s = s; o.t = t; o.q = q;
    o.r = col->r; o.g = col->g; o.b = col->b; o.a = col->a;
    o.fog = col->f;
    X->vb[X->nv++] = o;
    bb_add(X, o.x, o.y);
}

static void put_v(RqTex *X, const RqVert *v, int fst, const float *h = 0)
{
    if (fst) put(X, v, v, v->u * (1.0f / 16), v->v * (1.0f / 16), 1.0f, h);
    else put(X, v, v, fbits(v->s), fbits(v->t), fbits(v->q), h);
}

/* gs_hw emit_prim from a canonical primitive (sprites: 2 vertices, the second carries colour / z / fog / q) */
static void emit_prim(RqTex *X, int cls, const RqVert *q, int fst, const float *h)   /* h: step 5.1 residuals or 0 */
{
    switch (cls) {
    case RQ_C_POINT: put_v(X, &q[0], fst); break;
    case RQ_C_LINE:
        if (X->rs > 1) {   // a GL line would be one scaled pixel thin: a quad one GS pixel thick across the major axis
            RqVert a0 = q[0], a1 = q[0], b0 = q[1], b1 = q[1];
            int dx = q[1].x - q[0].x, dy = q[1].y - q[0].y;
            if ((dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy)) { a0.y -= 8; a1.y += 8; b0.y -= 8; b1.y += 8; }
            else { a0.x -= 8; a1.x += 8; b0.x -= 8; b1.x += 8; }
            put_v(X, &a0, fst); put_v(X, &a1, fst); put_v(X, &b1, fst);
            put_v(X, &a0, fst); put_v(X, &b1, fst); put_v(X, &b0, fst);
        } else { put_v(X, &q[0], fst); put_v(X, &q[1], fst); }
        break;
    case RQ_C_TRI:
        if (h) { put_v(X, &q[0], fst, h); put_v(X, &q[1], fst, h + 2); put_v(X, &q[2], fst, h + 4); }
        else { put_v(X, &q[0], fst); put_v(X, &q[1], fst); put_v(X, &q[2], fst); }
        break;
    case RQ_C_SPRITE: {
        const RqVert *a = &q[0], *b = &q[1];
        RqVert c0 = *a, c1 = *a, c2 = *a, c3 = *a;   // corners: (a.x,a.y) (b.x,a.y) (a.x,b.y) (b.x,b.y)
        c1.x = b->x; c2.y = b->y; c3.x = b->x; c3.y = b->y;
        float s0, t0, s1, t1, qq;
        if (fst) { s0 = a->u / 16.0f; t0 = a->v / 16.0f; s1 = b->u / 16.0f; t1 = b->v / 16.0f; qq = 1.0f; }
        else { s0 = fbits(a->s); t0 = fbits(a->t); s1 = fbits(b->s); t1 = fbits(b->t); qq = fbits(b->q); }
        put(X, &c0, b, s0, t0, qq); put(X, &c1, b, s1, t0, qq); put(X, &c2, b, s0, t1, qq);
        put(X, &c1, b, s1, t0, qq); put(X, &c3, b, s1, t1, qq); put(X, &c2, b, s0, t1, qq);
        break;
    }
    }
}

// ---------------------------------------------------------------- CPU access to VRAM (gs_hw_vram_*)

static int pending_overlaps(Rq *rq, uint32_t lo, uint32_t hi)
{
    RqTex *X = rq->tex;
    if (!X->nv) return 0;
    const uint64_t *f = X->cur.f;
    int by1 = (int)X->bb_y1 + 2;
    uint32_t a, b, fbw = (f[RQ_KEY_FRAME] >> 16) & 0x3f;
    rqs_block_range((f[RQ_KEY_FRAME] & 0x1ff) * 32, fbw, (f[RQ_KEY_FRAME] >> 24) & 0x3f, 0, by1, 0, &a, &b);
    if (overlap(a, b, lo, hi)) return 1;
    rqs_block_range((f[RQ_KEY_ZBUF] & 0x1ff) * 32, fbw, ((f[RQ_KEY_ZBUF] >> 24) & 15) | 0x30, 0, by1, 0, &a, &b);
    if (overlap(a, b, lo, hi)) return 1;
    if ((f[RQ_KEY_ATTR] >> 4) & 1) {   // the batch's texture is decoded at flush time
        uint64_t t0 = f[RQ_KEY_TEX0];
        int tw = (t0 >> 26) & 15, th = (t0 >> 30) & 15;
        if (tw > 10) tw = 10;
        if (th > 10) th = 10;
        rqs_block_range((uint32_t)(t0 & 0x3fff), (t0 >> 14) & 0x3f, (t0 >> 20) & 0x3f, 0, 1 << th, 1 << tw, &a, &b);
        if (overlap(a, b, lo, hi)) return 1;
    }
    return 0;
}

static void vram_read(Rq *rq, uint32_t lo, uint32_t hi, int psm)
{
    if (pending_overlaps(rq, lo, hi)) flush(rq);
    sync_vram(rq, lo, hi, 0, psm_bytes(psm));
}

/* A CPU write into [lo,hi) is about to happen: GPU-side data there must reach the VRAM model first. */
static void vram_write_begin(Rq *rq, uint32_t lo, uint32_t hi, int psm)
{
    if (pending_overlaps(rq, lo, hi)) flush(rq);
    sync_vram(rq, lo, hi, 0, psm_bytes(psm));
}

/* The CPU write actually changed bytes in [lo,hi): surfaces and decoded textures there are stale. */
static void vram_changed(Rq *rq, uint32_t lo, uint32_t hi, int psm)
{
    if (pending_overlaps(rq, lo, hi)) flush(rq);
    vram_written(rq, lo, hi, 0, psm_bytes(psm));
    bump(rq->tex, lo, hi, psm_bytes(psm));
}

static void vram_write(Rq *rq, uint32_t lo, uint32_t hi, int psm)
{
    vram_write_begin(rq, lo, hi, psm);
    vram_changed(rq, lo, hi, psm);
}

// ---------------------------------------------------------------- CLUT (gs.c deferred queue, gs_draw.c gs_clut_load)

static void clut_load(RqTex *X, uint64_t tex0, uint32_t cbp, uint64_t tc)
{
    int psm = (tex0 >> 20) & 0x3f;
    int cpsm = (tex0 >> 51) & 15, csm = (tex0 >> 55) & 1, csa = (tex0 >> 56) & 31;
    int n = (psm == RQ_PSMT8 || psm == RQ_PSMT8H) ? 256 : 16;
    if (!is_paletted(psm)) return;
    int c32 = cpsm == RQ_PSMCT32 || cpsm == RQ_PSMCT24;
    uint32_t off = n == 256 ? 0 : (uint32_t)(c32 ? (csa & 15) : csa) * 16;
    uint32_t px[256] = {};   // invalid CPSM: rqs_read_rect leaves it, entries read 0 (as rqs_read_pixel did); CSM1: the 16x16 / 8x2 CLUT rect in one walk (was one rqs_read_pixel per entry)
    if (!csm) rqs_read_rect(X->vram, cpsm, cbp, 1, 0, 0, n == 256 ? 16 : 8, n == 256 ? 16 : 2, px, n == 256 ? 16 : 8);
    for (int i = 0; i < n; i++) {
        int x, y;
        if (csm) {   // CSM2: linear strip at TEXCLUT (CT16 only)
            x = (int)((tc >> 6) & 0x3f) * 16 + i; y = (int)((tc >> 12) & 0x3ff);
            uint32_t cbw = tc & 0x3f;
            uint32_t c = rqs_read_pixel(X->vram, cpsm, cbp, cbw, x, y);
            X->clut_raw[(off + i) & 511] = (uint16_t)c;
            continue;
        }
        if (n == 256) { int j = (i & 0xe7) | ((i & 8) << 1) | ((i & 16) >> 1); x = j & 15; y = j >> 4; }
        else { x = i & 7; y = i >> 3; }
        uint32_t c = px[y * (n == 256 ? 16 : 8) + x];
        if (c32) { X->clut_raw[(off + i) & 255] = (uint16_t)c; X->clut_raw[((off + i) & 255) + 256] = (uint16_t)(c >> 16); }
        else X->clut_raw[(off + i) & 511] = (uint16_t)c;
    }
}

/* Execute queued CLUT loads (before a paletted draw, or before VRAM is overwritten). */
static void clut_run_pending(Rq *rq)
{
    RqTex *X = rq->tex;
    if (!X->clut_nq) return;
    int n = X->clut_nq;
    X->clut_nq = 0;
    WhyScope ws(X, W_CLUT);
    uint16_t old[512], nw[512];
    memcpy(old, X->clut_raw, sizeof old);
    for (int i = 0; i < n; i++) {
        uint32_t cbp = X->clut_q[i].cbp;
        vram_read(rq, cbp, cbp + 32, (X->clut_q[i].tex0 >> 51) & 15);
        clut_load(X, X->clut_q[i].tex0, cbp, X->clut_q[i].texclut);
    }
    if (memcmp(old, X->clut_raw, sizeof old)) {
        // the pending batch must still draw with the old palette
        memcpy(nw, X->clut_raw, sizeof nw);
        memcpy(X->clut_raw, old, sizeof old);
        flush(rq);
        memcpy(X->clut_raw, nw, sizeof nw);
        X->clut_gen++;
    }
}

/* CLUT record (gs.c tex0_write, hw path): deferred. A full 256 x 32-bit load supersedes everything queued before. */
static void do_clut(Rq *rq, const uint64_t *f)
{
    RqTex *X = rq->tex;
    int psm = (int)f[0], cpsm = (int)f[2], csm = (int)f[3];
    uint64_t tex0 = (f[0] << 20) | (f[1] << 37) | (f[2] << 51) | (f[3] << 55) | (f[4] << 56);
    if ((psm == RQ_PSMT8 || psm == RQ_PSMT8H) && (cpsm == RQ_PSMCT32 || cpsm == RQ_PSMCT24) && !csm) X->clut_nq = 0;
    if (X->clut_nq == CLUT_Q) clut_run_pending(rq);
    X->clut_q[X->clut_nq].tex0 = tex0;
    X->clut_q[X->clut_nq].texclut = f[6];
    X->clut_q[X->clut_nq].cbp = (uint32_t)f[1];
    X->clut_nq++;
}

// ---------------------------------------------------------------- transfers (gs.c)

static void tr_flush_rows(Rq *rq, int r1)
{
    RqTex *X = rq->tex;
    auto &g = X->stg;
    if (r1 <= g.rows_done) return;
    int r0 = g.rows_done, n = r1 - r0, w = g.rrw;
    uint32_t need = (uint32_t)w * (uint32_t)n;
    if (need > g.px_cap) { g.px_cap = need + need / 2; g.px = (uint32_t *)realloc(g.px, (size_t)g.px_cap * 4); }
    uint64_t p = (uint64_t)r0 * (uint64_t)w;
    uint32_t *o = g.px;
    const uint8_t *d = g.data;
    switch (g.bits) {
    case 32: memcpy(o, d + p * 4, (size_t)need * 4); break;
    case 24: for (uint32_t i = 0; i < need; i++) { const uint8_t *s = d + (p + i) * 3; o[i] = s[0] | (s[1] << 8) | ((uint32_t)s[2] << 16); } break;
    case 16: for (uint32_t i = 0; i < need; i++) { uint16_t v; memcpy(&v, d + (p + i) * 2, 2); o[i] = v; } break;
    case 8:  for (uint32_t i = 0; i < need; i++) o[i] = d[p + i]; break;
    default: for (uint32_t i = 0; i < need; i++) { uint64_t q = p + i; o[i] = (d[q >> 1] >> ((q & 1) * 4)) & 15; } break;
    }
    g.rows_done = r1;
    uint32_t lo, hi;
    if (rqs_write_rect(X->vram, g.dpsm, g.dbp, g.dbw, g.dx, g.dy + r0, w, n, o, w, &lo, &hi))
        vram_changed(rq, lo, hi, g.dpsm);   // games re-upload unchanged textures every frame: only real changes count
}

/* Host->local write-through. A transfer that arrives whole in one record (all rows are written by this do_upload) in a
 * format that writes every byte of its pixels, page-aligned and without wrap: a dirty page row of a surface whose pages
 * the transfer either leaves alone or writes completely needs no GPU -> VRAM download (the old GPU bytes of the
 * written pages are dead, those of the others stay valid on the GPU). Such rows stay dirty (keep); after the write the
 * written pages are pushed from the VRAM model into the surface (upload_push), so the GPU copy equals what the
 * download + write + re-upload path gave, byte for byte (same read_rgba / target_upload conversions). */
static int up_pages(int psm, uint32_t bp, uint32_t bw, int x, int y, int w, int h, uint64_t T[8], uint64_t C[8])
{
    int full = psm == RQ_PSMCT32 || psm == RQ_PSMZ32 || psm == RQ_PSMCT16 || psm == RQ_PSMCT16S || psm == RQ_PSMZ16 ||
               psm == RQ_PSMZ16S || psm == RQ_PSMT8 || psm == RQ_PSMT4;   // 24-bit and 8H / 4HL / 4HH keep bytes
    if (!full || (bp & 31) || w <= 0 || h <= 0 || x < 0 || y < 0) return 0;
    int bits = rqs_bits(psm);
    int pw = bits >= 16 ? 64 : 128, ph = bits == 32 ? 32 : bits == 4 ? 128 : 64;
    uint32_t ppr = bits >= 16 ? bw : bw / 2;
    if (!ppr || (bits < 16 && (bw & 1)) || x + w > (int)ppr * pw || y + h > 2048) return 0;
    memset(T, 0, 8 * sizeof *T);
    memset(C, 0, 8 * sizeof *C);
    for (int py = y / ph; py <= (y + h - 1) / ph; py++)
        for (int px = x / pw; px <= (x + w - 1) / pw; px++) {
            uint32_t P = bp / 32 + (uint32_t)py * ppr + (uint32_t)px;
            if (P >= RQ_GS_BLOCKS / 32) return 0;   // wraps past the end of local memory
            T[P >> 6] |= 1ull << (P & 63);
            if (px * pw >= x && (px + 1) * pw <= x + w && py * ph >= y && (py + 1) * ph <= y + h) C[P >> 6] |= 1ull << (P & 63);
        }
    return 1;
}

static inline int pg_in(const uint64_t *m, uint32_t P) { return (int)(m[P >> 6] >> (P & 63) & 1); }

static void upload_keep(Rq *rq, uint32_t lo, uint32_t hi)
{
    RqTex *X = rq->tex;
    auto &g = X->stg;
    if (!up_pages(g.dpsm, g.dbp, g.dbw, g.dx, g.dy, g.rrw, g.rrh, X->upT, X->upC)) return;
    for (int i = 0; i < MAX_SURF; i++) {
        Surface *t = &X->surf[i];
        X->keep[i] = X->push[i] = 0;
        if (!t->used || !t->dirty || (t->bp & 31)) continue;
        uint64_t rows = surf_rows_in(t, lo, hi) & t->dirty, drop = 0;
        uint32_t ppr = t->bw ? t->bw : 1;
        for (int r = 0; r < 64; r++) {
            if (!(rows >> r & 1)) continue;
            int touched = 0, all = 1, ok = 1;
            for (uint32_t k = 0; k < ppr && ok; k++) {
                uint32_t P = t->bp / 32 + (uint32_t)r * ppr + k;
                if (P >= RQ_GS_BLOCKS / 32) ok = 0;
                else if (pg_in(X->upT, P)) { touched = 1; if (!pg_in(X->upC, P)) ok = 0; }
                else all = 0;
            }
            if (!ok) continue;
            if (all) drop |= 1ull << r;   // every byte of the row is overwritten: the GPU copy just goes stale
            else {
                X->keep[i] |= 1ull << r;
                if (touched) X->push[i] |= 1ull << r;
            }
        }
        if (drop) {
            t->dirty &= ~drop;
            if (!t->dirty) t->hi_dirty = 0;
            t->stale |= drop;
            X->coh_epoch++;
        }
    }
    X->keep_on = 1;
}

static void upload_push(Rq *rq)
{
    RqTex *X = rq->tex;
    if (!X->keep_on) return;
    X->keep_on = 0;
    for (int i = 0; i < MAX_SURF; i++) {
        Surface *t = &X->surf[i];
        uint64_t rows = X->push[i];
        X->keep[i] = X->push[i] = 0;
        if (!rows) continue;
        uint32_t ppr = t->bw ? t->bw : 1;
        int ph = surf_ph(t), wpx = surf_wpx(t);
        for (int r = 0; r < 64; r++) {
            if (!(rows >> r & 1)) continue;
            int y = r * ph, y1 = y + ph > t->h ? t->h : y + ph;
            if (y1 <= y) continue;
            for (uint32_t k = 0; k < ppr;) {   // runs of written pages in the row
                if (!pg_in(X->upT, t->bp / 32 + (uint32_t)r * ppr + k)) { k++; continue; }
                uint32_t e = k;
                while (e < ppr && pg_in(X->upT, t->bp / 32 + (uint32_t)r * ppr + e)) e++;
                int x = (int)k * 64, x1 = (int)e * 64 > wpx ? wpx : (int)e * 64;
                k = e;
                if (x1 <= x) continue;
                read_rgba(X, t->psm, t->bp, t->bw, x, y, x1 - x, y1 - y, 0, 0, t->depth, X->xfer);
                BE(target_upload, t->t, x, y, y1, x1 - x, X->xfer);
                X->st.uploads++; X->up_why[W_UPLOAD]++; X->up_px += (uint64_t)(x1 - x) * (uint32_t)(y1 - y);
            }
        }
    }
    X->coh_epoch++;
}

/* UPLOAD record: skip = 0 opens the transfer (gs.c tr_begin), a continuation (skip > 0) appends to it. Every fully
 * received row is written (gs_tr_sync); the rows of an abandoned partial transfer stay unwritten (design 15). */
static void do_upload(Rq *rq, const uint64_t *f, const uint8_t *data)
{
    RqTex *X = rq->tex;
    auto &g = X->stg;
    uint32_t total = (uint32_t)f[U_TOTAL], skip = (uint32_t)f[U_SKIP], nb = (uint32_t)f[U_NBYTES];
    if (!skip) X->st.gs_up++;
    WhyScope ws(X, W_UPLOAD);
    X->st.gs_up_bytes += nb;
    if (!skip) {
        g.dpsm = (int)f[U_DPSM]; g.bits = tr_bits(g.dpsm);
        g.dbp = (uint32_t)f[U_DBP]; g.dbw = (uint32_t)f[U_DBW];
        g.dx = (int)f[U_DSAX]; g.dy = (int)f[U_DSAY];
        g.rrw = (int)f[U_RRW]; g.rrh = (int)f[U_RRH];
        g.active = g.rrw && g.rrh;
        g.total = total; g.len = 0; g.rows_done = 0;
        if (!g.active) return;
        if (total + 16 > g.cap) { g.cap = total + 16; g.data = (uint8_t *)realloc(g.data, g.cap); }
        uint32_t lo, hi;
        rqs_block_range(g.dbp, g.dbw, g.dpsm, g.dy, g.dy + g.rrh, g.dx + g.rrw, &lo, &hi);
        clut_run_pending(rq);
        if (pending_overlaps(rq, lo, hi)) flush(rq);   // before upload_keep reads the dirty rows
        if (nb >= total && !f[U_PARTIAL]) upload_keep(rq, lo, hi);   // whole transfer in this record
        vram_write_begin(rq, lo, hi, g.dpsm);
    } else if (!g.active || skip != g.len) return;
    if (nb > g.total - g.len) nb = g.total - g.len;
    memcpy(g.data + g.len, data, nb);
    g.len += nb;
    uint64_t r = (uint64_t)g.len * 8 / (uint32_t)g.bits / (uint32_t)g.rrw;
    tr_flush_rows(rq, r > (uint64_t)g.rrh ? g.rrh : (int)r);
    upload_push(rq);
    if (g.len >= g.total) g.active = 0;
}

static void do_copy(Rq *rq, const uint64_t *f)
{
    RqTex *X = rq->tex;
    uint32_t sbp = (uint32_t)f[0], sbw = (uint32_t)f[1], dbp = (uint32_t)f[5], dbw = (uint32_t)f[6];
    int spsm = (int)f[2], sx = (int)f[3], sy = (int)f[4], dpsm = (int)f[7], dx = (int)f[8], dy = (int)f[9];
    int dir = (int)f[10], w = (int)f[11], h = (int)f[12];
    uint32_t lo, hi;
    WhyScope ws(X, W_COPY);
    clut_run_pending(rq);
    rqs_block_range(sbp, sbw, spsm, sy, sy + h, sx + w, &lo, &hi); vram_read(rq, lo, hi, spsm);
    rqs_block_range(dbp, dbw, dpsm, dy, dy + h, dx + w, &lo, &hi); vram_write(rq, lo, hi, dpsm);
    if (sx + w <= 2048 && sy + h <= 2048 && dx + w <= 2048 && dy + h <= 2048 && w > 0 && h > 0) {
        // no wrap: copy through a snapshot of the source (gives the DIR ordering's result for overlaps)
        uint32_t need = (uint32_t)w * (uint32_t)h;
        if (need > X->cp_cap) { X->cp_cap = need; X->cp_tmp = (uint32_t *)realloc(X->cp_tmp, (size_t)need * 4); }
        rqs_read_rect(X->vram, spsm, sbp, sbw, sx, sy, w, h, X->cp_tmp, w);
        rqs_write_rect(X->vram, dpsm, dbp, dbw, dx, dy, w, h, X->cp_tmp, w, 0, 0);
        return;
    }
    for (int j = 0; j < h; j++) {
        int yy = (dir & 2) ? h - 1 - j : j;
        for (int i = 0; i < w; i++) {
            int xx = (dir & 1) ? w - 1 - i : i;
            uint32_t v = rqs_read_pixel(X->vram, spsm, sbp, sbw, (sx + xx) & 2047, (sy + yy) & 2047);
            rqs_write_pixel(X->vram, dpsm, dbp, dbw, (dx + xx) & 2047, (dy + yy) & 2047, v);
        }
    }
}

/* local->host: the whole source rect is read when the transfer starts; deliveries pack pixels from it */
static void do_readback(Rq *rq, const uint64_t *f)
{
    RqTex *X = rq->tex;
    uint32_t sbp = (uint32_t)f[0], sbw = (uint32_t)f[1];
    int spsm = (int)f[2], sx = (int)f[3], sy = (int)f[4], w = (int)f[5], h = (int)f[6];
    uint32_t lo, hi;
    WhyScope ws(X, W_READBACK);
    rqs_block_range(sbp, sbw, spsm, sy, sy + h, sx + w, &lo, &hi);
    vram_read(rq, lo, hi, spsm);
    auto &d = X->rd;
    d.n = (uint32_t)w * (uint32_t)h; d.pos = 0; d.active = d.n != 0; d.bits = tr_bits(spsm); d.acc = 0; d.nacc = 0;
    d.sbp = sbp; d.sbw = sbw; d.spsm = spsm; d.sx = sx; d.sy = sy; d.w = w; d.h = h;
    if (d.n > d.cap) { d.cap = d.n; d.px = (uint32_t *)realloc(d.px, (size_t)d.cap * 4); }
    if (sx + w <= 2048 && sy + h <= 2048) rqs_read_rect(X->vram, spsm, sbp, sbw, sx, sy, w, h, d.px, w);
    else
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) d.px[y * w + x] = rqs_read_pixel(X->vram, spsm, sbp, sbw, (sx + x) & 2047, (sy + y) & 2047);
}

/* packs the next n bytes of the open transfer like gs.c gs_fifo_read; a pixel split by the end of a delivery continues
 * in the next one (24-bit: 16 bytes = 5 1/3 pixels) */
static void rd_pack(RqTex *X, uint8_t *dst, uint32_t n)
{
    auto &d = X->rd;
    uint32_t i = 0;
    uint64_t acc = d.acc;
    int nacc = d.nacc;
    memset(dst, 0, n);
    if (!d.active && nacc < 8) { if (!X->warned_rb++) rq_log("rq_tex: FIFO read without an active local->host transfer"); return; }
    for (;;) {
        while (nacc >= 8 && i < n) { dst[i++] = (uint8_t)acc; acc >>= 8; nacc -= 8; }
        if (i >= n || !d.active) break;
        uint32_t v = d.px[d.pos];
        acc |= (uint64_t)(d.bits == 32 ? v : v & ((1u << d.bits) - 1)) << nacc;
        nacc += d.bits;
        if (++d.pos >= d.n) d.active = 0;
    }
    d.acc = acc; d.nacc = nacc;
}

/* the module's delivery buffer: rq memory at madr (write_readback) or the both-mode compare buffer */
static uint8_t *rb_dst(Rq *rq, uint32_t madr, uint32_t n)
{
    RqTex *X = rq->tex;
    X->rbc_n = 0;
    if (rq->opt.write_readback) {
        uint8_t *dst = rq_mem(rq_addr_fold(madr), n);
        if (!dst && !X->warned_rb++) rq_log("rq_tex: bad readback address %08x", madr);
        return dst;
    }
    if (n > X->rbc_cap) { X->rbc_cap = n; X->rbc = (uint8_t *)realloc(X->rbc, n); }   // rule 11: both mode
    X->rbc_n = n;
    return X->rbc;
}

void rqt_readback_deliver(Rq *rq, uint32_t madr, uint32_t qwc)
{
    RqTex *X = rq->tex;
    if (!X) return;
    rqt_consume(rq);   // sync point: the READBACK record and everything before it
    uint32_t n = qwc * 16;
    uint8_t *dst = rb_dst(rq, madr, n);
    if (dst) rd_pack(X, dst, n);
}

void rqt_fifo_read(Rq *rq, uint8_t out[16])
{
    RqTex *X = rq->tex;
    if (!X) return;
    rqt_consume(rq);
    if (rq->opt.write_readback) { X->rbc_n = 0; rd_pack(X, out, 16); }
    else rd_pack(X, rb_dst(rq, 0, 16), 16);
}

int rqt_fifo_pending(Rq *rq)
{
    RqTex *X = rq->tex;
    if (!X) return 0;
    rqt_consume(rq);   // the READBACK record opens the transfer
    return X->rd.active || X->rd.nacc >= 8;
}

uint32_t rq_readback_compare(Rq *rq, const void *ref, uint32_t nbytes)
{
    RqTex *X = rq->tex;
    if (!X || !X->rbc_n) return 0;   // null backend (no VRAM model) or no delivery packed
    const uint8_t *r = (const uint8_t *)ref;
    uint32_t m = nbytes < X->rbc_n ? nbytes : X->rbc_n, bad = 0, first = 0;
    for (uint32_t i = 0; i < m; i++)
        if (r[i] != X->rbc[i]) { if (!bad) first = i; bad++; }
    if (nbytes != X->rbc_n) { if (!bad) first = m; bad += (nbytes > m ? nbytes : X->rbc_n) - m; }
    RqStats *s = &rq->st;
    s->rb_cmp++;
    if (bad) {
        s->rb_bad_kicks++;
        s->rb_bad_bytes += bad;
        if (s->rb_lines < (uint32_t)rq->opt.max_diff_lines) {
            s->rb_lines++;
            const auto &d = X->rd;
            rq_log("rq readback diff: after vblank %u delivery %llu sbp 0x%x sbw %u psm 0x%x rect %d,%d %dx%d: byte %u of %u "
                   "ref %02x rq %02x (%u bytes differ)", s->vblank, (unsigned long long)s->rb_kicks, d.sbp, d.sbw, d.spsm,
                   d.sx, d.sy, d.w, d.h, first, nbytes, first < nbytes ? r[first] : 0, first < X->rbc_n ? X->rbc[first] : 0, bad);
        }
    }
    X->rbc_n = 0;
    return bad;
}

// ---------------------------------------------------------------- step 5.2 Hor+ squeeze + HUD anchoring (widescreen)
// Hor+ is render-side only (user rule s79: game RAM identical at every aspect): in a wide frame every 3D primitive
// (kinds MODEL / SHADOW / EFFECT / ENV, untagged non-sprites when untagged3d,
// the lens flare = POST sub 2 and the lock sight / markers = HUD 4, the cockpit panel = HUD 8, all positioned by the
// game's 4:3 projection) drawn into a frame-sized buffer (FBW * 64 = the displayed width) is squeezed by ws about the
// frame centre, and the presenter stretches the frame back out to
// the wider aspect: the 4:3 view in the middle, the geometry the game drew beyond its 4:3 edges (VU1 guard band) at
// the sides. Render targets (class TEXTURE, other buffer widths), full-screen POST passes, the fade (HUD 7) and
// untagged sprites are drawn as is (they cover the whole stretched frame). 2D whose x is in frame pixels is squeezed
// the same way about an anchor so it keeps its 4:3 proportions: hud 1 = the frame centre, hud 2 = the element's own
// screen edge, hud 0 = not (stretched). Elements (the hybrid's
// hud_ws.c before s54, here per group: battle HUD / menu): the previous frame's anchored primitives merged while they
// overlap or touch (union at most 45 % of the frame width) within a group; a primitive takes the anchor of the element
// containing its centre, else that of its own centre (outside the middle 16 % of the width = the nearer edge;
// hud_ws.c used the outer 35 %, which split the AP panel's digits between left and centre). The lock sight / markers
// (HUD 4, projected 3D points), the full-screen fade (7), the cockpit panel (8, TP2-projected: Hor+ already) and
// render-to-texture HUD (class TEXTURE) stay as drawn. ACNB_HUD_DUMP=V logs the element boxes built at vblank V.
// s80 camera.fov (render-side, game camera unchanged): the same WS_3D primitives are also scaled by zoom about the
// frame centre (x: centre of the frame width, y: centre of the record's scissor), x by ws * zoom; 2D is not zoomed.
struct WsRect { int x0, y0, x1, y1, w; uint32_t grp; };
enum { WS_MAXR = 4096, WS_MAXE = 256, WS_MAXW_PCT = 45 };
struct RqWide {
    float ws, zoom; int hud, untagged3d, on;   // on: ws < 1 or zoom != 1
    int disp_w;             // width in pixels of the displayed picture (last do_display), 0 = none yet
    WsRect prim[WS_MAXR], prev[WS_MAXR], tmp[2 * WS_MAXR], elem[WS_MAXE];
    signed char anchor[WS_MAXE];
    int nprim, nprev, nelem, dump_vb;
};

void rq_set_widescreen(Rq *rq, float ws, float zoom, int hud, int untagged3d)
{
    RqTex *X = rq->tex;
    if (!X) return;
    if (!(ws > 0.0f && ws < 0.999f)) ws = 1.0f;
    if (!(zoom > 0.0f) || fabsf(zoom - 1.0f) < 1e-4f) zoom = 1.0f;
    if (ws == 1.0f && zoom == 1.0f) {
        if (X->wide) { X->wide->ws = 1.0f; X->wide->zoom = 1.0f; X->wide->hud = 0; X->wide->on = 0; }
        return;
    }
    if (!X->wide) {
        X->wide = (RqWide *)calloc(1, sizeof(RqWide));
        const char *e = getenv("ACNB_HUD_DUMP");
        X->wide->dump_vb = e ? atoi(e) : -1;
    }
    X->wide->ws = ws; X->wide->zoom = zoom; X->wide->hud = ws < 1.0f ? hud : 0; X->wide->untagged3d = untagged3d;
    X->wide->on = 1;
}

/* do_display: the displayed picture's width (the 3D is squeezed only in draws into frame-sized buffers: battle draws
 * the 3D into an off-screen buffer, FBP 0x38, and copies it to the displayed one, FBP 0) */
static void ws_display(RqTex *X, int w)
{
    if (X->wide && w > 0) X->wide->disp_w = w;
}

enum { WS_NONE, WS_HUD, WS_MENU, WS_3D };
static uint32_t ws_group(const RqTag &t, int pcls, int untagged3d)   // WS_*: as is, battle HUD / text, menu, centre
{
    uint32_t cls = (uint32_t)(t.objid >> 32), inst = (uint32_t)t.objid;
    if (cls == RQ_OC_TEXTURE) return WS_NONE;   // render-to-texture passes
    switch (t.kind) {
    case RQ_K_HUD:
        if (cls != RQ_OC_HUD_ELEM || inst == 7) return WS_NONE;   // 7 = full-screen fade
        return inst == 4 || inst == 8 ? WS_3D : WS_HUD;            // lock sight / markers, cockpit panel: 4:3-projected
    case RQ_K_TEXT: return cls == RQ_OC_MENU ? WS_MENU : WS_HUD;
    case RQ_K_SPRITE2D: return WS_HUD;
    case RQ_K_MENU: return WS_MENU;
    case RQ_K_MODEL: case RQ_K_SHADOW: case RQ_K_EFFECT: case RQ_K_ENV: return WS_3D;
    case RQ_K_POST: return t.sub == 2 ? WS_3D : WS_NONE;   // lens flare (4:3-projected sprites); fills / blur / shimmer
    case RQ_K_NONE: return untagged3d && pcls != RQ_C_SPRITE ? WS_3D : WS_NONE;
    default: return WS_NONE;
    }
}

static int ws_anchor_of(int x0, int x1, int w)   // centre in the middle 16 % of the width: centre, else the nearer edge
{
    int c2 = x0 + x1;   // 2 * centre
    if (c2 * 100 < w * 84) return 0;
    if (c2 * 100 > w * 116) return 2;
    return 1;
}

/* before emit_prim: the x squeeze (and fov zoom) of this primitive (n canonical vertices, frame width w in pixels,
 * cy = the record's scissor centre y, pcls RQ_C_*) */
static void ws_prim(RqTex *X, const RqTag &t, const RqVert *q, int n, int w, float cy, int pcls)
{
    RqWide *W = X->wide;
    uint32_t g = ws_group(t, pcls, W->untagged3d);
    X->ax_on = 0; X->ay_on = 0;
    if (!g || w <= 0) return;
    if (g == WS_3D) {
        if (w != W->disp_w) return;   // not drawn into a frame-sized buffer (render-to-texture)
        X->ax_on = 1; X->ax_ws = W->ws * W->zoom; X->ax_fp = 0.5f * (float)w;
        if (W->zoom != 1.0f) { X->ay_on = 1; X->ay_s = W->zoom; X->ay_fp = cy; }
        return;
    }
    if (!W->hud) return;
    int a = 1;
    if (W->hud == 2) {
        int x0 = q[0].x, x1 = q[0].x, y0 = q[0].y, y1 = q[0].y;
        for (int i = 1; i < n; i++) {
            if (q[i].x < x0) x0 = q[i].x;
            if (q[i].x > x1) x1 = q[i].x;
            if (q[i].y < y0) y0 = q[i].y;
            if (q[i].y > y1) y1 = q[i].y;
        }
        x0 >>= 4; x1 = (x1 + 15) >> 4; y0 >>= 4; y1 = (y1 + 15) >> 4;
        if (W->nprim < WS_MAXR) W->prim[W->nprim++] = WsRect{x0, y0, x1, y1, w, g};
        int cx = (x0 + x1) >> 1, cy = (y0 + y1) >> 1, i = 0;
        while (i < W->nelem && !(W->elem[i].grp == g && W->elem[i].w == w && cx >= W->elem[i].x0 && cx <= W->elem[i].x1 &&
                                 cy >= W->elem[i].y0 && cy <= W->elem[i].y1)) i++;
        a = i < W->nelem ? W->anchor[i] : ws_anchor_of(x0, x1, w);
    }
    X->ax_on = 1;
    X->ax_ws = W->ws;
    X->ax_fp = a == 0 ? 0.0f : a == 2 ? (float)w : 0.5f * (float)w;
}

static int ws_touch(const WsRect *a, const WsRect *b)
{
    if (a->grp != b->grp || a->w != b->w) return 0;
    if (!(a->x0 <= b->x1 && b->x0 <= a->x1 && a->y0 <= b->y1 && b->y0 <= a->y1)) return 0;
    int x0 = a->x0 < b->x0 ? a->x0 : b->x0, x1 = a->x1 > b->x1 ? a->x1 : b->x1;
    return (x1 - x0) * 100 <= a->w * WS_MAXW_PCT;
}

static int ws_cmp(const void *a, const void *b)
{
    return memcmp(a, b, sizeof(WsRect));   // any total order; WsRect has no padding (6 x 4 bytes)
}

/* rq_present: the anchored primitives of this and the previous vblank become the next frame's element boxes (the
 * battle HUD is drawn at 30 Hz: a vblank can hold a part of it or nothing; an empty one keeps the boxes) */
static void ws_frame(RqTex *X, uint32_t vblank)
{
    RqWide *W = X->wide;
    if (!W) return;
    if (W->hud != 2) { W->nprim = W->nprev = W->nelem = 0; return; }
    if (!W->nprim) return;
    int n = W->nprev + W->nprim;
    memcpy(W->tmp, W->prev, W->nprev * sizeof *W->tmp);
    memcpy(W->tmp + W->nprev, W->prim, W->nprim * sizeof *W->tmp);
    memcpy(W->prev, W->prim, W->nprim * sizeof *W->prev);
    W->nprev = W->nprim;
    W->nprim = 0;
    qsort(W->tmp, n, sizeof *W->tmp, ws_cmp);   // drop duplicates (the same HUD in both vblanks, repeated glyph cells)
    int u = 0;
    for (int i = 0; i < n; i++) if (!u || ws_cmp(&W->tmp[u - 1], &W->tmp[i])) W->tmp[u++] = W->tmp[i];
    n = u;
    for (int changed = 1; changed;) {   // merge boxes until none touch
        changed = 0;
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++)
                if (ws_touch(&W->tmp[i], &W->tmp[j])) {
                    WsRect *a = &W->tmp[i], *b = &W->tmp[j];
                    if (b->x0 < a->x0) a->x0 = b->x0;
                    if (b->y0 < a->y0) a->y0 = b->y0;
                    if (b->x1 > a->x1) a->x1 = b->x1;
                    if (b->y1 > a->y1) a->y1 = b->y1;
                    W->tmp[j--] = W->tmp[--n];
                    changed = 1;
                }
    }
    W->nelem = n < WS_MAXE ? n : WS_MAXE;
    for (int i = 0; i < W->nelem; i++) {
        const WsRect &e = W->tmp[i];
        W->elem[i] = e;
        W->anchor[i] = (signed char)ws_anchor_of(e.x0, e.x1, e.w);
        if (W->dump_vb >= 0 && vblank == (uint32_t)W->dump_vb)
            rq_log("rq hud: vblank %u element %d group %u: %d,%d-%d,%d of %d anchor %s", vblank, i, e.grp, e.x0, e.y0, e.x1,
                   e.y1, e.w, W->anchor[i] == 0 ? "left" : W->anchor[i] == 2 ? "right" : "centre");
    }
}

// ---------------------------------------------------------------- draws

static void do_draw(Rq *rq, const RqRec *r, const uint64_t *f, const RqVert *v, uint32_t p0)
{
    static const int nvp[4] = {1, 2, 3, 2};
    RqTex *X = rq->tex;
    if (r->cls > RQ_C_SPRITE) return;
    int tme = (f[RQ_KEY_ATTR] >> 4) & 1;
    if (X->clut_nq && tme && is_paletted((f[RQ_KEY_TEX0] >> 20) & 0x3f)) clut_run_pending(rq);
    int fst = (f[RQ_KEY_ATTR] >> 8) & 1, m = nvp[r->cls];
    uint32_t np = (uint32_t)f[D_NPRIM];
    // step 5.1 (off: lay = 0, h = 0, snap = 0 - the batches of the original picture): 2D kinds on the 1x pixel grid
    // (per tag run: tags never split records), re-projected triangles from the residuals parallel to the vertices
    const RqRecList *l = rq->rec;
    int lay = rq->opt.layout2d && X->rs > 1;
    const float *h = rq->opt.reproject && r->cls == RQ_C_TRI && l->hxy.n >= (r->vtx + r->nvert) * 8
                     ? (const float *)l->hxy.p + 2 * r->vtx : 0;
    // step 5.2 (off: wide = 0): Hor+ 3D squeezed about the centre, 2D about its anchor for the widescreen picture
    int wide = X->wide && X->wide->on;
    int fw = (int)((f[RQ_KEY_FRAME] >> 16) & 0x3f) * 64;
    float fcy = 0.5f * (float)(((f[RQ_KEY_SCISSOR] >> 32) & 0x7ff) + ((f[RQ_KEY_SCISSOR] >> 48) & 0x7ff) + 1);
    uint32_t ri = 0, next = (lay || wide) && r->nxrun ? rqr_run(l, r->xrun)->vfirst : ~0u;
    const RqTag *tg = &r->tag;
    auto tag_at = [&](uint32_t vi) {
        while (vi >= next) { tg = &rqr_run(l, r->xrun + ri)->tag; ri++; next = ri < r->nxrun ? rqr_run(l, r->xrun + ri)->vfirst : ~0u; }
    };
    auto snap_at = [&](uint32_t vi) -> uint32_t {
        if (!lay) return 0;
        tag_at(vi);
        int kind = tg->kind;
        int on = kind == RQ_K_HUD || kind == RQ_K_TEXT || kind == RQ_K_SPRITE2D || (kind == RQ_K_MENU && r->cls == RQ_C_SPRITE);
        return on ? (uint32_t)X->rs : 0;
    };
    BKey k;
    memcpy(k.f, f, sizeof k.f);
    k.clut_gen = tme ? X->clut_gen : 0;
    k.cls = r->cls == RQ_C_POINT ? 0 : r->cls == RQ_C_LINE ? (X->rs > 1 ? 2 : 1) : 2;   // scaled: lines become quads
    k.snap = snap_at(p0 * m);
    k.spr = (rq->opt.texfilter == 2 || rq->opt.aniso) && r->cls == RQ_C_SPRITE;
    // step 5 video.blur = off (consumer only; records, hashes and readbacks untouched): the battle blur passes (ScrEff
    // Blur preset = POST sub 0x100 + slot of kind 5, sfx blur filter = POST sub 3) keep their draws and alpha writes
    // but blend to Cd; the menu flip copy (POST TEXTURE 0x1180, both sprites) becomes an opaque copy aligned texel
    // centre on pixel (no copy-blur weight, no flicker-filter offset: both sprites write the same exact copy)
    int mcopy = 0;
    if (rq->opt.blur_off && ((f[RQ_KEY_ATTR] >> 6) & 1)) {
        const RqTag *t = &r->tag;
        for (uint32_t j = 0; j < r->nxrun && rqr_run(l, r->xrun + j)->vfirst <= p0 * m; j++) t = &rqr_run(l, r->xrun + j)->tag;
        if (t->kind == RQ_K_POST) {
            if ((t->sub >= 0x100 && t->objid == RQ_OBJID(RQ_OC_NONE, 5)) || (t->sub == 3 && t->objid == RQ_OBJID(RQ_OC_NONE, 0)))
                k.f[RQ_KEY_ALPHA] = (k.f[RQ_KEY_ALPHA] & ~0xcfull) | 0x45;   // A = B = D = Cd: (Cd - Cd) * C + Cd
            else if (t->sub == 0 && t->objid == RQ_OBJID(RQ_OC_TEXTURE, 0x1180) && r->cls == RQ_C_SPRITE && tme
                     && (f[RQ_KEY_TEX0] & 0x3fff) == 0x1180) {
                k.f[RQ_KEY_ALPHA] = 0x80ull << 32 | 0x64;   // (Cs - Cd) * 0x80 / 0x80 + Cd = Cs
                mcopy = fst;
            }
        }
    }
    if (X->nv && memcmp(&k, &X->cur, sizeof k)) flush(rq);
    X->cur = k;
    for (uint32_t i = p0; i < np; i++) {
        if (lay) {
            uint32_t sn = snap_at(i * m);
            if (sn != X->cur.snap) { flush(rq); X->cur.snap = sn; }
        }
        if (X->nv && (uint32_t)X->nv + 6 > X->vb_cap) flush(rq);
        if (!X->nv) batch_open(rq);
        const RqVert *pv = v + i * m;
        RqVert al[2];
        if (mcopy) {   // blur off: x = u - 1/2 texel, y = v - 1/2 (GS samples at pixel corners: texel centres)
            int dx = (int)pv[0].u - 8 - pv[0].x, dy = (int)pv[0].v - 8 - pv[0].y;
            al[0] = pv[0]; al[1] = pv[1];
            al[0].x += dx; al[1].x += dx; al[0].y += dy; al[1].y += dy;
            pv = al;
        }
        if (wide) { tag_at(i * m); ws_prim(X, *tg, pv, m, fw, fcy, r->cls); }
        emit_prim(X, r->cls, pv, fst, h ? h + i * m * 2 : 0);
        X->ax_on = 0; X->ay_on = 0;
        X->st.verts++;
    }
}

// ---------------------------------------------------------------- display (gs_hw_display)

static void do_display(Rq *rq, const uint64_t *f)
{
    RqTex *X = rq->tex;
    flush(rq);
    WhyScope ws(X, W_DISPLAY);
    uint64_t pmode = f[0], smode2 = f[1];
    int en[2] = {(int)(pmode & 1), (int)((pmode >> 1) & 1)};
    X->out_any = 0;
    if (!en[0] && !en[1]) return;
    uint64_t dfb[2] = {f[2], f[4]}, dsp[2] = {f[3], f[5]};
    int field_mode = (smode2 & 1) && (smode2 & 2);
    int w = 0, h = 0, ox[2] = {0, 0}, oy[2] = {0, 0}, cw[2] = {0, 0}, ch[2] = {0, 0};
    for (int k = 0; k < 2; k++) {
        if (!en[k]) continue;
        int magh = ((dsp[k] >> 23) & 15) + 1;
        cw[k] = (int)(((dsp[k] >> 32) & 0xfff) + 1) / magh;
        ws_display(X, cw[k]);
        ch[k] = (int)((dsp[k] >> 44) & 0x7ff) + 1;
        if (field_mode) ch[k] /= 2;
        ox[k] = (int)(dsp[k] & 0xfff) / magh;
        oy[k] = (int)((dsp[k] >> 12) & 0x7ff);
        if (cw[k] > w) w = cw[k];
        if (ch[k] > h) h = ch[k];
    }
    if (w > 1024) w = 1024;
    if (h > 1024) h = 1024;
    int mx = 1 << 30, my = 1 << 30;
    for (int k = 0; k < 2; k++) if (en[k]) { if (ox[k] < mx) mx = ox[k]; if (oy[k] < my) my = oy[k]; }
    RqDisplayIn d;
    memset(&d, 0, sizeof d);
    d.en[0] = en[0]; d.en[1] = en[1]; d.w = w; d.h = h;
    d.mmod = (pmode >> 5) & 1; d.slbg = (pmode >> 7) & 1; d.alp = (pmode >> 8) & 0xff; d.bg = (uint32_t)f[6];
    for (int k = 0; k < 2; k++) {
        d.c[k].ox = ox[k] - mx; d.c[k].oy = (oy[k] - my) >> (field_mode ? 1 : 0);
        if (!en[k]) continue;
        uint32_t bp = (dfb[k] & 0x1ff) * 32, bw = (dfb[k] >> 9) & 0x3f;
        int psm = (dfb[k] >> 15) & 0x1f;
        int dbx = (dfb[k] >> 32) & 0x7ff, dby = (dfb[k] >> 43) & 0x7ff;
        d.c[k].cls = psm == RQ_PSMCT24 ? 1 : rqs_bits(psm) == 16 ? 2 : 0;
        Surface *s = surf_find(X, bp, bw, psm, 0);
        if (s && s->vh >= dby + ch[k]) {
            surf_validate(rq, s);
            d.c[k].target = s->t; d.c[k].sx = dbx; d.c[k].sy = dby;
        } else {   // not a render target: decode straight from VRAM
            uint32_t lo, hi;
            int rw = cw[k] > 2048 ? 2048 : cw[k], rh = ch[k] > 1024 ? 1024 : ch[k];
            rqs_block_range(bp, bw, psm, dby, dby + rh, dbx + rw, &lo, &hi);
            sync_vram(rq, lo, hi, 0, BYTES_ALL);
            if (!X->disp_px[k]) X->disp_px[k] = (uint32_t *)malloc((size_t)2048 * 1024 * 4);
            read_rgba(X, psm, bp, bw, dbx, dby, rw, rh, 0, 0, 0, X->disp_px[k]);
            d.c[k].rgba = X->disp_px[k]; d.c[k].rw = rw; d.c[k].rh = rh;
        }
    }
    X->out_any = BE(display, &d, &X->out_w, &X->out_h);
}

// ---------------------------------------------------------------- consumer

void rqt_consume(Rq *rq)
{
    RqTex *X = rq->tex;
    if (!X) return;
    RqRecList *l = rq->rec;
    if (X->cursor >= l->nrec) return;
    uint64_t t0 = rq_clock_ns();
    rq->st.f_syncs++;
    rq->be_ops->enter(rq->be);
    while (X->cursor < l->nrec) {
        if (l->up_open && l->up_rec == X->cursor) break;   // still receiving bytes
        const RqRec *r = rqr_at(l, X->cursor);
        const uint64_t *f = rqr_fld(l, r);
        switch (r->type) {
        case RQ_R_DRAW: {
            // a DRAW record grows while it is the last one (coalescing): resume after the prims already consumed
            uint32_t p0 = X->part_rec == X->cursor ? X->part_done : 0;
            do_draw(rq, r, f, (const RqVert *)l->verts.p + r->vtx, p0);
            if (X->cursor == l->nrec - 1) { X->part_rec = X->cursor; X->part_done = (uint32_t)f[D_NPRIM]; goto out; }
            break;
        }
        case RQ_R_UPLOAD: do_upload(rq, f, l->data.p + f[U_DATA]); break;
        case RQ_R_COPY: do_copy(rq, f); break;
        case RQ_R_CLUT: do_clut(rq, f); break;
        case RQ_R_READBACK: do_readback(rq, f); break;
        case RQ_R_DISPLAY: do_display(rq, f); break;
        }
        X->cursor++;
    }
out:
    rq->be_ops->leave(rq->be);
    rq->st.f_consume_ns += rq_clock_ns() - t0;
}

/* Early consumption: without it the whole frame's records reach the backend at rq_present, and the first surface
 * download of the frame waits for the GPU to render all of it. Consuming every RQ_EARLY records (the backend flushes
 * its command stream on leave) lets the GPU work while the game builds the rest of the frame. Same records, same
 * order, same batches (an open batch stays open across consume calls, a growing DRAW record resumes at part_done):
 * only host timing changes. */
enum { RQ_EARLY = 128 };
void rqt_kick_done(Rq *rq)
{
    RqTex *X = rq->tex;
    if (X && rq->rec->nrec >= X->cursor + RQ_EARLY) rqt_consume(rq);
}

void rqt_frame_end(Rq *rq)
{
    if (!rq->tex) return;
    ws_frame(rq->tex, rq->st.vblank);
    rq->tex->cursor = 0;
    rq->tex->part_rec = ~0u;
}

void rqt_create(Rq *rq)
{
    RqTex *X = (RqTex *)calloc(1, sizeof(RqTex));
    X->stamp_now = 1;
    X->coh_epoch = 1;
    X->rs = rq->opt.scale < 1 ? 1 : rq->opt.scale > 4 ? 4 : rq->opt.scale;
    X->part_rec = ~0u;
    rq->tex = X;
}

void rqt_destroy(Rq *rq)
{
    RqTex *X = rq->tex;
    if (!X) return;
    free(X->wide);
    free(X->stg.data); free(X->stg.px); free(X->rd.px); free(X->rbc); free(X->cp_tmp); free(X->disp_px[0]); free(X->disp_px[1]);
    free(X);
    rq->tex = 0;
}

int rqt_output(Rq *rq, int *w, int *h)
{
    RqTex *X = rq->tex;
    if (!X || !X->out_any) { *w = *h = 0; return 0; }
    *w = X->out_w; *h = X->out_h;
    return 1;
}

void rqt_frame_stats(Rq *rq, RqFrameStats *fs)
{
    RqTex *X = rq->tex;
    if (!X) return;
    fs->flushes = X->st.flushes; fs->draws = X->st.draws; fs->prims = X->st.verts;
    fs->uploads = X->st.gs_up; fs->upload_kb = (X->st.gs_up_bytes + 1023) / 1024;
    fs->decodes = X->st.decodes; fs->tex_partial = X->st.partial;
    fs->surf_up = X->st.uploads; fs->surf_down = X->st.downloads;
    fs->gpu_copies = X->st.gpu_copies; fs->rt_tex = X->st.rt_tex;
    memset(&X->st, 0, sizeof X->st);
}

int rqt_summary(Rq *rq, char *buf, int n)
{
    RqTex *X = rq->tex;
    if (!X || n <= 0) return 0;
    int k = snprintf(buf, n, "rq surface syncs: down %llu px %.0f ms", (unsigned long long)X->dn_px, X->dn_ns * 1e-6);
    for (int i = 0; i < 8 && k < n; i++) if (X->dn_why[i]) k += snprintf(buf + k, n - k, " %s %llu", why_name[i], (unsigned long long)X->dn_why[i]);
    if (k < n) k += snprintf(buf + k, n - k, " | up %llu px %.0f ms", (unsigned long long)X->up_px, X->up_ns * 1e-6);
    for (int i = 0; i < 8 && k < n; i++) if (X->up_why[i]) k += snprintf(buf + k, n - k, " %s %llu", why_name[i], (unsigned long long)X->up_why[i]);
    if (k < n) k += snprintf(buf + k, n - k, "\n");
    return k;
}
