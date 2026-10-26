/* src/autogen/PilotSearch.cpp - picks the "pilot" (ranker personality: name, colour id, logic data, class) of each
 * auto-generated Arena opponent. The pilot table is a disc file loaded by MArenaBridge_Loading: a stPilotDataHead
 * (three class lists: NumData[3] / OfsPilotInfo[3], OfsPilotName), stPilotDataInfo records and the u16 name text.
 * PilotSearch_Init caches the three list pointers once per search; PilotSearch(asmId, classFlag) collects the pilots
 * of the requested classes not yet used in this search and returns one at random; GetPilotIndex / GetPilotName turn a
 * record into its global index / copy its name. Only caller: __AsmSearch_SetSearchData.
 *
 * The rand() call order is the content (shared newlib RNG): SearchPilot draws once per candidate that passes both
 * duplicate lists (its "80 %" test is dead, both arms accept, but the draw stays), PilotSearch once for the pick.
 * Original oddities kept: asmId is never used (SearchPilot takes it as an unused 2nd argument, protofix); OfsName
 * doubles as the pilot's unique id; s_pPilotName is written, never read; a classFlag with only bits above 4 picks
 * nothing and returns 0. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
struct stPilotDataHead *s_pPilotDataHead;   /* 0x27cfe0 (gp-0x5c90) */
int s_PilotNum2;                            /* 0x27cfe4: entries in s_PilotId2 */
int s_PilotNum;                             /* 0x27cfe8: entries in s_PilotId */
short *s_pPilotName;                        /* 0x27cfec (written by Init, never read) */
int s_PilotId2[256];                        /* 0x1ef7dc0: pilots collected by the current PilotSearch call */
int s_PilotId[256];                         /* 0x1ef81c0: pilots picked in this search */
struct stPilotDataInfo *s_pPilotInfo[3];    /* 0x1ef85c0: the three class lists */
#endif

/* (rand() % (n << 14)) >> 14 with MWCC's signed remainder */
static inline int ps_rnd(int m) { return ps2_mod32(ps2_rand(), m) >> 14; }

static int __PilotSearch_SearchPilot(struct stPilotDataInfo **pPilotBuf, int asmId, struct stPilotDataInfo *pPilotData,
                                     int pilotNum)
{
    PS2_FRAME(0x60);
    (void)asmId;   /* never read */
    int matchNum = 0;
    struct stPilotDataInfo *p = pPilotData;
    for (int i = 0; i < pilotNum; i++, p++) {
        int n = s_PilotNum;
        int j = 0;
        while (j < n && s_PilotId[j] != p->OfsName) j++;
        if (j < n) continue;   /* picked by an earlier PilotSearch of this search */
        n = s_PilotNum2;
        int k = 0;
        while (k < n && s_PilotId2[k] != p->OfsName) k++;
        if (k < n) continue;   /* already collected by this call */
        int r = ps_rnd(0x190000);   /* 0..99 */
        int accept = (r < 80) ? 1 : 1;   /* (sic) both arms accept: the draw is all that remains */
        if (!accept) continue;
        *pPilotBuf++ = p;
        matchNum++;
        s_PilotId2[s_PilotNum2] = p->OfsName;
        s_PilotNum2++;
    }
    return matchNum;
}

int PilotSearch_GetPilotIndex(struct stPilotDataInfo *pPilotInfo)
{
    struct stPilotDataHead *h = s_pPilotDataHead;
    int idx = 0;
    for (int c = 0; c < 3; c++) {
        int num = h->NumData[c];
        struct stPilotDataInfo *p = s_pPilotInfo[c];
        for (int j = 0; j < num; j++, idx++, p++)
            if (p == pPilotInfo) return idx;
    }
    return -1;
}

void PilotSearch_GetPilotName(short *pNameBuf, struct stPilotDataInfo *pPilotInfo)
{
    /* source base is the head, not s_pPilotName; no length check (the caller's buffer is short[13]) */
    ps2_memcpy(pNameBuf, (char *)s_pPilotDataHead + pPilotInfo->OfsName, (unsigned int)(pPilotInfo->NumName * 2));
}

struct stPilotDataInfo *PilotSearch(int asmId, char classFlag)
{
    PS2_FRAME(0x440);
    PS2_STACK(struct stPilotDataInfo *[256], pPilotBuf, 0x40);
    int n = 0;
    ps2_memset(pPilotBuf, 0, 0x400);
    ps2_memset(s_PilotId2, 0, 0x400);
    s_PilotNum2 = 0;
    signed char cf = (signed char)classFlag;
    if (cf != 0) {
        if (cf & 1) n += __PilotSearch_SearchPilot(pPilotBuf, asmId, s_pPilotInfo[0], s_pPilotDataHead->NumData[0]);
        if (cf & 2) n += __PilotSearch_SearchPilot(pPilotBuf + n, asmId, s_pPilotInfo[1], s_pPilotDataHead->NumData[1]);
        if (cf & 4) n += __PilotSearch_SearchPilot(pPilotBuf + n, asmId, s_pPilotInfo[2], s_pPilotDataHead->NumData[2]);
    } else {   /* no class: all three */
        n += __PilotSearch_SearchPilot(pPilotBuf, asmId, s_pPilotInfo[0], s_pPilotDataHead->NumData[0]);
        n += __PilotSearch_SearchPilot(pPilotBuf + n, asmId, s_pPilotInfo[1], s_pPilotDataHead->NumData[1]);
        n += __PilotSearch_SearchPilot(pPilotBuf + n, asmId, s_pPilotInfo[2], s_pPilotDataHead->NumData[2]);
    }
    if (n == 0) return 0;
    int r = 0;
    if (n > 0) r = ps_rnd(n << 14);
    struct stPilotDataInfo *chosen = pPilotBuf[r];
    s_PilotId[s_PilotNum] = chosen->OfsName;
    s_PilotNum++;
    return chosen;
}

void PilotSearch_Init(void)
{
    PS2_FRAME(0x10);
    s_pPilotDataHead = (struct stPilotDataHead *)MArenaBridge_GetPilotData();
    struct stPilotDataHead *h = s_pPilotDataHead;
    s_pPilotInfo[0] = (struct stPilotDataInfo *)((char *)h + h->OfsPilotInfo[0]);
    s_pPilotInfo[1] = (struct stPilotDataInfo *)((char *)h + h->OfsPilotInfo[1]);
    s_pPilotInfo[2] = (struct stPilotDataInfo *)((char *)h + h->OfsPilotInfo[2]);
    s_pPilotName = (short *)((char *)h + h->OfsPilotName);
    ps2_memset(s_PilotId, 0, 0x400);
    s_PilotNum = 0;
}
