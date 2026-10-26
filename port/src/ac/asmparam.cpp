/* src/ac/asmparam.cpp - the assembly-parameter table (g_asmparam, StAcAsmParam, 0x320 bytes from disc file 0x5b).
 * AsmParam_Load runs at boot (__InitApp) and at every stage event setup (DF_StageEventProcInit). */
#include "acnb/port.h"

/* 1 on success. Only the first 0x320 bytes of the 2 KB read buffer are used. */
int AsmParam_Load(void)
{
    PS2_FRAME(0x810);
    PS2_STACK(char[2048], bufRead, 0x10);   /* filled by the interpreted File_Read */
    if (!File_Read(0x5b, 0x320, bufRead, 1)) return 0;
    CopyMemory(&g_asmparam, bufRead, 0x320);
    return 1;
}
