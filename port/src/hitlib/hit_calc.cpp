/* src/hitlib/hit_calc.cpp - small 3D vector helpers of the hit library (whole file). Vectors are float[4]; only xyz
 * are computed (CopyVector / SetVector also move w). Sums keep MWCC's ACC order (mula/madd). */
#include "acnb/port.h"
#include "acnb/ps2fp.h"

/* ans = (x, 0, z) / |(x, z)|, returns |(x, z)|; ans untouched when the length is below FLT_EPSILON */
float NormalizeXZ(float *ans, float *fv)
{
    float x = fv[0];
    float len = ps2_sqrt(x * x + fv[2] * fv[2]);
    if (len < 1.1920929e-07f) return 0.0f;
    float inv = 1.0f / len;
    ans[0] = x * inv;
    ans[1] = 0.0f;
    ans[2] = fv[2] * inv;
    return len;
}

void OuterProduct(float *ans, float *fv1, float *fv2)
{
    ans[0] = fv1[1] * fv2[2] - fv1[2] * fv2[1];
    ans[1] = fv1[2] * fv2[0] - fv1[0] * fv2[2];
    ans[2] = fv1[0] * fv2[1] - fv1[1] * fv2[0];
}

void ScaleVector(float *ans, float *fv, float scl)
{
    ans[0] = fv[0] * scl;
    ans[1] = fv[1] * scl;
    ans[2] = fv[2] * scl;
}

void SubVector(float *ans, float *fv1, float *fv2)
{
    ans[0] = fv1[0] - fv2[0];
    ans[1] = fv1[1] - fv2[1];
    ans[2] = fv1[2] - fv2[2];
}

void AddVector(float *ans, float *fv1, float *fv2)
{
    ans[0] = fv1[0] + fv2[0];
    ans[1] = fv1[1] + fv2[1];
    ans[2] = fv1[2] + fv2[2];
}

void CopyVector(float *ans, float *fv)
{
    ans[0] = fv[0];
    ans[1] = fv[1];
    ans[2] = fv[2];
    ans[3] = fv[3];
}

float Dist2VectorXZ(float *fv) { return fv[0] * fv[0] + fv[2] * fv[2]; }

float Dist2Vector(float *fv) { return (fv[0] * fv[0] + fv[1] * fv[1]) + fv[2] * fv[2]; }

float InnerProduct(float *fv1, float *fv2) { return (fv1[0] * fv2[0] + fv1[1] * fv2[1]) + fv1[2] * fv2[2]; }

void SetVector(float *ans, float x, float y, float z, float w)
{
    ans[0] = x;
    ans[1] = y;
    ans[2] = z;
    ans[3] = w;
}
