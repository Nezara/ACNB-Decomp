/* src/menu/mmanage.cpp - the menu scene manager (OLMENU). One stMobj (DF_MMANAGE_NewSystem) lives for a whole menu
 * session: from MenuMng.ret (why the menu was entered / which scene comes next) it builds the child screen (logo,
 * title, top menu MTOP, Versus entry MenuVsr, system option / new-game dialog, memory-card systems, gallery, name
 * entry) and keeps { child, screen id } in its dt. __MMANAGE_PlaySystem waits for the child's stat 5, then either
 * builds the next child or leaves the menu: it writes MenuMng.ret (exit code main() reads) / f_continue, erases
 * itself and calls DF_M1HELP_Finish; the interpreted __MenuStart loop then restarts NewSystem or returns to main().
 * __MMANAGE_PlayWait (25 frames) and __MMANAGE_PlayMissionTrasnSync (mission AC transfer) are waits it installs on
 * itself before finishing.
 * DF_MBGUIDE_Change / DF_MBACK_SetFadeOutNX / SetFadeInNX / ChangeTag are void(void) in DWARF but read a0: gcall_as.
 * Original quirks kept: several call arguments / screen ids are register leftovers in the original (MTOP(3) for ret 5,
 * MTOP(2) for ret 0xc, MTOP(6) for ret 0x61 / screen 14, screen 9 for ret 4, MenuSysOption_New(.., 2, 1)) - written
 * as literals; screen 7's "ready" exit clears f_continue, screens 8 / 9 leave it; screen slots 10 / 11 share one arm
 * and 12 is a no-op; ret 0x66 returns without a child or PlaySystem.
 * versus.quick (runtime/quick_match.c) replaces the first NewSystem call through hook_mmanage_new_system. */
#include "acnb/hooks.h"
#include "acnb/sdk.h"
#include <string.h>

/* dt of the manager object: the current child screen and its id (0..14) */
struct mmanage_dt {
    struct stMobj *child; /* 0x0 */
    int screen;           /* 0x4 */
};

static inline struct mmanage_dt *mmanage_dt(struct stMobj *o) { return (struct mmanage_dt *)&o->dt; }

/* the DWARF bit-field "delete" pair: two read-modify-write byte stores */
static inline void mmanage_erase(struct stMobj *o)
{
    if (o) {
        o->f_draw = 0;
        o->f_erase = 1;
    }
}

/* void(void) in DWARF, the original passes a0 */
static inline void mmanage_guide_end(void)
{
    gcall_as<int(int)>(GFN(DF_MBGUIDE_Change), 0x63);
}

static inline void mmanage_fade_out(void)
{
    gcall_as<void(int)>(GFN(DF_MBACK_SetFadeOutNX), 0);
}

static inline void mmanage_fade_in(void)
{
    gcall_as<void(int)>(GFN(DF_MBACK_SetFadeInNX), 0);
}

static inline void mmanage_change_tag(int tag)
{
    gcall_as<void(int)>(GFN(DF_MBACK_ChangeTag), tag);
}

/* IOP re-init (4 copies in PlaySystem) */
static void mmanage_iop_reinit(void)
{
    if (!(Iop_GetInitFlag() & 0x200)) {
        Snd_Cleanup();
        Iop_Init(0x201);
        sceSifInitIopHeap();
        Snd_Init();
        Ac2_Mcf_Init();
        Pad_Init();
        sceGsSyncV(0);
    }
}

/* i.LINK teardown (Versus screens 7 / 8 / 9) */
static void mmanage_link_teardown(void)
{
    fslnk_LinkSyncStop();
    fslnk_LinkSyncTerm();
    fslnk_LinkStop();
    fslnk_LinkTerm();
    fsilk_IlinkDestroy();
}

/* screens 7-11 when the child did not leave for a battle: error -> wait and back to the logo, else the title */
static void mmanage_error_or_title(struct stMobj *self, struct mmanage_dt *dt)
{
    if (MenuMng.stat == 0xc || loader_IsOutOfMenuError()) {
        MenuMng.ret = 0x60;
        MenuMng.f_continue = 1;
        self->play_func = (void (*)(void))GFN(__MMANAGE_PlayWait);
    } else {
        dt->screen = 0;
        dt->child = MTitle_NewSystem();
    }
}

static void __MMANAGE_PlayWait(struct stMobj *ioObjp)
{
    PS2_FRAME(0x10);
    unsigned char c = ioObjp->count + 1;
    ioObjp->count = c;
    if (c < 25) return;
    mmanage_erase(ioObjp);
    DF_M1HELP_Finish();
}

static void __MMANAGE_PlayMissionTrasnSync(struct stMobj *ioObjp)
{
    PS2_FRAME(0x20);
    if (MenuMng.stat == 0xc) {
        DF_M_AC_MissionTransForceEnd();
        mmanage_erase(ioObjp);
        DF_M1HELP_Finish();
        MenuMng.ret = 0x61;
        MenuMng.f_continue = 1;
    } else if (DF_M_AC_MissionTransSync()) {
        DF_MDRAW_Tm2LoadImageNX(EmblemSTM2, 0, 0);
        sceGsSyncPath(0, 0);
        mmanage_erase(ioObjp);
        DF_M1HELP_Finish();
    }
}

static void __MMANAGE_PlaySystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    struct mmanage_dt *dt = mmanage_dt(ioObjp);
    int leave = 0;
    struct stMobj *child = dt->child;
    if (!child) return;
    if (child->stat == 5) {
        mmanage_erase(dt->child);
        dt->child = 0;
        MenuWorkp = MenuWorkp_save;
        switch ((unsigned int)dt->screen) {
        case 0: /* title done */
            Game_InitAllData();
            DF_M_AC_Init((struct StAcData *)MenuMng.datAsm);
            memset(&play_info, 0, 0x40);
            play_info.connected_num = 1;
            switch (MenuMng.ret) {
            case 0x15:
            case 0xa:
            case 9:
                mmanage_iop_reinit();
                leave = 1;
                MenuMng.f_continue = 1;
                break;
            case 0x14:
                MenuMng.idx = 1;
                MenuMng.f_continue = 1;
                MenuMng.stat = 0;
                dt->screen = 0xd;
                dt->child = DF_MGALLE_NewSystem();
                break;
            case 0x64:
                dt->screen = 0;
                dt->child = MTitle_NewSystem();
                break;
            case 0xf:
                MenuMng.stat = 0;
                leave = 1;
                MenuMng.f_continue = 1;
                break;
            case 4:
            case 3:
                MenuMng.f_continue = 1;
                leave = 1;
                break;
            case 2:
                play_info.vsmenu_flow_id = 0;
                leave = 1;
                MenuMng.f_continue = 1;
                break;
            }
            break;
        case 1: /* company logo done */
            MenuMng.ret = 0x63;
            leave = 1;
            MenuMng.f_continue = 1;
            break;
        case 2: /* save card screen done */
            mmanage_guide_end();
            mmanage_fade_out();
            MenuMng.ret = 0x60;
            MenuMng.f_continue = 1;
            ioObjp->play_func = (void (*)(void))GFN(__MMANAGE_PlayWait);
            break;
        case 3: /* option / new-game dialog done */
            if (MenuMng.stat == 2) {
                MenuMng.stat = 0;
                dt->screen = 5;
                dt->child = DF_MNAMEE2_NewSystem(ioObjp, 0, 0, 0, 3, 0);
            } else {
                if (MenuMng.tm2Tbl[5]) {
                    DF_MMM_Free(0, MenuMng.tm2Tbl[5]);
                    MenuMng.tm2Tbl[5] = 0;
                }
                mmanage_guide_end();
                dt->screen = 0;
                dt->child = MTitle_NewSystem();
            }
            break;
        case 4: /* load card screen done */
            mmanage_guide_end();
            mmanage_fade_out();
            if (MenuMng.stat == 4) {
                MenuMng.f_new_game = 0;
                dt->screen = 6;
                dt->child = DF_MTOP_NewSystem(0);
            } else {
                dt->screen = 0;
                dt->child = MTitle_NewSystem();
            }
            break;
        case 5: /* name entry done */
            if (MenuMng.stat == 2) {
                if (MenuMng.tm2Tbl[5]) {
                    DF_MMM_Free(0, MenuMng.tm2Tbl[5]);
                    MenuMng.tm2Tbl[5] = 0;
                }
                mmanage_guide_end();
                mmanage_fade_out();
                DF_MGARAGE_SetMissionAc(0);
                MenuMng.mc_file = 0;
                MenuMng.ret = 0x61;
                MenuMng.movie_control = 2;
                MenuMng.mc_slot = 0;
                DF_EMB_MakeSmallEmblem(EmblemSTM2, EmblemTM2);
                ioObjp->play_func = (void (*)(void))GFN(__MMANAGE_PlayWait);
            } else {
                dt->screen = 3;
                dt->child = MenuSysOption_New(ioObjp, 0, 2, 1);
            }
            break;
        case 6: /* top menu done */
            DF_EMB_MakeSmallEmblem(EmblemSTM2, EmblemTM2);
            MyI.rec_ai = 0;
            switch (MenuMng.stat) {
            case 2:
                g_nextstage.sortie_mode = 0;
                leave = 1;
                MenuMng.ret = 1;
                break;
            case 3:
                dt->screen = 0;
                dt->child = MTitle_NewSystem();
                break;
            case 4:
            case 0xd:
                MenuMng.ret = 0x61;
                leave = 1;
                MenuMng.f_continue = 1;
                break;
            case 5:
                MenuMng.ret = 5;
                g_nextstage.sortie_mode = 1;
                DF_NPCTAB_Reset();
                DF_M_AC_MissionTransStart();
                ioObjp->play_func = (void (*)(void))GFN(__MMANAGE_PlayMissionTrasnSync);
                break;
            case 6:
                g_nextstage.sortie_mode = 2;
                MenuMng.ret = 0xb;
                MyI.rec_ai = 0;
                DF_M_AC_MissionTransStart();
                ioObjp->play_func = (void (*)(void))GFN(__MMANAGE_PlayMissionTrasnSync);
                break;
            case 0xa:
                g_nextstage.sortie_mode = 2;
                leave = 1;
                MenuMng.ret = 0xe;
                MyI.rec_ai = 1;
                break;
            case 7:
                g_nextstage.sortie_mode = 3;
                MenuMng.ret = 0xc;
                DF_M_AC_MissionTransStart();
                ioObjp->play_func = (void (*)(void))GFN(__MMANAGE_PlayMissionTrasnSync);
                break;
            case 8:
                leave = 1;
                MenuMng.ret = 0xd;
                g_nextstage.sortie_mode = 1;
                break;
            case 9:
                g_nextstage.sortie_mode = 0;
                MenuMng.ret = 1;
                DF_M_AC_MissionTransStart();
                ioObjp->play_func = (void (*)(void))GFN(__MMANAGE_PlayMissionTrasnSync);
                break;
            case 0xc:
                loader_SetDisposeExtPartsFlag();
                leave = 1;
                MenuMng.ret = 0x61;
                MenuMng.f_continue = 1;
                MenuMng.f_show_hdd_err = 0;
                break;
            case 0xe:
                MenuMng.f_continue = 0;
                leave = 1;
                MenuMng.ret = 0x11;
                break;
            }
            break;
        case 7: /* Versus entry (MenuVsr mode 0 / 1) done */
            if (play_info.vsmenu_flow_id == 1 || play_info.vsmenu_flow_id == 2) {
                leave = 1;
                MenuMng.ret = 2;
                MenuMng.f_continue = 1;
            } else if (play_info.f_ready == 1) {
                leave = 1;
                MenuMng.f_continue = 0;
                MenuMng.ret = 0x31;
                g_nextstage.sortie_mode = 4;
            } else {
                mmanage_link_teardown();
                mmanage_error_or_title(ioObjp, dt);
            }
            break;
        case 8: /* Versus entry (mode 2) done */
            if (play_info.f_ready == 1) {
                leave = 1;
                MenuMng.ret = 0x32;
                g_nextstage.sortie_mode = 4;
            } else {
                mmanage_link_teardown();
                mmanage_error_or_title(ioObjp, dt);
            }
            break;
        case 9: /* Versus entry (mode 3) done */
            if (play_info.f_ready == 1) {
                leave = 1;
                MenuMng.ret = 0x33;
                g_nextstage.sortie_mode = 4;
            } else {
                mmanage_link_teardown();
                mmanage_error_or_title(ioObjp, dt);
            }
            break;
        case 10:
        case 11: /* never created by NewSystem */
            if (MenuMng.stat == 0xb) {
                MenuMng.ret = 0x10;
                leave = 1;
                g_nextstage.sortie_mode = 6;
            } else {
                mmanage_error_or_title(ioObjp, dt);
            }
            break;
        case 12:
            break;
        case 13: /* gallery done */
            if (MenuMng.movie_control > 0) {
                dt->screen = 0;
                leave = 1;
                MenuMng.ret = 0x14;
            } else {
                dt->screen = 0;
                dt->child = MTitle_NewSystem();
            }
            break;
        case 14: /* convert card screen done */
            mmanage_guide_end();
            mmanage_fade_out();
            if (MenuMng.stat == 4) {
                Game_InitAllDataWOparts();
                memset(&play_info, 0, 0x40);
                MenuMng.f_new_game = 0;
                play_info.connected_num = 1;
                MenuMng.f_convert = 1;
                MenuMng.stat = 0;
                mmanage_iop_reinit();
                MyI.prologue = 1;
                dt->screen = 6;
                dt->child = DF_MTOP_NewSystem(6);
            } else {
                dt->screen = 0;
                dt->child = MTitle_NewSystem();
            }
            break;
        default:
            break;
        }
    }
    if (leave == 1) {
        mmanage_erase(ioObjp);
        DF_M1HELP_Finish();
    }
}

/* the menu-data file 0xbc8 (card screens' work data) into the work heap */
static void mmanage_load_card_data(void)
{
    MenuWorkp = (unsigned char *)DF_MFUNC_Aligned(MenuWorkp, 0x40);
    MenuMng.tm2Tbl[5] = MenuWorkp;
    File_Read(0xbc8, -1, (char *)MenuWorkp, 1);
    unsigned int n = DF_MDATA_Remap(MenuWorkp);
    MenuWorkp = MenuWorkp + n;
    DF_MVIEW_SetID(0xa);
    DF_MBACK_SetDraw(1);
    mmanage_fade_in();
}

struct stMobj *DF_MMANAGE_NewSystem(void)
{
    PS2_FRAME(0x30);
    if (hook_mmanage_new_system) {
        struct stMobj *q = hook_mmanage_new_system();
        if (q) return q;
    }
    struct stMobj *obj = DF_MOBJ_GetNewObject(0);
    if (!obj) return 0;
    obj->type = 0;
    struct mmanage_dt *dt = mmanage_dt(obj);
    MenuWorkp = (unsigned char *)DF_MFUNC_Aligned(MenuWorkp, 0x40);
    MenuWorkp_save = MenuWorkp;
    ps2_srand(MenuMng.frame);
    DF_M_AC_SetAll();

    if (MenuMng.stat == 0xc || loader_IsOutOfMenuError()) {
        MenuMng.f_ne_parts_remove = 0;
        MenuMng.f_add_new_parts = 0;
        MenuMng.f_ne_garage_remove = 0;
        switch (MenuMng.ret) {
        case 0x10:
        case 0xf:
            MenuMng.ret = 0x60;
            break;
        case 0x11:
        case 0:
        case 0xd:
        case 0x61:
        case 1:
        case 0xe:
        case 0xc:
        case 0xb:
        case 5:
            MenuMng.ret = 0x61;
            break;
        default:
            MenuMng.ret = 0x60;
            break;
        }
    }

    switch (MenuMng.ret) {
    case 0x11:
        dt->screen = 6;
        dt->child = DF_MTOP_NewSystem(0);
        break;
    case 0x15: /* CONVERT */
        MenuMng.stat = 0;
        mmanage_load_card_data();
        mmanage_change_tag(0x29);
        dt->screen = 0xe;
        dt->child = DF_MSYS_NewMcSystem(obj, 0, 7, 0, 0, 0);
        break;
    case 0xa: /* LOAD GAME */
        MenuMng.stat = 0;
        mmanage_load_card_data();
        mmanage_change_tag(0x29);
        dt->screen = 4;
        dt->child = DF_MSYS_NewMcSystem(obj, 0, 1, 0, 0, 0);
        break;
    case 9: /* NEW GAME */
        MenuMng.tm2Tbl[5] = 0;
        MenuMng.f_new_option = 1;
        dt->screen = 3;
        dt->child = MenuSysOption_NewGameDlg_New(obj, 0, 2, 0);
        break;
    case 0x14:
        MenuMng.stat = 0;
        MenuMng.f_continue = 1;
        dt->screen = 0xd;
        dt->child = DF_MGALLE_NewSystem();
        break;
    case 0x66: /* mission AC transfer, no child */
        DF_MBACK_SetDraw(0);
        DF_MGARAGE_SetMissionAc(0);
        MenuMng.ret = 1;
        DF_M_AC_MissionTransStart();
        obj->play_func = (void (*)(void))GFN(__MMANAGE_PlayMissionTrasnSync);
        MenuMng.f_new_system = 1;
        return obj;
    case 0x60:
        dt->screen = 1;
        dt->child = DF_MFSLOGO_NewSystem();
        break;
    case 0x63:
        dt->screen = 0;
        dt->child = MTitle_NewSystem();
        break;
    case 0x61:
        dt->screen = 6;
        if (MyI.prologue != 0 || g_gamedata[0].flagShowFirstNotice != 0) {
            dt->child = DF_MTOP_NewSystem(0);
        } else {
            dt->child = DF_MTOP_NewSystem(6);
            MyI.prologue = 1;
        }
        break;
    case 0x62: /* save card screen */
        mmanage_load_card_data();
        dt->screen = 2;
        dt->child = DF_MSYS_NewMcSystemForSave(obj, 0, 0, 0, 0, 0);
        break;
    case 0xd:
        dt->screen = 6;
        dt->child = DF_MTOP_NewSystem(4);
        mmanage_change_tag(0x17);
        break;
    case 0x33:
        dt->screen = 9;
        dt->child = MenuVsr_NewSystem(obj, 3);
        DF_MBACK_SetDraw(1);
        mmanage_fade_in();
        break;
    case 4:
        dt->screen = 9;
        dt->child = MenuVsr_NewSystem(obj, 0);
        DF_MBACK_SetDraw(1);
        mmanage_fade_in();
        break;
    case 0x32:
        dt->screen = 8;
        dt->child = MenuVsr_NewSystem(obj, 2);
        DF_MBACK_SetDraw(1);
        break;
    case 3:
        dt->screen = 8;
        dt->child = MenuVsr_NewSystem(obj, 0);
        DF_MBACK_SetDraw(1);
        break;
    case 0x31:
        dt->screen = 7;
        dt->child = MenuVsr_NewSystem(obj, 1);
        DF_MBACK_SetDraw(1);
        mmanage_fade_in();
        break;
    case 2:
        dt->screen = 7;
        dt->child = MenuVsr_NewSystem(obj, 0);
        DF_MBACK_SetDraw(1);
        mmanage_fade_in();
        break;
    case 0xc:
        dt->screen = 6;
        dt->child = DF_MTOP_NewSystem(2);
        break;
    case 0xe:
    case 0xb:
        dt->screen = 6;
        dt->child = DF_MTOP_NewSystem(1);
        break;
    case 5:
        dt->screen = 6;
        dt->child = DF_MTOP_NewSystem(3);
        mmanage_change_tag(0x18);
        break;
    case 1:
        Game_GetClearCount(0);
        DF_NPCTAB_Reset();
        dt->screen = 6;
        dt->child = DF_MTOP_NewSystem(5);
        break;
    case 0:
        if (gMManageCtr.fTitleSkip) {
            dt->screen = 6;
            dt->child = DF_MTOP_NewSystem(0);
        } else {
            dt->screen = 1;
            dt->child = DF_MFSLOGO_NewSystem();
        }
        break;
    default:
        break;
    }
    obj->play_func = (void (*)(void))GFN(__MMANAGE_PlaySystem);
    MenuMng.f_new_system = 1;
    return obj;
}
