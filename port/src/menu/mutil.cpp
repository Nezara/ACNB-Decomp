/* src/menu/mutil.cpp — small menu helpers (OLMENU): total training + arena points, adding a part to a
 * player's garage, the "move manager" that turns a distance into per-frame integer steps for the garage
 * fades (linear / ease / accel-decel), colour multiply, a debug text blit from the menu font texture and
 * the scene-name string factory. */
#include "acnb/nodwarf.h"
#include "acnb/ps2fp.h"

int MUtil_GetTotalPoint(void)
{
    int total = 0;

    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 5; j++)
            total += g_gamedata[0].trainingRecord[i][j].point;
    return total + g_gamedata[0].arenaBattle.pointArena;
}

/* Kind 10 parts are also registered as kind 11. */
void MUtil_AddParts(int player, int parts_kind, int parts_id)
{
    PS2_FRAME(0x40);
    int acpeq = -1;

    if (parts_kind == 10) acpeq = 11;
    if (parts_kind != -1 && parts_id != 0) PlayerInfo_SetExistPartsGarage(player, parts_kind, parts_id, 1);
    if (acpeq != -1 && parts_id != 0) PlayerInfo_SetExistPartsGarage(player, acpeq, parts_id, 1);
}

/* Integer step of this frame for moving dx over `time` frames: mode 2 linear, 1 ease in/out (cubic),
 * 0 accelerate then decelerate. Returns -99999 once count reached time. An unknown mode returns whatever
 * the original's v0 held on entry. */
int MoveMng_GetNextMoveValue(struct struct_d46b729f *pMoveMng, int dx, int time, char mode)
{
    float fdx = (float)dx;
    float ft = (float)time;
    float ft2 = ft * ft;
    float ft3 = ft * ft2;
    int count = pMoveMng->count;
    float fc = 0.5f + (float)count;
    int ret;

    if (mode == 2) {
        float v = pMoveMng->vMove + ps2_div(fdx, ft);
        pMoveMng->vMove = v;
        ret = ps2_ftoi(v - pMoveMng->vMovePast);
        pMoveMng->vMovePast = pMoveMng->vMovePast + (float)ret;
    } else if (mode == 1) {
        if (count == time - 1) {
            ret = dx - ps2_ftoi(pMoveMng->vMove);
        } else {
            float k = dx >= 0 ? 2.25f : -2.25f;
            float d6 = 6.0f * fdx;
            float a = -(d6 + 4.0f * k * ft3);
            float b = d6 + k * ft3;
            float t1 = ps2_div(a, 3.0f * ft2);
            float t2 = ps2_div(b, 3.0f * ft);
            float acc = t2 + t1 * fc;             /* adda.s */
            float r = acc + fc * (k * fc);        /* madd.s: product rounded, then added */
            float v = pMoveMng->vMove + r;
            pMoveMng->vMove = v;
            ret = ps2_ftoi(v - pMoveMng->vMovePast);
            pMoveMng->vMovePast = pMoveMng->vMovePast + (float)ret;
        }
    } else if (mode == 0) {
        int half = time / 2;
        int hdx = dx / 2;
        if (count < half) {
            float th = ps2_div((float)time, 2.0f);
            float sq = th * th;
            float f = ps2_div(2.0f * ((float)hdx - 0.0f * th), sq);
            ret = ps2_ftoi(0.0f + f * (float)count);
        } else {
            float th = ps2_div((float)time, 2.0f);
            float sq = th * th;
            float fh = (float)hdx;
            float v0 = ps2_div(2.0f * fh * th, sq);
            float f = ps2_div(2.0f * (fh - v0 * th), sq);
            ret = ps2_ftoi(v0 + f * (float)(count - half));
        }
    } else {
        ret = (int)PS2_STALE_GPR(2 /* v0 */);
    }
    if (count < time) {
        pMoveMng->count++;
        return ret;
    }
    return -99999;
}

void MoveMng_Init(struct struct_d46b729f *pMoveMng)
{
    memset(pMoveMng, 0, 0x10);
}

/* Per channel (a * b) >> 7, truncated to a byte (no clamp). */
void MenuUtil_MulColor(struct struct_76b810ec *color, struct struct_76b810ec *rhs, struct struct_76b810ec *lhs)
{
    color->r = (unsigned char)((rhs->r * lhs->r) >> 7);
    color->g = (unsigned char)((rhs->g * lhs->g) >> 7);
    color->b = (unsigned char)((rhs->b * lhs->b) >> 7);
    color->a = (unsigned char)((rhs->a * lhs->a) >> 7);
}

/* Debug: one menu string per line, 16 pixels apart. */
void MenuUtil_RenderTexStringAll(int *strid, int numid)
{
    PS2_FRAME(0x50);
    for (int i = 0, y = 0; i < numid; i++, y += 16)
        MenuUtil_RenderTexString(0, y, DF_MSTR_GetName(MenuMng.language, 0, strid[i]));
}

/* Blits A-Z (lower case folded) from the 20x14 glyph grid of menu texture 0x3f0 at (x, y); space and
 * 'I' advance 8 pixels, other letters 20, other characters are skipped. */
int MenuUtil_RenderTexString(int x, int y, unsigned char *pstr)
{
    PS2_FRAME(0xb0);
    PS2_STACK(struct stMDraw_BitBlt, blt, 0x60);
    PS2_STACK(struct struct_13efd7f4, tex, 0xa0);
    unsigned long long tex0;

    if (!pstr) return 1;
    if (!MenuTex_StandbyTexture(0x3f0, &tex)) return 0;
    memcpy(&tex0, &tex, 8);
    blt.dbp = 0x3c40;
    blt.dir = 0;
    blt.dbw = 8;
    blt.sbp = (int)(tex0 & 0x3fff);
    blt.sbw = (int)((tex0 >> 14) & 0x3f);
    blt.spsm = (int)((tex0 >> 20) & 0x3f);
    blt.dpsm = blt.spsm;
    blt.dsax = x;
    blt.dsay = y;
    blt.rrh = 14;
    blt.rrw = 20;
    for (; *pstr; pstr++) {
        unsigned char c = *pstr;
        if (c >= 'a' && c <= 'z') c -= 0x20;
        if (c >= 'A' && c <= 'Z') {
            blt.ssax = ((c - 'A') % 12) * 20;
            blt.ssay = ((c - 'A') / 12) * 14 + 0x30;
            MDraw_BitBlt(&blt);
            blt.dsax += c == 'I' ? 8 : 0x14;
        } else if (c == ' ') {
            blt.dsax += 8;
        }
    }
    return 1;
}

/* Shift-JIS lead byte (0x81-0xa0, 0xe0-0xff). */
int MenuUtil_IsLeadByte(unsigned char *inCp)
{
    unsigned char c = *inCp;

    if (c == 0) return 0;
    if ((c >= 0x80 && c <= 0xa0) || c >= 0xe0) return 1;
    return 0;
}

#ifdef ACNB_SOURCEPORT
/* original initializer (main ELF sdata 0x27c2a8; was a zero-filled stand-in: the scene names drew with alpha 0) */
struct_76b810ec mutil_scene_name_col = {0x93, 0x79, 0x6b, 0x4d};
#endif

struct stMobj *MenuUtil_NewSceneNameString(struct stMobj *pParent, int strid)
{
    PS2_FRAME(0x30);
    PS2_STACK(struct struct_76b810ec, col, 0x2c);

    col = mutil_scene_name_col;
    return DF_MOBJ_NewString(pParent, 0, strid, 0, 5, 12, 14, &col, 0, 0x800);
}
