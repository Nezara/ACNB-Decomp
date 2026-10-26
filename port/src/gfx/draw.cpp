/* src/gfx/draw.c - frame drawing control: the double buffers DBUFF (context 1) / DBUFF2 (its context-2 twin), the
 * flip family (Sortie / Menu / Tearing / Standard / NoVsync) with the odd / even field from timer0 + sceGsSyncV, the
 * battle display-list kick (Draw_KickDmaDl, VIF1 chain at MapKickP), render-to-texture env switches (shake RT
 * redirect, menu AC shadow restore), the menu draw -> display copy with copy-blur feedback, the frame buffer setup
 * and the GIF path-3 kicks.
 * Kicks: GifPath3KickMem2 / GifPath3KickMem / GifPath3KickSPR wait for DMA channel 2 (D2_CHCR.STR) and start a ready
 * packet in chain mode with tag id 0 in CHCR (REFE: the DMAC sends QWC qwords from MADR and stops). KickSPR marks
 * MADR as scratchpad (bit 31), KickMem2 keeps the low 28 address bits, KickMem flushes the data cache, clears bit 31
 * and also points TADR at DefaultDmaTADR (an END tag). Mem2 / SPR leave TADR as it was (port.h ps2_dma_kick_notadr).
 * Draw-env packets on the stack are DMA sources (sceGsPutDrawEnv sends PS2PTR(giftag)): PS2_STACK at the original
 * offsets. 2D helpers (GSL lists: lines, sprites, the lens-flare sprite, the full-screen fill), packet-word writers
 * (Make*), the VU0 point projection (DF_CALC_ScreenPoint / Ex) and the cockpit render targets (shadow buffer TBP
 * 0x3b40 and the pre-render frame for the gauge screen).
 * Render tags: the render targets are tagged by the texture they make (cockpit shadow = SHADOW, gauge screen = HUD;
 * closed before the restore like the shake RT); the full-screen fill and the lens-flare sprites are POST
 * (RQ_OC_NONE, sub 1 / 2); the plain 2D helpers inherit their caller's scope. */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/hooks.h"
#include "acnb/ps2fp.h"
#include "acnb/local/src/gfx/draw.h"

#include <stdio.h>
#include <stdlib.h>
#include "../../platform/plat.h"

#ifdef ACNB_SOURCEPORT
unsigned int DefaultDmaTADR[4] = {0x70000000, 0, 0, 0};   /* DMA END tag, QWC 0 */
char *PrimitiveBuffer[2] = {(char *)PS2_SPR(0x2000), (char *)PS2_SPR(0x3000)};
int g_nCopyBlur = 0x80;
int __Draw_FlipBufferCopy__ho = 9;
struct struct_c63aaab1 *CurDrawFrame;
struct struct_c63aaab1 *CurDispFrame;
void (*s_pfuncLastDraw)(void);
int g_nCopyUOffset;
int g_nCopyVOffset;
#endif

/* raw words of an sceGsDBuff (disp[2] 0, giftag0 0x50, draw0 0x60, clear0 0xe0, giftag1 0x140, draw1 0x150,
 * clear1 0x1d0) */
static inline unsigned long long &db64(struct_eb2b8baa &db, int off)
{
    return *(unsigned long long *)((unsigned char *)&db + off);
}
/* FBP / ZBP / DISPFB FBP: the low 9 bits of the halfword at off (lhu, and, or, sh) */
static inline void db_fbp(struct_eb2b8baa &db, int off, int v)
{
    unsigned short *h = (unsigned short *)((unsigned char *)&db + off);
    *h = (unsigned short)((*h & 0xfe00) | (v & 0x1ff));
}

/* the 0x90-byte draw-env packet: GIF tag (NLOOP 8, EOP, A+D) + a copy of an sceGsDrawEnv1 (8 qwords) */
static void draw_env_copy(struct_e97a9886 *e, const struct_c63aaab1 *src)
{
    e->GIFtag[0] = 0x8008;
    e->GIFtag[1] = 0x10000000;
    e->GIFtag[2] = 0xe;
    e->GIFtag[3] = 0;
    memcpy(&e->sgde, src, sizeof e->sgde);
}

/* a store of the current field g_odevCur (hook: the hybrid's even-field patch, acnb/hooks.h) */
static void draw_set_field(int f)
{
    g_odevCur = hook_field_store ? hook_field_store(f) : f;
}

static int draw_tdiff(void)
{
    return (int)((unsigned int)s_vtime0 - (unsigned int)s_vtime1);
}

/* sceGsSyncV(0). Oracle: the original (interpreted in the hybrid, frame 0x20) saves its ra at the caller's sp - 0x10:
 * the bridge's return sentinel here, the flip's own return address in the original - mirror it (conventions "Saved-ra
 * slots"); ra = the caller's jal + 8 */
static int draw_syncv(unsigned int ra)
{
    int r = sceGsSyncV(0);
    PS2_STACK(unsigned long long, slot, -0x10);
    slot = ra;
    (void)slot;
    return r;
}

/* render tag of the shake render target (host only): opened by DF_SetFrameBufferForTex, closed by the restore
 * (DF_RepairFrameBuffer) or the next redirect; the restore belongs to the frame, not the pass */
static int s_draw_rt_scope;
static void draw_rt_end(void)
{
    if (s_draw_rt_scope) {
        s_draw_rt_scope = 0;
        ps2_rq_tag_end();
    }
}

/* restores the current draw buffer's environment (the one SwapDBuff will not send: BuffID ? draw0 : draw1) after a
 * render-target pass (DF_SHAKE_Render) */
void DF_RepairFrameBuffer(void)
{
    PS2_FRAME(0xa0);
    PS2_STACK(struct_e97a9886, env, 0x10);
    draw_rt_end();
    draw_env_copy(&env, BuffID ? &DBUFF.draw0 : &DBUFF.draw1);
    FlushCache(0);
    sceGsPutDrawEnv(&env);
}

/* one draw env into the texture: default env w x h psm, SCISSOR (w - 1, h - 1), FRAME = FBP TBP >> 5, FBW w / 64,
 * psm (the whole word: FBMSK 0), then FlushCache, PutDrawEnv, SyncPath */
static void draw_tex_env(struct_e97a9886 *e, int w, int h, int psm, int TBP)
{
    e->GIFtag[0] = 0x8008;
    e->GIFtag[1] = 0x10000000;
    e->GIFtag[2] = 0xe;
    sceGsSetDefDrawEnv(&e->sgde, (short)psm, (short)w, (short)h, 0, 0);
    e->GIFtag[3] = 0;
    unsigned long long *env = (unsigned long long *)&e->sgde;
    env[6] = (unsigned long long)(long long)(w - 1) << 16 | (unsigned long long)(long long)(h - 1) << 48;
    env[0] = (unsigned long long)(long long)(TBP >> 5) | (unsigned long long)(long long)(w / 64) << 16
             | (unsigned long long)(long long)psm << 24;
    FlushCache(0);
    sceGsPutDrawEnv(e);
    sceGsSyncPath(0, 0);
}

/* redirects drawing into the texture at TBP (w x h, psm), optionally cleared to clearcol by one sprite. Quirk kept:
 * with clear the same env is put twice. */
void DF_SetFrameBufferForTex(int w, int h, int psm, int TBP, int clear, int *clearcol)
{
    PS2_FRAME(0x120);
    PS2_STACK(struct_e97a9886, env, 0x90);
    PS2_STACK(int[4], v0, 0x70);
    PS2_STACK(int[4], v1, 0x80);
    draw_rt_end();
    ps2_rq_tag_scope(RQ_K_POST, RQ_OBJID(RQ_OC_TEXTURE, (unsigned int)(TBP >> 5) << 5), 0);   /* render tag */
    s_draw_rt_scope = 1;
    if (clear) {
        draw_tex_env(&env, w, h, psm, TBP);
        v0[0] = (0x800 - w / 2) << 4;
        v0[1] = (0x800 - h / 2) << 4;
        v0[2] = 0x10;
        v0[3] = 0x10;
        v1[0] = (w / 2 + 0x800) << 4;
        v1[1] = (h / 2 + 0x800) << 4;
        v1[2] = 0x10;
        v1[3] = 0x10;
        GS_NewList();
        GS_Begin(6, 2, 0x41, 2);   /* sprite; RGBAQ, XYZ2 */
        GS_Color4iv(clearcol);
        GS_Vertex4iv(v0);
        GS_Color4iv(clearcol);
        GS_Vertex4iv(v1);
        GS_End();
        GS_FlushList();
        sceGsSyncPath(0, 0);
    }
    draw_tex_env(&env, w, h, psm, TBP);
}

/* restores the menu draw env (DBUFF.draw0) after the garage AC shadow pass */
void DF_KickPostReadyShadowForMenu(void)
{
    PS2_FRAME(0xa0);
    PS2_STACK(struct_e97a9886, env, 0x10);
    draw_env_copy(&env, &DBUFF.draw0);
    FlushCache(0);
    sceGsPutDrawEnv(&env);
}

/* the menu flip: waits for the vblank (or, after 4 slow frames in a row, busy-waits 0x4b120 timer0 ticks per frame
 * for up to 11 frames instead), always draws into DBUFF.draw0, then copies it to the display buffer */
void Draw_FlipBufferForMenu(void)
{
    PS2_FRAME(0x10);
    unsigned int self = PS2PTR(GFN(Draw_FlipBufferForMenu));
    s_vtime0 = get_timer0();
    g_odevPrev = g_odevCur;
    if (s_FlipCtrl.bIgnoreVBlank) {
        s_vtime0 = get_timer0();
        if (draw_tdiff() < 0x4b120) {
            do
                s_vtime0 = get_timer0();
            while (draw_tdiff() < 0x4b120);
            if (++s_FlipCtrl.nCount >= 11) {
                s_FlipCtrl.bIgnoreVBlank = 0;
                s_FlipCtrl.nCount = 0;
            }
        } else
            s_FlipCtrl.nCount = 0;
    } else {
        s_vtime0 = get_timer0();
        draw_set_field(!draw_syncv(self + 0xbc));
        int d = draw_tdiff();   /* the timer read before the wait */
        if (d < 0x25890) {
            draw_set_field(!draw_syncv(self + 0xec));
            s_FlipCtrl.nCount = 0;
        } else if (d < 0x4b120)
            s_FlipCtrl.nCount = 0;
        else if (++s_FlipCtrl.nCount >= 4) {
            s_FlipCtrl.nCount = 0;
            s_FlipCtrl.bIgnoreVBlank = 1;
        }
    }
    clear_timer0();
    s_vtime1 = get_timer0();
    CurDispFrame = &DBUFF.draw1;
    CurDrawFrame = &DBUFF.draw0;
    BuffID2 = 0;
    BuffID = 0;
    sceGsSetHalfOffset(&DBUFF.draw0, 0x800, 0x800, 0);
    sceGsSetHalfOffset(&DBUFF.draw1, 0x800, 0x800, 0);
    sceGsSetHalfOffset(&DBUFF2.draw0, 0x800, 0x800, 0);
    sceGsSetHalfOffset(&DBUFF2.draw1, 0x800, 0x800, 0);
    __Draw_FlipBufferCopy();
}

/* the menu draw -> display copy: draws the draw buffer (TBP 0x1180 = FBP 0x8c, 640 x 448) as two full-screen sprites
 * into the display buffer with ALPHA (Cs - Cd) * FIX + Cd: FIX = g_nCopyBlur, then blur / 2 with the copy offset
 * mirrored (flicker filter, g_nCopyUOffset / VOffset), then shows both DBuffs' buffer 0 */
void __Draw_FlipBufferCopy(void)
{
    PS2_FRAME(0xf0);
    PS2_STACK(struct_e97a9886, env, 0x50);
    PS2_STACK(int[4], rgba, 0x40);
    draw_env_copy(&env, &DBUFF.draw1);
    FlushCache(0);
    sceGsPutDrawEnv(&env);
    int U = g_nCopyUOffset, V = g_nCopyVOffset, blur = g_nCopyBlur;
    rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0x80;   /* lq of the aggregate at 0x25bf70 */
    {
        PS2_RQ_SCOPE(RQ_K_POST, RQ_OBJID(RQ_OC_TEXTURE, 0x1180), 0);   /* render tag (host only) */
        GS_NewList();
        struct_f386fb29 tex0;
        unsigned long long t = 0x0000000668029180ull;   /* TBP 0x1180, TBW 10, CT32, TW 10, TH 9, TCC 1 */
        memcpy(&tex0, &t, 8);
        GS_SetTEX0_1(tex0);
        struct_d42f65aa tex1;
        t = 0x60;   /* MMAG / MMIN linear */
        memcpy(&tex1, &t, 8);
        GS_SetTEX1_1(tex1);
        GS_SetTEST_1(0, 0, 0, 0, 0, 0, 1, 1);
        GS_SetCLAMP_1(2, 2, 0, 0x27f, 0, 0x1bf);
        GS_SetALPHA_1(0, 1, 2, 1, blur);
        GS_SetRGBA(rgba);
        int ho = __Draw_FlipBufferCopy__ho;
        GS_DrawSpriteDirect(U - ho, V - ho, U + (0x2800 - ho), V + (0x1c00 - ho), 0, 0, 0x2800, 0x1c00);
        GS_SetALPHA_1(0, 1, 2, 1, blur / 2);
        ho = __Draw_FlipBufferCopy__ho;
        GS_DrawSpriteDirect(-(ho + U), -(ho + V), (0x2800 - ho) - U, (0x1c00 - ho) - V, 0, 0, 0x2800, 0x1c00);
        GS_SetCLAMP_1(0, 0, 0, 0, 0, 0);
        GS_FlushList();
    }
    FlushCache(0);
    sceGsSwapDBuff(&DBUFF, 0);
    sceGsSwapDBuff(&DBUFF2, 0);
}

void Draw_SetCopyBlur(int blur)
{
    g_nCopyBlur = blur;
}

void Draw_SetFlickerFilter(int u, int v)
{
    g_nCopyUOffset = u;
    g_nCopyVOffset = v;
}

/* the common flip tail: current draw / display env (draw0 / draw1 by BuffID), XYOFFSET for the field in both
 * DBuffs (FlushCache after each when flush), then show + toggle DBUFF and DBUFF2 */
static void draw_flip_both(int flush)
{
    CurDrawFrame = BuffID ? &DBUFF.draw1 : &DBUFF.draw0;
    CurDispFrame = BuffID ? &DBUFF.draw0 : &DBUFF.draw1;
    sceGsSetHalfOffset(CurDrawFrame, 0x800, 0x800, (short)g_odevCur);
    if (flush) FlushCache(0);
    sceGsSetHalfOffset(BuffID2 ? &DBUFF2.draw1 : &DBUFF2.draw0, 0x800, 0x800, (short)g_odevCur);
    if (flush) FlushCache(0);
    sceGsSwapDBuff(&DBUFF, BuffID);
    BuffID = !BuffID;
    sceGsSwapDBuff(&DBUFF2, BuffID2);
    BuffID2 = !BuffID2;
}

/* the battle flip: field from the vblank wait unless the frame took between 0x2560c and 0x2cdc7 timer0 ticks (then
 * no wait, the field just alternates), screen effects + the last-draw hook (HUD, a PS2 function address), the
 * current draw env again, then the flip */
void Draw_FlipBufferForSortie(void)
{
    PS2_FRAME(0xa0);
    PS2_STACK(struct_e97a9886, env, 0x10);
    unsigned int self = PS2PTR(GFN(Draw_FlipBufferForSortie));
    s_vtime0 = get_timer0();
    int d = draw_tdiff();
    if (d < 0x2560c)
        t_oddeven = !draw_syncv(self + 0x3c);
    else if (d >= 0x2cdc7)
        t_oddeven = !draw_syncv(self + 0x6c);
    else
        t_oddeven = !g_odevCur;
    clear_timer0();
    ScrEff_DrawEffect();
    if (s_pfuncLastDraw) gcall(s_pfuncLastDraw);
    ScrEff_DrawDefault();
    draw_env_copy(&env, BuffID ? &DBUFF.draw0 : &DBUFF.draw1);
    FlushCache(0);
    sceGsPutDrawEnv(&env);
    g_odevPrev = g_odevCur;
    draw_set_field(t_oddeven);
    draw_flip_both(1);
    s_vtime1 = get_timer0();
}

/* flip without waiting (DF_WAIT_Sync) */
void Draw_FlipBufferNoVsync(void)
{
    PS2_FRAME(0x10);
    ScrEff_DrawDefault();
    draw_flip_both(0);
}

/* loading / link-screen flip: Sortie's field logic, screen effects only for the asymmetric (battle) buffers */
void Draw_FlipBufferTearing(void)
{
    PS2_FRAME(0x10);
    unsigned int self = PS2PTR(GFN(Draw_FlipBufferTearing));
    s_vtime0 = get_timer0();
    int old = g_odevCur;
    g_odevPrev = old;
    int d = draw_tdiff();
    if (d < 0x2560c)
        draw_set_field(!draw_syncv(self + 0x40));
    else if (d >= 0x2cdc7)
        draw_set_field(!draw_syncv(self + 0x6c));
    else
        draw_set_field(!old);
    clear_timer0();
    if (s_draw.isAsym) ScrEff_DrawDefault();
    draw_flip_both(1);
    s_vtime1 = get_timer0();
}

/* plain vsync flip of DBUFF only */
void Draw_FlipBufferStandard(void)
{
    PS2_FRAME(0x10);
    g_odevPrev = g_odevCur;
    draw_set_field(!draw_syncv(PS2PTR(GFN(Draw_FlipBufferStandard)) + 0x18));
    if (s_draw.isAsym) {
        ScrEff_DrawEffect();
        ScrEff_DrawDefault();
    }
    CurDrawFrame = BuffID ? &DBUFF.draw1 : &DBUFF.draw0;
    CurDispFrame = BuffID ? &DBUFF.draw0 : &DBUFF.draw1;
    sceGsSetHalfOffset(CurDrawFrame, 0x800, 0x800, (short)g_odevCur);
    FlushCache(0);
    sceGsSwapDBuff(&DBUFF, BuffID);
    BuffID = !BuffID;
}

/* clear colour of both clear packets: RGBAQ = r | g << 8 | b << 16 | a << 24 | Q 0x00000001 (libgcc 0x11d220
 * float -> u64 soft-double conversions: truncated, negative -> 0). Quirks kept: components not masked (out of
 * 0..255 they spill into the neighbours), Q is the denormal 1, not 1.0f. */
static unsigned long long draw_clr_rgbaq(const float *clr)
{
    unsigned long long v = ps2_f2u64(clr[1]) << 8;
    v |= ps2_f2u64(clr[0]);
    v |= ps2_f2u64(clr[2]) << 16;
    v |= ps2_f2u64(clr[3]) << 24;
    return v | 1ull << 32;
}

void DF_SetDBuffClrCol(struct struct_eb2b8baa *dbp, float *clr)
{
    PS2_FRAME(0x40);
    db64(*dbp, 0x100) = draw_clr_rgbaq(clr);   /* clear0.rgbaq */
    db64(*dbp, 0x1f0) = draw_clr_rgbaq(clr);   /* clear1.rgbaq */
}

struct struct_c63aaab1 *DF_GetCurDrawFrame(void)
{
    return CurDrawFrame;
}

/* per sortie: world-screen matrices to identity-w zero, view / shadow screen matrices (the first two calls are
 * overwritten by the 384 ones - kept: call count / VU0 state) */
void DRAW_Reset(void)
{
    PS2_FRAME(0x20);   /* the sceVu0ViewScreenMatrix wrapper writes farz at the EE sp */
    ZeroMemory32b(WORLD_SCREEN_MATRIX, 0x10);
    WORLD_SCREEN_MATRIX[3][3] = 1.0f;
    ZeroMemory32b(world_screen_matrix, 0x10);
    world_screen_matrix[3][3] = 1.0f;
    sceVu0ViewScreenMatrix(VIEW_SCREEN_MATRIX, 512.0f, 1.0f, 0.5f, 2048.0f, 2048.0f, 0.0f, 16777215.0f, 1.0f, 65536.0f);
    sceVu0ViewScreenMatrix(SHADOW_SCREEN_MATRIX, 512.0f, 1.0f, -1.0f, 2048.0f, 2048.0f, 0.0f, 16777215.0f, 1.0f,
                           65536.0f);
    sceVu0ViewScreenMatrix(VIEW_SCREEN_MATRIX, 384.0f, 1.0f, 0.5f, 2048.0f, 2048.0f, 0.0f, 16777215.0f, 1.0f, 65536.0f);
    sceVu0ViewScreenMatrix(SHADOW_SCREEN_MATRIX, 384.0f, 1.0f, -1.0f, 2048.0f, 2048.0f, 0.0f, 16777215.0f, 1.0f,
                           65536.0f);
    /* oracle: sceVu0ViewScreenMatrix (interpreted, frame 0xa0) left its ra at sp - 0x50 (conventions "Saved-ra
     * slots"): DRAW_Reset + 0x16c in the original */
    PS2_STACK(unsigned long long, vsm_ra, -0x50);
    vsm_ra = (unsigned long long)PS2PTR(GFN(DRAW_Reset)) + 0x16c;
    (void)vsm_ra;
}

/* the battle display-list kick: the matrices / camera positions the list's REF tags read at DMA time are copied
 * first (keep the order), then the VIF1 chain at MapKickP (bit 0x20000000 = the list is already flushed) */
void Draw_KickDmaDl(void)
{
    PS2_FRAME(0x20);
    if (!MapKickP) return;
    FlushCache(0);
    sceVu0CopyMatrix(world_screen_matrix, WORLD_SCREEN_MATRIX);
    sceVu0CopyMatrix(camera_matrix, CAMERA_MATRIX1);
    for (int i = 0; i < 4; i++) memcpy(CAM_POS[i], CAM[i].vec, 16);
    DF_STAGE_PushLocalMatAll();
    unsigned int p = PS2PTR(MapKickP);
    if (p & 0x20000000u)
        p &= ~0x20000000u;
    else
        FlushCache(0);
    ps2_dma_wait_vif1();
    ps2_dma_kick_qmt(1, 0x105, 0, 0, p);   /* QWC 0, MADR 0, TADR, CHCR (chain, TTE) */
    MapKickP = 0;
}

/* pfunc is a PS2 function address (GFN), called by the battle flip through gcall */
void Draw_SetLastDrawFunc(void (*pfunc)(void))
{
    s_pfuncLastDraw = pfunc;
}

struct StDraw *Draw_GetProperty(void)
{
    return &s_draw;
}

/* frame buffers for the battle (isSortie: 512 x 224 CT24 + Z24, the asymmetric half-height buffers at FBP 0x38) or
 * the menus (640 x 448 CT32 + Z16S, draw FBP 0x8c = TBP 0x1180 copied to display FBP 0 by __Draw_FlipBufferCopy).
 * DBUFF2 is DBUFF's context-2 twin (register ids FRAME_2 / ZBUF_2 / XYOFFSET_2 / SCISSOR_2 / TEST_2, PRIM CTXT). */
void Draw_SetupFrameBuffer(int isSortie, int isAsym)
{
    PS2_FRAME(0xa0);
    int w, hBuf, hScr, bw, psm, zpsm, fbpAsym;
    if (isSortie) {
        w = 0x200; hBuf = 0xe0; hScr = 0x1c0; bw = 8; psm = 1; zpsm = 0x31; fbpAsym = 0x38;
    } else {
        hBuf = 0x1c0; w = 0x280; bw = 0xa; hScr = 0x1c0; psm = 0; zpsm = 0x3a; fbpAsym = 0x46;
    }
    sceGsGetGParam()->ffmode = isSortie ? 1 : 0;
    sceGsSetDefDBuff(&DBUFF, (short)psm, (short)w, (short)hBuf, 3, (short)zpsm, 1);
    sceGsSetDefDBuff(&DBUFF2, (short)psm, (short)w, (short)hBuf, 3, (short)zpsm, 1);
    if (isAsym) {
        db_fbp(DBUFF, 0x60, fbpAsym);    /* draw0.frame1 */
        db_fbp(DBUFF, 0x150, fbpAsym);   /* draw1.frame1 */
        db_fbp(DBUFF2, 0x60, fbpAsym);
        db_fbp(DBUFF2, 0x150, fbpAsym);
    }
    db64(DBUFF, 0x100) = 1ull << 32;     /* clear0 / clear1 rgbaq: black, Q 0x00000001 */
    db64(DBUFF, 0x1f0) = 1ull << 32;
    db64(DBUFF2, 0x100) = 1ull << 32;
    db64(DBUFF2, 0x1f0) = 1ull << 32;
    db64(DBUFF2, 0x68) = 0x4d;  db64(DBUFF2, 0x158) = 0x4d;   /* FRAME_2 */
    db64(DBUFF2, 0x78) = 0x4f;  db64(DBUFF2, 0x168) = 0x4f;   /* ZBUF_2 */
    db64(DBUFF2, 0x88) = 0x19;  db64(DBUFF2, 0x178) = 0x19;   /* XYOFFSET_2 */
    db64(DBUFF2, 0x98) = 0x41;  db64(DBUFF2, 0x188) = 0x41;   /* SCISSOR_2 */
    db64(DBUFF2, 0xd8) = 0x48;  db64(DBUFF2, 0xe8) = 0x48;    /* TEST_2: draw0.test1, clear0 testa / testb */
    db64(DBUFF2, 0x138) = 0x48; db64(DBUFF2, 0x1c8) = 0x48;   /* draw1.test1, clear1 testa / testb */
    db64(DBUFF2, 0x1d8) = 0x48; db64(DBUFF2, 0x228) = 0x48;
    *(unsigned int *)((unsigned char *)&DBUFF2 + 0xf0) |= 0x200;    /* clear0.prim CTXT */
    *(unsigned int *)((unsigned char *)&DBUFF2 + 0x1e0) |= 0x200;   /* clear1.prim CTXT */
    if (isAsym) {
        /* PMODE of all four display envs: EN1 1, EN2 0, MMOD 1 (ALP), AMOD 0, ALP 0xff */
        unsigned char *pm[4] = {(unsigned char *)&DBUFF, (unsigned char *)&DBUFF + 0x28, (unsigned char *)&DBUFF2,
                                (unsigned char *)&DBUFF2 + 0x28};
        for (int i = 0; i < 4; i++) {
            pm[i][1] = 0xff;
            pm[i][0] = (unsigned char)((pm[i][0] & ~0x63) | 0x21);
        }
    }
    BuffID2 = 0;
    BuffID = 0;
    PBuffID = 0;
    if (isSortie) {
        GSX_SetWindowSize((short)w / 2, (short)hBuf / 2, 4, 3);
    } else {
        int n = (short)w * (short)hBuf * 4 / 4 / 2048;   /* frame pages: 0x8c */
        db_fbp(DBUFF, 0x60, n);        /* draw0.frame1 FBP */
        db_fbp(DBUFF2, 0x60, n);
        db_fbp(DBUFF2, 0x70, n * 2);   /* draw0 / draw1 zbuf1 ZBP */
        db_fbp(DBUFF, 0x70, n * 2);
        db_fbp(DBUFF2, 0x160, n * 2);
        db_fbp(DBUFF, 0x160, n * 2);
        db_fbp(DBUFF, 0x10, 0);        /* disp[0] / disp[1] DISPFB FBP */
        db_fbp(DBUFF, 0x38, n);
        db_fbp(DBUFF2, 0x10, 0);
        db_fbp(DBUFF2, 0x38, n);
        GSX_SetWindowSize((short)w / 2, (short)hBuf / 2, 4, 4);
        sceGsSetHalfOffset(&DBUFF.draw0, 0x800, 0x800, 0);
        sceGsSetHalfOffset(&DBUFF.draw1, 0x800, 0x800, 0);
        sceGsSetHalfOffset(&DBUFF2.draw0, 0x800, 0x800, 0);
        sceGsSetHalfOffset(&DBUFF2.draw1, 0x800, 0x800, 0);
        ZeroMemory(&s_FlipCtrl, 8);
    }
    FlushCache(0);
    draw_syncv(PS2PTR(GFN(Draw_SetupFrameBuffer)) + 0x640);
    sceGsPutDrawEnv(&DBUFF.giftag0);
    sceGsSyncPath(0, 0);
    sceGsPutDrawEnv(&DBUFF.giftag1);
    sceGsSyncPath(0, 0);
    s_draw.wScreen = (short)w;
    s_draw.hScreen = (short)hScr;
    s_draw.hBuffer = (short)hBuf;
    s_draw.bwScreen = (short)bw;
    s_draw.psmScreen = (short)psm;
    s_draw.psmZ = (short)zpsm;
    s_draw.isAsym = isAsym;
}

/* boot: menu frame buffers, camera matrices to identity, VU1 microprograms, screen effects */
void Draw_Init(void)
{
    PS2_FRAME(0x20);
    ZeroMemory8b(&s_draw, 0x14);
    Draw_SetupFrameBuffer(0, 0);
    sceVu0CopyMatrix(CAMERA_MATRIX1, IdentityMtx);
    sceVu0CopyMatrix(CAMERA_MATRIX2, IdentityMtx);
    sceVu0CopyMatrix(CAMERA_MATRIX3, IdentityMtx);
    DF_SendPGVU1();   /* the original passes a0 = 1 (unused) */
    ScrEff_Init();
}

void GifPath3KickMem2(unsigned int *addr, int qwc)
{
    ps2_dma_wait_gif();
    ps2_dma_kick_notadr(2, 0x105, PS2_DMA_ADDR(addr, PS2PTR(addr) & 0x0fffffffu), (unsigned int)qwc);
}

void GifPath3KickMem(unsigned int *addr, int qwc)
{
    PS2_FRAME(0x30);
    FlushCache(0);
    ps2_dma_wait_gif();
    ps2_dma_kick(2, 0x105, PS2_DMA_ADDR(addr, PS2PTR(addr) & 0x7fffffffu), (unsigned int)qwc, PS2PTR(DefaultDmaTADR));
}

void GifPath3KickSPR(unsigned int *addr, int qwc)
{
    ps2_dma_wait_gif();
    ps2_dma_kick_notadr(2, 0x105, PS2_DMA_ADDR(addr, PS2PTR(addr) | 0x80000000u), (unsigned int)qwc);
}

/* ---------------------------------------------------------------- packet-word writers (sfx / stat callers) */

unsigned int *MakePACKET(unsigned int *gp0, unsigned int a, unsigned int b, unsigned int c, unsigned int d)
{
    gp0[0] = a;
    gp0[1] = b;
    gp0[2] = c;
    gp0[3] = d;
    return gp0 + 4;
}

/* one A+D pair: the data dword first, then the register address */
unsigned int *MakeREGS(unsigned int *gp0, unsigned long long regs, unsigned long long data)
{
    *(unsigned long long *)gp0 = data;
    *(unsigned long long *)(gp0 + 2) = regs;
    return gp0 + 4;
}

/* a GIF tag. The original read-modify-writes the tag's bit fields, but defines every bit: NLOOP, EOP, bits 16-45
 * cleared, PRE = (prim != 0), PRIM, FLG, NREG; REGS in the second dword */
unsigned int *MakeGIF(unsigned int *gp0, unsigned int nloop, unsigned char eop, unsigned int prim, unsigned char flg,
                      unsigned char nreg, unsigned long long regs)
{
    unsigned long long t = (unsigned long long)(nloop & 0x7fff) | (unsigned long long)(eop & 1) << 15
                           | (unsigned long long)(prim != 0) << 46 | (unsigned long long)(prim & 0x7ff) << 47
                           | (unsigned long long)(flg & 3) << 58 | (unsigned long long)(nreg & 0xf) << 60;
    *(unsigned long long *)gp0 = t;
    *(unsigned long long *)(gp0 + 2) = regs;
    return gp0 + 4;
}

/* a DMA tag: QWC, ID (ADDR 0), second dword 0 */
unsigned int *MakeDMA(unsigned int *gp0, unsigned int qwc, unsigned int id)
{
    *(unsigned long long *)gp0 = (unsigned int)((qwc & 0xffff) | (id & 7) << 28);
    *(unsigned long long *)(gp0 + 2) = 0;
    return gp0 + 4;
}

/* ---------------------------------------------------------------- cockpit shadow buffer (CKPT_Draw) */

/* restores the current draw env after the cockpit shadow pass (byte-identical to DF_RepairFrameBuffer) */
void DF_KickPostReadyShadow(void)
{
    PS2_FRAME(0xa0);
    PS2_STACK(struct_e97a9886, env, 0x10);
    draw_rt_end();
    draw_env_copy(&env, BuffID ? &DBUFF.draw0 : &DBUFF.draw1);
    FlushCache(0);
    sceGsPutDrawEnv(&env);
}

void DF_KickPreReadyShadow(void)
{
    PS2_FRAME(0x20);
    PS2_STACK(int[4], rgba, 0x10);
    for (int i = 0; i < 4; i++) rgba[i] = 0;   /* byte loop in the original */
    DF_KickPreReadyShadowEx(0x3b40, 0x100, 0x100, rgba);
}

/* the cockpit shadow pass: draw env into tbp (wd x ht, PSMCT16, FBW rounded up), optionally cleared by one sprite
 * of rgba at Z 0xffffff, then the env again with the scissor shrunk by one pixel on every side (the shadow keeps a
 * cleared border). Twin of menu/mdraw DF_MDRAW_SetShadowFrame. */
void DF_KickPreReadyShadowEx(int tbp, int wd, int ht, int *rgba)
{
    PS2_FRAME(0x140);
    PS2_STACK(struct_e97a9886, env, 0xb0);
    PS2_STACK(int[2][4], v, 0x90);
    draw_rt_end();
    ps2_rq_tag_scope(RQ_K_SHADOW, RQ_OBJID(RQ_OC_TEXTURE, (unsigned int)(tbp >> 5) << 5), 0);   /* render tag */
    s_draw_rt_scope = 1;
    unsigned long long frame = (unsigned long long)(long long)(tbp >> 5)
                               | (unsigned long long)(long long)((wd + 63) / 64) << 16 | 0x2000000ull;
    env.GIFtag[0] = 0x8008;
    env.GIFtag[1] = 0x10000000;
    env.GIFtag[2] = 0xe;
    env.GIFtag[3] = 0;
    sceGsSetDefDrawEnv(&env.sgde, 2, (short)wd, (short)ht, 0, 0);
    *(unsigned long long *)&env.sgde.frame1 = frame;
    FlushCache(0);
    sceGsPutDrawEnv(&env);
    sceGsSyncPath(0, 0);
    if (rgba) {
        v[0][0] = (0x800 - wd / 2) << 4;
        v[0][1] = (0x800 - ht / 2) << 4;
        v[0][2] = 0xffffff;
        v[0][3] = 0;
        v[1][0] = (wd / 2 + 0x800) << 4;
        v[1][1] = (ht / 2 + 0x800) << 4;
        v[1][2] = 0xffffff;
        v[1][3] = 0;
        GS_NewList();
        GS_Begin(6, 2, 0x41, 2);   /* sprite; RGBAQ, XYZ2 */
        GS_Color4iv(rgba);
        GS_Vertex4iv(v[0]);
        GS_Color4iv(rgba);
        GS_Vertex4iv(v[1]);
        GS_End();
        GS_FlushList();
        sceGsSyncPath(0, 0);
    }
    env.GIFtag[0] = 0x8008;
    env.GIFtag[1] = 0x10000000;
    env.GIFtag[2] = 0xe;
    env.GIFtag[3] = 0;
    sceGsSetDefDrawEnv(&env.sgde, 2, (short)wd, (short)ht, 0, 0);
    *(unsigned long long *)&env.sgde.scissor1 = 1ull | (unsigned long long)(long long)(wd - 2) << 16 | 1ull << 32
                                                | (unsigned long long)(long long)(ht - 2) << 48;
    *(unsigned long long *)&env.sgde.frame1 = frame;
    FlushCache(0);
    sceGsPutDrawEnv(&env);
    sceGsSyncPath(0, 0);
}

/* ---------------------------------------------------------------- 2D helpers */

/* the w x h sprite centred on center (12.4 screen coordinates; the height is halved for the field buffer), culled
 * against the (framex, framey) window around the screen centre and the Z range: v = {lt, rb} */
static int draw_center_sprite(int (*v)[4], const int *center, int w, int h, int z, int framex, int framey)
{
    int hw = (int)((unsigned int)w << 4) / 2;
    int hh = (int)((unsigned int)h << 4) / 4;
    v[0][0] = center[0] - hw;
    v[0][1] = center[1] - hh;
    v[1][0] = center[0] + hw;
    v[1][1] = center[1] + hh;
    v[1][2] = z;
    v[0][2] = z;
    v[1][3] = 0x10;
    v[0][3] = 0x10;
    if (v[1][0] < (0x800 - framex) << 4) return 0;
    if (v[1][1] < (0x800 - framey) << 4) return 0;
    if ((framex + 0x800) << 4 < v[0][0]) return 0;
    if ((framey + 0x800) << 4 < v[0][1]) return 0;
    if (z < 0 || z >= 0x1000000) return 0;
    return 1;
}

static struct_f386fb29 draw_tex0(struct struct_1f802d8d tex)
{
    struct_f386fb29 t;
    memcpy(&t, &tex, 8);
    return t;
}

/* light-effect sprite: textured, ALPHA_1 0x48 (Cs * As + Cd), TEST_1 0x31ff1 */
void DF_DrawSpriteCenterLight(int *center, int *rgb, float *ST, struct struct_1f802d8d tex, int w, int h, int z,
                              int primadd, int framex, int framey)
{
    PS2_FRAME(0x70);
    PS2_STACK(int[2][4], v, 0x50);
    if (!draw_center_sprite(v, center, w, h, z, framex, framey)) return;
    PS2_RQ_SCOPE(RQ_K_POST, RQ_OBJID(RQ_OC_NONE, 0), 2);   /* render tag (host only): lens flare / light effect */
    GS_NewList();
    GS_SetTEXFLUSH();
    GS_SetTEX0_1(draw_tex0(tex));
    GS_SetRegister(0x14, 0x60);      /* TEX1_1: bilinear */
    GS_SetRegister(8, 0);            /* CLAMP_1 */
    GS_SetRegister(0x42, 0x48);      /* ALPHA_1 */
    GS_SetRegister(0x47, 0x31ff1);   /* TEST_1 */
    GS_Begin((unsigned int)primadd | 0x16, 2, 0x412, 3);   /* textured sprite; ST, RGBAQ, XYZ2 */
    GS_TexCoord2fv(ST);
    GS_Color4iv(rgb);
    GS_Vertex4iv(v[0]);
    GS_TexCoord2fv(ST + 2);
    GS_Color4iv(rgb);
    GS_Vertex4iv(v[1]);
    GS_End();
    GS_FlushList();
    sceGsSyncPath(0, 0);
}

/* lens-flare sprite (DF_EFF_DrawLensFlare) */
void DF_DrawSpriteCenter(int *center, int *rgb, float *st, struct struct_1f802d8d tex, int w, int h, int z,
                         int primadd, int framex, int framey)
{
    PS2_FRAME(0x40);
    PS2_STACK(int[2][4], v, 0x20);
    if (!draw_center_sprite(v, center, w, h, z, framex, framey)) return;
    PS2_RQ_SCOPE(RQ_K_POST, RQ_OBJID(RQ_OC_NONE, 0), 2);   /* render tag (host only): lens flare */
    DF_Draw2DSprite_ST(rgb, v, tex, st, primadd);
    sceGsSyncPath(0, 0);
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrappers (gen_bridge stops at 8 int args); tex is the 64-bit a3, framex / framey the
 * stack args at the caller's sp+0 / sp+8 */
static void bw__DF_DrawSpriteCenterLight(EECore *c)
{
    unsigned int sp = c->gpr[SP].uw[0];
    struct struct_1f802d8d tex;
    memcpy(&tex, &c->gpr[A3].ud[0], 8);
    DF_DrawSpriteCenterLight((int *)bridge_ptr(c->gpr[A0].uw[0]), (int *)bridge_ptr(c->gpr[A1].uw[0]),
                             (float *)bridge_ptr(c->gpr[A2].uw[0]), tex, c->gpr[T0].sw[0], c->gpr[T1].sw[0],
                             c->gpr[T2].sw[0], c->gpr[T3].sw[0], (int)rd32(sp), (int)rd32(sp + 8));
}
static void bw__DF_DrawSpriteCenter(EECore *c)
{
    unsigned int sp = c->gpr[SP].uw[0];
    struct struct_1f802d8d tex;
    memcpy(&tex, &c->gpr[A3].ud[0], 8);
    DF_DrawSpriteCenter((int *)bridge_ptr(c->gpr[A0].uw[0]), (int *)bridge_ptr(c->gpr[A1].uw[0]),
                        (float *)bridge_ptr(c->gpr[A2].uw[0]), tex, c->gpr[T0].sw[0], c->gpr[T1].sw[0],
                        c->gpr[T2].sw[0], c->gpr[T3].sw[0], (int)rd32(sp), (int)rd32(sp + 8));
}
#endif

/* a line xyz[0] -> xyz[1]: PRIM = the caller's ABE / FGE / ... bits (prim >> 15, arithmetic) | LINE + AA1 */
void DF_Draw2DLine_Color(int *rgb, int (*xyz)[4], int prim)
{
    PS2_FRAME(0x40);
    GS_NewList();
    GS_Begin((unsigned int)((prim >> 15) & ~0x7f) | 0x81, 2, 0x41, 2);
    GS_Color4iv(rgb);
    GS_Vertex4iv(xyz[0]);
    GS_Color4iv(rgb);
    GS_Vertex4iv(xyz[1]);
    GS_End();
    GS_FlushList();
}

/* full-screen flat sprite in both contexts (loading / black mask). Quirk kept: vertex 0's Z word is never written
 * (its fourth word gets 0xffffff instead), so the stale stack word at sp+0x28 goes into the GS list (GS-invisible:
 * a sprite takes the second vertex's Z) */
void DF_DrawColorScreen_NoSemi(unsigned int r, unsigned int g, unsigned int b, unsigned int a)
{
    PS2_FRAME(0x50);
    PS2_STACK(int[2][4], xyz, 0x20);
    PS2_STACK(int[4], col, 0x40);
    PS2_RQ_SCOPE(RQ_K_POST, RQ_OBJID(RQ_OC_NONE, 0), 1);   /* render tag (host only): screen fill */
    xyz[0][0] = 0x7000;
    col[0] = (int)r;
    xyz[1][0] = 0x9000;
    col[1] = (int)g;
    xyz[1][1] = 0x8700;
    col[2] = (int)b;
    col[3] = (int)a;
    xyz[0][1] = 0x7900;
    xyz[1][3] = 0x10;
    xyz[0][3] = 0xffffff;
    xyz[1][2] = 0xffffff;
    for (int pass = 0; pass < 2; pass++) {
        GS_NewList();
        GS_Begin(pass ? 0x206 : 6, 2, 0x41, 2);   /* sprite, context 1 then context 2 */
        GS_Color4iv(col);
        GS_Vertex4iv(xyz[0]);
        GS_Color4iv(col);
        GS_Vertex4iv(xyz[1]);
        GS_End();
        GS_FlushList();
        sceGsSyncPath(0, 0);
    }
}

void DF_Draw2DSprite_Color(int *rgb, int (*xyz)[4], int prim)
{
    PS2_FRAME(0x40);
    GS_NewList();
    GS_Begin((unsigned int)(prim & ~0x1f) | 6, 2, 0x41, 2);
    GS_Color4iv(rgb);
    GS_Vertex4iv(xyz[0]);
    GS_Color4iv(rgb);
    GS_Vertex4iv(xyz[1]);
    GS_End();
    GS_FlushList();
}

/* textured sprite with UV texel coordinates (SHIMMER) */
void DF_Draw2DSprite_UV(int *rgb, int (*xyz)[4], struct struct_1f802d8d tex, int *uv, int prim)
{
    PS2_FRAME(0x60);
    GS_NewList();
    GS_SetTEXFLUSH();
    GS_SetTEX0_1(draw_tex0(tex));
    GS_SetRegister(0x14, 0x60);
    GS_SetRegister(8, 0);
    GS_SetRGBA(rgb);
    GS_Begin((unsigned int)(prim & ~0xf) | 0x116, 2, 0x43, 2);   /* textured sprite, FST; UV, XYZ2 */
    GS_UVv(uv);
    GS_Vertex4iv(xyz[0]);
    GS_UVv(uv + 2);
    GS_Vertex4iv(xyz[1]);
    GS_End();
    GS_FlushList();
}

/* textured sprite with ST coordinates (SHIMMER, DF_DrawSpriteCenter) */
void DF_Draw2DSprite_ST(int *rgb, int (*xyz)[4], struct struct_1f802d8d tex, float *ST, int prim)
{
    PS2_FRAME(0x60);
    GS_NewList();
    GS_SetTEXFLUSH();
    GS_SetTEX0_1(draw_tex0(tex));
    GS_SetRegister(0x14, 0x60);
    GS_SetRegister(8, 0);
    GS_Begin((unsigned int)(prim & ~0xf) | 0x16, 2, 0x412, 3);   /* textured sprite; ST, RGBAQ, XYZ2 */
    GS_TexCoord2fv(ST);
    GS_Color4iv(rgb);
    GS_Vertex4iv(xyz[0]);
    GS_TexCoord2fv(ST + 2);
    GS_Color4iv(rgb);
    GS_Vertex4iv(xyz[1]);
    GS_End();
    GS_FlushList();
}

/* ---------------------------------------------------------------- VU0 point projection */

/* scr = ftoi4(m * loc / w) on VU0 (vf1-4 = m, vf16 = the product then the result, vf26 = the projected point, acc,
 * q as the asm leaves them; interpreted callers may read them). Same chain as gfx.cpp gfxvu0_RotTransPers minus vf31. */
static void draw_screen_point(int *scr, const float (*m)[4], const float *loc)
{
    auto *v = VU0();
    float l[4], r[4];
    memcpy(l, PS2_LQ_SRC(loc), 16);
    for (int k = 0; k < 4; k++) memcpy(v->vf[1 + k].f, PS2_LQ_SRC(m[k]), 16);
    for (int i = 0; i < 4; i++) {
        float a = v->vf[1].f[i] * l[0];
        a = a + v->vf[2].f[i] * l[1];
        a = a + v->vf[3].f[i] * l[2];
        v->acc[i] = a;
        r[i] = a + v->vf[4].f[i] * l[3];
    }
    v->q = ps2_div(1.0f, r[3]);
    for (int i = 0; i < 4; i++) v->vf[26].f[i] = r[i] * v->q;
    for (int i = 0; i < 4; i++) v->vf[16].sw[i] = ps2_ftoi(v->vf[26].f[i] * 16.0f);
    memcpy((void *)PS2_LQ_SRC(scr), v->vf[16].sw, 16);
}

void DF_CALC_ScreenPoint(float *loc, int *scr)
{
    draw_screen_point(scr, world_screen_matrix, loc);
}

void DF_CALC_ScreenPointEx(int *scr, float (*ws)[4], float *loc)
{
    draw_screen_point(scr, ws, loc);
}

/* ---------------------------------------------------------------- cockpit pre-render frame (set_model_attribute) */

/* back to the current draw buffer after the gauge-screen pre-render: SCISSOR, FRAME (the current env's FBP) and
 * XYOFFSET (+ half a line on the odd field) as one GS list */
void Draw_RestoreFrameBuffer(void)
{
    PS2_FRAME(0x10);
    draw_rt_end();
    GS_NewList();
    GS_SetRegister(0x40, (unsigned long long)(long long)(s_draw.wScreen - 1) << 16
                             | (unsigned long long)(long long)(s_draw.hBuffer - 1) << 48);   /* SCISSOR_1 */
    const struct_c63aaab1 *e = BuffID ? &DBUFF.draw0 : &DBUFF.draw1;
    unsigned int fbp = *(const unsigned short *)&e->frame1 & 0x1ff;
    GS_SetRegister(0x4c, (unsigned long long)(long long)s_draw.psmScreen << 24
                             | ((unsigned long long)(long long)s_draw.bwScreen << 16 | fbp));   /* FRAME_1 */
    int y = ((0x800 - s_draw.hBuffer / 2) << 4) + (g_odevCur ? 8 : 0);
    int x = (0x800 - s_draw.wScreen / 2) << 4;
    GS_SetRegister(0x18, (unsigned long long)(long long)x | (unsigned long long)(long long)y << 32);   /* XYOFFSET_1 */
    GS_FlushList();
}

/* draws into fbp (width x height, psm) from now on: SCISSOR, FRAME, XYOFFSET centred */
void Draw_SetupPreRenderFrameBuffer(short fbp, short psm, int width, int height)
{
    PS2_FRAME(0x50);
    draw_rt_end();
    ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_TEXTURE, (unsigned int)fbp << 5), 0);   /* render tag: gauge screen */
    s_draw_rt_scope = 1;
    GS_NewList();
    GS_SetRegister(0x40, (unsigned long long)(long long)(width - 1) << 16
                             | (unsigned long long)(long long)(height - 1) << 48);   /* SCISSOR_1 */
    GS_SetRegister(0x4c, (unsigned long long)(long long)psm << 24
                             | ((unsigned long long)(long long)fbp
                                | (unsigned long long)(long long)((width + 63) >> 6) << 16));   /* FRAME_1 */
    GS_SetRegister(0x18, (unsigned long long)(long long)((0x800 - width / 2) << 4)
                             | (unsigned long long)(long long)((0x800 - height / 2) << 4) << 32);   /* XYOFFSET_1 */
    GS_FlushList();
}
