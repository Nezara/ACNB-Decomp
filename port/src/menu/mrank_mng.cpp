/* src/menu/mrank_mng.cpp — training rank table (OLMENU, menu data entry 12): int[category][detail][level][5]
 * point thresholds; the rank of a score is the first threshold it is "good" against (1..5), 6 = none / no score. */
#include <string.h>

int MRankMng_CalcRank(int point)
{
    int *t, i;

    PS2_FRAME(0x50);
    if (point == -1) return 6;
    t = s_stRankMng.pDataHead +
        ((MenuMng.category_index * 5 + MenuMng.detail_index) * 5 + MenuMng.level_index) * 5;
    for (i = 0; i < 5; i++)
        if (MResultMng_IsGoodPoint(point, t[i]) != 0) return i + 1;
    return 6;
}

int MRankMng_Init(void)
{
    memset(&s_stRankMng, 0, 4);
    s_stRankMng.pDataHead = (int *)DF_MDATA_GetData(MenuMng.dataTbl[0], 0xc);
    return 0;
}
