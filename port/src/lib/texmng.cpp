/* src/lib/texmng.cpp - texture / CLUT cache for VRAM pages (whole file; only the menus use it: OLMENU's MenuTexMng
 * through menu/mtex, DF_M_AC_DrawShadow). Textures and CLUTs are registered into 256-entry tables; a handle is the
 * entry index | the entry's use counter << 16 (bumped by UnRegister, so stale handles fail). Standby* makes an entry
 * resident right before a draw and patches the caller's GS TEX0: textures get pages from the VramAlloc list
 * (evicting the cheapest blocks not used this frame when it is full) and are uploaded with __LoadImage; CLUTs live
 * in the last uClutPageNum pages, 8 slots per page (the oldest slot is reused). Begin / End bracket a frame: the
 * outermost Begin ticks nTime, the outermost End flushes the GS texture cache.
 * Work buffer (Create): 256 tex entries, 256 clut entries, the clut slot map (u16 entry index, 0xffff = free), then
 * the VramAlloc block pool. */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/local/src/lib/texmng.h"
#include <string.h>

/* handle -> entry, or 0 (inlined in the original) */
static tagTEXMNG_TEXENTRY *texmng_tex_lookup(struct_ed4039f4 *pTexMng, unsigned int handle)
{
    unsigned int index = handle & 0xffff;
    if (index >= 0x100) return 0;
    tagTEXMNG_TEXENTRY *e = &pTexMng->pTexEntry[index];
    if (!e->use || (handle >> 16) != e->usecount) return 0;
    return e;
}

static tagTEXMNG_CLUTENTRY *texmng_clut_lookup(struct_ed4039f4 *pTexMng, unsigned int handle)
{
    unsigned int index = handle & 0xffff;
    if (index >= 0x100) return 0;
    tagTEXMNG_CLUTENTRY *e = &pTexMng->pClutEntry[index];
    if (!e->use || (handle >> 16) != e->usecount) return 0;
    return e;
}

tagTEXMNG_RESULT TexMng_InvalidateAllClutCache(struct_ed4039f4 *pTexMng)
{
    for (int i = 0; i < 0x100; i++) {
        tagTEXMNG_CLUTENTRY *e = &pTexMng->pClutEntry[i];
        if (e->use && e->vramaddr >= 0) {
            pTexMng->pClutHandleMap[e->vramaddr] = 0xffff;
            e->vramaddr = -1;
        }
    }
    return TEXMNG_RESULT_OK;
}

tagTEXMNG_RESULT TexMng_InvalidateAllTexCache(struct_ed4039f4 *pTexMng)
{
    for (int i = 0; i < 0x100; i++) {
        tagTEXMNG_TEXENTRY *e = &pTexMng->pTexEntry[i];
        if (e->use && e->pvram) {
            VramAlloc_Free(&pTexMng->VramAlloc, e->pvram);
            e->pvram = 0;
        }
    }
    return TEXMNG_RESULT_OK;
}

int TexMng_IsClutFormat(int psm)
{
    switch (psm) {
    case 0x24: case 0x2c: case 0x14: case 0x1b: case 0x13: return 1;
    default: return 0;
    }
}

tagTEXMNG_RESULT TexMng_End(struct_ed4039f4 *pTexMng)
{
    PS2_FRAME(0x30);
    if (pTexMng->uNestedBeginCount == 0) return TEXMNG_RESULT_ERROR;
    if (--pTexMng->uNestedBeginCount == 0) {
        /* GIF packet (A+D, 1 register): TEXFLUSH; read by the DMA from PS2 memory */
        PS2_STACK(struct_1b9936ce, pkt, 0x10);
        pkt.giftag[0] = 0x1000000000008001ull;   /* NLOOP 1, EOP, PACKED, NREG 1 */
        pkt.giftag[1] = 0xe;                     /* A+D */
        pkt.anydata = 0;
        pkt.regtexflush = 0x3f;                  /* TEXFLUSH */
        FlushCache(0);
        sceDmaSendN(sceDmaGetChan(2), &pkt, 2);
        sceGsSyncPath(0, 0);
    }
    return TEXMNG_RESULT_OK;
}

tagTEXMNG_RESULT TexMng_Begin(struct_ed4039f4 *pTexMng)
{
    if (++pTexMng->uNestedBeginCount == 1) {
        pTexMng->nTime++;
        if (pTexMng->nTime <= 0) {   /* nTime wrapped: forget every usetime */
            pTexMng->nTime = 1;
            for (int i = 0; i < 0x100; i++) {
                tagTEXMNG_TEXENTRY *e = &pTexMng->pTexEntry[i];
                if (e->use && e->pvram) {
                    VramAlloc_Free(&pTexMng->VramAlloc, e->pvram);
                    e->pvram = 0;
                }
            }
            for (int i = 0; i < 0x100; i++) {
                tagTEXMNG_CLUTENTRY *e = &pTexMng->pClutEntry[i];
                if (e->use && e->vramaddr >= 0) {
                    pTexMng->pClutHandleMap[e->vramaddr] = 0xffff;
                    e->vramaddr = -1;
                }
            }
        }
    }
    return TEXMNG_RESULT_OK;
}

tagTEXMNG_RESULT TexMng_StandbyClut(struct_ed4039f4 *pTexMng, unsigned int handle, unsigned long long *pTex0)
{
    PS2_FRAME(0x50);
    unsigned short index = handle & 0xffff;
    tagTEXMNG_CLUTENTRY *e = texmng_clut_lookup(pTexMng, handle);
    if (!e) return TEXMNG_RESULT_INVALIDHANDLE;
    e->usetime = pTexMng->nTime;

    unsigned char *b = (unsigned char *)pTex0;
    int clutindex = e->vramaddr;
    int cbp;
    if (clutindex < 0) {
        clutindex = __TexMng_ReleaseOldestClutOncache(pTexMng);
        if (clutindex < 0) return TEXMNG_RESULT_LOADFAIL;
        pTexMng->pClutHandleMap[clutindex] = index;
        e->vramaddr = clutindex;
        cbp = (int)((pTexMng->uPageBegin + pTexMng->uPageNum - pTexMng->uClutPageNum + clutindex / 8) << 5) +
              (clutindex % 8) * 4;
        switch (e->ClutInfo.format) {
        case 0x2c: case 0x24: case 0x14:
            __LoadImage(e->ClutInfo.cformat, cbp, 8, 2, (u_long128 *)e->ClutInfo.pdata);
            break;
        case 0x1b: case 0x13:
            __LoadImage(e->ClutInfo.cformat, cbp, 0x10, 0x10, (u_long128 *)e->ClutInfo.pdata);
            break;
        }
        b[7] = (b[7] & 0x1f) | 0x40;   /* CLD 2: load CBP0 */
    } else {
        cbp = (int)((pTexMng->uPageBegin + pTexMng->uPageNum - pTexMng->uClutPageNum + clutindex / 8) << 5) +
              (clutindex % 8) * 4;
        b[7] = (b[7] & 0x1f) | 0x80;   /* CLD 4: load when CBP0 differs */
    }
    *pTex0 = (*pTex0 & 0xfff8001fffffffffull) | ((unsigned long long)(cbp & 0x3fff) << 37);   /* CBP */
    b[6] = (b[6] & 0x87) | ((e->ClutInfo.cformat & 0xf) << 3);                                /* CPSM */
    b[6] = b[6] & 0x7f;                                                                       /* CSM 0 */
    b[7] = b[7] & 0xe0;                                                                       /* CSA 0 */
    return TEXMNG_RESULT_OK;
}

/* a free clut slot, or the one used longest ago (not this frame); -1 if none */
static int __TexMng_ReleaseOldestClutOncache(struct_ed4039f4 *pTexMng)
{
    unsigned int n = pTexMng->uClutPageNum << 3;
    int best = -1;
    int bestage = 0;
    for (unsigned int i = 0; i < n; i++) {
        unsigned short ix = pTexMng->pClutHandleMap[i];
        if (ix == 0xffff) return (int)i;
        int age = pTexMng->nTime - pTexMng->pClutEntry[ix].usetime;
        if (bestage < age) {
            bestage = age;
            best = (int)i;
        }
    }
    if (best < 0) return -1;
    pTexMng->pClutEntry[pTexMng->pClutHandleMap[best]].vramaddr = -1;
    pTexMng->pClutHandleMap[best] = 0xffff;
    return best;
}

/* ceil(log2(x)) for x >= 1 (TEX0 TW / TH) */
static int texmng_log2(int x)
{
    unsigned int n = (unsigned int)(x + (x - 1));
    int r = -1;
    while (n) {
        n >>= 1;
        r++;
    }
    return r;
}

tagTEXMNG_RESULT TexMng_StandbyTexture(struct_ed4039f4 *pTexMng, unsigned int handle, unsigned long long *pTex0)
{
    PS2_FRAME(0x50);
    tagTEXMNG_TEXENTRY *e = texmng_tex_lookup(pTexMng, handle);
    if (!e) return TEXMNG_RESULT_INVALIDHANDLE;
    e->usetime = pTexMng->nTime;

    int tbp;
    if (e->pvram) {
        tbp = e->pvram->nAddr << 5;
    } else {
        int size = TexMng_GetPageSize(e->TexInfo.format, e->TexInfo.width, e->TexInfo.height);
        if (size <= 0) return TEXMNG_RESULT_LOADFAIL;
        e->pvram = VramAlloc_Alloc(&pTexMng->VramAlloc, size);
        if (!e->pvram) {
            if (!__TexMng__MakeVramSpace(pTexMng, size)) return TEXMNG_RESULT_VRAMOVER;
            e->pvram = VramAlloc_Alloc(&pTexMng->VramAlloc, size);
            if (!e->pvram) return TEXMNG_RESULT_LOADFAIL;
        }
        e->pvram->nUserData = (int)PS2PTR(e);
        tbp = e->pvram->nAddr << 5;
        if (e->TexInfo.pdata)
            __LoadImage(e->TexInfo.format, tbp, e->TexInfo.width, e->TexInfo.height, (u_long128 *)e->TexInfo.pdata);
    }

    int tw = texmng_log2(e->TexInfo.width);
    int th = texmng_log2(e->TexInfo.height);
    unsigned char *b = (unsigned char *)pTex0;
    unsigned short *h = (unsigned short *)pTex0;
    h[0] = (h[0] & 0xc000) | (tbp & 0x3fff);                                               /* TBP0 */
    b[3] = (b[3] & 0xc3) | ((tw & 0xf) << 2);                                                /* TW */
    *pTex0 = (*pTex0 & 0xfffffffc3fffffffull) | ((unsigned long long)(th & 0xf) << 30);      /* TH */
    int tbw = (e->TexInfo.width + 63) / 64;
    *pTex0 = (*pTex0 & 0xfffffffffff03fffull) | ((unsigned long long)(tbw & 0x3f) << 14);    /* TBW */
    h[1] = (h[1] & 0xfc0f) | ((e->TexInfo.format & 0x3f) << 4);                              /* PSM */
    return TEXMNG_RESULT_OK;
}

/* Frees the run of blocks (from some block on, enough pages for size) whose textures score lowest (priority +
 * 10 * their page size - frames since use); runs containing a block used this frame are skipped. 0 if none. */
static int __TexMng__MakeVramSpace(struct_ed4039f4 *pTexMng, int size)
{
    int bestscore = 0x7fffffff;
    _VRAMBLOCK *best = 0;
    for (_VRAMBLOCK *p = pTexMng->VramAlloc.BlockListHead.pNext; p; p = p->pNext) {
        int need = size;
        int score = 0;
        _VRAMBLOCK *q = p;
        bool skip = false;
        while (need > 0) {
            if (!q) goto evict;   /* ran off the list: stop searching */
            if (q->nSize < 0) {
                need += q->nSize;
            } else {
                tagTEXMNG_TEXENTRY *t = (tagTEXMNG_TEXENTRY *)(uintptr_t)(unsigned int)q->nUserData;
                /* usetime is read before the null test (a null owner reads PS2 address 0x14) */
                int usetime = t ? t->usetime : ((const int *)PS2_NULL_MEM())[5];
                if (usetime == pTexMng->nTime) {   /* used this frame: resume after it */
                    p = q;
                    skip = true;
                    break;
                }
                if (t)
                    score += t->TexInfo.priority +
                             (TexMng_GetPageSize(t->TexInfo.format, t->TexInfo.width, t->TexInfo.height) * 10 -
                              (pTexMng->nTime - usetime));
                need -= q->nSize;
            }
            q = q->pNext;
        }
        if (skip) continue;
        if (score < bestscore) {
            bestscore = score;
            best = p;
        }
    }
evict:
    if (!best) return 0;
    if (best->nSize < 0) {
        size += best->nSize;
        best = best->pNext;
    }
    while (size > 0) {
        _VRAMBLOCK *next = best->pNext;
        if (next && next->nSize < 0) {
            size += next->nSize;
            next = next->pNext;
        }
        size -= best->nSize;
        tagTEXMNG_TEXENTRY *t = (tagTEXMNG_TEXENTRY *)(uintptr_t)(unsigned int)best->nUserData;
        if (!t) t = (tagTEXMNG_TEXENTRY *)PS2_NULL_MEM();   /* the original stores to PS2 address 0x18 */
        t->pvram = 0;
        VramAlloc_Free(&pTexMng->VramAlloc, best);
        best = next;
    }
    return 1;
}

int TexMng_GetPageSize(int psm, int width, int height)
{
    int wunit, hunit;
    switch (psm) {
    case 0x14: wunit = 0x80; hunit = 0x80; break;
    case 0x13: wunit = 0x80; hunit = 0x40; break;
    case 0xa: case 2: wunit = 0x40; hunit = 0x40; break;
    case 0x2c: case 0x24: case 0x1b: case 0: wunit = 0x40; hunit = 0x20; break;
    default: return 0;
    }
    return ((width + wunit - 1) / wunit) * ((height + hunit - 1) / hunit);
}

/* GS upload of a w x h image at tbp in chunks of at most 0x7ffc0 bytes (GS-upload code, phase 2) */
static void __LoadImage(int psm, int tbp, int w, int h, u_long128 *pImage)
{
    PS2_FRAME(0x110);
    PS2_STACK(unsigned int, pImageSlot, 0xac);   /* the original spills pImage there */
    pImageSlot = PS2PTR(pImage);
    (void)pImageSlot;
    int bpl;   /* bytes per line */
    switch (psm) {
    case 0x14: case 0x2c: case 0x24: bpl = w / 2; break;
    case 0x13: case 0x1b: bpl = w; break;
    case 0xa: case 2: case 0x3a: case 0x32: bpl = w << 1; break;
    case 1: case 0x31: bpl = w * 3; break;
    case 0: case 0x30: bpl = w << 2; break;
    default: return;
    }
    int tbw = (w + 63) / 64;
    int rows = ps2_div32(0x7ffc0, bpl);
    PS2_RQ_SCOPE(RQ_K_UPLOAD, RQ_OBJID(RQ_OC_TEXTURE, tbp), psm);   /* render tag (host only) */
    PS2_STACK(struct_cb63eb70, li, 0xb0);   /* sceGsLoadImage: built and read by the interpreted SDK */
    for (int y = 0; y < h; y += rows) {
        if (h < y + rows) rows = h - y;
        sceGsSetDefLoadImage(&li, (short)tbp, (short)tbw, (short)psm, 0, (short)y, (short)w, (short)rows);
        FlushCache(0);
        sceGsExecLoadImage(&li, (char *)pImage + bpl * y);
        sceGsSyncPath(0, 0);
    }
}

tagTEXMNG_RESULT TexMng_UnRegisterClut(struct_ed4039f4 *pTexMng, unsigned int handle)
{
    if (handle == 0) return TEXMNG_RESULT_OK;
    tagTEXMNG_CLUTENTRY *e = texmng_clut_lookup(pTexMng, handle);
    if (!e) return TEXMNG_RESULT_INVALIDHANDLE;
    if (e->vramaddr >= 0) pTexMng->pClutHandleMap[e->vramaddr] = 0xffff;   /* vramaddr itself is kept */
    e->use = 0;
    e->usecount++;
    return TEXMNG_RESULT_OK;
}

tagTEXMNG_RESULT TexMng_RegisterClut(struct_ed4039f4 *pTexMng, tagTEXMNG_CLUTINFO *pClutInfo, unsigned int *handle)
{
    for (int i = 0; i < 0x100; i++) {
        tagTEXMNG_CLUTENTRY *e = &pTexMng->pClutEntry[i];
        if (e->use) continue;
        e->ClutInfo = *pClutInfo;
        e->usetime = 0;
        e->use = 1;
        e->vramaddr = -1;
        if (e->usecount == 0) e->usecount = 0xff;
        *handle = i | e->usecount << 16;
        return TEXMNG_RESULT_OK;
    }
    *handle = 0;
    return TEXMNG_RESULT_ENTRYOVER;
}

tagTEXMNG_RESULT TexMng_UnRegisterTex(struct_ed4039f4 *pTexMng, unsigned int handle)
{
    if (handle == 0) return TEXMNG_RESULT_OK;
    tagTEXMNG_TEXENTRY *e = texmng_tex_lookup(pTexMng, handle);
    if (!e) return TEXMNG_RESULT_INVALIDHANDLE;
    e->use = 0;
    e->usecount++;
    if (e->pvram) {
        VramAlloc_Free(&pTexMng->VramAlloc, e->pvram);
        e->pvram = 0;
    }
    return TEXMNG_RESULT_OK;
}

tagTEXMNG_RESULT TexMng_RegisterTex(struct_ed4039f4 *pTexMng, tagTEXMNG_TEXINFO *pTexInfo, unsigned int *handle)
{
    switch (pTexInfo->format) {
    case 0x14: case 0x13: case 0xa: case 2: case 1: case 0: break;
    default: return TEXMNG_RESULT_INVALIDFORMAT;
    }
    if (pTexInfo->width <= 0 || pTexInfo->height <= 0) return TEXMNG_RESULT_INVALIDFORMAT;
    for (int i = 0; i < 0x100; i++) {
        tagTEXMNG_TEXENTRY *e = &pTexMng->pTexEntry[i];
        if (e->use) continue;
        e->TexInfo = *pTexInfo;
        e->usetime = 0;
        e->use = 1;
        e->pvram = 0;
        if (e->usecount == 0) e->usecount = 1;
        *handle = i | e->usecount << 16;
        return TEXMNG_RESULT_OK;
    }
    *handle = 0;
    return TEXMNG_RESULT_ENTRYOVER;
}

/* the clut pages are taken from the end of the area; at least one page stays for textures */
int TexMng_GetWorkBuffSize(int pagenum, int clutpagenum)
{
    if (clutpagenum >= pagenum) clutpagenum = pagenum - 1;
    return clutpagenum * 0x10 + 0x3c00 + VramAlloc_GetWorkBuffSize(pagenum - clutpagenum);
}

char *TexMng_Destroy(struct_ed4039f4 *pTexMng)
{
    VramAlloc_Destroy();
    return pTexMng->pWorkBuff;
}

void TexMng_Create(struct_ed4039f4 *pTexMng, char *pbuff, int pagebegin, int pagenum, int clutpagenum)
{
    memset(pTexMng, 0, sizeof(struct_ed4039f4));
    if (clutpagenum >= pagenum) clutpagenum = pagenum - 1;
    pTexMng->uPageBegin = pagebegin;
    pTexMng->uPageNum = pagenum;
    pTexMng->uClutPageNum = clutpagenum;
    pTexMng->pWorkBuff = pbuff;
    pTexMng->pTexEntry = (tagTEXMNG_TEXENTRY *)pbuff;
    pTexMng->pClutEntry = (tagTEXMNG_CLUTENTRY *)(pbuff + 0x2000);
    pTexMng->pClutHandleMap = (unsigned short *)(pbuff + 0x3c00);
    VramAlloc_Create(&pTexMng->VramAlloc, pbuff + 0x3c00 + pTexMng->uClutPageNum * 0x10, pTexMng->uPageBegin,
                     pagenum - clutpagenum);
    VramAlloc_GetWorkBuffSize(pagenum - clutpagenum);   /* result unused */
    memset(pTexMng->pTexEntry, 0, 0x100 * sizeof(tagTEXMNG_TEXENTRY));
    memset(pTexMng->pClutEntry, 0, 0x100 * sizeof(tagTEXMNG_CLUTENTRY));
    for (unsigned int i = 0; i < pTexMng->uClutPageNum << 3; i++) pTexMng->pClutHandleMap[i] = 0xffff;
}
