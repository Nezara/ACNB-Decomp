/* lib/sdk/libdma.cpp - Sony libdma (SDK 2.7, main ELF 0x112c60-0x1133f8, no DWARF): channel lookup and the normal /
 * chain kicks the game uses (sceDmaGetChan, sceDmaSend, sceDmaSendN). Registered at the SDK addresses by
 * runtime/bridge_sdk.c, so the interpreted callers (boot MPG uploads, clearGsMem, FMV vblankHandler) use them too.
 * A sceDmaChan * is the channel's DMAC register block (CHCR +0, MADR +0x10, QWC +0x20, TADR +0x30); its registers are
 * only touched through the port.h ps2_dma_chan_* stand-ins (hybrid: the emulated DMAC in the original order; source
 * port: no registers - reads 0, and ps2_dma_chan_start hands the transfer to the render module).
 * Bring-up: sceDmaReset / sceDmaPutEnv (boot __InitPs2 and the FMV player; DMAC global registers through the port.h
 * ps2_dmac_* stand-ins, hybrid: the emulated registers in the original order; source port: no DMAC).
 * Not native: sceDmaGetEnv, sceDmaSendI, sceDmaRecv* (interpreted / unreached). */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include <string.h>

/* register blocks of channels 0..9 (the table at 0x2574d0): VIF0, VIF1, GIF, fromIPU, toIPU, SIF0-2, fromSPR, toSPR */
static const unsigned int s_dma_chan_base[10] = {
    0x10008000u, 0x10009000u, 0x1000a000u, 0x1000b000u, 0x1000b400u,
    0x1000c000u, 0x1000c400u, 0x1000c800u, 0x1000d000u, 0x1000d400u,
};

/* libdma_00112d58: DMA address of a memory address - a scratchpad address (top nibble 7) gets the SPR flag (bit 31);
 * anything else passes unchanged (no masking, unlike libgraph's inline conversion) */
static unsigned int dma_madr(unsigned int a)
{
#ifdef ACNB_SOURCEPORT
    return a;   /* host pointer as is (port.h PS2_DMA_ADDR, render module RQ_HOST_ADDR) */
#else
    if ((a >> 28) == 7) return (a & 0x0fffffffu) | 0x80000000u;
    return a;
#endif
}

/* libdma_00112c60: wait until the channel is idle (CHCR.STR clear). The original's 0x1000000-iteration timeout
 * (printf + register dump, then it keeps waiting) cannot fire here. */
static void dma_wait_idle(void *d)
{
    ps2_dma_chan_wait(d);
}

void *sceDmaGetChan(int id)
{
    if ((unsigned int)id < 10) return (void *)(uintptr_t)s_dma_chan_base[id];
    return 0;
}

/* chain-mode transfer starting at the tag at addr. TADR is only written when it is not -1 (an old SDK convention;
 * nothing writes -1 to D1/D2 TADR). Keeps every CHCR bit but STR / MOD / DIR (TTE, TIE, TAG of the last run).
 * Returns the CHCR value written (the original leaves it in v0). */
/* sceDmaSync (0x113388): mode 1 only polls CHCR.STR; every other mode (0, 2, ...) blocks until idle and returns 0.
 * The timeout argument is ignored by this SDK version. */
int sceDmaSync(void *d, int mode, int timeout)
{
    (void)timeout;
    if (mode == 1) return (int)((ps2_dma_chan_rd32(d, 0) >> 8) & 1);
    dma_wait_idle(d);
    return 0;
}

int sceDmaSend(void *d, void *addr)
{
    unsigned int tadr = dma_madr(PS2PTR(addr));
    dma_wait_idle(d);
    if (ps2_dma_chan_rd32(d, 0x30) != 0xffffffffu) ps2_dma_chan_wr32(d, 0x30, tadr);
    unsigned int chcr = (ps2_dma_chan_rd32(d, 0) & ~0xdu) | 0x105u;
    ps2_dma_chan_wr32(d, 0x20, 0);
    ps2_dma_chan_start(d, chcr, 0, 0, tadr);
    return (int)chcr;
}

/* normal-mode transfer of size qwords from addr: MADR (unless it reads -1), QWC, CHCR = old bits | STR | DIR */
int sceDmaSendN(void *d, void *addr, int size)
{
    unsigned int madr = dma_madr(PS2PTR(addr));
    dma_wait_idle(d);
    if (ps2_dma_chan_rd32(d, 0x10) != 0xffffffffu) ps2_dma_chan_wr32(d, 0x10, madr);
    unsigned int chcr = (ps2_dma_chan_rd32(d, 0) & ~0xdu) | 0x101u;
    ps2_dma_chan_wr32(d, 0x20, (unsigned int)size);
    ps2_dma_chan_start(d, chcr, madr, (unsigned int)size, 0);
    return (int)chcr;
}

#ifdef ACNB_SOURCEPORT
unsigned char sdk_dma_env[0x14];   /* 0x280360 */
unsigned int sdk_dma_chan_reg[24][4];   /* port.h ps2_dma_chan_*: the channel register words the game reads back */
#endif

/* channels with a register block sceDmaReset clears (0x26edf0: not SIF0-2) and the D_CTRL field encodings of a channel
 * id (0x26ee18 STS, 0x26ee28 STD, 0x26ee38 MFD; 0 = none) */
static const unsigned int s_dma_chan_reset[10] = {1, 1, 1, 1, 1, 0, 0, 0, 1, 1};
static const unsigned char s_dma_sts_code[10] = {0, 0, 0, 3, 0, 1, 0, 0, 2, 0};
static const unsigned char s_dma_std_code[10] = {0, 1, 2, 0, 0, 0, 3, 0, 0, 0};
static const unsigned char s_dma_mfd_code[10] = {0, 2, 3, 0, 0, 0, 0, 0, 0, 0};

/* D_CTRL STS / STD / MFD / RELE / RCYC, D_PCR, D_SQWC, D_RBOR, D_RBSR from env, then a copy of env for sceDmaGetEnv.
 * The original also reads D_PCR, D_SQWC, D_RBOR, D_RBSR at entry and drops the values (no read side effects in the
 * emulated DMAC, hw.c: skipped). */
int sceDmaPutEnv(sceDmaEnv *env)
{
    unsigned int c = ps2_dmac_rd32(0x00);
    if (env->sts >= 10) return -1;
    if (env->std >= 10) return -2;
    if (env->mfd >= 10) return -3;
    if (env->rcycle >= 7) return -4;
    c = (c & ~0x30u) | (unsigned int)s_dma_sts_code[env->sts] << 4;
    c = (c & ~0xc0u) | (unsigned int)s_dma_std_code[env->std] << 6;
    c = (c & ~0xcu) | (unsigned int)s_dma_mfd_code[env->mfd] << 2;
    if (env->rcycle) {
        c |= 2;
        c = (c & ~0x300u) | (unsigned int)(env->rcycle - 1) << 8;
    } else {
        c &= ~2u;
    }
    ps2_dmac_wr32(0x00, c);                                                         /* D_CTRL */
    ps2_dmac_wr32(0x20, (unsigned int)env->express << 16 | env->notify);           /* D_PCR */
    ps2_dmac_wr32(0x30, (unsigned int)env->tqwc << 16 | env->sqwc);                /* D_SQWC */
    ps2_dmac_wr32(0x50, PS2PTR(env->rbadr));                                        /* D_RBOR */
    ps2_dmac_wr32(0x40, env->rbmsk);                                                /* D_RBSR */
    memcpy(sdk_dma_env, env, 0x14);
    return 0;
}

/* clears SADR / CHCR / TADR / MADR / ASR1 / ASR0 of every channel but SIF0-2 (QWC stays), D_STAT (status bits
 * write-1-clear 0xff1f, then the read-back mask half written back = toggles every set mask bit off), puts the zero env
 * (memset helper libdma_00112d80, a byte loop) and with mode 1 sets D_CTRL.DMAE; returns the DMAE bit found at entry */
int sceDmaReset(int mode)
{
    PS2_FRAME(0x50);
    unsigned int old = ps2_dmac_rd32(0x00) & 1;
    for (int i = 0; i < 10; i++) {
        if (!s_dma_chan_reset[i]) continue;
        void *r = (void *)(uintptr_t)s_dma_chan_base[i];
        ps2_dma_chan_wr32(r, 0x80, 0);   /* SADR */
        ps2_dma_chan_wr32(r, 0x00, 0);   /* CHCR */
        ps2_dma_chan_wr32(r, 0x30, 0);   /* TADR */
        ps2_dma_chan_wr32(r, 0x10, 0);   /* MADR */
        ps2_dma_chan_wr32(r, 0x50, 0);   /* ASR1 */
        ps2_dma_chan_wr32(r, 0x40, 0);   /* ASR0 */
    }
    ps2_dmac_wr32(0x10, 0xff1fu);
    ps2_dmac_wr32(0x10, ps2_dmac_rd32(0x10) & 0xff1f0000u);
    PS2_STACK(sceDmaEnv, env, 0);
    memset(&env, 0, sizeof env);
    sceDmaPutEnv(&env);
    if (mode == 1) ps2_dmac_wr32(0x00, ps2_dmac_rd32(0x00) | 1);
    return (int)old;
}
