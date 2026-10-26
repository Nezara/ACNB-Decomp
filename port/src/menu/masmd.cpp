/* src/menu/masmd.cpp - MASMD, the "detail window" of the assembly (garage) screen (OLMENU overlay): the big panel at
 * the right that lists, for the selected part, rows of "label ... value" (and, in tuning compare mode, "old : new"
 * pairs coloured blue / red). The window object's dt is a struct_04cfaec0 (ioDwp); ioDwp->cur / ->fade are stMobj whose
 * dt is a struct_02da87f8 (p_dsp: the row lists). Every per-kind SetDetail<kind> setter builds 0x14-byte entries
 * (struct_b9b64ffd) and feeds them to __MASMD_AddParam (row builder), after __MASMD_Begin reset the layout cursor.
 * The compare table (ioDwp->pCompare) is keyed by the translated string id (MASMD_TransParamStringID folds ids that
 * mean the same quantity onto one row); MASMD_IsReverseParam lists the "lower is better" parameters.
 * __MASMD_AddStringCopy / __MASMD_ChangeAll are never written by any code (always 0): every string row is type 0
 * (NewStringDirect), but both branches are kept.
 * Group 1 native: TransParamStringID, IsReverseParam, ParamCompare_GetParamColor / AddParam, Begin, AddParam,
 * SetDetailOptional, SetDetailHowitzerWepL / ShieldWepL / BladeWepL. Group 2: SetDetailWepL / WepR / NormalWepR /
 * Back / RadarBack / WepBack. Group 3: SetDetailExtension + the six *Ext leaves, OtherInside. Group 4: SetDetailInside /
 * WepInside / Radiator / Generator / FCS (+ __GetFcsDispInfo) / Booster / Leg, SetDetailWepArm / WepArmB. Group 5:
 * SetDetailArm / Core / Head and the window framework (New / Change / Delete / FadeOut / FadeIn, the play_funcs
 * PlayDetailWin / PlayDWSub; NewDetailWin has a hand bw__ wrapper: 9 args). Nothing of masmd.c stays interpreted. */
#include "acnb/nodwarf.h"
#include "acnb/ps2fp.h"
#include <string.h>
#include <stdio.h>

#ifdef ACNB_SOURCEPORT
int __MASMD_ChangeAll;
int __MASMD_AddStringCopy;
char masmd_empty_str[1] = "";
char masmd_unkonwn_str[8] = "unkonwn";
char masmd_param_fmt[65] = "<COLOR>%02x%02x%02x%5d<COLOR>%02x%02x%02x:<COLOR>%02x%02x%02x%5d";
#endif

typedef struct struct_02da87f8 DspList;   /* p_dsp: row lists of one content */
typedef struct struct_04cfaec0 DetailWin; /* ioDwp: the window (dt of the window object) */
typedef struct struct_b9b64ffd DetailEnt; /* one row entry */

/* s_aTransStringId (OLMENU .data 0x1ee4e60): four id pairs, the second id folds onto the first */
static const int masmd_trans_tbl[4][2] = {
    {0x18bb8, 0x18bbd}, {0x18cb7, 0x18bc0}, {0x18bf5, 0x18bf9}, {0x18b88, 0x18b7b}};

/* MASMD_IsReverseParam's str_id_data (rodata 0x1edb860), copied onto the stack by IsReverse / GetParamColor */
static const int masmd_reverse_tbl[52] = {
    0x18b65, 0x18bd8, 0x18b67, 0x18b69, 0x18b63, 0x18b72, 0x18bd5, 0x18c1e, 0x18be3, 0x18bee, 0x18b88, 0x18be5, 0x18be6,
    0x18be7, 0x18bf1, 0x18bf2, 0x18bf3, 0x18bf4, 0x18bf0, 0x18c09, 0x18c0a, 0x18c0b, 0x18c0c, 0x18c0d, 0x18c1e, 0x18c1f,
    0x18b7f, 0x18c1d, 0x18bc2, 0x18bec, 0x18c0f, 0x18ba9, 0x18bb6, 0x18bb7, 0x18bbf, 0x18bc0, 0x18be0, 0x18bed, 0x18bee,
    0x18bf1, 0x18bf2, 0x18c0f, 0x18c20, 0x18ca5, 0x18c87, 0x18bd7, 0x18c98, 0x18b7e, 0x18cb7, 0x18b7b, 0x18b97, 0x18bd4};

/* main ELF sdata colours (0x27b4b0..0x27b4c8), bytes r, g, b, a */
static inline void masmd_col(struct struct_76b810ec &c, unsigned int w)
{
    c.r = (unsigned char)w;
    c.g = (unsigned char)(w >> 8);
    c.b = (unsigned char)(w >> 16);
    c.a = (unsigned char)(w >> 24);
}
#define MASMD_COL_DEFAULT 0x8092928cu
#define MASMD_COL_LABEL 0x80909090u
#define MASMD_COL_UP 0x80f08040u
#define MASMD_COL_DOWN 0x804646fcu

/* the translation loop (inlined three times in the original) */
static inline int masmd_trans(int strid)
{
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 2; j++)
            if (masmd_trans_tbl[i][j] == strid) return masmd_trans_tbl[i][0];
    return strid;
}

static inline DspList *masmd_dsp(struct stMobj *o) { return (DspList *)&o->dt; }
static inline DetailWin *masmd_dwp(struct stMobj *o) { return (DetailWin *)&o->dt; }

/* inline delete of a sub-object: f_draw = 0, f_erase = 1 */
static inline void masmd_erase(struct stMobj *p)
{
    p->f_draw = 0;
    p->f_erase = 1;
}

static inline char *masmd_name(unsigned int id) { return (char *)DF_MSTR_GetName(MenuMng.language, 0, id); }

int MASMD_TransParamStringID(int strid)
{
    return masmd_trans(strid);
}

int MASMD_IsReverseParam(int strid)
{
    PS2_FRAME(0xd0);
    PS2_STACK(int[52], tbl, 0);
    memcpy(tbl, masmd_reverse_tbl, sizeof(masmd_reverse_tbl));
    for (int i = 0; i < 52; i++)
        if (strid == tbl[i]) return 1;
    return 0;
}

/* compare colours of one row against the recorded table: col1 / col2 = default, Up (blue) when the new value is
 * above the recorded one, Down (red) below; swapped for "lower is better" parameters. 0 = no recorded entry. */
static int __ParamCompare_GetParamColor(struct struct_35af9d5b *pCompare, int strid, int value1, int value2,
                                        struct struct_76b810ec *col1, struct struct_76b810ec *col2)
{
    PS2_FRAME(0xe0);
    PS2_STACK(struct struct_76b810ec, Up, 0xdc);
    PS2_STACK(struct struct_76b810ec, Down, 0xd8);
    PS2_STACK(struct struct_76b810ec, t, 0xd4);
    strid = masmd_trans(strid);
    int key = masmd_trans(strid);
    struct struct_002cdb75 *e = 0;
    for (int i = 0; i < pCompare->nNumEntry; i++)
        if (pCompare->aEntry[i].nStrId == key) {
            e = &pCompare->aEntry[i];
            break;
        }
    masmd_col(Up, MASMD_COL_UP);
    masmd_col(Down, MASMD_COL_DOWN);
    masmd_col(*col1, MASMD_COL_DEFAULT);
    *col2 = *col1;
    if (!e) return 0;
    {
        PS2_STACK(int[52], tbl, 0);
        memcpy(tbl, masmd_reverse_tbl, sizeof(masmd_reverse_tbl));
        int rev = 0;
        for (int i = 0; i < 52; i++)
            if (strid == tbl[i]) {
                rev = 1;
                break;
            }
        if (rev) {
            t = Up;
            Up = Down;
            Down = t;
        }
    }
    if (e->nValue1 < value1) *col1 = Up;
    else if (value1 < e->nValue1) *col1 = Down;
    if (e->nValue2 < value2) *col2 = Up;
    else if (value2 < e->nValue2) *col2 = Down;
    return 1;
}

/* record one row in the compare table (overwrite an existing key, else append up to 20 entries) */
static void __ParamCompare_AddParam(struct struct_35af9d5b *pCompare, int strid, int value1, int value2)
{
    int key = masmd_trans(strid);
    struct struct_002cdb75 *e = 0;
    int n = pCompare->nNumEntry;
    for (int i = 0; i < n; i++)
        if (pCompare->aEntry[i].nStrId == key) {
            e = &pCompare->aEntry[i];
            break;
        }
    strid = masmd_trans(strid);
    if (e) {
        e->nValue1 = value1;
        e->nValue2 = value2;
    } else if (n < 20) {
        struct struct_002cdb75 *d = &pCompare->aEntry[n];
        d->nStrId = strid;
        d->nValue1 = value1;
        d->nValue2 = value2;
        pCompare->nNumEntry++;
    }
}

/* start a row list: cursor = the window rect, old value objects retired (label rows too unless bParamOnly) */
void __MASMD_Begin(struct struct_02da87f8 *p_dsp, int bParamOnly)
{
    struct stMobj *parent = p_dsp->pParent;
    p_dsp->f_prmonly = (char)bParamOnly;
    p_dsp->nNumParam = 0;
    p_dsp->x = masmd_dwp(parent)->rect.x;
    p_dsp->y = masmd_dwp(parent)->rect.y;
    p_dsp->w = masmd_dwp(parent)->rect.w;
    for (int i = 0; i < 20; i++) {
        if (!p_dsp->f_prmonly && p_dsp->itemp[i]) {
            masmd_erase(p_dsp->itemp[i]);
            p_dsp->itemp[i] = 0;
        }
        if (p_dsp->prm_objp[i]) {
            masmd_erase(p_dsp->prm_objp[i]);
            p_dsp->prm_objp[i] = 0;
        }
    }
}

/* one row: label item (unless f_prmonly), compare-table bookkeeping by ioDwp->nMode, then the value object */
static void __MASMD_AddParam(struct struct_02da87f8 *p_dsp, struct struct_b9b64ffd *pentry)
{
    PS2_FRAME(0x1f0);
    PS2_STACK(unsigned long long[6], sargs, 0);
    PS2_STACK(char[256], buff, 0xd0);
    PS2_STACK(struct stMenuRect, rect, 0x1d0);
    PS2_STACK(struct struct_76b810ec, colnochange, 0x1ec);
    PS2_STACK(struct struct_76b810ec, col1, 0x1e8);
    PS2_STACK(struct struct_76b810ec, col2, 0x1e4);
    int index = p_dsp->nNumParam;
    DetailWin *ioDwp = masmd_dwp(p_dsp->pParent);
    if (!p_dsp->f_prmonly) {
        rect.h = 0x12;
        rect.x = p_dsp->x + p_dsp->nIndent * 9;
        rect.y = p_dsp->y;
        rect.w = p_dsp->w - p_dsp->nIndent * 9;
        DF_MOBJ2_DeleteItem(p_dsp->itemp[index]);
        p_dsp->itemp[index] = 0;
        sargs[0] = 0;
        sargs[1] = 0;
        p_dsp->itemp[index] = DF_MOBJ2_NewItem(p_dsp->pParent, &rect, 0, 4, 7, pentry->strid, 0, 0, 0, 0);
        struct _mhelp_param_st *hpp =
            DF_MHELP_NewParam(0, pentry->strid, *(struct stMobj **)((char *)p_dsp->itemp[index] + 0x30));
        DF_MHELP_AddParamToList(ioDwp->hlist, hpp);
    }
    int x = p_dsp->x + p_dsp->w;
    int y = p_dsp->y;
    int param1 = pentry->param1;
    int param2 = pentry->param2;
    masmd_col(colnochange, MASMD_COL_DEFAULT);
    masmd_col(col1, MASMD_COL_DEFAULT);
    masmd_col(col2, MASMD_COL_DEFAULT);
    int flag = pentry->flag & ~0xff;
    unsigned int type = (unsigned char)pentry->type;
    if (type == 2 && (flag & 0x100) && param1 == 0) {
        type = 0;
        param1 = (int)PS2PTR(masmd_name(0x186d2));
    }
    if (type == 2) param2 = param1;
    if (ioDwp->nMode == 0 || ioDwp->nMode == 1) {
        if (type >= 2) __ParamCompare_AddParam(ioDwp->pCompare, pentry->strid, param1, param2);
        if (ioDwp->nMode == 1) goto end;
    } else if (ioDwp->nMode == 2) {
        if (type >= 2)
            __ParamCompare_GetParamColor(ioDwp->pCompare, pentry->strid, pentry->param1, pentry->param2, &col1, &col2);
    }
    switch (type) {
    case 4: {
        buff[255] = 0;
        sargs[0] = colnochange.g;
        sargs[1] = colnochange.b;
        sargs[2] = col2.r;
        sargs[3] = col2.g;
        sargs[4] = col2.b;
        sargs[5] = (unsigned long long)(long long)param2;
#ifdef ACNB_SOURCEPORT
        snprintf(buff, 0xff, masmd_param_fmt, col1.r, col1.g, col1.b, param1, colnochange.r, colnochange.g,
                 colnochange.b, col2.r, col2.g, col2.b, param2);
#else
        /* args 9..14 are the stack slots sargs (the caller's sp+0..0x28, EABI 8-byte slots) */
        gcall_as<int(char *, unsigned int, char *, int, int, int, int, int)>(GFN(Pf_SnPrintf), buff, 0xffu,
                                                                            masmd_param_fmt, (int)col1.r, (int)col1.g,
                                                                            (int)col1.b, param1, (int)colnochange.r);
#endif
        p_dsp->prm_objp[index] = DF_MOBJ_NewStringBase(p_dsp->pParent, 7, x - 0x58, y, &colnochange, 0, 0);
        DF_MOBJ_SetStringCopy(p_dsp->prm_objp[index], buff);
        break;
    }
    case 1:
        p_dsp->prm_objp[index] = DF_MOBJ_NewStringBase(p_dsp->pParent, 7, x, y, &colnochange, 0, 0);
        DF_MOBJ_SetStringCopy(p_dsp->prm_objp[index], (char *)(uintptr_t)(unsigned int)param1);
        DF_MOBJ_SetStringXFromRX(p_dsp->prm_objp[index], x);
        break;
    case 3:
        DF_MOBJ2_ChangeItemParam(p_dsp->itemp[index], param1, param2);
        break;
    case 2:
        sargs[0] = 0;
        p_dsp->prm_objp[index] = DF_MOBJ_NewNumber(p_dsp->pParent, param1, 5, 0, 7, x - 0x28, y, &col1, 0);
        break;
    case 0:
        p_dsp->prm_objp[index] = DF_MOBJ_NewStringDirect(p_dsp->pParent, (char *)(uintptr_t)(unsigned int)param1, 7,
                                                         x, y, &colnochange, 0, 0);
        DF_MOBJ_SetStringXFromRX(p_dsp->prm_objp[index], x);
        break;
    default:
        break;
    }
end:
    p_dsp->y += 0x12;
    p_dsp->nNumParam++;
}

/* entry builders of the setters (the original zero-fills / copies a rodata template into a stack temporary) */
static void masmd_add_str(DspList *p_dsp, int strid, char *s)
{
    DetailEnt e;
    memset(&e, 0, sizeof(e));
    e.type = __MASMD_AddStringCopy ? 1 : 0;
    e.strid = strid;
    e.param1 = (int)PS2PTR(s);
    __MASMD_AddParam(p_dsp, &e);
}

static void masmd_add_num(DspList *p_dsp, int strid, int v)
{
    DetailEnt e;
    memset(&e, 0, sizeof(e));
    e.type = 2;
    e.strid = strid;
    e.param1 = v;
    __MASMD_AddParam(p_dsp, &e);
}

static void masmd_add_pair(DspList *p_dsp, int strid, int v1, int v2)
{
    DetailEnt e;
    memset(&e, 0, sizeof(e));
    e.type = 4;
    e.strid = strid;
    e.param1 = v1;
    e.param2 = v2;
    __MASMD_AddParam(p_dsp, &e);
}

/* kind 13: two label rows (own NewItem calls, no Begin / AddParam), maker name and "intensify" text or slot count */
static void __MASMD_SetDetailOptional(struct struct_04cfaec0 *ioDwp)
{
    PS2_FRAME(0xa0);
    PS2_STACK(int[2], str_id, 0x90);
    PS2_STACK(struct struct_76b810ec, col, 0x9c);
    PS2_STACK(unsigned long long[2], sargs, 0);
    str_id[0] = 0x18c7d;
    str_id[1] = 0x18b73;
    masmd_col(col, MASMD_COL_LABEL);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    if (!ioDwp->f_prmonly) {
        PS2_STACK(struct stMenuRect, rect, 0x80);
        rect = ioDwp->rect;
        rect.x += 4;
        rect.y += 0x12;
        rect.w -= 0x1c;
        struct stMobj **itemp = masmd_dsp(ioDwp->cur)->itemp;
        for (int i = 0; i < 2; i++) {
            sargs[0] = 0;
            sargs[1] = 0;
            itemp[i] = DF_MOBJ2_NewItem(ioDwp->pMobj, &rect, 0, 4, 7, str_id[i], 0, 0, 0, 0);
            DF_MHELP_AddParamToList(ioDwp->hlist,
                                    DF_MHELP_NewParam(0, str_id[i], *(struct stMobj **)((char *)itemp[i] + 0x30)));
            rect.y += 0x12;
        }
    }
    struct PartsParamOptionSt *prmp = (struct PartsParamOptionSt *)ioDwp->datap;
    if (!prmp) return;
    int y = ioDwp->rect.y + 0x12;
    int xr = ioDwp->rect.x + ioDwp->rect.w - 0x12;
    char *name = masmd_name(0x18c5f + (signed char)prmp->basic.maker);
    p_dsp->prm_objp[0] = DF_MOBJ_NewStringDirect(ioDwp->pMobj, name, 7, xr, y, &col, 0, 0);
    DF_MOBJ_SetStringXFromRX(p_dsp->prm_objp[0], xr);
    if (strcmp((char *)prmp, "OP-INTENSIFY") == 0) {
        char *name2 = masmd_name(0x186d8);
        p_dsp->prm_objp[1] = DF_MOBJ_NewStringDirect(ioDwp->pMobj, name2, 7, xr, y + 0x12, &col, 0, 0);
        DF_MOBJ_SetStringXFromRX(p_dsp->prm_objp[1], xr);
    } else {
        sargs[0] = 0;
        p_dsp->prm_objp[1] = DF_MOBJ_NewNumber(ioDwp->pMobj, prmp->slot, 5, 0, 7, xr - 0x28, y + 0x12, &col, 0);
    }
}

/* left-hand howitzer: header, sight, damage : ammo, heat, range, lock count, reload, price, burst rows
 * (wepp is not NULL-checked, as in the original) */
static void __MASMD_SetDetailHowitzerWepL(struct struct_04cfaec0 *ioDwp, int inFlagEne)
{
    PS2_FRAME(0x280);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    if (!ioDwp->datap) return;
    struct PartsParamWeaponSt *wep = (struct PartsParamWeaponSt *)ioDwp->wepp;
    int reload1, reload2 = 0;
    if (wep->burst_shotcnt == 0) {
        reload1 = wep->reload_time;
    } else {
        reload1 = wep->breload_time;
        reload2 = wep->reload_time;
    }
    masmd_add_str(p_dsp, 0x18b81, masmd_name(inFlagEne ? 0x18c53 : 0x18c52));
    p_dsp->nIndent++;
    if (wep->lkmax == 0) {
        masmd_add_str(p_dsp, 0x18bc6, masmd_name(0x186d2));
    } else {
        char *s;
        switch (wep->sight_type) {
        case 3: s = masmd_name(0x18c69); break;
        case 2: s = masmd_name(0x18c68); break;
        case 1: s = masmd_name(0x18c67); break;
        case 0: s = masmd_name(0x18c6c); break;
        default: s = masmd_unkonwn_str; break;
        }
        masmd_add_str(p_dsp, 0x18bc6, s);
    }
    masmd_add_pair(p_dsp, 0x18b85, wep->dstat.damage, wep->blt_sta);
    masmd_add_num(p_dsp, 0x18bf8, wep->dstat.heat);
    masmd_add_num(p_dsp, 0x18b5c, ps2_ftoi(wep->act.range));
    if (wep->lkmax != 0) masmd_add_num(p_dsp, 0x18bc5, wep->lkmax);
    else masmd_add_str(p_dsp, 0x18bc5, masmd_name(0x186d2));
    masmd_add_num(p_dsp, 0x18bc2, reload1);
    if (!inFlagEne) masmd_add_num(p_dsp, 0x18ca5, wep->blt_price);
    else masmd_add_num(p_dsp, 0x18b72, wep->blt_price);
    if (wep->burst_shotcnt != 0) {
        masmd_add_num(p_dsp, 0x18c10, wep->burst_shotcnt);
        masmd_add_num(p_dsp, 0x18c0f, reload2);
    }
    p_dsp->nIndent--;
}

/* left-hand shield: defence rows and the boost cost ("---" when 0) */
static void __MASMD_SetDetailShieldWepL(struct struct_04cfaec0 *ioDwp)
{
    PS2_FRAME(0x150);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    struct PartsParamSwordSt *prmp = (struct PartsParamSwordSt *)ioDwp->datap;
    if (!prmp) return;
    masmd_add_str(p_dsp, 0x18b8e, masmd_empty_str);
    p_dsp->nIndent++;
    masmd_add_num(p_dsp, 0x18bcd, prmp->def_shell);
    masmd_add_num(p_dsp, 0x18caf, prmp->def_energy);
    masmd_add_num(p_dsp, 0x18c15, prmp->def_angx);
    masmd_add_num(p_dsp, 0x18be8, prmp->def_heat);
    int sub = prmp->boost_sub;
    if (sub > 0) masmd_add_num(p_dsp, 0x18bf2, sub);
    else masmd_add_str(p_dsp, 0x18bf2, masmd_name(0x186d2));
    p_dsp->nIndent--;
}

/* left-hand blade: laser blade rows from the sword record, W-blade (type 1) rows from the weapon record; the two
 * id sets are the translation pairs (same compare rows). wepp->ep is read unconditionally. */
static void __MASMD_SetDetailBladeWepL(struct struct_04cfaec0 *ioDwp)
{
    PS2_FRAME(0x1a0);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    struct PartsParamSwordSt *prmp = (struct PartsParamSwordSt *)ioDwp->datap;
    if (!prmp) return;
    struct PartsParamWeaponSt *wep = (struct PartsParamWeaponSt *)ioDwp->wepp;
    int type = prmp->type;
    /* s0: read before the type test, NULL wepp reads PS2 address 0x48 */
    int ep0 = wep ? wep->ep : ((const struct PartsParamWeaponSt *)PS2_NULL_MEM())->ep;
    int ep1 = prmp->boost_sub;  /* s1 */
    if (type == 1) {
        ep0 = ep1;
        ep1 = 0;
    }
    if (type != 1) {
        masmd_add_num(p_dsp, 0x18bb8, prmp->blade_dstat.damage);
        masmd_add_num(p_dsp, 0x18cb7, prmp->blade_heat);
        masmd_add_num(p_dsp, 0x18bf5, prmp->blade_dstat.heat);
        masmd_add_num(p_dsp, 0x18c07, ps2_ftoi(prmp->blade_range));
        masmd_add_num(p_dsp, 0x18b88, ep1);
    } else {
        masmd_add_num(p_dsp, 0x18bbd, wep->dstat.damage);
        masmd_add_num(p_dsp, 0x18bc0, wep->heat);
        masmd_add_num(p_dsp, 0x18bf9, wep->dstat.heat);
        masmd_add_num(p_dsp, 0x18bbe, ps2_ftoi(wep->act.range));
        masmd_add_num(p_dsp, 0x18b7b, ep0);
    }
}

/* kind 12: left-hand weapon: name, weight, energy rows, then by sub_type % 10: 3 howitzer, 2 shield, 1 / 0 blade */
static void __MASMD_SetDetailWepL(struct struct_04cfaec0 *ioDwp)
{
    PS2_FRAME(0xc0);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    struct PartsParamBasicSt *datap = (struct PartsParamBasicSt *)ioDwp->datap;
    if (!datap) return;
    __MASMD_Begin(p_dsp, ioDwp->f_prmonly);
    masmd_add_str(p_dsp, 0x18bb4, (char *)datap + 0x12);  /* part name inside the record (PS2 address) */
    masmd_add_num(p_dsp, 0x18bd5, datap->wp);
    masmd_add_num(p_dsp, 0x18bd8, datap->ep);
    int sub = ioDwp->sub_type;
    int k = sub % 10;
    if (k == 3) __MASMD_SetDetailHowitzerWepL(ioDwp, (sub % 100) / 10);
    else if (k == 2) __MASMD_SetDetailShieldWepL(ioDwp);
    else if (k == 1 || k == 0) __MASMD_SetDetailBladeWepL(ioDwp);
}

/* right-hand gun / back weapon body (NormalWepR and WepBack differ only in the heat-row id): header, sight, damage :
 * ammo, heat, range, lock count, reload, price, burst rows (wepp is not NULL-checked, as in the original) */
static void masmd_wep_rows(struct struct_04cfaec0 *ioDwp, int inFlagEne, int heat_id)
{
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    int reload2 = 0;
    if (!ioDwp->datap) return;
    struct PartsParamWeaponSt *wep = (struct PartsParamWeaponSt *)ioDwp->wepp;
    int reload1;
    if (wep->burst_shotcnt == 0) {
        reload1 = wep->reload_time;
    } else {
        reload1 = wep->breload_time;
        reload2 = wep->reload_time;
    }
    masmd_add_str(p_dsp, 0x18b81, masmd_name(inFlagEne ? 0x18c53 : 0x18c52));
    p_dsp->nIndent++;
    if (wep->lkmax == 0) {
        masmd_add_str(p_dsp, 0x18bc6, masmd_name(0x186d2));
    } else {
        char *s;
        switch (wep->sight_type) {
        case 3: s = masmd_name(0x18c69); break;
        case 2: s = masmd_name(0x18c68); break;
        case 1: s = masmd_name(0x18c67); break;
        case 0: s = masmd_name(0x18c6c); break;
        default: s = masmd_unkonwn_str; break;
        }
        masmd_add_str(p_dsp, 0x18bc6, s);
    }
    masmd_add_pair(p_dsp, 0x18b85, wep->dstat.damage, wep->blt_sta);
    masmd_add_num(p_dsp, heat_id, wep->dstat.heat);
    masmd_add_num(p_dsp, 0x18b5c, ps2_ftoi(wep->act.range));
    if (wep->lkmax != 0) masmd_add_num(p_dsp, 0x18b68, wep->lkmax);
    else masmd_add_str(p_dsp, 0x18b68, masmd_name(0x186d2));
    masmd_add_num(p_dsp, 0x18bc2, reload1);
    if (!inFlagEne) masmd_add_num(p_dsp, 0x18ca5, wep->blt_price);
    else masmd_add_num(p_dsp, 0x18b72, wep->blt_price);
    if (wep->burst_shotcnt != 0) {
        masmd_add_num(p_dsp, 0x18c10, wep->burst_shotcnt);
        masmd_add_num(p_dsp, 0x18c0f, reload2);
    }
    p_dsp->nIndent--;
}

/* right-hand gun rows (Begin was done by WepR) */
static void __MASMD_SetDetailNormalWepR(struct struct_04cfaec0 *ioDwp, int inFlagEne)
{
    PS2_FRAME(0x280);
    masmd_wep_rows(ioDwp, inFlagEne, 0x18bf8);
}

/* kind 11: right-hand weapon: name, weight, energy rows, then k 0 = gun (NormalWepR), k 1 = inline rows that mix the
 * weapon record (damage, ammo, heat, price) with the part record (0x10a, range float 0x10c; wepp unchecked) */
static void __MASMD_SetDetailWepR(struct struct_04cfaec0 *ioDwp)
{
    PS2_FRAME(0x190);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    struct PartsParamBasicSt *datap = (struct PartsParamBasicSt *)ioDwp->datap;
    if (!datap) return;
    __MASMD_Begin(p_dsp, ioDwp->f_prmonly);
    masmd_add_str(p_dsp, 0x18bb3, (char *)datap + 0x12);
    masmd_add_num(p_dsp, 0x18bd5, datap->wp);
    masmd_add_num(p_dsp, 0x18bd8, datap->ep);
    int sub = ioDwp->sub_type;
    int k = sub % 10;
    if (k == 1) {
        DspList *p = masmd_dsp(ioDwp->cur);
        struct PartsParamGunSt *gun = (struct PartsParamGunSt *)ioDwp->datap;
        if (!gun) return;
        struct PartsParamWeaponSt *wep = (struct PartsParamWeaponSt *)ioDwp->wepp;
        masmd_add_num(p, 0x18bba, wep->dstat.damage);
        masmd_add_num(p, 0x18bb5, wep->blt_sta);
        masmd_add_num(p, 0x18bb6, gun->blade_heat);
        masmd_add_num(p, 0x18bf6, wep->dstat.heat);
        masmd_add_num(p, 0x18c08, ps2_ftoi(gun->blade_range));
        masmd_add_num(p, 0x18ca5, wep->blt_price);
    } else if (k == 0) {
        __MASMD_SetDetailNormalWepR(ioDwp, (sub % 100) / 10);
    }
}

/* back radar: eccm header row, empty-string title, then range, type, interval and two stat-bit rows (indented) */
static void __MASMD_SetDetailRadarBack(struct struct_04cfaec0 *ioDwp)
{
    PS2_FRAME(0x190);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    struct PartsParamBackSt *prmp = (struct PartsParamBackSt *)ioDwp->datap;
    if (!prmp) return;
    masmd_add_num(p_dsp, 0x18be2, prmp->radar_stat.eccm_power);
    masmd_add_str(p_dsp, 0x18c1b, masmd_empty_str);
    p_dsp->nIndent++;
    masmd_add_num(p_dsp, 0x18c19, ps2_ftoi(prmp->radar_stat.rd_range));
    masmd_add_str(p_dsp, 0x18c1a, masmd_name(0x18c5b + prmp->radar_stat.type));
    masmd_add_num(p_dsp, 0x18c87, prmp->radar_stat.rd_intvl);
    masmd_add_str(p_dsp, 0x18cb4, masmd_name(0x18c4d + ((prmp->radar_stat.stat & 1) != 0)));
    masmd_add_str(p_dsp, 0x18cbe, masmd_name(0x18c4d + ((prmp->radar_stat.stat & 4) != 0)));
    p_dsp->nIndent--;
}

/* back weapon rows (Begin was done by Back); NormalWepR's twin with heat-row id 0x18ca3 */
static void __MASMD_SetDetailWepBack(struct struct_04cfaec0 *ioDwp, int inFlagEne)
{
    PS2_FRAME(0x280);
    masmd_wep_rows(ioDwp, inFlagEne, 0x18ca3);
}

/* kind 10: back unit: name, weight, energy rows, then k 5 = booster rows, 4 / 3 = magazine rows, 1 = radar,
 * 0 = weapon (2, 6..9: nothing) */
static void __MASMD_SetDetailBack(struct struct_04cfaec0 *ioDwp)
{
    PS2_FRAME(0x180);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    struct PartsParamBasicSt *datap = (struct PartsParamBasicSt *)ioDwp->datap;
    if (!datap) return;
    __MASMD_Begin(p_dsp, ioDwp->f_prmonly);
    masmd_add_str(p_dsp, 0x18bb2, (char *)datap + 0x12);
    masmd_add_num(p_dsp, 0x18bd5, datap->wp);
    masmd_add_num(p_dsp, 0x18bd8, datap->ep);
    int sub = ioDwp->sub_type;
    int k = sub % 10;
    if (k == 5) {
        DspList *p = masmd_dsp(ioDwp->cur);
        struct PartsParamBackSt *prmp = (struct PartsParamBackSt *)ioDwp->datap;
        if (!prmp) return;
        masmd_add_num(p, 0x18c00, prmp->boost_add);  /* signed (lh) */
        masmd_add_num(p, 0x18c1e, prmp->boost_ep);
        masmd_add_num(p, 0x18c1f, prmp->boost_heat);
    } else if (k == 4 || k == 3) {
        DspList *p = masmd_dsp(ioDwp->cur);
        struct PartsParamBackSt *prmp = (struct PartsParamBackSt *)ioDwp->datap;
        if (!prmp) return;
        masmd_add_str(p, 0x18b9b, masmd_name(prmp->magazine_add_mgun == 0 ? 0x18c72 : 0x18c73));
        masmd_add_num(p, 0x18b76, prmp->magazine_add_mgun + prmp->magazine_add_rifle);
    } else if (k == 1) {
        __MASMD_SetDetailRadarBack(ioDwp);
    } else if (k == 0) {
        __MASMD_SetDetailWepBack(ioDwp, (sub % 100) / 10);
    }
}

/* ---- group 3: extension and inside-part setters ---- */

/* extension kind 12 (energy): empty-string title, then charge (power * busy time), busy time, uses, reload, heat rows */
static void __MASMD_SetDetailEnergyExt(struct struct_04cfaec0 *ioDwp)
{
    PS2_FRAME(0x110);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    struct PartsParamExtensionSt *prmp = (struct PartsParamExtensionSt *)ioDwp->datap;
    if (!prmp) return;
    masmd_add_str(p_dsp, 0x18be9, masmd_empty_str);
    p_dsp->nIndent++;
    masmd_add_num(p_dsp, 0x18bd3, (int)((unsigned int)prmp->power * (unsigned int)prmp->boost_busytime));
    masmd_add_num(p_dsp, 0x18bd4, prmp->boost_busytime);
    masmd_add_num(p_dsp, 0x18bef, prmp->use_cnt);
    masmd_add_num(p_dsp, 0x18bf0, prmp->reload_time);
    masmd_add_num(p_dsp, 0x18b97, prmp->boost_heat);
    p_dsp->nIndent--;
}

/* extension kind 11 (cooler): the 0x18b94 row is a literal 0 */
static void __MASMD_SetDetailCoolerExt(struct struct_04cfaec0 *ioDwp)
{
    PS2_FRAME(0x110);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    struct PartsParamExtensionSt *prmp = (struct PartsParamExtensionSt *)ioDwp->datap;
    if (!prmp) return;
    masmd_add_str(p_dsp, 0x18b93, masmd_empty_str);
    p_dsp->nIndent++;
    masmd_add_num(p_dsp, 0x18b78, prmp->power);
    masmd_add_num(p_dsp, 0x18b94, 0);
    masmd_add_num(p_dsp, 0x18bec, prmp->boost_busytime);
    masmd_add_num(p_dsp, 0x18b91, prmp->boost_acctime);
    masmd_add_num(p_dsp, 0x18b92, prmp->boost_sub);
    p_dsp->nIndent--;
}

/* extension kind 10 (stealth) */
static void __MASMD_SetDetailStealthExt(struct struct_04cfaec0 *ioDwp)
{
    PS2_FRAME(0x110);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    struct PartsParamExtensionSt *prmp = (struct PartsParamExtensionSt *)ioDwp->datap;
    if (!prmp) return;
    masmd_add_str(p_dsp, 0x18b8f, masmd_empty_str);
    p_dsp->nIndent++;
    masmd_add_num(p_dsp, 0x18b82, prmp->ecm_power);
    masmd_add_num(p_dsp, 0x18cb0, prmp->power);
    masmd_add_num(p_dsp, 0x18bec, prmp->boost_busytime);
    masmd_add_num(p_dsp, 0x18b91, prmp->boost_acctime);
    masmd_add_num(p_dsp, 0x18b92, prmp->boost_sub);
    p_dsp->nIndent--;
}

/* extension kinds 6 / 9 (side shield): defence rows, heat defence and boost cost ("---" when 0) */
static void __MASMD_SetDetailSideShieldExt(struct struct_04cfaec0 *ioDwp)
{
    PS2_FRAME(0x190);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    struct PartsParamExtensionSt *prmp = (struct PartsParamExtensionSt *)ioDwp->datap;
    if (!prmp) return;
    masmd_add_str(p_dsp, 0x18b8e, masmd_empty_str);
    p_dsp->nIndent++;
    masmd_add_num(p_dsp, 0x18bcd, prmp->def_shell);
    masmd_add_num(p_dsp, 0x18caf, prmp->def_energy);
    masmd_add_num(p_dsp, 0x18c15, prmp->def_angx);  /* signed (lh) */
    int heat = prmp->def_heat;
    if (heat > 0) masmd_add_num(p_dsp, 0x18be8, heat);
    else masmd_add_str(p_dsp, 0x18be8, masmd_name(0x186d2));
    int sub = prmp->boost_sub;
    if (sub > 0) masmd_add_num(p_dsp, 0x18bf2, sub);
    else masmd_add_str(p_dsp, 0x18bf2, masmd_name(0x186d2));
    p_dsp->nIndent--;
}

/* extension kinds 2 / 15 (linked missile): weapon-record rows (wepp is not NULL-checked, as in the original) */
static void __MASMD_SetDetailLinkedMisExt(struct struct_04cfaec0 *ioDwp, int inFlagEne)
{
    PS2_FRAME(0x160);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    if (!ioDwp->datap) return;
    struct PartsParamWeaponSt *wep = (struct PartsParamWeaponSt *)ioDwp->wepp;
    masmd_add_str(p_dsp, 0x18b81, masmd_name(inFlagEne ? 0x18c53 : 0x18c52));
    p_dsp->nIndent++;
    masmd_add_pair(p_dsp, 0x18b85, wep->dstat.damage, wep->blt_sta);
    masmd_add_num(p_dsp, 0x18bf8, wep->dstat.heat);
    masmd_add_num(p_dsp, 0x18b5c, ps2_ftoi(wep->act.range));
    masmd_add_num(p_dsp, 0x18bc3, wep->shotcnt);
    masmd_add_num(p_dsp, 0x18bc2, wep->reload_time);
    if (!inFlagEne) masmd_add_num(p_dsp, 0x18ca5, wep->blt_price);
    else masmd_add_num(p_dsp, 0x18b72, wep->blt_price);
    p_dsp->nIndent--;
}

/* extension kinds 3 / 4 (VS missile): mixes the extension record (datap) and the weapon record (wepp, unchecked
 * after the first GetName, whose result the original discards) */
static void __MASMD_SetDetailVsMisExt(struct struct_04cfaec0 *ioDwp)
{
    PS2_FRAME(0x150);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    struct PartsParamExtensionSt *prmp = (struct PartsParamExtensionSt *)ioDwp->datap;
    if (!prmp) return;
    struct PartsParamWeaponSt *wep = (struct PartsParamWeaponSt *)ioDwp->wepp;
    masmd_name((wep && (wep->dstat.effect & 0x20)) ? 0x18c53 : 0x18c52);  /* dead */
    int ene = prmp->prog_no == 4;  /* lbu 0x100 */
    masmd_add_str(p_dsp, 0x18baf, masmd_name(ene ? 0x18c53 : 0x18c52));
    p_dsp->nIndent++;
    masmd_add_num(p_dsp, 0x18b98, prmp->vs_mgun_sr);
    masmd_add_num(p_dsp, 0x18bde, wep->blt_sta);
    masmd_add_num(p_dsp, 0x18bd0, prmp->vs_mgun_range);  /* signed (lh) */
    masmd_add_num(p_dsp, 0x18bec, wep->reload_time);
    if (!ene) masmd_add_num(p_dsp, 0x18ca5, wep->blt_price);
    else masmd_add_num(p_dsp, 0x18b72, wep->blt_price);
    p_dsp->nIndent--;
}

/* kind 9: extension: name, weight, energy rows, then by the raw sub_type (no % 10; jump table 0x1ee4e80):
 * 0 / 8 inline rows A, 2 / 15 linked missile, 3 / 4 VS missile, 6 / 9 side shield, 10 stealth, 11 cooler,
 * 12 energy, 14 inline rows B, 16 inline rows C, others nothing. The inline bodies use this function's frame. */
static void __MASMD_SetDetailExtension(struct struct_04cfaec0 *ioDwp)
{
    PS2_FRAME(0x240);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    struct PartsParamBasicSt *datap = (struct PartsParamBasicSt *)ioDwp->datap;
    if (!datap) return;
    __MASMD_Begin(p_dsp, ioDwp->f_prmonly);
    masmd_add_str(p_dsp, 0x18bb1, (char *)datap + 0x12);  /* part name inside the record (PS2 address) */
    masmd_add_num(p_dsp, 0x18bd5, datap->wp);
    masmd_add_num(p_dsp, 0x18bd8, datap->ep);
    int sub = ioDwp->sub_type;
    switch (sub) {
    case 0:
    case 8: {
        DspList *p = masmd_dsp(ioDwp->cur);
        struct PartsParamExtensionSt *prmp = (struct PartsParamExtensionSt *)ioDwp->datap;
        if (!prmp) return;
        masmd_add_str(p, 0x18b8b, masmd_empty_str);
        p->nIndent++;
        masmd_add_num(p, 0x18bff, prmp->power);
        masmd_add_num(p, 0x18c0b, prmp->boost_sub);
        masmd_add_num(p, 0x18c0d, prmp->boost_heat);
        masmd_add_num(p, 0x18c09, prmp->vs_mgun_sr);
        p->nIndent--;
        break;
    }
    case 2:
    case 15:
        __MASMD_SetDetailLinkedMisExt(ioDwp, sub / 10);
        break;
    case 3:
    case 4:
        __MASMD_SetDetailVsMisExt(ioDwp);
        break;
    case 6:
    case 9:
        __MASMD_SetDetailSideShieldExt(ioDwp);
        break;
    case 10:
        __MASMD_SetDetailStealthExt(ioDwp);
        break;
    case 11:
        __MASMD_SetDetailCoolerExt(ioDwp);
        break;
    case 12:
        __MASMD_SetDetailEnergyExt(ioDwp);
        break;
    case 14: {
        /* no NULL test here in the original */
        struct PartsParamExtensionSt *prmp = (struct PartsParamExtensionSt *)ioDwp->datap;
        DspList *p = masmd_dsp(ioDwp->cur);
        masmd_add_num(p, 0x18bad, prmp->vs_mgun_sr);
        p->nIndent++;
        masmd_add_num(p, 0x18bd1, prmp->vs_mgun_range);  /* signed (lh) */
        masmd_add_num(p, 0x18bf1, prmp->boost_sub);
        p->nIndent--;
        break;
    }
    case 16: {
        DspList *p = masmd_dsp(ioDwp->cur);
        struct PartsParamExtensionSt *prmp = (struct PartsParamExtensionSt *)ioDwp->datap;
        if (!prmp) return;
        masmd_add_num(p, 0x18be1, prmp->def_shell + prmp->def_energy);
        masmd_add_num(p, 0x18bdf, prmp->use_cnt);
        masmd_add_num(p, 0x18be0, prmp->reload_time);
        break;
    }
    default:
        break;
    }
}

/* inside part rows (Begin was done by SetDetailInside; wepp is not NULL-checked, as in the original): type 2 adds
 * an ecm row; the price row is "---" when inFlagEne */
static void __MASMD_SetDetailOtherInside(struct struct_04cfaec0 *ioDwp, int inFlagEne)
{
    PS2_FRAME(0x170);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    struct PartsParamInsideSt *prmp = (struct PartsParamInsideSt *)ioDwp->datap;
    if (!prmp) return;
    struct PartsParamWeaponSt *wep = (struct PartsParamWeaponSt *)ioDwp->wepp;
    masmd_add_str(p_dsp, 0x18c02, masmd_empty_str);
    p_dsp->nIndent++;
    if (prmp->type == 2) masmd_add_num(p_dsp, 0x18b82, wep->act.ecmnoise);
    masmd_add_num(p_dsp, 0x18c14, wep->act.lifetime);
    masmd_add_num(p_dsp, 0x18b5a, wep->blt_sta);
    masmd_add_num(p_dsp, 0x18ca4, wep->reload_time);
    if (inFlagEne) masmd_add_str(p_dsp, 0x18ca5, masmd_name(0x186d2));
    else masmd_add_num(p_dsp, 0x18ca5, wep->blt_price);
    p_dsp->nIndent--;
}

/* ---- group 4: inside / radiator / generator / FCS / booster / leg setters, arm-weapon rows ---- */

/* fill one entry the way the original's rodata templates do (all other words zero) */
static inline void masmd_ent(DetailEnt &e, int strid, int type, int p1, int p2)
{
    memset(&e, 0, sizeof(e));
    e.strid = strid;
    e.type = (enum_71127557)type;
    e.param1 = p1;
    e.param2 = p2;
}

/* fixed type-0 string row (template rows: no __MASMD_AddStringCopy test) */
static void masmd_add_str0(DspList *p_dsp, int strid, char *s)
{
    DetailEnt e;
    masmd_ent(e, strid, 0, (int)PS2PTR(s), 0);
    __MASMD_AddParam(p_dsp, &e);
}

/* inside weapon rows (Begin was done by SetDetailInside; wepp is not NULL-checked): header, damage : ammo, heat,
 * range ("---" when <= 0), reload, price ("---" when inFlagEne) */
static void __MASMD_SetDetailWepInside(struct struct_04cfaec0 *ioDwp, int inFlagEne)
{
    PS2_FRAME(0x1a0);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    if (!ioDwp->datap) return;
    struct PartsParamWeaponSt *wep = (struct PartsParamWeaponSt *)ioDwp->wepp;
    masmd_add_str(p_dsp, 0x18b81, masmd_name(inFlagEne ? 0x18c53 : 0x18c52));
    p_dsp->nIndent++;
    masmd_add_pair(p_dsp, 0x18b85, wep->dstat.damage, wep->blt_sta);
    masmd_add_num(p_dsp, 0x18bf8, wep->dstat.heat);
    float f = wep->act.range;
    if (f <= 0.0f) masmd_add_str(p_dsp, 0x18b5c, masmd_name(0x186d2));
    else masmd_add_num(p_dsp, 0x18b5c, ps2_ftoi(f));
    masmd_add_num(p_dsp, 0x18bc2, wep->reload_time);
    if (inFlagEne) masmd_add_str(p_dsp, 0x18ca5, masmd_name(0x186d2));
    else masmd_add_num(p_dsp, 0x18ca5, wep->blt_price);
    p_dsp->nIndent--;
}

/* kind 8: inside part: name, weight, energy, then sub_type % 10: 1 / 2 OtherInside, 0 WepInside */
static void __MASMD_SetDetailInside(struct struct_04cfaec0 *ioDwp)
{
    PS2_FRAME(0xc0);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    struct PartsParamBasicSt *datap = (struct PartsParamBasicSt *)ioDwp->datap;
    if (!datap) return;
    __MASMD_Begin(p_dsp, ioDwp->f_prmonly);
    masmd_add_str(p_dsp, 0x18bb0, (char *)datap + 0x12);
    masmd_add_num(p_dsp, 0x18bd5, datap->wp);
    masmd_add_num(p_dsp, 0x18bd8, datap->ep);
    int sub = ioDwp->sub_type;
    int k = sub % 10;
    if (k == 1 || k == 2) __MASMD_SetDetailOtherInside(ioDwp, sub / 10);
    else if (k == 0) __MASMD_SetDetailWepInside(ioDwp, sub / 10);
}

/* kind 7: radiator (record copied and tuned before Begin) */
static void __MASMD_SetDetailRadiator(struct struct_04cfaec0 *ioDwp)
{
    PS2_FRAME(0x1f0);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    if (!ioDwp->datap) return;
    struct PartsParamRadiatorSt tprm;
    memcpy(&tprm, ioDwp->datap, sizeof(tprm));
    if (ioDwp->pTune) GAcTune_TuneRadiator(&tprm, ioDwp->pTune);
    __MASMD_Begin(p_dsp, ioDwp->f_prmonly);
    masmd_add_num(p_dsp, 0x18bd5, tprm.basic.wp);
    masmd_add_num(p_dsp, 0x18bd8, tprm.basic.ep);
    masmd_add_num(p_dsp, 0x18c0e, tprm.cooling);
    masmd_add_num(p_dsp, 0x18baa, tprm.recov_cooling);
    masmd_add_num(p_dsp, 0x18b7f, tprm.recov_cooling_ep);
}

/* kind 6: generator (record copied and tuned before Begin; no energy row, unlike Radiator) */
static void __MASMD_SetDetailGenerator(struct struct_04cfaec0 *ioDwp)
{
    PS2_FRAME(0x1f0);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    if (!ioDwp->datap) return;
    struct PartsParamGeneratorSt tprm;
    memcpy(&tprm, ioDwp->datap, sizeof(tprm));
    if (ioDwp->pTune) GAcTune_TuneGenerator(&tprm, ioDwp->pTune);
    __MASMD_Begin(p_dsp, ioDwp->f_prmonly);
    masmd_add_num(p_dsp, 0x18bd5, tprm.basic.wp);
    masmd_add_num(p_dsp, 0x18b60, tprm.power);
    masmd_add_num(p_dsp, 0x18bc1, tprm.charge_max);
    masmd_add_num(p_dsp, 0x18bab, tprm.charge_recov);
    masmd_add_num(p_dsp, 0x18bf4, tprm.heat);
}

/* FCS display summary over the four lock records: per-step truncating float sum of the ranges, maxima clamped at 0
 * (pdisp is zeroed first), averages / 4 (truncating toward zero) */
static void __GetFcsDispInfo(struct struct_f2e6d00c *pdisp, struct PartsParamFcsSt *prmp)
{
    PS2_FRAME(0x30);
    ZeroMemory(pdisp, 0x1c);
    pdisp->lock_time = prmp->sight[0].lock_time;
    int lock_range = 0;
    for (int i = 0; i < 4; i++) {
        struct PartsParamBasicLockSt *s = &prmp->sight[i];
        float f = s->range;
        lock_range = ps2_ftoi((float)lock_range + f);
        int r = ps2_ftoi(f);
        if (r >= pdisp->lock_range_max) pdisp->lock_range_max = r;
        if (s->angx >= pdisp->sight_ang_max.x) pdisp->sight_ang_max.x = s->angx;
        if (s->angy >= pdisp->sight_ang_max.y) pdisp->sight_ang_max.y = s->angy;
        pdisp->sight_ang_ave.x += s->angx;
        pdisp->sight_ang_ave.y += s->angy;
    }
    pdisp->lock_range_ave = lock_range / 4;
    pdisp->sight_ang_ave.x /= 4;
    pdisp->sight_ang_ave.y /= 4;
}

/* kind 5: FCS (record not copied, no tuning): weight, energy, eccm, sight type, lock type, lock count, update speed,
 * lock time, sight angle max / average pairs, lock range max / average, parallel processing */
static void __MASMD_SetDetailFCS(struct struct_04cfaec0 *ioDwp)
{
    PS2_FRAME(0x250);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    struct PartsParamFcsSt *prmp = (struct PartsParamFcsSt *)ioDwp->datap;
    if (!prmp) return;
    struct struct_f2e6d00c disp;
    __GetFcsDispInfo(&disp, prmp);
    char *name;
    switch (prmp->sight_type) {
    case 4: name = masmd_name(0x18c6b); break;
    case 3: name = masmd_name(0x18c6a); break;
    case 2: name = masmd_name(0x18c68); break;
    case 1: name = masmd_name(0x18c67); break;
    case 0: name = masmd_name(0x18c6c); break;
    default: name = masmd_unkonwn_str; break;
    }
    __MASMD_Begin(p_dsp, ioDwp->f_prmonly);
    masmd_add_num(p_dsp, 0x18bd5, prmp->basic.wp);
    masmd_add_num(p_dsp, 0x18bd8, prmp->basic.ep);
    masmd_add_num(p_dsp, 0x18be2, prmp->eccm_power);
    masmd_add_str(p_dsp, 0x18bc7, name);
    masmd_add_str(p_dsp, 0x18c1c, masmd_name((prmp->lock_type & 0xf0) == 0 ? 0x18c59 : 0x18c5a));
    masmd_add_num(p_dsp, 0x18bc4, prmp->lockmax);
    masmd_add_num(p_dsp, 0x18c1d, prmp->system_update_speed);
    masmd_add_num(p_dsp, 0x18c20, disp.lock_time << 2);
    masmd_add_pair(p_dsp, 0x18c21, disp.sight_ang_max.x, disp.sight_ang_max.y);
    masmd_add_pair(p_dsp, 0x18c22, disp.sight_ang_ave.x, disp.sight_ang_ave.y);
    masmd_add_num(p_dsp, 0x18c23, disp.lock_range_max);
    masmd_add_num(p_dsp, 0x18c24, disp.lock_range_ave);
    masmd_add_num(p_dsp, 0x18bc9, prmp->paraeffic);
}

/* kind 4: booster (record copied and tuned before Begin): weight, energy, then an empty-titled group of power,
 * boost energy, boost heat (template arrays 0x1edb780 / 0x1edb7b0) */
static void __MASMD_SetDetailBooster(struct struct_04cfaec0 *ioDwp)
{
    PS2_FRAME(0x230);
    if (!ioDwp->datap) return;
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    struct PartsParamBoosterSt tprm;
    memcpy(&tprm, ioDwp->datap, sizeof(tprm));
    if (ioDwp->pTune) GAcTune_TuneBooster(&tprm, ioDwp->pTune);
    __MASMD_Begin(p_dsp, ioDwp->f_prmonly);
    DetailEnt a[2];
    masmd_ent(a[0], 0x18bd5, 2, tprm.basic.wp, 0);
    masmd_ent(a[1], 0x18bd8, 2, tprm.basic.ep, 0);
    for (int i = 0; i < 2; i++) __MASMD_AddParam(p_dsp, &a[i]);
    masmd_add_str(p_dsp, 0x18bfe, masmd_empty_str);
    p_dsp->nIndent++;
    DetailEnt b[3];
    masmd_ent(b[0], 0x18bff, 2, tprm.power, 0);
    masmd_ent(b[1], 0x18c0b, 2, tprm.boost_sub, 0);
    masmd_ent(b[2], 0x18c0d, 2, tprm.boost_heat, 0);
    for (int i = 0; i < 3; i++) __MASMD_AddParam(p_dsp, &b[i]);
    p_dsp->nIndent--;
}

/* kind 3: legs (record copied and tuned before Begin): 17 number rows (template 0x1edb620); leg types 3 / 4 show
 * "---" in rows 3, 11, 13, all others in rows 14, 15, 16 (the row keeps its id, type becomes 0 = string) */
static void __MASMD_SetDetailLeg(struct struct_04cfaec0 *ioDwp)
{
    static const int ids[17] = {0x18b52, 0x18bd5, 0x18ba9, 0x18c98, 0x18b5e, 0x18bcc, 0x18c7f, 0x18c0e, 0x18ba4,
                                0x18bdd, 0x18c9d, 0x18bea, 0x18bf7, 0x18bd2, 0x18bfb, 0x18c0b, 0x18b7e};
    static const int dash_a[3] = {3, 11, 13}, dash_b[3] = {14, 15, 16};
    PS2_FRAME(0x2f0);
    if (!ioDwp->datap) return;
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    struct PartsParamLegsSt tprm;
    memcpy(&tprm, ioDwp->datap, sizeof(tprm));
    if (ioDwp->pTune) GAcTune_TuneLeg(&tprm, ioDwp->pTune);
    __MASMD_Begin(p_dsp, ioDwp->f_prmonly);
    const int vals[17] = {tprm.defense.ap, tprm.basic.wp, tprm.basic.ep, tprm.stop_ep, tprm.max_wp,
                          tprm.defense.shell, tprm.defense.energy, tprm.defense.cooling, tprm.speed, tprm.rot_speed,
                          tprm.brake, tprm.damper, tprm.balance, tprm.jumpspd, tprm.boost_power, tprm.boost_sub,
                          tprm.boost_heat};
    DetailEnt entry[17];
    for (int i = 0; i < 17; i++) masmd_ent(entry[i], ids[i], 2, vals[i], 0);
    const int *dash = (tprm.type == 3 || tprm.type == 4) ? dash_a : dash_b;
    for (int j = 0; j < 3; j++) {
        entry[dash[j]].type = (enum_71127557)0;
        entry[dash[j]].param1 = (int)PS2PTR(masmd_name(0x186d2));
    }
    for (int i = 0; i < 17; i++) __MASMD_AddParam(p_dsp, &entry[i]);
}

/* arm-weapon (blade arm) rows, called by SetDetailArm with a1 = sub_type / 10 (never read: protofix) and the tuned
 * arm record in a2; wepp / wepp2 not NULL-checked, no Begin: blade damage / heat / range / energy rows, then an
 * empty-titled group of wep1 : wep2 compare rows (templates 0x1edb520 / 0x1edb590 / 0x1edb5b0) */
static void __MASMD_SetDetailWepArmB(struct struct_04cfaec0 *ioDwp, int inFlagEne, struct PartsParamArmsSt *prmp)
{
    (void)inFlagEne;
    PS2_FRAME(0x160);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    struct PartsParamWeaponSt *wep1 = (struct PartsParamWeaponSt *)ioDwp->wepp;
    struct PartsParamWeaponSt *wep2 = (struct PartsParamWeaponSt *)ioDwp->wepp2;
    DetailEnt a[5];
    masmd_ent(a[0], 0x18bb8, 2, prmp->blade_dstat.damage, 0);
    masmd_ent(a[1], 0x18cb7, 2, prmp->blade_heat, 0);
    masmd_ent(a[2], 0x18bf5, 2, wep1->dstat.heat, 0);
    masmd_ent(a[3], 0x18c07, 2, ps2_ftoi(prmp->blade_range), 0);
    masmd_ent(a[4], 0x18b88, 2, prmp->blade_bsub, 0);
    for (int i = 0; i < 5; i++) __MASMD_AddParam(p_dsp, &a[i]);
    masmd_add_str0(p_dsp, 0x18c25, masmd_empty_str); /* the template holds the PS2 address 0x27b4a0 */
    p_dsp->nIndent++;
    DetailEnt b[5];
    masmd_ent(b[0], 0x18c26, 4, wep1->dstat.damage, wep2->dstat.damage);
    masmd_ent(b[1], 0x18c27, 4, wep1->heat, wep2->heat);
    masmd_ent(b[2], 0x18c28, 4, wep1->dstat.heat, wep2->dstat.heat);
    int r1 = ps2_ftoi(wep1->act.range);
    int r2 = ps2_ftoi(wep2->act.range);
    masmd_ent(b[3], 0x18bbe, 4, r1, r2);
    masmd_ent(b[4], 0x18c29, 4, wep1->ep, wep2->ep);
    for (int i = 0; i < 5; i++) __MASMD_AddParam(p_dsp, &b[i]);
    p_dsp->nIndent--;
}

/* arm-weapon (gun arm) rows, called by SetDetailArm with the tuned arm record; wepp / wepp2 / prmp not checked, no
 * Begin: recoil control rows, then a header (energy flag dstat.effect & 0x20) and wep1 : wep2 compare rows */
static void __MASMD_SetDetailWepArm(struct struct_04cfaec0 *ioDwp, struct PartsParamArmsSt *prmp)
{
    PS2_FRAME(0x200);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    struct PartsParamWeaponSt *wep1 = (struct PartsParamWeaponSt *)ioDwp->wepp;
    struct PartsParamWeaponSt *wep2 = (struct PartsParamWeaponSt *)ioDwp->wepp2;
    int reload1, reload2;
    if (wep1->burst_shotcnt == 0) {
        reload1 = wep1->reload_time;
        reload2 = wep2->reload_time;
    } else {
        reload1 = wep1->breload_time;
        reload2 = wep2->breload_time;
    }
    DetailEnt a[2];
    float m = 100.0f * prmp->recoilctrl_accuracy; /* mul.s, then sub.s (not fused) */
    float acc = 300.0f - m;
    masmd_ent(a[0], 0x18bd6, 2, ps2_ftoi(acc), 0);
    masmd_ent(a[1], 0x18bd7, 2, prmp->recoilctrl_time, 0);
    for (int i = 0; i < 2; i++) __MASMD_AddParam(p_dsp, &a[i]);
    if ((wep1->dstat.effect & 0x20) == 0) masmd_add_str0(p_dsp, 0x18b81, masmd_name(0x18c52));
    else masmd_add_str0(p_dsp, 0x18b81, masmd_name(0x18c53));
    p_dsp->nIndent++;
    char *s;
    switch (wep1->sight_type) {
    case 3: s = masmd_name(0x18c69); break;
    case 2: s = masmd_name(0x18c68); break;
    case 1: s = masmd_name(0x18c67); break;
    case 0: s = masmd_name(0x18c6c); break;
    default: s = masmd_unkonwn_str; break;
    }
    DetailEnt b[5];
    masmd_ent(b[0], 0x18bc6, 0, (int)PS2PTR(s), 0);
    masmd_ent(b[1], 0x18bb9, 4, wep1->dstat.damage, wep2->dstat.damage);
    masmd_ent(b[2], 0x18bde, 2, wep1->blt_sta, 0);
    masmd_ent(b[3], 0x18bf8, 4, wep1->dstat.heat, wep2->dstat.heat);
    int r1 = ps2_ftoi(wep1->act.range);
    int r2 = ps2_ftoi(wep2->act.range);
    masmd_ent(b[4], 0x18b5c, 4, r1, r2);
    for (int i = 0; i < 5; i++) __MASMD_AddParam(p_dsp, &b[i]);
    if (wep1->lkmax > 0) masmd_add_pair(p_dsp, 0x18b68, wep1->lkmax, wep2->lkmax);
    else masmd_add_str0(p_dsp, 0x18b68, masmd_name(0x186d2));
    masmd_add_pair(p_dsp, 0x18bc2, reload1, reload2);
    if ((wep1->dstat.effect & 0x20) == 0) masmd_add_num(p_dsp, 0x18ca5, wep1->blt_price);
    else masmd_add_pair(p_dsp, 0x18b72, wep1->blt_price, wep2->blt_price);
    p_dsp->nIndent--;
}

/* ---- group 5: arm / core / head setters, the window framework ---- */

/* kind 2: arms (no datap test: both callers guard it; record copied and tuned before Begin): ap / weight / energy /
 * defences (template 0x1edb2e0), then by sub_type % 10: 2 blade arm (WepArmB), 1 and 3..9 gun arm (WepArm),
 * 0 plain arm: energy supply, recoil control accuracy 300 - 100 * x, recoil control time (template 0x1edb360) */
static void __MASMD_SetDetailArm(struct struct_04cfaec0 *ioDwp)
{
    PS2_FRAME(0x270);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    struct PartsParamArmsSt tprm;
    memcpy(&tprm, ioDwp->datap, sizeof(tprm));
    if (ioDwp->pTune) GAcTune_TuneArm(&tprm, ioDwp->pTune);
    __MASMD_Begin(p_dsp, ioDwp->f_prmonly);
    DetailEnt a[6];
    masmd_ent(a[0], 0x18b52, 2, tprm.defense.ap, 0);
    masmd_ent(a[1], 0x18bd5, 2, tprm.basic.wp, 0);
    masmd_ent(a[2], 0x18bd8, 2, tprm.basic.ep, 0);
    masmd_ent(a[3], 0x18bcc, 2, tprm.defense.shell, 0);
    masmd_ent(a[4], 0x18c7f, 2, tprm.defense.energy, 0);
    masmd_ent(a[5], 0x18c0e, 2, tprm.defense.cooling, 0);
    for (int i = 0; i < 6; i++) __MASMD_AddParam(p_dsp, &a[i]);
    int sub = ioDwp->sub_type;
    switch (sub % 10) {
    case 2: __MASMD_SetDetailWepArmB(ioDwp, sub / 10, &tprm); break;
    case 1: __MASMD_SetDetailWepArm(ioDwp, &tprm); break;
    case 0: {
        p_dsp = masmd_dsp(ioDwp->cur);
        DetailEnt b[3];
        float m = 100.0f * tprm.recoilctrl_accuracy; /* mul.s, then sub.s (not fused) */
        float acc = 300.0f - m;
        masmd_ent(b[0], 0x18b57, 2, tprm.ene_supply, 0);
        masmd_ent(b[1], 0x18bd6, 2, ps2_ftoi(acc), 0);
        masmd_ent(b[2], 0x18bd7, 2, tprm.recoilctrl_time, 0);
        for (int i = 0; i < 3; i++) __MASMD_AddParam(p_dsp, &b[i]);
        break;
    }
    default: __MASMD_SetDetailWepArm(ioDwp, &tprm); break;
    }
}

/* kind 1: core (no datap test; record copied and tuned before Begin): 11 rows (template 0x1edb110; rows 5 / 6 are
 * max_wp before cooling), then by the raw sub_type: 1 core weapon group (header + 5 rows, wepp unchecked), 0 overboost
 * group (empty-titled header + 3 rows) */
static void __MASMD_SetDetailCore(struct struct_04cfaec0 *ioDwp)
{
    PS2_FRAME(0x380);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    struct PartsParamCoreSt tprm;
    memcpy(&tprm, ioDwp->datap, sizeof(tprm));
    if (ioDwp->pTune) GAcTune_TuneCore(&tprm, ioDwp->pTune);
    __MASMD_Begin(p_dsp, ioDwp->f_prmonly);
    DetailEnt e[11];
    masmd_ent(e[0], 0x18b52, 2, tprm.defense.ap, 0);
    masmd_ent(e[1], 0x18bd5, 2, tprm.basic.wp, 0);
    masmd_ent(e[2], 0x18bd8, 2, tprm.basic.ep, 0);
    masmd_ent(e[3], 0x18bcc, 2, tprm.defense.shell, 0);
    masmd_ent(e[4], 0x18c7f, 2, tprm.defense.energy, 0);
    masmd_ent(e[5], 0x18b55, 2, tprm.max_wp, 0);
    masmd_ent(e[6], 0x18c0e, 2, tprm.defense.cooling, 0);
    masmd_ent(e[7], 0x18be4, 2, tprm.heat_limit, 0);
    masmd_ent(e[8], 0x18bda, 2, tprm.slot, 0);
    masmd_ent(e[9], 0x18c12, 2, tprm.vs_mgun_sr, 0);
    masmd_ent(e[10], 0x18c01, 0, (int)PS2PTR(masmd_name(0x18c4d + ((tprm.addon & 4) != 0))), 0);
    for (int i = 0; i < 11; i++) __MASMD_AddParam(p_dsp, &e[i]);
    int sub = ioDwp->sub_type;
    if (sub == 1) {
        struct PartsParamWeaponSt *wep = (struct PartsParamWeaponSt *)ioDwp->wepp;
        p_dsp = masmd_dsp(ioDwp->cur);
        DetailEnt h;
        masmd_ent(h, 0x18cc5, 0, (int)PS2PTR(masmd_name((wep->dstat.effect & 0x20) == 0 ? 0x18c52 : 0x18c53)), 0);
        __MASMD_AddParam(p_dsp, &h);
        DetailEnt b[5];
        masmd_ent(b[0], 0x18c8d, 4, wep->dstat.damage, wep->blt_sta);
        masmd_ent(b[1], 0x18bf8, 2, wep->dstat.heat, 0);
        masmd_ent(b[2], 0x18bce, 2, ps2_ftoi(wep->act.range), 0);
        masmd_ent(b[3], 0x18bc2, 2, wep->reload_time, 0);
        masmd_ent(b[4], 0x18ca5, 2, wep->blt_price, 0);
        if (wep->dstat.effect & 0x20) b[4].strid = 0x18bed;
        p_dsp->nIndent++;
        for (int i = 0; i < 5; i++) __MASMD_AddParam(p_dsp, &b[i]);
        p_dsp->nIndent--;
    } else if (sub == 0) {
        p_dsp = masmd_dsp(ioDwp->cur);
        masmd_add_str0(p_dsp, 0x18ba8, masmd_empty_str); /* the template holds the PS2 address 0x27b4a0 */
        DetailEnt b[3];
        masmd_ent(b[0], 0x18bfa, 2, tprm.ob_power * 10, 0);
        masmd_ent(b[1], 0x18c0a, 2, tprm.ob_ep, 0);
        masmd_ent(b[2], 0x18c0c, 2, tprm.obheat, 0); /* lh: signed */
        p_dsp->nIndent++;
        for (int i = 0; i < 3; i++) __MASMD_AddParam(p_dsp, &b[i]);
        p_dsp->nIndent--;
    }
}

/* kind 0: head (datap NULL -> return; record copied and tuned before Begin): 13 rows (template 0x1edafb0: numbers and
 * "yes / no"-style names), then, when the head has a radar (addon & 1), an indented radar group of 4 rows */
static void __MASMD_SetDetailHead(struct struct_04cfaec0 *ioDwp)
{
    static const int atlas_ids[4] = {0x18c4d, 0x18c65, 0x18c64, 0x18c66}; /* rodata 0x1edafa0, stack copy */
    PS2_FRAME(0x300);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    if (!ioDwp->datap) return;
    struct PartsParamHeadSt tprm;
    memcpy(&tprm, ioDwp->datap, sizeof(tprm));
    if (ioDwp->pTune) GAcTune_TuneHead(&tprm, ioDwp->pTune);
    __MASMD_Begin(p_dsp, ioDwp->f_prmonly);
    const unsigned char *b = (const unsigned char *)&tprm;
    DetailEnt e[13];
    masmd_ent(e[0], 0x18b52, 2, tprm.defense.ap, 0);
    masmd_ent(e[1], 0x18bd5, 2, tprm.basic.wp, 0);
    masmd_ent(e[2], 0x18bd8, 2, tprm.basic.ep, 0);
    masmd_ent(e[3], 0x18bcc, 2, tprm.defense.shell, 0);
    masmd_ent(e[4], 0x18c7f, 2, tprm.defense.energy, 0);
    masmd_ent(e[5], 0x18c0e, 2, tprm.defense.cooling, 0);
    masmd_ent(e[6], 0x18ba2, 2, tprm.balance, 0);
    masmd_ent(e[7], 0x18c80, 0, (int)PS2PTR(masmd_name(0x18c4a + (b[0x10c] & 0xf))), 0); /* cpu_grade */
    masmd_ent(e[8], 0x18c11, 0, (int)PS2PTR(masmd_name(atlas_ids[tprm.atlas_stat & 3])), 0);
    masmd_ent(e[9], 0x18ba1, 0, (int)PS2PTR(masmd_name(0x18c4d + ((tprm.addon & 0x10) != 0))), 0);
    masmd_ent(e[10], 0x18bdb, 0, (int)PS2PTR(masmd_name(0x18c4d + ((tprm.addon & 4) != 0))), 0);
    masmd_ent(e[11], 0x18be2, 2, tprm.radar_stat.eccm_power, 0);
    masmd_ent(e[12], 0x18c86, 0, (int)PS2PTR(masmd_name(0x18c4d + ((tprm.addon & 1) != 0))), 0);
    for (int i = 0; i < 13; i++) __MASMD_AddParam(p_dsp, &e[i]);
    if (tprm.addon & 1) {
        DetailEnt r[4];
        masmd_ent(r[0], 0x18c19, 2, ps2_ftoi(tprm.radar_stat.rd_range), 0);
        masmd_ent(r[1], 0x18c87, 2, tprm.radar_stat.rd_intvl, 0);
        masmd_ent(r[2], 0x18c13, 0, (int)PS2PTR(masmd_name(0x18c4d + ((tprm.radar_stat.stat & 1) != 0))), 0);
        masmd_ent(r[3], 0x18bdc, 0, (int)PS2PTR(masmd_name(0x18c4d + ((tprm.radar_stat.stat & 4) != 0))), 0);
        p_dsp->nIndent++;
        for (int i = 0; i < 4; i++) __MASMD_AddParam(p_dsp, &r[i]);
        p_dsp->nIndent--;
    }
}

/* play_func of the window: f_end -> tear down (help list if owned, children flagged, frame objects erased, compare /
 * tune buffers freed); else the stat machine: 2 fade out (7 frames, then 5), 4 changing / 1 fading in until the first
 * row object is idle. stat 4 / 1 dereference p_dsp / fo_p_dsp without a NULL test (quirk, kept). */
static void __MASMD_PlayDetailWin(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    DetailWin *ioDwp = masmd_dwp(ioObjp);
    DspList *p_dsp = ioDwp->cur ? masmd_dsp(ioDwp->cur) : (DspList *)PS2_NULL_MEM();
    DspList *fo_p_dsp = ioDwp->fade ? masmd_dsp(ioDwp->fade) : (DspList *)PS2_NULL_MEM();
    if (ioDwp->f_end == 1) {
        if (ioDwp->f_hlist_new) DF_MHELP_DeleteList(ioDwp->hlist);
        if (ioDwp->cur) masmd_dsp(ioDwp->cur)->f_end = 1;
        if (ioDwp->fade) masmd_dsp(ioDwp->fade)->f_end = 1;
        if (ioDwp->winp) {
            masmd_erase(ioDwp->winp);
            ioDwp->winp = 0;
        }
        if (ioDwp->strp) {
            masmd_erase(ioDwp->strp);
            ioDwp->strp = 0;
        }
        if (ioDwp->linep) {
            masmd_erase(ioDwp->linep);
            ioDwp->linep = 0;
        }
        if (ioObjp) masmd_erase(ioObjp);
        DF_MMM_Free(0, ioDwp->pCompare);
        ioDwp->pCompare = 0;
        DF_MMM_Free(0, ioDwp->pTune);
        ioDwp->pTune = 0;
        return;
    }
    switch (ioObjp->stat) {
    case 2:
        ioObjp->count++;
        if (ioObjp->count >= 7) ioObjp->stat = 5;
        break;
    case 4:
        if (!ioDwp->datap) {
            ioObjp->stat = 0;
        } else if (p_dsp->prm_objp[0]) {
            if (p_dsp->prm_objp[0]->stat == 0) ioObjp->stat = 0;
        } else {
            struct stMobj *o = fo_p_dsp->itemp[0];
            if (!o || o->stat == 5) ioObjp->stat = 0;
        }
        break;
    case 1: {
        struct stMobj *o = p_dsp->itemp[0];
        if (!o || o->stat == 0) ioObjp->stat = 0;
        break;
    }
    }
}

/* flag the window for deletion (PlayDetailWin tears it down next frame) and pop one help entry per shown label row */
void DF_MASMD_DeleteDetailWin(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    if (!ioObjp) return;
    DetailWin *ioDwp = masmd_dwp(ioObjp);
    ioDwp->f_end = 1;
    if (!ioDwp->cur) return;
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    for (int i = 0; i < 20; i++) {
        if (p_dsp->itemp[i]) {
            struct _mhelp_param_st *hpp = DF_MHELP_GetLastParam(ioDwp->hlist);
            if (hpp) DF_MHELP_DeleteParamFromList(ioDwp->hlist, hpp);
        }
    }
}

/* inlined child creation (cur / fade content objects): play_func PlayDWSub, parent / self links */
static inline struct stMobj *masmd_new_child(struct stMobj *parent)
{
    struct stMobj *o = DF_MOBJ_GetNewObject(parent);
    if (!o) return 0;
    o->type = 0;
    o->play_func = (void (*)(void))GFN(__MASMD_PlayDWSub);
    masmd_dsp(o)->pParent = parent;
    masmd_dsp(o)->pMobj = o;
    return o;
}

/* setter dispatch table (rodata 0x1edaf60, copied to the stack; indexed by kind with no bounds check) */
static void (*const masmd_set_tbl[14])(struct struct_04cfaec0 *) = {
    __MASMD_SetDetailHead,    __MASMD_SetDetailCore,      __MASMD_SetDetailArm,       __MASMD_SetDetailLeg,
    __MASMD_SetDetailBooster, __MASMD_SetDetailFCS,       __MASMD_SetDetailGenerator, __MASMD_SetDetailRadiator,
    __MASMD_SetDetailInside,  __MASMD_SetDetailExtension, __MASMD_SetDetailBack,      __MASMD_SetDetailWepR,
    __MASMD_SetDetailWepL,    __MASMD_SetDetailOptional};

/* show another part: same kind / sub kind (core: same vs_type) -> only the value objects fade (old ones move to the
 * new fade child); else the whole old content becomes the fading child and a new cur is built. Quirks kept: a new
 * fade child is made first even when it is replaced at once, p_dsp->y = rect.x, the category copy is unterminated,
 * pCompare is cleared only for modes 0 / 1, the old datap / hpp / fo->prm_objp[idx] are used unchecked. */
void DF_MASMD_ChangeDetailWin(struct stMobj *ioObjp, int inType, void *inDatap, void *inWepp, void *inWepp2, int mode,
                              struct StAcAsmTune *pTune)
{
    PS2_FRAME(0x100);
    if (!ioObjp) return;
    DetailWin *ioDwp = masmd_dwp(ioObjp);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    if (ioDwp->fade) masmd_dsp(ioDwp->fade)->f_end = 1;
    ioDwp->fade = masmd_new_child(ioObjp);
    ZeroMemory(ioDwp->pTune, 0x28);
    if (pTune) memcpy(ioDwp->pTune, pTune, 0x28);
    DspList *fo_p_dsp = masmd_dsp(ioDwp->fade);
    int sub_type = inDatap ? DF_MASM_GetSubType(inType, inDatap, inWepp) : -1;
    if (__MASMD_ChangeAll == 1) ioDwp->type = -1;
    ioDwp->nMode = (char)mode;
    if (ioDwp->nMode == 0 || ioDwp->nMode == 1) ZeroMemory(ioDwp->pCompare, 0xf4);
    int same = 0;
    if (ioDwp->type == inType && ioDwp->sub_type == sub_type) {
        if (inType != 1) same = 1;
        else if (inDatap && ((unsigned char *)inDatap)[0x120] == ((unsigned char *)ioDwp->datap)[0x120]) same = 1;
    }
    if (same) {
        ioDwp->f_prmonly = 1;
        for (int i = 0; i < 20; i++) {
            fo_p_dsp->prm_objp[i] = p_dsp->prm_objp[i];
            DF_MOBJ_SetFadeOut(fo_p_dsp->prm_objp[i]);
            p_dsp->prm_objp[i] = 0;
        }
        if (p_dsp->f_ctgr == 1) {
            strncpy((char *)fo_p_dsp->category, (char *)p_dsp->category, 0x1f); /* no terminator written (quirk) */
            /* the category string object's text pointer (obj+0x2c), unchecked */
            *(char **)((unsigned char *)fo_p_dsp->prm_objp[(int)p_dsp->ctgr_idx] + 0x2c) = (char *)fo_p_dsp->category;
        }
    } else {
        ioDwp->f_prmonly = 0;
        if (ioDwp->fade) masmd_dsp(ioDwp->fade)->f_end = 1;
        ioDwp->fade = ioDwp->cur;
        DspList *old = masmd_dsp(ioDwp->fade);
        ioDwp->cur = masmd_new_child(ioObjp);
        p_dsp = masmd_dsp(ioDwp->cur);
        for (int i = 0; i < 20; i++) {
            if (old->itemp[i]) {
                DF_MOBJ2_SetItemFadeOut(old->itemp[i]);
                struct _mhelp_param_st *hpp = DF_MHELP_GetLastParam(ioDwp->hlist);
                DF_MHELP_DeleteParamFromList(ioDwp->hlist, hpp); /* hpp not tested (unlike Delete) */
            }
            DF_MOBJ_SetFadeOut(old->prm_objp[i]);
        }
    }
    ioDwp->type = inType;
    ioDwp->sub_type = sub_type;
    ioDwp->datap = inDatap;
    ioDwp->wepp = inWepp;
    ioDwp->wepp2 = inWepp2;
    p_dsp->nNumParam = 0;
    p_dsp->x = ioDwp->rect.x;
    p_dsp->y = ioDwp->rect.x; /* sic: the original reloads rect word 0 */
    p_dsp->w = ioDwp->rect.w;
    if (ioDwp->datap) masmd_set_tbl[ioDwp->type](ioDwp);
    if (!ioDwp->f_prmonly)
        for (int i = 0; i < 20; i++) DF_MOBJ2_SetItemFadeIn(p_dsp->itemp[i]);
    for (int i = 0; i < 20; i++) DF_MOBJ_SetFadeIn(p_dsp->prm_objp[i]);
    ioObjp->stat = 4;
}

/* fade the window out (frame objects, the shown rows; a fading child is flagged for deletion); stat 2 */
void DF_MASMD_SetDetailWinFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    if (!ioObjp) return;
    DetailWin *ioDwp = masmd_dwp(ioObjp);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    DF_MOBJ_SetFadeOut(ioDwp->winp);
    DF_MOBJ_SetFadeOut(ioDwp->strp);
    DF_MOBJ_SetFadeOut(ioDwp->linep);
    if (ioDwp->fade) masmd_dsp(ioDwp->fade)->f_end = 1;
    ioDwp->fade = 0;
    for (int i = 0; i < 20; i++) {
        if (p_dsp->itemp[i]) DF_MOBJ2_SetItemFadeOut(p_dsp->itemp[i]);
        if (p_dsp->prm_objp[i]) DF_MOBJ_SetFadeOut(p_dsp->prm_objp[i]);
    }
    ioObjp->stat = 2;
    ioObjp->count = 0;
}

/* fade the window in (frame objects, the shown rows); stat 1 */
void DF_MASMD_SetDetailWinFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    if (!ioObjp) return;
    DetailWin *ioDwp = masmd_dwp(ioObjp);
    DspList *p_dsp = masmd_dsp(ioDwp->cur);
    DF_MOBJ_SetFadeIn(ioDwp->winp);
    DF_MOBJ_SetFadeIn(ioDwp->strp);
    DF_MOBJ_SetFadeIn(ioDwp->linep);
    for (int i = 0; i < 20; i++) {
        if (p_dsp->itemp[i]) DF_MOBJ2_SetItemFadeIn(p_dsp->itemp[i]);
        if (p_dsp->prm_objp[i]) DF_MOBJ_SetFadeIn(p_dsp->prm_objp[i]);
    }
    ioObjp->stat = 1;
    ioObjp->count = 0;
}

/* build the window: compare / tune buffers (always allocated, freed by PlayDetailWin), help list (own one if hlist is
 * NULL), cur + fade children, the first setter. mode 1 = record only: f_prmonly while building, then cleared with
 * sub_type = -1 so the first Change rebuilds everything. winp / strp / linep stay 0 (GetNewObject clears the object). */
struct stMobj *DF_MASMD_NewDetailWin(struct stMobj *inObjp, struct stMenuRect *inRectp, int inType, void *inDatap,
                                     void *inWepp, void *inWepp2, struct _mhelp_list_st *hlist,
                                     struct StAcAsmTune *pTune, int mode)
{
    PS2_FRAME(0xe0);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    obj->type = 0;
    DetailWin *ioDwp = masmd_dwp(obj);
    memcpy(&ioDwp->rect, inRectp, sizeof(ioDwp->rect)); /* lwc1 / swc1: bit copy */
    ioDwp->type = inType;
    ioDwp->datap = inDatap;
    ioDwp->wepp = inWepp;
    ioDwp->wepp2 = inWepp2;
    ioDwp->sub_type = -1;
    ioDwp->pMobj = obj;
    ioDwp->nMode = (char)mode;
    ioDwp->pCompare = (struct struct_35af9d5b *)DF_MMM_Alloc(0, 0xf4);
    ZeroMemory(ioDwp->pCompare, 0xf4);
    ioDwp->pTune = (struct StAcAsmTune *)DF_MMM_Alloc(0, 0x28);
    ZeroMemory(ioDwp->pTune, 0x28);
    if (pTune) memcpy(ioDwp->pTune, pTune, 0x28);
    if (!hlist) {
        ioDwp->hlist = DF_MHELP_NewList(0);
        ioDwp->f_hlist_new = 1;
    } else {
        ioDwp->hlist = hlist;
        ioDwp->f_hlist_new = 0;
    }
    ioDwp->depth = 1;
    if (ioDwp->datap) ioDwp->sub_type = DF_MASM_GetSubType(ioDwp->type, inDatap, inWepp);
    if (ioDwp->nMode == 1) ioDwp->f_prmonly = 1;
    ioDwp->cur = masmd_new_child(obj);
    ioDwp->fade = masmd_new_child(obj);
    if (ioDwp->datap) masmd_set_tbl[ioDwp->type](ioDwp);
    if (ioDwp->nMode == 1) {
        ioDwp->f_prmonly = 0;
        ioDwp->sub_type = -1;
    }
    obj->play_func = (void (*)(void))GFN(__MASMD_PlayDetailWin);
    obj->pkt_func = 0;
    return obj;
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper (9 int args, gen_bridge stops at 8); mode is the caller's stack arg at sp+0. Every
 * register is read before the call: the native calls the interpreted DF_MHELP_NewList, which clobbers a0-t3. */
static void bw__DF_MASMD_NewDetailWin(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    struct stMobj *inObjp = (struct stMobj *)bridge_ptr(c->gpr[A0].uw[0]);
    struct stMenuRect *inRectp = (struct stMenuRect *)bridge_ptr(c->gpr[A1].uw[0]);
    int inType = c->gpr[A2].sw[0];
    void *inDatap = bridge_ptr(c->gpr[A3].uw[0]);
    void *inWepp = bridge_ptr(c->gpr[T0].uw[0]);
    void *inWepp2 = bridge_ptr(c->gpr[T1].uw[0]);
    struct _mhelp_list_st *hlist = (struct _mhelp_list_st *)bridge_ptr(c->gpr[T2].uw[0]);
    struct StAcAsmTune *pTune = (struct StAcAsmTune *)bridge_ptr(c->gpr[T3].uw[0]);
    int mode = (int)rd32(sp);
    struct stMobj *r = DF_MASMD_NewDetailWin(inObjp, inRectp, inType, inDatap, inWepp, inWepp2, hlist, pTune, mode);
    c->gpr[V0].sd[0] = (s32)PS2PTR(r);
}
#endif

/* play_func of the cur / fade children: once flagged f_end, delete the label items, erase the value objects and
 * the child itself */
static void __MASMD_PlayDWSub(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    DspList *p_dsp = masmd_dsp(ioObjp);
    if (p_dsp->f_end != 1) return;
    for (int i = 0; i < 20; i++) {
        if (p_dsp->itemp[i]) {
            DF_MOBJ2_DeleteItem(p_dsp->itemp[i]);
            p_dsp->itemp[i] = 0;
        }
        if (p_dsp->prm_objp[i]) {
            masmd_erase(p_dsp->prm_objp[i]);
            p_dsp->prm_objp[i] = 0;
        }
    }
    if (ioObjp) masmd_erase(ioObjp);
}
