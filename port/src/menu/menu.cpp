/* src/menu/menu.cpp - the OLMENU main loop and its driver (menu/menu.c, whole file native).
 * DF_MENU_Init (once, from main) clears MenuMng; DF_MENU_MainLoop loops around __MENU_MainLoop and plays pending
 * menu movies first; __MENU_MainLoop = DF_MENU_Reset (carve the menu RAM, init every subsystem),
 * DF_MENU_ReadDataLoop (load the menu data blocks from disc into MenuWorkp, one state per frame), __MenuStart
 * (subsystem starts, player parts correction, MMD ids, DF_MMANAGE_NewSystem), then the per-frame loop: fade quad,
 * buffer flip, pad / disc / loader polling, all *_Play updates, the first VU1 packet chain and draws, the second chain
 * (__MenuMakePacket2ND: view, env ball, emblem, 2nd-pass objects, dialogs, ended and kicked at once from the current
 * write position; MPktCurrentp stays behind its END tag, so DF_MPKT_Kick later sends the area from its start again)
 * and __MenuILink (the link-play hook). Both loops run a whole menu session inside one call. DF_MENU_4VersusProc is a
 * trimmed copy of the frame body for the link menu; __ShowHddError is the HDD error dialog loop. Fades
 * (SetFadeIn / Out) only program MenuMng.fade*, which the loop consumes. */
#include "acnb/sdk.h"
#include "acnb/nodwarf.h"
#include "acnb/ps2fp.h"
#include <string.h>

#ifdef ACNB_SOURCEPORT
int g_nMenuBlur = 0x80;         /* 0x27b7c0 */
int g_nMenuCopyVOffset = 3;     /* 0x27b7bc */
/* OLMENU rodata 0x1eddd50 (names 0x1eddc80..0x1eddd38) and the path format 0x1edde30 (nodwarf.h) */
struct struct_64f25202 movieFileName[16] = {
    {NULL, 0, -1},
    {(char *)"DISC\\ACNB_OP.PSS", 0, -1},
    {(char *)"DISCN\\P1.PSS", 0, -1},
    {NULL, 0x33, -1},
    {(char *)"DISCN\\G1.PSS", 0, -1},
    {NULL, 0, -1},
    {(char *)"ARK_ACRT.PSS", 0, -1},
    {NULL, 0, -1},
    {(char *)"DISCP\\A1.PSS", 0, -1},
    {(char *)"DISCP\\A2.PSS", 0, -1},
    {(char *)"DISCP\\A3.PSS", 0, -1},
    {(char *)"DISCP\\A4.PSS", 0, -1},
    {(char *)"DISCP\\A5.PSS", 0, -1},
    {(char *)"DISCP\\A6.PSS", 0, -1},
    {(char *)"DISCP\\A7.PSS", 0, -1},
    {(char *)"DISCP\\A8.PSS", 0, -1},
};
char menu_fmt_movie[10] = "\\MOVIE\\%s";
#endif

void DF_MENU_SetFadeIn(int count)
{
    MenuMng.alpha = 0x80;
    MenuMng.fadecountmax = count < 2 ? 1 : count;
    MenuMng.fadetype = 1;
    MenuMng.fadecount = 0;
}

void DF_MENU_SetFadeOut(int count)
{
    MenuMng.alpha = 0;
    MenuMng.fadecountmax = count < 2 ? 1 : count;
    MenuMng.fadetype = 2;
    MenuMng.fadecount = 0;
}

/* no bounds check on movieid; the enum result is narrowed to a signed char */
int DF_MENU_PlayMovieBlockId(int movieid, void *inBuff, unsigned int inPadF, unsigned int inLoopF,
                             unsigned int inVolume)
{
    PS2_FRAME(0xe0);
    PS2_STACK(char[128], buf, 0x60);   /* handed to interpreted sprintf / Movie_PlayForMenu */
    int r;
    sceGsSyncPath(0, 0);
    struct struct_64f25202 *e = &movieFileName[movieid];
    if (e->fname) {
        ps2_sprintf_s(buf, menu_fmt_movie, e->fname);
        r = (signed char)Movie_PlayForMenu(buf, (char *)inBuff, inPadF, inLoopF, 0, inVolume, 0);
    } else {
        r = 1;
    }
    Draw_SetupFrameBuffer(0, 0);
    MenuTex_InvalidateCache();
    return r;
}

/* HDD error dialog: runs its own frame loop until the dialog closes. The loop is entered at its packet half. */
static void __ShowHddError(void)
{
    PS2_FRAME(0x10);
    DF_MDIALOG_Open(1, DF_MSTR_GetInfo(MenuMng.language, 1, 0x31510), NULL, 0, 0, 0, 1, 0);
    MenuMng.f_show_hdd_err = 1;
    if (loader_IsOutOfMenuError()) loader_ClearOutOfMenuError();
    for (;;) {
        DF_MVIEW_MakeVU1Packet();
        DF_MENVBL_MakeVu1Packet();
        DF_MBACK_MakeVu1Packet();
        DF_M_AC_MakeVu1Packet();
        DF_MOBJ_MakeVu1Paket();
        DF_MDIALOG_MakeVu1Paket();
        MPktCurrentp = DF_MPKT_MakeEnd(MPktCurrentp);
        DF_MMODEL_Update();
        sceGsSyncPath(0, 0);
        DF_MBACK_Draw();
        DF_M_AC_DrawShadow();
        DF_MHELP_DrawBack();
        DF_MOBJ_Draw();
        MenuMng.frame++;
        sceGsSyncPath(0, 0);
        Draw_FlipBufferTearing();
        DF_MMODEL_UpdateMatrix();
        DF_MPKT_Kick();
        fspad_PadUpdate();
        DF_MPAD_Update();
        Cdvd_Exec();
        DF_MDIALOG_Play();
        if (DF_MDIALOG_Sync() & 4) return;
    }
}

void DF_MENU_NotifyHddError(int asout)
{
    PS2_FRAME(0x20);
    loader_SetReadyHdd(0);
    MenuMng.f_use_readbuff = 0;
    MenuMng.stat = 0xc;
    play_info.f_error = 1;
    play_info.f_ready = -1;
    if (asout)
        loader_SetHddErrorAsOutOfMenu();
    else
        __ShowHddError();
}

/* one link-menu frame: no flip, no fade quad, no second packet pass */
int DF_MENU_4VersusProc(int inID, int inNum)
{
    PS2_FRAME(0x10);
    MenuMng.ilink.ps2id = inID;
    MenuMng.ilink.ps2num = inNum + 1;
    DF_MMODEL_UpdateMatrix();
    DF_MPKT_Kick();
    fspad_PadUpdate();
    DF_MPAD_Update();
    Cdvd_Exec();
    loader_AsyncProc(1);
    DF_MOBJ_Play();
    DF_M1HELP_Play();
    DF_MVIEW_Play();
    DF_MENVBL_Play();
    DF_MBACK_Play();
    DF_M_AC_Play();
    DF_MHELP_Play();
    DF_MDIALOG_Play();
    DF_MBGUIDE_Play();
    MenuKeyGuide_Update();
    Snd_Exec();
    Ads_Exec();
    DF_MVIEW_MakeVU1Packet();
    DF_MENVBL_MakeVu1Packet();
    DF_MBACK_MakeVu1Packet();
    DF_M_AC_MakeVu1Packet();
    DF_MOBJ_MakeVu1Paket();
    DF_MDIALOG_MakeVu1Paket();
    MPktCurrentp = DF_MPKT_MakeEnd(MPktCurrentp);
    DF_MMODEL_Update();
    sceGsSyncPath(0, 0);
    DF_MBACK_Draw();
    DF_M_AC_DrawShadow();
    DF_MHELP_DrawBack();
    DF_MOBJ_Draw();
    MenuMng.frame++;
    sceGsSyncPath(0, 0);
    __MenuILink();
    return MenuMng.ret;
}

void DF_MENU_SetResume(int type)
{
    if (type == 2)
        MenuMng.ret = 0x63;
    else if (type == 1)
        MenuMng.ret = 0x62;
}

/* fsmpg per-frame callback while a menu movie plays (registered by Movie_PlayForMenu) */
void DF_MENU_CallbackMovie(void)
{
    PS2_FRAME(0x10);
    fspad_PadUpdate();
    DF_MPAD_Update();
    if (MenuMng.f_movie_brief) {
        DF_MOBJ_Play();
        DF_M1HELP_Play();
        DF_MDIALOG_Play();
        DF_MBGUIDE_Play();
    }
    DF_MBACK_Draw();
    DF_M_AC_DrawShadow();
    DF_MHELP_DrawBack();
    DF_MOBJ_Draw();
    MenuMng.frame++;
    sceGsSyncPath(0, 0);
}

/* the menu teardown; written out twice in the original (__MENU_MainLoop exit, __MenuILink re-init) */
static void menu_release(void)
{
    MenuTex_UnRegisterCommon();
    MenuTex_UnRegisterJfont();
    MenuTex_UnRegisterControl();
    MenuTex_UnRegisterBndTexture(MenuMng.pTm2CommonBnd);
    MenuMng.pTm2CommonBnd = NULL;
    MenuTex_Clear();
    MenuKeyGuide_Cleanup();
    Bgm_RemoveSeq();
    DF_SND_Remove_SEMENU();
    DF_MFUNC_ClearMemory(MenuBuffTopp, MenuBuffSize >> 2);
}

/* menu start-up after Reset: load, start, wait one field, then the HDD error dialog if the load saw one */
static void menu_boot(void)
{
    int err = DF_MENU_ReadDataLoop();
    __MenuStart();
    sceGsSyncPath(0, 0);
    while (!sceGsSyncV(0)) {
    }
    g_odevCur = 1;
    BuffID2 = 1;
    BuffID = 1;
    if (err || loader_IsOutOfMenuError()) __ShowHddError();
}

static int __MENU_MainLoop(void *inBuff, unsigned int inSize, unsigned int inInitF)
{
    PS2_FRAME(0x50);
    PS2_STACK(struct stMenuRect, rect, 0x30);       /* fade quad, by pointer to DF_MDRAW_DrawColSprite */
    PS2_STACK(struct struct_76b810ec, col, 0x4c);
    if (inInitF) {
        MenuBuffTopp = (unsigned char *)inBuff;
        MenuBuffSize = inSize;
        DF_MENU_Reset();
        menu_boot();
    }
    do {
        Draw_SetFlickerFilter(g_nMenuCopyUOffset, g_nMenuCopyVOffset);
        Draw_SetCopyBlur(g_nMenuBlur);
        if (MenuMng.fadetype) {
            MenuMng.fadecount++;
            if (MenuMng.fadetype == 1)
                MenuMng.alpha = ps2_div32((MenuMng.fadecountmax - MenuMng.fadecount) << 7, MenuMng.fadecountmax);
            else
                MenuMng.alpha = ps2_div32(MenuMng.fadecount << 7, MenuMng.fadecountmax);
            if (MenuMng.fadecount >= MenuMng.fadecountmax) MenuMng.fadetype = 0;
        }
        if (MenuMng.alpha) {
            col.r = 0;
            col.g = 0;
            col.b = 0;
            col.a = MenuMng.alpha;
            rect.x = 0;
            rect.y = 0;
            rect.w = 0x280;
            rect.h = 0x1c0;
            DF_MDRAW_KickReg(0x42, 0x44);
            DF_MDRAW_DrawColSprite(&rect, 1, &col, 0);
        }
        Draw_FlipBufferForMenu();
        DF_MMODEL_UpdateMatrix();
        DF_MPKT_Kick();
        fspad_PadUpdate();
        DF_MPAD_Update();
        Cdvd_Exec();
        loader_AsyncProc(1);
        int run = DF_MOBJ_Play();
        DF_M1HELP_Play();
        DF_MVIEW_Play();
        DF_MENVBL_Play();
        DF_MBACK_Play();
        DF_M_AC_Play();
        DF_MHELP_Play();
        DF_MDIALOG_Play();
        DF_MBGUIDE_Play();
        MenuKeyGuide_Update();
        Snd_Exec();
        Ads_Exec();
        if (!run) break;
        DF_MVIEW_MakeVU1Packet();
        DF_MENVBL_MakeVu1Packet();
        DF_MBACK_MakeVu1Packet();
        DF_M_AC_MakeVu1Packet();
        DF_MOBJ_MakeVu1Paket();
        DF_MDIALOG_MakeVu1Paket();
        MPktCurrentp = DF_MPKT_MakeEnd(MPktCurrentp);
        sceGsSyncPath(0, 0);
        MenuTex_InvalidateCache();
        DF_MBACK_Draw();
        DF_M_AC_DrawShadow();
        DF_MHELP_DrawBack();
        DF_MOBJ_Draw();
        MenuMng.frame++;
        sceGsSyncPath(0, 0);
        __MenuMakePacket2ND(1);
        sceGsSyncPath(0, 0);
        MenuTex_InvalidateCache();
        DF_MOBJ_Draw_2ND();
        DF_MHELP_Draw();
        DF_MDIALOG_Draw();
        DF_MBGUIDE_Draw();
        MenuKeyGuide_Draw();
        DF_MMODEL_Update();
    } while (__MenuILink());
    menu_release();
    return MenuMng.ret;
}

/* inInitF is passed on every pass, so a re-entered loop re-runs the whole start-up when the caller passed 1 */
int DF_MENU_MainLoop(void *inBuff, unsigned int inSize, unsigned int inInitF)
{
    PS2_FRAME(0x1c0);
    PS2_STACK(char[128], name_other, 0x40);   /* handed to interpreted sprintf / Movie_PlayForMenu */
    PS2_STACK(char[128], name6, 0xc0);
    PS2_STACK(char[128], name7, 0x140);
    do {
        signed char g = MenuMng.movie_control;
        if (g > 0) {
            char *fname = movieFileName[g].fname;
            if (g == 2) {
                /* no movie */
            } else if (g == 6) {
                if (fname) {
                    ps2_sprintf_s(name6, menu_fmt_movie, fname);
                    Movie_PlayForMenu(name6, (char *)inBuff, 0, 0, 1, 0, 0);
                }
            } else if (g == 7) {
                if (fname) {
                    ps2_sprintf_s(name7, menu_fmt_movie, fname);
                    Movie_PlayForMenu(name7, (char *)inBuff, 0, 1, 1, 0, 0);
                }
                MenuMng.f_movie_brief = 0;
            } else {
                if (fname) {
                    ps2_sprintf_s(name_other, menu_fmt_movie, fname);
                    Movie_PlayForMenu(name_other, (char *)inBuff, 1, 1, 0, 0, 0);
                }
            }
            Draw_SetupFrameBuffer(0, 0);
            MenuMng.movie_control = 0;
            if (MenuMng.f_movie_end == 1) {
                MenuMng.f_movie_end = 0;
                return MenuMng.ret;
            }
        }
        MenuMng.f_continue = 0;
        __MENU_MainLoop(inBuff, inSize, inInitF);
    } while (MenuMng.movie_control > 0 || MenuMng.f_continue);
    return MenuMng.ret;
}

/* link-play hook, once per menu frame: 0 = leave the menu loop (DF_ILinkInit failed) */
static int __MenuILink(void)
{
    PS2_FRAME(0x30);
    int f = MenuMng.ilink.func_id;
    if (f == 0) return 1;
    MenuMng.ilink.func_id = 0;
    if (f != 1) return 1;
    if (!DF_ILinkInit(0)) return 0;
    menu_release();
    DF_MENU_Reset();
    menu_boot();
    return 1;
}

static void __MenuStart(void)
{
    PS2_FRAME(0x50);   /* sceVu0ViewScreenMatrix (interpreted) takes farz at sp+0 */
    MenuMng.stat = 0;
    DF_MVIEW_Init();
    DF_MHELP_Reset();
    MResultBridge_Init();
    MArenaAsemMng_Init();
    DF_MDATA_GetData(MenuMng.dataTbl[0], 0);   /* the result goes to DF_MPART_Init's a0, which it ignores */
    DF_MPART_Init();
    if (MenuMng.f_new_game == 1) DF_M_AC_Init((struct StAcData *)MenuMng.datAsm);
    MenuWorkp = DF_M_AC_InitWork(MenuWorkp);
    DF_M_AC_SetAll();
    MenuWorkp = DF_MENVBL_Init(MenuWorkp);
    DF_MBACK_Init(1, 0);
    sceVu0ViewScreenMatrix(MAC_SHADOW_SCREEN_MATRIX, 512.0f, 1.0f, -1.0f, 2048.0f, 2048.0f, 0.0f, 65535.0f, 10.0f,
                           65536.0f);
    if (MenuMng.f_new_game == 1) {
        if (gMManageCtr.fPartsAll)   /* debug switch */
            DF_MDBG_PartsAll();
        else {
            DF_MPART_SetAsmDefault();
            DF_MPART_SetShopDefault();   /* asm passes 0 (DWARF: no parameter) */
        }
        DF_MPART_ExtPartsDispose();   /* asm passes -1 (DWARF: no parameter) */
    }
    MenuMng.f_new_game = 0;
    if (loader_IsDisposeExtPartsFlag()) {
        DF_MPART_ExtPartsDispose();
        loader_ClearDisposeExtPartsFlag();
    }
    for (int i = 0; i < 4; i++) DF_MPART_CorrectPlayerParts(i);
    MenuTex_RegisterCommon();
    MenuTex_RegisterJfont();
    MenuTex_RegisterControl();
    MenuKeyGuide_Init();
    int ids[3] = {0x32, 0x33, -1};   /* rodata 0x1edde10 */
    for (int *p = ids; *p != -1; p++) {
        if (!DF_MMD_GetMMD(2, *p)) {
            struct stMmdData *m = DF_MMD_GetNewMMD(NULL);
            DF_MMD_SetData(m, 2, *p, DF_MMD_GetObjectMMD(*p));
        }
    }
    DF_MMANAGE_NewSystem();
}

/* load the menu data blocks, one state per frame; returns 1 when an HDD parts-table error was seen.
 * States 10 and 11 are dead (state 9 jumps to 12); state 13 retries forever on an error. */
int DF_MENU_ReadDataLoop(void)
{
    PS2_FRAME(0x50);
    int result = 0, done = 0;
    unsigned int state = 0;
    do {
        Draw_FlipBufferStandard();
        Cdvd_Exec();
        switch (state) {
        case 0:
            if (Cdvd_CdSync() == 1) break;
            MenuWorkp = (unsigned char *)DF_MFUNC_Aligned(MenuWorkp, 0x40);
            MenuMng.dataTbl[0] = MenuWorkp;
            Cdvd_ReadLsnBlind(0x1004, (char *)MenuMng.dataTbl[0], 0, 0);
            state++;
            break;
        case 1:
            if (Cdvd_CdSync() == 1) break;
            MenuWorkp += DF_MDATA_Remap(MenuMng.dataTbl[0]);
            MAppellationMng_Init();
            DF_MSTR_ReadStart(0);
            state++;
            break;
        case 2:
            if (DF_MSTR_Sync(0) == 1) break;
            MenuWorkp = (unsigned char *)DF_MFUNC_Aligned(MenuWorkp, 0x40);
            MenuMng.tm2Tbl[1] = MenuWorkp;
            Cdvd_ReadLsnBlind(0xfaa, (char *)MenuMng.tm2Tbl[1], 0, 0);
            state++;
            break;
        case 3:
            if (Cdvd_CdSync() == 1) break;
            MenuWorkp += DF_MDATA_Remap(MenuMng.tm2Tbl[1]);
            MenuWorkp = (unsigned char *)DF_MFUNC_Aligned(MenuWorkp, 0x40);
            MenuMng.tm2Tbl[2] = MenuWorkp;
            Cdvd_ReadLsnBlind(0xfa9, (char *)MenuMng.tm2Tbl[2], 0, 0);
            state++;
            break;
        case 4:
            if (Cdvd_CdSync() == 1) break;
            MenuWorkp += DF_MDATA_Remap(MenuMng.tm2Tbl[2]);
            MenuWorkp = (unsigned char *)DF_MFUNC_Aligned(MenuWorkp, 0x40);
            MenuMng.dataTbl[1] = MenuWorkp;
            Cdvd_ReadLsnBlind(0x1005, (char *)MenuMng.dataTbl[1], 0, 0);
            state++;
            break;
        case 5:
            if (Cdvd_CdSync() == 1) break;
            MenuWorkp += DF_MDATA_Remap(MenuMng.dataTbl[1]);
            state++;
            break;
        case 6:
            MenuWorkp = (unsigned char *)DF_MFUNC_Aligned(MenuWorkp, 0x40);
            MenuMng.datAsm = MenuWorkp;
            File_Read(0xbc2, -1, (char *)MenuMng.datAsm, 1);
            state++;
            break;
        case 7:
            if (Cdvd_CdSync() == 1) break;
            MenuWorkp += 0x2590;
            state++;
            break;
        case 8:
            state++;
            break;
        case 9:
            if (Cdvd_CdSync() == 1) break;
            state = 12;   /* skips the dead states 10 / 11 */
            break;
        case 10:   /* dead */
            MenuWorkp = (unsigned char *)DF_MFUNC_Aligned(MenuWorkp, 0x40);
            MenuMng.tm2Tbl[3] = MenuWorkp;
            Cdvd_ReadLsnBlind(0x29, (char *)MenuMng.tm2Tbl[3], 0, 0);
            state++;
            break;
        case 11:   /* dead */
            if (Cdvd_CdSync() == 1) break;
            if (gMManageCtr.fAddDirt) MenuWorkp += DF_MDATA_Remap(MenuMng.tm2Tbl[3]);
            state++;
            break;
        case 12:
            if (Cdvd_CdSync() == 1) break;
            MenuWorkp = (unsigned char *)DF_MFUNC_Aligned(MenuWorkp, 0x10);
            MenuMng.dataTbl[5] = MenuWorkp;
            MenuWorkp += 0x20000;
            loader_ReadMenuPartsTableAsync((char *)MenuMng.dataTbl[5]);
            state++;
            break;
        case 13:
            loader_AsyncProc(1);
            if (loader_IsAsyncBusy()) break;
            if (loader_GetAsyncResult() < 0) {
                loader_SetReadyHdd(0);
                result = 1;
                state = 13;
                break;
            }
            MenuWorkp = DF_MPART_Remap(MenuMng.dataTbl[5], MenuWorkp);
            MenuWorkp = (unsigned char *)DF_MFUNC_Aligned(MenuWorkp, 0x40);
            state++;
            break;
        case 14:
            if (Cdvd_CdSync() == 1) break;
            state++;
            break;
        case 15:
        case 16:
            if (Cdvd_CdSync() == 1) break;
            MenuWorkp = (unsigned char *)DF_MFUNC_Aligned(MenuWorkp, 0x40);
            state++;
            break;
        case 17:
            if (File_IsAsyncBusy()) break;
            File_Read(0x1007, -1, (char *)MenuWorkp, 0);
            state++;
            break;
        case 18: {
            if (File_IsAsyncBusy()) break;
            int sz = File_GetSize(0x1007);
            MenuMng.pTm2CommonBnd = (char *)MenuWorkp;
            MenuTex_RegisterBndTexture(MenuWorkp);
            MenuWorkp += sz;
            state++;
            break;
        }
        case 19:
            if (File_IsAsyncBusy()) break;
            File_Read(0xfb0, -1, (char *)MenuWorkp, 0);
            state++;
            break;
        case 20: {
            if (File_IsAsyncBusy()) break;
            int sz = File_GetSize(0xfb0);
            LAYOUT_RegistBindData(MenuWorkp);
            MenuWorkp += sz;
            state++;
            break;
        }
        case 21:
            MResultMng_Init();
            MRankMng_Init();
            MArenaPointMng_Init();
            state++;
            break;
        default:
            done = 1;
            break;
        }
        sceGsSyncPath(0, 0);
    } while (!done);
    return result;
}

void DF_MENU_Reset(void)
{
    PS2_FRAME(0x30);
    PS2_STACK(float[4], clr, 0x20);   /* clear colour, by pointer to DF_SetDBuffClrCol */
    memset(clr, 0, sizeof(float[4]));
    DF_SendPGVU1();   /* asm passes 0 (DWARF: no parameter) */
    DF_MFONT_Init();
    unsigned char *top = MenuBuffTopp;
    DF_MFUNC_ClearMemory(top, MenuBuffSize >> 2);   /* word count */
    MenuWorkTopp = (unsigned char *)DF_MFUNC_Aligned(top, 0x40);
    MenuWorkp = MenuWorkTopp;
    for (int i = 0; i < 9; i++) MenuMng.dataTbl[i] = NULL;
    for (int i = 0; i < 8; i++) MenuMng.tm2Tbl[i] = NULL;
    MenuMng.player[0].ac_idx = 0;
    MenuMng.player[1].ac_idx = 0;
    DF_MSTR_Init();
    DF_MDIALOG_Init();
    DF_MBGUIDE_Init();
    DF_MMM_Init();
    DF_SND_Setting_SEMENU((char *)MenuWorkp);
    char r = MenuMng.ret;
    if (r == 0x33 || r == 0x32 || r == 0x31 || r == 4 || r == 3 || r == 2)
        Bgm_RegisterSeq(0, (char *)MenuWorkp);
    else
        Bgm_RegisterSeq(Ac_GetDiscType() == 1 ? 0 : 2, (char *)MenuWorkp);
    MenuWorkp = DF_MMM_KindInit(0, MenuWorkp, 0xf49000);
    MenuWorkp = DF_MMM_KindInit(1, MenuWorkp, 0x40000);
    MenuWorkp = DF_MMM_KindInit(2, MenuWorkp, 0x2000);
    unsigned char *p = MenuWorkp;
    MenuWorkp += 0x3100;
    memset(p, 0, 0x3100);
    DF_MDRAW_InitBuffNoSPR((unsigned char *)DF_MFUNC_Aligned(p, 0x80));
    MenuTex_Init();
    MenuWorkp = DF_MOBJ_Init(MenuWorkp);
    MenuWorkp = DF_MHELP_Init(MenuWorkp);
    MenuWorkp = DF_MPKT_Init(MenuWorkp);
    MenuWorkp = DF_MMD_Init(MenuWorkp);
    MenuWorkp = DF_MMODEL_Init(MenuWorkp);
    MenuWorkp = DF_M1HELP_Init(MenuWorkp);
    Draw_SetupFrameBuffer(0, 0);
    MenuMng.fadecountmax = 0;
    MenuMng.fadecount = 0;
    MenuMng.alpha = 0;
    MenuMng.fadetype = 0;
    DF_SetDBuffClrCol(&DBUFF, clr);
    DF_SetDBuffClrCol(&DBUFF2, clr);
    ZeroMemory(&g_nextstage, 0x640);   /* main-game state shared with the sortie code */
}

void DF_MENU_Init(void)
{
    PS2_FRAME(0x20);
    if (!g_isInitMenu) {
        DF_MFUNC_ClearMemory(&MenuMng, 0x3a);   /* 0x3a words = sizeof MenuMng; redundant with the clear below */
        g_isInitMenu = 1;
    }
    char keep = MenuMng.f_new_system;
    MenuBuffTopp = NULL;
    MenuBuffSize = 0;
    DF_MFUNC_ClearMemory(&MenuMng, 0x3a);
    MenuMng.ret = 0;
    MenuMng.f_new_game = 1;
    MenuMng.language = 1;
    MenuMng.movie_control = 0;
    DF_MPAD_Init();
    DF_MDRAW_ChangeUseSPR(0);
    MenuMng.f_new_system = keep;
    MenuMng.f_movie_brief = 0;
}

void __MenuMakePacket2ND(int inEnvF)
{
    PS2_FRAME(0x30);   /* DF_M_AC_MakeEmbPacket / DF_MkDL_EnvBall are interpreted */
    unsigned int *startp = MPktCurrentp;
    DF_MVIEW_MakeVU1Packet();
    if (inEnvF) DF_MENVBL_MakeVu1Packet();
    DF_M_AC_MakeEmbPacket();
    DF_MOBJ_MakeVu1Paket2ND();
    if (inEnvF) DF_MDIALOG_MakeVu1Paket();
    MPktCurrentp = DF_MPKT_MakeEnd(MPktCurrentp);
    FlushCache(0);
    ps2_dma_wait_vif1();
    ps2_dma_kick_tadr_madr(1, 0x105, PS2PTR(startp), 0xffffffffu);   /* TADR, MADR = -1, CHCR */
}
