/* src/menu/mtop.cpp - the top-level scene of the main menu (the page with the unit board). DF_MTOP_NewSystem(modeMGC)
 * creates the scene manager StTopSceneMng (an StMSceneMng subclass, 0x30 bytes) and MUnitMng_Inits the unit mobjs;
 * __StTopSceneMng_play advances nSceneStep each time the current child scene (a MobjWrapScene around an stMobj)
 * finishes: step 0 -> MGenTab (tab 2 / label 5), 1 -> MGenTab (tab 3 / label 0xc), 2 -> the real top page
 * MTopRT_NewSystem(modeMGC) (after an 8 MB movie block unless modeMGC is 5 or 1), 3 / other -> teardown
 * (MUnitMng_Cleanup, finish the scene). The class info / message map are DWARF globals read by __MObj_New: the
 * hybrid uses the guest objects, the source port defines them below. */
#include <string.h>
#include "acnb/port.h"
#include "acnb/ps2fp.h"

/* f_draw = 0, f_erase = 1 on the child scene's mobj header, then clear pChildScene (the original re-loads the
 * pointer between the two byte updates) */
static inline void mtop_erase_child(struct _StTopSceneMng *pThis)
{
    pThis->pChildScene->CmdTarget.Base.f_draw = 0;
    pThis->pChildScene->CmdTarget.Base.f_erase = 1;
    pThis->pChildScene = 0;
}

static void __StTopSceneMng_OnQuitScene(struct _StTopSceneMng *pThis)
{
    PS2_FRAME(0x20);
    struct _StMScene *c = pThis->pChildScene;
    if (c) {
        pThis->pChildScene = 0;
        __MObj_Destruct((struct stMobj *)c);
        DF_MOBJ_Finish((struct stMobj *)pThis);
    }
}

static void __StTopSceneMng_pkt(struct _StTopSceneMng *pThis)
{
    MScene_MakePacket(pThis, 0);
}

static void __StTopSceneMng_draw(struct _StTopSceneMng *pThis)
{
    MScene_Draw(pThis, 0);
}

static void __StTopSceneMng_play(struct _StTopSceneMng *pThis)
{
    PS2_FRAME(0x30);
    struct _StMScene *c;
    struct stMobj *child;

    MScene_Update(pThis, ps2_u2f(0x3c888889));   /* 1/60 */
    c = pThis->pChildScene;
    if (c) {
        if (c->CmdTarget.Base.stat != 5)
            return;
        MSceneMng_Detach(&pThis->Base, c);
        if (pThis->pChildScene)
            mtop_erase_child(pThis);
        if (MyI.ending != 0) {
            /* unreached in the replays */
            g_gamedata[0].pointTotal = MUtil_GetTotalPoint();
            pThis->nSceneStep = 3;
        } else {
            int s = pThis->nSceneStep;
            pThis->nSceneStep = (s == 0 || s == 1) ? 2 : s + 1;
        }
    }

    switch (pThis->nSceneStep) {
    case 0:
        child = MGenTab_NewSystem((struct stMobj *)pThis, 2, 5, 0, 0, 0);
        pThis->pChildScene = MobjWrapScene_New(0x101, child, (struct stMobj *)pThis);
        MSceneMng_SetNextScene(&pThis->Base, pThis->pChildScene);
        return;
    case 1:
        child = MGenTab_NewSystem((struct stMobj *)pThis, 3, 0xc, 0, 0, 0);
        pThis->pChildScene = MobjWrapScene_New(0x101, child, (struct stMobj *)pThis);
        MSceneMng_SetNextScene(&pThis->Base, pThis->pChildScene);
        return;
    case 2:
        if (pThis->modeMGC != 5 && pThis->modeMGC != 1) {
            void *buf = DF_MMM_Alloc(0, 0x800000);
            if (buf) {
                memset(buf, 0, 0x800000);
                DF_MENU_PlayMovieBlockId(6, DF_MFUNC_Aligned(buf, 0x40), 1, 0, 0x1e3c);
                MenuTex_InvalidateCache();
                DF_MMM_Free(0, buf);
            }
        }
        child = MTopRT_NewSystem((struct stMobj *)pThis, pThis->modeMGC);
        pThis->pChildScene = MobjWrapScene_New(0x101, child, (struct stMobj *)pThis);
        MSceneMng_SetNextScene(&pThis->Base, pThis->pChildScene);
        return;
    default:   /* 3 and any other step: teardown */
        MUnitMng_Cleanup();
        DF_MOBJ_Finish((struct stMobj *)pThis);
        if (pThis->pChildScene)
            mtop_erase_child(pThis);
        return;
    }
}

struct stMobj *DF_MTOP_NewSystem(int modeMGC)
{
    PS2_FRAME(0x40);
    struct _StTopSceneMng *pThis = (struct _StTopSceneMng *)MSceneMng_New(0x100, 0,
        (struct _MOBJ_BASE_ST_CLASSINFO *)&StTopSceneMng_s_ClassInfo);
    int failed;

    if (!pThis)
        return 0;
    /* a failed, non-free mission result: modes 5 / 1 go straight to the top page too */
    failed = g_nextmenu.misres.flagSuccess == 0 && g_nextmenu.misres.idGameOver != 0 &&
             g_nextmenu.misres.modeFree == 0;
    MUnitMng_Init((struct stMobj *)pThis);
    pThis->modeMGC = modeMGC;
    if (pThis->modeMGC == 5 && !failed)
        pThis->nSceneStep = 0;
    else if (pThis->modeMGC == 1 && !failed)
        pThis->nSceneStep = 1;
    else if (pThis->modeMGC == 6) {
        pThis->nSceneStep = 3;
        MenuMng.stat = 0xe;
    } else
        pThis->nSceneStep = 2;
    return (struct stMobj *)pThis;
}

struct StMobjMsgMap *StTopSceneMng_GetMsgMap(void)
{
    return &StTopSceneMng_s_messageMap;
}

void StTopSceneMng_InitClassInfo(struct _StTopSceneMng_CLASSINFO *pClassInfo)
{
    struct _StMScene_CLASSINFO *ci = &pClassInfo->Base.Base;
    ci->Base.Base.play_func = (void (*)(struct stMobj *))GFN(__StTopSceneMng_play);
    ci->Base.Base.draw_func = (void (*)(struct stMobj *))GFN(__StTopSceneMng_draw);
    ci->Base.Base.pkt_func = (void (*)(struct stMobj *))GFN(__StTopSceneMng_pkt);
    ci->Base.fpGetMsgMap = GFN(StTopSceneMng_GetMsgMap);
}

#ifdef ACNB_SOURCEPORT
/* OLMENU 0x1ee61e0 / 0x1ee6228 / 0x1ee6230: class definition (callbacks filled by InitClassInfo) and message map */
struct _StTopSceneMng_CLASSINFO StTopSceneMng_s_ClassInfo = {.Base = {.Base = {.Base = {.Base = {
    .signature = 0x4a424f4d, .name = (char *)"StTopSceneMng", .size = 0x30,
    .pBaseClass = &StMSceneMng_s_ClassInfo.Base.Base.Base,
    .fpInitClassInfo = (void (*)(struct _MOBJ_BASE_ST_CLASSINFO *))StTopSceneMng_InitClassInfo}}}}};
struct StMobjMsgEntry StTopSceneMng_s_messageEntries[3] = {
    {0x403, 0x2000, 0, 0xffff, 3, (void (*)(struct stMobjBase *))__StTopSceneMng_OnQuitScene},
    {0x403, 0x1000, 0, 0xffff, 3, (void (*)(struct stMobjBase *))__StTopSceneMng_OnQuitScene},
    {}};
struct StMobjMsgMap StTopSceneMng_s_messageMap = {&StMSceneMng_s_messageMap, StTopSceneMng_s_messageEntries};
#endif
