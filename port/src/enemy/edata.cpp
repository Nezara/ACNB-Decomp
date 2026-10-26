/* src/enemy/edata.cpp (OLSORTIE) - the enemy joint-control table: Edata_ReadJconData loads file 1999 (header
 * {size, count, ..} then count {num, offset} pairs, offsets relative to the file start) into a Memm allocation and
 * turns it in place into {jdat pointer, num} entries. Called once per stage from ENEMY_AllEnemyInit (Versus too,
 * with 0 enemies). DFene_GetJConTab (garage3 layer 2) is the bounds-checked lookup. */
#include "acnb/port.h"
#include "acnb/sdk.h"

static int __SetJconData(char *pFileBuf)
{
    PS2_FRAME(0x30);
    int size = ((int *)pFileBuf)[0];
    s_JconTabNum = ((int *)pFileBuf)[1];   /* stays set if the allocation fails */
    s_pJconTab = (_jdat_tab_tag_ *)Memm_Alloc(1, size - 0x10, 0x10);
    if (!s_pJconTab) return 0;
    int *src = (int *)(pFileBuf + 0x10);
    memcpy(s_pJconTab, src, size - 0x10);
    /* the entries are rebuilt in place from the file words (entry i overwrites the copied pair i) */
    _jdat_tab_tag_ *tab = s_pJconTab;
    for (int i = 0; i < s_JconTabNum; i++, src += 2, tab++) {
        tab->num = src[0];
        int off = src[1] - 0x10;
        tab->jdat = tab->num ? (_jcon_data2 *)((char *)s_pJconTab + off) : 0;
    }
    return 1;
}

int Edata_ReadJconData(void)
{
    PS2_FRAME(0x20);
    char *pbuf = g_pbufRead;
    s_JconTabNum = 0;
    s_pJconTab = 0;
    if (Cdvd_GetSize2048(0x7cf) <= 0) return 0;
    if (File_Read(0x7cf, -1, pbuf, 1) == 0) return 0;
    return __SetJconData(pbuf) != 0;
}

_jdat_tab_tag_ *DFene_GetJConTab(int jcon_no)
{
    if (jcon_no < 0 || jcon_no >= s_JconTabNum) return 0;
    return s_pJconTab + jcon_no;
}
