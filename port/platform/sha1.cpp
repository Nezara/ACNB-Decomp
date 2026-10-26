/* SHA-1 (see sha1.h). */
#include "sha1.h"
#include <string.h>

static uint32_t rol(uint32_t x, int n) { return x << n | x >> (32 - n); }

static void block(Sha1 *s, const uint8_t *p)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999u; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdcu; }
        else { f = b ^ c ^ d; k = 0xca62c1d6u; }
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e;
}

extern "C" void sha1_init(Sha1 *s)
{
    static const uint32_t h0[5] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u};
    memcpy(s->h, h0, sizeof h0);
    s->len = 0;
    s->nbuf = 0;
}

extern "C" void sha1_update(Sha1 *s, const void *pv, size_t n)
{
    const uint8_t *p = (const uint8_t *)pv;
    s->len += n;
    if (s->nbuf) {
        while (n && s->nbuf < 64) { s->buf[s->nbuf++] = *p++; n--; }
        if (s->nbuf < 64) return;
        block(s, s->buf);
        s->nbuf = 0;
    }
    for (; n >= 64; n -= 64, p += 64) block(s, p);
    memcpy(s->buf, p, n);
    s->nbuf = (uint32_t)n;
}

extern "C" void sha1_final(Sha1 *s, uint8_t out[20])
{
    uint64_t bits = s->len * 8;
    uint8_t pad = 0x80;
    sha1_update(s, &pad, 1);
    pad = 0;
    while (s->nbuf != 56) sha1_update(s, &pad, 1);
    uint8_t l[8];
    for (int i = 0; i < 8; i++) l[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha1_update(s, l, 8);
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 4; j++) out[4 * i + j] = (uint8_t)(s->h[i] >> (24 - 8 * j));
}

extern "C" void sha1_hex(const uint8_t dig[20], char out[41])
{
    static const char hx[] = "0123456789abcdef";
    for (int i = 0; i < 20; i++) { out[2 * i] = hx[dig[i] >> 4]; out[2 * i + 1] = hx[dig[i] & 15]; }
    out[40] = 0;
}
