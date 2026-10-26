/* src/collision/hitwk.h - the collision work area in scratchpad RAM, shared by the hit functions. A caller puts the
 * shape under test at 0x40 (line / sphere / Y-cylinder), the HitFunc*SPR primitives test it against one polygon,
 * cylinder or sphere and leave the result at 0x00 (HitInfoWkSt: hit flag, hit model, position, normal, line
 * rates) and the polygon's edges / normal at 0x80. The shape's bounding box is kept at 0xc0; a model's hit
 * mesh is transformed to 0x100.. (the HMD vertex refs point there, see DF_ReadHMDPointer), and
 * HitSpcGetHitModelBoundBox leaves that mesh's bounding box at 0x00. */
#pragma once
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"

struct HitSprWk {
    union {
        HitInfoWkSt info;                       /* 0x00 */
        struct { float min[4], max[4]; } bb;    /* 0x00 model bounding box */
    };
    union {
        HitLineWkSt line;                       /* 0x40 P, V = direction * L, N = direction, L */
        HitSphereWkSt sphere;
        HitCylinderWkSt cyl;                    /* P = bottom centre, H, R */
    };
    HitPolygonWkSt poly;                        /* 0x80 */
    float min[4], max[4];                       /* 0xc0 bounding box of the tested shape */
    float hold[4];                              /* 0xe0 best hit so far (hitbox.cpp) */
    char _pad[0x10];
    float vtx[1008][4];                         /* 0x100 transformed hit mesh */
};
static_assert(sizeof(HitSprWk) == 0x4000, "scratchpad layout");

#define HIT_WK ((HitSprWk *)PS2_SPR(0))

/* The older layout of the same area used by hit.c / hfunc.c (floor search under a Y-cylinder hanging from loc
 * down by len; y grows downwards, so the floor found is the smallest y in [loc.y, loc.y + len]). */
struct HitOldWk {
    float pos[4];                               /* 0x00 hit position (.y = floor height) */
    float loc[4];                               /* 0x10 cylinder top centre */
    float norm[4];                              /* 0x20 floor normal */
    float min[4], max[4];                       /* 0x30 / 0x40 triangle box, xz grown by R */
    int hit;                                    /* 0x50 */
    float dist;                                 /* 0x54 cylinder top to the polygon's plane along y */
    float R;                                    /* 0x58 */
    float len;                                  /* 0x5c */
    char _pad60[7];
    unsigned char keep;                         /* 0x67 bit 0: DF_HIT_HitProc_YUKA keeps parent fallback / light */
    StModel *line_mdl;                          /* 0x68 model hit by DF_HIT_HitProc_Line */
    float lim;                                  /* 0x6c loc.y + len, lowered to each floor found */
    float RR;                                   /* 0x70 R * R */
    char _pad74[0xc];
    StModel *hist[8];                           /* 0x80 models stood on (DF_HIT_HitProc_YUKA) */
    char _pada0[0x60];
    float vtx[1008][4];                         /* 0x100 transformed hit mesh */
};
static_assert(sizeof(HitOldWk) == 0x4000, "scratchpad layout");

#define HIT_OWK ((HitOldWk *)PS2_SPR(0))

/* stage hit models: HitMap[0..HitMapNum) then HitObj[0..HitObjNum) */
static inline StModel *hit_model(int i) { return i < HitMapNum ? HitMap[i] : HitObj[i - HitMapNum]; }

/* HMD polygon list: a 0x20-byte header, then pv_num vertex refs (a strip of pv_num - 2 triangles); the first
 * ref's r / g / b carry the polygon's attributes (b: 1 = water, 7 = no hit) */
static inline HmdPrimSt *prim_verts(struct_de4d68f5 *ph) { return (HmdPrimSt *)(ph + 1); }
static inline struct_de4d68f5 *prim_next(struct_de4d68f5 *ph) { return (struct_de4d68f5 *)(prim_verts(ph) + ph->pv_num); }

/* the bounding box of the strip's n vertices overlaps the shape's box (HIT_WK->min / max) */
static inline int hit_prim_in_box(const HmdPrimSt *v, int n)
{
    const HitSprWk *w = HIT_WK;
    const float FMAX = ps2_u2f(0x7f7fffff);
    float mx[3] = {-FMAX, -FMAX, -FMAX}, mn[3] = {FMAX, FMAX, FMAX};
    do {
        const float *p = *v->v_id;
        for (int k = 0; k < 3; k++) {
            mx[k] = vu_max(mx[k], p[k]);
            mn[k] = vu_min(mn[k], p[k]);
        }
        v++;
    } while (--n);
    for (int k = 0; k < 3; k++)
        if (!(w->min[k] <= mx[k])) return 0;
    for (int k = 0; k < 3; k++)
        if (w->max[k] < mn[k]) return 0;
    return 1;
}

/* the model's hit mesh through matrix2 into HIT_WK->vtx (vf1-4 = matrix, vf16-19 = vertices, 4 at a time) */
static inline void hit_mesh_to_spr(StModel *m)
{
    HitSprWk *w = HIT_WK;
    const float (*mat)[4] = m->matrix2;
    const float (*vp)[4] = m->hmd->vp;
    unsigned int nv = m->hmd->v_num;
    for (unsigned int i = 0; i < nv; i++) vu_apply(w->vtx[i], mat, vp[i]);
    for (int j = 0; j < 4; j++) PS2_VF_MIRROR(1 + j, mat[j]);
    if (nv >= 4)
        for (int j = 1; j < 4; j++) PS2_VF_MIRROR(16 + j, w->vtx[(nv & ~3u) - 4 + j]);
    if (nv) PS2_VF_MIRROR(16, w->vtx[nv & 3 ? nv - 1 : nv - 4]);
}

/* oracle mirror of a VU0 .xyz result: vf<r>.xyz = v, w keeps the register's old lane */
static inline void hit_vf_xyz(int r, const float *v)
{
    float t[4] = {v[0], v[1], v[2], PS2_STALE_VF(r, 3)};
    PS2_VF_MIRROR(r, t);
    (void)t;
}

/* the test line's bounding box from P and P + V (vmini / vmax .xyz into vf20 / vf21) */
static inline void hit_line_box(void)
{
    HitSprWk *w = HIT_WK;
    for (int i = 0; i < 3; i++) {
        float e = w->line.P[i] + w->line.V[i];
        w->min[i] = vu_min(w->line.P[i], e);
        w->max[i] = vu_max(w->line.P[i], e);
    }
    w->min[3] = PS2_STALE_VF(20, 3);
    w->max[3] = PS2_STALE_VF(21, 3);
}

/* unit normal of strip triangle k hit by the line (from the edges HitFuncLinePolygonSPR left): the winding
 * alternates along the strip, pv_type.cwf flips it */
static inline void hit_line_normal(const struct_de4d68f5 *ph, int k)
{
    HitSprWk *w = HIT_WK;
    const float *a = w->poly.V12, *b = w->poly.V13;
    if (ph->pv_type.cwf == (k % 2 == 0)) { a = w->poly.V13; b = w->poly.V12; }
    vu_cross(w->poly.N, a, b);
    w->poly.N[3] = a[3];
    float q = vu_rcp_len3(w->poly.N);
    for (int i = 0; i < 3; i++) w->info.hitnorm[i] = w->poly.N[i] * q;
    w->info.hitnorm[3] = 0.0f;
}

/* 1 / |n| of a polygon normal as vmul.xyz + vaddx.z + vaddy.z, vsqrt, vmulq.w (1 * q), vdiv vf0w */
static inline float hit_rcp_len_zxy(const float *n)
{
    float x = n[0] * n[0], y = n[1] * n[1], z = n[2] * n[2];
    return ps2_div(1.0f, 1.0f * ps2_sqrt((z + x) + y));
}

/* edges V12 / V13 / V23 and the unit normal of triangle P1 P2 P3 into HIT_WK->poly (vf19-21 / vf16 in the
 * original: the .xyz ops keep those registers' old w in the stored edges, the normal keeps P1.w) */
static inline void hit_poly_prep(const float *P1, const float *P2, const float *P3)
{
    HitPolygonWkSt *p = &HIT_WK->poly;
    float e12[4], e13[4], e23[4], n[4];
    for (int i = 0; i < 3; i++) {
        e12[i] = P2[i] - P1[i];
        e13[i] = P3[i] - P1[i];
        e23[i] = P3[i] - P2[i];
    }
    e12[3] = PS2_STALE_VF(19, 3);
    e13[3] = PS2_STALE_VF(20, 3);
    e23[3] = PS2_STALE_VF(21, 3);
    vu_cross(n, e12, e13);
    n[3] = P1[3];
    memcpy(p->V12, e12, 16);
    memcpy(p->V13, e13, 16);
    memcpy(p->V23, e23, 16);
    float q = hit_rcp_len_zxy(n);
    p->N[0] = n[0] * q; p->N[1] = n[1] * q; p->N[2] = n[2] * q; p->N[3] = n[3];
}
