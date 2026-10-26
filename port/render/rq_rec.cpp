// Records: list + arenas, canonical masks (rq_key_build), FNV-1a 64 frame hash, both-mode compare, text dump.
// Also the frame API (rq_create / rq_present / rq_summary). Tags: rq_tag.cpp (records carry them, never hashed).
// rq_key_build and the rq_rec_* functions are shared with the hybrid reference tee (runtime/gs.c).
#include "rq_internal.h"
#include <stdio.h>

static uint32_t buf_grow(RqBuf *b, uint32_t sz)   /* uninitialised room for sz bytes */
{
    if (b->n + sz > b->cap) {
        uint32_t c = b->cap ? b->cap : 4096;
        while (c < b->n + sz) c *= 2;
        b->p = (uint8_t *)realloc(b->p, c);
        b->cap = c;
    }
    uint32_t o = b->n;
    b->n += sz;
    return o;
}

uint32_t rq_buf_alloc(RqBuf *b, uint32_t sz)
{
    if (b->n + sz > b->cap) {
        uint32_t c = b->cap ? b->cap : 4096;
        while (c < b->n + sz) c *= 2;
        b->p = (uint8_t *)realloc(b->p, c);
        b->cap = c;
    }
    uint32_t o = b->n;
    memset(b->p + o, 0, sz);
    b->n += sz;
    return o;
}

void rq_buf_free(RqBuf *b) { free(b->p); b->p = 0; b->n = b->cap = 0; }

// ---------------------------------------------------------------- field tables (one per record type)


static const char *const draw_f[D_N] = {"key.attr", "key.frame", "key.zbuf", "key.test", "key.alpha", "key.pabe", "key.fba",
    "key.tex0", "key.tex1", "key.clamp", "key.texa", "key.miptbp1", "key.miptbp2", "key.fogcol", "key.scissor",
    "key.colclamp", "key.dthe", "key.dimx", "nprim", "nvert", "vu_in"};
static const char *const upload_f[U_N] = {"dbp", "dbw", "dpsm", "dsax", "dsay", "rrw", "rrh", "total", "skip", "nbytes",
    "partial", "data_hash", "data"};
static const char *const copy_f[] = {"sbp", "sbw", "spsm", "ssax", "ssay", "dbp", "dbw", "dpsm", "dsax", "dsay", "dir", "rrw", "rrh"};
static const char *const readback_f[] = {"sbp", "sbw", "spsm", "ssax", "ssay", "rrw", "rrh", "delivered_qwc"};
static const char *const clut_f[] = {"psm", "cbp", "cpsm", "csm", "csa", "cld", "texclut"};
static const char *const display_f[] = {"pmode", "smode2", "dispfb1", "display1", "dispfb2", "display2", "bgcolor"};

struct RqType { const char *name; const char *const *f; uint16_t nf, nhash; };
static const RqType types[RQ_R_N] = {
    {"?", 0, 0, 0},
    {"DRAW", draw_f, D_N, D_N},
    {"UPLOAD", upload_f, U_N, U_DATA},   // the arena offset is not hashed
    {"COPY", copy_f, 13, 13},
    {"CLUT", clut_f, 7, 7},
    {"READBACK", readback_f, 8, 8},
    {"DISPLAY", display_f, 7, 7},
};
static const char *const vert_f[] = {"x", "y", "z", "s", "t", "q", "u", "v", "r", "g", "b", "a", "f"};
static const char *const cls_n[] = {"POINT", "LINE", "TRI", "SPRITE", "NONE"};

static uint64_t vert_field(const RqVert *v, int i)
{
    switch (i) {
    case 0: return (uint32_t)v->x; case 1: return (uint32_t)v->y; case 2: return v->z;
    case 3: return v->s; case 4: return v->t; case 5: return v->q; case 6: return v->u; case 7: return v->v;
    case 8: return v->r; case 9: return v->g; case 10: return v->b; case 11: return v->a; default: return v->f;
    }
}

static RqRec *rec_at(const RqRecList *l, uint32_t i) { return (RqRec *)l->recs.p + i; }
static uint64_t *fld(const RqRecList *l, const RqRec *r) { return (uint64_t *)(l->fields.p + r->off); }
static const RqVert *verts(const RqRecList *l, const RqRec *r) { return (const RqVert *)l->verts.p + r->vtx; }

static uint64_t fnv(uint64_t h, const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    for (uint32_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001b3ull;
    return h;
}

// ---------------------------------------------------------------- list

RqRecList *rq_rec_create(void)
{
    RqRecList *l = (RqRecList *)calloc(1, sizeof(RqRecList));
    l->rb_rec = -1;
    return l;
}

void rq_rec_destroy(RqRecList *l)
{
    if (!l) return;
    rq_buf_free(&l->recs); rq_buf_free(&l->fields); rq_buf_free(&l->verts); rq_buf_free(&l->data); rq_buf_free(&l->runs); rq_buf_free(&l->hxy);
    free(l);
}

uint32_t rq_rec_count(const RqRecList *l) { return l->nrec; }

static uint32_t rec_new(RqRecList *l, int type, int cls)
{
    uint32_t o = rq_buf_alloc(&l->recs, sizeof(RqRec));
    uint32_t fo = rq_buf_alloc(&l->fields, types[type].nf * 8);
    RqRec *r = (RqRec *)(l->recs.p + o);
    r->type = (uint8_t)type; r->cls = (uint8_t)cls; r->nf = types[type].nf;
    r->index = l->nrec; r->tag = l->tag; r->off = fo;
    return l->nrec++;
}

static void up_close(RqRecList *l, int partial)
{
    uint64_t *f = fld(l, rec_at(l, l->up_rec));
    f[U_NBYTES] = l->up_rec_have;
    f[U_PARTIAL] = partial;
    f[U_HASH] = 0;   // filled lazily from the record's data (rqr_fill_hashes): only hashes / compares / dumps read it
    l->up_open = 0;
    if (partial) { l->partials++; l->up_cont = 1; }
}

// any other record (or the frame end) while an upload still misses bytes closes it as partial; the remaining
// bytes then open a continuation UPLOAD (same rect, skip = bytes before it) - the gs_tr_sync semantics
static void before_other(RqRecList *l)
{
    if (l->up_open && l->up_have < l->up_total) up_close(l, 1);
}

static void up_open_rec(RqRecList *l)
{
    uint32_t i = rec_new(l, RQ_R_UPLOAD, RQ_C_NONE);
    uint64_t *f = fld(l, rec_at(l, i)), bb = l->up_bb, pos = l->up_pos, reg = l->up_reg;
    f[U_DBP] = (bb >> 32) & 0x3fff; f[U_DBW] = (bb >> 48) & 0x3f; f[U_DPSM] = (bb >> 56) & 0x3f;
    f[U_DSAX] = (pos >> 32) & 0x7ff; f[U_DSAY] = (pos >> 48) & 0x7ff;
    f[U_RRW] = reg & 0xfff; f[U_RRH] = (reg >> 32) & 0xfff;
    f[U_TOTAL] = l->up_total; f[U_SKIP] = l->up_have; f[U_DATA] = l->data.n;
    l->up_rec = i; l->up_rec_have = 0; l->up_hash = 0xcbf29ce484222325ull;
    l->up_open = 1; l->up_cont = 0;
}

static int psm_bits(int psm)
{
    switch (psm) {
    case 0x00: case 0x30: return 32;
    case 0x01: case 0x31: return 24;
    case 0x02: case 0x0a: case 0x32: case 0x3a: return 16;
    case 0x13: case 0x1b: return 8;
    default: return 4;
    }
}

static int paletted(int psm) { return psm == 0x13 || psm == 0x1b || psm == 0x14 || psm == 0x24 || psm == 0x2c; }

void rq_rec_upload_begin(RqRecList *l, uint64_t bb, uint64_t pos, uint64_t reg)
{
    before_other(l);
    l->up_open = 0; l->up_cont = 0;   // a new transfer abandons the old one (gs.c tr_begin)
    uint64_t total = (reg & 0xfff) * ((reg >> 32) & 0xfff) * (uint64_t)psm_bits((int)((bb >> 56) & 0x3f)) / 8;
    if (!total) return;
    l->up_bb = bb; l->up_pos = pos; l->up_reg = reg;
    l->up_total = (uint32_t)total; l->up_have = 0;
    up_open_rec(l);
}

void rq_rec_upload_data(RqRecList *l, const uint8_t *d, uint32_t n)
{
    if (!l->up_open) {
        if (!l->up_cont) return;
        up_open_rec(l);
    }
    if (n > l->up_total - l->up_have) n = l->up_total - l->up_have;
    uint32_t o = buf_grow(&l->data, n);   // every byte written below
    memcpy(l->data.p + o, d, n);
    l->up_have += n; l->up_rec_have += n;
    if (l->up_have == l->up_total) up_close(l, 0);
}

void rq_rec_copy(RqRecList *l, uint64_t bb, uint64_t pos, uint64_t reg)
{
    before_other(l);
    uint64_t *f = fld(l, rec_at(l, rec_new(l, RQ_R_COPY, RQ_C_NONE)));
    f[0] = bb & 0x3fff; f[1] = (bb >> 16) & 0x3f; f[2] = (bb >> 24) & 0x3f; f[3] = pos & 0x7ff; f[4] = (pos >> 16) & 0x7ff;
    f[5] = (bb >> 32) & 0x3fff; f[6] = (bb >> 48) & 0x3f; f[7] = (bb >> 56) & 0x3f; f[8] = (pos >> 32) & 0x7ff;
    f[9] = (pos >> 48) & 0x7ff; f[10] = (pos >> 59) & 3; f[11] = reg & 0xfff; f[12] = (reg >> 32) & 0xfff;
}

void rq_rec_readback(RqRecList *l, uint64_t bb, uint64_t pos, uint64_t reg)
{
    before_other(l);
    uint32_t i = rec_new(l, RQ_R_READBACK, RQ_C_NONE);
    uint64_t *f = fld(l, rec_at(l, i));
    f[0] = bb & 0x3fff; f[1] = (bb >> 16) & 0x3f; f[2] = (bb >> 24) & 0x3f; f[3] = pos & 0x7ff; f[4] = (pos >> 16) & 0x7ff;
    f[5] = reg & 0xfff; f[6] = (reg >> 32) & 0xfff;
    l->rb_rec = (int32_t)i;
}

void rq_rec_readback_data(RqRecList *l, uint32_t qwc)
{
    if (l->rb_rec >= 0) fld(l, rec_at(l, (uint32_t)l->rb_rec))[RB_QWC] += qwc;
}

void rq_rec_clut(RqRecList *l, uint64_t tex0, uint64_t texclut)
{
    before_other(l);
    uint64_t *f = fld(l, rec_at(l, rec_new(l, RQ_R_CLUT, RQ_C_NONE)));
    int csm = (int)((tex0 >> 55) & 1);
    f[0] = (tex0 >> 20) & 0x3f; f[1] = (tex0 >> 37) & 0x3fff; f[2] = (tex0 >> 51) & 15; f[3] = csm;
    f[4] = (tex0 >> 56) & 31; f[5] = (tex0 >> 61) & 7; f[6] = csm ? texclut & 0x3fffff : 0;
}

void rq_rec_display(RqRecList *l, const uint64_t priv[RQ_PRIV_N])
{
    before_other(l);
    uint64_t *f = fld(l, rec_at(l, rec_new(l, RQ_R_DISPLAY, RQ_C_NONE)));
    uint64_t pmode = priv[RQ_PRIV_PMODE] & 0xffe3;
    f[0] = pmode; f[1] = priv[RQ_PRIV_SMODE2] & 3;
    if (pmode & 1) { f[2] = priv[RQ_PRIV_DISPFB1] & 0x003fffff000fffffull; f[3] = priv[RQ_PRIV_DISPLAY1] & 0x007fffff1fffffffull; }
    if (pmode & 2) { f[4] = priv[RQ_PRIV_DISPFB2] & 0x003fffff000fffffull; f[5] = priv[RQ_PRIV_DISPLAY2] & 0x007fffff1fffffffull; }
    f[6] = priv[RQ_PRIV_BGCOLOR] & 0xffffff;
}

// ---------------------------------------------------------------- draw key + primitives

void rq_key_build(RqKey *k, uint64_t attr, const uint64_t c[RQ_CTX_N], const uint64_t g[RQ_GLOB_N])
{
    static const uint8_t cls[8] = {RQ_C_POINT, RQ_C_LINE, RQ_C_LINE, RQ_C_TRI, RQ_C_TRI, RQ_C_TRI, RQ_C_SPRITE, RQ_C_NONE};
    int tme = (attr >> 4) & 1, fge = (attr >> 5) & 1, abe = (attr >> 6) & 1, aa1 = (attr >> 7) & 1, fst = (attr >> 8) & 1;
    uint64_t *f = k->f;
    memset(k, 0, sizeof *k);
    k->cls = cls[attr & 7];
    f[RQ_KEY_ATTR] = attr & 0x5f8;
    f[RQ_KEY_FRAME] = c[RQ_CTX_FRAME] & 0xffffffff3f3f01ffull;
    uint64_t test = c[RQ_CTX_TEST] & 0x7ffff, zbuf = c[RQ_CTX_ZBUF] & 0x000000010f0001ffull;
    int ate = test & 1, date = (test >> 14) & 1, zte = (test >> 16) & 1, ztst = (test >> 17) & 3, zmsk = (zbuf >> 32) & 1;
    if (!ate) test &= ~0x3ffeull;
    if (!date) test &= ~(1ull << 15);
    if (!zte) test &= ~(3ull << 17);
    if (zte && (ztst >= 2 || !zmsk)) {
        int zpsm = (zbuf >> 24) & 15;
        k->zmax = zpsm == 0 ? 0xffffffffu : zpsm == 1 ? 0xffffffu : 0xffffu;
    } else zbuf &= ~0x0f0001ffull;
    f[RQ_KEY_ZBUF] = zbuf;
    f[RQ_KEY_TEST] = test;
    if (abe || aa1) { f[RQ_KEY_ALPHA] = c[RQ_CTX_ALPHA] & 0xff000000ffull; f[RQ_KEY_PABE] = g[RQ_GLOB_PABE] & 1; }
    f[RQ_KEY_FBA] = c[RQ_CTX_FBA] & 1;
    if (tme) {
        uint64_t tex0 = c[RQ_CTX_TEX0] & 0x1fffffffffffffffull;
        if (!paletted((int)((tex0 >> 20) & 0x3f))) tex0 &= ~(0xffffffull << 37);
        f[RQ_KEY_TEX0] = tex0;
        f[RQ_KEY_TEX1] = c[RQ_CTX_TEX1] & 0xfff001803fdull;
        f[RQ_KEY_CLAMP] = c[RQ_CTX_CLAMP] & 0xfffffffffffull;
        f[RQ_KEY_TEXA] = g[RQ_GLOB_TEXA] & 0xff000080ffull;
        if ((f[RQ_KEY_TEX1] >> 2) & 7) {
            f[RQ_KEY_MIPTBP1] = c[RQ_CTX_MIPTBP1] & 0x0fffffffffffffffull;
            f[RQ_KEY_MIPTBP2] = c[RQ_CTX_MIPTBP2] & 0x0fffffffffffffffull;
        }
        k->vflags |= fst ? RQ_VF_UV : RQ_VF_STQ;
    }
    if (fge) { f[RQ_KEY_FOGCOL] = g[RQ_GLOB_FOGCOL] & 0xffffff; k->vflags |= RQ_VF_FOG; }
    f[RQ_KEY_SCISSOR] = c[RQ_CTX_SCISSOR] & 0x07ff07ff07ff07ffull;
    f[RQ_KEY_COLCLAMP] = g[RQ_GLOB_COLCLAMP] & 1;
    f[RQ_KEY_DTHE] = g[RQ_GLOB_DTHE] & 1;
    if (f[RQ_KEY_DTHE]) f[RQ_KEY_DIMX] = g[RQ_GLOB_DIMX] & 0x7777777777777777ull;
}

void rqr_prim_hxy(RqRecList *l, const float (*h)[2], int nv)
{
    uint32_t o = buf_grow(&l->hxy, nv * 8);
    memcpy(l->hxy.p + o, h, nv * 8);
}

void rq_rec_prim(RqRecList *l, const RqKey *k, const RqVert *v, int nv)
{
    before_other(l);
    // coalesce with the last record when it is a DRAW of the same key + class. Tags do not split (record counts and
    // the hash stay equal to the untagged reference): a tag change appends a tag run at the record's vertex offset.
    RqRec *last = l->nrec ? rec_at(l, l->nrec - 1) : 0;
    uint64_t *f;
    int same = last && last->type == RQ_R_DRAW && last->cls == k->cls;
    if (same) {   // inline key compare (a memcmp call per primitive showed in the s78 host profile)
        const uint64_t *lf = fld(l, last);
        for (int i = 0; i < RQ_KEY_N; i++) if (lf[i] != k->f[i]) { same = 0; break; }
    }
    if (same) {
        f = fld(l, last);
        const RqTag *cur = last->nxrun ? &rqr_run(l, last->xrun + last->nxrun - 1)->tag : &last->tag;
        if (memcmp(cur, &l->tag, sizeof(RqTag))) {
            uint32_t ro = rq_buf_alloc(&l->runs, sizeof(RqTagRun));
            RqTagRun *tr = (RqTagRun *)(l->runs.p + ro);
            tr->vfirst = last->nvert; tr->tag = l->tag;
            if (!last->nxrun) last->xrun = ro / sizeof(RqTagRun);
            last->nxrun++;
        }
    } else {
        uint32_t i = rec_new(l, RQ_R_DRAW, (int)k->cls);
        last = rec_at(l, i);
        f = fld(l, last);
        memcpy(f, k->f, sizeof k->f);
        last->vtx = l->verts.n / sizeof(RqVert);
    }
    // canonical vertices written in place (every byte, pad included; was memset + temp + memcpy)
    uint32_t vo = buf_grow(&l->verts, nv * sizeof(RqVert));   // may realloc: read verts.p after it
    RqVert *c = (RqVert *)(l->verts.p + vo);
    uint32_t vf = k->vflags, zmax = k->zmax;
    for (int i = 0; i < nv; i++) {
        RqVert *o = &c[i];
        const RqVert *s = &v[i];
        o->x = s->x; o->y = s->y;
        o->z = zmax ? (s->z < zmax ? s->z : zmax) : 0;
        if (vf & RQ_VF_STQ) { o->s = s->s; o->t = s->t; o->q = s->q; } else { o->s = 0; o->t = 0; o->q = 0; }
        if (vf & RQ_VF_UV) { o->u = s->u & 0x3fff; o->v = s->v & 0x3fff; } else { o->u = 0; o->v = 0; }
        o->r = s->r; o->g = s->g; o->b = s->b; o->a = s->a;
        o->f = (vf & RQ_VF_FOG) ? s->f : 0;
        o->pad[0] = 0; o->pad[1] = 0; o->pad[2] = 0;
    }
    last->nvert += nv;
    f[D_NPRIM]++;
    f[D_NVERT] += nv;
}

// ---------------------------------------------------------------- frame: hash, compare, dump, clear

void rqr_close_frame(RqRecList *l) { before_other(l); }

/* U_HASH of the closed UPLOAD records = FNV-1a 64 over the record's bytes (the same value the incremental hash over
 * the deliveries gave); host-side cost only when something reads the canonical fields */
static void rqr_fill_hashes(const RqRecList *cl)
{
    RqRecList *l = (RqRecList *)cl;
    uint32_t i = l->hash_done;
    for (; i < l->nrec; i++) {
        if (l->up_open && i == l->up_rec) break;   // still receiving bytes
        const RqRec *r = rec_at(l, i);
        if (r->type != RQ_R_UPLOAD) continue;
        uint64_t *f = fld(l, r);
        f[U_HASH] = fnv(0xcbf29ce484222325ull, l->data.p + f[U_DATA], (uint32_t)f[U_NBYTES]);
    }
    l->hash_done = i;
}

uint64_t rqr_hash(const RqRecList *l)
{
    rqr_fill_hashes(l);
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint32_t i = 0; i < l->nrec; i++) {
        const RqRec *r = rec_at(l, i);
        uint8_t tc[2] = {r->type, r->cls};
        h = fnv(h, tc, 2);
        h = fnv(h, fld(l, r), types[r->type].nhash * 8);
        if (r->type == RQ_R_DRAW) h = fnv(h, verts(l, r), r->nvert * sizeof(RqVert));
    }
    return h;
}

static void set_diff(RqFrameResult *res, uint32_t i, const char *what, uint64_t a, uint64_t b)
{
    res->first_rec = i;
    snprintf(res->field, sizeof res->field, "%s", what);
    res->ref_val = a; res->rq_val = b;
}

int rqr_compare(const RqRecList *ref, const RqRecList *mod, RqFrameResult *res)
{
    rqr_fill_hashes(ref); rqr_fill_hashes(mod);
    uint32_t n = ref->nrec < mod->nrec ? ref->nrec : mod->nrec;
    char w[48];
    for (uint32_t i = 0; i < n; i++) {
        const RqRec *a = rec_at(ref, i), *b = rec_at(mod, i);
        if (a->type != b->type) { set_diff(res, i, "type", a->type, b->type); return 1; }
        const RqType *t = &types[a->type];
        if (a->cls != b->cls) { snprintf(w, sizeof w, "%s.cls", t->name); set_diff(res, i, w, a->cls, b->cls); return 1; }
        const uint64_t *fa = fld(ref, a), *fb = fld(mod, b);
        for (int k = 0; k < t->nhash; k++)
            if (fa[k] != fb[k]) { snprintf(w, sizeof w, "%s.%s", t->name, t->f[k]); set_diff(res, i, w, fa[k], fb[k]); return 1; }
        if (a->type == RQ_R_DRAW) {
            const RqVert *va = verts(ref, a), *vb = verts(mod, b);
            for (uint32_t k = 0; k < a->nvert; k++)
                for (int j = 0; j < 13; j++)
                    if (vert_field(&va[k], j) != vert_field(&vb[k], j)) {
                        snprintf(w, sizeof w, "DRAW.v[%u].%s", k, vert_f[j]);
                        set_diff(res, i, w, vert_field(&va[k], j), vert_field(&vb[k], j));
                        return 1;
                    }
        }
    }
    if (ref->nrec != mod->nrec) {
        int more_ref = ref->nrec > mod->nrec;
        const RqRec *r = more_ref ? rec_at(ref, n) : rec_at(mod, n);
        snprintf(w, sizeof w, "%s %s", more_ref ? "missing" : "extra", types[r->type].name);
        set_diff(res, n, w, ref->nrec, mod->nrec);
        return 1;
    }
    return 0;
}

void rqr_dump(const RqRecList *l, const char *path)
{
    rqr_fill_hashes(l);
    FILE *fp = fopen(path, "w");
    if (!fp) { rq_log("rq: cannot write %s", path); return; }
    for (uint32_t i = 0; i < l->nrec; i++) {
        const RqRec *r = rec_at(l, i);
        const RqType *t = &types[r->type];
        fprintf(fp, "#%u %s", i, t->name);
        if (r->type == RQ_R_DRAW) fprintf(fp, " %s", cls_n[r->cls]);
        fprintf(fp, " tag=%u:%llx:%u", r->tag.kind, (unsigned long long)r->tag.objid, r->tag.sub);
        if (r->type == RQ_R_DRAW && r->nxrun) fprintf(fp, " tagruns=%u", r->nxrun + 1);
        const uint64_t *f = fld(l, r);
        for (int k = 0; k < t->nf; k++) fprintf(fp, " %s=%llx", t->f[k], (unsigned long long)f[k]);
        fputc('\n', fp);
        if (r->type == RQ_R_DRAW) {
            const RqVert *v = verts(l, r);
            uint32_t ri = 0;
            for (uint32_t k = 0; k < r->nvert; k++) {
                if (ri < r->nxrun && rqr_run(l, r->xrun + ri)->vfirst == k) {
                    const RqTag *tg = &rqr_run(l, r->xrun + ri++)->tag;
                    fprintf(fp, "  tag=%u:%llx:%u\n", tg->kind, (unsigned long long)tg->objid, tg->sub);
                }
                fprintf(fp, "  x=%d y=%d z=%x s=%x t=%x q=%x u=%x v=%x rgba=%02x%02x%02x%02x f=%x\n", v[k].x, v[k].y, v[k].z,
                        v[k].s, v[k].t, v[k].q, v[k].u, v[k].v, v[k].r, v[k].g, v[k].b, v[k].a, v[k].f);
            }
        }
    }
    fclose(fp);
}

void rqr_clear(RqRecList *l)
{
    l->recs.n = l->fields.n = l->verts.n = l->data.n = l->runs.n = l->hxy.n = 0;
    l->nrec = 0;
    l->hash_done = 0;
    l->rb_rec = -1;
    l->up_open = 0;   // an incomplete upload was closed at the frame end (up_cont continues it)
}

// ---------------------------------------------------------------- module lifecycle + frame API

Rq *rq_create(const RqOptions *o)
{
    Rq *rq = (Rq *)calloc(1, sizeof(Rq));
    rq->opt = *o;
    if (o->ndump > 0 && o->dump_frames) {
        int n = o->ndump < 64 ? o->ndump : 64;
        memcpy(rq->dump_frames, o->dump_frames, n * sizeof(unsigned));
        rq->opt.ndump = n;
    }
    rq->opt.dump_frames = rq->dump_frames;
    if (!rq->opt.dump_dir) rq->opt.dump_dir = "rq";
    if (rq->opt.max_diff_lines <= 0) rq->opt.max_diff_lines = 10;
    rq->vif.cl = rq->vif.wl = 1;
    rq->gs.reg[0x1a] = 1;   // PRMODECONT.AC = 1
    rq->vu.micro_dirty = 1;
    rq->rec = rq_rec_create();
    rq->be_ops = o->backend == RQ_BACKEND_GL ? &rq_backend_gl : &rq_backend_null;
    rq->be = rq->be_ops->be_size ? calloc(1, rq->be_ops->be_size) : 0;
    if (rq->be_ops->init) rq->be_ops->init(rq->be, &rq->opt);
    if (rq->be_ops->draw) rqt_create(rq);
    rq->rec->hxy_on = rq->opt.reproject && rq->tex;   // step 5.1: only a renderer uses the residuals
    if (!rq->rec->hxy_on) rq->opt.reproject = 0;
    rqk_create(rq);   // the consumer needs a renderer; the null backend keeps records only
    return rq;
}

void rq_destroy(Rq *rq)
{
    if (!rq) return;
    rqt_destroy(rq);
    rqk_destroy(rq);
    if (rq->be_ops->shutdown) rq->be_ops->shutdown(rq->be);
    free(rq->be);
    rq_rec_destroy(rq->rec);
    free(rq);
}

static int want_dump(const Rq *rq, uint32_t vb)
{
    if (rq->opt.dump_all) return 1;
    for (int i = 0; i < rq->opt.ndump; i++) if (rq->dump_frames[i] == vb) return 1;
    return 0;
}

void rq_present(Rq *rq, uint32_t vblank, RqRecList *ref, RqFrameResult *out)
{
    uint64_t t0 = rq_clock_ns();
    RqFrameResult res;
    memset(&res, 0, sizeof res);
    res.vblank = vblank;
    rq->st.vblank = vblank;
    uint64_t priv[RQ_PRIV_N];
    for (int i = 0; i < RQ_PRIV_N; i++) memcpy(&priv[i], rq->gs.priv + rq_priv_offset(i), 8);
    rq_rec_display(rq->rec, priv);   // the reference's DISPLAY is appended by the host tee before this call
    rqr_close_frame(rq->rec);
    if (ref) rqr_close_frame(ref);
    rqt_consume(rq);   // sync point: render the frame (DISPLAY is the last record)
    res.records = rq->rec->nrec;
    if (rq->opt.hash) res.hash = rqr_hash(rq->rec);   // FNV over every vertex: only when it is logged
    if (ref) {
        res.compared = 1;
        res.differ = rqr_compare(ref, rq->rec, &res);
        rq->st.compared++;
        if (res.differ) {
            rq->st.differ_frames++;
            if (!rq->st.have_first) { rq->st.have_first = 1; rq->st.first = res; }
            if (rq->st.diff_lines < (uint32_t)rq->opt.max_diff_lines) {
                rq->st.diff_lines++;
                rq_log("rq diff: vblank %u record %u %s ref 0x%llx rq 0x%llx (records ref %u rq %u)", vblank, res.first_rec,
                       res.field, (unsigned long long)res.ref_val, (unsigned long long)res.rq_val, ref->nrec, rq->rec->nrec);
            }
        }
    }
    if (rq->opt.hash) {
        if (ref) rq_log("rq frame %u: %u records hash %016llx (ref %u records hash %016llx)", vblank, res.records,
                        (unsigned long long)res.hash, ref->nrec, (unsigned long long)rqr_hash(ref));
        else rq_log("rq frame %u: %u records hash %016llx", vblank, res.records, (unsigned long long)res.hash);
    }
    if (want_dump(rq, vblank)) {
        char p[512];
        snprintf(p, sizeof p, "%s/%u.txt", rq->opt.dump_dir, vblank);
        rqr_dump(rq->rec, p);
        if (ref) { snprintf(p, sizeof p, "%s/%u_ref.txt", rq->opt.dump_dir, vblank); rqr_dump(ref, p); }
    }
    rqk_frame(rq);
    rq->st.frames++;
    rq->st.records += res.records;
    {   // close the frame's work counters (rq_frame_stats)
        RqStats *s = &rq->st;
        RqFrameStats *fs = &s->last;
        memset(fs, 0, sizeof *fs);
        fs->vblank = vblank; fs->records = res.records; fs->kicks = s->f_kicks; fs->syncs = s->f_syncs;
        fs->readbacks = (uint32_t)(s->rb_kicks - s->f_rb0);
        rqt_frame_stats(rq, fs);
        fs->kick_ms = (float)(s->f_kick_ns * 1e-6); fs->consume_ms = (float)(s->f_consume_ns * 1e-6);
        fs->present_ms = (float)((rq_clock_ns() - t0) * 1e-6);
        s->r_kick_ns += s->f_kick_ns; s->r_consume_ns += s->f_consume_ns; s->r_present_ns += rq_clock_ns() - t0;
        s->r_syncs += s->f_syncs;
        s->f_kick_ns = s->f_consume_ns = 0; s->f_kicks = s->f_syncs = 0; s->f_rb0 = s->rb_kicks;
    }
    rqt_frame_end(rq);
    rqr_clear(rq->rec);
    if (ref) rqr_clear(ref);
    if (out) *out = res;
}

void rq_flush(Rq *rq) { rqt_consume(rq); }
int  rq_output(Rq *rq, int *w, int *h) { return rqt_output(rq, w, h); }
void rq_read_output(Rq *rq, uint32_t *bgra, int w, int h, int stride)
{
    if (!rq->tex) return;
    rq->be_ops->enter(rq->be);
    rq->be_ops->read_output(rq->be, bgra, w, h, stride);
    rq->be_ops->leave(rq->be);
}
void rq_blit_output(Rq *rq, int w, int h, int dx, int dy, int dw, int dh, int cw, int ch, int nearest)
{
    if (!rq->tex) return;
    rq->be_ops->enter(rq->be);
    rq->be_ops->blit_output(rq->be, w, h, dx, dy, dw, dh, cw, ch, nearest);
    rq->be_ops->leave(rq->be);
}

void rq_frame_stats(const Rq *rq, RqFrameStats *out) { *out = rq->st.last; }

int rq_stats(Rq *rq, char *buf, int n)
{
    const RqFrameStats *f = &rq->st.last;
    return snprintf(buf, n, "rq work vblank %u: %u records %u kicks | %u draws %u prims %u flushes | up %u (%u KB) decode %u "
                    "partial %u | surf up %u down %u | gpu-copy %u rt-tex %u readback %u | syncs %u | kick %.2f ms "
                    "consume %.2f ms present %.2f ms", f->vblank, f->records, f->kicks, f->draws, f->prims, f->flushes,
                    f->uploads, f->upload_kb, f->decodes, f->tex_partial, f->surf_up, f->surf_down, f->gpu_copies,
                    f->rt_tex, f->readbacks, f->syncs, f->kick_ms, f->consume_ms, f->present_ms);
}

int rq_summary(Rq *rq, char *buf, int n)
{
    int k;
    const RqStats *s = &rq->st;
    if (!s->compared)
        k = snprintf(buf, n, "rq: %llu frames, %llu records (no compare)", (unsigned long long)s->frames, (unsigned long long)s->records);
    else if (!s->differ_frames)
        k = snprintf(buf, n, "rq PASS: %llu frames, %llu records, 0 differ", (unsigned long long)s->compared, (unsigned long long)s->records);
    else
        k = snprintf(buf, n, "rq FAIL: first vblank %u record %u %s ref 0x%llx rq 0x%llx (%llu of %llu frames differ)",
                     s->first.vblank, s->first.first_rec, s->first.field, (unsigned long long)s->first.ref_val,
                     (unsigned long long)s->first.rq_val, (unsigned long long)s->differ_frames, (unsigned long long)s->compared);
    if (k < 0 || k >= n) return k;
    char rb[160];
    if (s->rb_cmp)
        snprintf(rb, sizeof rb, "readbacks %llu (%llu bytes, %llu compared), byte mismatches %llu (%llu deliveries)",
                 (unsigned long long)s->rb_kicks, (unsigned long long)s->rb_bytes, (unsigned long long)s->rb_cmp,
                 (unsigned long long)s->rb_bad_bytes, (unsigned long long)s->rb_bad_kicks);
    else
        snprintf(rb, sizeof rb, "readbacks %llu (%llu bytes), byte mismatches n/a", (unsigned long long)s->rb_kicks,
                 (unsigned long long)s->rb_bytes);
    k += snprintf(buf + k, n - k, "\nrq partial uploads %u, %s, vu1 calls %u missing %u\n", rq->rec->partials, rb,
                  rq->vu.calls, rq->vu.missing);
    if (k < 0 || k >= n) return k;
    if (rq->opt.reproject || rq->opt.layout2d) {   // step 5.1 resolution
        k += snprintf(buf + k, n - k, "rq resolution: internal x%d, re-projected triangles %llu (kinds 0x%x), 2D layout %d\n",
                      rq->opt.scale, (unsigned long long)s->reproj_tris, rq->opt.reproject, rq->opt.layout2d);
        if (k < 0 || k >= n) return k;
        if (rq->rec->hxy_on) {   // per tag kind: triangles / with a VU1 sideband residual at every vertex
            k += snprintf(buf + k, n - k, "rq reproject coverage (kind tris/sideband):");
            for (int i = 0; i < RQ_K_N && k >= 0 && k < n; i++)
                if (s->rp_tris[i]) k += snprintf(buf + k, n - k, " %d %llu/%llu", i, (unsigned long long)s->rp_tris[i],
                                                 (unsigned long long)s->rp_hit[i]);
            if (k >= 0 && k < n) k += snprintf(buf + k, n - k, "\n");
            if (k < 0 || k >= n) return k;
        }
    }
    k += snprintf(buf + k, n - k, "rq host time: kick %.0f ms (consume %.0f ms) present %.0f ms, %llu syncs\n",
                  s->r_kick_ns * 1e-6, s->r_consume_ns * 1e-6, s->r_present_ns * 1e-6, (unsigned long long)s->r_syncs);
    if (k < 0 || k >= n) return k;
    k += rqt_summary(rq, buf + k, n - k);
    if (k >= 0 && k < n && rq->be_ops->summary) k += rq->be_ops->summary(rq->be, buf + k, n - k);
    if (k < 0 || k >= n) return k;
    return k + rqk_summary(rq, buf + k, n - k);
}
