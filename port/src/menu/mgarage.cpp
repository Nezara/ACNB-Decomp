/* src/menu/mgarage.cpp - the garage top-level system (OLMENU): one stMobj "garage system" (struct_9546159b) whose
 * state machine runs the top menu (a SubSys "def" child with the item labels and the rotating AC), checks whether
 * the chosen item may be entered (__IsEnterOp / __IsEnterAsm / the per-item checks of __PlayGarageSys_PadCheckWait),
 * fades the top menu out, creates the chosen sub-system (Assembly, Performance, OP, Paint, Cockpit, Name, System /
 * AC Test) and rebuilds the top menu when the child finishes. Run by DF_MOBJ_Play (play_func per frame).
 * Native so far: g1 the state machine and the enter checks (DF_MGARAGE_SetMissionAc, __GarageSys_PlayAcTestDlg,
 * __PlayGarageSysDispatch, __PlayGarageSysFadeOut, __IsEnterOp, __IsEnterAsm, __PlayGarageSys_PadCheckWait2,
 * __PlayGarageSys_PadCheckWait, __PlayGarageSys); g2 the System object's creation / fade-in / fade-out / draw
 * (DF_MGARAGE_NewSystem, __PlaySystemFadeIn, __SetGarageSysFadeOut, __DrawGarageSystem, __MakeVu1PackeGGeneWin) and
 * the top-menu label object (MenuGarage_TopLabelInit, __MenuGarageTop_{CreateItem, Init, Play, FadeOut,
 * DeleteCursor}: 14 sprites + 7 strings sliding in / out with a 7-deep offset history, plus the row cursor);
 * g3 the top menu SubSys "def" object (__NewSubSysDef, __PlaySubSysDef{FadeIn, , RotateAC, FadeOut}: row cursor,
 * help line, L1 / R1 AC slot switch with the 15-frame circle rotation). The whole file is native.
 * The versus-mode test is the original's (modes 1 / 5 / 6); game.versus_garage reaches PadCheckWait's Paint / Name
 * tests through hook_garage_item_mode, the top labels through hook_garage_top_create_item and the SubSys help ids
 * through hook_garage_new_sub_sys_def (acnb/hooks.h). */
#include <string.h>
#include "acnb/sdk.h"
#include "acnb/hooks.h"
#include "acnb/ps2fp.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU 0x1ede1b0: the top label layout parts ("ita_garage0..6" OLMENU 0x1ede0d0, "mark0..6" main sdata 0x27b7f0,
 * "string_menu0..6" OLMENU 0x1ede140, "cursor" main sdata 0x27b828) */
char *sGarageTopLayout[22] = {
    (char *)"ita_garage0",  (char *)"ita_garage1",  (char *)"ita_garage2",  (char *)"ita_garage3",
    (char *)"ita_garage4",  (char *)"ita_garage5",  (char *)"ita_garage6",  (char *)"mark0",
    (char *)"mark1",        (char *)"mark2",        (char *)"mark3",        (char *)"mark4",
    (char *)"mark5",        (char *)"mark6",        (char *)"string_menu0", (char *)"string_menu1",
    (char *)"string_menu2", (char *)"string_menu3", (char *)"string_menu4", (char *)"string_menu5",
    (char *)"string_menu6", (char *)"cursor",
};
#endif

#define MGARAGE_SYS(o) ((struct struct_9546159b *)&(o)->dt)
#define MGARAGE_SSYS(o) ((struct struct_2857848f *)&(o)->dt)
#define MGARAGE_LABEL(o) ((struct _mobj_menu_garage_top_label_st *)&(o)->dt)
#define MGARAGE_PLAY(f) ((void (*)(void))GFN(f))

/* garage modes 1 / 5 / 6 are the versus modes (compiled as `(m - 1) <u 6 && (1 << (m - 1)) & 0x31`) */
static inline int mgarage_versus(int mode)
{
    return mode == 1 || mode == 5 || mode == 6;
}

/* DF_MBGUIDE_Change is void(void) in DWARF; the key list ends with 0x63 */
static inline void mgarage_guide2(int a, int b)
{
    gcall_as<int(int, int)>(GFN(DF_MBGUIDE_Change), a, b);
}

static inline void mgarage_guide5(int a, int b, int c, int d, int e)
{
    gcall_as<int(int, int, int, int, int)>(GFN(DF_MBGUIDE_Change), a, b, c, d, e);
}

/* child->f_draw = 0; child->f_erase = 1; slot = NULL (two byte RMWs, the slot re-read between them) */
static inline void mgarage_erase(struct stMobj **slot)
{
    (*slot)->f_draw = 0;
    (*slot)->f_erase = 1;
    *slot = NULL;
}

/* the top menu's labels start sliding out (PadCheckWait's enter arm, __SetGarageSysFadeOut); ss->label is
 * re-read for the count store and for MoveMng_Init */
static inline void mgarage_label_fade_out(struct struct_2857848f *ss)
{
    if (ss->label) {
        ss->label->play_func = MGARAGE_PLAY(__MenuGarageTop_FadeOut);
        ss->label->count = 0;
        struct _mobj_menu_garage_top_label_st *w = MGARAGE_LABEL(ss->label);
        int i;
        for (i = 0; i < 14; i++)
            UnitSprite_SetFadeOut(w->label[i]);
        for (; i < 21; i++)
            DF_MOBJ_SetFadeOut(w->label[i]);
        UnitSprite_SetFadeOut(w->cursor);
        MoveMng_Init(&MGARAGE_LABEL(ss->label)->move);
    }
}

/* body of DF_MGARAGE_SetMissionAc, also inlined by MWCC into __PlayGarageSys: no frame of its own. The tune
 * destination, the source AC and the (ignored) AC number come from three separate GetCurrentAcNo calls (quirk 7). */
static inline void mgarage_set_mission_ac(int player)
{
    struct stMenuAC **tbl = &M_ACTable[player];
    struct stMenuAC *acp1 = *tbl + (signed char)PlayerInfo_GetCurrentAcNo(player);
    struct stMenuAC *acp2 = *tbl + (signed char)PlayerInfo_GetCurrentAcNo(player);
    (void)PlayerInfo_GetCurrentAcNo(player);   /* a3 of the next call, unused by the callee */
    M_AC_GetTuneStatFromPlayerInfoByPartId(&acp1->real_tune, acp2, player);
    DF_M_AC_SetMissAC(player, *tbl + (signed char)PlayerInfo_GetCurrentAcNo(player));
    for (int i = 0; i < 3; i++)
        DF_M_AC_SetPlayerInfo(player, i);
    PlayerInfo_SetCurrentAcNo(player, MenuMng.player[player].ac_idx);
}

void DF_MGARAGE_SetMissionAc(int player)
{
    PS2_FRAME(0x50);
    mgarage_set_mission_ac(player);
}

/* the AC-test confirmation dialog (non-versus item 6) */
static void __GarageSys_PlayAcTestDlg(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct struct_9546159b *dt = MGARAGE_SYS(pMobj);

    int ret = DF_MDIALOG_Sync();
    dt->f_ondialog = 0;   /* delay slot: both paths */
    if (!(ret & 4))
        return;
    if ((ret & 0x10) == 0x10) {
        MenuMng.stat = 5;
        g_nextstage.stage = 0xb0;
        g_nextstage.sortie_mode = 1;
        g_nextstage.dobj_situ = 1;
        g_nextstage.chr_situ = 1;
        g_nextstage.ev_situ = 1;
        __SetGarageSysFadeOut(pMobj);
        Bgm_FadeOutStart();
    } else {
        dt->cur_ssysp->play_func = MGARAGE_PLAY(__PlaySubSysDef);
        pMobj->play_func = MGARAGE_PLAY(__PlayGarageSys);
    }
}

/* waits for dialogs, for the old top menu (stat 2) and for the item sub-system (stat 3), then rebuilds the top menu */
static void __PlayGarageSysDispatch(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    struct struct_9546159b *dt = MGARAGE_SYS(ioObjp);

    if (dt->f_ondialog != 0) {
        if (DF_MDIALOG_Sync() & 4) {
            dt->f_ondialog = 0;
            if (MenuMng.stat == 0xc) {
                dt->f_ondialog = 0;
                __SetGarageSysFadeOut(ioObjp);
            } else if (dt->f_addpartsdlg != 0) {   /* never cleared (quirk 2) */
                dt->f_ondialog = 0;
                MPadUser = dt->bk_mpaduser;
                ioObjp->play_func = MGARAGE_PLAY(__PlaySystemFadeIn);
            }
        }
        return;
    }

    if (ioObjp->stat == 2) {
        struct stMobj *child = dt->cur_ssysp;
        if (child->stat != 5)   /* read before the null test (quirk 1) */
            return;
        if (child)
            mgarage_erase(&dt->cur_ssysp);
        ioObjp->stat = 3;
        return;
    }

    if (dt->func_sysp == NULL) {   /* a refused item's dialog */
        if (DF_MDIALOG_Sync() == 4) {
            {
                /* 9 arguments: the 9th (0x63) is read from the caller's sp+0 */
                PS2_STACK(unsigned long long, arg9, 0);
                arg9 = 0x63;
                (void)arg9;
                gcall_as<int(int, int, int, int, int, int, int, int)>(GFN(DF_MBGUIDE_Change), dt->btnkind[0],
                    dt->btnkind[1], dt->btnkind[2], dt->btnkind[3], dt->btnkind[4], dt->btnkind[5], dt->btnkind[6],
                    dt->btnkind[7]);
            }
            dt->cur_ssysp->play_func = MGARAGE_PLAY(__PlaySubSysDef);
            ioObjp->play_func = MGARAGE_PLAY(__PlayGarageSys);
        }
        return;
    }

    struct stMobj *child = dt->func_sysp;
    if (child->stat != 5)
        return;
    if (child)
        mgarage_erase(&dt->func_sysp);

    int stat = MenuMng.stat;
    if (stat == 0xc || stat != 0) {
        __SetGarageSysFadeOut(ioObjp);
        return;
    }

    int mode = dt->mode;
    dt->cur_ssysp = __NewSubSysDef(ioObjp, 1, (mode == 6 || mode == 2) ? 1 : 0, dt->player, dt->func_idx, mode, 0);
    ioObjp->stat = 1;
    ioObjp->play_func = MGARAGE_PLAY(__PlayGarageSys);
    if (dt->f_performance == 0)
        DF_MVIEW_MotionStart(MViewp, dt->viewID);
    M_ACp[(int)dt->player][0]->flag.rotate = 1;
    DF_M_AC_SetFadeIn(M_ACp[(int)dt->player][1], 0xf, 0.0f);
    DF_M_AC_SetFadeIn(M_ACp[(int)dt->player][2], 0xf, 0.0f);
    dt->drawtexstring = 1;
    DF_MBGUIDE_Show();
    if (dt->f_performance == 0 &&
        (dt->func_idx == 4 || dt->func_idx == 2 || (dt->func_idx == 6 && mgarage_versus(dt->mode)))) {
        DF_M_AC_SetFadeIn(M_ACp[(int)dt->player][0], 0xf, 0.0f);
        DF_MBACK_SetSpotLightFadeIn();
    }
    if (mgarage_versus(dt->mode) && MenuMng.stat == 0 && dt->func_idx == 6)
        mgarage_guide5(4, 1, 2, 3, 0x63);
    dt->f_performance = 0;
}

/* leaving the garage: wait for the top menu, hide / reset the ACs, restore the menu work area */
static void __PlayGarageSysFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct struct_9546159b *dt = MGARAGE_SYS(ioObjp);

    switch (ioObjp->count) {
    case 0: {
        struct stMobj *child = dt->cur_ssysp;
        if (child) {
            if (child->stat != 5)
                return;
            if (child)
                mgarage_erase(&dt->cur_ssysp);
        }
        for (int i = 0; i < 3; i++) {
            if (dt->mode != 0 || i != 0)
                M_ACp[(int)dt->player][i]->flag.draw = 0;
        }
        ioObjp->count++;
        break;
    }
    case 1:
        for (int i = 0; i < 3; i++) {
            /* mode 0, AC 0: reset only while it is reading */
            if (dt->mode == 0 && i == 0 && !M_ACp[(int)dt->player][0]->flag.reading)
                continue;
            DF_M_AC_Reset(M_ACp[(int)dt->player][i]);
        }
        if (dt->mode != 0) {
            DF_MMD_DeleteMMDByKind(1);
            DF_MMD_ReleaseGeneralMMD();
        }
        MenuWorkp = dt->MenuWorkp_save;
        MenuMng.f_use_readbuff = 0;
        if (MenuMng.stat == 4) {
            Game_SetBgmVolume(dt->player, Game_GetOptionValue(dt->player, 1));
            Game_SetSeVolume(dt->player, Game_GetOptionValue(dt->player, 6));
        }
        if (MenuMng.stat == 4 && mgarage_versus(dt->mode)) {
            ioObjp->count++;
            return;
        }
        ioObjp->stat = 5;
        ioObjp->play_func = NULL;
        DF_MBACK_PopSceneLabel();
        break;
    case 2:
        f_versus_mcload = 1;   /* delay slot of the PopSceneLabel call (quirk 5) */
        DF_MBACK_PopSceneLabel();
        DF_MGARAGE_NewSystem(ioObjp, dt->mode, dt->player, dt->padMode);
        break;
    default:
        break;
    }
}

/* the dialog every refused item opens (func_sysp = NULL: Dispatch waits for it) */
static inline void mgarage_refuse_dialog(struct stMobj *ioObjp, struct struct_9546159b *dt, unsigned char *cp)
{
    dt->func_sysp = NULL;   /* delay slot of GetKind */
    DF_MBGUIDE_GetKind(&dt->btnkind[0]);
    mgarage_guide2(0xe, 0x63);
    DF_MDIALOG_Open(1, cp, 0, 0, 0, 0, 0, 0);
    ioObjp->play_func = MGARAGE_PLAY(__PlayGarageSysDispatch);
}

/* item 2 (OP): needs an arm part and an OP-equippable part (the table is freed on every path, quirk 4) */
static int __IsEnterOp(struct stMobj *ioObjp)
{
    PS2_FRAME(0x60);
    struct struct_9546159b *dt = MGARAGE_SYS(ioObjp);
    int ret = 1;
    unsigned char *cp = NULL;

    struct stMenuAC *acp = M_ACp[(int)dt->player][0];
    if (acp->flag.reading) {
        dt->cur_ssysp->play_func = MGARAGE_PLAY(__PlaySubSysDef);
        ioObjp->play_func = MGARAGE_PLAY(__PlayGarageSys);
        return 0;
    }
    if (acp->cur_part[1] == NULL) {
        ret = 0;
        cp = DF_MSTR_GetInfo(MenuMng.language, 1, 0x30db1);
    } else {
        struct stMPartIdTable *tbl = (struct stMPartIdTable *)DF_MMM_Alloc(1, 0x410);
        DF_MPART_MakeIDTable(tbl, 0, dt->player, 0xd, -1);
        if (tbl->num == 0) {
            cp = DF_MSTR_GetInfo(MenuMng.language, 1, 0x30db3);
            ret = 0;
        }
        DF_MMM_Free(1, tbl);
    }
    if (ret == 0)
        mgarage_refuse_dialog(ioObjp, dt, cp);
    return ret;
}

/* item 0 (Assembly) without cur_part[1]: needs some part of kind 1 (the caller already tested cur_part[1], quirk 3) */
static int __IsEnterAsm(struct stMobj *ioObjp)
{
    PS2_FRAME(0x460);
    struct struct_9546159b *dt = MGARAGE_SYS(ioObjp);
    int ret = 1;
    unsigned char *cp = NULL;
    int player = dt->player;

    if (M_ACp[player][0]->cur_part[1] == NULL) {
        struct stMPartIdTable id_table;   /* sp+0x50; filled and read by natives only */
        DF_MPART_MakeIDTable(&id_table, 0, player, 1, 1);
        if (id_table.num == 0) {
            cp = DF_MSTR_GetInfo(MenuMng.language, 1, 0x30dc0);
            ret = 0;
        }
    }
    if (ret == 0)
        mgarage_refuse_dialog(ioObjp, dt, cp);
    return ret;
}

/* five frames after the top menu started fading out: create the chosen item's sub-system */
static void __PlayGarageSys_PadCheckWait2(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct struct_9546159b *dt = MGARAGE_SYS(ioObjp);

    ioObjp->count++;
    if (ioObjp->count < 5)
        return;

    switch (dt->func_idx) {   /* sltiu 7 on the signed byte: negative = default */
    case 0:
        dt->func_sysp = DF_MASM_NewSystem(ioObjp, 0, dt->player);
        break;
    case 1:
        /* original also passes 0 in t0 (never read; protofix adds the unused a1) */
        dt->func_sysp = DF_MPERF_NewSystem(ioObjp, 0, dt->player, M_ACp[(int)dt->player][0]);
        break;
    case 2:
        dt->func_sysp = DF_MOP_NewSystem(ioObjp, dt->player);
        break;
    case 3:
        dt->func_sysp = DF_MPAINT_NewSystem(ioObjp, dt->player, NULL);
        DF_M1HELP_FadeOut();
        break;
    case 4: {
        int inMode = 0;
        if (mgarage_versus(dt->mode))
            inMode = (play_info.play_mode == 1);
        dt->func_sysp = DF_MCOCKPIT_NewSystem(ioObjp, dt->player, inMode);
        DF_M1HELP_FadeOut();
        break;
    }
    case 5:
        dt->func_sysp = DF_MNAMEE_NewSystem(ioObjp, dt->player);
        DF_M1HELP_FadeOut();
        break;
    case 6:
        if (mgarage_versus(dt->mode)) {
            for (int i = 0; i < 3; i++)
                DF_M_AC_SetFadeOut(M_ACp[(int)dt->player][i], 5, 0.0f);
            DF_MBACK_SetSpotLightFadeOut();
            dt->func_sysp = DF_MSYS_NewSystem(ioObjp, 1, dt->player, NULL, NULL);
        } else {
            sceGsSyncPath(0, 0);
            dt->func_sysp = DF_MACTEST_NewSystem(ioObjp, 0, dt->player, 0);
        }
        DF_M1HELP_FadeOut();
        break;
    default:
        break;
    }
    ioObjp->stat = 2;
    ioObjp->play_func = MGARAGE_PLAY(__PlayGarageSysDispatch);
}

/* the mode the Paint / Name tests see (game.versus_garage: the normal garage's) */
static inline int mgarage_item_mode(int item, int mode)
{
    return hook_garage_item_mode ? hook_garage_item_mode(item, mode) : mode;
}

/* accept on the top menu: may the item be entered? then fade the top menu out */
static void __PlayGarageSys_PadCheckWait(struct stMobj *ioObjp)
{
    PS2_FRAME(0x70);
    struct struct_9546159b *dt = MGARAGE_SYS(ioObjp);

    switch (dt->func_idx) {   /* sltiu 7 on the signed byte: negative / >= 7 = enter */
    case 0:   /* Assembly */
        if (M_ACp[(int)dt->player][0]->cur_part[1] != NULL)
            goto enter;
        if (__IsEnterAsm(ioObjp))
            goto enter;
        return;
    case 2:   /* OP */
        if (__IsEnterOp(ioObjp))
            goto enter;
        return;
    case 3:   /* Paint */
    case 5:   /* Name */
        if (!mgarage_versus(mgarage_item_mode(dt->func_idx, dt->mode)))
            goto enter;
        DF_MDIALOG_Open(5, DF_MSTR_GetInfo(MenuMng.language, 1, 0x33889), 0, 0, 0, 0, 1, 0);
        dt->f_ondialog = 1;
        ioObjp->play_func = MGARAGE_PLAY(__PlayGarageSysDispatch);
        return;
    case 4: {   /* Cockpit */
        struct _stMModel *h = M_ACp[(int)dt->player][0]->cur_part[0];
        void *infop = h ? h->prm.part.infop : NULL;
        if (infop)
            goto enter;
        DF_MDIALOG_Open(4, DF_MSTR_GetInfo(MenuMng.language, 1, 0x30e05), 0, 0, 0, 0, 1, 0);
        dt->f_ondialog = 1;
        ioObjp->play_func = MGARAGE_PLAY(__PlayGarageSysDispatch);
        return;
    }
    case 1: {   /* Performance */
        if (DF_M_AC_AllCheckReading(dt->player) != 0)
            return;
        struct stMenuAC *acp = M_ACp[(int)dt->player][0];
        if (acp->connect.cur.flag.incomplete) {
            DF_MBGUIDE_GetKind(&dt->btnkind[0]);
            mgarage_guide2(0xe, 0x63);
            DF_MDIALOG_Open(1, DF_MSTR_GetInfo(MenuMng.language, 1, 0x30db0), 0, 0, 0, 0, 0, 0);
            ioObjp->play_func = MGARAGE_PLAY(__PlayGarageSysDispatch);
            return;
        }
        goto enter;
    }
    case 6: {   /* System (versus) / AC Test */
        if (mgarage_versus(dt->mode))
            goto enter;
        struct stMenuAC *acp = M_ACp[(int)dt->player][0];
        if (acp->flag.reading) {
            dt->cur_ssysp->play_func = MGARAGE_PLAY(__PlaySubSysDef);
            ioObjp->play_func = MGARAGE_PLAY(__PlayGarageSys);
            return;
        }
        if (acp->connect.cur.flag.incomplete) {
            DF_MBGUIDE_GetKind(&dt->btnkind[0]);
            mgarage_guide2(0xe, 0x63);
            DF_MDIALOG_Open(1, DF_MSTR_GetInfo(MenuMng.language, 1, 0x30db0), 0, 0, 0, 0, 0, 0);
            ioObjp->play_func = MGARAGE_PLAY(__PlayGarageSysDispatch);
            return;
        }
        DF_MDIALOG_Open(2, DF_MSTR_GetInfo(MenuMng.language, 1, 0x30dcd), 0, 0, 0, 0, 1, 0);
        ioObjp->play_func = MGARAGE_PLAY(__GarageSys_PlayAcTestDlg);
        return;
    }
    default:
        break;
    }

enter:
    Voice_Call(0x3ea, 0x7f);
    {
        struct stMobj *child = dt->cur_ssysp;
        mgarage_label_fade_out(MGARAGE_SSYS(child));   /* ->label read before any null test (quirk 1) */
        child->play_func = MGARAGE_PLAY(__PlaySubSysDefFadeOut);
    }
    ioObjp->count = 0;
    ioObjp->stat = 2;
    ioObjp->play_func = MGARAGE_PLAY(__PlayGarageSys_PadCheckWait2);
}

/* the idle top menu: key guide, accept / cancel */
static void __PlayGarageSys(struct stMobj *ioObjp)
{
    PS2_FRAME(0x70);
    struct struct_9546159b *dt = MGARAGE_SYS(ioObjp);

    if (MenuMng.f_help == 1)
        return;
    if (DF_MBGUIDE_Sync(0) == 0)
        return;
    if (dt->cur_ssysp->stat != 0)   /* no null test (quirk 1) */
        return;
    MPadUser = dt->player;   /* delay slot of the stat == 0xc test */
    if (MenuMng.stat == 0xc) {
        __SetGarageSysFadeOut(ioObjp);
        return;
    }
    if (MenuKeyGuide_IsShown())
        return;
    if (MPad[MPadUser].start == 1) {
        if (mgarage_versus(dt->mode))
            MenuKeyGuide_Show(0x3c);
        else
            MenuKeyGuide_Show((unsigned char)(Ac_GetDiscType() == 1 ? 0xf : 0x10));
        return;
    }
    /* inlined DF_MGARAGE_SetMissionAc: for accept AND cancel (quirk 6) */
    if (MPad[MPadUser].Rdw == 1 || MPad[MPadUser].Rrt == 1)
        mgarage_set_mission_ac(dt->player);
    if (MPad[MPadUser].Rrt == 1) {
        Voice_Call(0x3eb, 0x7f);
        __SetGarageSysFadeOut(ioObjp);
        return;
    }
    switch (ioObjp->stat) {
    case 0:
        if (dt->mode == 2 || dt->mode == 6)
            return;
        if (DF_M_AC_AllCheckReading(dt->player) != 0)
            return;
        if (MPad[MPadUser].Rdw == 1) {
            dt->func_idx = (char)MGARAGE_SSYS(dt->cur_ssysp)->idx;
            dt->cur_ssysp->play_func = NULL;
            ioObjp->play_func = MGARAGE_PLAY(__PlayGarageSys_PadCheckWait);
        }
        return;
    case 1:
        if (dt->cur_ssysp->stat == 0)
            ioObjp->stat = 0;
        return;
    default:
        return;
    }
}

/* ---- g2: the System object's fade-out / fade-in / draw / creation ---- */

/* the garage is left: the mission AC is set up again (third inline copy of SetMissionAc), the top menu's labels and
 * SubSys slide / fade out, the ACs fade out (not slot 0 in garage mode 0), then __PlayGarageSysFadeOut */
static void __SetGarageSysFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x80);
    struct struct_9546159b *dt = MGARAGE_SYS(ioObjp);
    int i;

    mgarage_set_mission_ac(dt->player);
    struct stMobj *child = dt->cur_ssysp;
    if (child) {
        mgarage_label_fade_out(MGARAGE_SSYS(child));
        child->play_func = MGARAGE_PLAY(__PlaySubSysDefFadeOut);
    }
    for (i = 0; i < 3; i++)
        if (dt->mode != 0 || i != 0)
            DF_M_AC_SetFadeOut(M_ACp[(int)dt->player][i], 5, 0.0f);
    DF_M1HELP_FadeOut();
    DF_MBACK_SetSpotLightFadeOut();
    ioObjp->play_func = MGARAGE_PLAY(__PlayGarageSysFadeOut);
    ioObjp->stat = 2;
    ioObjp->count = 0;
    dt->step = 0;
}

/* a new-parts notice dialog (string id) on the System; the pad user is saved and handed to the player */
static inline void mgarage_parts_dialog(struct stMobj *ioObjp, struct struct_9546159b *dt, unsigned int id)
{
    DF_MDIALOG_Open(1, DF_MSTR_GetInfo(MenuMng.language, 1, id), NULL, 0, 0, 0, 1, 0);
    ioObjp->play_func = MGARAGE_PLAY(__PlayGarageSysDispatch);
    dt->f_ondialog = 1;
    dt->f_addpartsdlg = 1;
    dt->bk_mpaduser = (char)MPadUser;
    MPadUser = dt->player;
}

/* step 0: waits for the CD, shows the new-parts notices (cleared without dialogs in a linked versus game);
 * step 1: builds the top menu SubSys and fades the ACs in; step 2: 5 frames later the garage goes idle */
static void __PlaySystemFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    struct struct_9546159b *dt = MGARAGE_SYS(ioObjp);
    int i;

    if (dt->step == 2) {
        ioObjp->count++;
        if (ioObjp->count < 5)
            return;
        ioObjp->play_func = MGARAGE_PLAY(__PlayGarageSys);
    } else if (dt->step == 1) {
        int mode = dt->mode;
        if (mode == 3) {
            dt->func_idx = 6;   /* AC-test return */
        } else if (mode == 6 || mode == 2) {
            dt->cur_ssysp = __NewSubSysDef(ioObjp, 0, 1, dt->player, 0, mode, 0);
            for (i = 0; i < 3; i++)
                DF_M_AC_SetFadeIn(M_ACp[(int)dt->player][i], 0xf, 0.0f);
            DF_MBACK_SetSpotLightFadeIn();
            dt->step++;
            return;
        }
        dt->cur_ssysp = __NewSubSysDef(ioObjp, 0, 0, dt->player, dt->func_idx, dt->mode, 0);
        for (i = 0; i < 3; i++) {
            struct stMenuAC *acp = M_ACp[(int)dt->player][i];
            if (acp->flag.draw == 0 || acp->flag.reading == 1)
                DF_M_AC_SetFadeIn(acp, 0xf, 0.0f);
        }
        DF_MBACK_SetSpotLightFadeIn();
        dt->step++;
    } else if (dt->step == 0) {
        if (Cdvd_CdSync() == 1)   /* non-blocking poll: 1 = busy */
            return;
        if (play_info.mode == 1 && play_info.f_connect != 0) {
            if (MenuMng.f_ne_parts_remove)
                MenuMng.f_ne_parts_remove = 0;
            if (MenuMng.f_add_new_parts)
                MenuMng.f_add_new_parts = 0;
            if (MenuMng.f_ne_garage_remove)
                MenuMng.f_ne_garage_remove = 0;
        } else if (MenuMng.f_ne_parts_remove) {
            mgarage_parts_dialog(ioObjp, dt, 0x31511);
            MenuMng.f_ne_parts_remove = 0;
            MenuMng.f_ne_garage_remove = 0;   /* original: also cleared, without its own dialog */
            return;
        } else if (MenuMng.f_ne_garage_remove) {
            mgarage_parts_dialog(ioObjp, dt, 0x31513);
            MenuMng.f_ne_garage_remove = 0;
            return;
        } else if (MenuMng.f_add_new_parts) {
            mgarage_parts_dialog(ioObjp, dt, 0x31512);
            MenuMng.f_add_new_parts = 0;
            return;
        }
        dt->step++;
    }
}

/* pkt_func: stores MPktCurrentp back unchanged (no packets) */
static void __MakeVu1PackeGGeneWin(struct stMobj *pInObj)
{
    unsigned int *pDma = MPktCurrentp;

    if (pInObj)
        MPktCurrentp = pDma;
}

/* OLMENU 0x1ede210: the key-help tex strings of the garage */
static const int mgarage_draw_strid[8] = {0x19597, 0x195ac, 0x1959a, 0x19599, 0x1959b, 0x195a5, 0x195a2, 0x195a8};

/* draw_func: renders the garage's tex strings once each time drawtexstring is raised (versus: other help ids) */
static void __DrawGarageSystem(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct struct_9546159b *dt = MGARAGE_SYS(pMobj);

    if (dt->drawtexstring == 0)
        return;
    int strid[8];
    memcpy(strid, mgarage_draw_strid, sizeof strid);
    if (mgarage_versus(dt->mode)) {
        strid[3] = 0x18a99;
        strid[7] = 0x195be;
        strid[4] = 0x18a99;
        strid[6] = 0x18a99;
    }
    dt->drawtexstring = 0;
    MenuUtil_RenderTexStringAll(strid, 8);
}

/* the garage System object (garage mode inMode, player, pad mode); with f_versus_mcload set (a versus memory-card
 * load, __PlayGarageSysFadeOut count 2) inObjp itself is reused, its dt cleared except func_idx */
struct stMobj *DF_MGARAGE_NewSystem(struct stMobj *inObjp, int inMode, int inPlayer, int inPadMode)
{
    PS2_FRAME(0x70);
    struct stMobj *obj;

    if (f_versus_mcload == 0) {
        obj = DF_MOBJ_GetNewObject(inObjp);
        if (!obj)
            return NULL;
    } else {
        obj = inObjp;
    }
    obj->type = 0;
    obj->stat = 1;
    struct struct_9546159b *dt = MGARAGE_SYS(obj);
    if (f_versus_mcload == 1) {
        char save = dt->func_idx;
        memset(dt, 0, 0x40);
        dt->func_idx = save;
    }
    dt->mode = (char)inMode;
    dt->player = (char)inPlayer;
    dt->padMode = (char)inPadMode;
    dt->MenuWorkp_save = MenuWorkp;
    MenuMng.player_idx = dt->player;
    MenuMng.stat = 0;   /* delay slot: before SetGeneralMMD runs */
    DF_MMD_SetGeneralMMD();
    dt->viewID = (dt->mode == 2 || dt->mode == 6) ? 0x82 : 0xa;
    DF_MVIEW_MotionStart(MViewp, dt->viewID);
    int mode = dt->mode;
    if ((unsigned int)mode < 2 || mode == 5)
        mgarage_guide5(4, 1, 2, 3, 0x63);
    else if (mode == 2 || mode == 6)
        gcall_as<int(int, int, int)>(GFN(DF_MBGUIDE_Change), 4, 2, 0x63);
    DF_MBACK_PushSceneLabel(0x1e);
    DF_MBACK_SetSpotLightFadeIn();
    f_versus_mcload = 0;
    dt->step = 0;
    obj->play_func = MGARAGE_PLAY(__PlaySystemFadeIn);
    obj->draw_func = MGARAGE_PLAY(__DrawGarageSystem);
    if (dt->mode != 2 && dt->mode != 6)
        obj->pkt_func = MGARAGE_PLAY(__MakeVu1PackeGGeneWin);
    dt->drawtexstring = 1;
    return obj;
}

/* ---- g2: the top menu labels (_mobj_menu_garage_top_label_st) ---- */

/* the object ends: no more callbacks, hidden and marked for erase (the original's `if (obj)` is dead) */
static inline void mgarage_top_end(struct stMobj *o)
{
    o->stat = 5;
    o->play_func = 0;
    o->draw_func = 0;
    o->pkt_func = 0;
    o->f_draw = 0;
    o->f_erase = 1;
}

/* moveOffset[6..1] = moveOffset[5..0] */
static inline void mgarage_top_shift(struct _mobj_menu_garage_top_label_st *L)
{
    for (int i = 6; i > 0; i--)
        L->moveOffset[i] = L->moveOffset[i - 1];
}

/* carries an old row cursor through its fade-out (6 frames), then deletes it */
static void __MenuGarageTop_DeleteCursor(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    unsigned char old = pMobj->count;

    pMobj->count = old + 1;
    if (old < 6)
        return;
    UnitSprite_Delete(((struct _mobj_menu_garage_top_delete_cursor_st *)&pMobj->dt)->cursor);
    mgarage_top_end(pMobj);
}

/* the 14 label sprites (plates 0..6, marks 7..13), the 7 item strings (versus ids when fVS, Paint / Name greyed
 * out) and the blinking row cursor, all fading in. game.versus_garage swaps the versus Paint / Name strings and
 * skips the grey-out while hook_garage_top_create_item has its flag raised. */
static void __MenuGarageTop_CreateItem(struct stMobj *pMobj)
{
    PS2_FRAME(0xa0);
    struct _mobj_menu_garage_top_label_st *L = MGARAGE_LABEL(pMobj);
    struct struct_76b810ec col = {0x40, 0x40, 0x40, 0x40};   /* main sdata 0x27b840 */
    int i, k;

    if (hook_garage_top_create_item)
        hook_garage_top_create_item(1);
    for (i = 0; i < 14; i++) {
        L->label[i] = UnitSprite_New(pMobj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x459), sGarageTopLayout[i]);
        UnitSprite_SetFadeIn(L->label[i]);
    }
    int base = L->fVS ? 0xdcb68 : 0xdcb40;
    for (k = 0; i < 21; i++, k++) {
        char *lo = (char *)LAYOUT_GetLayoutData(0x459);
        L->label[i] = DF_MOBJ_NewStringFromLayoutName(pMobj, 0, base + k, 0, lo, sGarageTopLayout[i], NULL, 0, 0);
        DF_MOBJ_SetFadeIn(L->label[i]);
    }
    if (L->fVS) {
        UnitSprite_SetMultiply(L->label[10], &col);
        UnitSprite_SetMultiply(L->label[12], &col);
        DF_MOBJ_SetStringColorAll(L->label[17], &col);
        DF_MOBJ_SetStringColorAll(L->label[19], &col);
    }
    L->cursor = UnitSpriteBlink_New(pMobj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x459), sGarageTopLayout[21]);
    L->cursor->draw_prio = 3;
    UnitSprite_SetFadeIn(L->cursor);
    if (hook_garage_top_create_item)
        hook_garage_top_create_item(0);
}

/* slides the labels out to the left (-320) with a 7-frame offset trail, deletes them once settled (after 8 frames) */
static void __MenuGarageTop_FadeOut(struct stMobj *pMobj)
{
    PS2_FRAME(0x60);
    struct _mobj_menu_garage_top_label_st *L = MGARAGE_LABEL(pMobj);
    struct struct_214acd28 pos;
    int i;

    MoveMng_GetNextMoveValue(&L->move, -0x140, 8, 1);
    mgarage_top_shift(L);
    L->moveOffset[0] = ps2_ftoi(L->move.vMove);
    if (L->moveOffset[0] < -0x140)
        L->moveOffset[0] = -0x140;
    pos.y = 0;
    for (i = 0; i < 14; i++) {
        pos.x = L->moveOffset[i % 7];
        UnitSprite_SetMove(L->label[i], &pos, 0);
    }
    for (; i < 21; i++) {
        pos.x = L->moveOffset[i % 7];
        L->label[i]->dt.string.x = (short)ps2_ftoi((float)pos.x + L->strOffset);   /* original: no NULL test */
    }
    pos.x = L->moveOffset[L->id];
    UnitSprite_SetMove(L->cursor, &pos, 0);
    unsigned char old = pMobj->count;
    pMobj->count = old + 1;
    if (old < 8 || L->moveOffset[0] != L->moveOffset[6])
        return;
    for (i = 0; i < 14; i++)
        UnitSprite_Delete(L->label[i]);
    for (; i < 21; i++) {
        if (L->label[i]) {
            L->label[i]->f_draw = 0;
            L->label[i]->f_erase = 1;
            L->label[i] = NULL;
        }
    }
    UnitSprite_Delete(L->cursor);
    mgarage_top_end(pMobj);
}

/* moves the row cursor when id changed: the old one fades out in a carrier object, a new one blinks at row id */
static void __MenuGarageTop_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct _mobj_menu_garage_top_label_st *L = MGARAGE_LABEL(pMobj);
    struct struct_214acd28 pos = {0, 0x18};   /* main sdata 0x27b838 */

    pos.y = pos.y * L->id;
    if (L->id == L->prev_id)
        return;
    if (L->cursor) {
        struct stMobj *tmp = DF_MOBJ_GetNewObject(pMobj);
        if (!tmp)
            return;   /* original: prev_id not updated */
        UnitSprite_SetFadeOut(L->cursor);
        ((struct _mobj_menu_garage_top_delete_cursor_st *)&tmp->dt)->cursor = L->cursor;
        tmp->play_func = MGARAGE_PLAY(__MenuGarageTop_DeleteCursor);
    }
    L->cursor = UnitSpriteBlink_New(pMobj, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x459), sGarageTopLayout[21]);
    UnitSprite_SetFadeIn(L->cursor);
    UnitSprite_SetMove(L->cursor, &pos, 0);
    L->prev_id = L->id;
    L->cursor->draw_prio = 3;
}

/* slides the labels in from the left; once settled (after 9 frames) shows the cursor at row id and switches to
 * __MenuGarageTop_Play (stat stays 1: never cleared) */
static void __MenuGarageTop_Init(struct stMobj *pMobj)
{
    PS2_FRAME(0x70);
    struct _mobj_menu_garage_top_label_st *L = MGARAGE_LABEL(pMobj);
    struct struct_214acd28 pos;
    int i;

    MoveMng_GetNextMoveValue(&L->move, 0x140, 8, 1);
    mgarage_top_shift(L);
    L->moveOffset[0] = ps2_ftoi(L->move.vMove - 320.0f);
    if (L->moveOffset[0] > 0)
        L->moveOffset[0] = 0;
    pos.y = 0;
    for (i = 0; i < 14; i++) {
        pos.x = L->moveOffset[i % 7];
        UnitSprite_SetMove(L->label[i], &pos, 0);
    }
    for (i = 0; i < 7; i++)
        L->label[14 + i]->dt.string.x = (short)ps2_ftoi((float)L->moveOffset[i] + L->strOffset);
    unsigned char old = pMobj->count;
    pMobj->count = old + 1;
    if (old < 9 || L->moveOffset[0] != L->moveOffset[6])
        return;
    pMobj->count = 0;
    pMobj->play_func = MGARAGE_PLAY(__MenuGarageTop_Play);
    UnitSprite_SetFadeIn(L->cursor);
    pos.x = 0;
    pos.y = L->id * 24;
    UnitSprite_SetMove(L->cursor, &pos, 0);
    DF_MOBJ_SetDrawEnable(L->cursor, 1);
}

/* the top menu label object of the garage System pMobj (cursor row id), sliding in from -320 */
static struct stMobj *MenuGarage_TopLabelInit(struct stMobj *pMobj, int id)
{
    PS2_FRAME(0x60);
    struct stMobj *obj = DF_MOBJ_GetNewObject(pMobj);

    if (!obj)
        return NULL;
    struct _mobj_menu_garage_top_label_st *L = MGARAGE_LABEL(obj);
    obj->type = 0;
    obj->stat = 1;
    obj->count = 0;
    obj->play_func = MGARAGE_PLAY(__MenuGarageTop_Init);
    obj->draw_func = 0;
    obj->pkt_func = 0;
    L->timer = 0;
    L->id = id;
    L->prev_id = id;
    L->fVS = mgarage_versus(MGARAGE_SYS(pMobj)->mode);
    MoveMng_Init(&L->move);
    __MenuGarageTop_CreateItem(obj);
    L->strOffset = (float)L->label[14]->dt.string.x;   /* original: no NULL test */
    struct struct_214acd28 pos = {0, 0x18};   /* main sdata 0x27b830 */
    pos.y = pos.y * L->id;
    UnitSprite_SetMove(L->cursor, &pos, 0);
    DF_MOBJ_SetDrawEnable(L->cursor, 0);
    for (int i = 0; i < 7; i++)
        L->moveOffset[i] = -0x140;
    return obj;
}

/* ---------------------------------------------------------------- g3: the top menu SubSys "def" object */

#define MGARAGE_PI 3.14159274f   /* 0x40490fdb */

/* the AC slot rotation (L1 / R1): 15 frames of NXSetCirclePos with rate count * rot_dir / 15, the old front AC fading
 * out on frame 1 and the new one fading in on frame 7; on the settle frame a still-held L1 / R1 starts the next
 * rotation (MenuMng ac_idx only, no PlayerInfo_SetCurrentAcNo), otherwise back to __PlaySubSysDef */
static void __PlaySubSysDefRotateAC(struct stMobj *ioObjp)
{
    PS2_FRAME(0x70);
    struct struct_2857848f *dt = MGARAGE_SSYS(ioObjp);
    int flag = 0;
    int i;
    unsigned char old = ioObjp->count;

    ioObjp->count = old + 1;
    if (old < 15) {
        flag = 1;
    } else {
        for (i = 2; i >= 0; i--)
            M_ACp[(int)dt->player][i]->alpha.next = 0x80;
        for (i = 2; i >= 0; i--)
            M_ACp[(int)dt->player][i]->alpha.cur = 0x80;
        DF_M_AC_Select(dt->player, MenuMng.player[(int)dt->player].ac_idx);
        int r = dt->rot_dir;
        if (r == 1 && MPad[MPadUser].l1 != 0) {
            Voice_Call(0x3ee, 0x7f);
            if (--MenuMng.player[(int)dt->player].ac_idx < 0)
                MenuMng.player[(int)dt->player].ac_idx = 2;
            ioObjp->count = 1;
            dt->prev_ay = MGARAGE_PI;
        } else if (r == -1 && MPad[MPadUser].r1 != 0) {
            Voice_Call(0x3ee, 0x7f);
            if (++MenuMng.player[(int)dt->player].ac_idx >= 3)
                MenuMng.player[(int)dt->player].ac_idx = 0;
            ioObjp->count = 1;
            dt->prev_ay = MGARAGE_PI;
        } else {
            ioObjp->play_func = MGARAGE_PLAY(__PlaySubSysDef);
            ioObjp->count = 0;
            ioObjp->stat = 0;
            for (i = 0; i < 3; i++)
                DF_M_AC_SetCirclePos(M_ACp[(int)dt->player][i], i, dt->dist[i], 0.0f);
            M_ACp[(int)dt->player][0]->flag.rotate = 1;
            return;
        }
    }

    for (i = 0; i < 3; i++) {
        float rot = (float)dt->rot_dir;
        unsigned char count = ioObjp->count;
        float rate = (float)count * rot / 15.0f;
        DF_M_AC_NXSetCirclePos(M_ACp[(int)dt->player][i], i, dt->dist[i], rate, count >= 7);
    }
    if (flag == 1)
        M_ACp[(int)dt->player][0]->ang[1] = DF_MFUNC_CalcInterAng(dt->prev_ay, MGARAGE_PI, ioObjp->count, 15);
    if (ioObjp->count == 1) {
        if (dt->rot_dir > 0)
            DF_M_AC_SetFadeOut(M_ACp[(int)dt->player][2], 7, 0.0f);
        else if (dt->rot_dir < 0)
            DF_M_AC_SetFadeOut(M_ACp[(int)dt->player][0], 7, 0.0f);
    } else if (ioObjp->count == 7) {
        /* original: a1 is the 7 left from the count compare */
        if (dt->rot_dir > 0)
            DF_M_AC_SetFadeInNatural(M_ACp[(int)dt->player][2], 7, 0.0f);
        else if (dt->rot_dir < 0)
            DF_M_AC_SetFadeInNatural(M_ACp[(int)dt->player][0], 7, 0.0f);
    }
}

/* waits for the icon model list (never set in this file) to finish, then the SubSys is done (stat 5) */
static void __PlaySubSysDefFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    struct struct_2857848f *dt = MGARAGE_SSYS(ioObjp);
    struct stMobj *child = dt->imlistp;

    if (child == NULL) {
        ioObjp->stat = 5;
        ioObjp->play_func = 0;
    } else if (child->stat == 5) {
        DF_MOBJ2_DeleteIcnMdlList(child);
        dt->imlistp = NULL;
        ioObjp->stat = 5;
        ioObjp->play_func = 0;
    }
}

/* idle top menu: up / down move the row (help line follows), L1 / R1 switch the player's AC slot and start the
 * rotation (__PlaySubSysDefRotateAC); MPadUser is set to the SubSys player every frame past the dialog tests */
static void __PlaySubSysDef(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    struct struct_2857848f *dt = MGARAGE_SSYS(ioObjp);

    if (MenuMng.f_help == 1)
        return;
    if (DF_MDIALOG_Sync() == 1 || DF_MDIALOG_Sync() == 2 || DF_MDIALOG_Sync() == 3)
        return;
    int player = dt->player;
    int pre_idx = dt->idx;
    int pre_ac_idx = MenuMng.player[player].ac_idx;
    MPadUser = player;
    if (MenuKeyGuide_IsShown())
        return;
    if (MPad[MPadUser].select == 1)
        return;
    if (!(dt->flag & 1)) {
        if (MPad[MPadUser].repeat.dw) {
            Voice_Call(0x3e9, 0x7f);
            dt->idx++;
        } else if (MPad[MPadUser].repeat.up) {
            Voice_Call(0x3e9, 0x7f);
            dt->idx--;
        }
    }
    dt->idx = (dt->idx + dt->icon_num) % dt->icon_num;
    if (MPad[MPadUser].repeat.l1) {
        Voice_Call(0x3ee, 0x7f);
        if (--MenuMng.player[(int)dt->player].ac_idx < 0)
            MenuMng.player[(int)dt->player].ac_idx = 2;
        PlayerInfo_SetCurrentAcNo(dt->player, MenuMng.player[(int)dt->player].ac_idx);
        dt->rot_dir = 1;
    } else if (MPad[MPadUser].repeat.r1) {
        Voice_Call(0x3ee, 0x7f);
        if (++MenuMng.player[(int)dt->player].ac_idx >= 3)
            MenuMng.player[(int)dt->player].ac_idx = 0;
        PlayerInfo_SetCurrentAcNo(dt->player, MenuMng.player[(int)dt->player].ac_idx);
        dt->rot_dir = -1;
    }
    if (dt->idx != pre_idx)
        DF_M1HELP_ChangeString(DF_MSTR_GetInfo(MenuMng.language, 0, dt->helpStrId[dt->idx]));
    player = dt->player;
    if (MenuMng.player[player].ac_idx != pre_ac_idx) {
        dt->prev_ay = M_ACp[player][0]->ang[1];
        ioObjp->count = 0;
        ioObjp->stat = 4;
        ioObjp->play_func = MGARAGE_PLAY(__PlaySubSysDefRotateAC);
        return;
    }
    M_ACp[player][0]->flag.rotate = 1;
    if (dt->label)
        MGARAGE_LABEL(dt->label)->id = dt->idx;
}

/* six frames (fade_ct 0..5), then idle once the icon model list (never set here) is idle; frameless leaf */
static void __PlaySubSysDefFadeIn(struct stMobj *ioObjp)
{
    struct struct_2857848f *dt = MGARAGE_SSYS(ioObjp);

    if (dt->init_type != 1 && dt->init_type != 0)
        return;
    int fade_ct = dt->fade_ct;
    if ((unsigned int)fade_ct >= 6)
        return;
    if (fade_ct < 5) {
        dt->fade_ct = fade_ct + 1;
    } else if (dt->imlistp == NULL || dt->imlistp->stat == 0) {
        ioObjp->stat = 0;
        ioObjp->play_func = MGARAGE_PLAY(__PlaySubSysDef);
    }
}

/* game.versus_garage: the help ids of Paint / Name are swapped for the normal garage's after the object is built
 * (the first help line is already shown with the versus text, as the old post-call patch did) */
static inline void mgarage_help_hook(struct struct_2857848f *dt)
{
    if (hook_garage_new_sub_sys_def)
        hook_garage_new_sub_sys_def(dt->helpStrId);
}

/* the top menu SubSys (child of the garage System inObjp): the 3 ACs re-read on the first open (init_type 0), the
 * top labels (not for flag & 1 = garage modes 2 / 6), the per-item help ids and the first help line, the ACs on
 * their circle; inFirst is 0 at every call site */
static struct stMobj *__NewSubSysDef(struct stMobj *inObjp, int inInitType, int inFlag, int inPlayer, int inIdx,
                                     int inMode, int inFirst)
{
    PS2_FRAME(0xb0);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    int i;

    if (!obj)
        return NULL;
    struct struct_2857848f *dt = MGARAGE_SSYS(obj);
    obj->type = 0;
    obj->stat = 1;
    dt->init_type = inInitType;
    dt->idx = inIdx;
    dt->player = inPlayer;
    dt->flag = inFlag;
    dt->mode = inMode;
    dt->icon_num = 7;
    dt->f_icon = 1;
    float ay = M_ACp[(int)dt->player][0]->ang[1];
    if (dt->init_type == 0) {
        DF_M_AC_Select(dt->player, MenuMng.player[(int)dt->player].ac_idx);
        for (i = 0; i < 3; i++) {
            if (dt->mode == 0) {
                struct stMenuAC *a = M_ACp[(int)dt->player][i];
                if (a->flag.draw == 1 && a->flag.reading == 0)
                    continue;
            }
            struct stMenuAC *acp = M_ACp[(int)dt->player][i];
            DF_M_AC_SetData(acp);
            DF_M_AC_PartsRead(acp);
            acp->alpha.cur = 0;
        }
    }
    if (!(dt->flag & 1))
        dt->label = MenuGarage_TopLabelInit(inObjp, dt->idx);
    if (dt->mode == 2 || dt->mode == 6) {
        dt->helpStrId[0] = 0x18aa0;
    } else if (mgarage_versus(MGARAGE_SYS(inObjp)->mode)) {
        for (i = 0; i < 7; i++)
            dt->helpStrId[i] = 0xdcb68 + i;
    } else {
        for (i = 0; i < 7; i++)
            dt->helpStrId[i] = 0xdcb40 + i;
    }
    unsigned char *cp;
    if (inFirst == 1) {
        if (dt->mode == 4)
            cp = DF_MSTR_GetInfo(MenuMng.language, 0, 0x19644);
        else if (dt->mode == 3)
            cp = DF_MSTR_GetInfo(MenuMng.language, 0, AcTestSelextIdx + 0x19320);
        else
            cp = DF_MSTR_GetInfo(MenuMng.language, 0, dt->helpStrId[inIdx]);
        dt->f_first = 0;
    } else {
        cp = DF_MSTR_GetInfo(MenuMng.language, 0, dt->helpStrId[inIdx]);
    }
    DF_M1HELP_SetWindow(); /* empty stub; the original passes (0, 0x280) */
    DF_M1HELP_SetString(cp);
    DF_M1HELP_FadeIn();
    for (i = 0; i < 3; i++) {
        dt->dist[i] = 7.0f;
        DF_M_AC_SetCirclePos(M_ACp[(int)dt->player][i], i, dt->dist[i], 0.0f);
    }
    if (dt->init_type == 1)
        M_ACp[(int)dt->player][0]->ang[1] = ay;
    dt->fade_ct = 0;
    obj->play_func = MGARAGE_PLAY(__PlaySubSysDefFadeIn);
    mgarage_help_hook(dt);
    return obj;
}
