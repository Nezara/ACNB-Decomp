/* src/sfx/blur.cpp - full-screen "blur" overlay: up to two filters (Blur[2]) each redraw the frame buffer (TBP 0,
 * CT24, as a texture) over itself with the filter's colour and one of three alpha modes, as a GS list. Gated by
 * BlurEnable, which only InitBlur writes (0) as far as found, so in Versus DF_EFF_DrawBlur only tests the gate and
 * draw_blur is unreached. Render tag: POST, RQ_OBJID(RQ_OC_NONE, 0) sub 3 (sub 1 = screen fill, 2 = lens flare). */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/local/src/sfx/blur.h"

#ifdef ACNB_SOURCEPORT
unsigned long long s_tex0 = 0x0000000224120000ull;
#endif

/* one filter; color[3] (alpha) == 0 switches it off */
static void draw_blur(struct FilterSt *blur)
{
    PS2_FRAME(0x50);
    if (blur->color[3] == 0) return;
    PS2_RQ_SCOPE(RQ_K_POST, RQ_OBJID(RQ_OC_NONE, 0), 3);   /* render tag (host only): blur filter */
    GS_NewList();
    GS_SetTEXFLUSH();
    GS_SetCLAMP_1(1, 1, 0, 0, 0, 0);
    GS_SetTEST_1(1, 1, 0, 1, 0, 0, 0, 1);
    switch (blur->blend) {
    case 2: GS_SetALPHA_1(2, 0, 0, 1, 0); break;
    case 1: GS_SetALPHA_1(0, 2, 0, 1, 0); break;
    default: GS_SetALPHA_1(0, 1, 0, 1, 0); break;
    }
    s_tex0 = 0x0000000624120000ull;   /* TBP 0, TBW 8, PSMCT24, TW 9, TH 8, TCC 1: the frame buffer */
    struct_f386fb29 tex0;
    memcpy(&tex0, &s_tex0, 8);
    GS_SetTEX0_1(tex0);
    struct_d42f65aa tex1;
    memcpy(&tex1, &s_tex1, 8);
    GS_SetTEX1_1(tex1);
    alignas(16) int xyz[2][4];
    alignas(16) int uv[4];
    xyz[0][0] = 0x7000;
    xyz[0][1] = 0x7900;
    xyz[0][2] = 0xffffff;
    xyz[0][3] = 0;
    xyz[1][0] = xyz[0][0] + 0x2000;
    xyz[1][1] = xyz[0][1] + 0xe00;
    xyz[1][2] = xyz[0][2];
    xyz[1][3] = xyz[0][3];
    uv[0] = 0;
    uv[1] = 0;
    uv[2] = uv[0] + 0x2010;
    uv[3] = uv[1] + 0xe10;
    GS_DrawRectStripUvN(xyz, uv, blur->color, 1, 1);
    GS_FlushList();
}

void DF_EFF_DrawBlur(void)
{
    PS2_FRAME(0x30);
    if (BlurEnable == 0) return;
    sceGsSyncPath(0, 0);
    for (int i = 0; i < 2; i++) draw_blur(&Blur[i]);
}

void DF_EFF_InitBlur(void)
{
    BlurEnable = 0;
    for (int i = 0; i < 2; i++) {
        Blur[i].fmode = 0;
        Blur[i].blend = 0;
        Blur[i].ofs = 0.0f;   /* sw zero */
        Blur[i].color[0] = 0x80;
        Blur[i].color[1] = 0x80;
        Blur[i].color[2] = 0x80;
        Blur[i].color[3] = 0x80;
    }
}
