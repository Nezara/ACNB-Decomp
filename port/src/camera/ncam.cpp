/* src/camera/ncam.cpp - the neutral camera CAM[4] (replay viewer / spectating, CameraProc for camera id 4): picks a
 * live AC to watch ("random" from GiziRand by frame, re-picked every 300-870 frames or soon after the AC dies) and runs
 * a two-shot director: type 0 = from the nearest stage "neutral camera" marker (dummy type 1) drifting toward it, type
 * 1 = circling the AC at 30 units, both pulled in front of walls by a ray cast. Only DF_CALC_InitNeutralCamera runs in
 * a normal Versus match. Sintable / GiziRand are game data copied from the ELF in the source port (sp_missing.py).
 *
 * Programmer note on GiziRand (float[256] @0x25cb90, checked s62, all 256 entries bit-exact): it is not noise but
 * the numbers 0..255 / 256 in a shuffled order, made by an offline tool built with MSVC:
 *     srand(0); repeat rand() * 256 / 32768, skipping values already drawn, until all 256 are used -> k[i]
 *     GiziRand[i] = k[i] / 256 printed to 6 decimals (so 0.234375, 0.644531, ...)
 * MSVC rand() is x = x * 214013 + 2531011, (x >> 16) & 0x7fff. The tool's printf rounded exact halves up (2/256 =
 * 0.0078125 -> 0.007813); a modern "%f" rounds them to even and changes 32 entries, so a regenerator must round
 * half-up itself: n = (k * 390625 + 50) / 100, GiziRand[i] = (float)(n / 1e6).
 * Sintable's own note is in src/lib/calc.cpp. */
#include <string.h>
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"
#include "acnb/hooks.h"

void DF_CALC_SetNeutralCameraMode(int mode) { ReplayCameraMode = mode; }

/* index of the neutral-camera marker nearest to loc (3-D), its position into camloc; 0 / untouched if none */
int DF_CALC_GetNearestNeutralCamera(float *loc, float *camloc)
{
    float best = ps2_u2f(0x501502f9);   /* 1e10 */
    int id = 0;
    MarkerSt *m = Array->marker;
    for (int i = 0; i < 128; i++, m++) {
        if (m->type != 1) continue;
        float x = loc[0] - m->loc[0], y = loc[1] - m->loc[1], z = loc[2] - m->loc[2];
        float d2 = z * z + (x * x + y * y);
        if (d2 < best) {
            memcpy(camloc, m->loc, 16);
            best = d2;
            id = i;
        }
    }
    return id;
}

/* camera slot from eye / look-at: angles, world->view (CAMERA_MATRIX1 / 2; not the billboard matrix) and mat = world->
 * screen through the SDK projection (not CAM_UpdateViewScreenMatrix) */
void DF_CALC_ncamera(CameraSt *cam, float *eye, float *at, float (*mat)[4])
{
    PS2_FRAME(0x70);   /* sceVu0ViewScreenMatrix (interpreted) takes farz at sp+0 */
    memcpy(cam->vec, eye, 16);
    memcpy(cam->at, at, 16);
    float vv[4] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2], at[3]};
    float len = ps2_sqrt(vv[2] * vv[2] + (vv[0] * vv[0] + vv[1] * vv[1]));
    if (0.0f == len) vv[2] = 1.0f;
    vu_normalize(vv, vv);
    cam->ang[1] = ps2_atan2f(vv[0], vv[2]);
    cam->ang[0] = fsmath_AsinVU0(-vv[1]);
    sceVu0RotMatrixY(CAMERA_MATRIX1, IdentityMtx, -cam->ang[1]);
    sceVu0RotMatrixX(CAMERA_MATRIX1, CAMERA_MATRIX1, -cam->ang[0]);
    float eye2[4] = {eye[0] * -1.0f, eye[1] * -1.0f, eye[2] * -1.0f, eye[3]};
    sceVu0InversMatrix(CAMERA_MATRIX2, CAMERA_MATRIX1);
    vu_apply(eye2, CAMERA_MATRIX1, eye2);
    sceVu0TransMatrix(CAMERA_MATRIX1, CAMERA_MATRIX1, eye2);
    float scrz = cam->scrz, ax = ps2_u2f(0x3f4ccccd);
    if (hook_view_screen) hook_view_screen(1, &ax, &scrz);   /* step 5.2 widescreen / fov (NULL = original) */
    sceVu0ViewScreenMatrix(VIEW_SCREEN_MATRIX, scrz, ax, 0.5f, 2048.0f, 2048.0f, 0.0f, ps2_u2f(0x4b7fffff), 1.0f,
                           65536.0f);
    sceVu0MulMatrix(mat, VIEW_SCREEN_MATRIX, CAMERA_MATRIX1);
}

/* ray from start along the unit vector toward NeutralCamVec: the hit point clips NeutralCamVec */
static inline void ncam_clip_ray(float *start, float (&d)[4])
{
    d[0] = NeutralCamVec[0] - start[0]; d[1] = NeutralCamVec[1] - start[1]; d[2] = NeutralCamVec[2] - start[2];
    d[3] = NeutralCamVec[3];
    float len = ps2_sqrt(d[2] * d[2] + (d[0] * d[0] + d[1] * d[1]));
    sceVu0Normalize(d, d);
    DF_HIT_HitProc_Line(start, d, len, NeutralCamVec);
}

/* shot director for AC inPlayerNo: eye NeutralCamVec, look-at NeutralCamView -> CAM[4] and mat */
void DF_CALC_NeutralCamera2(float (*mat)[4], int inPlayerNo)
{
    PS2_FRAME(0xd0);
    const float PI = ps2_u2f(0x40490fdb), TWO_PI = ps2_u2f(0x40c90fdb);
    AcPlayerSt *acp = &g_ac[inPlayerNo];
    struct_6cd93cc0 *w = &NeutralCamWork;
    float svec[4] = {0.0f, 0.0f, 1.0f, 1.0f}, mat2[4][4];
    float yaw = PI - acp->ang[1];
    if (!(yaw <= PI)) yaw = yaw - TWO_PI;
    if (yaw < -PI) yaw = yaw + TWO_PI;
    sceVu0RotMatrixY(mat2, IdentityMtx, -yaw);
    vu_apply(svec, mat2, svec);   /* behind the AC */

    if (NeutralCameraInitialized) {
        w->camid = DF_CALC_GetNearestNeutralCamera(acp->loc, NeutralCamVec);
        memcpy(NeutralCamVecBack, NeutralCamVec, 16);
        NeutralCameraInitialized = 0;
    } else {
        if (w->camplayernobk != MyI.camplayerno) w->frame = w->endframe;   /* watched player changed: new shot */
        if (w->frame < w->endframe) {
            if (w->type == 1) {   /* orbit: follow while farther than 11 horizontally, look ahead along svec */
                float wk[4], tmp[4];
                wk[0] = acp->loc[0] - NeutralCamVec[0]; wk[1] = acp->loc[1] - NeutralCamVec[1];
                wk[2] = acp->loc[2] - NeutralCamVec[2]; wk[3] = acp->loc[3];
                float d2 = wk[0] * wk[0] + wk[2] * wk[2];
                if (!(d2 <= 1600.0f)) d2 = 1600.0f;
                float k = ps2_div(ps2_u2f(0x3c75c28f) * d2, 1600.0f);
                wk[1] = wk[1] - 16.0f;
                wk[0] = wk[0] * k; wk[1] = wk[1] * k; wk[2] = wk[2] * k;
                wk[0] = NeutralCamVec[0] + wk[0]; wk[1] = NeutralCamVec[1] + wk[1]; wk[2] = NeutralCamVec[2] + wk[2];
                wk[3] = NeutralCamVec[3];
                tmp[0] = acp->loc[0] - wk[0]; tmp[2] = acp->loc[2] - wk[2];
                if (!(tmp[0] * tmp[0] + tmp[2] * tmp[2] <= 121.0f)) memcpy(NeutralCamVec, wk, 16);
                memcpy(NeutralCamView, acp->loc, 16);
                float f = ps2_div((float)w->frame, 40.0f);
                float t0 = svec[0] * f, t2 = svec[2] * f;
                NeutralCamView[0] = NeutralCamView[0] + t0;
                NeutralCamView[2] = NeutralCamView[2] + t2;
                NeutralCamView[1] = NeutralCamView[1] - 10.0f;
            } else {   /* marker: drift toward the marker; this path counts two frames per call */
                float wk[3];
                const float k = ps2_u2f(0x3ac49ba6);
                wk[0] = (NeutralCamVecBack[0] - NeutralCamVec[0]) * k;
                wk[1] = (NeutralCamVecBack[1] - NeutralCamVec[1]) * k;
                wk[2] = (NeutralCamVecBack[2] - NeutralCamVec[2]) * k;
                NeutralCamVec[0] = NeutralCamVec[0] + wk[0];
                NeutralCamVec[1] = NeutralCamVec[1] + wk[1];
                NeutralCamVec[2] = NeutralCamVec[2] + wk[2];
                memcpy(NeutralCamView, acp->loc, 16);
                NeutralCamView[1] = NeutralCamView[1] - 4.0f;
                w->frame++;
            }
            w->frame++;
        } else {   /* new shot: 70 % marker, 30 % orbit (marker only in ReplayCameraMode 1) */
            if (ReplayCameraMode == 1) w->type = 0;
            else w->type = ps2_ftoi(100.0f * GiziRand[FRAME & 0xff]) < 70 ? 0 : 1;
            PS2_STACK(float[4], d, 0xb0);   /* passed to the interpreted DF_HIT_HitProc_Line */
            if (w->type == 1) {
                w->type = 1;
                w->camid = 0xff;
                unsigned int f = FRAME;
                NeutralCamVecBack[0] = acp->loc[0];
                NeutralCamVecBack[1] = acp->loc[1] - 10.0f;
                NeutralCamVecBack[2] = acp->loc[2];
                NeutralCamVec[1] = NeutralCamVecBack[1] + 0.0f;
                NeutralCamVec[0] = NeutralCamVecBack[0] + 30.0f * Sintable[f & 0xfff];
                NeutralCamVec[2] = NeutralCamVecBack[2] + 30.0f * Sintable[(f + 0x400) & 0xfff];
                memcpy(NeutralCamView, acp->loc, 16);
                w->endframe = 240;
                NeutralCamView[1] = NeutralCamView[1] - 10.0f;
                ncam_clip_ray(NeutralCamView, d);   /* (the orbit ray starts at the view point) */
            } else if (w->type == 0) {
                w->type = 0;
                PS2_STACK(float[4], loc2, 0xc0);
                int nc = DF_CALC_GetNearestNeutralCamera(acp->loc, loc2);
                if (w->camid != nc) {
                    w->camid = nc;
                    memcpy(NeutralCamVecBack, loc2, 16);
                    unsigned int f = FRAME;
                    NeutralCamVec[0] = NeutralCamVecBack[0] + 10.0f * Sintable[f & 0xfff];
                    NeutralCamVec[1] = NeutralCamVecBack[1] + 10.0f * Sintable[(f + 0x200) & 0xfff];
                    NeutralCamVec[2] = NeutralCamVecBack[2] + 10.0f * Sintable[(f + 0x400) & 0xfff];
                }
                memcpy(NeutralCamView, acp->loc, 16);
                NeutralCamView[1] = NeutralCamView[1] - 4.0f;
                w->endframe = 200;
                ncam_clip_ray(NeutralCamVecBack, d);
            }
            w->frame = 0;
        }
    }
    if (!(acp->loc[1] <= SysInfo->yMapZero)) NeutralCamVec[1] = SysInfo->yMapZero - 15.0f;
    CameraSt *c = &CAM[4];
    DF_CALC_ncamera(c, NeutralCamVec, NeutralCamView, mat);
    if (!(c->flags & 4)) {
        c->parent = HitBoxLineDecideParent(c->vec, 1, NULL);
        if (c->parent) {
            c->groupid = c->parent->groupid;
        } else if (acp->mp) {
            c->parent = acp->mp->nowparent;   /* unchecked */
            c->groupid = (c->parent ? c->parent : (const StModel *)PS2_NULL_MEM())->groupid;
        }
    }
    w->camplayernobk = MyI.camplayerno;
}

/* camera id 4 per frame: re-pick the watched AC when its time is up (within 60 frames once it is dead / out) */
void DF_CALC_NeutralCameraProc(float (*mat)[4])
{
    PS2_FRAME(0x30);
    struct_6cd93cc0 *w = &NeutralCamWork;
    AcPlayerSt *p = &g_ac[w->camPlayerNo];
    int done = p->dead != 0 || p->areaovered != 0;
    if (done && !(w->camCounter < 0x3d)) w->camCounter = 0x3c;
    if (--w->camCounter < 0) {
        DF_SND_SetDopplerDisable(2);
        w->prevPlayerNo = w->camPlayerNo;
        PS2_STACK(int[4], acid, 0x20);
        int n = get_live_ac(acid);
        /* the same GiziRand entry picks the AC and the time */
        if (n > 0) w->camPlayerNo = acid[ps2_ftoi(100.0f * GiziRand[FRAME & 0xff]) % n];
        w->camCounter = ps2_ftoi(100.0f * GiziRand[FRAME & 0xff]) % 20 * 30 + 300;
    }
    if (w->prevPlayerNo != w->camPlayerNo) {
        w->prevPlayerNo = w->camPlayerNo;
        w->type = 0;
    }
    DF_CALC_NeutralCamera2(mat, w->camPlayerNo);
}

/* once per match: eye / look-at between the first two AC models, CAM[4] reset */
void DF_CALC_InitNeutralCamera(void)
{
    memcpy(NeutralCamVec, MyAC[0].loc, 16);
    NeutralCamVec[0] = NeutralCamVec[0] + MyAC[1].loc[0];
    NeutralCamVec[1] = NeutralCamVec[1] + MyAC[1].loc[1];
    NeutralCamVec[2] = NeutralCamVec[2] + MyAC[1].loc[2];
    NeutralCamVec[0] = NeutralCamVec[0] * 0.5f;
    NeutralCamVec[1] = NeutralCamVec[1] * 0.5f;
    NeutralCamVec[2] = NeutralCamVec[2] * 0.5f;
    memcpy(NeutralCamVecBack, NeutralCamVec, 16);
    memcpy(NeutralCamView, NeutralCamVec, 16);
    memcpy(NeutralCamViewBack, NeutralCamVec, 16);
    NeutralCameraInitialized = 1;
    memset(&NeutralCamWork, 0, 0x20);
    NeutralCamWork.camplayernobk = MyI.camplayerno;
    ZeroMemory32b(&CAM[4], 0x40);
    CAM[4].near = 1.0f;
    CAM[4].scrz = 512.0f;
    CAM[4].groupidbk = 0xff;
    ReplayCameraMode = 0;
    NeutralCamWork.type = 0;
}
