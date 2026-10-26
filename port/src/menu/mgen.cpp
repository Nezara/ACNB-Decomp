/* src/menu/mgen.cpp - small helpers of the menu "generic" layer: build a 3D menu model from an MMD object id
 * (NewModelEasy -> NewModelEasyFromMMD, default pos / ang / scale / grey colour for NULL arguments), add a help-text
 * parameter to a help list, find the top index of a sub-category run in a part-id table (masm __AsmSys_Play's L1 / R1
 * jumps) and the "return to garage" key-guide call. */
#include <string.h>
#include "acnb/sdk.h"

/* OLMENU rodata 0x1ede230 / 0x1ede240 / 0x1ede250, main sdata 0x27b844 */
static const float mgen_def_pos[4] = {0.0f, 0.0f, 0.0f, 1.0f};
static const float mgen_def_ang[4] = {0.0f, 0x1.921fb6p+1f, 0.0f, 1.0f};   /* 0x40490fdb */
static const float mgen_def_scl[4] = {1.0f, 1.0f, 1.0f, 1.0f};

struct _stMModel *DF_MGEN_NewModelEasyFromMMD(int inId, void *datap, float (*inPosp)[4], float (*inAngp)[4],
                                              float (*inSclp)[4], struct struct_76b810ec *inColp)
{
    PS2_FRAME(0xd0);
    alignas(16) float pos[4];
    alignas(16) float ang[4];
    alignas(16) float scl[4];
    struct struct_76b810ec col;

    /* the defaults are copied even when the caller passes its own */
    memcpy(pos, mgen_def_pos, sizeof pos);
    memcpy(ang, mgen_def_ang, sizeof ang);
    memcpy(scl, mgen_def_scl, sizeof scl);
    col.r = col.g = col.b = col.a = 0x80;

    float (*posp)[4] = inPosp ? inPosp : &pos;
    float (*angp)[4] = inAngp ? inAngp : &ang;
    float (*sclp)[4] = inSclp ? inSclp : &scl;
    struct struct_76b810ec *colp = inColp ? inColp : &col;

    struct _stMModel *mp = DF_MMODEL_GetNewModel(0);
    struct stMmdData *mmdp = DF_MMD_GetNewMMD(0);
    DF_MMD_SetData(mmdp, 2, inId, datap);
    DF_MMODEL_SetData(mp, mmdp, posp, angp, sclp);
    DF_MMODEL_SetVertColor(mp, 0, colp);
    mp->alpha_reg = 0x88;
    mp->mulalpha = 0.0f;
    return mp;
}

struct _stMModel *DF_MGEN_NewModelEasy(int inId, float (*inPosp)[4], float (*inAngp)[4], float (*inSclp)[4],
                                       struct struct_76b810ec *inColp)
{
    PS2_FRAME(0x60);
    void *datap = DF_MMD_GetObjectMMD(inId);
    return DF_MGEN_NewModelEasyFromMMD(inId, datap, inPosp, inAngp, inSclp, inColp);
}

struct _mhelp_param_st *DF_MGEN_MHELP_Add(struct _mhelp_list_st *hlist, int str_id, struct stMobj *strp)
{
    PS2_FRAME(0x30);
    if (!hlist) return 0;
    struct _mhelp_param_st *hp = DF_MHELP_NewParam(0, str_id, strp);
    DF_MHELP_AddParamToList(hlist, hp);
    return hp;
}

/* category of the part in table cell idx */
static inline int mgen_cat(struct stMPartIdTable *t, int idx)
{
    return DF_MPART_GetCategory(t->cell[idx].id);
}

/* walk back (wrapping to num - 1) while the category stays the one of `start`; returns the first index of the
 * run, 0 when the walk comes all the way around */
static inline int mgen_run_top(struct stMPartIdTable *t, int start, int cat)
{
    int idx = start;
    int pre;
    do {
        pre = idx;
        idx--;
        if (idx < 0) idx = t->num - 1;
        if (idx == start) return 0;
    } while (mgen_cat(t, idx) == cat);
    return pre;
}

int DF_MGEN_GetTopIdx_InSubCategory(struct stMPartIdTable *id_tablep, int table_idx)
{
    PS2_FRAME(0x60);
    int sub = mgen_cat(id_tablep, table_idx);
    return mgen_run_top(id_tablep, table_idx, sub);
}

/* direct == 0: the top of the current run, or the previous run's top when already there; direct != 0: the first
 * index after the current run */
int DF_MGEN_SearchNextTopIdx_InSubCategory(struct stMPartIdTable *id_tablep, int table_idx, int direct)
{
    PS2_FRAME(0x80);
    if (id_tablep->num == 1) return table_idx;

    int cat = mgen_cat(id_tablep, table_idx);
    int top = mgen_run_top(id_tablep, table_idx, cat);
    if (direct == 0 && top < table_idx) return top;

    if (direct == 0) {
        int prev = table_idx - 1;
        if (prev < 0) prev = id_tablep->num - 1;
        int cat2 = mgen_cat(id_tablep, prev);
        return mgen_run_top(id_tablep, prev, cat2);
    }

    /* forward: walk on (wrapping to 0) while the category stays; 0 when the walk comes all the way around */
    cat = mgen_cat(id_tablep, table_idx);
    int idx = table_idx;
    do {
        idx++;
        if (idx >= id_tablep->num) idx = 0;
        if (idx == table_idx) return 0;
    } while (mgen_cat(id_tablep, idx) == cat);
    return idx;
}

void DF_MGEN_MBGUIDE_RetGarage(void)
{
    /* DF_MBGUIDE_Change takes a 0x63-terminated kind list (DWARF: int (void)); tail call */
    gcall_as<int(int, int, int, int, int)>(GFN(DF_MBGUIDE_Change), 4, 1, 2, 3, 0x63);
}
