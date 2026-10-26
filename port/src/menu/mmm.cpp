/* src/menu/mmm.cpp — menu memory manager (OLMENU): first-fit allocator over up to 4 arenas ("kinds",
 * MMMKindMng). Every block has a 16-byte header (MMMBlock) followed by its payload; the blocks of an
 * arena form a doubly linked list. Sizes are rounded up to 0x40; Free merges neighbouring free blocks.
 * quick_match.c calls DF_MMM_Alloc / DF_MMM_Free by name. */

struct MMMBlock {
    unsigned char used;    /* 1 = in use */
    unsigned char valid;   /* 1 once linked; 0 = untouched header (arena start after KindInit) */
    unsigned char pad[2];
    int size;              /* payload bytes */
    MMMBlock *prev;
    MMMBlock *next;        /* 0 = last block */
};

#define BLK(p) ((MMMBlock *)(p))

void DF_MMM_Free(int inKind, void *inBuff)
{
    MMMBlock *cur, *first, *p, *after;
    unsigned char *end;

    if (!inBuff) return;
    cur = BLK(MMMKindMng[inKind].buff);
    end = MMMKindMng[inKind].buff + MMMKindMng[inKind].size;
    /* Find the block; when the list runs to the arena end without a match, cur is the block past the end
     * and the code below still runs on it (original: clears one byte past the arena). */
    while ((unsigned char *)cur < end) {
        if (cur->used == 1 && inBuff == (unsigned char *)cur + 0x10) break;
        cur = cur->next;
        if (!cur) return;
    }
    cur->used = 0;
    /* Back to the first block of the free run. A prev whose used flag is neither 0 nor 1 is still taken
     * (branch delay slot), then the walk stops. */
    first = cur;
    while ((p = first->prev) != 0 && p->used != 1) {
        first = p;
        if (p->used != 0) break;
    }
    /* Forward to the first used block after the run. */
    after = cur->next;
    while (after && after->used == 0) after = after->next;
    if (!after) {
        first->next = 0;   /* the size of the new tail block is left as it was */
        return;
    }
    first->size = (int)((unsigned char *)after - ((unsigned char *)first + 0x10));
    first->next = after;
    after->prev = first;
}

void *DF_MMM_Alloc(int inKind, int inSize)
{
    PS2_FRAME(0x20);
    MMMBlock *prev = 0, *cur, *next;
    unsigned char *end;

    if (inSize <= 0) return 0;
    cur = BLK(MMMKindMng[inKind].buff);
    end = MMMKindMng[inKind].buff + MMMKindMng[inKind].size;
    if (inSize & 0x3f) inSize += 0x40 - (inSize & 0x3f);
    while ((unsigned char *)cur < end) {
        if (cur->used == 1) {
            prev = cur;
            cur = cur->next;
            continue;
        }
        if (cur->valid == 0) {
            cur->prev = 0;
            cur->next = 0;
        }
        next = cur->next;
        if (!next) {
            /* Last block: take it if the new tail header still fits in the arena. */
            unsigned char *tail = (unsigned char *)cur + inSize + 0x10;
            if (!(tail < end)) return 0;
            cur->used = 1;
            cur->valid = 1;
            cur->size = inSize;
            cur->prev = prev;
            cur->next = BLK(tail);
            DF_MFUNC_ClearMemory(cur->next, 4);
            return (unsigned char *)cur + 0x10;
        }
        if (inSize + 0x10 < cur->size && cur->valid == 1) {
            /* Split (a free block of exactly inSize + 0x10 is never reused). */
            int rest = cur->size - (inSize + 0x10);
            MMMBlock *old = cur->next;
            cur->used = 1;
            cur->size = inSize;
            cur->next = BLK((unsigned char *)cur + inSize + 0x10);
            cur->next->valid = 1;
            cur->next->used = 0;
            cur->next->size = rest;
            cur->next->prev = cur;
            cur->next->next = old;
            old->prev = cur->next;
            return (unsigned char *)cur + 0x10;
        }
        prev = cur;
        cur = next;
    }
    return 0;
}

/* Sets up arena inKind at inBuff (aligned to 16) and clears it; returns the end of the arena. */
unsigned char *DF_MMM_KindInit(int inKind, unsigned char *ioBuff, int inSize)
{
    PS2_FRAME(0x40);
    unsigned char *buff = (unsigned char *)DF_MFUNC_Aligned(ioBuff, 0x10);

    MMMKindMng[inKind].buff = buff;
    MMMKindMng[inKind].size = inSize;
    DF_MFUNC_ClearMemory(MMMKindMng[inKind].buff, inSize >> 2);
    return buff + inSize;
}

void DF_MMM_Init(void)
{
    for (int i = 0; i < 4; i++) {
        MMMKindMng[i].buff = 0;
        MMMKindMng[i].size = 0;
    }
}
