/* src/menu/mtop_rt.cpp - the single-player top menu ("RT" = root, after NEW GAME / LOAD GAME): one stMobj with an
 * inline stMTopRT payload (obj+0x10) showing four pages GARAGE / TRAINING / ARENA / SYSTEM (child UnitTop objects
 * Base / Training / System / Arena from the munit_top_* files) plus eight background models (ids 0x514-0x517 = page
 * independent layer, 0x51e-0x521 = per-page layer faded by rateFade[cursor]). A 4-phase state machine (payload.phase):
 * 1 Main (pad: up / down move the cursor, Cross decides, Start shows the key guide), 2 Proceed (7th frame after the
 * decide builds the chosen child screen into pChild), 3 Cancel (tear-down, obj->stat = 5), 4 Dispatch (waits for the
 * child to finish, then fades the menu back in or leaves). MTopRT_NewSystem is called by mtop.c's scene manager and
 * wrapped by MobjWrapScene_New, which moves play / draw / pkt into the wrapper afterwards.
 * Original quirks kept: FadeInPage_Garage does not clear flagFadeInGarage (the other three do); the Down and Up arms
 * are independent ifs (both can run in one frame); the GARAGE Cross with no AC model loaded is swallowed silently;
 * Pkt's alpha byte is 128 * (0.01 + x) (129 at full fade); the 0.2f ramps floor at 0.01f, not 0; Proceed keeps
 * modeMGC for SYSTEM only; NewSystem's resume arms (modeMGC 5 / 3 / 1) start in Proceed with a preset cursor, only
 * mode 3 calls DF_MENU_SetFadeIn(6); NewSystem sets flag.rotate inside the AC reload block, Dispatch always. */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

/* obj+0x10: the top menu payload */
#define RT(o) ((struct stMTopRT *)&(o)->dt)

static const float MTOPRT_PI = 0x1.921fb6p+1f;   /* 0x40490fdb */

/* MWCC float -> unsigned idiom (>= 2^31: subtract, cvt.w.s, or the top bit), stored with sb */
static inline unsigned char mtoprt_f2u8(float f)
{
    if (f >= 2147483648.0f) return (unsigned char)(ps2_ftoi(f - 2147483648.0f) | (int)0x80000000);
    return (unsigned char)ps2_ftoi(f);
}

static void __MTopRT_FadeOutPage_System(struct stMobj *pMobj)
{
    MUnitTopSystem_FadeOutStart(RT(pMobj)->pMobjUnitTopSystem, 0x12);
}

static void __MTopRT_FadeOutPage_Arena(struct stMobj *pMobj)
{
    MUnitTopArena_FadeOutStart(RT(pMobj)->pMobjUnitTopArena, 0x12);
}

static void __MTopRT_FadeOutPage_Training(struct stMobj *pMobj)
{
    MUnitTopTraining_FadeOutStart(RT(pMobj)->pMobjUnitTopTraining, 0x12);
}

static void __MTopRT_FadeOutPage_Garage(void)
{
}

static void __MTopRT_FadeInPage_System(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    MUnitTopSystem_FadeInStart(RT(pMobj)->pMobjUnitTopSystem, 0x11);
    RT(pMobj)->flagFadeInGarage = 0;
}

static void __MTopRT_FadeInPage_Arena(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    MUnitTopArena_FadeInStart(RT(pMobj)->pMobjUnitTopArena, 0x11);
    RT(pMobj)->flagFadeInGarage = 0;
}

static void __MTopRT_FadeInPage_Training(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    MUnitTopTraining_FadeInStart(RT(pMobj)->pMobjUnitTopTraining, 0x11);
    RT(pMobj)->flagFadeInGarage = 0;
}

/* (sic) no flagFadeInGarage write */
static void __MTopRT_FadeInPage_Garage(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    MUnitTopBase_SetData(RT(pMobj)->pMobjUnitTopBase, PlayerInfo_GetAcName(0, PlayerInfo_GetCurrentAcNo(0)));
}

typedef void (*mtoprt_page_fn)(struct stMobj *);

static void __MTopRT_ChangeCursor(struct stMobj *pMobj, int indexNew, int indexPast)
{
    PS2_FRAME(0x70);
    /* pFadeInTable (rodata 0x1ee0430) / pFadeOutTable (0x1ee0440), copied into the frame at sp+0x60 / sp+0x50 */
    const mtoprt_page_fn pFadeInTable[4] = {
        GFN(__MTopRT_FadeInPage_Garage), GFN(__MTopRT_FadeInPage_Training),
        GFN(__MTopRT_FadeInPage_Arena), GFN(__MTopRT_FadeInPage_System),
    };
    const mtoprt_page_fn pFadeOutTable[4] = {
        (mtoprt_page_fn)GFN(__MTopRT_FadeOutPage_Garage), GFN(__MTopRT_FadeOutPage_Training),
        GFN(__MTopRT_FadeOutPage_Arena), GFN(__MTopRT_FadeOutPage_System),
    };
#ifndef ACNB_SOURCEPORT
    PS2_STACK(unsigned int[4], in_words, 0x60);
    PS2_STACK(unsigned int[4], out_words, 0x50);
    for (int i = 0; i < 4; i++) in_words[i] = PS2PTR(pFadeInTable[i]);
    for (int i = 0; i < 4; i++) out_words[i] = PS2PTR(pFadeOutTable[i]);
#endif
    struct stMTopRT *rt = RT(pMobj);

    if (indexPast != -1 && indexPast != -2) {
        gcall(pFadeInTable[indexNew], pMobj);   /* the new page fades in first */
        gcall(pFadeOutTable[indexPast], pMobj);
    }
    if (indexNew == 0)
        MUnitTopBase_ChangeFrame(rt->pMobjUnitTopBase, 2);
    else
        MUnitTopBase_ChangeFrame(rt->pMobjUnitTopBase, 1);
    if ((unsigned int)(MenuMng.stat - 2) >= 5 && rt->modeMGC == 0)
        DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, indexNew + 0xdbbaa));
}

/* the shared "leave the page" tail of Main / Dispatch / NewSystem: the chosen page and the base fade out with phSys
 * (SYSTEM) or phOther, then the cursor frame is updated without a page change */
static inline void mtoprt_page_fade_out(struct stMobj *pMobj, char phSys, char phOther)
{
    struct stMTopRT *rt = RT(pMobj);
    char ph = phOther;
    switch (rt->cursor_index) {
    case 3:
        ph = phSys;
        MUnitTopSystem_FadeOutStart(rt->pMobjUnitTopSystem, ph);
        break;
    case 2:
        MUnitTopArena_FadeOutStart(rt->pMobjUnitTopArena, ph);
        break;
    case 1:
        MUnitTopTraining_FadeOutStart(rt->pMobjUnitTopTraining, ph);
        break;
    }
    MUnitTopBase_FadeOutStart(rt->pMobjUnitTopBase, ph);
    rt->flagFadeOut = 1;
    __MTopRT_ChangeCursor(pMobj, rt->cursor_index, -2);
}

static void __MTopRT_PlayPhaseDispatch(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct stMTopRT *rt = RT(pMobj);

    if (rt->pChild == NULL || rt->pChild->stat != 5) return;
    if (!DF_MVIEW_MotionSync(MViewp)) return;
    struct stMobj *child = rt->pChild;
    if (child) {
        child->f_draw = 0;
        rt->pChild->f_erase = 1;
        rt->pChild = NULL;
    }
    MenuMng.pTopUseAC = NULL;
    int s = MenuMng.stat;
    if ((unsigned int)(s - 2) < 2 || (unsigned int)(s - 5) < 2 || s == 4) {
        DF_MBACK_PushSceneLabel((enum_5124d09c)0);
        rt->phase = 3;
        pMobj->count = 0;
        mtoprt_page_fade_out(pMobj, 0xd, 0xd);
        return;
    }
    struct stMenuAC *acp = M_ACp[0][0];
    if (!acp->flag.set && !acp->flag.reading) {
        DF_MMD_SetGeneralMMD();
        acp = M_ACp[0][0];
        DF_M_AC_SetData(acp);
        DF_M_AC_PartsRead(acp);
        acp->alpha.cur = 0;
        DF_M_AC_SetFadeIn(M_ACp[0][0], 0xf, 0.0f);
        MenuMng.f_use_readbuff = 0;
    }
    M_ACp[0][0]->flag.rotate = 1;
    rt->phase = 1;
    pMobj->stat = 1;
    pMobj->count = 0;
    __MTopRT_FadeInItem(pMobj, 5);
}

static void __MTopRT_PlayPhaseCancel(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    unsigned char old = pMobj->count;
    pMobj->count = old + 1;
    if (old < 6) return;
    if (MenuMng.stat == 5) {
        DF_M_AC_MissionTransStart();
        DF_MENU_SetFadeOut(6);
        DF_MBACK_SetFadeOutNX();
        Bgm_FadeOutStart();
        DF_MBACK_PushSceneLabel((enum_5124d09c)0);
        DF_MBGUIDE_Hide();
        DF_M1HELP_FadeOut();
    }
    if (MenuMng.stat == 3) {
        DF_MBACK_PushSceneLabel((enum_5124d09c)0);
        DF_MBGUIDE_Hide();
    }
    __MTopRT_DeleteItem(pMobj);
    pMobj->stat = 5;
    pMobj->play_func = NULL;
    pMobj->draw_func = NULL;
    pMobj->pkt_func = NULL;
}

static void __MTopRT_PlayPhaseProceed(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct stMTopRT *rt = RT(pMobj);
    unsigned char old = pMobj->count;
    pMobj->count = old + 1;
    if (old < 6) return;
    rt->phase = 4;
    pMobj->stat = 3;
    switch (rt->cursor_index) {
    case 3:
        g_gamedata[0].pointTotal = MUtil_GetTotalPoint();
        DF_MVIEW_MotionStart(MViewp, 0xaa);
        rt->pChild = DF_MSYS_NewSystem(pMobj, 0, 0, rt->pMobjUnitTopBase, rt->pMobjUnitTopSystem);
        break;   /* (sic) modeMGC kept */
    case 2:
        rt->pChild = MArenaTop_NewSystem(pMobj);
        rt->modeMGC = 0;
        break;
    case 1:
        rt->pChild = MTrainingTop_NewSystem(pMobj, rt->modeMGC);
        rt->modeMGC = 0;
        break;
    case 0:
        MenuMng.pTopUseAC = M_ACp[0][0];
        DF_MVIEW_MotionStart(MViewp, 0xa);
        rt->pChild = DF_MGARAGE_NewSystem(pMobj, 0, 0, 0);
        rt->modeMGC = 0;
        break;
    }
}

static void __MTopRT_PlayPhaseMain(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct stMTopRT *rt = RT(pMobj);

    if (pMobj->stat == 1) {
        unsigned char old = pMobj->count;
        pMobj->count = old + 1;
        if (old < 6) return;
        pMobj->stat = 0;
    }
    if (MUnitTopSystem_IsMove(rt->pMobjUnitTopSystem)) return;

    if (g_gamedata[0].flagShowFirstNotice == 0) {
        if (rt->stepFirstNotice == 1) {
            if (rt->countFirstNotice < 30) {
                rt->countFirstNotice++;
                return;
            }
            DF_MDIALOG_SetBtnOff(0);   /* (sic) no matching Open in this file */
            rt->stepFirstNotice = -1;
            g_gamedata[0].flagShowFirstNotice = 1;
        } else if (rt->stepFirstNotice == 0) {
            rt->countFirstNotice = 0;
            rt->stepFirstNotice = 1;
        }
    }
    if (DF_MDIALOG_IsPlay()) return;
    if (MenuKeyGuide_IsShown()) return;

    if (MPad[MPadUser].start == 1) {
        MenuKeyGuide_Show(0x4d);
        return;
    }
    /* (sic) Down and Up are independent: both run when both repeat bits are set */
    if (MPad[MPadUser].repeat.dw == 1) {
        int old = rt->cursor_index;
        Voice_Call(0x3e9, 0x7f);
        rt->cursor_index++;
        if (rt->cursor_index >= 4) rt->cursor_index = 0;
        __MTopRT_ChangeCursor(pMobj, rt->cursor_index, old);
    }
    if (MPad[MPadUser].repeat.up == 1) {
        int old = rt->cursor_index;
        Voice_Call(0x3e9, 0x7f);
        rt->cursor_index--;
        if (rt->cursor_index < 0) rt->cursor_index = 3;
        __MTopRT_ChangeCursor(pMobj, rt->cursor_index, old);
    }
    if (MPad[MPadUser].Rdw != 1) return;
    if (rt->cursor_index == 1) return;   /* Stage 0: TRAINING inactive (mtraining_* not decompiled) */

    struct stMenuAC *acp = M_ACp[0][0];
    if (MUnitTopTraining_IsMove(rt->pMobjUnitTopTraining)) return;
    if (MUnitTopSystem_IsMove(rt->pMobjUnitTopSystem)) return;
    if (rt->cursor_index == 1 || rt->cursor_index == 2) {
        if (DF_M_AC_CheckCompose(0, (char)PlayerInfo_GetCurrentAcNo(0)) == 0) {
            DF_MDIALOG_Open(1, DF_MSTR_GetInfo(MenuMng.language, 1, 0x30db0), NULL, 0, 0, 0, 0, 0);
            return;
        }
    }
    int c = rt->cursor_index;
    if (c == 0 && acp->connect.cur.mi2p == NULL) {
        MenuMng.f_use_readbuff = 0;   /* (sic) swallowed without a sound */
        return;
    }
    if (c == 1 || c == 3) {
        DF_M_AC_SetFadeOut(M_ACp[0][0], 5, 0.0f);
        DF_M_AC_Reset2(M_ACp[0][0]);
        DF_MMD_DeleteMMDByKind(1);
        DF_MMD_ReleaseGeneralMMD();
        M_ACp[0][0]->flag.draw = 0;
    }
    Voice_Call(0x3ea, 0x7f);
    rt->phase = 2;
    pMobj->count = 0;
    mtoprt_page_fade_out(pMobj, 0x17, 9);
}

static void __MTopRT_DeleteItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct stMTopRT *rt = RT(pMobj);

    DF_MBACK_PushSceneLabel((enum_5124d09c)0);
    MUnitTopBase_Cleanup(rt->pMobjUnitTopBase);
    if (rt->pMobjUnitTopBase) {
        rt->pMobjUnitTopBase->f_draw = 0;
        rt->pMobjUnitTopBase->f_erase = 1;
        rt->pMobjUnitTopBase = NULL;
    }
    MUnitTopTraining_Cleanup(rt->pMobjUnitTopTraining);
    if (rt->pMobjUnitTopTraining) {
        rt->pMobjUnitTopTraining->f_draw = 0;
        rt->pMobjUnitTopTraining->f_erase = 1;
        rt->pMobjUnitTopTraining = NULL;
    }
    MUnitTopSystem_Cleanup(rt->pMobjUnitTopSystem);
    if (rt->pMobjUnitTopSystem) {
        rt->pMobjUnitTopSystem->f_draw = 0;
        rt->pMobjUnitTopSystem->f_erase = 1;
        rt->pMobjUnitTopSystem = NULL;
    }
    MUnitTopArena_Cleanup(rt->pMobjUnitTopArena);
    if (rt->pMobjUnitTopArena) {
        rt->pMobjUnitTopArena->f_draw = 0;
        rt->pMobjUnitTopArena->f_erase = 1;
        rt->pMobjUnitTopArena = NULL;
    }
    for (int i = 0; i < 8; i++) {
        DF_MMODEL_DeleteModelAndMMD(rt->apModel[i]);
        rt->apModel[i] = NULL;
    }
}

static void __MTopRT_FadeInItem(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x40);
    struct stMTopRT *rt = RT(pMobj);

    DF_MVIEW_MotionStart(MViewp, 1);
    DF_MBGUIDE_Show();
    DF_MBACK_SetFadeInNX();
    DF_M1HELP_SetWindow(); /* empty stub; the original passes (0, 0x280) */
    DF_M1HELP_SetString(DF_MSTR_GetInfo(MenuMng.language, 0, 0xdc372));
    DF_M1HELP_FadeIn();
    __MTopRT_ChangeCursor(pMobj, rt->cursor_index, -1);
    switch (rt->cursor_index) {
    case 3:
        if (phase == 5) phase = 0x19;
        MUnitTopSystem_FadeInStart(rt->pMobjUnitTopSystem, phase);
        break;
    case 2:
        MUnitTopArena_FadeInStart(rt->pMobjUnitTopArena, phase);
        break;
    case 1:
        MUnitTopTraining_FadeInStart(rt->pMobjUnitTopTraining, phase);
        break;
    case 0:
        MUnitTopBase_SetData(rt->pMobjUnitTopBase, PlayerInfo_GetAcName(0, PlayerInfo_GetCurrentAcNo(0)));
        break;
    }
    if (rt->cursor_index == 0)
        MUnitTopBase_FadeInStart(rt->pMobjUnitTopBase, phase, 2);
    else
        MUnitTopBase_FadeInStart(rt->pMobjUnitTopBase, phase, 1);
    rt->flagFadeOut = 0;
}

static void __MTopRT_CreateItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x80);
    struct stMTopRT *rt = RT(pMobj);

    DF_MBACK_PushSceneLabel((enum_5124d09c)1);
    rt->pMobjUnitTopBase = MUnitTopBase_NewSystem(pMobj, 1);
    rt->pMobjUnitTopTraining = MUnitTopTraining_NewSystem(pMobj);
    rt->pMobjUnitTopSystem = MUnitTopSystem_NewSystem(pMobj, 1, 0);
    rt->pMobjUnitTopArena = MUnitTopArena_NewSystem(pMobj);

    /* handed to the interpreted DF_MGEN_NewModelEasy: at their original slots (rodata 0x1ee0410 / 0x1ee0420, the
     * colour = the gp word 0x80808080 copied as raw bits; sp+0x7c is not 16-aligned) */
    PS2_STACK(float[4], pos, 0x60);
    PS2_STACK(float[4], ang, 0x50);
    PS2_STACK(struct struct_76b810ec, col, 0x7c);
    const unsigned int col_word = 0x80808080u;
    memcpy(&col, &col_word, 4);
    pos[0] = 0.0f;
    pos[1] = 0.0f;
    pos[2] = 1.0f;
    pos[3] = 0.0f;
    ang[0] = 0.0f;
    ang[1] = MTOPRT_PI;
    ang[2] = 0.0f;
    ang[3] = 1.0f;

    for (int i = 0; i < 4; i++) {
        rt->apModel[i] = DF_MGEN_NewModelEasy(0x514 + i, &pos, &ang, NULL, &col);
        rt->apModel[i]->test_reg = 0x70000;
        rt->apModel[i]->alpha_reg = 0x44;
        rt->apModel[i]->mulalpha = 1.0f;
    }
    for (int i = 4; i < 8; i++) {
        rt->apModel[i] = DF_MGEN_NewModelEasy(0x51e + (i - 4), &pos, &ang, NULL, &col);
        rt->apModel[i]->test_reg = 0x30000;
        rt->apModel[i]->alpha_reg = 0x48;
        rt->apModel[i]->mulalpha = 1.0f;
    }
}

static void __MTopRT_Pkt(struct stMobj *pMobj)
{
    PS2_FRAME(0x70);
    struct stMTopRT *rt = RT(pMobj);
    PS2_STACK(struct struct_76b810ec, col, 0x6c);
    const unsigned int col_word = 0x80808080u;   /* gp word 0x27bebc, raw bits */
    memcpy(&col, &col_word, 4);

    if (!(0.01f < rt->rateFadeMaster)) return;
    unsigned int *dmap = DF_MPKT_MakeVIFCode(MPktCurrentp, 0, 0, 0, 0x13000000);
    for (int i = 0; i < 4; i++) {
        rt->apModel[i]->pos[2] = -1.0f * rt->rateFade[i];
        rt->apModel[4 + i]->pos[2] = -1.0f * rt->rateFade[i];
        col.a = mtoprt_f2u8(128.0f * (0.01f + rt->rateFadeMaster));
        DF_MMODEL_SetVertColor(rt->apModel[i], 0, &col);
        col.a = mtoprt_f2u8(128.0f * (0.01f + rt->rateFade[i] * rt->rateFadeMaster));
        DF_MMODEL_SetVertColor(rt->apModel[4 + i], 0, &col);
    }
    for (int j = 0; j < 8; j++) {
        if (j < 4 || !(rt->rateFade[j - 4] <= 0.01f))
            dmap = DF_MMODEL_MakeVu1PacketAC(dmap, rt->apModel[j], NULL, 0xf);
    }
    MPktCurrentp = dmap;
}

static void __MTopRT_Draw(void)
{
}

static void __MTopRT_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct stMTopRT *rt = RT(pMobj);

    switch (rt->phase) {
    case 4: __MTopRT_PlayPhaseDispatch(pMobj); break;
    case 3: __MTopRT_PlayPhaseCancel(pMobj); break;
    case 2: __MTopRT_PlayPhaseProceed(pMobj); break;
    case 1: __MTopRT_PlayPhaseMain(pMobj); break;
    }
    /* store first, then clamp (floor 0.01f, not 0) */
    if (rt->flagFadeOut) {
        float m = rt->rateFadeMaster - 0.2f;
        rt->rateFadeMaster = m;
        if (m < 0.0f) rt->rateFadeMaster = 0.01f;
    } else {
        float m = rt->rateFadeMaster + 0.2f;
        rt->rateFadeMaster = m;
        if (1.0f < m) rt->rateFadeMaster = 1.0f;
    }
    if (!(0.01f < rt->rateFadeMaster)) return;
    for (int i = 0; i < 8; i++) DF_MMODEL_Play(rt->apModel[i]);
    for (int k = 0; k < 4; k++) {
        if (rt->cursor_index == k) {
            float r = rt->rateFade[k] + 0.2f;
            rt->rateFade[k] = r;
            if (1.0f < r) rt->rateFade[k] = 1.0f;
        } else {
            float r = rt->rateFade[k] - 0.2f;
            rt->rateFade[k] = r;
            if (r < 0.0f) rt->rateFade[k] = 0.01f;
        }
    }
}

struct stMobj *MTopRT_NewSystem(struct stMobj *pParentMobj, int modeMGC)
{
    PS2_FRAME(0x40);
    struct stMobj *pMobj = DF_MOBJ_GetNewObject(pParentMobj);
    if (pMobj == NULL) return NULL;
    struct stMTopRT *rt = RT(pMobj);

    memset(rt, 0, 0x58);
    rt->cursor_index = 0;
    rt->modeMGC = modeMGC;
    rt->rateFadeMaster = 0.01f;
    pMobj->type = 0;
    pMobj->stat = 1;
    pMobj->count = 0;
    pMobj->play_func = (void (*)(void))GFN(__MTopRT_Play);
    pMobj->draw_func = (void (*)(void))GFN(__MTopRT_Draw);
    pMobj->pkt_func = (void (*)(void))GFN(__MTopRT_Pkt);
    __MTopRT_CreateItem(pMobj);
    if (MenuMng.stat == 4) MenuMng.stat = 0;

    if (rt->modeMGC != 5 && rt->modeMGC != 1) {
        DF_MENU_SetFadeOut(0);
        DF_MVIEW_MotionStart(MViewp, 1);
        DF_MBGUIDE_Show();
        DF_MBACK_SetFadeInNX();
        DF_M1HELP_SetWindow(); /* empty stub; the original passes (0, 0x280) */
        DF_M1HELP_SetString(DF_MSTR_GetInfo(MenuMng.language, 0, 0xdc372));
        DF_M1HELP_FadeIn();
        Game_SetBgmVolume(0, Game_GetOptionValue(0, 1));
        Game_SetSeVolume(0, Game_GetOptionValue(0, 6));
        Bgm_Play();
    }
    DF_MGARAGE_SetMissionAc(0);

    if (rt->modeMGC == 5) {
        rt->cursor_index = 1;
        rt->phase = 2;
        pMobj->count = 0;
        mtoprt_page_fade_out(pMobj, 0x17, 9);
    } else if (rt->modeMGC == 3) {
        rt->cursor_index = 0;
        rt->phase = 2;
        pMobj->count = 0;
        mtoprt_page_fade_out(pMobj, 0x17, 9);
        DF_MENU_SetFadeIn(6);
    } else if (rt->modeMGC == 1) {
        rt->cursor_index = 2;
        rt->phase = 2;
        pMobj->count = 0;
        mtoprt_page_fade_out(pMobj, 0x17, 9);
    } else {
        struct stMenuAC *acp = M_ACp[0][0];
        if (!acp->flag.set && !acp->flag.reading) {
            DF_MMD_SetGeneralMMD();
            acp = M_ACp[0][0];
            DF_M_AC_SetData(acp);
            DF_M_AC_PartsRead(acp);
            acp->alpha.cur = 0;
            acp->flag.rotate = 1;   /* inside the reload block here (Dispatch: always) */
            DF_M_AC_SetFadeIn(M_ACp[0][0], 0xf, 0.0f);
            MenuMng.f_use_readbuff = 0;
        }
        rt->phase = 1;
        pMobj->stat = 1;
        pMobj->count = 0;
        __MTopRT_FadeInItem(pMobj, 1);
        DF_MENU_SetFadeIn(6);
    }
    return pMobj;
}
