/* src/main/npc.cpp - two things: (1) npcInfo, the list of NPC "friend" models a stage loads (up to 100
 * entries of 16 bytes plus counters; the VS setup menu adds entries, Stage_Load / the friend loaders read them);
 * (2) the COM AC file read: DF_LOOP_ReadMissionComAC loads one COM AC's assembly file (cdvd id macid + 0x109a) into
 * the shared read buffer, copies it into ComData / ComWare / the player's assembly and builds the AC.
 * DF_NPC_SetupChr / ReadFrndAC / ReadFrndComData are empty stubs in the shipped game. */
#include "acnb/sdk.h"

void DF_NPC_SetupChr(void) {}
int DF_NPC_ReadFrndAC(void) { return -100; }
void DF_NPC_ReadFrndComData(void) {}

/* the COM file: ware (assembly, 0x120 bytes), comdata (0x2800), partsdata */
typedef struct struct_4ddf5af8 MissAcFile;

/* Shared tail of both COM AC readers: the file is already in g_pbufRead. Counts to CopyMemory32b are words. */
static int npc_setup_com_ac(int player, int comno, int macid, AcAssemblyPartsSt *pasmbly, MyOptionSt *pacopt)
{
    MissAcFile *MissAC = (MissAcFile *)g_pbufRead;     /* re-read after the flush */
    CopyMemory32b(&ComData[comno], &MissAC->comdata, 0xa00);
    CopyMemory32b(&ComWare[comno], &MissAC->ware, 0x48);
    CopyMemory32b(pasmbly, &MissAC->ware, 0x48);
    pacopt->plus = MissAC->ware.plus;
    return AcLoader_LoadAndSetup(g_pbufAc[player], player, macid, 1) == 0 ? -1 : 0;
}

int DF_LOOP_ReadMissionComAC(int player, int comno, int macid)
{
    PS2_FRAME(0x70);
    AcAssemblyPartsSt *pasmbly = Sortie_GetAcAssemblyForInitializer(player);
    MyOptionSt *pacopt = Sortie_GetAcOptionForInitializer(player);
    if (g_nextstage.autoRankerMake != 0 && player == 1) return LOOP_ReadAutoMakeComAC(player, comno, macid);
    int cdvdid = macid + 0x109a;
    Cdvd_GetSize2048(cdvdid);                         /* result unused */
    NL_ReadData(cdvdid, g_pbufRead, -1, 0);
    FlushCache(0);
    return npc_setup_com_ac(player, comno, macid, pasmbly, pacopt);
}

/* Auto-made ranker COM: ignores macid and picks one of three fixed ACs at random; the file's assembly is then
 * replaced with g_nextstage.asmbly[1] before the copies. */
int LOOP_ReadAutoMakeComAC(int player, int comno, int macid)
{
    (void)macid;
    PS2_FRAME(0x80);
    AcAssemblyPartsSt *pasmbly = Sortie_GetAcAssemblyForInitializer(player);
    MyOptionSt *pacopt = Sortie_GetAcOptionForInitializer(player);
    PS2_STACK(unsigned int[3], tempAcid, 0x70);
    tempAcid[0] = 6;                                    /* rodata initializer (0x25fab0) */
    tempAcid[1] = 0x14;
    tempAcid[2] = 0x2b;
    int acid = (int)tempAcid[ps2_rand() % 3];
    int cdvdid = acid + 0x109a;
    Cdvd_GetSize2048(cdvdid);
    NL_ReadData(cdvdid, g_pbufRead, -1, 0);
    FlushCache(0);
    CopyMemory32b(g_pbufRead, &g_nextstage.asmbly[1], 0x48);
    return npc_setup_com_ac(player, comno, acid, pasmbly, pacopt);
}

/* Per-entry getters: -1 for an index outside 0 .. num_all-1. */
int DF_NPCTAB_IsForce(int index)
{
    if (index < 0 || index >= npcInfo.head.num_all) return -1;
    return npcInfo.frnd[index].force;
}

int DF_NPCTAB_GetLocID(int index)
{
    if (index < 0 || index >= npcInfo.head.num_all) return -1;
    return npcInfo.frnd[index].lcid;
}

int DF_NPCTAB_GetMechId(int index)
{
    if (index < 0 || index >= npcInfo.head.num_all) return -1;
    return npcInfo.frnd[index].midx;
}

int DF_NPCTAB_GetMechType(int index)
{
    if (index < 0 || index >= npcInfo.head.num_all) return -1;
    return npcInfo.frnd[index].mtype;
}

int DF_NPCTAB_GetFrndID(int index)
{
    if (index < 0 || index >= npcInfo.head.num_all) return -1;
    return npcInfo.frnd[index].fid;
}

int DF_NPCTAB_GetChrNum(void) { return npcInfo.head.num_chr; }
int DF_NPCTAB_GetACNum(void) { return npcInfo.head.num_ac; }
int DF_NPCTAB_GetFrndNum(void) { return npcInfo.head.num_all; }

/* mtype is stored as 0 just before the switch reads it back, so only num_ac ever counts (original quirk). */
int DF_NPCTAB_Add(int fid, int midx, int lcid, int force)
{
    int n = npcInfo.head.num_all;
    if (n >= 100) return -1;
    if (fid < 0 || fid >= 100) return -2;
    if (midx < 0 || midx >= 5) return -3;
    if (lcid < 0 || lcid >= 0x20) return -4;
    struct_e0a19b41 *npc = &npcInfo.frnd[n];
    npc->fid = (short)(fid + 0x31);
    npc->midx = (char)midx;
    npc->lcid = (char)lcid;
    npc->mtype = 0;
    npc->force = (char)force;
    npcInfo.head.num_all++;
    if (force == 0) {
        switch (npc->mtype) {
        case 0: npcInfo.head.num_ac++; break;
        case 1: npcInfo.head.num_chr++; break;
        case 2: npcInfo.head.num_chr += 3; break;
        }
    }
    return 0;
}

void DF_NPCTAB_SetTBPOfs(int tbp, int cbp)
{
    npcInfo.head.cur_ofs_tbp = (short)tbp;
    npcInfo.head.cur_ofs_cbp = (short)cbp;
}

void DF_NPCTAB_Reset(void) { ZeroMemory32b(&npcInfo, 0x194); }
