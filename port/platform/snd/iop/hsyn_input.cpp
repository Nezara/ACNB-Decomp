/* hsyn_input - group H5 of NI6b: the MS input half of
 * modhsyn. The stream parser f_15dc (MIDI-like bytes of modmidi and the EE's SE records), the channel message switch
 * f_12f8, the SE f9 / fd event handler f_1428, program change f_07ec, the controller table f_1140 (124 entries, 15
 * handlers + data entry f_1280), the controller setters with their three-list voice walks (f_0888 .. f_0ca8, f_094c),
 * the per-note SE setters of fd kinds 0 / 1 / 2 (f_0e90 / f_0d74 / f_0fac) and ResetControllers f_10c0.
 *
 * Grammar (f_15dc, instruction-checked): no running status; a byte without bit 7 where a status is expected is skipped;
 * data bytes are never validated (a status byte after a status is eaten as data); 0x80 / 0xc0 / 0xd0 take one data
 * byte, 0x90 / 0xa0 / 0xb0 / 0xe0 two (D_0b670 = 00 01 01 01 00 00 01 00); a missing data byte at the end ends the parse
 * without a call; f9 / fd records (4 / 6 data bytes) advance the pointer before the bounds test, so a truncated one ends
 * the parse; 0xf0 skips to and past 0xf7 (the end inside it ends the parse; f_1420 is an empty sink); every other 0xf1 ..
 * 0xff byte is consumed alone. The setters compare before storing, so a repeated value never touches a voice.
 * Unreached in every recording: sysex, pitch bend past its unchanged test, every CC besides 0x40 / 0x7b, every setter
 * walk, the fd wildcards. The "fe" SE-stream parser (f_278c, F1) is H5R's (hsyn_fe.cpp, REV). */
#include "hsyn.h"
#include "hsyn_state.h"

namespace hsyn {

/* ---------------------------------------------------------------- test trace (no behaviour) */

static InTraceFn s_trace;
static void *s_traceUser;
void SetInputTrace(InTraceFn fn, void *user)
{
    s_trace = fn;
    s_traceUser = user;
}
static inline void Trace(uint32_t kind, uint32_t port, uint32_t blk, uint32_t ch, int32_t a, int32_t b = 0, int32_t c = 0,
                         int32_t d = 0)
{
    if (!s_trace) return;
    InEvent e = {kind, port, blk, ch, a, b, c, d};
    s_trace(s_traceUser, &e);
}

/* ---------------------------------------------------------------- helpers */

/* the three channel lists, head -> tail (P = ch - 8, ch, ch + 8; next = v + 8, read after the call; nothing unlinks) */
template <class F> static inline void Walk3(uint32_t cha, F f)
{
    const uint32_t P[3] = {cha - 8, cha, cha + 8};
    for (int l = 0; l < 3; l++)
        for (uint32_t v = Lw(P[l] + 8); v != P[l]; v = Lw(v + 8)) f(v);
}
/* the compiler's x * 0x81020409 >> 32 + x >> 6 on x = (val & 0xff) << 7: equal to a plain / 127 (map 3.5) */
static inline uint32_t Scale127(uint32_t val) { return ((val & 0xff) << 7) / 127; }
/* f_0ca8 / f_0d74: val - 0x40, the sign-extended byte clamped at -0x3f */
static inline int8_t PanOf(uint32_t val)
{
    uint32_t x = val - 0x40;
    if ((int8_t)x < -0x3f) x = (uint32_t)-0x3f;
    return (int8_t)x;
}

/* ---------------------------------------------------------------- program change, controllers */

/* 0x07ec: bank slot ch[0x2a] (< 16), Prog chunk last index >= prog, offset != -1, tone count != 0; any failure zeroes
 * both the entry and the slot pointer (the channel then drops its note-ons) */
static void ProgramChange(uint32_t blk14, uint32_t cha, uint32_t prog)
{
    Channel *c = Ch(cha);
    uint32_t entry = 0, slotp = 0;
    if (c->bank < 0x10) {
        slotp = blk14 + (BLK_SLOT - BLK_VOL) + SLOT_SIZE * c->bank;
        uint32_t P = Slot(slotp)->prog;
        prog &= 0xff;
        if (P != 0 && !(R32(P + 0xc) < prog)) {
            uint32_t o = R32(P + 0x10 + 4 * prog);
            if (o != 0xffffffffu) {
                entry = P + o;
                if (R8(entry + 4) == 0) entry = 0;
            }
        }
    }
    c->prog = entry;
    c->slot = entry ? slotp : 0;
}

/* 0x0888: MIDI pitch bend, compared as s16 */
static void PitchBend(uint32_t cha, uint32_t msb, uint32_t lsb)
{
    Channel *c = Ch(cha);
    int16_t b = (int16_t)((lsb & 0xff) + ((msb & 0xff) << 7) - 0x2000);
    if (c->bend == b) return;
    c->bend = b;
    Walk3(cha, [b](uint32_t v) { SetBend(v, b); });
}
/* 0x094c: optionally copy the expression into each voice (before the call), then its volume update */
static void UpdateChannelVoices(uint32_t cha, uint32_t copyExpr)
{
    Walk3(cha, [cha, copyExpr](uint32_t v) {
        if (copyExpr) V(v)->expr = Ch(cha)->expr;
        UpdateVolume(v);
    });
}
/* 0x0a08 CC 7 / 0x0a64 CC 11: stored and compared as bytes */
static void SetChVolume(uint32_t cha, uint32_t val)
{
    uint32_t n = Scale127(val);
    if (Ch(cha)->vol == (n & 0xff)) return;
    Ch(cha)->vol = (uint8_t)n;
    UpdateChannelVoices(cha, 0);
}
static void SetChExpression(uint32_t cha, uint32_t val)
{
    uint32_t n = Scale127(val);
    if (Ch(cha)->expr == (n & 0xff)) return;
    Ch(cha)->expr = (uint8_t)n;
    UpdateChannelVoices(cha, 1);
}
/* 0x0ac0 CC 2 / 0x0b88 CC 1: stored as a byte, compared with the 16-bit value (differs only for val >= 0xff) */
static void SetChCc2(uint32_t cha, uint32_t val)
{
    uint32_t n = Scale127(val);
    if (Ch(cha)->cc2 == (n & 0xffff)) return;
    Ch(cha)->cc2 = (uint8_t)n;
    Walk3(cha, [n](uint32_t v) { SetTremoloDepth(v, n & 0xff); });
}
static void SetChCc1(uint32_t cha, uint32_t val)
{
    uint32_t n = Scale127(val);
    if (Ch(cha)->cc1 == (n & 0xffff)) return;
    Ch(cha)->cc1 = (uint8_t)n;
    Walk3(cha, [n](uint32_t v) { SetVibratoDepth(v, n & 0xff); });
}
/* 0x0c50 CC 5 (unreached) */
static void SetPortaTime(uint32_t cha, uint32_t val) { Ch(cha)->portaTime = (uint16_t)((val & 0xff) * 20); }
/* 0x0c68 CC 0x41: 0 = off (0xff); on arms (0x80 / 0x80) only from off */
static void SetPorta(uint32_t cha, uint32_t val)
{
    Channel *c = Ch(cha);
    if ((val & 0xff) == 0) {
        c->portaState = 0xff;
        return;
    }
    if (c->portaState != 0xff) return;
    c->portaState = 0x80;
    c->portaCtl = 0x80;
}
/* 0x0ca0 CC 0x54 (unreached) */
static void SetPortaCtl(uint32_t cha, uint32_t val) { Ch(cha)->portaCtl = (uint8_t)val; }
/* 0x0ca8 CC 10, compared as s8 */
static void SetChPan(uint32_t cha, uint32_t val)
{
    int8_t p = PanOf(val);
    if (Ch(cha)->pan == p) return;
    Ch(cha)->pan = p;
    Walk3(cha, [p](uint32_t v) { PanLaw(v, p); });
}

/* 0x10c0 (export ResetAllControler's body): hold off, bend centre, CC 2 / 1 0, volume / expression 0x7f, pan centre,
 * portamento off - in this order; program, bank, SE overrides, CC 5 / 0x54 and NRPN untouched */
void ResetControllers(uint32_t cha)
{
    HoldPedal(cha, 0);
    PitchBend(cha, 0x40, 0);
    SetChCc2(cha, 0);
    SetChCc1(cha, 0);
    SetChVolume(cha, 0x7f);
    SetChPan(cha, 0x40);
    SetChExpression(cha, 0x7f);
    SetPorta(cha, 0);
}

/* 0x1280 CC 6: data entry of the NRPN pair (MSB 2 effect type, 3 effect parameter); the triple is always cleared */
static void DataEntry(uint32_t cha, uint32_t val)
{
    Channel *c = Ch(cha);
    c->nrpnData = (uint8_t)val;
    if (c->nrpnLsb != 0xff) {
        if (c->nrpnMsb == 2) NrpnEffectType(c->nrpnLsb, c->nrpnData);
        else if (c->nrpnMsb == 3) NrpnEffectParam(c->nrpnLsb, c->nrpnData);
    }
    c->nrpnData = 0xff;
    c->nrpnLsb = 0xff;
    c->nrpnMsb = 0xff;
}

/* 0x1140: the 124-entry table D_0b310 (cc >= 0x7c and the unlisted entries return); the handlers get val & 0xff */
static void ControlChange(uint32_t blk14, uint32_t cha, uint32_t cc, uint32_t val)
{
    (void)blk14;
    Channel *c = Ch(cha);
    val &= 0xff;
    switch (cc & 0xff) {
    case 0x00: c->bank = (uint8_t)val; break;            /* f_1178 */
    case 0x01: SetChCc1(cha, val); break;                /* f_1180 */
    case 0x02: SetChCc2(cha, val); break;                /* f_1194 */
    case 0x05: SetPortaTime(cha, val); break;            /* f_11a8 */
    case 0x06: DataEntry(cha, val); break;               /* f_1280 */
    case 0x07: SetChVolume(cha, val); break;             /* f_11bc */
    case 0x0a: SetChPan(cha, val); break;                /* f_11d0 */
    case 0x0b: SetChExpression(cha, val); break;         /* f_11e4 */
    case 0x40: HoldPedal(cha, val); break;               /* f_11f8 */
    case 0x41: SetPorta(cha, val); break;                /* f_120c */
    case 0x54: SetPortaCtl(cha, val); break;             /* f_1220 */
    case 0x62: c->nrpnLsb = (uint8_t)val; break;         /* f_1278 */
    case 0x63:                                           /* f_1264 */
        c->nrpnMsb = (uint8_t)val;
        c->nrpnData = 0xff;
        c->nrpnLsb = 0xff;
        break;
    case 0x78: AllSoundOffCh(cha); break;                /* f_1234 */
    case 0x79: ResetControllers(cha); break;             /* f_1244 */
    case 0x7b: AllNotesOff(cha); break;                  /* f_1254 */
    default: break;                                      /* f_12e8 */
    }
}

/* 0x12f8: ch = status & 0xf; 0x90 with vel 0 is a note off; id 0xff = MIDI note; 0xa0 / 0xd0 ignored */
static void ChannelMessage(uint32_t blk, uint32_t port, uint32_t status, uint32_t d1, uint32_t d2)
{
    const uint32_t blk14 = blk + BLK_VOL, cha = ChannelAddr(blk, status & 0xf);
    d1 &= 0xff;
    d2 &= 0xff;
    switch (status & 0xf0) {
    case 0xb0:
        Trace('C', port, blk, cha, (int32_t)d1, (int32_t)d2);
        ControlChange(blk14, cha, d1, d2);
        break;
    case 0x90:
        if (d2 != 0) {
            Trace('N', port, blk, cha, (int32_t)d1, (int32_t)d2, 0xff);
            NoteOn(blk, cha, port, d1, d2, 0xff);
            break;
        }
        Trace('F', port, blk, cha, (int32_t)d1, 0xff);
        NoteOff(cha, d1, 0xff);
        break;
    case 0x80:
        Trace('F', port, blk, cha, (int32_t)d1, 0xff);
        NoteOff(cha, d1, 0xff);
        break;
    case 0xc0:
        Trace('P', port, blk, cha, (int32_t)d1);
        ProgramChange(blk14, cha, d1);
        break;
    case 0xe0:
        Trace('B', port, blk, cha, (int32_t)d2, (int32_t)d1);
        PitchBend(cha, d2, d1);
        break;
    default: break;
    }
}

/* ---------------------------------------------------------------- SE events */

/* 0x0e90 / 0x0d74 / 0x0fac: fd kinds 0 / 1 / 2 on every voice of the channel's three lists with (id == 0x7f or its id)
 * and (the any-key flag 0x80 or its key); no change test */
static inline bool SeMatch(uint32_t v, uint32_t keyflag, uint32_t id)
{
    return (id == 0x7f || V(v)->id == id) && ((keyflag & 0x80) || V(v)->key == (keyflag & 0xff));
}
static void SeNoteVolume(uint32_t cha, uint32_t keyflag, uint32_t id, uint32_t val)
{
    const uint8_t e = (uint8_t)Scale127(val);
    Walk3(cha, [=](uint32_t v) {
        if (!SeMatch(v, keyflag, id)) return;
        V(v)->expr = e;
        UpdateVolume(v);
    });
}
static void SeNotePan(uint32_t cha, uint32_t keyflag, uint32_t id, uint32_t val)
{
    const int8_t p = PanOf(val);
    Walk3(cha, [=](uint32_t v) {
        if (SeMatch(v, keyflag, id)) PanLaw(v, p);
    });
}
static void SeNoteBend(uint32_t cha, uint32_t keyflag, uint32_t id, uint32_t msb, uint32_t lsb)
{
    const int16_t b = (int16_t)((lsb & 0xff) + ((msb & 0xff) << 7) - 0x2000);
    Walk3(cha, [=](uint32_t v) {
        if (SeMatch(v, keyflag, id)) SetBend(v, b);
    });
}

/* 0x1428: q = the record's data bytes (f9: kind ch v0 v1, fd: kind ch key id v0 v1); the channel is the low nibble of
 * byte 1. f9 = the one-shot overrides the next started note-on consumes (NoteOn resets them); fd 0x10 = note on / off
 * (key & 0x7f), fd 0 / 1 / 2 = per-note volume / pan / bend with the any-key flag ch & 0x10. Other kinds: nothing. */
static void SeEvent(uint32_t blk, uint32_t port, uint32_t ev, uint32_t q)
{
    const uint32_t kind = R8(q), chb = R8(q + 1), b2 = R8(q + 2);
    const uint32_t cha = blk + BLK_CH + CH_SIZE * (chb & 0xf);
    Channel *c = Ch(cha);
    if ((ev & 0xff) == 0xf9) {
        Trace('O', port, blk, cha, (int32_t)kind, (int32_t)b2, (int32_t)R8(q + 3));
        switch (kind) {
        case 0: c->seVol = (uint8_t)b2; break;
        case 1: c->sePan = (int8_t)b2; break;
        case 2: c->seBend = (int16_t)(b2 + (R8(q + 3) << 7)); break;
        default: break;
        }
        return;
    }
    if ((ev & 0xff) != 0xfd) return;
    const uint32_t id = R8(q + 3), v0 = R8(q + 4);
    uint32_t key = b2;
    if (chb & 0x10) key |= 0x80;
    switch (kind) {
    case 0x10:
        key &= 0x7f;
        if (v0 != 0) {
            Trace('N', port, blk, cha, (int32_t)key, (int32_t)v0, (int32_t)id);
            NoteOn(blk, cha, port, key, v0, id);
        } else {
            Trace('F', port, blk, cha, (int32_t)key, (int32_t)id);
            NoteOff(cha, key, id);
        }
        break;
    case 0:
        Trace('D', port, blk, cha, 0, (int32_t)key, (int32_t)id, (int32_t)(v0 | R8(q + 5) << 8));
        SeNoteVolume(cha, key, id, v0);
        break;
    case 1:
        Trace('D', port, blk, cha, 1, (int32_t)key, (int32_t)id, (int32_t)(v0 | R8(q + 5) << 8));
        SeNotePan(cha, key, id, v0);
        break;
    case 2:
        Trace('D', port, blk, cha, 2, (int32_t)key, (int32_t)id, (int32_t)(v0 | R8(q + 5) << 8));
        SeNoteBend(cha, key, id, R8(q + 5), v0);
        break;
    default:
        Trace('D', port, blk, cha, (int32_t)kind, (int32_t)key, (int32_t)id, (int32_t)(v0 | R8(q + 5) << 8));
        break;
    }
}

/* ---------------------------------------------------------------- the parser */

/* D_0b670: a second data byte per status class 0x80 .. 0xe0 (raw IRX bytes) */
static const uint8_t kTwoData[8] = {0, 1, 1, 1, 0, 0, 1, 0};

/* 0x15dc: p < end on entry (f_174c); the first byte is read without a test */
void ParseMs(uint32_t blk, uint32_t port, uint32_t p, uint32_t end)
{
    do {
        uint32_t b = R8(p++);
        if (!(b & 0x80)) continue;
        if (b < 0xf0) {
            if (!(p < end)) return;
            uint32_t d1 = R8(p++), d2 = 0;
            if (kTwoData[(b - 0x80) >> 4]) {
                if (!(p < end)) return;
                d2 = R8(p++);
            }
            ChannelMessage(blk, port, b, d1, d2);
            continue;
        }
        if (b == 0xf0) {
            if (!(p < end)) return;
            while (R8(p++) != 0xf7)
                if (!(p < end)) return;
            Trace('X', port, blk, 0, 0);   /* f_1420(blk): empty sink */
            continue;
        }
        if (b == 0xf9 || b == 0xfd) {
            uint32_t q = p;
            p += b == 0xf9 ? 4 : 6;
            if (end < p) continue;   /* truncated: the loop test ends the parse */
            SeEvent(blk, port, b, q);
        }
    } while (p < end);
}

}  // namespace hsyn
