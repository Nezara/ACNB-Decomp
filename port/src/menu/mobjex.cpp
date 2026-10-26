/* src/menu/mobjex.cpp — the small class system the menu scene objects (StMScene, StMSceneMng, StMWindow ...) are
 * built on. A class is a _MOBJ_BASE_ST_CLASSINFO ('MOBJ' signature) chained to its base through pBaseClass; its
 * fpInitClassInfo fills the play / draw / packet / destructor / fade callbacks (base classes first). __MObj_New takes
 * a slot from the menu object pool (DF_MOBJ_GetNewObject) and copies the class's callbacks into it, __MObj_Destruct
 * runs the destructors (derived to base) and queues the slot for erase. StMobjCmdTarget adds an MFC-style message
 * map: OnCmdMsg looks the (message, code, id) up in the class's map, then in its base maps, and calls the handler
 * with the argument list its signature (nSig) selects. The base map handles SET_FADEIN (0x401) / SET_FADEOUT (0x402)
 * by calling the class's fade callbacks. */
#include "acnb/sdk.h"

typedef void (*mobjex_fn)(struct stMobj *);

static inline struct stMobjBase *mobjex_base(void *p)
{
    return (struct stMobjBase *)p;
}

/* message handlers (sig 5): call the class's fade callback without testing it; result 1 */
static int __CmdTarget_OnMsg_SET_FADEOUT(struct _StMobjCmdTarget *pThis)
{
    PS2_FRAME(0x10);
    if (pThis) gcall(pThis->Base.pClassInfo->fadeout_func, (struct stMobj *)pThis);
    return 1;
}

static int __CmdTarget_OnMsg_SET_FADEIN(struct _StMobjCmdTarget *pThis)
{
    PS2_FRAME(0x10);
    if (pThis) gcall(pThis->Base.pClassInfo->fadein_func, (struct stMobj *)pThis);
    return 1;
}

/* nCode = message << 16 | code (message 0 = 0x111); the first matching entry of the class's own map wins, the
 * base maps are searched only when nothing matches. Signatures: 1 fn(this) -> 1, 2 fn(this) -> its result,
 * 3 fn(this, nID) -> 1, 4 fn(this, pExtra) -> 1, 5 fn(this, pExtra) -> its result, others -> 0. */
int StMobjCmdTarget_OnCmdMsg(struct _StMobjCmdTarget *pTarget, unsigned short nID, unsigned int nCode, void *pExtra)
{
    PS2_FRAME(0x60);
    unsigned short msg = (unsigned short)(nCode >> 16);
    unsigned short code = (unsigned short)nCode;
    struct _StMobjCmdTarget_CLASSINFO *ci = (struct _StMobjCmdTarget_CLASSINFO *)pTarget->Base.pClassInfo;
    struct StMobjMsgMap *map;

    if (msg == 0) msg = 0x111;
    /* the original leaves pTarget in a0 for the (void) GetMsgMap */
    map = gcall_as<struct StMobjMsgMap *(struct _StMobjCmdTarget *)>(ci->fpGetMsgMap, pTarget);
    for (; map; map = map->pBaseMap) {
        struct StMobjMsgEntry *e = map->pMsgEntry;
        if (!e) continue;
        for (; e->nMessage != 0; e++) {
            if (e->nMessage != msg || e->nCode != code || nID < e->nID || nID > e->nLastID) continue;
            switch (e->nSig) {
            case 1: gcall_as<void(struct _StMobjCmdTarget *)>(e->pfn, pTarget); return 1;
            case 2: return gcall_as<int(struct _StMobjCmdTarget *)>(e->pfn, pTarget);
            case 3: gcall_as<void(struct _StMobjCmdTarget *, unsigned short)>(e->pfn, pTarget, nID); return 1;
            case 4: gcall_as<void(struct _StMobjCmdTarget *, void *)>(e->pfn, pTarget, pExtra); return 1;
            case 5: return gcall_as<int(struct _StMobjCmdTarget *, void *)>(e->pfn, pTarget, pExtra);
            default: return 0;
            }
        }
    }
    return 0;
}

/* virtual dispatch: tail call of the target class's fpOnCmdMsg with the same arguments */
int MCmdTarget_OnCmdMsg(void *pTarget, unsigned short nID, unsigned int nCode, void *pExtra)
{
    struct _StMobjCmdTarget_CLASSINFO *ci =
        (struct _StMobjCmdTarget_CLASSINFO *)mobjex_base(pTarget)->pClassInfo;
    return gcall(ci->fpOnCmdMsg, (struct _StMobjCmdTarget *)pTarget, nID, nCode, pExtra);
}

struct StMobjMsgMap *StMobjCmdTarget_GetMsgMap(void)
{
    return &StMobjCmdTarget_s_messageMap;
}

void StMobjCmdTarget_InitClassInfo(struct _StMobjCmdTarget_CLASSINFO *pClassInfo)
{
    pClassInfo->fpGetMsgMap = GFN(StMobjCmdTarget_GetMsgMap);
    pClassInfo->fpOnCmdMsg = GFN(StMobjCmdTarget_OnCmdMsg);
}

struct _MOBJ_BASE_ST_CLASSINFO *MObj_GetMyClassInfo(struct stMobjBase *pObj)
{
    return pObj->pClassInfo;
}

/* destructors, then hide the slot and flag it for DF_MOBJ_Play's erase pass */
void __MObj_Destruct(struct stMobj *pObj)
{
    PS2_FRAME(0x20);
    if (!pObj) return;
    __MObj_DestructAll(mobjex_base(pObj)->pClassInfo, mobjex_base(pObj));
    if (pObj) {
        pObj->f_draw = 0;
        pObj->f_erase = 1;
    }
}

/* the class info is initialised on first use even when no slot is free; the object keeps type 0 */
struct stMobj *__MObj_New(struct stMobj *pParent, struct _MOBJ_BASE_ST_CLASSINFO *pClassInfo)
{
    PS2_FRAME(0x30);
    struct stMobj *obj = DF_MOBJ_GetNewObject(pParent);

    if (!pClassInfo->initialized) {
        __MObj_InitClassInfo(pClassInfo, pClassInfo);
        pClassInfo->initialized = 1;
    }
    if (obj) {
        mobjex_base(obj)->pClassInfo = pClassInfo;
        mobjex_base(obj)->play_func = pClassInfo->play_func;
        mobjex_base(obj)->draw_func = pClassInfo->draw_func;
        mobjex_base(obj)->pkt_func = pClassInfo->pkt_func;
        __MObj_ConstructAll(pClassInfo, mobjex_base(obj));
    }
    return obj;
}

/* dtor_func of each class, derived to base (the original unrolls six levels and recurses for deeper chains) */
void __MObj_DestructAll(struct _MOBJ_BASE_ST_CLASSINFO *pBaseInfo, struct stMobjBase *pObj)
{
    PS2_FRAME(0x30);
    struct _MOBJ_BASE_ST_CLASSINFO *c = pBaseInfo;

    for (int depth = 0;;) {
        if (c->dtor_func) gcall(c->dtor_func, (struct stMobj *)pObj);
        c = c->pBaseClass;
        if (!c) return;
        if (++depth == 6) {
            __MObj_DestructAll(c, pObj);
            return;
        }
    }
}

/* this build's class info has no constructor: the original only walks the pBaseClass chain (nine levels unrolled,
 * then a recursive call) and calls nothing */
void __MObj_ConstructAll(struct _MOBJ_BASE_ST_CLASSINFO *pBaseInfo, struct stMobjBase *pObj)
{
    (void)pBaseInfo;
    (void)pObj;
}

/* fpInitClassInfo(pClassInfo) of every class in pBaseInfo's chain, root first and pBaseInfo last (the original
 * unrolls nine levels and recurses for deeper chains) */
void __MObj_InitClassInfo(struct _MOBJ_BASE_ST_CLASSINFO *pBaseInfo, struct _MOBJ_BASE_ST_CLASSINFO *pClassInfo)
{
    PS2_FRAME(0xb0);
    struct _MOBJ_BASE_ST_CLASSINFO *chain[10];
    int n = 1;

    chain[0] = pBaseInfo;
    while (n < 10 && (chain[n] = chain[n - 1]->pBaseClass) != 0) n++;
    if (n == 10) {
        __MObj_InitClassInfo(chain[9], pClassInfo);
        n = 9;
    }
    while (n-- > 0)
        if (chain[n]->fpInitClassInfo) gcall(chain[n]->fpInitClassInfo, pClassInfo);
}

#ifdef ACNB_SOURCEPORT
/* class definitions and the message map (signature "MOBJ"); StMobjCmdTarget's callbacks are filled by its
 * InitClassInfo at the first __MObj_New of a derived class */
struct _MOBJ_BASE_ST_CLASSINFO MOBJ_BASE_ST_s_ClassInfo = {.signature = 0x4a424f4d, .name = (char *)"MOBJ_BASE_ST",
    .size = 0x14, .initialized = 1};
struct StMobjMsgEntry StMobjCmdTarget_s_messageEntries[3] = {
    {0x401, 0, 0, 0, 5, (void (*)(struct stMobjBase *))__CmdTarget_OnMsg_SET_FADEIN},
    {0x402, 0, 0, 0, 5, (void (*)(struct stMobjBase *))__CmdTarget_OnMsg_SET_FADEOUT},
    {}};
struct StMobjMsgMap StMobjCmdTarget_s_messageMap = {0, StMobjCmdTarget_s_messageEntries};
struct _StMobjCmdTarget_CLASSINFO StMobjCmdTarget_s_ClassInfo = {.Base = {.signature = 0x4a424f4d,
    .name = (char *)"StMobjCmdTarget", .size = 0x14, .pBaseClass = &MOBJ_BASE_ST_s_ClassInfo,
    .fpInitClassInfo = (void (*)(struct _MOBJ_BASE_ST_CLASSINFO *))StMobjCmdTarget_InitClassInfo}};
#endif
