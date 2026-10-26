/* src/menu/mcolor.cpp - the AC colour / camouflage editor of the garage paint mode (OLMENU), built as stMobj state
 * machines (play_func per frame, pkt_func for the model packets; run by DF_MOBJ_Play / DF_MOBJ_MakeVu1Paket).
 * Three systems, all entered from mpaint.c: EditColSystem (part list + AC part model + 5-cube colour ring, with the
 * EditPartCol RGB bars and the SelPartMode camouflage chooser as children) and SmplColSystem (the 12-entry sample
 * colour list with a SmplColCube child object showing 5-6 cubes that slide left / right with the picked colour).
 * Native so far (g1): the SmplColCube object (PlayMove, Play, Pkt, CreateObj, SetColor) and the whole SmplColSystem
 * (New, FadeIn, Play, Move, MoveLabel, InitLabel, CheckColorModify, Dispatch, SetFadeOut, FadeOut); g2: the
 * EditColSystem part-list windows (OpenPartsWnd / ClosePartsWnd) and the whole EditPartCol RGB editor; g3: the
 * SelPartMode camouflage pattern chooser (New, FadeIn, Play, SetFadeOut, FadeOut); g4: the 5-cube colour ring
 * (CalcPosition_Ring, UpdateRotate, UpdateFade, Create) and the EditColSystem entry / fade-in / fade-out start / pkt.
 * The rest of the file (EditColSystem Play / Dialog / Dispatch / FadeOut) is still interpreted.
 * Locals handed to the interpreted DF_MGEN_NewModelEasy and the sprintf buffers live at their original PS2 stack
 * slots; the format / layout-name strings are nodwarf PS2 addresses. */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU 0x1edc108 / 0x1edc230 / 0x1edc260 / 0x1edc270 / 0x1edc280 (nodwarf.h) */
char mcolor_parts02_0[10] = "parts02_0";
char mcolor_fmt_light_cursor[16] = "light_cursor_%d";
char mcolor_light_cursor_0[15] = "light_cursor_0";
char mcolor_allow_up_0[11] = "allow_up_0";
char mcolor_allow_down_0[13] = "allow_down_0";
/* OLMENU 0x1edc118 / 0x1edc128 / 0x1edc1f0 / 0x1edc210 (nodwarf.h) */
char mcolor_parts02_1[10] = "parts02_1";
char mcolor_parts02_2[10] = "parts02_2";
char mcolor_light_cursor_mk2_0[19] = "light_cursor_mk2_0";
char mcolor_light_cursor_mk2_1[19] = "light_cursor_mk2_1";

/* OLMENU 0x1ee56c0 / 0x1ee56d0 / 0x1ee5740: the 7 cube slots (alpha, position, scale) */
unsigned char s_SmplColCubeAlpha[7] = {0x00, 0x40, 0x60, 0x80, 0x60, 0x40, 0x00};
float s_SmplColCubePos[7][4] = {
    {-13.0f, -1.0f, -2.25f, 0.0f}, {-11.6f, -1.0f, -2.25f, 0.0f}, {-10.2f, -1.0f, -2.25f, 0.0f},
    {-8.8f, -1.0f, -2.25f, 0.0f},  {-7.4f, -1.0f, -2.25f, 0.0f},  {-6.0f, -1.0f, -2.25f, 0.0f},
    {-4.6f, -1.0f, -2.25f, 0.0f},
};
float s_SmplColCubeScl[7] = {1.0f, 1.0f, 1.0f, 1.25f, 1.0f, 1.0f, 1.0f};

/* OLMENU 0x1edc250 / main sdata 0x27b638 / 0x27b5f8 / 0x27b600 / 0x27b608 (nodwarf.h) */
char mcolor_parts06a_0[11] = "parts06a_0";
char mcolor_fmt_03d[5] = "%03d";
char mcolor_str_red[4] = "RED";
char mcolor_str_green[6] = "GREEN";
char mcolor_str_blue[5] = "BLUE";
/* OLMENU 0x1edc240 (nodwarf.h) */
char mcolor_fmt_parts08[11] = "parts08_%d";

/* OLMENU 0x1edc0c0: the 13 EditPartCol unit sprites (names in main sdata 0x27b588..) */
char *s_ColEditUnitSpName[13] = {
    (char *)"RGB_0",   (char *)"gauge_0", (char *)"gauge_1", (char *)"gauge_2", (char *)"bar_0",
    (char *)"bar_1",   (char *)"bar_2",   (char *)"left_0",  (char *)"left_1",  (char *)"left_2",
    (char *)"right_0", (char *)"right_1", (char *)"right_2",
};
/* OLMENU 0x1edc170: the part-list windows (title + 6 rows) */
char *s_EditColSysObjWndName[7] = {
    (char *)"parts09_0", mcolor_parts02_0,    mcolor_parts02_1,    mcolor_parts02_2,
    (char *)"parts02_3", (char *)"parts02_4", (char *)"parts02_5",
};
/* OLMENU 0x1ee5318 / 0x1ee5320: rows per sel_mode (colour / camouflage) and their title string ids */
int s_partsTypeNum[2] = {6, 5};
int s_partsTypeStrId[2] = {0x18802, 0x194e2};
/* OLMENU 0x1ee5330 / 0x1ee54b0: AC part model position / scale [sel_mode][idx][0 list view, 1 colour edit view] */
float AC_PART_POS[2][6][2][4] = {
    {
        {{-9.6f, -0.5f, 2.75f, 0.0f}, {-9.7f, -3.5f, 5.7f, 0.0f}},
        {{-9.6f, -3.0f, 2.75f, 0.0f}, {-9.7f, -5.5f, 5.7f, 0.0f}},
        {{-9.6f, -2.0f, 2.75f, 0.0f}, {-9.7f, -5.5f, 5.7f, 0.0f}},
        {{-9.6f, -2.0f, 2.75f, 0.0f}, {-9.7f, -5.5f, 5.7f, 0.0f}},
        {{-9.6f, -2.0f, 2.75f, 0.0f}, {-9.7f, -5.5f, 5.7f, 0.0f}},
        {{-9.6f, -2.5f, 2.75f, 0.0f}, {-9.7f, -5.5f, 5.7f, 0.0f}},
    },
    {
        {{-9.6f, -0.5f, 2.75f, 0.0f}, {-9.7f, -3.5f, 5.7f, 0.0f}},
        {{-9.6f, -3.0f, 2.75f, 0.0f}, {-9.7f, -5.5f, 5.7f, 0.0f}},
        {{-9.6f, -2.0f, 2.75f, 0.0f}, {-9.7f, -5.5f, 5.7f, 0.0f}},
        {{-9.6f, -2.0f, 2.75f, 0.0f}, {-9.7f, -5.5f, 5.7f, 0.0f}},
        {{-9.6f, -2.5f, 2.75f, 0.0f}, {-9.7f, -5.5f, 5.7f, 0.0f}},
        {{0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    },
};
#define MCOLOR_SCL1 {{1.0f, 1.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 0.0f}}
float AC_PART_SCL[2][6][2][4] = {
    {MCOLOR_SCL1, MCOLOR_SCL1, MCOLOR_SCL1, MCOLOR_SCL1, MCOLOR_SCL1, MCOLOR_SCL1},
    {MCOLOR_SCL1, MCOLOR_SCL1, MCOLOR_SCL1, MCOLOR_SCL1, MCOLOR_SCL1, {{0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}}},
};
#undef MCOLOR_SCL1
/* OLMENU 0x1ee5630 / 0x1ee5640: the colour ring's centre and the 5 cube rest positions */
float COLOR_CUBE_ROT_CENTER[4] = {-11.6f, -1.8f, -0.95f, 0.0f};
float COLOR_CUBE_POS[5][4] = {
    {-11.6f, -1.0f, -2.75f, 0.0f}, {-10.2f, -1.0f, -2.75f, 0.0f}, {-8.8f, -1.0f, -2.75f, 0.0f},
    {-7.4f, -1.0f, -2.75f, 0.0f},  {-6.0f, -1.0f, -2.75f, 0.0f},
};
/* OLMENU 0x1ee5690 / 0x1ee56a0 / 0x1ee56b0: model rects (list / colour edit / pattern select) */
struct stMenuRect MCOLOR_MDL_RECT = {0, 0x32, 0x140, 0x1c0};
struct stMenuRect EDIT_COL_MDL_RECT = {0, 0, 0x140, 0x1c0};
struct stMenuRect SLCT_PAT_MDL_RECT = {0, 0, 0x140, 0x1c0};
#endif

#define MCOLOR_SCS(o) ((struct struct_bfa6e9ec *)&(o)->dt)
#define MCOLOR_CUBE(o) ((struct struct_406df828 *)&(o)->dt)
#define MCOLOR_PLAY(f) ((void (*)(void))GFN(f))

/* DF_MBGUIDE_Change / DF_MBACK_ChangeTag are void(void) in DWARF but read a0..t0 / a0 */
static inline void mcolor_guide(int a, int b, int c, int d, int e)
{
    gcall_as<int(int, int, int, int, int)>(GFN(DF_MBGUIDE_Change), a, b, c, d, e);
}

static inline void mcolor_change_tag(int tag)
{
    gcall_as<void(int)>(GFN(DF_MBACK_ChangeTag), tag);
}

static inline char *mcolor_layout(int id)
{
    return (char *)LAYOUT_GetLayoutData(id);
}

/* sample colour a (0..11): slot 7 is the AC's own custom colour */
static inline struct struct_098975f3 *mcolor_smpl_col(struct struct_406df828 *c, int a)
{
    return (a == 7) ? c->pCustomCol : DF_MFUNC_GetACSampleColor(a + 1);
}

/* cube vertex colour: swatch 1's first colour + 0x1e per byte (u8 wrap), alpha 0x80 */
static inline void mcolor_cube_col(struct struct_76b810ec *col, struct struct_098975f3 *cp)
{
    col->r = (unsigned char)(cp->p_col[1].s_col[0].r + 0x1e);
    col->g = (unsigned char)(cp->p_col[1].s_col[0].g + 0x1e);
    col->b = (unsigned char)(cp->p_col[1].s_col[0].b + 0x1e);
    col->a = 0x80;
}

/* the shadow follows the cube (scale + position quads), 1.0 lower */
static inline void mcolor_kage_follow(struct _stMModel *cube, struct _stMModel *kage)
{
    memcpy(kage->scale, cube->scale, sizeof(kage->scale));
    memcpy(kage->pos, cube->pos, sizeof(kage->pos));
    kage->pos[1] = kage->pos[1] + 1.0f;
}

/* ---- SmplColCube ---- */

/* one frame of the 6-frame slide: each cube moves from slot dst to slot src of the 7-slot strip */
static void __SmplColCube_PlayMove(struct stMobj *pMobj)
{
    PS2_FRAME(0xe0);
    struct struct_406df828 *c = MCOLOR_CUBE(pMobj);

    if (pMobj->count == 0) {
        sceGsSyncV(0);
        /* the SDK routine's saved ra below this frame (conventions "Saved-ra slots"): jal at +0x40 */
        PS2_STACK(unsigned long long, ra_slot, -0x10);
        ra_slot = PS2PTR(GFN(__SmplColCube_PlayMove)) + 0x48;
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
        if (a < 0)
            a += 12;
        else if (a >= 12)
            a -= 12;

        struct struct_76b810ec col;
        mcolor_cube_col(&col, mcolor_smpl_col(c, a));
        DF_MMODEL_SetVertColor(c->pCube[i], 0, &col);
        c->CubeAlpha[i] = DF_MFUNC_CalcInterValue(s_SmplColCubeAlpha[dst], s_SmplColCubeAlpha[src], pMobj->count, 6);

        float row_d[4] __attribute__((aligned(16)));
        float row_s[4] __attribute__((aligned(16)));
        memcpy(row_d, s_SmplColCubePos[dst], sizeof(row_d));
        memcpy(row_s, s_SmplColCubePos[src], sizeof(row_s));
        DF_MFUNC_CalcInterVector(c->pCube[i]->pos, row_d, row_s, pMobj->count, 6);

        float sd = s_SmplColCubeScl[dst];
        float s = sd + t * (s_SmplColCubeScl[src] - sd);
        c->pCube[i]->scale[2] = s;
        c->pCube[i]->scale[1] = s;
        c->pCube[i]->scale[0] = s;
        mcolor_kage_follow(c->pCube[i], c->pKage[i]);
        DF_MMODEL_Play(c->pCube[i]);
        DF_MMODEL_Play(c->pKage[i]);
    }
    if (pMobj->count >= 6) {
        pMobj->count = 0;
        pMobj->stat = 0;
    }
}

/* play_func of the cube object: 1 = fade in, 2 = fade out (6 frames each), 4 = slide */
static void __SmplColCube_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x10);
    struct struct_406df828 *c = MCOLOR_CUBE(pMobj);

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
        __SmplColCube_PlayMove(pMobj);
        break;
    default:
        break;
    }
}

/* pkt_func of the cube object: shadows first, then the cubes */
static void __SmplColCube_Pkt(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct struct_406df828 *c = MCOLOR_CUBE(pMobj);
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

/* 6 cubes (model 0x1cc) + 6 shadows (0x1cd) */
static void __SmplColCube_CreateObj(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct struct_406df828 *c = MCOLOR_CUBE(pMobj);

    for (int i = 0; i < 6; i++) {
        /* interpreted callee: angle quad (OLMENU 0x1edc1e0) and shadow colour (main sdata gp-0x763c) at their slots */
        PS2_STACK(struct struct_76b810ec, kage_col, 0x4c);
        PS2_STACK(float[4], ang, 0x30);
        kage_col.r = 0x80;
        kage_col.g = 0x80;
        kage_col.b = 0x80;
        kage_col.a = 0x40;
        ang[0] = 0.0f;
        ang[1] = 0.0f;
        ang[2] = 0.0f;
        ang[3] = 1.0f;
        c->pCube[i] = DF_MGEN_NewModelEasy(0x1cc, NULL, &ang, NULL, NULL);
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
static void SmplColCube_SetColor(struct stMobj *pMobj, int index, int dir)
{
    PS2_FRAME(0xa0);
    struct struct_406df828 *c = MCOLOR_CUBE(pMobj);

    c->Index = index;
    c->MoveDir = dir;
    if (dir != 0) {
        pMobj->count = 0;
        pMobj->stat = 4;
        return;
    }
    int i;
    for (i = 0; i < 5; i++) {
        int a = index + i - 2;
        if (a < 0)
            a += 12;
        else if (a >= 12)
            a -= 12;

        struct struct_76b810ec col;
        mcolor_cube_col(&col, mcolor_smpl_col(c, a));
        DF_MMODEL_SetVertColor(c->pCube[i], 0, &col);
        c->CubeAlpha[i] = s_SmplColCubeAlpha[i + 1];
        memcpy(c->pCube[i]->pos, s_SmplColCubePos[i + 1], sizeof(c->pCube[i]->pos));
        float s = s_SmplColCubeScl[i + 1];
        c->pCube[i]->scale[2] = s;
        c->pCube[i]->scale[1] = s;
        c->pCube[i]->scale[0] = s;
        DF_MMODEL_Play(c->pCube[i]);
        mcolor_kage_follow(c->pCube[i], c->pKage[i]);
    }
    for (; i < 6; i++)
        c->CubeAlpha[i] = 0;
}

/* ---- SmplColSystem ---- */

static void __SetSmplColSystemFadeOut(struct stMobj *ioObjp);
static void __PlaySmplColSystemFadeOut(struct stMobj *ioObjp);
static void __PlaySmplColSystemMove(struct stMobj *ioObjp);
static void __PlaySmplColSystem(struct stMobj *ioObjp);
static void __PlaySmplColSystemFadeIn(struct stMobj *ioObjp);

/* the "apply changes?" dialog: 4 = back to the list, 0x24 = revert + leave, 0x14 = keep + leave */
static void __PlaySmplColSystemDispatch(struct stMobj *ioObjp)
{
    PS2_FRAME(0x20);
    struct struct_bfa6e9ec *p = MCOLOR_SCS(ioObjp);
    int r = DF_MDIALOG_Sync();

    if (!(r & 4))
        return;
    if (r == 4) {
        ioObjp->stat = 0;
        ioObjp->play_func = MCOLOR_PLAY(__PlaySmplColSystem);
    } else if (r == 0x24) {
        DF_M_AC_UpdateClut(p->acp, p->bk_colp, 1);
        __SetSmplColSystemFadeOut(ioObjp);
    } else if (r == 0x14) {
        __SetSmplColSystemFadeOut(ioObjp);
    }
}

static void __PlaySmplColSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x60);
    struct struct_bfa6e9ec *p = MCOLOR_SCS(ioObjp);

    if (!DF_MBGUIDE_Sync(0))
        return;
    unsigned char old = ioObjp->count;
    ioObjp->count = old + 1;
    if (old < 0x1e)
        return;
    if (!DF_MVIEW_MotionSync(MViewp))
        return;

    for (int i = 0; i < 12; i++)
        WindowEx_Delete(p->p_label[i]);
    for (int i = 0; i < 2; i++) {
        if (p->p_allow_sp[i]) {
            p->p_allow_sp[i]->f_draw = 0;
            p->p_allow_sp[i]->f_erase = 1;
            p->p_allow_sp[i] = NULL;
        }
    }
    WindowEx_Delete(p->p_lt_crsr);

    struct stMobj *cube = p->p_col_cube;
    struct struct_406df828 *c = MCOLOR_CUBE(cube);
    for (int i = 0; i < 6; i++) {
        DF_MMODEL_DeleteModelAndMMD(c->pCube[i]);
        c->pCube[i] = NULL;
        DF_MMODEL_DeleteModelAndMMD(c->pKage[i]);
        c->pKage[i] = NULL;
    }
    cube->count = 0;
    cube->stat = 5;
    cube->play_func = NULL;
    cube->draw_func = NULL;
    cube->pkt_func = NULL;
    if (cube) {
        cube->f_draw = 0;
        cube->f_erase = 1;
    }
    DF_MMM_Free(2, p->bk_colp);
    DF_MBACK_SetHasRDraw(0);
    ioObjp->stat = 5;
    ioObjp->play_func = NULL;
}

/* label scroll: 7 frames, then back to the list */
static void __PlaySmplColSystemMove(struct stMobj *ioObjp)
{
    PS2_FRAME(0x20);
    struct struct_bfa6e9ec *p = MCOLOR_SCS(ioObjp);

    ioObjp->count++;
    if (ioObjp->count < 7)
        return;
    WindowEx_SetFadeIn(p->p_lt_crsr);
    ioObjp->count = 0;
    ioObjp->stat = 0;
    ioObjp->play_func = MCOLOR_PLAY(__PlaySmplColSystem);
}

/* scroll the 12 labels one row (dir < 0 up, dir > 0 down) over 6 frames */
static void __SmplColSystem_MoveLabel(struct stMobj *ioObjp, int dir)
{
    PS2_FRAME(0x90);
    struct struct_bfa6e9ec *p = MCOLOR_SCS(ioObjp);
    int row = p->slct_top - 1;

    for (int i = 0; i < 12; i++, row++) {
        struct struct_214acd28 pos = {0, 0x1f + 0x1b * i};
        if (row < 0)
            row += 12;
        else if (row >= 12)
            row -= 12;
        struct stMobj *w = p->p_label[row];
        WindowEx_MovePos(w, &pos, 6);
        if (dir < 0) {
            if (i == 1)
                WindowEx_SetFadeIn(w);
            else if (i == 0xb)
                WindowEx_SetFadeOut(w);
            DF_MOBJ_SetDrawEnable(w, i > 0 && i < 0xc);
        } else if (dir > 0) {
            if (i == 0)
                WindowEx_SetFadeOut(w);
            else if (i == 10)
                WindowEx_SetFadeIn(w);
            DF_MOBJ_SetDrawEnable(w, i < 0xb);
        }
    }
}

/* place the 12 labels (row slct_top - 1 at the top, rows 1..10 visible) */
static void __SmplColSystem_InitLabel(struct stMobj *ioObjp)
{
    PS2_FRAME(0x80);
    struct struct_bfa6e9ec *p = MCOLOR_SCS(ioObjp);
    int row = p->slct_top - 1;

    for (int i = 0; i < 12; i++, row++) {
        struct struct_214acd28 pos = {0, 0x1f + 0x1b * i};
        if (row < 0)
            row += 12;
        else if (row >= 12)
            row -= 12;
        struct stMobj *w = p->p_label[row];
        WindowEx_MovePos(w, &pos, 0);
        DF_MOBJ_SetDrawEnable(w, i > 0 && i < 0xb);
    }
}

static void __PlaySmplColSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0xf0);
    struct struct_bfa6e9ec *p = MCOLOR_SCS(ioObjp);
    int old_slct = p->slct;
    int old_top = p->slct_top;
    int dir = 0;

    if (p->p_col_cube->stat == 4)
        return;
    if (MenuKeyGuide_IsShown())
        return;
    if (MPad[MPadUser].start == 1) {
        MenuKeyGuide_Show(0x1f);
        return;
    }
    MPadUser = p->player;   /* original: global store every frame */
    if (MPad[MPadUser].Rrt == 1) {
        if (p->f_modify == 0) {
            DF_M_AC_UpdateClut(p->acp, p->bk_colp, 1);
            Voice_Call(0x3eb, 0x7f);
            __SetSmplColSystemFadeOut(ioObjp);
        } else {
            DF_MDIALOG_Open(2, DF_MSTR_GetInfo(MenuMng.language, 1, 0x30d41), NULL, 0, 0, 0, 1, 0);
            ioObjp->play_func = MCOLOR_PLAY(__PlaySmplColSystemDispatch);
            ioObjp->stat = 3;
        }
        return;
    }
    if (MPad[MPadUser].Rdw == 1) {
        Voice_Call(0x3ea, 0x7f);
        if (p->slct == 7) {
            DF_M_AC_UpdateClut(p->acp, p->bk_colp, 1);
            p->f_modify = 0;
        } else {
            struct struct_098975f3 *cp = DF_MFUNC_GetACSampleColor(p->slct + 1);
            DF_M_AC_UpdateClut(p->acp, cp, 1);
            p->f_modify = __CheckColorModify(p->bk_colp, cp);
        }
    }
    if (MPad[MPadUser].repeat.up == 1 || MPad[MPadUser].repeat.lt == 1) {
        Voice_Call(0x3e9, 0x7f);
        dir = -1;
        if (p->slct_view > 0)
            p->slct_view--;
        else
            p->slct_top = (p->slct_top + 11) % 12;
    } else if (MPad[MPadUser].repeat.dw == 1 || MPad[MPadUser].repeat.rt == 1) {
        Voice_Call(0x3e9, 0x7f);
        dir = 1;
        if (p->slct_view < 9)
            p->slct_view++;
        else
            p->slct_top = (p->slct_top + 1) % 12;
    }
    p->slct = (p->slct_top + p->slct_view) % 12;
    if (p->slct == old_slct)
        return;
    if (old_top != p->slct_top) {
        __SmplColSystem_MoveLabel(ioObjp, dir);
        WindowEx_SetFadeOut(p->p_lt_crsr);
        ioObjp->count = 0;
        ioObjp->stat = 4;
        ioObjp->play_func = MCOLOR_PLAY(__PlaySmplColSystemMove);
    }
    PS2_STACK(char[0x40], buf, 0x70);
    ps2_sprintf_d(buf, mcolor_fmt_light_cursor, p->slct_view);
    WindowEx_MoveToLayout(p->p_lt_crsr, mcolor_layout(0x423), buf, 0);
    SmplColCube_SetColor(p->p_col_cube, p->slct, dir);
}

/* 1 when the first 5 parts' 5 RGB colours differ (wepcol and parts 5..10 are not compared) */
static int __CheckColorModify(struct struct_098975f3 *inACColp1, struct struct_098975f3 *inACColp2)
{
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            struct struct_1bca1f17 *a = &inACColp1->p_col[i].s_col[j];
            struct struct_1bca1f17 *b = &inACColp2->p_col[i].s_col[j];
            if (a->r != b->r || a->g != b->g || a->b != b->b)
                return 1;
        }
    }
    return 0;
}

static void __PlaySmplColSystemFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x110);
    struct struct_bfa6e9ec *p = MCOLOR_SCS(ioObjp);

    if (p->step != 0) {
        if (p->p_label[0]->stat == 0 && p->p_col_cube->stat == 0) {
            ioObjp->stat = 0;
            ioObjp->play_func = MCOLOR_PLAY(__PlaySmplColSystem);
        }
        return;
    }
    unsigned char old = ioObjp->count;
    ioObjp->count = old + 1;
    if (old < 0x1e)
        return;

    struct stMobj *parent = ioObjp;
    for (int i = 0; i < 12; i++) {
        struct struct_214acd28 pos = {0, 0x3a + 0x1b * i};
        char *lo = mcolor_layout(0x423);
        p->p_label[i] = WindowEx_NewFromLayout(parent, (_WINDOWEX_TYPE)0, lo, mcolor_parts02_0);
        parent = p->p_label[i];
        WindowEx_MovePos(p->p_label[i], &pos, 0);
        WindowEx_SetString(p->p_label[i], (char *)DF_MSTR_GetName(MenuMng.language, 0, i + 0x19532));
        WindowEx_SetStringPos(p->p_label[i], 0x23, 5);
        WindowEx_SetFadeIn(p->p_label[i]);
    }
    char *lo = mcolor_layout(0x423);
    p->p_lt_crsr = WindowEx_NewFromLayout(parent, (_WINDOWEX_TYPE)0, lo, mcolor_light_cursor_0);
    struct stMobj *crsr = p->p_lt_crsr;
    WindowEx_SetFadeIn(p->p_lt_crsr);
    WindowEx_BlinkBegin(p->p_lt_crsr);
    lo = mcolor_layout(0x423);
    p->p_allow_sp[0] = DF_MOBJ2_NewBlinkSpriteFromLayoutName(crsr, lo, mcolor_allow_up_0, 0, 0);
    struct stMobj *up = p->p_allow_sp[0];
    lo = mcolor_layout(0x423);
    p->p_allow_sp[1] = DF_MOBJ2_NewBlinkSpriteFromLayoutName(up, lo, mcolor_allow_down_0, 0, 0);
    for (int i = 0; i < 2; i++)
        DF_MOBJ2_SetBlinkSpriteFadeIn(p->p_allow_sp[i]);

    struct struct_098975f3 *bk = p->bk_colp;
    struct stMobj *cube = DF_MOBJ_GetNewObject(ioObjp);
    if (cube) {
        struct struct_406df828 *c = MCOLOR_CUBE(cube);
        memset(c, 0, 0x58);
        c->pCustomCol = bk;
        c->FadeAlpha = 1.0f;
        cube->type = 0;
        cube->stat = 0;
        cube->count = 0;
        cube->play_func = MCOLOR_PLAY(__SmplColCube_Play);
        cube->pkt_func = MCOLOR_PLAY(__SmplColCube_Pkt);
        __SmplColCube_CreateObj(cube);
    }
    p->p_col_cube = cube;
    p->p_col_cube->count = 0;
    p->p_col_cube->stat = 1;
    __SmplColSystem_InitLabel(ioObjp);

    PS2_STACK(char[0x40], buf, 0x80);
    ps2_sprintf_d(buf, mcolor_fmt_light_cursor, p->slct_view);
    WindowEx_MoveToLayout(p->p_lt_crsr, mcolor_layout(0x423), buf, 0);
    SmplColCube_SetColor(p->p_col_cube, p->slct, 0);
    DF_M1HELP_SetString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x190c9));
    DF_M1HELP_FadeIn();
    p->step++;
}

static void __SetSmplColSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct struct_bfa6e9ec *p = MCOLOR_SCS(ioObjp);

    DF_M1HELP_FadeOut();
    DF_MBACK_PopSceneLabel();
    for (int i = 0; i < 12; i++)
        WindowEx_SetFadeOut(p->p_label[i]);
    for (int i = 0; i < 2; i++)
        DF_MOBJ2_SetBlinkSpriteFadeOut(p->p_allow_sp[i]);
    WindowEx_SetFadeOut(p->p_lt_crsr);
    p->p_col_cube->count = 0;
    p->p_col_cube->stat = 2;
    DF_MVIEW_MotionStart(MViewp, 0x21);
    mcolor_change_tag(9);
    mcolor_guide(0, 1, 2, 0, 0x63);
    ioObjp->count = 0;
    ioObjp->stat = 2;
    ioObjp->play_func = MCOLOR_PLAY(__PlaySmplColSystemFadeOut);
}

/* entry from mpaint.c: the sample-colour list for player inPlayer (pAC NULL = the player's current AC) */
struct stMobj *DF_MCOLOR_NewSmplColSystem(struct stMobj *inObjp, int inPlayer, struct stMenuAC *pAC)
{
    PS2_FRAME(0x50);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);

    if (!obj)
        return NULL;
    struct struct_bfa6e9ec *p = MCOLOR_SCS(obj);
    obj->type = 0;
    obj->stat = 1;
    p->player = (char)inPlayer;
    p->acp = pAC ? pAC : M_ACp[(int)p->player][0];
    p->step = 0;
    p->f_modify = 0;
    p->slct = 7;
    p->slct_top = 7;
    p->slct_view = 0;
    p->bk_colp = (struct struct_098975f3 *)DF_MMM_Alloc(2, 0xb0);
    memcpy(p->bk_colp, &p->acp->real_col, 0xb0);
    DF_MBACK_PushSceneLabel((enum_5124d09c)0x20);
    mcolor_guide(0, 1, 2, 0, 0x63);
    DF_MVIEW_MotionStart(MViewp, 0x1e);
    obj->stat = 1;
    obj->play_func = MCOLOR_PLAY(__PlaySmplColSystemFadeIn);
    obj->count = 0;
    return obj;
}

/* ---- EditColSystem part-list windows ---- */

#define MCOLOR_EPC(o) ((struct struct_dcc847c1 *)&(o)->dt)

/* the part list comes back: rows slide in staggered, the cursor row gets the light cursor */
static void __SetEditColSystem_OpenPartsWnd(struct struct_a99b3137 *p_ecsp)
{
    PS2_FRAME(0x160);

    for (int i = 0; i < s_partsTypeNum[(int)p_ecsp->sel_mode]; i++) {
        struct stMobj *w = p_ecsp->obj_wnd[i + 1];
        WindowEx_MoveToLayout(w, mcolor_layout(0x420), s_EditColSysObjWndName[i + 1], 6);
        if (i == p_ecsp->idx) {
            PS2_STACK(char[0x40], buf, 0x60);
            ps2_sprintf_d(buf, mcolor_fmt_light_cursor, p_ecsp->idx);
            WindowEx_MoveToLayout(p_ecsp->cursor_wnd, mcolor_layout(0x420), buf, 6);
            WindowEx_SetFadeIn(p_ecsp->cursor_wnd);
        } else {
            WindowEx_SetCurMoveCount(w, -i * 2);
        }
    }
    WindowEx_SetFadeIn(p_ecsp->obj_wnd[0]);
    for (int j = 0; j < 2; j++) {
        DF_MOBJ_SetDrawEnable(p_ecsp->apMobjSprite[j], 1);
        UnitSprite_SetFadeIn(p_ecsp->apMobjSprite[j]);
    }
    for (int j = 2; j < 4; j++)
        UnitSprite_SetFadeOut(p_ecsp->apMobjSprite[j]);
    p_ecsp->mdl_move_cnt = 0;
}

/* the other rows slide out left (300 px), the picked row moves to the first row's place (parts02_0) */
static void __SetEditColSystem_ClosePartsWnd(struct struct_a99b3137 *p_ecsp)
{
    PS2_FRAME(0x160);

    for (int i = 0; i < s_partsTypeNum[(int)p_ecsp->sel_mode]; i++) {
        if (i == p_ecsp->idx)
            continue;
        struct stMobj *w = p_ecsp->obj_wnd[i + 1];
        PS2_STACK(struct stMenuRect, rect, 0x150);
        WindowEx_GetRect(w, &rect);
        rect.x -= 300;
        WindowEx_Move(w, &rect, 6);
        WindowEx_SetCurMoveCount(w, -(s_partsTypeNum[(int)p_ecsp->sel_mode] - i - 1) * 2);
    }
    char *lo = mcolor_layout(0x420);
    WindowEx_MoveToLayout(p_ecsp->obj_wnd[p_ecsp->idx + 1], lo, s_EditColSysObjWndName[1], 6);
    PS2_STACK(char[0x40], buf, 0x50);
    ps2_sprintf_d(buf, mcolor_fmt_light_cursor, p_ecsp->idx);
    WindowEx_MoveToLayout(p_ecsp->cursor_wnd, mcolor_layout(0x420), buf, 6);
    WindowEx_SetFadeOut(p_ecsp->cursor_wnd);
    WindowEx_SetFadeOut(p_ecsp->obj_wnd[0]);
    for (int j = 0; j < 2; j++)
        UnitSprite_SetFadeOut(p_ecsp->apMobjSprite[j]);
    for (int j = 2; j < 4; j++) {
        DF_MOBJ_SetDrawEnable(p_ecsp->apMobjSprite[j], 1);
        UnitSprite_SetFadeIn(p_ecsp->apMobjSprite[j]);
    }
    p_ecsp->mdl_move_cnt = 0;
}

/* ---- EditPartCol (RGB bars) ---- */

static inline void mcolor_erase(struct stMobj **pp)
{
    if (*pp) {
        (*pp)->f_draw = 0;
        (*pp)->f_erase = 1;
        *pp = NULL;
    }
}

/* 6 frames: ring cubes, model rect / position / scale back to the list view; then the teardown at count 15 */
static void __PlayEditPartColFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x60);
    struct struct_dcc847c1 *p = MCOLOR_EPC(ioObjp);
    struct struct_a99b3137 *ecsp = (struct struct_a99b3137 *)p->ecsp;

    ioObjp->count++;
    ecsp->mdl_move_cnt++;
    int n = ecsp->mdl_move_cnt;
    if (n < 7) {
        if (s_pColCube->pMdl[0]) {
            for (int i = 0; i < 5; i++) {
                struct struct_e1bb11f7 *c = s_pColCube;
                DF_MFUNC_CalcInterVector(c->Pos[i], c->PrevPos[i], c->NextPos[i], n, 6);
                c = s_pColCube;
                memcpy(c->pKageMdl[i]->pos, c->Pos[i], 16);
                memcpy(c->pMdlCamo[i]->pos, c->pKageMdl[i]->pos, 16);
                memcpy(c->pMdl[i]->pos, c->pMdlCamo[i]->pos, 16);
                c->pKageMdl[i]->pos[1] = c->pKageMdl[i]->pos[1] + 1.0f;
            }
        }
        DF_MFUNC_CalcInterRect(&ecsp->mdl_rect, &ecsp->mdl_prev_rect, &ecsp->mdl_next_rect, n, 6);
        struct stMobj *m = ecsp->mdlp;
        memcpy(&m->dt.model.show_rect, &ecsp->mdl_rect, 16);
        memcpy(&ecsp->mdlp->dt.model.rect, &m->dt.model.show_rect, 16);
        DF_MFUNC_CalcInterVector(ecsp->mdl_pos, ecsp->mdl_prev_pos, ecsp->mdl_next_pos, n, 6);
        DF_MOBJ_SetModelPos(ecsp->mdlp, ecsp->mdl_pos);
        DF_MFUNC_CalcInterVector(ecsp->mdl_scl, ecsp->mdl_prev_scl, ecsp->mdl_next_scl, n, 6);
        DF_MOBJ_SetModelScale(ecsp->mdlp, ecsp->mdl_scl);
        return;
    }
    if (ioObjp->count < 0xf)
        return;
    for (int i = 0; i < 13; i++) {
        UnitSprite_Delete(p->p_unit_sp[i]);
        p->p_unit_sp[i] = NULL;
    }
    mcolor_erase(&p->strp);
    for (int j = 0; j < 3; j++)
        mcolor_erase(&p->rgb_str[j]);
    for (int j = 0; j < 3; j++)
        mcolor_erase(&p->rgb_val_str[j]);
    WindowEx_Delete(p->p_wnd);
    p->p_wnd = NULL;
    DF_MVIEW_MotionStart(MViewp, 0x1f);
    ioObjp->play_func = NULL;
    ioObjp->stat = 5;
}

static inline unsigned char *mcolor_rgb(struct struct_dcc847c1 *p, int k)
{
    switch (k) {
    case 1:
        return &p->col.g;
    case 2:
        return &p->col.b;
    default:
        return &p->col.r;
    }
}

/* bar k's width = its full width (prev_rect.w) * value / 200; the highlight sprites follow rgb_idx */
static void __SetEditPartColUpdateColorBar(struct stMobj *ioObjp)
{
    PS2_FRAME(0x60);
    struct struct_dcc847c1 *p = MCOLOR_EPC(ioObjp);

    for (int i = 0; i < 3; i++) {
        struct stMobj *child = UnitSprite_GetChildObject(p->p_unit_sp[4 + i], 0);
        int sel = (i == p->rgb_idx);
        int v = *mcolor_rgb(p, i);
        child->dt.sprite.rect.w = (child->dt.sprite.prev_rect.w * v) / 200;
        DF_MOBJ_SetDrawEnable(p->p_unit_sp[7 + i], sel);
        DF_MOBJ_SetDrawEnable(p->p_unit_sp[10 + i], sel);
    }
}

static void __PlayEditPartCol(struct stMobj *ioObjp)
{
    PS2_FRAME(0x160);
    struct struct_dcc847c1 *p = MCOLOR_EPC(ioObjp);

    if (MenuKeyGuide_IsShown())
        return;
    if (MPad[MPadUser].start == 1) {
        MenuKeyGuide_Show(0x1c);
        return;
    }
    int old_idx = p->idx;
    if (MPad[MPadUser].Rrt == 1) {
        Voice_Call(0x3eb, 0x7f);
        *p->f_modifyp = 0;
        __SetEditPartColFadeOut(ioObjp);
        return;
    }
    if (MPad[MPadUser].Rdw == 1) {
        Voice_Call(0x3ea, 0x7f);
        *p->f_modifyp = 1;
        __SetEditPartColFadeOut(ioObjp);
        return;
    }
    if (s_pColCube->RotCount == 0) {
        if (MPad[MPadUser].repeat.l1 == 1) {
            Voice_Call(0x3e9, 0x7f);
            s_pColCube->RotCount += 5;
            p->idx--;
            if (p->idx < 0)
                p->idx = 4;
        }
        if (MPad[MPadUser].repeat.r1 == 1) {
            Voice_Call(0x3e9, 0x7f);
            s_pColCube->RotCount -= 5;
            p->idx++;
            if (p->idx >= 5)
                p->idx = 0;
        }
    }
    if (p->idx != old_idx) {
        p->col.r = p->p_colp->s_col[p->idx].r;
        p->col.g = p->p_colp->s_col[p->idx].g;
        p->col.b = p->p_colp->s_col[p->idx].b;
        DF_MOBJ_SetStringID(p->strp, 0, p->idx + 0x18808, 0);
        return;
    }

    int old_rgb = p->rgb_idx;
    if (MPad[MPadUser].up == 1) {
        p->rgb_idx--;
        if (p->rgb_idx < 0)
            p->rgb_idx = 0;
    }
    if (MPad[MPadUser].dw == 1) {
        p->rgb_idx++;
        if (p->rgb_idx >= 3)
            p->rgb_idx = 2;
    }
    if (old_rgb != p->rgb_idx) {
        /* dead local (never read): zeroed byte-wise, y = row * 30 */
        PS2_STACK(struct struct_214acd28, mv, 0x158);
        memset(&mv, 0, sizeof(mv));
        mv.y = p->rgb_idx * 30;
        (void)mv;
        Voice_Call(0x3e9, 0x7f);
    }

    unsigned char *vp = mcolor_rgb(p, p->rgb_idx);
    unsigned char old = *vp;
    int v = old;
    int lt = MPad[MPadUser].lt;
    if (lt == 1 || lt >= 0x15) {
        v -= (lt == 1) ? 1 : 2;
        if (v < 0)
            v = 0;
    }
    int rt = MPad[MPadUser].rt;
    if (rt == 1 || rt >= 0x15) {
        v += (rt == 1) ? 1 : 2;
        if (v > 0xc8)
            v = 0xc8;
    }
    if (v != old)
        Voice_Call(0x3f2, 0x7f);
    *vp = (unsigned char)v;

    p->p_colp->s_col[p->idx].r = p->col.r;
    p->p_colp->s_col[p->idx].g = p->col.g;
    p->p_colp->s_col[p->idx].b = p->col.b;
    for (int k = 0; k < 3; k++) {
        PS2_STACK(char[0x40], buf, 0x50);
        ps2_sprintf_d(buf, mcolor_fmt_03d, *mcolor_rgb(p, k));
        DF_MOBJ_SetStringCopy(p->rgb_val_str[k], buf);
    }
    __SetEditPartColUpdateColorBar(ioObjp);
}

/* leave the RGB editor: everything fades, the part list comes back, ring and model return to the list view */
static void __SetEditPartColFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x60);
    struct struct_dcc847c1 *p = MCOLOR_EPC(ioObjp);
    struct struct_a99b3137 *ecsp = (struct struct_a99b3137 *)p->ecsp;

    DF_MOBJ_SetFadeOut(p->strp);
    for (int i = 0; i < 13; i++)
        UnitSprite_SetFadeOut(p->p_unit_sp[i]);
    for (int j = 0; j < 3; j++)
        DF_MOBJ_SetFadeOut(p->rgb_str[j]);
    for (int j = 0; j < 3; j++)
        DF_MOBJ_SetFadeOut(p->rgb_val_str[j]);
    WindowEx_SetFadeOut(p->p_wnd);
    __SetEditColSystem_OpenPartsWnd(ecsp);

    if (ecsp->sel_mode == 0) {
        struct struct_e1bb11f7 *c = s_pColCube;
        for (int k = 0; k < 5; k++) {
            memcpy(c->PrevPos[k], c->Pos[k], 16);
            memcpy(c->NextPos[k], COLOR_CUBE_POS[k], 16);
        }
        c->RotCount = 0;
        s_pColCube->Rot = 0;
    }
    ecsp->mdl_prev_rect = ecsp->mdl_rect;
    ecsp->mdl_next_rect = MCOLOR_MDL_RECT;
    memcpy(ecsp->mdl_prev_pos, ecsp->mdl_pos, 16);
    memcpy(ecsp->mdl_next_pos, AC_PART_POS[(int)ecsp->sel_mode][ecsp->idx][0], 16);
    memcpy(ecsp->mdl_prev_scl, ecsp->mdl_scl, 16);
    memcpy(ecsp->mdl_next_scl, AC_PART_SCL[(int)ecsp->sel_mode][ecsp->idx][0], 16);
    ioObjp->count = 0;
    ioObjp->stat = 2;
    ioObjp->play_func = MCOLOR_PLAY(__PlayEditPartColFadeOut);
    mcolor_guide(0, 1, 2, 0, 0x63);
    DF_M1HELP_FadeOut();
}

static void __PlayEditPartColFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    struct struct_dcc847c1 *p = MCOLOR_EPC(ioObjp);

    unsigned char old = ioObjp->count;
    ioObjp->count = old + 1;
    if (old < 0xf)
        return;
    if (p->step == 0) {
        DF_M1HELP_SetString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x18a9a));
        DF_M1HELP_FadeIn();
        p->step++;
    }
    if (DF_MVIEW_MotionSync(MViewp)) {
        ioObjp->count = 0;
        ioObjp->stat = 0;
        ioObjp->play_func = MCOLOR_PLAY(__PlayEditPartCol);
    }
}

/* OLMENU 0x1edc190: the three bars' (fill, sub) colours as r,g,b,a bytes */
static const unsigned int mcolor_bar_cols[6] = {0x803830cd, 0x80367fd1, 0x805a962a,
                                                0x804fba86, 0x80b73847, 0x80b57545};

/* the RGB editor for swatch 0 of ioPColp (one AC part): 13 unit sprites, a window, title + 3 labels + 3 values */
static struct stMobj *__NewEditPartCol(struct stMobj *inObjp, struct struct_c4910010 *ioPColp,
                                       struct struct_a99b3137 *ecsp, int *ioFlagModify)
{
    PS2_FRAME(0x1c0);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);

    if (!obj)
        return NULL;
    struct struct_dcc847c1 *p = MCOLOR_EPC(obj);
    obj->type = 0;
    p->p_colp = ioPColp;
    p->ecsp = ecsp;
    p->f_modifyp = ioFlagModify;
    p->idx = 0;
    p->col.r = p->p_colp->s_col[p->idx].r;
    p->col.g = p->p_colp->s_col[p->idx].g;
    p->col.b = p->p_colp->s_col[p->idx].b;

    struct stMobj *parent = obj;
    for (int i = 0; i < 13; i++) {
        struct _LO_HEADER *lo = (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x421);
        p->p_unit_sp[i] = UnitSprite_New(parent, lo, s_ColEditUnitSpName[i]);
        UnitSprite_SetFadeIn(p->p_unit_sp[i]);
        parent = p->p_unit_sp[i];
    }
    p->p_wnd = WindowEx_NewFromLayout(parent, (_WINDOWEX_TYPE)0, mcolor_layout(0x421), mcolor_parts06a_0);
    WindowEx_SetFadeIn(p->p_wnd);
    struct stMobj *wnd = p->p_wnd;

    for (int j = 0; j < 3; j++) {
        PS2_STACK(unsigned int[6], col_ary, 0x190);
        memcpy(col_ary, mcolor_bar_cols, sizeof(mcolor_bar_cols));
        struct stMobj *child = UnitSprite_GetChildObject(p->p_unit_sp[4 + j], 0);
        DF_MOBJ_SetSpriteFlag(child, 1);
        DF_MOBJ_SetSpriteDrawMode(child, 1);
        DF_MOBJ_SetSpriteSubColor(child, (struct struct_76b810ec *)&col_ary[j * 2 + 1]);
        memcpy(&child->dt.sprite.init_col, &col_ary[j * 2], 4);
        UnitSprite_SetFadeIn(p->p_unit_sp[4 + j]);
        memcpy(&child->dt.sprite.prev_rect, &child->dt.sprite.rect, 16);
    }
    __SetEditPartColUpdateColorBar(obj);

    PS2_STACK(struct struct_76b810ec, title_col, 0x1bc);
    title_col.r = 0x65;
    title_col.g = 0x65;
    title_col.b = 0x65;
    title_col.a = 0x80;
    /* the original's stack arguments 9 / 10 (inDepth, inFlag) */
    PS2_STACK(unsigned long long, arg9, 0x0);
    PS2_STACK(unsigned long long, arg10, 0x8);
    arg9 = 0;
    arg10 = 0;
    (void)arg9;
    (void)arg10;
    p->strp = DF_MOBJ_NewString(wnd, 0, 0x18808, 0, 1, 0x12, 0x115, &title_col, 0, 0);
    DF_MOBJ_SetFadeIn(p->strp);
    struct stMobj *prev = p->strp;

    for (int j = 0; j < 3; j++) {
        PS2_STACK(struct struct_76b810ec, lbl_col, 0x1b8);
        PS2_STACK(char *[3], names, 0x1a8);
        lbl_col.r = 0x8c;
        lbl_col.g = 0x92;
        lbl_col.b = 0x92;
        lbl_col.a = 0x80;
        names[0] = mcolor_str_red;
        names[1] = mcolor_str_green;
        names[2] = mcolor_str_blue;
        p->rgb_str[j] = DF_MOBJ_NewStringCopy(prev, names[j], 1, 0x72, 0x12f + 30 * j, &lbl_col, 0, 0);
        prev = p->rgb_str[j];
        DF_MOBJ_SetFadeIn(prev);
    }
    for (int j = 0; j < 3; j++) {
        PS2_STACK(struct struct_76b810ec, val_col, 0x1b4);
        PS2_STACK(char[0x40], buf, 0x90);
        val_col.r = 0x8c;
        val_col.g = 0x92;
        val_col.b = 0x92;
        val_col.a = 0x80;
        ps2_sprintf_d(buf, mcolor_fmt_03d, *mcolor_rgb(p, j));
        p->rgb_val_str[j] = DF_MOBJ_NewStringCopy(prev, buf, 1, 0xe8, 0x12f + 30 * j, &val_col, 0, 0);
        prev = p->rgb_val_str[j];
        DF_MOBJ_SetFadeIn(prev);
    }
    mcolor_guide(5, 1, 2, 0, 0x63);
    DF_MVIEW_MotionStart(MViewp, 0x20);
    obj->count = 0;
    obj->play_func = MCOLOR_PLAY(__PlayEditPartColFadeIn);
    obj->stat = 1;
    return obj;
}

/* ---- SelPartMode (camouflage pattern chooser) ---- */

#define MCOLOR_SPM(o) ((struct struct_ff3e9029 *)&(o)->dt)

/* 6 frames: ring cubes, model rect / position / scale back to the list view; then the windows go once the
 * object's own count reached 7, the window faded out and the view motion is done */
static void __PlaySelPartModeFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x60);
    struct struct_ff3e9029 *p = MCOLOR_SPM(ioObjp);
    struct struct_a99b3137 *ecsp = (struct struct_a99b3137 *)p->ecsp;

    ioObjp->count++;
    ecsp->mdl_move_cnt++;
    int n = ecsp->mdl_move_cnt;
    if (n < 7) {
        if (s_pColCube->pMdl[0]) {
            for (int i = 0; i < 5; i++) {
                struct struct_e1bb11f7 *c = s_pColCube;
                DF_MFUNC_CalcInterVector(c->Pos[i], c->PrevPos[i], c->NextPos[i], n, 6);
                c = s_pColCube;
                memcpy(c->pKageMdl[i]->pos, c->Pos[i], 16);
                memcpy(c->pMdlCamo[i]->pos, c->pKageMdl[i]->pos, 16);
                memcpy(c->pMdl[i]->pos, c->pMdlCamo[i]->pos, 16);
                c->pKageMdl[i]->pos[1] = c->pKageMdl[i]->pos[1] + 1.0f;
            }
        }
        DF_MFUNC_CalcInterRect(&ecsp->mdl_rect, &ecsp->mdl_prev_rect, &ecsp->mdl_next_rect, n, 6);
        struct stMobj *m = ecsp->mdlp;
        memcpy(&m->dt.model.show_rect, &ecsp->mdl_rect, 16);
        memcpy(&ecsp->mdlp->dt.model.rect, &m->dt.model.show_rect, 16);
        DF_MFUNC_CalcInterVector(ecsp->mdl_pos, ecsp->mdl_prev_pos, ecsp->mdl_next_pos, n, 6);
        DF_MOBJ_SetModelPos(ecsp->mdlp, ecsp->mdl_pos);
        DF_MFUNC_CalcInterVector(ecsp->mdl_scl, ecsp->mdl_prev_scl, ecsp->mdl_next_scl, n, 6);
        DF_MOBJ_SetModelScale(ecsp->mdlp, ecsp->mdl_scl);
        return;
    }
    if (ioObjp->count < 7)
        return;
    if (p->p_wnd_sp[0]->stat == 2)
        return;
    if (!DF_MVIEW_MotionSync(MViewp))
        return;
    for (int i = 0; i < 2; i++)
        WindowEx_Delete(p->p_wnd_sp[i]);
    for (int i = 0; i < 2; i++)
        WindowEx_Delete(p->p_cursor_wnd_sp[i]);
    ioObjp->play_func = NULL;
    ioObjp->stat = 5;
}

/* TypeAlpha (unread in this file) = f; plain cubes alpha 1, camouflage cubes 0 */
static inline void mcolor_type_alpha_reset(float f)
{
    s_pColCube->NextTypeAlpha = f;
    s_pColCube->PrevTypeAlpha = f;
    s_pColCube->TypeAlpha = f;
    for (int i = 0; i < 5; i++) {
        s_pColCube->pMdl[i]->mulalpha = 1.0f;
        s_pColCube->pMdlCamo[i]->mulalpha = 0.0f;
    }
}

static void __PlaySelPartMode(struct stMobj *ioObjp)
{
    PS2_FRAME(0x80);
    struct struct_ff3e9029 *p = MCOLOR_SPM(ioObjp);

    if (MenuKeyGuide_IsShown())
        return;
    if (MPad[MPadUser].start == 1) {
        MenuKeyGuide_Show(0x1e);
        return;
    }
    struct struct_a99b3137 *ecsp = (struct struct_a99b3137 *)p->ecsp;

    if (MPad[MPadUser].Rrt == 1) {
        /* decide: apply the chosen patterns (only when the parent's colours are unchanged) */
        Voice_Call(0x3eb, 0x7f);
        if (*p->f_modifyp == 0) {
            char flag4[4] = {1, 1, 1, 1};
            DF_MOBJ_SetPatternModelAC(ecsp->mdlp, flag4, 0);
            for (int i = 0; i < 4; i++) {
                flag4[i] = p->camouflage[i];
                p->pAC->camouflage[i] = p->camouflage[i];
                DF_M_AC_SetCamoPettern(p->pAC, p->pAC->cur_part[i], i);
            }
            DF_MOBJ_SetPatternModelAC(ecsp->mdlp, flag4, 1);
            DF_M_AC_UpdateClut(ecsp->acp, ecsp->colp, 1);
            int on = (p->sel_index == 0) ? p->camouflage[1] : p->camouflage[p->sel_index - 1];
            mcolor_type_alpha_reset(on ? 1.0f : 0.0f);
        }
        __SetSelPartModeFadeOut(ioObjp);
        return;
    }
    if (MPad[MPadUser].Rdw == 1) {
        /* cancel: report whether the AC's patterns differ from the ones the parent entered with */
        Voice_Call(0x3ea, 0x7f);
        char changed = 0;
        for (int i = 0; i < 4; i++) {
            if (p->pAC->camouflage[i] != ecsp->camouflage[i]) {
                changed = (char)(changed + 1);
                break;
            }
        }
        *p->f_modifyp = (changed != 0);
        __SetSelPartModeFadeOut(ioObjp);
        return;
    }
    if (!(MPad[MPadUser].repeat.up || MPad[MPadUser].repeat.dw))
        return;

    /* up / down: toggle the pattern and preview it on the model */
    p->idx = 1 - p->idx;
    Voice_Call(0x3e9, 0x7f);
    for (int j = 0; j < 2; j++)
        DF_MOBJ_SetDrawEnable(p->p_cursor_wnd_sp[j], p->idx == j);
    mcolor_type_alpha_reset(p->idx ? 1.0f : 0.0f);
    DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, p->idx + 0x194b0));
    char flag4[4];
    memset(flag4, 0, sizeof(flag4));
    if (p->sel_index == 0) {
        for (int i = 0; i < 4; i++) {
            flag4[i] = 1;
            p->pAC->camouflage[i] = (unsigned char)p->idx;
            DF_M_AC_SetCamoPettern(p->pAC, p->pAC->cur_part[i], i);
        }
    } else {
        flag4[p->sel_index - 1] = 1;
        p->pAC->camouflage[p->sel_index - 1] = (unsigned char)p->idx;
        DF_M_AC_SetCamoPettern(p->pAC, p->pAC->cur_part[p->sel_index - 1], p->sel_index - 1);
    }
    DF_MOBJ_SetPatternModelAC(ecsp->mdlp, flag4, p->idx);
    DF_M_AC_UpdateClut(p->pAC, ecsp->colp, 1);
}

/* leave the chooser: windows fade, the part list comes back, ring and model return to the list view */
static void __SetSelPartModeFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x60);
    struct struct_ff3e9029 *p = MCOLOR_SPM(ioObjp);
    struct struct_a99b3137 *ecsp = (struct struct_a99b3137 *)p->ecsp;

    for (int i = 0; i < 2; i++)
        WindowEx_SetFadeOut(p->p_wnd_sp[i]);
    for (int i = 0; i < 2; i++)
        WindowEx_SetFadeOut(p->p_cursor_wnd_sp[i]);
    __SetEditColSystem_OpenPartsWnd(ecsp);

    if (ecsp->sel_mode == 0) {
        struct struct_e1bb11f7 *c = s_pColCube;
        for (int k = 0; k < 5; k++) {
            memcpy(c->PrevPos[k], c->Pos[k], 16);
            memcpy(c->NextPos[k], COLOR_CUBE_POS[k], 16);
        }
        c->RotCount = 0;
        s_pColCube->Rot = 0;
    }
    ecsp->mdl_prev_rect = ecsp->mdl_rect;
    ecsp->mdl_next_rect = MCOLOR_MDL_RECT;
    memcpy(ecsp->mdl_prev_pos, ecsp->mdl_pos, 16);
    memcpy(ecsp->mdl_next_pos, AC_PART_POS[(int)ecsp->sel_mode][ecsp->idx][0], 16);
    memcpy(ecsp->mdl_prev_scl, ecsp->mdl_scl, 16);
    memcpy(ecsp->mdl_next_scl, AC_PART_SCL[(int)ecsp->sel_mode][ecsp->idx][0], 16);
    ioObjp->count = 0;
    ioObjp->stat = 2;
    ioObjp->play_func = MCOLOR_PLAY(__PlaySelPartModeFadeOut);
    mcolor_guide(0, 1, 2, 0, 0x63);
    DF_M1HELP_FadeOut();
}

/* step 0: the two pattern windows + their light cursors and the help line; then wait for the view motion */
static void __PlaySelPartModeFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct struct_ff3e9029 *p = MCOLOR_SPM(ioObjp);

    if (p->step == 0) {
        struct stMobj *w;
        p->p_wnd_sp[0] = w = WindowEx_NewFromLayout(ioObjp, (_WINDOWEX_TYPE)0, mcolor_layout(0x422), mcolor_parts02_1);
        p->p_wnd_sp[1] = w = WindowEx_NewFromLayout(w, (_WINDOWEX_TYPE)0, mcolor_layout(0x422), mcolor_parts02_2);
        p->p_cursor_wnd_sp[0] = w =
            WindowEx_NewFromLayout(w, (_WINDOWEX_TYPE)0xb, mcolor_layout(0x422), mcolor_light_cursor_mk2_0);
        p->p_cursor_wnd_sp[1] =
            WindowEx_NewFromLayout(w, (_WINDOWEX_TYPE)0xb, mcolor_layout(0x422), mcolor_light_cursor_mk2_1);
        WindowEx_SetString(p->p_wnd_sp[0], (char *)DF_MSTR_GetName(MenuMng.language, 0, 0x194b0));
        WindowEx_SetString(p->p_wnd_sp[1], (char *)DF_MSTR_GetName(MenuMng.language, 0, 0x194b1));
        WindowEx_SetStringPos(p->p_wnd_sp[0], 0x23, 5);
        WindowEx_SetStringPos(p->p_wnd_sp[1], 0x23, 5);
        for (int j = 0; j < 2; j++) {
            DF_MOBJ_SetDrawEnable(p->p_cursor_wnd_sp[j], p->idx == j);
            WindowEx_BlinkBegin(p->p_cursor_wnd_sp[j]);
        }
        for (int j = 0; j < 2; j++)
            WindowEx_SetFadeIn(p->p_wnd_sp[j]);
        for (int j = 0; j < 2; j++)
            WindowEx_SetFadeIn(p->p_cursor_wnd_sp[j]);
        DF_M1HELP_SetString(DF_MSTR_GetInfo(MenuMng.language, 0, p->idx + 0x194b0));
        DF_M1HELP_FadeIn();
        p->step++;
    }
    if (DF_MVIEW_MotionSync(MViewp)) {
        ioObjp->stat = 0;
        ioObjp->play_func = MCOLOR_PLAY(__PlaySelPartMode);
    }
}

/* the chooser for camouflage entry selIndex (0 = all four parts, 1..4 = one part) of the player's AC */
static struct stMobj *__NewSelPartMode(struct stMobj *inObjp, struct struct_c4910010 *ioPColp, int selIndex,
                                       int player, struct struct_a99b3137 *ecsp, int *ioFlagModify,
                                       struct stMenuAC *pAC)
{
    PS2_FRAME(0x80);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);

    if (!obj)
        return NULL;
    struct struct_ff3e9029 *p = MCOLOR_SPM(obj);
    obj->type = 0;
    p->p_colp = ioPColp;
    p->ecsp = ecsp;
    p->sel_index = (char)selIndex;
    p->f_modifyp = ioFlagModify;
    p->player = (char)player;
    p->pAC = pAC ? pAC : M_ACp[(int)p->player][0];
    p->idx = p->pAC->camouflage[selIndex == 0 ? 1 : selIndex - 1];
    memcpy(p->camouflage, p->pAC->camouflage, 4);
    /* original: 4 arguments; t0 still holds its own 5th argument (ecsp), which the stub only spills */
    mcolor_guide(1, 2, 0, 0x63, (int)PS2PTR(ecsp));
    obj->play_func = MCOLOR_PLAY(__PlaySelPartModeFadeIn);
    obj->stat = 1;
    return obj;
}

/* ---- colour ring (s_pColCube) + EditColSystem entry / fade-in / fade-out start ---- */

#define MCOLOR_ECS(o) ((struct struct_a99b3137 *)&(o)->dt)

/* cube Index of the 5-cube ring at angle Ang: radius 1.8 around COLOR_CUBE_ROT_CENTER in the x/z plane */
static void __ColorCube_CalcPosition_Ring(float (*pPos)[4], float Ang, int Index)
{
    PS2_FRAME(0x30);
    const float two_pi = 0x1.921fb6p+2f; /* 0x40c90fdb */
    const float pi = 0x1.921fb6p+1f;     /* 0x40490fdb */
    float a = two_pi * (float)Index / 5.0f;
    a = a + (-0x1.921fb6p+0f /* -pi/2 0xbfc90fdb */ + Ang);

    int k = 0;
    do {
        if (a < pi) {
            if (!(a < -pi))
                break;
            a = a + two_pi;
        } else {
            a = a - two_pi;
        }
        k++;
    } while (k < 10);

    /* original: Pos[k] -> PrevPos[k] with k = the wrap counter (normally 0), not Index; k up to 10 runs past
     * PrevPos into NextPos / RotCount, so address by byte offset */
    char *cb = (char *)s_pColCube;
    memcpy(cb + 0x90 + k * 16, cb + 0x40 + k * 16, 16);

    memcpy(*pPos, COLOR_CUBE_ROT_CENTER, 16);
    (*pPos)[0] = (*pPos)[0] + 0x1.ccccccp+0f /* 1.8f 0x3fe66666 */ * fsmath_CosVU0(a);
    (*pPos)[2] = (*pPos)[2] + 0x1.ccccccp+0f * fsmath_SinVU0(a);
}

/* one step of an L1 / R1 ring spin (RotCount steps left, Rot 0..24 = 25 steps per turn) */
static void __ColorCube_UpdateRotate(void)
{
    PS2_FRAME(0x50);
    if (!s_pColCube || !s_pColCube->pMdl[0])
        return;
    if (s_pColCube->RotCount < 0) {
        s_pColCube->RotCount++;
        s_pColCube->Rot--;
        if (s_pColCube->Rot < 0)
            s_pColCube->Rot += 25;
    } else if (s_pColCube->RotCount > 0) {
        s_pColCube->RotCount--;
        s_pColCube->Rot++;
        if (s_pColCube->Rot >= 25)
            s_pColCube->Rot -= 25;
    } else {
        return;
    }
    float a = 0x1.921fb6p+2f /* 2pi */ * (float)s_pColCube->Rot / 25.0f;

    for (int i = 0; i < 5; i++) {
        __ColorCube_CalcPosition_Ring(&s_pColCube->Pos[i], a, i);
        struct struct_e1bb11f7 *c = s_pColCube;
        memcpy(c->pKageMdl[i]->pos, c->Pos[i], 16);
        memcpy(c->pMdlCamo[i]->pos, c->pKageMdl[i]->pos, 16);
        memcpy(c->pMdl[i]->pos, c->pMdlCamo[i]->pos, 16);
        c->pKageMdl[i]->pos[1] = c->pKageMdl[i]->pos[1] + 1.0f;
    }
}

/* Alpha steps 1/10 of (Next - Prev) per frame towards NextAlpha; then cubes + shadows get Alpha and the
 * camouflage cubes 0 (original: every frame, overriding the TypeAlpha settings) */
static void __ColorCube_UpdateFade(void)
{
    struct struct_e1bb11f7 *c = s_pColCube;

    if (!c || !c->pMdl[0])
        return;
    if (c->Alpha != c->NextAlpha && c->PrevAlpha != c->NextAlpha) {
        c->Alpha = c->Alpha + 0x1.99999ap-4f /* 0.1f */ * (c->NextAlpha - c->PrevAlpha);
        if (c->PrevAlpha < c->NextAlpha) {
            if (!(c->Alpha <= c->NextAlpha))
                c->Alpha = c->NextAlpha;
        }
        if (!(c->PrevAlpha <= c->NextAlpha)) {
            if (c->Alpha < c->NextAlpha)
                c->Alpha = c->NextAlpha;
        }
    }
    for (int i = 0; i < 5; i++) {
        s_pColCube->pMdl[i]->mulalpha = s_pColCube->Alpha;
        s_pColCube->pMdlCamo[i]->mulalpha = 0.0f;
        s_pColCube->pKageMdl[i]->mulalpha = s_pColCube->Alpha;
    }
}

/* 5 cubes (model 0x1cc), 5 camouflage cubes (0x1ce) and 5 shadows (0x1cd) at COLOR_CUBE_POS */
static void __ColorCube_Create(void)
{
    PS2_FRAME(0x80);

    for (int i = 0; i < 5; i++) {
        /* interpreted callee: scale quad (OLMENU 0x1edc1c0), angle quad (0x1edc1d0) and the three colours (main
         * sdata gp-0x7648 / -0x7644 / -0x7640) at their original slots */
        PS2_STACK(float[4], scl, 0x60);
        PS2_STACK(float[4], ang, 0x50);
        PS2_STACK(struct struct_76b810ec, col_mdl, 0x7c);
        PS2_STACK(struct struct_76b810ec, col_camo, 0x78);
        PS2_STACK(struct struct_76b810ec, col_kage, 0x74);
        float (*pos)[4] = &COLOR_CUBE_POS[i];

        scl[0] = 0.8f;
        scl[1] = 0.8f;
        scl[2] = 0.8f;
        scl[3] = 1.0f;
        ang[0] = 0.0f;
        ang[1] = 0.0f;
        ang[2] = 0.0f;
        ang[3] = 1.0f;
        col_mdl.r = 0;
        col_mdl.g = 0;
        col_mdl.b = 0;
        col_mdl.a = 0x80;
        struct _stMModel *m = DF_MGEN_NewModelEasy(0x1cc, pos, &ang, &scl, &col_mdl);
        s_pColCube->pMdl[i] = m;
        s_pColCube->pMdl[i]->test_reg = 0;
        s_pColCube->pMdl[i]->alpha_reg = 0x44;
        s_pColCube->pMdl[i]->mulalpha = 1.0f;
        struct struct_e1bb11f7 *c = s_pColCube;
        memcpy(c->NextPos[i], c->pMdl[i]->pos, 16);
        memcpy(c->PrevPos[i], c->NextPos[i], 16);
        memcpy(c->Pos[i], c->PrevPos[i], 16);

        col_camo.r = 0;
        col_camo.g = 0;
        col_camo.b = 0;
        col_camo.a = 0x80;
        m = DF_MGEN_NewModelEasy(0x1ce, pos, &ang, &scl, &col_camo);
        s_pColCube->pMdlCamo[i] = m;
        s_pColCube->pMdlCamo[i]->test_reg = 0;
        s_pColCube->pMdlCamo[i]->alpha_reg = 0x44;
        s_pColCube->pMdlCamo[i]->mulalpha = 1.0f;

        col_kage.r = 0x80;
        col_kage.g = 0x80;
        col_kage.b = 0x80;
        col_kage.a = 0x40;
        m = DF_MGEN_NewModelEasy(0x1cd, pos, NULL, &scl, &col_kage);
        s_pColCube->pKageMdl[i] = m;
        s_pColCube->pKageMdl[i]->pos[1] = s_pColCube->pKageMdl[i]->pos[1] + 1.0f;
        s_pColCube->pKageMdl[i]->test_reg = 0;
        s_pColCube->pKageMdl[i]->alpha_reg = 0x42;
        s_pColCube->pKageMdl[i]->mulalpha = 1.0f;
    }
    s_pColCube->RotCount = 0;
    s_pColCube->Rot = 0;
    s_pColCube->Alpha = 1.0f;
    s_pColCube->PrevAlpha = 1.0f;
    s_pColCube->NextAlpha = 1.0f;
}

/* pkt_func of the EditColSystem: ring step + fade, then shadows, cubes (swatch colour + 0x1e, u8 wrap) and
 * camouflage cubes */
static void __PckEditColSystem(void)
{
    PS2_FRAME(0x80);
    unsigned int *pkt = DF_MPKT_MakeVIFCode(MPktCurrentp, 0, 0, 0, 0x13000000);

    __ColorCube_UpdateRotate();
    __ColorCube_UpdateFade();
    if (s_pColCube) {
        for (int i = 0; i < 5; i++) {
            if (s_pColCube->pKageMdl[i]) {
                DF_MMODEL_Play(s_pColCube->pKageMdl[i]);
                pkt = DF_MMODEL_MakeVu1PacketAC(pkt, s_pColCube->pKageMdl[i], NULL, 2);
            }
        }
        for (int i = 0; i < 5; i++) {
            struct struct_76b810ec col;
            col.r = 0;
            col.g = 0;
            col.b = 0;
            col.a = 0x80;
            struct struct_e1bb11f7 *c = s_pColCube;
            if (!c->pMdl[i])
                continue;
            if (c->pColor) {
                col.r = (unsigned char)(c->pColor->s_col[i].r + 0x1e);
                col.g = (unsigned char)(c->pColor->s_col[i].g + 0x1e);
                col.b = (unsigned char)(c->pColor->s_col[i].b + 0x1e);
            }
            DF_MMODEL_SetVertColor(s_pColCube->pMdl[i], 0, &col);
            DF_MMODEL_Play(s_pColCube->pMdl[i]);
            pkt = DF_MMODEL_MakeVu1PacketAC(pkt, s_pColCube->pMdl[i], NULL, 6);
            DF_MMODEL_SetVertColor(s_pColCube->pMdlCamo[i], 0, &col);
            DF_MMODEL_Play(s_pColCube->pMdlCamo[i]);
            pkt = DF_MMODEL_MakeVu1PacketAC(pkt, s_pColCube->pMdlCamo[i], NULL, 6);
        }
    }
    MPktCurrentp = pkt;
}

/* step 0 (after 15 frames): ring, AC part model, part-list windows, sprites and light cursor; step 1: help line;
 * step 2: wait for the cursor window, then the list play function */
static void __PlayEditColSystemFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x1b0);
    struct struct_a99b3137 *p = MCOLOR_ECS(ioObjp);
    PS2_STACK(struct stMenuRect, mdl_rect, 0x190);
    int strid[2] = {0x18802, 0x194e2}; /* main sdata gp-0x7658 */

    mdl_rect = MCOLOR_MDL_RECT;
    strid[(int)p->sel_mode] += p->idx;

    switch (p->step) {
    case 0: {
        unsigned char old = ioObjp->count;
        ioObjp->count = old + 1;
        if (old < 0xf)
            return;
        __ColorCube_Create();
        s_pColCube->pColor = &p->colp->p_col[1];
        s_pColCube->Alpha = 0.0f;
        s_pColCube->PrevAlpha = 0.0f;
        s_pColCube->NextAlpha = 1.0f;
        if (p->sel_mode == 1)
            mcolor_type_alpha_reset(p->acp->camouflage[1] ? 1.0f : 0.0f);

        /* the original's stack arguments 10..12 (pAC, player, inDispFlag) */
        PS2_STACK(unsigned long long, arg10, 0x0);
        PS2_STACK(unsigned long long, arg11, 0x8);
        PS2_STACK(unsigned long long, arg12, 0x10);
        arg10 = (unsigned long long)(long long)(int)PS2PTR(p->acp);
        arg11 = (unsigned long long)(long long)p->player;
        arg12 = 0x40000;
        (void)arg10;
        (void)arg11;
        (void)arg12;
        p->mdlp = DF_MOBJ_NewModelAC(ioObjp, 3, p->player, 0, &mdl_rect, NULL, NULL, 0x80,
                                     0x1.921fb6p+1f /* pi */, p->acp, p->player, 0x40000, 0.0f, 0.0f);
        DF_MOBJ_SetModelPos(p->mdlp, AC_PART_POS[(int)p->sel_mode][p->idx][0]);
        DF_MOBJ_SetModelScale(p->mdlp, AC_PART_SCL[(int)p->sel_mode][p->idx][0]);
        DF_MOBJ_SetFadeIn(p->mdlp);
        memcpy(p->mdl_pos, AC_PART_POS[(int)p->sel_mode][p->idx][0], 16);
        memcpy(p->mdl_next_pos, p->mdl_pos, 16);
        memcpy(p->mdl_prev_pos, p->mdl_next_pos, 16);
        memcpy(p->mdl_scl, AC_PART_SCL[(int)p->sel_mode][p->idx][0], 16);
        memcpy(p->mdl_next_scl, p->mdl_scl, 16);
        memcpy(p->mdl_prev_scl, p->mdl_next_scl, 16);

        /* title + rows (camouflage mode: title + 5 rows); each window is the parent of the next */
        int nw = 7;
        if (p->sel_mode > 0)
            nw--;
        struct stMobj *parent = ioObjp;
        for (int j = 0; j < nw; j++) {
            char *lo = mcolor_layout(0x420);
            p->obj_wnd[j] = WindowEx_NewFromLayout(parent, (_WINDOWEX_TYPE)0, lo, s_EditColSysObjWndName[j]);
            WindowEx_SetFadeIn(p->obj_wnd[j]);
            parent = p->obj_wnd[j];
        }
        for (int j = 0; j < s_partsTypeNum[(int)p->sel_mode]; j++) {
            struct stMobj *w = p->obj_wnd[j + 1];
            WindowEx_SetString(w, (char *)DF_MSTR_GetName(MenuMng.language, 0,
                                                          j + s_partsTypeStrId[(int)p->sel_mode]));
            WindowEx_SetStringPos(w, 0x23, 5);
        }
        PS2_STACK(char[0x40], buf, 0x90);
        for (int j = 0; j < 2; j++) {
            ps2_sprintf_d(buf, mcolor_fmt_parts08, j);
            p->apMobjSprite[j] = UnitSprite_New(parent, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x420), buf);
            DF_MOBJ_SetDrawEnable(p->apMobjSprite[j], 1);
            UnitSprite_SetFadeIn(p->apMobjSprite[j]);
        }
        for (int j = 2; j < 4; j++) {
            ps2_sprintf_d(buf, mcolor_fmt_parts08, j - 2);
            p->apMobjSprite[j] = UnitSprite_New(parent, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x421), buf);
            DF_MOBJ_SetDrawEnable(p->apMobjSprite[j], 0);
        }
        ps2_sprintf_d(buf, mcolor_fmt_light_cursor, 0);
        char *lo = mcolor_layout(0x420);
        p->cursor_wnd = WindowEx_NewFromLayout(parent, (_WINDOWEX_TYPE)0xb, lo, buf);
        WindowEx_SetFadeIn(p->cursor_wnd);
        WindowEx_BlinkBegin(p->cursor_wnd);
        p->step++;
        break;
    }
    case 1:
        DF_M1HELP_SetString(DF_MSTR_GetInfo(MenuMng.language, 0, strid[(int)p->sel_mode]));
        DF_M1HELP_FadeIn();
        if (p->sel_mode == 1) {
            s_pColCube->Alpha = 0.0f;
            s_pColCube->PrevAlpha = 0.0f;
            s_pColCube->NextAlpha = 1.0f;
        }
        p->step++;
        break;
    case 2:
        if (p->cursor_wnd->stat == 0) {
            ioObjp->play_func = MCOLOR_PLAY(__PlayEditColSystem);
            ioObjp->stat = 0;
        }
        break;
    default:
        break;
    }
}

/* leave the part list: mode 1 / 2 (RGB editor / pattern chooser) = list closes, ring (colour mode) and model move
 * to the edit view; mode 0 = everything fades out */
static void __SetEditColSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct struct_a99b3137 *p = MCOLOR_ECS(ioObjp);

    DF_M1HELP_FadeOut();
    ioObjp->count = 0;
    if (p->mode != 0) {
        __SetEditColSystem_ClosePartsWnd(p);
        if (p->sel_mode == 0) {
            for (int i = 0; i < 5; i++) {
                struct struct_e1bb11f7 *c = s_pColCube;
                memcpy(c->PrevPos[i], c->Pos[i], 16);
                __ColorCube_CalcPosition_Ring(&c->NextPos[i], 0.0f, i);
            }
            s_pColCube->RotCount = 0;
            s_pColCube->Rot = 0;
        }
        p->mdl_prev_rect = p->mdl_rect;
        p->mdl_next_rect = (p->sel_mode == 0) ? EDIT_COL_MDL_RECT : SLCT_PAT_MDL_RECT;
        memcpy(p->mdl_prev_pos, p->mdl_pos, 16);
        memcpy(p->mdl_next_pos, AC_PART_POS[(int)p->sel_mode][p->idx][1], 16);
        memcpy(p->mdl_prev_scl, p->mdl_scl, 16);
        memcpy(p->mdl_next_scl, AC_PART_SCL[(int)p->sel_mode][p->idx][1], 16);
        ioObjp->count = 0;
        ioObjp->stat = 2;
        ioObjp->play_func = MCOLOR_PLAY(__PlayEditColSystemFadeOut);
        if (p->sel_mode == 1) {
            s_pColCube->Alpha = 1.0f;
            s_pColCube->PrevAlpha = 1.0f;
            s_pColCube->NextAlpha = 0.0f;
        }
        return;
    }
    DF_MOBJ_SetFadeOut(p->mdlp);
    for (int i = 0; i < 7; i++)
        WindowEx_SetFadeOut(p->obj_wnd[i]);
    for (int i = 0; i < 2; i++)
        UnitSprite_SetFadeOut(p->apMobjSprite[i]);
    WindowEx_SetFadeOut(p->cursor_wnd);
    s_pColCube->Alpha = 1.0f;
    s_pColCube->PrevAlpha = 1.0f;
    s_pColCube->NextAlpha = 0.0f;
    DF_MBACK_PopSceneLabel();
    DF_MVIEW_MotionStart(MViewp, 0x21);
    ioObjp->count = 0;
    ioObjp->stat = 2;
    ioObjp->play_func = MCOLOR_PLAY(__PlayEditColSystemFadeOut);
}

/* entry from mpaint.c: the colour (mode 0) / camouflage (mode 1) part list for player inPlayer (pAC NULL = the
 * player's current AC) */
struct stMobj *DF_MCOLOR_NewEditColSystem(struct stMobj *inObjp, int inPlayer, int mode, struct stMenuAC *pAC)
{
    PS2_FRAME(0x60);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);

    if (!obj)
        return NULL;
    struct struct_a99b3137 *p = MCOLOR_ECS(obj);
    obj->type = 0;
    obj->stat = 1;
    p->player = (char)inPlayer;
    p->acp = pAC ? pAC : M_ACp[(int)p->player][0];
    p->sel_mode = (char)mode;
    p->mdl_rect = MCOLOR_MDL_RECT;
    p->mdl_next_rect = p->mdl_rect;
    p->mdl_prev_rect = p->mdl_next_rect;
    /* idx is the (zeroed) dt word: not initialised here */
    memcpy(p->mdl_pos, AC_PART_POS[(int)p->sel_mode][p->idx][0], 16);
    memcpy(p->mdl_next_pos, p->mdl_pos, 16);
    memcpy(p->mdl_prev_pos, p->mdl_next_pos, 16);
    memcpy(p->mdl_scl, AC_PART_SCL[(int)p->sel_mode][p->idx][0], 16);
    memcpy(p->mdl_next_scl, p->mdl_scl, 16);
    memcpy(p->mdl_prev_scl, p->mdl_next_scl, 16);
    p->bk_colp = (struct struct_098975f3 *)DF_MMM_Alloc(2, 0xb0);
    p->tmp_bk_colp = (struct struct_098975f3 *)DF_MMM_Alloc(2, 0xb0);
    p->colp = (struct struct_098975f3 *)DF_MMM_Alloc(2, 0xb0);
    p->fn_colp = (struct struct_098975f3 *)DF_MMM_Alloc(2, 0xb0);
    memcpy(p->bk_colp, &p->acp->real_col, 0xb0);
    memcpy(p->colp, p->bk_colp, 0xb0);
    memcpy(p->fn_colp, p->bk_colp, 0xb0);
    memcpy(&s_color, &p->acp->real_col, 0xb0); /* original: global copy, no reader seen */
    memcpy(p->camouflage, p->acp->camouflage, 4);
    if (p->sel_mode == 0) {
        mcolor_change_tag(0xa);
        DF_MBACK_PushSceneLabel((enum_5124d09c)0x26);
    } else {
        mcolor_change_tag(0x1e);
        DF_MBACK_PushSceneLabel((enum_5124d09c)0x31);
    }
    mcolor_guide(0, 1, 2, 0, 0x63);
    DF_MVIEW_MotionStart(MViewp, 0x1f);
    if (!s_pColCube) {
        s_pColCube = (struct struct_e1bb11f7 *)DF_MMM_Alloc(2, 0x160);
        ZeroMemory(s_pColCube, 0x160);
    }
    obj->stat = 1;
    obj->play_func = MCOLOR_PLAY(__PlayEditColSystemFadeIn);
    obj->pkt_func = MCOLOR_PLAY(__PckEditColSystem);
    obj->count = 0;
    return obj;
}

/* ---- EditColSystem play: dialog, child dispatch, fade-out, part list ---- */

/* play_func while the "apply changes?" dialog is open: 4 = back to the list, 0x14 = keep the edited colours,
 * 0x24 = restore the backup (and the entry patterns in camouflage mode); both leave the screen */
static void __PlayEditColSystemDialog(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    int r = DF_MDIALOG_Sync();

    if (!(r & 4))
        return;
    struct struct_a99b3137 *p = MCOLOR_ECS(ioObjp);
    if (r == 4) {
        ioObjp->stat = 0;
        ioObjp->play_func = MCOLOR_PLAY(__PlayEditColSystem);
    } else if (r == 0x24) {
        if (p->sel_mode == 0) {
            DF_M_AC_UpdateClut(p->acp, p->bk_colp, 1);
        } else {
            for (int i = 0; i < 4; i++) {
                p->acp->camouflage[i] = (unsigned char)p->camouflage[i];
                DF_M_AC_SetCamoPettern(p->acp, p->acp->cur_part[i], i);
            }
            DF_M_AC_UpdateClut(p->acp, p->bk_colp, 1);
        }
        memcpy(p->fn_colp, p->bk_colp, 0xb0);
        p->mode = 0; /* delay slot: before the call */
        __SetEditColSystemFadeOut(ioObjp);
    } else if (r == 0x14) {
        memcpy(p->fn_colp, p->colp, 0xb0);
        p->mode = 0; /* delay slot: before the call */
        __SetEditColSystemFadeOut(ioObjp);
    }
}

/* play_func while a child system (RGB editor / pattern chooser) runs: live colour preview while left / right is
 * held (RGB editor), and when the child is done: drop it, restore the colours if it reported no change, back to
 * the list fade-in (step 1) */
static void __PlayEditColSystemDispatch(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    struct struct_a99b3137 *p = MCOLOR_ECS(ioObjp);
    struct struct_c4910010 *cp;

    switch (p->idx) { /* swatch per part: {1, 0, 1, 2, 3, 4} */
    case 0: cp = &p->colp->p_col[1]; break;
    case 1: cp = &p->colp->p_col[0]; break;
    case 2: cp = &p->colp->p_col[1]; break;
    case 3: cp = &p->colp->p_col[2]; break;
    case 4: cp = &p->colp->p_col[3]; break;
    case 5: cp = &p->colp->p_col[4]; break;
    default: cp = NULL; break;
    }
    if (MPad[MPadUser].lt > 0 || MPad[MPadUser].rt > 0) { /* held counters: every frame while held */
        if (p->mode == 1) {
            if (p->idx == 0) {
                /* whole-AC entry: swatch 1 goes to swatches 0, 2, 3, 4 */
                for (int k = 0; k < 5; k++) {
                    if (k == 1)
                        continue;
                    memcpy(&p->colp->p_col[k], &p->colp->p_col[1], 0x10);
                }
            }
            DF_M_AC_UpdateClut(p->acp, p->colp, 1);
            DF_MOBJ_SetModelColor(p->mdlp, cp);
        }
    }
    /* original: csysp->stat is read before the null test */
    if (p->csysp->stat != 5)
        return;
    mcolor_erase(&p->csysp);
    p->csysp = NULL;
    if (p->f_tmp_modify == 0) {
        memcpy(p->colp, p->tmp_bk_colp, 0xb0);
        DF_M_AC_UpdateClut(p->acp, p->colp, 1);
        if (p->mode == 1) {
            if (p->idx == 0)
                DF_MOBJ_SetModelACColor(p->mdlp, p->colp);
            else
                DF_MOBJ_SetModelColor(p->mdlp, cp);
        }
    }
    p->f_modify = (p->f_modify != 0) ? 1 : (p->f_tmp_modify != 0);
    p->f_tmp_modify = 0;
    ioObjp->count = 0;
    p->step = 1;
    ioObjp->stat = 1;
    ioObjp->play_func = MCOLOR_PLAY(__PlayEditColSystemFadeIn);
}

/* after __SetEditColSystemFadeOut: mode 1 / 2 = 6 frames of ring / model move to the edit view, then (count 15 and
 * the view motion done) the child system; mode 0 = after 15 frames tear the whole screen down */
static void __PlayEditColSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x60);
    struct struct_a99b3137 *p = MCOLOR_ECS(ioObjp);

    ioObjp->count++;
    p->mdl_move_cnt++;
    int n = p->mdl_move_cnt;
    if (p->mode > 0 && n < 7) {
        if (s_pColCube->pMdl[0]) {
            for (int i = 0; i < 5; i++) {
                struct struct_e1bb11f7 *c = s_pColCube;
                DF_MFUNC_CalcInterVector(c->Pos[i], c->PrevPos[i], c->NextPos[i], n, 6);
                c = s_pColCube;
                memcpy(c->pKageMdl[i]->pos, c->Pos[i], 16);
                memcpy(c->pMdlCamo[i]->pos, c->pKageMdl[i]->pos, 16);
                memcpy(c->pMdl[i]->pos, c->pMdlCamo[i]->pos, 16);
                c->pKageMdl[i]->pos[1] = c->pKageMdl[i]->pos[1] + 1.0f;
            }
        }
        DF_MFUNC_CalcInterRect(&p->mdl_rect, &p->mdl_prev_rect, &p->mdl_next_rect, n, 6);
        struct stMobj *m = p->mdlp;
        memcpy(&m->dt.model.show_rect, &p->mdl_rect, 16);
        memcpy(&p->mdlp->dt.model.rect, &m->dt.model.show_rect, 16);
        DF_MFUNC_CalcInterVector(p->mdl_pos, p->mdl_prev_pos, p->mdl_next_pos, n, 6);
        DF_MOBJ_SetModelPos(p->mdlp, p->mdl_pos);
        DF_MFUNC_CalcInterVector(p->mdl_scl, p->mdl_prev_scl, p->mdl_next_scl, n, 6);
        DF_MOBJ_SetModelScale(p->mdlp, p->mdl_scl);
        return;
    }
    if (ioObjp->count < 0xf)
        return;
    if (!DF_MVIEW_MotionSync(MViewp))
        return;
    if (p->mode != 0) {
        if (p->mode == 1)
            p->csysp = __NewEditPartCol(ioObjp, p->over_colp, p, &p->f_tmp_modify);
        else
            p->csysp = __NewSelPartMode(ioObjp, p->over_colp, p->idx, p->player, p, &p->f_tmp_modify, p->acp);
        ioObjp->stat = 3;
        ioObjp->play_func = MCOLOR_PLAY(__PlayEditColSystemDispatch);
        return;
    }
    DF_M_AC_UpdateClut(p->acp, p->fn_colp, 1);
    mcolor_erase(&p->mdlp);
    for (int i = 0; i < 5; i++) {
        DF_MMODEL_DeleteModelAndMMD(s_pColCube->pMdl[i]);
        DF_MMODEL_DeleteModelAndMMD(s_pColCube->pMdlCamo[i]);
        DF_MMODEL_DeleteModelAndMMD(s_pColCube->pKageMdl[i]);
        s_pColCube->pMdl[i] = NULL;
        s_pColCube->pMdlCamo[i] = NULL;
        s_pColCube->pKageMdl[i] = NULL;
    }
    memset(s_pColCube, 0, 0x160);
    for (int i = 0; i < 7; i++)
        WindowEx_Delete(p->obj_wnd[i]);
    for (int i = 0; i < 4; i++)
        UnitSprite_Delete(p->apMobjSprite[i]);
    WindowEx_Delete(p->cursor_wnd);
    if (s_pColCube) { /* original: tested after the memset */
        DF_MMM_Free(2, s_pColCube);
        s_pColCube = NULL;
    }
    DF_MMM_Free(2, p->bk_colp);
    DF_MMM_Free(2, p->tmp_bk_colp);
    DF_MMM_Free(2, p->colp);
    DF_MMM_Free(2, p->fn_colp);
    ioObjp->stat = 5;
    ioObjp->play_func = NULL;
}

/* the part list: decide = leave (dialog first when colours changed), up / down = pick a part (new model, help
 * line, cursor), cancel on an unchanged row = open the RGB editor (mode 1) / pattern chooser (mode 2) */
static void __PlayEditColSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x1b0);
    struct struct_a99b3137 *p = MCOLOR_ECS(ioObjp);
    PS2_STACK(struct stMenuRect, rect, 0x1a0);
    int kind = 1;
    int id = 0;
    int flag = 0;
    struct struct_c4910010 *cp = NULL;

    rect = MCOLOR_MDL_RECT;
    if (MenuKeyGuide_IsShown())
        return;
    if (MPad[MPadUser].start == 1) {
        MenuKeyGuide_Show(p->sel_mode ? 0x1d : 0x1b);
        return;
    }
    int old = p->idx;
    if (MPad[MPadUser].Rrt == 1) {
        if (p->f_modify == 0) {
            DF_M_AC_UpdateClut(p->acp, p->bk_colp, 1);
            Voice_Call(0x3eb, 0x7f);
            p->mode = 0; /* delay slot: before the call */
            __SetEditColSystemFadeOut(ioObjp);
        } else {
            unsigned char *s = DF_MSTR_GetInfo(MenuMng.language, 1, p->sel_mode ? 0x31128 : 0x30d41);
            DF_MDIALOG_Open(2, s, NULL, 0, 0, 0, 1, 0);
            ioObjp->play_func = MCOLOR_PLAY(__PlayEditColSystemDialog);
            ioObjp->stat = 3;
        }
        return;
    }
    if (MPad[MPadUser].repeat.up) {
        Voice_Call(0x3e9, 0x7f);
        p->idx = p->idx - 1;
        if (p->idx < 0)
            p->idx = s_partsTypeNum[(int)p->sel_mode] - 1;
    } else if (MPad[MPadUser].repeat.dw) {
        Voice_Call(0x3e9, 0x7f);
        p->idx = p->idx + 1;
        if (!(p->idx < s_partsTypeNum[(int)p->sel_mode]))
            p->idx = 0;
    }
    switch (p->idx) {
    case 0:
        id = p->player;
        kind = 3;
        cp = &p->colp->p_col[1];
        break;
    case 1:
        id = p->acp->part_id[0];
        cp = &p->colp->p_col[0];
        break;
    case 2:
        id = p->acp->part_id[1];
        cp = &p->colp->p_col[1];
        break;
    case 3:
        if (p->sel_mode == 0) {
            flag = 1;
            id = p->acp->part_id[2];
            cp = &p->colp->p_col[2];
        } else {
            flag = 0;
            id = p->acp->part_id[2];
            cp = &p->colp->p_col[3];
        }
        break;
    case 4:
        if (p->sel_mode == 0) {
            flag = 0;
            id = p->acp->part_id[2];
            cp = &p->colp->p_col[3];
        } else {
            id = p->acp->part_id[5];
            cp = &p->colp->p_col[4];
        }
        break;
    case 5:
        id = p->acp->part_id[5];
        cp = &p->colp->p_col[4];
        break;
    default:
        break;
    }
    s_pColCube->pColor = cp;

    if (p->idx != old) {
        /* the old model's angle (dt.model.ay) is loaded in the delay slot of the null test: PS2 address 0x80
         * when there is no model */
        struct stMobj *m = p->mdlp;
        float ay = m ? m->dt.model.ay : *(const float *)((const char *)PS2_NULL_MEM() + 0x80);
        mcolor_erase(&p->mdlp);

        /* the original's stack arguments 10..12 (pAC, player, inDispFlag) */
        PS2_STACK(unsigned long long, arg10, 0x0);
        PS2_STACK(unsigned long long, arg11, 0x8);
        PS2_STACK(unsigned long long, arg12, 0x10);
        arg10 = (unsigned long long)(long long)(int)PS2PTR(p->acp);
        arg11 = (unsigned long long)(long long)p->player;
        arg12 = 0x40000;
        (void)arg10;
        (void)arg11;
        (void)arg12;
        p->mdlp = DF_MOBJ_NewModelAC(ioObjp, kind, id, flag, &rect, cp, cp, 0x80, ay, p->acp, p->player, 0x40000,
                                     0.0f, 0.0f);
        DF_MOBJ_SetModelPos(p->mdlp, AC_PART_POS[(int)p->sel_mode][p->idx][0]);
        DF_MOBJ_SetModelScale(p->mdlp, AC_PART_SCL[(int)p->sel_mode][p->idx][0]);
        memcpy(p->mdl_pos, AC_PART_POS[(int)p->sel_mode][p->idx][0], 16);
        memcpy(p->mdl_next_pos, p->mdl_pos, 16);
        memcpy(p->mdl_prev_pos, p->mdl_next_pos, 16);
        memcpy(p->mdl_scl, AC_PART_SCL[(int)p->sel_mode][p->idx][0], 16);
        memcpy(p->mdl_next_scl, p->mdl_scl, 16);
        memcpy(p->mdl_prev_scl, p->mdl_next_scl, 16);
        DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, p->idx + s_partsTypeStrId[(int)p->sel_mode]));
        PS2_STACK(char[0x40], buf, 0xa0);
        ps2_sprintf_d(buf, mcolor_fmt_light_cursor, p->idx);
        char *lo = mcolor_layout(0x420);
        WindowEx_MoveToLayout(p->cursor_wnd, lo, buf, 0);
        if (p->sel_mode == 1) {
            int on = p->acp->camouflage[(p->idx == 0) ? 1 : p->idx - 1];
            mcolor_type_alpha_reset(on ? 1.0f : 0.0f);
        }
        return;
    }
    if (MPad[MPadUser].Rdw == 1) {
        Voice_Call(0x3ea, 0x7f);
        memcpy(p->tmp_bk_colp, p->colp, 0xb0);
        p->mode = (char)(p->sel_mode + 1);
        p->over_colp = cp; /* delay slot: before the call */
        __SetEditColSystemFadeOut(ioObjp);
    }
}
