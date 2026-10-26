/* sd: native LIBSD (Sony sound device library, IOP) - see sd.h. Decompiled from the IRX (IRX.BIN@02d800, crc b9080047);
 * function comments give the original's module offset / map label.
 * Group P1 native: register API, Note2Pitch / Pitch2Note, SetCoreAttr / GetCoreAttr (+ SPDIF / bit helpers), handler
 * setters, the IRQ-9 handler, Init and its helpers, Quit. P2 native: DMA channel control (f_2168 + case bodies),
 * VoiceTrans / VoiceTransStatus / StopTrans, BlockTrans / BlockTransStatus (+ AutoDMA write / read set-up), the data-port
 * write, the DMA-end ISR. P3 native: reverb effects (SetEffectAttr / GetEffectAttr / SetEffectMode / SetEffectModeParams,
 * the preset writer f_013c, Clear / CleanEffectWorkArea + the latter's DMA chain f_0888). P4 native: ProcBatch /
 * ProcBatchEx (command lists; the SIF DMA helper f_3000 goes through Kernel::SifSetDma / SifDmaStat).
 * The original's delay loops (f_2120 / f_20c4) and write barrier (f_1e60) have no visible effect and are not mirrored;
 * its IOP-side register stores (DPCR, DMA timing / base registers in f_1550 / f_206c / f_2094) neither. The IOP DMA
 * registers the transfers use (MADR / BCR / CHCR) are a shadow (R.dma). */
#include "sd.h"
#include "../mixer.h"
#include <string.h>

namespace sd {

/* ---------------------------------------------------------------- module state (the original's .data / .bss) */

struct Cookie { uint32_t state; void *arg; };   /* D_052f8: ISR cookie per core {state word, trans handler arg} */

static struct State {
    uint32_t cpu_write_done[2];   /* D_05154 */
    uint32_t trans_idle[2];       /* D_0515c (init 1) */
    uint32_t spdif;               /* D_05290: last SPDIF value; its high half is the attr-bit table's entry 0 */
    uint32_t effect[10];          /* D_052a0: effect state, 5 words per core: [1] mode (D_052a4), [3] delay (D_052ac),
                                   * [4] feedback (D_052b0); word 0's low half = attr-bit entry 0xe (never written) */
    uint32_t done_unack[2];       /* D_052c8 */
    int32_t evf[2];               /* D_052d0 event flag ids */
    uint32_t inited;              /* D_052d8 */
    Callback irq_cb;              /* D_052dc */
    IntrHandler spu2_handler;     /* D_052e0 */
    void *spu2_arg;               /* D_052e4 */
    IntrHandler trans_handler[2]; /* D_052e8 */
    IntrHandler post_hook[2];     /* D_052f0 (never set in the executed code) */
    Cookie cookie[2];             /* D_052f8 (init {0, 0}, {1, 0}) */
    int32_t bt_side[2];           /* B_05940 */
    uint32_t bt_base[2];          /* B_05948 */
    uint32_t bt_half[2];          /* B_05950 */
    Callback trans_cb[2];         /* B_05970 */
    uint32_t esa_start[2];        /* B_05978: reverb work area start (byte address) */
    int32_t clean_n[2];           /* B_05320: CleanEffectWorkArea span count */
    int32_t clean_i[2];           /* B_05328: index of the span f_0888 starts next */
    struct { uint32_t addr, size; } clean_span[2][97];   /* B_05330: {SPU byte address, bytes} (0x308 bytes per channel) */
    uint32_t batch_word;          /* B_05960: ProcBatch command 6's DMA source word */
} S;

/* register-file words the mixer does not keep (the KON / KOFF latches, 0x1ae, the misc block 0x760-0x7fe incl. 0x7c0 /
 * 0x7c6 / 0x7c8 / 0x7ca); everything else is read back from the mixer (mixer.h: no register file) */
struct DmaRegs { uint32_t madr, bcr, chcr; };   /* IOP DMA ch4 / ch7 (see dma_chcr) */
static struct Shadow {
    uint32_t kon[2], koff[2];
    uint16_t r1ae[2], misc[0x50];
    DmaRegs dma[2];
} R;

static Kernel K;
static Hw H;

void Attach(const Kernel *k, const Hw *hw) { K = *k; H = *hw; }

void ResetModuleState()
{
    memset(&S, 0, sizeof S);
    memset(&R, 0, sizeof R);
    S.trans_idle[0] = S.trans_idle[1] = 1;
    S.cookie[1].state = 1;
}

/* ---------------------------------------------------------------- SPU2 register layer (shadow + mixer commands) */

static uint32_t set_hi(uint32_t a, uint16_t v) { return ((uint32_t)(v & 0xf) << 16) | (a & 0xffff); }
static uint32_t set_lo(uint32_t a, uint16_t v) { return (a & 0xf0000) | v; }

/* the 0x760 block: core volumes + reverb coefficients */
static int vol_param(uint32_t o)
{
    if (o <= 0x0e && !(o & 1)) return SND_CP_MVOLL + (int)(o / 2);
    if (o >= 0x14 && o < 0x28) return SND_CP_REV_IIR + (int)((o - 0x14) / 2);
    return -1;
}

static void spu_write(uint32_t r, uint16_t val)
{
    SndMixer *m = H.mixer;
    r &= 0x7fe;
    if (r >= 0x760) {
        if (r < 0x7b0) {
            int c = r >= 0x788, p = vol_param(r - (c ? 0x788 : 0x760));
            if (p >= 0) snd_mixer_core_set(m, c, p, val);
            return;
        }
        if (r == 0x7c2) { snd_mixer_irqinfo_set(m, val); return; }
        R.misc[(r - 0x760) / 2] = val;
        return;
    }
    int c = (int)(r >> 10);
    uint32_t o = r & 0x3ff;
    if (o < 0x180) {
        static const int vp[8] = {SND_VP_VOLL, SND_VP_VOLR, SND_VP_PITCH, SND_VP_ADSR1, SND_VP_ADSR2, SND_VP_ENVX, -1, -1};
        int p = vp[(o & 0xf) >> 1];
        if (p >= 0) snd_mixer_voice_set(m, c, (int)(o >> 4), p, val);
        return;
    }
    if (o >= 0x1c0 && o < 0x2e0) {
        int vi = (int)(o - 0x1c0) / 12, f = (int)(o - 0x1c0) % 12, w = f < 4 ? SND_VA_SSA : f < 8 ? SND_VA_LSA : SND_VA_NAX;
        uint32_t cur = snd_mixer_voice_addr_get(m, c, vi, w);
        snd_mixer_voice_addr_set(m, c, vi, w, (f & 2) ? set_lo(cur, val) : set_hi(cur, val));
        return;
    }
    if (o >= 0x2e4 && o < 0x33c) {
        int w = SND_CA_REV0 + (int)(o - 0x2e4) / 4;
        uint32_t cur = snd_mixer_core_addr_get(m, c, w);
        snd_mixer_core_addr_set(m, c, w, (o & 2) ? set_lo(cur, val) : set_hi(cur, val));
        return;
    }
    if (o < 0x198) {   /* PMON, NON, VMIXL, VMIXEL, VMIXR, VMIXER: lo / hi (8 bits) */
        int w = SND_CM_PMON + (int)(o - 0x180) / 4;
        uint32_t cur = snd_mixer_core_mask_get(m, c, w);
        snd_mixer_core_mask_set(m, c, w, (o & 2) ? (cur & 0xffffu) | (uint32_t)(val & 0xff) << 16 : (cur & 0xffff0000u) | val);
        return;
    }
    switch (o) {
    case 0x198: snd_mixer_core_set(m, c, SND_CP_MMIX, val); break;
    case 0x19a: snd_mixer_core_set(m, c, SND_CP_ATTR, val); break;
    case 0x19c: case 0x19e: {
        uint32_t cur = snd_mixer_core_addr_get(m, c, SND_CA_IRQA);
        snd_mixer_core_addr_set(m, c, SND_CA_IRQA, o == 0x19e ? set_lo(cur, val) : set_hi(cur, val));
        break;
    }
    case 0x1a0: R.kon[c] = (R.kon[c] & 0xffff0000u) | val; snd_mixer_key_on(m, c, val); break;
    case 0x1a2: R.kon[c] = (R.kon[c] & 0xffffu) | (uint32_t)(val & 0xff) << 16; snd_mixer_key_on(m, c, (uint32_t)(val & 0xff) << 16); break;
    case 0x1a4: R.koff[c] = (R.koff[c] & 0xffff0000u) | val; snd_mixer_key_off(m, c, val); break;
    case 0x1a6: R.koff[c] = (R.koff[c] & 0xffffu) | (uint32_t)(val & 0xff) << 16; snd_mixer_key_off(m, c, (uint32_t)(val & 0xff) << 16); break;
    case 0x1a8: case 0x1aa: {
        uint32_t cur = snd_mixer_core_addr_get(m, c, SND_CA_TSA);
        snd_mixer_core_addr_set(m, c, SND_CA_TSA, o == 0x1aa ? set_lo(cur, val) : set_hi(cur, val));
        break;
    }
    case 0x1ac: snd_mixer_port_write(m, c, &val, 1); break;
    case 0x1ae: R.r1ae[c] = val; break;
    case 0x1b0: snd_mixer_core_set(m, c, SND_CP_ADMAS, val); break;
    case 0x2e0: case 0x2e2: {
        uint32_t cur = snd_mixer_core_addr_get(m, c, SND_CA_ESA);
        snd_mixer_core_addr_set(m, c, SND_CA_ESA, o == 0x2e2 ? set_lo(cur, val) : set_hi(cur, val));
        break;
    }
    case 0x33c: snd_mixer_core_addr_set(m, c, SND_CA_EEA, ((uint32_t)(val & 0xf) << 16) | 0xffff); break;
    case 0x340: snd_mixer_endx_clear(m, c, val); break;
    case 0x342: snd_mixer_endx_clear(m, c, (uint32_t)val << 16); break;
    }
}

static uint16_t spu_read(uint32_t r)
{
    SndMixer *m = H.mixer;
    r &= 0x7fe;
    if (r >= 0x760) {
        if (r < 0x7b0) {
            int c = r >= 0x788;
            uint32_t o = r - (c ? 0x788 : 0x760);
            if (o == 0x10) return snd_mixer_core_get(m, c, SND_CP_MVOLXL);
            if (o == 0x12) return snd_mixer_core_get(m, c, SND_CP_MVOLXR);
            int p = vol_param(o);
            return p >= 0 ? snd_mixer_core_get(m, c, p) : 0;
        }
        if (r == 0x7c2) return snd_mixer_irqinfo(m);
        return R.misc[(r - 0x760) / 2];
    }
    int c = (int)(r >> 10);
    uint32_t o = r & 0x3ff;
    if (o < 0x180) {
        static const int vp[8] = {SND_VP_VOLL, SND_VP_VOLR, SND_VP_PITCH, SND_VP_ADSR1, SND_VP_ADSR2, SND_VP_ENVX,
                                  SND_VP_VOLXL, SND_VP_VOLXR};
        return snd_mixer_voice_get(m, c, (int)(o >> 4), vp[(o & 0xf) >> 1]);
    }
    if (o >= 0x1c0 && o < 0x2e0) {
        int vi = (int)(o - 0x1c0) / 12, f = (int)(o - 0x1c0) % 12, w = f < 4 ? SND_VA_SSA : f < 8 ? SND_VA_LSA : SND_VA_NAX;
        uint32_t v = snd_mixer_voice_addr_get(m, c, vi, w);
        return (f & 2) ? (uint16_t)v : (uint16_t)(v >> 16);
    }
    if (o >= 0x2e4 && o < 0x33c) {
        uint32_t v = snd_mixer_core_addr_get(m, c, SND_CA_REV0 + (int)(o - 0x2e4) / 4);
        return (o & 2) ? (uint16_t)v : (uint16_t)(v >> 16);
    }
    if (o < 0x198) {
        uint32_t v = snd_mixer_core_mask_get(m, c, SND_CM_PMON + (int)(o - 0x180) / 4);
        return (o & 2) ? (uint16_t)(v >> 16) : (uint16_t)v;
    }
    switch (o) {
    case 0x198: return snd_mixer_core_get(m, c, SND_CP_MMIX);
    case 0x19a: return snd_mixer_core_get(m, c, SND_CP_ATTR);
    case 0x19c: return (uint16_t)(snd_mixer_core_addr_get(m, c, SND_CA_IRQA) >> 16);
    case 0x19e: return (uint16_t)snd_mixer_core_addr_get(m, c, SND_CA_IRQA);
    case 0x1a0: return (uint16_t)R.kon[c];
    case 0x1a2: return (uint16_t)(R.kon[c] >> 16);
    case 0x1a4: return (uint16_t)R.koff[c];
    case 0x1a6: return (uint16_t)(R.koff[c] >> 16);
    case 0x1a8: return (uint16_t)(snd_mixer_core_addr_get(m, c, SND_CA_TSA) >> 16);
    case 0x1aa: return (uint16_t)snd_mixer_core_addr_get(m, c, SND_CA_TSA);
    case 0x1ac: return snd_mixer_port_read(m, c);
    case 0x1ae: return R.r1ae[c];
    case 0x1b0: return snd_mixer_core_get(m, c, SND_CP_ADMAS);
    case 0x2e0: return (uint16_t)(snd_mixer_core_addr_get(m, c, SND_CA_ESA) >> 16);
    case 0x2e2: return (uint16_t)snd_mixer_core_addr_get(m, c, SND_CA_ESA);
    case 0x33c: return (uint16_t)(snd_mixer_core_addr_get(m, c, SND_CA_EEA) >> 16);
    case 0x340: return (uint16_t)snd_mixer_endx(m, c);
    case 0x342: return (uint16_t)(snd_mixer_endx(m, c) >> 16);
    case 0x344: return snd_mixer_core_get(m, c, SND_CP_STATX);
    }
    return 0;
}

void RegWrite(uint32_t reg, uint16_t v)
{
    if (H.sync) H.sync(H.user);
    if (H.pre_write) H.pre_write(H.user, reg & 0x7fe, v);
    spu_write(reg, v);
    if (H.trace) H.trace(H.user, 'W', reg & 0x7fe, v);
}

uint16_t RegRead(uint32_t reg)
{
    if (H.sync) H.sync(H.user);
    uint16_t v = spu_read(reg);
    if (H.trace) H.trace(H.user, 'R', reg & 0x7fe, v);
    return v;
}

/* shorthands for the original's halfword stores / loads */
static void W(uint32_t reg, uint16_t v) { RegWrite(reg, v); }
static uint16_t Rd(uint32_t reg) { return RegRead(reg); }

/* ---------------------------------------------------------------- entry-code register table (D_04950, 41 entries) */

static const uint16_t reg_tbl[0x29] = {
    0x000, 0x002, 0x004, 0x006, 0x008, 0x00a, 0x00c, 0x00e,   /* 00-07 VOLL VOLR PITCH ADSR1 ADSR2 ENVX VOLXL VOLXR */
    0x198, 0x760, 0x762, 0x764, 0x766, 0x768, 0x76a, 0x76c,   /* 08 MMIX, 09-0f MVOLL MVOLR EVOLL EVOLR AVOLL AVOLR BVOLL */
    0x76e, 0x770, 0x772, 0x180, 0x184, 0x1a0, 0x1a4, 0x340,   /* 10 BVOLR 11-12 MVOLXL/R 13 PMON 14 NON 15 KON 16 KOFF 17 ENDX */
    0x188, 0x18c, 0x190, 0x194, 0x2e0, 0x33c, 0x1a8, 0x19c,   /* 18-1b VMIXL VMIXEL VMIXR VMIXER 1c ESA 1d EEA 1e TSA 1f IRQA */
    0x1c0, 0x1c4, 0x1c8, 0x19a, 0x1a8, 0x1ac, 0x1ae, 0x1b0,   /* 20 SSA 21 LSAX 22 NAX 23 ATTR 24 TSA 25 DATA 26 1ae 27 ADMAS */
    0x344,                                                     /* 28 STATX */
};
/* Q4: the original does not range-check idx (> 0x28 reads the following rodata words as register pointers; never hit):
 * natively such an entry has no register (stores dropped, loads 0) */
static int reg_of(uint32_t entry, uint32_t *reg)
{
    uint32_t idx = (entry >> 8) & 0xff;
    if (idx >= 0x29) return 0;
    *reg = reg_tbl[idx];
    return 1;
}

/* 0x3660 */
void SetParam(uint16_t entry, uint16_t value)
{
    uint32_t r;
    if (!reg_of(entry, &r)) return;
    W(r + (entry & 1) * ((entry & 0x80) ? 0x28u : 0x400u) + (entry & 0x3e) * 8u, value);
}

/* 0x36bc */
uint16_t GetParam(uint16_t entry)
{
    uint32_t r;
    if (!reg_of(entry, &r)) return 0;
    return Rd(r + (entry & 1) * ((entry & 0x80) ? 0x28u : 0x400u) + (entry & 0x3e) * 8u);
}

/* 0x371c: lo half, then bits 16-23 at +2 */
void SetSwitch(uint16_t entry, uint32_t value)
{
    uint32_t r;
    if (!reg_of(entry, &r)) return;
    r += (entry & 1) * 0x400u;
    W(r, (uint16_t)value);
    W(r + 2, (uint16_t)((value >> 16) & 0xff));
}

/* 0x3754: Q5 the hi register is not masked to 8 bits */
uint32_t GetSwitch(uint16_t entry)
{
    uint32_t r;
    if (!reg_of(entry, &r)) return 0;
    r += (entry & 1) * 0x400u;
    uint32_t lo = Rd(r);
    uint32_t hi = Rd(r + 2);
    return lo | hi << 16;
}

/* 0x378c: hi = a >> 17, then (not for EEA) lo = (a >> 1) & 0xfff8 */
void SetAddr(uint16_t entry, uint32_t value)
{
    uint32_t r;
    if (!reg_of(entry, &r)) return;
    r += (entry & 1) * 0x400u + (entry & 0x3e) * 6u;
    W(r, (uint16_t)(value >> 17));
    if ((entry & 0xff00) == 0x1d00) return;
    W(r + 2, (uint16_t)((value >> 1) & 0xfff8));
}

/* 0x37f0: EEA has no lo half (0x1ffff); LSAX / NAX are read twice and the second pair wins if they differ */
uint32_t GetAddr(uint16_t entry)
{
    uint32_t r;
    if (!reg_of(entry, &r)) return 0;
    r += (entry & 1) * 0x400u + (entry & 0x3e) * 6u;
    uint32_t hi = (uint32_t)Rd(r) << 17, lo = 0x1ffff;
    uint32_t e = entry & 0xff00;
    if (e == 0x1d00) return hi | lo;
    lo = (uint32_t)Rd(r + 2) << 1;
    if (e == 0x2100 || e == 0x2200) {
        uint32_t hi2 = (uint32_t)Rd(r) << 17;
        uint32_t lo2 = (uint32_t)Rd(r + 2) << 1;
        if (hi != hi2 || lo != lo2) { hi = hi2; lo = lo2; }
    }
    return hi | lo;
}

/* ---------------------------------------------------------------- pitch (rodata D_05170 semitones, D_05188 fine steps) */

/* the two tables are adjacent in the original (semi_tbl[12] then fine_tbl[128]); Q3: Note2Pitch can index below fine_tbl
 * (into semi_tbl, kept by the contiguous array) or outside both (other module data: 0 natively, never hit) */
static const uint16_t pitch_rom[12 + 128] = {
    0x8000, 0x879c, 0x8fac, 0x9837, 0xa145, 0xaadc, 0xb504, 0xbfc8, 0xcb2f, 0xd744, 0xe411, 0xf1a1,
    0x8000, 0x800e, 0x801d, 0x802c, 0x803b, 0x804a, 0x8058, 0x8067, 0x8076, 0x8085, 0x8094, 0x80a3, 0x80b1, 0x80c0, 0x80cf, 0x80de,
    0x80ed, 0x80fc, 0x810b, 0x811a, 0x8129, 0x8138, 0x8146, 0x8155, 0x8164, 0x8173, 0x8182, 0x8191, 0x81a0, 0x81af, 0x81be, 0x81cd,
    0x81dc, 0x81eb, 0x81fa, 0x8209, 0x8218, 0x8227, 0x8236, 0x8245, 0x8254, 0x8263, 0x8272, 0x8282, 0x8291, 0x82a0, 0x82af, 0x82be,
    0x82cd, 0x82dc, 0x82eb, 0x82fa, 0x830a, 0x8319, 0x8328, 0x8337, 0x8346, 0x8355, 0x8364, 0x8374, 0x8383, 0x8392, 0x83a1, 0x83b0,
    0x83c0, 0x83cf, 0x83de, 0x83ed, 0x83fd, 0x840c, 0x841b, 0x842a, 0x843a, 0x8449, 0x8458, 0x8468, 0x8477, 0x8486, 0x8495, 0x84a5,
    0x84b4, 0x84c3, 0x84d3, 0x84e2, 0x84f1, 0x8501, 0x8510, 0x8520, 0x852f, 0x853e, 0x854e, 0x855d, 0x856d, 0x857c, 0x858b, 0x859b,
    0x85aa, 0x85ba, 0x85c9, 0x85d9, 0x85e8, 0x85f8, 0x8607, 0x8617, 0x8626, 0x8636, 0x8645, 0x8655, 0x8664, 0x8674, 0x8683, 0x8693,
    0x86a2, 0x86b2, 0x86c1, 0x86d1, 0x86e0, 0x86f0, 0x8700, 0x870f, 0x871f, 0x872e, 0x873e, 0x874e, 0x875d, 0x876d, 0x877d, 0x878c,
};
static uint32_t rom(int i) { return i >= 0 && i < 140 ? pitch_rom[i] : 0; }
static uint32_t semi_tbl(int i) { return rom(i); }
static uint32_t fine_tbl(int i) { return rom(12 + i); }

/* 0x3890 */
uint16_t Note2Pitch(uint16_t center_note, uint16_t center_fine, uint16_t note, int16_t fine)
{
    int32_t ft = (int32_t)fine + (int32_t)center_fine;
    int32_t q = (ft < 0 ? ft + 0x7f : ft) >> 7;
    int32_t semis = (int16_t)(note + q - center_note);
    ft -= q << 7;
    int32_t oct = semis / 12;
    int32_t rem = semis - oct * 12;
    int32_t sh = oct - 2, idx = rem;
    if ((int16_t)rem < 0 || ((int16_t)rem == 0 && ft < 0)) { idx = rem + 12; sh = oct - 3; }
    if (ft < 0) { idx = idx - 1 + q; ft += (q + 1) << 7; }   /* Q3: q added to the index again */
    int32_t prod = (int32_t)(semi_tbl((int16_t)idx) * fine_tbl(ft));
    uint32_t r = (uint32_t)(prod >> 16);
    if ((int16_t)sh < 0) {
        uint32_t n = (uint32_t)-(int32_t)(int16_t)sh;
        r = (r + (1u << ((n - 1) & 31))) >> (n & 31);
    }
    return (uint16_t)r;
}

/* 0x39a4 (NOT RUN in the baselines) */
uint16_t Pitch2Note(uint16_t center_note, uint16_t center_fine, uint16_t pitch)
{
    uint32_t p = pitch >= 0x4000 ? 0x3fff : pitch;
    int top = 0, semi = 0, fn = 0;
    for (int i = 0; i < 14; i++)
        if ((p >> i) & 1) top = i;
    uint32_t v = (p << (15 - top)) & 0xffff;
    for (int i = 11; i >= 0; i--)
        if (v >= semi_tbl(i)) { semi = i; break; }
    uint32_t f = ((v << 15) / semi_tbl(semi)) & 0xffff;
    for (int i = 127; i >= 0; i--)
        if (f >= fine_tbl(i)) { fn = i; break; }
    uint32_t t = center_fine + (uint32_t)(fn + 1);
    uint32_t n = center_note + (uint32_t)((top - 12) * 12) + (uint32_t)semi + ((t & 0xffff) >> 7);
    return (uint16_t)(((t & 0x7e) + (n << 8)) & 0xfffe);
}

/* ---------------------------------------------------------------- core attributes */

static int suspend(int *st) { return K.CpuSuspendIntr(K.user, st); }
static int resume(int st) { return K.CpuResumeIntr(K.user, st); }

/* f_3ae0: SPDIF output mode (v & 0xf) / copy protection (bit 7) / media (v & 0xf00) */
static int SetSpdif(uint16_t v)
{
    uint16_t t = Rd(0x7c0) & 0xfe57;
    uint16_t a = Rd(0x7c6) & 0xfffd;
    switch (v & 0xf) {
    case 0: t |= 0x20; break;
    case 1: t |= 0x100; a |= 2; break;
    case 2: break;
    case 3: t |= 0x100; break;
    default: return -100;
    }
    if (v & 0x80) a |= 0x8000;
    else a &= 0x7fff;
    if ((v & 0xf00) == 0x800) { W(0x7c8, 0x200); a = (a & 0xc0ff) | 0x1900; }
    else if ((v & 0xf00) == 0x400) { W(0x7c8, 0); a = (a & 0xc0ff) | 0x100; }
    else { W(0x7c8, 0x200); a = (a & 0xc0ff) | 0x900; }
    W(0x7c0, t);
    W(0x7c6, a);
    S.spdif = v;
    return 0;
}

/* D_05292[(entry & 0xe) / 2]: halfwords that overlap D_05290's high half (entry 0) and D_052a0's low half (entry 0xe) */
static int attr_bit(uint16_t entry)
{
    switch (entry & 0xe) {
    case 0: return (int16_t)(S.spdif >> 16);
    case 2: return 7;    /* effect enable */
    case 4: return 6;    /* IRQ enable */
    case 6: return 14;   /* mute */
    case 8: return 8;
    case 0xe: return (int16_t)S.effect[0];
    default: return 0;   /* 0xa, 0xc: zero data words */
    }
}

/* f_3c10: one ATTR bit through GetParam / SetParam (0x2300 | core) inside CpuSuspendIntr; returns CpuResumeIntr's v0 */
static int SetAttrBit(uint16_t entry, uint16_t value)
{
    int bit = attr_bit(entry), st;
    suspend(&st);
    uint16_t e = (uint16_t)((entry & 1) | 0x2300);
    uint16_t v = GetParam(e);
    SetParam(e, (uint16_t)((v & ~(1u << (bit & 31))) | ((value & 1u) << (bit & 31))));
    return resume(st);
}

/* 0x3cac */
int SetCoreAttr(uint16_t entry, uint16_t value)
{
    uint16_t e = entry & 0xfffe;
    if (e == 0xa) return SetSpdif(value);
    if (e == 8) {   /* noise clock (NOT RUN) */
        int st;
        suspend(&st);
        uint16_t r = (uint16_t)((entry & 1) | 0x2300);
        uint16_t v = GetParam(r);
        SetParam(r, (uint16_t)((v & 0xc0ff) | ((value & 0x3f) << 8)));
        return resume(st);
    }
    return SetAttrBit(entry, value);
}

/* 0x3d60 (NOT RUN) */
uint16_t GetCoreAttr(uint16_t entry)
{
    uint16_t v = Rd(0x19a + (entry & 1) * 0x400u);
    switch (entry & 0xe) {
    case 2: return (v >> 7) & 1;
    case 4: return (v >> 6) & 1;
    case 6: return (v >> 14) & 1;
    case 8: return (v >> 8) & 0x3f;
    case 0xa: return (uint16_t)S.spdif;
    default: return 0;
    }
}

/* ---------------------------------------------------------------- handlers */

/* 0x3df0 (NOT RUN). Q4: ch not masked to 0/1 in the original (other values store past B_05970): ignored natively */
Callback SetTransCallback(int ch, Callback fn)
{
    uint16_t c = (uint16_t)ch;
    if (c >= 2) return 0;
    Callback old = S.trans_cb[c];
    S.trans_cb[c] = fn;
    return old;
}

/* 0x3e2c: returns the OLD handler. Q4: ch unmasked (2 would hit D_052f0): ignored natively */
IntrHandler SetTransIntrHandler(int ch, IntrHandler fn, void *arg)
{
    if ((unsigned)ch >= 2) return 0;
    IntrHandler old = S.trans_handler[ch];
    S.trans_handler[ch] = fn;
    S.cookie[ch].arg = arg;
    return old;
}

/* 0x3e68 (NOT RUN) */
void *GetTransIntrHandlerArgument(int ch) { return (unsigned)ch < 2 ? S.cookie[ch].arg : 0; }

/* 0x3e80 (NOT RUN) */
Callback SetIRQCallback(Callback fn)
{
    Callback old = S.irq_cb;
    S.irq_cb = fn;
    return old;
}

/* 0x3ea0 */
IntrHandler SetSpu2IntrHandler(IntrHandler fn, void *arg)
{
    IntrHandler old = S.spu2_handler;
    S.spu2_handler = fn;
    S.spu2_arg = arg;
    return old;
}

/* 0x3ec8 (NOT RUN) */
void *GetSpu2IntrHandlerArgument() { return S.spu2_arg; }

/* 0x3ee0 (Ghidra bounds; iopdis shows it inside GetSpu2IntrHandlerArgument): SPU2 IRQ (IRQ 9). The pending mask is the
 * IRQ info register's bits 2-3 (core 0 / 1); each pending core's ATTR IRQ-enable bit 6 is cleared (that acknowledges it),
 * then the user handler runs; repeated while bits are pending. Returns 1. */
int Spu2Isr(void *)
{
    if (!S.spu2_handler && !S.irq_cb) return 1;
    int mask = (Rd(0x7c2) & 0xc) >> 2;
    while (mask) {
        if (mask & 1) W(0x19a, Rd(0x19a) & 0xffbf);
        if (mask & 2) W(0x59a, Rd(0x59a) & 0xffbf);
        if (S.spu2_handler) S.spu2_handler(mask, S.spu2_arg);
        else if (S.irq_cb) S.irq_cb(0);
        mask = (Rd(0x7c2) & 0xc) >> 2;
    }
    return 1;
}

/* ---------------------------------------------------------------- P2: IOP DMA channels (ch4 = core 0, ch7 = core 1) */

/* MADR / BCR / CHCR of the two IOP DMA channels (0xbf8010c0 / 0xbf801500): plain IOP registers in the original, kept as
 * the hybrid's iop_cpu.c does (Q12): MADR is never advanced, CHCR's start bit (0x01000000) is set by the store that starts
 * the transfer and cleared by DmaEnd (DMA / ADMA span done, before IRQ 36 / 40). */
static uint32_t iop_madr(const void *p) { return (uint32_t)((uintptr_t)p - (uintptr_t)H.iop_ram); }

/* CHCR store; the start bit runs the transfer as the hybrid's spu2_dma: an AutoDMA span when the core's ADMAS bit is set
 * (the mixer pulls it from madr while mixing), else bcr-sized sample-memory DMA at TSA (bit 0: IOP -> SPU) */
static void dma_chcr(int c, uint32_t v)
{
    DmaRegs &d = R.dma[c];
    d.chcr = v;
    if (!(v & 0x01000000u)) return;
    if (H.sync) H.sync(H.user);
    SndMixer *m = H.mixer;
    uint32_t bs = d.bcr & 0xffff, bc = d.bcr >> 16, size = bs * (bc ? bc : 1) * 4;
    if (snd_mixer_core_get(m, c, SND_CP_ADMAS) & (c + 1)) {
        snd_mixer_adma_start(m, c, d.madr, size);
        if (H.trace_dma) H.trace_dma(H.user, c, d.madr, v, size, 0);
        return;
    }
    uint32_t off = d.madr & 0x1fffffffu;   /* kseg bits as the IOP's physical bus */
    uint8_t *p = H.iop_ram && off < H.iop_ram_size ? H.iop_ram + off : 0;
    if (!p) size = 0;                      /* hybrid: "DMA from bad address", size 0 */
    else if (size > H.iop_ram_size - off) size = H.iop_ram_size - off;
    if (v & 1) snd_mixer_dma_write(m, c, p, size);
    else snd_mixer_dma_read(m, c, p, size);
    if (H.trace_dma) H.trace_dma(H.user, c, d.madr, v, size, p);
}

void DmaEnd(int core) { R.dma[core & 1].chcr &= ~0x01000000u; }

/* f_2254 (op 2): transfer start address (byte address -> halfword, 8-aligned), lo then hi, hi read back */
static int SetTsa(uint32_t base, uint32_t spu_addr)
{
    uint32_t t = (spu_addr >> 1) & ~7u;
    W(base + 0x1aa, (uint16_t)t);
    W(base + 0x1a8, (uint16_t)(t >> 16));
    Rd(base + 0x1a8);
    return 0;
}

/* f_21fc (op 4): 0 = channel free; -1 = ATTR DMA bits set, CHCR busy or AutoDMA on */
static int DmaStatus(int c, uint32_t base)
{
    if (Rd(base + 0x19a) & 0x30) return -1;
    if (R.dma[c].chcr & 0x01000000u) return -1;
    if (Rd(base + 0x1b0) == 0) return 0;
    return -1;
}

/* f_22ac (op 6, write) / f_2308 (op 5, read; NOT RUN): ATTR DMA mode, then MADR / BCR {0x10 words, ceil(n / 64) blocks} /
 * CHCR start. f_206c / f_2094 (the IOP's SPU bus timing register) have no native effect. Returns the block-rounded size. */
static int DmaStart(int c, uint32_t base, uint32_t madr, uint32_t n, int write)
{
    int st;
    suspend(&st);
    uint16_t a = Rd(base + 0x19a);
    W(base + 0x19a, write ? (uint16_t)((a & 0xffcf) | 0x20) : (uint16_t)(a | 0x30));
    resume(st);
    uint32_t blocks = (n >> 6) + ((n & 0x3f) != 0);
    R.dma[c].madr = madr;
    R.dma[c].bcr = 0x10 | (blocks & 0xffff) << 16;
    dma_chcr(c, write ? 0x01000201u : 0x01000200u);
    return (int)(blocks << 6);
}

/* f_23c4 (op 0xa, stop; NOT RUN): ends a running transfer, wakes VoiceTransStatus waiters, returns the interrupted
 * address (MADR, with the BlockTrans side in bits 24-31 if AutoDMA was on). The MADR / side pair is read twice in the
 * original (torn-read check against the ISR); one read natively (no preemption inside sd). The two polls are bounded
 * (0x1000000 / 0xf00 reads) as in the original. */
static int DmaStop(int c, uint32_t base)
{
    DmaRegs &d = R.dma[c];
    uint32_t madr = 0, side = 0, r = 0;
    if (d.chcr & 0x01000000u) {
        side = (uint32_t)S.bt_side[c];
        madr = d.madr;
        d.chcr &= 0xfeffffffu;
        if (Rd(base + 0x19a) & 0x30)
            if (!(Rd(base + 0x344) & 0x80))
                for (uint32_t n = 1; n <= 0x1000000 && !(Rd(base + 0x344) & 0x80); n++) {}
    }
    if (Rd(base + 0x19a) & 0x30) {
        int st;
        suspend(&st);
        W(base + 0x19a, Rd(base + 0x19a) & 0xffcf);
        resume(st);
        if (Rd(base + 0x19a) & 0x30)
            for (uint32_t n = 1; n < 0xf01 && (Rd(base + 0x19a) & 0x30); n++) {}
    }
    int adma = 0;
    if (Rd(base + 0x1b0) & 7) {
        adma = 1;
        W(base + 0x1b0, 0);
    }
    if (K.QueryIntrContext(K.user)) K.iSetEventFlag(K.user, S.evf[c], 1);
    else K.SetEventFlag(K.user, S.evf[c], 1);
    S.done_unack[c] = 0;
    S.trans_idle[c] = 1;
    S.post_hook[c] = 0;
    if (madr) {
        r = madr & 0xffffff;
        if (adma) r |= side << 24;
    }
    return (int)r;
}

/* f_2168: DMA channel control, code = ch << 4 | op (jump table D_04880: 2 TSA, 4 status, 5 read, 6 write, 0xa stop, other
 * ops return 0). ch = code >> 4 (callers pass core 0 / 1). */
static int DmaCtl(int code, uint32_t a1 = 0, uint32_t a2 = 0)
{
    int c = (code >> 4) & 1;
    uint32_t base = (uint32_t)c * 0x400;
    switch ((code & 0xf) - 2) {
    case 0: return SetTsa(base, a1);
    case 2: return DmaStatus(c, base);
    case 3: return DmaStart(c, base, a1, a2, 0);
    case 4: return DmaStart(c, base, a1, a2, 1);
    case 8: return DmaStop(c, base);
    default: return 0;
    }
}

/* f_261c (VoiceTrans mode 8; NOT RUN): CPU write through the data port, 0x40 bytes per burst, each followed by the
 * "port write" ATTR mode and a bounded STATX bit-10 poll (a delay(1) before each re-read: no native effect); then ATTR
 * DMA bits off and the transfer counts as acknowledged. Returns 0. */
static int PortWrite(const uint8_t *p, uint32_t n, int c)
{
    uint32_t base = (uint32_t)c * 0x400;
    int st;
    while (n) {
        uint32_t k = n < 0x41 ? n : 0x40;
        for (uint32_t i = 0; i < k; i += 2, p += 2) {
            uint16_t hw;
            memcpy(&hw, p, 2);
            W(base + 0x1ac, hw);
        }
        suspend(&st);
        W(base + 0x19a, (uint16_t)((Rd(base + 0x19a) & 0xffcf) | 0x10));
        resume(st);
        if (Rd(base + 0x344) & 0x400)
            for (uint32_t t = 1; t < 0xf01 && (Rd(base + 0x344) & 0x400); t++) {}
        n -= k;
    }
    suspend(&st);
    W(base + 0x19a, Rd(base + 0x19a) & 0xffcf);
    resume(st);
    S.trans_idle[c] = 1;
    return 0;
}

/* 0x1890: IOP -> SPU (mode & 3 == 0) or SPU -> IOP (1; NOT RUN) sample-memory transfer of size rounded up to 0x40 at
 * spu_addr. Mode 8 (NOT RUN) writes through the data port instead of DMA. -210 channel busy, -201 previous transfer not
 * acknowledged (VoiceTransStatus), -100 bad mode. The ISR (DmaIsr) completes the DMA. */
int VoiceTrans(int ch, uint16_t mode, void *iop, uint32_t spu_addr, uint32_t size)
{
    int c = ch & 1;
    if (DmaCtl(c << 4 | 4) != 0) return -210;
    if (S.trans_idle[c] != 1) return -201;
    uint32_t dir = mode & 3;
    uint32_t n = (size >> 6) << 6;
    if (size & 0x3f) n += 0x40;
    if (dir == 0) {
        if (mode & 8) {
            S.cookie[c].state = (uint32_t)c | 0x500;
            S.cpu_write_done[c] = S.trans_idle[c];   /* the 1 just checked */
            DmaCtl(c << 4 | 2, spu_addr);
            PortWrite((const uint8_t *)iop, n, c);
            return (int)n;
        }
        S.cookie[c].state = (uint32_t)c | 0x500;
        S.cpu_write_done[c] = 0;
        DmaCtl(c << 4 | 2, spu_addr);
        if (K.QueryIntrContext(K.user)) K.iClearEventFlag(K.user, S.evf[c], 0xfffffffeu);
        else K.ClearEventFlag(K.user, S.evf[c], 0xfffffffeu);
        S.trans_idle[c] = 0;
        return DmaCtl(c << 4 | 6, iop_madr(iop), n);
    }
    if (dir != 1) return -100;
    S.cookie[c].state = (uint32_t)c | 0x900;
    S.cpu_write_done[c] = 0;
    DmaCtl(c << 4 | 2, spu_addr);
    if (K.QueryIntrContext(K.user)) K.iClearEventFlag(K.user, S.evf[c], 0xfffffffeu);
    else K.ClearEventFlag(K.user, S.evf[c], 0xfffffffeu);
    S.trans_idle[c] = 0;
    return DmaCtl(c << 4 | 5, iop_madr(iop), n);
}

/* 0x1ae0: 1 = done and acknowledged. wait 0: promotes a done-unacknowledged transfer (Q8); wait 1: blocks in
 * WaitEventFlag until the ISR sets the flag (the only blocking point of sd; -202 from interrupt context), then
 * acknowledges unconditionally (0x1b74 jumps into the promote block at 0x1b94: done-unacknowledged cleared, returns 1).
 * Other wait values return the acknowledge state. Q4: ch is (s16), not masked (other values read other module data:
 * 0 natively). */
int VoiceTransStatus(int ch, int wait)
{
    int c = (int16_t)ch;
    if ((unsigned)c >= 2) return 0;
    if (S.cpu_write_done[c] == 1 || S.trans_idle[c] == 1) return 1;
    int16_t w = (int16_t)wait;
    if (w == 1) {
        if (K.QueryIntrContext(K.user)) return -202;
        uint32_t res;
        K.WaitEventFlag(K.user, S.evf[c], 1, 0, &res);   /* WEF_AND, no clear */
    }
    if ((w == 0 && S.done_unack[c]) || w == 1) {
        S.done_unack[c] = 0;
        S.trans_idle[c] = 1;
    }
    return (int)S.trans_idle[c];
}

/* 0x1bd8 (NOT RUN) */
int StopTrans(int ch)
{
    int c = ch & 1;
    S.cookie[c].state = (uint32_t)c;
    return DmaCtl(c << 4 | 0xa);
}

/* f_2c0c: AutoDMA (sound data input) from IOP RAM: base / half size / side 0 for the ISR's ping-pong, ATTR DMA bits off,
 * TSA 0, ADMAS = core bit, then MADR / BCR / CHCR. start (BlockTrans mode 3; NOT RUN) begins inside the buffer: the
 * first span runs to the end of its half (rounded up to 1 KB, ending at the half's end); outside both halves -> -100. */
static int AdmaWrite(uint32_t addr, uint32_t size, int c, uint32_t mode, uint32_t start)
{
    uint32_t base = (uint32_t)c * 0x400;
    S.bt_base[c] = addr;
    S.bt_side[c] = 0;
    S.bt_half[c] = size;
    uint32_t first = start;
    int32_t len;
    if (!start) {
        first = addr;
        len = (int32_t)size;
    } else {
        uint32_t off = start - addr;
        if (off < size) {
            len = (int32_t)(size - off);
        } else {
            if (!(mode & 0x10)) return -100;
            off -= size;
            if (off >= size) return -100;
            S.bt_side[c]++;
            len = (int32_t)(size - off);
        }
        int32_t q = len / 1024;
        if (len - q * 1024 > 0) {
            len = (q + 1) * 1024;
            first = addr + ((uint32_t)S.bt_side[c] * size + (size - (uint32_t)len));
        }
    }
    int st;
    suspend(&st);
    W(base + 0x19a, Rd(base + 0x19a) & 0xffcf);
    resume(st);
    W(base + 0x1a8, 0);
    W(base + 0x1aa, 0);
    W(base + 0x1b0, (uint16_t)(1u << c));
    R.dma[c].madr = first;
    uint32_t blocks = (uint32_t)(len / 64 + (len % 64 > 0));
    R.dma[c].bcr = 0x10 | (blocks & 0xffff) << 16;
    dma_chcr(c, 0x01000201u);
    return (int)size;
}

/* f_2e34 (BlockTrans mode 1; NOT RUN): core output capture into IOP RAM: TSA = ((mode & 0xf00) << 1) + 0x400 (hi 0),
 * 0x1ae = (mode & 0xf000) >> 11, ADMAS = 4, read DMA of one half. The delay f_2120(3) and f_2094 have no native effect. */
static int AdmaRead(uint32_t addr, uint32_t size, int c, uint32_t mode)
{
    uint32_t base = (uint32_t)c * 0x400;
    S.bt_base[c] = addr;
    S.bt_side[c] = 0;
    S.bt_half[c] = size;
    int st;
    suspend(&st);
    W(base + 0x19a, Rd(base + 0x19a) & 0xffcf);
    resume(st);
    W(base + 0x1a8, 0);
    W(base + 0x1aa, (uint16_t)(((mode & 0xf00) << 1) + 0x400));
    W(base + 0x1ae, (uint16_t)((mode & 0xf000) >> 11));
    W(base + 0x1b0, 4);
    R.dma[c].madr = addr;
    int32_t h = (int32_t)S.bt_half[c];
    uint32_t blocks = (uint32_t)(h / 64 + (h % 64 > 0));
    R.dma[c].bcr = 0x10 | (blocks & 0xffff) << 16;
    dma_chcr(c, 0x01000200u);
    return (int)size;
}

/* 0x1c20: AutoDMA block transfer. mode & 3: 0 write (sound data input), 1 read (NOT RUN), 2 stop (NOT RUN), 3 write from
 * `start` (NOT RUN); mode & 0x10 = loop: the buffer is two halves of size / 2 that the ISR re-arms alternately, and the
 * HALVED size is returned. The channel status is read first for every mode (stop ignores it). On an error the cookie
 * state falls back to the bare core. */
int BlockTrans(int ch, uint16_t mode, void *iop, uint32_t size, void *start)
{
    int c = ch & 1;
    int busy = DmaCtl(c << 4 | 4);
    int r;
    switch (mode & 3) {
    case 0:
        if (busy) return -210;
        S.cookie[c].state = (uint32_t)c | 0x600;
        if (mode & 0x10) { S.cookie[c].state = (uint32_t)c | 0x1600; size >>= 1; }
        r = AdmaWrite(iop_madr(iop), size, c, mode, 0);
        break;
    case 1:
        if (busy) return -210;
        S.cookie[c].state = (uint32_t)c | 0xa00;
        if (mode & 0x10) { S.cookie[c].state = (uint32_t)c | 0x1a00; size >>= 1; }
        r = AdmaRead(iop_madr(iop), size, c, mode);
        break;
    case 2:
        S.cookie[c].state = (uint32_t)c;
        return DmaCtl(c << 4 | 0xa);
    default:
        if (busy) return -210;
        S.cookie[c].state = (uint32_t)c | 0x600;
        if (mode & 0x10) { S.cookie[c].state = (uint32_t)c | 0x1600; size >>= 1; }
        r = AdmaWrite(iop_madr(iop), size, c, mode, start ? iop_madr(start) : 0);
        break;
    }
    if (r < 0) S.cookie[c].state = (uint32_t)c;
    return r;
}

/* 0x1e3c -> f_2b78: side << 24 | the last PROGRAMMED MADR (Q12; 0 when AutoDMA is off). The original reads side / MADR
 * twice (torn-read check); one read natively. Q4: ch (s16) unmasked (0 natively for other values). */
uint32_t BlockTransStatus(int ch)
{
    int c = (int16_t)ch;
    if ((unsigned)c >= 2) return 0;
    uint32_t side = (uint32_t)S.bt_side[c], madr = R.dma[c].madr;
    if (!(Rd((uint32_t)c * 0x400 + 0x1b0) & 7)) madr = 0;
    return side << 24 | (madr & 0xffffff);
}

/* f_27b8: DMA-end ISR, IRQ 36 (core 0) / 40 (core 1), arg = the core's cookie {state, handler arg}; the core comes from
 * the state word. Runs with QueryIntrContext() != 0. Returns 1.
 * VoiceTrans (0x100): wait for STATX DMA-ready and the ATTR DMA bits to clear (bounded polls), then the internal hook
 * (D_052f0, never set) or iSetEventFlag + the trans handler / callback (acknowledged at once) or "done, unacknowledged".
 * BlockTrans (0x200): non-loop (NOT RUN) stops AutoDMA before the handler (Q9); loop re-arms the other half before the
 * handler; the acknowledge state and the event flag are not touched. Read direction (0x800): FlushDcache. */
int DmaIsr(void *arg)
{
    Cookie *ck = (Cookie *)arg;
    uint32_t st = ck->state, dir = st & 0xc00;
    uint32_t to_spu;
    if (dir == 0x400) to_spu = 1;
    else if (dir == 0x800) to_spu = 0;
    else return 1;
    int c = (int)(st & 0xff);
    if (c >= 2) return 1;   /* the state word's core is always 0 / 1 (written from masked channels) */
    uint32_t base = (uint32_t)c * 0x400;
    switch (st & 0x300) {
    case 0x100:
        if (!(Rd(base + 0x344) & 0x80))
            for (uint32_t n = 1; n <= 0x1000000 && !(Rd(base + 0x344) & 0x80); n++) {}
        W(base + 0x19a, Rd(base + 0x19a) & 0xffcf);
        if (Rd(base + 0x19a) & 0x30)
            for (uint32_t n = 1; n < 0xf01 && (Rd(base + 0x19a) & 0x30); n++) {}
        if (!to_spu) K.FlushDcache(K.user);
        if (S.post_hook[c]) {
            S.post_hook[c](c, 0);
            return 1;
        }
        K.iSetEventFlag(K.user, S.evf[c], 1);
        if (S.trans_handler[c]) {
            S.trans_idle[c] = 1;
            S.trans_handler[c](c, ck->arg);
        } else if (S.trans_cb[c]) {
            S.trans_idle[c] = 1;
            /* Q: the original sets no a0 for the callback (shared jalr at 0x2b54); undefined on hardware, the hybrid
             * kernel leaves iSetEventFlag's a0 = the event flag id. SetTransCallback is imported by no game module. */
            S.trans_cb[c]((void *)(uintptr_t)(uint32_t)S.evf[c]);
        } else {
            S.done_unack[c] = 1;
        }
        return 1;
    case 0x200: {
        /* a0 left for the callback (no a0 set at 0x2b54, undefined on hardware; the hybrid kernel's FlushDcache keeps
         * a0): non-loop = the ATTR register address, loop = the core's DMA MADR address (ch 4 / 7). */
        uint32_t cb_a0 = 0xbf90019au + base;
        if (!(st & 0x1000)) {   /* NOT RUN */
            W(base + 0x19a, Rd(base + 0x19a) & 0xffcf);
            W(base + 0x1b0, 0);
            Rd(base + 0x1b0);
        } else {
            int32_t side = 1 - S.bt_side[c];
            int32_t h = (int32_t)S.bt_half[c];
            uint32_t madr = S.bt_base[c] + (uint32_t)(side * h);
            uint32_t blocks = (uint32_t)(h / 64 + (h % 64 > 0));
            R.dma[c].bcr = (R.dma[c].bcr & 0xffff) | (blocks & 0xffff) << 16;
            S.bt_side[c] = side;
            R.dma[c].madr = madr;
            dma_chcr(c, to_spu | 0x01000200u);
            cb_a0 = 0xbf8010c0u + (uint32_t)c * 0x440;
        }
        if (!to_spu) K.FlushDcache(K.user);
        if (S.trans_handler[c]) S.trans_handler[c](c, ck->arg);
        else if (S.trans_cb[c]) S.trans_cb[c]((void *)(uintptr_t)cb_a0);
        return 1;
    }
    }
    return 1;
}

/* ---------------------------------------------------------------- P3: reverb effects */

/* D_04a68: work-area size per effect mode in 8-byte units (D_04a40, the 0x10000 - size table next to it, is dead data) */
static const uint32_t WORK_SIZE[10] = {0x2, 0x4d8, 0x3e8, 0x908, 0xdfc, 0x15bc, 0x1ed8, 0x3008, 0x3008, 0x780};

/* D_04a90: reverb presets, 0x44 bytes each: register mask (0 = all; 0 in every entry) and 32 register values (p[i] is the
 * original's byte offset 4 + 2 * i; see SetPreset). Modes 0 off, 1 room, 2 studio A, 3 studio B, 4 studio C, 5 hall,
 * 6 space, 7 echo, 8 delay, 9 pipe (SDK names). Values from the IRX bytes (IRX.BIN@02d800 + 0x4a90). */
struct Preset { uint32_t mask; uint16_t p[32]; };
static const Preset PRESET[10] = {
    {0x0, {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0001, 0x0000, 0x0000, 0x0000, 0x0000,
           0x0000, 0x0000, 0x0001, 0x0001, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000}},
    {0x0, {0x007d, 0x005b, 0x6d80, 0x54b8, 0xbed0, 0x0000, 0x0000, 0xba80, 0x5800, 0x5300, 0x04d6, 0x0333, 0x03f0, 0x0227, 0x0374, 0x01ef,
           0x0336, 0x01b7, 0x0335, 0x01b6, 0x0334, 0x01b5, 0x0334, 0x01b5, 0x0334, 0x01b5, 0x01b4, 0x0136, 0x00b8, 0x005c, 0x8000, 0x8000}},
    {0x0, {0x0033, 0x0025, 0x70f0, 0x4fa8, 0xbce0, 0x4410, 0xc0f0, 0x9c00, 0x5280, 0x4ec0, 0x03e4, 0x031b, 0x03a4, 0x02af, 0x0372, 0x0266,
           0x031c, 0x025d, 0x025c, 0x018e, 0x022f, 0x0135, 0x01d2, 0x00b7, 0x018f, 0x00b5, 0x00b4, 0x0080, 0x004c, 0x0026, 0x8000, 0x8000}},
    {0x0, {0x00b1, 0x007f, 0x70f0, 0x4fa8, 0xbce0, 0x4510, 0xbef0, 0xb4c0, 0x5280, 0x4ec0, 0x0904, 0x076b, 0x0824, 0x065f, 0x07a2, 0x0616,
           0x076c, 0x05ed, 0x05ec, 0x042e, 0x050f, 0x0305, 0x0462, 0x02b7, 0x042f, 0x0265, 0x0264, 0x01b2, 0x0100, 0x0080, 0x8000, 0x8000}},
    {0x0, {0x00e3, 0x00a9, 0x6f60, 0x4fa8, 0xbce0, 0x4510, 0xbef0, 0xa680, 0x5680, 0x52c0, 0x0dfb, 0x0b58, 0x0d09, 0x0a3c, 0x0bd9, 0x0973,
           0x0b59, 0x08da, 0x08d9, 0x05e9, 0x07ec, 0x04b0, 0x06ef, 0x03d2, 0x05ea, 0x031d, 0x031c, 0x0238, 0x0154, 0x00aa, 0x8000, 0x8000}},
    {0x0, {0x01a5, 0x0139, 0x6000, 0x5000, 0x4c00, 0xb800, 0xbc00, 0xc000, 0x6000, 0x5c00, 0x15ba, 0x11bb, 0x14c2, 0x10bd, 0x11bc, 0x0dc1,
           0x11c0, 0x0dc3, 0x0dc0, 0x09c1, 0x0bc4, 0x07c1, 0x0a00, 0x06cd, 0x09c2, 0x05c1, 0x05c0, 0x041a, 0x0274, 0x013a, 0x8000, 0x8000}},
    {0x0, {0x033d, 0x0231, 0x7e00, 0x5000, 0xb400, 0xb000, 0x4c00, 0xb000, 0x6000, 0x5400, 0x1ed6, 0x1a31, 0x1d14, 0x183b, 0x1bc2, 0x16b2,
           0x1a32, 0x15ef, 0x15ee, 0x1055, 0x1334, 0x0f2d, 0x11f6, 0x0c5d, 0x1056, 0x0ae1, 0x0ae0, 0x07a2, 0x0464, 0x0232, 0x8000, 0x8000}},
    {0x0, {0x0003, 0x0003, 0x7fff, 0x7fff, 0x0000, 0x0000, 0x0000, 0x8100, 0x0000, 0x0000, 0x1ffd, 0x0ffd, 0x1009, 0x0009, 0x0000, 0x0000,
           0x1009, 0x0009, 0x1fff, 0x1fff, 0x1ffe, 0x1ffe, 0x1ffe, 0x1ffe, 0x1ffe, 0x1ffe, 0x1008, 0x1004, 0x0008, 0x0004, 0x8000, 0x8000}},
    {0x0, {0x0003, 0x0003, 0x7fff, 0x7fff, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1ffd, 0x0ffd, 0x1009, 0x0009, 0x0000, 0x0000,
           0x1009, 0x0009, 0x1fff, 0x1fff, 0x1ffe, 0x1ffe, 0x1ffe, 0x1ffe, 0x1ffe, 0x1ffe, 0x1008, 0x1004, 0x0008, 0x0004, 0x8000, 0x8000}},
    {0x0, {0x0017, 0x0013, 0x70f0, 0x4fa8, 0xbce0, 0x4510, 0xbef0, 0x8500, 0x5f80, 0x54c0, 0x0371, 0x02af, 0x02e5, 0x01df, 0x02b0, 0x01d7,
           0x0358, 0x026a, 0x01d6, 0x011e, 0x012d, 0x00b1, 0x011f, 0x0059, 0x01a0, 0x00e3, 0x0058, 0x0040, 0x0028, 0x0014, 0x8000, 0x8000}},
};

/* the core's effect state words (D_052a0 + core * 0x14; core is not range checked in the original: 0 / 1 natively) */
static uint32_t *fx_state(int core) { return &S.effect[(core & 1) * 5]; }
/* the zero page (D_04d40) as the host pointer VoiceTrans takes / as an IOP address */
static void *zero_page() { return (void *)((uintptr_t)H.iop_ram + H.zero_page); }

/* f_0110: a reverb address register pair (reg = halfword index from the core's base): value in 4-byte units, hi then lo
 * (Q2) */
static void SetRevAddr(int core, uint32_t reg, uint16_t v)
{
    uint32_t a = (((uint32_t)core << 9) + reg) * 2, v4 = (uint32_t)v << 2;
    W(a, (uint16_t)(v4 >> 16));
    W(a + 2, (uint16_t)v4);
}

/* f_013c: the preset's registers in table order (mask 0 = all, else bit i = p[i]): p[0] / p[1] FB_SRC_A / B (0x2e4 /
 * 0x2e8), p[2..9] the 0x774-0x782 coefficients, p[10..29] the address pairs 0x2ec-0x338, p[30] / p[31] 0x784 / 0x786 */
static void SetPreset(int core, const Preset *pr)
{
    uint32_t m = pr->mask, vb = (uint32_t)core * 0x28;
    int all = m == 0;
    if (all || (m & 1)) SetRevAddr(core, 0x172, pr->p[0]);
    if (all || (m & 2)) SetRevAddr(core, 0x174, pr->p[1]);
    for (int i = 2; i < 10; i++)
        if (all || ((m >> i) & 1)) W(0x774 + (uint32_t)(i - 2) * 2 + vb, pr->p[i]);
    for (int i = 10; i < 30; i++)
        if (all || ((m >> i) & 1)) SetRevAddr(core, 0x176 + (uint32_t)(i - 10) * 2, pr->p[i]);
    if (all || ((m >> 30) & 1)) W(0x784 + vb, pr->p[30]);
    if (all || ((m >> 31) & 1)) W(0x786 + vb, pr->p[31]);
}

/* f_1100: the work area's last byte (EEA hi << 17 | 0x1ffff) */
static uint32_t WorkEnd(int core) { return (uint32_t)Rd(0x33c + (uint32_t)core * 0x400) << 17 | 0x1ffff; }

/* echo / delay (modes 7 / 8; NOT RUN): delay and feedback into the state and the preset copy (SetEffectAttr 0xe40-0xf5c,
 * SetEffectModeParams 0x144c-0x14f4: same arithmetic, u16 stores) */
static void EchoParams(uint32_t *fx, Preset *pr, uint32_t delay, uint32_t feedback)
{
    fx[3] = delay;
    uint32_t d = delay + 1, d32 = d << 5;
    uint16_t *p = pr->p;
    p[10] = (uint16_t)((d << 6) - p[0]);
    p[11] = (uint16_t)(d32 - p[1]);
    p[16] = (uint16_t)(p[17] + d32);
    p[12] = (uint16_t)(p[13] + d32);
    p[27] = (uint16_t)(p[29] + d32);
    p[26] = (uint16_t)(p[28] + d32);
    fx[4] = feedback;
    p[7] = (uint16_t)(feedback * 0x102);
}

/* 0x660: zeroes the work area of effect `mode` below the core's EEA through VoiceTrans on DMA channel ch (independent of
 * core), 1 KB per transfer, waiting for each (VoiceTransStatus(ch, 1): blocks the calling thread). A size that is no
 * multiple of 0x40 (modes 4 and 5: WORK_SIZE 0xdfc / 0x15bc) first sends a 0x40-byte head at the unaligned start. The
 * channel's trans handler / callback are saved, zeroed and restored around the transfers (the ISR then marks them "done,
 * unacknowledged" and the wait acknowledges them). -100 bad mode, -210 channel busy, -201 not acknowledged, -202
 * interrupt context, or the failing VoiceTrans / VoiceTransStatus result. ch is not masked in the original (index into
 * the per-channel words): 0 / 1 natively. */
static int ClearChunks(int16_t ch, uint32_t start, uint32_t start2, uint32_t size)
{
    int r;
    if (start2) {
        r = VoiceTrans(ch, 0, zero_page(), start * 2, 0x40);
        if (r < 0) return r;
        r = VoiceTransStatus(ch, 1);
        if (r < 0) return r;
        start = start2;
    }
    for (int more = 1; more;) {
        uint32_t n = 0x400;
        if (size < 0x401) { n = size; more = 0; }
        r = VoiceTrans(ch, 0, zero_page(), start * 2, n);
        if (r < 0) return r;
        r = VoiceTransStatus(ch, 1);
        if (r < 0) return r;
        size -= 0x400;
        start += 0x200;
    }
    return 0;
}

int ClearEffectWorkArea(int core, int ch, int mode)
{
    uint32_t m = (uint32_t)mode & 0xff;
    if (m >= 10) return -100;
    if (m == 0) return 0;
    if (DmaCtl(ch << 4 | 4) != 0) return -210;
    int c = ch & 1;
    if (S.trans_idle[c] != 1) return -201;
    if (K.QueryIntrContext(K.user)) return -202;
    uint32_t size = WORK_SIZE[m] * 8;
    uint32_t start = (WorkEnd(core) - (size - 1)) >> 1, start2 = 0;   /* halfword addresses */
    if (size & 0x3f) {
        size = (size >> 6) << 6;
        start2 = (WorkEnd(core) - (size - 1)) >> 1;
    }
    IntrHandler h = S.trans_handler[c];
    Callback cb = S.trans_cb[c];
    S.trans_handler[c] = 0;
    S.trans_cb[c] = 0;
    int r = ClearChunks((int16_t)ch, start, start2, size);
    S.trans_handler[c] = h;
    S.trans_cb[c] = cb;
    return r;
}

/* f_0888 (NOT RUN): the DMA-end post hook (D_052f0) CleanEffectWorkArea installs: starts the next span straight through
 * the channel (TSA, then a DMA from the zero page; no VoiceTrans bookkeeping), and removes itself before the last one so
 * the last span's ISR completes the transfer normally. The index is incremented before use: span 0 is never sent (Q14). */
static int CleanNext(int core, void *)
{
    int c = core & 1;
    int32_t i = ++S.clean_i[c];
    if (!(i < S.clean_n[c] - 1)) S.post_hook[c] = 0;
    uint32_t addr = 0, size = 0;
    if (i >= 0 && i < 97) { addr = S.clean_span[c][i].addr; size = S.clean_span[c][i].size; }
    DmaCtl(c << 4 | 2, addr);
    DmaCtl(c << 4 | 6, H.zero_page, size);
    return 0;
}

/* 0x97c (NOT RUN): CleanEffectWorkArea, the non-blocking form: the first 1 KB by VoiceTrans, the rest chained from the
 * DMA-end ISR through f_0888 (span list B_05330: 1 KB spans after the first, the remainder last). A size that is no
 * multiple of 0x40 (modes 4 / 5) first writes a 0x40-byte head through the data port (VoiceTrans mode 8). Returns
 * 0 once the first DMA started (or its error); no interrupt-context check. Addresses are byte addresses here. */
int CleanEffectWorkArea(int core, int ch, int mode)
{
    uint32_t m = (uint32_t)mode & 0xff;
    if (m >= 10) return -100;
    if (m == 0) return 0;
    if (DmaCtl(ch << 4 | 4) != 0) return -210;
    int c = ch & 1;
    if (S.trans_idle[c] != 1) return -201;
    uint32_t size = WORK_SIZE[m] * 8;
    uint32_t start = WorkEnd(core) - (size - 1);
    int r;
    if (size & 0x3f) {
        size = (size >> 6) << 6;
        r = VoiceTrans((int16_t)ch, 8, zero_page(), start, 0x40);
        if (r < 0) return r;
        start = WorkEnd(core) - (size - 1);
    }
    uint32_t a = start + 0x400;
    int32_t left = (int32_t)size - 0x400;
    int k = 0;
    S.clean_n[c] = 0;
    for (; !(left < 0x401) && k < 96; k++) {
        S.clean_span[c][k].addr = a;
        S.clean_span[c][k].size = 0x400;
        a += 0x400;
        S.clean_n[c]++;
        left -= 0x400;
    }
    S.clean_span[c][k].addr = a;
    S.clean_span[c][k].size = (uint32_t)left;
    S.clean_n[c]++;
    S.post_hook[c] = CleanNext;
    S.clean_i[c] = 0;
    r = VoiceTrans((int16_t)ch, 0, zero_page(), start, 0x400);
    return r < 0 ? r : 0;
}

/* 0xbbc (NOT RUN): returns the last value read (depth_r), as the original's v0 */
int GetEffectAttr(int core, EffectAttr *attr)
{
    uint32_t *fx = fx_state(core);
    uint32_t vb = (uint32_t)core * 0x28;
    attr->core = core;
    attr->mode = fx[1];
    attr->delay = fx[3];
    attr->feedback = fx[4];
    attr->depth_l = Rd(0x764 + vb);
    uint16_t r = Rd(0x766 + vb);
    attr->depth_r = r;
    return r;
}

/* 0xc2c: effect mode + depth (+ echo / delay parameters), optionally clearing the old and new work areas (flag 0x100,
 * through channel 1 with 0x200; ClearEffectWorkArea blocks). Stores the mode (Q13) and the work-area start before it can
 * fail; with the effect enabled (ATTR bit 7) it is switched off around the update. ESA lo is written without the 0xfff8
 * mask (Q1; no visible difference: every WORK_SIZE is even, so esa >> 1 is a multiple of 8). The original's fp = 1
 * constant leaves a preset-only branch (mask 0x0c011c80 / 0x0c011c00) dead (0xdf8-0xe3c, 0xed0-0xf2c). */
int SetEffectAttr(int core, const EffectAttr *attr)
{
    uint32_t *fx = fx_state(core);
    uint32_t flags = attr->mode, prev = 0;
    int clear = 0, clear_ch = 0;
    if (flags & 0x100) {
        prev = fx[1];
        clear = 1;
        clear_ch = (flags & 0x200) != 0;
    }
    uint32_t mode = flags & 0xff;
    if (mode >= 10) return -100;
    fx[1] = mode;
    S.esa_start[core & 1] = WorkEnd(core) - (WORK_SIZE[mode] * 8 - 1);
    Preset pr = PRESET[fx[1]];
    if (mode == 7) { fx[4] = 0x80; fx[3] = 0x80; }
    else if (mode == 8) { fx[4] = 0; fx[3] = 0x80; }
    else { fx[4] = 0; fx[3] = 0; }
    if (mode == 7 || mode == 8) EchoParams(fx, &pr, attr->delay, attr->feedback);
    uint32_t a = 0x19a + (uint32_t)core * 0x400, vb = (uint32_t)core * 0x28;
    int on = (Rd(a) >> 7) & 1, st, r = 0;
    if (on) {
        suspend(&st);
        W(a, Rd(a) & 0xff7f);
        resume(st);
    }
    if (on && clear) r = ClearEffectWorkArea(core, clear_ch, (int)prev);
    if (r >= 0) {
        W(0x764 + vb, attr->depth_l);
        W(0x766 + vb, attr->depth_r);
        SetPreset(core, &pr);
        uint32_t esa = S.esa_start[core & 1], b = (uint32_t)core * 0x400;
        W(0x2e0 + b, (uint16_t)(esa >> 17));
        W(0x2e2 + b, (uint16_t)(esa >> 1));
        Rd(0x2e2 + b);
        r = clear ? ClearEffectWorkArea(core, clear_ch, (int)mode) : 0;
    }
    if (on) {
        suspend(&st);
        W(a, Rd(a) | 0x80);
        Rd(a);
        resume(st);
    }
    return r;
}

/* 0x1128 (NOT RUN): mode only: depth 0, delay / feedback 0, the plain preset; the optional clear (0x100 / 0x200) is the
 * non-blocking CleanEffectWorkArea after the effect is re-enabled. No ESA read-back; ATTR is read back after
 * CpuResumeIntr (SetEffectAttr: before). */
int SetEffectMode(int core, const EffectAttr *attr)
{
    uint32_t *fx = fx_state(core);
    uint32_t flags = attr->mode;
    int clean = 0, ch = 0;
    if (flags & 0x100) {
        clean = 1;
        ch = (flags & 0x200) != 0;
    }
    uint32_t mode = flags & 0xff;
    if (mode >= 10) return -100;
    fx[1] = mode;
    fx[3] = 0;
    fx[4] = 0;
    S.esa_start[core & 1] = WorkEnd(core) - (WORK_SIZE[mode] * 8 - 1);
    Preset pr = PRESET[mode];
    uint32_t a = 0x19a + (uint32_t)core * 0x400, vb = (uint32_t)core * 0x28, b = (uint32_t)core * 0x400;
    int on = (Rd(a) >> 7) & 1, st;
    if (on) {
        suspend(&st);
        W(a, Rd(a) & 0xff7f);
        resume(st);
    }
    W(0x764 + vb, 0);
    W(0x766 + vb, 0);
    SetPreset(core, &pr);
    uint32_t esa = S.esa_start[core & 1];
    W(0x2e0 + b, (uint16_t)(esa >> 17));
    W(0x2e2 + b, (uint16_t)(esa >> 1));
    if (on) {
        suspend(&st);
        W(a, Rd(a) | 0x80);
        resume(st);
        Rd(a);
    }
    return clean ? CleanEffectWorkArea(core, ch, (int)mode) : 0;
}

/* 0x1380 (NOT RUN): depth, and for the current mode 7 / 8 the delay / feedback registers only (mask 0x0c011c80); the
 * mode byte must equal the current mode (-100 otherwise) */
int SetEffectModeParams(int core, const EffectAttr *attr)
{
    uint32_t *fx = fx_state(core);
    uint32_t m = attr->mode & 0xff;   /* lbu */
    if (m >= 10 || fx[1] != m) return -100;
    if (fx[1] == 7 || fx[1] == 8) {
        Preset pr = PRESET[fx[1]];
        pr.mask = 0x0c011c80;
        EchoParams(fx, &pr, attr->delay, attr->feedback);
        SetPreset(core, &pr);
    }
    uint32_t vb = (uint32_t)core * 0x28;
    W(0x764 + vb, attr->depth_l);
    W(0x766 + vb, attr->depth_r);
    return 0;
}

/* ---------------------------------------------------------------- P4: command lists (NOT RUN in any log) */

/* f_3000: one SIF DMA IOP -> EE {src, dest, size, attr 0} (sceSifSetDma inside CpuSuspendIntr), then polls sceSifDmaStat
 * (each poll inside CpuSuspendIntr) until it is < 0. Returns 0; -1 if the queue was full (id 0) or after 2^31 polls. */
static int SifDmaWait(uint32_t ee, const void *src, uint32_t size)
{
    SifDmaTransfer t = {src, ee, size, 0};
    int st;
    suspend(&st);
    int id = K.SifSetDma(K.user, &t, 1);
    resume(st);
    if (id == 0) return -1;
    for (uint32_t n = 0;;) {
        suspend(&st);
        int busy = K.SifDmaStat(K.user, id);
        resume(st);
        if (busy < 0) return 0;
        if ((int32_t)++n < 0) return -1;
    }
}

/* f_31e8 / f_3588 (command 5): sw entry to the IOP address value. Native: dropped outside Hw::iop_ram (the original
 * stores anywhere; an unaligned address would raise an address error) */
static void IopStore(uint32_t addr, uint32_t v)
{
    if (H.iop_ram && addr < H.iop_ram_size && H.iop_ram_size - addr >= 4) memcpy(H.iop_ram + addr, &v, 4);
}

/* 0x30bc (NOT RUN; handlers f_3138-f_321c, jump table D_048b0): runs num commands; ret (optional) gets one word per
 * command: 0 for cmd 1-5, f_3000's 0 / -1 for 6 / 7, the value for 0x11-0x14 (GetParam / GetCoreAttr & 0xffff). An
 * unknown command i (0, 8-0x10, > 0x14) returns -(i + 1) at once (ret[0 .. i-1] written); else returns num.
 * Command 7 sends ret itself (entry bytes, as filled so far; the IOP address 0 if ret is 0). */
int ProcBatch(const BatchEntry *batch, uint32_t *ret, uint32_t num)
{
    uint32_t i;
    for (i = 0; i < num; i++) {
        const BatchEntry *b = &batch[i];
        uint32_t v = 0;
        switch (b->cmd) {
        case 1: SetParam(b->entry, (uint16_t)b->value); break;
        case 2: SetSwitch(b->entry, b->value); break;
        case 3: SetAddr(b->entry, b->value); break;
        case 4: SetCoreAttr(b->entry, (uint16_t)b->value); break;
        case 5: IopStore(b->value, b->entry); break;
        case 6: S.batch_word = b->entry; v = (uint32_t)SifDmaWait(b->value, &S.batch_word, 4); break;
        case 7: v = (uint32_t)SifDmaWait(b->value, ret, b->entry); break;
        case 0x11: v = GetParam(b->entry); break;
        case 0x12: v = GetSwitch(b->entry); break;
        case 0x13: v = GetAddr(b->entry); break;
        case 0x14: v = GetCoreAttr(b->entry); break;
        default: return -(int32_t)i - 1;
        }
        if (ret) ret[i] = v;
    }
    return (int)i;
}

/* 0x32a8 (NOT RUN; handlers f_3328-f_35bc, jump table D_04900): ProcBatch with "all voices" forms. SetParam / GetParam
 * with voice field 31 ((entry & 0x3e) == 0x3e) and SetAddr / GetAddr with (entry & 0x7e) == 0x7e (Q15: bit 6 too) run
 * the access for each voice k < 24 set in voice, entry (entry & 0xffc1) | k << 1. Q15 ret index n (the return value):
 * one slot per command, except Set*-all advances it by the number of voices (only the last slot written, with 0; mask 0
 * rewrites the previous slot, ret[-1] for the first command), GetParam-all by 24 (unselected voices repeat the last
 * value read, 0 before the first) and GetAddr-all by the number of voices (one value each). Unknown command i returns
 * -(i + 1). Command 7 sends ret from its start (not from slot n). */
int ProcBatchEx(const BatchEntry *batch, uint32_t *ret, uint32_t num, uint32_t voice)
{
    int32_t n = 0;   /* s3 */
    for (uint32_t i = 0; i < num; i++, n++) {
        const BatchEntry *b = &batch[i];
        uint16_t e = b->entry;
        uint32_t v = 0;
        switch (b->cmd) {
        case 1:
            if ((e & 0x3e) != 0x3e) { SetParam(e, (uint16_t)b->value); break; }
            for (int k = 0; k < 24; k++)
                if (voice & (1u << k)) { n++; SetParam((uint16_t)((e & 0xffc1) | k << 1), (uint16_t)b->value); }
            n--;
            break;
        case 0x11:
            if ((e & 0x3e) != 0x3e) { v = GetParam(e); break; }
            for (int k = 0; k < 24; k++, n++) {
                if (voice & (1u << k)) v = GetParam((uint16_t)((e & 0xffc1) | k << 1));
                if (ret) ret[n] = v;
            }
            n--;
            break;
        case 2: SetSwitch(e, b->value); break;
        case 0x12: v = GetSwitch(e); break;
        case 3:
            if ((e & 0x7e) != 0x7e) { SetAddr(e, b->value); break; }
            for (int k = 0; k < 24; k++)
                if (voice & (1u << k)) { n++; SetAddr((uint16_t)((e & 0xffc1) | k << 1), b->value); }
            n--;
            break;
        case 0x13:
            if ((e & 0x7e) != 0x7e) { v = GetAddr(e); break; }
            for (int k = 0; k < 24; k++)
                if (voice & (1u << k)) {
                    v = GetAddr((uint16_t)((e & 0xffc1) | k << 1));
                    if (ret) ret[n] = v;
                    n++;
                }
            n--;
            break;
        case 4: SetCoreAttr(e, (uint16_t)b->value); break;
        case 0x14: v = GetCoreAttr(e); break;
        case 5: IopStore(b->value, e); break;
        case 6: S.batch_word = e; v = (uint32_t)SifDmaWait(b->value, &S.batch_word, 4); break;
        case 7: v = (uint32_t)SifDmaWait(b->value, ret, e); break;
        default: return -(int32_t)i - 1;
        }
        if (ret) ret[n] = v;
    }
    return n;
}

/* ---------------------------------------------------------------- Init family */

/* f_1550: IOP-side SPU2 bus setup (base / DPCR / timing registers: no native effect) between CpuSuspendIntr / Resume */
static void InitIop()
{
    int st;
    suspend(&st);
    resume(st);
}

/* f_15e8 */
static void InitHw()
{
    InitIop();
    W(0x7c6, 0x900);
    W(0x7c8, 0x200);
    W(0x7ca, 8);
}

/* f_1e7c: SPU2 reset. Q10: the STATX poll cannot succeed (bit 7 is always set) and times out after 0xf01 reads; the
 * second branch (core 1 STATX) only runs when core 0's bits clear */
static int ResetSpu()
{
    W(0x7c0, 0);
    W(0x7c0, 0x8000);
    W(0x760, 0); W(0x762, 0); W(0x788, 0); W(0x78a, 0);
    W(0x1b0, 0); W(0x5b0, 0); W(0x19a, 0); W(0x59a, 0);
    W(0x19a, 0x8000); W(0x59a, 0x8000);
    W(0x760, 0); W(0x762, 0); W(0x788, 0); W(0x78a, 0);
    uint32_t n = 0;
    uint16_t v = Rd(0x344);
    for (;;) {   /* the counter is bumped in both branch delay slots (and undone once on the core-1 path): +1 either way */
        if (!(v & 0x7ff) && !(Rd(0x744) & 0x7ff)) break;
        if (++n >= 0xf01) break;
        v = Rd(0x344);
    }
    W(0x1a4, 0xffff); W(0x1a6, 0xff); W(0x5a4, 0xffff); W(0x5a6, 0xff);
    W(0x180, 0); W(0x182, 0); W(0x580, 0); W(0x582, 0);
    W(0x184, 0); W(0x186, 0); W(0x584, 0); W(0x586, 0);
    return 0;
}

/* f_4004: silent loop block to 0x2800 by the data port, then voices / KON / KOFF / ENDX. Q6: core 0 gets the READ-BACK of
 * each core-1 register */
static void InitVoices()
{
    static const uint16_t silent[8] = {0x0700, 0, 0, 0, 0, 0, 0, 0};   /* D_05308 */
    W(0x19a, Rd(0x19a) & 0xffcf);
    W(0x1a8, 0);
    W(0x1aa, 0x2800);
    for (int i = 0; i < 8; i++) W(0x1ac, silent[i]);
    W(0x19a, (Rd(0x19a) & 0xffcf) | 0x10);
    if (Rd(0x344) & 0x400)   /* not taken in the baselines */
        for (uint32_t n = 1; n <= 0x1000000 && (Rd(0x344) & 0x400); n++) {}
    W(0x19a, Rd(0x19a) & 0xffcf);
    static const uint16_t vinit[5] = {0, 0, 0x3fff, 0, 0};
    for (uint32_t v = 0; v < 24; v++) {
        for (uint32_t k = 0; k < 5; k++) {
            uint32_t r1 = 0x400 + v * 0x10 + k * 2;
            W(r1, vinit[k]);
            W(r1 - 0x400, Rd(r1));
        }
        uint32_t a1 = 0x5c0 + v * 12;
        W(a1, 0);
        W(a1 - 0x400, Rd(a1));
        W(a1 + 2, 0x2800);
        W(a1 - 0x400 + 2, Rd(a1 + 2));
    }
    W(0x5a0, 0xffff); W(0x1a0, Rd(0x5a0));
    W(0x5a2, 0xff); W(0x1a2, Rd(0x5a2));
    W(0x5a4, 0xffff); W(0x1a4, Rd(0x5a4));
    W(0x5a6, 0xff); W(0x1a6, Rd(0x5a6));
    W(0x342, 0); W(0x340, Rd(0x342));
    W(0x742, 0); W(0x740, Rd(0x742));
}

/* f_1638(flag & 0xf): core defaults. MVOL / EVOL / EEA only for flag 0; AVOL / BVOL always */
static void InitCore(int flag)
{
    W(0x7c0, 0xc032);
    if (!flag) { W(0x19a, 0xc000); W(0x59a, 0xc001); }
    else { W(0x19a, 0xc080); W(0x59a, 0xc081); }
    for (uint32_t c = 0; c < 2; c++) {
        uint32_t b = c * 0x400;
        W(b + 0x188, 0xffff); W(b + 0x18a, 0xff); W(b + 0x190, 0xffff); W(b + 0x192, 0xff);
        W(b + 0x18c, 0xffff); W(b + 0x18e, 0xff); W(b + 0x194, 0xffff); W(b + 0x196, 0xff);
    }
    W(0x198, 0xff0);
    W(0x598, 0xffc);
    if (!flag) {
        W(0x760, 0); W(0x762, 0); W(0x788, 0); W(0x78a, 0);
        W(0x764, 0); W(0x766, 0); W(0x78c, 0); W(0x78e, 0);
        W(0x33c, 0xe); W(0x73c, 0xf);
    }
    W(0x768, 0); W(0x76a, 0);
    W(0x790, 0x7fff); W(0x792, 0x7fff);
    W(0x76c, 0); W(0x76e, 0); W(0x794, 0); W(0x796, 0);
}

/* f_4304: interrupt handlers off, all module state reset (Q7: handlers registered earlier are dropped), reverb work
 * area defaults (flag 0), event flags created once (later Inits set them to 1). Returns 0 or -301 */
static int ResetState(int flag)
{
    int x;
    K.DisableIntr(K.user, 0x24, &x);
    K.DisableIntr(K.user, 0x28, &x);
    K.DisableIntr(K.user, 9, &x);
    K.ReleaseIntrHandler(K.user, 0x24);
    K.ReleaseIntrHandler(K.user, 0x28);
    K.ReleaseIntrHandler(K.user, 9);
    S.cpu_write_done[0] = S.cpu_write_done[1] = 0;
    S.trans_idle[0] = S.trans_idle[1] = 1;
    S.done_unack[0] = S.done_unack[1] = 0;
    S.spu2_handler = 0;
    S.trans_handler[0] = S.trans_handler[1] = 0;
    S.post_hook[0] = S.post_hook[1] = 0;
    S.cookie[0].state = 0; S.cookie[1].state = 1;
    S.cookie[0].arg = 0; S.cookie[1].arg = 0;
    S.spu2_arg = 0;
    S.irq_cb = 0;
    S.trans_cb[0] = S.trans_cb[1] = 0;
    int r = 0;
    if ((flag & 0xf) == 0) {
        memset(S.effect, 0, sizeof S.effect);   /* bzero(D_052a0, 0x28) */
        S.esa_start[0] = 0x1dfff0;
        S.esa_start[1] = 0x1ffff0;
        W(0x2e0, 0xe); W(0x2e2, 0xfff8); W(0x6e0, 0xf); W(0x6e2, 0xfff8);
    }
    EventFlagParam p = {2, 0, 1};   /* EA_MULTI, no option, initial bits 1 */
    for (int c = 0; c < 2; c++) {
        if (S.evf[c] > 0) {
            if (K.QueryIntrContext(K.user)) K.iSetEventFlag(K.user, S.evf[c], 1);
            else K.SetEventFlag(K.user, S.evf[c], 1);
        } else {
            S.evf[c] = K.CreateEventFlag(K.user, &p);
            if (S.evf[c] <= 0) r = -301;
        }
    }
    return r;
}

/* f_4570 */
static void ClearIds()
{
    S.inited = 0;
    S.evf[0] = S.evf[1] = 0;
}

/* f_00b0 */
int ModuleStart()
{
    int st;
    suspend(&st);
    resume(st);   /* RegisterLibraryEntries in between in the original */
    InitIop();
    ClearIds();
    return 0;
}

/* 0x458c */
int Init(int flag)
{
    InitHw();
    if ((flag & 0xf) == 0) ResetSpu();
    int r = ResetState(flag);
    InitVoices();
    InitCore(flag & 0xf);
    K.EnableIntr(K.user, 0x24);
    K.EnableIntr(K.user, 0x28);
    K.EnableIntr(K.user, 9);   /* Enable before Register, as in the original */
    K.RegisterIntrHandler(K.user, 0x24, 1, DmaIsr, &S.cookie[0]);
    K.RegisterIntrHandler(K.user, 0x28, 1, DmaIsr, &S.cookie[1]);
    K.RegisterIntrHandler(K.user, 9, 1, Spu2Isr, S.spu2_arg);
    S.inited = 1;
    return r;
}

/* f_466c (export 2, module stop; NOT RUN): stop both DMA channels, delete the flags (ids are not cleared), handlers off */
int Quit()
{
    DmaCtl(0x0a);
    DmaCtl(0x1a);
    if (S.evf[0] > 0) K.DeleteEventFlag(K.user, S.evf[0]);
    if (S.evf[1] > 0) K.DeleteEventFlag(K.user, S.evf[1]);
    int x;
    K.DisableIntr(K.user, 0x28, &x);
    K.DisableIntr(K.user, 0x24, &x);
    K.DisableIntr(K.user, 9, &x);
    K.ReleaseIntrHandler(K.user, 0x28);
    K.ReleaseIntrHandler(K.user, 0x24);
    K.ReleaseIntrHandler(K.user, 9);
    return 0;
}

}  // namespace sd
