/* hsyn_note - group H3a of NI6b: note on
 * (f_65dc: the HD tone / sample walk, allocation with its exhaustion ladder, the voice header), the voice setup f_3bb4
 * (the M3 / M4 call boundary, in asm order), the allocator f_6b8c, the playing-list steal f_6c30, note off f_5cd4, the
 * release of a never-started voice f_5e80, the hold pedal f_5fb4, all notes off f_60f4, the velocity curve f_9064, the
 * LFO setup f_9434, LfoStart f_93ec and LfoRelease f_9778, plus M4's two pure clamps f_3988 / f_39cc (shared with M4's
 * f_52d0). H3b (REV, unit tests only): the pending-list steal f_6e34, the exclusion-group kill f_6f94, portamento
 * f_5248, the LFO setup tail 0x95b4 and the SE twin f_9830 / f_9990, the six LFO shapes and the LCG f_9b80.
 *
 * List walks are the original's (direction, re-reads, the "continue at next's prev" rule of f_5cd4). Function comments
 * give the module offset. Unreached in every recording: the Vagi-index-past-the-end voice, the exclusion group, the
 * port cap (f_6458), the pending-list steal (f_6e34), portamento, the pedal-down paths, LFO setups past the mode test,
 * every LfoRelease past its enable test, f_9064's custom curve table. */
#include "hsyn.h"
#include "hsyn_state.h"
#include "hsyn_sd.h"
#include "iopdiv.h"

namespace hsyn {

/* D_0b6c0: the core-selector bit of each core (f_6c30 / f_6e34) */
static const uint8_t kCoreSel[2] = {0x10, 0x20};

/* divu with its `break 7` guard: IOP divide-by-zero rule (iopdiv.h: LO 0xffffffff, one plat_log per site, the asm goes
 * on; unreachable in Versus: tick = B_0b920 != 0; a zero LFO frequency would hit it, see quirks) */

/* ---------------------------------------------------------------- M4's clamps */

/* 0x3988: s16 clamp of a1 + a2 + (s16)a0 */
int16_t ClampAdd16(int32_t a0, int32_t a1, int32_t a2)
{
    int32_t s = (int32_t)((uint32_t)a1 + (uint32_t)a2 + (uint32_t)(int32_t)(int16_t)a0);
    if (0x7fff < s) s = 0x7fff;
    else if (s < -0x8000) s = -0x8000;
    return (int16_t)s;
}
/* 0x39cc: s8 clamp of a1 + a2 + (s8)a0 */
int8_t ClampAdd8(int32_t a0, int32_t a1, int32_t a2)
{
    int32_t s = (int32_t)((uint32_t)a1 + (uint32_t)a2 + (uint32_t)(int32_t)(int8_t)a0);
    if (s >= 0x80) s = 0x7f;
    else if (s < -0x80) s = -0x80;
    return (int8_t)s;
}

/* ---------------------------------------------------------------- allocation */

/* 0x6b8c: selector & 0x30 == 0x10 / 0x20 forces core 0 / 1, anything else takes core 1 iff free0 < free1 (tie -> core
 * 0). No fall-back to the other core. Pops the free list's TAIL; nothing in the voice is touched. */
uint32_t AllocVoice(uint32_t sel)
{
    Bss &g = S();
    uint32_t t = sel & 0x30, c;
    if (t == 0x10) c = 0;
    else if (t == 0x20) c = 1;
    else c = g.freeCnt[0] < g.freeCnt[1] ? 1 : 0;
    uint8_t n = g.freeCnt[c];
    if (n == 0) return 0;
    g.freeCnt[c] = (uint8_t)(n - 1);
    uint32_t va = g.free[c].last;
    ListA_Unlink(va);
    return va;
}

/* 0x6c30: steal from the playing lists (pool exhausted). Core 1 before core 0 (if the selector, 0x30 when its core
 * bits are 0, has the core's bit), each list tail -> head, MS voices only. Victim order: flags class (& 0x300) lower
 * first; inside the class lower priority, then a smaller ENVX snapshot (v[0x12]; first found wins ties). The best
 * priority starts as the NEW note's, so a voice of higher priority is never taken. Queues the KOFF only: no ADSR2
 * write, no free-list push, the free count and the stat flag stay (the caller pushes the voice to pending). */
uint32_t StealPlaying(uint32_t prio, uint32_t sel) { return StealPlayingKind(prio, sel, STEAL_MS_ONLY); }

/* the body of f_6c30, shared with M6's twin f_84d8 (hsyn_se.cpp StealPlayingSe): the two differ only in the kind test
 * */
uint32_t StealPlayingKind(uint32_t prio, uint32_t sel, int filter)
{
    Bss &g = S();
    uint32_t mask = (sel & 0x30) ? sel : 0x30;
    uint32_t newPrio = prio & 0xffff, best = prio, cls = 0x300, env = 0xffff, victim = 0;
    for (int c = 1; c >= 0; c--) {
        if (!(mask & kCoreSel[c])) continue;
        uint32_t h = PlayingHead((uint32_t)c);
        for (uint32_t p = Lw(h + 4); p != h; p = Lw(p + 4)) {
            Voice *pv = V(p);
            uint32_t pc = pv->flags & 0x300u;
            if (filter == STEAL_MS_ONLY ? pv->kind != 0
                : filter == STEAL_SE_ONLY ? pv->kind != 1
                : filter == STEAL_NOT_SE ? pv->kind == 1 : false)
                continue;
            if (pc < (cls & 0xffff)) {
                uint32_t q = pv->prio;
                if (newPrio < q) continue;
                best = q;
                cls = pc;
                env = pv->env;
                victim = p;
            } else if (pc == (cls & 0xffff)) {
                uint32_t q = pv->prio, b = best & 0xffff;
                if (q == b) {
                    if (pv->env < env) {
                        env = pv->env;
                        victim = p;
                    }
                } else if (q < b) {
                    best = q;
                    env = pv->env;
                    victim = p;
                }
            }
        }
    }
    if (!victim) return 0;
    Voice *v = V(victim);
    g.maskKoff[v->core] |= 1u << (v->idx & 31);
    uint32_t b14 = v->blk14;
    uint8_t live = R8(b14 + 2);
    if (live != 0) W8(b14 + 2, (uint8_t)(live - 1));
    if (v->kind == 0) {
        if (g.liveMs != 0) g.liveMs--;
    } else if (v->kind == 1) {
        if (g.liveSe != 0) g.liveSe--;
    }
    ListB_Unlink(victim);
    ListA_Unlink(victim);
    v->flags = 0;
    v->id = 0xff;
    v->kind = 0xff;
    v->b58 = 0xff;
    return victim;
}

/* ---------------------------------------------------------------- note on */

/* 0x65dc: every (tone, sample) pair whose ranges match starts a voice. Key range at the tone (+2 lo / +4 hi), velocity
 * ranges at the Sset (+1 / +2, unmasked) and the Smpl (+2 / +4); bit 7 of the tone / Smpl bounds is a cross-fade flag,
 * masked off here (M4's f_4c88 reads it). The port is used only for the stat flag byte (0x90 | port). The one-shot SE
 * overrides ch[0x28] / [0x2f] / [0x30] are copied into every started voice and reset once, iff a voice started; no
 * program = drop, and a port at its cap with nothing to steal returns at once (no reset, later tones not tried).
 * The voice header is stored before the sub-object inits, list 1 push, portamento, VoiceSetup and the pending push. */
void NoteOn(uint32_t blk, uint32_t cha, uint32_t port, uint32_t key, uint32_t vel, uint32_t id)
{
    Bss &g = S();
    Channel *ch = Ch(cha);
    const uint32_t blk14 = blk + BLK_VOL;
    const uint8_t k8 = (uint8_t)key, v8 = (uint8_t)vel;
    const uint32_t prog = ch->prog, slot = ch->slot;
    if (prog == 0) return;
    int started = 0;
    int32_t n = R8(prog + 4);
    uint32_t tone = prog + R32(prog);
    const BankSlot *s = Slot(slot);
    const uint32_t ssetC = s->sset, smplC = s->smpl, vagiC = s->vagi;
    const uint8_t flag = (uint8_t)(port | 0x90);
    if (n > 0) {
        do {
            if ((int32_t)k8 < (int32_t)(R8(tone + 2) & 0x7f) || (int32_t)(R8(tone + 4) & 0x7f) < (int32_t)k8) continue;
            uint32_t si = R16(tone);
            if (R32(ssetC + 0xc) < si) continue;
            uint32_t so = R32(ssetC + 0x10 + 4 * si);
            if (so == 0xffffffffu) continue;
            const uint32_t sset = ssetC + so;
            if (v8 < R8(sset + 1) || R8(sset + 2) < v8) continue;
            if (R8(sset + 3) == 0) continue;
            for (int32_t k = 0; k < (int32_t)R8(sset + 3); k++) {
                uint32_t mi = R16(sset + 4 + 2 * (uint32_t)k);
                if (R32(smplC + 0xc) < mi) continue;
                uint32_t mo = R32(smplC + 0x10 + 4 * mi);
                if (mo == 0xffffffffu) continue;
                const uint32_t smpl = smplC + mo;
                if ((int32_t)v8 < (int32_t)(R8(smpl + 2) & 0x7f) || (int32_t)(R8(smpl + 4) & 0x7f) < (int32_t)v8) continue;
                uint32_t vi = R16(smpl), vagi = 0;
                if (!(R32(vagiC + 0xc) < vi)) {   /* an index past the end = a voice without sample (noise; REV) */
                    uint32_t vo = R32(vagiC + 0x10 + 4 * vi);
                    if (vo == 0xffffffffu) continue;
                    vagi = vagiC + vo;
                    if (R32(vagi) == 0xffffffffu) continue;
                }
                uint8_t grp = R8(smpl + 0xe);
                if (grp) KillGroup(cha, grp);
                uint32_t pr = (uint32_t)R8(smpl + 0xf) + R8(blk);
                if (!(R8(blk14 + 2) < R8(blk + 1)))
                    if (!StealChannel(blk14, pr)) return;
                uint32_t va = AllocVoice(R8(smpl + 0x29));
                if (!va) {
                    if (g.reapPending != 0) {
                        g.reapPending = 0;
                        if (Reap()) va = AllocVoice(R8(smpl + 0x29));
                    }
                    if (!va) va = StealPlaying(pr & 0xffff, R8(smpl + 0x29));
                    if (!va) va = StealPending(pr & 0xffff, R8(smpl + 0x29));
                    if (!va) continue;
                }
                Voice *v = V(va);
                v->flags = 0x208;
                v->blk14 = blk14;
                v->ch = cha;
                v->prog = prog;
                v->smpl = smpl;
                v->vagi = vagi;
                v->tone = tone;
                v->env = 0;
                v->porta = 0;
                v->prio = (uint16_t)pr;
                v->key = k8;
                v->id = id;
                v->fixedPitch = 0;
                v->bdBase = Slot(slot)->bd;
                v->vel = v8;
                v->group = R8(smpl + 0xe);
                v->velCurve = R8(sset);
                v->seBend = ch->seBend;
                v->seVol = ch->seVol;
                v->kind = 0;
                v->sePan = (uint8_t)ch->sePan;
                v->blkKind = R8(blk + 3);
                Ramp1Init(va);
                Ramp2Init(va);
                SegInit(va);
                W8(blk14 + 2, (uint8_t)(R8(blk14 + 2) + 1));   /* not saturated */
                g.liveMs++;
                ListB_PushFront(va, cha + CH_LIST1);
                started = 1;
                if (!(ch->portaState & 0x80) && ch->portaTime != 0) Portamento(va);
                VoiceSetup(va);
                ListA_PushFront(va, PendingHead(v->core));
                if (g.statBuf) StatFlag(v->core, v->idx) = flag;
            }
        } while (tone += 0x14, --n > 0);
    }
    if (started) {
        ch->seBend = -1;
        ch->seVol = 0xff;
        ch->sePan = -1;
    }
}

/* 0x3bb4: LFO setups, depth / tremolo / vibrato seeds, velocity curve and volume term, pan, expression, pitch seeds.
 * Key-follow terms are (key - centre) * depth / 12. v[0x4e] and v[0x54] are stored before
 * the calls that read them. The volume product wraps at 32 bits (left to right). */
void VoiceSetup(uint32_t va)
{
    Bss &g = S();
    Voice *v = V(va);
    const uint32_t cha = v->ch, prog = v->prog, smpl = v->smpl, tone = v->tone;
    Channel *ch = Ch(cha);
    const uint32_t blk = ch->blk;
    LfoSetup(va + 0x78, prog, R32(blk + 8), (int32_t)R32(blk + 4), R8(smpl + 0x28) & 7, R16(smpl + 0x20), R16(smpl + 0x22),
             g.tickLen, 0);
    int32_t s3 = (int32_t)(((uint32_t)v->key - R8(tone + 0xb)) * (uint32_t)(int32_t)(int8_t)R8(tone + 0xa)) / 12;
    int32_t s2 = VelDepth(va, (int8_t)R8(smpl + 5), R8(smpl + 6), R8(smpl + 7));
    v->vibBase[0] = ClampAdd16((int16_t)R16(prog + 0x18), s3, s2);
    v->vibBase[1] = ClampAdd16((int16_t)R16(prog + 0x1a), s3, s2);
    LfoSetup(va + 0xa0, prog, R32(blk + 8), (int32_t)R32(blk + 4), R8(smpl + 0x28) & 0x70, R16(smpl + 0x24),
             R16(smpl + 0x26), g.tickLen, 1);
    s3 = (int32_t)(((uint32_t)v->key - R8(tone + 0xd)) * (uint32_t)(int32_t)(int8_t)R8(tone + 0xc)) / 12;
    s2 = VelDepth(va, (int8_t)R8(smpl + 8), R8(smpl + 9), R8(smpl + 0xa));
    v->tremBase[0] = ClampAdd8((int8_t)R8(prog + 0x20), s3, s2);
    v->tremBase[1] = ClampAdd8((int8_t)R8(prog + 0x21), s3, s2);
    SetTremoloDepth(va, ch->cc2);
    SetVibratoDepth(va, ch->cc1);
    uint32_t c = VelCurve(v->vel, v->velCurve, R32(blk + 0x10), (int32_t)R32(blk + 0xc)) & 0xff;
    int32_t t = (int32_t)((uint32_t)R8(prog + 6) * R8(tone + 0x10) * R8(smpl + 0x10) * c);
    if (t < 0) t += 0x3fff;
    int32_t s0 = t >> 14;
    uint32_t r = CrossFade(va);
    v->volTerm = (uint16_t)(((uint32_t)s0 * r) >> 16);
    uint32_t key = v->key;
    uint32_t kf = (key - R8(tone + 0xf)) * (uint32_t)(int32_t)(int8_t)R8(tone + 0xe) +
                  (key - R8(prog + 0xb)) * (uint32_t)(int32_t)(int8_t)R8(prog + 0xa);
    v->panMode = R8(prog + 0xc) & 1;
    PanBase(va, (int32_t)kf / 12);
    int32_t pan;
    if (v->sePan & 0x80) pan = ch->pan;   /* override unset (0xff) or >= 0x80: the channel's CC 10 */
    else {
        int32_t a = (int32_t)v->sePan - 0x40;
        if ((int8_t)a < -0x3f) a = -0x3f;
        pan = (int8_t)a;
    }
    PanLaw(va, pan);
    v->expr = v->seVol == 0xff ? ch->expr : v->seVol;
    UpdateVolume(va);
    v->lastVolR = 0xffff;
    v->lastVolL = 0xffff;
    if (v->vagi != 0) {
        v->pitchCache = 0x7fffffff;
        v->rateRatio = (uint32_t)(((uint64_t)((uint32_t)R16(v->vagi + 4) << 16) * 0x057619f1u) >> 32) >> 10;
        int32_t b;
        if (v->seBend & 0x8000) b = ch->bend;   /* no SE bend override: the channel's MIDI bend */
        else b = (int16_t)(uint16_t)((uint16_t)v->seBend - 0x2000);
        SetBend(va, b);
    }
}

/* ---------------------------------------------------------------- note off, release, pedal */

/* 0x5e80: free a voice that never sounded (the caller already left the channel list): port live count and the live
 * MS / SE counter (saturating), off the pending list, free count + 1, FREE list front, stat flag bits 4-6 cleared.
 * Unlike Kill (f_6314) the three sub-object states are not cleared. */
void ReleaseVoice(uint32_t va)
{
    Bss &g = S();
    Voice *v = V(va);
    uint32_t b14 = v->blk14;
    v->flags = 0;
    uint8_t live = R8(b14 + 2);
    if (live != 0) W8(b14 + 2, (uint8_t)(live - 1));
    if (v->kind == 0) {
        if (g.liveMs != 0) g.liveMs--;
    } else if (v->kind == 1) {
        if (g.liveSe != 0) g.liveSe--;
    }
    ListA_Unlink(va);
    g.freeCnt[v->core]++;
    ListA_PushFront(va, FreeHead(v->core));
    if (g.statBuf) {
        uint8_t &f = StatFlag(v->core, v->idx);
        f &= 0x8f;
    }
    v->id = 0xff;
    v->kind = 0xff;
    v->b58 = 0xff;
}

/* the released-voice tail shared by f_5cd4 / f_5fb4 / f_60f4: KOFF queued, front of list 3 */
static inline void QueueKoff(const Voice *v) { S().maskKoff[v->core] |= 1u << (v->idx & 31); }
static inline void StatReleased(const Voice *v)
{
    if (S().statBuf) {
        uint8_t &f = StatFlag(v->core, v->idx);
        f = (uint8_t)((f & 0x8f) | 0x30);
    }
}

/* 0x5cd4: list 1 only, tail -> head; every voice of the key (id match or id 0xff) is released: under the hold pedal to
 * the front of list 2 (flag 0x100), a never-started voice freed at once (no KOFF), else LFO release, KOFF, front of
 * list 3, stat 0x3p. The walk goes on at the unlinked voice's old next node's prev word. */
void NoteOff(uint32_t cha, uint32_t key, uint32_t id)
{
    Channel *ch = Ch(cha);
    if (ch->portaState < 0x81) ch->portaState = (uint8_t)key;
    const uint32_t P = cha - 8;
    uint32_t va = Lw(P + 0xc);
    while (va != P) {
        Voice *v = V(va);
        if (v->key == (key & 0xff) && (v->id == id || id == 0xff)) {
            uint32_t nx = v->cnext;
            ListB_Unlink(va);
            uint16_t fl = v->flags;
            v->flags = fl & 0xfcff;
            if (ch->hold != 0) {
                v->flags = (uint16_t)((fl & 0xfcff) | 0x100);
                ListB_PushFront(va, cha + CH_LIST2);
            } else if (fl & 8) {
                ReleaseVoice(va);
            } else {
                LfoRelease(va + 0x78);
                LfoRelease(va + 0xa0);
                QueueKoff(v);
                ListB_PushFront(va, cha + CH_LIST3);
                StatReleased(v);
            }
            va = nx;
        }
        va = Lw(va + 0xc);
    }
}

/* 0x5fb4: pedal down stores the raw value; pedal up (and it was down) moves list 2 to list 3 HEAD first (newest
 * first), freeing never-started voices; LFO release + KOFF for the others, 0x100 cleared after the push. */
void HoldPedal(uint32_t cha, uint32_t val)
{
    Channel *ch = Ch(cha);
    if (val & 0xff) {
        ch->hold = (uint8_t)val;
        return;
    }
    if (ch->hold == 0) return;
    ch->hold = (uint8_t)val;
    uint32_t va;
    while ((va = Lw(cha + CH_LIST2)) != cha) {
        Voice *v = V(va);
        ListB_Unlink(va);
        if (v->flags & 8) {
            ReleaseVoice(va);
            continue;
        }
        LfoRelease(va + 0x78);
        LfoRelease(va + 0xa0);
        QueueKoff(v);
        ListB_PushFront(va, cha + CH_LIST3);
        v->flags &= 0xfcff;
        StatReleased(v);
    }
}

/* 0x60f4: lists 1 then 2, each TAIL first (re-read after every removal): never-started voices freed, the others
 * KOFF + front of list 3 + stat 0x3p. No LFO release, no pedal test, list 3 untouched. */
void AllNotesOff(uint32_t cha)
{
    const uint32_t P[2] = {cha - 8, cha};
    for (int l = 0; l < 2; l++) {
        uint32_t va;
        while ((va = Lw(P[l] + 0xc)) != P[l]) {
            Voice *v = V(va);
            ListB_Unlink(va);
            uint16_t fl = v->flags;
            if (fl & 8) {
                ReleaseVoice(va);
                continue;
            }
            v->flags = fl & 0xfcff;
            QueueKoff(v);
            ListB_PushFront(va, cha + CH_LIST3);
            StatReleased(v);
        }
    }
}

/* ---------------------------------------------------------------- H3b: pending steal, exclusion group, portamento */

/* 0x6e34 (unreached: the last rung of NoteOn's ladder): sel without a core bit means both; core 1 then 0, each
 * pending list from its tail; an MS / fd voice (kind 0) whose priority is below the best so far (start prio + 1,
 * compared as u16) wins, so later equal ones do not. The victim never sounded: no KOFF, no free-list traffic, it
 * leaves both lists with its port / live counts dropped (saturating) and is returned to NoteOn. */
uint32_t StealPending(uint32_t prio, uint32_t sel) { return StealPendingKind(prio, sel, true); }

/* the body of f_6e34, shared with M6's f_875c (hsyn_se.cpp StealPendingSe = the same walk without the kind test) */
uint32_t StealPendingKind(uint32_t prio, uint32_t sel, bool msOnly)
{
    Bss &g = S();
    uint32_t best = prio + 1, victim = 0;
    if ((sel & 0x30) == 0) sel = 0x30;
    for (int c = 1; c >= 0; c--) {
        if (!(sel & kCoreSel[c])) continue;
        const uint32_t h = PendingHead((uint32_t)c);
        for (uint32_t p = Lw(h + 4); p != h; p = Lw(p + 4)) {
            const Voice *v = V(p);
            if ((!msOnly || v->kind == 0) && v->prio < (best & 0xffff)) {
                best = v->prio;
                victim = p;
            }
        }
    }
    if (victim == 0) return 0;
    Voice *v = V(victim);
    const uint32_t b14 = v->blk14;
    uint8_t live = R8(b14 + 2);
    if (live != 0) W8(b14 + 2, (uint8_t)(live - 1));
    if (v->kind == 0) {
        if (g.liveMs != 0) g.liveMs--;
    } else if (v->kind == 1) {
        if (g.liveSe != 0) g.liveSe--;
    }
    ListB_Unlink(victim);
    ListA_Unlink(victim);
    v->flags = 0;
    v->id = 0xff;
    v->kind = 0xff;
    v->b58 = 0xff;
    return victim;
}

/* 0x6f94 (unreached: every recorded Smpl +0xe is 0; also M6's f_7954): the channel's lists 1, 2, 3 (P = ch - 8, ch,
 * ch + 8), each from its tail; a voice of the group that never sounded (flags & 8) is unlinked and released, a
 * sounding one gets ADSR2 0xc021 at once (lists 1 / 2 also queue a KOFF; list 3 has its KOFF queued already) and is
 * killed. The walk continues at the saved next voice's prev. */
void KillGroup(uint32_t ch, uint32_t group)
{
    Bss &g = S();
    const uint32_t P[3] = {ch - 8, ch, ch + 8};
    group &= 0xff;
    for (int l = 0; l < 3; l++) {
        const uint32_t pn = P[l];
        for (uint32_t p = Lw(pn + 0xc); p != pn; p = Lw(p + 0xc)) {
            Voice *v = V(p);
            if (v->group != group) continue;
            const uint32_t nx = v->cnext;
            if (v->flags & 8) {
                ListB_Unlink(p);
                ReleaseVoice(p);
            } else {
                if (l < 2) g.maskKoff[v->core] |= 1u << v->idx;
                SdSetParam(0x400 | v->core | (uint32_t)v->idx << 1, 0xc021);
                Kill(p);
            }
            p = nx;
        }
    }
}

/* 0x5248 (unreached: CC 0x41 / 5 never sent): offset = (last released key - key) << 7, direction bit 4 (up from
 * below, offset >= 0) or 2; step = |offset| x tickLen / portaTime (divu; the caller tests portaTime != 0), >> 8
 * rounded toward zero as signed. f_4de0 counts the offset toward 0. */
void Portamento(uint32_t va)
{
    Voice *v = V(va);
    const Channel *ch = Ch(v->ch);
    const int32_t d = (int32_t)(((uint32_t)ch->portaState - v->key) << 7);
    v->porta = d;
    uint32_t m;
    if (d < 0) {
        v->flags |= 2;
        m = (uint32_t)-d;
    } else {
        v->flags |= 4;
        m = (uint32_t)d;
    }
    int32_t q = (int32_t)IOP_DIVU(m * S().tickLen, ch->portaTime, 0x52ac, "hsyn Portamento time 0");
    if (q < 0) q += 0xff;
    v->portaStep = (uint16_t)(q >> 8);
}

/* ---------------------------------------------------------------- velocity curve, LFO setup */

/* 0x9064: vel 0 counts as 1; a custom table row (id >> 4) - 1 < n remaps it (rows of 126 bytes indexed vel - 1:
 * original stride quirk; n is 0 in Versus); then the shape id & 0xf (>= 11 -> 0) of D_0b6f8. D_0b6f8 has 10 entries:
 * shape 10 reads the next word (the sine table) and would jump into IOP RAM - VelShape gets the index as is. */
uint32_t VelCurve(uint32_t vel, uint32_t id, uint32_t tbl, int32_t n)
{
    uint32_t x = (vel & 0xff) == 0 ? 1 : vel;
    int32_t k = (int32_t)((id & 0xff) >> 4) - 1;
    if (k != -1 && k < n) x = R8(tbl + (uint32_t)k * 126 + (x & 0xff) - 1);
    uint32_t s = id & 0xf;
    if (s >= 11) s = 0;
    return VelShape(s, x & 0xff) & 0xff;
}


/* 0x9b80: LCG, seed word D_0b8b0 (module .data, 0x02cee9d7 at load) */
int32_t Rand()
{
    uint32_t s = R32(g_base + D_SEED) * 0x41c64e6du + 0x3039;
    W32(g_base + D_SEED, s);
    return (int32_t)s >> 16;
}

/* the common tail 0x95b4 (f_9594) / 0x99b0 (f_9990): off = 0 (prog = HD Prog entry) or 4 (SE parameter record).
 * delay / fade-in in ticks rounded, the start phase = base + depth x Rand() (one LCG step), wrapped once into
 * 0 .. 0xffff, the phase step 0x10000 / period; then enable (mode & 0x66) and start (mode & 0x55). */
static void LfoTail(uint32_t obj, uint32_t prog, uint32_t mode, uint32_t a14, uint32_t a18, uint32_t tick, uint32_t which,
                    uint32_t off)
{
    StepObj *o = reinterpret_cast<StepObj *>(Mem(obj));
    const uint32_t half = tick >> 1;
    o->delay = (uint16_t)IOP_DIVU(((a14 & 0xffff) << 8) + half, tick, which ? 0x99c4 : 0x95c8, "hsyn LFO tick 0");
    uint32_t fade = IOP_DIVU(((a18 & 0xffff) << 8) + half, tick, which ? 0x99e0 : 0x95e4, "hsyn LFO tick 0");
    if (fade != 0) {
        fade = IOP_DIVU(0x10000, fade, which ? 0x99fc : 0x9600, "hsyn LFO fade 0");
        if (fade == 0) fade = 1;
    }
    o->rate = (int32_t)fade;
    const uint32_t p = prog + off;
    o->phase = (uint32_t)R8(p + (which ? 0x13 : 0x12)) << 8;
    int32_t r = (int16_t)Rand();
    uint32_t prod = o->phase * (uint32_t)r;   /* mult; mflo */
    if ((int32_t)prod < 0) prod += 0xffff;
    o->phase = (uint32_t)((int32_t)prod >> 16);
    o->phase += (uint32_t)R8(p + (which ? 0x11 : 0x10)) << 8;
    const uint32_t freq = (uint32_t)R16(p + (which ? 0x16 : 0x14)) << 8;
    int32_t ph = (int32_t)o->phase;
    if (ph < 0) o->phase = (uint32_t)ph + 0x10000;
    else if (0xffff < ph) o->phase = (uint32_t)ph + 0xffff0000u;
    o->step = IOP_DIVU(0x10000, IOP_DIVU(freq + half, tick, which ? 0x9b10 : 0x9714, "hsyn LFO tick 0"), which ? 0x9b24 : 0x9728, "hsyn LFO period 0");
    if (mode & 0x66) o->enable = 1;
    if (mode & 0x55) LfoStart(obj);
}

/* 0x9434 (+ the epilogue 0x975c and the jump table D_0b5c8 0x9544 .. 0x9594) and its twin 0x9830 (epilogue 0x9b58,
 * table D_0b5e8 0x9940 .. 0x9990, the same shape addresses; every prog byte offset + 4): clears the enable / state
 * bytes and the output; returns unless mode & 0x77 (Versus: always, Smpl +0x28 == 0). Shape select = prog[0xe]
 * (LFO 1) / [0xf] (LFO 2): bit 7 searches the custom table (8-byte entries, byte 0 compared with 0x80 only) and falls
 * back to a stale table length; 1..6 are the built-in shapes (6 = the 200-entry sine table D_0b720); 0 / > 6 return.
 * The shape is stored as its module address (f_922c's LfoShapeEval reads it). */
static void LfoSetupAt(uint32_t obj, uint32_t prog, uint32_t tbl, int32_t n, uint32_t mode, uint32_t a14, uint32_t a18,
                       uint32_t tick, uint32_t which, uint32_t off)
{
    StepObj *o = reinterpret_cast<StepObj *>(Mem(obj));
    o->enable = 0;
    o->state = 0;
    o->out = 0;
    if ((mode & 0x77) == 0) return;
    o->table = 0;
    uint32_t sel = R8(prog + off + (which ? 0xf : 0xe));
    if (sel & 0x80) {
        uint32_t p = tbl;
        for (int32_t i = 0; i < n; i++, p += 8) {
            if (R8(p) == 0x80) {
                o->table = R32(p + 4);
                o->shape = g_base + 0x9184;
                W16(obj + 0x24, R16(p + 2));
                break;
            }
        }
        if (o->table == 0 && R16(obj + 0x24) == 0) return;   /* miss: a stale +0x24 still runs the tail */
    } else {
        static const uint16_t kShape[6] = {0x90f0, 0x9108, 0x9128, 0x9164, 0x91f8, 0x9184};
        if (sel == 0 || sel >= 7) return;
        if (sel == 6) {
            o->table = g_base + 0xb720;
            W16(obj + 0x24, 200);
        }
        o->shape = g_base + kShape[sel - 1];
    }
    LfoTail(obj, prog, mode, a14, a18, tick, which, off);
}
void LfoSetup(uint32_t obj, uint32_t prog, uint32_t tbl, int32_t n, uint32_t mode, uint32_t a14, uint32_t a18,
              uint32_t tick, uint32_t which)
{
    LfoSetupAt(obj, prog, tbl, n, mode, a14, a18, tick, which, 0);
}
/* 0x9830 (M4's f_52d0, SE-stream voices; unreached) */
void LfoSetupSe(uint32_t obj, uint32_t prog, uint32_t tbl, int32_t n, uint32_t mode, uint32_t a14, uint32_t a18,
                uint32_t tick, uint32_t which)
{
    LfoSetupAt(obj, prog, tbl, n, mode, a14, a18, tick, which, 4);
}

/* the shape functions at f_9064 + 0x8c .. (0x90f0 saw up, 0x9108 saw down, 0x9128 triangle, 0x9164 square, 0x9184
 * table with linear interpolation) and f_91f8 (sample and hold: a new Rand() / 4 on the phase carry, else the last
 * output), called by f_922c as obj[0x14](obj, carry); phase = obj[0x18]. Results are the asm's s16 ranges. An
 * address that is none of them (never stored by the setups) returns 0; the original would jump to it. */
int32_t LfoShapeEval(uint32_t obj, uint32_t carry)
{
    if (g_shapeMock) return ShapeMockEval(obj, carry);
    const StepObj *o = reinterpret_cast<const StepObj *>(Mem(obj));
    const uint32_t ph = o->phase;
    switch (o->shape - g_base) {
    case 0x90f0: return (int32_t)((ph - 0x8000) << 15) >> 16;
    case 0x9108: return (int32_t)((0xffffu - ph - 0x8000) << 15) >> 16;
    case 0x9128: {
        uint32_t x = ph;
        if (0xbfff < (int32_t)ph) x = ph + 0xffff0000u;
        else if (!((int32_t)ph < 0x4001)) x = 0x8000 - ph;
        return (int16_t)x;
    }
    case 0x9164: return 0x7fff < (int32_t)ph ? 0x4000 : -0x4000;
    case 0x9184: {
        const uint32_t len = R16(obj + 0x24), t = o->table;
        const uint32_t pr = ph * len;   /* mult; mflo */
        uint32_t i = pr >> 16;
        int32_t a = (int16_t)R16(t + 2 * i);
        i++;
        if (!(i < len)) i = 0;
        int32_t b = (int16_t)R16(t + 2 * i);
        a = (int32_t)((uint32_t)a + (uint32_t)((int32_t)((uint32_t)(b - a) * (pr & 0xffff)) >> 16));
        uint32_t h = (uint32_t)a + ((uint32_t)a >> 31);
        return (int32_t)(h << 15) >> 16;
    }
    case 0x91f8: return carry ? (int32_t)((uint32_t)Rand() << 16) >> 18 : (int32_t)o->out;
    default: return 0;
    }
}

/* 0x93ec: delay -> state 1 (counter = delay), else fade-in rate -> state 2 (level 0), else state 3 */
void LfoStart(uint32_t obj)
{
    StepObj *o = reinterpret_cast<StepObj *>(Mem(obj));
    if (o->delay) {
        o->state = 1;
        o->counter = o->delay;
    } else if (o->rate) {
        o->state = 2;
        o->level = 0;
    } else o->state = 3;
}

/* 0x9778: only an enabled object; state 0 starts it, 1 and >= 4 end (6), 2 / 3 go to the hold (4, counter = delay)
 * or the release ramp (5, level 0x10000) or end */
void LfoRelease(uint32_t obj)
{
    StepObj *o = reinterpret_cast<StepObj *>(Mem(obj));
    if (o->enable == 0) return;
    uint8_t s = o->state;
    if (s == 1 || s >= 4) o->state = 6;
    else if (s == 0) LfoStart(obj);
    else if (o->delay) {
        o->state = 4;
        o->counter = o->delay;
    } else if (o->rate) {
        o->state = 5;
        o->level = 0x10000;
    } else o->state = 6;
}

}  // namespace hsyn
