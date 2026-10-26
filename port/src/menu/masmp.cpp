/* src/menu/masmp.cpp — MASMP, the "gene window" of the assembly (garage) screen (OLMENU overlay): the small panel under
 * the part list that shows the selected part's parameters (rows of label + number) and colours each number by comparing
 * it with the equipped part (__MASMP_GetCol: blue = higher, red = lower, grey = equal / not comparable). One stMobj
 * (type 0, play_func __MASMP_PlayGeneWin, pkt_func __MakeVu1PackeGeneWin) owns a struct_ad077ad7 in its dt.
 * DF_MASMP_NewGeneWin builds it, DF_MASMP_ChangeGeneWin swaps the shown part (old rows fade out, new rows come from a
 * per-kind setter picked from a 14-entry table by gwp->type), Delete / SetGeneWinFadeOut / SetGeneWinFadeIn drive its
 * lifetime. Flag byte gwp+0x105: f_prmonly (same kind: only the numbers change), f_end (delete request polled by
 * PlayGeneWin), f_left (shape 1), f_change (= !inFlagDefault: equip.* rows are kept).
 * pWinMdl is only ever written with 0: the model branches are dead, but kept exactly (and the NULL-tolerant model calls
 * are still made).
 * The whole file is native: the window framework (GetCol, PlayGeneWin, Change / Delete / FadeOut / FadeIn,
 * SetGeneItems, the pkt function, NewGeneWin) and all 14 SetGene* setters with their leaves. */
#include "acnb/nodwarf.h"
#include "acnb/ps2fp.h"
#include <string.h>

#ifdef ACNB_SOURCEPORT
char masmp_underline[24] = "_______________________";
char masmp_unknown[8] = "Unknown";
#endif

typedef struct struct_ad077ad7 GeneWin;   /* gene window (dt) */
typedef void (*MasmpSetFn)(struct struct_ad077ad7 *);

static inline GeneWin *masmp_gwp(struct stMobj *o) { return (GeneWin *)&o->dt; }

/* inline delete of a sub-object: f_draw = 0, f_erase = 1 */
static inline void masmp_erase(struct stMobj *p)
{
    p->f_draw = 0;
    p->f_erase = 1;
}

/* the setter table (OLMENU rodata 0x1edb990), copied to the stack by Change / NewGeneWin and indexed by gwp->type
 * without a bounds check; the hybrid keeps the stack copy as PS2 words */
#ifdef ACNB_SOURCEPORT
typedef MasmpSetFn MasmpTblEnt;
#else
typedef unsigned int MasmpTblEnt;
#endif

static void masmp_copy_tbl(MasmpTblEnt *tbl)
{
    static const MasmpSetFn src[14] = {
        GFN(__MASMP_SetGeneHead),      GFN(__MASMP_SetGeneCore),      GFN(__MASMP_SetGeneArm),
        GFN(__MASMP_SetGeneLeg),       GFN(__MASMP_SetGeneBooster),   GFN(__MASMP_SetGeneFCS),
        GFN(__MASMP_SetGeneGenerator), GFN(__MASMP_SetGeneRadiator),  GFN(__MASMP_SetGeneInside),
        GFN(__MASMP_SetGeneExtension), GFN(__MASMP_SetGeneBack),      GFN(__MASMP_SetGeneWepR),
        GFN(__MASMP_SetGeneWepL),      GFN(__MASMP_SetGeneOptional),
    };
    for (int i = 0; i < 14; i++) tbl[i] = (MasmpTblEnt)(uintptr_t)src[i];
}

static inline MasmpSetFn masmp_fn(MasmpTblEnt e) { return (MasmpSetFn)(uintptr_t)e; }

/* datap == NULL and !f_change: clear itemNum and the equip / disp rows */
static void masmp_clear_rows(GeneWin *gwp)
{
    gwp->itemNum = 0;
    for (int i = 0; i < 6; i++) {
        gwp->equip.str_id[i] = 0;
        gwp->equip.val[i] = 0;
        gwp->disp.str_id[i] = 0;
        gwp->disp.val[i] = 0;
    }
}

/* the window model's rest pose (dead: pWinMdl is never created) */
static void masmp_reset_model(struct _stMModel *m)
{
    m->scale[0] = 0x1.033334p+3f;    /* 8.1f */
    m->scale[1] = 0x1.a66666p+1f;    /* 3.3f */
    m->scale[2] = 0.5f;
    m->ang[0] = 0x1.f46bbap-3f;      /* 14 deg */
    m->ang[1] = -0x1.aceea0p-2f;     /* -24 deg */
}

/* colour of row inItemIdx: compares the displayed value with the equipped part's row of the same string id.
 * Quirk: the sign test of the displayed value uses the equip row index j, the compare uses inItemIdx. */
static struct struct_76b810ec __MASMP_GetCol(struct struct_ad077ad7 *ioGwp, int inItemIdx)
{
    PS2_FRAME(0x50);
    struct struct_76b810ec col = {0x90, 0x90, 0x90, 0x80};   /* sp+0x4c default */
    struct struct_76b810ec hi = {0x40, 0x80, 0xf0, 0x80};    /* sp+0x48 displayed higher */
    struct struct_76b810ec lo = {0xfc, 0x46, 0x46, 0x80};    /* sp+0x44 displayed lower */
    if (MASMD_IsReverseParam(ioGwp->disp.str_id[inItemIdx])) {
        struct struct_76b810ec t = hi;
        hi = lo;
        lo = t;
    }
    int n = ioGwp->itemNum;
    int j = 0;
    if (n > 0) {
        int sid = ioGwp->disp.str_id[inItemIdx];
        do {
            if (ioGwp->equip.str_id[j] == sid) break;
            j++;
        } while (j < n);
    }
    if (j >= n) return col;
    int e = ioGwp->equip.val[j];
    if (e < 0) return col;
    if (ioGwp->disp.val[j] < 0) return col;
    int d = ioGwp->disp.val[inItemIdx];
    if (e < d)
        col = hi;
    else if (d < e)
        col = lo;
    return col;
}

#ifndef ACNB_SOURCEPORT
/* oracle: MW sret (a0 = result slot, a1 = gwp, a2 = row) for the interpreted setters; v0 is not set */
static void bw____MASMP_GetCol(EECore *c)
{
    void *out = bridge_ptr(c->gpr[A0].uw[0]);   /* latched first: the interpreted MASMD_IsReverseParam call clobbers a0 */
    struct struct_76b810ec r = __MASMP_GetCol((struct struct_ad077ad7 *)bridge_ptr(c->gpr[A1].uw[0]), c->gpr[A2].sw[0]);
    memcpy(out, &r, 4);
}
#endif

static void __MASMP_PlayGeneWin(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    GeneWin *gwp = masmp_gwp(ioObjp);
    DF_MMODEL_Play(gwp->pWinMdl);
    if (gwp->f_end) {
        for (int i = 0; i < 6; i++) {
            if (gwp->cur.itemp[i]) {
                DF_MOBJ2_DeleteItem(gwp->cur.itemp[i]);
                gwp->cur.itemp[i] = 0;
            }
            if (gwp->cur.prm_objp[i]) {
                masmp_erase(gwp->cur.prm_objp[i]);
                gwp->cur.prm_objp[i] = 0;
            }
        }
        for (int i = 0; i < 6; i++) {
            if (gwp->fade.itemp[i]) {
                DF_MOBJ2_DeleteItem(gwp->fade.itemp[i]);
                gwp->fade.itemp[i] = 0;
            }
            if (gwp->fade.prm_objp[i]) {
                masmp_erase(gwp->fade.prm_objp[i]);
                gwp->fade.prm_objp[i] = 0;
            }
        }
        if (gwp->winp) {
            masmp_erase(gwp->winp);
            gwp->winp = 0;
        }
        if (gwp->strp[0]) {
            masmp_erase(gwp->strp[0]);
            gwp->strp[0] = 0;
        }
        if (gwp->strp[1]) {
            masmp_erase(gwp->strp[1]);
            gwp->strp[1] = 0;
        }
        if (ioObjp) masmp_erase(ioObjp);
        DF_MMODEL_DeleteModel(gwp->pWinMdl);
        gwp->pWinMdl = 0;
        return;
    }
    switch (ioObjp->stat) {
    case 2: { /* fade out */
        struct _stMModel *m = gwp->pWinMdl;
        if (m) {
            m->scale[1] = m->scale[1] - 0x1.19999ap-1f;   /* 0.55f */
            m->scale[2] = m->scale[2] - 0x1.555556p-4f;   /* 0.083333336f */
            if (m->scale[1] < 0.0f) m->scale[1] = 0.0f;
            if (m->scale[2] < 0.0f) m->scale[2] = 0.0f;
        }
        if (gwp->strp[0]->stat == 5) ioObjp->stat = 5;   /* strp[0] not NULL-tested */
        break;
    }
    case 4: /* changing */
        if (gwp->datap == 0) {
            ioObjp->stat = 0;
            return;
        }
        if (gwp->cur.prm_objp[0]) {
            if (gwp->cur.prm_objp[0]->stat != 0) return;
        } else if (gwp->fade.itemp[0]) {
            if (gwp->fade.itemp[0]->stat != 5) return;
        }
        ioObjp->stat = 0;
        if (gwp->pWinMdl) masmp_reset_model(gwp->pWinMdl);
        break;
    case 1: { /* fade in */
        struct _stMModel *m = gwp->pWinMdl;
        if (m) {
            m->scale[0] = m->scale[0] + 0x1.59999ap+0f;   /* 1.35f */
            m->scale[1] = m->scale[1] + 0x1.19999ap-1f;   /* 0.55f */
            m->scale[2] = m->scale[2] + 0x1.555556p-4f;   /* 0.083333336f */
            if (!(m->scale[0] <= 0x1.033334p+3f)) m->scale[0] = 0x1.033334p+3f;   /* 8.1f */
            if (!(m->scale[1] <= 0x1.a66666p+1f)) m->scale[1] = 0x1.a66666p+1f;   /* 3.3f */
            if (!(m->scale[2] <= 0.5f)) m->scale[2] = 0.5f;
            m->ang[0] = m->ang[0] + 0x1.4d9d26p-5f;
            m->ang[1] = m->ang[1] + -0x1.53923ep-2f;
            m->ang[0] = DF_MFUNC_AngForm(m->ang[0]);
            m->ang[1] = DF_MFUNC_AngForm(m->ang[1]);
            if (!(0x1.ca5dcp+5f * m->ang[0] <= 14.0f)) m->ang[0] = 0x1.f46bbap-3f;
            if (0x1.ca5dcp+5f * m->ang[1] < -24.0f) m->ang[1] = -0x1.aceea0p-2f;
        }
        if (gwp->cur.itemp[0] == 0)
            ioObjp->stat = 0;
        else if (gwp->cur.itemp[0]->stat == 0)
            ioObjp->stat = 0;
        break;
    }
    default:
        break;
    }
}

/* inFlagDefault == 0 marks a "change" (equip.* rows kept); the unused slot (t0) is never read: wepp2 = inWepp */
void DF_MASMP_ChangeGeneWin(struct stMobj *ioObjp, int inType, void *inDatap, void *inWepp, void *unused,
                            int inFlagDefault, struct StAcAsmTune *pTune)
{
    PS2_FRAME(0xe0);
    PS2_STACK(MasmpTblEnt[14], tbl, 0xa0);
    GeneWin *gwp = masmp_gwp(ioObjp);
    (void)unused;
    for (int i = 0; i < 6; i++) {
        if (gwp->fade.itemp[i]) {
            DF_MOBJ2_DeleteItem(gwp->fade.itemp[i]);
            gwp->fade.itemp[i] = 0;
        }
        if (gwp->fade.prm_objp[i]) {
            masmp_erase(gwp->fade.prm_objp[i]);
            gwp->fade.prm_objp[i] = 0;
        }
    }
    int sub = -1;
    if (inDatap) sub = DF_MASM_GetSubType(inType, inDatap, inWepp);
    gwp->pTune = pTune;
    if (gwp->type == inType && gwp->sub_type == sub && inFlagDefault == 0) {
        gwp->f_prmonly = -1;   /* bit set (signed 1-bit field) */
        for (int i = 0; i < 6; i++) {
            gwp->fade.prm_objp[i] = gwp->cur.prm_objp[i];
            DF_MOBJ_SetFadeOut(gwp->fade.prm_objp[i]);
            gwp->cur.prm_objp[i] = 0;
        }
    } else {
        gwp->f_prmonly = 0;
        gwp->fade = gwp->cur;
        for (int i = 0; i < 6; i++) {
            if (gwp->fade.itemp[i]) {
                DF_MOBJ2_SetItemFadeOut(gwp->fade.itemp[i]);
                struct _mhelp_param_st *last = DF_MHELP_GetLastParam(gwp->hlist);
                if (gwp->init_hlp_num < gwp->hlist->count) DF_MHELP_DeleteParamFromList(gwp->hlist, last);
            }
            DF_MOBJ_SetFadeOut(gwp->fade.prm_objp[i]);
            gwp->cur.itemp[i] = 0;
            gwp->cur.prm_objp[i] = 0;
        }
    }
    gwp->type = inType;
    gwp->sub_type = sub;
    gwp->datap = inDatap;
    gwp->wepp = inWepp;
    gwp->wepp2 = inWepp;
    gwp->f_change = (inFlagDefault == 0);
    masmp_copy_tbl(tbl);
    if (gwp->datap != 0)
        gcall(masmp_fn(tbl[gwp->type]), gwp);   /* unchecked index */
    else if (!gwp->f_change)
        masmp_clear_rows(gwp);
    if (!gwp->f_prmonly)
        for (int i = 0; i < 6; i++) DF_MOBJ2_SetItemFadeIn(gwp->cur.itemp[i]);   /* callee tolerates NULL */
    for (int i = 0; i < 6; i++) DF_MOBJ_SetFadeIn(gwp->cur.prm_objp[i]);
    ioObjp->stat = 4;
}

/* sets the delete request (PlayGeneWin tears the window down) and trims the help list back to its construction size */
void DF_MASMP_DeleteGeneWin(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    GeneWin *gwp = masmp_gwp(ioObjp);
    gwp->f_end = -1;   /* bit set (signed 1-bit field) */
    for (int i = 0; i < 6; i++) {
        if (gwp->cur.itemp[i]) {
            struct _mhelp_param_st *last = DF_MHELP_GetLastParam(gwp->hlist);
            if (last && gwp->init_hlp_num < gwp->hlist->count) DF_MHELP_DeleteParamFromList(gwp->hlist, last);
        }
    }
}

void DF_MASMP_SetGeneWinFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    GeneWin *gwp = masmp_gwp(ioObjp);
    DF_MOBJ_SetFadeOut(gwp->winp);
    DF_MOBJ_SetFadeOut(gwp->strp[0]);
    DF_MOBJ_SetFadeOut(gwp->strp[1]);
    DF_MOBJ_SetFadeOut((struct stMobj *)gwp->pWinMdl);   /* a model passed as an object (always NULL) */
    for (int i = 0; i < 6; i++) {
        if (gwp->fade.itemp[i]) {
            DF_MOBJ2_DeleteItem(gwp->fade.itemp[i]);
            gwp->fade.itemp[i] = 0;
        }
        if (gwp->fade.prm_objp[i]) {
            masmp_erase(gwp->fade.prm_objp[i]);
            gwp->fade.prm_objp[i] = 0;
        }
    }
    for (int i = 0; i < 6; i++) {
        if (gwp->cur.itemp[i]) DF_MOBJ2_SetItemFadeOut(gwp->cur.itemp[i]);
        if (gwp->cur.prm_objp[i]) DF_MOBJ_SetFadeOut(gwp->cur.prm_objp[i]);
    }
    ioObjp->stat = 2;
    ioObjp->count = 0;
}

void DF_MASMP_SetGeneWinFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    GeneWin *gwp = masmp_gwp(ioObjp);
    DF_MOBJ_SetFadeIn(gwp->winp);
    DF_MOBJ_SetFadeIn(gwp->strp[0]);
    DF_MOBJ_SetFadeIn(gwp->strp[1]);
    DF_MOBJ_SetFadeIn((struct stMobj *)gwp->pWinMdl);
    for (int i = 0; i < 6; i++) {
        if (gwp->cur.itemp[i]) DF_MOBJ2_SetItemFadeIn(gwp->cur.itemp[i]);
        if (gwp->cur.prm_objp[i]) DF_MOBJ_SetFadeIn(gwp->cur.prm_objp[i]);
    }
    ioObjp->stat = 1;
    ioObjp->count = 0;
}

/* creates the label rows (one item + help param per string id); the help param refers to the item's dt+0x20 object */
static void __MASMP_SetGeneItems(struct struct_ad077ad7 *ioGwp, int inNum, int *inStrIDp)
{
    PS2_FRAME(0xa0);
    PS2_STACK(struct stMenuRect, rect, 0x90);
    rect = ioGwp->rect;
    ioGwp->itemNum = (char)inNum;
    for (int i = 0; i < inNum; i++) {
        int sid = MASMD_TransParamStringID(inStrIDp[i]);
        ioGwp->cur.itemp[i] = DF_MOBJ2_NewItem(ioGwp->strp[0], &rect, 0, 3, 7, inStrIDp[i], 0, 0, 0, 0);
        struct stMobj *ref = ((struct stMobj **)&ioGwp->cur.itemp[i]->dt)[8];   /* item +0x30 */
        DF_MHELP_AddParamToList(ioGwp->hlist, DF_MHELP_NewParam(0, inStrIDp[i], ref));
        if (!ioGwp->f_change) ioGwp->equip.str_id[i] = sid;
        ioGwp->disp.str_id[i] = sid;
        rect.y += 0x12;
    }
}

static void __MakeVu1PackeGeneWin(struct stMobj *pInObj)
{
    PS2_FRAME(0x20);
    if (pInObj) {
        unsigned int *dmap = DF_MPKT_MakeVIFCode(MPktCurrentp, 0, 0, 0, 0x13000000);
        if (masmp_gwp(pInObj)->pWinMdl) dmap = DF_MMODEL_MakeVu1PacketAC(dmap, masmp_gwp(pInObj)->pWinMdl, 0, 3);
        MPktCurrentp = dmap;
    }
}

/* builds the window: title + underline strings, then the rows of the shown part through the setter table.
 * Writes only these dt fields (the slot is zero from the object pool), no memset. */
struct stMobj *DF_MASMP_NewGeneWin(struct stMobj *inObjp, struct stMenuRect *inRectp, int inType, int inShape,
                                   void *inDatap, void *inWepp, void *inWepp2, struct _mhelp_list_st *ioHListp,
                                   struct StAcAsmTune *pTune)
{
    PS2_FRAME(0xf0);
    PS2_STACK(struct struct_76b810ec, col_title, 0xec);
    PS2_STACK(struct struct_76b810ec, col_line, 0xe8);
    PS2_STACK(MasmpTblEnt[14], tbl, 0xb0);
    col_title = {0x65, 0x65, 0x65, 0x80};
    col_line = {0x49, 0x49, 0x49, 0x80};
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (obj == 0) return 0;
    obj->type = 0;
    GeneWin *gwp = masmp_gwp(obj);
    gwp->rect = *inRectp;
    gwp->type = inType;
    gwp->datap = inDatap;
    gwp->wepp = inWepp;
    gwp->wepp2 = inWepp2;
    gwp->hlist = ioHListp;
    gwp->depth = 1;
    gwp->init_hlp_num = gwp->hlist->count;
    gwp->pTune = pTune;
    if (inShape == 1)
        gwp->f_left = -1;   /* bit set (signed 1-bit field) */
    else
        gwp->f_left = 0;
    if (gwp->datap) gwp->sub_type = DF_MASM_GetSubType(gwp->type, gwp->datap, inWepp);
    gwp->pWinMdl = 0;
    gwp->strp[0] = DF_MOBJ_NewString(obj, 0, 0x18b50, 0, 0, 0x14, 0x120, &col_title, 0, 0x800);
    DF_MOBJ_SetFadeIn(gwp->strp[0]);
    gwp->strp[1] = DF_MOBJ_NewStringDirect(gwp->strp[0], masmp_underline, 0, 0x15, 0x122, &col_line, 0, 0x800);
    DF_MOBJ_SetFadeIn(gwp->strp[1]);
    masmp_copy_tbl(tbl);
    if (gwp->datap != 0)
        gcall(masmp_fn(tbl[gwp->type]), gwp);   /* unchecked index */
    else if (!gwp->f_change)
        masmp_clear_rows(gwp);
    obj->play_func = (void (*)(void))GFN(__MASMP_PlayGeneWin);
    obj->pkt_func = (void (*)(void))GFN(__MakeVu1PackeGeneWin);
    return obj;
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper (9 int args, gen_bridge stops at 8); pTune is the caller's stack arg at sp+0 */
static void bw__DF_MASMP_NewGeneWin(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    struct stMobj *r = DF_MASMP_NewGeneWin(
        (struct stMobj *)bridge_ptr(c->gpr[A0].uw[0]), (struct stMenuRect *)bridge_ptr(c->gpr[A1].uw[0]),
        c->gpr[A2].sw[0], c->gpr[A3].sw[0], bridge_ptr(c->gpr[T0].uw[0]), bridge_ptr(c->gpr[T1].uw[0]),
        bridge_ptr(c->gpr[T2].uw[0]), (struct _mhelp_list_st *)bridge_ptr(c->gpr[T3].uw[0]),
        (struct StAcAsmTune *)bridge_ptr(rd32(sp)));
    c->gpr[V0].sd[0] = (s32)PS2PTR(r);
}
#endif

/* ---- group 2: optional / left-weapon / right-weapon setters ----------------------------------------------------------
 * Every numeric row follows one template: equip.val[i] only when !f_change, disp.val[i] always (before GetCol, which
 * reads both), colour from GetCol, value object = NewNumber at (xr - 0x28, y + 0x12 * i). xr = rect.x + rect.w, minus
 * 0x10 when !f_left (constant for every row). D = gwp->datap, W = gwp->wepp (never NULL-checked). */

static inline unsigned short masmp_u16(const void *p, int off) { return *(const unsigned short *)((const char *)p + off); }
static inline unsigned char masmp_u8(const void *p, int off) { return *(const unsigned char *)((const char *)p + off); }
static inline int masmp_f2i(const void *p, int off) { return ps2_ftoi(*(const float *)((const char *)p + off)); }

static inline int masmp_xr(GeneWin *gwp)
{
    int x = gwp->rect.x + gwp->rect.w;
    return gwp->f_left ? x : x - 0x10;
}

static inline void masmp_num_row(GeneWin *gwp, int i, int v, int xr, int y)
{
    if (!gwp->f_change) gwp->equip.val[i] = v;
    gwp->disp.val[i] = v;
    struct struct_76b810ec col = __MASMP_GetCol(gwp, i);
    gwp->cur.prm_objp[i] = DF_MOBJ_NewNumber(gwp->strp[0], v, 5, 0, 7, xr - 0x28, y + 0x12 * i, &col, 0);
}

/* optional part: one row; "OP-INTENSIFY" shows a label string instead of the slot number */
static void __MASMP_SetGeneOptional(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0x70);
    int ids[1] = {0x18b73};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 1, ids);
    void *D = ioGwp->datap;
    if (!D) return;
    int y = ioGwp->rect.y;
    if (strcmp((const char *)D, "OP-INTENSIFY") == 0) {
        char *name = (char *)DF_MSTR_GetName(MenuMng.language, 0, 0x186d8);
        struct struct_76b810ec col = {0x90, 0x90, 0x90, 0x80};
        int xr = masmp_xr(ioGwp);
        if (!ioGwp->f_change) ioGwp->equip.val[0] = -1;
        ioGwp->disp.val[0] = -1;
        ioGwp->cur.prm_objp[0] = DF_MOBJ_NewStringDirect(ioGwp->strp[0], name, 7, xr, y, &col, 0, 0);
        DF_MOBJ_SetStringXFromRX(ioGwp->cur.prm_objp[0], xr);
        return;
    }
    masmp_num_row(ioGwp, 0, masmp_u8(D, 0x100) /* PartsParamOptionSt.slot */, masmp_xr(ioGwp), y);
}

/* left weapon, howitzer */
static void __MASMP_SetGeneHowitzerWepL(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0xa0);
    int ids[5] = {0x18bd5, 0x18bd8, 0x18bb9, 0x18b5a, 0x18b5c};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    void *D = ioGwp->datap;
    if (!D) return;
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
    void *W = ioGwp->wepp;
    masmp_num_row(ioGwp, 2, masmp_u16(W, 0x1a), xr, y);
    masmp_num_row(ioGwp, 3, masmp_u16(W, 0x04) /* blt_sta */, xr, y);
    masmp_num_row(ioGwp, 4, masmp_f2i(W, 0x58), xr, y);
}

/* left weapon, shield (PartsParamSwordSt record) */
static void __MASMP_SetGeneShieldWepL(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0xa0);
    int ids[5] = {0x18bd5, 0x18bd8, 0x18bcc, 0x18c7f, 0x18c15};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    struct PartsParamSwordSt *D = (struct PartsParamSwordSt *)ioGwp->datap;
    if (!D) return;
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
    masmp_num_row(ioGwp, 2, D->def_shell, xr, y);
    masmp_num_row(ioGwp, 3, D->def_energy, xr, y);
    masmp_num_row(ioGwp, 4, (D->def_angx + D->def_angy) / 2, xr, y);   /* signed, truncating */
}

/* left weapon, blade: datap NULL creates nothing (not even the labels); type 1 shows wepp values */
static void __MASMP_SetGeneBladeWepL(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0xe0);
    struct PartsParamSwordSt *D = (struct PartsParamSwordSt *)ioGwp->datap;
    void *W = ioGwp->wepp;
    if (!D) return;
    int y = ioGwp->rect.y;
    if (D->type == 1) {
        int ids[5] = {0x18bd5, 0x18bd8, 0x18bbd, 0x18bbe, 0x18b7b};
        if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
        int xr = masmp_xr(ioGwp);
        masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
        masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
        masmp_num_row(ioGwp, 2, masmp_u16(W, 0x1a), xr, y);
        masmp_num_row(ioGwp, 3, masmp_f2i(W, 0x58), xr, y);
        masmp_num_row(ioGwp, 4, D->boost_sub, xr, y);
    } else {
        int ids[5] = {0x18bd5, 0x18bd8, 0x18bb8, 0x18c07, 0x18b88};
        if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
        int xr = masmp_xr(ioGwp);
        masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
        masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
        masmp_num_row(ioGwp, 2, masmp_u16(D, 0x10e), xr, y);   /* blade_dstat +2 */
        masmp_num_row(ioGwp, 3, ps2_ftoi(D->blade_range), xr, y);
        masmp_num_row(ioGwp, 4, D->boost_sub, xr, y);
    }
}

/* left weapon: by sub type % 10 (signed) */
static void __MASMP_SetGeneWepL(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0x10);
    switch (ioGwp->sub_type % 10) {
    case 0:
    case 1: __MASMP_SetGeneBladeWepL(ioGwp); break;
    case 2: __MASMP_SetGeneShieldWepL(ioGwp); break;
    case 3: __MASMP_SetGeneHowitzerWepL(ioGwp); break;
    }
}

/* right weapon, blade (PartsParamGunSt record + weapon params) */
static void __MASMP_SetGeneBladeWepR(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0xb0);
    int ids[5] = {0x18bd5, 0x18bd8, 0x18bba, 0x18bb5, 0x18c08};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    struct PartsParamGunSt *D = (struct PartsParamGunSt *)ioGwp->datap;
    if (!D) return;
    void *W = ioGwp->wepp;
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
    masmp_num_row(ioGwp, 2, masmp_u16(W, 0x1a), xr, y);
    masmp_num_row(ioGwp, 3, masmp_u16(W, 0x04), xr, y);
    masmp_num_row(ioGwp, 4, ps2_ftoi(D->blade_range), xr, y);
}

/* right weapon, gun */
static void __MASMP_SetGeneNormalWepR(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0xb0);
    int ids[5] = {0x18bd5, 0x18bd8, 0x18bb9, 0x18bde, 0x18b5c};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    void *D = ioGwp->datap;
    if (!D) return;
    void *W = ioGwp->wepp;
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
    masmp_num_row(ioGwp, 2, masmp_u16(W, 0x1a), xr, y);
    masmp_num_row(ioGwp, 3, masmp_u16(W, 0x04), xr, y);
    masmp_num_row(ioGwp, 4, masmp_f2i(W, 0x58), xr, y);
}

/* right weapon: by sub type % 10 (signed) */
static void __MASMP_SetGeneWepR(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0x10);
    switch (ioGwp->sub_type % 10) {
    case 0: __MASMP_SetGeneNormalWepR(ioGwp); break;
    case 1: __MASMP_SetGeneBladeWepR(ioGwp); break;
    }
}

/* ---- group 3: back-unit setters (SetGeneBack, table slot 10) and three extension setters (SetGeneExtension's jump
 * table: 12 Energy, 13 CureStat, 16 AddMagazine). Same row template as group 2; string rows show a GetName label with
 * equip / disp val = -1 and no GetCol, at xr itself (not xr - 0x28). */

/* back unit, booster (PartsParamBackSt) */
static void __MASMP_SetGeneBoosterBack(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0xa0);
    int ids[5] = {0x18bd5, 0x18bd8, 0x18c00, 0x18c1e, 0x18c1f};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    struct PartsParamBackSt *D = (struct PartsParamBackSt *)ioGwp->datap;
    if (!D) return;
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
    masmp_num_row(ioGwp, 2, D->boost_add, xr, y);   /* signed (lh) */
    masmp_num_row(ioGwp, 3, D->boost_ep, xr, y);
    masmp_num_row(ioGwp, 4, D->boost_heat, xr, y);
}

/* back unit, magazine: four rows, row 2 = "machine gun magazine present" label */
static void __MASMP_SetGeneMagzineBack(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0x90);
    int ids[4] = {0x18b65, 0x18bd8, 0x18b9b, 0x18b76};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 4, ids);
    struct PartsParamBackSt *D = (struct PartsParamBackSt *)ioGwp->datap;
    if (!D) return;
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
    {
        struct struct_76b810ec col = {0x90, 0x90, 0x90, 0x80};
        if (!ioGwp->f_change) ioGwp->equip.val[2] = -1;
        ioGwp->disp.val[2] = -1;
        char *name = (char *)DF_MSTR_GetName(MenuMng.language, 0, D->magazine_add_mgun ? 0x18c73 : 0x18c72);
        ioGwp->cur.prm_objp[2] = DF_MOBJ_NewStringDirect(ioGwp->strp[0], name, 7, xr, y + 0x24, &col, 0, 0);
        DF_MOBJ_SetStringXFromRX(ioGwp->cur.prm_objp[2], xr);
    }
    masmp_num_row(ioGwp, 3, (int)D->magazine_add_mgun + (int)D->magazine_add_rifle, xr, y);
}

/* back unit, radar: row 3 = (int) radar float, row 4 = radar type label */
static void __MASMP_SetGeneRadarBack(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0xa0);
    int ids[5] = {0x18bd5, 0x18bd8, 0x18be2, 0x18c19, 0x18c1a};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    void *D = ioGwp->datap;
    if (!D) return;
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
    masmp_num_row(ioGwp, 2, masmp_u16(D, 0x11e), xr, y);   /* radar_stat +0xe */
    masmp_num_row(ioGwp, 3, masmp_f2i(D, 0x110), xr, y);   /* radar_stat +0 */
    {
        struct struct_76b810ec col = {0x90, 0x90, 0x90, 0x80};
        if (!ioGwp->f_change) ioGwp->equip.val[4] = -1;
        ioGwp->disp.val[4] = -1;
        /* unchecked index (radar_stat +0xd) */
        char *name = (char *)DF_MSTR_GetName(MenuMng.language, 0, 0x18c5b + masmp_u8(D, 0x11d));
        ioGwp->cur.prm_objp[4] = DF_MOBJ_NewStringDirect(ioGwp->strp[0], name, 7, xr, y + 0x48, &col, 0, 0);
        DF_MOBJ_SetStringXFromRX(ioGwp->cur.prm_objp[4], xr);
    }
}

/* back unit, weapon (same rows as NormalWepR) */
static void __MASMP_SetGeneBackWep(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0xb0);
    int ids[5] = {0x18bd5, 0x18bd8, 0x18bb9, 0x18bde, 0x18b5c};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    void *D = ioGwp->datap;
    void *W = ioGwp->wepp;
    if (!D) return;
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
    masmp_num_row(ioGwp, 2, masmp_u16(W, 0x1a), xr, y);
    masmp_num_row(ioGwp, 3, masmp_u16(W, 0x04), xr, y);
    masmp_num_row(ioGwp, 4, masmp_f2i(W, 0x58), xr, y);
}

/* back unit: by sub type % 10 (signed) */
static void __MASMP_SetGeneBack(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0x10);
    switch (ioGwp->sub_type % 10) {
    case 5: __MASMP_SetGeneBoosterBack(ioGwp); break;
    case 4:
    case 3: __MASMP_SetGeneMagzineBack(ioGwp); break;
    case 1: __MASMP_SetGeneRadarBack(ioGwp); break;
    case 0: __MASMP_SetGeneBackWep(ioGwp); break;
    }
}

/* extension, cure status: four rows, row 3 from the weapon params */
static void __MASMP_SetGeneCureStatExt(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0xa0);
    int ids[4] = {0x18b65, 0x18bd8, 0x18b71, 0x18b72};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 4, ids);
    struct PartsParamExtensionSt *D = (struct PartsParamExtensionSt *)ioGwp->datap;
    void *W = ioGwp->wepp;
    if (!D) return;
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
    masmp_num_row(ioGwp, 2, D->boost_acctime, xr, y);
    masmp_num_row(ioGwp, 3, masmp_u16(W, 0x06) /* blt_price */, xr, y);
}

/* extension, additional magazine */
static void __MASMP_SetGeneAddMagazineExt(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0xa0);
    int ids[5] = {0x18bd5, 0x18bd8, 0x18be1, 0x18bdf, 0x18be0};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    struct PartsParamExtensionSt *D = (struct PartsParamExtensionSt *)ioGwp->datap;
    if (!D) return;
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
    masmp_num_row(ioGwp, 2, (int)D->def_shell + (int)D->def_energy, xr, y);
    masmp_num_row(ioGwp, 3, D->use_cnt, xr, y);
    masmp_num_row(ioGwp, 4, D->reload_time, xr, y);
}

/* extension, energy */
static void __MASMP_SetGeneEnergyExt(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0xa0);
    int ids[5] = {0x18bd5, 0x18bd8, 0x18bd3, 0x18bef, 0x18bf3};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    struct PartsParamExtensionSt *D = (struct PartsParamExtensionSt *)ioGwp->datap;
    if (!D) return;
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
    masmp_num_row(ioGwp, 2, (int)((unsigned int)D->power * D->boost_busytime), xr, y);   /* mult, low 32 bits */
    masmp_num_row(ioGwp, 3, D->use_cnt, xr, y);
    masmp_num_row(ioGwp, 4, D->boost_heat, xr, y);
}

/* extension, cooler: row 3 from the weapon params */
static void __MASMP_SetGeneCoolerExt(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0xb0);
    int ids[5] = {0x18b65, 0x18bd8, 0x18b71, 0x18b72, 0x18b78};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    struct PartsParamExtensionSt *D = (struct PartsParamExtensionSt *)ioGwp->datap;
    if (!D) return;
    void *W = ioGwp->wepp;
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
    masmp_num_row(ioGwp, 2, D->boost_acctime, xr, y);
    masmp_num_row(ioGwp, 3, masmp_u16(W, 0x06) /* blt_price */, xr, y);
    masmp_num_row(ioGwp, 4, D->power, xr, y);
}

/* extension, stealth */
static void __MASMP_SetGeneStealthExt(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0xa0);
    int ids[5] = {0x18b65, 0x18bd8, 0x18b82, 0x18b71, 0x18b72};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    struct PartsParamExtensionSt *D = (struct PartsParamExtensionSt *)ioGwp->datap;
    if (!D) return;
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
    masmp_num_row(ioGwp, 2, D->ecm_power, xr, y);
    masmp_num_row(ioGwp, 3, D->boost_acctime, xr, y);
    masmp_num_row(ioGwp, 4, D->boost_sub, xr, y);
}

/* extension, side shield: row 4 = mean of the two (signed) defence angles */
static void __MASMP_SetGeneSideShieldExt(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0xa0);
    int ids[5] = {0x18bd5, 0x18bd8, 0x18bcc, 0x18c7f, 0x18c15};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    struct PartsParamExtensionSt *D = (struct PartsParamExtensionSt *)ioGwp->datap;
    if (!D) return;
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
    masmp_num_row(ioGwp, 2, D->def_shell, xr, y);
    masmp_num_row(ioGwp, 3, D->def_energy, xr, y);
    masmp_num_row(ioGwp, 4, ((int)D->def_angx + (int)D->def_angy) / 2, xr, y);   /* truncating */
}

/* extension, booster */
static void __MASMP_SetGeneBoosterExt(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0xa0);
    int ids[5] = {0x18bd5, 0x18bd8, 0x18b66, 0x18c0b, 0x18c0d};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    struct PartsParamExtensionSt *D = (struct PartsParamExtensionSt *)ioGwp->datap;
    if (!D) return;
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
    masmp_num_row(ioGwp, 2, D->power, xr, y);
    masmp_num_row(ioGwp, 3, D->boost_sub, xr, y);
    masmp_num_row(ioGwp, 4, D->boost_heat, xr, y);
}

/* extension, counter missile: four rows */
static void __MASMP_SetGeneCounterMisExt(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0x90);
    int ids[4] = {0x18bd5, 0x18bd8, 0x18bad, 0x18bd1};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 4, ids);
    struct PartsParamExtensionSt *D = (struct PartsParamExtensionSt *)ioGwp->datap;
    if (!D) return;
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
    masmp_num_row(ioGwp, 2, D->vs_mgun_sr, xr, y);
    masmp_num_row(ioGwp, 3, D->vs_mgun_range, xr, y);   /* signed (lh) */
}

/* extension, interceptor missile: row 3 from the weapon params */
static void __MASMP_SetGeneInterceptorMisExt(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0xb0);
    int ids[5] = {0x18bd5, 0x18bd8, 0x18b98, 0x18bde, 0x18b5c};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    struct PartsParamExtensionSt *D = (struct PartsParamExtensionSt *)ioGwp->datap;
    if (!D) return;
    void *W = ioGwp->wepp;
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
    masmp_num_row(ioGwp, 2, D->vs_mgun_sr, xr, y);
    masmp_num_row(ioGwp, 3, masmp_u16(W, 0x04) /* blt_sta */, xr, y);
    masmp_num_row(ioGwp, 4, D->vs_mgun_range, xr, y);   /* signed (lh) */
}

/* extension, linked missile: rows 2-4 from the weapon params */
static void __MASMP_SetGeneLinkedMisExt(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0xb0);
    int ids[5] = {0x18bd5, 0x18bd8, 0x18bb9, 0x18b5a, 0x18b5c};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    struct PartsParamExtensionSt *D = (struct PartsParamExtensionSt *)ioGwp->datap;
    if (!D) return;
    void *W = ioGwp->wepp;
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
    masmp_num_row(ioGwp, 2, masmp_u16(W, 0x1a), xr, y);
    masmp_num_row(ioGwp, 3, masmp_u16(W, 0x04) /* blt_sta */, xr, y);
    masmp_num_row(ioGwp, 4, masmp_f2i(W, 0x58), xr, y);
}

/* extension: by sub type (unsigned jump table 0..16; 1, 5, 7 and anything above 16 show nothing) */
static void __MASMP_SetGeneExtension(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0x10);
    switch ((unsigned int)ioGwp->sub_type) {
    case 0:
    case 8:  __MASMP_SetGeneBoosterExt(ioGwp); break;
    case 2:
    case 15: __MASMP_SetGeneLinkedMisExt(ioGwp); break;
    case 3:
    case 4:  __MASMP_SetGeneInterceptorMisExt(ioGwp); break;
    case 6:
    case 9:  __MASMP_SetGeneSideShieldExt(ioGwp); break;
    case 10: __MASMP_SetGeneStealthExt(ioGwp); break;
    case 11: __MASMP_SetGeneCoolerExt(ioGwp); break;
    case 12: __MASMP_SetGeneEnergyExt(ioGwp); break;
    case 13: __MASMP_SetGeneCureStatExt(ioGwp); break;
    case 14: __MASMP_SetGeneCounterMisExt(ioGwp); break;
    case 16: __MASMP_SetGeneAddMagazineExt(ioGwp); break;
    default: break;
    }
}

/* ---- group 5: inside / radiator / generator / FCS / booster ---------------------------------------------------------
 * Same row template as groups 2-4. String rows (Inside A r2 / r4, FCS r2 / r3) mark both value slots -1 and pass xr
 * itself (not xr - 0x28) to NewStringDirect and SetStringXFromRX. The tuned setters copy datap into a host local P,
 * run GAcTune_Tune<kind>(&P, pTune) when pTune is set (native, writes P only) and read every row from P. */

static inline void masmp_str_mark(GeneWin *gwp, int i)
{
    if (!gwp->f_change) gwp->equip.val[i] = -1;
    gwp->disp.val[i] = -1;
}

static inline void masmp_str_obj(GeneWin *gwp, int i, void *name, int xr, int y, struct struct_76b810ec *col)
{
    gwp->cur.prm_objp[i] = DF_MOBJ_NewStringDirect(gwp->strp[0], (char *)name, 7, xr, y, col, 0, 0);
    DF_MOBJ_SetStringXFromRX(gwp->cur.prm_objp[i], xr);
}

/* inside part: sub_type % 10 == 0 -> A, raw sub_type == 2 -> B, else C; returns before SetGeneItems when datap is NULL */
static void __MASMP_SetGeneInside(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0x120);
    void *D = ioGwp->datap;
    if (!D) return;
    int y = ioGwp->rect.y;
    void *W = ioGwp->wepp;   /* PartsParamWeaponSt*, never NULL-checked */
    if (ioGwp->sub_type % 10 == 0) {
        int ids[5] = {0x18bd5, 0x18bd8, 0x18bb9, 0x18b5a, 0x18b5c};
        if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
        int xr = masmp_xr(ioGwp);
        masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
        masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
        unsigned int v = masmp_u16(W, 0x1a);   /* dstat +2 */
        if (v != 0) {
            masmp_num_row(ioGwp, 2, (int)v, xr, y);
        } else {
            struct struct_76b810ec col = {0x90, 0x90, 0x90, 0x80};
            masmp_str_mark(ioGwp, 2);
            void *name = DF_MSTR_GetName(MenuMng.language, 0, 0x186d2);
            masmp_str_obj(ioGwp, 2, name, xr, y + 0x24, &col);
        }
        masmp_num_row(ioGwp, 3, masmp_u16(W, 0x04) /* blt_sta */, xr, y);
        float f = *(const float *)((const char *)W + 0x58);
        if (!(f <= 0.0f)) {   /* NaN takes the numeric row */
            masmp_num_row(ioGwp, 4, masmp_f2i(W, 0x58), xr, y);
        } else {
            struct struct_76b810ec col = {0x90, 0x90, 0x90, 0x80};
            masmp_str_mark(ioGwp, 4);
            void *name = DF_MSTR_GetName(MenuMng.language, 0, 0x186d2);
            masmp_str_obj(ioGwp, 4, name, xr, y + 0x48, &col);
        }
    } else if (ioGwp->sub_type == 2) {
        int ids[5] = {0x18bd5, 0x18bd8, 0x18b82, 0x18c14, 0x18b5a};
        if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
        int xr = masmp_xr(ioGwp);
        masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
        masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
        masmp_num_row(ioGwp, 2, masmp_u16(W, 0x92), xr, y);   /* act +0x42 */
        masmp_num_row(ioGwp, 3, masmp_u16(W, 0x50), xr, y);   /* act +0 */
        masmp_num_row(ioGwp, 4, masmp_u16(W, 0x04) /* blt_sta */, xr, y);
    } else {
        int ids[5] = {0x18bd5, 0x18bd8, 0x18c14, 0x18b5a, 0x18bc2};
        if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
        int xr = masmp_xr(ioGwp);
        masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
        masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
        masmp_num_row(ioGwp, 2, masmp_u16(W, 0x50), xr, y);   /* act +0 */
        masmp_num_row(ioGwp, 3, masmp_u16(W, 0x04) /* blt_sta */, xr, y);
        masmp_num_row(ioGwp, 4, masmp_u8(W, 0x10) /* reload_time */, xr, y);
    }
}

/* radiator (tuned) */
static void __MASMP_SetGeneRadiator(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0x1b0);
    int ids[5] = {0x18bd5, 0x18bd8, 0x18c0e, 0x18baa, 0x18b7f};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    if (!ioGwp->datap) return;
    struct PartsParamRadiatorSt P;
    memcpy(&P, ioGwp->datap, sizeof P);
    if (ioGwp->pTune) GAcTune_TuneRadiator(&P, ioGwp->pTune);
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(&P, 0xbc), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(&P, 0xbe), xr, y);
    masmp_num_row(ioGwp, 2, P.cooling, xr, y);
    masmp_num_row(ioGwp, 3, P.recov_cooling, xr, y);
    masmp_num_row(ioGwp, 4, P.recov_cooling_ep, xr, y);
}

/* generator (tuned; no 0xbe row) */
static void __MASMP_SetGeneGenerator(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0x1b0);
    int ids[5] = {0x18bd5, 0x18b60, 0x18bc1, 0x18bab, 0x18bf4};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    if (!ioGwp->datap) return;
    struct PartsParamGeneratorSt P;
    memcpy(&P, ioGwp->datap, sizeof P);
    if (ioGwp->pTune) GAcTune_TuneGenerator(&P, ioGwp->pTune);
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(&P, 0xbc), xr, y);
    masmp_num_row(ioGwp, 1, P.power, xr, y);
    masmp_num_row(ioGwp, 2, P.charge_max, xr, y);
    masmp_num_row(ioGwp, 3, P.charge_recov, xr, y);
    masmp_num_row(ioGwp, 4, P.heat, xr, y);
}

/* FCS (untuned): rows 2 / 3 are strings (sight type, lock type) */
static void __MASMP_SetGeneFCS(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0xc0);
    int ids[5] = {0x18bd5, 0x18bd8, 0x18bc7, 0x18c1c, 0x18c1d};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    struct PartsParamFcsSt *D = (struct PartsParamFcsSt *)ioGwp->datap;
    if (!D) return;
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(D, 0xbc), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(D, 0xbe), xr, y);
    /* The original indexes its 5-word stack copy with sight_type unchecked (sp+0x70 + 4 * idx: an index > 4 reads
     * unrelated stack). Port: out-of-range indices are clamped to 4 (never seen in the part data, see quirks). */
    static const int sight_ids[5] = {0x18c54, 0x18c55, 0x18c56, 0x18c57, 0x18c58};
    unsigned int si = D->sight_type;
    void *name = DF_MSTR_GetName(MenuMng.language, 0, sight_ids[si < 5 ? si : 4]);
    struct struct_76b810ec col2 = {0x90, 0x90, 0x90, 0x80};
    masmp_str_mark(ioGwp, 2);
    masmp_str_obj(ioGwp, 2, name, xr, y + 0x24, &col2);
    name = DF_MSTR_GetName(MenuMng.language, 0, (D->lock_type & 0xf0) ? 0x18c5a : 0x18c59);
    struct struct_76b810ec col3 = {0x90, 0x90, 0x90, 0x80};
    masmp_str_mark(ioGwp, 3);
    masmp_str_obj(ioGwp, 3, name, xr, y + 0x36, &col3);
    masmp_num_row(ioGwp, 4, D->system_update_speed, xr, y);
}

/* booster (tuned) */
static void __MASMP_SetGeneBooster(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0x1d0);
    int ids[5] = {0x18bd5, 0x18bd8, 0x18bff, 0x18c0b, 0x18c0d};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    if (!ioGwp->datap) return;
    struct PartsParamBoosterSt P;
    memcpy(&P, ioGwp->datap, sizeof P);
    if (ioGwp->pTune) GAcTune_TuneBooster(&P, ioGwp->pTune);
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(&P, 0xbc), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(&P, 0xbe), xr, y);
    masmp_num_row(ioGwp, 2, P.power, xr, y);
    masmp_num_row(ioGwp, 3, P.boost_sub, xr, y);
    masmp_num_row(ioGwp, 4, P.boost_heat, xr, y);
}

/* ---- group 6: leg / arm (+ blade / weapon / normal leaves) / core / head ----------------------------------------------
 * Same row template. Leg / Core / Head copy datap into a host local P (after SetGeneItems, datap NULL -> return) and
 * tune it; SetGeneArm copies and tunes before anything else (datap NULL -> no SetGeneItems at all) and hands &P to the
 * leaf picked by sub_type % 10 (1 weapon arm, 2 blade arm, anything else normal arm). Core r4 / Head r4 are strings. */

/* leg (tuned) */
static void __MASMP_SetGeneLeg(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0x1f0);
    int ids[5] = {0x18b52, 0x18bd5, 0x18bd8, 0x18b5e, 0x18ba4};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    if (!ioGwp->datap) return;
    struct PartsParamLegsSt P;
    memcpy(&P, ioGwp->datap, sizeof P);
    if (ioGwp->pTune) GAcTune_TuneLeg(&P, ioGwp->pTune);
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(&P, 0x100), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(&P, 0xbc), xr, y);
    masmp_num_row(ioGwp, 2, masmp_u16(&P, 0xbe), xr, y);
    masmp_num_row(ioGwp, 3, P.max_wp, xr, y);
    masmp_num_row(ioGwp, 4, P.speed, xr, y);
}

/* blade arm (prmp = SetGeneArm's tuned copy) */
static void __MASMP_SetGeneBladeArm(struct struct_ad077ad7 *ioGwp, struct PartsParamArmsSt *prmp)
{
    PS2_FRAME(0xa0);
    int ids[5] = {0x18b52, 0x18bd5, 0x18bd8, 0x18bb8, 0x18c07};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(prmp, 0x100), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(prmp, 0xbc), xr, y);
    masmp_num_row(ioGwp, 2, masmp_u16(prmp, 0xbe), xr, y);
    masmp_num_row(ioGwp, 3, masmp_u16(prmp, 0x132), xr, y);   /* blade_dstat +2 */
    masmp_num_row(ioGwp, 4, ps2_ftoi(prmp->blade_range), xr, y);
}

/* weapon arm: rows 3 / 4 from wepp (never NULL-checked) */
static void __MASMP_SetGeneWepArm(struct struct_ad077ad7 *ioGwp, struct PartsParamArmsSt *prmp)
{
    PS2_FRAME(0xb0);
    int ids[5] = {0x18b52, 0x18bd5, 0x18bd8, 0x18bb9, 0x18b5a};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    void *W = ioGwp->wepp;
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(prmp, 0x100), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(prmp, 0xbc), xr, y);
    masmp_num_row(ioGwp, 2, masmp_u16(prmp, 0xbe), xr, y);
    masmp_num_row(ioGwp, 3, masmp_u16(W, 0x1a), xr, y);   /* dstat +2 */
    masmp_num_row(ioGwp, 4, masmp_u16(W, 0x04) /* blt_sta */, xr, y);
}

/* normal arm: r4 = (int)(300 - 100 * recoilctrl_accuracy), mul then sub in single precision (no FMA) */
static void __MASMP_SetGeneNormalArm(struct struct_ad077ad7 *ioGwp, struct PartsParamArmsSt *prmp)
{
    PS2_FRAME(0xa0);
    int ids[5] = {0x18b52, 0x18bd5, 0x18bd8, 0x18b57, 0x18bd6};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(prmp, 0x100), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(prmp, 0xbc), xr, y);
    masmp_num_row(ioGwp, 2, masmp_u16(prmp, 0xbe), xr, y);
    masmp_num_row(ioGwp, 3, prmp->ene_supply, xr, y);
    masmp_num_row(ioGwp, 4, ps2_ftoi(300.0f - 100.0f * prmp->recoilctrl_accuracy), xr, y);
}

/* arm (tuned): datap NULL -> nothing at all */
static void __MASMP_SetGeneArm(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0x170);
    if (!ioGwp->datap) return;
    struct PartsParamArmsSt P;
    memcpy(&P, ioGwp->datap, sizeof P);
    if (ioGwp->pTune) GAcTune_TuneArm(&P, ioGwp->pTune);
    int r = ioGwp->sub_type % 10;   /* signed remainder */
    if (r == 1) __MASMP_SetGeneWepArm(ioGwp, &P);
    else if (r == 2) __MASMP_SetGeneBladeArm(ioGwp, &P);
    else __MASMP_SetGeneNormalArm(ioGwp, &P);
}

/* core (tuned): r4 = OB type string; func_type other than 0 / 1 / 5 shows the main-ELF literal "Unknown" */
static void __MASMP_SetGeneCore(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0x200);
    int ids[5] = {0x18b52, 0x18bd5, 0x18bd8, 0x18b55, 0x18b56};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    if (!ioGwp->datap) return;
    struct PartsParamCoreSt P;
    memcpy(&P, ioGwp->datap, sizeof P);
    if (ioGwp->pTune) GAcTune_TuneCore(&P, ioGwp->pTune);
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(&P, 0x100), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(&P, 0xbc), xr, y);
    masmp_num_row(ioGwp, 2, masmp_u16(&P, 0xbe), xr, y);
    masmp_num_row(ioGwp, 3, P.max_wp, xr, y);
    void *name;
    switch (P.func_type) {
    case 0: name = DF_MSTR_GetName(MenuMng.language, 0, 0x18c50); break;
    case 1: name = DF_MSTR_GetName(MenuMng.language, 0, 0x18c4f); break;
    case 5: name = DF_MSTR_GetName(MenuMng.language, 0, 0x18c51); break;
    default: name = masmp_unknown; break;   /* PS2 address: the string object keeps the pointer */
    }
    struct struct_76b810ec col = {0x90, 0x90, 0x90, 0x80};
    masmp_str_mark(ioGwp, 4);
    masmp_str_obj(ioGwp, 4, name, xr, y + 0x48, &col);
}

/* head (tuned, 0x140-byte record): r4 = string 0x18c4d / 0x18c4e by addon bit 0 */
static void __MASMP_SetGeneHead(struct struct_ad077ad7 *ioGwp)
{
    PS2_FRAME(0x1f0);
    int ids[5] = {0x18b52, 0x18bd5, 0x18bd8, 0x18be2, 0x18c18};
    if (!ioGwp->f_prmonly) __MASMP_SetGeneItems(ioGwp, 5, ids);
    if (!ioGwp->datap) return;
    struct PartsParamHeadSt P;
    memcpy(&P, ioGwp->datap, sizeof P);
    if (ioGwp->pTune) GAcTune_TuneHead(&P, ioGwp->pTune);
    int y = ioGwp->rect.y;
    int xr = masmp_xr(ioGwp);
    masmp_num_row(ioGwp, 0, masmp_u16(&P, 0x100), xr, y);
    masmp_num_row(ioGwp, 1, masmp_u16(&P, 0xbc), xr, y);
    masmp_num_row(ioGwp, 2, masmp_u16(&P, 0xbe), xr, y);
    masmp_num_row(ioGwp, 3, masmp_u16(&P, 0x11e), xr, y);   /* radar_stat +0xe */
    void *name = DF_MSTR_GetName(MenuMng.language, 0, 0x18c4d + ((P.addon & 1) != 0));
    struct struct_76b810ec col = {0x90, 0x90, 0x90, 0x80};
    masmp_str_mark(ioGwp, 4);
    masmp_str_obj(ioGwp, 4, name, xr, y + 0x48, &col);
}
