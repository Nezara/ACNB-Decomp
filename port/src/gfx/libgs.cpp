/* src/gfx/libgs.c - "GSL", the immediate-mode GS list API used by the HUD (stat/), the scr_eff presets, blur,
 * pause / fade and the menu flip copy. GS_NewList points the cursor s_ppcktCur at one of the two scratchpad buffers
 * PrimitiveBuffer[PBuffID]; the GS_Set* functions append A+D register writes, the GS_Draw* functions append PACKED
 * GIF packets (sprites, triangle strips); GS_FlushList closes the list with an END tag, kicks it on GIF path 3 and
 * toggles PBuffID. Sprite helpers take screen pixels: GSX_SetWindowSize stores the pixel shifts, coordinates become
 * ofs + (0x8000 - frame_size) + (v << shift) in 12.4 window space. No function checks for overflow (callers use
 * GS_GetRest). Every function keeps a local cursor and stores it back before returning (interpreted libgs functions
 * keep appending through the global).
 * GS_BitBlt / GS_LoadImage append local->local / host->local transfers to the list; DF_StoreImage / DF_LoadImageEx are
 * immediate SDK transfers (sceGsExecStoreImage / sceGsExecLoadImage) bracketed by GIF-path idle waits.
 * Interpreted: GS_DrawLine, GS_SetZBUF_1. GS_SetTEXA / GS_SetBlendMode / GS_UVv /
 * GS_TexCoord3f / GS_NoKick (cockpit, sprmng, draw_tp2 primitive lists) are never reached in Versus. */
#include <cstring>
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/gif.h"
#include "acnb/sdk.h"
#include "acnb/local/src/gfx/libgs.h"

#ifdef ACNB_SOURCEPORT
struct struct_e95276e9 s_gsi = {0, 0, 4, 4, -24, -16, -24, -16, 0, 0, 1, 1, 0, 0};   /* main 0x25bf90 */
unsigned int *s_ppcktTop;
unsigned int *s_ppcktCur;
#endif

/* one 64-bit sd into the packet */
static inline unsigned int *gs_put64(unsigned int *p, unsigned long long v)
{
    p[0] = (unsigned int)v;
    p[1] = (unsigned int)(v >> 32);
    return p + 2;
}

/* (long long)v << sh: the original's sign-extended register images (dsll32 / dsra32 / dsll), no masking */
static inline unsigned long long gs_sx(int v, int sh)
{
    return (unsigned long long)(long long)v << sh;
}

/* sllv: shift count = low 5 bits */
static inline unsigned int gs_shl(int v, int s)
{
    return (unsigned int)v << (s & 31);
}

/* XYZF2 qword {x, y, 0x00ffffff, 0x10} (F word 0x10, not put_xyzf2's 0) */
static inline unsigned int *gs_xyzf(unsigned int *p, unsigned int x, unsigned int y)
{
    p[0] = x;
    p[1] = y;
    p[2] = 0x00ffffff;
    p[3] = 0x10;
    return p + 4;
}

/* 16-byte lq / sq copy (lq ignores the low 4 address bits) */
static inline unsigned int *gs_put_quad(unsigned int *p, const void *src)
{
    const unsigned int *s = (const unsigned int *)PS2_LQ_SRC(src);
    p[0] = s[0];
    p[1] = s[1];
    p[2] = s[2];
    p[3] = s[3];
    return p + 4;
}

/* A+D tag with PRE = 1, EOP = 0: {1, 0x10004000, 0xe, 0} */
static inline unsigned int *gs_tag_ad_pre(unsigned int *p)
{
    return gif_tag(p, 1, 0, 1, 0, 0, 1, GIF_R_AD);
}

void GS_DrawRectStripStN(int (*xyzSrc)[4], float *stSrc, int *rgb, int isTex, int isAlpha)
{
    PS2_FRAME(0x20);
    PS2_STACK(int[4], q0, 0);
    PS2_STACK(int[4], q1, 0x10);
    int *xyz = &xyzSrc[0][0];
    q0[0] = xyz[0];
    q0[1] = xyz[1];
    q0[2] = xyz[2];
    q0[3] = xyz[3];
    q1[0] = q0[0];
    q1[1] = xyz[5];
    q1[2] = xyz[6];
    q1[3] = xyz[7];
    int n = (int)((unsigned int)xyz[4] - (unsigned int)xyz[0]) / 1024;
    unsigned int prim = (isTex ? 0x10u : 0u) | 4 | (isAlpha ? 0x40u : 0u);
    float s0 = stSrc[0];
    float t0 = stSrc[1];
    float t1 = stSrc[3];
    float ds = ps2_div(stSrc[2] - s0, (float)n);
    int cnt = n + 1;
    unsigned int *p = s_ppcktCur;
    p = gif_tag(p, (unsigned int)cnt * 2, 0, 1, prim & 0x7ff, 0, 3, 0x412);
    float sa = s0, sb = s0;
    for (int i = 0; i < cnt; i++) {
        p[0] = ps2_f2u(sa);
        p[1] = ps2_f2u(t0);
        p[2] = 0x3f800000;
        p[3] = 0;
        p += 4;
        sa = sa + ds;
        p = gs_put_quad(p, rgb);
        p = gs_put_quad(p, q0);
        p[0] = ps2_f2u(sb);
        p[1] = ps2_f2u(t1);
        p[2] = 0x3f800000;
        p[3] = 0;
        p += 4;
        sb = sb + ds;
        p = gs_put_quad(p, rgb);
        p = gs_put_quad(p, q1);
        q0[0] += 0x400;
        q1[0] += 0x400;
    }
    s_ppcktCur = p;
}

void GS_DrawRectStripUvN(int (*xyzSrc)[4], int *uvSrc, int *rgb, int isTex, int isAlpha)
{
    PS2_FRAME(0x20);
    PS2_STACK(int[4], q0, 0);
    PS2_STACK(int[4], q1, 0x10);
    int *xyz = &xyzSrc[0][0];
    unsigned int prim = (isTex ? 0x110u : 0u) | 4 | (isAlpha ? 0x40u : 0u);
    q0[0] = xyz[0];
    q0[1] = xyz[1];
    q0[2] = xyz[2];
    q0[3] = xyz[3];
    q1[0] = q0[0];
    q1[1] = xyz[5];
    q1[2] = xyz[6];
    q1[3] = xyz[7];
    int n = (int)((unsigned int)xyz[4] - (unsigned int)xyz[0]) / 1024;
    if (n == 0)
        return;
    unsigned int ua = (unsigned int)uvSrc[0], ub = ua;
    unsigned int v0 = (unsigned int)uvSrc[1];
    unsigned int v1 = (unsigned int)uvSrc[3];
    unsigned int du = (unsigned int)((int)((unsigned int)uvSrc[2] - ua) / n);
    int cnt = n + 1;
    unsigned int *p = s_ppcktCur;
    p = gif_tag(p, (unsigned int)cnt * 2, 0, 1, prim & 0x7ff, 0, 3, 0x413);
    for (int i = 0; i < cnt; i++) {
        p = put_uv(p, ua, v0);
        ua += du;
        p = gs_put_quad(p, rgb);
        p = gs_put_quad(p, q0);
        p = put_uv(p, ub, v1);
        ub += du;
        p = gs_put_quad(p, rgb);
        p = gs_put_quad(p, q1);
        q0[0] += 0x400;
        q1[0] += 0x400;
    }
    s_ppcktCur = p;
}

/* one textured / alpha SPRITE (2 vertices) with explicit UV and colour: tag {2, prim, PRE, NREG 3 = UV RGBAQ XYZF2},
 * then per vertex UV {u, v, 0, 0}, rgb (lq), xyz (lq, word 3 as the caller wrote it) */
void GS_DrawRectUvN(int (*xyzSrc)[4], int *uvSrc, int *rgb, int isTex, int isAlpha)
{
    unsigned int prim = (isTex ? 0x110u : 0u) | 6 | (isAlpha ? 0x40u : 0u);
    unsigned int *p = s_ppcktCur;
    p[0] = 2;
    p[1] = ((prim & 0x7ff) << 15) | 0x30004000u;
    p[2] = 0x413;
    p[3] = 0;
    p += 4;
    for (int i = 0; i < 2; i++) {
        p[0] = (unsigned int)uvSrc[2 * i];
        p[1] = (unsigned int)uvSrc[2 * i + 1];
        p[2] = 0;
        p[3] = 0;
        p += 4;
        p = gs_put_quad(p, rgb);
        p = gs_put_quad(p, xyzSrc[i]);
    }
    s_ppcktCur = p;
}

void GS_DrawSpriteDirect(int ltx, int lty, int rbx, int rby, int ltu, int ltv, int rbu, int rbv)
{
    unsigned int *p = s_ppcktCur;
    p = gif_tag(p, 2, 1, 1, 0x156, 0, 2, 0x43);
    p[0] = (unsigned int)ltu;   /* UV: words 2, 3 not written */
    p[1] = (unsigned int)ltv;
    p += 4;
    p = gs_xyzf(p, 0x8000u - (unsigned int)s_gsi.frame_wd + (unsigned int)ltx,
                0x8000u - (unsigned int)s_gsi.frame_ht + (unsigned int)lty);
    p[0] = (unsigned int)rbu;
    p[1] = (unsigned int)rbv;
    p += 4;
    p = gs_xyzf(p, 0x8000u - (unsigned int)s_gsi.frame_wd + (unsigned int)rbx,
                0x8000u - (unsigned int)s_gsi.frame_ht + (unsigned int)rby);
    s_ppcktCur = p;
}

void DF_ACSTAT_DrawSprite(int x, int y, float u, float v, int w, int h)
{
    int sx = s_gsi.shift_x;
    int sy = s_gsi.shift_y;
    float uw = u + (float)w;
    float vh = v + (float)h;
    GS_DrawSpriteDP((int)gs_shl(x, sx), (int)gs_shl(y, sy), (int)gs_shl((int)((unsigned int)x + (unsigned int)w), sx),
                    (int)gs_shl((int)((unsigned int)y + (unsigned int)h), sy), ps2_ftoi(u * 16.0f),
                    ps2_ftoi(v * 16.0f), ps2_ftoi(uw * 16.0f), ps2_ftoi(vh * 16.0f), 0);
}

void GS_DrawSpriteOS(int x, int y, int w, int h, float sx, float sy)
{
    int shx = s_gsi.shift_x;
    int shy = s_gsi.shift_y;
    unsigned int X = gs_shl(x, shx);
    unsigned int Y = gs_shl(y, shy);
    unsigned int W = (unsigned int)ps2_ftoi((float)(int)gs_shl(w, shx) * sx);
    unsigned int H = (unsigned int)ps2_ftoi((float)(int)gs_shl(h, shy) * sy);
    unsigned int *p = s_ppcktCur;
    p = gif_tag(p, 2, 1, 1, 0x6, 0, 1, GIF_R_XYZF2);
    p = gs_xyzf(p, (unsigned int)s_gsi.ofs_ltx + (0x8000u - (unsigned int)s_gsi.frame_wd + X),
                (unsigned int)s_gsi.ofs_lty + (0x8000u - (unsigned int)s_gsi.frame_ht + Y));
    p = gs_xyzf(p, (unsigned int)s_gsi.ofs_rbx + (0x8000u - (unsigned int)s_gsi.frame_wd + (X + W)),
                (unsigned int)s_gsi.ofs_rby + (0x8000u - (unsigned int)s_gsi.frame_ht + (Y + H)));
    s_ppcktCur = p;
}

void GS_DrawSpriteS(int x, int y, int w, int h)
{
    int shx = s_gsi.shift_x;
    int shy = s_gsi.shift_y;
    unsigned int X = gs_shl(x, shx);
    unsigned int Y = gs_shl(y, shy);
    unsigned int W = gs_shl(w, shx);
    unsigned int H = gs_shl(h, shy);
    unsigned int *p = s_ppcktCur;
    p = gif_tag(p, 2, 1, 1, 0x46, 0, 1, GIF_R_XYZF2);
    p = gs_xyzf(p, (unsigned int)s_gsi.ofs_ltx + (0x8000u - (unsigned int)s_gsi.frame_wd + X),
                (unsigned int)s_gsi.ofs_lty + (0x8000u - (unsigned int)s_gsi.frame_ht + Y));
    p = gs_xyzf(p, (unsigned int)s_gsi.ofs_rbx + (0x8000u - (unsigned int)s_gsi.frame_wd + (X + W)),
                (unsigned int)s_gsi.ofs_rby + (0x8000u - (unsigned int)s_gsi.frame_ht + (Y + H)));
    s_ppcktCur = p;
}

void GS_DrawSpriteEx(int x, int y, int u, int v, int w, int h, int flags)
{
    int sx = s_gsi.shift_x;
    int sy = s_gsi.shift_y;
    GS_DrawSpriteDP((int)gs_shl(x, sx), (int)gs_shl(y, sy), (int)gs_shl((int)((unsigned int)x + (unsigned int)w), sx),
                    (int)gs_shl((int)((unsigned int)y + (unsigned int)h), sy), (int)gs_shl(u, 4), (int)gs_shl(v, 4),
                    (int)gs_shl((int)((unsigned int)u + (unsigned int)w), 4),
                    (int)gs_shl((int)((unsigned int)v + (unsigned int)h), 4), flags);
}

void GS_DrawSprite(int x, int y, int u, int v, int w, int h)
{
    int sx = s_gsi.shift_x;
    int sy = s_gsi.shift_y;
    GS_DrawSpriteDP((int)gs_shl(x, sx), (int)gs_shl(y, sy), (int)gs_shl((int)((unsigned int)x + (unsigned int)w), sx),
                    (int)gs_shl((int)((unsigned int)y + (unsigned int)h), sy), (int)gs_shl(u, 4), (int)gs_shl(v, 4),
                    (int)gs_shl((int)((unsigned int)u + (unsigned int)w), 4),
                    (int)gs_shl((int)((unsigned int)v + (unsigned int)h), 4), 0);
}

void GS_DrawSpriteD(int ltx, int lty, int rbx, int rby, float ltu, float ltv, float rbu, float rbv, int flags)
{
    int sx = s_gsi.shift_x;
    int sy = s_gsi.shift_y;
    GS_DrawSpriteDP((int)gs_shl(ltx, sx), (int)gs_shl(lty, sy), (int)gs_shl(rbx, sx), (int)gs_shl(rby, sy),
                    ps2_ftoi(ltu * 16.0f), ps2_ftoi(ltv * 16.0f), ps2_ftoi(rbu * 16.0f), ps2_ftoi(rbv * 16.0f), flags);
}

void GS_DrawSpriteDP(int ltx, int lty, int rbx, int rby, int ltu, int ltv, int rbu, int rbv, int flags)
{
    if (flags & 1) {
        int t = ltu;
        ltu = rbu;
        rbu = t;
    }
    if (flags & 2) {
        int t = ltv;
        ltv = rbv;
        rbv = t;
    }
    unsigned int *p = s_ppcktCur;
    p = gif_tag(p, 2, 1, 1, 0x156, 0, 2, 0x43);
    p[0] = (unsigned int)ltu + (unsigned int)s_gsi.ofs_ltu;   /* UV: words 2, 3 not written */
    p[1] = (unsigned int)ltv + (unsigned int)s_gsi.ofs_ltv;
    p += 4;
    p = gs_xyzf(p, (unsigned int)s_gsi.ofs_ltx + (0x8000u - (unsigned int)s_gsi.frame_wd + (unsigned int)ltx),
                (unsigned int)s_gsi.ofs_lty + (0x8000u - (unsigned int)s_gsi.frame_ht + (unsigned int)lty));
    p[0] = (unsigned int)rbu + (unsigned int)s_gsi.ofs_rbu;
    p[1] = (unsigned int)rbv + (unsigned int)s_gsi.ofs_rbv;
    p += 4;
    p = gs_xyzf(p, (unsigned int)s_gsi.ofs_rbx + (0x8000u - (unsigned int)s_gsi.frame_wd + (unsigned int)rbx),
                (unsigned int)s_gsi.ofs_rby + (0x8000u - (unsigned int)s_gsi.frame_ht + (unsigned int)rby));
    s_ppcktCur = p;
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper (gen_bridge stops at 8 int args); flags is the caller's stack arg at sp+0 */
static void bw__GS_DrawSpriteDP(EECore *c)
{
    unsigned int sp = c->gpr[SP].uw[0];
    GS_DrawSpriteDP(c->gpr[A0].sw[0], c->gpr[A1].sw[0], c->gpr[A2].sw[0], c->gpr[A3].sw[0], c->gpr[T0].sw[0],
                    c->gpr[T1].sw[0], c->gpr[T2].sw[0], c->gpr[T3].sw[0], (int)rd32(sp));
}
#endif

void GS_SetTextureOffset(int ofsUls, int ofsUlt, int ofsLrs, int ofsLrt)
{
    s_gsi.ofs_ltu = ofsUls;
    s_gsi.ofs_ltv = ofsUlt;
    s_gsi.ofs_rbu = ofsLrs;
    s_gsi.ofs_rbv = ofsLrt;
}

void GS_SetDrawOffset(int ofsUlx, int ofsUly, int ofsLrx, int ofsLry, int flipshift)
{
    s_gsi.ofs_ltx = ofsUlx;
    s_gsi.ofs_lty = ofsUly;
    s_gsi.ofs_rbx = ofsLrx;
    s_gsi.ofs_rby = ofsLry;
    s_gsi.flipshift = flipshift;
}

void GS_GetDrawOffset(int *pofsUlx, int *pofsUly, int *pofsLrx, int *pofsLry, int *pflipshift)
{
    *pofsUlx = s_gsi.ofs_ltx;
    *pofsUly = s_gsi.ofs_lty;
    *pofsLrx = s_gsi.ofs_rbx;
    *pofsLry = s_gsi.ofs_rby;
    *pflipshift = s_gsi.flipshift;
}

void GSX_SetWindowSize(int wd, int ht, int sx, int sy)
{
    s_gsi.shift_x = sx;
    s_gsi.shift_y = sy;
    s_gsi.frame_wd = (int)gs_shl(wd, sx);
    s_gsi.frame_ht = (int)gs_shl(ht, sy);
}

void DF_ACSTAT_DrawSpriteC(int x, int y, int w, int h)
{
    unsigned int X1 = gs_shl(x + 0x700, 4);
    unsigned int Y1 = gs_shl(y / 2 + 0x790, 4);
    unsigned int *p = s_ppcktCur;
    p = gif_tag(p, 2, 1, 1, 0x46, 0, 1, GIF_R_XYZF2);
    p = gs_xyzf(p, X1, Y1);
    p = gs_xyzf(p, X1 + (unsigned int)w, Y1 + (unsigned int)(h / 2));
    s_ppcktCur = p;
}

void GS_SetRGBA(int *rgbai)
{
    unsigned int *p = s_ppcktCur;
    p = gif_tag(p, 1, 1, 0, 0, 0, 1, GIF_R_RGBAQ);
    p = gs_put_quad(p, rgbai);
    s_ppcktCur = p;
}

void GS_SetTEST_1(int ate, int atest, int aref, int afail, int date, int datm, int zte, int ztst)
{
    unsigned long long d = gs_sx(ate, 0) | gs_sx(atest, 1) | gs_sx(aref, 4) | gs_sx(afail, 12) | gs_sx(date, 14) |
                           gs_sx(datm, 15) | gs_sx(zte, 16) | gs_sx(ztst, 17);
    unsigned int *p = s_ppcktCur;
    p = gif_tag_ad(p, 1);
    p = put_ad(p, d, 0x47);
    s_ppcktCur = p;
}

void GS_SetTEX1_1(struct struct_d42f65aa tex1)
{
    PS2_FRAME(0x10);
    PS2_STACK(struct struct_d42f65aa, spill, 8);
    spill = tex1;
    unsigned long long d;
    memcpy(&d, &tex1, 8);
    unsigned int *p = s_ppcktCur;
    p = gs_tag_ad_pre(p);
    s_gsi.linear = tex1.MMAG ? 1 : (tex1.MMIN != 0);
    p = put_ad(p, d, GS_TEX1_1);
    s_ppcktCur = p;
}

void GS_SetTEX0_1(struct struct_f386fb29 tex0)
{
    PS2_FRAME(0x10);
    PS2_STACK(struct struct_f386fb29, spill, 8);
    spill = tex0;
    unsigned long long d;
    memcpy(&d, &tex0, 8);
    unsigned int *p = s_ppcktCur;
    p = gif_tag_ad(p, 1);
    p = put_ad(p, d, GS_TEX0_1);
    s_ppcktCur = p;
}

void GS_SetCLAMP_1(int wms, int wmt, int minu, int maxu, int minv, int maxv)
{
    unsigned long long d = gs_sx(wms, 0) | gs_sx(wmt, 2) | gs_sx(minu, 4) | gs_sx(maxu, 14) | gs_sx(minv, 24) |
                           gs_sx(maxv, 34);
    unsigned int *p = s_ppcktCur;
    p = gs_tag_ad_pre(p);
    p = put_ad(p, d, GS_CLAMP_1);
    s_ppcktCur = p;
}

void GS_SetTEXFLUSH(void)
{
    unsigned int *p = s_ppcktCur;
    p = gs_tag_ad_pre(p);
    p = put_ad(p, 0, 0x3f);
    s_ppcktCur = p;
}

void GS_SetALPHA_1(int a, int b, int c, int d, int fix)
{
    unsigned long long v = gs_sx(a, 0) | gs_sx(b, 2) | gs_sx(c, 4) | gs_sx(d, 6) | gs_sx(fix, 32);
    unsigned int *p = s_ppcktCur;
    p = gs_tag_ad_pre(p);
    p = put_ad(p, v, GS_ALPHA_1);
    s_ppcktCur = p;
}

void GS_SetRegisterS(unsigned long long *addr, int num)
{
    unsigned int *p = s_ppcktCur;
    p = gs_put64(p, gs_sx(num, 0) | 0x8000ull | 1ull << 60);
    p = gs_put64(p, GIF_R_AD);
    for (unsigned int i = (unsigned int)num * 2; i != 0; i--)
        p = gs_put64(p, *addr++);
    s_ppcktCur = p;
}

void GS_SetRegister(unsigned long long adr, unsigned long long reg)
{
    unsigned int *p = s_ppcktCur;
    p = gs_tag_ad_pre(p);
    p = put_ad(p, reg, adr);
    s_ppcktCur = p;
}

void GS_TexCoord2fv(float *st)
{
    unsigned int *p = s_ppcktCur;
    p[0] = ps2_f2u(st[0]);
    p[1] = ps2_f2u(st[1]);
    p[2] = 0x3f800000;
    p[3] = 0;
    s_ppcktCur = p + 4;
}

void GS_Color4iv(int *col)
{
    s_ppcktCur = gs_put_quad(s_ppcktCur, col);
}

void GS_Vertex4iv(int *vtx)
{
    s_ppcktCur = gs_put_quad(s_ppcktCur, vtx);
}

void GS_End(void)
{
}

void GS_Begin(unsigned int prim, int cnt, unsigned long long regs, int nreg)
{
    unsigned int *p = s_ppcktCur;
    p[0] = (unsigned int)cnt;
    p[1] = (unsigned int)nreg << 28 | (prim & 0x7ff) << 15 | 0x4000;
    p = gs_put64(p + 2, regs);
    s_ppcktCur = p;
}

void GS_FlushList(void)
{
    PS2_FRAME(0x10);
    unsigned int *top = s_ppcktTop;
    if (!top)
        return;
    unsigned int *p = s_ppcktCur;
    p = gs_put64(p, 0x8000);
    p = gs_put64(p, 0);
    s_ppcktCur = p;
    int qwc = (int)((char *)p - (char *)top) / 16;
#ifdef ACNB_SOURCEPORT
    bool ram = !((unsigned char *)top >= g_ps2_spr && (unsigned char *)top < g_ps2_spr + sizeof(g_ps2_spr));
#else
    bool ram = (int)PS2PTR(top) < 0x70000000;
#endif
    if (ram)
        GifPath3KickMem2(top, qwc);
    else
        GifPath3KickSPR(top, qwc);
    s_ppcktCur = nullptr;
    s_ppcktTop = nullptr;
    PBuffID = PBuffID == 0;
}

void GS_NewListEx(void *pbuff0, void *pbuff1)
{
    unsigned int *p = (unsigned int *)(PBuffID ? pbuff1 : pbuff0);
    s_ppcktCur = p;
    s_ppcktTop = p;
}

void GS_NewList(void)
{
    unsigned int *p = (unsigned int *)PrimitiveBuffer[PBuffID];
    s_ppcktCur = p;
    s_ppcktTop = p;
}

int GS_GetRest(void)
{
    return 0x100 - (int)((char *)s_ppcktCur - (char *)s_ppcktTop) / 16;
}

/* local -> local transfer: four separate A+D tags (PRE 1, EOP 0), BITBLTBUF / TRXPOS / TRXREG / TRXDIR 2.
 * Every int is sign-extended into the 64-bit OR chain (no masking: a negative argument smears). */
void GS_BitBlt(int bpDst, int bwDst, int psmDst, int xDst, int yDst, int bpSrc, int bwSrc, int psmSrc, int xSrc,
               int ySrc, int width, int height)
{
    unsigned int *p = s_ppcktCur;
    p = gs_tag_ad_pre(p);
    p = gs_put64(p, gs_sx(bpSrc, 0) | gs_sx(bwSrc, 16) | gs_sx(psmSrc, 24) | gs_sx(bpDst, 32) | gs_sx(bwDst, 48) |
                    gs_sx(psmDst, 56));
    p = gs_put64(p, 0x50);
    p = gs_tag_ad_pre(p);
    p = gs_put64(p, gs_sx(xSrc, 0) | gs_sx(ySrc, 16) | gs_sx(xDst, 32) | gs_sx(yDst, 48));
    p = gs_put64(p, 0x51);
    p = gs_tag_ad_pre(p);
    p = gs_put64(p, gs_sx(width, 0) | gs_sx(height, 32));
    p = gs_put64(p, 0x52);
    p = gs_tag_ad_pre(p);
    p = gs_put64(p, 2);
    p = gs_put64(p, 0x53);
    s_ppcktCur = p;
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper (12 int args): xSrc / ySrc / width / height are the caller's stack args at sp+0 / 8 /
 * 0x10 / 0x18 (lw) */
static void bw__GS_BitBlt(EECore *c)
{
    unsigned int sp = c->gpr[SP].uw[0];
    GS_BitBlt(c->gpr[A0].sw[0], c->gpr[A1].sw[0], c->gpr[A2].sw[0], c->gpr[A3].sw[0], c->gpr[T0].sw[0],
              c->gpr[T1].sw[0], c->gpr[T2].sw[0], c->gpr[T3].sw[0], (int)rd32(sp), (int)rd32(sp + 8),
              (int)rd32(sp + 0x10), (int)rd32(sp + 0x18));
}
#endif

/* host -> local image upload appended to the list (only caller MsgWnd_Draw, never reached in a replay).
 * Quirks kept: 4-bit formats count 2 bytes per pixel (4x the data); an unsupported PSM leaves the byte count as the
 * caller's stale t1; an odd dword count leaves the cursor 8 bytes into a qword; NLOOP masked to 15 bits. */
void GS_LoadImage(void *pimg, int tbp, int psm, int wd, int ht)
{
    int bytes;
    switch (psm) {
    case 0x24: case 0x2c: case 0x14: bytes = (wd * ht) << 1; break;
    case 0x1b: case 0x13: bytes = wd * ht; break;
    case 2: bytes = (wd * ht) << 1; break;
    case 1: { int v = wd * ht; bytes = (v << 1) + v; break; }
    case 0: bytes = (wd * ht) << 2; break;
    default: bytes = (int)PS2_STALE_GPR(9 /* t1 */); break;
    }
    int tbw = (wd + 63) >> 6;
    unsigned int *p = s_ppcktCur;
    p = gs_put64(p, 0x1000000000000000ull | 4);
    p = gs_put64(p, 0xe);
    p = gs_put64(p, gs_sx(tbp, 32) | gs_sx(tbw, 48) | gs_sx(psm, 56));
    p = gs_put64(p, 0x50);
    p = gs_put64(p, 0);
    p = gs_put64(p, 0x51);
    p = gs_put64(p, gs_sx(wd, 0) | gs_sx(ht, 32));
    p = gs_put64(p, 0x52);
    p = gs_put64(p, 0);
    p = gs_put64(p, 0x53);
    p = gs_put64(p, 0x0800000000000000ull | 0x8000 | (gs_sx(bytes >> 4, 0) & 0x7fff));
    p = gs_put64(p, 0);
    int n = bytes >> 3;
    const unsigned int *s = (const unsigned int *)pimg;
    for (int i = 0; i < n; i++) {
        p[0] = s[0];
        p[1] = s[1];
        p += 2;
        s += 2;
    }
    s_ppcktCur = p;
}

/* black (alpha-only) flat sprite over a screen-pixel rectangle. Quirk kept: vertex 1's RGBAQ qword is skipped, never
 * written (stale buffer bytes; a flat SPRITE takes the last vertex's colour). */
void GS_DrawBlack(int x, int y, int w, int h, int alpha)
{
    unsigned int *p = s_ppcktCur;
    p[0] = 0x8002;        /* NLOOP 2, EOP, PRE, PRIM 0x46 (SPRITE | ABE), PACKED, NREG 2: RGBAQ, XYZ2 */
    p[1] = 0x20234000;
    p[2] = 0x41;
    p[3] = 0;
    p += 8;               /* vertex 1 RGBAQ: not written */
    p = gs_xyzf(p, (unsigned int)s_gsi.ofs_ltx + (0x8000u - (unsigned int)s_gsi.frame_wd + gs_shl(x, s_gsi.shift_x)),
                (unsigned int)s_gsi.ofs_lty + (0x8000u - (unsigned int)s_gsi.frame_ht + gs_shl(y, s_gsi.shift_y)));
    p[0] = 0;
    p[1] = 0;
    p[2] = 0;
    p[3] = (unsigned int)alpha;
    p += 4;
    p = gs_xyzf(p, (unsigned int)s_gsi.ofs_rbx + (0x8000u - (unsigned int)s_gsi.frame_wd + gs_shl(x + w, s_gsi.shift_x)),
                (unsigned int)s_gsi.ofs_rby + (0x8000u - (unsigned int)s_gsi.frame_ht + gs_shl(y + h, s_gsi.shift_y)));
    s_ppcktCur = p;
}

/* D2_CHCR.STR || GIF_STAT.APATH (source port: never busy) */
static inline void gs_wait_path_idle(void)
{
    while (ps2_gs_path_busy() & 0x12) {}
}

/* GS -> memory readback of a rectangle (stage / AC part textures at load time) */
void DF_StoreImage(void *addr, int tbp, int psm, int x, int y, int wd, int ht)
{
    PS2_FRAME(0x90);
    PS2_STACK(struct_9d4f855e, si, 0x20);   /* sceGsStoreImage: built and sent by the SDK */
    PS2_RQ_SCOPE(RQ_K_READBACK, RQ_OBJID(RQ_OC_TEXTURE, (unsigned int)tbp & 0x3fff), (unsigned int)psm);   /* render tag (host only) */
    gs_wait_path_idle();
    sceGsSetDefStoreImage(&si, (short)tbp, (short)((wd + 63) >> 6), (short)psm, (short)x, (short)y, (short)wd,
                          (short)ht);
    FlushCache(0);
    sceGsExecStoreImage(&si, addr);
    gs_wait_path_idle();
}

/* immediate memory -> GS upload (weapon-name textures) */
void DF_LoadImageEx(void *addr, int tbp, int tbw, int psm, int x, int y, int wd, int ht)
{
    PS2_FRAME(0x80);
    PS2_STACK(struct_cb63eb70, li, 0x20);   /* sceGsLoadImage: built and sent by the SDK */
    PS2_RQ_SCOPE(RQ_K_UPLOAD, RQ_OBJID(RQ_OC_TEXTURE, (unsigned int)tbp & 0x3fff), (unsigned int)psm);   /* render tag (host only) */
    gs_wait_path_idle();
    sceGsSetDefLoadImage(&li, (short)tbp, (short)tbw, (short)psm, (short)x, (short)y, (short)wd, (short)ht);
    FlushCache(0);
    sceGsExecLoadImage(&li, addr);
    gs_wait_path_idle();
}

/* A+D TEXA write. Original quirk: the three ints are sign-extended to 64 bits and OR-ed without masking, so a negative
 * argument fills the fields above it (the only caller passes 0x80, 1, 0x80). */
void GS_SetTEXA(int ta0, int aem, int ta1)
{
    unsigned int *p = s_ppcktCur;
    p = gs_put64(p, 0x1000000000008001ULL);   /* GIF tag: NLOOP 1, EOP, NREG 1 */
    p = gs_put64(p, 0xe);                      /* A+D */
    p = gs_put64(p, (unsigned long long)(long long)ta0 | (unsigned long long)(long long)aem << 15 |
                        (unsigned long long)(long long)ta1 << 32);
    p = gs_put64(p, 0x3b);                     /* TEXA */
    s_ppcktCur = p;
}

/* ALPHA_1 preset: 0 -> 0x44, 1 -> 0x48, 2 -> 0x42 (other values append nothing). The tag is written as four words
 * (NLOOP 1, PRE + NREG 1, REGS A+D) */
void GS_SetBlendMode(int blend)
{
    unsigned long long alpha;
    switch (blend) {
    case 0: alpha = 0x44; break;
    case 1: alpha = 0x48; break;
    case 2: alpha = 0x42; break;
    default: return;
    }
    unsigned int *p = s_ppcktCur;
    p[0] = 1; p[1] = 0x10004000; p[2] = 0xe; p[3] = 0;
    p = gs_put64(p + 4, alpha);
    p = gs_put64(p, 0x42);                     /* ALPHA_1 */
    s_ppcktCur = p;
}

/* UV register qword from two ints (copied as words) */
void GS_UVv(int *uv)
{
    unsigned int *p = s_ppcktCur;
    p[0] = (unsigned int)uv[0];
    p[1] = (unsigned int)uv[1];
    p[2] = 0; p[3] = 0;
    s_ppcktCur = p + 4;
}

/* ST(Q) qword: the three floats' bits, then a zero word */
void GS_TexCoord3f(float s, float t, float q)
{
    unsigned int *p = s_ppcktCur;
    memcpy(p, &s, 4);
    memcpy(p + 1, &t, 4);
    memcpy(p + 2, &q, 4);
    p[3] = 0;
    s_ppcktCur = p + 4;
}

/* sets ADC (bit 15 of the last word) of the vertex just written: no drawing kick for it. No empty-list check. */
void GS_NoKick(void)
{
    unsigned int *p = s_ppcktCur;
    p[-1] |= 0x8000;
}
