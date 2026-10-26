/* lib/fs/fsspummng.cpp - EE-side allocator for SPU2 sound RAM (whole file). Up to 16 blocks (id 0..15), each a free
 * list of (address, size) ranges and an alloc list; the 16 bookkeeping nodes s_SMemInfo are shared by all blocks and
 * come from the pool list s_SMemMng.info. Links are 16-bit node indices (-1 = nil). AddMemBlock pushes a range on the
 * front of a free list (unsorted, unmerged), Alloc carves first fit from the front of a range (exact fit moves the node,
 * otherwise a pool node takes the front part), Free moves a node back into the free list sorted by address and
 * coalesces it with its neighbours (_SortInsertInfo). Driver: lib/fs/fslibsdsynth.cpp (Init, AddSpu2MemoryBlock,
 * AddBankFromEE2, RemoveBank).
 * Quirks kept: AddMemBlock with an empty pool uses node -1 = s_SMemInfo[-1] (the pool head s_SMemMng.info + 12 bytes;
 * unreachable: 2 blocks, 16 nodes); Free ignores an address it cannot find and releases only the first match; Alloc
 * never aligns; a merged-away node keeps its stale iSpuAddr / iSize. */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/local/lib/fs/fsspummng.h"

/* node by (sign-extended) index; index -1 reaches s_SMemInfo[-1] as in the original */
static inline struct struct_6d45f61a *spu_node(int idx)
{
    return (struct struct_6d45f61a *)((unsigned char *)s_SMemInfo + idx * 0x10);
}

/* take the first node of the pool (num already known non-zero); returns its index */
static int spu_pool_take(void)
{
    struct struct_62dc437e *pool = &s_SMemMng.info;
    pool->num = (short)(pool->num - 1);
    int n = pool->top;
    struct struct_6d45f61a *p = spu_node(n);
    pool->top = p->next;
    if (p->next != -1) spu_node(p->next)->prev = -1;
    p->next = -1;
    return n;
}

/* push node k on the front of list l (prev = -1 first) */
static void spu_push_front(struct struct_62dc437e *l, int k)
{
    struct struct_6d45f61a *p = spu_node(k);
    p->prev = -1;
    p->next = l->top;
    l->top = (short)k;
    l->num = (short)(l->num + 1);
    if (p->next != -1) spu_node(p->next)->prev = (short)k;
}

/* _SortInsertInfo's block R (inlined three times in the original): remove node k from list l, return it to the pool */
static void spu_unlink_to_pool(struct struct_62dc437e *l, int k)
{
    struct struct_6d45f61a *p = spu_node(k);
    if (l->top == k) l->top = p->next;
    l->num = (short)(l->num - 1);
    if (p->prev != -1) spu_node(p->prev)->next = p->next;
    if (p->next != -1) spu_node(p->next)->prev = p->prev;
    p->prev = -1;
    p->next = -1;
    spu_push_front(&s_SMemMng.info, k);
}

static void _SortInsertInfo(struct struct_62dc437e *pList, int Index)
{
    struct struct_6d45f61a *pInfo = spu_node(Index);
    if (pList->top == -1) {
        pList->top = (short)Index;
        pList->num = (short)(pList->num + 1);
        pInfo->prev = -1;
        pInfo->next = -1;
        return;
    }
    /* first node with a higher address (equal goes after); last = the node visited before it */
    int c = pList->top;
    struct struct_6d45f61a *last = 0;
    while (c != -1) {
        last = spu_node(c);
        if (pInfo->iSpuAddr < last->iSpuAddr) break;
        c = last->next;
    }
    if (c == -1) {
        /* append after the tail; prev written as the asm computes it (= the tail in a consistent list) */
        last->next = (short)Index;
        if (last->prev != -1)
            pInfo->prev = spu_node(last->prev)->next;
        else
            pInfo->prev = pList->top;
        pInfo->next = -1;
        pList->num = (short)(pList->num + 1);
        if (pInfo->iSpuAddr == last->iSpuAddr + last->iSize) {
            last->iSize = last->iSize + pInfo->iSize;
            spu_unlink_to_pool(pList, Index);
        }
        return;
    }
    /* insert before c */
    struct struct_6d45f61a *pNext = spu_node(c);
    pInfo->next = (short)c;
    pInfo->prev = pNext->prev;
    pNext->prev = (short)Index;
    if (pInfo->prev != -1) spu_node(pInfo->prev)->next = (short)Index;
    pList->num = (short)(pList->num + 1);
    if (pList->top == c) pList->top = (short)Index;
    struct struct_6d45f61a *cur = pInfo;
    if (pInfo->prev != -1) {
        struct struct_6d45f61a *pPrev = spu_node(pInfo->prev);
        if (pInfo->iSpuAddr == pPrev->iSpuAddr + pPrev->iSize) {
            pPrev->iSize = pPrev->iSize + pInfo->iSize;
            spu_unlink_to_pool(pList, Index);
            cur = pPrev;
        }
    }
    if (pNext->iSpuAddr == cur->iSpuAddr + cur->iSize) {
        cur->iSize = cur->iSize + pNext->iSize;
        spu_unlink_to_pool(pList, c);
    }
}

void FsSpuMmng_Free(int id, int iSpuAddr)
{
    PS2_FRAME(0x10);
    if (id < 0 || id >= 16) return;
    struct struct_62dc437e *l = &s_SMemMng.alloc[id];
    int top = l->top;
    int a = top;
    if (a == -1) return;
    struct struct_6d45f61a *p;
    for (;;) {
        p = spu_node(a);
        if (p->iSpuAddr == iSpuAddr) break;
        a = p->next;
        if (a == -1) return;
    }
    if (top == a) l->top = p->next;
    l->num = (short)(l->num - 1);
    if (p->prev != -1) spu_node(p->prev)->next = p->next;
    if (p->next != -1) spu_node(p->next)->prev = p->prev;
    p->prev = -1;
    p->next = -1;   /* jal delay slot */
    _SortInsertInfo(&s_SMemMng.free[id], a);
}

int FsSpuMmng_Alloc(int id, int iSize)
{
    if (id < 0 || id >= 16) return 0;
    if (iSize <= 0) return 0;
    struct struct_62dc437e *fl = &s_SMemMng.free[id];
    if (fl->num == 0) return 0;
    int head = fl->top;
    int a = head;
    if (a == -1) return 0;
    struct struct_6d45f61a *p;
    for (;;) {
        p = spu_node(a);
        if (!(p->iSize < iSize)) break;
        a = p->next;
        if (a == -1) return 0;
    }
    struct struct_62dc437e *al = &s_SMemMng.alloc[id];
    if (p->iSize == iSize) {
        /* exact fit: move the node to the alloc list */
        if (head == a) fl->top = p->next;
        fl->num = (short)(fl->num - 1);
        if (p->prev != -1) spu_node(p->prev)->next = p->next;
        if (p->next != -1) spu_node(p->next)->prev = p->prev;
        p->prev = -1;
        p->next = -1;
        spu_push_front(al, a);
        return p->iSpuAddr;
    }
    /* split: a pool node takes the front part, the free node keeps the rest */
    if (s_SMemMng.info.num == 0) return 0;
    int m = spu_pool_take();
    if (m == -1) return 0;
    struct struct_6d45f61a *q = spu_node(m);
    q->iSpuAddr = p->iSpuAddr;
    q->iSize = iSize;
    p->iSpuAddr = p->iSpuAddr + iSize;
    p->iSize = p->iSize - iSize;
    spu_push_front(al, m);
    return q->iSpuAddr;
}

void FsSpuMmng_AddMemBlock(int id, int iSpuAddr, int iSize)
{
    if (id < 0 || id >= 16) return;
    int n = s_SMemMng.info.num == 0 ? -1 : spu_pool_take();   /* -1: s_SMemInfo[-1] (quirk, unreachable) */
    struct struct_6d45f61a *p = spu_node(n);
    p->iSpuAddr = iSpuAddr;
    p->iSize = iSize;
    spu_push_front(&s_SMemMng.free[id], n);
}

void FsSpuMmng_Init(void)
{
    PS2_FRAME(0x10);
    ps2_memset(&s_SMemMng, 0, 0x84);
    ps2_memset(s_SMemInfo, 0, 0x100);
    for (int id = 0; id < 16; id++) {
        s_SMemMng.free[id].top = -1;
        s_SMemMng.free[id].num = 0;
        s_SMemMng.alloc[id].top = -1;
        s_SMemMng.alloc[id].num = 0;
    }
    s_SMemMng.info.top = 0;
    s_SMemMng.info.num = 16;
    for (int i = 0; i < 16; i++) {
        s_SMemInfo[i].iSpuAddr = 0;
        s_SMemInfo[i].iSize = 0;
        s_SMemInfo[i].next = (short)(i + 1);
        s_SMemInfo[i].prev = (short)(i - 1);
    }
    s_SMemInfo[15].next = -1;
}
