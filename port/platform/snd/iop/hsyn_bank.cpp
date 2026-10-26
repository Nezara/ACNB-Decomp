/* hsyn_bank - see hsyn.h. Group H3 of NI6b: Load with the
 * HD chunk-tag check, the reverb / effect setup (SetEffectAttr + the MIDI data-entry effect changes f_1e70 / f_1ee0),
 * GetChStat, the output mode, SESetMaxVoices, VoiceTrans and the sd log ring (SetLogRing + the three helpers the
 * hsyn_sd.h wrappers call). The SE / MS voice queries are in hsyn_query.cpp. Function comments give the module offset. */
#include "hsyn.h"
#include "hsyn_state.h"
#include "hsyn_sd.h"

namespace hsyn {

/* ---------------------------------------------------------------- Load */

/* HD chunk ids: a pair of u32, the text stored byte-reversed in the file ("IECS" "sreV"); f_02e0 compares byte i of each
 * word with name[3 - i] of the forward .rodata strings (0xb2d0 ..), i.e. le32(chunk) == be32(name). */
constexpr uint32_t TAG_SCEI = 0x53434549u, TAG_VERS = 0x56657273u, TAG_HEAD = 0x48656164u, TAG_PROG = 0x50726f67u,
                   TAG_SSET = 0x53736574u, TAG_SMPL = 0x536d706cu, TAG_VAGI = 0x56616769u, TAG_SETB = 0x53657462u;
constexpr uint32_t ABSENT = 0xffffffffu;   /* chunk offset -1 */

/* 0x02e0: 1 = match (pure; the original's early exit has no side effect) */
static int ChunkTag(uint32_t chunk, uint32_t name)
{
    return R32(chunk) == TAG_SCEI && R32(chunk + 4) == name;
}

/* 0x034c, export 6. The slot is zeroed before any check; a tag mismatch zeroes it again and returns -1 WITHOUT the
 * channel detach; bd_spu == 0 or hd == 0 = unload (zero + detach, 0). Only the 8-byte tags are validated. No test of
 * the group / block pointer (as f_1b84); the slot and port compares are unsigned. */
int Load(uint32_t ctx, uint32_t port, uint32_t bd_spu, uint32_t hd, uint32_t slot)
{
    if (ctx == 0 || !(slot < 0x10)) return -1;
    uint32_t g = R32(ctx + CTX_GROUP);
    uint32_t k = port * 2 + 1;
    if (!(k < R32(g + GRP_N))) return -1;
    uint32_t blk = R32(k * ENTRY_SIZE + R32(g + GRP_ENTRIES) + ENTRY_PTR);
    if (Blk(blk)->singleBank == 1) slot = 0;   /* dead: nothing sets singleBank */
    uint32_t sa = SlotAddr(blk, slot);
    BankSlot *s = Slot(sa);
    s->head = s->prog = s->sset = s->smpl = s->vagi = s->setb = s->bd = 0;
    if (bd_spu != 0 && hd != 0) {
        /* the chunk offsets (from the HD start) are read through the head chunk pointer: head + 0x14 .. 0x24 */
        s->head = hd + 0x10;
        uint32_t o = R32(s->head + 0x14);
        if (o != ABSENT) s->prog = hd + o;
        o = R32(s->head + 0x18);
        if (o != ABSENT) s->sset = hd + o;
        o = R32(s->head + 0x1c);
        if (o != ABSENT) s->smpl = hd + o;
        o = R32(s->head + 0x20);
        if (o != ABSENT) s->vagi = hd + o;
        if (R8(hd + 0xe) >= 2) {   /* format version 2: Setb chunk */
            o = R32(s->head + 0x24);
            if (o != ABSENT) s->setb = hd + o;
        }
        bool ok = ChunkTag(hd, TAG_VERS) && ChunkTag(s->head, TAG_HEAD) && (s->prog == 0 || ChunkTag(s->prog, TAG_PROG)) &&
                  (s->sset == 0 || ChunkTag(s->sset, TAG_SSET)) && (s->smpl == 0 || ChunkTag(s->smpl, TAG_SMPL)) &&
                  (s->vagi == 0 || ChunkTag(s->vagi, TAG_VAGI)) && (s->setb == 0 || ChunkTag(s->setb, TAG_SETB));
        if (!ok) {
            s->head = s->prog = s->sset = s->smpl = s->vagi = s->setb = s->bd = 0;
            return -1;
        }
        s->bd = bd_spu;
    }
    /* detach: a channel whose program came from this slot (address compare) forgets its Prog entry and slot */
    for (uint32_t i = 0; i < 16; i++) {
        Channel *c = Ch(ChannelAddr(blk, i));
        if (c->slot == sa) {
            c->prog = 0;
            c->slot = 0;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- effects */

/* D_0b678 (.data, u32 x 16): the effect type of the record's mode nibble; 11..15 -> 0 (off) */
static const uint32_t kEffectMode[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 0, 0, 0, 0};

/* 0x1d7c, export 9. rec (IOP RAM): +0 u32 core, +4 u32 mode (low nibble type, 0x100 = clear the work area), +8 / +0xa
 * u16 depth L / R, +0xc delay, +0x10 feedback, +0x14 / +0x16 u16 EVOL L / R. The core-attr "on" flag comes from the
 * table value before 0x100 is or-ed in; bit 0x200 is ignored. */
int SetEffectAttr(uint32_t rec)
{
    sd::EffectAttr a;
    a.core = (int32_t)R32(rec);
    a.depth_l = R16(rec + 8);
    a.depth_r = R16(rec + 0xa);
    a.delay = R32(rec + 0xc);
    a.feedback = R32(rec + 0x10);
    uint32_t m = kEffectMode[R32(rec + 4) & 0xf];
    a.mode = m;
    uint32_t on = m != 0;
    if (R32(rec + 4) & 0x100) a.mode = m | 0x100;
    SdSetEffectAttr((int32_t)R32(rec), &a);
    SdSetCoreAttr(R16(rec) | 2u, on);
    SdSetParam(R16(rec) | 0xb80u, R16(rec + 0x14));   /* EVOL L */
    SdSetParam(R16(rec) | 0xc80u, R16(rec + 0x16));   /* EVOL R */
    return 0;
}

/* 0x1e70 (CC 6 data entry, M5 f_1280; unreached): effect type val (< 10: type 10 is not reachable this way) on core
 * sel >> 4 for sel 0 / 0x10 only; the work area is always cleared (0x100) */
void NrpnEffectType(uint8_t sel, uint8_t val)
{
    if (!(val < 10)) return;
    if (sel != 0 && sel != 0x10) return;
    int core = (sel & 0x10) != 0;
    sd::EffectAttr a = {};
    SdGetEffectAttr(core, &a);
    a.mode = val | 0x100u;
    SdSetEffectAttr(core, &a);
}

/* 0x1ee0 (unreached): sel & 0xf = 0 depth +val << 8, 1 depth -(val << 8), 2 delay, 3 feedback; other selectors still
 * re-apply the unchanged attr (quirk) */
void NrpnEffectParam(uint8_t sel, uint8_t val)
{
    if (sel & 0xe0) return;
    int core = (sel & 0x10) != 0;
    sd::EffectAttr a = {};
    SdGetEffectAttr(core, &a);
    switch (sel & 0xf) {
    case 0:
        a.depth_l = a.depth_r = (uint16_t)(val << 8);
        break;
    case 1:
        a.depth_l = a.depth_r = (uint16_t)-(int32_t)(val << 8);
        break;
    case 2:
        a.delay = val;
        break;
    case 3:
        a.feedback = val;
        break;
    default:
        break;
    }
    SdSetEffectAttr(core, &a);
}

/* ---------------------------------------------------------------- small exports */

/* 0x1fb8, export 17 (unreached): per channel bit 0 / 1 / 2 = list 1 / 2 / 3 non-empty (head word != pseudo-node; a
 * corrupt list also reads non-empty) */
int GetChStat(uint32_t ctx, uint32_t port, uint32_t buf)
{
    uint32_t blk, b14;
    if (!PortLookup(ctx, port, &blk, &b14)) return -1;
    for (uint32_t i = 0; i < 16; i++) {
        uint32_t ch = b14 + 4 + CH_SIZE * i;
        uint32_t f = Lw(ch + CH_LIST1) != ch - 8;
        if (Lw(ch + CH_LIST2) != ch) f |= 2;
        if (Lw(ch + CH_LIST3) != ch + 8) f |= 4;
        W8(buf + i, (uint8_t)f);
    }
    return 0;
}

/* 0x2054, export 18 */
int SetOutputMode(int mode)
{
    if (mode == 0) {
        S().mono = 1;
        return 0;
    }
    if (mode == 1) {
        S().mono = 0;
        return 0;
    }
    return -1;
}

/* 0x2094, export 27 (no importer): mono ^ 1 != 0 */
int GetOutputMode() { return S().mono != 1; }

/* 0x2f5c, export 19: the byte is tested, the low byte stored (0x130 passes as 0x30) */
int SESetMaxVoices(uint32_t n)
{
    if (!((n & 0xff) < 0x31)) return -1;
    S().seMaxVoices = (uint8_t)n;
    return 0;
}

/* 0x0230, export 7 (SYTH RPC 0x10000, unreached): wait, transfer (mode 0), wait; 0 when sd moved the 64-byte-rounded
 * size (not the requested one) */
int VoiceTrans(uint32_t ch, uint32_t iop, uint32_t spu, uint32_t size)
{
    uint32_t n64 = (size >> 6 << 6) + ((size & 0x3f) ? 0x40 : 0);
    int c = (int16_t)ch;
    SdVoiceTransStatus((uint32_t)c, 1);
    int r = SdVoiceTrans((uint32_t)c, 0, iop, spu, size);
    SdVoiceTransStatus((uint32_t)c, 1);
    return (uint32_t)r == n64 ? 0 : -1;
}

/* ---------------------------------------------------------------- the sd log ring (dead: nothing installs one) */

/* 0x9bb0, export 16 (no importer). ring: +0 entry count, +4 (zeroed), +8 next index, entries (0x1c) from +0xc */
int SetLogRing(uint32_t ring)
{
    W32(g_base + D_LOGRING, ring);
    if (ring) {
        W32(ring + 8, 0);
        W32(ring + 4, 0);
    }
    return 0;
}

/* 0x9bcc: the next entry (index wraps at the count) */
uint32_t LogAlloc()
{
    uint32_t ring = R32(g_base + D_LOGRING);
    if (!ring) return 0;
    uint32_t i = R32(ring + 8);
    uint32_t n = R32(ring);
    W32(ring + 8, i + 1);
    if (!(i + 1 < n)) W32(ring + 8, 0);
    return ring + 0xc + 0x1c * i;
}

/* 0x9c24: +0 kind | 0x80000000 (call in progress), +8 / +0xc the register arguments, +0x10 .. +0x18 the stack ones */
void LogFill(uint32_t e, uint32_t kind, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t f)
{
    if (!e) return;
    W32(e, kind | 0x80000000u);
    W32(e + 8, a);
    W32(e + 0xc, b);
    W32(e + 0x10, c);
    W32(e + 0x14, d);
    W32(e + 0x18, f);
}

/* 0x9c68: +4 result, in-progress bit cleared */
void LogDone(uint32_t e, uint32_t r)
{
    if (!e) return;
    uint32_t k = R32(e);
    W32(e + 4, r);
    W32(e, k & 0x7fffffffu);
}

}  // namespace hsyn
