/* src/stat/acfnt.cpp - HUD font texture registers and line helper (whole file). */
#include "acnb/sdk.h"

/* 2D line at the front z (0xfffe0), primitive LINE with Gouraud / alpha blend */
void DF_ACFONT_DrawLine(int *rgb, int (*xyz)[4])
{
    xyz[0][2] = 0xfffe0;
    xyz[0][3] = 0;
    xyz[1][2] = 0xfffe0;
    xyz[1][3] = 0;
    DF_Draw2DLine_Color(rgb, xyz, 0x20004041);
}

/* TEX0 values of the HUD font pages */
void DF_ACFONT_Init(void)
{
    for (int i = 0; i < 5; i++) AC_FONT_TEXTURE[i] = 0x8007ed0621413940ull;
    AC_FONT_TEXTURE[5] = 0x8007ed25e14139c0ull;
    AC_FONT_TEXTURE[6] = 0x8007ee059db09980ull;
}
