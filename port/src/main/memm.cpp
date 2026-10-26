/* src/main/memm.cpp — the game's linear heap over one static buffer (s_bufGeneral, 0x170a000 bytes) split into four
 * pools {flag, size, top, next}: 1 = model / general heap (bump allocator; Memm_Alloc(1, ..) is what almost everything
 * uses), 2 / 3 = the two matrix buffers, 0 = an empty marker. Memm_InitGeneralBuffer sets it up at boot,
 * Memm_AllocStageBuffer re-partitions it per sortie: [pool3 matrix][pool2 matrix][pool0][pool1 model heap]. The special
 * read buffer is carved off the END of pool 1 for streaming loads; the IRX pack buffer and g_pbufRead are fixed offsets.
 *
 * ABI: callers pass Memm_Alloc a debug file / line in a3 / t0 (ignored). s_pmngBuffer is not null-checked except by
 * the replay buffer getters (original). */
#include "acnb/sdk.h"

static_assert(sizeof(StBufferManage) == 0x48, "StBufferManage");

int Memm_GetReplayBuffSize(void)
{
    return s_pmngBuffer ? s_pmngBuffer->sizeReplayBuff : -1;
}

void *Memm_GetReplayBuff(void)
{
    return s_pmngBuffer ? s_pmngBuffer->pbufReplay : 0;
}

void *Memm_GetNext(void)
{
    return s_pmngBuffer->meminfo[1].next;
}

int Memm_GetRest(void)
{
    struct_1e7a2ed8 *m = &s_pmngBuffer->meminfo[1];
    return m->size - (int)(m->next - m->top);
}

/* type 0 top, 1 next, 2 used, 3 free, 4 size */
int Memm_GetInfo(int memid, int type)
{
    if (memid < 0 || memid >= 4) return -1;
    struct_1e7a2ed8 *m = &s_pmngBuffer->meminfo[memid];
    switch (type) {
    case 3: return m->size - (int)(m->next - m->top);
    case 4: return m->size;
    case 2: return (int)(m->next - m->top);
    case 1: return (int)(intptr_t)m->next;
    case 0: return (int)(intptr_t)m->top;
    }
    return -1;
}

void Memm_ReleaseSpecialReadBuffer(void)
{
    if (s_pbufSpUsed && s_sizeSpBufUsed) {
        ZeroMemory(s_pbufSpUsed, s_sizeSpBufUsed);
        s_pbufSpUsed = 0;
        s_sizeSpBufUsed = 0;
    }
}

/* (sic) a previous special buffer is replaced without being released */
void *Memm_GetSpecialReadBuffer(int size)
{
    struct_1e7a2ed8 *m = &s_pmngBuffer->meminfo[1];
    char *end = m->top + m->size;
    if (size < (int)(end - m->next)) {
        s_sizeSpBufUsed = size;
        s_pbufSpUsed = end - size;
        return end - size;
    }
    return 0;
}

void Memm_ReleaseIrxPackFileReadBuffer(void)
{
    memset(s_bufGeneral + 0x158a000, 0, 0x180000);
}

void *Memm_GetIrxPackFileReadBuffer(void)
{
    return s_bufGeneral + 0x158a000;
}

/* (sic) the fit test uses the unaligned next, so an aligned block can overrun the pool by up to align - 1 bytes */
void *Memm_Alloc(int memid, int size, int align)
{
    if (size <= 0) return 0;
    if (memid < 0 || memid >= 4) return 0;
    struct_1e7a2ed8 *m = &s_pmngBuffer->meminfo[memid];
    if (m->size - (int)(m->next - m->top) < size) return 0;
    if (align > 1) {
        /* signed div / mult on the PS2 address (always positive there) */
        uintptr_t n = (uintptr_t)m->next;
        m->next = (char *)(((n + align - 1) / (uintptr_t)align) * (uintptr_t)align);
    }
    char *p = m->next;
    m->next = p + size;
    return p;
}

void Memm_AllocStageBuffer(int mtxsize)
{
    memset(&s_mngBuffer[0], 0, 0x90);
    s_pmngBuffer = &s_mngBuffer[0];
    s_sizeMatrixBuff = mtxsize;
    s_sizeModelBuff = 0x160a000;
    s_sizeModelBuff = s_sizeModelBuff - mtxsize * 2;
    StBufferManage *mg = s_pmngBuffer;
    mg->meminfo[3].size = mtxsize;
    mg->meminfo[3].next = s_bufGeneral;
    mg->meminfo[3].top = s_bufGeneral;
    ZeroMemory32b(s_bufGeneral, (unsigned int)mtxsize >> 2);

    int msize = s_sizeMatrixBuff;
    char *base = s_bufGeneral + mtxsize;
    mg = s_pmngBuffer;
    mg->meminfo[2].size = msize;
    mg->meminfo[2].next = base;
    mg->meminfo[2].top = base;
    ZeroMemory32b(base, (unsigned int)msize >> 2);

    base += msize;
    mg = s_pmngBuffer;
    mg->meminfo[0].size = 0;
    mg->meminfo[0].next = base;
    mg->meminfo[0].top = base;
    ZeroMemory32b(base, 0);

    int model = s_sizeModelBuff;
    mg = s_pmngBuffer;
    mg->meminfo[1].size = model;
    mg->meminfo[1].next = base;
    mg->meminfo[1].top = base;
    ZeroMemory32b(base, (unsigned int)model >> 2);

    s_pmngBuffer->pbufReplay = ReplayBuffer;
    s_pmngBuffer->sizeReplayBuff = 0x100000;
}

void Memm_InitGeneralBuffer(void)
{
    ZeroMemory32b(s_bufGeneral, 0x5c2800);
    s_sizeModelBuff = 0x800000;
    memset(&s_mngBuffer[0], 0, 0x90);
    s_pmngBuffer = &s_mngBuffer[0];
    int model = s_sizeModelBuff;
    StBufferManage *mg = s_pmngBuffer;
    mg->meminfo[1].size = model;
    mg->meminfo[1].next = s_bufGeneral;
    mg->meminfo[1].top = s_bufGeneral;
    ZeroMemory32b(s_bufGeneral, (unsigned int)model >> 2);
    char *base = s_bufGeneral + model;
    mg = s_pmngBuffer;
    mg->meminfo[0].size = 0;
    mg->meminfo[0].next = base;
    mg->meminfo[0].top = base;
    ZeroMemory32b(base, 0);
    g_pbufGlobal = s_bufGeneral;
    g_pbufRead = s_bufGeneral + 0x160a000;
}
