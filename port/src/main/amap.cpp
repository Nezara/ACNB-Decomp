/* src/main/amap.cpp - the in-battle area map (AMAP). Native: DF_AMAP_ReadData (boot: the map sprite table, disc
 * file 0x15), DF_AMAP_Init (per sortie: clear the map / object flag arrays and the map strings, build the map's
 * view-screen matrix, count the models that make up the map total) and its counter __AMAP_GetSumNum; the map
 * screen's sprite layer (DF_AMAP_SetSpr, the SPR1..6 sprite state functions, __AMAP_DrawSpr) and its place-name
 * strings (DF_AMAP_DrawString, __AMAP_DrawStrings); the crosshair lines (__AMAP_MoveLine), the compass
 * (DF_AMAP_DrawCompass, a GS packet DMA'd from the stack), the explored-model search run by __ControlAc every frame
 * (DF_AMAP_SearchModel) and the model display test (__AMAP_DispCheck); the map screen itself: DF_AMAP_MainLoop
 * (called by AreaMap) sets it up (__AMAP_Init: sound, camera, VU1 program, sprites), runs the blocking frame loop
 * (__AMAP_Exec: pad (__AMAP_Pad), step machine (__AMAP_Ctrl), camera (__AMAP_CamCtrl, which also overwrites the
 * game's world_screen_matrix), sprite state functions, model display list (__AMAP_MakeDL, kicked next frame), 2D
 * passes) and tears it down. The whole file is native; stage_cam_dist (per-map camera distance) is ELF_DATA. */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"

/* models counted towards the map total: not hidden (use bit 0), not flagged 0x80, not destroyed */
static int amap_count(StModel *m, int num, int n)
{
    for (int i = 0; i < num; i++, m++)
        if (!(m->use & 1) && !(m->mflag & 0x80) && !DF_GIMK_IsDestroyModel(m)) n++;
    return n;
}

static int __AMAP_GetSumNum(void)
{
    PS2_FRAME(0x50);
    int n = amap_count(Map, Array->head.mnum, 0);
    n = amap_count(Obj, Array->head.onum, n);
    return amap_count(Dobj, Array->head.dnum, n);
}

void DF_AMAP_ReadData(void)
{
    PS2_FRAME(0x10);
    File_Read(0x15, File_GetSize(0x15), AmapData, 1);
}

void DF_AMAP_Init(void)
{
    PS2_FRAME(0x20);   /* sceVu0ViewScreenMatrix takes its 9th float at sp+0 */
    ZeroMemory32b(AmapMapFlag, 0x20);
    ZeroMemory32b(AmapObjFlag, 0x18);
    ZeroMemory32b(AmapDobjFlag, 0xc);
    ZeroMemory32b(AmapMapRecFlag, 0x20);
    ZeroMemory32b(AmapObjRecFlag, 0x18);
    ZeroMemory32b(AmapDobjRecFlag, 0xc);
    ZeroMemory32b(MapAmapString, 0x100);
    NowAmapString = 0;
    sceVu0ViewScreenMatrix(AmapViewScreen, 512.0f, 1.0f, 0.5f, 2048.0f, 2048.0f, 0.0f, ps2_u2f(0x4b7fffff), 1.0f,
                           65536.0f);
    AmapSumNum = __AMAP_GetSumNum();
    AmapRecNum = 0;
}

/* ---- sprite layer ------------------------------------------------------------------------------------------- */

#ifdef ACNB_SOURCEPORT
char *AmapString[32] = {
    (char *)"LOCATION NOW", (char *)"TARGET", (char *)"FRIEND 1", (char *)"FRIEND 2", (char *)"FRIEND 3",
    (char *)"POINT A", (char *)"POINT B", (char *)"POINT C", (char *)"DEF TARGET", (char *)"COOLING DEVICE",
    (char *)"WAY IN", (char *)"CRUISER", (char *)"CARRIER", (char *)"SPACESHIP", (char *)"POWER REACTOR",
    (char *)"CENTER OF PHOBOS", (char *)"ESCAPE POINT", (char *)"TRANSPORTER", (char *)"OBJECTIVE POINT",
    (char *)"BONUS",   /* 20..31 NULL */
};
int AmapStrColor[32][4] = {
    {160, 16, 0, 128},    {255, 128, 64, 128},  {111, 199, 249, 128}, {111, 199, 249, 128}, {111, 199, 249, 128},
    {128, 128, 128, 128}, {128, 128, 128, 128}, {128, 128, 128, 128}, {255, 128, 64, 128},  {128, 128, 128, 128},
    {128, 128, 128, 128}, {128, 128, 128, 128}, {128, 128, 128, 128}, {128, 128, 128, 128}, {128, 128, 128, 128},
    {128, 128, 128, 128}, {255, 128, 64, 128},  {111, 199, 249, 128}, {255, 128, 64, 128},   /* 19..31 zero */
};
typedef void (*amap_sprfunc_t)(struct amap_spr_tag *, struct amap_mng_tag *);
void (*DF_AMAP_SprFuncTb[7])(struct amap_spr_tag *, struct amap_mng_tag *) = {
    nullptr,
    (amap_sprfunc_t)GFN(DF_AMAP_SPR1),
    (amap_sprfunc_t)GFN(DF_AMAP_SPR2),
    GFN(DF_AMAP_SPR3),
    (amap_sprfunc_t)GFN(DF_AMAP_SPR4),
    (amap_sprfunc_t)GFN(DF_AMAP_SPR5),
    (amap_sprfunc_t)GFN(DF_AMAP_SPR6),
};
unsigned long long AmapSprTex0[1] = {0x8007d08621312e40ull};
#endif

/* STA2_RGBA rows 0 and 11 (DWARF int[0][4]) */
static inline const int *amap_sta2_row(int row) { return &STA2_RGBA[0][0] + row * 4; }

/* the SPR functions' shared block: cockpit colour (4 bytes at mcol), c[i] = (float)mcol[i] stored first, then each
 * rgba channel scaled by c / 128 (cvt.w.s truncates) */
static void amap_colorize(struct amap_spr_tag *spr, unsigned char *mcol, float *c)
{
    DF_MFUNC_GetCockpitColor((struct_76b810ec *)mcol, g_gamedata[0].cockpitcol);
    for (int i = 0; i < 4; i++) c[i] = (float)mcol[i];
    for (int i = 0; i < 4; i++) spr->rgba[i] = ps2_ftoi(ps2_div((float)spr->rgba[i], 128.0f) * c[i]);
}

/* colourise, wait for xystat >= 4, fade out */
static void DF_AMAP_SPR6(struct amap_spr_tag *spr)
{
    PS2_FRAME(0x40);
    PS2_STACK(float[4], c, 0x20);
    PS2_STACK(unsigned char[4], mcol, 0x3c);
    switch (spr->flag) {
    case 0:
        amap_colorize(spr, mcol, c);
        spr->flag++;
        break;
    case 1:
        if (AmapMng.xystat >= 4) spr->flag++;
        break;
    case 2:
        spr->rgba[3] -= 4;
        if (spr->rgba[3] <= 0) spr->flag++;
        break;
    }
}

/* colourise, fade in, step xystat 3 -> 4, fade out */
static void DF_AMAP_SPR5(struct amap_spr_tag *spr)
{
    PS2_FRAME(0x40);
    PS2_STACK(float[4], c, 0x20);
    PS2_STACK(unsigned char[4], mcol, 0x3c);
    switch (spr->flag) {
    case 0:
        amap_colorize(spr, mcol, c);
        spr->flag++;
        break;
    case 1:
        spr->rgba[3] += 4;
        if (spr->rgba[3] >= 0x80) spr->flag++;
        break;
    case 2:
        if (AmapMng.xystat >= 3) {
            spr->flag++;
            AmapMng.xystat++;
        }
        break;
    case 3:
        spr->rgba[3] -= 4;
        if (spr->rgba[3] <= 0) spr->flag++;
        break;
    }
}

/* one-shot: colourise, then drop the state function */
static void DF_AMAP_SPR4(struct amap_spr_tag *spr)
{
    PS2_FRAME(0x40);
    PS2_STACK(float[4], c, 0x20);
    PS2_STACK(unsigned char[4], mcol, 0x3c);
    amap_colorize(spr, mcol, c);
    spr->func = nullptr;
}

/* alpha pulse between STA2_RGBA rows 0 / 11 (colour kept after the first frame), a voice at frame 50 */
static void DF_AMAP_SPR3(struct amap_spr_tag *spr, struct amap_mng_tag *amp)
{
    PS2_FRAME(0x60);
    PS2_STACK(float[4], c, 0x30);
    PS2_STACK(int[4], save, 0x40);
    PS2_STACK(unsigned char[4], mcol, 0x5c);
    float s = fsmath_SinVU0(ps2_u2f(0x3d80adfd) * (float)spr->frm);
    float f = 0.5f * (1.0f + s);
    const int *a = amap_sta2_row(0), *b = amap_sta2_row(11);
    spr->rgba[3] = ps2_ftoi((float)b[3] + (float)(0x40 - a[3]) * f);
    if (spr->flag == 1) memcpy(save, spr->rgba, 16);
    amap_colorize(spr, mcol, c);
    if (spr->flag == 1) {
        spr->rgba[0] = save[0];
        spr->rgba[1] = save[1];
        spr->rgba[2] = save[2];
    }
    spr->flag = 1;
    if (spr->frm < 100) spr->frm = spr->frm + 1;
    else spr->frm = 0;
    if (spr->frm == 0x32 && amp->snd == 0) {
        amp->snd = 1;
        Voice_Call(0x42, 0x7f);
    }
}

/* STA2_RGBA row 0 (state 1) or row 11 (else; colourised when state 0), frame counter 0..100 */
static void DF_AMAP_SPR2(struct amap_spr_tag *spr)
{
    PS2_FRAME(0x40);
    PS2_STACK(float[4], c, 0x20);
    PS2_STACK(unsigned char[4], mcol, 0x3c);
    memcpy(spr->rgba, amap_sta2_row(spr->state == 1 ? 0 : 11), 16);
    if (spr->state == 0) amap_colorize(spr, mcol, c);
    spr->frm = spr->frm + 1;
    if (spr->frm >= 0x65) spr->frm = 0;
}

/* colour pulse between STA2_RGBA rows 11 and 0, colourised */
static void DF_AMAP_SPR1(struct amap_spr_tag *spr)
{
    PS2_FRAME(0x40);
    PS2_STACK(float[4], c, 0x20);
    PS2_STACK(unsigned char[4], mcol, 0x3c);
    float s = fsmath_SinVU0(ps2_u2f(0x3d80adfd) * (float)spr->frm);
    float f = 0.5f * (1.0f + s);
    const int *a = amap_sta2_row(0), *b = amap_sta2_row(11);
    for (int i = 0; i < 4; i++) spr->rgba[i] = ps2_ftoi((float)b[i] + f * (float)(a[i] - b[i]));
    amap_colorize(spr, mcol, c);
    if (spr->frm < 100) spr->frm = spr->frm + 1;
    else spr->frm = 0;
}

void DF_AMAP_SetSpr(int sprid, int type, int blend, int draw, int istate, int ulx, int uly, int wd, int ht, float uls,
                    float ult, float wds, float htt, int r, int g, int b, int a)
{
    if (sprid < 0 || sprid >= 0x30) return;
    struct amap_spr_tag *s = &AmapSpr[sprid];
    s->ulx = (short)ulx;
    s->uly = (short)uly;
    s->uls = uls;
    s->ult = ult;
    s->wd = (short)wd;
    s->ht = (short)ht;
    s->rgba[0] = r;
    s->rgba[1] = g;
    s->rgba[2] = b;
    s->rgba[3] = a;
    s->func = DF_AMAP_SprFuncTb[type];   /* no range check on type; flag is not touched */
    s->frm = 0;
    s->step = 0;
    s->state = (short)istate;
    s->blend = (unsigned char)blend;
    s->draw_type = (char)draw;
    s->wds = wds;
    s->htt = htt;
}

#ifndef ACNB_SOURCEPORT
/* 13 int args: a0..a3, t0..t3, then ht, r, g, b, a in the caller's 8-byte stack slots; floats f12..f15 */
static void bw__DF_AMAP_SetSpr(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    DF_AMAP_SetSpr(c->gpr[A0].sw[0], c->gpr[A1].sw[0], c->gpr[A2].sw[0], c->gpr[A3].sw[0], c->gpr[T0].sw[0],
                   c->gpr[T1].sw[0], c->gpr[T2].sw[0], c->gpr[T3].sw[0], (int)rd32(sp), c->fpr[12].f, c->fpr[13].f,
                   c->fpr[14].f, c->fpr[15].f, (int)rd32(sp + 8), (int)rd32(sp + 0x10), (int)rd32(sp + 0x18),
                   (int)rd32(sp + 0x20));
}
#endif

static void amap_tex1(unsigned long long v)
{
    struct_d42f65aa t;
    memcpy(&t, &v, 8);
    GS_SetTEX1_1(t);
}

static void amap_tex0(unsigned long long v)
{
    struct_f386fb29 t;
    memcpy(&t, &v, 8);
    GS_SetTEX0_1(t);
}

/* blend: 0 -> (0,1,0,1), 1 -> (0,2,0,1), 2 -> (2,0,0,1) */
static void amap_blend(int blend, int &a, int &b)
{
    if (blend == 2) { a = 2; b = 0; }
    else if (blend == 1) { a = 0; b = 2; }
    else { a = 0; b = 1; }
}

static void __AMAP_DrawSpr(void)
{
    PS2_FRAME(0x80);
    PS2_STACK(int[2][4], xyz, 0x60);
    GS_NewList();
    GS_SetTEXFLUSH();
    GS_SetTEST_1(0, 0, 0, 0, 0, 0, 1, 1);
    amap_tex1(STA2_TEX1[0]);
    GS_SetCLAMP_1(1, 1, 0, 0, 0, 0);
    GS_SetALPHA_1(0, 1, 0, 1, 0);
    amap_tex0(AmapSprTex0[0]);
    GS_FlushList();
    /* pass 1: sprites */
    for (int ii = 0; ii < 0x30; ii++) {
        struct amap_spr_tag *spr = &AmapSpr[ii];
        if (spr->rgba[3] == 0 || spr->draw_type == 2) continue;
        GS_NewList();
        int a, b;
        amap_blend(spr->blend, a, b);
        GS_SetALPHA_1(a, b, 0, 1, 0);
        GS_SetRGBA(spr->rgba);
        if (spr->draw_type == 0)
            DF_ACSTAT_DrawSprite(spr->ulx, spr->uly, spr->uls, spr->ult, spr->wd, spr->ht);
        else
            GS_DrawSpriteD(spr->ulx, spr->uly, spr->ulx + spr->wd, spr->uly + spr->ht, spr->uls, spr->ult,
                           spr->uls + spr->wds, spr->ult + spr->htt, 0);
        GS_FlushList();
    }
    /* pass 2: 2D lines from (ulx, uly) to (wd, ht) */
    for (int ii = 0; ii < 0x30; ii++) {
        struct amap_spr_tag *spr = &AmapSpr[ii];
        if (spr->rgba[3] == 0 || spr->draw_type != 2) continue;
        int a, b;
        amap_blend(spr->blend, a, b);
        DF_SetAlphaRegister(a, b, 0, 1, 0, 0);
        static const int init[2][4] = {{0, 0, 0xffffff, 0}, {0, 0, 0xffffff, 0}};   /* 0x25ef20 */
        memcpy(xyz, init, sizeof init);
        xyz[0][0] = (spr->ulx + 0x700) << 4;
        xyz[0][1] = (spr->uly << 3) + 0x7900;
        xyz[1][0] = (spr->wd + 0x700) << 4;
        xyz[1][1] = (spr->ht << 3) + 0x7900;
        FlushCache(0);
        DF_Draw2DLine_Color(spr->rgba, xyz, 0);
    }
    sceGsSyncPath(0, 0);
}

/* a place-name marker at world point loc: a short tick line and the string next to it */
void DF_AMAP_DrawString(float *loc, int *rgb, char *str)
{
    PS2_FRAME(0x80);
    PS2_STACK(float[4], loc2, 0x40);
    PS2_STACK(int[2][4], xyz2, 0x50);
    PS2_STACK(int[4], xyz, 0x70);
    memcpy(loc2, PS2_LQ_SRC(loc), 16);
    loc2[3] = 1.0f;
    DF_CALC_ScreenPoint(loc2, xyz);
    memcpy(xyz2[0], xyz, 16);
    memcpy(xyz2[1], xyz, 16);
    if (!(xyz[2] < 0xfffff1) || xyz[2] < 0) return;
    xyz2[1][3] = 0x10;
    xyz2[0][3] = 0x10;
    xyz2[1][2] = 0xfffffa;
    xyz2[0][2] = 0xfffffa;
    xyz2[1][0] += 0x200;
    xyz2[1][1] -= 0x100;
    if (xyz2[0][0] >= 0x6e00 && xyz2[0][0] < 0x9001 && xyz2[0][1] >= 0x7900 && xyz2[0][1] < 0x8901)
        DF_Draw2DLine_Color(rgb, xyz2, 0x2020c000);
    float ulx = 32.0f + 0.0625f * (float)(xyz[0] - 0x7000);
    float uly = 2.0f * (0.0625f * (float)(xyz[1] - 0x7900)) - 40.0f;
    if (ulx < -512.0f) return;
    if (!(ulx <= 512.0f)) return;
    if (uly < -224.0f) return;
    if (!(uly <= 448.0f)) return;
    GS_NewList();
    GS_SetRGBA(rgb);
    amap_tex0(STA2_TEXTURE[5]);
    StatFont_DrawString(ps2_ftoi(ulx), ps2_ftoi(uly), -1, -1, str, 0x111);
    GS_FlushList();
}

/* "LOCATION NOW" at the player, then the map's own place names */
static void __AMAP_DrawStrings(struct amap_mng_tag *amp)
{
    PS2_FRAME(0x40);
    PS2_STACK(float[4], loc, 0x30);
    GS_NewList();
    GS_SetTEXFLUSH();
    GS_SetTEST_1(0, 0, 0, 0, 0, 0, 1, 1);
    amap_tex1(STA2_TEX1[0]);
    GS_SetCLAMP_1(1, 1, 0, 0, 0, 0);
    GS_SetALPHA_1(0, 1, 0, 1, 0);
    GS_FlushList();
    DF_AMAP_DrawString(amp->loc, AmapStrColor[0], AmapString[0]);
    for (int ii = 0; ii < NowAmapString; ii++) {
        struct struct_4aa2628a *e = &MapAmapString[ii];
        if (!e->on) continue;
        int id = e->id;   /* signed byte index, no range check */
        if (e->mp == nullptr) {
            memcpy(loc, e->loc, 16);
        } else {
            if (e->mp->use & 1) continue;
            /* lqc2 vf16 = mp->loc, vf17 = e->loc, vadd.xyz vf16 (w stays mp->loc[3]), sqc2 */
            float v[4];
            memcpy(v, e->mp->loc, 16);
            memcpy(&VU0()->vf[17], e->loc, 16);
            v[0] = v[0] + e->loc[0];
            v[1] = v[1] + e->loc[1];
            v[2] = v[2] + e->loc[2];
            memcpy(&VU0()->vf[16], v, 16);
            memcpy(loc, v, 16);
        }
        if (e->disp && !(amp->atlas & 2)) continue;
        DF_AMAP_DrawString(loc, AmapStrColor[id], AmapString[id]);
    }
}

/* ---- geometry: crosshair, compass, model search ------------------------------------------------------------- */

static const float AMAP_PI = 3.1415927f;     /* 0x40490fdb */
static const float AMAP_2PI = 6.2831855f;    /* 0x40c90fdb */

/* a draw_type 2 sprite = 2D line (ulx, uly) -> (wd, ht) */
static void amap_line(int id, int x0, int y0, int x1, int y1)
{
    struct amap_spr_tag *s = &AmapSpr[id];
    s->ulx = (short)x0;
    s->uly = (short)y0;
    s->wd = (short)x1;
    s->ht = (short)y1;
}

/* the crosshair: lines through the player's screen point; xystat -1 voice, 0 slide in, 1 the four corner sprites,
 * 2 shrink the box, >= 3 follow */
static void __AMAP_MoveLine(void)
{
    PS2_FRAME(0x80);
    PS2_STACK(int[4], ivec, 0x60);
    PS2_STACK(short[2], add, 0x7c);
    unsigned int addw = 0x0009000eu;   /* lwc1 / swc1 of the .sdata word 0x27af00 (a denormal): add = {14, 9} */
    memcpy(add, &addw, 4);
    DF_CALC_ScreenPointEx(ivec, AmapWorldScreen, g_ac[0].loc);
    ivec[0] = (ivec[0] >> 4) - 0x700;
    ivec[1] = (((ivec[1] >> 4) - 0x800) * 2) + 0xe0;
    struct amap_mng_tag *amp = &AmapMng;
    switch (amp->xystat) {
    case -1:
        Voice_Call(0x41, 0x7f);
        amp->xystat++;
        /* fall through */
    case 0: {
        unsigned char cnt = 0;
        for (int i = 0; i < 2; i++) {
            int tgt = ivec[i], cur = amp->xyloc[i];
            int d = cur - tgt;
            if (d < 0) d = -d;
            int step = add[i];
            if (!(step < d)) {
                amp->xyloc[i] = (short)tgt;
                cnt++;
            } else if (tgt < cur) {
                amp->xyloc[i] = (short)(cur - step);   /* unreached in garage3 */
            } else {
                amp->xyloc[i] = (short)(cur + step);
            }
        }
        amap_line(28, 0, amp->xyloc[1], 0x200, amp->xyloc[1]);
        amap_line(29, amp->xyloc[0], 0, amp->xyloc[0], 0x1c0);
        if (cnt == 2) amp->xystat++;
        return;
    }
    case 1:
        DF_AMAP_SetSpr(0x1e, 5, 1, 2, 0, -0x13, -0x14, 0x14, -0x14, 0.0f, 0.0f, 0.0f, 0.0f, 0x54, 0x54, 0x54, 0);
        DF_AMAP_SetSpr(0x1f, 5, 1, 2, 0, -0x13, 0x14, 0x14, 0x14, 0.0f, 0.0f, 0.0f, 0.0f, 0x54, 0x54, 0x54, 0);
        DF_AMAP_SetSpr(0x20, 5, 1, 2, 0, -0x14, -0x14, -0x14, 0x16, 0.0f, 0.0f, 0.0f, 0.0f, 0x54, 0x54, 0x54, 0);
        DF_AMAP_SetSpr(0x21, 5, 1, 2, 0, 0x14, -0x14, 0x14, 0x16, 0.0f, 0.0f, 0.0f, 0.0f, 0x54, 0x54, 0x54, 0);
        amp->xyloc[0] = 0x100;
        amp->xystat++;
        /* fall through */
    case 2:
        amp->xyloc[0] -= 0x14;
        if (amp->xyloc[0] < 0x15) amp->xystat++;
        break;
    default:
        break;
    }
    int x = ivec[0], y = ivec[1];
    amap_line(28, 0, y, 0x200, y);
    amap_line(29, x, 0, x, 0x1c0);
    int r = amp->xyloc[0];
    amap_line(30, x - (r - 1), y - r, r + x, y - r);
    amap_line(31, x - (r - 1), r + y, r + x, r + y);
    amap_line(32, x - r, y - r, x - r, r + 2 + y);
    amap_line(33, r + x, y - r, r + x, r + 2 + y);
}

/* v normalised into [-pi, pi] (c.ole / bc1t: while !(v <= pi)) */
static float amap_wrap_pi(float v)
{
    while (!(v <= AMAP_PI)) v -= AMAP_2PI;
    while (v < -AMAP_PI) v += AMAP_2PI;
    return v;
}

/* the compass: a textured 4-vertex strip rotated by the camera angles, built on the stack and DMA'd from there (D2
 * chain, TTE) */
static void DF_AMAP_DrawCompass(struct amap_mng_tag *amp)
{
    PS2_FRAME(0x250);
    PS2_STACK(unsigned int[64], buffer, 0x40);
    PS2_STACK(int[4][4], loc, 0x140);
    PS2_STACK(float[4][4], matrix, 0x180);
    PS2_STACK(int[4][4], uv, 0x1c0);
    PS2_STACK(float[4][4], loc0, 0x200);
    PS2_STACK(float[2], ang, 0x240);
    PS2_STACK(struct_76b810ec, mcol, 0x24c);
    static const float loc0_init[4][4] = {{-3.0f, -3.0f, 0.0f, 1.0f}, {-3.0f, 3.0f, 0.0f, 1.0f},
                                          {3.0f, -3.0f, 0.0f, 1.0f}, {3.0f, 3.0f, 0.0f, 1.0f}};   /* 0x25ef40 */
    static const int uv_init[4][4] = {{0, 0x788, 0, 0}, {0, 0, 0, 0}, {0, 0x788, 0, 0}, {0, 0, 0, 0}};   /* 0x25ef80 */
    memcpy(loc0, loc0_init, sizeof loc0_init);
    memcpy(uv, uv_init, sizeof uv_init);
    uv[1][1] = 0xff0;
    uv[3][1] = 0xff0;
    uv[2][0] = 0x870;
    uv[3][0] = 0x870;
    memset(ang, 0, 8);   /* byte loop over sp+0x240..0x247 */
    ang[0] = amp->angx - ps2_u2f(0x3fc90fdb);
    ang[1] = amp->angy;
    ang[0] = amap_wrap_pi(ang[0]);
    ang[1] = amap_wrap_pi(ang[1]);
    sceVu0RotMatrixZ(matrix, IdentityMtx, -ang[1]);   /* SDK RotMatrix* are frameless (ra kept in a2): no ra slot */
    sceVu0RotMatrixX(matrix, matrix, ang[0]);
    for (int i = 0; i < 4; i++) {
        DF_CALC_ScreenPointEx(loc[i], matrix, loc0[i]);
        loc[i][0] += 0x1b8;
        loc[i][1] += 0x58;
    }
    unsigned int *p = MakeDMA(buffer, 0xe, 7);
    p = MakeGIF(p, 1, 1, 0x154, 0, 0xd, 0x000434343431eeeeull);
    p = MakeREGS(p, 8, 5);              /* CLAMP_1 */
    p = MakeREGS(p, 0x47, 0x30000);     /* TEST_1 */
    p = MakeREGS(p, 0x42, 0x48);        /* ALPHA_1 */
    p = MakeREGS(p, 6, AmapSprTex0[0]); /* TEX0_1 */
    DF_MFUNC_GetCockpitColor(&mcol, g_gamedata[0].cockpitcol);
    p = MakePACKET(p, mcol.r, mcol.g, mcol.b, mcol.a);
    for (int i = 0; i < 4; i++) {
        p = MakePACKET(p, uv[i][0], uv[i][1], 0, 0);
        p = MakePACKET(p, (loc[i][0] + 0x6c0) << 4, (loc[i][1] << 3) + 0x7900, 0x1000000, 0);
    }
    sceGsSyncPath(0, 0);
    SyncDCache(buffer, buffer + 64);
    ps2_dma_kick_qmt(2, 0x145, 0, 0, PS2PTR(buffer));   /* original: QWC 0, MADR 0, TADR, SyncDCache, CHCR */
    sceGsSyncPath(0, 0);
}

/* one model array's slice [start, end): mark models whose sphere (R + 30, xz only) holds the probe point inside the
 * y band; first-time finds count towards AmapRecNum; without `memory` the others are un-flagged */
static void amap_search(StModel *base, int start, int end, unsigned char *flag, unsigned char *rec, const float *loc2,
                        float *d, int memory)
{
    StModel *m = base + start;
    for (int i = start; i < end; i++, m++) {
        if (m->use & 1) continue;
        int inside = 0;
        if (m->type == 1 || m->type == 2 || m->type == 8) {
            if (loc2[1] <= 10.0f + m->Ybottom && !(loc2[1] < m->Ytop - 10.0f)) {
                const float *sph = m->mlist->sphere;
                vu_sub_xyz(d, loc2, sph);
                memcpy(&VU0()->vf[16], d, 16);   /* vf16 = loc2 - sphere (w = loc2.w), vf17 = sphere */
                memcpy(&VU0()->vf[17], sph, 16);
                d[3] = 1.0f;
                float R = 30.0f + m->mlist->R;
                inside = (d[0] * d[0] + d[2] * d[2] <= R * R);
            }
        }
        int idx = i >> 3;
        unsigned char bit = (unsigned char)(1 << (i & 7));
        if (inside) {
            flag[idx] |= bit;
            if (!(m->mflag & 0x80) && !(rec[idx] & bit) && !DF_GIMK_IsDestroyModel(m)) {
                rec[idx] |= bit;
                AmapRecNum++;
            }
        } else if (!memory) {
            flag[idx] &= (unsigned char)~bit;
        }
    }
}

/* marks the map / object models near a probe point 35 units in front of loc (heading angy); 1/8 of the arrays per
 * frame by FRAME & 7 (6, 7: nothing) */
void DF_AMAP_SearchModel(float *loc, float angy, int memory)
{
    PS2_FRAME(0x140);
    PS2_STACK(int, ednum, 0xa0);
    PS2_STACK(float[4], dobj_d, 0xb0);
    PS2_STACK(float[4], obj_d, 0xc0);
    PS2_STACK(float[4], map_d, 0xd0);
    PS2_STACK(float[4], svec, 0xe0);
    PS2_STACK(float[4], loc2, 0xf0);
    PS2_STACK(float[4][4], mat, 0x100);
    static const float svec_init[4] = {0.0f, 0.0f, 1.0f, 1.0f};   /* 0x25eef0 */
    ednum = 0;
    memcpy(svec, svec_init, 16);
    if (g_sortie.pwork->isPlayPolyg == 1) return;
    if (MyI.padoff == 1) return;
    float t = AMAP_PI - angy;
    if (!(t <= AMAP_PI)) t -= AMAP_2PI;
    if (t < -AMAP_PI) t += AMAP_2PI;
    memcpy(loc2, PS2_LQ_SRC(loc), 16);
    sceVu0RotMatrixY(mat, IdentityMtx, -t);
    /* svec = mat * svec (vmulax / madday / maddaz / maddw); ACC holds the first three terms */
    float acc[4];
    for (int i = 0; i < 4; i++) acc[i] = (mat[0][i] * svec[0] + mat[1][i] * svec[1]) + mat[2][i] * svec[2];
    vu_apply(svec, mat, svec);
    svec[0] = svec[0] * 35.0f;   /* vmulx.xyz by vf31.x = 0x420c0000, w kept */
    svec[1] = svec[1] * 35.0f;
    svec[2] = svec[2] * 35.0f;
    loc2[0] = loc2[0] + svec[0];  /* vadd.xyz, w kept */
    loc2[1] = loc2[1] + svec[1];
    loc2[2] = loc2[2] + svec[2];
    /* oracle VU0 state: vf1-4 = mat, vf16 = loc2, vf17 = svec, vf31 = qmtc2 of v1 after `lui 0x420c` (0x1db048):
     * the lui writes the low 64 bits, the upper 64 still hold svec_init.zw from the lq at 0x1daf68 */
    for (int i = 0; i < 4; i++) memcpy(&VU0()->vf[1 + i], mat[i], 16);
    memcpy(&VU0()->vf[16], loc2, 16);
    memcpy(&VU0()->vf[17], svec, 16);
    {
        unsigned int w31[4] = {0x420c0000u, 0u, 0x3f800000u, 0x3f800000u};
        memcpy(&VU0()->vf[31], w31, 16);
    }
    memcpy(&VU0()->acc, acc, 16);
    int ms = 0, me = 0, os = 0, oe = 0, ds = 0;
    switch (FRAME & 7) {
    case 0: me = Array->head.mnum >> 1; break;
    case 1: me = Array->head.mnum; ms = me >> 1; break;
    case 2: oe = Array->head.onum >> 1; break;
    case 3: oe = Array->head.onum; os = oe >> 1; break;
    case 4: ednum = Array->head.dnum >> 1; break;
    case 5: ednum = Array->head.dnum; ds = ednum >> 1; break;
    default: break;
    }
    amap_search(Map, ms, me, AmapMapFlag, AmapMapRecFlag, loc2, map_d, memory);
    amap_search(Obj, os, oe, AmapObjFlag, AmapObjRecFlag, loc2, obj_d, memory);
    amap_search(Dobj, ds, ednum, AmapDobjFlag, AmapDobjRecFlag, loc2, dobj_d, memory);
}

/* is the model shown on the map? mode 0: found by SearchModel (flag bit at aid); 1: use bit 5 clear; 2 / 3: any
 * visible model */
static int __AMAP_DispCheck(struct StModel *mp, int mode)
{
    unsigned char *flag;
    switch (mp->type) {
    case 1: flag = AmapMapFlag; break;
    case 2: flag = AmapObjFlag; break;
    case 8: flag = AmapDobjFlag; break;
    default: return 0;
    }
    if (mode == 3 || mode == 2) {
        if (mp->mflag & 0x80) return 0;
        if (mp->use & 1) return 0;
        return 1;
    }
    if (mode == 1) return (mp->use & 0x20) ? 0 : 1;
    if (mp->mflag & 0x80) return 0;
    if (mp->use & 1) return 0;
    unsigned int i = mp->aid;
    return (flag[i >> 3] & (1 << (i & 7))) ? 1 : 0;
}

/* ---- frame loop: display list, camera, pad, step machine ---------------------------------------------------- */

#ifdef ACNB_SOURCEPORT
/* A+D pairs XYOFFSET_1 / 2 (0x7000, 0x7900), SCISSOR_1 / 2 (0..0x1ff, 0..0xdf) */
union union_7a05e625 viewport = {{{{0x0000790000007000ll, 0x18, 0x0000790000007000ll, 0x19, 0x00df000001ff0000ll, 0x40,
                                    0x00df000001ff0000ll, 0x41}}}};   /* 0x277ea0 */
float AmapClearColor[4] = {0.0f, 0.0f, 0.0f, 100.0f};   /* 0x277ef0 */
/* stage_cam_dist (0x278100, float[256]) is ELF_DATA (scripts/sp_missing.py) */
#endif

static const float AMAP_ANGX_MAX = 1.5533430f;   /* 0x3fc6d3f2 */

/* one VIF / DMA packet: cnt QWC 1 {0, 0, 0, UNPACK V4-32 x4 to `vif`}, ref QWC 4 to matrix m (a PS2 address: the
 * list is kicked next frame) */
static unsigned int *amap_ref(unsigned int *p, unsigned int vif, const void *m)
{
    p[0] = 0x10000001u;
    p[1] = 0;
    p[2] = 0;
    p[3] = 0;
    p[4] = 0;
    p[5] = 0;
    p[6] = 0;
    p[7] = vif;
    p[8] = 0x30000004u;
    p[9] = (unsigned int)PS2PTR(m);
    p[10] = 0;
    p[11] = 0;
    return p + 12;
}

static void __AMAP_MakeDL(void)
{
    PS2_FRAME(0x40);
    PS2_STACK(unsigned int, dummyDMA, 0x38);
    PS2_STACK(unsigned int, gpw, 0x3c);
    dummyDMA = 0x30224000u;
    unsigned int *gp = (unsigned int *)DF_MkDL_GetBuffer();
    MapKickP = (char *)gp;
    DF_MkDL_View(&gp, &viewport, 3);
    /* quirk: lwc1 / swc1 of the int 0x30224000 over two unit-matrix entries every frame */
    memcpy(&AmapLight[0][1], &dummyDMA, 4);
    memcpy(&AmapLight[1][0], &dummyDMA, 4);
    FlushCache(0);
    gp = amap_ref(gp, 0x6c040000u, AmapWorldScreen);
    gp = amap_ref(gp, 0x6c04001cu, AmapWorldView);
    gp = amap_ref(gp, 0x6c040010u, AmapLight);
    gp = amap_ref(gp, 0x6c04000cu, AmapLight);
    StModel *mp = Map;
    for (int i = 0; i < Array->head.mnum; i++, mp++) {
        mp->clip = 0;
        if (__AMAP_DispCheck(mp, AmapDispMode)) DF_MkDL_Model(&gp, mp, 0, 0);
    }
    mp = Obj;
    for (int i = 0; i < Array->head.onum; i++, mp++) {
        mp->clip = 0;
        if (__AMAP_DispCheck(mp, AmapDispMode)) DF_MkDL_Model(&gp, mp, 0, 0);
    }
    mp = Dobj;
    for (int i = 0; i < Array->head.dnum; i++, mp++) {
        mp->clip = 0;
        if (__AMAP_DispCheck(mp, AmapDispMode)) DF_MkDL_Model(&gp, mp, 0, 0);
    }
    DF_MkDL_ACModel(&gp, MyAC, 0, 0);
    GL_Sync(&gp);
    DF_MkDL_View(&gp, &viewport, 2);
    GL_EndList(&gp);
    gpw = (unsigned int)PS2PTR(gp);   /* the original's cursor slot */
}

/* the move sound: (re)started whenever no voice is held (vh >= 0x50, also after a failed call) */
static void amap_move_voice(struct amap_mng_tag *amp)
{
    if (amp->vh >= 0x50) amp->vh = (unsigned char)Voice_CallLoop(0x43, 0x7f);
}

/* at.xyz += / -= step * AmapWorldView column c (mul.s, then add.s / sub.s) */
static void amap_move_at(struct amap_mng_tag *amp, int c, int k, float step)
{
    for (int i = 0; i < 3; i++) {
        float d = step * AmapWorldView[i][c];
        amp->at[i] = k > 0 ? amp->at[i] + d : amp->at[i] - d;
    }
}

/* camera: zoom (0x800 / 0x400, both = reset), translate (0x2000 held) or rotate by the pad bits, then the look-at
 * view, AmapWorldScreen = AmapViewScreen * AmapWorldView, copied over the game's world_screen_matrix */
static void __AMAP_CamCtrl(struct amap_mng_tag *amp)
{
    PS2_FRAME(0x60);
    PS2_STACK(float, upz, 0);   /* gluLookAtF's 9th float */
    unsigned char sndflag = 0;
    float step = ps2_div(amp->movspd * amp->dist, 400.0f);
    unsigned short pad = amp->pad1;
    if ((pad & 0x800) && (pad & 0x400)) {
        amp->angx = AMAP_ANGX_MAX;
        amp->angy = amp->ang[1];
        amp->dist = stage_cam_dist[g_sortie.pconstant->map];   /* no bounds check */
        memcpy(amp->at, amp->loc, 16);
    } else {
        if (pad & 0x800) {
            amp->dist = amp->dist - step;
            if (amp->dist < 50.0f) amp->dist = 50.0f;
            else sndflag = 1;
            amap_move_voice(amp);
        }
        if (pad & 0x400) {
            amp->dist = amp->dist + step;
            if (amp->dist <= 1800.0f) sndflag = 1;
            else amp->dist = 1800.0f;
            amap_move_voice(amp);
        }
    }
    if (pad & 0x2000) {
        /* translate (sndflag = 1 sits in the delay slot of the vh test) */
        if (pad & 0x80) {
            sndflag = 1;
            amap_move_voice(amp);
            amap_move_at(amp, 0, 1, step);
        }
        if (pad & 0x20) {
            sndflag = 1;
            amap_move_voice(amp);
            amap_move_at(amp, 0, -1, step);
        }
        if (pad & 0x10) {
            amp->at[0] = amp->at[0] + step * AmapWorldView[0][1];
            sndflag = 1;
            amap_move_voice(amp);
            amp->at[1] = amp->at[1] + step * AmapWorldView[1][1];
            amp->at[2] = amp->at[2] + step * AmapWorldView[2][1];
        }
        if (pad & 0x40) {
            sndflag = 1;
            amap_move_voice(amp);
            amap_move_at(amp, 1, -1, step);
        }
    } else {
        /* rotate; the clamps test the unclamped value */
        if (pad & 0x10) {
            amp->angx = amp->angx + amp->rotspd;
            if (amp->angx <= AMAP_ANGX_MAX) sndflag = 1;
            else amp->angx = AMAP_ANGX_MAX;
            amap_move_voice(amp);
        }
        if (pad & 0x40) {
            amp->angx = amp->angx - amp->rotspd;
            if (amp->angx < -AMAP_ANGX_MAX) amp->angx = -AMAP_ANGX_MAX;
            else sndflag = 1;
            amap_move_voice(amp);
        }
        if (pad & 0x80) {
            sndflag = 1;
            amap_move_voice(amp);
            amp->angy = amp->angy + amp->rotspd;
        }
        if (pad & 0x20) {
            sndflag = 1;
            amap_move_voice(amp);
            amp->angy = amp->angy - amp->rotspd;
        }
    }
    if (!sndflag && amp->vh < 0x50) {
        Voice_Stop(amp->vh, 0);
        amp->vh = 0x50;
    }
    ang2vec(amp->vec, amp->angy, amp->angx);
    memcpy(amp->eye, amp->at, 16);
    amp->eye[0] = amp->eye[0] + amp->dist * amp->vec[0];
    amp->eye[1] = amp->eye[1] + amp->dist * amp->vec[1];
    amp->eye[2] = amp->eye[2] + amp->dist * amp->vec[2];
    upz = amp->up[2];
    gluLookAtF(AmapWorldView, amp->eye[0], amp->eye[1], amp->eye[2], amp->at[0], amp->at[1], amp->at[2], amp->up[0],
               amp->up[1], amp->up[2]);
    /* vf1-4 = AmapViewScreen rows, vf22-25 = AmapWorldView rows replaced by the vmulax / madday / maddaz / maddw chain */
    vu_mul_matrix(AmapWorldScreen, (const float (*)[4])AmapViewScreen, (const float (*)[4])AmapWorldView);
    {
        float acc[4];
        for (int i = 0; i < 4; i++)
            acc[i] = (AmapViewScreen[0][i] * AmapWorldView[3][0] + AmapViewScreen[1][i] * AmapWorldView[3][1]) +
                     AmapViewScreen[2][i] * AmapWorldView[3][2];
        for (int i = 0; i < 4; i++) memcpy(&VU0()->vf[1 + i], AmapViewScreen[i], 16);
        for (int i = 0; i < 4; i++) memcpy(&VU0()->vf[22 + i], AmapWorldScreen[i], 16);
        memcpy(&VU0()->acc, acc, 16);
    }
    sceVu0CopyMatrix(world_screen_matrix, AmapWorldScreen);   /* SDK CopyMatrix is frameless: no saved-ra slot */
}

/* step machine: 0 fade in (13 frames), 1 wait for trigger bit 1, 2 fade out (13 frames, then leave), 3 pause menu */
static int __AMAP_Ctrl(struct amap_mng_tag *amp)
{
    PS2_FRAME(0x40);
    unsigned short s0 = amp->step;
    unsigned short pad1 = amp->pad1, pad1trg = amp->pad1trg;
    int ret = 0;
    switch (s0) {
    case 0:
        amp->framecnt++;
        if (amp->framecnt >= 0xd) amp->step++;
        break;
    case 1:
        if (pad1trg & 1) {
            amp->retval = 0;
            amp->step++;
        }
        break;
    case 2:
        amp->framecnt++;
        if (amp->framecnt >= 0xd) ret = 1;
        break;
    case 3: {
        int r = DF_PAUSE_Proc(pad1, pad1trg);
        if (r == 2) {
            amp->retval = 0;
            amp->step = 1;
        } else if (r == 1) {
            amp->retval = 1;
            amp->step = 2;
        }
        break;
    }
    default:
        break;
    }
    if (s0 != amp->step) amp->framecnt = 0;
    return ret;
}

/* pad bits + the left stick (last frame's, read before the update) as 0x10 / 0x40 (v) and 0x80 / 0x20 (h) */
static void __AMAP_Pad(struct amap_mng_tag *amp)
{
    PS2_FRAME(0x40);
    unsigned char l3h = Pad_GetL3h(0);
    unsigned char l3v = Pad_GetL3v(0);
    amp->pad1b = (unsigned short)~amp->pad1;
    Pad_UpdateData();
    amp->pad1 = Pad_GetOnBits(0);
    amp->pad1trg = Pad_GetTriggerBits(0);
    if (l3v < 0x37) amp->pad1 |= 0x10;
    else if (l3v >= 0xc9) amp->pad1 |= 0x40;
    if (l3h < 0x37) amp->pad1 |= 0x80;
    else if (l3h >= 0xc9) amp->pad1 |= 0x20;
}

/* the blocking frame loop: one iteration per frame until __AMAP_Ctrl returns > 0 */
static void __AMAP_Exec(struct amap_mng_tag *amp)
{
    PS2_FRAME(0x50);
    int loop = 1;
    do {
        amp->snd = 0;
        Draw_FlipBufferTearing();
        Draw_KickDmaDl();
        __AMAP_Pad(amp);
        if (__AMAP_Ctrl(amp) > 0) loop = 0;
        __AMAP_CamCtrl(amp);
        for (int i = 0; i < 0x30; i++) {
            struct amap_spr_tag *s = &AmapSpr[i];
            if (s->func) gcall(s->func, s, amp);
        }
        __AMAP_MakeDL();
        Snd_Exec();
        ps2_dma_wait_vif1();
        sceGsSyncPath(0, 0);
        __AMAP_DrawSpr();
        __AMAP_DrawStrings(amp);
        __AMAP_MoveLine();
        DF_AMAP_DrawCompass(amp);
        ps2_dma_wait_gif();
        sceGsSyncPath(0, 0);
    } while (loop);
}

static void __AMAP_Init(struct amap_mng_tag *amp, float *loc, float *ang, unsigned int atlas)
{
    PS2_FRAME(0x90);
    PS2_STACK(unsigned long long[5], args, 0);      /* SetSpr's stack arguments (last call's values stay) */
    PS2_STACK(unsigned char[2], dead, 0x8c);        /* two dead bytes: atlas & 1, atlas & 2 */
    DF_SND_Mute(1);
    DF_SND_ChangeVCMode(1);
    ZeroMemory32b(amp, 0x28);   /* only up to pad[4] */
    amp->reverb = -1;
    amp->vh = 0x50;
    AmapDispMode = 0;
    amp->atlas = (unsigned char)atlas;
    amp->dist = stage_cam_dist[g_sortie.pconstant->map];
    amp->rotspd = ps2_u2f(0x3cd67750);   /* 0.026179938 = 1.5 deg */
    amp->movspd = ps2_u2f(0x40d55555);   /* 6.6666665 */
    memcpy(amp->ang, PS2_LQ_SRC(ang), 16);
    memcpy(amp->loc, PS2_LQ_SRC(loc), 16);
    memcpy(amp->at, amp->loc, 16);
    memcpy(amp->eye, amp->loc, 16);
    amp->angx = AMAP_ANGX_MAX;
    amp->angy = amp->ang[1];
    ang2vec(amp->vec, amp->angy, amp->angx);
    amp->eye[0] = amp->eye[0] + amp->dist * amp->vec[0];
    amp->eye[1] = amp->eye[1] + amp->dist * amp->vec[1];
    amp->eye[2] = amp->eye[2] + amp->dist * amp->vec[2];
    amp->up[0] = 0.0f;
    amp->up[1] = 1.0f;
    amp->up[2] = 0.0f;
    amp->up[3] = 1.0f;
    ps2_dma_wait_vif1();
    ps2_dma_wait_gif();
    sceGsSyncPath(0, 0);
    DF_SetDBuffClrCol(&DBUFF, AmapClearColor);
    DF_SetDBuffClrCol(&DBUFF2, AmapClearColor);
    Draw_FlipBufferTearing();
    DF_DMA_Send(1, 1, g_pmpgVu1Amap, 1);
    MapKickP = nullptr;
    DF_ReadTMXPointer(AmapData, 1);
    amp->reverb = (char)fssdsynthAddReverb(4, 0x2700, 0x2700, 0x78, 0x78, 0x2700, 0x2700, 0);
    if (amp->reverb >= 0 && amp->reverb < 4) fssdsynthSetReverb(amp->reverb);
    amp->xyloc[0] = 0;
    amp->xyloc[1] = 0;
    amp->xystat = -1;
    sceVu0UnitMatrix(AmapLight);   /* SDK UnitMatrix is frameless: no saved-ra slot */
    FlushCache(0);
    ps2_memset(AmapSpr, 0, sizeof AmapSpr);
    dead[0] = 0;
    dead[1] = 0;
    dead[0] = (unsigned char)(amp->atlas & 1);
    dead[1] = (unsigned char)(amp->atlas & 2);
    /* (sprid, type, blend, draw, istate, ulx, uly, wd, ht, uls, ult, wds, htt, r, g, b, a) */
    DF_AMAP_SetSpr(0, 4, 0, 0, 0, 0, 0, 0x200, 0x28, 216.0f, 0.0f, 40.0f, 40.0f, 0x80, 0x80, 0x80, 0x40);
    DF_AMAP_SetSpr(1, 4, 0, 0, 0, 0, 0x198, 0x200, 0x28, 216.0f, 0.0f, 40.0f, 40.0f, 0x80, 0x80, 0x80, 0x40);
    DF_AMAP_SetSpr(2, 4, 1, 0, 0, 0x16, 0x14, 0xc8, 0x12, 0.0f, 0.0f, 200.0f, 18.0f, 0x80, 0x80, 0x80, 0x80);
    DF_AMAP_SetSpr(3, 4, 1, 0, 0, 0x11c, 0x19c, 0xcc, 0x10, 0.0f, 18.0f, 204.0f, 16.0f, 0x80, 0x80, 0x80, 0x80);
    DF_AMAP_SetSpr(0x1c, 6, 1, 2, 0, 0, 0, 0x200, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0x54, 0x54, 0x54, 0x80);
    DF_AMAP_SetSpr(0x1d, 6, 1, 2, 0, 0, 0, 0, 0x1c0, 0.0f, 0.0f, 0.0f, 0.0f, 0x54, 0x54, 0x54, 0x80);
    args[0] = 0x1c0;
    args[1] = 0x54;
    args[2] = 0x54;
    args[3] = 0x54;
    args[4] = 0x80;
}

/* the area map screen (AreaMap): set up, run until closed, restore the sound / VU1 state; returns retval */
int DF_AMAP_MainLoop(float *loc, float *ang, unsigned int atlas)
{
    PS2_FRAME(0x20);
    struct amap_mng_tag *amp = &AmapMng;
    __AMAP_Init(amp, loc, ang, atlas);
    __AMAP_Exec(amp);
    ps2_dma_wait_vif1();
    ps2_dma_wait_gif();
    sceGsSyncPath(0, 0);
    Draw_FlipBufferTearing();
    DF_DMA_Send(1, 1, g_pmpgVu12f, 1);
    MapKickP = nullptr;
    if (amp->vh < 0x50) {   /* unreached: Exec leaves vh at 0x50 */
        Voice_Stop(amp->vh, 0);
        amp->vh = 0x50;
    }
    ps2_dma_wait_vif1();
    ps2_dma_wait_gif();
    sceGsSyncPath(0, 0);
    Draw_FlipBufferTearing();
    if (amp->reverb >= 0 && amp->reverb < 4) {
        fssdsynthRemoveReverb(amp->reverb);
        amp->reverb = -1;
    }
    DF_SND_ResumeReveb();
    DF_SND_Mute(0);
    DF_SND_ChangeVCMode(0);
    return amp->retval;
}
