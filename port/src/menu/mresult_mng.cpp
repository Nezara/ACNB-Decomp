/* src/menu/mresult_mng.cpp - the mission / training result window manager (MResultMng_Play builds the result
 * table pages from the result-data chunk of the menu archive, judges new values against the past ones and shows
 * the "get class" popup). Only MResultMng_Init is ported: the menu data loader (DF_MENU_ReadDataLoop) calls it
 * in every menu, Versus included. The other functions of the file are out of scope and stay interpreted. */
#include <string.h>

#ifdef ACNB_SOURCEPORT
struct stMResultMng s_stResultMng;   /* OLMENU 0x1ef97b0 (BSS) */
char *s_pResultDataHead;             /* main sdata 0x27d0b4 */
#endif

int MResultMng_Init(void)
{
    PS2_FRAME(0x10);
    memset(&s_stResultMng, 0, 0x38);
    s_stResultMng.offset.x = 0x30;
    s_stResultMng.offset.y = 0x62;
    s_pResultDataHead = (char *)DF_MDATA_GetData(MenuMng.dataTbl[0], 0xb);
    return 0;
}
