/* src/stat/stattex.cpp - HUD texture files (whole file, OLSORTIE overlay): StatTex_ReadTextureFile loads the
 * gauge layout textures for the stat type (files 0x47 / 0x49 / 0x4c), the common sight / HUD textures (0x50) and
 * the WIN / LOSE texture (0x51) into the sortie heap; the Load* functions upload them to VRAM on demand. */
#include "acnb/sdk.h"

/* gauge layout: 4 = split screen; otherwise by EO core and maker (also forced for replays) */
int StatTex_MakeType(int split, int maker, int eo)
{
    if (!MyI.replaymode && split) return 4;
    if (eo) return maker ? 2 : 0;
    return maker ? 3 : 1;
}

int StatTex_SetDrawConfigure(int isLinear)
{
    PS2_FRAME(0x10);
    if (!s_isValidLinear || !isLinear) return 0;
    GS_SetDrawOffset(s_cnfLinear.ofsLtX, s_cnfLinear.ofsLtY, s_cnfLinear.ofsRbX, s_cnfLinear.ofsRbY,
                     s_cnfLinear.flipshiftY);
    GS_SetTextureOffset(s_cnfLinear.ofsLtU, s_cnfLinear.ofsLtV, s_cnfLinear.ofsRbU, s_cnfLinear.ofsRbV);
    return 1;
}

int StatTex_LoadWinLoseTexture(void)
{
    PS2_FRAME(0x10);
    DF_ReadTM2Pointer(s_stattex.ptm2WinLose, 1);
    return 1;
}

int StatTex_LoadSightTexture(int stype)
{
    PS2_FRAME(0x10);
    DF_ReadTM2Pointer(s_stattex.ptm2Sight[stype], 1);
    return 1;
}

int StatTex_LoadVariousTexture(int type)
{
    PS2_FRAME(0x10);
    DF_ReadTMXPointer(s_stattex.ptmxSortie[type], 1);
    DF_ReadTMXPointer(s_stattex.ptmxCommon, 1);
    return 1;
}

/* whole file into a new heap block (read through g_pbufRead); 0 on failure */
static char *read_file(int id)
{
    int size = File_GetSize(id);
    int real = File_GetRealSize(id);
    char *p = (char *)Memm_Alloc(1, real, 0x10);
    if (!p) return 0;
    if (!File_NlRead(id, size, g_pbufRead)) return 0;
    CopyMemory(p, g_pbufRead, real);
    return p;
}

int StatTex_ReadTextureFile(int type)
{
    PS2_FRAME(0x40);
    char *p;
    ZeroMemory8b(&s_stattex, 0x30);
    if (MyI.replaymode) type = 0;
    switch (type) {
    case 0:
    case 1:
    case 2:
    case 3:
        if (!(p = read_file(0x47))) return 0;
        s_stattex.ptmxSortie[0] = p;
        s_stattex.ptmxSortie[1] = p;
        if (!(p = read_file(0x49))) return 0;
        s_stattex.ptmxSortie[2] = p;
        s_stattex.ptmxSortie[3] = p;
        break;
    case 4:
        if (!(p = read_file(0x4c))) return 0;
        s_stattex.ptmxSortie[4] = p;
        break;
    case 5:
        if (!(p = read_file(0x47))) return 0;
        s_stattex.ptmxSortie[5] = p;
        break;
    case 6:
        if (!(p = read_file(0x47))) return 0;
        s_stattex.ptmxSortie[6] = p;
        break;
    }
    if (!(p = read_file(0x50))) return 0;
    s_stattex.ptmxCommon = p;
    s_stattex.ptm2Sight[0] = p + *(int *)(p + 0x10);
    s_stattex.ptm2Sight[1] = p + *(int *)(p + 0x20);
    s_stattex.ptm2Sight[2] = p + *(int *)(p + 0x30);
    if (!(p = read_file(0x51))) return 0;
    s_stattex.ptm2WinLose = p;
    StatFont_Init();
    return 1;
}
