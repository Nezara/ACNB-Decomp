/* src/gfx/font.cpp - DF_FontSpriteKick: one textured two-vertex sprite (a message-window glyph, stat MsgWnd_Draw) as a
 * GS list, with the texture / alpha / test registers first when `mode` is set. Render tag: TEXT,
 * RQ_OBJID(RQ_OC_HUD_ELEM, 11) sub 11 (message window; HUD element ids 1-10 are taken). */
#include "acnb/port.h"

void DF_FontSpriteKick(int *rgb, int (*xyz)[4], struct struct_1f802d8d tex, float *ST, int prim, int mode)
{
    PS2_FRAME(0x70);
    unsigned long long t;
    memcpy(&t, &tex, 8);
    PS2_RQ_SCOPE(RQ_K_TEXT, RQ_OBJID(RQ_OC_HUD_ELEM, 11), 11);   /* render tag (host only): message window text */
    GS_NewList();
    if (mode != 0) {
        GS_SetRegister(0x3f, 0);   /* TEXFLUSH */
        GS_SetRegister(6, t);      /* TEX0_1 */
        GS_SetRegister(0x14, 0);   /* TEX1_1 */
        GS_SetRegister(8, 0);      /* CLAMP_1 */
        GS_SetRegister(0x1c, 4);   /* TEXA */
        GS_SetRegister(0x16, t);   /* TEX2_1 */
        GS_SetALPHA_1(0, 2, 0, 1, 0);
        GS_SetTEST_1(0, 0, 0, 0, 0, 0, 1, 1);
    }
    GS_Begin((unsigned int)prim, 2, 0x412, 3);   /* ST, RGBAQ, XYZ2 */
    GS_TexCoord2fv(ST);
    GS_Color4iv(rgb);
    GS_Vertex4iv(xyz[0]);
    GS_TexCoord2fv(ST + 2);
    GS_Color4iv(rgb);
    GS_Vertex4iv(xyz[1]);
    GS_End();
    GS_FlushList();
}
