/* lib/sdk/libc.cpp - newlib string / memory functions, BSD qsort and the sprintf chain (see "stdio" below) of the
 * main ELF (no DWARF), ported from the binary. Registered at their PS2 addresses in runtime/bridge_sdk.c (interpreted callers); natives keep using the
 * host <string.h> (same effect, see below), except qsort (ps2_qsort -> ps2_native_qsort in both builds).
 *
 * The EE string leaves (memcpy 0x116938, strcpy 0x1192e0, strncpy 0x1196e8, strcmp / strncmp / strlen) have 8 / 16-byte
 * ld / lq fast paths that are taken only when both pointers are 8 / 16-aligned, so the distance between source and
 * destination is a multiple of the chunk size, and each chunk is read before it is written: the effect is the plain
 * forward byte loop's for every input, overlapping ranges included (memcpy with dst inside (src, src + n) repeats the
 * first dst - src bytes; host memcpy is undefined there). The wide zero-byte tests only choose between the paths
 * (strncpy's first 16-byte test is inverted - `pnor` before `psubb` flags any byte <= 0x7e - so it nearly always takes
 * the byte path; no effect on the result). strcmp / strncmp return the difference of the first differing bytes as
 * unsigned chars (host strcmp may return any value of the right sign; the natives in src/ only test == 0).
 *
 * qsort: newlib's BSD qsort.c (Bentley & McIlroy, "Engineering a Sort Function"), literally, because the order of
 * equal / inconsistently compared elements (mpart's __MPART_TableQsort returns 0 / -1 only) depends on the algorithm.
 *
 * Copyright (c) 1992, 1993 The Regents of the University of California. All rights reserved.
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
 * conditions of the BSD license (qsort.c 8.1, 6/4/93) are met. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "acnb/port.h"
#include "acnb/sdk.h"

/* ---------------------------------------------------------------- string / memory leaves */

/* 0x116938: forward copy, returns dst */
void *ps2_memcpy(void *dst, const void *src, unsigned int n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d <= s || d >= s + n) {
        memmove(d, s, n);   /* a forward copy reads every source byte before it is overwritten here */
        return dst;
    }
    /* dst inside (src, src + n): each k-byte step copies bytes written k bytes earlier (pattern repeat) */
    unsigned int k = (unsigned int)(d - s);
    for (unsigned int i = 0; i < n; i += k) memcpy(d + i, s + i, n - i < k ? n - i : k);
    return dst;
}

/* 0x116af0: stores c & 0xff (sq / sd pattern stores for an aligned dst, same bytes) */
void *ps2_memset(void *dst, int c, unsigned int n)
{
    memset(dst, c & 0xff, n);
    return dst;
}

/* 0x1193f8 */
unsigned int ps2_strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return (unsigned int)(p - s);
}

/* 0x119198: *s1 - *s2 (unsigned chars) at the first difference or NUL */
int ps2_strcmp(const char *s1, const char *s2)
{
    while (*s1 != 0 && *s1 == *s2) s1++, s2++;
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

/* 0x119530: as strcmp, at most n bytes; n == 0 -> 0 */
int ps2_strncmp(const char *s1, const char *s2, unsigned int n)
{
    for (; n != 0; n--, s1++, s2++) {
        if (*s1 != *s2) return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
        if (*s1 == 0) return 0;
    }
    return 0;
}

/* 0x1192e0: copies through the NUL, returns dst (byte at a time: the chunked paths have the same effect) */
char *ps2_strcpy(char *dst, const char *src)
{
    char *d = dst, c;
    do {
        c = *src++;
        *d++ = c;
    } while (c != 0);
    return dst;
}

/* 0x1196e8: copies up to n bytes incl. the NUL, zero-pads the rest of n, returns dst */
char *ps2_strncpy(char *dst, const char *src, unsigned int n)
{
    char *d = dst;
    while (n != 0) {
        char c = *src++;
        n--;
        *d++ = c;
        if (c == 0) break;
    }
    while (n != 0) {
        *d++ = 0;
        n--;
    }
    return dst;
}

/* 0x119b10: `lbu (_ctype_ + 1 + c)`, no range check: a negative c (a signed char >= 0x80) reads the 128 bytes before
 * _ctype_ (0x26edc9..0x26ee48: "libdma: sync timeout" and libdma data). The hybrid reads them from PS2 memory; the
 * source port uses the c values whose byte has _L (2) set in the ELF image. Those bytes do not change at run time:
 * the only code refs into 0x26edc8..0x26ee48 are lbu reads of libdma's nibble lookup tables (0x26ee18 / 28 / 38,
 * 0x112f54..) plus a printf string and a lw (scripts/xrefs.py, s88). Callers: MFONT_DrawChar passes lbu (0..255);
 * only strFileOpen passes a signed lb char, negative only for a path byte >= 0x80 (disc paths are ASCII).
 * c > 255 reads past the table (unreached; treated as "not lower case"). */
int ps2_toupper(int c)
{
    int lower;
#ifdef ACNB_SOURCEPORT
    if (c < 0) {
        static const signed char neg_l[] = {-111, -107, -105, -103, -102, -96, -93, -46, -41, -31, -27, -16, -15};
        lower = 0;
        for (unsigned int i = 0; i < sizeof neg_l; i++)
            if (neg_l[i] == c) lower = 1;
    } else
#endif
    lower = (unsigned int)(c + 128) < 384u ? ((ps2_ctype_ + 1)[c] & 2) : 0;
    return lower ? c - 0x20 : c;
}

/* 0x1134a8: abs(INT_MIN) = INT_MIN (negu) */
int ps2_abs(int x) { return x < 0 ? (int)(0u - (unsigned int)x) : x; }

/* ---------------------------------------------------------------- qsort (0x117d68) */

typedef int qsort_cmp_t(void *, void *);

/* swapcode: do-while over es / sizeof(TYPE) units (swaptype <= 1: 8-byte longs, 2: bytes) */
static void qsort_swapfunc(char *a, char *b, unsigned int n, int swaptype)
{
    if (swaptype <= 1) {
        long long i = n / 8;   /* 64-bit count, daddiu / bgtz */
        do {
            unsigned long long t, u;
            memcpy(&t, a, 8);
            memcpy(&u, b, 8);
            memcpy(a, &u, 8);
            memcpy(b, &t, 8);
            a += 8, b += 8;
        } while (--i > 0);
    } else {
        long long i = n;
        do {
            char t = *a;
            *a++ = *b;
            *b++ = t;
        } while (--i > 0);
    }
}

static inline void qsort_swap(char *a, char *b, unsigned int es, int swaptype)
{
    if (swaptype == 0) {
        unsigned long long t, u;
        memcpy(&t, a, 8);
        memcpy(&u, b, 8);
        memcpy(a, &u, 8);
        memcpy(b, &t, 8);
    } else
        qsort_swapfunc(a, b, es, swaptype);
}

static inline char *qsort_med3(char *a, char *b, char *c, qsort_cmp_t *cmp)
{
    return gcall(cmp, (void *)a, (void *)b) < 0
               ? (gcall(cmp, (void *)b, (void *)c) < 0 ? b : (gcall(cmp, (void *)a, (void *)c) < 0 ? c : a))
               : (gcall(cmp, (void *)b, (void *)c) > 0 ? b : (gcall(cmp, (void *)a, (void *)c) < 0 ? a : c));
}

void ps2_native_qsort(void *base, unsigned int n, unsigned int es, int (*cmp)(void *, void *))
{
    PS2_FRAME(0xc0);
    char *a = (char *)base, *pa, *pb, *pc, *pd, *pl, *pm, *pn;
    int swaptype, swap_cnt, cmp_result;
    unsigned int d, r;

loop:
    swaptype = (((uintptr_t)a | es) & 7) ? 2 : es == 8 ? 0 : 1;
    swap_cnt = 0;
    if (n < 7) {
        for (pm = a + es; pm < a + n * es; pm += es)
            for (pl = pm; pl > a && gcall(cmp, (void *)(pl - es), (void *)pl) > 0; pl -= es)
                qsort_swap(pl, pl - es, es, swaptype);
        return;
    }
    pm = a + (n / 2) * es;
    if (n > 7) {
        pl = a;
        pn = a + (n - 1) * es;
        if (n > 40) {
            d = (n / 8) * es;
            pl = qsort_med3(pl, pl + d, pl + 2 * d, cmp);
            pm = qsort_med3(pm - d, pm, pm + d, cmp);
            pn = qsort_med3(pn - 2 * d, pn - d, pn, cmp);
        }
        pm = qsort_med3(pl, pm, pn, cmp);
    }
    qsort_swap(a, pm, es, swaptype);
    pa = pb = a + es;
    pc = pd = a + (n - 1) * es;
    for (;;) {
        while (pb <= pc && (cmp_result = gcall(cmp, (void *)pb, (void *)a)) <= 0) {
            if (cmp_result == 0) {
                swap_cnt = 1;
                qsort_swap(pa, pb, es, swaptype);
                pa += es;
            }
            pb += es;
        }
        while (pb <= pc && (cmp_result = gcall(cmp, (void *)pc, (void *)a)) >= 0) {
            if (cmp_result == 0) {
                swap_cnt = 1;
                qsort_swap(pc, pd, es, swaptype);
                pd -= es;
            }
            pc -= es;
        }
        if (pb > pc) break;
        qsort_swap(pb, pc, es, swaptype);
        swap_cnt = 1;
        pb += es;
        pc -= es;
    }
    if (swap_cnt == 0) {   /* switch to insertion sort */
        for (pm = a + es; pm < a + n * es; pm += es)
            for (pl = pm; pl > a && gcall(cmp, (void *)(pl - es), (void *)pl) > 0; pl -= es)
                qsort_swap(pl, pl - es, es, swaptype);
        return;
    }

    pn = a + n * es;
    {
        int r1 = (int)(pa - a), r2 = (int)(pb - pa);   /* signed min (slt) */
        int rs = r1 < r2 ? r1 : r2;
        if (rs > 0) qsort_swapfunc(a, pb - rs, (unsigned int)rs, swaptype);
    }
    {
        unsigned int r1 = (unsigned int)(pd - pc), r2 = (unsigned int)(pn - pd) - es;   /* unsigned min (sltu) */
        r = r1 < r2 ? r1 : r2;
        if ((int)r > 0) qsort_swapfunc(pb, pn - r, r, swaptype);
    }
    if ((r = (unsigned int)(pb - pa)) > es) ps2_native_qsort(a, r / es, es, cmp);
    if ((r = (unsigned int)(pd - pc)) > es) {
        /* iterate rather than recurse to save stack space */
        a = pn - r;
        n = r / es;
        goto loop;
    }
}

/* ---------------------------------------------------------------- stdio: sprintf chain (newlib 1.x) */
/* newlib's sprintf, literally from the binary (vfprintf.c / fvwrite.c / findfp.c / mbtowc_r.c / locale.c of the EE
 * toolchain): sprintf 0x118cd8 -> vfprintf 0x119c30 (CHECK_INIT: __sinit 0x114eb0 + std 0x114ca0, once per boot)
 * -> _vfprintf_r 0x119ca8 (localeconv 0x115960 / _localeconv_r 0x115928, _mbtowc_r 0x116788) -> __sprint 0x119b30
 * -> __sfvwrite 0x115408 -> memmove 0x1169e8.
 * ap = the EABI vararg slots: 8 bytes each (int / pointer in the low word, long and double as 64-bit values);
 * sprintf's prologue spills a2..t3 right below the caller's sp, so they continue into the caller's stack arguments.
 * Pointers are PS2 addresses in the hybrid (identity-mapped RAM), host pointers in the source port. The locals that
 * reach interpreted code (cvt / exponent outputs, the iov data a real FILE's write function gets) sit at their
 * original stack slots (PS2_STACK).
 * Not ported yet (gcall into the original in the hybrid; the source port has only string FILEs and no caller passing
 * a double, so they are unreachable there - nl_unported aborts): the floating-point conversion cvt 0x11b318
 * (-> _dtoa_r 0x113860, the Balloc family, _malloc_r) and exponent 0x11b4d8; the real-FILE paths __swsetup
 * 0x11b6c8, __sbprintf 0x119b78, fflush 0x114b88 and the FILE's _write function.
 * Original behaviour kept: %lld / %qd set QUADINT, which no conversion tests (the value is read as an int, lw);
 * %ld reads a 64-bit long (ld); %p sign-extends the pointer (lw: >= 0x80000000 prints 16 hex digits); %e of 0.0
 * pads with zeroes (newlib's `_double || flags & ALT == 0` precedence slip = `_double != 0`); the scan loop's
 * _mbtowc_r gets _impure_ptr and __mb_cur_max, not the FILE's reent. */

struct ps2_sbuf { unsigned char *_base; int _size; };
struct ps2_FILE {                              /* newlib struct __sFILE */
    unsigned char *_p;                         /* 0x00 */
    int _r, _w;                                /* 0x04, 0x08 */
    short _flags, _file;                       /* 0x0c, 0x0e */
    ps2_sbuf _bf;                              /* 0x10 */
    int _lbfsize;                              /* 0x18 */
    void *_cookie;                             /* 0x1c */
    unsigned int _read, _write, _seek, _close; /* 0x20..0x2c: guest code addresses */
    ps2_sbuf _ub;                              /* 0x30 */
    unsigned char *_up;                        /* 0x38 */
    int _ur;                                   /* 0x3c */
    unsigned char _ubuf[3], _nbuf[1];          /* 0x40, 0x43 */
    ps2_sbuf _lb;                              /* 0x44 */
    int _blksize, _offset;                     /* 0x4c, 0x50 */
    void *_data;                               /* 0x54: struct _reent * */
};
static_assert(sizeof(ps2_FILE) == 0x58, "newlib FILE layout");
struct ps2_siov { const void *iov_base; unsigned int iov_len; };
struct ps2_suio { ps2_siov *uio_iov; int uio_iovcnt; int uio_resid; };

enum { NL_SLBF = 0x1, NL_SNBF = 0x2, NL_SRD = 0x4, NL_SWR = 0x8, NL_SRW = 0x10, NL_SERR = 0x40, NL_SSTR = 0x200 };
/* struct _reent offsets: __sdidinit, __cleanup, __sglue {_next, _niobs, _iobs}, __sf[3] */
enum { RE_SDIDINIT = 0x38, RE_CLEANUP = 0x3c, RE_GLUE_NEXT = 0x1d8, RE_GLUE_NIOBS = 0x1dc, RE_GLUE_IOBS = 0x1e0,
       RE_SF = 0x1e4 };
static inline unsigned int &re_word(void *r, int off) { return *(unsigned int *)((char *)r + off); }

/* newlib data and vfprintf's string constants (hybrid: PS2 addresses in runtime/nodwarf_abs.s) */
extern "C" {
extern int ps2_mb_cur_max;               /* __mb_cur_max 0x2577f0 (1) */
extern char *ps2_lconv[];                /* struct lconv 0x26efb0: decimal_point first */
extern const char ps2_vfp_blanks[16];    /* 0x26f150 */
extern const char ps2_vfp_zeroes[16];    /* 0x26f160 */
extern const char ps2_vfp_inf[];         /* 0x26f170 "Inf" */
extern const char ps2_vfp_nan[];         /* 0x26f178 "NaN" */
extern const char ps2_vfp_xdigs_l[];     /* 0x26f180 "0123456789abcdef" */
extern const char ps2_vfp_null[];        /* 0x26f198 "(null)" */
extern const char ps2_vfp_xdigs_u[];     /* 0x26f1a0 "0123456789ABCDEF" */
extern const char ps2_vfp_badbase[];     /* 0x26f1b8 "bug in vfprintf: bad base" */
extern const char ps2_vfp_zero[];        /* 0x26f1d8 "0" */
extern const char ps2_vfp_dot[];         /* 0x26f1e0 "." */
}
#ifdef ACNB_SOURCEPORT
extern "C" {
int ps2_mb_cur_max = 1;
char *ps2_lconv[10] = {(char *)".", (char *)"", (char *)"", (char *)"", (char *)"",
                       (char *)"", (char *)"", (char *)"", (char *)"", (char *)""};   /* + 8 CHAR_MAX chars, unread */
const char ps2_vfp_blanks[16] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
const char ps2_vfp_zeroes[16] = {'0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0'};
const char ps2_vfp_inf[] = "Inf";
const char ps2_vfp_nan[] = "NaN";
const char ps2_vfp_xdigs_l[] = "0123456789abcdef";
const char ps2_vfp_null[] = "(null)";
const char ps2_vfp_xdigs_u[] = "0123456789ABCDEF";
const char ps2_vfp_badbase[] = "bug in vfprintf: bad base";
const char ps2_vfp_zero[] = "0";
const char ps2_vfp_dot[] = ".";
}
/* std() stores the stdio functions' guest addresses; the source port has no guest code (and calls none of them) */
#define NL_GUEST_FN(a) 0u
[[noreturn]] static void nl_unported(const char *what)
{
    fprintf(stderr, "lib/sdk/libc: newlib %s is not ported yet (source port)\n", what);
    abort();
}
static int nl_swsetup(ps2_FILE *) { nl_unported("__swsetup"); }
static int nl_fflush(ps2_FILE *) { nl_unported("fflush"); }
static int nl_write(ps2_FILE *, const char *, int) { nl_unported("FILE write"); }
static int nl_sbprintf(ps2_FILE *, const char *, char *) { nl_unported("__sbprintf"); }
static char *nl_cvt(void *, unsigned long long, int, int, char *, int *, int, int *) { nl_unported("cvt / _dtoa_r"); }
static int nl_exponent(char *, int, int) { nl_unported("exponent"); }
#else
#define NL_GUEST_FN(a) (a)
static int nl_swsetup(ps2_FILE *fp) { return gcall_as<int(ps2_FILE *)>(0x11b6c8u, fp); }
static int nl_fflush(ps2_FILE *fp) { return gcall_as<int(ps2_FILE *)>(0x114b88u, fp); }
static int nl_write(ps2_FILE *fp, const char *p, int n)
{
    return gcall_as<int(void *, const char *, int)>(fp->_write, fp->_cookie, p, n);
}
static int nl_sbprintf(ps2_FILE *fp, const char *fmt, char *ap)
{
    return gcall_as<int(ps2_FILE *, const char *, char *)>(0x119b78u, fp, fmt, ap);
}
/* cvt(data, value, ndigits, flags, &sign, &decpt, ch, &length): value is one 64-bit register (a1) */
static char *nl_cvt(void *data, unsigned long long value, int ndigits, int flags, char *sign, int *decpt, int ch,
                    int *length)
{
    return gcall_as<char *(void *, unsigned long long, int, int, char *, int *, int, int *)>(
        0x11b318u, data, value, ndigits, flags, sign, decpt, ch, length);
}
static int nl_exponent(char *p0, int exp, int fmtch) { return gcall_as<int(char *, int, int)>(0x11b4d8u, p0, exp, fmtch); }
#endif

/* 0x114ca0 std(): set up one of the three standard FILEs */
void ps2_native_std(ps2_FILE *fp, int flags, int file, void *ptr)
{
    fp->_p = 0;
    fp->_r = 0;
    fp->_w = 0;
    fp->_flags = (short)flags;
    fp->_file = (short)file;
    fp->_bf._base = 0;
    fp->_bf._size = 0;
    fp->_lbfsize = 0;
    fp->_cookie = fp;
    fp->_read = NL_GUEST_FN(0x118d68u);    /* __sread */
    fp->_write = NL_GUEST_FN(0x118dd0u);   /* __swrite */
    fp->_seek = NL_GUEST_FN(0x118e50u);    /* __sseek */
    fp->_close = NL_GUEST_FN(0x118eb8u);   /* __sclose */
    fp->_data = ptr;
}

/* 0x114eb0 __sinit(s) */
void ps2_native___sinit(void *s)
{
    re_word(s, RE_CLEANUP) = NL_GUEST_FN(0x114e70u);   /* _cleanup_r */
    re_word(s, RE_SDIDINIT) = 1;
    ps2_FILE *sf = (ps2_FILE *)((char *)s + RE_SF);
    ps2_native_std(sf, NL_SRD, 0, s);
    ps2_native_std(sf + 1, NL_SWR | NL_SLBF, 1, s);
    ps2_native_std(sf + 2, NL_SWR | NL_SNBF, 2, s);
    re_word(s, RE_GLUE_NEXT) = 0;
    *(ps2_FILE **)((char *)s + RE_GLUE_IOBS) = sf;
    re_word(s, RE_GLUE_NIOBS) = 3;
}

/* 0x115928 _localeconv_r / 0x115960 localeconv: the "C" locale's struct lconv */
void *ps2_native__localeconv_r(void *data)
{
    (void)data;
    return ps2_lconv;
}
void *ps2_native_localeconv(void) { return ps2_native__localeconv_r(ps2_impure_ptr); }

/* 0x116788 _mbtowc_r (single-byte "C" locale) */
int ps2_native__mbtowc_r(void *r, int *pwc, const char *s, unsigned int n, void *state)
{
    int dummy;
    (void)r;
    (void)state;
    if (pwc == 0) pwc = &dummy;
    if (s == 0) return 0;
    if (n == 0) return -1;
    *pwc = (unsigned char)*s;
    return *s != 0;
}

/* 0x1169e8 memmove: backward byte copy when dst lies inside (src, src + n), else a forward copy (16 / 8-byte chunks
 * when both pointers are 16-aligned, each chunk read before it is written): the host memmove's effect */
void *ps2_memmove(void *dst, const void *src, unsigned int n)
{
    memmove(dst, src, n);
    return dst;
}

/* 0x115408 __sfvwrite: write the uio's iov list to fp. String FILEs (__SSTR) copy what fits into _w and pretend the
 * rest was written too. */
int ps2_native___sfvwrite(ps2_FILE *fp, ps2_suio *uio)
{
    unsigned int len;
    const char *p = 0;
    ps2_siov *iov;
    int w, s;
    int nlknown, nldist = 0;

    if ((len = (unsigned int)uio->uio_resid) == 0) return 0;
    PS2_FRAME(0x50);
    if (((fp->_flags & NL_SWR) == 0 || fp->_bf._base == 0) && nl_swsetup(fp)) return -1;   /* cantwrite */
    iov = uio->uio_iov;
    len = 0;
#define NL_GETIOV(extra) while (len == 0) { extra; p = (const char *)iov->iov_base; len = iov->iov_len; iov++; }
#define NL_COPY(n) memmove(fp->_p, p, (unsigned int)(n))
    if (fp->_flags & NL_SNBF) {
        /* unbuffered: write up to BUFSIZ bytes at a time */
        do {
            NL_GETIOV(;)
            w = nl_write(fp, p, len < 0x401 ? (int)len : 0x400);
            if (w <= 0) goto err;
            p += w;
            len -= w;
        } while ((uio->uio_resid -= w) != 0);
    } else if ((fp->_flags & NL_SLBF) == 0) {
        /* fully buffered (and strings) */
        do {
            NL_GETIOV(;)
            w = fp->_w;
            if (fp->_flags & NL_SSTR) {
                if (len < (unsigned int)w) w = (int)len;
                NL_COPY(w);
                fp->_w -= w;
                fp->_p += w;
                w = (int)len;   /* but pretend all was copied */
            } else if (fp->_p > fp->_bf._base && len > (unsigned int)w) {
                NL_COPY(w);     /* fill and flush */
                fp->_p += w;
                if (nl_fflush(fp)) goto err;
            } else if (len >= (unsigned int)(w = fp->_bf._size)) {
                w = nl_write(fp, p, w);   /* write directly */
                if (w <= 0) goto err;
            } else {
                w = (int)len;   /* fill and done */
                NL_COPY(w);
                fp->_w -= w;
                fp->_p += w;
            }
            p += w;
            len -= w;
        } while ((uio->uio_resid -= w) != 0);
    } else {
        /* line buffered: like fully buffered, flushed after each newline */
        nlknown = 0;
        do {
            NL_GETIOV(nlknown = 0)
            if (!nlknown) {
                const char *nl = (const char *)memchr(p, '\n', len);
                nldist = nl ? (int)(nl + 1 - p) : (int)len + 1;
                nlknown = 1;
            }
            s = len < (unsigned int)nldist ? (int)len : nldist;
            w = fp->_w + fp->_bf._size;
            if (fp->_p > fp->_bf._base && s > w) {
                NL_COPY(w);
                fp->_p += w;
                if (nl_fflush(fp)) goto err;
            } else if (s >= (w = fp->_bf._size)) {
                w = nl_write(fp, p, w);
                if (w <= 0) goto err;
            } else {
                w = s;
                NL_COPY(w);
                fp->_w -= w;
                fp->_p += w;
            }
            if ((nldist -= w) == 0) {
                if (nl_fflush(fp)) goto err;   /* copied the newline: flush and forget */
                nlknown = 0;
            }
            p += w;
            len -= w;
        } while ((uio->uio_resid -= w) != 0);
    }
#undef NL_GETIOV
#undef NL_COPY
    return 0;
err:
    fp->_flags |= NL_SERR;
    return -1;
}

/* 0x119b30 __sprint: flush the iov list */
int ps2_native___sprint(ps2_FILE *fp, ps2_suio *uio)
{
    if (uio->uio_resid == 0) {
        uio->uio_iovcnt = 0;
        return 0;
    }
    PS2_FRAME(0x10);
    int err = ps2_native___sfvwrite(fp, uio);
    uio->uio_iovcnt = 0;
    uio->uio_resid = 0;
    return err;
}

/* newlib isinf / isnan (0x118880 / 0x1188c8) on the double's bits */
static int nl_isinf(unsigned long long d)
{
    unsigned int hx = (unsigned int)(d >> 32) & 0x7fffffffu, lx = (unsigned int)d;
    hx |= (lx | (0u - lx)) >> 31;
    hx = 0x7ff00000u - hx;
    return 1 - (int)((hx | (0u - hx)) >> 31);
}
static int nl_isnan(unsigned long long d)
{
    unsigned int hx = (unsigned int)(d >> 32) & 0x7fffffffu, lx = (unsigned int)d;
    hx |= (lx | (0u - lx)) >> 31;
    hx = 0x7ff00000u - hx;
    return (int)(hx >> 31);
}

/* va_arg on the EABI slots */
static inline int nl_arg_i32(char *&ap) { int v; memcpy(&v, ap, 4); ap += 8; return v; }
static inline long long nl_arg_i64(char *&ap) { long long v; memcpy(&v, ap, 8); ap += 8; return v; }
static inline void *nl_arg_ptr(char *&ap)
{
    unsigned int a = (unsigned int)nl_arg_i32(ap);
    return a ? (void *)HOSTPTR((uintptr_t)a) : (void *)0;
}

enum { NL_ALT = 0x1, NL_HEXPREFIX = 0x2, NL_LADJUST = 0x4, NL_LONGDBL = 0x8, NL_LONGINT = 0x10, NL_QUADINT = 0x20,
       NL_SHORTINT = 0x40, NL_ZEROPAD = 0x80, NL_FPT = 0x100 };
enum { NL_OCT, NL_DEC, NL_HEX };
enum { NL_BUF = 348 };   /* MAXEXP 308 + MAXFRACT 39 + 1 */

/* 0x119ca8 _vfprintf_r(data, fp, fmt0, ap) */
int ps2_native__vfprintf_r(void *data, ps2_FILE *fp, const char *fmt0, char *ap)
{
    PS2_FRAME(0x290);
    PS2_STACK(char[7], expstr, 0);        /* exponent()'s output */
    PS2_STACK(ps2_suio, uio, 0x10);
    PS2_STACK(ps2_siov[8], iov, 0x20);
    PS2_STACK(char[NL_BUF], buf, 0x60);   /* %c, %[diouxX] */
    PS2_STACK(char[2], ox, 0x1c0);        /* 0x / X. prefix */
    PS2_STACK(char, softsign, 0x1d0);     /* cvt()'s outputs */
    PS2_STACK(char, sign, 0x1d1);
    PS2_STACK(int, expt, 0x1dc);
    PS2_STACK(int, ndig, 0x1e0);
    const char *fmt;
    int ch, n, m;
    const char *cp;
    char *q;
    ps2_siov *iovp;
    int flags, ret, width, prec;
    int wc, state = 0;
    const char *decimal_point = ((char **)ps2_native_localeconv())[0];
    unsigned long long dbl = 0;   /* %[eEfgG] argument (bits) */
    int expsize = 0;
    unsigned long long uquad = 0;
    int base = NL_DEC;
    int dprec, realsz, size = 0;
    const char *xdigs = 0;

#define NL_PRINT(ptr, len) do { \
        iovp->iov_base = (ptr); \
        iovp->iov_len = (unsigned int)(len); \
        uio.uio_resid += (len); \
        iovp++; \
        if (++uio.uio_iovcnt >= 8) { \
            if (ps2_native___sprint(fp, &uio)) goto error; \
            iovp = iov; \
        } \
    } while (0)
#define NL_PAD(howmany, with) do { \
        if ((n = (howmany)) > 0) { \
            while (n > 16) { NL_PRINT(with, 16); n -= 16; } \
            NL_PRINT(with, n); \
        } \
    } while (0)
#define NL_FLUSH() do { \
        if (uio.uio_resid && ps2_native___sprint(fp, &uio)) goto error; \
        uio.uio_iovcnt = 0; \
        iovp = iov; \
    } while (0)
    /* SARG / UARG: QUADINT is never tested (long = 64 bits on the EE) */
#define NL_UARG() (flags & NL_LONGINT ? (unsigned long long)nl_arg_i64(ap) \
                   : flags & NL_SHORTINT ? (unsigned long long)(unsigned short)nl_arg_i32(ap) \
                   : (unsigned long long)(unsigned int)nl_arg_i32(ap))
#define NL_DIGIT(c) ((unsigned int)((c) - '0') < 10u)

    if (((fp->_flags & NL_SWR) == 0 || fp->_bf._base == 0) && nl_swsetup(fp)) return -1;   /* cantwrite */
    /* unbuffered files go through a temporary buffer */
    if ((fp->_flags & (NL_SNBF | NL_SWR | NL_SRW)) == (NL_SNBF | NL_SWR) && fp->_file >= 0)
        return nl_sbprintf(fp, fmt0, ap);

    fmt = fmt0;
    uio.uio_iov = iovp = iov;
    uio.uio_resid = 0;
    uio.uio_iovcnt = 0;
    ret = 0;

    for (;;) {
        cp = fmt;
        while ((n = ps2_native__mbtowc_r(ps2_impure_ptr, &wc, fmt, (unsigned int)ps2_mb_cur_max, &state)) > 0) {
            fmt += n;
            if (wc == '%') {
                fmt--;
                break;
            }
        }
        if ((m = (int)(fmt - cp)) != 0) {
            NL_PRINT(cp, m);
            ret += m;
        }
        if (n <= 0) goto done;
        fmt++;   /* skip over '%' */

        flags = 0;
        dprec = 0;
        width = 0;
        prec = -1;
        sign = 0;

    rflag:
        ch = *fmt++;
    reswitch:
        switch (ch) {
        case ' ':
            if (!sign) sign = ' ';   /* "if the space and + flags both appear, the space flag is ignored" */
            goto rflag;
        case '#':
            flags |= NL_ALT;
            goto rflag;
        case '*':
            if ((width = nl_arg_i32(ap)) >= 0) goto rflag;
            width = -width;
            /* fall through */
        case '-':
            flags |= NL_LADJUST;
            goto rflag;
        case '+':
            sign = '+';
            goto rflag;
        case '.':
            if ((ch = *fmt++) == '*') {
                n = nl_arg_i32(ap);
                prec = n < 0 ? -1 : n;
                goto rflag;
            }
            n = 0;
            while (NL_DIGIT(ch)) {
                n = (int)(10u * (unsigned int)n + (unsigned int)(ch - '0'));
                ch = *fmt++;
            }
            prec = n < 0 ? -1 : n;
            goto reswitch;
        case '0':
            flags |= NL_ZEROPAD;   /* a flag, not the start of a width */
            goto rflag;
        case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
            n = 0;
            do {
                n = (int)(10u * (unsigned int)n + (unsigned int)(ch - '0'));
                ch = *fmt++;
            } while (NL_DIGIT(ch));
            width = n;
            goto reswitch;
        case 'L':
            flags |= NL_LONGDBL;
            goto rflag;
        case 'h':
            flags |= NL_SHORTINT;
            goto rflag;
        case 'l':
            if (*fmt == 'l') {
                fmt++;
                flags |= NL_QUADINT;
            } else {
                flags |= NL_LONGINT;
            }
            goto rflag;
        case 'q':
            flags |= NL_QUADINT;
            goto rflag;
        case 'c':
            cp = buf;
            buf[0] = (char)nl_arg_i32(ap);
            size = 1;
            sign = 0;
            break;
        case 'D':
            flags |= NL_LONGINT;
            /* fall through */
        case 'd':
        case 'i':
            uquad = flags & NL_LONGINT ? (unsigned long long)nl_arg_i64(ap)
                    : flags & NL_SHORTINT ? (unsigned long long)(long long)(short)nl_arg_i32(ap)
                    : (unsigned long long)(long long)nl_arg_i32(ap);
            if ((long long)uquad < 0) {
                uquad = 0 - uquad;
                sign = '-';
            }
            base = NL_DEC;
            goto number;
        case 'e':
        case 'E':
        case 'f':
        case 'g':
        case 'G':
            if (prec == -1) prec = 6;
            else if ((ch == 'g' || ch == 'G') && prec == 0) prec = 1;
            dbl = (unsigned long long)nl_arg_i64(ap);   /* LONGDBL reads the same 64-bit slot */
            /* do this before tricky precision changes */
            if (nl_isinf(dbl)) {
                if (ps2_cmpdf2(dbl, 0) < 0) sign = '-';
                cp = ps2_vfp_inf;
                size = 3;
                break;
            }
            if (nl_isnan(dbl)) {
                cp = ps2_vfp_nan;
                size = 3;
                break;
            }
            flags |= NL_FPT;
            cp = nl_cvt(data, dbl, prec, flags, &softsign, &expt, ch, &ndig);
            if (ch == 'g' || ch == 'G') {
                if (expt <= -4 || expt > prec) ch = ch == 'g' ? 'e' : 'E';
                else ch = 'g';
            }
            if (ch <= 'e') {   /* 'e' or 'E' fmt */
                --expt;
                expsize = nl_exponent(expstr, expt, ch);
                size = expsize + ndig;
                if (ndig > 1 || flags & NL_ALT) ++size;
            } else if (ch == 'f') {
                if (expt > 0) {
                    size = expt;
                    if (prec || flags & NL_ALT) size += prec + 1;
                } else {   /* "0.X" */
                    size = prec + 2;
                }
            } else if (expt >= ndig) {   /* fixed g fmt */
                size = expt;
                if (flags & NL_ALT) ++size;
            } else {
                size = ndig + (expt > 0 ? 1 : 2 - expt);
            }
            if (softsign) sign = '-';
            break;
        case 'n':
            if (flags & NL_LONGINT) *(long long *)nl_arg_ptr(ap) = ret;
            else if (flags & NL_SHORTINT) *(short *)nl_arg_ptr(ap) = (short)ret;
            else *(int *)nl_arg_ptr(ap) = ret;
            continue;   /* no output */
        case 'O':
            flags |= NL_LONGINT;
            /* fall through */
        case 'o':
            uquad = NL_UARG();
            base = NL_OCT;
            goto nosign;
        case 'p':
            uquad = (unsigned long long)(long long)nl_arg_i32(ap);   /* lw: sign-extended */
            base = NL_HEX;
            xdigs = ps2_vfp_xdigs_l;
            flags |= NL_HEXPREFIX;
            ch = 'x';
            goto nosign;
        case 's':
            if ((cp = (const char *)nl_arg_ptr(ap)) == 0) cp = ps2_vfp_null;
            if (prec >= 0) {
                /* only the first prec characters may be read */
                const char *p = (const char *)memchr(cp, 0, (unsigned int)prec);
                if (p != 0) {
                    size = (int)(p - cp);
                    if (size > prec) size = prec;
                } else {
                    size = prec;
                }
            } else {
                size = (int)strlen(cp);
            }
            sign = 0;
            break;
        case 'U':
            flags |= NL_LONGINT;
            /* fall through */
        case 'u':
            uquad = NL_UARG();
            base = NL_DEC;
            goto nosign;
        case 'X':
            xdigs = ps2_vfp_xdigs_u;
            goto hex;
        case 'x':
            xdigs = ps2_vfp_xdigs_l;
        hex:
            uquad = NL_UARG();
            base = NL_HEX;
            if (flags & NL_ALT && uquad != 0) flags |= NL_HEXPREFIX;   /* leading 0x / X only if non-zero */
        nosign:
            sign = 0;
        number:
            if ((dprec = prec) >= 0) flags &= ~NL_ZEROPAD;   /* a precision overrides the 0 flag */
            cp = buf + NL_BUF;
            /* a zero value with an explicit precision of zero prints no characters */
            if (uquad != 0 || prec != 0) {
                q = buf + NL_BUF;
                switch (base) {
                case NL_OCT:
                    do {
                        *--q = (char)((uquad & 7) + '0');
                        uquad >>= 3;
                    } while (uquad);
                    if (flags & NL_ALT && *q != '0') *--q = '0';   /* octal leading 0 */
                    break;
                case NL_DEC:
                    while (uquad >= 10) {   /* __umoddi3 / __udivdi3 */
                        *--q = (char)(ps2_umoddi3(uquad, 10) + '0');
                        uquad = ps2_udivdi3(uquad, 10);
                    }
                    *--q = (char)(uquad + '0');
                    break;
                case NL_HEX:
                    do {
                        *--q = xdigs[uquad & 15];
                        uquad >>= 4;
                    } while (uquad);
                    break;
                default:
                    cp = ps2_vfp_badbase;
                    size = (int)strlen(cp);
                    goto skipsize;
                }
                cp = q;
            }
            size = (int)(buf + NL_BUF - cp);
        skipsize:
            break;
        default:   /* "%?" prints ?, unless ? is NUL */
            if (ch == '\0') goto done;
            cp = buf;
            buf[0] = (char)ch;
            size = 1;
            sign = 0;
            break;
        }

        /* cp: the string to pad out to width. size excludes the decimal precision, realsz includes it. */
        realsz = dprec > size ? dprec : size;
        if (sign) realsz++;
        else if (flags & NL_HEXPREFIX) realsz += 2;

        if ((flags & (NL_LADJUST | NL_ZEROPAD)) == 0) NL_PAD(width - realsz, ps2_vfp_blanks);   /* right-adjusting blanks */
        if (sign) {
            NL_PRINT(&sign, 1);
        } else if (flags & NL_HEXPREFIX) {
            ox[0] = '0';
            ox[1] = (char)ch;
            NL_PRINT(ox, 2);
        }
        if ((flags & (NL_LADJUST | NL_ZEROPAD)) == NL_ZEROPAD) NL_PAD(width - realsz, ps2_vfp_zeroes);   /* zero padding */
        NL_PAD(dprec - size, ps2_vfp_zeroes);   /* leading zeroes from the decimal precision */

        if ((flags & NL_FPT) == 0) {
            NL_PRINT(cp, size);
        } else if (ch >= 'f') {   /* 'f' or 'g' */
            if (ps2_cmpdf2(dbl, 0) == 0) {
                NL_PRINT(ps2_vfp_zero, 1);   /* kludge for __dtoa irregularity */
                if (expt < ndig || (flags & NL_ALT) != 0) {
                    NL_PRINT(decimal_point, 1);
                    NL_PAD(ndig - 1, ps2_vfp_zeroes);
                }
            } else if (expt <= 0) {
                NL_PRINT(ps2_vfp_zero, 1);
                if (expt || ndig) {
                    NL_PRINT(decimal_point, 1);
                    NL_PAD(-expt, ps2_vfp_zeroes);
                    NL_PRINT(cp, ndig);
                }
            } else if (expt >= ndig) {
                NL_PRINT(cp, ndig);
                NL_PAD(expt - ndig, ps2_vfp_zeroes);
                if (flags & NL_ALT) NL_PRINT(ps2_vfp_dot, 1);
            } else {
                NL_PRINT(cp, expt);
                cp += expt;
                NL_PRINT(ps2_vfp_dot, 1);
                NL_PRINT(cp, ndig - expt);
            }
        } else {   /* 'e' or 'E' */
            if (ndig > 1 || flags & NL_ALT) {
                ox[0] = *cp++;
                ox[1] = '.';
                NL_PRINT(ox, 2);
                if (ps2_cmpdf2(dbl, 0) != 0) NL_PRINT(cp, ndig - 1);
                else NL_PAD(ndig - 1, ps2_vfp_zeroes);   /* 0.[0..]: __dtoa irregularity */
            } else {   /* XeYYY */
                NL_PRINT(cp, 1);
            }
            NL_PRINT(expstr, expsize);
        }
        if (flags & NL_LADJUST) NL_PAD(width - realsz, ps2_vfp_blanks);   /* left-adjusting padding */
        ret += width > realsz ? width : realsz;
        NL_FLUSH();
    }
done:
    NL_FLUSH();
error:
    return (fp->_flags & NL_SERR) ? -1 : ret;
#undef NL_PRINT
#undef NL_PAD
#undef NL_FLUSH
#undef NL_UARG
#undef NL_DIGIT
}

/* 0x119c30 vfprintf(fp, fmt0, ap): CHECK_INIT, then _vfprintf_r (a tail jump after the 0x20 frame is popped) */
int ps2_native_vfprintf(ps2_FILE *fp, const char *fmt0, char *ap)
{
    if (fp->_data == 0) fp->_data = ps2_impure_ptr;
    if (re_word(fp->_data, RE_SDIDINIT) == 0) {
        PS2_FRAME(0x20);
        ps2_native___sinit(fp->_data);
    }
    return ps2_native__vfprintf_r(fp->_data, fp, fmt0, ap);
}

/* 0x118cd8 sprintf(str, fmt, ...): ap = the vararg slots (the original's sp + 0x90 = the caller's sp - 0x30) */
int ps2_native_sprintf(char *str, const char *fmt, char *ap)
{
    PS2_FRAME(0xc0);
    PS2_STACK(ps2_FILE, f, 0);   /* only _p, _w, _flags, _bf, _data are set (the rest is stale stack, unread) */
    f._flags = NL_SWR | NL_SSTR;
    f._bf._base = f._p = (unsigned char *)str;
    f._bf._size = f._w = 0x7fffffff;
    f._data = ps2_impure_ptr;
    int ret = ps2_native_vfprintf(&f, fmt, ap);
    *f._p = 0;
    return ret;
}

/* 0x11b5b8 vsprintf(str, fmt, ap): sprintf's twin with a 0x70 frame (FILE at sp+0, same fields; _data = the impure
 * pointer word 0x2577e8, as in sprintf) */
int ps2_native_vsprintf(char *str, const char *fmt, char *ap)
{
    PS2_FRAME(0x70);
    PS2_STACK(ps2_FILE, f, 0);
    f._bf._size = f._w = 0x7fffffff;
    f._flags = NL_SWR | NL_SSTR;
    f._bf._base = f._p = (unsigned char *)str;
    f._data = ps2_impure_ptr;
    int ret = ps2_native_vfprintf(&f, fmt, ap);
    *f._p = 0;
    return ret;
}
