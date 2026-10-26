/* src/gfx/scr_eff.cpp — full-screen post effects ("screen effect presets"). s_screff holds 12 slots; ScrEff_SetPreset
 * binds a slot to a kind (draw function + cleared parameters), the stage code (__SetScreffParam) fills the parameters
 * from map data and switches slots on. Every flip, Draw_FlipBuffer* calls ScrEff_DrawEffect (runs the enabled slots'
 * GS lists, drawn into the draw buffer FBP 0x38) and then ScrEff_DrawDefault (copies the 512x224 draw buffer into the
 * display buffer FBP 0 with the GIF list __MakeDrawToDispDl built at boot, then sets DISPFB / DISPLAY 1 + 2).
 * The slots store PS2 function addresses (GFN) and are called through gcall (DrawEffect wraps each in a POST slot
 * tag scope). Tone and Glare first render into the 8x8 / glare page FBP 0x19a (TBP 0x3340), tagged as that texture,
 * then reopen the slot scope for the pass that samples it. */
#include "acnb/local/src/gfx/scr_eff.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"

#define SCREFF_PI 0x1.921fb6p+1f      /* 0x40490fdb */
#define SCREFF_2PI 0x1.921fb6p+2f     /* 0x40c90fdb */

/* ALPHA_1 from a preset's alpha fields (A B C D FIX) */
static unsigned long long screff_alpha(const StScreenEffectAlpha *a)
{
    return (unsigned long long)a->a | (unsigned long long)a->b << 2 | (unsigned long long)a->c << 4 |
           (unsigned long long)a->d << 6 | (unsigned long long)a->f << 32;
}

/* the 512x224 screen rectangle (x / y in 12.4 after XYOFFSET 0x7000 / 0x7900) */
static void screff_rect(int rect[2][4])
{
    rect[0][0] = 0x7000; rect[0][1] = 0x7900; rect[0][2] = 0xffffff; rect[0][3] = 0;
    rect[1][0] = 0x7000 + 0x2000; rect[1][1] = 0x7900 + 0xe00; rect[1][2] = 0xffffff; rect[1][3] = 0;
}

/* GS state every preset starts with: the draw buffer as frame, full-screen scissor and offset */
static void screff_frame_regs(void)
{
    GS_SetRegister(0x40, 0x00df000001ff0000ULL);   /* SCISSOR_1 512x224 */
    GS_SetRegister(0x4c, 0x80038);                 /* FRAME_1 FBP 0x38 FBW 8 CT32 */
    GS_SetRegister(0x18, 0x7900ULL << 32 | 0x7000);   /* XYOFFSET_1 */
}

/* the slot index of a preset's parameter block (= ScrEff_DrawEffect's loop index) */
static int screff_slot(const StScreenEffectParam *param)
{
    return (int)(((const char *)param - (const char *)&s_screff.set[0].param) / (int)sizeof(StScreenEffectSet));
}

/* render-target pass done: close its texture tag; scopes nest, so this falls back to the POST slot scope
 * ScrEff_DrawEffect opened (pushing the slot scope again here leaked one scope per call, s75 g7b verify) */
static void screff_slot_scope(int)
{
    ps2_rq_tag_end();
}

void ScrEff_SetObEffectParam(int (*prgba)[4])
{
    s_prgbaObEff = prgba;
}

StScreenEffectParam *ScrEff_GetPresetParam(int index)
{
    return &s_screff.set[index].param;
}

void ScrEff_SetPresetStatus(int index, int on)
{
    if (s_screff.set[index].func) s_screff.set[index].is = (unsigned char)on;
}

int ScrEff_GetPresetKind(int index)
{
    return s_screff.set[index].kind;
}

void ScrEff_SetPreset(int index, int kind)
{
    PS2_FRAME(0x10);
    typedef void (*Fn)(StScreenEffectParam *);
    Fn f;
    switch (kind) {
    case 0: f = 0; break;
    case 1: f = (Fn)GFN(__DrawPresetDefault); break;   /* (takes no parameter, called with the slot) */
    case 2: f = GFN(__DrawPresetDiffuse); break;
    case 3: f = GFN(__DrawPresetGlare); break;
    case 4: f = GFN(__DrawPresetSelf); break;
    case 5: f = GFN(__DrawPresetBlur); break;
    case 6: f = GFN(__DrawPresetTone); break;
    case 7: f = GFN(__DrawPresetIlluminate); break;
    case 8: f = GFN(__DrawPresetNoise); break;
    default: return;   /* (unsigned kind >= 9) */
    }
    StScreenEffectSet *s = &s_screff.set[index];
    s->func = f;
    s->kind = (unsigned char)kind;
    s->is = 0;
    memset(&s->param, 0, 0x50);
}

/* Copy the draw buffer to the display buffer and show it on both display circuits. */
void ScrEff_DrawDefault(void)
{
    PS2_FRAME(0x10);
    /* render target: the copy makes display buffer TBP 0, which Blur samples next frame */
    ps2_rq_tag_scope(RQ_K_POST, RQ_OBJID(RQ_OC_TEXTURE, 0), 0);
    GifPath3KickMem2((unsigned int *)s_dlDrawScreen, (int)s_qwcDlSize);
    ps2_dma_wait_gif();
    ps2_rq_tag_end();   /* the display registers belong to the frame */
    ps2_gs_priv_write64(0x12000070u, 0x1000);                  /* DISPFB1: FBP 0 FBW 8 CT32 */
    ps2_gs_priv_write64(0x12000080u, 0x001bf9ff0203227cULL);   /* DISPLAY1: DX 636 DY 50 MAGH 4 DW 2559 DH 447 */
    ps2_gs_priv_write64(0x12000090u, 0x1000);                  /* DISPFB2 */
    ps2_gs_priv_write64(0x120000a0u, 0x001bf9ff0203227cULL);   /* DISPLAY2 */
}

/* Run the enabled presets. Slot 3's alpha takes the OB part's delta every frame (accumulates, clamped 0..0x80);
 * s_prgbaObEff is only cleared when screen effects are on. */
void ScrEff_DrawEffect(void)
{
    PS2_FRAME(0x30);
    if (!StgConf.draw_screff) return;
    if (s_prgbaObEff) {
        int *a = &s_screff.set[3].param.color[0][3];
        *a = *a + (*s_prgbaObEff)[3];
        if (*a > 0x80) *a = 0x80;
        else if (*a < 0) *a = 0;
    }
    for (int i = 0; i < 12; i++) {
        StScreenEffectSet *s = &s_screff.set[i];
        if (s->is) {
            PS2_RQ_SCOPE(RQ_K_POST, RQ_OBJID(RQ_OC_NONE, s->kind), 0x100u + (unsigned int)i);   /* sub 0x100 + slot (lens flare = 2) */
            gcall(s->func, &s->param);
        }
    }
    s_prgbaObEff = 0;
}

static void screff_color(StScreenEffectParam *p, int k, int r, int g, int b, int a)
{
    p->color[k][0] = r;
    p->color[k][1] = g;
    p->color[k][2] = b;
    p->color[k][3] = a;
}

static void screff_alpha_set(StScreenEffectParam *p, int k, int a, int b, int c, int d, int f)
{
    p->alpha[k].a = (unsigned char)a;
    p->alpha[k].b = (unsigned char)b;
    p->alpha[k].c = (unsigned char)c;
    p->alpha[k].d = (unsigned char)d;
    p->alpha[k].f = (unsigned char)f;
}

/* Boot: clear the slots, bind slots 4..11 to their presets (all off), build the draw -> display copy, set the
 * presets' default parameters. */
void ScrEff_Init(void)
{
    PS2_FRAME(0x50);
    ZeroMemory8b(&s_screff, 0x480);
    ScrEff_SetPreset(4, 4);    /* Self */
    ScrEff_SetPreset(5, 3);    /* Glare */
    ScrEff_SetPreset(6, 4);    /* Self */
    ScrEff_SetPreset(7, 2);    /* Diffuse */
    ScrEff_SetPreset(8, 7);    /* Illuminate */
    ScrEff_SetPreset(9, 8);    /* Noise */
    ScrEff_SetPreset(10, 6);   /* Tone */
    ScrEff_SetPreset(11, 5);   /* Blur */
    __MakeDrawToDispDl();

    StScreenEffectParam *p = &s_screff.set[4].param;
    screff_color(p, 0, 0x7f, 0x7f, 0x7f, 0x80);
    screff_alpha_set(p, 0, 0, 1, 0, 1, 0);

    p = &s_screff.set[5].param;
    screff_color(p, 0, 0x7f, 0x7f, 0x7f, 0x80);
    screff_color(p, 1, 0x7f, 0x7f, 0x7f, 0x3f);
    screff_color(p, 2, 0x7f, 0x7f, 0x7f, 0x3f);
    screff_alpha_set(p, 0, 0, 1, 1, 2, 0);
    screff_alpha_set(p, 1, 0, 2, 0, 1, 0);
    p->value[0] = 0xff; p->value[1] = 0x7f; p->value[2] = 4; p->value[3] = 1;

    p = &s_screff.set[6].param;
    screff_color(p, 0, 0x7f, 0x7f, 0x7f, 0x80);
    screff_alpha_set(p, 0, 0, 1, 0, 1, 0);

    p = &s_screff.set[7].param;
    screff_color(p, 0, 0x80, 0x80, 0x80, 0x80);
    screff_color(p, 1, 0x80, 0x64, 0x10, 0x80);
    screff_alpha_set(p, 0, 1, 2, 2, 0, 0x80);
    screff_alpha_set(p, 1, 0, 2, 0, 1, 0);
    p->value[0] = 0x96;

    p = &s_screff.set[8].param;
    screff_color(p, 0, 0x80, 0x80, 0x80, 0x80);
    screff_color(p, 1, 0x80, 0x80, 0x80, 0x80);
    screff_alpha_set(p, 0, 1, 2, 2, 0, 0x80);
    screff_alpha_set(p, 1, 0, 2, 0, 1, 0);
    p->value[0] = 0x80; p->value[1] = 0x80; p->value[2] = 0x64;

    p = &s_screff.set[9].param;
    screff_color(p, 0, 0x80, 0x80, 0x80, 0x40);
    screff_alpha_set(p, 0, 1, 0, 0, 1, 0);
    p->value[0] = 0x80; p->value[1] = 0x70;

    p = &s_screff.set[10].param;
    screff_color(p, 0, 0x40, 0x40, 0x80, 0x80);
    screff_color(p, 1, 0x80, 0x80, 0x80, 0x40);
    screff_alpha_set(p, 0, 0, 1, 0, 1, 0);
    p->value[0] = 0x7f; p->value[1] = 0x7f;

    p = &s_screff.set[11].param;
    screff_color(p, 0, 0x80, 0x80, 0x80, 0x40);
    screff_alpha_set(p, 0, 0, 1, 0, 1, 0);
    p->value[0] = 0x3f; p->value[1] = 0x3f; p->value[2] = 0x3f; p->value[3] = 0x3f;
}

/* Noise: a random window (origin 0..127 texels per frame) of the 4-bit noise texture (TBP 0x2fe0, CLUT 0x3ef3)
 * stretched over the screen; value[0] / value[1] set the window size. */
static void __DrawPresetNoise(StScreenEffectParam *param)
{
    PS2_FRAME(0x70);
    __DrawPresetNoise__s_irand = (int)(((unsigned int)__DrawPresetNoise__s_irand * 85u + 0x187u) & 0xffffffu);
    int r1 = __DrawPresetNoise__s_irand & 0x7f;
    __DrawPresetNoise__s_irand = (int)(((unsigned int)__DrawPresetNoise__s_irand * 85u + 0x187u) & 0xffffffu);
    int r0 = __DrawPresetNoise__s_irand & 0x7f;
    GS_NewList();
    GS_SetTEXFLUSH();
    GS_SetRegister(8, 0);                           /* CLAMP_1 repeat */
    GS_SetRegister(0x47, 0x31003);
    GS_SetRegister(0x42, screff_alpha(&param->alpha[0]));
    GS_SetRegister(6, 0x8007de65dd40afe0ULL);       /* TEX0_1: TBP 0x2fe0 TBW 2 T4, CBP 0x3ef3 */
    GS_SetRegister(0x14, 0x60);
    int rect[2][4];
    screff_rect(rect);
    int uv[4];
    uv[0] = r1 << 4;
    uv[1] = r0 << 4;
    uv[2] = uv[0] + (param->value[0] * 4 + 1) * 16;
    uv[3] = uv[1] + (param->value[1] * 4 + 1) * 16;
    GS_DrawRectStripUvN(rect, uv, param->color[0], 1, 1);
    GS_FlushList();
    ps2_dma_wait_gif();
}

/* Illuminate (1 view only): pass 1 maps an 8-bit light texture over the screen, its st window scaled by value[0..2]
 * and scrolled by the camera yaw (value[3] = 0 / 1: texture 0x3080 with CLUT 0x3f00 / 0x3f04; 2: texture 0x30c0, CLUT
 * 0x3ed0, fixed st); pass 2 blends the draw buffer onto itself.
 * Quirk: other value[3] values leave TBP / CBP unassigned - the original uses the stale s2 / s1 (s1 = DrawEffect's
 * slot index, s2 = its caller's s2) and no yaw. */
static void __DrawPresetIlluminate(StScreenEffectParam *param)
{
    PS2_FRAME(0xd0);
    float st[4] = {0.f, 0.f, 1.f, 1.f};
    if (ViewNum != 1) return;
    CameraSt *cam = &CAM[ViewCamID[0]];
    float ang = 0.f;
    int tbp, cbp;
    unsigned char kind = param->value[3];
    if (kind == 2) {
        tbp = 0x30c0;
        cbp = 0x3ed0;
        st[0] = ang; st[1] = ang; st[2] = 1.f; st[3] = 1.f;
    } else {
        float sx = 0x1.0624dep-9f * (128.f - (float)param->value[0]);   /* 0.002f */
        float sy = 0x1.0624dep-9f * (128.f - (float)param->value[1]);
        float sz = 0x1.0624dep-10f * (float)param->value[2];            /* 0.001f */
        if (kind == 1) {
            tbp = 0x3080; ang = cam->ang[0]; cbp = 0x3f04;
        } else if (kind == 0) {
            tbp = 0x3080; ang = cam->ang[0]; cbp = 0x3f00;
        } else {
            cbp = screff_slot(param);          /* stale s1 */
            tbp = (int)PS2_STALE_GPR(S2);     /* stale s2 */
        }
        float lo = 0.5f - sz;
        ang = ps2_div(ang, SCREFF_2PI);
        float hi = 0.5f + sz;
        st[1] = (lo + sy) - ang;
        st[3] = (hi + sy) - ang;
        st[0] = 0.f + (lo + sx);
        st[2] = 0.f + (hi + sx);
    }
    GS_NewList();
    screff_frame_regs();
    GS_SetTEXFLUSH();
    GS_SetRegister(8, 5);
    GS_SetRegister(0x47, 0x31003);
    GS_SetRegister(0x42, screff_alpha(&param->alpha[0]));
    GS_SetRegister(6, (unsigned long long)(long long)tbp | 5ULL << 32 | 0xdd308000ULL |
                      (unsigned long long)(long long)cbp << 37 | 1ULL << 63);   /* TEX0_1: TBW 2 T8 128x128, CLUT CT32 load */
    GS_SetRegister(0x14, 0x60);
    int rect[2][4];
    screff_rect(rect);
    GS_DrawRectStripStN(rect, st, param->color[0], 1, 1);
    GS_SetTEXFLUSH();
    GS_SetRegister(0x47, 0x31003);
    GS_SetRegister(0x42, screff_alpha(&param->alpha[1]));
    GS_SetRegister(6, 6ULL << 32 | 0x24020700);   /* TEX0_1: the draw buffer CT32 */
    GS_SetRegister(0x14, 0);
    screff_rect(rect);
    int uv[4] = {0, 0, 0x2010, 0xe10};
    GS_DrawRectStripUvN(rect, uv, param->color[1], 1, 1);
    GS_FlushList();
    ps2_dma_wait_gif();
}

/* Tone: fill the 8x8 tone page (FBP 0x19a) with color[0] (ALPHA 0x44 = (Cs - Cd) * As + Cd onto what it held), then
 * stretch it over the screen with color[1]. */
static void __DrawPresetTone(StScreenEffectParam *param)
{
    PS2_FRAME(0xc0);
    int slot = screff_slot(param);
    ps2_rq_tag_scope(RQ_K_POST, RQ_OBJID(RQ_OC_TEXTURE, 0x3340), (unsigned int)slot);   /* render target TBP 0x3340 */
    GS_NewList();
    GS_SetRegister(0x40, 0x0007000000070000ULL);   /* SCISSOR_1 8x8 */
    GS_SetRegister(0x4c, 0x1019a);                 /* FRAME_1 FBP 0x19a FBW 1 CT32 */
    GS_SetRegister(0x18, 0x7fc0ULL << 32 | 0x7fc0);
    GS_SetRegister(0x47, 0x31002);
    GS_SetRegister(0x42, 0x44);
    /* GS_Vertex4iv copies with lq (16-aligned source): the vertices at their original stack slots */
    PS2_STACK(int[4], v0, 0xa0);
    PS2_STACK(int[4], v1, 0xb0);
    v0[0] = 0x7fc0; v0[1] = 0x7fc0; v0[2] = 0xffffff; v0[3] = 0;
    v1[0] = v0[0] + 0x80; v1[1] = v0[1] + 0x80; v1[2] = v0[2]; v1[3] = v0[3];
    GS_Begin(6, 2, 0x41, 2);                        /* sprite, RGBAQ XYZ2 */
    GS_Color4iv(param->color[0]);
    GS_Vertex4iv(v0);
    GS_Color4iv(param->color[0]);
    GS_Vertex4iv(v1);
    GS_End();
    GS_FlushList();                                 /* (no wait) */
    screff_slot_scope(slot);
    GS_NewList();
    screff_frame_regs();
    GS_SetTEXFLUSH();
    GS_SetRegister(0x3b, 0x80ULL << 32 | 0x8080);   /* TEXA */
    GS_SetRegister(8, 5);
    GS_SetRegister(0x47, 0x31003);
    GS_SetRegister(0x42, screff_alpha(&param->alpha[0]));
    GS_SetRegister(6, 6ULL << 32 | 0x20107340);     /* TEX0_1: TBP 0x3340 TBW 1 CT24 256x256 */
    GS_SetRegister(0x14, 0x60);
    int rect[2][4];
    screff_rect(rect);
    int uv[4] = {0, 0, 0x80, 0x80};
    GS_DrawRectStripUvN(rect, uv, param->color[1], 1, 1);
    GS_FlushList();
    ps2_dma_wait_gif();
}

/* Blur: blend the previous displayed frame (display buffer, TBP 0, as CT24) over the draw buffer, its UVs inset by
 * value[0..3] (feedback trail). */
static void __DrawPresetBlur(StScreenEffectParam *param)
{
    PS2_FRAME(0x50);
    GS_NewList();
    screff_frame_regs();
    GS_SetTEXFLUSH();
    GS_SetRegister(0x3b, 0x80ULL << 32 | 0x8080);   /* TEXA */
    GS_SetRegister(0x47, 0x31003);                  /* TEST_1 */
    GS_SetRegister(0x42, screff_alpha(&param->alpha[0]));
    GS_SetRegister(6, 6ULL << 32 | 0x24120000);     /* TEX0_1: TBP 0 TBW 8 CT24 512x256 */
    GS_SetRegister(0x14, 0x60);                     /* TEX1_1: linear */
    int rect[2][4];
    screff_rect(rect);
    int uv[4] = {param->value[0] + 0x10, param->value[1] + 0x10, 0x2010 - param->value[2], 0xe10 - param->value[3]};
    GS_DrawRectStripUvN(rect, uv, param->color[0], 1, 1);
    GS_FlushList();
    ps2_dma_wait_gif();
}

/* Self: blend the draw buffer (as CT24) onto itself. */
static void __DrawPresetSelf(StScreenEffectParam *param)
{
    PS2_FRAME(0x50);
    GS_NewList();
    screff_frame_regs();
    GS_SetTEXFLUSH();
    GS_SetRegister(0x47, 0x31003);
    GS_SetRegister(0x42, screff_alpha(&param->alpha[0]));
    GS_SetRegister(6, 6ULL << 32 | 0x24120700);   /* TEX0_1: TBP 0x700 (the draw buffer) TBW 8 CT24 */
    GS_SetRegister(0x14, 0);
    int rect[2][4];
    screff_rect(rect);
    int uv[4] = {0, 0, 0x2010, 0xe10};
    GS_DrawRectStripUvN(rect, uv, param->color[0], 1, 1);
    GS_FlushList();
    ps2_dma_wait_gif();
}

/* Glare: shrink the draw buffer into a (value[0] + 1) x (value[1] + 1) image on page FBP 0x19a (cleared to color[0]
 * first), then stretch it back over the screen with color[2], plus up to 4 offset copies (value[3] >= 2..5, offset
 * value[2] pixels). Debug leftover: holding pad button 0x800 draws the stretched image opaque. */
static void __DrawPresetGlare(StScreenEffectParam *param)
{
    PS2_FRAME(0x100);
    int w = param->value[0] + 1;
    int h = param->value[1] + 1;
    long long fbw = (w + 63) / 64;
    if (w < 2 || h < 2) return;
    int slot = screff_slot(param);
    ps2_rq_tag_scope(RQ_K_POST, RQ_OBJID(RQ_OC_TEXTURE, 0x3340), (unsigned int)slot);   /* render target TBP 0x3340 */
    GS_NewList();
    GS_SetRegister(0x40, (unsigned long long)(long long)(w - 1) << 16 | (unsigned long long)(long long)(h - 1) << 48);
    GS_SetRegister(0x4c, (unsigned long long)fbw << 16 | 0x19a);
    int oy = 0x800 - h / 2;
    int ox = 0x800 - w / 2;
    GS_SetRegister(0x18, (unsigned long long)(long long)(oy << 4) << 32 | (unsigned long long)(long long)(ox << 4));
    GS_SetRegister(0x47, 0x31002);
    GS_SetRegister(0x42, 0x44);
    int rect[2][4];
    rect[0][0] = ox << 4; rect[0][1] = oy << 4; rect[0][2] = 0xffffff; rect[0][3] = 0;
    rect[1][0] = rect[0][0] + (w << 4); rect[1][1] = rect[0][1] + (h << 4); rect[1][2] = 0xffffff; rect[1][3] = 0;
    int uv[4] = {0, 0, 0x2000, 0xe00};
    GS_DrawRectStripUvN(rect, uv, param->color[0], 0, 0);   /* untextured fill */
    GS_SetTEXFLUSH();
    GS_SetRegister(0x47, 0x31003);
    GS_SetRegister(0x42, screff_alpha(&param->alpha[0]));
    GS_SetRegister(8, 5);
    GS_SetRegister(6, 6ULL << 32 | 0x24120700);    /* TEX0_1: the draw buffer as CT24 */
    GS_SetRegister(0x14, 0x60);
    GS_DrawRectStripUvN(rect, uv, param->color[1], 1, 1);
    GS_FlushList();                                /* (no wait) */
    screff_slot_scope(slot);
    GS_NewList();
    screff_frame_regs();
    GS_SetTEXFLUSH();
    GS_SetRegister(8, 5);
    GS_SetRegister(0x47, 0x31003);
    GS_SetRegister(0x42, screff_alpha(&param->alpha[1]));
    GS_SetRegister(6, (unsigned long long)fbw << 14 | (6ULL << 32 | 0x20103340));   /* TEX0_1: TBP 0x3340 TBW fbw CT24 */
    GS_SetRegister(0x14, 0x60);
    screff_rect(rect);
    uv[0] = 0; uv[1] = 0; uv[2] = w << 4; uv[3] = h << 4;
    int isAlpha = Pad_IsOn(0, 0x800) == 0;
    GS_DrawRectStripUvN(rect, uv, param->color[2], 1, isAlpha);
    unsigned char n = param->value[3];
    if (n >= 2) {
        GS_FlushList();
        ps2_dma_wait_gif();
        GS_NewList();
        rect[0][0] += param->value[2] << 4;
        rect[0][1] += param->value[2] << 4;
        rect[1][0] += param->value[2] << 4;
        rect[1][1] += param->value[2] << 4;
        GS_DrawRectStripUvN(rect, uv, param->color[2], 1, 1);
    }
    if (param->value[3] >= 3) {
        rect[0][0] -= param->value[2] << 5;
        rect[1][0] -= param->value[2] << 5;
        GS_DrawRectStripUvN(rect, uv, param->color[2], 1, 1);
    }
    if (param->value[3] >= 4) {
        GS_FlushList();
        ps2_dma_wait_gif();
        GS_NewList();
        rect[0][1] -= param->value[2] << 5;
        rect[1][1] -= param->value[2] << 5;
        GS_DrawRectStripUvN(rect, uv, param->color[2], 1, 1);
    }
    if (param->value[3] >= 5) {
        rect[0][0] += param->value[2] << 5;
        rect[1][0] += param->value[2] << 5;
        GS_DrawRectStripUvN(rect, uv, param->color[2], 1, 1);
    }
    GS_FlushList();
    ps2_dma_wait_gif();
}

/* Diffuse (lens flare maps, 1 view only): pass 1 maps the 8-bit flare texture over the screen, its st window
 * (size by value[0]) following the sun's direction relative to the view direction; pass 2 blends the draw buffer onto
 * itself. Without lens flare the window stays 0..1. */
static void __DrawPresetDiffuse(StScreenEffectParam *param)
{
    PS2_FRAME(0xb0);
    float st[4] = {0.f, 0.f, 1.f, 1.f};
    if (Array && Array->head.sysinfo.lensflareon && StgConf.lensflare && ViewNum == 1) {
        CameraSt *cam = &CAM[ViewCamID[0]];
        float size = 0x1.0624dep-10f * (float)param->value[0];   /* 0.001f */
        float dsun[4], dat[4], asun[4], aat[4];
        vu_sub_xyz(dsun, CopyOfSysInfo.lensflare.sun, cam->vec);
        vu_sub_xyz(dat, cam->at, cam->vec);
        PS2_VF_MIRROR(16, dat);
        PS2_VF_MIRROR(17, cam->vec);
        vec2ang(asun, dsun);
        vec2ang(aat, dat);
        float dy = aat[1] - asun[1];
        float dx = aat[0] - asun[0];
        /* (a NaN angle loops forever) */
        while (!(dy <= SCREFF_PI)) dy -= SCREFF_2PI;
        while (dy < -SCREFF_PI) dy += SCREFF_2PI;
        while (!(dx <= SCREFF_PI)) dx -= SCREFF_2PI;
        while (dx < -SCREFF_PI) dx += SCREFF_2PI;
        float u = ps2_div(dy, SCREFF_2PI);
        float lo = 0.5f - size;
        float v = ps2_div(dx, SCREFF_2PI);
        st[0] = lo + u;
        st[1] = lo - v;
        float hi = 0.5f + size;
        st[2] = hi + u;
        st[3] = hi - v;
    }
    GS_NewList();
    screff_frame_regs();
    GS_SetTEXFLUSH();
    GS_SetRegister(8, 0x4010);
    GS_SetRegister(0x47, 0x31003);
    GS_SetRegister(0x42, screff_alpha(&param->alpha[0]));
    GS_SetRegister(6, 0x8007d805dd30af40ULL);      /* TEX0_1: TBP 0x2f40 TBW 2 T8, CBP 0x3ec0 */
    GS_SetRegister(0x14, 0x60);
    int rect[2][4];
    screff_rect(rect);
    GS_DrawRectStripStN(rect, st, param->color[0], 1, 1);
    GS_SetTEXFLUSH();
    GS_SetRegister(0x47, 0x31003);
    GS_SetRegister(0x42, screff_alpha(&param->alpha[1]));
    GS_SetRegister(6, 6ULL << 32 | 0x24020700);    /* TEX0_1: the draw buffer CT32 */
    GS_SetRegister(0x14, 0);
    screff_rect(rect);
    int uv[4] = {0, 0, 0x2010, 0xe10};
    GS_DrawRectStripUvN(rect, uv, param->color[1], 1, 1);
    GS_FlushList();
    ps2_dma_wait_gif();
}

/* Default: the plain draw -> display copy without the display registers. */
static void __DrawPresetDefault(void)
{
    PS2_FRAME(0x10);
    GifPath3KickMem2((unsigned int *)s_dlDrawScreen, (int)s_qwcDlSize);
    ps2_dma_wait_gif();
}

static unsigned int *screff_ad(unsigned int *q, unsigned long long data, unsigned int reg)
{
    q[0] = (unsigned int)data;
    q[1] = (unsigned int)(data >> 32);
    q[2] = reg;
    q[3] = 0;
    return q + 4;
}

/* The draw -> display copy (64 qw): A+D setup (FRAME FBP 0 as CT24, offset, TEXFLUSH, CLAMP, TEST, TEX0 = the draw
 * buffer, TEX1 nearest) and one 18-vertex textured triangle strip of 9 columns 64 pixels wide. */
static void __MakeDrawToDispDl(void)
{
    unsigned int *q = (unsigned int *)s_dlDrawScreen;
    q[0] = 7; q[1] = 0x10004000; q[2] = 0xe; q[3] = 0;   /* GIF tag NLOOP 7, A+D */
    q += 4;
    q = screff_ad(q, 0x01080000, 0x4c);                  /* FRAME_1 */
    q = screff_ad(q, 0x7900ULL << 32 | 0x7000, 0x18);    /* XYOFFSET_1 */
    q = screff_ad(q, 1, 0x3f);                           /* TEXFLUSH */
    q = screff_ad(q, 5, 8);                              /* CLAMP_1 */
    q = screff_ad(q, 0x31002, 0x47);                     /* TEST_1 */
    q = screff_ad(q, 6ULL << 32 | 0x24020700, 6);        /* TEX0_1 */
    q = screff_ad(q, 0, 0x14);                           /* TEX1_1 */
    q[0] = 0x12; q[1] = 0x308a4000; q[2] = 0x413; q[3] = 0;   /* NLOOP 18, PRIM tri strip TME FST, UV RGBAQ XYZ2 */
    q += 4;
    int top[4] = {0x7000, 0x7900, 0xffffff, 0};
    int bot[4] = {0x7000, 0x8700, 0xffffff, 0};
    unsigned int u = 0;
    for (unsigned int i = 0; i < 9; i++) {
        q[0] = u; q[1] = 0; q[2] = 0; q[3] = 0;
        q[4] = 0x7f; q[5] = 0x7f; q[6] = 0x7f; q[7] = 0x7f;
        memcpy(q + 8, top, 16);
        q[12] = u; q[13] = 0xdf0; q[14] = 0; q[15] = 0;
        q[16] = 0x7f; q[17] = 0x7f; q[18] = 0x7f; q[19] = 0x7f;
        memcpy(q + 20, bot, 16);
        q += 24;
        u += 0x400;
        top[0] += 0x400;
        bot[0] += 0x400;
    }
    q[0] = 0x8000; q[1] = 0; q[2] = 0; q[3] = 0;   /* EOP */
    q += 4;
    s_qwcDlSize = (unsigned int)((char *)q - s_dlDrawScreen) >> 4;
}
