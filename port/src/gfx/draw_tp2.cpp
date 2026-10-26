/* src/gfx/draw_tp2.c - draws a TP2 model (the cockpit panel) with GSL immediate primitives instead of VU1: per object
 * the world-screen matrix (scratchpad 0x70000000, DF_SetWorldScreenMatrix) times the object matrix goes to
 * scratchpad 0x40 and VU0 vf1-4, every vertex is projected with gfxvu0_RotTransPers into a screen array (int4 + q),
 * vertices outside the Z range / the 16-bit screen window get bit 15 of word 3, and each primitive is sent as a
 * textured strip (ST from s, t * q) whose kicks are suppressed (GS_NoKick) while one of its last three vertices is
 * clipped. Prim colour = scratchpad 0x1a0 (DF_SetPrimColor). Only caller: stat/cockpit.cpp (not in Versus).
 * Render tag: the whole model is HUD element 8 (the cockpit's id in cockpit.cpp). */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"
#include "acnb/hooks.h"
#include "acnb/local/src/gfx/draw_tp2.h"

static void draw_primlist_tex(struct struct_de4d68f5 *php, int pnum, int (*scrv)[4], float *qp)
{
    PS2_FRAME(0x80);
    while (pnum--) {
        unsigned int n = php->pv_num;
        unsigned int prim = php->flag & 0x7ff;
        unsigned char *e = (unsigned char *)php + 0x20;
        unsigned int hist = 0;   /* clip flags of the last three vertices */
        GS_NewList();
        GS_Begin(prim | 0x40, (int)n, 0x412, 3);   /* + FST off, ST, RGBAQ, XYZ2 */
        while (n--) {
            unsigned int idx = *(unsigned short *)e;
            float q = qp[idx];
            int *sv = scrv[idx];
            hist = (hist << 1 & 7) | (unsigned int)(sv[3] & 0x8000) >> 15;
            GS_TexCoord3f(*(float *)(e + 8) * q, *(float *)(e + 0xc) * q, q);
            GS_Color4iv((int *)PS2_SPR(0x1a0));
            GS_Vertex4iv(sv);
            if (hist) GS_NoKick();
            e += 0x20;
        }
        GS_End();
        GS_FlushList();
        php = (struct struct_de4d68f5 *)e;
    }
}

/* Quirk kept: the vertex loop is a do-while on the 32-bit count, so v_num 0 would run 2^32 times */
static void draw_object(struct _objtbl *obj, float (*mat)[4])
{
    PS2_FRAME(0x28070);
    PS2_STACK(float[4], tmp, 0x60);
    PS2_STACK(float[0x2000], q, 0x70);
    PS2_STACK(int[0x2000][4], scr, 0x8070);
    auto *v = VU0();
    float (*ws)[4] = (float (*)[4])PS2_SPR(0);
    float (*cm)[4] = (float (*)[4])PS2_SPR(0x40);
    float a[4][4], o[4][4];
    memcpy(a, ws, 64);
    memcpy(o, PS2_LQ_SRC(mat), 64);
    for (int r = 0; r < 4; r++) {
        for (int i = 0; i < 4; i++) {
            float s = a[0][i] * o[r][0];
            s = s + a[1][i] * o[r][1];
            s = s + a[2][i] * o[r][2];
            v->acc[i] = s;
            v->vf[22 + r].f[i] = s + a[3][i] * o[r][3];
        }
    }
    for (int r = 0; r < 4; r++) {
        memcpy(cm[r], v->vf[22 + r].f, 16);
        memcpy(v->vf[1 + r].f, v->vf[22 + r].f, 16);
    }
    unsigned int cnt = obj->v_num;
    float (*vp)[4] = obj->vp;
    int i = 0;
    do {
        gfxvu0_RotTransPers(scr[i], tmp, vp[i]);
        q[i] = tmp[3];
        if (scr[i][2] < 0 || scr[i][2] >= 0x1000000 || ((scr[i][0] | scr[i][1]) & 0xffff0000)) scr[i][3] |= 0x8000;
        i++;
    } while (--cnt != 0);
    draw_primlist_tex(obj->php, obj->p_num, scr, q);
}

/* draws every object of mp (with the TOD remap table when kind bit 0: entry byte 2 = object index, 0xff = skip),
 * calling func(mp, index, argp) first when set */
void DF_DrawModel_TP2(struct StModel *mp, int (*func)(struct StModel *, int, void *), void *argp)
{
    PS2_FRAME(0x80);
    PS2_RQ_SCOPE(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 8), 8);   /* render tag (host only): cockpit panel */
    for (int i = 0, rt = 0, ob = 0; i < mp->nobj; i++, rt += 0x30, ob += 0x50) {
        int j;
        if (mp->kind & 1) {
            StXtod *tod = mp->tod;
            j = ((unsigned char *)tod->animtable[tod->nowtodid])[rt + 2];
            if (j == 0xff) continue;
        } else
            j = i;
        if (func) gcall(func, mp, j, argp);
        draw_object((struct _objtbl *)((char *)mp->otp + ob), *mp->op[j].wrmatp);
    }
}

void DF_SetPrimColor(int *rgba)
{
    memcpy(PS2_SPR(0x1a0), PS2_LQ_SRC(rgba), 16);
}

/* scratchpad 0x70000000 = the panel's view-screen matrix: scrz = 256 / tan(fovy * pi / 360), aspect 0.8 x 0.5,
 * centre 2048, Z 0 .. 0xffffff, near 1, far 65536 */
void DF_SetWorldScreenMatrix(float fovy)
{
    PS2_FRAME(0x20);
    float t = ps2_tanf(0x1.1df46ap-7f * fovy);
    float scrz = ps2_div(256.0f, t), ax = 0.8f;
    if (hook_view_screen) hook_view_screen(2, &ax, &scrz);   /* step 5.2 widescreen (NULL = original) */
    sceVu0ViewScreenMatrix((float (*)[4])PS2_SPR(0), scrz, ax, 0.5f, 2048.0f, 2048.0f, 0.0f, 16777215.0f, 1.0f, 65536.0f);
}
