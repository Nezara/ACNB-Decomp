/* src/stat/mapst.cpp - map strings (whole file): labels attached to a model or a world point (MapString, up to
 * 32 slots, NowMapString = used count). DF_ACSTAT_DrawMapStringEx projects each visible label (model loc / matrix
 * centre + offset, optionally limited to a camera distance) and __MAPST_DrawStringEx draws a leader line and the
 * text with the status font. */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"

#define A16 __attribute__((aligned(16)))

static void __MAPST_DrawStringEx(int viewno, float *loc, int *rgb, char *str, float (*ws)[4])
{
    PS2_FRAME(0x90);
    float v[4] A16;
    int scr[4] A16;
    int line[2][4] A16;
    memcpy(v, loc, 16);
    v[3] = 1.0f;
    DF_CALC_ScreenPointEx(scr, ws, v);
    memcpy(line[0], scr, 16);
    memcpy(line[1], scr, 16);
    if (!(scr[2] < 0xfffff1) || scr[2] < 0) return;
    line[0][2] = line[1][2] = 0xfffffa;
    line[0][3] = line[1][3] = 0x10;
    line[1][0] += 0x200;
    line[1][1] -= 0x100;
    if (0x6a00 <= scr[0] && scr[0] < 0x9401 && 0x7900 <= scr[1] && scr[1] < 0x8901)
        DF_Draw2DLine_Color(rgb, line, 0x2020c000);
    float x = (float)(scr[0] - 0x7000) * 0.0625f + 32.0f;
    float y = 2.0f * ((float)(scr[1] - 0x7900) * 0.0625f) - 40.0f;
    if (x < -512.0f || !(x <= 512.0f) || y < -224.0f || !(y <= 448.0f)) return;
    GS_NewList();
    GS_SetRGBA(rgb);
    struct_f386fb29 tex0;
    int i = ViewNum == 1 ? 5 : ginfo[viewno].maker ? 9 : 8;
    memcpy(&tex0, &STA2_TEXTURE[i], 8);
    GS_SetTEX0_1(tex0);
    StatFont_DrawString(ps2_ftoi(x), ps2_ftoi(y), -1, -1, str, 0x111);
    GS_FlushList();
}

void DF_ACSTAT_DrawMapStringEx(int view, float (*ws)[4])
{
    PS2_FRAME(0x90);
    if (g_sortie.pwork->isPlayPolyg == 1) return;
    int camid = ViewCamID[view];
    GSX_SetWindowSize(0x100, 0xe0, 4, 3);
    GS_NewList();
    GS_SetRegister(0x42, 0x44);
    GS_SetRegister(0x47, 0x30000);
    GS_FlushList();
    sceGsSyncPath(0, 0);
    float cam[4] A16, pos[4] A16;
    memcpy(cam, CAM[0].vec, 16);
    struct_4aa2628a *e = MapString;
    for (int n = 0; n < NowMapString; n++, e++) {
        if (!e->on) continue;
        StModel *mp = e->mp;
        if (!mp) {
            memcpy(pos, e->loc, 16);
        } else {
            if (e->disp && (!(mp->on_layer & 1 << view) || (mp->use & 1))) continue;
            if (mp->use & 1) continue;
            if (!MyI.livemon && mp == &MyAC[camid]) continue;
            int type = mp->type;
            if (type == 4 || type == 3) {
                if (mp->use & 4) continue;
                memcpy(pos, mp->loc, 16);
            } else if (type == 8 || type == 2 || type == 1) {
                vu_apply(pos, (const float (*)[4])mp->matrix2, mp->localcenter);
            } else {
                memcpy(pos, mp->loc, 16);
            }
            for (int k = 0; k < 3; k++) pos[k] = pos[k] + e->loc[k];   /* vadd.xyz */
        }
        if (!(e->cdist <= ps2_u2f(0x33d6bf95))) {   /* 1e-7 */
            float dx = pos[0] - cam[0], dy = pos[1] - cam[1], dz = pos[2] - cam[2];
            if (!(dz * dz + (dx * dx + dy * dy) <= e->cdist)) continue;
        }
        int id = e->id;
        __MAPST_DrawStringEx(view, pos, AmapStrCol[id], AmapString[id], ws);
    }
    sceGsSyncPath(0, 0);
}

void DF_ACSTAT_DelMapString(int on)
{
    if (!on) return;
    MapString[on - 1].on = 0;
    if (on - 1 == NowMapString) NowMapString--;
}

int DF_ACSTAT_AddMapString(StModel *mp, float x, float y, float z, float dist, int strid)
{
    struct_4aa2628a *e = MapString;
    int i = 0;
    for (; i < NowMapString; i++, e++)
        if (!e->on) break;
    if (i == NowMapString) NowMapString++;
    e->on = (char)(i + 1);
    e->mp = mp;
    e->id = (char)strid;
    e->loc[0] = x;
    e->loc[1] = y;
    e->loc[2] = z;
    e->loc[3] = 1.0f;
    e->cdist = dist * dist;
    return e->on;
}

void MAPSTR_Reset(void)
{
    ZeroMemory32b(MapString, 0x100);
    NowMapString = 0;
}
