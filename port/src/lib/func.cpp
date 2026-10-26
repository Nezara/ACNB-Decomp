/* src/lib/func.cpp — memory helpers. Sizes: *512b = 64-byte blocks (CopyMemory512b: 16-byte units), *128b and *8b =
 * bytes, *32b = 32-bit words. The quadword parts use lq/sq (or lqc2/sqc2), which ignore the low 4 address bits: with
 * an unaligned pointer they move the enclosing aligned quadwords, while the byte/word tails use the exact address.
 * Callers do pass unaligned buffers (HUD weapon icons), so that is reproduced here. DmaMemCopy copies by DMA through
 * the scratchpad. */
#include <string.h>

#define QALIGN(p) ((unsigned char *)((uintptr_t)(p) & ~(uintptr_t)15))

/* n quadwords from src to dst, each access aligned down like lq/sq; returns nothing, pointers advance by 16 * n */
static void qcopy(void *dstv, const void *srcv, int n)
{
    unsigned char *dst = (unsigned char *)dstv;
    const unsigned char *src = (const unsigned char *)srcv;
    for (int i = 0; i < n; i++) memmove(QALIGN(dst + i * 16), QALIGN(src + i * 16), 16);
}

static void qzero(void *dstv, int n)
{
    unsigned char *dst = (unsigned char *)dstv;
    for (int i = 0; i < n; i++) memset(QALIGN(dst + i * 16), 0, 16);
}

/* Ring search for a free slot: from *topp to max, then from 0; chk(slot) != 0 means free. */
void *DF_GetFreeBuffer(int *topp, void *bufp, int dsiz, int max, int (*chk)(void *))
{
    PS2_FRAME(0x80);
    int i = *topp;
    char *p = (char *)bufp + i * dsiz;
    for (; i < max; i++, p += dsiz)
        if (gcall(chk, p)) break;
    if (i >= max) {
        p = (char *)bufp;
        for (i = 0; i < max; i++, p += dsiz)
            if (gcall(chk, p)) break;
        if (i >= max) return 0;
    }
    *topp = i;
    return p;
}

/* size quadwords (lqc2/sqc2, 4 per loop then the rest) */
void CopyMemory512b(void *p, void *p2, int size)
{
    if (size > 0) qcopy(p, p2, size);
}

/* size bytes: size >> 4 quadwords, then size & 15 bytes */
void CopyMemory128b(void *dest, void *src, int size)
{
    unsigned char *d = (unsigned char *)dest, *s = (unsigned char *)src;
    int nq = size >> 4;
    if (nq > 0) { qcopy(d, s, nq); d += nq * 16; s += nq * 16; }
    for (int i = 0; i < (size & 15); i++) d[i] = s[i];
}

/* size bytes; quadwords only when size >= 64 */
void CopyMemory8b(void *bufp, void *bufp2, int size)
{
    unsigned char *d = (unsigned char *)bufp, *s = (unsigned char *)bufp2;
    if (size & ~0x3f) {
        int nq = size >> 4;
        if (nq > 0) { qcopy(d, s, nq); d += nq * 16; s += nq * 16; }
        size &= 15;
    }
    for (int i = 0; i < size; i++) d[i] = s[i];
}

void CopyMemory(void *dest, void *src, int size) { memcpy(dest, src, (size_t)size); }   /* newlib memcpy */

/* size words; quadwords only when size >= 16 */
void CopyMemory32b(void *bufp, void *bufp2, int size)
{
    unsigned char *d = (unsigned char *)bufp, *s = (unsigned char *)bufp2;
    if (size & ~0xf) {
        int nq = size >> 2;
        if (nq > 0) { qcopy(d, s, nq); d += nq * 16; s += nq * 16; }
        size &= 3;
    }
    memmove(d, s, (size_t)size * 4);
}

void ZeroMemory512b(void *pdest, unsigned int size) { qzero(pdest, (int)size * 4); }

void ZeroMemory32b(void *pdest, unsigned int size)
{
    unsigned char *d = (unsigned char *)pdest;
    if (size & ~0xfu) {
        int nq = (int)(size >> 2);
        qzero(d, nq);
        d += nq * 16;
        size &= 3;
    }
    memset(d, 0, (size_t)size * 4);
}

void ZeroMemory8b(void *pdest, int size)
{
    unsigned char *d = (unsigned char *)pdest;
    int nq = size >> 4;
    if (nq > 0) { qzero(d, nq); d += nq * 16; }
    memset(d, 0, (size_t)(size & 15));
}

void ZeroMemory(void *pdest, int size) { memset(pdest, 0, (size_t)size); }   /* newlib memset */

/* Qwc quadwords from Src to Dest by DMA through the scratchpad (toSPR, then fromSPR) in blocks of 0x400 qwords (the
 * lower 16 KB of the scratchpad); a last block of exactly 0x400 goes the tail path. The scratchpad keeps the last
 * block. */
void DmaMemCopy(unsigned int Dest, unsigned int Src, unsigned int Qwc)
{
    while (!(Qwc < 0x401)) {
        ps2_spr_dma_to(Src, 0x400);
        Src += 0x4000;
        ps2_spr_dma_from(Dest, 0x400);
        Dest += 0x4000;
        Qwc -= 0x400;
    }
    ps2_spr_dma_to(Src, Qwc);
    ps2_spr_dma_from(Dest, Qwc);
}
