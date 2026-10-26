/* src/camera/camfunc.cpp - coarse on-screen test of a world-space box against the current camera (CAMERA_MATRIX1 and
 * the view-frustum slopes of sight.c): used by event scripts / stage region checks (STAGE_SightInOut) and enemy revival
 * to avoid spawning in view. Not executed in Versus. */
#include <float.h>
#include <string.h>
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"

/* view-space transform with the translation row taken once (vmaddw by vf0.w = 1, whatever v[3] is) */
static inline void cam_apply_w1(float *out, const float (*m)[4], const float *v)
{
    float t[4] = {v[0], v[1], v[2], 1.0f};
    vu_apply(out, m, t);
}

/* 1 if the box min..max may be visible: its view-space bounding box (8 corners, left in the scratchpad) against the
 * frustum slopes aax / aay */
int CamFunc_RegionSightInOut(float *min, float *max)
{
    float vmin[4] = {FLT_MAX, FLT_MAX, FLT_MAX, 1.0f}, vmax[4] = {-FLT_MAX, -FLT_MAX, -FLT_MAX, 1.0f};
    float aax, aay, c[4], h[4];
    Sight_GetVFCFactor(&aax, &aay);
    c[0] = min[0] + max[0]; c[1] = min[1] + max[1]; c[2] = min[2] + max[2]; c[3] = min[3];
    c[0] = c[0] * 0.5f; c[1] = c[1] * 0.5f; c[2] = c[2] * 0.5f;
    c[3] = 1.0f;
    const float (*m)[4] = CAMERA_MATRIX1;
    float(*spr)[4] = (float(*)[4])PS2_SPR(0);
    for (int i = 0; i < 8; i++) {   /* bit set: the min coordinate */
        spr[i][0] = (i & 1) ? min[0] : max[0];
        spr[i][1] = (i & 2) ? min[1] : max[1];
        spr[i][2] = (i & 4) ? min[2] : max[2];
        spr[i][3] = 0.0f;
    }
    cam_apply_w1(c, m, c);
    for (int i = 0; i < 8; i++) {
        cam_apply_w1(spr[i], m, spr[i]);
        for (int k = 0; k < 3; k++) {
            vmin[k] = vu_min(spr[i][k], vmin[k]);
            vmax[k] = vu_max(spr[i][k], vmax[k]);
        }
    }
    for (int k = 0; k < 3; k++) h[k] = (vmax[k] - vmin[k]) * 0.5f;
    float cz = c[2];
    if (cz < -h[2]) return 0;
    float lx = h[0] + cz * aax;
    if (!(c[0] * c[0] <= lx * lx)) return 0;
    float ly = h[1] + cz * aay;
    if (!(c[1] * c[1] <= ly * ly)) return 0;
    return 1;
}
