/* src/menu/mtraining_trend.cpp - the trend page of the generic tab scene (menu/mgen_tab): NewSystem(parent, mode)
 * creates the trend mobj (play only; its Draw / Pkt are empty) plus a draw-only child whose DrawTrend runs
 * MTrendMng_Play (menu/mtrend_mng, the three trend pages) while the page is shown (state 1) and refreshes the
 * unit-tab-info arrows when the page changes. Mode 1 / 3 show the current combat record (GetTrainingTrend), mode 2
 * one stored record (GetTrainingTrendLv from MenuMng's category / detail / level). FadeInItem / FadeOutItem /
 * InitPhaseCancel are driven by MGenTab.
 * Original quirks kept: the trend mobj leaks when the child allocation fails; Play phase 4 tests dt.pChild, which
 * nothing sets (dead), phase 2 has no writer; modes other than 1..3 leave the trend manager's config stale. */
#include <string.h>

#define MTT(o) ((struct struct_eedf415d *)&(o)->dt)
#define MTT_CHILD(o) ((struct struct_3af321b3 *)&(o)->dt)

static inline void mtt_erase(struct stMobj *p)
{
    p->f_draw = 0;
    p->f_erase = 1;
}

/* draw function of the child mobj (pMobj = the child; its dt.pParent = the trend mobj) */
static void __MTrainingTrend_DrawTrend(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    struct stMobj *parent = MTT_CHILD(pMobj)->pParent;
    struct struct_eedf415d *d = MTT(parent);
    if (d->state == 1) MTrendMng_Play();
    if (MTrendMng_IsChangePage()) {
        char st = MTrendMng_GetArrowState();
        MUnitTabInfo_SetArrowState(d->pMobjUnitTabInfo, st);
    }
}

void MTrainingTrend_InitPhaseCancel(struct stMobj *pMobj)
{
    MTT(pMobj)->phase = 3;
    pMobj->count = 0;
    MTT(pMobj)->state = 2;
}

void MTrainingTrend_FadeOutItem(struct stMobj *pMobj)
{
    MTT(pMobj)->state = 2;
}

void MTrainingTrend_FadeInItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    struct struct_eedf415d *d = MTT(pMobj);
    d->state = 1;
    char st = MTrendMng_GetArrowState();
    MUnitTabInfo_SetArrowState(d->pMobjUnitTabInfo, st);
    if (d->mode == 1 || d->mode == 2)
        DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, 0xdbc10));
    else
        DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, 0xdbc17));
}

static void __MTrainingTrend_Pkt(void)
{
}

static void __MTrainingTrend_Draw(void)
{
}

static void __MTrainingTrend_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    struct struct_eedf415d *d = MTT(pMobj);
    int c;
    switch (d->phase) {
    case 4:   /* dead: dt.pChild is never set */
        if (d->pChild == NULL) return;
        if (d->pChild->stat != 5) return;
        if (!DF_MVIEW_MotionSync(MViewp)) return;
        if (d->pChild != NULL) {
            mtt_erase(d->pChild);
            d->pChild = NULL;
        }
        break;
    case 3:
        c = pMobj->count;
        pMobj->count = c + 1;
        if (c < 6) return;
        if (d->pMobjTrend != NULL) {
            mtt_erase(d->pMobjTrend);
            d->pMobjTrend = NULL;
        }
        pMobj->stat = 5;
        pMobj->play_func = 0;
        pMobj->draw_func = 0;
        pMobj->pkt_func = 0;
        break;
    case 2:   /* unreached */
        c = pMobj->count;
        pMobj->count = c + 1;
        if (c < 6) return;
        d->phase = 4;
        pMobj->stat = 3;
        break;
    case 1:
        if (pMobj->stat != 1) return;
        c = pMobj->count;
        pMobj->count = c + 1;
        if (c < 6) return;
        pMobj->stat = 0;
        break;
    }
}

struct stMobj *MTrainingTrend_NewSystem(struct stMobj *pParentMobj, char mode)
{
    PS2_FRAME(0x40);
    struct stMobj *m = DF_MOBJ_GetNewObject(pParentMobj);
    if (m == NULL) return NULL;
    struct struct_eedf415d *d = MTT(m);
    memset(d, 0, 0x14);
    d->mode = mode;
    m->type = 0;
    m->stat = 1;
    m->count = 0;
    m->play_func = (void (*)(void))GFN(__MTrainingTrend_Play);
    m->draw_func = (void (*)(void))GFN(__MTrainingTrend_Draw);
    m->pkt_func = (void (*)(void))GFN(__MTrainingTrend_Pkt);
    d->pMobjUnitTabInfo = MUnitMng_GetUnit(7);
    d->pMobjTrend = DF_MOBJ_GetNewObject(m);
    if (d->pMobjTrend == NULL) return NULL;   /* m stays allocated (original) */
    memset(&d->pMobjTrend->dt, 0, 8);
    MTT_CHILD(d->pMobjTrend)->pParent = m;
    d->pMobjTrend->count = 0;
    d->pMobjTrend->play_func = 0;
    d->pMobjTrend->draw_func = (void (*)(void))GFN(__MTrainingTrend_DrawTrend);
    d->pMobjTrend->pkt_func = 0;
    switch (d->mode) {
    case 1:
    case 3: {
        struct _training_trend_st *t = GetTrainingTrend();
        MTrendMng_SetConfig(MenuMng.category_index, MenuMng.detail_index, MenuMng.level_index, t, 1);
        break;
    }
    case 2: {
        struct _training_trend_st *t =
            GetTrainingTrendLv(MenuMng.category_index, MenuMng.detail_index, MenuMng.level_index);
        MTrendMng_SetConfig(MenuMng.category_index, MenuMng.detail_index, MenuMng.level_index, t, 2);
        break;
    }
    }
    d->phase = 1;
    return m;
}
