/* lib/binder.cpp — the "binder" file archive: a 0x20-byte StListHeader followed by file_num StFileInfo records
 * (0x10 bytes: id, group, data offset, size, name). Lookups by file id are linear searches; Binder_Init points the
 * manager at an image and relocates the name offsets to pointers once (remapped flag). Used by the CD reader, the
 * stream player and the layout reader. */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include <string.h>

/* The three searches are separate copies of the same loop in the original (file_num read once); the found record
 * is copied to a 16-byte stack local at sp+0x20 (or to pfi). */
int Binder_GetSize(StBinderMng *mng, int id)
{
    PS2_FRAME(0x30);
    PS2_STACK(StFileInfo, finfo, 0x20);
    StFileInfo *p = mng->file_infop;
    unsigned int num = mng->head->file_num;
    int found = -1;
    for (unsigned int i = 0; i < num; i++, p++) {
        if (p->file_id == id) {
            memcpy(&finfo, p, 0x10);
            found = (int)i;
            break;
        }
    }
    if (found < 0) return -1;
    return (int)finfo.file_size;
}

int Binder_GetOffset(StBinderMng *mng, int id)
{
    PS2_FRAME(0x30);
    PS2_STACK(StFileInfo, finfo, 0x20);
    StFileInfo *p = mng->file_infop;
    unsigned int num = mng->head->file_num;
    int found = -1;
    for (unsigned int i = 0; i < num; i++, p++) {
        if (p->file_id == id) {
            memcpy(&finfo, p, 0x10);
            found = (int)i;
            break;
        }
    }
    if (found < 0) return -1;
    return (int)finfo.data_offset;
}

int Binder_IsExist(StBinderMng *mng, int id, StFileInfo *pfi)
{
    PS2_FRAME(0x30);
    PS2_STACK(StFileInfo, finfo, 0x20);
    if (pfi == NULL) pfi = &finfo;
    StFileInfo *p = mng->file_infop;
    unsigned int num = mng->head->file_num;
    for (unsigned int i = 0; i < num; i++, p++) {
        if (p->file_id == id) {
            memcpy(pfi, p, 0x10);
            return (int)i;
        }
    }
    return -1;
}

/* Relocates the name offsets once if the header says they are present (headerFlag.filenameOffset) and the image
 * was not remapped yet; the loop re-reads mng->head->file_num every iteration. */
void Binder_Init(StBinderMng *mng, void *data, unsigned int lsn)
{
    mng->head = (StListHeader *)data;
    mng->file_infop = (StFileInfo *)((char *)mng->head + 0x20);
    mng->lsn = lsn;
    StListHeader *head = mng->head;
    if (!head->headerFlag.filenameOffset) return;
    if (head->remapped != 0) return;
    StFileInfo *pfi = mng->file_infop;
    for (unsigned int ii = 0; ii < mng->head->file_num; ii++, pfi++) {
        if (pfi->name != NULL) pfi->name = (char *)data + (uintptr_t)pfi->name;
    }
    mng->head->remapped = 1;
}
