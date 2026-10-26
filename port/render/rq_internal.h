/* struct Rq and its parts (design section 6). C++ only, never included outside port/render/. */
#pragma once
#include "rq.h"
#include "rq_host.h"
#include "rq_vu1.h"
#include "rq_backend.h"
#include <string.h>
#include <stdlib.h>

/* DMA-side address folding (design 4.2). Hybrid: PS2 windows - scratchpad by bit 31 or 0x7xxxxxxx, RAM by the 32 MB
 * physical window. RQ_HOST_ADDR (sourceport build, step 1.4): packet words hold 32-bit host pointers (PS2PTR of an
 * i686 build; the scratchpad is the host array g_ps2_spr), so no folding beyond the DMA's 16-byte alignment. */
#ifdef RQ_HOST_ADDR
static inline int rq_addr_is_spr(uint32_t) { return 0; }
static inline uint32_t rq_addr_fold(uint32_t a) { return a & ~15u; }
#else
static inline int rq_addr_is_spr(uint32_t a) { return (a & 0x80000000u) || (a & 0x70000000u) == 0x70000000u; }
static inline uint32_t rq_addr_fold(uint32_t a) { return rq_addr_is_spr(a) ? 0x70000000u | (a & 0x3ff0) : a & 0x01fffff0u; }
#endif

/* growable byte buffer; contents addressed by offset (rule 6: no pointers kept across appends) */
struct RqBuf { uint8_t *p; uint32_t n, cap; };
uint32_t rq_buf_alloc(RqBuf *b, uint32_t sz);   /* zero-filled; returns the offset */
void rq_buf_free(RqBuf *b);

struct RqRec {
    uint8_t type, cls; uint16_t nf;
    uint32_t index;
    RqTag tag;              /* never hashed; DRAW: the tag of the first vertex run */
    uint32_t off;           /* fields: nf x uint64_t at `fields` + off */
    uint32_t vtx, nvert;    /* DRAW: canonical RqVert run in `verts` */
    uint32_t xrun, nxrun;   /* DRAW: later tag runs (RqTagRun at `runs` index xrun..), see rq_rec_prim */
};

/* a tag change inside a coalesced DRAW record: vertices from `vfirst` (record-relative) on carry `tag` */
struct RqTagRun { uint32_t vfirst; RqTag tag; };

/* field indices of the record types (rq_rec.cpp tables; rq_tex.cpp consumes them) */
enum { U_DBP, U_DBW, U_DPSM, U_DSAX, U_DSAY, U_RRW, U_RRH, U_TOTAL, U_SKIP, U_NBYTES, U_PARTIAL, U_HASH, U_DATA, U_N };
enum { D_NPRIM = RQ_KEY_N, D_NVERT, D_VUIN, D_N };
enum { RB_QWC = 7 };

struct RqRecList {
    RqBuf recs, fields, verts, data, runs;
    RqBuf hxy;              /* step 5.1 (module list, RqOptions.reproject): float2 per vertex parallel to `verts` -
                               the re-projected position minus the canonical one, in GS pixels; never hashed */
    int hxy_on;
    uint32_t nrec;
    RqTag tag;              /* tag for new records */
    /* host->local transfer in progress (UPLOAD record, partial / continuation rule) */
    int up_open, up_cont;
    uint32_t up_rec, up_total, up_have, up_rec_have;
    uint64_t up_hash, up_bb, up_pos, up_reg;
    int32_t rb_rec;         /* last READBACK record this frame, -1 none */
    uint32_t partials;      /* uploads closed early (run total) */
    uint32_t hash_done;     /* records before this index have U_HASH filled (lazy: rqr_fill_hashes) */
};

struct RqVif {
    uint32_t cl, wl, mode, mask, row[4], col[4];
    uint32_t base, ofst, tops, itops, dbf, mark;
    uint32_t code, need, have, buf[1024 + 8];
    int direct; uint32_t direct_qw[4]; int direct_n;
};

struct RqVu {
    uint8_t data[0x4000];   // calloc on i686 is 8-aligned: no over-alignment in struct Rq
    uint8_t micro[0x4000];
    RqVu1Hxy hxy[1024];     /* step 5.1 sideband (rq_vu1.h), live when RqOptions.reproject */
    RqVuRegs regs;
    uint32_t top, itop;
    uint32_t prog_hash[RQ_VU1_MAXPROG];
    int micro_dirty;
    RqVu1Ctx ctx;           /* persistent counters / warnings; mem, regs, top refreshed per call */
    uint32_t calls, missing;
};

struct RqGifPath { uint64_t tag0, regs; uint32_t nloop, nreg, flg, reg_i; int active; };

struct RqGs {
    uint64_t reg[0x80];
    uint64_t ctx[2][RQ_CTX_N];
    uint64_t prim, prmode;
    float q;
    RqVert vq[3];
    int nv;
    uint32_t cbp0, cbp1;
    int tr_active; uint32_t tr_bytes_left;
    uint8_t priv[0x2000];   /* privileged register shadow (0x12000000 window) */
    RqKey key; int key_ok;  /* emit_prim's cached rq_key_build result; cleared by any non-vertex register write */
    float hx, hy; int hok;  /* step 5.1: residual of the XYZ2 qword being written (packed_write), for vertex_kick */
    float vh[3][2];         /* residuals parallel to vq */
    int vhok[3];            /* 1 = vh holds a sideband residual (coverage stats only) */
};

struct RqStats {
    uint64_t frames, records, differ_frames, compared;
    uint32_t diff_lines;
    int have_first; RqFrameResult first;
    uint64_t events;
    uint64_t reproj_tris;   /* step 5.1: triangles recorded with residuals (kind in RqOptions.reproject) */
    uint64_t rp_tris[RQ_K_N], rp_hit[RQ_K_N];   /* step 5.1 coverage: triangles per tag kind / with a sideband residual
                                                 at every vertex (any kind, while the sideband is live) */
    /* tag coverage (rq_tag.cpp): segments = records + extra DRAW tag runs */
    uint64_t tag_segs[RQ_K_N], tag_verts[RQ_K_N];
    uint64_t tag_attach, tag_hits, tag_scopes, tag_unbalanced, tag_overflow, tag_runs;
    /* local->host readbacks: deliveries seen / bytes, both-mode compares (rq_readback_compare) */
    uint64_t rb_kicks, rb_bytes, rb_cmp, rb_bad_kicks, rb_bad_bytes;
    uint32_t rb_lines, vblank;   /* vblank = last rq_present (readbacks happen after it) */
    /* per-frame work (rq_frame_stats): the open frame's host timing + counts, closed by rq_present into `last` */
    uint64_t f_kick_ns, f_consume_ns, f_rb0;
    uint32_t f_kicks, f_syncs;
    uint64_t r_kick_ns, r_consume_ns, r_present_ns, r_syncs;   /* run totals of the above (rq_summary) */
    RqFrameStats last;
};

/* packet tag side table: open addressing keyed by the DMA-side address; two generations (frame attached + next) */
struct RqTagEnt { uint32_t addr, used; RqTag tag; };
enum { RQ_TAGTAB_BITS = 14, RQ_TAGTAB_N = 1 << RQ_TAGTAB_BITS };
struct RqTagTab { RqTagEnt *e; uint32_t n; };

struct Rq {
    RqOptions opt;
    unsigned dump_frames[64];
    const RqBackendOps *be_ops; void *be;
    uint32_t asr[3][2];     /* DMA chain call stack (Dn_ASR0/1) per channel */
    RqVif vif;
    RqVu vu;
    RqGifPath gif[4];
    RqGs gs;
    RqTag scope[8]; int nscope;
    RqTagTab tagtab[2]; int tagcur;   /* tagcur = this frame's table */
    RqTag ptag;             /* current packet tag (kind NONE = none); reset at the end of every kick */
    RqRecList *rec;
    uint32_t ev;            /* RQ_EV_* raised during the current input call */
    RqStats st;
    struct RqTex *tex;      /* consumer (VRAM model, surfaces, texture cache); NULL with the null backend */
};

/* rq_packet.cpp */
void rqp_vif_write(Rq *rq, const uint32_t *w, uint32_t n);
uint32_t rqp_gif_transfer(Rq *rq, int path, const uint8_t *d, uint32_t qwc);
void rqp_xgkick(Rq *rq, uint32_t addr);
/* rq_gs.cpp */
void rqg_write_reg(Rq *rq, int reg, uint64_t v);
void rqg_tr_write(Rq *rq, const uint8_t *d, uint32_t n);
/* rq_vu1.cpp */
void rqv_exec(Rq *rq, uint32_t addr);
void rqv_micro_written(Rq *rq);
/* rq_tex.cpp: the record consumer (design section 3), runs at sync points */
void rqt_create(Rq *rq);
void rqt_destroy(Rq *rq);
void rqt_consume(Rq *rq);                  /* execute the records not yet consumed (stops at an open UPLOAD) */
void rqt_frame_end(Rq *rq);                /* the record list is about to be cleared */
void rqt_kick_done(Rq *rq);                /* after a kick: consume early once enough records wait (GPU overlap) */
void rqt_readback_deliver(Rq *rq, uint32_t madr, uint32_t qwc);   /* ch 1 DIR = 0 kick (rule 11) */
void rqt_fifo_read(Rq *rq, uint8_t out[16]);   /* one VIF1 FIFO qword of the open local->host transfer */
int  rqt_fifo_pending(Rq *rq);                 /* transfer data left (VIF1_STAT.FQC) */
int  rqt_output(Rq *rq, int *w, int *h);
void rqt_frame_stats(Rq *rq, RqFrameStats *fs);   /* consumer counters of the open frame -> fs, reset */
int  rqt_summary(Rq *rq, char *buf, int n);       /* run totals of the surface syncs by cause (one line) */
static inline RqRec *rqr_at(const RqRecList *l, uint32_t i) { return (RqRec *)l->recs.p + i; }
static inline uint64_t *rqr_fld(const RqRecList *l, const RqRec *r) { return (uint64_t *)(l->fields.p + r->off); }
/* rq_tag.cpp */
void rqk_create(Rq *rq);
void rqk_destroy(Rq *rq);
void rqk_visit(Rq *rq, uint32_t addr);     /* DMA walker: tadr / madr / REF target visited */
void rqk_kick_end(Rq *rq);
void rqk_frame(Rq *rq);                    /* rq_present, before the record list is cleared: coverage + generation */
int  rqk_summary(const Rq *rq, char *buf, int n);
/* rq_rec.cpp */
static inline const RqTagRun *rqr_run(const RqRecList *l, uint32_t i) { return (const RqTagRun *)l->runs.p + i; }
void rqr_close_frame(RqRecList *l);
uint64_t rqr_hash(const RqRecList *l);
/* first difference: returns 1 and fills r->first_rec / field / values */
int  rqr_compare(const RqRecList *ref, const RqRecList *mod, RqFrameResult *r);
void rqr_clear(RqRecList *l);
void rqr_prim_hxy(RqRecList *l, const float (*h)[2], int nv);   /* step 5.1: residuals of the vertices just added */
void rqr_dump(const RqRecList *l, const char *path);
