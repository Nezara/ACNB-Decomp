/* hsyn_query - see hsyn.h. Group H3 of NI6b: the SE / MS voice queries and the
 * SE-port release / kill helpers; all unreached in Versus (four of the six exports have no importer). A query walks the
 * three channel voice lists (pseudo-nodes ch - 8, ch, ch + 8) tail -> head through the layout-B prev word (+0xc): SE
 * ports (blk flags bit 0) only channel 0, MS ports all 16 channels. The buffer store comes before the count test, so
 * one entry is written for any n <= 0 as soon as a voice matches (quirk). Function comments give the module offset. */
#include "hsyn.h"
#include "hsyn_state.h"
#include "hsyn_sd.h"

namespace hsyn {

/* ---------------------------------------------------------------- SE ports */

/* 0x7358 (f_2f84): every voice of channel 0's list 1 (tail first) is unlinked; a pending one (flags & 8) is released
 * (f_7338 = M3 f_5e80), the others lose 0x300, get a queued KOFF and go to the front of list 3 (statbuf flag 0x30) */
void SeReleaseAllCh(uint32_t ch0)
{
    uint32_t p = ch0 - 8;
    for (uint32_t va; (va = Lw(p + 0xc)) != p;) {
        ListB_Unlink(va);
        Voice *v = V(va);
        uint16_t f = v->flags;
        if (f & 8) {
            ReleaseVoice(va);
            continue;
        }
        v->flags = f & 0xfcff;
        S().maskKoff[v->core] |= 1u << (v->idx & 31);
        ListB_PushFront(va, ch0 + CH_LIST3);
        if (S().statBuf) {
            uint8_t &s = StatFlag(v->core, v->idx);
            s = (uint8_t)((s & 0x8f) | 0x30);
        }
    }
}

/* 0x7750 (f_2fdc): lists 1 then 3 of channel 0, tail first: queued KOFF, ADSR2 = 0xc021 (immediate fast release),
 * kill (f_782c = f_6314) */
void SeKillAllCh(uint32_t ch0)
{
    const uint32_t lists[2] = {ch0 - 8, ch0 + 8};
    for (uint32_t p : lists)
        for (uint32_t va; (va = Lw(p + 0xc)) != p;) {
            Voice *v = V(va);
            S().maskKoff[v->core] |= 1u << (v->idx & 31);
            SdSetParam(0x400u | v->core | (uint32_t)v->idx << 1, 0xc021);
            Kill(va);
        }
}

/* 0x2f84, export 20 (no importer) */
int SEReleaseAll(uint32_t ctx, uint32_t port)
{
    uint32_t blk, b14;
    if (!PortLookup(ctx, port, &blk, &b14)) return -1;
    if (!(Blk(blk)->flags & 1)) return -1;
    SeReleaseAllCh(b14 + 4);
    return 0;
}

/* 0x2fdc, export 21 (no importer) */
int SEKillAll(uint32_t ctx, uint32_t port)
{
    uint32_t blk, b14;
    if (!PortLookup(ctx, port, &blk, &b14)) return -1;
    if (!(Blk(blk)->flags & 1)) return -1;
    SeKillAllCh(b14 + 4);
    return 0;
}

/* 0x3034, export 22 (SYTH RPC 0x11700, unreached): u8 voice numbers (core * 24 + idx) of the voices with this id on
 * channel 0 of an SE port; n is used as s8 */
int SERetrieveVoiceNumberByID(uint32_t ctx, uint32_t port, uint32_t id, uint32_t buf, int32_t n)
{
    uint32_t blk, b14;
    if (!PortLookup(ctx, port, &blk, &b14)) return -1;
    if (!(Blk(blk)->flags & 1)) return -1;
    int32_t lim = (int8_t)n, cnt = 0;
    const uint32_t lists[3] = {b14 - 4, b14 + 4, b14 + 0xc};
    for (uint32_t p : lists)
        for (uint32_t va = Lw(p + 0xc); va != p; va = Lw(va + 0xc)) {
            Voice *v = V(va);
            if (v->id != id) continue;
            W8(buf + (uint32_t)cnt, (uint8_t)(v->core * 24 + v->idx));
            cnt++;
            if (!(cnt < lim)) return cnt;
        }
    return cnt;
}

/* 0x3148, export 25 (no importer): the distinct u32 voice ids on channel 0 of an SE port */
int SEGetVoiceIDs(uint32_t ctx, uint32_t port, uint32_t buf, int32_t n)
{
    uint32_t blk, b14;
    if (!PortLookup(ctx, port, &blk, &b14)) return -1;
    if (!(Blk(blk)->flags & 1)) return -1;
    int32_t cnt = 0;
    const uint32_t lists[3] = {b14 - 4, b14 + 4, b14 + 0xc};
    for (uint32_t p : lists)
        for (uint32_t va = Lw(p + 0xc); va != p; va = Lw(va + 0xc)) {
            uint32_t id = V(va)->id;
            int32_t j = 0;
            while (j < cnt && R32(buf + 4 * (uint32_t)j) != id) j++;
            if (j < cnt) continue;   /* already listed */
            W32(buf + 4 * (uint32_t)cnt, id);
            cnt++;
            if (!(cnt < n)) return cnt;
        }
    return cnt;
}

/* ---------------------------------------------------------------- MS ports */

/* the MS walk of 0x3264 / 0x33b8: every voice with v->id == (id & 0xff) on the 16 channels, lists 1 / 2 / 3 each,
 * tail first; requires the voice-stat buffer and an MS port. noStat = the result when there is no voice-stat buffer:
 * -1 in 0x3264; 0x33b8 has a nop in that branch's delay slot and returns f_1b84's 1 (quirk) */
template <class F>
static int MsWalk(uint32_t ctx, uint32_t port, uint32_t id, int32_t n, int noStat, F put)
{
    uint32_t blk, b14;
    if (!PortLookup(ctx, port, &blk, &b14)) return -1;
    if (S().statBuf == 0) return noStat;
    if (Blk(blk)->flags & 1) return -1;
    id &= 0xff;
    int32_t lim = (int8_t)n, cnt = 0;
    for (uint32_t i = 0; i < 16; i++) {
        uint32_t ch = b14 + 4 + CH_SIZE * i;
        const uint32_t lists[3] = {ch - 8, ch, ch + 8};
        for (uint32_t p : lists)
            for (uint32_t va = Lw(p + 0xc); va != p; va = Lw(va + 0xc)) {
                Voice *v = V(va);
                if (v->id != id) continue;
                put(cnt, v);
                cnt++;
                if (!(cnt < lim)) return cnt;
            }
    }
    return cnt;
}

/* 0x3264, export 23 (SYTH RPC 0x11000, unreached): the u8 statbuf flag of each voice with the id */
int MSGetVoiceStateByID(uint32_t ctx, uint32_t port, uint32_t id, uint32_t buf, int32_t n)
{
    return MsWalk(ctx, port, id, n, -1, [buf](int32_t k, Voice *v) { W8(buf + (uint32_t)k, StatFlag(v->core, v->idx)); });
}

/* 0x33b8, export 24 (no importer): the u16 statbuf ENVX of each voice with the id; 1 (not -1) without a statbuf */
int MSGetVoiceEnvByID(uint32_t ctx, uint32_t port, uint32_t id, uint32_t buf, int32_t n)
{
    return MsWalk(ctx, port, id, n, 1,
                  [buf](int32_t k, Voice *v) { W16(buf + 2 * (uint32_t)k, StatEnv(v->core, v->idx)); });
}

}  // namespace hsyn
