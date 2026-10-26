/* src/menu/munit_ez_official.cpp - the "OFFICIAL" tab preview of the ARENA top page (marena_top.c cursor 1): two
 * ranker cards (apMobjCard[2], each a child mobj holding 36 layout-managed sprite / string mobjs) and one guide line.
 * A passive page like munit_ez_match / munit_ez_ranking: MArenaTop creates it (NewSystem), slides it with
 * FadeInStart / FadeOutStart and deletes it (Cleanup); Play drives the slide state machine for both cards at once.
 * The work block (struct_bba7b090) is INLINE in pMobj->dt; each card's dt is a struct_c42eefd4 (pParent + 36 mobjs
 * from the file's 4-record stMobjManager table, layout 0x44c). The table's record 0 pLayoutHeader is rewritten
 * before every manager call, so the hybrid keeps the table at its PS2 address (nodwarf munit_ez_official_mng).
 * Card_ResetData fills a card from g_gamedata[0].officialBattle[(int)index] (class letter or rank + suffix, leg / weight
 * / range icons, NEW mark, pilot name, user points; "--" placeholders for an empty card); Card_Reset re-places a
 * card's icon rows from the "dummy_element%d" layout entries (card 1 is 146 px lower) and hides them.
 * UNREACHED by every replay (the OFFICIAL tab needs an official card): Play's arms past the phase-0 test,
 * Card_ResetData, Card_Reset, DrawLine_Play, FadeInStart, FadeOutStart are asm-verified only (REV).
 * Original quirks kept (ported literally, marked (sic)). */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

#ifdef ACNB_SOURCEPORT
/* main sdata 0x27c020 / 0x27c028, OLMENU 0x1ee1ee8 / 0x1ee1ef8: the table's layout names */
static char munit_ez_official_name_kage[] = "kage%d";
static char munit_ez_official_name_string[] = "string%d";
static char munit_ez_official_name_element[] = "element%d";
static char munit_ez_official_name_new[] = "new";
/* OLMENU 0x1ee1f10 (4 x 0x40) */
struct stMobjManager munit_ez_official_mng[4] = {
    {.pLayoutName = munit_ez_official_name_kage, .pLayoutHeader = 0, .numContinue = 4, .typeMObj = 1},
    {.pLayoutName = munit_ez_official_name_string, .pLayoutHeader = 0, .numContinue = 13, .typeMObj = 2,
     .flag = 0, .dummy_c0 = {0}, .exString = {.category = 0, .strid = 0xdbdb2}},
    {.pLayoutName = munit_ez_official_name_element, .pLayoutHeader = 0, .numContinue = 18, .typeMObj = 1},
    {.pLayoutName = munit_ez_official_name_new, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 1},
};
/* main sdata 0x27c048..0x27c06d, OLMENU 0x1ee2030 / 0x1ee2040: the Card_* sprintf formats */
char munit_ez_official_fmt_d[3] = "%d";
char munit_ez_official_str_st[3] = "ST";
char munit_ez_official_str_nd[3] = "ND";
char munit_ez_official_str_rd[3] = "RD";
char munit_ez_official_str_th[3] = "TH";
char munit_ez_official_fmt_s[3] = "%s";
char munit_ez_official_str_dash2[3] = "--";
char munit_ez_official_str_dash5[6] = "-----";
char munit_ez_official_str_dash8[9] = "--------";
char munit_ez_official_fmt_dummy_element[16] = "dummy_element%d";
#endif

#define MUEO_MNG munit_ez_official_mng
#define MUEO(o) ((struct struct_bba7b090 *)&(o)->dt)
#define MUEO_CARD(o) ((struct struct_c42eefd4 *)&(o)->dt)
#define MUEO_LINE(o) ((struct struct_519b1c81 *)&(o)->dt)
#define MUEO_STRDT(o) ((struct struct_d2c56d2a *)&(o)->dt)

static inline void mueo_set_header(void)
{
    MUEO_MNG[0].pLayoutHeader = (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x44c);
}

/* f_draw = 0, f_erase = 1 (two read-modify-writes of byte 0) */
static inline void mueo_erase(struct stMobj *o)
{
    o->f_draw = 0;
    o->f_erase = 1;
}

/* line mobj draw_func (despite the name): the guide line (y 234) at the current alpha, shifted by posMove */
static void __MUnitEzOfficial_DrawLine_Pkt(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct stMobj *parent = MUEO_LINE(pMobj)->pParent;
    struct struct_76b810ec aColor[2] = {{0x44, 0x4e, 0x4e, 0}, {0x44, 0x4e, 0x4e, 0}};   /* sdata 0x27c040 */
    int aPos[2][4] = {{0, 0xea, 0, 0}, {0, 0xea, 0, 0}};                                /* OLMENU 0x1ee2010 */

    aPos[0][0] = MUEO(parent)->posMove + 0x14c;
    aPos[1][0] = MUEO(parent)->posMove + 0x26c;
    aColor[1].a = MUEO_LINE(pMobj)->alpha;
    aColor[0].a = MUEO_LINE(pMobj)->alpha;
    DF_MDRAW_DrawPrim(2, 2, aPos, aColor, 1.0f, 0);
}

/* line mobj play_func while a fade runs: alpha over 6 frames (stat 1 = in, stat 2 = out), then the func clears
 * itself (UNREACHED) */
static void __MUnitEzOfficial_DrawLine_Play(struct stMobj *pMobj)
{
    struct struct_519b1c81 *l = MUEO_LINE(pMobj);

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

/* the rank shown on a card: one higher when it is not above the player's own numeric rank */
static inline int mueo_disp_rank(unsigned char rank)
{
    char own = g_gamedata[0].arenaBattle.rank;

    if (own < 100) return rank < own ? rank : rank + 1;
    return rank;
}

/* card mobj slots: [4] rank / class, [5] suffix, [6] pilot name, [12] / [15] user points plus / minus,
 * [17..19] class icons, [20..23] rank-range icons, [24..28] leg, [29..31] weight, [32..34] range, [35] NEW
 * (UNREACHED) */
static void __MUnitEzOfficial_Card_ResetData(struct stMobj *pMobj, char index)
{
    PS2_FRAME(0x80);
    PS2_STACK(char[0x2c], buff, 0x50);
    PS2_STACK(struct struct_76b810ec, color, 0x7c);
    struct struct_bba7b090 *w = MUEO(pMobj);
    struct struct_c42eefd4 *cd = MUEO_CARD(w->apMobjCard[(int)index]);
    struct _arena_official_card_st *ob = &g_gamedata[0].officialBattle[(int)index];
    int k;

    color.r = 0x40; color.g = 0x40; color.b = 0x40; color.a = 0x80;   /* sdata 0x27c038 */
    if (ob->idRanker == 0) {
        /* empty card: grey strings, dashes */
        for (k = 4; k < 0x11; k++) DF_MOBJ_SetStringColorAll(cd->apMobj[k], &color);
        DF_MOBJ_SetDrawEnable(cd->apMobj[35], 0);
        ps2_sprintf_d(buff, munit_ez_official_str_dash2, 0);
        DF_MOBJ_SetStringCopy(cd->apMobj[4], buff);
        DF_MOBJ_SetDrawEnable(cd->apMobj[5], 0);
        ps2_sprintf_d(buff, munit_ez_official_str_dash8, 0);
        DF_MOBJ_SetStringCopy(cd->apMobj[6], buff);
        ps2_sprintf_d(buff, munit_ez_official_str_dash5, 0);
        DF_MOBJ_SetStringCopy(cd->apMobj[12], buff);
        ps2_sprintf_d(buff, munit_ez_official_str_dash5, 0);
        DF_MOBJ_SetStringCopy(cd->apMobj[15], buff);
        return;
    }

    if (ob->flagSearch.Class.Data != 0) {
        /* class card: icon + letter 'A' + class / 2, no suffix */
        DF_MOBJ_SetDrawEnable(cd->apMobj[(ob->flagSearch.Class.Data >> 1) + 0x11], 1);
        buff[1] = 0;
        buff[0] = (char)((ob->flagSearch.Class.Data >> 1) + 0x41);
        DF_MOBJ_SetStringCopy(cd->apMobj[4], buff);
        DF_MOBJ_SetDrawEnable(cd->apMobj[5], 0);
    } else if (ob->flagSearch.Rank != 0) {
        unsigned int j;
        int d;

        for (j = 0; j < 4; j++) {
            if (ob->flagSearch.Rank >= g_aRankRangeTable[j][0] && ob->flagSearch.Rank <= g_aRankRangeTable[j][1]) break;
        }
        DF_MOBJ_SetDrawEnable(cd->apMobj[j + 0x14], 1);   /* (sic) no range matches -> j 4 = apMobj[24], a leg icon */
        ps2_sprintf_d(buff, munit_ez_official_fmt_d, mueo_disp_rank(ob->flagSearch.Rank));
        DF_MOBJ_SetStringCopy(cd->apMobj[4], buff);

        d = mueo_disp_rank(ob->flagSearch.Rank);
        if (d >= 100) {
            buff[0] = 0;
        } else if (d == 1 || d == 0x15) {
            ps2_sprintf_d(buff, munit_ez_official_str_st, 0);   /* the suffix text is the sprintf format */
        } else if (d == 2 || d == 0x16) {
            ps2_sprintf_d(buff, munit_ez_official_str_nd, 0);
        } else if (d == 3 || d == 0x17) {
            ps2_sprintf_d(buff, munit_ez_official_str_rd, 0);
        } else {
            ps2_sprintf_d(buff, munit_ez_official_str_th, 0);   /* (sic) 11..13 -> "TH" too, as the siblings */
        }
        DF_MOBJ_SetStringCopy(cd->apMobj[5], buff);
    }

    /* the first set bit of each search flag shows its icon */
    {
        char f = ob->flagSearch.LegType.Data;
        for (k = 0; k < 5; k++) {
            if ((f >> k) & 1) {
                DF_MOBJ_SetDrawEnable(cd->apMobj[k + 0x18], 1);
                break;
            }
        }
        f = ob->flagSearch.Weight.Data;
        for (k = 0; k < 3; k++) {
            if ((f >> k) & 1) {
                DF_MOBJ_SetDrawEnable(cd->apMobj[k + 0x1d], 1);
                break;
            }
        }
        f = ob->flagSearch.Range.Data;
        for (k = 0; k < 3; k++) {
            if ((f >> k) & 1) {
                DF_MOBJ_SetDrawEnable(cd->apMobj[k + 0x20], 1);
                break;
            }
        }
    }
    if (ob->flagNew == 0) DF_MOBJ_SetDrawEnable(cd->apMobj[35], 0);

    ps2_sprintf_s(buff, munit_ez_official_fmt_s, ob->namePilot);
    DF_MOBJ_SetStringCopy(cd->apMobj[6], buff);
    ps2_sprintf_d(buff, munit_ez_official_fmt_d, ob->flagSearch.UserPointPlus);
    DF_MOBJ_SetStringCopy(cd->apMobj[12], buff);
    ps2_sprintf_d(buff, munit_ez_official_fmt_d, ob->flagSearch.UserPointMinus);
    DF_MOBJ_SetStringCopy(cd->apMobj[15], buff);
}

/* back to the layout position (card 1 146 px lower), then every icon row is re-placed at its "dummy_element%d"
 * entry and hidden (UNREACHED) */
static void __MUnitEzOfficial_Card_Reset(struct stMobj *pMobj, char index)
{
    PS2_FRAME(0x100);
    PS2_STACK(char[32], name, 0x80);
    /* sdata 0x27c030: per dummy element the first / last apMobj slot it positions */
    char aRange[4][2] = {{0x11, 0x17}, {0x18, 0x1c}, {0x1d, 0x1f}, {0x20, 0x22}};
    struct _LAYOUT_RET_DATA layout;
    struct struct_c42eefd4 *cd;
    int dy, j, k;

    mueo_set_header();
    cd = MUEO_CARD(MUEO(pMobj)->apMobjCard[(int)index]);
    MOBJManager_ResetPos(MUEO_MNG, 4, &cd->apMobj[0]);
    dy = index * 0x92;
    MOBJManager_Move(MUEO_MNG, 4, &cd->apMobj[0], 0, dy, 0);
    for (j = 0; j < 4; j++) {
        ps2_sprintf_d(name, munit_ez_official_fmt_dummy_element, j);
        if (!DF_LO_GetLayoutData((unsigned char *)LAYOUT_GetLayoutData(0x44c), (unsigned char *)name, 0, &layout))
            return;
        layout.Pos.y = layout.Pos.y + dy;
        for (k = aRange[j][0]; k <= aRange[j][1]; k++) {
            UnitSprite_SetPosition(cd->apMobj[k], (struct struct_214acd28 *)&layout.Pos);
            DF_MOBJ_SetDrawEnable(cd->apMobj[k], 0);
        }
    }
}

static void __MUnitEzOfficial_DeleteItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct struct_bba7b090 *w = MUEO(pMobj);
    int i;

    mueo_set_header();
    for (i = 0; i < 2; i++) {
        /* (sic) the card is dereferenced before its null test */
        MOBJManager_Delete(&MUEO_CARD(w->apMobjCard[i])->apMobj[0], 4, MUEO_MNG);
        if (w->apMobjCard[i] != 0) {
            mueo_erase(w->apMobjCard[i]);
            w->apMobjCard[i] = 0;
        }
    }
    if (w->pMobjLine != 0) {
        mueo_erase(w->pMobjLine);
        w->pMobjLine = 0;
    }
}

static void __MUnitEzOfficial_CreateItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x70);
    struct struct_bba7b090 *w = MUEO(pMobj);
    struct stMobj *o;
    int i;

    mueo_set_header();
    for (i = 0; i < 2; i++) {
        struct struct_c42eefd4 *cd;

        w->apMobjCard[i] = DF_MOBJ_GetNewObject(pMobj);
        if (w->apMobjCard[i] == 0) return;
        cd = MUEO_CARD(w->apMobjCard[i]);
        memset(cd, 0, 0x94);
        cd->pParent = pMobj;   /* the store is the Create jal's delay slot: before Create runs */
        MOBJManager_Create(pMobj, &cd->apMobj[0], 4, MUEO_MNG);
        /* right-align the 1st, 9th and 12th string */
        MUEO_STRDT(cd->apMobj[4])->flag |= 0x10000;
        MUEO_STRDT(cd->apMobj[12])->flag |= 0x10000;
        MUEO_STRDT(cd->apMobj[15])->flag |= 0x10000;
    }

    w->pMobjLine = DF_MOBJ_GetNewObject(pMobj);
    o = w->pMobjLine;
    if (o == 0) return;
    memset(MUEO_LINE(o), 0, 8);
    MUEO_LINE(o)->pParent = pMobj;
    MUEO_LINE(o)->alpha = 0;
    MUEO_LINE(o)->alpha_max = 0x20;
    w->pMobjLine->draw_func = (void (*)(void))GFN(__MUnitEzOfficial_DrawLine_Pkt);
}

static void __MUnitEzOfficial_Pkt(void)
{
}

static void __MUnitEzOfficial_Draw(void)
{
}

/* both cards by x (header refreshed before the loop and before each Move) */
static inline void mueo_cards_move(struct struct_bba7b090 *w, int x)
{
    int i;

    mueo_set_header();
    for (i = 0; i < 2; i++) {
        mueo_set_header();
        MOBJManager_Move(MUEO_MNG, 4, &MUEO_CARD(w->apMobjCard[i])->apMobj[0], x, 0, 0);
    }
}

static inline void mueo_cards_reset(struct stMobj *pMobj)
{
    int i;

    mueo_set_header();
    for (i = 0; i < 2; i++) __MUnitEzOfficial_Card_Reset(pMobj, (char)i);
}

/* the page slide (every arm past the phase-0 test is REV only): (sic) sequential ifs, so 1 -> 2, 5 -> 6,
 * 9 -> 0xa, 0xd -> 0xe run in the same call; unlike the Match / Ranking siblings every sentinel arm only clears
 * the phase */
static void __MUnitEzOfficial_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x70);
    struct struct_bba7b090 *w = MUEO(pMobj);
    int v;

    if (w->phase == 0) return;
    mueo_set_header();

    if (w->phase == 1) {
        MoveMng_Init(&w->stMoveMng);
        mueo_cards_reset(pMobj);
        w->posMove = 0;
        mueo_cards_move(w, 0x130);
        w->posMove = w->posMove + 0x130;   /* the first Card_ResetData jal's delay slot */
        __MUnitEzOfficial_Card_ResetData(pMobj, 0);
        __MUnitEzOfficial_Card_ResetData(pMobj, 1);
        w->phase = 2;
    }
    if (w->phase == 2) {
        v = MoveMng_GetNextMoveValue(&w->stMoveMng, -0x130, 8, 1);
        if (v == -99999) {
            w->phase = 0;
        } else {
            mueo_cards_move(w, v);
            w->posMove = w->posMove + v;
        }
    }
    if (w->phase == 5) {
        MoveMng_Init(&w->stMoveMng);
        mueo_cards_reset(pMobj);
        w->posMove = 0;
        mueo_cards_move(w, -0x130);
        w->posMove = w->posMove - 0x130;   /* the first Card_ResetData jal's delay slot */
        __MUnitEzOfficial_Card_ResetData(pMobj, 0);
        __MUnitEzOfficial_Card_ResetData(pMobj, 1);
        w->phase = 6;
    }
    if (w->phase == 6) {
        v = MoveMng_GetNextMoveValue(&w->stMoveMng, 0x130, 8, 1);
        if (v == -99999) {
            w->phase = 0;
        } else {
            mueo_cards_move(w, v);
            w->posMove = w->posMove + v;
        }
    }
    if (w->phase == 9) {
        MoveMng_Init(&w->stMoveMng);
        mueo_cards_reset(pMobj);
        w->posMove = 0;
        w->phase = 0xa;
    }
    if (w->phase == 0xa) {
        v = MoveMng_GetNextMoveValue(&w->stMoveMng, -0x130, 8, 1);
        if (v == -99999) {
            w->phase = 0;
        } else {
            mueo_cards_move(w, v);
            w->posMove = w->posMove + v;
        }
    }
    if (w->phase == 0xd) {
        MoveMng_Init(&w->stMoveMng);
        mueo_cards_reset(pMobj);
        w->posMove = 0;
        w->phase = 0xe;
    }
    if (w->phase == 0xe) {
        v = MoveMng_GetNextMoveValue(&w->stMoveMng, 0x130, 8, 1);
        if (v == -99999) {
            w->phase = 0;
        } else {
            mueo_cards_move(w, v);
            w->posMove = w->posMove + v;
        }
    }
    if (w->phase == 0x11) {
        __MUnitEzOfficial_Card_ResetData(pMobj, 0);
        __MUnitEzOfficial_Card_ResetData(pMobj, 1);
        w->phase = 0;
    }
    if (w->phase == 0x12) w->phase = 0;
}

/* the original is a tail jump (j DeleteItem): no frame */
void MUnitEzOfficial_Cleanup(struct stMobj *pMobj)
{
    __MUnitEzOfficial_DeleteItem(pMobj);
}

struct stMobj *MUnitEzOfficial_NewSystem(struct stMobj *pParentMobj)
{
    PS2_FRAME(0x20);
    struct stMobj *m = DF_MOBJ_GetNewObject(pParentMobj);

    if (m == 0) return 0;
    memset(MUEO(m), 0, 0x28);
    m->type = 0;
    m->stat = 1;
    m->count = 0;
    m->play_func = (void (*)(void))GFN(__MUnitEzOfficial_Play);
    m->draw_func = (void (*)(void))GFN(__MUnitEzOfficial_Draw);
    m->pkt_func = (void (*)(void))GFN(__MUnitEzOfficial_Pkt);
    __MUnitEzOfficial_CreateItem(m);
    return m;
}

/* UNREACHED (marena_top's callers never run with the OFFICIAL tab): no null tests on the cards / line */
void MUnitEzOfficial_FadeOutStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x50);
    struct struct_bba7b090 *w = MUEO(pMobj);
    int i;

    w->phase = phase;
    mueo_set_header();
    for (i = 0; i < 2; i++) MOBJManager_FadeOut(&MUEO_CARD(w->apMobjCard[i])->apMobj[0], 0, 4, MUEO_MNG);
    w->pMobjLine->play_func = (void (*)(void))GFN(__MUnitEzOfficial_DrawLine_Play);
    w->pMobjLine->stat = 2;
    w->pMobjLine->count = 0;
}

void MUnitEzOfficial_FadeInStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x60);
    struct struct_bba7b090 *w = MUEO(pMobj);
    int i;

    w->phase = phase;
    mueo_set_header();
    for (i = 0; i < 2; i++) {
        MOBJManager_FadeIn(&MUEO_CARD(w->apMobjCard[i])->apMobj[0], 0, 4, MUEO_MNG);
        __MUnitEzOfficial_Card_Reset(pMobj, (char)i);
    }
    w->pMobjLine->play_func = (void (*)(void))GFN(__MUnitEzOfficial_DrawLine_Play);
    w->pMobjLine->stat = 1;
    w->pMobjLine->count = 0;
}
