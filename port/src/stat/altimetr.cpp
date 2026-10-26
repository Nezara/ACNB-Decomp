/* src/stat/altimetr.cpp - the cockpit HUD altimeter (whole file): two pairs of bracket sprites and two mirrored
 * 8-px ladders (left x 0xa6, right x 0x153) whose texture rows scroll with the AC's height above the stage's zero
 * level (yMapZero). */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

/* col: unused (both callers pass STA2_RGBA; protofix: dropped from DWARF). Quirks kept: the draw offset is read
 * and never used; the ladder is always 0xc8 px high (rest + 4 * 0x28 + iyy). */
void Altimeter_TypeA(int player, int *col, int ofs_y)
{
    PS2_FRAME(0x80);
    (void)col;
    PS2_STACK(float, f20_save, 0);
    f20_save = PS2_STALE_FPR(20);   /* the prologue's swc1 f20, 0(sp) */
    if (player < 0 || player >= 4) return;
    PS2_STACK(int, ofsUlx, 0x7c);
    PS2_STACK(int, ofsUly, 0x78);
    PS2_STACK(int, ofsUrx, 0x74);
    PS2_STACK(int, ofsUry, 0x70);
    PS2_STACK(int, flipshift, 0x6c);
    GS_GetDrawOffset(&ofsUlx, &ofsUly, &ofsUrx, &ofsUry, &flipshift);
    float height = MyAC[player].loc[1] - Array->head.sysinfo.yMapZero;
    PS2_STACK(int[4], mycol, 0x50);
    DF_ACST2_GetMyCol(mycol, player);
    GS_NewList();
    GS_SetRGBA(mycol);
    struct_f386fb29 t;
    memcpy(&t, &STA2_TEXTURE[5], 8);
    GS_SetTEX0_1(t);
    GS_SetALPHA_1(0, 1, 0, 1, 0);
    GS_DrawSprite(0xa0, ofs_y + 0x78, 0xe6, 0x34, 0x10, 6);
    GS_DrawSprite(0x150, ofs_y + 0x78, 0xe6, 0x34, 0x10, 6);
    GS_DrawSprite(0xa0, ofs_y + 0x142, 0xd6, 0x34, 0x10, 6);
    GS_DrawSprite(0x150, ofs_y + 0x142, 0xd6, 0x34, 0x10, 6);
    /* scroll phase: (40000 + height) * 40 / 50 modulo 40, made even */
    float yy = (40.0f * (40000.0f + height)) / 50.0f;
    float q = (float)ps2_ftoi(yy / 40.0f);
    int iyy = ps2_ftoi(yy - 40.0f * q);
    iyy += iyy % 2;
    int by = ofs_y + 0x7c;
    int rest = 0x28 - iyy;
    GS_DrawSpriteEx(0xa6, by, 0xf8, 0xd8 + iyy, 8, rest, 0);
    GS_DrawSpriteEx(0x153, by, 0xf8, 0xd8 + iyy, 8, rest, 1);
    by += rest;
    for (int i = 0; i < 4; i++) {
        GS_DrawSpriteEx(0xa6, by, 0xf8, 0xd8, 8, 0x28, 0);
        GS_DrawSpriteEx(0x153, by, 0xf8, 0xd8, 8, 0x28, 1);
        by += 0x28;
    }
    GS_DrawSpriteEx(0xa6, by, 0xf8, 0xd8, 8, iyy, 0);
    GS_DrawSpriteEx(0x153, by, 0xf8, 0xd8, 8, iyy, 1);
    GS_FlushList();
}
