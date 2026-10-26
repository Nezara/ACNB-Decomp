/* src/menu/mbriefingplayer.cpp - the mission-briefing text player (typewriter text + voice) of the unit / arena
 * "program" frames (OLMENU). One DF_MMM allocation (stMenuBriefingPlayer, 0x20098 bytes: 64 text pages of 16 lines x
 * 0x80 bytes, then the control fields). Play reveals the brief string character by character (__ProceedChar), the
 * parser (__ProcessControlCode) handles '#' comments, '/' (new line), '*' (page wait) and the "&X(...)" codes; DrawText
 * draws the 16 lines of the current page with a fade on the line being typed.
 * Native: SetXY, DrawText, Delete, Play, New, __ProceedChar, __AddChar, __ProcessControlCode, __NextLine. The helpers
 * only the "&X(" codes / page flips reach (__AddSpace, __AddString, __SetColor, __SetColorDirect, __NextPage, __atov,
 * __atoi) stay interpreted and are called through the bridge. */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
char mbriefingplayer_empty_str[1] = "";
char mbriefingplayer_fmt_dstr[5] = "%d%s";
char mbriefingplayer_fmt_color[20] = "<COLOR>%02x%02x%02x";
#endif

/* character slot base of line `line` of page `page` (byte arithmetic like the asm: line may be past 15) */
static inline char *mbp_line(struct stMenuBriefingPlayer *pMBP, int page, int line)
{
    return (char *)pMBP + page * 0x800 + line * 0x80 + 0x40;
}

/* Pf_SPrintf(buf, "<COLOR>%02x%02x%02x", TextColor.r, .g, .b): buffer and format are PS2 memory (lbu args) */
static inline void mbp_sprintf_color(char *buf, struct stMenuBriefingPlayer *pMBP)
{
    int r = pMBP->TextColor.r, g = pMBP->TextColor.g, b = pMBP->TextColor.b;
#ifdef ACNB_SOURCEPORT
    Pf_SPrintf(buf, mbriefingplayer_fmt_color, r, g, b);
#else
    gcall_as<int(char *, char *, int, int, int)>(GFN(Pf_SPrintf), buf, mbriefingplayer_fmt_color, r, g, b);
#endif
}

static inline void mbp_sprintf_dstr(char *buf, int v, char *s)
{
#ifdef ACNB_SOURCEPORT
    Pf_SPrintf(buf, mbriefingplayer_fmt_dstr, v, s);
#else
    gcall_as<int(char *, char *, int, char *)>(GFN(Pf_SPrintf), buf, mbriefingplayer_fmt_dstr, v, s);
#endif
}

/* one character into the current line (nStrCur read, stored + 1, then the byte) */
static inline void mbp_put(struct stMenuBriefingPlayer *pMBP, char c)
{
    int line = pMBP->nLineCur;
    int n = pMBP->nStrCur;
    pMBP->nStrCur = n + 1;
    mbp_line(pMBP, pMBP->nPageCur, line)[n] = c;
}

void MenuBriefingPlayer_SetXY(struct stMenuBriefingPlayer *pMBP, int x, int y)
{
    pMBP->x = x;
    pMBP->y = y;
}

/* alpha arrives in a1 (DWARF says v0; the callers pass a1) */
void MenuBriefingPlayer_DrawText(struct stMenuBriefingPlayer *pMBP, unsigned char alpha)
{
    PS2_FRAME(0x2c0);
    PS2_STACK(struct struct_acb9a91f, Attr, 0x90);   /* guest object read by DF_MFONT_* */
    unsigned char *p = (unsigned char *)pMBP + pMBP->nPageCur * 0x800 + 0x40;
    unsigned char *z = (unsigned char *)&Attr;
    for (int n = 0x228; n != 0; n--) *z++ = 0;
    struct struct_76b810ec color = pMBP->DefaultColor;   /* sp+0x2bc */
    color.a = alpha;
    Attr.flag = 1;
    Attr.col = color;
    Attr.depth = 1;
    Attr.x = (short)pMBP->x;
    Attr.type = 6;
    DF_MFONT_SetFontAttr(&Attr, Attr.type);
    if (pMBP->State == 3) Attr.col.a = (unsigned char)pMBP->nFadeCnt;
    /* quirk: fadebegin / fadeend are set only on the line being typed and never reset for the lines after it */
    for (int i = 0, dy = 0; i < 16; i++, dy += 20) {
        Attr.strp = p;
        Attr.y = (short)(pMBP->y + dy);
        if (pMBP->State == 1 && pMBP->nLineCur == i) {
            int d = pMBP->nStrDispCur;
            Attr.fadebegin = d - 4;
            Attr.fadeend = d;
        }
        DF_MFONT_Draw(&Attr, 1);
        p += 0x80;
    }
}

/* one more visible character; when the line has no hidden characters left, parse / add text until it has */
static void __ProceedChar(struct stMenuBriefingPlayer *pMBP)
{
    PS2_FRAME(0x40);
    pMBP->nStrDispCur += 1;
    pMBP->nSleepFrame += 15;
    int disp = pMBP->nStrDispCur;
    if (!(DF_MFONT_GetCharCount((unsigned char *)mbp_line(pMBP, pMBP->nPageCur, pMBP->nLineCur)) < disp)) return;
    for (;;) {
        char *s1 = pMBP->pBriefCur;
        pMBP->pBriefCur = __ProcessControlCode(pMBP, s1);
        disp = pMBP->nStrDispCur;   /* re-read: __NextLine moved the line and cleared the count */
        if (!(DF_MFONT_GetCharCount((unsigned char *)mbp_line(pMBP, pMBP->nPageCur, pMBP->nLineCur)) < disp)) return;
        char *cur = pMBP->pBriefCur;
        if (*cur == 0) {
            pMBP->State = 4;
            return;
        }
        if (pMBP->State == 2) return;
        if (!(pMBP->nSleepFrame < 0x10)) return;
        if (s1 == cur || *s1 == 0) break;   /* nothing consumed by a control code: add a character */
    }
    if (MenuUtil_IsLeadByte((unsigned char *)pMBP->pBriefCur)) {
        char *q = pMBP->pBriefCur;
        pMBP->pBriefCur = q + 1;
        __AddChar(pMBP, *q);
        q = pMBP->pBriefCur;
        if (*q == 0) {   /* quirk: only the trail byte is NUL-tested */
            pMBP->State = 4;
            return;
        }
        pMBP->pBriefCur = q + 1;
        __AddChar(pMBP, *q);
    } else {
        char *q = pMBP->pBriefCur;
        pMBP->pBriefCur = q + 1;
        __AddChar(pMBP, *q);
    }
}

/* adds c to the current line; at column 0x7f it wraps (inlined __NextLine: the new line starts with the 13-char
 * "<COLOR>rrggbb" prefix so the colour survives; the inner copy of the wrap recurses into __AddChar) */
static void __AddChar(struct stMenuBriefingPlayer *pMBP, char c)
{
    PS2_FRAME(0xe0);
    if (pMBP->nStrCur >= 0x7f) {
        pMBP->nLineCur++;
        pMBP->nStrCur = 0;
        pMBP->nStrDispCur = 0;
        if (pMBP->nLineCur < 16) {
            PS2_STACK(char[0x40], buff, 0xa0);
            /* (TextColor = TextColor: a 4-byte self copy, no effect) */
            mbp_sprintf_color(buff, pMBP);
            char *q = buff;
            char ch = *q;
            while (ch != 0) {
                char s1 = ch;
                q++;
                if (pMBP->nStrCur >= 0x7f) {   /* dead in practice (nStrCur 0..18 here) */
                    pMBP->nLineCur++;
                    pMBP->nStrCur = 0;
                    pMBP->nStrDispCur = 0;
                    if (pMBP->nLineCur < 16) {
                        PS2_STACK(char[0x40], buff2, 0x60);
                        mbp_sprintf_color(buff2, pMBP);
                        for (char *r = buff2; *r != 0;) {
                            char rc = *r++;
                            __AddChar(pMBP, rc);
                        }
                    }
                }
                if (pMBP->nLineCur < 16) mbp_put(pMBP, s1);
                ch = *q;
            }
        }
    }
    if (pMBP->nLineCur < 16) mbp_put(pMBP, c);   /* lines past 15 drop the character */
}

/* '#' comment, CR / LF, '/', '*' and the "&..." codes at pcur; returns where parsing goes on */
static char *__ProcessControlCode(struct stMenuBriefingPlayer *pMBP, char *pcur)
{
    PS2_FRAME(0x170);
    char *p = pcur;
    /* quirk: the comment and the CR / LF skip run once per call */
    if (*p == '#') {
        p++;
        while (*p != '\n' && *p != 0) p++;
    }
    while (*p == '\r' || *p == '\n') p++;
    char c = *p;
    if (c == '/') {
        __NextLine(pMBP);
        return p + 1;
    }
    if (c == '*') {
        pMBP->State = 2;
        pMBP->nFadeCnt = 0x80;
        return p + 1;
    }
    if (c != '&') return p;
    char d = p[1];
    if (d == 0) return p + 1;
    if (d == '/') {
        __AddChar(pMBP, '/');
        return p + 2;
    }
    if (d == '*') {
        __AddChar(pMBP, '*');
        return p + 2;
    }
    p += 2;
    if (*p != '(') return p;
    switch (d) {   /* asm: compare chain S s P p N n X x D d G g C c */
    case 'S': case 's': {
        PS2_STACK(int, v, 0x14c);   /* written by the interpreted __atoi */
        v = 0;
        p = (char *)__atoi((unsigned char *)p + 1, &v);
        __AddSpace(pMBP, v);
        break;
    }
    case 'P': case 'p': {
        PS2_STACK(int, x, 0x158);
        PS2_STACK(int, y, 0x154);
        PS2_STACK(int, z, 0x150);   /* results unused */
        p = (char *)__atov((unsigned char *)p + 1, &x, &y, &z);
        break;
    }
    case 'N': case 'n': {
        PS2_STACK(int, key, 0x15c);
        key = 0;
        p = (char *)__atoi((unsigned char *)p + 1, &key);
        char *s = mbriefingplayer_empty_str;
        if (key == 1) s = PlayerInfo_GetAcName(0, PlayerInfo_GetCurrentAcNo(0));
        else if (key == 0) s = g_gamedata[0].name;
        if (!s || ps2_strlen(s) == 0) s = (char *)DF_MSTR_GetName(MenuMng.language, 0, 0x189f4);
        __AddString(pMBP, s);
        break;
    }
    case 'X': case 'x': {
        PS2_STACK(int, key, 0x160);
        PS2_STACK(char[0x10c], buff, 0x40);   /* Pf_SPrintf target, sp+0x40..0x14c */
        key = 0;
        p = (char *)__atoi((unsigned char *)p + 1, &key);
        int value = 0;
        if (key == 3) value = pMBP->RewardInfo.SpSub;
        else if (key == 2) value = pMBP->RewardInfo.SpAdd;
        else if (key == 1) value = pMBP->RewardInfo.Suceess;
        else if (key == 0) value = pMBP->RewardInfo.Prepaid;
        char *name = (char *)DF_MSTR_GetName(MenuMng.language, 0, 0x186c4);
        mbp_sprintf_dstr(buff, value, name);
        __AddString(pMBP, buff);
        break;
    }
    case 'D': case 'd': {
        PS2_STACK(int, delay, 0x164);
        delay = 0;
        p = (char *)__atoi((unsigned char *)p + 1, &delay);
        int t = (int)((unsigned int)delay * 900u);   /* delay*15*15*4 by shifts, signed / 60 */
        pMBP->nSleepFrame += t / 60;
        break;
    }
    case 'G': case 'g': {
        PS2_STACK(int, icon, 0x168);
        icon = 0;
        p = (char *)__atoi((unsigned char *)p + 1, &icon);
        if (pMBP->fnCbDispIcon) gcall(pMBP->fnCbDispIcon, pMBP->ParamCbDispIcon, icon);
        break;
    }
    case 'C': case 'c': {
        PS2_STACK(int, color, 0x16c);
        color = 0;
        p = (char *)__atoi((unsigned char *)p + 1, &color);
        __SetColor(pMBP, color);
        break;
    }
    default:
        return p + 1;   /* quirk: an unknown letter leaves its arguments; p is still on the '(' */
    }
    if (*p == ')') p++;
    return p;
}

/* next line: same shape as __AddChar's wrap with the recursion into __NextLine itself; no payload character */
static void __NextLine(struct stMenuBriefingPlayer *pMBP)
{
    PS2_FRAME(0x110);
    pMBP->nLineCur++;
    pMBP->nStrCur = 0;
    pMBP->nStrDispCur = 0;
    if (pMBP->nLineCur < 16) {
        PS2_STACK(char[0x40], buff, 0xd0);
        /* (TextColor = TextColor: a 4-byte self copy, no effect) */
        mbp_sprintf_color(buff, pMBP);
        char *q = buff;
        char ch = *q;
        while (ch != 0) {
            char s7 = ch;
            q++;
            if (pMBP->nStrCur >= 0x7f) {   /* dead in practice */
                pMBP->nLineCur++;
                pMBP->nStrCur = 0;
                pMBP->nStrDispCur = 0;
                if (pMBP->nLineCur < 16) {
                    PS2_STACK(char[0x40], buff2, 0x90);
                    mbp_sprintf_color(buff2, pMBP);
                    for (char *r = buff2; *r != 0;) {
                        char s3 = *r++;
                        if (pMBP->nStrCur >= 0x7f) __NextLine(pMBP);
                        if (pMBP->nLineCur < 16) mbp_put(pMBP, s3);
                    }
                }
            }
            if (pMBP->nLineCur < 16) mbp_put(pMBP, s7);
            ch = *q;
        }
    }
}

void MenuBriefingPlayer_Delete(struct stMenuBriefingPlayer *pMBP)
{
    PS2_FRAME(0x20);
    if (pMBP) {
        Vags_Stop();
        DF_MMM_Free(0, pMBP);
    }
}

/* state machine: 0 starts the voice and plays on as 1; 1 reveals text by sleep frames; 3 fades to the next page */
void MenuBriefingPlayer_Play(struct stMenuBriefingPlayer *pMBP)
{
    PS2_FRAME(0x30);
    if (pMBP->State == 0) {
        pMBP->State = 1;
        Vags_Stop();
        if (pMBP->pVoiceBuff) Vags_Play(0, 2, pMBP->pVoiceBuff);
    }
    unsigned char st = (unsigned char)pMBP->State;
    if (st == 4 || st == 2) return;
    if (st == 3) {
        pMBP->nFadeCnt -= 0x18;
        if (pMBP->nFadeCnt < 0) {
            pMBP->nFadeCnt = 0x80;   /* before __NextPage (jal delay slot) */
            __NextPage(pMBP);
            int page = pMBP->nPageCur;
            Vags_Stop();
            if (pMBP->pVoiceBuff) Vags_Play(page, 2, pMBP->pVoiceBuff);
        }
        return;
    }
    pMBP->nSleepFrame -= 0x1e;
    while (pMBP->nSleepFrame <= 0 && pMBP->State == 1) __ProceedChar(pMBP);
}

struct stMenuBriefingPlayer *MenuBriefingPlayer_New(struct StMissionInfo *pMissInfo, char *pBriefStr, void *pVoiceBuff)
{
    PS2_FRAME(0x50);
    struct stMenuBriefingPlayer *p = (struct stMenuBriefingPlayer *)DF_MMM_Alloc(0, 0x20098);
    ZeroMemory(p, 0x20098);
    if (!pBriefStr) pBriefStr = mbriefingplayer_empty_str;   /* PS2 address: the player keeps the pointer */
    p->pBriefCur = pBriefStr;
    p->pBriefStr = pBriefStr;
    p->pVoiceBuff = pVoiceBuff;
    if (pMissInfo) memcpy(&p->MissInfo, pMissInfo, 0x40);   /* 8 x two lw / sw */
    p->nFadeCnt = 0x80;
    p->x = 0x14d;
    p->y = 0x4e;
    /* s_aTextColor[0] (OLMENU 0x1ee4f20) and the main sdata copy gp-0x7748: both 8c 92 92 80 */
    p->DefaultColor.r = 0x8c; p->DefaultColor.g = 0x92; p->DefaultColor.b = 0x92; p->DefaultColor.a = 0x80;
    p->State = 0;
    p->TextColor.r = 0x8c; p->TextColor.g = 0x92; p->TextColor.b = 0x92; p->TextColor.a = 0x80;
    return p;
}
