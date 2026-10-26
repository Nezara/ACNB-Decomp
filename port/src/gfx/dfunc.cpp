/* src/gfx/dfunc.c - small draw helpers: texture-flush GIF packets kicked on DMA channel 2 (tex_flush at boot,
 * DF_Texflush for the lens flare), one-register GSL lists for TEST / ALPHA (context 1 or 2), a fixed register-reset
 * packet (DF_KickPreDraw, i.LINK screens), the VU1 microcode upload (DF_SendPGVU1: four MPG chains on channel 1)
 * and a generic sceDmaSend wrapper (DF_DMA_Send, area map).
 * The three function-local `dma` packet buffers are DMA sources: they stay in PS2 memory (DWARF globals in the
 * hybrid, 16-aligned file statics in the source port). No render tags: the register lists inherit the caller's
 * scope, the uploads are UPLOAD records by themselves. */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/local/src/gfx/dfunc.h"

#ifdef ACNB_SOURCEPORT
unsigned int DF_KickPreDraw__dma[128] __attribute__((aligned(16)));
unsigned int DF_Texflush__dma[8] __attribute__((aligned(16)));
unsigned int tex_flush__dma[12] __attribute__((aligned(16)));
#endif

/* TEXFLUSH through a DMA END tag (QWC 2: GIF tag + one A+D pair) started on channel 2 in chain mode, waiting for the
 * channel before and after */
void tex_flush(void)
{
    PS2_FRAME(0x10);
    unsigned int *p = tex_flush__dma;
    p[1] = 0;
    p[0] = 0x70000002;   /* DMA END, QWC 2 */
    p[2] = 0;
    p[3] = 0;
    p[4] = 0x8001;       /* GIF tag: NLOOP 1, EOP, PRE (PRIM 0), NREG 1, A+D */
    p[5] = 0x10004000;
    p[6] = 0xe;
    p[7] = 0;
    p[8] = 0xffffffff;   /* TEXFLUSH */
    p[9] = 0;
    p[10] = 0x3f;
    p[11] = 0;
    FlushCache(0);
    ps2_dma_wait_gif();
    ps2_dma_kick_tadr(2, 0x105, PS2PTR(tex_flush__dma));
    ps2_dma_wait_gif();
}

/* TEXFLUSH as a ready GIF packet (GifPath3KickMem) */
void DF_Texflush(void)
{
    unsigned int *p = DF_Texflush__dma;
    p[0] = 0x8001;
    p[1] = 0x10004000;
    p[2] = 0xe;
    p[3] = 0;
    *(unsigned long long *)(p + 4) = 0xffffffffull;
    *(unsigned long long *)(p + 6) = 0x3f;
    GifPath3KickMem(DF_Texflush__dma, 2);
}

/* TEST_1 (ctxt 1) or TEST_2 as a one-register GSL list (each field sign-extended, no masks) */
void DF_SetTestRegister(int ate, int atst, int aref, int afail, int date, int datm, int zte, int ztst, int ctxt)
{
    PS2_FRAME(0x90);
    /* the original's prologue saves s6 at sp+0x60 (the upper half = the stale vertex-0 Z word the next sibling
     * DF_DrawColorScreen_NoSemi puts into its GS list); not mirrored: s75 g8b verify, battle1 rq hashes identical
     * with / without PS2_SAVED_GPR(22, 0x60) and vs the interpreted chain */
    GS_NewList();
    GS_SetRegister(ctxt == 1 ? 0x47 : 0x48,
                   (unsigned long long)(long long)ate | (unsigned long long)(long long)atst << 1
                       | (unsigned long long)(long long)aref << 4 | (unsigned long long)(long long)afail << 12
                       | (unsigned long long)(long long)date << 14 | (unsigned long long)(long long)datm << 15
                       | (unsigned long long)(long long)zte << 16 | (unsigned long long)(long long)ztst << 17);
    GS_FlushList();
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper (gen_bridge stops at 8 int args); ctxt is the caller's stack arg at sp+0 */
static void bw__DF_SetTestRegister(EECore *c)
{
    unsigned int sp = c->gpr[SP].uw[0];
    DF_SetTestRegister(c->gpr[A0].sw[0], c->gpr[A1].sw[0], c->gpr[A2].sw[0], c->gpr[A3].sw[0], c->gpr[T0].sw[0],
                       c->gpr[T1].sw[0], c->gpr[T2].sw[0], c->gpr[T3].sw[0], (int)rd32(sp));
}
#endif

/* ALPHA_1 (ctxt 1) or ALPHA_2 as a one-register GSL list */
void DF_SetAlphaRegister(int a, int b, int c, int d, int fix, int ctxt)
{
    PS2_FRAME(0x70);
    GS_NewList();
    GS_SetRegister(ctxt == 1 ? 0x42 : 0x43,
                   (unsigned long long)(long long)a | (unsigned long long)(long long)b << 2
                       | (unsigned long long)(long long)c << 4 | (unsigned long long)(long long)d << 6
                       | (unsigned long long)(long long)fix << 32);
    GS_FlushList();
}

/* register reset before i.LINK screen drawing: PABE 0, ALPHA_1 / _2 0, TEST_1 0x7000f, TEST_2 0x50ff5, CLAMP_1 0
 * (PABE is written twice) */
void DF_KickPreDraw(void)
{
    unsigned int *p = DF_KickPreDraw__dma;
    p[0] = 0x8007;   /* GIF tag: NLOOP 7, EOP, PRE (PRIM 0), NREG 1, A+D */
    p[1] = 0x10004000;
    p[2] = 0xe;
    p[3] = 0;
    unsigned long long *q = (unsigned long long *)(p + 4);
    q[0] = 0;       q[1] = 0x49;    /* PABE */
    q[2] = 0;       q[3] = 0x42;    /* ALPHA_1 */
    q[4] = 0;       q[5] = 0x49;    /* PABE */
    q[6] = 0;       q[7] = 0x43;    /* ALPHA_2 */
    q[8] = 0x7000f; q[9] = 0x47;    /* TEST_1 */
    q[10] = 0x50ff5; q[11] = 0x48;  /* TEST_2 */
    q[12] = 0;      q[13] = 8;      /* CLAMP_1 */
    GifPath3KickMem(DF_KickPreDraw__dma, 8);
}

/* uploads the VU1 microprograms (channel 1 with TTE on: the MPG chains) and waits for the channel */
void DF_SendPGVU1(void)
{
    PS2_FRAME(0x20);
    void *ch = sceDmaGetChan(1);
    ps2_dma_chan_wr8(ch, (ps2_dma_chan_rd8(ch) & ~0x40u) | 0x40u);
    sceDmaSend(ch, g_pmpgVu1Nlnf);
    sceDmaSend(ch, g_pmpgVu1Shadow);
    sceDmaSend(ch, g_pmpgVu12f);
    sceDmaSend(ch, g_pmpgVu1OnlyAmg);
    ps2_dma_wait_vif1();
}

/* chain-mode send on channel ch with TTE = tte & 1, then sceDmaSync (blocking when blk == 0) */
void DF_DMA_Send(int ch, int tte, void *addr, int blk)
{
    PS2_FRAME(0x40);
    void *c = sceDmaGetChan(ch);
    ps2_dma_chan_wr8(c, (ps2_dma_chan_rd8(c) & ~0x40u) | (unsigned int)(tte & 1) << 6);
    sceDmaSend(c, addr);
    sceDmaSync(c, blk == 0, 0);
}
