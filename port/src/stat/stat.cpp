/* src/stat/stat.cpp - sortie message window (whole file): s_msgwndSortie shows arbiter / COM messages (with voice),
 * s_msgwndQuit the quit / connection-error prompts. State on: 1 open (window grows, voice starts at cnt 2),
 * 2 text (typed out at 2 characters per frame; mwait frames, 0 = until closed), 3 close. MsgWnd_Draw draws a
 * string with the 8x16 message font (s_bufFont, uploaded per character); '_' + 3 digits sets the colour, '\n'
 * starts a new line and lets the voice go (vgo) once the typing has reached it.
 * Quirks: MsgWnd_ShowQuit clears s_msgwndSortie.son and sets s_msgwndSortie.ms_type instead of the quit window's;
 * MsgWnd_Draw's line-feed voice trigger always tests s_msgwndSortie (also for the quit window); the original
 * passes extra arguments to the voice calls (vh - 1, 0 / volume 0x7f) that the callees ignore. */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

#define A16 __attribute__((aligned(16)))

/* window colour: the camera player's cockpit colour (cockpit colour 0 in split screen or with a free camera) */
static void msgwnd_col(int *rgba)
{
    struct_76b810ec c;
    if (!g_sortie.pconstant->isSplit || MyI.replaymode) {
        if (MyI.camplayerno < 4) {
            DF_ACST2_GetMyCol(rgba, MyI.camplayerno);
            return;
        }
        DF_MFUNC_GetCockpitColor(&c, 0);
    } else {
        DF_MFUNC_GetCockpitColor(&c, 0);
    }
    rgba[0] = c.r;
    rgba[1] = c.g;
    rgba[2] = c.b;
    rgba[3] = c.a;
}

void MsgWnd_CancelVoice(void)
{
    PS2_FRAME(0x10);
    if (s_msgwndSortie.vh > 0) Voice_StopMessage();
}

void MsgWnd_DrawQuit(void)
{
    PS2_FRAME(0x10);
    StMessageSys *m = &s_msgwndQuit;
    switch (m->on) {
    case 1:
        __DrawMsgWnd(m->cnt);
        if (++m->cnt > 0x15) {
            m->cnt = 0;
            m->on++;
        }
        break;
    case 2:
        __DrawMsgWnd(999);
        MsgWnd_Draw(0x88, 0xa9, (unsigned char *)m->mess, m->cnt);
        m->cnt++;
        if (m->mwait != 0 && m->cnt >= m->mwait) {
            m->cnt = 0x15;
            m->on++;
        }
        break;
    case 3:
        __DrawMsgWnd(m->cnt);
        if (--m->cnt <= 0) {
            m->on = 0;
            m->cnt = 0;
            m->vh = 0;
        }
        break;
    }
}

void MsgWnd_DrawAfter(void)
{
    PS2_FRAME(0x30);
    PS2_RQ_SCOPE(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 13), 13);   /* render tag (host only): message window */
    GS_NewList();
    GS_SetRegister(0x42, 0x44);
    GS_SetRegister(0x47, 0x3800f);
    GS_SetCLAMP_1(1, 1, 0, 0, 0, 0);
    GS_FlushList();
    unsigned char text = g_gamedata[0].option.textmode;
    if (MyI.pause) return;
    StMessageSys *m = &s_msgwndSortie;
    switch (m->on) {
    case 1:
    case 3:
        if (text) __DrawMsgWnd(m->cnt);
        break;
    case 2:
        if (text) __DrawMsgWnd(999);
        if (text) MsgWnd_Draw(0x88, 0xa9, (unsigned char *)m->mess, m->cnt);
        break;
    }
}

void MsgWnd_Draw(int x, int y, unsigned char *mj, int cnt)
{
    PS2_FRAME(0xe0);
    float st[4] A16;
    int xyz[2][4] A16;
    int x0 = x;
    y -= 2;
    DF_SetAlphaRegister(0, 1, 0, 1, 0, 1);
    st[0] = 0.0f;
    st[1] = 0.0f;
    st[2] = 0.25f;
    xyz[1][3] = 0;
    st[3] = 0.5625f;
    xyz[0][3] = 0;
    msgwnd_col(s_rgbFont);
    unsigned long long tex0_bits = 0x4007f80555407a60ull;   /* tbp0 0x3a60 tbw 1 psm 0x14 4-bit, 32x32, clut 0x3fc0 */
    struct_1f802d8d tex0;
    memcpy(&tex0, &tex0_bits, 8);
    int shown = cnt * 2, pos = 0;
    for (unsigned int c = *mj; c != 0; c = *mj) {
        if (c & 0x80) {   /* 2-byte SJIS: converted, not drawn */
            kanji_sjis2euc(c, mj[1]);
            mj += 2;
        } else if (c == ' ') {
            x += 8;
            mj++;
        } else if (c == '_') {
            s_rgbFont[0] = (mj[1] - '0') * 0x10;
            s_rgbFont[1] = (mj[2] - '0') * 0x10;
            s_rgbFont[2] = (mj[3] - '0') * 0x10;
            mj += 4;
        } else if (c == '\n') {
            x = x0;
            y += 10;
            mj++;
            if (pos <= shown && s_msgwndSortie.s_handle != 1 && !s_msgwndSortie.vgo) s_msgwndSortie.vgo = 1;
        } else if (c < 0x21 || c >= 0x7f) {
            mj++;
        } else {
            int n = (int)c - 0x21, odd = n % 2;
            if (odd) {
                st[0] = 0.25f;
                st[2] = 0.5f;
            } else {
                st[0] = 0.0f;
                st[2] = 0.25f;
            }
            n -= odd;
            mj++;
            GS_NewList();
            GS_LoadImage(s_bufFont + n * 0x48 + 0x40, 0x3a60, 0x14, 0x10, 0x12);
            GS_FlushList();
            if (pos > shown) {
                s_rgbFont[3] = 0;
            } else {
                int a = shown - pos;
                if (a >= 0x10) a = 0x10;
                s_rgbFont[3] = a << 3;
            }
            xyz[0][0] = (x + 0x700) << 4;
            xyz[0][1] = (y + 0x790) << 4;
            xyz[0][2] = 0x124f800;
            xyz[1][0] = (x + 0x708) << 4;
            xyz[1][1] = (y + 0x79a) << 4;
            xyz[1][2] = 0x124f800;
            DF_FontSpriteKick(s_rgbFont, xyz, tex0, st, 0x56, 1);
            x += 8;
            pos += 8;
            ps2_gif_wait_idle();
        }
    }
    DF_ACST2_GetMyCol(s_rgbFont, MyI.myplayerno[0]);
}

void MsgWnd_ClearQuit(void)
{
    s_msgwndQuit.on = 0;
    s_msgwndQuit.cnt = 0;
    s_msgwndQuit.snd_id = 0;
    s_msgwndQuit.s_handle = 0;
    s_msgwndQuit.vh = 0;
    s_msgwndQuit.ilinkerr = 0;
    s_msgwndQuit.ms_type = 0;
}

void MsgWnd_Clear(void)
{
    s_msgwndSortie.on = 0;
    s_msgwndSortie.cnt = 0;
    s_msgwndSortie.snd_id = 0;
    s_msgwndSortie.s_handle = 0;
    s_msgwndSortie.vgo = 0;
    s_msgwndSortie.son = 0;
    s_msgwndSortie.vh = 0;
    s_msgwndSortie.ms_type = 0;
}

/* arbiter message (idSnd = message voice) */
void MsgWnd_ShowArb(char *pstrMsg, int idSnd)
{
    PS2_FRAME(0x30);
    Voice_StopMessage();
    s_msgwndSortie.snd_id = idSnd;
    s_msgwndSortie.on = 1;
    s_msgwndSortie.cnt = 0;
    int len = (int)strlen(pstrMsg);
    s_msgwndSortie.s_handle = 0;
    s_msgwndSortie.vh = 0;
    s_msgwndSortie.vgo = 0;
    s_msgwndSortie.mwait = len * 4 + 0x96;
    s_msgwndSortie.ms_type = 1;
    s_msgwndSortie.son = 0;
    strcpy(s_msgwndSortie.mess, pstrMsg);
}

/* COM message: pstrMsg 0 = system message idComMsg (text + voice), else the COM voice of idComMsg */
void MsgWnd_ShowCom(short idComMsg, char *pstrMsg)
{
    PS2_FRAME(0x40);
    Voice_StopComt();
    unsigned short snd;
    if (!pstrMsg) {
        StComMsgUnit unit;
        if (!ComMsg_GetSystemMessage(&unit, idComMsg)) return;
        pstrMsg = unit.pstrMsg;
        snd = unit.idSnd;
    } else {
        snd = ComMsg_GetSoundId(idComMsg);
        if (!snd) return;
    }
    s_msgwndSortie.snd_id = snd;
    s_msgwndSortie.on = 1;
    s_msgwndSortie.cnt = 0;
    int len = (int)strlen(pstrMsg);
    s_msgwndSortie.vh = 0;
    s_msgwndSortie.vgo = 0;
    s_msgwndSortie.son = 0;
    s_msgwndSortie.mwait = len * 2 + 0x96;
    s_msgwndSortie.s_handle = 1;
    s_msgwndSortie.ms_type = 2;
    strcpy(s_msgwndSortie.mess, pstrMsg);
}

void MsgWnd_ShowQuit(int mode)
{
    PS2_FRAME(0x10);
    s_msgwndQuit.cnt = 0;
    s_msgwndQuit.mwait = 0;
    s_msgwndQuit.on = 1;
    s_msgwndQuit.snd_id = -1;
    s_msgwndQuit.s_handle = 0;
    s_msgwndQuit.vh = 0;
    s_msgwndSortie.son = 0;
    s_msgwndSortie.ms_type = 3;
    const char *s;
    int ilinkerr;
    switch (mode) {
    case 1: s = "    Abort AC test?"; ilinkerr = 0; break;
    case 2: s = "    Connection Error."; ilinkerr = 1; break;
    case 4: s = "    Canceling connection.\nThe number of linked systems changed."; ilinkerr = 1; break;
    case 5: s = "    PlayStation BB Unit Error."; ilinkerr = 0; break;
    case 3: s = "    Canceling Connection."; ilinkerr = 1; break;
    default: s = "    Abort Mission?"; ilinkerr = 0; break;
    }
    strcpy(s_msgwndQuit.mess, s);
    s_msgwndQuit.ilinkerr = (char)ilinkerr;
}

int MsgWnd_IsBusy(int isSortie)
{
    return (isSortie ? s_msgwndSortie.on : s_msgwndQuit.on) > 0;
}

void MsgWnd_Exec(void)
{
    PS2_FRAME(0x20);
    unsigned char text = g_gamedata[0].option.textmode;
    if (MyI.pause) return;
    StMessageSys *m = &s_msgwndSortie;
    switch (m->on) {
    case 1:
        if (!text) m->vgo = 1;
        m->cnt++;
        if (m->cnt == 1 && !m->son && m->snd_id > 0 && m->s_handle != 1) m->son = (char)Voice_Call(0xe, 0x60);
        if (m->cnt == 2 && m->snd_id > 0) {
            if (m->s_handle == 1) m->vh = Voice_CallComt(m->snd_id);
            else m->vh = Voice_CallMessage(m->snd_id);
            m->vh++;
            m->vgo = 2;
        }
        if (m->cnt >= 0x10) {
            m->on++;
            m->cnt = 0;
            if (m->s_handle == 1 && !m->vgo) m->vgo = 1;
        }
        break;
    case 2:
        m->cnt++;
        if (m->mwait != 0 && m->cnt >= m->mwait) {
            m->on++;
            m->cnt = 0xf;
        }
        break;
    case 3:
        m->cnt--;
        m->vgo = 0;
        if (m->cnt <= 0) {
            m->on = 0;
            m->cnt = 0;
            m->vh = 0;
        }
        break;
    }
}

/* loads the font conversion table and glyphs, uploads the font CLUT (once) */
void MsgWnd_Init(void)
{
    PS2_FRAME(0x70);
    if (s_flagInited) return;
    PS2_STACK(unsigned char[0x60], li, 0x10);
    Cdvd_GetSize2048(0xa8c);
    File_Read(0xa8c, -1, (char *)s_tblEuc, 1);
    Cdvd_GetSize2048(0xa8e);
    File_Read(0xa8e, -1, s_bufFont, 1);
    sceGsSetDefLoadImage(li, 0x3a60, 0, 0x14, 0, 0, 8, 0x12);
    FlushCache(0);
    sceGsExecLoadImage(li, s_bufFont + 0x40);
    ps2_gif_wait_idle();
    sceGsSetDefLoadImage(li, 0x3fc0, 1, 0, 0, 0, 8, 2);
    FlushCache(0);
    sceGsExecLoadImage(li, s_bufFont);
    ps2_gif_wait_idle();
    tex_flush();
    s_flagInited = 1;
}

/* window frame: grows in over cnt 0..7, the cursor blinks in from 8..15 (cnt 999 = open) */
static void __DrawMsgWnd(int cnt)
{
    PS2_FRAME(0xa0);
    int x = 0x80, y = 0x140, h = 0x68, w = 0x101, alpha = 0x40, a = 0x7f;
    float blink = 0.5f + 0.5f * Sintable[(FRAME << 6) & 0xfff];
    if (cnt < 8) {
        float t = (float)cnt / 8.0f;
        w = ps2_ftoi(257.0f * t);
        h = ps2_ftoi(104.0f * t);
        x = (0x101 - w) / 2 + 0x80;
        y = (0x68 - h) / 2 + 0x140;
        blink = 0.0f;
        alpha = cnt << 3;
        a = 0;
    } else if (cnt < 0x10) {
        float t = (float)(cnt - 8) / 8.0f;
        blink = blink * t;
        a = ps2_ftoi(127.0f * t);
    }
    int ca = ps2_ftoi(128.0f * blink);
    GS_NewList();
    GS_SetTEST_1(1, 1, 0, 0, 0, 0, 1, 1);
    GS_SetALPHA_1(0, 1, 0, 1, 0);
    GS_DrawBlack(x, y, w, h, alpha);
    GS_SetALPHA_1(0, 2, 0, 1, 0);
    int rgba[4] A16;
    msgwnd_col(rgba);
    rgba[3] = a;
    GS_SetRGBA(rgba);
    struct_f386fb29 tex0;
    memcpy(&tex0, &STA2_TEXTURE[6], 8);
    GS_SetTEX0_1(tex0);
    GS_SetCLAMP_1(1, 1, 0, 0, 0, 0);
    GS_DrawSpriteD(0x80, 0x140, 0x181, 0x150, 0.0f, 0.0f, 256.0f, 16.0f, 0);
    GS_DrawSpriteD(0x80, 0x1a0, 0x181, 0x1a8, 0.0f, 32.0f, 256.0f, 40.0f, 0);
    for (int i = 0x10; i < 0x60; i += 8)
        GS_DrawSpriteD(0x80, i + 0x140, 0x181, i + 0x148, 0.0f, 24.0f, 256.0f, 32.0f, 0);
    rgba[0] = rgba[1] = rgba[2] = 0x7f;
    rgba[3] = ca;
    GS_SetRGBA(rgba);
    GS_DrawSpriteD(0x78, 0x138, 0x98, 0x158, 186.0f, 42.0f, 218.0f, 74.0f, 0);
    GS_FlushList();
}
