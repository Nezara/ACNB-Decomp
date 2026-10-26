/* lib/fslibmfstrm/sbuf.cpp - EE-side stream ring-buffer bookkeeping of the FsMFStrm client (whole file).
 * s_StrmBuffInfo[8] is a pool of buffer records kept as two doubly linked lists of short indices (free / allocated,
 * heads and counts in s_StrmBuffMng), the same shape as mstream.c's s_StrmInfo. Each record would carry a read window
 * (pBuf / pBufTop / pBufEnd / iBufSize / iReadSize / pBufRead) and a semaphore s_SemaID[index] that a reader blocks on
 * until the IOP delivers a block.
 *
 * Quirks kept: nothing in the library ever allocates a buffer (there is no sbuf_Alloc) and nothing creates the
 * semaphores (sbuf_Init sets all eight ids to -1), so stream records keep hSBuf == NULL and every function except
 * sbuf_Init returns at its first guard in this binary; sbuf_Quit only clears pBuf (frees nothing). */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/local/lib/fslibmfstrm/sbuf.h"

#ifdef ACNB_SOURCEPORT
struct _STREAM_BUF_MNG_ST s_StrmBuffMng = { { 0, 8 }, { -1, 0 } };   /* initialised data; flags 0 */
#endif

#define MNG s_StrmBuffMng

/* unlink a record from the allocated list and push it onto the free list */
static void _sbuf_ReleaseStrmBufInfo(struct _STREAM_BUF_ST *pInfo)
{
    if (!MNG.flags.b.inited) return;
    if (!pInfo) return;
    if (pInfo->prev != -1) s_StrmBuffInfo[pInfo->prev].next = pInfo->next;
    if (pInfo->next != -1) s_StrmBuffInfo[pInfo->next].prev = pInfo->prev;
    if (pInfo->index == MNG.alloc.top) MNG.alloc.top = pInfo->next;
    MNG.alloc.num = (short)(MNG.alloc.num - 1);
    if (MNG.free.top != -1) s_StrmBuffInfo[MNG.free.top].prev = pInfo->index;
    pInfo->next = MNG.free.top;
    pInfo->prev = -1;
    MNG.free.top = pInfo->index;
    MNG.free.num = (short)(MNG.free.num + 1);
}

void sbuf_Init(void)
{
    if (MNG.flags.b.inited) return;
    for (int i = 0; i < 8; i++) {
        _STREAM_BUF_ST *p = &s_StrmBuffInfo[i];
        p->pBuf = 0;
        p->pBufTop = 0;
        p->pBufEnd = 0;
        p->iBufSize = 0;
        p->iReadSize = 0;
        p->pBufRead = 0;
        p->next = (short)(i + 1);
        p->prev = (short)(i - 1);
        p->index = (short)i;
        s_SemaID[i] = -1;
    }
    s_StrmBuffInfo[7].next = -1;
    MNG.free.top = 0;
    MNG.free.num = 8;
    MNG.alloc.top = -1;
    MNG.alloc.num = 0;
    MNG.flags.b.inited = 1;   /* byte RMW: the other bits of the flags byte are kept */
}

void sbuf_Quit(void)
{
    if (!MNG.flags.b.inited) return;
    for (int i = 0; i < 8; i++)
        if (s_StrmBuffInfo[i].pBuf) s_StrmBuffInfo[i].pBuf = 0;
    MNG.flags.b.inited = 0;
}

void sbuf_FreeStreamBuffer(void *hSBuf)
{
    PS2_FRAME(0x30);
    if (!MNG.flags.b.inited) return;
    if (!hSBuf) return;
    _STREAM_BUF_ST *h = (_STREAM_BUF_ST *)hSBuf;
    ps2_WaitSema(s_SemaID[h->index]);
    ps2_DeleteSema(s_SemaID[h->index]);
    h->pBufEnd = 0;
    h->pBufTop = 0;
    h->pBuf = 0;
    h->iBufSize = 0;
    h->iReadSize = 0;
    h->pBufRead = 0;
    _sbuf_ReleaseStrmBufInfo(h);
}

/* the block at pBufRead was consumed: move the end of the free window there and wake the reader */
int sbuf_FreeReadBlock(void *hSBuf)
{
    PS2_FRAME(0x30);
    if (!MNG.flags.b.inited) return 0;
    if (!hSBuf) return 0;
    _STREAM_BUF_ST *h = (_STREAM_BUF_ST *)hSBuf;
    if (!h->pBuf) return 0;
    if (!h->pBufRead) return 0;
    h->pBufEnd = h->pBufRead;
    if ((unsigned int)h->pBufTop < (unsigned int)h->pBufEnd)
        h->iReadSize = (int)((unsigned int)h->pBufEnd - (unsigned int)h->pBufTop);
    else if ((unsigned int)h->pBufEnd < (unsigned int)h->pBufTop)
        h->iReadSize = h->iBufSize - (int)((unsigned int)h->pBufTop - (unsigned int)h->pBufEnd);
    else
        h->iReadSize = h->iBufSize;
    ps2_SignalSema(s_SemaID[h->index]);
    return 1;
}

/* block: wait for the IOP block, then release the semaphore again (0); else poll: 1 = still busy */
int sbuf_SyncReadBlock(void *hSBuf, int block)
{
    PS2_FRAME(0x40);
    if (!MNG.flags.b.inited) return 0;
    if (!hSBuf) return 0;
    _STREAM_BUF_ST *h = (_STREAM_BUF_ST *)hSBuf;
    if (!h->pBuf) return 0;
    if (!h->pBufRead) return 0;
    if (block) {
        ps2_WaitSema(s_SemaID[h->index]);
        ps2_SignalSema(s_SemaID[h->index]);
        return 0;
    }
    if (ps2_PollSema(s_SemaID[h->index]) < 0) return 1;
    ps2_SignalSema(s_SemaID[h->index]);
    return 0;
}

/* reset the read window to the whole buffer; returns the old iReadSize */
int sbuf_ClearData(void *hSBuf)
{
    if (!MNG.flags.b.inited) return 0;
    if (!hSBuf) return 0;
    _STREAM_BUF_ST *h = (_STREAM_BUF_ST *)hSBuf;
    if (!h->pBuf) return 0;
    int r = h->iReadSize;
    char *b = h->pBuf;
    h->pBufEnd = b;
    h->pBufTop = b;
    h->iReadSize = 0;
    h->pBufRead = h->pBuf;
    return r;
}
