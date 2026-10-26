/* hsyn_fe - group H5R of NI6b: the "fe" SE-stream parser of modhsyn
 * (f_278c, picked by StreamDispatch for port blocks with flags bit 0) with its command handlers, the six SE voice selects
 * (f_20e0, f_2208, f_234c, f_2478, f_2560, f_2648) and the two ramp setters f_a748 (pitch, voice + 0xc8) / f_a8c0
 * (volume, voice + 0xdc). The 0xb0 kinds 0..5 / 0xc / 0xd end in the f_ac40 switch (SeSegCmd, hsyn_se.cpp); notes and
 * releases go to the H6 SE engine (SeNoteOn / SeNoteOff / SeReleaseById / SeReleaseMasked).
 *
 * REV: the fe flavour is produced only by modsein (SeStreamOn) after SYTH's RPC 0x11600, which Versus never sends: exec 0
 * in all nine iopcov runs, no recorded stream contains it. Checked by hsyn::UnitTest (h5r_tests). Module offsets in the
 * function comments. Divides follow the IOP divide-by-zero rule (iopdiv.h): x / 0 gives the interpreter's LO, one
 * plat_log per site, the function goes on as the asm. */
#include "hsyn.h"
#include "hsyn_state.h"
#include "../../plat.h"
#include "iopdiv.h"

namespace hsyn {

static uint32_t s_parseSe;
uint32_t ParseSeCalls() { return s_parseSe; }
void ParseSeReset() { s_parseSe = 0; }

/* 0x20ac: a VLQ at *pp (7 bits per byte, bit 7 = more), no end check */
static uint32_t ReadVlq(uint32_t &p)
{
    uint32_t acc = 0, b;
    do {
        b = R8(p);
        p++;
        acc = (acc << 7) + (b & 0x7f);
    } while (b & 0x80);
    return acc;
}

/* the voice select shared by f_20e0 .. f_2648: only when flags bit 0 is set; channel 0's lists 1 and 3 (pseudo-nodes
 * blk + 0x10 / blk + 0x20; list 2 and the other channels are not searched), each from the tail (prev links), the prev
 * read after the action; match id (word), b58 == bank (word compare), prog / key bytes. act returns false to stop
 * (a trap) */
template <class Act> static void SeSelect(uint32_t blk, uint32_t id, uint32_t bank, uint32_t a, uint32_t b, Act act)
{
    if (!(R8(blk + 2) & 1)) return;
    const uint32_t P[2] = {blk + 0x10, blk + 0x20};
    for (int i = 0; i < 2; i++) {
        for (uint32_t s = Lw(P[i] + 0xc); s != P[i]; s = Lw(s + 0xc)) {
            const Voice *v = V(s);
            if (v->id == id && (uint32_t)v->b58 == bank && v->progIdx == (a & 0xff) && v->key == (b & 0xff))
                if (!act(s)) return;
        }
    }
}

/* 0xa748: the pitch ramp (voice + 0xc8, stepped by SeRamp1Step): n = ticks - 1 (>= 1), delta = val16 * 128 / 100
 * (signed magic = plain /100), slope = (delta & 0xffff) << 8 / n; kind 0xf negates both */
static int SePitchRamp(uint32_t va, uint32_t vlq, uint32_t val16, uint32_t kind)
{
    const uint32_t t = S().tickLen;
    int32_t n = (int32_t)(IOP_DIVU(vlq << 8, t, 0xa758, "hsyn SePitchRamp tickLen 0") - 1u);   /* t 0: -2 -> n 1 */
    int32_t s = (int32_t)((val16 & 0xffff) << 7) / 100;
    if (n <= 0) n = 1;
    int32_t q = (int32_t)((uint32_t)(s & 0xffff) << 8) / n;
    if (kind == 0xf) {
        q = (int32_t)(0u - (uint32_t)q);
        s = (int32_t)(0u - (uint32_t)s);
    }
    SeRamp &r = V(va)->ramp1;
    r.slope = (uint32_t)q;
    r.count = 1;
    r.state = 1;
    r.delta = (uint16_t)s;
    r.len = n;
    r.base = r.out;
    return 0;
}

/* 0xa8c0: the volume ramp (voice + 0xdc, SeRamp2Step): target = c * curve(c) * (ch.vol * ch.expr) >> 14 (u16), from
 * volScaled + (s16)out; slope = (d << 16) / n */
static int SeVolRamp(uint32_t va, uint32_t vlq, uint32_t c)
{
    const uint32_t t = S().tickLen;
    int32_t n = (int32_t)(IOP_DIVU(vlq << 8, t, 0xa8d8, "hsyn SeVolRamp tickLen 0") - 1u);
    Voice *v = V(va);
    const uint32_t ch = v->ch, blk = Ch(ch)->blk;
    if (n <= 0) n = 1;
    const uint32_t c8 = c & 0xff;
    const uint32_t r = VelCurve(c8, v->velCurve, R32(blk + 0x10), (int32_t)R32(blk + 0xc)) & 0xff;
    const uint32_t target = ((c8 * r * ((uint32_t)Ch(ch)->vol * Ch(ch)->expr)) >> 14) & 0xffff;
    SeRamp &g = v->ramp2;
    const int32_t d = (int32_t)target - ((int32_t)v->volScaled + (int16_t)g.out);
    const int32_t q = (int32_t)((uint32_t)d << 16) / n;
    g.len = n;
    g.base = g.out;
    g.count = 1;
    g.state = 1;
    g.delta = (uint16_t)d;
    g.slope = (uint32_t)q;
    return 0;
}

/* 0x20e0 (kind 7) */
static void SelVolRamp(uint32_t blk, uint32_t vlq, uint32_t bank, uint32_t a, uint32_t b, uint32_t c, uint32_t id)
{
    SeSelect(blk, id, bank, a, b, [&](uint32_t va) { SeVolRamp(va, vlq, c & 0xff); return true; });
}

/* 0x2208 (kinds 0..5, 0xc, 0xd): the f_ac40 switch with c1 as a byte, c2 as (s8) on the stack (read back with lbu) */
static void SelSeg(uint32_t blk, uint32_t kind, uint32_t val, uint32_t bank, uint32_t a, uint32_t b, uint32_t c1, uint32_t c2,
                   uint32_t id)
{
    SeSelect(blk, id, bank, a, b, [&](uint32_t va) { SeSegCmd(va, kind, val, c1 & 0xff, c2 & 0xff); return true; });
}

/* 0x234c (kinds 0xe, 0xf) */
static void SelPitchRamp(uint32_t blk, uint32_t kind, uint32_t vlq, uint32_t bank, uint32_t a, uint32_t b, uint32_t val16,
                         uint32_t id)
{
    SeSelect(blk, id, bank, a, b, [&](uint32_t va) { SePitchRamp(va, vlq, val16 & 0xffff, kind); return true; });
}

/* 0x2478 (kinds 0x10 sel 0 / 0x20 sel 1): sel 0 -> vibDepth[0] (u16), sel 1 -> tremDepth[0] (byte); the CC 1 / CC 2
 * depth fields. 0x2560 (0x11 / 0x21) is the same with vibDepth[1] / tremDepth[1]. */
static void SelDepth(uint32_t which, uint32_t sel, uint32_t blk, uint32_t bank, uint32_t a, uint32_t b, uint32_t val16,
                     uint32_t id)
{
    SeSelect(blk, id, bank, a, b, [&](uint32_t va) {
        Voice *v = V(va);
        if (sel == 0) v->vibDepth[which] = (int16_t)val16;
        else if (sel == 1) v->tremDepth[which] = (int8_t)val16;
        return true;
    });
}

/* 0x2648 (kinds 0x12 sel 0 / 0x22 sel 1): LFO step = 0x10000 / (((val16 << 8) + tickLen / 2) / tickLen), unsigned;
 * sel 0 -> lfo1.step (v + 0x94), sel 1 -> lfo2.step (v + 0xbc). tickLen 0 -> q1 0xffffffff -> step 0; a q1 of 0
 * ((val16 << 8) + tickLen / 2 < tickLen) -> step 0xffffffff (iopdiv rule, divu x / 0). */
static void SelRate(uint32_t sel, uint32_t blk, uint32_t bank, uint32_t a, uint32_t b, uint32_t val16, uint32_t id)
{
    const uint32_t t = S().tickLen, half = t >> 1, x = ((val16 & 0xffff) << 8) + half;
    SeSelect(blk, id, bank, a, b, [&](uint32_t va) {
        if (sel > 1) return true;
        const uint32_t q1 = sel ? IOP_DIVU(x, t, 0x2740, "hsyn SelRate tickLen 0") : IOP_DIVU(x, t, 0x2714, "hsyn SelRate tickLen 0");
        (sel ? V(va)->lfo2 : V(va)->lfo1).step =
            sel ? IOP_DIVU(0x10000u, q1, 0x2754, "hsyn SelRate period 0") : IOP_DIVU(0x10000u, q1, 0x2728, "hsyn SelRate period 0");
        return true;
    });
}

/* 0x278c: the fe parser. Record: fe type id(u32 LE) st payload; st = class | bank. A byte other than fe is skipped up
 * to the next fe; at least 7 bytes from the type byte are needed (else the parse ends). Notes (0x90 / 0xa0): prog,
 * note, velo, [pan if type != 0, else -0x80], lo, hi (bend = lo | hi << 7) - a short note payload is skipped without
 * consuming; 0xa0 velo 0 = note off. 0xb0: a, b, kind + the kind's data (table D_0b520; 3 bytes needed after st).
 * 0xc0: sub-command. Other classes: nothing (p stays after st). A handler moves p only past what it read. */
void ParseSe(uint32_t blk, uint32_t port, uint32_t p, uint32_t end)
{
    s_parseSe++;
    uint32_t b = R8(p++);                                    /* first byte: no end check (StreamDispatch: p < end) */
    for (;;) {
        while (b != 0xfe) {
            if (!(p < end)) return;
            b = R8(p++);
        }
        const uint32_t q = p;                                /* the type byte */
        if (!(q < end)) return;
        if (!(q + 6 < end)) return;
        const uint32_t type = R8(q);
        const uint32_t id = R8(q + 1) | (uint32_t)R8(q + 2) << 8 | (uint32_t)R8(q + 3) << 16 | (uint32_t)R8(q + 4) << 24;
        const uint32_t st = R8(q + 5), bank = st & 0xf, cls = st & 0xf0;
        p = q + 6;
        if (cls == 0x90 || cls == 0xa0) {
            if (!(end < p + (type ? 6u : 5u))) {
                const uint32_t prog = R8(p), note = R8(p + 1), velo = R8(p + 2);
                uint32_t pan = 0xffffff80u;
                p += 3;
                if (type) pan = R8(p++);
                const uint32_t lo = R8(p), hi = R8(p + 1);
                p += 2;
                const uint32_t bend = lo | hi << 7;
                if (cls == 0x90 || velo != 0) SeNoteOn(blk, port, bank, prog, note, velo, (int8_t)pan, bend, id);
                else SeNoteOff(blk, bank, prog, note, id);   /* f_718c (port, stack word 0x14 = 0 unused) */
            }
        } else if (cls == 0xb0) {
            if (q + 9 < end) {
                const uint32_t a = R8(q + 6), bb = R8(q + 7), kind = R8(q + 8);
                p = q + 9;
                switch (kind) {
                case 0: case 1: case 3: case 4: {           /* 0x2a38 */
                    const uint32_t val = ReadVlq(p);
                    const uint32_t c1 = R8(p++);
                    const uint32_t c2 = R8(p++);
                    SelSeg(blk, kind, val, bank, a, bb, c1, c2, id);
                    break;
                }
                case 2:                                      /* 0x2a9c */
                    SelSeg(blk, 2, 0, bank, a, bb, 0, 0, id);
                    break;
                case 5: {                                    /* 0x2ad4 */
                    const uint32_t c2 = R8(p);
                    p += 2;
                    SelSeg(blk, 5, 0, bank, a, bb, 0, c2, id);
                    break;
                }
                case 7: {                                    /* 0x2af8 */
                    const uint32_t val = ReadVlq(p);
                    const uint32_t c = R8(p);
                    p += 2;
                    SelVolRamp(blk, val, bank, a, bb, c, id);
                    break;
                }
                case 0xa:                                    /* 0x2f00: skip VLQ + 2 */
                    ReadVlq(p);
                    p += 2;
                    break;
                case 0xc: case 0xd: {                        /* 0x2b40 */
                    const uint32_t val = ReadVlq(p);
                    const uint32_t c2 = R8(p);
                    p += 2;
                    SelSeg(blk, kind, val, bank, a, bb, 0, c2, id);
                    break;
                }
                case 0xe: case 0xf: {                        /* 0x2b94 */
                    const uint32_t val = ReadVlq(p);
                    const uint32_t lo = R8(p++);
                    const uint32_t hi = R8(p++);
                    SelPitchRamp(blk, kind, val, bank, a, bb, lo | hi << 8, id);
                    break;
                }
                case 0x10: case 0x11: case 0x20: case 0x21: { /* 0x2bf8 / 0x2c24 / 0x2c60 / 0x2cb8: two bytes (modsein's
                                                                * 0x20 / 0x21 builder emits one: quirk) */
                    const uint32_t lo = R8(p++);
                    const uint32_t hi = R8(p++);
                    SelDepth(kind & 1, kind >> 5, blk, bank, a, bb, lo | hi << 8, id);
                    break;
                }
                case 0x12: case 0x22: {                      /* 0x2c50 / 0x2d10 */
                    const uint32_t val = ReadVlq(p);
                    SelRate(kind >> 5, blk, bank, a, bb, val & 0xffff, id);
                    break;
                }
                default:                                     /* 0x2d48: the payload is not skipped (resync on fe) */
                    iopk::Printf("SESQ: unknown command %02x\n", kind);
                    break;
                }
            }
        } else if (cls == 0xc0) {                            /* 0x2d60 */
            const uint32_t s = p;
            const uint32_t sub = R8(s);
            p = s + 1;
            switch (sub) {
            case 0x1e: {                                     /* f_7730 -> f_75d4 (its id argument unused) */
                const uint32_t x = R8(s + 1) | (uint32_t)R8(s + 2) << 8 | (uint32_t)R8(s + 3) << 16 | (uint32_t)R8(s + 4) << 24;
                const uint32_t y = R8(s + 5) | (uint32_t)R8(s + 6) << 8 | (uint32_t)R8(s + 7) << 16 | (uint32_t)R8(s + 8) << 24;
                p = s + 9;
                SeReleaseMasked(blk, x, y);
                break;
            }
            case 0x1f: SeReleaseById(blk, id); break;       /* f_75b4 -> f_7468 */
            case 0x00: case 0x11: case 0x12: p = s + 4; break;
            case 0x10: p = s + 7; break;
            case 0x13: p = s + 5; break;
            case 0x20: case 0x30: p = s + 3; break;
            case 0x31:                                       /* the f_2f00 skip */
                ReadVlq(p);
                p += 2;
                break;
            default: break;                                  /* p = s + 1 */
            }
        }
        /* 0x2f18: the loop tail */
        if (!(p < end)) return;
        b = R8(p++);
    }
}

}  // namespace hsyn
