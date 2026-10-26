/* src/camera/cam.cpp - the per-player cameras CAM[6] (0-3 players, 4 = neutral / replay camera of ncam.c) and the
 * matrices derived from them. Once per view and frame CameraProc calls DF_CalculateCamera: the behaviour selected by
 * view_mode (s_pCamFunc: 0 back view behind the AC, 1 cockpit, 2 enemy, 4 debug free camera, 5 event / movie polygon)
 * places eye and look-at point, then the world->view matrix CAMERA_MATRIX1, its inverse CAMERA_MATRIX2, the
 * y-billboard matrix CAMERA_MATRIX3 and (CAM_UpdateViewScreenMatrix) the projection matrices are built. The back view
 * follows the AC lazily and is pushed out of walls and characters by __cam_SetCollPos. */
#include <string.h>
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"
#include "acnb/hooks.h"

#ifdef ACNB_SOURCEPORT
void (*s_pCamFunc[8])(float *, float *, CameraSt *) = {__cam_CamFuncBackView, __cam_CamFuncCockpit, __cam_CamFuncEnemy, 0,
                                                       __cam_CamFuncFree, __cam_CamFuncEvPolyg, 0, 0};
float __cam_CamFuncFree__up[4] = {0.0f, -1.0f, 0.0f, 1.0f};
#endif

static const float CAM_PI = ps2_u2f(0x40490fdb), CAM_2PI = ps2_u2f(0x40c90fdb);

/* the row 3 = translation-free y-billboard matrix from the inverse view matrix */
void CAM_CalcYSpriteMatrix(float (*res)[4], float (*cm2)[4])
{
    float v[4], m[4][4];
    memcpy(v, cm2[2], 16);
    v[1] = 0.0f;
    sceVu0Normalize(v, v);
    m[0][0] = v[2]; m[0][1] = 0.0f; m[0][2] = -v[0]; m[0][3] = 0.0f;
    m[1][0] = 0.0f; m[1][1] = 1.0f; m[1][2] = 0.0f; m[1][3] = 0.0f;
    memcpy(m[2], v, 16);
    m[3][0] = 0.0f; m[3][1] = 0.0f; m[3][2] = 0.0f; m[3][3] = 1.0f;
    sceVu0CopyMatrix(res, m);
}

/* (also inlined at the end of DF_CalculateCamera) a NULL parent model reads PS2 address 0 */
void CAM_SetParentGroup(CameraSt *camp)
{
    PS2_FRAME(0x20);
    if (camp->flags & 4) return;
    if (CopyOfSysInfo.isHeightGroup && camp->vec[1] < CopyOfSysInfo.heightGroup) {
        camp->groupid = CopyOfSysInfo.heightGroupID;
        return;
    }
    StModel *m = HitBoxLineDecideParent(camp->vec, 1, NULL);
    const StModel *mm = m ? m : (const StModel *)PS2_NULL_MEM();
    if (!(mm->mflag & 0x100000)) {
        camp->parent = m;
        camp->groupid = (camp->parent ? camp->parent : (const StModel *)PS2_NULL_MEM())->groupid;
    }
}

void CAM_SetFovy(CameraSt *camp, float fovy)
{
    PS2_FRAME(0x20);
    camp->scrz = ps2_div(256.0f, ps2_tanf(ps2_u2f(0x3c0efa35) * fovy));
}

/* movie cameras: the camera from a world->view matrix (rotation order X, Y, Z here) */
void CAM_SetWorldViewMatrix(CameraSt *camp, float (*wvmat)[4])
{
    PS2_FRAME(0xb0);
    float tmp[4][4], mat[4][4];
    sceVu0InversMatrix(tmp, wvmat);
    sceVu0CopyMatrix(CAMERA_MATRIX1, tmp);
    camp->vec[0] = wvmat[3][0];
    camp->vec[1] = wvmat[3][1];
    camp->vec[2] = wvmat[3][2];
    camp->vec[3] = 1.0f;
    camp->vvec[0] = wvmat[0][2];
    camp->vvec[1] = wvmat[1][2];
    camp->vvec[2] = wvmat[2][2];
    camp->vvec[3] = wvmat[3][2];
    camp->ang[1] = ps2_atan2f(camp->vvec[0], camp->vvec[2]);
    camp->ang[0] = fsmath_AsinVU0(-camp->vvec[1]);
    float at[4] = {camp->vec[0] + camp->vvec[0], camp->vec[1] + camp->vvec[1], camp->vec[2] + camp->vvec[2], camp->vec[3]};
    memcpy(camp->at, at, 16);
    sceVu0RotMatrixX(mat, IdentityMtx, camp->ang[0]);
    sceVu0RotMatrixY(mat, mat, camp->ang[1]);
    sceVu0RotMatrixZ(mat, mat, camp->ang[2]);
    sceVu0InversMatrix(CAMERA_MATRIX2, mat);
    CAM_CalcYSpriteMatrix(CAMERA_MATRIX3, CAMERA_MATRIX2);
}

void CAM_UpdateWorldScreenMatrix(CameraSt *camp)
{
    CAM_UpdateViewScreenMatrix(camp);
    sceVu0MulMatrix(WORLD_SCREEN_MATRIX, VIEW_SCREEN_MATRIX, CAMERA_MATRIX1);
}

/* view->screen (VIEW_SCREEN_MATRIX), view->clip (g_matViewClip) and clip->screen (g_matClipScreen) from near / scrz;
 * 0.8 x / 0.5 y pixel aspect, screen centre 2048, depth 0..16777215 over near..65536 */
void CAM_UpdateViewScreenMatrix(CameraSt *camp)
{
    float ax = ps2_u2f(0x3f4ccccd);   /* 0.8f */
    float near = camp->near, scrz = camp->scrz;
    if (hook_view_screen) hook_view_screen(0, &ax, &scrz);   /* step 5.2 widescreen / fov (NULL = original) */
    float f21 = ps2_div(ps2_u2f(0x44ffe000) * near, scrz);   /* 2047.5 * near / scrz */
    float (*vs)[4] = VIEW_SCREEN_MATRIX;
    sceVu0UnitMatrix(vs);
    vs[0][0] = scrz;
    vs[1][1] = scrz;
    vs[2][2] = 0.0f;
    vs[3][3] = 0.0f;
    vs[3][2] = 1.0f;
    vs[2][3] = 1.0f;
    float l[4][4];
    sceVu0UnitMatrix(l);
    l[1][1] = 0.5f;
    l[0][0] = ax;
    float f20 = 65536.0f * near;
    float f2 = -near + 65536.0f;
    l[3][0] = 2048.0f;
    l[3][1] = 2048.0f;
    l[2][2] = ps2_div(f20 * ps2_u2f(0x4b7fffff), f2);
    l[3][2] = ps2_div(ps2_u2f(0xcb7fffff) * near + 0.0f, f2);
    acvu0_MulMatrix(vs, l, vs);

    float (*vc)[4] = g_matViewClip;
    sceVu0UnitMatrix(vc);
    vc[3][3] = 0.0f;
    float q = ps2_div(2.0f * near, f21 + f21);
    vc[2][3] = 1.0f;
    vc[0][0] = q;
    vc[1][1] = q;
    float d = 65536.0f - near;
    vc[2][2] = ps2_div(65536.0f + near, d);
    vc[3][2] = ps2_div(-2.0f * f20, d);

    float (*cs)[4] = g_matClipScreen;
    sceVu0UnitMatrix(cs);
    cs[2][2] = ps2_u2f(0xcaffffff);
    cs[3][2] = ps2_u2f(0x4affffff);
    cs[3][3] = 1.0f;
    cs[3][0] = 2048.0f;
    cs[3][1] = 2048.0f;
    cs[0][0] = ps2_div(f21 * (scrz * ax), near);
    cs[1][1] = ps2_div(f21 * (scrz * 0.5f), near);
}

/* angle wrap loops of the free / event cameras (each pass re-reads the stored angle) */
static inline void cam_wrap_loop(float *a)
{
    while (!(*a <= CAM_PI)) *a = *a - CAM_2PI;
    while (*a < -CAM_PI) *a = *a + CAM_2PI;
}

/* free camera speed factor: hold frames / 15, at most 1 */
static inline float cam_hold_rate(int n)
{
    float f = ps2_div((float)n, 15.0f);
    if (!(f <= 1.0f)) f = 1.0f;
    return f;
}

/* view_mode 4: debug camera on pad slot 1 (d-pad turn / move, L2 / R2 pitch, L1 / R1 strafe, triangle / cross up / down,
 * square look at the AC, circle keep the parent group) */
static void __cam_CamFuncFree(float *vec, float *at, CameraSt *cam)
{
    PS2_FRAME(0x120);
    struct_178ad92e *pad = &__cam_CamFuncFree__pad;
    /* (the original's "else: clear all counters" branch is behind a constant true test) */
    auto hold = [](int &n, unsigned short button) { n = Pad_IsOn(1, button) ? n + 1 : 0; };
    hold(pad->lu, 0x10);     /* up */
    hold(pad->ld, 0x40);     /* down */
    hold(pad->ll, 0x80);     /* left */
    hold(pad->lr, 0x20);     /* right */
    hold(pad->ru, 0x1000);   /* triangle */
    hold(pad->rd, 0x4000);   /* cross */
    hold(pad->rl, 0x8000);   /* square */
    hold(pad->rr, 0x2000);   /* circle */
    hold(pad->l1, 0x400);
    hold(pad->l2, 0x100);
    hold(pad->r1, 0x800);
    hold(pad->r2, 0x200);

    memcpy(vec, cam->vec, 16);
    AcPlayerSt *acp = cam->player_id < 4 ? &g_ac[cam->player_id] : NULL;
    if (pad->rl > 0 && acp && acp->mp) {
        float da[4], center[4], db[4], d[4], ang[4];
        da[0] = cam->at[0] - cam->vec[0]; da[1] = cam->at[1] - cam->vec[1]; da[2] = cam->at[2] - cam->vec[2]; da[3] = cam->at[3];
        vu_normalize(da, da);
        DF_GetModelCenter(center, acp->mp);
        db[0] = center[0] - cam->vec[0]; db[1] = center[1] - cam->vec[1]; db[2] = center[2] - cam->vec[2]; db[3] = center[3];
        vu_normalize(db, db);
        d[0] = da[0] - db[0]; d[1] = da[1] - db[1]; d[2] = da[2] - db[2]; d[3] = da[3];
        d[0] = d[0] * 0.75f; d[1] = d[1] * 0.75f; d[2] = d[2] * 0.75f;
        d[0] = d[0] + db[0]; d[1] = d[1] + db[1]; d[2] = d[2] + db[2];
        vu_normalize(d, d);
        vec2ang(ang, d);
        cam->ang[0] = ang[0];
        cam->ang[1] = ang[1];
    } else {
        if (pad->lr > 0) {
            cam->ang[1] = cam->ang[1] + cam->fcam_rot * cam_hold_rate(pad->lr);
            cam_wrap_loop(&cam->ang[1]);
        }
        if (pad->ll > 0) {
            cam->ang[1] = cam->ang[1] + -cam->fcam_rot * cam_hold_rate(pad->ll);
            cam_wrap_loop(&cam->ang[1]);
        }
        if (pad->l2 > 0 && pad->r2 > 0) {
            cam->ang[0] = 0.0f;
        } else {
            if (pad->l2 > 0) {
                cam->ang[0] = cam->ang[0] + cam->fcam_rot * cam_hold_rate(pad->l2);
                cam_wrap_loop(&cam->ang[0]);
            }
            if (pad->r2 > 0) {
                cam->ang[0] = cam->ang[0] + -cam->fcam_rot * cam_hold_rate(pad->r2);
                cam_wrap_loop(&cam->ang[0]);
            }
            const float half_pi = ps2_u2f(0x3fc90fdb);
            float a = cam->ang[0];
            if (!(a <= half_pi)) cam->ang[0] = half_pi;
            else if (a < -half_pi) cam->ang[0] = -half_pi;
        }
    }
    if (pad->rr > 0) cam->flags |= 4;
    else cam->flags &= 0xfffb;

    float fwd[4], dir[4], right[4], t[4];
    ang2vec(fwd, cam->ang[1], cam->ang[0]);
    fwd[0] = fwd[0] * 100.0f; fwd[1] = fwd[1] * 100.0f; fwd[2] = fwd[2] * 100.0f;
    ang2vec(dir, cam->ang[1], 0.0f);
    const float *up = __cam_CamFuncFree__up;
    vu_cross(right, up, dir);
    right[3] = up[3];
    vu_normalize(right, right);
    /* hold < 0x3d frames: slow speed, then fast */
    struct { int n; const float *v; float sign; } mv[6] = {
        {pad->lu, dir, 1.0f}, {pad->ld, dir, -1.0f}, {pad->l1, right, 1.0f},
        {pad->r1, right, -1.0f}, {pad->ru, up, 1.0f}, {pad->rd, up, -1.0f}};
    for (int i = 0; i < 6; i++) {
        if (mv[i].n <= 0) continue;
        float s = mv[i].n < 0x3d ? cam->fcam_mov_low : cam->fcam_mov_hi;
        if (mv[i].sign < 0.0f) s = -s;
        t[0] = mv[i].v[0] * s; t[1] = mv[i].v[1] * s; t[2] = mv[i].v[2] * s;
        vec[0] = vec[0] + t[0]; vec[1] = vec[1] + t[1]; vec[2] = vec[2] + t[2];
    }
    float a4[4] = {vec[0] + fwd[0], vec[1] + fwd[1], vec[2] + fwd[2], vec[3]};
    memcpy(at, a4, 16);
}

/* event camera gaze on a model: (unused vec, at, cam); interpolated (1 % per frame) or fixed */
static void __cam_SetGazeModel(float *vec, float *at, CameraSt *pNowCam)
{
    (void)vec;
    PS2_FRAME(0x30);
    __evmng_event_id_st *ev = EVMNG_GetEventID(pNowCam->ev_gaze_mdl_evid);
    StModel *m = STAGE_GetModelPointer(ev->type, ev->id);
    const float k = ps2_u2f(0x3c23d700);
    if (pNowCam->ev_gaze_intp) {
        at[0] = m->loc[0] + pNowCam->ev_gaze_ofs[0];
        at[1] = m->loc[1] + pNowCam->ev_gaze_ofs[1];
        at[2] = m->loc[2] + pNowCam->ev_gaze_ofs[2];
        at[3] = m->loc[3];
        for (int i = 0; i < 3; i++) at[i] = pNowCam->at[i] + k * (at[i] - pNowCam->at[i]);
        memcpy(pNowCam->at, at, 16);
    } else {
        memcpy(pNowCam->ev_gaze, m->loc, 16);
        for (int i = 0; i < 3; i++) pNowCam->ev_gaze[i] = pNowCam->ev_gaze[i] + pNowCam->ev_gaze_ofs[i];
        memcpy(at, pNowCam->ev_gaze, 16);
    }
}

/* event camera position: ev_pos or a model's position; interpolated (1 % per frame) or fixed. (vec, unused at, cam) */
static void __cam_SetPosModel(float *vec, float *at, CameraSt *pNowCam)
{
    (void)at;
    PS2_FRAME(0x40);
    float fvwk[4];
    if (pNowCam->ev_pos_mdl_evid == 0) {
        memcpy(fvwk, pNowCam->ev_pos, 16);
    } else {
        __evmng_event_id_st *ev = EVMNG_GetEventID(pNowCam->ev_pos_mdl_evid);
        StModel *m = STAGE_GetModelPointer(ev->type, ev->id);
        memcpy(fvwk, m->loc, 16);
    }
    fvwk[3] = 1.0f;
    if (pNowCam->ev_gaze_intp) {
        const float k = ps2_u2f(0x3c23d700);
        memcpy(vec, fvwk, 16);
        for (int i = 0; i < 3; i++) vec[i] = pNowCam->vec[i] + k * (vec[i] - pNowCam->vec[i]);
        memcpy(pNowCam->vec, vec, 16);
    } else {
        memcpy(pNowCam->ev_pos, fvwk, 16);
        memcpy(vec, fvwk, 16);
    }
}

/* view_mode 5: event / movie polygon camera, driven by CAM[0]'s ev_* fields */
static void __cam_CamFuncEvPolyg(float *vec, float *at, CameraSt *pNowCam)
{
    PS2_FRAME(0xb0);
    CameraSt *c0 = &CAM[0];
    static const float fwd[4] = {0.0f, 0.0f, 1.0f, 1.0f};   /* 0x25ad00 ({0, 0, -1, 1} at 0x25ad10 is copied, unused) */
    float m[4][4];
    switch (c0->ev_flags & 0xff) {
    case 4:
        __cam_SetPosModel(vec, at, pNowCam);
        break;
    case 2:
        if (c0->ev_trans_frame != 0) {   /* lhu + blez: unsigned */
            vec[0] = pNowCam->vec[0] + c0->ev_pos[0];
            vec[1] = pNowCam->vec[1] + c0->ev_pos[1];
            vec[2] = pNowCam->vec[2] + c0->ev_pos[2];
            vec[3] = pNowCam->vec[3];
            c0->ev_trans_frame--;
        }
        break;
    case 1:
        acvu0_CopyVectorF_ni(vec, c0->ev_pos);
        acvu0_CopyVectorF_ni(at, c0->ev_ang);
        break;
    default:
        memcpy(vec, pNowCam->vec, 16);
        break;
    }
    unsigned short fl = c0->ev_flags;
    switch (fl & 0xf00) {
    case 0x400:
        if ((fl & 0xf000) == 0x2000) __cam_SetGazeModel(vec, at, pNowCam);
        else memcpy(at, c0->ev_gaze, 16);   /* 0x1000 and the rest */
        break;
    case 0x200:
        memcpy(at, c0->ev_gaze, 16);
        c0->ev_flags &= 0xfdff;
        break;
    case 0x100:
        if (c0->ev_ang_frame != 0) {
            pNowCam->ang[0] = pNowCam->ang[0] + c0->ev_ang[0];
            pNowCam->ang[1] = pNowCam->ang[1] + c0->ev_ang[1];
            pNowCam->ang[2] = pNowCam->ang[2] + c0->ev_ang[2];
            cam_wrap_loop(&pNowCam->ang[0]);
            cam_wrap_loop(&pNowCam->ang[1]);
            cam_wrap_loop(&pNowCam->ang[2]);
            c0->ev_ang_frame--;
            sceVu0RotMatrixZ(m, IdentityMtx, pNowCam->ang[2]);
            sceVu0RotMatrixX(m, m, pNowCam->ang[0]);
            sceVu0RotMatrixY(m, m, pNowCam->ang[1]);
            vu_apply(at, m, fwd);
            at[0] = at[0] + vec[0];
            at[1] = at[1] + vec[1];
            at[2] = at[2] + vec[2];
        } else {
            c0->ev_flags = fl & 0xfeff;
        }
        break;
    default:
        sceVu0RotMatrixZ(m, IdentityMtx, pNowCam->ang[2]);
        sceVu0RotMatrixX(m, m, pNowCam->ang[0]);
        sceVu0RotMatrixY(m, m, pNowCam->ang[1]);
        vu_apply(at, m, fwd);
        break;
    }
}

/* view_mode 2 (enemy overlay cameras): behind the enemy model cam->datap along its view angles */
static void __cam_CamFuncEnemy(float *vec, float *at, CameraSt *cam)
{
    PS2_FRAME(0x60);
    EnemySt *ep = (EnemySt *)cam->datap;
    if (!ep) return;
    StModel *mp = ep->mp;
    if (!mp) return;
    float back = mp->R + cam->back_dist;
    at[0] = vec[0] = mp->loc[0];
    at[1] = vec[1] = mp->loc[1] - mp->tall;
    at[2] = vec[2] = mp->loc[2];
    at[3] = vec[3] = 1.0f;
    float cvec[4];
    ang2vec(cvec, ep->view.angy, ep->view.angx);
    vec[0] = at[0] - cvec[0] * back;
    vec[1] = (at[1] - cvec[1] * back) - cam->back_height;
    vec[2] = at[2] - cvec[2] * back;
    vec[3] = 1.0f;
    if (cam->flags & 2) {
        cam->flags &= 0xfffd;
        return;
    }
    __cam_SetCollPos(cam, vec, at);
    const float te = ps2_u2f(0x3dccccd0), ta = ps2_u2f(0x3e374bc8);
    for (int i = 0; i < 3; i++) vec[i] = cam->vec[i] + te * (vec[i] - cam->vec[i]);
    for (int i = 0; i < 3; i++) at[i] = cam->at[i] + ta * (at[i] - cam->at[i]);
}

/* lock-on offset of the view (back view / cockpit): the angles of the primary lock target, scaled by dang_fc */
static inline void cam_lock_offset(float *dtgt, AcPlayerSt *acp, CameraSt *cam)
{
    StWepLock *l = acp->lockmng.pri_lock;
    if (l && l->ofp && (l->lock_stat & 2)) {
        dtgt[0] = l->ang[0] * cam->dang_fc;
        dtgt[1] = acp->lockmng.pri_lock->ang[1] * cam->dang_fc;
    } else {
        memset(dtgt, 0, 16);
    }
    cam->dang[0] = go_angf(cam->dang[0], dtgt[0], cam->dang_add);
    cam->dang[1] = go_angf(cam->dang[1], dtgt[1], cam->dang_add);
    if (!(cam->dang[0] <= CAM_PI)) cam->dang[0] = cam->dang[0] - CAM_2PI;
    if (!(cam->dang[1] <= CAM_PI)) cam->dang[1] = cam->dang[1] - CAM_2PI;
}

/* view_mode 1: eye and look-at start at the cockpit joint (object 4), looking along the AC's view */
static void __cam_CamFuncCockpit(float *vec, float *at, CameraSt *cam)
{
    PS2_FRAME(0x70);
    AcPlayerSt *acp = &g_ac[cam->player_id];
    float(*wm)[4] = *acp->mp->op[4].wrmatp;
    memcpy(vec, wm[3], 16);
    memcpy(at, wm[3], 16);
    float dtgt[4], cvec[4], ang[2];
    cam_lock_offset(dtgt, acp, cam);
    ang[0] = acp->view.ang[0] + cam->dang[0];
    float a = CAM_PI + (acp->ang[1] + cam->dang[1]);
    while (!(a <= CAM_PI)) a = a - CAM_2PI;
    ang[1] = a;
    while (ang[1] < -CAM_PI) ang[1] = ang[1] + CAM_2PI;
    const float lim = ps2_u2f(0x3f860a92);   /* 60 degrees */
    float p = ang[0];
    if (!(p <= lim)) p = lim;
    else if (p < -lim) p = -lim;
    ang[0] = p;
    ang2vec(cvec, ang[1], ang[0]);
    at[0] = at[0] + cvec[0];
    at[1] = at[1] + cvec[1];
    at[2] = at[2] + cvec[2];
}

/* view_mode 0: the player camera, back_dist behind the AC along its view (+ lock-on offset), lagging behind */
static void __cam_CamFuncBackView(float *vec, float *at, CameraSt *cam)
{
    PS2_FRAME(0xb0);
    AcPlayerSt *acp = &g_ac[cam->player_id];
    cam->back_dist = ViewNum < 2 ? 21.0f : 30.0f;
    memcpy(at, acp->loc, 16);
    at[1] = at[1] - acp->cam_height;
    float ang[2], dtgt[4], vang[2], cvec[4];
    ang[0] = acp->view.ang[0];
    ang[1] = CAM_PI + acp->ang[1];
    if (!(ang[1] <= CAM_PI)) ang[1] = ang[1] - CAM_2PI;
    cam_lock_offset(dtgt, acp, cam);
    vang[0] = ang[0] + cam->dang[0];
    vang[1] = ang[1] + cam->dang[1];
    if (!(vang[1] <= CAM_PI)) vang[1] = vang[1] - CAM_2PI;
    if (vang[1] < -CAM_PI) vang[1] = vang[1] + CAM_2PI;
    ang2vec(cvec, vang[1], vang[0]);
    vec[0] = at[0] - cvec[0] * cam->back_dist;
    vec[1] = (at[1] - cvec[1] * cam->back_dist) - cam->back_height;
    vec[2] = at[2] - cvec[2] * cam->back_dist;
    vec[3] = 1.0f;
    if (cam->flags & 2) {   /* no-delay frame */
        cam->flags &= 0xfffd;
        if (MyI.replaymode && (acp->areastat & 2)) memcpy(vec, cam->vec, 16);
        return;
    }
    __cam_SetCollPos(cam, vec, at);
    float te = 1.0f - cam->back_eye_tr;
    for (int i = 0; i < 3; i++) vec[i] = cam->vec[i] + te * (vec[i] - cam->vec[i]);
    for (int i = 0; i < 3; i++) at[i] = at[i] + cvec[i] * 7.0f;
    vec[3] = 1.0f;
    float ta = 1.0f - cam->back_at_tr;
    for (int i = 0; i < 3; i++) at[i] = cam->at[i] + ta * (at[i] - cam->at[i]);
    if ((acp->areastat & 2) || (cam->stat & 1)) {   /* area over / frozen */
        memcpy(vec, cam->vec, 16);
        memcpy(at, cam->at, 16);
    }
}

/* keeps the eye out of walls (line + sphere hits), out of characters near it and >= 6.5 from the look-at point */
static void __cam_SetCollPos(CameraSt *cam, float *eyePos, float *atPos)
{
    PS2_FRAME(0xe0);
    (void)cam;
    float eyeVec[4], tmp[4], eyeSph[4], d[4], chrpos[4], v[4];
    eyeVec[0] = eyePos[0] - atPos[0]; eyeVec[1] = eyePos[1] - atPos[1]; eyeVec[2] = eyePos[2] - atPos[2];
    eyeVec[3] = eyePos[3];
    float len = 6.0f + ps2_sqrt(eyeVec[2] * eyeVec[2] + (eyeVec[0] * eyeVec[0] + eyeVec[1] * eyeVec[1]));
    vu_normalize(eyeVec, eyeVec);
    if (HitBoxHitProcLine(atPos, eyeVec, len, 0, 1)) {
        float t = *(float *)PS2_SPR(0x70) - ps2_u2f(0x3dcccccd);
        tmp[0] = eyeVec[0] * t; tmp[1] = eyeVec[1] * t; tmp[2] = eyeVec[2] * t;
        float e[4] = {atPos[0] + tmp[0], atPos[1] + tmp[1], atPos[2] + tmp[2], atPos[3]};
        memcpy(eyePos, e, 16);
    }
    eyeSph[0] = eyeVec[0] * 5.0f; eyeSph[1] = eyeVec[1] * 5.0f; eyeSph[2] = eyeVec[2] * 5.0f;
    eyeSph[0] = eyePos[0] - eyeSph[0]; eyeSph[1] = eyePos[1] - eyeSph[1]; eyeSph[2] = eyePos[2] - eyeSph[2];
    eyeSph[3] = eyePos[3];
    if (HitBoxHitProcSphere(eyeSph, 5.0f, 0, 1)) {
        const float *hp = (const float *)PS2_SPR(0x10);
        d[0] = eyeSph[0] - hp[0]; d[1] = eyeSph[1] - hp[1]; d[2] = eyeSph[2] - hp[2];
        float dist = ps2_sqrt(d[2] * d[2] + (d[0] * d[0] + d[1] * d[1]));
        float pen = 5.0f - dist;
        if (!(pen <= 0.0f)) {
            float k = ps2_div(pen, dist);
            d[0] = d[0] * k; d[1] = d[1] * k; d[2] = d[2] * k;
            eyePos[0] = eyePos[0] + d[0]; eyePos[1] = eyePos[1] + d[1]; eyePos[2] = eyePos[2] + d[2];
        }
    }
    /* the nearest character whose height span contains the eye (the original's "hit" flag here is always 0) */
    float best = ps2_u2f(0x4cbebc20), bestR = 0.0f;
    StModel *m = Chr;
    int n = StgInfo.cnum;
    for (int i = 0; i < n; i++, m++) {
        if (m->loc[1] <= eyePos[1]) continue;
        if (!(m->loc[1] - m->tall < eyePos[1])) continue;
        if (m->use & 1) continue;
        v[0] = eyePos[0] - m->loc[0]; v[1] = eyePos[1] - m->loc[1]; v[2] = eyePos[2] - m->loc[2]; v[3] = eyePos[3];
        v[1] = 0.0f;
        float d2 = sceVu0InnerProduct(v, v);
        if (!(best <= d2)) {
            best = d2;
            memcpy(chrpos, m->loc, 16);
            bestR = m->R;
        }
    }
    float lim = 7.0f + bestR;
    if (best < lim * lim) {
        float dist = ps2_sqrt(best);
        float push = lim - dist;
        v[0] = eyePos[0] - chrpos[0]; v[1] = eyePos[1] - chrpos[1]; v[2] = eyePos[2] - chrpos[2]; v[3] = eyePos[3];
        v[1] = 0.0f;
        if (0.0f == dist) {
            memcpy(v, atPos, 16);   /* quirk: the zero-distance fallback pushes by the look-at position */
        } else {
            float k = ps2_div(push, dist);
            v[0] = v[0] * k; v[1] = v[1] * k; v[2] = v[2] * k;
        }
        eyePos[0] = eyePos[0] + v[0]; eyePos[1] = eyePos[1] + v[1]; eyePos[2] = eyePos[2] + v[2];
    }
    d[0] = eyePos[0] - atPos[0]; d[1] = eyePos[1] - atPos[1]; d[2] = eyePos[2] - atPos[2]; d[3] = eyePos[3];
    if (d[0] * d[0] + d[2] * d[2] < 42.25f) {
        d[1] = 0.0f;
        vu_normalize(d, d);
        d[0] = d[0] * 6.5f; d[1] = d[1] * 6.5f; d[2] = d[2] * 6.5f;
        eyePos[0] = atPos[0] + d[0];
        eyePos[2] = atPos[2] + d[2];
    }
}

/* res moves toward tgt: res = tgt + (1 - tracef) * (res - tgt) */
void trace_fv(float *res, float *tgt, float tracef)
{
    float t = 1.0f - tracef;
    for (int i = 0; i < 3; i++) res[i] = tgt[i] + t * (res[i] - tgt[i]);
}

void Cam_SetScreenZ(int camid, float scrz) { CAM[camid].scrz = scrz; }

void DFcam_SetNoDelay(int player) { CAM[player].flags |= 2; }

/* new_mode -1 restores the previous mode; player views hide the own AC in the cockpit view (1) */
void DFcam_SetViewMode(int player, int new_mode)
{
    PS2_FRAME(0x30);
    CameraSt *c = &CAM[player];
    if (new_mode == -1) {
        c->view_mode = c->oview_mode;
    } else if (c->view_mode != new_mode) {
        c->oview_mode = c->view_mode;
        c->view_mode = new_mode;
    }
    if (player < 4) {
        int view = GetPlayerViewID(player);
        if (c->view_mode == 1) ViewMode_HideAcObject(player, view, 1);
        else if (c->view_mode != 7 && c->view_mode != 4) ViewMode_HideAcObject(player, view, 0);
    }
}

/* once per view and frame (CameraProc): camera behaviour, view matrices, projection, parent group */
void DF_CalculateCamera(int player, float (*matWs)[4], float (*matWc)[4])
{
    PS2_FRAME(0x80);
    if (g_sortie.pwork->isPlayPolyg == 1) return;
    DF_InitCamera(player, player, 0);
    CameraSt *c = &CAM[player];
    PS2_STACK(float[4], vec, 0x70);   /* handed to the behaviour function through the table */
    PS2_STACK(float[4], at, 0x60);
    void (*fn)(float *, float *, CameraSt *) = s_pCamFunc[c->view_mode];
    if (fn) {
        gcall(fn, (float *)vec, (float *)at, c);
    } else {
        memcpy(vec, c->vec, 16);
        memcpy(at, c->at, 16);
    }
    acvu0_CopyVectorF(c->vec, vec);
    acvu0_CopyVectorF(c->at, at);
    float dir[4];
    acvu0_SubVector(dir, at, vec);
    acvu0_Normalize(dir, dir);
    memcpy(c->vvec, dir, 16);
    float x = dir[0] * vec[0], y = dir[1] * vec[1], z = dir[2] * vec[2];
    c->vvec[3] = -(z + (x + y));
    if (!(dir[1] <= 1.0f)) dir[1] = 1.0f;
    if (dir[1] < -1.0f) dir[1] = -1.0f;
    c->ang[1] = ps2_atan2f(dir[0], dir[2]);
    c->ang[0] = fsmath_AsinVU0(-dir[1]);
    float r0 = -add_angf(c->ang[0], c->fangx);
    float r1 = -add_angf(c->ang[1], c->fangy);
    float r2 = -add_angf(c->ang[2], c->fangz);
    acvu0_RotMatrixY(CAMERA_MATRIX1, IdentityMtx, r1);
    acvu0_RotMatrixX(CAMERA_MATRIX1, CAMERA_MATRIX1, r0);
    acvu0_RotMatrixZ(CAMERA_MATRIX1, CAMERA_MATRIX1, r2);
    sceVu0InversMatrix(CAMERA_MATRIX2, CAMERA_MATRIX1);
    CAM_CalcYSpriteMatrix(CAMERA_MATRIX3, CAMERA_MATRIX2);
    vec[0] = vec[0] * -1.0f;
    vec[1] = vec[1] * -1.0f;
    vec[2] = vec[2] * -1.0f;
    acvu0_ApplyMatrix(vec, CAMERA_MATRIX1, vec);
    acvu0_TransMatrix(CAMERA_MATRIX1, CAMERA_MATRIX1, vec);
    CAMERA_MATRIX1[3][0] = CAMERA_MATRIX1[3][0] - c->flocx;   /* shake offset */
    CAMERA_MATRIX1[3][1] = CAMERA_MATRIX1[3][1] - c->flocy;
    CAMERA_MATRIX1[3][2] = CAMERA_MATRIX1[3][2] - c->flocz;
    CAM_UpdateViewScreenMatrix(c);
    acvu0_MulMatrix(matWs, VIEW_SCREEN_MATRIX, CAMERA_MATRIX1);
    acvu0_MulMatrix(matWc, g_matViewClip, CAMERA_MATRIX1);
    CAM_SetParentGroup(c);   /* inlined in the original (same code) */
    c->timer++;
}

/* force 0: only once per camera; player 0-3 starts at the AC */
void DF_InitCamera(int camid, int player, int force)
{
    CameraSt *c = &CAM[camid];
    if ((c->flags & 1) && !force) return;
    ZeroMemory32b(c, 0x40);
    c->flags |= 3;
    c->player_id = player;
    c->timer = 0;
    c->datap = 0;
    c->dang_add = ps2_u2f(0x3c8efa35);
    c->dang_fc = 0.5f;
    c->back_dist = g_sortie.pconstant->isSplit ? 30.0f : 21.0f;
    c->back_height = 1.0f;
    c->back_eye_tr = ps2_u2f(0x3f666666);
    c->back_at_tr = ps2_u2f(0x3f522d0e);
    c->near = 1.0f;
    c->scrz = 512.0f;
    c->parent = NULL;
    c->groupid = 0;
    c->groupidbk = 0xff;
    c->fcam_rot = ps2_u2f(0x3c8efa35);
    c->fcam_mov_low = 0.25f;
    c->fcam_mov_hi = 1.5f;
    acvu0_CopyVectorF_ni(c->ev_pos, c->vec);
    acvu0_CopyVectorF_ni(c->ev_ang, c->at);
    if (player >= 0 && player < 4) {
        memcpy(c->vec, g_ac[player].loc, 16);
        c->vec[1] = c->vec[1] - g_ac[player].tall;
    }
}

void DF_CAM_InitAll(void)
{
    for (int ii = 0; ii < 6; ii++) DF_InitCamera(ii, ii < 4 ? ii : 0, 1);
}
