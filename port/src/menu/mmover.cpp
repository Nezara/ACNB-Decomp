/* src/menu/mmover.cpp — menu "movers" (OLMENU): a rect, float vec4, int or int vec2 value that moves
 * from its current value to a target over nMaxCount frames. X_Move starts a move (Prev = Cur), X_Update
 * advances one frame (1 while moving, 0 when done), X_SetValue / Mover_SetPos jump to a value. The
 * interpolation itself is in mfunc.cpp (DF_MFUNC_CalcInter*). */

void MoverRect_SetValue(struct _MOVERRECT *pMover, struct stMenuRect *rect)
{
    pMover->nCount = 0;
    pMover->nMaxCount = 0;
    pMover->Cur = *rect;
}

void MoverRect_Move(struct _MOVERRECT *pMover, struct stMenuRect *rect, int count)
{
    if (count <= 0) count = 1;
    pMover->Prev = pMover->Cur;
    pMover->Next = *rect;
    pMover->nCount = 0;
    pMover->nMaxCount = count;
}

int MoverRect_Update(struct _MOVERRECT *pMover)
{
    PS2_FRAME(0x10);
    if (pMover->nMaxCount <= 0) return 0;
    pMover->nCount++;
    if (pMover->nCount < 0) {
        pMover->Cur = pMover->Prev;
        return 1;
    }
    if (pMover->nCount < pMover->nMaxCount) {
        DF_MFUNC_CalcInterRect(&pMover->Cur, &pMover->Prev, &pMover->Next, pMover->nCount, pMover->nMaxCount);
        return 1;
    }
    pMover->nMaxCount = 0;
    pMover->Cur = pMover->Next;
    return 0;
}

void MoverF4_SetValue(struct _MOVERF4 *pMover, float *next)
{
    pMover->nCount = 0;
    pMover->nMaxCount = 0;
    DF_MFUNC_CopyMemoryC(pMover->Cur, next, 0x10);
}

void MoverF4_Move(struct _MOVERF4 *pMover, float *next, int count)
{
    PS2_FRAME(0x30);
    if (count <= 0) count = 1;
    memcpy(pMover->Prev, pMover->Cur, 0x10);
    DF_MFUNC_CopyMemoryC(pMover->Next, next, 0x10);
    pMover->nCount = 0;
    pMover->nMaxCount = count;
}

int MoverF4_Update(struct _MOVERF4 *pMover)
{
    PS2_FRAME(0x10);
    if (pMover->nMaxCount <= 0) return 0;
    pMover->nCount++;
    if (pMover->nCount < 0) {
        memcpy(pMover->Cur, pMover->Prev, 0x10);
        return 1;
    }
    if (pMover->nCount < pMover->nMaxCount) {
        DF_MFUNC_CalcInterVector(pMover->Cur, pMover->Prev, pMover->Next, pMover->nCount, pMover->nMaxCount);
        return 1;
    }
    pMover->nMaxCount = 0;
    memcpy(pMover->Cur, pMover->Next, 0x10);
    return 0;
}

void MoverF4_Init(struct _MOVERF4 *pMover)
{
    ZeroMemory(pMover, 0x40);
}

void MoverI_SetValue(struct _MOVERI *pMover, int value)
{
    pMover->nCount = 0;
    pMover->nMaxCount = 0;
    pMover->Cur = value;
}

void MoverI_Move(struct _MOVERI *pMover, int next, int count)
{
    if (count <= 0) count = 1;
    pMover->Prev = pMover->Cur;
    pMover->Next = next;
    pMover->nCount = 0;
    pMover->nMaxCount = count;
}

int MoverI_Update(struct _MOVERI *pMover)
{
    if (pMover->nMaxCount <= 0) return 0;
    pMover->nCount++;
    if (pMover->nCount < 0) {
        pMover->Cur = pMover->Prev;
        return 1;
    }
    if (pMover->nCount < pMover->nMaxCount) {
        pMover->Cur = (pMover->Prev * (pMover->nMaxCount - pMover->nCount) + pMover->nCount * pMover->Next) /
                      pMover->nMaxCount;
        return 1;
    }
    pMover->nMaxCount = 0;
    pMover->Cur = pMover->Next;
    return 0;
}

void MoverI_Init(struct _MOVERI *pMover)
{
    ZeroMemory(pMover, 0x14);
}

void Mover_SetPos(struct _MOVER *pMover, struct struct_214acd28 *pPosNext)
{
    pMover->nCount = 0;
    pMover->nMaxCount = 0;
    pMover->PosCur = *pPosNext;
}

void Mover_Move(struct _MOVER *pMover, struct struct_214acd28 *pPosNext, int count)
{
    if (count <= 0) count = 1;
    pMover->PosPrev = pMover->PosCur;
    pMover->PosNext = *pPosNext;
    pMover->nCount = 0;
    pMover->nMaxCount = count;
}

int Mover_Update(struct _MOVER *pMover)
{
    PS2_FRAME(0x10);
    if (pMover->nMaxCount <= 0) return 0;
    pMover->nCount++;
    if (pMover->nCount < pMover->nMaxCount) {
        DF_MFUNC_CalcInterIVec2(&pMover->PosCur, &pMover->PosPrev, &pMover->PosNext, pMover->nCount, pMover->nMaxCount);
        return 1;
    }
    pMover->nMaxCount = 0;
    pMover->PosCur = pMover->PosNext;
    return 0;
}

/* Unsigned test: a negative nMaxCount counts as moving. */
int Mover_IsMove(struct _MOVERBASE *p)
{
    return p->nMaxCount != 0;
}
