/* src/stage/destobjmem.cpp - destroyed-object memory: a state byte per (logical stage 1..100, destructible 1..64)
 * that survives between missions (saved with the game). The store s_dmem holds only 0x200 bytes (8 stages), so a
 * logical stage >= 9 indexes past it into the item data that follows (kept as the original indexes it). */
#include "acnb/sdk.h"

static inline unsigned char *destobj_row(int lstage)
{
    return (unsigned char *)&s_dmem + (lstage - 1) * 64;
}

void DestObjMem_Load(StDestroyObjSave *psave)
{
    CopyMemory(&s_dmem, psave, 0x200);
}

void DestObjMem_Save(StDestroyObjSave *psave)
{
    CopyMemory(psave, &s_dmem, 0x200);
}

/* apply the stage's modification file (500 + id): 1 clear, 2 set, 3 toggle (logical stage 100 is rejected here only) */
int DestObjMem_ReadMissionState(unsigned int rstage, unsigned char idDestModify)
{
    PS2_FRAME(0x870);
    PS2_STACK(char[0x800], bufRead, 0x70);
    PS2_STACK(unsigned char[0x40], dmemmod, 0x30);
    int lstage = StageInfo_RealToLogicalStageNo(rstage);
    int cdvdid = idDestModify + 500;
    if (idDestModify == 0 || lstage <= 0 || !(lstage < 100)) return 1;
    if (!File_IsExist(cdvdid)) return 1;
    unsigned char *base = (lstage > 0 && lstage < 101) ? destobj_row(lstage) : 0;
    if (!File_NlRead(cdvdid, 0x40, bufRead)) return 0;
    CopyMemory(dmemmod, bufRead, 0x40);
    for (int i = 0; i < 0x40; i++) {
        switch (dmemmod[i]) {
        case 3: base[i] = base[i] == 0; break;
        case 2: base[i] = 1; break;
        case 1: base[i] = 0; break;
        }
    }
    return 1;
}

unsigned char DestObjMem_GetDestObjCnt(int lstage, int idDestObj)
{
    if (!(lstage > 0 && lstage < 101)) return 0;
    if (!(idDestObj > 0 && idDestObj < 0x41)) return 0;
    return destobj_row(lstage)[idDestObj - 1];
}

void DestObjMem_MemoryDestObj(int lstage, int idDestObj, unsigned char cntInit)
{
    if (!(lstage > 0 && lstage < 101)) return;
    if (!(idDestObj > 0 && idDestObj < 0x41)) return;
    destobj_row(lstage)[idDestObj - 1] = cntInit;
}

int DestObjMem_Init(void)
{
    PS2_FRAME(0x10);
    ZeroMemory8b(&s_dmem, 0x200);
    return 1;
}
