/* src/lib/gfzfunc.cpp - GFZ container unpacking (the Versus part of the file: Gfz_Uncompress). A GFZ file has a
 * 0x20-byte StGfzHeader whose nameType names the payload format: "fsliblzs" = an LZSS file (with its own header),
 * "rawdata" = sizeFile - 0x20 stored bytes. Returns 0, or -1 for an unknown type. */
#include "acnb/port.h"
#include <string.h>

int Gfz_Uncompress(StGfzHeader *pGfz, char *pBuff)
{
    int type;
    if (strcmp(pGfz->nameType, "fsliblzs") == 0)
        type = 0;
    else if (strcmp(pGfz->nameType, "rawdata") == 0)
        type = 1;
    else
        type = -1;

    if (type == 1) {
        memcpy(pBuff, (char *)pGfz + sizeof(StGfzHeader), pGfz->sizeFile - (int)sizeof(StGfzHeader));
        return 0;
    }
    if (type == 0) {
        FsLZSSUncompress((unsigned char *)pBuff, (unsigned char *)pGfz + sizeof(StGfzHeader));
        return 0;
    }
    return -1;
}
