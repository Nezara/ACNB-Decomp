/* png - minimal PNG writer for screenshots (stored deflate blocks, RGB 8 bit). Same output as the hybrid's
 * runtime/video.c write_png (the hybrid keeps its own copy: it is not linked from port/platform/video). */
#include "png.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc_tab[256];

static uint32_t crc32_upd(uint32_t c, const uint8_t *p, size_t n)
{
    if (!crc_tab[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t x = i;
            for (int k = 0; k < 8; k++) x = (x >> 1) ^ (0xedb88320u & -(x & 1));
            crc_tab[i] = x;
        }
    c = ~c;
    while (n--) c = crc_tab[(c ^ *p++) & 255] ^ (c >> 8);
    return ~c;
}

static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static void chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n)
{
    uint8_t h[8];
    be32(h, n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    uint32_t c = crc32_upd(0, (const uint8_t *)type, 4);
    c = crc32_upd(c, d, n);
    uint8_t t[4];
    be32(t, c);
    fwrite(t, 1, 4, f);
}

int png_write_bgra(const char *path, const uint32_t *px, int w, int h, int stride)
{
    size_t raw_n = (size_t)h * (1 + (size_t)w * 3);
    uint8_t *raw = (uint8_t *)malloc(raw_n), *o = raw;
    size_t nblk = (raw_n + 65534) / 65535;
    uint8_t *z = (uint8_t *)malloc(2 + raw_n + nblk * 5 + 4), *q = z;
    if (!raw || !z) { free(raw); free(z); return 0; }
    for (int y = 0; y < h; y++) {
        *o++ = 0;
        for (int x = 0; x < w; x++) {
            uint32_t c = px[(size_t)y * stride + x];
            *o++ = (uint8_t)(c >> 16); *o++ = (uint8_t)(c >> 8); *o++ = (uint8_t)c;
        }
    }
    *q++ = 0x78; *q++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    for (size_t off = 0; off < raw_n; off += 65535) {
        uint32_t n = raw_n - off > 65535 ? 65535 : (uint32_t)(raw_n - off);
        *q++ = off + n >= raw_n; *q++ = (uint8_t)n; *q++ = (uint8_t)(n >> 8); *q++ = (uint8_t)~n; *q++ = (uint8_t)(~n >> 8);
        memcpy(q, raw + off, n);
        q += n;
    }
    be32(q, (b << 16) | a);
    q += 4;
    FILE *f = fopen(path, "wb");
    if (f) {
        static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
        fwrite(sig, 1, 8, f);
        uint8_t ihdr[13];
        be32(ihdr, (uint32_t)w); be32(ihdr + 4, (uint32_t)h);
        ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
        chunk(f, "IHDR", ihdr, 13);
        chunk(f, "IDAT", z, (uint32_t)(q - z));
        chunk(f, "IEND", 0, 0);
        fclose(f);
    }
    free(raw);
    free(z);
    return f != 0;
}
