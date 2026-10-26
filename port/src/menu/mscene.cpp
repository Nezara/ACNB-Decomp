/* src/menu/mscene.cpp — menu scenes (OLMENU), a small MFC-like layer on the menu object classes (mobjex): a scene
 * manager owns the current and the next scene, switches them in Update (detach the old one, attach the new one) and
 * forwards Update / Draw / MakePacket / command messages to the current scene through its class info. A wrap scene
 * turns an ordinary menu object (its play / draw / packet callbacks) into a scene that reports its exit to the
 * manager when the object finishes. MScene's own virtual methods are empty. */

static inline struct _StMScene_CLASSINFO *mscene_ci(void *obj)
{
    return (struct _StMScene_CLASSINFO *)MObj_GetMyClassInfo((struct stMobjBase *)obj);
}

/* the current scene handles the message first, then the manager's own message map (its raw result) */
int MSceneMng_OnCmdMsg(struct _StMSceneMng *pThis, unsigned short nID, unsigned int nCode, void *pExtra)
{
    PS2_FRAME(0x50);
    if (pThis->pCurScene && StMobjCmdTarget_OnCmdMsg(&pThis->pCurScene->CmdTarget, nID, nCode, pExtra)) return 1;
    return StMobjCmdTarget_OnCmdMsg(&pThis->MScene.CmdTarget, nID, nCode, pExtra);
}

void MSceneMng_Detach(struct _StMSceneMng *pThis, struct _StMScene *pDetachScene)
{
    PS2_FRAME(0x20);
    if (pThis->pCurScene == pDetachScene) pThis->pCurScene = 0;
    if (pThis->pNextScene == pDetachScene) pThis->pNextScene = 0;
    if (pDetachScene->pSceneMng) {
        pDetachScene->pSceneMng = 0;
        gcall(mscene_ci(pDetachScene)->fpOnDetach, pDetachScene);
    }
}

void MSceneMng_MakePacket(struct _StMSceneMng *pThis, int dummy)
{
    struct _StMScene *cur;

    PS2_FRAME(0x30);
    if (!pThis || !(cur = pThis->pCurScene)) return;
    if (DF_MOBJ_IsFinished((struct stMobj *)cur) || !cur) return;
    gcall(mscene_ci(cur)->fpMakePacket, cur, dummy);
}

void MSceneMng_Draw(struct _StMSceneMng *pThis, int dummy)
{
    struct _StMScene *cur;

    PS2_FRAME(0x30);
    if (!pThis || !(cur = pThis->pCurScene)) return;
    if (DF_MOBJ_IsFinished((struct stMobj *)cur) || !cur) return;
    gcall(mscene_ci(cur)->fpDraw, cur, dummy);
}

/* switches to the next scene if it changed (detach the old one, attach the new one, a finished scene is restarted),
 * then updates the current scene */
void MSceneMng_Update(struct _StMSceneMng *pThis, float dt)
{
    struct _StMScene *cur, *next;

    PS2_FRAME(0x40);
    cur = pThis->pCurScene;
    if (cur != pThis->pNextScene) {
        if (cur) {
            pThis->pCurScene = 0;
            if (pThis->pNextScene == cur) pThis->pNextScene = 0;
            if (cur->pSceneMng) {
                cur->pSceneMng = 0;
                gcall(mscene_ci(cur)->fpOnDetach, cur);
            }
        }
        pThis->pCurScene = pThis->pNextScene;
        if (pThis->pCurScene) {
            next = pThis->pNextScene;
            next->pSceneMng = pThis;
            if (((struct stMobj *)next)->stat == 5) ((struct stMobj *)next)->stat = 0;
            gcall(mscene_ci(next)->fpOnAttach, next);
        }
    }
    cur = pThis->pCurScene;
    if (!cur || DF_MOBJ_IsFinished((struct stMobj *)cur) || !cur) return;
    gcall(mscene_ci(cur)->fpUpdate, cur, dt);
}

void MSceneMng_SetNextScene(struct _StMSceneMng *pThis, struct _StMScene *pScene)
{
    pThis->pNextScene = pScene;
}

struct _StMSceneMng *MSceneMng_New(int ctrlID, struct stMobj *pParent, struct _MOBJ_BASE_ST_CLASSINFO *pClassInfo)
{
    struct _StMSceneMng *m;

    PS2_FRAME(0x20);
    m = (struct _StMSceneMng *)__MObj_New(pParent, pClassInfo);
    if (m) m->MScene.ctrlID = (unsigned short)ctrlID;
    return m;
}

struct StMobjMsgMap *StMSceneMng_GetMsgMap(void)
{
    return &StMSceneMng_s_messageMap;
}

void StMSceneMng_InitClassInfo(struct _StMSceneMng_CLASSINFO *pClassInfo)
{
    struct _StMScene_CLASSINFO *ci = &pClassInfo->Base;
    ci->Base.fpGetMsgMap = GFN(StMSceneMng_GetMsgMap);
    ci->Base.fpOnCmdMsg = (decltype(ci->Base.fpOnCmdMsg))GFN(MSceneMng_OnCmdMsg);
    ci->fpUpdate = (decltype(ci->fpUpdate))GFN(MSceneMng_Update);
    ci->fpDraw = (decltype(ci->fpDraw))GFN(MSceneMng_Draw);
    ci->fpMakePacket = (decltype(ci->fpMakePacket))GFN(MSceneMng_MakePacket);
}

static void __StMobjWrapScene_MakePacket(struct _StMobjWrapScene *pThis)
{
    PS2_FRAME(0x10);
    if (pThis && pThis->pChild) gcall(pThis->fp_pkt, pThis->pChild);
}

static void __StMobjWrapScene_Draw(struct _StMobjWrapScene *pThis)
{
    PS2_FRAME(0x10);
    if (pThis && pThis->pChild) gcall(pThis->fp_draw, pThis->pChild);
}

/* f_draw off, f_erase on (byte read-modify-writes as the bitfield stores) */
static inline void mscene_erase(struct stMobj *c)
{
    unsigned char *b = (unsigned char *)c;
    *b = (unsigned char)(*b & ~4);
    *b = (unsigned char)((*b & ~2) | 2);
}

/* runs the wrapped object; once it is finished (stat 5) it is erased, dropped and the exit is reported */
static void __StMobjWrapScene_Update(struct _StMobjWrapScene *pThis)
{
    struct stMobj *c;

    PS2_FRAME(0x20);
    if (!pThis || !pThis->pChild) return;
    gcall(pThis->fp_play, pThis->pChild);
    c = pThis->pChild;
    if (c->stat != 5) return;
    if (c) mscene_erase(c);
    pThis->pChild = 0;
    MScene_NotifyExit(pThis, 0);
}

static void __StMobjWrapScene_dtor(struct _StMobjWrapScene *pThis)
{
    struct stMobj *c;

    if (!pThis || !(c = pThis->pChild)) return;
    pThis->pChild = 0;
    if (c) mscene_erase(c);
}

/* the wrapper takes over the child's play / draw / packet callbacks (the child's own slots are cleared) */
struct _StMScene *MobjWrapScene_New(int IDD, struct stMobj *pChild, struct stMobj *pParent)
{
    struct _StMobjWrapScene *w;

    PS2_FRAME(0x30);
    w = (struct _StMobjWrapScene *)__MObj_New(pParent, (struct _MOBJ_BASE_ST_CLASSINFO *)&StMobjWrapScene_s_ClassInfo);
    if (w) w->Base.ctrlID = (unsigned short)IDD;
    if (w) {
        w->pChild = pChild;
        w->fp_play = (void (*)(struct stMobj *))pChild->play_func;
        w->fp_draw = (void (*)(struct stMobj *))pChild->draw_func;
        w->fp_pkt = (void (*)(struct stMobj *))pChild->pkt_func;
        pChild->play_func = 0;
        pChild->draw_func = 0;
        pChild->pkt_func = 0;
    }
    return &w->Base;
}

void StMobjWrapScene_InitClassInfo(struct _StMobjWrapScene_CLASSINFO *pClassInfo)
{
    struct _StMScene_CLASSINFO *ci = &pClassInfo->Base;
    ci->Base.Base.dtor_func = (decltype(ci->Base.Base.dtor_func))GFN(__StMobjWrapScene_dtor);
    ci->fpUpdate = (decltype(ci->fpUpdate))GFN(__StMobjWrapScene_Update);
    ci->fpDraw = (decltype(ci->fpDraw))GFN(__StMobjWrapScene_Draw);
    ci->fpMakePacket = (decltype(ci->fpMakePacket))GFN(__StMobjWrapScene_MakePacket);
}

/* offers the message to the parent chain (scene -> manager -> ...) until one handles it. The original unrolls 6
 * levels and recurses from the 6th ancestor. */
static int __MScene_Call_ScnMngOnCmdMsg(struct _StMScene *pThis, unsigned short nID, unsigned int nCode, void *pExtra)
{
    struct _StMScene *p;

    PS2_FRAME(0x50);
    for (p = (struct _StMScene *)pThis->pSceneMng; p; p = (struct _StMScene *)p->pSceneMng)
        if (MCmdTarget_OnCmdMsg(p, nID, nCode, pExtra)) return 1;
    return 0;
}

/* DF_MOBJ_IsFinished is called before the null test (as in the original) */
void MScene_MakePacket(void *pThis, int dummy)
{
    PS2_FRAME(0x30);
    if (DF_MOBJ_IsFinished((struct stMobj *)pThis) || !pThis) return;
    gcall(mscene_ci(pThis)->fpMakePacket, (struct _StMScene *)pThis, dummy);
}

void MScene_Draw(void *pThis, int dummy)
{
    PS2_FRAME(0x30);
    if (DF_MOBJ_IsFinished((struct stMobj *)pThis) || !pThis) return;
    gcall(mscene_ci(pThis)->fpDraw, (struct _StMScene *)pThis, dummy);
}

void MScene_Update(void *pThis, float dt)
{
    PS2_FRAME(0x30);
    if (DF_MOBJ_IsFinished((struct stMobj *)pThis) || !pThis) return;
    gcall(mscene_ci(pThis)->fpUpdate, (struct _StMScene *)pThis, dt);
}

/* finishes the scene and sends command 0x403 | ctrlID to the parent chain */
int MScene_NotifyExit(void *pvoid, unsigned short ExitCode)
{
    struct _StMScene *s = (struct _StMScene *)pvoid;

    PS2_FRAME(0x30);
    DF_MOBJ_Finish((struct stMobj *)s);
    return __MScene_Call_ScnMngOnCmdMsg(s, ExitCode, s->ctrlID | 0x4030000u, 0) != 0;
}

struct StMobjMsgMap *StMScene_GetMsgMap(void)
{
    return &StMScene_s_messageMap;
}

void StMScene_InitClassInfo(struct _StMScene_CLASSINFO *pClassInfo)
{
    pClassInfo->Base.fpGetMsgMap = GFN(StMScene_GetMsgMap);
    pClassInfo->Base.Base.play_func = (void (*)(struct stMobj *))GFN(__MScene_play);
    pClassInfo->Base.Base.draw_func = (void (*)(struct stMobj *))GFN(__MScene_draw);
    pClassInfo->Base.Base.pkt_func = (void (*)(struct stMobj *))GFN(__MScene_pkt);
    pClassInfo->fpUpdate = (decltype(pClassInfo->fpUpdate))GFN(__MScene_Update);
    pClassInfo->fpDraw = (decltype(pClassInfo->fpDraw))GFN(__MScene_Draw);
    pClassInfo->fpMakePacket = (decltype(pClassInfo->fpMakePacket))GFN(__MScene_MakePacket);
    pClassInfo->fpOnAttach = (decltype(pClassInfo->fpOnAttach))GFN(__MScene_OnAttach);
    pClassInfo->fpOnDetach = (decltype(pClassInfo->fpOnDetach))GFN(__MScene_OnDetach);
}

/* default virtual methods: empty */
static void __MScene_OnDetach(void) {}
static void __MScene_OnAttach(void) {}
static void __MScene_MakePacket(void) {}
static void __MScene_Draw(void) {}
static void __MScene_Update(void) {}
static void __MScene_pkt(void) {}
static void __MScene_draw(void) {}
static void __MScene_play(void) {}

#ifdef ACNB_SOURCEPORT
/* class definitions and message maps (signature "MOBJ"; base classes: StMScene <- StMobjCmdTarget) */
struct _StMScene_CLASSINFO StMScene_s_ClassInfo = {.Base = {.Base = {.signature = 0x4a424f4d, .name = (char *)"StMScene",
    .size = 0x1c, .pBaseClass = &StMobjCmdTarget_s_ClassInfo.Base,
    .fpInitClassInfo = (void (*)(struct _MOBJ_BASE_ST_CLASSINFO *))StMScene_InitClassInfo}}};
struct StMobjMsgEntry StMScene_s_messageEntries[1] = {};
struct StMobjMsgMap StMScene_s_messageMap = {&StMobjCmdTarget_s_messageMap, StMScene_s_messageEntries};
struct _StMobjWrapScene_CLASSINFO StMobjWrapScene_s_ClassInfo = {.Base = {.Base = {.Base = {.signature = 0x4a424f4d,
    .name = (char *)"StMobjWrapScene", .size = 0x2c, .pBaseClass = &StMScene_s_ClassInfo.Base.Base,
    .fpInitClassInfo = (void (*)(struct _MOBJ_BASE_ST_CLASSINFO *))StMobjWrapScene_InitClassInfo}}}};
struct _StMSceneMng_CLASSINFO StMSceneMng_s_ClassInfo = {.Base = {.Base = {.Base = {.signature = 0x4a424f4d,
    .name = (char *)"StMSceneMng", .size = 0x24, .pBaseClass = &StMScene_s_ClassInfo.Base.Base,
    .fpInitClassInfo = (void (*)(struct _MOBJ_BASE_ST_CLASSINFO *))StMSceneMng_InitClassInfo}}}};
struct StMobjMsgEntry StMSceneMng_s_messageEntries[1] = {};
struct StMobjMsgMap StMSceneMng_s_messageMap = {&StMScene_s_messageMap, StMSceneMng_s_messageEntries};
#endif
