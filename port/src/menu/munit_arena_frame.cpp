/* src/menu/munit_arena_frame.cpp - the framed header panel of the arena screens (Ranking list, Match, Official,
 * Result): 13 layout-managed mobjs (6 "waku%d" sprites, 7 "string%d" strings) built from layout 0x451 by the file's
 * 2-entry stMobjManager table, 8 extra strings made with DF_MOBJ_NewStringBase (result screen text: points, rank and
 * their labels), and a child "briefing" mobj hosting a MenuBriefingPlayer (the scrolling briefing text of the
 * selected ranker; interpreted menu/mbriefingplayer.c). The frame mobj's work is a struct_8eedc3e8, the briefing
 * mobj's a struct_34dae972.
 * Driven by the parent arena screens (NewSystem / FadeInStart / FadeOutStart / SetData / SetResultData /
 * ChangeMode / Cleanup); its own Play forwards to MenuBriefingPlayer_Play and collapses the one-shot phase byte,
 * Pkt / Draw are empty. The briefing child fades its text alpha with its own play function (PlayBriefing, 7 frames)
 * and draws it with DrawBriefing.
 * The table's entry 0 pLayoutHeader is rewritten before every MOBJManager_* call, so the hybrid keeps the table at
 * its PS2 address (nodwarf munit_arena_frame_mng).
 * Original quirks kept (ported literally, marked (sic)). */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

#ifdef ACNB_SOURCEPORT
/* main sdata 0x27bf38 / OLMENU 0x1ee1150..0x1ee11df */
static char munit_arena_frame_name_waku[] = "waku%d";
static char munit_arena_frame_name_string[] = "string%d";
struct stMobjManager munit_arena_frame_mng[2] = {
    {.pLayoutName = munit_arena_frame_name_waku, .pLayoutHeader = 0, .numContinue = 6, .typeMObj = 1},
    /* (sic) pLayoutHeader stays 0: MOBJManager reuses the previous record's header */
    {.pLayoutName = munit_arena_frame_name_string, .pLayoutHeader = 0, .numContinue = 7, .typeMObj = 2,
     .flag = 0, .dummy_c0 = {0}, .exString = {.category = 0, .strid = 0xdbcb8}},
};
/* main sdata 0x27bf44..0x27bf66 */
char munit_arena_frame_fmt_d[3] = "%d";
char munit_arena_frame_fmt_c[3] = "%c";
char munit_arena_frame_str_st[3] = "ST";
char munit_arena_frame_str_nd[3] = "ND";
char munit_arena_frame_str_rd[3] = "RD";
char munit_arena_frame_str_th[3] = "TH";
char munit_arena_frame_str_string2[8] = "string2";
#endif

#define MUAF_MNG munit_arena_frame_mng
#define MUAF(o) ((struct struct_8eedc3e8 *)&(o)->dt)
#define MUAF_BRIEF(o) ((struct struct_34dae972 *)&(o)->dt)
#define MUAF_STR(o) ((struct struct_d2c56d2a *)&(o)->dt)

static inline void muaf_set_header(void)
{
    MUAF_MNG[0].pLayoutHeader = (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x451);
}

/* f_draw = 0, f_erase = 1 (two read-modify-writes of byte 0) */
static inline void muaf_erase(struct stMobj *o)
{
    o->f_draw = 0;
    o->f_erase = 1;
}

/* yellow text colour 0x8061a5de (main sdata 0x27bf34 / 0x27bf40), copied as raw bits (lwc1 / swc1) */
static inline void muaf_set_yellow(struct struct_76b810ec *c)
{
    unsigned int col = 0x8061a5de;

    memcpy(c, &col, 4);
}

/* x of a two-piece label centred at `centre`: (sum + (sum < 0)) >> 1 */
static inline int muaf_left(int centre, int w0, int w1)
{
    int sum = w0 + w1;

    if (sum < 0) sum += 1;
    return centre - (sum >> 1);
}

/* briefing mobj draw_func: the briefing text at the current fade alpha, only while the frame shows mode 1 */
static void __MUnitArenaFrame_DrawBriefing(struct stMobj *pMobj)
{
    PS2_FRAME(0x10);
    struct struct_34dae972 *b = MUAF_BRIEF(pMobj);
    struct struct_8eedc3e8 *w = MUAF(b->pParent);

    if (b != 0 && b->pBriefPlay != 0 && w->modeShow == 1) MenuBriefingPlayer_DrawText(b->pBriefPlay, b->alpha);
}

/* briefing mobj play_func while fading: stat 1 = fade in, stat 2 = fade out, 7 frames each (count 0..6) */
static void __MUnitArenaFrame_PlayBriefing(struct stMobj *pMobj)
{
    struct struct_34dae972 *b = MUAF_BRIEF(pMobj);

    if (pMobj->stat == 2) {
        if (pMobj->count >= 7) {
            pMobj->stat = 5;   /* (sic) fade-out ends in stat 5, not 0 */
            pMobj->play_func = 0;
        } else {
            float q = ps2_div((float)pMobj->count, 6.0f);
            b->alpha = (unsigned char)ps2_ftoi((float)b->alpha_max * (1.0f - q));
        }
    } else if (pMobj->stat == 1) {
        if (pMobj->count >= 7) {
            pMobj->stat = 0;
            pMobj->play_func = 0;
        } else {
            float q = ps2_div((float)pMobj->count, 6.0f);
            b->alpha = (unsigned char)ps2_ftoi((float)b->alpha_max * q);
        }
    }
    pMobj->count = pMobj->count + 1;
}

/* swap the shown half of the frame (ChangeMode's tail call) */
static void __MUnitArenaFrame_CrossFade(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_8eedc3e8 *w = MUAF(pMobj);
    int i;

    if (w->modeShow == 0) {
        DF_MOBJ_SetDrawEnable(w->apMobj[1], 1);
        UnitSprite_SetFadeIn(w->apMobj[1]);
        for (i = 2; i < 4; i++) UnitSprite_SetFadeOut(w->apMobj[i]);
        for (i = 7; i < 9; i++) DF_MOBJ_SetFadeOut(w->apMobj[i]);
    } else {
        UnitSprite_SetFadeOut(w->apMobj[1]);
        for (i = 2; i < 4; i++) {
            DF_MOBJ_SetDrawEnable(w->apMobj[i], 1);
            UnitSprite_SetFadeIn(w->apMobj[i]);
        }
        for (i = 7; i < 9; i++) {
            DF_MOBJ_SetDrawEnable(w->apMobj[i], 1);
            DF_MOBJ_SetFadeIn(w->apMobj[i]);
        }
    }
    DF_MOBJ_SetDrawEnable(w->apMobj[8], 0);   /* (sic) both arms: apMobj[8] hidden right after its fade */
}

static void __MUnitArenaFrame_DeleteItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct struct_8eedc3e8 *w = MUAF(pMobj);
    struct struct_34dae972 *b = MUAF_BRIEF(w->pMobjBriefing);
    int i;

    muaf_set_header();
    MOBJManager_Delete(&w->apMobj[0], 2, MUAF_MNG);
    for (i = 0; i < 8; i++) {
        if (w->apMobjString[i] != 0) {
            muaf_erase(w->apMobjString[i]);
            w->apMobjString[i] = 0;
        }
    }
    MenuBriefingPlayer_Delete(b->pBriefPlay);   /* (sic) NULL after a fresh NewSystem (SetData deletes too) */
    if (w->pMobjBriefing != 0) {
        muaf_erase(w->pMobjBriefing);
        w->pMobjBriefing = 0;
    }
}

static void __MUnitArenaFrame_CreateItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    PS2_STACK(struct struct_76b810ec, colorYellow, 0x4c);
    struct struct_8eedc3e8 *w = MUAF(pMobj);
    int i;

    muaf_set_yellow(&colorYellow);
    muaf_set_header();
    MOBJManager_Create(pMobj, &w->apMobj[0], 2, MUAF_MNG);
    for (i = 6; i < 13; i++) MUAF_STR(w->apMobj[i])->id = 0xdbcb8;
    for (i = 0; i < 8; i++) {
        w->apMobjString[i] = DF_MOBJ_NewStringBase(pMobj, 7, 0, 0, &colorYellow, 0, 0);
        MUAF_STR(w->apMobjString[i])->flag = 1;
        MUAF_STR(w->apMobjString[i])->ref_type = 0;
        MUAF_STR(w->apMobjString[i])->id = 0xdbcb8;
    }
}

static void __MUnitArenaFrame_Pkt(void)
{
}

static void __MUnitArenaFrame_Draw(void)
{
}

static void __MUnitArenaFrame_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    struct struct_8eedc3e8 *w = MUAF(pMobj);
    struct struct_34dae972 *b = MUAF_BRIEF(w->pMobjBriefing);

    if (b != 0 && b->pBriefPlay != 0 && w->modeShow == 1) MenuBriefingPlayer_Play(b->pBriefPlay);

    /* (sic) sequential ifs: 1 -> 2 -> 0, 5 -> 6 -> 0, 9 -> 10 -> 0, 13 -> 14 -> 0 within one call */
    if (w->phase == 0) return;
    if (w->phase == 1) w->phase = 2;
    if (w->phase == 2) w->phase = 0;
    if (w->phase == 5) w->phase = 6;
    if (w->phase == 6) w->phase = 0;
    if (w->phase == 9) w->phase = 10;
    if (w->phase == 10) w->phase = 0;
    if (w->phase == 13) w->phase = 14;
    if (w->phase == 14) w->phase = 0;
}

/* modeShow -1 toggles 0 <-> 1; frameless, tail call into CrossFade */
void MUnitArenaFrame_ChangeMode(struct stMobj *pMobj, char modeShow)
{
    struct struct_8eedc3e8 *w = MUAF(pMobj);

    if (modeShow == -1) {
        if (w->modeShow == 0) w->modeShow = 1;
        else w->modeShow = 0;
    } else {
        w->modeShow = modeShow;
    }
    __MUnitArenaFrame_CrossFade(pMobj);
}

/* points block: value text in pVal (char type 7), "points" label (0xdbbaf) in pUnit, centred at `centre` */
static void muaf_points(struct stMobj *pVal, struct stMobj *pUnit, char *buff, int v, int centre)
{
    int w0, w1, x;

    ps2_sprintf_d(buff, munit_arena_frame_fmt_d, v);
    DF_MOBJ_SetStringCopy(pVal, buff);
    w0 = DF_MFONT_GetStringWSize((unsigned char *)buff, 7, 3);
    MUAF_STR(pUnit)->id = 0xdbbaf;
    MUAF_STR(pUnit)->char_type = 8;
    MUAF_STR(pUnit)->flag = 0x401;
    w1 = DF_MFONT_GetStringWSize(DF_MSTR_GetName(MenuMng.language, 0, 0xdbbaf), 8, 0x401);
    x = muaf_left(centre, w0, w1);
    MUAF_STR(pVal)->x = x;
    MUAF_STR(pVal)->y = 0x14b;
    MUAF_STR(pUnit)->x = x + w0;
    MUAF_STR(pUnit)->y = 0x14f;
}

/* rank block: r < 100 = number in pNum + ST/ND/RD/TH suffix in pSuf; r >= 100 = "rank" label (0xdbbb0) in pNum and
 * the rank letter (r - 0x23) in pSuf */
static void muaf_rank(struct stMobj *pNum, struct stMobj *pSuf, char *buff, int r, int centre)
{
    int w0, w1, x;

    if (r < 100) {
        if (r < 100) ps2_sprintf_d(buff, munit_arena_frame_fmt_d, r);
        else ps2_sprintf_d(buff, munit_arena_frame_fmt_c, (char)(r - 0x23));   /* (sic) dead arm */
        DF_MOBJ_SetStringCopy(pNum, buff);
        w0 = DF_MFONT_GetStringWSize((unsigned char *)buff, 7, 3);
        if (r < 100) {
            /* (sic) the suffix text is the sprintf format */
            if (r == 1 || r == 0x15) ps2_sprintf_d(buff, munit_arena_frame_str_st, 0);
            else if (r == 2 || r == 0x16) ps2_sprintf_d(buff, munit_arena_frame_str_nd, 0);
            else if (r == 3 || r == 0x17) ps2_sprintf_d(buff, munit_arena_frame_str_rd, 0);
            else ps2_sprintf_d(buff, munit_arena_frame_str_th, 0);
        } else {
            buff[0] = 0;   /* (sic) dead arm */
        }
        DF_MOBJ_SetStringCopy(pSuf, buff);
        MUAF_STR(pSuf)->char_type = 8;
        w1 = DF_MFONT_GetStringWSize((unsigned char *)buff, 8, 1);
        x = muaf_left(centre, w0, w1);
        MUAF_STR(pNum)->x = x;
        MUAF_STR(pNum)->y = 0x178;
        MUAF_STR(pSuf)->x = x + w0;
        MUAF_STR(pSuf)->y = 0x17c;
    } else {
        MUAF_STR(pNum)->id = 0xdbbb0;
        MUAF_STR(pNum)->char_type = 8;
        w0 = DF_MFONT_GetStringWSize(DF_MSTR_GetName(MenuMng.language, 0, 0xdbbb0), 8, 1);
        if (r < 100) ps2_sprintf_d(buff, munit_arena_frame_fmt_d, r);   /* (sic) dead arm */
        else ps2_sprintf_d(buff, munit_arena_frame_fmt_c, (char)(r - 0x23));
        DF_MOBJ_SetStringCopy(pSuf, buff);
        w1 = DF_MFONT_GetStringWSize((unsigned char *)buff, 7, 1);
        x = muaf_left(centre, w0, w1);
        MUAF_STR(pNum)->x = x;
        MUAF_STR(pNum)->y = 0x17c;   /* (sic) y values swapped against the r < 100 arm */
        MUAF_STR(pSuf)->x = x + w0;
        MUAF_STR(pSuf)->y = 0x178;
    }
}

/* arena result screen: AC name, past / new points and past / new rank */
void MUnitArenaFrame_SetResultData(struct stMobj *pMobj, char *pNameAC, int pointNew, int pointPast, int rankNew,
                                   int rankPast)
{
    PS2_FRAME(0x80);
    PS2_STACK(char[16], buff, 0x60);
    PS2_STACK(struct struct_76b810ec, colorYellow, 0x7c);
    struct struct_8eedc3e8 *w = MUAF(pMobj);

    muaf_set_yellow(&colorYellow);
    DF_MOBJ_SetStringCopy(w->apMobj[6], pNameAC);
    MUAF_STR(w->apMobj[9])->id = 0xdbf28;
    MUAF_STR(w->apMobj[10])->id = 0xdbcc5;
    MUAF_STR(w->apMobj[11])->id = 0xdbf29;
    MUAF_STR(w->apMobj[12])->id = 0xdbcc5;
    DF_MOBJ_SetStringColorAll(w->apMobj[10], &colorYellow);
    DF_MOBJ_SetStringColorAll(w->apMobj[12], &colorYellow);

    muaf_points(w->apMobjString[0], w->apMobjString[4], buff, pointPast, 400);
    muaf_points(w->apMobjString[1], w->apMobjString[5], buff, pointNew, 0x220);
    muaf_rank(w->apMobjString[2], w->apMobjString[6], buff, rankPast, 400);
    muaf_rank(w->apMobjString[3], w->apMobjString[7], buff, rankNew, 0x220);
}

/* ranking list: AC name, "rank" header and the ranker's briefing text placed at layout part "string2" */
void MUnitArenaFrame_SetData(struct stMobj *pMobj, char *pNameAC, int rank)
{
    PS2_FRAME(0xb0);
    PS2_STACK(char[32], name, 0x40);
    PS2_STACK(struct _LAYOUT_RET_DATA, layout, 0x60);
    struct struct_8eedc3e8 *w = MUAF(pMobj);
    struct struct_34dae972 *b = MUAF_BRIEF(w->pMobjBriefing);
    unsigned int strid;

    if (pNameAC != 0) DF_MOBJ_SetStringCopy(w->apMobj[6], pNameAC);
    else DF_MOBJ_SetStringCopy(w->apMobj[6], (char *)DF_MSTR_GetName(MenuMng.language, 0, 0x7a508));
    MUAF_STR(w->apMobj[7])->id = 0xdbec5;
    MenuBriefingPlayer_Delete(b->pBriefPlay);
    if (rank < 100) strid = rank + 0x7a24b;
    else strid = 0x7a508;
    b->pBriefPlay = MenuBriefingPlayer_New(0, (char *)DF_MSTR_GetName(MenuMng.language, 0, strid), 0);
    ps2_sprintf_d(name, munit_arena_frame_str_string2, 0);
    if (DF_LO_GetLayoutData((unsigned char *)LAYOUT_GetLayoutData(0x451), (unsigned char *)name, 0, &layout))
        MenuBriefingPlayer_SetXY(b->pBriefPlay, layout.Pos.x, layout.Pos.y);
}

void MUnitArenaFrame_FadeOutStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x40);
    struct struct_8eedc3e8 *w = MUAF(pMobj);
    int i;

    w->phase = phase;
    muaf_set_header();
    MOBJManager_FadeOut(&w->apMobj[0], 0, 2, MUAF_MNG);
    for (i = 0; i < 8; i++) DF_MOBJ_SetFadeOut(w->apMobjString[i]);
    w->pMobjBriefing->play_func = (void (*)(void))GFN(__MUnitArenaFrame_PlayBriefing);
    w->pMobjBriefing->stat = 2;
    w->pMobjBriefing->count = 0;
}

void MUnitArenaFrame_FadeInStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x40);
    struct struct_8eedc3e8 *w = MUAF(pMobj);
    int i;

    w->phase = phase;
    muaf_set_header();
    MOBJManager_FadeIn(&w->apMobj[0], 0, 2, MUAF_MNG);
    for (i = 0; i < 8; i++) DF_MOBJ_SetFadeIn(w->apMobjString[i]);
    w->pMobjBriefing->play_func = (void (*)(void))GFN(__MUnitArenaFrame_PlayBriefing);
    w->pMobjBriefing->stat = 1;
    w->pMobjBriefing->count = 0;

    /* the fade-ins above run first, then the hide pass overrides draw-enable */
    if (w->mode == 1) {
        for (i = 1; i < 4; i++) DF_MOBJ_SetDrawEnable(w->apMobj[i], 0);
        for (i = 7; i < 9; i++) DF_MOBJ_SetDrawEnable(w->apMobj[i], 0);
    } else if (w->mode == 3 || w->mode == 2 || w->mode == 0) {
        for (i = 2; i < 6; i++) DF_MOBJ_SetDrawEnable(w->apMobj[i], 0);
        for (i = 7; i < 13; i++) DF_MOBJ_SetDrawEnable(w->apMobj[i], 0);
        for (i = 0; i < 8; i++) DF_MOBJ_SetDrawEnable(w->apMobjString[i], 0);
    }
}

/* frameless tail jump to DeleteItem */
void MUnitArenaFrame_Cleanup(struct stMobj *pMobj)
{
    __MUnitArenaFrame_DeleteItem(pMobj);
}

struct stMobj *MUnitArenaFrame_NewSystem(struct stMobj *pParentMobj, char mode)
{
    PS2_FRAME(0x40);
    struct stMobj *m;
    struct stMobj *bm;
    struct struct_8eedc3e8 *w;
    struct struct_34dae972 *b;

    m = DF_MOBJ_GetNewObject(pParentMobj);
    if (m == 0) return 0;
    w = MUAF(m);
    memset(w, 0, 0x60);
    w->mode = mode;
    m->type = 0;
    m->stat = 1;
    m->count = 0;
    m->play_func = (void (*)(void))GFN(__MUnitArenaFrame_Play);
    m->draw_func = (void (*)(void))GFN(__MUnitArenaFrame_Draw);
    m->pkt_func = (void (*)(void))GFN(__MUnitArenaFrame_Pkt);
    bm = DF_MOBJ_GetNewObject(m);
    w->pMobjBriefing = bm;
    if (bm == 0) return 0;   /* (sic) the frame mobj stays alive with Play installed and no briefing child */
    b = MUAF_BRIEF(bm);
    memset(b, 0, 0xc);
    bm->count = 0;
    bm->play_func = 0;
    bm->draw_func = (void (*)(void))GFN(__MUnitArenaFrame_DrawBriefing);
    bm->pkt_func = 0;
    bm->draw_prio = 2;
    b->pParent = m;
    b->alpha = 0;
    b->alpha_max = 0x7f;
    __MUnitArenaFrame_CreateItem(m);
    return m;
}
