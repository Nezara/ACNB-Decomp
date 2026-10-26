/* src/menu/mtrend_mng.cpp - renderer of the "trend" (training / arena combat record) pages shown by
 * menu/mtraining_trend: SetConfig stores the selection and the _training_trend_st record in s_stTrendMng (mode 1
 * also rewrites the record's weapon ids from AC 0's parts), Play draws the current page through a page table
 * (Page1 weapons used, Page2 movement records + other movement data, Page3 per-weapon average + other data; Page4 /
 * Page5 are empty) and pages with pad down / up (SE 0x3e9); IsChangePage / GetArrowState feed the unit-tab arrows.
 * Times are frames at 60 / s, shown as "minutes:seconds:centiseconds" (OutputDataForTime, inlined in Page2 / Page3).
 * Original quirks kept: SetConfig does not reset page / page_past (a page survives a revisit); SetConfig's mode-1
 * remap mutates the live game-data record every time a trend page opens; each page title is drawn in the yellow
 * colour word, the rows in the default one; skipped weapon slots do not advance the row; Page2's second loop
 * strcmp's the info string without a NULL check (Page3 checks); the page index is not range-checked (table of 5,
 * page_max 2). The 9-int tables copied to the stack every iteration (dead) are mirrored at their original slots. */
#include <string.h>
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"
#include "acnb/nodwarf.h"

#ifdef ACNB_SOURCEPORT
/* main sdata 0x27bee8.. and OLMENU rodata 0x1ee0558 (nodwarf.h) */
char mtrend_fmt_d[8] = "%d";
char mtrend_fmt_slash[4] = "/";
char mtrend_fmt_s_slash[4] = "%s/";
char mtrend_fmt_time[16] = "%02d:%02d:%02d";
#endif

#define MTREND_COL_DEFAULT 0x80808080u   /* main 0x27becc / 0x27bed0 / 0x27bed8 / 0x27bee0 */
#define MTREND_COL_YELLOW  0x8061a5deu   /* main 0x27bed4 / 0x27bedc / 0x27bee4 (page titles) */

/* weapon-name table (OLMENU 0x1ee0500): copied to the frame and handed to DF_MPART_ID2Name as a third argument the
 * callee ignores (dead) */
static const int s_mtrend_name_tab[9] = {0xb, 0xc, 0xb, 0xc, 0xa, 0xa, 8, 2, 9};
/* SetConfig remap table (OLMENU 0x1ee0530): stMenuAC part slot of each trend weapon slot */
static const int s_mtrend_part_tab[9] = {0x10, 0x11, 0x12, 0x13, 0xe, 0xf, 0xa, 2, 0xc};

static const char s_mtrend_time_tag[] = "<TIME>";   /* main 0x27bef0, only compared */

static inline void mtrend_set_col(struct struct_acb9a91f *pAttr, unsigned int c)
{
    memcpy(&pAttr->col, &c, 4);   /* four byte stores in the original */
}

/* x = right - width of the current string */
static inline short mtrend_right_x(struct struct_acb9a91f *pAttr, int right)
{
    return (short)(right - DF_MFONT_GetStringWSize(pAttr->strp, pAttr->type, pAttr->flag));
}

static void __MTrendMng_OutputDataForTime(char *pStrOut, int data)
{
    int sec60 = data % 60;
    float cs = 0x1.aaaaaap+0f * (float)sec60;   /* 0x3fd55555, single mul.s, truncated */
    ps2_sprintf_ddd(pStrOut, mtrend_fmt_time, data / 3600, (data / 60) % 60, ps2_ftoi(cs));
}

static void __MTrendMng_Page5(void)
{
}

static void __MTrendMng_Page4(void)
{
}

/* per-weapon average (total_dist / total_time) + other combat data */
static void __MTrendMng_Page3(struct struct_acb9a91f *pAttr)
{
    PS2_FRAME(0xe0);
    PS2_STACK(char[16], name, 0xc0);
    PS2_STACK(int[9], deadTab, 0x70);
    int aOtherState[5];
    int lang;
    short numLine = 0;
    short y;

    struct _training_trend_st *pTrend = s_stTrendMng.pTrendData;
    aOtherState[0] = (int)pTrend->chargeNum;
    aOtherState[1] = (int)pTrend->overheatTime;
    aOtherState[2] = (int)pTrend->damageNum;
    aOtherState[3] = (int)pTrend->lockonPer;
    aOtherState[4] = (int)pTrend->restAP;

    pAttr->type = 1;
    pAttr->flag = 0;
    mtrend_set_col(pAttr, MTREND_COL_YELLOW);
    pAttr->strp = DF_MSTR_GetName(MenuMng.language, 0, 0xdbc90);
    pAttr->x = 0x30;
    pAttr->y = 0x68;
    DF_MFONT_SetFontAttr(pAttr, pAttr->type);
    DF_MFONT_Draw(pAttr, 1);

    numLine++;
    y = (short)(numLine * 0x14 + 0x68);
    mtrend_set_col(pAttr, MTREND_COL_DEFAULT);
    for (int j = 0; j < 9; j++) {
        struct _training_trend_weapon_st *w = &s_stTrendMng.pTrendData->weapon[j];
        if (w->cate_id == 0xff) continue;
        memcpy(deadTab, s_mtrend_name_tab, sizeof(deadTab));
        DF_MPART_ID2Name(w->weapon_id, name);
        if (name[0] == 0) continue;
        pAttr->type = 7;
        pAttr->flag = 0x401;
        pAttr->strp = (unsigned char *)name;
        pAttr->x = 0x36;
        pAttr->y = y;
        DF_MFONT_SetFontAttr(pAttr, pAttr->type);
        DF_MFONT_Draw(pAttr, 1);

        w = &s_stTrendMng.pTrendData->weapon[j];
        float t = w->total_time;
        if (0.0f == t)
            ps2_sprintf_d(name, mtrend_fmt_d, 0);
        else
            ps2_sprintf_d(name, mtrend_fmt_d, ps2_ftoi(ps2_div(w->total_dist, t)));
        pAttr->strp = (unsigned char *)name;
        pAttr->x = mtrend_right_x(pAttr, 0x11d);
        pAttr->y = y;
        DF_MFONT_Draw(pAttr, 1);

        lang = MenuMng.language;
        pAttr->strp = DF_MSTR_GetInfo(lang, 0, 0xdbc92);
        pAttr->x = 0x11d;
        pAttr->y = y;
        DF_MFONT_Draw(pAttr, 1);
        numLine++;
        y += 0x14;
    }

    y = (short)(numLine * 0x14 + 0x68);
    for (int k = 0; k < 5; k++) {
        unsigned int id = 0xdbc99 + k;
        pAttr->type = 7;
        pAttr->flag = 0x401;
        pAttr->strp = DF_MSTR_GetName(MenuMng.language, 0, id);
        pAttr->x = 0x36;
        pAttr->y = y;
        DF_MFONT_SetFontAttr(pAttr, pAttr->type);
        DF_MFONT_Draw(pAttr, 1);

        unsigned char *info = DF_MSTR_GetInfo(MenuMng.language, 0, id);
        if (info != NULL && ps2_strcmp((const char *)info, s_mtrend_time_tag) == 0)
            __MTrendMng_OutputDataForTime(name, aOtherState[k]);   /* inlined in the original */
        else
            ps2_sprintf_d(name, mtrend_fmt_d, aOtherState[k]);
        pAttr->strp = (unsigned char *)name;
        pAttr->x = mtrend_right_x(pAttr, 0x11d);
        pAttr->y = y;
        DF_MFONT_Draw(pAttr, 1);

        pAttr->strp = DF_MSTR_GetInfo(MenuMng.language, 0, id);
        unsigned char *unit = pAttr->strp;
        if (unit != NULL && ps2_strcmp((const char *)unit, s_mtrend_time_tag) != 0) {
            pAttr->x = 0x11d;
            pAttr->y = y;
            DF_MFONT_Draw(pAttr, 1);
        }
        numLine++;
        y += 0x14;
    }
}

/* movement records (time / percentage) + other movement data */
static void __MTrendMng_Page2(struct struct_acb9a91f *pAttr)
{
    PS2_FRAME(0xc0);
    PS2_STACK(char[16], name, 0xa0);
    /* OLMENU 0x1ee04f0, copied to sp+0x90 */
    void (*const apFuncTable[4])(char *, int) = {
        __MTrendMng_OutputDataForTime,
        __MTrendMng_OutputDataForTime,
        0,
        0,
    };
    int aOtherMoveState[4];
    short numLine = 0;
    short y;

    struct _training_trend_st *pTrend = s_stTrendMng.pTrendData;
    aOtherMoveState[0] = (int)pTrend->landTime;
    aOtherMoveState[1] = (int)pTrend->flyTime;
    aOtherMoveState[2] = ps2_ftoi(pTrend->averageSpeed);
    aOtherMoveState[3] = ps2_ftoi(pTrend->moveDistance);

    pAttr->type = 1;
    pAttr->flag = 0;
    mtrend_set_col(pAttr, MTREND_COL_YELLOW);
    pAttr->strp = DF_MSTR_GetName(MenuMng.language, 0, 0xdbc7c);
    pAttr->x = 0x30;
    pAttr->y = 0x68;
    DF_MFONT_SetFontAttr(pAttr, pAttr->type);
    DF_MFONT_Draw(pAttr, 1);

    numLine++;
    y = (short)(numLine * 0x14 + 0x68);
    mtrend_set_col(pAttr, MTREND_COL_DEFAULT);
    for (int i = 0; i < 5; i++) {
        pAttr->type = 7;
        pAttr->flag = 0x401;
        pAttr->strp = DF_MSTR_GetName(MenuMng.language, 0, 0xdbc7d + i);
        pAttr->x = 0x36;
        pAttr->y = y;
        DF_MFONT_SetFontAttr(pAttr, pAttr->type);
        DF_MFONT_Draw(pAttr, 1);

        __MTrendMng_OutputDataForTime(name, (int)s_stTrendMng.pTrendData->moveRecord[i]);   /* inlined */
        pAttr->strp = (unsigned char *)name;
        pAttr->x = mtrend_right_x(pAttr, 0xfa);
        pAttr->y = y;
        DF_MFONT_Draw(pAttr, 1);

        ps2_sprintf_d(name, mtrend_fmt_slash, 0);   /* no arguments in the original */
        pAttr->strp = (unsigned char *)name;
        pAttr->x = 0xfa;
        pAttr->y = y;
        DF_MFONT_Draw(pAttr, 1);

        ps2_sprintf_d(name, mtrend_fmt_d, (int)s_stTrendMng.pTrendData->movePer[i]);
        pAttr->strp = (unsigned char *)name;
        pAttr->x = mtrend_right_x(pAttr, 0x11d);
        pAttr->y = y;
        DF_MFONT_Draw(pAttr, 1);

        pAttr->strp = DF_MSTR_GetInfo(MenuMng.language, 0, 0xdbc7e);
        pAttr->x = 0x11d;
        pAttr->y = y;
        DF_MFONT_Draw(pAttr, 1);
        numLine++;
        y += 0x14;
    }

    y = (short)(numLine * 0x14 + 0x68);
    for (unsigned int k = 0; k < 4; k++) {
        unsigned int id = 0xdbc82 + k;
        pAttr->type = 7;
        pAttr->flag = 0x401;
        pAttr->strp = DF_MSTR_GetName(MenuMng.language, 0, id);
        pAttr->x = 0x36;
        pAttr->y = y;
        DF_MFONT_SetFontAttr(pAttr, pAttr->type);
        DF_MFONT_Draw(pAttr, 1);

        if (apFuncTable[k] != 0)
            apFuncTable[k](name, aOtherMoveState[k]);
        else
            ps2_sprintf_d(name, mtrend_fmt_d, aOtherMoveState[k]);
        pAttr->strp = (unsigned char *)name;
        pAttr->x = mtrend_right_x(pAttr, 0x11d);
        pAttr->y = y;
        DF_MFONT_Draw(pAttr, 1);

        pAttr->strp = DF_MSTR_GetInfo(MenuMng.language, 0, id);
        if (ps2_strcmp((const char *)pAttr->strp, s_mtrend_time_tag) != 0) {   /* no NULL check (original) */
            pAttr->x = 0x11d;
            pAttr->y = y;
            DF_MFONT_Draw(pAttr, 1);
        }
        numLine++;
        y += 0x14;
    }
}

/* weapons used: shot count + unit, use percentage */
static void __MTrendMng_Page1(struct struct_acb9a91f *pAttr)
{
    PS2_FRAME(0xb0);
    PS2_STACK(char[16], name, 0x90);
    PS2_STACK(int[9], deadTab, 0x60);

    pAttr->type = 1;
    pAttr->flag = 0;
    mtrend_set_col(pAttr, MTREND_COL_YELLOW);
    pAttr->strp = DF_MSTR_GetName(MenuMng.language, 0, 0xdbc72);
    pAttr->x = 0x30;
    pAttr->y = 0x68;
    DF_MFONT_SetFontAttr(pAttr, pAttr->type);
    DF_MFONT_Draw(pAttr, 1);

    mtrend_set_col(pAttr, MTREND_COL_DEFAULT);
    short y = 0x7c;
    for (int i = 0; i < 9; i++) {
        struct _training_trend_weapon_st *w = &s_stTrendMng.pTrendData->weapon[i];
        if (w->cate_id == 0xff) continue;
        memcpy(deadTab, s_mtrend_name_tab, sizeof(deadTab));
        DF_MPART_ID2Name(w->weapon_id, name);
        if (name[0] == 0) continue;
        pAttr->type = 7;
        pAttr->flag = 0x401;
        pAttr->strp = (unsigned char *)name;
        pAttr->x = 0x36;
        pAttr->y = y;
        DF_MFONT_SetFontAttr(pAttr, pAttr->type);
        DF_MFONT_Draw(pAttr, 1);

        ps2_sprintf_d(name, mtrend_fmt_d, s_stTrendMng.pTrendData->weapon[i].numShoot);
        pAttr->strp = (unsigned char *)name;
        pAttr->x = mtrend_right_x(pAttr, 0xed);
        pAttr->y = y;
        DF_MFONT_Draw(pAttr, 1);

        unsigned char *unit;
        if (s_stTrendMng.pTrendData->weapon[i].cate_id == 1)
            unit = DF_MSTR_GetInfo(MenuMng.language, 0, 0xdbc74);
        else
            unit = DF_MSTR_GetInfo(MenuMng.language, 0, 0xdbc73);
        ps2_sprintf_s(name, mtrend_fmt_s_slash, (const char *)unit);
        pAttr->strp = (unsigned char *)name;
        pAttr->x = 0xed;
        pAttr->y = y;
        DF_MFONT_Draw(pAttr, 1);

        ps2_sprintf_d(name, mtrend_fmt_d, s_stTrendMng.pTrendData->weapon[i].usePer);
        pAttr->strp = (unsigned char *)name;
        pAttr->x = mtrend_right_x(pAttr, 0x11d);
        pAttr->y = y;
        DF_MFONT_Draw(pAttr, 1);

        pAttr->strp = DF_MSTR_GetInfo(MenuMng.language, 0, 0xdbc75);
        pAttr->x = 0x11d;
        pAttr->y = y;
        DF_MFONT_Draw(pAttr, 1);
        y += 0x14;
    }
}

char MTrendMng_IsChangePage(void)
{
    if (s_stTrendMng.page_past == s_stTrendMng.page) return 0;
    s_stTrendMng.page_past = s_stTrendMng.page;
    return 1;
}

/* 2 = right arrow only (page 0), 3 = both (page 1), 1 = left only (any other page, negative too) */
char MTrendMng_GetArrowState(void)
{
    char page = s_stTrendMng.page;
    if (page == 0) return 2;
    if (page == 1) return 3;
    return 1;
}

void MTrendMng_Play(void)
{
    PS2_FRAME(0x240);
    PS2_STACK(struct struct_acb9a91f, attr, 0x10);   /* guest object read by DF_MFONT_* */
    /* MTrendMng_Play__apDrawPageFuncTable (OLMENU 0x1ee04d0); Page4 / Page5 take no argument */
    static void (*const apDrawPageFuncTable[5])(struct struct_acb9a91f *) = {
        __MTrendMng_Page1,
        __MTrendMng_Page2,
        __MTrendMng_Page3,
        [](struct struct_acb9a91f *) { __MTrendMng_Page4(); },
        [](struct struct_acb9a91f *) { __MTrendMng_Page5(); },
    };

    memset(&attr, 0, 0x228);
    attr.flag = 0x401;
    mtrend_set_col(&attr, MTREND_COL_DEFAULT);
    attr.depth = 1;
    attr.x = 0x30;
    attr.y = 0x68;
    attr.type = 3;
    DF_MFONT_SetFontAttr(&attr, attr.type);

    void (*fn)(struct struct_acb9a91f *) = apDrawPageFuncTable[(int)s_stTrendMng.page];   /* unchecked index */
    if (fn != 0) fn(&attr);

    if (MenuKeyGuide_IsShown() == 0) {
        if (MPad[MPadUser].repeat.dw == 1 && s_stTrendMng.page < s_stTrendMng.page_max) {
            Voice_Call(0x3e9, 0x7f);
            s_stTrendMng.page++;
        }
        if (MPad[MPadUser].repeat.up == 1 && 0 < s_stTrendMng.page) {
            Voice_Call(0x3e9, 0x7f);
            s_stTrendMng.page--;
        }
    }
}

void MTrendMng_SetConfig(char category_index, char detail_index, char level_index,
                         struct _training_trend_st *pTrendData, char mode)
{
    PS2_FRAME(0x30);   /* taken in the delay slot of the mode test */
    s_stTrendMng.category_index = category_index;
    s_stTrendMng.detail_index = detail_index;
    s_stTrendMng.level_index = level_index;
    s_stTrendMng.page_max = 2;
    s_stTrendMng.pTrendData = pTrendData;
    s_stTrendMng.mode = mode;
    if (s_stTrendMng.mode != 1) return;

    PS2_STACK(int[9], tab, 0);   /* the table is copied to sp+0 before every remapped slot (dead after return) */
    for (int i = 0; i < 9; i++) {
        struct _training_trend_weapon_st *w = &s_stTrendMng.pTrendData->weapon[i];
        if (w->cate_id == 0xff) continue;
        memcpy(tab, s_mtrend_part_tab, sizeof(tab));
        w->weapon_id = (unsigned short)M_ACp[0][0]->part_id[tab[i]];
    }
}
