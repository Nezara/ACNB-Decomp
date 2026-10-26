// VRAM addressing for the render module's own 4 MB model (design section 2): forked from runtime/gs_swz.c,
// gs_tables.h (copy in rq_swz_tables.h) and gs_hw.c gs_block_range / layout_of / block_rect.
#include "rq_swz.h"
#include <string.h>
#include <stddef.h>
#include "rq_swz_tables.h"

int rqs_bits(int psm)
{
    switch (psm) {
    case RQ_PSMCT32: case RQ_PSMCT24: case RQ_PSMZ32: case RQ_PSMZ24: case RQ_PSMT8H: case RQ_PSMT4HL: case RQ_PSMT4HH: return 32;
    case RQ_PSMCT16: case RQ_PSMCT16S: case RQ_PSMZ16: case RQ_PSMZ16S: return 16;
    case RQ_PSMT8: return 8;
    default: return 4;
    }
}

int rqs_layout(int psm)
{
    switch (psm) {
    case RQ_PSMCT32: case RQ_PSMCT24: case RQ_PSMT8H: case RQ_PSMT4HL: case RQ_PSMT4HH: return 0;
    case RQ_PSMZ32: case RQ_PSMZ24: return 1;
    case RQ_PSMCT16: return 2;
    case RQ_PSMCT16S: return 3;
    case RQ_PSMZ16: return 4;
    case RQ_PSMZ16S: return 5;
    case RQ_PSMT8: return 6;
    default: return 7;
    }
}

void rqs_block_range(uint32_t bp, uint32_t bw, int psm, int y0, int y1, int wpx, uint32_t *lo, uint32_t *hi)
{
    int bits = rqs_bits(psm);
    int pw = bits >= 16 ? 64 : 128, ph = bits == 32 ? 32 : bits == 4 ? 128 : 64;
    uint32_t ppr = (bw * 64 + pw - 1) / pw, n = (uint32_t)(wpx + pw - 1) / pw;
    if (n > ppr) ppr = n;
    if (!ppr) ppr = 1;
    if (y0 < 0) y0 = 0;
    uint32_t r0 = (uint32_t)y0 / ph, r1 = ((uint32_t)y1 + ph - 1) / ph;
    *lo = bp + r0 * ppr * 32;
    *hi = bp + r1 * ppr * 32;
    if (*hi > RQ_GS_BLOCKS) *hi = RQ_GS_BLOCKS;
}

void rqs_block_rect(int psm, uint32_t tbp, uint32_t tbw, uint32_t b, int *x, int *y, int *bwpx, int *bhpx)
{
    int bits = rqs_bits(psm), pw = bits >= 16 ? 64 : 128, ph = bits == 32 ? 32 : bits == 4 ? 128 : 64;
    const uint8_t *tab;
    int cols;
    switch (rqs_layout(psm)) {
    case 0: tab = &rqs_block32[0][0]; cols = 8; *bwpx = 8; *bhpx = 8; break;
    case 1: tab = &rqs_block32z[0][0]; cols = 8; *bwpx = 8; *bhpx = 8; break;
    case 2: tab = &rqs_block16[0][0]; cols = 4; *bwpx = 16; *bhpx = 8; break;
    case 3: tab = &rqs_block16s[0][0]; cols = 4; *bwpx = 16; *bhpx = 8; break;
    case 4: tab = &rqs_block16z[0][0]; cols = 4; *bwpx = 16; *bhpx = 8; break;
    case 5: tab = &rqs_block16sz[0][0]; cols = 4; *bwpx = 16; *bhpx = 8; break;
    case 6: tab = &rqs_block8[0][0]; cols = 8; *bwpx = 16; *bhpx = 16; break;
    default: tab = &rqs_block4[0][0]; cols = 4; *bwpx = 32; *bhpx = 16; break;
    }
    uint32_t ppr = bits >= 16 ? tbw : tbw / 2;
    if (!ppr) ppr = 1;
    uint32_t rel = b - tbp, page = rel / 32, blk = rel % 32;
    int i = 0;
    while (i < 31 && tab[i] != blk) i++;
    *x = (int)(page % ppr) * pw + (i % cols) * *bwpx;
    *y = (int)(page / ppr) * ph + (i / cols) * *bhpx;
}

// ---- rect access, one 256-byte block at a time (gs_swz.c)
struct Lay {
    int pw, ph, bwp, bhp, bcols;
    const uint8_t *btab;
    int half_bw, kind;
    uint32_t mask; int sh;
    int pws, phs, bwps, bhps;   // log2 of pw / ph / bwp / bhp (powers of two; x, y >= 0 in the rect walkers)
};

static int log2i(int v) { int n = 0; while ((1 << n) < v) n++; return n; }
static int lay_fin(Lay *l) { l->pws = log2i(l->pw); l->phs = log2i(l->ph); l->bwps = log2i(l->bwp); l->bhps = log2i(l->bhp); return 1; }

static int lay_of(int psm, Lay *l)
{
    memset(l, 0, sizeof *l);
    l->mask = 0xffffffffu;
    switch (psm) {
    case RQ_PSMCT32: case RQ_PSMCT24: case RQ_PSMT8H: case RQ_PSMT4HL: case RQ_PSMT4HH:
    case RQ_PSMZ32: case RQ_PSMZ24:
        l->pw = 64; l->ph = 32; l->bwp = 8; l->bhp = 8; l->bcols = 8; l->kind = 32;
        l->btab = (psm == RQ_PSMZ32 || psm == RQ_PSMZ24) ? &rqs_block32z[0][0] : &rqs_block32[0][0];
        if (psm == RQ_PSMCT24 || psm == RQ_PSMZ24) l->mask = 0xffffff;
        else if (psm == RQ_PSMT8H) { l->mask = 0xff; l->sh = 24; }
        else if (psm == RQ_PSMT4HL) { l->mask = 0xf; l->sh = 24; }
        else if (psm == RQ_PSMT4HH) { l->mask = 0xf; l->sh = 28; }
        return lay_fin(l);
    case RQ_PSMCT16: case RQ_PSMCT16S: case RQ_PSMZ16: case RQ_PSMZ16S:
        l->pw = 64; l->ph = 64; l->bwp = 16; l->bhp = 8; l->bcols = 4; l->kind = 16;
        l->btab = psm == RQ_PSMCT16S ? &rqs_block16s[0][0] : psm == RQ_PSMZ16 ? &rqs_block16z[0][0] :
                  psm == RQ_PSMZ16S ? &rqs_block16sz[0][0] : &rqs_block16[0][0];
        return lay_fin(l);
    case RQ_PSMT8:
        l->pw = 128; l->ph = 64; l->bwp = 16; l->bhp = 16; l->bcols = 8; l->kind = 8; l->half_bw = 1;
        l->btab = &rqs_block8[0][0];
        return lay_fin(l);
    case RQ_PSMT4:
        l->pw = 128; l->ph = 128; l->bwp = 32; l->bhp = 16; l->bcols = 4; l->kind = 4; l->half_bw = 1;
        l->btab = &rqs_block4[0][0];
        return lay_fin(l);
    }
    return 0;
}

static inline uint8_t *block_ptr(uint8_t *vram, const Lay *l, uint32_t bp, uint32_t bw, int bx, int by)
{
    uint32_t pbw = l->half_bw ? bw >> 1 : bw;
    uint32_t page = (uint32_t)(by >> l->phs) * pbw + (uint32_t)(bx >> l->pws);
    uint32_t blk = (bp & 0x3fff) + page * 32 +
                   l->btab[((by & (l->ph - 1)) >> l->bhps) * l->bcols + ((bx & (l->pw - 1)) >> l->bwps)];
    return vram + (blk & 0x3fff) * 256;   // wraps at 4 MB
}

#define FOR_BLOCKS(...)                                                                      \
    for (int by = y0 & -l.bhp;    by < y0 + h; by += l.bhp) {                               \
        int r0 = by < y0 ? y0 - by : 0, r1 = by + l.bhp > y0 + h ? y0 + h - by : l.bhp;      \
        for (int bx = x0 & -l.bwp;    bx < x0 + w; bx += l.bwp) {                           \
            int c0 = bx < x0 ? x0 - bx : 0, c1 = bx + l.bwp > x0 + w ? x0 + w - bx : l.bwp;  \
            uint8_t *B = block_ptr(vram, &l, bp, bw, bx, by);                                \
            ptrdiff_t o = (ptrdiff_t)(by - y0) * stride + (bx - x0);                         \
            __VA_ARGS__                                                                      \
        }                                                                                    \
    }

void rqs_read_rect(uint8_t *vram, int psm, uint32_t bp, uint32_t bw, int x0, int y0, int w, int h, uint32_t *dst, int stride)
{
    Lay l;
    if (w <= 0 || h <= 0 || x0 < 0 || y0 < 0 || !lay_of(psm, &l)) return;
    switch (l.kind) {
    case 32: {
        uint32_t m = l.mask; int sh = l.sh;
        FOR_BLOCKS({
            const uint32_t *W = (const uint32_t *)B;
            for (int r = r0; r < r1; r++) {
                uint32_t *d = dst + o + (ptrdiff_t)r * stride;
                const uint8_t *col = rqs_col32[r];
                for (int c = c0; c < c1; c++) d[c] = (W[col[c]] >> sh) & m;
            }
        })
        break;
    }
    case 16:
        FOR_BLOCKS({
            const uint16_t *H = (const uint16_t *)B;
            for (int r = r0; r < r1; r++) {
                uint32_t *d = dst + o + (ptrdiff_t)r * stride;
                const uint8_t *col = rqs_col16[r];
                for (int c = c0; c < c1; c++) d[c] = H[col[c]];
            }
        })
        break;
    case 8:
        FOR_BLOCKS({
            for (int r = r0; r < r1; r++) {
                uint32_t *d = dst + o + (ptrdiff_t)r * stride;
                const uint8_t *col = rqs_col8[r];
                for (int c = c0; c < c1; c++) d[c] = B[col[c]];
            }
        })
        break;
    case 4:
        FOR_BLOCKS({
            for (int r = r0; r < r1; r++) {
                uint32_t *d = dst + o + (ptrdiff_t)r * stride;
                const uint16_t *col = rqs_col4[r];
                for (int c = c0; c < c1; c++) { uint32_t a = col[c]; d[c] = (B[a >> 1] >> ((a & 1) * 4)) & 15; }
            }
        })
        break;
    }
}

int rqs_write_rect(uint8_t *vram, int psm, uint32_t bp, uint32_t bw, int x0, int y0, int w, int h, const uint32_t *src,
                   int stride, uint32_t *lo, uint32_t *hi)
{
    Lay l;
    uint32_t clo = ~0u, chi = 0;
    if (w <= 0 || h <= 0 || x0 < 0 || y0 < 0 || !lay_of(psm, &l)) return 0;
#define MARK() if (diff) { uint32_t b = (uint32_t)((B - vram) >> 8); if (b < clo) clo = b; if (b >= chi) chi = b + 1; }
    switch (l.kind) {
    case 32: {
        uint32_t keep = ~(l.mask << l.sh), m = l.mask; int sh = l.sh;
        if (m == 0xffffffffu) {   // CT32 / Z32: whole words (the common case, no merge)
            FOR_BLOCKS({
                uint32_t *W = (uint32_t *)B, diff = 0;
                for (int r = r0; r < r1; r++) {
                    const uint32_t *s = src + o + (ptrdiff_t)r * stride;
                    const uint8_t *col = rqs_col32[r];
                    for (int c = c0; c < c1; c++) { uint32_t nw = s[c]; diff |= W[col[c]] ^ nw; W[col[c]] = nw; }
                }
                MARK()
            })
            break;
        }
        FOR_BLOCKS({
            uint32_t *W = (uint32_t *)B, diff = 0;
            for (int r = r0; r < r1; r++) {
                const uint32_t *s = src + o + (ptrdiff_t)r * stride;
                const uint8_t *col = rqs_col32[r];
                for (int c = c0; c < c1; c++) {
                    uint32_t old = W[col[c]], nw = (old & keep) | ((s[c] & m) << sh);
                    diff |= old ^ nw;
                    W[col[c]] = nw;
                }
            }
            MARK()
        })
        break;
    }
    case 16:
        FOR_BLOCKS({
            uint16_t *H = (uint16_t *)B;
            uint32_t diff = 0;
            for (int r = r0; r < r1; r++) {
                const uint32_t *s = src + o + (ptrdiff_t)r * stride;
                const uint8_t *col = rqs_col16[r];
                for (int c = c0; c < c1; c++) { uint16_t nw = (uint16_t)s[c]; diff |= H[col[c]] ^ nw; H[col[c]] = nw; }
            }
            MARK()
        })
        break;
    case 8:
        FOR_BLOCKS({
            uint32_t diff = 0;
            for (int r = r0; r < r1; r++) {
                const uint32_t *s = src + o + (ptrdiff_t)r * stride;
                const uint8_t *col = rqs_col8[r];
                for (int c = c0; c < c1; c++) { uint8_t nw = (uint8_t)s[c]; diff |= B[col[c]] ^ nw; B[col[c]] = nw; }
            }
            MARK()
        })
        break;
    case 4:
        FOR_BLOCKS({
            uint32_t diff = 0;
            for (int r = r0; r < r1; r++) {
                const uint32_t *s = src + o + (ptrdiff_t)r * stride;
                const uint16_t *col = rqs_col4[r];
                for (int c = c0; c < c1; c++) {
                    uint32_t a = col[c];
                    uint8_t *p = &B[a >> 1], old = *p;
                    uint8_t nw = (a & 1) ? (uint8_t)((old & 0x0f) | (s[c] << 4)) : (uint8_t)((old & 0xf0) | (s[c] & 15));
                    diff |= old ^ nw;
                    *p = nw;
                }
            }
            MARK()
        })
        break;
    }
#undef MARK
    if (lo) { *lo = clo; *hi = chi; }
    return chi != 0;
}

// single pixels through the rect walker (gs_swz.c --gs-selftest checks rect == gs_read_pixel / gs_write_pixel)
uint32_t rqs_read_pixel(uint8_t *vram, int psm, uint32_t bp, uint32_t bw, int x, int y)
{
    uint32_t v = 0;
    rqs_read_rect(vram, psm, bp, bw, x, y, 1, 1, &v, 1);
    return v;
}

void rqs_write_pixel(uint8_t *vram, int psm, uint32_t bp, uint32_t bw, int x, int y, uint32_t v)
{
    rqs_write_rect(vram, psm, bp, bw, x, y, 1, 1, &v, 1, 0, 0);
}
