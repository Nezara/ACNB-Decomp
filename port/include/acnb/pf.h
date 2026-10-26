/* Pf_SPrintf (lib/pf.c, the game's own BSD-style printf; variadic, native: src/lib/pf.cpp) with one int argument
 * (pf_sprintf_d) or a string and an int (pf_sprintf_sd). The original code formats HUD numbers / file names with it
 * instead of newlib sprintf. The hybrid calls it through gcall (the registered native, or the original when checked);
 * the source port calls the C++ variadic. */
#pragma once
#ifdef ACNB_SOURCEPORT
static inline int pf_sprintf_d(char *buf, const char *fmt, int v) { return Pf_SPrintf(buf, (char *)fmt, v); }
static inline int pf_sprintf_sd(char *buf, const char *fmt, const char *s, int v)
{
    return Pf_SPrintf(buf, (char *)fmt, s, v);
}
#else
static inline int pf_sprintf_d(char *buf, const char *fmt, int v)
{
    return gcall_as<int(char *, const char *, int)>(GFN(Pf_SPrintf), buf, fmt, v);
}
static inline int pf_sprintf_sd(char *buf, const char *fmt, const char *s, int v)
{
    return gcall_as<int(char *, const char *, const char *, int)>(GFN(Pf_SPrintf), buf, fmt, s, v);
}
#endif
