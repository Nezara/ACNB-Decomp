/* src/menu/munit_top_arena.cpp - the ARENA page of the single-player top menu (menu/mtop_rt's ARENA cursor,
 * stMTopRT.pMobjUnitTopArena): the arena record panel - the arena rank (a letter for rank >= 100, else a number
 * with an ST / ND / RD / TH suffix), the four counters (battles / wins / losses / draws), the arena points and the
 * two rank markers ("koma0" sprite + "koma1" blinking sprite) placed at g_aPosTable[] by the player's rank.
 * One stMobj whose dt is a struct_f9c9a569: 21 mobjs built from layout 0x453 by ONE MOBJManager_Create over the
 * file's 5-entry stMobjManager table (17 strings "string%d" with strid base 0xdbcb8, position_under, position,
 * koma0, koma1). ResetData fills the strings from g_gamedata[0].arenaBattle (always player 0); Play is the slide
 * state machine (phase + stMoveMng + posMove) that moves all 21 mobjs by +-0x130 px. Pkt and Draw are empty.
 * The table's pLayoutHeader is rewritten before every MOBJManager_* call, so the hybrid keeps the table at its PS2
 * address (nodwarf munit_top_arena_mng).
 * Original quirks kept (ported literally, marked (sic)). */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU 0x1ee3140..0x1ee32af, main sdata 0x27c220..0x27c23f */
static char munit_top_arena_name_string[] = "string%d";
static char munit_top_arena_name_position_under[] = "position_under";
static char munit_top_arena_name_position[] = "position";
static char munit_top_arena_name_kom0[] = "koma0";   /* main sdata 0x27c210 */
static char munit_top_arena_name_kom1[] = "koma1";   /* 0x27c218 */
struct stMobjManager munit_top_arena_mng[5] = {
    {.pLayoutName = munit_top_arena_name_string, .pLayoutHeader = 0, .numContinue = 0x11, .typeMObj = 2,
     .flag = 0, .dummy_c0 = {0}, .exString = {.category = 0, .strid = 0xdbcb8}},
    {.pLayoutName = munit_top_arena_name_position_under, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 1},
    {.pLayoutName = munit_top_arena_name_position, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 1},
    {.pLayoutName = munit_top_arena_name_kom0, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 1},
    {.pLayoutName = munit_top_arena_name_kom1, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 4},
};
char munit_top_arena_fmt_d[3] = "%d";
char munit_top_arena_fmt_c[3] = "%c";
char munit_top_arena_str_st[3] = "ST";
char munit_top_arena_str_nd[3] = "ND";
char munit_top_arena_str_rd[3] = "RD";
char munit_top_arena_str_th[3] = "TH";
char munit_top_arena_str_space[2] = " ";

#endif

/* g_aPosTable: DWARF global of menu/munit_ez_ranking.c (the source port defines it there) */
#define MUTA_POSTABLE g_aPosTable

#define MUTA_MNG munit_top_arena_mng
#define MUTA(o) ((struct struct_f9c9a569 *)&(o)->dt)
/* the string mobjs of this page: dt.string (struct_d2c56d2a) */
#define MUTA_STR(o) ((struct struct_d2c56d2a *)&(o)->dt)

static inline void muta_set_header(void)
{
    MUTA_MNG[0].pLayoutHeader = (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x453);
}

/* (sic) unchecked: g_aPosTable has 33 rows, so only ranks 1..30 and 100..102 land inside it */
static inline int muta_pos_index(void)
{
    char rank = g_gamedata[0].arenaBattle.rank;

    if (rank < 100) return rank - 1;
    return rank - 0x46;
}

/* place the two rank markers at the table row of the current rank, shifted by the slide offset */
static void muta_set_pos(struct struct_f9c9a569 *pWork)
{
    int idx = muta_pos_index();
    struct struct_214acd28 pos;

    pos.x = pWork->posMove + MUTA_POSTABLE[idx].x;
    pos.y = MUTA_POSTABLE[idx].y;
    UnitSprite_SetPosition(pWork->apMobj[19], &pos);
    UnitSprite_SetPosition(pWork->apMobj[20], &pos);
}

static void __MUnitTopArena_ResetData(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    PS2_STACK(char[32], buff, 0x30);
    struct struct_f9c9a569 *pWork = MUTA(pMobj);

    if (g_gamedata[0].arenaBattle.rank < 100) {
        /* numeric rank: number + suffix (UNREACHED by any replay) */
        char rank = g_gamedata[0].arenaBattle.rank;
        if (rank < 100) ps2_sprintf_d(buff, munit_top_arena_fmt_d, rank);
        else ps2_sprintf_d(buff, munit_top_arena_fmt_c, (char)(rank - 0x23));   /* (sic) dead arm */
        DF_MOBJ_SetStringCopy(pWork->apMobj[1], buff);

        rank = g_gamedata[0].arenaBattle.rank;
        if (rank >= 100) {
            buff[0] = 0;   /* (sic) dead arm: the test above cannot fail here */
        } else if (rank == 1 || rank == 0x15) {
            ps2_sprintf_d(buff, munit_top_arena_str_st, 0);   /* (sic) the suffix text is the sprintf format */
        } else if (rank == 2 || rank == 0x16) {
            ps2_sprintf_d(buff, munit_top_arena_str_nd, 0);
        } else if (rank == 3 || rank == 0x17) {
            ps2_sprintf_d(buff, munit_top_arena_str_rd, 0);
        } else {
            ps2_sprintf_d(buff, munit_top_arena_str_th, 0);   /* (sic) 31 -> "31TH", 11 -> "11TH" */
        }
        DF_MOBJ_SetStringCopy(pWork->apMobj[2], buff);

        ps2_sprintf_d(buff, munit_top_arena_str_space, 0);
        DF_MOBJ_SetStringCopy(pWork->apMobj[15], buff);
        ps2_sprintf_d(buff, munit_top_arena_str_space, 0);
        DF_MOBJ_SetStringCopy(pWork->apMobj[16], buff);
    } else {
        /* letter rank: the label mobj gets a fixed strid and the letter goes next to it */
        MUTA_STR(pWork->apMobj[15])->id = 0xdbbb0;

        char rank = g_gamedata[0].arenaBattle.rank;
        if (rank < 100) ps2_sprintf_d(buff, munit_top_arena_fmt_d, rank);   /* (sic) dead arm */
        else ps2_sprintf_d(buff, munit_top_arena_fmt_c, (char)(rank - 0x23));
        DF_MOBJ_SetStringCopy(pWork->apMobj[16], buff);

        ps2_sprintf_d(buff, munit_top_arena_str_space, 0);
        DF_MOBJ_SetStringCopy(pWork->apMobj[1], buff);
        ps2_sprintf_d(buff, munit_top_arena_str_space, 0);
        DF_MOBJ_SetStringCopy(pWork->apMobj[2], buff);
    }

    ps2_sprintf_d(buff, munit_top_arena_fmt_d, g_gamedata[0].arenaBattle.numBattle);
    DF_MOBJ_SetStringCopy(pWork->apMobj[5], buff);
    ps2_sprintf_d(buff, munit_top_arena_fmt_d, g_gamedata[0].arenaBattle.numWin);
    DF_MOBJ_SetStringCopy(pWork->apMobj[7], buff);
    ps2_sprintf_d(buff, munit_top_arena_fmt_d, g_gamedata[0].arenaBattle.numLose);
    DF_MOBJ_SetStringCopy(pWork->apMobj[9], buff);
    ps2_sprintf_d(buff, munit_top_arena_fmt_d, g_gamedata[0].arenaBattle.numDraw);
    DF_MOBJ_SetStringCopy(pWork->apMobj[11], buff);
    ps2_sprintf_d(buff, munit_top_arena_fmt_d, g_gamedata[0].arenaBattle.pointArena);
    DF_MOBJ_SetStringCopy(pWork->apMobj[13], buff);

    /* the two markers are placed straight from the table row (no slide offset here) */
    {
        int idx = muta_pos_index();
        UnitSprite_SetPosition(pWork->apMobj[19], &MUTA_POSTABLE[idx]);
        UnitSprite_SetPosition(pWork->apMobj[20], &MUTA_POSTABLE[idx]);
    }
}

static void __MUnitTopArena_CreateItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    PS2_STACK(struct struct_76b810ec, color, 0x3c);
    struct struct_f9c9a569 *pWork = MUTA(pMobj);
    unsigned int col = 0x8061a5de;   /* main sdata 0x27c220, copied as raw bits (lwc1 / swc1) */

    memcpy(&color, &col, 4);
    muta_set_header();
    MOBJManager_Create(pMobj, &pWork->apMobj[0], 5, MUTA_MNG);

    /* right-align the numeric strings */
    MUTA_STR(pWork->apMobj[1])->flag |= 0x10000;
    MUTA_STR(pWork->apMobj[5])->flag |= 0x10000;
    MUTA_STR(pWork->apMobj[7])->flag |= 0x10000;
    MUTA_STR(pWork->apMobj[9])->flag |= 0x10000;
    MUTA_STR(pWork->apMobj[11])->flag |= 0x10000;
    MUTA_STR(pWork->apMobj[13])->flag |= 0x10000;

    DF_MOBJ_SetStringColorAll(pWork->apMobj[0], &color);
    DF_MOBJ_SetStringColorAll(pWork->apMobj[3], &color);
    DF_MOBJ_SetStringColorAll(pWork->apMobj[12], &color);

    MUTA_STR(pWork->apMobj[0])->id = 0xdc3d4;
    MUTA_STR(pWork->apMobj[3])->id = 0xdc3d5;
    MUTA_STR(pWork->apMobj[4])->id = 0xdc37a;
    MUTA_STR(pWork->apMobj[6])->id = 0xdc3d6;
    MUTA_STR(pWork->apMobj[8])->id = 0xdc3d7;
    MUTA_STR(pWork->apMobj[10])->id = 0xdc3d8;
    MUTA_STR(pWork->apMobj[12])->id = 0xdc3d9;
    MUTA_STR(pWork->apMobj[14])->id = 0xdbbaf;

    __MUnitTopArena_ResetData(pMobj);
}

static void __MUnitTopArena_Pkt(void)
{
}

static void __MUnitTopArena_Draw(void)
{
}

/* Play helpers (inlined in the original: no frames of their own) */
static void muta_move(struct struct_f9c9a569 *pWork, int dx)
{
    muta_set_header();
    pWork->posMove += dx;   /* the store is the Move jal's delay slot: Move does not read it */
    MOBJManager_Move(MUTA_MNG, 5, &pWork->apMobj[0], dx, 0, 0);
}

/* phase start: restart the move manager and put all 21 mobjs back at their layout positions */
static void muta_slide_start(struct struct_f9c9a569 *pWork)
{
    MoveMng_Init(&pWork->stMoveMng);
    muta_set_header();
    pWork->posMove = 0;   /* the store is the ResetPos jal's delay slot */
    MOBJManager_ResetPos(MUTA_MNG, 5, &pWork->apMobj[0]);
}

/* one slide step; returns 1 when the move manager is done */
static int muta_slide_step(struct struct_f9c9a569 *pWork, int dx)
{
    int v = MoveMng_GetNextMoveValue(&pWork->stMoveMng, dx, 8, 1);

    if (v == -99999) return 1;
    muta_move(pWork, v);
    muta_set_pos(pWork);
    return 0;
}

static void __MUnitTopArena_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x80);
    struct struct_f9c9a569 *pWork = MUTA(pMobj);

    if (pWork->phase == 0) return;

    /* the blocks run in sequence in one call and each re-reads phase, so a block that sets the next phase is
     * followed by that phase's block in the same frame (1 -> 2, 5 -> 6, 9 -> 0xa, 0xd -> 0xe). The asm order is
     * not numeric: keep it. (every arm below is REV-only: no replay reaches a non-zero phase) */
    if (pWork->phase == 1) {           /* slide in from the right */
        muta_slide_start(pWork);
        muta_move(pWork, 0x130);
        muta_set_pos(pWork);
        DF_MOBJ_SetDrawEnable(pWork->apMobj[18], 0);
        pWork->phase = 2;
    }
    if (pWork->phase == 2) {
        if (muta_slide_step(pWork, -0x130)) {
            DF_MOBJ_SetDrawEnable(pWork->apMobj[18], 1);
            pWork->phase = 0;
        }
    }
    if (pWork->phase == 5) {           /* slide in from the left */
        muta_slide_start(pWork);
        muta_move(pWork, -0x130);
        muta_set_pos(pWork);
        pWork->phase = 6;
    }
    if (pWork->phase == 6) {
        if (muta_slide_step(pWork, 0x130)) pWork->phase = 0;
    }
    if (pWork->phase == 9) {           /* slide out to the left: no Move, no SetPosition */
        muta_slide_start(pWork);
        pWork->phase = 0xa;
    }
    if (pWork->phase == 0xa) {
        if (muta_slide_step(pWork, -0x130)) pWork->phase = 0;
    }
    if (pWork->phase == 0xd) {         /* slide out to the right: no Move, no SetPosition */
        muta_slide_start(pWork);
        pWork->phase = 0xe;
    }
    if (pWork->phase == 0xe) {
        if (muta_slide_step(pWork, 0x130)) pWork->phase = 0;
    }
    if (pWork->phase == 0x11) pWork->phase = 0;
    if (pWork->phase == 0x12) pWork->phase = 0;
}

void MUnitTopArena_FadeOutStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x20);
    struct struct_f9c9a569 *pWork = MUTA(pMobj);

    pWork->phase = phase;
    muta_set_header();
    MOBJManager_FadeOut(&pWork->apMobj[0], 0, 5, MUTA_MNG);
    pWork->flagFadeOutUnit = 1;
}

void MUnitTopArena_FadeInStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x20);
    struct struct_f9c9a569 *pWork = MUTA(pMobj);

    pWork->phase = phase;
    muta_set_header();
    MOBJManager_FadeIn(&pWork->apMobj[0], 0, 5, MUTA_MNG);
    pWork->flagFadeOutUnit = 0;   /* (unlike the SYSTEM page this does NOT call ResetData) */
}

void MUnitTopArena_Cleanup(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);

    muta_set_header();
    MOBJManager_Delete(&MUTA(pMobj)->apMobj[0], 5, MUTA_MNG);
}

struct stMobj *MUnitTopArena_NewSystem(struct stMobj *pParentMobj)
{
    PS2_FRAME(0x20);
    struct stMobj *obj = DF_MOBJ_GetNewObject(pParentMobj);

    if (obj == 0) return 0;

    memset(MUTA(obj), 0, 0x70);
    obj->type = 0;
    obj->stat = 1;
    obj->count = 0;
    obj->play_func = (void (*)(void))GFN(__MUnitTopArena_Play);
    obj->draw_func = (void (*)(void))GFN(__MUnitTopArena_Draw);
    obj->pkt_func = (void (*)(void))GFN(__MUnitTopArena_Pkt);
    __MUnitTopArena_CreateItem(obj);
    return obj;
}
