/* src/menu/mdbg.cpp - menu debug helpers (OLMENU). Only DF_MDBG_PartsAll so far: the developers' "all parts" switch
 * (gMManageCtr.fPartsAll, called from __MenuStart / DF_M_AC_Init) and the unlock_parts option's worker (Stage 0:
 * sourceport/main/host_opt.cpp). */

/* Every part with a name goes into the garage of players 1 and 2: per menu kind (0..13, ids kind * 1000 + index) and
 * table index 0..255, a part whose ID2Name finds an entry is set present in the garage of its acpeq slot by true id;
 * a back weapon of category < 0x13 (menu kind 10, i.e. one also usable as an arm unit) goes into the arm-unit slot
 * (acpeq 11) too. The original keeps the state in a char: 1 = named, 2 = named back weapon usable as an arm unit,
 * 0 = no entry; the garage flag passed is (state > 0). */
void DF_MDBG_PartsAll(void)
{
    PS2_FRAME(0x90);
    for (int player = 0; player < 2; player++) {
        for (int kind = 0, base = 0; kind < 14; kind++, base += 1000) {
            int acpeq = MPart_MpartToAcpeq(kind);
            for (int i = 0; i < 256; i++) {
                int id = i + base;
                signed char st = 1;
                if (kind == 10 && DF_MPART_GetCategory(id) < 0x13) st = 2;
                if (DF_MPART_ID2Name(id, 0) != 1) st = 0;   /* the original passes kind in a2 too (unused) */
                if (!st) continue;
                int tid = DF_MPART_Index2TrueID(kind, i);
                if (tid < 0) continue;
                PlayerInfo_SetExistPartsGarage(player, acpeq, tid, st > 0);
                if (acpeq == 10 && DF_MPART_GetCategory(id) < 0x13) PlayerInfo_SetExistPartsGarage(player, 11, tid, 1);
            }
        }
    }
}
