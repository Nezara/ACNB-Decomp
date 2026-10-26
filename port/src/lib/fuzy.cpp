/* src/lib/fuzy.cpp - fuzzy-logic membership helpers for the COM AI (whole file). Each returns a degree, normally in
 * 0..1 (no clamping): "v is about m (+-d)", "v is small / large relative to m" with a ramp 2d wide around m, a fuzzy
 * OR (a + b - ab) and a half-scale around 0.5. Negative widths are negated (neg.s, so -0 stays -0); the compares are
 * written so NaN inputs take the same branch as the original's c.olt / c.ole tests. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"

/* adda.s a, b; msub.s: ACC = a + b (rounded), result = ACC - a * b (product rounded first) */
float fz_add(float a, float b)
{
    float s = a + b;
    float p = a * b;
    return s - p;
}

float fz_half(float a)
{
    float t = a - 0.5f;
    t = 0.5f * t;
    return 0.5f + t;
}

float fz_equal(float v, float m, float d)
{
    if (d < 0.0f) d = -d;
    float diff = (v < m) ? m - v : v - m;
    if (!(diff <= d)) return 0.0f;
    return 1.0f - ps2_div(diff, d);
}

float fz_small(float v, float m, float d)
{
    if (d < 0.0f) d = -d;
    if (!(v <= m + d)) return 0.0f;
    float lo = m - d;
    if (v < lo) return 1.0f;
    return 1.0f - ps2_div(v - lo, 2.0f * d);
}

float fz_large(float v, float m, float d)
{
    if (d < 0.0f) d = -d;
    if (!(v <= m + d)) return 1.0f;
    float lo = m - d;
    if (v < lo) return 0.0f;
    return ps2_div(v - lo, 2.0f * d);
}
