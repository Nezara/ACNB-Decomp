/* src/enemy/eneanim.cpp (OLSORTIE) - per-frame movement and matrix update of the enemy ground walker: the control
 * function turns the animation's move vector into a velocity (DFene_AnimWalkH), moves the model with character /
 * wall collision (DFene_MoveH + DFene_HIT_KABE_Char), and DFene_UpdateMatrix / DFene_ApplyMatrix rebuild the model
 * matrix (parent-relative when the enemy has a parent model) and push it to the object matrices. DFene_AnimUpdate is
 * the stored `animfunc` (forwards to Anim_Update). garage3 layer 2: these six; DFene_AnimWalk, DFene_AttachMatrix,
 * DFene_IsAnimEnd, DFene_ResetAnimLoop and DFene_InitAnimGroup stay interpreted for now. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"
#include "acnb/sdk.h"
#include <cstring>

#define ENEANIM_HIT_INFO ((HitInfoWkSt *)PS2_SPR(0))   /* collision result in scratchpad (collision/hitwk.h) */

/* lqc2 a; lqc2 b; vadd.xyz a, a, b; sqc2: d.xyz = a + b, d.w = a.w. d may alias a or b. */
static inline void eneanim_add_xyz(float *d, const float *a, const float *b)
{
    float x = a[0] + b[0], y = a[1] + b[1], z = a[2] + b[2], w = a[3];
    d[0] = x; d[1] = y; d[2] = z; d[3] = w;
}

/* the matrix rebuild shared by DFene_ApplyMatrix and DFene_UpdateMatrix (ep = mp->arg0p, not NULL) */
static void eneanim_rebuild_matrix(StModel *mp, EnemySt *ep)
{
    if (ep->parent) {
        DF_MakeModelLocalMatrixZXY_Model(mp, ep->initpos, mp->ang, mp->scale);
        vu_mul_matrix(mp->matrix2, (const float (*)[4])ep->parent->matrix2, (const float (*)[4])mp->matrix2);
        memcpy(mp->loc, mp->matrix2[3], 16);
    } else {
        DF_MakeModelLocalMatrixZXY_Model(mp, mp->loc, mp->ang, mp->scale);
    }
    sceVu0CopyMatrix(*mp->op[0].wrmatp, mp->matrix2);
    if (mp->nobj >= 2) Anim_UpdateChildObjMatrix(mp->op + 1, mp->matrix2);
}

void DFene_AnimUpdate(StModel *mp)
{
    Anim_Update(mp);   /* original: `j Anim_Update` */
}

void DFene_ApplyMatrix(float *dstvec, StModel *mp, float *vec)
{
    PS2_FRAME(0x50);
    alignas(16) float v[4];
    v[0] = -vec[0];                          /* neg.s: sign flip */
    v[1] = vec[1];
    v[2] = -vec[2];
    v[3] = vec[3];
    EnemySt *ep = (EnemySt *)mp->arg0p;
    if (ep) eneanim_rebuild_matrix(mp, ep);
    vu_apply(dstvec, (const float (*)[4])mp->matrix2, v);   /* always, also without arg0p */
}

void DFene_UpdateMatrix(StModel *mp)
{
    PS2_FRAME(0x30);
    EnemySt *ep = (EnemySt *)mp->arg0p;
    if (!ep) return;
    eneanim_rebuild_matrix(mp, ep);
}

int DFene_HIT_KABE_Char(StModel *mp, float *loc, float *vel, float R, float tall, float *dest)
{
    PS2_FRAME(0x30);
    EnemySt *ep = (EnemySt *)mp->arg0p;
    if ((mp->use & 2) || (ep->use & 1)) {    /* no character collision */
        vel[1] = 0.0f;
        vel[3] = 1.0f;
        eneanim_add_xyz(dest, loc, vel);
        return 0;
    }
    eneanim_add_xyz(dest, loc, vel);
    alignas(16) float out[4];
    if (!HitChrHitProcChr(mp, dest, tall, R, out)) return 0;   /* CyH = tall, CyR = R */
    StModel *pm = ENEANIM_HIT_INFO->pHitMdl;
    float y2 = out[1] * out[1];
    float x2 = out[0] * out[0];
    float xz = x2 + out[2] * out[2];         /* mula / madd */
    if (xz < y2) {
        out[1] = 0.0f;
        eneanim_add_xyz(dest, dest, out);
    }
    return pm != 0;                          /* the scratchpad hit model, not the hit flag */
}

int DFene_MoveH(EnemySt *ep, StModel *mp, float *mvec)
{
    PS2_FRAME(0xf0);
    PS2_STACK(unsigned char[12], clr, 0xe0);   /* the original's dead 12-byte clear */
    memset(clr, 0, 12);
    int flags = 0;
    alignas(16) float old[4], neu[4], start[4], mv[4], out[4], delta[4], row2[4], row0[4];
    memcpy(old, mp->loc, 16);
    if (ep->sw.nohit) {
        memcpy(neu, old, 16);
        eneanim_add_xyz(neu, neu, mvec);
    } else {
        mv[0] = mvec[0];
        mv[1] = 0.0f;
        mv[2] = mvec[2];
        mv[3] = 1.0f;
        float tall = mp->tall - ps2_u2f(0x3e4ccccd) /* 0.2 */;
        memcpy(start, old, 16);
        start[1] = start[1] - ps2_u2f(0x3dcccccd) /* 0.1 */;
        if (DFene_HIT_KABE_Char(mp, start, mv, mp->R, tall, neu)) flags |= 2;
        mv[0] = neu[0] - old[0];
        mv[2] = neu[2] - old[2];
        if (!(ep->edat->flag & 0x100)) {
            int hit;
            if ((mp->use & 2) || (((EnemySt *)mp->arg0p)->use & 1)) {
                mv[3] = 1.0f;
                mv[1] = 0.0f;
                eneanim_add_xyz(neu, start, mv);
                hit = 0;
            } else {
                eneanim_add_xyz(neu, start, mv);
                if (HitBoxHitProcKabe(mp, neu, tall, mp->R, out)) {
                    StModel *pm = ENEANIM_HIT_INFO->pHitMdl;
                    eneanim_add_xyz(neu, neu, out);
                    hit = pm != 0;
                } else {
                    hit = 0;
                }
            }
            if (hit) flags |= 1;
        }
    }
    vu_sub_xyz(delta, neu, old);
    delta[1] = 0.0f;
    vu_sub_xyz(delta, delta, mvec);
    memcpy(row2, mp->matrix2[2], 16);
    memcpy(row0, mp->matrix2[0], 16);
    {
        float t = row2[0] * delta[0], u = row2[1] * delta[1], v = row2[2] * delta[2];
        float s = t + u;
        ep->cvf = -(v + s);
    }
    {
        float t = row0[0] * delta[0], u = row0[1] * delta[1], v = row0[2] * delta[2];
        float s = t + u;
        ep->cvl = v + s;
    }
    if (flags != 3) {                        /* both hits block the move; y is never moved here */
        mp->loc[0] = neu[0];
        mp->loc[2] = neu[2];
        mp->loc[3] = 1.0f;
    }
    return flags;
}

int DFene_AnimWalkH(EnemySt *ep, float climb, float rate)
{
    PS2_FRAME(0x70);
    StModel *mp = ep->mp;
    alignas(16) float vec[4] = {0.0f, 0.0f, 0.0f, 0.0f}, mv[4], out[4];
    Anim_GetMoveVector(mp, vec);
    volatile float vneg1 = -1.0f;            /* vmulx.xyz by vf31x = -1: a real multiply (FTZ), not a sign flip */
    float neg1 = vneg1;
    vec[0] = vec[0] * neg1;
    vec[1] = vec[1] * neg1;
    vec[2] = vec[2] * neg1;
    mv[0] = vec[0];
    mv[1] = 0.0f;
    mv[2] = vec[2];
    mv[3] = 0.0f;
    DFene_ApplyMatrix(out, mp, mv);
    float v0 = ep->vel[0];
    ep->vel[0] = v0 + rate * (out[0] - v0);
    float v2 = ep->vel[2];
    ep->vel[2] = v2 + rate * (out[2] - v2);
    (void)climb;                             /* passed on in f12, DFene_MoveH never reads it */
    return DFene_MoveH(ep, mp, ep->vel);
}
