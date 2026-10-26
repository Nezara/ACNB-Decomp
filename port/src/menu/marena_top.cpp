/* src/menu/marena_top.cpp - the ARENA top page of the single-player menu (TOP -> ARENA): one stMobj with an inline
 * payload (obj+0x10, struct_d5e55dfb) and three tabs SEARCH (cursor 0, child page MArenaSearch), OFFICIAL (1,
 * MArenaOfficial) and RANKING (2, MArenaRanking mode 1). It owns five unit mobjs: MUnitArenaTop (tab strip),
 * MUnitArenaBase (frame) and the page previews MUnitEzMatch / EzOfficial / EzRanking (only the one under the cursor is
 * faded in). Phases (payload.phase): 1 Main (pad: Down / Up move the cursor, circle leaves to the TOP menu, cross
 * fades out and starts the child), 2 the 7th frame after the confirm builds the child into pChild, 3 cancel tear-down
 * (obj->stat = 5), 4 Dispatch (waits for the child to end, then brings the page back or leaves). MArenaTop_NewSystem
 * is called by mtop_rt.cpp's __MTopRT_PlayPhaseProceed. Twin of mtop_rt.cpp.
 * Original quirks kept: the OFFICIAL tab is skipped (and its confirm swallowed) when both official cards'
 * idRanker are 0; Down and Up are independent ifs; the confirm is swallowed silently while the AC has no mi2p;
 * flag.rotate is set inside the AC reload block; ChangeCursor fades the new page in before the old one out;
 * NewSystem changes the help string twice (the second call unconditionally with cursor 0's id). */
#include <string.h>
#include "acnb/sdk.h"

/* obj+0x10: the page payload */
#define AT(o) ((struct struct_d5e55dfb *)&(o)->dt)

/* "OFFICIAL tab has a card" (g_gamedata[0].officialBattle[0/1].idRanker, B read only when A == 0) */
static inline int marenatop_has_official(void)
{
    return g_gamedata[0].officialBattle[0].idRanker != 0 || g_gamedata[0].officialBattle[1].idRanker != 0;
}

static void __MArenaTop_ChangeCursor(struct stMobj *pMobj, int indexNew, int indexPast)
{
    PS2_FRAME(0x40);
    struct struct_d5e55dfb *w = AT(pMobj);

    MUnitArenaTop_ChangeCursor(w->pMobjUnitArenaTop, indexNew);
    MUnitArenaBase_ChangeCursor(w->pMobjUnitArenaBase, indexNew);   /* the original also passes indexPast in a2 */
    switch (indexNew) {
    case 0: MUnitEzMatch_FadeInStart(w->pMobjUnitEzMatch, 0x11); break;
    case 1: MUnitEzOfficial_FadeInStart(w->pMobjUnitEzOfficial, 0x11); break;
    case 2: MUnitEzRanking_FadeInStart(w->pMobjUnitEzRanking, 0x11); break;
    }
    switch (indexPast) {
    case 0: MUnitEzMatch_FadeOutStart(w->pMobjUnitEzMatch, 0x12); break;
    case 1: MUnitEzOfficial_FadeOutStart(w->pMobjUnitEzOfficial, 0x12); break;
    case 2: MUnitEzRanking_FadeOutStart(w->pMobjUnitEzRanking, 0x12); break;
    }
    if ((unsigned int)(MenuMng.stat - 2) >= 5)
        DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, indexNew + 0xdbd94));
}

/* the shared "page fades out" tail (Dispatch leave path, cancel, confirm) */
static inline void marenatop_fade_out(struct stMobj *pMobj, char ph)
{
    struct struct_d5e55dfb *w = AT(pMobj);
    MUnitArenaTop_FadeOutStart(w->pMobjUnitArenaTop, ph);
    MUnitArenaBase_FadeOutStart(w->pMobjUnitArenaBase, ph);
    switch (w->cursor_index) {
    case 0: MUnitEzMatch_FadeOutStart(w->pMobjUnitEzMatch, ph); break;
    case 1: MUnitEzOfficial_FadeOutStart(w->pMobjUnitEzOfficial, ph); break;
    case 2: MUnitEzRanking_FadeOutStart(w->pMobjUnitEzRanking, ph); break;
    }
}

static inline void marenatop_fade_in(struct stMobj *pMobj, char ph)
{
    struct struct_d5e55dfb *w = AT(pMobj);
    MUnitArenaTop_FadeInStart(w->pMobjUnitArenaTop, ph);
    MUnitArenaBase_FadeInStart(w->pMobjUnitArenaBase, ph);
    switch (w->cursor_index) {
    case 0: MUnitEzMatch_FadeInStart(w->pMobjUnitEzMatch, ph); break;
    case 1: MUnitEzOfficial_FadeInStart(w->pMobjUnitEzOfficial, ph); break;
    case 2: MUnitEzRanking_FadeInStart(w->pMobjUnitEzRanking, ph); break;
    }
}

static void __MArenaTop_PlayPhaseDispatch(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct struct_d5e55dfb *w = AT(pMobj);

    if (w->pChild == NULL || w->pChild->stat != 5) return;
    if (!DF_MVIEW_MotionSync(MViewp)) return;
    if (w->pChild) {
        w->pChild->f_draw = 0;
        w->pChild->f_erase = 1;
        w->pChild = NULL;
    }
    int s = MenuMng.stat;
    if ((unsigned int)(s - 2) < 2 || (unsigned int)(s - 5) < 2 || s == 4) {
        w->phase = 3;
        pMobj->count = 0;   /* (sic) pMobj->stat not written */
        marenatop_fade_out(pMobj, 0xd);
        return;
    }
    w->phase = 1;
    pMobj->stat = 1;
    pMobj->count = 0;
    marenatop_fade_in(pMobj, 5);
    DF_MVIEW_MotionStart(MViewp, 0x52);
    if ((unsigned int)(MenuMng.stat - 2) >= 5)
        DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, w->cursor_index + 0xdbd94));
}

static void __MArenaTop_PlayPhaseMain(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_d5e55dfb *w = AT(pMobj);

    if (pMobj->stat == 1) {
        unsigned char old = pMobj->count;
        pMobj->count = old + 1;
        if (old < 6) return;
        pMobj->stat = 0;
    }
    struct stMenuAC *acp = M_ACp[0][0];
    if (!acp->flag.set && !acp->flag.reading) {
        DF_MMD_SetGeneralMMD();
        acp = M_ACp[0][0];
        DF_M_AC_SetData(acp);
        DF_M_AC_PartsRead(acp);
        acp->alpha.cur = 0;
        acp->flag.rotate = 1;   /* inside the reload block (mtop_rt Dispatch: always) */
        DF_M_AC_SetFadeIn(M_ACp[0][0], 0xf, 0.0f);
        MenuMng.f_use_readbuff = 0;
    }
    if (MenuKeyGuide_IsShown()) return;
    if (MPad[MPadUser].start == 1) {
        MenuKeyGuide_Show(0x46);
        return;
    }
    /* (sic) Down and Up are independent: both run when both repeat bits are set */
    if (MPad[MPadUser].repeat.dw == 1) {
        int indexPast = w->cursor_index;
        Voice_Call(0x3e9, 0x7f);
        w->cursor_index++;
        if (w->cursor_index >= 3) w->cursor_index = 0;
        if (w->cursor_index == 1 && !marenatop_has_official()) w->cursor_index++;
        __MArenaTop_ChangeCursor(pMobj, w->cursor_index, indexPast);
    }
    if (MPad[MPadUser].repeat.up == 1) {
        int indexPast = w->cursor_index;
        Voice_Call(0x3e9, 0x7f);
        w->cursor_index--;
        if (w->cursor_index < 0) w->cursor_index = 2;
        if (w->cursor_index == 1 && !marenatop_has_official()) w->cursor_index--;
        __MArenaTop_ChangeCursor(pMobj, w->cursor_index, indexPast);
    }
    if (MPad[MPadUser].Rrt == 1) {
        Voice_Call(0x3eb, 0x7f);
        w->phase = 3;
        pMobj->count = 0;
        marenatop_fade_out(pMobj, 0xd);
        return;
    }
    if (MPad[MPadUser].Rdw != 1) return;
    if (M_ACp[0][0]->connect.cur.mi2p == NULL) return;              /* (sic) swallowed silently */
    if (w->cursor_index == 1 && !marenatop_has_official()) return;   /* (sic) swallowed silently */
    DF_M_AC_SetFadeOut(M_ACp[0][0], 5, 0.0f);
    DF_M_AC_Reset2(M_ACp[0][0]);
    DF_MMD_DeleteMMDByKind(1);
    DF_MMD_ReleaseGeneralMMD();
    Voice_Call(0x3ea, 0x7f);
    w->phase = 2;
    pMobj->count = 0;
    marenatop_fade_out(pMobj, 9);
}

/* each unit: Cleanup, then erase + clear the slot */
static inline void marenatop_erase(struct stMobj **slot)
{
    if (*slot) {
        (*slot)->f_draw = 0;
        (*slot)->f_erase = 1;
        *slot = NULL;
    }
}

static void __MArenaTop_DeleteItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    struct struct_d5e55dfb *w = AT(pMobj);

    MUnitArenaTop_Cleanup(w->pMobjUnitArenaTop);
    marenatop_erase(&w->pMobjUnitArenaTop);
    MUnitArenaBase_Cleanup(w->pMobjUnitArenaBase);
    marenatop_erase(&w->pMobjUnitArenaBase);
    MUnitEzMatch_Cleanup(w->pMobjUnitEzMatch);
    marenatop_erase(&w->pMobjUnitEzMatch);
    MUnitEzOfficial_Cleanup(w->pMobjUnitEzOfficial);
    marenatop_erase(&w->pMobjUnitEzOfficial);
    MUnitEzRanking_Cleanup(w->pMobjUnitEzRanking);
    marenatop_erase(&w->pMobjUnitEzRanking);
    if ((unsigned int)(MenuMng.stat - 2) >= 5) DF_MBACK_PopSceneLabel();
}

static void __MArenaTop_Pkt(void)
{
}

static void __MArenaTop_Draw(void)
{
}

static void __MArenaTop_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct struct_d5e55dfb *w = AT(pMobj);

    switch (w->phase) {
    case 1:
        __MArenaTop_PlayPhaseMain(pMobj);
        break;
    case 2: {
        unsigned char old = pMobj->count;
        pMobj->count = old + 1;
        if (old < 6) return;
        w->phase = 4;
        pMobj->stat = 3;   /* (sic) count not reset */
        switch (w->cursor_index) {
        case 0: w->pChild = MArenaSearch_NewSystem(pMobj); break;
        case 1: w->pChild = MArenaOfficial_NewSystem(pMobj); break;
        case 2: w->pChild = MArenaRanking_NewSystem(pMobj, 1, NULL); break;
        }
        break;
    }
    case 3: {
        unsigned char old = pMobj->count;
        pMobj->count = old + 1;
        if (old < 6) return;
        __MArenaTop_DeleteItem(pMobj);
        pMobj->stat = 5;
        pMobj->play_func = NULL;
        pMobj->draw_func = NULL;
        pMobj->pkt_func = NULL;
        break;
    }
    case 4:
        __MArenaTop_PlayPhaseDispatch(pMobj);
        break;
    }
}

struct stMobj *MArenaTop_NewSystem(struct stMobj *pParentMobj)
{
    PS2_FRAME(0x30);
    struct stMobj *m = DF_MOBJ_GetNewObject(pParentMobj);
    if (m == NULL) return NULL;
    struct struct_d5e55dfb *w = AT(m);

    memset(w, 0, 0x1c);
    m->type = 0;
    m->stat = 1;
    m->count = 0;
    m->play_func = (void (*)(void))GFN(__MArenaTop_Play);
    m->draw_func = (void (*)(void))GFN(__MArenaTop_Draw);
    m->pkt_func = (void (*)(void))GFN(__MArenaTop_Pkt);
    w->pMobjUnitArenaTop = MUnitArenaTop_NewSystem(m);
    w->pMobjUnitArenaBase = MUnitArenaBase_NewSystem(m);
    w->pMobjUnitEzMatch = MUnitEzMatch_NewSystem(m);
    w->pMobjUnitEzOfficial = MUnitEzOfficial_NewSystem(m);
    w->pMobjUnitEzRanking = MUnitEzRanking_NewSystem(m);
    DF_MBACK_PushSceneLabel((enum_5124d09c)9);
    w->phase = 1;
    m->stat = 1;
    m->count = 0;
    marenatop_fade_in(m, 1);   /* cursor_index is 0 after the memset */
    DF_MVIEW_MotionStart(MViewp, 0x52);
    if ((unsigned int)(MenuMng.stat - 2) >= 5)
        DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, w->cursor_index + 0xdbd94));
    DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, 0xdbd94));   /* (sic) second, unconditional */
    return m;
}
