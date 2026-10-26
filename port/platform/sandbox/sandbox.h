/* Sandbox: the state-aware input script engine (--script F, was runtime/script.c) shared by both
 * builds, over a small per-build backend: hybrid runtime/sbx_hybrid.c (guest memory, PS2 addresses), source port
 * sourceport/main/host_sbx.cpp (host memory, module base + RVA). The engine never touches game code; it reads game
 * state through the backend and decides the pads at the pad-poll point of each vblank (hybrid video.c poll_input,
 * source port host_vsync step 5). Command and expression reference: script.cpp's header.
 *
 * Names (platform/names/names.h since s103): a script never holds a raw address. Symbols come from the generated table sbx_names.tsv beside the exe
 * (scripts/sbx_names.py; post-link step of both builds), struct layouts for field paths from sbx_types.tsv; the table's
 * sentinels are checked against the running build at load (a stale table refuses the script). Places:
 *   NAME                    symbol (unique name; a clash is a load error)
 *   FILE.c:NAME             the symbol of that source file (menu/msys.c:__PlaySystem or msys.c:__PlaySystem)
 *   @0xADDR                 the symbol whose PS2 address is ADDR (looked up in the table: valid in both builds)
 *   NAME[i].field[j]..      field path through the DWARF types (arrays / struct members, no pointer chase)
 * Steps S1+ add pokes, faults and overlays here. */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include "../ps2_pad.h"
#include "../names/names.h"   /* backend + name / type tables (shared with platform/statehash) */
#ifdef __cplusplus
extern "C" {
#endif

/* SbxBackend (the build backend) and the name table API: ../names/names.h */


/* --script F: reads the script (+ includes), the name tables, lints every expression. 1 = ok, 0 = refused (logged) */
int sbx_script_load(const char *path);
int sbx_script_active(void);
int sbx_script_failed(void);   /* the script failed (timeout / assert / runtime error) or a sandbox replay was refused */
int sbx_exit_code(void);       /* 0 ok, 3 script / event failure, 2 sandbox replay refused at its start (card hash) */

/* S1 pokes + replay format v3.
 * Recording: a script that uses poke / freeze / unfreeze / card records format v3 (header below); any other script
 * records plain v2 exactly as before. The build calls sbx_rec_start where it would write the v2 header: 1 = the
 * sandbox took the file (it writes "#acnb-replay 3" + "#acnb-sim SIMCFG" + "#acnb-sbx .." + "#sbx-line" lines itself
 * at the first poll point, once the card can be hashed), 0 = write the v2 header as before. A v3 replay played while
 * recording is re-recorded as v3 (same header and events). */
int sbx_rec_start(FILE *rec, const char *simcfg);
/* Playback: after replay_load accepted a format-3 file (replay_version() == 3), before the game starts. Loads the
 * name table, checks the header (overlays, pins -> backend set_pins), lints every event line. 1 = ok, 0 = refused
 * (logged; the build exits 2). Logs "sandbox: SANDBOX replay (script X, N events)". */
int sbx_replay_load(const char *path);
/* The poke point: once per vblank at the pad-poll point, after the pads are decided (replay
 * frame / script poll), before the latch - hybrid video.c poll_input, source port host_vsync step 5. Applies the v3
 * replay's events of vblank vb (file order), then re-applies every active freeze. A failed event sets *exit_after. */
void sbx_vblank(uint32_t vb, uint32_t *exit_after);
/* One vblank: decide both pads (pf[0] pad 1, pf[1] pad 2, *p2_on = pad 2 plugged in). rec = the --record file (segment
 * marks), *shot = request a PNG, *exit_after = finish (set once). */
void sbx_script_poll(uint32_t vb, PadFrame pf[2], int *p2_on, FILE *rec, int *shot, uint32_t *exit_after);

#ifdef __cplusplus
}
#endif
