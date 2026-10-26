/* src/menu/munit_program.cpp - the Training "program" window of the mission-select screens (munit_top_training and
 * the arena / ranking pages open it through munit_mng).
 * One stMobj (NewSystem) whose dt is a struct_eacfa163 owning 29 mobjs built by ONE MOBJManager_Create from layout
 * 0x43f through the file's 12-entry stMobjManager table (window, five program strings, medals, map frame, training
 * strings, passcode and movie strings), eight extra hidden string mobjs (apMobjString), a movie window, a briefmap
 * (CD sector 0x3e8 + 25 * category + 5 * detail + level, read into a DF_MMM buffer) and a child "graph" mobj with
 * its own play / draw funcs. Play is the slide state machine (phase + mode + mode2 + stMoveMng): FadeInStart /
 * FadeOutStart (still interpreted) set the phase, Play slides the managed mobjs by +-0x130 px in 8 frames and starts
 * the briefmap load after a countdown once the movie is done. Pkt and Draw are empty (the mobjs draw themselves).
 * Native here: PlayGraph, DeleteItem (+ Cleanup), CreateItem, Pkt, Draw, Play, NewSystem. The other 24 functions
 * (DrawGraph, FadeIn/OutItem, Reset, Move, UpdateBriefmap, ...) stay interpreted and are called through thunks.
 * The table's [0].pLayoutHeader is rewritten before every MOBJManager_* call: the hybrid keeps the DWARF table at
 * its PS2 address, the source port has a file-local copy.
 * Original quirks kept (ported literally, marked (sic)). */
#include <string.h>
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU 0x1ee26a0.. (layout names), main sdata 0x27c130 / 0x27c138, the table OLMENU 0x1ee2760 */
static char munit_program_name_win[] = "win_program";
static char munit_program_name_string[] = "string_program%d";
static char munit_program_name_medal_under[] = "medal_under";
static char munit_program_name_medal[] = "medal%d";
static char munit_program_name_map_under[] = "map_under";
static char munit_program_name_map_over[] = "map_over";
static char munit_program_name_training[] = "string_training_s%d";
static char munit_program_name_passbox[] = "passbox";
static char munit_program_name_passcode[] = "string_passcode";
static char munit_program_name_pass[] = "string_pass%d";
static char munit_program_name_change[] = "string_change%d";
static char munit_program_name_movie[] = "string_movie%d";
#define MUP_SPR(n, num, type) {.pLayoutName = n, .pLayoutHeader = 0, .numContinue = num, .typeMObj = type, \
                               .flag = 0, .dummy_c0 = {0}}
#define MUP_STR(n, num) {.pLayoutName = n, .pLayoutHeader = 0, .numContinue = num, .typeMObj = 2, \
                         .flag = 0, .dummy_c0 = {0}, .exString = {.category = 0, .strid = 0xdbcb8}}
static struct stMobjManager munit_program_mng[12] = {
    MUP_SPR(munit_program_name_win, 1, 3),
    MUP_STR(munit_program_name_string, 5),
    MUP_SPR(munit_program_name_medal_under, 1, 1),
    MUP_SPR(munit_program_name_medal, 3, 1),
    MUP_SPR(munit_program_name_map_under, 1, 1),
    MUP_SPR(munit_program_name_map_over, 1, 1),
    MUP_STR(munit_program_name_training, 5),
    MUP_SPR(munit_program_name_passbox, 1, 1),
    MUP_STR(munit_program_name_passcode, 1),
    MUP_STR(munit_program_name_pass, 4),
    MUP_STR(munit_program_name_change, 4),
    MUP_STR(munit_program_name_movie, 2),
};
#undef MUP_SPR
#undef MUP_STR
#define MUP_MNG munit_program_mng
#else
#define MUP_MNG __MUnitProgram_ResetBriefmap__aMobjData
#endif

#define MUP(o) ((struct struct_eacfa163 *)&(o)->dt)
#define MUP_GRAPH(o) ((struct struct_10f2ef38 *)&(o)->dt)
#define MUP_STRDT(o) ((struct struct_d2c56d2a *)&(o)->dt)

#define MUP_LAYOUT 0x43f
#define MUP_BRIEFMAP_FILE 0x3e8
#define MUP_MOVE_END (-99999)

static inline void mup_set_header(void)
{
    MUP_MNG[0].pLayoutHeader = (struct _LO_HEADER *)LAYOUT_GetLayoutData(MUP_LAYOUT);
}

/* f_draw = 0, then f_erase = 1 (two separate byte writes in the original) */
static inline void mup_erase(struct stMobj *o)
{
    o->f_draw = 0;
    o->f_erase = 1;
}

/* graph mobj play_func: restart the graph when the selected detail / level changed */
static void __MUnitProgram_PlayGraph(struct stMobj *pMobj)
{
    struct struct_10f2ef38 *g = MUP_GRAPH(pMobj);
    char detail = MenuMng.detail_index;

    if (detail == g->detail_index && MenuMng.level_index == g->level_index) return;
    g->detail_index = detail;
    g->level_index = MenuMng.level_index;
    g->step = 1;
    g->count = 0;
}

static void __MUnitProgram_DeleteItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    struct struct_eacfa163 *d = MUP(pMobj);

    mup_set_header();
    MOBJManager_Delete(&d->apMobj[0], 12, MUP_MNG);
    if (d->pMovieWnd) {
        MovieWnd_Delete(d->pMovieWnd);
        d->pMovieWnd = 0;
    }
    if (d->pMobjGraph) {
        mup_erase(d->pMobjGraph);
        d->pMobjGraph = 0;
    }
    if (d->pMobjBriefmap) {   /* (sic) tested twice */
        mup_erase(d->pMobjBriefmap);
        d->pMobjBriefmap = 0;
    }
    if (d->texidBriefmap) MenuTex_UnRegisterSingleTex(d->texidBriefmap);   /* (sic) texid not cleared */
    DF_MMM_Free(0, d->pBriefmap);   /* (sic) even when NULL; pointer not cleared */
    for (int i = 0; i < 8; i++) {
        if (d->apMobjString[i]) {
            mup_erase(d->apMobjString[i]);
            d->apMobjString[i] = 0;
        }
    }
}

static void __MUnitProgram_CreateItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct struct_eacfa163 *d = MUP(pMobj);
    /* colorYellow: main sdata 0x27c140 word 0x8061a5de, copied to sp+0x4c */
    struct struct_76b810ec colorYellow = {0xde, 0xa5, 0x61, 0x80};

    mup_set_header();
    MOBJManager_Create(pMobj, &d->apMobj[0], 12, MUP_MNG);
    d->apMobj[27]->draw_prio = 3;
    d->apMobj[28]->draw_prio = 3;
    MUP_STRDT(d->apMobj[4])->flag |= 0x10000;
    DF_MOBJ_SetStringColorAll(d->apMobj[24], &colorYellow);
    DF_MOBJ_SetStringColorAll(d->apMobj[26], &colorYellow);
    for (int i = 0; i < 8; i++) {
        d->apMobjString[i] = DF_MOBJ_NewStringBase(pMobj, 7, 0, 0, &colorYellow, 0, 0);
        MUP_STRDT(d->apMobjString[i])->flag = 1;
        MUP_STRDT(d->apMobjString[i])->ref_type = 0;
        MUP_STRDT(d->apMobjString[i])->id = 0xdbcb8;
        d->apMobjString[i]->draw_prio = 3;
        DF_MOBJ_SetDrawEnable(d->apMobjString[i], 0);
    }
}

static void __MUnitProgram_Pkt(void)
{
}

static void __MUnitProgram_Draw(void)
{
}

static void __MUnitProgram_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x60);
    struct struct_eacfa163 *d = MUP(pMobj);

    /* The blocks run in sequence in one call and each re-reads `phase`, so 1 -> 2, 0xd -> 0xe and 5 -> 6 run in the
     * same frame; blocks 2 / 0xe / 6 return straight to the epilogue. Keep the asm order. */
    if (d->phase == 1) {
        if (d->mode != 2 && d->mode2 != 2) __MUnitProgram_FadeInItem(pMobj);
        if (d->mode == 3 && d->mode2 != 2) {
            __MUnitProgram_Reset(pMobj);
            __MUnitProgram_Move(pMobj, 0x130, 0);
        }
        d->phase = 2;
    }
    if (d->phase == 2) {
        int v = MoveMng_GetNextMoveValue(&d->stMoveMng, d->mode2 == 2 ? 0x130 : -0x130, 8, 1);
        if (v == MUP_MOVE_END) {
            __MUnitProgram_Reset(pMobj);
            if (d->mode == 2) {
                DF_MOBJ_SetDrawEnable(d->apMobj[1], 1);
                DF_MOBJ_SetDrawEnable(d->apMobj[2], 1);
                DF_MOBJ_SetDrawEnable(d->apMobj[3], 1);
                DF_MOBJ_SetFadeIn(d->apMobj[1]);
                DF_MOBJ_SetFadeIn(d->apMobj[2]);
                DF_MOBJ_SetFadeIn(d->apMobj[3]);
                /* (sic) string ids set after the FadeIn calls */
                MUP_STRDT(d->apMobj[1])->id = 0xdbbc8 + MenuMng.category_index * 10 + MenuMng.detail_index;
                MUP_STRDT(d->apMobj[2])->id = 0xdbc09;
                MUP_STRDT(d->apMobj[3])->id = 0xdbc04 + MenuMng.level_index;
                __MUnitProgram_Move(pMobj, -0x130, 0);
            }
            d->phase = 0;
            d->mode2 = 0;
        } else {
            __MUnitProgram_Move(pMobj, v, 0);
        }
        return;
    }
    if (d->phase == 0xd) {
        if (d->mode2 == 2) __MUnitProgram_FadeInItem(pMobj);
        if (d->mode != 2 && d->mode2 != 2) __MUnitProgram_FadeOutItem(pMobj);
        d->phase = 0xe;
    }
    if (d->phase == 0xe) {
        /* (sic) both mode2 arms slide by +0x130 */
        int v = MoveMng_GetNextMoveValue(&d->stMoveMng, 0x130, 8, 1);
        if (v == MUP_MOVE_END) {
            if (d->mode == 1) {
                DF_MOBJ_SetFadeOut(d->apMobj[1]);
                DF_MOBJ_SetFadeOut(d->apMobj[2]);
                DF_MOBJ_SetFadeOut(d->apMobj[3]);
            }
            d->phase = 0;
            d->mode2 = 0;
        } else {
            __MUnitProgram_Move(pMobj, v, 0);
        }
        return;
    }
    if (d->phase == 5) {
        __MUnitProgram_Reset(pMobj);
        d->mode = 2;
        __MUnitProgram_FadeInItem(pMobj);
        d->phase = 6;
    }
    if (d->phase == 6) {
        int v = MoveMng_GetNextMoveValue(&d->stMoveMng, -0x130, 8, 1);
        if (v == MUP_MOVE_END) {
            d->phase = 0;
            d->mode2 = 0;
        } else {
            __MUnitProgram_Move(pMobj, v, 0);
        }
        return;
    }
    if (d->phase == 0x11) {
        __MUnitProgram_FadeInItem(pMobj);
        d->phase = 0;
    }
    if (d->phase == 0x12) {
        __MUnitProgram_FadeOutItem(pMobj);
        d->phase = 0;
    }

    /* trend medal highlight */
    if (d->flagFadeIn == 1 && d->phase == 0 && d->mode != 3) {
        struct _training_trend_st *lv =
            GetTrainingTrendLv(MenuMng.category_index, MenuMng.detail_index, MenuMng.level_index);
        signed char typeClass = (signed char)lv->prize;   /* low byte only */
        for (int i = 7; i < 10; i++) DF_MOBJ_SetDrawEnable(d->apMobj[i], 0);
        if (typeClass != 0) DF_MOBJ_SetDrawEnable(d->apMobj[10 - typeClass], 1);   /* (sic) unbounded */
    }

    /* movie done (or never playing) -> after a 5-frame wait, start the briefmap read */
    if (d->pMovieWnd &&
        (MovieWnd_IsEnd(d->pMovieWnd) || !MovieWnd_IsPlay(d->pMovieWnd) || MovieWnd_IsWait(d->pMovieWnd)) &&
        d->stepProceedSelectToProgram == 1) {
        if (d->countWait >= 5) {
            Cdvd_ReadLsnBlind(MUP_BRIEFMAP_FILE + MenuMng.category_index * 25 + MenuMng.detail_index * 5 +
                                  MenuMng.level_index,
                              d->pBriefmap, 0, 0);
            d->stepLoadBriefmap = 1;
            d->stepProceedSelectToProgram = 2;
            d->countWait = 0;
        } else {
            d->countWait = d->countWait + 1;
        }
    }

    if (d->mode != 1 || d->stepProceedSelectToProgram == 2) __MUnitProgram_UpdateBriefmap(pMobj);
    if (d->stepProceedSelectToProgram == 3) {
        /* (sic) pMobjBriefmap read without a null check */
        const struct stMobj *bm =
            d->pMobjBriefmap ? d->pMobjBriefmap : (const struct stMobj *)PS2_NULL_MEM();
        if (bm->stat == 0) {
            if (d->pMovieWnd) {
                MovieWnd_Delete(d->pMovieWnd);
                d->pMovieWnd = 0;
            }
            d->stepProceedSelectToProgram = 4;
        }
    }
}

void MUnitProgram_Cleanup(struct stMobj *pMobj)
{
    __MUnitProgram_DeleteItem(pMobj);
}

struct stMobj *MUnitProgram_NewSystem(struct stMobj *pParentMobj, char mode)
{
    PS2_FRAME(0x40);
    struct stMobj *obj = DF_MOBJ_GetNewObject(pParentMobj);

    if (obj == 0) return 0;

    struct struct_eacfa163 *d = MUP(obj);
    memset(d, 0, 0xd0);
    d->mode = mode;
    obj->type = 0;
    obj->stat = 1;
    obj->count = 0;
    obj->play_func = (void (*)(void))GFN(__MUnitProgram_Play);
    obj->draw_func = (void (*)(void))GFN(__MUnitProgram_Draw);
    obj->pkt_func = (void (*)(void))GFN(__MUnitProgram_Pkt);
    __MUnitProgram_CreateItem(obj);

    d->pMobjGraph = DF_MOBJ_GetNewObject(obj);
    if (d->pMobjGraph == 0) return 0;   /* (sic) nothing freed */

    struct struct_10f2ef38 *g = MUP_GRAPH(d->pMobjGraph);
    memset(g, 0, 8);
    g->pParent = obj;
    g->step = 1;
    g->detail_index = -1;
    g->level_index = -1;
    d->pMobjGraph->count = 0;
    d->pMobjGraph->play_func = (void (*)(void))GFN(__MUnitProgram_PlayGraph);
    d->pMobjGraph->draw_func = (void (*)(void))GFN(__MUnitProgram_DrawGraph);
    d->pMobjGraph->pkt_func = 0;
    d->pMobjGraph->f_draw = 0;

    d->pBriefmap = (char *)DF_MMM_Alloc(0, File_GetSize(MUP_BRIEFMAP_FILE));
    if (d->pBriefmap == 0) return 0;   /* (sic) leaks the mobjs */
    return obj;
}
