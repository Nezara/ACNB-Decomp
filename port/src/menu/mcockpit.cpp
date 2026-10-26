/* src/menu/mcockpit.cpp - the cockpit (HUD layout / colour) settings screen of the Versus garage (OLMENU), built as
 * stMobj state machines (play_func per frame, pkt_func for model packets; run by DF_MOBJ_Play / DF_MOBJ_MakeVu1Paket).
 * Pieces: System (the 14 HUD panel sprites, entered from mgarage.c via DF_MCOCKPIT_NewSystem), SubSys (an option
 * list child), ColSys (the HUD colour picker: 7 colour windows, a light cursor and the ColorCube strip) and the
 * ColorCube object (6 cubes + 6 shadows sliding along a 7-slot strip; a near-copy of mcolor's SmplColCube, but the
 * colours come straight from the object's own aColor table, wrapped by ColorNum).
 * Native so far: g1 the ColorCube object (PlayMove, Play, Pkt, CreateObj, SetColor, New), __RefreshCockpitMarker
 * and the whole ColSys; g2 the SubSys and the System (incl. DF_MCOCKPIT_NewSystem); g3 the garage labels (the
 * "ita_cockpit" / mark / string items and the blinking row cursor that slide in and out with the page).
 * The System loads the cockpit texture BND through the interpreted async File_* chain (one poll per frame).
 * Locals handed to the interpreted DF_MGEN_NewModelEasy and the newlib sprintf buffers live at their original PS2
 * stack slots; the format / layout-name strings are nodwarf PS2 addresses. */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU 0x1edc050 / 0x1edc060 / 0x1edc070 (nodwarf.h) */
char mcockpit_fmt_light_cursor[16] = "light_cursor_%d";
char mcockpit_parts02_0[10] = "parts02_0";
char mcockpit_light_cursor_0[15] = "light_cursor_0";
/* OLMENU 0x1edc080 / 0x1edc090 / 0x1edc0a0 / 0x1edc0b0 (nodwarf.h) */
char mcockpit_fmt_parts10[11] = "parts10_%d";
char mcockpit_fmt_parts13[11] = "parts13_%d";
char mcockpit_parts10_2[10] = "parts10_2";
char mcockpit_panel_ita_0[12] = "panel_ita_0";

/* OLMENU 0x1ee4f40: the 14 HUD panels (layout name, colour follows the picker, visibility flags [f_expanel index or
 * -1] and their inversions) */
struct struct_8a7f9c3e s_PanelInfo[14] = {
    {"default_0", 1, {-1, -1}, {0, 0}},     {"speed_0", 1, {6, -1}, {0, 0}},
    {"EP_0", 1, {6, -1}, {1, 0}},           {"EP_redzone_0", 0, {-1, -1}, {0, 0}},
    {"enemy_0", 1, {0, -1}, {0, 0}},        {"enemy_wep_0", 0, {0, -1}, {0, 0}},
    {"enemy_redzone_0", 0, {0, -1}, {0, 0}}, {"altimeter_0", 1, {7, -1}, {0, 0}},
    {"outerheat_0", 1, {5, -1}, {0, 0}},    {"connect_0", 1, {6, 0}, {0, 0}},
    {"wep1_0", 0, {-1, -1}, {0, 0}},        {"wep2_0", 0, {-1, -1}, {0, 0}},
    {"wep3_0", 1, {-1, -1}, {0, 0}},        {"RoE_0", 1, {4, -1}, {0, 0}},
};

/* OLMENU 0x1ee5240 / 0x1ee52b0 / 0x1ee52d0: the 7 cube slots (position, scale, alpha); same values as mcolor's */
float s_ColorCubePos[7][4] = {
    {-13.0f, -1.0f, -2.25f, 0.0f}, {-11.6f, -1.0f, -2.25f, 0.0f}, {-10.2f, -1.0f, -2.25f, 0.0f},
    {-8.8f, -1.0f, -2.25f, 0.0f},  {-7.4f, -1.0f, -2.25f, 0.0f},  {-6.0f, -1.0f, -2.25f, 0.0f},
    {-4.6f, -1.0f, -2.25f, 0.0f},
};
float s_ColorCubeScl[7] = {1.0f, 1.0f, 1.0f, 1.25f, 1.0f, 1.0f, 1.0f};
unsigned char s_ColorCubeAlpha[7] = {0x00, 0x40, 0x60, 0x80, 0x60, 0x40, 0x00};

/* OLMENU 0x1ee5220: the garage label layout parts ("ita_cockpit0/1" OLMENU 0x1edc008 / 0x1edc018, the rest main
 * sdata 0x27b540..0x27b560) */
char *sGarageCockpitLayout[7] = {
    (char *)"ita_cockpit0", (char *)"ita_cockpit1", (char *)"mark0",  (char *)"mark1",
    (char *)"string0",      (char *)"string1",      (char *)"cursor",
};
#endif

#define MCOCKPIT_CUBE(o) ((struct struct_93e92ced *)&(o)->dt)
#define MCOCKPIT_CSYS(o) ((struct struct_56d6c007 *)&(o)->dt)
#define MCOCKPIT_SYS(o) ((struct struct_1063bab4 *)&(o)->dt)
#define MCOCKPIT_SSYS(o) ((struct struct_b5786106 *)&(o)->dt)
#define MCOCKPIT_PLAY(f) ((void (*)(void))GFN(f))

/* DF_MBGUIDE_Change / DF_MBACK_ChangeTag are void(void) in DWARF but read a0..t0 / a0 */
static inline void mcockpit_guide(int a, int b, int c, int d, int e)
{
    gcall_as<int(int, int, int, int, int)>(GFN(DF_MBGUIDE_Change), a, b, c, d, e);
}

static inline void mcockpit_change_tag(int tag)
{
    gcall_as<void(int)>(GFN(DF_MBACK_ChangeTag), tag);
}

static inline char *mcockpit_layout(int id)
{
    return (char *)LAYOUT_GetLayoutData(id);
}

/* colour index a wrapped into 0..ColorNum-1 (two sequential tests, ColorNum re-read) */
static inline int mcockpit_wrap(struct struct_93e92ced *c, int a)
{
    if (a < 0)
        a += c->ColorNum;
    if (a >= c->ColorNum)
        a -= c->ColorNum;
    return a;
}

/* the shadow follows the cube (scale + position quads), 1.0 lower */
static inline void mcockpit_kage_follow(struct _stMModel *cube, struct _stMModel *kage)
{
    memcpy(kage->scale, cube->scale, sizeof(kage->scale));
    memcpy(kage->pos, cube->pos, sizeof(kage->pos));
    kage->pos[1] = kage->pos[1] + 1.0f;
}

/* ---- ColorCube ---- */

/* one frame of the 6-frame slide: each cube moves from slot dst to slot src of the 7-slot strip */
static void __ColorCubePlayMove(struct stMobj *pMobj)
{
    PS2_FRAME(0xa0);
    struct struct_93e92ced *c = MCOCKPIT_CUBE(pMobj);

    if (pMobj->count == 0) {
        sceGsSyncV(0);
        /* the SDK routine's saved ra below this frame (conventions "Saved-ra slots"): jal at +0x34 */
        PS2_STACK(unsigned long long, ra_slot, -0x10);
        ra_slot = PS2PTR(GFN(__ColorCubePlayMove)) + 0x3c;
        (void)ra_slot;
    }
    pMobj->count++;
    float t = (float)pMobj->count / 6.0f;

    for (int i = 0; i < 6; i++) {
        int dir = c->MoveDir;
        int dst = (dir > 0) ? i + 1 : i;
        int src = dst - dir;
        if (src < 0)
            src += 7;
        else if ((unsigned int)src >= 7)
            src -= 7;
        int a = c->Index + i - 2;
        if (dir > 0)
            a -= 1;
        a = mcockpit_wrap(c, a);

        DF_MMODEL_SetVertColor(c->pCube[i], 0, &c->aColor[a]);
        c->CubeAlpha[i] = DF_MFUNC_CalcInterValue(s_ColorCubeAlpha[dst], s_ColorCubeAlpha[src], pMobj->count, 6);

        float prev_pos[4] __attribute__((aligned(16)));
        float next_pos[4] __attribute__((aligned(16)));
        memcpy(prev_pos, s_ColorCubePos[dst], sizeof(prev_pos));
        memcpy(next_pos, s_ColorCubePos[src], sizeof(next_pos));
        DF_MFUNC_CalcInterVector(c->pCube[i]->pos, prev_pos, next_pos, pMobj->count, 6);

        float sd = s_ColorCubeScl[dst];
        float s = sd + t * (s_ColorCubeScl[src] - sd);
        c->pCube[i]->scale[2] = s;
        c->pCube[i]->scale[1] = s;
        c->pCube[i]->scale[0] = s;
        mcockpit_kage_follow(c->pCube[i], c->pKage[i]);
        DF_MMODEL_Play(c->pCube[i]);
        DF_MMODEL_Play(c->pKage[i]);
    }
    if (pMobj->count >= 6) {
        pMobj->count = 0;
        pMobj->stat = 0;
    }
}

/* play_func of the cube object: 1 = fade in, 2 = fade out (6 frames each), 4 = slide */
static void __ColorCubePlay(struct stMobj *pMobj)
{
    PS2_FRAME(0x10);
    struct struct_93e92ced *c = MCOCKPIT_CUBE(pMobj);

    switch (pMobj->stat) {
    case 1:
        pMobj->count++;
        c->FadeAlpha = (float)pMobj->count / 6.0f;
        if (pMobj->count >= 6) {
            pMobj->count = 0;
            pMobj->stat = 0;
        }
        break;
    case 2:
        pMobj->count++;
        c->FadeAlpha = (float)(6 - pMobj->count) / 6.0f;
        if (pMobj->count >= 6) {
            pMobj->count = 0;
            pMobj->stat = 0;
        }
        break;
    case 4:
        __ColorCubePlayMove(pMobj);
        break;
    default:
        break;
    }
}

/* pkt_func of the cube object: shadows first, then the cubes */
static void __ColorCubePkt(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct struct_93e92ced *c = MCOCKPIT_CUBE(pMobj);
    unsigned int *pkt = DF_MPKT_MakeVIFCode(MPktCurrentp, 0, 0, 0, 0x13000000);

    for (int i = 0; i < 6; i++) {
        c->pKage[i]->mulalpha = c->FadeAlpha * (float)c->CubeAlpha[i] / 128.0f;
        DF_MMODEL_Play(c->pKage[i]);
        pkt = DF_MMODEL_MakeVu1PacketAC(pkt, c->pKage[i], NULL, 2);
    }
    for (int i = 0; i < 6; i++) {
        c->pCube[i]->mulalpha = c->FadeAlpha * (float)c->CubeAlpha[i] / 128.0f;
        DF_MMODEL_Play(c->pCube[i]);
        pkt = DF_MMODEL_MakeVu1PacketAC(pkt, c->pCube[i], NULL, 6);
    }
    MPktCurrentp = pkt;
}

/* 6 cubes (model 0x1cc, zero angle) + 6 shadows (0x1cd) */
static void __ColorCubeCreateObj(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct struct_93e92ced *c = MCOCKPIT_CUBE(pMobj);

    for (int i = 0; i < 6; i++) {
        /* interpreted callee: angle quad and shadow colour (main sdata gp-0x76ec) at their slots */
        PS2_STACK(struct struct_76b810ec, kage_col, 0x4c);
        PS2_STACK(float[4], hako_ang, 0x30);
        kage_col.r = 0x80;
        kage_col.g = 0x80;
        kage_col.b = 0x80;
        kage_col.a = 0x40;
        memset(hako_ang, 0, sizeof(hako_ang));
        c->pCube[i] = DF_MGEN_NewModelEasy(0x1cc, NULL, &hako_ang, NULL, NULL);
        c->pCube[i]->test_reg = 0;
        c->pCube[i]->alpha_reg = 0x44;
        c->pCube[i]->mulalpha = 1.0f;
        c->pKage[i] = DF_MGEN_NewModelEasy(0x1cd, NULL, NULL, NULL, &kage_col);
        c->pKage[i]->test_reg = 0;
        c->pKage[i]->alpha_reg = 0x42;
        c->pKage[i]->mulalpha = 1.0f;
    }
}

/* dir != 0 starts the slide (PlayMove); dir == 0 places the 5 visible cubes at once */
static void ColorCubeSetColor(struct stMobj *pMobj, int index, int dir)
{
    PS2_FRAME(0x60);
    struct struct_93e92ced *c = MCOCKPIT_CUBE(pMobj);

    c->Index = index;
    c->MoveDir = dir;
    if (dir != 0) {
        pMobj->count = 0;
        pMobj->stat = 4;
        return;
    }
    int i;
    for (i = 0; i < 5; i++) {
        int a = mcockpit_wrap(c, index + i - 2);
        DF_MMODEL_SetVertColor(c->pCube[i], 0, &c->aColor[a]);
        c->CubeAlpha[i] = s_ColorCubeAlpha[i + 1];
        memcpy(c->pCube[i]->pos, s_ColorCubePos[i + 1], sizeof(c->pCube[i]->pos));
        float s = s_ColorCubeScl[i + 1];
        c->pCube[i]->scale[2] = s;
        c->pCube[i]->scale[1] = s;
        c->pCube[i]->scale[0] = s;
        DF_MMODEL_Play(c->pCube[i]);
        mcockpit_kage_follow(c->pCube[i], c->pKage[i]);
    }
    for (; i < 6; i++)
        c->CubeAlpha[i] = 0;
}

/* a cube strip showing ColorNum (<= 16) colours, child of pParentMobj */
static struct stMobj *ColorCubeNew(struct stMobj *pParentMobj, int ColorNum, struct struct_76b810ec *aColor)
{
    PS2_FRAME(0x50);

    if (!pParentMobj)
        return NULL;
    if (ColorNum < 0 || ColorNum > 0x10)
        return NULL;
    if (!aColor)
        return NULL;
    struct stMobj *obj = DF_MOBJ_GetNewObject(pParentMobj);
    if (!obj)
        return NULL;
    struct struct_93e92ced *c = MCOCKPIT_CUBE(obj);
    memset(c, 0, 0x98);
    memcpy(c->aColor, aColor, ColorNum * 4);
    c->ColorNum = ColorNum;
    c->FadeAlpha = 1.0f;
    obj->type = 0;
    obj->stat = 0;
    obj->count = 0;
    obj->play_func = MCOCKPIT_PLAY(__ColorCubePlay);
    obj->pkt_func = MCOCKPIT_PLAY(__ColorCubePkt);
    __ColorCubeCreateObj(obj);
    return obj;
}

/* ---- HUD panel markers ---- */

/* show / hide the 14 HUD panels by the option flags, recolour the colour-following ones, and lift the three weapon
 * panels by 6 px when option 4 is off */
static void __RefreshCockpitMarker(struct stMobj **panel_sp, char *inFExp, struct struct_76b810ec *inCol)
{
    PS2_FRAME(0x80);

    for (int i = 0; i < 14; i++) {
        struct struct_8a7f9c3e *info = &s_PanelInfo[i];
        int f0 = 1;
        if (info->vis_type[0] != -1 && inFExp[info->vis_type[0]] == 0)
            f0 = 0;
        int f1 = 1;
        if (info->vis_type[1] != -1 && inFExp[info->vis_type[1]] == 0)
            f1 = 0;
        if (info->vis_type_rev[0])
            f0 = !f0;
        if (info->vis_type_rev[1])
            f1 = !f1;
        DF_MOBJ_SetDrawEnable(panel_sp[i], f0 ? (f1 != 0) : 0);
        if (info->col_chng)
            UnitSprite_SetMultiply(panel_sp[i], inCol);
    }
    struct struct_214acd28 offset = {0, 0};
    if (inFExp[4] == 0)
        offset.y = -6;
    UnitSprite_SetMove(panel_sp[10], &offset, 0);
    UnitSprite_SetMove(panel_sp[11], &offset, 0);
    UnitSprite_SetMove(panel_sp[12], &offset, 0);
}

/* ---- ColSys (HUD colour picker) ---- */

/* waits for the guide, 30 frames and the camera, then deletes the windows and the cube models; stat 5 = done */
static void __PlayCockpitColSysFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct struct_56d6c007 *p = MCOCKPIT_CSYS(ioObjp);

    if (DF_MBGUIDE_Sync(0) == 0)
        return;
    unsigned char old = ioObjp->count;
    ioObjp->count = old + 1;
    if (old < 0x1e)
        return;
    if (DF_MVIEW_MotionSync(MViewp) == 0)
        return;
    for (int i = 0; i < 7; i++)
        WindowEx_Delete(p->c_list_wnd[i]);
    WindowEx_Delete(p->c_light_cursor);

    struct stMobj *cube = p->col_cube;
    struct struct_93e92ced *cc = MCOCKPIT_CUBE(cube);
    for (int i = 0; i < 6; i++) {
        DF_MMODEL_DeleteModelAndMMD(cc->pCube[i]);
        cc->pCube[i] = NULL;
        DF_MMODEL_DeleteModelAndMMD(cc->pKage[i]);
        cc->pKage[i] = NULL;
    }
    cube->count = 0;
    cube->stat = 5;
    cube->play_func = NULL;
    cube->draw_func = NULL;
    cube->pkt_func = NULL;
    if (cube) {   /* original: tested only after the stores above */
        cube->f_draw = 0;
        cube->f_erase = 1;
    }
    ioObjp->stat = 5;
    ioObjp->play_func = NULL;
}

static void __SetCockpitColSysFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct struct_56d6c007 *p = MCOCKPIT_CSYS(ioObjp);

    DF_M1HELP_FadeOut();
    for (int i = 0; i < 7; i++)
        WindowEx_SetFadeOut(p->c_list_wnd[i]);
    WindowEx_SetFadeOut(p->c_light_cursor);
    p->col_cube->count = 0;
    p->col_cube->stat = 2;
    DF_MVIEW_MotionStart(MViewp, 0x28);
    mcockpit_change_tag(0x13);
    mcockpit_guide(0, 1, 2, 0, 0x63);
    DF_MBACK_PopSceneLabel();
    ioObjp->count = 0;
    ioObjp->stat = 2;
    ioObjp->play_func = MCOCKPIT_PLAY(__PlayCockpitColSysFadeOut);
}

static void __PlayCockpitColSys(struct stMobj *ioObjp)
{
    PS2_FRAME(0xb0);
    struct struct_56d6c007 *p = MCOCKPIT_CSYS(ioObjp);

    if (MenuKeyGuide_IsShown())
        return;
    if (MPad[MPadUser].start == 1) {   /* original: the previous frame's MPadUser */
        MenuKeyGuide_Show(0x26);
        return;
    }
    MPadUser = p->player;   /* original: global store every frame */
    if (p->col_cube->stat == 4)
        return;
    if (MPad[MPadUser].Rrt == 1) {
        Voice_Call(0x3eb, 0x7f);
        *p->idx = p->bsel;
        DF_MFUNC_GetCockpitColor(p->col, *p->idx);
        __RefreshCockpitMarker(p->panel_sp, p->f_expanel, p->col);
        __SetCockpitColSysFadeOut(ioObjp);
        return;
    }
    if (MPad[MPadUser].Rdw == 1) {
        Voice_Call(0x3ea, 0x7f);
        p->f_modify = 1;
        p->bsel = *p->idx;
        DF_MFUNC_GetCockpitColor(p->col, *p->idx);
        __RefreshCockpitMarker(p->panel_sp, p->f_expanel, p->col);
    }
    PS2_STACK(char[128], str, 0x30);
    ps2_sprintf_d(str, mcockpit_fmt_light_cursor, *p->idx + 1);
    WindowEx_MoveToLayout(p->c_light_cursor, mcockpit_layout(0x423), str, 0);

    struct struct_2d84b07b *pad = &MPad[MPadUser];
    if (pad->repeat.dw || pad->repeat.rt) {
        Voice_Call(0x3e9, 0x7f);
        (*p->idx)++;
        if (*p->idx >= 7)
            *p->idx = 0;
        ColorCubeSetColor(p->col_cube, *p->idx, 1);
    } else if (pad->repeat.up || pad->repeat.lt) {
        Voice_Call(0x3e9, 0x7f);
        (*p->idx)--;
        if (*p->idx < 0)
            *p->idx = 6;
        ColorCubeSetColor(p->col_cube, *p->idx, -1);
    }
}

/* after 30 frames: builds the cube strip (colours lightened by 0x20), the 7 colour windows (each the child of the
 * previous one) and the light cursor; then waits for the first window's fade-in */
static void __PlayCockpitColSysFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x120);
    struct struct_56d6c007 *p = MCOCKPIT_CSYS(ioObjp);

    if (p->step != 0) {
        if (p->c_list_wnd[0]->stat == 0) {
            ioObjp->stat = 0;
            ioObjp->play_func = MCOCKPIT_PLAY(__PlayCockpitColSys);
        }
        return;
    }
    unsigned char old = ioObjp->count;
    ioObjp->count = old + 1;
    if (old < 0x1e)
        return;

    struct struct_76b810ec color[7];
    for (int i = 0; i < 7; i++) {
        DF_MFUNC_GetCockpitColor(&color[i], i);
        unsigned int r = color[i].r;
        color[i].r = (r < 0xe0) ? (unsigned char)(r + 0x20) : 0xff;
        unsigned int g = color[i].g;
        color[i].g = (g < 0xe0) ? (unsigned char)(g + 0x20) : 0xff;
        unsigned int b = color[i].b;
        color[i].b = (b < 0xe0) ? (unsigned char)(b + 0x20) : 0xff;
    }
    p->col_cube = ColorCubeNew(ioObjp, 7, color);
    ColorCubeSetColor(p->col_cube, *p->idx, 0);
    p->col_cube->count = 0;
    p->col_cube->stat = 1;

    struct stMobj *p_pre_obj = ioObjp;
    for (int i = 0; i < 7; i++) {
        struct struct_214acd28 pos = {0, 0x55 + 0x1b * i};
        char *lo = mcockpit_layout(0x423);
        p->c_list_wnd[i] = WindowEx_NewFromLayout(p_pre_obj, (_WINDOWEX_TYPE)0, lo, mcockpit_parts02_0);
        p_pre_obj = p->c_list_wnd[i];
        WindowEx_MovePos(p_pre_obj, &pos, 0);
        WindowEx_SetString(p->c_list_wnd[i], (char *)DF_MSTR_GetName(MenuMng.language, 0, i + 0x18ac6));
        WindowEx_SetStringPos(p->c_list_wnd[i], 0x23, 5);
        WindowEx_SetFadeIn(p->c_list_wnd[i]);
    }
    PS2_STACK(char[128], str, 0x70);
    ps2_sprintf_d(str, mcockpit_fmt_light_cursor, *p->idx + 1);
    char *lo = mcockpit_layout(0x423);
    p->c_light_cursor = WindowEx_NewFromLayout(p_pre_obj, (_WINDOWEX_TYPE)0, lo, mcockpit_light_cursor_0);
    WindowEx_SetFadeIn(p->c_light_cursor);
    WindowEx_BlinkBegin(p->c_light_cursor);
    WindowEx_MoveToLayout(p->c_light_cursor, mcockpit_layout(0x423), str, 0);
    DF_M1HELP_SetString(DF_MSTR_GetInfo(MenuMng.language, 0, p->mstr_top_idx));
    DF_M1HELP_FadeIn();
    DF_MBACK_PushSceneLabel((enum_5124d09c)0x2e);
    p->step++;
}

/* the colour picker for player inPlayer as a child of the System object inObjp; edits System.panelColIdx in place */
static struct stMobj *__SetCockpitColSysFadeIn(struct stMobj *inObjp, int inPlayer)
{
    PS2_FRAME(0x40);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);

    if (!obj)
        return NULL;
    struct struct_56d6c007 *p = MCOCKPIT_CSYS(obj);
    struct struct_1063bab4 *sys = MCOCKPIT_SYS(inObjp);
    obj->type = 0;
    obj->stat = 1;
    p->idx = &sys->panelColIdx;
    p->bsel = *p->idx;
    p->player = (char)inPlayer;
    p->step = 0;
    p->f_modify = 0;
    p->f_expanel = sys->f_expanel;
    p->col = &sys->col;
    p->panel_sp = &sys->panel_sp[0];
    p->maker = sys->maker;
    p->mstr_top_idx = 0x190c8;
    mcockpit_change_tag(0x14);
    mcockpit_guide(0, 1, 2, 0, 0x63);
    DF_MVIEW_MotionStart(MViewp, 0x1e);
    obj->count = 0;
    obj->stat = 1;
    obj->play_func = MCOCKPIT_PLAY(__PlayCockpitColSysFadeIn);
    return obj;
}

/* ---- SubSys (option list) ---- */

/* SubSys row -> f_expanel index of the row's representative flag (OLMENU 0x1edc030) */
static const int mcockpit_subsys_tab[5] = {5, 6, 0, 7, 4};

/* f_expanel flag of SubSys row i (0 outside 0..4) */
static inline int mcockpit_subsys_on(const char *f_expanel, int i)
{
    if (i < 0 || i >= 5)
        return 0;
    return f_expanel[mcockpit_subsys_tab[i]] != 0;
}

/* waits for the camera and the first window's fade-out, then deletes the windows; stat 5 = done */
static void __PlayCockpitSubSysFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct struct_b5786106 *p = MCOCKPIT_SSYS(ioObjp);

    if (!DF_MVIEW_ChangeSync())
        return;
    if (p->list_wnd[0]->stat != 5)
        return;
    for (int i = 0; i < 5; i++)
        WindowEx_Delete(p->list_wnd[i]);
    WindowEx_Delete(p->light_cursor);
    ioObjp->stat = 5;
    ioObjp->play_func = NULL;
}

/* the option list: Cancel closes, Accept toggles the row's option flag(s), up / down move the cursor */
static void __PlayCockpitSubSys(struct stMobj *ioObjp)
{
    PS2_FRAME(0x90);
    struct struct_b5786106 *p = MCOCKPIT_SSYS(ioObjp);

    if (MenuKeyGuide_IsShown())
        return;
    int a = MPadUser;
    if (MPad[a].start == 1) {
        MenuKeyGuide_Show(0x25);
        return;
    }
    int prev_cury = p->cury;
    if (MPad[a].Rrt == 1) {
        Voice_Call(0x3eb, 0x7f);
        p->func = 0;
        for (int i = 0; i < 5; i++)
            WindowEx_SetFadeOut(p->list_wnd[i]);
        WindowEx_SetFadeOut(p->light_cursor);
        DF_M1HELP_FadeOut();
        DF_MBACK_PopSceneLabel();
        mcockpit_change_tag(0x13);
        ioObjp->stat = 2;
        ioObjp->play_func = MCOCKPIT_PLAY(__PlayCockpitSubSysFadeOut);
        return;
    }
    if (MPad[a].Rdw == 1) {
        Voice_Call(0x3ea, 0x7f);
        int cury = p->cury;
        char *f = p->f_expanel;
        int on = !mcockpit_subsys_on(f, cury);   /* the new value, 0 / 1 */
        switch (cury) {
        case 0:
            f[5] = (char)on;
            break;
        case 1:
            f[6] = (char)on;
            break;
        case 2:   /* one row, four panels (stores 3, 2, 1, 0) */
            f[3] = (char)on;
            f[2] = (char)on;
            f[1] = (char)on;
            f[0] = (char)on;
            break;
        case 3:
            f[7] = (char)on;
            break;
        case 4:
            f[4] = (char)on;
            break;
        default:
            break;
        }
        __RefreshCockpitMarker(p->panel_sp, p->f_expanel, p->col);
        WindowEx_SetFlag(p->list_wnd[(int)p->cury], on ? 4 : 0);
        return;
    }
    if (MPad[a].repeat.up) {
        Voice_Call(0x3e9, 0x7f);
        p->cury--;
        if (p->cury < 0)
            p->cury = 4;
    }
    if (MPad[MPadUser].repeat.dw) {   /* sequential test, MPadUser re-read */
        Voice_Call(0x3e9, 0x7f);
        p->cury++;
        if (p->cury >= 5)
            p->cury = 0;
    }
    int cur = p->cury;
    if (prev_cury == cur)
        return;
    PS2_STACK(char[32], aName, 0x70);
    ps2_sprintf_d(aName, mcockpit_fmt_parts10, cur + 2);
    WindowEx_MoveToLayout(p->light_cursor, mcockpit_layout(0x426), aName, 0);
    DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, p->mstr_top_idx + p->cury));
}

/* two idle frames, then the 5 option windows (each the child of the previous one) and the cursor; then waits for
 * the first window's fade-in */
static void __PlayCockpitSubSysFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x180);
    struct struct_b5786106 *p = MCOCKPIT_SSYS(ioObjp);

    if (p->step < 3)
        ioObjp->count++;
    switch (p->step) {
    case 0:
    case 1:
        p->step++;
        break;
    case 2: {
        struct stMobj *prev = ioObjp;
        for (int i = 0; i < 5; i++) {
            PS2_STACK(char[256], name, 0x80);
            ps2_sprintf_d(name, mcockpit_fmt_parts13, i + 2);
            p->list_wnd[i] = WindowEx_NewFromLayout(prev, (_WINDOWEX_TYPE)0, mcockpit_layout(0x41c), name);
            prev = p->list_wnd[i];
            WindowEx_SetString(p->list_wnd[i], (char *)DF_MSTR_GetName(MenuMng.language, 0, i + 0x18ab0));
            WindowEx_SetStringPos(p->list_wnd[i], 0x23, 5);
            WindowEx_SetFadeIn(p->list_wnd[i]);
            if (mcockpit_subsys_on(p->f_expanel, i))
                WindowEx_SetFlag(p->list_wnd[i], 4);
        }
        p->light_cursor = WindowEx_NewFromLayout(prev, (_WINDOWEX_TYPE)0, mcockpit_layout(0x426), mcockpit_parts10_2);
        WindowEx_BlinkBegin(p->light_cursor);
        WindowEx_SetFadeIn(p->light_cursor);
        DF_M1HELP_SetString(DF_MSTR_GetInfo(MenuMng.language, 0, p->mstr_top_idx + p->idx));
        DF_M1HELP_FadeIn();
        DF_MBACK_PushSceneLabel((enum_5124d09c)0x2f);
        p->step++;
        break;
    }
    case 3:
        if (p->list_wnd[0]->stat == 0) {
            ioObjp->play_func = MCOCKPIT_PLAY(__PlayCockpitSubSys);
            ioObjp->stat = 0;
        }
        break;
    default:
        break;
    }
}

/* the option list for player inPlayer as a child of the System object inObjp; edits System.f_expanel in place */
static struct stMobj *__SetCockpitSubSysFadeIn(struct stMobj *inObjp, int inPlayer)
{
    PS2_FRAME(0x50);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);

    if (!obj)
        return NULL;
    struct struct_b5786106 *p = MCOCKPIT_SSYS(obj);
    struct struct_1063bab4 *sys = MCOCKPIT_SYS(inObjp);
    obj->type = 0;
    obj->stat = 1;
    p->player = inPlayer;
    p->f_expanel = sys->f_expanel;
    p->col = &sys->col;
    p->maker = sys->maker;
    p->panel_sp = &sys->panel_sp[0];
    mcockpit_change_tag(0x15);
    p->mstr_top_idx = 0x18ab0;
    obj->play_func = MCOCKPIT_PLAY(__PlayCockpitSubSysFadeIn);
    obj->count = 0;
    return obj;
}

/* ---- System (HUD panel page) ---- */

/* draw_func: renders the page's tex strings once (on the frame SystemFadeIn step 0 asked for it) */
static void __DrawCockpitSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x20);
    struct struct_1063bab4 *sys = MCOCKPIT_SYS(ioObjp);

    if (!sys->drawtexstring)
        return;
    if (sys->mode == 0) {
        int strid[2] = {0x195a7, 0x195a6};   /* main sdata 0x27b578 */
        sys->drawtexstring = 0;
        MenuUtil_RenderTexStringAll(strid, 2);
    } else {
        int strid[1] = {0x195a6};   /* main sdata 0x27b580 */
        sys->drawtexstring = 0;
        MenuUtil_RenderTexStringAll(strid, 1);
    }
}

/* waits for the sub screen (SubSys / ColSys) to finish, erases it and re-enters SystemFadeIn at step 3 */
static void __PlayCockpitSystemDispatch(struct stMobj *ioObjp)
{
    struct struct_1063bab4 *sys = MCOCKPIT_SYS(ioObjp);
    struct stMobj *c = sys->csysp;

    /* original: stat is read before the NULL test */
    if ((c ? c : (struct stMobj *)PS2_NULL_MEM())->stat != 5)
        return;
    if (c) {
        c->f_draw = 0;
        sys->csysp->f_erase = 1;
        sys->csysp = NULL;
    }
    sys->step = 3;
    ioObjp->stat = 1;
    ioObjp->count = 0xf;
    ioObjp->play_func = MCOCKPIT_PLAY(__PlayCockpitSystemFadeIn);
}

/* func 0 (exit): waits for the panel window, then deletes the page, commits the options / colour to g_gamedata and
 * frees the texture; func 1 (accept): opens SubSys (idx 0) or ColSys (idx 1) and dispatches */
static void __PlayCockpitSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct struct_1063bab4 *sys = MCOCKPIT_SYS(ioObjp);

    if (!DF_MVIEW_ChangeSync())
        return;
    if (sys->func == 0 && sys->step == 0) {
        if (sys->panel_wnd->stat == 5) {
            DF_M1HELP_FadeOut();
            DF_MBACK_PopSceneLabel();
            sys->step++;
        }
        return;
    }
    if (sys->func == 0) {
        WindowEx_Delete(sys->panel_wnd);
        sys->panel_wnd = NULL;
        for (int i = 0; i < 14; i++) {
            UnitSprite_Delete(sys->panel_sp[i]);
            sys->panel_sp[i] = NULL;
        }
        g_gamedata[sys->player].cockpit = 0;
        for (unsigned int j = 0; j < 8; j++)
            g_gamedata[sys->player].cockpit |= (unsigned int)(sys->f_expanel[j] & 1) << j;
        g_gamedata[sys->player].cockpitcol = sys->panelColIdx;
        MenuTex_UnRegisterBndTexture(sys->tex_data);
        MenuWorkp = sys->defTBP;   /* the unaligned MenuWorkp saved by NewSystem */
        ioObjp->stat = 5;
        ioObjp->play_func = NULL;
        return;
    }
    /* the original also passes a dead third argument (f_expanel / panelColIdx) */
    if (sys->mode == 0) {
        if (sys->idx == 1)
            sys->csysp = __SetCockpitColSysFadeIn(ioObjp, sys->player);
        else if (sys->idx == 0)
            sys->csysp = __SetCockpitSubSysFadeIn(ioObjp, sys->player);
    } else {
        sys->csysp = __SetCockpitColSysFadeIn(ioObjp, sys->player);
    }
    ioObjp->stat = 3;
    ioObjp->play_func = MCOCKPIT_PLAY(__PlayCockpitSystemDispatch);
}

/* the page: Cancel exits (func 0), Accept opens the selected row (func 1), up / down select the row */
static void __PlayCockpitSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    struct struct_1063bab4 *sys = MCOCKPIT_SYS(ioObjp);

    if (MenuKeyGuide_IsShown())
        return;
    int a = MPadUser;
    if (MPad[a].start == 1) {
        MenuKeyGuide_Show(0x24);
        return;
    }
    int prev_idx = sys->idx;
    if (MPad[a].Rrt == 1) {
        Voice_Call(0x3eb, 0x7f);
        sys->func = 0;
        __SetCockpitSystemFadeOut(ioObjp);
        return;
    }
    if (MPad[a].Rdw == 1) {
        Voice_Call(0x3ea, 0x7f);
        sys->func = 1;
        __SetCockpitSystemFadeOut(ioObjp);
        return;
    }
    if (MPad[a].repeat.up) {
        Voice_Call(0x3e9, 0x7f);
        sys->idx--;
        if (sys->idx < 0)
            sys->idx = sys->idx_max - 1;
    }
    if (MPad[MPadUser].repeat.dw) {   /* sequential test, MPadUser re-read */
        Voice_Call(0x3e9, 0x7f);
        sys->idx++;
        if (sys->idx_max - 1 < sys->idx)
            sys->idx = 0;
    }
    if (sys->label)
        ((struct _mobj_menu_garage_cockpit_st *)&sys->label->dt)->id = sys->idx;
    if (prev_idx == sys->idx)
        return;
    DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, sys->mstr_top_idx + sys->idx));
}

/* 0/1: loads the cockpit texture BND (file 0x1017) into the menu work area (async read, one poll per frame);
 * 2/3: after 15 frames the garage labels and the help text (3 = back from a sub screen); 4: the panel window and the
 * 14 HUD panel sprites (first time only); 5: waits for the fade-ins */
static void __PlayCockpitSystemFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x70);
    struct struct_1063bab4 *sys = MCOCKPIT_SYS(ioObjp);

    PS2_STACK(char[12], atexid, 0x60);   /* dead local, zeroed every call */
    memset(atexid, 0, sizeof(atexid));
    if (sys->step < 3)
        ioObjp->count++;
    switch (sys->step) {
    case 0:
        if (File_IsAsyncBusy())
            return;
        MenuWorkp = (unsigned char *)DF_MFUNC_Aligned(MenuWorkp, 0x40);
        sys->tex_data = MenuWorkp;
        File_Read(0x1017, -1, (char *)sys->tex_data, 0);
        sys->drawtexstring = 1;
        sys->step++;
        break;
    case 1: {
        if (File_IsAsyncBusy())
            return;
        int size = File_GetSize(0x1017);
        MenuWorkp = MenuWorkp + size;
        MenuTex_RegisterBndTexture(sys->tex_data);
        sys->step++;
        break;
    }
    case 2:
        if (ioObjp->count < 0xf)
            return;
        sys->label = __MenuGarage_CockpitLabelInit(ioObjp, sys->idx);
        /* original: overwrites the split LabelInit set (with its inverse) */
        ((struct _mobj_menu_garage_cockpit_st *)&sys->label->dt)->split = sys->mode;
        DF_M1HELP_SetString(DF_MSTR_GetInfo(MenuMng.language, 0, sys->mstr_top_idx + sys->idx));
        DF_M1HELP_FadeIn();
        sys->step += 2;
        break;
    case 3:
        if (ioObjp->count < 0xf)
            return;
        sys->label = __MenuGarage_CockpitLabelInit(ioObjp, sys->idx);
        DF_M1HELP_SetString(DF_MSTR_GetInfo(MenuMng.language, 0, sys->mstr_top_idx + sys->idx));
        DF_M1HELP_FadeIn();
        sys->step++;
        break;
    case 4:
        if (sys->panel_wnd == NULL) {
            sys->panel_wnd = WindowEx_NewFromLayout(ioObjp, (_WINDOWEX_TYPE)0,
                                                    mcockpit_layout(sys->maker == 0 ? 0x428 : 0x429),
                                                    mcockpit_panel_ita_0);
            struct stMobj *prev = sys->panel_wnd;
            WindowEx_SetFadeIn(prev);
            for (int j = 0; j < 14; j++) {
                char *lo = mcockpit_layout(sys->maker == 0 ? 0x428 : 0x429);
                sys->panel_sp[j] = UnitSprite_New(prev, (struct _LO_HEADER *)lo, s_PanelInfo[j].name);
                prev = sys->panel_sp[j];
                UnitSprite_SetFadeIn(sys->panel_sp[j]);
            }
            UnitSprite_SetSpriteFlag(sys->panel_sp[1], 1);
            UnitSprite_SetSpriteFlag(sys->panel_sp[4], 1);
            __RefreshCockpitMarker(&sys->panel_sp[0], sys->f_expanel, &sys->col);
        }
        sys->step++;
        break;
    case 5:
        if (sys->label->stat != 0)
            return;
        if (sys->panel_wnd->stat != 0)
            return;
        ioObjp->play_func = MCOCKPIT_PLAY(__PlayCockpitSystem);
        ioObjp->stat = 0;
        break;
    default:
        break;
    }
}

/* starts the page's fade-out (func 0: panels too, back to the garage guide; func 1: help only) and the labels' */
static void __SetCockpitSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct struct_1063bab4 *sys = MCOCKPIT_SYS(ioObjp);

    if (sys->func == 0) {
        for (int i = 0; i < 14; i++)
            UnitSprite_SetFadeOut(sys->panel_sp[i]);
        WindowEx_SetFadeOut(sys->panel_wnd);
        mcockpit_change_tag(0);
        mcockpit_guide(4, 1, 2, 3, 0x63);
    } else if (sys->func == 1) {
        DF_M1HELP_FadeOut();
    }
    sys->step = 0;
    /* original: label not NULL-tested */
    sys->label->play_func = MCOCKPIT_PLAY(__MenuGarage_CockpitFadeOut);
    struct _mobj_menu_garage_cockpit_st *L = (struct _mobj_menu_garage_cockpit_st *)&sys->label->dt;
    int i;
    for (i = 0; i < 4; i++)
        if (L->label[i])
            UnitSprite_SetFadeOut(L->label[i]);
    for (; i < 6; i++)
        if (L->label[i])
            DF_MOBJ_SetFadeOut(L->label[i]);
    UnitSprite_SetFadeOut(L->cursor);
    ioObjp->stat = 2;
    ioObjp->play_func = MCOCKPIT_PLAY(__PlayCockpitSystemFadeOut);
}

/* the cockpit settings page for player inPlayer (inMode != 0: colour row only) as a child of inObjp */
struct stMobj *DF_MCOCKPIT_NewSystem(struct stMobj *inObjp, int inPlayer, int inMode)
{
    PS2_FRAME(0x50);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);

    if (!obj)
        return NULL;
    struct struct_1063bab4 *sys = MCOCKPIT_SYS(obj);
    obj->type = 0;
    obj->stat = 1;
    sys->player = inPlayer;
    sys->mode = (char)inMode;
    sys->idx_max = sys->mode ? 1 : 2;
    mcockpit_change_tag(0x13);
    sys->mstr_top_idx = sys->mode ? 0x18abc : 0x18abb;
    mcockpit_guide(0, 1, 2, 0, 0x63);
    DF_MBACK_PushSceneLabel((enum_5124d09c)0x2d);
    for (int i = 0; i < 3; i++) {
        /* original: issued twice with the same arguments */
        DF_M_AC_SetFadeOut(M_ACp[sys->player][i], 5, 0.0f);
        DF_M_AC_SetFadeOut(M_ACp[sys->player][i], 5, 0.0f);
    }
    DF_MBACK_SetSpotLightFadeOut();
    DF_MVIEW_MotionStart(MViewp, 0x28);
    sys->defTBP = MenuWorkp;
    for (unsigned int j = 0; j < 8; j++)
        sys->f_expanel[j] = (char)((g_gamedata[sys->player].cockpit >> j) & 1);
    sys->f_expanel[8] = sys->f_expanel[0] | sys->f_expanel[1] | sys->f_expanel[2] | sys->f_expanel[3];
    sys->panelColIdx = g_gamedata[sys->player].cockpitcol;
    DF_MFUNC_GetCockpitColor(&sys->col, sys->panelColIdx);
    /* maker of the AC's first part (original: reads PS2 address 0x28 when the part is missing) */
    struct _stMModel *m = M_ACp[sys->player][0]->cur_part[0];
    const char *info = m ? (const char *)m->prm.part.infop : (const char *)PS2_NULL_MEM();
    sys->maker = info[0x28];
    obj->play_func = MCOCKPIT_PLAY(__PlayCockpitSystemFadeIn);
    obj->draw_func = MCOCKPIT_PLAY(__DrawCockpitSystem);
    obj->count = 0;
    return obj;
}

/* ---- g3: garage labels (_mobj_menu_garage_cockpit_st) ---- */

#define MCOCKPIT_LBL(o) ((struct _mobj_menu_garage_cockpit_st *)&(o)->dt)

/* the object ends: no more callbacks, hidden and marked for erase */
static inline void mcockpit_label_end(struct stMobj *o)
{
    o->stat = 5;
    o->play_func = 0;
    o->draw_func = 0;
    o->pkt_func = 0;
    o->f_draw = 0;
    o->f_erase = 1;
}

/* labels 0..3 (sprites) and 4..5 (strings, x = offset + strOffset) follow moveOffset[i % 2] */
static inline void mcockpit_label_move(struct _mobj_menu_garage_cockpit_st *L, struct struct_214acd28 *pos)
{
    int i;

    pos->y = 0;
    for (i = 0; i < 4; i++) {
        if (L->label[i]) {
            pos->x = L->moveOffset[i % 2];
            UnitSprite_SetMove(L->label[i], pos, 0);
        }
    }
    for (; i < 6; i++) {
        if (L->label[i]) {
            pos->x = L->moveOffset[i % 2];
            L->label[i]->dt.string.x = (short)ps2_ftoi((float)pos->x + L->strOffset);
        }
    }
}

/* carries an old row cursor through its fade-out (6 frames), then deletes it */
static void __MenuGarage_CockpitDeleteCursor(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    unsigned char old = pMobj->count;

    pMobj->count = old + 1;
    if (old < 6)
        return;
    UnitSprite_Delete(((struct _mobj_menu_garage_cockpit_delete_cursor_st *)&pMobj->dt)->cursor);
    mcockpit_label_end(pMobj);
}

/* the label sprites (split: both cockpit rows), the two strings and the hidden row cursor, all fading in */
static void __MenuGarage_CockpitCreateItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct _mobj_menu_garage_cockpit_st *L = MCOCKPIT_LBL(pMobj);
    int i, n;

    for (i = 0; i < 6; i++)
        L->label[i] = NULL;
    L->label[0] = UnitSprite_New(pMobj, (struct _LO_HEADER *)mcockpit_layout(0x458), sGarageCockpitLayout[0]);
    UnitSprite_SetFadeIn(L->label[0]);
    if (L->split) {
        L->label[1] = UnitSprite_New(pMobj, (struct _LO_HEADER *)mcockpit_layout(0x458), sGarageCockpitLayout[1]);
        UnitSprite_SetFadeIn(L->label[1]);
    }
    L->label[2] = UnitSprite_New(pMobj, (struct _LO_HEADER *)mcockpit_layout(0x458), sGarageCockpitLayout[2]);
    UnitSprite_SetFadeIn(L->label[2]);
    if (L->split) {
        L->label[3] = UnitSprite_New(pMobj, (struct _LO_HEADER *)mcockpit_layout(0x458), sGarageCockpitLayout[3]);
        UnitSprite_SetFadeIn(L->label[3]);
    }
    n = 4;
    if (L->split) {
        L->label[4] = DF_MOBJ_NewStringFromLayoutName(pMobj, 0, 0xdcb72, 0, mcockpit_layout(0x458),
                                                      sGarageCockpitLayout[4], NULL, 0, 0);
        DF_MOBJ_SetFadeIn(L->label[4]);
        n++;
    }
    /* original: without split the second string takes the first one's layout part */
    L->label[5] = DF_MOBJ_NewStringFromLayoutName(pMobj, 0, 0xdcb73, 0, mcockpit_layout(0x458),
                                                  sGarageCockpitLayout[n], NULL, 0, 0);
    DF_MOBJ_SetFadeIn(L->label[5]);
    L->cursor = UnitSpriteBlink_New(pMobj, (struct _LO_HEADER *)mcockpit_layout(0x458), sGarageCockpitLayout[6]);
    UnitSprite_SetFadeIn(L->cursor);
    DF_MOBJ_SetDrawEnable(L->cursor, 0);
}

/* deletes the sprites and the cursor, erases the strings */
static void __MenuGarage_CockpitDeleteItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct _mobj_menu_garage_cockpit_st *L = MCOCKPIT_LBL(pMobj);
    int i;

    for (i = 0; i < 4; i++)
        if (L->label[i])
            UnitSprite_Delete(L->label[i]);
    for (; i < 6; i++) {
        if (L->label[i]) {
            L->label[i]->f_draw = 0;
            L->label[i]->f_erase = 1;
            L->label[i] = NULL;
        }
    }
    /* original: not NULL-tested */
    UnitSprite_Delete(L->cursor);
}

/* slides the labels out to the left (-320), deletes them once settled (after 8 frames) */
static void __MenuGarage_CockpitFadeOut(struct stMobj *pMobj)
{
    PS2_FRAME(0x60);
    struct _mobj_menu_garage_cockpit_st *L = MCOCKPIT_LBL(pMobj);
    struct struct_214acd28 pos;

    MoveMng_GetNextMoveValue(&L->move, -0x140, 8, 1);
    L->moveOffset[1] = L->moveOffset[0];
    L->moveOffset[0] = ps2_ftoi(L->move.vMove);
    if (L->moveOffset[0] < -0x140)
        L->moveOffset[0] = -0x140;
    mcockpit_label_move(L, &pos);
    int old = L->count;
    L->count = old + 1;
    if (old < 8 || L->moveOffset[0] != L->moveOffset[1])
        return;
    __MenuGarage_CockpitDeleteItem(pMobj);
    mcockpit_label_end(pMobj);
}

/* moves the row cursor when id changed: the old one fades out in a helper object, a new one blinks at row id */
static void __MenuGarage_CockpitPlay(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct _mobj_menu_garage_cockpit_st *L = MCOCKPIT_LBL(pMobj);

    if (L->id == L->prev_id)
        return;
    struct struct_214acd28 pos = {0, 0x38};
    pos.y = pos.y * L->id;
    if (L->cursor) {
        struct stMobj *tmp = DF_MOBJ_GetNewObject(pMobj);
        if (!tmp) {
            L->prev_id = L->id;
            return;
        }
        UnitSprite_SetFadeOut(L->cursor);
        ((struct _mobj_menu_garage_cockpit_delete_cursor_st *)&tmp->dt)->cursor = L->cursor;
        tmp->play_func = MCOCKPIT_PLAY(__MenuGarage_CockpitDeleteCursor);
    }
    L->cursor = UnitSpriteBlink_New(pMobj, (struct _LO_HEADER *)mcockpit_layout(0x458), sGarageCockpitLayout[6]);
    UnitSprite_SetFadeIn(L->cursor);
    UnitSprite_SetMove(L->cursor, &pos, 0);
    L->prev_id = L->id;
}

/* slides the labels in from the left (stat 1 while moving); once settled (after 9 frames) shows the cursor at row
 * id and switches to __MenuGarage_CockpitPlay */
static void __MenuGarage_CockpitFadeIn(struct stMobj *pMobj)
{
    PS2_FRAME(0x70);
    struct _mobj_menu_garage_cockpit_st *L = MCOCKPIT_LBL(pMobj);
    struct struct_214acd28 pos;

    MoveMng_GetNextMoveValue(&L->move, 0x140, 8, 1);
    L->moveOffset[1] = L->moveOffset[0];
    L->moveOffset[0] = ps2_ftoi(L->move.vMove - 320.0f);
    if (L->moveOffset[0] > 0)
        L->moveOffset[0] = 0;
    mcockpit_label_move(L, &pos);
    pMobj->stat = 1;
    int old = L->count;
    L->count = old + 1;
    if (old < 9 || L->moveOffset[0] != L->moveOffset[1])
        return;
    UnitSprite_SetFadeIn(L->cursor);
    pos.x = 0;
    pos.y = L->id * 56;
    UnitSprite_SetMove(L->cursor, &pos, 0);
    DF_MOBJ_SetDrawEnable(L->cursor, 1);
    pMobj->play_func = MCOCKPIT_PLAY(__MenuGarage_CockpitPlay);
    MoveMng_Init(&L->move);
    pMobj->stat = 0;
}

/* the garage label object of the System pMobj (cursor row id), sliding in from -320 */
struct stMobj *__MenuGarage_CockpitLabelInit(struct stMobj *pMobj, int id)
{
    PS2_FRAME(0x60);
    struct stMobj *obj = DF_MOBJ_GetNewObject(pMobj);

    if (!obj)
        return NULL;
    struct _mobj_menu_garage_cockpit_st *L = MCOCKPIT_LBL(obj);
    obj->type = 0;
    obj->stat = 1;
    obj->count = 0;
    obj->play_func = MCOCKPIT_PLAY(__MenuGarage_CockpitFadeIn);
    obj->draw_func = 0;
    obj->pkt_func = 0;
    L->id = id;
    L->prev_id = id;
    /* original: the inverse of the System's mode (the System overwrites it with mode afterwards) */
    L->split = MCOCKPIT_SYS(pMobj)->mode == 0;
    __MenuGarage_CockpitCreateItem(obj);
    MoveMng_Init(&L->move);
    /* original: label[5] not NULL-tested */
    L->strOffset = (float)L->label[5]->dt.string.x;
    L->moveOffset[0] = -0x140;
    L->moveOffset[1] = -0x140;
    struct struct_214acd28 pos = {0, 0x38};
    pos.y = pos.y * L->id;
    UnitSprite_SetMove(L->cursor, &pos, 0);
    return obj;
}
