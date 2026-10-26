/* src/menu/mdata.cpp — menu data archives (OLMENU). A loaded archive is {u32 size, u32 count, 8 bytes,
 * entries[count] of 16 bytes: u32 offset, 4 bytes, u8 relocated flag, 3 bytes}. Entry offsets are turned
 * into pointers on first access (GetData / GetTm2TexInfo) or all at once (Remap). */

struct MDataEntry {
    unsigned int ofs;   /* offset from the archive start; a pointer once relocated */
    unsigned int pad;
    unsigned char relocated;
    unsigned char pad2[7];
};

struct MDataHead {
    unsigned int size;
    unsigned int count;
    unsigned int pad[2];
    MDataEntry entry[1];
};

/* Shared body of GetData / GetTm2TexInfo. An index is accepted when 0 <= idx < count, or for any idx when
 * count is 0 (original: reads past the table then). */
static void *mdata_get(void *inBuff, int inNum)
{
    MDataHead *h = (MDataHead *)inBuff;
    MDataEntry *e;

    if (!h) return 0;
    if ((inNum < 0 || !((unsigned int)inNum < h->count)) && h->count != 0) return 0;
    e = &h->entry[inNum];
    if (e->relocated == 0) {
        e->relocated = 1;
        if (e->ofs != 0) e->ofs = PS2PTR(h) + e->ofs;
    }
    return (void *)(uintptr_t)e->ofs;
}

/* TM2 texture info of entry inNum = 16 bytes past the entry start. */
struct struct_13efd7f4 *DF_MDATA_GetTm2TexInfo(void *inBuff, int inNum)
{
    unsigned char *p = (unsigned char *)mdata_get(inBuff, inNum);
    return p ? (struct struct_13efd7f4 *)(p + 0x10) : 0;
}

void *DF_MDATA_GetData(void *inBuff, int inNum)
{
    return mdata_get(inBuff, inNum);
}

/* Relocates every entry; returns the archive size. */
unsigned int DF_MDATA_Remap(void *ioBuff)
{
    MDataHead *h = (MDataHead *)ioBuff;
    unsigned int i;

    if (!h) return 0;
    for (i = 0; i < h->count; i++) {
        MDataEntry *e = &h->entry[i];
        if (e->relocated == 0) {
            e->relocated = 1;
            if (e->ofs != 0) e->ofs += PS2PTR(h);
        }
    }
    return h->size;
}
