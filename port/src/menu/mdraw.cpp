/* src/menu/mdraw.cpp — menu 2D drawing (OLMENU): sprites, windows and their decorations as GIF packets, built in
 * the scratchpad (or a RAM packet buffer when the menu runs without SPR) and kicked by DMA.
 * Native so far: class M1 = the leaves - the pixel / texel offsets applied to rectangle edges (g_mdofs: linear
 * mode adds the per-edge offsets, the Y edges also shift by g_odevCur * flipshift for the odd field; non-linear
 * mode adds 8 to the left / top texel edges only) and the packet buffer helpers; the window decorations of part A
 * (spot, light cursor, simple labels, tag window, the 9-patch boxes, report 1): rects / uv quads from rodata
 * templates built on the stack, one texture (0x3e9 menu parts, 0x3ec light parts), one DF_MDRAW_DrawTexSprite per
 * piece; part B (report 2, string-select bars, M1 help bar, the VS / RE / RE2 / light / general / black window
 * frames, Nexus general window). Class P part 1 (phase 2): the packet builders - DrawTexSprite and its variants,
 * DrawPrim, DrawColSprite, KickReg, LoadImage and the kick DF_MDRAW_GIFKickSPR (acnb/gif.h + port.h ps2_dma_kick).
 * Part 2: the TM2 upload Tm2LoadImageNX, the local copy MDraw_BitBlt and the shadow render target
 * DF_MDRAW_SetShadowFrame (SDK draw env / DMA calls). */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"
#include "acnb/gif.h"

int MDraw_GetOffsetBottomV(int v)
{
    if (g_mdofs.linear) v += g_mdofs.ofs_rbv;
    return v;
}

int MDraw_GetOffsetRightU(int u)
{
    if (g_mdofs.linear) u += g_mdofs.ofs_rbu;
    return u;
}

int MDraw_GetOffsetTopV(int v)
{
    return g_mdofs.linear ? v + g_mdofs.ofs_ltv : v + 8;
}

int MDraw_GetOffsetLeftU(int u)
{
    return g_mdofs.linear ? u + g_mdofs.ofs_ltu : u + 8;
}

/* odd-field shift: mult low word of g_odevCur * flipshift */
static int mdraw_field_shift(void)
{
    return (int)((unsigned int)g_odevCur * (unsigned int)g_mdofs.flipshift);
}

int MDraw_GetOffsetBottomY(int y)
{
    if (g_mdofs.linear) y = y + g_mdofs.ofs_rby + mdraw_field_shift();
    return y;
}

int MDraw_GetOffsetRightX(int x)
{
    if (g_mdofs.linear) x += g_mdofs.ofs_rbx;
    return x;
}

int MDraw_GetOffsetTopY(int y)
{
    if (g_mdofs.linear) y = y + g_mdofs.ofs_lty + mdraw_field_shift();
    return y;
}

int MDraw_GetOffsetLeftX(int x)
{
    if (g_mdofs.linear) x += g_mdofs.ofs_ltx;
    return x;
}

void MDraw_SetLinearOffset(int isLinear)
{
    g_mdofs.linear = isLinear != 0;
}

/* current packet buffer: the scratchpad double buffer or the RAM one (DF_MENU_Reset's InitBuffNoSPR) */
static unsigned int *mdraw_cur_buff(void)
{
    if (s_UseSPR) return MenuPktBuff[MenuPktID];
    return MenuPktBuffNoSPR[MenuPktIDNoSPR];
}

/* 1 when inDmap plus inOffset qwords reaches 0x180 qwords (6 KB) past the buffer start (signed / 16, toward 0) */
int DF_MDRAW_IsSPRBuffOver(unsigned int *inDmap, int inOffset)
{
    int bytes = (int)((char *)inDmap - (char *)mdraw_cur_buff());
    return inOffset + bytes / 16 >= 0x180;
}

unsigned int *DF_MDRAW_GetSPRBuff(void)
{
    return mdraw_cur_buff();
}

void DF_MDRAW_ChangeUseSPR(unsigned int inF)
{
    s_UseSPR = inF;
}

void DF_MDRAW_InitBuffNoSPR(unsigned char *pInBuff)
{
    MenuPktBuffNoSPR[0] = (unsigned int *)pInBuff;
    MenuPktBuffNoSPR[1] = (unsigned int *)(pInBuff + 0x1800);
}

/* ---- packet builders, part 1 (class P, phase 2 group 1) ----
 * Every builder writes its GIF packet from the start of the current buffer (no 0x180-qword limit check; a NULL
 * buffer skips the packet) and ends with DF_MDRAW_GIFKickSPR, which sends buffer start .. end through DMA channel
 * 2 (chain mode, an END tag in RAM after the packet) and flips to the other half of the double buffer. Screen
 * coordinates are 12.4 fixed point with the 0x6c0 / (0x800 - 0xe0) offsets; no tag calls here: the packets carry the
 * ambient scope of their caller. */

#ifdef ACNB_SOURCEPORT
struct struct_94091e88 g_mdofs = {0, -8, -3, -8, -3, 0, 0, 0, 0, 0};
unsigned int *MenuPktBuff[2] = {(unsigned int *)PS2_SPR(0x1000), (unsigned int *)PS2_SPR(0x2800)};
unsigned int DF_MDRAW_GIFKickSPR__dma[4] = {0x70000000, 0, 0, 0};   /* DMA END tag, QWC 0 */
#endif

/* the buffer switch after a kick (the active scheme's id only) */
static void mdraw_flip(void)
{
    if (s_UseSPR) MenuPktID = 1 - MenuPktID;
    else MenuPktIDNoSPR = 1 - MenuPktIDNoSPR;
}

/* DMA source address of a packet buffer: scratchpad addresses carry bit 31 (SPR flag), RAM ones have it cleared */
static unsigned int mdraw_dma_addr(const unsigned int *buf)
{
    return PS2_DMA_ADDR(buf, s_UseSPR ? PS2PTR(buf) | 0x80000000u : PS2PTR(buf) & 0x7fffffffu);
}

/* the 64-bit TEX0 / TEX1 register images of a texture info */
static inline unsigned long long mdraw_tex0(const struct_13efd7f4 *tex)
{
    unsigned long long v;
    memcpy(&v, (const unsigned char *)tex, 8);
    return v;
}

static inline unsigned long long mdraw_tex1(const struct_13efd7f4 *tex)
{
    unsigned long long v;
    memcpy(&v, (const unsigned char *)tex + 8, 8);
    return v;
}

/* GIF tag of the TEX1 / TEX0 setting: {A+D NLOOP 2} {TEX1_1} {TEX0_1} */
static unsigned int *mdraw_put_tex(unsigned int *p, const struct_13efd7f4 *tex)
{
    p = gif_tag_ad(p, 2);
    p = put_ad(p, mdraw_tex1(tex), GS_TEX1_1);
    return put_ad(p, mdraw_tex0(tex), GS_TEX0_1);
}

/* kick buffer start .. inDmap (qwords, signed / 16 toward 0) when it is not empty: RAM buffers flush the D-cache
 * first; MADR = the buffer start (SPR flag in bit 31), TADR = the END tag, CHCR = chain | STR; then flip buffers */
void DF_MDRAW_GIFKickSPR(unsigned int *inDmap)
{
    PS2_FRAME(0x20);
    unsigned int use_spr = s_UseSPR;
    unsigned int *buf = use_spr ? MenuPktBuff[MenuPktID] : MenuPktBuffNoSPR[MenuPktIDNoSPR];
    int n = (int)((char *)inDmap - (char *)buf) / 16;
    if (n <= 0) return;
    if (!use_spr) FlushCache(0);
    ps2_dma_wait_gif();
    ps2_dma_kick(2, 0x105, mdraw_dma_addr(mdraw_cur_buff()), (unsigned int)n, PS2PTR(DF_MDRAW_GIFKickSPR__dma));
    mdraw_flip();
}

/* one GS register write through A+D: {A+D NLOOP 1} {inData, inAddr} */
void DF_MDRAW_KickReg(unsigned long long inAddr, unsigned long long inData)
{
    PS2_FRAME(0x10);
    unsigned int *p = mdraw_cur_buff();
    if (!p) return;
    p = gif_tag_ad(p, 1);
    p = put_ad(p, inData, inAddr);
    DF_MDRAW_GIFKickSPR(p);
}

/* texture upload: CNT tag (6 qwords) + the SDK's load-image GIF packet (BITBLTBUF / TRXPOS / TRXREG / TRXDIR, then
 * the IMAGE tag), then a REFE tag that sends the pixels straight from inSbp (QWC = the IMAGE tag's NLOOP). Kicked
 * as a chain from the buffer start (TADR + CHCR only: MADR / QWC come from the walk); TBW = ceil(inW / 64) */
void DF_MDRAW_LoadImage(void *inSbp, unsigned int inTbp, unsigned int inPsm, int inX, int inY, int inW, int inH)
{
    PS2_FRAME(0x30);
    unsigned int *p = mdraw_cur_buff();
    if (!p) return;
    p = dma_tag(p, 6, GIF_ID_CNT, 0);
    int tbw = inW / 64 + (inW % 64 != 0);
    sceGsSetDefLoadImage(p, (short)inTbp, (short)tbw, (short)inPsm, (short)inX, (short)inY, (short)inW, (short)inH);
    unsigned int qwc = *(unsigned short *)((unsigned char *)p + 0x50) & 0x7fffu;
    dma_tag(p + 0x18, qwc, GIF_ID_REFE, PS2PTR(inSbp));
    if (!s_UseSPR) FlushCache(0);
    ps2_dma_wait_gif();
    ps2_dma_kick(2, 0x105, 0, 0, mdraw_dma_addr(mdraw_cur_buff()));
    mdraw_flip();
}

/* textured sprite (TEX1 / TEX0 of the texture, then RGBAQ + two UV / XYZF2 corners). Re-derives g_mdofs.linear from
 * TEX1 (MMAG or MMIN set) when drawn; linear mode adds the per-edge offsets (y edges also the field shift), else
 * the left / top texel edges get +8. f_movie_brief doubles the 0xe0 y offset. NULL inColp = 0x80808080 */
void DF_MDRAW_DrawTexSprite(struct struct_13efd7f4 *inTexp, struct stMenuRect *inRectp, int *inUV,
                            struct struct_76b810ec *inColp, unsigned int inDepth)
{
    PS2_FRAME(0x20);
    PS2_STACK(unsigned char[4], dcol, 0x1c);
    dcol[0] = 0x80; dcol[1] = 0x80; dcol[2] = 0x80; dcol[3] = 0x80;
    unsigned int *p = mdraw_cur_buff();
    int f = MenuMng.f_movie_brief ? 2 : 1;
    if (!inTexp) return;
    int x0 = inRectp->x, x1 = inRectp->x + inRectp->w;
    int y0 = inRectp->y, y1 = inRectp->y + inRectp->h;
    const unsigned char *col = inColp ? (const unsigned char *)inColp : dcol;
    if (!p) return;

    p = mdraw_put_tex(p, inTexp);
    const unsigned char *t1 = (const unsigned char *)inTexp + 8;
    unsigned short t1h;
    memcpy(&t1h, t1, 2);
    g_mdofs.linear = (t1[0] >> 5 & 1) || (t1h >> 6 & 7) != 0;
    p = gif_tag_prim(p, 1, 0x15e, 5, 0x43431);
    p = put_rgbaq(p, col);
    int ybias = 0x800 - 0xe0 * f;
    unsigned int z = inDepth << 4;
    int u0 = (inUV[0] << 4) + (g_mdofs.linear ? g_mdofs.ofs_ltu : 8);
    int v0 = (inUV[1] << 4) + (g_mdofs.linear ? g_mdofs.ofs_ltv : 8);
    p = put_uv(p, u0, v0);
    int sx0 = g_mdofs.linear ? ((x0 + 0x6c0) << 4) + g_mdofs.ofs_ltx : (x0 + 0x6c0) << 4;
    int sy0 = g_mdofs.linear ? ((y0 + ybias) << 4) + g_mdofs.ofs_lty + mdraw_field_shift() : (y0 + ybias) << 4;
    p = put_xyzf2(p, sx0, sy0, z);
    int u1 = inUV[2] << 4;
    if (g_mdofs.linear) u1 += g_mdofs.ofs_rbu;
    int v1 = inUV[3] << 4;
    if (g_mdofs.linear) v1 += g_mdofs.ofs_rbv;
    p = put_uv(p, u1, v1);
    int sx1 = g_mdofs.linear ? ((x1 + 0x6c0) << 4) + g_mdofs.ofs_rbx : (x1 + 0x6c0) << 4;
    int sy1 = g_mdofs.linear ? ((y1 + ybias) << 4) + g_mdofs.ofs_rby + mdraw_field_shift() : (y1 + ybias) << 4;
    p = put_xyzf2(p, sx1, sy1, z);
    DF_MDRAW_GIFKickSPR(p);
}

/* the older sprite: same packet, no texture NULL check, no g_mdofs offsets, fixed 0xe0 y offset */
void DF_MDRAW_DrawTexSpriteOld(struct struct_13efd7f4 *inTexp, struct stMenuRect *inRectp, int *inUV,
                               struct struct_76b810ec *inColp, unsigned int inDepth)
{
    PS2_FRAME(0x20);
    PS2_STACK(unsigned char[4], dcol, 0x1c);
    dcol[0] = 0x80; dcol[1] = 0x80; dcol[2] = 0x80; dcol[3] = 0x80;
    unsigned int *p = mdraw_cur_buff();
    int x0 = inRectp->x, x1 = inRectp->x + inRectp->w;
    int y0 = inRectp->y, y1 = inRectp->y + inRectp->h;
    const unsigned char *col = inColp ? (const unsigned char *)inColp : dcol;
    if (!p) return;

    unsigned int z = inDepth << 4;
    p = mdraw_put_tex(p, inTexp);
    p = gif_tag_prim(p, 1, 0x15e, 5, 0x43431);
    p = put_rgbaq(p, col);
    p = put_uv(p, inUV[0] << 4, inUV[1] << 4);
    p = put_xyzf2(p, (x0 + 0x6c0) << 4, (y0 << 4) + 0x7200, z);
    p = put_uv(p, inUV[2] << 4, inUV[3] << 4);
    p = put_xyzf2(p, (x1 + 0x6c0) << 4, (y1 << 4) + 0x7200, z);
    DF_MDRAW_GIFKickSPR(p);
}

/* the sprite rotated by 90 degrees as a 4-vertex triangle strip (u runs along y): (x0, y0; u0, v1) (x0, y1; u2, v1)
 * (x1, y0; u0, v3) (x1, y1; u2, v3). No NULL checks on the texture or colour */
void DF_MDRAW_DrawTexSprite2(struct struct_13efd7f4 *inTexp, struct stMenuRect *inRectp, int *inUV,
                             struct struct_76b810ec *inColp, unsigned int inDepth)
{
    PS2_FRAME(0x10);
    unsigned int *p = mdraw_cur_buff();
    int x0 = inRectp->x, x1 = inRectp->x + inRectp->w;
    int y0 = inRectp->y, y1 = inRectp->y + inRectp->h;
    if (!p) return;

    unsigned int sy0 = (y0 << 4) + 0x7200, sy1 = (y1 << 4) + 0x7200;
    unsigned int z = inDepth << 4;
    p = mdraw_put_tex(p, inTexp);
    p = gif_tag_prim(p, 2, 0x15c, 5, 0x43431);
    for (int i = 0; i < 2; i++) {
        int v = inUV[i ? 3 : 1];
        unsigned int sx = ((i ? x1 : x0) + 0x6c0) << 4;
        p = put_rgbaq(p, (const unsigned char *)inColp);
        p = put_uv(p, inUV[0] << 4, v << 4);
        p = put_xyzf2(p, sx, sy0, z);
        p = put_uv(p, inUV[2] << 4, v << 4);
        p = put_xyzf2(p, sx, sy1, z);
    }
    DF_MDRAW_GIFKickSPR(p);
}

/* textured quad as a triangle strip with one colour per corner (inColp = 4 colours in vertex order (x0, y0)
 * (x0, y1) (x1, y0) (x1, y1)); positions get a -8 (half pixel) bias on both axes, no g_mdofs */
void DF_MDRAW_DrawTexSprite4Colors(struct struct_13efd7f4 *inTexp, struct stMenuRect *inRectp, int *inUV,
                                   struct struct_76b810ec *inColp, unsigned int inDepth)
{
    PS2_FRAME(0x70);
    unsigned int *p = mdraw_cur_buff();
    if (!inTexp) return;
    int x0 = inRectp->x, y0 = inRectp->y;
    int x1 = x0 + inRectp->w, y1 = y0 + inRectp->h;
    if (!p) return;

    unsigned int z = inDepth << 4;
    p = mdraw_put_tex(p, inTexp);
    p = gif_tag_prim(p, 4, 0x15c, 3, 0x431);
    const unsigned char *col = (const unsigned char *)inColp;
    for (int k = 0; k < 4; k++) {
        int u = (k & 2) ? inUV[2] : inUV[0];
        int v = (k & 1) ? inUV[3] : inUV[1];
        int x = (k & 2) ? x1 : x0;
        int y = (k & 1) ? y1 : y0;
        p = put_rgbaq(p, col);
        col += 4;
        p = put_uv(p, u << 4, v << 4);
        p = put_xyzf2(p, ((x + 0x6c0) << 4) - 8, (y << 4) + 0x71f8, z);
    }
    DF_MDRAW_GIFKickSPR(p);
}

void DF_MDRAW_DrawTexSprite4ColorsI(int texindex, struct stMenuRect *inRectp, int *inUV,
                                    struct struct_76b810ec *inColp, unsigned int inDepth)
{
    PS2_FRAME(0x60);
    PS2_STACK(struct_13efd7f4, tex, 0x50);
    if (MenuTex_StandbyTexture(texindex, &tex)) DF_MDRAW_DrawTexSprite4Colors(&tex, inRectp, inUV, inColp, inDepth);
}

void DF_MDRAW_DrawTexSpriteI(int texid, struct stMenuRect *inRectp, int *inUV, struct struct_76b810ec *inColp,
                             unsigned int inDepth)
{
    PS2_FRAME(0x60);
    PS2_STACK(struct_13efd7f4, tex, 0x50);
    if (MenuTex_StandbyTexture(texid, &tex)) DF_MDRAW_DrawTexSprite(&tex, inRectp, inUV, inColp, inDepth);
}

/* sprite of a menu texture with bilinear filtering forced on (local TEX1: MMAG = 1, MMIN = 1); pRegAlpha != NULL
 * first sends ALPHA_1 = *pRegAlpha as its own kick (two kicks, two buffer flips) */
void DF_MDRAW_DrawTexSpriteIL(int texid, struct stMenuRect *inRectp, int *inUV, struct struct_76b810ec *inColp,
                              unsigned int inDepth, unsigned long long *pRegAlpha)
{
    PS2_FRAME(0x70);
    PS2_STACK(struct_13efd7f4, tex, 0x60);
    if (!MenuTex_StandbyTexture(texid, &tex)) return;
    unsigned char *t1 = (unsigned char *)&tex + 8;
    t1[0] = (unsigned char)((t1[0] & ~0x20) | 0x20);
    unsigned short h;
    memcpy(&h, t1, 2);
    h = (unsigned short)((h & ~0x1c0) | 0x40);
    memcpy(t1, &h, 2);
    if (pRegAlpha) {
        unsigned int *p = mdraw_cur_buff();
        if (p) {
            p = gif_tag_ad(p, 1);
            p = put_ad(p, *pRegAlpha, GS_ALPHA_1);
            DF_MDRAW_GIFKickSPR(p);
        }
    }
    DF_MDRAW_DrawTexSprite(&tex, inRectp, inUV, inColp, inDepth);
}

/* untextured primitive (prim type | IIP | ABE | FST, AntiAlias == 1 adds AA1) with numpoint RGBAQ / XYZF2 vertices;
 * vertex alpha = (u8)(int)(Alpha * colour alpha). A count <= 0 still sends the lone tag */
void DF_MDRAW_DrawPrim(int prim, int numpoint, int (*pos)[4], struct struct_76b810ec *color, float Alpha,
                       int AntiAlias)
{
    PS2_FRAME(0x10);
    unsigned int *p = mdraw_cur_buff();
    unsigned int gprim = (unsigned int)(prim | 0x148);
    if (!p) return;
    if (AntiAlias == 1) gprim |= 0x80;
    p = gif_tag_prim(p, (unsigned int)numpoint, gprim, 2, 0x41);
    const unsigned char *c = (const unsigned char *)color;
    for (int i = 0; i < numpoint; i++) {
        float a = Alpha * (float)c[3];
        unsigned int v;
        if (2147483648.0f <= a) {
            float t = a - 2147483648.0f;
            v = (unsigned int)ps2_ftoi(t) | 0x80000000u;
        } else {
            v = (unsigned int)ps2_ftoi(a);
        }
        p = put_rgbaq(p, c[0], c[1], c[2], v & 0xff);
        c += 4;
        p = put_xyzf2(p, (pos[i][0] + 0x6c0) << 4, (pos[i][1] << 4) + 0x7200, pos[i][2] << 4);
    }
    DF_MDRAW_GIFKickSPR(p);
}

/* flat-shaded / gouraud rectangle as a triangle strip (x0, y0) (x0, y1) (x1, y0) (x1, y1); the colour pointer moves
 * to the next colour only while colours remain (vertex k uses colour min(k, inColNum - 1)) */
void DF_MDRAW_DrawColSprite(struct stMenuRect *inRectp, int inColNum, struct struct_76b810ec *inColp,
                            unsigned int inDepth)
{
    PS2_FRAME(0x10);
    unsigned int *p = mdraw_cur_buff();
    int x0 = inRectp->x, x1 = inRectp->x + inRectp->w;
    int y0 = inRectp->y, y1 = inRectp->y + inRectp->h;
    if (!p) return;

    unsigned int z = inDepth << 4;
    p = gif_tag_prim(p, 4, 0x14c, 2, 0x41);
    const unsigned char *col = (const unsigned char *)inColp;
    for (int k = 0; k < 4; k++) {
        int x = (k / 2) ? x1 : x0;
        int y = (k % 2) ? y1 : y0;
        inColNum--;
        p = put_rgbaq(p, col);
        p = put_xyzf2(p, (x + 0x6c0) << 4, (y << 4) + 0x7200, z);
        if (inColNum > 0) col += 4;
    }
    DF_MDRAW_GIFKickSPR(p);
}

/* textured quad from four float corners (triangle strip, one colour for all four, TEX0 only); Blend 3 keeps the
 * current ALPHA_1, 1 / 2 / other set 0x48 / 0x42 / 0x44. x = (int)(16 * (1728 + x)), y = (int)(16 * y) + 0x7200 */
void DF_MDRAW_DrawTexSprite4(struct struct_13efd7f4 *inTexp, struct struct_76b810ec *pColor,
                             struct struct_839bfa8e *uv, struct struct_0d65143e *pos, int inDepth, int Blend)
{
    PS2_FRAME(0x10);
    unsigned int *p = mdraw_cur_buff();
    if (!inTexp || !p) return;

    p = gif_tag_ad(p, Blend == 3 ? 1 : 2);
    p = put_ad(p, mdraw_tex0(inTexp), GS_TEX0_1);
    if (Blend != 3) p = put_ad(p, Blend == 1 ? 0x48 : Blend == 2 ? 0x42 : 0x44, GS_ALPHA_1);
    p = gif_tag_prim(p, 4, 0x15c, 3, 0x431);
    unsigned int z = (unsigned int)inDepth << 4;
    for (int k = 0; k < 4; k++) {
        p = put_rgbaq(p, (const unsigned char *)pColor);
        p = put_uv(p, uv[k].u << 4, uv[k].v << 4);
        float fx = 1728.0f + pos[k].x;
        fx = 16.0f * fx;
        float fy = 16.0f * pos[k].y;
        p = put_xyzf2(p, ps2_ftoi(fx), ps2_ftoi(fy) + 0x7200, z);
    }
    DF_MDRAW_GIFKickSPR(p);
}

/* ---- packet builders, part 2 (class P, phase 2 group 1b): texture upload, local copy, shadow frame ---- */

/* 64-bit image of a sign-extended int (the original's lw + dsll / or: a negative field smears upward) */
static inline unsigned long long mdraw_sx(int v)
{
    return (unsigned long long)(long long)v;
}

/* GS local-to-local copy (BITBLTBUF / TRXPOS / TRXREG / TRXDIR = 2) through an A+D packet on the stack, sent with
 * the SDK's normal-mode DMA on channel 2; bit 6 of the channel's first byte (D2_CHCR.TTE) is forced on for the send
 * and restored */
void MDraw_BitBlt(struct stMDraw_BitBlt *pBitBlt)
{
    PS2_FRAME(0x80);
    PS2_STACK(unsigned int[20], pkt, 0x30);
    PS2_RQ_SCOPE(RQ_K_UPLOAD, 0, 0);   /* render tag (host only) */
    struct stMDraw_BitBlt *b = pBitBlt;
    unsigned int *p = gif_tag_ad(pkt, 4);
    p = put_ad(p, mdraw_sx(b->sbp) | mdraw_sx(b->sbw) << 16 | mdraw_sx(b->spsm) << 24 | mdraw_sx(b->dbp) << 32 |
                      mdraw_sx(b->dbw) << 48 | mdraw_sx(b->dpsm) << 56, GS_BITBLTBUF);
    p = put_ad(p, mdraw_sx(b->ssax) | mdraw_sx(b->ssay) << 16 | mdraw_sx(b->dsax) << 32 | mdraw_sx(b->dsay) << 48 |
                      mdraw_sx(b->dir) << 59, GS_TRXPOS);
    p = put_ad(p, mdraw_sx(b->rrw) | mdraw_sx(b->rrh) << 32, GS_TRXREG);
    put_ad(p, 2, GS_TRXDIR);
    void *ch = sceDmaGetChan(2);
    unsigned int flag = ps2_dma_chan_rd8(ch);
    ps2_dma_chan_wr8(ch, (flag & ~0x40u) | 0x40u);
    FlushCache(0);
    sceDmaSendN(ch, pkt, 5);
    ps2_dma_chan_wr8(ch, (ps2_dma_chan_rd8(ch) & ~0x40u) | ((flag >> 6) & 1) << 6);
    sceGsSyncPath(0, 0);
}

/* render target setup for the AC shadow: draw env (FRAME at inTBP, FBW 4, 256 x 256 scissor) sent through the SDK,
 * the target cleared with a black 256 x 256 sprite at max depth, then the env again with the scissor shrunk to
 * 1..254 (the shadow keeps a cleared one-pixel border). SyncDCache's end is sp + 0x5180 in the original (20 KB
 * above its own frame, kept). */
static void mdraw_shadow_env(unsigned int *env, unsigned long long frame, unsigned long long scissor)
{
    gif_tag_ad(env, 8);
    sceGsSetDefDrawEnv(env + 4, 0, 0x100, 0x100, 0, 0);
    memcpy(env + 16, &scissor, 8);   /* SCISSOR_1 (pair 3) */
    memcpy(env + 4, &frame, 8);      /* FRAME_1 (pair 0) */
    SyncDCache(env, (unsigned char *)env + 0x5100);
}

void DF_MDRAW_SetShadowFrame(int inTBP)
{
    PS2_FRAME(0x140);
    PS2_STACK(unsigned int[36], env, 0x80);
    PS2_STACK(int[2][4], xyz, 0x110);
    PS2_STACK(int[4], rgb, 0x130);
    PS2_RQ_SCOPE(RQ_K_SHADOW, 0, 0);   /* render tag (host only) */
    unsigned long long frame = mdraw_sx(inTBP >> 5) | 0x40000;
    mdraw_shadow_env(env, frame, 0x00ff000000ff0000ull);
    sceGsPutDrawEnv(env);
    rgb[0] = rgb[1] = rgb[2] = rgb[3] = 0;
    xyz[0][0] = 0x7800; xyz[0][1] = 0x7800; xyz[0][2] = 0xfffff0; xyz[0][3] = 0;
    xyz[1][0] = 0x8800; xyz[1][1] = 0x8800; xyz[1][2] = 0xfffff0; xyz[1][3] = 0;
    ps2_dma_wait_gif();
    DF_Draw2DSprite_Color(rgb, xyz, 0);
    mdraw_shadow_env(env, frame, 0x00fe000100fe0001ull);
    ps2_dma_wait_gif();
    sceGsPutDrawEnv(env);
    ps2_dma_wait_gif();
}

/* TM2 texture upload: TEX0 header at +0x10, pixels at +0x20, CLUT after the pixels (+ the size word at +4).
 * Pixels go up in slices of (0x4000 / 0x10000 / 0x20000 by PSM) / W rows; the source / TBP advance per slice uses
 * TBW * 64 as the row stride (not W: equal for widths that are multiples of 64). PSMs other than 0 / 0x13 / 0x1b /
 * 0x14 do not advance (would resend the same rows). 8-bit CLUTs: 16 x 16 (CSM1) or 256 x 1 (CSM2); 4-bit: 8 x 2 or
 * 16 x 1; PSMCT32 has none */
void DF_MDRAW_Tm2LoadImageNX(void *inTm2p, int inTBPOfst, int inCBPOfst)
{
    PS2_FRAME(0xd0);
    unsigned char *tm2 = (unsigned char *)inTm2p;
    unsigned long long tex0;
    memcpy(&tex0, tm2 + 0x10, 8);
    int tbp0 = *(unsigned short *)(tm2 + 0x10) & 0x3fff;
    unsigned int psm = (unsigned int)(tex0 >> 20) & 0x3f;
    PS2_RQ_SCOPE(RQ_K_UPLOAD, RQ_OBJID(RQ_OC_TEXTURE, tbp0 + inTBPOfst), psm);   /* render tag (host only) */
    unsigned char *src = tm2 + 0x20;
    int tbp = tbp0 + inTBPOfst;
    unsigned short w = (unsigned short)(1u << ((tex0 >> 26) & 0xf));
    unsigned short remain = (unsigned short)(1u << ((tex0 >> 30) & 0xf));
    int chunk = psm == 0x14 ? 0x20000 : (psm == 0x13 || psm == 0x1b) ? 0x10000 : 0x4000;
    unsigned short fp = (unsigned short)(chunk / (int)w);
    while (remain != 0) {
        unsigned short rows = (int)fp < (int)remain ? fp : remain;
        DF_MDRAW_LoadImage(src, (unsigned int)tbp, psm, 0, 0, w, rows);
        memcpy(&tex0, tm2 + 0x10, 8);
        psm = (unsigned int)(tex0 >> 20) & 0x3f;
        unsigned long long stride = ((tex0 >> 14) & 0x3f) << 6;   /* TBW * 64 */
        unsigned long long r = (unsigned long long)(long long)(int)rows;
        if (psm == 0) {
            unsigned long long m = r * stride;
            tbp += (int)(m >> 6);
            src += (int)m * 4;
        } else if (psm == 0x13 || psm == 0x1b) {
            tbp += (int)((r * stride >> 2) >> 6);
            src += (int)(r * (stride >> 2)) * 4;
        } else if (psm == 0x14) {
            tbp += (int)((r * stride >> 3) >> 6);
            src += (int)(r * (stride >> 3)) * 4;
        }
        remain = (unsigned short)(remain - rows);
    }
    memcpy(&tex0, tm2 + 0x10, 8);
    int cbp = (int)(((tex0 << 13) >> 50) + (unsigned long long)(long long)inCBPOfst);
    unsigned char *clut = tm2 + *(unsigned int *)(tm2 + 4) + 0x20;
    unsigned int b16 = tm2[0x16];
    unsigned int cpsm = (b16 >> 3) & 0xf;
    unsigned int csm = b16 >> 7;
    if (psm == 0x13 || psm == 0x1b) {
        if (csm == 0) DF_MDRAW_LoadImage(clut, (unsigned int)cbp, cpsm, 0, 0, 0x10, 0x10);
        else DF_MDRAW_LoadImage(clut, (unsigned int)cbp, cpsm, 0, 0, 0x100, 1);
    } else if (psm == 0x14) {
        if (csm == 0) DF_MDRAW_LoadImage(clut, (unsigned int)cbp, cpsm, 0, 0, 8, 2);
        else DF_MDRAW_LoadImage(clut, (unsigned int)cbp, cpsm, 0, 0, 0x10, 1);
    }
}

/* ---- window decorations, part A (classes M2 / M3) ----
 * Every piece is a rect + uv quad on the stack (the original's slots: DF_MDRAW_DrawTexSprite and the other packet
 * builders were interpreted when this part was written) drawn with one DrawTexSprite call. */

static inline void mdraw_quad(int *d, int a, int b, int c, int e)
{
    d[0] = a; d[1] = b; d[2] = c; d[3] = e;
}

static inline void mdraw_rect(struct stMenuRect *r, int x, int y, int w, int h)
{
    r->x = x; r->y = y; r->w = w; r->h = h;
}

/* 32-bit mult low word (MWCC's mult / mult1) */
static inline int mdraw_mul(int a, int b)
{
    return (int)((unsigned int)a * (unsigned int)b);
}

/* soft round spot (texture 0x3ec): the caller's colour multiplied into a dim grey (0x80, 0x80, 0x80, 0x3c) */
void DF_MDRAW_DrawSpot(struct stMenuRect *pRect, struct struct_76b810ec *pColor, int depth)
{
    PS2_FRAME(0x60);
    PS2_STACK(int[4], uv, 0x40);
    PS2_STACK(struct_13efd7f4, texinfo, 0x30);
    PS2_STACK(struct_76b810ec, col, 0x5c);

    mdraw_quad(uv, 0xd0, 0x22, 0x100, 0x52);
    col.r = 0x80; col.g = 0x80; col.b = 0x80; col.a = 0x3c;
    MenuUtil_MulColor(&col, &col, pColor);
    if (MenuTex_StandbyTexture(0x3ec, &texinfo)) DF_MDRAW_DrawTexSprite(&texinfo, pRect, uv, &col, depth);
}

/* the two 12 px end caps of the light cursor bar (texture 0x3ec; both use the same uv) */
void DF_MDRAW_DrawLightCursor(struct stMenuRect *pRect, struct struct_76b810ec *pColor, int depth)
{
    PS2_FRAME(0x80);
    PS2_STACK(struct stMenuRect, ra, 0x60);
    PS2_STACK(struct stMenuRect, rb, 0x50);
    PS2_STACK(int[4], uva, 0x40);
    PS2_STACK(int[4], uvb, 0x30);
    PS2_STACK(struct_13efd7f4, texinfo, 0x70);

    mdraw_rect(&ra, pRect->x, pRect->y, 0xc, pRect->h);
    mdraw_rect(&rb, pRect->x + pRect->w - 0xc, pRect->y, 0xc, pRect->h);
    mdraw_quad(uva, 0xee, 0, 0xfa, 0x14);
    mdraw_quad(uvb, 0xee, 0, 0xfa, 0x14);
    if (!MenuTex_StandbyTexture(0x3ec, &texinfo)) return;
    DF_MDRAW_DrawTexSprite(&texinfo, &ra, uva, pColor, depth);
    DF_MDRAW_DrawTexSprite(&texinfo, &rb, uvb, pColor, depth);
}

/* three-piece label (texture 0x3e9): 4 px caps around a stretched middle; under 8 px the caps split the width (the
 * middle piece stays at x + 4 with width 0). uvt = the three uv quads of the variant */
static void mdraw_simple_label3(struct stMenuRect *pRect, struct struct_76b810ec *pColor, int depth,
                                const int (*uvt)[4], struct stMenuRect *ra, struct stMenuRect *rb,
                                struct stMenuRect *rc, int *uva, int *uvb, int *uvc, struct_13efd7f4 *texinfo)
{
    int w = pRect->w, x = pRect->x, y = pRect->y, h = pRect->h;
    int cap0, mid, cap2;
    if (w < 8) {
        cap0 = w / 2;
        mid = 0;
        cap2 = (w + 1) / 2;
    } else {
        cap0 = 4;
        mid = w - 8;
        cap2 = 4;
    }
    mdraw_rect(ra, x, y, cap0, h);
    mdraw_rect(rb, x + 4, y, mid, h);
    mdraw_rect(rc, x + w - 4, y, cap2, h);
    mdraw_quad(uva, uvt[0][0], uvt[0][1], uvt[0][2], uvt[0][3]);
    mdraw_quad(uvb, uvt[1][0], uvt[1][1], uvt[1][2], uvt[1][3]);
    mdraw_quad(uvc, uvt[2][0], uvt[2][1], uvt[2][2], uvt[2][3]);
    if (!MenuTex_StandbyTexture(0x3e9, texinfo)) return;
    DF_MDRAW_DrawTexSprite(texinfo, ra, uva, pColor, depth);
    DF_MDRAW_DrawTexSprite(texinfo, rb, uvb, pColor, depth);
    DF_MDRAW_DrawTexSprite(texinfo, rc, uvc, pColor, depth);
}

/* OLMENU 0x1edcdb0 / 0x1edcd80 / 0x1edccd0 */
static const int mdraw_uv_label_upr[3][4] = {{85, 1, 89, 22}, {91, 1, 95, 22}, {89, 1, 85, 22}};
static const int mdraw_uv_label_btm[3][4] = {{73, 1, 77, 21}, {79, 1, 83, 21}, {77, 1, 73, 21}};
static const int mdraw_uv_label[3][4] = {{61, 1, 65, 23}, {67, 1, 71, 23}, {65, 1, 61, 23}};

void DF_MDRAW_DrawSimpleLabelUpr(struct stMenuRect *pRect, struct struct_76b810ec *pColor, int depth)
{
    PS2_FRAME(0xa0);
    PS2_STACK(struct stMenuRect, ra, 0x80);
    PS2_STACK(struct stMenuRect, rb, 0x70);
    PS2_STACK(struct stMenuRect, rc, 0x60);
    PS2_STACK(int[4], uva, 0x50);
    PS2_STACK(int[4], uvb, 0x40);
    PS2_STACK(int[4], uvc, 0x30);
    PS2_STACK(struct_13efd7f4, texinfo, 0x90);
    mdraw_simple_label3(pRect, pColor, depth, mdraw_uv_label_upr, &ra, &rb, &rc, uva, uvb, uvc, &texinfo);
}

void DF_MDRAW_DrawSimpleLabelBtm(struct stMenuRect *pRect, struct struct_76b810ec *pColor, int depth)
{
    PS2_FRAME(0xa0);
    PS2_STACK(struct stMenuRect, ra, 0x80);
    PS2_STACK(struct stMenuRect, rb, 0x70);
    PS2_STACK(struct stMenuRect, rc, 0x60);
    PS2_STACK(int[4], uva, 0x50);
    PS2_STACK(int[4], uvb, 0x40);
    PS2_STACK(int[4], uvc, 0x30);
    PS2_STACK(struct_13efd7f4, texinfo, 0x90);
    mdraw_simple_label3(pRect, pColor, depth, mdraw_uv_label_btm, &ra, &rb, &rc, uva, uvb, uvc, &texinfo);
}

void DF_MDRAW_DrawSimpleLabel(struct stMenuRect *pRect, struct struct_76b810ec *pColor, int depth)
{
    PS2_FRAME(0xa0);
    PS2_STACK(struct stMenuRect, ra, 0x80);
    PS2_STACK(struct stMenuRect, rb, 0x70);
    PS2_STACK(struct stMenuRect, rc, 0x60);
    PS2_STACK(int[4], uva, 0x50);
    PS2_STACK(int[4], uvb, 0x40);
    PS2_STACK(int[4], uvc, 0x30);
    PS2_STACK(struct_13efd7f4, texinfo, 0x90);
    mdraw_simple_label3(pRect, pColor, depth, mdraw_uv_label, &ra, &rb, &rc, uva, uvb, uvc, &texinfo);
}

/* two-piece labels (texture 0x3e9): L = a 4 px left cap + the rest, R = the rest + a 4 px right cap */
void DF_MDRAW_DrawSimpleLabelL(struct stMenuRect *pRect, struct struct_76b810ec *pColor, int depth)
{
    PS2_FRAME(0x80);
    PS2_STACK(struct stMenuRect, ra, 0x60);
    PS2_STACK(struct stMenuRect, rb, 0x50);
    PS2_STACK(int[4], uva, 0x40);
    PS2_STACK(int[4], uvb, 0x30);
    PS2_STACK(struct_13efd7f4, texinfo, 0x70);

    mdraw_rect(&ra, pRect->x, pRect->y, 4, pRect->h);
    mdraw_rect(&rb, pRect->x + 4, pRect->y, pRect->w - 4, pRect->h);
    mdraw_quad(uva, 59, 1, 55, 27);
    mdraw_quad(uvb, 49, 1, 53, 27);
    if (!MenuTex_StandbyTexture(0x3e9, &texinfo)) return;
    DF_MDRAW_DrawTexSprite(&texinfo, &ra, uva, pColor, depth);
    DF_MDRAW_DrawTexSprite(&texinfo, &rb, uvb, pColor, depth);
}

void DF_MDRAW_DrawSimpleLabelR(struct stMenuRect *pRect, struct struct_76b810ec *pColor, int depth)
{
    PS2_FRAME(0x80);
    PS2_STACK(struct stMenuRect, ra, 0x60);
    PS2_STACK(struct stMenuRect, rb, 0x50);
    PS2_STACK(int[4], uva, 0x40);
    PS2_STACK(int[4], uvb, 0x30);
    PS2_STACK(struct_13efd7f4, texinfo, 0x70);

    mdraw_rect(&ra, pRect->x, pRect->y, pRect->w - 4, pRect->h);
    mdraw_rect(&rb, pRect->x + pRect->w - 4, pRect->y, 4, pRect->h);
    mdraw_quad(uva, 49, 1, 53, 27);
    mdraw_quad(uvb, 55, 1, 59, 27);
    if (!MenuTex_StandbyTexture(0x3e9, &texinfo)) return;
    DF_MDRAW_DrawTexSprite(&texinfo, &ra, uva, pColor, depth);
    DF_MDRAW_DrawTexSprite(&texinfo, &rb, uvb, pColor, depth);
}

/* the tag window sprite: CLAMP_1 = 5 (REGION_CLAMP u, CLAMP v), ALPHA_1 from the five blend bytes (A, B, C, D, FIX),
 * then the sprite. The sprite is drawn even when StandbyTexture fails (result ignored); inShape is unused */
void DF_MDRAW_NexusDrawTagVertex4(int inShape, struct stMenuRect *inRectp, struct struct_76b810ec *inColp,
                                  unsigned int inDepth, int *InUV, int TexID, unsigned char *pInBlend)
{
    PS2_FRAME(0x80);
    PS2_STACK(struct_13efd7f4, texinfo, 0x70);
    (void)inShape;

    MenuTex_StandbyTexture(TexID, &texinfo);
    DF_MDRAW_KickReg(8, 5);
    unsigned long long alpha = (unsigned long long)pInBlend[0] | (unsigned long long)pInBlend[1] << 2 |
                               (unsigned long long)pInBlend[2] << 4 | (unsigned long long)pInBlend[3] << 6 |
                               (unsigned long long)pInBlend[4] << 32;
    DF_MDRAW_KickReg(0x42, alpha);
    DF_MDRAW_DrawTexSprite(&texinfo, inRectp, InUV, inColp, inDepth);
}

/* 3 x 3 patch box: column widths = the block widths with the rest in the middle (a rect narrower than both blocks
 * splits its width between them in proportion, middle 0), rows likewise; cell (row, col) uses uv
 * {u, v, u + uw[col], v + vh[row]} (negative uw / vh mirror the right / bottom blocks). bHole skips the centre */
void __Draw9Box2(int texid, struct stMenuRect *pRect, struct struct_76b810ec *pColor, int (*u)[3], int (*v)[3],
                 int *uw, int *vh, int *blockw, int *blockh, int bHole)
{
    PS2_FRAME(0x140);
    PS2_STACK(struct_13efd7f4, texinfo, 0x110);
    PS2_STACK(int[3], cw, 0x130);
    PS2_STACK(int[3], ch, 0x120);
    PS2_STACK(struct stMenuRect, r, 0x100);
    PS2_STACK(int[4], uv, 0xf0);

    if (!MenuTex_StandbyTexture(texid, &texinfo)) return;
    cw[0] = blockw[0];
    cw[1] = pRect->w - (blockw[1] + blockw[0]);
    cw[2] = blockw[1];
    if (pRect->w < blockw[0] + blockw[1]) {
        cw[1] = 0;
        cw[0] = ps2_div32(mdraw_mul(pRect->w, blockw[0]), blockw[0] + blockw[1]);
        cw[2] = pRect->w - ps2_div32(mdraw_mul(pRect->w, blockw[0]), blockw[0] + blockw[1]);
    }
    ch[0] = blockh[0];
    ch[1] = pRect->h - (blockh[1] + blockh[0]);
    ch[2] = blockh[1];
    if (pRect->h < blockh[0] + blockh[1]) {
        ch[1] = 0;
        ch[0] = ps2_div32(mdraw_mul(pRect->h, blockh[0]), blockh[0] + blockh[1]);
        ch[2] = pRect->h - ps2_div32(mdraw_mul(pRect->h, blockh[0]), blockh[0] + blockh[1]);
    }
    int y = pRect->y;
    for (int row = 0; row < 3; row++) {
        int x = pRect->x;
        for (int col = 0; col < 3; col++) {
            if (!bHole || col != 1 || row != 1) {
                mdraw_rect(&r, x, y, cw[col], ch[row]);
                mdraw_quad(uv, u[row][col], v[row][col], uw[col] + u[row][col], vh[row] + v[row][col]);
                DF_MDRAW_DrawTexSprite(&texinfo, &r, uv, pColor, 0);
            }
            x += cw[col];
        }
        y += ch[row];
    }
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper (gen_bridge stops at 8 int args); blockh / bHole are the caller's stack args at
 * sp+0 / sp+8 */
static void bw____Draw9Box2(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    __Draw9Box2(c->gpr[A0].sw[0], (struct stMenuRect *)bridge_ptr(c->gpr[A1].uw[0]),
                (struct struct_76b810ec *)bridge_ptr(c->gpr[A2].uw[0]), (int (*)[3])bridge_ptr(c->gpr[A3].uw[0]),
                (int (*)[3])bridge_ptr(c->gpr[T0].uw[0]), (int *)bridge_ptr(c->gpr[T1].uw[0]),
                (int *)bridge_ptr(c->gpr[T2].uw[0]), (int *)bridge_ptr(c->gpr[T3].uw[0]), (int *)bridge_ptr(rd32(sp)),
                (int)rd32(sp + 8));
}
#endif

/* the 9-patch frames: tables copied to the stack (u[3][3], v[3][3], uw[3], vh[3]), block sizes, texture 0x3e9 */
static void mdraw_9box_tables(int (*u)[3], int (*v)[3], int *uw, int *vh, const int *tu, const int *tv,
                              const int *tuw, const int *tvh)
{
    memcpy(u, tu, 0x24);
    memcpy(v, tv, 0x24);
    memcpy(uw, tuw, 0xc);
    memcpy(vh, tvh, 0xc);
}

/* OLMENU 0x1edd0d0 / 0x1edd100 / 0x1edd128 / 0x1edd138 */
static const int mdraw_gene_u[9] = {64, 74, 72, 64, 74, 72, 64, 74, 72};
static const int mdraw_gene_v[9] = {72, 72, 72, 82, 82, 82, 80, 80, 80};
static const int mdraw_gene_uw[3] = {8, 8, -8};
static const int mdraw_gene_vh[3] = {8, 8, -8};

/* filled board with 8 px borders, alpha at 3/4 of the caller's */
void DF_MDRAW_DrawGeneBoard(struct stMenuRect *pRect, struct struct_76b810ec *pColor)
{
    PS2_FRAME(0xb0);
    PS2_STACK(int[3][3], u, 0x50);
    PS2_STACK(int[3][3], v, 0x20);
    PS2_STACK(int[3], uw, 0xa0);
    PS2_STACK(int[3], vh, 0x90);
    PS2_STACK(int[2], blockw, 0x88);
    PS2_STACK(int[2], blockh, 0x80);
    PS2_STACK(struct_76b810ec, col, 0xac);

    mdraw_9box_tables(u, v, uw, vh, mdraw_gene_u, mdraw_gene_v, mdraw_gene_uw, mdraw_gene_vh);
    col = *pColor;
    col.a = (unsigned char)((col.a * 3 * 32) / 128);
    blockw[0] = 8; blockw[1] = 8;
    blockh[0] = 8; blockh[1] = 8;
    __Draw9Box2(0x3e9, pRect, &col, u, v, uw, vh, blockw, blockh, 0);
}

/* OLMENU 0x1edcee0 / 0x1edcf10 / 0x1edcf38 / 0x1edcf48 */
static const int mdraw_wakubtm_u[9] = {87, 111, 109, 97, 0, 119, 97, 121, 119};
static const int mdraw_wakubtm_v[9] = {47, 47, 47, 17, 0, 17, 15, 15, 15};
static const int mdraw_wakubtm_uw[3] = {22, 4, -22};
static const int mdraw_wakubtm_vh[3] = {12, 4, -14};

/* AC frame, bottom variant: hollow, blocks 22 x {12, 14} */
void DF_MDRAW_DrawAcWakuBtm(struct stMenuRect *pRect, struct struct_76b810ec *pColor)
{
    PS2_FRAME(0xb0);
    PS2_STACK(int[3][3], u, 0x50);
    PS2_STACK(int[3][3], v, 0x20);
    PS2_STACK(int[3], uw, 0x90);
    PS2_STACK(int[3], vh, 0x80);
    PS2_STACK(int[2], blockw, 0xa8);
    PS2_STACK(int[2], blockh, 0xa0);

    blockw[0] = 0x16; blockw[1] = 0x16;
    blockh[0] = 0xc; blockh[1] = 0xe;
    mdraw_9box_tables(u, v, uw, vh, mdraw_wakubtm_u, mdraw_wakubtm_v, mdraw_wakubtm_uw, mdraw_wakubtm_vh);
    __Draw9Box2(0x3e9, pRect, pColor, u, v, uw, vh, blockw, blockh, 1);
}

/* AcWakuC / A / B: hollow, blocks bw x 14 */
static void mdraw_acwaku(struct stMenuRect *pRect, struct struct_76b810ec *pColor, int bw, const int *tu,
                         const int *tv, const int *tuw, const int *tvh, int (*u)[3], int (*v)[3], int *uw, int *vh,
                         int *blockw, int *blockh)
{
    mdraw_9box_tables(u, v, uw, vh, tu, tv, tuw, tvh);
    blockw[0] = bw; blockw[1] = bw;
    blockh[0] = 0xe; blockh[1] = 0xe;
    __Draw9Box2(0x3e9, pRect, pColor, u, v, uw, vh, blockw, blockh, 1);
}

/* OLMENU 0x1edce60.. (C), 0x1edcde0.. (A), 0x1edcd00.. (B): u, v, uw, vh */
static const int mdraw_wakuc_u[9] = {87, 101, 99, 87, 0, 99, 87, 101, 99};
static const int mdraw_wakuc_v[9] = {25, 25, 25, 41, 0, 41, 39, 39, 39};
static const int mdraw_wakuc_uw[3] = {12, 4, -12};
static const int mdraw_wakuc_vh[3] = {14, 4, -14};
static const int mdraw_wakua_u[9] = {61, 81, 79, 61, 0, 79, 61, 81, 79};
static const int mdraw_wakua_v[9] = {25, 25, 25, 41, 0, 41, 39, 39, 39};
static const int mdraw_wakua_uw[3] = {18, 4, -18};
static const int mdraw_wakua_vh[3] = {14, 4, -14};
static const int mdraw_wakub_u[9] = {97, 121, 119, 97, 0, 119, 97, 121, 119};
static const int mdraw_wakub_v[9] = {1, 1, 1, 17, 0, 17, 15, 15, 15};
static const int mdraw_wakub_uw[3] = {22, 4, -22};
static const int mdraw_wakub_vh[3] = {14, 4, -14};

void DF_MDRAW_DrawAcWakuC(struct stMenuRect *pRect, struct struct_76b810ec *pColor)
{
    PS2_FRAME(0xb0);
    PS2_STACK(int[3][3], u, 0x50);
    PS2_STACK(int[3][3], v, 0x20);
    PS2_STACK(int[3], uw, 0xa0);
    PS2_STACK(int[3], vh, 0x90);
    PS2_STACK(int[2], blockw, 0x88);
    PS2_STACK(int[2], blockh, 0x80);
    mdraw_acwaku(pRect, pColor, 0xc, mdraw_wakuc_u, mdraw_wakuc_v, mdraw_wakuc_uw, mdraw_wakuc_vh,
                 u, v, uw, vh, blockw, blockh);
}

void DF_MDRAW_DrawAcWakuA(struct stMenuRect *pRect, struct struct_76b810ec *pColor)
{
    PS2_FRAME(0xb0);
    PS2_STACK(int[3][3], u, 0x50);
    PS2_STACK(int[3][3], v, 0x20);
    PS2_STACK(int[3], uw, 0xa0);
    PS2_STACK(int[3], vh, 0x90);
    PS2_STACK(int[2], blockw, 0x88);
    PS2_STACK(int[2], blockh, 0x80);
    mdraw_acwaku(pRect, pColor, 0x12, mdraw_wakua_u, mdraw_wakua_v, mdraw_wakua_uw, mdraw_wakua_vh,
                 u, v, uw, vh, blockw, blockh);
}

void DF_MDRAW_DrawAcWakuB(struct stMenuRect *pRect, struct struct_76b810ec *pColor)
{
    PS2_FRAME(0xb0);
    PS2_STACK(int[3][3], u, 0x50);
    PS2_STACK(int[3][3], v, 0x20);
    PS2_STACK(int[3], uw, 0xa0);
    PS2_STACK(int[3], vh, 0x90);
    PS2_STACK(int[2], blockw, 0x88);
    PS2_STACK(int[2], blockh, 0x80);
    mdraw_acwaku(pRect, pColor, 0x16, mdraw_wakub_u, mdraw_wakub_v, mdraw_wakub_uw, mdraw_wakub_vh,
                 u, v, uw, vh, blockw, blockh);
}

/* report label: a 40 px high three-piece plate (texture 0x3e9, alpha 3/4; the rect's height is not used) and the
 * caption string 0x189f2 / 0x189f3 (labeltype) at (x + 5, y + 3) in the caller's colour; depth is unused */
void DF_MDRAW_DrawReport1(struct stMenuRect *pRect, struct struct_76b810ec *pColor, int depth, int labeltype)
{
    PS2_FRAME(0x340);
    PS2_STACK(struct_76b810ec, col, 0x33c);
    PS2_STACK(struct stMenuRect, ra, 0x310);
    PS2_STACK(struct stMenuRect, rb, 0x300);
    PS2_STACK(struct stMenuRect, rc, 0x2f0);
    PS2_STACK(int[4], uva, 0x2e0);
    PS2_STACK(int[4], uvb, 0x2d0);
    PS2_STACK(int[4], uvc, 0x2c0);
    PS2_STACK(struct_13efd7f4, texinfo, 0x320);
    PS2_STACK(struct_acb9a91f, attr, 0x90);
    (void)depth;

    int x = pRect->x, y = pRect->y, w = pRect->w;
    col = *pColor;
    mdraw_rect(&ra, x, y, 4, 40);
    mdraw_rect(&rb, x + 4, y, w - 8, 40);
    mdraw_rect(&rc, x + w - 4, y, 4, 40);
    mdraw_quad(uva, 33, 72, 37, 112);
    mdraw_quad(uvb, 39, 72, 43, 112);
    mdraw_quad(uvc, 37, 72, 33, 112);
    if (!MenuTex_StandbyTexture(0x3e9, &texinfo)) return;
    col.a = (unsigned char)((col.a * 3 * 32) / 128);
    DF_MDRAW_DrawTexSprite(&texinfo, &ra, uva, &col, 0);
    DF_MDRAW_DrawTexSprite(&texinfo, &rb, uvb, &col, 0);
    DF_MDRAW_DrawTexSprite(&texinfo, &rc, uvc, &col, 0);
    memset(&attr, 0, sizeof(attr));
    DF_MFONT_SetFontAttr(&attr, 0);
    attr.x = (short)(x + 5);
    attr.y = (short)(y + 3);
    attr.strp = DF_MSTR_GetName(MenuMng.language, 0, labeltype ? 0x189f3 : 0x189f2);
    attr.col = *pColor;
    DF_MFONT_Draw(&attr, 0);
}

/* ---- window decorations, part B (class M3; none runs in the replays: asm-verified) ---- */

/* report label, wide variant: a 40 px high seven-piece plate (texture 0x3e9, alpha 3/4; widths 4 / 3 / 12 / stretch /
 * 12 / 3 / 4 from the rodata templates at OLMENU 0x1edd000, each piece starts where the previous one ends) and the
 * caption 0x189f2 / 0x189f3 (labeltype) at (x + 5, y + 3) in the caller's colour; the rect's height is not used */
void DF_MDRAW_DrawReport2(struct stMenuRect *pRect, struct struct_76b810ec *pColor, int depth, int labeltype)
{
    PS2_FRAME(0x3d0);
    PS2_STACK(struct_13efd7f4, texinfo, 0x3b0);
    PS2_STACK(struct_76b810ec, col, 0x3cc);
    PS2_STACK(struct stMenuRect[7], r, 0x340);   /* rect7 .. rect1 = r[0] .. r[6] (rect1 at 0x3a0) */
    PS2_STACK(int[7][4], uv, 0x2d0);             /* uv7 .. uv1 */
    PS2_STACK(struct_acb9a91f, attr, 0xa0);
    static const int widths[7] = {4, 3, 12, 0, 12, 3, 4};
    static const int uvs[7][4] = {{33, 72, 37, 112}, {39, 72, 43, 112}, {44, 72, 56, 112}, {58, 72, 62, 112},
                                  {56, 72, 44, 112}, {43, 72, 39, 112}, {37, 72, 33, 112}};

    int x = pRect->x, y = pRect->y, w = pRect->w;
    col = *pColor;
    col.a = (unsigned char)((col.a * 3 * 32) / 128);
    if (!MenuTex_StandbyTexture(0x3e9, &texinfo)) return;
    int px = x;
    for (int i = 0; i < 7; i++) {
        struct stMenuRect *ri = &r[6 - i];
        mdraw_rect(ri, px, y, i == 3 ? w - 0x26 : widths[i], 40);
        px = ri->x + ri->w;
        mdraw_quad(uv[6 - i], uvs[i][0], uvs[i][1], uvs[i][2], uvs[i][3]);
    }
    for (int i = 0; i < 7; i++) DF_MDRAW_DrawTexSprite(&texinfo, &r[6 - i], uv[6 - i], &col, depth);
    memset(&attr, 0, sizeof(attr));
    DF_MFONT_SetFontAttr(&attr, 0);
    attr.x = (short)(x + 5);
    attr.y = (short)(y + 3);
    attr.strp = DF_MSTR_GetName(MenuMng.language, 0, labeltype ? 0x189f3 : 0x189f2);
    attr.col = *pColor;
    DF_MFONT_Draw(&attr, 0);
}

/* horizontal strip of pieces of texture 0x3e9 at the rect's y / h, each starting where the previous one ends; the
 * piece with width 0 in the table stretches over pRect->w - fixed. Piece i goes to r[n - 1 - i] (stack order) */
static void mdraw_strip(struct stMenuRect *pRect, int n, const int *widths, int fixed, struct stMenuRect *r)
{
    int px = pRect->x;
    for (int i = 0; i < n; i++) {
        struct stMenuRect *ri = &r[n - 1 - i];
        mdraw_rect(ri, px, pRect->y, widths[i] ? widths[i] : pRect->w - fixed, pRect->h);
        px = ri->x + ri->w;
    }
}

/* string-select bar, RE variant: 7 pieces (OLMENU 0x1edcc10..); the sixth piece reuses the first one's uv */
void DF_MDRAW_DrawStrSelFixRE(struct stMenuRect *pRect, struct struct_76b810ec *pColor, int depth)
{
    PS2_FRAME(0x180);
    PS2_STACK(struct_13efd7f4, texinfo, 0x170);
    PS2_STACK(struct stMenuRect[7], r, 0x100);   /* rect7 .. rect1 */
    PS2_STACK(int[6][4], uv, 0xa0);              /* uv7, uv5, uv4, uv3, uv2, uv1 */
    static const int widths[7] = {21, 12, 0, 12, 22, 6, 4};
    static const int uvs[6][4] = {{75, 47, 79, 71}, {51, 47, 39, 71}, {33, 47, 37, 71}, {39, 47, 51, 71},
                                  {51, 47, 73, 71}, {81, 47, 85, 71}};
    static const int uvi[7] = {5, 4, 3, 2, 1, 5, 0};

    mdraw_strip(pRect, 7, widths, 0x4d, r);
    for (int i = 0; i < 6; i++) mdraw_quad(uv[5 - i], uvs[i][0], uvs[i][1], uvs[i][2], uvs[i][3]);
    if (!MenuTex_StandbyTexture(0x3e9, &texinfo)) return;
    for (int i = 0; i < 7; i++) DF_MDRAW_DrawTexSprite(&texinfo, &r[6 - i], uv[uvi[i]], pColor, depth);
}

/* string-select bar: 6 pieces (OLMENU 0x1edcb60..) */
void DF_MDRAW_DrawStrSelFixR(struct stMenuRect *pRect, struct struct_76b810ec *pColor, int depth)
{
    PS2_FRAME(0x170);
    PS2_STACK(struct_13efd7f4, texinfo, 0x160);
    PS2_STACK(struct stMenuRect[6], r, 0x100);   /* rect6 .. rect1 */
    PS2_STACK(int[6][4], uv, 0xa0);              /* uv6 .. uv1 */
    static const int widths[6] = {21, 12, 0, 12, 4, 4};
    static const int uvs[6][4] = {{75, 47, 79, 71}, {51, 47, 39, 71}, {33, 47, 37, 71}, {39, 47, 51, 71},
                                  {75, 47, 79, 71}, {81, 47, 85, 71}};

    mdraw_strip(pRect, 6, widths, 0x35, r);
    for (int i = 0; i < 6; i++) mdraw_quad(uv[5 - i], uvs[i][0], uvs[i][1], uvs[i][2], uvs[i][3]);
    if (!MenuTex_StandbyTexture(0x3e9, &texinfo)) return;
    for (int i = 0; i < 6; i++) DF_MDRAW_DrawTexSprite(&texinfo, &r[5 - i], uv[5 - i], pColor, depth);
}

/* M1 help bar: three fixed pieces at (0, 404), 20 + 600 + 20 x 44 (texture 0x3e9); pRect is not used (protofix) */
void DF_MDRAW_DrawM1Help(struct stMenuRect *pRect, struct struct_76b810ec *pColor, int depth)
{
    PS2_FRAME(0xa0);
    PS2_STACK(struct_13efd7f4, texinfo, 0x90);
    PS2_STACK(struct stMenuRect[3], r, 0x60);    /* rect3 .. rect1 */
    PS2_STACK(int[3][4], uv, 0x30);              /* uv3 .. uv1 */
    (void)pRect;

    mdraw_rect(&r[2], 0, 404, 20, 44);
    mdraw_rect(&r[1], 20, 404, 600, 44);
    mdraw_rect(&r[0], 620, 404, 20, 44);
    mdraw_quad(uv[2], 1, 1, 21, 45);
    mdraw_quad(uv[1], 23, 1, 27, 45);
    mdraw_quad(uv[0], 21, 1, 1, 45);
    if (!MenuTex_StandbyTexture(0x3e9, &texinfo)) return;
    for (int i = 2; i >= 0; i--) DF_MDRAW_DrawTexSprite(&texinfo, &r[i], uv[i], pColor, depth);
}

/* VS window bars: 20 px caps around a stretched middle, height ht, with the caller's texture (NULL: nothing) */
static void mdraw_vs_bar(struct_13efd7f4 *tex_info, struct stMenuRect *inRectp, struct struct_76b810ec *inColp,
                         unsigned int inDepth, int ht, const int (*uvt)[4], struct stMenuRect *rect, int (*uv)[4])
{
    memcpy(uv, uvt, 0x30);
    if (!tex_info) return;
    int w = inRectp->w;
    mdraw_rect(&rect[0], inRectp->x, inRectp->y, 0x14, ht);
    mdraw_rect(&rect[1], inRectp->x + 0x14, inRectp->y, w - 0x28, ht);
    mdraw_rect(&rect[2], inRectp->x + w - 0x14, inRectp->y, 0x14, ht);
    for (int ii = 0; ii < 3; ii++) DF_MDRAW_DrawTexSprite(tex_info, &rect[ii], uv[ii], inColp, inDepth);
}

/* OLMENU 0x1edd160 / 0x1edca70 */
static const int mdraw_uv_lightvs[3][4] = {{0, 24, 20, 48}, {20, 24, 40, 48}, {104, 24, 124, 48}};
static const int mdraw_uv_genevs[3][4] = {{0, 0, 20, 18}, {20, 0, 40, 18}, {96, 0, 116, 18}};

void DF_MDRAW_DrawLightWindowVS(struct_13efd7f4 *tex_info, struct stMenuRect *inRectp, struct struct_76b810ec *inColp,
                                unsigned int inDepth)
{
    PS2_FRAME(0xd0);
    PS2_STACK(struct stMenuRect[3], edge_rect, 0xa0);
    PS2_STACK(int[3][4], edge_uv, 0x70);
    mdraw_vs_bar(tex_info, inRectp, inColp, inDepth, 0x18, mdraw_uv_lightvs, edge_rect, edge_uv);
}

void DF_MDRAW_DrawGeneralWindowVS(struct_13efd7f4 *tex_info, struct stMenuRect *inRectp,
                                  struct struct_76b810ec *inColp, unsigned int inDepth)
{
    PS2_FRAME(0xd0);
    PS2_STACK(struct stMenuRect[3], edge_rect, 0xa0);
    PS2_STACK(int[3][4], edge_uv, 0x70);
    mdraw_vs_bar(tex_info, inRectp, inColp, inDepth, 0x12, mdraw_uv_genevs, edge_rect, edge_uv);
}

/* 8-cell frame (4 corners c x c + 4 edges, no centre) of a rect at least c x c: rect[0..2] top row, [3] / [4] the
 * left / right edges, [5..7] bottom row */
static void mdraw_frame8(struct stMenuRect *in, int c, struct stMenuRect *rect)
{
    int w = in->w, h = in->h;
    if (w < c) w = c;
    if (h < c) h = c;
    int x = in->x, y = in->y;
    mdraw_rect(&rect[0], x, y, c, c);
    mdraw_rect(&rect[1], x + c, y, w - 2 * c, c);
    mdraw_rect(&rect[2], x + w - c, y, c, c);
    mdraw_rect(&rect[3], x, y + c, c, h - 2 * c);
    mdraw_rect(&rect[4], x + w - c, y + c, c, h - 2 * c);
    mdraw_rect(&rect[5], x, y + h - c, c, c);
    mdraw_rect(&rect[6], x + c, y + h - c, w - 2 * c, c);
    mdraw_rect(&rect[7], x + w - c, y + h - c, c, c);
}

/* RE2 light frame (16 px corners): draws with a NULL texture info, so DrawTexSprite returns at once - the frame is
 * never drawn */
void DF_MDRAW_DrawLightWindowRE2(struct stMenuRect *inRectp, struct struct_76b810ec *inColp, unsigned int inDepth)
{
    PS2_FRAME(0x170);
    PS2_STACK(struct stMenuRect[8], rect, 0xf0);
    PS2_STACK(int[8][4], uv, 0x70);
    static const int uvt[8][4] = {{64, 200, 80, 216}, {84, 200, 108, 216}, {128, 176, 144, 192},
                                  {64, 220, 80, 235}, {112, 220, 128, 235}, {64, 239, 80, 255},
                                  {84, 239, 108, 255}, {128, 199, 144, 215}};

    memcpy(uv, uvt, sizeof(uvt));
    mdraw_frame8(inRectp, 0x10, rect);
    for (int i = 0; i < 8; i++)
        if (rect[i].w > 0 && rect[i].h > 0) DF_MDRAW_DrawTexSprite(NULL, &rect[i], uv[i], inColp, inDepth);
}

/* light frames (20 px corners): texture 0 (LightWindow) / 0x15 (LightWindow2), looked up per drawn cell */
static void mdraw_light_frame(struct stMenuRect *inRectp, struct struct_76b810ec *inColp, unsigned int inDepth,
                              int texid, const int (*uvt)[4], struct stMenuRect *rect, int (*uv)[4],
                              struct_13efd7f4 *texinfo)
{
    memcpy(uv, uvt, 0x80);
    mdraw_frame8(inRectp, 0x14, rect);
    for (int i = 0; i < 8; i++)
        if (rect[i].w > 0 && rect[i].h > 0 && MenuTex_StandbyTexture(texid, texinfo))
            DF_MDRAW_DrawTexSprite(texinfo, &rect[i], uv[i], inColp, inDepth);
}

/* OLMENU 0x1edc880 / 0x1edc800 */
static const int mdraw_uv_light2[8][4] = {{96, 192, 116, 212}, {116, 192, 140, 212}, {236, 192, 256, 212},
                                          {96, 212, 116, 227}, {236, 212, 256, 227}, {96, 236, 116, 256},
                                          {116, 236, 140, 256}, {236, 236, 256, 256}};
static const int mdraw_uv_light[8][4] = {{64, 200, 84, 220}, {84, 200, 108, 220}, {108, 200, 128, 220},
                                         {64, 220, 84, 235}, {108, 220, 128, 235}, {64, 235, 84, 255},
                                         {84, 235, 108, 255}, {108, 235, 128, 255}};

void DF_MDRAW_DrawLightWindow2(struct stMenuRect *inRectp, struct struct_76b810ec *inColp, unsigned int inDepth)
{
    PS2_FRAME(0x180);
    PS2_STACK(struct stMenuRect[8], rect, 0x100);
    PS2_STACK(int[8][4], uv, 0x80);
    PS2_STACK(struct_13efd7f4, texinfo, 0x70);
    mdraw_light_frame(inRectp, inColp, inDepth, 0x15, mdraw_uv_light2, rect, uv, &texinfo);
}

void DF_MDRAW_DrawLightWindow(struct stMenuRect *inRectp, struct struct_76b810ec *inColp, unsigned int inDepth)
{
    PS2_FRAME(0x180);
    PS2_STACK(struct stMenuRect[8], rect, 0x100);
    PS2_STACK(int[8][4], uv, 0x80);
    PS2_STACK(struct_13efd7f4, texinfo, 0x70);
    mdraw_light_frame(inRectp, inColp, inDepth, 0, mdraw_uv_light, rect, uv, &texinfo);
}

/* RE bars (texture 0, looked up per piece): left cap lw, right cap 16, middle w - lw - 16, height ht; the width is
 * at least 20 */
static void mdraw_re_bar(struct stMenuRect *inRectp, struct struct_76b810ec *inColp, unsigned int inDepth, int lw,
                         int ht, const int (*uvt)[4], struct stMenuRect *rect, int (*uv)[4], struct_13efd7f4 *texinfo)
{
    memcpy(uv, uvt, 0x30);
    int w = inRectp->w;
    if (w < 0x14) w = 0x14;
    mdraw_rect(&rect[0], inRectp->x, inRectp->y, lw, ht);
    mdraw_rect(&rect[1], inRectp->x + lw, inRectp->y, w - (lw + 0x10), ht);
    mdraw_rect(&rect[2], inRectp->x + w - 0x10, inRectp->y, 0x10, ht);
    for (int i = 0; i < 3; i++)
        if (MenuTex_StandbyTexture(0, texinfo)) DF_MDRAW_DrawTexSprite(texinfo, &rect[i], uv[i], inColp, inDepth);
}

/* OLMENU 0x1edc930 / 0x1edc900 */
static const int mdraw_uv_lightre[3][4] = {{64, 160, 84, 200}, {85, 160, 105, 200}, {128, 176, 144, 216}};
static const int mdraw_uv_genere[3][4] = {{0, 136, 10, 160}, {10, 136, 54, 160}, {64, 136, 80, 160}};

void DF_MDRAW_DrawLightWindowRE(struct stMenuRect *inRectp, struct struct_76b810ec *inColp, unsigned int inDepth)
{
    PS2_FRAME(0xd0);
    PS2_STACK(struct stMenuRect[3], rect, 0xa0);
    PS2_STACK(int[3][4], uv, 0x70);
    PS2_STACK(struct_13efd7f4, texinfo, 0x60);
    mdraw_re_bar(inRectp, inColp, inDepth, 0x14, 0x28, mdraw_uv_lightre, rect, uv, &texinfo);
}

void DF_MDRAW_DrawGeneralWindowRE(struct stMenuRect *inRectp, struct struct_76b810ec *inColp, unsigned int inDepth)
{
    PS2_FRAME(0xd0);
    PS2_STACK(struct stMenuRect[3], rect, 0xa0);
    PS2_STACK(int[3][4], uv, 0x70);
    PS2_STACK(struct_13efd7f4, texinfo, 0x60);
    mdraw_re_bar(inRectp, inColp, inDepth, 0xa, 0x18, mdraw_uv_genere, rect, uv, &texinfo);
}

/* RE2 general window (texture 0, looked up per piece; size at least 20 x 20): top row of 10 px caps + middle, then
 * the body in 70 px bands ((h - 20) / 70 of them) plus the remainder band (drawn even when 0 px high; its uv bottom
 * is set to top + remainder), then the bottom row */
void DF_MDRAW_DrawGeneralWindowRE2(struct stMenuRect *inRectp, struct struct_76b810ec *inColp, unsigned int inDepth)
{
    PS2_FRAME(0x220);
    PS2_STACK(struct stMenuRect[6], edge_rect, 0x1c0);
    PS2_STACK(struct stMenuRect[3], center_rect, 0x190);
    PS2_STACK(int[6][4], edge_uv, 0x130);
    PS2_STACK(int[3][4], center_uv, 0x100);
    PS2_STACK(struct_13efd7f4, tex_top, 0xf0);
    PS2_STACK(struct_13efd7f4, tex_band, 0xe0);
    PS2_STACK(struct_13efd7f4, tex_rest, 0xd0);
    PS2_STACK(struct_13efd7f4, tex_btm, 0xc0);
    static const int uv_edge[6][4] = {{0, 160, 10, 171}, {10, 160, 54, 171}, {190, 120, 200, 132},
                                      {0, 246, 10, 256}, {10, 246, 54, 256}, {190, 141, 200, 152}};
    static const int uv_center[3][4] = {{0, 172, 10, 246}, {10, 172, 55, 246}, {54, 172, 64, 246}};

    memcpy(edge_uv, uv_edge, sizeof(uv_edge));
    memcpy(center_uv, uv_center, sizeof(uv_center));
    int w = inRectp->w, h = inRectp->h;
    if (w < 0x14) w = 0x14;
    if (h < 0x14) h = 0x14;
    int v = (h - 0x14) / 0x46, vv = (h - 0x14) % 0x46;
    int x = inRectp->x, y = inRectp->y;
    mdraw_rect(&edge_rect[0], x, y, 0xa, 0xa);
    mdraw_rect(&edge_rect[1], x + 0xa, y, w - 0x14, 0xa);
    mdraw_rect(&edge_rect[2], x + w - 0xa, y, 0xa, 0xa);
    mdraw_rect(&edge_rect[3], x, y + h - 0xa, 0xa, 0xa);
    mdraw_rect(&edge_rect[4], x + 0xa, y + h - 0xa, w - 0x14, 0xa);
    mdraw_rect(&edge_rect[5], x + w - 0xa, y + h - 0xa, 0xa, 0xa);
    for (int i = 0; i < 3; i++)
        if (MenuTex_StandbyTexture(0, &tex_top))
            DF_MDRAW_DrawTexSprite(&tex_top, &edge_rect[i], edge_uv[i], inColp, inDepth);
    for (int j = 0, off = 0; j < v; j++, off += 0x46) {
        mdraw_rect(&center_rect[0], inRectp->x, inRectp->y + 0xa + off, 0xa, 0x46);
        mdraw_rect(&center_rect[1], inRectp->x + 0xa, inRectp->y + 0xa + off, w - 0x14, 0x46);
        mdraw_rect(&center_rect[2], inRectp->x + w - 0xa, inRectp->y + 0xa + off, 0xa, 0x46);
        for (int i = 0; i < 3; i++)
            if (MenuTex_StandbyTexture(0, &tex_band))
                DF_MDRAW_DrawTexSprite(&tex_band, &center_rect[i], center_uv[i], inColp, inDepth);
    }
    int ry = inRectp->y + 0xa + v * 0x46;
    mdraw_rect(&center_rect[0], inRectp->x, ry, 0xa, vv);
    mdraw_rect(&center_rect[1], inRectp->x + 0xa, ry, w - 0x14, vv);
    mdraw_rect(&center_rect[2], inRectp->x + w - 0xa, ry, 0xa, vv);
    for (int i = 0; i < 3; i++) center_uv[i][3] = center_uv[i][1] + vv;
    for (int i = 0; i < 3; i++)
        if (MenuTex_StandbyTexture(0, &tex_rest))
            DF_MDRAW_DrawTexSprite(&tex_rest, &center_rect[i], center_uv[i], inColp, inDepth);
    for (int i = 3; i < 6; i++)
        if (MenuTex_StandbyTexture(0, &tex_btm))
            DF_MDRAW_DrawTexSprite(&tex_btm, &edge_rect[i], edge_uv[i], inColp, inDepth);
}

/* the Nexus general window: one sprite of texture 0x3ed with the caller's uv */
void DF_MDRAW_NexusDrawGeneralWindow(struct stMenuRect *pInRect, struct struct_76b810ec *InColp, unsigned int InDepth,
                                     int *InUV)
{
    PS2_FRAME(0x60);
    PS2_STACK(struct_13efd7f4, texinfo, 0x50);
    if (MenuTex_StandbyTexture(0x3ed, &texinfo)) DF_MDRAW_DrawTexSprite(&texinfo, pInRect, InUV, InColp, InDepth);
}

/* black window (texture 0x3f0): DF_MFUNC_SetBlackWindowAttr lays out up to 7 cells for the shape and flags the ones
 * to draw */
void DF_MDRAW_DrawBlackWindow(unsigned int inShape, struct stMenuRect *inRectp, struct struct_76b810ec *inColp,
                              unsigned int inDepth)
{
    PS2_FRAME(0x170);
    PS2_STACK(int[7][4], uv, 0xf0);
    PS2_STACK(struct stMenuRect[7], rect, 0x80);
    PS2_STACK(unsigned char[7], flag, 0x168);
    PS2_STACK(struct_13efd7f4, texinfo, 0x70);
    static const int uvt[7][4] = {{8, 232, 24, 248}, {24, 224, 32, 232}, {8, 232, 24, 248}, {24, 248, 32, 255},
                                  {0, 224, 8, 232}, {8, 232, 24, 248}, {0, 248, 8, 255}};

    memcpy(uv, uvt, sizeof(uvt));
    if (!MenuTex_StandbyTexture(0x3f0, &texinfo)) return;
    memset(flag, 0, 7);
    DF_MFUNC_SetBlackWindowAttr((int)inShape, inRectp, rect, flag);
    for (int i = 0; i < 7; i++)
        if (flag[i]) DF_MDRAW_DrawTexSprite(&texinfo, &rect[i], uv[i], inColp, inDepth);
}
