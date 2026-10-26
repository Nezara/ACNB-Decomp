/* Render module public API (C-compatible). Contract:
 * The module turns the PS2's DMA / VIF1 / GIF / GS input into canonical records, hashes and (from step 1.2)
 * renders them. All state lives in one Rq instance; nothing here reads game state. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct Rq Rq;                 /* opaque */
typedef struct RqRecList RqRecList;   /* opaque; one per side in both mode */

enum { RQ_BACKEND_NULL, RQ_BACKEND_GL };

typedef struct {
    int scale, backend;          /* internal resolution 1..4 (backend-only); RQ_BACKEND_NULL | _GL */
    int write_readback, hash;    /* rule 11 (off in both mode); log "rq frame N: C records hash H" per vblank */
    int keep_records;            /* hash, compare or dump active: keep records until rq_present */
    const unsigned *dump_frames; int ndump, dump_all; const char *dump_dir;   /* text dumps, "build/rq" */
    int max_diff_lines;          /* both mode: frames with detailed diff lines (default 10) */
    /* step 5.1 resolution (video.resolution; 0 = off, the original picture at `scale`): reproject = bit mask of
     * (1 << RQ_K_*) kinds whose VU1-projected triangles are drawn from the program's float screen position instead of
     * the 12.4 GS one; layout2d = 2D kinds (HUD TEXT SPRITE2D, MENU sprites) evaluated on the 1x pixel grid (integer
     * scale of the original 2D picture). Neither changes records or hashes (consumer / backend only). */
    uint32_t reproject; int layout2d;
    /* step 5 display options (consumer / backend only: records, hashes and readbacks untouched; 0 = original):
     * blur_off = the battle blur passes (ScrEff Blur preset, sfx blur filter) blend nothing and the menu flip copy is
     * an exact, pixel-aligned opaque copy (no copy-blur / flicker filter); texfilter 1 = nearest, 2 = bilinear forced
     * on non-sprite primitives (game textures only, never render targets); aniso = max taps (2..16) along the
     * footprint's major axis of bilinear-filtered triangles; aa_resolve = 2: the picture is rendered at `scale` and
     * box-filtered down by 2 at display (supersampling; the caller doubled `scale`). */
    int blur_off, texfilter, aniso, aa_resolve;
} RqOptions;

Rq  *rq_create(const RqOptions *o);
void rq_destroy(Rq *rq);

/* inputs (kicks); ch 1 = VIF1, ch 2 = GIF; other channels ignored. chcr/madr/qwc/tadr = register values at STR time.
 * ch 1 with chcr.DIR = 0 is a local->host readback delivery to madr. Returns RQ_EV_* bits. */
uint32_t rq_dma_kick(Rq *rq, int ch, uint32_t chcr, uint32_t madr, uint32_t qwc, uint32_t tadr);
uint32_t rq_fifo_write(Rq *rq, int path /* 1 = VIF1 FIFO, 3 = GIF FIFO */, const uint32_t qw[4]);
/* VIF1 FIFO read (FDR set): the next qword of the local->host transfer into out16 (write_readback; both mode: the
 * compare buffer, out16 untouched - the reference delivers, then rq_readback_compare(rq, out16, 16)). pending =
 * data left (VIF1_STAT.FQC). */
void     rq_fifo_read(Rq *rq, void *out16);
int      rq_fifo_pending(Rq *rq);
void     rq_priv_write(Rq *rq, uint32_t addr /* 0x12000000.. */, int size, const void *val);
enum { RQ_EV_VIF1_IRQ = 1, RQ_EV_GS_SIGNAL = 2, RQ_EV_GS_FINISH = 4, RQ_EV_VU1_MISSING = 8 };
uint64_t rq_siglblid(const Rq *rq);

/* frame */
typedef struct { uint32_t records, vblank; uint64_t hash; int compared, differ; uint32_t first_rec;
                 char field[48]; uint64_t ref_val, rq_val; } RqFrameResult;
void rq_present(Rq *rq, uint32_t vblank, RqRecList *ref /* both mode, else NULL */, RqFrameResult *out);
void rq_flush(Rq *rq);
int  rq_output(Rq *rq, int *w, int *h);
void rq_read_output(Rq *rq, uint32_t *bgra, int w, int h, int stride);
void rq_blit_output(Rq *rq, int w, int h, int dx, int dy, int dw, int dh, int cw, int ch, int nearest);
/* Step 5.2 widescreen, consumer only (GL positions; records / hashes untouched: the Hor+ of video.aspect is render-side
 * only), set by the host per frame before its draws: ws = x scale of the Hor+ picture in the 4:3 frame ((4/3) /
 * aspect; 1 = off): the 3D drawn into frame-sized buffers (MODEL SHADOW EFFECT ENV, lens flare, lock sight / markers,
 * cockpit panel; untagged non-sprites with untagged3d) is squeezed about the frame centre; hud = how the 2D kinds (HUD
 * except lock / fade / cockpit, TEXT, SPRITE2D, MENU) are squeezed by ws to keep their proportions once the presenter
 * stretches the frame: 0 not (stretched), 1 about the frame centre (4:3 area), 2 each element about its own edge. */
void rq_set_widescreen(Rq *rq, float ws, float zoom, int hud, int untagged3d);   /* zoom: camera.fov (3D, x and y) */
/* Per-frame module work (perf hud, phase 3 hitch work): the counters of the frame closed by the last rq_present
 * (inputs from the previous present up to and including this one; readbacks after a present count to the next).
 * Consumer fields are 0 with the null backend. Host timing only - never part of records, hashes or state. */
typedef struct {
    uint32_t vblank, records, kicks;
    uint32_t draws, prims, flushes;      /* backend draw calls / primitives / batch flushes */
    uint32_t uploads, upload_kb;         /* GS host->local transfers consumed (UPLOAD records) */
    uint32_t decodes, tex_partial;       /* texture cache decodes (VRAM model -> GPU texture) / partial ones */
    uint32_t surf_up, surf_down;         /* surface syncs: VRAM model -> render target / render target -> model */
    uint32_t gpu_copies, rt_tex, readbacks, syncs;   /* syncs = consumer runs (sync points) */
    float kick_ms, consume_ms, present_ms;   /* host CPU in rq_dma_kick + rq_fifo_write / in the consumer (part of
                                                both others) / in rq_present */
} RqFrameStats;
void rq_frame_stats(const Rq *rq, RqFrameStats *out);
int  rq_stats(Rq *rq, char *buf, int n);   /* the last frame's RqFrameStats as one text line */
int  rq_summary(Rq *rq, char *buf, int n);
/* both mode (write_readback off, GL backend): the module packs each local->host delivery into its own buffer; the host
 * calls this right after the reference delivered the same kick into `ref` and gets the number of differing bytes
 * (0 = byte-exact). Counted in rq_summary ("readbacks .., byte mismatches .."); the first max_diff_lines deliveries
 * that differ are logged ("rq readback diff: .."). Returns 0 without comparing when nothing was packed (null
 * backend: no VRAM model). */
uint32_t rq_readback_compare(Rq *rq, const void *ref, uint32_t nbytes);

/* tags (design sections 7 and 17). Never hashed, never compared, never change record counts. A record gets the current
 * packet tag, else the top of the scope stack, else NONE. rq_tag_packet attaches to the DMA-side address the walker
 * visits (chain tag qword, normal-mode MADR, REF target); valid in the frame attached and the next; re-attach
 * overwrites; a hit with kind NONE clears the packet tag (end marker after a tagged run of packets). */
typedef struct { uint16_t kind, sub; uint32_t flags; uint64_t objid; } RqTag;
enum { RQ_K_NONE, RQ_K_MODEL, RQ_K_SHADOW, RQ_K_EFFECT, RQ_K_SPRITE2D, RQ_K_HUD, RQ_K_TEXT, RQ_K_MENU, RQ_K_POST,
       RQ_K_ENV, RQ_K_UPLOAD, RQ_K_READBACK, RQ_K_N };
/* object classes: objid = class << 32 | instance (stable ids for phase 4) */
enum { RQ_OC_NONE, RQ_OC_STAGE_MESH, RQ_OC_AC_PART, RQ_OC_CHR, RQ_OC_ITEM, RQ_OC_WEAPON, RQ_OC_EFFECT, RQ_OC_HUD_ELEM,
       RQ_OC_MENU, RQ_OC_TEXTURE };
#define RQ_OBJID(cls, inst) ((uint64_t)(cls) << 32 | (uint32_t)(inst))
void rq_tag_packet(Rq *rq, uint32_t ps2addr, const RqTag *t);
void rq_tag_scope(Rq *rq, const RqTag *t);   /* push (depth 8; deeper pushes are counted, the 8th stays on top) */
void rq_tag_end(Rq *rq);                     /* pop */
typedef struct { uint16_t kind, part; float pos[3], normal[3]; uint64_t objid; } RqEvent;
void rq_event(Rq *rq, const RqEvent *e);

/* ---- records and canonical form (design section 5) */
enum { RQ_R_DRAW = 1, RQ_R_UPLOAD, RQ_R_COPY, RQ_R_CLUT, RQ_R_READBACK, RQ_R_DISPLAY, RQ_R_N };
enum { RQ_C_POINT, RQ_C_LINE, RQ_C_TRI, RQ_C_SPRITE, RQ_C_NONE };

/* GS context registers, in the order of the reference's GSContext (the tee passes it as is) */
enum { RQ_CTX_XYOFFSET, RQ_CTX_TEX0, RQ_CTX_TEX1, RQ_CTX_TEX2, RQ_CTX_CLAMP, RQ_CTX_MIPTBP1, RQ_CTX_MIPTBP2,
       RQ_CTX_SCISSOR, RQ_CTX_ALPHA, RQ_CTX_TEST, RQ_CTX_FBA, RQ_CTX_FRAME, RQ_CTX_ZBUF, RQ_CTX_N };
/* global registers the key needs: TEXA 0x3b, FOGCOL 0x3d, PABE 0x49, COLCLAMP 0x46, DTHE 0x45, DIMX 0x44 */
enum { RQ_GLOB_TEXA, RQ_GLOB_FOGCOL, RQ_GLOB_PABE, RQ_GLOB_COLCLAMP, RQ_GLOB_DTHE, RQ_GLOB_DIMX, RQ_GLOB_N };
/* privileged registers: 0x12000000 + i * 0x10 for i < 15, then CSR, IMR, BUSDIR, SIGLBLID */
enum { RQ_PRIV_PMODE, RQ_PRIV_SMODE1, RQ_PRIV_SMODE2, RQ_PRIV_SRFSH, RQ_PRIV_SYNCH1, RQ_PRIV_SYNCH2, RQ_PRIV_SYNCV,
       RQ_PRIV_DISPFB1, RQ_PRIV_DISPLAY1, RQ_PRIV_DISPFB2, RQ_PRIV_DISPLAY2, RQ_PRIV_EXTBUF, RQ_PRIV_EXTDATA,
       RQ_PRIV_EXTWRITE, RQ_PRIV_BGCOLOR, RQ_PRIV_CSR, RQ_PRIV_IMR, RQ_PRIV_BUSDIR, RQ_PRIV_SIGLBLID, RQ_PRIV_N };
static inline uint32_t rq_priv_offset(int i)   /* byte offset in the 0x12000000 window */
{
    static const uint16_t hi[4] = {0x1000, 0x1010, 0x1040, 0x1080};
    return i < 15 ? (uint32_t)i * 0x10 : hi[i - 15];
}

/* canonical draw key (masked fields, design section 5 table) */
enum { RQ_KEY_ATTR, RQ_KEY_FRAME, RQ_KEY_ZBUF, RQ_KEY_TEST, RQ_KEY_ALPHA, RQ_KEY_PABE, RQ_KEY_FBA, RQ_KEY_TEX0,
       RQ_KEY_TEX1, RQ_KEY_CLAMP, RQ_KEY_TEXA, RQ_KEY_MIPTBP1, RQ_KEY_MIPTBP2, RQ_KEY_FOGCOL, RQ_KEY_SCISSOR,
       RQ_KEY_COLCLAMP, RQ_KEY_DTHE, RQ_KEY_DIMX, RQ_KEY_N };
enum { RQ_VF_STQ = 1, RQ_VF_UV = 2, RQ_VF_FOG = 4 };
typedef struct {
    uint64_t f[RQ_KEY_N];
    uint32_t zmax;      /* 0 = Z unused (z canonical 0), else the ZBUF format max */
    uint32_t vflags;    /* RQ_VF_*: which vertex fields are live */
    uint32_t cls;       /* RQ_C_* from the PRIM type */
} RqKey;
/* GS-domain vertex: x, y 12.4 after XYOFFSET; s t q float bits; u v 14.4 */
typedef struct { int32_t x, y; uint32_t z; uint32_t s, t, q; uint16_t u, v; uint8_t r, g, b, a, f, pad[3]; } RqVert;

/* canonicalizer - shared by the module frontend and the hybrid reference tee (gs.c) */
RqRecList *rq_rec_create(void);
void rq_rec_destroy(RqRecList *l);
void rq_key_build(RqKey *k, uint64_t attr /* effective PRIM attr, type in bits 0-2 */, const uint64_t ctx[RQ_CTX_N],
                  const uint64_t glob[RQ_GLOB_N]);
void rq_rec_prim(RqRecList *l, const RqKey *k, const RqVert *v /* GS-domain, unmasked */, int nv);
void rq_rec_upload_begin(RqRecList *l, uint64_t bitbltbuf, uint64_t trxpos, uint64_t trxreg);
void rq_rec_upload_data(RqRecList *l, const uint8_t *d, uint32_t n);
void rq_rec_copy(RqRecList *l, uint64_t bitbltbuf, uint64_t trxpos, uint64_t trxreg);
void rq_rec_readback(RqRecList *l, uint64_t bitbltbuf, uint64_t trxpos, uint64_t trxreg);
void rq_rec_readback_data(RqRecList *l, uint32_t qwc);   /* a ch 1 DIR = 0 delivery of qwc quadwords */
void rq_rec_clut(RqRecList *l, uint64_t tex0 /* with CLD */, uint64_t texclut);
void rq_rec_display(RqRecList *l, const uint64_t priv[RQ_PRIV_N]);
uint32_t rq_rec_count(const RqRecList *l);

#ifdef __cplusplus
}
#endif
