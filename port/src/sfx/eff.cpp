/* src/sfx/eff.cpp - sun lens flare (DF_EFF_DrawLensFlare, from DrawAfter when the stage and system info enable it).
 * Projects the sun, reads the 1x1 PSMZ24 z-buffer pixel under it back from the GS (sceGsStoreImage into the
 * scratchpad) to decide whether it is hidden, fades s_rateLfShow in (+0.2) / out (-0.1), draws 15 flare sprites along
 * the sun -> screen-centre line (only while lit) and two glow sprites around the sun, plus a third (scaled by
 * s_rateLfShow) while the sun is on screen. Game state (s_rateLfShow, so the glow alpha) depends on GS memory: in the
 * hybrid the readback is the emulated GS; in the source port sceGsExecStoreImage hands the rectangle to the render
 * module (READBACK record). Quirks kept: with the sun off screen there is no readback, `lit` stays 1, so the flares are
 * drawn and the fade rises (only glow 3 is skipped); the visibility test uses scr[1] >> 3, the readback scr[1] >> 4.
 * DF_EFF_DrawLightEffectZ (proc_zsprite, not Versus) stays interpreted.
 * Render tag: POST, RQ_OBJID(RQ_OC_NONE, 0) sub 2 (same id DF_DrawSpriteCenter / Light use for the lens flare); the
 * TMX texture load before it is not tagged. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"
#include "acnb/local/src/sfx/eff.h"

#ifdef ACNB_SOURCEPORT
float DF_EFF_DrawLensFlare__ST[4][4] = {
    {0.5f, 0.5f, 1.0f, 1.0f}, {0.5f, 0.0f, 1.0f, 0.5f}, {0.0f, 0.5f, 0.5f, 1.0f}, {0.0f, 0.0f, 0.5f, 0.5f}};
int DF_EFF_DrawLensFlare__screencenter[4] = {0x8000, 0x8000, 0, 0x10};
int DF_EFF_DrawLensFlare__flaresize[16] = {0x200, 0x40, 0x64, 0x10, 0x36, 0x18, 0x16, 0x10,
                                           0x2e, 0x50, 0x18, 0x1e, 0x0c, 0x80, 0x120, 0};
int DF_EFF_DrawLensFlare__flaredist[16] = {-0x48, -0x1c, 2, 0x32, 0x38, 0x3c, 0x5c, 0x80,
                                           0x88, 0x90, 0x92, 0xa2, 0xa3, 0xbc, 0xd4, 0};
int DF_EFF_DrawLensFlare__flarealpha[16] = {8, 0x10, 0x30, 0x18, 8, 0x18, 0x10, 0x20,
                                            0x10, 8, 0x10, 0x10, 0x0c, 0x10, 8, 0};
int DF_EFF_DrawLensFlare__type[16] = {3, 3, 1, 3, 3, 3, 2, 0, 2, 2, 2, 3, 3, 3, 1, 0};
#endif

/* sceGsStoreImage (0x70 bytes: DMA tag + GIF tag + BITBLTBUF / TRXPOS / TRXREG / FINISH / TRXDIR), built and DMA'd
 * by the SDK */
struct alignas(16) lf_store_image { unsigned long long q[14]; };

static struct_1f802d8d eff_tex0(unsigned long long v)
{
    struct_1f802d8d t;
    memcpy(&t, &v, 8);
    return t;
}

void DF_EFF_DrawLensFlare(float *sun)
{
    PS2_FRAME(0x1a0);
    PS2_STACK(struct lf_store_image, simg, 0xe0);
    alignas(16) int scr[4];      /* sp+0x160: x, y, z (12.4) */
    alignas(16) int rgba[4];     /* sp+0x170 */
    alignas(16) int center[4];   /* sp+0x180 */
    alignas(16) float st[4];     /* sp+0x150 */
    unsigned long long tex0;
    float scale = CopyOfSysInfo.lensflare.alpha * ps2_div((float)CopyOfSysInfo.colLf.rgba[3], 128.0f);
    int vis = 1, lit = 1;
    sceGsSyncPath(0, 0);
    DF_ReadTMXPointer(LenstexTopp, 1);
    DF_CALC_ScreenPoint(sun, scr);
    int ax = (scr[0] >> 4) - 0x800;
    int ay = (scr[1] >> 3) - 0x1000;
    if ((scr[2] >> 4) < 0) return;   /* behind the camera */
    PS2_RQ_SCOPE(RQ_K_POST, RQ_OBJID(RQ_OC_NONE, 0), 2);   /* render tag (host only): lens flare */
    int dX = DF_EFF_DrawLensFlare__screencenter[0] - scr[0];
    int dY = DF_EFF_DrawLensFlare__screencenter[1] - scr[1];
    if (!(ax >= -0xff && ax < 0x100)) vis = 0;
    if (!(ay >= -0xdf && ay < 0xe0)) vis = 0;
    int r2 = (int)((unsigned int)ax * (unsigned int)ax + (unsigned int)ay * (unsigned int)ay);
    float fade = 128.0f - ps2_div(128.0f * (float)r2, 128000.0f);
    if (!(fade <= 128.0f)) fade = 128.0f;
    if (fade < 0.0f) fade = 0.0f;

    sceGsSyncPath(0, 0);
    DF_SetAlphaRegister(0, 2, 0, 1, 0, 1);
    sceGsSyncPath(0, 0);
    DF_SetTestRegister(1, 0, 0xff, 1, 0, 0, 1, 1, 1);
    sceGsSyncPath(0, 0);

    /* occlusion: the z-buffer pixel under the sun (game state depends on GS memory) */
    if (vis) {
        *(unsigned int *)PS2_SPR(0) = 0;
        sceGsSetDefStoreImage(&simg, 0xe00, 8, 0x31 /* PSMZ24 */, (short)((scr[0] >> 4) - 0x700),
                              (short)((scr[1] >> 4) - 0x790), 1, 1);
        FlushCache(0);
        sceGsExecStoreImage(&simg, PS2_SPR(0));
        sceGsSyncPath(0, 0);
        if ((scr[2] >> 4) < (int)(*(unsigned int *)PS2_SPR(0) & 0xffffff)) lit = 0;
    }
    if (lit) {
        float r = s_rateLfShow + 0.2f;
        s_rateLfShow = r;
        if (!(r <= 1.0f)) s_rateLfShow = 1.0f;
    } else {
        float r = s_rateLfShow - 0.1f;
        s_rateLfShow = r;
        if (r < 0.0f) s_rateLfShow = 0.0f;
    }
    DF_Texflush();

    if (lit) {
        tex0 = 0x8007cd85dd30bb40ull;
        rgba[2] = 0x80;
        rgba[1] = 0x80;
        rgba[0] = 0x80;
        for (int i = 0; i < 15; i++) {
            rgba[3] = ps2_ftoi(scale * ps2_div(fade * (float)DF_EFF_DrawLensFlare__flarealpha[i], 128.0f));
            int dist = DF_EFF_DrawLensFlare__flaredist[i];
            center[0] = ps2_ftoi((float)scr[0] + 0.01f * (float)(int)((unsigned int)dX * (unsigned int)dist));
            center[1] = ps2_ftoi((float)scr[1] + 0.01f * (float)(int)((unsigned int)dY * (unsigned int)dist));
            int size = DF_EFF_DrawLensFlare__flaresize[i];
            DF_DrawSpriteCenter(center, rgba, DF_EFF_DrawLensFlare__ST[DF_EFF_DrawLensFlare__type[i]], eff_tex0(tex0),
                                size, size, 0xfffff0, 0x40, 0x140, 0xe0);
        }
    }
    DF_SetTestRegister(1, 0, 0xff, 1, 0, 0, 1, 3, 1);
    sceGsSyncPath(0, 0);

    /* glow 1 */
    float d = ps2_div((float)r2, 152576.0f);
    tex0 = 0x8007ce05dd30bb80ull;
    rgba[0] = CopyOfSysInfo.colLf.rgba[0];
    rgba[1] = CopyOfSysInfo.colLf.rgba[1];
    rgba[2] = CopyOfSysInfo.colLf.rgba[2];
    rgba[3] = 0x60;
    rgba[3] = ps2_ftoi((float)rgba[3] * scale);
    center[0] = scr[0];
    center[1] = scr[1];
    st[0] = 0.0f;
    st[1] = 0.0f;
    st[2] = 1.0f;
    st[3] = 1.0f;
    int size = 0xf0 - ps2_ftoi(225.0f * ps2_sqrt(d));
    DF_DrawSpriteCenter(center, rgba, st, eff_tex0(tex0), size, size, scr[2] + 0x3200, 0x40, 0x140, 0xe0);

    /* glow 2 */
    tex0 = 0x8007ce85dd30bbc0ull;
    rgba[0] = CopyOfSysInfo.colLf.rgba[0];
    rgba[1] = CopyOfSysInfo.colLf.rgba[1];
    rgba[2] = CopyOfSysInfo.colLf.rgba[2];
    rgba[3] = 0x40;
    size = 0x190 - ps2_ftoi(360.0f * ps2_sqrt(d));
    rgba[3] = ps2_ftoi((float)rgba[3] * scale);
    center[0] = scr[0];
    center[1] = scr[1];
    st[0] = 0.0f;
    st[1] = 0.0f;
    st[2] = 1.0f;
    st[3] = 1.0f;
    DF_DrawSpriteCenter(center, rgba, st, eff_tex0(tex0), size, size, scr[2] + 0x5000, 0x40, 0x140, 0xe0);

    DF_SetTestRegister(1, 0, 0xff, 1, 0, 0, 0, 3, 1);
    if (!vis) return;

    /* glow 3: the big halo, faded by s_rateLfShow and the distance from the screen centre */
    tex0 = 0x8007ce05dd30bb80ull;
    rgba[2] = 0x80;
    rgba[1] = 0x80;
    rgba[0] = 0x80;
    rgba[3] = ps2_ftoi(scale * (s_rateLfShow * (48.0f - 48.0f * ps2_sqrt(ps2_div((float)r2, 50176.0f)))));
    if (rgba[3] <= 0) return;
    center[0] = scr[0];
    center[1] = scr[1];
    st[0] = 0.0f;
    st[1] = 0.0f;
    st[2] = 1.0f;
    st[3] = 1.0f;
    DF_DrawSpriteCenter(center, rgba, st, eff_tex0(tex0), 0x578, 0x578, scr[2] + 0x5000, 0x40, 0x140, 0xe0);
}
