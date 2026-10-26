// Packet frontend: DMA walker ch 1 / 2 (normal, source chain, readback direction), VIF1 parser (UNPACK, MPG,
// MSCAL / MSCNT, DIRECT), GIF paths 1 / 2 / 3 (PACKED / REGLIST / IMAGE), XGKICK.
// Forked from runtime/dma.c (sink, run_chain), vif.c (whole), gs.c (gif_transfer, packed_write, gif_xgkick).
// The module always takes the GIF path for XGKICK: its records equal the reference's strip fast path (design 14.3).
#include "rq_internal.h"

// ---------------------------------------------------------------- DMA

// DMA-side address -> host pointer (bit 31 or 0x7xxxxxxx: scratchpad)
static uint8_t *dptr(uint32_t a, uint32_t len) { return rq_mem(rq_addr_fold(a), len); }

static void sink(Rq *rq, int ch, uint32_t addr, uint32_t qwc)
{
    while (qwc) {
        uint32_t run = qwc;
        if ((addr & 0x80000000u) && rq_addr_is_spr(addr)) { uint32_t left = (0x4000 - (addr & 0x3ff0)) / 16; if (run > left) run = left; }
        uint8_t *p = dptr(addr, run * 16);
        if (!p) { rq_log("rq: dma%d bad address %08x (%u qw)", ch, addr, run); return; }
        if (ch == 1) rqp_vif_write(rq, (const uint32_t *)p, run * 4);
        else rqp_gif_transfer(rq, 3, p, run);
        addr += run * 16;
        qwc -= run;
    }
}

static void run_chain(Rq *rq, int ch, uint32_t chcr, uint32_t madr, uint32_t qwc, uint32_t tadr)
{
    int tte = (chcr >> 6) & 1, tie = (chcr >> 7) & 1;
    if (qwc) {
        rqk_visit(rq, madr);
        sink(rq, ch, madr, qwc);
        madr += qwc * 16;
        uint32_t id = (chcr >> 28) & 7;
        if (id == 0 || id == 7) return;
    }
    for (int guard = 0; guard < 1000000; guard++) {
        rqk_visit(rq, tadr);   // tags: the tag qword's address
        uint8_t *tp = dptr(tadr, 16);
        if (!tp) { rq_log("rq: dma%d bad tag address %08x", ch, tadr); break; }
        uint32_t t[4];
        memcpy(t, tp, 16);
        chcr = (chcr & 0xffff) | (t[0] & 0xffff0000u);
        qwc = t[0] & 0xffff;
        uint32_t id = (t[0] >> 28) & 7, addr = t[1];
        int irq = (t[0] >> 31) & 1;
        if (tte && ch == 1) rqp_vif_write(rq, &t[2], 2);
        int end = 0;
        if (id == 0 || id == 3 || id == 4) rqk_visit(rq, addr);   // REF target
        switch (id) {
        case 0: madr = addr; sink(rq, ch, madr, qwc); madr += qwc * 16; tadr += 16; end = 1; break;   // refe
        case 1: madr = tadr + 16; sink(rq, ch, madr, qwc); madr += qwc * 16; tadr = madr; break;    // cnt
        case 2: madr = tadr + 16; sink(rq, ch, madr, qwc); madr += qwc * 16; tadr = addr; break;    // next
        case 3: case 4: madr = addr; sink(rq, ch, madr, qwc); madr += qwc * 16; tadr += 16; break;  // ref / refs
        case 5: {                                                                                   // call
            madr = tadr + 16; sink(rq, ch, madr, qwc); madr += qwc * 16;
            int asp = (chcr >> 4) & 3;
            if (asp >= 2) { rq_log("rq: dma%d call stack overflow", ch); end = 1; break; }
            rq->asr[ch][asp] = madr;
            chcr = (chcr & ~0x30u) | ((asp + 1) << 4);
            tadr = addr;
            break;
        }
        case 6: {                                                                                   // ret
            madr = tadr + 16; sink(rq, ch, madr, qwc); madr += qwc * 16;
            int asp = (chcr >> 4) & 3;
            if (asp == 0) { end = 1; break; }
            asp--;
            tadr = rq->asr[ch][asp];
            chcr = (chcr & ~0x30u) | (asp << 4);
            break;
        }
        default: madr = tadr + 16; sink(rq, ch, madr, qwc); madr += qwc * 16; end = 1; break;          // end
        }
        if (end || (irq && tie)) break;
    }
}

static uint32_t dma_kick(Rq *rq, int ch, uint32_t chcr, uint32_t madr, uint32_t qwc, uint32_t tadr)
{
    rq->ev = 0;
    if (ch != 1 && ch != 2) return 0;
    qwc &= 0xffff;
    if (ch == 1 && !(chcr & 1)) {   // VIF1 -> memory: GS local->host delivery
        rq_rec_readback_data(rq->rec, qwc);
        rq->st.rb_kicks++; rq->st.rb_bytes += (uint64_t)qwc * 16;
        rqt_readback_deliver(rq, madr, qwc);   // rule 11: bytes from the VRAM model; in both mode the reference delivers
        return rq->ev;
    }
    if (((chcr >> 2) & 3) == 1) run_chain(rq, ch, chcr, madr, qwc, tadr);
    else { rqk_visit(rq, madr); sink(rq, ch, madr, qwc); }
    rqk_kick_end(rq);
    return rq->ev;
}

// host timing for rq_frame_stats (one clock read pair per kick / FIFO qword; not state)
uint32_t rq_dma_kick(Rq *rq, int ch, uint32_t chcr, uint32_t madr, uint32_t qwc, uint32_t tadr)
{
    uint64_t t0 = rq_clock_ns();
    uint32_t ev = dma_kick(rq, ch, chcr, madr, qwc, tadr);
    rqt_kick_done(rq);
    rq->st.f_kicks++;
    rq->st.f_kick_ns += rq_clock_ns() - t0;
    return ev;
}

uint32_t rq_fifo_write(Rq *rq, int path, const uint32_t qw[4])
{
    uint64_t t0 = rq_clock_ns();
    rq->ev = 0;
    if (path == 1) rqp_vif_write(rq, qw, 4);
    else if (path == 3) rqp_gif_transfer(rq, 3, (const uint8_t *)qw, 1);
    rq->st.f_kick_ns += rq_clock_ns() - t0;
    return rq->ev;
}

// VIF1 FIFO read (lq 0x10005000, FDR set): one qword of the local->host transfer, counted as a delivery like a DIR = 0
// kick; the reference packs the same qword (gs_fifo_read) unless the module delivers
void rq_fifo_read(Rq *rq, void *out16)
{
    uint64_t t0 = rq_clock_ns();
    rq_rec_readback_data(rq->rec, 1);
    rq->st.rb_kicks++; rq->st.rb_bytes += 16;
    rqt_fifo_read(rq, (uint8_t *)out16);
    rq->st.f_kick_ns += rq_clock_ns() - t0;
}

int rq_fifo_pending(Rq *rq) { return rqt_fifo_pending(rq); }

// ---------------------------------------------------------------- VIF1

static uint32_t unpack_words(const RqVif *vf, uint32_t code)
{
    uint32_t cmd = (code >> 24) & 0x7f;
    uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3;
    uint32_t num = (code >> 16) & 0xff; if (!num) num = 256;
    uint32_t cl = vf->cl, wl = vf->wl;
    uint32_t ndata = num;
    if (wl > cl && wl) ndata = (num / wl) * cl + ((num % wl) < cl ? (num % wl) : cl);
    uint32_t bytes = (vn == 3 && vl == 3) ? 2 : (vn + 1) * (32 >> vl) / 8;
    return (ndata * bytes + 3) / 4;
}

static void do_unpack(Rq *rq, uint32_t code, const uint32_t *data, uint32_t nwords)
{
    RqVif *vf = &rq->vif;
    uint8_t *vu = rq->vu.data;
    uint32_t cmd = (code >> 24) & 0x7f;
    uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3, msk = (cmd >> 4) & 1;
    uint32_t num = (code >> 16) & 0xff; if (!num) num = 256;
    uint32_t addr = code & 0x3ff, usn = (code >> 14) & 1;
    if ((code >> 15) & 1) addr += vf->tops;
    uint32_t cl = vf->cl, wl = vf->wl;
    if (!wl) wl = 1;
    if (vn == 3 && vl == 0 && !msk && vf->mode == 0 && cl == wl && nwords >= num * 4) {
        const uint8_t *src = (const uint8_t *)data;
        for (uint32_t k = 0; k < num;) {
            uint32_t qa = (addr + k) & 0x3ff, run = num - k;
            if (run > 0x400 - qa) run = 0x400 - qa;
            memcpy(vu + qa * 16, src + k * 16, run * 16);
            k += run;
        }
        return;
    }
    uint32_t bytes = (vn == 3 && vl == 3) ? 2 : (vn + 1) * (32 >> vl) / 8;
    const uint8_t *src = (const uint8_t *)data;
    uint32_t avail = nwords * 4, pos = 0;
    for (uint32_t k = 0; k < num; k++) {
        uint32_t cpos = k % wl;
        uint32_t qa = (wl <= cl) ? addr + (k / wl) * cl + cpos : addr + k;
        int fill = wl > cl && cpos >= cl;
        uint32_t v[4] = {0, 0, 0, 0};
        if (!fill) {
            uint32_t comp[4] = {0, 0, 0, 0};
            if (vn == 3 && vl == 3) {
                uint16_t c = pos + 2 <= avail ? (uint16_t)(src[pos] | (src[pos + 1] << 8)) : 0;
                comp[0] = (c & 31) << 3; comp[1] = ((c >> 5) & 31) << 3; comp[2] = ((c >> 10) & 31) << 3; comp[3] = (c >> 15) << 7;
            } else {
                uint32_t sz = 4 >> vl;
                for (uint32_t c = 0; c < 4; c++) {
                    uint32_t p = pos + c * sz;
                    if (c > vn && !(vn == 2 && c == 3)) break;
                    if (p + sz > avail) { comp[c] = 0; continue; }
                    uint32_t x;
                    switch (sz) {
                    case 4: memcpy(&x, src + p, 4); break;
                    case 2: x = src[p] | (src[p + 1] << 8); if (!usn) x = (uint32_t)(int32_t)(int16_t)x; break;
                    default: x = src[p]; if (!usn) x = (uint32_t)(int32_t)(int8_t)x; break;
                    }
                    comp[c] = x;
                }
            }
            switch (vn) {
            case 0: v[0] = v[1] = v[2] = v[3] = comp[0]; break;
            case 1: v[0] = comp[0]; v[1] = comp[1]; v[2] = comp[0]; v[3] = comp[1]; break;
            default: memcpy(v, comp, 16); break;
            }
            pos += bytes;
        }
        uint32_t *q = (uint32_t *)(vu + (qa & 0x3ff) * 16);
        uint32_t mrow = cpos < 3 ? cpos : 3;
        for (int f = 0; f < 4; f++) {
            uint32_t m = msk ? (vf->mask >> (mrow * 8 + f * 2)) & 3 : 0;
            if (fill && !msk) m = 3;
            switch (m) {
            case 0:
                if (fill) { q[f] = vf->row[f]; break; }
                if (vf->mode == 1) q[f] = v[f] + vf->row[f];
                else if (vf->mode == 2) { vf->row[f] += v[f]; q[f] = vf->row[f]; }
                else q[f] = v[f];
                break;
            case 1: q[f] = vf->row[f]; break;
            case 2: q[f] = vf->col[mrow]; break;
            default: break;
            }
        }
    }
}

static void start_micro(Rq *rq, uint32_t addr)
{
    RqVif *vf = &rq->vif;
    rq->vu.top = vf->tops & 0x3ff;
    rq->vu.itop = vf->itops & 0x3ff;
    if (vf->dbf) { vf->dbf = 0; vf->tops = vf->base; }
    else { vf->dbf = 1; vf->tops = vf->base + vf->ofst; }
    rqv_exec(rq, addr);
}

static void finish_cmd(Rq *rq)
{
    RqVif *vf = &rq->vif;
    uint32_t code = vf->code, cmd = (code >> 24) & 0x7f;
    if (cmd >= 0x60) do_unpack(rq, code, vf->buf, vf->have);
    else switch (cmd) {
    case 0x20: vf->mask = vf->buf[0]; break;
    case 0x30: memcpy(vf->row, vf->buf, 16); break;
    case 0x31: memcpy(vf->col, vf->buf, 16); break;
    case 0x4a: {
        uint32_t addr = (code & 0xffff) * 8;
        for (uint32_t i = 0; i < vf->have; i++) memcpy(rq->vu.micro + ((addr + i * 4) & 0x3fff), &vf->buf[i], 4);
        rqv_micro_written(rq);
        break;
    }
    }
    if (code & 0x80000000u) rq->ev |= RQ_EV_VIF1_IRQ;
}

static void command(Rq *rq, uint32_t code)
{
    RqVif *vf = &rq->vif;
    uint32_t cmd = (code >> 24) & 0x7f, imm = code & 0xffff;
    vf->code = code;
    vf->have = 0;
    vf->need = 0;
    switch (cmd) {
    case 0x00: case 0x10: case 0x11: case 0x13: case 0x06: break;     // NOP FLUSHE FLUSH FLUSHA MSKPATH3
    case 0x01: vf->cl = imm & 0xff; vf->wl = (imm >> 8) & 0xff; break;
    case 0x02: vf->ofst = imm & 0x3ff; vf->dbf = 0; vf->tops = vf->base; break;
    case 0x03: vf->base = imm & 0x3ff; break;
    case 0x04: vf->itops = imm & 0x3ff; break;
    case 0x05: vf->mode = imm & 3; break;
    case 0x07: vf->mark = imm; break;
    case 0x14: case 0x15: start_micro(rq, imm * 8); break;
    case 0x17: start_micro(rq, rq->vu.regs.tpc); break;
    case 0x20: vf->need = 1; return;
    case 0x30: case 0x31: vf->need = 4; return;
    case 0x4a: { uint32_t n = (code >> 16) & 0xff; vf->need = (n ? n : 256) * 2; return; }
    case 0x50: case 0x51: vf->direct = imm ? imm : 65536; vf->direct_n = 0; return;
    default:
        if (cmd >= 0x60) { vf->need = unpack_words(vf, code); if (vf->need) return; do_unpack(rq, code, 0, 0); break; }
        break;
    }
    if (code & 0x80000000u) rq->ev |= RQ_EV_VIF1_IRQ;
}

void rqp_vif_write(Rq *rq, const uint32_t *w, uint32_t n)
{
    RqVif *vf = &rq->vif;
    for (uint32_t i = 0; i < n; i++) {
        if (vf->direct) {
            vf->direct_qw[vf->direct_n++] = w[i];
            if (vf->direct_n == 4) {
                rqp_gif_transfer(rq, 2, (const uint8_t *)vf->direct_qw, 1);
                vf->direct_n = 0;
                if (--vf->direct == 0 && (vf->code & 0x80000000u)) rq->ev |= RQ_EV_VIF1_IRQ;
            }
            continue;
        }
        if (vf->need) {
            if (!vf->have && vf->need <= n - i && ((vf->code >> 24) & 0x7f) >= 0x60) {
                uint32_t k = vf->need;
                vf->need = 0;
                do_unpack(rq, vf->code, w + i, k);
                if (vf->code & 0x80000000u) rq->ev |= RQ_EV_VIF1_IRQ;
                i += k - 1;
                continue;
            }
            vf->buf[vf->have++] = w[i];
            if (--vf->need == 0) finish_cmd(rq);
            continue;
        }
        command(rq, w[i]);
    }
}

// ---------------------------------------------------------------- GIF

/* step 5.1: an XYZF2 / XYZ2 qword read from VU1 data memory whose words a program of the current call stored with a
 * float position (RqVu1Hxy): the residual (float - stored integer) goes with the vertex; anything else gets none */
static void hxy_take(Rq *rq, const uint8_t *q, const uint32_t *w)
{
    RqGs *g = &rq->gs;
    g->hok = 0;
    if (q < rq->vu.data || q >= rq->vu.data + sizeof rq->vu.data) return;
    const RqVu1Hxy *h = &rq->vu.hxy[(uint32_t)(q - rq->vu.data) >> 4];
    if (h->gen != rq->vu.calls || h->ix != w[0] || h->iy != w[1] || w[0] > 0xffff || w[1] > 0xffff) return;
    g->hx = (h->x - (float)w[0]) * (1.0f / 16); g->hy = (h->y - (float)w[1]) * (1.0f / 16);
    g->hok = 1;
}

static void packed_write(Rq *rq, int r, const uint8_t *q)
{
    uint32_t w[4];
    memcpy(w, q, 16);
    uint64_t lo = (uint64_t)w[0] | ((uint64_t)w[1] << 32);
    switch (r) {
    case 0x0: rqg_write_reg(rq, 0x00, lo & 0x7ff); break;
    case 0x1: {
        uint64_t v = (w[0] & 0xff) | ((w[1] & 0xff) << 8) | ((w[2] & 0xff) << 16) | ((uint64_t)(w[3] & 0xff) << 24);
        uint32_t qb; memcpy(&qb, &rq->gs.q, 4);
        rqg_write_reg(rq, 0x01, v | ((uint64_t)qb << 32));
        break;
    }
    case 0x2: rqg_write_reg(rq, 0x02, lo); memcpy(&rq->gs.q, &w[2], 4); break;
    case 0x3: rqg_write_reg(rq, 0x03, (w[0] & 0x3fff) | ((uint64_t)(w[1] & 0x3fff) << 16)); break;
    case 0x4: {
        if (rq->opt.reproject) hxy_take(rq, q, w);
        uint64_t v = (w[0] & 0xffff) | ((uint64_t)(w[1] & 0xffff) << 16) | ((uint64_t)((w[2] >> 4) & 0xffffff) << 32) |
                     ((uint64_t)((w[3] >> 4) & 0xff) << 56);
        rqg_write_reg(rq, (w[3] & 0x8000) ? 0x0c : 0x04, v);
        break;
    }
    case 0x5: {
        if (rq->opt.reproject) hxy_take(rq, q, w);
        uint64_t v = (w[0] & 0xffff) | ((uint64_t)(w[1] & 0xffff) << 16) | ((uint64_t)w[2] << 32);
        rqg_write_reg(rq, (w[3] & 0x8000) ? 0x0d : 0x05, v);
        break;
    }
    case 0xa: rqg_write_reg(rq, 0x0a, (uint64_t)((w[3] >> 4) & 0xff) << 56); break;
    case 0xe: rqg_write_reg(rq, q[8] & 0x7f, lo); break;
    case 0xf: break;
    default: rqg_write_reg(rq, r, lo); break;
    }
}

uint32_t rqp_gif_transfer(Rq *rq, int path, const uint8_t *d, uint32_t qwc)
{
    RqGifPath *p = &rq->gif[path];
    uint32_t i = 0;
    while (i < qwc) {
        if (!p->active) {
            uint64_t t0, t1;
            memcpy(&t0, d + i * 16, 8); memcpy(&t1, d + i * 16 + 8, 8);
            i++;
            p->tag0 = t0; p->regs = t1;
            p->nloop = t0 & 0x7fff;
            p->flg = (t0 >> 58) & 3;
            p->nreg = (t0 >> 60) & 15; if (!p->nreg) p->nreg = 16;
            p->reg_i = 0;
            rq->gs.q = 1.0f;
            if (((t0 >> 46) & 1) && p->flg == 0) rqg_write_reg(rq, 0x00, (t0 >> 47) & 0x7ff);
            p->active = p->nloop != 0;
            if (!p->active && ((t0 >> 15) & 1) && path == 1) return i;
            continue;
        }
        switch (p->flg) {
        case 0:
            packed_write(rq, (p->regs >> (p->reg_i * 4)) & 15, d + i * 16);
            i++;
            if (++p->reg_i >= p->nreg) { p->reg_i = 0; p->nloop--; }
            break;
        case 1:
            for (int h = 0; h < 2 && p->nloop; h++) {
                uint64_t v; memcpy(&v, d + i * 16 + h * 8, 8);
                int r = (p->regs >> (p->reg_i * 4)) & 15;
                if (r != 0xe && r != 0xf) rqg_write_reg(rq, r, v);
                if (++p->reg_i >= p->nreg) { p->reg_i = 0; p->nloop--; }
            }
            i++;
            break;
        default: {
            uint32_t n = qwc - i < p->nloop ? qwc - i : p->nloop;
            rqg_tr_write(rq, d + i * 16, n * 16);
            i += n; p->nloop -= n;
            break;
        }
        }
        if (p->nloop == 0) {
            p->active = 0;
            if (((p->tag0 >> 15) & 1) && path == 1) return i;
        }
    }
    return i;
}

void rqp_xgkick(Rq *rq, uint32_t addr)
{
    rq->gif[1].active = 0;
    for (int guard = 0; guard < 1024; guard++) {
        uint32_t a = addr & 0x3ff;
        uint32_t n = rqp_gif_transfer(rq, 1, rq->vu.data + a * 16, 1);
        addr += n;
        if (!rq->gif[1].active && (rq->gif[1].tag0 >> 15) & 1) return;
    }
}
