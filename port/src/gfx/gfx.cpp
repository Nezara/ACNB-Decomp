/* src/gfx/gfx.cpp — DMA/VIF packet helpers ("GL_*"): every function appends to the packet at *gpp.
 * GL_LoadVector only references the vector (DMA ref), GL_LoadVector2 / GL_LoadMatrix2 copy it into the packet.
 * GL_LoadMatrices is a gfx.h inline (one
 * outlined copy per file that uses it). */
#include "acnb/sdk.h"
#include "acnb/vu0m.h"
#include "acnb/gif.h"

/* Point through the matrix its callers preload into VU0 vf1-vf4 (inline asm elsewhere): *svp = ftoi4(p / p.w),
 * *qp = vf31 with w = 1 / p.w. Works on the live VU0 registers so interpreted callers see the same state. */
void gfxvu0_RotTransPers(int *svp, float *qp, float *vp)
{
    auto *v = VU0();
    float x = vp[0], y = vp[1], z = vp[2], w = vp[3], r[4];
    for (int i = 0; i < 4; i++) {
        float a = v->vf[1].f[i] * x;
        a = a + v->vf[2].f[i] * y;
        a = a + v->vf[3].f[i] * z;
        v->acc[i] = a;
        r[i] = a + v->vf[4].f[i] * w;
    }
    v->q = ps2_div(1.0f, r[3]);
    for (int i = 0; i < 4; i++) v->vf[26].f[i] = r[i] * v->q;
    v->vf[31].f[3] = 1.0f * v->q;
    for (int i = 0; i < 4; i++) v->vf[16].sw[i] = ps2_ftoi(v->vf[26].f[i] * 16.0f);
    memcpy(qp, v->vf[31].f, 16);
    memcpy(svp, v->vf[16].sw, 16);
}

/* DMA cnt 1 + FLUSH */
void GL_Sync(unsigned int **gpp)
{
    unsigned int *p = *gpp;
    p[0] = 0x10000001; p[1] = 0; p[2] = 0; p[3] = 0;
    p[4] = 0; p[5] = 0; p[6] = 0; p[7] = 0x11000000;
    *gpp = p + 8;
}

/* DMA end */
void GL_EndList(unsigned int **gpp)
{
    unsigned int *p = *gpp;
    p[0] = 0x60000000; p[1] = 0; p[2] = 0; p[3] = 0;
    *gpp = p + 4;
}

/* DIRECT GIF A+D packet of num (data, address) register pairs from addr[] */
void GL_SetRegisters(unsigned int **gpp, unsigned long long *addr, int num)
{
    unsigned int *p = *gpp;
    p[0] = (unsigned int)(num + 2) | 0x10000000;   /* DMA cnt */
    p[1] = 0; p[2] = 0; p[3] = 0; p[4] = 0; p[5] = 0; p[6] = 0;
    p[7] = (unsigned int)(num + 1) | 0x51000000;   /* DIRECT */
    unsigned long long *q = (unsigned long long *)(p + 8);
    q[0] = (unsigned long long)((long long)num & 0x7fff) | 0x1000000000008000ULL;   /* GIF tag: EOP, NREG 1 */
    q[1] = 0xe;
    q += 2;
    for (int i = 0; i < num * 2; i++) *q++ = addr[i];
    *gpp = (unsigned int *)q;
}

/* Render-target switch inside the DL: the SDK's default draw environment for a wd x ht psm buffer at tbp (FRAME and
 * SCISSOR overwritten, the rest as sceGsSetDefDrawEnv leaves it) as an A+D packet; with clc, a sprite over the whole
 * target in colour *clc clears it. */
void GL_SetDrawEnv1(unsigned int **gpp, int wd, int ht, int psm, int tbp, int *clc)
{
    PS2_FRAME(0x120);
    /* sceGsSetDefDrawEnv read-modify-writes PRMODECONT / COLCLAMP / DTHE (bit 0 only): the stale upper bits of these
     * slots go into the DL, so env stays at its original stack slot */
    PS2_STACK(struct_c63aaab1, env, 0x90);
    PS2_STACK(int[2][4], xyz, 0x70);
    unsigned int *g = *gpp;
    sceGsSetDefDrawEnv(&env, (short)psm, (short)wd, (short)ht, 0, 0);
    unsigned long long scissor = (unsigned long long)(long long)(wd - 1) << 16 | (unsigned long long)(long long)(ht - 1) << 48;
    memcpy((char *)&env + 0x30, &scissor, 8);
    unsigned long long frame = (unsigned long long)(long long)(tbp >> 5) | (unsigned long long)(long long)(wd / 64) << 16 |
                               (unsigned long long)(long long)psm << 24;
    memcpy((char *)&env, &frame, 8);   /* FBMSK = 0 */
    GL_SetRegisters(&g, (unsigned long long *)&env, 8);
    if (clc) {
        int hw = wd / 2, hh = ht / 2;
        xyz[0][0] = (0x800 - hw) << 4; xyz[0][1] = (0x800 - hh) << 4; xyz[0][2] = 0x10; xyz[0][3] = 0x10;
        xyz[1][0] = (hw + 0x800) << 4; xyz[1][1] = (hh + 0x800) << 4; xyz[1][2] = 0x10; xyz[1][3] = 0x10;
        unsigned int *p = g;
        p[0] = 0x10000006; p[1] = 0; p[2] = 0; p[3] = 0;            /* DMA cnt 6 */
        p[4] = 0; p[5] = 0; p[6] = 0; p[7] = 0x51000005;            /* DIRECT 5 */
        p[8] = 2; p[9] = 0x20034000; p[10] = 0x41; p[11] = 0;       /* GIF NLOOP 2, PRE sprite, NREG 2: RGBAQ XYZ2 */
        memcpy(p + 12, PS2_LQ_SRC(clc), 16);
        memcpy(p + 16, xyz[0], 16);
        memcpy(p + 20, PS2_LQ_SRC(clc), 16);
        memcpy(p + 24, xyz[1], 16);
        g = p + 28;
    }
    *gpp = g;
}

/* single A+D register write */
void GL_SetRegister(unsigned int **gpp, unsigned long long addr, unsigned long long reg)
{
    unsigned int *p = *gpp;
    p[0] = 0x10000003; p[1] = 0; p[2] = 0; p[3] = 0;
    p[4] = 0; p[5] = 0; p[6] = 0; p[7] = 0x51000002;
    unsigned long long *q = (unsigned long long *)(p + 8);
    q[0] = 0x1000000000008001ULL; q[1] = 0xe;
    q[2] = reg; q[3] = addr;
    *gpp = p + 16;
}

/* DMA cnt 5: UNPACK V4-32 x4 to VU base, then the 4 rows copied in */
void GL_LoadMatrix2(unsigned int **gpp, int base, void *mtx)
{
    unsigned int *p = *gpp;
    p[0] = 0x10000005; p[1] = 0; p[2] = 0; p[3] = 0;
    p[4] = 0; p[5] = 0; p[6] = 0; p[7] = (unsigned int)base | 0x6c040000;
    memcpy(p + 8, mtx, 64);
    *gpp = p + 24;
}

/* DMA cnt 2: UNPACK V4-32 x1 to VU base, the vector copied in */
void GL_LoadVector2(unsigned int **gpp, int base, void *vec)
{
    unsigned int *p = *gpp;
    p[0] = 0x10000002; p[1] = 0; p[2] = 0; p[3] = 0;
    p[4] = 0; p[5] = 0; p[6] = 0; p[7] = (unsigned int)base | 0x6c010000;
    memcpy(p + 8, vec, 16);
    *gpp = p + 12;
}

/* gfx.h inline: DMA cnt 1 with UNPACK V4-32 x(4 ct) to VU base, then DMA ref 4 ct qw -> ct matrices at mtx */
void GL_LoadMatrices(unsigned int **gpp, int base, void *mtx, int ct)
{
    if (ct <= 0) return;
    unsigned int *p = *gpp;
    p[0] = 0x10000001; p[1] = 0; p[2] = 0; p[3] = 0;
    p[4] = 0; p[5] = 0; p[6] = 0; p[7] = (unsigned int)base | (unsigned int)ct << 18 | 0x6c000000;
    p[8] = (unsigned int)ct << 2 | 0x30000000; p[9] = PS2PTR(mtx); p[10] = 0; p[11] = 0;
    *gpp = p + 12;
}

/* DMA cnt 1 with UNPACK V4-32 x1 to VU base, then DMA ref 1 qw -> vec (read when the list is sent) */
void GL_LoadVector(unsigned int **gpp, int base, void *vec)
{
    unsigned int *p = *gpp;
    p[0] = 0x10000001; p[1] = 0; p[2] = 0; p[3] = 0;
    p[4] = 0; p[5] = 0; p[6] = 0; p[7] = (unsigned int)base | 0x6c010000;
    p[8] = 0x30000001; p[9] = PS2PTR(vec); p[10] = 0; p[11] = 0;
    *gpp = p + 12;
}

/* Texture upload of a PSMT4 image (wd x ht, TBW at least 2) to tbp plus its 8x2 CT32 CLUT to cbp: per image a CNT
 * qword pair (VIF DIRECT: A+D BITBLTBUF / TRXPOS / TRXREG / TRXDIR + IMAGE tag) and a REF tag streaming the data
 * from RAM. The A+D tags carry no EOP. Fields are not masked: tbp / wd / cbp sign-extend into the high words. */
void GL_LoadImage4b(unsigned int **gpp, void *img, int tbp, int wd, int ht, void *img2, int cbp)
{
    unsigned int *p = *gpp;
    int tbw = (wd + 63) >> 6;
    if (tbw < 2) tbw = 2;
    int size = (int)((unsigned int)wd * (unsigned int)ht) >> 5;
    unsigned long long w64 = (unsigned long long)(long long)tbw;
    p = dma_tag(p, 7, GIF_ID_CNT, 0);
    p[0] = 0; p[1] = 0; p[2] = 0; p[3] = (unsigned int)(size + 6) | 0x51000000; p += 4;   /* VIF DIRECT */
    p = gif_tag(p, 4, 0, 0, 0, 0, 1, GIF_R_AD);
    p = put_ad(p, w64 << 16 | 0x14000000ull | (unsigned long long)(unsigned int)tbp << 32 | w64 << 48 | 0x14ull << 56,
               GS_BITBLTBUF);
    p = put_ad(p, 0, GS_TRXPOS);
    p = put_ad(p, (unsigned long long)(long long)wd | (unsigned long long)(unsigned int)ht << 32, GS_TRXREG);
    p = put_ad(p, 0, GS_TRXDIR);
    p = gif_tag(p, (unsigned int)size & 0x7fff, 1, 0, 0, 2, 0, 0);   /* IMAGE, EOP */
    p = dma_tag(p, (unsigned int)size & 0xffff, GIF_ID_REF, PS2PTR(img));
    /* CLUT: 16 entries as 8 x 2 CT32, buffer width 1 */
    p = dma_tag(p, 7, GIF_ID_CNT, 0);
    p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 0x5100000a; p += 4;
    p = gif_tag(p, 4, 0, 0, 0, 0, 1, GIF_R_AD);
    p = put_ad(p, 0x10000ull | (unsigned long long)(unsigned int)cbp << 32 | 1ull << 48, GS_BITBLTBUF);
    p = put_ad(p, 0, GS_TRXPOS);
    p = put_ad(p, 8 | 2ull << 32, GS_TRXREG);
    p = put_ad(p, 0, GS_TRXDIR);
    p = gif_tag(p, 4, 1, 0, 0, 2, 0, 0);
    p = dma_tag(p, 4, GIF_ID_REF, PS2PTR(img2));
    *gpp = p;
}

/* Texture upload of a PSMCT32 image (wd x ht) to tbp: CNT qword pair with the A+D transfer setup + IMAGE tag, then
 * a REF tag streaming the pixels. No TBW clamp here (unlike GL_LoadImage4b); size = (wd * ht * 4) >> 4 computed as
 * written (shift left, then arithmetic shift right). */
void GL_LoadImage(unsigned int **gpp, void *img, int tbp, int wd, int ht)
{
    unsigned int *p = *gpp;
    int size = (int)(((unsigned int)wd * (unsigned int)ht) << 2) >> 4;
    unsigned long long w64 = (unsigned long long)(long long)((wd + 63) >> 6);
    p = dma_tag(p, 7, GIF_ID_CNT, 0);
    p[0] = 0; p[1] = 0; p[2] = 0; p[3] = (unsigned int)(size + 6) | 0x51000000; p += 4;   /* VIF DIRECT */
    p = gif_tag(p, 4, 0, 0, 0, 0, 1, GIF_R_AD);
    p = put_ad(p, w64 << 16 | (unsigned long long)(unsigned int)tbp << 32 | w64 << 48, GS_BITBLTBUF);
    p = put_ad(p, 0, GS_TRXPOS);
    p = put_ad(p, (unsigned long long)(long long)wd | (unsigned long long)(unsigned int)ht << 32, GS_TRXREG);
    p = put_ad(p, 0, GS_TRXDIR);
    p = gif_tag(p, (unsigned int)size & 0x7fff, 1, 0, 0, 2, 0, 0);   /* IMAGE, EOP */
    p = dma_tag(p, (unsigned int)size & 0xffff, GIF_ID_REF, PS2PTR(img));
    *gpp = p;
}

/* GL_RectUV's float -> unsigned conversion (the compiler's idiom): 16 * uv, values >= 2^31 go through f - 2^31 with
 * bit 31 set; NaN fails the compare and takes the direct cvt.w.s */
static inline unsigned int rectuv_st(float uv)
{
    float f = 16.0f * uv;
    if (2147483648.0f <= f) return (unsigned int)ps2_ftoi(f - 2147483648.0f) | 0x80000000u;
    return (unsigned int)ps2_ftoi(f);
}

/* One textured sprite (DIRECT 5 qwords: GIF tag with PRIM 0x116 | pp, then UV, XYZ2, UV, XYZ2) from rect (x0, y0, x1,
 * y1 in 12.4) and uv (u0, v0, u1, v1 texels, times 16). Original quirk: pp is shifted into the tag as a 32-bit value
 * << 47, so its bits above 16 fall off the 64-bit tag (the only caller passes 0). */
void GL_RectUV(unsigned int **gpp, int *rect, float *uv, unsigned int pp)
{
    unsigned int *p = *gpp;
    unsigned long long tag = (unsigned long long)(pp | 0x116) << 47 | 0x2000400000008002ULL;
    p[0] = 0x10000006; p[1] = 0; p[2] = 0; p[3] = 0;   /* DMA cnt 6 */
    p[4] = 0; p[5] = 0; p[6] = 0; p[7] = 0x51000005;   /* DIRECT 5 */
    p[8] = (unsigned int)tag; p[9] = (unsigned int)(tag >> 32);
    p[10] = 0x43; p[11] = 0;                            /* REGS: UV, XYZ2 (x2) */
    p[12] = rectuv_st(uv[0]);
    p[13] = rectuv_st(uv[1]);
    p[14] = 0; p[15] = 0;
    p[16] = (unsigned int)rect[0]; p[17] = (unsigned int)rect[1]; p[18] = 0xffffff; p[19] = 0x10;
    p[20] = rectuv_st(uv[2]);
    p[21] = rectuv_st(uv[3]);
    p[22] = 0; p[23] = 0;
    p[24] = (unsigned int)rect[2]; p[25] = (unsigned int)rect[3]; p[26] = 0xffffff; p[27] = 0x10;
    *gpp = p + 28;
}
