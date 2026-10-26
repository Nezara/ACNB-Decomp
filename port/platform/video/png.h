#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* px: 0xAARRGGBB words (rq_read_output / GL_BGRA), top row first; alpha dropped. 1 = written. */
int png_write_bgra(const char *path, const uint32_t *px, int w, int h, int stride);
#ifdef __cplusplus
}
#endif
