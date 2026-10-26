/* src/menu/munit_ranker_name3.cpp - one "ranker card" of the Arena ranking list (menu/marena_ranking is the only
 * user; in Versus it is the "pick the COM opponent" list).
 * One stMobj whose dt is a struct_3f5b981b owning 8 mobjs built by a single MOBJManager_Create from layout 0x461
 * through the file's 4-entry stMobjManager table: apMobj[0] the window card ("ita_ranker"), apMobj[1..4] four
 * strings ("string_ranker%d": rank number, ordinal suffix, name for mode 1, name for mode 2), apMobj[5] the cursor
 * sprite and apMobj[6..7] the two scroll arrows ("cursor" / "arrow%d").
 * marena creates 11 cards (id 0..maxCard) plus one cursor card (id -1). Each card is a slide state machine
 * (phase + stMoveMng): FadeInStart / FadeOutStart only set `phase`, Play (play_func) then slides the four managed
 * table entries by +-0x130 px horizontally (8 frames, ease mode 1) after `countWait = id` frames of stagger, or by
 * +-0x24 px vertically (4 frames, linear mode 2) when the list scrolls. Pkt and Draw are empty (the mobjs draw
 * themselves).
 * The table's [0].pLayoutHeader is rewritten before (most of) the MOBJManager_* calls, so the hybrid keeps the
 * table at its PS2 address (nodwarf munit_ranker_name3_mng), as munit_top_system / munit_top_training do.
 * Original quirks kept (ported literally, marked (sic)). */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU 0x1ee2bd0 / 0x1ee2be0 (layout names), 0x1ee2bf0 (the table), main sdata 0x27c188 / 0x27c190 / 0x27c198.. */
static char munit_ranker_name3_name_window[] = "ita_ranker";
static char munit_ranker_name3_name_string[] = "string_ranker%d";
static char munit_ranker_name3_name_cursor[] = "cursor";
static char munit_ranker_name3_name_arrow[] = "arrow%d";
struct stMobjManager munit_ranker_name3_mng[4] = {
    {.pLayoutName = munit_ranker_name3_name_window, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 1,
     .flag = 0, .dummy_c0 = {0}},
    {.pLayoutName = munit_ranker_name3_name_string, .pLayoutHeader = 0, .numContinue = 4, .typeMObj = 2,
     .flag = 0, .dummy_c0 = {0}, .exString = {.category = 0, .strid = 0xdbcb8}},
    {.pLayoutName = munit_ranker_name3_name_cursor, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 4,
     .flag = 0, .dummy_c0 = {0}},
    {.pLayoutName = munit_ranker_name3_name_arrow, .pLayoutHeader = 0, .numContinue = 2, .typeMObj = 4,
     .flag = 0, .dummy_c0 = {0}},
};
/* all six are passed to sprintf AS THE FORMAT (sic) */
char munit_ranker_name3_str_space[2] = " ";
char munit_ranker_name3_fmt_d[3] = "%d";
char munit_ranker_name3_str_st[3] = "ST";
char munit_ranker_name3_str_nd[3] = "ND";
char munit_ranker_name3_str_rd[3] = "RD";
char munit_ranker_name3_str_th[3] = "TH";
#endif

#define MURN_MNG munit_ranker_name3_mng
#define MURN(o) ((struct struct_3f5b981b *)&(o)->dt)
/* apMobj[1..4] are string mobjs: dt.string (struct_d2c56d2a) */
#define MURN_STR(o) ((struct struct_d2c56d2a *)&(o)->dt)

#define MURN_LAYOUT 0x461
#define MURN_MOVE_END (-99999)

static inline void murn_set_header(void)
{
    MURN_MNG[0].pLayoutHeader = (struct _LO_HEADER *)LAYOUT_GetLayoutData(MURN_LAYOUT);
}

static void __MUnitRankerName3_Pkt(void)
{
}

static void __MUnitRankerName3_Draw(void)
{
}

/* Play helpers (inlined in the original: no frames of their own) */

/* hide the three sprites (cursor + the two arrows) of a card that is fading with the group */
static inline void murn_sprites_off(struct struct_3f5b981b *pWork)
{
    DF_MOBJ_SetDrawEnable(pWork->apMobj[5], 0);
    DF_MOBJ_SetDrawEnable(pWork->apMobj[6], 0);
    DF_MOBJ_SetDrawEnable(pWork->apMobj[7], 0);
}

/* the window + the four strings of the spare card (id == maxCard) are never shown */
static inline void murn_hide_card(struct struct_3f5b981b *pWork)
{
    DF_MOBJ_SetDrawEnable(pWork->apMobj[0], 0);
    for (int i = 1; i < 5; i++) DF_MOBJ_SetDrawEnable(pWork->apMobj[i], 0);
}

/* after every move step the sprites are pinned back and the cursor put on its row */
static void murn_repos(struct stMobj *pMobj)
{
    struct struct_3f5b981b *pWork = MURN(pMobj);
    struct struct_214acd28 pos;
    struct struct_214acd28 posCursor;

    pos.x = 0;
    pos.y = 0;
    UnitSprite_SetPosition(pWork->apMobj[5], &pos);
    pos.x = 0;
    pos.y = 0;
    UnitSprite_SetPosition(pWork->apMobj[6], &pos);
    pos.x = 0;
    pos.y = 0;
    UnitSprite_SetPosition(pWork->apMobj[7], &pos);

    char index = pWork->cursor_index;
    posCursor.x = 0;
    posCursor.y = (int)index * 36;
    UnitSprite_SetPosition(pWork->apMobj[5], &posCursor);
    pWork->cursor_index = index;   /* (sic) the same byte written back */
}

/* restart the move manager, put the group back and place it on its row */
static inline void murn_slide_start(struct stMobj *pMobj)
{
    struct struct_3f5b981b *pWork = MURN(pMobj);

    MoveMng_Init(&pWork->stMoveMng);
    murn_set_header();
    MOBJManager_ResetPos(MURN_MNG, 4, &pWork->apMobj[0]);
    MOBJManager_Move(MURN_MNG, 4, &pWork->apMobj[0], 0, (int)pWork->id * 36, 0);   /* id re-read from memory */
}

static void __MUnitRankerName3_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0xa0);
    struct struct_3f5b981b *pWork = MURN(pMobj);

    if (pWork->phase == 0) return;

    /* The blocks run in sequence in one call and each re-reads `phase` from memory, so a block that sets the next
     * phase is followed by that phase's block in the same frame (0x13 -> 0x14, 0x15 -> 0x16, 1 -> 2, 5 -> 6 -> 0,
     * 9 -> 0xa -> 0, 0xd -> 0xe). Keep the asm order. */
    if (pWork->phase == 0x13) {           /* list scrolls up: this card slides up by 0x24 */
        MoveMng_Init(&pWork->stMoveMng);
        if (pWork->id == 0) {
            murn_set_header();
            MOBJManager_FadeOut(&pWork->apMobj[0], 0, 4, MURN_MNG);
        } else if (pWork->id == pWork->maxCard) {
            murn_set_header();
            MOBJManager_FadeIn(&pWork->apMobj[0], 0, 4, MURN_MNG);
            if (pWork->flagFade == 0) murn_sprites_off(pWork);
        }
        pWork->phase = 0x14;
    }
    if (pWork->phase == 0x14) {
        int v = MoveMng_GetNextMoveValue(&pWork->stMoveMng, -0x24, 4, 2);
        if (v == MURN_MOVE_END) {
            pWork->phase = 0;
        } else {
            murn_set_header();
            MOBJManager_Move(MURN_MNG, 4, &pWork->apMobj[0], 0, v, 0);
            murn_repos(pMobj);
        }
    }
    if (pWork->phase == 0x15) {           /* list scrolls down: this card slides down by 0x24 */
        MoveMng_Init(&pWork->stMoveMng);
        if (pWork->id == pWork->maxCard - 1) {
            murn_set_header();
            MOBJManager_FadeOut(&pWork->apMobj[0], 0, 4, MURN_MNG);
        } else if (pWork->id == pWork->maxCard) {
            murn_set_header();
            MOBJManager_FadeIn(&pWork->apMobj[0], 0, 4, MURN_MNG);
            if (pWork->flagFade == 0) murn_sprites_off(pWork);
        }
        pWork->phase = 0x16;
    }
    if (pWork->phase == 0x16) {
        int v = MoveMng_GetNextMoveValue(&pWork->stMoveMng, 0x24, 4, 2);
        if (v == MURN_MOVE_END) {
            pWork->phase = 0;
        } else {
            murn_set_header();
            MOBJManager_Move(MURN_MNG, 4, &pWork->apMobj[0], 0, v, 0);
            murn_repos(pMobj);
        }
    }
    if (pWork->phase == 1) {              /* slide in from the right, staggered by id frames */
        pWork->countWait = pWork->id;
        if (pWork->id == pWork->maxCard) murn_hide_card(pWork);
        murn_slide_start(pMobj);
        murn_set_header();
        MOBJManager_Move(MURN_MNG, 4, &pWork->apMobj[0], -0x130, 0, 0);
        murn_repos(pMobj);
        pWork->phase = 2;
    }
    if (pWork->phase == 2) {
        if (pWork->countWait == 0 && pWork->id != pWork->maxCard) {
            murn_set_header();
            MOBJManager_FadeIn(&pWork->apMobj[0], 0, 4, MURN_MNG);
            if (pWork->flagFade == 0) murn_sprites_off(pWork);
        }
        char wait = pWork->countWait;
        pWork->countWait = wait - 1;
        if (wait > 0) return;
        int v = MoveMng_GetNextMoveValue(&pWork->stMoveMng, 0x130, 8, 1);
        if (v == MURN_MOVE_END) {
            pWork->phase = 0;
        } else {
            murn_set_header();
            MOBJManager_Move(MURN_MNG, 4, &pWork->apMobj[0], v, 0, 0);
            murn_repos(pMobj);
        }
    }
    /* (sic) phases 5 / 9 are transient: they only clear `phase` in the same call */
    if (pWork->phase == 5) pWork->phase = 6;
    if (pWork->phase == 6) pWork->phase = 0;
    if (pWork->phase == 9) pWork->phase = 0xa;
    if (pWork->phase == 0xa) pWork->phase = 0;
    if (pWork->phase == 0xd) {            /* slide out to the left, staggered by id frames */
        pWork->countWait = pWork->id;
        if (pWork->id == pWork->maxCard) murn_hide_card(pWork);
        murn_slide_start(pMobj);          /* (sic) no second Move and no reposition here */
        pWork->phase = 0xe;
    }
    if (pWork->phase == 0xe) {
        if (pWork->countWait == 0) {      /* (sic) no `id != maxCard` test, unlike phase 2 */
            murn_set_header();
            MOBJManager_FadeOut(&pWork->apMobj[0], 0, 4, MURN_MNG);
        }
        char wait = pWork->countWait;
        pWork->countWait = wait - 1;
        if (wait > 0) return;
        int v = MoveMng_GetNextMoveValue(&pWork->stMoveMng, -0x130, 8, 1);
        if (v == MURN_MOVE_END) {
            pWork->phase = 0;
        } else {
            murn_set_header();
            MOBJManager_Move(MURN_MNG, 4, &pWork->apMobj[0], v, 0, 0);
            murn_repos(pMobj);
        }
    }
}

void MUnitRankerName3_FadeOutCursor(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    struct struct_3f5b981b *pWork = MURN(pMobj);

    UnitSprite_SetFadeOut(pWork->apMobj[5]);
    UnitSprite_SetFadeOut(pWork->apMobj[6]);
    UnitSprite_SetFadeOut(pWork->apMobj[7]);
    pWork->flagFade = 0;
}

void MUnitRankerName3_FadeInCursor(struct stMobj *pMobj, char enableArrow)
{
    PS2_FRAME(0x30);
    struct struct_3f5b981b *pWork = MURN(pMobj);

    DF_MOBJ_SetDrawEnable(pWork->apMobj[5], 1);
    UnitSprite_SetFadeIn(pWork->apMobj[5]);
    if (enableArrow) {
        DF_MOBJ_SetDrawEnable(pWork->apMobj[6], 1);
        DF_MOBJ_SetDrawEnable(pWork->apMobj[7], 1);
        UnitSprite_SetFadeIn(pWork->apMobj[6]);
        UnitSprite_SetFadeIn(pWork->apMobj[7]);
    }
    pWork->flagFade = 1;
}

void MUnitRankerName3_SetPos(struct stMobj *pMobj, char id)
{
    PS2_FRAME(0x40);
    struct struct_3f5b981b *pWork = MURN(pMobj);

    murn_set_header();
    MOBJManager_ResetPos(MURN_MNG, 4, &pWork->apMobj[0]);
    /* (sic) the argument id is used for the row but never stored in pWork->id */
    MOBJManager_Move(MURN_MNG, 4, &pWork->apMobj[0], 0, (int)id * 36, 0);
}

void MUnitRankerName3_SetID(struct stMobj *pMobj, char id)
{
    MURN(pMobj)->id = id;
}

void MUnitRankerName3_SetStrings(struct stMobj *pMobj, char rank, char *pNamePilot)
{
    PS2_FRAME(0x80);
    PS2_STACK(char[32], buff, 0x60);
    struct struct_3f5b981b *pWork = MURN(pMobj);

    ps2_sprintf_d(buff, munit_ranker_name3_str_space, 0);
    for (int i = 1; i < 5; i++) {
        DF_MOBJ_SetDrawEnable(pWork->apMobj[i], 0);
        DF_MOBJ_SetStringCopy(pWork->apMobj[i], buff);   /* the same " " for all four */
    }

    if (pWork->mode == 1) {
        /* the Arena Top list: rank number + ordinal suffix + name (UNREACHED by any replay) */
        if (rank != 0) {
            ps2_sprintf_d(buff, munit_ranker_name3_fmt_d, rank);
            DF_MOBJ_SetStringCopy(pWork->apMobj[1], buff);
            if (rank < 100) {
                /* (sic) 31 / 41 / ... get "TH" but 21 / 22 / 23 are special-cased */
                if (rank == 1 || rank == 0x15) ps2_sprintf_d(buff, munit_ranker_name3_str_st, 0);
                else if (rank == 2 || rank == 0x16) ps2_sprintf_d(buff, munit_ranker_name3_str_nd, 0);
                else if (rank == 3 || rank == 0x17) ps2_sprintf_d(buff, munit_ranker_name3_str_rd, 0);
                else ps2_sprintf_d(buff, munit_ranker_name3_str_th, 0);
            } else {
                buff[0] = 0;
            }
            DF_MOBJ_SetStringCopy(pWork->apMobj[2], buff);
        }
        DF_MOBJ_SetStringCopy(pWork->apMobj[3], pNamePilot);
        /* (sic) runs for rank 0 too, fading in the two empty strings */
        for (int i = 1; i < 4; i++) {
            DF_MOBJ_SetDrawEnable(pWork->apMobj[i], 1);
            DF_MOBJ_SetFadeIn(pWork->apMobj[i]);
        }
    } else {
        DF_MOBJ_SetStringCopy(pWork->apMobj[4], pNamePilot);
        DF_MOBJ_SetDrawEnable(pWork->apMobj[4], 1);
        DF_MOBJ_SetFadeIn(pWork->apMobj[4]);
    }
}

void MUnitRankerName3_ChangeCursor(struct stMobj *pMobj, int indexNew)
{
    PS2_FRAME(0x40);
    struct struct_3f5b981b *pWork = MURN(pMobj);
    struct struct_214acd28 pos;

    pos.x = 0;
    pos.y = indexNew * 36;
    UnitSprite_SetPosition(pWork->apMobj[5], &pos);
    pWork->cursor_index = (char)indexNew;
}

int MUnitRankerName3_IsMove(struct stMobj *pMobj)
{
    return MURN(pMobj)->phase != 0;
}

void MUnitRankerName3_FadeOutStart(struct stMobj *pMobj, char phase)
{
    MURN(pMobj)->phase = phase;
}

void MUnitRankerName3_FadeInStart(struct stMobj *pMobj, char phase)
{
    MURN(pMobj)->phase = phase;
}

void MUnitRankerName3_Cleanup(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);

    murn_set_header();
    MOBJManager_Delete(&MURN(pMobj)->apMobj[0], 4, MURN_MNG);
}

struct stMobj *MUnitRankerName3_NewSystem(struct stMobj *pParentMobj, char id, char maxCard, char mode)
{
    PS2_FRAME(0x60);
    struct stMobj *obj = DF_MOBJ_GetNewObject(pParentMobj);

    if (obj == 0) return 0;

    struct struct_3f5b981b *pWork = MURN(obj);
    memset(pWork, 0, 0x3c);
    pWork->id = id;
    pWork->maxCard = maxCard;
    pWork->mode = mode;
    obj->type = 0;
    obj->stat = 1;
    obj->count = 0;
    obj->play_func = (void (*)(void))GFN(__MUnitRankerName3_Play);
    obj->draw_func = (void (*)(void))GFN(__MUnitRankerName3_Draw);
    obj->pkt_func = (void (*)(void))GFN(__MUnitRankerName3_Pkt);
    murn_set_header();
    MOBJManager_Create(obj, &pWork->apMobj[0], 4, MURN_MNG);
    MURN_STR(pWork->apMobj[1])->flag |= 0x10000;
    return obj;
}
