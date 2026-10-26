/* src/autogen/AutoGenColor.cpp - colour scheme of an auto-generated Arena opponent AC.
 * AutoGenColor_InitAsmColor (AsmSearchInit, once) fills s_aPilotGenColor[256] with random colour "recipes" (srand(0)
 * first: a constant table); AutoGenColor_CreateAsmColor(out, colorId, pilotId) (__AsmSearch_SetSearchData, once per search
 * result) turns recipe pilotId into the 13 StAcPartsColor entries of the AC, using the camouflage / joint colour tables.
 *
 * The rand() call ORDER is the whole content (the native rand shares newlib's state): an optional re-roll of one
 * recipe field (bAuto), the weapon colour draw, five permutation draws (the last one, % 0x4000, is always 0 but is
 * drawn) and the camouflage draw. The original is six unrolled copies of one per-part loop that differ only in which
 * permutation slot a part's base colour comes from; here that is one loop and a (PartsColorType, part) slot table.
 *
 * Original bug kept: Init stores CamouflageColorType = 1..16 and this indexes s_CamouflageCol[CamouflageColorType]
 * with no -1, so row 0 is never used and 16 reads the 9 bytes after the 16-row table (the first ints of
 * s_CamouflageColRate: colours {8,0,0} {0,8,0} {0,0,8}). agc_camo_byte reproduces the flat read in both builds. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
/* small constants owned by this file (OLMENU / main .data) */
unsigned char s_aJointColorRate[6] = {0x18, 0x2c, 0x3c, 0x4c, 0x5c, 0x64};   /* 0x27b3b8 */
int s_bInitAsmColor;                                                          /* 0x27cfd8 */
unsigned char s_aJointColor[6][3] = {                                         /* 0x1eda980 */
    {80, 80, 80}, {50, 50, 50}, {100, 70, 65}, {90, 80, 60}, {70, 90, 90}, {130, 130, 130},
};
unsigned char s_CamouflageCol[16][3][3] = {                                   /* 0x1ee4b00 */
    {{90, 125, 85}, {100, 55, 35}, {20, 45, 20}},     {{130, 115, 75}, {60, 35, 20}, {30, 60, 30}},
    {{30, 55, 30}, {80, 110, 65}, {20, 20, 20}},      {{45, 65, 90}, {105, 115, 125}, {20, 30, 50}},
    {{125, 125, 115}, {70, 70, 70}, {30, 40, 40}},    {{150, 130, 100}, {100, 90, 65}, {50, 40, 30}},
    {{180, 190, 190}, {140, 140, 140}, {85, 85, 80}}, {{170, 150, 120}, {110, 100, 80}, {95, 40, 30}},
    {{180, 120, 90}, {110, 50, 50}, {40, 40, 40}},    {{70, 70, 70}, {35, 35, 35}, {15, 15, 15}},
    {{130, 140, 150}, {65, 75, 95}, {85, 50, 40}},    {{140, 160, 180}, {40, 110, 180}, {40, 50, 100}},
    {{180, 90, 20}, {200, 150, 100}, {100, 50, 20}},  {{160, 50, 50}, {180, 110, 110}, {90, 20, 20}},
    {{85, 60, 85}, {140, 100, 120}, {25, 25, 45}},    {{40, 40, 40}, {180, 180, 180}, {180, 180, 180}},
};
int s_CamouflageColRate[16] = {8, 8, 8, 8, 8, 8, 6, 6, 6, 6, 6, 5, 5, 5, 5, 2};   /* 0x1ee4b90 */
struct struct_c064b11d s_aPilotGenColor[256];                                 /* 0x1ee8160 (bss) */
#endif

/* (rand() % (n << 14)) >> 14, MWCC's signed remainder (rand is never negative). */
static inline int agc_rnd(int m) { return ps2_mod32(ps2_rand(), m) >> 14; }

/* R(n) for the colour channels: 0..200 (the asm's clamp to 200 is dead for this range) */
static inline int agc_chan(void) { return agc_rnd(0x324000); }

/* Fills the 256 colour recipes. Starts with srand(0), so the table is a constant and the global newlib RNG is left at
 * a constant position afterwards: every draw (incl. the rejection-loop retries) must stay in the original order. */
void __AutoGenColor_InitAsmColor(void)
{
    PS2_FRAME(0xa0);
    ps2_srand(0);
    ps2_memset(s_aPilotGenColor, 0, 0x6000);
    struct struct_c064b11d *e = s_aPilotGenColor;
    for (int id = 0; id < 256; id++, e++) {
        /* detail colour: bright enough in sum, but one channel dark */
        int r, g, b;
        do {
            r = agc_chan();
            g = agc_chan();
            b = agc_chan();
        } while (r + g + b < 300 || !(r < 0x3d || g < 0x3d || b < 0x3d));
        e->DetailColor[0] = (unsigned char)r;
        e->DetailColor[1] = (unsigned char)g;
        e->DetailColor[2] = (unsigned char)b;

        int p = agc_rnd(0x190000);
        for (int i = 0; i < 6; i++)
            if (p < s_aJointColorRate[i]) { e->JointColorId = (unsigned char)i; break; }

        for (int k = 0; k < 5; k++) {
            unsigned char *bc = &e->BaseColor[k].Color[0][0];
            for (int c = 0; c < 3; c++) {
                int t = agc_rnd(0x190000);
                int type = 0;
                if (t < 25) type = 0;
                else if (t < 50) type = 1;
                else if (t < 75) type = 2;
                else if (t < 100) type = 3;
                unsigned char *col = bc + c * 3;
                if (type == 3) {   /* one channel doubled */
                    int a = agc_chan();
                    int d = a * 2;
                    if (a > 200) a = 200;
                    if (d > 200) d = 200;
                    int q = agc_rnd(0xc000);
                    if (q == 2) { col[0] = (unsigned char)d; col[1] = (unsigned char)a; col[2] = (unsigned char)a; }
                    else if (q == 1) { col[0] = (unsigned char)a; col[1] = (unsigned char)d; col[2] = (unsigned char)a; }
                    else if (q == 0) { col[0] = (unsigned char)a; col[1] = (unsigned char)a; col[2] = (unsigned char)d; }
                } else if (type == 2) {   /* three channels, permuted (jump table 0x1ee4cb0) */
                    int s3 = agc_chan(), s2 = agc_chan(), s1 = agc_chan();
                    int q = agc_rnd(0x18000);
                    int o0, o1, o2;
                    switch (q) {
                    case 0: o0 = s3; o1 = s2; o2 = s1; break;
                    case 1: o0 = s3; o1 = s1; o2 = s2; break;
                    case 2: o0 = s2; o1 = s3; o2 = s1; break;
                    case 3: o0 = s2; o1 = s1; o2 = s3; break;
                    case 4: o0 = s1; o1 = s3; o2 = s2; break;
                    case 5: o0 = s1; o1 = s2; o2 = s3; break;
                    default: goto written;
                    }
                    col[0] = (unsigned char)o0; col[1] = (unsigned char)o1; col[2] = (unsigned char)o2;
                } else if (type == 1) {   /* two channels, one of them twice */
                    int a = agc_chan(), d = agc_chan();
                    int q = agc_rnd(0xc000);
                    if (q == 2) { col[0] = (unsigned char)d; col[1] = (unsigned char)a; col[2] = (unsigned char)a; }
                    else if (q == 1) { col[0] = (unsigned char)a; col[1] = (unsigned char)d; col[2] = (unsigned char)a; }
                    else if (q == 0) { col[0] = (unsigned char)a; col[1] = (unsigned char)a; col[2] = (unsigned char)d; }
                } else {   /* grey */
                    int v = agc_chan();
                    col[0] = col[1] = col[2] = (unsigned char)v;
                }
            written:
                /* reject: grey of middle brightness / too grey a colour, then too dark */
                int x = col[0], y = col[1], z = col[2];
                if (type == 0) {
                    if (!(x < 0x3c || x >= 0xb5)) { c--; continue; }
                } else {
                    int avg = (x + y + z) / 3;
                    int lo = avg - 10, hi = avg + 10;
                    if (!(x < lo || hi < x || y < lo || hi < y || z < lo || hi < z)) { c--; continue; }
                }
                if (x < 16 && y < 16 && z < 16) c--;
            }
            /* dull grouping of colours 1 / 2 with a bright colour 0: redo the whole base colour */
            int f = 0;
            if (!(bc[4] < bc[3] - 0x4b) && !(bc[3] + 0x32 < bc[4]) && bc[5] < bc[4]) f = 1;
            if (!(bc[7] < bc[6] - 0x4b) && !(bc[6] + 0x32 < bc[7]) && bc[8] < bc[7]) f = 1;
            if (f && bc[0] + bc[1] + bc[2] >= 0xb4) k--;
        }

        e->bAuto = (unsigned char)(agc_rnd(0x190000) < 10);

        p = agc_rnd(0x190000);
        e->PartsColorType = p < 80 ? 0 : p < 88 ? 1 : p < 93 ? 2 : p < 96 ? 3 : p < 98 ? 4 : p < 100 ? 5 : 0;
        p = agc_rnd(0x190000);
        e->CategoryColorType = p < 10 ? 0 : p < 15 ? 1 : p < 55 ? 2 : p < 85 ? 3 : p < 100 ? 4 : 0;
        p = agc_rnd(0x190000);
        e->WeaponColorType = p < 10 ? 0 : p < 70 ? 1 : p < 100 ? 2 : 0;
        e->WeaponColorId = (unsigned char)agc_rnd(0x18000);
        p = agc_rnd(0x190000);
        e->CamouflageType = p < 85 ? 0 : p < 90 ? 1 : p < 95 ? 2 : p < 100 ? 3 : 0;

        /* 1 + index of the first cumulative rate > p (the rates sum to 100) */
        p = agc_rnd(0x190000);
        int acc = 0, i = 0;
        while (!(p < acc)) {
            acc += s_CamouflageColRate[i];
            i++;
            if (i >= 16) break;
        }
        e->CamouflageColorType = (unsigned char)i;
    }
}

/* public entry (AsmSearchInit) */
void AutoGenColor_InitAsmColor(void)
{
    PS2_FRAME(0x10);
    __AutoGenColor_InitAsmColor();
    s_bInitAsmColor = 1;
}

/* s_CamouflageCol as the flat byte read the original does (idx * 9 + k from 0x1ee4b00): rows past the 16-row
 * table continue into s_CamouflageColRate (0x1ee4b90, directly after it), little-endian int bytes. */
static unsigned char agc_camo_byte(int idx, int k)
{
    int off = idx * 9 + k;
    if (off < (int)sizeof(s_CamouflageCol)) return (&s_CamouflageCol[0][0][0])[off];
    return ((const unsigned char *)s_CamouflageColRate)[off - (int)sizeof(s_CamouflageCol)];
}

/* permutation slot of each part's base colour per PartsColorType (the six arms; part 5 and 7..12 never use it) */
static const unsigned char agc_slot[6][7] = {
    {0, 0, 0, 0, 0, 0, 0},   /* arm 0 0x1df6ad0 */
    {0, 0, 1, 1, 0, 0, 0},   /* arm 1 0x1df6e88: parts 2 / 3 -> perm[1] */
    {0, 1, 0, 0, 0, 0, 0},   /* arm 2 0x1df7268: part 1 -> perm[1] */
    {0, 2, 1, 1, 2, 0, 0},   /* arm 3 0x1df7640: parts 1 / 4 -> perm[2], 2 / 3 -> perm[1] */
    {0, 0, 2, 1, 0, 0, 0},   /* arm 4 0x1df7a38: part 2 -> perm[2], 3 -> perm[1] */
    {0, 3, 2, 1, 4, 0, 0},   /* arm 5 0x1df7e20: 1 -> perm[3], 2 -> perm[2], 3 -> perm[1], 4 -> perm[4] */
};

void __AutoGenColor_CreateAsmColor(StAcPartsColor *aAcPartsColor, int colorId, int pilotId)
{
    PS2_FRAME(0x140);
    (void)colorId;
    int used[5], perm[5];
    int camo = 0;

    ps2_memset(aAcPartsColor, 0, 0xd0);
    struct struct_c064b11d e = s_aPilotGenColor[pilotId];   /* local copy: the re-rolls never touch the table */

    if (e.bAuto == 1) {
        int x = (ps2_rand() & 0xffff) >> 14;
        if (x == 3) e.CamouflageType = (unsigned char)((ps2_rand() & 0xffff) >> 14);
        else if (x == 2) e.WeaponColorType = (unsigned char)agc_rnd(0xc000);
        else if (x == 1) e.CategoryColorType = (unsigned char)agc_rnd(0x14000);
        else if (x == 0) e.PartsColorType = (unsigned char)agc_rnd(0x18000);
    }

    if (e.WeaponColorType == 2) {
        if (agc_rnd(0x190000) < 50) e.WeaponColorId = (unsigned char)agc_rnd(0x18000);
    } else if (e.WeaponColorType == 1) {
        if (agc_rnd(0x190000) < 75) e.WeaponColorId = (unsigned char)agc_rnd(0x18000);
    }

    /* random permutation of the five base colours: j-th still unused index */
    ps2_memset(used, 0, 0x14);
    int rem = 5, r = 0x14000;
    for (int i = 0; i < 5; i++) {
        int j = 0;
        if (rem > 0) j = ps2_mod32(ps2_rand(), r) >> 14;
        int n = 0;
        for (int k = 0; k < 5; k++) {
            if (used[k] != 0) continue;
            if (n == j) {
                perm[i] = k;
                used[k] = 1;
                r -= 0x4000;
                rem--;
                break;
            }
            n++;
        }
    }

    switch (e.CamouflageType) {
    case 3: if (agc_rnd(0x190000) < 50) camo = 1; break;
    case 2: if (agc_rnd(0x190000) < 25) camo = 1; break;
    case 1: camo = 1; break;
    default: break;
    }

    unsigned int arm = e.PartsColorType;
    if (arm >= 6) return;
    for (int part = 0; part < 13; part++) {
        unsigned char *o = (unsigned char *)&aAcPartsColor[part];
        if (camo) o[0xf] = 1;
        if (part == 5) continue;
        if (part >= 7) {   /* weapon parts: colour id only */
            o[0] = e.WeaponColorId;
            continue;
        }
        const unsigned char *b = &e.BaseColor[perm[agc_slot[arm][part]]].Color[0][0];
        const unsigned char *A = b, *Bc = b + 3, *C = b + 6;
        const unsigned char *c1 = 0, *c2 = 0;
        switch (e.CategoryColorType) {
        case 0: c1 = A; c2 = A; break;
        case 1: c1 = A; c2 = C; break;
        case 2: c1 = Bc; c2 = Bc; break;
        case 3: c1 = Bc; c2 = A; break;
        case 4: c1 = Bc; c2 = C; break;
        default: break;
        }
        if (c1) {
            for (int k = 0; k < 3; k++) o[k] = A[k];
            for (int k = 0; k < 3; k++) o[3 + k] = c1[k];
            for (int k = 0; k < 3; k++) o[6 + k] = c2[k];
        }
        o[9] = e.DetailColor[0];
        o[10] = e.DetailColor[1];
        o[11] = e.DetailColor[2];
        const unsigned char *jc = s_aJointColor[e.JointColorId];
        o[12] = jc[0];
        o[13] = jc[1];
        o[14] = jc[2];
        if (camo)
            for (int k = 0; k < 9; k++) o[k] = agc_camo_byte(e.CamouflageColorType, k);
    }
}

/* public entry: a `j` tail call with the arguments untouched (pilot id in a2) */
void AutoGenColor_CreateAsmColor(StAcPartsColor *aAcPartsColor, int colorId, int pilotId)
{
    __AutoGenColor_CreateAsmColor(aAcPartsColor, colorId, pilotId);
}
