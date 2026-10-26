/* iopdiv.h - the IOP divide-by-zero rule, shared by every native IOP
 * module: the native does what the hybrid oracle's IOP interpreter does (runtime/iop_cpu.c).
 *  - div  (SPECIAL 0x1a): x / 0 -> LO = (x >= 0 ? -1 : 1), HI = x; INT_MIN / -1 -> LO = INT_MIN, HI = 0.
 *  - divu (SPECIAL 0x1b): x / 0 -> LO = 0xffffffff, HI = x.
 *  - break (SPECIAL 0x0d): ee_log("iop: break ...") and continue. GCC's guard `bnez b, 1f; div; break 7` (iopdis shows
 *    the code field as 0x1c00) and `break 6` (0x1800, INT_MIN / -1) sits after the bnez's delay slot, so it IS executed
 *    for a zero divisor, and the hybrid logs it and goes on with the LO / HI above. Modules built without the guard
 *    (FSSDEVAG, FSSDIVAG, FSSD) give the same LO / HI silently.
 * So a native site continues as the asm with these LO / HI values, plus one plat_log per site (the IOP_DIV* macros keep
 * one static flag per expansion; `at` = the module offset of the div, shown in the log). INT_MIN / -1 is not logged. */
#pragma once
#include <stdint.h>
#include "../../plat.h"

namespace iopdiv {

inline void Warn(bool &warned, const char *what, uint32_t at, uint32_t a)
{
    if (warned) return;
    warned = true;
    plat_log("iop: %s: divide by zero at %05x (dividend %08x): no trap, LO / HI as runtime/iop_cpu.c, the asm goes on",
             what, at, a);
}

/* divu LO */
inline uint32_t DivuLo(uint32_t a, uint32_t b, bool &w, const char *what, uint32_t at)
{
    if (b == 0) { Warn(w, what, at, a); return 0xffffffffu; }
    return a / b;
}

/* div LO */
inline int32_t DivLo(int32_t a, int32_t b, bool &w, const char *what, uint32_t at)
{
    if (b == 0) { Warn(w, what, at, (uint32_t)a); return a >= 0 ? -1 : 1; }
    if (b == -1) return (int32_t)(0u - (uint32_t)a);   /* INT_MIN / -1 = INT_MIN */
    return a / b;
}

/* div HI */
inline int32_t DivHi(int32_t a, int32_t b, bool &w, const char *what, uint32_t at)
{
    if (b == 0) { Warn(w, what, at, (uint32_t)a); return a; }
    if (b == -1) return 0;
    return a % b;
}

}  // namespace iopdiv

/* one static log flag per macro expansion (each lambda is its own type) */
#define IOPDIV_FLAG_ ([]() -> bool & { static bool w_; return w_; }())
#define IOP_DIVU(a, b, at, what) ::iopdiv::DivuLo((uint32_t)(a), (uint32_t)(b), IOPDIV_FLAG_, (what), (uint32_t)(at))
#define IOP_DIV(a, b, at, what) ::iopdiv::DivLo((int32_t)(a), (int32_t)(b), IOPDIV_FLAG_, (what), (uint32_t)(at))
#define IOP_REM(a, b, at, what) ::iopdiv::DivHi((int32_t)(a), (int32_t)(b), IOPDIV_FLAG_, (what), (uint32_t)(at))
