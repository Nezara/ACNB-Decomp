/* src/lib/vramalloc.cpp - page-granular VRAM allocator used by texmng (whole file). The pages are covered by a doubly
 * linked list of _VRAMBLOCKs behind BlockListHead (nSize 1, so it is never free / merged); a free block has a
 * negative nSize. Blocks come from a pool singly linked through pNext, carved from the caller's work buffer.
 * Alloc is first fit and splits the new block off the front of the free one; Free merges with the previous free
 * block (the previous node survives) and then with the next one (the next node survives and takes the address). */
#include "acnb/port.h"
#include <string.h>

/* pool pop / push (inlined in the original) */
static _VRAMBLOCK *vramalloc_pop(_VRAMALLOC *pVramAlloc)
{
    _VRAMBLOCK *b = pVramAlloc->pFreeBlock;
    if (!b) return 0;
    pVramAlloc->nFreeBlockNum--;
    pVramAlloc->pFreeBlock = b->pNext;
    return b;
}

static void vramalloc_push(_VRAMALLOC *pVramAlloc, _VRAMBLOCK *b)
{
    pVramAlloc->nFreeBlockNum++;
    b->pPrev = 0;
    b->nAddr = 0;
    b->nSize = 0;
    b->nUserData = 0;
    b->pNext = pVramAlloc->pFreeBlock;
    pVramAlloc->pFreeBlock = b;
}

void VramAlloc_Free(_VRAMALLOC *pVramAlloc, _VRAMBLOCK *pBlock)
{
    if (!pBlock || pBlock->nSize <= 0) return;
    pVramAlloc->nFreePageNum += pBlock->nSize;
    pBlock->nSize = -pBlock->nSize;

    _VRAMBLOCK *prev = pBlock->pPrev;
    if (prev && prev->nSize < 0) {   /* merge into the previous free block */
        prev->nSize += pBlock->nSize;
        pBlock->pPrev->pNext = pBlock->pNext;
        if (pBlock->pNext) pBlock->pNext->pPrev = pBlock->pPrev;
        vramalloc_push(pVramAlloc, pBlock);
        pBlock = prev;
    }

    _VRAMBLOCK *next = pBlock->pNext;
    if (next && next->nSize < 0) {   /* merge into the next free block, which takes this block's address */
        next->nSize += pBlock->nSize;
        pBlock->pNext->nAddr = pBlock->nAddr;
        pBlock->pNext->pPrev = pBlock->pPrev;
        if (pBlock->pPrev) pBlock->pPrev->pNext = pBlock->pNext;
        vramalloc_push(pVramAlloc, pBlock);
    }
}

_VRAMBLOCK *VramAlloc_Alloc(_VRAMALLOC *pVramAlloc, int pagesize)
{
    if (pagesize <= 0) return 0;
    for (_VRAMBLOCK *p = pVramAlloc->BlockListHead.pNext; p; p = p->pNext) {
        if (p->nSize >= 0 || -p->nSize < pagesize) continue;
        int addr = p->nAddr;
        _VRAMBLOCK *res = p;
        if (pagesize < -p->nSize) {   /* split: the new block goes in front of the rest */
            _VRAMBLOCK *n = vramalloc_pop(pVramAlloc);
            if (!n) return 0;
            n->pNext = p;
            n->pPrev = p->pPrev;
            if (p->pPrev) p->pPrev->pNext = n;
            p->pPrev = n;
            p->nSize += pagesize;
            p->nAddr += pagesize;
            res = n;
        }
        pVramAlloc->nFreePageNum -= pagesize;
        res->nSize = pagesize;
        res->nAddr = addr;
        return res;
    }
    return 0;
}

void VramAlloc_Destroy(void) {}

int VramAlloc_GetWorkBuffSize(int pagesize) { return pagesize * (int)sizeof(_VRAMBLOCK); }

void VramAlloc_Create(_VRAMALLOC *pVramAlloc, char *pbuff, int pageaddr, int pagesize)
{
    memset(pVramAlloc, 0, sizeof(_VRAMALLOC));
    pVramAlloc->nMaxPageNum = pagesize;
    pVramAlloc->nFreePageNum = pagesize;
    pVramAlloc->nPageAddr = pageaddr;
    pVramAlloc->nMaxBlockNum = pagesize;
    pVramAlloc->nFreeBlockNum = pagesize;
    pVramAlloc->pFreeBlock = (_VRAMBLOCK *)pbuff;
    memset(pVramAlloc->pFreeBlock, 0, pVramAlloc->nMaxBlockNum * sizeof(_VRAMBLOCK));
    _VRAMBLOCK *b = pVramAlloc->pFreeBlock;
    for (int i = 0; i < pVramAlloc->nMaxBlockNum - 1; i++, b++) b->pNext = b + 1;

    /* one block covering all pages; no null check (pagesize 0 would write to PS2 address 0) */
    _VRAMBLOCK *n = vramalloc_pop(pVramAlloc);
    if (!n) n = (_VRAMBLOCK *)PS2_NULL_MEM();
    n->nAddr = pVramAlloc->nPageAddr;
    n->nSize = -pVramAlloc->nMaxPageNum;
    n->pNext = 0;
    n->pPrev = &pVramAlloc->BlockListHead;
    pVramAlloc->BlockListHead.pNext = n;
    pVramAlloc->BlockListHead.nSize = 1;
}
