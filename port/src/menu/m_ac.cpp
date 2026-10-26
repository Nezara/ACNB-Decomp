/* src/menu/m_ac.cpp — the garage AC viewer / editor logic (OLMENU). Two players x three stMenuAC records
 * (M_ACTable[2], 0x3e0 bytes each; M_ACp[2][3] is the rotated view the draw functions use) hold the current AC:
 * 21 part slots (menu part id kind * 1000 + index plus an _stMModel), the equipment colours, camouflage, tune stats
 * and the "tmp part" being previewed. DF_M_AC_Play runs each AC's read / play state machine, __ConnectPart attaches
 * every part to its parent, DF_M_AC_MakeVu1Packet / DrawShadow build the GS packets.
 *
 * Native here (group G1): the player-info transfer (stMenuAC <-> the player's AcAssemblyPartsSt record, the mission
 * table copy), the tune-stat readers, the colour-record conversion and the equip-index / slot-index / part-id maps;
 * group G3: the equip-flag / tmp-part checks, the hand models, the per-slot part set-up (__SetPartInfo*) and the
 * draw-table builder / select highlight of the previewed part; group G4: the read state machines (whole AC and the
 * previewed part), the fade / basic play functions, DF_M_AC_Play, SelectPart, SetEquipNecessary and __DeleteTmpParts;
 * group G5: the tmp part preview / commit (SetTmpPart, UpdateTmpPart and its per-slot __Update* helpers);
 * group G2: the fade / colour / camouflage setters, PartsRead, Select / SetAll, InitWork / Init, the carousel
 * positions (SetCirclePos / NXSetCirclePos), Reset / Reset2, Copy and SetData; group G6a: the per-slot attach
 * switch __ConnectPart, DF_M_AC_ConnectPartByID and the MI2 node walkers (__ConnectObject / BaseAnim); group G6b:
 * the attach helpers (weapon arms, spare / arm weapons, extensions, arm inside covers, hands); rest part A: the
 * mission-transfer chain (never runs: M_AC_Trans is only ever written with 0); rest part B: the dirt texture pass
 * (__SetPartTM2 / __OneAC_ReadAll_SetAllTM2, gMManageCtr.fAddDirt is 0) and the sell / reposit checks (RemovePart,
 * IsEquipPart3 / 4), never executed in the replays.
 * Group G7 (phase 2 step 2 group 5b): the packet drivers (per-AC VU1 model packets, the emblem TM2 upload), the
 * shadow VIF chain (__OneAC_MakeShadowPkt), DF_M_AC_DrawShadow and the projected shadow boards (SPR GIF packets). */
#include <string.h>
#include "acnb/sdk.h"
#include "acnb/vu0m.h"

static int __EquipIDX2IDX(int inIdx);

/* the AC record of player p, slot n (the original indexes M_ACTable[p] without a bounds test beyond its callers') */
static inline stMenuAC *mac_table(int p, int n)
{
    return M_ACTable[p] + n;
}

struct_c4910010 *M_AC_GetWepColorFromACPEQ(eAcPartsEquip equip, struct_098975f3 *color)
{
    unsigned char *c = (unsigned char *)color;
    switch ((unsigned char)equip) {
    case 10: return (struct_c4910010 *)(c + 0x90);
    case 11: return (struct_c4910010 *)(c + 0xa0);
    case 12: return (struct_c4910010 *)(c + 0x50);
    case 13: return (struct_c4910010 *)(c + 0x60);
    case 14: return (struct_c4910010 *)(c + 0x70);
    case 15: return (struct_c4910010 *)(c + 0x80);
    default: return (struct_c4910010 *)(c + 0x50);
    }
}

eAcTuneParts M_AC_eAcPartsEquip_to_eAcTuneParts(eAcPartsEquip parts_kind)
{
    unsigned int k = (unsigned char)parts_kind;
    return (eAcTuneParts)(k < 8 ? k : 0);
}

int DF_M_AC_CheckCompose(int nPlayer, int nAc)
{
    PS2_FRAME(0x40);
    /* every mandatory slot (0..3, 5..7) must be set; the dead player-0 query before each `return 0` is original */
    for (int jj = 0; jj < 4; jj++)
        if (PlayerInfo_GetAssemblyPartsId(nPlayer, nAc, jj) == 0) {
            PlayerInfo_GetAssemblyPartsId(0, nAc, jj);
            return 0;
        }
    for (int jj = 5; jj < 8; jj++)
        if (PlayerInfo_GetAssemblyPartsId(nPlayer, nAc, jj) == 0) {
            PlayerInfo_GetAssemblyPartsId(0, nAc, jj);
            return 0;
        }
    /* the leg query reads player 0's record, not nPlayer's (a0 = 0 left by the loop's delay slot) */
    int kind = DF_MPART_TrueID2Index(3, PlayerInfo_GetAssemblyPartsId(0, nAc, 3));
    if (PlayerInfo_GetAssemblyPartsId(nPlayer, nAc, 4) != 0) return 1;
    if (DF_MPART_GetCategory(kind) == 0xf) return 1;
    if (DF_MPART_GetCategory(kind) == 0xd) return 1;
    PlayerInfo_GetAssemblyPartsId(0, nAc, 3);
    return 0;
}

int DF_M_AC_IsEquipColorChangableWep(stMenuAC *pAc)
{
    PS2_FRAME(0x20);
    PS2_STACK(int[6], idx, 0);   /* local copy of the rodata slot list (0x25fe40) */
    idx[0] = 0x10; idx[1] = 0x11; idx[2] = 0x12; idx[3] = 0x13; idx[4] = 0xe; idx[5] = 0xf;
    for (unsigned int i = 0; i < 6; i++)
        if (pAc->part_id[idx[i]] % 1000 != 0) return 1;
    return 0;
}

int DF_M_AC_AllCheckReading(int player)
{
    return M_ACp[player][0]->flag.reading | M_ACp[player][1]->flag.reading | M_ACp[player][2]->flag.reading;
}

void DF_M_AC_SetPlayerInfo(int inPlayer, int inNum)
{
    PS2_FRAME(0x40);
    if (!(inPlayer < 2 && inNum < 3)) return;
    stMenuAC *acp = mac_table(inPlayer, inNum);
    AcAssemblyPartsSt *wpp = PlayerInfo_GetAssemblyInfo(inPlayer, inNum);
    /* part_id[3], [4], [11], [13] are not stored */
    static const unsigned char kind[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 11, 12, 11, 12};
    static const unsigned char slot[16] = {0, 1, 2, 5, 6, 7, 8, 9, 10, 12, 14, 15, 16, 17, 18, 19};
    for (int k = 0; k < 16; k++)
        wpp->idParts[k] = DF_MPART_Index2TrueID(kind[k], acp->part_id[slot[k]]);
    wpp->option = acp->option.equip[0];   /* dead store (original) */
    wpp->option = 0;
    for (int b = 1; b < 0x20; b++) {
        if (!((acp->option.equip[0] >> (b - 1)) & 1)) continue;
        int a = DF_MPART_Index2TrueID(0xd, b) - 1;
        if (a >= 0 && a < 0x1f) wpp->option |= 1u << a;
    }
    wpp->use_slot = acp->option.use_slot;
    unsigned char *w = (unsigned char *)wpp;
    unsigned char *c = (unsigned char *)&acp->real_col;
    memcpy(w + 0x20, c, 0x50);   /* the five body colours (byte copies) */
    w[0xb0] = c[0x5f];
    w[0xc0] = c[0x6f];
    w[0x90] = c[0x9f];
    w[0xa0] = c[0xaf];
    w[0xd0] = c[0x7f];
    w[0xe0] = c[0x8f];
    memcpy(&wpp->tune, &acp->real_tune, 0x28);
    w[0x2f] = acp->camouflage[0];
    w[0x3f] = acp->camouflage[1];
    w[0x4f] = acp->camouflage[2];
    w[0x5f] = acp->camouflage[2];   /* camouflage[2] twice, [3] goes to the fifth block */
    w[0x6f] = acp->camouflage[3];
}

void DF_M_AC_PARTS_COLOR_STtoMENU_AC_COLOR_ST(struct_098975f3 *menucolor, StAcPartsColor *accolor)
{
    unsigned char *d = (unsigned char *)menucolor;
    const unsigned char *s = (const unsigned char *)accolor;
    memcpy(d, s, 0x50);
    d[0x5f] = s[0x90];
    d[0x6f] = s[0xa0];
    d[0x7f] = s[0xb0];
    d[0x8f] = s[0xc0];
    d[0x9f] = s[0x70];
    d[0xaf] = s[0x80];
}

void M_AC_UpdateTuneStat(stMenuAC *pAc, int bReal)
{
    PS2_FRAME(0x10);
    StAcAsmTune *tune = bReal ? &pAc->real_tune : &pAc->cur_tune;
    if (pAc->player >= 0 && pAc->player < 2)
        M_AC_GetTuneStatFromPlayerInfo(tune, pAc, pAc->player, 0, bReal);
}

/* the eight tuneable kinds and their part slots */
static const unsigned char mac_tune_slot[8] = {0, 1, 2, 5, 6, 7, 8, 9};

void M_AC_GetTuneStatFromPlayerInfo(StAcAsmTune *pTune, stMenuAC *acp, int nPlayer, int unused, int bReal)
{
    PS2_FRAME(0x60);
    (void)unused;
    _stMModel **parts = bReal ? acp->real_part : acp->cur_part;
    ZeroMemory(pTune, 0x28);
    for (int kind = 0; kind < 8; kind++) {
        _stMModel *mp = parts[mac_tune_slot[kind]];
        if (!mp) continue;
        int tid = DF_MPART_Index2TrueID(kind, mp->id % 1000);
        for (int j = 0; j < 5; j++)
            pTune->parts[kind].lv[j] = PlayerInfo_GetPartsTuneLv(nPlayer, kind, tid, j);
    }
}

void M_AC_GetTuneStatFromPlayerInfoByPartId(StAcAsmTune *pTune, stMenuAC *acp, int nPlayer)
{
    PS2_FRAME(0x60);
    ZeroMemory(pTune, 0x28);
    for (int kind = 0; kind < 8; kind++) {
        int id = acp->part_id[mac_tune_slot[kind]] % 1000;
        if (id == 0) continue;
        int tid = DF_MPART_Index2TrueID(kind, id);
        for (int j = 0; j < 5; j++)
            pTune->parts[kind].lv[j] = PlayerInfo_GetPartsTuneLv(nPlayer, kind, tid, j);
    }
}

void DF_M_AC_GetPlayerInfo(int inPlayer, int inNum)
{
    PS2_FRAME(0x80);
    if (!(inPlayer < 2 && inNum < 3)) return;
    stMenuAC *acp = mac_table(inPlayer, inNum);
    AcAssemblyPartsSt *wpp = PlayerInfo_GetAssemblyInfo(inPlayer, inNum);

    /* drop stored parts the loader no longer knows (the core falls back to the first one in the garage) */
    for (int k = 0; k < 0x10; k++) {
        int mp = MPart_AcpeqToMpart(k);
        if (wpp->idParts[k] == 0) continue;
        if (loader_IsValidParts(loader_ConvKindMenuToLoader(mp), wpp->idParts[k])) continue;
        if (k == 1) {
            wpp->idParts[k] = 1;
            for (int i = 0; i < 0x100; i++)
                if (PlayerInfo_IsExistPartsGarage(inPlayer, 1, i) != 0) {
                    wpp->idParts[k] = i;
                    break;
                }
        } else {
            wpp->idParts[k] = 0;
        }
        MenuMng.f_ne_parts_remove = -1;   /* bit 0 set */
    }
    for (int b = 1; b < 0x20; b++) {
        unsigned int m = 1u << (b - 1);
        if (!(wpp->option & m)) continue;
        if (loader_IsValidParts(0xd, b)) continue;
        wpp->option &= ~m;
        MenuMng.f_ne_parts_remove = -1;
    }

    acp->part_id[0] = DF_MPART_TrueID2Index(0, wpp->idParts[0]);
    acp->part_id[1] = DF_MPART_TrueID2Index(1, wpp->idParts[1]);
    acp->part_id[2] = DF_MPART_TrueID2Index(2, wpp->idParts[2]);
    acp->part_id[2] = DF_MPART_TrueID2Index(2, wpp->idParts[2]);   /* stored twice (original) */
    acp->part_id[5] = DF_MPART_TrueID2Index(3, wpp->idParts[3]);
    acp->part_id[6] = DF_MPART_TrueID2Index(4, wpp->idParts[4]);
    acp->part_id[7] = DF_MPART_TrueID2Index(5, wpp->idParts[5]);
    acp->part_id[8] = DF_MPART_TrueID2Index(6, wpp->idParts[6]);
    acp->part_id[9] = DF_MPART_TrueID2Index(7, wpp->idParts[7]);
    acp->part_id[10] = DF_MPART_TrueID2Index(8, wpp->idParts[8]);
    acp->part_id[11] = DF_MPART_TrueID2Index(8, wpp->idParts[8]);
    acp->part_id[12] = DF_MPART_TrueID2Index(9, wpp->idParts[9]);
    acp->part_id[13] = DF_MPART_TrueID2Index(9, wpp->idParts[9]);
    acp->part_id[14] = DF_MPART_TrueID2Index(10, wpp->idParts[10]);
    acp->part_id[15] = DF_MPART_TrueID2Index(10, wpp->idParts[11]);
    acp->part_id[16] = DF_MPART_TrueID2Index(11, wpp->idParts[12]);
    acp->part_id[17] = DF_MPART_TrueID2Index(12, wpp->idParts[13]);
    acp->part_id[18] = DF_MPART_TrueID2Index(11, wpp->idParts[14]);
    acp->part_id[19] = DF_MPART_TrueID2Index(12, wpp->idParts[15]);
    acp->part_id[4] = 15000;   /* the empty hand markers */
    acp->part_id[3] = 15000;

    acp->option.equip[0] = 0;
    for (int b = 1; b < 0x20; b++) {
        if (!((wpp->option >> (b - 1)) & 1)) continue;
        int v = (DF_MPART_TrueID2Index(0xd, b) - 1) % 1000;
        if (v >= 0 && v < 0x1f) acp->option.equip[0] |= 1u << v;
    }
    acp->option.use_slot = wpp->use_slot;
    unsigned char *w = (unsigned char *)wpp;
    unsigned char *c = (unsigned char *)&acp->real_col;
    memcpy(c, w + 0x20, 0x50);
    c[0x5f] = w[0xb0];
    c[0x6f] = w[0xc0];
    c[0x7f] = w[0xd0];
    c[0x8f] = w[0xe0];
    c[0x9f] = w[0x90];
    c[0xaf] = w[0xa0];
    acp->colp = &acp->real_col;
    M_AC_GetTuneStatFromPlayerInfoByPartId(&acp->cur_tune, acp, inPlayer);
    M_AC_GetTuneStatFromPlayerInfoByPartId(&acp->real_tune, acp, inPlayer);
    acp->camouflage[0] = w[0x2f];
    acp->camouflage[1] = w[0x3f];
    acp->camouflage[2] = w[0x4f];
    acp->camouflage[3] = w[0x6f];
}

/* ---- mission transfer (M_ACMissionTable -> g_nextstage.asmbly). M_AC_Trans is only ever written with 0 in the
 * whole ELF, so __TransMissionAC never runs; MissionTransStart is reached from no Versus path either. ---- */

/* true part id of menu id inId of kind inKind: byte 0x17 of the kind table entry, no bounds test (id % 1000 may
 * be negative: MW's signed div) */
static inline short mac_miss_tid(int inKind, int inId)
{
    return ((unsigned char *)MPartKindTblp[inKind] + 0x10 + (inId % 1000) * 0x20)[0x17];
}

/* the assembly record of mission slot num (NULL for num >= 4: the unguarded stores then go to PS2 address 0+) */
static inline unsigned char *mac_miss_dst(const struct_d8c9564a *amp)
{
    if (amp->num < 4) return (unsigned char *)&g_nextstage.asmbly[(int)amp->num];
    return 0;
}

/* colour block copy of body slot n (15 bytes + the camouflage byte; the arm slot copies two blocks) */
static inline void mac_miss_col(unsigned char *d, const struct_d8c9564a *amp, int n)
{
    const unsigned char *s = (const unsigned char *)amp;
    unsigned char *w = d ? d : (unsigned char *)PS2_NULL_MEM();
    int o = 0x20 + n * 0x10 + (n == 3 ? 0x10 : 0);
    for (int i = 0; i < 15; i++) w[o + i] = s[o + i];
    if (n == 2)
        for (int i = 0; i < 15; i++) w[0x50 + i] = s[0x50 + i];
    w[o + 15] = amp->camouflage[n];
    if (n == 2) w[0x5f] = amp->camouflage[2];
}

/* the kind table and assembly slot of each of the 16 mission ids */
static const unsigned char s_mac_miss_kind[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xa, 0xb, 0xc, 0xb, 0xc};

static void __SetMissionPartsImmediate(struct_d8c9564a *ioAmp)
{
    PS2_FRAME(0x30);
    PS2_STACK(char[12], f_tm2off, 0x20);   /* zero-filled, never read */
    for (int i = 0; i < 12; i++) f_tm2off[i] = 0;
    unsigned char *d = mac_miss_dst(ioAmp);
    /* code order: slots 0..3 with their colour blocks, then 4..9, 12..15, 10, 11 */
    static const unsigned char order[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xc, 0xd, 0xe, 0xf, 0xa, 0xb};
    for (int k = 0; k < 16; k++) {
        int n = order[k];
        short tid = mac_miss_tid(s_mac_miss_kind[n], ioAmp->part_id[n]);
        if (d) ((short *)d)[n] = tid;
        if (n < 4) mac_miss_col(d, ioAmp, n);
    }
    unsigned char *w = d ? d : (unsigned char *)PS2_NULL_MEM();
    const unsigned char *s = (const unsigned char *)ioAmp;
    for (int i = 0; i < 0x28; i++) w[0xf8 + i] = s[0xd4 + i];   /* tune */
    w[0xb0] = s[0x7f];
    w[0xc0] = s[0x8f];
    w[0x90] = s[0xbf];
    w[0xa0] = s[0xcf];
    w[0xd0] = s[0x9f];
    w[0xe0] = s[0xaf];
    if (d) ((AcAssemblyPartsSt *)d)->option = ioAmp->option;
    if (d) g_nextstage.price[(int)ioAmp->num] = Mpart_GetAssmblyPartsPrice(ioAmp->num, (AcAssemblyPartsSt *)d);
}

/* one step of the frame-by-frame transfer: converts mission id `step` (the read MMD's info data is fetched and
 * dropped); the f_tm2off local is filled per step and never read */
static void __SetMissionParts(struct_d8c9564a *ioAmp, stMmdData *inMmdp)
{
    PS2_FRAME(0x50);
    PS2_STACK(char[12], f_tm2off, 0x40);
    for (int i = 0; i < 12; i++) f_tm2off[i] = 0;
    unsigned char *d = mac_miss_dst(ioAmp);
    if (inMmdp) DF_MDATA_GetData(inMmdp->OTH.datap, 0);
    unsigned int n = (unsigned int)(int)ioAmp->step;
    if (n >= 0x10) return;
    short tid = mac_miss_tid(s_mac_miss_kind[n], ioAmp->part_id[n]);
    if (d) ((short *)d)[n] = tid;
    switch (n) {
    case 0: case 1: case 2: case 3:
        mac_miss_col(d, ioAmp, n);
        break;
    case 4: case 9: case 12: case 13: case 14: case 15:
        f_tm2off[0] = 1;
        for (int i = 0; i < 6; i++) {
            f_tm2off[2 * i] = 1;
            f_tm2off[2 * i + 1] = g_nDbgWepColor != i;
        }
        break;
    case 5: case 6: case 7: case 8:
        f_tm2off[0] = 1;
        break;
    case 10:
        f_tm2off[2] = 1;
        f_tm2off[1] = 1;
        f_tm2off[0] = 1;
        break;
    case 11:
        f_tm2off[3] = 1;
        f_tm2off[1] = 1;
        f_tm2off[0] = 1;
        break;
    }
}

/* per frame while M_AC_Trans == 1: converts the mission ACs id by id (loading each part's MMD first); stops at the
 * first record that is not finished this frame, clears M_AC_Trans when all five are done */
static void __TransMissionAC(void)
{
    PS2_FRAME(0x60);
    struct_d8c9564a *amp = M_ACMissionTable;
    int i;
    for (i = 0; i < 5; i++, amp++) {
        if (!amp->f_use || amp->f_end == 1) continue;
        AcAssemblyPartsSt *d = (AcAssemblyPartsSt *)mac_miss_dst(amp);
        while (amp->step < 0x10) {
            int id = amp->part_id[(int)amp->step];
            int go = 0;
            if (amp->count == 0) {
                if (id % 1000 == 0) {
                    __SetMissionParts(amp, 0);
                    amp->count = 2;
                } else if (Cdvd_CdSync() != 1) {
                    stMmdData *mmd = DF_MMD_GetMMD(1, id);
                    if (mmd) {
                        __SetMissionParts(amp, mmd);
                        amp->count = 2;
                    } else {
                        DF_MMD_ReadStart(DF_MMD_GetNewMMD(0), 1, id, 0x10000);
                        amp->count++;
                    }
                }
            } else if (amp->count == 1 || amp->count == 2) {
                if (amp->count == 1) {
                    int r = DF_MMD_ReadSync(0);
                    if (r == 0) break;
                    if (r == -1) {
                        DF_MENU_NotifyHddError(1);
                        MenuMng.stat = 0xc;
                        MenuMng.f_use_readbuff = 0;
                        M_AC_Trans = 0;
                        MenuMng.f_show_hdd_err = 0;   /* bits 3-7 of the flag byte */
                        return;
                    }
                    stMmdData *mmd = DF_MMD_GetMMD(1, id);
                    const stMmdData *m = mmd ? mmd : (const stMmdData *)PS2_NULL_MEM();
                    if (m->f_set == 1) __SetMissionParts(amp, mmd);
                    amp->count++;
                }
                /* count 2: next id; after the last one the record is stored and priced */
                amp->step++;
                if (amp->step >= 0x10) {
                    if (d) {
                        d->option = amp->option;
                        g_nextstage.price[(int)amp->num] = Mpart_GetAssmblyPartsPrice(amp->num, d);
                    }
                    amp->f_end = 1;
                }
                amp->count = 0;
                go = 1;
            }
            if (!go) break;
        }
        if (amp->f_end == 0) break;
    }
    if (i >= 5) M_AC_Trans = 0;
}

void DF_M_AC_MissionTransForceEnd(void)
{
    M_AC_Trans = 0;
}

int DF_M_AC_MissionTransSync(void)
{
    return M_AC_Trans != 1;
}

/* converts every used mission AC at once (no MMD reads) */
void DF_M_AC_MissionTransStart(void)
{
    PS2_FRAME(0x30);
    struct_d8c9564a *amp = M_ACMissionTable;
    for (int i = 0; i < 5; i++, amp++)
        if (amp->f_use) __SetMissionPartsImmediate(amp);
}

void DF_M_AC_SetMissAC(int inNum, stMenuAC *inACp)
{
    PS2_FRAME(0x60);
    if (M_AC_Trans == 1) return;
    if (inNum < 0 || inNum >= 6 || !inACp) return;
    struct_d8c9564a *amp = M_ACMissionTable + inNum;
    for (int i = 0; i < 0x10; i++)
        amp->part_id[i] = inACp->part_id[__EquipIDX2IDX(i)];
    amp->option = 0;
    for (int b = 1; b < 0x20; b++) {
        if (!((inACp->option.equip[0] >> (b - 1)) & 1)) continue;
        int a = DF_MPART_Index2TrueID(0xd, b) - 1;
        if (a >= 0 && a < 0x1f) amp->option |= 1u << a;
    }
    memcpy(&amp->col, &inACp->real_col, 0xb0);
    memcpy(&amp->tune, &inACp->real_tune, 0x28);
    amp->camouflage[0] = inACp->camouflage[0];
    amp->camouflage[1] = inACp->camouflage[1];
    amp->camouflage[2] = inACp->camouflage[2];
    amp->camouflage[3] = inACp->camouflage[3];
    amp->num = inNum;
    amp->f_use = 1;
    amp->f_end = 0;
    amp->step = 0;
    amp->count = 0;
}

/* equip index (the player record's 16 ids) -> stMenuAC slot */
static int __EquipIDX2IDX(int inIdx)
{
    static const unsigned char tbl[16] = {0, 1, 2, 5, 6, 7, 8, 9, 0xa, 0xc, 0xe, 0xf, 0x10, 0x11, 0x12, 0x13};
    return (unsigned int)inIdx < 16 ? tbl[inIdx] : 0;
}

int DF_M_AC_EquipIDX2IDX(int inIdx)
{
    return __EquipIDX2IDX(inIdx);
}

/* stMenuAC slot -> equip index (slots 3, 4 = hands, 11, 13 = second back / extension halves -> 0) */
int DF_M_AC_IDX2EquipIDX(int inIdx)
{
    static const unsigned char tbl[20] = {0, 1, 2, 0, 0, 3, 4, 5, 6, 7, 8, 0, 9, 0, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};
    return (unsigned int)inIdx < 20 ? tbl[inIdx] : 0;
}

/* menu part id (kind * 1000 + index) -> stMenuAC slot of its kind */
int DF_M_AC_PartID2IDX(int inID)
{
    static const unsigned char tbl[14] = {0, 1, 2, 5, 6, 7, 8, 9, 0xa, 0xc, 0xe, 0x10, 0x11, 0x14};
    unsigned int k = (unsigned int)(inID / 1000);
    return k < 14 ? tbl[k] : 0;
}

/* ---- group G6a: the part attach switch (__ConnectPart) and the MI2 node walkers ---- */

/* generic connect: the MI2 connect range of this part kind (static cprm, nobj clamped to the model's object count,
 * not count - 1), walk the MI2 tree from the AC matrix; nothing when GetConnectPrm fails or there is no MI2 */
static inline void mac_connect_objects(stMenuAC *acp, _stMModel *part, int kind, void *mi2p)
{
    if (DF_MPART_GetConnectPrm(kind, part, &__ConnectPart__cprm) != 1) return;
    if (!mi2p) return;
    struct_45c47ec5 *d = part->mdlp;
    if (d->nobj < (signed char)__ConnectPart__cprm.nobj) __ConnectPart__cprm.nobj = (char)d->nobj;
    __ConnectObject((_obj_motion *)((unsigned char *)mi2p + 0x10), &__ConnectPart__cprm, d->objp,
                    acp->local_world_mat);
    part->flag.calc_objmat = 1;
}

/* base anim of sub-models: MI2 block inNum of mmd's MI2 data (needs at least inMin blocks), node 1 of its motion
 * table onto each sub-model k of part (k from first to first + n - 1), parent = object 0 of the base model */
static inline void mac_base_anim(_stMModel *part, stMmdData *mmd, int inMin, int inNum,
                                 struct_45c47ec5 *base, int first, int n)
{
    if (mmd->MI2.num < inMin) return;
    unsigned char *a = (unsigned char *)DF_MDATA_GetData(mmd->MI2.datap, inNum);
    if (!a) return;
    for (int i = 0; i < n; i++)
        __ConnectObjectBaseAnim((_obj_motion *)(a + 0x10 + 0x40), part->mdlp[first + i].objp,
                                base->objp->local_world_mat);
    part->flag.calc_objmat = 1;
}

/* attach part to data polygon connect_dp_id of sub-model mdl of base: base object matrix, row 3 = the polygon
 * vertex (all four lanes, w as GetDataPoly left it); draw = 0 when the polygon is missing */
static inline void mac_attach_poly_dp(_stMModel *part, _stMModel *base, int mdl, int dp, float *v, float *w)
{
    int r = DF_MMODEL_GetDataPoly(base, mdl, dp, v, w);
    if (r >= 0) {
        sceVu0CopyMatrix(part->local_world_mat, base->mdlp[mdl].objp[r].local_world_mat);
        memcpy(part->local_world_mat[3], v, 16);
        part->flag.calc_lwmat = 1;
    } else {
        part->flag.draw = 0;
    }
}

static inline void mac_attach_poly(_stMModel *part, _stMModel *base, int mdl, float *v, float *w)
{
    mac_attach_poly_dp(part, base, mdl, part->prm.part.connect_dp_id, v, w);
}

/* attach part inPartIdx of the AC to its parent (per-slot switch, slot = the stMenuAC part index) */
static void __ConnectPart(stMenuAC *ioACp, _stMModel *ioPartp, int inPartIdx)
{
    PS2_FRAME(0x120);
    PS2_STACK(unsigned char, unused_b, 0x11c);
    unused_b = 0;   /* a never-read local */
    (void)unused_b;
    if (!ioPartp->flag.set_mdl) return;
    void *mi2p;
    int kind, weparm;
    if (ioACp->flag.tmp_connect == 1 && ioACp->connect.tmp.mi2p) {
        mi2p = ioACp->connect.tmp.mi2p;
        kind = ioACp->connect.tmp.kind;
        weparm = ioACp->connect.tmp.flag.weparm;
    } else {
        mi2p = ioACp->connect.cur.mi2p;
        kind = ioACp->connect.cur.kind;
        weparm = ioACp->connect.cur.flag.weparm;
    }
    if ((unsigned int)inPartIdx >= 0x14) return;
    _stMModel **cur = ioACp->cur_part;
    switch (inPartIdx) {
    case 0:
        ioPartp->flag.draw = cur[1] != 0;
        mac_connect_objects(ioACp, ioPartp, kind, mi2p);
        break;
    case 1:   /* core: then (even when the connect step failed) the base anim of its sub-models 1.. */
        ioPartp->flag.draw = cur[1] != 0;
        mac_connect_objects(ioACp, ioPartp, kind, mi2p);
        mac_base_anim(ioPartp, ioPartp->mmdp, 2, 1, ioPartp->mdlp, 1, ioPartp->mdl_num - 1);
        break;
    case 5:   /* legs: base anim only for categories 0xd / 0xf (GetCategory called again for the 0xf test) */
        ioPartp->flag.draw = cur[1] != 0;
        mac_connect_objects(ioACp, ioPartp, kind, mi2p);
        if (DF_MPART_GetCategory(cur[5]->id) != 0xd && DF_MPART_GetCategory(cur[5]->id) != 0xf) break;
        mac_base_anim(ioPartp, ioPartp->mmdp, 2, 1, ioPartp->mdlp, 1, ioPartp->mdl_num - 1);
        break;
    case 2:   /* arms */
        if (weparm == 1) {
            __ConnectWeaponArms(ioACp, ioPartp, (struct_e6319a72 *)mi2p, kind);
        } else {
            ioPartp->flag.draw = cur[1] != 0;
            mac_connect_objects(ioACp, ioPartp, kind, mi2p);
        }
        __ConnectInsideCover(ioPartp);
        break;
    case 6: {   /* every sub-model of the part on block 2 of the core's MI2 data */
        ioPartp->flag.draw = cur[1] != 0;
        _stMModel *core = cur[1];
        if (!core) break;
        mac_base_anim(ioPartp, core->mmdp, 3, 2, core->mdlp, 0, ioPartp->mdl_num);
        break;
    }
    case 3:
    case 4:   /* hands */
        if (!cur[1]) {
            ioPartp->flag.draw = 0;
            break;
        }
        __ConnectHand(inPartIdx, ioPartp, cur[2]);
        break;
    case 7:
    case 8:
    case 9: {   /* on a core data polygon, only while being previewed (connect.part[0]) or selected */
        PS2_STACK(float[4], v, 0xc0);
        PS2_STACK(float[4], w, 0xb0);
        if (ioACp->connect.part[0] == ioPartp || ioPartp->flag.select == 1) {
            ioPartp->flag.draw = 1;
            mac_attach_poly(ioPartp, cur[1], 0, v, w);
        } else {
            ioPartp->flag.draw = 0;
        }
        break;
    }
    case 10:
    case 11: {   /* on an arm data polygon; the test compares connect.part[0] with slot 10, not this part */
        PS2_STACK(float[4], v, 0xa0);
        PS2_STACK(float[4], w, 0x90);
        if (ioACp->connect.part[0] == cur[10] || ioPartp->flag.select == 1) {
            ioPartp->flag.draw = 1;
            mac_attach_poly(ioPartp, cur[2], 0, v, w);
        } else {
            ioPartp->flag.draw = 0;
        }
        break;
    }
    case 14:
    case 15: {   /* back weapons on the core; cannons tilted (-45 deg on leg categories 0xb / 0xd / 0xf, else -90) */
        PS2_STACK(float[4], v, 0x80);
        PS2_STACK(float[4], w, 0x70);
        PS2_STACK(float[4][4], rot, 0xd0);
        ioPartp->flag.draw = 1;
        mac_attach_poly(ioPartp, cur[1], 0, v, w);
        if (ioPartp->prm.part.f_cannon != 1) break;
        int cat = cur[5] ? DF_MPART_GetCategory(cur[5]->id) : 0;
        if (cat == 0xb || cat == 0xd || cat == 0xf)
            sceVu0RotMatrixX(rot, IdentityMtx, -0x1.921fb6p-1f);
        else
            sceVu0RotMatrixX(rot, IdentityMtx, -0x1.921fb6p+0f);
        sceVu0MulMatrix(ioPartp->local_world_mat, ioPartp->local_world_mat, rot);
        break;
    }
    case 12:
    case 13:
        __ConnectExtension(ioPartp, ioACp);
        break;
    case 16:
    case 17:
        __ConnectArmWep(ioPartp, ioACp);
        break;
    case 18:
    case 19:
        __ConnectSpareWep(ioPartp, ioACp);
        break;
    }
}

/* ---- group G6b: the attach helpers __ConnectPart calls ---- */

/* weapon arms (connect flag weparm): the two arm models hang off the core's object 0 matrix, each node matrix =
 * RotX(AngForm(-pi/2 + rot.x)) Trans(pos) of the first TOD key of MI2 node obj_id (+ 4 nodes for the second arm),
 * written to part objects 0 and 4. No calc_objmat (and no draw change past the core test) without an MI2 */
static void __ConnectWeaponArms(stMenuAC *ioACp, _stMModel *ioPartp, struct_e6319a72 *inAhp, int inKind)
{
    PS2_FRAME(0xe0);
    PS2_STACK(struct_9cdd2847, cprm, 0xdc);
    PS2_STACK(float[4][4], rot, 0x90);
    if (DF_MPART_GetConnectPrm(inKind, ioPartp, &cprm) != 1) return;
    if (!ioACp->cur_part[1]) {
        ioPartp->flag.draw = 0;
        return;
    }
    ioPartp->flag.draw = 1;
    float (*core)[4] = ioACp->cur_part[1]->mdlp->objp->local_world_mat;
    struct_45c47ec5 *d = ioPartp->mdlp;
    __mmodel_object_st *o = d->objp;
    if (!inAhp) return;
    if (d->nobj < (signed char)cprm.nobj) cprm.nobj = (char)d->nobj;
    _obj_motion *ent = (_obj_motion *)((unsigned char *)inAhp + 0x10) + (signed char)cprm.obj_id;
    for (int i = 0; i < 2; i++) {
        struct_3c2ab559 *t = ent->tod_data;
        sceVu0RotMatrixX(rot, IdentityMtx, DF_MFUNC_AngForm(-0x1.921fb6p+0f + t->angle[0]));
        sceVu0TransMatrix(rot, rot, t->position);
        vu_mul_matrix(o->local_world_mat, (const float (*)[4])core, (const float (*)[4])rot);
        ent += 4;
        o += 4;
    }
    ioPartp->flag.calc_objmat = 1;
}

/* the common early-outs of the hand-held weapon attaches: draw = 0; needs the arm and the core, the arm model
 * loaded and played and an arm that is not category 7; then draw = 1 */
static inline _stMModel *mac_armwep_arm(_stMModel *part, stMenuAC *acp)
{
    part->flag.draw = 0;
    _stMModel *arm = acp->cur_part[2];
    if (!arm || !acp->cur_part[1]) return 0;
    if (!arm->mdlp || !arm->flag.played) return 0;
    if (DF_MPART_GetCategory(arm->id) == 7) return 0;
    part->flag.draw = 1;
    return arm;
}

/* spare weapons (kind 11 = R on hand 3's sub-model 1 dp 10, kind 12 = L on hand 4's sub-model 0 dp 9), shifted by
 * a fixed offset; with the ranker motion only part->pos is set */
static void __ConnectSpareWep(_stMModel *ioMp, stMenuAC *inACp)
{
    PS2_FRAME(0x130);
    if (!ioMp) return;
    if (!mac_armwep_arm(ioMp, inACp)) return;
    if (inACp->cur_part[5]) DF_MPART_GetCategory(inACp->cur_part[5]->id);   /* result dropped */
    int k = ioMp->id / 1000;
    if (k == 11) {
        PS2_STACK(float[4], tv, 0x120);
        PS2_STACK(float[4], alt, 0xd0);
        PS2_STACK(float[4][4], t, 0xe0);
        PS2_STACK(float[4], v, 0x60);
        PS2_STACK(float[4], w, 0x50);
        static const float c_tv[4] = {-0x1.266666p+1f, 2.0f, -1.0f, 1.0f};      /* rodata 0x25fe00 */
        static const float c_alt[4] = {-0x1.19999ap+0f, -0x1.a66666p+1f, -4.0f, 1.0f};   /* 0x25fe10 */
        memcpy(tv, c_tv, 16);
        if (MenuMng.idRankerMotion) {
            memcpy(alt, c_alt, 16);
            memcpy(ioMp->pos, alt, 16);
            return;
        }
        ioMp->flag.draw = 1;
        mac_attach_poly_dp(ioMp, inACp->cur_part[3], 1, 0xa, v, w);
        sceVu0TransMatrix(t, IdentityMtx, tv);
        vu_mul_matrix(ioMp->local_world_mat, (const float (*)[4])ioMp->local_world_mat, (const float (*)[4])t);
    } else if (k == 12) {
        PS2_STACK(float[4], tv, 0xc0);
        PS2_STACK(float[4], alt, 0x70);
        PS2_STACK(float[4][4], t, 0x80);
        PS2_STACK(float[4], v, 0x40);
        PS2_STACK(float[4], w, 0x30);
        static const float c_tv[4] = {0x1.266666p+1f, 2.0f, -1.0f, 1.0f};       /* rodata 0x25fe20 */
        static const float c_alt[4] = {0x1.19999ap+0f, -0x1.a66666p+1f, -4.0f, 1.0f};    /* 0x25fe30 */
        memcpy(tv, c_tv, 16);
        if (MenuMng.idRankerMotion) {
            memcpy(alt, c_alt, 16);
            memcpy(ioMp->pos, alt, 16);
            return;
        }
        ioMp->flag.draw = 1;
        mac_attach_poly_dp(ioMp, inACp->cur_part[4], 0, 9, v, w);
        sceVu0TransMatrix(t, IdentityMtx, tv);
        vu_mul_matrix(ioMp->local_world_mat, (const float (*)[4])ioMp->local_world_mat, (const float (*)[4])t);
    }
}

/* hand-held arm weapons on data polygon connect_dp_id: 10 = hand 3 (sub-model 1), 9 = hand 4, 8 / 11 = the arm;
 * other dps keep draw = 1 without a matrix */
static void __ConnectArmWep(_stMModel *ioMp, stMenuAC *inACp)
{
    PS2_FRAME(0x90);
    if (!ioMp) return;
    if (!mac_armwep_arm(ioMp, inACp)) return;
    switch (ioMp->prm.part.connect_dp_id) {
    case 10: {
        PS2_STACK(float[4], v, 0x40);
        PS2_STACK(float[4], w, 0x30);
        ioMp->flag.draw = 1;
        mac_attach_poly(ioMp, inACp->cur_part[3], 1, v, w);
        break;
    }
    case 9: {
        PS2_STACK(float[4], v, 0x60);
        PS2_STACK(float[4], w, 0x50);
        ioMp->flag.draw = 1;
        mac_attach_poly(ioMp, inACp->cur_part[4], 0, v, w);
        break;
    }
    case 8:
    case 11: {
        PS2_STACK(float[4], v, 0x80);
        PS2_STACK(float[4], w, 0x70);
        ioMp->flag.draw = 1;
        mac_attach_poly(ioMp, inACp->cur_part[2], 0, v, w);
        break;
    }
    }
}

/* extensions: RotX(pi/2) under the AC matrix, row 3 = the arm's data polygon vertex or {0, 0, 7, 1} */
static void __ConnectExtension(_stMModel *ioExtp, stMenuAC *inACp)
{
    PS2_FRAME(0x60);
    PS2_STACK(float[4], def, 0x50);
    PS2_STACK(float[4], v, 0x40);
    PS2_STACK(float[4], w, 0x30);
    static const float c_def[4] = {0.0f, 0.0f, 7.0f, 1.0f};   /* rodata 0x25fdf0 */
    memcpy(def, c_def, 16);
    if (!ioExtp) return;
    _stMModel *arm = inACp->cur_part[2];
    if (!arm || !inACp->cur_part[1] || !arm->mdlp) {
        ioExtp->flag.draw = 0;
        return;
    }
    ioExtp->flag.draw = 1;
    sceVu0RotMatrixX(ioExtp->local_world_mat, IdentityMtx, DF_MFUNC_AngForm(0x1.921fb6p+0f));
    vu_mul_matrix(ioExtp->local_world_mat, (const float (*)[4])inACp->local_world_mat,
                  (const float (*)[4])ioExtp->local_world_mat);
    if (DF_MMODEL_GetDataPoly(arm, 0, ioExtp->prm.part.connect_dp_id, v, w) < 0) memcpy(v, def, 16);
    memcpy(ioExtp->local_world_mat[3], v, 16);
    ioExtp->flag.calc_lwmat = 1;
}

/* the arm's inside covers: sub-models 1 / 2 on arm data polygons 0x28 / 0x29 (object matrix of sub-model 0, row 3 =
 * the polygon vertex); each cover object = base * RotMatrix(ang) Trans(pos). No NULL test of the arm */
static void __ConnectInsideCover(_stMModel *inArmp)
{
    PS2_FRAME(0xf0);
    PS2_STACK(float[4], v, 0xa0);
    PS2_STACK(float[4], w, 0x90);
    PS2_STACK(float[4][4], base, 0xb0);   /* the original's v copy to sp+0xe0 is base row 3 (0xb0 + 0x30) */
    for (int s = 1; s < 3; s++) {
        struct_45c47ec5 *d = inArmp->mdlp + s;
        int r = DF_MMODEL_GetDataPoly(inArmp, 0, s == 1 ? 0x28 : 0x29, v, w);
        if (r < 0) continue;
        sceVu0CopyMatrix(base, inArmp->mdlp->objp[r].local_world_mat);
        memcpy(base[3], v, 16);
        for (int i = 0; i < d->nobj; i++) {   /* nobj re-read every iteration */
            __mmodel_object_st *o = d->objp + i;
            DF_MFUNC_RotMatrix(o->local_world_mat, IdentityMtx, o->ang);
            sceVu0TransMatrix(o->local_world_mat, o->local_world_mat, o->pos);
            vu_mul_matrix(o->local_world_mat, (const float (*)[4])base, (const float (*)[4])o->local_world_mat);
        }
    }
}

/* hands (slot 3 / 4): on a category 7 arm the arm's data polygon connect_dp_id (draw then follows the arm's draw);
 * else a copy of arm object 7 (hand 3, into the hand's sub-model 1) or object 3 (hand 4, sub-model 0) */
static void __ConnectHand(int inPartIdx, _stMModel *ioHandp, _stMModel *inArmp)
{
    PS2_FRAME(0x60);
    PS2_STACK(float[4], v, 0x50);
    PS2_STACK(float[4], w, 0x40);
    ioHandp->flag.draw = 1;
    if (!inArmp) {
        ioHandp->flag.draw = 0;
        return;
    }
    if (DF_MPART_GetCategory(inArmp->id) < 0) return;
    if (DF_MPART_GetCategory(inArmp->id) == 7) {
        ioHandp->flag.draw = 1;
        mac_attach_poly(ioHandp, inArmp, 0, v, w);
        ioHandp->flag.draw = inArmp->flag.draw;
        return;
    }
    if (inPartIdx == 3)
        sceVu0CopyMatrix(ioHandp->mdlp[1].objp->local_world_mat, inArmp->mdlp->objp[7].local_world_mat);
    else
        sceVu0CopyMatrix(ioHandp->mdlp->objp->local_world_mat, inArmp->mdlp->objp[3].local_world_mat);
    ioHandp->flag.calc_objmat = 1;
}

/* attach ioMp to data polygon connect_dp_id of sub-model inMdlNum of inParentMp (mobj's sword / weapon arms) */
void DF_M_AC_ConnectPartByID(_stMModel *ioMp, _stMModel *inParentMp, int inMdlNum)
{
    PS2_FRAME(0x60);
    PS2_STACK(float[4], v, 0x50);
    PS2_STACK(float[4], w, 0x40);
    ioMp->flag.draw = 1;
    mac_attach_poly(ioMp, inParentMp, inMdlNum, v, w);
}

/* node matrix = parent * (RotX(rot.x) RotZ(rot.z) RotY(rot.y) Trans) from the node's first TOD key (rot quad at
 * tod + 0x40, translation at tod + 0x10); the inline VU0 product as vu_mul_matrix */
static inline void mac_node_matrix(float (*m)[4], float *rot, float *trans, _obj_motion *inAtp,
                                   float (*inParentMat)[4])
{
    struct_3c2ab559 *t = inAtp->tod_data;
    memcpy(trans, t->position, 16);
    memcpy(rot, t->angle, 16);
    sceVu0RotMatrixX(m, IdentityMtx, DF_MFUNC_AngForm(rot[0]));
    sceVu0RotMatrixZ(m, m, DF_MFUNC_AngForm(rot[2]));
    sceVu0RotMatrixY(m, m, DF_MFUNC_AngForm(rot[1]));
    sceVu0TransMatrix(m, m, trans);
    vu_mul_matrix(m, (const float (*)[4])inParentMat, (const float (*)[4])m);
}

/* walk the MI2 tree: every node with a tp2id gets its matrix copied to that object; returns the node count. The
 * matrices are PS2_STACK: a child gets its parent's stack matrix, and DF_MMODEL_GetDataPoly's stack quad w (read
 * stale by the ConnectPart attach cases) overlaps m[3][3] of these frames */
static int __ConnectObjectBaseAnim(_obj_motion *inAtp, __mmodel_object_st *ioObjp, float (*inParentMat)[4])
{
    PS2_FRAME(0xc0);
    PS2_STACK(float[4], rot, 0x60);
    PS2_STACK(float[4], trans, 0x70);
    PS2_STACK(float[4][4], m, 0x80);
    int n = 1;
    mac_node_matrix(m, rot, trans, inAtp, inParentMat);
    unsigned int idx = inAtp->tp2id;
    if (idx != 0xffff) sceVu0CopyMatrix(ioObjp[idx].local_world_mat, m);
    if (inAtp->child) n += __ConnectObjectBaseAnim(inAtp->child, ioObjp, m);
    if (inAtp->sibling) n += __ConnectObjectBaseAnim(inAtp->sibling, ioObjp, inParentMat);
    return n;
}

/* same walk; nodes with objid in [obj_id, obj_id + nobj) go to object objid - obj_id (signed byte compares) */
static int __ConnectObject(_obj_motion *inAtp, struct_9cdd2847 *inCpp, __mmodel_object_st *ioObjp,
                           float (*inParentMat)[4])
{
    PS2_FRAME(0xd0);
    PS2_STACK(float[4], rot, 0x70);
    PS2_STACK(float[4], trans, 0x80);
    PS2_STACK(float[4][4], m, 0x90);
    int n = 1;
    mac_node_matrix(m, rot, trans, inAtp, inParentMat);
    int idx = inAtp->objid;
    int lo = (signed char)inCpp->obj_id;
    if (idx >= lo && idx < lo + (signed char)inCpp->nobj)
        sceVu0CopyMatrix(ioObjp[idx - lo].local_world_mat, m);
    if (inAtp->child) n += __ConnectObject(inAtp->child, inCpp, ioObjp, m);
    if (inAtp->sibling) n += __ConnectObject(inAtp->sibling, inCpp, ioObjp, inParentMat);
    return n;
}

/* ---- group G3: equip flags, hand models, per-slot part set-up, the draw table ---- */

/* the five spec bits DF_MACSPEC_Get reports (info @0x5c) -> the connect flag byte (cur or tmp) */
void DF_M_AC_CheckEquipFlag(stMenuAC *ioACp, int inCType)
{
    PS2_FRAME(0x50);
    struct_986e7f40 *flagp = inCType ? &ioACp->connect.tmp.flag : &ioACp->connect.cur.flag;
    if (ioACp->player >= 0 && ioACp->player < 2)
        M_AC_GetTuneStatFromPlayerInfo(&ioACp->cur_tune, ioACp, ioACp->player, 0, 0);
    if (ioACp->player >= 0 && ioACp->player < 2)
        M_AC_GetTuneStatFromPlayerInfo(&ioACp->real_tune, ioACp, ioACp->player, 0, 1);
    struct_dbbd570b *spec = (struct_dbbd570b *)DF_MMM_Alloc(2, 0x9c);   /* not NULL-checked */
    if (!ioACp->flag.reading)
        DF_MACSPEC_Get(spec, ioACp, inCType);
    else
        spec->info = 0;
    flagp->incomplete = (spec->info & 1) != 0;
    flagp->overweight = (spec->info & 2) != 0;
    flagp->core_ow = (spec->info & 4) != 0;
    flagp->ene_low = (spec->info & 8) != 0;
    flagp->no_core = (spec->info & 0x10) != 0;
    DF_MMM_Free(2, spec);
}

/* error mask of the previewed (tmp) part: 1 no arm, 2 arm cannot hold weapons (category 7), 4 slot already empty,
 * 8 leg type without this slot, 0x10 / 0x40 back-weapon pairing, 0x20 core takes no back weapon */
static void __CheckTmpEquipEnable(stMenuAC *ioACp)
{
    PS2_FRAME(0x50);
    unsigned int err = 0;
    int back = 0;
    if (!ioACp) return;
    if (ioACp->flag.tmp_connect) {
        if (ioACp->connect.tmp_part_id % 1000 == 0) {
            if (ioACp->part_id[(int)ioACp->connect.tmp_idx] % 1000 == 0) err |= 4;
        } else {
            switch (ioACp->connect.tmp_idx) {
            case 6:
                if (ioACp->part_id[5] % 1000 != 0) {
                    int cat = DF_MPART_GetCategory(ioACp->part_id[5]);
                    if (cat == 0xd || cat == 0xf) err |= 8;
                }
                break;
            case 0x12:
            case 0x13: {
                _stMModel *core = ioACp->cur_part[1];
                unsigned char *info = core ? (unsigned char *)core->prm.part.infop : 0;
                if (info && !(info[0x10d] & 4)) {
                    err |= 0x20;
                    break;
                }
                back = 1;
            }
                /* fall through */
            case 0x10:
            case 0x11: {
                int arm = ioACp->part_id[2];
                if (arm % 1000 == 0) {
                    err |= 1;
                    break;
                }
                DF_MPART_GetCategory(arm);   /* first result dropped (original) */
                if (DF_MPART_GetCategory(arm) == 7) {
                    err |= 2;
                    break;
                }
                if (back) {
                    int other = ioACp->part_id[ioACp->connect.tmp_idx == 0x12 ? 0x10 : 0x11];
                    if (other % 1000 == 0)
                        err |= 0x10;
                    else if (other == ioACp->connect.tmp_part_id)
                        err |= 0x40;
                }
                break;
            }
            case 0xa:
            case 0xb:
            case 0xc:
            case 0xd:
                if (ioACp->part_id[2] % 1000 == 0) err |= 1;
                break;
            default:
                break;
            }
        }
    }
    if (ioACp->player >= 0 && ioACp->player < 2)
        M_AC_GetTuneStatFromPlayerInfo(&ioACp->cur_tune, ioACp, ioACp->player, 0, 0);
    DF_M_AC_CheckEquipFlag(ioACp, 1);
    ioACp->connect.tmp_equip_err = err;
}

/* rebuild the two hand models of the arm in cur_part[2]: inKind 0 = the real AC (slots 3 / 4), else the tmp pair */
static void __SetHandModel(stMenuAC *ioACp, int inKind)
{
    PS2_FRAME(0x60);
    if (!ioACp->cur_part[2]) return;
    int cat = DF_MPART_GetCategory(ioACp->cur_part[2]->id);
    if (ioACp->cur_part[3]) {
        DF_MMODEL_DeleteModel(ioACp->cur_part[3]);
        ioACp->cur_part[3] = 0;
        if (!inKind) {
            ioACp->real_part[3] = 0;
            ioACp->part_id[3] = 15000;
        }
    }
    if (ioACp->cur_part[4]) {
        DF_MMODEL_DeleteModel(ioACp->cur_part[4]);
        ioACp->cur_part[4] = 0;
        if (!inKind) {
            ioACp->real_part[4] = 0;
            ioACp->part_id[4] = 15000;
        }
    }
    _stMModel *r, *l;
    if (DF_MPART_GetCategory(ioACp->cur_part[2]->id) != 7) {
        stMmdData *mmd = DF_MMD_GetMMD(1, 0x3a99 + cat / 2);
        if (!mmd) return;
        r = DF_MMODEL_GetNewModel(ioACp->cur_part[2]);
        if (!r) return;
        DF_MMODEL_SetData(r, mmd, 0, 0, &ioACp->scale);
        r->mdlp[0].f_draw = 0;
        l = DF_MMODEL_GetNewModel(ioACp->cur_part[2]);
        if (!l) return;   /* r stays created but unstored (original) */
        DF_MMODEL_SetData(l, mmd, 0, 0, &ioACp->scale);
        l->mdlp[1].f_draw = 0;
    } else {
        /* laser-type arm: the hands are sub-models 4 (R) / 3 (L) of the arm's own MMD */
        stMmdData *mmd = ioACp->cur_part[2]->mmdp;
        r = DF_MMODEL_GetNewModel(ioACp->cur_part[2]);
        if (!r) return;
        DF_MMODEL_SetData(r, mmd, 0, 0, &ioACp->scale);
        for (int i = 0; i < r->mdl_num; i++) r->mdlp[i].f_draw = (i == 4);
        r->prm.part.connect_dp_id = 0xa;
        l = DF_MMODEL_GetNewModel(ioACp->cur_part[2]);
        if (!l) return;
        DF_MMODEL_SetData(l, mmd, 0, 0, &ioACp->scale);
        for (int i = 0; i < l->mdl_num; i++) l->mdlp[i].f_draw = (i == 3);
        l->prm.part.connect_dp_id = 8;
    }
    unsigned char *col = (unsigned char *)ioACp->colp;
    DF_MMODEL_SetPartsCluts(r, (struct_c4910010 *)(col + 0x20), (struct_c4910010 *)(col + 0x20));
    col = (unsigned char *)ioACp->colp;
    DF_MMODEL_SetPartsCluts(l, (struct_c4910010 *)(col + 0x30), (struct_c4910010 *)(col + 0x30));
    if (!inKind) {
        ioACp->part_id[3] = (short)r->id;
        ioACp->part_id[4] = (short)l->id;
        ioACp->real_part[3] = r;
        ioACp->real_part[4] = l;
    } else {
        ioACp->connect.part[1] = r;
        ioACp->connect.part[2] = l;
    }
}

static inline struct_c4910010 *mac_col(stMenuAC *acp, int off)
{
    return (struct_c4910010 *)((unsigned char *)acp->colp + off);
}

/* back weapon slots 0xe (R) / 0xf (L); a W back weapon (both slots) also sets up its partner model, picked by
 * inPartType (0 real, 1 cur, 2 the tmp pair) */
static void __SetPartInfo_Back(stMenuAC *ioACp, _stMModel *ioPartp, int inIdx, int inCType, int inPartType)
{
    PS2_FRAME(0x80);
    int wback = DF_MMODEL_IsWBackWeapon(ioPartp);
    _stMModel *pair = 0;
    if (inIdx == 0xe) {
        if (ioPartp->id % 1000 != 0) {
            if (inPartType == 2) pair = ioACp->connect.part[1];
            else if (inPartType == 1) pair = ioACp->cur_part[15];
            else if (inPartType == 0) pair = ioACp->real_part[15];
            if (ioPartp->mdlp) {
                ioPartp->mdlp[0].f_draw = 0;
                ioPartp->f_tm2off[0] = 1;
                DF_MMODEL_SetShadow(ioPartp, 1);
            }
            ioPartp->prm.part.connect_dp_id = 2;
            ioPartp->prm.part.f_cannon = (char)DF_MMODEL_IsBackCannon(ioPartp);
            if (wback && pair && pair->id != ioPartp->id) {
                DF_MMODEL_ReleaseModel(pair);
                DF_MMODEL_SetData(pair, ioPartp->mmdp, &ioPartp->pos, &ioPartp->ang, &ioPartp->scale);
                __SetPartInfo_Back(ioACp, pair, 0xf, inCType, inPartType);
                DF_MMODEL_SetWepColor(pair, mac_col(ioACp, 0x90));
            }
        }
    } else {
        /* an empty left slot returns without updating the wback flag (the right slot's empty path updates it) */
        if (ioPartp->id % 1000 == 0) return;
        if (inPartType == 2) pair = ioACp->connect.part[1];
        else if (inPartType == 1) pair = ioACp->cur_part[14];
        else if (inPartType == 0) pair = ioACp->real_part[14];
        if (ioPartp->mdlp && ioPartp->mmdp->OMD.num >= 2) {
            ioPartp->mdlp[1].f_draw = 0;
            ioPartp->f_tm2off[1] = 1;
            DF_MMODEL_SetShadow(ioPartp, 0);
        }
        ioPartp->prm.part.connect_dp_id = 0;
        ioPartp->prm.part.f_cannon = (char)DF_MMODEL_IsBackCannon(ioPartp);
        if (wback && pair && pair->id != ioPartp->id) {
            DF_MMODEL_ReleaseModel(pair);
            DF_MMODEL_SetData(pair, ioPartp->mmdp, &ioPartp->pos, &ioPartp->ang, &ioPartp->scale);
            __SetPartInfo_Back(ioACp, pair, 0xe, inCType, inPartType);
            DF_MMODEL_SetWepColor(pair, mac_col(ioACp, 0x90));
        }
    }
    if (inCType == 0)
        ioACp->connect.cur.flag.wback = wback & 1;
    else
        ioACp->connect.tmp.flag.wback = wback & 1;
}

/* arm: weapon arms (category 7, sub-category < 0xb or >= 0x15) hide six objects and sub-models 3 / 4, other arms
 * hide objects 3 and 7 only */
static void __SetPartInfo_Arm(stMenuAC *ioACp, _stMModel *ioPartp, int inCType)
{
    PS2_FRAME(0x40);
    if (!ioPartp) return;
    if (DF_MPART_GetCategory(ioPartp->id) < 0) return;
    if (ioPartp->mdl_num == 0) return;
    int special = DF_MPART_GetCategory(ioPartp->id) == 7;
    if (special && DF_MPART_GetSubCategory(ioPartp->id) >= 0xb)
        special = !(DF_MPART_GetSubCategory(ioPartp->id) < 0x15);
    if (special) {
        ioPartp->mdlp->objp[1].f_draw = 0;
        ioPartp->mdlp->objp[2].f_draw = 0;
        ioPartp->mdlp->objp[3].f_draw = 0;
        ioPartp->mdlp->objp[5].f_draw = 0;
        ioPartp->mdlp->objp[6].f_draw = 0;
        ioPartp->mdlp->objp[7].f_draw = 0;
        ioPartp->mdlp[3].f_draw = 0;
        ioPartp->mdlp[4].f_draw = 0;
    } else {
        ioPartp->mdlp->objp[3].f_draw = 0;
        ioPartp->mdlp->objp[7].f_draw = 0;
    }
    if (inCType == 0)
        ioACp->connect.cur.flag.weparm = special;
    else
        ioACp->connect.tmp.flag.weparm = special;
}

/* per-slot set-up of a freshly loaded part model: camouflage, cluts / weapon colours, shadows, connect dp id */
static int __SetPartInfo(stMenuAC *ioACp, _stMModel *ioPartp, int inIdx, int inCType, int inPartType)
{
    PS2_FRAME(0x50);
    if (!ioPartp) return 0;
    int weparm = inCType ? ioACp->connect.tmp.flag.weparm : ioACp->connect.cur.flag.weparm;
    switch ((unsigned int)inIdx) {
    case 0:   /* head */
        DF_M_AC_SetCamoPettern(ioACp, ioPartp, 0);
        DF_MMODEL_SetPartsCluts(ioPartp, mac_col(ioACp, 0), 0);
        if (!ioACp->flag.shadow_off) DF_MMODEL_SetShadow(ioPartp, 0);
        break;
    case 1:   /* core */
        DF_M_AC_SetCamoPettern(ioACp, ioPartp, 1);
        DF_MMODEL_SetPartsCluts(ioPartp, mac_col(ioACp, 0x10), 0);
        if (!ioACp->flag.shadow_off) {
            DF_MMODEL_SetShadow(ioPartp, 0);
            DF_MMODEL_SetShadow(ioPartp, 1);
        }
        break;
    case 2:   /* arm */
        DF_M_AC_SetCamoPettern(ioACp, ioPartp, 2);
        DF_MMODEL_SetPartsCluts(ioPartp, mac_col(ioACp, 0x30), mac_col(ioACp, 0x20));
        if (!ioACp->flag.shadow_off) DF_MMODEL_SetShadow(ioPartp, 0);
        __SetPartInfo_Arm(ioACp, ioPartp, inCType);
        break;
    case 3:   /* right hand */
        if (weparm == 1)
            DF_MMODEL_SetPartsCluts(ioPartp, mac_col(ioACp, 0x30), mac_col(ioACp, 0x20));
        else
            DF_MMODEL_SetPartsCluts(ioPartp, mac_col(ioACp, 0x20), mac_col(ioACp, 0x20));
        if (!ioACp->flag.shadow_off) DF_MMODEL_SetShadow(ioPartp, weparm == 1 ? 4 : 1);
        ioPartp->prm.part.connect_dp_id = 0xa;
        break;
    case 4:   /* left hand */
        if (weparm == 1)
            DF_MMODEL_SetPartsCluts(ioPartp, mac_col(ioACp, 0x30), mac_col(ioACp, 0x20));
        else
            DF_MMODEL_SetPartsCluts(ioPartp, mac_col(ioACp, 0x30), mac_col(ioACp, 0x30));
        if (!ioACp->flag.shadow_off) DF_MMODEL_SetShadow(ioPartp, weparm == 1 ? 3 : 0);
        ioPartp->prm.part.connect_dp_id = 8;
        break;
    case 5:   /* legs */
        DF_M_AC_SetCamoPettern(ioACp, ioPartp, 3);
        DF_MMODEL_SetPartsCluts(ioPartp, mac_col(ioACp, 0x40), 0);
        if (!ioACp->flag.shadow_off) {
            DF_MMODEL_SetShadow(ioPartp, 0);
            DF_MMODEL_SetShadow(ioPartp, 1);
        }
        break;
    case 6:
        break;
    case 7:
    case 8:
    case 9:
        ioPartp->prm.part.connect_dp_id = 0x50;
        if (ioPartp->mdlp) ioPartp->mdlp[1].f_draw = 0;
        ioPartp->flag.draw = 0;
        break;
    case 10:
        ioPartp->prm.part.connect_dp_id = 0x52;
        if (ioPartp->mdlp) ioPartp->mdlp[1].f_draw = 0;
        break;
    case 11:
        ioPartp->prm.part.connect_dp_id = 0x51;
        if (ioPartp->mdlp) ioPartp->mdlp[1].f_draw = 0;
        break;
    case 12:
        if (ioPartp->mdlp) ioPartp->mdlp[0].f_draw = 0;
        ioPartp->prm.part.connect_dp_id = 6;
        if (!ioACp->flag.shadow_off) DF_MMODEL_SetShadow(ioPartp, 1);
        break;
    case 13:
        if (ioPartp->mdlp && ioPartp->mmdp->OMD.num >= 2) ioPartp->mdlp[1].f_draw = 0;
        ioPartp->prm.part.connect_dp_id = 4;
        if (!ioACp->flag.shadow_off) DF_MMODEL_SetShadow(ioPartp, 0);
        break;
    case 14:
        __SetPartInfo_Back(ioACp, ioPartp, inIdx, inCType, inPartType);
        DF_MMODEL_SetWepColor(ioPartp, mac_col(ioACp, 0x90));
        break;
    case 15:
        __SetPartInfo_Back(ioACp, ioPartp, inIdx, inCType, inPartType);
        if (DF_MMODEL_IsWBackWeapon(ioPartp))
            DF_MMODEL_SetWepColor(ioPartp, mac_col(ioACp, 0x90));
        else
            DF_MMODEL_SetWepColor(ioPartp, mac_col(ioACp, 0xa0));
        break;
    case 16: {   /* arm weapon R */
        unsigned char *info = (unsigned char *)ioPartp->prm.part.infop;
        ioPartp->prm.part.connect_dp_id = info ? info[0x101] : 0xa;
        DF_MMODEL_SetWepColor(ioPartp, mac_col(ioACp, 0x50));
        if (!ioACp->flag.shadow_off) DF_MMODEL_SetShadow(ioPartp, 0);
        break;
    }
    case 17: {   /* arm weapon L */
        unsigned char *info = (unsigned char *)ioPartp->prm.part.infop;
        ioPartp->prm.part.connect_dp_id = info ? info[0x101] : 8;
        DF_MMODEL_SetWepColor(ioPartp, mac_col(ioACp, 0x60));
        if (!ioACp->flag.shadow_off) DF_MMODEL_SetShadow(ioPartp, 0);
        break;
    }
    case 18:
        DF_MMODEL_SetWepColor(ioPartp, mac_col(ioACp, 0x70));
        break;
    case 19:
        DF_MMODEL_SetWepColor(ioPartp, mac_col(ioACp, 0x80));
        break;
    default:
        break;
    }
    return 1;
}

/* select highlight of the previewed slot (and the parts shown with it) */
static void __OneAC_SetTempPartsAttr(stMenuAC *ioACp)
{
    PS2_FRAME(0x40);
    int idx = ioACp->connect.tmp_idx;
    _stMModel **cur = ioACp->cur_part;
    switch (idx) {
    case 0xf:
        DF_MMODEL_Select(cur[idx], 1, 1);
        if (ioACp->connect.tmp.flag.wback == 1) DF_MMODEL_Select(cur[idx - 1], 1, 1);
        break;
    case 0xe:
        DF_MMODEL_Select(cur[idx], 1, 1);
        if (ioACp->connect.tmp.flag.wback == 1) DF_MMODEL_Select(cur[idx + 1], 1, 1);
        break;
    case 0xa:
        DF_MMODEL_Select(cur[2], 1, 3);
        DF_MMODEL_Select(cur[3], 1, 3);
        DF_MMODEL_Select(cur[4], 1, 3);
        for (int k = 0; k < 2; k++)
            if (cur[idx + k]) DF_MMODEL_Select(cur[idx + k], 0, 2);
        break;
    case 9:
    case 8:
    case 7:
        DF_MMODEL_Select(cur[1], 1, 3);
        if (cur[idx]) DF_MMODEL_Select(cur[idx], 0, 2);
        break;
    case 0xc:
        DF_MMODEL_Select(cur[idx], 1, 1);
        DF_MMODEL_Select(cur[idx + 1], 1, 1);
        break;
    case 2:
        DF_MMODEL_Select(cur[idx], 1, 1);
        DF_MMODEL_Select(cur[idx + 1], 1, 1);
        DF_MMODEL_Select(cur[idx + 2], 1, 1);
        break;
    default:
        DF_MMODEL_Select(cur[idx], 1, 1);
        break;
    }
}

/* draw table = real_part with the previewed (tmp) part and its paired models patched in */
static void __OneAC_SetPartsTable(stMenuAC *inACp, _stMModel **ioPartp)
{
    for (int i = 0; i < 21; i++) ioPartp[i] = inACp->real_part[i];
    if (inACp->flag.tmp_connect != 1) return;
    _stMModel *tp = inACp->connect.part[0];
    int idx = inACp->connect.tmp_idx;
    if (tp)
        ioPartp[idx] = tp->flag.set_mdl == 1 ? tp : 0;
    else if (inACp->connect.tmp_part_id % 1000 == 0)
        ioPartp[idx] = 0;
    switch (idx) {
    case 0xf:
        if (inACp->connect.cur.flag.wback == 1) ioPartp[idx - 1] = 0;
        if (inACp->connect.tmp.flag.wback == 1)
            ioPartp[idx - 1] = ioPartp[idx] ? inACp->connect.part[1] : 0;
        break;
    case 0xe:
        if (inACp->connect.cur.flag.wback == 1) ioPartp[idx + 1] = 0;
        if (inACp->connect.tmp.flag.wback == 1)
            ioPartp[idx + 1] = ioPartp[idx] ? inACp->connect.part[1] : 0;
        break;
    case 0xa:
    case 0xc:
        ioPartp[idx + 1] = inACp->connect.part[1];
        break;
    case 2:
        ioPartp[idx + 1] = inACp->connect.part[1];
        ioPartp[idx + 2] = inACp->connect.part[2];
        break;
    default:
        break;
    }
}

/* ---- rest part B: the "dirt" texture pass (gMManageCtr.fAddDirt, 0 in every replay: never runs) ---- */

/* TEX0 TW / TH of a cached TM2 header (struct_13efd7f4 at +0x20: TW = bits 26..29, TH = bits 30..33) */
static inline int mac_tm2_tw(const unsigned char *h)
{
    return (int)((*(const unsigned long long *)(h + 0x20) >> 26) & 0xf);
}
static inline int mac_tm2_th(const unsigned char *h)
{
    return (int)((*(const unsigned long long *)(h + 0x20) >> 30) & 0xf);
}

/* dirt pass of one part's texture sub_stat[idx] (idx 0 / 1 / 2 / 5 have 1 / 1 / 4 / 2 textures, other slots are
 * done at once): 0 cache the 8-bit texture + CLUT (pAcTm2P, shared per AC slot; mode 1 = the tmp part's row 3) and
 * expand it to 32 bits in MenuWorkp, 1 overlay the dirt image, 2 set the colour reduction up, 3 run it until done
 * (odd textures re-index against the previous texture's CLUT) and restore MenuWorkp. Returns part_stat[idx] (100 =
 * every texture of the part done) */
static int __SetPartTM2(stMenuAC *ioACp, _stMModel *ioPartp, int inIdx, int mode)
{
    PS2_FRAME(0xc0);
    PS2_STACK(tag_REDUCE_DEF_ST, def, 0xa0);
    if (!ioPartp) return 0;
    int tab = mode == 1 ? 3 : ioACp->num;
    int sub = ioACp->sub_stat[inIdx];
    char *subp = &ioACp->sub_stat[inIdx];
    int row, cnt;
    switch (inIdx) {
    case 0: row = 0; cnt = 1; break;
    case 1: row = 1; cnt = 1; break;
    case 2: row = 2; cnt = 4; break;
    case 5: row = 3; cnt = 2; break;
    default:
        ioACp->part_stat[inIdx] = 100;
        return 100;
    }
    char *ps = &ioACp->part_stat[inIdx];
    switch (*ps) {
    case 0: {
        unsigned char **ip = &ioACp->indexp[row][sub];
        if (!*ip) {
            unsigned char **cache = &pAcTm2P[tab][row][sub];
            if (*cache) {
                *ip = *cache;
            } else {
                pAcTm2Buff = (unsigned char *)DF_MFUNC_Aligned(pAcTm2Buff, 0x10);
                unsigned char *h = pAcTm2Buff;
                pAcTm2Buff = h + 0x40;
                *cache = h;
                ioACp->indexp[row][sub] = h;
                stMmdTm2Data *d = (stMmdTm2Data *)ioACp->indexp[row][sub];
                stMmdTm2Data *src = ioPartp->mmdp->TM2.tm2datap + sub;
                memcpy(d, src, 0x40);
                unsigned int sz = d->tex_img.size;
                pAcTm2Buff = (unsigned char *)DF_MFUNC_Aligned(pAcTm2Buff, 0x10);
                unsigned char *b = pAcTm2Buff;
                pAcTm2Buff = b + sz;
                d->tex_img.datap = (unsigned int *)b;
                d->tex_img.imagep = (unsigned int *)((unsigned char *)d->tex_img.datap + 0x80);
                memcpy(d->tex_img.datap, src->tex_img.datap, src->tex_img.size);
                sz = d->clut_img.size;
                pAcTm2Buff = (unsigned char *)DF_MFUNC_Aligned(pAcTm2Buff, 0x10);
                b = pAcTm2Buff;
                pAcTm2Buff = b + sz;
                d->clut_img.datap = (unsigned int *)b;
                d->clut_img.imagep = (unsigned int *)((unsigned char *)d->clut_img.datap + 0x80);
                memcpy(d->clut_img.datap, src->clut_img.datap, src->clut_img.size);
            }
        }
        stMmdTm2Data *t = ioPartp->mmdp->TM2.tm2datap + sub;
        unsigned int *tbp = t->tex_img.imagep;
        unsigned char *h = *ip;
        unsigned int *cbp = ioPartp->prm.part.clutp ? ioPartp->prm.part.clutp[sub].imagep : t->clut_img.imagep;
        int pixels = 1 << (mac_tm2_th(h) + mac_tm2_tw(h));
        ioACp->def_workp = (char *)MenuWorkp;
        ioACp->tmp_tm2p = (char *)DF_MFUNC_Aligned(MenuWorkp, 0x40);
        MenuWorkp = (unsigned char *)ioACp->tmp_tm2p + pixels * 4;
        LAYER_Convert8to32((unsigned char *)ioACp->tmp_tm2p, (struct_13efd7f4 *)(h + 0x20), (unsigned char *)tbp,
                           (unsigned char *)cbp, 0);
        (*ps)++;
        break;
    }
    case 1: {
        unsigned char *h = ioACp->indexp[row][sub];
        int pixels = 1 << (mac_tm2_th(h) + mac_tm2_tw(h));
        /* the 32-bit copy is not reserved: MenuWorkp stays where it is */
        unsigned char *bak = (unsigned char *)DF_MFUNC_Aligned(MenuWorkp, 0x40);
        memcpy(bak, ioACp->tmp_tm2p, pixels * 4);
        unsigned char *lc = (unsigned char *)DF_MDATA_GetData(MenuMng.tm2Tbl[3], 1);
        LAYER_OverLay((unsigned char *)ioACp->tmp_tm2p, bak, lc + 0x20, mac_tm2_tw(h), mac_tm2_th(h), 0);
        (*ps)++;
        break;
    }
    case 2: {
        unsigned char **r = ioACp->indexp[row];
        unsigned char *h = r[sub];
        int pixels = 1 << (mac_tm2_th(h) + mac_tm2_tw(h));
        memset(&def, 0, 0x20);
        def.img32 = ioACp->tmp_tm2p;
        def.pixels = pixels;
        def.img8 = *(void **)(h + 8);
        def.clut = *(void **)(h + 0x18);
        def.type = 0;
        def.work = MenuWorkp;
        def.wksize = 0;
        def.clut_num = 0x100;
        if (sub % 2 != 0) {
            def.clut = *(void **)(r[*subp - 1] + 0x18);
            memcpy(*(void **)(h + 0x18), def.clut, 0x400);
        }
        REDUCE_Init(&def);
        def.wksize = REDUCE_GetWorkSize(&def);
        MenuWorkp += def.wksize;
        (*ps)++;
        break;
    }
    case 3: {
        int res = sub % 2 != 0 ? REDUCE_MakeIndexProc() : REDUCE_Proc(0);
        if (res == 1) return *ps;
        (*subp)++;
        if (*subp < cnt) {
            *ps = 0;
        } else {
            *subp = 0;
            *ps = 100;
        }
        if (ioACp->def_workp) {
            MenuWorkp = (unsigned char *)ioACp->def_workp;
            ioACp->def_workp = 0;
        }
        break;
    }
    default:
        break;
    }
    return *ps;
}

/* read chain step 3 (dirt pass only): one texture step of the first unfinished loaded part per frame (0); with every
 * part done, or with no dirt pass, the AC is set (1). A part not loaded yet also ends the frame (0) */
static int __OneAC_ReadAll_SetAllTM2(stMenuAC *ioACp)
{
    PS2_FRAME(0x10);
    if (gMManageCtr.fAddDirt) {
        for (int i = 0; i < 0x15; i++) {
            _stMModel *p = ioACp->real_part[i];
            if (!p) {
                if (ioACp->part_id[i] % 1000 != 0) return 0;
                continue;
            }
            if (ioACp->part_stat[i] != 100) {
                __SetPartTM2(ioACp, p, i, 0);
                return 0;
            }
        }
    }
    ioACp->flag.set_tm2 = 1;
    ioACp->flag.reading = 0;
    ioACp->read_func = 0;
    return 1;
}

/* ---- group G4: read state machines, play, select ---- */

static inline void mac_show_tmp(_stMModel *p)
{
    p->flag.draw = 1;
    p->mdlp[1].f_draw = 0;
}

/* read_func of a previewed (tmp) part (installed by DF_M_AC_SetTmpPart): 0 read its connect MI2 (or reuse the
 * current one), 1 copy it out of the read buffer, 2 set the model(s) up, 3 dirt textures; returns 1 on every path */
static int __OneAC_ReadTmpPart(stMenuAC *ioACp)
{
    PS2_FRAME(0x50);
    int idx = ioACp->connect.tmp_idx;
    stMmdData *mmd = DF_MMD_GetMMD(1, ioACp->connect.tmp_part_id);
    _stMModel *part = ioACp->connect.part[0];
    int mi2id = ioACp->connect.tmp.mi2id;
    switch (ioACp->read_cc) {
    case 0: {
        if (!mmd) break;
        if (MenuMng.idRankerMotion == 0 && mi2id == ioACp->connect.cur.mi2id) {
            ioACp->connect.tmp.kind = ioACp->connect.cur.kind;
            ioACp->connect.tmp.mi2p = ioACp->connect.cur.mi2p;
            ioACp->read_cc = 2;
            break;
        }
        int lsn = MenuMng.idRankerMotion ? (signed char)MenuMng.idRankerMotion + 0x1fa3 : mi2id + 0x1f3f;
        if (Cdvd_ReadLsnBlind(lsn, g_pbufRead, 0, 0) == 0) {
            ioACp->read_cc++;
        } else {
            /* read refused: fall back to the current connect MI2 */
            ioACp->connect.tmp.kind = ioACp->connect.cur.kind;
            ioACp->connect.tmp.mi2p = ioACp->connect.cur.mi2p;
            ioACp->read_cc = 2;
            MenuMng.f_use_readbuff = 0;
        }
        break;
    }
    case 1: {
        if (Cdvd_CdSync() == 1) break;
        int n = *(int *)g_pbufRead;
        ioACp->connect.tmp.mi2p = DF_MMM_Alloc(0, n);
        DF_MFUNC_CopyMemory(ioACp->connect.tmp.mi2p, g_pbufRead, n >> 2);
        DF_MFUNC_RemapMI2(ioACp->connect.tmp.mi2p);
        MenuMng.f_use_readbuff = 0;
        ioACp->read_cc++;
        break;
    }
    case 2:
        if (!mmd->f_set) break;
        DF_MMODEL_SetData(part, mmd, &ioACp->pos, &ioACp->ang, &ioACp->scale);
        if (DF_MMODEL_IsWBackWeapon(part) == 1)
            ioACp->connect.part[1] = DF_MMODEL_GetNewModel(ioACp->connect.part[0]);
        __SetPartInfo(ioACp, part, idx, 1, 2);
        ioACp->flag.tmp_connect = 1;
        __OneAC_SetPartsTable(ioACp, ioACp->cur_part);
        switch (idx) {
        case 9:
        case 8:
        case 7:
            if (part->mdlp) mac_show_tmp(part);
            break;
        case 0xc:
            DF_MMODEL_SetData(ioACp->connect.part[1], mmd, &ioACp->pos, &ioACp->ang, &ioACp->scale);
            __SetPartInfo(ioACp, ioACp->connect.part[1], idx + 1, 1, 2);
            break;
        case 0xa:
            DF_MMODEL_SetData(ioACp->connect.part[1], mmd, &ioACp->pos, &ioACp->ang, &ioACp->scale);
            __SetPartInfo(ioACp, ioACp->connect.part[1], idx + 1, 1, 2);
            if (part->mdlp) mac_show_tmp(part);
            if (ioACp->connect.part[1]->mdlp) mac_show_tmp(ioACp->connect.part[1]);
            break;
        case 2:
            __SetHandModel(ioACp, 1);
            __SetPartInfo(ioACp, ioACp->connect.part[1], idx + 1, 1, 2);
            __SetPartInfo(ioACp, ioACp->connect.part[2], idx + 2, 1, 2);
            break;
        default:
            break;
        }
        if (gMManageCtr.fAddDirt == 0) {
            ioACp->flag.reading = 0;
            ioACp->read_func = 0;
        }
        __CheckTmpEquipEnable(ioACp);
        ioACp->part_stat[idx] = 0;
        ioACp->read_cc++;
        break;
    case 3:
        if (gMManageCtr.fAddDirt == 0) break;
        if (ioACp->flag.set_tm2 == 1 || __SetPartTM2(ioACp, part, idx, 1) == 100) {
            ioACp->flag.reading = 0;
            ioACp->flag.set_tm2 = 1;
            ioACp->read_func = 0;
        }
        break;
    default:
        break;
    }
    return 1;
}

/* read chain step 2: once the MMD reads are done, set every loaded part up; returns 1 only on the dirt path */
static int __OneAC_ReadAll_SetAllParts(stMenuAC *ioACp)
{
    PS2_FRAME(0x60);
    if (MenuMng.stat == 0xc) return 0;
    int r = DF_MMD_ReadSync(0);
    if (gMManageCtr.fAddDirt == 0 && r != 0) {
        ioACp->flag.reading = 0;
        ioACp->read_func = 0;
    }
    if (r == -1) {
        DF_MENU_NotifyHddError(0);
        MenuMng.f_use_readbuff = 0;
        MenuMng.f_show_hdd_err = 0;   /* bits 3-7 of the flag byte */
        M_AC_Trans = 0;
        return 0;
    }
    for (int i = 0; i < 21; i++) {
        _stMModel *p = ioACp->real_part[i];
        if (!p || !p->mmdp->datap || p->flag.set_mdl) continue;
        DF_MMODEL_SetData(p, p->mmdp, &ioACp->pos, &ioACp->ang, &ioACp->scale);
        __SetPartInfo(ioACp, p, i, 0, 0);
        if (i == 2) {
            __SetHandModel(ioACp, 0);
            __SetPartInfo(ioACp, ioACp->real_part[3], i + 1, 0, 0);
            __SetPartInfo(ioACp, ioACp->real_part[4], i + 2, 0, 0);
        }
        ioACp->part_stat[i] = 0;
    }
    if (gMManageCtr.fAddDirt) {
        if (r == 0) return 0;
        ioACp->read_cc++;
        ioACp->read_func = (int (*)(void))GFN(__OneAC_ReadAll);
        if (ioACp->player >= 0 && ioACp->player < 2)
            M_AC_GetTuneStatFromPlayerInfo(&ioACp->real_tune, ioACp, ioACp->player, 0, 1);
        if (ioACp->player >= 0 && ioACp->player < 2)
            M_AC_GetTuneStatFromPlayerInfo(&ioACp->cur_tune, ioACp, ioACp->player, 0, 0);
        DF_M_AC_CheckEquipFlag(ioACp, 0);
        return 1;
    }
    if (!ioACp->flag.reading) {
        if (ioACp->player >= 0 && ioACp->player < 2)
            M_AC_GetTuneStatFromPlayerInfo(&ioACp->real_tune, ioACp, ioACp->player, 0, 1);
        if (ioACp->player >= 0 && ioACp->player < 2)
            M_AC_GetTuneStatFromPlayerInfo(&ioACp->cur_tune, ioACp, ioACp->player, 0, 0);
        DF_M_AC_CheckEquipFlag(ioACp, 0);
    }
    return 0;
}

/* read chain step 1: take the connect MI2 out of the read buffer, start the MMD read of every unloaded part */
static int __OneAC_ReadAll_ReadAllParts(stMenuAC *ioACp)
{
    PS2_FRAME(0x40);
    if (Cdvd_CdSync() == 1) return 0;
    if (!ioACp->flag.set_mi2) {
        int n = *(int *)g_pbufRead;
        if (ioACp->connect.cur.mi2p) DF_MMM_Free(0, ioACp->connect.cur.mi2p);
        ioACp->connect.cur.mi2p = DF_MMM_Alloc(0, n);
        DF_MFUNC_CopyMemory(ioACp->connect.cur.mi2p, g_pbufRead, n >> 2);
        DF_MFUNC_RemapMI2(ioACp->connect.cur.mi2p);
        ioACp->flag.set_mi2 = 1;
        MenuMng.f_use_readbuff = 0;
    }
    int i;
    for (i = 0; i < 21; i++) {
        _stMModel *p = ioACp->real_part[i];
        if (!p || p->mmdp->datap) continue;
        int id = p->mmdp->id;
        if (id % 1000 == 0 || id / 1000 == 0xf) continue;
        if (DF_MMD_ReadStart(p->mmdp, p->mmdp->kind, id, 0x10000) < 0) break;   /* retried next frame */
    }
    if (i >= 21) ioACp->read_cc++;
    return 1;
}

/* read chain step 0: start the disc read of the connect MI2 of this core / arm / leg (skipped when unchanged) */
static int __OneAC_ReadAll_ReadConnectMI2(stMenuAC *ioACp)
{
    PS2_FRAME(0x60);
    int core = 1000, arm = 2000, leg = 3000;
    if (DF_MMD_ReadSync(0) == 0) return 0;
    if (Cdvd_CdSync() == 1) return 0;
    if (MenuMng.f_use_readbuff == 1) return 0;
    if (ioACp->cur_part[1]) core = ioACp->cur_part[1]->id;
    if (ioACp->cur_part[2]) arm = ioACp->cur_part[2]->id;
    if (ioACp->cur_part[5]) leg = ioACp->cur_part[5]->id;
    int mi2id = DF_MPART_GetConnectMI2ID(core, arm, leg);
    if (ioACp->connect.cur.mi2id == mi2id) {
        ioACp->flag.set_mi2 = 1;
        ioACp->read_cc++;
        return 1;
    }
    int lsn = MenuMng.idRankerMotion ? (signed char)MenuMng.idRankerMotion + 0x1fa3 : mi2id + 0x1f3f;
    if (Cdvd_ReadLsnBlind(lsn, g_pbufRead, 0, 0) != 0)
        Cdvd_ReadLsnBlind(0x1f61, g_pbufRead, 0, 0);
    MenuMng.f_use_readbuff = 1;
    ioACp->connect.cur.mi2id = (char)mi2id;
    ioACp->connect.cur.kind = (char)DF_MPART_GetConnectMI2Kind(core, arm, leg);
    ioACp->flag.set_mi2 = 0;
    ioACp->flag.reading = 1;
    ioACp->read_cc++;
    return 1;
}

/* read_func of a whole AC (DF_M_AC_PartsRead / SetEquipNecessary) */
static int __OneAC_ReadAll(stMenuAC *ioACp)
{
    PS2_FRAME(0x10);
    switch (ioACp->read_cc) {
    case 0: return __OneAC_ReadAll_ReadConnectMI2(ioACp);
    case 1: return __OneAC_ReadAll_ReadAllParts(ioACp);
    case 2: return __OneAC_ReadAll_SetAllParts(ioACp);
    case 3: return __OneAC_ReadAll_SetAllTM2(ioACp);
    default: return 0;
    }
}

/* play_func while fading: alpha prev -> next over cc_max frames (and add_z per frame), then PlayBasic */
static void __OneAC_PlayFade(stMenuAC *ioACp)
{
    PS2_FRAME(0x10);
    if (ioACp->flag.reading == 1) return;
    if (MenuMng.stat == 0xc) return;
    short old = ioACp->cc;
    ioACp->cc = old + 1;
    if (old < ioACp->cc_max) {
        int max = ioACp->cc_max, c = ioACp->cc;
        ioACp->alpha.cur = (unsigned char)((ioACp->alpha.prev * (max - c) + ioACp->alpha.next * c) / max);
        ioACp->pos[2] = ioACp->pos[2] + ioACp->add_z;
    } else {
        ioACp->alpha.cur = ioACp->alpha.next;
        ioACp->stat = ioACp->stat == 1 ? 3 : 0x63;
        ioACp->play_func = (void (*)(void))GFN(__OneAC_PlayBasic);
    }
    __OneAC_PlayBasic(ioACp);
}

/* play_func: spin, rebuild the draw table, the AC matrix (scale, RotY(pi + ang.y), RotZ, RotX, pos), attach and
 * play every part */
static void __OneAC_PlayBasic(stMenuAC *ioACp)
{
    PS2_FRAME(0x60);
    if (ioACp->flag.rotate == 1 && MenuMng.f_help == 0)
        ioACp->ang[1] = ioACp->ang[1] - 0x1.1df46ap-7f;   /* 0.5 deg */
    __OneAC_SetPartsTable(ioACp, ioACp->cur_part);
    if (ioACp->flag.tmp_connect == 1) __OneAC_SetTempPartsAttr(ioACp);
    /* vmul.xyz of IdentityMtx rows 0-2 by scale (w lanes stay the identity's), row 3 copied */
    float (*m)[4] = ioACp->local_world_mat;
    for (int j = 0; j < 4; j++) m[3][j] = IdentityMtx[3][j];
    for (int r = 0; r < 3; r++) {
        for (int j = 0; j < 3; j++) m[r][j] = IdentityMtx[r][j] * ioACp->scale[j];
        m[r][3] = IdentityMtx[r][3];
    }
    sceVu0RotMatrixY(m, m, DF_MFUNC_AngForm(0x1.921fb6p+1f + ioACp->ang[1]));
    sceVu0RotMatrixZ(m, m, DF_MFUNC_AngForm(ioACp->ang[2]));
    sceVu0RotMatrixX(m, m, DF_MFUNC_AngForm(ioACp->ang[0]));
    sceVu0TransMatrix(m, m, ioACp->pos);
    float alpha = (float)ioACp->alpha.cur / 128.0f;
    for (int i = 0; i < 21; i++) {
        _stMModel *p = ioACp->cur_part[i];
        if (!p) continue;
        __ConnectPart(ioACp, p, i);
        p->mulalpha = alpha;   /* delay slot of the DF_MMODEL_Play call */
        DF_MMODEL_Play(p);
    }
}

/* per frame: shadow packets, then each AC's read_func and play_func (the 2 x 3 rotated M_ACp view) */
void DF_M_AC_Play(void)
{
    PS2_FRAME(0x50);
    if (MenuMng.stat == 0xc) return;
    unsigned int *dmap = MPktTmpTopp;   /* local only: the global is not advanced */
    for (int p = 0; p < 2; p++)
        for (int i = 0; i < 3; i++) {
            stMenuAC *acp = M_ACp[p][i];
            memcpy(acp->pos2, acp->pos, 16);
            if (acp->flag.draw == 1 && acp->alpha.cur > 0)
                dmap = __OneAC_MakeShadowPkt(dmap, acp);
            else
                acp->shadow_dmap = 0;
            if (acp->read_func) {
                if (gMManageCtr.fAddDirt) {
                    /* dirt pass: a read_func returning 0 ends the whole frame (no TransMissionAC either) */
                    if (gcall_as<int(stMenuAC *)>(acp->read_func, acp) == 0) return;
                } else {
                    gcall_as<int(stMenuAC *)>(acp->read_func, acp);
                }
            }
            if (acp->play_func) gcall_as<void(stMenuAC *)>(acp->play_func, acp);
        }
    if (M_AC_Trans == 1) __TransMissionAC();
}

/* drop the previewed part: its MI2 (unless shared with the current one) and its up to three models */
static void __DeleteTmpParts(stMenuAC *ioACp)
{
    PS2_FRAME(0x30);
    if (ioACp->connect.tmp.mi2p && ioACp->connect.tmp.mi2p != ioACp->connect.cur.mi2p)
        DF_MMM_Free(0, ioACp->connect.tmp.mi2p);
    ioACp->connect.tmp.mi2p = 0;
    ioACp->connect.tmp.kind = 0;
    ioACp->connect.tmp.mi2id = 0;
    ioACp->connect.tmp_part_id = 0;
    ioACp->connect.tmp_idx = -1;
    for (int k = 0; k < 3; k++) {
        if (ioACp->connect.part[k]) DF_MMODEL_DeleteModelAndMMD(ioACp->connect.part[k]);
        ioACp->connect.part[k] = 0;
    }
}

static inline void mac_select_light(_stMModel *p, int inFlag)
{
    p->flag.select = inFlag & 1;
    p->ld_matp = (float (*)[4][4])((unsigned char *)MLightMng + 0x100);
    p->lc_matp = (float (*)[4][4])((unsigned char *)MLightMng + 0x140);
}

/* select highlight of an equip slot (and the parts shown with it) */
void DF_M_AC_SelectPart(stMenuAC *ioACp, int inEquipKind, int inFlag)
{
    PS2_FRAME(0x50);
    int idx = __EquipIDX2IDX(inEquipKind);
    _stMModel *parts[2];
    parts[0] = ioACp->real_part[idx];
    switch (idx) {
    case 0xf:
        DF_MMODEL_Select(parts[0], inFlag, inFlag);
        if (ioACp->connect.cur.flag.wback == 1) DF_MMODEL_Select(ioACp->real_part[14], inFlag, inFlag);
        break;
    case 0xe:
        DF_MMODEL_Select(parts[0], inFlag, inFlag);
        if (ioACp->connect.cur.flag.wback == 1) DF_MMODEL_Select(ioACp->real_part[15], inFlag, inFlag);
        break;
    case 9:
    case 8:
    case 7:
        DF_MMODEL_Select(ioACp->cur_part[1], inFlag, inFlag == 1 ? 3 : 0);
        if (parts[0]) mac_select_light(parts[0], inFlag);
        break;
    case 0xa: {
        parts[1] = ioACp->real_part[idx + 1];
        int m = inFlag == 1 ? 3 : 0;
        DF_MMODEL_Select(ioACp->cur_part[2], inFlag, m);
        DF_MMODEL_Select(ioACp->cur_part[3], inFlag, m);
        DF_MMODEL_Select(ioACp->cur_part[4], inFlag, m);
        for (int k = 0; k < 2; k++)
            if (parts[k]) mac_select_light(parts[k], inFlag);
        break;
    }
    case 0xc:
        DF_MMODEL_Select(parts[0], inFlag, inFlag);
        DF_MMODEL_Select(ioACp->real_part[idx + 1], inFlag, inFlag);
        break;
    case 2:
        DF_MMODEL_Select(parts[0], inFlag, inFlag);
        DF_MMODEL_Select(ioACp->real_part[idx + 1], inFlag, inFlag);
        DF_MMODEL_Select(ioACp->real_part[idx + 2], inFlag, inFlag);
        break;
    default:
        DF_MMODEL_Select(parts[0], inFlag, inFlag);
        break;
    }
}

/* empty the slot: keep the kind (id kind * 1000), drop its model */
static inline void mac_empty_slot(stMenuAC *acp, int idx)
{
    acp->part_id[idx] = (short)(acp->part_id[idx] / 1000 * 1000);
    DF_MMODEL_DeleteModelAndMMD(acp->real_part[idx]);
    acp->real_part[idx] = 0;
}

/* fill every empty mandatory slot (equip kinds 0..7) with the first part the player owns and start the read;
 * slot 6 follows the leg: categories 13+ have none (an equipped one is removed). Returns -1 busy, 1 parts added,
 * 2 some kind had no part, 0 nothing to do */
int DF_M_AC_SetEquipNecessary(stMenuAC *ioACp)
{
    PS2_FRAME(0x4a0);
    PS2_STACK(stMPartIdTable, tbl, 0x90);
    if (!ioACp) return -1;
    if (ioACp->flag.reading == 1) return -1;
    if (ioACp->flag.tmp_connect == 1) return -1;
    int fail = 0, added = 0;
    for (int k = 0; k < 8; k++) {
        int idx = __EquipIDX2IDX(k);
        if (ioACp->part_id[idx] % 1000 != 0) {
            if (idx == 6 && !(DF_MPART_GetCategory(ioACp->part_id[5]) < 0xd) && ioACp->part_id[idx] % 1000 != 0)
                mac_empty_slot(ioACp, idx);
            continue;
        }
        if (idx == 6 && !(DF_MPART_GetCategory(ioACp->part_id[5]) < 0xd)) continue;
        DF_MFUNC_ClearMemory(&tbl, 0x104);
        DF_MPART_MakeIDTable(&tbl, 2, ioACp->player, k, k);
        if (tbl.num <= 0) {
            fail++;
            continue;
        }
        if (ioACp->part_id[idx] % 1000 != 0) mac_empty_slot(ioACp, idx);   /* dead: the slot is empty here */
        ioACp->part_id[idx] = tbl.cell[0].id;
        stMmdData *mmd = DF_MMD_GetMMD(1, ioACp->part_id[idx]);
        if (!mmd) {
            mmd = DF_MMD_GetNewMMD(0);
            mmd->kind = 1;
            mmd->id = ioACp->part_id[idx];
        }
        ioACp->real_part[idx] = DF_MMODEL_GetNewModel(0);
        added++;
        ioACp->real_part[idx]->mmdp = mmd;
        ioACp->real_part[idx]->kind = mmd->kind;
        ioACp->real_part[idx]->id = mmd->id;
    }
    if (added > 0) {
        __OneAC_SetPartsTable(ioACp, ioACp->cur_part);
        memset(ioACp->part_stat, 0, 0x15);
        memset(ioACp->sub_stat, 0, 0x15);
        ioACp->flag.reading = 1;
        ioACp->flag.set_tm2 = 0;
        ioACp->read_cc = 0;
        ioACp->read_func = (int (*)(void))GFN(__OneAC_ReadAll);
    }
    if (added > 0) return 1;
    if (fail > 0) return 2;
    return 0;
}

/* ---- group G5: tmp part editing (commit / preview of a part in the assembly screen) ---- */

/* the inlined "empty this slot" helper: 1 when the slot held a part (kind kept, model dropped), 0 when already empty */
static inline int mac_try_empty_slot(stMenuAC *acp, int idx)
{
    if (acp->part_id[idx] % 1000 == 0) return 0;
    mac_empty_slot(acp, idx);
    return 1;
}

/* commit a back weapon (slot 0xe / 0xf); a W-back weapon (connect flag wback) fills both back slots. Returns the
 * removed-parts mask (8 / 0x10: the partner slot's weapon makes way for a W-back weapon, 0x20: a W-back weapon is
 * replaced by a single one) */
static unsigned int __UpdateBack(stMenuAC *ioACp, int inIdx)
{
    PS2_FRAME(0x60);
    unsigned int mask = 0;
    int other = inIdx == 0xf ? 0xe : 0xf;
    if (!ioACp->connect.cur.flag.wback) {
        if (ioACp->connect.tmp.flag.wback == 1 && ioACp->real_part[other])
            mask |= other == 0xe ? 8 : 0x10;
    } else if (!ioACp->connect.tmp.flag.wback && (ioACp->real_part[14] || ioACp->real_part[15])) {
        mask |= 0x20;
    }
    DF_MMODEL_DeleteModelAndMMD(ioACp->real_part[inIdx]);
    ioACp->real_part[inIdx] = ioACp->connect.part[0];
    ioACp->part_id[inIdx] = ioACp->connect.tmp_part_id;
    if (ioACp->connect.cur.flag.wback == 1 || ioACp->connect.tmp.flag.wback == 1) {
        DF_MMODEL_DeleteModelAndMMD(ioACp->real_part[other]);
        ioACp->real_part[other] = ioACp->connect.part[1];
        ioACp->part_id[other] = ioACp->connect.tmp.flag.wback == 1 ? ioACp->connect.tmp_part_id : 10000;
    }
    return mask;
}

/* commit an arm weapon (slots 0x10..0x13); a removed or duplicated hand weapon (0x10 / 0x11) also empties its
 * partner slot (0x12 / 0x13): mask 0x800 (slot 0x10) / 0x1000 (slot 0x11) */
static unsigned int __UpdateArmWep(stMenuAC *ioACp, int inIdx)
{
    PS2_FRAME(0x70);
    unsigned int mask = 0;
    int other = inIdx == 0x10 ? 0x12 : 0x13;
    unsigned int bit = inIdx == 0x10 ? 0x800 : 0x1000;
    DF_MMODEL_DeleteModelAndMMD(ioACp->real_part[inIdx]);
    ioACp->real_part[inIdx] = ioACp->connect.part[0];
    ioACp->part_id[inIdx] = ioACp->connect.tmp_part_id;
    if (inIdx != 0x11 && inIdx != 0x10) return 0;
    if (!ioACp->real_part[inIdx]) {
        if (mac_try_empty_slot(ioACp, other) == 1) mask |= bit;
    } else if (ioACp->part_id[other] == ioACp->part_id[inIdx]) {
        if (mac_try_empty_slot(ioACp, other) == 1) mask |= bit;
    }
    return mask;
}

/* commit the arms (slot 2 + the two hand models 3 / 4); no arms empties the arm-mounted slots (mask 4), weapon arms
 * (category 7) empty the hand / back weapon slots 16..19 */
static unsigned int __UpdateArm(stMenuAC *ioACp)
{
    PS2_FRAME(0x40);
    unsigned int mask = 0;
    DF_MMODEL_DeleteModelAndMMD(ioACp->real_part[2]);
    ioACp->real_part[2] = ioACp->connect.part[0];
    ioACp->part_id[2] = ioACp->connect.tmp_part_id;
    DF_MMODEL_DeleteModelAndMMD(ioACp->real_part[3]);
    DF_MMODEL_DeleteModelAndMMD(ioACp->real_part[4]);
    ioACp->real_part[3] = ioACp->connect.part[1];
    ioACp->real_part[4] = ioACp->connect.part[2];
    ioACp->part_id[3] = (short)(ioACp->connect.part[1] ? ioACp->connect.part[1]->id : 0);
    ioACp->part_id[4] = (short)(ioACp->connect.part[2] ? ioACp->connect.part[2]->id : 0);
    if (!ioACp->real_part[2]) {
        static const unsigned char slots[8] = {10, 11, 12, 13, 16, 17, 18, 19};
        int n = 0;
        for (int k = 0; k < 8; k++) n += mac_try_empty_slot(ioACp, slots[k]);
        if (n > 0) mask |= 4;
        return mask;
    }
    /* the first category query's result is dropped (original) */
    DF_MPART_GetCategory(ioACp->real_part[2]->id);
    int id = ioACp->real_part[2]->id;
    if (id == 0 || DF_MPART_GetCategory(id) != 7) return mask;
    if (mac_try_empty_slot(ioACp, 16) == 1) mask |= 1;
    if (mac_try_empty_slot(ioACp, 17) == 1) mask |= 2;
    if (mac_try_empty_slot(ioACp, 18) == 1) mask |= 0x2800;
    if (mac_try_empty_slot(ioACp, 19) == 1) mask |= 0x3000;
    return mask;
}

/* commit the core; a core without back-weapon support (info byte 0x10d bit 2) empties the back slots 18 / 19, and
 * any option parts are removed (mask 0x80) */
static unsigned int __UpdateCore(stMenuAC *ioACp)
{
    PS2_FRAME(0x30);
    unsigned int mask = 0;
    DF_MMODEL_DeleteModelAndMMD(ioACp->real_part[1]);
    ioACp->real_part[1] = ioACp->connect.part[0];
    ioACp->part_id[1] = ioACp->connect.tmp_part_id;
    _stMModel *core = ioACp->real_part[1];
    if (core) {
        unsigned char *info = (unsigned char *)core->prm.part.infop;
        if (info && !(info[0x10d] & 4)) {
            if (mac_try_empty_slot(ioACp, 18) == 1) mask |= 0x2800;
            if (mac_try_empty_slot(ioACp, 19) == 1) mask |= 0x3000;
        }
    }
    if (ioACp->option.equip[0] != 0 || ioACp->option.equip[1] != 0) {
        ioACp->option.equip[0] = 0;
        ioACp->option.equip[1] = 0;
        mask |= 0x80;
        ioACp->option.use_slot = 0;
    }
    return mask;
}

/* end the preview: inFlag 1 commits the previewed part into the AC (returns the mask of parts removed with it,
 * 0x80000000 when the part cannot be equipped), anything else drops it; then rebuild the draw table */
unsigned int DF_M_AC_UpdateTmpPart(stMenuAC *ioACp, int inFlag)
{
    PS2_FRAME(0x70);
    unsigned int mask = 0;
    if (!ioACp->flag.tmp_connect) {
        ioACp->connect.tmp_part_id = 0;
        ioACp->connect.tmp_idx = -1;
        return 0;
    }
    if (ioACp->connect.tmp_equip_err != 0 && inFlag == 1) return 0x80000000u;
    if (inFlag == 1) {
        /* a part whose model was never created (and not a removal) skips the slot update and the MI2 swap */
        if (ioACp->connect.part[0] || ioACp->connect.tmp_part_id % 1000 == 0) {
            int idx = ioACp->connect.tmp_idx;
            switch (idx) {
            case 0x11:
            case 0x10:
                mask = __UpdateArmWep(ioACp, idx);
                break;
            case 0xf:
            case 0xe:
                mask = __UpdateBack(ioACp, idx);
                break;
            case 0xc:
            case 0xa:
                DF_MMODEL_DeleteModelAndMMD(ioACp->real_part[idx]);
                ioACp->real_part[idx] = ioACp->connect.part[0];
                ioACp->part_id[idx] = ioACp->connect.tmp_part_id;
                DF_MMODEL_DeleteModelAndMMD(ioACp->real_part[idx + 1]);
                ioACp->real_part[idx + 1] = ioACp->connect.part[1];
                ioACp->part_id[idx + 1] = ioACp->connect.tmp_part_id;
                break;
            case 5: {
                int cat = 0;
                DF_MMODEL_DeleteModelAndMMD(ioACp->real_part[5]);
                ioACp->real_part[5] = ioACp->connect.part[0];
                ioACp->part_id[5] = ioACp->connect.tmp_part_id;
                if (ioACp->real_part[5]) cat = DF_MPART_GetCategory(ioACp->real_part[5]->id);
                if (cat == 0xd || cat == 0xf)
                    if (mac_try_empty_slot(ioACp, 6) == 1) mask |= 0x400;
                break;
            }
            case 2:
                mask = __UpdateArm(ioACp);
                break;
            case 1:
                mask = __UpdateCore(ioACp);
                break;
            default:
                DF_MMODEL_DeleteModelAndMMD(ioACp->real_part[idx]);
                ioACp->real_part[idx] = ioACp->connect.part[0];
                ioACp->part_id[idx] = ioACp->connect.tmp_part_id;
                break;
            }
            ioACp->connect.cur.mi2id = ioACp->connect.tmp.mi2id;
            ioACp->connect.cur.kind = ioACp->connect.tmp.kind;
            if (ioACp->connect.cur.mi2p != ioACp->connect.tmp.mi2p) {
                DF_MMM_Free(0, ioACp->connect.cur.mi2p);
                ioACp->connect.cur.mi2p = ioACp->connect.tmp.mi2p;
            }
        }
        ioACp->connect.cur.flag = ioACp->connect.tmp.flag;
        ioACp->connect.tmp.mi2p = 0;
        ioACp->connect.tmp.kind = 0;
        ioACp->connect.tmp.mi2id = 0;
        ioACp->connect.part[0] = 0;
        ioACp->connect.part[1] = 0;
        ioACp->connect.part[2] = 0;
    } else {
        __DeleteTmpParts(ioACp);
    }
    /* tmp_idx is not reset on the commit path (original) */
    ioACp->flag.tmp_connect = 0;
    ioACp->connect.tmp_part_id = 0;
    __OneAC_SetPartsTable(ioACp, ioACp->cur_part);
    for (int i = 0; i < 21; i++) {
        if (!ioACp->cur_part[i]) continue;
        ioACp->cur_part[i]->flag.draw = 1;
        DF_MMODEL_Select(ioACp->cur_part[i], 0, 0);
        __SetPartInfo(ioACp, ioACp->cur_part[i], i, 0, 1);
    }
    if (ioACp->player >= 0 && ioACp->player < 2)
        M_AC_GetTuneStatFromPlayerInfo(&ioACp->real_tune, ioACp, ioACp->player, 0, 1);
    if (ioACp->player >= 0 && ioACp->player < 2)
        M_AC_GetTuneStatFromPlayerInfo(&ioACp->cur_tune, ioACp, ioACp->player, 0, 0);
    return mask;
}

/* start previewing part inPartID in equip slot inIdx (an id with index 0 = remove the slot's part): create its
 * model(s) and install the tmp read state machine (read_cc 2 = reuse the current connect MI2 when the part is the
 * equipped one) */
void DF_M_AC_SetTmpPart(stMenuAC *ioACp, int inIdx, int inPartID)
{
    PS2_FRAME(0x60);
    PS2_STACK(int[3], ids, 0x50);   /* core / arm / leg ids for the connect MI2 query */
    memset(ids, 0, 12);
    ids[0] = ioACp->part_id[1];
    ids[1] = ioACp->part_id[2];
    ids[2] = ioACp->part_id[5];
    if (ioACp->flag.reading == 1) return;
    __DeleteTmpParts(ioACp);
    int idx = __EquipIDX2IDX(inIdx);
    ioACp->connect.tmp_idx = (char)idx;
    ioACp->connect.tmp_part_id = (short)inPartID;
    DF_MFUNC_ClearMemoryC(&ioACp->connect.tmp.flag, 1);
    ioACp->connect.tmp.flag.weparm = ioACp->connect.cur.flag.weparm;
    ioACp->connect.tmp.flag.wback = 0;
    if (inPartID % 1000 == 0) {
        ioACp->connect.tmp.mi2id = ioACp->connect.cur.mi2id;
        ioACp->connect.tmp.kind = ioACp->connect.cur.kind;
        ioACp->connect.tmp.mi2p = ioACp->connect.cur.mi2p;
        if (ioACp->real_part[idx]) ioACp->real_part[idx]->flag.draw = 0;
        if (ioACp->connect.cur.flag.wback == 1 && (idx == 0xe || idx == 0xf)) {
            if (ioACp->real_part[14]) ioACp->real_part[14]->flag.draw = 0;
            if (ioACp->real_part[15]) ioACp->real_part[15]->flag.draw = 0;
        }
        ioACp->flag.tmp_connect = 1;
        __OneAC_SetPartsTable(ioACp, ioACp->cur_part);
        if (ioACp->player >= 0 && ioACp->player < 2)
            M_AC_GetTuneStatFromPlayerInfo(&ioACp->cur_tune, ioACp, ioACp->player, 0, 0);
        __CheckTmpEquipEnable(ioACp);
        ioACp->read_func = 0;
        return;
    }
    switch (inPartID / 1000) {
    case 8:
    case 9:
        ioACp->connect.part[0] = DF_MMODEL_GetNewModel(0);
        ioACp->connect.part[1] = DF_MMODEL_GetNewModel(ioACp->connect.part[0]);
        ioACp->connect.part[1]->kind = 1;
        ioACp->connect.part[1]->id = inPartID;
        break;
    case 3:
        ioACp->connect.part[0] = DF_MMODEL_GetNewModel(0);
        ids[2] = inPartID;
        break;
    case 2:
        ioACp->connect.part[0] = DF_MMODEL_GetNewModel(0);
        ioACp->connect.part[1] = DF_MMODEL_GetNewModel(ioACp->connect.part[0]);
        ioACp->connect.part[2] = DF_MMODEL_GetNewModel(ioACp->connect.part[1]);
        ids[1] = inPartID;
        break;
    case 1:
        ioACp->connect.part[0] = DF_MMODEL_GetNewModel(0);
        ids[0] = inPartID;
        break;
    default:
        ioACp->connect.part[0] = DF_MMODEL_GetNewModel(0);
        break;
    }
    ioACp->connect.part[0]->kind = 1;
    ioACp->connect.part[0]->id = inPartID;
    if (inPartID == ioACp->part_id[idx]) {
        /* re-selecting the equipped part: reuse the current connect MI2 */
        ioACp->connect.tmp.mi2id = ioACp->connect.cur.mi2id;
        ioACp->connect.tmp.kind = ioACp->connect.cur.kind;
        ioACp->connect.tmp.mi2p = ioACp->connect.cur.mi2p;
        ioACp->read_cc = 2;
    } else {
        int mi2id = DF_MPART_GetConnectMI2ID(ids[0], ids[1], ids[2]);
        /* tune stat before the MI2 id is stored (original order) */
        if (ioACp->player >= 0 && ioACp->player < 2)
            M_AC_GetTuneStatFromPlayerInfo(&ioACp->cur_tune, ioACp, ioACp->player, 0, 0);
        ioACp->connect.tmp.mi2id = (char)mi2id;
        ioACp->connect.tmp.kind = (char)DF_MPART_GetConnectMI2Kind(ids[0], ids[1], ids[2]);
        ioACp->read_cc = 0;
    }
    ioACp->flag.reading = 1;
    ioACp->flag.set_tm2 = 0;
    ioACp->read_func = (int (*)(void))GFN(__OneAC_ReadTmpPart);
}

/* ---- rest part B: sell flow / reposit checks (never executed in the replays) ---- */

/* take a sold part off the AC (idx from the part id; a left back weapon 0xe becomes 0xf for inFlag 1 unless the
 * back weapon is a W-back one). Returns the warning mask: 0x200 = the core (nothing is removed), 4 = dependent parts
 * went with it (arm weapons / extensions of an arm, the paired option of an arm weapon) */
unsigned int DF_M_AC_RemovePart(stMenuAC *ioACp, int inPartID, int inFlag)
{
    PS2_FRAME(0x60);
    unsigned int mask = 0;
    int idx = DF_M_AC_PartID2IDX(inPartID);
    if (idx == 0xe && !ioACp->connect.cur.flag.wback && inFlag == 1) idx++;
    switch (idx) {
    case 0x14:
        ioACp->option.equip[0] &= ~(1u << ((inPartID % 1000 - 1) & 31));
        break;
    case 0x10:
    case 0x11: {
        int pair = idx == 0x10 ? 0x12 : 0x13;
        if (inPartID == ioACp->part_id[idx]) {
            if (mac_try_empty_slot(ioACp, idx) && mac_try_empty_slot(ioACp, pair)) mask |= 4;
        } else if (inPartID == ioACp->part_id[pair]) {
            mac_try_empty_slot(ioACp, pair);
        }
        break;
    }
    case 0xf:
        mac_try_empty_slot(ioACp, idx);
        if (ioACp->connect.cur.flag.wback == 1) mac_try_empty_slot(ioACp, idx - 1);
        break;
    case 0xe:
        mac_try_empty_slot(ioACp, idx);
        if (ioACp->connect.cur.flag.wback == 1) mac_try_empty_slot(ioACp, idx + 1);
        break;
    case 0xc:
    case 0xa:
        for (int i = 0; i < 2; i++) mac_try_empty_slot(ioACp, idx + i);
        break;
    case 2: {
        for (int i = 0; i < 3; i++) mac_try_empty_slot(ioACp, idx + i);
        static const unsigned char dep[8] = {10, 11, 12, 13, 16, 17, 19, 18};
        int n = 0;
        for (int i = 0; i < 8; i++) n += mac_try_empty_slot(ioACp, dep[i]);
        if (n > 0) mask |= 4;
        break;
    }
    case 1:
        if (inPartID == ioACp->part_id[idx]) mask |= 0x200;
        break;
    default:
        mac_try_empty_slot(ioACp, idx);
        break;
    }
    __OneAC_SetPartsTable(ioACp, ioACp->cur_part);
    if (ioACp->player >= 0 && ioACp->player < 2)
        M_AC_GetTuneStatFromPlayerInfo(&ioACp->cur_tune, ioACp, ioACp->player, 0, 0);
    if (ioACp->player >= 0 && ioACp->player < 2)
        M_AC_GetTuneStatFromPlayerInfo(&ioACp->real_tune, ioACp, ioACp->player, 0, 1);
    DF_M_AC_CheckEquipFlag(ioACp, 0);
    return mask;
}

/* is the part equipped (options: its bit; a left back weapon id counts as the right slot unless flagLR; arm
 * weapons also match their paired slot 0x12 / 0x13) */
int DF_M_AC_IsEquipPart4(stMenuAC *inACp, int inPartID, int flagLR)
{
    PS2_FRAME(0x40);
    int idx = DF_M_AC_PartID2IDX(inPartID);
    if (idx == 0xe && flagLR != 0) idx = 0xf;
    if (idx == 0x14) return (inACp->option.equip[0] & (1u << ((inPartID % 1000 - 1) & 31))) != 0;
    if (inPartID == inACp->part_id[idx]) return 1;
    if (idx == 0x10) return inACp->part_id[0x12] == inPartID;
    if (idx == 0x11) return inACp->part_id[0x13] == inPartID;
    return 0;
}

/* the same for the sell screen: a back weapon also matches the left back slot */
int DF_M_AC_IsEquipPart3(stMenuAC *inACp, int inPartID)
{
    PS2_FRAME(0x30);
    int idx = DF_M_AC_PartID2IDX(inPartID);
    if (idx == 0x14) return (inACp->option.equip[0] & (1u << ((inPartID % 1000 - 1) & 31))) != 0;
    if (inPartID == inACp->part_id[idx]) return 1;
    if (idx == 0xe) return inACp->part_id[0xf] == inPartID;
    if (idx == 0x10) return inACp->part_id[0x12] == inPartID;
    if (idx == 0x11) return inACp->part_id[0x13] == inPartID;
    return 0;
}

/* ---- group G2: setters, init, circle positions, reset / copy ---- */

/* camouflage of one paint block: the first `num` textures of the part get the camouflage switch (or 0 when that many
 * from the end are not there), the rest its inverse. No bound on the MMD's texture count (f_tm2off has 24 entries) */
void DF_M_AC_SetCamoPettern(stMenuAC *macp, _stMModel *dpartp, int pIdx)
{
    if (!dpartp) return;
    stMmdData *mmd = dpartp->mmdp;
    if (!mmd) return;
    unsigned char camo = macp->camouflage[pIdx];
    int num;
    switch (pIdx) {
    case 0: num = 1; break;
    case 1: num = 1; break;
    case 2: num = 4; break;
    case 3: num = 2; break;
    default: return;
    }
    int n = mmd->TM2.num;
    if (n <= 0) return;
    unsigned char *off = (unsigned char *)dpartp + 0xaf;   /* f_tm2off[i], unbounded */
    for (int i = 0; i < n; i = i + 1, n = dpartp->mmdp->TM2.num) {
        if (i < num)
            off[i] = i + num < n ? camo : 0;
        else
            off[i] = (unsigned char)(1 - camo);
    }
}

/* weapon colours of the six weapon slots (inFlag 1: copy the colour block into real_col and use that) */
void DF_M_AC_UpdateWepColor(stMenuAC *ioACp, struct_098975f3 *inACColp, int inFlag)
{
    PS2_FRAME(0x20);
    if (!ioACp) return;
    if (inFlag == 1) {
        if (inACColp) memmove(&ioACp->real_col, inACColp, 0xb0);   /* forward byte-pair copy */
        ioACp->colp = &ioACp->real_col;
    } else {
        ioACp->colp = inACColp;
    }
    DF_MMODEL_SetWepColor(ioACp->cur_part[16], mac_col(ioACp, 0x50));
    DF_MMODEL_SetWepColor(ioACp->cur_part[17], mac_col(ioACp, 0x60));
    DF_MMODEL_SetWepColor(ioACp->cur_part[18], mac_col(ioACp, 0x70));
    DF_MMODEL_SetWepColor(ioACp->cur_part[19], mac_col(ioACp, 0x80));
    if (DF_MMODEL_IsWBackWeapon(ioACp->cur_part[15]) == 1 || DF_MMODEL_IsWBackWeapon(ioACp->cur_part[14]) == 1)
        DF_MMODEL_SetWepColor(ioACp->cur_part[15], mac_col(ioACp, 0x90));
    else
        DF_MMODEL_SetWepColor(ioACp->cur_part[15], mac_col(ioACp, 0xa0));
    DF_MMODEL_SetWepColor(ioACp->cur_part[14], mac_col(ioACp, 0x90));
}

/* body colours (CLUTs) of the six body parts; inFlag 1 copies the block into real_col but keeps the weapon colours
 * (+0x50..0xaf restored from a backup), otherwise colp points at the caller's block */
void DF_M_AC_UpdateClut(stMenuAC *ioACp, struct_098975f3 *inACColp, int inFlag)
{
    PS2_FRAME(0xd0);
    if (!ioACp) return;
    if (inFlag == 1) {
        unsigned char backup[0xb0];
        memcpy(backup, &ioACp->real_col, 0xb0);
        if (inACColp) memmove(&ioACp->real_col, inACColp, 0xb0);   /* forward byte-pair copy */
        memcpy((unsigned char *)&ioACp->real_col + 0x50, backup + 0x50, 0x60);
        ioACp->colp = &ioACp->real_col;
    } else {
        ioACp->colp = inACColp;
    }
    DF_MMODEL_SetPartsCluts(ioACp->cur_part[0], mac_col(ioACp, 0), 0);
    DF_MMODEL_SetPartsCluts(ioACp->cur_part[1], mac_col(ioACp, 0x10), 0);
    DF_MMODEL_SetPartsCluts(ioACp->cur_part[2], mac_col(ioACp, 0x30), mac_col(ioACp, 0x20));
    DF_MMODEL_SetPartsCluts(ioACp->cur_part[4], mac_col(ioACp, 0x30), mac_col(ioACp, 0x30));
    DF_MMODEL_SetPartsCluts(ioACp->cur_part[3], mac_col(ioACp, 0x20), mac_col(ioACp, 0x20));
    DF_MMODEL_SetPartsCluts(ioACp->cur_part[5], mac_col(ioACp, 0x40), 0);
}

/* copy an AC for display (model objects): placement, connect MI2, colours, camouflage and flags; new models share
 * the source's MMDs. The whole record is cleared first (player / stat / read_func included) */
void DF_M_AC_Copy(stMenuAC *outACp, stMenuAC *inACp)
{
    PS2_FRAME(0x80);
    if (!outACp || !inACp) return;
    if (inACp->flag.reading == 1) return;
    DF_MFUNC_ClearMemory(outACp, 0xf8);
    memcpy(outACp->pos, inACp->pos, 16);
    memcpy(outACp->ang, inACp->ang, 16);
    memcpy(outACp->scale, inACp->scale, 16);
    outACp->connect.cur.mi2id = inACp->connect.cur.mi2id;
    outACp->connect.cur.kind = inACp->connect.cur.kind;
    outACp->connect.cur.mi2p = inACp->connect.cur.mi2p;
    outACp->connect.cur.flag = inACp->connect.cur.flag;
    outACp->play_func = inACp->play_func;
    memcpy(&outACp->real_col, &inACp->real_col, 0xb0);
    outACp->colp = &outACp->real_col;
    outACp->alpha.cur = 0x80;
    for (int k = 0; k < 4; k++) outACp->camouflage[k] = inACp->camouflage[k];
    memcpy(&outACp->flag, &inACp->flag, 8);
    outACp->flag.tmp_connect = 0;
    outACp->flag.rotate = 0;
    outACp->flag.shadow_off = 1;
    for (int i = 0; i < 21; i++) {
        outACp->part_id[i] = inACp->part_id[i];
        _stMModel *p = inACp->real_part[i];
        if (!p || p->mmdp->id % 1000 == 0) continue;
        outACp->real_part[i] = DF_MMODEL_GetNewModel(0);
        outACp->real_part[i]->mmdp = p->mmdp;   /* the MMD is shared */
    }
    __OneAC_SetPartsTable(outACp, outACp->cur_part);
    __OneAC_ReadAll_SetAllParts(outACp);
}

/* fade out over inMax frames (alpha cur -> 0, pos.z += inAddZ per frame) */
void DF_M_AC_SetFadeOut(stMenuAC *ioACp, int inMax, float inAddZ)
{
    ioACp->alpha.prev = ioACp->alpha.cur;
    ioACp->alpha.next = 0;
    ioACp->cc = 0;
    ioACp->cc_max = (short)inMax;
    ioACp->add_z = inAddZ;
    ioACp->stat = 2;
    ioACp->play_func = (void (*)(void))GFN(__OneAC_PlayFade);
}

/* fade in from the current alpha */
void DF_M_AC_SetFadeInNatural(stMenuAC *ioACp, int inMax, float inAddZ)
{
    ioACp->flag.draw = 1;
    ioACp->alpha.prev = ioACp->alpha.cur;
    ioACp->alpha.next = 0x80;
    ioACp->cc = 0;
    ioACp->cc_max = (short)inMax;
    ioACp->add_z = inAddZ;
    ioACp->stat = 1;
    ioACp->play_func = (void (*)(void))GFN(__OneAC_PlayFade);
}

/* fade in from alpha 0 */
void DF_M_AC_SetFadeIn(stMenuAC *ioACp, int inMax, float inAddZ)
{
    ioACp->flag.draw = 1;
    ioACp->alpha.cur = 0;
    ioACp->alpha.prev = 0;
    ioACp->alpha.next = 0x80;
    ioACp->cc = 0;
    ioACp->cc_max = (short)inMax;
    ioACp->add_z = inAddZ;
    ioACp->stat = 1;
    ioACp->play_func = (void (*)(void))GFN(__OneAC_PlayFade);
}

/* start reading the whole AC (read_func __OneAC_ReadAll); -1 while a read is running */
int DF_M_AC_PartsRead(stMenuAC *ioACp)
{
    PS2_FRAME(0x20);
    if (ioACp->flag.reading == 1) return -1;
    memset(ioACp->part_stat, 0, 0x15);
    memset(ioACp->sub_stat, 0, 0x15);
    ioACp->flag.reading = 1;
    ioACp->flag.set_tm2 = 0;
    ioACp->flag.draw = 0;
    ioACp->read_cc = 0;
    ioACp->read_func = (int (*)(void))GFN(__OneAC_ReadAll);
    return 1;
}

/* rotate the player's M_ACp view so that AC inNum comes first */
void DF_M_AC_Select(int inPlayer, int inNum)
{
    for (int k = 0; k < 3; k++) {
        M_ACp[inPlayer][k] = M_ACTable[inPlayer] + inNum;
        inNum = inNum + 1 < 3 ? inNum + 1 : 0;
    }
}

/* drop every model, the connect MI2 and the tmp part, clear the record and restore num, player, the part ids, the
 * colours, the option words and the camouflage; Reset2 also frees the MMDs */
static inline void mac_reset(stMenuAC *ioACp, int inDelMMD)
{
    short ids[21];
    unsigned char col[0xb0];
    unsigned int opt[4];
    unsigned char camo[4];
    for (int i = 0; i < 21; i++) ids[i] = ioACp->part_id[i];
    memcpy(col, &ioACp->real_col, 0xb0);
    memcpy(opt, &ioACp->option, 16);   /* lwc1 / swc1: raw bits */
    for (int k = 0; k < 4; k++) camo[k] = ioACp->camouflage[k];
    for (int i = 0; i < 21; i++) {
        if (!ioACp->real_part[i]) continue;
        DF_MMD_ReadBreak(ioACp->real_part[i]->mmdp);
        if (inDelMMD)
            DF_MMODEL_DeleteModelAndMMD(ioACp->real_part[i]);
        else
            DF_MMODEL_DeleteModel(ioACp->real_part[i]);
        ioACp->real_part[i] = 0;
    }
    ioACp->connect.cur.mi2id = 0;
    ioACp->connect.cur.kind = 0;
    if (ioACp->connect.cur.mi2p) {
        DF_MMM_Free(0, ioACp->connect.cur.mi2p);
        ioACp->connect.cur.mi2p = 0;
    }
    __DeleteTmpParts(ioACp);
    short num = ioACp->num;
    char player = ioACp->player;
    DF_MFUNC_ClearMemory(ioACp, 0xf8);
    ioACp->num = num;
    ioACp->player = player;
    for (int i = 0; i < 21; i++) ioACp->part_id[i] = ids[i];
    memcpy(&ioACp->real_col, col, 0xb0);
    ioACp->colp = &ioACp->real_col;
    memcpy(&ioACp->option, opt, 16);
    for (int k = 0; k < 4; k++) ioACp->camouflage[k] = camo[k];
}

void DF_M_AC_Reset2(stMenuAC *ioACp)
{
    PS2_FRAME(0x190);
    mac_reset(ioACp, 1);
}

void DF_M_AC_Reset(stMenuAC *ioACp)
{
    PS2_FRAME(0x190);
    mac_reset(ioACp, 0);
}

/* (re)build the AC's models from its part ids and place it on its circle slot; once (flag.set) */
void DF_M_AC_SetData(stMenuAC *ioACp)
{
    PS2_FRAME(0xa0);
    if (ioACp->flag.set == 1) return;
    float f = 0x1.1df46ap-6f * ((360.0f * (float)ioACp->num) / 3.0f);   /* pi / 180 */
    fsmath_SinVU0(f);   /* results dropped (original) */
    fsmath_CosVU0(f);
    float ay = f + 0x1.921fb6p+1f;
    ioACp->pos[0] = 0.0f; ioACp->pos[1] = 0.0f; ioACp->pos[2] = 0.0f; ioACp->pos[3] = 1.0f;
    ioACp->ang[0] = 0.0f; ioACp->ang[1] = ay; ioACp->ang[2] = 0.0f; ioACp->ang[3] = 1.0f;
    ioACp->scale[0] = 1.0f; ioACp->scale[1] = 1.0f; ioACp->scale[2] = 1.0f; ioACp->scale[3] = 1.0f;
    ioACp->alpha.cur = 0x80;
    ioACp->env_alpha = 1.0f;
    ioACp->add_z = 0.0f;
    _stMModel *prev = 0;
    for (int i = 0; i < 21; i++) {
        if (ioACp->real_part[i]) {
            DF_MMODEL_DeleteModelAndMMD(ioACp->real_part[i]);
            ioACp->real_part[i] = 0;
        }
        if (ioACp->part_id[i] % 1000 == 0) continue;
        stMmdData *mmd = DF_MMD_GetMMD(1, ioACp->part_id[i]);
        if (!mmd) {
            mmd = DF_MMD_GetNewMMD(0);
            mmd->kind = 1;
            mmd->id = ioACp->part_id[i];
        }
        ioACp->real_part[i] = DF_MMODEL_GetNewModel(prev);
        ioACp->real_part[i]->mmdp = mmd;
        ioACp->real_part[i]->kind = mmd->kind;
        ioACp->real_part[i]->id = mmd->id;
        prev = ioACp->real_part[i];
    }
    __OneAC_SetPartsTable(ioACp, ioACp->cur_part);
    ioACp->flag.draw = 1;
    ioACp->flag.set = 1;
    ioACp->stat = 0x63;
    ioACp->play_func = (void (*)(void))GFN(__OneAC_PlayBasic);
}

/* circle slots of the AC selection carousel (rodata 0x25fcc0 / 0x25fd10, identical) */
static const float mac_circle_pos[5][4] = {
    {0.0f, 0.0f, -15.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 15.0f, 1.0f},
    {0.0f, 0.0f, 30.0f, 1.0f}, {0.0f, 0.0f, 45.0f, 1.0f},
};

/* pos = (1 - rate) * from + rate * to per xyz lane (mula.s / madd.s: both products rounded), w = 1 */
static inline void mac_circle_lerp(stMenuAC *acp, const float *from, const float *to, float rate)
{
    float r1 = 1.0f - rate;
    for (int j = 0; j < 3; j++) {
        float a = r1 * from[j];
        float b = rate * to[j];
        acp->pos[j] = a + b;
    }
    acp->pos[3] = 1.0f;
}

/* carousel position between slot idx + 1 and its neighbour (rate > 0: towards idx + 2, < 0: towards idx); inCnt 1
 * wraps the two-AC carousel. The rate comes in f13 (callers pass a dropped float in f12) */
void DF_M_AC_NXSetCirclePos(stMenuAC *ioACp, int inIdx, float unused, float inRate, int inCnt)
{
    (void)unused;
    const float (*P)[4] = mac_circle_pos;
    ioACp->flag.rotate = 0;
    if (!(inRate <= 0.0f)) {
        int to = inIdx + 2;
        if (inCnt == 1 && inIdx == 2) {
            to = 1;
            inIdx = -1;
        }
        mac_circle_lerp(ioACp, P[inIdx + 1], P[to], inRate);
    } else if (inRate < 0.0f) {
        inRate = -inRate;
        if (inCnt == 1 && inIdx == 0) inIdx = 3;
        mac_circle_lerp(ioACp, P[inIdx + 1], P[inIdx], inRate);
    } else {
        memcpy(ioACp->pos, P[inIdx + 1], 16);
    }
    ioACp->ang[0] = 0.0f;
    ioACp->ang[1] = 0x1.921fb6p+1f;
    ioACp->ang[2] = 0.0f;
    ioACp->ang[3] = 1.0f;
}

void DF_M_AC_SetCirclePos(stMenuAC *ioACp, int inIdx, float unused, float inRate)
{
    (void)unused;
    const float (*P)[4] = mac_circle_pos;
    ioACp->flag.rotate = 0;
    if (!(inRate <= 0.0f))
        mac_circle_lerp(ioACp, P[inIdx + 1], P[inIdx + 2], inRate);
    else if (inRate < 0.0f)
        mac_circle_lerp(ioACp, P[inIdx + 1], P[inIdx], -inRate);
    else
        memcpy(ioACp->pos, P[inIdx + 1], 16);
    if (MenuMng.pTopUseAC != ioACp) {
        ioACp->ang[0] = 0.0f;
        ioACp->ang[1] = 0x1.921fb6p+1f;
        ioACp->ang[2] = 0.0f;
        ioACp->ang[3] = 1.0f;
    }
}

/* load both players' three ACs from the player info (player 0 AC 0 also from the mission table) and rotate the
 * M_ACp views to the current AC */
void DF_M_AC_SetAll(void)
{
    PS2_FRAME(0x80);
    for (int p = 0; p < 2; p++) {
        for (int n = 0; n < 3; n++) {
            stMenuAC *acp = mac_table(p, n);
            acp->num = (short)n;
            acp->player = (char)p;
            DF_M_AC_GetPlayerInfo(p, n);
            if (p == 0 && n == 0) DF_M_AC_SetMissAC(0, acp);
        }
        MenuMng.player[p].ac_idx = PlayerInfo_GetCurrentAcNo(p);
        int cur = MenuMng.player[p].ac_idx;
        for (int k = 0; k < 3; k++) {
            M_ACp[p][k] = M_ACTable[p] + cur;
            cur = cur + 1 < 3 ? cur + 1 : 0;
        }
    }
}

/* carve the mission table (unaligned: the first DF_MFUNC_Aligned result is dropped) and the 2 x 3 AC records out of
 * the menu work buffer */
unsigned char *DF_M_AC_InitWork(unsigned char *ioBuff)
{
    PS2_FRAME(0x40);
    DF_MFUNC_Aligned(ioBuff, 0x10);   /* result dropped (original) */
    M_ACMissionTable = (struct_d8c9564a *)ioBuff;
    DF_MFUNC_ClearMemory(M_ACMissionTable, 0x14f);
    unsigned char *p = ioBuff + 0x53c;
    for (int pl = 0; pl < 2; pl++) {
        p = (unsigned char *)DF_MFUNC_Aligned(p, 0x40);
        M_ACTable[pl] = (stMenuAC *)p;
        DF_MFUNC_ClearMemory(M_ACTable[pl], 0x2e8);
        p += 0xba0;
    }
    return p;
}

/* all six assemblies = the default AC (inAdp's part ids) with the default colours; then the default garage stock
 * (or every part in the debug mode) */
void DF_M_AC_Init(StAcData *inAdp)
{
    PS2_FRAME(0x50);
    pAcTm2Buff = (unsigned char *)ReplayBuffer;
    for (int pl = 0; pl < 2; pl++)
        for (int n = 0; n < 3; n++) {
            AcAssemblyPartsSt *wpp = PlayerInfo_GetAssemblyInfo(pl, n);
            unsigned char *w = (unsigned char *)wpp;
            memcpy(w, inAdp, 0x20);   /* the 16 part ids (lh / sh) */
            wpp->option = 0;
            wpp->use_slot = 0;
            unsigned char *d = (unsigned char *)DF_MDATA_GetData(MenuMng.dataTbl[0], 2);
            memcpy(w + 0x20, d + 0x10, 0xd0);
            for (int o = 0x70; o <= 0xe0; o += 0x10) w[o] = 0;
        }
    if (gMManageCtr.fPartsAll) {
        DF_MDBG_PartsAll();
    } else {
        DF_MPART_SetAsmDefault();
        DF_MPART_SetShopDefault();
    }
}

/* ---- group G7: packet / GS drivers (phase 2 step 2 group 5b) ---- */

/* the AC's VU1 model packets: visible parts, opaque ones first, then the see-through ones; a lone part gets a TEST_1
 * register first. Camouflage is re-applied to the four paint blocks (kind 0..3) before each part's packet */
void DF_M_AC_OneACMakeVu1Packet(stMenuAC *inACp, int inIndex)
{
    PS2_FRAME(0xc0);
    if (!inACp->flag.draw) return;
    if (inACp->alpha.cur == 0) return;
    unsigned int *dmap = MPktCurrentp;
    _stMModel *list[21];
    memset(list, 0, sizeof list);
    int n = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < 21; i++) {
            _stMModel *p = inACp->cur_part[i];
            if (p && p->flag.draw && p->flag.see_through == pass) list[n++] = p;
        }
    int count = 0;
    for (int i = 0; i < 21; i++)
        if (list[i]) count++;
    if (count == 1) dmap = DF_MPKT_MakeGSRegister(dmap, 6, 0x8000000001400000ull);
    for (int s = 0; s < 21; s++) {
        _stMModel *p = list[s];
        if (!p) continue;
        int k = p->id / 1000;
        if (k == 3 || k == 2 || k == 1 || k == 0) DF_M_AC_SetCamoPettern(inACp, p, k);
        ps2_rq_tag_packet(dmap, RQ_K_MODEL, RQ_OBJID(RQ_OC_AC_PART, 0x10000u | (unsigned int)inACp->num << 8 | s),
                          (unsigned int)p->id);   /* render tag (host only) */
        dmap = DF_MMODEL_MakeVu1PacketAC(dmap, list[s], inACp, inIndex);
    }
    ps2_rq_tag_packet(dmap, RQ_K_NONE, 0, 0);
    MPktCurrentp = dmap;
}

/* body shared by MakeEmbPacket and MakeVu1Packet (inlined twice in the original): TEX0_1 / TEX0_2 for the emblem,
 * its TM2 header, one upload per player row that has a drawn AC. 0 = menu in the HDD error state (nothing built) */
static int mac_emb_upload(void)
{
    if (MenuMng.stat == 0xc) return 0;
    unsigned int *dmap = DF_MPKT_MakeGSRegister(MPktCurrentp, 6, 0x4000000045304000ull);
    dmap = DF_MPKT_MakeGSRegister(dmap, 6, 0x6000000045304000ull);
    char *emb = EmblemTM2[(int)MenuMng.player_idx];   /* lb: signed */
    DF_EMB_MakeTm2Header(emb, 0, 0x3280, 0x3d5c);
    for (int p = 0; p < 2; p++)
        for (int i = 0; i < 3; i++) {
            if (!M_ACp[p][i]->flag.draw) continue;   /* no NULL test */
            ps2_rq_tag_packet(dmap, RQ_K_UPLOAD, RQ_OBJID(RQ_OC_TEXTURE, 0x3280), 0);   /* render tag (host only) */
            dmap = DF_MPKT_MakeTM2LoadImage(dmap, emb);
            ps2_rq_tag_packet(dmap, RQ_K_NONE, 0, 0);
            break;
        }
    MPktCurrentp = dmap;
    return 1;
}

/* emblem upload only (__MenuMakePacket2ND); MakeVu1Packet uploads it again in the same frame */
void DF_M_AC_MakeEmbPacket(void)
{
    PS2_FRAME(0x50);
    mac_emb_upload();
}

/* emblem upload, then the six ACs' model packets (index 0); the HDD error state skips both */
void DF_M_AC_MakeVu1Packet(void)
{
    PS2_FRAME(0x50);
    if (!mac_emb_upload()) return;
    for (int p = 0; p < 2; p++)
        for (int i = 0; i < 3; i++) DF_M_AC_OneACMakeVu1Packet(M_ACp[p][i], 0);
}

/* the AC's shadow VIF chain (kicked by DF_M_AC_DrawShadow into the shadow render target): the shadow screen matrix
 * times RotX(pi/2), then per drawn object of every drawn part its shadow matrix (local_world_mat moved to y 40 and
 * relative to the AC's x / z) by reference and a call of the object's shadow VIF program */
static unsigned int *__OneAC_MakeShadowPkt(unsigned int *ioDmap, stMenuAC *ioACp)
{
    PS2_FRAME(0xf0);
    alignas(16) float m[4][4];
    sceVu0RotMatrixX(m, IdentityMtx, 0x1.921fb6p+0f);
    vu_mul_matrix(m, MAC_SHADOW_SCREEN_MATRIX, m);   /* 0x1f3390: vf1-4 = shadow screen matrix, vf22-25 = m */
    ioACp->shadow_dmap = ioDmap;
    ps2_rq_tag_packet(ioDmap, RQ_K_SHADOW, RQ_OBJID(RQ_OC_AC_PART, 0x10000u | (unsigned int)ioACp->num << 8),
                      0xffff);   /* render tag (host only) */
    unsigned int *dmap = DF_MPKT_MakeUnpack(ioDmap, 0, 4, m);
    for (int i = 0; i < 21; i++) {
        _stMModel *part = ioACp->cur_part[i];
        if (!part || !part->flag.draw || !part->flag.set_mdl || !part->mdlp) continue;
        struct_45c47ec5 *desc = part->mdlp;
        for (int g = 0; g < part->mdl_num; g++, desc++) {
            if (!desc->f_draw) continue;
            OmdTableSt *st = desc->s_otp;
            if (!st) continue;
            __mmodel_object_st *obj = desc->objp;
            for (int o = 0; o < desc->nobj; o++, obj++, st++) {
                if (!obj->f_draw) continue;
                sceVu0CopyMatrix(obj->shadow_mat, obj->local_world_mat);
                obj->shadow_mat[3][1] = 40.0f;
                obj->shadow_mat[3][0] = obj->shadow_mat[3][0] - ioACp->pos[0];
                obj->shadow_mat[3][2] = obj->shadow_mat[3][2] - ioACp->pos[2];
                dmap = DF_MPKT_MakeUnpackRef(dmap, 4, 4, obj->shadow_mat);
                dmap = DF_MPKT_MakeCall(dmap, (void *)(uintptr_t)st->mstart);
            }
        }
    }
    dmap = DF_MPKT_MakeEnd(dmap);
    ps2_rq_tag_packet(dmap, RQ_K_NONE, 0, 0);
    return dmap;
}

/* the shadow boards of one AC: diag(1.34) moved to pos2, through the menu view's world_screen_mat2; pass 0 draws one
 * board, pass 1 six boards on a ring of radius 0.15 (soft edge), each a 4-vertex textured strip sampling the shadow
 * render target (TBP 0x3340) with ALPHA FIX = the shadow alpha. ST = (0 or 1) * q: the zeros are 0 * q (-0.0f behind
 * the camera); a vertex outside the guard band / depth range sets ADC on itself and the next two */
static void __OneAC_DrawShadowBoards(stMenuAC *inACp)
{
    PS2_FRAME(0x190);
    static const float base[4][4] = {
        {7.5f, -0x1.99999ap-4f, 7.5f, 1.0f}, {-7.5f, -0x1.99999ap-4f, 7.5f, 1.0f},
        {7.5f, -0x1.99999ap-4f, -7.5f, 1.0f}, {-7.5f, -0x1.99999ap-4f, -7.5f, 1.0f}};
    static const float S[4] = {0x1.570a3ep+0f, 0x1.570a3ep+0f, 0x1.570a3ep+0f, 1.0f};   /* 1.34 */
    static const float C[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    PS2_RQ_SCOPE(RQ_K_SHADOW, RQ_OBJID(RQ_OC_AC_PART, 0x10000u | (unsigned int)inACp->num << 8), 0xfffe);
    const unsigned long long tex0 = 6ull << 32 | 0x20013340;   /* TBP 0x3340 TBW 4 CT32 256 x 256 TCC */
    DF_MDRAW_KickReg(0x47, 0x51001);                                                /* TEST_1 */
    DF_MDRAW_KickReg(0x42, (unsigned long long)inACp->alpha.shadow << 32 | 0x62);   /* ALPHA_1 FIX */
    DF_MDRAW_KickReg(8, 5);                                                         /* CLAMP_1 */
    alignas(16) float M[4][4];
    for (int r = 0; r < 3; r++) {   /* vmul.xyz by S: w lanes keep the identity's */
        for (int j = 0; j < 3; j++) M[r][j] = IdentityMtx[r][j] * S[j];
        M[r][3] = IdentityMtx[r][3];
    }
    for (int j = 0; j < 4; j++) M[3][j] = IdentityMtx[3][j];
    sceVu0TransMatrix(M, M, inACp->pos2);
    vu_mul_matrix(M, MViewp->world_screen_mat2, M);   /* 0x1f38ac; stays in vf1-4 in the original */
    for (int s = 0; s < 2; s++) {
        unsigned int *P = DF_MDRAW_GetSPRBuff();
        P[0] = 0x8002; P[1] = 0x10000000; P[2] = 0xe; P[3] = 0;   /* A+D x2 */
        P[4] = 0x60; P[5] = 0; P[6] = 0x14; P[7] = 0;            /* TEX1_1 */
        P[8] = (unsigned int)tex0; P[9] = (unsigned int)(tex0 >> 32); P[10] = 6; P[11] = 0;   /* TEX0_1 */
        DF_MDRAW_GIFKickSPR(P + 12);
        int rings = s ? 6 : 1;
        for (int r = 0; r < rings; r++) {
            float ang = DF_MFUNC_AngForm(ps2_div(0x1.921fb6p+2f * (float)r, 6.0f));
            float tv[4];
            float c = ps2_cosf(ang);
            tv[1] = 0.0f;
            tv[0] = 0x1.333334p-3f * ((float)s * c);   /* 0.15; pass 0: computed, unused */
            float sn = ps2_sinf(ang);
            tv[3] = 1.0f;
            tv[2] = 0x1.333334p-3f * ((float)s * sn);
            P = DF_MDRAW_GetSPRBuff();
            P[0] = 0x8004; P[1] = 0x302e4000; P[2] = 0x412; P[3] = 0;   /* tri strip, ST / RGBAQ / XYZF2 x4 */
            unsigned int *d = P + 4;
            unsigned int flag = 0;
            for (int k = 0; k < 4; k++) {
                float corner[4], v[4], st[4];
                if (s) {
                    for (int j = 0; j < 3; j++) corner[j] = base[k][j] + tv[j];   /* vadd.xyz */
                    corner[3] = base[k][3];
                } else {
                    memcpy(corner, base[k], 16);
                }
                vu_apply(v, M, corner);
                float q = ps2_div(1.0f, v[3]);
                for (int j = 0; j < 3; j++) st[j] = C[j] * q;   /* vmulx.xyz: 0 * q keeps the sign of q */
                st[3] = C[3];
                for (int j = 0; j < 3; j++) v[j] = v[j] * q;
                v[3] = 0.0f;
                d[0] = ps2_f2u(st[(k & 1) == 0 ? 2 : 0]);
                d[1] = ps2_f2u(st[k < 2 ? 1 : 3]);
                d[2] = ps2_f2u(q);
                d[3] = 0;
                d[4] = d[5] = d[6] = d[7] = 0x80;
                int *xyz = (int *)(d + 8);
                for (int j = 0; j < 4; j++) xyz[j] = ps2_ftoi(v[j] * 16.0f);   /* vftoi4 */
                d += 12;
                if (!(xyz[0] > 0 && xyz[0] < 0x10000 && xyz[1] > 0 && xyz[1] < 0x10000 && xyz[2] > 0 &&
                      xyz[2] < 0x0ffffff0))
                    flag |= 1;
                if (flag & 0xff) d[-1] |= 0x8000;   /* ADC */
                flag = (flag << 1) & 6;
            }
            DF_MDRAW_GIFKickSPR(d);
        }
    }
}

/* the AC shadows of the garage: per drawn AC with a core, the shadow alpha (no clamp: wraps for pos.z > 28), its
 * shadow VIF chain into the 0x3340 render target, the post pass, then its shadow boards */
void DF_M_AC_DrawShadow(void)
{
    PS2_FRAME(0x60);   /* DF_KickPostReadyShadowForMenu is interpreted */
    DF_MDRAW_KickReg(0x42, 0x42);   /* ALPHA_1 */
    DF_MDRAW_KickReg(8, 5);         /* CLAMP_1 */
    for (int p = 0; p < 2; p++)
        for (int i = 0; i < 3; i++) {
            stMenuAC *acp = M_ACp[p][i];
            if (!acp->flag.draw || !acp->cur_part[1]) continue;
            float f = ps2_div(0.5f * acp->pos[2], 14.0f);
            float a = 4.0f * (1.0f - f);
            float v = a * (float)acp->alpha.cur;
            float w = ps2_div(v, 128.0f);
            unsigned int b;
            if (2147483648.0f <= w)   /* float -> unsigned conversion */
                b = (unsigned int)ps2_ftoi(w - 2147483648.0f) | 0x80000000u;
            else
                b = (unsigned int)ps2_ftoi(w);
            acp->alpha.shadow = (unsigned char)b;
            sceGsSyncPath(0, 0);
            DF_MDRAW_SetShadowFrame(0x3340);
            DF_MDRAW_KickReg(0x47, 0x3000f);   /* TEST_1 */
            DF_MPKT_KickImmediate(acp->shadow_dmap);
            sceGsSyncPath(0, 0);
            DF_KickPostReadyShadowForMenu();
            DF_MDRAW_KickReg(0x3f, 0xffffffff);   /* TEXFLUSH */
            __OneAC_DrawShadowBoards(acp);
        }
    sceGsSyncPath(0, 0);
    TexMng_InvalidateAllClutCache(&MenuTexMng);
    TexMng_InvalidateAllTexCache(&MenuTexMng);
}
