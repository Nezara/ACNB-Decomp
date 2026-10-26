/* src/main/nowld.cpp - the "now loading" screen. NL_LoadTexture loads its two TMX textures (file ids 0x1e / 0x1f)
 * once; NL_LoopNowLoading runs the flip / pad / draw loop for two passes (nlloop 1) or until the async file read is
 * done (nlloop 0); NL_ReadData is the file read most loaders use (blocking, or async with the loading loop).
 * Nl_Draw paints the loading bar: a black screen, the scrolling bar texture (s_tex0Nl[2], tinted, 0..300 frame cycle)
 * and the static frame texture (s_tex0Nl[1]) on top, as a libgs list. Render tag: SPRITE2D, RQ_OBJID(RQ_OC_NONE, 0)
 * sub 0 (first SPRITE2D user). */
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"
#include "acnb/local/src/main/nowld.h"

#ifdef ACNB_SOURCEPORT
/* GS TEX0 words of the loading-screen textures (0x25fa70) */
unsigned long long s_tex0Nl[4] = {0x2007f00621313b40ull, 0x2007f005e1313b40ull, 0x2007f085a1313bc0ull,
                                  0x2007f1059d30bc00ull};
#endif

/* the loading screen (callers pass a0 0 / 1, never read) */
void Nl_Draw(void)
{
    PS2_FRAME(0x80);
    PS2_STACK(int[4], rgbB, 0x60);   /* lq / sq of the rodata constants, before the gate */
    PS2_STACK(int[4], rgbA, 0x70);
    struct StDraw *p = Draw_GetProperty();
    rgbA[0] = 0xa5;
    rgbA[1] = 0x53;
    rgbA[2] = 0x54;
    rgbA[3] = 0x80;
    rgbB[0] = rgbB[1] = rgbB[2] = rgbB[3] = 0x80;
    if (s_isLoadNl == 0) return;
    PS2_RQ_SCOPE(RQ_K_SPRITE2D, RQ_OBJID(RQ_OC_NONE, 0), 0);   /* render tag (host only): now loading */
    DF_ReadTMXPointer((char *)s_bufType1Nl, 1);
    GSX_SetWindowSize(p->wScreen / 2, p->hScreen / 2, 4, 3);
    int W = p->wScreen;
    int off = ps2_ftoi(ps2_div((float)s_cntNlAnim, 300.0f) * (float)(W * 2)) - W;
    GS_NewList();
    GS_DrawBlack(0, 0, p->wScreen, p->hScreen, 0);
    W = p->wScreen;
    int hw = W / 2;
    int y = p->hScreen / 2 - 0x10;
    struct_f386fb29 tex0;
    memcpy(&tex0, &s_tex0Nl[2], 8);
    GS_SetTEX0_1(tex0);
    GS_SetALPHA_1(0, 1, 0, 1, 0);
    GS_SetRGBA(rgbA);
    GS_SetTEST_1(0, 0, 0, 0, 0, 0, 1, 1);
    GS_DrawSpriteD(off, y, hw + off, y + 0x20, 0.0f, 0.0f, 256.0f, 32.0f, 0);
    GS_DrawSpriteD(hw + off, y, W + off, y + 0x20, 0.0f, 32.0f, 256.0f, 64.0f, 0);
    memcpy(&tex0, &s_tex0Nl[1], 8);
    GS_SetTEX0_1(tex0);
    GS_SetALPHA_1(2, 0, 0, 1, 0);
    GS_SetRGBA(rgbB);
    GS_SetTEST_1(1, 1, 0, 0, 0, 0, 1, 1);
    GS_DrawSpriteD(0, y, hw, y + 0x20, 0.0f, 0.0f, 256.0f, 32.0f, 0);
    GS_DrawSpriteD(hw, y, W, y + 0x20, 0.0f, 32.0f, 256.0f, 64.0f, 0);
    GS_SetRegister(0x47, 0x31003);   /* TEST_1 */
    GS_SetRegister(0x42, 0x44);      /* ALPHA_1 */
    s_cntNlAnim = s_cntNlAnim + 1;
    if (s_cntNlAnim >= 0x12d) s_cntNlAnim = 0;
    GS_FlushList();
    sceGsSyncPath(0, 0);
}

/* Nl_Draw is declared void(void) in DWARF but the callers load a0 = 0 (modes 0 / 1) or 1 (mode 2); Nl_Draw
 * overwrites a0 before reading it, so the value is unused. */
static void nl_draw_mode(void)
{
    switch (s_modeNl) {
    case 0:
    case 1: Nl_Draw(); break;
    case 2: Nl_Draw(); break;
    default: break;
    }
}

/* nlloop 0: loop until the async read finishes (File_AsyncProc runs after each pass's drawing); 1: two passes;
 * any other value never ends (original behaviour). */
void NL_LoopNowLoading(int nlloop)
{
    PS2_FRAME(0x40);
    int frame = 0;
    int loop = 1;
    do {
        Draw_FlipBufferTearing();
        Pad_UpdateData();
        DF_DrawColorScreen_NoSemi(0, 0, 0, 0x80);
        sceGsSyncPath(0, 0);
        nl_draw_mode();
        ps2_dma_wait_gif();
        sceGsSyncPath(0, 0);
        if (nlloop == 1) {
            if (frame > 0) loop = 0;
        } else if (nlloop == 0) {
            File_AsyncProc();
            if (!File_IsAsyncBusy()) loop = 0;
        }
        frame++;
    } while (loop);
}

void NL_DrawNowLoading(void)
{
    PS2_FRAME(0x10);
    nl_draw_mode();
}

int NL_ReadData(int id, void *buff, int maxsize, int block)
{
    PS2_FRAME(0x20);
    File_Read(id, maxsize, (char *)buff, block == 1);
    if (block == 0) NL_LoopNowLoading(0);
    return File_GetResult();
}

int NL_GetMode(void) { return s_modeNl; }
void NL_SetMode(int mode) { s_modeNl = mode; }

/* s_cntNlAnim is reset on every call (the store sits in the delay slot of the s_isLoadNl test); the size queries
 * only prime the cdvd size cache. */
void NL_LoadTexture(void)
{
    PS2_FRAME(0x10);
    s_cntNlAnim = 0;
    if (s_isLoadNl) return;
    Cdvd_GetSize2048(0x1e);
    File_Read(0x1e, -1, (char *)s_bufType1Nl, 1);
    Cdvd_GetSize2048(0x1f);
    File_Read(0x1f, -1, (char *)s_bufType2Nl, 1);
    s_isLoadNl = 1;
}
