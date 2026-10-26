/* hsyn_se - group H6 of NI6b: the SE-stream ("fe" port) voice engine of modhsyn.
 * SE note on (f_840c dispatch -> f_7954 HD walk of bank slot `bank`, or f_8004 the SETB chunk of slot 0, one entry per
 * key), the release walkers by id / mask / (bank, prog, key, id) (f_7468, f_75d4, f_718c), the port-cap steal of
 * channel 0 (f_784c), the SE twins of the playing / pending steals (f_84d8 + f_8700, f_875c; the bodies are the M3
 * natives StealPlayingKind / StealPendingKind), the exclusion-group limit (f_88ac with its one-voice kill f_70dc). The
 * thunks f_7338 (ReleaseVoice), f_782c (Kill), f_84b8 (AllocVoice(a0 & 0xff)) and the blk + 0x18 adaptors f_75b4 /
 * f_7730 are inlined.
 *
 * H6S (with H5R): the f_ac40 pan-segment command switch SeSegCmd, called by the fe
 * parser's voice select f_2208 (hsyn_fe.cpp).
 *
 * REV: no recording executes one instruction of this file (exec 0 in all nine iopcov runs); the only caller is H5R's
 * ParseSe (f_278c, hsyn_fe.cpp). Checked by hsyn::UnitTest (h6_tests, h5r_tests). Only channel 0 of the port is used (ch0 =
 * blk + 0x18); list walks are the original's (direction, the "continue at the saved next's prev" rule). No divide in
 * the H6 part; the switch's `break 0x1c00` guards follow the IOP divide-by-zero rule (iopdiv.h). Open: nothing in modhsyn sets
 * blk[2] bit 1 (the 0x608 voice flags), and blk[3] == 1 (f_8004) is never set in Versus. Function comments give the
 * module offset. */
#include "hsyn.h"
#include "hsyn_state.h"
#include "hsyn_sd.h"
#include "../../plat.h"
#include "iopdiv.h"

namespace hsyn {

static inline void SeQueueKoff(const Voice *v) { S().maskKoff[v->core] |= 1u << (v->idx & 31); }

/* ---------------------------------------------------------------- release walkers */

/* the release body of f_718c / f_7468 / f_75d4 for one matching voice of ch0 list 1: unlink, flags & 0xfcff (stored
 * before the test); a never-started voice is freed (f_7338 = ReleaseVoice), the others get both LFO releases, a queued
 * KOFF, the front of list 3 and stat 0x3p */
static void SeReleaseVoice(uint32_t va, uint32_t ch0)
{
    Voice *v = V(va);
    ListB_Unlink(va);
    const uint16_t fl = v->flags;
    v->flags = fl & 0xfcff;
    if (fl & 8) {
        ReleaseVoice(va);
        return;
    }
    LfoRelease(va + 0x78);
    LfoRelease(va + 0xa0);
    SeQueueKoff(v);
    ListB_PushFront(va, ch0 + CH_LIST3);
    if (S().statBuf) {
        uint8_t &f = StatFlag(v->core, v->idx);
        f = (uint8_t)((f & 0x8f) | 0x30);
    }
}

/* ch0 list 1 from the tail; a released voice continues at its old next's prev (lists 2 / 3 untouched, no portaState) */
template <class Match> static void SeReleaseWalk(uint32_t ch0, Match match)
{
    const uint32_t P = ch0 - 8;
    for (uint32_t va = Lw(P + 0xc); va != P; va = Lw(va + 0xc)) {
        if (!match(V(va))) continue;
        const uint32_t nx = V(va)->cnext;
        SeReleaseVoice(va, ch0);
        va = nx;
    }
}

/* 0x718c (f_278c: 0xa0 with velocity 0; port and the stack word 0x14 unused): id (or the wildcard id 0xff), b58 == the
 * full bank word, prog / key bytes */
void SeNoteOff(uint32_t blk, uint32_t bank, uint32_t prog, uint32_t key, uint32_t id)
{
    const uint8_t p8 = (uint8_t)prog, k8 = (uint8_t)key;
    SeReleaseWalk(blk + BLK_CH, [&](const Voice *v) {
        return (v->id == id || id == 0xff) && (uint32_t)v->b58 == bank && v->progIdx == p8 && v->key == k8;
    });
}

/* 0x75b4 -> 0x7468: v.id == id (full word) */
void SeReleaseById(uint32_t blk, uint32_t id)
{
    SeReleaseWalk(blk + BLK_CH, [&](const Voice *v) { return v->id == id; });
}

/* 0x7730 -> 0x75d4 (f_7730's id argument unused): (v.id & y) == (x & y) */
void SeReleaseMasked(uint32_t blk, uint32_t x, uint32_t y)
{
    const uint32_t xy = x & y;
    SeReleaseWalk(blk + BLK_CH, [&](const Voice *v) { return (v->id & y) == xy; });
}

/* ---------------------------------------------------------------- kills and steals */

/* 0x70dc: one voice of the group limit; a pending one is unlinked and freed, a sounding one gets the KOFF only when
 * flag == 1 (list 1), ADSR2 0xc021 at once and Kill (f_782c = f_6314, which unlinks it). Returns the old cnext. */
uint32_t SeKillOne(uint32_t va, uint32_t flag)
{
    Voice *v = V(va);
    const uint32_t nx = v->cnext;
    if (v->flags & 8) {
        ListB_Unlink(va);
        ReleaseVoice(va);
        return nx;
    }
    if (flag == 1) SeQueueKoff(v);
    SdSetParam(0x400u | v->core | (uint32_t)v->idx << 1, 0xc021);
    Kill(va);
    return nx;
}

/* 0x784c: port at its cap. ch0 list 3 from the head, then (only if none) list 1 from the head: a voice whose prio <=
 * the best so far (start = the new note's, compared as u16; later equals win) is the victim; KOFF (also for list 3),
 * ADSR2 0xc021, Kill. Not the 16-channel StealChannel (f_6458). */
int SeStealChannel(uint32_t blk14, uint32_t prio)
{
    const uint32_t ch0 = blk14 + 4;
    uint32_t best = prio, victim = 0;
    const uint32_t P[2] = {ch0 + 8, ch0 - 8};
    for (int l = 0; l < 2 && !victim; l++)
        for (uint32_t p = Lw(P[l] + 8); p != P[l]; p = Lw(p + 8)) {
            const uint32_t q = V(p)->prio;
            if (!((best & 0xffff) < q)) {
                victim = p;
                best = q;
            }
        }
    if (!victim) return 0;
    Voice *v = V(victim);
    SeQueueKoff(v);
    SdSetParam(0x400u | v->core | (uint32_t)v->idx << 1, 0xc021);
    Kill(victim);
    return 1;
}

/* 0x84d8: f_6c30's playing-list steal without its "MS only" rule; filt != 0 restricts the candidates to SE voices
 * (seOnly != 0) or to every other kind (seOnly == 0). The victim leaves both lists and is returned (not freed). */
uint32_t StealPlayingSe(uint32_t prio, uint32_t sel, uint32_t filt, uint32_t seOnly)
{
    return StealPlayingKind(prio, sel, filt == 0 ? STEAL_ANY : seOnly != 0 ? STEAL_SE_ONLY : STEAL_NOT_SE);
}

/* 0x8700: no SE cap -> any kind; under the cap only non-SE voices, at / over it only SE voices */
uint32_t SeStealPlaying(uint32_t prio, uint32_t sel)
{
    const Bss &g = S();
    uint32_t filt = 1, seOnly = 0;
    if (g.seMaxVoices == 0) filt = 0;
    else if (!(g.liveSe < g.seMaxVoices)) seOnly = 1;
    return StealPlayingSe(prio & 0xffff, sel & 0xff, filt, seOnly);
}

/* 0x875c: f_6e34 without the kind == 0 test */
uint32_t StealPendingSe(uint32_t prio, uint32_t sel) { return StealPendingKind(prio, sel, false); }

/* 0x88ac: count the group's voices in ch0 lists 3 and 1 (list 2 ignored); more than `limit` (signed compare of the
 * bytes) kills ALL of them, list 3 first (no KOFF), then list 1 (KOFF queued), each from the tail */
int SeGroupLimit(uint32_t ch0, uint32_t group, uint32_t limit)
{
    const uint32_t P[2] = {ch0 + 8, ch0 - 8};
    const uint32_t g8 = group & 0xff;
    int32_t n = 0;
    for (uint32_t pn : P)
        for (uint32_t p = Lw(pn + 0xc); p != pn; p = Lw(p + 0xc))
            if (V(p)->group == g8) n++;
    if (!((int32_t)(limit & 0xff) < n)) return 0;
    for (int l = 0; l < 2; l++)
        for (uint32_t p = Lw(P[l] + 0xc); p != P[l]; p = Lw(p + 0xc))
            if (V(p)->group == g8) p = SeKillOne(p, l == 1 ? 1u : 0u);
    return 1;
}

/* ---------------------------------------------------------------- note on */

/* the allocation ladder shared by f_7954 (normal branch) and f_8004: pool, reap once, SE-aware playing steal (f_8700),
 * pending steal of any kind (f_875c) */
static uint32_t SeAlloc(uint32_t pr, uint32_t sel)
{
    Bss &g = S();
    uint32_t va = AllocVoice(sel);
    if (!va && g.reapPending != 0) {
        g.reapPending = 0;
        if (Reap()) va = AllocVoice(sel);
    }
    if (!va) va = SeStealPlaying(pr & 0xffff, sel);
    if (!va) va = StealPendingSe(pr & 0xffff, sel);
    return va;
}

/* 0x7954 (blk[3] == 0): NoteOn's HD walk on bank slot `bank` (unchecked index: blk + 0x398 + 0x1c * bank) of channel 0.
 * Differences to NoteOn: ch0.prog = the program; -1 for no program / a zero-tone program / the port
 * cap without a steal (even after earlier voices started: no override reset then); the SE cap steals an SE voice
 * first (f_84d8 filter 1, 1; none = next sample); else the f_8700 / f_875c ladder; flags 0x608 when blk[2] & 2; the
 * header takes bank / prog / bend from the stream and copies ch0's seBend / seVol; kind 1. */
static int SeNoteOnSlot(uint32_t blk, uint32_t port, uint32_t bank, uint32_t prog, uint32_t key, uint32_t vel,
                        uint32_t pan, uint32_t bend, uint32_t id)
{
    Bss &g = S();
    const uint32_t blk14 = blk + BLK_VOL, ch0 = blk + BLK_CH;
    const uint8_t p8 = (uint8_t)prog, k8 = (uint8_t)key, v8 = (uint8_t)vel, pan8 = (uint8_t)pan;
    const uint16_t b16 = (uint16_t)bend;
    const uint32_t slot = blk14 + 0x384 + 0x1c * bank;
    const BankSlot *s = Slot(slot);
    const uint32_t progC = s->prog;
    uint32_t progp = 0;
    if (progC && !(R32(progC + 0xc) < p8)) {
        const uint32_t off = R32(progC + 0x10 + 4 * (uint32_t)p8);
        if (off != 0xffffffffu) {
            progp = progC + off;
            if (R8(progp + 4) == 0) progp = 0;
        }
    }
    if (!progp) return -1;
    Channel *c0 = Ch(ch0);
    int started = 0;
    c0->prog = progp;   /* ch0.slot is not written */
    int32_t n = R8(progp + 4);
    uint32_t tone = progp + R32(progp);
    const uint32_t ssetC = s->sset, smplC = s->smpl, vagiC = s->vagi;
    const uint8_t flag = (uint8_t)(port | 0x90);
    if (n > 0) {
        do {
            if ((int32_t)k8 < (int32_t)(R8(tone + 2) & 0x7f) || (int32_t)(R8(tone + 4) & 0x7f) < (int32_t)k8) continue;
            const uint32_t si = R16(tone);
            if (R32(ssetC + 0xc) < si) continue;
            const uint32_t so = R32(ssetC + 0x10 + 4 * si);
            if (so == 0xffffffffu) continue;
            const uint32_t sset = ssetC + so;
            if (v8 < R8(sset + 1) || R8(sset + 2) < v8) continue;
            if (R8(sset + 3) == 0) continue;
            for (int32_t k = 0; k < (int32_t)R8(sset + 3); k++) {
                const uint32_t mi = R16(sset + 4 + 2 * (uint32_t)k);
                if (R32(smplC + 0xc) < mi) continue;
                const uint32_t mo = R32(smplC + 0x10 + 4 * mi);
                if (mo == 0xffffffffu) continue;
                const uint32_t smpl = smplC + mo;
                if ((int32_t)v8 < (int32_t)(R8(smpl + 2) & 0x7f) || (int32_t)(R8(smpl + 4) & 0x7f) < (int32_t)v8) continue;
                const uint32_t vi = R16(smpl);
                uint32_t vagi = 0;
                if (!(R32(vagiC + 0xc) < vi)) {   /* an index past the end = a voice without sample */
                    const uint32_t vo = R32(vagiC + 0x10 + 4 * vi);
                    if (vo == 0xffffffffu) continue;
                    vagi = vagiC + vo;
                    if (R32(vagi) == 0xffffffffu) continue;
                }
                const uint8_t grp = R8(smpl + 0xe);
                if (grp) KillGroup(ch0, grp);
                const uint32_t pr = (uint32_t)R8(smpl + 0xf) + R8(blk);
                if (!(R8(blk14 + 2) < R8(blk + 1)))
                    if (!SeStealChannel(blk14, pr)) return -1;
                const uint8_t sel = R8(smpl + 0x29);
                uint32_t va;
                if (g.seMaxVoices != 0 && !(g.liveSe < g.seMaxVoices)) va = StealPlayingSe(pr, sel, 1, 1);
                else va = SeAlloc(pr, sel);
                if (!va) continue;
                Voice *v = V(va);
                v->flags = (R8(blk + 2) & 2) ? 0x608 : 0x208;
                v->blk14 = blk14;
                v->ch = ch0;
                v->prog = progp;
                v->smpl = smpl;
                v->vagi = vagi;
                v->tone = tone;
                v->env = 0;
                v->porta = 0;
                v->prio = (uint16_t)pr;
                v->progIdx = p8;
                v->key = k8;
                v->id = id;
                v->vel = v8;
                v->bdBase = s->bd;
                v->fixedPitch = b16;
                v->group = R8(smpl + 0xe);
                v->velCurve = R8(sset);
                v->seBend = c0->seBend;
                v->sePan = pan8;
                v->kind = 1;
                v->seVol = c0->seVol;
                v->b58 = (uint8_t)bank;
                v->blkKind = R8(blk + 3);
                Ramp1Init(va);
                Ramp2Init(va);
                SegInit(va);
                W8(blk14 + 2, (uint8_t)(R8(blk14 + 2) + 1));   /* not saturated */
                g.liveSe++;                                      /* not saturated */
                ListB_PushFront(va, ch0 + CH_LIST1);
                started = 1;
                if (!(c0->portaState & 0x80) && c0->portaTime != 0) Portamento(va);
                VoiceSetup(va);   /* the MS setup, not VoiceSetupSe */
                ListA_PushFront(va, PendingHead(v->core));
                if (g.statBuf) StatFlag(v->core, v->idx) = flag;
            }
        } while (tone += 0x14, --n > 0);
    }
    if (started) {
        c0->seBend = -1;
        c0->seVol = 0xff;
        c0->sePan = -1;
    }
    return 0;
}

/* 0x8004 (blk[3] == 1, never in Versus): slot 0's SETB chunk: bank entry (count test `count < bank` on the full word),
 * program record (`ent[0] < prog`), key range [R+5, R+6], entry = R + R[0] + ((key - lo) & 0xff) * R[4]. A bad vagi
 * offset / sample returns -1 (not "next"); then the group limit, the port cap, the ladder (-1 when it fails). The
 * voice's prog word is the entry itself, no tone / smpl, seBend / seVol unset, VoiceSetupSe; ch0 is not written. */
static int SeNoteOnSetb(uint32_t blk, uint32_t port, uint32_t bank, uint32_t prog, uint32_t key, uint32_t vel,
                        uint32_t pan, uint32_t bend, uint32_t id)
{
    Bss &g = S();
    const uint32_t blk14 = blk + BLK_VOL, ch0 = blk + BLK_CH, slot = blk + BLK_SLOT;   /* `beqz slot` is dead */
    const uint8_t p8 = (uint8_t)prog, k8 = (uint8_t)key, v8 = (uint8_t)vel, pan8 = (uint8_t)pan;
    const uint16_t b16 = (uint16_t)bend;
    const uint32_t setbC = Slot(slot)->setb, vagiC = Slot(slot)->vagi;
    if (!setbC || !vagiC) return -1;
    if (R32(setbC + 0xc) < bank) return -1;
    const uint32_t off = R32(setbC + 0x10 + 4 * bank);
    if (off == 0xffffffffu) return -1;
    const uint32_t ent = setbC + off;
    if (R32(ent) < p8) return -1;
    const uint32_t o2 = R32(ent + 4 + 4 * (uint32_t)p8);
    if (o2 == 0xffffffffu) return -1;
    const uint32_t rec = setbC + o2;
    const uint8_t lo = R8(rec + 5);
    if (k8 < lo || R8(rec + 6) < k8) return -1;
    const uint32_t e = rec + R32(rec) + (uint32_t)((int32_t)R8(rec + 4) * (int32_t)((key - lo) & 0xff));
    const uint32_t vi = R16(e);
    uint32_t vagi = 0;
    if (!(R32(vagiC + 0xc) < vi)) {
        const uint32_t vo = R32(vagiC + 0x10 + 4 * vi);
        if (vo == 0xffffffffu) return -1;
        vagi = vagiC + vo;
        if (R32(vagi) == 0xffffffffu) return -1;
    }
    if (R8(e + 7)) SeGroupLimit(ch0, R8(e + 7), R8(e + 8));
    const uint32_t pr = (uint32_t)R8(e + 9) + R8(blk);
    if (!(R8(blk14 + 2) < R8(blk + 1)))
        if (!SeStealChannel(blk14, pr)) return -1;
    const uint32_t va = SeAlloc(pr, R8(e + 0xd));
    if (!va) return -1;
    Voice *v = V(va);
    v->flags = (R8(blk + 2) & 2) ? 0x608 : 0x208;
    v->blk14 = blk14;
    v->ch = ch0;
    v->prog = e;
    v->tone = 0;
    v->smpl = 0;
    v->vagi = vagi;
    v->env = 0;
    v->porta = 0;
    v->prio = (uint16_t)pr;
    v->progIdx = p8;
    v->key = k8;
    v->id = id;
    v->vel = v8;
    v->fixedPitch = b16;
    v->bdBase = Slot(slot)->bd;
    v->group = R8(e + 7);
    v->seBend = -1;
    v->seVol = 0xff;
    v->sePan = pan8;
    v->kind = 1;
    v->velCurve = R8(e + 2);
    v->b58 = (uint8_t)bank;
    v->blkKind = R8(blk + 3);
    Ramp1Init(va);
    Ramp2Init(va);
    SegInit(va);
    W8(blk14 + 2, (uint8_t)(R8(blk14 + 2) + 1));   /* not saturated */
    g.liveSe++;                                      /* not saturated */
    ListB_PushFront(va, ch0 + CH_LIST1);
    VoiceSetupSe(va);
    ListA_PushFront(va, PendingHead(v->core));
    if (g.statBuf) StatFlag(v->core, v->idx) = (uint8_t)(port | 0x90);
    return 0;
}

/* 0x840c: blk[3] 0 -> the HD walk, 1 -> SETB, else -1 (no call). Narrows prog / key / vel to bytes, pan to s8, bend to
 * u16; bank, port and id are passed as they are. */
int SeNoteOn(uint32_t blk, uint32_t port, uint32_t bank, uint32_t prog, uint32_t key, uint32_t vel, int32_t pan,
             uint32_t bend, uint32_t id)
{
    const uint32_t p = prog & 0xff, k = key & 0xff, v = vel & 0xff, b = bend & 0xffff;
    const uint32_t pn = (uint32_t)(int32_t)(int8_t)pan;
    switch (R8(blk + 3)) {
    case 0: return SeNoteOnSlot(blk, port, bank, p, k, v, pn, b, id);
    case 1: return SeNoteOnSetb(blk, port, bank, p, k, v, pn, b, id);
    default: return -1;
    }
}

/* ---------------------------------------------------------------- f_ac40 switch */

/* D_0b8d0: the mode word per kind (bit 1 = negative direction, tested by kind 5); only 0 1 3 4 12 13 are stored */
static const uint32_t kSegMode[14] = {0x0001, 0x1002, 0x2004, 0x3001, 0x4002, 0x5004, 0x6000,
                                      0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xc001, 0xd002};

static inline int32_t SegNeg(int32_t x) { return (int32_t)(0u - (uint32_t)x); }

/* the switch's divides (`break 0x1c00` / `break 0x1800` guards): IOP divide-by-zero rule (iopdiv.h) */

/* 0xac40 (prelude) + 0xac5c .. 0xb0b0 (bodies; table D_0b610): a pan sweep of the voice's segment object v + 0xf0
 * from the current pan (the running segment's accumulator, else panLow + 0x40) towards c2. f_2208 passes c1 as a byte,
 * c2 is read with lbu from the stack (the stack word 0x14 is never read). Returns 0, -1 for kinds 6..11 and >= 14. */
int SeSegCmd(uint32_t va, uint32_t kind, uint32_t val, uint32_t c1, uint32_t c2)
{
    if (kind >= 14) return -1;                                   /* sltiu 0xe at 0xac50, before the divides */
    Voice *v = V(va);
    SeSeg &g = v->seg;
    const int32_t pl = (int8_t)v->panLow;
    int32_t base = (int32_t)((uint32_t)(pl + 0x40) << 12);       /* t6 */
    uint32_t cur = (uint32_t)v->panLow + 0x40;                   /* t2 (only its low byte is used) */
    if (g.state - 1u < 2u) {
        base = g.acc;
        cur = ((uint32_t)base + (base < 0 ? 0xfffu : 0u)) >> 12;  /* srl: logical after the round-to-zero bias */
    }
    uint32_t tgt = cur;                                          /* t4 */
    uint32_t ticks = 1;                                          /* t5 */
    int32_t rate = 1;                                            /* t1 */
    if (val != 0) {
        const uint32_t t = S().tickLen;
        ticks = IOP_DIVU(val << 8, t, 0xacbc, "hsyn SeSegCmd tickLen 0");          /* divu: t 0 -> 0xffffffff */
        rate = IOP_DIV(0xff000, ticks, 0xacd4, "hsyn SeSegCmd ticks 0");           /* div: ticks 0 -> -1 */
    }
    /* the shared byte distance of kinds 3 / 4 / 12 / 13: (c2 - cur) mod 0x100, both as unsigned bytes */
    int32_t curU = (int8_t)cur;
    if (curU < 0) curU += 0x100;
    int32_t tgtU = (int8_t)c2;
    if (tgtU < 0) tgtU += 0x100;
    int32_t fwd = tgtU - curU;
    if (fwd < 0) fwd += 0x100;
    switch (kind) {
    case 1:                                                      /* 0xad1c: negate, then kind 0's body */
        rate = SegNeg(rate);
        /* fall through */
    case 0:                                                      /* 0xad20 */
        g.rep = (int32_t)c1;
        g.flag = 0;
        break;
    case 2:                                                      /* 0xaf54: no sweep; target as a full int */
        g.rep = 1;
        g.flag = 0;
        g.target = pl + 0x40;
        return 0;
    case 3: {                                                    /* 0xad2c */
        g.rep = (int32_t)c1;
        tgt = c2;
        const int32_t d = fwd ? fwd : 0x100;
        g.flag = 1;
        g.reload = IOP_DIV(d << 12, rate, 0xad80, "hsyn SeSegCmd kind 3 rate 0");
        break;
    }
    case 4: {                                                    /* 0xadac: rate negated AFTER the divide */
        g.rep = (int32_t)c1;
        tgt = c2;
        const int32_t back = fwd ? 0x100 - fwd : 0;
        const int32_t q = IOP_DIV(back << 12, rate, 0xae08, "hsyn SeSegCmd kind 4 rate 0");
        rate = SegNeg(rate);
        g.flag = 1;
        g.reload = q;
        break;
    }
    case 5: {                                                    /* 0xaf74: retarget, no sweep start */
        int32_t a = pl + 0x40;
        if (a < 0) a = pl + 0x140;
        int32_t c = (int8_t)c2;
        if (c < 0) c += 0x100;
        int32_t d = c - a;
        if (d < 0) d += 0x100;
        else if (d == 0) d = 0x100;
        int32_t r = g.slope;
        if (g.w04 & 2) {
            d = 0x100 - d;
            if (d == 0) d = 0x100;
            r = SegNeg(r);
        }
        /* SegInit leaves slope 0: a first kind 5 divides by 0 (q -1 -> len -1, flag 0) */
        const int32_t q = IOP_DIV(d << 12, r, 0xafec, "hsyn SeSegCmd kind 5 slope 0");
        g.target = (int8_t)c2;
        if ((uint32_t)g.reload < (uint32_t)q) {
            g.len = q;
            g.flag = 0;
        } else {
            g.reload = q;
            g.flag = 1;
        }
        g.rep = 1;
        return 0;
    }
    case 12: {                                                   /* 0xae40: duration held, rate from the distance */
        g.rep = 1;
        tgt = c2;
        const int32_t d = fwd ? fwd : 0x100;
        rate = IOP_DIV(d << 12, ticks, 0xae98, "hsyn SeSegCmd kind 12 ticks 0");
        g.reload = (int32_t)ticks;
        g.flag = 0;
        break;
    }
    case 13: {                                                   /* 0xaec8: negate AFTER the divide */
        g.rep = 1;
        tgt = c2;
        int32_t back = 0x100 - fwd;
        if (back == 0) back = 0x100;
        rate = SegNeg(IOP_DIV(back << 12, ticks, 0xaf20, "hsyn SeSegCmd kind 13 ticks 0"));
        g.reload = (int32_t)ticks;
        g.flag = 0;
        break;
    }
    default:                                                     /* 6..11: f_ac5c, after the divides */
        return -1;
    }
    /* 0xb050: start the segment */
    g.state = 1;
    g.w04 = (int32_t)kSegMode[kind];
    g.base = base;
    g.target = (int8_t)tgt;
    g.slope = rate;
    g.len = (int32_t)ticks;
    g.pos = 0;
    g.panL = v->panGainL;
    g.panR = v->panGainR;
    g.w34 = (uint32_t)(int32_t)(int8_t)v->volPhase;
    return 0;
}

}  // namespace hsyn
