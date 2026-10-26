/* src/menu/mpart.cpp - the menu's parts database (OLMENU). The parts file holds 14 part kinds (MPartKindTblp[kind]:
 * a 0x10-byte head {num} followed by num 0x20-byte entries: +0x09 name, +0x17 true id "tid" (the id PlayerInfo and the
 * assembly use), +0x18 category, +0x19 sub category, +0x1a third sort key, +0x1b bit 7 back-weapon ("wback") flag and
 * low 7 bits OP slot, +0x1c price). Menu ids are kind * 1000 + table index; Remap sorts each kind's entries (category,
 * sub category, key 3) and builds MPartTid2Idx (tid -> index). The file maps ids <-> tids <-> names / categories /
 * LSNs / connect parameters, builds the garage / shop / repository id lists (MakeIDTable), buys and sells parts and
 * fills the default shop and garage. Entry 0 of each kind is the "none" part.
 * Menu kinds -> acpeq (PlayerInfo / assembly slot): 0..10 -> 0..10, 11 -> 12, 12 -> 13, 13 -> 16 (acpeq 11 = back
 * weapons also usable as arm units, 14 / 15 = the extra 12 / 13 slots). DF_MPART_MakeIDTable_acpeq is a function
 * static used as a hidden argument: the Nx wrappers set it to override the kind -> acpeq lookup. */
#include <string.h>
#include "acnb/hooks.h"
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
struct stMpartKindHead *MPartKindTblp[14];
unsigned char *MPartTid2Idx[14];
int DF_MPART_MakeIDTable_acpeq = -1;
char mpart_fmt_s[4] = "%s";
char mpart_empty_str[4] = "";
char mpart_fmt_sys[8] = "SYS-%d";
#endif

/* menu kind -> acpeq (OLMENU 0x1edfb20) */
static const int s_mpart_kind2acpeq[14] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 16};
/* acpeq -> menu kind (OLMENU 0x1edfb60) */
static const int s_mpart_acpeq2kind[17] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 11, 12, 11, 12, 13};
/* {obj_id, nobj, 0, 0} per [MI2 kind][part kind 0..4] (OLMENU 0x1edfaa0) */
static const unsigned char s_mpart_connect_prm[6][5][4] = {
    {{4, 1}, {3, 1}, {5, 8}, {0xe, 9}, {0, 0}},  {{4, 1}, {3, 1}, {5, 8}, {0xe, 10}, {0, 0}},
    {{4, 1}, {3, 1}, {5, 8}, {0xe, 12}, {0, 0}}, {{4, 1}, {3, 1}, {5, 8}, {0xe, 13}, {0, 0}},
    {{4, 1}, {3, 1}, {5, 8}, {0xe, 1}, {0, 0}},  {{4, 1}, {3, 1}, {5, 8}, {0xe, 1}, {0, 0}},
};

/* entry i of a kind table */
static inline unsigned char *mpart_entry(struct stMpartKindHead *head, int i)
{
    return (unsigned char *)head + 0x10 + i * 0x20;
}

/* tid of index i (i <= num: entry num, one past the table, is read too), -1 above */
static inline int mpart_tid(struct stMpartKindHead *head, int i)
{
    return head->num < i ? -1 : mpart_entry(head, i)[0x17];
}

static inline int mpart_is_wback(const unsigned char *e)
{
    return (signed char)e[0x1b] < 0;
}

/* first entry of the back-weapon table (kind 10) with tid == id % 1000: its wback flag, 0 when there is none */
static int mpart_wback10_by_tid(int id)
{
    struct stMpartKindHead *head = MPartKindTblp[10];
    int i = id % 1000;
    int j;
    for (j = 0; j < head->num; j++)
        if (mpart_entry(head, j)[0x17] == i) return mpart_is_wback(mpart_entry(head, j));
    return 0;
}

/* eAcPartsEquip 15 / 14 (the extra slots) -> 13 / 12 */
static inline int mpart_fold_acpeq(int acpeq)
{
    if (acpeq == 0xf) return 0xd;
    if (acpeq == 0xe) return 0xc;
    return acpeq;
}

/* the assembly (MenuMng.datAsm idParts) has part tid in slot acpeq; slots 10 / 11 (back weapons) are interchangeable */
static int mpart_is_equipped(int acpeq, int tid)
{
    short *asmp = (short *)MenuMng.datAsm;
    int a = mpart_fold_acpeq(acpeq & 0xff) & 0xff;
    if (a == 0xb || a == 0xa) return asmp[0xb] == tid || asmp[0xa] == tid;
    return asmp[a] == tid;
}

/* Drops the arm-unit copies (acpeq 11) of back-only weapons from the garage of player. */
void DF_MPART_CorrectPlayerParts(int player)
{
    PS2_FRAME(0x30);
    int id;
    for (id = 1; id < 0x100; id++) {
        if (!PlayerInfo_IsExistParts(player, 0xb, id)) continue;
        if (mpart_wback10_by_tid(id)) PlayerInfo_SetExistPartsGarage(player, 0xb, id, 0);
    }
}

/* Price of the assembly's parts (owned in the garage or repository): used parts count 90 %. Slot 16 (option) always
 * prices index 0x20 of the option table. The price of a part not found is read from the entry past the table. */
int Mpart_GetAssmblyPartsPrice(int player, struct AcAssemblyPartsSt *pasmbly)
{
    PS2_FRAME(0xd0);
    int total = 0;
    int acpeq;
    for (acpeq = 0; acpeq < 0x11; acpeq++) {
        int kind = s_mpart_acpeq2kind[acpeq];
        int id = acpeq == 0x10 ? 0x20 : pasmbly->idParts[acpeq];
        if (id < 0 || id >= 0x100) continue;
        if (!PlayerInfo_IsExistPartsGarage(player, acpeq, id) && !PlayerInfo_IsExistPartsReposit(player, acpeq, id))
            continue;
        struct stMpartKindHead *head = MPartKindTblp[kind];
        unsigned char *e = mpart_entry(head, 0);
        int j;
        for (j = 0; j < head->num && j != id; j++) e += 0x20;
        if (PlayerInfo_IsPartsUsed(player, acpeq, id))
            total += (unsigned int)(*(int *)(e + 0x1c) * 9) / 10;
        else
            total += *(int *)(e + 0x1c);
    }
    return total;
}

/* Is part_id (menu id) of slot acpeq the one in the current assembly? (tid 0 = none: never) */
int DF_MPART_IsInitialPartIdx(eAcPartsEquip acpeq, int part_id)
{
    int kind = s_mpart_acpeq2kind[acpeq & 0xff];
    int tid = mpart_tid(MPartKindTblp[kind], part_id % 1000 % 1000) % 1000;
    if (tid == 0) return 0;
    return mpart_is_equipped(acpeq, tid);
}

/* Bit (tid - 1) set for every set bit (index - 1) of the option-part (kind 13) index mask, indices 1..31. */
unsigned int DF_MPART_Option_Index2TrueID(unsigned int tid)
{
    struct stMpartKindHead *head = MPartKindTblp[13];
    unsigned int result = 0;
    int i;
    for (i = 1; i < 0x20; i++) {
        if (!((tid >> (i - 1)) & 1)) continue;
        int t = mpart_tid(head, i % 1000) - 1;
        if (t >= 0 && t < 0x1f) result |= 1u << t;
    }
    return result;
}

int DF_MPART_GetRepositACPEQ(int acpeq)
{
    return mpart_fold_acpeq(acpeq);
}

int MPart_AcpeqToMpart(int acpeq)
{
    return s_mpart_acpeq2kind[acpeq];
}

int MPart_MpartToAcpeq(int kind)
{
    return s_mpart_kind2acpeq[kind];
}

int DF_MPART_TrueID2Index(int kind, int id)
{
    return MPartTid2Idx[kind][id] + kind * 1000;
}

int DF_MPART_Index2TrueID(int kind, int id)
{
    return mpart_tid(MPartKindTblp[kind], id % 1000);
}

/* acpeq of a buy / sell: the Nx override, else the kind's */
static inline int mpart_acpeq(int kind)
{
    return DF_MPART_MakeIDTable_acpeq != -1 ? DF_MPART_MakeIDTable_acpeq : s_mpart_kind2acpeq[kind];
}

/* Moves inNum of part inPartID (menu id) from the shop to the garage. Counts are clamped to 0..99 but only "any"
 * is stored: the shop keeps the part while 1 - inNum > 0, the garage gets it when (owned ? 1 : 0) + inNum > 0. */
void DF_MPART_Buy(int inPlayer, int inPartID, int inNum)
{
    PS2_FRAME(0x160);
    int kind = inPartID / 1000;
    int t = mpart_tid(MPartKindTblp[kind], inPartID % 1000 % 1000) + kind * 1000;
    int tid = t % 1000;
    int tkind = t / 1000;
    int garage = PlayerInfo_IsExistPartsGarage(inPlayer, mpart_acpeq(tkind), tid) != 0;
    int shop = PlayerInfo_IsExistPartsShop(inPlayer, mpart_acpeq(tkind), tid) != 0;
    if (!shop) return;
    int n = shop - inNum;
    if (n < 0) n = 0;
    if (n >= 100) n = 99;
    PlayerInfo_SetExistPartsShop(inPlayer, mpart_acpeq(tkind), tid, n > 0);
    n = garage + inNum;
    if (n < 0) n = 0;
    if (n >= 100) n = 99;
    PlayerInfo_SetExistPartsGarage(inPlayer, mpart_acpeq(tkind), tid, n > 0);
}

/* Table index of menu id inID (0 when absent or no table). */
int DF_MPART_PartID2TableIdx(struct stMPartIdTable *inTablep, int inID)
{
    if (!inTablep) return 0;
    int n = inTablep->num;
    int j;
    for (j = 0; j < n; j++)
        if (inTablep->cell[j].id == inID) return j;
    return 0;
}

/* Fills outTablep with the menu ids of kind inPartKind that player inPlayer has: inType low 16 bits 0 garage
 * (+ "none" first unless the kind is 1 or 13), 1 shop, 2 garage, 3 shop (+ "none" when empty), 4 / 5 garage + the
 * repository with the equipped part left out (4: "none" when empty), 6 all; bit 16 adds the repository. inPartEq 11
 * (arm-unit slot) checks back-only weapons as acpeq 10; slots 14 / 15 list back-only weapons only. */
void DF_MPART_MakeIDTable(struct stMPartIdTable *outTablep, int inType, int inPlayer, int inPartKind, int inPartEq)
{
    PS2_FRAME(0x140);
    if (!outTablep) return;
    int flags = inType & 0xffff0000;
    int type = inType & 0xffff;
    if (type == 4 || type == 5) flags |= 0x10000;
    DF_MFUNC_ClearMemory(outTablep, 0x104);
    outTablep->player = inPlayer;
    outTablep->type = type;
    outTablep->kind = inPartKind;
    int acpeq = DF_MPART_MakeIDTable_acpeq;
    if (acpeq == -1) acpeq = s_mpart_kind2acpeq[inPartKind];
    acpeq = mpart_fold_acpeq(acpeq);

    struct struct_2ed2ff5f *cell = outTablep->cell;
    if (inPartKind != 1 && inPartKind != 13 && type == 0) {
        outTablep->cell[0].id = inPartKind * 1000;
        cell++;
        outTablep->num++;
    }
    int repo = flags & 0x10000;
    int idx;
    for (idx = 1; idx < MPartKindTblp[inPartKind]->num; idx++) {
        struct stMpartKindHead *head = MPartKindTblp[inPartKind];
        int tid = mpart_tid(head, idx % 1000);
        int have = 0, selected = 0;
        int asback = inPartEq == 0xb && mpart_is_wback(mpart_entry(head, idx));
        switch (type) {
        case 0: case 2: case 4: case 5:
            have = PlayerInfo_IsExistPartsGarage(inPlayer, asback ? 10 : acpeq, tid) != 0;
            if (type == 4 || type == 5) {
                int t = tid % 1000;
                if (t != 0 && mpart_is_equipped(inPartEq, t)) {
                    selected = 1;
                    have = 0;
                }
            }
            break;
        case 1: case 3:
            have = PlayerInfo_IsExistPartsShop(inPlayer, asback ? 10 : acpeq, tid) != 0;
            break;
        case 6:
            have = 1;
            break;
        }
        if (!selected && !have && repo) {
            if (asback) have = PlayerInfo_IsExistPartsReposit(inPlayer, 10, tid) != 0;
            else have = (unsigned char)PlayerInfo_IsExistPartsReposit(inPlayer, acpeq, tid);
        }
        if ((inPartEq == 0xe || inPartEq == 0xf) && !mpart_is_wback(mpart_entry(MPartKindTblp[inPartKind], idx)))
            continue;
        if ((unsigned char)have > 0) {
            cell->id = inPartKind * 1000 + idx;
            cell->num = have;
            cell++;
            outTablep->num++;
        }
    }
    if (outTablep->num != 0) return;
    if (type == 3) {
        cell->id = inPartKind * 1000;
        cell++;
        outTablep->num++;
    }
    if (type == 4) {
        cell->id = inPartKind * 1000;
        outTablep->num++;
    }
}

/* Model connection of a part (core / arm / leg ... kinds 0..4) for MI2 kind inMI2Kind: -1 on NULL, 0 for kind >= 5. */
int DF_MPART_GetConnectPrm(int inMI2Kind, struct _stMModel *inPartp, struct struct_9cdd2847 *outCpp)
{
    if (!inPartp || !outCpp) return -1;
    int kind = inPartp->id / 1000;
    if (kind < 0 || kind >= 5) return 0;
    const unsigned char *p = s_mpart_connect_prm[inMI2Kind][kind];
    outCpp->obj_id = p[0];
    outCpp->nobj = p[1];
    outCpp->unused[0] = p[2];
    outCpp->unused[1] = p[3];
    return 1;
}

/* category of a menu id (0 when the index is out of the table) */
static int mpart_category(int id)
{
    struct stMpartKindHead *head = MPartKindTblp[id / 1000];
    int i = id % 1000;
    return i < head->num ? mpart_entry(head, i)[0x18] : 0;
}

/* MI2 kind of a leg category: 5 -> 1, 9 -> 2, 0xb -> 3, 0xd -> 4, 0xf -> 5, else 0 */
int DF_MPART_GetConnectMI2Kind(int inCoreID, int inArmID, int inLegID)
{
    switch (mpart_category(inLegID)) {
    case 5: return 1;
    case 9: return 2;
    case 0xb: return 3;
    case 0xd: return 4;
    case 0xf: return 5;
    }
    return 0;
}

int DF_MPART_GetConnectMI2ID(int inCoreID, int inArmID, int inLegID)
{
    int r = 1;
    switch (mpart_category(inCoreID)) {
    case 7: break;
    case 1: r += 0x18; break;
    case 4: r += 0x30; break;
    }
    switch (mpart_category(inArmID)) {
    case 1: r += 8; break;
    case 3: r += 0x10; break;
    }
    switch (mpart_category(inLegID)) {
    case 0xf: r += 7; break;
    case 0xd: r += 6; break;
    case 0xb: r += 5; break;
    case 9: r += 3; break;
    case 7: r += 4; break;
    case 5: break;
    case 3: r += 2; break;
    case 1: r += 1; break;
    }
    return r;
}

int DF_MPART_GetSubCategory(int inID)
{
    struct stMpartKindHead *head = MPartKindTblp[inID / 1000];
    int i = inID % 1000;
    return i < head->num ? mpart_entry(head, i)[0x19] : 0;
}

int DF_MPART_GetCategory(int inID)
{
    return mpart_category(inID);
}

/* LSN of the part's data file: per-kind base + tid - 1 (the kind table is read unchecked) */
int DF_MPART_ID2LsnID(int inID)
{
    static const int base[14] = {0xa474, 0xa5a0, 0xa6cc, 0xa7f8, 0xa924, 0xaa50, 0xab7c,
                                 0xaca8, 0xadd4, 0xaf00, 0xb02c, 0xb158, 0xb284, 0xb3b0};
    int kind = inID / 1000;
    int lsn = (unsigned int)kind < 14 ? base[kind] : 0xa474;
    return lsn + (mpart_tid(MPartKindTblp[kind], inID % 1000 % 1000) - 1);
}

/* entry of index i in kind's table (i == 0 or not found: NULL) */
static unsigned char *mpart_find_index(int kind, int i)
{
    struct stMpartKindHead *head = MPartKindTblp[kind];
    unsigned char *e = mpart_entry(head, 0);
    int j;
    for (j = 0; j < head->num && j != i; j++) e += 0x20;
    if (j == 0 || j >= head->num) return 0;
    return e;
}

/* OP slot of an option part (kind 13), 7-bit signed; -1 for other kinds / none */
int DF_MPART_GetOPSlot(int inID)
{
    if (inID / 1000 != 13) return -1;
    unsigned char *e = mpart_find_index(13, inID % 1000);
    if (!e) return -1;
    return (int)((unsigned int)e[0x1b] << 25) >> 25;
}

int DF_MPART_GetOPPrice(int inID)
{
    int kind = inID / 1000;
    if (kind < 0 || kind >= 14) return -1;
    unsigned char *e = mpart_find_index(kind, inID % 1000);
    if (!e) return -1;
    return *(int *)(e + 0x1c);
}

char *DF_MPART_GetNamePointer(int inID)
{
    int kind = inID / 1000;
    if (kind < 0 || kind >= 14) return 0;
    unsigned char *e = mpart_find_index(kind, inID % 1000);
    if (!e) return 0;
    return (char *)e + 9;
}

/* back-only weapon by menu id (kind 10 only; the index is not range-checked) */
int DF_MPART_IsWbackWepIdx(int inID)
{
    if (inID / 1000 != 10) return 0;
    return mpart_is_wback(mpart_entry(MPartKindTblp[10], inID % 1000));
}

/* back-only weapon by tid: -1 (not 1) when set */
int DF_MPART_IsWbackWep(int inID)
{
    return -mpart_wback10_by_tid(inID);
}

/* Name of a part by kind * 1000 + tid into outName (may be NULL): 1 when found, 0 for none / not found ("") and for
 * kind 15 ("SYS-<tid>"), -1 for another kind out of range. */
int DF_MPART_PartsID2Name(int inID, char *outName)
{
    PS2_FRAME(0x10);
    int kind = inID / 1000;
    int i = inID % 1000;
    if (kind == 0xf) {
        if (outName) ps2_sprintf_d(outName, mpart_fmt_sys, i);
        return 0;
    }
    if (kind < 0 || kind >= 14) return -1;
    struct stMpartKindHead *head = MPartKindTblp[kind];
    unsigned char *e = mpart_entry(head, 0);
    int j;
    for (j = 0; j < head->num && e[0x17] != i; j++) e += 0x20;
    if (j == 0 || j >= head->num) {
        if (outName) ps2_sprintf_s(outName, mpart_fmt_s, mpart_empty_str);
        return 0;
    }
    if (outName) ps2_sprintf_s(outName, mpart_fmt_s, (const char *)e + 9);
    return 1;
}

/* As PartsID2Name for a menu id (kind * 1000 + index). */
int DF_MPART_ID2Name(int inID, char *outName)
{
    PS2_FRAME(0x10);
    int kind = inID / 1000;
    int i = inID % 1000;
    if (kind == 0xf) {
        if (outName) ps2_sprintf_d(outName, mpart_fmt_sys, i);
        return 0;
    }
    if (kind < 0 || kind >= 14) return -1;
    unsigned char *e = mpart_find_index(kind, i);
    if (!e) {
        if (outName) ps2_sprintf_s(outName, mpart_fmt_s, mpart_empty_str);
        return 0;
    }
    if (outName) ps2_sprintf_s(outName, mpart_fmt_s, (const char *)e + 9);
    return 1;
}

void DF_MPART_ExtPartsDispose(void)
{
}

/* Puts the default shop stock (file 0xbd5: per kind 32 tids, kind 10 followed by 32 unused bytes) into the shops of
 * players 1 and 2; back weapons that are not back-only also go to the arm-unit shop (acpeq 11). */
void DF_MPART_SetShopDefault(void)
{
    PS2_FRAME(0x8e0);
    PS2_STACK(unsigned char[0x800], buf, 0xe0);
    if (!File_Read(0xbd5, 0x1e0, (char *)buf, 1)) return;
    int player;
    for (player = 0; player < 2; player++) {
        const unsigned char *p = buf;
        int kind;
        for (kind = 0; kind < 14; kind++) {
            int acpeq = s_mpart_kind2acpeq[kind];
            int n;
            for (n = 0; n < 0x20; n++, p++) {
                int tid = *p;
                if (tid == 0) continue;
                if (DF_MPART_ID2Name(MPartTid2Idx[kind][tid] + kind * 1000, 0) != 1) continue;
                PlayerInfo_AddShopParts(player, acpeq, tid);
                if (acpeq == 10 && !mpart_wback10_by_tid(tid)) PlayerInfo_AddShopParts(player, 0xb, tid);
            }
            if (kind == 10) p += 0x20;
        }
    }
}

/* Puts every named part (tids 0..127 of each kind) that is not a secret part into the garages of players 1 and 2. */
void DF_MPART_SetAsmDefault(void)
{
    PS2_FRAME(0xa0);
    int player, kind, id;
    for (player = 0; player < 2; player++)
        for (kind = 0; kind < 14; kind++)
            for (id = 0; id < 0x80; id++)
                if (!MSecretPartsMng_IsSecretParts(kind, id) && DF_MPART_PartsID2Name(kind * 1000 + id, 0))
                    MUtil_AddParts(player, s_mpart_kind2acpeq[kind], id);
    if (hook_mpart_asm_default) hook_mpart_asm_default();   /* port hook (game.unlock_parts) */
}

/* qsort comparator of the entries: category, sub category, then key 3 - which never answers "greater" (0 / -1). */
static int __MPART_TableQsort(void *v1, void *v2)
{
    const unsigned char *a = (const unsigned char *)v1, *b = (const unsigned char *)v2;
    if (b[0x18] < a[0x18]) return 1;
    if (a[0x18] < b[0x18]) return -1;
    if (b[0x19] < a[0x19]) return 1;
    if (a[0x19] < b[0x19]) return -1;
    if (b[0x1a] < a[0x1a]) return 1;
    return -(a[0x1a] < b[0x1a]);
}

/* Takes the loaded parts file (14 kind tables after a 0x10-byte header): sorts each kind (newlib qsort) and builds its
 * tid -> index table (0xff bytes each) in wkBuff. Returns the end of the tid tables. */
unsigned char *DF_MPART_Remap(void *inBuff, void *wkBuff)
{
    PS2_FRAME(0x70);
    struct stMpartKindHead *head = (struct stMpartKindHead *)((unsigned char *)inBuff + 0x10);
    unsigned char *wk = (unsigned char *)wkBuff;
    int kind;
    for (kind = 0; kind < 14; kind++) {
        MPartKindTblp[kind] = head;
        ps2_qsort(mpart_entry(head, 0), head->num, 0x20, GFN(__MPART_TableQsort));
        MPartTid2Idx[kind] = wk;
        memset(MPartTid2Idx[kind], 0, 0xff);
        wk += 0xff;
        int j;
        for (j = 0; j < head->num; j++) MPartTid2Idx[kind][mpart_entry(head, j)[0x17]] = j;
        head = (struct stMpartKindHead *)mpart_entry(head, head->num);
    }
    return wk;
}

void DF_MPART_Init(void)
{
}

/* Buy with acpeq as the slot (11 = arm unit: back-only weapons count as acpeq 10). */
void DF_MPART_BuyNx(int inPlayer, int inPartID, int acpeq, int inNum)
{
    PS2_FRAME(0x10);
    if (acpeq == 0xb && mpart_is_wback(mpart_entry(MPartKindTblp[inPartID / 1000], inPartID % 1000))) acpeq = 10;
    DF_MPART_MakeIDTable_acpeq = acpeq;
    DF_MPART_Buy(inPlayer, inPartID, inNum);
    DF_MPART_MakeIDTable_acpeq = -1;
}

/* Puts a part back into the shop: unused, tune levels 0..4 cleared. */
void DF_MPART_SellNx(int inPlayer, int inPartID, int acpeq)
{
    PS2_FRAME(0x50);
    if (acpeq == 0xb && DF_MPART_IsWbackWepIdx(inPartID)) acpeq = 10;
    int kind = inPartID / 1000;
    int i = inPartID % 1000 % 1000;
    PlayerInfo_SetExistPartsShop(inPlayer, acpeq, mpart_tid(MPartKindTblp[kind], i), 1);
    int tid = mpart_tid(MPartKindTblp[kind], i);
    PlayerInfo_SetPartsUsed(inPlayer, acpeq, tid, 0);
    int lv;
    for (lv = 0; lv < 5; lv++) PlayerInfo_SetPartsTuneLv(inPlayer, acpeq, tid, lv, 0);
}

/* Is the part (id % 1000 used as the PlayerInfo id) in the garage (inType 0) or shop? Leaves the acpeq static set
 * to acpeq (only -1 resets it). */
int DF_MPART_GetPartCountNx(int inType, int inPlayer, int inPartID, int acpeq)
{
    PS2_FRAME(0x50);
    DF_MPART_MakeIDTable_acpeq = acpeq;
    int i = inPartID % 1000;
    if (acpeq == -1) acpeq = s_mpart_kind2acpeq[inPartID / 1000];
    if (inType == 0) return PlayerInfo_IsExistPartsGarage(inPlayer, acpeq, i) != 0;
    return PlayerInfo_IsExistPartsShop(inPlayer, acpeq, i) != 0;
}

/* MakeIDTable with inPartEq also as the acpeq override. */
void DF_MPART_MakeIDTableNx(struct stMPartIdTable *outTablep, int inType, int inPlayer, int inPartKind, int inPartEq)
{
    PS2_FRAME(0x10);
    DF_MPART_MakeIDTable_acpeq = inPartEq;
    DF_MPART_MakeIDTable(outTablep, inType, inPlayer, inPartKind, inPartEq);
    DF_MPART_MakeIDTable_acpeq = -1;
}
