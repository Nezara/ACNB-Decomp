/* src/menu/mobj_manager.cpp — data-driven object sets for the MUnit* menu screens (arena, colour bar, detail, ranking
 * ...). The caller describes its objects as an array of stMobjManager records (layout header + part name, count,
 * kind, string / window extras) and keeps a parallel array of stMobj pointers: MOBJManager_Create builds one object
 * per record (or numContinue objects whose part names are sprintf(name, j)), Move / ResetPos / FadeIn / FadeOut /
 * Delete walk the same arrays and call the per-kind callback from a 5-entry table (kind 0 dummy, 1 unit sprite,
 * 2 string, 3 WindowEx, 4 blinking unit sprite). A record with numContinue n occupies n slots: the drivers keep
 * `extra` (sum of n - 1) so the slot of record i, item j is i + extra + j. The objects are created hidden; the
 * fade-in callbacks show them. */
#include "acnb/sdk.h"

typedef struct stMobj *(*mman_create_fn)(struct stMobj *, struct stMobj **, struct _LO_HEADER *, char *,
                                         struct stMobjManager *, int);
typedef char (*mman_move_fn)(struct stMobj *, int, int, int);
typedef char (*mman_obj_fn)(struct stMobj *);

/* the callback tables (OLMENU 0x1edf650 .. 0x1edf6f0, copied to the drivers' stacks): kind order dummy, unit sprite,
 * string, WindowEx, blinking unit sprite. Move and ResetPos use the same Move callbacks. */
static inline mman_create_fn mman_create_tab(int type)
{
    switch (type) {
    case 0: return (mman_create_fn)GFN(__MOBJManager_CreateDummy);
    case 1: return (mman_create_fn)GFN(__MOBJManager_CreateUnitSprite);
    case 2: return (mman_create_fn)GFN(__MOBJManager_CreateString);
    case 3: return (mman_create_fn)GFN(__MOBJManager_CreateWindowEx);
    default: return (mman_create_fn)GFN(__MOBJManager_CreateUnitSpriteBlink);
    }
}

static inline mman_move_fn mman_move_tab(int type)
{
    switch (type) {
    case 0: return (mman_move_fn)GFN(__MOBJManager_MoveDummy);
    case 1: return (mman_move_fn)GFN(__MOBJManager_MoveUnitSprite);
    case 2: return (mman_move_fn)GFN(__MOBJManager_MoveString);
    case 3: return (mman_move_fn)GFN(__MOBJManager_MoveWindowEx);
    default: return (mman_move_fn)GFN(__MOBJManager_MoveUnitSpriteBlink);
    }
}

static inline mman_obj_fn mman_delete_tab(int type)
{
    switch (type) {
    case 0: return (mman_obj_fn)GFN(__MOBJManager_DeleteDummy);
    case 1: return (mman_obj_fn)GFN(__MOBJManager_DeleteUnitSprite);
    case 2: return (mman_obj_fn)GFN(__MOBJManager_DeleteString);
    case 3: return (mman_obj_fn)GFN(__MOBJManager_DeleteWindowEx);
    default: return (mman_obj_fn)GFN(__MOBJManager_DeleteUnitSpriteBlink);
    }
}

static inline mman_obj_fn mman_fadeout_tab(int type)
{
    switch (type) {
    case 0: return (mman_obj_fn)GFN(__MOBJManager_FadeOutDummy);
    case 1: return (mman_obj_fn)GFN(__MOBJManager_FadeOutUnitSprite);
    case 2: return (mman_obj_fn)GFN(__MOBJManager_FadeOutString);
    case 3: return (mman_obj_fn)GFN(__MOBJManager_FadeOutWindowEx);
    default: return (mman_obj_fn)GFN(__MOBJManager_FadeOutUnitSpriteBlink);
    }
}

static inline mman_obj_fn mman_fadein_tab(int type)
{
    switch (type) {
    case 0: return (mman_obj_fn)GFN(__MOBJManager_FadeInDummy);
    case 1: return (mman_obj_fn)GFN(__MOBJManager_FadeInUnitSprite);
    case 2: return (mman_obj_fn)GFN(__MOBJManager_FadeInString);
    case 3: return (mman_obj_fn)GFN(__MOBJManager_FadeInWindowEx);
    default: return (mman_obj_fn)GFN(__MOBJManager_FadeInUnitSpriteBlink);
    }
}

/* ---- Move callbacks: flag 0 = add (x, y), 2 = set ---- */

/* flag 2 resets the offset from the layout position to (0, 0) (x / y ignored); any other flag but 0 leaves pos
 * unwritten and SetPosition gets the stale stack words (PS2_STACK) */
static char __MOBJManager_MoveUnitSpriteBlink(struct stMobj *pMobj, int x, int y, int flag)
{
    PS2_FRAME(0x50);
    PS2_STACK(struct struct_214acd28, pos, 0x48);

    if (flag == 2) {
        pos.x = 0;
        pos.y = 0;
    } else if (flag == 0) {
        UnitSprite_GetPosition(pMobj, &pos);
        pos.x += x;
        pos.y += y;
    }
    UnitSprite_SetPosition(pMobj, &pos);
    return 0;
}

static char __MOBJManager_MoveWindowEx(struct stMobj *pMobj, int x, int y, int flag)
{
    PS2_FRAME(0x20);
    PS2_STACK(struct struct_214acd28, pos, 0x18);

    pos.x = x;
    pos.y = y;
    if (flag == 2) WindowEx_MovePos(pMobj, &pos, 0);
    else if (flag == 0) WindowEx_MoveAddPos(pMobj, &pos, 0);
    return 0;
}

/* writes the string position without a type test */
static char __MOBJManager_MoveString(struct stMobj *pMobj, int x, int y, int flag)
{
    if (flag == 2) {
        pMobj->dt.string.x = (short)x;
        pMobj->dt.string.y = (short)y;
    } else if (flag == 0) {
        pMobj->dt.string.x = (short)(pMobj->dt.string.x + (short)x);
        pMobj->dt.string.y = (short)(pMobj->dt.string.y + (short)y);
    }
    return 0;
}

static char __MOBJManager_MoveUnitSprite(struct stMobj *pMobj, int x, int y, int flag)
{
    PS2_FRAME(0x50);
    PS2_STACK(struct struct_214acd28, pos, 0x48);

    if (flag == 2) {
        pos.x = 0;
        pos.y = 0;
    } else if (flag == 0) {
        UnitSprite_GetPosition(pMobj, &pos);
        pos.x += x;
        pos.y += y;
    }
    UnitSprite_SetPosition(pMobj, &pos);
    return 0;
}

static char __MOBJManager_MoveDummy(void)
{
    return 0;
}

/* ---- Delete callbacks ---- */

static char __MOBJManager_DeleteUnitSpriteBlink(struct stMobj *pMobj)
{
    PS2_FRAME(0x10);
    UnitSprite_Delete(pMobj);
    return 0;
}

static char __MOBJManager_DeleteWindowEx(struct stMobj *pMobj)
{
    PS2_FRAME(0x10);
    WindowEx_Delete(pMobj);
    return 0;
}

/* queues the string for DF_MOBJ_Play's erase pass */
static char __MOBJManager_DeleteString(struct stMobj *pMobj)
{
    if (pMobj) {
        pMobj->f_draw = 0;
        pMobj->f_erase = 1;
    }
    return 0;
}

static char __MOBJManager_DeleteUnitSprite(struct stMobj *pMobj)
{
    PS2_FRAME(0x10);
    UnitSprite_Delete(pMobj);
    return 0;
}

static char __MOBJManager_DeleteDummy(void)
{
    return 0;
}

/* ---- FadeOut callbacks ---- */

static char __MOBJManager_FadeOutUnitSpriteBlink(struct stMobj *pMobj)
{
    PS2_FRAME(0x10);
    UnitSprite_SetFadeOut(pMobj);
    return 0;
}

static char __MOBJManager_FadeOutWindowEx(struct stMobj *pMobj)
{
    PS2_FRAME(0x10);
    WindowEx_SetFadeOut(pMobj);
    return 0;
}

static char __MOBJManager_FadeOutString(struct stMobj *pMobj)
{
    PS2_FRAME(0x10);
    DF_MOBJ_SetFadeOut(pMobj);
    return 0;
}

static char __MOBJManager_FadeOutUnitSprite(struct stMobj *pMobj)
{
    PS2_FRAME(0x10);
    UnitSprite_SetFadeOut(pMobj);
    return 0;
}

static char __MOBJManager_FadeOutDummy(void)
{
    return 0;
}

/* ---- FadeIn callbacks: show the (hidden) object, then start its fade-in ---- */

static char __MOBJManager_FadeInUnitSpriteBlink(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    DF_MOBJ_SetDrawEnable(pMobj, 1);
    UnitSprite_SetFadeIn(pMobj);
    return 0;
}

static char __MOBJManager_FadeInWindowEx(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    DF_MOBJ_SetDrawEnable(pMobj, 1);
    WindowEx_SetFadeIn(pMobj);
    return 0;
}

static char __MOBJManager_FadeInString(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    DF_MOBJ_SetDrawEnable(pMobj, 1);
    DF_MOBJ_SetFadeIn(pMobj);
    return 0;
}

static char __MOBJManager_FadeInUnitSprite(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    DF_MOBJ_SetDrawEnable(pMobj, 1);
    UnitSprite_SetFadeIn(pMobj);
    return 0;
}

static char __MOBJManager_FadeInDummy(void)
{
    return 0;
}

/* ---- Create callbacks: store the new object in *ppMobjOut, hide it and return the slot pointer ---- */

static struct stMobj *__MOBJManager_CreateUnitSpriteBlink(struct stMobj *pParent, struct stMobj **ppMobjOut,
                                                          struct _LO_HEADER *pLayoutHeader, char *pLayoutName)
{
    PS2_FRAME(0x20);
    *ppMobjOut = UnitSpriteBlink_New(pParent, pLayoutHeader, pLayoutName);
    DF_MOBJ_SetDrawEnable(*ppMobjOut, 0);
    return (struct stMobj *)ppMobjOut;
}

static struct stMobj *__MOBJManager_CreateWindowEx(struct stMobj *pParent, struct stMobj **ppMobjOut,
                                                   struct _LO_HEADER *pLayoutHeader, char *pLayoutName,
                                                   struct stMobjManager *pMngData)
{
    PS2_FRAME(0x20);
    *ppMobjOut = WindowEx_NewFromLayout(pParent, pMngData->exWindowEx.windowType, (char *)pLayoutHeader, pLayoutName);
    DF_MOBJ_SetDrawEnable(*ppMobjOut, 0);
    return (struct stMobj *)ppMobjOut;
}

/* string id = strid + index (the item of a numContinue record) */
static struct stMobj *__MOBJManager_CreateString(struct stMobj *pParent, struct stMobj **ppMobjOut,
                                                 struct _LO_HEADER *pLayoutHeader, char *pLayoutName,
                                                 struct stMobjManager *pMngData, int index)
{
    PS2_FRAME(0x30);
    struct stMobjManagerExDataString *ex = &pMngData->exString;

    *ppMobjOut = DF_MOBJ_NewStringFromLayoutName(pParent, ex->category, ex->strid + index, ex->refType,
                                                 (char *)pLayoutHeader, pLayoutName, ex->color, ex->depth, ex->flag);
    DF_MOBJ_SetDrawEnable(*ppMobjOut, 0);
    return (struct stMobj *)ppMobjOut;
}

static struct stMobj *__MOBJManager_CreateUnitSprite(struct stMobj *pParent, struct stMobj **ppMobjOut,
                                                     struct _LO_HEADER *pLayoutHeader, char *pLayoutName)
{
    PS2_FRAME(0x20);
    *ppMobjOut = UnitSprite_New(pParent, pLayoutHeader, pLayoutName);
    DF_MOBJ_SetDrawEnable(*ppMobjOut, 0);
    return (struct stMobj *)ppMobjOut;
}

static struct stMobj *__MOBJManager_CreateDummy(void)
{
    return 0;
}

/* ---- drivers ---- */

/* moves every object to its layout position (Move callback, flag 2: the unit sprites reset their offset);
 * the layout record is not cleared: a part missing from the layout moves to the stale stack x / y */
char MOBJManager_ResetPos(struct stMobjManager *pMngData, int numMngData, struct stMobj **ppMobjData)
{
    PS2_FRAME(0x160);
    PS2_STACK(struct _LAYOUT_RET_DATA, lo, 0xd0);
    PS2_STACK(char[0x40], name, 0x120);
    struct _LO_HEADER *hdr = 0;
    int extra = 0;

    if (!pMngData || !ppMobjData) return -1;
    for (int i = 0; i < numMngData; i++, pMngData++) {
        int type = pMngData->typeMObj;
        if (type == 0) continue;
        if (pMngData->pLayoutHeader) hdr = pMngData->pLayoutHeader;
        int n = pMngData->numContinue;
        if (n != 0) {
            for (int j = 0; j < n; j++) {
                struct stMobj **slot = &ppMobjData[extra + i + j];
                if (!*slot) continue;
                ps2_sprintf_d(name, pMngData->pLayoutName, j);
                DF_LO_GetLayoutData((unsigned char *)hdr, (unsigned char *)name, 0, &lo);
                gcall(mman_move_tab(type), *slot, lo.Pos.x, lo.Pos.y, 2);
            }
            extra += n - 1;
        } else {
            struct stMobj **slot = &ppMobjData[i + extra];
            if (!*slot) continue;
            DF_LO_GetLayoutData((unsigned char *)hdr, (unsigned char *)pMngData->pLayoutName, 0, &lo);
            gcall(mman_move_tab(type), *slot, lo.Pos.x, lo.Pos.y, 2);
        }
    }
    return 0;
}

char MOBJManager_Move(struct stMobjManager *pMngData, int numMngData, struct stMobj **ppMobjData, int x, int y,
                      int flag)
{
    PS2_FRAME(0xd0);
    int extra = 0;

    if (!pMngData || !ppMobjData) return -1;
    for (int i = 0; i < numMngData; i++, pMngData++) {
        int type = pMngData->typeMObj;
        if (type == 0) continue;
        int n = pMngData->numContinue;
        if (n != 0) {
            for (int j = 0; j < n; j++) {
                struct stMobj *obj = ppMobjData[extra + i + j];
                if (obj) gcall(mman_move_tab(type), obj, x, y, flag);
            }
            extra += n - 1;
        } else {
            struct stMobj *obj = ppMobjData[i + extra];
            if (obj) gcall(mman_move_tab(type), obj, x, y, flag);
        }
    }
    return 0;
}

/* The single-object branch clears ppMobjData[i + extra + s4], where s4 is the item counter register of the
 * numContinue branch: n (or 0 for n < 0) after such a record, before the first one the CALLER's s4
 * (PS2_STALE_GPR). With s4 != 0 the deleted object keeps its pointer and another slot is cleared. */
char MOBJManager_Delete(struct stMobj **ppMobjData, int numMngData, struct stMobjManager *pMngData)
{
    PS2_FRAME(0xc0);
    int s4 = (int)PS2_STALE_GPR(20 /* s4 */);
    int extra = 0;

    if (!ppMobjData || !pMngData) return -1;
    for (int i = 0; i < numMngData; i++, pMngData++) {
        int type = pMngData->typeMObj;
        if (type == 0) continue;
        int n = pMngData->numContinue;
        if (n != 0) {
            for (s4 = 0; s4 < n; s4++) {
                struct stMobj **slot = &ppMobjData[extra + i + s4];
                if (*slot) {
                    gcall(mman_delete_tab(type), *slot);
                    *slot = 0;
                }
            }
            extra += n - 1;
        } else {
            struct stMobj *obj = ppMobjData[i + extra];
            if (obj) {
                gcall(mman_delete_tab(type), obj);
                ppMobjData[i + extra + s4] = 0;
            }
        }
    }
    return 0;
}

/* FadeOut / FadeIn handle the records start .. start + num - 1; the slot offset of the records before start is
 * summed without the type test of the main loop */
static void mman_fade(struct stMobj **ppMobjData, int startMngData, int numMngData, struct stMobjManager *pMngData,
                      int fade_in)
{
    int extra = 0;

    for (int i = 0; i < startMngData; i++)
        if (pMngData[i].numContinue != 0) extra += pMngData[i].numContinue - 1;
    for (int i = startMngData; i < startMngData + numMngData; i++) {
        struct stMobjManager *e = &pMngData[i];
        int type = e->typeMObj;
        if (type == 0) continue;
        mman_obj_fn fn = fade_in ? mman_fadein_tab(type) : mman_fadeout_tab(type);
        int n = e->numContinue;
        if (n != 0) {
            for (int j = 0; j < n; j++) {
                struct stMobj *obj = ppMobjData[extra + i + j];
                if (obj) gcall(fn, obj);
            }
            extra += n - 1;
        } else {
            struct stMobj *obj = ppMobjData[i + extra];
            if (obj) gcall(fn, obj);
        }
    }
}

char MOBJManager_FadeOut(struct stMobj **ppMobjData, int startMngData, int numMngData, struct stMobjManager *pMngData)
{
    PS2_FRAME(0xb0);
    if (!ppMobjData || !pMngData) return -1;
    mman_fade(ppMobjData, startMngData, numMngData, pMngData, 0);
    return 0;
}

char MOBJManager_FadeIn(struct stMobj **ppMobjData, int startMngData, int numMngData, struct stMobjManager *pMngData)
{
    PS2_FRAME(0xb0);
    if (!ppMobjData || !pMngData) return -1;
    mman_fade(ppMobjData, startMngData, numMngData, pMngData, 1);
    return 0;
}

/* pParent is not tested; a NULL pLayoutHeader reuses the previous record's header */
char MOBJManager_Create(struct stMobj *pParent, struct stMobj **ppMobjOut, int numMngData,
                        struct stMobjManager *pMngData)
{
    PS2_FRAME(0x120);
    PS2_STACK(char[0x40], name, 0xc0);
    struct _LO_HEADER *hdr = 0;
    int extra = 0;

    if (!ppMobjOut || !pMngData) return -1;
    for (int i = 0; i < numMngData; i++, pMngData++) {
        int type = pMngData->typeMObj;
        if (type == 0) continue;
        if (pMngData->pLayoutHeader) hdr = pMngData->pLayoutHeader;
        int n = pMngData->numContinue;
        if (n != 0) {
            mman_create_fn fn = mman_create_tab(type);
            for (int j = 0; j < n; j++) {
                ps2_sprintf_d(name, pMngData->pLayoutName, j);
                gcall(fn, pParent, &ppMobjOut[extra + i + j], hdr, (char *)name, pMngData, j);
            }
            extra += n - 1;
        } else {
            gcall(mman_create_tab(type), pParent, &ppMobjOut[i + extra], hdr, pMngData->pLayoutName, pMngData, 0);
        }
    }
    return 0;
}
