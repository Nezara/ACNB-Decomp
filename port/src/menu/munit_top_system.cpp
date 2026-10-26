/* src/menu/munit_top_system.cpp - the SYSTEM page of the top menu (the profile / stats panel: player name,
 * appellation, the three class rows with counts and percentages, training points, arena rank and points, total
 * points). Two hosts: the single-player top menu (menu/mtop_rt, mode 1, player 0) and the system menu (menu/msys,
 * mode 2, player = dt.player, recreated per entry in the Versus garage).
 * One stMobj whose dt is a struct_3bc93e83: 29 string mobjs built from layout 0x455 by one MOBJManager_Create
 * through the file's single-entry stMobjManager table ("string%d", strid base 0xdbcb8). ResetData (re)fills the
 * strings from g_gamedata[player]; Play is the slide state machine (phase + stMoveMng) that moves all 29 mobjs by
 * +-0x130 px (and by +-0x10 for the SYSTEM cursor). Pkt and Draw are empty (the mobjs draw themselves).
 * The table's pLayoutHeader is rewritten before every MOBJManager_* call, so the hybrid keeps the table at its PS2
 * address (nodwarf munit_top_system_mng).
 * Original quirks kept (ported literally, marked (sic)). */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU 0x1ee3340 / 0x1ee3350 / 0x1ee3390, main sdata 0x27c24c..0x27c274 */
static char munit_top_system_name_string[] = "string%d";
struct stMobjManager munit_top_system_mng[1] = {
    {.pLayoutName = munit_top_system_name_string, .pLayoutHeader = 0, .numContinue = 0x1d, .typeMObj = 2,
     .flag = 0, .dummy_c0 = {0}, .exString = {.category = 0, .strid = 0xdbcb8}},
};
char munit_top_system_fmt_pct[9] = "(%s%d%%)";
char munit_top_system_fmt_d[3] = "%d";
char munit_top_system_pad0[1] = "";
char munit_top_system_pad2[3] = "  ";
char munit_top_system_pad4[5] = "    ";
char munit_top_system_fmt_c[3] = "%c";
char munit_top_system_str_st[3] = "ST";
char munit_top_system_str_nd[3] = "ND";
char munit_top_system_str_rd[3] = "RD";
char munit_top_system_str_th[3] = "TH";
char munit_top_system_str_space[2] = " ";
#endif

#define MUTS_MNG munit_top_system_mng
#define MUTS(o) ((struct struct_3bc93e83 *)&(o)->dt)
/* every mobj here is a string mobj: dt.string (struct_d2c56d2a) */
#define MUTS_STR(o) ((struct struct_d2c56d2a *)&(o)->dt)

static inline void muts_set_header(void)
{
    MUTS_MNG[0].pLayoutHeader = (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x455);
}

static void __MUnitTopSystem_ResetData(struct stMobj *pMobj)
{
    PS2_FRAME(0xc0);
    PS2_STACK(struct struct_76b810ec, color, 0xbc);
    PS2_STACK(char[8], buff2, 0xb0);
    PS2_STACK(char[64], buff, 0x70);
    PS2_STACK(int[4], aClassNum, 0x60);
    struct struct_3bc93e83 *pWork = MUTS(pMobj);
    unsigned int col = 0x8061a5de;   /* main sdata 0x27c248, copied as raw bits (lwc1 / swc1) */
    int pointTraining = 0;

    memcpy(&color, &col, 4);
    memset(aClassNum, 0, 0x10);

    DF_MOBJ_SetStringCopy(pWork->apMobj[1], g_gamedata[(int)pWork->player].name);
    /* (sic) the appellation text itself is the sprintf format */
    ps2_sprintf_d(buff, MAppellationMng_GetAppellation(pWork->player), 0);
    DF_MOBJ_SetStringCopy(pWork->apMobj[3], buff);

    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 5; j++) {
            struct _training_trend_st *trend = &g_gamedata[(int)pWork->player].TrainingTrend[25 * i + 5 * j];
            /* unrolled 5x in the original; (sic) the prize index is unchecked */
            aClassNum[trend[0].prize]++;
            aClassNum[trend[1].prize]++;
            aClassNum[trend[2].prize]++;
            aClassNum[trend[3].prize]++;
            aClassNum[trend[4].prize]++;
            pointTraining += g_gamedata[(int)pWork->player].trainingRecord[i][j].point;
        }
    }

    for (int c = 0; c < 3; c++) {
        ps2_sprintf_d(buff, munit_top_system_fmt_d, aClassNum[3 - c]);
        DF_MOBJ_SetStringCopy(pWork->apMobj[6 + 3 * c], buff);
    }

    for (int c = 0; c < 3; c++) {
        int temp = aClassNum[3 - c] * 100 / 150;
        if (temp >= 100) ps2_sprintf_d(buff2, munit_top_system_pad0, 0);
        else if (temp >= 10) ps2_sprintf_d(buff2, munit_top_system_pad2, 0);
        else ps2_sprintf_d(buff2, munit_top_system_pad4, 0);
        ps2_sprintf_sd(buff, munit_top_system_fmt_pct, buff2, temp);
        DF_MOBJ_SetStringCopy(pWork->apMobj[7 + 3 * c], buff);
    }

    ps2_sprintf_d(buff, munit_top_system_fmt_d, pointTraining);
    DF_MOBJ_SetStringCopy(pWork->apMobj[15], buff);

    if (g_gamedata[(int)pWork->player].arenaBattle.rank < 100) {
        /* numeric arena rank: number + ST / ND / RD / TH (UNREACHED by any replay) */
        char rank = g_gamedata[(int)pWork->player].arenaBattle.rank;
        if (rank < 100) ps2_sprintf_d(buff, munit_top_system_fmt_d, rank);
        else ps2_sprintf_d(buff, munit_top_system_fmt_c, (char)(rank - 0x23));   /* (sic) dead arm */
        DF_MOBJ_SetStringCopy(pWork->apMobj[19], buff);

        rank = g_gamedata[(int)pWork->player].arenaBattle.rank;
        if (rank >= 100) {
            buff[0] = 0;   /* (sic) dead arm: the test above cannot fail here */
        } else if (rank == 1 || rank == 0x15) {
            ps2_sprintf_d(buff, munit_top_system_str_st, 0);
        } else if (rank == 2 || rank == 0x16) {
            ps2_sprintf_d(buff, munit_top_system_str_nd, 0);
        } else if (rank == 3 || rank == 0x17) {
            ps2_sprintf_d(buff, munit_top_system_str_rd, 0);
        } else {
            ps2_sprintf_d(buff, munit_top_system_str_th, 0);
        }
        DF_MOBJ_SetStringCopy(pWork->apMobj[20], buff);

        ps2_sprintf_d(buff, munit_top_system_str_space, 0);
        DF_MOBJ_SetStringCopy(pWork->apMobj[27], buff);
        ps2_sprintf_d(buff, munit_top_system_str_space, 0);
        DF_MOBJ_SetStringCopy(pWork->apMobj[28], buff);
    } else {
        /* letter rank: the "rank" label mobj gets a fixed strid and the letter goes next to it */
        MUTS_STR(pWork->apMobj[27])->id = 0xdbbb0;
        char rank = g_gamedata[(int)pWork->player].arenaBattle.rank;
        if (rank < 100) ps2_sprintf_d(buff, munit_top_system_fmt_d, rank);   /* (sic) dead arm */
        else ps2_sprintf_d(buff, munit_top_system_fmt_c, (char)(rank - 0x23));
        DF_MOBJ_SetStringCopy(pWork->apMobj[28], buff);

        ps2_sprintf_d(buff, munit_top_system_str_space, 0);
        DF_MOBJ_SetStringCopy(pWork->apMobj[19], buff);
        ps2_sprintf_d(buff, munit_top_system_str_space, 0);
        DF_MOBJ_SetStringCopy(pWork->apMobj[20], buff);
    }

    ps2_sprintf_d(buff, munit_top_system_fmt_d, g_gamedata[(int)pWork->player].arenaBattle.pointArena);
    DF_MOBJ_SetStringCopy(pWork->apMobj[22], buff);
    ps2_sprintf_d(buff, munit_top_system_fmt_d,
                  pointTraining + g_gamedata[(int)pWork->player].arenaBattle.pointArena);
    DF_MOBJ_SetStringCopy(pWork->apMobj[25], buff);

    /* right-align the numeric strings */
    MUTS_STR(pWork->apMobj[6])->flag |= 0x10000;
    MUTS_STR(pWork->apMobj[9])->flag |= 0x10000;
    MUTS_STR(pWork->apMobj[12])->flag |= 0x10000;
    MUTS_STR(pWork->apMobj[15])->flag |= 0x10000;
    MUTS_STR(pWork->apMobj[19])->flag |= 0x10000;
    MUTS_STR(pWork->apMobj[22])->flag |= 0x10000;
    MUTS_STR(pWork->apMobj[25])->flag |= 0x10000;

    DF_MOBJ_SetStringColorAll(pWork->apMobj[0], &color);
    DF_MOBJ_SetStringColorAll(pWork->apMobj[2], &color);
    DF_MOBJ_SetStringColorAll(pWork->apMobj[4], &color);
    DF_MOBJ_SetStringColorAll(pWork->apMobj[17], &color);
    DF_MOBJ_SetStringColorAll(pWork->apMobj[24], &color);

    MUTS_STR(pWork->apMobj[0])->id = 0xdc370;
    MUTS_STR(pWork->apMobj[2])->id = 0xdc371;
    MUTS_STR(pWork->apMobj[4])->id = 0xdc372;
    MUTS_STR(pWork->apMobj[5])->id = 0xdc373;
    MUTS_STR(pWork->apMobj[8])->id = 0xdc374;
    MUTS_STR(pWork->apMobj[11])->id = 0xdc375;
    MUTS_STR(pWork->apMobj[14])->id = 0xdc376;
    MUTS_STR(pWork->apMobj[16])->id = 0xdbbaf;
    MUTS_STR(pWork->apMobj[17])->id = 0xdc377;
    MUTS_STR(pWork->apMobj[18])->id = 0xdc378;
    MUTS_STR(pWork->apMobj[21])->id = 0xdc379;
    MUTS_STR(pWork->apMobj[23])->id = 0xdbbaf;
    MUTS_STR(pWork->apMobj[24])->id = 0xdc37a;
    MUTS_STR(pWork->apMobj[26])->id = 0xdbbaf;
}

static void __MUnitTopSystem_Pkt(void)
{
}

static void __MUnitTopSystem_Draw(void)
{
}

/* Play helpers (inlined in the original: no frames of their own) */
static inline void muts_move(struct stMobj *pMobj, int x)
{
    muts_set_header();
    MOBJManager_Move(MUTS_MNG, 1, &MUTS(pMobj)->apMobj[0], x, 0, 0);
}

/* phase start: restart the move manager, put everything back and shift it by the stored vertical offset */
static void muts_slide_start(struct stMobj *pMobj)
{
    struct struct_3bc93e83 *pWork = MUTS(pMobj);

    MoveMng_Init(&pWork->stMoveMng);
    muts_set_header();
    MOBJManager_ResetPos(MUTS_MNG, 1, &pWork->apMobj[0]);
    muts_move(pMobj, pWork->valMoveOffsetV);   /* read after ResetPos */
}

/* one slide step; returns 1 when the move manager is done */
static int muts_slide_step(struct stMobj *pMobj, int dx, char mode)
{
    int v = MoveMng_GetNextMoveValue(&MUTS(pMobj)->stMoveMng, dx, 8, mode);

    if (v == -99999) return 1;
    muts_move(pMobj, v);
    return 0;
}

static void __MUnitTopSystem_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_3bc93e83 *pWork = MUTS(pMobj);

    if (pWork->phase == 0) return;

    /* the blocks run in sequence in one call and each re-reads phase, so a block that sets the next phase is
     * followed by that phase's block in the same frame (1 -> 2, 5 -> 6, 9 -> 0xa, 0xd -> 0xe, 0x17 -> 0x18,
     * 0x19 -> 0x1a). The asm order is not numeric: keep it. */
    if (pWork->phase == 1) {           /* slide in from the right */
        muts_slide_start(pMobj);
        muts_move(pMobj, 0x130);
        pWork->phase = 2;
    }
    if (pWork->phase == 2) {
        if (muts_slide_step(pMobj, -0x130, 1)) pWork->phase = 0;
    }
    if (pWork->phase == 5) {           /* slide in from the left */
        muts_slide_start(pMobj);
        muts_move(pMobj, -0x130);
        pWork->phase = 6;
    }
    if (pWork->phase == 6) {
        if (muts_slide_step(pMobj, 0x130, 1)) pWork->phase = 0;
    }
    if (pWork->phase == 9) {           /* slide out to the left */
        muts_slide_start(pMobj);
        pWork->phase = 0xa;
    }
    if (pWork->phase == 0xa) {
        if (muts_slide_step(pMobj, -0x130, 1)) pWork->phase = 0;
    }
    if (pWork->phase == 0xd) {         /* slide out to the right */
        muts_slide_start(pMobj);
        pWork->phase = 0xe;
    }
    if (pWork->phase == 0xe) {
        if (muts_slide_step(pMobj, 0x130, 1)) pWork->phase = 0;
    }
    if (pWork->phase == 0x17) {        /* SYSTEM cursor: scroll up by 0x10 */
        muts_slide_start(pMobj);
        pWork->phase = 0x18;
    }
    if (pWork->phase == 0x18) {
        if (muts_slide_step(pMobj, -0x10, 2)) {
            pWork->valMoveOffsetV = -0x10;
            pWork->phase = 0;
        }
    }
    if (pWork->phase == 0x19) {        /* SYSTEM cursor: scroll back down */
        muts_slide_start(pMobj);
        pWork->phase = 0x1a;
    }
    if (pWork->phase == 0x1a) {
        if (muts_slide_step(pMobj, 0x10, 2)) {
            pWork->valMoveOffsetV = 0;
            pWork->phase = 0;
        }
    }
    if (pWork->phase == 0x11) pWork->phase = 0;
    if (pWork->phase == 0x12) pWork->phase = 0;
}

int MUnitTopSystem_IsMove(struct stMobj *pMobj)
{
    return MUTS(pMobj)->phase != 0;
}

void MUnitTopSystem_FadeOutStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x20);
    struct struct_3bc93e83 *pWork = MUTS(pMobj);

    pWork->phase = phase;
    if (pWork->phase == 0x17 || pWork->phase == 0x19) return;   /* cursor scroll: no fade */
    muts_set_header();
    MOBJManager_FadeOut(&pWork->apMobj[0], 0, 1, MUTS_MNG);
}

void MUnitTopSystem_FadeInStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x20);
    struct struct_3bc93e83 *pWork = MUTS(pMobj);

    pWork->phase = phase;
    if (pWork->phase == 0x17 || pWork->phase == 0x19) return;   /* cursor scroll: no fade, no refill */
    muts_set_header();
    MOBJManager_FadeIn(&pWork->apMobj[0], 0, 1, MUTS_MNG);
    __MUnitTopSystem_ResetData(pMobj);
}

void MUnitTopSystem_Cleanup(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);

    muts_set_header();
    MOBJManager_Delete(&MUTS(pMobj)->apMobj[0], 1, MUTS_MNG);
}

struct stMobj *MUnitTopSystem_NewSystem(struct stMobj *pParentMobj, char mode, char player)
{
    PS2_FRAME(0x50);
    struct stMobj *obj = DF_MOBJ_GetNewObject(pParentMobj);

    if (obj == 0) return 0;

    struct struct_3bc93e83 *pWork = MUTS(obj);
    memset(pWork, 0, 0x90);
    pWork->mode = mode;
    pWork->player = player;
    if (pWork->mode == 2) pWork->valMoveOffsetV = -0x10;   /* msys: the page sits 0x10 px higher */
    else pWork->valMoveOffsetV = 0;
    obj->type = 0;
    obj->stat = 1;
    obj->count = 0;
    obj->play_func = (void (*)(void))GFN(__MUnitTopSystem_Play);
    obj->draw_func = (void (*)(void))GFN(__MUnitTopSystem_Draw);
    obj->pkt_func = (void (*)(void))GFN(__MUnitTopSystem_Pkt);
    muts_set_header();
    MOBJManager_Create(obj, &pWork->apMobj[0], 1, MUTS_MNG);
    __MUnitTopSystem_ResetData(obj);
    return obj;
}
