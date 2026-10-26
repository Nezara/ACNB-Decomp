/* src/menu/munit_ez_match.cpp - the "MATCH" tab preview of the ARENA top page (marena_top.c cursor 0): the 18-tile
 * element board (same 5x5 grid as the search page, g_aIndexToElementTable) as shadow + tile sprites, 5 strings,
 * 3 "null" cover sprites (one per style, aStyle) drawn over the tiles the player has unlocked, and 3 horizontal guide
 * lines. A passive page: MArenaTop creates it (NewSystem), slides it in / out (FadeInStart / FadeOutStart with a
 * phase) and deletes it (Cleanup); Play only runs the slide state machine (posMove, MoveMng ease, 0x130 px).
 * The work block (struct_ae77747c) is INLINE in pMobj->dt (not a pointer, unlike munit_arena_search). Its 44 sprite /
 * string mobjs come from the file's 4-record stMobjManager table (layout 0x44b), plus two plain child mobjs:
 * pMobjNullIta (cover sprites) and pMobjLine (guide lines, alpha-faded by DrawLine_Play).
 * The table's record 0 pLayoutHeader is rewritten before every MOBJManager_* call, so the hybrid keeps the table at
 * its PS2 address (nodwarf munit_ez_match_mng); the sprintf format is a nodwarf PS2 string.
 * Original quirks kept (ported literally, marked (sic)). */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

#ifdef ACNB_SOURCEPORT
/* main sdata 0x27c000 / 0x27c008, OLMENU 0x1ee1d58 / 0x1ee1d68: the table's layout names */
static char munit_ez_match_name_kage[] = "kage%d";
static char munit_ez_match_name_null[] = "null_%d";
static char munit_ez_match_name_string[] = "string%d";
char munit_ez_match_fmt_element[16] = "element%d";
/* OLMENU 0x1ee1d80 (4 x 0x40) */
struct stMobjManager munit_ez_match_mng[4] = {
    {.pLayoutName = munit_ez_match_name_kage, .pLayoutHeader = 0, .numContinue = 18, .typeMObj = 1},
    {.pLayoutName = munit_ez_match_fmt_element, .pLayoutHeader = 0, .numContinue = 18, .typeMObj = 1},
    {.pLayoutName = munit_ez_match_name_string, .pLayoutHeader = 0, .numContinue = 5, .typeMObj = 2,
     .flag = 0, .dummy_c0 = {0}, .exString = {.category = 0, .strid = 0xdbda8}},
    {.pLayoutName = munit_ez_match_name_null, .pLayoutHeader = 0, .numContinue = 3, .typeMObj = 1},
};
#endif

#define MUEM_MNG munit_ez_match_mng
#define MUEM(o) ((struct struct_ae77747c *)&(o)->dt)
#define MUEM_NULLITA(o) ((struct struct_3ca1b328 *)&(o)->dt)
#define MUEM_LINE(o) ((struct struct_519b1c81 *)&(o)->dt)
/* the work block of a child mobj's parent unit */
#define MUEM_PARENT(o) MUEM(MUEM_NULLITA(o)->pParent)

/* tile index -> cover-sprite style (apMobj[41 + style]); the original copies it to the stack in each user */
static const char muem_aStyle[18] = {1, 1, 1, 0, 0, 0, 0, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1};

static inline void muem_set_header(void)
{
    MUEM_MNG[0].pLayoutHeader = (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x44b);
}

/* f_draw = 0, f_erase = 1 (two read-modify-writes of byte 0) */
static inline void muem_erase(struct stMobj *o)
{
    o->f_draw = 0;
    o->f_erase = 1;
}

/* line mobj draw_func (despite the name): the 3 guide lines at the current alpha, shifted by posMove */
static void __MUnitEzMatch_DrawLine_Pkt(struct stMobj *pMobj)
{
    PS2_FRAME(0xc0);
    struct struct_ae77747c *w = MUEM_PARENT(pMobj);
    struct struct_76b810ec aColor[2] = {{0x44, 0x4e, 0x4e, 0}, {0x44, 0x4e, 0x4e, 0}};
    int aPosX[2] = {0x14c, 0x262};
    int aPosY[3] = {0xbc, 0x10c, 0x144};
    int aPos[2][4];
    unsigned int i;

    aColor[1].a = MUEM_LINE(pMobj)->alpha;
    aColor[0].a = MUEM_LINE(pMobj)->alpha;
    for (i = 0; i < 3; i++) {
        memset(aPos, 0, sizeof(aPos));
        aPos[0][0] = aPosX[0] + w->posMove;
        aPos[0][1] = aPosY[i];
        aPos[1][0] = aPosX[1] + w->posMove;
        aPos[1][1] = aPosY[i];
        if (i == 0) aPos[1][0] = aPos[1][0] + 9;   /* (sic) the top line is 9 px longer */
        DF_MDRAW_DrawPrim(2, 2, aPos, aColor, 1.0f, 0);
    }
}

/* line mobj play_func while fading: stat 1 = fade in, stat 2 = fade out, 7 frames each (count 0..6) */
static void __MUnitEzMatch_DrawLine_Play(struct stMobj *pMobj)
{
    struct struct_519b1c81 *l = MUEM_LINE(pMobj);

    if (pMobj->stat == 2) {
        if (pMobj->count >= 7) {
            pMobj->stat = 5;   /* (sic) fade-out ends in stat 5 */
            pMobj->play_func = 0;
        } else {
            float q = ps2_div((float)pMobj->count, 6.0f);
            l->alpha = (char)ps2_ftoi((float)l->alpha_max * (1.0f - q));
        }
    } else if (pMobj->stat == 1) {
        if (pMobj->count >= 7) {
            pMobj->stat = 0;
            pMobj->play_func = 0;
        } else {
            float q = ps2_div((float)pMobj->count, 6.0f);
            l->alpha = (char)ps2_ftoi((float)l->alpha_max * q);
        }
    }
    pMobj->count = pMobj->count + 1;
}

/* null-ita mobj draw_func: the style's cover sprite drawn over every tile whose flag bit is clear */
static void __MUnitEzMatch_NullIta_Draw(struct stMobj *pMobj)
{
    PS2_FRAME(0x110);
    PS2_STACK(char[32], name, 0xa0);
    struct struct_ae77747c *w = MUEM_PARENT(pMobj);
    struct _LAYOUT_RET_DATA layout;
    int x, y;

    for (y = 0; y < 5; y++) {
        char *pFlagCan = (char *)&w->flagSearchCan + y;
        for (x = 0; x < 5; x++) {
            int index = g_aIndexToElementTable[y][x];
            if (index < 0) continue;
            if ((pFlagCan[0] >> x) & 1) continue;
            ps2_sprintf_d(name, munit_ez_match_fmt_element, index);
            /* (sic) a failed lookup leaves both loops and the function */
            if (!DF_LO_GetLayoutData((unsigned char *)LAYOUT_GetLayoutData(0x44b), (unsigned char *)name, 0, &layout))
                return;
            layout.Pos.x = layout.Pos.x + w->posMove;
            struct stMobj **pp = &w->apMobj[41 + muem_aStyle[index]];
            UnitSprite_SetPosition(*pp, (struct struct_214acd28 *)&layout.Pos);
            UnitSprite_Draw(*pp);
        }
    }
}

/* pMobj = the null-ita mobj: fade out the cover sprites of the styles in use, hide the others */
static void __MUnitEzMatch_NullIta_FadeOut(struct stMobj *pMobj)
{
    PS2_FRAME(0x60);
    struct struct_ae77747c *w = MUEM_PARENT(pMobj);
    char aDoFadeOut[3];
    int x, y, j;

    memset(aDoFadeOut, 0, sizeof(aDoFadeOut));
    for (y = 0; y < 5; y++) {
        for (x = 0; x < 5; x++) {
            int index = g_aIndexToElementTable[y][x];
            if (index >= 0 && !((((char *)&w->flagSearchCan)[y] >> x) & 1)) {
                int s = muem_aStyle[index];
                aDoFadeOut[s] = (char)(aDoFadeOut[s] + 1);
            }
        }
    }
    for (j = 0; j < 3; j++) {
        if (aDoFadeOut[j]) UnitSprite_SetFadeOut(w->apMobj[41 + j]);
        else DF_MOBJ_SetDrawEnable(w->apMobj[41 + j], 0);
    }
}

/* pMobj = the null-ita mobj: hide the tile sprites under the covers, fade the used cover sprites in */
static void __MUnitEzMatch_NullIta_FadeIn(struct stMobj *pMobj)
{
    PS2_FRAME(0x90);
    struct struct_ae77747c *w = MUEM_PARENT(pMobj);
    char aDoFadeIn[3];
    int x, y, j;

    memset(aDoFadeIn, 0, sizeof(aDoFadeIn));
    for (y = 0; y < 5; y++) {
        for (x = 0; x < 5; x++) {
            int index = g_aIndexToElementTable[y][x];
            if (index >= 0 && !((((char *)&w->flagSearchCan)[y] >> x) & 1)) {
                int s = muem_aStyle[index];
                aDoFadeIn[s] = (char)(aDoFadeIn[s] + 1);
                DF_MOBJ_SetDrawEnable(w->apMobj[18 + index], 0);
            }
        }
    }
    for (j = 0; j < 3; j++) {
        if (aDoFadeIn[j]) {
            DF_MOBJ_SetDrawEnable(w->apMobj[41 + j], 1);
            UnitSprite_SetFadeIn(w->apMobj[41 + j]);
        } else {
            DF_MOBJ_SetDrawEnable(w->apMobj[41 + j], 0);
        }
    }
}

static void __MUnitEzMatch_CreateItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_ae77747c *w = MUEM(pMobj);
    struct stMobj *o;

    muem_set_header();
    MOBJManager_Create(pMobj, &w->apMobj[0], 4, MUEM_MNG);

    w->pMobjNullIta = DF_MOBJ_GetNewObject(pMobj);
    if (w->pMobjNullIta == 0) return;
    memset(MUEM_NULLITA(w->pMobjNullIta), 0, 4);
    MUEM_NULLITA(w->pMobjNullIta)->pParent = pMobj;
    w->pMobjNullIta->draw_func = (void (*)(void))GFN(__MUnitEzMatch_NullIta_Draw);

    w->pMobjLine = DF_MOBJ_GetNewObject(pMobj);
    o = w->pMobjLine;
    if (o == 0) return;
    memset(MUEM_LINE(o), 0, 8);
    MUEM_LINE(o)->pParent = pMobj;
    MUEM_LINE(o)->alpha = 0;
    MUEM_LINE(o)->alpha_max = 0x20;
    w->pMobjLine->draw_func = (void (*)(void))GFN(__MUnitEzMatch_DrawLine_Pkt);
}

static void __MUnitEzMatch_Pkt(void)
{
}

static void __MUnitEzMatch_Draw(void)
{
}

/* the page slide: (sic) sequential ifs, so 1 -> 2, 5 -> 6, 9 -> 0xa, 0xd -> 0xe run in the same call;
 * (sic) the sentinel arms of 2 / 6 only clear the phase, those of 0xa / 0xe also reset the positions */
static void __MUnitEzMatch_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct struct_ae77747c *w = MUEM(pMobj);
    int v;

    if (w->phase == 0) return;
    muem_set_header();

    if (w->phase == 1) {
        MoveMng_Init(&w->stMoveMng);
        muem_set_header();
        MOBJManager_ResetPos(MUEM_MNG, 4, &w->apMobj[0]);
        w->posMove = 0;
        muem_set_header();
        MOBJManager_Move(MUEM_MNG, 4, &w->apMobj[0], 0x130, 0, 0);
        w->posMove = w->posMove + 0x130;
        w->phase = 2;
    }
    if (w->phase == 2) {
        v = MoveMng_GetNextMoveValue(&w->stMoveMng, -0x130, 8, 1);
        if (v == -99999) {
            w->phase = 0;
        } else {
            muem_set_header();
            MOBJManager_Move(MUEM_MNG, 4, &w->apMobj[0], v, 0, 0);
            w->posMove = w->posMove + v;
        }
    }
    if (w->phase == 5) {
        MoveMng_Init(&w->stMoveMng);
        muem_set_header();
        MOBJManager_ResetPos(MUEM_MNG, 4, &w->apMobj[0]);
        w->posMove = 0;
        muem_set_header();
        MOBJManager_Move(MUEM_MNG, 4, &w->apMobj[0], -0x130, 0, 0);
        w->posMove = w->posMove - 0x130;
        w->phase = 6;
    }
    if (w->phase == 6) {
        v = MoveMng_GetNextMoveValue(&w->stMoveMng, 0x130, 8, 1);
        if (v == -99999) {
            w->phase = 0;
        } else {
            muem_set_header();
            MOBJManager_Move(MUEM_MNG, 4, &w->apMobj[0], v, 0, 0);
            w->posMove = w->posMove + v;
        }
    }
    if (w->phase == 9) {
        MoveMng_Init(&w->stMoveMng);
        muem_set_header();
        MOBJManager_ResetPos(MUEM_MNG, 4, &w->apMobj[0]);
        w->posMove = 0;
        w->phase = 0xa;
    }
    if (w->phase == 0xa) {
        v = MoveMng_GetNextMoveValue(&w->stMoveMng, -0x130, 8, 1);
        if (v == -99999) {
            muem_set_header();
            MOBJManager_ResetPos(MUEM_MNG, 4, &w->apMobj[0]);
            w->posMove = 0;
            w->phase = 0;
        } else {
            muem_set_header();
            MOBJManager_Move(MUEM_MNG, 4, &w->apMobj[0], v, 0, 0);
            w->posMove = w->posMove + v;
        }
    }
    if (w->phase == 0xd) {
        MoveMng_Init(&w->stMoveMng);
        muem_set_header();
        MOBJManager_ResetPos(MUEM_MNG, 4, &w->apMobj[0]);
        w->posMove = 0;
        w->phase = 0xe;
    }
    if (w->phase == 0xe) {
        v = MoveMng_GetNextMoveValue(&w->stMoveMng, 0x130, 8, 1);
        if (v == -99999) {
            muem_set_header();
            MOBJManager_ResetPos(MUEM_MNG, 4, &w->apMobj[0]);
            w->posMove = 0;
            w->phase = 0;
        } else {
            muem_set_header();
            MOBJManager_Move(MUEM_MNG, 4, &w->apMobj[0], v, 0, 0);
            w->posMove = w->posMove + v;
        }
    }
    if (w->phase == 0x11) w->phase = 0;
    if (w->phase == 0x12) w->phase = 0;
}

void MUnitEzMatch_FadeOutStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x20);
    struct struct_ae77747c *w = MUEM(pMobj);

    w->phase = phase;
    muem_set_header();
    MOBJManager_FadeOut(&w->apMobj[0], 0, 4, MUEM_MNG);
    w->pMobjLine->play_func = (void (*)(void))GFN(__MUnitEzMatch_DrawLine_Play);
    w->pMobjLine->stat = 2;
    w->pMobjLine->count = 0;
    __MUnitEzMatch_NullIta_FadeOut(w->pMobjNullIta);
}

void MUnitEzMatch_FadeInStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x40);
    struct struct_ae77747c *w = MUEM(pMobj);
    int i;

    w->phase = phase;
    muem_set_header();
    MOBJManager_FadeIn(&w->apMobj[0], 0, 4, MUEM_MNG);
    for (i = 41; i < 44; i++) {   /* the cover sprites are drawn by NullIta_Draw only */
        DF_MOBJ_SetDrawEnable(w->apMobj[i], 0);
        w->apMobj[i]->draw_func = 0;
    }
    w->pMobjLine->play_func = (void (*)(void))GFN(__MUnitEzMatch_DrawLine_Play);
    w->pMobjLine->stat = 1;
    w->pMobjLine->count = 0;
    __MUnitEzMatch_NullIta_FadeIn(w->pMobjNullIta);
}

void MUnitEzMatch_Cleanup(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    struct struct_ae77747c *w = MUEM(pMobj);

    muem_set_header();
    MOBJManager_Delete(&w->apMobj[0], 4, MUEM_MNG);
    if (w->pMobjNullIta != 0) {
        muem_erase(w->pMobjNullIta);
        w->pMobjNullIta = 0;
    }
    if (w->pMobjLine != 0) {
        muem_erase(w->pMobjLine);
        w->pMobjLine = 0;
    }
}

struct stMobj *MUnitEzMatch_NewSystem(struct stMobj *pParentMobj)
{
    PS2_FRAME(0x30);
    struct stMobj *m = DF_MOBJ_GetNewObject(pParentMobj);
    struct struct_ae77747c *w;

    if (m == 0) return 0;
    w = MUEM(m);
    memset(w, 0, 0xe4);
    memcpy(&w->flagSearchCan, &g_gamedata[0].asmSearchFlag, 0x10);   /* 8 halfword copies in the original */
    m->type = 0;
    m->stat = 1;
    m->count = 0;
    m->play_func = (void (*)(void))GFN(__MUnitEzMatch_Play);
    m->draw_func = (void (*)(void))GFN(__MUnitEzMatch_Draw);
    m->pkt_func = (void (*)(void))GFN(__MUnitEzMatch_Pkt);
    __MUnitEzMatch_CreateItem(m);
    return m;
}
