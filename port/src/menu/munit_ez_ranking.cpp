/* src/menu/munit_ez_ranking.cpp - the "RANKING" tab preview of the ARENA top page (marena_top.c cursor 2): the
 * player's arena rank (a number with an ST / ND / RD / TH suffix, or a letter for rank >= 100), the points still
 * needed for the next rank, a hint how many battles remain before the next official match, and the two rank-ladder
 * markers ("koma0" sprite + "koma1" blink sprite) placed at g_aPosTable[] by the rank.
 * A passive page like munit_ez_match: MArenaTop creates it (NewSystem), slides it in / out (FadeInStart /
 * FadeOutStart with a phase) and deletes it (Cleanup); Play runs the slide state machine (posMove, MoveMng ease,
 * 0x130 px) through the Move / Reset helpers. The work block (struct_2818fa9d) is INLINE in pMobj->dt.
 * Two stMobjManager tables: table 1 (the DWARF static Move::aMobjData, layout 0x44d, 4 strings) and table 2 (layout
 * 0x453 = munit_top_arena's layout, 3 + 2 strings, 4 sprites). Each table's record 0 pLayoutHeader is rewritten
 * before the manager calls, so the hybrid keeps both tables at their PS2 addresses (table 2 = nodwarf
 * munit_ez_ranking_mng2); the sprintf formats are nodwarf PS2 strings.
 * This file owns g_aPosTable (also read by munit_top_arena.cpp); the source port defines it here.
 * Original quirks kept (ported literally, marked (sic)). */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU 0x1ee2050.. (layout names), main sdata 0x27c070 / 0x27c078 */
static char munit_ez_ranking_name_string[] = "string%d";
static char munit_ez_ranking_name_string15[] = "string15";
static char munit_ez_ranking_name_string16[] = "string16";
static char munit_ez_ranking_name_position_under[] = "position_under";
static char munit_ez_ranking_name_position[] = "position";
static char munit_ez_ranking_name_koma0[] = "koma0";
static char munit_ez_ranking_name_koma1[] = "koma1";
/* OLMENU 0x1ee2060: the DWARF static __MUnitEzRanking_Move::aMobjData[1] */
static struct stMobjManager munit_ez_ranking_mng1[1] = {
    {.pLayoutName = munit_ez_ranking_name_string, .pLayoutHeader = 0, .numContinue = 4, .typeMObj = 2,
     .flag = 0, .dummy_c0 = {0}, .exString = {.category = 0, .strid = 0xdbdc6}},
};
#define MUER_MNG1 munit_ez_ranking_mng1
#define MUER_STR(n, num) {.pLayoutName = n, .pLayoutHeader = 0, .numContinue = num, .typeMObj = 2, \
                          .flag = 0, .dummy_c0 = {0}, .exString = {.category = 0, .strid = 0xdbcb8}}
/* OLMENU 0x1ee20e0 (7 x 0x40) */
struct stMobjManager munit_ez_ranking_mng2[7] = {
    MUER_STR(munit_ez_ranking_name_string, 3),
    MUER_STR(munit_ez_ranking_name_string15, 1),
    MUER_STR(munit_ez_ranking_name_string16, 1),
    {.pLayoutName = munit_ez_ranking_name_position_under, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 1},
    {.pLayoutName = munit_ez_ranking_name_position, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 1},
    {.pLayoutName = munit_ez_ranking_name_koma0, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 1},
    {.pLayoutName = munit_ez_ranking_name_koma1, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 4},
};
#undef MUER_STR
/* main sdata 0x27c084..0x27c09f */
char munit_ez_ranking_fmt_d[3] = "%d";
char munit_ez_ranking_fmt_c[3] = "%c";
char munit_ez_ranking_str_st[3] = "ST";
char munit_ez_ranking_str_nd[3] = "ND";
char munit_ez_ranking_str_rd[3] = "RD";
char munit_ez_ranking_str_th[3] = "TH";
char munit_ez_ranking_str_space[2] = " ";

/* OLMENU 0x1ee6340: rank-ladder marker positions. Rows 0..29 = numeric ranks 1..30, rows 30..32 = letter ranks
 * 100..102 (DWARF global of this file; munit_top_arena.cpp reads it too) */
struct struct_214acd28 g_aPosTable[33] = {
    {513, 107}, {496, 122}, {493, 124}, {490, 126}, {487, 128}, {477, 144}, {474, 146}, {471, 148},
    {468, 150}, {465, 152}, {453, 168}, {447, 170}, {441, 172}, {435, 174}, {429, 176}, {414, 189},
    {412, 190}, {410, 191}, {408, 192}, {406, 193}, {404, 194}, {402, 195}, {400, 196}, {398, 197},
    {396, 198}, {394, 199}, {392, 200}, {390, 201}, {388, 202}, {386, 203}, {363, 214}, {346, 220},
    {329, 226},
};
#else
#define MUER_MNG1 __MUnitEzRanking_Move__aMobjData
#endif

#define MUER_MNG2 munit_ez_ranking_mng2
#define MUER(o) ((struct struct_2818fa9d *)&(o)->dt)
/* the string mobjs of this page: dt.string (struct_d2c56d2a) */
#define MUER_STRDT(o) ((struct struct_d2c56d2a *)&(o)->dt)

static inline void muer_set_header1(void)
{
    MUER_MNG1[0].pLayoutHeader = (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x44d);
}

static inline void muer_set_header2(void)
{
    MUER_MNG2[0].pLayoutHeader = (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x453);
}

/* (sic) unchecked: g_aPosTable has 33 rows, so only ranks 1..30 and 100..102 land inside it */
static inline int muer_pos_index(void)
{
    char rank = g_gamedata[0].arenaBattle.rank;

    if (rank < 100) return rank - 1;
    return rank - 0x46;
}

/* the two markers (apMobjSub[7] koma0, [8] koma1) at the rank's table row, 0x18 px lower, + dx */
static inline void muer_set_marker_pos(struct struct_2818fa9d *w, int dx)
{
    int idx = muer_pos_index();
    struct struct_214acd28 pos;

    pos.x = dx + g_aPosTable[idx].x;
    pos.y = g_aPosTable[idx].y + 0x18;
    UnitSprite_SetPosition(w->apMobjSub[7], &pos);
    UnitSprite_SetPosition(w->apMobjSub[8], &pos);
}

/* the texts: apMobjSub[1] rank number, [2] suffix, [3] letter-rank label, [4] letter; apMobj[1] points to the next
 * rank, apMobj[3] the battles-needed hint */
static void __MUnitEzRanking_ResetData(struct stMobj *pMobj)
{
    PS2_FRAME(0xc0);
    PS2_STACK(char[128], buff, 0x30);
    struct struct_2818fa9d *w = MUER(pMobj);

    if (g_gamedata[0].arenaBattle.rank < 100) {
        /* numeric rank: number + suffix (UNREACHED by any replay) */
        char rank = g_gamedata[0].arenaBattle.rank;
        if (rank < 100) ps2_sprintf_d(buff, munit_ez_ranking_fmt_d, rank);
        else ps2_sprintf_d(buff, munit_ez_ranking_fmt_c, (char)(rank - 0x23));   /* (sic) dead arm */
        DF_MOBJ_SetStringCopy(w->apMobjSub[1], buff);

        rank = g_gamedata[0].arenaBattle.rank;
        if (rank >= 100) {
            buff[0] = 0;   /* (sic) dead arm: the test above cannot fail here */
        } else if (rank == 1 || rank == 0x15) {
            ps2_sprintf_d(buff, munit_ez_ranking_str_st, 0);   /* (sic) the suffix text is the sprintf format */
        } else if (rank == 2 || rank == 0x16) {
            ps2_sprintf_d(buff, munit_ez_ranking_str_nd, 0);
        } else if (rank == 3 || rank == 0x17) {
            ps2_sprintf_d(buff, munit_ez_ranking_str_rd, 0);
        } else {
            ps2_sprintf_d(buff, munit_ez_ranking_str_th, 0);   /* (sic) 11 -> "11TH" but also 12 / 13 */
        }
        DF_MOBJ_SetStringCopy(w->apMobjSub[2], buff);

        ps2_sprintf_d(buff, munit_ez_ranking_str_space, 0);
        DF_MOBJ_SetStringCopy(w->apMobjSub[3], buff);
        ps2_sprintf_d(buff, munit_ez_ranking_str_space, 0);
        DF_MOBJ_SetStringCopy(w->apMobjSub[4], buff);
    } else {
        /* letter rank: the label mobj gets a fixed strid and the letter goes next to it */
        MUER_STRDT(w->apMobjSub[3])->id = 0xdbbb0;

        char rank = g_gamedata[0].arenaBattle.rank;
        if (rank < 100) ps2_sprintf_d(buff, munit_ez_ranking_fmt_d, rank);   /* (sic) dead arm */
        else ps2_sprintf_d(buff, munit_ez_ranking_fmt_c, (char)(rank - 0x23));
        DF_MOBJ_SetStringCopy(w->apMobjSub[4], buff);

        ps2_sprintf_d(buff, munit_ez_ranking_str_space, 0);
        DF_MOBJ_SetStringCopy(w->apMobjSub[1], buff);
        ps2_sprintf_d(buff, munit_ez_ranking_str_space, 0);
        DF_MOBJ_SetStringCopy(w->apMobjSub[2], buff);
    }

    muer_set_header2();
    muer_set_marker_pos(w, 0);   /* no slide offset here */

    /* points to the next rank: the first rank band containing the rank */
    {
        struct struct_f3000f24 *pArenaPoint = MArenaPointMng_GetArenaPoint();
        char rank = g_gamedata[0].arenaBattle.rank;
        unsigned int i;
        int d;

        for (i = 0; i < 8; i++) {
            if (g_aRankChackTable[i][0] <= rank && rank <= g_aRankChackTable[i][1]) break;
        }
        /* (sic) no band found -> i == 8 reads pArenaPoint[8], past the 8-entry array */
        d = pArenaPoint[i].point - g_gamedata[0].arenaBattle.pointArena;
        if (d < 0) d = 0;
        ps2_sprintf_d(buff, munit_ez_ranking_fmt_d, d);
        DF_MOBJ_SetStringCopy(w->apMobj[1], buff);
    }

    /* battles needed before the next official match */
    {
        struct struct_4ef7c987 *pHead = MArenaPointMng_GetArenaPointHead();
        char past = g_gamedata[0].arenaBattle.resultPastOfficialForRankup;
        char n;
        char numNeedBattle;

        if (past == 0) n = 0;
        else if (past == 1) n = pHead->numMatchNextInviteLose;
        else n = pHead->numMatchNextInviteDraw;
        numNeedBattle = (char)(n - g_gamedata[0].arenaBattle.numBattleFromBeforeOfficialForRankup);
        if (numNeedBattle > 0 && g_gamedata[0].officialBattle[1].idRanker == 0) {
            char *fmt = (char *)DF_MSTR_GetName(MenuMng.language, 0, 0xdbdc9);
            ps2_sprintf_d(buff, fmt, numNeedBattle);
        } else {
            ps2_sprintf_d(buff, munit_ez_ranking_str_space, 0);
        }
        DF_MOBJ_SetStringCopy(w->apMobj[3], buff);
    }
}

static void __MUnitEzRanking_Move(struct stMobj *pMobj, int x, int y)
{
    PS2_FRAME(0x60);
    struct struct_2818fa9d *w = MUER(pMobj);

    muer_set_header1();
    muer_set_header2();
    w->posMove = w->posMove + x;   /* the store is the first Move jal's delay slot */
    MOBJManager_Move(MUER_MNG1, 1, &w->apMobj[0], x, y, 0);
    MOBJManager_Move(MUER_MNG2, 7, &w->apMobjSub[0], x, y, 0);
    {
        int posMove = w->posMove;
        muer_set_header2();
        muer_set_marker_pos(w, posMove);
    }
}

/* both tables back at their layout positions, then table 2 0x18 px down (table 1 stays) */
static void __MUnitEzRanking_Reset(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct struct_2818fa9d *w = MUER(pMobj);

    muer_set_header1();
    muer_set_header2();
    w->posMove = 0;   /* the store is the first ResetPos jal's delay slot */
    MOBJManager_ResetPos(MUER_MNG1, 1, &w->apMobj[0]);
    MOBJManager_ResetPos(MUER_MNG2, 7, &w->apMobjSub[0]);
    MOBJManager_Move(MUER_MNG2, 7, &w->apMobjSub[0], 0, 0x18, 0);
    {
        int posMove = w->posMove;
        muer_set_header2();
        muer_set_marker_pos(w, posMove);
    }
}

static void __MUnitEzRanking_Pkt(void)
{
}

static void __MUnitEzRanking_Draw(void)
{
}

/* the page slide: (sic) sequential ifs, so 1 -> 2, 5 -> 6, 9 -> 0xa, 0xd -> 0xe run in the same call;
 * (sic) the sentinel arms of 2 / 6 only clear the phase, those of 0xa / 0xe also Reset */
static void __MUnitEzRanking_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct struct_2818fa9d *w = MUER(pMobj);
    int v;

    if (w->phase == 0) return;
    muer_set_header1();   /* (only table 1's header here) */

    if (w->phase == 1) {
        MoveMng_Init(&w->stMoveMng);
        __MUnitEzRanking_Reset(pMobj);
        __MUnitEzRanking_Move(pMobj, 0x130, 0);
        w->phase = 2;
    }
    if (w->phase == 2) {
        v = MoveMng_GetNextMoveValue(&w->stMoveMng, -0x130, 8, 1);
        if (v == -99999) w->phase = 0;
        else __MUnitEzRanking_Move(pMobj, v, 0);
    }
    if (w->phase == 5) {
        MoveMng_Init(&w->stMoveMng);
        __MUnitEzRanking_Reset(pMobj);
        __MUnitEzRanking_Move(pMobj, -0x130, 0);
        w->phase = 6;
    }
    if (w->phase == 6) {
        v = MoveMng_GetNextMoveValue(&w->stMoveMng, 0x130, 8, 1);
        if (v == -99999) w->phase = 0;
        else __MUnitEzRanking_Move(pMobj, v, 0);
    }
    if (w->phase == 9) {
        MoveMng_Init(&w->stMoveMng);
        __MUnitEzRanking_Reset(pMobj);
        w->phase = 0xa;
    }
    if (w->phase == 0xa) {
        v = MoveMng_GetNextMoveValue(&w->stMoveMng, -0x130, 8, 1);
        if (v == -99999) {
            __MUnitEzRanking_Reset(pMobj);
            w->phase = 0;
        } else {
            __MUnitEzRanking_Move(pMobj, v, 0);
        }
    }
    if (w->phase == 0xd) {
        MoveMng_Init(&w->stMoveMng);
        __MUnitEzRanking_Reset(pMobj);
        w->phase = 0xe;
    }
    if (w->phase == 0xe) {
        v = MoveMng_GetNextMoveValue(&w->stMoveMng, 0x130, 8, 1);
        if (v == -99999) {
            __MUnitEzRanking_Reset(pMobj);
            w->phase = 0;
        } else {
            __MUnitEzRanking_Move(pMobj, v, 0);
        }
    }
    if (w->phase == 0x11) {
        __MUnitEzRanking_Reset(pMobj);
        w->phase = 0;
    }
    if (w->phase == 0x12) {
        __MUnitEzRanking_Reset(pMobj);
        w->phase = 0;
    }
}

void MUnitEzRanking_FadeOutStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x20);
    struct struct_2818fa9d *w = MUER(pMobj);

    w->phase = phase;
    muer_set_header1();
    muer_set_header2();
    MOBJManager_FadeOut(&w->apMobj[0], 0, 1, MUER_MNG1);
    MOBJManager_FadeOut(&w->apMobjSub[0], 0, 7, MUER_MNG2);
}

void MUnitEzRanking_FadeInStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x30);
    struct struct_2818fa9d *w = MUER(pMobj);

    w->phase = phase;
    muer_set_header1();
    muer_set_header2();
    MOBJManager_FadeIn(&w->apMobj[0], 0, 1, MUER_MNG1);
    MOBJManager_FadeIn(&w->apMobjSub[0], 0, 7, MUER_MNG2);
    __MUnitEzRanking_ResetData(pMobj);
}

void MUnitEzRanking_Cleanup(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    struct struct_2818fa9d *w = MUER(pMobj);

    muer_set_header1();
    muer_set_header2();
    MOBJManager_Delete(&w->apMobj[0], 1, MUER_MNG1);
    MOBJManager_Delete(&w->apMobjSub[0], 7, MUER_MNG2);
}

struct stMobj *MUnitEzRanking_NewSystem(struct stMobj *pParentMobj)
{
    PS2_FRAME(0x40);
    PS2_STACK(struct struct_76b810ec, color, 0x3c);
    struct stMobj *m = DF_MOBJ_GetNewObject(pParentMobj);
    struct struct_2818fa9d *w;
    unsigned int col = 0x8061a5de;   /* main sdata 0x27c080, copied as raw bits (lwc1 / swc1) */

    if (m == 0) return 0;
    w = MUER(m);
    memset(w, 0, 0x50);
    m->type = 0;
    m->stat = 1;
    m->count = 0;
    m->play_func = (void (*)(void))GFN(__MUnitEzRanking_Play);
    m->draw_func = (void (*)(void))GFN(__MUnitEzRanking_Draw);
    m->pkt_func = (void (*)(void))GFN(__MUnitEzRanking_Pkt);
    memcpy(&color, &col, 4);
    muer_set_header1();
    muer_set_header2();
    MOBJManager_Create(m, &w->apMobj[0], 1, MUER_MNG1);
    MUER_STRDT(w->apMobj[1])->flag |= 0x10000;   /* in the next Create's delay slot: before it runs */
    MOBJManager_Create(m, &w->apMobjSub[0], 7, MUER_MNG2);
    MUER_STRDT(w->apMobjSub[1])->flag |= 0x10000;
    DF_MOBJ_SetStringColorAll(w->apMobjSub[0], &color);
    MUER_STRDT(w->apMobjSub[0])->id = 0xdc3d4;
    return m;
}
