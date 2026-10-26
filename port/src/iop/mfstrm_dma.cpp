/* mfstrm dma.c: the SIF DMA arbiter (never used in Versus: its thread polls an empty list every 4 ms; only RPC
 * 0x100000 queues a request). Requests (s_ArbInfo, 8 records, `short` links) sit in an alloc list sorted by priority;
 * the dma thread sends one block per pass (packet buffer present: GetData copy into it; absent: the ring block itself,
 * removed after the DMA) and notifies the EE (fsmfstrm_CallRpc 0x10000100) when a request is done or cancelled.
 * Lock order s_DmaSema -> s_SemaID (the mstrm_* calls inside). */
#include "mfstrm_state.h"

namespace mfstrm {

int g_dma_update_calls;
uint64_t g_dma_update_t;

/* 0xde8: pop the free-list head, push it at the FRONT of the alloc list, clear the record (stream -1) */
int _GetArbiterInfo()
{
    if (s_ArbMng.free.num <= 0) return -1;
    int i = s_ArbMng.free.top;
    ArbInfo *r = &s_ArbInfo[i];
    s_ArbMng.free.top = r->next;
    if (s_ArbMng.free.top != -1) s_ArbInfo[s_ArbMng.free.top].prev = -1;
    s_ArbMng.free.num--;
    r->next = s_ArbMng.alloc.top;
    if (r->next != -1) s_ArbInfo[r->next].prev = (int16_t)i;
    r->prev = -1;
    s_ArbMng.alloc.top = (int16_t)i;
    s_ArbMng.alloc.num++;
    r->src = 0;
    r->dst = 0;
    r->remain = 0;
    r->chunk = 0;
    r->prio = 0;
    r->state = 0;
    r->flags = 0;
    r->stream = -1;
    return (int16_t)i;
}

/* 0xef0: unlink from the alloc list, push on the free list (LIFO). Q-G4e: the body (state, flags, src ..) is kept. */
void _ReleaseArbiterInfo(int idx)
{
    if (idx < 0 || idx >= 8) return;
    ArbInfo *r = &s_ArbInfo[idx];
    if (r->next != -1) s_ArbInfo[r->next].prev = r->prev;
    if (r->prev != -1) s_ArbInfo[r->prev].next = r->next;
    if (s_ArbMng.alloc.top == (int16_t)idx) s_ArbMng.alloc.top = r->next;
    s_ArbMng.alloc.num--;
    int16_t f = s_ArbMng.free.top;
    if (f != -1) s_ArbInfo[f].prev = (int16_t)idx;
    r->next = f;
    r->prev = -1;
    s_ArbMng.free.top = (int16_t)idx;
    s_ArbMng.free.num++;
}

/* 0xffc (leaf): move idx forward past every successor with prio <= its own, then backward past every predecessor with
 * prio >= its own. Ties swap (Q-G4j: equal priorities end up newest first; _mstrm_SortInfo does not swap on ties).
 * Both passes assume the cursor is adjacent to idx (no test; true on a sorted list). */
void _dma_SortArbitInfo(int idx)
{
    ArbInfo *r = &s_ArbInfo[idx];
    int j = r->next;
    while (j != -1) {
        ArbInfo *q = &s_ArbInfo[j];
        if (q->prio < r->prio || q->prio == r->prio) {
            q->prev = r->prev;
            r->next = q->next;
            if (r->next != -1) s_ArbInfo[q->next].prev = (int16_t)idx;
            if (r->prev != -1) s_ArbInfo[r->prev].next = (int16_t)j;
            else s_ArbMng.alloc.top = (int16_t)j;
            r->prev = (int16_t)j;
            q->next = (int16_t)idx;
            j = r->next;
        } else {
            j = q->next;
        }
    }
    j = r->prev;
    while (j != -1) {
        ArbInfo *q = &s_ArbInfo[j];
        if (r->prio < q->prio || q->prio == r->prio) {
            q->next = r->next;
            r->prev = q->prev;
            if (r->next != -1) s_ArbInfo[r->next].prev = (int16_t)j;
            if (q->prev != -1) s_ArbInfo[q->prev].next = (int16_t)idx;
            else s_ArbMng.alloc.top = (int16_t)idx;
            r->next = (int16_t)j;
            q->prev = (int16_t)idx;
            j = r->prev;
        } else {
            j = q->prev;
        }
    }
}

/* 0x1194: the arbiter thread (prio 0x2d: it runs only when every higher thread is blocked; a ReleaseWaitThread from
 * strm_Polling ends the DelayThread early and the 4000 us grid restarts from there) */
static void _dma_Thread(void *)
{
    for (;;) {
        dma_Update();
        iopk::DelayThread(4000);
    }
}

/* 0x11b4 (RPC 0x400). The sema parameter block's option word (sp+0x20) is never written (stack garbage, ignored by
 * the kernel: 0 here). Q3: g_DmaThID is stored even when StartThread failed and s_bInited is set after every failure
 * printf. */
void dma_ArbiterInit()
{
    if (s_bInited) return;
    for (int i = 0; i < 8; i++) {
        ArbInfo *a = &s_ArbInfo[i];
        a->prev = (int16_t)(i - 1);
        a->next = (int16_t)(i + 1);
        a->src = a->dst = 0;
        a->remain = a->chunk = 0;
        a->prio = 0;
        a->state = 0;
        a->flags = 0;
        a->stream = -1;
    }
    s_ArbMng.free.num = 8;
    s_ArbInfo[7].next = -1;
    s_ArbMng.free.top = 0;
    s_ArbMng.alloc.top = -1;
    s_ArbMng.last_idx = -1;
    s_ArbMng.alloc.num = 0;
    iopk::SemaParam sp;
    sp.attr = 0;
    sp.option = 0;
    sp.init_count = 1;
    sp.max_count = 1;
    s_DmaSema = iopk::CreateSema(&sp);
    if (s_DmaSema < 0) iopk::Printf("dma_ArbiterInit : ERROR!!! Can not create semaphore...\n");
    iopk::ThreadParam tp;
    tp.attr = 0x02000000;
    tp.option = 0;
    tp.entry = _dma_Thread;
    tp.stack_size = 0x800;
    tp.priority = 0x2d;
    int tid = iopk::CreateThread(&tp);
    if (tid > 0) {
        int r = iopk::StartThread(tid, 0);
        if (r < 0) iopk::Printf("dma_ArbiterInit : ERROR!!! Can not start dma thread...ret=%d\n", r);
        g_DmaThID = tid;
    } else {
        iopk::Printf("dma_ArbiterInit : ERROR!!! Can't create dma thread...\n");
    }
    s_bInited = 1;
}

/* 0x1430 (RPC 0x500): the semaphore is taken, then deleted while held; Q-G5h: s_DmaSema keeps the deleted id; the
 * lists and records stay (a second dma_ArbiterInit re-initialises them). g_DmaThID = -1 is stored before
 * dma_FreePacketBuffer (delay slot). */
void dma_ArbiterQuit()
{
    if (!s_bInited) return;
    iopk::WaitSema(s_DmaSema);
    iopk::DeleteSema(s_DmaSema);
    iopk::TerminateThread(g_DmaThID);
    iopk::DeleteThread(g_DmaThID);
    g_DmaThID = -1;
    dma_FreePacketBuffer();
    s_bInited = 0;
}

/* 0x14b4 (RPC 0x600): one buffer only (a second call returns 0 and keeps the first, Q3). The clamp compare is signed:
 * a negative size passes, AllocSysMemory fails on it -> 0. */
int dma_AllocPacketBuffer(int size)
{
    if (s_dma_pkt != 0) return 0;
    if (size >= 0xffff1) size = 0xffff0;
    uint32_t p = iopk::AllocSysMemory(0, (uint32_t)size, 0);
    s_dma_pkt = p;
    if (p) s_dma_pkt_size = size;
    return p != 0;
}

/* 0x1530 (RPC 0x700, dma_ArbiterQuit): 0 while a DMA is in flight */
int dma_FreePacketBuffer()
{
    if (s_DmaStat != 0) return 0;
    if (s_dma_pkt != 0) iopk::FreeSysMemory(s_dma_pkt);
    s_dma_pkt = 0;
    s_dma_pkt_size = 0;
    return 1;
}

/* 0x1594 (RPC 0x100000: prio -1, f0 1, f1 0): queue `size` bytes of `stream` for the EE address ee. size is clamped
 * to remain + valid (signed); a negative result is returned without a record, size 0 queues a request that finishes
 * at once (Q-G4k); every other refusal returns -1 (Q-G4l). The prio is tested signed, stored as its low 16 bits. */
int dma_RequestTransBuffer(uint32_t stream, uint32_t ee, uint32_t size, int prio, int f0, int f1)
{
    int ret = -1;
    int32_t sz = (int32_t)size;
    int32_t rem, tot;
    int a;
    ArbInfo *r;
    iopk::WaitSema(s_DmaSema);
    if (mstrm_GetAttachDataSize((int)stream) <= 0) {
        iopk::Printf("%s : ERROR!!! ", "dma_RequestTransBuffer");
        iopk::Printf("select stream is not attached...\n");
        goto out;
    }
    if (prio < 0) prio = mstrm_GetPriority((int)stream);
    if (prio < 0) {
        iopk::Printf("%s : ERROR!!! ", "dma_RequestTransBuffer");
        iopk::Printf("Cannot get priority...\n");
        goto out;
    }
    rem = mstrm_GetRemainSize((int)stream);
    tot = (int32_t)((uint32_t)rem + (uint32_t)mstrm_GetValidDataSize((int)stream));
    if (tot < sz) sz = tot;
    if (sz < 0) {
        ret = sz;
        goto out;
    }
    a = _GetArbiterInfo();
    if (a < 0) {
        iopk::Printf("%s : ERROR!!! ", "dma_RequestTransBuffer");
        iopk::Printf("Cannot find free arbiter information...\n");
        goto out;
    }
    r = &s_ArbInfo[a];
    r->dst = ee;
    r->remain = sz;
    r->stream = (int16_t)stream;
    r->prio = (uint16_t)prio;
    r->state = 1;
    r->flags = (r->flags & ~1u) | (uint32_t)(f0 != 0);
    r->flags = (r->flags & ~2u) | ((uint32_t)(f1 != 0) << 1);
    _dma_SortArbitInfo(a);
    ret = sz;
out:
    iopk::SignalSema(s_DmaSema);
    return ret;
}

/* 0x1768 (RPC 0x300000): only the first record of the stream is looked at. In flight (state 2 / 3): cancel flag +
 * abort, dma_Update finishes it; else released at once (state 0 is stored before the release) and the EE notified.
 * Q-G4g: alloc.top is read before the lock. Word 1 of the notification is the original's a3 residue = the free-list
 * head before the release. */
void dma_CancelTransBuffer(uint32_t stream)
{
    int s0 = s_ArbMng.alloc.top;
    iopk::WaitSema(s_DmaSema);
    while (s0 != -1) {
        ArbInfo *r = &s_ArbInfo[s0];
        if (r->stream == (int)stream) {
            if (r->state == 2 || r->state == 3) {
                r->flags = (r->flags & ~4u) | 4;
                s_sdiData.abort = 1;
            } else {
                r->state = 0;
                int16_t f = s_ArbMng.free.top;
                _ReleaseArbiterInfo(s0);
                fsmfstrm_CallRpc(0x10000100, 0, r->stream, f);
            }
            break;
        }
        s0 = r->next;
    }
    iopk::SignalSema(s_DmaSema);
}

/* 0x1860: one arbiter pass (the dma thread, every 4000 us). The alloc.num test is outside the lock (the only part any
 * baseline runs). A: release cancelled records that are not in flight (Q-G4f: the walk re-reads r->next AFTER the
 * release, i.e. continues down the free list, and may release a stale free record a second time); B: complete the
 * record in flight (state 3; abort -> state 4, the next pass's A releases it); C: pick the next one (smallest prio;
 * among equals round robin after last_idx; a cancelled record ends the scan, Q-G4i); D: send one block (packet buffer:
 * GetData copy, never ReleaseData; none: the ring block of GetBufferData, removed in B after the DMA) and wake the
 * update thread. Compares signed. */
void dma_Update()
{
    g_dma_update_calls++;
    g_dma_update_t = iopk::Now();
    int s2 = -1, s0 = -1;
    int len = 0;
    int s4 = 0x10000;
    if (s_ArbMng.alloc.num <= 0) return;
    iopk::WaitSema(s_DmaSema);
    /* A. cancel sweep */
    for (int a = s_ArbMng.alloc.top; a != -1;) {
        ArbInfo *r = &s_ArbInfo[a];
        if ((r->flags & 4) && r->state != 2 && r->state != 3) {
            r->state = 0;
            int16_t f = s_ArbMng.free.top;
            _ReleaseArbiterInfo(a);
            fsmfstrm_CallRpc(0x10000100, 0, r->stream, f);
        }
        a = r->next;
    }
    /* B. the record in flight */
    for (int a = s_ArbMng.alloc.top; a != -1; a = s_ArbInfo[a].next)
        if (s_ArbInfo[a].state == 3) {
            s2 = a;
            break;
        }
    if (s2 != -1) {
        if (!_SifTransSync()) {
            iopk::SignalSema(s_DmaSema);
            return;
        }
        ArbInfo *r = &s_ArbInfo[s2];
        if (s_sdiData.abort == 0) {
            r->state = 4;
            r->remain = (int32_t)((uint32_t)r->remain - (uint32_t)r->chunk);
            if (s_dma_pkt == 0) mstrm_ReleaseData(r->stream, r->chunk);
            if (r->remain <= 0) {
                r->state = 5;
                /* word 1 = the original's a3 residue: s2 (the B walk) with the packet buffer, whatever
                 * mstrm_ReleaseData left without it (0 here); the EE ignores it */
                fsmfstrm_CallRpc(0x10000100, 0, r->stream, s_dma_pkt != 0 ? s2 : 0);
                _ReleaseArbiterInfo(s2);
            } else if (!(r->flags & 4)) {
                if (r->flags & 1) r->dst += (uint32_t)r->chunk;
                if (r->flags & 2) {
                    len = mstrm_GetRemainSize(r->stream);
                    if (len > 0) {
                        s0 = s2;
                        r->state = 2;
                    }
                }
            }
        } else {
            r->state = 4;
            s_sdiData.abort = 0;
        }
    }
    /* C. pick */
    if (s0 == -1) {
        int s3 = s_ArbMng.last_idx != -1 ? s_ArbInfo[s_ArbMng.last_idx].prio : 0;
        for (s2 = s_ArbMng.alloc.top; s2 != -1; s2 = s_ArbInfo[s2].next) {
            ArbInfo *r = &s_ArbInfo[s2];
            if (s4 < (int)r->prio) break;
            if (r->flags & 4) break;
            if (r->state != 1 && r->state != 4) continue;
            len = mstrm_GetValidDataSize(r->stream);
            if (len <= 0) continue;
            int pr = r->prio;
            if (pr < s4) {
                s0 = s2;
                s4 = pr;
            } else if (pr == s4 && pr == s3 && r->prev != -1 && s_ArbInfo[r->prev].prio == pr &&
                       r->prev == s_ArbMng.last_idx) {
                s0 = s2;
            }
        }
    }
    /* D. send */
    if (s0 != -1) {
        ArbInfo *r = &s_ArbInfo[s0];
        len = r->remain;
        if (s_dma_pkt != 0) {
            if (s_dma_pkt_size < len) len = s_dma_pkt_size;
            len = mstrm_GetData(r->stream, s_dma_pkt, len);
            r->src = s_dma_pkt;
        } else {
            if (len >= 0x4001) len = 0x4000;
            uint32_t p = mstrm_GetBufferData(r->stream, &len);
            if (p) r->src = p;
            else len = -1;
        }
        if (len > 0) {
            r->chunk = len;
            r->state = 3;
            _SifTransBuffer(r);
            s_ArbMng.last_idx = (int16_t)s0;
            iopk::SignalSema(g_iUpdateSema);
        } else {
            r->state = 1;
        }
    }
    iopk::SignalSema(s_DmaSema);
}

/* 0x1d4c (RPC 0x200000): the first record of the stream: 1 when finished (5) or cancelled / free (0), else 0; no
 * record -> 1 */
int dma_Sync(uint32_t stream)
{
    int s0 = 1;
    iopk::WaitSema(s_DmaSema);
    for (int i = s_ArbMng.alloc.top; i != -1; i = s_ArbInfo[i].next) {
        const ArbInfo *r = &s_ArbInfo[i];
        if (r->stream == (int)stream) {
            s0 = (r->state == 5 || r->state == 0) ? 1 : 0;
            break;
        }
    }
    iopk::SignalSema(s_DmaSema);
    return s0;
}

}  // namespace mfstrm
