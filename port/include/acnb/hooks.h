/* Port hooks: the only places where decompiled code (port/src) reaches our additions (options, debug probes).
 * Each hook defaults to the original game's behaviour; the hybrid runtime (or later the source port's option code)
 * changes them at startup. Definitions: port/hooks/hooks.cpp, compiled into both builds. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/* LoopSortie_Do, once per battle logic frame right after ComProc (debug probes: ACNB_VS_LOG, time-over test).
 * NULL = original. */
extern void (*hook_sortie_logic_frame)(void);

/* DF_MPART_SetAsmDefault, after the default garage / repository is filled (game.unlock_parts: every part unlocked).
 * NULL = original. */
extern void (*hook_mpart_asm_default)(void);

/* Save_DistributeSaveData (main/save.cpp), after the save image is copied in (game.unlock_parts: every part unlocked
 * again after a memory-card load). NULL = original. */
extern void (*hook_distribute_save)(void);

/* DF_MOBJ_NewStringFromLayoutName, on entry: returns the string id to use (game.versus_garage: the garage top menu's
 * Paint / Name labels get the normal garage's ids). NULL = original. */
extern int (*hook_mobj_string_layout_id)(int id);

/* DF_MOBJ_SetStringColorAll, on entry: nonzero skips the call (game.versus_garage: Paint / Name are not greyed out).
 * NULL = original. */
extern int (*hook_mobj_skip_string_color_all)(void);

/* UnitSprite_SetMultiply, on entry: nonzero skips the call (game.versus_garage: same predicate as above).
 * NULL = original. */
extern int (*hook_unitsprite_skip_multiply)(void);

/* DF_InitMemCard, after the original body (game.versus_garage: gLoadCheckF[] = 1 for every player so saving may
 * create a new file). NULL = original. */
extern void (*hook_init_mem_card)(void);

/* __PlayGarageSys_PadCheckWait (menu/mgarage.cpp), at the Paint / Name (items 3 / 5) versus-mode tests: returns the
 * garage mode to test (game.versus_garage: 0 = the normal garage, so both items may be entered). NULL = original. */
extern int (*hook_garage_item_mode)(int item, int mode);

/* __MenuGarageTop_CreateItem (menu/mgarage.cpp): called with 1 on entry and 0 before it returns (game.versus_garage:
 * while raised, hook_mobj_string_layout_id / the two grey-out skips give Paint / Name the normal garage's labels).
 * NULL = original. */
extern void (*hook_garage_top_create_item)(int enter);

/* __NewSubSysDef (menu/mgarage.cpp), last thing before it returns the new SubSys object: gets its helpStrId[8]
 * (game.versus_garage: Paint / Name ids 0xdcb6b / 0xdcb6d -> the normal garage's 0xdcb43 / 0xdcb45; the first help
 * line is already built). NULL = original. */
extern void (*hook_garage_new_sub_sys_def)(unsigned int *helpStrId);

/* DF_MMANAGE_NewSystem (menu/mmanage.cpp), on entry: a non-NULL result is returned as the menu manager object instead
 * of building the scene (versus.quick: the first call returns a manager holding a finished Versus screen, so the menu
 * leaves straight for the battle). NULL hook or NULL result = original. */
struct stMobj;
extern struct stMobj *(*hook_mmanage_new_system)(void);

/* __DoStepDbgMapSelect (main/main.cpp), after the body, before it returns 5 (versus.quick: the first Versus sortie's
 * MyI.random_seed = versus.seed). NULL = original. */
extern void (*hook_step_map_select)(void);

/* Draw_FlipBuffer* (gfx/draw.c; DF_WAIT_Sync once native), every store of the current field g_odevCur: returns the
 * value to store. The hybrid sets it unless ACNB_INTERLACE_OFFSET=1: 0 = always the even field, the same patch
 * runtime/hle.c applies to the original's stores (no half-line shimmer of the field-mode offsets on a progressive
 * display). NULL = original. */
extern int (*hook_field_store)(int field);

/* Step 5.2 view hooks (runtime/widescreen.c). Aspect (Hor+) and camera.fov are render-side only since s79 / s80 and
 * need no game hook (user rule s79). hook_view_screen runs before the projection matrices are built - cam 0
 * CAM_UpdateViewScreenMatrix (battle camera), 1 DF_CALC_ncamera (replay / neutral camera), 2 DF_SetWorldScreenMatrix
 * (TP2 cockpit panel), 3 mview __UpdateCamMatrix (menu 3D views) - and may change the x pixel-aspect factor ax (0.8;
 * 1.0 for cam 3) and the focal length scrz used for them (the camera's own scrz is not written). Unused since s80
 * (it would be GAME STATE: the projection matrices live in game RAM). NULL = original. */
extern void (*hook_view_screen)(int cam, float *ax, float *scrz);

/* mkdl set_frustum (__MakeGeneralDl / __MakeBgDl view-frustum culling, __Vfc): fx = the frustum's x constant (320 =
 * 256 / 0.8), scrz = CUR_DRAW_CAM->scrz. video.culling = match_view (s80, opt-in) widens it in battle to the aspect
 * (320 / ws) and a wider camera.fov (scrz / tan ratio) - GAME STATE (culling flags, display lists), recorded in the
 * replay settings header (runtime/simcfg.c). NULL = original. */
extern void (*hook_cull_frustum)(float *fx, float *scrz);

/* i.LINK back end (src/link/ilink.cpp, source port only): the fsilk_* entries liblnk's linkMode 0 arm and the direct
 * callers (mmanage, loader) reach. Set = a host link back end (port/platform/net, `--link host|join`) answers every call
 * with the fsilk contract; NULL = the entries are unported (report + exit 70,
 * the sp_missing stub behaviour). The hybrid never reads it (fsilk stays interpreted there). */
struct AcnbIlinkOps {
    int (*IlinkInit)(void);
    int (*IlinkTerm)(void);
    int (*IlinkDestroy)(void);
    int (*IlinkReset)(void);
    int (*IlinkStart)(void);
    int (*IlinkStop)(void);
    int (*IlinkSetCheckAlive)(int mode);
    int (*IlinkProc)(void);
    int (*IlinkCheckConnect)(int block, int connect_no);
    int (*GetConnectedNum)(void);
    int (*IlinkSyncIndex)(void);
    int (*IlinkSyncInit)(char player, short size, char fps, char dup);
    int (*IlinkSyncStart)(void);
    int (*IlinkSyncStop)(void);
    int (*IlinkSyncTerm)(void);
    int (*IlinkSyncProc)(void);
    int (*IlinkSyncSetData)(void *sync_data);
    int (*IlinkSyncGetData)(void *sync_data);
    int (*IlinkSendData)(void *send_data, int size);
    int (*IlinkSendDataSize)(void);
    int (*IlinkRecvDataSize)(int bno);
    int (*IlinkRecvData)(void *copy_buf, int bno, int size);
    int (*socketSendDataL)(char *buf, int size);
    int (*socketRecvDataL)(char *buf, int size);
    unsigned int (*GetStatSendDataL)(void);
    unsigned int (*GetStatRecvDataL)(void);
};
extern const struct AcnbIlinkOps *hook_ilink;

/* LoopSortie_Do, right after FRAME++ (end of an un-paused battle frame): frame = the new FRAME, ac = &g_ac[0] (4 x
 * 0x2600 bytes), rand_next = newlib rand's u64 state. Debug probe only (ACNB_FPU_LOG, hooks/fpu_probe.cpp: the
 * PCSX2 PINE logger's CSV, scripts/pine_log.py). NULL = original. */
extern void (*hook_sortie_frame_end)(unsigned int frame, const void *ac, const void *rand_next);

#ifdef __cplusplus
}
#endif
