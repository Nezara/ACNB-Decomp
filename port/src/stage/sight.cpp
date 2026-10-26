/* src/stage/sight.cpp - lock-on candidate search: each frame, for each user player, the model nearest (radius 15) to a
 * point 10 units ahead of the AC (hitbox module) becomes g_ac[player].smp. Also keeps the view-frustum-cull slope
 * factors (set at stage load, read by the camera region checks). */
#include <string.h>
#include "acnb/sdk.h"
#include "acnb/nodwarf.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"

void Sight_GetVFCFactor(float *pAAX, float *pAAY)
{
    if (pAAX) *pAAX = sight_aax;
    if (pAAY) *pAAY = sight_aay;
}

/* the heading fed to the rotation is pi - ang[1], wrapped into [-pi, pi] */
void DF_SIGHT_SightHitProc(int player)
{
    PS2_FRAME(0xb0);
    const float PI = ps2_u2f(0x40490fdb), TWO_PI = ps2_u2f(0x40c90fdb);
    float sightR2 = 15.0f * 15.0f;
    float sightdist = 10.0f;
    float svec[4] __attribute__((aligned(16))) = {0.0f, 0.0f, 1.0f, 1.0f};
    float mat[4][4] __attribute__((aligned(16)));
    float loc[4] __attribute__((aligned(16)));
    AcPlayerSt *acp = &g_ac[player];
    StModel *mp = &MyAC[player];
    float angy = PI - mp->ang[1];
    if (!(angy <= PI)) angy -= TWO_PI;
    if (angy < -PI) angy += TWO_PI;
    sceVu0RotMatrixY(mat, IdentityMtx, -angy);
    memcpy(loc, mp->loc, 16);
    vu_apply(svec, (const float (*)[4])mat, svec);
    svec[0] = svec[0] * sightdist;   /* (xyz only: w keeps the transform's) */
    svec[1] = svec[1] * sightdist;
    svec[2] = svec[2] * sightdist;
    loc[0] = loc[0] + svec[0];
    loc[1] = loc[1] + svec[1];
    loc[2] = loc[2] + svec[2];
    StModel *smp = 0;
    HitBoxGetNearSightModel(&smp, acp, loc, sightR2);
    acp->smp = smp;
}

void DF_SIGHT_SetVFCFactor(float x, float y)
{
    float t1 = x * x, t0 = y * y;
    t1 = 1.0f + t1;
    t0 = 1.0f + t0;
    sight_aax = x;
    sight_aay = y;
    sight_bbx = ps2_sqrt(t1);   /* (written, never read) */
    sight_bby = ps2_sqrt(t0);
}
