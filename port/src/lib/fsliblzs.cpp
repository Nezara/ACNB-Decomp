/* src/lib/fsliblzs.cpp - the game's LZSS decompressor (whole file). A file starts with a 0xc-byte header (version,
 * big-endian uncompressed size, extension, type); type 1 = stored, otherwise LZSS: flag bytes (LSB first, bit set =
 * match), literals, and 2-byte matches = 12-bit offset from the start of a 4 KB window of the output + 4-bit
 * (length - 1), length nibble 0 = end of data. The window starts at dst; once the output is 4 KB past it, it jumps to
 * dst - 0x1000 and from then on advances by the length of every item written. Matches copy bytewise, so a source
 * overlapping the bytes being written replicates them. No bounds checks. */
#include "acnb/port.h"

int FsLZSSGetUncompressSize(_FS_LZSS_FILE_HEADER_ST *headerP)
{
    return (headerP->uncomSize[0] << 24) + (headerP->uncomSize[1] << 16) + (headerP->uncomSize[2] << 8) +
           headerP->uncomSize[3];
}

void FsLZSSUncompress(unsigned char *dstBufP, unsigned char *srcBufP)
{
    _FS_LZSS_FILE_HEADER_ST *headerP = (_FS_LZSS_FILE_HEADER_ST *)srcBufP;
    unsigned char *src = srcBufP + sizeof(_FS_LZSS_FILE_HEADER_ST);
    unsigned char *dst = dstBufP;

    if (headerP->type == 1) {   /* stored */
        unsigned int n = FsLZSSGetUncompressSize(headerP);
        while (n--) *dst++ = *src++;
        return;
    }

    unsigned char *win = dstBufP;
    int first = 1;
    unsigned int mask = 1;
    unsigned int flags = *src++;
    for (;;) {
        int len;
        if ((flags & mask) == 0) {
            *dst++ = *src++;
            len = 1;
        } else {
            int off = (src[0] << 4) + (src[1] >> 4);
            int cnt = src[1] & 0xf;
            if (cnt == 0) return;
            len = cnt + 1;
            src += 2;
            unsigned char *from = win + off;
            for (int i = 0; i < len; i++) *dst++ = from[i];
        }
        if ((int)(dst - win) >= 0x1000) {
            win += len;
            if (first) {
                win = dst - 0x1000;
                first = 0;
            }
        }
        mask = (mask << 1) & 0xff;
        if (mask == 0) {
            flags = *src++;
            mask = 1;
        }
    }
}
