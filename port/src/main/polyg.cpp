/* src/main/polyg.cpp - the sortie's single polygon-movie player instance (g_PgpMng): thin wrappers over pgplay.c used
 * by the sortie loop (PolygProc each frame), Stage_Load (PolygInit / PolygRegist) and the event scripts (PolygStart). */
#include "acnb/port.h"

void PolygProc(void)
{
    PGP_Proc(&g_PgpMng);
}

void PolygStart(int pgbno, int loop)
{
    PS2_FRAME(0x20);
    if (PGP_SetPGBFile(&g_PgpMng, pgbno) == 0) PGP_Start(&g_PgpMng, loop);
}

void *PolygRegist(void *rp)
{
    PS2_FRAME(0x20);
    PGP_Create(&g_PgpMng);
    return PGP_RegistPGBPack(&g_PgpMng, (PackPGBHeadSt *)rp);
}

void PolygInit(void)
{
    memset(&g_PgpMng, 0, 0x1494);
}
