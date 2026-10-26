/* src/menu/mresult_bridge.cpp - a small global mailbox between the mission-event code (which records result items
 * while a mission runs) and the result screens (MResultMng): a byte of flag bits (s_stMResultBridge.flag) and 64 int
 * result values (s_aBridgeData) with a write index (s_indexBridgeData). AddData appends, Get/SetAtData access by
 * index, Init resets the index at boot and at every mission start (the flags and the data are not cleared).
 * No bounds checks (original behaviour). Lives in the main ELF (0x1ff700), not in the menu overlay. */
#include "acnb/local/src/menu/mresult_bridge.h"

char MResultBridge_GetFlag(void)
{
    return s_stMResultBridge.flag;
}

/* mode 0 sets the bits, any other mode clears them (the clear always runs first) */
void MResultBridge_SetFlag(char flag, char mode)
{
    s_stMResultBridge.flag &= (char)~flag;
    if (mode == 0) s_stMResultBridge.flag |= flag;
}

int *MResultBridge_GetDataBuff(void)
{
    return (int *)s_aBridgeData;
}

int MResultBridge_GetAtData(int index)
{
    return s_aBridgeData[index].data;
}

void MResultBridge_SetAtData(int index, int data)
{
    s_aBridgeData[index].data = data;
}

void MResultBridge_AddData(int data)
{
    s_aBridgeData[s_indexBridgeData].data = data;
    s_indexBridgeData = s_indexBridgeData + 1;
}

void MResultBridge_Init(void)
{
    s_indexBridgeData = 0;
}
