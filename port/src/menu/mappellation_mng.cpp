/* src/menu/mappellation_mng.cpp — pilot appellations (rank titles, OLMENU). Menu data entry 10 holds {u8 numData,
 * 3 bytes} followed by 32-byte entries {int point, char name[28]}; a player's title is entry
 * g_gamedata[player].indexAppellation. CreateMessage promotes player 0 to the highest title the total points reach
 * and builds the announcement text. */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
/* main-ELF sdata "%s" at 0x27b3f0 (nodwarf.h), the format of the announcement sprintf */
char mappel_fmt_s[4] = "%s";
#endif

/* player 0 holds the last title */
char MAppellationMng_IsTopAppellation(void)
{
    return g_gamedata[0].indexAppellation == s_stAppellationMng.pDataHead->numData - 1;
}

char *MAppellationMng_GetAppellation(int player)
{
    return s_stAppellationMng.pData[(int)g_gamedata[player].indexAppellation].name;
}

char *MAppellationMng_GetMessage(void)
{
    return s_stAppellationMng.pMessage;
}

/* From the top title down to (excluding) the current one: the first whose point the total reaches becomes player 0's
 * title and its message ("<header>" + the localised "... %s ..." line). Returns 1 if the title changed. */
char MAppellationMng_CreateMessage(void)
{
    PS2_FRAME(0x140);
    PS2_STACK(char[0x100], line, 0x40);
    struct struct_f34d4a7d *e;
    char changed = 0;
    int total, cur, i;

    if (s_stAppellationMng.pMessage) {
        DF_MMM_Free(0, s_stAppellationMng.pMessage);
        s_stAppellationMng.pMessage = 0;
    }
    s_stAppellationMng.pMessage = (char *)DF_MMM_Alloc(0, 0x200);
    ps2_sprintf_s(s_stAppellationMng.pMessage, mappel_fmt_s, (const char *)DF_MSTR_GetName(MenuMng.language, 0, 0xdc758));
    total = MUtil_GetTotalPoint();
    cur = g_gamedata[0].indexAppellation;
    for (i = s_stAppellationMng.pDataHead->numData - 1; cur < i; i--) {
        e = &s_stAppellationMng.pData[i];
        if (total < e->point) continue;
        ps2_sprintf_s(line, (const char *)DF_MSTR_GetName(MenuMng.language, 0, 0xdc759), s_stAppellationMng.pData[i].name);
        strcat(s_stAppellationMng.pMessage, line);
        changed = 1;
        g_gamedata[0].indexAppellation = (char)i;
        break;
    }
    return changed;
}

void MAppellationMng_Cleanup(void)
{
    if (s_stAppellationMng.pMessage) {
        DF_MMM_Free(0, s_stAppellationMng.pMessage);
        s_stAppellationMng.pMessage = 0;
    }
}

/* no check of the data pointer */
int MAppellationMng_Init(void)
{
    memset(&s_stAppellationMng, 0, 0x10);
    s_stAppellationMng.pDataHead = (struct stAppellationDataHead *)DF_MDATA_GetData(MenuMng.dataTbl[0], 10);
    s_stAppellationMng.pData = (struct struct_f34d4a7d *)((char *)s_stAppellationMng.pDataHead + 4);
    return 1;
}
