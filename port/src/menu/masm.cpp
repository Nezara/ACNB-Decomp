/* src/menu/masm.cpp — the AC assembly ("ASM", garage) screen of the OLMENU overlay. DF_MASM_NewSystem builds one stMobj
 * whose dt is a struct_2c7da168 ("p_asmp") and whose play_func walks the screen: fade in -> __AsmSys_Play (pad handling:
 * part up / down, category change, equip / reposit / tune, dialogs, help) -> one of the transition functions
 * (PlayChanging / PlayChangingDisp, PlayDialog, PlayFadeOut, the tune sub-screen PlayFadeOutForTune / PlayDispatchTune)
 * and back. Sub-objects: category / part name strings, model window, MASMP gene window, MASMD detail window, the AC spec
 * window (__MASM_NewACSpecWin; byte obj+0x7c = 1 asks it to delete itself), the alert window manager and four blinking
 * arrows (psArrowBuf). Sub-objects are torn down by an inline "delete" (f_draw = 0, f_erase = 1, pointer = NULL).
 * Native so far: group 1 (UpdateTunedStr, the tune family CheckTune / SetFadeInFromTune / PlayFadeOutForTune /
 * EnterTune / PlayDispatchTune, __PlayOpenDialog, DF_MASM_GetSubType, pkt / draw functions, PlayChangingDisp,
 * PlayFadeOut), group 2 (PlayDialog, CheckEquip, PlayChanging) and group 3 (Change / NewDetailWin, ChangeDisp,
 * ChangePart, OnPartsListModeChange, OnReposit) and group 4 (Play, SetFadeOut, PlayFadeIn, CheckAlart,
 * Change / CreateGeneWin2) and group 5 (DF_MASM_NewSystem, the AC spec window PlayACSpecWin / SetACSpecInfo /
 * NewACSpecWin, ChangeCategoryStr, the alert window manager PlayAlartWinMng / SetAlartWinMngItems / PlayAlartWin and
 * ArrowBlink): the whole file is native. */
#include "acnb/nodwarf.h"
#include "acnb/pf.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

#ifdef ACNB_SOURCEPORT
char masm_tune_d0[8] = "tuned_0";
char masm_fmt_tuned[8] = "%s%3d%%";
char masm_fmt_ss[8] = "%s%s";
char masm_spec_line[25] = "________________________";
#endif

typedef struct struct_2c7da168 AsmSys;   /* assembly screen (dt) */

static inline AsmSys *masm_dt(struct stMobj *o) { return (AsmSys *)&o->dt; }
/* dt viewed as words (sub-window dt layouts that have no DWARF struct here) */
static inline struct stMobj **masm_dtw(struct stMobj *o) { return (struct stMobj **)&o->dt; }

static inline void masm_erase(struct stMobj *p)
{
    p->f_draw = 0;
    p->f_erase = 1;
}

/* AC spec window: byte obj+0x7c = 1 is the delete request __MASM_PlayACSpecWin polls */
static inline void masm_spec_close(struct stMobj *w) { ((unsigned char *)w)[0x7c] = 1; }

static inline void masm_spec_fadein(struct stMobj *w)
{
    struct stMobj **d = masm_dtw(w);
    int i;
    DF_MOBJ_SetFadeIn(d[0x58 / 4]);
    DF_MOBJ_SetFadeIn(d[0x5c / 4]);
    DF_MOBJ_SetFadeIn(d[0x54 / 4]);
    for (i = 0; i < 17; i++) DF_MOBJ2_SetItemFadeIn(d[4 + i]);
    w->stat = 1;
}

static inline void masm_spec_fadeout(struct stMobj *w)
{
    struct stMobj **d = masm_dtw(w);
    int i;
    DF_MOBJ_SetFadeOut(d[0x58 / 4]);
    DF_MOBJ_SetFadeOut(d[0x5c / 4]);
    DF_MOBJ_SetFadeOut(d[0x54 / 4]);
    for (i = 0; i < 17; i++) DF_MOBJ2_SetItemFadeOut(d[4 + i]);
    w->stat = 2;
}

/* psArrowBuf[3] alive and not fading out / deleted -> blink the arrows */
static inline void masm_arrow_blink(void)
{
    struct stMobj *a = psArrowBuf[3];
    if (a && a->stat != 2 && a->stat != 5) __ArrowBlink();
}

static void __AsmSys_UpdateTunedStr(struct stMobj *pMobj)
{
    PS2_FRAME(0x130);
    PS2_STACK(char[256], buff, 0x30);
    AsmSys *dt = masm_dt(pMobj);
    unsigned char rate;
    int m;

    if (dt->pTuned) {
        masm_erase(dt->pTuned);
        dt->pTuned = 0;
    }
    m = dt->part_id % 1000;
    if (m == 0) return;
    rate = PlayerInfo_GetPartsTuneRate(dt->player, dt->kind_idx,
                                       DF_MPART_Index2TrueID(dt->kind_tbl[(int)dt->kind_idx], m));
    if (rate == 0) return;
    dt->pTuned = DF_MOBJ_NewStringBaseFromLayoutName(dt->pStrFence, (char *)LAYOUT_GetLayoutData(0x3f2), masm_tune_d0,
                                                     0, 0, 0);
    pf_sprintf_sd(buff, masm_fmt_tuned, (const char *)DF_MSTR_GetName(MenuMng.language, 0, 0x186c7), rate);
    DF_MOBJ_SetStringCopy(dt->pTuned, buff);
    dt->pTuned->type = 9;
    DF_MOBJ_SetFadeIn(dt->pTuned);
}

/* queue one refusal dialog and switch to PlayDialog (inlined three times in the original) */
static inline void masm_tune_refuse(struct stMobj *o, AsmSys *dt, int id)
{
    dt->dlg_num = 0;
    dt->dlg_idx = 0;
    if (dt->dlg_num < 4) dt->dlg_id[(int)dt->dlg_num++] = id;
    o->count = 0;
    o->play_func = (void (*)(void))GFN(__AsmSys_PlayDialog);
    Voice_Call(0x3f1, 0x7f);
}

static int __AsmSys_CheckTune(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    AsmSys *dt = masm_dt(ioObjp);
    int k;

    if (dt->disp_kind != 0) return 0;
    if (dt->part_id % 1000 == 0) return 0;
    k = dt->kind_idx;
    /* jump table 0x1ee4de0: kinds 0-7 tunable except 5 (FCS); out of range (incl. negative) not tunable */
    if (!((unsigned int)k < 8 && k != 5)) {
        masm_tune_refuse(ioObjp, dt, 0x30ddd);
        return 0;
    }
    if (dt->kind_tbl[k] == 2 && DF_MPART_GetCategory(dt->part_id) == 7) {
        masm_tune_refuse(ioObjp, dt, 0x30ddd);
        return 0;
    }
    if (DF_MPART_IsInitialPartIdx((eAcPartsEquip)(unsigned char)dt->kind_idx, dt->part_id) != 0) {
        masm_tune_refuse(ioObjp, dt, 0x30ddc);
        return 0;
    }
    return 1;
}

static void __AsmSys_SetFadeInFromTune(struct stMobj *ioObjp)
{
    PS2_FRAME(0x70);
    AsmSys *dt = masm_dt(ioObjp);
    struct stMobj *w;
    int i, n;

    WindowEx_SetFadeIn(dt->pACSpecWin);
    UnitSprite_SetFadeIn(dt->pACWakuWin);
    UnitSprite_SetFadeIn(dt->pPartsWakuWin);
    UnitSprite_SetFadeIn(dt->pPartsWakuWinInner);
    UnitSprite_SetFadeIn(dt->pACNameWin);
    WindowEx_SetFadeIn(dt->pPartsSpecWin);
    WindowEx_SetFadeIn(dt->pPartsNameWin);
    for (i = 0; i < 4; i++) DF_MOBJ_SetFadeIn(psArrowBuf[i]);
    if (dt->f_equip) UnitSprite_SetFadeIn(dt->pNowEq);
    if (dt->f_reposit) UnitSprite_SetFadeIn(dt->pNowRepo);
    if (dt->cMaker != dt->nCompanyIcon) {
        n = dt->nCompanyIcon;
        if (n >= 0 && n < 3) UnitSprite_SetFadeOut(dt->apCompanyIcon[n]);
        dt->nCompanyIcon = dt->cMaker;
        n = dt->nCompanyIcon;
        if (n >= 0 && n < 3) {
            DF_MOBJ_SetDrawEnable(dt->apCompanyIcon[n], 1);
            UnitSprite_SetFadeIn(dt->apCompanyIcon[(int)dt->nCompanyIcon]);
        }
    }
    DF_MOBJ_SetFadeIn(dt->pname_strp);
    DF_MOBJ_SetFadeIn(dt->pAcName);
    DF_MOBJ_SetFadeIn(dt->pAcNo);
    __AsmSys_UpdateTunedStr(ioObjp);
    M_AC_UpdateTuneStat(dt->acp, 0);
    M_AC_UpdateTuneStat(dt->acp, 1);
    DF_M_AC_CheckEquipFlag(dt->acp, dt->acp->flag.tmp_connect != 0);
    __AsmSys_CheckAlart(ioObjp);
    w = dt->awmngp;
    if (w) {
        struct stMobj **items = masm_dtw(w);
        w->f_draw = 1;
        for (i = 0; i < 6; i++) {
            struct stMobj *p = items[i];
            if (p) {
                UnitSprite_SetFadeIn(masm_dtw(p)[0]);
                p->stat = 1;
                p->count = 0;
            }
        }
        w->stat = 1;
    }
    __AsmSys_CreateGeneWin2(ioObjp, 1);
    if (!dt->f_equip) __AsmSys_ChangeGeneWin2(ioObjp, 0, 0);
    DF_MASMD_SetDetailWinFadeIn(dt->detailwinp);
    if (dt->ac_specwinp) masm_spec_close(dt->ac_specwinp);
    dt->ac_specwinp = 0;
    /* original: the same field tested again right after it was nulled (dead; probably meant ac_specwinfop) */
    if (dt->ac_specwinp) masm_spec_close(dt->ac_specwinp);
    dt->ac_specwinp = __MASM_NewACSpecWin(dt->pACSpecWin, M_ACp[(int)dt->player][0], 0, 0);
    masm_spec_fadein(dt->ac_specwinp);
    DF_MVIEW_MotionStart(MViewp, 0x14);
    ioObjp->stat = 1;
    ioObjp->play_func = (void (*)(void))GFN(__AsmSys_PlayFadeIn);
    ioObjp->count = 0;
}

static void __AsmSys_PlayFadeOutForTune(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    AsmSys *dt = masm_dt(ioObjp);
    int c = ioObjp->count;

    ioObjp->count = c + 1;
    if (c < 6) return;
    DF_MASMP_DeleteGeneWin(dt->genewinp);
    dt->genewinp = 0;
    ioObjp->stat = 3;
    ioObjp->play_func = (void (*)(void))GFN(__AsmSys_PlayDispatchTune);
    ioObjp->count = 0;
}

static void __AsmSys_EnterTune(struct stMobj *pInObj)
{
    PS2_FRAME(0x70);
    AsmSys *dt = masm_dt(pInObj);
    void *pParts = 0;
    struct StAcAsmTune *pTune;
    struct stMobj *w;
    int i, n, tid;

    if (dt->part_id % 1000 != 0) {
        int idx = DF_M_AC_PartID2IDX(dt->part_id);
        struct _stMModel *m;
        if (dt->kind_idx == 0xb) idx += 1;
        m = dt->acp->cur_part[idx];
        if (m) pParts = m->prm.part.infop;
    }
    /* original: the tune record comes from M_ACp[player][0], not dt->acp */
    pTune = &M_ACp[(int)dt->player][0]->cur_tune;
    tid = DF_MPART_Index2TrueID(dt->kind_tbl[(int)dt->kind_idx], dt->part_id);
    dt->pChild = MenuTune_New(pInObj, pParts, pTune, dt->kind_idx, dt->player, tid);
    WindowEx_SetFadeOut(dt->pACSpecWin);
    UnitSprite_SetFadeOut(dt->pACWakuWin);
    UnitSprite_SetFadeOut(dt->pPartsWakuWin);
    UnitSprite_SetFadeOut(dt->pPartsWakuWinInner);
    UnitSprite_SetFadeOut(dt->pACNameWin);
    WindowEx_SetFadeOut(dt->pPartsSpecWin);
    WindowEx_SetFadeOut(dt->pPartsNameWin);
    for (i = 0; i < 4; i++) DF_MOBJ_SetFadeOut(psArrowBuf[i]);
    UnitSprite_SetFadeOut(dt->pNowEq);
    if (dt->f_reposit) UnitSprite_SetFadeOut(dt->pNowRepo);
    if (dt->nCompanyIcon != -1) {
        n = dt->nCompanyIcon;
        if (n >= 0 && n < 3) UnitSprite_SetFadeOut(dt->apCompanyIcon[n]);
        dt->nCompanyIcon = -1;
        /* original: dead re-test (-1 is never 0..2) */
        n = dt->nCompanyIcon;
        if (n >= 0 && n < 3) {
            DF_MOBJ_SetDrawEnable(dt->apCompanyIcon[n], 1);
            UnitSprite_SetFadeIn(dt->apCompanyIcon[(int)dt->nCompanyIcon]);
        }
    }
    DF_MOBJ_SetFadeOut(dt->pTuned);
    w = dt->awmngp;
    if (w) {
        struct stMobj **items = masm_dtw(w);
        for (i = 0; i < 6; i++) {
            struct stMobj *p = items[i];
            if (p) {
                UnitSprite_SetFadeOut(masm_dtw(p)[0]);
                p->stat = 2;
                p->count = 0;
            }
        }
        w->stat = 2;
    }
    DF_MOBJ_SetFadeOut(dt->pname_strp);
    DF_MASMP_SetGeneWinFadeOut(dt->genewinp);
    DF_MASMD_SetDetailWinFadeOut(dt->detailwinp);
    if (dt->ac_specwinp) masm_spec_fadeout(dt->ac_specwinp);
    DF_MOBJ_SetFadeOut(dt->pAcName);
    DF_MOBJ_SetFadeOut(dt->pAcNo);
    DF_MOBJ_SetFadeOut(dt->pname_strp);   /* original: pname_strp faded out a second time */
    pInObj->stat = 2;
    pInObj->play_func = (void (*)(void))GFN(__AsmSys_PlayFadeOutForTune);
    pInObj->count = 0;
}

static void __AsmSys_PlayDispatchTune(struct stMobj *pInObj)
{
    PS2_FRAME(0x70);
    AsmSys *dt = masm_dt(pInObj);
    struct stMobj *c = dt->pChild;
    unsigned char lv[5];
    unsigned char tp;
    int i, m;

    /* original: pChild->stat is read before the NULL test */
    if ((c ? c : (struct stMobj *)PS2_NULL_MEM())->stat != 5) return;
    if (dt->pChild) {
        masm_erase(dt->pChild);
        dt->pChild = 0;
    }
    tp = (unsigned char)M_AC_eAcPartsEquip_to_eAcTuneParts((eAcPartsEquip)(unsigned char)dt->kind_idx);
    for (i = 0; i < 5; i++) lv[i] = M_ACp[(int)dt->player][0]->cur_tune.parts[tp].lv[i];
    m = dt->part_id % 1000;
    for (i = 0; i < 5; i++)
        PlayerInfo_SetPartsTuneLv(dt->player, dt->kind_idx,
                                  DF_MPART_Index2TrueID(dt->kind_tbl[(int)dt->kind_idx], m), i, lv[i]);
    __AsmSys_SetFadeInFromTune(pInObj);
}

static void __PlayOpenDialog(struct stMobj *ioObjp)
{
    PS2_FRAME(0x20);
    if ((DF_MDIALOG_Sync() & 4) != 4) return;
    if (MenuMng.stat == 0xc) {
        __AsmSys_SetFadeOut(ioObjp);
    } else {
        DF_MBGUIDE_Pop();
        ioObjp->play_func = (void (*)(void))GFN(__AsmSys_Play);
    }
}

/* inType = eAcPartsEquip, inDatap = the part's parameter record, inWepp = the weapon record (not NULL-tested for
 * types 8-0xc in the original) */
int DF_MASM_GetSubType(int inType, void *inDatap, void *inWepp)
{
    const unsigned char *d = (const unsigned char *)inDatap;
    const unsigned char *w = inWepp ? (const unsigned char *)inWepp : (const unsigned char *)PS2_NULL_MEM();
    int wflag, v;

    switch (inType) {
    case 0:
        return (d[0x10f] & 1) ? 1 : 0;
    case 1:
        v = d[0x114];
        if (v == 1) return 0;
        return v == 0 ? 1 : 2;
    case 2:
        v = d[0x108];
        if (inWepp && (*(const unsigned short *)(w + 0x18) & 0x20)) v += 10;
        return v;
    case 8:
        wflag = *(const unsigned short *)(w + 0x18) & 0x20;
        v = d[0x100];
        if (wflag) v += 10;
        return v;
    case 9:
        v = d[0x100];
        if (v == 2 || v == 3) {
            if (*(const unsigned short *)(w + 0x18) & 0x20) v += 10;
            return v;
        }
        if (v == 1 || v == 5 || v == 7) return 0;
        return v;
    case 0xa:
        wflag = *(const unsigned short *)(w + 0x18) & 0x20;
        v = d[0x101];
        if (wflag) v += 10;
        if (w[2] != 0) v += 100;
        return v;
    case 0xb:
        v = (w[0] == 4) ? 1 : 0;
        if (*(const unsigned short *)(w + 0x18) & 0x20) v += 10;
        if (w[2] != 0) v += 100;
        return v;
    case 0xc:
        wflag = *(const unsigned short *)(w + 0x18) & 0x20;
        v = d[0x100];
        if (wflag) v += 10;
        if (w[2] != 0) v += 100;
        return v;
    default:
        return 0;
    }
}

static void __MakeVu1PacketAssem(void)
{
    PS2_FRAME(0x10);
    MPktCurrentp = DF_MPKT_MakeVIFCode(MPktCurrentp, 0, 0, 0, 0x13000000);
}

static void __AsmSys_Draw(void)
{
}

static void __AsmSys_PlayChangingDisp(struct stMobj *ioObjp)
{
    PS2_FRAME(0x60);
    AsmSys *dt = masm_dt(ioObjp);

    masm_arrow_blink();
    if (dt->disp_kind == 0) {
        /* parts side: detail window out, gene window + new spec window in */
        if (dt->step == 0) {
            int i;
            if (dt->detailwinp->stat != 5) return;
            DF_MASMD_DeleteDetailWin(dt->detailwinp);
            dt->detailwinp = 0;
            if (dt->ac_specwinfop) masm_spec_close(dt->ac_specwinfop);
            dt->ac_specwinfop = 0;
            if (dt->ac_specwinp) masm_spec_close(dt->ac_specwinp);
            dt->ac_specwinp = __MASM_NewACSpecWin(dt->pACSpecWin, M_ACp[(int)dt->player][0], 0, dt->disp_kind);
            masm_spec_fadein(dt->ac_specwinp);
            dt->step += 1;
            i = DF_M_AC_EquipIDX2IDX(dt->kind_idx);
            if (dt->acp->real_part[i] == 0) {
                __AsmSys_CreateGeneWin2(ioObjp, 0);
            } else {
                __AsmSys_CreateGeneWin2(ioObjp, 1);
                if (!dt->f_equip) __AsmSys_ChangeGeneWin2(ioObjp, 0, 0);
            }
            DF_MASMP_SetGeneWinFadeIn(dt->genewinp);
        } else if (dt->step == 1) {
            /* original: ac_specwinp dereferenced unchecked (created in step 0) */
            if (dt->ac_specwinp->stat == 0) dt->step = dt->step + 1;
        } else {
            ioObjp->stat = 0;
            ioObjp->play_func = (void (*)(void))GFN(__AsmSys_Play);
        }
    } else if (dt->disp_kind == 1) {
        /* gene / detail side: gene window out, detail window + new spec window in */
        if (dt->step == 2) {
            ioObjp->stat = 0;
            ioObjp->play_func = (void (*)(void))GFN(__AsmSys_Play);
        } else if (dt->step == 1) {
            if (dt->detailwinp->stat != 0) return;
            if (dt->ac_specwinfop) masm_spec_close(dt->ac_specwinfop);
            dt->ac_specwinfop = 0;
            ioObjp->stat = 0;
            ioObjp->play_func = (void (*)(void))GFN(__AsmSys_Play);
            dt->step += 1;
        } else if (dt->step == 0) {
            if (dt->genewinp->stat != 5) return;
            DF_MASMP_DeleteGeneWin(dt->genewinp);
            dt->genewinp = 0;
            if (dt->f_equip) {
                __AsmSys_NewDetailWin(ioObjp, 1, 0);
            } else {
                __AsmSys_NewDetailWin(ioObjp, 1, 1);
                __AsmSys_ChangeDetailWin(ioObjp, 0, 2);
            }
            if (dt->ac_specwinp) masm_spec_close(dt->ac_specwinp);
            dt->ac_specwinp = __MASM_NewACSpecWin(dt->pACSpecWin, M_ACp[(int)dt->player][0], 0, dt->disp_kind);
            masm_spec_fadein(dt->ac_specwinp);
            dt->step += 1;
        }
    }
}

/* inline delete of a dt string / sprite object */
static inline void masm_delete(struct stMobj **pp)
{
    if (*pp) {
        masm_erase(*pp);
        *pp = 0;
    }
}

static void __AsmSys_PlayFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    AsmSys *dt = masm_dt(ioObjp);
    int c, i;

    masm_arrow_blink();
    c = ioObjp->count;
    ioObjp->count = c + 1;
    if (c < 5) return;
    if (dt->mdlwinp->stat != 5) return;
    if (DF_MVIEW_MotionSync(MViewp) == 0) return;
    masm_delete(&dt->ctgr_strp);
    masm_delete(&dt->pname_strp);
    masm_delete(&dt->vstrp);
    masm_delete(&dt->fo_ctgr_strp);
    masm_delete(&dt->fo_pname_strp);
    masm_delete(&dt->pAcName);
    masm_delete(&dt->pAcNo);
    masm_delete(&dt->pTuned);
    dt->pStrFence = 0;
    UnitSprite_Delete(dt->pNowEq);
    UnitSprite_Delete(dt->pNowRepo);
    for (i = 0; i < 4; i++) masm_delete(&psArrowBuf[i]);
    DF_MOBJ2_DeleteMdlWin(dt->mdlwinp);
    if (dt->ac_specwinp) masm_spec_close(dt->ac_specwinp);
    if (dt->ac_specwinfop) masm_spec_close(dt->ac_specwinfop);
    if (dt->genewinp) DF_MASMP_DeleteGeneWin(dt->genewinp);
    DF_MASMD_DeleteDetailWin(dt->detailwinp);
    DF_MHELP_DeleteList(dt->hlist);
    DF_MMM_Free(1, dt->id_tablep);
    dt->id_tablep = 0;
    DF_MMM_Free(2, dt->pname);
    DF_MMM_Free(2, dt->fo_pname);
    UnitSprite_Delete(dt->pPartsWakuWin);
    UnitSprite_Delete(dt->pPartsWakuWinInner);
    UnitSprite_Delete(dt->pACNameWin);
    UnitSprite_Delete(dt->pACWakuWin);
    WindowEx_Delete(dt->pPartsSpecWin);
    WindowEx_Delete(dt->pACSpecWin);
    WindowEx_Delete(dt->pCategoryWin);
    WindowEx_Delete(dt->pPartsNameWin);
    ((unsigned char *)dt->awmngp)[0x40] = 1;   /* alert window manager: close request (awmngp kept) */
    dt->pname = 0;
    dt->fo_pname = 0;
    for (i = 0; i < 3; i++) {
        UnitSprite_Delete(dt->apCompanyIcon[i]);
        dt->apCompanyIcon[i] = 0;
    }
    ioObjp->stat = 5;
    ioObjp->play_func = 0;
}

/* ---- group 2: PlayDialog, CheckEquip, PlayChanging ---- */

/* queue one dialog id (signed-byte dlg_num < 4 guard, post-increment); 1 = pushed */
static inline int masm_push_dlg(AsmSys *dt, int id)
{
    if (dt->dlg_num < 4) {
        dt->dlg_id[(int)dt->dlg_num++] = id;
        return 1;
    }
    return 0;
}

/* DF_MBGUIDE_Change is a key-kind list ended by 0x63 (DWARF: int (void)) */
static inline void masm_guide2(int a, int b) { gcall_as<int(int, int)>(GFN(DF_MBGUIDE_Change), a, b); }
static inline void masm_guide5(int a, int b, int c, int d, int e)
{
    gcall_as<int(int, int, int, int, int)>(GFN(DF_MBGUIDE_Change), a, b, c, d, e);
}

static void __AsmSys_PlayDialog(struct stMobj *ioObjp)
{
    PS2_FRAME(0x60);
    AsmSys *dt = masm_dt(ioObjp);
    int idx = 0;
    unsigned char *top, *btm;
    int id;

    /* no return: with dlg_num == 0 the rest still runs (quirk) */
    if (dt->dlg_num == 0) ioObjp->play_func = (void (*)(void))GFN(__AsmSys_Play);

    if (dt->f_after_sell_info == 0) {
        if (ioObjp->count == 0) {
            masm_guide2(0xe, 0x63);
            DF_MDIALOG_Open(4, DF_MSTR_GetInfo(MenuMng.language, 1, dt->dlg_id[(int)dt->dlg_idx]), 0, 0, 0, 0, 0, 0);
            ioObjp->count++;
        } else if (ioObjp->count == 1) {
            if (!(DF_MDIALOG_Sync() & 4)) return;
            dt->dlg_idx++;
            if (dt->dlg_idx < dt->dlg_num) {
                ioObjp->count = 0;
                return;
            }
            {
                /* 9 arguments: the 9th (0x63) is read from the caller's sp+0 */
                PS2_STACK(unsigned long long, arg9, 0);
                arg9 = 0x63;
                (void)arg9;
                gcall_as<int(int, int, int, int, int, int, int, int)>(GFN(DF_MBGUIDE_Change), dt->btnkind[0],
                    dt->btnkind[1], dt->btnkind[2], dt->btnkind[3], dt->btnkind[4], dt->btnkind[5], dt->btnkind[6],
                    dt->btnkind[7]);
            }
            ioObjp->play_func = (void (*)(void))GFN(__AsmSys_Play);
        }
        return;
    }

    /* f_after_sell_info: composed "after sell" message */
    if (ioObjp->count == 0) {
        masm_guide2(0xe, 0x63);
        if (dt->info_strp == 0) dt->info_strp = (char *)DF_MMM_Alloc(2, 0x80);
        id = dt->dlg_id[(int)dt->dlg_idx];
        if (id & 0x700) {
            top = DF_MSTR_GetInfo(MenuMng.language, 1, 0x30dbc);
            btm = DF_MSTR_GetInfo(MenuMng.language, 1, 0x30dbd);
            id = dt->dlg_id[(int)dt->dlg_idx];
            if (id & 0x100) idx = 0;
            if (id & 0x200) idx = 1;
            if (id & 0x400) idx = 2;
        } else if (id & 0x3800) {
            top = DF_MSTR_GetInfo(MenuMng.language, 1, 0x30dbe);
            btm = DF_MSTR_GetInfo(MenuMng.language, 1, 0x30dbf);
            id = dt->dlg_id[(int)dt->dlg_idx];
            if (id & 0x800) idx = 0;
            if (id & 0x1000) idx = 1;
            if (id & 0x2000) idx = 2;
        } else {
            /* info_strp stays allocated (quirk) */
            DF_MDIALOG_Open(4, DF_MSTR_GetInfo(MenuMng.language, 1, id), 0, 0, 0, 0, 0, 0);
            ioObjp->count++;
            return;
        }
        ps2_sprintf_ss(dt->info_strp, masm_fmt_ss, (const char *)top, (const char *)btm);
        dt->info_strp[2] += (signed char)idx;   /* control-code byte of the message selects the variant */
        DF_MDIALOG_Open(4, (unsigned char *)dt->info_strp, 0, 0, 0, 0, 0, 0);
        ioObjp->count++;
    } else if (ioObjp->count == 1) {
        if (!(DF_MDIALOG_Sync() & 4)) return;
        dt->dlg_idx++;
        if (dt->dlg_idx < dt->dlg_num) {
            ioObjp->count = 0;
            return;
        }
        if (dt->info_strp) DF_MMM_Free(2, dt->info_strp);
        dt->info_strp = 0;
        dt->f_after_sell_info = 0;
        if (dt->part_id % 1000 != 0) masm_guide5(1, 2, 3, 0xf, 0x63);
        else masm_guide5(1, 2, 3, 0, 0x63);
        ioObjp->play_func = (void (*)(void))GFN(__AsmSys_Play);
    }
}

/* list kinds whose part has a parameter record (DF_MDATA_GetData(.., 1)) */
static inline int masm_kind_has_data(int k)
{
    return k == 0xf || k == 0xe || k == 0xd || k == 0xc || k == 9 || k == 8 || k == 1 || k == 0xb || k == 0xa || k == 2;
}

static inline void *masm_part_data(struct _stMModel *p, int n)
{
    return DF_MDATA_GetData((void *)p->mmdp->OTH.datap, n);
}

static void __AsmSys_CheckEquip(struct stMobj *ioObjp)
{
    PS2_FRAME(0x80);
    AsmSys *dt = masm_dt(ioObjp);
    struct _stMModel *partp;
    unsigned int err, info;
    int m, tid, e, player, pushed;

    dt->dlg_num = 0;
    dt->dlg_idx = 0;
    DF_MBGUIDE_GetKind(dt->btnkind);

    if (dt->part_id / 1000 != 1 && dt->acp->connect.tmp.flag.no_core == 1) {
        masm_push_dlg(dt, 0x30dc1);
        goto fin;
    }
    if (dt->pre_part_id == dt->part_id && dt->pre_part_id % 1000 == 0 && dt->pre_kind_idx == dt->kind_idx) {
        masm_push_dlg(dt, 0x30dae);
        goto fin;
    }
    if (dt->f_equip == 1 && dt->part_id % 1000 != 0) {
        masm_push_dlg(dt, 0x30dc6);
        goto fin;
    }

    err = dt->acp->connect.tmp_equip_err;
    if (err != 0) {
        if (dt->dlg_num < 4 && (err & 4)) {
            masm_push_dlg(dt, 0x30dae);
            goto fin;
        }
        pushed = 0;
        if (dt->dlg_num < 4 && (err & 1)) pushed += masm_push_dlg(dt, 0x30da5);
        if (dt->dlg_num < 4 && (err & 2)) pushed += masm_push_dlg(dt, 0x30da6);
        if (pushed == 0) {
            if (dt->dlg_num < 4 && (err & 0x10)) masm_push_dlg(dt, 0x30dd1);
            if (dt->dlg_num < 4 && (err & 0x20)) masm_push_dlg(dt, 0x30dd3);
            if (dt->dlg_num < 4 && (err & 0x40)) masm_push_dlg(dt, 0x30de0);
        }
        if (dt->dlg_num < 4 && (err & 8)) masm_push_dlg(dt, 0x30da7);
        goto fin;
    }

    /* the equip itself */
    partp = dt->acp->connect.part[0];
    info = DF_M_AC_UpdateTmpPart(M_ACp[(int)dt->player][0], 1);
    DF_M_AC_SelectPart(M_ACp[(int)dt->player][0], dt->kind_idx, 1);
    if (dt->f_reposit) {
        tid = DF_MPART_Index2TrueID(dt->kind_tbl[(int)dt->kind_idx], dt->part_id % 1000);
        player = dt->player;
        e = DF_MPART_GetRepositACPEQ((unsigned char)dt->kind_idx) & 0xff;
        if (e == 0xb && DF_MPART_IsWbackWep(tid)) e = 0xa;
        PlayerInfo_SetExistPartsGarage(player, e & 0xff, tid, 1);
        if (dt->f_reposit) {
            dt->f_reposit = 0;
            if (dt->disp_kind == 0) {
                if (dt->f_reposit) UnitSprite_SetFadeIn(dt->pNowRepo);   /* dead arm (quirk) */
                else UnitSprite_SetFadeOut(dt->pNowRepo);
            }
        }
    }
    /* results discarded (quirk) */
    if (masm_kind_has_data(dt->kind_idx) && partp) masm_part_data(partp, 1);
    if (dt->kind_idx == 2 && partp) masm_part_data(partp, 2);
    __AsmSys_ChangeDetailWin(ioObjp, 1, 0);
    __AsmSys_ChangeGeneWin2(ioObjp, 1, 1);
    m = dt->part_id % 1000;
    if (m == 0) Voice_Call(0x3f0, 0x7f);
    else Voice_Call(0x3ef, 0x7f);
    if (info == 0) return;   /* count / play_func untouched */

    if (dt->dlg_num < 4 && (info & 0x80)) masm_push_dlg(dt, 0x30daf);
    if (dt->dlg_num < 4 && (info & 4)) masm_push_dlg(dt, 0x30dc5);
    pushed = 0;
    if (dt->dlg_num < 4 && (info & 3)) pushed = masm_push_dlg(dt, 0x30da8);
    if (!pushed) {
        if (dt->dlg_num < 4 && (info & 0x2000)) pushed = masm_push_dlg(dt, 0x30dd2);
        if (!pushed) {
            if (dt->dlg_num < 4 && (info & 0x800)) masm_push_dlg(dt, 0x30dd4);
            if (dt->dlg_num < 4 && (info & 0x1000)) masm_push_dlg(dt, 0x30dd5);
        }
    }
    if (dt->dlg_num < 4 && (info & 0x400)) masm_push_dlg(dt, 0x30dac);
    if (dt->dlg_num < 4 && (info & 8)) masm_push_dlg(dt, 0x30da9);
    if (dt->dlg_num < 4 && (info & 0x10)) masm_push_dlg(dt, 0x30daa);
    if (dt->dlg_num < 4 && (info & 0x20)) masm_push_dlg(dt, 0x30dad);
    if (dt->dlg_num < 4 && (info & 0x40)) masm_push_dlg(dt, 0x30dab);

fin:
    Voice_Call(0x3f1, 0x7f);
    ioObjp->count = 0;
    ioObjp->play_func = (void (*)(void))GFN(__AsmSys_PlayDialog);
}

/* company icon of the selected part's maker (c = -1: none); nCompanyIcon is re-read after the store */
static inline void masm_company_icon(AsmSys *dt, int c)
{
    int n;
    if (c == dt->nCompanyIcon) return;
    n = dt->nCompanyIcon;
    if (n >= 0 && n < 3) UnitSprite_SetFadeOut(dt->apCompanyIcon[n]);
    dt->nCompanyIcon = (char)c;
    n = dt->nCompanyIcon;
    if (n >= 0 && n < 3) {
        DF_MOBJ_SetDrawEnable(dt->apCompanyIcon[n], 1);
        UnitSprite_SetFadeIn(dt->apCompanyIcon[(int)dt->nCompanyIcon]);
    }
}

/* state 1 / 2: set the temp part on the other AC slots that still need it (dialogSync bit 8 + num) */
static inline void masm_sync_slot(AsmSys *dt, int slot)
{
    struct stMenuAC *a = M_ACp[(int)dt->player][slot];
    if (dt->dialogSync & (0x100 << a->num)) DF_M_AC_SetTmpPart(a, dt->kind_idx, dt->part_id);
}

static void __AsmSys_PlayChanging(struct stMobj *ioObjp)
{
    PS2_FRAME(0x90);
    PS2_STACK(struct struct_76b810ec, col, 0x8c);
    AsmSys *dt = masm_dt(ioObjp);
    struct stMenuAC *pac;
    struct _stMModel *partp = 0;
    struct StAcAsmTune *pTune;
    char *pname, *infop, *pinfo;
    void *datap, *wepp;
    unsigned int info;
    int tid, e, player, v, f_eq;

    col.r = 0x90;   /* gp-0x77ec: 0x80909090 */
    col.g = 0x90;
    col.b = 0x90;
    col.a = 0x80;
    pac = M_ACp[(int)dt->player][0];
    masm_arrow_blink();

    if (MenuMng.stat == 0xc) {   /* HDD error: leave (mdlwinp unchecked) */
        if (dt->mdlwinp->stat != 4) {
            if (MenuMng.f_show_hdd_err != 0) {
                __AsmSys_SetFadeOut(ioObjp);
            } else {
                DF_MDIALOG_Open(1, DF_MSTR_GetInfo(MenuMng.language, 1, 0x31510), 0, 0, 0, 0, 1, 0);
                MenuMng.f_show_hdd_err = 1;
                ioObjp->play_func = (void (*)(void))GFN(__PlayOpenDialog);
            }
        }
        return;
    }

    switch (ioObjp->count) {
    case 0:
        if (dt->part_id % 1000 != 0 && !DF_MOBJ2_MdlWinReadSync(dt->mdlwinp)) return;
        DF_M_AC_SetTmpPart(M_ACp[(int)dt->player][0], dt->kind_idx, dt->part_id);
        if (dt->dialogSync & 0x700) ioObjp->count++;
        else ioObjp->count = 0xa;
        return;
    case 1:
        if (DF_M_AC_AllCheckReading(dt->player)) return;
        masm_sync_slot(dt, 1);
        ioObjp->count++;
        return;
    case 2:
        if (DF_M_AC_AllCheckReading(dt->player)) return;
        masm_sync_slot(dt, 2);
        ioObjp->count = 0xa;
        return;
    case 0xa:
        break;
    case 0xb:
        if (DF_MBGUIDE_Sync(0) == 0) return;
        if (dt->mdlwinp->stat == 4) return;
        info = 0;
        if (dt->dialogSync & 0x100) info |= DF_M_AC_UpdateTmpPart(M_ACTable[(int)dt->player], 1);
        if (dt->dialogSync & 0x200) info |= DF_M_AC_UpdateTmpPart(M_ACTable[(int)dt->player] + 1, 1);
        if (dt->dialogSync & 0x400) info |= DF_M_AC_UpdateTmpPart(M_ACTable[(int)dt->player] + 2, 1);
        DF_M_AC_SelectPart(M_ACp[(int)dt->player][0], dt->kind_idx, 1);
        if (dt->dlg_num < 4 && (info & 0x80)) masm_push_dlg(dt, 0x30daf);
        __AsmSys_CheckAlart(ioObjp);
        dt->pre_part_id = dt->part_id;
        dt->pre_kind_idx = dt->kind_idx;
        ioObjp->count = 0;
        ioObjp->play_func = (void (*)(void))GFN(__AsmSys_PlayDialog);
        dt->dialogSync = 0;
        return;
    default:
        return;
    }

    /* state 0xa: the new part is read, update everything */
    if (DF_M_AC_AllCheckReading(dt->player)) return;
    if (dt->acp->flag.reading) return;   /* bit 1 (0x1e0a45c); the map said tmp_connect */
    M_AC_UpdateTuneStat(pac, 0);
    ((struct struct_f8cbbdcd *)&dt->mdlwinp->dt)->f_stay = 0;
    if (dt->part_id % 1000 != 0) {
        partp = dt->acp->connect.part[0];
        if (!partp->flag.init) return;   /* read before the NULL test (quirk) */
        pname = DF_MMODEL_GetPartsName(partp);
        infop = partp ? (char *)partp->prm.part.infop : 0;
        /* results discarded (quirk); keyed on kind_tbl[kind_idx] */
        switch (dt->kind_tbl[(int)dt->kind_idx]) {
        case 2:
            if (partp) masm_part_data(partp, 1);
            if (partp) masm_part_data(partp, 2);
            break;
        case 0xc: case 0xb: case 0xa: case 9: case 8: case 1:
            if (partp) masm_part_data(partp, 1);
            break;
        }
        pinfo = infop + 0x29 + MenuMng.language * 0x49;
    } else {
        pname = 0;
        infop = 0;
        pinfo = 0;
    }

    if (pname) {
        strncpy(dt->pname, pname, 0x1f);
        DF_MHELP_SetParamEnable(DF_MHELP_GetParam(dt->hlist, 1), 1);
    } else {
        dt->pname[0] = 0;
        DF_MHELP_SetParamEnable(DF_MHELP_GetParam(dt->hlist, 1), 0);
    }

    if (dt->pname_strp == 0) {
        dt->pname_strp = DF_MOBJ_NewStringDirectFromLayoutName(dt->pStrFence, dt->pname,
                                                               (char *)LAYOUT_GetLayoutData(0x3f2), (char *)"parts20c_0",
                                                               &col, 0, 0);
        dt->pname_strp->f_help = 1;
        DF_MOBJ_SetFadeIn(dt->pname_strp);
    }

    if (infop) {
        dt->cMaker = infop[0x28];
        if (dt->disp_kind == 0) masm_company_icon(dt, dt->cMaker);
    } else {
        dt->cMaker = -1;
        masm_company_icon(dt, -1);
    }

    if (dt->disp_kind == 0) __AsmSys_UpdateTunedStr(ioObjp);

    memset(dt->virsion_str, 0, 16);
    tid = DF_MPART_Index2TrueID(dt->part_id / 1000, dt->part_id % 1000);
    if (loader_GetVersion(dt->part_id / 1000, tid) >= 2)
        loader_GetPartsVersionString(dt->virsion_str, dt->part_id / 1000, tid);

    if (dt->vstrp == 0) {
        dt->vstrp = DF_MOBJ_NewStringDirect(dt->mdlwinp, dt->virsion_str, 1, 0x64, 0xdc, &col, 0, 0x64);
        DF_MOBJ_SetFadeIn(dt->vstrp);
    }

    DF_M_AC_CheckEquipFlag(dt->acp, 1);
    __AsmSys_CheckAlart(ioObjp);
    DF_M1HELP_ChangeString((unsigned char *)pinfo);

    if (dt->detailwinp) {
        if (dt->f_equip || dt->f_resetdetailwin) __AsmSys_ChangeDetailWin(ioObjp, 1, 0);
        else __AsmSys_ChangeDetailWin(ioObjp, 0, 2);
    }
    dt->f_resetdetailwin = 0;   /* also when detailwinp is NULL */

    if (dt->genewinp) {
        f_eq = dt->f_equip;
        if (!f_eq && dt->acp->real_part[DF_M_AC_EquipIDX2IDX(dt->kind_idx)] == 0) f_eq = 1;
        pTune = &dt->acp->cur_tune;
        /* keyed on kind_idx directly (step 3 uses kind_tbl) */
        datap = (masm_kind_has_data(dt->kind_idx) && partp) ? masm_part_data(partp, 1) : 0;
        wepp = (dt->kind_idx == 2 && partp) ? masm_part_data(partp, 2) : 0;
        if (dt->genewinp)
            DF_MASMP_ChangeGeneWin(dt->genewinp, dt->kind_tbl[(int)dt->kind_idx],
                                   partp ? partp->prm.part.infop : 0, datap, wepp, f_eq, pTune);
    }

    /* reposit flag (GetRepositACPEQ applied twice: quirk) */
    tid = DF_MPART_Index2TrueID(dt->part_id / 1000, dt->part_id % 1000);
    e = DF_MPART_GetRepositACPEQ(dt->kind_idx);
    player = dt->player;
    e = DF_MPART_GetRepositACPEQ(e & 0xff) & 0xff;
    if (e == 0xb && DF_MPART_IsWbackWep(tid)) e = 0xa;
    v = PlayerInfo_IsExistPartsReposit(player, e & 0xff, tid) != 0;
    if (dt->f_reposit != v) {
        dt->f_reposit = v;
        if (dt->disp_kind == 0) {
            if (dt->f_reposit) UnitSprite_SetFadeIn(dt->pNowRepo);
            else UnitSprite_SetFadeOut(dt->pNowRepo);
        }
    }

    if (dt->ac_specwinp) __MASM_SetACSpecInfo(dt->ac_specwinp);

    if (dt->f_after_sell_info == 1) {
        if (dt->dialogSync & 0x700) {
            ioObjp->count++;   /* -> 0xb */
        } else {
            ioObjp->count = 0;
            ioObjp->play_func = (void (*)(void))GFN(__AsmSys_PlayDialog);
        }
    } else {
        /* count stays 0xa (quirk) */
        masm_guide5(1, 2, 3, dt->part_id % 1000 != 0 ? 0xf : 0, 0x63);
        ioObjp->play_func = (void (*)(void))GFN(__AsmSys_Play);
    }
}

/* ---- group 3: detail window wrappers, ChangeDisp, ChangePart, OnPartsListModeChange, OnReposit ---- */

/* selected part of the AC (bEquip: the equipped part + real tune, else the shown part + current tune) */
static inline struct _stMModel *masm_detail_part(AsmSys *dt, int bEquip, struct StAcAsmTune **pTune)
{
    struct _stMModel *partp;
    if (bEquip) partp = dt->acp->real_part[DF_M_AC_EquipIDX2IDX(dt->kind_idx)];
    else partp = dt->acp->cur_part[DF_M_AC_EquipIDX2IDX(dt->kind_idx)];
    *pTune = bEquip ? &dt->acp->real_tune : &dt->acp->cur_tune;
    return partp;
}

static void __AsmSys_ChangeDetailWin(struct stMobj *ioObjp, int bEquip, int mode)
{
    PS2_FRAME(0x60);
    AsmSys *dt = masm_dt(ioObjp);
    struct _stMModel *partp;
    struct StAcAsmTune *pTune;
    void *datap, *wepp;

    if (dt->detailwinp == 0) return;
    partp = masm_detail_part(dt, bEquip, &pTune);
    /* keyed on kind_idx directly */
    datap = (masm_kind_has_data(dt->kind_idx) && partp) ? masm_part_data(partp, 1) : 0;
    wepp = (dt->kind_idx == 2 && partp) ? masm_part_data(partp, 2) : 0;
    DF_MASMD_ChangeDetailWin(dt->detailwinp, dt->kind_tbl[(int)dt->kind_idx], partp ? partp->prm.part.infop : 0, datap,
                             wepp, mode, pTune);
}

static void __AsmSys_NewDetailWin(struct stMobj *ioObjp, int bEquip, int mode)
{
    PS2_FRAME(0x90);
    PS2_STACK(struct stMenuRect, detail_rect, 0x80);   /* handed to interpreted MASMD: PS2 memory */
    PS2_STACK(unsigned long long, arg9, 0);            /* the 9th argument (mode) on the PS2 stack */
    AsmSys *dt = masm_dt(ioObjp);
    struct _stMModel *partp;
    struct StAcAsmTune *pTune;
    void *datap, *wepp;

    detail_rect.x = 24;   /* 0x1edae30 */
    detail_rect.y = 90;
    detail_rect.w = 270;
    detail_rect.h = 336;
    partp = masm_detail_part(dt, bEquip, &pTune);
    datap = (masm_kind_has_data(dt->kind_idx) && partp) ? masm_part_data(partp, 1) : 0;
    wepp = (dt->kind_idx == 2 && partp) ? masm_part_data(partp, 2) : 0;
    DF_MASMD_DeleteDetailWin(dt->detailwinp);   /* no NULL test (interpreted callee) */
    arg9 = (unsigned long long)(long long)mode;
    (void)arg9;
    dt->detailwinp = DF_MASMD_NewDetailWin(dt->pStrFence, &detail_rect, dt->kind_tbl[(int)dt->kind_idx],
                                           partp ? partp->prm.part.infop : 0, datap, wepp, dt->hlist, pTune, mode);
    DF_MASMD_SetDetailWinFadeIn(dt->detailwinp);
}

/* alert window manager items: fade in (1) / out (2) their sprites */
static inline void masm_awmng_items(struct stMobj *w, int in)
{
    struct stMobj **d = masm_dtw(w);
    int j;
    for (j = 0; j < 6; j++) {
        struct stMobj *a = d[j];
        if (a) {
            if (in) UnitSprite_SetFadeIn(masm_dtw(a)[0]);
            else UnitSprite_SetFadeOut(masm_dtw(a)[0]);
            a->stat = in ? 1 : 2;
            a->count = 0;
        }
    }
}

/* the AC spec window: request deletion of the old fade-out copy, fade out the current one through fop, drop fop */
static inline void masm_spec_to_fo(AsmSys *dt)
{
    if (dt->ac_specwinfop) masm_spec_close(dt->ac_specwinfop);
    dt->ac_specwinfop = dt->ac_specwinp;
    if (dt->ac_specwinfop) masm_spec_fadeout(dt->ac_specwinfop);
    dt->ac_specwinfop = 0;
}

static void __AsmSys_ChangeDisp(struct stMobj *ioObjp)
{
    PS2_FRAME(0x70);
    AsmSys *dt = masm_dt(ioObjp);
    struct stMobj *w;
    int i;

    if (dt->disp_kind == 1) {
        /* to the gene / detail side */
        Voice_Call(0x3ec, 0x7f);
        w = dt->awmngp;
        if (w) {
            masm_awmng_items(w, 0);
            w->stat = 2;
        }
        masm_spec_to_fo(dt);
        if (dt->genewinp) DF_MASMP_SetGeneWinFadeOut(dt->genewinp);
        UnitSprite_SetFadeOut(dt->pNowEq);
        DF_MOBJ_SetFadeOut(dt->pTuned);
        if (dt->f_reposit) UnitSprite_SetFadeOut(dt->pNowRepo);
        masm_company_icon(dt, -1);   /* the FadeIn arm after the store is dead (quirk) */
        for (i = 0; i < 4; i++) DF_MOBJ_SetFadeOut(psArrowBuf[i]);
        WindowEx_MoveToLayout(dt->pACSpecWin, (char *)LAYOUT_GetLayoutData(0x3f3), (char *)"parts01a_0", 6);
        WindowEx_MoveToLayout(dt->pPartsSpecWin, (char *)LAYOUT_GetLayoutData(0x3f3), (char *)"parts01_0", 6);
        UnitSprite_SetFadeOut(dt->pACWakuWin);
        UnitSprite_SetFadeOut(dt->pPartsWakuWin);
        UnitSprite_SetFadeOut(dt->pPartsWakuWinInner);
    } else if (dt->disp_kind == 0) {
        /* back to the parts side */
        Voice_Call(0x3ec, 0x7f);
        masm_spec_to_fo(dt);
        WindowEx_ResetRect(dt->pACSpecWin, 6);
        WindowEx_ResetRect(dt->pPartsSpecWin, 6);
        UnitSprite_SetFadeIn(dt->pACWakuWin);
        UnitSprite_SetFadeIn(dt->pPartsWakuWin);
        UnitSprite_SetFadeIn(dt->pPartsWakuWinInner);
        __AsmSys_UpdateTunedStr(ioObjp);
        DF_MASMD_SetDetailWinFadeOut(dt->detailwinp);   /* no NULL test (interpreted callee) */
        for (i = 0; i < 4; i++) DF_MOBJ_SetFadeIn(psArrowBuf[i]);
        if (dt->f_equip && dt->disp_kind == 0) UnitSprite_SetFadeIn(dt->pNowEq);   /* disp_kind re-read (always 0) */
        if (dt->f_reposit) UnitSprite_SetFadeIn(dt->pNowRepo);
        masm_company_icon(dt, dt->cMaker);
        w = dt->awmngp;
        if (w) {
            w->f_draw = 1;
            masm_awmng_items(w, 1);
            w->stat = 1;
        }
    }
    ioObjp->count = 0;
    ioObjp->play_func = (void (*)(void))GFN(__AsmSys_PlayChangingDisp);
    dt->step = 0;
}

/* colour record at a byte offset of acp->colp */
static inline struct struct_c4910010 *masm_colp(AsmSys *dt, int off)
{
    return (struct struct_c4910010 *)((unsigned char *)dt->acp->colp + off);
}

static void __AsmSys_ChangePart(struct stMobj *ioObjp, int inDir)
{
    PS2_FRAME(0x80);
    PS2_STACK(unsigned long long, arg9, 0);   /* SetMdlWinChange's 9th argument slot */
    AsmSys *dt = masm_dt(ioObjp);
    struct struct_c4910010 *p_colp = 0, *p_colp2 = 0;
    int flag = 0;
    int k;

    dt->part_id = dt->id_tablep->cell[(int)dt->idx].id;   /* no bounds test on idx */
    k = dt->kind_idx;
    switch (k) {
    case 0xf: case 0xe: case 0xd: case 0xc:
        p_colp = M_AC_GetWepColorFromACPEQ((eAcPartsEquip)(k & 0xff), dt->acp->colp);
        break;
    case 2:
        p_colp = masm_colp(dt, 0x30);
        p_colp2 = masm_colp(dt, 0x20);
        break;
    case 3:
        p_colp = masm_colp(dt, 0x40);
        break;
    case 1:
        p_colp = masm_colp(dt, 0x10);
        break;
    case 0:
        p_colp = masm_colp(dt, 0);
        break;
    case 0xa: case 0xb:
        p_colp = M_AC_GetWepColorFromACPEQ((eAcPartsEquip)(k & 0xff), dt->acp->colp);
        p_colp2 = M_AC_GetWepColorFromACPEQ((eAcPartsEquip)0xa, dt->acp->colp);
        if (dt->kind_idx == 0xa) flag = 1;
        break;
    }

    if (inDir != 0) {
        arg9 = 0;
        (void)arg9;
        DF_MOBJ2_SetMdlWinChange(dt->mdlwinp, 1, dt->part_id, p_colp, p_colp2, flag, inDir, 1, 0);
        masm_delete(&dt->fo_pname_strp);
        dt->fo_pname_strp = dt->pname_strp;
        strncpy(dt->fo_pname, dt->pname, 0x1f);   /* no terminator written (quirk) */
        /* the string object's text pointer (obj+0x2c); pname_strp dereferenced unchecked */
        *(char **)((unsigned char *)dt->fo_pname_strp + 0x2c) = dt->fo_pname;
        DF_MOBJ_SetFadeOut(dt->fo_pname_strp);
        dt->pname_strp = 0;
    }
    ((struct struct_f8cbbdcd *)&dt->mdlwinp->dt)->f_stay = 1;
    ioObjp->count = 0;
    ioObjp->play_func = (void (*)(void))GFN(__AsmSys_PlayChanging);
}

static void __AsmSys_OnPartsListModeChange(struct stMobj *ioObjp)
{
    PS2_FRAME(0x460);
    PS2_STACK(struct stMPartIdTable, OldIdTable, 0x50);
    AsmSys *dt = masm_dt(ioObjp);
    int oldidx, k, v;

    DF_MPART_Index2TrueID(dt->kind_tbl[(int)dt->kind_idx], dt->part_id % 1000);   /* result unused (quirk) */
    if (dt->cListMode == 0) {
        dt->cListMode = 1;
        DF_MPART_MakeIDTableNx(dt->id_tablep, dt->cListMode ? 0x10000 : 0, dt->player, dt->kind_tbl[(int)dt->kind_idx],
                               dt->kind_idx);
        dt->idx = (char)DF_MPART_PartID2TableIdx(dt->id_tablep, dt->part_id);
        if (!(dt->idx >= 0 && dt->idx < dt->id_tablep->num)) dt->idx = 0;
        return;   /* no ChangePart (quirk) */
    }

    oldidx = dt->idx;
    OldIdTable = *dt->id_tablep;
    dt->cListMode = 0;
    DF_MPART_MakeIDTableNx(dt->id_tablep, dt->cListMode ? 0x10000 : 0, dt->player, dt->kind_tbl[(int)dt->kind_idx],
                           dt->kind_idx);
    dt->idx = (char)DF_MPART_PartID2TableIdx(dt->id_tablep, dt->part_id);
    if (dt->idx > 0 && dt->idx < dt->id_tablep->num) return;
    if (oldidx == 0) return;   /* idx left as returned (quirk) */
    /* walk back from the old position to a part that is in the new list; cell 0 is never tried */
    k = oldidx;
    do {
        v = DF_MPART_PartID2TableIdx(dt->id_tablep, OldIdTable.cell[k].id);
        if (v > 0 && v < dt->id_tablep->num) {
            dt->idx = (char)v;
            __AsmSys_ChangePart(ioObjp, 4);
            return;
        }
    } while (--k > 0);
    dt->idx = 0;
    __AsmSys_ChangePart(ioObjp, 4);
}

static void __AsmSys_OnReposit(struct stMobj *ioObjp)
{
    PS2_FRAME(0x70);
    AsmSys *dt = masm_dt(ioObjp);
    int mask = 0, i, tid, acpeq, player, notIn;

    if (dt->part_id % 1000 == 0) return;
    for (i = 0; i < 3; i++)
        if (DF_M_AC_IsEquipPart4(M_ACTable[(int)dt->player] + i, dt->part_id, dt->kind_idx == 0xb)) mask |= 1 << i;
    if (mask != 0) {
        /* equipped on one of the ACs: refuse */
        dt->dlg_num = 0;
        dt->dlg_idx = 0;
        masm_push_dlg(dt, 0x30dfd + mask);
        ioObjp->count = 0;
        ioObjp->play_func = (void (*)(void))GFN(__AsmSys_PlayDialog);
        Voice_Call(0x3f1, 0x7f);
        return;
    }

    Voice_Call(0x3ea, 0x7f);
    tid = DF_MPART_Index2TrueID(dt->kind_tbl[(int)dt->kind_idx], dt->part_id % 1000);
    acpeq = DF_MPART_GetRepositACPEQ(dt->kind_idx);
    if (acpeq == 0xb && DF_MPART_IsWbackWepIdx(dt->part_id)) acpeq = 0xa;   /* full part id here, tid below (quirk) */
    notIn = (PlayerInfo_IsExistPartsReposit(dt->player, acpeq, tid) != 0) ^ 1;
    player = dt->player;
    acpeq = DF_MPART_GetRepositACPEQ(acpeq & 0xff) & 0xff;   /* applied twice (quirk) */
    if (acpeq == 0xb && DF_MPART_IsWbackWep(tid)) acpeq = 0xa;
    if (notIn) PlayerInfo_SetExistPartsReposit(player, acpeq & 0xff, tid, 1);
    else PlayerInfo_SetExistPartsGarage(player, acpeq & 0xff, tid, 1);

    if (notIn) {
        DF_MPART_Index2TrueID(dt->part_id / 1000, dt->part_id % 1000);   /* result unused (quirk) */
        if (dt->cListMode == 0) {
            /* the part left the list: rebuild it and step back one entry */
            DF_MPART_MakeIDTableNx(dt->id_tablep, dt->cListMode ? 0x10000 : 0, dt->player,
                                   dt->kind_tbl[(int)dt->kind_idx], dt->kind_idx);
            dt->idx = dt->idx - 1;
            if (dt->idx < 0) dt->idx = dt->id_tablep->num - 1;
            __AsmSys_ChangePart(ioObjp, 4);
        } else if (dt->f_reposit != 1) {
            dt->f_reposit = 1;
            if (dt->disp_kind == 0) {
                if (dt->f_reposit) UnitSprite_SetFadeIn(dt->pNowRepo);
                else UnitSprite_SetFadeOut(dt->pNowRepo);   /* dead arm (quirk) */
            }
        }
    } else if (dt->f_reposit) {
        dt->f_reposit = 0;
        if (dt->disp_kind == 0) {
            if (dt->f_reposit) UnitSprite_SetFadeIn(dt->pNowRepo);   /* dead arm (quirk) */
            else UnitSprite_SetFadeOut(dt->pNowRepo);
        }
    }
}

/* ---- group 4: Play, SetFadeOut, PlayFadeIn, CheckAlart, ChangeGeneWin2, CreateGeneWin2 ---- */

/* acp->part_id[] index of the selected list kind (kinds 0xb / 0xe / 0xf adjusted) */
static inline int masm_kind_part_idx(AsmSys *dt)
{
    int p = DF_M_AC_PartID2IDX(dt->kind_tbl[(int)dt->kind_idx] * 1000);
    if (dt->kind_idx == 0xb) p++;
    if (dt->kind_idx == 0xe) p = 0x12;
    if (dt->kind_idx == 0xf) p = 0x13;
    return p;
}

/* arrow i pressed: blink boost; the counter goes through a float round trip (exact for small ints) */
static inline void masm_arrow_kick(int i)
{
    AddBlinkAlpha[i] = 0x23;
    ArrowBlinkCnt[i] = ps2_ftoi((float)ArrowBlinkCnt[i] + 12.0f);
}

static inline int masm_cell_category(AsmSys *dt) { return DF_MPART_GetCategory(dt->id_tablep->cell[(int)dt->idx].id); }

/* step idx forward to the first entry of a later category than c; wrap to 0 */
static inline void masm_next_category(AsmSys *dt, int c)
{
    dt->idx = dt->idx + 1;
    while (dt->idx < dt->id_tablep->num) {
        if (c < masm_cell_category(dt)) break;
        dt->idx = dt->idx + 1;
    }
    if (!(dt->idx < dt->id_tablep->num)) dt->idx = 0;
}

static void __AsmSys_Play(struct stMobj *ioObjp)
{
    PS2_FRAME(0x80);
    PS2_STACK(struct struct_76b810ec, col, 0x7c);
    PS2_STACK(unsigned long long, arg9, 0);   /* 9th argument slot of SetMdlWinChange (stage 1) */
    AsmSys *dt = masm_dt(ioObjp);
    struct stMenuAC *acp;
    struct _stMModel *partp;
    struct stMobj *w;
    char *info;
    unsigned char *h;
    int kind, dir = 0, old, ret, id, tid, c, a, s, p;

    col.r = 0x90;   /* gp-0x77f0: 0x80909090 (raw bytes, a denormal as float) */
    col.g = 0x90;
    col.b = 0x90;
    col.a = 0x80;
    masm_arrow_blink();
    if (dt->mdlwinp->stat == 4) return;
    if (MenuMng.f_help == 1) return;
    if (DF_MBGUIDE_Sync(0) == 0) return;
    if (DF_MVIEW_MotionSync(MViewp) == 0) return;
    if (MenuKeyGuide_IsShown() != 0) return;
    kind = dt->kind_idx;
    MPadUser = dt->player;

    if (MenuMng.stat == 0xc) {   /* HDD error: leave */
        if (dt->mdlwinp->stat == 4) return;
        if (MenuMng.f_show_hdd_err != 0) {
            __AsmSys_SetFadeOut(ioObjp);
            return;
        }
        DF_MDIALOG_Open(1, DF_MSTR_GetInfo(MenuMng.language, 1, 0x31510), 0, 0, 0, 0, 1, 0);
        MenuMng.f_show_hdd_err = 1;
        ioObjp->play_func = (void (*)(void))GFN(__PlayOpenDialog);
        return;
    }

    /* circle: leave the screen */
    if (dt->f_necessary_end == 1 && dt->f_first_check == 0 && MPad[MPadUser].Rrt == 1) {
        Voice_Call(0x3eb, 0x7f);
        MenuMng.f_asem_detail_on = 0;
        __AsmSys_SetFadeOut(ioObjp);
        return;
    }
    if (dt->acp->flag.reading == 1) return;

    if (!dt->f_necessary_end) {
        /* stage 1: equip the necessary parts */
        old = dt->acp->part_id[0];
        dt->f_necessary_end = 1;
        ret = DF_M_AC_SetEquipNecessary(dt->acp);
        if (ret > 0 && ret == 1) dt->f_necessary = 1;
        if (dt->f_necessary == 1) {
            acp = dt->acp;
            id = acp->part_id[0];
            if (id > 0 && id != old) {
                arg9 = 0;
                (void)arg9;
                DF_MOBJ2_SetMdlWinChange(dt->mdlwinp, 1, id, (struct struct_c4910010 *)acp->colp, 0, 0, 3, 1, 0);
                dt->part_id = dt->acp->part_id[0];
                dt->idx = (char)DF_MPART_PartID2TableIdx(dt->id_tablep, dt->part_id);
                dt->pre_part_id = dt->part_id;
                dt->pre_kind_idx = dt->kind_idx;
            }
        }
        return;
    }

    if (dt->f_first_check == 1) {
        /* stage 2: first frame after the necessary parts */
        partp = dt->acp->real_part[0];
        M_AC_UpdateTuneStat(dt->acp, 0);
        M_AC_UpdateTuneStat(dt->acp, 1);
        __AsmSys_ChangeGeneWin2(ioObjp, 1, 1);
        if (partp) {
            DF_MMODEL_GetPartsName(partp);   /* result discarded (quirk) */
            info = partp ? (char *)partp->prm.part.infop : 0;
            h = (unsigned char *)info + MenuMng.language * 0x49 + 0x29;
            if (h) DF_M1HELP_ChangeString(h);
            if (info) {
                c = info[0x28];
                dt->cMaker = (char)c;
                masm_company_icon(dt, c);
            }
        }
        if (partp) {
            masm_delete(&dt->pname_strp);
            strncpy(dt->pname, DF_MMODEL_GetPartsName(partp), 0x1f);   /* no terminator written */
            dt->pname_strp = DF_MOBJ_NewStringDirectFromLayoutName(dt->pStrFence, dt->pname,
                                                                   (char *)LAYOUT_GetLayoutData(0x3f2),
                                                                   (char *)"parts20c_0", &col, 0, 0);
            __AsmSys_UpdateTunedStr(ioObjp);
            dt->pname_strp->f_help = 1;
            DF_MOBJ_SetFadeIn(dt->pname_strp);
            memset(dt->virsion_str, 0, 16);
            tid = DF_MPART_Index2TrueID(dt->part_id / 1000, dt->part_id % 1000);
            if (loader_GetVersion(dt->part_id / 1000, tid) >= 2)
                loader_GetPartsVersionString(dt->virsion_str, dt->part_id / 1000, tid);
            DF_MOBJ_SetFadeIn(dt->vstrp);
        }
        DF_M_AC_CheckEquipFlag(dt->acp, 0);   /* result unused */
        dt->f_first_check = 0;
        if (dt->f_necessary == 1) {
            DF_MBGUIDE_GetKind(dt->btnkind);
            __AsmSys_CheckAlart(ioObjp);
            /* no dlg_num < 4 guard here (quirk): raw store at dt+0x70 + 4 * dlg_num */
            p = dt->dlg_num;
            dt->dlg_num = (char)(p + 1);
            *(int *)((unsigned char *)dt->dlg_id + 4 * p) = 0x30da4;
            ioObjp->count = 0;
            ioObjp->play_func = (void (*)(void))GFN(__AsmSys_PlayDialog);
            dt->f_necessary = 0;
        }
        return;
    }

    /* stage 3: normal frame */
    if (dt->fo_ctgr_strp && dt->fo_ctgr_strp->stat == 5) masm_delete(&dt->fo_ctgr_strp);

    if (MPad[MPadUser].select == 1) {
        DF_MHELP_Mode(1);
        return;
    }
    if (MenuKeyGuide_IsShown()) return;
    if (MPad[MPadUser].start == 1) {
        MenuKeyGuide_Show(0x11);
        return;
    }
    if (MPad[MPadUser].Rdw == 1) {   /* cross: equip */
        __AsmSys_CheckEquip(ioObjp);
        DF_M_AC_CheckEquipFlag(dt->acp, 0);   /* result unused */
        __AsmSys_CheckAlart(ioObjp);
        dt->pre_part_id = dt->part_id;
        dt->pre_kind_idx = dt->kind_idx;
        return;
    }
    if (MPad[MPadUser].r2 == 1) {
        __AsmSys_OnReposit(ioObjp);
        return;
    }
    if (MPad[MPadUser].l2 == 1) {
        Voice_Call(0x3ea, 0x7f);
        __AsmSys_OnPartsListModeChange(ioObjp);
        return;
    }
    if (MPad[MPadUser].Rlt == 1) {   /* square: tune */
        if (__AsmSys_CheckTune(ioObjp)) {
            Voice_Call(0x3ea, 0x7f);
            __AsmSys_EnterTune(ioObjp);
        }
        return;
    }
    if (MPad[MPadUser].Rup == 1) {   /* triangle: parts / gene side */
        w = dt->awmngp;   /* no NULL test (quirk) */
        if (w->stat == 5 || w->stat == 0) {
            dt->disp_kind = (char)(dt->disp_kind + 1);
            if (dt->disp_kind >= 2) dt->disp_kind = 0;
            MenuMng.f_asem_detail_on = dt->disp_kind;
            DF_MHELP_Select(dt->hlist, 0);
            __AsmSys_ChangeDisp(ioObjp);
            return;
        }
        /* awmngp busy: the press falls through to the repeat chain */
    }

    /* repeat keys */
    if (MPad[MPadUser].repeat.r1) {   /* next category */
        Voice_Call(0x3ee, 0x7f);
        dir = 3;
        if (dt->kind_idx == 1) {
            c = masm_cell_category(dt);
            masm_next_category(dt, c);
        } else {
            c = masm_cell_category(dt);
            if (dt->idx == 0) dt->idx = (dt->id_tablep->num < 2) ? 0 : 1;
            else masm_next_category(dt, c);
        }
    } else if (MPad[MPadUser].repeat.l1) {   /* previous category */
        Voice_Call(0x3ee, 0x7f);
        dir = 4;
        if (dt->kind_idx == 1) {
            dt->idx = (char)DF_MGEN_SearchNextTopIdx_InSubCategory(dt->id_tablep, dt->idx, 0);
        } else {
            a = dt->idx;
            if (a != 0) {
                if (a == 1) {
                    dt->idx = 0;
                } else {
                    dt->idx = (char)DF_MGEN_SearchNextTopIdx_InSubCategory(dt->id_tablep, a, 0);
                    if (dt->idx == 0) dt->idx = 1;
                }
            } else if (dt->id_tablep->num != 0) {
                dt->idx = (char)(dt->id_tablep->num - 1);
                s = dt->idx;
                if (s != DF_MGEN_GetTopIdx_InSubCategory(dt->id_tablep, s))
                    dt->idx = (char)DF_MGEN_SearchNextTopIdx_InSubCategory(dt->id_tablep, s, 0);
                if (dt->idx == 0) dt->idx = 1;
            }
        }
    } else if (MPad[MPadUser].repeat.rt) {
        Voice_Call(0x3ee, 0x7f);
        dt->idx = dt->idx + 1;
        if (!(dt->idx < dt->id_tablep->num)) dt->idx = 0;
        dir = 3;
        masm_arrow_kick(0);
    } else if (MPad[MPadUser].repeat.lt) {
        Voice_Call(0x3ee, 0x7f);
        dt->idx = dt->idx - 1;
        if (dt->idx < 0) dt->idx = (char)(dt->id_tablep->num - 1);
        if (dt->idx < 0) dt->idx = 0;
        dir = 4;
        masm_arrow_kick(1);
    } else if (MPad[MPadUser].repeat.up) {
        Voice_Call(0x3ee, 0x7f);
        dt->kind_idx = dt->kind_idx - 1;
        if (dt->kind_idx < 0) dt->kind_idx = 0xf;
        dir = 2;
        masm_arrow_kick(3);
    } else if (MPad[MPadUser].repeat.dw) {
        Voice_Call(0x3ee, 0x7f);
        dt->kind_idx = dt->kind_idx + 1;
        if (dt->kind_idx >= 0x10) dt->kind_idx = 0;
        dir = 1;
        masm_arrow_kick(2);
    }

    if (dt->kind_idx != kind) {
        /* list kind changed: rebuild the list (pre_part_id / pre_kind_idx not refreshed, quirk) */
        ((struct struct_f8cbbdcd *)&dt->mdlwinp->dt)->f_stay = 1;
        DF_M_AC_UpdateTmpPart(dt->acp, 0);
        DF_M_AC_SelectPart(dt->acp, kind, 0);
        DF_MPART_MakeIDTableNx(dt->id_tablep, dt->cListMode ? 0x10000 : 0, dt->player, dt->kind_tbl[(int)dt->kind_idx],
                               dt->kind_idx);
        p = masm_kind_part_idx(dt);
        dt->part_id = dt->acp->part_id[p];
        dt->idx = (char)DF_MPART_PartID2TableIdx(dt->id_tablep, dt->part_id);
        __ChangeCategoryStr(ioObjp);
        DF_M_AC_SelectPart(dt->acp, dt->kind_idx, 1);
        dt->f_resetdetailwin = 1;
    }
    if (dir != 0) __AsmSys_ChangePart(ioObjp, dir);
}

static void __AsmSys_SetFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x70);
    AsmSys *dt = masm_dt(ioObjp);
    struct stMobj *w;
    int i;

    for (i = 0; i < 4; i++) DF_MOBJ_SetFadeOut(psArrowBuf[i]);
    DF_MOBJ_SetFadeOut(dt->ctgr_strp);
    DF_MOBJ_SetFadeOut(dt->pname_strp);
    DF_MOBJ_SetFadeOut(dt->vstrp);
    DF_MOBJ_SetFadeOut(dt->pAcName);
    DF_MOBJ_SetFadeOut(dt->pAcNo);
    DF_MOBJ_SetFadeOut(dt->pTuned);
    WindowEx_SetFadeOut(dt->pACSpecWin);
    UnitSprite_SetFadeOut(dt->pACWakuWin);
    UnitSprite_SetFadeOut(dt->pPartsWakuWin);
    UnitSprite_SetFadeOut(dt->pPartsWakuWinInner);
    UnitSprite_SetFadeOut(dt->pACNameWin);
    WindowEx_SetFadeOut(dt->pPartsSpecWin);
    WindowEx_SetFadeOut(dt->pCategoryWin);
    WindowEx_SetFadeOut(dt->pPartsNameWin);
    UnitSprite_SetFadeOut(dt->pNowEq);
    UnitSprite_SetFadeOut(dt->pNowRepo);   /* regardless of f_reposit (quirk) */
    w = dt->awmngp;   /* no NULL test (quirk) */
    masm_awmng_items(w, 0);
    w->stat = 2;
    masm_company_icon(dt, -1);   /* the FadeIn arm after the store is dead */
    DF_MOBJ2_SetMdlWinFadeOut(dt->mdlwinp);
    if (dt->ac_specwinp) masm_spec_fadeout(dt->ac_specwinp);   /* pointer kept */
    if (dt->genewinp) DF_MASMP_SetGeneWinFadeOut(dt->genewinp);
    DF_MASMD_SetDetailWinFadeOut(dt->detailwinp);   /* no NULL test (interpreted callee) */
    DF_M_AC_UpdateTmpPart(dt->acp, 0);
    DF_M_AC_SelectPart(dt->acp, dt->kind_idx, 0);
    DF_MVIEW_MotionStart(MViewp, 0xa);
    masm_guide5(4, 1, 2, 3, 0x63);
    DF_M1HELP_FadeOut();
    DF_MBACK_PopSceneLabel();
    ioObjp->stat = 2;
    ioObjp->play_func = (void (*)(void))GFN(__AsmSys_PlayFadeOut);
    ioObjp->count = 0;
}

static void __AsmSys_PlayFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    AsmSys *dt = masm_dt(ioObjp);
    int c;

    masm_arrow_blink();
    c = ioObjp->count;
    ioObjp->count = c + 1;
    if (c < 5) return;
    if (DF_M1HELP_GetStat() != 0) return;
    DF_MOBJ_SetDrawEnable(dt->pNowRepo, 1);
    M_ACp[(int)dt->player][1]->flag.draw = 0;
    M_ACp[(int)dt->player][2]->flag.draw = 0;
    DF_M_AC_SelectPart(M_ACp[(int)dt->player][0], dt->kind_idx, 1);
    ioObjp->stat = 0;
    ioObjp->play_func = (void (*)(void))GFN(__AsmSys_Play);   /* count not reset */
}

static void __AsmSys_CheckAlart(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    PS2_STACK(int[6], flag, 0x20);   /* handed to interpreted __MASM_SetAlartWinMngItems: PS2 memory */
    AsmSys *dt = masm_dt(ioObjp);
    struct struct_986e7f40 *fp;
    unsigned int err;
    int p, f;

    /* (the original zero-fills flag[] first; every word is overwritten below) */
    p = masm_kind_part_idx(dt);
    if (dt->part_id == dt->acp->part_id[p] && dt->part_id % 1000 != 0) {
        if (!dt->f_equip) {
            dt->f_equip = 1;
            if (dt->disp_kind == 0) UnitSprite_SetFadeIn(dt->pNowEq);
        }
    } else {
        if (dt->f_equip == 1) dt->f_equip = 0;
        UnitSprite_SetFadeOut(dt->pNowEq);   /* on every call of this arm */
    }

    err = dt->acp->connect.tmp_equip_err;
    f = dt->acp->connect.tmp.flag.no_core;   /* default: always the tmp flag */
    switch (dt->kind_idx) {   /* jump table 0x1ee4e00, 16 entries */
    case 1:
        f = 0;
        break;
    case 4:
        if (err & 8) f = 1;
        break;
    case 8: case 9:
        if (err & 1) f = 1;
        break;
    case 0xc: case 0xd: case 0xe: case 0xf:
        if ((err & 1) || (err & 2) || (err & 0x10) || (err & 0x20) || (err & 0x40)) f = 1;
        break;
    default:
        break;
    }
    if (dt->f_disable != f) dt->f_disable = f & 1;

    fp = (dt->acp->flag.tmp_connect == 1) ? &dt->acp->connect.tmp.flag : &dt->acp->connect.cur.flag;
    flag[0] = fp->overweight;
    flag[1] = fp->core_ow;
    flag[2] = fp->ene_low;
    flag[3] = fp->no_core;
    flag[4] = fp->incomplete;
    flag[5] = dt->f_disable;
    __MASM_SetAlartWinMngItems(dt->awmngp, flag);
}

static void __AsmSys_ChangeGeneWin2(struct stMobj *ioObjp, int bEquipedPart, int bEquip)
{
    PS2_FRAME(0x70);
    AsmSys *dt = masm_dt(ioObjp);
    struct _stMModel *partp;
    struct StAcAsmTune *pTune;
    void *datap, *wepp;

    partp = masm_detail_part(dt, bEquipedPart, &pTune);   /* partp / pTune both keyed on bEquipedPart */
    datap = (masm_kind_has_data(dt->kind_idx) && partp) ? masm_part_data(partp, 1) : 0;
    wepp = (dt->kind_idx == 2 && partp) ? masm_part_data(partp, 2) : 0;
    if (dt->genewinp)   /* wepp lands in the callee's unused 5th slot */
        DF_MASMP_ChangeGeneWin(dt->genewinp, dt->kind_tbl[(int)dt->kind_idx], partp ? partp->prm.part.infop : 0, datap,
                               wepp, bEquip, pTune);
}

static void __AsmSys_CreateGeneWin2(struct stMobj *ioObjp, int bEquip)
{
    PS2_FRAME(0x80);
    PS2_STACK(struct stMenuRect, gene_rect, 0x70);   /* handed to interpreted MASMP: PS2 memory */
    PS2_STACK(unsigned long long, arg9, 0);          /* the 9th argument (pTune) on the PS2 stack */
    AsmSys *dt = masm_dt(ioObjp);
    struct _stMModel *partp;
    struct StAcAsmTune *pTune;
    void *datap, *wepp;

    gene_rect.x = 24;   /* 0x1edae20 */
    gene_rect.y = 306;
    gene_rect.w = 270;
    gene_rect.h = 96;
    partp = masm_detail_part(dt, bEquip, &pTune);
    datap = (masm_kind_has_data(dt->kind_idx) && partp) ? masm_part_data(partp, 1) : 0;
    wepp = (dt->kind_idx == 2 && partp) ? masm_part_data(partp, 2) : 0;
    if (dt->genewinp) DF_MASMP_DeleteGeneWin(dt->genewinp);
    arg9 = PS2PTR(pTune);
    (void)arg9;
    dt->genewinp = DF_MASMP_NewGeneWin(dt->pStrFence, &gene_rect, dt->kind_tbl[(int)dt->kind_idx], 1,
                                       partp ? partp->prm.part.infop : 0, datap, wepp, dt->hlist, pTune);
    DF_MASMP_SetGeneWinFadeIn(dt->genewinp);
}

/* ---- group 5: DF_MASM_NewSystem, the AC spec window, the alert window manager, the arrow blink ---- */

typedef struct struct_460ce172 AsmSpecWin;     /* AC spec window dt (0x70) */
typedef struct struct_8ccb27e4 AsmAlartMng;    /* alert window manager dt (0x34) */
typedef struct struct_e350d6fe AsmAlartWin;    /* alert window dt (8) */

static inline AsmSpecWin *masm_spec_dt(struct stMobj *o) { return (AsmSpecWin *)&o->dt; }
static inline AsmAlartMng *masm_awmng_dt(struct stMobj *o) { return (AsmAlartMng *)&o->dt; }
static inline AsmAlartWin *masm_awin_dt(struct stMobj *o) { return (AsmAlartWin *)&o->dt; }

/* s_aszAlartSprLayoutName (0x1edab40) / s_aCompanyIconName (0x1edab88): constant name tables */
static const char *const masm_alart_spr_name[6] = {"juryokata_0", "wanbujuryo_0", "syuturyokubusoku_0",
                                                   "equipped_0", "soubifukanzen_0", "soubifuno_0"};
static const char *const masm_company_icon_name[3] = {"kigyou_mir_0", "kigyou_cre_0", "kigyou_kis_0"};

/* NewACSpecWin rect[2] (0x1edaba0, indexed by inMode) and iteminfo[17] (0x1edabc0: strid, itemtype, detailonly, place) */
static const struct stMenuRect masm_spec_rect[2] = {{336, 306, 276, 100}, {336, 90, 276, 100}};
static const struct struct_aad44dad masm_spec_items[17] = {
    {0x18b01, 1, 0, 5}, {0x18b04, 2, 0, 5}, {0x18b05, 2, 0, 5}, {0x18b02, 1, 1, 5}, {0x18b03, 2, 0, 5},
    {0x18b1a, 1, 1, 6}, {0x18b14, 1, 1, 5}, {0x18b15, 1, 1, 5}, {0x18b18, 1, 0, 5}, {0x18b0c, 1, 1, 5},
    {0x18ba2, 1, 1, 5}, {0x18b0e, 3, 1, 5}, {0x18b16, 1, 1, 5}, {0x18b17, 1, 1, 5}, {0x18b19, 1, 1, 5},
    {0x18bcc, 1, 1, 5}, {0x18c7f, 1, 1, 5}};
/* SetACSpecInfo iteminfo template (0x1edacd0): only detailonly is non-zero */
static const int masm_spec_info_detailonly[17] = {0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1};

/* category string: move the current one to the fade-out slot (inline delete of the old fade-out copy) */
static inline void masm_ctgr_to_fo(AsmSys *dt)
{
    if (dt->ctgr_strp) {
        masm_delete(&dt->fo_ctgr_strp);
        dt->fo_ctgr_strp = dt->ctgr_strp;
        DF_MOBJ_SetFadeOut(dt->ctgr_strp);
        dt->ctgr_strp = 0;
    }
}

struct stMobj *DF_MASM_NewSystem(struct stMobj *inObjp, int inMode, int inPlayer)
{
    PS2_FRAME(0x190);
    PS2_STACK(struct _LAYOUT_RET_DATA, LayoutData, 0xf0);
    /* rodata / sdata copies (bit copies of ints / bytes) */
    struct stMenuRect win_rect = {30, 83, 260, 171};     /* 0x1edae00 -> sp+0x140 */
    struct stMenuRect mdl_rect = {30, 83, 260, 171};     /* 0x1edadf0 -> sp+0x150 */
    struct stMenuRect maker_rect = {236, 180, 64, 64};   /* 0x1edade0 -> sp+0x160 */
    static const int Index[3] = {0x19e13, 0x19e14, 0x19e15};   /* 0x1edae10: AC-number label string ids */
    struct struct_76b810ec col = {0x90, 0x90, 0x90, 0x80};          /* gp-0x77f8 */
    struct struct_76b810ec AsmStrCol2 = {0x65, 0x65, 0x65, 0x80};   /* gp-0x77f4 */
    struct struct_76b810ec col3 = {0x8c, 0x92, 0x92, 0x80};         /* gp-0x7800 */
    struct stMobj *objp, *w;
    AsmSys *dt;
    struct stMenuAC *acp;
    struct _stMModel *partp;
    struct _mhelp_param_st *hp;
    void *lo;
    char *info;
    unsigned char *help;
    int i, ii, ret, PartsNum, PartsSum = 0, acno;

    memset(psArrowBuf, 0, 0x10);   /* the four arrows only */
    objp = DF_MOBJ_GetNewObject(inObjp);
    if (!objp) return 0;
    /* dt is NOT cleared beyond GetNewObject's 0x50 bytes: only the fields below are written (stale slot memory) */
    dt = masm_dt(objp);
    objp->type = 0;
    objp->stat = 1;
    dt->mode = (char)inMode;
    dt->player = (char)inPlayer;
    dt->kind_idx = 0;
    dt->acp = M_ACp[(int)dt->player][0];
    dt->nCompanyIcon = -1;
    dt->cListMode = 0;
    ArrowBlinkCnt[3] = 0x3c;
    ArrowBlinkCnt[2] = 0x3c;
    ArrowBlinkCnt[1] = 0x3c;
    ArrowBlinkCnt[0] = 0x3c;
    AddBlinkAlpha[3] = 2;
    AddBlinkAlpha[2] = 2;
    AddBlinkAlpha[1] = 2;
    AddBlinkAlpha[0] = 2;
    dt->pname = (char *)DF_MMM_Alloc(2, 0x20);
    dt->fo_pname = (char *)DF_MMM_Alloc(2, 0x20);
    for (i = 0; i < 11; i++) dt->kind_tbl[i] = (char)i;
    dt->kind_tbl[11] = 10;
    dt->kind_tbl[12] = 11;
    dt->kind_tbl[13] = 12;
    dt->kind_tbl[14] = 11;
    dt->kind_tbl[15] = 12;
    dt->hlist = DF_MHELP_NewList(0);

    /* flags: only ever set here (the other bits keep the slot's stale value) */
    if (dt->acp->flag.reading == 1) dt->f_first_check = 1;
    ret = DF_M_AC_SetEquipNecessary(dt->acp);
    if (ret > 0) {
        dt->f_necessary_end = 1;
        dt->f_first_check = 1;
        if (ret == 1) dt->f_necessary = 1;
    }
    if (!dt->f_first_check) DF_M_AC_CheckEquipFlag(dt->acp, 0);   /* result discarded */

    dt->id_tablep = (struct stMPartIdTable *)DF_MMM_Alloc(1, 0x410);
    DF_MPART_MakeIDTableNx(dt->id_tablep, dt->cListMode ? 0x10000 : 0, dt->player, dt->kind_tbl[(int)dt->kind_idx],
                           dt->kind_idx);
    partp = dt->acp->real_part[0];
    dt->part_id = dt->acp->part_id[0];
    dt->kind_idx = 0;
    dt->idx = (char)DF_MPART_PartID2TableIdx(dt->id_tablep, dt->part_id);
    MenuMng.f_asem_detail_on = 0;
    dt->disp_kind = 0;
    dt->pre_part_id = dt->part_id;
    dt->pre_kind_idx = 0;
    dt->cMaker = -1;

    /* frames / sprites */
    dt->pACSpecWin = WindowEx_NewFromLayout(objp, (_WINDOWEX_TYPE)0, (char *)LAYOUT_GetLayoutData(0x3f2), (char *)"parts01a_0");
    dt->pACWakuWin = UnitSprite_New(objp, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x3f2), (char *)"parts07_0");
    dt->pPartsWakuWin = UnitSprite_New(objp, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x3f2), (char *)"parts04_0");
    dt->pPartsWakuWinInner = UnitSprite_New(objp, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x3f2), (char *)"parts04_1");
    dt->nCompanyIcon = -1;
    for (i = 0; i < 3; i++) {
        lo = LAYOUT_GetLayoutData(0x3f2);
        dt->apCompanyIcon[i] = UnitSprite_New(objp, (struct _LO_HEADER *)lo, (char *)masm_company_icon_name[i]);
        DF_MOBJ_SetDrawEnable(dt->apCompanyIcon[i], 0);
        dt->apCompanyIcon[i]->type = 7;
    }
    dt->pPartsSpecWin = WindowEx_NewFromLayout(objp, (_WINDOWEX_TYPE)0, (char *)LAYOUT_GetLayoutData(0x3f2), (char *)"parts01_0");
    dt->pCategoryWin = WindowEx_NewFromLayout(objp, (_WINDOWEX_TYPE)0, (char *)LAYOUT_GetLayoutData(0x3f2), (char *)"parts00_0");
    dt->pPartsNameWin = WindowEx_NewFromLayout(objp, (_WINDOWEX_TYPE)0, (char *)LAYOUT_GetLayoutData(0x3f2), (char *)"parts03_0");
    dt->pACNameWin = UnitSprite_New(objp, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x3f2), (char *)"parts06_0");
    dt->pStrFence = dt->pACNameWin;
    WindowEx_SetFadeIn(dt->pACSpecWin);
    UnitSprite_SetFadeIn(dt->pACWakuWin);
    UnitSprite_SetFadeIn(dt->pPartsWakuWin);
    UnitSprite_SetFadeIn(dt->pPartsWakuWinInner);
    UnitSprite_SetFadeIn(dt->pACNameWin);
    WindowEx_SetFadeIn(dt->pPartsSpecWin);
    WindowEx_SetFadeIn(dt->pCategoryWin);
    WindowEx_SetFadeIn(dt->pPartsNameWin);
    dt->pNowEq = UnitSprite_New(objp, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x3f2), (char *)"equipped_0");
    UnitSprite_SetFadeIn(dt->pNowEq);
    dt->pNowEq->type = 7;
    dt->pNowRepo = UnitSprite_New(objp, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x3f2), (char *)"reposit_0");
    UnitSprite_SetFadeOut(dt->pNowRepo);
    dt->pNowRepo->type = 7;
    DF_MOBJ_SetDrawEnable(dt->pNowRepo, 0);

    /* arrows (the bound is the layout's part count, not clamped to 4) */
    PartsNum = DF_LO_GetPartsNumInUnitFromName((unsigned char *)LAYOUT_GetLayoutData(0x3f2), (unsigned char *)"yajirushi");
    for (ii = 0; ii < PartsNum; ii++) {
        DF_LO_GetLayoutData((unsigned char *)LAYOUT_GetLayoutData(0x3f2), (unsigned char *)"yajirushi", ii, &LayoutData);
        psArrowBuf[ii] = DF_MOBJ_NXNewWindow(objp, 1, 1, (struct stMenuRect *)&LayoutData.Pos, &LayoutData.Col, 0,
                                             LayoutData.UV, LayoutData.TexID, LayoutData.Blend);
        psArrowBuf[ii]->type = 10;
        DF_MOBJ_SetFadeIn(psArrowBuf[ii]);
        ArrowSaveAlpha[ii] = (float)(unsigned int)LayoutData.Col.a;
    }
    PartsSum += PartsNum;
    DF_MBACK_PushSceneLabel((enum_5124d09c)0x1f);

    /* AC number / name labels */
    acno = (signed char)PlayerInfo_GetCurrentAcNo(inPlayer);
    lo = LAYOUT_GetLayoutData(0x3f2);
    dt->pAcNo = DF_MOBJ_NewStringFromLayoutName(dt->pStrFence, 0, Index[acno], 0, (char *)lo, (char *)"parts20e_1",
                                                &AsmStrCol2, 0, 0);
    DF_MOBJ_SetFadeIn(dt->pAcNo);
    dt->pAcName = DF_MOBJ_NewStringBaseFromLayoutName(dt->pStrFence, (char *)LAYOUT_GetLayoutData(0x3f2),
                                                      (char *)"parts20g_10", &AsmStrCol2, 0, 0);
    acno = PlayerInfo_GetCurrentAcNo(inPlayer);
    DF_MOBJ_SetStringCopy(dt->pAcName, PlayerInfo_GetAcName(dt->player, (char)acno));
    DF_MOBJ_SetFadeIn(dt->pAcName);
    dt->CatNameNum = PartsSum - 1;

    /* category string: ChangeCategoryStr's body inline (dead pre-delete at creation; f_help NOT set here) */
    masm_ctgr_to_fo(dt);
    lo = LAYOUT_GetLayoutData(0x3f2);
    dt->ctgr_strp = DF_MOBJ_NewStringFromLayoutName(dt->pStrFence, 0, dt->kind_idx + 0x18aec, 0, (char *)lo,
                                                    (char *)"parts20_0", &col3, 0, 0);
    DF_MOBJ_SetFadeIn(dt->ctgr_strp);
    DF_MHELP_AddParamToList(dt->hlist, DF_MHELP_NewParam(0, 0x18aec, dt->ctgr_strp));

    /* part name */
    lo = LAYOUT_GetLayoutData(0x3f2);
    dt->pname_strp = DF_MOBJ_NewStringDirectFromLayoutName(dt->pStrFence, dt->pname, (char *)lo, (char *)"parts20c_0",
                                                           &col, 0, 0);
    __AsmSys_UpdateTunedStr(objp);   /* reads the stale pTuned (quirk) */
    DF_MOBJ_SetFadeIn(dt->pname_strp);
    hp = DF_MHELP_NewParam(0, 0x18a89, dt->pname_strp);
    DF_MHELP_AddParamToList(dt->hlist, hp);
    if (DF_MMODEL_GetPartsName(partp)) {
        strncpy(dt->pname, DF_MMODEL_GetPartsName(partp), 0x1f);   /* no terminator written */
    } else {
        dt->pname[0] = 0;
        DF_MHELP_SetParamEnable(hp, 0);
    }

    /* company icon */
    info = partp ? (char *)partp->prm.part.infop : 0;
    if (info) {
        dt->cMaker = info[0x28];
        masm_company_icon(dt, dt->cMaker);
    }

    DF_M_AC_SetFadeOut(M_ACp[(int)dt->player][1], 5, 0.3f);
    DF_M_AC_SetFadeOut(M_ACp[(int)dt->player][2], 5, 0.3f);

    /* model window (sp+0x28 = the unused rodata pointer "parts_e_0" in the original) */
    dt->mdlwinp = DF_MOBJ2_NexusNewMdlWin(objp, 1, &win_rect, &mdl_rect, &maker_rect,
                                          (struct struct_c4910010 *)M_ACp[(int)dt->player][0]->colp, 0, 1, dt->part_id, 0,
                                          0, dt->player, 0, 0, 1, 0.5f, 0.0f, 1, 1.0f);
    DF_MOBJ2_SetMdlWinFadeIn(dt->mdlwinp);
    DF_MVIEW_MotionStart(MViewp, 0x14);
    acp = dt->acp;
    M_AC_UpdateTuneStat(acp, 0);
    M_AC_UpdateTuneStat(dt->acp, 1);
    __AsmSys_CreateGeneWin2(objp, 1);
    DF_MASMP_SetGeneWinFadeIn(dt->genewinp);

    /* AC spec window (always mode 0 here); NULL would be dereferenced (original) */
    dt->ac_specwinp = __MASM_NewACSpecWin(objp, M_ACp[(int)dt->player][0], 0, 0);
    masm_spec_fadein(dt->ac_specwinp);

    /* alert window manager (stored even when NULL) */
    w = DF_MOBJ_GetNewObject(objp);
    if (w) {
        w->type = 0;
        w->play_func = (void (*)(void))GFN(__MASM_PlayAlartWinMng);
    }
    dt->awmngp = w;

    /* help text: called with NULL when the part has no info record */
    info = partp ? (char *)partp->prm.part.infop : 0;
    help = info ? (unsigned char *)info + MenuMng.language * 0x49 + 0x29 : 0;
    DF_M1HELP_ChangeString(help);

    objp->play_func = (void (*)(void))GFN(__AsmSys_PlayFadeIn);
    objp->draw_func = (void (*)(void))GFN(__AsmSys_Draw);
    objp->pkt_func = (void (*)(void))GFN(__MakeVu1PacketAssem);
    objp->count = 0;
    dt->step = 0;
    __AsmSys_CheckAlart(objp);
    masm_guide5(1, 2, 3, 0xf, 0x63);
    return objp;
}

static void __MASM_PlayACSpecWin(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    AsmSpecWin *dt = masm_spec_dt(ioObjp);
    int k;

    if (dt->f_end == 1) {
        masm_delete(&dt->winp);
        masm_delete(&dt->strp);
        masm_delete(&dt->linep);
        for (k = 0; k < 17; k++) {
            DF_MOBJ2_DeleteItem(dt->itemp[k]);
            dt->itemp[k] = 0;
        }
        DF_MHELP_DeleteList(dt->hlist);   /* hlist not cleared */
        if (ioObjp) masm_erase(ioObjp);
        return;
    }
    if (dt->f_set == 0 && !dt->acp->flag.reading) {
        __MASM_SetACSpecInfo(ioObjp);
        dt->f_set = 1;
    }
    switch (ioObjp->stat) {
    case 2:
        ioObjp->count++;
        if (ioObjp->count >= 7) ioObjp->stat = 5;
        break;
    case 1:
        ioObjp->count++;
        if (ioObjp->count >= 7) ioObjp->stat = 0;
        break;
    default:
        break;
    }
}

static void __ChangeCategoryStr(struct stMobj *inObjp)
{
    PS2_FRAME(0x40);
    AsmSys *dt = masm_dt(inObjp);
    struct struct_76b810ec col = {0x8c, 0x92, 0x92, 0x80};   /* gp-0x77fc */
    struct _mhelp_param_st *p;
    void *lo;

    masm_ctgr_to_fo(dt);
    lo = LAYOUT_GetLayoutData(0x3f2);
    dt->ctgr_strp = DF_MOBJ_NewStringFromLayoutName(dt->pStrFence, 0, dt->kind_idx + 0x18aec, 0, (char *)lo,
                                                    (char *)"parts20_0", &col, 0, 0);
    DF_MOBJ_SetFadeIn(dt->ctgr_strp);
    dt->ctgr_strp->f_help = 1;
    p = DF_MHELP_GetParam(dt->hlist, 0);   /* unchecked; ref_obj keeps the old string (quirk) */
    p->ctgrID = 0;
    p->strID = dt->kind_idx + 0x18aec;
}

static void __MakeVu1PacketAcSpec(void)
{
}

static void __MASM_SetACSpecInfo(struct stMobj *ioObjp)
{
    PS2_FRAME(0x180);
    AsmSpecWin *dt = masm_spec_dt(ioObjp);
    struct struct_76b810ec col2 = {0xfc, 0x46, 0x46, 0x80};   /* gp-0x7804, sp+0x178 */
    struct struct_76b810ec col1 = {0x90, 0x90, 0x90, 0x80};   /* gp-0x7808, sp+0x17c */
    struct struct_73e71405 iteminfo[17];
    struct struct_dbbd570b *specp;
    int i, j;

    specp = (struct struct_dbbd570b *)DF_MMM_Alloc(2, 0x9c);
    DF_MACSPEC_Get(specp, dt->acp, 1);
    for (i = 0; i < 17; i++) {
        iteminfo[i].detailonly = masm_spec_info_detailonly[i];
        iteminfo[i].val1 = 0;
        iteminfo[i].val2 = 0;
        iteminfo[i].col1 = 0;
    }
    iteminfo[0].val1 = specp->ap;
    iteminfo[1].val1 = specp->core_wp_max;
    iteminfo[1].val2 = specp->core_wp;
    iteminfo[1].col1 = (specp->core_wp_max < specp->core_wp) ? &col2 : &col1;
    iteminfo[2].val1 = specp->legs_wp_max;
    iteminfo[2].val2 = specp->legs_wp;
    iteminfo[2].col1 = (specp->legs_wp_max < specp->legs_wp) ? &col2 : &col1;
    iteminfo[3].val1 = specp->wp;
    iteminfo[4].val1 = specp->ep_max;
    iteminfo[4].val2 = specp->ep;
    iteminfo[4].col1 = (specp->ep_max < specp->ep) ? &col2 : &col1;
    iteminfo[5].val1 = specp->max_charge;
    iteminfo[6].val1 = specp->effic_ep;
    iteminfo[7].val1 = specp->boost_time;
    iteminfo[8].val1 = specp->boost_max_speed;
    iteminfo[9].val1 = specp->rot_speed;
    iteminfo[10].val1 = specp->stability;
    iteminfo[11].val1 = specp->recov_cooling;
    iteminfo[11].val2 = specp->cooling;
    iteminfo[12].val1 = specp->eccm_perform;
    iteminfo[13].val1 = specp->op;
    iteminfo[14].val1 = specp->lockon_proper;
    iteminfo[15].val1 = specp->def_shell;
    iteminfo[16].val1 = specp->def_energy;

    j = 0;
    for (i = 0; i < 17; i++) {
        if (iteminfo[i].detailonly == 0 || dt->f_mode != 0) {
            struct struct_76b810ec *c = iteminfo[i].col1 ? iteminfo[i].col1 : &col1;
            DF_MOBJ2_ChangeItemParamEx(dt->itemp[j], iteminfo[i].val1, iteminfo[i].val2, &col1, c);   /* color1 always col1 */
            j++;
        }
    }
    DF_MMM_Free(2, specp);
}

static struct stMobj *__MASM_NewACSpecWin(struct stMobj *inObjp, struct stMenuAC *inACp, int unused, int inMode)
{
    PS2_FRAME(0x230);
    PS2_STACK(struct _LAYOUT_RET_DATA, Layout, 0x1a0);
    struct struct_76b810ec GeneLineCol = {0x49, 0x49, 0x49, 0x80};   /* gp-0x780c */
    struct struct_76b810ec title_col = {0x65, 0x65, 0x65, 0x80};     /* gp-0x7810 */
    struct stMenuRect i_rect;
    struct stMobj *objp;
    AsmSpecWin *dt;
    unsigned int i;
    int j;

    (void)unused;
    objp = DF_MOBJ_GetNewObject(inObjp);
    if (!objp) return 0;
    dt = masm_spec_dt(objp);
    objp->type = 0;
    dt->pParent = inObjp;
    dt->rect = masm_spec_rect[inMode];   /* inMode unchecked */
    dt->acp = inACp;
    dt->f_mode = (char)inMode;
    dt->hlist = DF_MHELP_NewList(0);
    if (inMode == 0) {   /* title + underline; mode 1 leaves strp / linep stale */
        DF_LO_GetLayoutData((unsigned char *)LAYOUT_GetLayoutData(0x3f2), (unsigned char *)"parts20e_2", 0, &Layout);
        dt->strp = DF_MOBJ_NewStringFromLayout(dt->pParent, 0, 0x18b00, 0, &Layout, &title_col, 1, 0x800);
        Layout.Pos.y += 2;
        dt->linep = DF_MOBJ_NewStringDirectFromLayout(dt->pParent, masm_spec_line, &Layout,
                                                      &GeneLineCol, 0, 0x800);
    }
    i_rect = dt->rect;
    j = 0;
    for (i = 0; i < 17; i++) {
        const struct struct_aad44dad *it = &masm_spec_items[i];
        if (it->detailonly == 0 || inMode != 0) {
            dt->itemp[j] = DF_MOBJ2_NewItem(objp, &i_rect, it->itemtype, 4, 7, it->strid, it->place, 0, 0, 1);
            /* item obj+0x30 = the item's label string (pLabel[0]) */
            DF_MHELP_AddParamToList(dt->hlist,
                                    DF_MHELP_NewParam(0, it->strid, *(struct stMobj **)((char *)dt->itemp[j] + 0x30)));
            i_rect.y += 0x12;
            j++;
        }
    }
    if (!dt->acp->flag.reading) {
        __MASM_SetACSpecInfo(objp);
        dt->f_set = 1;
    }
    objp->play_func = (void (*)(void))GFN(__MASM_PlayACSpecWin);
    objp->pkt_func = (void (*)(void))GFN(__MakeVu1PacketAcSpec);   /* no draw_func; stat stays 0 */
    return objp;
}

static void __MASM_PlayAlartWinMng(struct stMobj *ioObjp)
{
    AsmAlartMng *dt = masm_awmng_dt(ioObjp);
    int i, busy = 0;

    if (dt->f_end == 1) {
        for (i = 0; i < 6; i++) {
            if (dt->awinp[i]) masm_awin_dt(dt->awinp[i])->f_end = 1;
            if (dt->fo_awinp[i]) masm_awin_dt(dt->fo_awinp[i])->f_end = 1;
        }
        if (ioObjp) masm_erase(ioObjp);
        return;
    }
    switch (ioObjp->stat) {
    case 2:
        for (i = 0; i < 6; i++)
            if (dt->awinp[i] && dt->awinp[i]->stat != 5) busy = 1;
        if (!busy) {
            ioObjp->stat = 5;
            ioObjp->f_draw = 0;
        }
        break;
    case 1:
        for (i = 0; i < 6; i++)
            if (dt->awinp[i] && dt->awinp[i]->stat != 0) busy = 1;
        if (!busy) ioObjp->stat = 0;
        break;
    default:
        break;
    }
    for (i = 0; i < 6; i++)
        if (dt->awinp[i]) dt->awinp[i]->f_draw = ioObjp->f_draw;
}

static void __MASM_SetAlartWinMngItems(struct stMobj *ioObjp, int *inFlagp)
{
    PS2_FRAME(0x80);
    AsmAlartMng *dt = masm_awmng_dt(ioObjp);
    struct stMobj *w;
    int i;

    for (i = 0; i < 6; i++) {
        if (dt->fo_awinp[i]) masm_awin_dt(dt->fo_awinp[i])->f_end = 1;
        dt->fo_awinp[i] = 0;
        if (inFlagp[i] == 1) {
            if (dt->awinp[i] == 0) {   /* an existing window is left as it is */
                w = DF_MOBJ_GetNewObject(ioObjp);
                if (w) {
                    w->type = 0;
                    masm_awin_dt(w)->pAlertSpr = UnitSprite_New(w, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x3f2),
                                                                (char *)masm_alart_spr_name[i]);
                    masm_awin_dt(w)->pAlertSpr->type = 7;
                    w->play_func = (void (*)(void))GFN(__MASM_PlayAlartWin);
                }
                dt->awinp[i] = w;   /* stored even when NULL */
                w = dt->awinp[i];
                if (w) {
                    UnitSprite_SetFadeIn(masm_awin_dt(w)->pAlertSpr);
                    w->stat = 1;
                    w->count = 0;
                }
            }
        } else if (dt->awinp[i]) {
            dt->fo_awinp[i] = dt->awinp[i];
            dt->awinp[i] = 0;
            w = dt->fo_awinp[i];
            if (w) {
                UnitSprite_SetFadeOut(masm_awin_dt(w)->pAlertSpr);
                w->stat = 2;
                w->count = 0;
            }
        }
    }
}

static void __MASM_PlayAlartWin(struct stMobj *ioObjp)
{
    PS2_FRAME(0x20);
    AsmAlartWin *dt = masm_awin_dt(ioObjp);

    if (dt->f_end == 1) {
        UnitSprite_Delete(dt->pAlertSpr);
        if (ioObjp) masm_erase(ioObjp);
        return;
    }
    switch (ioObjp->stat) {
    case 2:
        ioObjp->count++;
        if (ioObjp->count >= 7) ioObjp->stat = 5;
        break;
    case 1:
        ioObjp->count++;
        if (ioObjp->count >= 7) ioObjp->stat = 0;
        break;
    default:
        break;
    }
    dt->pAlertSpr->f_draw = ioObjp->f_draw;
}

/* blink the four arrows: alpha byte = the arrow window's cur_col.a (obj+0x53) */
static void __ArrowBlink(void)
{
    int ii, cnt, add, al;
    unsigned char *ap;

    for (ii = 0; ii < 4; ii++) {
        cnt = ArrowBlinkCnt[ii];
        if (cnt > 0) {
            ArrowBlinkCnt[ii] = cnt - 1;
            ap = (unsigned char *)psArrowBuf[ii] + 0x53;
            add = AddBlinkAlpha[ii];
            al = *ap;
            if (al + add < 0xff) {
                *ap = (unsigned char)(al + (add & 0xff));
                /* effectively dead: the stored byte is < 0xff (kept) */
                if (((unsigned char *)psArrowBuf[ii])[0x53] == 0xff && ArrowBlinkCnt[ii] >= 0xb) ArrowBlinkCnt[ii] = 10;
            }
        } else {
            AddBlinkAlpha[3] = 2;
            AddBlinkAlpha[2] = 2;
            AddBlinkAlpha[1] = 2;
            AddBlinkAlpha[0] = 2;
            ap = (unsigned char *)psArrowBuf[ii] + 0x53;
            add = AddBlinkAlpha[ii];
            al = *ap;
            if (al - add < 0x3d) {
                ArrowBlinkCnt[0] = 0x3c;
                ArrowBlinkCnt[1] = 0x3c;
                ArrowBlinkCnt[2] = 0x3c;
                ArrowBlinkCnt[3] = 0x3c;
            } else {
                *ap = (unsigned char)(al - (add & 0xff));
            }
        }
    }
}
