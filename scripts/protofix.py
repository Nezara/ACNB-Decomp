"""Corrections to DWARF function prototypes (symbols/functions.json), applied by every generator that reads it.

MWCC leaves parameters the function never uses out of the DWARF, but callers still pass them, so the following
parameters arrive one register later than DWARF says. Each fix inserts the missing parameters (name, type) at a
position of the parameter list. Verified against the call sites / the function's own register use.
"""
import json
import os

FIXES = {
    # variadic printf wrappers (lib/pf.c): the format's arguments follow pfmt (MW EABI: a2..t3 / a3..t3 spilled into
    # 8-byte slots, then the caller's stack); a nameless "..." parameter makes the prototype variadic
    "Pf_SPrintf": [(2, "", ["fund", "..."])],
    "Pf_SnPrintf": [(3, "", ["fund", "..."])],
    # stat/altimetr.c: both callers pass STA2_RGBA in a1 (unused, dropped from DWARF); ofs_y arrives in a2
    # (asm 0x240370 move s0, a2)
    "Altimeter_TypeA": [(1, "col", ["ptr", ["fund", "int"]])],
    # polyg/fsPs2PolyG.c statics: Calc / Analyze pass pPolyG in a0 (never read); the real arguments arrive in a1..a3
    # (asm 0x206fd8 move s0, a1 / 0x207278 move s1, a1 / 0x207520 move s0, a1; calls at 0x208820 / 0x208ae8 / 0x208ffc)
    "fsPs2PolyG_Calc_CamMatTgt": [(0, "pPolyG", ["ptr", ["struct", "_FS_POLYG_ST"]])],
    "fsPs2PolyG_Calc_CamMatFree": [(0, "pPolyG", ["ptr", ["struct", "_FS_POLYG_ST"]])],
    "fsPs2PolyG_Calc_VecRotMat": [(0, "pPolyG", ["ptr", ["struct", "_FS_POLYG_ST"]])],
    # marena_point_mng.c: (mode, pFlagSearch); DWARF drops the unused mode. __MArenaResult_PlayPhaseLoad passes
    # (2, &flagSearch) in a0 / a1 (asm 0x1e04f7c / 0x1e052e0)
    "MArenaPointMng_SetArenaPointOne": [(0, "mode", ["fund", "char"])],
    # clip.c static: DF_MkDL_ClipModel passes the clip plane in a1 (never read); vtx1 / vtx2 arrive in a2 / a3 and the
    # store-to-cross_data flag SW in t0 (asm 0x19c4f0 move s3, a2 / move s2, a3 / move s1, t0)
    "__nakas_mkdl_cross_point_Mk3": [(1, "pln", ["ptr", ["fund", "float"]])],
    # (xy, size): DF_SHAKE_Main passes the rect pointer in a0 (sp+0xa0) and size in a1 (unused); DWARF says (void)
    # AutoGenColor.c static: the public wrapper is a `j` tail call, so colorId (a1, never read: a1 is reset to 0 for the
    # memset) is still there and the pilot id arrives in a2 (asm 0x1df6708 move s0, a2); DWARF says (out, pilotId)
    "__AutoGenColor_CreateAsmColor": [(1, "colorId", ["fund", "int"])],
    # PilotSearch.c static: PilotSearch passes its asmId in a1 (never read); pPilotData / pilotNum arrive in a2 / a3
    # (asm 0x1dfa170 move s1, a2 / 0x1dfa150 move s4, a3); DWARF says (pPilotBuf, pPilotData, pilotNum)
    "__PilotSearch_SearchPilot": [(1, "asmId", ["fund", "int"])],
    "DF_SHAKE_Forward": [(0, "xy", ["ptr", ["fund", "int"]]), (1, "size", ["fund", "int"])],
    # variadic (loc, argNum, ...): the ids arrive in a2..t3 (MW EABI spills them); its only caller passes two
    "STAGE_IsInReg": [(2, "id0", ["fund", "int"]), (3, "id1", ["fund", "int"]), (4, "id2", ["fund", "int"]),
                      (5, "id3", ["fund", "int"]), (6, "id4", ["fund", "int"]), (7, "id5", ["fund", "int"])],
    # (SurNum, num, packP): the se.c procs pass (4, 0x3f /* voices */, s_strsrpk) in a0 / a1 / a2
    "fssdsynthPackSeStreamSurroundOn": [(1, "num", ["fund", "int"])],
    # (mp, vu1mp, omdtab): reads the table from a2; callers pass mp->vu1mp / mp->omdtable (DF_SetModelAttribute*)
    "DF_MDL_SetDrawFlag": [(1, "vu1mp", ["ptr", ["fund", "unsigned int"]])],
    # array capacities the function never checks: HitBoxGetInOutSphereModel passes them in t1 and on the stack
    "__GetInOutRegionBox": [(5, "numInArrMax", ["fund", "int"]), (8, "numOutArrMax", ["fund", "int"])],
    # (pChrMdl, CyP, CyH, CyR, IntVec) like HitBoxHitProcWater / Yuka: reads CyP from a1 and IntVec from a2
    "HitBoxHitProcKabe": [(0, "pChrMdl", ["ptr", ["struct", "StModel"]])],
    # sight colour index (sight_col row) in t2: draw_wep_lock passes it after the lock speed (DWARF's col_no, t1)
    "DF_SIGHT_DrawSight": [(6, "col_idx", ["fund", "int"])],
    # (frp, num, playerno, form): DF_DrawFriendStat passes 4 in a1 (unused), the player to skip in a2, form in a3
    "DF_GetFriendStat_VS": [(1, "num", ["fund", "int"])],
    "DF_GetFriendStat_MISSION": [(1, "num", ["fund", "int"])],
    # DF_DrawModel_TP2 callback (mp, id, argp): uses id (a1) and the cockpit manager in argp (a2), not mp
    # (ep, mp, eip): the think functions pass their mp in a1, the function reads eip from a2 (asm: move s0, a2)
    "ene_execute_operation": [(1, "mp", ["ptr", ["struct", "StModel"]])],
    # eneop.c op scores (EnemyOpSt.calcfunc `float (*)(EnemySt *, EneInfoSt *)`): DWARF drops the unused ep, the
    # functions read eip from a1 (asm 0x1e43210 enevl_stay `lw v0, 0x20(a1)`; enevl_stay2 / back / charge /
    # turn_around the same, layer 1 / 2)
    "enevl_stay": [(0, "ep", ["ptr", ["struct", "EnemySt"]])],
    "enevl_stay2": [(0, "ep", ["ptr", ["struct", "EnemySt"]])],
    "enevl_back": [(0, "ep", ["ptr", ["struct", "EnemySt"]])],
    "common_jcon_on2": [(0, "ep", ["ptr", ["struct", "EnemySt"]])],
    "enevl_charge": [(0, "ep", ["ptr", ["struct", "EnemySt"]])],
    "enevl_turn_around": [(0, "ep", ["ptr", ["struct", "EnemySt"]])],
    # sandbox gE: same a1 = eip pattern (asm 0x1e42168 / 0x1e43158 / 0x1e437ec / 0x1e43e80 `lw v0, 0x20(a1)`)
    "enevl_attack": [(0, "ep", ["ptr", ["struct", "EnemySt"]])],
    "enevl_approach2": [(0, "ep", ["ptr", ["struct", "EnemySt"]])],
    "enevl_fly_rear": [(0, "ep", ["ptr", ["struct", "EnemySt"]])],
    "enevl_round_rear": [(0, "ep", ["ptr", ["struct", "EnemySt"]])],
    # (ep, edat, eip): e120_select_weapon passes its edat in a1 (unused), the callee reads eip from a2 (lwc1 4(a2))
    "default_select_weapon": [(1, "edat", ["ptr", ["struct", "EnemyDataSt"]])],
    # (ep, mp, boost, subboost): Ene120_Initial passes mp in a1, edat->sfx[0] / [1] in a2 / a3 (asm 0x1e38768..74)
    "ene_boost_sfx_init": [(1, "mp", ["ptr", ["struct", "StModel"]])],
    # enemy/ene004.c: variadic (mp, msg, ...); the asm spills a2..t3 and reads the first vararg as a 64-bit double
    # (asm 0x1e1f000 sd a2, 0x1e1f068 ld a0) - the __gimk_ReceiveMsg pattern
    "Ene004_MsgFunc": [(2, "arg0", ["fund", "unsigned long long"])],
    # eneanim.c: (ep, mp) with ep unused - the bodies read mp from a1 (0x1e3a970 / 0x1e3a990 lw 0xa0(a1)); every
    # caller passes ep in a0 (ene004 0x1e1f0d0 / 0x1e1f184)
    "DFene_IsAnimEnd": [(0, "ep", ["ptr", ["struct", "EnemySt"]])],
    "DFene_ResetAnimLoop": [(0, "ep", ["ptr", ["struct", "EnemySt"]])],
    # (ep, mp, edat): e094_bst_mng passes mp in a1 (unused), the body reads edat from a2 (asm 0x1e347ac lbu 0x136(a2))
    "e094_on_blade": [(1, "mp", ["ptr", ["struct", "StModel"]])],
    "set_model_attribute": [(0, "mp", ["ptr", ["struct", "StModel"]])],
    # magic2 appear functions: the wrappers pass their (unused) n_speed in t3, so option / onp arrive on the stack
    "DF_MAGIC2_AppearMagicPtr": [(7, "n_speed", ["ptr", ["array", [4], ["fund", "float"]]])],
    "DF_MAGIC2_AppearMagic_SPL2Ptr": [(7, "n_speed", ["ptr", ["array", [4], ["fund", "float"]]])],
    "DF_MAGIC2_AppearMagic_SPL2AlphaPtr": [(7, "n_speed", ["ptr", ["array", [4], ["fund", "float"]]])],
    # (own, tgt, result, ap, start_hit_own, start_hit_water): the HitProg callbacks pass their own/tgt in a0/a1
    "__spell_Hit": [(0, "own", ["ptr", ["fund", "void"]]), (1, "tgt", ["ptr", ["fund", "void"]])],
    # installer dialog callback int (*)(void *pArg, int param): the asm reads param from a1 (a0 = pArg, unused)
    "__OpenInstallerDialog": [(0, "pArg", ["ptr", ["fund", "void"]])],
    # msys list screens: __PlayMcSystemNext passes 0 in t1 (never read), mcInfop in t2, argp in t3, argp2 on the stack
    "__NewMcEmbSystem": [(5, "unused", ["fund", "int"])],
    "__NewMcGameSystem": [(5, "unused", ["fund", "int"])],
    # gimmick message handler (mp->msgfunc) is variadic: msg 1 / 2 read the first two varargs (a2, a3)
    # (gzcp, prvp, acp): AcGaze_Control passes its gzcp in a0 (unused), prvp / acp arrive in a1 / a2
    "__LockReinforce": [(0, "gzcp", ["ptr", ["struct", "AcPartsGazeSt"]])],
    # enemy/shoot.c: ene_shoot passes 14 integer args; the 12th (sp+0x18 = (wep->func >> 5) & 7) is never read,
    # so dstat / fire arrive at sp+0x20 / sp+0x28
    "ene_shoot_ex": [(11, "wep_ext", ["fund", "int"])],
    "__gimk_ReceiveMsg":[(2, "arg0", ["ptr", ["fund", "void"]]), (3, "arg1", ["ptr", ["fund", "void"]])],
    # masmd: SetDetailArm passes sub_type / 10 in a1 (never read); the arm record arrives in a2
    "__MASMD_SetDetailWepArmB": [(1, "inFlagEne", ["fund", "int"])],
    # (pGimk, hloc, pDstat): every caller passes the hit position in a1, the function reads pDstat from a2
    "__gimk_ExecDestruct": [(1, "hloc", ["ptr", ["fund", "float"]])],
    # AC model message handler (mp->msgfunc) is variadic: msg 1 reads the first vararg (a2, the incoming spell)
    "AC_msgfunc": [(2, "arg0", ["ptr", ["fund", "void"]])],
    # (player, id): reads id from a1; __gimk_InitHiddenParts passes player 0 in a0
    "Game_IsAchievement": [(0, "player", ["fund", "int"])],
    # (acp, wctrlp, ...): MWCC dropped the unused acp; the asm reads wctrlp from a1 and every caller passes acp in a0
    "AcWep_AppearShell": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]])],
    "AcWepGun_Init": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]])],
    "AcWepMissile_Init": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]])],
    "onfree_gun": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]])],
    "ontrigger_cannon": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]])],
    "onfree_missile": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]])],
    "acblade_WbladeInit": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]])],
    "onequip_wblade": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]])],
    # DF_COLL hit callback (acp, tgt, result): MWCC dropped the unused 2nd parameter, result arrives in a2
    "HitBladeCallback": [(1, "tgt", ["ptr", ["fund", "void"]])],
    "blade_hit_callback": [(1, "tgt", ["ptr", ["fund", "void"]])],
    "onequip_rb": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]])],
    # COM extension AIs are called through com_extinit_tbl / extmng.actfunc (extmngp, extctrlp); MWCC dropped extmngp
    "com_extinit_cooler": [(0, "extmngp", ["ptr", ["struct", "stComExtMng"]])],
    "com_extinit_stealth": [(0, "extmngp", ["ptr", ["struct", "stComExtMng"]])],
    "com_extinit_sideshield": [(0, "extmngp", ["ptr", ["struct", "stComExtMng"]])],
    "com_extinit_hoverboost": [(0, "extmngp", ["ptr", ["struct", "stComExtMng"]])],
    "com_extinit_multiboost": [(0, "extmngp", ["ptr", ["struct", "stComExtMng"]])],
    "com_extinit_turnboost": [(0, "extmngp", ["ptr", ["struct", "stComExtMng"]])],
    "comext_vsKAKUweapon": [(0, "extmngp", ["ptr", ["struct", "stComExtMng"]])],
    # COM weapon callbacks: val(wm, wp, &wm->wk), main(wm, datap, wp, &wm->wk), setup(wm, wparamp, &wm->wk)
    "fmine_calcvalue": [(0, "wmngp", ["ptr", ["struct", "stComWepMng"]])],
    "bom_calcvalue": [(0, "wmngp", ["ptr", ["struct", "stComWepMng"]])],
    "noisemaker_calcvalue": [(0, "wmngp", ["ptr", ["struct", "stComWepMng"]])],
    "pilebanker_calcvalue": [(0, "wmngp", ["ptr", ["struct", "stComWepMng"]])],
    "std_walkcannon_calcvalue": [(0, "wmngp", ["ptr", ["struct", "stComWepMng"]])],
    "std_shitcannon_calcvalue": [(0, "wmngp", ["ptr", ["struct", "stComWepMng"]])],
    "stdmissile_calcvalue": [(0, "wmngp", ["ptr", ["struct", "stComWepMng"]])],
    "stdgun_calcvalue": [(0, "wmngp", ["ptr", ["struct", "stComWepMng"]])],
    "decoy_onactive": [(2, "wp", ["ptr", ["struct", "_weapon_st"]])],
    "stdmissile_onactive": [(1, "paramp", ["ptr", ["struct", "stComWepData"]])],
    "ComWepmng_std_Missile": [(1, "wparamp", ["ptr", ["struct", "stComWepData"]])],
    # (comp, swordp, comdatp): ComInit passes com_acp->param.swordp in a1, which COMLAU_Init never reads
    "COMLAU_Init": [(1, "swordp", ["ptr", ["fund", "void"]])],
    # COM operation callbacks (stComOpMng tables): main(op, wk), val(op, wk), init / sel_init(op, datap, wk), mvchk(op, v)
    "xz_movechk": [(0, "opmngp", ["ptr", ["struct", "stComOpMng"]])],
    "op_avoid_obstacle": [(0, "opmngp", ["ptr", ["struct", "stComOpMng"]])],
    "end_avoid_missile": [(0, "opmngp", ["ptr", ["struct", "stComOpMng"]])],
    "init_follow_marker": [(0, "opmngp", ["ptr", ["struct", "stComOpMng"]]), (1, "datap", ["ptr", ["struct", "stOpData"]])],
    "si_follow_marker": [(1, "datap", ["ptr", ["struct", "stOpData"]])],
    "init_avoid_obstacle": [(1, "datap", ["ptr", ["struct", "stOpData"]])],
    "init_stdvsene": [(0, "opmngp", ["ptr", ["struct", "stComOpMng"]])],
    "mv_stdvse_stitch": [(0, "opmngp", ["ptr", ["struct", "stComOpMng"]])],
    "mv_stdvse_curve": [(0, "opmngp", ["ptr", ["struct", "stComOpMng"]])],
    "__GetEscapeViewPos": [(2, "param", ["ptr", ["struct", "stComOpParamEscapeView"]])],
    # cam.c statics: MWCC dropped the unused parameters; the callers pass (cam, eye, at) / (vec, at, cam) in a0..a2
    "__cam_SetCollPos": [(0, "cam", ["ptr", ["struct", "CameraSt"]])],
    "__cam_SetGazeModel": [(0, "vec", ["ptr", ["fund", "float"]])],
    "__cam_SetPosModel": [(1, "at", ["ptr", ["fund", "float"]])],
    # comtree value functions all have the table type (pTreeBuff, pNode, data); MWCC dropped the unused parameters
    "__ComTree_ValueCommand": [(0, "pTreeBuff", ["ptr", ["fund", "char"]]), (2, "data", ["fund", "char"])],
    "__ComTree_ValueWeapon": [(0, "pTreeBuff", ["ptr", ["fund", "char"]]), (2, "data", ["fund", "char"])],
    "__ComTree_ValueOperation": [(0, "pTreeBuff", ["ptr", ["fund", "char"]]), (2, "data", ["fund", "char"])],
    "__ComTree_ValueIf": [(0, "pTreeBuff", ["ptr", ["fund", "char"]])],
    "__ComTree_ValueRoot": [(0, "pTreeBuff", ["ptr", ["fund", "char"]]), (1, "pNode", ["ptr", ["struct", "tagNode"]]),
                            (2, "data", ["fund", "char"])],
    # parts_ext: every open / busy / close / act slot is called (prvp->acp, extp) and every extension_initfunc entry
    # (acp, extp, partsp); MWCC dropped the unused leading / trailing parameters (asm: extp in a1, partsp in a2)
    "__CloseExtAddMagazine": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]])],
    "__OpenExtAddMagazine": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]])],
    "__WaitingExtAddMagazine": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]])],
    "__CloseExtCurestat": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]])],
    "__WaitingExtCurestat": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]])],
    "EXTenepack_open": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]])],
    "EXTenepack_standby": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]])],
    "EXTcoolant_standby": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]])],
    "EXTstealth_standby": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]])],
    "__WaitingExtBooster": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]])],
    "EXTinit_enepack": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]])],
    "EXTinit_vsweapon": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]])],
    "EXTenepack_close": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]]), (1, "extp", ["ptr", ["struct", "AcPartsExtSt"]])],
    "EXTcoolant_close": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]]), (1, "extp", ["ptr", ["struct", "AcPartsExtSt"]])],
    "EXTneshield_close": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]]), (1, "extp", ["ptr", ["struct", "AcPartsExtSt"]])],
    "EXTneshield_busy": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]]), (1, "extp", ["ptr", ["struct", "AcPartsExtSt"]])],
    "EXTneshield_open": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]]), (1, "extp", ["ptr", ["struct", "AcPartsExtSt"]])],
    "EXTboost_close": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]]), (1, "extp", ["ptr", ["struct", "AcPartsExtSt"]])],
    "EXTshield_standby": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]]), (1, "extp", ["ptr", ["struct", "AcPartsExtSt"]])],
    "EXTinit_neshield": [(0, "acp", ["ptr", ["struct", "AcPlayerSt"]]), (2, "partsp", ["ptr", ["struct", "PartsParamExtensionSt"]])],
    "EXTinit_shield": [(2, "partsp", ["ptr", ["struct", "PartsParamExtensionSt"]])],
    "EXTinit_linkweapon": [(2, "partsp", ["ptr", ["struct", "PartsParamExtensionSt"]])],
    "__InitExtBooster": [(2, "partsp", ["ptr", ["struct", "PartsParamExtensionSt"]])],
    # sinfo.c exception helpers: called with the Info_IncData registers (info_id, index, param); MWCC dropped index (a1)
    "__IncMissionExceptionData": [(1, "index", ["fund", "int"])],
    "__IncPlayerExceptionData": [(1, "index", ["fund", "int"])],
    # (frame, rmode, proceed): LoopSortie_Do passes FRAME in a0 (unused), MyI.replaymode in a1, MyI.replayon in a2
    "DF_RPLAY_Count": [(0, "frame", ["fund", "int"])],
    # (key, trg): PauseProc passes BothKey in a0 (unused) and BothKey & BothKeyB in a1; the procs test a1 (asm: andi a1, 0x80)
    "DF_PAUSE_Proc": [(0, "key", ["fund", "unsigned int"])],
    "DF_PAUSE_Restart_Proc": [(0, "key", ["fund", "unsigned int"])],
    # mstr.c (ctgr, id): DF_MSTR_GetName / GetInfo pass their inCtgrID in a0 (unused), the search key in a1
    "__GetParam": [(0, "inCtgrID", ["fund", "int"])],
    # mpart.c (core, arm, leg): __OneAC_ReadAll_ReadConnectMI2 / DF_M_AC_SetTmpPart pass the GetConnectMI2ID argument
    # list; GetConnectMI2Kind reads only the leg id (a2)
    "DF_MPART_GetConnectMI2Kind": [(0, "inCoreID", ["fund", "int"]), (1, "inArmID", ["fund", "int"])],
    # mdraw.c: __MOBJ_DrawWindow passes the window's shape in a0 / its rect in a0; both callees start at a1
    "DF_MDRAW_NexusDrawTagVertex4": [(0, "inShape", ["fund", "int"])],
    "DF_MDRAW_DrawM1Help": [(0, "pRect", ["ptr", ["struct", "stMenuRect"]])],
    # mdraw.c (rect, color, depth, labeltype): DF_MDRAW_DrawReport1 never reads its depth (a2); labeltype is in a3
    "DF_MDRAW_DrawReport1": [(2, "depth", ["fund", "int"])],
    # m_ac.c (pTune, acp, nPlayer, unused, bReal): every caller sets a3 = 0 and passes bReal in t0 (asm: beqz t0)
    "M_AC_GetTuneStatFromPlayerInfo": [(3, "unused", ["fund", "int"])],
    # m_ac.c (acp, idx, unused, rate[, cnt]): the carousel callers load a float into f12 that the functions never read;
    # the rate is in f13 (asm: c.ole.s f13, f0), cnt in a2
    "DF_M_AC_SetCirclePos": [(2, "unused", ["fund", "float"])],
    "DF_M_AC_NXSetCirclePos": [(2, "unused", ["fund", "float"])],
    # loader.c (kind, id): DF_M_AC_GetPlayerInfo passes the loader kind in a0 (unused), the function tests a1
    "loader_IsValidParts": [(0, "iKind", ["fund", "int"])],
    # loader.c: a 7th argument in t2 (asm 0x1b0ebc beqz t2: the HDD source is allowed); DF_MMD_ReadStart passes it
    "loader_GetMmdLocation": [(6, "iAllowHdd", ["fund", "int"])],
    # 16 ints: a1 (callers pass 1 / 2) and the stack args at sp+0x20 / sp+0x28 (a rodata pointer) are unused
    "DF_MOBJ2_NexusNewMdlWin": [(1, "inUnused1", ["fund", "int"]), (12, "inUnused2", ["fund", "int"]),
                                (13, "inUnused3", ["fund", "int"])],
    # masm.c (inObjp, inACp, unused, inMode): the callers pass 0 in a2 (overwritten at entry); inMode arrives in a3
    # (asm 0x1e0e8dc move s0, a3 -> sb obj+0x7e)
    "__MASM_NewACSpecWin": [(2, "unused", ["fund", "int"])],
    # masmp.c (ioObjp, inType, inDatap, inWepp, unused, inFlagDefault, pTune): the body never reads t0 (asm: a3 -> s1,
    # t1 -> fp, t2 -> s7); __AsmSys_PlayChanging / __AsmSys_ChangeGeneWin2 pass the weapon record there
    "DF_MASMP_ChangeGeneWin": [(4, "unused", ["ptr", ["fund", "void"]])],
    # mvss.c: every caller passes (obj, 5, 1); the 5 in a1 is never read, control arrives in a2 (asm 0x1ecbfe4 /
    # 0x1ecc160 beqz a2); DF_MVSS_NewSystem: mvse passes &play_info in a1 (unused) and the mode in a2 (lb a2, 0x9f(s3))
    "__MVSS_ItemFadeOut": [(1, "kind", ["fund", "int"])],
    "__MVSS_ItemFadeIn": [(1, "kind", ["fund", "int"])],
    "DF_MVSS_NewSystem": [(1, "inPlayinfo", ["ptr", ["struct", "struct_c774638e"]])],
    # mvse.c: every caller passes (obj, &dt.tagimp[0] (unused), num_connect, idx) in a0..a3
    "__MVSE_SetTagPos": [(1, "unused", ["ptr", ["ptr", ["struct", "stMobj"]]])],
    # mperf.c: PadCheckWait2 passes (parent, 0, player, M_ACp[player][0], 0); a1 / t0 never read (asm 0x1e88510 a2, a3)
    "DF_MPERF_NewSystem": [(1, "unused", ["fund", "int"])],
    # mkeyguide.c static: MenuKeyGuideFunc passes (page, Per); the function reads Per from a1 (asm 0x1e628cc move s1, a1)
    "MenuKeyGuideDrawTitle": [(0, "page_unused", ["ptr", ["struct", "_MENUKEYGUIDE_PAGE_ST"]])],
    # hdd.c statics in s_pfuncHddTask (int (*)(StHddDescriptor *, void *)): hdd_ReadFileAsync / hdd_Sync pass phdd in a0,
    # the task reads pParam from a1 (asm 0x1af5b0 / 0x1af8d0 move s1, a1)
    "TaskReadFile": [(0, "phdd", ["ptr", ["struct", "StHddDescriptor"]])],
    "TaskWriteFile": [(0, "phdd", ["ptr", ["struct", "StHddDescriptor"]])],
    # lib/fs/fslibsd.c: fssdCallRpc(module, wait, cmd, w0..w7) - DWARF names 3, the body copies 8 payload words into the
    # RPC buffer: w0..w4 from a3 / t0..t3, w5..w7 from the caller's stack args sp+0 / +8 / +0x10 (asm 0x146bf0..0x146d10)
    "fssdCallRpc": [(3 + i, "w%d" % i, ["fund", "int"]) for i in range(8)],
    # lib/fslibmfstrm/fslibmfstrm.c: fsmfstrm_CallRpc(cmd, block, w0..w7) - DWARF names 2, the body copies 8 payload
    # words into the RPC buffer: w0..w5 from a2 / a3 / t0..t3 (home area sp+0x70..0x98), w6 / w7 = the caller's sp+0 / +8
    # (asm 0x105000..0x1050e0)
    "fsmfstrm_CallRpc": [(2 + i, "w%d" % i, ["fund", "int"]) for i in range(8)],
    # IOP -> EE handlers in s_psrcFunc: IopToEeRpcDispatch calls them with (cmd, pData, iSize, &iRetSize); the unused iSize
    # is dropped from DWARF, piRetSize arrives in a3 (asm 0x10f6c0 sw a3, 0x40(sp) / 0x14792c move s0, a3)
    "_IopToEeRpcDispatch": [(2, "iSize", ["fund", "int"])],
    "RpcCallBack": [(2, "iSize", ["fund", "int"])],
    # src/link/fslibcom.c: fscom_Recv(cd, data, size, mode) - the body reads mode from t0 (asm 0x1ca250 move s0, t0) and
    # never a3; every liblnk caller passes a3 = 0 and t0 = 0 (asm 0x1d4674 / 0x1d4678): the unused a3 is dropped from DWARF
    "fscom_Recv": [(3, "unused", ["fund", "int"])],
}


def load_functions(sym_dir):
    fs = json.load(open(os.path.join(sym_dir, "functions.json"), encoding="utf-8"))
    for f in fs:
        for pos, name, typ in FIXES.get(f["name"], ()):
            f["params"].insert(pos, {"name": name, "type": typ, "loc": {}})
    return fs
