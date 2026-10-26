/* src/menu/mkeyguide.cpp - the big "key guide" help panel (OLMENU) that opens on Start in almost every menu (the
 * one-line strip at the bottom is mbguide.cpp). A data file loaded at menu start (File 0xfaf) lists, per scene id
 * given to MenuKeyGuide_Show, a page of entries (button icon id + text); MenuKeyGuide_Draw runs the fade state
 * machine and paints the veil, the dialog window, the rows (text + button icons) and the "KEYGUIDE" title.
 * State: g_KeyGuideMng (Persent = fade percent 0..100, PosY = base y the rows slide from).
 *
 * g1: the 24 button-icon painters MenuKeyGuideIconSet00..23, reached through MenuKeyGuideIconFunc[nIconId - 1]
 * (PS2 data table) from DrawMess with PosY = the row's y. They come from four asm templates that differ only in
 * immediates: class A (one sprite, frame 0x160), class B (two sprites, 0x2b0), class C (four sprites, 0x530) and
 * the empty IconSet00. Every sprite copies the whole 76-int uv table onto the stack (19 quads, a dead copy except
 * the quad passed on), sets a 26x22 (or 50x22) rect whose y slides from g.PosY to PosY and whose height grows with
 * Persent, sets GS ALPHA_1 and draws texture 0x3eb. The class helpers below have no frame of their own: they run
 * on the wrapper's PS2_FRAME, so their PS2_STACK offsets are the wrapper's original slots.
 *
 * g2: the text / panel code (FontPrint, DrawMess, DrawTitle, MenuKeyGuideFunc), the fade state machine
 * (StatusFunc[State]: 0 fade in, 1 steady, 2 fade out), Show / Hide / Update (close on circle or Start) and the
 * loader (__MenuKeyGuideFile_Init relocates the file's index fields into pointers in place). Stack objects passed
 * to the native draw code sit at their original slots (PS2_STACK); every /100 is the original's signed division. */
#include <string.h>
#include "acnb/nodwarf.h"

#ifdef ACNB_SOURCEPORT
struct __MENUKEYGUIDE_MNG g_KeyGuideMng;
char mkeyguide_title[9] = "KEYGUIDE";
#endif

/* the 76-int icon uv table (OLMENU 0x1ededb0, identical copy at 0x1edeee0 used by IconSet21 / 22): quads of
 * (u0, v0, u1, v1) on the 104x110 button sheet */
static const int s_kg_uv[76] = {
     0,  0,  26,  22,   26,  0,  52,  22,   52,  0,  78,  22,   78,  0, 104,  22,
     0, 22,  26,  44,   26, 22,  52,  44,   52, 22,  78,  44,   78, 22, 104,  44,
     0, 44,  26,  66,   26, 44,  52,  66,   52, 44,  78,  66,   78, 44, 104,  66,
     0, 66,  26,  88,   26, 66,  52,  88,   52, 66,  78,  88,   78, 66, 104,  88,
     0, 88,  26, 110,   26, 88,  76, 110,   76, 88, 126, 110,
};

/* icon colour: the raw word at gp-0x71fc (0x27ba74; IconSet21 / 22 read gp-0x71f8 = 0x27ba78, same value) */
#define KG_ICON_COL 0x80808080u

/* one sprite: copy = the stack copy of the uv table, rect / col = its rect and colour slots */
static void kg_icon_sprite(int PosY, int x, int w, int quad, int (&copy)[76], struct stMenuRect &rect,
                           struct struct_76b810ec &col)
{
    unsigned int c = KG_ICON_COL;
    int Per;
    int gy;

    memcpy(&col, &c, sizeof(col));
    memcpy(copy, s_kg_uv, sizeof(copy));
    rect.x = x;
    rect.w = w;
    Per = g_KeyGuideMng.Persent;
    gy = g_KeyGuideMng.PosY;
    rect.y = gy + (Per * (PosY - gy)) / 100;
    rect.h = (Per * 22) / 100;                       /* stored in KickReg's delay slot */
    DF_MDRAW_KickReg(0x42, 0x8000000044ull);         /* GS ALPHA_1 */
    DF_MDRAW_DrawTexSpriteI(0x3eb, &rect, &copy[quad * 4], &col, 0);
}

/* class A (62 insns): one sprite. Frame 0x160: copy sp+0x10, rect sp+0x140, col sp+0x15c. */
static void kg_icon_class_a(int PosY, int x, int w, int quad)
{
    PS2_STACK(int[76], copy, 0x10);
    PS2_STACK(struct stMenuRect, rect, 0x140);
    PS2_STACK(struct struct_76b810ec, col, 0x15c);

    kg_icon_sprite(PosY, x, w, quad, copy, rect, col);
}

/* class B (121 insns): two sprites at x 0x10d / 0x127. Frame 0x2b0. */
static void kg_icon_class_b(int PosY, int quad1, int quad2)
{
    PS2_STACK(int[76], copy1, 0x160);
    PS2_STACK(struct stMenuRect, rect1, 0x290);
    PS2_STACK(struct struct_76b810ec, col1, 0x2ac);
    PS2_STACK(int[76], copy2, 0x20);
    PS2_STACK(struct stMenuRect, rect2, 0x150);
    PS2_STACK(struct struct_76b810ec, col2, 0x2a8);

    kg_icon_sprite(PosY, 0x10d, 0x1a, quad1, copy1, rect1, col1);
    kg_icon_sprite(PosY, 0x127, 0x1a, quad2, copy2, rect2, col2);
}

/* class C (233 insns): four sprites at x 0xf3 / 0x10d / 0x127 / 0x141. Frame 0x530. */
static void kg_icon_class_c(int PosY, int quad1, int quad2, int quad3, int quad4)
{
    PS2_STACK(int[76], copy1, 0x3e0);
    PS2_STACK(struct stMenuRect, rect1, 0x510);
    PS2_STACK(struct struct_76b810ec, col1, 0x52c);
    PS2_STACK(int[76], copy2, 0x2a0);
    PS2_STACK(struct stMenuRect, rect2, 0x3d0);
    PS2_STACK(struct struct_76b810ec, col2, 0x528);
    PS2_STACK(int[76], copy3, 0x160);
    PS2_STACK(struct stMenuRect, rect3, 0x290);
    PS2_STACK(struct struct_76b810ec, col3, 0x524);
    PS2_STACK(int[76], copy4, 0x20);
    PS2_STACK(struct stMenuRect, rect4, 0x150);
    PS2_STACK(struct struct_76b810ec, col4, 0x520);

    kg_icon_sprite(PosY, 0xf3, 0x1a, quad1, copy1, rect1, col1);
    kg_icon_sprite(PosY, 0x10d, 0x1a, quad2, copy2, rect2, col2);
    kg_icon_sprite(PosY, 0x127, 0x1a, quad3, copy3, rect3, col3);
    kg_icon_sprite(PosY, 0x141, 0x1a, quad4, copy4, rect4, col4);
}

static void MenuKeyGuideIconSet23(int PosY) { PS2_FRAME(0x530); kg_icon_class_c(PosY, 14, 15, 4, 6); }
static void MenuKeyGuideIconSet22(int PosY) { PS2_FRAME(0x160); kg_icon_class_a(PosY, 0x10e, 0x32, 18); }
static void MenuKeyGuideIconSet21(int PosY) { PS2_FRAME(0x160); kg_icon_class_a(PosY, 0x10e, 0x32, 17); }
static void MenuKeyGuideIconSet20(int PosY) { PS2_FRAME(0x2b0); kg_icon_class_b(PosY, 5, 7); }
static void MenuKeyGuideIconSet19(int PosY) { PS2_FRAME(0x2b0); kg_icon_class_b(PosY, 4, 6); }
static void MenuKeyGuideIconSet18(int PosY) { PS2_FRAME(0x160); kg_icon_class_a(PosY, 0x11a, 0x1a, 7); }
static void MenuKeyGuideIconSet17(int PosY) { PS2_FRAME(0x160); kg_icon_class_a(PosY, 0x11a, 0x1a, 6); }
static void MenuKeyGuideIconSet16(int PosY) { PS2_FRAME(0x160); kg_icon_class_a(PosY, 0x11a, 0x1a, 5); }
static void MenuKeyGuideIconSet15(int PosY) { PS2_FRAME(0x160); kg_icon_class_a(PosY, 0x11a, 0x1a, 4); }
static void MenuKeyGuideIconSet14(int PosY) { PS2_FRAME(0x2b0); kg_icon_class_b(PosY, 0, 1); }
static void MenuKeyGuideIconSet13(int PosY) { PS2_FRAME(0x160); kg_icon_class_a(PosY, 0x11a, 0x1a, 2); }
static void MenuKeyGuideIconSet12(int PosY) { PS2_FRAME(0x160); kg_icon_class_a(PosY, 0x11a, 0x1a, 3); }
static void MenuKeyGuideIconSet11(int PosY) { PS2_FRAME(0x160); kg_icon_class_a(PosY, 0x11a, 0x1a, 1); }
static void MenuKeyGuideIconSet10(int PosY) { PS2_FRAME(0x160); kg_icon_class_a(PosY, 0x11a, 0x1a, 0); }
static void MenuKeyGuideIconSet09(int PosY) { PS2_FRAME(0x530); kg_icon_class_c(PosY, 12, 13, 14, 15); }
static void MenuKeyGuideIconSet08(int PosY) { PS2_FRAME(0x2b0); kg_icon_class_b(PosY, 13, 15); }
static void MenuKeyGuideIconSet07(int PosY) { PS2_FRAME(0x2b0); kg_icon_class_b(PosY, 14, 12); }
static void MenuKeyGuideIconSet06(int PosY) { PS2_FRAME(0x2b0); kg_icon_class_b(PosY, 14, 15); }
static void MenuKeyGuideIconSet05(int PosY) { PS2_FRAME(0x2b0); kg_icon_class_b(PosY, 12, 13); }
static void MenuKeyGuideIconSet04(int PosY) { PS2_FRAME(0x160); kg_icon_class_a(PosY, 0x11a, 0x1a, 15); }
static void MenuKeyGuideIconSet03(int PosY) { PS2_FRAME(0x160); kg_icon_class_a(PosY, 0x11a, 0x1a, 14); }
static void MenuKeyGuideIconSet02(int PosY) { PS2_FRAME(0x160); kg_icon_class_a(PosY, 0x11a, 0x1a, 13); }
static void MenuKeyGuideIconSet01(int PosY) { PS2_FRAME(0x160); kg_icon_class_a(PosY, 0x11a, 0x1a, 12); }
static void MenuKeyGuideIconSet00(void) {}          /* icon id 1: no icon */

/* ---- g2 ---- */

/* raw colour words in main sdata (gp-relative), copied as 4 / 8 bytes */
#define KG_DLG_COL   0x60808080u   /* 0x27ba50 dialog window */
#define KG_ULINE_COL 0x80494949u   /* 0x27ba58 / 5c title underline */
#define KG_TITLE_COL 0x80505050u   /* 0x27ba60 title font */
#define KG_SEP_COL   0x80494949u   /* 0x27ba68 / 6c row separators (alpha >> 2 at use) */
#define KG_TEXT_COL  0x809b917du   /* 0x27ba70 row text */

/* OLMENU constants copied onto the stack */
static const int s_kg_veil[4][4] = {{0, 0, 0, 0}, {0, 448, 0, 0}, {640, 0, 0, 0}, {640, 448, 0, 0}}; /* 0x1edece0 */
static const unsigned int s_kg_veil_col[4] = {0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u};   /* 0x1eded20 */
static const int s_kg_dlg[4] = {237, 39, 392, 30};                    /* 0x1eded30 / 0x1eded40 (identical) */
static const int s_kg_title_line[2][4] = {{243, 58, 0, 0}, {623, 58, 0, 0}};   /* 0x1eded50 */
static const int s_kg_trect[4] = {351, 65, 16, 24};                   /* 0x1eded70 */
static const int s_kg_irect[4] = {295, 62, 16, 16};                   /* 0x1eded80 */
static const int s_kg_row_line[2][4] = {{243, 84, 0, 0}, {623, 84, 0, 0}};     /* 0x1eded90 */

/* MenuKeyGuideIconFunc[24] (OLMENU 0x1edec80) and MenuKeyGuideStatusFunc[3] (0x1edec70): code-pointer tables.
 * IconSet00 takes no argument; the source port reaches it through an int-taking shim. */
typedef void (*KgIconFn)(int);
typedef void (*KgStatusFn)(void);
#ifdef ACNB_SOURCEPORT
static void kg_icon_none(int) { MenuKeyGuideIconSet00(); }
#define KG_ICON00 (&kg_icon_none)
#else
#define KG_ICON00 ((KgIconFn)GFN(MenuKeyGuideIconSet00))
#endif
static const KgIconFn s_kg_icon_tbl[24] = {
    KG_ICON00,                    GFN(MenuKeyGuideIconSet01), GFN(MenuKeyGuideIconSet02), GFN(MenuKeyGuideIconSet03),
    GFN(MenuKeyGuideIconSet04), GFN(MenuKeyGuideIconSet05), GFN(MenuKeyGuideIconSet06), GFN(MenuKeyGuideIconSet07),
    GFN(MenuKeyGuideIconSet08), GFN(MenuKeyGuideIconSet09), GFN(MenuKeyGuideIconSet10), GFN(MenuKeyGuideIconSet11),
    GFN(MenuKeyGuideIconSet12), GFN(MenuKeyGuideIconSet13), GFN(MenuKeyGuideIconSet14), GFN(MenuKeyGuideIconSet15),
    GFN(MenuKeyGuideIconSet16), GFN(MenuKeyGuideIconSet17), GFN(MenuKeyGuideIconSet18), GFN(MenuKeyGuideIconSet19),
    GFN(MenuKeyGuideIconSet20), GFN(MenuKeyGuideIconSet21), GFN(MenuKeyGuideIconSet22), GFN(MenuKeyGuideIconSet23),
};
static const KgStatusFn s_kg_status_tbl[3] = {
    GFN(MenuKeyGuideStatusFadeIn), GFN(MenuKeyGuideStatusStat), GFN(MenuKeyGuideStatusFadeOut),
};

/* One row of text. Variadic in the original (MW EABI spills t0..t3 to sp+0x3b0 as the va area; its only caller
 * passes four arguments, so those words are stale and only a '%' in the data-file text would read them): ported
 * with four arguments and a zeroed va area at the original slot. The height is scaled AFTER SetFontAttr. */
static void MenuKeyGuideFontPrint(int PosX, int PosY, int Per, char *pFormat)
{
    PS2_FRAME(0x3d0);
    PS2_STACK(char[0x100], buf, 0x40);
    PS2_STACK(struct struct_acb9a91f, attr, 0x140);
    PS2_STACK(unsigned int, col, 0x36c);
    PS2_STACK(unsigned long long[4], va, 0x3b0);
    int gy;

    va[0] = 0;
    va[1] = 0;
    va[2] = 0;
    va[3] = 0;
    memset(&attr, 0, sizeof(attr));
    col = KG_TEXT_COL;
    memset(buf, 0, sizeof(buf));
    Pf_VsnPrintf(buf, 0x100, pFormat, (char *)va);
    gy = g_KeyGuideMng.PosY;
    attr.x = (short)PosX;
    attr.flag = 0x501;
    attr.strp = (unsigned char *)buf;
    attr.y = (short)(gy + (Per * ((short)PosY - gy)) / 100);
    memcpy(&attr.col, &col, sizeof(attr.col));
    DF_MFONT_SetFontAttr(&attr, 7);
    attr.h = (short)((attr.h * Per) / 100);
    DF_MFONT_Draw(&attr, 0);
}

/* the page's rows: text, button icon (IconFunc[nIconId - 1], 1-based), separator line between rows */
static void MenuKeyGuideDrawMess(struct _MENUKEYGUIDE_PAGE_ST *page, int Per)
{
    PS2_FRAME(0x100);
    PS2_STACK(int, icon_y, 0xa0);                 /* compiler temp copy of irect[1] */
    PS2_STACK(int[2][4], line, 0xb0);
    PS2_STACK(int[4], irect, 0xd0);
    PS2_STACK(struct stMenuRect, trect, 0xe0);
    PS2_STACK(struct struct_76b810ec[2], lcol, 0xf8);
    unsigned int w[2] = {KG_SEP_COL, KG_SEP_COL};
    int n, gy, pitch, text_y, i;

    memcpy(&trect, s_kg_trect, sizeof(trect));
    memcpy(irect, s_kg_irect, sizeof(irect));
    memcpy(line, s_kg_row_line, sizeof(line));
    memcpy(lcol, w, sizeof(lcol));
    lcol[0].a = (unsigned char)(lcol[0].a >> 2);
    lcol[1].a = (unsigned char)(lcol[1].a >> 2);
    n = page->nNumEntry;
    trect.h = (trect.h * Per) / 100;              /* dead store (never read) */
    gy = g_KeyGuideMng.PosY;
    line[0][1] = gy + (Per * (line[0][1] - gy)) / 100;
    line[1][1] = gy + (Per * (line[1][1] - gy)) / 100;
    pitch = (24 * Per) / 100;
    if (n <= 0)
        return;
    icon_y = irect[1];
    text_y = trect.y;
    for (i = 0; i < n; i++) {
        DF_MDRAW_KickReg(0x42, 0x8000000044ull);
        MenuKeyGuideFontPrint(trect.x, text_y + i * pitch, Per, page->aEntry[i].pText);
        gcall(s_kg_icon_tbl[page->aEntry[i].nIconId - 1], icon_y + i * pitch);   /* id 0: word before the table */
        DF_MDRAW_KickReg(0x42, 0x48);
        if (i < n - 1)
            DF_MDRAW_DrawPrim(2, 2, line, lcol, 1.0f, 1);
        line[0][1] += pitch;
        line[1][1] += pitch;
    }
}

/* "KEYGUIDE" + underline. DWARF says (int Per); the original reads Per from a1 (a0 = the page, unused). */
static void MenuKeyGuideDrawTitle(struct _MENUKEYGUIDE_PAGE_ST *page_unused, int Per)
{
    PS2_FRAME(0x280);
    PS2_STACK(int[2][4], line, 0x30);
    PS2_STACK(struct struct_acb9a91f, attr, 0x50);
    PS2_STACK(struct struct_76b810ec[2], lcol, 0x278);
    unsigned int w[2] = {KG_ULINE_COL, KG_ULINE_COL};
    unsigned int c = KG_TITLE_COL;
    int gy;

    (void)page_unused;
    memset(&attr, 0, sizeof(attr));
    memcpy(line, s_kg_title_line, sizeof(line));
    memcpy(lcol, w, sizeof(lcol));
    attr.flag = 0x800;
    attr.x = 0xfa;
    attr.y = 0x30;
    attr.strp = (unsigned char *)mkeyguide_title;
    memcpy(&attr.col, &c, sizeof(attr.col));
    DF_MFONT_SetFontAttr(&attr, 0);
    gy = g_KeyGuideMng.PosY;
    attr.y = (short)(gy + (Per * (attr.y - gy)) / 100);
    attr.h = (short)((attr.h * Per) / 100);
    DF_MFONT_Draw(&attr, 0);
    DF_MDRAW_KickReg(0x42, 0x48);
    gy = g_KeyGuideMng.PosY;
    line[0][1] = gy + (Per * (line[0][1] - gy)) / 100;
    line[1][1] = gy + (Per * (line[1][1] - gy)) / 100;
    DF_MDRAW_DrawPrim(2, 2, line, lcol, 1.0f, 1);
}

/* the whole panel for scene Id: veil, dialog window, rows, title */
void MenuKeyGuideFunc(int Id)
{
    PS2_FRAME(0xb0);
    PS2_STACK(struct stMenuRect, dlg, 0x30);
    PS2_STACK(int[4], pos, 0x40);
    PS2_STACK(struct struct_76b810ec[4], bcol, 0x50);
    PS2_STACK(int[4][4], bg, 0x60);
    PS2_STACK(struct struct_76b810ec, dcol, 0xac);
    struct _MENUKEYGUIDE_FILEHEADER_ST *file;
    struct _MENUKEYGUIDE_PAGE_ST *page;
    unsigned int c = KG_DLG_COL;
    int Per;

    memcpy(bg, s_kg_veil, sizeof(bg));
    memcpy(bcol, s_kg_veil_col, sizeof(bcol));
    memcpy(pos, s_kg_dlg, sizeof(pos));
    file = g_KeyGuideMng.pKeyGuideFile;
    if (file->nNumScene < Id)                     /* (sic) Id == nNumScene passes: one scene past the table */
        return;
    page = ((struct _MENUKEYGUIDE_SCENE_ST *)((char *)file + 0x20))[Id].aPage;
    Per = g_KeyGuideMng.Persent;
    DF_MDRAW_KickReg(0x42, ((unsigned long long)(long long)(0x80 - (Per * 64) / 100) << 32) | 0xa9);
    DF_MDRAW_DrawPrim(4, 4, bg, bcol, 1.0f, 0);
    DF_MDRAW_KickReg(0x42, 0x8000000044ull);
    g_KeyGuideMng.PosX = pos[0];
    g_KeyGuideMng.PosY = pos[1];
    memcpy(&dlg, s_kg_dlg, sizeof(dlg));
    memcpy(&dcol, &c, sizeof(dcol));
    dlg.h = dlg.h + 24 * page->nNumEntry;
    dlg.h = (dlg.h * Per) / 100;
    DF_MDRAW_KickReg(0x42, 0x6400000064ull);
    DF_MDIALOG_DrawDialogWnd(&dlg, &dcol);
    MenuKeyGuideDrawMess(page, Per);
    MenuKeyGuideDrawTitle(page, Per);
    DF_MDRAW_KickReg(0x42, 0x8000000044ull);
}

/* StatusFunc[1]: steady */
static void MenuKeyGuideStatusStat(void)
{
    g_KeyGuideMng.count = 0;
    g_KeyGuideMng.Persent = 100;
}

/* StatusFunc[2]: 5-frame fade out; State stays 2 */
static void MenuKeyGuideStatusFadeOut(void)
{
    g_KeyGuideMng.count++;
    g_KeyGuideMng.Persent = 100 - (100 * g_KeyGuideMng.count) / 5;
    if (g_KeyGuideMng.count < 5 && g_KeyGuideMng.Persent >= 0)
        return;
    g_KeyGuideMng.bIsShown = 0;
    g_KeyGuideMng.count = 0;
    g_KeyGuideMng.Persent = 0;
}

/* StatusFunc[0]: 5-frame fade in, then steady */
static void MenuKeyGuideStatusFadeIn(void)
{
    g_KeyGuideMng.count++;
    g_KeyGuideMng.Persent = (100 * g_KeyGuideMng.count) / 5;
    if (g_KeyGuideMng.count < 5 && g_KeyGuideMng.Persent < 101)
        return;
    g_KeyGuideMng.count = 0;
    g_KeyGuideMng.Persent = 100;
    g_KeyGuideMng.State = 1;
}

void MenuKeyGuide_Draw(void)
{
    PS2_FRAME(0x10);
    if (g_KeyGuideMng.bIsShown) {
        gcall(s_kg_status_tbl[(unsigned char)g_KeyGuideMng.State]);
        MenuKeyGuideFunc(g_KeyGuideMng.nScene);
    }
}

/* close on circle (Rrt) or Start while steady */
void MenuKeyGuide_Update(void)
{
    PS2_FRAME(0x10);
    struct struct_2d84b07b *pad;

    if (!g_KeyGuideMng.bIsShown || (unsigned char)g_KeyGuideMng.State == 2)
        return;
    if ((unsigned char)g_KeyGuideMng.State != 1)
        return;
    pad = &MPad[MPadUser];
    if (pad->Rrt == 1 || pad->start == 1) {
        g_KeyGuideMng.count = 0;
        g_KeyGuideMng.State = 2;
        Voice_Call(0x3ed, 0x7f);
    }
}

int MenuKeyGuide_IsShown(void)
{
    return g_KeyGuideMng.bIsShown;
}

/* (sic) no count reset: a Hide during the fade-in fades out from the stale count */
void MenuKeyGuide_Hide(void)
{
    g_KeyGuideMng.State = 2;
}

void MenuKeyGuide_Show(int nScene)
{
    g_KeyGuideMng.nScene = nScene;
    g_KeyGuideMng.count = 0;
    g_KeyGuideMng.bIsShown = 1;
    g_KeyGuideMng.Persent = 0;
    g_KeyGuideMng.State = 0;
    Voice_Call(0x3ec, 0x7f);                      /* tail call */
}

/* relocate the loaded file in place: the scene / page / entry pointer fields hold indexes (16-byte records), the
 * texts follow the entry array as NUL-terminated strings in scene -> page -> entry order. (sic) bRemapped is
 * tested but never set. */
int __MenuKeyGuideFile_Init(struct _MENUKEYGUIDE_FILEHEADER_ST *pData)
{
    PS2_FRAME(0xb0);
    struct _MENUKEYGUIDE_SCENE_ST *sc;
    struct _MENUKEYGUIDE_PAGE_ST *pg;
    char *str;
    int i, j, k;

    if (pData->bRemapped)
        return 1;
    pData->aScene = (struct _MENUKEYGUIDE_SCENE_ST *)((char *)pData + 0x20);
    pData->aPage = (struct _MENUKEYGUIDE_PAGE_ST *)((char *)pData->aScene + pData->nNumScene * 16);
    pData->aEntry = (struct _MENUKEYGUIDE_ENTRY_ST *)((char *)pData->aPage + pData->nNumPage * 16);
    str = (char *)pData->aEntry + pData->nNumEntry * 16;
    for (i = 0; i < pData->nNumScene; i++) {
        sc = &pData->aScene[i];
        sc->aPage = (struct _MENUKEYGUIDE_PAGE_ST *)((char *)pData->aPage + (int)(uintptr_t)sc->aPage * 16);
        for (j = 0; j < sc->nNumPage; j++) {
            pg = &sc->aPage[j];
            pg->aEntry = (struct _MENUKEYGUIDE_ENTRY_ST *)((char *)pData->aEntry + (int)(uintptr_t)pg->aEntry * 16);
            for (k = 0; k < pg->nNumEntry; k++) {
                pg->aEntry[k].pText = str;
                str += strlen(str) + 1;           /* newlib strlen (interpreted in the original), pure */
            }
        }
    }
    return 1;
}

int MenuKeyGuide_Cleanup(void)
{
    PS2_FRAME(0x10);
    DF_MMM_Free(0, g_KeyGuideMng.pKeyGuideFile);
    g_KeyGuideMng.pKeyGuideFile = 0;
    return 1;
}

int MenuKeyGuide_Init(void)
{
    PS2_FRAME(0x10);
    int size;

    ZeroMemory(&g_KeyGuideMng, 0x20);
    size = File_GetSize(0xfaf);
    g_KeyGuideMng.pKeyGuideFile = (struct _MENUKEYGUIDE_FILEHEADER_ST *)DF_MMM_Alloc(0, size);
    File_Read(0xfaf, -1, (char *)g_KeyGuideMng.pKeyGuideFile, 1);
    __MenuKeyGuideFile_Init(g_KeyGuideMng.pKeyGuideFile);
    return 1;
}
