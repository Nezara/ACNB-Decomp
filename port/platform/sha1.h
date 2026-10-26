/* SHA-1 (FIPS 180-1) for disc identity checks (phase 3 2.6). Generic, no game knowledge. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct Sha1 {
    uint32_t h[5];
    uint64_t len;        /* bytes hashed */
    uint8_t buf[64];
    uint32_t nbuf;
} Sha1;

void sha1_init(Sha1 *s);
void sha1_update(Sha1 *s, const void *p, size_t n);
void sha1_final(Sha1 *s, uint8_t out[20]);
void sha1_hex(const uint8_t dig[20], char out[41]);   /* lower-case hex + NUL */

#ifdef __cplusplus
}
#endif
