/* Stage 0's fixed sim tweaks, set through the port hooks (acnb/hooks.h)
 * before the game's main(). No switches: Stage 0 always plays this way, and its replay header says so
 * (host_vsync.cpp sp_sim_settings / sim_header).
 *
 * 1. Interlace off: every store of the current field (Draw_FlipBuffer*, gfx/draw.cpp draw_set_field) stores the even
 *    field, so still scenes do not shimmer by half a line on a progressive display (= the hybrid's default; the dev
 *    tree's ACNB_INTERLACE_OFFSET=1 switch is gone).
 * 2. game.unlock_parts = 1 by default: every part is in the garage. A replay plays at the unlock_parts its settings
 *    header recorded (0 for old replays without a header; host_vsync.cpp sim_header, user s103), so old replays
 *    stay in step; new recordings write the value in force (= the development version's runtime/game_opt.c, ported). Part ownership
 *    lives in s_playerinfo (2 bits per part: garage / shop / repository). DF_MPART_SetAsmDefault fills it with every
 *    part not in s_aSecretPartsList - on a new game (__MenuStart) and on entering Versus (DF_M_AC_Init, after
 *    PlayerInfo_Init wiped it); the Versus garage lists player 1's garage parts (DF_MPART_MakeIDTable type 0). The
 *    developers' debug switch gMManageCtr.fPartsAll calls DF_MDBG_PartsAll there instead (every named part, players 1
 *    and 2). We run that after SetAsmDefault and after Save_DistributeSaveData (ownership loaded from the memory card),
 *    followed by DF_MPART_CorrectPlayerParts (drops arm-unit copies of back-only weapons, as __MenuStart does).
 *    Unlocked parts go into the memory card save when the game saves.
 * (Blur off, the third tweak, is a display option: host_rq.cpp RqOptions.blur_off.) */
#include <stdio.h>
#include <string.h>
#include "acnb/types.h"
#include "acnb/globals.h"
#include "acnb/funcs.h"
#include "acnb/hooks.h"
#include "../../port/platform/ovlreg.h"

extern "C" int sp_unlock_parts = 1;   /* 1 = default; a replay's header may set 0 */

namespace {

int field_even(int field) { (void)field; return 0; }

int garage_parts(int player)   /* parts with state 1 (garage) in bits 4-5 of byte 2 of [17 acpeq][256 id] words */
{
    const unsigned char *s = (const unsigned char *)&s_playerinfo[player] + 0x360;
    int n = 0;
    for (int i = 0; i < 17 * 256; i++) n += (s[i * 4 + 2] & 0x30) == 0x10;
    return n;
}

void unlock_all_parts(const char *where)
{
    if (!sp_unlock_parts) return;   /* replay recorded with unlock_parts=0 */
    const char *o = ovl_current();
    if (!o || strcmp(o, "OLMENU")) return;   /* the parts tables live in OLMENU */
    int before = garage_parts(0);
    DF_MDBG_PartsAll();
    for (int p = 0; p < 4; p++) DF_MPART_CorrectPlayerParts(p);
    fprintf(stderr, "game: all parts unlocked after %s (player 1 garage parts %d -> %d)\n", where, before, garage_parts(0));
}

void hook_asm_default(void) { unlock_all_parts("default parts"); }
void hook_dist_save(void) { unlock_all_parts("memory card load"); }

}  // namespace

extern "C" void sp_opt_install(void)
{
    hook_field_store = field_even;
    hook_mpart_asm_default = hook_asm_default;
    hook_distribute_save = hook_dist_save;
}
