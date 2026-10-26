/* lib/sdk/newlib.cpp - newlib functions and data of the main ELF that decompiled code uses: srand / rand (both
 * builds) and the data they and the decompiled code read. The hybrid build uses the data at their PS2 addresses
 * (runtime/nodwarf_abs.s), so those definitions are source-port only.
 *
 * _ctype_ table from newlib libc/ctype/ctype_.c (identical to the bytes at 0x26ee48):
 * Copyright (c) 1989 The Regents of the University of California. All rights reserved.
 * Redistribution and use in source and binary forms are permitted provided that the above copyright notice and
 * this paragraph are duplicated in all such forms. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "acnb/sdk.h"

/* rand's state: _impure_ptr->_new._reent._rand_next */
static inline unsigned long long *ps2_rand_next() { return (unsigned long long *)((char *)ps2_impure_ptr + 0xa8); }

extern "C" void ps2_srand(unsigned int seed) { *ps2_rand_next() = seed; }

/* newlib rand: 64-bit LCG (Knuth's MMIX multiplier), bits 32..62 of the new state */
extern "C" int ps2_rand(void)
{
    unsigned long long n = *ps2_rand_next() * 0x5851f42d4c957f2dull + 1;
    *ps2_rand_next() = n;
    return (int)(n >> 32) & 0x7fffffff;
}

#ifdef ACNB_SOURCEPORT

/* the default reentrancy struct: only _rand_next (+0xa8, 1 after _REENT_INIT) is used so far */
static unsigned long long s_impure_data[0x400 / 8] = {[0xa8 / 8] = 1};
extern "C" void *ps2_impure_ptr = s_impure_data;

/* newlib sprintf: lib/sdk/libc.cpp's port (same output and return value as the original, also for formats that
 * do not match the arguments: the unused slots are 0). The arguments go into EABI vararg slots. */
static int nl_sprintf(char *buf, const char *fmt, unsigned long long a0, unsigned long long a1 = 0,
                      unsigned long long a2 = 0, unsigned long long a3 = 0)
{
    unsigned long long ap[8] = {a0, a1, a2, a3};
    return ps2_native_sprintf(buf, fmt, (char *)ap);
}
static inline unsigned long long nl_i(int v) { return (unsigned long long)(long long)v; }
static inline unsigned long long nl_p(const char *p) { return (unsigned long long)(uintptr_t)p; }
extern "C" int ps2_sprintf_d(char *buf, const char *fmt, int v) { return nl_sprintf(buf, fmt, nl_i(v)); }
extern "C" int ps2_sprintf_s(char *buf, const char *fmt, const char *str) { return nl_sprintf(buf, fmt, nl_p(str)); }
extern "C" int ps2_sprintf_ss(char *buf, const char *fmt, const char *s1, const char *s2)
{
    return nl_sprintf(buf, fmt, nl_p(s1), nl_p(s2));
}
extern "C" int ps2_sprintf_dss(char *buf, const char *fmt, int v, const char *s1, const char *s2)
{
    return nl_sprintf(buf, fmt, nl_i(v), nl_p(s1), nl_p(s2));
}
extern "C" int ps2_sprintf_sd(char *buf, const char *fmt, const char *s, int v)
{
    return nl_sprintf(buf, fmt, nl_p(s), nl_i(v));
}
extern "C" int ps2_sprintf_sdd(char *buf, const char *fmt, const char *s, int v1, int v2)
{
    return nl_sprintf(buf, fmt, nl_p(s), nl_i(v1), nl_i(v2));
}
extern "C" int ps2_sprintf_ddd(char *buf, const char *fmt, int v1, int v2, int v3)
{
    return nl_sprintf(buf, fmt, nl_i(v1), nl_i(v2), nl_i(v3));
}
extern "C" int ps2_sprintf_sss(char *buf, const char *fmt, const char *s1, const char *s2, const char *s3)
{
    return nl_sprintf(buf, fmt, nl_p(s1), nl_p(s2), nl_p(s3));
}
extern "C" int ps2_sprintf_ssss(char *buf, const char *fmt, const char *s1, const char *s2, const char *s3, const char *s4)
{
    return nl_sprintf(buf, fmt, nl_p(s1), nl_p(s2), nl_p(s3), nl_p(s4));
}
extern "C" int ps2_sprintf_scsssssd(char *buf, const char *fmt, const char *s1, int c, const char *s2, const char *s3,
                                    const char *s4, const char *s5, const char *s6, int v)
{
    unsigned long long ap[8] = {nl_p(s1), nl_i(c), nl_p(s2), nl_p(s3), nl_p(s4), nl_p(s5), nl_p(s6), nl_i(v)};
    return ps2_native_sprintf(buf, fmt, (char *)ap);
}
extern "C" int ps2_printf_d(const char *fmt, int v) { return printf(fmt, v); }
extern "C" int ps2_printf_dd(const char *fmt, int v1, int v2) { return printf(fmt, v1, v2); }
extern "C" int ps2_printf_dddd(const char *fmt, int v1, int v2, int v3, int v4) { return printf(fmt, v1, v2, v3, v4); }
extern "C" int ps2_printf_s(const char *fmt, const char *s) { return printf(fmt, s); }
extern "C" int ps2_printf(const char *fmt) { return printf(fmt, 0); }   /* no conversions; the 0 keeps -Wformat-security quiet */
extern "C" void *ps2_malloc(unsigned int size) { return malloc(size); }
extern "C" void ps2_free(void *p) { free(p); }
/* newlib's BSD qsort (lib/sdk/libc.cpp): the host qsort orders mpart's inconsistent comparator differently */
extern "C" void ps2_qsort(void *base, unsigned int n, unsigned int size, int (*cmp)(void *, void *))
{
    ps2_native_qsort(base, n, size, cmp);
}

enum { U = 0x01, L = 0x02, N = 0x04, S = 0x08, P = 0x10, C = 0x20, X = 0x40, B = 0x80 };

extern "C" const unsigned char ps2_ctype_[1 + 256] = {
    0,
    C,     C,     C,     C,     C,     C,     C,     C,
    C,     C | S, C | S, C | S, C | S, C | S, C,     C,
    C,     C,     C,     C,     C,     C,     C,     C,
    C,     C,     C,     C,     C,     C,     C,     C,
    S | B, P,     P,     P,     P,     P,     P,     P,
    P,     P,     P,     P,     P,     P,     P,     P,
    N,     N,     N,     N,     N,     N,     N,     N,
    N,     N,     P,     P,     P,     P,     P,     P,
    P,     U | X, U | X, U | X, U | X, U | X, U | X, U,
    U,     U,     U,     U,     U,     U,     U,     U,
    U,     U,     U,     U,     U,     U,     U,     U,
    U,     U,     U,     P,     P,     P,     P,     P,
    P,     L | X, L | X, L | X, L | X, L | X, L | X, L,
    L,     L,     L,     L,     L,     L,     L,     L,
    L,     L,     L,     L,     L,     L,     L,     L,
    L,     L,     L,     P,     P,     P,     P,     C,
};

#endif
