/* src/main/emb.cpp - the player emblem: a 128x128 8-bit texture in a TM2-style container ("big", 0x4420 bytes =
 * 0x20 header + 0x4000 pixels + 0x400 CLUT) and a 64x64 point-sampled copy ("small", 0x1420 bytes). The header holds
 * the sizes and a GS TEX0 image (+0x10) built with bit-field read-modify-writes. Boot (__InitApp) and
 * __InitGameData build both from the default CLUT0; the emblem edit / load menus use MakeTm2 / DecompTm2 / Cmp. */
#include "acnb/port.h"
#include <string.h>

/* The game's memcmp (newlib, 0x1168a0): difference of the first differing unsigned bytes, 0 if equal. */
static int emb_memcmp(const void *a, const void *b, unsigned int n)
{
    const unsigned char *p = (const unsigned char *)a, *q = (const unsigned char *)b;
    for (; n; n--, p++, q++)
        if (*p != *q) return (int)*p - (int)*q;
    return 0;
}

/* type 0 = big (128x128, TBW 2), else small (64x64, TBW 1); the image fields are kept in the original's
 * mask / or order on the zeroed header. */
void DF_EMB_MakeTm2Header(void *buff, int type, int tbp, int cbp)
{
    PS2_FRAME(0x60);
    unsigned char *b = (unsigned char *)buff;
    unsigned short *h10 = (unsigned short *)(b + 0x10), *h12 = (unsigned short *)(b + 0x12);
    unsigned short *h18 = (unsigned short *)(b + 0x18);
    unsigned long long *d10 = (unsigned long long *)(b + 0x10);
    memset(buff, 0, 0x20);
    *h10 = (*h10 & 0xc000) | (tbp & 0x3fff);                                               /* TBP0 */
    *d10 = (*d10 & 0xfff8001fffffffffull) | ((unsigned long long)(cbp & 0x3fff) << 37);   /* CBP */
    if (type == 0) {
        ((int *)b)[0] = 0x4420;
        ((int *)b)[1] = 0x4000;
        *d10 = (*d10 & 0xfffffffffff03fffull) | 0x8000;                                    /* TBW 2 */
        b[0x13] = (b[0x13] & 0xc3) | 0x1c;                                                 /* TW 7 */
        *d10 = (*d10 & 0xfffffffc3fffffffull) | 0x1c0000000ull;                            /* TH 7 */
    } else {
        ((int *)b)[0] = 0x1420;
        ((int *)b)[1] = 0x1000;
        *d10 = (*d10 & 0xfffffffffff03fffull) | 0x8000;
        b[0x13] = (b[0x13] & 0xc3) | 0x18;                                                 /* TW 6 */
        *d10 = (*d10 & 0xfffffffc3fffffffull) | 0x180000000ull;                            /* TH 6 */
    }
    *h12 = (*h12 & 0xfc0f) | 0x130;
    b[0x14] = (b[0x14] & 0xfb) | 4;
    b[0x14] = (b[0x14] & 0xe7) | 0;
    b[0x16] = (b[0x16] & 0x87) | 0;
    b[0x16] = (b[0x16] & 0x7f) | 0;
    b[0x17] = (b[0x17] & 0xe0) | 0;
    b[0x17] = (b[0x17] & 0x1f) | 0x80;
    b[0x18] = (b[0x18] & 0xdf) | 0x20;
    *h18 = (*h18 & 0xfe3f) | 0x40;
    b[0x19] = (b[0x19] & 0xfd) | 0;
}

/* 128 -> 64 point sampling: every other pixel of every other row; then the CLUT. */
void DF_EMB_MakeSmallEmblem(void *dest, void *src)
{
    PS2_FRAME(0x30);
    DF_EMB_MakeTm2Header(dest, 1, 0x1e30, 0x3d5c);
    char *ptrd = (char *)dest + 0x20;
    char *ptrs = (char *)src + 0x20;
    for (int yy = 0; yy < 0x40; yy++) {
        for (int xx = 0; xx < 0x40; xx++) {
            *ptrd++ = *ptrs;
            ptrs += 2;
        }
        ptrs += 0x80;
    }
    memcpy((char *)dest + 0x1020, (char *)src + 0x4020, 0x400);
}

/* 0 when equal (or any pointer is NULL); palette entry 0 is not compared. */
int DF_EMB_Cmp(void *tbb, void *cbb, void *tm2)
{
    PS2_FRAME(0x30);
    if (tbb == 0 || cbb == 0 || tm2 == 0) return 0;
    int ret1 = emb_memcmp(tbb, (char *)tm2 + 0x20, 0x4000);
    int ret2 = emb_memcmp((char *)cbb + 4, (char *)tm2 + 0x4024, 0x3fc);
    return ret1 | ret2;
}

void DF_EMB_DecompTm2(void *tbb, void *cbb, void *tm2)
{
    PS2_FRAME(0x30);
    memcpy(tbb, (char *)tm2 + 0x20, 0x4000);
    memcpy(cbb, (char *)tm2 + 0x4020, 0x400);
}

/* Palette entry 0 is cleared (transparent). */
void DF_EMB_MakeTm2(void *tm2, void *tbb, void *cbb)
{
    PS2_FRAME(0x30);
    char *t = (char *)tm2;
    memcpy(t + 0x20, tbb, 0x4000);
    memcpy(t + 0x4020, cbb, 0x400);
    t[0x4020] = 0;
    t[0x4021] = 0;
    t[0x4022] = 0;
    t[0x4023] = 0;
}

void DF_EMB_Init(void *buff, void *sbuff)
{
    PS2_FRAME(0x30);
    DF_EMB_MakeTm2Header(buff, 0, 0x2100, 0x3d5c);
    memset((char *)buff + 0x20, 0, 0x4000);
    memcpy((char *)buff + 0x4020, CLUT0, 0x400);
    DF_EMB_MakeSmallEmblem(sbuff, buff);
}

/* Default CLUT from file 0x16 (a 0x4800-byte TM2 read onto the stack; its CLUT is at +0x4020). */
void DF_EMB_InitCLUT(void)
{
    PS2_FRAME(0x4810);
    PS2_STACK(char[0x4800], buff, 0x10);
    File_Read(0x16, -1, buff, 1);
    memcpy(CLUT0, buff + 0x4020, 0x400);
}
