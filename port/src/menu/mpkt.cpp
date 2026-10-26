/* src/menu/mpkt.cpp — the menu's VIF1 packet buffer (OLMENU). DF_MPKT_Init carves three 0x27000-byte areas
 * (MPktBuff[3]); MPktBuffID selects the one being built, MPktCurrentp is its write pointer and MPktTmpTopp the third
 * area (shadow packets). Every DF_MPKT_Make* appends one DMA-chain element (a tag qword: id << 28 | qwc, address;
 * cnt = 1, ref = 3, call = 5, end = 6, plus its payload) and returns the advanced write pointer: VIF codes, UNPACK
 * V4-32 (copied or referenced), one GS register via DIRECT / A+D, a textured sprite, IMAGE uploads (raw and from a
 * TM2 head). DF_MPKT_Kick hands the chain to DMA channel 1 and flips the buffer.
 * All 13 native. The kicks write only the DMAC registers the original writes, in its order (port.h
 * ps2_dma_kick_tadr / _tadr_madr): Kick = TADR, MADR = -1, CHCR 0x105 and only for a non-empty chain (an empty one
 * neither kicks nor flips); KickImmediate = TADR, CHCR after waiting for VIF1 and GIF. */
#include <string.h>
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
unsigned int *MPktBuff[3];
int MPktBuffID;
unsigned int *MPktTmpTopp;
unsigned int *MPktCurrentp;
#endif

/* n quads from src to dst as lq / sq (both ignore the low four address bits) */
static unsigned int *mpkt_copy_quads(unsigned int *dst, const void *src, unsigned int n)
{
    const unsigned char *s = (const unsigned char *)src;
    while (n--) {
        memcpy((void *)((uintptr_t)dst & ~(uintptr_t)15), (const void *)((uintptr_t)s & ~(uintptr_t)15), 16);
        s += 16;
        dst += 4;
    }
    return dst;
}

/* a cnt tag with qwc 1 followed by four VIF codes */
unsigned int *DF_MPKT_MakeVIFCode(unsigned int *ioDmap, unsigned int inCode1, unsigned int inCode2, unsigned int inCode3,
                                  unsigned int inCode4)
{
    ioDmap[0] = 0x10000001; ioDmap[1] = 0;
    ioDmap[2] = 0; ioDmap[3] = 0;
    ioDmap[4] = inCode1; ioDmap[5] = inCode2;
    ioDmap[6] = inCode3; ioDmap[7] = inCode4;
    return ioDmap + 8;
}

/* cnt tag (qwc 1) carrying UNPACK V4-32, then a ref tag to the caller's data (not copied: it must outlive the DMA) */
unsigned int *DF_MPKT_MakeUnpackRef(unsigned int *ioDmap, unsigned int inBase, unsigned int inQwc, void *inData)
{
    ioDmap[0] = 0x10000001; ioDmap[1] = 0;
    ioDmap[2] = 0; ioDmap[3] = 0;
    ioDmap[4] = 0; ioDmap[5] = 0;
    ioDmap[6] = 0; ioDmap[7] = inBase | inQwc << 16 | 0x6c000000;
    ioDmap[8] = 0x30000000 | inQwc; ioDmap[9] = PS2PTR(inData);
    ioDmap[10] = 0; ioDmap[11] = 0;
    return ioDmap + 12;
}

/* cnt tag (qwc + 1: the UNPACK code's qword counts) with UNPACK V4-32 and inQwc quads copied inline */
unsigned int *DF_MPKT_MakeUnpack(unsigned int *ioDmap, unsigned int inBase, unsigned int inQwc, void *inData)
{
    ioDmap[0] = (inQwc + 1) | 0x10000000; ioDmap[1] = 0;
    ioDmap[2] = 0; ioDmap[3] = 0;
    ioDmap[4] = 0; ioDmap[5] = 0;
    ioDmap[6] = 0; ioDmap[7] = inBase | inQwc << 16 | 0x6c000000;
    return mpkt_copy_quads(ioDmap + 8, inData, inQwc);
}

/* one GS register through VIF DIRECT (2 quads): GIF tag A+D x1, then {data, address} (low 64 bits of each) */
unsigned int *DF_MPKT_MakeGSRegister(unsigned int *ioDmap, unsigned long long inAddr, unsigned long long inData)
{
    ioDmap[0] = 0x10000003; ioDmap[1] = 0;
    ioDmap[2] = 0; ioDmap[3] = 0;
    ioDmap[4] = 0; ioDmap[5] = 0;
    ioDmap[6] = 0; ioDmap[7] = 0x51000002;
    ioDmap[8] = 0x8001; ioDmap[9] = 0x10000000;
    ioDmap[10] = 0xe; ioDmap[11] = 0;
    memcpy(&ioDmap[12], &inData, 8);
    memcpy(&ioDmap[14], &inAddr, 8);
    return ioDmap + 16;
}

unsigned int *DF_MPKT_MakeEnd(unsigned int *ioDmap)
{
    ioDmap[0] = 0x60000000; ioDmap[1] = 0;
    ioDmap[2] = 0; ioDmap[3] = 0;
    return ioDmap + 4;
}

unsigned int *DF_MPKT_MakeCall(unsigned int *ioDmap, void *inData)
{
    ioDmap[0] = 0x50000000; ioDmap[1] = PS2PTR(inData);
    ioDmap[2] = 0; ioDmap[3] = 0;
    return ioDmap + 4;
}

/* One textured sprite through VIF DIRECT 9: TEX1 / TEX0 (A+D), then a PRIM-preset GIF tag (sprite, textured, UV;
 * regs RGBAQ UV XYZF2 UV XYZF2). Screen coordinates get the 0x6c0 / (0x800 - 0xe0) offsets (no g_mdofs); the depth
 * goes into Z unshifted, UV are << 4. */
unsigned int *DF_MPKT_MakeTexSprite(unsigned int *ioDmap, struct struct_13efd7f4 *inTexp, struct stMenuRect *inRectp,
                                    int *inUV, struct struct_76b810ec *inColp, unsigned int inDepth)
{
    int x = inRectp->x, y = inRectp->y, w = inRectp->w, h = inRectp->h;
    unsigned int *p = ioDmap;
    p[0] = 0x1000000a; p[1] = 0;
    p[2] = 0; p[3] = 0;
    p[4] = 0; p[5] = 0;
    p[6] = 0; p[7] = 0x51000009;
    p[8] = 0x8002; p[9] = 0x10000000;
    p[10] = 0xe; p[11] = 0;
    memcpy(&p[12], &inTexp->tex1, 8);
    p[14] = 0x14; p[15] = 0;
    memcpy(&p[16], &inTexp->tex0, 8);
    p[18] = 6; p[19] = 0;
    p[20] = 0x8001; p[21] = 0x50ab4000;
    p[22] = 0x43431; p[23] = 0;
    p[24] = inColp->r; p[25] = inColp->g;
    p[26] = inColp->b; p[27] = inColp->a;
    p[28] = inUV[0] << 4; p[29] = inUV[1] << 4;
    p[30] = 0; p[31] = 0;
    p[32] = (x + 0x6c0) << 4; p[33] = (y << 4) + 0x7200;
    p[34] = inDepth; p[35] = 0;
    p[36] = inUV[2] << 4; p[37] = inUV[3] << 4;
    p[38] = 0; p[39] = 0;
    p[40] = (x + w + 0x6c0) << 4; p[41] = ((y + h) << 4) + 0x7200;
    p[42] = inDepth; p[43] = 0;
    return p + 44;
}

/* MakeTexSprite for a menu texture id; a failed lookup returns 0 (the caller's packet pointer is lost) */
unsigned int *DF_MPKT_MakeTexSpriteI(unsigned int *ioDmap, int texid, struct stMenuRect *inRectp, int *inUV,
                                     struct struct_76b810ec *inColp, unsigned int inDepth)
{
    PS2_FRAME(0x70);
    PS2_STACK(struct struct_13efd7f4, tex, 0x60);
    if (!MenuTex_StandbyTexture(texid, &tex)) return 0;
    return DF_MPKT_MakeTexSprite(ioDmap, &tex, inRectp, inUV, inColp, inDepth);
}

/* One IMAGE upload: cnt tag + VIF DIRECT, the 6-qword GS load-image header (sceGsSetDefLoadImage), then the image
 * data copied inline (n = the image GIF tag's NLOOP quads). */
unsigned int *DF_MPKT_MakeLoadImage(unsigned int *ioDmap, void *inSbp, unsigned int inTbp, unsigned int inPsm, int inX,
                                    int inY, int inW, int inH)
{
    PS2_FRAME(0x50);
    ioDmap[0] = 0x10000007; ioDmap[1] = 0;
    ioDmap[2] = 0; ioDmap[3] = 0;
    ioDmap[4] = 0; ioDmap[5] = 0;
    ioDmap[6] = 0; ioDmap[7] = 0x51000006;
    unsigned int *hdr = ioDmap + 8;
    sceGsSetDefLoadImage(hdr, (short)inTbp, (short)(inW / 64 + (inW % 64 != 0)), (short)inPsm, (short)inX, (short)inY,
                         (short)inW, (short)inH);
    unsigned int n = *(unsigned short *)((unsigned char *)hdr + 0x50) & 0x7fff;
    ioDmap[0] = 0x10000000 | (n + 7);
    ioDmap[7] = 0x51000000 | (n + 6);
    return mpkt_copy_quads(hdr + 0x18, inSbp, n);
}

/* Uploads a TM2 texture (header TEX0 at +0x10, image at +0x20, clut at +0x20 past the offset in +4) as IMAGE
 * slices of at most one page's worth of lines, then its clut (8-bit: 16 x 16 or 256 x 1 by CSM; 4-bit: 8 x 2 or
 * 16 x 1). Unknown PSMs upload slices without advancing (same lines again) and no clut. */
unsigned int *DF_MPKT_MakeTM2LoadImage(unsigned int *ioDmap, void *inTm2p)
{
    PS2_FRAME(0xd0);
    unsigned char *tm2 = (unsigned char *)inTm2p;
    unsigned long long tex0;
    memcpy(&tex0, tm2 + 0x10, 8);
    unsigned char *src = tm2 + 0x20;
    unsigned int tbp = tex0 & 0x3fff;
    unsigned int psm = (tex0 >> 20) & 0x3f;
    int width = (1 << ((tex0 >> 26) & 0xf)) & 0xffff;
    unsigned short rest = (1 << ((tex0 >> 30) & 0xf)) & 0xffff;
    unsigned short lines;
    switch (psm) {
    case 0x14: lines = 0x20000 / width; break;
    case 0x13: case 0x1b: lines = 0x10000 / width; break;
    default: lines = 0x4000 / width; break;
    }
    while (rest) {
        unsigned short n = (int)lines < (int)rest ? lines : rest;
        ioDmap = DF_MPKT_MakeLoadImage(ioDmap, src, tbp, psm, 0, 0, width, n);
        memcpy(&tex0, tm2 + 0x10, 8);
        psm = (tex0 >> 20) & 0x3f;
        unsigned long long tbw64 = ((tex0 >> 14) & 0x3f) << 6;
        if (psm == 0) {
            unsigned long long px = (unsigned long long)n * tbw64;
            tbp += (int)(px >> 6);
            src += (unsigned int)px << 2;
        } else if (psm == 0x13 || psm == 0x1b) {
            tbp += (int)((unsigned long long)n * tbw64 >> 2 >> 6);
            src += (unsigned int)((unsigned long long)n * (tbw64 >> 2)) << 2;
        } else if (psm == 0x14) {
            tbp += (int)((unsigned long long)n * tbw64 >> 3 >> 6);
            src += (unsigned int)((unsigned long long)n * (tbw64 >> 3)) << 2;
        }
        rest = rest - n;
    }
    unsigned char *clut = tm2 + *(int *)(tm2 + 4) + 0x20;
    if (psm != 0x13 && psm != 0x1b && psm != 0x14) return ioDmap;
    memcpy(&tex0, tm2 + 0x10, 8);
    unsigned int cbp = (tex0 >> 37) & 0x3fff;
    unsigned int cpsm = (tm2[0x16] >> 3) & 0xf;
    int csm = tm2[0x16] >> 7;
    if (psm == 0x14) return DF_MPKT_MakeLoadImage(ioDmap, clut, cbp, cpsm, 0, 0, csm ? 0x10 : 8, csm ? 1 : 2);
    return DF_MPKT_MakeLoadImage(ioDmap, clut, cbp, cpsm, 0, 0, csm ? 0x100 : 0x10, csm ? 1 : 0x10);
}

/* start the chain at inDmap on VIF1 at once (m_ac.c's shadow chain); no wait and no kick for NULL */
void DF_MPKT_KickImmediate(unsigned int *inDmap)
{
    if (!inDmap) return;
    ps2_dma_wait_vif1();
    ps2_dma_wait_gif();
    ps2_dma_kick_tadr(1, 0x105, PS2PTR(inDmap));
}

/* send the current buffer's chain (it ends with an END tag) and flip to the other buffer; nothing on an empty chain */
void DF_MPKT_Kick(void)
{
    if (!(MPktBuff[MPktBuffID] < MPktCurrentp)) return;
    FlushCache(0);
    ps2_dma_wait_vif1();
    ps2_dma_kick_tadr_madr(1, 0x105, PS2PTR(MPktBuff[MPktBuffID]), 0xffffffffu);
    MPktBuffID = 1 - MPktBuffID;
    MPktCurrentp = MPktBuff[MPktBuffID];
}

/* carve the three 0x27000-byte areas out of ioBuff (aligned to 0x100); returns the end of the third */
unsigned char *DF_MPKT_Init(unsigned char *ioBuff)
{
    unsigned char *p = (unsigned char *)DF_MFUNC_Aligned(ioBuff, 0x100);
    MPktBuff[0] = (unsigned int *)p;
    MPktBuffID = 0;
    MPktBuff[1] = (unsigned int *)(p + 0x27000);
    MPktBuff[2] = (unsigned int *)(p + 0x4e000);
    MPktCurrentp = MPktBuff[MPktBuffID];
    MPktTmpTopp = MPktBuff[2];
    return p + 0x75000;
}
