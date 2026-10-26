/* What each build provides to the render module (design section 4.2): link-time free functions.
 * Hybrid: runtime/rq_glue.c. Sourceport: sourceport/main/host_rq.cpp (step 1.4; RQ_HOST_ADDR - addr is a 32-bit host
 * pointer, rq_internal.h rq_addr_fold). */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* addr = RAM 0..0x01ffffff or 0x70000000 + SPR offset (< 0x4000); NULL if [addr, addr+len) is not one
 * contiguous valid range. The module applies the DMA address rules (bit 31 -> SPR) before calling. */
uint8_t *rq_mem(uint32_t addr, uint32_t len);
void     rq_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
uint64_t rq_clock_ns(void);                     /* monotonic */
void    *rq_gl_proc(const char *name);          /* GL entry point of the current context */
int      rq_gl_current(void);                   /* 1 if a GL 4.5 context is current */
/* host GPU timing (GL backend; no-ops allowed): a segment of GL work begins (1) / ends (0) - the host brackets it
 * with GPU timestamp queries; ns of CPU time spent blocked on the GPU (readbacks, fence waits) */
void     rq_gpu_seg(int begin);
void     rq_gpu_wait_ns(uint64_t ns);

#ifdef __cplusplus
}
#endif
