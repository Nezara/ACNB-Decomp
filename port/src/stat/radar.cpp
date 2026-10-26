/* src/stat/radar.cpp - the HUD radar (whole file).
 * DF_RADER_SetRaderStruct collects the radar contacts of an AC (other ACs, stage enemies, visible spells / missiles
 * within range, honouring stealth / jamming cflags against the radar's abilities) into RADER_OBJ, rotated into the
 * AC's heading. DF_RADAR_Proc draws the radar frame (or ECM noise), the contacts (colour by height, friend / missile)
 * and the mission area alert / over boundaries clipped to the radar circle. */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"
#include "acnb/nodwarf.h"

#define A16 __attribute__((aligned(16)))

static const float PI_F = ps2_u2f(0x40490fdb), TWO_PI_F = ps2_u2f(0x40c90fdb);

/* yaw matrix for the radar: rotation by -(pi - yaw) wrapped to [-pi, pi) */
static void radar_rot(float (*m)[4], float angy)
{
    float a = PI_F - angy;
    float n = (float)ps2_ftoi(ps2_div(a, TWO_PI_F));
    sceVu0RotMatrixY(m, IdentityMtx, (a - TWO_PI_F * n) - PI_F);
}

/* clips the z-parallel segment v0-v1 (same x) to the circle (myx, myz, R); 1 when it misses */
static int __RADER_CalcLineOfTwoZ(float myx, float myz, float R, float (*v0)[4], float (*v1)[4])
{
    float *a = *v0, *b = *v1;
    if (!(myx - R <= a[0]) || myx + R < a[0]) return 1;
    if (!(a[2] <= b[2])) {
        float t[4];
        memcpy(t, a, 16);
        memcpy(a, b, 16);
        memcpy(b, t, 16);
    }
    float rr = R * R;
    float d = myx - a[0];
    float s = ps2_sqrt(rr - d * d);
    if (a[2] <= myz - s) a[2] = myz - s;
    if (!(a[2] < myz + s)) a[2] = myz + s;
    d = myx - b[0];
    s = ps2_sqrt(rr - d * d);
    if (b[2] <= myz - s) b[2] = myz - s;
    if (!(b[2] < myz + s)) b[2] = myz + s;
    return 0;
}

/* the same for an x-parallel segment (same z) */
static int __RADER_CalcLineOfTwoX(float myx, float myz, float R, float (*v0)[4], float (*v1)[4])
{
    float *a = *v0, *b = *v1;
    if (!(myz - R <= a[2]) || myz + R < a[2]) return 1;
    if (!(a[0] <= b[0])) {
        float t[4];
        memcpy(t, a, 16);
        memcpy(a, b, 16);
        memcpy(b, t, 16);
    }
    float rr = R * R;
    float d = myz - a[2];
    float s = ps2_sqrt(rr - d * d);
    if (a[0] <= myx - s) a[0] = myx - s;
    if (!(a[0] < myx + s)) a[0] = myx + s;
    d = myz - b[2];
    s = ps2_sqrt(rr - d * d);
    if (!(b[0] < myx + s)) b[0] = myx + s;
    if (b[0] <= myx - s) b[0] = myx - s;
    return 0;
}

/* one boundary segment in radar space (36 x 44 px for max_dist) */
static void __RADER_DrawMissionArea_Line(float *loc, float angy, int *rgb, float *v0, float *v1, float max_dist)
{
    PS2_FRAME(0xc0);
    float m[4][4] A16;
    int xyz[2][4] A16;
    radar_rot(m, angy);
    for (int k = 0; k < 3; k++) v0[k] = v0[k] - loc[k];
    for (int k = 0; k < 3; k++) v1[k] = v1[k] - loc[k];
    vu_apply(v0, (const float (*)[4])m, v0);
    vu_apply(v1, (const float (*)[4])m, v1);
    const float *v[2] = {v0, v1};
    for (int i = 0; i < 2; i++) {
        xyz[i][0] = (RadarOffsetX + 0x702 + ps2_ftoi(36.0f * ps2_div(-v[i][0], max_dist) - 0.1f)) * 16;
        xyz[i][1] = (RadarOffsetY - 1 + ps2_ftoi(0.1f + 44.0f * ps2_div(v[i][2], max_dist))) * 8 + 0x7900;
        xyz[i][2] = 0;
        xyz[i][3] = 0;
    }
    DF_ACFONT_DrawLine(rgb, xyz);
}

/* the 4 edges of the rectangle v0 / v1 (x-z corners) clipped to the radar circle of radius R around loc */
static void __RADER_DrawMissionArea(float *loc, float angy, float R, int *rgb, float *v0, float *v1)
{
    PS2_FRAME(0x80);
    float p[4] A16, q[4] A16;
    memcpy(p, v0, 16);
    memcpy(q, v0, 16);
    q[0] = v1[0];
    if (!__RADER_CalcLineOfTwoX(loc[0], loc[2], R, &p, &q)) __RADER_DrawMissionArea_Line(loc, angy, rgb, p, q, R);
    memcpy(p, v1, 16);
    memcpy(q, v1, 16);
    p[0] = v0[0];
    if (!__RADER_CalcLineOfTwoX(loc[0], loc[2], R, &p, &q)) __RADER_DrawMissionArea_Line(loc, angy, rgb, p, q, R);
    memcpy(p, v0, 16);
    memcpy(q, v0, 16);
    q[2] = v1[2];
    if (!__RADER_CalcLineOfTwoZ(loc[0], loc[2], R, &p, &q)) __RADER_DrawMissionArea_Line(loc, angy, rgb, p, q, R);
    memcpy(p, v1, 16);
    memcpy(q, v1, 16);
    p[2] = v0[2];
    if (!__RADER_CalcLineOfTwoZ(loc[0], loc[2], R, &p, &q)) __RADER_DrawMissionArea_Line(loc, angy, rgb, p, q, R);
}

/* contact marker (8 x 8 sprite; v = 56 type 3, 48 type 2, 41 otherwise) */
static void __RADER_PutObject(float *rgb, int *prt_xyz, int type)
{
    PS2_FRAME(0x50);
    int col[4] A16;
    float v = type == 3 ? 56.0f : type == 2 ? 48.0f : 41.0f;
    int y = RadarOffsetY + prt_xyz[1] + 1, x = RadarOffsetX + prt_xyz[0] + 1;
    for (int k = 0; k < 4; k++) col[k] = ps2_ftoi(rgb[k]);
    ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 2), 2); GS_NewList();
    GS_SetALPHA_1(0, 1, 0, 1, 0);
    struct_f386fb29 tex0;
    memcpy(&tex0, &STA2_TEXTURE[7], 8);
    GS_SetTEX0_1(tex0);
    GS_SetRGBA(col);
    DF_ACSTAT_DrawSprite(x, y, 120.0f, v, 8, 8);
    GS_FlushList(); ps2_rq_tag_end();
}

/* contact colour: friend (2, 3) / missile (1) fixed, others blend RADARcolor[2] (level) towards [0] (below) or
 * [1] (above) with the height difference up to the radar's height range */
static void __RADER_CalcObjectColor2(struct_5d0a4059 *rad, float *rgb, float dy, int type)
{
    if (type == 3 || type == 2) {
        memcpy(rgb, RADARcolor[4], 16);
        return;
    }
    if (type == 1) {
        memcpy(rgb, RADARcolor[3], 16);
        return;
    }
    int above;
    if (dy <= 0.0f) {
        above = 0;
        dy = -dy;
    } else {
        above = 1;
    }
    float h = rad->height;
    if (!(dy <= h)) dy = h;
    float t = ps2_div(dy, h);
    float lev[4], c[4];
    for (int k = 0; k < 3; k++) lev[k] = RADARcolor[2][k] * (1.0f - t);
    lev[3] = RADARcolor[2][3];
    for (int k = 0; k < 3; k++) c[k] = RADARcolor[above][k] * t;
    c[3] = RADARcolor[above][3];
    for (int k = 0; k < 3; k++) c[k] = c[k] + lev[k];
    for (int k = 0; k < 4; k++) rgb[k] = (float)ps2_ftoi(c[k]);
}

static void __RADER_CalcPutObject(struct_2aa89a30 *rp, int num, struct_5d0a4059 *rad)
{
    PS2_FRAME(0x80);
    float rgb[4] A16;
    int xyz[4] A16;
    if (rad->range <= 0.0f) return;
    DF_SetAlphaRegister(0, 1, 0, 1, 0, 1);
    xyz[2] = 0;
    xyz[3] = 0;
    for (int i = 0; i < num; i++, rp++) {
        xyz[0] = ps2_ftoi(ps2_div(-36.0f * rp->scr[0], rad->range)) - 4;
        xyz[1] = ps2_ftoi(ps2_div(44.0f * rp->scr[2], rad->range)) - 4;
        __RADER_CalcObjectColor2(rad, rgb, rp->scr[1], rp->type);
        __RADER_PutObject(rgb, xyz, rp->type);
    }
}

/* contact positions relative to the AC, rotated into its heading */
static void __RADER_RotLocation(int player)
{
    PS2_FRAME(0x90);
    float m[4][4] A16, d[4] A16;
    AcPlayerSt *acp = &g_ac[player];
    radar_rot(m, acp->ang[1]);
    struct_2aa89a30 *o = RADER_OBJ[player];
    for (int i = 0; i < NumOfRaderObject[player]; i++, o++) {
        for (int k = 0; k < 3; k++) d[k] = o->loc[k] - acp->loc[k];
        d[3] = o->loc[3];
        vu_apply(o->scr, (const float (*)[4])m, d);
    }
}

void DF_RADER_SetRaderStruct(float max_dist, int player)
{
    PS2_FRAME(0xe0);
    AcPlayerSt *me = &g_ac[player];
    unsigned char rstat = me->param.radar_stat;
    int see_stealth = rstat & 8, see_jam = rstat & 4, see_missile = rstat & 1;
    int cnum = StgInfo.cnum;
    unsigned short mine = me->mp->cflag;
    int *count = &NumOfRaderObject[player];
    struct_2aa89a30 *o = RADER_OBJ[player];
    *count = 0;
    auto hidden = [&](unsigned short cf) {
        return ((cf & 0x400) && !see_stealth) || ((cf & 0x200) && !see_jam);
    };
    auto add = [&](float *loc, float dist, int type) {
        o->type = type;
        o->dist = dist;
        memcpy(o->loc, loc, 16);
        o++;
        (*count)++;
    };
    for (int i = 0; i < NowPlayerNum; i++) {
        AcPlayerSt *a = &g_ac[i];
        if (i == player || a->dead || a->areaovered) continue;
        unsigned short cf = a->mp->cflag;
        if (hidden(cf) || (a->mp->use & 1) || (cf & 0x4000)) continue;
        float d = DF_CALC_GetDist_xz(a->loc, me->loc);
        if (!(d <= max_dist)) continue;
        add(a->loc, d, (a->mp->cflag & mine & 0xf) ? 2 : 0);
    }
    EnemySt *e = Enemy;
    for (int i = 0; i < cnum; i++, e++) {
        StModel *m = e->mp;
        if (m->use & 1) continue;
        unsigned short cf = m->cflag;
        if ((cf & 0x4000) || hidden(cf)) continue;
        float d = DF_CALC_GetDist_xz(m->loc, me->loc);
        if (!(d <= max_dist)) continue;
        if (*count >= 0x7f) break;
        add(m->loc, d, (m->cflag & mine & 0xf) ? 2 : 0);
    }
    for (int i = 0; i < 0x200; i++) {
        _spell2 *s = &sap0[i];
        if (s->on <= 0) continue;
        if (s->stat.is_missile) {
            if (!see_missile) continue;
            if (s->spell->stealth_rate && !see_stealth) continue;
        } else {
            if (!s->flag.dummy) continue;
            if (s->flag.kind == 2 && s->flag.id == player) continue;
        }
        float d = DF_CALC_GetDist_xz(s->matrix[3], me->loc);
        if (!(d <= max_dist)) continue;
        if (*count >= 0x7f) break;
        add(s->matrix[3], d, s->stat.is_missile ? 1 : 0);
    }
    __RADER_RotLocation(player);
}

/* contacts, then (without ECM noise) the area alert (yellow) and over (red) boundaries */
static void __RADER_PutRaderInfo(int playerno, float noiseratio)
{
    PS2_FRAME(0x90);
    int col[4] A16;
    struct_5d0a4059 *rad = &Sta_Rader[playerno];
    AcPlayerSt *acp = &g_ac[playerno];
    __RADER_CalcPutObject(RADER_OBJ[playerno], NumOfRaderObject[playerno], rad);
    if (!(noiseratio < 0.5f)) return;
    int alpha = ps2_ftoi(96.0f * (1.0f - noiseratio));
    col[0] = 0xff;
    col[1] = 0xff;
    col[2] = 0x80;
    col[3] = alpha;
    __RADER_DrawMissionArea(acp->rangeCenter, acp->rangeAng[1], rad->range, col, CopyOfSysInfo.rgnalert[0],
                            CopyOfSysInfo.rgnalert[1]);
    col[0] = 0xff;
    col[1] = 0x80;
    col[2] = 0x80;
    col[3] = alpha;
    __RADER_DrawMissionArea(acp->rangeCenter, acp->rangeAng[1], rad->range, col, CopyOfSysInfo.rgnover[0],
                            CopyOfSysInfo.rgnover[1]);
}

/* radar face: {u, v, ...} per split / radar type (type 3 = round, drawn as 4 mirrored quarters) */
static const int s_radar_uv[2][4][4] = {{{0, 0, 35, 87}, {36, 0, 71, 87}, {72, 0, 107, 87}, {108, 0, 143, 43}},
                                        {{0, 0, 35, 87}, {36, 0, 71, 87}, {72, 0, 107, 87}, {108, 0, 143, 43}}};

static void set_tex0(int i)
{
    struct_f386fb29 t;
    memcpy(&t, &STA2_TEXTURE[i], 8);
    GS_SetTEX0_1(t);
}

static void __RADAR_Draw(int radar_type, int playerno, int ulx, int uly, int split, int *col, float noiseratio)
{
    PS2_FRAME(0x110);
    int c[4] A16;
    GS_SetTEST_1(1, 1, 0, 0, 0, 0, 0, 1);
    set_tex0(split ? 10 : 2);
    if (col) memcpy(c, col, 16);
    else DF_ACST2_GetMyCol(c, playerno);
    const int *uv = s_radar_uv[split][radar_type];
    c[3] = ps2_ftoi((float)c[3] * (1.0f - noiseratio));
    GS_SetRGBA(c);
    GS_SetALPHA_1(0, 2, 0, 1, 0);
    if (radar_type == 3) {
        GS_DrawSpriteEx(ulx, uly, uv[0], uv[1], 0x24, 0x2c, 0);
        GS_DrawSpriteEx(ulx + 0x24, uly, uv[0], uv[1], 0x24, 0x2c, 1);
        GS_DrawSpriteEx(ulx + 0x24, uly + 0x2c, uv[0], uv[1], 0x24, 0x2c, 3);
        GS_DrawSpriteEx(ulx, uly + 0x2c, uv[0], uv[1], 0x24, 0x2c, 2);
    } else {
        GS_DrawSpriteEx(ulx, uly, uv[0], uv[1], 0x24, 0x58, 0);
        GS_DrawSpriteEx(ulx + 0x24, uly, uv[0], uv[1], 0x24, 0x58, 1);
    }
}

void DF_RADAR_Proc(int radar_type, StGaugeInfo *gip, int playerno, int split, int ulx, int uly, int *col)
{
    PS2_FRAME(0xa0);
    int c[4] A16;
    RadarOffsetX = ulx + 0x21;
    RadarOffsetY = uly + 0x2b;
    float noise = gip->ecmerror_rate;
    ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 2), 2); GS_NewList();
    if (split) {
        set_tex0(10);
        c[0] = c[1] = c[2] = 0x80;
        c[3] = noise < 0.5f ? 0x30 : 0x18;
        GS_SetRGBA(c);
        GS_SetALPHA_1(2, 0, 0, 1, 0);
        GS_DrawSpriteEx(ulx, uly, 0x6c, 0x2c, 0x24, 0x2c, 0);
        GS_DrawSpriteEx(ulx + 0x24, uly, 0x6c, 0x2c, 0x24, 0x2c, 1);
        GS_DrawSpriteEx(ulx, uly + 0x2c, 0x6c, 0x2c, 0x24, 0x2c, 2);
        GS_DrawSpriteEx(ulx + 0x24, uly + 0x2c, 0x6c, 0x2c, 0x24, 0x2c, 3);
    }
    if (noise < 0.5f) {
        __RADAR_Draw(radar_type, playerno, ulx, uly, split, col, noise);
    } else {
        /* jammed: noise texture (Radar_MakeNoiseTex) over the radar */
        c[0] = c[1] = c[2] = c[3] = 0x80;
        c[3] = ps2_ftoi((float)c[3] * noise);
        GS_SetRegister(0x42, 0x48);
        GS_SetRegister(6, 0x5dc00bb40ull);
        GS_SetRGBA(c);
        GS_DrawSpriteEx(ulx, uly, 0, 0, 0x48, 0x58, 0);
        GS_SetRegister(0x42, 0x44);
        ps2_dma_wait_gif();
    }
    GS_FlushList(); ps2_rq_tag_end();
    Sta_Rader[playerno] = ACSTAT_RADER;
    Sta_Rader[playerno].range = g_ac[playerno].radar_range;
    __RADER_PutRaderInfo(playerno, noise);
}

#ifdef ACNB_SOURCEPORT
int radar_noise_seed = 0x1e2bf0;   /* main 0x27b234 (no DWARF name; nodwarf.h in the hybrid) */
#endif

/* 2 x 2 flipped tiling of the noise source quad (screen 0x7c00.. 12.4, uv = u0, v0, u0 + du, v0 + dv) */
static void noise_quad(int (&xyz)[2][4], int (&uv)[4], int *rgba, int x, int y, int u, int v, int du, int dv)
{
    xyz[0][0] = x;
    xyz[0][1] = y;
    xyz[0][2] = 0xffffff;
    xyz[0][3] = 0;
    xyz[1][0] = x + 0x240;
    xyz[1][1] = y + 0x2c0;
    xyz[1][2] = xyz[0][2];
    xyz[1][3] = xyz[0][3];
    uv[0] = u;
    uv[1] = v;
    uv[2] = u + du;
    uv[3] = v + dv;
    GS_DrawRectUvN(xyz, uv, rgba, 1, 1);
}

/* renders the ECM radar static into GS memory (FRAME_1 FBP 0x1da, 128 x 128): the noise texture tiled 2 x 2 with
 * flips, then a 2-strip rectangle at a random texel offset, then restores the display registers. The offset comes from
 * a private LCG s = (85 s + 0x187) mod 2^24 advanced twice per call (u, v = s & 0x7f; the low 7 bits cycle with a
 * short period - original weakness). split only picks the TEX0 table entry. */
void Radar_MakeNoiseTex(int split)
{
    PS2_FRAME(0x100);
    PS2_STACK(int[4], rgba, 0xc0);
    PS2_STACK(int[4], uv, 0xd0);
    PS2_STACK(int[2][4], xyz, 0xe0);
    PS2_STACK(int, uRand, 0xb0);
    PS2_STACK(int, vRand, 0xa0);
    rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0x80;   /* static at 0x265a00 */
    radar_noise_seed = (int)(((unsigned int)radar_noise_seed * 85u + 0x187u) & 0xffffffu);
    uRand = radar_noise_seed & 0x7f;
    radar_noise_seed = (int)(((unsigned int)radar_noise_seed * 85u + 0x187u) & 0xffffffu);
    vRand = radar_noise_seed & 0x7f;
    GS_NewList();
    GS_SetRegister(0x40, 0x7f0000ull | 0x7f0000ull << 32);   /* SCISSOR_1 0..127 */
    GS_SetRegister(0x4c, 0x201da);                         /* FRAME_1 */
    GS_SetRegister(0x18, 0x7c00ull | 0x7c00ull << 32);     /* XYOFFSET_1 */
    GS_SetTEXFLUSH();
    struct_f386fb29 t;
    memcpy(&t, &STA2_TEXTURE[split ? 10 : 2], 8);
    GS_SetTEX0_1(t);
    GS_SetRegister(0x42, 0x44);                            /* ALPHA_1 */
    GS_SetRegister(0x14, 0);                               /* TEX1_1 */
    GS_SetTEXFLUSH();
    noise_quad(xyz, uv, rgba, 0x7c00, 0x7c00, 0x6c0, 0x2c0, 0x250, 0x2d0);
    noise_quad(xyz, uv, rgba, 0x7e40, 0x7c00, 0x8f0, 0x2c0, -0x230, 0x2d0);
    noise_quad(xyz, uv, rgba, 0x7c00, 0x7ec0, 0x6c0, 0x570, 0x250, -0x2b0);
    noise_quad(xyz, uv, rgba, 0x7e40, 0x7ec0, 0x8f0, 0x570, -0x230, -0x2b0);
    GS_SetTEXFLUSH();
    GS_SetRegister(8, 0);                                  /* CLAMP_1 */
    GS_SetRegister(0x47, 0x31003);                         /* TEST_1 */
    GS_SetRegister(6, 0x8007de65dd40afe0ull);              /* TEX0_1 */
    GS_SetRegister(0x14, 0);
    GS_SetRegister(0x42, 0x42);
    xyz[0][0] = 0x7c00;
    xyz[0][1] = 0x7c00;
    xyz[0][2] = 0xffffff;
    xyz[0][3] = 0;
    xyz[1][0] = xyz[0][0] + 0x800;
    xyz[1][1] = xyz[0][1] + 0x800;
    xyz[1][2] = xyz[0][2];
    xyz[1][3] = xyz[0][3];
    uv[0] = uRand << 4;
    uv[1] = vRand << 4;
    uv[2] = uv[0] + 0x810;
    uv[3] = uv[1] + 0x810;
    GS_DrawRectStripUvN(xyz, uv, rgba, 1, 1);
    GS_SetRegister(0x40, 0x1ff0000ull | 0xdf0000ull << 32);
    GS_SetRegister(0x4c, 0x80038);
    GS_SetRegister(0x18, 0x7900ull << 32 | 0x7000);
    GS_SetRegister(0x18, (unsigned long long)((0x7900 + (g_odevCur ? 8 : 0)) & 0xffff) << 32 | 0x7000);
    GS_FlushList();
    ps2_dma_wait_gif();
}
