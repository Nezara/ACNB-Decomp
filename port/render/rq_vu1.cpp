// VU1 model: 16 KB data + 16 KB micro memory and registers (in struct Rq), program lookup by micro-memory hash,
// dispatch into the shared HLE table (rq_vu1_progs.cpp). No interpreter: an unmatched MSCAL / MSCNT is skipped,
// counted and reported as RQ_EV_VU1_MISSING (design 4.3 / 14.5). Forked from vif.c start_micro + vu1_hle.c lookup.
#include "rq_internal.h"

uint32_t rq_vu1_hash(const uint8_t *m, uint32_t lo, uint32_t hi)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = lo * 8; i < hi * 8; i++) h = (h ^ m[i]) * 16777619u;
    return h;
}

int rq_vu1_lookup(const uint32_t *prog_hash, uint32_t addr, int *entry, int with_check_only)
{
    for (int p = 0; p < rq_vu1_nprogs; p++) {
        if (prog_hash[p] != rq_vu1_progs[p].hash || (rq_vu1_progs[p].check_only && !with_check_only)) continue;
        for (int e = 0; e < 2; e++)
            if (rq_vu1_progs[p].at[e] * 8 == addr) { *entry = e; return p; }
    }
    return -1;
}

void rqv_micro_written(Rq *rq) { rq->vu.micro_dirty = 1; }

static void kick(RqVu1Ctx *c, uint32_t qw, int stream)
{
    (void)stream;
    rqp_xgkick((Rq *)c->kick_user, qw);
}

void rqv_exec(Rq *rq, uint32_t addr)
{
    RqVu *vu = &rq->vu;
    if (vu->micro_dirty) {
        for (int p = 0; p < rq_vu1_nprogs; p++) vu->prog_hash[p] = rq_vu1_hash(vu->micro, rq_vu1_progs[p].lo, rq_vu1_progs[p].hi);
        vu->micro_dirty = 0;
    }
    int e, p = rq_vu1_lookup(vu->prog_hash, addr, &e, 0);
    if (p < 0) {
        vu->missing++;
        rq->ev |= RQ_EV_VU1_MISSING;
        if (vu->missing <= 5) rq_log("rq: vu1 entry %04x has no HLE program (skipped, %u so far)", addr / 8, vu->missing);
        return;
    }
    vu->calls++;
    RqVu1Ctx *c = &vu->ctx;
    c->mem = vu->data; c->r = &vu->regs; c->top = vu->top; c->itop = vu->itop;
    c->kick = kick; c->kick_user = rq;
    c->hxy = rq->opt.reproject ? vu->hxy : 0;   /* step 5.1: entries of this call only (gen = vu->calls) */
    c->hgen = vu->calls;
    rq_vu1_progs[p].entry[e](c);
}
