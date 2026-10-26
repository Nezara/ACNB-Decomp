/* src/sfx/shake.cpp - heat-haze / shock "shake" distortion sources (logic part). Up to 16 world-space sources
 * (0x20 bytes each in `shake`) created by AC hover boosters and boost magic; handles are slot + 1 (0 = none, the
 * setters ignore handles <= 0). DF_SHAKE_Proc (once per draw frame, from DrawAfter) counts each live source down
 * (frame 0xffffffff = endless), advances its 16-step bokashi animation every second frame, projects it with
 * WORLD_SCREEN_MATRIX and hands the screen position to DF_SHAKE_Main. DF_SHAKE_Init loads the 16 bokashi textures
 * (file 12). Render part (phase 2 g9b): DF_SHAKE_Main draws one source as a three-stage feedback chain - Forward
 * BITBLTs the framebuffer rect under it into VRAM 0x3c40, TextureOut uploads the current bokashi texture (PSMT8 at
 * 0x3b40, CLUT 0x3f80), Render draws it scaled into the 128x64 render target 0x3bc0, and Main's packet BITBLTs that RT
 * (PSMT8H) into the alpha byte of the 0x3c40 copy and draws it back over the screen as an alpha-blended sprite
 * (SpriteDraw). All packets go to the GIF by direct D2 chain kicks (QWC 0, MADR 0, TADR, FlushCache, CHCR 0x145);
 * Forward / Render / TextureOut build them on their own stack (PS2_STACK at the original offsets). Quirk kept:
 * Forward and Main return with their DMA in flight and do not sync (Forward's packet lives in a dead frame the next
 * sibling overwrites after its own SyncPath; the hybrid DMAC and the render module both consume a chain at the CHCR
 * write). Render tags: Forward = POST, texture 0x3c40 sub 0; TextureOut = UPLOAD 0x3b40 (psm 0x13) / 0x3f80 (CLUT,
 * psm 0); Render's state packet = POST, texture 0x3bc0 (its draw is inside draw.c's RT scope); Main = POST, texture
 * 0x3c40 sub 1. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"
#include "acnb/vu0m.h"

#ifdef ACNB_SOURCEPORT
char before_texture = -1;
#endif

void DF_SHAKE_SetSW(int num, char sw)
{
    if (num > 0) shake[num - 1].sw = (int)sw;
}

void DF_SHAKE_SetPos(int num, float *pos)
{
    if (num > 0) {
        shake[num - 1].pos[0] = pos[0];
        shake[num - 1].pos[1] = pos[1];
        shake[num - 1].pos[2] = pos[2];
    }
}

void DF_SHAKE_ADDz(int num, int addz)
{
    struct_285ba98f *p = &shake[num - 1];
    if (num > 0 && p->frame != 0) p->addz = addz;
}

void DF_SHAKE_Kill(int num)
{
    struct_285ba98f *p = &shake[num - 1];
    if (num > 0 && p->frame != 0) p->frame = 0;
}

int DF_SHAKE_CheckFree(void *adr)
{
    return ((struct_285ba98f *)adr)->frame == 0;
}

/* Projection on the live VU0 registers (DrawAfter's interpreted code runs after it): vf1-vf4 = WORLD_SCREEN_MATRIX,
 * ACC = 3-term sum, vf16 = ftoi4 result, vf26 = r * q, Q = 1 / r.w. No behind-camera test (w <= 0 projects
 * anywhere; Main's bounds / z tests reject most). */
void DF_SHAKE_Proc(void)
{
    PS2_FRAME(0xb0);
    PS2_STACK(int[4], shake_out, 0x90);
    PS2_STACK(float[4], pos, 0xa0);
    int chg = 0;
    if ((unsigned int)SHAKE_FRAME < FRAME && !(FRAME & 1)) chg = 1;
    auto *v = VU0();
    memcpy(&v->vf[1], WORLD_SCREEN_MATRIX, 64);
    struct_285ba98f *p = shake;
    for (int i = 0; i < 16; i++, p++) {
        if (p->frame == 0 || p->sw == 0) continue;
        if (p->frame != 0xffffffffu) p->frame--;
        if ((unsigned int)SHAKE_FRAME < FRAME && chg) p->time++;
        if (p->time >= 16) p->time = 0;
        pos[0] = p->pos[0];
        pos[1] = p->pos[1];
        pos[2] = p->pos[2];
        pos[3] = 1.0f;
        float r[4];
        for (int k = 0; k < 4; k++) {
            float a = v->vf[1].f[k] * pos[0];
            a = a + v->vf[2].f[k] * pos[1];
            a = a + v->vf[3].f[k] * pos[2];
            v->acc[k] = a;
            r[k] = a + v->vf[4].f[k] * pos[3];
        }
        v->q = ps2_div(1.0f, r[3]);
        for (int k = 0; k < 4; k++) v->vf[26].f[k] = r[k] * v->q;
        for (int k = 0; k < 4; k++) v->vf[16].sw[k] = ps2_ftoi(v->vf[26].f[k] * 16.0f);
        memcpy(shake_out, v->vf[16].sw, 16);
        shake_out[0] = (shake_out[0] - 0x7000) >> 4;
        shake_out[1] = (shake_out[1] - 0x7900) >> 4;
        shake_out[2] = shake_out[2] + p->addz;
        if (shake_out[2] >= 0x1000000) shake_out[2] = 0xffffff;
        DF_SHAKE_Main(shake_out, p->time);
    }
    SHAKE_FRAME = FRAME;
}

/* new source at pos for `frame` frames (0xffffffff = endless), switched off; returns its handle or 0 */
int DF_SHAKE_AppearShake(float *pos, unsigned int frame)
{
    PS2_FRAME(0x50);
    int idx = -1;
    struct_285ba98f *p =
        (struct_285ba98f *)DF_GetFreeBuffer(&ShakeStTop, shake, 0x20, 0x10, GFN(DF_SHAKE_CheckFree));
    if (p) {
        idx = ShakeStTop;
        ShakeStTop = idx + 1;
        if (!(ShakeStTop < 0x10)) ShakeStTop = 0;
    }
    if (!p) return 0;
    ZeroMemory32b(p, 8);
    p->pos[0] = pos[0];
    p->pos[1] = pos[1];
    p->pos[2] = pos[2];
    p->frame = frame;
    p->sw = 0;
    return idx + 1;
}

void DF_SHAKE_Init(void)
{
    PS2_FRAME(0x10);
    shake = (struct_285ba98f *)Memm_Alloc(1, 0x200, 0x10);
    ZeroMemory32b(shake, 0x80);
    shake_buffer = Memm_Alloc(1, 0x24240, 0x10);
    NL_ReadData(0xc, shake_buffer, -1, 0);
    unsigned int *offs = (unsigned int *)shake_buffer;
    for (int i = 0; i < 16; i++) {
        bokashi[i] = 0;
        if (offs[i]) bokashi[i] = (char *)shake_buffer + offs[i];
    }
    ShakeStTop = 0;
    SHAKE_FRAME = 0;
    output_frame = -1;
    before_texture = -1;
}

/* ---------------------------------------------------------------- render part (phase 2 group 9b) */

/* one textured sprite (TEX0_2, CTXT2, ABE, FST) as a 7-qword DMA chain node (tag id `id`) at gp0; returns the end.
 * ADC (no drawing kick) is set when the rect is off screen or empty. The x0 + 0x6fe / x1 + 0x702 (y + 0x78e / 0x792)
 * asymmetry is the original's. */
unsigned int *DF_SHAKE_SpriteDraw(unsigned int *gp0, int u, int v, int w, int h, int x0, int y0, int x1, int y1, int z,
                                  unsigned long long tex0, unsigned int id)
{
    PS2_FRAME(0x90);
    unsigned int adc = 0;
    if (!(x0 < 0x1ff) || !(y0 < 0xdf) || x1 < 0 || y1 < 0 || x0 == x1 || y0 == y1) adc = 0x8000;
    unsigned int *p = MakeDMA(gp0, 6, id);
    p = MakeGIF(p, 1, 1, 0x35e, 0, 5, 0x5353e);   /* A+D, UV, XYZF2, UV, XYZF2 */
    p = MakeREGS(p, 7, tex0);                    /* TEX0_2 */
    p = MakePACKET(p, (unsigned int)(u << 4), (unsigned int)(v << 4), 0, 0);
    int zf = z >> 4;
    p = MakePACKET(p, (unsigned int)((x0 + 0x6fe) << 4), (unsigned int)((y0 + 0x78e) << 4), (unsigned int)zf, adc);
    p = MakePACKET(p, (unsigned int)((u + w) << 4), (unsigned int)((v + h) << 4), 0, 0);
    return MakePACKET(p, (unsigned int)((x1 + 0x702) << 4), (unsigned int)((y1 + 0x792) << 4), (unsigned int)zf, adc);
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper (gen_bridge stops at 8 int args); y1 / z / tex0 / id are the caller's stack args at
 * sp+0 / 8 / 0x10 / 0x18 */
static void bw__DF_SHAKE_SpriteDraw(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    unsigned int *r = DF_SHAKE_SpriteDraw((unsigned int *)bridge_ptr(c->gpr[A0].uw[0]), c->gpr[A1].sw[0],
                                          c->gpr[A2].sw[0], c->gpr[A3].sw[0], c->gpr[T0].sw[0], c->gpr[T1].sw[0],
                                          c->gpr[T2].sw[0], c->gpr[T3].sw[0], (int)rd32(sp), (int)rd32(sp + 8),
                                          rd64(sp + 0x10), rd32(sp + 0x18));
    c->gpr[V0].sd[0] = (s32)PS2PTR(r);
}
#endif

/* D2 chain kick of a packet (original store order QWC 0, MADR 0, TADR, FlushCache, CHCR 0x145) */
static void shake_kick(const void *p)
{
    FlushCache(0);
    ps2_dma_kick_qmt(2, 0x145, 0, 0, PS2PTR(p));
}

/* copies the framebuffer rect xy (x0, y0, x1, y1; clamped to 0..0x1fe x 0..0xde) to VRAM 0x3c40 (CT32, BW 2) at the
 * offset the clamp cut off; size is unused. Returns with the DMA running (no trailing sync). */
static void DF_SHAKE_Forward(int *xy, int size)
{
    (void)size;
    PS2_FRAME(0xf0);
    PS2_STACK(unsigned int[28], pkt, 0x60);
    PS2_STACK(int[4], r, 0xd0);
    PS2_STACK(int[2], d, 0xe8);
    r[0] = xy[0];
    r[1] = xy[1];
    r[2] = xy[2];
    r[3] = xy[3];
    d[0] = 0;
    d[1] = 0;
    struct struct_c63aaab1 *f = DF_GetCurDrawFrame();
    if (!(r[0] < 0x1ff && r[1] < 0xdf && r[2] >= 0 && r[3] >= 0)) return;
    int x0 = r[0] > 0 ? r[0] : 0;
    int y0 = r[1] > 0 ? r[1] : 0;
    int x1 = r[2] < 0x1fe ? r[2] : 0x1fe;
    int y1 = r[3] < 0xde ? r[3] : 0xde;
    int w = x1 - x0;
    int h = y1 - y0;
    if (w == 0 || h == 0) return;
    if (r[0] < 0) d[0] = d[0] - r[0];
    if (r[1] < 0) d[1] = d[1] - r[1];
    unsigned int *p = MakeDMA(pkt, 6, 7);
    p = MakeGIF(p, 5, 1, 0, 0, 1, 0xe);
    p = MakeREGS(p, 0x3f, 0);   /* TEXFLUSH */
    unsigned int fbw = ((const unsigned char *)f)[2];
    unsigned int fbp = *(const unsigned short *)f;
    p = MakeREGS(p, 0x50, 0x23c40ull << 32 | (unsigned long long)((fbw & 0x3f) << 16 | (fbp & 0x1ff) << 5));
    p = MakeREGS(p, 0x51, (unsigned long long)(long long)d[1] << 48 | (unsigned long long)(long long)d[0] << 32
                              | (unsigned long long)(long long)y0 << 16 | (unsigned long long)(long long)x0);
    p = MakeREGS(p, 0x52, (unsigned long long)(long long)h << 32 | (unsigned long long)(long long)w);
    MakeREGS(p, 0x53, 2);   /* TRXDIR local -> local */
    sceGsSyncPath(0, 0);
    PS2_RQ_SCOPE(RQ_K_POST, RQ_OBJID(RQ_OC_TEXTURE, 0x3c40), 0);   /* render tag (host only): framebuffer copy */
    shake_kick(pkt);
}

/* TEXFLUSH-only packet (3 qwords) */
static void shake_texflush(unsigned int *pkt)
{
    unsigned int *p = MakeDMA(pkt, 2, 7);
    p = MakeGIF(p, 1, 1, 0, 0, 1, 0xe);
    MakeREGS(p, 0x3f, 0);
}

/* uploads bokashi texture `time` (128x64 PSMT8 at 0x3b40 + its 16x16 CT32 CLUT at 0x3f80) unless it is already there
 * this frame */
static void DF_SHAKE_TextureOut(unsigned int time)
{
    PS2_FRAME(0xf0);
    char *tex = (char *)bokashi[time];
    if (!((unsigned int)output_frame < FRAME) && (int)before_texture == (int)time) return;
    before_texture = (char)time;
    output_frame = (int)FRAME;
    PS2_STACK(unsigned int[12], pa, 0x60);
    PS2_STACK(struct_cb63eb70, li, 0x90);   /* sceGsLoadImage: built and sent by the SDK */
    PS2_STACK(unsigned int[12], pb, 0x30);
    PS2_RQ_SCOPE(RQ_K_UPLOAD, RQ_OBJID(RQ_OC_TEXTURE, 0x3b40), 0x13);   /* render tag (host only) */
    sceGsSyncPath(0, 0);
    shake_texflush(pa);
    sceGsSyncPath(0, 0);
    shake_kick(pa);
    sceGsSyncPath(0, 0);
    sceGsSetDefLoadImage(&li, 0x3b40, 2, 0x13, 0, 0, 0x80, 0x40);
    FlushCache(0);
    char *img = tex + 0x20;
    sceGsExecLoadImage(&li, img);
    sceGsSyncPath(0, 0);
    {
        PS2_RQ_SCOPE(RQ_K_UPLOAD, RQ_OBJID(RQ_OC_TEXTURE, 0x3f80), 0);   /* render tag (host only): CLUT */
        sceGsSetDefLoadImage(&li, 0x3f80, 1, 0, 0, 0, 0x10, 0x10);
        FlushCache(0);
        sceGsExecLoadImage(&li, img + ((int *)tex)[1]);
    }
    sceGsSyncPath(0, 0);
    shake_texflush(pb);
    sceGsSyncPath(0, 0);
    shake_kick(pb);
    sceGsSyncPath(0, 0);
}

/* draws the bokashi texture scaled to size x size/2 into the 128x64 render target 0x3bc0 (cleared to 0), Z writes off */
static void DF_SHAKE_Render(int size)
{
    PS2_FRAME(0xf0);
    PS2_STACK(unsigned int[36], pkt, 0x30);
    PS2_STACK(int[4], col, 0x20);
    unsigned int *p = MakeDMA(pkt, 4, 7);
    p = MakeGIF(p, 3, 1, 0x116, 0, 1, 0xe);
    p = MakeREGS(p, 8, 5);                              /* CLAMP_1 */
    p = MakeREGS(p, 0x47, 0x30000);                     /* TEST_1 */
    MakeREGS(p, 0x4e, 1ull << 32 | 0x3100008cull);      /* ZBUF_1: ZBP 0x8c, PSMZ24, ZMSK */
    sceGsSyncPath(0, 0);
    {
        PS2_RQ_SCOPE(RQ_K_POST, RQ_OBJID(RQ_OC_TEXTURE, 0x3bc0), 0);   /* render tag (host only): RT pass state */
        shake_kick(pkt);
    }
    col[0] = col[1] = col[2] = col[3] = 0;
    sceGsSyncPath(0, 0);
    DF_SetFrameBufferForTex(0x80, 0x40, 0, 0x3bc0, 1, col);
    sceGsSyncPath(0, 0);
    p = MakeDMA(pkt, 8, 7);
    p = MakeGIF(p, 1, 1, 0x116, 0, 7, 0x0e5353e1);      /* RGBAQ, A+D, UV, XYZ2, UV, XYZ2, A+D */
    p = MakePACKET(p, 0x80, 0x80, 0x80, 0x80);
    p = MakeREGS(p, 6, 0x8007f0059d30bb40ull);          /* TEX0_1: 0x3b40 BW 2 PSMT8, CLUT 0x3f80 */
    p = MakePACKET(p, 0, 0, 0, 0);
    p = MakePACKET(p, 0x7c00, 0x7e00, 0x10, 0);
    p = MakePACKET(p, 0x800, 0x400, 0, 0);
    p = MakePACKET(p, (unsigned int)((size + 0x7c0) << 4), (unsigned int)((size / 2 + 0x7e0) << 4), 0x10, 0);
    MakeREGS(p, 0x4e, 0x3100008c);                      /* ZBUF_1: ZMSK off */
    sceGsSyncPath(0, 0);
    shake_kick(pkt);
    sceGsSyncPath(0, 0);
    DF_RepairFrameBuffer();
    sceGsSyncPath(0, 0);
}

/* one source at screen position pos0 (x, y, z) with bokashi frame `time`: size from z (max 0x80), rect size x size/2
 * centred on it, skipped when z <= 0 or off screen; packets alternate between the two halves of buffer0 */
static void DF_SHAKE_Main(int *pos0, unsigned int time)
{
    PS2_FRAME(0xb0);
    PS2_STACK(int[4], xy, 0xa0);
    if (pos0[2] <= 0) return;
    int size = ps2_ftoi(ps2_div(128.0f * (float)pos0[2], 1999999.0f));
    if (size > 0x80) size = 0x80;
    int half = size / 2;
    xy[0] = pos0[0] - half;
    xy[1] = pos0[1] - size / 4;
    xy[2] = xy[0] + size;
    xy[3] = xy[1] + half;
    if (!(xy[0] < 0x1ff && xy[1] < 0xdf && xy[2] > 0 && xy[3] > 0)) return;
    DF_SHAKE_Forward(xy, size);
    DF_SHAKE_TextureOut(time);
    DF_SHAKE_Render(size);
    unsigned int *buf = (unsigned int *)(DF_SHAKE_Main__buffer0 + (DF_SHAKE_Main__sw << 9));
    unsigned int *p = MakeDMA(buf, 7, 1);
    p = MakeGIF(p, 6, 1, 0, 0, 1, 0xe);
    p = MakeREGS(p, 0x3f, 0);                                       /* TEXFLUSH */
    p = MakeREGS(p, 0x50, 0x1b023c40ull << 32 | 0x1b023bc0ull);     /* BITBLTBUF RT 0x3bc0 -> 0x3c40 alpha, PSMT8H */
    p = MakeREGS(p, 0x51, 0);                                       /* TRXPOS */
    p = MakeREGS(p, 0x52, (unsigned long long)(long long)half << 32 | (unsigned long long)(long long)size);
    p = MakeREGS(p, 0x53, 2);                                       /* TRXDIR */
    p = MakeREGS(p, 0x3f, 0);
    p = MakeDMA(p, 5, 1);
    p = MakeGIF(p, 1, 1, 0x356, 0, 4, 0x1eee);
    p = MakeREGS(p, 9, 5);                                          /* CLAMP_2 */
    p = MakeREGS(p, 0x48, 0x71001);                                 /* TEST_2 */
    p = MakeREGS(p, 0x43, 0x44);                                    /* ALPHA_2 */
    p = MakePACKET(p, 0x80, 0x80, 0x80, 0x80);
    /* outgoing stack args of the 12-argument call (sd: y1, z, tex0, id) */
    PS2_STACK(long long, a_y1, 0x0);
    PS2_STACK(long long, a_z, 0x8);
    PS2_STACK(unsigned long long, a_tex0, 0x10);
    PS2_STACK(long long, a_id, 0x18);
    a_y1 = xy[3];
    a_z = pos0[2];
    a_tex0 = 0x59c00bc40ull;   /* TEX0_2: 0x3c40 BW 2 CT32 TW 7 TH 6 TCC 1 */
    a_id = 7;
    (void)a_y1; (void)a_z; (void)a_id;
    DF_SHAKE_SpriteDraw(p, 0, 0, size, half, xy[0], xy[1], xy[2], xy[3], pos0[2], a_tex0, 7);
    sceGsSyncPath(0, 0);
    {
        PS2_RQ_SCOPE(RQ_K_POST, RQ_OBJID(RQ_OC_TEXTURE, 0x3c40), 1);   /* render tag (host only): alpha merge + sprite */
        shake_kick(buf);
    }
    DF_SHAKE_Main__sw = DF_SHAKE_Main__sw == 0;
}
