/* src/menu/mgen_tab.cpp - the "generic tab" scene of the result / record pages: one mobj (MGenTab) that owns the
 * two-entry tab strip (MUnitTab, unit 6) and two child pages picked by tab_id:
 *   3 = arena result + trend (mode 3), 2 = training result (mode 1) + trend (mode 1),
 *   1 = training record + record detail (data pointers passed in), 0 = training result (mode 2) + trend (mode 2).
 * LEFT / RIGHT / L1 / R1 move the tab cursor (0..1) and fade the pages in / out; the decide button runs
 * InitPhaseCancel, PlayPhaseCancel (phase 3) then erases both children, pops the scene label and finishes the mobj.
 * Created by __StTopSceneMng_play (mtop) and run through a MobjWrapScene (Play / Draw / Pkt move into the wrapper).
 * The tab 0 / 1 / 2 arms call interpreted MTrainingResult_* / MTrainingRRecord_* / MTrainingRDetail_*, hence the
 * PS2_FRAMEs. PlayPhaseDispatch (phase 4) is unreached: nothing in the game stores phase 2, the only way into it.
 * Original quirks kept: FadeIn / OutUnder only know indexes 0 and 1; PlayPhaseCancel reads child->stat before the
 * NULL test; NewSystem does not NULL-check the child pages; an unknown tab_id skips MUnitTab_ChangeMode. */
#include <string.h>

#ifdef ACNB_SOURCEPORT
/* main sdata 0x27d064 / 0x27d068 (the hybrid uses the PS2 words through the local header's asm labels) */
char s_indexCursorTop;
char s_indexCursorBottom;
#endif

#define MGT(o) ((struct struct_3d24a714 *)&(o)->dt)

/* f_draw = 0, f_erase = 1 (two byte RMWs in the original) */
static inline void mgt_erase(struct stMobj *p)
{
    p->f_draw = 0;
    p->f_erase = 1;
}

static void __MGenTab_FadeOutUnder(struct stMobj *pMobj, int index)
{
    PS2_FRAME(0x10);
    struct struct_3d24a714 *d = MGT(pMobj);
    switch (d->tab_id) {
    case 3:
        if (index == 1) MTrainingTrend_FadeOutItem(d->apChild[1]);
        else if (index == 0) MArenaResult_FadeOutItem(d->apChild[0]);
        break;
    case 2:
        if (index == 1) MTrainingTrend_FadeOutItem(d->apChild[1]);
        else if (index == 0) MTrainingResult_FadeOutItem(d->apChild[0]);
        break;
    case 1:
        if (index == 1) MTrainingRDetail_FadeOutItem(d->apChild[1]);
        else if (index == 0) MTrainingRRecord_FadeOutItem(d->apChild[0]);
        break;
    case 0:
        if (index == 1) MTrainingTrend_FadeOutItem(d->apChild[1]);
        else if (index == 0) MTrainingResult_FadeOutItem(d->apChild[0]);
        break;
    }
}

static void __MGenTab_FadeInUnder(struct stMobj *pMobj, int index)
{
    PS2_FRAME(0x10);
    struct struct_3d24a714 *d = MGT(pMobj);
    switch (d->tab_id) {
    case 3:
        if (index == 1) MTrainingTrend_FadeInItem(d->apChild[1]);
        else if (index == 0) MArenaResult_FadeInItem(d->apChild[0]);
        break;
    case 2:
        if (index == 1) MTrainingTrend_FadeInItem(d->apChild[1]);
        else if (index == 0) MTrainingResult_FadeInItem(d->apChild[0]);
        break;
    case 1:
        if (index == 1) MTrainingRDetail_FadeInItem(d->apChild[1]);
        else if (index == 0) MTrainingRRecord_FadeInItem(d->apChild[0]);
        break;
    case 0:
        if (index == 1) MTrainingTrend_FadeInItem(d->apChild[1]);
        else if (index == 0) MTrainingResult_FadeInItem(d->apChild[0]);
        break;
    }
}

/* shared tail of NewSystem and PlayPhaseDispatch: tab strip mode, cursor, page fades, first item fade-in.
 * The ChangeMode modes are register leftovers in the original (tab 0: 1, tab 1: 2, tab 2: 0, tab 3: 3);
 * an unknown tab_id skips ChangeMode and the item fade. */
static inline void mgt_start(struct stMobj *m)
{
    struct struct_3d24a714 *d = MGT(m);
    switch (d->tab_id) {
    case 3: MUnitTab_ChangeMode(d->pMobjUnitTab, 3); break;
    case 2: MUnitTab_ChangeMode(d->pMobjUnitTab, 0); break;
    case 1: MUnitTab_ChangeMode(d->pMobjUnitTab, 2); break;
    case 0: MUnitTab_ChangeMode(d->pMobjUnitTab, 1); break;
    }
    int idx = d->cursor_index;
    MUnitTab_ChangeCursor(d->pMobjUnitTab, idx);   /* the original also passes idx in a2 (ignored) */
    __MGenTab_FadeInUnder(m, idx);
    __MGenTab_FadeOutUnder(m, idx);
    MUnitTab_FadeInStart(d->pMobjUnitTab);
    __MGenTab_FadeInUnder(m, d->cursor_index);
    switch (d->tab_id) {
    case 3: MArenaResult_FadeInItemOne(d->apChild[0]); break;
    case 2: MTrainingResult_FadeInItemOne(d->apChild[0]); break;
    case 1: MTrainingRRecord_FadeInItemOne(d->apChild[0]); break;
    case 0: MTrainingResult_FadeInItemOne(d->apChild[0]); break;
    }
}

/* phase 4 (unreached): wait for the child and the view motion, then restart the page */
static void __MGenTab_PlayPhaseDispatch(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_3d24a714 *d = MGT(pMobj);
    if (d->pChild != NULL && d->pChild->stat != 5) return;
    if (!DF_MVIEW_MotionSync(MViewp)) return;
    if (d->pChild != NULL) {
        mgt_erase(d->pChild);
        d->pChild = NULL;
    }
    d->phase = 1;
    pMobj->stat = 1;
    pMobj->count = 0;
    mgt_start(pMobj);
}

/* phase 3: after 6 frames wait for both pages to finish, erase them, pop the scene label and finish */
static void __MGenTab_PlayPhaseCancel(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    struct struct_3d24a714 *d = MGT(pMobj);
    int c = pMobj->count;
    pMobj->count = c + 1;
    if (c < 6) return;
    switch (d->tab_id) {
    case 3:
    case 2:
    case 1:
    case 0:
        /* stat is read before the NULL test (original) */
        if (d->apChild[0]->stat != 5) return;
        if (d->apChild[1]->stat != 5) return;
        if (d->apChild[0] != NULL) {
            mgt_erase(d->apChild[0]);
            d->apChild[0] = NULL;
        }
        if (d->apChild[1] != NULL) {
            mgt_erase(d->apChild[1]);
            d->apChild[1] = NULL;
        }
        break;
    }
    if ((unsigned int)(MenuMng.stat - 2) >= 5 && d->label_id != 0) DF_MBACK_PopSceneLabel();
    pMobj->stat = 5;
    pMobj->play_func = 0;
    pMobj->draw_func = 0;
    pMobj->pkt_func = 0;
}

static void __MGenTab_InitPhaseCancel(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct struct_3d24a714 *d = MGT(pMobj);
    d->phase = 3;
    pMobj->count = 0;
    MUnitTab_FadeOutStart(d->pMobjUnitTab);
    switch (d->tab_id) {
    case 3:
        MArenaResult_FadeOutItemOne(d->apChild[0]);
        MArenaResult_InitPhaseCancel(d->apChild[0]);
        MTrainingTrend_InitPhaseCancel(d->apChild[1]);
        break;
    case 2:
        MTrainingResult_FadeOutItemOne(d->apChild[0]);
        MTrainingResult_InitPhaseCancel(d->apChild[0]);
        MTrainingTrend_InitPhaseCancel(d->apChild[1]);
        break;
    case 1:
        MTrainingRRecord_FadeOutItemOne(d->apChild[0]);
        MTrainingRRecord_InitPhaseCancel(d->apChild[0]);
        MTrainingRDetail_InitPhaseCancel(d->apChild[1]);
        break;
    case 0:
        MTrainingResult_FadeOutItemOne(d->apChild[0]);
        MTrainingResult_InitPhaseCancel(d->apChild[0]);
        MTrainingTrend_InitPhaseCancel(d->apChild[1]);
        MUnitColorbar_FadeInStart(MUnitMng_GetUnit(5), 0x17);
        break;
    }
}

/* move the cursor to the new index and swap the page fades */
static inline void mgt_cursor_moved(struct stMobj *pMobj, int old)
{
    struct struct_3d24a714 *d = MGT(pMobj);
    int idx = d->cursor_index;
    MUnitTab_ChangeCursor(d->pMobjUnitTab, idx);   /* the original also passes old in a2 (ignored) */
    __MGenTab_FadeInUnder(pMobj, idx);
    __MGenTab_FadeOutUnder(pMobj, old);
}

/* phase 1: input */
static void __MGenTab_PlayPhaseMain(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct struct_3d24a714 *d = MGT(pMobj);
    if (pMobj->stat == 1) {
        int c = pMobj->count;
        pMobj->count = c + 1;
        if (c < 6) return;
        pMobj->stat = 0;
    }
    if (MenuKeyGuide_IsShown()) return;
    if (MUnitTab_IsMove(d->pMobjUnitTab)) return;
    if (d->tab_id == 2 && MTrainingResult_IsMove(d->apChild[0])) return;
    if (d->tab_id == 3 && MArenaResult_IsMove(d->apChild[0])) return;

    if (MPad[MPadUser].repeat.rt == 1 || MPad[MPadUser].repeat.r1 == 1) {
        int old = d->cursor_index;
        Voice_Call(0x3e9, 0x7f);
        d->cursor_index = d->cursor_index + 1;
        if (s_indexCursorBottom < d->cursor_index) d->cursor_index = s_indexCursorTop;
        mgt_cursor_moved(pMobj, old);
    }
    if (MPad[MPadUser].repeat.lt == 1 || MPad[MPadUser].repeat.l1 == 1) {
        int old = d->cursor_index;
        Voice_Call(0x3e9, 0x7f);
        d->cursor_index = d->cursor_index - 1;
        if (d->cursor_index < s_indexCursorTop) d->cursor_index = s_indexCursorBottom;
        mgt_cursor_moved(pMobj, old);
    }
    if (d->tab_id == 1 && d->cursor_index == 1) MTrainingRDetail_ChangeCursor(d->apChild[1]);
    if (MPad[MPadUser].Rrt == 1 || ((d->tab_id == 2 || d->tab_id == 3) && MPad[MPadUser].Rdw == 1)) {
        if (MyI.ending != 0) {
            DF_MENU_SetFadeOut(6);
            DF_MBACK_SetFadeOutNX();   /* the original passes a0 = 0 */
            Bgm_FadeOutStart();
            DF_MBACK_PushSceneLabel((enum_5124d09c)0);
            DF_MBGUIDE_Hide();
            DF_M1HELP_FadeOut();
            MenuMng.stat = 2;
        }
        Voice_Call(0x3eb, 0x7f);
        __MGenTab_InitPhaseCancel(pMobj);
    }
}

static void __MGenTab_Pkt(void)
{
}

static void __MGenTab_Draw(void)
{
}

static void __MGenTab_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x10);
    struct struct_3d24a714 *d = MGT(pMobj);
    switch (d->phase) {
    case 4:
        __MGenTab_PlayPhaseDispatch(pMobj);
        break;
    case 3:
        __MGenTab_PlayPhaseCancel(pMobj);
        break;
    case 2: {
        int c = pMobj->count;
        pMobj->count = c + 1;
        if (c >= 6) {
            d->phase = 4;
            pMobj->stat = 3;
        }
        break;
    }
    case 1:
        __MGenTab_PlayPhaseMain(pMobj);
        break;
    }
}

struct stMobj *MGenTab_NewSystem(struct stMobj *pParentMobj, char tab_id, char label_id, char *pData0, char *pData1,
                                 char *pData2)
{
    PS2_FRAME(0x80);
    struct stMobj *m = DF_MOBJ_GetNewObject(pParentMobj);
    if (m == NULL) return NULL;
    struct struct_3d24a714 *d = MGT(m);
    memset(d, 0, 0x24);
    d->tab_id = tab_id;
    d->label_id = label_id;
    m->type = 0;
    m->stat = 1;
    m->count = 0;
    m->play_func = (void (*)(void))GFN(__MGenTab_Play);
    m->draw_func = (void (*)(void))GFN(__MGenTab_Draw);
    m->pkt_func = (void (*)(void))GFN(__MGenTab_Pkt);
    d->pMobjUnitTab = MUnitMng_GetUnit(6);
    if (d->label_id != 0) DF_MBACK_PushSceneLabel((enum_5124d09c)(d->label_id & 0xff));
    switch (d->tab_id) {
    case 3:
        d->apChild[0] = MArenaResult_NewSystem(m);
        d->apChild[1] = MTrainingTrend_NewSystem(m, 3);
        s_indexCursorTop = 0;
        s_indexCursorBottom = 1;
        break;
    case 2:
        d->apChild[0] = MTrainingResult_NewSystem(m, 1);
        d->apChild[1] = MTrainingTrend_NewSystem(m, 1);
        s_indexCursorTop = 0;
        s_indexCursorBottom = 1;
        break;
    case 1:
        d->apChild[0] = MTrainingRRecord_NewSystem(m, pData0, pData1, pData2);
        d->apChild[1] = MTrainingRDetail_NewSystem(m, pData0, pData1, pData2);
        s_indexCursorTop = 0;
        s_indexCursorBottom = 1;
        break;
    case 0:
        d->apChild[0] = MTrainingResult_NewSystem(m, 2);   /* mode 2 = register leftover a1 in the original */
        d->apChild[1] = MTrainingTrend_NewSystem(m, 2);
        s_indexCursorTop = 0;
        s_indexCursorBottom = 1;
        break;
    }
    d->phase = 1;
    m->stat = 1;
    m->count = 0;
    mgt_start(m);
    return m;
}
