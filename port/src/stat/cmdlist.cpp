/* src/stat/cmdlist.cpp - the cockpit HUD command list interpreter (draw_cmdlist) and the commands Versus runs.
 * A command list (from the cockpit data) is a sequence of 8-byte commands: byte 0 = opcode (index into cmd_exec),
 * short at +2 = argument, then either two shorts (x, y at +4 / +6) or an int (skip count at +4); sprite and gauge
 * commands take a second 8-byte word (u, v at +8 / +0xa, w, h at +0xc / +0xe). Conditional commands skip the
 * following n commands when their condition is false. Opcode >= 0x35 or an empty slot ends the list.
 * Drawing goes through the libgs display-list helpers (GS_*), which stay for the render detour. */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

#define A16 __attribute__((aligned(16)))

typedef unsigned long long Cmd;

static short cmd_s(const Cmd *ds, int off) { short v; memcpy(&v, (const char *)ds + off, 2); return v; }
static int cmd_i(const Cmd *ds, int off) { int v; memcpy(&v, (const char *)ds + off, 4); return v; }

/* skip form: the next command, or n further on when the condition fails */
static Cmd *cmd_skip(Cmd *ds, int cond) { return cond ? ds + 1 : ds + cmd_i(ds, 4) + 1; }

/* the colour of the last exec_rgba with a 0..128 alpha pulse sin(phase) (phase 0..63) */
static void set_rgba_pulse(int phase)
{
    int col[4] A16 = {s_rgba[0], s_rgba[1], s_rgba[2], 0};
    col[3] = ps2_ftoi(128.0f * (0.5f + 0.5f * Sintable[(phase << 6) & 0xfff]));
    GS_SetRGBA(col);
}

/* calls the training-mode hook arg, or skips when there is none */
static Cmd *exec_training_state(Cmd *ds)
{
    PS2_FRAME(0x20);
    void *fn = ginfop->trainingFunc[cmd_s(ds, 2)];
    if (!fn) return ds + cmd_i(ds, 4);
    gcall_as<void(int)>(fn, (int)cmd_s(ds, 2));
    return ds + 1;
}

static Cmd *exec_weapon(Cmd *ds)
{
    PS2_FRAME(0x20);
    GS_FlushList();
    DF_DrawWeapon(ginfop, 0, s_ofs_y);
    GS_NewList();
    return ds + 1;
}

static Cmd *exec_hanger(Cmd *ds) { return cmd_skip(ds, ginfop->unit_hanger != -1); }
static Cmd *exec_intercept(Cmd *ds) { return cmd_skip(ds, ginfop->corei != -1); }

static Cmd *exec_addoffset(Cmd *ds)
{
    s_ofs_x = s_ofs_x + cmd_s(ds, 4);
    s_ofs_y = s_ofs_y + cmd_s(ds, 6);
    return ds + 1;
}

static Cmd *exec_offset(Cmd *ds)
{
    s_ofs_x = cmd_s(ds, 4);
    s_ofs_y = cmd_s(ds, 6);
    return ds + 1;
}

/* heading tape: arg = width | frame flag << 12 at (x, y); the N / E / S / W letters (one each 90 degrees, 84 px
 * per 45 degrees) with three ticks between them, clipped to [x, x + width) */
static Cmd *exec_compass(Cmd *ds)
{
    PS2_FRAME(0x110);
    static const float dir[4] = {0.0f, 90.0f, 180.0f, 270.0f};
    static const int letter[4][4] = {{48, 156, 12, 10}, {48, 146, 12, 10}, {156, 146, 12, 10}, {96, 156, 12, 10}};
    const float deg = ps2_u2f(0x42652ee1), rad = ps2_u2f(0x3c8efa35);   /* 180 / pi, pi / 180 */
    short arg = cmd_s(ds, 2);
    int x = cmd_s(ds, 4), y = cmd_s(ds, 6);
    int w = arg & 0xfff, frame = (arg & 0xf000) >> 12;
    int r = x + w;
    float head = deg * ginfop->angy;
    if (head < 0.0f) head = head + 360.0f;
    GS_FlushList();
    GS_NewList();
    if (frame) {
        GS_DrawLine(x, y, r, y);
        GS_DrawLine(r, y, r, y + 13);
        GS_DrawLine(r, y + 12, x, y + 12);
        GS_DrawLine(x, y + 12, x, y);
        GS_FlushList();
        GS_NewList();
    }
    head = rad * head;
    for (int i = 0; i < 4; i++) {
        float a = sub_angf(rad * dir[i], head);
        int cx = cmd_s(ds, 4) + w / 2 + ps2_ftoi((84.0f * (-deg * a)) / 45.0f);
        int lx = cx - 6;
        if (!(lx < r)) continue;
        int rx = lx + 12;
        if (!(rx < x)) {
            int u = letter[i][0];
            if (lx < x) {
                u = u + (x - lx);
                lx = x;
            }
            if (r < rx) rx = r;
            GS_DrawSprite(lx, y + 2, u, letter[i][1], rx - lx, letter[i][3]);
        }
        int tx = cx + 7;
        for (int j = 0; j < 4; j++, tx += 0x28) {
            int tr = j == 3 ? tx + 0x22 : tx + 0x28;
            if (!(tx < r)) break;
            if (tr < x) continue;
            int sx = tx, u = 0x82;
            if (tx < x) {
                sx = x;
                u = 0x82 + (x - tx);
            }
            if (r < tr) tr = r;
            GS_DrawSprite(sx, y + 2, u, 0xba, tr - sx, 8);
        }
    }
    GS_FlushList();
    GS_NewList();
    return ds + 1;
}

/* skips unless the extension slot (ablob 0..3) is arg */
static Cmd *exec_eob(Cmd *ds)
{
    int ok = 0;
    switch (ginfop->ablob) {
    case 3: ok = cmd_s(ds, 2) == 3; break;
    case 2: ok = cmd_s(ds, 2) == 2; break;
    case 0: ok = cmd_s(ds, 2) == 0; break;
    case 1: ok = cmd_s(ds, 2) == 1; break;
    }
    return cmd_skip(ds, ok);
}

static Cmd *exec_optskip_ene(Cmd *ds) { return cmd_skip(ds, (ginfop->flags & (int)cmd_s(ds, 2)) != 0); }
static Cmd *exec_optskip(Cmd *ds) { return cmd_skip(ds, (ginfop->flags & (1u << (cmd_s(ds, 2) & 0x1f))) != 0); }

/* area-over warning: blinks while leavin_frm > 0 */
static Cmd *exec_leavarea(Cmd *ds)
{
    PS2_FRAME(0x30);
    if (ginfop->leavin_frm <= 0) return ds + cmd_i(ds, 4) + 1;
    set_rgba_pulse((int)FRAME);
    return ds + 1;
}

/* system error warning +4 blinks while its frame counter runs, else skips +6 commands */
static Cmd *exec_syserr(Cmd *ds)
{
    PS2_FRAME(0x30);
    if (ginfop->syserr_frm[cmd_s(ds, 4)] <= 0) return ds + cmd_s(ds, 6) + 1;
    set_rgba_pulse((int)FRAME);
    return ds + 1;
}

/* AC status warning +4, pulsing with its own frame counter */
static Cmd *exec_acstat(Cmd *ds)
{
    PS2_FRAME(0x30);
    short frm = ginfop->acstat_frm[cmd_s(ds, 4)];
    if (frm <= 0) return ds + cmd_s(ds, 6) + 1;
    set_rgba_pulse(frm);
    return ds + 1;
}

/* heat gauge (x, y, w, h): background, fill = heat / limit (blue while cooling, the top 20% red) */
static Cmd *exec_myheat(Cmd *ds)
{
    PS2_FRAME(0x50);
    if (ginfop->acid >= 0) {
        int w = cmd_s(ds, 0xc), h = cmd_s(ds, 0xe);
        float rate = ps2_div((float)ginfop->myheat, (float)ginfop->core_tmprt_limit);
        if (!(rate <= 1.0f)) rate = 1.0f;
        GS_SetRGBA(STA2_RGBA[18]);
        GS_DrawSpriteS(cmd_s(ds, 4), cmd_s(ds, 6), w, h);
        if (ginfop->is_cooling == 1) {
            GS_SetRGBA(STA2_RGBA[13]);
            GS_DrawSpriteOS(cmd_s(ds, 4), cmd_s(ds, 6), w, h, rate, 1.0f);
        } else {
            GS_SetRGBA(ginfop->mycol);
            GS_DrawSpriteOS(cmd_s(ds, 4), cmd_s(ds, 6), w, h, rate, 1.0f);
            if (!(rate <= 0.8f)) {
                GS_SetRGBA(STA2_RGBA[13]);
                float fw = (float)w;
                int x = ps2_ftoi((float)cmd_s(ds, 4) + 0.8f * fw);
                float sx = (rate - 0.8f) / 0.2f;
                GS_DrawSpriteOS(x, cmd_s(ds, 6), ps2_ftoi(0.2f * fw), h, sx, 1.0f);
            }
        }
    }
    GS_SetRGBA(ginfop->mycol);
    return ds + 2;
}

/* energy gauge, filled upwards: boost level bstrate (when above redrate) and the red zone redrate */
static Cmd *exec_myenergy(Cmd *ds)
{
    PS2_FRAME(0x40);
    GS_SetTEST_1(1, 1, 0, 0, 0, 0, 1, 1);
    if (ginfop->acid >= 0) {
        int h = cmd_s(ds, 0xe), w = cmd_s(ds, 0xc);
        if (!(ginfop->bstrate <= ginfop->redrate)) {
            GS_SetTEXFLUSH();
            GS_SetRGBA(ginfop->mycol);
            GS_DrawSpriteOS(cmd_s(ds, 4), cmd_s(ds, 6) + 1, w, -h, 1.0f, ginfop->bstrate);
        }
        GS_SetTEXFLUSH();
        GS_SetRGBA(STA2_RGBA[13]);
        GS_DrawSpriteOS(cmd_s(ds, 4), cmd_s(ds, 6) + 1, w, -h, 1.0f, ginfop->redrate);
    }
    return ds + 2;
}

/* heat number */
static Cmd *exec_mytemp(Cmd *ds)
{
    PS2_FRAME(0x30);
    char buf[16];
    if (ginfop->acid < 0) return ds + 1;
    if (ginfop->is_cooling == 1) ACSTAT_SetDrawColor_AlphaDiv(1, STA2_RGBA[13], 2);
    else ACSTAT_SetDrawColor_AlphaDiv(1, ginfop->mycol, 2);
    ps2_sprintf_d(buf, "%04d", ginfop->myheat);
    DF_ACSTAT_DrawNumberOffset(cmd_s(ds, 4), cmd_s(ds, 6), buf, 4, 9);
    ACSTAT_SetDrawColor_AlphaDiv(0, 0, 1);
    if (ginfop->is_cooling == 1) GS_SetRGBA(STA2_RGBA[13]);
    else GS_SetRGBA(ginfop->mycol);
    return ds + 1;
}

/* ECM noise number */
static Cmd *exec_outnoise(Cmd *ds)
{
    PS2_FRAME(0x30);
    char buf[16];
    ACSTAT_SetDrawColor_AlphaDiv(1, ginfop->mycol, 2);
    ps2_sprintf_d(buf, "%04d", ginfop->ecmnoise);
    DF_ACSTAT_DrawNumber(cmd_s(ds, 4), cmd_s(ds, 6), buf, 3);
    ACSTAT_SetDrawColor_AlphaDiv(0, 0, 1);
    return ds + 1;
}

/* match timer seconds / minutes (timed sorties only); minutes also draws the ':' */
static Cmd *exec_mtimes(Cmd *ds)
{
    PS2_FRAME(0x30);
    char buf[16];
    if (g_sortie.pconstant->modeSortie) {
        ps2_sprintf_d(buf, "%02d", ginfop->times);
        DF_ACSTAT_DrawNumber(cmd_s(ds, 4), cmd_s(ds, 6), buf, 1);
    }
    return ds + 1;
}

static Cmd *exec_mtimem(Cmd *ds)
{
    PS2_FRAME(0x30);
    char buf[16];
    if (g_sortie.pconstant->modeSortie) {
        ps2_sprintf_d(buf, "%02d", ginfop->timem);
        DF_ACSTAT_DrawNumber(cmd_s(ds, 4), cmd_s(ds, 6), buf, 1);
        GS_DrawSpriteD(0xfc, 0x22, 0x104, 0x36, 240.0f, 0.0f, 248.0f, 20.0f, 0);
    }
    return ds + 1;
}

/* armour points (STA2_RGBA[2] while stat bit 3 is set) */
static Cmd *exec_ap(Cmd *ds)
{
    PS2_FRAME(0x30);
    char buf[16];
    if (ginfop->acid < 0) return ds + 1;
    ps2_sprintf_d(buf, "%04d", ginfop->myap);
    if (ginfop->stat & 8) GS_SetRGBA(STA2_RGBA[2]);
    else GS_SetRGBA(ginfop->mycol);
    DF_ACSTAT_DrawNumber(cmd_s(ds, 4), cmd_s(ds, 6), buf, 0);
    GS_SetRGBA(ginfop->mycol);
    return ds + 1;
}

/* blend mode: 0 normal, 1 additive-ish, 2 subtract-ish (GS ALPHA_1 a, b, c, d) */
static Cmd *exec_alpha(Cmd *ds)
{
    PS2_FRAME(0x20);
    int mode = cmd_i(ds, 4);
    if (mode == 2) GS_SetALPHA_1(2, 0, 0, 1, 0);
    else if (mode == 1) GS_SetALPHA_1(0, 2, 0, 1, 0);
    else if (mode == 0) GS_SetALPHA_1(0, 1, 0, 1, 0);
    return ds + 1;
}

static void set_tex0(int i)
{
    struct_f386fb29 t;
    memcpy(&t, &STA2_TEXTURE[i], 8);
    GS_SetTEX0_1(t);
}

static Cmd *exec_texid(Cmd *ds)
{
    PS2_FRAME(0x20);
    if (cmd_i(ds, 4) < 0x10) set_tex0(cmd_i(ds, 4));
    return ds + 1;
}

static Cmd *exec_flush(Cmd *ds)
{
    PS2_FRAME(0x20);
    GS_FlushList();
    GS_NewList();
    return ds + 1;
}

/* colour: the AC / gauge colours (100..105) or STA2_RGBA[arg] */
static Cmd *exec_syscol(Cmd *ds)
{
    PS2_FRAME(0x30);
    switch (cmd_s(ds, 2)) {
    case 100: GS_SetRGBA(ginfop->mycol); break;
    case 101: GS_SetRGBA(ginfop->excol); break;
    case 102: GS_SetRGBA(ginfop->obcol); break;
    case 103: GS_SetRGBA(ginfop->coreicol); break;
    case 104: GS_SetRGBA(ginfop->unit_hanger_col); break;
    case 105: {
        int col[4] A16;
        memcpy(col, ginfop->mycol, 16);
        col[3] = 0x40;
        GS_SetRGBA(col);
        break;
    }
    default: GS_SetRGBA(STA2_RGBA[cmd_s(ds, 2)]); break;
    }
    return ds + 1;
}

static Cmd *exec_rgba(Cmd *ds)
{
    PS2_FRAME(0x20);
    const unsigned char *b = (const unsigned char *)ds;
    s_rgba[0] = b[4];
    s_rgba[1] = b[5];
    s_rgba[2] = b[6];
    s_rgba[3] = b[7];
    GS_SetRGBA(s_rgba);
    return ds + 1;
}

/* sprite (x, y) + offset, size (w, h), texels (u, v); arg = GS_DrawSpriteD flags */
static Cmd *exec_spr(Cmd *ds)
{
    PS2_FRAME(0x20);
    int u = cmd_s(ds, 8), v = cmd_s(ds, 0xa), w = cmd_s(ds, 0xc), h = cmd_s(ds, 0xe);
    int ltx = cmd_s(ds, 4) + s_ofs_x, lty = cmd_s(ds, 6) + s_ofs_y;
    GS_DrawSpriteD(ltx, lty, ltx + w, lty + h, (float)u, (float)v, (float)(u + w), (float)(v + h), cmd_s(ds, 2));
    return ds + 2;
}

/* split screen: the number (1-4) of AC arg in free-for-all (or its team label RL / RT / BL / BT: leader / team, red /
 * blue) and its AP / 4, at (x, y) + offset; nothing for the own AC or an AC beyond NowPlayerNum */
static Cmd *exec_ac_ap_split(Cmd *ds)
{
    PS2_FRAME(0x50);
    static char *const code_str[4] = {(char *)"RL", (char *)"RT", (char *)"BL", (char *)"BT"};   /* 0x260a70 */
    char str[16];
    int no = cmd_s(ds, 2);
    if (!(no < NowPlayerNum)) return ds + 1;
    if (ginfop->acid == no) return ds + 1;
    GS_FlushList();
    GS_NewList();
    switch (MyI.vsform) {
    case 0:
        Pf_SPrintf(str, (char *)"%d", no + 1);
        DF_ACSTAT_DrawNumberOffset(cmd_s(ds, 4) + s_ofs_x, cmd_s(ds, 6) + s_ofs_y, str, 4, 9);
        DF_ACSTAT_DrawSprite(cmd_s(ds, 4) + s_ofs_x + 9, cmd_s(ds, 6) + s_ofs_y, 20.0f, 194.0f, 10, 8);
        break;
    case 1: {
        char *label;
        if (no == MyI.leader[0]) label = code_str[0];
        else if (no == MyI.leader[1]) label = code_str[2];
        else if (MyAC[no].cflag & 1) label = code_str[1];
        else label = code_str[3];
        Pf_SPrintf(str, (char *)"%s", label);
        StatFont_DrawString(cmd_s(ds, 4) + s_ofs_x, cmd_s(ds, 6) + s_ofs_y, 0x12, 8, str, 0x11);
        break;
    }
    case 2:
        Pf_SPrintf(str, (char *)"%s", (MyAC[no].cflag & 1) ? code_str[1] : code_str[3]);
        StatFont_DrawString(cmd_s(ds, 4) + s_ofs_x, cmd_s(ds, 6) + s_ofs_y, 0x12, 8, str, 0x11);
        break;
    }
    Pf_SPrintf(str, (char *)"%04d", (g_ac[no].hp + 3) / 4);
    DF_ACSTAT_DrawNumberOffset(cmd_s(ds, 4) + s_ofs_x + 0x16, cmd_s(ds, 6) + s_ofs_y, str, 4, 9);
    return ds + 1;
}

/* skips unless AC arg exists and is not the own AC */
static Cmd *exec_if_otherac(Cmd *ds)
{
    int a = cmd_s(ds, 2);
    if (!(a < NowPlayerNum)) return ds + cmd_i(ds, 4) + 1;
    return cmd_skip(ds, ginfop->acid != a);
}

static Cmd *exec_if_radar(Cmd *ds) { return cmd_skip(ds, ginfop->is_radar_func == 1); }

/* Versus time limit: seconds / minutes of the remaining frames (rounded up to whole seconds), 0 once negative */
static Cmd *exec_vstimes(Cmd *ds)
{
    PS2_FRAME(0x30);
    char buf[16];
    int sec = AC_VS_RESTTIME >= 0 ? ((AC_VS_RESTTIME + 59) / 60) % 60 : 0;
    ps2_sprintf_d(buf, "%02d", sec);
    DF_ACSTAT_DrawNumber(cmd_s(ds, 4), cmd_s(ds, 6), buf, 1);
    return ds + 1;
}

static Cmd *exec_vstimem(Cmd *ds)
{
    PS2_FRAME(0x30);
    char buf[16];
    int min = AC_VS_RESTTIME >= 0 ? ((AC_VS_RESTTIME + 59) / 60) / 60 : 0;
    ps2_sprintf_d(buf, "%02d", min);
    DF_ACSTAT_DrawNumber(cmd_s(ds, 4), cmd_s(ds, 6), buf, 1);
    return ds + 1;
}

static Cmd *exec_maker(Cmd *ds) { return cmd_skip(ds, ginfop->maker == cmd_s(ds, 2)); }

/* selects the gauge info of AC arg for the following commands */
static Cmd *exec_xginfo(Cmd *ds)
{
    ginfop = &ginfo[cmd_s(ds, 2)];
    return ds + 1;
}

/* extension (back unit) ammo, number table arg - 1; ';' when there is none. Adds s_ofs_y only, not s_ofs_x */
static Cmd *exec_eoblt(Cmd *ds)
{
    PS2_FRAME(0x50);
    char buf[16];
    int uly = cmd_s(ds, 6) + s_ofs_y;
    int nfont = cmd_s(ds, 2) - 1;
    ACSTAT_SetDrawColor_AlphaDiv(1, ginfop->obcol, 2);
    if (ginfop->eoblt >= 0) {
        ps2_sprintf_d(buf, "%03d", ginfop->eoblt);
        DF_ACSTAT_DrawNumberOffset(cmd_s(ds, 4), uly, buf, nfont, 9);
    } else {
        DF_ACSTAT_DrawNumberOffset(cmd_s(ds, 4), uly, (char *)";", nfont, 9);
    }
    ACSTAT_SetDrawColor_AlphaDiv(0, 0, 1);
    return ds + 1;
}

/* inverse of exec_optskip: skips when option bit arg is set */
static Cmd *exec_none_optskip(Cmd *ds)
{
    return cmd_skip(ds, (ginfop->flags & (1u << (cmd_s(ds, 2) & 0x1f))) == 0);
}

/* remaining-AP meter: background bar, then the fill rem_rate wide */
static Cmd *exec_remnants_meter(Cmd *ds)
{
    PS2_FRAME(0x20);
    GS_SetALPHA_1(0, 1, 0, 1, 0);
    GS_SetTEST_1(1, 1, 0, 0, 0, 0, 1, 1);
    GS_SetRGBA(STA2_RGBA[18]);
    GS_DrawSpriteS(cmd_s(ds, 4), cmd_s(ds, 6), 0x3c, 5);
    GS_SetRGBA(ginfop->mycol);
    GS_DrawSpriteOS(cmd_s(ds, 4), cmd_s(ds, 6), 0x3c, 5, ginfop->rem_rate, 1.0f);
    return ds + 1;
}

/* speed meter: the alpha test reference between x and y (the bound pixel values) at 1 - speed / 10000 */
static Cmd *exec_spdmeter(Cmd *ds)
{
    PS2_FRAME(0x20);
    float mn = (float)cmd_s(ds, 4);
    float mx = (float)cmd_s(ds, 6);
    float r = 1.0f - ginfop->myspd / 10000.0f;
    float v = mn + r * (mx - mn);
    if (!(v <= mx)) v = mx;
    if (v < mn) v = mn;
    GS_SetTEST_1(1, 6, ps2_ftoi(0.5f + v), 0, 0, 0, 1, 1);
    return ds + 1;
}

/* speed number (speed / 10) */
static Cmd *exec_myspeed(Cmd *ds)
{
    PS2_FRAME(0x30);
    char buf[16];
    ACSTAT_SetDrawColor_AlphaDiv(1, ginfop->mycol, 2);
    ps2_sprintf_d(buf, "%03d", ps2_ftoi(ginfop->myspd) / 10);
    DF_ACSTAT_DrawNumber(cmd_s(ds, 4), cmd_s(ds, 6), buf, 2);
    ACSTAT_SetDrawColor_AlphaDiv(0, 0, 1);
    return ds + 1;
}

/* ECM noise number, split screen (offset font) */
static Cmd *exec_outnoise_split(Cmd *ds)
{
    PS2_FRAME(0x30);
    char buf[16];
    ACSTAT_SetDrawColor_AlphaDiv(1, ginfop->mycol, 2);
    ps2_sprintf_d(buf, "%04d", ginfop->ecmnoise);
    DF_ACSTAT_DrawNumberOffset(cmd_s(ds, 4), cmd_s(ds, 6), buf, 4, 9);
    ACSTAT_SetDrawColor_AlphaDiv(0, 0, 1);
    return ds + 1;
}

/* outside temperature; a '-' among the first three characters is drawn as the ';' glyph */
static Cmd *exec_outtemp(Cmd *ds)
{
    PS2_FRAME(0x30);
    char buf[16];
    ACSTAT_SetDrawColor_AlphaDiv(1, ginfop->mycol, 2);
    ps2_sprintf_d(buf, "%03d", ginfop->outtemp);
    for (int i = 0; i < 3; i++)
        if (buf[i] == '-') buf[i] = ';';
    DF_ACSTAT_DrawNumberOffset(cmd_s(ds, 4), cmd_s(ds, 6), buf, 4, 9);
    ACSTAT_SetDrawColor_AlphaDiv(0, 0, 1);
    return ds + 1;
}

void draw_cmdlist(Cmd *ds)
{
    PS2_FRAME(0x30);
    s_ofs_y = 0;
    s_ofs_x = 0;
    GS_NewList();
    GS_SetTEXFLUSH();
    GS_SetTEST_1(0, 0, 0, 0, 0, 0, 1, 1);
    set_tex0(1);
    GS_SetCLAMP_1(1, 1, 0, 0, 0, 0);
    GS_SetALPHA_1(0, 1, 0, 1, 0);
    for (unsigned char op = *(unsigned char *)ds; op < 0x35 && cmd_exec[op]; op = *(unsigned char *)ds) {
        if (GS_GetRest() < 0x20) {
            GS_FlushList();
            GS_NewList();
        }
        ds = gcall(cmd_exec[op], ds);
    }
    GS_FlushList();
}

#ifdef ACNB_SOURCEPORT
/* the original's initialized tables (generated by scripts/sp_datagen.py from the PS2 image) */
unsigned long long *(*cmd_exec[54])(unsigned long long *) = {   /* main 0x260940 */
    exec_rgba, exec_syscol, exec_spr, exec_flush, exec_texid, exec_alpha, exec_test, exec_ap, exec_mtimem,
    exec_mtimes, exec_outtemp, exec_outnoise, exec_outnoise_split, exec_mytemp, exec_myspeed, exec_myenergy,
    exec_myheat, exec_tgenergy, exec_tgheat, exec_ap2p, exec_ap3p, exec_ap4p, exec_spdmeter, exec_remnants,
    exec_acstat, exec_syserr, exec_leavarea, exec_optskip, exec_none_optskip, exec_optskip_ene, exec_exblt,
    exec_eoblt, exec_xginfo, exec_maker, exec_vstimem, exec_vstimes, exec_eob, exec_outtemp_test, exec_compass,
    exec_remnants_meter, exec_offset, exec_addoffset, exec_tgwepnam, exec_tgequip, exec_friend, exec_intercept,
    exec_hanger, exec_weapon, exec_weapon_split, exec_if_radar, exec_if_otherac, exec_ac_ap_split,
    exec_training_state, 0
};
#endif
