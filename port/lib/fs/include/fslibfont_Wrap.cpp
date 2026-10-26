/* lib/fs/include/fslibfont_Wrap.cpp - lib/fs/include/fslibfont_Wrap.h: the printf-style text draw wrapper over the
 * DWARF-less fontlib. In the original it is a `static` defined in the header, so every TU including it gets a copy;
 * the ELF has one, in lib/fs/fslibmmo (0x144d30). This file is its own TU so gen_bridge registers the native at that
 * address; lib/fs/fslibmmo/fslibmmo.cpp #includes it (same TU as the original) and calls it directly.
 * FsFontDrawText(x, y, fmt, ...): set the position (fontlib 0x102c60 with x / y in f12 / f13), format fmt into a
 * 0x428-byte stack buffer (fontlib 0x1025e0, a vsprintf; ap = the MW EABI varargs slots a1..t3 the prologue spills
 * at sp+0x448), draw it (0x1032a0) unless the formatter failed.
 * Original quirks kept: the string is a FORMAT string (a '%' in the text is interpreted); no buffer bound; the only
 * caller (_DrawText) passes no varargs, so ap points at whatever the spill slots hold (the native leaves them as
 * they are: below the caller's sp, nothing else reads them). */
#include "acnb/sdk.h"

static int FsFontDrawText(float x, float y, char *str)
{
    PS2_FRAME(0x480);
    PS2_STACK(char[0x428], buf, 0x10);                  /* sp+0x10 .. 0x438 (fmt spill at 0x438) */
    PS2_STACK(unsigned long long[7], va, 0x448);        /* a1..t3 spill slots = ap */
    fontlib_00102c60(6, x, y);
    int r = fontlib_001025e0(buf, str, va);
    if (r < 0) return r;
    return fontlib_001032a0(0, buf);
}
