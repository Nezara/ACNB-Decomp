/* VRAM addressing of the module's own 4 MB GS memory model (design section 2). C++ only (render/ internal). */
#pragma once
#include <stdint.h>

enum { RQ_PSMCT32 = 0x00, RQ_PSMCT24 = 0x01, RQ_PSMCT16 = 0x02, RQ_PSMCT16S = 0x0a, RQ_PSMT8 = 0x13, RQ_PSMT4 = 0x14,
       RQ_PSMT8H = 0x1b, RQ_PSMT4HL = 0x24, RQ_PSMT4HH = 0x2c, RQ_PSMZ32 = 0x30, RQ_PSMZ24 = 0x31, RQ_PSMZ16 = 0x32,
       RQ_PSMZ16S = 0x3a };
#define RQ_GS_BLOCKS 0x4000u                  /* 4 MB local memory, 256-byte blocks */
#define RQ_VRAM_SIZE (RQ_GS_BLOCKS * 256u)

int  rqs_bits(int psm);     /* container bits as gs_hw.c psm_bits (24-bit and 8H/4HL/4HH formats: 32) */
int  rqs_layout(int psm);   /* swizzle family (gs_hw.c layout_of) */
/* blocks [lo,hi) touched by rows y0..y1-1 (and at least wpx pixels wide) of a buffer (gs_hw.c gs_block_range) */
void rqs_block_range(uint32_t bp, uint32_t bw, int psm, int y0, int y1, int wpx, uint32_t *lo, uint32_t *hi);
/* texel rect of VRAM block b of a texture at tbp / tbw (gs_hw.c block_rect) */
void rqs_block_rect(int psm, uint32_t tbp, uint32_t tbw, uint32_t b, int *x, int *y, int *bwpx, int *bhpx);
/* gs_swz.c: raw pixel values as gs_read_pixel / gs_write_pixel */
void rqs_read_rect(uint8_t *vram, int psm, uint32_t bp, uint32_t bw, int x, int y, int w, int h, uint32_t *dst, int stride);
/* returns 1 if any byte changed, with the changed block range [lo,hi) when lo != 0 */
int  rqs_write_rect(uint8_t *vram, int psm, uint32_t bp, uint32_t bw, int x, int y, int w, int h, const uint32_t *src,
                    int stride, uint32_t *lo, uint32_t *hi);
uint32_t rqs_read_pixel(uint8_t *vram, int psm, uint32_t bp, uint32_t bw, int x, int y);
void rqs_write_pixel(uint8_t *vram, int psm, uint32_t bp, uint32_t bw, int x, int y, uint32_t v);
