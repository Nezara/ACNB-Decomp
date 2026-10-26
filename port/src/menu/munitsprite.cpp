/* src/menu/munitsprite.cpp — "unit sprites" (OLMENU): an stMobj that owns up to 32 child sprite objects built from one
 * named parts unit of a layout file (DF_LO_GetPartsNumInUnitFromName / DF_LO_GetLayoutData). The parent keeps a shared
 * screen offset (a _MOVER tween), a multiply colour and, for the title screen flash, a scale / angle; its draw function
 * draws every child with that offset and colour (the children's own draw_func is cleared). Kind 0 = plain sprite
 * children (DF_MOBJ_NewSpriteFromLayout), kind 1 = blink sprite children (DF_MOBJ2_NewBlinkSpriteFromLayout).
 * The unit data lives in the stMobj dt union with no DWARF member: struct_36ee7a34 (apSprite[32], nNumSprite, Mover,
 * stMulColor, DrawType, Ang, Scl) at obj + 0x10. */
#include "acnb/hooks.h"

typedef void (*mobj_fn)(void);

static inline struct struct_36ee7a34 *unit_of(struct stMobj *pMobj)
{
    return (struct struct_36ee7a34 *)&pMobj->dt;
}

void UnitSprite_SetAng(struct stMobj *pMobj, float Ang)
{
    unit_of(pMobj)->Ang = Ang;
}

void UnitSprite_SetScl(struct stMobj *pMobj, float Scl)
{
    unit_of(pMobj)->Scl = Scl;
}

/* no NULL test of pMobj (0 callers) */
void UnitSprite_SetTexIdAll(struct stMobj *pMobj, int idTex)
{
    struct struct_36ee7a34 *u = unit_of(pMobj);
    for (int i = 0; i < u->nNumSprite; i++) {
        struct stMobj *c = u->apSprite[i];
        if (c) *(int *)((unsigned char *)c + 0x80) = idTex;
    }
}

void UnitSprite_SetSpriteFlag(struct stMobj *pMobj, int flag)
{
    PS2_FRAME(0x50);
    if (!pMobj) return;
    struct struct_36ee7a34 *u = unit_of(pMobj);
    for (int i = 0; i < u->nNumSprite; i++)
        if (u->apSprite[i]) DF_MOBJ_SetSpriteFlag(u->apSprite[i], (unsigned char)flag);
}

/* negative Idx passes the bound test (reads before apSprite) */
struct stMobj *UnitSprite_GetChildObject(struct stMobj *pMobj, int Idx)
{
    if (!pMobj) return 0;
    struct struct_36ee7a34 *u = unit_of(pMobj);
    if (Idx < u->nNumSprite) return u->apSprite[Idx];
    return 0;
}

/* no NULL test */
void UnitSprite_SetMultiply(struct stMobj *pMobj, struct struct_76b810ec *pMul)
{
    if (hook_unitsprite_skip_multiply && hook_unitsprite_skip_multiply()) return;   /* port hook */

    struct struct_36ee7a34 *u = unit_of(pMobj);
    unsigned char r = pMul->r, g = pMul->g, b = pMul->b, a = pMul->a;
    u->stMulColor.r = r;
    u->stMulColor.g = g;
    u->stMulColor.b = b;
    u->stMulColor.a = a;
}

/* draw_func of the title screen flash (installed by __MenuTitle_FlashInit) */
void __UnitSprite_DrawSclAng(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    if (!pMobj->f_draw) return;
    struct struct_36ee7a34 *u = unit_of(pMobj);
    for (int i = 0; i < u->nNumSprite; i++)
        if (u->apSprite[i])
            __MOBJ_DrawSpriteOffsetScaleAng(u->apSprite[i], &u->Mover.PosCur, &u->stMulColor, u->Scl, u->Ang);
}

/* draw_func of every unit sprite */
void __UnitSprite_Draw(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    if (!pMobj->f_draw) return;
    struct struct_36ee7a34 *u = unit_of(pMobj);
    for (int i = 0; i < u->nNumSprite; i++)
        if (u->apSprite[i]) DF_MOBJ_DrawSpriteOffset(u->apSprite[i], &u->Mover.PosCur, &u->stMulColor);
}

/* same body as __UnitSprite_Draw */
void UnitSprite_Draw(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    if (!pMobj->f_draw) return;
    struct struct_36ee7a34 *u = unit_of(pMobj);
    for (int i = 0; i < u->nNumSprite; i++)
        if (u->apSprite[i]) DF_MOBJ_DrawSpriteOffset(u->apSprite[i], &u->Mover.PosCur, &u->stMulColor);
}

/* play_func of every unit sprite (tail call; Mover_Update's result is left in v0, unused) */
void __UnitSprite_Play(struct stMobj *pMobj)
{
    Mover_Update(&unit_of(pMobj)->Mover);
}

void UnitSprite_SetMove(struct stMobj *pMobj, struct struct_214acd28 *pPos, int count)
{
    Mover_Move(&unit_of(pMobj)->Mover, pPos, count);
}

/* no NULL test of pMobj */
void UnitSprite_GetPosition(struct stMobj *pMobj, struct struct_214acd28 *pPos)
{
    if (!pPos) return;
    struct struct_36ee7a34 *u = unit_of(pMobj);
    pPos->x = u->Mover.PosCur.x;
    pPos->y = u->Mover.PosCur.y;
}

void UnitSprite_SetPosition(struct stMobj *pMobj, struct struct_214acd28 *pPos)
{
    Mover_SetPos(&unit_of(pMobj)->Mover, pPos);
}

/* children are passed on unchecked (NULL included), unlike the draw loops */
void UnitSprite_SetFadeOut(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    if (!pMobj) return;
    struct struct_36ee7a34 *u = unit_of(pMobj);
    for (int i = 0; i < u->nNumSprite; i++) {
        switch (u->DrawType) {
        case 0: DF_MOBJ_SetFadeOut(u->apSprite[i]); break;
        case 1: DF_MOBJ2_SetBlinkSpriteFadeOut(u->apSprite[i]); break;
        }
    }
}

/* no NULL test of pMobj (unlike SetFadeOut) */
void UnitSprite_SetFadeIn(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_36ee7a34 *u = unit_of(pMobj);
    for (int i = 0; i < u->nNumSprite; i++) {
        switch (u->DrawType) {
        case 0: DF_MOBJ_SetFadeIn(u->apSprite[i]); break;
        case 1: DF_MOBJ2_SetBlinkSpriteFadeIn(u->apSprite[i]); break;
        }
    }
}

/* clears all 32 slots (not nNumSprite, which is left as is) */
void UnitSprite_Delete(struct stMobj *pMobj)
{
    if (!pMobj) return;
    struct struct_36ee7a34 *u = unit_of(pMobj);
    for (int i = 0; i < 0x20; i++) {
        if (u->apSprite[i]) {
            u->apSprite[i]->f_draw = 0;
            u->apSprite[i]->f_erase = 1;
            u->apSprite[i] = 0;
        }
    }
    pMobj->f_draw = 0;
    pMobj->f_erase = 1;
}

struct stMobj *__UnitSprite_New(struct stMobj *pParent, struct _LO_HEADER *pHeader, char *partsname, int Kind)
{
    PS2_FRAME(0xc0);
    PS2_STACK(struct _LAYOUT_RET_DATA, lay, 0x70);   /* handed to interpreted DF_MOBJ2_NewBlinkSpriteFromLayout */

    struct stMobj *obj = DF_MOBJ_GetNewObject(pParent);
    if (!obj) return 0;
    struct struct_36ee7a34 *u = unit_of(obj);
    u->nNumSprite = DF_LO_GetPartsNumInUnitFromName((unsigned char *)pHeader, (unsigned char *)partsname);
    if (u->nNumSprite >= 0x20) u->nNumSprite = 0x20;
    u->stMulColor.a = 0x80;
    u->stMulColor.b = 0x80;
    u->stMulColor.g = 0x80;
    u->stMulColor.r = 0x80;
    u->DrawType = Kind;
    obj->play_func = (mobj_fn)GFN(__UnitSprite_Play);
    obj->draw_func = (mobj_fn)GFN(__UnitSprite_Draw);
    for (int i = 0; i < u->nNumSprite; i++) {
        if (!DF_LO_GetLayoutData((unsigned char *)pHeader, (unsigned char *)partsname, i, &lay)) continue;
        switch (u->DrawType) {
        case 0: u->apSprite[i] = DF_MOBJ_NewSpriteFromLayout(obj, &lay); break;
        case 1: u->apSprite[i] = DF_MOBJ2_NewBlinkSpriteFromLayout(obj, &lay, 0, 0); break;
        }
        /* a NULL slot here (allocation failure / other kind) makes the original store 0 at PS2 address 8: skipped */
        if (u->apSprite[i]) u->apSprite[i]->draw_func = 0;
    }
    return obj;
}

struct stMobj *UnitSpriteBlink_New(struct stMobj *pParent, struct _LO_HEADER *pHeader, char *partsname)
{
    return __UnitSprite_New(pParent, pHeader, partsname, 1);
}

struct stMobj *UnitSprite_New(struct stMobj *pParent, struct _LO_HEADER *pHeader, char *partsname)
{
    return __UnitSprite_New(pParent, pHeader, partsname, 0);
}
