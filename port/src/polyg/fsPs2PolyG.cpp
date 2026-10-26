/* src/polyg/fsPs2PolyG.cpp — the polygon-movie ("PolyG", PGB cutscene format) player core. A movie file (lpFile)
 * holds cuts with model / camera / light / SFX key tracks; fsPs2PolyG_Calc advances the time and evaluates every
 * track into the per-frame camera / model / light / SFX state, Analyze turns the file's offset tables into pointers
 * when a file is attached. Init / Create / AllocBuffer / InitUseOB are the setup entry points used by main/pgplay.
 * Native so far: the setup functions and the three PGB getters (G1), the camera / model matrix builders and the key
 * track evaluators (G2), Analyze and Calc (G3): the whole file is native. */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

static_assert(offsetof(_FS_POLYG_ST, CalcNoise) == 0x74, "_FS_POLYG_ST.CalcNoise");
static_assert(offsetof(_FS_POLYG_ST, lpPrePG) == 0x1158, "_FS_POLYG_ST.lpPrePG");
static_assert(offsetof(_FS_POLYG_ST, stTmpSfxBuf) == 0x11b0, "_FS_POLYG_ST.stTmpSfxBuf");
static_assert(offsetof(_FS_POLYG_ST, stTmpSfxSub) == 0x11c0, "_FS_POLYG_ST.stTmpSfxSub");
static_assert(offsetof(_FS_PGBFST_CUT_DATA_HEADER, dwMdlDtOffset) == 0x14, "_FS_PGBFST_CUT_DATA_HEADER.dwMdlDtOffset");

/* ---- G2: camera / model matrices and the key-track evaluators (static helpers of Calc) ---- */

#define PG_PI     0x1.921fb6p+1f   /* 0x40490fdb */
#define PG_2PI    0x1.921fb6p+2f   /* 0x40c90fdb */

/* angle into [-pi, pi] by whole turns; `!(a <= pi)`: a NaN angle never leaves the first loop (original hang) */
static inline float pg_wrap_angle(float a)
{
    while (!(a <= PG_PI)) a -= PG_2PI;
    while (a < -PG_PI) a += PG_2PI;
    return a;
}

/* Look-at camera: rows = X / Y / Z axes from eye -> target (up = -Y), optional roll about Z (eye ang.x), row 3 = eye.
 * The "_n" (noise) offsets apply when their w flag is 1.0f. pPolyG is unused (protofix). */
static _HRESULT fsPs2PolyG_Calc_CamMatTgt(_FS_POLYG_ST *pPolyG, _D3DXMATRIX *mOut, _FS_PGBVEC_POSANG *lpEye,
                                          _FS_PGBVEC_POSANG *lpTgt)
{
    (void)pPolyG;
    PS2_FRAME(0xf0);
    if (mOut == 0 || lpEye == 0 || lpTgt == 0) return 1;
    _D3DXVECTOR4 fEyePos __attribute__((aligned(16))) = lpEye->pos;
    _D3DXVECTOR4 fTgtPos __attribute__((aligned(16))) = lpTgt->pos;
    if (lpEye->pos_n.w == 1.0f) {
        fEyePos.x = fEyePos.x + lpEye->pos_n.x;
        fEyePos.y = fEyePos.y + lpEye->pos_n.y;
        fEyePos.z = fEyePos.z + lpEye->pos_n.z;
        fEyePos.w = 1.0f;
    }
    if (lpTgt->pos_n.w == 1.0f) {
        fTgtPos.x = fTgtPos.x + lpTgt->pos_n.x;
        fTgtPos.y = fTgtPos.y + lpTgt->pos_n.y;
        fTgtPos.z = fTgtPos.z + lpTgt->pos_n.z;
        fTgtPos.w = 1.0f;
    }
    float fRoll = lpEye->ang.x;
    if (lpEye->ang_n.w == 1.0f) fRoll = fRoll + lpEye->ang_n.x;
    fsPs2PolyGVu0_MatrixIdentity(mOut);
    /* CamZ.w / Up.w are never written in the original; only x / y / z reach mOut (RotationAxis reads xyz) */
    _D3DXVECTOR3 v3CamZ __attribute__((aligned(16))) = {};
    _D3DXVECTOR3 v3Up __attribute__((aligned(16))) = {};
    _D3DXVECTOR3 v3CamX __attribute__((aligned(16)));
    _D3DXVECTOR3 v3CamY __attribute__((aligned(16)));
    _D3DXMATRIX mRot __attribute__((aligned(16)));
    v3CamZ.x = fTgtPos.x - fEyePos.x;
    v3CamZ.y = fTgtPos.y - fEyePos.y;
    v3CamZ.z = fTgtPos.z - fEyePos.z;
    v3Up.y = -1.0f;
    v3Up.x = 0.0f;
    v3Up.z = 0.0f;
    fsPs2PolyGVu0_Vec3Normalize(&v3CamZ, &v3CamZ);
    fsPs2PolyGVu0_Vec3Cross(&v3CamX, &v3CamZ, &v3Up);
    fsPs2PolyGVu0_Vec3Normalize(&v3CamX, &v3CamX);
    fsPs2PolyGVu0_Vec3Cross(&v3CamY, &v3CamZ, &v3CamX);
    fsPs2PolyGVu0_Vec3Normalize(&v3CamY, &v3CamY);
    mOut->m[0][0] = v3CamX.x;
    mOut->m[0][1] = v3CamX.y;
    mOut->m[0][2] = v3CamX.z;
    mOut->m[1][0] = v3CamY.x;
    mOut->m[1][1] = v3CamY.y;
    mOut->m[1][2] = v3CamY.z;
    mOut->m[2][0] = v3CamZ.x;
    mOut->m[2][1] = v3CamZ.y;
    mOut->m[2][2] = v3CamZ.z;
    if (fRoll != 0.0f) {
        fsPs2PolyGVu0_MatrixRotationAxis(&mRot, &v3CamZ, fRoll);
        fsPs2PolyGVu0_MatrixMultiply(mOut, mOut, &mRot);
    }
    mOut->m[3][0] = fEyePos.x;
    mOut->m[3][1] = fEyePos.y;
    mOut->m[3][2] = fEyePos.z;
    return 0;
}

/* Free camera: view direction = -(0, 0, -1) rotated by pitch (ang.x) / yaw (ang.y), rows X / Y / Z orthonormalised
 * against wing (0, -1, 0), row 3 = pos (+ pos_n when its flag is set). pPolyG is unused (protofix). */
static _HRESULT fsPs2PolyG_Calc_CamMatFree(_FS_POLYG_ST *pPolyG, _D3DXMATRIX *mOut, _FS_PGBVEC_POSANG *lpCam)
{
    (void)pPolyG;
    PS2_FRAME(0xc0);
    if (mOut == 0 || lpCam == 0) return 1;
    float fPitch = pg_wrap_angle(lpCam->ang.x);
    float fYaw = pg_wrap_angle(lpCam->ang.y);
    _D3DXMATRIX rotMat __attribute__((aligned(16)));
    _D3DXVECTOR4 eyeVec __attribute__((aligned(16)));
    _D3DXVECTOR4 wingVec __attribute__((aligned(16)));
    _D3DXVECTOR3 verticalVec __attribute__((aligned(16)));
    fsPs2PolyGVu0_MatrixRotationPitchYawRoll(&rotMat, fYaw, fPitch, 0.0f);
    eyeVec.x = 0.0f;
    eyeVec.y = 0.0f;
    eyeVec.z = -1.0f;
    eyeVec.w = 0.0f;
    fsPs2PolyGVu0_Vec4Transform(&eyeVec, &eyeVec, &rotMat);
    eyeVec.x = -eyeVec.x;   /* neg.s: sign flip (keeps -0.0) */
    eyeVec.y = -eyeVec.y;
    eyeVec.z = -eyeVec.z;
    wingVec.y = -1.0f;
    wingVec.x = 0.0f;
    wingVec.z = 0.0f;
    wingVec.w = 0.0f;
    fsPs2PolyGVu0_Vec3Cross(&verticalVec, (_D3DXVECTOR3 *)&eyeVec, (_D3DXVECTOR3 *)&wingVec);
    _D3DXVECTOR3 *row0 = (_D3DXVECTOR3 *)&mOut->m[0][0];
    _D3DXVECTOR3 *row1 = (_D3DXVECTOR3 *)&mOut->m[1][0];
    _D3DXVECTOR3 *row2 = (_D3DXVECTOR3 *)&mOut->m[2][0];
    fsPs2PolyGVu0_Vec3Normalize(row0, &verticalVec);
    fsPs2PolyGVu0_Vec3Normalize(row2, (_D3DXVECTOR3 *)&eyeVec);
    fsPs2PolyGVu0_Vec3Cross(row1, row2, row0);
    fsPs2PolyGVu0_Vec3Normalize(row1, row1);
    mOut->m[0][3] = 0.0f;
    mOut->m[1][3] = 0.0f;
    mOut->m[2][3] = 0.0f;
    mOut->m[3][0] = lpCam->pos.x;
    mOut->m[3][1] = lpCam->pos.y;
    mOut->m[3][2] = lpCam->pos.z;
    mOut->m[3][3] = 1.0f;
    if (lpCam->pos_n.w == 1.0f) {
        mOut->m[3][0] = mOut->m[3][0] + lpCam->pos_n.x;
        mOut->m[3][1] = mOut->m[3][1] + lpCam->pos_n.y;
        mOut->m[3][2] = mOut->m[3][2] + lpCam->pos_n.z;
    }
    return 0;
}

/* (pi + v) reduced by whole turns with an int truncation of a / 2pi (cvt.w.s), then - pi */
static inline float pg_vecrot_wrap(float v)
{
    float a = PG_PI + v;
    if (a < 0.0f)
        a = a - PG_2PI * (float)(ps2_ftoi(ps2_div(a, PG_2PI)) - 1);
    else
        a = a - PG_2PI * (float)ps2_ftoi(ps2_div(a, PG_2PI));
    return a - PG_PI;
}

/* mOut = (RotX * RotZ) * RotY from the Euler angles in vRot (row-vector matrices). pPolyG is unused (protofix). */
static _HRESULT fsPs2PolyG_Calc_VecRotMat(_FS_POLYG_ST *pPolyG, _D3DXMATRIX *mOut, _D3DXVECTOR4 *vRot)
{
    (void)pPolyG;
    PS2_FRAME(0x140);
    if (vRot == 0 || mOut == 0) return 1;
    float fX = pg_vecrot_wrap(vRot->x);
    float fY = pg_vecrot_wrap(vRot->y);
    float fZ = pg_vecrot_wrap(vRot->z);
    _D3DXMATRIX sMatE __attribute__((aligned(16)));
    _D3DXMATRIX sMatZ __attribute__((aligned(16)));
    _D3DXMATRIX sMatY __attribute__((aligned(16)));
    _D3DXMATRIX sMatX __attribute__((aligned(16)));
    fsPs2PolyGVu0_MatrixIdentity(&sMatE);
    sMatZ = sMatE;
    sMatY = sMatZ;
    sMatX = sMatY;
    float sx = fsmath_SinVU0(fX);
    float sy = fsmath_SinVU0(fY);
    float sz = fsmath_SinVU0(fZ);
    float cx = fsmath_CosVU0(fX);
    float cy = fsmath_CosVU0(fY);
    float cz = fsmath_CosVU0(fZ);
    sMatX.m[1][2] = -sx;
    sMatY.m[2][0] = -sy;
    sMatZ.m[0][1] = -sz;
    sMatZ.m[0][0] = cz;
    sMatZ.m[1][1] = cz;
    sMatX.m[1][1] = cx;
    sMatX.m[2][2] = cx;
    sMatX.m[2][1] = sx;
    sMatY.m[0][2] = sy;
    sMatZ.m[1][0] = sz;
    sMatY.m[2][2] = cy;
    sMatY.m[0][0] = cy;
    fsPs2PolyGVu0_MatrixMultiply(mOut, &sMatX, &sMatZ);
    fsPs2PolyGVu0_MatrixMultiply(mOut, mOut, &sMatY);
    return 0;
}

/* Model tree: evaluate this node's Tod keys (Hermite pos / ang, Lerp scl - unused) + noise into lpOPBuf[dwObjId] =
 * RotX * RotZ * RotY * Trans (* parent), then recurse into the child (this node as parent) and the sibling */
static _HRESULT UpdateMatrix(_FS_POLYG_ST *pPolyG, _D3DXMATRIX *lpParOP, _FS_PGBFST_ANIM_TABLE *lpCldTbl,
                             _D3DXMATRIX *lpOPBuf)
{
    PS2_FRAME(0x100);
    /* the asm loads dwObjId before the null test (reads PS2 address 8 for a null table) */
    if (lpCldTbl == 0 || lpOPBuf == 0 || pPolyG->fNowTimeCut < 0.0f) return 1;
    _D3DXMATRIX *lpOP = (_D3DXMATRIX *)((char *)lpOPBuf + (unsigned int)(lpCldTbl->dwObjId << 6));
    _FS_PGBVEC_POSANG vec __attribute__((aligned(16)));
    PS2_STACK(float, fRatio, 0xfc);   /* GetCurrentTod leaves it unwritten when the key length is 0: stale read */
    memset(&vec, 0, 0x50);
    char *base = (char *)pPolyG->lpFile;
    _FS_PGBFST_TOD_DATA *lpTod = (_FS_PGBFST_TOD_DATA *)(base + lpCldTbl->dwTodDtOffset);
    _FS_PGBFST_NOISE_PARAM *lpNos =
        lpCldTbl->dwNosDtOffset ? (_FS_PGBFST_NOISE_PARAM *)(base + lpCldTbl->dwNosDtOffset) : 0;
    if (lpTod != 0 && lpCldTbl->dwTodKeyNum != 0) {
        _FS_PGBFST_TOD_DATA *key = fsPs2PolyG_GetCurrentTod(pPolyG, &fRatio, lpTod, lpCldTbl->dwTodKeyNum);
        int ret;
        if (lpTod == 0 || pPolyG->fNowTimeCut < 0.0f)
            ret = 1;
        else if (key == 0)
            ret = 1;
        else {
            _FS_PGBFST_TOD_DATA *next = key + 1;
            fsPs2PolyGVu0_Vec4Hermite(&vec.pos, &key->fPos, &key->fPosOT, &next->fPos, &next->fPosIT, fRatio);
            fsPs2PolyGVu0_Vec4Hermite(&vec.ang, &key->fAng, &key->fAngOT, &next->fAng, &next->fAngIT, fRatio);
            fsPs2PolyGVu0_Vec4Lerp(&vec.scl, &key->fScl, &next->fScl, fRatio);
            ret = 0;
        }
        if ((ret & 0xff) == 0) {
            vec.pos.w = 1.0f;
            vec.ang.w = 1.0f;
            fsPs2PolyG_GetCurrentNoise(pPolyG, &vec, lpNos);
        }
    }
    if (vec.ang_n.w == 1.0f) {
        vec.ang.w = 1.0f;
        vec.ang.x = vec.ang.x + vec.ang_n.x;
        vec.ang.y = vec.ang.y + vec.ang_n.y;
        vec.ang.z = vec.ang.z + vec.ang_n.z;
    }
    if (vec.pos_n.w == 1.0f) {
        vec.pos.w = 1.0f;
        vec.pos.x = vec.pos.x + vec.pos_n.x;
        vec.pos.y = vec.pos.y + vec.pos_n.y;
        vec.pos.z = vec.pos.z + vec.pos_n.z;
    }
    float fAngX = pg_wrap_angle(vec.ang.x);
    float fAngY = pg_wrap_angle(vec.ang.y);
    float fAngZ = pg_wrap_angle(vec.ang.z);
    sceVu0RotMatrixX(lpOP->m, gPolyGVu0_IdentityMat, fAngX);
    sceVu0RotMatrixZ(lpOP->m, lpOP->m, fAngZ);
    sceVu0RotMatrixY(lpOP->m, lpOP->m, fAngY);
    sceVu0TransMatrix(lpOP->m, lpOP->m, &vec.pos.x);
    if (lpParOP) sceVu0MulMatrix(lpOP->m, lpParOP->m, lpOP->m);
    if (lpCldTbl->dwChildOffset)
        UpdateMatrix(pPolyG, lpOP, (_FS_PGBFST_ANIM_TABLE *)((char *)pPolyG->lpFile + lpCldTbl->dwChildOffset), lpOPBuf);
    if (lpCldTbl->dwSiblOffset)
        UpdateMatrix(pPolyG, lpParOP, (_FS_PGBFST_ANIM_TABLE *)((char *)pPolyG->lpFile + lpCldTbl->dwSiblOffset), lpOPBuf);
    return 0;
}

/* Key search shared by the Lgt / Fov / Noise / Tod evaluators: key i spans wFrmLen frames after the previous one,
 * starting at the first key's wKeyFrm; dwKeyNum - 1 is unsigned (dwKeyNum == 0 searches past the array: original). */

/* Light parameters: Lerp of the colours + the four scalars between the current key and the next; returns 1 (lpOut
 * untouched) when the time is past the last key */
static _HRESULT fsPs2PolyG_GetCurrentLgtPrm(_FS_POLYG_ST *pPolyG, _FS_PGBST_LIGHTDATA *lpOut,
                                            _FS_PGBFST_PARAM_ANIM_DATA *lpPrmAry, unsigned int dwKeyNum)
{
    PS2_FRAME(0x50);
    if (lpOut == 0 || lpPrmAry == 0 || pPolyG->fNowTimeCut < 0.0f) return 1;
    float now = pPolyG->fNowTimeCut;
    float fFrm = pPolyG->fFrmMSec;
    unsigned int dwMax = dwKeyNum - 1;
    _FS_PGBFST_PARAM_ANIM_DATA *key = lpPrmAry;
    float fIn = (float)key->wKeyFrm * fFrm;
    float fStart = 0.0f, fLen = 0.0f;
    unsigned int dwLpn;
    for (dwLpn = 0; dwLpn < dwMax; dwLpn++, key++) {
        fStart = fIn;
        fLen = (float)key->wFrmLen * fFrm;
        fIn = fIn + fLen;
        if (now < fIn) break;
    }
    if (dwLpn >= dwMax) return 1;
    _FS_PGBFST_PARAM_ANIM_DATA *next = key + 1;
    float t = ps2_div(now - fStart, fLen);
    fsPs2PolyGVu0_Vec4Lerp(&lpOut->fDiffuse, &key->fClrDif, &next->fClrDif, t);
    fsPs2PolyGVu0_Vec4Lerp(&lpOut->fSpeculer, &key->fClrSpc, &next->fClrSpc, t);
    fsPs2PolyGVu0_Vec4Lerp(&lpOut->fAmbient, &key->fClrAmb, &next->fClrAmb, t);
    lpOut->fIntensity = key->fIntensity + t * (next->fIntensity - key->fIntensity);
    lpOut->fRange = key->fRange + t * (next->fRange - key->fRange);
    lpOut->fSpotHotSize = key->fSpotHotSize + t * (next->fSpotHotSize - key->fSpotHotSize);
    lpOut->fSpotFallSize = key->fSpotFallSize + t * (next->fSpotFallSize - key->fSpotFallSize);
    return 0;
}

/* FOV keys (32-bit frame fields): linear between keys, the current key's FOV past the last key */
static float fsPs2PolyG_GetCurrentFov(_FS_POLYG_ST *pPolyG, _FS_PGBFST_CAMERA_FOV_DATA *lpFovAry, unsigned int dwKeyNum)
{
    if (lpFovAry == 0 || pPolyG->fNowTimeCut < 0.0f) return 0.0f;
    float now = pPolyG->fNowTimeCut;
    float fFrm = pPolyG->fFrmMSec;
    unsigned int dwMax = dwKeyNum - 1;
    _FS_PGBFST_CAMERA_FOV_DATA *key = lpFovAry;
    float fIn = (float)key->dwKeyFrm * fFrm;
    float fStart = 0.0f, fLen = 0.0f;
    unsigned int dwLpn;
    for (dwLpn = 0; dwLpn < dwMax; dwLpn++, key++) {
        fStart = fIn;
        fLen = (float)key->dwFrmLen * fFrm;
        fIn = fIn + fLen;
        if (now < fIn) break;
    }
    if (dwLpn >= dwMax) return key->fFov;
    float t = ps2_div(now - fStart, fLen);
    float f0 = key[0].fFov;
    return f0 + t * (key[1].fFov - f0);
}

/* the camera whose [dwSetFrm, dwEndFrm + 1) frame range holds the current time, or 0 */
static _FS_PGBFST_CAMERA_DATA_HEADER *fsPs2PolyG_GetCurrentCam(_FS_POLYG_ST *pPolyG,
                                                               _FS_PGBFST_CAMERA_DATA_HEADER *lpCamAry)
{
    if (lpCamAry == 0 || pPolyG->fNowTimeCut < 0.0f) return 0;
    float now = pPolyG->fNowTimeCut;
    unsigned int n = pPolyG->dwCamDtNum;
    unsigned int i;
    _FS_PGBFST_CAMERA_DATA_HEADER *cam = lpCamAry;
    for (i = 0; i < n; i++, cam++) {
        float fFrm = pPolyG->fFrmMSec;
        float fA = (float)(unsigned int)(cam->dwEndFrm + 1) * fFrm;
        float fB = (float)cam->dwSetFrm * fFrm;
        if (fB <= now && now < fA) break;
    }
    if (i >= n) cam = 0;
    return cam;
}

/* Noise offsets: strength from the key track (Hermite) or the fixed fStr, NoiseAtTime into pos_n (dwFPosAng 0) /
 * ang_n (1) / a throw-away local (other), w = 1.0f as the "noise set" flag; chained via dwNxPrmOffset */
static _HRESULT fsPs2PolyG_GetCurrentNoise(_FS_POLYG_ST *pPolyG, _FS_PGBVEC_POSANG *vOut,
                                           _FS_PGBFST_NOISE_PARAM *lpNosDt)
{
    PS2_FRAME(0x90);
    if (vOut == 0 || lpNosDt == 0 || pPolyG->fNowTimeCut < 0.0f) return 1;
    _D3DXVECTOR4 vDmy __attribute__((aligned(16)));
    _FS_NOISEST_NOISE_PARAM stPrm __attribute__((aligned(16)));
    _D3DXVECTOR4 *dst;
    if (lpNosDt->dwFPosAng == 1)
        dst = &vOut->ang_n;
    else if (lpNosDt->dwFPosAng == 0)
        dst = &vOut->pos_n;
    else
        dst = &vDmy;
    memcpy(&stPrm, PS2_LQ_SRC(lpNosDt), 0x30);   /* three lq: low 4 address bits ignored */
    dst->w = 1.0f;
    if (lpNosDt->dwAnmDtOffset == 0) {
        stPrm.fStr = lpNosDt->stPrm.fStr;
    } else {
        _FS_PGBFST_NOISE_STRENGTH_ANIM *key =
            (_FS_PGBFST_NOISE_STRENGTH_ANIM *)((char *)pPolyG->lpFile + lpNosDt->dwAnmDtOffset);
        unsigned int dwMax = lpNosDt->dwKeyNum - 1;
        float fFrm = pPolyG->fFrmMSec;
        float fIn = (float)key->wKeyFrm * fFrm;
        float fStart = 0.0f, fLen = 0.0f;
        float now = pPolyG->fNowTimeCut;
        unsigned int dwLpn;
        for (dwLpn = 0; dwLpn < dwMax; dwLpn++, key++) {
            fStart = fIn;
            fLen = (float)key->wFrmLen * fFrm;
            fIn = fIn + fLen;
            if (now < fIn) break;
        }
        if (dwLpn < dwMax) {
            _FS_PGBFST_NOISE_STRENGTH_ANIM *next = key + 1;
            float t = ps2_div(pPolyG->fNowTimeCut - fStart, fLen);
            fsPs2PolyGVu0_Vec4Hermite(&stPrm.fStr, &key->fStr, &key->fStrOT, &next->fStr, &next->fStrIT, t);
        } else {
            stPrm.fStr = key->fStr;
        }
    }
    fsPs2PGBNoise_NoiseAtTime(&pPolyG->CalcNoise, dst, pPolyG->fNowTimeCut, pPolyG->fTimeLenCut, &stPrm);
    if (lpNosDt->dwNxPrmOffset)
        return fsPs2PolyG_GetCurrentNoise(pPolyG, vOut,
                                          (_FS_PGBFST_NOISE_PARAM *)((char *)pPolyG->lpFile + lpNosDt->dwNxPrmOffset));
    return 0;
}

/* Tod (pos / ang / scl Hermite) keys: returns the current key (not clamped: key[dwKeyNum - 1] when the time is past
 * the end), *pRto = position inside it (only when its length != 0); while Calc computes the camera, a key shorter
 * than 3 frames or the track's first frame disables the sound Doppler effect */
static _FS_PGBFST_TOD_DATA *fsPs2PolyG_GetCurrentTod(_FS_POLYG_ST *pPolyG, float *pRto, _FS_PGBFST_TOD_DATA *lpTodAry,
                                                     unsigned int dwKeyNum)
{
    PS2_FRAME(0x20);
    unsigned int dwMax = dwKeyNum - 1;
    float fStart = 0.0f, fLen = 0.0f;
    /* the asm reads lpTodAry->wKeyFrm before the null test */
    if (lpTodAry == 0 || pPolyG->fNowTimeCut < 0.0f) return 0;
    float fFrm = pPolyG->fFrmMSec;
    float fIn = (float)lpTodAry->wKeyFrm * fFrm;
    float fOutTop = fIn;
    float now = pPolyG->fNowTimeCut;
    _FS_PGBFST_TOD_DATA *key = lpTodAry;
    for (unsigned int dwLpn = 0; dwLpn < dwMax; dwLpn++, key++) {
        fStart = fIn;
        fLen = (float)key->wFrmLen * fFrm;
        fIn = fIn + fLen;
        if (now < fIn) break;
    }
    if (fLen != 0.0f && pRto) *pRto = ps2_div(now - fStart, fLen);
    if (s_bNowComputeCameraTod) {
        if ((int)key->wFrmLen < 3 || pPolyG->fNowTimeCut - fOutTop < pPolyG->fFrmMSec) DF_SND_SetDopplerDisable(2);
    }
    return key;
}

/* ---- G3: Analyze (attach a file / advance cuts) and Calc (per-frame evaluation) ---- */

/* dwTgt == 0: attach a new file (cut tables, whole-movie length); dwTgt != 0: retire the current SFX state and
 * advance dwTgt cuts. Then set up the current cut: its tables, the per-cut buffer carve-up, model tags, the static
 * camera, light init and the SFX object / data lists (in the other half of the SFX buffer). Returns 1 without a file
 * or frame time, or when there is no next cut (end of the movie). Original quirks kept: dwFrmLenCut = dwCutEnd while
 * the whole length sums dwCutFrm; fNowTimeCut starts at 0.01f; the light init copies 0x34 bytes (fRange / hot / fall
 * sizes stay uninitialised); pMatAryFirst points past the model's matrices; no bounds checks against the buffers. */
static _HRESULT fsPs2PolyG_Analyze(_FS_POLYG_ST *pPolyG, unsigned int dwTgt)
{
    PS2_FRAME(0x160);
    if (pPolyG->fFrmMSec == 0.0f) return 1;
    if (pPolyG->lpFile == 0) return 1;
    char *base = (char *)pPolyG->lpFile;
    _FS_PGBST_SFXOBJ *pdtSubLastObj = 0;
    if (dwTgt == 0) {
        pPolyG->lpAllHD = (_FS_PGBFST_SCENE_FILEHEADER *)pPolyG->lpFile;
        pPolyG->lpCutHD = (_FS_PGBFST_CUT_DATA_HEADER *)(base + 0x10);
        pPolyG->dwCutNum = pPolyG->lpAllHD->dwCutNum;
        pPolyG->dwFrmLenCut = 0;
        pPolyG->dwFrmLenAll = 0;
        pPolyG->dwNowCut = 0;
        if (pPolyG->lpTmpCut != 0) {
            pPolyG->ppCutHDBuf = (_FS_PGBFST_CUT_DATA_HEADER **)pPolyG->lpTmpCut;
            pPolyG->dwTmpOthSize = ((pPolyG->dwCutNum + 3) >> 2) << 4;
            pPolyG->ppMdlLsBuf = (_FS_PGBFST_SET_MODEL_LIST **)((char *)pPolyG->lpTmpCut + pPolyG->dwTmpOthSize);
            pPolyG->dwTmpOthSize = pPolyG->dwTmpOthSize + (((pPolyG->dwCutNum + 3) >> 2) << 4);
            pPolyG->dwTmpCutSize = pPolyG->dwTmpCutSize - pPolyG->dwTmpOthSize;
            pPolyG->lpTmpCut = (char *)pPolyG->lpTmpCut + pPolyG->dwTmpOthSize;
        }
        _FS_PGBFST_CUT_DATA_HEADER **a = pPolyG->ppCutHDBuf;
        _FS_PGBFST_SET_MODEL_LIST **m = pPolyG->ppMdlLsBuf;
        _FS_PGBFST_CUT_DATA_HEADER *cut = pPolyG->lpCutHD;
        if (a == 0 || m == 0) return 1;
        /* bounded by the chain, not by dwCutNum (original) */
        while (cut != 0 && m != 0) {
            pPolyG->dwFrmLenAll = pPolyG->dwFrmLenAll + cut->dwCutFrm;
            *a = cut;
            *m = (_FS_PGBFST_SET_MODEL_LIST *)((char *)cut + 0x40);
            if (cut->dwNxCutOffset == 0) break;
            a++;
            m++;
            cut = (_FS_PGBFST_CUT_DATA_HEADER *)(base + cut->dwNxCutOffset);
        }
        pPolyG->fTimeLenAll = (float)pPolyG->dwFrmLenAll * pPolyG->fFrmMSec;
        pPolyG->fNowTimeAll = 0.0f;
        pPolyG->fNowTimeCut = 0x1.47ae14p-7f;   /* 0x3c23d70a (0.01f) */
        pPolyG->dwFlag = pPolyG->dwFlag & ~2u;
    } else {
        /* reset the current SFX buffer's "on" items (they are retired into stTmpSfxSub by the swap below) */
        unsigned int n = pPolyG->stTmpSfxBuf.dwObjNum;
        if (n != 0) {
            _FS_PGBST_SFXOBJ *obj = pPolyG->stTmpSfxBuf.lpObj;
            while (n-- != 0) {
                _FS_PGBST_SFXDATA *lastOpe = 0;
                unsigned int cnt = 0;
                obj->lpFstCur = 0;
                obj->dwOpeNum = 0;
                obj->dwOnNum = 0;
                for (_FS_PGBST_SFXDATA *d = obj->lpFstDt; d != 0; d = d->lpNxDt) {
                    if (d->dwSetOut == 1) {
                        if (obj->lpFstCur == 0) obj->lpFstCur = d;
                        else lastOpe->lpNxCur = d;
                        d->dwSetOut = 0;
                        lastOpe = d;
                        if (obj->dwType != 1) cnt++;
                    }
                }
                obj->dwOpeNum = cnt;
                pPolyG->dwSfxOpeAll = pPolyG->dwSfxOpeAll + cnt;
                if (obj->lpNxObj == 0) break;
                obj = obj->lpNxObj;
            }
            pdtSubLastObj = obj;   /* the last object reached (one past the counted ones if the chain is longer) */
        }
        while (dwTgt != 0 && pPolyG->lpCutHD != 0) {
            unsigned int nx = pPolyG->lpCutHD->dwNxCutOffset;
            if (nx == 0) return 1;   /* no next cut: end of the movie */
            pPolyG->lpCutHD = (_FS_PGBFST_CUT_DATA_HEADER *)(base + nx);
            dwTgt--;
            pPolyG->dwNowCut++;
        }
    }

    _FS_PGBFST_CUT_DATA_HEADER *cut = pPolyG->lpCutHD;
    pPolyG->dwMdlLsNum = cut->dwMdlLstNum;
    pPolyG->lpMdlLs = (_FS_PGBFST_SET_MODEL_LIST *)((char *)cut + 0x40);
    pPolyG->dwMdlDtNum = cut->dwMdlDtNum;
    pPolyG->lpMdlDt = (_FS_PGBFST_SET_MODEL_DATA *)(base + cut->dwMdlDtOffset);
    pPolyG->dwCamDtNum = cut->dwCamDtNum;
    pPolyG->lpCamHD = (_FS_PGBFST_CAMERA_DATA_HEADER *)(base + cut->dwCamDtOffset);
    pPolyG->dwLgtDtNum = cut->dwLgtDtNum;
    pPolyG->lpLgtTb = (_FS_PGBFST_LIGHT_TABLE *)(base + cut->dwLgtDtOffset);
    pPolyG->dwSfxDtNum = cut->dwSfxDtNum;
    pPolyG->lpSfxTb = (_FS_PGBFST_SFX_TABLE *)(base + cut->dwSfxDtOffset);
    pPolyG->dwFrmLenCut = cut->dwCutEnd;
    pPolyG->fTimeLenCut = (float)pPolyG->dwFrmLenCut * pPolyG->fFrmMSec;
    pPolyG->fNowTimeCut = pPolyG->fNowTimeCut + (float)cut->dwCutStart * pPolyG->fFrmMSec;

    /* per-cut buffer: lights, one camera, model tags + matrices (too small: pointers keep their old values) */
    if (pPolyG->lpTmpCut != 0) {
        pPolyG->pstLgtFst = (_FS_PGBST_LIGHTDATA *)pPolyG->lpTmpCut;
        unsigned int sz = pPolyG->dwLgtDtNum * 0x70;
        if (pPolyG->dwTmpCutSize >= sz) {
            pPolyG->pstCamFst = (_FS_PGBST_CAMDATA *)((char *)pPolyG->lpTmpCut + sz);
            sz = sz + 0x50;
            if (pPolyG->dwTmpCutSize >= sz) {
                pPolyG->pstMdlFst = (_FS_PGBST_MDLTAG *)((char *)pPolyG->lpTmpCut + sz);
                pPolyG->dwTmpCutUse = sz + (pPolyG->dwTmpCmnSize + pPolyG->dwTmpOthSize);
            }
        }
    }

    if (pPolyG->dwCalcFlag & 1) {
        unsigned int sz = pPolyG->dwMdlDtNum << 5;
        _FS_PGBST_MDLTAG *pre = 0;
        _FS_PGBST_MDLTAG *tag = pPolyG->pstMdlFst;
        _FS_PGBFST_SET_MODEL_DATA *mdl = pPolyG->lpMdlDt;
        pPolyG->dwTmpCutUse = pPolyG->dwTmpCutUse + sz;
        char *mats = (char *)tag + sz;
        if (pPolyG->dwMdlDtNum != 0) {
            unsigned int i = 0;
            while (mdl != 0) {
                if (pre) pre->pstNx = tag;
                tag->pstPre = pre;
                tag->pstNx = 0;
                tag->dwObjNum = mdl->dwObjNum;
                tag->dwSetObjId = mdl->dwSetObjId;
                tag->dwObjNameId = mdl->dwObjNameId;
                tag->pModelName = mdl->strModelName;
                if (mdl->dwAnmDtOffset != 0) {
                    tag->pstAnmEntry = (_D3DXMATRIX *)mats;
                    mats += tag->dwObjNum << 6;
                    tag->pMatAryFirst = (_D3DXMATRIX *)mats;   /* after the increment (original) */
                    pPolyG->dwTmpCutUse = pPolyG->dwTmpCutUse + (tag->dwObjNum << 6);
                }
                i++;
                pre = tag;
                tag++;
                mdl++;
                if (!(i < pPolyG->dwMdlDtNum)) break;
            }
        }
    }

    if (pPolyG->dwCalcFlag & 2) {
        _FS_PGBFST_CAMERA_DATA_HEADER *hd = pPolyG->lpCamHD;
        _FS_PGBST_CAMDATA *mOut = pPolyG->pstCamFst;
        _FS_PGBFST_CAMERA_DATA_HEADER *cam = fsPs2PolyG_GetCurrentCam(pPolyG, hd);
        pPolyG->pstCamOp = 0;
        if (cam != 0) {
            if (cam->dwTodDtOffset == 0 && cam->dwTgtDtOffset == 0) {   /* static camera: matrix once here */
                _FS_PGBVEC_POSANG pCam __attribute__((aligned(16)));
                memset(&pCam, 0, 0x50);
                pCam.pos = cam->fInitTrs;
                pCam.ang = cam->fInitRot;
                fsPs2PolyG_Calc_CamMatFree(pPolyG, &mOut->matWrdView, &pCam);
            }
            if (cam->dwFovDtOffset == 0) mOut->fFov = cam->fInitFov;
            pPolyG->pstCamOp = pPolyG->pstCamFst;
        }
    }

    if (pPolyG->dwCalcFlag & 4) {
        _FS_PGBST_LIGHTDATA *dst = pPolyG->pstLgtFst;
        _FS_PGBFST_LIGHT_TABLE *tbl = pPolyG->lpLgtTb;
        if (pPolyG->dwLgtDtNum != 0) {
            unsigned int i = 0;
            do {
                if (tbl->dwTodDtOffset == 0) ps2_memcpy(dst, &tbl->fInitTrs, 0x20);   /* fInitTrs / Rot -> fPos / Ang */
                if (tbl->dwPrmDtOffset == 0) {
                    ps2_memcpy(&dst->fDiffuse, &tbl->fClrDif, 0x34);   /* 3 colours + fIntensity only (original) */
                    dst->dwType = tbl->dwType;
                    dst->dwDecay = tbl->dwDecay;
                }
                i++;
                tbl++;
                dst++;
            } while (i < pPolyG->dwLgtDtNum);
        }
    }

    if (pPolyG->dwCalcFlag & 8) {
        /* swap the two SFX buffers (also on attach) and clear the new current one */
        _FS_PGBST_SFXBUF tmp = pPolyG->stTmpSfxBuf;
        pPolyG->stTmpSfxBuf = pPolyG->stTmpSfxSub;
        pPolyG->stTmpSfxSub = tmp;
        ps2_memset(pPolyG->stTmpSfxBuf.lpObj, 0, pPolyG->dwTmpCmnSize >> 1);
        _FS_PGBST_SFXOBJ *first = pPolyG->stTmpSfxBuf.lpObj;
        _FS_PGBST_SFXOBJ *prev = 0;
        char *p = (char *)first;
        unsigned int cnt = 0;
        _FS_PGBFST_SFX_TABLE *tbl = pPolyG->lpSfxTb;
        if (pPolyG->dwSfxDtNum != 0) {
            unsigned int i = 0;
            do {
                if (tbl->dwPrmDtOffset == 0) {
                    tbl++;   /* no data: no object slot */
                } else {
                    _FS_PGBST_SFXOBJ *po = (_FS_PGBST_SFXOBJ *)p;
                    po->dwSfxNum = tbl->dwSfxDtNum;
                    po->lpOnFile = tbl;
                    if (prev) prev->lpNxObj = po;
                    if (tbl->dwObjId != 0xffffffffu && tbl->dwTodDtOffset == 0) {
                        _D3DXMATRIX matRot __attribute__((aligned(16)));
                        _D3DXMATRIX matTrans __attribute__((aligned(16)));
                        po->fPos = tbl->fInitTrs;
                        po->fAng = tbl->fInitRot;
                        fsPs2PolyGVu0_MatrixIdentity(&matRot);
                        fsPs2PolyG_Calc_VecRotMat(pPolyG, &matRot, &po->fAng);
                        fsPs2PolyGVu0_MatrixTranslation(&matTrans, po->fPos.x, po->fPos.y, po->fPos.z);
                        fsPs2PolyGVu0_MatrixMultiply(&po->matPosAng, &matRot, &matTrans);
                    }
                    prev = po;
                    p += 0x90;
                    tbl++;
                    cnt++;
                }
                i++;
            } while (i < pPolyG->dwSfxDtNum);
        }
        pPolyG->stTmpSfxBuf.dwObjNum = cnt;
        pPolyG->stTmpSfxBuf.lpPrm = (_FS_PGBST_SFXDATA *)p;   /* the SFXDATA array follows the objects */
        for (_FS_PGBST_SFXOBJ *obj = first; obj != 0 && cnt != 0; obj = obj->lpNxObj) {
            obj->lpFstDt = (_FS_PGBST_SFXDATA *)p;
            _FS_PGBFST_SFX_PARAM *prm = (_FS_PGBFST_SFX_PARAM *)(base + obj->lpOnFile->dwPrmDtOffset);
            _FS_PGBST_SFXDATA *prevD = 0;
            unsigned int num = obj->dwSfxNum;
            for (unsigned int j = 0; j < num; j++) {
                _FS_PGBST_SFXDATA *d = (_FS_PGBST_SFXDATA *)p;
                d->dwType = prm->dwType;
                d->dwSfxID = prm->dwEncId;
                d->dwIDataNum = prm->dwIDtNum;
                d->dwFDataNum = prm->dwFDtNum;
                d->pdwIData = (unsigned int *)(base + prm->dwIDtOffset);
                d->pfFData = (float *)(base + prm->dwFDtOffset);
                d->lpObj = obj;
                d->fOnTime = (float)prm->dwSetFrm * pPolyG->fFrmMSec;
                d->fOffTime = (float)prm->dwEndFrm * pPolyG->fFrmMSec;
                d->lpPreDt = prevD;
                if (prevD) prevD->lpNxDt = d;
                prevD = d;
                p += 0x40;
                cnt++;   /* the asm counts here too; only tested for != 0 */
                prm = (_FS_PGBFST_SFX_PARAM *)(base + prm->dwNxPrmOffset);
            }
            obj->lpEndDt = prevD;
        }
        /* link the retired buffer's last object to the new objects (Calc's pstSfxFst runs through both) */
        if (pPolyG->stTmpSfxBuf.dwObjNum != 0 && pdtSubLastObj != 0) {
            pdtSubLastObj->lpNxObj = pPolyG->stTmpSfxBuf.lpObj;
            pPolyG->stTmpSfxSub.dwObjNum = pPolyG->stTmpSfxSub.dwObjNum + pPolyG->stTmpSfxBuf.dwObjNum;
        }
    }
    return 0;
}

/* The Tod-track block Calc inlines five times: v must be zeroed by the caller; *pRto is the original's ratio stack
 * slot (GetCurrentTod leaves it unwritten for a zero-length key: a one-key track reads it stale, and the "next" key
 * is then past the track). */
static inline void pg_calc_tod(_FS_POLYG_ST *pPolyG, _FS_PGBVEC_POSANG *v, float *pRto, _FS_PGBFST_TOD_DATA *lpTod,
                               unsigned int dwKeyNum, _FS_PGBFST_NOISE_PARAM *lpNos)
{
    if (lpTod == 0 || dwKeyNum == 0) return;
    _FS_PGBFST_TOD_DATA *key = fsPs2PolyG_GetCurrentTod(pPolyG, pRto, lpTod, dwKeyNum);
    /* `now < 0` is c.olt: a NaN time proceeds */
    if (lpTod == 0 || pPolyG->fNowTimeCut < 0.0f || key == 0) return;
    _FS_PGBFST_TOD_DATA *next = key + 1;
    fsPs2PolyGVu0_Vec4Hermite(&v->pos, &key->fPos, &key->fPosOT, &next->fPos, &next->fPosIT, *pRto);
    fsPs2PolyGVu0_Vec4Hermite(&v->ang, &key->fAng, &key->fAngOT, &next->fAng, &next->fAngIT, *pRto);
    fsPs2PolyGVu0_Vec4Lerp(&v->scl, &key->fScl, &next->fScl, *pRto);   /* scl is never read by Calc */
    v->pos.w = 1.0f;
    v->ang.w = 1.0f;
    fsPs2PolyG_GetCurrentNoise(pPolyG, v, lpNos);
}

static inline _FS_PGBFST_NOISE_PARAM *pg_nos(char *base, unsigned int off)
{
    return off ? (_FS_PGBFST_NOISE_PARAM *)(base + off) : 0;
}

/* Per-frame update: advance the time by fTimeMv (not on the first call after an attach), step to the next cut when
 * the current one is over (one cut per call; *pdwCutF = 1), then evaluate the model, camera, light and SFX tracks.
 * Returns the time left in the whole movie, -9999.0f for a negative step or at the end of the movie. */
float fsPs2PolyG_Calc(_FS_POLYG_ST *pPolyG, float fTimeMv, unsigned int *pdwCutF)
{
    PS2_FRAME(0x2e0);
    /* the five ratio slots of the inlined Tod blocks: read stale for a one-key track (original stack offsets) */
    PS2_STACK(float, fRtoSfx, 0x2cc);
    PS2_STACK(float, fRtoLgt, 0x2d0);
    PS2_STACK(float, fRtoTgt, 0x2d4);
    PS2_STACK(float, fRtoTgtEye, 0x2d8);
    PS2_STACK(float, fRtoFreeEye, 0x2dc);
    if (fTimeMv < 0.0f) return -9999.0f;   /* 0xc61c3c00 */
    unsigned int flag = pPolyG->dwFlag;
    if (flag & 2) {
        pPolyG->fNowTimeAll = pPolyG->fNowTimeAll + fTimeMv;
        pPolyG->fNowTimeCut = pPolyG->fNowTimeCut + fTimeMv;
    } else {
        pPolyG->dwFlag = flag | 2;
    }
    pPolyG->dwSfxOpeAll = 0;
    if (pdwCutF) *pdwCutF = 0;
    pPolyG->pstSfxFst = pPolyG->stTmpSfxBuf.lpObj;
    if (pPolyG->fTimeLenCut <= pPolyG->fNowTimeCut) {
        pPolyG->fNowTimeCut = pPolyG->fNowTimeCut - pPolyG->fTimeLenCut;
        if (pdwCutF) *pdwCutF = 1;
        if (fsPs2PolyG_Analyze(pPolyG, 1)) return -9999.0f;   /* no next cut: end of the movie */
        if (pPolyG->stTmpSfxSub.dwObjNum == 0) pPolyG->pstSfxFst = pPolyG->stTmpSfxBuf.lpObj;
    }
    char *base = (char *)pPolyG->lpFile;

    /* models: ends on the tag chain, dwMdlDtNum is only tested for != 0 (original) */
    if (pPolyG->dwCalcFlag & 1) {
        _FS_PGBFST_SET_MODEL_DATA *mdl = pPolyG->lpMdlDt;
        _FS_PGBST_MDLTAG *tag = pPolyG->pstMdlFst;
        while (pPolyG->dwMdlDtNum != 0 && tag != 0) {
            unsigned int off = mdl->dwAnmDtOffset;
            if (off != 0) {
                _FS_PGBFST_ANIM_TABLE *tbl = (_FS_PGBFST_ANIM_TABLE *)(base + off);
                if (tbl->dwTodDtOffset != 0) UpdateMatrix(pPolyG, 0, tbl, tag->pstAnmEntry);
            }
            tag = tag->pstNx;
            mdl++;
        }
    }

    if (pPolyG->dwCalcFlag & 2) {
        _FS_PGBFST_CAMERA_DATA_HEADER *hd = pPolyG->lpCamHD;
        _FS_PGBST_CAMDATA *mOut = pPolyG->pstCamFst;
        _FS_PGBFST_CAMERA_DATA_HEADER *cam = fsPs2PolyG_GetCurrentCam(pPolyG, hd);
        pPolyG->pstCamOp = 0;
        s_bNowComputeCameraTod = 1;
        if (cam != 0) {
            _FS_PGBVEC_POSANG vCam __attribute__((aligned(16)));
            unsigned int type = cam->dwCamType;
            if (type == 1) {   /* target camera */
                unsigned int tgtOff = cam->dwTgtDtOffset;
                if (tgtOff != 0) {
                    _FS_PGBFST_TARGET_TABLE *tbl = (_FS_PGBFST_TARGET_TABLE *)(base + tgtOff);
                    _FS_PGBVEC_POSANG vTgt __attribute__((aligned(16)));
                    memset(&vCam, 0, 0x50);
                    memset(&vTgt, 0, 0x50);
                    if (cam->dwTodDtOffset != 0) {
                        _FS_PGBFST_TOD_DATA *lpTod = (_FS_PGBFST_TOD_DATA *)(base + cam->dwTodDtOffset);
                        pg_calc_tod(pPolyG, &vCam, &fRtoTgtEye, lpTod, cam->dwTodKeyNum,
                                    pg_nos(base, cam->dwNosDtOffset));
                    } else {
                        vCam.pos = cam->fInitTrs;
                        vCam.ang = cam->fInitRot;
                    }
                    unsigned int tkeys = tbl->dwTodKeyNum;
                    if (tkeys != 0) {   /* the target's keys follow its table */
                        _FS_PGBFST_TOD_DATA *lpTod = (_FS_PGBFST_TOD_DATA *)((char *)tbl + 0x30);
                        pg_calc_tod(pPolyG, &vTgt, &fRtoTgt, lpTod, tkeys, pg_nos(base, tbl->dwNosDtOffset));
                    } else {
                        vTgt.pos = tbl->fInitTrs;
                    }
                    fsPs2PolyG_Calc_CamMatTgt(pPolyG, &mOut->matWrdView, &vCam, &vTgt);
                }
            } else if (type == 0) {   /* free camera (a Tod array with zero keys still builds a zero pose) */
                memset(&vCam, 0, 0x50);
                if (cam->dwTodDtOffset != 0) {
                    _FS_PGBFST_TOD_DATA *lpTod = (_FS_PGBFST_TOD_DATA *)(base + cam->dwTodDtOffset);
                    pg_calc_tod(pPolyG, &vCam, &fRtoFreeEye, lpTod, cam->dwTodKeyNum,
                                pg_nos(base, cam->dwNosDtOffset));
                } else {
                    vCam.pos = cam->fInitTrs;
                    vCam.ang = cam->fInitRot;
                }
                fsPs2PolyG_Calc_CamMatFree(pPolyG, &mOut->matWrdView, &vCam);
            }
            if (cam->dwFovDtOffset != 0)
                mOut->fFov = fsPs2PolyG_GetCurrentFov(pPolyG, (_FS_PGBFST_CAMERA_FOV_DATA *)(base + cam->dwFovDtOffset),
                                                      cam->dwFovKeyNum);
            else
                mOut->fFov = cam->fInitFov;
            pPolyG->pstCamOp = pPolyG->pstCamFst;
        }
        s_bNowComputeCameraTod = 0;
    }

    if (pPolyG->dwCalcFlag & 4) {
        _FS_PGBST_LIGHTDATA *dst = pPolyG->pstLgtFst;
        _FS_PGBFST_LIGHT_TABLE *tbl = pPolyG->lpLgtTb;
        if (pPolyG->dwLgtDtNum != 0) {
            unsigned int i = 0;
            do {
                if (tbl->dwTodDtOffset != 0) {
                    _FS_PGBVEC_POSANG vLgt __attribute__((aligned(16)));
                    _FS_PGBFST_TOD_DATA *lpTod = (_FS_PGBFST_TOD_DATA *)(base + tbl->dwTodDtOffset);
                    _FS_PGBFST_NOISE_PARAM *lpNos = pg_nos(base, tbl->dwNosDtOffset);
                    memset(&vLgt, 0, 0x50);
                    pg_calc_tod(pPolyG, &vLgt, &fRtoLgt, lpTod, tbl->dwTodKeyNum, lpNos);
                    dst->fPos = vLgt.pos;   /* also when no key was found (zeros) */
                    dst->fAng = vLgt.ang;
                }
                if (tbl->dwPrmDtOffset != 0)
                    fsPs2PolyG_GetCurrentLgtPrm(pPolyG, dst, (_FS_PGBFST_PARAM_ANIM_DATA *)(base + tbl->dwPrmDtOffset),
                                                tbl->dwPrmKeyNum);
                i++;
                tbl++;
                dst++;
            } while (i < pPolyG->dwLgtDtNum);
        }
    }

    if (pPolyG->dwCalcFlag & 8) {
        _FS_PGBST_SFXOBJ *obj = pPolyG->stTmpSfxBuf.lpObj;
        unsigned int cnt = pPolyG->stTmpSfxBuf.dwObjNum;   /* read once, only tested for != 0 (original) */
        unsigned int s = 0;   /* register s1: carries over between data items and objects (original) */
        _D3DXMATRIX matE __attribute__((aligned(16)));
        fsPs2PolyGVu0_MatrixIdentity(&matE);
        while (obj != 0 && cnt != 0) {
            _FS_PGBFST_SFX_TABLE *tbl = obj->lpOnFile;
            unsigned int onNum = 0, opeNum = 0;
            _FS_PGBST_SFXDATA *lastOpe = 0;
            obj->lpFstCur = 0;
            unsigned int id = tbl->dwObjId;
            if (id == 0xfffffffeu || id == 0xffffffffu) {   /* attached to a model object (-1) / a model (-2) */
                _FS_PGBST_MDLTAG *mdl = 0;
                if (pPolyG->dwMdlDtNum != 0)
                    for (mdl = pPolyG->pstMdlFst; mdl != 0 && mdl->dwSetObjId != tbl->dwAMdlId; mdl = mdl->pstNx) {}
                _D3DXMATRIX *src;
                if (id == 0xffffffffu)
                    src = mdl ? (_D3DXMATRIX *)((char *)mdl->pstAnmEntry + (tbl->dwAObjId << 6)) : &matE;
                else
                    src = mdl ? mdl->pstAnmEntry : &matE;
                memcpy(&obj->matPosAng, src, 0x40);
                if (id == 0xffffffffu) {
                    obj->dwType = 0;
                    obj->dwLinkId = 0;
                } else {
                    obj->dwType = 1;
                    obj->dwLinkId = tbl->dwAMdlId;
                }
            } else {
                if (tbl->dwTodDtOffset != 0) {   /* zero offset: the matrix stays as Analyze set it */
                    _FS_PGBVEC_POSANG vObj __attribute__((aligned(16)));
                    _D3DXMATRIX matRot __attribute__((aligned(16)));
                    _D3DXMATRIX matTrans __attribute__((aligned(16)));
                    _FS_PGBFST_TOD_DATA *lpTod = (_FS_PGBFST_TOD_DATA *)(base + tbl->dwTodDtOffset);
                    _FS_PGBFST_NOISE_PARAM *lpNos = pg_nos(base, tbl->dwNosDtOffset);
                    memset(&vObj, 0, 0x50);
                    pg_calc_tod(pPolyG, &vObj, &fRtoSfx, lpTod, tbl->dwTodKeyNum, lpNos);
                    obj->fPos = vObj.pos;
                    obj->fAng = vObj.ang;
                    fsPs2PolyGVu0_MatrixIdentity(&matRot);
                    fsPs2PolyG_Calc_VecRotMat(pPolyG, &matRot, &obj->fAng);
                    fsPs2PolyGVu0_MatrixTranslation(&matTrans, obj->fPos.x, obj->fPos.y, obj->fPos.z);
                    fsPs2PolyGVu0_MatrixMultiply(&obj->matPosAng, &matRot, &matTrans);
                }
                obj->dwType = 0;
                obj->dwLinkId = 0;
            }
            /* on / off state of every data item; items that changed are chained into the lpFstCur list */
            for (_FS_PGBST_SFXDATA *d = obj->lpFstDt; d != 0; d = d->lpNxDt) {
                unsigned int old = d->dwSetOut;
                int test;
                if (old == 1) {
                    test = 1;
                } else if (old == 0) {
                    s = (d->fOnTime <= pPolyG->fNowTimeCut) ? 1 : 0;
                    test = s != 0;
                } else {
                    test = 0;   /* s keeps the previous value */
                }
                if (test) s = (d->fOnTime <= d->fOffTime && d->fOffTime <= pPolyG->fNowTimeCut) ? 0 : 1;
                if (s == 1) onNum++;
                if (s != old) {
                    if (obj->lpFstCur == 0) obj->lpFstCur = d;
                    else lastOpe->lpNxCur = d;
                    lastOpe = d;
                    opeNum++;
                }
                d->dwSetOut = s;
            }
            obj->dwOpeNum = opeNum;
            obj->dwOnNum = onNum;
            pPolyG->dwSfxOpeAll = pPolyG->dwSfxOpeAll + opeNum;
            obj = obj->lpNxObj;
        }
    }
    return pPolyG->fTimeLenAll - pPolyG->fNowTimeAll;
}

/* the cut array starts at lpFile + 0x10 (after the scene header); no null check */
_FS_PGBFST_CUT_DATA_HEADER *fsPs2PolyG_GetPGBCut_DatArray(_FS_POLYG_ST *pPolyG)
{
    return (_FS_PGBFST_CUT_DATA_HEADER *)((char *)pPolyG->lpFile + 0x10);
}

_FS_PGBFST_SET_MODEL_DATA *fsPs2PolyG_GetPGBModelLst_DatArray(_FS_POLYG_ST *pPolyG, unsigned int cutID)
{
    char *base = (char *)pPolyG->lpFile;
    _FS_PGBFST_CUT_DATA_HEADER *cut = (_FS_PGBFST_CUT_DATA_HEADER *)(base + 0x10);
    return (_FS_PGBFST_SET_MODEL_DATA *)(base + cut[cutID].dwMdlDtOffset);
}

unsigned int fsPs2PolyG_GetPGBModelLst_DatNum(_FS_POLYG_ST *pPolyG, unsigned int cutID)
{
    _FS_PGBFST_CUT_DATA_HEADER *cut = (_FS_PGBFST_CUT_DATA_HEADER *)((char *)pPolyG->lpFile + 0x10);
    return cut[cutID].dwMdlDtNum;
}

/* Attach a movie file: split the work buffer (1/16 each for the two SFX buffers, the rest for the cut data), then
 * Analyze(pPolyG, 0) (a tail jump in the original: no frame of its own; Analyze's PS2_FRAME starts at the caller's sp) */
_HRESULT fsPs2PolyG_InitUseOB(_FS_POLYG_ST *pPolyG, void *pFile, unsigned int dwFileSize)
{
    if (pPolyG->lpTmp) {
        pPolyG->stTmpSfxBuf.lpObj = (_FS_PGBST_SFXOBJ *)pPolyG->lpTmp;
        unsigned int n = pPolyG->dwTmpSize >> 4;
        pPolyG->stTmpSfxSub.lpObj = (_FS_PGBST_SFXOBJ *)((char *)pPolyG->lpTmp + n);
        n = n + n;
        pPolyG->dwTmpCmnSize = n;
        pPolyG->dwTmpCutSize = pPolyG->dwTmpSize - n;
        pPolyG->lpTmpCut = (char *)pPolyG->lpTmp + n;
        pPolyG->stTmpSfxSub.dwObjNum = 0;
        pPolyG->stTmpSfxBuf.dwObjNum = 0;
    }
    pPolyG->lpFile = pFile;
    pPolyG->dwFileSize = dwFileSize;
    return fsPs2PolyG_Analyze(pPolyG, 0);
}

/* a name of 64+ characters is not copied (sFName keeps its old content) */
_HRESULT fsPs2PolyG_Create(_FS_POLYG_ST *pPolyG, char *psFile, float fFrmTm)
{
    PS2_FRAME(0x40);
    if (ps2_strlen(psFile) < 0x40) ps2_strcpy(pPolyG->sFName, psFile);
    pPolyG->fFrmMSec = fFrmTm;
    return 0;
}

_HRESULT fsPs2PolyG_AllocBuffer(_FS_POLYG_ST *pPolyG, void *lpBuf, unsigned int dwBufSize)
{
    if (pPolyG->lpTmp != 0 || dwBufSize == 0) return 1;
    if (lpBuf == 0 && dwBufSize != 0) dwBufSize = 0;   /* original: always true here when lpBuf is null */
    pPolyG->lpTmp = lpBuf;
    pPolyG->dwTmpSize = dwBufSize;
    return 0;
}

/* Clear the player state (lpMdlLs, dwMdlLsNum and everything from dwSfxOpeAll on are not touched: the original) and
 * build the noise tables (srand(0) + 512 rand()) */
_HRESULT fsPs2PolyG_Init(_FS_POLYG_ST *pPolyG)
{
    PS2_FRAME(0x20);
    pPolyG->sFName[0] = 0;
    pPolyG->lpTmpCut = 0;
    pPolyG->lpTmp = 0;
    pPolyG->lpFile = 0;
    pPolyG->dwTmpCutUse = 0;
    pPolyG->dwFlag = 0;
    pPolyG->dwTmpSize = 0;
    pPolyG->dwFileSize = 0;
    pPolyG->dwCalcFlag = 0xff;
    pPolyG->fFrmMSec = 0x1.0aeeeep+4f;   /* 0x41857777 (~16.68; PGP_Create / Create pass 0x41855556) */
    pPolyG->hFile = 0x80000000u;
    pPolyG->lpNxPG = 0;
    pPolyG->lpPrePG = 0;
    pPolyG->lpAllHD = 0;
    pPolyG->lpCutHD = 0;
    pPolyG->lpMdlDt = 0;
    pPolyG->lpCamHD = 0;
    pPolyG->lpLgtTb = 0;
    pPolyG->lpSfxTb = 0;
    pPolyG->dwSfxDtNum = 0;
    pPolyG->dwLgtDtNum = 0;
    pPolyG->dwCamDtNum = 0;
    pPolyG->dwMdlDtNum = 0;
    pPolyG->dwFrmLenCut = 0;
    pPolyG->dwFrmLenAll = 0;
    pPolyG->dwNowCut = 0;
    pPolyG->dwCutNum = 0;
    pPolyG->fNowTimeCut = 0.0f;
    pPolyG->fNowTimeAll = 0.0f;
    pPolyG->fTimeLenCut = 0.0f;
    pPolyG->fTimeLenAll = 0.0f;
    /* a zeroed 16-byte local copied into stTmpSfxSub, then stTmpSfxSub into stTmpSfxBuf */
    _FS_PGBST_SFXBUF stInit;
    memset(&stInit, 0, sizeof stInit);
    pPolyG->stTmpSfxSub = stInit;
    pPolyG->stTmpSfxBuf = pPolyG->stTmpSfxSub;
    fsPs2PGBNoise_Init(&pPolyG->CalcNoise);
    return 0;
}
