/* hsyn - see hsyn.h. Group H1 of NI6b: module start,
 * Init, channel / voice-pool init, SetReservVoice, the port lookup and the small exports, the ATick shell with the
 * per-port stream dispatch f_174c and the KON time guard f_5c30. Function comments give the original's module offset.
 * State layout, views and the list helpers: hsyn_state.h; sd calls: hsyn_sd.h; the per-tick sweeps, kill / steal and the
 * voice step objects (H2): hsyn_sweep.cpp; the fe SE-stream parser (H5R): hsyn_fe.cpp; unit-test hooks: hsyn_stub.cpp. */
#include "hsyn.h"
#include "hsyn_state.h"
#include "hsyn_sd.h"

namespace hsyn {

uint32_t g_base;
const SdMock *g_sdMock;
void StubReset();   /* hsyn_stub.cpp */
void ParseSeReset();   /* hsyn_fe.cpp: the ParseSeCalls counter */

/* ---------------------------------------------------------------- module start */

/* 0x0000: CpuSuspendIntr, RegisterLibraryEntries (vanishes: the exports are linked), CpuResumeIntr; returns
 * (result != 0) = 0. The phantom image is zeroed (the .bss); the one mutable .data word another cluster reads (the
 * f_9b80 LCG seed) is placed here, the read-only .data tables are constants in their clusters. */
int ModuleStart(const iopk::ModuleArgs *m)
{
    g_base = m->base;
    StubReset();
    ParseSeReset();
    W32(g_base + D_SEED, 0x02cee9d7u);
    int st;
    iopk::CpuSuspendIntr(&st);
    iopk::CpuResumeIntr(st);
    return 0;
}

uint32_t Base() { return g_base; }

/* ---------------------------------------------------------------- init */

/* 0x0770: three empty B lists, controller defaults (+0x34 untouched) */
void ChannelInit(uint32_t ch, uint32_t blk)
{
    Channel *c = Ch(ch);
    c->list1[1] = ch - 8;
    c->list1[0] = ch - 8;
    c->list3[1] = ch + 8;
    c->list3[0] = ch + 8;
    c->vol = 0x80;
    c->expr = 0x80;
    c->list2[1] = ch;
    c->list2[0] = ch;
    c->prog = 0;
    c->blk = blk;
    c->slot = 0;
    c->hold = 0;
    c->bend = 0;
    c->portaTime = 0;
    c->seBend = -1;
    c->bank = 0;
    c->cc2 = 0;
    c->seVol = 0xff;
    c->sePan = -1;
    c->cc1 = 0;
    c->pan = 0;
    c->portaState = 0xff;
    c->nrpnData = 0xff;
    c->nrpnLsb = 0xff;
    c->nrpnMsb = 0xff;
}

/* 0xa724 / 0xa89c / 0xabf4: the SE-stream sub-objects (also re-run at every note-on by M3 / M6) */
static void RampInit(SeRamp *r)
{
    r->state = 0;
    r->base = 0;
    r->out = 0;
    r->delta = 0;
    r->slope = 0;
    r->len = 0;
    r->count = 0;
}
int Ramp1Init(uint32_t v) { RampInit(&V(v)->ramp1); return 0; }
int Ramp2Init(uint32_t v) { RampInit(&V(v)->ramp2); return 0; }
int SegInit(uint32_t v)
{
    SeSeg *s = &V(v)->seg;
    s->w04 = -1;
    s->panL = 0x80;
    s->panR = 0x80;
    s->w34 = 1;
    s->state = 0;
    s->acc = s->base = s->target = s->slope = s->len = s->rep = s->flag = s->pos = s->reload = 0;
    return 0;
}

/* 0x3510: the four list heads (reserved, playing, pending, free), then every voice core-major pushed at the free
 * list's back (head -> tail = voices 0 .. 23), the counts and the per-core masks / VMIX shadows */
void PoolInit()
{
    Bss &g = S();
    for (uint32_t c = 0; c < 2; c++) ListA_Init(ReservedHead(c));
    for (uint32_t c = 0; c < 2; c++) ListA_Init(PlayingHead(c));
    for (uint32_t c = 0; c < 2; c++) ListA_Init(PendingHead(c));
    for (uint32_t c = 0; c < 2; c++) ListA_Init(FreeHead(c));
    for (uint32_t c = 0; c < 2; c++)
        for (uint32_t i = 0; i < 24; i++) {
            uint32_t va = VoiceAddr(c, i);
            Voice *v = V(va);
            v->seBend = -1;
            v->flags = 0;
            v->blk14 = v->ch = v->prog = v->tone = v->smpl = v->vagi = v->bdBase = 0;
            v->env = 0;
            v->porta = 0;
            v->prio = 0;
            v->core = (uint8_t)c;
            v->idx = (uint8_t)i;
            v->seVol = 0xff;
            v->sePan = 0x80;
            v->id = 0xff;
            v->kind = 0xff;
            v->b58 = 0xff;
            v->fixedPitch = 0;
            Ramp1Init(va);
            Ramp2Init(va);
            SegInit(va);
            ListA_PushBack(va, FreeHead(c));
        }
    g.freeCnt[1] = 0x18;
    g.freeCnt[0] = 0x18;
    g.maskKoff[1] = g.maskKoff[0] = 0;
    g.maskKon[1] = g.maskKon[0] = 0;
    g.maskC[1] = g.maskC[0] = 0;
    g.vmix[1].r = g.vmix[1].l = g.vmix[0].r = g.vmix[0].l = 0;
    g.vmixE[1].r = g.vmixE[1].l = g.vmixE[0].r = g.vmixE[0].l = 0;
}

/* 0x0050: B_0b920 / B_0b960 are written before the argument checks (quirk); an entry pair with a null stream or block
 * pointer is skipped (the stream pointer is only tested) */
int Init(uint32_t ctx, uint32_t usec)
{
    Bss &g = S();
    g.statBuf = 0;
    g.tickLen = (uint32_t)(((uint64_t)(uint32_t)(usec << 8) * 0x10624dd3u) >> 32) >> 6;
    if (ctx == 0 || R32(ctx + CTX_NGROUP) == 0) return -1;
    uint32_t grp = R32(ctx + CTX_GROUP);
    if (grp == 0) return -1;
    uint32_t n = R32(grp + GRP_N), e = R32(grp + GRP_ENTRIES);
    if (n & 1) return -1;
    if (e == 0) return -1;
    if (1 < (int32_t)n) {
        for (int32_t k = 1;; k += 2, e += 2 * ENTRY_SIZE) {
            uint32_t blk;
            if (R32(e + ENTRY_PTR) != 0 && (blk = R32(e + ENTRY_SIZE + ENTRY_PTR)) != 0) {
                PortBlock *b = Blk(blk);
                b->maxVoices = 0x30;
                b->prio = 0;
                b->flags = 0;
                b->singleBank = 0;
                b->w4[0] = b->w4[1] = b->w4[2] = b->w4[3] = 0;
                b->vol = 0x100;
                b->live = 0;
                for (int s = 0; s < 16; s++) {
                    BankSlot *sl = &b->slot[s];
                    sl->head = sl->prog = sl->sset = sl->smpl = sl->vagi = sl->setb = sl->bd = 0;
                }
                for (uint32_t i = 0; i < 16; i++) ChannelInit(ChannelAddr(blk, i), blk);
            }
            if (!(k + 2 < (int32_t)R32(grp + GRP_N))) break;   /* the count is re-read per pair */
        }
    }
    PoolInit();
    g.konIdx = 0xff;
    g.reserve[1] = 0;
    g.reserve[0] = 0;
    g.mono = 0;
    g.reapPending = 1;
    g.seMaxVoices = 0;
    g.liveSe = 0;
    g.liveMs = 0;
    return 0;
}

/* ---------------------------------------------------------------- reserve mask */

/* 0x1a08: per core (core 0 first): store the mask; every reserved voice back to the free list's TAIL (reserved-list
 * order: these are the next ones allocated); then each FREE voice whose bit is set moves to the reserved list's front.
 * Pending / playing voices are not touched even when their bit is set (quirk). No sd. */
int SetReservVoice(const uint32_t mask[2])
{
    Bss &g = S();
    for (uint32_t c = 0; c < 2; c++) {
        uint32_t m = mask[c];
        uint32_t F = FreeHead(c), R = ReservedHead(c);
        g.reserve[c] = m;
        uint32_t v;
        while ((v = Lw(R)) != R) {
            ListA_Unlink(v);
            ListA_PushBack(v, F);
            g.freeCnt[c]++;
        }
        for (v = Lw(F); v != F; v = Lw(v)) {
            if (m & (1u << (V(v)->idx & 31))) {
                uint32_t prev = Lw(v + 4);
                ListA_Unlink(v);
                ListA_PushFront(v, R);
                g.freeCnt[c]--;
                v = prev;   /* continue after the removed node */
            }
        }
    }
    return 0;
}

/* 0x1b5c, export 26 (unreached) */
int GetReservVoice(uint32_t out[2])
{
    out[0] = S().reserve[0];
    out[1] = S().reserve[1];
    return 0;
}

/* ---------------------------------------------------------------- port exports */

/* 0x1b84: no test of the group or block pointer (quirk); the compare is unsigned */
int PortLookup(uint32_t ctx, uint32_t port, uint32_t *blk, uint32_t *blk14)
{
    if (ctx == 0) return 0;
    uint32_t g = R32(ctx + CTX_GROUP);
    uint32_t k = port * 2 + 1;
    if (!(k < R32(g + GRP_N))) return 0;
    uint32_t b = R32(k * ENTRY_SIZE + R32(g + GRP_ENTRIES) + ENTRY_PTR);
    *blk = b;
    *blk14 = b + BLK_VOL;
    return 1;
}

/* 0x1bd4 */
int SetVolume(uint32_t ctx, uint32_t port, uint32_t vol)
{
    uint32_t blk, b14;
    if (!PortLookup(ctx, port, &blk, &b14)) return -1;
    W16(b14, (uint16_t)vol);
    return 0;
}

/* 0x1c18, export 11 (unreached): 0 for a bad port, not -1 (quirk) */
int GetVolume(uint32_t ctx, uint32_t port)
{
    uint32_t blk, b14;
    if (!PortLookup(ctx, port, &blk, &b14)) return 0;
    return R16(b14);
}

/* 0x1c50 / 0x1cb4 / 0x1d18: one call per channel (blk14 + 4 + 0x38 i) */
int AllNoteOff(uint32_t ctx, uint32_t port)
{
    uint32_t blk, b14;
    if (!PortLookup(ctx, port, &blk, &b14)) return -1;
    for (uint32_t i = 0; i < 16; i++) AllNotesOff(b14 + 4 + CH_SIZE * i);
    return 0;
}

int AllSoundOff(uint32_t ctx, uint32_t port)   /* export 13 (unreached) */
{
    uint32_t blk, b14;
    if (!PortLookup(ctx, port, &blk, &b14)) return -1;
    for (uint32_t i = 0; i < 16; i++) AllSoundOffCh(b14 + 4 + CH_SIZE * i);
    return 0;
}

int ResetAllControler(uint32_t ctx, uint32_t port)
{
    uint32_t blk, b14;
    if (!PortLookup(ctx, port, &blk, &b14)) return -1;
    for (uint32_t i = 0; i < 16; i++) ResetControllers(b14 + 4 + CH_SIZE * i);
    return 0;
}

/* 0x1e60 */
int SetVoiceStatBuffer(uint32_t buf)
{
    S().statBuf = buf;
    return 0;
}

/* 0x0658, export 28 (unreached): zero one bank slot (slot 0 if singleBank == 1) and re-init the channels that use it */
int ResetBankSlot(uint32_t ctx, uint32_t port, uint32_t slot)
{
    if (ctx == 0 || !(slot < 0x10)) return -1;
    uint32_t g = R32(ctx + CTX_GROUP);
    uint32_t k = port * 2 + 1;
    if (!(k < R32(g + GRP_N))) return -1;
    uint32_t blk = R32(k * ENTRY_SIZE + R32(g + GRP_ENTRIES) + ENTRY_PTR);
    if (blk == 0) return -1;
    if (Blk(blk)->singleBank == 1) slot = 0;
    uint32_t sa = SlotAddr(blk, slot);
    BankSlot *sl = Slot(sa);
    sl->head = sl->prog = sl->sset = sl->smpl = sl->vagi = sl->setb = sl->bd = 0;
    for (uint32_t i = 0; i < 16; i++) {
        uint32_t ch = ChannelAddr(blk, i);
        if (Ch(ch)->slot == sa) ChannelInit(ch, blk);
    }
    return 0;
}

/* ---------------------------------------------------------------- the tick */

/* 0x5c30: the only effect of the result is ATick's reset of B_0b991 (no other reader): unobservable, kept for the
 * GetSystemTime / SysClock2USec call count. The "usec + 1000000" branch is right only for a one-second gap (quirk). */
int KonGuard()
{
    iopk::SysClock clk;
    uint32_t sec, usec;
    iopk::GetSystemTime(&clk);
    iopk::SysClock2USec(&clk, &sec, &usec);
    Bss &g = S();
    int32_t diff;
    if (sec == g.konSec) diff = (int32_t)(usec - g.konUs);
    else if (sec - 1 == g.konSec) return 1;
    else diff = (int32_t)(usec + 1000000u - g.konUs);
    return !(diff < 21);
}

/* 0x174c: ports in ascending order; a buffer's "used" word is cleared BEFORE its parser runs; the cap word is not
 * consulted and "used" is not range-checked */
void StreamDispatch(uint32_t entries, int32_t count)
{
    if (!(1 < count)) return;
    uint32_t e = entries;
    for (int32_t k = 1, port = 0;; k += 2, e += 2 * ENTRY_SIZE, port++) {
        uint32_t buf = R32(e + ENTRY_PTR), blk = R32(e + ENTRY_SIZE + ENTRY_PTR);
        if (buf != 0 && blk != 0) {
            uint32_t p = buf + SBUF_DATA, end = buf + R32(buf + SBUF_USED) + SBUF_DATA;
            if (p < end) {
                W32(buf + SBUF_USED, 0);
                if (!(Blk(blk)->flags & 1)) ParseMs(blk, (uint32_t)port, p, end);
                else ParseSe(blk, (uint32_t)port, p, end);
            }
        }
        if (!(k + 2 < count)) break;
    }
}

/* 0x183c, export 5. sd order of one tick: [GetSwitch VMIXL / VMIXR / VMIXEL / VMIXER core 0, only if core 0 has a
 * reserve mask] [the same for core 1] -> the stream parse (note on / off, controllers) -> f_37c4 (KOFF c0, KOFF c1,
 * per core: ENVX reads + voice starts, KON) -> f_4de0 (per core: ENDX, ENVX, pitch / volume writes). The VMIX shadow
 * of a core is cleared under its reserve mask and refilled from the hardware right before the next core's. */
int ATick(uint32_t ctx)
{
    Bss &g = S();
    if (g.konIdx != 0xff) {
        if (KonGuard()) g.konIdx = 0xff;
    }
    for (uint32_t c = 0; c < 2; c++) {
        uint32_t nr = ~g.reserve[c];
        g.vmix[c].l &= nr;
        g.vmix[c].r &= nr;
        g.vmixE[c].l &= nr;
        g.vmixE[c].r &= nr;
        uint32_t r = g.reserve[c];
        if (r != 0) {
            uint32_t s = SdGetSwitch(c | 0x1800);
            g.vmix[c].l |= s & r;
            s = SdGetSwitch(c | 0x1a00);
            g.vmix[c].r |= s & r;
            s = SdGetSwitch(c | 0x1900);
            g.vmixE[c].l |= s & r;
            s = SdGetSwitch(c | 0x1b00);
            g.vmixE[c].r |= s & r;
        }
    }
    uint32_t grp = R32(ctx + CTX_GROUP);
    StreamDispatch(R32(grp + GRP_ENTRIES), (int32_t)R32(grp + GRP_N));
    uint32_t pend = PendingSweep();
    uint32_t work = PlayingSweep();
    if (g.statBuf) {
        Stat(g.statBuf)->pend = pend;
        Stat(g.statBuf)->work = work;
    }
    return 0;
}

/* ---------------------------------------------------------------- test view */

void GetView(View *v)
{
    Bss &g = S();
    v->freeCnt[0] = g.freeCnt[0];
    v->freeCnt[1] = g.freeCnt[1];
    v->konIdx = g.konIdx;
    v->reserve[0] = g.reserve[0];
    v->reserve[1] = g.reserve[1];
    v->statBuf = g.statBuf;
    v->tickLen = g.tickLen;
    v->reapPending = g.reapPending;
    for (int c = 0; c < 2; c++) {
        v->vmix[c][0] = g.vmix[c].l;
        v->vmix[c][1] = g.vmix[c].r;
        v->vmix[c][2] = g.vmixE[c].l;
        v->vmix[c][3] = g.vmixE[c].r;
    }
}

int ListOrder(int list, int core, uint8_t *idx, int max)
{
    uint32_t c = (uint32_t)core & 1;
    uint32_t h = list == 0 ? FreeHead(c) : list == 1 ? ReservedHead(c) : list == 2 ? PendingHead(c) : PlayingHead(c);
    int n = 0;
    uint32_t prev = h;
    for (uint32_t v = Lw(h); v != h; v = Lw(v)) {
        uint32_t off = v - (g_base + B_VOICES);
        if (n >= 48 || off >= 2 * CORE_STRIDE || off % VOICE_SIZE || Lw(v + 4) != prev || V(v)->core != c) return -1;
        if (n < max) idx[n] = V(v)->idx;
        n++;
        prev = v;
    }
    return Lw(h + 4) == prev ? n : -1;
}

}  // namespace hsyn
