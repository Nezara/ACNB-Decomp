/* src/menu/munit_top_training.cpp - the TRAINING page of the single-player top menu (mtop_rt's pMobjUnitTopTraining).
 * One stMobj whose dt is a struct_9b539272: 22 mobjs built from layout 0x456 by MOBJManager_Create through the
 * file's 2-entry stMobjManager table (18 strings "string%d" = 6 rows x {title, value, unit}, 4 sprites "class%d" =
 * the S/A/B/C class icons) plus two helper mobjs: pMobjLine (DrawLine_Pkt: the 5 separator lines, float fade alpha)
 * and pMobjClass (DrawClass_Draw: per-category class bar + class icon counts from g_gamedata[0].trainingRecord,
 * signed-byte fade alpha). Play runs the slide state machine (phase + stMoveMng + posMove) that shifts everything
 * +-0x130 px; FadeInStart / FadeOutStart set the phase and start the helper fades.
 * The table's pLayoutHeader is rewritten before every MOBJManager_* call, so the hybrid keeps it at its PS2 address
 * (nodwarf munit_top_training_mng).
 * Original quirks kept (ported literally, marked (sic)). */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU 0x1ee33a0..0x1ee3468, main sdata 0x27c278 / 0x27c29c / 0x27c2a0 */
static char munit_top_training_name_string[] = "string%d";
static char munit_top_training_name_class[] = "class%d";
struct stMobjManager munit_top_training_mng[2] = {
    {.pLayoutName = munit_top_training_name_string, .pLayoutHeader = 0, .numContinue = 0x12, .typeMObj = 2,
     .flag = 0, .dummy_c0 = {0}, .exString = {.category = 0, .strid = 0xdbcb8}},
    {.pLayoutName = munit_top_training_name_class, .pLayoutHeader = 0, .numContinue = 4, .typeMObj = 1},
};
char munit_top_training_fmt_shadow[15] = "dummy_shadow%d";
char munit_top_training_fmt_class[14] = "dummy_class%d";
char munit_top_training_fmt_d[3] = "%d";
char munit_top_training_str_12345[6] = "12345";
#endif

#define MUTT_MNG munit_top_training_mng
#define TT(o) ((struct struct_9b539272 *)&(o)->dt)

/* pMobjLine's dt (not in DWARF) */
struct munit_top_training_line {
    struct stMobj *pParent; /* 0x0 */
    float alpha;            /* 0x4 */
    float alphaMax;         /* 0x8 */
};
/* pMobjClass's dt (not in DWARF) */
struct munit_top_training_class {
    struct stMobj *pParent; /* 0x0 */
    signed char alpha;      /* 0x4 */
    signed char alphaMax;   /* 0x5 */
};
#define TT_LINE(o) ((struct munit_top_training_line *)&(o)->dt)
#define TT_CLASS(o) ((struct munit_top_training_class *)&(o)->dt)

static inline void mutt_set_header(void)
{
    MUTT_MNG[0].pLayoutHeader = (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x456);
}

static void __MUnitTopTraining_DrawClass_Draw(struct stMobj *pMobj)
{
    PS2_FRAME(0x150);
    PS2_STACK(struct struct_76b810ec, color, 0x14c);
    PS2_STACK(unsigned long long, regAlpha, 0x140);
    PS2_STACK(int[4], aClassNum, 0x130);
    PS2_STACK(struct _LAYOUT_RET_DATA, layout, 0xe0);
    PS2_STACK(char[32], name, 0xc0);
    PS2_STACK(struct stMenuRect, rect, 0xb0);
    PS2_STACK(int[4], uv, 0xa0);
    struct struct_9b539272 *pWork = TT(TT_CLASS(pMobj)->pParent);
    unsigned int col = 0x00808080;   /* main sdata 0x27c298, copied as raw bits */

    memcpy(&color, &col, 4);
    color.a = TT_CLASS(pMobj)->alpha;
    if (TT_CLASS(pMobj)->alpha == 0) return;

    for (int i = 0x12; i < 0x16; i++) DF_MOBJ_SetDrawEnable(pWork->apMobj[i], 1);

    for (int j = 0; j < 6; j++) {
        struct _training_savedata_st *row = g_gamedata[0].trainingRecord[j];
        if (row[0].MaxLv == 0) continue;
        memset(aClassNum, 0, 0x10);
        int sum = 0;
        for (int i = 0; i < 5; i++) {
            for (int k = 0; k < 5; k++) {
                if (row[i].MaxLv < k + 1) continue;
                aClassNum[GetTrainingTrendLv(j, i, k)->prize]++;   /* (sic) unchecked index */
                sum++;
            }
        }

        ps2_sprintf_d(name, munit_top_training_fmt_shadow, j);
        if (!DF_LO_GetLayoutData((unsigned char *)LAYOUT_GetLayoutData(0x456), (unsigned char *)name, 0, &layout))
            return;   /* (sic) skips the disable loop */
        int len = (sum - 1) * 11;
        layout.Pos.x = layout.Pos.x + pWork->posMove;
        regAlpha = 0x48;

        rect.x = layout.Pos.x;
        rect.y = layout.Pos.y;
        rect.w = 6;
        rect.h = 4;
        uv[0] = 0x60;
        uv[1] = 0;
        uv[2] = 0x66;
        uv[3] = 4;
        DF_MDRAW_DrawTexSpriteIL(0x517, &rect, uv, &color, 1, &regAlpha);
        rect.x = layout.Pos.x + 6;
        rect.y = layout.Pos.y;
        rect.w = len;
        rect.h = 4;
        uv[0] = 0x66;
        uv[1] = 0;
        uv[2] = 0x70;
        uv[3] = 4;
        DF_MDRAW_DrawTexSpriteIL(0x517, &rect, uv, &color, 1, &regAlpha);
        rect.x = layout.Pos.x + 6 + len;
        rect.y = layout.Pos.y;
        rect.w = 6;
        rect.h = 4;
        uv[0] = 0x70;
        uv[1] = 0;
        uv[2] = 0x76;
        uv[3] = 4;
        DF_MDRAW_DrawTexSpriteIL(0x517, &rect, uv, &color, 1, &regAlpha);

        ps2_sprintf_d(name, munit_top_training_fmt_class, j);
        if (!DF_LO_GetLayoutData((unsigned char *)LAYOUT_GetLayoutData(0x456), (unsigned char *)name, 0, &layout))
            return;   /* (sic) skips the disable loop */
        layout.Pos.x = layout.Pos.x + pWork->posMove;
        for (int c = 0; c < 3; c++) {
            for (int k = 0; k < aClassNum[c + 1]; k++) {
                UnitSprite_SetPosition(pWork->apMobj[20 - c], (struct struct_214acd28 *)&layout.Pos);
                UnitSprite_Draw(pWork->apMobj[20 - c]);
                layout.Pos.x += 11;
            }
        }
        for (int k = 0; k < aClassNum[0]; k++) {
            UnitSprite_SetPosition(pWork->apMobj[21], (struct struct_214acd28 *)&layout.Pos);
            UnitSprite_Draw(pWork->apMobj[21]);
            layout.Pos.x += 11;
        }
    }

    for (int i = 0x12; i < 0x16; i++) DF_MOBJ_SetDrawEnable(pWork->apMobj[i], 0);
}

static void __MUnitTopTraining_DrawLine_Pkt(struct stMobj *pMobj)
{
    PS2_FRAME(0xd0);
    PS2_STACK(struct struct_76b810ec[2], aColor, 0xc8);
    PS2_STACK(int[2], aPosX, 0xc0);
    PS2_STACK(int[5], aPosY, 0xa0);
    PS2_STACK(int[2][4], aPos, 0x80);
    struct struct_9b539272 *pWork = TT(TT_LINE(pMobj)->pParent);
    unsigned long long col = 0x004e4e44004e4e44ull;   /* main sdata 0x27c288 */
    float f;
    unsigned int a;

    memcpy(aColor, &col, 8);
    aPosX[0] = 0x14c;   /* main sdata 0x27c290 */
    aPosX[1] = 0x26b;
    aPosY[0] = 0x6a;    /* OLMENU 0x1ee3430 */
    aPosY[1] = 0xa4;
    aPosY[2] = 0xde;
    aPosY[3] = 0x118;
    aPosY[4] = 0x152;
    f = TT_LINE(pMobj)->alpha;
    if (2147483648.0f <= f) a = (unsigned int)ps2_ftoi(f - 2147483648.0f) | 0x80000000u;
    else a = (unsigned int)ps2_ftoi(f);
    aColor[1].a = (unsigned char)a;
    aColor[0].a = (unsigned char)a;
    for (unsigned int i = 0; i < 5; i++) {
        memset(aPos, 0, 0x20);
        aPos[0][0] = aPosX[0] + pWork->posMove;
        aPos[0][1] = aPosY[i];
        aPos[1][0] = aPosX[1] + pWork->posMove;
        aPos[1][1] = aPosY[i];
        DF_MDRAW_DrawPrim(2, 2, aPos, aColor, 1.0f, 0);
    }
}

static void __MUnitTopTraining_DrawClass_Play(struct stMobj *pMobj)
{
    struct munit_top_training_class *dt = TT_CLASS(pMobj);

    if (pMobj->stat == 2) {
        int cnt = pMobj->count;
        if (cnt >= 7) {
            pMobj->stat = 5;
            pMobj->play_func = 0;
        } else {
            dt->alpha = (signed char)ps2_ftoi((float)dt->alphaMax * (1.0f - (float)cnt / 6.0f));
        }
    } else if (pMobj->stat == 1) {
        int cnt = pMobj->count;
        if (cnt >= 7) {
            pMobj->stat = 0;
            pMobj->play_func = 0;
        } else {
            dt->alpha = (signed char)ps2_ftoi((float)dt->alphaMax * ((float)cnt / 6.0f));
        }
    }
    pMobj->count++;
}

static void __MUnitTopTraining_DrawLine_Play(struct stMobj *pMobj)
{
    struct munit_top_training_line *dt = TT_LINE(pMobj);

    if (pMobj->stat == 2) {
        float cur = dt->alpha;
        if (cur <= 0.0f) {
            dt->alpha = 0.0f;
            pMobj->stat = 0;
            pMobj->play_func = 0;
        } else {
            dt->alpha = cur - dt->alphaMax / 6.0f;
        }
    } else if (pMobj->stat == 1) {
        float max = dt->alphaMax;
        float cur = dt->alpha;
        if (max < cur) {
            dt->alpha = max;
            pMobj->stat = 0;
            pMobj->play_func = 0;
        } else {
            dt->alpha = cur + max / 6.0f;
        }
    }
}

static void __MUnitTopTraining_FadeOutItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_9b539272 *pWork = TT(pMobj);

    mutt_set_header();
    MOBJManager_FadeOut(&pWork->apMobj[0], 0, 2, MUTT_MNG);
    pWork->pMobjLine->play_func = (void (*)(void))GFN(__MUnitTopTraining_DrawLine_Play);
    pWork->pMobjLine->stat = 2;
    pWork->pMobjLine->count = 0;
    pWork->pMobjClass->play_func = (void (*)(void))GFN(__MUnitTopTraining_DrawClass_Play);
    pWork->pMobjClass->stat = 2;
    pWork->pMobjClass->count = 0;
    for (int i = 0x12; i < 0x16; i++) UnitSprite_SetFadeOut(pWork->apMobj[i]);
}

static void __MUnitTopTraining_FadeInItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x60);
    PS2_STACK(char[16], buff, 0x50);
    struct struct_9b539272 *pWork = TT(pMobj);

    mutt_set_header();
    MOBJManager_FadeIn(&pWork->apMobj[0], 0, 2, MUTT_MNG);
    for (int i = 0x12; i < 0x16; i++) DF_MOBJ_SetDrawEnable(pWork->apMobj[i], 0);
    for (int j = 0; j < 6; j++) {
        if (g_gamedata[0].trainingRecord[j][0].MaxLv == 0) {
            DF_MOBJ_SetDrawEnable(pWork->apMobj[3 * j], 0);
            DF_MOBJ_SetDrawEnable(pWork->apMobj[3 * j + 1], 0);
            DF_MOBJ_SetDrawEnable(pWork->apMobj[3 * j + 2], 0);
        }
    }
    for (int j = 0; j < 6; j++) {
        struct _training_savedata_st *row = g_gamedata[0].trainingRecord[j];
        unsigned int sum = 0 + row[0].point + row[1].point + row[2].point + row[3].point + row[4].point;
        if (sum == 0) {
            DF_MOBJ_SetDrawEnable(pWork->apMobj[3 * j + 1], 0);
            DF_MOBJ_SetDrawEnable(pWork->apMobj[3 * j + 2], 0);
        } else {
            ps2_sprintf_d(buff, munit_top_training_fmt_d, (int)sum);
            DF_MOBJ_SetStringCopy(pWork->apMobj[3 * j + 1], buff);
        }
    }
    pWork->pMobjLine->play_func = (void (*)(void))GFN(__MUnitTopTraining_DrawLine_Play);
    pWork->pMobjLine->stat = 1;
    pWork->pMobjLine->count = 0;
    pWork->pMobjClass->play_func = (void (*)(void))GFN(__MUnitTopTraining_DrawClass_Play);
    pWork->pMobjClass->stat = 1;
    pWork->pMobjClass->count = 0;
    for (int i = 0x12; i < 0x16; i++) UnitSprite_SetFadeIn(pWork->apMobj[i]);
    pWork->pMobjClass->draw_func = (void (*)(void))GFN(__MUnitTopTraining_DrawClass_Draw);
}

static void __MUnitTopTraining_CreateItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x70);
    PS2_STACK(struct struct_76b810ec, color, 0x6c);
    PS2_STACK(char[16], buff, 0x50);
    struct struct_9b539272 *pWork = TT(pMobj);
    unsigned int col = 0x8061a5de;   /* main sdata 0x27c280, raw bits (a denormal as float) */

    memcpy(&color, &col, 4);
    mutt_set_header();
    MOBJManager_Create(pMobj, &pWork->apMobj[0], 2, MUTT_MNG);
    for (int j = 0; j < 6; j++) pWork->apMobj[3 * j + 1]->dt.string.flag |= 0x10000;
    for (int j = 0; j < 6; j++) DF_MOBJ_SetStringColorAll(pWork->apMobj[3 * j], &color);
    for (int i = 0; i < 6; i++) {
        pWork->apMobj[3 * i]->dt.string.id = 0xdbbb4 + i;
        pWork->apMobj[3 * i + 2]->dt.string.id = 0xdbbaf;   /* (sic) same unit string for every row */
        ps2_sprintf_d(buff, munit_top_training_str_12345, 0);   /* no conversion: copies "12345" */
        DF_MOBJ_SetStringCopy(pWork->apMobj[3 * i + 1], buff);
    }

    pWork->pMobjLine = DF_MOBJ_GetNewObject(pMobj);
    if (pWork->pMobjLine == 0) return;
    struct munit_top_training_line *line = TT_LINE(pWork->pMobjLine);
    memset(line, 0, 0xc);
    line->pParent = pMobj;
    line->alpha = 0.0f;
    line->alphaMax = 32.0f;
    pWork->pMobjLine->draw_func = (void (*)(void))GFN(__MUnitTopTraining_DrawLine_Pkt);

    pWork->pMobjClass = DF_MOBJ_GetNewObject(pMobj);
    if (pWork->pMobjClass == 0) return;
    pWork->pMobjClass->type = 0;
    pWork->pMobjClass->stat = 1;
    pWork->pMobjClass->count = 0;
    pWork->pMobjClass->play_func = 0;
    pWork->pMobjClass->pkt_func = 0;
    struct munit_top_training_class *cls = TT_CLASS(pWork->pMobjClass);
    cls->pParent = pMobj;
    cls->alpha = 0;
    cls->alphaMax = 0x40;
    for (int i = 0x12; i < 0x16; i++) pWork->apMobj[i]->draw_func = 0;
}

static void __MUnitTopTraining_Pkt(void)
{
}

static void __MUnitTopTraining_Draw(void)
{
}

/* one slide step towards dx; the -99999 sentinel ends the slide */
static void mutt_slide(struct struct_9b539272 *pWork, int dx)
{
    int v = MoveMng_GetNextMoveValue(&pWork->stMoveMng, dx, 8, 1);
    if (v == -99999) {
        pWork->phase = 0;
    } else {
        mutt_set_header();
        pWork->posMove += v;
        MOBJManager_Move(MUTT_MNG, 2, &pWork->apMobj[0], v, 0, 0);
    }
}

/* MoveMng_Init + ResetPos (posMove = 0) */
static void mutt_reset(struct struct_9b539272 *pWork)
{
    MoveMng_Init(&pWork->stMoveMng);
    mutt_set_header();
    pWork->posMove = 0;
    MOBJManager_ResetPos(MUTT_MNG, 2, &pWork->apMobj[0]);
}

static void mutt_move(struct struct_9b539272 *pWork, int dx)
{
    mutt_set_header();
    pWork->posMove += dx;
    MOBJManager_Move(MUTT_MNG, 2, &pWork->apMobj[0], dx, 0, 0);
}

static void __MUnitTopTraining_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct struct_9b539272 *pWork = TT(pMobj);

    if (pWork->phase == 0) return;
    if (pWork->phase == 1) {
        mutt_reset(pWork);
        mutt_move(pWork, 0x130);
        pWork->phase = 2;
    }
    if (pWork->phase == 2) mutt_slide(pWork, -0x130);
    if (pWork->phase == 5) {
        mutt_reset(pWork);
        mutt_move(pWork, -0x130);
        pWork->phase = 6;
    }
    if (pWork->phase == 6) mutt_slide(pWork, 0x130);
    if (pWork->phase == 9) {
        mutt_reset(pWork);
        pWork->phase = 0xa;
    }
    if (pWork->phase == 0xa) mutt_slide(pWork, -0x130);
    if (pWork->phase == 0xd) {
        mutt_reset(pWork);
        pWork->phase = 0xe;
    }
    if (pWork->phase == 0xe) mutt_slide(pWork, 0x130);
    if (pWork->phase == 0x11) pWork->phase = 0;
    if (pWork->phase == 0x12) pWork->phase = 0;
}

int MUnitTopTraining_IsMove(struct stMobj *pMobj)
{
    return TT(pMobj)->phase != 0;
}

void MUnitTopTraining_FadeOutStart(struct stMobj *pMobj, char phase)
{
    TT(pMobj)->phase = phase;
    __MUnitTopTraining_FadeOutItem(pMobj);   /* tail j: no frame of its own */
}

void MUnitTopTraining_FadeInStart(struct stMobj *pMobj, char phase)
{
    TT(pMobj)->phase = phase;
    __MUnitTopTraining_FadeInItem(pMobj);    /* tail j: no frame of its own */
}

void MUnitTopTraining_Cleanup(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    struct struct_9b539272 *pWork = TT(pMobj);

    mutt_set_header();
    MOBJManager_Delete(&pWork->apMobj[0], 2, MUTT_MNG);
    if (pWork->pMobjLine) {
        pWork->pMobjLine->f_draw = 0;
        pWork->pMobjLine->f_erase = 1;
        pWork->pMobjLine = 0;
    }
    if (pWork->pMobjClass) {
        pWork->pMobjClass->f_draw = 0;
        pWork->pMobjClass->f_erase = 1;
        pWork->pMobjClass = 0;
    }
}

struct stMobj *MUnitTopTraining_NewSystem(struct stMobj *pParentMobj)
{
    PS2_FRAME(0x20);
    struct stMobj *obj = DF_MOBJ_GetNewObject(pParentMobj);

    if (obj == 0) return 0;
    memset(&obj->dt, 0, 0x7c);
    obj->type = 0;
    obj->stat = 1;
    obj->count = 0;
    obj->play_func = (void (*)(void))GFN(__MUnitTopTraining_Play);
    obj->draw_func = (void (*)(void))GFN(__MUnitTopTraining_Draw);
    obj->pkt_func = (void (*)(void))GFN(__MUnitTopTraining_Pkt);
    __MUnitTopTraining_CreateItem(obj);
    return obj;
}
