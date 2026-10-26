/* src/lib/pf.cpp - the game's own sprintf (4.4BSD vfprintf with the floating-point part replaced by a simple
 * digit loop, __ftoa). All native. Pf_SPrintf / Pf_SnPrintf are variadic: the original spills a2..t3 (a3..t3) into
 * its MW EABI save area, where the slots continue into the caller's stack arguments, and passes that as ap; the
 * hybrid enters them through bw__Pf_SPrintf / bw__Pf_SnPrintf (the same spill), native code calls the C++ variadics
 * (host va_list: __vfprintf's body is a template over the argument source).
 * ap is the PS2 va_list: 8-byte slots (ints / pointers in the low word, doubles as 64-bit values).
 * Float conversions differ from BSD: the value is converted to float, __ftoa writes at most maxfig characters
 * (digits and the point) into a static buffer, and only size - 2 characters of it are printed (%e/%E never print
 * the exponent built in expstr). Kept as is. */
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include "acnb/port.h"
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

enum {
    PF_ALT = 0x01, PF_HEXPREFIX = 0x02, PF_LADJUST = 0x04, PF_LONGDBL = 0x08, PF_LONGINT = 0x10,
    PF_SHORTINT = 0x40, PF_ZEROPAD = 0x80, PF_FPT = 0x100,
};
enum { PF_NIOV = 8, PF_PADSIZE = 16, PF_BUF = 348 };

/* va_arg on the PS2 slots */
static inline int pf_arg_int(char *&ap) { int v; memcpy(&v, ap, 4); ap += 8; return v; }
static inline void *pf_arg_ptr(char *&ap) { return (void *)(uintptr_t)(unsigned int)pf_arg_int(ap); }
static inline unsigned long long pf_arg_dbl(char *&ap) { unsigned long long d; memcpy(&d, ap, 8); ap += 8; return d; }
/* the same reads on a host va_list (the C++ variadics Pf_SPrintf / Pf_SnPrintf) */
struct pf_va { va_list v; };
static inline int pf_arg_int(pf_va *ap) { return va_arg(ap->v, int); }
static inline void *pf_arg_ptr(pf_va *ap) { return va_arg(ap->v, void *); }
static inline unsigned long long pf_arg_dbl(pf_va *ap)
{
    double d = va_arg(ap->v, double);
    unsigned long long u;
    memcpy(&u, &d, 8);
    return u;
}

static int __exponent(char *p0, int exp, int fmtch)
{
    char *p, *t;
    char expbuf[308];

    p = p0;
    *p++ = fmtch;
    if (exp < 0) {
        exp = -exp;
        *p++ = '-';
    } else {
        *p++ = '+';
    }
    t = expbuf + 308;
    if (exp > 9) {
        do {
            *--t = exp % 10 + '0';
        } while ((exp /= 10) > 9);
        *--t = exp + '0';
        for (; t < expbuf + 308; *p++ = *t++);
    } else {
        *p++ = '0';
        *p++ = exp + '0';
    }
    return p - p0;
}

/* the inlined __sprint: write out and empty the iov list */
static int __sfvwrite(struct_03fdc680 *fp, __suio *uio);
static inline int pf_sprint(struct_03fdc680 *fp, __suio *uio)
{
    if (uio->uio_resid == 0) {
        uio->uio_iovcnt = 0;
        return 0;
    }
    int err = __sfvwrite(fp, uio);
    uio->uio_resid = 0;
    uio->uio_iovcnt = 0;
    return err;
}

static char *__ultoa(unsigned int val, char *endp, int base, int octzero, char *xdigs);
static int __ftoa(char *pstr, float value, int maxlen, int maxfig);

/* newlib isinf / isnan (0x118880 / 0x1188c8) on the double's bits */
static int pf_isinf(unsigned long long d)
{
    unsigned int hx = (unsigned int)(d >> 32) & 0x7fffffffu, lx = (unsigned int)d;
    hx |= (lx | (0u - lx)) >> 31;
    hx = 0x7ff00000u - hx;
    return 1 - (int)((hx | (0u - hx)) >> 31);
}
static int pf_isnan(unsigned long long d)
{
    unsigned int hx = (unsigned int)(d >> 32) & 0x7fffffffu, lx = (unsigned int)d;
    hx |= (lx | (0u - lx)) >> 31;
    return (int)((0x7ff00000u - hx) >> 31);
}

template <class AP> static int pf_vfprintf(struct_03fdc680 *fp, char *fmt0, AP ap)
{
    static const char blanks[PF_PADSIZE] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
                                            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};   /* 0x0025cb30 */
    static const char zeroes[PF_PADSIZE] = {'0', '0', '0', '0', '0', '0', '0', '0',
                                            '0', '0', '0', '0', '0', '0', '0', '0'};   /* 0x0025cb40 */
    char *fmt = fmt0;
    int ch, n;
    char *cp;
    __siov *iovp;
    int flags, ret = 0, width, prec;
    char sign, softsign;
    float fvalue;
    int expt, expsize = 0, ndig = 0;
    char expstr[7];
    unsigned int ulval = 0;
    int base = 10;
    int dprec, fieldsz, realsz, size;
    char *xdigs = 0;
    __suio uio;
    __siov iov[PF_NIOV];
    char buf[PF_BUF];
    char ox[2];

#define PRINT(ptr, len) do { \
        iovp->iov_base = (void *)(ptr); \
        iovp->iov_len = (len); \
        uio.uio_resid += (len); \
        iovp++; \
        if (++uio.uio_iovcnt >= PF_NIOV) { \
            if (pf_sprint(fp, &uio)) goto error; \
            iovp = iov; \
        } \
    } while (0)
#define PAD(howmany, with) do { \
        if ((n = (howmany)) > 0) { \
            while (n > PF_PADSIZE) { PRINT(with, PF_PADSIZE); n -= PF_PADSIZE; } \
            PRINT(with, n); \
        } \
    } while (0)
#define FLUSH() do { \
        if (uio.uio_resid && pf_sprint(fp, &uio)) goto error; \
        uio.uio_iovcnt = 0; \
        iovp = iov; \
    } while (0)
#define UARG() (flags & PF_LONGINT ? (unsigned int)pf_arg_int(ap) \
                : flags & PF_SHORTINT ? (unsigned int)(unsigned short)pf_arg_int(ap) : (unsigned int)pf_arg_int(ap))

    /* unbuffered unix file: the BSD __sbprintf path (not used by the game) */
    if ((fp->_flags & 0x1a) == 0xa && fp->_file >= 0) return 0;

    uio.uio_iov = iovp = iov;
    uio.uio_resid = 0;
    uio.uio_iovcnt = 0;

    for (;;) {
        for (cp = fmt; (ch = *fmt) != '\0' && ch != '%'; fmt++);
        if ((n = fmt - cp) != 0) {
            PRINT(cp, n);
            ret += n;
        }
        if (ch == '\0') goto done;
        fmt++;

        flags = 0;
        dprec = 0;
        width = 0;
        prec = -1;
        sign = '\0';

    rflag:
        ch = *fmt++;
    reswitch:
        switch (ch) {
        case ' ':
            if (!sign) sign = ' ';
            goto rflag;
        case '#':
            flags |= PF_ALT;
            goto rflag;
        case '*':
            if ((width = pf_arg_int(ap)) >= 0) goto rflag;
            width = -width;
            [[fallthrough]];
        case '-':
            flags |= PF_LADJUST;
            goto rflag;
        case '+':
            sign = '+';
            goto rflag;
        case '.':
            if ((ch = *fmt++) == '*') {
                n = pf_arg_int(ap);
                prec = n < 0 ? -1 : n;
                goto rflag;
            }
            n = 0;
            while ((unsigned)(ch - '0') < 10) {
                n = 10 * n + (ch - '0');
                ch = *fmt++;
            }
            prec = n < 0 ? -1 : n;
            goto reswitch;
        case '0':
            flags |= PF_ZEROPAD;
            goto rflag;
        case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
            n = 0;
            do {
                n = 10 * n + (ch - '0');
                ch = *fmt++;
            } while ((unsigned)(ch - '0') < 10);
            width = n;
            goto reswitch;
        case 'L':
            flags |= PF_LONGDBL;
            goto rflag;
        case 'h':
            flags |= PF_SHORTINT;
            goto rflag;
        case 'l':
            flags |= PF_LONGINT;
            goto rflag;
        case 'c':
            *(cp = buf) = (char)pf_arg_int(ap);
            size = 1;
            sign = '\0';
            break;
        case 'D':
            flags |= PF_LONGINT;
            [[fallthrough]];
        case 'd':
        case 'i': {
            int v = flags & PF_LONGINT ? pf_arg_int(ap)
                    : flags & PF_SHORTINT ? (short)pf_arg_int(ap) : pf_arg_int(ap);
            if (v < 0) {
                v = -v;
                sign = '-';
            }
            ulval = (unsigned int)v;
            base = 10;
            goto number;
        }
        case 'e':
        case 'E':
            prec = prec == -1 ? 7 : prec + 1;
            goto fp_begin;
        case 'f':
            if (prec != 0) flags |= PF_ALT;
            [[fallthrough]];
        case 'g':
        case 'G':
            if (prec == -1) prec = 6;
        fp_begin: {
            unsigned long long d = pf_arg_dbl(ap);   /* ld; __truncdfsf2 (truncating soft double) */
            fvalue = ps2_u2f(ps2_truncdfsf2(d));
            /* __extendsfdf2 + newlib isinf / isnan: never true on the PS2 (fp-bit widens a float with exponent 255
             * as a 2^128-range number), so an Inf / NaN float goes on to __ftoa - kept */
            if (pf_isinf(ps2_extendsfdf2(ps2_f2u(fvalue)))) {
                if (fvalue < 0.0f) sign = '-';
                cp = (char *)"Inf";
                size = 3;
                break;
            }
            if (pf_isnan(ps2_extendsfdf2(ps2_f2u(fvalue)))) {
                cp = (char *)"NaN";
                size = 3;
                break;
            }
            flags |= PF_FPT;
            if (fvalue < 0.0f) {
                fvalue = -fvalue;
                softsign = '-';
            } else {
                softsign = '\0';
            }
            ndig = __ftoa(pf_ftoa_buf, fvalue, 0x40, prec);
            expt = ndig - (prec + 1);
            cp = pf_ftoa_buf;
            size = expt;
            if (ch == 'g' || ch == 'G') {
                if (expt < -3 || expt > prec) ch = ch == 'g' ? 'e' : 'E';
                else ch = 'g';
            }
            if (ch < 'f') {   /* e / E */
                expsize = __exponent(expstr, expt - 1, ch);
                size = expsize + ndig;
                if (ndig > 1 || flags & PF_ALT) ++size;
            } else if (ch == 'f') {
                if (expt > 0) {
                    if (prec || flags & PF_ALT) size = expt + (prec + 1);
                } else {
                    size = prec + 2;
                }
            } else if (expt >= ndig) {
                if (flags & PF_ALT) ++size;
            } else {
                size = ndig + (expt > 0 ? 1 : 2 - expt);
            }
            if (softsign) sign = '-';
            break;
        }
        case 'n':
            if (flags & PF_LONGINT) {
                long long *p = (long long *)pf_arg_ptr(ap);
                *p = ret;
            } else if (flags & PF_SHORTINT) {
                short *p = (short *)pf_arg_ptr(ap);
                *p = ret;
            } else {
                int *p = (int *)pf_arg_ptr(ap);
                *p = ret;
            }
            continue;
        case 'O':
            flags |= PF_LONGINT;
            [[fallthrough]];
        case 'o':
            ulval = UARG();
            base = 8;
            goto nosign;
        case 'p':
            ulval = (unsigned int)pf_arg_int(ap);
            base = 16;
            xdigs = (char *)"0123456789abcdef";   /* 0x0025cb50 */
            flags |= PF_HEXPREFIX;
            ch = 'x';
            goto nosign;
        case 's':
            if ((cp = (char *)pf_arg_ptr(ap)) == 0) cp = (char *)"(null)";
            if (prec >= 0) {
                char *p = (char *)memchr(cp, 0, prec);
                if (p) {
                    size = p - cp;
                    if (size > prec) size = prec;
                } else {
                    size = prec;
                }
            } else {
                size = strlen(cp);
            }
            sign = '\0';
            break;
        case 'U':
            flags |= PF_LONGINT;
            [[fallthrough]];
        case 'u':
            ulval = UARG();
            base = 10;
            goto nosign;
        case 'X':
            xdigs = (char *)"0123456789ABCDEF";   /* 0x0025cb70 */
            goto hex;
        case 'x':
            xdigs = (char *)"0123456789abcdef";   /* 0x0025cb50 */
        hex:
            ulval = UARG();
            base = 16;
            if (flags & PF_ALT && ulval != 0) flags |= PF_HEXPREFIX;
        nosign:
            sign = '\0';
        number:
            if ((dprec = prec) >= 0) flags &= ~PF_ZEROPAD;
            cp = buf + PF_BUF;
            if (ulval != 0 || prec != 0) cp = __ultoa(ulval, cp, base, flags & PF_ALT, xdigs);
            size = buf + PF_BUF - cp;
            break;
        default:
            if (ch == '\0') goto done;
            cp = buf;
            *cp = ch;
            size = 1;
            sign = '\0';
            break;
        }

        fieldsz = size;
        if (sign) fieldsz++;
        else if (flags & PF_HEXPREFIX) fieldsz += 2;
        realsz = dprec > fieldsz ? dprec : fieldsz;

        if ((flags & (PF_LADJUST | PF_ZEROPAD)) == 0) PAD(width - realsz, blanks);
        if (sign) {
            PRINT(&sign, 1);
        } else if (flags & PF_HEXPREFIX) {
            ox[0] = '0';
            ox[1] = ch;
            PRINT(ox, 2);
        }
        if ((flags & (PF_LADJUST | PF_ZEROPAD)) == PF_ZEROPAD) PAD(width - realsz, zeroes);
        PAD(dprec - fieldsz, zeroes);
        if (flags & PF_FPT) PRINT(cp, size - 2);
        else PRINT(cp, size);
        if (flags & PF_LADJUST) PAD(width - realsz, blanks);

        ret += width > realsz ? width : realsz;
        FLUSH();
    }
done:
    FLUSH();
error:
    return fp->_flags & 0x40 ? -1 : ret;   /* __SERR */
#undef PRINT
#undef PAD
#undef FLUSH
#undef UARG
}
static int __vfprintf(struct_03fdc680 *fp, char *fmt0, char *ap)
{
    PS2_FRAME(0x310);
    return pf_vfprintf(fp, fmt0, ap);
}
static int pf_vfprintf_va(struct_03fdc680 *fp, char *fmt0, pf_va *ap)
{
    PS2_FRAME(0x310);
    return pf_vfprintf(fp, fmt0, ap);
}

/* string output only (__SSTR): copies what fits into _w, consumes everything */
static int __sfvwrite(struct_03fdc680 *fp, __suio *uio)
{
    unsigned int len;
    char *p;
    __siov *iov;
    unsigned int w;

    if (uio->uio_resid == 0) return 0;
    iov = uio->uio_iov;
    p = (char *)iov->iov_base;
    len = iov->iov_len;
    iov++;
    if (fp->_flags & 2) return 0;   /* __SNBF */
    if (fp->_flags & 1) return 0;   /* __SLBF */
    do {
        while (len == 0) {
            p = (char *)iov->iov_base;
            len = iov->iov_len;
            iov++;
        }
        w = (unsigned int)fp->_w;
        if (fp->_flags & 0x200) {   /* __SSTR */
            if (len < w) w = len;
            memcpy(fp->_p, p, w);
            fp->_w -= w;
            fp->_p += w;
            w = len;
        }
        p += w;
        len -= w;
    } while ((uio->uio_resid -= w) != 0);
    return 0;
}

static char *__ultoa(unsigned int val, char *endp, int base, int octzero, char *xdigs)
{
    char *cp = endp;
    int sval;

    switch (base) {
    case 10:
        if (val < 10) {
            *--cp = val + '0';
            return cp;
        }
        if (val > 0x7fffffffu) {
            *--cp = val % 10 + '0';
            sval = val / 10;
        } else {
            sval = val;
        }
        do {
            *--cp = sval % 10 + '0';
            sval /= 10;
        } while (sval != 0);
        break;
    case 8:
        do {
            *--cp = (val & 7) + '0';
            val >>= 3;
        } while (val);
        if (octzero && *cp != '0') *--cp = '0';
        break;
    case 16:
        do {
            *--cp = xdigs[val & 15];
            val >>= 4;
        } while (val);
        break;
    }
    return cp;
}

/* Decimal digits of value: i counts the integer digits still to write; the point goes after the last of them (i
 * then parks at -0x10000), and the loop stops once maxfig characters are written while i < 2. Returns the length,
 * 0 if maxlen < 3, or 1 with "?" when maxlen - 1 characters would be exceeded. */
static int __ftoa(char *pstr, float value, int maxlen, int maxfig)
{
    int i = 0, integer;
    float ten;
    char *pc = pstr;

    if (maxlen < 3) return 0;
    if (value < 0.0f) {
        *pc++ = '-';
        value = -value;
    }
    if (pc - pstr < maxlen - 1) {
        ten = 1.0f;
        while (!(value * ten < 1.0f)) {
            ten = ten * 0.1f;
            i++;
        }
        if (i < maxlen - 1) {
            if (ten < 1.0f) ten = ten * 10.0f;
            for (;;) {
                integer = ps2_ftoi(value * ten);
                *pc++ = (integer > 0 ? integer % 10 : 0) + '0';
                if (!(pc - pstr < maxlen - 1)) break;
                i--;
                if (i <= 0 && i >= -0x10000) {
                    i = -0x10000;
                    *pc++ = '.';
                }
                int intfig = pc - pstr;
                if (!(intfig < maxlen - 1)) break;
                if (intfig >= maxfig && i < 2) {
                    *pc = '\0';
                    return intfig;
                }
                ten = ten * 10.0f;
            }
        }
    }
    *pstr = '?';
    pstr[1] = '\0';
    return 1;
}

int Pf_VsnPrintf(char *pstr, unsigned int n, char *pfmt, char *ap)
{
    PS2_FRAME(0x30);
    struct_03fdc680 f;   /* _r, _file, _lbfsize stay unset (not read for a string) */
    if ((int)n < 1) return -1;
    f._flags = 0x208;   /* __SWR | __SSTR */
    f._w = n - 1;
    f._p = (unsigned char *)pstr;
    f._bf._base = (unsigned char *)pstr;
    f._bf._size = n - 1;
    int ret = __vfprintf(&f, pfmt, ap);
    *f._p = '\0';
    return ret;
}

/* Pf_SPrintf / Pf_SnPrintf: a string FILE at sp + 0x10 of the 0x80 / 0x90 frame (_r, _file, _lbfsize unset);
 * w = room left for characters (the terminating NUL always fits) */
static int pf_sprintf_str(char *pstr, unsigned int w, char *pfmt, char *ap)
{
    struct_03fdc680 f;
    f._p = (unsigned char *)pstr;
    f._bf._base = (unsigned char *)pstr;
    f._flags = 0x208;   /* __SWR | __SSTR */
    f._w = w;
    f._bf._size = w;
    int ret = __vfprintf(&f, pfmt, ap);
    *f._p = '\0';
    return ret;
}
static int pf_sprintf_str(char *pstr, unsigned int w, char *pfmt, pf_va *ap)
{
    struct_03fdc680 f;
    f._p = (unsigned char *)pstr;
    f._bf._base = (unsigned char *)pstr;
    f._flags = 0x208;
    f._w = w;
    f._bf._size = w;
    int ret = pf_vfprintf_va(&f, pfmt, ap);
    *f._p = '\0';
    return ret;
}

int Pf_SPrintf(char *pstr, char *pfmt, ...)
{
    PS2_FRAME(0x80);
    pf_va a;
    va_start(a.v, pfmt);
    int ret = pf_sprintf_str(pstr, 0x7ffffffeu, pfmt, &a);
    va_end(a.v);
    return ret;
}

int Pf_SnPrintf(char *pstr, unsigned int n, char *pfmt, ...)
{
    if ((int)n < 1) return -1;
    PS2_FRAME(0x90);
    pf_va a;
    va_start(a.v, pfmt);
    int ret = pf_sprintf_str(pstr, n - 1, pfmt, &a);
    va_end(a.v);
    return ret;
}

#ifndef ACNB_SOURCEPORT
/* MIPS -> native: the original prologues store the variadic registers as 8-byte slots right below the caller's sp
 * (Pf_SPrintf: sd a2..t3 at 0x50..0x78 of 0x80, Pf_SnPrintf: sd a3..t3 at 0x68..0x88 of 0x90) and pass that as ap,
 * so the caller's stack arguments follow; the argument registers are read before the native runs */
static void bw__Pf_SPrintf(EECore *c)
{
    char *pstr = (char *)bridge_ptr(c->gpr[A0].uw[0]), *pfmt = (char *)bridge_ptr(c->gpr[A1].uw[0]);
    u32 sp = c->gpr[SP].uw[0];
    for (int i = 0; i < 6; i++) *(u64 *)bridge_ptr(sp - 0x30 + 8 * i) = c->gpr[A2 + i].ud[0];
    PS2_FRAME(0x80);
    c->gpr[V0].sd[0] = pf_sprintf_str(pstr, 0x7ffffffeu, pfmt, (char *)bridge_ptr(sp - 0x30));
}
static void bw__Pf_SnPrintf(EECore *c)
{
    char *pstr = (char *)bridge_ptr(c->gpr[A0].uw[0]), *pfmt = (char *)bridge_ptr(c->gpr[A2].uw[0]);
    int n = c->gpr[A1].sw[0];
    u32 sp = c->gpr[SP].uw[0];
    for (int i = 0; i < 5; i++) *(u64 *)bridge_ptr(sp - 0x28 + 8 * i) = c->gpr[A3 + i].ud[0];
    if (n < 1) {   /* bgtz a1 */
        c->gpr[V0].sd[0] = -1;
        return;
    }
    PS2_FRAME(0x90);
    c->gpr[V0].sd[0] = pf_sprintf_str(pstr, (unsigned int)n - 1, pfmt, (char *)bridge_ptr(sp - 0x28));
}
#endif
