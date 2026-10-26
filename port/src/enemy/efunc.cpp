/* src/enemy/efunc.cpp - angle helpers of the enemy code (whole file, OLSORTIE overlay): signed angle differences,
 * rotation matrix -> X/Z/Y Euler angles, and approaching a target angle by a fraction of the difference.
 * Every result is wrapped into [-PI, PI] by repeated 2PI steps (`while (!(a <= PI))` / `while (a < -PI)`). */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"

static const float EFUNC_PI = 3.14159274f;    /* 0x40490fdb */
static const float EFUNC_PI2 = 6.28318548f;   /* 0x40c90fdb */

static inline void efunc_wrap(float *a)
{
    if (!(*a <= EFUNC_PI)) {
        do *a = *a - EFUNC_PI2; while (!(*a <= EFUNC_PI));
    }
    if (*a < -EFUNC_PI) {
        do *a = *a + EFUNC_PI2; while (*a < -EFUNC_PI);
    }
}

float ene_get_angle_to_angle(float target_angle, float my_angle)
{
    float d = target_angle - my_angle;
    if (!(d <= EFUNC_PI)) return d - EFUNC_PI2;
    if (d < -EFUNC_PI) return d + EFUNC_PI2;
    return d;
}

/* dstang[i] moves towards the angles of mat by 1/rotspd of the (wrapped) difference; rotspd <= 1 snaps */
void Matrix2AngleXZY_Approach(float *dstang, float (*mat)[4], float rotspd)
{
    PS2_FRAME(0x60);
    float ang[4] __attribute__((aligned(16)));
    DFvu0_Matrix2XZYang(ang, mat);
    for (int i = 0; i < 3; i++) {
        if (rotspd <= 1.0f) {
            dstang[i] = ang[i];
        } else {
            dstang[i] = dstang[i] + sub_angf(ang[i], dstang[i]) / rotspd;
            efunc_wrap(&dstang[i]);
        }
    }
}

/* rot = {x, y, z}: y from the image of the x axis, z its elevation; x from the image of the z axis after undoing
 * y and z. The VU apply uses the full xyzw of the copied matrix (w of the axis vectors = 0). */
void DFvu0_Matrix2XZYang(float *rot, float (*mat)[4])
{
    PS2_FRAME(0xe0);
    float m[4][4] __attribute__((aligned(16)));
    float axis[4] __attribute__((aligned(16)));
    float v[4] __attribute__((aligned(16)));

    sceVu0CopyMatrix(m, mat);
    axis[0] = 1.0f; axis[1] = 0.0f; axis[2] = 0.0f; axis[3] = 0.0f;
    vu_apply(v, (const float (*)[4])m, axis);
    rot[1] = -ps2_atan2f(v[2], v[0]);
    rot[2] = ps2_atan2f(v[1], ps2_sqrt(v[0] * v[0] + v[2] * v[2]));   /* mula.s / madd.s */
    sceVu0RotMatrixY(m, m, -rot[1]);
    sceVu0RotMatrixZ(m, m, -rot[2]);
    axis[0] = 0.0f; axis[1] = 0.0f; axis[2] = 1.0f; axis[3] = 0.0f;
    vu_apply(v, (const float (*)[4])m, axis);
    rot[0] = ps2_atan2f(-v[1], v[2]);
    efunc_wrap(&rot[0]);
    efunc_wrap(&rot[1]);
    efunc_wrap(&rot[2]);
}

/* returns the difference used (0 when div <= 1 snaps) */
float Angle_Approach(float *srcangp, float trgang, float div)
{
    PS2_FRAME(0x30);
    if (div <= 1.0f) {
        *srcangp = trgang;
        return 0.0f;
    }
    float d = sub_angf(trgang, *srcangp);
    *srcangp = *srcangp + d / div;
    efunc_wrap(srcangp);
    return d;
}
