/* src/sfx/weth.c (OLSORTIE) - weather: up to 1024 rain / snow particles in s_Rain, spawned around the view camera by
 * WETH_RainProc from g_RainParam (state 0 = rain/snow running and ramping up, 1 = stopping, 2 = stopped) and moved
 * by a per-type proc (OneRainProc / OneRainProcInit: 0 rain, 1 snow, 2 snow 2, 3 rain 2). loc2 is the streak end.
 * WETH_DrawRain: the GIF packet builder (64 SPR batches through GifPath3KickSPR). */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/gif.h"
#include "acnb/local/src/sfx/weth.h"

/* loc2 = loc + (32, 4, 32) */
static void __weth_NormalSnow2Proc(__rain_st *pRain)
{
    pRain->loc[0] = pRain->loc[0] + SnowVec[0];   /* vadd.xyz: w kept */
    pRain->loc[1] = pRain->loc[1] + SnowVec[1];
    pRain->loc[2] = pRain->loc[2] + SnowVec[2];
    for (int i = 0; i < 4; i++) pRain->loc2[i] = pRain->loc[i];
    pRain->loc2[0] = 32.0f + pRain->loc[0];
    pRain->loc2[1] = pRain->loc2[1] + 4.0f;
    pRain->loc2[2] = 32.0f + pRain->loc[2];
    if (--pRain->frm <= 0) pRain->on = 0;
}

static void __weth_NormalSnow2Init(__rain_st *pRain)
{
    pRain->frm = (short)SnowFrm;
    pRain->type = 2;
    pRain->texid = 0;
}

static void __weth_NormalSnowProc(__rain_st *pRain)
{
    pRain->loc[0] = pRain->loc[0] + SnowVec[0];
    pRain->loc[1] = pRain->loc[1] + SnowVec[1];
    pRain->loc[2] = pRain->loc[2] + SnowVec[2];
    for (int i = 0; i < 4; i++) pRain->loc2[i] = pRain->loc[i];
    pRain->loc2[0] = 32.0f + pRain->loc[0];
    pRain->loc2[1] = pRain->loc2[1] + 4.0f;
    pRain->loc2[2] = 32.0f + pRain->loc[2];
    if (--pRain->frm <= 0) pRain->on = 0;
}

static void __weth_NormalSnowInit(__rain_st *pRain)
{
    pRain->frm = (short)SnowFrm;
    pRain->type = 1;
    pRain->texid = 0;
}

static void __weth_NormalRain2Proc(__rain_st *pRain)
{
    pRain->loc[0] = pRain->loc[0] + 4.0f;
    pRain->loc[1] = pRain->loc[1] + 64.0f;
    pRain->loc[2] = pRain->loc[2] + 4.0f;
    for (int i = 0; i < 4; i++) pRain->loc2[i] = pRain->loc[i];
    pRain->loc2[0] = 32.0f + pRain->loc[0];
    pRain->loc2[1] = pRain->loc2[1] + 4.0f;
    pRain->loc2[2] = 64.0f + pRain->loc[2];
    if (--pRain->frm <= 0) pRain->on = 0;
}

static void __weth_NormalRain2Init(__rain_st *pRain)
{
    pRain->frm = 0xc;
    pRain->type = 3;
    pRain->texid = 0;
}

static void __weth_NormalRainProc(__rain_st *pRain)
{
    pRain->loc[0] = pRain->loc[0] + 4.0f;
    pRain->loc[1] = pRain->loc[1] + 64.0f;
    pRain->loc[2] = pRain->loc[2] + 4.0f;
    for (int i = 0; i < 4; i++) pRain->loc2[i] = pRain->loc[i];
    pRain->loc2[0] = pRain->loc2[0] + 4.0f;
    pRain->loc2[1] = pRain->loc2[1] + 72.0f;
    pRain->loc2[2] = pRain->loc2[2] + 4.0f;
    if (--pRain->frm <= 0) pRain->on = 0;
}

static void __weth_NormalRainInit(__rain_st *pRain)
{
    pRain->frm = 0xc;
    pRain->type = 0;
    pRain->texid = 0;
}

/* camera position + ofs + dif * [-1, 1) per axis */
static void rain_spawn_pos(float *loc)
{
    CameraSt *cam = &CAM[ViewCamID[0]];
    for (int i = 0; i < 3; i++) {
        unsigned short r = (unsigned short)ps2_rand();
        loc[i] = cam->vec[i] + g_RainParam.ofs[i] + g_RainParam.dif[i] * (1.0f - 2.0f * ((float)r / 65536.0f));
    }
    loc[3] = 1.0f;
}

void WETH_RainProc(void)
{
    PS2_FRAME(0x120);
    float loc[4] __attribute__((aligned(16)));
    if (g_RainParam.state == 2) return;
    if (g_RainParam.state == 1) {
        g_RainParam.now_num--;
        for (int i = 0; i < g_RainParam.num; i++) {
            rain_spawn_pos(loc);
            WETH_AppearRain(loc, 5, 0x10, g_RainParam.type);
        }
        if (g_RainParam.now_num <= 0) {
            g_RainParam.now_num = 0;
            g_RainParam.state = 2;
        }
    } else if (g_RainParam.state == 0) {
        if (g_RainParam.now_num < g_RainParam.num && (FRAME & 1)) g_RainParam.now_num++;
        for (int i = 0; i < g_RainParam.num; i++) {
            rain_spawn_pos(loc);
            WETH_AppearRain(loc, 5, 0x10, g_RainParam.type);
        }
    }
}

void WETH_RainInit(void)
{
    ZeroMemory32b(s_Rain, 0x4000);
    ZeroMemory32b(&g_RainParam, 0xc);
    s_NowRainTop = 0;
}

void WETH_WeatherProc(void)
{
    PS2_FRAME(0x30);
    NowRainNum = 0;
    __rain_st *p = s_Rain;
    for (int i = 0; i < 0x400; i++, p++) {
        if (!p->on || !p->func) continue;
        gcall(p->func, p);
        if (p->on) NowRainNum++;
    }
}

/* Takes the first free slot from s_NowRainTop on, wrapping to [0, s_NowRainTop). on = the first search's index + 1
 * (0x401 after a wrap). */
int WETH_AppearRain(float *loc, int lx, int ly, char prog)
{
    PS2_FRAME(0x10);
    int top = s_NowRainTop;
    int i = top;
    __rain_st *p = &s_Rain[top];
    while (i < 0x400 && p->on) { i++; p++; }
    if (i < 0x400) {
        s_NowRainTop = i + 1;
    } else {
        int j = 0;
        p = s_Rain;
        while (j < top && p->on) { j++; p++; }
        if (j >= top) return 0;
        s_NowRainTop = j + 1;
    }
    for (int k = 0; k < 4; k++) p->loc[k] = loc[k];
    p->on = (short)(i + 1);
    p->lx = lx;
    p->ly = ly;
    p->func = (void (*)(_rain_st *))OneRainProc[(int)prog];
    gcall(OneRainProcInit[(int)prog], p);
    return 1;
}

void WETH_Reset(void)
{
    ZeroMemory32b(s_Rain, 0x4000);
    RainRGBA[0][0] = 0x60;
    RainRGBA[0][1] = 0x60;
    RainRGBA[0][2] = 0x6c;
    RainRGBA[0][3] = 0x20;
    RainRGBA[1][0] = 0x80;
    SnowOfsY = -30.0f;
    RainRGBA[1][1] = 0x80;
    SnowHt = 96.0f;
    RainRGBA[1][2] = 0x80;
    SnowWd = 512.0f;
    RainRGBA[1][3] = 0x80;
    SnowFrm = 0x40;
    SnowVec[3] = 1.0f;
    SnowNum = 0xe;
    s_NowRainTop = 0;
    SnowVec[0] = 4.0f;
    SnowScale = 1.0f;
    SnowVec[1] = 4.0f;
    SnowVec[2] = 4.0f;
}

/* ---------------------------------------------------------------- WETH_DrawRain (GIF packet builder)
 * Original inline-asm COP2 code run on the live VU0 registers (VU0()), so the end state (vf1-4, vf16-20, vf22-27,
 * vf31, ACC, Q of the last particle drawn) is the original's in both builds. */

/* vf<d> = M * vf<d> with M = vf1..vf4: vmulax / vmadday / vmaddaz.xyzw ACC, vmaddw.xyzw vf<d> */
static void weth_vu_apply(int d)
{
    auto *v = VU0();
    float x = v->vf[d].f[0], y = v->vf[d].f[1], z = v->vf[d].f[2], w = v->vf[d].f[3];
    for (int i = 0; i < 4; i++) {
        float a = v->vf[1].f[i] * x;
        a = a + v->vf[2].f[i] * y;
        a = a + v->vf[3].f[i] * z;
        v->acc[i] = a;
        v->vf[d].f[i] = a + v->vf[4].f[i] * w;
    }
}

/* GS guard band / depth range test of an ftoi4 vertex: off-screen -> ADC */
static int weth_vtx_out(const int *s)
{
    return ((unsigned int)(s[0] | s[1]) & 0xffff0000u) != 0 || s[2] < 0 || s[2] >= 0x1000000;
}

/* projection of vf16 / vf17 through vf1..vf4 into the 12.4 XYZF2 words s[0..7] (scratchpad 0x70000000); word 3 (F) =
 * vf12.w + vf13.w / w: the fog factors that __MakeSfxDl (gfx/mkdl) left in vf12 / vf13 (stale-register coupling).
 * ADC (bit 15 of word 3) on vertex 2 when it is out, on both when vertex 1 is out. vf16 / vf17 = the final words. */
static void weth_project_pair(int *s)
{
    auto *v = VU0();
    weth_vu_apply(16);
    weth_vu_apply(17);                                      /* ACC.xyz = vertex 2's three-term chain */
    v->q = ps2_div(1.0f, v->vf[16].f[3]);                   /* vdiv q, vf0w, vf16w */
    v->acc[3] = v->vf[12].f[3] * 1.0f;                      /* vmulaw.w acc, vf12, vf0w */
    v->vf[26].f[3] = v->acc[3] + v->vf[13].f[3] * v->q;     /* vmaddq.w vf26, vf13, q */
    for (int i = 0; i < 3; i++) v->vf[26].f[i] = v->vf[16].f[i] * v->q;
    v->q = ps2_div(1.0f, v->vf[17].f[3]);
    for (int i = 0; i < 4; i++) v->vf[16].sw[i] = ps2_ftoi(v->vf[26].f[i] * 16.0f);   /* vftoi4 */
    v->vf[27].f[3] = v->acc[3] + v->vf[13].f[3] * v->q;
    for (int i = 0; i < 3; i++) v->vf[27].f[i] = v->vf[17].f[i] * v->q;
    memcpy(s, v->vf[16].sw, 16);
    for (int i = 0; i < 4; i++) v->vf[17].sw[i] = ps2_ftoi(v->vf[27].f[i] * 16.0f);
    memcpy(s + 4, v->vf[17].sw, 16);
    if (weth_vtx_out(s + 4)) s[7] |= 0x8000;
    if (weth_vtx_out(s)) {
        s[3] |= 0x8000;
        s[7] |= 0x8000;
    }
    memcpy(v->vf[16].sw, s, 16);
    memcpy(v->vf[17].sw, s + 4, 16);
}

/* Draws the live particles of s_Rain: type 0 (and anything not 1-3) as a LINE loc -> loc2 (RGBAQ, XYZF2 x2), types
 * 1-3 as a camera-facing SPRITE of rain_xyz * SnowScale around loc (ST, RGBAQ, XYZF2 x2; st 0,0,0,0 / 1,1,1,1, so
 * vertex 1 has Q 0). 64 batches of 16 particles, each batch a packet in the scratchpad PrimitiveBuffer[PBuffID]
 * kicked through GifPath3KickSPR (a batch without live particles kicks nothing). The GS state (alpha, test, clamp,
 * the snow texture tex0[1]) goes out first as a GS list. RainRGBA row 1 is not used here; the original also builds a
 * GIFtag_tex A+D tag that it never reads (dropped).
 * Render tag: EFFECT, objid RQ_OC_EFFECT / 0xe3 << 16 (weather family, single instance 0), sub 0. */
void WETH_DrawRain(void)
{
    PS2_FRAME(0x110);
    PS2_RQ_SCOPE(RQ_K_EFFECT, RQ_OBJID(RQ_OC_EFFECT, 0xe3u << 16), 0);
    auto *v = VU0();
    alignas(16) static const float st[2][4] = {{0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}};
    alignas(16) float matrix[4][4];
    alignas(16) float spr[2][4];
    unsigned int GIFtag_sp[4], GIFtag[4];

    sceGsSyncPath(0, 0);
    GS_NewList();
    GS_SetALPHA_1(0, 2, 0, 1, 0);
    GS_SetTEST_1(1, 0, 0, 1, 0, 0, 1, 2);
    GS_SetCLAMP_1(1, 1, 0, 0, 0, 0);
    struct_f386fb29 t;
    memcpy(&t, &tex0[1], 8);
    GS_SetTEX0_1(t);
    GS_FlushList();
    sceGsSyncPath(0, 0);

    gif_tag_prim(GIFtag, 2, 0x41, 2, GIF_R_RGBAQ | GIF_R_XYZF2 << 4);                         /* LINE ABE */
    gif_tag_prim(GIFtag_sp, 2, 0x56, 3, GIF_R_ST | GIF_R_RGBAQ << 4 | GIF_R_XYZF2 << 8);    /* SPRITE TME ABE */

    __rain_st *pRain = s_Rain;
    for (int i = 0; i < 64; i++) {
        unsigned int *p = (unsigned int *)PrimitiveBuffer[PBuffID];
        for (int j = 0; j < 16; j++, pRain++) {
            if (!pRain->on) continue;
            int *s = (int *)PS2_SPR(0);
            int type = pRain->type;
            if (type == 3 || type == 2 || type == 1) {
                memcpy(p, GIFtag_sp, 16);
                p += 4;
                sceVu0TransMatrix(matrix, CAMERA_MATRIX2, pRain->loc);
                for (int k = 0; k < 4; k++) memcpy(v->vf[1 + k].f, WORLD_SCREEN_MATRIX[k], 16);
                for (int k = 0; k < 4; k++) memcpy(v->vf[22 + k].f, matrix[k], 16);
                for (int k = 0; k < 4; k++) weth_vu_apply(22 + k);
                for (int k = 0; k < 4; k++) memcpy(matrix[k], v->vf[22 + k].f, 16);
                for (int k = 0; k < 4; k++) memcpy(v->vf[1 + k].f, matrix[k], 16);
                for (int k = 0; k < 2; k++) {
                    /* qmtc2 of the GPR v0 after mfc1 SnowScale: x = SnowScale, y = its sign extension, z / w = the
                     * upper half that lq v0 = GIFtag_sp left (words 2, 3) */
                    v->vf[31].f[0] = SnowScale;
                    v->vf[31].sw[1] = (int)ps2_f2u(SnowScale) >> 31;
                    v->vf[31].uw[2] = GIFtag_sp[2];
                    v->vf[31].uw[3] = GIFtag_sp[3];
                    memcpy(v->vf[16].f, rain_xyz[k], 16);
                    for (int c = 0; c < 3; c++) v->vf[16].f[c] = v->vf[16].f[c] * v->vf[31].f[0];   /* vmulx.xyz */
                    memcpy(spr[k], v->vf[16].f, 16);
                }
                memcpy(v->vf[16].f, spr[0], 16);
                memcpy(v->vf[17].f, spr[1], 16);
                weth_project_pair(s);
                memcpy(v->vf[18].sw, RainRGBA[0], 16);
                memcpy(v->vf[19].f, st[0], 16);
                memcpy(v->vf[20].f, st[1], 16);
                memcpy(p + 8, v->vf[16].sw, 16);
                memcpy(p + 20, v->vf[17].sw, 16);
                memcpy(p + 4, v->vf[18].sw, 16);
                memcpy(p + 16, v->vf[18].sw, 16);
                memcpy(p, v->vf[19].f, 16);
                memcpy(p + 12, v->vf[20].f, 16);
                p += 24;
            } else {
                memcpy(p, GIFtag, 16);
                p += 4;
                for (int k = 0; k < 4; k++) memcpy(v->vf[1 + k].f, WORLD_SCREEN_MATRIX[k], 16);
                memcpy(v->vf[16].f, pRain->loc, 16);
                memcpy(v->vf[17].f, pRain->loc2, 16);
                weth_project_pair(s);
                memcpy(v->vf[18].sw, RainRGBA[0], 16);
                memcpy(p + 4, v->vf[16].sw, 16);
                memcpy(p + 12, v->vf[17].sw, 16);
                memcpy(p, v->vf[18].sw, 16);
                memcpy(p + 8, v->vf[18].sw, 16);
                p += 16;
            }
        }
        int n = (int)((char *)p - PrimitiveBuffer[PBuffID]) / 16;
        if (n) {
            GifPath3KickSPR((unsigned int *)PrimitiveBuffer[PBuffID], n);
            PBuffID = !PBuffID;
        }
    }
    sceGsSyncPath(0, 0);
}

#ifdef ACNB_SOURCEPORT
/* the original's initialized tables (generated by scripts/sp_datagen.py from the PS2 image) */
void (*OneRainProcInit[4])(struct __rain_st *) = {   /* OLSORTIE 0x1e6c820 */
    __weth_NormalRainInit, __weth_NormalSnowInit, __weth_NormalSnow2Init, __weth_NormalRain2Init
};
void (*OneRainProc[4])(struct __rain_st *) = {   /* OLSORTIE 0x1e6c830 */
    __weth_NormalRainProc, __weth_NormalSnowProc, __weth_NormalSnow2Proc, __weth_NormalRain2Proc
};
#endif
