/* lib/fslibmfstrm/mstream.cpp - EE-side stream table of the FsMFStrm client (whole file). s_StrmInfo[8] is a pool of
 * stream records kept as two doubly linked lists of short indices (free / allocated, heads and counts in s_StrmMng);
 * info.index of a record is the IOP stream index the IOP returned for its allocation (the EE index is the slot).
 * A binary semaphore (s_SemaID, max 1) serialises every public entry against the IOP -> EE notification thread
 * (fslibmfstrm.cpp _ItoE_RpcDispatch -> mstrm_EndReading / CheckReadError, never seen in any run). The stream
 * operations themselves are stream.c / sbuf.c.
 *
 * Quirks kept: mstrm_FreeIopStream sends the allocation command 0x1000 and treats a nonzero reply as freed;
 * mstrm_Quit deletes s_SemaID before the loop that waits on it again (mstrm_FreeStreamBuffer); mstrm_AttachSector
 * prints its debug lines on SUCCESS and signals s_iSyncSema (-1, never created) every time; the helper thread
 * (s_iThreadID), sync semaphore, s_iEndReadFlag and both callbacks exist as code only (nothing sets them);
 * mstrm_EndReading's callback branch reads auto_free of s_StrmInfo[index] (index = the IOP index argument), not of
 * the matching slot; _mstrm_GetInfo leaves the record's prev as it was (the free-list head's -1).
 * SemaParam currentCount / numWaitThreads / attr / option are never written (stale stack the kernel copies). */
#include "acnb/port.h"
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/local/lib/fslibmfstrm/mstream.h"

#ifdef ACNB_SOURCEPORT
char mstrm_AttachSector____FUNCTION__[19] = "mstrm_AttachSector";
int s_SemaID__S__mstream = -1;
int s_iThreadID = -1;
int s_iSyncSema = -1;
int s_iEndReadFlag = -1;
char mstrm_msg_init_sema[51] = "mstrm_Init : ERROR!!! Can not create semaphore...\n";
char mstrm_msg_create[65] = "mstrm_CreateStream : ERROR!!! can not get stream information...\n";
char mstrm_msg_alloc_iop[71] = "mstrm_AllocIopStream : ERROR!!! IOP stream setup was failure...ret=%d\n";
char mstrm_msg_delete_index[55] = "mstrm_DeleteStream : ERROR!!! invalid stream index...\n";
char mstrm_msg_freeiop_index[56] = "mstrm_FreeIopStream : ERROR!!! invalid stream index...\n";
char mstrm_msg_freeiop_fail[59] = "mstrm_freeIopStream : ERROR!!! Can not free iop stream...\n";
char mstrm_msg_freebuf_index[59] = "mstrm_FreeStreamBuffer : ERROR!!! invalid stream index...\n";
char mstrm_msg_attach_func[6] = "%s : ";
char mstrm_msg_attach[44] = "Index = %d, Lsn = %d, Size = %d, Prio = %d\n";
#endif

#define SEMA s_SemaID__S__mstream

/* take the head of the free list and push it onto the allocated list (LIFO); -1 when none is free */
static short _mstrm_GetInfo(void)
{
    if (s_StrmMng.free.num == 0) return -1;
    short i = s_StrmMng.free.top;
    _MSTRM_INFO_ST *p = &s_StrmInfo[i];
    s_StrmMng.free.top = p->next;
    s_StrmMng.free.num = (short)(s_StrmMng.free.num - 1);
    if (p->next != -1) s_StrmInfo[p->next].prev = -1;
    if (s_StrmMng.alloc.top != -1) s_StrmInfo[s_StrmMng.alloc.top].prev = i;
    p->next = s_StrmMng.alloc.top;
    s_StrmMng.alloc.top = i;
    s_StrmMng.alloc.num = (short)(s_StrmMng.alloc.num + 1);
    return i;
}

/* unlink a record from the allocated list and push it onto the free list */
static void _mstrm_ReleaseInfo(short index)
{
    if (index < 0 || index >= 8) return;
    _MSTRM_INFO_ST *p = &s_StrmInfo[index];
    if (p->prev != -1) s_StrmInfo[p->prev].next = p->next;
    if (p->next != -1) s_StrmInfo[p->next].prev = p->prev;
    if (s_StrmMng.alloc.top == index) s_StrmMng.alloc.top = p->next;
    s_StrmMng.alloc.num = (short)(s_StrmMng.alloc.num - 1);
    if (s_StrmMng.free.top != -1) s_StrmInfo[s_StrmMng.free.top].prev = index;
    p->next = s_StrmMng.free.top;
    p->prev = -1;
    s_StrmMng.free.top = index;
    s_StrmMng.free.num = (short)(s_StrmMng.free.num + 1);
}

void mstrm_Init(void)
{
    PS2_FRAME(0x40);
    if (s_StrmMng.flags.b.inited) return;
    for (int i = 0; i < 8; i++) {
        strm_InitInfo(&s_StrmInfo[i].info);
        s_StrmInfo[i].next = (short)(i + 1);
        s_StrmInfo[i].prev = (short)(i - 1);
        s_StrmInfo[i].auto_read = 1;
        s_StrmInfo[i].auto_free = 0;
    }
    s_StrmInfo[7].next = -1;
    s_StrmMng.flags.uiVal = 0;
    s_StrmMng.uiStatus = 0;
    s_StrmMng.alloc.top = -1;
    s_StrmMng.alloc.num = 0;
    s_StrmMng.free.top = 0;
    s_StrmMng.free.num = 8;
    sbuf_Init();
    PS2_STACK(SemaParam, sema, 0x20);   /* currentCount / numWaitThreads / attr / option: stale stack */
    sema.initCount = 1;
    sema.maxCount = 1;
    SEMA = ps2_CreateSema(&sema);
    if (SEMA < 0)
        ps2_printf(mstrm_msg_init_sema);
    else
        s_StrmMng.flags.b.inited = 1;
}

void mstrm_Quit(void)
{
    PS2_FRAME(0x40);
    if (!s_StrmMng.flags.b.inited) return;
    ps2_WaitSema(SEMA);
    ps2_DeleteSema(SEMA);   /* before the loop: mstrm_FreeStreamBuffer's WaitSema then hits a dead id */
    for (int i = s_StrmMng.alloc.top; i != -1;) {
        _MSTRM_INFO_ST *p = &s_StrmInfo[i];
        strm_Close(&p->info);
        int next = p->next;
        mstrm_FreeStreamBuffer(i);
        _mstrm_ReleaseInfo((short)i);
        i = next;
    }
    sbuf_Quit();
    if (s_iThreadID >= 0) {
        ps2_TerminateThread(s_iThreadID);
        ps2_DeleteThread(s_iThreadID);
    }
    if (s_iSyncSema >= 0) ps2_DeleteSema(s_iSyncSema);
    s_StrmMng.flags.b.inited = 0;
}

int mstrm_CreateStream(void)
{
    PS2_FRAME(0x20);
    ps2_WaitSema(SEMA);
    short i = _mstrm_GetInfo();
    if (i < 0) ps2_printf(mstrm_msg_create);
    ps2_SignalSema(SEMA);
    return i;
}

int mstrm_AllocIopStream(int index, int iSize)
{
    PS2_FRAME(0x50);
    int ret = 1;
    if (index < 0 || index >= 8) return 0;
    if (iSize <= 0) return 0;
    ps2_WaitSema(SEMA);
    PS2_STACK(int, size, 0x40);   /* the argument's spill slot, byte-copied into the send buffer */
    size = (iSize + 0x7ff) & -0x800;
    _MSTRM_INFO_ST *p = &s_StrmInfo[index];
    p->info.index = fsmfstrm_CallRpcStruct(0x1000, 1, 4, 4, &size);   /* the reply is the IOP stream index */
    if (p->info.index < 0) {
        ps2_printf_d(mstrm_msg_alloc_iop, p->info.index);
        ret = 0;
    }
    ps2_SignalSema(SEMA);
    return ret;
}

int mstrm_DeleteStream(int index)
{
    PS2_FRAME(0x30);
    if (index < 0 || index >= 8) {
        ps2_printf(mstrm_msg_delete_index);
        return 0;
    }
    if (s_StrmInfo[index].info.index != -1) mstrm_FreeIopStream(index);
    ps2_WaitSema(SEMA);
    _MSTRM_INFO_ST *p = &s_StrmInfo[index];
    sbuf_FreeStreamBuffer(p->info.hSBuf);
    strm_DetachStreamBuffer(&p->info);
    _mstrm_ReleaseInfo((short)index);
    ps2_SignalSema(SEMA);
    return 1;
}

int mstrm_FreeIopStream(int index)
{
    PS2_FRAME(0x40);
    if (index < 0 || index >= 8) {
        ps2_printf(mstrm_msg_freeiop_index);
        return 0;
    }
    ps2_WaitSema(SEMA);
    _MSTRM_INFO_ST *p = &s_StrmInfo[index];
    /* the allocation command 0x1000 again (original), sending &p->info.index */
    int r = fsmfstrm_CallRpcStruct(0x1000, 1, 4, 4, p);
    if (r != 0)
        p->info.index = -1;
    else
        ps2_printf(mstrm_msg_freeiop_fail);
    ps2_SignalSema(SEMA);
    return r;
}

int mstrm_FreeStreamBuffer(int index)
{
    PS2_FRAME(0x30);
    if (index < 0 || index >= 8) {
        ps2_printf(mstrm_msg_freebuf_index);
        return 0;
    }
    ps2_WaitSema(SEMA);
    _MSTRM_INFO_ST *p = &s_StrmInfo[index];
    sbuf_FreeStreamBuffer(p->info.hSBuf);
    strm_DetachStreamBuffer(&p->info);
    ps2_SignalSema(SEMA);
    return 1;
}

int mstrm_AttachSector(int index, int iLsn, int iSize, unsigned short usPrio)
{
    PS2_FRAME(0x70);
    if (index < 0 || index >= 8) return 0;
    ps2_WaitSema(SEMA);
    int r = strm_OpenSector(&s_StrmInfo[index].info, iLsn, iSize, usPrio);
    if (r != 0) {   /* the debug lines print on success */
        ps2_printf_s(mstrm_msg_attach_func, mstrm_AttachSector____FUNCTION__);
        ps2_printf_dddd(mstrm_msg_attach, index, iLsn, iSize, (int)(usPrio & 0xffff));
    }
    ps2_SignalSema(SEMA);
    ps2_SignalSema(s_iSyncSema);   /* -1: never created, the kernel returns -1 */
    return r;
}

int mstrm_DetachFileSector(int index)
{
    PS2_FRAME(0x30);
    if (index < 0 || index >= 8) return 0;
    ps2_WaitSema(SEMA);
    int r = strm_Close(&s_StrmInfo[index].info);
    ps2_SignalSema(SEMA);
    return r;
}

int mstrm_SetReloadSize(int index, int iSize)
{
    PS2_FRAME(0x40);
    if (index < 0 || index >= 8) return -1;
    ps2_WaitSema(SEMA);
    int r = strm_SetReloadSize(&s_StrmInfo[index].info, iSize);
    ps2_SignalSema(SEMA);
    return r;
}

/* IOP -> EE end-of-read notification for IOP stream index `index` */
void mstrm_EndReading(int index)
{
    PS2_FRAME(0x30);
    ps2_WaitSema(SEMA);
    for (int i = 0; i < 8; i++) {
        if (index != s_StrmInfo[i].info.index) continue;
        strm_EndReading(&s_StrmInfo[i].info);
        if (s_iThreadID >= 0) {   /* dead: no helper thread */
            s_iEndReadFlag = i;   /* jal delay slot (0x106488): stored before the SignalSema */
            ps2_SignalSema(SEMA);
            ps2_SignalSema(s_iSyncSema);
        } else if (s_StrmInfo[i].info.uiStatus == 5 && s_ReadEndCB != nullptr) {   /* dead: no callback */
            ps2_SignalSema(SEMA);
            if (s_StrmInfo[index].auto_free) mstrm_FreeStreamBuffer(i);   /* [index], not [i] (asm 0x1064e8) */
            gcall(s_ReadEndCB, i);
        } else {
            ps2_SignalSema(SEMA);
        }
        return;
    }
    ps2_SignalSema(SEMA);
}

void mstrm_CheckReadError(int index, int ErrNo)
{
    PS2_FRAME(0x30);
    if (s_ReadErrCB) gcall(s_ReadErrCB, index, ErrNo);
}

int mstrm_GetIopIndex(int index)
{
    if (index < 0 || index >= 8) return -1;
    return s_StrmInfo[index].info.index;
}
