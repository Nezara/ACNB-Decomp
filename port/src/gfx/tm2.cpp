/* src/gfx/tm2.c - TM2 texture block upload. A block (stage / AC part / HUD / loading-screen textures loaded from disc)
 * starts with a 0x20-byte header: +0 byte distance to the next block, +4 CLUT offset (CLUT data at block + off + 0x20),
 * +0x10 a GS TEX0 register image; the pixels follow at +0x20. load_tm2(_TSS) upload the pixels and, for the 4 / 8 bit
 * formats, the CLUT with sceGsSetDefLoadImage / FlushCache / sceGsExecLoadImage, waiting for the GIF path after each
 * upload. DF_ReadTM2Pointer(_TSS) optionally upload and return the next block. The _TSS variants add caller TBP / CBP
 * offsets. Quirks kept (load_tm2): the size class counts half-bytes with factor 8 for every PSM except 8 bit (2) and
 * 4 bit (1), so 16 / 24 bit images get 4x / 2.7x their size: the 0x80000 split threshold and the second half's source
 * offset are wrong for those; the split assumes an even height. */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/globals.h"
#include "acnb/funcs.h"

/* TEX0 image fields, extracted as the asm does */
static inline unsigned long long tm2_tex0(const unsigned int *tm2p) { return *(const unsigned long long *)(tm2p + 4); }
static inline unsigned int tm2_tbp0(const unsigned int *tm2p) { return *(const unsigned short *)((const char *)tm2p + 0x10) & 0x3fff; }
static inline unsigned int tm2_tbw(const unsigned int *tm2p) { return (unsigned int)((tm2_tex0(tm2p) << 44) >> 58); }
static inline unsigned int tm2_psm(const unsigned int *tm2p)
{
    return (unsigned int)(((unsigned long long)*(const unsigned short *)((const char *)tm2p + 0x12) << 54) >> 58);
}
static inline unsigned int tm2_tw(const unsigned int *tm2p)
{
    return (unsigned int)(((unsigned long long)*((const unsigned char *)tm2p + 0x13) << 58) >> 60);
}
static inline unsigned int tm2_th(const unsigned int *tm2p) { return (unsigned int)((tm2_tex0(tm2p) << 30) >> 60); }
static inline unsigned long long tm2_cbp(const unsigned int *tm2p) { return (tm2_tex0(tm2p) << 13) >> 50; }
static inline unsigned int tm2_cpsm(const unsigned int *tm2p)
{
    return (unsigned int)(((unsigned long long)*((const unsigned char *)tm2p + 0x16) << 57) >> 60);
}

/* D2_CHCR.STR || GIF_STAT.APATH (source port: never busy) */
static inline void tm2_wait(void)
{
    while (ps2_gs_path_busy() & 0x12) {}
}

/* one upload: SetDefLoadImage, FlushCache(0), ExecLoadImage, wait (render tag: UPLOAD keyed by the destination TBP) */
static void tm2_upload(void *lp, short dbp, short dbw, short dpsm, short y, short w, short h, void *src)
{
    PS2_RQ_SCOPE(RQ_K_UPLOAD, RQ_OBJID(RQ_OC_TEXTURE, (unsigned int)(unsigned short)dbp & 0x3fff),
                 (unsigned int)(unsigned short)dpsm);   /* render tag (host only) */
    sceGsSetDefLoadImage(lp, dbp, dbw, dpsm, 0, y, w, h);
    FlushCache(0);
    sceGsExecLoadImage(lp, src);
    tm2_wait();
}

/* CLUT size by PSM: 8 bit 16 x 16, 4 bit 8 x 2; 0 = no CLUT */
static inline int tm2_clut_size(unsigned int psm, short *w, short *h)
{
    switch (psm) {
    case 0x13: case 0x1b: *w = 0x10; *h = 0x10; return 1;
    case 0x14: case 0x2c: case 0x24: *w = 8; *h = 2; return 1;
    default: return 0;
    }
}

void load_tm2_TSS(unsigned int *tm2p, int TBP, int CBP)
{
    PS2_FRAME(0x90);
    PS2_STACK(struct_cb63eb70, lp, 0x30);   /* sceGsLoadImage: built and sent by the SDK */
    char *base = (char *)tm2p;
    tm2_upload(&lp, (short)(int)(tm2_tbp0(tm2p) + (unsigned int)TBP), (short)tm2_tbw(tm2p), (short)tm2_psm(tm2p), 0,
               (short)(1 << tm2_tw(tm2p)), (short)(1 << tm2_th(tm2p)), base + 0x20);
    short w, h;
    if (!tm2_clut_size(tm2_psm(tm2p), &w, &h)) return;
    tm2_upload(&lp, (short)(tm2_cbp(tm2p) + (unsigned long long)(long long)CBP), 1, (short)tm2_cpsm(tm2p), 0, w, h,
               base + ((int *)tm2p)[1] + 0x20);
}

void load_tm2(unsigned int *tm2p)
{
    PS2_FRAME(0xb0);
    PS2_STACK(struct_cb63eb70, lp, 0x50);   /* sceGsLoadImage: built and sent by the SDK */
    char *base = (char *)tm2p;
    short width = (short)(1 << tm2_tw(tm2p));
    short height = (short)(1 << tm2_th(tm2p));
    unsigned int psm = tm2_psm(tm2p) & 0xffff;
    int k;
    switch (psm) {
    case 0x2c: case 0x24: case 0x14: k = 1; break;
    case 0x1b: case 0x13: k = 2; break;
    default: k = 8; break;
    }
    int units = (int)((unsigned int)k * ((unsigned int)(int)width * (unsigned int)(int)height));
    if (units < 0x80000) {
        tm2_upload(&lp, (short)tm2_tbp0(tm2p), (short)tm2_tbw(tm2p), (short)psm, 0, width, height, base + 0x20);
    } else {
        short h2 = (short)(height / 2);
        tm2_upload(&lp, (short)tm2_tbp0(tm2p), (short)tm2_tbw(tm2p), (short)psm, 0, width, h2, base + 0x20);
        tm2_upload(&lp, (short)tm2_tbp0(tm2p), (short)tm2_tbw(tm2p), (short)tm2_psm(tm2p), h2, width, h2,
                   base + units / 4 + 0x20);
    }
    short w, h;
    if (!tm2_clut_size(tm2_psm(tm2p), &w, &h)) return;
    tm2_upload(&lp, (short)tm2_cbp(tm2p), 1, (short)tm2_cpsm(tm2p), 0, w, h, base + ((int *)tm2p)[1] + 0x20);
}

char *DF_ReadTM2Pointer_TSS(char *bufp, int mode, int TBP, int CBP)
{
    PS2_FRAME(0x20);
    if (mode) load_tm2_TSS((unsigned int *)bufp, TBP, CBP);
    return bufp + *(int *)bufp;
}

char *DF_ReadTM2Pointer(char *bufp, int mode)
{
    PS2_FRAME(0x20);
    if (mode) load_tm2((unsigned int *)bufp);
    return bufp + *(int *)bufp;
}
