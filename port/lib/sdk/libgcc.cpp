/* lib/sdk/libgcc.cpp - libgcc of the main ELF (no DWARF): GCC's fp-bit.c soft double (0x11d060..0x11e140) and the
 * 64-bit unsigned divide / modulo (0x11c578 / 0x11caf8), ported literally from the binary as integer code, so every
 * result is bit-identical to the PS2 on any host.
 * This fp-bit build has no rounding step at all: + - * / and double -> float truncate toward zero (ROUND_TOWARDS_ZERO),
 * denormal inputs read as zero and tiny results flush to zero (NO_DENORMALS), overflow gives Inf, add / sub drop the
 * smaller operand outright when the exponents differ by 64 or more, NaN results are quiet (0/0, Inf - Inf, 0 * Inf
 * = 0x7ff8000000000000), the float side has no Inf / NaN (unpack_f reads exponent 255 as a number, so
 * __extendsfdf2(Inf) = 2^128; pack_f gives Inf for |x| >= 2^129 and NaN / Inf inputs). The double -> int conversion
 * MWCC code calls (0x11e0a0, sdk_names "__fixdfsi") has unsigned semantics: negative -> 0, >= 2^32 / Inf -> -1.
 * Entry points use raw IEEE bit patterns (the hybrid registers them at their PS2 addresses in runtime/bridge_sdk.c,
 * so the interpreted libc uses them too); natives use the double-typed ps2_d* wrappers of acnb/sdk.h. */
#include <stdint.h>
#include "acnb/sdk.h"

namespace {

enum { CLASS_SNAN, CLASS_QNAN, CLASS_ZERO, CLASS_NUMBER, CLASS_INFINITY };

/* fp-bit's fp_number_type (0x18 bytes on the PS2 stack). A ZERO from unpack keeps normal_exp / fraction stale in
 * the original; nothing reads them for that class (locals here start zeroed). */
struct fp_number_type {
    int cls;          /* +0 */
    int sign;         /* +4 */
    int normal_exp;   /* +8: unbiased */
    uint64_t fraction;   /* +0x10: implicit one at bit 60 (8 guard bits); NaN: the raw 52-bit mantissa */
};
struct fp_number_f {   /* float flavour: fraction is a u32 at +0xc, implicit one at bit 30 (7 guard bits) */
    int cls, sign, normal_exp;
    uint32_t fraction;
};

const uint64_t IMPLICIT_1 = 1ull << 60, IMPLICIT_2 = 1ull << 61;
const fp_number_type nan_part = {CLASS_SNAN, 0, 0, 0};   /* 0x26f758, 24 zero bytes: packs to 0x7ff8000000000000 */

inline bool is_nan(int cls) { return (unsigned)cls < 2; }   /* sltiu 2: SNAN / QNAN */

/* 0x11d5a8 */
void unpack_d(uint64_t bits, fp_number_type *p)
{
    uint64_t mant = bits & 0xfffffffffffffull;
    unsigned exp = (unsigned)(bits >> 52) & 0x7ff;
    p->sign = (int)(bits >> 63);
    if (exp == 0) {   /* zero and denormals alike */
        p->cls = CLASS_ZERO;
        return;
    }
    if (exp == 0x7ff) {
        p->normal_exp = 0x3ff;
        if (mant == 0) {
            p->cls = CLASS_INFINITY;
            p->fraction = IMPLICIT_1;
            return;
        }
        p->cls = mant & 1ull << 51 ? CLASS_QNAN : CLASS_SNAN;
        p->fraction = mant;
        return;
    }
    p->cls = CLASS_NUMBER;
    p->normal_exp = (int)exp - 0x3ff;
    p->fraction = mant << 8 | IMPLICIT_1;
}

/* 0x11d4d0: no rounding - the guard bits are shifted out */
uint64_t pack_d(const fp_number_type *p)
{
    uint64_t fraction = p->fraction;
    unsigned exp = 0;
    if (is_nan(p->cls)) {
        exp = 0x7ff;
        fraction |= 1ull << 51;   /* quiet */
    } else if (p->cls == CLASS_INFINITY) {
        exp = 0x7ff;
        fraction = 0;
    } else if (p->cls == CLASS_ZERO) {
        fraction = 0;
    } else if (fraction == 0) {
        /* +-0 */
    } else if (p->normal_exp < -0x3fe) {
        fraction = 0;   /* below 2^-1022: flushed */
    } else if (p->normal_exp < 0x400) {
        exp = (unsigned)(p->normal_exp + 0x3ff);
        fraction >>= 8;
    } else {
        exp = 0x7ff;    /* overflow -> Inf */
        fraction = 0;
    }
    return (uint64_t)(p->sign & 1) << 63 | (uint64_t)(exp & 0x7ff) << 52 | (fraction & 0xfffffffffffffull);
}

/* 0x11d400: exponent 0 -> ZERO (only cls / sign written); exponent 255 is an ordinary number (2^128 range) */
void unpack_f(uint32_t bits, fp_number_f *p)
{
    unsigned exp = bits >> 23 & 0xff;
    p->sign = (int)(bits >> 31);
    if (exp == 0) {
        p->cls = CLASS_ZERO;
        return;
    }
    p->normal_exp = (int)exp - 0x7f;
    p->cls = CLASS_NUMBER;
    p->fraction = (bits & 0x7fffff) << 7 | 0x40000000u;
}

/* 0x11d348 (wrapper 0x11d460 takes the fields as arguments): only ZERO is looked at as a class - NaN / Inf parts
 * pack by their exponent (0x3ff from unpack_d -> Inf) */
uint32_t pack_f(const fp_number_f *p)
{
    uint32_t fraction = p->fraction;
    unsigned exp = 0;
    if (p->cls == CLASS_ZERO) {
        fraction = 0;
    } else if (fraction == 0) {
        /* +-0 */
    } else if (p->normal_exp < -0x7e) {
        fraction = 0;
    } else if (p->normal_exp > 0x80) {
        exp = 0xff;
        fraction = 0;
    } else if (p->normal_exp == 0x80) {
        /* original quirk (0x11d384..0x11d398): exponent field 255 with the shifted mantissa (NaN-looking pattern,
         * Inf when the low bits are 0); fraction with bit 31 set -> Inf */
        exp = 0xff;
        fraction = (int32_t)fraction >= 0 ? fraction >> 7 : 0;
    } else {
        exp = (unsigned)(p->normal_exp + 0x7f);
        fraction >>= 7;
    }
    return (uint32_t)(p->sign & 1) << 31 | (exp & 0xff) << 23 | (fraction & 0x7fffff);
}

/* 0x11d668 */
const fp_number_type *fpadd_parts(const fp_number_type *a, const fp_number_type *b, fp_number_type *tmp)
{
    if (is_nan(a->cls)) return a;
    if (is_nan(b->cls)) return b;
    if (a->cls == CLASS_INFINITY) {
        if (b->cls == CLASS_INFINITY && a->sign != b->sign) return &nan_part;   /* Inf - Inf */
        return a;
    }
    if (b->cls == CLASS_INFINITY) return b;
    if (b->cls == CLASS_ZERO) {
        if (a->cls != CLASS_ZERO) return a;
        *tmp = *a;
        tmp->sign = a->sign & b->sign;   /* -0 + -0 = -0, else +0 */
        return tmp;
    }
    if (a->cls == CLASS_ZERO) return b;

    int a_normal_exp = a->normal_exp, b_normal_exp = b->normal_exp;
    uint64_t a_fraction = a->fraction, b_fraction = b->fraction;
    int diff = a_normal_exp - b_normal_exp;
    if (diff < 0) diff = -diff;
    if (diff < 64) {
        /* the asm shifts one bit per iteration keeping a sticky bit 0: f = f >> 1 | (f & 1) */
        if (a_normal_exp > b_normal_exp) {
            int d = a_normal_exp - b_normal_exp;
            b_fraction = b_fraction >> d | ((b_fraction & ((1ull << d) - 1)) != 0);
            b_normal_exp = a_normal_exp;
        }
        if (a_normal_exp < b_normal_exp) {
            int d = b_normal_exp - a_normal_exp;
            a_fraction = a_fraction >> d | ((a_fraction & ((1ull << d) - 1)) != 0);
            a_normal_exp = b_normal_exp;
        }
    } else if (a_normal_exp > b_normal_exp) {
        b_fraction = 0;   /* no sticky: the smaller operand is dropped */
    } else {
        a_normal_exp = b_normal_exp;
        a_fraction = 0;
    }

    if (a->sign != b->sign) {
        int64_t tfraction = a->sign ? (int64_t)(b_fraction - a_fraction) : (int64_t)(a_fraction - b_fraction);
        if (tfraction < 0) {
            tmp->sign = 1;
            tmp->normal_exp = a_normal_exp;
            tmp->fraction = (uint64_t)-tfraction;
        } else {
            tmp->sign = 0;
            tmp->normal_exp = a_normal_exp;
            tmp->fraction = (uint64_t)tfraction;
        }
        while (tmp->fraction - 1 < IMPLICIT_1 - 1) {   /* 0 < f < IMPLICIT_1 (f - 1 > 0x0ffffffffffffffe test) */
            tmp->fraction <<= 1;
            tmp->normal_exp--;
        }
    } else {
        tmp->sign = a->sign;
        tmp->normal_exp = a_normal_exp;
        tmp->fraction = a_fraction + b_fraction;
    }
    tmp->cls = CLASS_NUMBER;
    if (tmp->fraction >= IMPLICIT_2) {
        tmp->fraction = tmp->fraction >> 1 | (tmp->fraction & 1);
        tmp->normal_exp++;
    }
    return tmp;
}

/* 0x11dd50 (__fpcmp_parts): NaN -> 1 for every comparison */
int fpcmp_parts(const fp_number_type *a, const fp_number_type *b)
{
    if (is_nan(a->cls) || is_nan(b->cls)) return 1;
    if (a->cls == CLASS_INFINITY && b->cls == CLASS_INFINITY) return b->sign - a->sign;
    if (a->cls == CLASS_INFINITY) return a->sign ? -1 : 1;
    if (b->cls == CLASS_INFINITY) return b->sign ? 1 : -1;
    if (a->cls == CLASS_ZERO && b->cls == CLASS_ZERO) return 0;
    if (a->cls == CLASS_ZERO) return b->sign ? 1 : -1;
    if (b->cls == CLASS_ZERO) return a->sign ? -1 : 1;
    if (a->sign != b->sign) return a->sign ? -1 : 1;
    if (a->normal_exp > b->normal_exp) return a->sign ? -1 : 1;
    if (a->normal_exp < b->normal_exp) return a->sign ? 1 : -1;
    if (a->fraction > b->fraction) return a->sign ? -1 : 1;
    if (a->fraction < b->fraction) return a->sign ? 1 : -1;
    return 0;
}

}   // namespace

/* 0x11d8c8 */
unsigned long long ps2_adddf3(unsigned long long x, unsigned long long y)
{
    fp_number_type a = {}, b = {}, tmp = {};
    unpack_d(x, &a);
    unpack_d(y, &b);
    return pack_d(fpadd_parts(&a, &b, &tmp));
}

/* 0x11d920 */
unsigned long long ps2_subdf3(unsigned long long x, unsigned long long y)
{
    fp_number_type a = {}, b = {}, tmp = {};
    unpack_d(x, &a);
    unpack_d(y, &b);
    b.sign ^= 1;
    return pack_d(fpadd_parts(&a, &b, &tmp));
}

/* 0x11d988: 61 x 61 bit fraction product from four 32 x 32 partial products (__muldi3); only the high 64 bits reach
 * the result (truncated) */
unsigned long long ps2_muldf3(unsigned long long x, unsigned long long y)
{
    fp_number_type a = {}, b = {}, tmp = {};
    unpack_d(x, &a);
    unpack_d(y, &b);
    if (is_nan(a.cls)) {
        a.sign = a.sign != b.sign;
        return pack_d(&a);
    }
    if (is_nan(b.cls)) {
        b.sign = a.sign != b.sign;
        return pack_d(&b);
    }
    if (a.cls == CLASS_INFINITY) {
        if (b.cls == CLASS_ZERO) return pack_d(&nan_part);
        a.sign = a.sign != b.sign;
        return pack_d(&a);
    }
    if (b.cls == CLASS_INFINITY) {
        if (a.cls == CLASS_ZERO) return pack_d(&nan_part);
        b.sign = a.sign != b.sign;
        return pack_d(&b);
    }
    if (a.cls == CLASS_ZERO) {
        a.sign = a.sign != b.sign;
        return pack_d(&a);
    }
    if (b.cls == CLASS_ZERO) {
        b.sign = a.sign != b.sign;
        return pack_d(&b);
    }

    uint64_t lo_a = a.fraction & 0xffffffffu, hi_a = a.fraction >> 32;
    uint64_t lo_b = b.fraction & 0xffffffffu, hi_b = b.fraction >> 32;
    uint64_t ll = lo_b * lo_a, hl = hi_b * lo_a, lh = lo_b * hi_a, hh = hi_b * hi_a;
    uint64_t cross = hl + lh;
    uint64_t carry_cross = cross < hl;
    uint64_t low = ll + (cross << 32);
    uint64_t carry_low = low < ll;
    uint64_t high = (carry_cross << 32) + carry_low + (cross >> 32) + hh;

    int exp = a.normal_exp + b.normal_exp + 4;
    tmp.sign = a.sign != b.sign;
    while (high >= IMPLICIT_2) {
        exp++;
        if (high & 1) low = low >> 1 | 1ull << 63;   /* fp-bit: low only moves when a bit comes in */
        high >>= 1;
    }
    while (high < IMPLICIT_1) {
        exp--;
        high <<= 1;
        if (low & 1ull << 63) high |= 1;
        low <<= 1;
    }
    tmp.normal_exp = exp;
    tmp.fraction = high;
    tmp.cls = CLASS_NUMBER;
    return pack_d(&tmp);
}

/* 0x11dc10 (not reached in the Versus replays): restoring division, 61 quotient bits, no remainder / sticky */
unsigned long long ps2_divdf3(unsigned long long x, unsigned long long y)
{
    fp_number_type a = {}, b = {};
    unpack_d(x, &a);
    unpack_d(y, &b);
    if (is_nan(a.cls)) return pack_d(&a);   /* NaN keeps its own sign */
    if (is_nan(b.cls)) return pack_d(&b);
    a.sign ^= b.sign;
    if (a.cls == CLASS_INFINITY || a.cls == CLASS_ZERO) {
        if (a.cls == b.cls) return pack_d(&nan_part);   /* Inf / Inf, 0 / 0 */
        return pack_d(&a);
    }
    if (b.cls == CLASS_INFINITY) {   /* x / Inf: a NUMBER with fraction 0 -> +-0 */
        a.fraction = 0;
        a.normal_exp = 0;
        return pack_d(&a);
    }
    if (b.cls == CLASS_ZERO) {
        a.cls = CLASS_INFINITY;
        return pack_d(&a);
    }
    a.normal_exp -= b.normal_exp;
    uint64_t numerator = a.fraction, denominator = b.fraction;
    if (numerator < denominator) {
        numerator <<= 1;
        a.normal_exp--;
    }
    uint64_t bit = IMPLICIT_1, quotient = 0;
    while (bit) {
        if (!(numerator < denominator)) {
            quotient |= bit;
            numerator -= denominator;
        }
        bit >>= 1;
        numerator <<= 1;
    }
    a.fraction = quotient;
    return pack_d(&a);
}

/* 0x11de70: <0 / 0 / >0; NaN -> 1 */
int ps2_cmpdf2(unsigned long long x, unsigned long long y)
{
    fp_number_type a = {}, b = {};
    unpack_d(x, &a);
    unpack_d(y, &b);
    return fpcmp_parts(&a, &b);
}

/* 0x11dec0: a0 is the whole 64-bit register (an int arrives sign-extended); exact */
unsigned long long ps2_floatsidf(long long a0)
{
    fp_number_type in = {};
    in.cls = CLASS_NUMBER;
    in.sign = (int)((uint32_t)a0 >> 31);
    if (a0 == 0) {
        in.cls = CLASS_ZERO;
        return pack_d(&in);
    }
    in.normal_exp = 0x3c;
    if (in.sign) {
        if (a0 == (long long)(int32_t)0x80000000u) return 0xc1e0000000000000ull;   /* -2^31 */
        in.fraction = (uint64_t)(int64_t)(int32_t)(0u - (uint32_t)a0);   /* negu: 32-bit, sign-extended */
    } else {
        in.fraction = (uint64_t)a0;
    }
    while (in.fraction < IMPLICIT_1) {
        in.fraction <<= 1;
        in.normal_exp--;
    }
    return pack_d(&in);
}

/* 0x11d060 (__floatdidf): (double)hi * 65536.0 * 65536.0 + (double)(unsigned)lo, through the soft ops */
unsigned long long ps2_floatdidf(long long a)
{
    unsigned long long hi = ps2_floatsidf(a >> 32);
    hi = ps2_muldf3(hi, 0x40f0000000000000ull);   /* 65536.0 */
    hi = ps2_muldf3(hi, 0x40f0000000000000ull);
    long long lw = (int32_t)(uint32_t)a;
    unsigned long long lo = ps2_floatsidf(lw);
    if (lw < 0) lo = ps2_adddf3(lo, 0x41f0000000000000ull);   /* + 2^32 */
    return ps2_adddf3(hi, lo);
}

/* 0x11df78: signed double -> int (only newlib's _dtoa_r calls it; not reached in the Versus replays) */
int ps2_fixdfsi(unsigned long long x)
{
    fp_number_type a = {};
    unpack_d(x, &a);
    if (a.cls == CLASS_ZERO || is_nan(a.cls)) return 0;
    if (a.cls == CLASS_INFINITY) return a.sign ? (int)0x80000000u : 0x7fffffff;
    if (a.normal_exp < 0) return 0;
    if (a.normal_exp >= 31) return a.sign ? (int)0x80000000u : 0x7fffffff;
    uint32_t v = (uint32_t)(a.fraction >> (60 - a.normal_exp));
    return (int)(a.sign ? 0u - v : v);
}

/* 0x11e0a0 (sdk_names "__fixdfsi": the double -> int MWCC code calls): negative / NaN / zero -> 0, Inf or
 * >= 2^32 -> 0xffffffff, else truncated (the v0 register gets the 32-bit value sign-extended) */
unsigned int ps2_fixunsdfsi(unsigned long long x)
{
    fp_number_type a = {};
    unpack_d(x, &a);
    if (a.cls == CLASS_ZERO || is_nan(a.cls) || a.sign) return 0;
    if (a.cls == CLASS_INFINITY) return 0xffffffffu;
    if (a.normal_exp < 0) return 0;
    if (a.normal_exp >= 32) return 0xffffffffu;
    /* the e >= 61 dsllv arm (0x11e10c) is dead: e < 32 here */
    return (uint32_t)(a.fraction >> (60 - a.normal_exp));
}

/* 0x11d108 (__fixunsdfdi; not reached in the Versus replays): negative -> 0; high word through * 2^-32, then the
 * remainder corrects the low word */
unsigned long long ps2_fixunsdfdi(unsigned long long a)
{
    if (ps2_cmpdf2(a, 0) < 0) return 0;
    unsigned long long r = (unsigned long long)ps2_fixunsdfsi(ps2_muldf3(a, 0x3df0000000000000ull)) << 32;   /* 2^-32 */
    unsigned long long t;
    if ((long long)r < 0) {
        t = ps2_floatdidf((long long)(r >> 1 | (r & 1)));
        t = ps2_adddf3(t, t);
    } else {
        t = ps2_floatdidf((long long)r);
    }
    unsigned long long rem = ps2_subdf3(a, t);
    if (ps2_cmpdf2(rem, 0) < 0) r -= ps2_fixunsdfsi(ps2_subdf3(0, rem));
    else r += ps2_fixunsdfsi(rem);
    return r;
}

/* 0x11d220 (sdk_names "__fixdfdi", but the argument is a float in $f12): (unsigned long long)(double)f, the
 * 0x11d108 body after __extendsfdf2; negative -> 0 (DF_SetDBuffClrCol) */
unsigned long long ps2_fixunssfdi(unsigned int fbits)
{
    return ps2_fixunsdfdi(ps2_extendsfdf2(fbits));
}

/* 0x11d490: exact widening, except denormal floats -> +-0 and exponent 255 -> 2^128 range numbers */
unsigned long long ps2_extendsfdf2(unsigned int fbits)
{
    fp_number_f in = {};
    unpack_f(fbits, &in);
    fp_number_type out = {in.cls, in.sign, in.normal_exp, (uint64_t)in.fraction << 30};
    return pack_d(&out);   /* via 0x11e018 */
}

/* 0x11e048 (not reached in the Versus replays): truncated, bit 0 sticky */
unsigned int ps2_truncdfsf2(unsigned long long x)
{
    fp_number_type in = {};
    unpack_d(x, &in);
    uint32_t tmp = (uint32_t)(in.fraction >> 30);
    if (in.fraction & 0x3fffffff) tmp |= 1;
    fp_number_f out = {in.cls, in.sign, in.normal_exp, tmp};
    return pack_f(&out);   /* via 0x11d460 */
}

/* 0x11c578 / 0x11caf8 (__udivdi3 / __umoddi3, libgcc2 udivmoddi4 on 32-bit words: exact). A zero divisor traps in
 * the original (break 7); unreachable (newlib vfprintf divides by 8 / 10 / 16). */
unsigned long long ps2_udivdi3(unsigned long long n, unsigned long long d) { return d ? n / d : 0; }
unsigned long long ps2_umoddi3(unsigned long long n, unsigned long long d) { return d ? n % d : n; }
