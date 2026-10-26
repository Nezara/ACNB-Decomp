/* ivag SPU RAM allocator: 16 records linked by `short` index into three
 * lists - free (sorted by address, adjacent blocks coalesced), alloc (newest first) and the unused pool. First fit in
 * free-list order, the split takes the front of the block; address 0 means failure (Q-6). Function comments give the
 * original's image offset. */
#include "ivag_state.h"
#include <string.h>

namespace ivag {

static SMemInfo *info(int i) { return &s_SMemInfo[i]; }
static int32_t add(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }   /* addu wraps */

/* 0x690: a record from the pool (prev / addr / size untouched); -1 = pool empty */
int _GetInfo()
{
    ListHead &pool = s_SMemMng.pool;
    if (pool.num == 0) return -1;
    pool.num--;
    int idx = pool.top;
    SMemInfo *n = info(idx);
    pool.top = n->next;
    if (n->next != -1) info(n->next)->prev = -1;
    n->next = -1;
    return idx;
}

/* 0x70c: insert at the head */
void _LinkInfo(ListHead *list, int idx)
{
    SMemInfo *n = info(idx);
    n->prev = -1;
    n->next = list->top;
    list->top = (int16_t)idx;
    list->num++;
    if (n->next != -1) info(n->next)->prev = (int16_t)idx;
}

/* 0x770: the head test compares top, and num drops even if idx is not in the list (GH6) */
void _UnlinkInfo(ListHead *list, int idx)
{
    SMemInfo *n = info(idx);
    if (list->top == idx) list->top = n->next;
    list->num--;
    if (n->prev != -1) info(n->prev)->next = n->next;
    if (n->next != -1) info(n->next)->prev = n->prev;
    n->prev = -1;
    n->next = -1;
}

/* 0x80c: sorted insert (equal addresses go after the equal node: the walk stops at the first greater address), then
 * merge with the neighbours; merged-away records go back to the pool */
void _SortInsertInfo(ListHead *list, int idx)
{
    SMemInfo *node = info(idx);
    int s = list->top;
    if (s == -1) {
        list->top = (int16_t)idx;
        list->num++;
        node->prev = -1;
        node->next = -1;
        return;
    }
    SMemInfo *at = 0;   /* s2: the last node visited */
    while (s != -1) {
        at = info(s);
        if (node->addr < at->addr) break;
        s = at->next;
    }
    if (s == -1) {
        /* ran off the end: append after `at` (its index derived as the asm does: its prev's next, or the head) */
        at->next = (int16_t)idx;
        node->prev = at->prev != -1 ? info(at->prev)->next : list->top;
        node->next = -1;
        list->num++;
        if (node->addr == add(at->addr, at->size)) {
            at->size = add(at->size, node->size);
            _UnlinkInfo(list, idx);
            _LinkInfo(&s_SMemMng.pool, idx);
        }
        return;
    }
    node->next = (int16_t)s;
    node->prev = at->prev;
    at->prev = (int16_t)idx;
    if (node->prev != -1) info(node->prev)->next = (int16_t)idx;
    list->num++;
    if (list->top == s) list->top = (int16_t)idx;
    SMemInfo *cur = node;
    if (node->prev != -1) {
        SMemInfo *p = info(node->prev);
        if (node->addr == add(p->addr, p->size)) {
            p->size = add(p->size, node->size);
            _UnlinkInfo(list, idx);
            _LinkInfo(&s_SMemMng.pool, idx);
            cur = p;
        }
    }
    if (at->addr == add(cur->addr, cur->size)) {
        cur->size = add(cur->size, at->size);
        _UnlinkInfo(list, s);
        _LinkInfo(&s_SMemMng.pool, s);
    }
}

/* 0xa70 */
void SpuMmng_Init(int addr, int size)
{
    memset(&s_SMemMng, 0, sizeof s_SMemMng);
    memset(s_SMemInfo, 0, sizeof s_SMemInfo);
    s_SMemMng.base = addr;
    s_SMemMng.size = size;
    s_SMemMng.free.num = 0;
    s_SMemMng.alloc.num = 0;
    s_SMemMng.pool.top = 0;
    s_SMemMng.free.top = -1;
    s_SMemMng.alloc.top = -1;
    s_SMemMng.pool.num = 16;
    for (int i = 0; i < 16; i++) {
        SMemInfo *n = info(i);
        n->addr = 0;
        n->size = 0;
        n->next = (int16_t)(i + 1);
        n->prev = (int16_t)(i - 1);
    }
    info(15)->next = -1;
    int id = _GetInfo();
    info(id)->addr = addr;
    info(id)->size = size;
    _LinkInfo(&s_SMemMng.free, id);
}

/* 0xb80: SPU address or 0 */
int SpuMmng_Alloc(int size)
{
    if (size <= 0) return 0;
    if (s_SMemMng.free.num == 0) return 0;
    for (int n = s_SMemMng.free.top; n != -1; n = info(n)->next) {
        SMemInfo *b = info(n);
        if (b->size < size) continue;
        if (b->size == size) {
            _UnlinkInfo(&s_SMemMng.free, n);
            _LinkInfo(&s_SMemMng.alloc, n);
            return b->addr;
        }
        int m = _GetInfo();
        if (m == -1) return 0;
        SMemInfo *r = info(m);
        r->addr = b->addr;
        r->size = size;
        b->addr = add(b->addr, size);
        b->size = add(b->size, -size);
        _LinkInfo(&s_SMemMng.alloc, m);
        return r->addr;
    }
    return 0;
}

/* 0xce4: an unknown address is ignored */
void SpuMmng_Free(int addr)
{
    for (int n = s_SMemMng.alloc.top; n != -1; n = info(n)->next) {
        if (info(n)->addr == addr) {
            _UnlinkInfo(&s_SMemMng.alloc, n);
            _SortInsertInfo(&s_SMemMng.free, n);
            return;
        }
    }
}

}  // namespace ivag
