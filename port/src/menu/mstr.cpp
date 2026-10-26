/* src/menu/mstr.cpp — menu string table (OLMENU). One file (LSN 0xfa5) is read into the shared read
 * buffer, copied into a menu heap block and relocated (__SetData); entries are sorted by string id and
 * hold a name / info pointer per language (0 = Japanese, else English). quick_match.c calls
 * DF_MSTR_GetName by name. */
#include "acnb/nodwarf.h"

/* Binary search by string id (inCtgrID is passed by the callers but not used). */
struct struct_38af1411 *__GetParam(int inCtgrID, unsigned int inStrID)
{
    struct struct_38af1411 *tbl = (struct struct_38af1411 *)(MStrTable + 1);
    int lo = 0, hi = MStrTable->num - 1;

    (void)inCtgrID;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        struct struct_38af1411 *e = &tbl[mid];
        if (inStrID < e->id)
            hi = mid - 1;
        else if (e->id < inStrID)
            lo = mid + 1;
        else
            return e;
    }
    return 0;
}

unsigned char *DF_MSTR_GetInfo(int inLang, int inCtgrID, unsigned int inStrID)
{
    PS2_FRAME(0x20);
    struct struct_38af1411 *e = __GetParam(inCtgrID, inStrID);

    if (!e) return mstr_empty_str;
    return inLang == 0 ? e->jpn.info_ofst : e->eng.info_ofst;
}

unsigned char *DF_MSTR_GetName(int inLang, int inCtgrID, unsigned int inStrID)
{
    PS2_FRAME(0x20);
    struct struct_38af1411 *e = __GetParam(inCtgrID, inStrID);

    if (!e) return mstr_empty_str;
    return inLang == 0 ? e->jpn.name_ofst : e->eng.name_ofst;
}

/* Copies the loaded table into a menu heap block and turns its string offsets into pointers. */
void __SetData(void)
{
    PS2_FRAME(0x20);
    int size = *(int *)(g_pbufRead + 0xc);
    struct struct_38af1411 *e;
    int i;

    MStrTable = (struct struct_50788c44 *)DF_MMM_Alloc(0, size);
    if (!MStrTable) return;
    DF_MFUNC_CopyMemory(MStrTable, g_pbufRead, size >> 2);
    e = (struct struct_38af1411 *)(MStrTable + 1);
    for (i = 0; i < MStrTable->num; i++, e++) {
        unsigned char *base = (unsigned char *)MStrTable;
        if (e->jpn.name_ofst) e->jpn.name_ofst = base + PS2PTR(e->jpn.name_ofst);
        if (e->jpn.info_ofst) e->jpn.info_ofst = base + PS2PTR(e->jpn.info_ofst);
        if (e->eng.name_ofst) e->eng.name_ofst = base + PS2PTR(e->eng.name_ofst);
        if (e->eng.info_ofst) e->eng.info_ofst = base + PS2PTR(e->eng.info_ofst);
    }
}

/* inMode 0: poll once (1 = still reading); else wait for the read. Then installs the table. */
int DF_MSTR_Sync(int inMode)
{
    PS2_FRAME(0x10);
    if (inMode == 0) {
        if (Cdvd_CdSync() == 1) return 1;
    } else {
        while (Cdvd_CdSync() == 1) {
        }
    }
    __SetData();
    MStrReadIdx = -1;
    return 0;
}

/* Starts reading the string table unless a read is in flight; inIdx is only remembered. */
void DF_MSTR_ReadStart(int inIdx)
{
    PS2_FRAME(0x10);
    if (MStrReadIdx != -1) return;
    MStrReadIdx = inIdx;
    Cdvd_ReadLsnBlind(0xfa5, g_pbufRead, 0, 0);
}

void DF_MSTR_Init(void)
{
    MStrTable = 0;
    MStrReadIdx = -1;
}
