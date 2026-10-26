/* hsyn_sweep - group H2 of NI6b: the per-tick
 * pending sweep f_37c4 (KOFF / KON flush, voice starts) and playing sweep f_4de0 (ENDX / ENVX readback, kills, voice
 * updates, the voice-stat env words), the reap sweep f_509c, kill / steal (f_6314, f_6458, f_6234), the envelope / LFO
 * step object f_922c with its five handlers, and the SE-stream voice objects (f_a6a0, f_a810, f_a9e0, f_aaac).
 *
 * List walks follow the original exactly: tail -> head (oldest first); before a callee that
 * unlinks the voice the next pointer v[0] is read, and the walk continues with that node's prev word. Function comments
 * give the original's module offset. Unreached in every recording: f_6234, f_6458, f_a6a0, f_a810, f_a9e0, f_aaac, and
 * f_922c's handlers (the step objects stay in state 0 in Versus); f_509c runs in battle1 only. */
#include "hsyn.h"
#include "hsyn_state.h"
#include "hsyn_sd.h"

namespace hsyn {

static inline uint32_t VoiceReg(uint32_t c, uint32_t idx, uint32_t kind) { return c | idx << 1 | kind; }

/* ---------------------------------------------------------------- kill / steal */

/* 0x6314: leave the channel list, port live count and the live MS / SE counter (both saturating at 0), leave the
 * global list, back to the FREE list's front (the allocator pops the tail: freed voices are reused FIFO), statbuf
 * flag bits 4-6 cleared. No SPU access. */
void Kill(uint32_t va)
{
    Voice *v = V(va);
    Bss &g = S();
    ListB_Unlink(va);
    uint32_t b14 = v->blk14;
    v->flags = 0;
    uint8_t live = R8(b14 + 2);   /* blk + 0x16 */
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
    v->ramp1.state = 0;
    v->ramp2.state = 0;
    v->seg.state = 0;
}

/* queue KOFF + ADSR2 = 0xc021 (fast exponential release) at once, then kill: the steal of f_6458 / f_6234 */
static void ReleaseKill(uint32_t va)
{
    Voice *v = V(va);
    S().maskKoff[v->core] |= 1u << (v->idx & 31);
    SdSetParam(VoiceReg(v->core, v->idx, 0x400), 0xc021);
    Kill(va);
}

/* candidates of one B list (P = pseudo-node), head -> tail: prio <= thr (unsigned 16-bit) wins and lowers thr, so
 * the later of equal candidates wins */
static void StealScan(uint32_t P, uint32_t &thr, uint32_t &victim)
{
    for (uint32_t x = Lw(P + 8); x != P; x = Lw(x + 8)) {
        uint32_t pr = V(x)->prio;
        if (!((thr & 0xffff) < pr)) {
            victim = x;
            thr = pr;
        }
    }
}

/* 0x6458 (unreached: the port cap 48 = the whole pool): pass 1 the released lists (ch + 0x10) of the 16 channels,
 * pass 2 (only if pass 1 found nothing) per channel the key-held list then the hold list, the same thr */
int StealChannel(uint32_t blk14, uint32_t thr)
{
    uint32_t victim = 0;
    for (uint32_t i = 0; i < 16; i++) StealScan(blk14 + 4 + CH_SIZE * i + 8, thr, victim);
    if (victim == 0) {
        for (uint32_t i = 0; i < 16; i++) {
            uint32_t ch = blk14 + 4 + CH_SIZE * i;
            StealScan(ch - 8, thr, victim);
            StealScan(ch, thr, victim);
        }
        if (victim == 0) return 0;
    }
    ReleaseKill(victim);
    return 1;
}

/* 0x6234 (unreached; export 13, CC 0x78): every voice of the channel's three lists (P = ch - 8, ch, ch + 8), each
 * list from its tail (Kill unlinks: the tail is re-read) */
void AllSoundOffCh(uint32_t ch)
{
    const uint32_t P[3] = {ch - 8, ch, ch + 8};
    for (int l = 0; l < 3; l++) {
        uint32_t v;
        while ((v = Lw(P[l] + 0xc)) != P[l]) ReleaseKill(v);
    }
}

/* ---------------------------------------------------------------- the sweeps */

/* 0x37c4 -> the voices still waiting ("pend"). KOFF of both cores first; per core the pending list oldest first: a
 * voice is started when its hardware voice is quiet (ENVX < 0x3e8) and it is not an SE-stream voice without key;
 * started voices move to the playing list's front whatever blkKind is (only 0 / 1 call a starter: quirk); the KON
 * mask of a core is flushed before the next core's voices. */
uint32_t PendingSweep()
{
    Bss &g = S();
    for (uint32_t c = 0; c < 2; c++) {
        if (g.maskKoff[c] != 0) {
            SdSetSwitch(c | 0x1600, g.maskKoff[c]);
            g.maskKoff[c] = 0;
        }
    }
    uint32_t pend = 0;
    for (uint32_t c = 0; c < 2; c++) {
        uint32_t H = PendingHead(c);
        for (uint32_t va = Lw(H + 4); va != H;) {
            Voice *v = V(va);
            if (!(v->kind == 1 && v->key == 0xff) && SdGetParam(VoiceReg(c, v->idx, 0x500)) < 0x3e8) {
                uint32_t next = v->next;
                if (v->blkKind == 0) VoiceStart(va);
                else if (v->blkKind == 1) VoiceStartSe(va);
                ListA_Unlink(va);
                ListA_PushFront(va, PlayingHead(c));
                va = Lw(next + 4);
            } else {
                pend++;
                va = Lw(va + 4);
            }
        }
        if (g.maskKon[c] != 0) {
            SdSetSwitch(c | 0x1500, g.maskKon[c]);
            g.maskKon[c] = 0;
        }
    }
    return pend;
}

/* the kill rule shared by f_4de0 / f_509c: flags & 0x300 (key released / held-off) with the ENDX countdown v[0x4b]
 * running -> count down (to 0 at once when the voice's ENDX bit is clear), env reported as 0x7fff, no kill; countdown
 * done -> kill only when ENDX is set and ENVX == 0; flags & 0x300 == 0 -> kill when ENVX == 0. Returns 1 = kill;
 * *env = the value to report. */
static int KillRule(Voice *v, uint32_t flags, uint32_t endx, uint32_t *env)
{
    if (flags & 0x300) {
        uint8_t b = v->endxCount;
        if (b != 0) {
            v->endxCount = (endx & (1u << (v->idx & 31))) ? (uint8_t)(b - 1) : 0;
            *env = 0x7fff;
            return 0;
        }
        if (!(endx & (1u << (v->idx & 31)))) return 0;
        return (*env & 0xffff) == 0;
    }
    return (*env & 0xffff) == 0;
}

/* 0x4de0 -> the playing voices processed ("work"). Per core: ENDX read always (also for an empty list); the playing
 * list oldest first; while reapPending is set (cleared by a note-on that found the pool empty this tick) each voice's
 * ENVX is read and either the voice is killed (not counted, not processed) or its env word goes into the voice-stat
 * buffer (u16, never cleared; 0x7fff during the ENDX countdown). Every other voice: portamento countdown and pitch
 * object (if it has a sample), volume object, then the M4 pitch / volume writers. reapPending is set again at the end. */
uint32_t PlayingSweep()
{
    Bss &g = S();
    uint32_t work = 0;
    for (uint32_t c = 0; c < 2; c++) {
        uint32_t H = PlayingHead(c);
        uint32_t endx = SdGetSwitch(c | 0x1700);
        for (uint32_t va = Lw(H + 4); va != H;) {
            Voice *v = V(va);
            uint32_t flags = v->flags;
            if (g.reapPending != 0) {
                uint32_t env = SdGetParam(VoiceReg(c, v->idx, 0x500));
                if (KillRule(v, flags, endx, &env)) {
                    uint32_t next = v->next;
                    Kill(va);
                    if (g.statBuf) {
                        uint8_t &f = StatFlag(v->core, v->idx);
                        f &= 0x8f;
                    }
                    va = Lw(next + 4);
                    continue;
                }
                if (g.statBuf) StatEnv(v->core, v->idx) = (uint16_t)env;
            }
            if (v->vagi != 0) {
                if (flags & 6) {
                    if (flags & 2) {
                        int32_t t = (int32_t)((uint32_t)v->porta + v->portaStep);
                        v->porta = t;
                        if (t >= 0) {
                            v->porta = 0;
                            v->flags = (uint16_t)(v->flags & 0xfffd);
                        }
                    } else {
                        int32_t t = (int32_t)((uint32_t)v->porta - v->portaStep);
                        v->porta = t;
                        if (t <= 0) {
                            v->porta = 0;
                            v->flags = (uint16_t)(v->flags & 0xfffb);
                        }
                    }
                }
                StepObjStep(va + 0x78);
                if (v->kind == 1) SeRamp1Step(va + 0xc8);
                PitchWrite(va);
            }
            StepObjStep(va + 0xa0);
            if (v->kind == 1) {
                SeSegStep(va);
                SeRamp2Step(va);
            }
            VolumeWrite(va);
            work++;
            va = Lw(va + 4);
        }
    }
    g.reapPending = 1;
    return work;
}

/* 0x509c -> 1 if a voice was killed. Called by the note-on paths when a core's free pool is empty (battle1 only), in
 * the middle of the stream parse: the same walk and kill rule as f_4de0 (its own ENDX + ENVX reads), but the ENVX
 * goes into the voice's v[0x12] snapshot (0x7fff during the countdown; the steal ladder's tie-break), not into the
 * voice-stat buffer, and nothing else is processed. */
int Reap()
{
    Bss &g = S();
    int killed = 0;
    for (uint32_t c = 0; c < 2; c++) {
        uint32_t H = PlayingHead(c);
        uint32_t endx = SdGetSwitch(c | 0x1700);
        for (uint32_t va = Lw(H + 4); va != H;) {
            Voice *v = V(va);
            uint32_t flags = v->flags;
            uint32_t env = SdGetParam(VoiceReg(c, v->idx, 0x500));
            int k = KillRule(v, flags, endx, &env);
            v->env = (uint16_t)env;   /* the ENVX, or 0x7fff during the countdown (the asm stores both, in that order) */
            if (k) {
                uint32_t next = v->next;
                Kill(va);
                if (g.statBuf) {
                    uint8_t &f = StatFlag(v->core, v->idx);
                    f &= 0x8f;
                }
                va = next;
                killed = 1;
            }
            va = Lw(va + 4);
        }
    }
    return killed;
}

/* ---------------------------------------------------------------- step objects */

/* 0x922c with its handlers 0x92cc (1 delay), 0x9308 (2 attack), 0x93cc (3 sustain = the common tail), 0x934c
 * (4 hold), 0x9390 (5 decay); one frame, so one function. States >= 2 advance the phase (wrapping at 0x10000, carry
 * to the shape) and evaluate the shape first. The scaled outputs are (s16 shape * level 16.16) / 65536 truncated
 * toward zero (low 32 bits of the product). Output stored as u16 at +0x10 and returned as s16. */
int16_t StepObjStep(uint32_t oa)
{
    StepObj *o = reinterpret_cast<StepObj *>(Mem(oa));
    uint32_t a0 = 0, carry = 0;
    uint32_t s = o->state;
    if (s != 0 && s < 6) {
        if (s >= 2) {
            uint32_t ph = o->phase + o->step;
            if (0xffff < (int32_t)ph) {
                ph += 0xffff0000u;
                carry = 1;
            }
            o->phase = ph;
            a0 = (uint32_t)LfoShapeEval(oa, carry);
        }
        switch (s) {
        case 1: {
            uint16_t n = (uint16_t)(o->counter - 1);
            o->counter = n;
            if (n == 0) {
                if (o->rate != 0) {
                    o->level = 0;
                    o->state = 2;
                } else o->state = 3;
            }
            break;
        }
        case 2: {
            int32_t lv = o->level;
            int32_t p = (int32_t)((uint32_t)(int32_t)(int16_t)a0 * (uint32_t)lv);
            if (p < 0) p += 0xffff;
            a0 = (uint32_t)p >> 16;
            int32_t nl = (int32_t)((uint32_t)lv + (uint32_t)o->rate);
            o->level = nl;
            if (0xffff < nl) o->state = 3;
            break;
        }
        case 4: {
            uint16_t n = (uint16_t)(o->counter - 1);
            o->counter = n;
            if (n == 0) {
                if (o->rate != 0) {
                    o->level = 0x10000;
                    o->state = 5;
                } else o->state = 6;
            }
            break;
        }
        case 5: {
            int32_t lv = o->level;
            int32_t p = (int32_t)((uint32_t)(int32_t)(int16_t)a0 * (uint32_t)lv);
            if (p < 0) p += 0xffff;
            int32_t nl = (int32_t)((uint32_t)lv - (uint32_t)o->rate);
            a0 = (uint32_t)p >> 16;
            o->level = nl;
            if (nl <= 0) o->state = 6;
            break;
        }
        default: break;   /* 3: the tail only */
        }
    }
    o->out = (int16_t)a0;
    return (int16_t)a0;
}

/* 0xa6a0 (obj = v + 0xc8) / 0xa810 (v; obj v + 0xdc): linear ramp out = base + slope * count >> 8 / >> 16 (low 32
 * bits, logical shift) while count < len (unsigned), then out = base + delta and state 2; count counts on */
void SeRamp1Step(uint32_t oa)
{
    SeRamp *r = reinterpret_cast<SeRamp *>(Mem(oa));
    if (r->state != 1) return;
    uint32_t cnt = (uint32_t)r->count;
    if (cnt < (uint32_t)r->len) {
        uint32_t p = r->slope * cnt;
        r->count = (int32_t)(cnt + 1);
        r->out = (uint16_t)(r->base + (p >> 8));
    } else {
        r->state = 2;
        r->out = (uint16_t)(r->base + r->delta);
        r->count = (int32_t)((uint32_t)r->count + 1);
    }
}

int SeRamp2Step(uint32_t va)
{
    SeRamp *r = &V(va)->ramp2;
    if (r->state != 1) return 0;
    uint32_t cnt = (uint32_t)r->count;
    if (cnt < (uint32_t)r->len) {
        uint32_t p = r->slope * cnt;
        r->count = (int32_t)(cnt + 1);
        r->out = (uint16_t)(r->base + (p >> 16));
    } else {
        r->out = (uint16_t)(r->base + r->delta);
        r->state = 2;
        r->count = (int32_t)((uint32_t)r->count + 1);
    }
    return 0;
}

/* 0xa9e0: pan law into out[0] (L) / out[1] (R) (u32 words, 0x80 = unity); mono: both 0x80. flag != 0 folds the
 * signed pan to |pan|; the range clamps are dead for an s8 (kept). Centre 0x40; 0 silences R, >= 0x7f silences L. */
void SePanLaw(uint32_t out, uint32_t flag, int32_t pan)
{
    W32(out + 4, 0x80);
    W32(out, 0x80);
    if (S().mono) return;
    int32_t p = (int8_t)pan;
    if (flag != 0) {
        if (p < -0x80) p += 0xff;
        else if (!(p < 0x80)) p -= 0xff;
        if (p < 0) p = -p;
    } else {
        if (!(p < 0x80)) p = 0x7f;
        if (p < -0x80) p = -0x80;
    }
    if (p == 0) {
        W32(out + 4, 0);
        return;
    }
    if (!(p < 0x7f)) {
        W32(out, 0);
        return;
    }
    if (p < 0x40) {
        W32(out + 4, (uint32_t)(p << 1) & 0xff);
        return;
    }
    if (p < 0x41) return;
    W32(out, (uint32_t)((0x80 - p) << 1) & 0xff);
}

/* 0xaaac (obj = v + 0xf0, state 1 only): a segment of len steps from base with slope (20.12 accumulator, wrapped into
 * -0x7ffff .. 0x7f000); at the segment end the repeat counter reloads (flag: one reload of len) or parks pos at len,
 * then the target is taken (state 2). Every step feeds (s8)(acc >> 12) to the pan law with v[0x4e]. */
void SeSegStep(uint32_t va)
{
    Voice *v = V(va);
    SeSeg *s = &v->seg;
    uint32_t st = s->state;
    if (st != 1) return;
    if (!((uint32_t)s->pos < (uint32_t)s->len)) {
        int32_t rep = s->rep;
        s->pos = 0;
        if (rep != 0) {
            rep--;
            s->rep = rep;
            if (rep == 0) {
                if (s->flag != 0) {
                    int32_t rl = s->reload;
                    s->rep = (int32_t)st;
                    s->flag = 0;
                    s->len = rl;
                } else {
                    s->rep = -1;
                    s->pos = s->len;
                }
            }
        }
    }
    int32_t a2;
    if ((uint32_t)s->pos < (uint32_t)s->len) {
        a2 = (int32_t)((uint32_t)s->base + (uint32_t)s->slope * (uint32_t)s->pos);
        if (0x7f000 < a2) a2 = (int32_t)((uint32_t)a2 + 0xfff01000u);
        else if (a2 < (int32_t)0xfff80001u) a2 = (int32_t)((uint32_t)a2 + 0xff000u);
        s->acc = a2;
        if (a2 < 0) a2 += 0xfff;
        a2 >>= 12;
    } else {
        a2 = s->target;
        s->state = 2;
        s->acc = (int32_t)((uint32_t)a2 << 12);
    }
    a2 = (int8_t)a2;
    s->pos = (int32_t)((uint32_t)s->pos + 1);
    SePanLaw(va + 0x11c, v->panMode, a2);
}

}  // namespace hsyn
