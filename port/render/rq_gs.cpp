// GS frontend: register file (2 contexts, PRMODECONT), vertex queue -> primitives, TEX0 / TEX2 CLD decision,
// transfer state, privileged shadow. Forked from runtime/gs.c (gs_write_reg, vertex_kick, tex0_write, tr_*);
// emits records instead of drawing. No VRAM here: the consumer (rq_tex, step 1.2) owns it.
#include "rq_internal.h"

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

// effective PRIM attributes: PRIM when PRMODECONT.AC = 1, else PRMODE (type always from PRIM)
static uint64_t eff_attr(const RqGs *g) { return (g->reg[0x1a] & 1) ? g->prim : ((g->prmode & ~7ull) | (g->prim & 7)); }

static void emit_prim(Rq *rq, int n)
{
    RqGs *g = &rq->gs;
    uint64_t attr = eff_attr(g);
    uint64_t glob[RQ_GLOB_N] = {g->reg[0x3b], g->reg[0x3d], g->reg[0x49], g->reg[0x46], g->reg[0x45], g->reg[0x44]};
    // the key only changes with a register write other than vertex data (rqg_write_reg clears key_ok)
    if (!g->key_ok) { rq_key_build(&g->key, attr, g->ctx[(attr >> 9) & 1], glob); g->key_ok = 1; }
    rq_rec_prim(rq->rec, &g->key, g->vq, n);
    // step 5.1: residuals only for triangles of the re-projected kinds (zeros keep `hxy` parallel to the vertices)
    if (rq->rec->hxy_on) {
        static const float zero[3][2] = {};
        int kind = rq->rec->tag.kind, on = g->key.cls == RQ_C_TRI && (rq->opt.reproject >> kind & 1);
        if (on && kind != RQ_K_MODEL) {   // s81: other kinds only in draws into a frame-sized buffer (FBW = the
            // displayed one): render-to-texture targets (shadow / env maps) can be read back into game RAM later
            // (battle1 vblank 8142 Stage_Load downloads a page the menu's targets drew into), and downloads must
            // stay the 1x picture (sub-pixel 0, design 22.7)
            uint64_t pm, fb;
            memcpy(&pm, g->priv + rq_priv_offset(RQ_PRIV_PMODE), 8);
            memcpy(&fb, g->priv + rq_priv_offset((pm & 2) ? RQ_PRIV_DISPFB2 : RQ_PRIV_DISPFB1), 8);
            on = ((g->key.f[RQ_KEY_FRAME] >> 16) & 0x3f) == ((fb >> 9) & 0x3f);
        }
        rq->st.reproj_tris += on;
        if (g->key.cls == RQ_C_TRI && kind < RQ_K_N) {   // coverage: which kinds the VU1 sideband reaches
            rq->st.rp_tris[kind]++;
            rq->st.rp_hit[kind] += g->vhok[0] && g->vhok[1] && g->vhok[2];
        }
        rqr_prim_hxy(rq->rec, on ? g->vh : zero, n);
    }
}

static RqVert cur_vertex(RqGs *g, int xyzf, uint64_t v)
{
    const uint64_t *c = g->ctx[(eff_attr(g) >> 9) & 1];
    RqVert vx;   // every field set below
    vx.pad[0] = vx.pad[1] = vx.pad[2] = 0;
    uint64_t rgbaq = g->reg[0x01], st = g->reg[0x02], uv = g->reg[0x03];
    int ofx = (int)(c[RQ_CTX_XYOFFSET] & 0xffff), ofy = (int)((c[RQ_CTX_XYOFFSET] >> 32) & 0xffff);
    vx.x = (int)(v & 0xffff) - ofx;
    vx.y = (int)((v >> 16) & 0xffff) - ofy;
    if (xyzf) { vx.z = (uint32_t)((v >> 32) & 0xffffff); vx.f = (uint8_t)(v >> 56); }
    else { vx.z = (uint32_t)(v >> 32); vx.f = (uint8_t)(g->reg[0x0a] >> 56); }
    vx.r = (uint8_t)rgbaq; vx.g = (uint8_t)(rgbaq >> 8); vx.b = (uint8_t)(rgbaq >> 16); vx.a = (uint8_t)(rgbaq >> 24);
    vx.q = (uint32_t)(rgbaq >> 32); vx.s = (uint32_t)st; vx.t = (uint32_t)(st >> 32);
    vx.u = uv & 0x3fff; vx.v = (uv >> 16) & 0x3fff;
    return vx;
}

static void vertex_kick(Rq *rq, int xyzf, uint64_t v, int draw)
{
    RqGs *g = &rq->gs;
    int type = (int)(g->prim & 7);
    RqVert vx = cur_vertex(g, xyzf, v);
    static const int need[8] = {1, 2, 2, 3, 3, 3, 2, 1};
    float hx = g->hok ? g->hx : 0.0f, hy = g->hok ? g->hy : 0.0f;   // step 5.1 residual (packed_write), else none
    int hk = g->hok;
    g->hok = 0;
#define VH(d, s) (g->vh[d][0] = g->vh[s][0], g->vh[d][1] = g->vh[s][1], g->vhok[d] = g->vhok[s])
    if (g->nv < 3) { g->vh[g->nv][0] = hx; g->vh[g->nv][1] = hy; g->vhok[g->nv] = hk; g->vq[g->nv++] = vx; }
    else {
        g->vq[0] = g->vq[1]; g->vq[1] = g->vq[2]; g->vq[2] = vx; VH(0, 1); VH(1, 2); g->vh[2][0] = hx; g->vh[2][1] = hy;
        g->vhok[2] = hk;
    }
    if (g->nv < need[type]) return;
    if (draw) emit_prim(rq, need[type]);
    switch (type) {
    case 0: case 1: case 3: case 6: case 7: g->nv = 0; break;
    case 2: g->vq[0] = g->vq[1]; VH(0, 1); g->nv = 1; break;
    case 4: g->vq[0] = g->vq[1]; g->vq[1] = g->vq[2]; VH(0, 1); VH(1, 2); g->nv = 2; break;
    case 5: g->vq[1] = g->vq[2]; VH(1, 2); g->nv = 2; break;
    }
#undef VH
}

static void tex0_write(Rq *rq, int ctx, uint64_t v)
{
    RqGs *g = &rq->gs;
    uint64_t orig = v;
    int cld = (int)((v >> 61) & 7);
    v &= ~(7ull << 61);
    g->ctx[ctx][RQ_CTX_TEX0] = v;
    uint32_t cbp = (uint32_t)((v >> 37) & 0x3fff);
    int load = 0;
    switch (cld) {
    case 1: load = 1; break;
    case 2: load = 1; g->cbp0 = cbp; break;
    case 3: load = 1; g->cbp1 = cbp; break;
    case 4: load = cbp != g->cbp0; g->cbp0 = cbp; break;
    case 5: load = cbp != g->cbp1; g->cbp1 = cbp; break;
    }
    if (load && paletted((int)((v >> 20) & 0x3f))) rq_rec_clut(rq->rec, orig, g->reg[0x1c]);
}

#define BITBLT g->reg[0x50]
#define TRXPOS g->reg[0x51]
#define TRXREG g->reg[0x52]

static void tr_begin(Rq *rq)
{
    RqGs *g = &rq->gs;
    uint32_t rrw = TRXREG & 0xfff, rrh = (TRXREG >> 32) & 0xfff;
    g->tr_active = rrw && rrh;
    g->tr_bytes_left = (uint32_t)((uint64_t)rrw * rrh * psm_bits((int)((BITBLT >> 56) & 0x3f)) / 8);
    rq_rec_upload_begin(rq->rec, BITBLT, TRXPOS, TRXREG);
}

void rqg_tr_write(Rq *rq, const uint8_t *d, uint32_t n)
{
    RqGs *g = &rq->gs;
    if (!g->tr_active) return;
    if (n > g->tr_bytes_left) n = g->tr_bytes_left;
    if (n) rq_rec_upload_data(rq->rec, d, n);
    g->tr_bytes_left -= n;
    if (g->tr_bytes_left == 0) g->tr_active = 0;
}

static uint64_t *priv64(RqGs *g, uint32_t off) { return (uint64_t *)(g->priv + off); }

void rqg_write_reg(Rq *rq, int reg, uint64_t v)
{
    RqGs *g = &rq->gs;
    reg &= 0x7f;
    // RGBAQ / ST / UV / XYZF2 / XYZ2 / FOG / XYZF3 / XYZ3 do not feed rq_key_build; everything else may
    if (reg == 0 || (reg > 0x05 && reg != 0x0a && reg != 0x0c && reg != 0x0d)) g->key_ok = 0;
    switch (reg) {
    case 0x00: g->prim = v & 0x7ff; g->nv = 0; g->reg[0] = v; return;
    case 0x04: vertex_kick(rq, 1, v, 1); g->reg[reg] = v; return;
    case 0x05: vertex_kick(rq, 0, v, 1); g->reg[reg] = v; return;
    case 0x0c: vertex_kick(rq, 1, v, 0); g->reg[reg] = v; return;
    case 0x0d: vertex_kick(rq, 0, v, 0); g->reg[reg] = v; return;
    case 0x06: case 0x07: tex0_write(rq, reg - 0x06, v); return;
    case 0x16: case 0x17: {   // TEX2: replaces PSM + CLUT fields of TEX0
        uint64_t *c = g->ctx[reg - 0x16];
        uint64_t mask = (0x3full << 20) | (~0ull << 37);
        tex0_write(rq, reg - 0x16, (c[RQ_CTX_TEX0] & ~mask) | (v & mask));
        c[RQ_CTX_TEX2] = v;
        return;
    }
    case 0x08: case 0x09: g->ctx[reg - 0x08][RQ_CTX_CLAMP] = v; return;
    case 0x14: case 0x15: g->ctx[reg - 0x14][RQ_CTX_TEX1] = v; return;
    case 0x18: case 0x19: g->ctx[reg - 0x18][RQ_CTX_XYOFFSET] = v; return;
    case 0x1b: g->prmode = v; return;
    case 0x34: case 0x35: g->ctx[reg - 0x34][RQ_CTX_MIPTBP1] = v; return;
    case 0x36: case 0x37: g->ctx[reg - 0x36][RQ_CTX_MIPTBP2] = v; return;
    case 0x40: case 0x41: g->ctx[reg - 0x40][RQ_CTX_SCISSOR] = v; return;
    case 0x42: case 0x43: g->ctx[reg - 0x42][RQ_CTX_ALPHA] = v; return;
    case 0x47: case 0x48: g->ctx[reg - 0x47][RQ_CTX_TEST] = v; return;
    case 0x4a: case 0x4b: g->ctx[reg - 0x4a][RQ_CTX_FBA] = v; return;
    case 0x4c: case 0x4d: g->ctx[reg - 0x4c][RQ_CTX_FRAME] = v; return;
    case 0x4e: case 0x4f: g->ctx[reg - 0x4e][RQ_CTX_ZBUF] = v; return;
    case 0x50: g->reg[reg] = v; return;
    case 0x53:
        g->reg[reg] = v;
        switch (v & 3) {
        case 0: tr_begin(rq); break;
        case 1: rq_rec_readback(rq->rec, BITBLT, TRXPOS, TRXREG); break;
        case 2: rq_rec_copy(rq->rec, BITBLT, TRXPOS, TRXREG); break;
        }
        return;
    case 0x54: { uint8_t b[8]; memcpy(b, &v, 8); rqg_tr_write(rq, b, 8); return; }   // HWREG
    case 0x60: {   // SIGNAL
        uint32_t id = (uint32_t)(v >> 32), msk = (uint32_t)v;
        uint64_t *sl = priv64(g, 0x1080);
        *sl = (*sl & ~(uint64_t)msk) | (id & msk);
        *priv64(g, 0x1000) |= 1;
        rq->ev |= RQ_EV_GS_SIGNAL;
        return;
    }
    case 0x61:     // FINISH
        *priv64(g, 0x1000) |= 2;
        rq->ev |= RQ_EV_GS_FINISH;
        return;
    case 0x62: {   // LABEL
        uint32_t id = (uint32_t)(v >> 32), msk = (uint32_t)v;
        uint64_t *sl = priv64(g, 0x1080);
        *sl = (*sl & ~((uint64_t)msk << 32)) | ((uint64_t)(id & msk) << 32);
        return;
    }
    }
    g->reg[reg] = v;
}

void rq_priv_write(Rq *rq, uint32_t addr, int size, const void *val)
{
    RqGs *g = &rq->gs;
    uint32_t r = addr & 0x1fff;
    uint64_t v = 0;
    memcpy(&v, val, size > 8 ? 8 : size);
    if (r == 0x1000) {
        uint64_t csr = *priv64(g, 0x1000);
        csr &= ~(v & 0x1f);                 // SIGNAL/FINISH/HSINT/VSINT/EDWINT: write 1 clears
        if (v & 0x200) csr &= 1u << 13;     // RESET
        *priv64(g, 0x1000) = csr;
        return;
    }
    if (r + (uint32_t)size <= sizeof g->priv) memcpy(g->priv + r, val, size);
}

uint64_t rq_siglblid(const Rq *rq) { uint64_t v; memcpy(&v, rq->gs.priv + 0x1080, 8); return v; }
