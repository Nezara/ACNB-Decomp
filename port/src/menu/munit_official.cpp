/* src/menu/munit_official.cpp - one "official battle card" of the ARENA OFFICIAL page (marena_official.c makes two,
 * id 0 / 1 = g_gamedata[0].officialBattle[id], 190 px apart). NewSystem builds a mobj whose dt (struct_eb1af934,
 * inline) holds 40 layout-managed child mobjs from the file's 7-record stMobjManager table (layout 0x452): frame,
 * 4 shadows, 18 icons, 14 strings, map picture, NEW tag, cursor. SetData fills the card (class letter or rank +
 * suffix, rank band / leg / weight / range icons, ranker and pilot names, +/- points; an empty card shows grey dashes);
 * Play runs the slide-in / slide-out state machine started by FadeInStart / FadeOutStart(phase).
 * The table's record 0 pLayoutHeader is rewritten before every manager call, so the hybrid keeps the table at its
 * PS2 address (nodwarf munit_official_mng).
 * Unreached (REV, asm-only): SetData's `d >= 100` / ND / RD suffix arms and the `Rank == 0 && Class == 0` card, the
 * NewSystem alloc failure, Play phases 5 / 6.
 * Original quirks kept (ported literally, marked (sic)). */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU 0x1ee22a0 / 0x1ee22b8 / 0x1ee22c8, main sdata 0x27c0a8..0x27c0b8: the table's layout names */
static char munit_official_name_frame[] = "ita_arena_official";
static char munit_official_name_element[] = "element%d";
static char munit_official_name_string[] = "string%d";
static char munit_official_name_map[] = "amap";   /* sdata 0x27c0a8 (the map said "map") */
static char munit_official_name_new[] = "new";
static char munit_official_name_cursor[] = "cursor";
/* main sdata 0x27c0a0, 0x27c0d0..0x27c0f5, OLMENU 0x1ee24a0: sprintf formats */
char munit_official_fmt_kage[7] = "kage%d";
char munit_official_fmt_d[3] = "%d";
char munit_official_str_st[3] = "ST";
char munit_official_str_nd[3] = "ND";
char munit_official_str_rd[3] = "RD";
char munit_official_str_th[3] = "TH";
char munit_official_fmt_s[3] = "%s";
char munit_official_str_dash2[3] = "--";
char munit_official_str_dash5[6] = "-----";
char munit_official_str_dash8[9] = "--------";
/* OLMENU 0x1ee22e0 (7 x 0x40) */
struct stMobjManager munit_official_mng[7] = {
    {.pLayoutName = munit_official_name_frame, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 1},
    {.pLayoutName = munit_official_fmt_kage, .pLayoutHeader = 0, .numContinue = 4, .typeMObj = 1},
    {.pLayoutName = munit_official_name_element, .pLayoutHeader = 0, .numContinue = 18, .typeMObj = 1},
    {.pLayoutName = munit_official_name_string, .pLayoutHeader = 0, .numContinue = 14, .typeMObj = 2,
     .flag = 0, .dummy_c0 = {0}, .exString = {.category = 0, .strid = 0xdbcb8}},
    {.pLayoutName = munit_official_name_map, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 1},
    {.pLayoutName = munit_official_name_new, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 1},
    {.pLayoutName = munit_official_name_cursor, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 4},
};
#endif

#define MUO_MNG munit_official_mng
#define MUO(o) ((struct struct_eb1af934 *)&(o)->dt)
#define MUO_STRDT(o) ((struct struct_d2c56d2a *)&(o)->dt)

static inline void muo_set_header(void)
{
    MUO_MNG[0].pLayoutHeader = (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x452);
}

/* the rank shown on the card: one higher when it is not above the player's own numeric rank */
static inline int muo_disp_rank(int rank)
{
    int own = g_gamedata[0].arenaBattle.rank;

    if (own < 100) return rank < own ? rank : rank + 1;
    return rank;
}

/* apMobj slots: [5 + (Class >> 1)] class icons, [8 + j] rank band, [0xc + k] leg, [0x11 + k] weight, [0x14 + k] range,
 * [23] class / rank, [24] suffix, [25] ranker name, [26] pilot name, [27] label, [31] / [34] points, [38] NEW */
static void __MUnitOfficial_SetData(struct stMobj *pMobj)
{
    PS2_FRAME(0x70);
    PS2_STACK(char[32], buff, 0x40);
    PS2_STACK(struct struct_76b810ec, color, 0x6c);
    struct struct_eb1af934 *w = MUO(pMobj);
    struct _arena_official_card_st *ob;
    int k;

    color.r = 0x40; color.g = 0x40; color.b = 0x40; color.a = 0x80;   /* sdata 0x27c0cc */
    ob = &g_gamedata[0].officialBattle[(int)w->id];
    if (ob->idRanker == 0) {   /* 0x1eb5410: empty card */
        for (k = 23; k < 37; k++) DF_MOBJ_SetStringColorAll(w->apMobj[k], &color);
        MUO_STRDT(w->apMobj[27])->id = 0xdbdd1;
        DF_MOBJ_SetDrawEnable(w->apMobj[38], 0);
        ps2_sprintf_d(buff, munit_official_str_dash2, 0);
        DF_MOBJ_SetStringCopy(w->apMobj[23], buff);
        DF_MOBJ_SetDrawEnable(w->apMobj[24], 0);
        DF_MOBJ_SetDrawEnable(w->apMobj[25], 0);
        ps2_sprintf_d(buff, munit_official_str_dash8, 0);
        DF_MOBJ_SetStringCopy(w->apMobj[26], buff);
        ps2_sprintf_d(buff, munit_official_str_dash5, 0);
        DF_MOBJ_SetStringCopy(w->apMobj[31], buff);
        ps2_sprintf_d(buff, munit_official_str_dash5, 0);
        DF_MOBJ_SetStringCopy(w->apMobj[34], buff);
        return;
    }

    if (ob->flagSearch.Class.Data != 0) {   /* class letter */
        DF_MOBJ_SetDrawEnable(w->apMobj[(ob->flagSearch.Class.Data >> 1) + 5], 1);
        ob = &g_gamedata[0].officialBattle[(int)w->id];
        buff[1] = 0;
        buff[0] = (char)((ob->flagSearch.Class.Data >> 1) + 'A');
        DF_MOBJ_SetStringCopy(w->apMobj[23], buff);
        DF_MOBJ_SetDrawEnable(w->apMobj[24], 0);
        DF_MOBJ_SetDrawEnable(w->apMobj[25], 0);
        MUO_STRDT(w->apMobj[27])->id = 0xdbdd1;
    } else if (ob->flagSearch.Rank != 0) {   /* rank number + suffix (0x1eb4e38) */
        int rank = ob->flagSearch.Rank;
        int d;
        unsigned int j;

        /* (sic) a rank outside 1..30 ends at j = 4 and enables element [12], a LegType slot */
        for (j = 0; j < 4; j++) {
            if (rank < g_aRankRangeTable[j][0]) continue;
            if (!(g_aRankRangeTable[j][1] < rank)) break;
        }
        DF_MOBJ_SetDrawEnable(w->apMobj[8 + j], 1);
        ob = &g_gamedata[0].officialBattle[(int)w->id];
        rank = ob->flagSearch.Rank;
        ps2_sprintf_d(buff, munit_official_fmt_d, muo_disp_rank(rank));
        DF_MOBJ_SetStringCopy(w->apMobj[23], buff);
        ob = &g_gamedata[0].officialBattle[(int)w->id];
        rank = ob->flagSearch.Rank;
        d = muo_disp_rank(rank);
        if (d >= 100) buff[0] = 0;   /* REV: no suffix */
        else if (d == 1 || d == 21) ps2_sprintf_d(buff, munit_official_str_st, 0);   /* the suffix is the format */
        else if (d == 2 || d == 22) ps2_sprintf_d(buff, munit_official_str_nd, 0);   /* REV */
        else if (d == 3 || d == 23) ps2_sprintf_d(buff, munit_official_str_rd, 0);   /* REV */
        else ps2_sprintf_d(buff, munit_official_str_th, 0);   /* (sic) 11..13 and 31.. -> "TH" */
        DF_MOBJ_SetStringCopy(w->apMobj[24], buff);
        ob = &g_gamedata[0].officialBattle[(int)w->id];
        /* (sic) the ranker name uses the raw Rank, the shown number may be Rank + 1 */
        ps2_sprintf_s(buff, munit_official_fmt_s,
                      (const char *)DF_MSTR_GetName(MenuMng.language, 0, ob->flagSearch.Rank + 0x7a1e7));
        DF_MOBJ_SetStringCopy(w->apMobj[25], buff);
        MUO_STRDT(w->apMobj[27])->id = 0xdbdd2;
    }
    /* Rank == 0 && Class == 0 (REV): label keeps its id */

    /* 0x1eb51a0: first set bit of each flag byte */
    ob = &g_gamedata[0].officialBattle[(int)w->id];
    for (k = 0; k < 5; k++) {
        if ((ob->flagSearch.LegType.Data >> k) & 1) {
            DF_MOBJ_SetDrawEnable(w->apMobj[0xc + k], 1);
            break;
        }
    }
    ob = &g_gamedata[0].officialBattle[(int)w->id];
    for (k = 0; k < 3; k++) {
        if ((ob->flagSearch.Weight.Data >> k) & 1) {
            DF_MOBJ_SetDrawEnable(w->apMobj[0x11 + k], 1);
            break;
        }
    }
    ob = &g_gamedata[0].officialBattle[(int)w->id];
    for (k = 0; k < 3; k++) {
        if ((ob->flagSearch.Range.Data >> k) & 1) {
            DF_MOBJ_SetDrawEnable(w->apMobj[0x14 + k], 1);
            break;
        }
    }
    ob = &g_gamedata[0].officialBattle[(int)w->id];
    if (ob->flagNew == 0) DF_MOBJ_SetDrawEnable(w->apMobj[38], 0);
    ob = &g_gamedata[0].officialBattle[(int)w->id];
    ps2_sprintf_s(buff, munit_official_fmt_s, ob->namePilot);
    DF_MOBJ_SetStringCopy(w->apMobj[26], buff);
    ob = &g_gamedata[0].officialBattle[(int)w->id];
    ps2_sprintf_d(buff, munit_official_fmt_d, ob->flagSearch.UserPointPlus);
    DF_MOBJ_SetStringCopy(w->apMobj[31], buff);
    ob = &g_gamedata[0].officialBattle[(int)w->id];
    ps2_sprintf_d(buff, munit_official_fmt_d, ob->flagSearch.UserPointMinus);
    DF_MOBJ_SetStringCopy(w->apMobj[34], buff);
}

/* place the icon rows on the "kage%d" layout entries (shifted down by id * 190) and hide them; a missing entry
 * ends the whole loop */
static void __MUnitOfficial_ResetElements(struct stMobj *pMobj)
{
    PS2_FRAME(0xf0);
    PS2_STACK(char[32], name, 0x70);
    /* sdata 0x27c0c0: first / last apMobj slot per shadow row */
    char aElement[4][2] = {{5, 0xb}, {0xc, 0x10}, {0x11, 0x13}, {0x14, 0x16}};
    struct _LAYOUT_RET_DATA layout;
    struct struct_eb1af934 *w = MUO(pMobj);
    int j, k;

    for (j = 0; j < 4; j++) {
        ps2_sprintf_d(name, munit_official_fmt_kage, j);
        if (!DF_LO_GetLayoutData((unsigned char *)LAYOUT_GetLayoutData(0x452), (unsigned char *)name, 0, &layout))
            return;
        layout.Pos.y = layout.Pos.y + w->id * 190;
        for (k = aElement[j][0]; k <= aElement[j][1]; k++) {
            UnitSprite_SetPosition(w->apMobj[k], (struct struct_214acd28 *)&layout.Pos);
            DF_MOBJ_SetDrawEnable(w->apMobj[k], 0);
        }
    }
}

static void __MUnitOfficial_Reset(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct struct_eb1af934 *w = MUO(pMobj);
    struct _arena_official_card_st *ob;
    struct struct_214acd28 pos;
    char i;

    muo_set_header();
    MOBJManager_ResetPos(MUO_MNG, 7, &w->apMobj[0]);
    MOBJManager_Move(MUO_MNG, 7, &w->apMobj[0], 0, w->id * 190, 0);
    __MUnitOfficial_ResetElements(pMobj);
    ob = &g_gamedata[0].officialBattle[(int)w->id];
    /* (sic) every card but `idRanker != 0 && Class == 0` moves strings 23 / 24 / 26 */
    if (ob->idRanker == 0 || ob->flagSearch.Class.Data != 0) {
        MUO_STRDT(w->apMobj[23])->y = MUO_STRDT(w->apMobj[23])->y + 8;
        MUO_STRDT(w->apMobj[24])->y = MUO_STRDT(w->apMobj[24])->y + 8;
        MUO_STRDT(w->apMobj[26])->y = MUO_STRDT(w->apMobj[26])->y - 8;
    }
    i = w->cursor_index;
    pos.x = 0;
    pos.y = i * 190;
    UnitSprite_SetPosition(w->apMobj[39], &pos);
    w->cursor_index = i;
}

static void __MUnitOfficial_CreateItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct struct_eb1af934 *w = MUO(pMobj);
    int k;

    muo_set_header();
    MOBJManager_Create(pMobj, &w->apMobj[0], 7, MUO_MNG);
    for (k = 0; k < 40; k++) w->apMobj[k]->draw_prio = 1;
    MUO_STRDT(w->apMobj[23])->flag |= 0x10000;
    MUO_STRDT(w->apMobj[31])->flag |= 0x10000;
    MUO_STRDT(w->apMobj[34])->flag |= 0x10000;
    MUO_STRDT(w->apMobj[28])->id = 0xdbdd3;
    MUO_STRDT(w->apMobj[29])->id = 0xdbdd4;
    MUO_STRDT(w->apMobj[30])->id = 0xdbdd5;
    MUO_STRDT(w->apMobj[32])->id = 0xdbbaf;
    MUO_STRDT(w->apMobj[33])->id = 0xdbe5c;
    MUO_STRDT(w->apMobj[35])->id = 0xdbbaf;
    MUO_STRDT(w->apMobj[36])->id = 0xdbe5d;
}

static void __MUnitOfficial_Pkt(void)
{
}

static void __MUnitOfficial_Draw(void)
{
}

/* Sequential phase tests (one call may run several): 1 -> 2 slide in, 5 -> 6 -> 0, 9 -> 0xa -> 0, 0xd -> 0xe slide
 * out. (sic) countWait is decremented on every slide frame (it runs negative); a card waits id * 2 frames. */
static void __MUnitOfficial_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x70);
    struct struct_eb1af934 *w = MUO(pMobj);
    char c;
    int v, i;

    if (w->phase == 0) return;
    if (w->phase == 1) {
        w->countWait = w->id * 2;   /* MoveMng_Init's delay slot */
        MoveMng_Init(&w->stMoveMng);
        __MUnitOfficial_Reset(pMobj);
        muo_set_header();
        MOBJManager_Move(MUO_MNG, 7, &w->apMobj[0], -0x130, 0, 0);
        w->phase = 2;
    }
    if (w->phase == 2) {
        if (w->countWait == 0) {
            muo_set_header();
            MOBJManager_FadeIn(&w->apMobj[0], 0, 7, MUO_MNG);
            DF_MOBJ_SetDrawEnable(w->apMobj[39], 0);
            DF_MOBJ_SetDrawEnable(w->apMobj[37], 0);
            for (i = 5; i < 0x17; i++) DF_MOBJ_SetDrawEnable(w->apMobj[i], 0);
            __MUnitOfficial_SetData(pMobj);
        }
        c = w->countWait;
        w->countWait = c - 1;
        if (c > 0) return;
        v = MoveMng_GetNextMoveValue(&w->stMoveMng, 0x130, 8, 1);
        if (v == -99999) {
            if (w->id == 0) {
                DF_MOBJ_SetDrawEnable(w->apMobj[39], 1);
                UnitSprite_SetFadeIn(w->apMobj[39]);
                w->apMobj[39]->draw_prio = 3;
            }
            w->phase = 0;
        } else {
            muo_set_header();
            MOBJManager_Move(MUO_MNG, 7, &w->apMobj[0], v, 0, 0);
        }
    }
    if (w->phase == 5) w->phase = 6;   /* REV */
    if (w->phase == 6) w->phase = 0;   /* REV */
    if (w->phase == 9) w->phase = 0xa;
    if (w->phase == 0xa) w->phase = 0;
    if (w->phase == 0xd) {
        w->countWait = w->id * 2;
        MoveMng_Init(&w->stMoveMng);
        __MUnitOfficial_Reset(pMobj);
        w->phase = 0xe;
    }
    if (w->phase == 0xe) {
        if (w->countWait == 0) {
            /* sdata 0x27c0c8: one icon per row */
            char aFade[4] = {5, 0xc, 0x11, 0x14};

            muo_set_header();
            MOBJManager_FadeOut(&w->apMobj[0], 0, 7, MUO_MNG);
            if (w->apMobj[37]->f_draw) UnitSprite_SetFadeOut(w->apMobj[37]);
            for (i = 0; i < 4; i++) DF_MOBJ_SetFadeOut(w->apMobj[(int)aFade[i]]);
        }
        c = w->countWait;
        w->countWait = c - 1;
        if (c > 0) return;
        v = MoveMng_GetNextMoveValue(&w->stMoveMng, -0x130, 8, 1);
        if (v == -99999) {
            w->phase = 0;
        } else {
            muo_set_header();
            MOBJManager_Move(MUO_MNG, 7, &w->apMobj[0], v, 0, 0);
        }
    }
}

void MUnitOfficial_SetArenaMapFadeIn(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    struct struct_eb1af934 *w = MUO(pMobj);

    DF_MOBJ_SetDrawEnable(w->apMobj[37], 1);
    UnitSprite_SetFadeIn(w->apMobj[37]);
}

void MUnitOfficial_SetArenaMapID(struct stMobj *pMobj, int idTex)
{
    UnitSprite_SetTexIdAll(MUO(pMobj)->apMobj[37], idTex);
}

void MUnitOfficial_ChangeCursor(struct stMobj *pMobj, int indexNew)
{
    PS2_FRAME(0x40);
    struct struct_214acd28 pos;

    pos.x = 0;
    pos.y = indexNew * 190;
    UnitSprite_SetPosition(MUO(pMobj)->apMobj[39], &pos);
    MUO(pMobj)->cursor_index = (char)indexNew;
}

int MUnitOfficial_IsMove(struct stMobj *pMobj)
{
    return MUO(pMobj)->phase != 0;
}

void MUnitOfficial_FadeOutStart(struct stMobj *pMobj, char phase)
{
    MUO(pMobj)->phase = phase;
}

void MUnitOfficial_FadeInStart(struct stMobj *pMobj, char phase)
{
    MUO(pMobj)->phase = phase;
}

void MUnitOfficial_Cleanup(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    muo_set_header();
    MOBJManager_Delete(&MUO(pMobj)->apMobj[0], 7, MUO_MNG);
}

struct stMobj *MUnitOfficial_NewSystem(struct stMobj *pParentMobj, char id)
{
    PS2_FRAME(0x40);
    struct stMobj *m = DF_MOBJ_GetNewObject(pParentMobj);

    if (m == 0) return 0;   /* REV */
    memset(MUO(m), 0, 0xb8);
    MUO(m)->id = id;
    m->type = 0;
    m->stat = 1;
    m->count = 0;
    m->play_func = (void (*)(void))GFN(__MUnitOfficial_Play);
    m->draw_func = (void (*)(void))GFN(__MUnitOfficial_Draw);
    m->pkt_func = (void (*)(void))GFN(__MUnitOfficial_Pkt);
    __MUnitOfficial_CreateItem(m);
    return m;
}
