/* src/menu/munit_ranking_frame.cpp - the AC-name plate of the training ranking screens ("RankingFrame"): one mobj
 * group from a 2-record table (layout 0x447): the frame sprite waku_ac (apMobj[0]) and the AC-name string
 * string_ac_name (apMobj[1], text id 0xdbcb8). Play has no animation, it only writes state 3 ("idle") every frame;
 * FadeInStart / FadeOutStart set state 1 and start the group fade, so IsMove is 1 for exactly one frame.
 * Created by MUnitMng_Init for every top menu; only MTrainingRanking / RRecord / RDetail use the other entry points.
 * The table's record 0 pLayoutHeader is rewritten before every MOBJManager_* call, so the hybrid keeps the table at
 * its PS2 address (nodwarf munit_ranking_frame_mng); the source port defines it below. */
#include <string.h>
#include "acnb/nodwarf.h"

#ifdef ACNB_SOURCEPORT
/* main sdata 0x27c1d8 / OLMENU 0x1ee2de8: the table's layout names */
static char munit_ranking_frame_name_waku[] = "waku_ac";
static char munit_ranking_frame_name_string[] = "string_ac_name";
/* OLMENU 0x1ee2e00 (2 x 0x40) */
struct stMobjManager munit_ranking_frame_mng[2] = {
    {.pLayoutName = munit_ranking_frame_name_waku, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 1},
    {.pLayoutName = munit_ranking_frame_name_string, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 2,
     .flag = 0, .dummy_c0 = {0}, .exString = {.category = 0, .strid = 0xdbcb8}},
};
#endif

/* the unit's work area = mobj->dt (no DWARF struct; NewSystem clears 0x10 bytes) */
struct MUnitRankingFrameWork {
    int pad0;                    /* d+0 */
    struct stMobj *apMobj[2];    /* d+4: waku_ac, string_ac_name */
    char state;                  /* d+0xc: 1 = fade started, 3 = idle */
    char pad_d[3];
};

#define MURF_MNG munit_ranking_frame_mng
#define MURF(o) ((struct MUnitRankingFrameWork *)&(o)->dt)

static inline void murf_set_header(void)
{
    MURF_MNG[0].pLayoutHeader = (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x447);
}

static void __MUnitRankingFrame_Pkt(void)
{
}

static void __MUnitRankingFrame_Draw(void)
{
}

static void __MUnitRankingFrame_Play(struct stMobj *pMobj)
{
    MURF(pMobj)->state = 3;
}

void MUnitRankingFrame_SetString(struct stMobj *pMobj, char *pNameAC)
{
    DF_MOBJ_SetStringCopy(MURF(pMobj)->apMobj[1], pNameAC);
}

int MUnitRankingFrame_IsMove(struct stMobj *pMobj)
{
    return MURF(pMobj)->state != 3;
}

void MUnitRankingFrame_FadeOutStart(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    MURF(pMobj)->state = 1;
    murf_set_header();
    MOBJManager_FadeOut(&MURF(pMobj)->apMobj[0], 0, 2, MURF_MNG);
}

void MUnitRankingFrame_FadeInStart(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    MURF(pMobj)->state = 1;
    murf_set_header();
    MOBJManager_FadeIn(&MURF(pMobj)->apMobj[0], 0, 2, MURF_MNG);
}

void MUnitRankingFrame_Cleanup(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    murf_set_header();
    MOBJManager_Delete(&MURF(pMobj)->apMobj[0], 2, MURF_MNG);
}

struct stMobj *MUnitRankingFrame_NewSystem(struct stMobj *pParentMobj)
{
    PS2_FRAME(0x20);
    struct stMobj *m = DF_MOBJ_GetNewObject(pParentMobj);

    if (m == 0) return 0;
    memset(&m->dt, 0, 0x10);
    m->type = 0;
    m->stat = 1;
    m->count = 0;
    m->play_func = (void (*)(void))GFN(__MUnitRankingFrame_Play);
    m->draw_func = (void (*)(void))GFN(__MUnitRankingFrame_Draw);
    m->pkt_func = (void (*)(void))GFN(__MUnitRankingFrame_Pkt);
    murf_set_header();
    MOBJManager_Create(m, &MURF(m)->apMobj[0], 2, MURF_MNG);
    return m;
}
