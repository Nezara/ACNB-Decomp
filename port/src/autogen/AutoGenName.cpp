/* src/autogen/AutoGenName.cpp - name of an auto-generated Arena opponent AC.
 * A disc-loaded keyword database (stNameKeywordDataHead, handed over by MArenaBridge_GetKeywordList) holds 4 groups
 * of stNameKeywordDataInfo records plus their u16 character data. AutoGenName_Init (once per search) caches the group
 * pointers and clears KeywordUseFlag; AutoGenName(out) (once per search result) picks one random keyword (never from
 * group 1), stores it into one of the three clitic slots (front / centre / back) by its Attribute, fills the slots
 * still WAITing with random keywords that fit the remaining length (<= 18 characters in total), then concatenates them.
 *
 * The rand() call ORDER is the content (newlib's RNG is shared with the colour generator and everything after):
 * AutoGenName draws one value and throws it away; every clitic-count choice in the Storage variants is
 * (rand() % 0x4000) >> 14 = always 0, but it is drawn; retry loops (Free's slot pick, GetEnableKeywordGroup) draw a
 * variable number of times; SearchKeywordInfo draws once only when it has a candidate.
 * Original oddities kept: GetEnableKeywordGroup maps the used group 0 -> 1 and 1 -> 0 as "previous group" (2 / 3 are
 * ignored), its second "< 70" test is dead and it loops forever when the only enabled group equals prev; only the
 * front slot marks its keyword in KeywordUseFlag; SearchKeywordInfo's 1000-entry candidate list has no bound. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
struct stNameKeywordDataHead *s_pNameKeywordHead;   /* 0x27cfdc (gp-0x5c94) */
unsigned char KeywordUseFlag[40000];                /* 0x1eee160 OLMENU (bss) */
char *s_pKeywordData[4];                            /* 0x1ef7da0 (written by Init, never read) */
struct stNameKeywordDataInfo *s_pKeywordInfo[4];    /* 0x1ef7db0 */
#endif

/* s_EnableKeywordGroup (0x1ee4cd0): EnableKeywordGroup &= this by the stored keyword's Group;
 * s_EnableKeywordMask (0x1ee4ce0): bit of group g in EnableKeywordGroup. */
static const int agn_enable_group[4] = {0xd, 0xc, 9, 5};
static const int agn_enable_mask[4] = {1, 2, 4, 8};

/* (rand() % m) >> 14 with MWCC's signed remainder (m = n << 14; rand is never negative) */
static inline int agn_rnd(int m) { return ps2_mod32(ps2_rand(), m) >> 14; }
/* R(1): always 0, but a draw */
static inline int agn_r1(void) { return agn_rnd(0x4000); }

static inline void agn_and_enable(struct stAutoGenName *p, struct stNameKeywordDataInfo *info)
{
    p->EnableKeywordGroup &= agn_enable_group[(int)info->Group];
}

static void __AutoGenName_MakeKeywordData(unsigned short *pKeywordBuf, struct stAutoGenName *pAutoGenName)
{
    PS2_FRAME(0x60);
    int n = 0;
    for (int i = 0; i < 3; i++) {
        struct stNameKeywordDataInfo *info = pAutoGenName->KeywordData[i].pKeywordInfo;
        if (info) {
            ps2_memcpy(pKeywordBuf + n, (char *)s_pNameKeywordHead + info->DataOffset,
                       (unsigned int)(info->CharNum * 2));
            n += info->CharNum;
        }
    }
    pKeywordBuf[n] = 0;
}

static int __AutoGenName_ChkKeywordAttr_Back(char attr)
{
    return attr == 0 || attr == 3 || attr == 5 || attr == 6;
}

static int __AutoGenName_ChkKeywordAttr_Center(char attr)
{
    return attr == 0 || attr == 2 || attr == 4 || attr == 6;
}

static int __AutoGenName_ChkKeywordAttr_Front(char attr)
{
    return attr == 0 || attr == 1 || attr == 4 || attr == 5;
}

/* s_ChkKeywordAttr[cliticPos] = {Front, Center, Back} (0x1eda9c0) */
static inline int agn_chk_attr(unsigned int pos, char attr)
{
    switch (pos) {
    case 0: return __AutoGenName_ChkKeywordAttr_Front(attr);
    case 1: return __AutoGenName_ChkKeywordAttr_Center(attr);
    case 2: return __AutoGenName_ChkKeywordAttr_Back(attr);
    default: return 0;   /* outside the 3-entry table: unreachable (callers pass the slot index) */
    }
}

static inline int agn_match(struct stNameKeywordDataInfo *info, int charNum, unsigned int pos)
{
    return KeywordUseFlag[info->KeywordId] == 0 && info->CharNum == charNum && agn_chk_attr(pos, info->Attribute);
}

/* A random unused record of group `group` with CharNum == charNum that fits slot cliticPos; one rand() when there is
 * a candidate. The original keeps the candidate indices in a 1000-int stack list (sp+0xa0..0x1040) with no bound;
 * past 1000 it would write into the callers' frames but still read back what it wrote, so the host keeps the first
 * 1000 and finds a later pick by a rescan (Chk* are pure, nothing changes between the passes). */
static struct stNameKeywordDataInfo *__AutoGenName_SearchKeywordInfo(int charNum, int group, enCliticPos cliticPos)
{
    PS2_FRAME(0x1040);
    int list[1000];
    unsigned int pos = (unsigned char)cliticPos;
    int num = s_pNameKeywordHead->DataNum[group];
    int cnt = 0;
    for (int k = 0; k < num; k++) {
        if (agn_match(&s_pKeywordInfo[group][k], charNum, pos)) {
            if (cnt < 1000) list[cnt] = k;
            cnt++;
        }
    }
    if (cnt == 0) return 0;
    int r = (cnt > 0) ? agn_rnd(cnt << 14) : 0;
    int k;
    if (r < 1000) {
        k = list[r];
    } else {
        int c = 0;
        for (k = 0; k < num; k++)
            if (agn_match(&s_pKeywordInfo[group][k], charNum, pos) && c++ == r) break;
    }
    return s_pKeywordInfo[group] + k;
}

/* A random enabled group (70 % 0, 15 % 2, 15 % 3; group 1 never) that differs from the "previous" group; -1 when
 * nothing is enabled (checked after the first draw). */
static int __AutoGenName_GetEnableKeywordGroup(struct stAutoGenName *pAutoGenName)
{
    PS2_FRAME(0x30);
    int prev = -1;
    for (int i = 0; i < 3; i++) {
        struct stNameKeywordDataInfo *info = pAutoGenName->KeywordData[i].pKeywordInfo;
        if (info) {
            if (info->Group == 1) prev = 0;        /* inverted mapping (original) */
            else if (info->Group == 0) prev = 1;
        }
    }
    for (;;) {
        int r = agn_rnd(0x190000);
        int g = 0;
        if (r < 70) g = 0;
        else if (r < 70) g = 1;                    /* dead: repeats the first test (original) */
        else if (r < 85) g = 2;
        else if (r < 100) g = 3;
        if (g == prev || g == 1) continue;
        if (pAutoGenName->EnableKeywordGroup == 0) return -1;
        if (!(pAutoGenName->EnableKeywordGroup & agn_enable_mask[g])) continue;
        return g;
    }
}

/* Random length for the next WAIT slot: 0 when nothing is left; else keeps one character per WAIT slot in reserve. */
static int __AutoGenName_CalcLeftKeywordCharNum(struct stAutoGenName *pAutoGenName)
{
    PS2_FRAME(0x20);
    int left = 0x12, wait = 0;
    for (int i = 0; i < 3; i++) {
        struct stKeywordData *kd = &pAutoGenName->KeywordData[i];
        unsigned char st = (unsigned char)kd->CliticState;
        if (st == 0) continue;
        if (st == 2) wait++;
        else if (st == 1 && kd->pKeywordInfo) left -= kd->pKeywordInfo->CharNum;
    }
    if (left == 0 || wait == 0) return 0;
    int s = left - wait;
    if (s >= 2) {
        if (agn_rnd(0x50000) > 0) {
            s--;
            return ((s > 0) ? agn_rnd(s << 14) : 0) + 2;
        }
        return ((s > 0) ? agn_rnd(s << 14) : 0) + 1;
    }
    return ((s > 0) ? agn_rnd(s << 14) : 0) + 1;
}

/* Keyword length to try first for group g (left = CalcLeft's result). */
static inline int agn_first_len(int g, int left)
{
    int len = left;
    switch (g) {
    case 3: if (left >= 5) len = (ps2_rand() & 0xffff) / 0x4000 + 1; break;
    case 2: if (left >= 2) len = 1; break;
    case 1: if (left >= 5) len = (ps2_rand() & 0xffff) / 0x4000 + 1; break;   /* dead (g == 1 is skipped) */
    case 0: if (left >= 9) len = (ps2_rand() & 0x1ffff) / 0x4000 + 1; break;
    }
    return len;
}

/* Tries len, len - 1 ... 1 (Search is called at least once). */
static inline struct stNameKeywordDataInfo *agn_search_down(int len, int g, int i)
{
    struct stNameKeywordDataInfo *found;
    do {
        found = __AutoGenName_SearchKeywordInfo(len, g, (enCliticPos)(unsigned char)i);
        if (found) break;
        len--;
    } while (len > 0);
    return found;
}

/* Fills every WAIT slot: up to 4 random groups (no group twice), then groups 0 / 2 / 3 in order. */
static void __AutoGenName_GetOtherKeywordInfo(struct stAutoGenName *pAutoGenName)
{
    PS2_FRAME(0xa0);
    struct stAutoGenName *p = pAutoGenName;
    for (int i = 0; i < 3; i++) {
        struct stKeywordData *kd = &p->KeywordData[i];
        struct stNameKeywordDataInfo *found = 0;
        unsigned char st = (unsigned char)kd->CliticState;
        if (st == 1 || st == 0) continue;
        int left = __AutoGenName_CalcLeftKeywordCharNum(p);
        if (__AutoGenName_GetEnableKeywordGroup(p) == -1) return;
        /* tried[] at sp+0x90; [-1] (sp+0x8c, frame padding) would be hit by g == -1, which cannot happen here
         * (EnableKeywordGroup is non-zero after the check above): host array shifted by one */
        int tried_[5] = {0, 0, 0, 0, 0};
        int *tried = tried_ + 1;
        for (int t = 0; t < 4; t++) {
            int g = __AutoGenName_GetEnableKeywordGroup(p);
            if (g == 1) continue;
            if (tried[g]) continue;
            tried[g] = 1;
            found = agn_search_down(agn_first_len(g, left), g, i);
            if (found) break;
        }
        if (!found) {
            for (int t = 0; t < 4; t++) {
                int g = t;
                if (g == 1) continue;
                if (!(p->EnableKeywordGroup & agn_enable_mask[g])) continue;
                found = agn_search_down(agn_first_len(g, left), g, i);
                if (found) break;
            }
        }
        kd->pKeywordInfo = found;
        kd->CliticState = 1;
        if (found && i == 0 && found->KeywordId < 40000) KeywordUseFlag[found->KeywordId] = 1;
    }
}

/* Storage variants (s_StorageKeyword[Attribute], 0x1eda9a0). KeywordData[0 / 1 / 2] = front / centre / back. */
static void __AutoGenName_StorageKeyword_Back(struct stAutoGenName *pAutoGenName, struct stNameKeywordDataInfo *pKeywordInfo)
{
    PS2_FRAME(0x20);
    struct stKeywordData *kd = pAutoGenName->KeywordData;
    kd[2].pKeywordInfo = pKeywordInfo;
    kd[2].CliticState = 1;
    agn_and_enable(pAutoGenName, pKeywordInfo);
    if (kd[1].CliticState == 2 && kd[0].CliticState == 2) {
        if (agn_r1() != 0) {
            if (agn_r1() != 0) kd[0].CliticState = 0;
            else kd[1].CliticState = 0;
            pAutoGenName->CliticNum = 1;
        } else {
            pAutoGenName->CliticNum = 2;
        }
    }
}

static void __AutoGenName_StorageKeyword_Center(struct stAutoGenName *pAutoGenName, struct stNameKeywordDataInfo *pKeywordInfo)
{
    PS2_FRAME(0x20);
    struct stKeywordData *kd = pAutoGenName->KeywordData;
    kd[1].pKeywordInfo = pKeywordInfo;
    kd[1].CliticState = 1;
    agn_and_enable(pAutoGenName, pKeywordInfo);
    if (kd[2].CliticState == 2 && kd[0].CliticState == 2) {
        if (agn_r1() != 0) {
            kd[0].CliticState = 0;
            pAutoGenName->CliticNum = 1;
        } else {
            pAutoGenName->CliticNum = 2;
        }
    }
}

static void __AutoGenName_StorageKeyword_Front(struct stAutoGenName *pAutoGenName, struct stNameKeywordDataInfo *pKeywordInfo)
{
    PS2_FRAME(0x20);
    struct stKeywordData *kd = pAutoGenName->KeywordData;
    kd[0].pKeywordInfo = pKeywordInfo;
    kd[0].CliticState = 1;
    agn_and_enable(pAutoGenName, pKeywordInfo);
    if (agn_r1() != 0) {
        kd[1].CliticState = 0;
        pAutoGenName->CliticNum = 1;
    } else {
        pAutoGenName->CliticNum = 2;
    }
}

static void __AutoGenName_StorageKeyword_Center_Back(struct stAutoGenName *pAutoGenName, struct stNameKeywordDataInfo *pKeywordInfo)
{
    PS2_FRAME(0x30);
    if (agn_r1() == 0) {
        __AutoGenName_StorageKeyword_Back(pAutoGenName, pKeywordInfo);
        return;
    }
    struct stKeywordData *kd = pAutoGenName->KeywordData;   /* dead arm: centre */
    kd[1].pKeywordInfo = pKeywordInfo;
    kd[1].CliticState = 1;
    agn_and_enable(pAutoGenName, pKeywordInfo);
    if (kd[0].CliticState == 2 && kd[2].CliticState == 2) {
        if (agn_r1() != 0) {
            kd[0].CliticState = 0;
            pAutoGenName->CliticNum = 1;
        } else {
            pAutoGenName->CliticNum = 2;
        }
    }
}

static void __AutoGenName_StorageKeyword_Front_Back(struct stAutoGenName *pAutoGenName, struct stNameKeywordDataInfo *pKeywordInfo)
{
    PS2_FRAME(0x30);
    if (agn_r1() == 0) {
        __AutoGenName_StorageKeyword_Back(pAutoGenName, pKeywordInfo);
        return;
    }
    struct stKeywordData *kd = pAutoGenName->KeywordData;   /* dead arm: front */
    kd[0].pKeywordInfo = pKeywordInfo;
    kd[0].CliticState = 1;
    agn_and_enable(pAutoGenName, pKeywordInfo);
    if (agn_r1() != 0) {
        kd[1].CliticState = 0;
        pAutoGenName->CliticNum = 1;
    } else {
        pAutoGenName->CliticNum = 2;
    }
}

static void __AutoGenName_StorageKeyword_Front_Center(struct stAutoGenName *pAutoGenName, struct stNameKeywordDataInfo *pKeywordInfo)
{
    PS2_FRAME(0x30);
    struct stKeywordData *kd = pAutoGenName->KeywordData;
    if (agn_r1() != 0) {                                    /* dead arm: front */
        kd[0].pKeywordInfo = pKeywordInfo;
        kd[0].CliticState = 1;
        agn_and_enable(pAutoGenName, pKeywordInfo);
        if (agn_r1() != 0) {
            kd[1].CliticState = 0;
            pAutoGenName->CliticNum = 1;
        } else {
            pAutoGenName->CliticNum = 2;
        }
        return;
    }
    kd[1].pKeywordInfo = pKeywordInfo;                      /* centre */
    kd[1].CliticState = 1;
    agn_and_enable(pAutoGenName, pKeywordInfo);
    if (kd[0].CliticState == 2 && kd[2].CliticState == 2) {
        if (agn_r1() != 0) {
            kd[0].CliticState = 0;
            pAutoGenName->CliticNum = 1;
        } else {
            pAutoGenName->CliticNum = 2;
        }
    }
}

/* Attribute 0: a random WAIT slot (all three are WAIT on entry: one draw). */
static void __AutoGenName_StorageKeyword_Free(struct stAutoGenName *pAutoGenName, struct stNameKeywordDataInfo *pKeywordInfo)
{
    PS2_FRAME(0x30);
    struct stKeywordData *kd = pAutoGenName->KeywordData;
    int k;
    do {
        k = agn_rnd(0xc000);
    } while (kd[k].CliticState != 2);
    kd[k].pKeywordInfo = pKeywordInfo;
    kd[k].CliticState = 1;
    agn_and_enable(pAutoGenName, pKeywordInfo);
    if (k == 0) {
        if (kd[1].CliticState == 2 && kd[2].CliticState == 2) {
            if (agn_r1() != 0) {
                kd[1].CliticState = 0;
                pAutoGenName->CliticNum = 1;
            } else {
                pAutoGenName->CliticNum = 2;
            }
        }
    } else if (k == 1) {
        if (kd[0].CliticState == 2 && kd[2].CliticState == 2) {
            if (agn_r1() != 0) {
                kd[0].CliticState = 0;
                pAutoGenName->CliticNum = 1;
            } else {
                pAutoGenName->CliticNum = 2;
            }
        }
    } else if (k == 2) {
        if (kd[1].CliticState == 2 && kd[0].CliticState == 2) {
            if (agn_r1() != 0) {
                if (agn_r1() != 0) kd[1].CliticState = 0;
                else kd[0].CliticState = 0;
                pAutoGenName->CliticNum = 1;
            } else {
                pAutoGenName->CliticNum = 2;
            }
        }
    }
}

/* Uniform pick over all records except group 1's. */
static struct stNameKeywordDataInfo *__AutoGenName_GetRandKeywordInfo(void)
{
    PS2_FRAME(0x60);
    struct stNameKeywordDataHead *h = s_pNameKeywordHead;
    int tab1 = h->DataNum[0];
    int total = tab1 + h->DataNum[1] + h->DataNum[2] + h->DataNum[3];   /* running sum, same order as the asm */
    int skip = h->DataNum[1];
    int r = (total - skip > 0) ? agn_rnd((total - skip) << 14) : 0;
    if (!(r < tab1)) r += skip;
    return (struct stNameKeywordDataInfo *)((char *)h + 0x20) + (unsigned short)r;
}

int AutoGenName_Init(void)
{
    PS2_FRAME(0x10);
    s_pNameKeywordHead = (struct stNameKeywordDataHead *)MArenaBridge_GetKeywordList();
    struct stNameKeywordDataHead *h = s_pNameKeywordHead;
    s_pKeywordInfo[0] = (struct stNameKeywordDataInfo *)((char *)h + 0x20);
    for (int g = 0; g < 3; g++) s_pKeywordInfo[g + 1] = s_pKeywordInfo[g] + h->DataNum[g];
    for (int g = 0; g < 4; g++) s_pKeywordData[g] = (char *)h + h->DataOffset[g];
    ps2_memset(KeywordUseFlag, 0, 0x9c40);
    return 1;
}

void AutoGenName(short *pAutoGenName)
{
    PS2_FRAME(0x40);
    struct stAutoGenName agn;
    ps2_memset(&agn, 0, 0x20);
    agn.EnableKeywordGroup = 0xd;
    agn.KeywordData[0].CliticState = 2;
    agn.KeywordData[1].CliticState = 2;
    agn.KeywordData[2].CliticState = 2;
    ps2_rand();   /* discarded draw (original) */
    struct stNameKeywordDataInfo *info = __AutoGenName_GetRandKeywordInfo();
    if (!info) return;
    switch (info->Attribute) {   /* s_StorageKeyword[Attribute] */
    case 0: __AutoGenName_StorageKeyword_Free(&agn, info); break;
    case 1: __AutoGenName_StorageKeyword_Front(&agn, info); break;
    case 2: __AutoGenName_StorageKeyword_Center(&agn, info); break;
    case 3: __AutoGenName_StorageKeyword_Back(&agn, info); break;
    case 4: __AutoGenName_StorageKeyword_Front_Center(&agn, info); break;
    case 5: __AutoGenName_StorageKeyword_Front_Back(&agn, info); break;
    case 6: __AutoGenName_StorageKeyword_Center_Back(&agn, info); break;
    default: break;   /* slot 7 = 0 / past the table: the original jalr's garbage (unreachable) */
    }
    __AutoGenName_GetOtherKeywordInfo(&agn);
    __AutoGenName_MakeKeywordData((unsigned short *)pAutoGenName, &agn);
}
