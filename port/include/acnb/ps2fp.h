/* PS2 float semantics for decompiled code, matching the interpreter (runtime/ee.c, vu0.c): ordinary IEEE single
 * arithmetic in the same operation order is bit-identical, except where PS2 hardware has no Inf/NaN:
 *   - division by zero gives +-FLT_MAX (sign = sign(a) ^ sign(b)) instead of Inf/NaN;
 *   - sqrt takes |x|.
 * Use these wherever a divisor can be 0 or a sqrt argument negative. Keep the original expression order
 * (MWCC evaluates left to right and never reassociates) so gameplay code stays deterministic vs the interpreter.
 * clang does not fold `0.0f + x` / `0.0f - x` (signed zeros), so VU "add to zero" idioms can be written as is. */
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

static inline uint32_t ps2_f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static inline float ps2_u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

#ifdef ACNB_FPU_PCSX2
/* FPU rounding test build only: the helpers'
 * divide / sqrt round to nearest under an RZ MXCSR, like PCSX2 (FPU / VU round mode chop, FPUDiv round mode
 * nearest). One asm block per op keeps the MXCSR switch and the op in order. Plain `/` in game code stays RZ. */
static inline float ps2_fdiv_(float a, float b)
{
    unsigned int c = __builtin_ia32_stmxcsr(), n = c & ~0x6000u;
    __asm__ volatile("ldmxcsr %1; divss %2, %0; ldmxcsr %3" : "+x"(a) : "m"(n), "x"(b), "m"(c));
    return a;
}
static inline float ps2_fsqrt_(float a)
{
    unsigned int c = __builtin_ia32_stmxcsr(), n = c & ~0x6000u;
    float r;
    __asm__ volatile("ldmxcsr %1; sqrtss %2, %0; ldmxcsr %3" : "=x"(r) : "m"(n), "x"(a), "m"(c));
    return r;
}
#else
#define ps2_fdiv_(a, b) ((a) / (b))
#define ps2_fsqrt_(a) sqrtf(a)
#endif

/* div.s / VDIV */
static inline float ps2_div(float a, float b)
{
    if (b == 0.0f) return ps2_u2f(((ps2_f2u(a) ^ ps2_f2u(b)) & 0x80000000u) | 0x7f7fffffu);
    return ps2_fdiv_(a, b);
}

/* sqrt.s / VSQRT */
static inline float ps2_sqrt(float a) { return ps2_fsqrt_(fabsf(a)); }

/* rsqrt.s / VRSQRT: a / sqrt(|b|) */
static inline float ps2_rsqrt(float a, float b)
{
    float r = ps2_fsqrt_(fabsf(b));
    if (r == 0.0f) return ps2_u2f((ps2_f2u(a) & 0x80000000u) | 0x7f7fffffu);
    return ps2_fdiv_(a, r);
}

/* cvt.w.s (truncation; PS2 saturates) */
static inline int ps2_ftoi(float f)
{
    return f >= 2147483647.0f ? 0x7fffffff : f <= -2147483648.0f ? (int)0x80000000 : (int)f;
}

/* MIPS div / divu results as the interpreter computes them (no trap on zero). */
static inline int ps2_div32(int a, int b)
{
    if (!b) return a < 0 ? 1 : -1;
    if (a == (int)0x80000000 && b == -1) return a;
    return a / b;
}
static inline int ps2_mod32(int a, int b)
{
    if (!b) return a;
    if (a == (int)0x80000000 && b == -1) return 0;
    return a % b;
}
static inline unsigned int ps2_modu32(unsigned int a, unsigned int b) { return b ? a % b : a; }
