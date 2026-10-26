/* acnb/gif.h - GS packet building helpers for decompiled packet builders (phase 2): DMA tags, GIF tags, A+D
 * register writes and the PACKED register qwords. Pure stores into a packet (PS2 memory: scratchpad / RAM packet
 * buffers); each helper writes one full qword (all four words) and returns the pointer to the next qword.
 * Little-endian word stores = the original's sw / sd sequences byte for byte. DMA kicks are not here (port.h
 * ps2_dma_kick stand-in, called by the file that owns the kick). */
#pragma once

/* DMA source-chain tag ids (DMAtag bits 28-30) */
enum {
    GIF_ID_REFE = 0, GIF_ID_CNT = 1, GIF_ID_NEXT = 2, GIF_ID_REF = 3, GIF_ID_REFS = 4, GIF_ID_CALL = 5,
    GIF_ID_RET = 6, GIF_ID_END = 7
};

/* GIF PACKED register descriptors (GIFtag REGS nibbles) */
enum {
    GIF_R_PRIM = 0, GIF_R_RGBAQ = 1, GIF_R_ST = 2, GIF_R_UV = 3, GIF_R_XYZF2 = 4, GIF_R_XYZ2 = 5, GIF_R_AD = 0xe
};

/* GS register addresses used with A+D */
enum {
    GS_TEX0_1 = 0x06, GS_CLAMP_1 = 0x08, GS_TEX1_1 = 0x14, GS_ALPHA_1 = 0x42, GS_BITBLTBUF = 0x50,
    GS_TRXPOS = 0x51, GS_TRXREG = 0x52, GS_TRXDIR = 0x53
};

/* DMA tag {qwc | id << 28, addr, 0, 0} (addr = a PS2 address: PS2PTR) */
static inline unsigned int *dma_tag(unsigned int *p, unsigned int qwc, unsigned int id, unsigned int addr)
{
    p[0] = qwc | id << 28;
    p[1] = addr;
    p[2] = 0;
    p[3] = 0;
    return p + 4;
}

/* general GIF tag: NLOOP, EOP, PRE, PRIM, FLG, NREG; regs = the REGS dword */
static inline unsigned int *gif_tag(unsigned int *p, unsigned int nloop, unsigned int eop, unsigned int pre,
                                    unsigned int prim, unsigned int flg, unsigned int nreg, unsigned long long regs)
{
    unsigned long long d0 = (unsigned long long)nloop | (unsigned long long)eop << 15 |
                            (unsigned long long)pre << 46 | (unsigned long long)prim << 47 |
                            (unsigned long long)flg << 58 | (unsigned long long)nreg << 60;
    p[0] = (unsigned int)d0;
    p[1] = (unsigned int)(d0 >> 32);
    p[2] = (unsigned int)regs;
    p[3] = (unsigned int)(regs >> 32);
    return p + 4;
}

/* A+D GIF tag: NLOOP n, EOP, PACKED, NREG 1 = A+D -> {n | 0x8000, 0x10000000, 0xe, 0} */
static inline unsigned int *gif_tag_ad(unsigned int *p, unsigned int n)
{
    p[0] = n | 0x8000;
    p[1] = 0x10000000;
    p[2] = GIF_R_AD;
    p[3] = 0;
    return p + 4;
}

/* PACKED GIF tag with PRE: {nloop | 0x8000, prim << 15 | 0x4000 | nreg << 28, regs, 0} (nloop is OR-ed as a full
 * word, like the original's `ori`: a negative count smears into the upper bits) */
static inline unsigned int *gif_tag_prim(unsigned int *p, unsigned int nloop, unsigned int prim, unsigned int nreg,
                                         unsigned int regs)
{
    p[0] = nloop | 0x8000;
    p[1] = prim << 15 | 0x4000 | nreg << 28;
    p[2] = regs;
    p[3] = 0;
    return p + 4;
}

/* A+D qword {data, addr} */
static inline unsigned int *put_ad(unsigned int *p, unsigned long long data, unsigned long long addr)
{
    p[0] = (unsigned int)data;
    p[1] = (unsigned int)(data >> 32);
    p[2] = (unsigned int)addr;
    p[3] = (unsigned int)(addr >> 32);
    return p + 4;
}

/* PACKED RGBAQ from colour bytes r, g, b, a (each zero-extended into a word; Q = 0) */
static inline unsigned int *put_rgbaq(unsigned int *p, const unsigned char *c)
{
    p[0] = c[0];
    p[1] = c[1];
    p[2] = c[2];
    p[3] = c[3];
    return p + 4;
}

static inline unsigned int *put_rgbaq(unsigned int *p, unsigned int r, unsigned int g, unsigned int b, unsigned int a)
{
    p[0] = r;
    p[1] = g;
    p[2] = b;
    p[3] = a;
    return p + 4;
}

/* PACKED UV {u, v, 0, 0} (12.4 texel coordinates) */
static inline unsigned int *put_uv(unsigned int *p, unsigned int u, unsigned int v)
{
    p[0] = u;
    p[1] = v;
    p[2] = 0;
    p[3] = 0;
    return p + 4;
}

/* PACKED XYZF2 {x, y, z, 0} (12.4 window coordinates; word 3 = F 0, ADC off) */
static inline unsigned int *put_xyzf2(unsigned int *p, unsigned int x, unsigned int y, unsigned int z)
{
    p[0] = x;
    p[1] = y;
    p[2] = z;
    p[3] = 0;
    return p + 4;
}
