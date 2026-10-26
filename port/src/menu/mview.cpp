/* src/menu/mview.cpp — the menu's two cameras. MViewMng[2] (OLMENU): view 0 = MViewp (main menu view, scrz 1000),
 * view 1 = MWorkViewp (scrz 512, model work / AC viewer). Each holds the camera position / angles (cam), field of
 * view and the view_screen / world_screen / world_view matrices. A camera motion (menu data archive item 4, params
 * item 3) blends prev -> next over count_max frames with a sine ease (__ChangeView), optionally chaining the next
 * motion. DF_MVIEW_Play runs once per menu frame; DF_MVIEW_MakeVU1Packet* append the camera to the VU1 packet.
 * All native; the SDK matrix helpers (sceVu0*) are the SDK's, not the game's acvu0_*. */
#include <string.h>
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"
#include "acnb/hooks.h"

#ifdef ACNB_SOURCEPORT
struct struct_af7eae9d MViewMng[2];
#endif

#define MVIEW_D ps2_u2f(0x3c8efa35)   /* pi / 180 */
#define MVIEW_R ps2_u2f(0x42652ee0)   /* 180 / pi */
#define MVIEW_H ps2_u2f(0x3fc90fdb)   /* pi / 2 */

static inline void mview_put64(unsigned int *p, unsigned long long v)
{
    p[0] = (unsigned int)v;
    p[1] = (unsigned int)(v >> 32);
}

void DF_MVIEW_MakeWorkVU1Packet(void)
{
    PS2_FRAME(0x30);
    struct struct_af7eae9d *v = MWorkViewp;
    unsigned int *p = MPktCurrentp;
    __UpdateCamMatrix(v);
    p = DF_MPKT_MakeUnpack(p, 0x4a, 1, &v->cam);
    p = DF_MPKT_MakeUnpack(p, 0, 4, v->world_screen_mat);
    p = DF_MPKT_MakeUnpack(p, 0x1c, 4, v->world_view_mat);
    MPktCurrentp = p;
}

void DF_MVIEW_MakeVU1PacketMyView(struct struct_af7eae9d *pView)
{
    PS2_FRAME(0x30);
    unsigned int *p = MPktCurrentp;
    __UpdateCamMatrix(pView);
    p = DF_MPKT_MakeUnpack(p, 0x4a, 1, &pView->cam);
    p = DF_MPKT_MakeUnpack(p, 0, 4, pView->world_screen_mat);
    p = DF_MPKT_MakeUnpack(p, 0x1c, 4, pView->world_view_mat);
    MPktCurrentp = p;
}

void DF_MVIEW_MakeVU1Packet(void)
{
    PS2_FRAME(0x10);
    unsigned int *p = DF_MPKT_MakeUnpack(MPktCurrentp, 0x4a, 1, &MViewp->cam);
    p = __MakeDrawEnvPakcet(p);
    p = DF_MPKT_MakeUnpack(p, 0, 4, MViewp->world_screen_mat);
    p = DF_MPKT_MakeUnpack(p, 0x1c, 4, MViewp->world_view_mat);
    MPktCurrentp = p;
}

/* cnt tag (12 qwords), DIRECTHL 11, GIF tag A+D x 10: PABE, ALPHA_1/2, TEST_1/2, CLAMP_1, SCISSOR_1/2, XYOFFSET_1/2 */
static unsigned int *__MakeDrawEnvPakcet(unsigned int *ioDmap)
{
    unsigned int *d = ioDmap;
    d[0] = 0x1000000c; d[1] = 0; d[2] = 0; d[3] = 0;
    d[4] = 0; d[5] = 0; d[6] = 0; d[7] = 0x5100000b;
    d[8] = 0x800a; d[9] = 0x10004000; d[10] = 0xe; d[11] = 0;
    mview_put64(d + 12, 0);                       mview_put64(d + 14, 0x49);
    mview_put64(d + 16, 0xa);                     mview_put64(d + 18, 0x42);
    mview_put64(d + 20, 0x0000004000000068ull);   mview_put64(d + 22, 0x43);
    mview_put64(d + 24, 0x7000f);                 mview_put64(d + 26, 0x47);
    mview_put64(d + 28, 0x50ff5);                 mview_put64(d + 30, 0x48);
    mview_put64(d + 32, 0);                       mview_put64(d + 34, 8);
    mview_put64(d + 36, 0x01bf0000027f0000ull);   mview_put64(d + 38, 0x40);
    mview_put64(d + 40, 0x01bf0000027f0000ull);   mview_put64(d + 42, 0x41);
    mview_put64(d + 44, 0x0000720000006c00ull);   mview_put64(d + 46, 0x18);
    mview_put64(d + 48, 0x0000720000006c00ull);   mview_put64(d + 50, 0x19);
    return d + 52;
}

static void __UpdateCamMatrix(struct struct_af7eae9d *ioVmp)
{
    PS2_FRAME(0x90);
    PS2_STACK(float[4], ang, 0x30);
    PS2_STACK(float[4], vec, 0x40);
    PS2_STACK(float[4][4], T, 0x50);

    sceVu0CopyMatrix(ioVmp->world_screen_mat2, ioVmp->world_screen_mat);
    vec[0] = ioVmp->cam.pos[0] * -1.0f;
    vec[1] = ioVmp->cam.pos[1] * -1.0f;
    vec[2] = ioVmp->cam.pos[2] * -1.0f;
    vec[3] = ioVmp->cam.pos[3];                /* vmulx.xyz: w keeps cam.pos.w */
    sceVu0TransMatrix(T, IdentityMtx, vec);
    ang[0] = DF_MFUNC_AngForm(ioVmp->cam.ang[0]);
    ang[1] = DF_MFUNC_AngForm(ioVmp->cam.ang[1]);
    ang[2] = DF_MFUNC_AngForm(ioVmp->cam.ang[2]);
    ang[3] = ioVmp->cam.ang[3];
    sceVu0RotMatrixY(ioVmp->world_view_mat, IdentityMtx, -ang[1]);
    sceVu0RotMatrixX(ioVmp->world_view_mat, ioVmp->world_view_mat, -ang[0]);
    sceVu0RotMatrixZ(ioVmp->world_view_mat, ioVmp->world_view_mat, -ang[2]);
    vu_mul_matrix(ioVmp->world_view_mat, ioVmp->world_view_mat, T);
    /* farz (9th float) goes to sp+0 of this frame */
    float scrz = ioVmp->scrz, ax = 1.0f;
    if (hook_view_screen) hook_view_screen(3, &ax, &scrz);   /* step 5.2 widescreen menus (NULL = original) */
    sceVu0ViewScreenMatrix(ioVmp->view_screen_mat, scrz, ax, ps2_u2f(0x3f70a3d7), 2048.0f + ioVmp->ox,
                           2048.0f + ioVmp->oy, 0.0f, ps2_u2f(0x477fff00), 10.0f, 65536.0f);
    vu_mul_matrix(ioVmp->world_screen_mat, ioVmp->view_screen_mat, ioVmp->world_view_mat);
}

static void __ChangeView(struct struct_af7eae9d *ioVmp)
{
    PS2_FRAME(0xa0);
    PS2_STACK(float[4], npos, 0x60);
    PS2_STACK(float[4], nang, 0x70);
    PS2_STACK(float[4], ppos, 0x80);
    PS2_STACK(float[4], pang, 0x90);
    struct struct_af7eae9d *v = ioVmp;

    ppos[0] = v->prev.px; ppos[1] = v->prev.py; ppos[2] = v->prev.pz; ppos[3] = 1.0f;
    pang[0] = v->prev.ax; pang[1] = v->prev.ay; pang[2] = v->prev.az; pang[3] = 1.0f;
    npos[0] = v->next.px; npos[1] = v->next.py; npos[2] = v->next.pz; npos[3] = 1.0f;
    nang[0] = v->next.ax; nang[1] = v->next.ay; nang[2] = v->next.az; nang[3] = 1.0f;

    short cnt = (short)(v->count + 1);
    v->count = cnt;
    float d0 = MVIEW_D * v->prev.view_deg;
    float d1 = MVIEW_D * v->next.view_deg;
    float f;

    if (cnt < v->count_max) {
        int half, n;
        bool first = v->count < v->count_max / 2;
        half = v->count_max / 2;
        if (first) n = half - v->count;           /* theta = -(H * n / half) */
        else n = v->count - half;                 /* theta = +(H * n / half) */
        for (int i = 0; i < 3; i++) {
            float th, hd, pv;
            pv = ppos[i];
            hd = 0.5f * (npos[i] - pv);
            th = ps2_div(MVIEW_H * (float)n, (float)half);
            if (first) th = -th;
            v->cam.pos[i] = pv + hd * (1.0f + fsmath_SinVU0(th));
            pv = pang[i];
            hd = 0.5f * (nang[i] - pv);
            th = ps2_div(MVIEW_H * (float)n, (float)half);
            if (first) th = -th;
            v->cam.ang[i] = pv + hd * (1.0f + fsmath_SinVU0(th));
        }
        float hd = 0.5f * (d1 - d0);
        float th = ps2_div(MVIEW_H * (float)n, (float)half);
        if (first) th = -th;
        f = d0 + hd * (1.0f + fsmath_SinVU0(th));
        v->view_deg = MVIEW_R * f;
    } else {
        memcpy(v->cam.pos, npos, 16);
        memcpy(v->cam.ang, nang, 16);
        v->view_deg = v->next.view_deg;
        f = MVIEW_D * v->next.view_deg;
        if (v->next_mot_id == -1)
            v->stat = 0;
        else
            DF_MVIEW_MotionStart(v, v->next_mot_id);
    }
    v->scrz = DF_MFUNC_CalcScrzFromViewRad(f);
}

void DF_MVIEW_Play(void)
{
    PS2_FRAME(0x30);
    struct struct_af7eae9d *v = MViewMng;
    for (int i = 0; i < 2; i++, v++) {
        if (v->stat == 4) __ChangeView(v);
        __UpdateCamMatrix(v);
    }
}

int DF_MVIEW_ChangeSync(void)
{
    return 1;
}

int DF_MVIEW_MotionSync(struct struct_af7eae9d *inVmp)
{
    return inVmp->stat == 0;
}

void DF_MVIEW_MotionStart(struct struct_af7eae9d *ioVmp, int inID)
{
    PS2_FRAME(0x40);
    if (!ioVmp) return;
    struct struct_985f0e5e *mot = __GetCamMotion(inID);
    struct struct_97e41c4a *endp = __GetCamParam(mot->end_id);
    if (mot->start_id == -1) {
        ioVmp->prev.px = ioVmp->cam.pos[0];
        ioVmp->prev.py = ioVmp->cam.pos[1];
        ioVmp->prev.pz = ioVmp->cam.pos[2];
        ioVmp->prev.ax = ioVmp->cam.ang[0];
        ioVmp->prev.ay = ioVmp->cam.ang[1];
        ioVmp->prev.az = ioVmp->cam.ang[2];
        ioVmp->prev.view_deg = ioVmp->view_deg;
    } else {
        struct struct_97e41c4a *startp = __GetCamParam(mot->start_id);
        memcpy(&ioVmp->prev, startp, 0x20);
        ioVmp->prev.ax = MVIEW_D * ioVmp->prev.ax;
        ioVmp->prev.ay = MVIEW_D * ioVmp->prev.ay;
        ioVmp->prev.az = MVIEW_D * ioVmp->prev.az;
    }
    memcpy(&ioVmp->next, endp, 0x20);
    ioVmp->next.ax = MVIEW_D * ioVmp->next.ax;
    ioVmp->next.ay = MVIEW_D * ioVmp->next.ay;
    ioVmp->next.az = MVIEW_D * ioVmp->next.az;
    ioVmp->next_mot_id = mot->next_id;
    ioVmp->count = 0;
    ioVmp->count_max = (short)((mot->frame * 15 * 2) / 60);
    ioVmp->stat = 4;
}

/* first record whose id matches; no match (or no records) -> the first record; no archive item -> 0 */
static struct struct_985f0e5e *__GetCamMotion(int inID)
{
    PS2_FRAME(0x20);
    unsigned char *p = (unsigned char *)DF_MDATA_GetData(MenuMng.dataTbl[0], 4);
    if (!p) return 0;
    unsigned int n = ((struct struct_add4cfba *)p)->num;
    struct struct_985f0e5e *r = (struct struct_985f0e5e *)(p + 0x10);
    for (unsigned int i = 0; i < n; i++, r++)
        if (r->id == inID) return r;
    return (struct struct_985f0e5e *)(p + 0x10);
}

static struct struct_97e41c4a *__GetCamParam(int inID)
{
    PS2_FRAME(0x20);
    unsigned char *p = (unsigned char *)DF_MDATA_GetData(MenuMng.dataTbl[0], 3);
    if (!p) return 0;
    unsigned int n = ((struct struct_add4cfba *)p)->num;
    struct struct_97e41c4a *r = (struct struct_97e41c4a *)(p + 0x10);
    for (unsigned int i = 0; i < n; i++, r++)
        if (r->id == inID) return r;
    return (struct struct_97e41c4a *)(p + 0x10);
}

void DF_MVIEW_SetID(int inID)
{
    PS2_FRAME(0x30);
    PS2_STACK(float[4], ang, 0x10);
    PS2_STACK(float[4], pos, 0x20);
    struct struct_985f0e5e *mot = __GetCamMotion(inID);
    struct struct_97e41c4a *prm = __GetCamParam(mot->end_id);
    pos[0] = prm->px; pos[1] = prm->py; pos[2] = prm->pz; pos[3] = 1.0f;
    ang[0] = prm->ax; ang[1] = prm->ay; ang[2] = prm->az; ang[3] = 0.0f;
    DF_MVIEW_Set(pos, ang, prm->view_deg);
}

void DF_MVIEW_Set(float *inPos, float *inAng, float inViewDeg)
{
    PS2_FRAME(0x30);
    PS2_STACK(float[4], ang2, 0x10);
    PS2_STACK(float[4], pos2, 0x20);
    memcpy(pos2, inPos, 16);   /* lq / sq: callers pass 16-aligned stack quads */
    ang2[0] = MVIEW_D * inAng[0];
    ang2[1] = MVIEW_D * inAng[1];
    ang2[2] = MVIEW_D * inAng[2];
    ang2[3] = 0.0f;
    for (int t = 0; t < 2; t++) {
        struct struct_af7eae9d *v = &MViewMng[t];
        v->scrz = t == 0 ? 1000.0f : 512.0f;
        memcpy(v->cam.pos, pos2, 16);
        memcpy(v->cam.ang, ang2, 16);
        v->ox = 0.0f;
        v->oy = 0.0f;
        v->view_deg = inViewDeg;
        v->stat = 0;
    }
    MLightMng = (struct struct_815baf6a *)DF_MDATA_GetData(MenuMng.dataTbl[0], 6);
    MViewp = &MViewMng[0];
    MWorkViewp = &MViewMng[1];
}

void DF_MVIEW_Init(void)
{
    PS2_FRAME(0x30);
    PS2_STACK(float[4], ang, 0x10);
    PS2_STACK(float[4], pos, 0x20);
    pos[0] = 0.0f; pos[1] = -5.0f; pos[2] = -35.0f; pos[3] = 1.0f;   /* OLMENU 0x1ee3470 */
    memset(ang, 0, 16);
    DF_MVIEW_Set(pos, ang, 30.0f);
}
