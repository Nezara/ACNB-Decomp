/* Game data the DWARF does not name (function-local static data, anonymous constants) that decompiled code uses.
 * Names are ours; the hybrid build places them at their PS2 addresses (runtime/nodwarf_abs.s), the source port has
 * to provide them like any other game data. Keep this list and nodwarf_abs.s in sync. */
#pragma once
#include "acnb/types.h"
#ifdef __cplusplus
extern "C" {
#endif

/* gfx/mkdl.c: static u_long of mkdl_xlu (GS register value of the translucent pass) */
extern unsigned long long mkdl_xlu_reg;                         /* 0x0027ad68 */
/* gfx/mkdl.c: 4 quadwords DMA-referenced into both VU1 buffers by the effect pass */
extern unsigned int mkdl_effect_vu1_const[16];                   /* 0x00277de0 */
/* sfx/magic.c: constant vector of the camera-roll code in DF_MAGIC2 (x, z used) */
extern const float magic2_roll_axis[4];                          /* 0x00260130 */
/* ac/acctrl.c __ExecAc: floor / water damage direction {0, -1, 0, 1} and the flare-burn damage status */
extern float execac_down_vec[4];                                 /* 0x0025aac0 */
extern SfxDstat execac_flare_dstat;                              /* 0x0025aae0 */
/* com/comlau.c (OLSORTIE): COM extension-part programs by PartsParamExtensionSt.prog_no */
extern void (*const com_extinit_tbl[0x12])(stComExtMng *, AcPartsExtSt *);   /* 0x01e6aa90 */
/* event/cond.c: interpolation curves by __cond_element_st.intpType (Linear, Sin, Pulse, EaseIn, EaseOut) */
extern float (*const cond_intp_func[5])(__cond_element_st *);   /* 0x0025add0 */
/* lib/pf.c: __vfprintf's __ftoa output buffer (0x40 bytes, bss) */
extern char pf_ftoa_buf[0x40];                                   /* 0x002ecc30 */
/* stage/sight.c: view-frustum-cull slope factors (DWARF names s_aax / s_aay / s_bbx / s_bby clash with gfx/mkdl.c's) */
extern float sight_aax;                                          /* 0x0027cc80 */
extern float sight_aay;                                          /* 0x0027cc78 */
extern float sight_bbx;                                          /* 0x0027cc7c */
extern float sight_bby;                                          /* 0x0027cc74 */
/* ac/parts_opc.c: AcOpc_Control's four function-local `static float fRand` (same DWARF name, consecutive sdata words;
 * the generated local header declares all four under one asm label) */
extern float opc_frand[4];                                        /* 0x0027b2b4 */

/* menu/mstr.c: "" (main ELF sdata, gp-0x6ed0) that DF_MSTR_GetName / GetInfo return for an unknown string id */
extern unsigned char mstr_empty_str[4];                           /* 0x0027bda0 */
/* menu/mobj.c: "" (main ELF sdata, gp-0x7158) that DF_MOBJ_NewStringBase points a new string object at */
extern unsigned char mobj_empty_str[4];                           /* 0x0027bb18 */
/* menu/mutil.c: MenuUtil_NewSceneNameString text colour (main ELF sdata, gp-0x69c8) */
extern struct_76b810ec mutil_scene_name_col;                       /* 0x0027c2a8 */
/* menu/mappellation_mng.c, msecretparts_mng.c: sprintf formats (main ELF sdata; gp-0x7880 "%s", gp-0x6f00 "%s<BR>",
 * gp-0x6eec "%s") */
extern char mappel_fmt_s[4];                                      /* 0x0027b3f0 */
extern char msecret_fmt_s_br[8];                                  /* 0x0027bd70 */
/* menu/marena_ranking.c: the name_com sprintf format "%s" (main ELF sdata, gp-0x787c) */
extern char marena_fmt_s[4];                                      /* 0x0027b3f4 */
extern char msecret_fmt_s[4];                                     /* 0x0027bd84 */
/* menu/msemb.c __EMBDATA_New: the window part-name format (main ELF sdata, gp-0x6ed8) and the three layout part
 * names of the emblem list (OLMENU rodata) */
extern char msemb_fmt_ita[8];                                     /* 0x0027bd98 "ita_%d" */
extern char msemb_select_0[16];                                   /* 0x01ee00e0 "select_0" */
extern char msemb_yaji_ue_0[16];                                  /* 0x01ee00f0 "yaji_ue_0" */
extern char msemb_yaji_shita_0[16];                               /* 0x01ee0100 "yaji_shita_0" */
/* menu/mpart.c: DF_MPART_PartsID2Name / ID2Name sprintf formats (main ELF sdata; gp-0x6f8c "%s", gp-0x6f88 "",
 * gp-0x6f80 "SYS-%d") */
extern char mpart_fmt_s[4];                                       /* 0x0027bce4 */
extern char mpart_empty_str[4];                                   /* 0x0027bce8 */
extern char mpart_fmt_sys[8];                                     /* 0x0027bcf0 */
/* menu/mdialog.c: DF_MDIALOG_Open's DlgMng.chara_linep, 29 underscores (OLMENU rodata) */
extern char mdialog_chara_line[30];                               /* 0x01edc730 */
/* menu/mdialog.c: __DrawShopInfo's sprintf format "%ld" (main ELF sdata, gp-0x75f8) */
extern char mdialog_fmt_ld[4];                                    /* 0x0027b678 */
/* menu/mname.c: FadeIn2's layout part names "name_0" / "shita_0" and the empty start text (main ELF sdata, gp-0x71b0 /
 * gp-0x71a8 / gp-0x71a0) */
extern char mname_name0[8];                                       /* 0x0027bac0 */
extern char mname_shita0[8];                                      /* 0x0027bac8 */
extern char mname_empty_str[4];                                   /* 0x0027bad0 */
extern char msys_fmt_ita[8];                                      /* 0x0027be28 */
/* menu/msys.c: __GAMEDATA_SetData's sprintf format "%ld" (main ELF sdata, gp-0x6e40) and __GAMEDATA_New's pBroken
 * text "Corrupt File", stored as the string object's strp (OLMENU rodata) */
extern char msys_fmt_ld[4];                                       /* 0x0027be30 */
extern char msys_corrupt[16];                                     /* 0x01ee0310 */
/* menu/msys.c: __MakeMcGameSystemList's scroll-arrow part names "up_0" / "down_0" (main ELF sdata, gp-0x6e10 / -0x6e08) */
extern char msys_up_0[8];                                         /* 0x0027be60 */
extern char msys_down_0[8];                                       /* 0x0027be68 */
/* menu/masm.c: __AsmSys_UpdateTunedStr's layout part "tuned_0" and format "%s%3d%%" (main ELF sdata, gp-0x77e8 / -0x77e0) */
extern char masm_tune_d0[8];                                      /* 0x0027b488 */
extern char masm_fmt_tuned[8];                                    /* 0x0027b490 */
/* menu/masm.c: __AsmSys_PlayDialog's sprintf format "%s%s" (main ELF sdata, gp-0x77d8) */
extern char masm_fmt_ss[8];                                       /* 0x0027b498 */
/* menu/masm.c: __MASM_NewACSpecWin's underline, 24 underscores (OLMENU rodata; the string object keeps the pointer) */
extern char masm_spec_line[25];                                   /* 0x01edaf40 */
/* menu/masmp.c: DF_MASMP_NewGeneWin's underline, 23 underscores (OLMENU rodata; the string object keeps the pointer) */
extern char masmp_underline[24];                                  /* 0x01edbdf0 */
/* menu/masmp.c: __MASMP_SetGeneCore's "Unknown" (main ELF sdata, gp-0x7758; the string object keeps the pointer) */
extern char masmp_unknown[8];                                     /* 0x0027b518 */
/* menu/masmd.c: the empty string (main ELF sdata, gp-0x77d0; Shield header, the string object keeps the pointer) */
extern char masmd_empty_str[1];                                   /* 0x0027b4a0 */
/* menu/masmd.c: Howitzer's unknown sight name "unkonwn" (sic; main ELF sdata, gp-0x77a0; kept by the string object) */
extern char masmd_unkonwn_str[8];                                 /* 0x0027b4d0 */
/* menu/masmd.c: __MASMD_AddParam's compare-row format (OLMENU rodata; handed to the interpreted Pf_SnPrintf) */
extern char masmd_param_fmt[65];                                  /* 0x01edb930 */

/* menu/mvsg.c: __MVSG_ItemSetCursor / ItemCreate sprintf formats and strings (OLMENU rodata 0x1ee4198.., main ELF
 * sdata 0x27c328..; newlib sprintf runs on PS2 memory in the hybrid) */
extern char mvsg_parts10a_0[11];                                      /* 0x01ee4198 */
extern char mvsg_fmt_parts10[11];                                     /* 0x01ee41a8 */
extern char mvsg_fmt_parts20t[12];                                    /* 0x01ee41b8 */
extern char mvsg_fmt_com[10];                                         /* 0x01ee41c8 */
extern char mvsg_fmt_parts20f[12];                                    /* 0x01ee41d8 */
extern char mvsg_fmt_parts20w[12];                                    /* 0x01ee41e8 */
extern char mvsg_fmt_parts20l[12];                                    /* 0x01ee41f8 */
extern char mvsg_fmt_parts20c[12];                                    /* 0x01ee4208 */
extern char mvsg_fmt_parts20wc[13];                                   /* 0x01ee4218 */
extern char mvsg_fmt_parts20lc[13];                                   /* 0x01ee4228 */
extern char mvsg_fmt_parts20b[12];                                    /* 0x01ee4238 */
extern char mvsg_banner_0[9];                                         /* 0x01ee4248 */
extern char mvsg_parts20d_22[12];                                     /* 0x01ee4258 */
extern char mvsg_parts10_0[10];                                       /* 0x01ee4268 */
extern char mvsg_fmt_name[8];                                         /* 0x0027c328 */
extern char mvsg_leader[8];                                           /* 0x0027c330 */
extern char mvsg_empty[1];                                            /* 0x0027c338 */
extern char mvsg_fmt_player[8];                                       /* 0x0027c340 */
extern char mvsg_red[5];                                              /* 0x0027c348 */
extern char mvsg_blue[6];                                             /* 0x0027c350 */
extern char mvsg_space[2];                                            /* 0x0027c358 */
extern char mvsg_fmt_s[3];                                            /* 0x0027c324 "%s" */
extern char mvsg_fmt_d[3];                                            /* 0x0027c35c "%d" */
/* menu/membe.c: __FadeInWindowList's window titles kept by DF_MOBJ_NewStringDirect (main ELF sdata, gp-0x74e0 /
 * gp-0x74d0) */
extern char membe_palette_str[8];                                     /* 0x0027b790 "PALETTE" */
extern char membe_preview_str[8];                                     /* 0x0027b7a0 "PREVIEW" */
/* menu/membe.c: __DrawEmbEditSystem's sprintf formats (main ELF sdata, gp-0x74c0 / gp-0x74b8) */
extern char membe_fmt_03d[5];                                         /* 0x0027b7b0 "%03d" */
extern char membe_fmt_1d[4];                                          /* 0x0027b7b8 "%1d" */
/* menu/mefunc.c: __CreateRgbCtrl's bar labels (kept by DF_MOBJ_NewStringDirect) and the RGB value format (main ELF
 * sdata 0x27b718.., gp-0x753c = "%d") */
extern char mefunc_str_red[4];                                        /* 0x0027b718 "RED" */
extern char mefunc_str_green[6];                                      /* 0x0027b720 "GREEN" */
extern char mefunc_str_blue[5];                                       /* 0x0027b728 "BLUE" */
extern char mefunc_fmt_d[3];                                          /* 0x0027b734 "%d" */
/* menu/mcolor.c: SmplColSystem layout-part names (WindowEx / blink-sprite constructors) and the newlib sprintf format
 * (OLMENU 0x1edc108..0x1edc280) */
extern char mcolor_parts02_0[10];                                     /* 0x01edc108 "parts02_0" */
extern char mcolor_fmt_light_cursor[16];                              /* 0x01edc230 "light_cursor_%d" */
extern char mcolor_light_cursor_0[15];                                /* 0x01edc260 "light_cursor_0" */
extern char mcolor_allow_up_0[11];                                    /* 0x01edc270 "allow_up_0" */
extern char mcolor_allow_down_0[13];                                  /* 0x01edc280 "allow_down_0" */
/* menu/mcolor.c: SelPartMode window / cursor layout-part names */
extern char mcolor_parts02_1[10];                                     /* 0x01edc118 "parts02_1" */
extern char mcolor_parts02_2[10];                                     /* 0x01edc128 "parts02_2" */
extern char mcolor_light_cursor_mk2_0[19];                            /* 0x01edc1f0 "light_cursor_mk2_0" */
extern char mcolor_light_cursor_mk2_1[19];                            /* 0x01edc210 "light_cursor_mk2_1" */
/* menu/mcolor.c: EditPartCol window layout name, "%03d" sprintf format and the RGB bar labels (main ELF sdata) */
extern char mcolor_parts06a_0[11];                                    /* 0x01edc250 "parts06a_0" */
extern char mcolor_fmt_03d[5];                                        /* 0x0027b638 "%03d" */
extern char mcolor_str_red[4];                                        /* 0x0027b5f8 "RED" */
extern char mcolor_str_green[6];                                      /* 0x0027b600 "GREEN" */
extern char mcolor_str_blue[5];                                       /* 0x0027b608 "BLUE" */
/* menu/mcolor.c: EditColSystem part-list sprite name format (sprintf) */
extern char mcolor_fmt_parts08[11];                                  /* 0x01edc240 "parts08_%d" */
/* menu/mvss.c: situation-setup sprintf formats and %s arguments (OLMENU rodata 0x1ee4550.., main ELF sdata
 * 0x27c3d4..; newlib sprintf runs on PS2 memory in the hybrid) */
extern char mvss_live_monitor[13];                                    /* 0x01ee4550 "LIVE MONITOR" */
extern char mvss_fmt_parts10[11];                                     /* 0x01ee4580 "parts10_%d" */
extern char mvss_fmt_parts10a[12];                                    /* 0x01ee4590 "parts10a_%d" */
extern char mvss_no_limit[9];                                         /* 0x01ee45a0 "NO LIMIT" */
extern char mvss_fmt_parts20d[12];                                    /* 0x01ee45b0 "parts20d_%d" */
extern char mvss_player_0[9];                                         /* 0x01ee45c0 "player_0" */
extern char mvss_p1[3];                                               /* 0x0027c3d4 "1P" */
extern char mvss_p2[3];                                               /* 0x0027c3d8 "2P" */
extern char mvss_p3[3];                                               /* 0x0027c3dc "3P" */
extern char mvss_p4[3];                                               /* 0x0027c3e0 "4P" */
extern char mvss_amp[2];                                              /* 0x0027c3e4 "&" */
extern char mvss_empty[1];                                            /* 0x0027c3e8 "" */
extern char mvss_fmt_time[8];                                         /* 0x0027c3f8 "%02d:00" */
extern char mvss_fmt_s[3];                                            /* 0x0027c400 "%s" */
extern char mvss_fmt_sss[7];                                          /* 0x0027c408 "%s%s%s" */
extern char mvst_fmt_parts20e[12];                                    /* 0x01ee47c8 "parts20e_%d" */
extern char mcf_fmt_ssss[9];                                          /* 0x0025ff80 "%s%s%s%s" */
extern char mcf_slus_dir[14];                                         /* 0x00260000 "/BASLUS-21200" */
extern char mcf_fmt_iconsys[14];                                      /* 0x00260010 "%s%s/icon.sys" */
extern char mcf_fmt_ss[5];                                            /* 0x0027af80 "%s%s" */
extern char mcf_fmt_ss_s[8];                                          /* 0x0027af88 "%s%s/%s" */
/* menu/mcockpit.c: ColSys light-cursor sprintf format and layout-part names (OLMENU 0x1edc050..0x1edc070) */
extern char mcockpit_fmt_light_cursor[16];                            /* 0x01edc050 "light_cursor_%d" */
extern char mcockpit_parts02_0[10];                                   /* 0x01edc060 "parts02_0" */
extern char mcockpit_light_cursor_0[15];                              /* 0x01edc070 "light_cursor_0" */
/* menu/mcockpit.c g2: SubSys sprintf formats and layout-part names (OLMENU 0x1edc080..0x1edc0b0) */
extern char mcockpit_fmt_parts10[11];                                 /* 0x01edc080 "parts10_%d" */
extern char mcockpit_fmt_parts13[11];                                 /* 0x01edc090 "parts13_%d" */
extern char mcockpit_parts10_2[10];                                   /* 0x01edc0a0 "parts10_2" */
extern char mcockpit_panel_ita_0[12];                                 /* 0x01edc0b0 "panel_ita_0" */
/* menu/mvse.c: Versus entry screen sprintf formats, layout-part names and tag strings (OLMENU rodata 0x1ee4128..,
 * main ELF sdata 0x27c2bc..; newlib sprintf runs on PS2 memory, DF_MOBJ_NewStringDirect keeps the tag pointers) */
extern char mvse_fmt_parts10[11];                                     /* 0x01ee4128 "parts10_%d" */
extern char mvse_fmt_player[23];                                      /* 0x01ee4140 "%1dPLAYER%s  %s SCREEN" */
extern char mvse_fmt_parts20b[12];                                    /* 0x01ee4158 "parts20b_%d" */
extern char mvse_parts10_0[10];                                       /* 0x01ee4168 "parts10_0" */
extern char mvse_parts20a_0[11];                                      /* 0x01ee4178 "parts20a_0" */
extern char mvse_str_1p[3];                                           /* 0x0027c2bc "1P" */
extern char mvse_str_empty[1];                                        /* 0x0027c2c0 "" */
extern char mvse_str_1p2p[6];                                         /* 0x0027c2c8 "1P&2P" */
extern char mvse_str_2p[3];                                           /* 0x0027c2d0 "2P" */
extern char mvse_str_3plive[8];                                       /* 0x0027c2d8 "3P&LIVE" */
extern char mvse_str_3p4p[6];                                         /* 0x0027c2e0 "3P&4P" */
extern char mvse_str_live[5];                                         /* 0x0027c2e8 "LIVE" */
extern char mvse_str_3p[3];                                           /* 0x0027c2f0 "3P" */
extern char mvse_str_4p[3];                                           /* 0x0027c2f4 "4P" */
extern char mvse_str_full[5];                                         /* 0x0027c300 "FULL" */
extern char mvse_str_split[6];                                        /* 0x0027c308 "SPLIT" */
extern char mvse_str_s[2];                                            /* 0x0027c310 "S" */
extern char mvse_fmt_ita[7];                                          /* 0x0027c318 "ita_%d" */
extern char mvse_fmt_s[3];                                            /* 0x0027c320 "%s" (NewSystem) */
extern char mperf_fmt_d[3];                                           /* 0x0027bd30 "%d" */
extern char mperf_str_empty[1];                                       /* 0x0027bd34 "" */
extern char mperf_fmt_ac[5];                                          /* 0x0027bd38 "AC%d" */
extern char mop_str_empty[8];                                         /* 0x0027bbe0 "" (8 zero bytes; stored by string objects) */
extern char mop_fmt_sp_d_sl[5];                                       /* 0x0027bbe8 " %d/" */
extern char mop_fmt_d_sl[4];                                          /* 0x0027bbf0 "%d/" */
extern char mop_fmt_s_sp_d[6];                                        /* 0x0027bbf8 "%s %d" */
extern char mop_fmt_s_d[5];                                           /* 0x0027bc00 "%s%d" */
extern char mop_fmt_sp_d[4];                                          /* 0x0027bc10 " %d" */
extern char mop_fmt_d[3];                                             /* 0x0027bc14 "%d" */
extern char mop_fmt_parts13[11];                                      /* 0x01edf7f0 "parts13_%d" */
extern char mop_fmt_parts10[11];                                      /* 0x01edf800 "parts10_%d" */
extern char mop_fmt_parts14[11];                                      /* 0x01edf810 "parts14_%d" */
extern char mop_name_parts13_12[11];                                  /* 0x01edf730 "parts13_12" (also a sprintf format) */
extern char mop_name_parts13_13[11];                                  /* 0x01edf740 "parts13_13" (also a sprintf format) */
extern char mop_name_parts04_0[10];                                   /* 0x01edf7a0 "parts04_0" (NexusNewMdlWin inUnused3) */
extern char mop_fmt_parts13_0[10];                                    /* 0x01edf8a0 "parts13_0" (a sprintf format) */

/* menu/moption.c option screen (garage3 g3a): Pf_SPrintf formats / layout part names (OLMENU rodata, main sdata) */
extern char mopt_name_parts10_7[10];                                  /* 0x01edf960 "parts10_7" */
extern char mopt_name_parts10_6[10];                                  /* 0x01edf970 "parts10_6" */
extern char mopt_name_parts10_4[10];                                  /* 0x01edf980 "parts10_4" */
extern char mopt_name_parts10_5[10];                                  /* 0x01edf990 "parts10_5" */
extern char mopt_name_parts10_3[10];                                  /* 0x01edf9a0 "parts10_3" */
extern char mopt_name_parts10_2[10];                                  /* 0x01edf9b0 "parts10_2" */
extern char mopt_name_parts10_0[10];                                  /* 0x01edf9c0 "parts10_0" */
extern char mopt_name_parts10_1[10];                                  /* 0x01edf9d0 "parts10_1" */
extern char mopt_name_parts10a_0[11];                                 /* 0x01edf9e0 "parts10a_0" */
extern char mopt_fmt_sound[9];                                        /* 0x01edf9f0 "sound_%d" */
extern char mopt_fmt_stereo2[11];                                     /* 0x01edfa00 "stereo2_%d" */
extern char mopt_fmt_2sentaku[12];                                    /* 0x01edfa10 "2sentaku_%d" */
extern char mopt_fmt_parts20d[12];                                    /* 0x01edfa20 "parts20d_%d" */
extern char mopt_name_dolby2_0[9];                                    /* 0x01edfa30 "dolby2_0" */
extern char mopt_fmt_3d[4];                                           /* 0x0027bc4c "%3d" */
extern char mopt_fmt_key[7];                                          /* 0x0027bc50 "key_%d" */
extern char mopt_name_left_0[7];                                      /* 0x0027bc58 "left_0" */
extern char mopt_name_right_0[8];                                     /* 0x0027bc60 "right_0" */
extern char mopt_name_left_1[7];                                      /* 0x0027bc68 "left_1" */
extern char mopt_name_right_1[8];                                     /* 0x0027bc70 "right_1" */
extern char mtitle_movie_path[33];                                   /* 0x01ee03d0 "cdrom0:\MOVIE\DISC\ACNB_OP.PSS;1" (Movie_PlayForMenu, hle-hooked) */
/* menu/mvsr.c: sprintf formats / layout part names (OLMENU rodata; main ELF sdata gp-0x6908 "ita_%d") */
extern char mvsr_fmt_parts20b[12];                                    /* 0x01ee4290 "parts20b_%d" */
extern char mvsr_parts10_0[10];                                       /* 0x01ee42a0 "parts10_0" (a sprintf format) */
extern char mvsr_parts20b_2[11];                                      /* 0x01ee42b0 "parts20b_2" (a sprintf format) */
extern char mvsr_fmt_parts10[11];                                     /* 0x01ee42c0 "parts10_%d" */
extern char mvsr_fmt_ita[7];                                          /* 0x0027c368 "ita_%d" */
extern char menu_fmt_movie[10];                                        /* 0x01edde30 "\MOVIE\%s" */
/* menu/mwepcolor1.c: layout part names / sprintf formats (OLMENU rodata) */
extern char mwepcolor1_parts02_0[10];                                 /* 0x01ee4990 "parts02_0" */
extern char mwepcolor1_fmt_parts10[11];                               /* 0x01ee49a0 "parts10_%d" */
extern char mwepcolor1_fmt_parts02[11];                               /* 0x01ee49b0 "parts02_%d" */
extern char mwepcolor1_fmt_parts08[11];                               /* 0x01ee49c0 "parts08_%d" */
extern char mwepcolor2_fmt_parts10[11];                              /* 0x01ee4a28 "parts10_%d" */
/* menu/munit_top_training.c: stMobjManager table (pLayoutHeader rewritten by the file) and sprintf formats */
extern struct stMobjManager munit_top_training_mng[2];               /* 0x01ee33b0 (2 x 0x40) */
extern char munit_top_training_fmt_shadow[15];                       /* 0x01ee3448 "dummy_shadow%d" */
extern char munit_top_training_fmt_class[14];                        /* 0x01ee3458 "dummy_class%d" */
extern char munit_top_training_fmt_d[3];                             /* 0x0027c29c "%d" */
extern char munit_top_training_str_12345[6];                         /* 0x0027c2a0 "12345" (a sprintf format) */
/* menu/munit_top_system.c: stMobjManager table (pLayoutHeader rewritten by the file) and sprintf formats */
extern struct stMobjManager munit_top_system_mng[1];                 /* 0x01ee3350 (1 x 0x40) */
extern char munit_top_system_fmt_pct[9];                             /* 0x01ee3390 "(%s%d%%)" */
extern char munit_top_system_fmt_d[3];                               /* 0x0027c24c "%d" */
extern char munit_top_system_pad0[1];                                /* 0x0027c250 "" (percent pad, >= 100) */
extern char munit_top_system_pad2[3];                                /* 0x0027c254 "  " (percent pad, 10..99) */
extern char munit_top_system_pad4[5];                                /* 0x0027c258 "    " (percent pad, < 10) */
extern char munit_top_system_fmt_c[3];                               /* 0x0027c260 "%c" */
extern char munit_top_system_str_st[3];                              /* 0x0027c264 "ST" (a sprintf format) */
extern char munit_top_system_str_nd[3];                              /* 0x0027c268 "ND" */
extern char munit_top_system_str_rd[3];                              /* 0x0027c26c "RD" */
extern char munit_top_system_str_th[3];                              /* 0x0027c270 "TH" */
extern char munit_top_system_str_space[2];                           /* 0x0027c274 " " */
/* menu/munit_ranker_name3.c: stMobjManager table (pLayoutHeader rewritten by the file) and sprintf formats */
extern struct stMobjManager munit_ranker_name3_mng[4];               /* 0x01ee2bf0 (4 x 0x40) */
extern char munit_ranker_name3_str_space[2];                         /* 0x0027c198 " " (a sprintf format) */
extern char munit_ranker_name3_fmt_d[3];                             /* 0x0027c19c "%d" */
extern char munit_ranker_name3_str_st[3];                            /* 0x0027c1a0 "ST" (a sprintf format) */
extern char munit_ranker_name3_str_nd[3];                            /* 0x0027c1a4 "ND" */
extern char munit_ranker_name3_str_rd[3];                            /* 0x0027c1a8 "RD" */
extern char munit_ranker_name3_str_th[3];                            /* 0x0027c1ac "TH" */
/* menu/munit_ranker_name2.c: stMobjManager table (entry 0's pLayoutHeader rewritten by the file) and sprintf formats
 * (addrsym shows the formats as munit_ez_ranking_str_space + 0xd8..) */
extern struct stMobjManager munit_ranker_name2_mng[2];               /* 0x01ee2b50 (2 x 0x40) */
extern char munit_ranker_name2_fmt_d[3];                             /* 0x0027c174 "%d" */
extern char munit_ranker_name2_str_st[3];                            /* 0x0027c178 "ST" (a sprintf format) */
extern char munit_ranker_name2_str_nd[3];                            /* 0x0027c17c "ND" */
extern char munit_ranker_name2_str_rd[3];                            /* 0x0027c180 "RD" */
extern char munit_ranker_name2_str_th[3];                            /* 0x0027c184 "TH" */
/* menu/munit_top_base.c: stMobjManager table (entry 0's pLayoutHeader rewritten by the file); right after
 * munit_top_arena_mng[5] (ends 0x1ee32b0), the "string_ac" name sits at 0x1ee32b0 in between */
extern struct stMobjManager munit_top_base_mng[2];                   /* 0x01ee32c0 (2 x 0x40) */
/* menu/munit_system.c: stMobjManager table (entry 0's pLayoutHeader rewritten by the file); addrsym shows it inside
 * munit_ranker_name3_mng's declared range */
extern struct stMobjManager munit_system_mng[4];                     /* 0x01ee2ea0 (4 x 0x40) */
/* menu/munit_top_arena.c: stMobjManager table (entry 0's pLayoutHeader rewritten by the file) and sprintf formats */
extern struct stMobjManager munit_top_arena_mng[5];                  /* 0x01ee3170 (5 x 0x40) */
extern char munit_top_arena_fmt_d[3];                                /* 0x0027c224 "%d" */
extern char munit_top_arena_fmt_c[3];                                /* 0x0027c228 "%c" */
extern char munit_top_arena_str_st[3];                               /* 0x0027c22c "ST" (a sprintf format) */
extern char munit_top_arena_str_nd[3];                               /* 0x0027c230 "ND" */
extern char munit_top_arena_str_rd[3];                               /* 0x0027c234 "RD" */
extern char munit_top_arena_str_th[3];                               /* 0x0027c238 "TH" */
extern char munit_top_arena_str_space[2];                            /* 0x0027c23c " " */
/* menu/munit_arena_frame.c: stMobjManager table (entry 0's pLayoutHeader rewritten by the file) and sprintf formats */
extern struct stMobjManager munit_arena_frame_mng[2];               /* 0x01ee1160 (2 x 0x40; addrsym: mtune BaseColor+0x140) */
extern char munit_arena_frame_fmt_d[3];                             /* 0x0027bf44 "%d" */
extern char munit_arena_frame_fmt_c[3];                             /* 0x0027bf48 "%c" */
extern char munit_arena_frame_str_st[3];                            /* 0x0027bf4c "ST" (a sprintf format) */
extern char munit_arena_frame_str_nd[3];                            /* 0x0027bf50 "ND" */
extern char munit_arena_frame_str_rd[3];                            /* 0x0027bf54 "RD" */
extern char munit_arena_frame_str_th[3];                            /* 0x0027bf58 "TH" */
extern char munit_arena_frame_str_string2[8];                       /* 0x0027bf60 "string2" (a sprintf format) */
/* menu/mvss2.c: situation-window sprintf formats and %s arguments (a second set next to mvss.c's: OLMENU rodata
 * 0x1ee4610.., main ELF sdata 0x27c41c..; newlib sprintf runs on PS2 memory in the hybrid) */
extern char mvss2_live_monitor[13];                                  /* 0x01ee4610 "LIVE MONITOR" */
extern char mvss2_situation[10];                                     /* 0x01ee4640 "SITUATION" (a sprintf format) */
extern char mvss2_time_limit[11];                                    /* 0x01ee4650 "TIME LIMIT" (a sprintf format) */
extern char mvss2_break_target[13];                                  /* 0x01ee4660 "BREAK TARGET" (a sprintf format) */
extern char mvss2_no_limit[9];                                       /* 0x01ee4670 "NO LIMIT" */
extern char mvss2_fmt_parts20d[12];                                  /* 0x01ee4680 "parts20d_%d" */
extern char mvss2_player_0[9];                                       /* 0x01ee4690 "player_0" (a sprintf format) */
extern char mvss2_1p[3];                                             /* 0x0027c41c "1P" */
extern char mvss2_2p[3];                                             /* 0x0027c420 "2P" */
extern char mvss2_3p[3];                                             /* 0x0027c424 "3P" */
extern char mvss2_4p[3];                                             /* 0x0027c428 "4P" */
extern char mvss2_amp[2];                                            /* 0x0027c42c "&" */
extern char mvss2_empty[1];                                          /* 0x0027c430 "" */
extern char mvss2_fmt_s[3];                                          /* 0x0027c434 "%s" */
extern char mvss2_fmt_time[8];                                       /* 0x0027c438 "%02d:00" */
extern char mvss2_all_ac[7];                                         /* 0x0027c440 "ALL AC" (a sprintf format) */
extern char mvss2_leader[7];                                         /* 0x0027c448 "LEADER" (a sprintf format) */
extern char mvss2_team[5];                                           /* 0x0027c450 "TEAM" (a sprintf format) */
extern char mvss2_fmt_sss[7];                                        /* 0x0027c458 "%s%s%s" */
/* menu/munit_colorbar.c: stMobjManager table (entry 0's pLayoutHeader rewritten by the file; addrsym shows it as
 * munit_arena_frame_mng+0x5b0, but that table ends at 0x1ee11e0) and the two sprite names Reset uses as sprintf formats */
extern struct stMobjManager munit_colorbar_mng[3];                   /* 0x01ee1710 (3 x 0x40) */
extern char munit_colorbar_name_mono[14];                            /* 0x01ee16e0 "colorbar_mono" (a sprintf format) */
extern char munit_colorbar_name_grad[14];                            /* 0x01ee16f0 "colorbar_grad" (a sprintf format) */
extern char mkeyguide_title[9];                                     /* 0x01edf010 "KEYGUIDE" (key guide title, attr.strp) */
/* menu/mbriefingplayer.c: MenuBriefingPlayer_New's default brief text "" (main ELF sdata, gp-0x7738; pBriefStr keeps it) */
extern char mbriefingplayer_empty_str[1];                            /* 0x0027b538 */
extern char mbriefingplayer_fmt_dstr[5];                             /* 0x0027b530 "%d%s" (Pf_SPrintf format, &X( reward text) */
extern char mbriefingplayer_fmt_color[20];                           /* 0x01edbff0 "<COLOR>%02x%02x%02x" (OLMENU rodata, line-wrap colour prefix) */
/* main/main.c: movie and overlay paths (main ELF rodata; handed to the interpreted Env_ConvertPath / Overlay_Load) */
extern char main_movie_e1[29];                                    /* 0x0025f9b0 */
extern char main_movie_e2[29];                                    /* 0x0025f9d0 */
extern char main_movie_i1[29];                                    /* 0x0025f9f0 */
extern char main_movie_i2[29];                                    /* 0x0025fa10 */
extern char main_ovl_sortie[23];                                  /* 0x0025fa30 */
extern char main_ovl_menu[21];                                    /* 0x0025fa50 */

/* main/pgplay.c: PGP_Create's PolyG name "test" (main ELF sdata, gp-0x7d10; handed to the interpreted
 * fsPs2PolyG_Create) */
extern char pgplay_polyg_name[5];                                  /* 0x0027af60 */
/* autogen/AutoGenLogic.c __AutoGenLogic_CreateComTree: sprintf(tree, "%s", "ComEdit") (main ELF sdata, gp-0x7f84 /
 * gp-0x7f80) */
extern char agl_fmt_s[4];                                         /* 0x0027acec "%s" */
extern char agl_comedit[8];                                       /* 0x0027acf0 "ComEdit" */

/* menu/marena_result.c: the result screen's sprintf formats / cell texts (main ELF sdata gp-0x7848 .. gp-0x7820, and
 * the play-time format in OLMENU rodata) and misc_0013e740's passcode character table (64 x 8-byte slots holding one
 * 2-byte SJIS full-width character + NUL; ELF_DATA in the source port) */
extern char marena_res_fmt_s[8];                                  /* 0x0027b428 "%s" */
extern char marena_res_dummy[8];                                  /* 0x0027b430 "dummy" */
extern char marena_res_fmt_d[8];                                  /* 0x0027b438 "%d" */
extern char marena_res_pts[8];                                    /* 0x0027b440 "PTS." */
extern char marena_res_ap_none[8];                                /* 0x0027b448 "??" 81 93 */
extern char marena_res_ap_pct[8];                                 /* 0x0027b450 "%d" 81 93 */
extern char marena_res_fmt_time[20];                              /* 0x01edaab0 OLMENU "%02d" 81 66 "%02d" 81 68 "%02d" */
extern unsigned char marena_passcode_chars[512];                  /* 0x00271400 */

/* menu/munit_arena_search.c: stMobjManager table (record 0's pLayoutHeader rewritten by the file) and sprintf formats */
extern struct stMobjManager munit_arena_search_mng[7];            /* 0x01ee12b0 (7 x 0x40; addrsym: _munit_arena_frame_mng+0x150) */
extern char munit_arena_search_fmt_element[12];                    /* 0x01ee1278 "element%d" */
extern char munit_arena_search_str_element0[16];                   /* 0x01ee1488 "element0" (a sprintf format) */
extern char munit_arena_search_str_element1[16];                   /* 0x01ee1498 "element18" (a sprintf format) */

/* menu/munit_ez_match.c: stMobjManager table (record 0's pLayoutHeader rewritten by the file) and the sprintf format */
extern struct stMobjManager munit_ez_match_mng[4];                /* 0x01ee1d80 (4 x 0x40) */
extern char munit_ez_match_fmt_element[16];                       /* 0x01ee1d58 "element%d" (also record 1's name) */

/* menu/munit_ez_ranking.c: table 2 (record 0's pLayoutHeader rewritten by the file; table 1 is the DWARF static
 * Move::aMobjData) and the sprintf formats */
extern struct stMobjManager munit_ez_ranking_mng2[7];             /* 0x01ee20e0 (7 x 0x40) */
extern char munit_ez_ranking_fmt_d[3];                            /* 0x0027c084 "%d" */
extern char munit_ez_ranking_fmt_c[3];                            /* 0x0027c088 "%c" */
extern char munit_ez_ranking_str_st[3];                           /* 0x0027c08c "ST" (a sprintf format) */
extern char munit_ez_ranking_str_nd[3];                           /* 0x0027c090 "ND" */
extern char munit_ez_ranking_str_rd[3];                           /* 0x0027c094 "RD" */
extern char munit_ez_ranking_str_th[3];                           /* 0x0027c098 "TH" */
extern char munit_ez_ranking_str_space[2];                        /* 0x0027c09c " " */

/* menu/munit_ez_official.c: stMobjManager table (record 0's pLayoutHeader rewritten by the file) */
extern struct stMobjManager munit_ez_official_mng[4];             /* 0x01ee1f10 (4 x 0x40) */
extern char munit_ez_official_fmt_d[3];                            /* 0x0027c048 "%d" */
extern char munit_ez_official_str_st[3];                           /* 0x0027c04c "ST" (a sprintf format) */
extern char munit_ez_official_str_nd[3];                           /* 0x0027c050 "ND" */
extern char munit_ez_official_str_rd[3];                           /* 0x0027c054 "RD" */
extern char munit_ez_official_str_th[3];                           /* 0x0027c058 "TH" */
extern char munit_ez_official_fmt_s[3];                            /* 0x0027c05c "%s" */
extern char munit_ez_official_str_dash2[3];                        /* 0x0027c060 "--" */
extern char munit_ez_official_str_dash5[6];                        /* 0x0027c068 "-----" */
extern char munit_ez_official_str_dash8[9];                        /* 0x01ee2030 "--------" */
extern char munit_ez_official_fmt_dummy_element[16];               /* 0x01ee2040 "dummy_element%d" */
/* menu/munit_official.c: stMobjManager table (record 0's pLayoutHeader rewritten by the file) + sprintf formats */
extern struct stMobjManager munit_official_mng[7];                /* 0x01ee22e0 (7 x 0x40) */
extern char munit_official_fmt_kage[7];                           /* 0x0027c0a0 "kage%d" */
extern char munit_official_fmt_d[3];                              /* 0x0027c0d0 "%d" */
extern char munit_official_str_st[3];                             /* 0x0027c0d4 "ST" (a sprintf format) */
extern char munit_official_str_nd[3];                             /* 0x0027c0d8 "ND" */
extern char munit_official_str_rd[3];                             /* 0x0027c0dc "RD" */
extern char munit_official_str_th[3];                             /* 0x0027c0e0 "TH" */
extern char munit_official_fmt_s[3];                              /* 0x0027c0e4 "%s" */
extern char munit_official_str_dash2[3];                          /* 0x0027c0e8 "--" */
extern char munit_official_str_dash5[6];                          /* 0x0027c0f0 "-----" */
extern char munit_official_str_dash8[9];                          /* 0x01ee24a0 "--------" */
/* menu/marena_official.c: sprintf format (no arguments) */
extern char marena_official_fmt_dummy_emblem[13];                 /* 0x01edaaa0 "dummy_emblem" */

/* menu/munit_arena_top.c / _base.c / _match.c: stMobjManager tables (record 0's pLayoutHeader rewritten by the files) */
extern struct stMobjManager munit_arena_top_mng[4];               /* 0x01ee14e0 (4 x 0x40; addrsym: _munit_arena_search_str_element1+0x48) */
extern struct stMobjManager munit_arena_base_mng[2];              /* 0x01ee10d0 (2 x 0x40; addrsym: mtune BaseColor+0xb0) */
extern struct stMobjManager munit_arena_match_mng[2];             /* 0x01ee11e0 (2 x 0x40; addrsym: _munit_arena_frame_mng+0x80) */
/* menu/munit_ranking_frame.c / munit_tab.c / munit_tab_info.c: stMobjManager tables (record 0's pLayoutHeader
 * rewritten by the files); addrsym shows them inside munit_ranker_name3_mng / munit_system_mng */
extern struct stMobjManager munit_ranking_frame_mng[2];           /* 0x01ee2e00 (2 x 0x40) */
extern struct stMobjManager munit_tab_mng[4];                     /* 0x01ee2fb0 (4 x 0x40) */
extern struct stMobjManager munit_tab_info_mng[2];                /* 0x01ee30c0 (2 x 0x40) */
/* menu/munit_briefing.c: 1-record stMobjManager table (pLayoutHeader rewritten by the file); addrsym shows it as
 * munit_arena_frame_mng+0x490 */
extern struct stMobjManager munit_briefing_mng[1];                /* 0x01ee15f0 (1 x 0x40) */

/* menu/mtrend_mng.c: sprintf formats (main sdata / OLMENU rodata) */
extern char mtrend_fmt_d[8];                                      /* 0x0027bee8 "%d" */
extern char mtrend_fmt_slash[4];                                  /* 0x0027bef8 "/" */
extern char mtrend_fmt_s_slash[4];                                /* 0x0027befc "%s/" */
extern char mtrend_fmt_time[16];                                  /* 0x01ee0558 OLMENU "%02d:%02d:%02d" */

/* lib/cdvd.c Cdvd_SetupFile: the two archive names handed to sceCdSearchFile (string literals) */
extern char cdvd_acbin_path[10];                                  /* 0x0025cb00 "\AC.BIN;1" */
extern char cdvd_discbin_path[12];                                /* 0x0025cb10 "\DISC.BIN;1" */

/* main/iop.c: IOP module table (local initialiser of __LoadPackIopModules, copied to its stack) and the strings
 * handed to the SIF hooks (main ELF rodata / sdata: guest addresses required) */
extern struct StModuleArg iop_module_tbl[36];                     /* 0x0025f500 (36 x 0x1c) */
extern char iop_rebootimg_path[23];                               /* 0x0025f8f0 "cdrom0:\IOPRP271.IMG;1" */
extern char iop_netcnf_arg[0x53];                                 /* 0x0025f910 "icon=cdrom0:\CONF\SYS_NET.ICO;1" (0x53 bytes passed) */
extern char iop_irxpack_path[22];                                 /* 0x0025f970 "cdrom0:\IOP\IRX.BIN;1" */
extern char iop_dev9x[7];                                         /* 0x0027af10 "dev9x:" */

/* hdd/hdd.c: device names (sdata literals) and TaskCreatePartition's partition-name format, handed to aio / sprintf
 * (guest addresses required) */
extern char hdd_dev_hdd0[6];                                      /* 0x0027ade0 "hdd0:" */
extern char hdd_dev_pfs0[6];                                      /* 0x0027ade8 "pfs0:" */
extern char hdd_prtt_fmt[29];                                     /* 0x0025c8a0 "%sP%c.%s.%s.%s,%s,%s,%dM,PFS" */
extern char loader_fmt_02d[5];                                    /* 0x0027adf0 "%02d" (loader_GetVersionString) */

/* lib/fslibmpg2_dd/playpss.c: mpg_GetRequiredBufferSize's buffer report (printf formats) */
extern char mpg_msg_rule[55];                                     /* 0x0026e460 "#####################################################\n" */
extern char mpg_msg_static[23];                                   /* 0x0026e4a0 "\tStatic buffer size :\n" */
extern char mpg_msg_voBufData[25];                                /* 0x0026e4c0 "\t\tvoBufData        : %d\n" */
extern char mpg_msg_voBufTag[25];                                 /* 0x0026e4e0 "\t\tvoBufTag         : %d\n" */
extern char mpg_msg_viBufTag[25];                                 /* 0x0026e500 "\t\tviBufTag         : %d\n" */
extern char mpg_msg_mpegWork[25];                                 /* 0x0026e520 "\t\tmpegWork         : %d\n" */
extern char mpg_msg_defStack[25];                                 /* 0x0026e540 "\t\tdefStack         : %d\n" */
extern char mpg_msg_readBuf[25];                                  /* 0x0026e560 "\t\treadBuf          : %d\n" */
extern char mpg_msg_audioBuff[25];                                /* 0x0026e580 "\t\taudioBuff        : %d\n" */
extern char mpg_msg_digitalAudioBuff[25];                         /* 0x0026e5a0 "\t\tdigitalAudioBuff : %d\n" */
extern char mpg_msg_viBufData[25];                                /* 0x0026e5c0 "\t\tviBufData        : %d\n" */
extern char mpg_msg_videoDecStatc[25];                            /* 0x0026e5e0 "\t\tvideoDecStatc    : %d\n" */
extern char mpg_msg_timeStamp[25];                                /* 0x0026e600 "\t\ttimeStamp        : %d\n" */
/* hdd/libaio.c: scePrintf messages (guest addresses: the printf hook reads PS2 memory) */
extern char aio_msg_unknown[29];                                  /* 0x0025c8c0 "aio: unknown i/o operation.\n" */
extern char aio_msg_overlap[40];                                  /* 0x0025c8e0 "aio: overlapped operation %d, while %d\n" */


/* lib/fs/fslibsd.c: the sound transport's SIF RPC objects (file statics without DWARF): the EE server of SID 0x102a,
 * its queue, the client bound to SID 0x101a; the CallRpcSyncWait timeout message (dead under hle.c) */
extern struct _sif_serve_data fssd_sd;                            /* 0x0028bc40 (0x44) */
extern struct _sif_queue_data fssd_qd;                            /* 0x0028bc90 (0x18) */
extern struct _sif_client_data fssd_cd;                           /* 0x0028ccf0 (0x28) */
extern char fssd_msg_sync_wait[45];                               /* 0x00259b70 "<CallRpcSyncWait> sceSifCheckStatRpc() == 1\n" */
/* lib/fslibsdivag: Init / Quit error messages (dead: the IOP module always answers 1) */
extern char fssdivag_msg_init[57];                                /* 0x0026ea00 "fssdivagInit : ERROR!!! IOP module can't initialized...\n" */
extern char fssdivag_msg_quit[61];                                /* 0x0026ea40 "fssdivagQuit : ERROR!!! IOP module didn't be initialized...\n" */
/* lib/fs/fslibsdsynth.c: SE / reverb error messages (error paths, unreached in the replays) */
extern char fssynth_msg_se_idx[46];                               /* 0x00259ca0 "<fslibsdsynth> se index isn't exist. - id=%d\n" */
extern char fssynth_msg_se_room[45];                              /* 0x00259cd0 "<fslibsdsynth> can't se on. - valid-size=%d\n" */
extern char fssynth_msg_se_id[46];                                /* 0x00259d00 "<fslibsdsynth> can't se on. - wrong-se-id=%d\n" */
extern char fssynth_msg_setrev_id[47];                            /* 0x00259d30 "<fssdsynthSetReverb> wrong reverb id. - id=%d\n" */
extern char fssynth_msg_rmrev_id[50];                             /* 0x00259d60 "<fssdsynthRemoveReverb> wrong reverb id. - id=%d\n" */
extern char fssynth_msg_rmrev_undef[53];                          /* 0x00259da0 "<fssdsynthRemoveReverb> reverb not defined. - id=%d\n" */
extern char fssynth_msg_rev_full[40];                             /* 0x00259de0 "<fslibsdsynth> reverb table over flow.\n" */
extern char fssynth_msg_stopsq_idx[40];                           /* 0x00259e10 "<fssdsynthStopSq> wrong index. - id=%d\n" */
extern char fssynth_msg_stopsq_undef[42];                         /* 0x00259e40 "<fssdsynthStopSq> sq not define. - id=%d\n" */
extern char fssynth_msg_stopsq_fail[50];                          /* 0x00259e70 "<fssdsynthStopSq> can't stop sq. - id=%d, res=%d\n" */
extern char fssynth_msg_eos_idx[44];                              /* 0x00259eb0 "<fssdsynthCheckSqEos> wrong index. - id=%d\n" */
extern char fssynth_msg_playsq_idx[40];                           /* 0x00259ee0 "<fssdsynthPlaySq> wrong index. - id=%d\n" */
extern char fssynth_msg_playsq_undef[42];                         /* 0x00259f10 "<fssdsynthPlaySq> sq not define. - id=%d\n" */
extern char fssynth_msg_playsq_fail[51];                          /* 0x00259f40 "<fssdsynthPlaySq> can't Start sq. - id=%d, res=%d\n" */
extern char fssynth_msg_rmsq_iop[42];                             /* 0x00259f80 "<fssdsynthRemoveSq> error iop addr. - %x\n" */
extern char fssynth_msg_rmsq_undef[49];                           /* 0x00259fb0 "<fssdsynthRemoveSq> sq file is't exist. - id=%d\n" */
extern char fssynth_msg_addr[9];                                  /* 0x00259fe8 "addr=%p\n" */
extern char fssynth_msg_bank[9];                                  /* 0x00259ff8 "bank=%d\n" */
extern char fssynth_msg_addsq_full[44];                           /* 0x0025a010 "<fssdsynthAddSqFromEE> sq table over flow.\n" */
extern char fssynth_msg_addsq_port[53];                           /* 0x0025a040 "<fssdsynthAddSqFromEE> wrong port number. - port=%d\n" */
extern char fssynth_msg_addsq_alloc[45];                          /* 0x0025a080 "<fssdsynthAddSqFromEE> can't alloc iop heap\n" */
extern char fssynth_msg_addsq_iop[25];                            /* 0x0025a0b0 "sq-iop-addr=0x%x...0x%x\n" */
extern char fssynth_msg_addsq_hd[60];                             /* 0x0025a0d0 "<fssdsynthAddSqFromEE> can't load hd to hsyn csl. - res=%d\n" */
extern char fssynth_msg_addsq_bank[32];                           /* 0x0025a110 "<fssdsynthAddSqFromEE> bank=%d\n" */
extern char fssynth_msg_addsq_sq[60];                             /* 0x0025a130 "<fssdsynthAddSqFromEE> can't load sq to midi csl. - res=%d\n" */
extern char fssynth_msg_rmbank_idx[51];                           /* 0x0025a170 "<fssdsynthRemoveBank> wong bank index. - index=%d\n" */
extern char fssynth_msg_rmbank_iop[44];                           /* 0x0025a1b0 "<fssdsynthRemoveBank> error iop addr. - %x\n" */
extern char fssynth_msg_rmbank_hd[51];                            /* 0x0025a1e0 "<fssdsynthRemoveBank> hd data is't exist. - id=%d\n" */
extern char fssynth_msg_addbank_full[49];                         /* 0x0025a220 "<fssdsynthAddBankFromEE2> bank table over flow.\n" */
extern char fssynth_msg_addbank_port[56];                         /* 0x0025a260 "<fssdsynthAddBankFromEE2> wrong port number. - port=%d\n" */
extern char fssynth_msg_addbank_alloc[48];                        /* 0x0025a2a0 "<fssdsynthAddBankFromEE2> can't alloc iop heap\n" */
extern char fssynth_msg_hd_addr[12];                              /* 0x0025a2d0 "hd-addr=%p\n" */
extern char fssynth_msg_addbank_hd[25];                           /* 0x0025a2e0 "HD IOP Addr:0x%x...0x%x\n" */
extern char fssynth_msg_addbank_mono[30];                         /* 0x0025a300 "Mono HD IOP Addr:0x%x...0x%x\n" */
extern char fssynth_msg_addbank_spu[57];                          /* 0x0025a320 "<fssdsynthAddBankFromEE2> cannot allocate SPU2 memory..." */
extern char fssynth_msg_addbank_dma[63];                          /* 0x0025a360 "<fssdsynthAddBankFromEE2> can't transfer buffer. - ee-buff=%x\n" */
extern char fssynth_msg_addbank_bd[60];                           /* 0x0025a3a0 "<fssdsynthAddBankFromEE2> can't trans bd to spu2. - res=%d\n" */
extern char fssynth_msg_addbank_hsyn[63];                         /* 0x0025a3e0 "<fssdsynthAddBankFromEE2> can't load hd to hsyn csl. - res=%d\n" */
extern char fssynth_msg_stereo_hd[52];                            /* 0x0025a420 "<fslibsdsynth> can't load hd to hsyn csl. - res=%d\n" */
extern char fssynth_msg_stereo_bank[24];                          /* 0x0025a460 "<fslibsdsynth> Bank %d\n" */
extern char fssynth_msg_transbuf[52];                             /* 0x0025a480 "<fslibsdsynth> can't Transfer Buffer. - EE-buff:%x\n" */

extern char fsmfstrm_msg_bind[44];                                /* 0x0026dc20 "!!!!!!!! sceSifBindRpc is failed. !!!!!!!!\n" */
extern char fsmfstrm_msg_thread[30];                              /* 0x0026dc50 "Starting RPC thread on EE...\n" */
extern char fsmfstrm_msg_create_thread[63];                       /* 0x0026dc70 "_CreateRpcThread : ERROR!!! CreateThread is failed. - id = %d\n" */
extern char fsmfstrm_msg_sync_wait[51];                           /* 0x0026dcb0 "CallRpcSyncWait : ERROR sceSifCheckStatRpc() == 1\n" */
extern char fsmfstrm_msg_send_over[58];                           /* 0x0026dcf0 "CallRpc : ERROR!!! RPC send size is over. iSend=%d -> %d\n" */
extern char fsmfstrm_msg_recv_over[61];                           /* 0x0026dd30 "CallRpc : ERROR!!! RPC receive size is over. iRecv=%d -> %d\n" */
extern char fsmfstrm_msg_struct_over[72];                         /* 0x0026dd70 "fsmfstrm_CallRpcStruct : ERROR!!! RPC send size is over.iSend=%d -> %d\n" */
extern char fsmfstrm_msg_alloc_info[68];                          /* 0x0026ddc0 "fsmfstrm_AllocStream : ERROR!!! can not find stream information...\n" */
extern char fsmfstrm_msg_alloc_iop[64];                           /* 0x0026de10 "fsmfstrm_AllocStream : ERROR!!! can not allocate IOP memory...\n" */
extern char mstrm_msg_init_sema[51];                              /* 0x0026de50 "mstrm_Init : ERROR!!! Can not create semaphore...\n" */
extern char mstrm_msg_create[65];                                 /* 0x0026de90 "mstrm_CreateStream : ERROR!!! can not get stream information...\n" */
extern char mstrm_msg_alloc_iop[71];                              /* 0x0026dee0 "mstrm_AllocIopStream : ERROR!!! IOP stream setup was failure...ret=%d\n" */
extern char mstrm_msg_delete_index[55];                           /* 0x0026df30 "mstrm_DeleteStream : ERROR!!! invalid stream index...\n" */
extern char mstrm_msg_freeiop_index[56];                          /* 0x0026df70 "mstrm_FreeIopStream : ERROR!!! invalid stream index...\n" */
extern char mstrm_msg_freeiop_fail[59];                           /* 0x0026dfb0 "mstrm_freeIopStream : ERROR!!! Can not free iop stream...\n" */
extern char mstrm_msg_freebuf_index[59];                          /* 0x0026dff0 "mstrm_FreeStreamBuffer : ERROR!!! invalid stream index...\n" */
extern char mstrm_msg_attach_func[6];                             /* 0x0026e030 "%s : " */
extern char mstrm_msg_attach[44];                                 /* 0x0026e060 "Index = %d, Lsn = %d, Size = %d, Prio = %d\n" */
extern char strm_msg_error[15];                                   /* 0x0026e0a0 "%s : ERROR!!! " */
extern char strm_msg_busy[30];                                    /* 0x0026e0b0 Shift-JIS "...\n" (cannot attach while busy) */
extern char strm_msg_name[27];                                    /* 0x0026e0d0 "[SECTOR] : %d, [SIZE] : %d" */
extern char strm_msg_endread[60];                                 /* 0x0026e0f0 "strm_EndReading : ERROR!!! sbuf_FreeReadBlock is failed...\n" */
/* link/fseth.c: scePrintf messages of the power-off cluster (guest addresses: the printf hook reads PS2 memory) */
extern char fseth_msg_where[11];                                  /* 0x0025cfe8 "%s(%04d): " */
extern char fseth_msg_terminate[65];                              /* 0x0025d000 "***** FSETH : failed Terminate Thread : return %d : ID %d *****
" */
extern char fseth_msg_delthread[54];                              /* 0x0025d050 "***** FSETH : failed Delete Thread : return %d *****
" */
extern char fseth_msg_delsema[57];                                /* 0x0025d090 "***** FSETH : failed Delete Semaphore : return %d *****
" */
extern char fseth_file[8];                                        /* 0x0027ae38 "fseth.c" (__FILE__ of the debug prints) */
extern char fseth_msg_downif[59];                                 /* 0x0025d0d0 "***** FSETH : sceInetCtlDownInterface() failed.(%d) *****" + LF */
extern char fseth_msg_noupif[60];                                 /* 0x0025d110 "***** FSETH : sceInetCtlUpInterface is not executed. *****" + LF */
extern char fseth_dev_pfs[5];                                     /* 0x0027ae40 "pfs:" (sceDevctl device of the power-off thread) */
extern char fseth_dev_dev9x[7];                                   /* 0x0027ae48 "dev9x:" */
/* lib/sdk _LoadEmbeddedIrx: the IRX embedded in the boot ELF ("ck01", 0x359 bytes up to 0x259b59, imports cdvdman),
 * 0x360 = the 16-rounded size the original DMAs to the IOP; ELF_DATA in the source port */
extern unsigned char embedded_irx_ck01[0x360];                    /* 0x00259800 */
extern char liblnk_msg_modem_error[18];                           /* 0x0025ea20 "Modem Error : %d" + LF (src/link/liblnk) */
extern char liblnk_ip_101[15];                                    /* 0x0025ea38 "172.16.255.101" */
extern char liblnk_ip_102[15];                                    /* 0x0025ea48 "172.16.255.102" */
extern char liblnk_msg_recv_error[41];                            /* 0x0025e9b0 "Data recv error, ret = %d( size = %d )" + 2 LF */
extern char liblnk_msg_packet_error[50];                          /* 0x0025e9e0 ">< >< >< [ Packet No. Error, %d != %d ] >< >< ><" + LF */
extern char liblnk_msg_iop_mem[14];                               /* 0x0025ea58 "IOP MEM = %d" + LF */
extern char liblnk_msg_vendor_error[22];                          /* 0x0025eb10 "  vendor = error[%d]" + LF */
extern char liblnk_msg_prio_start[20];                            /* 0x0025eb60 "Set Priority start" + LF */
extern char liblnk_msg_prio_end[18];                              /* 0x0025eb80 "Set Priority end" + LF */
extern char liblnk_msg_get_over_set[43];                          /* 0x0025ea70 "ERROR GET FRAME OVER SET FRAME !!!! %d %d" + LF */
extern char liblnk_msg_sync_timeout[43];                          /* 0x0025eaa0 "IlinkSyncProc timeout.( MySyncStat = %d )" + LF */
extern char mvsw_parts20a_0[11];                                      /* 0x01ee4900 "parts20a_0" (a sprintf format) */
extern char mvsw_possible[9];                                         /* 0x01ee4920 "POSSIBLE" */
extern char mvsw_impossible[11];                                      /* 0x01ee4930 "IMPOSSIBLE" */
extern char mvsw_fmt_com[7];                                          /* 0x0027c4d0 "COMx%d" */
extern char mvsw_no_limit[9];                                         /* 0x01ee4910 "NO LIMIT" */
extern char mvsw_fmt_time[8];                                         /* 0x0027c4a8 "%02d:00" */
extern char mvsw_all_ac[7];                                           /* 0x0027c4b0 "ALL AC" (a sprintf format) */
extern char mvsw_leader[7];                                           /* 0x0027c4b8 "LEADER" (a sprintf format) */
extern char mvsw_team[5];                                             /* 0x0027c4c0 "TEAM" (a sprintf format) */
extern char mvsw_fmt_s[3];                                            /* 0x0027c4c8 "%s" */
extern char mvsw2_parts20a_0[11];                                     /* 0x01ee4960 "parts20a_0" (a sprintf format) */
/* lib/sdk/fontlib.cpp (fslibfont): the current font's state pointers into s_Font (gp sbss words; the DWARF attributes
 * them to _HAKOKAGEMDL_COLOR__S__src_menu_mwepcolor2) and the character class table (bss), read by interpreted fontlib */
extern _FS_FONT_DATA_ST *fsfont_pFontData;                            /* 0x0027c580 */
extern struct_c5a57ef2 *fsfont_pPs2Info;                              /* 0x0027c584 */
extern _FS_FONT_DRAW_STATE_ST *fsfont_pDrawState;                     /* 0x0027c588 */
extern _FS_FONT_FORM_ST *fsfont_pStdForm;                             /* 0x0027c58c */
extern _FS_FONT_DEFTAG_MNG_ST *fsfont_pDefTagMng;                     /* 0x0027c590 */
extern _FS_FONT_TAG_MNG_ST *fsfont_pTagMng;                           /* 0x0027c594 */
extern unsigned char fsfont_charclass[256];                           /* 0x0027d100 */
extern int radar_noise_seed;                                         /* 0x0027b234 */
#ifdef __cplusplus
}
#endif
