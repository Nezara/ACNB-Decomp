/* src/hitlib/hit_check.cpp - yes/no line tests of the hit library (whole file). A Y-cylinder hangs from its top
 * centre YCyP down by YCyH (radius YCyR); a line is LP + t LV, t in 0..1. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"

int IsHitLineYCylinder(float *LP, float *LV, float *YCyP, float YCyH, float YCyR)
{
    PS2_FRAME(0x90);
    PS2_STACK(float[4], d, 0x70);
    PS2_STACK(float, t, 0x8c);
    PS2_STACK(float[4], bot, 0x50);
    PS2_STACK(float[4], v, 0x60);
    SubVector(d, LP, YCyP);
    float a = Dist2VectorXZ(LV);
    float rr = YCyR * YCyR;
    float b = d[0] * LV[0] + d[2] * LV[2];
    float disc = b * b - a * (Dist2VectorXZ(d) - rr);
    if (disc < 0.0f) return 0;
    float sq = ps2_sqrt(disc);
    float nb = -b;
    t = ps2_div(nb - sq, a);
    float t2 = ps2_div(nb + sq, a);
    if (!(t <= 1.0f) || t2 < 0.0f) return 0;
    if (t < 0.0f) t = 0.0f;
    float y = d[1] + LV[1] * t;     /* height of the entry into the infinite cylinder */
    if (y < -YCyH) {
        /* enters below the bottom: must come up through the bottom disc */
        if (LV[1] <= 0.0f) return 0;
        SetVector(bot, YCyP[0], YCyP[1] - YCyH, YCyP[2], 1.0f);
        GetHitRateLineYPlane(LP, LV, bot[1], &t);
        if (t < 0.0f || !(t <= 1.0f)) return 0;
        ScaleVector(v, LV, t);
        AddVector(v, LP, v);
        SubVector(v, bot, v);
    } else {
        if (y <= 0.0f) return 1;
        /* enters above the top: must come down through the top disc */
        if (!(LV[1] < 0.0f)) return 0;
        GetHitRateLineYPlane(LP, LV, YCyP[1], &t);
        if (t < 0.0f || !(t <= 1.0f)) return 0;
        ScaleVector(v, LV, t);
        AddVector(v, LP, v);
        SubVector(v, YCyP, v);
    }
    return Dist2VectorXZ(v) <= rr;
}

int IsHitLineSphere(float *LP, float *LV, float *SP, float SR)
{
    PS2_FRAME(0x60);
    PS2_STACK(float[4], v, 0x50);
    SetLineVector(v, LP, LV, GetNearRatePointLine(SP, LP, LV));
    SubVector(v, SP, v);
    return Dist2Vector(v) <= SR * SR;
}
