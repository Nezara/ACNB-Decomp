/* src/gfx/tmx.c - TMX texture archive: a 0x10-byte header {size, num, ver, dmy} followed by num 0x10-byte entries
 * {offset, ext[4], fsize, dmy}; each entry's offset (from the archive start) is a TM2 block that DF_ReadTM2Pointer(_TSS)
 * uploads when mode != 0. Both return the last reader's result (the end of the last TM2 block), or bufp + 0x10 when
 * no reader ran. Quirk kept: the _TSS variant skips entries with offset 0, and an archive whose last entries are
 * skipped returns the end of the last block actually read. */
#include "acnb/port.h"
#include "acnb/globals.h"
#include "acnb/funcs.h"

char *DF_ReadTMXPointer_TSS(char *bufp, int mode, int TBP, int CBP)
{
    PS2_FRAME(0x70);
    char *ret = bufp + 0x10;
    unsigned int *ent = (unsigned int *)(bufp + 0x10);
    for (unsigned int i = 0; i < ((unsigned int *)bufp)[1]; i++, ent += 4) {
        if (ent[0] != 0) ret = DF_ReadTM2Pointer_TSS(bufp + ent[0], mode, TBP, CBP);
    }
    return ret;
}

char *DF_ReadTMXPointer(char *bufp, int mode)
{
    PS2_FRAME(0x50);
    unsigned int num = ((unsigned int *)bufp)[1];
    char *ret = bufp + 0x10 + (num << 4);
    unsigned int *ent = (unsigned int *)(bufp + 0x10);
    for (unsigned int i = 0; i < ((unsigned int *)bufp)[1]; i++, ent += 4) ret = DF_ReadTM2Pointer(bufp + ent[0], mode);
    return ret;
}
