/* src/lib/calc.cpp - general math helpers (whole file): angle wrapping / stepping in radians, vector <-> angle
 * conversions (atan2, the 4096-entry Sintable or the fsmath VU0 sine), Kochanek-Bartels key interpolation
 * (DF_CALC_MotionStep), small matrix helpers and clamped adds.
 * Angles: ang[0] = pitch (x rotation, positive = down), ang[1] = yaw (atan2(x, z)). Angle wrapping is done with
 * loops (while > PI subtract 2 PI, while < -PI add 2 PI), so an angle far outside the range costs many iterations
 * but stays bit-exact. VU0 .xyz ops keep the w of the source register (written out where the original stores it). */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/vu0m.h"

static const float CALC_PI = 3.14159274f;       /* 0x40490fdb */
static const float CALC_PI2 = 6.28318548f;      /* 0x40c90fdb */
static const float CALC_HALF_PI = 1.57079637f;  /* 0x3fc90fdb */

/* Zvec2Matrix's static vec_Yone (0x0025caf0) */
static const float vec_Yone[4] = {0.0f, 1.0f, 0.0f, 0.0f};

/* the inlined wrap loops: to -PI..PI (the upper loop tests !(a <= PI)) */
static inline float calc_wrap_pi(float a)
{
    while (!(a <= CALC_PI)) a = a - CALC_PI2;
    while (a < -CALC_PI) a = a + CALC_PI2;
    return a;
}

/* go_angf's reduction to 0..2PI: a - trunc(a / 2PI) * 2PI, then one correction step each way */
static inline float calc_mod_pi2(float a)
{
    a = a - CALC_PI2 * (float)ps2_ftoi(a / CALC_PI2);
    if (a < 0.0f) a = a + CALC_PI2;
    if (!(a < CALC_PI2)) a = a - CALC_PI2;
    return a;
}

/* Sintable (float[5120] @0x272d70, lib/calc.c's data; the source port copies it from the user's ELF, sp_missing.py
 * ELF_DATA): 4096 sine steps per turn plus a 0x400 overhang so the cosine is Sintable[ix + 0x400].
 * Programmer note (checked s62, all 5120 entries bit-exact): the table is the output of an offline generator,
 *     Sintable[i] = (float)strtod(sprintf("%f", sin(2 * 3.141592655 * i / 4096)))
 * i.e. a double-precision sin with PI typed as 3.141592655 (slightly above real PI), printed to 6 decimals and
 * compiled back in as float literals. That is why the values are "0.001534" rather than sinf()'s, why
 * Sintable[2048] is -0.0 and Sintable[4096] +0.0 (the long PI overshoots), and why a few entries round the other way
 * from a true-PI table. The nearest entry is 1.4e-10 from a rounding edge, so any modern libm regenerates it. */

/* Sintable index of an angle: 4096 steps per turn; the cosine is the same table a quarter turn (0x400) later */
static inline int calc_sin_index(float a)
{
    return (ps2_ftoi((4096.0f * a) / CALC_PI2) + 0x1000) & 0xfff;
}

/* Direction of orgvec as angles, clamped to minx..maxx (pitch) and miny..maxy (yaw); when clamped, dstvec =
 * the clamped direction scaled to orgvec's length (dstvec.w = the rotation matrix's [2][3]). Returns 1 if clamped. */
int VectorClipXYang(float *dstvec, float *orgvec, float minx, float maxx, float miny, float maxy)
{
    int clipped = 0;
    float xz = orgvec[0] * orgvec[0];
    xz = xz + orgvec[2] * orgvec[2];
    float rx = ps2_atan2f(-orgvec[1], ps2_sqrt(xz));
    float ry = ps2_atan2f(orgvec[0], orgvec[2]);
    if (rx < minx) { rx = minx; clipped = 1; }
    if (!(rx <= maxx)) { rx = maxx; clipped = 1; }
    if (ry < miny) { ry = miny; clipped = 1; }
    if (!(ry <= maxy)) { ry = maxy; clipped = 1; }
    if (clipped) {
        float newmat[4][4] __attribute__((aligned(16)));
        sceVu0RotMatrixX(newmat, IdentityMtx, rx);
        sceVu0RotMatrixY(newmat, newmat, ry);
        float y2 = orgvec[1] * orgvec[1], x2 = orgvec[0] * orgvec[0];
        float len = x2 + y2;
        len = ps2_sqrt(len + orgvec[2] * orgvec[2]);
        float r0 = newmat[2][0] * len, r1 = newmat[2][1] * len, r2 = newmat[2][2] * len, w = newmat[2][3];
        dstvec[0] = r0; dstvec[1] = r1; dstvec[2] = r2; dstvec[3] = w;
    }
    return clipped;
}

/* Value of a key track at nFrame: outside curKey..next (or no next) = curKey's vector (all 4 words); else per
 * component linear (next->linear bit set, or all 3 bits = linear for xyz) or a Kochanek-Bartels Hermite curve with
 * curKey's / next's tension, continuity, bias; tangents scaled by the neighbouring key spans. w is not written. */
void DF_CALC_MotionStep(float *resVec, HermKeySt *curKey, int nFrame)
{
    HermKeySt *key0 = curKey, *key1 = curKey->next;
    float adj0 = 0.0f, adj1 = 0.0f;
    if (!key1 || nFrame < key0->frame || !(nFrame < key1->frame)) {
        memcpy(resVec, key0->vec, 16);
        return;
    }
    float d10 = (float)(key1->frame - key0->frame);
    float t = (float)(nFrame - key0->frame) / d10;
    if ((key1->linear & 7) == 7) {
        for (int ii = 0; ii < 3; ii++) resVec[ii] = key0->vec[ii] + t * (key1->vec[ii] - key0->vec[ii]);
        return;
    }
    float t2 = t * t;
    float t3 = t * t2;
    float tens0 = 1.0f - key0->tens, tens1 = 1.0f - key1->tens;
    float h2 = (3.0f * t2 - t3) - t3;
    float h4 = t3 - t2;
    float h3 = t + (h4 - t2);
    float dd0a = (1.0f + key0->bias) * (tens0 * (1.0f + key0->cont));
    float dd0b = (1.0f - key0->bias) * (tens0 * (1.0f - key0->cont));
    float ds1a = (1.0f + key1->bias) * (tens1 * (1.0f - key1->cont));
    float ds1b = (1.0f - key1->bias) * (tens1 * (1.0f + key1->cont));
    float h1 = 1.0f - h2;
    if (key0->prev) adj0 = d10 / (float)(key1->frame - key0->prev->frame);
    if (key1->next) adj1 = d10 / (float)(key1->next->frame - key0->frame);
    float dd0half = 0.5f * (dd0a + dd0b);
    float ds1half = 0.5f * (ds1a + ds1b);
    for (int ii = 0; ii < 3; ii++) {
        float p0 = key0->vec[ii], p1 = key1->vec[ii];
        float d = p1 - p0;
        float res;
        if (!(key1->linear & (1 << ii))) {
            float dd0 = d * dd0half;
            if (key0->prev) {
                float a = dd0a * (p0 - key0->prev->vec[ii]);
                dd0 = adj0 * (a + dd0b * d);
            }
            float ds1 = d * ds1half;
            if (key1->next) {
                float a = ds1b * (key1->next->vec[ii] - p1);
                ds1 = adj1 * (a + ds1a * d);
            }
            float s = h1 * p0 + h2 * p1;
            s = s + dd0 * h3;
            res = ds1 * h4 + s;
        } else {
            res = p0 + t * d;
        }
        resVec[ii] = res;
    }
}

/* dstmat = srcmat rotated by xrot about x (srcmat x RotX, VU0 ACC chains) */
void DFvu0_MulRotMatrixX(float (*dstmat)[4], float (*srcmat)[4], float xrot)
{
    float rmat[4][4] __attribute__((aligned(16)));
    sceVu0RotMatrixX(rmat, IdentityMtx, xrot);
    vu_mul_matrix(dstmat, (const float (*)[4])srcmat, (const float (*)[4])rmat);
}

void DFvu0_TransposeMatrix(float (*dstmat)[4], float (*srcmat)[4])
{
    float tmpmat[4][4] __attribute__((aligned(16)));
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) tmpmat[i][j] = srcmat[j][i];
    sceVu0CopyMatrix(dstmat, tmpmat);
}

/* Orthonormal basis with row 2 = zvec (copied as is, w included): row 1 = Y with its zvec part removed, normalised
 * (or -/+Z when zvec is within 0.999 of +-Y), row 0 = row1 x row2 (w = row 1's w); row 3 stays identity. */
void Zvec2Matrix(float (*dstmat)[4], float *zvec)
{
    sceVu0CopyMatrix(dstmat, IdentityMtx);
    memcpy(dstmat[2], zvec, 16);
    float inp = dstmat[2][0] * vec_Yone[0] + dstmat[2][1] * vec_Yone[1];
    inp = inp + dstmat[2][2] * vec_Yone[2];
    float ainp = inp < 0.0f ? -inp : inp;
    if (!(ainp < 0.999f)) {
        dstmat[1][0] = 0.0f;
        dstmat[1][1] = 0.0f;
        dstmat[1][2] = inp < 0.0f ? 1.0f : -1.0f;
        dstmat[1][3] = 0.0f;
    } else {
        float inpv[4];
        for (int i = 0; i < 3; i++) inpv[i] = dstmat[2][i] * inp;
        for (int i = 0; i < 3; i++) dstmat[1][i] = vec_Yone[i] - inpv[i];
        dstmat[1][3] = vec_Yone[3];
        sceVu0Normalize(dstmat[1], dstmat[1]);
    }
    vu_cross(dstmat[0], dstmat[1], dstmat[2]);
    dstmat[0][3] = dstmat[1][3];
}

float add_angf(float a, float b) { return calc_wrap_pi(a + b); }

void DF_CALC_Vector2Angle(float *ang, float *vec)
{
    float xz = vec[0] * vec[0];
    xz = xz + vec[2] * vec[2];
    ang[0] = ps2_atan2f(-vec[1], ps2_sqrt(xz));
    ang[1] = ps2_atan2f(vec[0], vec[2]);
}

/* unit vector of (pitch, yaw) from Sintable; vec[1] = sin(pitch) (positive pitch = up here), w = 1 */
void DF_CALC_Angle2Vector(float *vec, float *ang)
{
    int ix = calc_sin_index(ang[0]);
    int iy = calc_sin_index(ang[1]);
    float cos_x = Sintable[ix + 0x400];
    vec[0] = cos_x * Sintable[iy];
    vec[1] = Sintable[ix];
    vec[2] = cos_x * Sintable[iy + 0x400];
    vec[3] = 1.0f;
}

/* as DF_CALC_Angle2Vector with separate angles and vec[1] = -sin(angx) */
void ang2vec2(float *vec, float angy, float angx)
{
    int ix = calc_sin_index(angx);
    int iy = calc_sin_index(angy);
    float cos_x = Sintable[ix + 0x400];
    vec[0] = cos_x * Sintable[iy];
    vec[1] = -Sintable[ix];
    vec[2] = cos_x * Sintable[iy + 0x400];
    vec[3] = 1.0f;
}

/* unit vector of yaw theta and pitch phi (fsmath VU0 sine, calls in the original order) */
void ang2vec(float *vec, float theta, float phi)
{
    PS2_FRAME(0x30);
    float cos_phi = fsmath_CosVU0(phi);
    vec[0] = cos_phi * fsmath_SinVU0(theta);
    vec[1] = -fsmath_SinVU0(phi);
    vec[2] = cos_phi * fsmath_CosVU0(theta);
    vec[3] = 1.0f;
}

float inverse_angf(float a)
{
    if (!(a <= 0.0f)) return a - CALC_PI;
    return CALC_PI + a;
}

/* Step ang towards tgt by at most add the short way round; all three reduced to 0..2PI first, the result wrapped
 * to -PI..PI. add >= PI (or ang == tgt, or within reach) returns tgt. */
float go_angf(float ang, float tgt, float add)
{
    ang = calc_mod_pi2(ang);
    tgt = calc_mod_pi2(tgt);
    if (ang == tgt) return calc_wrap_pi(tgt);
    add = calc_mod_pi2(add);
    if (!(add < CALC_PI)) return calc_wrap_pi(tgt);
    float dif = tgt - ang;
    if (!(dif <= 0.0f)) {
        if (dif <= CALC_PI) {
            if (dif <= add) return calc_wrap_pi(tgt);
            ang = ang + add;
        } else {
            if (CALC_PI2 - dif <= add) return calc_wrap_pi(tgt);
            ang = ang - add;
            if (ang < 0.0f) ang = ang + CALC_PI2;
        }
    } else if (dif < -CALC_PI) {
        if (CALC_PI2 + dif <= add) return calc_wrap_pi(tgt);
        ang = ang + add;
        if (!(ang < CALC_PI2)) ang = ang - CALC_PI2;
    } else {
        if (!(dif < -add)) return calc_wrap_pi(tgt);
        ang = ang - add;
    }
    return calc_wrap_pi(ang);
}

float sub_angf(float a, float b) { return calc_wrap_pi(a - b); }

/* a0 = a1 +- up to dangx / dangy in tenths (rand() % 0x54000 >> 14 = 0..20, minus 10) */
void angrand(float *a0, float *a1, float dangx, float dangy)
{
    int r = ps2_rand();
    a0[0] = a1[0] + (dangx * (float)(((r % 0x54000) >> 14) - 10)) / 10.0f;
    r = ps2_rand();
    a0[1] = a1[1] + (dangy * (float)(((r % 0x54000) >> 14) - 10)) / 10.0f;
}

/* ang = (pitch, yaw, 0, 0) of vec; pitch = atan(-y / xz length), +-PI/2 (or 0) when vec is vertical */
void vec2ang(float *ang, float *vec)
{
    float dist = vec[0] * vec[0];
    dist = ps2_sqrt(dist + vec[2] * vec[2]);
    if (0.0f == dist) {
        if (vec[1] < 0.0f) ang[0] = CALC_HALF_PI;
        else if (!(vec[1] <= 0.0f)) ang[0] = -CALC_HALF_PI;
        else ang[0] = 0.0f;
    } else {
        ang[0] = ps2_atanf(-vec[1] / dist);
    }
    ang[1] = ps2_atan2f(vec[0], vec[2]);
    ang[2] = 0.0f;
    ang[3] = 0.0f;
}

/* Move pos towards tgt by spd; arriving (distance <= spd) sets pos = tgt and returns 1. Otherwise
 * pos.xyz = tgt + (pos - tgt) * (dist - spd) / dist, pos.w = tgt.w. */
int trace_spd(float *pos, float *tgt, float spd)
{
    float vec[4];
    for (int i = 0; i < 3; i++) vec[i] = pos[i] - tgt[i];
    float x2 = vec[0] * vec[0], y2 = vec[1] * vec[1], z2 = vec[2] * vec[2];
    float dist = ps2_sqrt(z2 + (x2 + y2));
    if (!(dist <= spd)) {
        float rate = ps2_div(dist - spd, dist);
        for (int i = 0; i < 3; i++) vec[i] = vec[i] * rate;
        for (int i = 0; i < 3; i++) pos[i] = tgt[i] + vec[i];
        pos[3] = tgt[3];
        return 0;
    }
    memcpy(pos, tgt, 16);
    return 1;
}

/* *datap += addval clamped to min..max; returns 1 if clamped at max, -1 at min (min wins), else 0 */
int AddF_minmax(float *datap, float addval, float min, float max)
{
    float data = *datap + addval;
    int clip = 0;
    if (!(data <= max)) { data = max; clip = 1; }
    if (data < min) { data = min; clip = -1; }
    *datap = data;
    return clip;
}

int AddF_min(float *datap, float add, float min)
{
    float data = *datap + add;
    if (data < min) { *datap = min; return 1; }
    *datap = data;
    return 0;
}

int AddF_max(float *datap, float add, float max)
{
    float data = *datap + add;
    if (!(data <= max)) { *datap = max; return 1; }
    *datap = data;
    return 0;
}

/* norm.xyz moves towards trg by rate, w = 0, then normalised */
void norm_interpolate(float *norm, float *trg, float rate)
{
    for (int i = 0; i < 3; i++) norm[i] = norm[i] + rate * (trg[i] - norm[i]);
    norm[3] = 0.0f;
    sceVu0Normalize(norm, norm);
}

float Add_Angle(float ang, float add) { return calc_wrap_pi(ang + add); }

/* yaw from loc2 to loc */
float DF_CALC_GetAngY(float *loc, float *loc2)
{
    float vec[4];
    for (int i = 0; i < 3; i++) vec[i] = loc[i] - loc2[i];
    return ps2_atan2f(vec[0], vec[2]);
}

float DF_CALC_GetDist_xz(float *loc, float *loc2)
{
    float x = loc[0] - loc2[0], z = loc[2] - loc2[2];
    float x2 = x * x, z2 = z * z;
    return ps2_sqrt(x2 + z2);
}

float DF_CALC_GetDist(float *loc, float *loc2)
{
    float x = loc[0] - loc2[0], y = loc[1] - loc2[1], z = loc[2] - loc2[2];
    float x2 = x * x, y2 = y * y, z2 = z * z;
    return ps2_sqrt(z2 + (x2 + y2));
}

/* ang = (pitch, yaw, 0, 1) of the matrix's -Z axis (row 2 negated) */
void MatrixZtoAng(float *ang, void *mat)
{
    float *zz = (float *)mat + 8;
    float xz = zz[0] * zz[0];
    xz = xz + zz[2] * zz[2];
    ang[0] = ps2_atan2f(-zz[1], ps2_sqrt(xz));
    ang[1] = ps2_atan2f(-zz[0], -zz[2]);
    ang[2] = 0.0f;
    ang[3] = 1.0f;
}

/* limit the xz length of vec to max */
void vector_limit_zx(float *vec, float max)
{
    float x = vec[0];
    float len = x * x;
    len = ps2_sqrt(len + vec[2] * vec[2]);
    if (!(len <= max)) {
        float ff = ps2_div(max, len);
        vec[0] = x * ff;
        vec[2] = vec[2] * ff;
    }
}
