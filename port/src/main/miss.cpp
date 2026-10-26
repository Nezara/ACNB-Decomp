/* src/main/miss.cpp - the mission table (s_miTable, 150 StMissionInfo from disc file 0x39). Native so far: Miss_Init
 * (boot: load the table, reset the training records); the rest of miss.c stays interpreted. */
#include "acnb/port.h"

/* 1 on success; only 0x2580 bytes of the 10 KB read buffer are used */
int Miss_Init(void)
{
    PS2_FRAME(0x2810);
    PS2_STACK(char[10240], buf, 0x10);   /* filled by the interpreted File_Read */
    if (!File_Read(0x39, 0x2580, buf, 1)) return 0;
    CopyMemory(s_miTable, buf, 0x2580);
    TrainingRecordReset();
    TrainingTrendInit();
    return 1;
}
