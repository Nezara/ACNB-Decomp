/* ivag voice engine + play / stop / volume commands:
 * _Operate (the per-call state machine over the 8 tracks: stop / data end / pause / start-resume / deferred-fill
 * release, then KOFF / KON / VMIX through sd under CpuSuspendIntr, the IOP -> EE `a1000000` notification and the
 * 43 us _Wait2Ticks), the CommandProc handlers (_CommandPlay / Pause / Close) and the RPC-facing API (ivagPlay / Close /
 * Pause / GetChNum / SetChVolume / SetMasterVolume / SetVolumePack / SetTrackReverb). _Operate runs on three threads
 * (ThreadProc via _IRQProc under semOp, CommandProc via _CommandPlay under semOp / _CommandClose under the track sema,
 * the fssd RPC thread via Quit / ForceReset); it blocks in _WaitDmaTrans and always in _Wait2Ticks, after every sd write
 * (H-G3-1 / H-G3-2).
 *
 * All arithmetic is the R3000's: the volume scale is the low 32 bits of a `mult`, then a signed `div` by 0x3fff
 * (VolScale); the pitch a signed `div` of rate << 12 by 48000. `1 << voice` is `sllv` (shift amount mod 32). The
 * CallRpc payload words 2 .. 7 are _Operate's frame residue in the original (map 20.3 R): the callers pass the recorded
 * constants as OpRes. Host-only guards (logged; none is reached in the recordings): a voice core >= 2 (the original
 * indexes its 2-word stack masks with it, Q-G3f), a null slot in the pause arm (Q-G3b), channel indices outside the two
 * host slots, a negative channel in SetChVolume (Q-G3e). Function comments give the original's image offset. */
#include "ivag_state.h"
#include "fssd.h"
#include "../../platform/plat.h"
#include <string.h>

namespace ivag {

int g_op_calls;

static inline uint32_t vbit(uint8_t voice) { return 1u << (voice & 31); }
static inline uint16_t ventry(const IVagVoice *vp) { return (uint16_t)(vp->core | vp->voice << 1); }

/* (u)int product, low 32 bits, signed div by 0x3fff (truncates toward zero) */
int32_t VolScale(int32_t vol, int32_t master)
{
    return (int32_t)((uint32_t)vol * (uint32_t)master) / 0x3fff;
}

/* signed div of rate << 12 by 0xbb80 (48000 -> 0x1000) */
int32_t PitchOf(int32_t rate)
{
    return (int32_t)((uint32_t)rate << 12) / 48000;
}

/* 0x2c0c */
uint16_t _Change15bitVolume(int v)
{
    if (v >= 0) return (uint16_t)v;
    return (uint16_t)(0x7fff - ((0u - (uint32_t)v) & 0x3fff));
}

/* the 2-word per-core masks: a core outside 0 / 1 would write the original's stack (Q-G3f) */
static inline bool core_ok(const IVagVoice *vp)
{
    if (vp->core < 2) return true;
    plat_log("ivag: _Operate: voice slot core %u (not 0 / 1), mask update dropped", vp->core);
    return false;
}

static void pack_free(int i)
{
    s_IVagStatePack.state[i] = 0;
    s_IVagStatePack.req[i].w0 = -1;
    s_IVagStatePack.req[i].w1 = 0;
}

/* 0x1e2c */
void _Operate(const OpRes *res)
{
    IVagMng &m = s_IVagMng;
    uint32_t koff[2] = {0, 0}, kon[2] = {0, 0}, rev[2] = {0, 0};
    uint32_t s3 = 0, s2 = 0;   /* started / ended track masks */
    int st;
    g_op_calls++;
    for (int i = 0; i < 8; i++) {
        IVagTrack *t = &s_IVagTrack[i];
        if (!(t->flags & 1) && t->state == 0) {   /* 0x1ef8: free track */
            t->stopState = 0;
            pack_free(i);
            continue;
        }
        int ss = t->stopState;
        if (ss == 1 || ss == 2) {   /* 0x1f48 stop / 0x2048 data end */
            _WaitDmaTrans();
            for (int v = 0; v < t->nch; v++) {
                if (v >= 2) { plat_log("ivag: _Operate: track %d channel %d (host has 2 slots)", i, v); break; }
                IVagVoice *vp = t->vp[v];
                if (vp) {
                    if ((vp->flags & 1) && core_ok(vp)) koff[vp->core] |= vbit(vp->voice);
                    vp->flags &= ~1;
                    SpuMmng_Free(vp->spu);
                }
                t->vp[v] = 0;
            }
            t->nch = 0;
            if (ss == 1) {
                t->flags &= ~1;
                t->state = 0;
                t->stopState = 0;
                pack_free(i);
            } else {   /* state stays 6 (set by _IRQProc) */
                t->needTrans = 1;
                s2 |= 1u << i;
                t->stopState = 0;
                s_IVagStatePack.state[i] = 6;
            }
        } else if (ss == 3) {   /* 0x2128 pause (REV): slots and rings kept */
            _WaitDmaTrans();
            for (int v = 0; v < t->nch && v < 2; v++) {
                IVagVoice *vp = t->vp[v];
                if (!vp) { plat_log("ivag: _Operate: pause of track %d with an empty slot %d", i, v); continue; }
                if ((vp->flags & 1) && core_ok(vp)) koff[vp->core] |= vbit(vp->voice);
            }
            t->state = 5;
            t->needTrans = 1;
            t->stopState = 0;
            s_IVagStatePack.state[i] = 5;
        } else if (ss == 4 && (t->state == 5 || t->state == 3)) {   /* 0x21d0 start / resume */
            if (t->state == 3 && (t->flags & 2)) t->flags &= ~2;   /* REV */
            _WaitDmaTrans();
            for (int v = 0; v < t->nch && v < 2; v++) {
                IVagVoice *vp = t->vp[v];
                if (vp && (vp->flags & 1) && core_ok(vp)) kon[vp->core] |= vbit(vp->voice);
            }
            int32_t pitch = PitchOf(t->rate);
            for (int v = 0; v < t->nch; v++) {
                if (v >= 2) { plat_log("ivag: _Operate: track %d channel %d (host has 2 slots)", i, v); break; }
                IVagVoice *vp = t->vp[v];
                int32_t sL = 0, sR = 0;
                if (vp) {   /* the asm loads the volumes before its null test */
                    sL = VolScale(vp->volL, (uint16_t)m.masterL);
                    sR = VolScale(vp->volR, (uint16_t)m.masterR);
                }
                iopk::CpuSuspendIntr(&st);
                if (vp) {
                    uint16_t e = ventry(vp);
                    IoSetParam(e, _Change15bitVolume(sL));
                    IoSetParam(e | 0x100, _Change15bitVolume(sR));
                    IoSetParam(e | 0x200, (uint16_t)pitch);
                    IoSetParam(e | 0x300, 0x80ff);
                    IoSetParam(e | 0x400, 0xdfe0);
                    IoSetAddr(e | 0x2040, (uint32_t)vp->spu + (uint32_t)t->half * ((t->flags >> 1) & 1));
                    if ((t->flags & 8) && core_ok(vp)) rev[vp->core] |= vbit(vp->voice);   /* REV */
                }
                iopk::CpuResumeIntr(st);
            }
            if (t->state == 3) s3 |= 1u << i;   /* a first start notifies the EE, a resume does not */
            t->needTrans = 0;
            t->irqCount = 0;
            t->state = 4;
            t->stopState = 0;
            s_IVagStatePack.state[i] = 4;
        } else if (ss == 5 && t->state == 2) {   /* 0x24d8: releases ivagSetup's deferred first fill */
            t->needTrans = 0;
            t->stopState = 0;
        }
    }
    _GetSystemTime();
    iopk::CpuSuspendIntr(&st);
    if (koff[0]) IoSetSwitch(0x1600, koff[0]);   /* REV */
    if (koff[1]) IoSetSwitch(0x1601, koff[1]);
    if (kon[0]) IoSetSwitch(0x1500, kon[0]);     /* REV */
    if (kon[1]) IoSetSwitch(0x1501, kon[1]);
    if (kon[0]) {   /* REV: VMIXER first, both reads before both writes */
        uint32_t a = IoGetSwitch(0x1b00), b = IoGetSwitch(0x1900);
        IoSetSwitch(0x1b00, rev[0] | (a & ~kon[0]));
        IoSetSwitch(0x1900, rev[0] | (b & ~kon[0]));
    }
    if (kon[1]) {
        uint32_t a = IoGetSwitch(0x1b01), b = IoGetSwitch(0x1901);
        IoSetSwitch(0x1b01, rev[1] | (a & ~kon[1]));
        IoSetSwitch(0x1901, rev[1] | (b & ~kon[1]));
    }
    iopk::CpuResumeIntr(st);
    s_KeyOn[0] = (s_KeyOn[0] & ~koff[0]) | kon[0];
    s_KeyOn[1] = (s_KeyOn[1] & ~koff[0]) | kon[1];   /* Q-G3a: core 0's KOFF mask */
    if (s3 || s2) {   /* fsiopsdCallRpc(10, 0, 0x1000000, s3, s2, frame residue): no-wait */
        static const OpRes zero = {{0, 0, 0, 0, 0, 0}};
        const OpRes *r = res ? res : &zero;
        uint32_t w[8] = {s3, s2, r->w[0], r->w[1], r->w[2], r->w[3], r->w[4], r->w[5]};
        IoCallRpc(w);
    }
    _Wait2Ticks();
}

/* 0x2c38 (CommandProc, cmd 2) */
void _CommandPlay(int track)
{
    IVagMng &m = s_IVagMng;
    IVagTrack *t = &s_IVagTrack[track];
    iopk::WaitSema(m.semOp);
    int st = t->state;
    if (st != 5 && st != 2) {
        if (st != 3) {   /* REV: states 0, 1, 4, 6, 7+ */
            iopk::SignalSema(m.semOp);
            return;
        }
        t->flags &= ~2;
    }
    t->stopState = 4;
    /* a playing IRQ master: its next _IRQProc _Operate starts this track (H-G3-4) */
    if (m.irqTrack != 0xff && m.irqTrack < 8 && s_IVagTrack[m.irqTrack].state == 4) {
        iopk::SignalSema(m.semOp);
        return;
    }
    /* frame residue (20.3 R): stale 0x20, then the caller's s0 = &T[track], s1 = track, CommandProc's s2 .. s4 */
    const OpRes res = {{0x20, g_base + 0x5e30 + 0x40u * (uint32_t)track, (uint32_t)track, 0, 0, 0}};
    _Operate(&res);
    _UpdateMasterTrack(0);
    _SetIntrMasterTrack();
    iopk::SignalSema(m.semOp);
    if (m.mode != 0) return;
    if (_StartTransTrackProc(track) == 0) {   /* REV */
        iopk::WaitSema(m.semOp);
        m.mode = 2;
        _SetIntrMasterTrack();
        iopk::SignalSema(m.semOp);
    }
}

/* 0x2db4 (CommandProc, cmd 3; REV) */
void _CommandPause(int track)
{
    IVagTrack *t = &s_IVagTrack[track];
    if (t->state != 4) return;
    iopk::WaitSema(s_IVagMng.semOp);
    t->stopState = 3;
    iopk::SignalSema(s_IVagMng.semOp);
}

/* 0x2e20 (CommandProc, cmd 4): jump table D_05430 {state 0 -> return, 1 .. 6 -> the body}; with a playing IRQ master
 * the stop waits for its next _IRQProc (Q-G3g, also when the closed track is the master) */
void _CommandClose(int track)
{
    IVagMng &m = s_IVagMng;
    IVagTrack *t = &s_IVagTrack[track];
    switch (t->state) {
    case 1: case 2: case 3: case 4: case 5: case 6:
        iopk::WaitSema(m.semOp);
        t->stopState = 1;
        iopk::SignalSema(m.semOp);
        if (m.irqTrack == 0xff || (m.irqTrack < 8 && s_IVagTrack[m.irqTrack].state != 4)) {
            /* residue (unverified, no CallRpc from here in the recordings): the stale word as in _CommandPlay, the
             * caller's s0 = &IVagSema[track], s1 = track */
            const OpRes res = {{0x20, g_base + 0x5aa0 + 4u * (uint32_t)track, (uint32_t)track, 0, 0, 0}};
            iopk::WaitSema(s_IVagSema[track]);
            _Operate(&res);
            iopk::SignalSema(s_IVagSema[track]);
        }
        break;
    default:   /* 0, 7+ */
        break;
    }
}

/* ---------------------------------------------------------------- the RPC-facing API */

/* 0x492c: states 2, 3, 5 (Q-9; repeated every vb by the EE until the track plays, H-G3-4) */
int ivagPlay(int track)
{
    if (track < 0 || track >= 8) return 0;
    int st = s_IVagTrack[track].state;
    if (st == 5 || st == 2 || st == 3) return _AddCommand(track, 2);
    return 0;
}

/* 0x49b0: jump table D_054b0 {state 0 -> 0, 1 .. 6 -> _AddCommand(track, 4)} (Q-8) */
int ivagClose(int track)
{
    if (track < 0 || track >= 8) return 0;
    switch (s_IVagTrack[track].state) {
    case 1: case 2: case 3: case 4: case 5: case 6:
        return _AddCommand(track, 4);
    default:
        return 0;
    }
}

/* 0x4a38 (REV) */
int ivagPause(int track)
{
    if (track < 0 || track >= 8) return 0;
    if (s_IVagTrack[track].state == 4) return _AddCommand(track, 3);
    return 0;
}

/* 0x4aa4 (REV) */
int ivagGetChNum(int track)
{
    if (track < 0 || track >= 8) return 0;
    const IVagTrack *t = &s_IVagTrack[track];
    return (t->flags & 1) ? t->nch : 0;
}

/* 0x4afc: states 1 .. 5; stores the slot volumes, then writes VOLL / VOLR at once, keyed or not (Q-G3e) */
int ivagSetChVolume(int track, int ch, uint16_t l, uint16_t r)
{
    if (track < 0 || track >= 8) return 0;
    IVagTrack *t = &s_IVagTrack[track];
    if (t->state == 6 || t->state == 0) return 0;
    if (!(ch < t->nch)) return 0;
    if (ch < 0 || ch >= 2 || !t->vp[ch]) {   /* the original reads T.vp[ch] anyway */
        plat_log("ivag: ivagSetChVolume(%d, %d): channel outside the track's slots", track, ch);
        return 0;
    }
    IVagVoice *vp = t->vp[ch];
    vp->volL = l;
    vp->volR = r;
    int32_t sL = VolScale(l, (uint16_t)s_IVagMng.masterL);
    int32_t sR = VolScale(r, (uint16_t)s_IVagMng.masterR);
    int st;
    iopk::CpuSuspendIntr(&st);
    IoSetParam(ventry(vp), _Change15bitVolume(sL));
    IoSetParam(ventry(vp) | 0x100, _Change15bitVolume(sR));
    iopk::CpuResumeIntr(st);
    return 1;
}

/* 0x4c5c (REV): masters stored before the writes; every used slot 0 .. 15 (keyed or not) */
int ivagSetMasterVolume(uint16_t l, uint16_t r)
{
    if (!s_iInitialize) return 0;
    s_IVagMng.masterL = (int16_t)l;
    s_IVagMng.masterR = (int16_t)r;
    int st;
    iopk::CpuSuspendIntr(&st);
    for (int s = 0; s < 16; s++) {
        IVagVoice *vp = &s_IVagVoice[s];
        if (!(vp->flags & 1)) continue;
        int32_t sR = VolScale(vp->volR, r);
        int32_t sL = VolScale(vp->volL, l);
        IoSetParam(ventry(vp), _Change15bitVolume(sL));
        IoSetParam(ventry(vp) | 0x100, _Change15bitVolume(sR));
    }
    iopk::CpuResumeIntr(st);
    return 1;
}

static inline int16_t rd16s(const uint8_t *p)
{
    int16_t v;
    memcpy(&v, p, 2);
    return v;
}

/* 0x4dc4 (REV): records {lh track, lh ch, lh volL, lh volR}; the masters sampled once; volumes reloaded signed (Q-G3d);
 * a bad track or channel clears the result, an inactive track does not (asm 0x4e5c: the branch skips the clear);
 * registers only for playing tracks. The ladder clamps n to the received records (Q-G3f) */
int ivagSetVolumePack(const uint8_t *p, int n)
{
    int32_t mL = (uint16_t)s_IVagMng.masterL, mR = (uint16_t)s_IVagMng.masterR;
    int ret = 1;
    for (int i = 0; i < n; i++) {
        const uint8_t *rec = p + 8 * i;
        int tr = rd16s(rec);
        if (tr < 0 || tr >= 8) { ret = 0; continue; }
        IVagTrack *t = &s_IVagTrack[tr];
        if (!(t->flags & 1)) continue;
        int ch = rd16s(rec + 2);
        if (ch < 0 || !(ch < t->nch)) { ret = 0; continue; }
        if (ch >= 2 || !t->vp[ch]) {
            plat_log("ivag: ivagSetVolumePack: track %d channel %d outside the host slots", tr, ch);
            continue;
        }
        IVagVoice *vp = t->vp[ch];
        vp->volL = (uint16_t)rd16s(rec + 4);
        vp->volR = (uint16_t)rd16s(rec + 6);
        if (t->state != 4) continue;
        int32_t sL = VolScale((int16_t)vp->volL, mL);
        int32_t sR = VolScale((int16_t)vp->volR, mR);
        int st;
        iopk::CpuSuspendIntr(&st);
        IoSetParam(ventry(vp), _Change15bitVolume(sL));
        IoSetParam(ventry(vp) | 0x100, _Change15bitVolume(sR));
        iopk::CpuResumeIntr(st);
    }
    return ret;
}

/* 0x52a0: compares the raw `on` with bit 3 (Q-G3c); per channel VMIXER then VMIXEL read-modify-write; no slot null test
 * in the original (host: skipped + logged) */
void ivagSetTrackReverb(int track, int on)
{
    if (track < 0 || track >= 8) return;
    IVagTrack *t = &s_IVagTrack[track];
    if (!(t->flags & 1)) return;
    if (on == ((t->flags >> 3) & 1)) return;
    t->flags = (uint8_t)((t->flags & ~8) | ((on & 1) << 3));
    int st;
    iopk::CpuSuspendIntr(&st);
    for (int ch = 0; ch < t->nch; ch++) {
        IVagVoice *vp = ch < 2 ? t->vp[ch] : 0;
        if (!vp) { plat_log("ivag: ivagSetTrackReverb: track %d channel %d has no slot", track, ch); continue; }
        uint32_t bit = vbit(vp->voice), add = on ? bit : 0;
        uint32_t a = IoGetSwitch((uint16_t)(vp->core | 0x1b00));
        IoSetSwitch((uint16_t)(vp->core | 0x1b00), add | (a & ~bit));
        uint32_t b = IoGetSwitch((uint16_t)(vp->core | 0x1900));
        IoSetSwitch((uint16_t)(vp->core | 0x1900), add | (b & ~bit));
    }
    iopk::CpuResumeIntr(st);
}

}  // namespace ivag
