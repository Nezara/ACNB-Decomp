/* sein - see sein.h. Comments give the original's module offset. The
 * original is -O0 code (every local through the frame); the builders assemble the payload in a stack buffer and hand it
 * to the emitter f_03f0, natively a host array. Memory outside IOP RAM reads 0 and drops writes. */
#include "sein.h"
#include <string.h>

namespace sein {

static uint32_t g_base;

static uint8_t R8(uint32_t a) { const uint8_t *p = iopk::Ptr(a); return p ? *p : 0; }
static uint32_t R32(uint32_t a) { const uint8_t *p = iopk::Ptr(a); uint32_t v = 0; if (p) memcpy(&v, p, 4); return v; }
static void W8(uint32_t a, uint8_t v) { uint8_t *p = iopk::Ptr(a); if (p) *p = v; }
static void W32(uint32_t a, uint32_t v) { uint8_t *p = iopk::Ptr(a); if (p) memcpy(p, &v, 4); }

/* 0x000: CpuSuspendIntr; RegisterLibraryEntries (vanishes: 0 = registered); CpuResumeIntr; returns r ? 1 : 0 */
int ModuleStart(const iopk::ModuleArgs *m)
{
    g_base = m->base;
    int st;
    iopk::CpuSuspendIntr(&st);
    iopk::CpuResumeIntr(st);
    return 0;
}

uint32_t Base() { return g_base; }

/* 0x070: ctx[0] (group count, signed) >= 2, group 1 = {buffNum, buffCtx} non-zero, and every buffer entry's pointer
 * (entry + 4) non-zero with cap >= 8 (the map had "ptr == 0 passes": the asm returns -1 for it too) */
int Init(uint32_t ctx)
{
    if (!ctx) return -1;
    uint32_t grp = R32(ctx + 4);
    if ((int32_t)R32(ctx) < 2) return -1;
    if (!grp) return -1;
    uint32_t n = R32(grp + 8), bc = R32(grp + 0xc);
    if (!n || !bc) return -1;
    for (int32_t i = 0; i < (int32_t)n; i++) {
        uint32_t p = R32(bc + (uint32_t)i * 8 + 4);
        if (!p || R32(p) < 8) return -1;
    }
    return 0;
}

/* 0x200 / 0x228: empty hooks */
int ATick(uint32_t) { return 0; }
int Load(uint32_t) { return 0; }

/* 0x3f0, the emitter: port < group 1's buffNum (unsigned); buffer = entry[port] + 4; room test cap >= used + len + 6 + 8
 * (u32, the 8-byte header included - unlike SYTH's FlushSe); then fe 01 id[0..3] and len payload bytes at
 * buf + 8 + used, used += len + 6. The payload comes from a host array (builders) or IOP memory (export 8). */
static int emit(uint32_t ctx, uint32_t port, uint32_t id, const uint8_t *host, uint32_t iop, uint32_t len)
{
    uint32_t total = len + 6;
    if (!ctx) return -1;
    uint32_t g = R32(ctx + 4) + 8;
    if (!(port < R32(g))) return -1;
    uint32_t buf = R32(R32(g + 4) + port * 8 + 4);
    if (R32(buf) < total + 8 + R32(buf + 4)) return -1;
    uint32_t p = buf + R32(buf + 4) + 8;
    W8(p++, 0xfe);
    W8(p++, 1);
    W8(p++, (uint8_t)id);
    W8(p++, (uint8_t)(id >> 8));
    W8(p++, (uint8_t)(id >> 16));
    W8(p++, (uint8_t)(id >> 24));
    for (uint32_t k = len; k; k--) W8(p++, host ? *host++ : R8(iop++));
    W32(buf + 4, R32(buf + 4) + total);
    return 0;
}

int PutSeMsg(uint32_t ctx, uint32_t port, uint32_t id, uint32_t data, uint32_t len)
{
    return emit(ctx, port, id, 0, data, len);
}

/* 0x250: the VLQ writer: 4 groups of 7 bits + (v >> 28) & 0xf, emitted from the highest non-zero group (group 0 at
 * least) down, bit 7 set on all but the last; returns the byte count */
static int vlq(uint32_t v, uint8_t **out)
{
    uint8_t g[5];
    for (int i = 0; i < 4; i++) {
        g[i] = (uint8_t)(v & 0x7f);
        v >>= 7;
    }
    g[4] = (uint8_t)(v & 0xf);
    int k = 4;
    while (!g[k]) {
        if (--k <= 0) break;
    }
    int n = 1;
    for (int i = k; i > 0; i--, n++) *(*out)++ = (uint8_t)(g[i] | 0x80);
    *(*out)++ = g[0];
    return n;
}

/* 0x664: PutMsg(ctx, port, id, w0, ...): the 4 bytes of w0's argument slot, then 3 bytes of the first variadic slot
 * (the original copies the slot bytes: little-endian layout) */
int PutMsg(uint32_t ctx, uint32_t port, uint32_t id, uint32_t w0, uint32_t w1)
{
    if ((w0 & 0xf0) != 0xa0) return -1;
    uint8_t b[8];
    uint8_t *p = b;
    for (int k = 0; k < 4; k++) *p++ = (uint8_t)(w0 >> (8 * k));
    for (int k = 0; k < 3; k++) *p++ = (uint8_t)(w1 >> (8 * k));
    return emit(ctx, port, id, b, 0, (uint32_t)(p - b));
}

/* the note records: st|bank prog note velo pan lo hi (lo / hi = bend & 0x7f, (bend & 0x3f80) >> 7, or 0 0) */
static int note(uint32_t ctx, uint32_t port, uint32_t id, uint8_t st, uint32_t bank, uint32_t prog, uint32_t note_,
                uint32_t velo, uint32_t pan, bool has_bend, uint32_t bend)
{
    if (!(bank < 0x10)) return -1;
    uint8_t b[16];
    uint8_t *p = b;
    *p++ = (uint8_t)(bank | st);
    *p++ = (uint8_t)prog;
    *p++ = (uint8_t)note_;
    *p++ = (uint8_t)velo;
    *p++ = (uint8_t)pan;
    *p++ = has_bend ? (uint8_t)(bend & 0x7f) : 0;
    *p++ = has_bend ? (uint8_t)((bend & 0x3f80) >> 7) : 0;
    return emit(ctx, port, id, b, 0, (uint32_t)(p - b));
}

/* 0x828 */
int NoteOn(uint32_t ctx, uint32_t port, uint32_t id, uint32_t bank, uint32_t prog, uint32_t n, uint32_t velo, uint32_t pan)
{
    return note(ctx, port, id, 0xa0, bank, prog, n, velo, pan, false, 0);
}
/* 0x9e0 */
int PitchOn(uint32_t ctx, uint32_t port, uint32_t id, uint32_t bank, uint32_t prog, uint32_t n, uint32_t velo, uint32_t pan,
            uint32_t bend)
{
    return note(ctx, port, id, 0xa0, bank, prog, n, velo, pan, true, bend);
}
/* 0xbb0 */
int NoteOn90(uint32_t ctx, uint32_t port, uint32_t id, uint32_t bank, uint32_t prog, uint32_t n, uint32_t velo, uint32_t pan)
{
    return note(ctx, port, id, 0x90, bank, prog, n, velo, pan, false, 0);
}
/* 0xd68 */
int PitchOn90(uint32_t ctx, uint32_t port, uint32_t id, uint32_t bank, uint32_t prog, uint32_t n, uint32_t velo,
              uint32_t pan, uint32_t bend)
{
    return note(ctx, port, id, 0x90, bank, prog, n, velo, pan, true, bend);
}

/* 0xf38: c0 1f */
int AllNoteOff(uint32_t ctx, uint32_t port, uint32_t id)
{
    const uint8_t b[2] = {0xc0, 0x1f};
    return emit(ctx, port, id, b, 0, 2);
}

/* 0xff0: c0 1e x (u32 LE) y (u32 LE) */
int AllNoteOffMask(uint32_t ctx, uint32_t port, uint32_t id, uint32_t x, uint32_t y)
{
    uint8_t b[16];
    uint8_t *p = b;
    *p++ = 0xc0;
    *p++ = 0x1e;
    for (int k = 0; k < 4; k++) *p++ = (uint8_t)(x >> (8 * k));
    for (int k = 0; k < 4; k++) *p++ = (uint8_t)(y >> (8 * k));
    return emit(ctx, port, id, b, 0, (uint32_t)(p - b));
}

/* the b0 head: b0|bank a b kind */
static uint8_t *ctl_head(uint8_t *p, uint32_t bank, uint32_t a, uint32_t b, uint32_t kind)
{
    *p++ = (uint8_t)(bank | 0xb0);
    *p++ = (uint8_t)a;
    *p++ = (uint8_t)b;
    *p++ = (uint8_t)kind;
    return p;
}

/* 0x1254: b0|bank a b 07 VLQ(v) c 00 */
int TimeVolume(uint32_t ctx, uint32_t port, uint32_t id, uint32_t bank, uint32_t a, uint32_t b, uint32_t v, uint32_t c)
{
    if (!(bank < 0x10)) return -1;
    uint8_t buf[16];
    uint8_t *p = ctl_head(buf, bank, a, b, 7);
    vlq(v, &p);
    *p++ = (uint8_t)c;
    *p++ = 0;
    return emit(ctx, port, id, buf, 0, (uint32_t)(p - buf));
}

/* 0x1400: kind 0xc / 0xd: b0|bank a b kind VLQ(v) c 00 */
int TimePanpot(uint32_t ctx, uint32_t port, uint32_t id, uint32_t bank, uint32_t a, uint32_t b, uint32_t v, uint32_t c,
               uint32_t kind)
{
    if (!(bank < 0x10)) return -1;
    if (kind != 0xc && kind != 0xd) return -1;
    uint8_t buf[16];
    uint8_t *p = ctl_head(buf, bank, a, b, kind);
    vlq(v, &p);
    *p++ = (uint8_t)c;
    *p++ = 0;
    return emit(ctx, port, id, buf, 0, (uint32_t)(p - buf));
}

/* 0x15d8: kind 0xe / 0xf: b0|bank a b kind VLQ(v) c (c & 0xff00) >> 8 */
int TimePitch(uint32_t ctx, uint32_t port, uint32_t id, uint32_t bank, uint32_t a, uint32_t b, uint32_t v, uint32_t c,
              uint32_t kind)
{
    if (!(bank < 0x10)) return -1;
    if (kind != 0xe && kind != 0xf) return -1;
    uint8_t buf[16];
    uint8_t *p = ctl_head(buf, bank, a, b, kind);
    vlq(v, &p);
    *p++ = (uint8_t)c;
    *p++ = (uint8_t)((c & 0xff00) >> 8);
    return emit(ctx, port, id, buf, 0, (uint32_t)(p - buf));
}

/* 0x17c0: kind 0x10 / 0x11 / 0x12: b0|bank a b kind, then VLQ(v) for 0x12, else v (c, c >> 8: 2 bytes) */
int PitchLFO(uint32_t ctx, uint32_t port, uint32_t id, uint32_t bank, uint32_t a, uint32_t b, uint32_t v, uint32_t kind)
{
    if (!(bank < 0x10)) return -1;
    if (kind != 0x10 && kind != 0x11 && kind != 0x12) return -1;
    uint8_t buf[16];
    uint8_t *p = ctl_head(buf, bank, a, b, kind);
    if (kind == 0x12) vlq(v, &p);
    else {
        *p++ = (uint8_t)v;
        *p++ = (uint8_t)((v & 0xff00) >> 8);
    }
    return emit(ctx, port, id, buf, 0, (uint32_t)(p - buf));
}

/* 0x19cc: kind 0x20 / 0x21 / 0x22: b0|bank a b kind, then VLQ(v) for 0x22, else ONE byte v (hsyn's parser reads two
 * for 0x20 / 0x21: mismatch, kept) */
int AmpLFO(uint32_t ctx, uint32_t port, uint32_t id, uint32_t bank, uint32_t a, uint32_t b, uint32_t v, uint32_t kind)
{
    if (!(bank < 0x10)) return -1;
    if (kind != 0x20 && kind != 0x21 && kind != 0x22) return -1;
    uint8_t buf[16];
    uint8_t *p = ctl_head(buf, bank, a, b, kind);
    if (kind == 0x22) vlq(v, &p);
    else *p++ = (uint8_t)v;
    return emit(ctx, port, id, buf, 0, (uint32_t)(p - buf));
}

}  // namespace sein
