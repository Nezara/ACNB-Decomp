/* src/gfx/multx.cpp — MultiTex: animated / scrolling textures rendered into texture pages every frame
 * (render-to-texture). MultiTex_Init copies the stage's 16 entries (Array->btex) into s_tblMultx at stage start;
 * MultiTex_Draw (from __MakeGeneralDl) dispatches on s_modeMultx. Normal mode (the only one Versus reaches) redirects
 * the draw env to each entry's destination page (GL_SetDrawEnv1), sets TEX0 to the source texture and draws one
 * 4-vertex triangle strip whose UVs scroll with FRAME (__SetUVandVertexData, rotated per srot) and whose alpha pulses
 * with a sine. Noise / Clear modes (OLSORTIE __scrsys_ChangeTexture only, never in Versus) fill the first used entry's
 * page with random-offset noise / a black strip. */
#include "acnb/ps2fp.h"
#include "acnb/local/src/gfx/multx.h"

#ifdef ACNB_SOURCEPORT
int __DrawNoiseMode__s_irand = 0x001e2bf0;
unsigned char s_tblPsm[9] = {0x00, 0x01, 0x02, 0x0a, 0x13, 0x14, 0x1b, 0x24, 0x2c};
#endif

void MultiTex_Draw(unsigned int **gpp)
{
    PS2_FRAME(0x10);
    switch (s_modeMultx) {
    case 0: __DrawNormalMode(gpp); break;
    case 1: __DrawNoiseMode(gpp); break;
    case 2: __DrawClearMode(gpp); break;
    }
}

void MultiTex_Init(void)
{
    CopyMemory128b(s_tblMultx, Array->btex, 0x400);
}

/* Normal mode: per used entry, a render pass into page dtbp */
static void __DrawNormalMode(unsigned int **gpp)
{
    PS2_FRAME(0xe0);   /* keeps GL_SetDrawEnv1's env (RMW of stale stack) at its original slot */
    unsigned int *g = *gpp;
    GL_Sync(&g);
    GL_SetRegister(&g, 0x3f, 0);                       /* TEXFLUSH */
    GL_SetRegister(&g, 6, 0x8000000001400000ULL);      /* TEX0_1 */
    for (int i = 0; i < 16; i++) {
        struct struct_cfbac98f *e = &s_tblMultx[i];
        if (!e->dtbp) continue;
        /* render tag (host only): a pass into texture page dtbp */
        ps2_rq_tag_packet(g, RQ_K_ENV, RQ_OBJID(RQ_OC_TEXTURE, e->dtbp), (unsigned int)i);
        GL_SetRegister(&g, 0x4e, 0x0000000131000000ULL);   /* ZBUF_1: ZBP 0, PSMZ32, ZMSK */
        GL_SetRegister(&g, 0x47, 0x50000);                 /* TEST_1 */
        GL_SetDrawEnv1(&g, e->dwd, e->dht, s_tblPsm[e->dpsm], e->dtbp, NULL);
        GL_SetRegister(&g, 8, 0);                          /* CLAMP_1 */
        GL_SetRegister(&g, 0x42, AlphaBlendPaturn[e->blend]);
        int tw = 0, th = 0;
        for (int v = e->swd; v >= 2; v >>= 1) tw++;
        for (int v = e->sht; v >= 2; v >>= 1) th++;
        unsigned int tbw = (unsigned int)e->swd >> 6;
        unsigned long long tex0 = (unsigned long long)tbw << 14 | e->stbp | (unsigned long long)s_tblPsm[e->spsm] << 20 |
                                  (unsigned long long)tw << 26 | (unsigned long long)th << 30 | 4ULL << 32 /* TCC */ |
                                  (unsigned long long)e->scbp << 37 | (unsigned long long)s_tblPsm[e->scpsm] << 51 |
                                  0x8000000000000000ULL /* CLD 4 */;
        GL_SetRegister(&g, 6, tex0);                       /* TEX0_1 */
        GL_SetRegister(&g, 0x14, 0x60);                    /* TEX1_1 */
        unsigned int *p = g;
        p[0] = 0x1000000b; p[1] = 0; p[2] = 0; p[3] = 0;   /* DMA cnt 11 */
        p[4] = 0; p[5] = 0; p[6] = 0; p[7] = 0x5100000a;   /* DIRECT 10 */
        p[8] = 1; p[9] = 0x90aa4000;                       /* GIF NLOOP 1, PRE tristrip TME ABE FST, NREG 9 */
        p[10] = 0x34343431; p[11] = 4;                     /* RGBAQ, (UV, XYZ2) x4 */
        memcpy(p + 12, e->rgba, 16);
        if (e->a_frm) {
            float amin = (float)(int)e->a_min, amax = (float)(int)e->a_max;
            int r = ps2_mod32((int)(FRAME + e->a_phase), e->a_frm);
            float t = (float)r / (float)(int)e->a_frm;
            float s = fsmath_SinVU0(0x1.921fb6p+2f * t);   /* 2pi */
            float f = 1.0f + s;
            f = 0.5f * f;
            float d = amax - amin;
            f = f * d;
            f = amin + f;
            p[15] = (unsigned int)ps2_ftoi(f);
        }
        g = p + 16;
        __SetUVandVertexData(&g, e);
    }
    ps2_rq_tag_packet(g, RQ_K_NONE, 0, 0);   /* the ZBUF / DBUFF restore belongs to the frame, not the pass */
    GL_SetRegister(&g, 0x4e, 0x3100008c);                  /* ZBUF_1 back */
    GL_SetRegisters(&g, (unsigned long long *)(BuffID ? &DBUFF.draw1 : &DBUFF.draw0), 8);
    *gpp = g;
}

/* UV scroll offset: (FRAME * speed) mod m, as a 12.4 texel coordinate; 32-bit wrapping multiply, signed remainder */
static inline int multx_scroll(int s, int m)
{
    if (s > 0) return ps2_mod32((int)(FRAME * (unsigned int)s), m) >> 4;
    if (s < 0) return (m - ps2_mod32((int)(FRAME * (unsigned int)-s), m)) >> 4;
    return 0;
}

static inline unsigned int *multx_vertex(unsigned int *p, int u, int v, int x, int y)
{
    p[0] = (unsigned int)u; p[1] = (unsigned int)v; p[2] = 0; p[3] = 0;   /* UV */
    p[4] = (unsigned int)x; p[5] = (unsigned int)y; p[6] = 0xffffff; p[7] = 0x10;   /* XYZ2 */
    return p + 8;
}

/* the strip's 4 vertices: UV (A, B), (A, B + dB), (A + dA, B), (A + dA, B + dB) on the destination corners, rotated by
 * srot (cases 1 / 3 swap the source axes) */
static void __SetUVandVertexData(unsigned int **ppGSBuf, struct struct_cfbac98f *pBtex)
{
    unsigned int *p = *ppGSBuf;
    int hw = pBtex->dwd >> 1, hh = pBtex->dht >> 1;
    int SW = pBtex->swd << 4, SH = pBtex->sht << 4;
    int X0 = (0x800 - hw) << 4, X1 = (hw + 0x800) << 4;
    int Y0 = (0x800 - hh) << 4, Y1 = (hh + 0x800) << 4;
    int mw = pBtex->swd << 8, mh = pBtex->sht << 8;
    int A, B, P, Q;
    switch (pBtex->srot) {
    case 0: A = multx_scroll(pBtex->scs, mw); B = multx_scroll(pBtex->sct, mh); P = SW; Q = SH; break;
    case 1: A = multx_scroll(-pBtex->sct, mh); B = multx_scroll(pBtex->scs, mw); P = SH; Q = SW; break;
    case 2: A = multx_scroll(-pBtex->scs, mw); B = multx_scroll(-pBtex->sct, mh); P = SW; Q = SH; break;
    case 3: A = multx_scroll(pBtex->sct, mh); B = multx_scroll(-pBtex->scs, mw); P = SH; Q = SW; break;
    default: *ppGSBuf = p; return;
    }
    int A2 = A + ps2_ftoi((float)P * pBtex->xs);
    int B2 = B + ps2_ftoi((float)Q * pBtex->xt);
    int x[4], y[4];
    switch (pBtex->srot) {
    case 0: x[0] = X0; y[0] = Y0; x[1] = X0; y[1] = Y1; x[2] = X1; y[2] = Y0; x[3] = X1; y[3] = Y1; break;
    case 1: x[0] = X0; y[0] = Y1; x[1] = X1; y[1] = Y1; x[2] = X0; y[2] = Y0; x[3] = X1; y[3] = Y0; break;
    case 2: x[0] = X1; y[0] = Y1; x[1] = X1; y[1] = Y0; x[2] = X0; y[2] = Y1; x[3] = X0; y[3] = Y0; break;
    default: x[0] = X1; y[0] = Y0; x[1] = X0; y[1] = Y0; x[2] = X1; y[2] = Y1; x[3] = X0; y[3] = Y1; break;
    }
    p = multx_vertex(p, A, B, x[0], y[0]);
    p = multx_vertex(p, A, B2, x[1], y[1]);
    p = multx_vertex(p, A2, B, x[2], y[2]);
    p = multx_vertex(p, A2, B2, x[3], y[3]);
    *ppGSBuf = p;
}

/* the render pass header of the noise / clear modes: DMA cnt, DIRECT, GIF tag (lo, hi), REGS (lo, hi), RGBAQ */
static inline unsigned int *multx_pass_head(unsigned int *p, unsigned int qwc, unsigned int tag_hi, unsigned int regs_lo,
                                            unsigned int regs_hi, const unsigned int *rgba)
{
    p[0] = 0x10000000 | qwc; p[1] = 0; p[2] = 0; p[3] = 0;          /* DMA cnt */
    p[4] = 0; p[5] = 0; p[6] = 0; p[7] = 0x51000000 | (qwc - 1);    /* DIRECT */
    p[8] = 1; p[9] = tag_hi;
    p[10] = regs_lo; p[11] = regs_hi;
    memcpy(p + 12, rgba, 16);
    return p + 16;
}

static inline unsigned int *multx_xyz(unsigned int *p, int x, int y)
{
    p[0] = (unsigned int)x; p[1] = (unsigned int)y; p[2] = 0xffffff; p[3] = 0x10;
    return p + 4;
}

/* Clear mode: a black sprite strip over the destination page. Original quirk: the loop leaves after the first used
 * entry (only that page is cleared). */
static void __DrawClearMode(unsigned int **ppg)
{
    PS2_FRAME(0x50);
    alignas(16) static const unsigned int rgba[4] = {0, 0, 0, 0x80};   /* 0x25c060 */
    unsigned int *g = *ppg;
    GL_Sync(&g);
    for (int i = 0; i < 16; i++) {
        struct struct_cfbac98f *e = &s_tblMultx[i];
        if (!e->dtbp) continue;
        ps2_rq_tag_packet(g, RQ_K_ENV, RQ_OBJID(RQ_OC_TEXTURE, e->dtbp), (unsigned int)i);
        GL_SetDrawEnv1(&g, e->dwd, e->dht, s_tblPsm[e->dpsm], e->dtbp, NULL);
        GL_SetRegister(&g, 0x4e, 0x0000000131000000ULL);   /* ZBUF_1: ZMSK */
        int hw = e->dwd >> 1, hh = e->dht >> 1;
        int X0 = (0x800 - hw) << 4, X1 = (hw + 0x800) << 4;
        int Y0 = (0x800 - hh) << 4, Y1 = (hh + 0x800) << 4;
        unsigned int *p = multx_pass_head(g, 7, 0x50024000, 0x44441, 0, rgba);   /* tristrip, NREG 5: RGBAQ, XYZ2 x4 */
        p = multx_xyz(p, X0, Y0);
        p = multx_xyz(p, X0, Y1);
        p = multx_xyz(p, X1, Y0);
        p = multx_xyz(p, X1, Y1);
        g = p;
        ps2_rq_tag_packet(g, RQ_K_NONE, 0, 0);
        GL_SetRegister(&g, 0x4e, 0x31000070);              /* ZBUF_1 back */
        GL_SetRegisters(&g, (unsigned long long *)(BuffID ? &DBUFF.draw1 : &DBUFF.draw0), 8);
        break;
    }
    *ppg = g;
}

/* Noise mode: the first used entry's page gets a grey sprite strip textured from the fixed TEX0 page at a random
 * 0..127 texel offset (s_irand: 24-bit LCG x = x * 85 + 0x187, stepped twice per call even when nothing is drawn).
 * Original quirk: the loop leaves after the first used entry, as in clear mode. */
static void __DrawNoiseMode(unsigned int **ppg)
{
    PS2_FRAME(0x70);
    alignas(16) static const unsigned int rgba[4] = {0x40, 0x40, 0x40, 0x80};   /* 0x25c050 */
    int &x = __DrawNoiseMode__s_irand;
    x = (int)(((unsigned int)x * 85u + 0x187u) & 0xffffffu);
    int ru = x & 0x7f;
    x = (int)(((unsigned int)x * 85u + 0x187u) & 0xffffffu);
    int rv = x & 0x7f;
    unsigned int *g = *ppg;
    GL_Sync(&g);
    for (int i = 0; i < 16; i++) {
        struct struct_cfbac98f *e = &s_tblMultx[i];
        if (!e->dtbp) continue;
        ps2_rq_tag_packet(g, RQ_K_ENV, RQ_OBJID(RQ_OC_TEXTURE, e->dtbp), (unsigned int)i);
        GL_SetDrawEnv1(&g, e->dwd, e->dht, s_tblPsm[e->dpsm], e->dtbp, NULL);
        GL_SetRegister(&g, 0x4e, 0x0000000131000000ULL);   /* ZBUF_1: ZMSK */
        GL_SetRegister(&g, 0x3f, 0);                       /* TEXFLUSH */
        GL_SetRegister(&g, 8, 0);                          /* CLAMP_1 */
        GL_SetRegister(&g, 0x47, 0x31003);                 /* TEST_1 */
        GL_SetRegister(&g, 6, 0x8007de65dd40a000ULL);      /* TEX0_1 (passed as a 128-bit value, high half 0) */
        GL_SetRegister(&g, 0x14, 0);                       /* TEX1_1 */
        GL_SetRegister(&g, 0x42, 0x44);                    /* ALPHA_1 */
        int hw = e->dwd >> 1, hh = e->dht >> 1;
        int X0 = (0x800 - hw) << 4, X1 = (hw + 0x800) << 4;
        int Y0 = (0x800 - hh) << 4, Y1 = (hh + 0x800) << 4;
        int u0 = (ru + 1) << 4, v0 = (rv + 1) << 4;
        int u1 = u0 + 0x810, v1 = v0 + 0x810;
        unsigned int *p = multx_pass_head(g, 11, 0x90aa4000, 0x34343431, 4, rgba);   /* as normal mode */
        p = multx_vertex(p, u0, v0, X0, Y0);
        p = multx_vertex(p, u0, v1, X0, Y1);
        p = multx_vertex(p, u1, v0, X1, Y0);
        p = multx_vertex(p, u1, v1, X1, Y1);
        g = p;
        ps2_rq_tag_packet(g, RQ_K_NONE, 0, 0);
        GL_SetRegister(&g, 0x4e, 0x31000070);              /* ZBUF_1 back */
        GL_SetRegisters(&g, (unsigned long long *)(BuffID ? &DBUFF.draw1 : &DBUFF.draw0), 8);
        break;
    }
    *ppg = g;
}
