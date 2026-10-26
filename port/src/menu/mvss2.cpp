/* src/menu/mvss2.cpp - the "situation" window of the Versus setup screens (AC setup mvsg, team setup mvst, the wait
 * screens mvsw): a struct_9fc9058b (0x58 bytes + 8 x 16-byte string buffers carved from MenuWorkp) owning a
 * WindowEx frame, the map picture, five strings ("SITUATION", "TIME LIMIT", "BREAK TARGET", the time limit, the
 * battle form) and the local player's id string ("1P", or "LIVE MONITOR" on a spectator console). The help line
 * is shown only on link clients. DF_MVS_SetSituationWindow also writes play_info.local_player_num on every call.
 * The sprintf calls are newlib's (ps2_sprintf_*): buffers, formats and every %s argument are PS2 memory (nodwarf
 * strings, PS2_STACK buffers, the guest-pointer table apPlayerString). */
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include <string.h>

#ifdef ACNB_SOURCEPORT
char mvss2_live_monitor[13] = "LIVE MONITOR";
char mvss2_situation[10] = "SITUATION";
char mvss2_time_limit[11] = "TIME LIMIT";
char mvss2_break_target[13] = "BREAK TARGET";
char mvss2_no_limit[9] = "NO LIMIT";
char mvss2_fmt_parts20d[12] = "parts20d_%d";
char mvss2_player_0[9] = "player_0";
char mvss2_1p[3] = "1P";
char mvss2_2p[3] = "2P";
char mvss2_3p[3] = "3P";
char mvss2_4p[3] = "4P";
char mvss2_amp[2] = "&";
char mvss2_empty[1] = "";
char mvss2_fmt_s[3] = "%s";
char mvss2_fmt_time[8] = "%02d:00";
char mvss2_all_ac[7] = "ALL AC";
char mvss2_leader[7] = "LEADER";
char mvss2_team[5] = "TEAM";
char mvss2_fmt_sss[7] = "%s%s%s";
#endif

/* the inline "delete" of a mobj (f_draw = 0, then f_erase = 1: two separate flag-byte writes) */
static inline void mvss2_erase(struct stMobj *p)
{
    p->f_draw = 0;
    p->f_erase = 1;
}

/* the help line is used only on a link client that is not set up yet: the last console (self_id ==
 * connected_num - 1) only when it is not a live monitor, the others when live_monitor is 0 or 1 */
static inline int mvss2_help_on(void)
{
    char self = play_info.self_id;
    if (self == 0) return 0;
    if (play_info.f_setuped != 0) return 0;
    if (self == play_info.connected_num - 1) return play_info.live_monitor == 0;
    if (play_info.live_monitor == 1) return 1;
    return play_info.live_monitor == 0;
}

/* the fixed captions and the time-limit / battle-form texts (the captions are the sprintf FORMAT, no arguments) */
static void __MVS_SetSituationString(struct struct_9fc9058b *whsp)
{
    PS2_FRAME(0x20);
    if (whsp == 0) return;
    ps2_sprintf_d(whsp->strBuff[0], mvss2_situation, 0);
    ps2_sprintf_d(whsp->strBuff[1], mvss2_time_limit, 0);
    ps2_sprintf_d(whsp->strBuff[2], mvss2_break_target, 0);
    if (play_info.time_limit == 16) ps2_sprintf_s(whsp->strBuff[4], mvss2_fmt_s, mvss2_no_limit);
    else ps2_sprintf_d(whsp->strBuff[4], mvss2_fmt_time, play_info.time_limit);
    switch (play_info.vs_form) {
    case 0: ps2_sprintf_d(whsp->strBuff[5], mvss2_all_ac, 0); break;
    case 1: ps2_sprintf_d(whsp->strBuff[5], mvss2_leader, 0); break;
    case 2: ps2_sprintf_d(whsp->strBuff[5], mvss2_team, 0); break;
    default: break;   /* strBuff[5] keeps its old text */
    }
    DF_MOBJ_SetStringDirect(whsp->strp[3], whsp->strBuff[4]);
    DF_MOBJ_SetStringDirect(whsp->strp[4], whsp->strBuff[5]);
}

/* fade every part out (no null check of whsp) */
void DF_MVS_SetSituationWindowFadeOut(struct struct_9fc9058b *whsp)
{
    PS2_FRAME(0x40);
    if (whsp->swp) WindowEx_SetFadeOut(whsp->swp);
    if (whsp->mapp[0]) DF_MOBJ_SetFadeOut(whsp->mapp[0]);
    for (int i = 0; i < 5; i++) {
        if (whsp->strp[i]) DF_MOBJ_SetFadeOut(whsp->strp[i]);
    }
    if (whsp->idp[0]) DF_MOBJ_SetFadeOut(whsp->idp[0]);
    if (mvss2_help_on()) DF_M1HELP_FadeOut();
}

/* show and fade every part in (the map picture is only faded in; no null check of whsp) */
static void __MVS_SetSituationWindowFadeIn(struct struct_9fc9058b *whsp)
{
    PS2_FRAME(0x40);
    if (whsp->swp) {
        DF_MOBJ_SetDrawEnable(whsp->swp, 1);
        WindowEx_SetFadeIn(whsp->swp);
    }
    if (whsp->mapp[0]) DF_MOBJ_SetFadeIn(whsp->mapp[0]);
    for (int i = 0; i < 5; i++) {
        if (whsp->strp[i]) {
            DF_MOBJ_SetDrawEnable(whsp->strp[i], 1);
            DF_MOBJ_SetFadeIn(whsp->strp[i]);
        }
    }
    if (whsp->idp[0]) {
        DF_MOBJ_SetDrawEnable(whsp->idp[0], 1);
        DF_MOBJ_SetFadeIn(whsp->idp[0]);
    }
    if (mvss2_help_on()) {
        DF_M1HELP_SetString(DF_MSTR_GetInfo(MenuMng.language, 0, 0x18fc6));
        DF_M1HELP_FadeIn();
    }
}

/* delete the parts (the struct and its string buffers stay: the owner screen reuses them) */
void DF_MVS_DeleteSituationWindow(struct struct_9fc9058b *whsp)
{
    PS2_FRAME(0x20);
    if (whsp == 0) return;
    if (whsp->swp) {
        WindowEx_Delete(whsp->swp);
        ZeroMemory(whsp->swp, 0x140);   /* clears the deleted window mobj */
        whsp->swp = 0;
    }
    if (whsp->mapp[0]) {
        mvss2_erase(whsp->mapp[0]);
        whsp->mapp[0] = 0;
    }
    for (int i = 0; i < 5; i++) {
        if (whsp->strp[i]) {
            mvss2_erase(whsp->strp[i]);
            whsp->strp[i] = 0;
        }
    }
    if (whsp->idp[0]) {
        mvss2_erase(whsp->idp[0]);
        whsp->idp[0] = 0;
    }
}

/* build (or re-show) the situation window under prev_objp; *pwhsp is allocated from MenuWorkp when NULL */
struct stMobj *DF_MVS_SetSituationWindow(struct stMobj *prev_objp, struct struct_9fc9058b **pwhsp)
{
    PS2_FRAME(0x2f0);
    PS2_STACK(char *[7], apPlayerString, 0x80);
    PS2_STACK(char[256], name0, 0xa0);
    PS2_STACK(char[256], name1, 0x1a0);
    PS2_STACK(struct stMenuRect, win_rect, 0x2a0);
    PS2_STACK(int[4], uv, 0x2b0);
    PS2_STACK(int[3], nlTable, 0x2c8);
    PS2_STACK(char[10], name10, 0x2d8);
    PS2_STACK(unsigned int, id_col, 0x2e4);
    PS2_STACK(unsigned int, str_col, 0x2e8);
    PS2_STACK(unsigned int, win_col, 0x2ec);

    if (pwhsp == 0) return prev_objp;
    if (*pwhsp == 0) {   /* never taken: every caller passes an allocated struct */
        *pwhsp = (struct struct_9fc9058b *)(((uintptr_t)MenuWorkp + 0xf) & ~(uintptr_t)0xf);
        memset(*pwhsp, 0, sizeof(struct struct_9fc9058b));
        struct struct_9fc9058b *a = *pwhsp;
        a->strBuff[0] = (char *)(a + 1);
        memset(a->strBuff[0], 0, 0x10);
        for (int k = 1; k < 8; k++) {
            a->strBuff[k] = a->strBuff[k - 1] + 0x10;
            memset(a->strBuff[k], 0, 0x10);
        }
        MenuWorkp = (unsigned char *)a->strBuff[7] + 0x10;
    }
    struct struct_9fc9058b *w = *pwhsp;

    if (w->swp == 0) {
        memcpy(name10, "parts01_0", 10);
        w->swp = WindowEx_NewFromLayout(prev_objp, (_WINDOWEX_TYPE)0, (char *)LAYOUT_GetLayoutData(0x40b), name10);
        DF_MOBJ_SetDrawEnable(w->swp, 0);
    }

    /* map picture: texture 0x1388 + map id */
    uv[0] = 0;
    uv[1] = 0;
    uv[2] = 0x1d0;
    uv[3] = 0x40;
    win_rect.x = 0x58;
    win_rect.y = 0x40;
    win_rect.w = 0x1d0;
    win_rect.h = 0x40;
    win_col = 0x80808080u;
    if (w->mapp[0] == 0) {
        PS2_STACK(unsigned long long, arg9, 0);   /* the 9th argument's slot (sd $zero, 0($sp)) */
        arg9 = 0;
        w->mapp[0] = DF_MOBJ_NXNewSprite(prev_objp, 0x1388 + play_info.map_id, &win_rect, uv,
                                         (struct struct_76b810ec *)&win_col, 0, 0, 0, 0);
    }

    /* the five strings: strp[0..2] use strBuff[0..2], strp[3] / [4] use strBuff[4] / [5] */
    str_col = 0x80c6c077u;
    for (int i = 0; i < 5; i++) {
        ps2_sprintf_d(name1, mvss2_fmt_parts20d, i);   /* every pass, also when the string exists */
        if (w->strp[i] == 0) {
            int j = i < 3 ? i : i + 1;
            w->strp[i] = DF_MOBJ_NewStringDirectFromLayoutName(prev_objp, w->strBuff[j],
                                                               (char *)LAYOUT_GetLayoutData(0x40b), name1,
                                                               (struct struct_76b810ec *)&str_col, 0, 0);
            DF_MOBJ_SetDrawEnable(w->strp[i], 0);
        }
    }

    /* side effect on the game state: the number of local players */
    play_info.local_player_num = 1;
    if (play_info.play_mode == 1) {
        if (!(play_info.self_id == 1 && play_info.connected_num == 2 && play_info.player_num == 3))
            play_info.local_player_num = 2;
    }

    /* the local player's id string */
    if (w->idp[0] == 0) {
        id_col = 0x80c6c077u;
        ps2_sprintf_d(name0, mvss2_player_0, 0);
        ZeroMemory(s_aPlayerString, 0x10);
        char player_num = play_info.player_num;
        nlTable[0] = 0;
        nlTable[1] = 4;
        nlTable[2] = 1;
        apPlayerString[0] = mvss2_1p;
        apPlayerString[1] = mvss2_2p;
        apPlayerString[2] = mvss2_3p;
        apPlayerString[3] = mvss2_4p;
        apPlayerString[4] = mvss2_amp;
        apPlayerString[5] = mvss2_live_monitor;
        apPlayerString[6] = mvss2_empty;
        char play_mode = play_info.play_mode;
        int t = 0;
        if (play_mode == 1 && play_info.self_id > 0) t = 1;
        char self_id = play_info.self_id;
        nlTable[0] = self_id + t;
        if (play_mode == 0) {
            nlTable[2] = 6;
            nlTable[1] = 6;
        }
        if (play_mode == 1) {
            if (self_id == 1 && play_info.connected_num == 2 && player_num == 3) {
                nlTable[2] = 6;
                nlTable[1] = 6;
            } else {
                nlTable[2] = nlTable[0] + 1;
            }
        }
        char cn = play_info.connected_num;
        if (self_id == cn - 1 && !(cn < 3) && player_num < (self_id + 1) * (play_mode + 1) - play_mode)
            nlTable[0] = 5;
        if (nlTable[0] == 5) ps2_sprintf_s(s_aPlayerString, mvss2_fmt_s, apPlayerString[5]);
        else
            ps2_sprintf_sss(s_aPlayerString, mvss2_fmt_sss, apPlayerString[nlTable[0]], apPlayerString[nlTable[1]],
                            apPlayerString[nlTable[2]]);
        w->idp[0] = DF_MOBJ_NewStringDirectFromLayoutName(prev_objp, s_aPlayerString,
                                                          (char *)LAYOUT_GetLayoutData(0x40a), name0,
                                                          (struct struct_76b810ec *)&id_col, 0, 0x10000);
        DF_MOBJ_SetDrawEnable(w->idp[0], 0);
    }

    if (mvss2_help_on()) DF_M1HELP_SetWindow(); /* empty stub; the original passes (0, 0x280) */

    __MVS_SetSituationString(w);   /* need_fadein is always 1 */
    __MVS_SetSituationWindowFadeIn(w);
    return prev_objp;
}
