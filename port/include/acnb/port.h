/* Porting layer for decompiled game code (port/src, port/lib): the only way decompiled code touches anything that
 * is PS2-specific. One copy of the decompiled code builds into both programs:
 *   - port/ (hybrid oracle, default): natives run next to the original MIPS code in the interpreter and are checked
 *     bit-exact against it, so these helpers go through the bridge and mirror PS2 state interpreted code relies on;
 *   - sourceport/ (ACNB_SOURCEPORT): no emulation; the helpers are plain C++ and the oracle-only mirrors vanish.
 * Everything marked "oracle" disappears with the interpreter in phase 4.
 *
 * Rules for decompiled code
 *   - Call through a function pointer from game data with gcall(fp, args...) (typed by fp), never fp(args).
 *     gcall_as<Sig>(fp, ...) where the original passes a different argument list than the DWARF prototype;
 *     gcall_sret(GFN(f), &ret, ...) for a function that returns a struct / union (MW: hidden result pointer in a0).
 *   - Take the address of a game function that is stored in game data or compared with game data via GFN(name).
 *   - PS2_FRAME(n) / PS2_STACK(T, name, off) / PS2_STALE_GPR(r): oracle mirrors of the original stack frame and
 *     registers where original code reads stale (uninitialised) stack or registers.
 *   - PS2_STALE_VF(r, i): lane i of VU0 register vf<r> where a masked VU op (.xyz) leaves the old lane in a result
 *     the original stores; PS2_VF_MIRROR(r, v) keeps vf<r> = v after a native replaced a full-width VU write that
 *     such stale lanes elsewhere depend on.
 *   - VU0 macro-mode registers that inline asm passes between functions: VU0()->vf[i] / acc / q.
 *   - PS2_NULL_MEM(): what the original reads through a null pointer (PS2 address 0).
 *   - PS2PTR(p): a pointer as the 32-bit word the PS2 stores in packets; HOSTPTR(p): a pointer loaded from game data
 *     (may be an uncached RAM mirror on the PS2). */
#pragma once
#include <stdint.h>
#include <string.h>
#include <type_traits>

#ifndef ACNB_SOURCEPORT
#include "bridge.h"
#endif

#ifndef PS2PTR
#define PS2PTR(p) ((unsigned int)(uintptr_t)(p))
#endif

/* PS2_DMA_ADDR(p, ps2): the DMA-side address of a packet at p (a kick's MADR / TADR, a REF tag's ADDR). Hybrid: `ps2`,
 * the original's conversion of PS2PTR(p) (scratchpad flag bit 31, physical masks). Source port: the host pointer as
 * is - the render module is built with RQ_HOST_ADDR, and a masked / flagged host
 * pointer would only survive while host buffers sit below 0x10000000 (phase 2 step 4). */
#ifdef ACNB_SOURCEPORT
#define PS2_DMA_ADDR(p, ps2) PS2PTR(p)
#else
#define PS2_DMA_ADDR(p, ps2) (ps2)
#endif

#ifdef ACNB_SOURCEPORT

#define HOSTPTR(p) ((void *)(p))
#define GFN(name) (&name)

template <class R, class... P, class... A> inline R gcall(R (*fp)(P...), A... args) { return fp((P)args...); }

#define PS2_FRAME(n) ((void)0)
/* EE sp is 16-aligned, so every original stack buffer is too; the quadword helpers (src/lib/func.cpp QALIGN) round
 * down like sq, so an unaligned host local would make them write below the buffer (B7 boot crash: ZeroMemory8b zeroed
 * loader_LoadInstallInfo's return slot when cReadBuf % 16 == 0xc) */
#define PS2_STACK(T, name, off) alignas(16) std::type_identity_t<T> name {}
#define PS2_STALE_GPR(r) 0u
#define PS2_STALE_FPR(r) 0.0f
#define PS2_SAVED_GPR(r, off) ((void)0)
/* lq source address: the oracle aligns it down to 16 bytes like the EE; the source port copies from p itself
 * (its host locals have no PS2 alignment, and every original lq source was 16-aligned) */
#define PS2_LQ_SRC(p) ((const void *)(p))
#define PS2_STALE_VF(r, i) 0.0f
#define PS2_VF_MIRROR(r, v) ((void)0)
#define PS2_ACC_MIRROR(v) ((void)0)
#define PS2_Q_MIRROR(q) ((void)0)

/* VU0 macro-mode register file shared by inline-asm helpers of the original code */
union ps2_qword { uint64_t ud[2]; int64_t sd[2]; uint32_t uw[4]; int32_t sw[4]; float f[4]; };
struct Vu0MacroRegs { ps2_qword vf[32]; float acc[4]; float q; };
inline Vu0MacroRegs g_vu0_macro = {{{.f = {0.0f, 0.0f, 0.0f, 1.0f}}}};
#define VU0() (&g_vu0_macro)

/* scratchpad RAM (16 KB) that the original uses as fast work memory */
alignas(64) inline unsigned char g_ps2_spr[0x4000];
#define PS2_SPR(off) ((void *)(g_ps2_spr + (off)))

/* what the original reads through a null pointer (PS2 address 0, kernel RAM): zeros here */
alignas(16) inline const unsigned char g_ps2_null_mem[256] = {};
#define PS2_NULL_MEM() ((const void *)g_ps2_null_mem)

/* waits for DMA channels that the PS2 build kicks packets on (render detour: nothing to wait for) */
static inline void ps2_dma_wait_vif1(void) {}
static inline void ps2_dma_wait_gif(void) {}
static inline void ps2_gif_wait_idle(void) {}

/* scratchpad DMA (lib/func.c DmaMemCopy): qwc quadwords main memory -> scratchpad offset 0 and back (DMA ignores the
 * low 4 address bits) */
static inline void ps2_spr_dma_to(unsigned int madr, unsigned int qwc)
{
    memmove(g_ps2_spr, (void *)(uintptr_t)(madr & ~15u), (size_t)qwc * 16);
}
static inline void ps2_spr_dma_from(unsigned int madr, unsigned int qwc)
{
    memmove((void *)(uintptr_t)(madr & ~15u), g_ps2_spr, (size_t)qwc * 16);
}

/* EE timer 0 (lib/timer.c): the virtual EE clock of port/platform/kern/ps2_clock (boot slice B3, survey 2.4). EE time
 * moves only at vblank steps (ps2_vsync_wait / kernel idle), as the hybrid at CYC_PER_INSN 0, so get_timer0 has the
 * hybrid's value sequence; host time never reaches game code. The compare interrupt (INTC 9) reaches timer0_handler
 * through the native kernel's handler table. */
extern "C" unsigned ps2c_timer0_read(unsigned reg);
extern "C" void ps2c_timer0_write(unsigned reg, unsigned v);
extern "C" unsigned ps2c_gs_field(void);
static inline unsigned int ps2_timer0_count(void) { return ps2c_timer0_read(0x00); }
static inline void ps2_timer0_set_count(unsigned int v) { ps2c_timer0_write(0x00, v); }
static inline unsigned int ps2_timer0_mode(void) { return ps2c_timer0_read(0x10); }
static inline void ps2_timer0_set_mode(unsigned int v) { ps2c_timer0_write(0x10, v); }
static inline void ps2_timer0_set_target(unsigned int v) { ps2c_timer0_write(0x20, v); }
/* EE timer 1 (src/link/ilink.c clear_timer1 / get_timer1: the modem link's rate limiter): not modelled in the source
 * port - only the link back ends use it (init_timer1 / get_timer1 are unreached offline), so the COUNT write is dropped */
static inline void ps2_timer1_set_count(unsigned int v) { (void)v; }
static inline void ps2_ei(void) {}

#else /* hybrid oracle */

#define GFN(name) ((decltype(&name))(uintptr_t)PS2ADDR_##name)

/* argument marshalling by parameter type: float -> $f12.., 64-bit -> full GPR, everything else sign-extended 32 */
template <class T> inline void gcall_arg_(u64 *ia, int &ni, float *fa, int &nf, T v)
{
    if constexpr (std::is_same_v<T, float>) fa[nf++] = v;
    else if constexpr (std::is_pointer_v<T>) ia[ni++] = BR_I((u32)(uintptr_t)v);
    else if constexpr (sizeof(T) == 8) ia[ni++] = (u64)v;
    else ia[ni++] = BR_I(v);
}

/* Calls the PS2 code fp points to (the interpreter dispatches to a registered native itself). */
template <class R, class... P, class... A> inline R gcall(R (*fp)(P...), A... args)
{
    static_assert(sizeof...(P) == sizeof...(A), "gcall: argument count differs from the function pointer type");
    u64 ia[sizeof...(P) + 1];
    float fa[sizeof...(P) + 1], fr = 0;
    int ni = 0, nf = 0;
    (gcall_arg_<P>(ia, ni, fa, nf, (P)args), ...);
    u64 r = bridge_call((u32)(uintptr_t)fp, -1, ia, ni, fa, nf, std::is_same_v<R, float> ? &fr : nullptr);
    if constexpr (std::is_void_v<R>) return;
    else if constexpr (std::is_same_v<R, float>) return fr;
    else if constexpr (std::is_pointer_v<R>) return (R)bridge_ptr((u32)r);
    else if constexpr (sizeof(R) == 8) return (R)r;
    else return (R)(s32)r;
}

#define PS2_FRAME(n) unsigned int ps2_frame_ __attribute__((cleanup(ps2_frame_pop_))) = (n); ee_cur->gpr[29 /* sp */].uw[0] -= (n)
static inline void ps2_frame_pop_(unsigned int *n) { ee_cur->gpr[29 /* sp */].uw[0] += *n; }
extern "C" int ps2_stack_log;   /* ACNB_STACK_LOG=1 (memtrace.c): log PS2_STACK locals inside the ACNB_STALE_WATCH range */
extern "C" void ps2_stack_note(const char *fn, unsigned int addr, unsigned int size);
#define PS2_STACK(T, name, off) std::type_identity_t<T> &name = *(std::type_identity_t<T> *)(uintptr_t)(ee_cur->gpr[29 /* sp */].uw[0] + (off));     if (ps2_stack_log) ps2_stack_note(__func__, ee_cur->gpr[29].uw[0] + (off), sizeof(std::type_identity_t<T>))
#define PS2_STALE_GPR(r) (ee_cur->gpr[r].uw[0])
/* a callee-saved FPR the original reads before writing it = the caller's value (natives leave the EE FPRs alone) */
#define PS2_STALE_FPR(r) (ee_cur->fpr[r].f)
/* the original prologue's `sq r, off(sp)` of a callee-saved GPR (all 128 bits), for a stale slot a later sibling
 * callee reads; no user now (DF_SetTestRegister s6 -> NoSemi vertex 0 Z proved unneeded, s75 g8b) */
#define PS2_SAVED_GPR(r, off) memcpy((void *)(uintptr_t)(ee_cur->gpr[29 /* sp */].uw[0] + (off)), &ee_cur->gpr[r], 16)
/* lq / sq ignore the low 4 address bits. Only PS2 memory (identity-mapped RAM / scratchpad) is masked: a host local
 * standing in for an original (16-aligned) stack object is read as is - masking an unaligned host address read the
 * wrong bytes */
static inline const void *ps2_lq_src_(const void *p)
{
    uintptr_t a = (uintptr_t)p;
    if (a < 0x02000000u || a - 0x70000000u < 0x4000u) a &= ~(uintptr_t)15;
    return (const void *)a;
}
#define PS2_LQ_SRC(p) ps2_lq_src_((const void *)(p))
#define PS2_STALE_VF(r, i) (ee_cur->vu0->vf[r].f[i])
#define PS2_VF_MIRROR(r, v) memcpy(&ee_cur->vu0->vf[r], (v), 16)
#define PS2_ACC_MIRROR(v) memcpy(ee_cur->vu0->acc, (v), 16)
#define PS2_Q_MIRROR(x) (ee_cur->vu0->q = (x))

#define VU0() (ee_cur->vu0)
#define PS2_SPR(off) ((void *)(uintptr_t)(0x70000000u + (off)))   /* identity-mapped scratchpad */
#define PS2_NULL_MEM() ((const void *)mem_ptr(0))                    /* oracle: the emulated PS2 address 0 */

static inline void ps2_dma_wait_vif1(void) { while (rd32(0x10009000u) & 0x100) {} }   /* D1_CHCR.STR */
static inline void ps2_dma_wait_gif(void) { while (rd32(0x1000a000u) & 0x100) {} }    /* D2_CHCR.STR */
/* GIF DMA done and GIF_STAT.APATH idle (the original's busy-wait after a direct GIF kick) */
static inline void ps2_gif_wait_idle(void) { do ps2_dma_wait_gif(); while (rd32(0x10003020u) & 0xc00); }

/* scratchpad DMA (lib/func.c DmaMemCopy): the original's DMAC register sequence, so the emulated DMAC copies (address
 * masking, SPR wrap) and leaves D_PCR / D_STAT / the channel registers as the original does */
static inline void ps2_spr_dma_(unsigned int ch_base, unsigned int bit, unsigned int chcr, unsigned int madr,
                                unsigned int qwc)
{
    wr32(0x1000e020u, 0xfffffc00u);        /* D_PCR */
    wr32(0x1000e010u, bit);                /* D_STAT: clear the channel's CIS bit */
    wr32(ch_base + 0x10u, madr);           /* MADR */
    wr32(ch_base + 0x20u, qwc);            /* QWC */
    wr32(ch_base + 0x80u, 0x70000000u);    /* SADR: scratchpad offset 0 */
    wr32(0x1000e020u, rd32(0x1000e020u) | bit);
    wr32(ch_base, chcr);                   /* CHCR: start */
    while (rd32(ch_base) & 0x100) {}       /* the original's bc0f wait on the CPCOND */
}
static inline void ps2_spr_dma_to(unsigned int madr, unsigned int qwc) { ps2_spr_dma_(0x1000d400u, 0x200, 0x101, madr, qwc); }
static inline void ps2_spr_dma_from(unsigned int madr, unsigned int qwc) { ps2_spr_dma_(0x1000d000u, 0x100, 0x100, madr, qwc); }

/* EE timer 0 (lib/timer.c: COUNT, MODE, COMP) and the EE `ei` of its interrupt handler */
static inline unsigned int ps2_timer0_count(void) { return rd32(0x10000000u); }
static inline void ps2_timer0_set_count(unsigned int v) { wr32(0x10000000u, v); }
static inline unsigned int ps2_timer0_mode(void) { return rd32(0x10000010u); }
static inline void ps2_timer0_set_mode(unsigned int v) { wr32(0x10000010u, v); }
static inline void ps2_timer0_set_target(unsigned int v) { wr32(0x10000020u, v); }
static inline void ps2_timer1_set_count(unsigned int v) { wr32(0x10000800u, v); }   /* EE timer 1 COUNT */
static inline void ps2_ei(void) { ee_cur->cop0[12] |= 0x10000; }   /* Status.EIE */

#endif

/* Render module tags: debug / phase 4 labels for what the GS draws. Host
 * only - never touch PS2 memory or game state (bcheck-neutral); no-ops when the module is off (ACNB_RENDER=gs).
 *   ps2_rq_tag_packet(p, kind, objid, sub): label the DMA packet starting at p (a chain tag qword, a normal-mode MADR
 *     or a REF target) until the walker meets the next tagged address; kind RQ_K_NONE there ends the run. Valid in
 *     the frame attached and the next.
 *   PS2_RQ_SCOPE(kind, objid, sub): ambient tag for the rest of the C++ scope (direct kicks: SPR / GS lists / menus);
 *     ps2_rq_tag_scope / ps2_rq_tag_end where a scope does not match a C++ block. objid = RQ_OBJID(RQ_OC_*, inst). */
#include "../../render/rq.h"
extern "C" void acnb_rq_tag(int op, unsigned int addr, unsigned int kind, unsigned int sub, unsigned long long objid);
static inline void ps2_rq_tag_packet(const void *p, unsigned int kind, unsigned long long objid, unsigned int sub)
{
    acnb_rq_tag(0, PS2PTR(p), kind, sub, objid);
}
static inline void ps2_rq_tag_scope(unsigned int kind, unsigned long long objid, unsigned int sub)
{
    acnb_rq_tag(1, 0, kind, sub, objid);
}
static inline void ps2_rq_tag_end(void) { acnb_rq_tag(2, 0, 0, 0, 0); }

/* Kick stand-ins for natives that start a DMA channel or write a GS privileged
 * register. Hybrid: the original register sequence into the emulated DMAC / GS (reference + module through dma.c /
 * hw.c); a caller whose original also clears D_STAT or sets D_PCR does that itself. Source port: straight into the
 * module (events ignored: nothing waits on them; ps2_dma_wait_* / sceGsSyncPath are no-ops there). */
#ifdef ACNB_SOURCEPORT
extern "C" Rq *acnb_rq(void);   /* sourceport/main/host_rq.cpp */
static inline void ps2_dma_kick(int ch, unsigned int chcr, unsigned int madr, unsigned int qwc, unsigned int tadr)
{
    (void)rq_dma_kick(acnb_rq(), ch, chcr, madr, qwc, tadr);
}
static inline void ps2_dma_kick_notadr(int ch, unsigned int chcr, unsigned int madr, unsigned int qwc)
{
    (void)rq_dma_kick(acnb_rq(), ch, chcr, madr, qwc, 0);   /* chain with QWC > 0 and tag id 0: TADR never read */
}
static inline void ps2_dma_kick_tadr(int ch, unsigned int chcr, unsigned int tadr)
{
    (void)rq_dma_kick(acnb_rq(), ch, chcr, 0, 0, tadr);
}
static inline void ps2_dma_kick_tadr_madr(int ch, unsigned int chcr, unsigned int tadr, unsigned int madr)
{
    (void)rq_dma_kick(acnb_rq(), ch, chcr, madr, 0, tadr);
}
static inline void ps2_dma_kick_qmt(int ch, unsigned int chcr, unsigned int madr, unsigned int qwc, unsigned int tadr)
{
    (void)rq_dma_kick(acnb_rq(), ch, chcr, madr, qwc, tadr);
}
static inline void ps2_gs_priv_write64(unsigned int addr, unsigned long long v) { rq_priv_write(acnb_rq(), addr, 8, &v); }
/* SDK sceDmaChan register words (CHCR +0, MADR +0x10, QWC +0x20, TADR +0x30) of the ten channels (0x10008000 ..
 * 0x1000d400, slot = (addr - 0x10008000) >> 10): no device, but the game reads back what it wrote - DF_SendPGVU1 /
 * DF_DMA_Send / MDraw_BitBlt set CHCR.TTE with a byte read-modify-write and sceDmaSend keeps it (s99: reading 0 here
 * dropped TTE, so the MPG codes in the boot microcode chains' tags never reached VU1: g_pmpgVu1Shadow at 0x600 missing,
 * the garage AC's shadow empty). The start runs the transfer to completion (STR stays clear). lib/sdk/libdma.cpp. */
extern "C" unsigned int sdk_dma_chan_reg[24][4];
static inline unsigned int *sp_dma_chan_reg(const void *ch, unsigned int off)
{
    return &sdk_dma_chan_reg[((PS2PTR(ch) - 0x10008000u) >> 10) % 24][(off >> 4) & 3];
}
static inline unsigned int ps2_dma_chan_rd8(const void *ch) { return *sp_dma_chan_reg(ch, 0) & 0xff; }
static inline void ps2_dma_chan_wr8(void *ch, unsigned int v) { unsigned int *r = sp_dma_chan_reg(ch, 0); *r = (*r & ~0xffu) | (v & 0xff); }
static inline unsigned int ps2_dma_chan_rd32(const void *ch, unsigned int off) { return *sp_dma_chan_reg(ch, off); }
static inline void ps2_dma_chan_wr32(void *ch, unsigned int off, unsigned int v) { *sp_dma_chan_reg(ch, off) = v; }
static inline void ps2_dma_chan_wait(const void *ch) { (void)ch; }
static inline void ps2_dma_chan_start(void *ch, unsigned int chcr, unsigned int madr, unsigned int qwc, unsigned int tadr)
{
    *sp_dma_chan_reg(ch, 0) = chcr & ~0x100u;
    (void)rq_dma_kick(acnb_rq(), (int)((PS2PTR(ch) - 0x10008000u) >> 12), chcr, madr, qwc, tadr);   /* 1 VIF1, 2 GIF */
}
static inline unsigned int ps2_gs_path_busy(void) { return 0; }
/* GS readback (sceGsExecStoreImage) / vsync stand-ins (lib/sdk/libgraph.cpp). The source port has no GsPutIMR
 * kernel state, no VIF1_STAT / FIFO registers (the readback pixels come through rq_dma_kick ch 1 DIR 0
 * deliveries). ps2_vsync_wait (sourceport/main/host_vsync.cpp) is the vblank step: EE time to the next vblank,
 * sound host, CSR.FIELD toggle, present / pace, pad poll, timer interrupts, exit-after; it returns what the
 * original's wait returns (always 0: its EIntr result after DIntr). ps2_gs_field is CSR.FIELD (ps2_clock). */
static inline unsigned int ps2_gs_imr_get(void) { return 0; }
static inline unsigned int ps2_gs_imr_put(unsigned int v) { (void)v; return 0; }
static inline void ps2_vif1_set_stat(unsigned int v) { (void)v; }
static inline void ps2_vif1_fifo_write(const void *q16) { rq_fifo_write(acnb_rq(), 1, (const uint32_t *)q16); }
extern "C" int ps2_vsync_wait(void);
static inline unsigned int ps2_gs_field(void) { return ps2c_gs_field(); }
/* bring-up stand-ins (lib/sdk sceGsResetGraph / ResetPath / sceDmaReset / PutEnv): GS CSR read (no GS: revision 0x1b,
 * ID 0x55, FINISH, FIELD like ps2_gs_field), DMAC global registers D_CTRL 0xe000 .. D_RBOR 0xe050 (off = the low 16
 * bits; no DMAC: reads 0), VIF1_FBRST / VIF1_ERR / GIF_CTRL (no paths to reset) */
static inline unsigned long long ps2_gs_csr(void)
{
    return (0x55ull << 24) | (0x1bull << 16) | ((unsigned long long)ps2_gs_field() << 13) | 2;
}
static inline unsigned int ps2_dmac_rd32(unsigned int off) { (void)off; return 0; }
static inline void ps2_dmac_wr32(unsigned int off, unsigned int v) { (void)off; (void)v; }
static inline void ps2_vif1_fbrst(unsigned int v) { (void)v; }
static inline void ps2_vif1_err(unsigned int v) { (void)v; }
static inline void ps2_gif_ctrl(unsigned int v) { (void)v; }
#else
static inline void ps2_dma_kick(int ch, unsigned int chcr, unsigned int madr, unsigned int qwc, unsigned int tadr)
{
    unsigned int base = ch == 1 ? 0x10009000u : 0x1000a000u;   /* D1 VIF1 / D2 GIF */
    wr32(base + 0x10u, madr);   /* MADR */
    wr32(base + 0x20u, qwc);    /* QWC */
    wr32(base + 0x30u, tadr);   /* TADR */
    wr32(base, chcr);           /* CHCR (STR): starts the transfer */
}
/* a kick that leaves TADR as it is (gfx/draw.c GifPath3KickSPR / KickMem2: chain mode, CHCR tag id 0 = REFE, so
 * the DMAC sends QWC qwords from MADR and stops without reading a tag); the original's store order QWC, MADR, CHCR */
static inline void ps2_dma_kick_notadr(int ch, unsigned int chcr, unsigned int madr, unsigned int qwc)
{
    unsigned int base = ch == 1 ? 0x10009000u : 0x1000a000u;
    wr32(base + 0x20u, qwc);    /* QWC */
    wr32(base + 0x10u, madr);   /* MADR */
    wr32(base, chcr);           /* CHCR (STR) */
}
/* chain-mode kicks that write only TADR (and MADR) and then CHCR, in the original store order (menu/mpkt.c
 * DF_MPKT_Kick: TADR, MADR = -1, CHCR; KickImmediate: TADR, CHCR; menu.c __MenuMakePacket2ND like Kick). QWC keeps
 * what the last transfer left (0 after a finished chain), so the DMAC reads the first tag at TADR. */
static inline void ps2_dma_kick_tadr(int ch, unsigned int chcr, unsigned int tadr)
{
    unsigned int base = ch == 1 ? 0x10009000u : 0x1000a000u;
    wr32(base + 0x30u, tadr);   /* TADR */
    wr32(base, chcr);           /* CHCR (STR) */
}
static inline void ps2_dma_kick_tadr_madr(int ch, unsigned int chcr, unsigned int tadr, unsigned int madr)
{
    unsigned int base = ch == 1 ? 0x10009000u : 0x1000a000u;
    wr32(base + 0x30u, tadr);   /* TADR */
    wr32(base + 0x10u, madr);   /* MADR */
    wr32(base, chcr);           /* CHCR (STR) */
}
/* a full chain kick in the store order QWC, MADR, TADR, CHCR (gfx/draw.c Draw_KickDmaDl: QWC 0, MADR 0, TADR = the
 * battle display list; ps2_dma_kick stores MADR first) */
static inline void ps2_dma_kick_qmt(int ch, unsigned int chcr, unsigned int madr, unsigned int qwc, unsigned int tadr)
{
    unsigned int base = ch == 1 ? 0x10009000u : 0x1000a000u;
    wr32(base + 0x20u, qwc);    /* QWC */
    wr32(base + 0x10u, madr);   /* MADR */
    wr32(base + 0x30u, tadr);   /* TADR */
    wr32(base, chcr);           /* CHCR (STR) */
}
static inline void ps2_gs_priv_write64(unsigned int addr, unsigned long long v) { wr64(addr, v); }
/* byte 0 of an SDK sceDmaChan (sceDmaGetChan: the channel's CHCR register; MDraw_BitBlt toggles bit 6 = TTE):
 * an 8-bit hardware register access (source port: no channel, reads 0) */
static inline unsigned int ps2_dma_chan_rd8(const void *ch) { return rd8(PS2PTR(ch)); }
static inline void ps2_dma_chan_wr8(void *ch, unsigned int v) { wr8(PS2PTR(ch), (u8)v); }
/* 32-bit registers of an SDK sceDmaChan (libdma.cpp: CHCR +0, MADR +0x10, QWC +0x20, TADR +0x30; source port: no
 * registers, reads 0), the wait for CHCR.STR clear, and the start: hybrid = the CHCR store (the caller has written
 * MADR / QWC / TADR itself, in the original order); source port = the render module kick with those values */
static inline unsigned int ps2_dma_chan_rd32(const void *ch, unsigned int off) { return rd32(PS2PTR(ch) + off); }
static inline void ps2_dma_chan_wr32(void *ch, unsigned int off, unsigned int v) { wr32(PS2PTR(ch) + off, v); }
static inline void ps2_dma_chan_wait(const void *ch) { while (rd32(PS2PTR(ch)) & 0x100) {} }
static inline void ps2_dma_chan_start(void *ch, unsigned int chcr, unsigned int madr, unsigned int qwc, unsigned int tadr)
{
    (void)madr; (void)qwc; (void)tadr;
    wr32(PS2PTR(ch), chcr);
}
/* sceGsSyncPath's busy mask: bit 0 D1_CHCR.STR, 1 D2_CHCR.STR, 2 VIF1_STAT FQC / VPS, 3 VPU_STAT VU1 running (the
 * emulator's CFC2 vi29 reads 0), 4 GIF_STAT.APATH (source port: nothing is ever busy) */
static inline unsigned int ps2_gs_path_busy(void)
{
    unsigned int m = 0;
    if (rd32(0x10009000u) & 0x100) m |= 1;
    if (rd32(0x1000a000u) & 0x100) m |= 2;
    if (rd32(0x10003c00u) & 0x1f000003u) m |= 4;
    if (rd32(0x10003020u) & 0xc00) m |= 0x10;
    return m;
}
/* GS readback (sceGsExecStoreImage): GsGetIMR / GsPutIMR (syscalls 0x70 / 0x71: the kernel's gs_imr, PutIMR returns
 * the old mask), GS CSR read (hw.c gs_priv_read: FINISH, vblank fast-forward every 8th poll - one call per original
 * `ld`), VIF1_STAT, the VIF1 FIFO (an `lq` / `sq` of 0x10005000 through the same mmio path as interpreted code:
 * rq_fifo_write + vif1_write on writes) and the error-path GIF / VIF1 resets */
extern "C" u32 gs_imr;
static inline unsigned int ps2_gs_imr_get(void) { return gs_imr; }
static inline unsigned int ps2_gs_imr_put(unsigned int v) { unsigned int o = gs_imr; gs_imr = v; return o; }
static inline unsigned long long ps2_gs_csr(void) { return rd64(0x12001000u); }
static inline unsigned int ps2_vif1_stat(void) { return rd32(0x10003c00u); }
static inline void ps2_vif1_set_stat(unsigned int v) { wr32(0x10003c00u, v); }
static inline void ps2_vif1_fifo_read(void *q16) { r128 q; rd128(0x10005000u, &q); memcpy(q16, &q, 16); }
static inline void ps2_vif1_fifo_write(const void *q16) { r128 q; memcpy(&q, q16, 16); wr128(0x10005000u, &q); }
static inline void ps2_gif_vif1_reset(void) { wr32(0x10003000u, 1); wr32(0x10003c10u, 1); }   /* GIF_CTRL, VIF1_FBRST */
/* bring-up stand-ins (lib/sdk sceGsResetPath / sceDmaReset / PutEnv): DMAC global registers 0x1000e000 + off (D_CTRL 0,
 * D_STAT 0x10: hw.c's write-1-clears / toggles semantics, D_PCR 0x20, D_SQWC 0x30, D_RBSR 0x40, D_RBOR 0x50), VIF1_FBRST,
 * VIF1_ERR, GIF_CTRL - one emulated register access each, in the caller's order */
static inline unsigned int ps2_dmac_rd32(unsigned int off) { return rd32(0x1000e000u + off); }
static inline void ps2_dmac_wr32(unsigned int off, unsigned int v) { wr32(0x1000e000u + off, v); }
static inline void ps2_vif1_fbrst(unsigned int v) { wr32(0x10003c10u, v); }
static inline void ps2_vif1_err(unsigned int v) { wr32(0x10003c20u, v); }
static inline void ps2_gif_ctrl(unsigned int v) { wr32(0x10003000u, v); }
#endif
struct ps2_rq_scope_ {
    ps2_rq_scope_(unsigned int kind, unsigned long long objid, unsigned int sub) { ps2_rq_tag_scope(kind, objid, sub); }
    ~ps2_rq_scope_() { ps2_rq_tag_end(); }
};
#define PS2_RQ_SCOPE_CAT2_(a, b) a##b
#define PS2_RQ_SCOPE_CAT_(a, b) PS2_RQ_SCOPE_CAT2_(a, b)
#define PS2_RQ_SCOPE(kind, objid, sub) ps2_rq_scope_ PS2_RQ_SCOPE_CAT_(ps2_rq_scope_, __LINE__)((kind), (objid), (sub))

/* Calls fp with the argument list Sig (the original passes more arguments than the DWARF prototype says). */
template <class Sig, class F, class... A> inline auto gcall_as(F fp, A... args)
{
    return gcall((Sig *)(uintptr_t)fp, args...);
}

/* Calls fp, which returns an aggregate (struct / union): MW's ABI passes the result address as a hidden first argument
 * (a0). In the hybrid ret must point into PS2 memory (the original's PS2_STACK slot). */
template <class R, class... P, class... A> inline void gcall_sret(R (*fp)(P...), R *ret, A... args)
{
#ifdef ACNB_SOURCEPORT
    *ret = fp((P)args...);
#else
    gcall((void (*)(R *, P...))(uintptr_t)fp, ret, (P)args...);
#endif
}
