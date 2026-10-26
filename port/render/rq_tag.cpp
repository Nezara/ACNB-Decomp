// Tags (design sections 7 and 17): packet side table keyed by the DMA-side address (two generations: the frame a tag
// is attached in and the next), the scope stack, the walker's current packet tag, per-kind coverage counts.
// Tags are never hashed or compared and never split a record (rq_rec_prim keeps tag runs inside a DRAW instead).
#include "rq_internal.h"
#include <stdio.h>

static const char *const kind_n[RQ_K_N] = {"NONE", "MODEL", "SHADOW", "EFFECT", "SPRITE2D", "HUD", "TEXT", "MENU",
                                            "POST", "ENV", "UPLOAD", "READBACK"};

// same address folding as the walker's dptr: scratchpad by bit 31 or 0x7xxxxxxx, RAM by the 32 MB physical window
static uint32_t norm(uint32_t a) { return rq_addr_fold(a); }

static uint32_t slot(uint32_t a) { return ((a >> 4) * 2654435761u) >> (32 - RQ_TAGTAB_BITS); }

static const RqTagEnt *find(const RqTagTab *t, uint32_t a)
{
    if (!t->n) return 0;
    for (uint32_t i = slot(a), k = 0; k < RQ_TAGTAB_N; k++, i = (i + 1) & (RQ_TAGTAB_N - 1)) {
        const RqTagEnt *e = &t->e[i];
        if (!e->used) return 0;
        if (e->addr == a) return e;
    }
    return 0;
}

// the tag new records get: packet tag, else top of the scope stack, else NONE
static void sync(Rq *rq)
{
    static const RqTag none = {0, 0, 0, 0};
    const RqTag *t = &none;
    if (rq->ptag.kind) t = &rq->ptag;
    else if (rq->nscope > 0) t = &rq->scope[(rq->nscope < 8 ? rq->nscope : 8) - 1];
    rq->rec->tag = *t;
}

void rqk_create(Rq *rq)
{
    for (int i = 0; i < 2; i++) rq->tagtab[i].e = (RqTagEnt *)calloc(RQ_TAGTAB_N, sizeof(RqTagEnt));
}

void rqk_destroy(Rq *rq)
{
    for (int i = 0; i < 2; i++) { free(rq->tagtab[i].e); rq->tagtab[i].e = 0; }
}

void rq_tag_packet(Rq *rq, uint32_t ps2addr, const RqTag *tag)
{
    RqTagTab *t = &rq->tagtab[rq->tagcur];
    uint32_t a = norm(ps2addr);
    rq->st.tag_attach++;
    for (uint32_t i = slot(a), k = 0; k < RQ_TAGTAB_N; k++, i = (i + 1) & (RQ_TAGTAB_N - 1)) {
        RqTagEnt *e = &t->e[i];
        if (e->used && e->addr != a) continue;
        if (!e->used) {
            if (t->n >= RQ_TAGTAB_N * 3 / 4) { rq->st.tag_overflow++; return; }
            e->used = 1; e->addr = a; t->n++;
        }
        e->tag = *tag;   // re-attach overwrites
        return;
    }
}

void rqk_visit(Rq *rq, uint32_t addr)
{
    uint32_t a = norm(addr);
    const RqTagEnt *e = find(&rq->tagtab[rq->tagcur], a);
    if (!e) e = find(&rq->tagtab[rq->tagcur ^ 1], a);
    if (!e) return;
    rq->st.tag_hits++;
    rq->ptag = e->tag;   // kind NONE: end marker, back to the scope stack
    sync(rq);
}

void rqk_kick_end(Rq *rq)
{
    if (!rq->ptag.kind) return;
    memset(&rq->ptag, 0, sizeof rq->ptag);
    sync(rq);
}

void rq_tag_scope(Rq *rq, const RqTag *t)
{
    if (rq->nscope < 8) rq->scope[rq->nscope] = *t;
    rq->nscope++;
    rq->st.tag_scopes++;
    sync(rq);
}

void rq_tag_end(Rq *rq)
{
    if (rq->nscope > 0) rq->nscope--;
    sync(rq);
}

void rq_event(Rq *rq, const RqEvent *e) { (void)e; rq->st.events++; }

void rqk_frame(Rq *rq)
{
    const RqRecList *l = rq->rec;
    RqStats *s = &rq->st;
    for (uint32_t i = 0; i < l->nrec; i++) {
        const RqRec *r = rqr_at(l, i);
        if (r->type != RQ_R_DRAW) { s->tag_segs[r->tag.kind < RQ_K_N ? r->tag.kind : 0]++; continue; }
        uint32_t v0 = 0, kind = r->tag.kind;
        for (uint32_t k = 0; k <= r->nxrun; k++) {
            uint32_t v1 = k < r->nxrun ? rqr_run(l, r->xrun + k)->vfirst : r->nvert;
            if (kind >= RQ_K_N) kind = 0;
            s->tag_segs[kind]++;
            s->tag_verts[kind] += v1 - v0;
            if (k < r->nxrun) { v0 = v1; kind = rqr_run(l, r->xrun + k)->tag.kind; }
        }
        s->tag_runs += r->nxrun;
    }
    // a scope may legally stay open across a vblank (the hybrid's vblank can fire inside interpreted code called from
    // a scoped native); counted only. A growing depth at exit means a missing rq_tag_end.
    if (rq->nscope) s->tag_unbalanced++;
    memset(&rq->ptag, 0, sizeof rq->ptag);
    // next generation: this frame's table becomes the previous one, the one before is cleared
    rq->tagcur ^= 1;
    RqTagTab *t = &rq->tagtab[rq->tagcur];
    if (t->n) { memset(t->e, 0, RQ_TAGTAB_N * sizeof(RqTagEnt)); t->n = 0; }
    sync(rq);
}

int rqk_summary(const Rq *rq, char *buf, int n)
{
    const RqStats *s = &rq->st;
    int k = snprintf(buf, n, "rq tags (segments/vertices):");
    for (int i = 1; i <= RQ_K_N && k >= 0 && k < n; i++) {
        int j = i % RQ_K_N;   // NONE last
        k += snprintf(buf + k, n - k, " %s %llu/%llu", kind_n[j], (unsigned long long)s->tag_segs[j],
                      (unsigned long long)s->tag_verts[j]);
    }
    if (k < 0 || k >= n) return k;
    k += snprintf(buf + k, n - k, "\nrq tag calls: packet %llu (hits %llu, overflow %llu), scopes %llu (open at vblank %llu, depth now %d), "
                  "draw tag runs %llu, events %llu", (unsigned long long)s->tag_attach, (unsigned long long)s->tag_hits,
                  (unsigned long long)s->tag_overflow, (unsigned long long)s->tag_scopes,
                  (unsigned long long)s->tag_unbalanced, rq->nscope, (unsigned long long)s->tag_runs, (unsigned long long)s->events);
    return k;
}
