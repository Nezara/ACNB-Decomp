/* src/menu/msecretparts_mng.cpp â€” "secret" parts (OLMENU): the 71 {kind, id} parts that are unlocked by arena ranks
 * and training records rather than sold. IsSecretParts keeps them out of the default garage (DF_MPART_SetAsmDefault);
 * the Create*List functions hand out the parts a player has earned (into s_stSecretPartsMng's index list, up to 8
 * for the message) and CreateShowMessage lists them in a dialog. List index ranges: training sessions 0..29 (one per
 * category x session), total training levels 30..39, training ranks 40 / 41, category totals 42..47, level-3 ranks
 * 48..57, arena ranks 58..70. */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
unsigned char s_aSecretPartsList[71][2] = {
    {0x03, 0x24}, {0x06, 0x08}, {0x08, 0x15}, {0x0a, 0x59}, {0x0b, 0x73}, {0x0c, 0x3f}, {0x09, 0x12}, {0x04, 0x05},
    {0x00, 0x16}, {0x03, 0x1e}, {0x00, 0x0c}, {0x08, 0x0e}, {0x08, 0x11}, {0x0b, 0x7c}, {0x0c, 0x30}, {0x07, 0x07},
    {0x09, 0x24}, {0x03, 0x11}, {0x02, 0x2b}, {0x05, 0x0b}, {0x02, 0x2a}, {0x0c, 0x31}, {0x0b, 0x6f}, {0x0b, 0x28},
    {0x00, 0x18}, {0x0a, 0x43}, {0x05, 0x0f}, {0x0d, 0x19}, {0x0a, 0x4a}, {0x0b, 0x7d}, {0x09, 0x16}, {0x0c, 0x34},
    {0x09, 0x25}, {0x00, 0x17}, {0x09, 0x22}, {0x0b, 0x76}, {0x03, 0x41}, {0x0d, 0x09}, {0x00, 0x13}, {0x0c, 0x3e},
    {0x0b, 0x1d}, {0x0a, 0x64}, {0x0a, 0x66}, {0x0c, 0x44}, {0x0b, 0x44}, {0x0a, 0x60}, {0x0c, 0x41}, {0x0b, 0x1e},
    {0x0c, 0x43}, {0x0a, 0x62}, {0x0b, 0x09}, {0x0b, 0x0e}, {0x0c, 0x45}, {0x0b, 0x04}, {0x0a, 0x61}, {0x0c, 0x40},
    {0x0b, 0x52}, {0x00, 0x19}, {0x0b, 0x5f}, {0x01, 0x0f}, {0x0c, 0x37}, {0x0c, 0x05}, {0x0b, 0x7f}, {0x0a, 0x63},
    {0x0c, 0x47}, {0x0a, 0x65}, {0x0c, 0x48}, {0x0c, 0x46}, {0x0c, 0x42}, {0x0b, 0x2b}, {0x00, 0x1a},
};
/* arena rank needed (rank <= limit) for list entries 58..70 */
char s_aLimitRank[13] = {17, 13, 7, 3, 101, 100, 30, 25, 20, 15, 10, 5, 1};
/* main-ELF sdata 0x27bd70 / 0x27bd84 (nodwarf.h): the message and part-name sprintf formats */
char msecret_fmt_s_br[8] = "%s<BR>";
char msecret_fmt_s[4] = "%s";
#endif

/* kind / id compared as unsigned bytes against the int arguments */
char MSecretPartsMng_IsSecretParts(int kind, int id)
{
    int i;
    for (i = 0; i < 71; i++)
        if (kind == s_aSecretPartsList[i][0] && id == s_aSecretPartsList[i][1]) return 1;
    return 0;
}

/* Builds "<top message><BR>  <kind name>  <part name>" lines for the remembered list entries and opens a dialog with
 * them (trailing blanks of the part name cut). Returns the number of lines. */
char MSecretPartsMng_CreateShowMessage(int indexTopMessage)
{
    PS2_FRAME(0x90);
    PS2_STACK(char[0x4c], name, 0x40);            /* part name copy, 0x40..0x8b */
    PS2_STACK(unsigned char[2], pair, 0x8c);      /* {kind, id} of the current entry */
    char num = 0;
    int idx;

    if (s_stSecretPartsMng.pMessage) {
        DF_MMM_Free(0, s_stSecretPartsMng.pMessage);
        s_stSecretPartsMng.pMessage = 0;
    }
    s_stSecretPartsMng.pMessage = (char *)DF_MMM_Alloc(0, 0x800);
    s_stSecretPartsMng.index = 0;
    ps2_sprintf_s(s_stSecretPartsMng.pMessage, msecret_fmt_s_br,
                  (const char *)DF_MSTR_GetName(MenuMng.language, 0, indexTopMessage));
    for (;;) {
        if (s_stSecretPartsMng.index >= 8) {
            pair[1] = 0;
            idx = -1;
            pair[0] = 0;
        } else {
            idx = s_stSecretPartsMng.aSecretPartsListIndex[(int)s_stSecretPartsMng.index];
            if (idx != -1) {
                memcpy(pair, s_aSecretPartsList[idx], 2);
                s_stSecretPartsMng.index++;
            }
        }
        if (idx == -1) break;
        if (num != 0) strcat(s_stSecretPartsMng.pMessage, "<BR>");
        strcat(s_stSecretPartsMng.pMessage, "  ");
        strcat(s_stSecretPartsMng.pMessage, (const char *)DF_MSTR_GetName(MenuMng.language, 0, pair[0] + 0xdbf92));
        strcat(s_stSecretPartsMng.pMessage, "  ");
        ps2_sprintf_s(name, msecret_fmt_s, (const char *)DF_MSTR_GetName(MenuMng.language, 0, idx + 0xdbfec));
        for (int i = (int)strlen(name) - 1; i > 0 && name[i] == ' '; i--) name[i] = 0;
        strcat(s_stSecretPartsMng.pMessage, name);
        num++;
    }
    if (num != 0) DF_MDIALOG_Open(4, (unsigned char *)s_stSecretPartsMng.pMessage, 0, 0, 0, 0, 1, 0);
    return num;
}

/* Gives list entry idx to player 0 unless it is already owned; remembers it for the message (up to 8). */
static void msecret_add(int idx)
{
    unsigned char *e = s_aSecretPartsList[(signed char)idx];
    MUtil_AddParts(0, MPart_MpartToAcpeq(e[0]), e[1]);
    if (s_stSecretPartsMng.index < 8) {
        s_stSecretPartsMng.aSecretPartsListIndex[(int)s_stSecretPartsMng.index] = (char)idx;
        s_stSecretPartsMng.index++;
        s_stSecretPartsMng.numList++;
    }
}

static int msecret_owned(int idx)
{
    return PlayerInfo_IsExistParts(0, MPart_MpartToAcpeq(s_aSecretPartsList[idx][0]), s_aSecretPartsList[idx][1]);
}

/* same with the acpeq the caller converted before its own test */
static int msecret_owned_as(int idx, int acpeq)
{
    return PlayerInfo_IsExistParts(0, acpeq, s_aSecretPartsList[idx][1]);
}

static void msecret_reset(void)
{
    int i;
    memset(&s_stSecretPartsMng, 0, 0x10);
    for (i = 0; i < 8; i++) s_stSecretPartsMng.aSecretPartsListIndex[i] = -1;
}

/* arena: entry 58 + i for every rank limit the player's arena rank has reached */
char MSecretPartsMng_CreateArenaSecretPartsList(void)
{
    char num = 0;
    int rank, i;

    PS2_FRAME(0x70);
    msecret_reset();
    rank = g_gamedata[0].arenaBattle.rank;
    for (i = 0; i < 13; i++) {
        int idx = i + 0x3a;
        if (s_aLimitRank[i] < rank) continue;
        if (msecret_owned(idx)) continue;
        msecret_add(idx);
        num++;
    }
    return num;
}

/* training: a session with all 5 levels entered, every 15 entered levels, more than 60 % of the levels of categories
 * 0-2 / 3-4, a whole category (25 levels), every 15 levels with rank 3; 150 rank-3 levels also enable the cockpit */
char MSecretPartsMng_CreateTrainingSecretPartsList(void)
{
    PS2_FRAME(0xe0);
    int cnt[6];
    int total = 0, top = 0, n, cate, sess, lv, i, acpeq;
    char num = 0;

    memset(cnt, 0, sizeof cnt);
    msecret_reset();
    for (cate = 0; cate < 6; cate++) {
        for (sess = 0; sess < 5; sess++) {
            int idx;
            n = 0;
            for (lv = 0; lv < 5; lv++) {
                if (GetTrainingTrendLv(cate, sess, lv)->prize != 0) {
                    cnt[cate]++;
                    n++;
                    total++;
                    if (GetTrainingTrendLv(cate, sess, lv)->prize == 3) top++;
                }
            }
            idx = sess + cate * 5;
            acpeq = MPart_MpartToAcpeq(s_aSecretPartsList[idx][0]);   /* converted before the test */
            if (n == 5 && !msecret_owned_as(idx, acpeq)) {
                msecret_add(idx);
                num++;
            }
        }
    }
    for (i = 1; i < 11; i++) {
        int idx = i + 0x1d;
        acpeq = MPart_MpartToAcpeq(s_aSecretPartsList[idx][0]);
        if (total >= 15 * i && !msecret_owned_as(idx, acpeq)) {
            msecret_add(idx);
            num++;
        }
    }
    n = 0;
    for (cate = 0; cate < 3; cate++)
        for (sess = 0; sess < 5; sess++)
            for (lv = 0; lv < 5; lv++)
                if (GetTrainingTrendLv(cate, sess, lv)->prize != 0) n++;
    if (ps2_u2f(0x3f19930c) < (float)n / 75.0f && !msecret_owned(0x28)) {
        msecret_add(0x28);
        num++;
    }
    n = 0;
    for (cate = 3; cate < 5; cate++)
        for (sess = 0; sess < 5; sess++)
            for (lv = 0; lv < 5; lv++)
                if (GetTrainingTrendLv(cate, sess, lv)->prize != 0) n++;
    if (ps2_u2f(0x3f19930c) < (float)n / 50.0f && !msecret_owned(0x29)) {
        msecret_add(0x29);
        num++;
    }
    for (i = 0; i < 6; i++) {
        int idx = i + 0x2a;
        acpeq = MPart_MpartToAcpeq(s_aSecretPartsList[idx][0]);
        if (cnt[i] == 0x19 && !msecret_owned_as(idx, acpeq)) {
            msecret_add(idx);
            num++;
        }
    }
    for (i = 1; i < 11; i++) {
        int idx = i + 0x2f;
        acpeq = MPart_MpartToAcpeq(s_aSecretPartsList[idx][0]);
        if (top >= 15 * i && !msecret_owned_as(idx, acpeq)) {
            msecret_add(idx);
            num++;
        }
    }
    if (top == 150) g_gamedata[0].isEnableCockpit = 1;
    return num;
}

void MSecretPartsMng_Cleanup(void)
{
    if (s_stSecretPartsMng.pMessage) {
        DF_MMM_Free(0, s_stSecretPartsMng.pMessage);
        s_stSecretPartsMng.pMessage = 0;
    }
}
