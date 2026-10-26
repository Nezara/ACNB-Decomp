/* src/main/pause.cpp - the in-battle pause / end-of-match result menu: DF_PAUSE_Proc / DF_PAUSE_Restart_Proc read
 * the pad edge bits and return the chosen action after a 10-frame confirm countdown (0 = nothing yet, 1 = OK,
 * 2 = CANCEL, 3 = RESTART); DF_PAUSE_Draw paints the black box, the button strip, the captions and the
 * "Quit? / Retry? / View Replay?" message window. s_ok = selected button, s_frame = confirm countdown. */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include <string.h>

static const int pause_dim_col[4] __attribute__((aligned(16))) = {0x40, 0x40, 0x40, 0x80}; /* 0x279260 */

/* countdown finished: the action of the selected button */
static inline int pause_result(void)
{
    if (s_ok == 2) return 3;
    if (s_ok == 1) return 1;
    if (s_ok == 0) return 2;
    return 0;
}

/* modeSortie 0 (not in Versus): OK / CANCEL / RESTART, left / right wrap */
int DF_PAUSE_Restart_Proc(unsigned int key, unsigned int trg)
{
    (void)key;
    PS2_FRAME(0x10);
    if (s_frame > 0) {
        if (--s_frame > 0) return 0;
        return pause_result();
    }
    if (trg & 0x80) {
        s_ok = (s_ok + 4) % 3;
        Voice_Call(0x23, 0x3c007f);
    } else if (trg & 0x20) {
        s_ok = (s_ok + 2) % 3;
        Voice_Call(0x23, 0x3c007f);
    } else if (trg & 0x4000) {
        s_frame = 10;
    } else if (trg & 8) {
        s_ok = 0;
        s_frame = 10;
    }
    return 0;
}

int DF_PAUSE_Proc(unsigned int key, unsigned int trg)
{
    (void)key;
    PS2_FRAME(0x10);
    if (s_frame > 0) {
        if (--s_frame > 0) return 0;
        return pause_result();
    }
    if (trg & 0x80) {
        if (s_ok == 0) {
            s_ok = 1;
            Voice_Call(0x23, 0x3c007f);
        }
    } else if (trg & 0x20) {
        if (s_ok != 0) {
            s_ok = 0;
            Voice_Call(0x23, 0x3c007f);
        }
    } else if (trg & 0x4000) {
        s_frame = 10;
    } else if (trg & 8) {
        if (MyI.pause == 3) {
            s_ok = 2;
            s_frame = 10;
        } else if (MyI.pause == 1) {
            s_ok = 0;
            s_frame = 10;
        }
    }
    return 0;
}

static inline void pause_tex0(int i)
{
    struct_f386fb29 t;
    memcpy(&t, &STA2_TEXTURE[i], 8);
    GS_SetTEX0_1(t);
}

/* rgba widened from the cockpit colour (the u8 temporary lives at its own stack slot per use site) */
static inline void pause_cockpit_col(int *col, StColorU8 *tmp)
{
    DF_MFUNC_GetCockpitColor((struct_76b810ec *)tmp, 0);
    const unsigned char *c = (const unsigned char *)tmp;
    col[0] = c[0];
    col[1] = c[1];
    col[2] = c[2];
    col[3] = c[3];
}

void DF_PAUSE_Draw(void)
{
    PS2_FRAME(0x60);
    PS2_STACK(int[4], col, 0x40);       /* handed to the interpreted GS_SetRGBA */
    PS2_STACK(StColorU8, ck_b1, 0x5c);  /* cockpit colour temporaries, one per use site */
    PS2_STACK(StColorU8, ck_b2, 0x58);
    PS2_STACK(StColorU8, ck_c2, 0x54);
    PS2_STACK(StColorU8, ck_c1, 0x50);
    const int dy = MyI.pause == 3 ? -6 : 0;   /* bottom button row shift */
    const float u1 = 256.0f;

    ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 9), 9); GS_NewList();
    GS_SetTEST_1(1, 1, 0, 0, 0, 0, 0, 1);
    GS_SetALPHA_1(0, 1, 0, 1, 0);
    GS_SetALPHA_1(0, 1, 0, 1, 0);
    GS_DrawBlack(0x80, 0xa8, 0x101, 0x68, 0x40);
    GS_FlushList(); ps2_rq_tag_end();

    /* button strip */
    if (!g_sortie.pconstant->isSplit || MyI.replaymode == 1) {
        ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 9), 9); GS_NewList();
        if (MyI.camplayerno < 4) DF_ACST2_GetMyCol(col, MyI.camplayerno);
        else pause_cockpit_col(col, &ck_b1);
        GS_SetTEST_1(1, 1, 0, 0, 0, 0, 0, 1);
        pause_tex0(6);
        GS_SetRegister(0x14, 0);
        GS_SetRGBA(col);
        GS_SetALPHA_1(0, 2, 0, 1, 0);
        GS_SetTEXFLUSH();
        GS_DrawSpriteD(0x80, 0xa8, 0x180, 0xb0, 0.0f, 16.0f, u1, 24.0f, 0);
        GS_DrawSpriteD(0x80, 0x108, 0x180, 0x110, 0.0f, 32.0f, 256.0f, 40.0f, 0);
        for (int y = 8; y < 0x59; y += 8) GS_DrawSpriteD(0x80, y + 0xa8, 0x180, y + 0xb0, 0.0f, 24.0f, 256.0f, 32.0f, 0);
        GS_FlushList(); ps2_rq_tag_end();
    } else {
        ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 9), 9); GS_NewList();
        pause_cockpit_col(col, &ck_b2);
        GS_SetTEST_1(1, 1, 0, 0, 0, 0, 0, 1);
        pause_tex0(9);
        GS_SetRegister(0x14, 0);
        GS_SetRGBA(col);
        GS_SetALPHA_1(0, 2, 0, 1, 0);
        GS_SetTEXFLUSH();
        GS_DrawSpriteD(0x80, 0xa8, 0x180, 0xb0, 0.0f, 232.0f, u1, 240.0f, 0);
        GS_DrawSpriteD(0x80, 0x108, 0x180, 0x110, 0.0f, 248.0f, 256.0f, 256.0f, 0);
        for (int y = 8; y < 0x59; y += 8) GS_DrawSpriteD(0x80, y + 0xa8, 0x180, y + 0xb0, 0.0f, 240.0f, 256.0f, 248.0f, 0);
        GS_FlushList(); ps2_rq_tag_end();
    }

    /* captions and buttons (replaymode != 0 here vs == 1 above: same for 0 / 1) */
    ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 9), 9); GS_NewList();
    if (!g_sortie.pconstant->isSplit || MyI.replaymode != 0) {
        pause_tex0(5);
        if (MyI.camplayerno < 4) DF_ACST2_GetMyCol(col, MyI.camplayerno);
        else pause_cockpit_col(col, &ck_c1);
    } else {
        pause_tex0(9);
        pause_cockpit_col(col, &ck_c2);
    }
    GS_SetTEST_1(1, 1, 0, 0, 0, 0, 0, 1);
    GS_SetRegister(0x14, 0);
    GS_SetRGBA(col);
    GS_SetALPHA_1(0, 2, 0, 1, 0);
    GS_SetTEXFLUSH();
    GS_SetRGBA(col);
    if (MyI.pause == 1) {
        unsigned int m = g_sortie.pconstant->modeSortie;
        if (m - 4 < 3 || m == 2 || m == 3) StatFont_DrawString(0xc3, 0xb6, 0x90, 10, (char *)"QUIT VERSUS", 0x21);
        else StatFont_DrawString(0xc0, 0xb6, 0x90, 10, (char *)"QUIT MISSION", 0x21);
    } else if (MyI.pause == 2) {
        StatFont_DrawString(0xdb, 0xb6, 0x90, 10, (char *)"REPLAY", 0x21);
    } else {
        StatFont_DrawString(0xd2, 0xb6, 0x90, 10, (char *)"CONTINUE", 0x21);
    }
    int *dim = (int *)pause_dim_col;
    if (g_sortie.pconstant->modeSortie == 0) {
        GS_SetRGBA(s_ok == 1 ? col : dim);
        StatFont_DrawString(0x92, dy + 0xf0, 0x18, 10, (char *)"OK", 0x21);
        GS_SetRGBA(s_ok == 0 ? col : dim);
        StatFont_DrawString(0xbe, dy + 0xf0, 0x48, 10, (char *)"CANCEL", 0x21);
        GS_SetRGBA(s_ok == 2 ? col : dim);
        StatFont_DrawString(0x11a, dy + 0xf0, 0x48, 10, (char *)"RESTART", 0x21);
    } else {
        GS_SetRGBA(s_ok == 1 ? col : dim);
        StatFont_DrawString(0xc0, dy + 0xf0, 0x18, 10, (char *)"OK", 0x21);
        GS_SetRGBA(s_ok == 0 ? col : dim);
        StatFont_DrawString(0x100, dy + 0xf0, 0x48, 10, (char *)"CANCEL", 0x21);
    }
    if (MyI.pause == 3) {
        GS_SetRGBA(col);
        StatFont_DrawString(0x9b, 0xfe, 0xde, 10, (char *)"REPLAY  START", 0x21);
        GS_DrawSprite(0xe5, 0xfe, 0x9c, 0xa6, 6, 9);
        GS_DrawSprite(0x12d, 0xfd, 0, 0xd9, 0x37, 10);
    }
    GS_FlushList(); ps2_rq_tag_end();

    /* message window */
    const char *str;
    switch (g_sortie.pconstant->modeSortie) {
    default:
    case 0: str = "Abort Mission?"; break;
    case 1: str = "Abort AC test?"; break;
    case 2: case 3: case 6:
        str = MyI.pause == 1 ? "Quit?" : MyI.pause == 2 ? "View Replay?" : "Retry?";
        break;
    case 4: case 5:   /* a separate copy of the case above in the original */
        str = MyI.pause == 1 ? "Quit?" : MyI.pause == 2 ? "View Replay?" : "Retry?";
        break;
    }
    int len = (int)strlen(str);
    float w = 16.0f * (0.25f * (float)len);
    int x = ps2_ftoi(u1 - w);
    MsgWnd_Draw(x, MyI.pause == 3 ? 0x68 : 0x67, (unsigned char *)str, (len >> 1) * 8 + 8);
}

void DF_PAUSE_Init(void)
{
    s_ok = 0;
    s_frame = 0;
}
