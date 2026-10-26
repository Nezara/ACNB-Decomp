/* src/sfx/shimmer.cpp - full-screen "shimmer" (heat-wave) post effect driven by the stage script. The screen is cut
 * into divx * divy cells (16 x 14 after Init, 32 x 16 pixels each) and every cell is redrawn from the current draw
 * buffer (TBP 0x700, used as a texture) shifted by a per-cell offset from the GiziRand table, scaled by an amplitude
 * (dx / dy) that eases towards the target (wd / ht) while switching on (on = 1) and decays while switching off
 * (on = 3). State is the single manager s_ShimMng. SHIMMER_Switch / SHIMMER_SetAmp are only called from the OLSORTIE
 * script system (__scrsys_ShimmerOnOff / __scrsys_ShimmerSetAmp), so in Versus `on` stays 0 and SHIMMER_Draw returns
 * at the switch. Quirk kept: the (dead, mode 0) ST path scales s by 1/1024 although the texture is 512 wide.
 * Render tag: POST, RQ_OBJID(RQ_OC_NONE, 0) sub 4 (sub 1 screen fill, 2 lens flare, 3 blur). */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"
#include "acnb/local/src/sfx/shimmer.h"

#ifdef ACNB_SOURCEPORT
/* [1] = TBP 0x700 TBW 8 PSMCT32 TW 9 TH 8 TCC 1 CLD 4 (the draw buffer); [0] unused */
unsigned long long SHIMMER_Draw__tex0[2] = {0x8000000624120000ull, 0x8000000624020700ull};
#endif

static struct_1f802d8d shim_tex0(unsigned long long v)
{
    struct_1f802d8d t;
    memcpy(&t, &v, 8);
    return t;
}

void SHIMMER_Draw(void)
{
    PS2_FRAME(0x110);
    struct_9b7a4a1b *m = &s_ShimMng;
    alignas(16) int rgba[4] = {0x80, 0x80, 0x80, 0x40};
    switch (m->on) {
    case 3:   /* switching off: amplitude decays */
        m->dx = m->dx * (1.0f - m->rx);
        m->dy = m->dy * (1.0f - m->ry);
        if (m->dx < 0.0125f && m->dy < 0.0125f) m->on = 0;
        break;
    case 1: { /* switching on: amplitude eases towards the target */
        m->dx = m->dx + m->rx * (m->wd - m->dx);
        m->dy = m->dy + m->ry * (m->ht - m->dy);
        float d = m->wd - m->dx;
        if (d < 0.0f) d = -d;
        if (!(d < 0.0125f)) break;
        d = m->ht - m->dy;
        if (d < 0.0f) d = -d;
        if (!(d < 0.0125f)) break;
        m->on = 2;
        break;
    }
    case 2:
        break;
    default:
        return;
    }

    /* z test (GEQUAL) only when a depth is set, else ZTST always; ALPHA_1 = (Cs - Cd) * As + Cd */
    unsigned long long test = (m->dpth < 0xffffff) ? 0x70000 : 0x30000;
    unsigned long long tex0 = SHIMMER_Draw__tex0[1];
    int bias = (g_odevCur == 0) ? -8 : 0;
    PS2_RQ_SCOPE(RQ_K_POST, RQ_OBJID(RQ_OC_NONE, 0), 4);   /* render tag (host only): shimmer */
    sceGsSyncPath(0, 0);
    GS_NewList();
    GS_SetRegister(0x47, test);   /* TEST_1 */
    GS_SetRegister(0x42, 0x11);   /* ALPHA_1 */
    GS_FlushList();

    alignas(16) float st[4];
    alignas(16) int xyz[2][4];
    alignas(16) int uv[4];
    for (int i = 0; i < m->divx; i++) {
        for (int j = 0; j < m->divy; j++) {
            float r = GiziRand[(unsigned int)(j + (int)(FRAME + (unsigned int)(2 * i))) & 0xff] - 0.5f;
            float x = m->sizx * (float)i;
            float y = m->sizy * (float)j;
            float ox = m->dx * r;
            float oy = m->dy * r;
            int X0 = (ps2_ftoi(x) + 0x700) << 4;
            int Y0 = (ps2_ftoi(y) + 0x790) << 4;
            xyz[0][0] = X0 - 8;
            xyz[0][1] = Y0 + bias;
            xyz[1][0] = X0 + ps2_ftoi(16.0f * m->sizx) - 8;
            xyz[1][1] = bias + (Y0 + ps2_ftoi(16.0f * m->sizy));
            xyz[1][2] = m->dpth;
            xyz[0][2] = xyz[1][2];
            xyz[1][3] = 0x10;
            xyz[0][3] = 0x10;
            st[0] = x + ox;
            st[1] = y + oy;
            st[2] = (x + m->sizx) + ox;
            st[3] = (y + m->sizy) + oy;
            if (st[0] < 0.0f) st[0] = 0.0f;
            if (st[1] < 0.0f) st[1] = 0.0f;
            if (!(st[2] <= 512.0f)) st[2] = 512.0f;
            if (!(st[3] <= 224.0f)) st[3] = 224.0f;
            if (m->mode == 0) {
                st[0] = st[0] * (1.0f / 1024.0f);
                st[1] = st[1] * (1.0f / 256.0f);
                st[2] = st[2] * (1.0f / 1024.0f);
                st[3] = st[3] * (1.0f / 256.0f);
                DF_Draw2DSprite_ST(rgba, xyz, shim_tex0(tex0), st, 0);
            } else {
                uv[0] = ps2_ftoi(16.0f * st[0]);
                uv[1] = ps2_ftoi(16.0f * st[1]);
                uv[2] = ps2_ftoi(16.0f * st[2]);
                uv[3] = ps2_ftoi(16.0f * st[3]);
                DF_Draw2DSprite_UV(rgba, xyz, shim_tex0(tex0), uv, 0);
            }
        }
    }
}

/* -1: off at once, 0: fade out, 1: fade in; other values ignored */
void SHIMMER_Switch(int on)
{
    if (on == -1) s_ShimMng.on = 0;
    else if (on == 0) s_ShimMng.on = 3;
    else if (on == 1) s_ShimMng.on = 1;
}

/* quirk kept: current = target too, so the ease-in ends on the next draw */
void SHIMMER_SetAmp(float wd, float ht)
{
    s_ShimMng.wd = wd;
    s_ShimMng.dx = wd;
    s_ShimMng.on = 1;
    s_ShimMng.ht = ht;
    s_ShimMng.dy = ht;
}

void SHIMMER_Init(void)
{
    PS2_FRAME(0x10);
    memset(&s_ShimMng, 0, 0x30);
    s_ShimMng.mode = 1;
    s_ShimMng.divx = 16;
    s_ShimMng.divy = 14;
    s_ShimMng.wd = 3.0f;
    s_ShimMng.ht = 1.5f;
    s_ShimMng.rx = 0.0078125f;
    s_ShimMng.ry = 0.0078125f;
    s_ShimMng.dpth = 0xffffff;
    s_ShimMng.sizx = ps2_div(512.0f, (float)s_ShimMng.divx);
    s_ShimMng.sizy = ps2_div(224.0f, (float)s_ShimMng.divy);
}
