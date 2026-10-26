/* src/stage/stage_info.cpp - real map number -> logical stage number (100 logical stages x 8 map ids, loaded at boot
 * from CD file 800); the logical number keys the destroyed-object memory. */
#include "acnb/sdk.h"

/* first logical stage (1-based) listing the map, else 0 */
int StageInfo_RealToLogicalStageNo(unsigned int rstage)
{
    for (int i = 0; i < 100; i++)
        for (int j = 0; j < 8; j++)
            if (rstage == s_tblMapLtR[i].mapRelReal[j]) return i + 1;
    return 0;
}

int StageInfo_Init(void)
{
    PS2_FRAME(0x10);
    return __LoadLogicalMapInfo() != 0;
}

static int __LoadLogicalMapInfo(void)
{
    PS2_FRAME(0x810);
    PS2_STACK(char[0x800], bufRead, 0x10);
    if (!File_Read(800, 800, bufRead, 1)) return 0;
    CopyMemory(s_tblMapLtR, bufRead, 0x320);
    return 1;
}
