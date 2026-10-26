/* Shared VU1 HLE program table (design section 4.3). The module's VU1 model and the frozen reference's
 * runtime/vu1_hle.c dispatcher both call these programs; every bit of state comes in through RqVu1Ctx. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    union { uint32_t u[4]; float f[4]; uint64_t ud[2]; } vf[32];
    uint16_t vi[16];
    float acc[4];
    float i, q, p;
    uint32_t r, mac, status, clip, tpc;
    uint64_t cycle;
} RqVuRegs;   /* == the reference's VU1State layout (vu1_hle.c static_asserts it) */

/* Step 5.1 re-projection sideband: the float screen position (x, y in GS 1/16 units, i.e. before FTOI4) of an XYZ2
 * qword a program stored at data qword i, with the two integer words it stored (ix, iy) and the call that stored it
 * (gen). The module's XGKICK parser uses it only when gen is the current call and the words still match. */
typedef struct { float x, y; uint32_t ix, iy, gen; } RqVu1Hxy;

typedef struct RqVu1Ctx {
    uint8_t *mem; RqVuRegs *r; uint32_t top, itop;              /* 16 KB data memory, registers, VIF TOP / ITOP */
    void (*kick)(struct RqVu1Ctx *c, uint32_t qw, int stream);  /* XGKICK; stream = strip fast path allowed */
    void *kick_user; uint32_t st_env, st_pl, st_vert;           /* counters (were file statics in vu1_hle.c) */
    uint32_t warned;                                            /* one-time warnings (were function statics) */
    RqVu1Hxy *hxy; uint32_t hgen;   /* 1024 entries (one per data qword) or NULL (reference, option off): step 5.1 */
} RqVu1Ctx;

typedef struct {
    const char *name; uint32_t lo, hi, hash; void (*entry[2])(RqVu1Ctx *); uint32_t at[2];
    uint32_t tag;      /* output GIF tag at TOP+tag, vertices (3 qw) after it */
    uint32_t live;     /* VF registers the program leaves for later calls (check mode compares them) */
    int stq;           /* 0: STQ slots are left stale (untextured program), not compared */
    uint32_t livei;    /* VI registers likewise */
    int check_only;    /* 1: new HLE awaiting verification - served only in ACNB_VU1_HLE_CHECK mode (vu1_hle.c), the
                          module and the plain reference skip it (interpreter / RQ_EV_VU1_MISSING) */
} RqVu1Prog;

#define RQ_VU1_MAXPROG 32
extern const RqVu1Prog rq_vu1_progs[];
extern const int rq_vu1_nprogs;
uint32_t rq_vu1_hash(const uint8_t *micro, uint32_t lo, uint32_t hi);   /* FNV-1a 32 over bytes lo*8..hi*8 */
/* index of the program whose image hash matches and has an entry at `addr` (bytes), else -1; check_only programs
 * only when with_check_only */
int rq_vu1_lookup(const uint32_t *prog_hash, uint32_t addr, int *entry, int with_check_only);

#ifdef __cplusplus
}
#endif
