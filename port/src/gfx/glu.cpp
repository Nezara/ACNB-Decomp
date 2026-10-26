/* src/gfx/glu.cpp - gluLookAtF (whole file): a row-vector look-at (view) matrix from eye, target and up.
 * f = normalised (at - eye), s = normalised up x f, u = normalised f x s; columns 0 / 1 / 2 = s / u / f (+f, not
 * OpenGL's -f), row 3 = -(eye . s, eye . u, eye . f). Normalising multiplies by 1 / sqrt (FLT_MAX for a zero vector). */
#include "acnb/port.h"
#include "acnb/ps2fp.h"

void gluLookAtF(float (*mf)[4], float xEye, float yEye, float zEye, float xAt, float yAt, float zAt, float xUp,
                float yUp, float zUp)
{
    float dx = xAt - xEye, dy = yAt - yEye, dz = zAt - zEye;
    float inv = ps2_div(1.0f, ps2_sqrt((dx * dx + dy * dy) + dz * dz));
    float fz = dz * inv, fy = dy * inv, fx = dx * inv;

    float sx = yUp * fz - zUp * fy;
    float sy = zUp * fx - xUp * fz;
    float sz = xUp * fy - yUp * fx;
    inv = ps2_div(1.0f, ps2_sqrt((sx * sx + sy * sy) + sz * sz));
    sx = sx * inv;
    sz = sz * inv;
    sy = sy * inv;

    float ux = fy * sz - fz * sy;
    float uy = fz * sx - fx * sz;
    float uz = fx * sy - fy * sx;
    inv = ps2_div(1.0f, ps2_sqrt((ux * ux + uy * uy) + uz * uz));
    ux = ux * inv;
    uy = uy * inv;
    uz = uz * inv;

    mf[0][0] = sx; mf[1][0] = sy; mf[2][0] = sz; mf[3][0] = -((xEye * sx + yEye * sy) + zEye * sz);
    mf[0][1] = ux; mf[1][1] = uy; mf[2][1] = uz; mf[3][1] = -((xEye * ux + yEye * uy) + zEye * uz);
    mf[0][2] = fx; mf[1][2] = fy; mf[2][2] = fz; mf[3][2] = -((xEye * fx + yEye * fy) + zEye * fz);
    mf[0][3] = 0.0f; mf[1][3] = 0.0f; mf[2][3] = 0.0f; mf[3][3] = 1.0f;
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper (gen_bridge stops at 8 float args); zUp is the caller's stack arg at sp+0 */
static void bw__gluLookAtF(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    u32 zup = rd32(sp);
    float z;
    memcpy(&z, &zup, 4);
    gluLookAtF((float (*)[4])bridge_ptr(c->gpr[A0].uw[0]), c->fpr[12].f, c->fpr[13].f, c->fpr[14].f, c->fpr[15].f,
               c->fpr[16].f, c->fpr[17].f, c->fpr[18].f, c->fpr[19].f, z);
}
#endif
