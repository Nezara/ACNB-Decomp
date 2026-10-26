/* src/menu/mperf.cpp - the garage's AC performance screen (OLMENU; top-menu item 1, created by
 * __PlayGarageSys_PadCheckWait2): an AC-name banner, a six-slot weapon table (back L/R, arm R/L, hanger R/L), a
 * 7-axis radar chart (six ability ranks + a total rank, redrawn every frame as a filled fan + outline), an AP readout
 * and the player's emblem. Circle leaves (fade out, pop the scene label, free everything), Start opens the key guide.
 * One stMobj whose dt union holds a POINTER to the heap struct_b4312594 (DF_MMM_Alloc(0, 0x1b0) in NewSystem); run by
 * DF_MOBJ_Play / DF_MOBJ_Draw through play_func (__Play) / draw_func (__Draw).
 * Whole file native: the state machine (__Play, __ProcInit / Main / Exit / KeyGuide), the fades, the radar chart
 * (__GetTotalRank, __DrawGraph, __Draw, __CreateGraphWindow), the teardown (__DeleteAll, __DeleteGraphWindow,
 * __DeleteParamWindow), the weapon rows (__Set*WeaponData, __CreateParamWindow) and DF_MPERF_NewSystem.
 * menu/mop.c has same-named statics (__SetFadeAll, __Play, ...): the bridge prelude maps this file's statics by its own
 * local header (addresses 0x1e86490..), so the names do not clash. */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

#ifdef ACNB_SOURCEPORT
/* main sdata 0x27bcf8 / 0x27bcfc */
struct struct_76b810ec s_WindowTitleStringColor = {0x8a, 0x8a, 0x8a, 0x80};
struct struct_76b810ec s_NormalStringColor = {0x7d, 0x91, 0x9b, 0x80};
/* main sdata 0x27bd30 / 0x27bd34 / 0x27bd38 (nodwarf.h): newlib sprintf formats must be PS2 memory */
char mperf_fmt_d[3] = "%d";
char mperf_str_empty[1] = "";
char mperf_fmt_ac[5] = "AC%d";
#endif

/* obj+0x10 is the dt pointer (not the inline union) */
#define MPERF_DT(o) (*(struct struct_b4312594 **)&(o)->dt)

/* main sdata 0x27bd20 / 0x27bd24: radar fill / outline colour words (byte 3 = alpha, set per frame) */
#define MPERF_GRAPH_FILL_COL 0x00362800u
#define MPERF_GRAPH_LINE_COL 0x00c6ad6du

/* inlined DF_MOBJ_Delete + clear: f_draw = 0 stored, then f_erase = 1 (byte RMW pair), slot zeroed; null-tested */
static inline void mperf_delete(struct stMobj **pp)
{
    if (*pp != 0) {
        (*pp)->f_draw = 0;
        (*pp)->f_erase = 1;
        *pp = 0;
    }
}

/* the six spec values the ranks are taken from (inlined 3x in the original; array zeroed, then all six written) */
static inline void mperf_spec_values(const struct struct_dbbd570b *s, int v[6])
{
    v[0] = s->op;
    v[1] = s->dp;
    v[2] = s->effic_ep;
    v[3] = s->boost_max_speed;
    v[4] = (int)((unsigned int)s->cooling + (unsigned int)s->recov_cooling) / 2;   /* signed halve, trunc to zero */
    v[5] = s->eccm_perform;
}

/* first j with value >= thr[j] (descending thresholds, signed); 6 -> 5 */
static inline int mperf_rank(int value, const int thr[6])
{
    int j;
    for (j = 0; j < 6; j++) {
        if (!(value < thr[j]))
            break;
    }
    if (j == 6)
        j = 5;
    return j;
}

static void __SetFadeAll(struct stMobj *pObj, int FadeIn)
{
    PS2_FRAME(0x70);
    struct struct_b4312594 *dt = MPERF_DT(pObj);
    void (*unit)(struct stMobj *);
    void (*mobj)(struct stMobj *);
    int i;

    if (FadeIn == 1) {
        unit = UnitSprite_SetFadeIn;
        mobj = DF_MOBJ_SetFadeIn;
    } else {
        unit = UnitSprite_SetFadeOut;
        mobj = DF_MOBJ_SetFadeOut;
    }
    unit(dt->AcNameWindow.pWindow);
    mobj(dt->AcNameWindow.pAcNumber);
    mobj(dt->AcNameWindow.pAcName);
    for (i = 0; i < 6; i++) {
        struct struct_c17afc46 *it = &dt->ParamWindow.aWindowItem[i];
        unit(it->pWindow);
        mobj(it->pName);
        mobj(it->pAttackStr);
        mobj(it->pBulletNumStr);
        mobj(it->pAttackVal);
        mobj(it->pBulletNumVal);
    }
    for (i = 0; i < 2; i++)
        unit(dt->GraphWindow.apWindow[i]);
    for (i = 0; i < 7; i++) {
        mobj(dt->GraphWindow.apParamName[i]);
        mobj(dt->GraphWindow.apParamRank[i]);
    }
    mobj(dt->GraphWindow.pName);
    unit(dt->ApWindow.pWindow);
    mobj(dt->ApWindow.pName);
    mobj(dt->ApWindow.pAp);
    mobj(dt->EmblemWindow.pWindow);
}

static int __GetTotalRank(struct stMobj *pObj)
{
    PS2_FRAME(0xd0);
    struct struct_fb2047f5 *rk = (struct struct_fb2047f5 *)DF_MDATA_GetData(MenuMng.dataTbl[0], 9);
    int total = 0;
    int i, j;

    for (i = 0; i < 6; i++) {
        struct struct_dbbd570b *spec = &MPERF_DT(pObj)->AcSpec;
        /* re-fetched every pass; only the row comes from this fetch, the points from the first */
        struct struct_fb2047f5 *t = (struct struct_fb2047f5 *)DF_MDATA_GetData(MenuMng.dataTbl[0], 9);
        int v[6];
        mperf_spec_values(spec, v);
        total += rk->aRankToPoint[mperf_rank(v[i], t->aaPointToRank[i])];
    }
    for (j = 0; j < 6; j++) {
        if (!(total < rk->aTotalPointToRank[j]))
            break;
    }
    if (j == 6)
        j = 5;
    return j;
}

static void __DrawGraph(struct stMobj *pObj)
{
    PS2_FRAME(0x220);
    struct struct_b4312594 *dt = MPERF_DT(pObj);
    /* OLMENU 0x1edfef0 centre, 0x1edff00 hexagon corners, 0x1edff60 spoke order (closed polygon) */
    static const int centre[4] = {0x1fd, 0xc3, 0, 0};
    static const int hex[6][4] = {
        {0x1fd, 0x6f, 0, 0}, {0x1b5, 0x98, 0, 0}, {0x245, 0x98, 0, 0},
        {0x1b5, 0xee, 0, 0}, {0x245, 0xee, 0, 0}, {0x1fd, 0x117, 0, 0},
    };
    static const int order[7] = {0, 2, 4, 5, 3, 1, 0};
    int vtx[8][4] __attribute__((aligned(16)));
    unsigned int col[8] __attribute__((aligned(16)));
    unsigned int alpha = dt->GraphWindow.apParamRank[0]->dt.sprite.cur_col.a;   /* rank sprite 0 alpha (+0x53) */
    unsigned int fill = (MPERF_GRAPH_FILL_COL & 0x00ffffffu) | (alpha << 24);
    unsigned int line = (MPERF_GRAPH_LINE_COL & 0x00ffffffu) | (alpha << 24);
    int k;

    for (k = 0; k < 8; k++)
        col[k] = fill;
    memset(vtx, 0, sizeof(vtx));
    vtx[0][0] = centre[0];
    vtx[0][1] = centre[1];
    vtx[0][2] = centre[2];
    vtx[0][3] = centre[3];
    for (k = 0; k < 7; k++) {
        int ax = order[k];
        struct struct_dbbd570b *spec = &MPERF_DT(pObj)->AcSpec;
        struct struct_fb2047f5 *t = (struct struct_fb2047f5 *)DF_MDATA_GetData(MenuMng.dataTbl[0], 9);
        int v[6];
        int f, dx, dy;
        mperf_spec_values(spec, v);
        f = 6 - mperf_rank(v[ax], t->aaPointToRank[ax]);
        dx = hex[ax][0] - centre[0];
        dy = hex[ax][1] - centre[1];
        dx = (int)((unsigned int)dx * (unsigned int)f) / 6;   /* mult (low 32) + magic /6 */
        dy = (int)((unsigned int)dy * (unsigned int)f) / 6;   /* MMI mult1 (low 32) + magic /6 */
        vtx[k + 1][0] = centre[0] + dx;
        vtx[k + 1][1] = centre[1] + dy;
    }
    DF_MDRAW_DrawPrim(5, 8, vtx, (struct struct_76b810ec *)col, 1.0f, 0);
    for (k = 0; k < 8; k++)
        col[k] = line;
    DF_MDRAW_KickReg(0x42, 0x44);   /* ALPHA_1 */
    DF_MDRAW_DrawPrim(2, 7, vtx + 1, (struct struct_76b810ec *)col, 1.0f, 0);
}

/* original: `j __DrawGraph` (no frame) */
static void __Draw(struct stMobj *pObj)
{
    __DrawGraph(pObj);
}

static void __ProcKeyGuide(struct stMobj *pObj)
{
    PS2_FRAME(0x20);
    struct struct_b4312594 *dt = MPERF_DT(pObj);
    if (!MenuKeyGuide_IsShown())
        dt->ProcMode = 2;
}

static void __ProcMain(struct stMobj *pObj)
{
    PS2_FRAME(0x20);
    struct struct_b4312594 *dt = MPERF_DT(pObj);
    int u = MPadUser;

    if (MPad[u].Rrt == 1) {
        dt->ProcMode = 1;
        pObj->count = 0;
    } else if (MPad[u].start == 1) {
        MenuKeyGuide_Show(0x33);
        dt->ProcMode = 3;
    }
}

static void __ProcExit(struct stMobj *pObj)
{
    PS2_FRAME(0x20);
    if (pObj->count == 0) {
        Voice_Call(0x3eb, 0x7f);
        __SetFadeAll(pObj, 0);
        DF_MBACK_PopSceneLabel();   /* original passes a0 = 0x34; DWARF void (unused) */
    } else if (pObj->count == 6) {
        __DeleteAll(pObj);
    }
    pObj->count++;
}

static void __ProcInit(struct stMobj *pObj)
{
    PS2_FRAME(0x20);
    struct struct_b4312594 *dt = MPERF_DT(pObj);

    __SetFadeAll(pObj, 1);
    DF_M_AC_SetFadeOut(M_ACp[dt->PlayerNo][1], 5, 0.0f);
    DF_M_AC_SetFadeOut(M_ACp[dt->PlayerNo][2], 5, 0.0f);
    gcall_as<int(int, int)>(GFN(DF_MBGUIDE_Change), 2, 0x63);   /* DWARF void(void); key list ends with 0x63 */
    DF_MVIEW_MotionStart(MViewp, 0xb);
    DF_MBACK_PushSceneLabel((enum_5124d09c)0x34);
    DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x18ccc));
    dt->ProcMode = 2;
    Voice_Call(0x3ec, 0x7f);
}

static void __Play(struct stMobj *pObj)
{
    PS2_FRAME(0x20);
    /* OLMENU 0x1edfee0, copied to sp+0x10; ProcMode not bound-checked */
    void (*const tbl[4])(struct stMobj *) = {GFN(__ProcInit), GFN(__ProcExit), GFN(__ProcMain), GFN(__ProcKeyGuide)};
    gcall(tbl[MPERF_DT(pObj)->ProcMode], pObj);
}

static void __DeleteAll(struct stMobj *pObj)
{
    PS2_FRAME(0x30);
    struct struct_b4312594 *dt = MPERF_DT(pObj);

    UnitSprite_Delete(dt->AcNameWindow.pWindow);
    mperf_delete(&dt->AcNameWindow.pAcNumber);
    mperf_delete(&dt->AcNameWindow.pAcName);
    __DeleteParamWindow(pObj);
    __DeleteGraphWindow(pObj);
    dt = MPERF_DT(pObj);
    UnitSprite_Delete(dt->ApWindow.pWindow);
    mperf_delete(&dt->ApWindow.pName);
    mperf_delete(&dt->ApWindow.pAp);
    dt = MPERF_DT(pObj);
    mperf_delete(&dt->EmblemWindow.pWindow);
    MenuTex_UnRegisterSingleTex(dt->EmblemWindow.TextureId);
    pObj->stat = 5;
    pObj->play_func = 0;
    pObj->draw_func = 0;
    DF_MMM_Free(0, MPERF_DT(pObj));
}

static void __DeleteGraphWindow(struct stMobj *pObj)
{
    PS2_FRAME(0x40);
    struct struct_45db0ffd *g = &MPERF_DT(pObj)->GraphWindow;
    int i;

    for (i = 0; i < 2; i++)
        UnitSprite_Delete(g->apWindow[i]);
    mperf_delete(&g->pName);
    for (i = 0; i < 7; i++)
        mperf_delete(&g->apParamName[i]);
    for (i = 0; i < 7; i++)
        mperf_delete(&g->apParamRank[i]);
}

static void __CreateGraphWindow(struct stMobj *pObj)
{
    PS2_FRAME(0x220);
    struct struct_45db0ffd *g = &MPERF_DT(pObj)->GraphWindow;
    /* main sdata 0x27bd18: {OLMENU 0x1edfd58 "graph_ita_0", sdata 0x27bd10 "hex_0"} (read during the call only) */
    static const char *const names[2] = {"graph_ita_0", "hex_0"};
    /* OLMENU 0x1edfd70 param-name positions, 0x1edfdb0 param-name string ids */
    static const int pos[7][2] = {
        {0x1f0, 0x4a}, {0x1a8, 0x6c}, {0x21e, 0x6c}, {0x1aa, 0x10c}, {0x22d, 0x10c}, {0x1e7, 0x12e}, {0x208, 0x14e},
    };
    static const int ids[7] = {0x19242, 0x19243, 0x19244, 0x19245, 0x19246, 0x19247, 0x19248};
    /* OLMENU 0x1edfdd0 rank-sprite rects, 0x1edfe40 rank uv rects, 0x1edfea0 rank colours (stack copies) */
    struct stMenuRect rects[7] __attribute__((aligned(16))) = {
        {0x1f2, 0x5a, 0x18, 0x12},  {0x1a8, 0x7c, 0x18, 0x12}, {0x23c, 0x7c, 0x18, 0x12}, {0x1a8, 0xfa, 0x18, 0x12},
        {0x23c, 0xfa, 0x18, 0x12}, {0x1f2, 0x11c, 0x18, 0x12}, {0x252, 0x14e, 0x18, 0x12},
    };
    int uv[6][4] __attribute__((aligned(16))) = {
        {0x02, 0x6c, 0x1a, 0x7e}, {0x1c, 0x6c, 0x34, 0x7e}, {0x36, 0x6c, 0x4e, 0x7e},
        {0x50, 0x6c, 0x68, 0x7e}, {0x6a, 0x6c, 0x82, 0x7e}, {0x84, 0x6c, 0x9c, 0x7e},
    };
    unsigned int cols[6] __attribute__((aligned(16))) = {
        0x8046d4ffu, 0x803097ffu, 0x804953e0u, 0x80c1588au, 0x80d08736u, 0x80689768u,
    };
    int i;

    for (i = 0; i < 2; i++)
        g->apWindow[i] = UnitSprite_New(pObj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x41e), (char *)names[i]);
    g->pName = DF_MOBJ_NewStringBase(pObj, 0, 0x194, 0x34, &s_WindowTitleStringColor, 0, 0);
    DF_MOBJ_SetStringID(g->pName, 0, 0x19240, 0);
    for (i = 0; i < 7; i++) {
        g->apParamName[i] = DF_MOBJ_NewStringBase(pObj, 7, pos[i][0], pos[i][1], &s_NormalStringColor, 0, 0);
        DF_MOBJ_SetStringID(g->apParamName[i], 0, ids[i], 0);
    }
    for (i = 0; i < 7; i++) {
        int rank;
        if (i == 6) {
            rank = __GetTotalRank(pObj);
        } else {
            struct struct_dbbd570b *spec = &MPERF_DT(pObj)->AcSpec;
            struct struct_fb2047f5 *t = (struct struct_fb2047f5 *)DF_MDATA_GetData(MenuMng.dataTbl[0], 9);
            int v[6];
            mperf_spec_values(spec, v);
            rank = mperf_rank(v[i], t->aaPointToRank[i]);
        }
        /* 9th argument (blend 1) at the caller's sp+0 in the original */
        g->apParamRank[i] = DF_MOBJ_NXNewSprite(pObj, 0x3f3, &rects[i], uv[rank], (struct struct_76b810ec *)&cols[rank],
                                                0, 0, 0, 1);
    }
}

static void __DeleteParamWindow(struct stMobj *pObj)
{
    PS2_FRAME(0x30);
    struct struct_b4312594 *dt = MPERF_DT(pObj);
    int i;

    for (i = 0; i < 6; i++) {
        struct struct_c17afc46 *it = &dt->ParamWindow.aWindowItem[i];
        UnitSprite_Delete(it->pWindow);
        mperf_delete(&it->pName);
        mperf_delete(&it->pAttackStr);
        mperf_delete(&it->pBulletNumStr);
        mperf_delete(&it->pAttackVal);
        mperf_delete(&it->pBulletNumVal);
    }
}

/* ---- g2: weapon rows, param window, NewSystem ---- */

/* part-param pointer (prm @0xd0 + 8) and the weapon data (DF_MDATA_GetData(mmdp->OTH word @0x50, 1)) of a part */
#define MPERF_PARAM(T, m) (*(T **)((char *)(m) + 0xd8))
#define MPERF_WDATA(m) \
    ((struct PartsParamWeaponSt *)DF_MDATA_GetData(*(void **)((char *)(m)->mmdp + 0x50), 1))

/* hide arm: f_draw = 0 on the row's window and name (one RMW each) */
static inline void mperf_hide_row(struct struct_c17afc46 *it)
{
    it->pWindow->f_draw = 0;
    it->pName->f_draw = 0;
}

static void __SetArmWeaponLData(struct struct_c17afc46 *pItem, struct stMenuAC *pAc, int PartsIndex)
{
    PS2_FRAME(0x80);
    struct _stMModel *part = pAc->real_part[PartsIndex];
    struct PartsParamSwordSt *sw = part ? MPERF_PARAM(struct PartsParamSwordSt, part) : 0;
    struct PartsParamWeaponSt *wp = 0;

    if (part)
        wp = MPERF_WDATA(part);
    if (sw == 0) {
        mperf_hide_row(pItem);
        return;
    }
    switch (sw->type) {
    case 0:
    case 1: {
        PS2_STACK(char[16], buf, 0x60);
        int dmg = sw->blade_dstat.damage;   /* @0x10e, read before the calls */
        DF_MOBJ_SetStringID(pItem->pAttackStr, 0, 0x19242, 0);
        DF_MOBJ_SetStringID(pItem->pBulletNumStr, 0, 0x1924d, 0);
        ps2_sprintf_d(buf, mperf_fmt_d, dmg);
        DF_MOBJ_SetStringCopy(pItem->pAttackVal, buf);
        DF_MOBJ_SetStringCopy(pItem->pBulletNumVal, mperf_str_empty);
        break;
    }
    case 2:
        DF_MOBJ_SetStringID(pItem->pAttackStr, 0, 0x1924e, 0);
        DF_MOBJ_SetStringCopy(pItem->pBulletNumStr, mperf_str_empty);
        DF_MOBJ_SetStringCopy(pItem->pAttackVal, mperf_str_empty);
        DF_MOBJ_SetStringCopy(pItem->pBulletNumVal, mperf_str_empty);
        break;
    case 3: {
        PS2_STACK(char[16], buf, 0x40);
        DF_MOBJ_SetStringID(pItem->pAttackStr, 0, 0x19242, 0);
        DF_MOBJ_SetStringID(pItem->pBulletNumStr, 0, 0x19241, 0);
        ps2_sprintf_d(buf, mperf_fmt_d, wp->dstat.damage);
        DF_MOBJ_SetStringCopy(pItem->pAttackVal, buf);
        ps2_sprintf_d(buf, mperf_fmt_d, wp->blt_sta);
        DF_MOBJ_SetStringCopy(pItem->pBulletNumVal, buf);
        break;
    }
    default:
        break;
    }
}

/* gun-kind rows (R arm weapon / R hanger): kinds 0-2 attack + bullets, kind 3 attack only */
static inline void mperf_gun_row(struct struct_c17afc46 *it, unsigned int kind, struct PartsParamWeaponSt *wd,
                                 char *buf012, char *buf3)
{
    switch (kind) {
    case 0:
    case 1:
    case 2:
        DF_MOBJ_SetStringID(it->pAttackStr, 0, 0x19242, 0);
        DF_MOBJ_SetStringID(it->pBulletNumStr, 0, 0x19241, 0);
        ps2_sprintf_d(buf012, mperf_fmt_d, wd->dstat.damage);
        DF_MOBJ_SetStringCopy(it->pAttackVal, buf012);
        ps2_sprintf_d(buf012, mperf_fmt_d, wd->blt_sta);
        DF_MOBJ_SetStringCopy(it->pBulletNumVal, buf012);
        break;
    case 3: {
        int dmg = wd->dstat.damage;   /* read before the calls */
        DF_MOBJ_SetStringID(it->pAttackStr, 0, 0x19242, 0);
        DF_MOBJ_SetStringID(it->pBulletNumStr, 0, 0x1924d, 0);
        ps2_sprintf_d(buf3, mperf_fmt_d, dmg);
        DF_MOBJ_SetStringCopy(it->pAttackVal, buf3);
        DF_MOBJ_SetStringCopy(it->pBulletNumVal, mperf_str_empty);
        break;
    }
    default:
        break;
    }
}

static void __SetSpareWeaponData(struct stMobj *pObj)
{
    PS2_FRAME(0x90);
    struct struct_b4312594 *dt = MPERF_DT(pObj);
    struct struct_c17afc46 *items = dt->ParamWindow.aWindowItem;
    struct _stMModel *part = dt->pAc->real_part[0x12];   /* R hanger */
    struct PartsParamGunSt *gun = part ? MPERF_PARAM(struct PartsParamGunSt, part) : 0;
    struct PartsParamWeaponSt *wd = part ? MPERF_WDATA(part) : 0;
    PS2_STACK(char[16], buf012, 0x70);
    PS2_STACK(char[16], buf3, 0x50);

    if (gun == 0)
        mperf_hide_row(&items[4]);
    else
        mperf_gun_row(&items[4], *((unsigned char *)gun + 0x100), wd, buf012, buf3);   /* PartsParamGunSt type */
    __SetArmWeaponLData(&items[5], dt->pAc, 0x13);   /* L hanger */
}

static void __SetArmWeaponData(struct stMobj *pObj)
{
    PS2_FRAME(0x110);
    struct struct_b4312594 *dt = MPERF_DT(pObj);
    struct struct_c17afc46 *items = dt->ParamWindow.aWindowItem;
    struct stMenuAC *pAc = dt->pAc;
    struct _stMModel *arm = pAc->real_part[2];
    /* arms are mandatory; a NULL arm would read PS2 address 0x108 (quirk) */
    struct PartsParamArmsSt *ap = arm ? MPERF_PARAM(struct PartsParamArmsSt, arm)
                                      : (struct PartsParamArmsSt *)PS2_NULL_MEM();
    struct PartsParamWeaponSt *wd;

    if (ap->type == 0) {
        struct _stMModel *w = pAc->real_part[0x10];   /* R arm weapon */
        struct PartsParamGunSt *gun = w ? MPERF_PARAM(struct PartsParamGunSt, w) : 0;
        PS2_STACK(char[16], buf012, 0x70);
        PS2_STACK(char[16], buf3, 0x50);
        wd = w ? MPERF_WDATA(w) : 0;
        if (gun == 0)
            mperf_hide_row(&items[2]);
        else
            mperf_gun_row(&items[2], *((unsigned char *)gun + 0x100), wd, buf012, buf3);
        __SetArmWeaponLData(&items[3], pAc, 0x11);   /* L arm weapon */
        return;
    }

    /* arm with a built-in weapon (type 1) / W-blade arm (type 2): both arm rows show the arm's data */
    wd = arm ? MPERF_WDATA(arm) : 0;
    if (ap->type == 2) {
        PS2_STACK(char[16], bufR, 0xb0);
        PS2_STACK(char[16], bufL, 0x90);
        int dmg = ap->blade_dstat.damage;   /* @0x132 */
        DF_MOBJ_SetStringID(items[2].pAttackStr, 0, 0x19242, 0);
        DF_MOBJ_SetStringID(items[2].pBulletNumStr, 0, 0x1924d, 0);
        ps2_sprintf_d(bufR, mperf_fmt_d, dmg);
        DF_MOBJ_SetStringCopy(items[2].pAttackVal, bufR);
        DF_MOBJ_SetStringCopy(items[2].pBulletNumVal, mperf_str_empty);
        dmg = ap->blade_dstat.damage;   /* re-read */
        DF_MOBJ_SetStringID(items[3].pAttackStr, 0, 0x19242, 0);
        DF_MOBJ_SetStringID(items[3].pBulletNumStr, 0, 0x1924d, 0);
        ps2_sprintf_d(bufL, mperf_fmt_d, dmg);
        DF_MOBJ_SetStringCopy(items[3].pAttackVal, bufL);
        DF_MOBJ_SetStringCopy(items[3].pBulletNumVal, mperf_str_empty);
    } else if (ap->type == 1) {
        PS2_STACK(char[16], bufR, 0xf0);
        PS2_STACK(char[16], bufL, 0xd0);
        DF_MOBJ_SetStringID(items[2].pAttackStr, 0, 0x19242, 0);
        DF_MOBJ_SetStringID(items[2].pBulletNumStr, 0, 0x19241, 0);
        ps2_sprintf_d(bufR, mperf_fmt_d, wd->dstat.damage);
        DF_MOBJ_SetStringCopy(items[2].pAttackVal, bufR);
        ps2_sprintf_d(bufR, mperf_fmt_d, wd->blt_sta);
        DF_MOBJ_SetStringCopy(items[2].pBulletNumVal, bufR);
        DF_MOBJ_SetStringID(items[3].pAttackStr, 0, 0x19242, 0);
        DF_MOBJ_SetStringID(items[3].pBulletNumStr, 0, 0x19241, 0);
        ps2_sprintf_d(bufL, mperf_fmt_d, wd->dstat.damage);
        DF_MOBJ_SetStringCopy(items[3].pAttackVal, bufL);
        ps2_sprintf_d(bufL, mperf_fmt_d, wd->blt_sta);
        DF_MOBJ_SetStringCopy(items[3].pBulletNumVal, bufL);
    }
}

static void __SetBackWeaponData(struct stMobj *pObj)
{
    PS2_FRAME(0x100);
    struct struct_b4312594 *dt = MPERF_DT(pObj);
    struct struct_c17afc46 *items = dt->ParamWindow.aWindowItem;
    struct stMenuAC *pAc = dt->pAc;
    /* main sdata 0x27bd00 row index {0, 1}, 0x27bd08 back part slots {0xe, 0xf} */
    static const int idx[2] = {0, 1};
    static const int slot[2] = {0xe, 0xf};
    /* OLMENU 0x1edfd40 label id per back type, copied to sp+0xb0; indexed by type unchecked (quirk) */
    PS2_STACK(int[6], tbl, 0xb0);
    PS2_STACK(char[16], buf0, 0x90);
    PS2_STACK(char[16], buf34, 0xd0);
    int i;

    tbl[0] = 0x19242;
    tbl[1] = 0x1924f;
    tbl[2] = 0x19251;
    tbl[3] = 0x19250;
    tbl[4] = 0x19250;
    tbl[5] = 0x19252;
    for (i = 0; i < 2; i++) {
        struct _stMModel *w = pAc->real_part[slot[i]];
        struct struct_c17afc46 *row = &items[idx[i]];
        struct PartsParamBackSt *p;
        struct PartsParamWeaponSt *wd;

        if (w == 0) {
            mperf_hide_row(row);
            continue;
        }
        p = MPERF_PARAM(struct PartsParamBackSt, w);
        wd = MPERF_WDATA(w);
        if (p->type == 0) {
            DF_MOBJ_SetStringID(row->pAttackStr, 0, 0x19242, 0);
            DF_MOBJ_SetStringID(row->pBulletNumStr, 0, 0x19241, 0);
            ps2_sprintf_d(buf0, mperf_fmt_d, wd->dstat.damage);
            DF_MOBJ_SetStringCopy(row->pAttackVal, buf0);
            ps2_sprintf_d(buf0, mperf_fmt_d, wd->blt_sta);
            DF_MOBJ_SetStringCopy(row->pBulletNumVal, buf0);
        } else {
            DF_MOBJ_SetStringID(row->pAttackStr, 0, tbl[p->type], 0);
            DF_MOBJ_SetStringCopy(row->pAttackVal, mperf_str_empty);
            if (p->type == 3 || p->type == 4) {   /* magazine units */
                DF_MOBJ_SetStringID(row->pBulletNumStr, 0, 0x19241, 0);
                ps2_sprintf_d(buf34, mperf_fmt_d, p->magazine_add_rifle + p->magazine_add_mgun);
                DF_MOBJ_SetStringCopy(row->pBulletNumVal, buf34);
            } else {
                DF_MOBJ_SetStringCopy(row->pBulletNumStr, mperf_str_empty);
                DF_MOBJ_SetStringCopy(row->pBulletNumVal, mperf_str_empty);
            }
        }
    }
}

static void __CreateParamWindow(struct stMobj *pObj)
{
    PS2_FRAME(0x1e0);
    struct struct_c17afc46 *it = MPERF_DT(pObj)->ParamWindow.aWindowItem;
    /* OLMENU 0x1edfc10 window names (strings 0x1edfba8..), read during UnitSprite_New only */
    static const char *const names[6] = {"back_unit_0", "back_unit_1", "arm_unit_0", "arm_unit_1",
                                         "hanger_unit_0", "hanger_unit_1"};
    /* OLMENU 0x1edfc30 name, 0x1edfc60 attack label, 0x1edfc90 bullet label, 0x1edfcc0 attack value,
     * 0x1edfcf0 bullet value positions; 0x1edfd20 unit-name string ids */
    static const int namePos[6][2] = {{24, 64}, {268, 64}, {24, 166}, {280, 166}, {24, 268}, {244, 268}};
    static const int atkPos[6][2] = {{24, 76}, {304, 76}, {24, 178}, {304, 178}, {24, 280}, {304, 280}};
    static const int bltPos[6][2] = {{24, 94}, {304, 94}, {24, 196}, {304, 196}, {24, 298}, {304, 298}};
    static const int atkValPos[6][2] = {{94, 76}, {374, 76}, {94, 178}, {374, 178}, {94, 280}, {374, 280}};
    static const int bltValPos[6][2] = {{94, 94}, {374, 94}, {94, 196}, {374, 196}, {94, 298}, {374, 298}};
    static const int nameId[6] = {0x1923a, 0x1923b, 0x1923c, 0x1923d, 0x1923e, 0x1923f};
    int i;

    for (i = 0; i < 6; i++, it++) {
        it->pWindow = UnitSprite_New(pObj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x41e), (char *)names[i]);
        it->pName = DF_MOBJ_NewStringBase(pObj, 0, namePos[i][0], namePos[i][1], &s_WindowTitleStringColor, 0, 0);
        it->pAttackStr = DF_MOBJ_NewStringBase(pObj, 7, atkPos[i][0], atkPos[i][1], &s_NormalStringColor, 0, 0);
        it->pBulletNumStr = DF_MOBJ_NewStringBase(pObj, 7, bltPos[i][0], bltPos[i][1], &s_NormalStringColor, 0, 0);
        it->pAttackVal = DF_MOBJ_NewStringBase(pObj, 7, atkValPos[i][0] + 8, atkValPos[i][1], &s_NormalStringColor, 0,
                                               0x10000);
        it->pBulletNumVal = DF_MOBJ_NewStringBase(pObj, 7, bltValPos[i][0] + 8, bltValPos[i][1], &s_NormalStringColor,
                                                  0, 0x10000);
        DF_MOBJ_SetStringID(it->pName, 0, nameId[i], 0);
    }
    __SetBackWeaponData(pObj);
    __SetArmWeaponData(pObj);
    __SetSpareWeaponData(pObj);
}

/* DWARF (pParentObj, PlayerNo, pAc); the asm reads PlayerNo from a2, pAc from a3 (protofix adds the unused a1;
 * the caller's t0 = 0 is never read either) */
struct stMobj *DF_MPERF_NewSystem(struct stMobj *pParentObj, int unused, int PlayerNo, struct stMenuAC *pAc)
{
    PS2_FRAME(0xa0);
    struct stMobj *obj = DF_MOBJ_GetNewObject(pParentObj);
    struct struct_b4312594 *dt;
    PS2_STACK(char[16], acbuf, 0x80);
    PS2_STACK(char[16], apbuf, 0x70);
    int n;
    (void)unused;

    if (obj == 0)
        return 0;
    MPERF_DT(obj) = (struct struct_b4312594 *)DF_MMM_Alloc(0, 0x1b0);   /* 0x1a0 struct, not zeroed */
    obj->type = 0;
    obj->stat = 1;
    obj->play_func = (void (*)(void))GFN(__Play);
    obj->draw_func = (void (*)(void))GFN(__Draw);
    obj->count = 0;
    dt = MPERF_DT(obj);
    dt->pAc = pAc;
    dt->PlayerNo = PlayerNo;
    dt->ProcMode = 0;
    DF_MACSPEC_Get(&dt->AcSpec, pAc, 0);

    dt = MPERF_DT(obj);
    dt->AcNameWindow.pWindow = UnitSprite_New(obj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x41e), (char *)"ac_name_0");
    dt->AcNameWindow.pAcNumber = DF_MOBJ_NewStringBase(obj, 0, 0x18, 0x22, &s_WindowTitleStringColor, 0, 0);
    dt->AcNameWindow.pAcName = DF_MOBJ_NewStringBase(obj, 7, 0x44, 0x22, &s_NormalStringColor, 0, 0);
    n = (signed char)PlayerInfo_GetCurrentAcNo(dt->PlayerNo);
    ps2_sprintf_d(acbuf, mperf_fmt_ac, n + 1);
    DF_MOBJ_SetStringCopy(dt->AcNameWindow.pAcNumber, acbuf);
    DF_MOBJ_SetStringCopy(dt->AcNameWindow.pAcName, PlayerInfo_GetAcName(dt->PlayerNo, (char)n));
    __CreateParamWindow(obj);
    __CreateGraphWindow(obj);

    dt = MPERF_DT(obj);
    /* main sdata 0x27bd28 "ap&c_0" (read during the call only) */
    dt->ApWindow.pWindow = UnitSprite_New(obj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x41e), (char *)"ap&c_0");
    dt->ApWindow.pName = DF_MOBJ_NewStringBase(obj, 7, 0x1b8, 0x16a, &s_NormalStringColor, 0, 0);
    dt->ApWindow.pAp = DF_MOBJ_NewStringBase(obj, 7, 0x268, 0x16a, &s_NormalStringColor, 0, 0x10000);
    DF_MOBJ_SetStringID(dt->ApWindow.pName, 0, 0x19249, 0);
    ps2_sprintf_d(apbuf, mperf_fmt_d, dt->AcSpec.ap);
    DF_MOBJ_SetStringCopy(dt->ApWindow.pAp, apbuf);

    /* emblem: OLMENU 0x1edfec0 rect (sp+0x60), 0x1edfed0 uv (sp+0x50); EmblemTM2[PlayerNo] unchecked */
    {
        struct stMenuRect rect __attribute__((aligned(16))) = {0x13c, 0x148, 0x48, 0x48};
        int uv[4] __attribute__((aligned(16))) = {0, 0, 0x80, 0x80};
        dt = MPERF_DT(obj);
        dt->EmblemWindow.TextureId = MenuTex_RegisterSingleTex((struct struct_c53f69de *)EmblemTM2[dt->PlayerNo]);
        /* 9th argument (blend 0) at the caller's sp+0 in the original */
        dt->EmblemWindow.pWindow = DF_MOBJ_NXNewSprite(obj, dt->EmblemWindow.TextureId, &rect, uv, 0, 0, 0, 1, 0);
    }
    return obj;
}
