/* hsyn_voice - group H4a of NI6b: the voice register writer. The SPU
 * register program of a voice start (f_3f70, and its SE-stream twin f_5520), the pitch writer f_4408, the volume
 * writer f_4664, and the setters M3's VoiceSetup (f_3bb4) and M5's controllers call: bend f_4610, volume term f_486c,
 * velocity depth f_48a4 with its curve selector f_8edc and the 11 curves of D_0b6d0, the velocity shapes of D_0b6f8
 * (VelShape, M3's f_9064), tremolo / vibrato depth f_4908 / f_49b0, pan base f_4a60, pan law f_4b34, cross-fade f_4c88,
 * the ADSR key-follow helpers f_3a0c / f_3a94 / f_3b38. (The clamps f_3988 / f_39cc are in hsyn_note.cpp.) H4b (REV,
 * unit tests only): the SE-stream voice setup f_52d0 with its setters f_59a0 / f_5a44 (+ the dead 0x5af0) / f_5b9c.
 *
 * Arithmetic is the asm's: 32-bit wrapping products (MulLo), the compilers' magic-number divisions written out as the
 * asm computes them (Div12 / Div63 / Div126 / Div127u: exact integer divisions over every input the callers can give,
 * map section 10, checked by hsyn::UnitTest against plain division), `x < 0 ? x + 2^n - 1 : x` then `>> n` (TShift,
 * truncating), logical shifts where the asm uses srl. The two function-pointer tables are the raw IRX words (module
 * offsets) and dispatch by value, so the original's data-selected targets stay visible:
 *  - D_0b6d0 (f_8edc, 11 entries): index 10 is the first word of D_0b6f8 (f_8f84, identity) - valid, as in the original.
 *  - D_0b6f8 (f_9064 / VelShape, 10 entries): selector 10 reads the next word, 0x04050000 (the first word of the s16
 *    wave table at 0xb720, not relocated), and the original would jalr to IOP address 0x04050000 - unmapped (RAM is
 *    0 .. 0x1fffff, mirrored to 8 MB), so the fetch raises a bus error and the IOP stops; nothing a native can
 *    reproduce. Native: one plat_log, then the identity (x & 0xff, what selector 0 and the >= 11 fall-back give).
 *    Unreachable in Versus (Sset curve ids are all 0).
 *  - the signed / unsigned divisions guarded by `break 7` (curves 6 .. 9, CrossFade) cannot divide by zero for any
 *    voice NoteOn accepts (curves: only a custom velocity table can give x = 0 or x > 0x7f; CrossFade: the window test
 *    of f_65dc excludes it). A zero divisor follows the IOP divide-by-zero rule (iopdiv.h): the interpreter's LO
 *    (div: -1 / 1 by the dividend's sign; divu: 0xffffffff), one plat_log per site, the asm goes on.
 * f_8f9c (velocity shape 2, and inside 3 / 4 / 5): `li a0, 1` at 0x8fb4 overwrites the argument before the quotient
 * test, so x * x / 127 == 0 (x <= 11) returns 1, not the argument ( 5 corrected in the
 * H4a verify). The quotient is masked to 8 bits (x >= 0xb4 wraps).
 *
 * sd calls go through the hsyn_sd.h wrappers, in the asm's order (the KON / VP / VMIX trace order). Unreached in every
 * recording: key-follow ADSR, cross-fade windows, pan mode 1 (signed) and its wrap loops, mono output, custom velocity
 * tables, curves 1 .. 9, shapes 1 .. 9, the inverted volume phase, LFO / tremolo / vibrato terms (LFO outputs are 0 in
 * Versus), the fixed-pitch override, the SE-stream (kind 1 / blkKind 1) branches of f_4408 / f_4664 and f_5520, the
 * sample-less (noise) voice start, the 0x3fff clamps. */
#include "hsyn.h"
#include "hsyn_state.h"
#include "iopdiv.h"
#include "hsyn_sd.h"
#include "../../plat.h"

namespace hsyn {

/* ---------------------------------------------------------------- arithmetic idioms of the asm */

static inline int32_t MulLo(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }   /* mult; mflo */
static inline int32_t MulHi(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * (int64_t)b) >> 32); }   /* mult; mfhi */
static inline int32_t Add(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }   /* addu */
static inline int32_t Sub(int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); }   /* subu */
/* (x < 0 ? x + 2^n - 1 : x) >> n = x / 2^n truncated */
static inline int32_t TShift(int32_t x, int n) { return (x < 0 ? Add(x, (int32_t)((1u << n) - 1)) : x) >> n; }
/* mult n, 0x2aaaaaab; mfhi; sra 1; - (n >> 31) = n / 12 */
static inline int32_t Div12(int32_t n) { return Sub(MulHi(n, 0x2aaaaaab) >> 1, n >> 31); }
/* mult n, 0x82082083; mfhi; + n; sra 6 (126) / sra 5 (63); - (n >> 31) */
static inline int32_t Div126(int32_t n) { return Sub(Add(MulHi(n, (int32_t)0x82082083u), n) >> 6, n >> 31); }
static inline int32_t Div63(int32_t n) { return Sub(Add(MulHi(n, (int32_t)0x82082083u), n) >> 5, n >> 31); }
/* multu m, 0x02040811; (hi + ((m - hi) >> 1)) >> 6 = m / 127 (unsigned) */
static inline uint32_t Div127u(uint32_t m)
{
    uint32_t hi = (uint32_t)(((uint64_t)m * 0x02040811u) >> 32);
    return (hi + ((m - hi) >> 1)) >> 6;
}
/* the signed div of curves 6 .. 9 (break 7 on a zero divisor -> iopdiv rule; 0x80000000 / -1 cannot happen: |n| < 2^24).
 * site: 0 = f_8c60 / f_8ccc (0x8ca0 / 0x8d0c), 1 = p < x (0x8d68 / 0x8e34), 2 = p >= x (0x8db8 / 0x8e8c) */
static int32_t DivCurve(int32_t n, int32_t d, uint32_t fn, int site)
{
    static bool warned[4][3];
    static const uint32_t at[4][3] = {{0x8ca0, 0, 0}, {0x8d0c, 0, 0}, {0, 0x8d68, 0x8db8}, {0, 0x8e34, 0x8e8c}};
    const int f = fn == 0x8c60 ? 0 : fn == 0x8ccc ? 1 : fn == 0x8d38 ? 2 : 3;
    return iopdiv::DivLo(n, d, warned[f][site], "hsyn velocity curve", at[f][site]);
}

/* ---------------------------------------------------------------- ADSR key-follow (REV bodies) */

/* 0x3a0c: one ADSR bit field (value >> shift) & mask += diff * depth / 12, clamped to 0 .. mask */
uint32_t KeyFollowField(uint32_t a0, uint32_t depth, int32_t diff, uint32_t mask, uint32_t shift)
{
    int32_t q = Div12(MulLo(diff, (int8_t)depth));
    if (q == 0) return a0 & 0xffff;
    const uint32_t m = mask & 0xffff;
    shift &= 31;
    int32_t f = Add((int32_t)(((a0 & 0xffff) >> shift) & m), q);
    if (f < 0) f = 0;
    else if ((int32_t)m < f) f = (int32_t)m;
    return ((a0 & ~(m << shift)) | ((uint32_t)f << shift)) & 0xffff;
}
/* 0x3a94: ADSR1 = smpl +0x12 with attack rate (bits 8..14), decay rate (4..7), sustain level (0..3) key-follow, in
 * this order; a field is followed when its depth byte (signed) is non-zero */
uint32_t Adsr1(uint32_t key, uint32_t smpl)
{
    uint32_t v = R16(smpl + 0x12);
    if ((int8_t)R8(smpl + 0x16)) v = KeyFollowField(v & 0xffff, R8(smpl + 0x16), (int32_t)(key - R8(smpl + 0x17)), 0x7f, 8);
    if ((int8_t)R8(smpl + 0x18)) v = KeyFollowField(v & 0xffff, R8(smpl + 0x18), (int32_t)(key - R8(smpl + 0x19)), 0x0f, 4);
    if ((int8_t)R8(smpl + 0x1e)) v = KeyFollowField(v & 0xffff, R8(smpl + 0x1e), (int32_t)(key - R8(smpl + 0x1f)), 0x0f, 0);
    return v & 0xffff;
}
/* 0x3b38: ADSR2 = smpl +0x14 with sustain rate (bits 6..12) and release rate (0..4) key-follow */
uint32_t Adsr2(uint32_t key, uint32_t smpl)
{
    uint32_t v = R16(smpl + 0x14);
    if ((int8_t)R8(smpl + 0x1a)) v = KeyFollowField(v & 0xffff, R8(smpl + 0x1a), (int32_t)(key - R8(smpl + 0x1b)), 0x7f, 6);
    if ((int8_t)R8(smpl + 0x1c)) v = KeyFollowField(v & 0xffff, R8(smpl + 0x1c), (int32_t)(key - R8(smpl + 0x1d)), 0x1f, 0);
    return v & 0xffff;
}

/* ---------------------------------------------------------------- voice start (f_3f70 / f_5520) */

/* one VMIX shadow bit of this voice: set iff the mix bit is set */
static inline void MixBit(uint32_t &w, bool on, uint32_t bit)
{
    if (on) w |= bit;
    else w &= ~bit;
}
/* the common body: se = f_5520 (SE-stream voice: ADSR words and mix bits from the parameter record v[0x64] at +0xe /
 * +0x10 / +0xd instead of the key-followed Smpl words and Smpl +0x29). VOLL / VOLR always first, also for a voice
 * without sample; the NON shadow is cleared for sample voices and set for noise voices; the four VMIX shadows are
 * edited before the five SetSwitch calls (NON, L, R, EL, ER); the KON bit is only queued (f_37c4 flushes it). */
static void Start(uint32_t va, bool se)
{
    Bss &g = S();
    Voice *v = V(va);
    const uint32_t idx = v->idx, reg = v->core | (idx << 1), bit = 1u << (idx & 31);
    const uint32_t rec = se ? v->prog : v->smpl;
    VolumeWrite(va);
    if (v->vagi != 0) {
        g.maskC[v->core] &= ~bit;
        PitchWrite(va);
        SdSetAddr(reg | 0x2040, v->bdBase + R32(v->vagi));
        if (se) {
            SdSetParam(reg | 0x300, R16(rec + 0xe));
            SdSetParam(reg | 0x400, R16(rec + 0x10));
        } else {
            SdSetParam(reg | 0x300, Adsr1(v->key, rec) & 0xffff);
            SdSetParam(reg | 0x400, Adsr2(v->key, rec) & 0xffff);
        }
        if (!(R8(v->vagi + 6) & 1)) v->flags |= 1;   /* write-only flag (no reader in the module) */
    } else {
        uint32_t key = v->key;
        if (key >= 0x40) key = 0x3f;
        SdSetCoreAttr(v->core | 8, key);   /* noise clock */
        g.maskC[v->core] |= bit;
    }
    SdSetSwitch(v->core | 0x1400, g.maskC[v->core]);   /* NON */
    const uint32_t mo = se ? 0xd : 0x29;
    MixBit(g.vmix[v->core].l, R8(rec + mo) & 1, bit);
    MixBit(g.vmix[v->core].r, R8(rec + mo) & 2, bit);
    MixBit(g.vmixE[v->core].l, R8(rec + mo) & 4, bit);
    MixBit(g.vmixE[v->core].r, R8(rec + mo) & 8, bit);
    SdSetSwitch(v->core | 0x1800, g.vmix[v->core].l);
    SdSetSwitch(v->core | 0x1a00, g.vmix[v->core].r);
    SdSetSwitch(v->core | 0x1900, g.vmixE[v->core].l);
    SdSetSwitch(v->core | 0x1b00, g.vmixE[v->core].r);
    g.maskKon[v->core] |= bit;
    g.konCore = v->core;
    g.konIdx = v->idx;
    iopk::SysClock clk;
    iopk::GetSystemTime(&clk);
    iopk::SysClock2USec(&clk, &g.konSec, &g.konUs);
    v->endxCount = 3;
    v->flags &= 0xfff7;   /* no longer pending */
    if (g.statBuf) {
        uint8_t &f = StatFlag(v->core, v->idx);
        f = (uint8_t)((f & 0x8f) | 0x20);   /* state 2: started */
    }
}
/* 0x3f70 */
void VoiceStart(uint32_t va) { Start(va, false); }
/* 0x5520 (blkKind 1; unreached) */
void VoiceStartSe(uint32_t va) { Start(va, true); }

/* ---------------------------------------------------------------- pitch (f_4408) */

/* 0x4408: the pitch SUM (LFO 1 x vibrato depth >> 14, bend, portamento, SE ramp) is the cache; on a change: fixed
 * pitch override (flag 0x400 clear), else coarse note key + prog[8] + tone[0x12] and fine prog[9] + smpl[0xc] +
 * tone[0x13] (SE-stream: centre = key, note = key + rec[5], fine = rec[6]); a fixed pitch with flag 0x400 re-derives
 * note / fine with Pitch2Note. Negative fine totals are floor-normalised, non-negative ones passed as they are;
 * Note2Pitch(centre, 0, note, fine) times the sample-rate ratio >> 16, clamped to 0x3fff (the override too). */
void PitchWrite(uint32_t va)
{
    Voice *v = V(va);
    const int32_t o = v->lfo1.out;
    int32_t s0 = TShift(MulLo(o >= 0 ? v->vibDepth[0] : v->vibDepth[1], o), 14);
    s0 = Add(s0, Add(v->bendTerm, v->porta));
    if (v->kind == 1 && v->ramp1.state != 0) s0 = Add(s0, (int16_t)v->ramp1.out);
    if (s0 == v->pitchCache) return;
    const uint32_t fixed = v->fixedPitch;
    uint32_t pitch;
    if (fixed != 0 && !(v->flags & 0x400)) {
        v->pitchCache = (int32_t)fixed;
        pitch = fixed;
    } else {
        v->pitchCache = s0;
        uint32_t centre;
        int32_t note, fine0, a0;
        if (v->blkKind == 1) {
            const uint32_t rec = v->prog;
            centre = v->key;
            note = (int32_t)v->key + (int8_t)R8(rec + 5);
            fine0 = a0 = (int8_t)R8(rec + 6);
        } else {
            const uint32_t prog = v->prog, tone = v->tone, smpl = v->smpl;
            centre = R8(smpl + 0xb);
            fine0 = (int8_t)R8(smpl + 0xc);
            note = (int32_t)v->key + (int8_t)R8(prog + 8) + (int8_t)R8(tone + 0x12);
            a0 = (int8_t)R8(prog + 9) + fine0 + (int8_t)R8(tone + 0x13);
        }
        if (fixed != 0) {
            uint32_t r = SdPitch2Note(centre, (uint32_t)fine0 & 0xffff, fixed) & 0xffff;
            note = (int32_t)(r >> 8);
            a0 = (int32_t)(r & 0xff);
        }
        s0 = Add(s0, a0);
        if (s0 < 0) {
            int32_t q = Add(s0, 0x7f) >> 7;
            note = Add(note, q);
            s0 = Sub(s0, (int32_t)((uint32_t)q << 7));
            if (s0 != 0) {
                s0 += 0x80;
                note -= 1;
            }
        }
        pitch = SdNote2Pitch(centre, 0, (uint32_t)note & 0xffff, (uint32_t)s0);
        pitch = ((pitch & 0xffff) * v->rateRatio) >> 16;
    }
    if (pitch >= 0x4000) pitch = 0x3fff;
    SdSetParam(v->core | ((uint32_t)v->idx << 1) | 0x200, pitch & 0xffff);
}

/* ---------------------------------------------------------------- volume (f_4664, f_486c) and bend (f_4610) */

/* 0x4664: scaled volume (+ SE ramp 2) x port volume / 256, x tremolo (LFO 2 output x depth / 128 + 0x4000) / 16384
 * (>= 0), x pan gain >> 7 per side (SE: the segment stepper's gains), clamp 0x3fff; written only when L or R differs
 * from the cache (0xffff after VoiceSetup); phase 0 writes the 15-bit negation (the cache keeps the positive value) */
void VolumeWrite(uint32_t va)
{
    Voice *v = V(va);
    const bool seObj = v->kind == 1 && v->seg.state != 0;
    int32_t a1 = v->volScaled;
    if (v->kind == 1 && v->ramp2.state != 0) a1 = Add(a1, (int16_t)v->ramp2.out);
    a1 = TShift(MulLo(a1, R16(v->blk14)), 8);
    const int32_t s = v->lfo2.out;
    const int32_t t = TShift(MulLo(s, s >= 0 ? v->tremDepth[0] : v->tremDepth[1]), 7) + 0x4000;
    int32_t a2 = TShift(MulLo(a1, t), 14);
    if (a2 < 0) a2 = 0;
    uint32_t gl = seObj ? v->seg.panL : v->panGainL;
    uint32_t L = (gl * (uint32_t)a2) >> 7;
    if (L >= 0x4000) L = 0x3fff;
    uint32_t gr = seObj ? v->seg.panR : v->panGainR;
    uint32_t R = (gr * (uint32_t)a2) >> 7;
    if (R >= 0x4000) R = 0x3fff;
    if (L == v->lastVolL && R == v->lastVolR) return;
    uint32_t ph = v->volPhase;
    v->lastVolL = (uint16_t)L;
    v->lastVolR = (uint16_t)R;
    const uint32_t reg = v->core | ((uint32_t)v->idx << 1);
    if (seObj) ph = v->seg.w34 & 0xff;
    if (ph != 0) {
        SdSetParam(reg, L & 0xffff);
        SdSetParam(reg | 0x100, R & 0xffff);
    } else {
        SdSetParam(reg, (0u - L) & 0x7fff);
        SdSetParam(reg | 0x100, (0u - R) & 0x7fff);
    }
}
/* 0x486c: v[0x18] = ch vol x expression x volume term >> 14 (unsigned, low 16 bits kept) */
void UpdateVolume(uint32_t va)
{
    if (g_sdMock) M4Log(0x486c, va, 0);
    Voice *v = V(va);
    uint32_t a = (uint32_t)Ch(v->ch)->vol * v->expr;
    a *= v->volTerm;
    v->volScaled = (uint16_t)(a >> 14);
}
/* 0x4610: bend term (1/128 semitone) = (s16) bend x the tone's range (+8 up, +6 down) / 8192 truncated; no tone: no
 * store */
void SetBend(uint32_t va, int32_t bend)
{
    if (g_sdMock) M4Log(0x4610, va, bend);
    Voice *v = V(va);
    const uint32_t tone = v->tone;
    if (tone == 0) return;
    const int32_t b = (int16_t)bend;
    const int32_t range = b >= 0 ? R16(tone + 8) : R16(tone + 6);
    v->bendTerm = TShift(MulLo(range, b), 13);
}

/* ---------------------------------------------------------------- velocity curves (f_8edc + D_0b6d0) */

/* A(x) = (x - 1) << 15 / 126; S(a) = a * a / 16384 (32-bit product, truncated) */
static inline int32_t CurveA(uint32_t x) { return Div126((int32_t)(((x & 0xff) - 1) << 15)); }
static inline int32_t CurveS(int32_t a) { return TShift(MulLo(a, a), 14); }
/* D_0b6d0: f_89d0, f_8a04, f_8a38, f_8ab0, f_8b3c, f_8bd0, f_8c60, f_8ccc, f_8d38, f_8e04 | f_8f84 */
static const uint32_t kCurveTbl[11] = {0x89d0, 0x8a04, 0x8a38, 0x8ab0, 0x8b3c, 0x8bd0, 0x8c60, 0x8ccc, 0x8d38, 0x8e04, 0x8f84};
/* the curve at module offset fn, x = velocity (after the table), p = the depth parameter p1 (both & 0xff) */
static int32_t Curve(uint32_t fn, uint32_t x, uint32_t p)
{
    x &= 0xff;
    p &= 0xff;
    const int32_t d = (int32_t)x - (int32_t)p;
    switch (fn) {
    case 0x89d0: return Div126((int32_t)((uint32_t)d << 16));
    case 0x8a04: return Div126((int32_t)((uint32_t)-d << 16));
    case 0x8a38: return TShift(Sub(MulLo(CurveA(x), CurveA(x)), MulLo(CurveA(p), CurveA(p))), 14);
    case 0x8ab0: return Sub(Sub(0x10000, CurveS(CurveA(x))), Sub(0x10000, CurveS(CurveA(p))));
    case 0x8b3c: return Sub(Sub(0x10000, CurveS(Sub(0x10000, CurveA(x)))), Sub(0x10000, CurveS(Sub(0x10000, CurveA(p)))));
    case 0x8bd0: return Sub(CurveS(Sub(0x10000, CurveA(x))), CurveS(Sub(0x10000, CurveA(p))));
    case 0x8c60:
    case 0x8ccc: {
        if (x == p) return 0;
        const int32_t den = p < x ? 0x7f - (int32_t)p : (int32_t)p - 1;
        return DivCurve((int32_t)((uint32_t)(fn == 0x8c60 ? d : -d) << 16), den, fn, 0);
    }
    case 0x8d38:
    case 0x8e04: {
        if (x == p) return 0;
        const int32_t n = (int32_t)((uint32_t)d << 15);
        if (p < x) {
            int32_t q = DivCurve(n, 0x7f - (int32_t)p, fn, 1);
            if (fn == 0x8d38) return CurveS(q);
            return Sub(0x10000, CurveS(Sub(0x8000, q)));
        }
        int32_t q = DivCurve(n, (int32_t)p - 1, fn, 2);
        if (fn == 0x8d38) return -CurveS(q);
        return Add(CurveS(Add(q, 0x8000)), (int32_t)0xffff0000u);
    }
    case 0x8f84: return (int32_t)x;
    }
    return 0;
}
/* 0x8edc: vel 0 counts as 1; custom table row ((p2 & 0xff) >> 4) - 1 < n remaps vel and p1 (rows of 126 bytes indexed
 * value - 1, as f_9064); curve p2 & 0xf (>= 11 -> 0) of (x, p1) */
int32_t VelCurveDepth(uint32_t vel, uint32_t p1, uint32_t p2, uint32_t tbl, int32_t n)
{
    uint32_t x = vel;
    if ((x & 0xff) == 0) x = 1;
    const int32_t k = (int32_t)((p2 & 0xff) >> 4) - 1;
    if (k != -1 && k < n) {
        tbl += (uint32_t)k * 126;
        x = R8(tbl + (x & 0xff) - 1);
        p1 = R8(tbl + (p1 & 0xff) - 1);
    }
    uint32_t s = p2 & 0xf;
    if (s >= 11) s = 0;
    return Curve(kCurveTbl[s], x & 0xff, p1 & 0xff);
}
/* 0x48a4: the voice's velocity through curve (p1, p2) of its port block's tables, times depth (s8) / 65536 */
int32_t VelDepth(uint32_t va, int32_t depth, uint32_t p1, uint32_t p2)
{
    if (g_sdMock) M4Log(0x48a4, va, depth, (int32_t)p1, (int32_t)p2);
    Voice *v = V(va);
    const uint32_t blk = Ch(v->ch)->blk;
    int32_t r = VelCurveDepth(v->vel, p1 & 0xff, p2 & 0xff, R32(blk + 0x10), (int32_t)R32(blk + 0xc));
    return TShift(MulLo(r, (int8_t)depth), 16);
}

/* ---------------------------------------------------------------- velocity shapes (D_0b6f8, M3's f_9064) */

/* 0x8f9c: x * x / 127 masked to 8 bits; 1 when the quotient is 0 (x <= 11) */
static uint32_t ShapeSq(uint32_t x)
{
    x &= 0xff;
    uint32_t r = Div127u(x * x);
    return r != 0 ? (r & 0xff) : 1;
}
/* D_0b6f8[0..9] + the word after it (selector 10: the wave table's first word, not code) */
static const uint32_t kShapeTbl[11] = {0x8f84, 0x8f8c, 0x8f9c, 0x8fe0, 0x9008, 0x903c, 0x8f84, 0x8f8c, 0x8f9c, 0x8fe0, 0x04050000};
uint32_t VelShape(uint32_t sel, uint32_t x)
{
    if (g_sdMock) M4Log(0x6f8, 0, (int32_t)sel, (int32_t)x);
    const uint32_t fn = sel < 11 ? kShapeTbl[sel] : 0;
    switch (fn) {
    case 0x8f84: return x & 0xff;
    case 0x8f8c: return (0x80 - x) & 0xff;
    case 0x8f9c: return ShapeSq(x);
    case 0x8fe0: return (0x80 - ShapeSq(x & 0xff)) & 0xff;
    case 0x9008: return (0x80 - ShapeSq((0x80 - x) & 0xff)) & 0xff;
    case 0x903c: return ShapeSq((0x80 - x) & 0xff) & 0xff;
    }
    static bool logged;
    if (!logged) {
        logged = true;
        plat_log("hsyn: velocity shape %u jumps to %08x (data, the original stops the IOP); identity used", sel, fn);
    }
    return x & 0xff;
}

/* ---------------------------------------------------------------- tremolo / vibrato depth (CC 2 / CC 1) */

static inline int32_t Clamp8(int32_t s) { return s >= 0x80 ? 0x7f : (s < -0x80 ? -0x80 : s); }
static inline int32_t Clamp16(int32_t s) { return 0x7fff < s ? 0x7fff : (s < -0x8000 ? -0x8000 : s); }
/* 0x4908: v[0x4f] / v[0x50] = clamp8(cc2 x prog[0x22] / [0x23] / 128 + base) */
void SetTremoloDepth(uint32_t va, uint32_t cc2)
{
    if (g_sdMock) M4Log(0x4908, va, (int32_t)cc2);
    Voice *v = V(va);
    const int32_t c = (int32_t)(cc2 & 0xff);
    v->tremDepth[0] = (int8_t)Clamp8(TShift(MulLo(c, (int8_t)R8(v->prog + 0x22)), 7) + v->tremBase[0]);
    v->tremDepth[1] = (int8_t)Clamp8(TShift(MulLo(c, (int8_t)R8(v->prog + 0x23)), 7) + v->tremBase[1]);
}
/* 0x49b0: v[0x20] / v[0x22] = clamp16(cc1 x prog s16 [0x1c] / [0x1e] / 128 + base) */
void SetVibratoDepth(uint32_t va, uint32_t cc1)
{
    if (g_sdMock) M4Log(0x49b0, va, (int32_t)cc1);
    Voice *v = V(va);
    const int32_t c = (int32_t)(cc1 & 0xff);
    v->vibDepth[0] = (int16_t)Clamp16(TShift(MulLo(c, (int16_t)R16(v->prog + 0x1c)), 7) + v->vibBase[0]);
    v->vibDepth[1] = (int16_t)Clamp16(TShift(MulLo(c, (int16_t)R16(v->prog + 0x1e)), 7) + v->vibBase[1]);
}

/* ---------------------------------------------------------------- pan (f_4a60 / f_4b34) and cross-fade (f_4c88) */

/* 0x4a60: pan position from prog[7] + tone[0x11] + smpl[0xd] (- 0x80) + the key-follow term. Mode 1 (signed): wrapped
 * into -0x80 .. 0x7f in steps of 0xff, v[0x53] untouched; mode 0: magnitudes, clamped 0 .. 0x7f, v[0x53] = 1. */
void PanBase(uint32_t va, int32_t kf)
{
    if (g_sdMock) M4Log(0x4a60, va, kf);
    Voice *v = V(va);
    int32_t a2 = (int8_t)R8(v->prog + 7), t0 = (int8_t)R8(v->tone + 0x11), a3 = (int8_t)R8(v->smpl + 0xd);
    if (v->panMode) {
        int32_t a = Add(a2 - 0x80, Add(t0 + a3, kf));
        if (a >= 0x80) {
            a -= 0xff;
            while (a >= 0x80) a -= 0xff;
        }
        if (a < -0x80) {
            a += 0xff;
            while (a < -0x80) a += 0xff;
        }
        v->panPos = (int16_t)a;
        return;
    }
    if (a2 < 0) a2 = -a2;
    if (t0 < 0) t0 = -t0;
    if (a3 < 0) a3 = -a3;
    int32_t a = Add(a2 - 0x80, Add(t0 + a3, kf));
    if (a < 0) a = 0;
    else if (a >= 0x80) a = 0x7f;
    v->volPhase = 1;
    v->panPos = (int16_t)a;
}
/* 0x4b34: gains 0x80 / 0x80 (mono output: nothing more, v[0x53] stale); pan clamped to +-0x3f; mode 1 folds the signed
 * position (sign -> v[0x53]); a = offset from centre: a > 0 attenuates L, a < 0 attenuates R, (0x3f - |a|) * 128 / 63,
 * 0 from |a| = 63; v[0x5a] = a (write-only) */
void PanLaw(uint32_t va, int32_t pan)
{
    if (g_sdMock) M4Log(0x4b34, va, pan);
    Voice *v = V(va);
    v->panGainR = 0x80;
    v->panGainL = 0x80;
    v->panLow = 0;
    if (S().mono != 0) return;
    int32_t c = (int8_t)pan;
    if (c >= 0x40) c = 0x3f;
    else if (c < -0x3f) c = -0x3f;
    int32_t a;
    if (v->panMode) {
        a = v->panPos;
        uint32_t sign = 1;
        if (a < 0) {
            a = -a;
            sign = 0;
        }
        a += c;
        if (a < 0) {
            sign ^= 1;
            a = -a;
        } else if (a >= 0x80) {
            sign ^= 1;
            a = 0x100 - a;
        }
        v->volPhase = (uint8_t)sign;
        a -= 0x40;
    } else {
        a = c + v->panPos - 0x40;
    }
    if (a > 0) v->panGainL = a >= 0x3f ? 0 : (uint8_t)Div63((0x3f - a) << 7);
    else if (a < 0) v->panGainR = a < -0x3e ? 0 : (uint8_t)Div63((a + 0x3f) << 7);
    v->panLow = (uint8_t)a;
}
/* one cross-fade factor r * num / den (divu, iopdiv rule: den 0 -> 0xffffffff); key 0 = the velocity window's divu
 * (0x4d20), 1 = the key window's (0x4dcc) */
static uint32_t FadeDiv(uint32_t n, uint32_t den, int key)
{
    static bool warned[2];
    return iopdiv::DivuLo(n, den, warned[key], "hsyn CrossFade window", key ? 0x4dccu : 0x4d20u);
}
/* 0x4c88: 16.16 factor: velocity window of the Smpl (lo +2 / centre +3 / hi +4, bit 7 = fade enabled), then the key
 * window of the tone (+2 / +3 / +4); unsigned arithmetic (mult lo, divu) */
uint32_t CrossFade(uint32_t va)
{
    if (g_sdMock) M4Log(0x4c88, va, 0);
    Voice *v = V(va);
    const uint32_t smpl = v->smpl, tone = v->tone;
    uint32_t r = 0x10000;
    uint32_t c = R8(smpl + 3);
    if (v->vel < c) {
        uint32_t lo = R8(smpl + 2);
        if ((lo & 0x80) && (lo & 0x7f) != c) {
            uint32_t l = (lo & 0x7f) - 1;
            r = FadeDiv(((uint32_t)v->vel - l) << 16, c - l, 0);
        }
    } else {
        uint32_t hi = R8(smpl + 4);
        if ((hi & 0x80) && (hi & 0x7f) != c) {
            uint32_t h = (hi & 0x7f) + 1;
            r = FadeDiv((h - v->vel) << 16, h - c, 0);
        }
    }
    c = R8(tone + 3);
    if (v->key < c) {
        uint32_t lo = R8(tone + 2);
        if ((lo & 0x80) && (lo & 0x7f) != c) {
            uint32_t l = (lo & 0x7f) - 1;
            r = FadeDiv(r * ((uint32_t)v->key - l), c - l, 1);
        }
    } else {
        uint32_t hi = R8(tone + 4);
        if ((hi & 0x80) && (hi & 0x7f) != c) {
            uint32_t h = (hi & 0x7f) + 1;
            r = FadeDiv(r * (h - v->key), h - c, 1);
        }
    }
    return r;
}

/* ---------------------------------------------------------------- H4b: the SE-stream voice setup
 * pe = v[0x64] is the SE parameter record (not an HD Prog entry). Callers: M6's f_8004 only (unreached). */

/* 0x59a0: f_4908 with pe[0x20] / [0x21] */
void SetTremoloDepthSe(uint32_t va, uint32_t cc2)
{
    if (g_sdMock) M4Log(0x59a0, va, (int32_t)cc2);
    Voice *v = V(va);
    const int32_t c = (int32_t)(cc2 & 0xff);
    v->tremDepth[0] = (int8_t)Clamp8(TShift(MulLo(c, (int8_t)R8(v->prog + 0x20)), 7) + v->tremBase[0]);
    v->tremDepth[1] = (int8_t)Clamp8(TShift(MulLo(c, (int8_t)R8(v->prog + 0x21)), 7) + v->tremBase[1]);
}
/* 0x5a44: f_49b0 byte for byte (pe s16 [0x1c] / [0x1e]) */
void SetVibratoDepthSe(uint32_t va, uint32_t cc1)
{
    if (g_sdMock) M4Log(0x5a44, va, (int32_t)cc1);
    Voice *v = V(va);
    const int32_t c = (int32_t)(cc1 & 0xff);
    v->vibDepth[0] = (int16_t)Clamp16(TShift(MulLo(c, (int16_t)R16(v->prog + 0x1c)), 7) + v->vibBase[0]);
    v->vibDepth[1] = (int16_t)Clamp16(TShift(MulLo(c, (int16_t)R16(v->prog + 0x1e)), 7) + v->vibBase[1]);
}
/* 0x5af0 (inside f_5a44's heuristic range, no caller, no table entry: dead): the same with a u16 argument */
void SetVibratoDepthSe16(uint32_t va, uint32_t cc1)
{
    Voice *v = V(va);
    const int32_t c = (int32_t)(cc1 & 0xffff);
    v->vibDepth[0] = (int16_t)Clamp16(TShift(MulLo(c, (int16_t)R16(v->prog + 0x1c)), 7) + v->vibBase[0]);
    v->vibDepth[1] = (int16_t)Clamp16(TShift(MulLo(c, (int16_t)R16(v->prog + 0x1e)), 7) + v->vibBase[1]);
}
/* 0x5b9c: pan position = (s8)pe[4]; mode 1 keeps it (the f_4a60 wrap loops cannot fire for an s8), mode 0 takes the
 * magnitude (-0x80 -> 0x7f) and sets v[0x53] = 1 */
void PanBaseSe(uint32_t va)
{
    if (g_sdMock) M4Log(0x5b9c, va, 0);
    Voice *v = V(va);
    int32_t a = (int8_t)R8(v->prog + 4);
    if (v->panMode) {
        if (a >= 0x80) {
            a -= 0xff;
            while (a >= 0x80) a -= 0xff;
        }
        if (a < -0x80) {
            a += 0xff;
            while (a < -0x80) a += 0xff;
        }
        v->panPos = (int16_t)a;
        return;
    }
    if (a < 0) a = -a;
    if (a >= 0x80) a = 0x7f;
    v->volPhase = 1;
    v->panPos = (int16_t)a;
}

/* 0x52d0: f_3bb4 for SE-stream voices, in asm order: the two LFO setups through f_9830 (record offsets + 4), depths
 * without key-follow (ClampAdd(x, 0, depth)), the SE tremolo / vibrato setters, volume term = pe[3] x curve (no >> 14,
 * no cross-fade), pan from pe[4] and the voice's own override (0x80 = centre; never the channel's CC 10), expression
 * = the channel's (no seVol override), caches invalidated, pitch cache / ratio when there is a sample, bend term 0
 * (no SetBend). */
void VoiceSetupSe(uint32_t va)
{
    Bss &g = S();
    Voice *v = V(va);
    const uint32_t pe = v->prog;
    const Channel *ch = Ch(v->ch);
    const uint32_t blk = ch->blk;
    LfoSetupSe(va + 0x78, pe, R32(blk + 8), (int32_t)R32(blk + 4), R8(pe + 0x31) & 7, R16(pe + 0x22), R16(pe + 0x26),
               g.tickLen, 0);
    int32_t s2 = VelDepth(va, (int8_t)R8(pe + 0x2a), R8(pe + 0x2c), R8(pe + 0x2e));
    v->vibBase[0] = ClampAdd16((int16_t)R16(pe + 0x1c), 0, s2);
    v->vibBase[1] = ClampAdd16((int16_t)R16(pe + 0x1e), 0, s2);
    LfoSetupSe(va + 0xa0, pe, R32(blk + 8), (int32_t)R32(blk + 4), R8(pe + 0x31) & 0x70, R16(pe + 0x24), R16(pe + 0x28),
               g.tickLen, 1);
    s2 = VelDepth(va, (int8_t)R8(pe + 0x2b), R8(pe + 0x2d), R8(pe + 0x2f));
    v->tremBase[0] = ClampAdd8((int8_t)R8(pe + 0x20), 0, s2);
    v->tremBase[1] = ClampAdd8((int8_t)R8(pe + 0x21), 0, s2);
    SetTremoloDepthSe(va, ch->cc2);
    SetVibratoDepthSe(va, ch->cc1);
    const uint32_t c = VelCurve(v->vel, v->velCurve, R32(blk + 0x10), (int32_t)R32(blk + 0xc)) & 0xff;
    v->volTerm = (uint16_t)(R8(pe + 3) * c);
    v->panMode = R8(pe + 0xc) & 1;
    PanBaseSe(va);
    int32_t pan = 0;
    if (v->sePan != 0x80) {
        int32_t a = (int32_t)v->sePan - 0x40;
        if ((int8_t)a < -0x3f) a = -0x3f;
        pan = (int8_t)a;
    }
    PanLaw(va, pan);
    v->expr = ch->expr;
    UpdateVolume(va);
    v->lastVolR = 0xffff;
    v->lastVolL = 0xffff;
    if (v->vagi != 0) {
        v->pitchCache = 0x7fffffff;
        v->bendTerm = 0;
        v->rateRatio = (uint32_t)(((uint64_t)((uint32_t)R16(v->vagi + 4) << 16) * 0x057619f1u) >> 32) >> 10;
    }
}

}  // namespace hsyn
