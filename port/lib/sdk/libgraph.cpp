/* lib/sdk/libgraph.cpp - Sony libgraph (SDK 2.7, main ELF 0x11e2d0-0x11ff38, no DWARF): the GS packet builders and
 * GIF-channel kicks the game uses every frame - GParam, draw environment (sceGsSetDefDrawEnv / sceGsPutDrawEnv),
 * image upload (sceGsSetDefLoadImage / sceGsExecLoadImage), sceGsSyncPath, sceGsSetHalfOffset - and the libkernl
 * FlushCache syscall. Registered at the SDK addresses by runtime/bridge_sdk.c (interpreted callers use them too).
 * Kicks go through port.h (ps2_dma_wait_gif + ps2_dma_kick_notadr: QWC, MADR, CHCR like the original); the
 * 0x1000000-iteration timeouts (printf, return -1) of the waits cannot fire in the emulator or the source port.
 * Display side: sceGsPutDispEnv (privileged PMODE / SMODE2 / DISPFB / DISPLAY / BGCOLOR), sceGsSwapDBuff, sceGsSyncV
 * (source port only: the hybrid keeps the original, its wait depends on the interpreter's interrupt delivery);
 * readback: sceGsSetDefStoreImage / sceGsExecStoreImage (VIF1 local -> host transfer).
 * Double buffer setup: sceGsSetDefDBuff with sceGsSetDefDispEnv / sceGsSetDefClear (s75 g8a).
 * Bring-up (s76 g10b): sceGsResetGraph (GParam + GS reset / display mode; boot, FMV and LoopSortie_Begin) and
 * sceGsResetPath (VIF1 / GIF path reset; boot, FMV). */
#include "acnb/port.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
/* GParam (0x257c70): the ELF's initial words 0x00020001 0x00030001 0 0 */
extern "C" sceGsGParam sdk_gparam = {1, 2, 1, 3, 0, 0};
#endif

/* GIF DMA address of a packet (libgraph's inline conversion): the physical address, scratchpad flagged by bit 31.
 * Source port: the host pointer unchanged (port.h PS2_DMA_ADDR; the render module is built with RQ_HOST_ADDR). */
static unsigned int gs_dma_madr(unsigned int a)
{
#ifdef ACNB_SOURCEPORT
    return a;
#else
    if ((a & 0x70000000u) == 0x70000000u) return (a & 0x0fffffffu) | 0x80000000u;
    return a & 0x0fffffffu;
#endif
}

sceGsGParam *sceGsGetGParam(void)
{
    return &sdk_gparam;
}

/* libgraph_0011e830: 8 KB pages of a frame buffer psm / w x h (64x32-pixel pages, 64x64 for the 16-bit formats),
 * doubled unless inter == 1 and ffmode == 0 (GParam read at every call: Draw_SetupFrameBuffer rewrites ffmode) */
static short gs_frame_pages(short psm, short w, short h)
{
    sceGsGParam *gp = sceGsGetGParam();
    int pw = (w + 63) / 64;
    int ph = (psm & 2) ? (h + 63) / 64 : (h + 31) / 32;
    int r = pw * ph;
    if (gp->inter == 1 && gp->ffmode == 0) return (short)r;
    return (short)(r * 2);
}

/* draw environment 1 (A+D list, 8 qwords, after the caller's GIF tag): FRAME_1, ZBUF_1 (right after the frame
 * pages), XYOFFSET_1 (centred in the 4096 space), SCISSOR_1, PRMODECONT, COLCLAMP, DTHE, TEST_1. PRMODECONT, COLCLAMP
 * and DTHE are read-modify-writes of the caller's buffer (only bit 0 is set / cleared). */
int sceGsSetDefDrawEnv(void *draw, short psm, short w, short h, short ztest, short zpsm)
{
    unsigned long long *e = (unsigned long long *)draw;
    e[1] = 0x4c;
    e[0] = (unsigned long long)(((w + 63) >> 6) & 0x3f) << 16 | (unsigned long long)(psm & 0xf) << 24;
    e[3] = 0x4e;
    unsigned long long zbuf = (unsigned long long)(long long)gs_frame_pages(psm, w, h)
                              | (unsigned long long)(zpsm & 0xf) << 24;
    if (ztest == 0) zbuf |= 1ull << 32;   /* ZMSK: no depth writes */
    e[2] = zbuf;
    unsigned long long ox = 0x800ull - (unsigned long long)(long long)(short)(w >> 1);
    unsigned long long oy = 0x800ull - (unsigned long long)(long long)(short)(h >> 1);
    unsigned long long scx = (unsigned long long)(long long)(w - 1) << 16;
    unsigned long long scy = (unsigned long long)(long long)(h - 1) << 48;
    unsigned long long prmodecont = e[8] | 1, colclamp = e[10] | 1;
    e[5] = 0x18;
    e[4] = ox << 4 | oy << 36;
    e[7] = 0x40;
    e[6] = scx | scy;
    e[9] = 0x1a;
    e[8] = prmodecont;
    e[11] = 0x46;
    e[10] = colclamp;
    e[13] = 0x45;
    e[12] = (psm & 2) ? e[12] | 1 : e[12] & ~1ull;   /* dither the 16-bit formats */
    e[15] = 0x47;
    e[14] = ztest ? (unsigned long long)((ztest & 3) << 17) | 0x10000 : 0x30000;   /* ZTE, ZTST / ZTST always */
    return 8;
}

/* sends the GIF tag at giftag and its NLOOP A+D qwords on the GIF channel; CHCR is stored, not merged (TTE cleared) */
int sceGsPutDrawEnv(void *giftag)
{
    ps2_dma_wait_gif();
    unsigned int qwc = (unsigned int)((int)(*(unsigned long long *)giftag & 0x7fff) + 1);
    ps2_dma_kick_notadr(2, 0x101, gs_dma_madr(PS2PTR(giftag)), qwc);
    return 0;
}

/* display environment (sceGsDispEnv, 0x28): PMODE 0x66 (read circuit 2, ALP 0 ... as the SDK), SMODE2 from GParam
 * (non-interlaced 2, interlaced FRAME 3, FIELD 1), DISPFB (FBP 0), DISPLAY centred for the video mode with dx / dy
 * added, BGCOLOR 0. The display-offset words (sp+0..0xc) are 0 for NTSC / PAL; for the other modes (DTV / VESA)
 * the original reads rom0:ROMVER (libgraph_0011e348) and asks the kernel (syscall 0x80) for them - not ported: ACNB
 * only resets the GS to NTSC, those offsets are taken as 0 here. An unknown mode prints a message (not ported) and
 * leaves DISPLAY as it was. Signed C arithmetic and 64-bit field shifts as the original (no masking of DW / DH). */
void sceGsSetDefDispEnv(void *disp, short psm, short w, short h, short dx, short dy)
{
    unsigned long long *d = (unsigned long long *)disp;
    sceGsGParam *gp = sceGsGetGParam();
    int off0 = 0, off1 = 0;   /* off2 / off3 (sp+8 / 0xc) are unused */
    d[0] = 0x66;
    d[1] = gp->inter == 0 ? 2 : (gp->ffmode != 0 ? 3 : 1);
    d[2] = (unsigned long long)(psm & 0xf) << 15 | (unsigned long long)(((w + 0x3f) >> 6) & 0x3f) << 9;
    short mode = gp->omode;
    if (mode == 2 || mode == 3) {   /* NTSC / PAL */
        int ntsc = mode == 2;
        int magh = (w + 0x9ff) / w;
        unsigned long long dxv = (unsigned long long)((long long)(dx * magh) + (long long)off0 + (ntsc ? 0x27c : 0x290))
                                 & 0xfff;
        unsigned long long dy_, dh;
        if (gp->inter == 1) {
            dy_ = (unsigned long long)((dy + off1 + (ntsc ? 0x32 : 0x48)) & 0xfff) << 12;
            dh = (unsigned long long)(long long)(gp->ffmode != 0 ? h * 2 - 1 : h - 1) << 44;
        } else {
            dy_ = (unsigned long long)((dy + off1 + (ntsc ? 0x19 : 0x24)) & 0xfff) << 12;
            dh = (unsigned long long)(long long)(h - 1) << 44;
        }
        d[3] = (unsigned long long)(long long)(magh - 1) << 23 | (unsigned long long)(long long)(magh * w - 1) << 32
               | dxv | dh | dy_;
    } else if (mode == 0x50) {   /* DTV 480p: MAGH 1 */
        int cx = (0x2d0 - w) / 2 * 2;
        unsigned long long dxv = (unsigned long long)((long long)off0 + cx + (long long)(dx * 2) + 0xe8) & 0xfff;
        unsigned long long dy_ = (unsigned long long)((dy + off1 + 0x23) & 0xfff) << 12;
        d[3] = (unsigned long long)(long long)(h - 1) << 44 | (unsigned long long)(long long)(w * 2 - 1) << 32
               | dxv | 0x800000ull | dy_;
    }
    d[4] = 0;
}

/* clear packet (sceGsClear, 6 A+D qwords): TEST (ZTE, ZTST ALWAYS), PRIM sprite, RGBAQ (Q 1.0), the two XYZ2
 * corners (x, y, x + w, y + h in 12.4, z), TEST again with the caller's depth test (0: ALWAYS). Unmasked shifts as
 * the original (a value over 0xfff spills into the next field). Returns 6. */
int sceGsSetDefClear(void *clear, short ztest, short x, short y, short w, short h, int r, int g, int b, int a,
                     unsigned int z)
{
    unsigned long long *c = (unsigned long long *)clear;
    unsigned long long rgbaq = (unsigned long long)(r & 0xff) | 0x3f80000000000000ull
                               | ((unsigned long long)(g & 0xff) << 8 | (unsigned long long)(b & 0xff) << 16)
                               | (unsigned long long)(a & 0xff) << 24;
    unsigned long long zz = (unsigned long long)z << 32;
    unsigned long long xy0 = (unsigned long long)(long long)(x << 4)
                             | (unsigned long long)(long long)(y << 4) << 16;
    unsigned long long xy1 = (unsigned long long)(long long)((x + w) << 4)
                             | (unsigned long long)(long long)((y + h) << 4) << 16;
    c[2] = 6;      c[5] = 1;
    c[4] = rgbaq;  c[6] = xy0 | zz;
    c[9] = 5;      c[8] = xy1 | zz;
    c[11] = 0x47;  c[1] = 0x47;
    c[0] = 0x30000;
    c[3] = 0;
    c[7] = 5;
    c[10] = ztest ? (unsigned long long)((ztest & 3) << 17) | 0x10000 : 0x30000;
    return 6;
}

/* double buffer (sceGsDBuff, 0x230): both display envs, both draw envs, optionally both clear packets, the two
 * GIF tags (NLOOP 8, or 14 with the clear, EOP, A+D), then for a non-interlaced or interlaced-FRAME GParam the
 * second buffer half: draw0's FBP and disp[1]'s DISPFB FBP = half the frame pages. Sony returns nothing useful. */
void sceGsSetDefDBuff(void *db, short psm, short w, short h, short ztest, short zpsm, short clear)
{
    unsigned char *p = (unsigned char *)db;
    sceGsGParam *gp = sceGsGetGParam();
    sceGsSetDefDispEnv(p, psm, w, h, 0, 0);
    sceGsSetDefDispEnv(p + 0x28, psm, w, h, 0, 0);
    sceGsSetDefDrawEnv(p + 0x60, psm, w, h, ztest, zpsm);
    sceGsSetDefDrawEnv(p + 0x150, psm, w, h, ztest, zpsm);
    if (clear) {
        short x = (short)(0x800 - (w >> 1)), y = (short)(0x800 - (h >> 1));
        sceGsSetDefClear(p + 0xe0, ztest, x, y, w, h, 0, 0, 0, 0, 0);
        sceGsSetDefClear(p + 0x1d0, ztest, x, y, w, h, 0, 0, 0, 0, 0);
    }
    unsigned long long *t0 = (unsigned long long *)(p + 0x50), *t1 = (unsigned long long *)(p + 0x140);
    unsigned long long nloop = clear ? 14 : 8;
    t0[0] = nloop | 0x8000 | 1ull << 60;   /* NLOOP, EOP, NREG 1 */
    t1[0] = nloop | 0x8000 | 1ull << 60;
    t0[1] = 0xe;                           /* REGS: A+D */
    t1[1] = 0xe;
    int pages = gs_frame_pages(psm, w, h);
    bool second = (gp->inter == 1 && gp->ffmode == 1) || gp->inter == 0;
    if (!second) return;
    int half = pages >> 1;
    unsigned long long *fb = (unsigned long long *)(p + 0x60), *dfb = (unsigned long long *)(p + 0x38);
    *fb = (*fb & ~0x1ffull) | (unsigned long long)(half & 0x1ff);
    *dfb = (*dfb & ~0x1ffull) | (unsigned long long)((short)half & 0x1ff);
}

/* busy state of the drawing paths: bit 0 D1 (VIF1 DMA), 1 D2 (GIF DMA), 2 VIF1 FIFO / VPS, 3 VU1 running, 4 GIF
 * PATH active. mode 0 waits until all are idle (returns 0), any other mode returns the mask (vblankHandler, FMV).
 * The timeout argument is unused (the original reuses a1 as its counter). */
int sceGsSyncPath(int mode, unsigned short timeout)
{
    (void)timeout;
    if (mode != 0) return (int)ps2_gs_path_busy();
    ps2_dma_wait_vif1();
    ps2_dma_wait_gif();
    while (ps2_gs_path_busy() & 0x1c) {}
    return 0;
}

/* 6-qword image upload header: GIF tag (NLOOP 4, A+D) + BITBLTBUF / TRXPOS / TRXREG / TRXDIR (host -> local), then
 * the IMAGE tag (EOP, FLG 2) with the image's qword count. The fields are not masked: the sign-extended shorts smear
 * into the neighbouring fields when negative. Returns 6, or 0 (lp untouched) when the image exceeds 0x7fff qwords;
 * a psm outside the table gives qwc 0. */
int sceGsSetDefLoadImage(void *lp, short dbp, short dbw, short dpsm, short x, short y, short w, short h)
{
    int qwc = 0;
    switch ((unsigned int)dpsm) {
    case 0x00: case 0x30: qwc = (w * h) >> 2; break;                             /* PSMCT32 / PSMZ32 */
    case 0x01: case 0x31: { int n = w * h; qwc = (n * 2 + n) >> 4; break; }      /* 24 */
    case 0x02: case 0x0a: case 0x32: case 0x3a: qwc = (w * h) >> 3; break;       /* 16 / 16S */
    case 0x13: case 0x1b: qwc = (w * h) >> 4; break;                             /* T8 / T8H */
    case 0x14: case 0x24: case 0x2c: qwc = (w * h) >> 5; break;                  /* T4 / T4HL / T4HH */
    default: break;
    }
    if (qwc > 0x7fff) return 0;   /* the original prints a message */
    unsigned long long *p = (unsigned long long *)lp;
    p[10] = 0; p[11] = 0;
    p[0] = 0; p[1] = 0;
    p[10] = (unsigned long long)(qwc & 0x7fff) | 0x8000 | 2ull << 58;   /* IMAGE tag: NLOOP, EOP, FLG IMAGE */
    p[0] = 4 | 1ull << 60;                                               /* NLOOP 4, NREG 1 */
    p[1] = 0xe;                                                          /* REGS: A+D */
    p[2] = (unsigned long long)(long long)dbp << 32 | (unsigned long long)(long long)dbw << 48
           | (unsigned long long)(long long)dpsm << 56;                  /* BITBLTBUF */
    p[3] = 0x50;
    p[4] = (unsigned long long)(long long)x << 32 | (unsigned long long)(long long)y << 48;   /* TRXPOS */
    p[5] = 0x51;
    p[6] = (unsigned long long)(long long)w | (unsigned long long)(long long)h << 32;          /* TRXREG */
    p[7] = 0x52;
    p[9] = 0x53;
    p[8] = 0;                                                            /* TRXDIR: host -> local */
    return 6;
}

/* sends the header (6 qwords), then the image (the IMAGE tag's NLOOP qwords from srcaddr) without waiting for it */
int sceGsExecLoadImage(void *lp, void *srcaddr)
{
    ps2_dma_wait_gif();
    ps2_dma_kick_notadr(2, 0x101, gs_dma_madr(PS2PTR(lp)), 6);
    ps2_dma_wait_gif();
    unsigned int qwc = (unsigned int)(int)(((unsigned long long *)lp)[10] & 0x7fff);
    ps2_dma_kick_notadr(2, 0x101, gs_dma_madr(PS2PTR(srcaddr)), qwc);
    return 0;
}

/* XYOFFSET_1 of draw so that (centerx, centery) is the centre of its scissor, + half a pixel for the odd field.
 * Original bug kept: ox is a full 64-bit value, so a negative one (centerx < w / 2) smears 1s over the Y field. */
void sceGsSetHalfOffset(void *draw, short centerx, short centery, short field)
{
    unsigned long long *e = (unsigned long long *)draw;
    unsigned long long sc = e[6];
    unsigned long long w2 = (((sc >> 16) & 0x7ff) + 1) >> 1;
    unsigned long long h2 = (((sc >> 48) & 0x7ff) + 1) >> 1;
    unsigned long long ox = ((unsigned long long)(long long)centerx - w2) << 4;
    unsigned long long oy = ((unsigned long long)(long long)centery - h2) << 4;
    if (field) oy += 8;
    e[4] = ox | oy << 32;
}

/* display environment -> the privileged registers, read circuit 2 (GS revision 1 parts: circuit 1, and BGCOLOR's
 * value goes to EXTDATA 0x120000c0 - SDK quirk kept). GParam's revision is read at every call. */
void sceGsPutDispEnv(void *disp)
{
    const unsigned long long *d = (const unsigned long long *)disp;
    if (sceGsGetGParam()->version == 1) {
        ps2_gs_priv_write64(0x12000000u, d[0]);   /* PMODE */
        ps2_gs_priv_write64(0x12000070u, d[2]);   /* DISPFB1 */
        ps2_gs_priv_write64(0x12000080u, d[3]);   /* DISPLAY1 */
        ps2_gs_priv_write64(0x120000c0u, d[4]);   /* EXTDATA (BGCOLOR's value) */
        return;
    }
    ps2_gs_priv_write64(0x12000000u, d[0]);       /* PMODE */
    ps2_gs_priv_write64(0x12000020u, d[1]);       /* SMODE2 */
    ps2_gs_priv_write64(0x12000090u, d[2]);       /* DISPFB2 */
    ps2_gs_priv_write64(0x120000a0u, d[3]);       /* DISPLAY2 */
    ps2_gs_priv_write64(0x120000e0u, d[4]);       /* BGCOLOR */
}

/* shows buffer id & 1 (disp[id], 0x28 bytes each) and sends the matching draw packet (GIF tag at +0x50 / +0x140) */
int sceGsSwapDBuff(void *db, int id)
{
    int i = id & 1;
    sceGsPutDispEnv((unsigned char *)db + i * 0x28);
    return sceGsPutDrawEnv((unsigned char *)db + (i ? 0x140 : 0x50));
}

#ifdef ACNB_SOURCEPORT
/* waits for the next vblank (mode unused). The value is state the Draw_Flip* family uses: inter == 1 (Versus) ->
 * the GS CSR FIELD bit, else the wait's own result (always 0). The vsync_flag branch (SetVSyncFlag handler wait) is
 * dead - nothing sets GParam+8; the original returns the field there for inter == 1, else CSR >> 13 (simplified to
 * the field here). Hybrid: bridge_sdk.c calls the original. */
int sceGsSyncV(int mode)
{
    (void)mode;
    sceGsGParam *gp = sceGsGetGParam();
    int r = ps2_vsync_wait();
    if (gp->vsync_flag != 0) return (int)ps2_gs_field();
    if (gp->inter == 1) return (int)ps2_gs_field();
    return r;
}
#endif

/* 7-qword readback header: VIF1 NOP / MSKPATH3 (mask path 3) / FLUSHA / DIRECT 6, then GIF tag (NLOOP 5, EOP, A+D)
 * + BITBLTBUF (source) / TRXPOS / TRXREG / FINISH / TRXDIR 1 (local -> host). Every word is written; the shorts are
 * sign-extended (a negative one smears into the higher fields). Returns 7. */
int sceGsSetDefStoreImage(void *sp, short sbp, short sbw, short spsm, short x, short y, short w, short h)
{
    unsigned long long *p = (unsigned long long *)sp;
    unsigned int *u = (unsigned int *)sp;
    p[2] = 0; p[3] = 0;
    unsigned long long tag = (p[2] & ~0x7fffull) | 5 | 0x8000;
    tag = (tag & 0x0fffffffffffffffull) | 1ull << 60;
    unsigned long long regs = (p[3] & ~0xfull) | 0xe;
    u[1] = 0x06008000u;   /* MSKPATH3 1 */
    p[13] = 0x53;
    u[2] = 0x13000000u;   /* FLUSHA */
    u[3] = 0x50000006u;   /* DIRECT 6 */
    p[2] = tag;
    p[3] = regs;
    p[4] = (unsigned long long)(long long)sbp | (unsigned long long)(long long)sbw << 16
           | (unsigned long long)(long long)spsm << 24;                                     /* BITBLTBUF */
    p[5] = 0x50;
    p[6] = (unsigned long long)(long long)x | (unsigned long long)(long long)y << 16;     /* TRXPOS */
    p[7] = 0x51;
    p[8] = (unsigned long long)(long long)w | (unsigned long long)(long long)h << 32;     /* TRXREG */
    p[9] = 0x52;
    p[11] = 0x61;
    p[12] = 1;            /* TRXDIR: local -> host */
    u[0] = 0;             /* NOP */
    p[10] = 0;            /* FINISH */
    return 7;
}

#ifndef ACNB_SOURCEPORT
/* the original's busy-waits with their shared timeout counter (0x1000000 polls): CSR.FINISH and VIF1_STAT.FQC. The
 * emulator's VIF1_STAT has FQC = 1 while FDR is set and transfer data is left (hw.c hw_read), so the FIFO reads below
 * pass the first poll, natively and interpreted alike. */
static int gs_wait_finish(unsigned int &to)
{
    if (ps2_gs_csr() & 2) return 1;
    for (;;) {
        if (to > 0x1000000u) return 0;
        to++;
        if (ps2_gs_csr() & 2) return 1;
    }
}
static int gs_wait_fqc(unsigned int &to)
{
    if (ps2_vif1_stat() & 0x1f000000u) return 1;
    for (;;) {
        if (to > 0x1000000u) return 0;
        to++;
        if (ps2_vif1_stat() & 0x1f000000u) return 1;
    }
}
#endif

/* VIF1 MSKPATH3 0: unmasks path 3 after the transfer (0x257ca0) */
alignas(16) static const unsigned int gs_unmask_path3[4] = {0x06000000u, 0, 0, 0};

/* local -> host transfer of the rectangle sp describes into dst. Bytes = w * h * bpp: whole 8-qword blocks come by
 * VIF1 DMA (DIR 0), the remaining qwords and the partial tail qword (its first `tail` bytes) through the VIF1 FIFO;
 * with a tail the GS height is rounded up (sp's TRXREG is rewritten before the header is sent) and the extra qwords
 * are drained. Path 3 is masked by the header and unmasked at the end; the GS FINISH interrupt is masked meanwhile.
 * psm outside the table: nothing is read. Returns 0, -1 on a timeout (the original's message printf is dropped). */
int sceGsExecStoreImage(void *sp, void *dst)
{
    PS2_FRAME(0x90);
    PS2_STACK(unsigned int[4], scratch, 0);
    unsigned long long *p = (unsigned long long *)sp;
    unsigned long long trxreg = p[8];
    int w = (int)(trxreg & 0xfff), h = (int)((trxreg >> 32) & 0xfff);
    int hr = 0, full = 0, rest = 0, tail = 0, drain = 0, total = 0;   /* a1, s2, s1, s3, s6 */
    int qw = -1;
    switch ((unsigned int)(p[4] >> 24) & 0x3f) {
    case 0x00: case 0x30: {                                   /* 32-bit */
        int b = (w * h) << 2;
        qw = b >> 4; tail = b & 15;
        if (tail) { hr = (h + 3) & 0x1ffc; total = (w * hr) >> 2; }
        break;
    }
    case 0x01: case 0x31: {                                   /* 24-bit */
        int n = w * h, b = n * 2 + n;
        qw = b >> 4; tail = b & 15;
        if (tail) { hr = (h + 15) & 0x1ff0; int m = w * hr; total = (m * 2 + m) >> 4; }
        break;
    }
    case 0x02: case 0x0a: case 0x32: case 0x3a: {             /* 16-bit */
        int b = (w * h) << 1;
        qw = b >> 4; tail = b & 15;
        if (tail) { hr = (h + 7) & ~7; total = (w * hr) >> 3; }
        break;
    }
    case 0x13: case 0x1b: {                                   /* 8-bit (rounded to 8 rows: quirk, only the drain) */
        int b = w * h;
        qw = b >> 4; tail = b & 15;
        if (tail) { hr = (h + 7) & ~7; total = (w * hr) >> 4; }
        break;
    }
    case 0x14: case 0x24: case 0x2c: {                        /* 4-bit */
        int n = w * h;
        qw = n >> 5; tail = (n >> 1) & 15;
        if (tail) { hr = (h + 7) & ~7; total = (w * hr) >> 5; }
        break;
    }
    default:
        break;
    }
    if (qw >= 0) {
        full = qw & ~7;
        rest = qw & 7;
        if (tail) drain = total - full - rest - 1;
        else hr = h;
    }
    if (tail) p[8] = (unsigned long long)(long long)(int)(trxreg & 0xfff) | (unsigned long long)(unsigned int)hr << 32;

    unsigned char *out = (unsigned char *)dst;
    ps2_dma_wait_vif1();
    unsigned int imr = ps2_gs_imr_put(ps2_gs_imr_get() | 0x200);   /* mask the FINISH interrupt */
    ps2_gs_priv_write64(0x12001000u, 2);                            /* CSR: clear FINISH */
    ps2_dma_kick_notadr(1, 0x101, gs_dma_madr(PS2PTR(sp)), 7);
    ps2_dma_wait_vif1();
#ifndef ACNB_SOURCEPORT
    unsigned int to = 0;
    if (!gs_wait_finish(to)) {
        ps2_vif1_fifo_write(gs_unmask_path3);
        return -1;
    }
#endif
    ps2_vif1_set_stat(0x00800000u);                                 /* FDR: FIFO direction VIF1 -> EE */
    ps2_gs_priv_write64(0x12001040u, 1);                            /* BUSDIR: local -> host */
    if (full) {
        ps2_dma_kick_notadr(1, 0x100, gs_dma_madr(PS2PTR(dst)), (unsigned int)full);
        ps2_dma_wait_vif1();
    }
#ifdef ACNB_SOURCEPORT
    /* the FIFO reads become render module deliveries (each continues where the last one stopped); no drain (the
     * module ends the transfer with the rectangle) */
    if (rest) ps2_dma_kick_notadr(1, 0x100, gs_dma_madr(PS2PTR(out + full * 16)), (unsigned int)rest);
    if (tail) {
        ps2_dma_kick_notadr(1, 0x100, gs_dma_madr(PS2PTR(scratch)), 1);
        memcpy(out + (full + rest) * 16, scratch, (size_t)tail);
    }
    (void)drain;
#else
    for (int i = 0; i < rest; i++) {
        if (!gs_wait_fqc(to)) goto fail;
        ps2_vif1_fifo_read(out + (full + i) * 16);
    }
    if (tail) {
        if (!gs_wait_fqc(to)) goto fail;
        ps2_vif1_fifo_read(scratch);
        memcpy(out + (full + rest) * 16, scratch, (size_t)tail);
        for (int i = 0; i < drain; i++) {
            if (!gs_wait_fqc(to)) goto fail;
            ps2_vif1_fifo_read(scratch);
        }
    }
#endif
    ps2_vif1_set_stat(0);
    ps2_gs_priv_write64(0x12001040u, 0);                            /* BUSDIR: host -> local */
    ps2_gs_imr_put(imr);
    ps2_gs_priv_write64(0x12001000u, 2);
    ps2_vif1_fifo_write(gs_unmask_path3);
    return 0;
#ifndef ACNB_SOURCEPORT
fail:
    ps2_gs_priv_write64(0x12001000u, 0x100);                        /* CSR */
    ps2_gs_priv_write64(0x12001040u, 0);
    ps2_gif_vif1_reset();
    return -1;
#endif
}

/* VIF1 codes sceGsResetPath sends through the FIFO (2 qwords at 0x257c80): STCYCL 4/4, STMASK + mask 0, STMOD 0,
 * MSKPATH3 0, BASE 0, OFFSET 0, ITOP 0 */
alignas(16) static const unsigned int gs_vif1_init_codes[8] = {
    0x01000404u, 0x20000000u, 0x00000000u, 0x05000000u,
    0x06000000u, 0x03000000u, 0x02000000u, 0x04000000u,
};

/* VIF1 reset (FBRST.RST), VIF1_ERR = ME0 (DMAtag mismatch errors masked), VU1 reset, the default VIF1 codes,
 * GIF reset (GIF_CTRL.RST). The original's `cfc2 / ctc2 vi28 |= 0x200` (FBRST.RS1 = VU1 reset) is not modelled: the
 * hybrid's VU0 masks CTC2 vi28 to 0x0c0c (a no-op write, runtime/vu0.c) and the source port has no VU1. */
void sceGsResetPath(void)
{
    ps2_vif1_fbrst(1);
    ps2_vif1_err(2);
    ps2_vif1_fifo_write(&gs_vif1_init_codes[0]);
    ps2_vif1_fifo_write(&gs_vif1_init_codes[4]);
    ps2_gif_ctrl(1);
}

/* mode 0: GS reset (CSR.RESET; one 64-bit write, then exactly one CSR read for the revision - the hybrid GS fast-
 * forwards a vblank every 8th CSR poll, hw.c), GParam, GsPutIMR(0xff00) (all GS interrupts masked), the SetVSyncFlag
 * handler removal (dead: vsync_flag is never set), display mode (SetGsCrt). mode 1: CSR.FLUSH only. mode 5: GParam +
 * display mode without the reset. Other modes: nothing. GParam is shared state every flip reads (PutDispEnv's version,
 * SetDefDispEnv, SyncV's inter). */
void sceGsResetGraph(short mode, short inter, short omode, short ffmode)
{
    PS2_FRAME(0x50);
    if (mode == 1) {
        ps2_gs_priv_write64(0x12001000u, 0x100);
        return;
    }
    if (mode == 0) {
        sceGsGParam *g = sceGsGetGParam();
        ps2_gs_priv_write64(0x12001000u, 0x200);
        g->inter = inter;
        unsigned long long csr = ps2_gs_csr();
        g->omode = omode;
        g->version = (short)((csr >> 16) & 0xff);
        ps2_gs_imr_put(0xff00);
        g->ffmode = ffmode != 0;
        if (g->vsync_flag != 0) {
            DisableIntc(2);
            RemoveIntcHandler(2, (int)g->vsync_id);
            g->vsync_id = 0;
            g->vsync_flag = 0;
        }
    } else if (mode == 5) {
        sceGsGParam *g = sceGsGetGParam();
        unsigned long long csr = ps2_gs_csr();
        g->ffmode = ffmode != 0;
        g->inter = inter;
        g->omode = omode;
        g->version = (short)((csr >> 16) & 0xff);
    } else {
        return;
    }
    SetGsCrt((short)(inter & 1), (short)(omode & 0xff), (short)(ffmode & 1));
}

#ifdef ACNB_SOURCEPORT
/* libkernl syscall 2 (display mode: interlace, NTSC / PAL / ..., field / frame): no CRTC to program yet - the
 * phase-3 display hook point (the hybrid kernel ignores it too, kernel.c) */
int SetGsCrt(short inter, short omode, short ffmode)
{
    (void)inter; (void)omode; (void)ffmode;
    return 0;
}
#endif

/* libkernl syscall 0x64: no data cache to write back / invalidate (the hybrid kernel ignores it too) */
void FlushCache(int operation)
{
    (void)operation;
}
