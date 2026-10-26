/* src/main/loop_sortie.cpp — the battle ("sortie") main loop: LoopSortie_Begin loads the stage and every AC (users, mission
 * ACs, friend NPC table, arena / AI ACs, in that slot order), LoopSortie_Do runs one frame per iteration (pads ->
 * COM AI pads -> AC control -> camera -> stage / effect / event procs -> display list), LoopSortie_DrawIndicator is
 * the HUD pass, DrawAfter the post-display-list effect pass, LoopSortie_End the shutdown with the music fade-out;
 * plus the pause / area-map / movie-skip handlers and the link / split-screen player numbering. */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

#include "acnb/hooks.h"


static inline int sortie_mode(void) { return g_sortie.pconstant->modeSortie; }
static inline int is_versus_mode(int m) { return m == 3 || m == 2 || m == 6 || m == 5 || m == 4; }

/* Team table of the split-screen battle (mode 5): the entries of the enabled pads first, then the others. */
static void make_team_cond(unsigned char *res, unsigned char *src, unsigned int mask)
{
    unsigned char *ptr = src;
    for (int ii = 0; ii < 4; ii++, ptr++)
        if (mask & (1 << ii)) *res++ = *ptr;
    for (int ii = 0; ii < 4; ii++, src++)
        if (!(mask & (1 << ii))) *res++ = *src;
}

static void set_team_for_tb(int acid)
{
    unsigned char tc = team_condition[acid];
    int str;
    if (MyI.vsform == 2) {
        if (tc & 1) {
            DF_PLAY_ResetCF(acid, 1);
            str = (tc & 0x10) ? 0x10 : 0x11;
        } else if (tc & 2) {
            DF_PLAY_ResetCF(acid, 2);
            str = (tc & 0x10) ? 0x12 : 0x13;
        } else {
            DF_PLAY_ResetCF(acid, (unsigned short)(1 << (acid & 31)));
            return;
        }
        DF_ACSTAT_AddMapString(&MyAC[acid], 0.0f, -10.0f, 0.0f, 100000.0f, str);
    } else if (MyI.vsform == 1) {
        if (tc & 1) {
            DF_PLAY_ResetCF(acid, 1);
            if (tc & 0x10) { str = 7; MyI.leader[0] = (short)acid; } else str = 8;
        } else if (tc & 2) {
            DF_PLAY_ResetCF(acid, 2);
            if (tc & 0x10) { str = 9; MyI.leader[1] = (short)acid; } else str = 10;
        } else {
            DF_PLAY_ResetCF(acid, (unsigned short)(1 << (acid & 31)));
            return;
        }
        DF_ACSTAT_AddMapString(&MyAC[acid], 0.0f, -10.0f, 0.0f, 100000.0f, str);
    }
}

/* Team setup shared by every AC kind in __AcReadModelOrInit's init pass. */
static void init_team(int acid)
{
    int m = sortie_mode();
    if (m == 3) {
        DF_PLAY_ResetCF(acid, acid < 2 ? 1 : 2);
        MyI.leader[0] = 0;
        MyI.leader[1] = 2;
    } else if (m == 5 || m == 4) {
        set_team_for_tb(acid);
        if (!MyI.dstwep_en) AcCtrl_SetNoDeestroyParts(&g_ac[acid], 1);
    }
}

static inline float *ac_spawn(int acid) { return SysInfo->locInit[acid]; }

static void init_at_spawn(int acid)
{
    AcCtrl_BootupStage(acid);
    float *loc = ac_spawn(acid);
    DF_PLAY_InitLocation(&g_ac[acid], loc, loc[3]);
    init_team(acid);
}

static void init_at_mission_loc(int acid, struct_441fa347 *info)
{
    AcCtrl_BootupStage(acid);
    AcPlayerSt *acp = &g_ac[acid];
    memcpy(acp->loc, info->loc, 16);
    memcpy(acp->ang, info->ang, 16);
}

/* isInit 0: load every AC's models / AI; 1: place them and set teams. Slot order: user players, mission ACs
 * (stage data), friend NPC table (versus COMs come from here), arena rankers, AI ACs. */
static int __AcReadModelOrInit(int isInit)
{
    PS2_FRAME(0x90);
    int acid = 0, comno = 0;
    for (int i = 0; i < NowUserPlayerNum; i++, acid++) {
        if (!UseAI[acid]) {
            if (!isInit) {
                if (!AcLoader_LoadAndSetup((char *)g_pbufAc[acid], acid, -1, AcOpt.low)) return -1;
            } else {
                AcCtrl_BootupStage(acid);
                init_team(acid);
            }
        } else {
            if (!isInit) {
                if (!AcLoader_LoadAndSetup((char *)g_pbufAc[acid], acid, -1, 1)) return -1;
            } else {
                init_at_spawn(acid);
                init_team(acid);   /* the original runs the team setup twice for AI-driven user slots */
            }
            comno++;
        }
    }
    int nmis = Array->head.acnum + Array->head.mtnum;
    for (int i = 0; i < nmis; i++, comno++) {
        struct_441fa347 *info = Stage_GetMissionAcInfo(i);
        if (info->chrid != 0) { DF_NPC_ReadFrndComData(); continue; }
        if (!info->fid) {
            if (!isInit) {
                if (DF_LOOP_ReadMissionComAC(acid, comno, info->id) < 0) return -1;
            } else init_at_mission_loc(acid, info);
        } else {
            if (!isInit) {
                if (DF_NPC_ReadFrndAC() < 0) return -1;
            } else {
                init_at_mission_loc(acid, info);
                DF_PLAY_ResetCF(acid, 1);
            }
        }
        acid++;
    }
    StgInfo.top_fac = (unsigned short)acid;
    StgInfo.num_fac = 0;
    StgInfo.num_fmt = 0;
    StgInfo.num_ffg = 0;
    int nfrnd = DF_NPCTAB_GetFrndNum();
    for (int i = 0; i < nfrnd; i++) {
        if (DF_NPCTAB_IsForce(i)) {
            if (!isInit) {
                if (!AcLoader_LoadAndSetup((char *)g_pbufAc[acid], acid, -1, 1)) return -1;
            } else init_at_spawn(acid);
            acid++;
            comno++;
            StgInfo.num_fac++;
            continue;
        }
        if (UseAI[acid]) continue;
        int fid = DF_NPCTAB_GetFrndID(i);
        DF_NPCTAB_GetMechId(i);
        int type = DF_NPCTAB_GetMechType(i);
        if (type == 0) {
            int loc = DF_NPCTAB_GetLocID(i);
            if (!isInit) {
                if (DF_LOOP_ReadMissionComAC(acid, comno, fid) < 0) return -1;
            } else {
                AcCtrl_BootupStage(acid);
                int m = sortie_mode();
                if (m == 3 || m == 5 || m == 4) {
                    float *sp = ac_spawn(acid);
                    DF_PLAY_InitLocation(&g_ac[acid], sp, sp[3]);
                    init_team(acid);
                } else {
                    DF_PLAY_InitLocation(&g_ac[acid], Array->frndloc[loc], Array->frndloc[loc][3]);
                    DF_PLAY_ResetCF(acid, 1);
                    StModel *mp = g_ac[acid].mp;
                    if (m == 0 || m == 1) DF_TALK2_SetModel(DF_TALK2_SearchTCom(DF_NPCTAB_GetFrndID(i)), mp);
                }
            }
            acid++;
            comno++;
            StgInfo.num_fac++;
        } else if (type == 1) {
            DF_NPC_ReadFrndComData();
            comno++;
            StgInfo.num_fmt++;
        } else {
            StgInfo.num_ffg++;
        }
    }
    for (int i = 0; i < NowArenaACNum; i++, acid++, comno++) {
        if (!isInit) {
            if (DF_LOOP_ReadMissionComAC(acid, comno, g_sortie.pconstant->idRanker[i]) < 0) return -1;
        } else init_at_spawn(acid);
    }
    for (int i = 0; i < NowAIACNum; i++, acid++) {
        if (!isInit) {
            if (!AcLoader_LoadAndSetup((char *)g_pbufAc[acid], acid, -1, 1)) return -1;
        } else init_at_spawn(acid);
    }
    return 0;
}

/* Friendly NPC models of the mission (none in Versus): one model slot per clone after the stage characters. */
static void DF_Read_FrndModel_All(void)
{
    PS2_FRAME(0xd0);
    PS2_STACK(float[3][4], ofs_tab, 0xa0);
    int aid = Array->head.cnum;
    StgInfo.top_fchr = (unsigned short)aid;
    StModel *mp = &Chr[aid];
    StModel *mps = &ChrS[aid];
    int fnum = DF_NPCTAB_GetFrndNum();
    DF_NPCTAB_SetTBPOfs(Array->head.sysinfo.tbpChrOfs, Array->head.sysinfo.cbpChrOfs);
    int arrid = 0;
    for (int ii = 0; ii < fnum; ii++) {
        if (DF_NPCTAB_IsForce(ii)) continue;
        int mtype = DF_NPCTAB_GetMechType(ii);
        if (!mtype) continue;
        DF_NPCTAB_GetFrndID(ii);   /* results unused */
        DF_NPCTAB_GetMechId(ii);
        int clone;
        if (mtype == 2) {
            ofs_tab[0][0] = 0.0f; ofs_tab[0][1] = 0.0f; ofs_tab[0][2] = 0.0f; ofs_tab[0][3] = 1.0f;
            ofs_tab[1][0] = 17.0f; ofs_tab[1][1] = 5.0f; ofs_tab[1][2] = 20.0f; ofs_tab[1][3] = 1.0f;
            ofs_tab[2][0] = -17.0f; ofs_tab[2][1] = 5.0f; ofs_tab[2][2] = 20.0f; ofs_tab[2][3] = 1.0f;
            clone = 3;
        } else if (mtype == 1) {
            ofs_tab[0][0] = 0.0f; ofs_tab[0][1] = 0.0f; ofs_tab[0][2] = 0.0f; ofs_tab[0][3] = 1.0f;
            clone = 1;
        } else clone = 0;
        if (clone <= 0) continue;
        /* empty stub in this build (jr ra): the original passes (mp, mps, aid, ii, Farray + arrid, clone, ofs_tab) */
        DF_NPC_SetupChr();
        if (sortie_mode() == 0 || sortie_mode() == 1)
            DF_TALK2_SetModel(DF_TALK2_SearchTCom(DF_NPCTAB_GetFrndID(ii)), mp);
        aid += clone;
        arrid += clone;
        mp += clone;
        mps += clone;
    }
    (void)arrid;
    (void)mps;
}

/* Link / split-screen battles: the player numbers this console controls and the camera of each view. */
static void DF_SetLinkPlayerNo(void)
{
    PS2_FRAME(0x10);
    PS2_STACK(int[4], players, 0);
    StSortieConstant *c = g_sortie.pconstant;
    if (!c->isSplit) {
        if (MyI.link_index < NowUserPlayerNum) {
            MyI.myplayerno[0] = (char)MyI.link_index;
            ViewCamID[0] = MyI.myplayerno[0];
        } else {
            MyI.livemon = 1;
            ViewCamID[0] = 4;
        }
    } else if (MyI.link_index < 2 && !MyI.replaymode) {
        if (c->modeSortie == 5) {
            unsigned int mask = (unsigned int)(int)play_info.f_enable_pad;
            int id = 0;
            for (int ii = 0; ii < 4; ii++) players[ii] = (mask & (1 << ii)) ? id++ : 4;
            /* COMs take the free pad slots - but the break leaves jj on the slot just written, so every COM
             * rewrites the first free slot (original quirk, kept) */
            int num = play_info.com_num;
            int *p = players;
            int jj = 0;
            for (int m = 0; m < num; m++) {
                for (; jj < 4; jj++, p++) {
                    if (!(mask & (1 << jj))) { *p = id++; break; }
                }
            }
            MyI.myplayerno[0] = (char)players[MyI.link_index * 2];
            MyI.myplayerno[1] = (char)players[MyI.link_index * 2 + 1];
        } else {
            MyI.myplayerno[0] = (char)(MyI.link_index << 1);
            MyI.myplayerno[1] = (char)(MyI.myplayerno[0] + 1);
        }
        int limit = play_info.player_num + play_info.com_num;
        ViewCamID[0] = MyI.myplayerno[0];
        if (ViewCamID[0] >= limit) ViewCamID[0] = 4;
        ViewCamID[1] = MyI.myplayerno[1];
        if (ViewCamID[1] >= limit) ViewCamID[1] = 4;
        ViewNum = 2;
    } else {
        MyI.livemon = 1;
        ViewNum = 1;
        ViewCamID[0] = 4;
    }
}

void LoopSortie_DrawIndicator(void)
{
    PS2_FRAME(0x30);
    ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 10), 10); GS_NewList();
    GS_SetRegister(0x18, (unsigned long long)((g_odevCur ? 8 : 0) + 0x7900) << 32 | 0x7000);
    GS_SetRegister(0x4e, 0x131000070ull);
    GS_FlushList(); ps2_rq_tag_end();
    if (!MyI.livemon && !MyI.syskeyoff && !MyI.pause && MyI.gauge) {
        sceGsSyncPath(0, 0);
        draw_sight_screen();
    }
    if (!MyI.livemon) {
        DF_ACST2_DrawStat(MyI.pause, MyI.gauge);
        for (int v = 0; v < ViewNum; v++) {
            if (ViewCamID[v] < 4) {
                sceGsSyncPath(0, 0);
                DF_SGNL_Draw(ViewCamID[v]);
            }
        }
    }
    if (!MyI.pause) {
        sceGsSyncPath(0, 0);
        MsgWnd_DrawAfter();
    }
    sceGsSyncPath(0, 0);
    DF_FADE_Draw();
    if (MyI.pause) {
        sceGsSyncPath(0, 0);
        DF_PAUSE_Draw();
    }
}

int LoopSortie_Do(void)
{
    PS2_FRAME(0x40);
    int kicked = 0;
    __StartLoopSortie();
    DF_FADE_Exec();
    SetTrainingTrendWeapon(&g_ac[0]);
    SetTrainingTrendInitAP(g_ac[0].hp / 4);
    if (sortie_mode() == 0) AcWep_ReleaseWeapon(&g_ac[0], g_ac[0].equip_weapon);
    for (;;) {
        if (!MyI.mov_skip) {
            MyI.draw_skip = 0;
            kicked = MapKickP ? kicked + 1 : 0;
            if (kicked == 1) {
                DF_SetDBuffClrCol(&DBUFF, CopyOfSysInfo.colBg);
                DF_SetDBuffClrCol(&DBUFF2, CopyOfSysInfo.colBg);
            }
            Draw_FlipBufferForSortie();
        } else {
            DF_WAIT_Sync();
            MyI.draw_skip = 1;
        }
        if (MyI.mainproc) {
            ps2_dma_wait_vif1();
            ps2_dma_wait_gif();
            sceGsSyncPath(0, 0);
            break;
        }
        if (!MyI.draw_skip) Draw_KickDmaDl();
        else MapKickP = 0;
        Pad_UpdateData();
        if (KeySyncProc() < 0) break;
        DF_EVT_TermProc();
        if (MyI.forward) {
            if (s_first_wait > 0) {
                for (int i = 0; i < 4; i++) Pad_NeutralDataCompact(&s_keyCur[i]);
                BothKey = 0;
            }
            if (MyI.syskeyoff) {
                for (int i = 0; i < 4; i++) Pad_NeutralDataCompact(&s_keyCur[i]);
                BothKey = 0;
                Pad_ClearData();
            }
            __ExecSkipPolyg();
            PauseProc();
            if (MyI.restart) { MyI.restart = 0; break; }
            ReplayProc();
            if (!MyI.pause) {
                if (AreaMap()) goto frame_end;
                MyI.rproceed = (char)DF_RPLAY_Count(FRAME, MyI.replaymode, MyI.replayon);
                if (MyI.replaymode && MyI.rproceed < 0) DF_EVT_QuitReplay();
                HetUpdateProc();
                HitBoxUpdateProc();
                if (s_first_wait < 1) {
                    Com_Dmg_Proc();
                    ComProc();
                    if (hook_sortie_logic_frame) hook_sortie_logic_frame();   /* port hook (debug probes) */
                }
                CtrlProc();
                for (int i = 0; i < NowPlayerNum; i++) AcCtrl_UpdateMatrix(&g_ac[i]);
            }
            CameraProc();
            if (!MyI.pause) {
                DF_StageEventProc();
                PolygProc();
                EneSq_Proc();
                DF_FIGHT_FightProc();
                DF_FIGHT_UpdateAllEnemyMatrix();
                DF_SPELL2_SpellProc();
                DF_COLL_Update();
                DF_MAGIC2_Proc();
                DF_MAGIC3_Proc();
                DF_EYE2_Proc();
                CLOUD_Proc();
                DF_EVMNG_Proc();
                if (MyI.padoff != 1) SetLockTime();
                if (StgConf.script) DF_ScriptProc();
                EcmField_Exec();
                AbVal_Proc();
                CombatRecord_OnFrameMove(&g_ac[0]);
                DF_PointLightProc();
                if (!MyI.mov_play && (sortie_mode() == 0 || sortie_mode() == 1)) DF_TALK2_Exec();
                MsgWnd_Exec();
                DF_ScrMessage_Proc();
                int m = sortie_mode();
                if (m == 3 || m == 2 || m == 6 || m == 5 || m == 4)
                    for (int i = 0; i < NowPlayerNum; i++) DF_SGNL_Proc(i);
            }
        }
        DF_FADE_Exec();
        if (!MyI.mov_skip) {
            DF_ADSVC_Proc();
            DF_RTRC_Proc();
            Snd_Exec();
            Ads_Exec();
        }
        Stage_ExecTransSpTexture();
        Stage_ExecMapScreenEffect();
        if (!ReplayForward && !MyI.draw_skip) {
            STAGE_MAddLight();
            DF_MkDL_Frame();
        }
        ps2_dma_wait_vif1();
        sceGsSyncPath(0, 0);
        DrawAfter();
        if (!MyI.pause && MyI.forward) {
            FRAME++;
            if (hook_sortie_frame_end)   /* port hook (debug probe ACNB_FPU_LOG) */
                hook_sortie_frame_end(FRAME, &g_ac[0], (char *)ps2_impure_ptr + 0xa8);
            if (s_first_wait < 1) {
                if (AC_VS_RESTTIME > 0) AC_VS_RESTTIME--;
            } else s_first_wait--;
        }
    frame_end:
        sceGsSyncPath(0, 0);
        s_frameSortie++;
    }
    sceGsSyncPath(0, 0);
    return 0;
}

/* MWCC's float -> u32 conversion (values >= 2^31 converted with the top bit split off). */
static inline unsigned int sortie_ftou(float v)
{
    if (v >= 2147483648.0f) return (unsigned int)ps2_ftoi(v - 2147483648.0f) | 0x80000000u;
    return (unsigned int)ps2_ftoi(v);
}

/* Battle shutdown: wait for the DMA, stop link / pads, fade the music out over 60 frames, release the stage. */
void LoopSortie_End(void)
{
    PS2_FRAME(0x20);
    DF_LOOP_BlackMask();
    ps2_dma_wait_vif1();
    ps2_dma_wait_gif();
    sceGsSyncPath(0, 0);
    int m = sortie_mode();
    if (m < 2 || m == 7) {
        if (Array) DFene_SEStopAll();
    } else if (m == 4 && play_info.connected_num >= 2) {
        fslnk_SetCheckAlive(0);
        fslnk_LinkSyncStop();
        fslnk_LinkSyncTerm();
    }
    sceGsSyncV(0);
    fspad_PadSetVibra(0, 0, 0);
    fspad_PadSetVibra(1, 0, 0);
    fspad_PadUpdate();
    sceGsSyncV(0);
    for (int i = 0; i < 30; i++) {
        float rate = (float)(0x1d - i) / 30.0f;
        float c = fsmath_CosVU0(1.5707964f * rate);
        float v = 16383.0f * (0.5f + 0.5f * (1.0f - c));
        DF_SND_SetMVolume((unsigned short)sortie_ftou(v));
        sceGsSyncV(0);
    }
    for (int i = 0; i < 30; i++) {
        float rate = (float)(i + 1) / 30.0f;
        float c = fsmath_CosVU0(1.5707964f * rate);
        float v = 16383.0f * (c * 0.5f);
        DF_SND_SetMVolume((unsigned short)sortie_ftou(v));
        sceGsSyncV(0);
    }
    DF_SND_SetRevVolume(0);
    for (int i = 0; i < 30; i++) sceGsSyncV(0);
    Snd_EndStage();
    sceSdRemote(1, 0x8000, 0);
    MsgWnd_Clear();
    sceGsSyncV(0);
    PLIT_Reset();
    PTEMP_Reset();
    HEFF_Reset();
    WETH_Reset();
    DOOR_Destroy();
    WEPICON_Destroy();
    EcmField_Cleanup();
    Stage_End();
    Sortie_EndStage();
    CopyCombatRecordToTrainingTrend();
    Array = 0;
    sceGsSyncV(0);
    DF_SND_SetMode(1);
    DF_SND_SetReverb(5, 5000, 5000, 0, 0, 6000, 6000);
    DF_SND_SetMVolume(0x3fff);
    Draw_SetupFrameBuffer(0, 0);
    Draw_SetLastDrawFunc(0);
}

int LoopSortie_Begin(void)
{
    PS2_FRAME(0x820);
    Draw_SetupFrameBuffer(1, 1);
    Draw_SetLastDrawFunc(GFN(LoopSortie_DrawIndicator));
    sceSdRemote(1, 0x8000, 0);
    Sortie_BeginStage();
    char *secom = (char *)((unsigned int)(g_pbufGlobal + 0xff) & 0xffffff00u);
    if (!File_Read(0x9c4, -1, secom, 1)) return -1;
    DF_SND_Setting_SECOM((StBgmHead *)secom);
    MyI.fade = 0;
    MyI.fadetime = 0;
    MyI.padoff = 1;
    MyI.replayon = 1;
    MyI.rproceed = 0;
    MyI.message = 0;
    MyI.ilinkerr = 0;
    MyI.pause = 0;
    if (sortie_mode() == 4 || sortie_mode() == 5) MyI.gameproc = 0;
    MyI.syskeyoff = 0;
    FRAME = 0;
    MyI.deadstop = 0;
    MyI.dstwep_en = 1;
    MyI.mov_play = 0;
    MyI.mov_skip = 0;
    MyI.draw_skip = 0;
    MyI.areaovered = 0;
    DRAW_Reset();
    MAPSTR_Reset();
    AcLoader_Init();
    DF_SIGHT_SetVFCFactor(0.5f, 0.362f);
    DF_MkDL_SetVFCFactor(0.5f, 0.362f);
    MyI.camplayerno = 0;
    AcOpt.shadow = 1;
    MyI.link_index = 0;
    AcOpt.txx = 1;
    MyI.connected = 1;
    MyI.vsnext = 0;
    DF_MkDL_ViewReset();
    NowArenaACNum = 0;
    NowAIACNum = 0;
    for (int i = 0; i < 4; i++) UseAI[i] = 0;
    switch (sortie_mode()) {
    case 0: case 1:
        MyI.myplayerno[0] = 0;
        NowAIACNum = 0;
        MyI.myplayerno[1] = 1;
        NowPlayerNum = 1;
        NowUserPlayerNum = 1;
        break;
    case 2:
        MyI.myplayerno[0] = 0;
        AcOpt.txx = 0;
        NowUserPlayerNum = 1;
        NowArenaACNum = 1;
        NowPlayerNum = 2;
        break;
    case 3:
        NowArenaACNum = 2;
        MyI.myplayerno[0] = 0;
        AcOpt.txx = 0;
        NowUserPlayerNum = 1;
        NowPlayerNum = 3;
        AcOpt.low = 1;
        break;
    case 4: case 5:
        if (sortie_mode() == 5) make_team_cond(team_condition, (unsigned char *)play_info.team_condition, play_info.f_enable_pad);
        else CopyMemory(team_condition, play_info.team_condition, 4);
        MyI.pikwep_en = 0;
        AcOpt.low = 1;
        MyI.dstwep_en = 0;
        AcOpt.txx = 0;
        NowUserPlayerNum = play_info.player_num;
        MyI.connected = play_info.connected_num;
        MyI.link_index = play_info.self_id;
        MyI.vsform = play_info.vs_form;
        NowPlayerNum = NowUserPlayerNum;
        DF_SetLinkPlayerNo();
        for (int i = 0; i < 4; i++) UseAI[i] = play_info.ai_mode[i] & 1;
        break;
    case 6:
        AcOpt.txx = 0;
        MyI.myplayerno[0] = 0;
        NowUserPlayerNum = 1;
        NowAIACNum = 1;
        NowPlayerNum = 1;
        break;
    }
    File_Read(g_sortie.pconstant->map * 100 + 0x2711, -1, g_pbufRead, 0);
    NL_LoopNowLoading(0);
    unsigned char head[0x800] __attribute__((aligned(16)));
    CopyMemory8b(head, g_pbufRead, 0x800);
    Memm_AllocStageBuffer(*(short *)head << 6);
    StgConf.use_mstm = sortie_mode() != 4;
    StgConf.use_ads = !StgConf.use_mstm;
    float vol = ((float)SoundManageData.scene_bgmvol / 127.0f) * (float)head[3];
    SoundManageData.scene_bgmvol = (unsigned char)(vol < 2147483648.0f ? ps2_ftoi(vol) : ps2_ftoi(vol - 2147483648.0f));
    if (SoundManageData.scene_bgmvol > 0x7f) SoundManageData.scene_bgmvol = 0x7f;
    ps2_srand(MyI.random_seed);
    MyI.bgm_random = ps2_rand();
    DestObjMem_ReadMissionState(g_sortie.pconstant->map, g_nextstage.destobj_stat_id);
    if (!Stage_Load(g_sortie.pconstant->map, *(unsigned int *)(head + 4))) return -4;
    EcmField_Init();
    if ((sortie_mode() == 4 || sortie_mode() == 5) && !g_sortie.pconstant->isSplit && NowPlayerNum < 3) AcOpt.low = 0;
    AcLoader_AllocAcBuffer();
    for (int i = NowUserPlayerNum; i < NowPlayerNum; i++) Game_SetKeyDefault(i);
    InitMyOption();
    int rsize = Memm_GetReplayBuffSize();
    if (sortie_mode() == 6) rsize -= 0x35000;
    DF_RPLAY_Init(MyI.replaymode, NowPlayerNum, Memm_GetReplayBuff(), rsize - 0x80);
    if (sortie_mode() == 0 || sortie_mode() == 1) {
        DF_TALK2_Init(Memm_Alloc(1, 0xe0000, 0x80), 0xe0000);
        DF_TALK2_Start();
    }
    if (MyI.rec_ai) Memm_Alloc(1, 0x2400, 0x10);
    int reduce = (sortie_mode() == 4 || sortie_mode() == 5) && g_sortie.pconstant->map <= 99;
    DF_MAGIC2_AllInit(reduce);
    DF_MAGIC3_Init(reduce);
    DF_EYE2_Init(ViewNum == 1 ? 0x20 : 0x10);
    DF_SPELL2_Init();
    DF_SHAKE_Init();
    CLOUD_Init();
    for (int i = 0; i < 4; i++) AcCtrl_Init(i);
    DF_Read_FrndModel_All();
    WEPICON_Create();
    if (__AcReadModelOrInit(0) < 0) { LoopSortie_End(); return -3; }
    if (is_versus_mode(sortie_mode())) MyI.padoff = 0;
    if (__AcReadModelOrInit(1) < 0) { LoopSortie_End(); return -3; }
    if (g_nextstage.autoRankerMake) {
        AutoGenLogic_Init();
        AutoGenLogic_CreateComLogic(&g_ac[1], &g_nextPilotInfo, g_nextPilotClass);
        AutoGenLogic_Finish();
    }
    init_stat_system();
    EmsVag_Reset();
    Ads_Reset();
    NL_LoopNowLoading(1);
    Stage_Begin();
    NL_LoopNowLoading(1);
    MyI.gauge = !(sortie_mode() == 0 || sortie_mode() == 1);
    MyI.padoff = 1;
    NL_LoopNowLoading(1);
    DF_EFF_InitBlur();
    SHIMMER_Init();
    DF_FADE_Init(1);
    NL_LoopNowLoading(1);
    DF_CAM_InitAll();
    DF_CALC_InitNeutralCamera();
    ViewMode_CheckAll(0);
    NL_LoopNowLoading(1);
    DF_AMAP_Init();
    DF_LOOP_BlackMask();
    DF_SND_SetMode(g_gamedata[0].option.stereo);
    Snd_BeginStage();
    DF_StageEventProcInit();
    AbVal_Init(1);
    MkDL_Init(Map, Array->head.mnum, Obj, Array->head.onum);
    if (sync_link(1) < 0) return -2;
    if (sortie_mode() == 4 || sortie_mode() == 5) sceGsResetGraph(0, 1, 2, 1);
    return 0;
}

/* Select (edge) opens the area map in single-player sorties; its quit choice ends the mission. */
static int AreaMap(void)
{
    PS2_FRAME(0x10);
    int rmode = MyI.replaymode;
    int padon = (BothKey & BothKeyB & 1) != 0;
    int m = sortie_mode();
    if ((m == 0 || m == 1) && !rmode && !MyI.padoff && FRAME && padon) {
        Vibra_StopAll();
        Vags_Stop();
        int player = MyI.myplayerno[0];
        if (DF_AMAP_MainLoop(MyAC[player].loc, MyAC[player].ang, g_ac[player].param.atlas_stat) == 1) DF_EVT_GameQuit(1);
        return 1;
    }
    return 0;
}

static int ReplayProc(void)
{
    PS2_FRAME(0x10);
    if (!MyI.replaymode || MyI.padoff || !FRAME) return 0;
    if (BothKey & BothKeyB & 0x2000) {
        DF_EVT_QuitReplay();
        MyI.replayon = 0;
        return 0;
    }
    int p = MyI.myplayerno[0];
    if (!(s_keyCur[p].bits & ~s_keyPrev[p].bits & 0x8000)) return 0;
    if (ViewCamID[0] < 4) {
        if (++ViewCamID[0] >= NowPlayerNum) ViewCamID[0] = 4;
    } else ViewCamID[0] = 0;
    int v = ViewCamID[0];
    CAM[v].groupidbk = 0xff;
    if (v < 4) {
        WEPICON_SetWepName(v, -1);
        if (!(g_ac[ViewCamID[0]].areastat & 2)) DFcam_SetNoDelay(ViewCamID[0]);
    }
    return 0;
}

/* Camera mode after the pause menu closes. */
static void __ChangeViewMode_BehindCockpit(int nPlayerID)
{
    PS2_FRAME(0x20);
    int m = sortie_mode();
    if (m == 0 || m == 1 || m == 2 || m == 3) {
        if (!Game_IsEnableCockpitView(nPlayerID)) ViewMode_CheckAll(0);
        else if (Pad_IsOn(nPlayerID, 1) == 1) {
            if (!CAM[nPlayerID].oview_mode) DFcam_SetViewMode(nPlayerID, 1);
            else ViewMode_CheckAll(0);
        } else DFcam_SetViewMode(nPlayerID, CAM[0].oview_mode);   /* CAM[0], not CAM[nPlayerID] (the caller passes 0) */
    } else ViewMode_CheckAll(0);
}

/* Start (edge) opens the pause menu; while paused, runs the menu and applies its result. */
static int PauseProc(void)
{
    PS2_FRAME(0x20);
    unsigned int mkey = BothKey;
    unsigned int mkeyB = BothKeyB;
    if (MyI.pause) {
        int retval = sortie_mode() ? DF_PAUSE_Proc(mkey, mkey & mkeyB) : DF_PAUSE_Restart_Proc(mkey, mkey & mkeyB);
        if (retval) {
            DF_FADE_SetProc(0, 0, 0);
            if (MyI.pause == 1) {
                __ChangeViewMode_BehindCockpit(0);
                if (retval == 1) {
                    DF_EVT_GameQuit(1);
                    Sortie_SetEscapeSortie(1);
                }
                if (retval == 3) {
                    MyI.restart = 1;
                    g_nextstage.quickStart = 1;
                }
            } else if (MyI.pause == 2) {
                if (retval == 1) MyI.vsnext = 2;
            } else if (MyI.pause == 3) {
                if (retval == 1) MyI.vsnext = 1;
                else if (retval == 3) MyI.vsnext = 2;
            }
            MyI.pause = 0;
        }
    } else if ((mkey & mkeyB & 8) && !MyI.replaymode && !MyI.padoff && FRAME && g_sortie.pwork->isEnablePause) {
        MyI.pause = 1;
        Vibra_StopAll();
        DF_PAUSE_Init();
        DF_FADE_SetProc(0, 1, 0);
        if (sortie_mode() == 4 || sortie_mode() == 5) {
            for (int i = 0; i < NowPlayerNum; i++) DFcam_SetViewMode(i, 7);
        } else DFcam_SetViewMode(0, 7);
    }
    return 0;
}

void LOOP_SetPause(int mode)
{
    MyI.pause = (char)mode;
    Vibra_StopAll();
}

/* Post-display-list pass: stage-event after-draw, lens flare / hit effects / shake (single view), shimmer, blur. */
static void DrawAfter(void)
{
    PS2_FRAME(0x10);
    if (!FRAME || MyI.draw_skip) return;
    sceGsSyncPath(0, 0);
    DF_StageEventProcAfter();
    if (CopyOfSysInfo.lensflareon && StgConf.lensflare && ViewNum == 1) {
        sceGsSyncPath(0, 0);
        DF_EFF_DrawLensFlare(CopyOfSysInfo.lensflare.sun);
    }
    if (StgConf.draw_hfx && ViewNum == 1) {
        sceGsSyncPath(0, 0);
        DF_EFF2_HitEffectProcAfter();
    }
    if (StgConf.draw_shk && ViewNum == 1) {
        sceGsSyncPath(0, 0);
        DF_SHAKE_Proc();
    }
    SHIMMER_Draw();
    sceGsSyncPath(0, 0);
    DF_EFF_DrawBlur();
}

/* Lock sights and map strings of each view, drawn with that view's scissor; then view 1's scissor again. */
static void draw_sight_screen(void)
{
    PS2_FRAME(0xf0);
    PS2_STACK(float[4][4], wsmat, 0x70);
    PS2_STACK(unsigned char[0x40], vp, 0xb0);
    unsigned short half = g_odevCur ? 8 : 0;
    for (int ii = 0; ii < ViewNum; ii++) {
        memcpy(vp, ViewportTab[ViewNum] + ii, 0x40);
        *(unsigned short *)(vp + 4) += half;
        *(unsigned short *)(vp + 0x14) += half;
        ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 10), 10); GS_NewList();
        GS_SetRegisterS((unsigned long long *)vp, 4);
        GS_FlushList(); ps2_rq_tag_end();
        DF_VP_GetMatrix(ii, wsmat, 0, 1);
        if (!DF_SGNL_IsGaugeOff(ViewCamID[ii])) DF_draw_lock_sight(ViewCamID[ii], wsmat);
        DF_ACSTAT_DrawMapStringEx(ii, wsmat);
    }
    memcpy(vp, ViewportTab[1], 0x40);
    *(unsigned short *)(vp + 4) += half;
    *(unsigned short *)(vp + 0x14) += half;
    ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 10), 10); GS_NewList();
    GS_SetRegisterS((unsigned long long *)vp, 4);
    GS_FlushList(); ps2_rq_tag_end();
}

/* Movie skip on / off: frame-wait sync, voice / sound mute. */
static void DF_LOOP2_MovSkip(int skip)
{
    PS2_FRAME(0x10);
    if (skip) {
        if (!MyI.mov_skip) {
            DF_WAIT_SyncInit();
            MyI.mov_skip = 1;
            MyI.syskeyoff = 1;
            Vags_Stop();
            DF_SND_Mute(1);
            DF_SND_CallOff(1);
        }
    } else if (MyI.mov_skip) {
        DF_WAIT_SyncTerm();
        MyI.mov_skip = 0;
        MyI.syskeyoff = 0;
        DF_SND_Mute(0);
        DF_SND_CallOff(0);
    }
}

/* Skipping a polygon movie with start (single-player sorties): fade out, skip, fade back in when it ends. */
static void __ExecSkipPolyg(void)
{
    PS2_FRAME(0x10);
    int m = sortie_mode();
    if (m != 0 && m != 1) return;
    if (!MyI.mov_play) {
        if (MyI.fade == 1) {
            DF_FADE_Stop(0);
            DF_FADE_SetProc(0, 2, 0x19);
            MyI.fade = 0;
        } else if (MyI.mov_skip) {
            DF_FADE_SetProc(0, 2, 0x19);
            DF_LOOP2_MovSkip(0);
            MyI.fade = 0;
        }
    } else if (!MyI.mov_skip && MyI.fade != 2) {
        if (MyI.fade == 1) {
            if (DF_FADE_IsStop(0)) {
                DF_LOOP2_MovSkip(1);
                MyI.fade = 2;
            }
        } else if (MyI.fade == 0 && (BothKey & 8) && DF_FADE_IsStop(0)) {
            DF_FADE_SetProc(0, 3, 0x19);
            MyI.fade = 1;
            Voice_StopMessage();   /* the original passes (0, 0) */
        }
    }
}

static void __ControlAc(int player, int rmode, int rproceed, StCompactPadData *key)
{
    PS2_FRAME(0x70);
    AcPlayerSt *acp = &g_ac[player];
    StCompactPadData *k = &key[player];   /* key = the AC key array (s_keyCur) */
    if (player >= NowUserPlayerNum && acp->id >= 0 && acp->dead) Pad_NeutralDataCompact(k);
    if (MyI.padoff) Pad_NeutralDataCompact(k);
    if (rmode == 0) {
        if (is_versus_mode(sortie_mode()) && acp->areaovered) Pad_NeutralDataCompact(k);
        AcCtrl_Exec(player, k);
        DF_RPLAY_SetData(player, k);
        if (rproceed) DF_RPLAY_NextData(player);
    } else {
        StCompactPadData pad __attribute__((aligned(16)));
        Pad_NeutralDataCompact(&pad);
        if (rproceed) {
            CopyMemory(&pad, DF_RPLAY_GetData(player), 4);
            AcCtrl_Exec(player, &pad);
            DF_RPLAY_NextData(player);
        } else AcCtrl_Exec(player, &pad);
    }
    if (player < NowUserPlayerNum) {
        DF_SIGHT_SightHitProc(player);
        if (player == MyI.myplayerno[0] && (sortie_mode() == 0 || sortie_mode() == 1)) {
            StModel *mp = &MyAC[player];
            float loc[4] __attribute__((aligned(16)));
            loc[0] = mp->loc[0];
            loc[1] = mp->loc[1] - g_ac[player].tall * 0.5f;
            memcpy(&loc[2], &mp->loc[2], 8);
            DF_AMAP_SearchModel(loc, mp->ang[1], g_ac[player].param.atlas_stat & 1);
        }
    }
}

static int CtrlProc(void)
{
    PS2_FRAME(0x20);
    for (int i = 0; i < NowPlayerNum; i++) g_ac[i].stat &= 0xff8c;
    for (int i = 0; i < NowPlayerNum; i++) __ControlAc(i, MyI.replaymode, MyI.rproceed == 1, s_keyCur);
    return 0;
}

/* COM AI: each COM's DF_ComInKey output is its AC's pad for this frame. */
static int ComProc(void)
{
    PS2_FRAME(0x50);
    stCom *com = (stCom *)AC3_Com_GetComInfo();
    for (int i = 0; i < ComNum; i++, com++) {
        if (com->type == 0) {
            int slot = ComKeyIndex[i];
            unsigned short bits = com->body.com_acp->dead ? 0 : (unsigned short)DF_ComInKey(i);
            if (!MyI.replaymode) s_keyCur[slot].bits = bits;
        } else if (!(com->body.com_enp->use & 0x80)) {
            DF_ComInKey(i);
        }
    }
    return 0;
}

static int CameraProc(void)
{
    PS2_FRAME(0x50);
    if (MyI.livemon) ViewCamID[0] = ViewCamID[1] = ViewCamID[2] = ViewCamID[3] = 4;
    DF_VP_Swap();
    for (int v = 0; v < ViewNum; v++) {
        int id = ViewCamID[v];
        CameraSt *camp;
        if (id < 4) {
            camp = &CAM[id];
            DF_CalculateCamera(id, WORLD_SCREEN_MATRIX, g_matWorldClip);
            MyI.camplayerno = id;
        } else if (id == 4) {
            MyI.camplayerno = 4;
            camp = &CAM[4];
            DF_CALC_NeutralCameraProc(WORLD_SCREEN_MATRIX);
        } else continue;
        DF_VP_SetMatrix(v, WORLD_SCREEN_MATRIX, CAMERA_MATRIX1, CAMERA_MATRIX2);
        DF_VP_SetCamera(v, camp);
        if (DF_LayerConnectProc(camp->groupidbk, camp->groupid, v)) camp->groupidbk = camp->groupid;
    }
    return 0;
}

static int KeySyncProc(void)
{
    PS2_FRAME(0x30);
    MyI.forward = 0;
    MyI.livemon = 0;
    CopyMemory(s_keyPrev, s_keyCur, 0x10);
    for (int i = 0; i < 4; i++) Pad_NeutralDataCompact(&s_keyCur[i]);
    if ((sortie_mode() == 4 || sortie_mode() == 5) && MyI.connected > 1) {
        int r = DF_SyncLink();
        if (r == 1) MyI.forward = 1;
        else if (r < 0) {
            MyI.mainproc = 3;
            MyI.ilinkerr = r == -2 ? 2 : 1;
            ps2_dma_wait_vif1();
            ps2_dma_wait_gif();
            sceGsSyncPath(0, 0);
            return -1;
        }
        MyI.link_index = fslnk_LinkSyncIndex();
        if (!g_sortie.pconstant->isSplit) {
            if (MyI.link_index < NowUserPlayerNum) MyI.myplayerno[0] = (char)MyI.link_index;
            else { MyI.livemon = 1; ViewCamID[0] = 4; }
        } else if (MyI.link_index > 1) {
            MyI.livemon = 1;
            ViewCamID[0] = 4;
        }
        if (sortie_mode() != 5) {
            char *ld = (char *)&ilinkdata;
            for (int i = 0; i < 4; i++) CopyMemory(&s_keyCur[i], ld + i * 4, 4);
        }
    } else {
        Pad_GetCompactData(0, &s_keyCur[0]);
        Pad_GetCompactData(1, &s_keyCur[1]);
        Pad_AssignDigitalToAnalog(0, (short *)&s_keyCur[0]);
        if (g_sortie.pconstant->isSplit) Pad_AssignDigitalToAnalog(1, (short *)&s_keyCur[1]);
        MyI.forward = 1;
    }
    BothKeyB = ~BothKey;
    unsigned short bits = s_keyCur[0].bits;
    if (sortie_mode() != 0 && sortie_mode() != 1)
        bits = s_keyCur[3].bits | s_keyCur[2].bits | s_keyCur[0].bits | s_keyCur[1].bits;
    BothKey = bits;
    return 0;
}

static void init_stat_system(void)
{
    PS2_FRAME(0x40);
    int me = MyI.myplayerno[0];
    char maker = g_ac[me].param.ppartsd->core.basic.maker;
    GSX_SetWindowSize(0x100, 0xe0, 4, 3);
    ViewMode_UseCockpit(me);   /* passed as a 4th argument the callee does not take */
    int type = StatTex_MakeType(g_sortie.pconstant->isSplit, maker, maker == 0);
    int m = sortie_mode();
    DF_ACST2_Open(m == 5 || m == 4 ? g_nextstage.vstime : -1, type, me);
    if (sortie_mode() == 4 || sortie_mode() == 5) DF_ACST2_DispSwitch(0x100, 1);
    if (!g_sortie.pconstant->isSplit || MyI.replaymode) {
        DF_SGNL_Init(0);
        for (int i = 0; i < 4; i++) DF_SGNL_SetCenter(i, 0x100, 0xe0);
    } else {
        DF_SGNL_Init(1);
        if (sortie_mode() == 5) {
            int v = 0;
            for (int i = 0; i < 4; i++)
                if (play_info.f_enable_pad & (1 << i)) DF_SGNL_SetCenter(v++, (i & 1) * 0x100 + 0x80, 0xe0);
            for (int i = 0; i < 4; i++)
                if (!(play_info.f_enable_pad & (1 << i))) DF_SGNL_SetCenter(v++, (i & 1) * 0x100 + 0x80, 0xe0);
        } else {
            DF_SGNL_SetCenter(0, 0x80, 0xe0);
            DF_SGNL_SetCenter(1, 0x180, 0xe0);
            DF_SGNL_SetCenter(2, 0x80, 0xe0);
            DF_SGNL_SetCenter(3, 0x180, 0xe0);
        }
    }
    MsgWnd_Clear();
    Lock_Init();
    WEPICON_SetWepName(MyI.myplayerno[0], -1);
    if (ViewNum == 2) WEPICON_SetWepName(MyI.myplayerno[1], -1);
}

static void __StartLoopSortie(void)
{
    PS2_FRAME(0x20);
    BothKey = 0;
    s_frameSortie = 0;
    int m = sortie_mode();
    if (m == 4 || m == 5 || m == 2 || m == 3 || m == 6) {
        for (int ii = 0; ii < NowPlayerNum; ii++) DF_SGNL_StartReadGo(ii);
        s_first_wait = 0x78;
    } else s_first_wait = 0;
    DF_FADE_SetProc(0, 2, 10);
}
