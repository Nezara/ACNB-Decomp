/* Logical state hash: once per vblank, at the sandbox poke point of both builds (hybrid
 * runtime/video.c video_vblank after the pad poll, source port host_vsync.cpp step() after step 5), hash a named list
 * of game fields (port/statehash/fields.txt, DWARF paths compiled against sbx_types.tsv through platform/names) per
 * area and write
 *     state vb V fr F ov O: TOTAL flow H rng H ac0 H .. spl N:H .. full H
 * through the backend's log (hybrid ee_log -> stderr, source port stderr). Reads memory only: no game code, no hooks.
 * Env: ACNB_STATE_HASH=1 (on), ACNB_STATE_FIELDS (field file, default port/statehash/fields.txt), ACNB_STATE_DUMP=V[,V..]
 * | A-B (write ACNB_STATE_DUMP_DIR/state_<vb>.txt, default dir `state` in the working directory: `area path = value`
 * per field), ACNB_STATE_FULL=0 (skip the full tier: `full -`). Off = statehash_on() is one cached test per vblank;
 * on, the cost per vblank is logged every 1000 vblanks (`statehash: cost ..`). */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* ACNB_STATE_HASH set (read once) */
int statehash_on(void);
/* one vblank (call only when statehash_on(); the build's backend must be set: sbx_hybrid_init / sbx_sourceport_init).
 * The first call loads the names + the field file; a load error is logged and turns the hash off for the run. */
void statehash_vblank(uint32_t vb);

#ifdef __cplusplus
}
#endif
