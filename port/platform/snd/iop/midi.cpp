/* midi - see midi.h. Group W1 of NI7: everything SYTH's 8 imports
 * reach except the Song-mode step (unreachable); W2: the event player PlayEnv with Meta, Sysex / SysexCont, the NRPN
 * loop machinery (Nrpn / FindLoop) and the poly-aftertouch extension table. Function comments give
 * the original's module offset. The debug / verbose printf's are kept (plat_log) behind the module-arg words as in the
 * original; no recording passes module args (all IMOD lines: args 0). */
#include "midi.h"
#include "midi_state.h"
#include "../../plat.h"
#include "iopdiv.h"
#include <stdlib.h>
#include <string.h>

namespace midi {

uint32_t g_base;
CallbackFn g_cbFn;
void *g_cbUser;

void SetChMsgResolver(CallbackFn fn, void *u)
{
    g_cbFn = fn;
    g_cbUser = u;
}

static uint32_t Debug() { return R32(g_base + D_DEBUG); }
static uint32_t Verbose() { return R32(g_base + D_VERBOSE); }

/* D_3eb0: the channel bit table (u16[16], 1 << i) */
static uint16_t ChBit(uint32_t ch) { return (uint16_t)(1u << (ch & 0xf)); }

/* ---------------------------------------------------------------- module start */

/* D_3e70: the module-arg table {name, length, word} */
static const struct { const char *name; int len; uint32_t off; } ARGTAB[] = {
    {"debug", 5, D_DEBUG},
    {"verbose", 7, D_VERBOSE},
};

/* look_ctype_table(c) & 8: the C-locale space class */
static bool IsSpace(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

/* 0x0000: for argv[1 ..]: skip blanks, `name=N` of the table -> strtol(.., 10) into the word (first match only);
 * CpuSuspendIntr, RegisterLibraryEntries (vanishes: the exports are linked), CpuResumeIntr; returns (result != 0) = 0.
 * The phantom image is zeroed (the .bss and the two arg words). */
int ModuleStart(const iopk::ModuleArgs *m)
{
    g_base = m->base;
    for (int i = 1; i < m->argc; i++) {
        const char *s = m->argv[i];
        if (!s || !strlen(s)) continue;
        while (IsSpace(*s)) s++;
        for (const auto &a : ARGTAB)
            if (!strncmp(s, a.name, (size_t)a.len)) {
                /* the original reads past the name's '=' unchecked; a bare "debug" would read beyond the NUL */
                const char *v = s[a.len] ? s + a.len + 1 : s + a.len;
                W32(g_base + a.off, (uint32_t)strtol(v, 0, 10));
                break;
            }
    }
    int st;
    iopk::CpuSuspendIntr(&st);
    iopk::CpuResumeIntr(st);
    return 0;
}

uint32_t Base() { return g_base; }

/* ---------------------------------------------------------------- init / load */

/* 0x0140 sceMidi_Init: the ATick step first (also on a bad ctx); per input pair: route masks 1, the five callbacks
 * cleared, Load (fails while the SQ pointer is 0: result ignored); then the output buffers' used words. A bad output
 * group returns -1 after the ports were reset (Q9). */
int Init(uint32_t ctx, uint32_t usec)
{
    W32(g_base + B_ATICK, usec << 7);
    if (!ctx) return -1;
    if ((int32_t)R32(ctx + CTX_NGROUP) < 2) return -1;
    uint32_t g = R32(ctx + CTX_GROUP);
    if (!g) return -1;
    uint32_t ents = R32(g + GRP_ENTRIES);
    if (R32(g + GRP_N) & 1) return -1;
    if (!ents) return -1;
    for (uint32_t i = 0, k = 1; (int32_t)k < (int32_t)R32(g + GRP_N); i++, k += 2) {
        uint32_t s2 = R32(ents + i * 16 + 0xc);
        if (!s2) continue;
        for (int c = 15; c >= 0; c--) W16(s2 + S2_ROUTE + (uint32_t)c * 2, 1);
        W16(s2 + S2_SYSROUTE, 1);
        W32(s2 + S2_CHCB, 0);
        W32(s2 + S2_METACB, 0);
        W32(s2 + S2_SYSEXCB, 0);
        W32(s2 + S2_LOOPCB, 0);
        W32(s2 + S2_NRPNCB, 0);
        Load(ctx, i);
    }
    uint32_t g1 = g + GRP_SIZE;
    uint32_t n = R32(g1 + GRP_N), out = R32(g1 + GRP_ENTRIES);
    if (!n || !out) return -1;
    for (uint32_t k = 0; (int32_t)k < (int32_t)R32(g1 + GRP_N); k++) {
        uint32_t buf = R32(out + k * 8 + 4);
        if (buf) W32(buf + SBUF_USED, 0);
    }
    return 0;
}

/* 0x02ac: byte i of w0 equals a[3 - i] and byte i of w1 equals b[3 - i] (the tags are stored byte-reversed) */
int TagEq(uint32_t w0, uint32_t w1, const char *a, const char *b)
{
    if (Verbose())
        plat_log("[%c%c%c%c] [%c%c%c%c]", (int)(int8_t)(w0 >> 24), (int)(int8_t)(w0 >> 16),
                (int)(int8_t)(w0 >> 8), (int)(int8_t)w0, (int)(int8_t)(w1 >> 24), (int)(int8_t)(w1 >> 16),
                (int)(int8_t)(w1 >> 8), (int)(int8_t)w1);
    for (int i = 0; i < 4; i++) {
        if ((int8_t)(w0 >> (8 * i)) != (int8_t)a[3 - i]) return 0;
        if ((int8_t)(w1 >> (8 * i)) != (int8_t)b[3 - i]) return 0;
    }
    return 1;
}

/* 0x03a8 sceMidi_Load: resets S2 / env, then the chunk checks Vers, Sequ, Song (optional: a bad tag = no song, Q9),
 * Midi; stores the three chunk pointers and sets "loaded" */
int Load(uint32_t ctx, uint32_t port)
{
    if (!ctx) return -1;
    uint32_t g = R32(ctx + CTX_GROUP);
    if (!(port * 2 + 1 < R32(g + GRP_N))) return -1;
    uint32_t s2 = R32(R32(g + GRP_ENTRIES) + port * 16 + 0xc);
    if (!s2) return -1;
    uint32_t env = s2 + S2_ENV;
    W32(s2 + S2_SONG, 0xffffffffu);
    W32(s2 + S2_SEQ, 0xffffffffu);
    W32(s2 + S2_TICK, 0);
    W32(s2 + S2_FLAGS, 0);
    W32(env + E_SEQU, 0);
    W32(env + E_MIDI, 0);
    W32(env + E_SONGCH, 0);
    W16(env + E_USED, 0);
    for (int c = 15; c >= 0; c--) W8(env + E_VOLF + (uint32_t)c, 0x80);
    W8(env + E_MASTER, 0x80);
    uint32_t sq = R32(R32(R32(ctx + CTX_GROUP) + GRP_ENTRIES) + port * 16 + 4);
    if (!sq) return -1;
    if (!TagEq(R32(sq), R32(sq + 4), "SCEI", "Vers")) return -1;
    uint32_t sequ = sq + R32(sq + 8);
    if (!TagEq(R32(sequ), R32(sequ + 4), "SCEI", "Sequ")) return -1;
    uint32_t song = 0, off = R32(sequ + 0x10);
    if (off != 0xffffffffu) {
        song = sq + off;
        if (!TagEq(R32(song), R32(song + 4), "SCEI", "Song")) song = 0;
    }
    off = R32(sequ + 0x14);
    if (off == 0xffffffffu) return -1;
    uint32_t mid = sq + off;
    if (!TagEq(R32(mid), R32(mid + 4), "SCEI", "Midi")) return -1;
    W32(env + E_SEQU, sequ);
    W32(env + E_MIDI, mid);
    W32(env + E_SONGCH, song);
    W32(s2 + S2_FLAGS, FL_LOADED);
    return 0;
}

/* ---------------------------------------------------------------- the per-tick shell */

/* 0x06b0 sceMidi_ATick: every port struct with "playing" steps; then Song mode (SongStep, midi_song.cpp; never set by
 * SYTH) in the same iteration */
int ATick(uint32_t ctx)
{
    uint32_t g = R32(ctx + CTX_GROUP), g1 = g + GRP_SIZE;
    for (uint32_t k = 1; (int32_t)k < (int32_t)R32(g + GRP_N); k += 2) {
        uint32_t s2 = R32(R32(g + GRP_ENTRIES) + k * 8 + 4);
        if (!s2) continue;
        if (R32(s2 + S2_FLAGS) & FL_PLAY) Step(s2, g1);
        if (R32(s2 + S2_FLAGS) & FL_SONG) SongStep(ctx, k / 2, s2);
    }
    return 0;
}

/* ---------------------------------------------------------------- time */

/* 0x0c20: tick length = (((tempo << 7) / div) << 8) / rel, u32 (divu + break 0x1c00: IOP divide-by-zero rule, iopdiv.h) */
void Recalc(uint32_t env)
{
    uint32_t div = R32(env + E_DIV), rel = R32(env + E_REL);
    uint32_t v = IOP_DIVU(R32(env + E_TEMPO) << 7, div, 0x0c30, "midi Recalc division 0");
    W32(env + E_TLEN, IOP_DIVU(v << 8, rel, 0x0c48, "midi Recalc relative tempo 0"));
}

/* 0x0c5c: a variable-length quantity at the read pointer (7 bits per byte, bit 7 = another byte follows) */
uint32_t Vlq(uint32_t env)
{
    uint32_t p = R32(env + E_PTR), v = 0;
    uint8_t b;
    do {
        b = R8(p++);
        v = (v << 7) + (b & 0x7f);
    } while (b & 0x80);
    W32(env + E_PTR, p);
    return v;
}

/* 0x0c90: the next delta time (0 and the flag cleared when the last channel event carried bit 7, Q3), added to the
 * absolute tick of the next event */
uint32_t NextDelta(uint32_t env)
{
    if (R8(env + E_DFLAG)) {
        W8(env + E_DFLAG, 0);
        return 0;
    }
    uint32_t v = Vlq(env);
    W32(env + E_NEXT, R32(env + E_NEXT) + v);
    return v;
}

/* 0x1860: the time loop - play everything due at the current tick, then advance while the accumulator holds a whole
 * tick; PlayEnv 0 (end of track / error) stops the port and discards the accumulator (Q10) */
void Step(uint32_t s2, uint32_t group)
{
    uint32_t env = s2 + S2_ENV;
    uint32_t acc = R32(env + E_ACC) + R32(g_base + B_ATICK);
    while (PlayEnv(s2, env, group)) {
        uint32_t len = R32(env + E_TLEN);
        if (acc < len) {
            W32(env + E_ACC, acc);
            return;
        }
        acc -= len;
        W32(s2 + S2_TICK, R32(s2 + S2_TICK) + 1);
    }
    W32(s2 + S2_FLAGS, R32(s2 + S2_FLAGS) & ~FL_PLAY);
    AllNotesOff(s2, group);
    W32(env + E_ACC, 0);
}

/* ---------------------------------------------------------------- the event player (NI7 W2) */

/* a non-null meta / sysex / loop / NRPN-user callback (S2 +0x3c / 0x44 / 0x4c / 0x54): SYTH never sets them (only +0x34)
 * and a native port cannot call IOP code; logged once, then treated as unset (for meta / sysex / loop the same path as
 * a callback that returned non-zero) */
static void Unsupported(uint32_t bit, const char *what, uint32_t cb)
{
    static uint32_t warned;
    if (!(warned & bit)) plat_log("midi: %s callback %06x is IOP code: not supported, treated as unset", what, cb);
    warned |= bit;
}

/* 0x0d2c: meta event (type = PlayEnv's d1; the read pointer is at the VLQ length). 0x2f end of track: the ended bit,
 * returns 0 (length / data not skipped); 0x51 tempo: the length must be 3 (else 0 without the ended bit, Q4),
 * big-endian us / quarter, Recalc; other types are skipped. */
int Meta(uint32_t s2, uint32_t env, uint32_t type)
{
    uint32_t len = Vlq(env);
    type &= 0xff;
    if (uint32_t cb = R32(s2 + S2_METACB)) Unsupported(1, "meta", cb);
    if (type == 0x2f) {
        W32(s2 + S2_FLAGS, R32(s2 + S2_FLAGS) | FL_ENDED);
        return 0;
    }
    if (type == 0x51) {
        if (len != 3) {
            if (Debug()) plat_log("parseMetaEvent tempo length error %d", (int)len);
            return 0;
        }
        uint32_t p = R32(env + E_PTR);
        uint32_t t = ((uint32_t)R8(p) << 16) + ((uint32_t)R8(p + 1) << 8) + R8(p + 2);
        W32(env + E_TEMPO, t);
        if (Verbose()) {
            plat_log("TEMPO = %d", (int)IOP_DIVU(60000000u, t, 0x0e40, "midi TEMPO print tempo 0"));
        }
        W32(env + E_PTR, p + 3);
        Recalc(env);
        return 1;
    }
    W32(env + E_PTR, R32(env + E_PTR) + len);
    return 1;
}

/* 0x0ce8: status 0xf7 (sysex continuation): step back over the d1 PlayEnv consumed, VLQ length, skip the data */
void SysexCont(uint32_t env)
{
    W32(env + E_PTR, R32(env + E_PTR) - 1);
    uint32_t len = Vlq(env);
    W32(env + E_PTR, R32(env + E_PTR) + len);
}

/* 0x0ee4: status 0xf0. The VLQ length is read AFTER the d1 byte PlayEnv already consumed (Q7: unlike 0xf7 no step back,
 * so "f0 len data" loses its length byte). 0xf0 + len bytes go to every output buffer of the sysex mask (S2 +0x30) with
 * room (used + len + 9 <= cap, else dropped for that buffer); nothing with group 0; then the data is skipped. */
void Sysex(uint32_t s2, uint32_t env, uint32_t group)
{
    uint32_t len = Vlq(env);
    if (uint32_t cb = R32(s2 + S2_SYSEXCB)) Unsupported(2, "sysex", cb);
    if (group) {
        uint32_t mask = R16(s2 + S2_SYSROUTE);
        for (uint32_t idx = 0; mask; mask >>= 1, idx++) {
            if (!(mask & 1)) continue;   /* (the asm skips 4 bits at a time when the low nibble is 0: same result) */
            uint32_t e = OutEntry(idx, group);
            uint32_t buf = e ? R32(e + 4) : 0;
            if (!buf) continue;
            uint32_t used = R32(buf + SBUF_USED);
            if (R32(buf + SBUF_CAP) < used + len + 9) {
                if (Debug()) plat_log("paesExcEvent Buffer OverRun");
                continue;
            }
            uint32_t dst = buf + SBUF_DATA + used, src = R32(env + E_PTR);
            W32(buf + SBUF_USED, used + 1 + len);
            W8(dst++, 0xf0);
            for (uint32_t i = 0; i < len; i++) W8(dst++, R8(src + i));
        }
    }
    W32(env + E_PTR, R32(env + E_PTR) + len);
}

/* 0x0790: the loop-table entry with that id (8 x 12 B), 0 if none */
uint32_t FindLoop(uint32_t table, uint32_t id)
{
    id &= 0xff;
    for (uint32_t i = 0; i < LOOP_N; i++, table += LOOP_SIZE)
        if (R8(table) == id) return table;
    return 0;
}

/* 0x07c4: CC 99 / 98 / 6 / 38 = NRPN MSB / LSB, data MSB / LSB (99 and 98 clear both data bytes), then by the NRPN MSB:
 * 0 loop start (id = data MSB), 1 loop end (id, count = data LSB; 0 = forever), 0x10 + LSB 0 / 0x11 and 0x10 + LSB 1 /
 * 0x12 the user data callback (8 / 14 bit), 0x7f + LSB 0x7f reset. A completed operation resets the four bytes to 0xff.
 * status = the channel status byte (the callback's channel), val = d2 & 0x7f. Loop end with group 0 (SetLocation's
 * chase) or flag 8 (never set) only resets. Quirk kept: with all 8 entries taken a new loop start writes a 9th entry
 * past the table (the 12 bytes after the port struct = the next port's S2 +0x00 .. 0x0b). */
void Nrpn(uint32_t s2, uint32_t env, uint32_t status, uint32_t cc, uint32_t val, uint32_t group)
{
    switch (cc & 0xff) {
    case 0x26: W8(env + E_DATA_LSB, (uint8_t)val); break;
    case 0x06: W8(env + E_DATA_MSB, (uint8_t)val); break;
    case 0x62:
        W8(env + E_NRPN_LSB, (uint8_t)val);
        W8(env + E_DATA_LSB, 0xff);
        W8(env + E_DATA_MSB, 0xff);
        break;
    case 0x63:
        W8(env + E_NRPN_MSB, (uint8_t)val);
        W8(env + E_DATA_LSB, 0xff);
        W8(env + E_DATA_MSB, 0xff);
        break;
    default: return;
    }
    bool reset = false;
    int data = 0;   /* 8 / 14: the user data callback */
    uint32_t msb = R8(env + E_NRPN_MSB), lsb = R8(env + E_NRPN_LSB);
    if (msb == 0x10) data = lsb == 0 ? 8 : lsb == 1 ? 14 : 0;
    else if (msb == 0x11) data = 8;
    else if (msb == 0x12) data = 14;
    if (msb == 0) {   /* loop start */
        uint32_t id = R8(env + E_DATA_MSB);
        if (id != 0xff) {
            uint32_t e = FindLoop(env + E_LOOP, id);
            if (e) {
                if (R8(e + 1) != 0xff && Debug()) plat_log("loop start duplext %d", (int)id);
            } else {
                e = env + E_LOOP;
                uint32_t i = 0;
                for (; i < LOOP_N && R8(e) != 0xff; i++) e += LOOP_SIZE;
                if (i == LOOP_N) {
                    static bool warned;
                    if (!warned) plat_log("midi: loop table full: loop start %u written past the table (as the original)", id);
                    warned = true;
                }
            }
            W8(e + 1, 0xff);
            W8(e + 0, R8(env + E_DATA_MSB));
            W8(e + 2, R8(env + E_RUN));
            W8(e + 3, R8(env + E_DFLAG));
            W32(e + 4, R32(env + E_NEXT));
            W32(e + 8, R32(env + E_PTR));
            reset = true;
        }
    } else if (msb == 1) {   /* loop end */
        if (R8(env + E_DATA_MSB) != 0xff && R8(env + E_DATA_LSB) != 0xff) {
            reset = true;
            uint32_t e = 0;
            if (!(R32(s2 + S2_FLAGS) & 8) && group) {
                e = FindLoop(env + E_LOOP, R8(env + E_DATA_MSB));
                if (!e && Debug()) plat_log("not found loop start %d", (int)R8(env + E_DATA_MSB));
            }
            if (e) {
                if (R8(e + 1) == 0xff) W8(e + 1, R8(env + E_DATA_LSB));
                if (R8(env + E_DATA_LSB) != 0 && R8(e + 1) == 0) {
                    W8(e + 1, 0xff);   /* loop over */
                } else {
                    if (uint32_t cb = R32(s2 + S2_LOOPCB)) Unsupported(4, "loop", cb);
                    W32(env + E_PTR, R32(e + 8));
                    uint32_t t = R32(e + 4);
                    W32(s2 + S2_TICK, t);
                    W32(env + E_NEXT, t);
                    W8(env + E_DFLAG, R8(e + 3));
                    W8(env + E_RUN, R8(e + 2));
                    if (R8(env + E_DATA_LSB)) W8(e + 1, (uint8_t)(R8(e + 1) - 1));
                }
            }
        }
    } else if (data) {   /* user data: 8 bit = val, 14 bit = (data MSB & 0x7f) << 7 | data LSB & 0x7f */
        if (R8(env + E_DATA_MSB) != 0xff && (data == 8 || R8(env + E_DATA_LSB) != 0xff)) {
            reset = true;
            uint32_t cb = R32(s2 + S2_NRPNCB);
            if (group && cb) Unsupported(8, "nrpn", cb);
        }
    } else if (msb == 0x7f) {
        reset = lsb == 0x7f;
    }
    (void)status;
    if (reset) {
        W8(env + E_DATA_LSB, 0xff);
        W8(env + E_DATA_MSB, 0xff);
        W8(env + E_NRPN_LSB, 0xff);
        W8(env + E_NRPN_MSB, 0xff);
    }
}

/* the next stream byte (PlayEnv's inline `lbu` + pointer step) */
static uint32_t NextByte(uint32_t env)
{
    uint32_t q = R32(env + E_PTR);
    W32(env + E_PTR, q + 1);
    return R8(q);
}

/* 0x1260 .. 0x17ac PlayEnv: the event player, ONE function in the original (f_1260 + the D_039a8 CC case blocks
 * f_14b0 .. f_15f8 + the join f_1694 share s0 = d2, s1 = msg, s2 = env, s3 = byte count (0 = nothing to send), s4 = d1,
 * s5 = S2, s6 = group). Plays every event due at the current tick S2 +8; returns 1 = the next event is in the future,
 * 0 = end of track (meta 0x2f: ended bit) or an error (data byte without running status, unknown status 0xf1 .. 0xfe,
 * tempo meta length != 3: no ended bit, Q4). An event is a status byte (bit 7) + d1, or a data byte = d1 under the
 * running status (written by channel events only, Q8). Channel messages are built unmasked (Send masks 0x7f7fff);
 * after one the delta-omission flag = (d1 | d2) & 0x80 (Q3: no delta follows); 0xf0+ events clear it. group 0 =
 * SetLocation's chase (nothing is written). */
int PlayEnv(uint32_t s2, uint32_t env, uint32_t group)
{
    if (R32(s2 + S2_TICK) < R32(env + E_NEXT)) return 1;
    do {
        uint32_t p = R32(env + E_PTR);
        uint32_t st = R8(p), d1;
        W32(env + E_PTR, p + 1);
        if (st & 0x80) {
            d1 = R8(p + 1);
            W32(env + E_PTR, p + 2);
        } else {
            uint32_t run = R8(env + E_RUN);
            if (!run) {
                if (Debug()) plat_log("playEnv running status error %02x %02x", st, run);
                return 0;
            }
            d1 = st;
            st = run;
        }
        if (st < 0xf0) {
            W8(env + E_RUN, (uint8_t)st);
            uint32_t ch = st & 0xf, cls = st & 0xf0, r = env + E_REC + ch * REC_SIZE;
            uint32_t msg = st | d1 << 8, d2 = 0;
            int n = 3;
            switch (cls) {
            case 0x80:
            case 0xd0: n = 2; break;
            case 0xc0:
                W8(r + REC_PROG, (uint8_t)(d1 & 0x7f));
                n = 2;
                break;
            case 0x90:
            case 0xe0:
                d2 = NextByte(env);
                msg |= d2 << 16;
                if (cls == 0xe0) W16(r + REC_BEND, (uint16_t)(msg >> 8));   /* d1 | d2 << 8, unmasked */
                break;
            case 0xa0:
                if (uint32_t ext = R32(env + E_EXT)) {
                    /* poly-aftertouch extension table (REV): 2 bytes (status, d1) per ((d1 & 0xf0) | ch), third byte
                     * (d1 & 0xf) << 3 | 7; no d2 is read; an index past the table sends nothing */
                    uint32_t i2 = ((d1 & 0xf0) | ch) << 1;
                    if (i2 < R16(ext + 2)) msg = R8(ext + 4 + i2) | (uint32_t)R8(ext + 5 + i2) << 8 | ((d1 & 0xf) << 3 | 7) << 16;
                    else n = 0;
                } else {
                    d2 = NextByte(env);
                    msg |= d2 << 16;
                }
                break;
            case 0xb0:
                d2 = NextByte(env);
                msg |= d2 << 16;
                if (d1 >= 100) break;   /* past D_039a8: forwarded */
                switch (d1) {
                case 0x00: W8(r + REC_CC0, (uint8_t)(d2 & 0x7f)); break;
                case 0x01: W8(r + REC_CC1, (uint8_t)(d2 & 0x7f)); break;
                case 0x02: W8(r + REC_CC2, (uint8_t)(d2 & 0x7f)); break;
                case 0x05: W8(r + REC_CC5, (uint8_t)(d2 & 0x7f)); break;
                case 0x0a: W8(r + REC_CC10, (uint8_t)(d2 & 0x7f)); break;
                case 0x0b: W8(r + REC_CC11, (uint8_t)(d2 & 0x7f)); break;
                case 0x40: W8(r + REC_CC64, (uint8_t)(d2 & 0x7f)); break;
                case 0x41: W8(r + REC_CC65, (uint8_t)(d2 & 0x7f)); break;
                case 0x07:   /* f_1520: recorded raw, forwarded scaled (Q6), bit 7 of d2 kept */
                    W8(env + E_VOL + ch, (uint8_t)(d2 & 0x7f));
                    W8(r + REC_CC7, (uint8_t)(d2 & 0x7f));
                    d2 = (d2 & 0x80) | VolScale(env, ch);
                    msg = (msg & 0xffff) | (d2 & 0xff) << 16;
                    break;
                case 0x20:   /* f_15dc: re-route the channel (this message already goes out on the new route) */
                    W16(s2 + S2_ROUTE + ch * 2, (uint16_t)(1u << (d2 & 0xf)));
                    break;
                case 0x06:
                case 0x26:
                case 0x62:
                case 0x63:   /* f_15f8, then forwarded */
                    Nrpn(s2, env, st, d1, d2 & 0x7f, group);
                    break;
                default: break;
                }
                break;
            }
            if (n) Send(s2, env, group, msg, n);
            W8(env + E_DFLAG, (uint8_t)((d1 | d2) & 0x80));
        } else {
            W8(env + E_DFLAG, 0);
            if (st == 0xf7) SysexCont(env);
            else if (st == 0xf0) Sysex(s2, env, group);
            else if (st == 0xff) {
                if (!Meta(s2, env, d1)) return 0;
            } else {
                if (Debug()) plat_log("playEnv unknown status %02x", st);
                return 0;
            }
        }
        NextDelta(env);
    } while (!(R32(s2 + S2_TICK) < R32(env + E_NEXT)));
    return 1;
}

/* ---------------------------------------------------------------- output */

/* 0x0ebc: output entry idx of the group, 0 past its end */
uint32_t OutEntry(uint32_t idx, uint32_t group)
{
    return idx < R32(group + GRP_N) ? R32(group + GRP_ENTRIES) + idx * 8 : 0;
}

/* 0x1084: the only writer of the stream buffers. Data bits 7 are masked here only (0x7f7fff), then the channel filter
 * (SYTH chMsgCB: a 0 result drops the message), the channel-used bit (before the group test: also set with group 0),
 * then n bytes, low byte first, into every buffer of the channel's route mask with room for them (used + n + 8 <= cap;
 * else that buffer drops it, Q11). No running status. */
void Send(uint32_t s2, uint32_t env, uint32_t group, uint32_t msg, int n)
{
    msg &= 0x7f7fff;
    uint32_t cb = R32(s2 + S2_CHCB);
    if (cb) {
        if (g_cbFn) {
            msg = g_cbFn(g_cbUser, cb, msg, R32(s2 + S2_CHCB_ARG));
        } else {
            static bool warned;
            if (!warned) plat_log("midi: channel-message callback %06x without a resolver (passed)", cb);
            warned = true;
        }
        if (!msg) return;
    }
    W16(env + E_USED, (uint16_t)(R16(env + E_USED) | ChBit(msg)));
    if (!group) return;
    uint32_t mask = R16(s2 + S2_ROUTE + (msg & 0xf) * 2);
    for (uint32_t idx = 0; mask; mask >>= 1, idx++) {
        if (!(mask & 1)) continue;
        uint32_t e = OutEntry(idx, group);
        uint32_t buf = e ? R32(e + 4) : 0;
        if (!buf) continue;
        uint32_t used = R32(buf + SBUF_USED), end = used + (uint32_t)n;
        if (R32(buf + SBUF_CAP) < end + 8) {
            if (Debug()) plat_log("sendChMsg Buffer OverRun");
            continue;
        }
        W32(buf + SBUF_USED, end);
        uint32_t p = buf + SBUF_DATA + used, m = msg;
        for (int i = n - 1; i != -1; i--, m >>= 8) W8(p++, (uint8_t)m);
    }
}

/* 0x1218: CC 7 value scaled by the channel factor and the master factor, max 0x7f */
uint32_t VolScale(uint32_t env, uint32_t ch)
{
    ch &= 0xff;
    uint32_t v = (uint32_t)R8(env + E_VOL + ch) * R8(env + E_VOLF + ch) * R8(env + E_MASTER);
    v >>= 14;
    return v < 0x80 ? v : 0x7f;
}

/* 0x17ac: per channel in the used mask: hold off (CC 64 = 0), all notes off (CC 123 = 0); then the mask is cleared */
void AllNotesOff(uint32_t s2, uint32_t group)
{
    uint32_t env = s2 + S2_ENV;
    for (uint32_t ch = 0; ch < 16; ch++) {
        if (!(R16(env + E_USED) & ChBit(ch))) continue;
        Send(s2, env, group, ch | 0x40b0, 3);
        Send(s2, env, group, ch | 0x7bb0, 3);
    }
    W16(env + E_USED, 0);
}

/* 0x1fec: CC 7 = VolScale, only while playing (Q5: ResendState runs before the flag is set) */
void SendVolume(uint32_t s2, uint32_t env, uint32_t group, uint32_t ch)
{
    if (!(R32(s2 + S2_FLAGS) & FL_PLAY)) return;
    uint32_t v = VolScale(env, ch & 0xff);
    Send(s2, env, group, ch | 0x7b0 | v << 16, 3);
}

/* 0x1910: per channel, every recorded controller (bit 7 clear = seen) again: CC 0, program, CC 1, 2, 5, CC 7 (via
 * SendVolume; the raw value goes back into the volume byte), CC 10, 11, 64, 65, pitch bend */
void ResendState(uint32_t s2, uint32_t group)
{
    uint32_t env = s2 + S2_ENV;
    static const struct { uint8_t off, cc; } CCS1[] = {{REC_CC1, 1}, {REC_CC2, 2}, {REC_CC5, 5}};
    static const struct { uint8_t off, cc; } CCS2[] = {{REC_CC10, 10}, {REC_CC11, 11}, {REC_CC64, 0x40}, {REC_CC65, 0x41}};
    for (uint32_t ch = 0; ch < 16; ch++) {
        uint32_t r = env + E_REC + ch * REC_SIZE;
        if (!(R8(r + REC_CC0) & 0x80)) Send(s2, env, group, ch | 0x00b0 | (uint32_t)R8(r + REC_CC0) << 16, 3);
        if (!(R8(r + REC_PROG) & 0x80)) Send(s2, env, group, ch | 0xc0 | (uint32_t)R8(r + REC_PROG) << 8, 2);
        for (const auto &c : CCS1)
            if (!(R8(r + c.off) & 0x80)) Send(s2, env, group, ch | 0xb0 | (uint32_t)c.cc << 8 | (uint32_t)R8(r + c.off) << 16, 3);
        uint8_t v7 = R8(r + REC_CC7);
        if (!(v7 & 0x80)) {
            W8(env + E_VOL + ch, v7);
            SendVolume(s2, env, group, ch);
        }
        for (const auto &c : CCS2)
            if (!(R8(r + c.off) & 0x80)) Send(s2, env, group, ch | 0xb0 | (uint32_t)c.cc << 8 | (uint32_t)R8(r + c.off) << 16, 3);
        uint16_t bend = R16(r + REC_BEND);
        if (!(bend & 0x8080)) Send(s2, env, group, ch | 0xe0 | (uint32_t)bend << 8, 3);
    }
}

/* ---------------------------------------------------------------- sequence control */

/* 0x2614: S2 = input entry 2 port + 1, env = S2 + 0x5c; no null test of S2 (Q1: the original also stores S2 through
 * a null s2 pointer, never passed) */
int PortOf(uint32_t ctx, uint32_t port, uint32_t *s2, uint32_t *env)
{
    if (!ctx) return 0;
    uint32_t g = R32(ctx + CTX_GROUP);
    uint32_t k = port * 2 + 1;
    if (!(k < R32(g + GRP_N))) return 0;
    uint32_t p = R32(R32(g + GRP_ENTRIES) + k * 8 + 4);
    if (!s2) return 0;
    *s2 = p;
    *env = p + S2_ENV;
    return 1;
}

/* 0x1c90: tick 0 of the selected sequence: counters, running status, NRPN bytes, channel volumes, controller records,
 * tick length, then the first delta */
int RewindEnv(uint32_t s2)
{
    uint32_t env = s2 + S2_ENV;
    W32(s2 + S2_TICK, 0);
    W32(env + E_NEXT, 0);
    W32(env + E_ACC, 0);
    W32(env + E_TLEN, 0);
    W8(env + E_RUN, 0);
    W8(env + E_DFLAG, 0);
    W8(env + E_DATA_MSB, 0xff);
    W8(env + E_NRPN_MSB, 0xff);
    W8(env + E_DATA_LSB, 0xff);
    W8(env + E_NRPN_LSB, 0xff);
    W32(env + E_PTR, R32(env + E_EV0));
    for (uint32_t ch = 0; ch < 16; ch++) {
        uint32_t r = env + E_REC + ch * REC_SIZE;
        W8(env + E_VOL + ch, 0x7f);
        for (uint32_t i = 0; i < 10; i++) W8(r + i, 0xff);
        W16(r + REC_BEND, 0xffff);
    }
    Recalc(env);
    NextDelta(env);
    return 1;
}

/* 0x1bb4 sceMidi_MidiPlaySwitch: 0 = stop (AllNotesOff, if playing), 1 = play (loaded, not ended, selected:
 * ResendState, then the flag), other values: 0 */
int MidiPlaySwitch(uint32_t ctx, uint32_t port, uint32_t on)
{
    if (!ctx) return -1;
    uint32_t g = R32(ctx + CTX_GROUP);
    uint32_t k = port * 2 + 1;
    if (!(k < R32(g + GRP_N))) return -1;
    uint32_t s2 = R32(R32(g + GRP_ENTRIES) + k * 8 + 4);
    if (!s2) return -1;
    uint32_t g1 = g + GRP_SIZE;
    if (on == 0) {
        if (!(R32(s2 + S2_FLAGS) & FL_PLAY)) return 0;
        AllNotesOff(s2, g1);
        W32(s2 + S2_FLAGS, R32(s2 + S2_FLAGS) & ~FL_PLAY);
        return 0;
    }
    if (on != 1) return 0;
    if ((R32(s2 + S2_FLAGS) & (FL_LOADED | FL_ENDED)) != FL_LOADED) return -1;
    if (R32(s2 + S2_SEQ) == 0xffffffffu) return -1;
    ResendState(s2, g1);
    W32(s2 + S2_FLAGS, R32(s2 + S2_FLAGS) | FL_PLAY);
    return 0;
}

/* 0x1d50 sceMidi_SelectMidi: stops a playing port, clears playing / ended (not 0x100), then sequence idx of the Midi
 * chunk (whose +0xc word is the LAST index, Q2), the extension-header test, the loop table, tempo 500,000 us / quarter,
 * relative tempo 1.0, the division, RewindEnv */
int SelectMidi(uint32_t ctx, uint32_t port, uint32_t idx)
{
    uint32_t s2, env;
    if (!PortOf(ctx, port, &s2, &env)) return -1;
    uint32_t mid = R32(env + E_MIDI);
    if (R32(s2 + S2_FLAGS) & FL_PLAY) MidiPlaySwitch(ctx, port, 0);
    uint32_t fl = R32(s2 + S2_FLAGS) & ~(FL_PLAY | FL_ENDED);
    W32(s2 + S2_FLAGS, fl);
    W32(s2 + S2_SEQ, 0xffffffffu);
    if (!(fl & FL_LOADED)) return -1;
    if (!mid) return -1;
    if (R32(mid + 0xc) < idx) return -1;
    uint32_t off = R32(mid + 0x10 + idx * 4);
    if (off == 0xffffffffu) return -1;
    uint32_t seq = mid + off;
    W32(env + E_EXT, seq + 6);
    uint32_t evoff = R32(seq);
    if (evoff == 0xffffffffu) return -1;
    if (R16(seq + 6) != 1 || R16(seq + 8) == 0 || evoff < 0xc) W32(env + E_EXT, 0);
    for (uint32_t i = 0; i < LOOP_N; i++) {
        W8(env + E_LOOP + i * LOOP_SIZE, 0xff);
        W8(env + E_LOOP + i * LOOP_SIZE + 1, 0xff);
    }
    W32(env + E_EV0, seq + R32(seq));
    W32(env + E_TEMPO, 0x7a120);
    W32(env + E_REL, 0x100);
    W32(env + E_DIV, R16(seq + 4));
    if (!RewindEnv(s2)) return -1;
    W32(s2 + S2_SEQ, idx);
    return 0;
}

/* 0x1efc sceMidi_MidiSetLocation: back = rewind; forward = chase with group 0 (events run, nothing is written; the
 * tick jumps to the next event time, Q12). REV: SYTH always passes 0 (a no-op after Select). */
int MidiSetLocation(uint32_t ctx, uint32_t port, uint32_t tick)
{
    uint32_t s2, env;
    if (!PortOf(ctx, port, &s2, &env)) return -1;
    if (tick < R32(s2 + S2_TICK) && !RewindEnv(s2)) return -1;
    while (R32(s2 + S2_TICK) < tick) {
        if (R32(s2 + S2_FLAGS) & FL_ENDED) return -1;
        uint32_t next = R32(env + E_NEXT);
        if (!(next < tick)) {
            W32(s2 + S2_TICK, tick);
            break;
        }
        W32(s2 + S2_TICK, next);
        if (!PlayEnv(s2, env, 0)) break;
    }
    return R32(s2 + S2_TICK) == tick ? 0 : -1;
}

/* 0x22d8 sceMidi_MidiGetAbsoluteTempo (REV: SYTH midiRelTempo, never RPC'd): 60,000,000 / tempo clamped to
 * 20 .. 255; 0 for a bad port, no sequence or tempo 0 */
int MidiGetAbsoluteTempo(uint32_t ctx, uint32_t port)
{
    uint32_t s2, env;
    if (!PortOf(ctx, port, &s2, &env)) return 0;
    if (R32(s2 + S2_SEQ) == 0xffffffffu) return 0;
    uint32_t t = R32(env + E_TEMPO);
    if (!t) return 0;
    uint32_t bpm = 60000000u / t;
    if (bpm < 20) bpm = 20;
    else if (bpm >= 0x100) bpm = 0xff;
    return (int)(bpm & 0xff);
}

/* 0x2374 sceMidi_MidiSetRelativeTempo */
int MidiSetRelativeTempo(uint32_t ctx, uint32_t port, uint32_t t)
{
    uint32_t s2, env;
    if (!PortOf(ctx, port, &s2, &env)) return -1;
    W32(env + E_REL, t & 0xffff);
    Recalc(env);
    return 0;
}

}  // namespace midi
