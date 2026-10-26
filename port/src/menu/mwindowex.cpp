/* src/menu/mwindowex.cpp — WindowEx (OLMENU): the generic menu window / label object. An stMobj of class StMWindow
 * (created through the mobjex class machinery, __MObj_New(parent, &StMWindow_s_ClassInfo)) holding a tweened rectangle
 * (_MOVERRECT), a tweened fade alpha (_MOVERI, 0..0x80), a base colour, a GS ALPHA register value, a window type
 * (WINDOWEX_TYPE_*) and an optional label string. __WindowEx_Draw picks one of ~20 mdraw / mdialog frame drawers by
 * type, __WindowEx_DrawString prints the label for the text-bearing types. play / draw / fade-in / fade-out slots live
 * in the class info, filled by StMWindow_InitClassInfo.
 * The object data has no DWARF member in the stMobj dt union: _StMWindow overlays the whole object.
 * NULL pMobj: most setters store through `pMobj ? pMobj : 0` (PS2 address = field offset); no caller passes NULL, so the
 * port returns early there instead of writing PS2 low memory. */
#include <string.h>

static inline struct _StMWindow *win_of(struct stMobj *pMobj)
{
    return (struct _StMWindow *)pMobj;
}

/* sdata colour words (gp-0x6758 / -0x6754 / -0x6750 / -0x674c), little-endian r,g,b,a */
static const unsigned int WINDOWEX_STR_COL = 0x8092928c;
static const unsigned int WINDOWEX_STR_COL_ALT = 0x80606060;
static const unsigned int WINDOWEX_ICON_COL = 0x00808080;

/* OLMENU templates 0x1ee4a60..0x1ee4aaf */
static const int WINDOWEX_ICON_RECT[4] = {0, 0, 0x14, 0x14};           /* 0x1ee4a60 */
static const int WINDOWEX_UV_FLAG2[4] = {0xba, 0x22, 0xce, 0x36};       /* 0x1ee4a70 */
static const int WINDOWEX_UV_FLAG4[4] = {0, 0xa0, 0x14, 0xb4};          /* 0x1ee4a80 */
static const int WINDOWEX_UV_SLOT[4] = {0x2b, 0x1d, 0x39, 0x2d};        /* 0x1ee4a90 */
static const int WINDOWEX_UV_SLOT_SEL[4] = {0xc0, 0x38, 0xce, 0x48};    /* 0x1ee4aa0 */

static inline struct struct_76b810ec windowex_col(unsigned int w)
{
    struct struct_76b810ec c;
    memcpy(&c, &w, 4);
    return c;
}

void WindowEx_SetAlphaReg(struct stMobj *pMobj, unsigned long long alpha_reg)
{
    if (!pMobj) return;   /* original: sd at PS2 address 0x78 */
    win_of(pMobj)->ulAlphaReg = alpha_reg;
}

/* label text for the text-bearing types (any other type draws nothing) */
void __WindowEx_DrawString(struct stMobj *pMobj)
{
    PS2_FRAME(0x270);
    struct _StMWindow *w = win_of(pMobj);
    int ox = w->strx;
    int oy = w->stry;
    struct struct_76b810ec col = windowex_col(WINDOWEX_STR_COL);
    struct struct_76b810ec alt = windowex_col(WINDOWEX_STR_COL_ALT);
    int font = 3;
    unsigned int flag = 0;
    if (w->flags & 8) col = alt;
    col.a = (unsigned char)((col.a * w->AlphaMover.Cur) / 128);
    if (!w->pStr) return;
    switch (w->Type) {
    case 0xd:
    case 3:
        font = 2;
        break;
    case 0x23:
        font = 2;
        break;
    case 0x22:
        flag = 1;
        break;
    case 0x21:
    case 0x20:
        ox = 0x14;
        oy = 0x11;
        flag = 1;
        break;
    case 0x1f:
    case 0x1e:
        ox = 8;
        oy = 0x11;
        flag = 1;
        break;
    default:
        return;
    }
    struct struct_acb9a91f attr;
    memset(&attr, 0, sizeof(attr));
    attr.flag = flag;   /* before SetFontAttr: it reads flag */
    DF_MFONT_SetFontAttr(&attr, font);
    attr.x = (short)(w->RectMover.Cur.x + ox);
    attr.y = (short)(w->RectMover.Cur.y + oy);
    attr.strp = (unsigned char *)w->pStr;
    attr.col = col;
    DF_MFONT_Draw(&attr, 0);
}

void WindowEx_SetStringPos(struct stMobj *pMobj, int x, int y)
{
    if (!pMobj) return;   /* original: sh at PS2 0x8a / 0x8c */
    win_of(pMobj)->strx = (short)x;
    win_of(pMobj)->stry = (short)y;
}

void WindowEx_GetRect(struct stMobj *pMobj, struct stMenuRect *pRect)
{
    if (!pMobj) {   /* original: reads PS2 0x1c..0x28 */
        memcpy(pRect, (unsigned char *)PS2_NULL_MEM() + 0x1c, 0x10);
        return;
    }
    *pRect = win_of(pMobj)->RectMover.Cur;
}

void WindowEx_SetString(struct stMobj *pMobj, char *str)
{
    if (!pMobj) return;   /* original: sw at PS2 0x84 */
    win_of(pMobj)->pStr = str;
}

void WindowEx_BlinkEnd(struct stMobj *pMobj)
{
    if (!pMobj) return;   /* original: RMW at PS2 0x90 */
    win_of(pMobj)->flags &= 0xfe;
}

void WindowEx_BlinkBegin(struct stMobj *pMobj)
{
    if (!pMobj) return;   /* original: RMW at PS2 0x90 */
    win_of(pMobj)->flags |= 1;
}

void WindowEx_ResetRect(struct stMobj *pMobj, int count)
{
    struct _StMWindow *w = win_of(pMobj);
    MoverRect_Move(&w->RectMover, &w->InitRect, count);
}

int WindowEx_IsMoving(struct stMobj *pMobj)
{
    return Mover_IsMove((struct _MOVERBASE *)&win_of(pMobj)->RectMover);
}

int WindowEx_MoveToLayout(struct stMobj *pMobj, char *pLayoutHeader, char *partsunitname, int count)
{
    PS2_FRAME(0x80);
    PS2_STACK(struct _LAYOUT_RET_DATA, lay, 0x30);
    if (!DF_LO_GetLayoutData((unsigned char *)pLayoutHeader, (unsigned char *)partsunitname, 0, &lay)) return 0;
    struct _StMWindow *w = win_of(pMobj);
    if (count == 0)
        MoverRect_SetValue(&w->RectMover, (struct stMenuRect *)&lay.Pos);
    else
        MoverRect_Move(&w->RectMover, (struct stMenuRect *)&lay.Pos, count);
    return 1;
}

void WindowEx_SetCurMoveCount(struct stMobj *pMobj, int count)
{
    if (!pMobj) return;   /* original: sw at PS2 0x14 */
    win_of(pMobj)->RectMover.nCount = count;
}

void WindowEx_MoveAddPos(struct stMobj *pMobj, struct struct_214acd28 *pos, int count)
{
    PS2_FRAME(0x20);
    struct _StMWindow *w = win_of(pMobj);
    struct stMenuRect rect = w->RectMover.Cur;
    rect.x += pos->x;
    rect.y += pos->y;
    if (count == 0)
        MoverRect_SetValue(&w->RectMover, &rect);
    else
        MoverRect_Move(&w->RectMover, &rect, count);
}

void WindowEx_MovePos(struct stMobj *pMobj, struct struct_214acd28 *pos, int count)
{
    PS2_FRAME(0x20);
    struct _StMWindow *w = win_of(pMobj);
    struct stMenuRect rect = w->RectMover.Cur;
    rect.x = pos->x;
    rect.y = pos->y;
    if (count == 0)
        MoverRect_SetValue(&w->RectMover, &rect);
    else
        MoverRect_Move(&w->RectMover, &rect, count);
}

void WindowEx_Move(struct stMobj *pMobj, struct stMenuRect *pRect, int count)
{
    PS2_FRAME(0x10);
    struct _StMWindow *w = win_of(pMobj);
    if (count == 0)
        MoverRect_SetValue(&w->RectMover, pRect);
    else
        MoverRect_Move(&w->RectMover, pRect, count);
}

void WindowEx_SetFadeOut(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    if (!pMobj) return;
    MoverI_Move(&win_of(pMobj)->AlphaMover, 0, 6);
    pMobj->stat = 2;
}

/* no NULL test (NULL would store stat at PS2 address 2) */
void WindowEx_SetFadeIn(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct _StMWindow *w = win_of(pMobj);
    MoverI_SetValue(&w->AlphaMover, 0);
    MoverI_Move(&w->AlphaMover, 0x80, 6);
    pMobj->stat = 1;
}

/* both movers update every frame; fade-out ends on the alpha mover alone */
void __WindowEx_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct _StMWindow *w = win_of(pMobj);
    int a = MoverI_Update(&w->AlphaMover);
    int r = MoverRect_Update(&w->RectMover);
    if (w->flags & 1) w->ucBlinkCount++;
    if (pMobj->stat == 1) {
        if (!a && !r) pMobj->stat = 0;
    } else if (pMobj->stat == 2) {
        if (!a) pMobj->stat = 5;
    }
}

void WindowEx_SetColor(struct stMobj *pMobj, struct struct_76b810ec *pColor)
{
    memcpy(&win_of(pMobj)->Color, pColor, 4);
}

void WindowEx_SetFlag(struct stMobj *pMobj, unsigned int flag)
{
    if (!pMobj) return;
    win_of(pMobj)->flags = flag;
}

unsigned int WindowEx_GetFlag(struct stMobj *pMobj)
{
    if (!pMobj) return 0;
    return win_of(pMobj)->flags;
}

/* spot rect around the window: {x - w/4, y - h/2, 3w/2, 2h} (signed divisions round toward zero) */
static void windowex_draw_spot(const struct stMenuRect *rect, struct struct_76b810ec col)
{
    struct stMenuRect spot;
    spot.x = rect->x - rect->w / 4;
    spot.y = rect->y - rect->h / 2;
    spot.w = (rect->w * 3) / 2;
    spot.h = rect->h * 2;
    DF_MDRAW_DrawSpot(&spot, &col, 0);
}

void __WindowEx_Draw(struct stMobj *pMobj)
{
    PS2_FRAME(0xe0);
    struct _StMWindow *w = win_of(pMobj);
    struct struct_76b810ec col = w->Color;
    struct stMenuRect rect = w->RectMover.Cur;
    col.a = (unsigned char)((w->Color.a * w->AlphaMover.Cur) / 128);
    if (MenuMng.f_help == 1 && !pMobj->f_help) col.a = col.a >> 1;
    /* blink factor, applied to every window: k = 128..255 (243 for a window that never blinks) */
    int k = (((255 - w->ucBlinkCount) % 63) * 4) - 127;
    if (k < 0) k = -k;
    k += 0x80;
    col.a = (unsigned char)((col.a * k) >> 8);
    DF_MDRAW_KickReg(0x42, w->ulAlphaReg);

    switch (w->Type) {
    case 0xe: {   /* OPARTS_SLOT: icon sprites, own colour (fade alpha only) */
        struct stMenuRect r = w->RectMover.Cur;
        int uvA[4], uvB[4];
        memcpy(uvA, WINDOWEX_UV_SLOT, 0x10);
        memcpy(uvB, WINDOWEX_UV_SLOT_SEL, 0x10);
        struct struct_76b810ec c = windowex_col(WINDOWEX_ICON_COL);
        c.a = (unsigned char)w->AlphaMover.Cur;
        DF_MDRAW_KickReg(0x42, 0x44);
        DF_MDRAW_DrawTexSpriteI(0x3e9, &r, uvA, &c, 0);
        if (w->flags & 2) {
            DF_MDRAW_KickReg(0x42, 0x48);
            DF_MDRAW_DrawTexSpriteI(0x3ec, &r, uvB, &c, 0);
        }
        break;
    }
    case 0x23:
    case 0x22: DF_MDRAW_DrawGeneBoard(&rect, &col); break;   /* asm also passes depth 0 in a2 */
    case 0x21: DF_MDRAW_DrawReport2(&rect, &col, 0, 1); break;
    case 0x20: DF_MDRAW_DrawReport2(&rect, &col, 0, 0); break;
    case 0x1f: DF_MDRAW_DrawReport1(&rect, &col, 0, 1); break;
    case 0x1e: DF_MDRAW_DrawReport1(&rect, &col, 0, 0); break;
    case 0x25: windowex_draw_spot(&rect, col); break;   /* SPOT */
    case 0xb:   /* LIGHTCURSOR_SPOT */
        DF_MDRAW_DrawLightCursor(&rect, &col, 0);
        windowex_draw_spot(&rect, col);
        break;
    case 0xd:   /* STRSEL_FIX_RE + optional icon */
        DF_MDRAW_DrawStrSelFixRE(&rect, &col, 0);
        if (w->flags & 6) {
            struct stMenuRect r;
            memcpy(&r, WINDOWEX_ICON_RECT, 0x10);
            r.x = w->RectMover.Cur.x + 0x10f;
            r.y = w->RectMover.Cur.y + 1;
            struct struct_76b810ec c = windowex_col(WINDOWEX_ICON_COL);
            c.a = (unsigned char)w->AlphaMover.Cur;
            DF_MDRAW_KickReg(0x42, 0x48);
            int uv[4];
            if (w->flags & 2) {
                memcpy(uv, WINDOWEX_UV_FLAG2, 0x10);
                DF_MDRAW_DrawTexSpriteI(0x3ec, &r, uv, &c, 0);
            } else if (w->flags & 4) {
                memcpy(uv, WINDOWEX_UV_FLAG4, 0x10);
                DF_MDRAW_DrawTexSpriteI(0x3ec, &r, uv, &c, 0);
            }
        }
        break;
    case 0x24: DF_MDRAW_DrawLightCursor(&rect, &col, 0); break;
    case 0xa: DF_MDRAW_DrawAcWakuC(&rect, &col); break;   /* asm passes a2 = 0 */
    case 9: DF_MDRAW_DrawAcWakuB(&rect, &col); break;
    case 8: DF_MDRAW_DrawAcWakuA(&rect, &col); break;
    case 7: DF_MDRAW_DrawSimpleLabelUpr(&rect, &col, 0); break;
    case 6: DF_MDRAW_DrawSimpleLabelBtm(&rect, &col, 0); break;
    case 5: DF_MDRAW_DrawAcWakuBtm(&rect, &col); break;
    case 4: DF_MDRAW_DrawSimpleLabel(&rect, &col, 0); break;
    case 3: DF_MDRAW_DrawStrSelFixR(&rect, &col, 0); break;
    case 2: DF_MDIALOG_DrawDialogWnd(&rect, &col); break;
    case 0xf: DF_MDRAW_DrawSimpleLabelL(&rect, &col, 0); break;
    case 1: DF_MDRAW_DrawSimpleLabelR(&rect, &col, 0); break;
    default: break;
    }
    __WindowEx_DrawString(pMobj);
}

/* str and the other fields are left as they are */
void WindowEx_Delete(struct stMobj *pMobj)
{
    if (!pMobj) return;
    pMobj->f_draw = 0;
    pMobj->f_erase = 1;
}

/* the layout result is not tested: on failure the stale stack buffer feeds rect / colour / blend */
struct stMobj *WindowEx_NewFromLayout(struct stMobj *pParent, _WINDOWEX_TYPE type, char *pLayoutHeader,
                                      char *partsunitname)
{
    PS2_FRAME(0x80);
    PS2_STACK(struct _LAYOUT_RET_DATA, lay, 0x30);
    DF_LO_GetLayoutData((unsigned char *)pLayoutHeader, (unsigned char *)partsunitname, 0, &lay);
    if (type == 0) type = (_WINDOWEX_TYPE)lay.CommonID[0];
    struct stMobj *obj = __MObj_New(pParent, &StMWindow_s_ClassInfo.Base.Base);
    if (obj) {
        struct _StMWindow *w = win_of(obj);
        w->Type = type;
        memcpy(&w->InitRect, &lay.Pos, 0x10);
        w->Color = lay.Col;
        w->ulAlphaReg = 0x44;
        MoverRect_SetValue(&w->RectMover, (struct stMenuRect *)&lay.Pos);
        MoverI_SetValue(&w->AlphaMover, 0x80);
        if (type == 0x24) w->flags |= 1;
    }
    unsigned long long alpha = (unsigned long long)lay.Blend[0] | (unsigned long long)lay.Blend[1] << 2 |
                               (unsigned long long)lay.Blend[2] << 4 | (unsigned long long)lay.Blend[3] << 6 |
                               (unsigned long long)lay.Blend[4] << 32;
    /* NULL obj (allocation failure): the original stores alpha at PS2 address 0x78 - skipped */
    if (obj) win_of(obj)->ulAlphaReg = alpha;
    return obj;
}

/* no NULL test of rect / color */
struct stMobj *WindowEx_New(struct stMobj *pParent, _WINDOWEX_TYPE type, struct stMenuRect *rect,
                            struct struct_76b810ec *color)
{
    PS2_FRAME(0x50);
    struct stMobj *obj = __MObj_New(pParent, &StMWindow_s_ClassInfo.Base.Base);
    if (!obj) return 0;
    struct _StMWindow *w = win_of(obj);
    w->Type = type;
    w->InitRect = *rect;
    w->Color = *color;
    w->ulAlphaReg = 0x44;
    MoverRect_SetValue(&w->RectMover, rect);
    MoverI_SetValue(&w->AlphaMover, 0x80);
    if (type == 0x24) w->flags |= 1;
    return obj;
}

void StMWindow_InitClassInfo(struct _StMWindow_CLASSINFO *pClassInfo)
{
    struct _MOBJ_BASE_ST_CLASSINFO *ci = &pClassInfo->Base.Base;
    ci->play_func = (void (*)(struct stMobj *))GFN(__WindowEx_Play);
    ci->draw_func = (void (*)(struct stMobj *))GFN(__WindowEx_Draw);
    ci->fadein_func = (void (*)(struct stMobj *))GFN(WindowEx_SetFadeIn);
    ci->fadeout_func = (void (*)(struct stMobj *))GFN(WindowEx_SetFadeOut);
}

#ifdef ACNB_SOURCEPORT
/* class definition (signature "MOBJ"; base class StMobjCmdTarget); the rest is filled by StMWindow_InitClassInfo */
struct _StMWindow_CLASSINFO StMWindow_s_ClassInfo = {.Base = {.Base = {.signature = 0x4a424f4d, .name = (char *)"StMWindow",
    .size = 0xc0, .pBaseClass = &StMobjCmdTarget_s_ClassInfo.Base,
    .fpInitClassInfo = (void (*)(struct _MOBJ_BASE_ST_CLASSINFO *))StMWindow_InitClassInfo}}};
#endif
