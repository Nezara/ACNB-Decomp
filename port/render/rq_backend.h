/* Backend interface (design section 4.4): no GL types. rq_tex does all PS2-domain work (swizzle, 16-bit <-> RGBA8,
 * palettes, stamps, dirty / stale rows, which rows to sync) and hands decoded RGBA + row ranges to the backend; the
 * backend owns only GPU objects. Ids: targets are backend handles (0 = none); textures (tex_create, target_snapshot)
 * are a second id space; RqBatch.tex_is_target says which one RqBatch.tex is. All sizes / rows are in GS pixels -
 * the internal resolution (RqOptions.scale) is the backend's business. */
#pragma once
#include "rq.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct { float x, y; uint32_t z; float s, t, q; uint8_t r, g, b, a; float fog; } RqHwVert;   /* = gs_hw HwVert */
typedef struct {
    uint32_t color, depth; int tw, th;          /* target ids (0 = none), target size in GS pixels */
    int sx0, sy0, sx1, sy1, prim;               /* SCISSOR (inclusive); 0 points 1 lines 2 tris (sprites expanded) */
    uint32_t first, n;                          /* vertices in the backend's vertex memory (vtx_alloc) */
    int tme, fst, iip, fge; uint32_t tex; int tex_is_target, amode, glw, glh, tex_rs;   /* cache entry or snapshot */
    int tfx, tcc, tsw, tsh, wms, wmt, minu, maxu, minv, maxv, ta0, ta1, aem, linear; uint32_t fogcol;
    int abe, ba, bb, bc, bd, fix, fba;          /* raw GS ALPHA fields; the backend maps them (gs_hw blend_setup) */
    int ate, atst, aref, afail, zte, ztst, zmsk; uint32_t zmax; int cmask[4];   /* cmask: FBMSK + PSM rules */
    int snap;   /* step 5.1 2D layout: > 1 = evaluate texture / colour / fog at the first sub-pixel of each GS pixel
                   (the internal-resolution factor; integer scale of the 1x picture), 0 = per scaled pixel */
    int aniso;  /* step 5 video.aniso: > 1 = up to this many bilinear taps along the footprint's major axis */
} RqBatch;
typedef struct {
    int en[2], w, h, mmod, slbg, alp; uint32_t bg;             /* merged-picture size in GS pixels */
    struct { uint32_t target; const uint32_t *rgba; int rw, rh, sx, sy, cls, ox, oy; } c[2];   /* ox/oy: output offset */
} RqDisplayIn;
typedef struct RqBackendOps {
    int  (*init)(void *be, const RqOptions *o);  void (*shutdown)(void *be);
    void (*enter)(void *be);  void (*leave)(void *be);         /* save / restore host API state (shared context) */
    uint32_t (*tex_create)(void *be, int w, int h);  void (*tex_destroy)(void *be, uint32_t t);   /* RGBA8 */
    void (*tex_update)(void *be, uint32_t t, int x, int y, int w, int h, const uint32_t *rgba, int stride);
    uint32_t (*target_create)(void *be, int depth, int w, int h);  void (*target_destroy)(void *be, uint32_t t);
    void (*target_upload)(void *be, uint32_t t, int x, int y0, int y1, int w, const uint32_t *px);   /* RGBA8 | raw Z;
                                                     columns [x, x + w), rows [y0, y1), px stride w */
    void (*target_download)(void *be, uint32_t t, int y0, int y1, int w, uint32_t *px);       /* 1x, nearest */
    void (*target_copy)(void *be, uint32_t dst, uint32_t src, int y0, int y1, int w, uint32_t zmask, int keep_a);
    uint32_t (*target_snapshot)(void *be, uint32_t t, int w, int vh);   /* sampling the target being drawn: a tex id */
    /* vertex memory for one batch, written in place (persistent ring): returns room for *cap (<= max) vertices
     * starting at vertex *first; draw() commits first + n */
    RqHwVert *(*vtx_alloc)(void *be, uint32_t max, uint32_t *first, uint32_t *cap);
    void (*draw)(void *be, const RqBatch *b);
    int  (*display)(void *be, const RqDisplayIn *d, int *w, int *h);   /* fs_disp merge; w,h at internal resolution */
    void (*read_output)(void *be, uint32_t *bgra, int w, int h, int stride);
    void (*blit_output)(void *be, int w, int h, int dx, int dy, int dw, int dh, int cw, int ch, int nearest);
    int  (*summary)(void *be, char *buf, int n);   /* optional exit stats line(s) appended to rq_summary */
    uint32_t be_size;   /* bytes of backend instance state rq_create allocates for `be` */
} RqBackendOps;
extern const RqBackendOps rq_backend_null, rq_backend_gl;

#ifdef __cplusplus
}
#endif
