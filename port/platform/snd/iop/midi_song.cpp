/* midi_song: modmidi's Song mode and the exports nothing imports.
 * A Song chunk (SQ Sequ +0x10) is a stream of 3-byte ops `a0 c d` (start sequence d, song volume / pan / tempo, fade,
 * repeat with an `a1 hi lo` jump target, controls) that SongStep (called from ATick while S2 flag 0x100 is set) runs
 * between sequences; exports 7 .. 13 drive it, 17 .. 19 / 22 .. 24 are channel volume / tempo setters and getters, 25
 * unloads a port, 1 .. 3 are empty. Dead in this game: no SQ on the disc has a Song chunk and no module imports these
 * exports; the unit tests (midi_selftest.cpp SongTest) are the only check. Original quirks kept as written (
 * 12.4): Unload stops port 2 n + 1 (Q-W3a), the raw fade target (Q-W3d), the repeat table rules (Q-W3g), SongSearch's
 * flag writes (Q-W3h). Divides by 0 (bpm 0, ATick step 0, fade length 0): the `break 0x1c00` guard IS reached (it sits
 * after the bnez's delay slot), the hybrid's interpreter logs the break and goes on; IOP divide-by-zero rule (iopdiv.h):
 * the interpreter's LO + one plat_log per site, and the store happens as in the asm. */
#include "midi.h"
#include "midi_state.h"
#include "../../plat.h"
#include "iopdiv.h"

namespace midi {

static uint32_t SongDebug() { return R32(g_base + D_DEBUG); }

/* ---------------------------------------------------------------- exports 17 .. 19, 22 .. 25 */

/* 0x056c export 25: reset a port struct to the unloaded state. Q-W3a: the port argument is overwritten with 2 port + 1
 * before the stop, so the port stopped is entry 2 (2 port + 1) + 1, not this one; volumes AND factors become 0 */
int Unload(uint32_t ctx, uint32_t port)
{
    if (!ctx) return -1;
    uint32_t g = R32(ctx + CTX_GROUP);
    port = port * 2 + 1;
    if (!(port < R32(g + GRP_N))) return -1;
    uint32_t s2 = R32(R32(g + GRP_ENTRIES) + port * 8 + 4);
    if (!s2) return -1;
    if (R32(s2 + S2_FLAGS) & FL_PLAY) MidiPlaySwitch(ctx, port, 0);
    uint32_t env = s2 + S2_ENV;
    W32(s2 + S2_SONG, 0xffffffffu);
    W32(s2 + S2_SEQ, 0xffffffffu);
    W32(s2 + S2_FLAGS, 0);
    W32(s2 + S2_TICK, 0);
    W32(env + E_SEQU, 0);
    W8(env + E_MASTER, 0x80);
    W32(env + E_MIDI, 0);
    W32(env + E_SONGCH, 0);
    W32(env + E_EXT, 0);
    W32(env + E_EV0, 0);
    W32(env + E_NEXT, 0);
    W32(env + E_ACC, 0);
    W32(env + E_TLEN, 0);
    W8(env + E_RUN, 0);
    W8(env + E_DFLAG, 0);
    W8(env + E_DATA_MSB, 0xff);
    W8(env + E_NRPN_MSB, 0xff);
    W8(env + E_DATA_LSB, 0xff);
    W8(env + E_NRPN_LSB, 0xff);
    W16(env + E_USED, 0);
    for (uint32_t ch = 0; ch < 16; ch++) {
        uint32_t r = env + E_REC + ch * REC_SIZE;
        W8(env + E_VOL + ch, 0);
        W8(env + E_VOLF + ch, 0);
        for (uint32_t i = 0; i < 10; i++) W8(r + i, 0xff);
        W16(r + REC_BEND, 0xffff);
    }
    return 0;
}

/* 0x2070 export 17: channel volume factor (ch < 16) or the master factor (ch 0xff, all 16 CC 7 re-sent while
 * playing); the CC 7 goes out only while playing (SendVolume) */
int SetChannelVolumeFactor(uint32_t ctx, uint32_t port, uint32_t ch, uint32_t v)
{
    uint32_t s2, env;
    if (!PortOf(ctx, port, &s2, &env)) return -1;
    uint32_t g1 = R32(ctx + CTX_GROUP) + GRP_SIZE;
    ch &= 0xff;
    if (ch < 16) {
        W8(env + E_VOLF + ch, (uint8_t)v);
        SendVolume(s2, env, g1, ch);
        return 0;
    }
    if (ch != 0xff) return -1;
    W8(env + E_MASTER, (uint8_t)v);
    if (R32(s2 + S2_FLAGS) & FL_PLAY)
        for (uint32_t c = 0; c < 16; c++) SendVolume(s2, env, g1, c);
    return 0;
}

/* 0x2160 export 18: channel volume (CC 7 raw value and its controller record; bit 7 set -> 0x7f), ch 0xff = all 16 */
int SetChannelVolume(uint32_t ctx, uint32_t port, uint32_t ch, uint32_t v)
{
    uint32_t s2, env;
    if (!PortOf(ctx, port, &s2, &env)) return -1;
    uint32_t g1 = R32(ctx + CTX_GROUP) + GRP_SIZE;
    uint8_t b = (v & 0x80) ? 0x7f : (uint8_t)v;
    ch &= 0xff;
    if (ch < 16) {
        W8(env + E_VOL + ch, b);
        W8(env + E_REC + ch * REC_SIZE + REC_CC7, b);
        SendVolume(s2, env, g1, ch);
        return 0;
    }
    if (ch != 0xff) return -1;
    for (uint32_t c = 0; c < 16; c++) {
        W8(env + E_VOL + c, b);
        W8(env + E_REC + c * REC_SIZE + REC_CC7, b);
        SendVolume(s2, env, g1, c);
    }
    return 0;
}

/* 0x2274 export 19: tempo = 60,000,000 / (bpm & 0xff) us per quarter (no "selected" test, Q-W3i) */
int SetTempoBpm(uint32_t ctx, uint32_t port, uint32_t bpm)
{
    uint32_t s2, env;
    if (!PortOf(ctx, port, &s2, &env)) return -1;
    bpm &= 0xff;
    W32(env + E_TEMPO, IOP_DIVU(60000000u, bpm, 0x22a4, "midi SetTempoBpm bpm 0"));
    Recalc(env);
    return 0;
}

/* 0x23c0 export 22: relative tempo; 0 on a bad port or with no sequence selected */
int GetRelativeTempo(uint32_t ctx, uint32_t port)
{
    uint32_t s2, env;
    if (!PortOf(ctx, port, &s2, &env)) return 0;
    if (R32(s2 + S2_SEQ) == 0xffffffffu) return 0;
    return R16(env + E_REL);
}

/* 0x2410 export 23: tempo in us per quarter, stored as given */
int SetTempoRaw(uint32_t ctx, uint32_t port, uint32_t us)
{
    uint32_t s2, env;
    if (!PortOf(ctx, port, &s2, &env)) return -1;
    W32(env + E_TEMPO, us);
    Recalc(env);
    return 0;
}

/* 0x2454 export 24: tempo in us per quarter; 0 on a bad port or with no sequence selected */
int GetTempoRaw(uint32_t ctx, uint32_t port)
{
    uint32_t s2, env;
    if (!PortOf(ctx, port, &s2, &env)) return 0;
    if (R32(s2 + S2_SEQ) == 0xffffffffu) return 0;
    return (int)R32(env + E_TEMPO);
}

/* 0x3740 exports 1 .. 3: `jr ra` (the original returns the caller's v0; map open question 2) */
int Reserved() { return 0; }

/* ---------------------------------------------------------------- Song helpers */

/* 0x24a4: msg on all 16 channels of port's struct (entry read without bound or PortOf, Q-W3i) */
void SendAll(uint32_t ctx, uint32_t port, uint32_t msg, int n)
{
    uint32_t g = R32(ctx + CTX_GROUP);
    uint32_t s2 = R32(R32(g + GRP_ENTRIES) + ((port << 4) | 8) + 4);
    if (!s2) return;
    uint32_t g1 = g + GRP_SIZE;
    msg &= 0xfffff0;
    for (uint32_t ch = 0; ch < 16; ch++) Send(s2, s2 + S2_ENV, g1, msg | ch, n);
}

/* 0x2550: master factor = song volume (or the fade's interpolation) * song factor / 128; sent only when it differs
 * from the last one (Q-W3d: the u8 last value against the unmasked v, the master byte gets v & 0xff) */
void SongVolUpdate(uint32_t ctx, uint32_t port, uint32_t env)
{
    int32_t v;
    uint32_t rem = R32(env + E_FADE_REM);
    if (rem) {
        uint32_t tgt = R8(env + E_FADE_TGT);
        int32_t prod = (int32_t)(((uint32_t)R8(env + E_SG_VOL) - tgt) * rem);
        int32_t len = (int32_t)R32(env + E_FADE_LEN);
        v = (int32_t)((uint32_t)IOP_DIV(prod, len, 0x2598, "midi SongVolUpdate fade length 0") + tgt);
    } else {
        v = R8(env + E_SG_VOL);
    }
    int32_t p = (int32_t)((uint32_t)v * R8(env + E_SG_VOLF));
    if (p < 0) p += 0x7f;
    v = p >> 7;
    if ((uint32_t)R8(env + E_SG_LAST) != (uint32_t)v) {
        SetChannelVolumeFactor(ctx, port, 0xff, (uint32_t)v & 0xff);
        W8(env + E_SG_LAST, (uint8_t)v);
    }
}

/* 0x2670: song flags 0x700 off, read pointer to the song start, repeat keys cleared (counts kept) */
void SongReset(uint32_t s2, uint32_t env)
{
    W32(s2 + S2_FLAGS, R32(s2 + S2_FLAGS) & ~0x700u);
    W32(env + E_SG_PTR, R32(env + E_SG_START));
    for (int i = 7; i >= 0; i--) W32(env + E_SG_REP + (uint32_t)i * 8, 0);
}

/* 0x27f4 / 0x2818 / 0x283c: song volume 0 .. 0x80, pan 0 .. 0x7f, tempo 10 .. 255 (signed) */
void ClampVol(uint32_t env, int32_t v)
{
    if (v < 0) v = 0;
    else if (v > 0x80) v = 0x80;
    W8(env + E_SG_VOL, (uint8_t)v);
}
void ClampPan(uint32_t env, int32_t v)
{
    if (v < 0) v = 0;
    else if (v > 0x7f) v = 0x7f;
    W8(env + E_SG_PAN, (uint8_t)v);
}
void ClampTempo(uint32_t env, int32_t v)
{
    if (v < 10) v = 10;
    else if (v >= 0x100) v = 0xff;
    W8(env + E_SG_TEMPO, (uint8_t)v);
}

/* 0x2864 (op 0): select sequence idx, song relative tempo + bpm, play, master volume, pan on 16 channels, flag 0x200.
 * Returns 1; a failed Select / Play returns 0 (the `bnez v0` delay slot zeroes v0 at 0x2898 / 0x28d0), and PlaySong
 * then goes on with the next op */
int SongStart(uint32_t ctx, uint32_t port, uint32_t s2, uint32_t idx)
{
    uint32_t env = s2 + S2_ENV;
    if (SelectMidi(ctx, port, idx)) return 0;
    MidiSetRelativeTempo(ctx, port, R16(env + E_SG_REL));
    SetTempoBpm(ctx, port, R8(env + E_SG_TEMPO));
    if (MidiPlaySwitch(ctx, port, 1)) return 0;
    SongVolUpdate(ctx, port, env);
    SendAll(ctx, port, (uint32_t)R8(env + E_SG_PAN) << 16 | 0xab0, 3);
    W32(s2 + S2_FLAGS, R32(s2 + S2_FLAGS) | FL_SONGACT);
    return 1;
}

/* 0x292c (op 0x11 count): the `a1 hi lo` op after it; key = the pointer after it. Count N plays the body N + 1 times,
 * 0 = forever (Q-W3g). Returns 0 on a missing a1 / a full table (the song stops), else 1. The loop callback (S2 +0x4c)
 * is IOP code: not supported, treated as unset; its info buffer (0x3ee0) is still written as the original does. */
int SongRepeat(uint32_t s2, uint32_t count)
{
    uint32_t env = s2 + S2_ENV;
    uint32_t p = R32(env + E_SG_PTR);
    if (R8(p) != 0xa1) {
        if (SongDebug()) plat_log("songRepeat Status Error %02x", R8(p));
        return 0;
    }
    W32(env + E_SG_PTR, p + 1);
    uint32_t hi = R8(p + 1);
    W32(env + E_SG_PTR, p + 2);
    uint32_t lo = R8(p + 2);
    W32(env + E_SG_PTR, p + 3);
    W8(g_base + 0x3ee1, (uint8_t)count);
    uint32_t key = p + 3;
    W32(g_base + 0x3ee4, key);
    uint32_t off = (hi << 8) + lo;
    if (uint32_t cb = R32(s2 + S2_LOOPCB)) {
        static bool warned;
        if (!warned) plat_log("midi: song loop callback %06x is IOP code: not supported, treated as unset", cb);
        warned = true;
    }
    if (count & 0xff) {
        uint32_t i;
        for (i = 0; i < 8; i++) {
            uint32_t e = env + E_SG_REP + i * 8;
            if (R32(e) != key) continue;
            uint8_t rem = (uint8_t)(R8(e + 4) - 1);
            W8(e + 4, rem);
            if (!rem) {
                W32(e, 0);
                return 1;
            }
            break;
        }
        if (i == 8) {
            for (i = 0; i < 8; i++) {
                uint32_t e = env + E_SG_REP + i * 8;
                if (R32(e)) continue;
                W8(g_base + 0x3ee2, 1);
                W32(e, key);
                W8(e + 4, (uint8_t)count);
                break;
            }
            if (i == 8) {
                if (SongDebug()) plat_log("songRepeat repeat table overflow");
                return 0;
            }
        }
    }
    W32(env + E_SG_PTR, R32(env + E_SG_START) + off);
    return 1;
}

/* 0x2b6c (+ the 15 case blocks of jump table D_03c10, epilogue 0x2ef4): run song ops until a start op succeeds, the
 * song mode flag clears or an error stops the song (SongPlaySwitch 0). The 0x100 test after every op re-reads S2. */
void PlaySong(uint32_t ctx, uint32_t port, uint32_t s2)
{
    uint32_t env = s2 + S2_ENV;
    if (!(R32(s2 + S2_FLAGS) & FL_SONG)) return;
    do {
        uint32_t p = R32(env + E_SG_PTR);
        uint32_t op = R8(p);
        W32(env + E_SG_PTR, p + 1);
        if (op != 0xa0) {
            if (SongDebug()) plat_log("playSong unknown status %02x", op);
            SongPlaySwitch(ctx, port, 0);
            return;
        }
        uint32_t c = R8(p + 1);
        W32(env + E_SG_PTR, p + 2);
        uint32_t d = R8(p + 2);
        W32(env + E_SG_PTR, p + 3);
        switch (c) {
        case 0x00:   /* 0x2c40 */
            if (SongStart(ctx, port, s2, d)) return;
            W8(env + E_SG_STATE, R8(env + E_SG_STATE) & 0xfb);
            break;
        case 0x01: ClampVol(env, (int32_t)d); break;                                          /* 0x2d74 */
        case 0x02: ClampVol(env, (int32_t)(d + R8(env + E_SG_VOL))); break;                   /* 0x2c70 */
        case 0x03: ClampVol(env, (int32_t)R8(env + E_SG_VOL) - (int32_t)d); break;            /* 0x2c8c */
        case 0x04: ClampPan(env, (int32_t)d); break;                                          /* 0x2ca8 */
        case 0x05: ClampPan(env, (int32_t)(d + R8(env + E_SG_PAN))); break;                   /* 0x2cbc */
        case 0x06: ClampPan(env, (int32_t)R8(env + E_SG_PAN) - (int32_t)d); break;            /* 0x2cd8 */
        case 0x07: {   /* 0x2cf4: fade length = d * 0.1 s in ATicks (Q-W3e) */
            W32(env + E_FADE_LEN, IOP_DIVU(d * 3125u << 12, R32(g_base + B_ATICK), 0x2d28, "midi PlaySong fade op ATick step 0"));
            break;
        }
        case 0x08:   /* 0x2d3c: fade to d (raw target, Q-W3d); same volume or length 0 = set directly */
            if (R8(env + E_SG_VOL) == d) W32(env + E_FADE_REM, 0);
            else W32(env + E_FADE_REM, R32(env + E_FADE_LEN));
            if (R32(env + E_FADE_REM) == 0) ClampVol(env, (int32_t)d);
            else W8(env + E_FADE_TGT, (uint8_t)d);
            break;
        case 0x11:   /* 0x2d88; the search-running branch is dead (Q-W3c) */
            if (R8(env + E_SG_STATE) & 4) {
                uint32_t q = R32(env + E_SG_PTR);
                if (R8(q) == 0xa1) {
                    W32(env + E_SG_PTR, q + 3);
                } else {
                    if (SongDebug()) plat_log("playSong loop ofst staus error %02x", R8(q));
                    SongPlaySwitch(ctx, port, 0);
                    return;
                }
            } else if (!SongRepeat(s2, d)) {
                SongPlaySwitch(ctx, port, 0);
                return;
            }
            break;
        case 0x21: ClampTempo(env, (int32_t)d); break;                                        /* 0x2df8 */
        case 0x22: ClampTempo(env, (int32_t)(d + R8(env + E_SG_TEMPO))); break;               /* 0x2e0c */
        case 0x23: ClampTempo(env, (int32_t)R8(env + E_SG_TEMPO) - (int32_t)d); break;        /* 0x2e28 */
        case 0x7f:   /* 0x2e44: controls */
            if (d == 1) {
                MidiPlaySwitch(ctx, port, 0);
            } else if (d == 0) {
                MidiPlaySwitch(ctx, port, 0);
                SendAll(ctx, port, 0x78b0, 3);
            } else if (d == 0x7f) {
                W32(s2 + S2_FLAGS, (R32(s2 + S2_FLAGS) & ~0x300u) | FL_SONGEND);
            } else {
                if (SongDebug()) plat_log("playSong unknown control %02x", d);
                SongPlaySwitch(ctx, port, 0);
                return;
            }
            break;
        default:     /* 0x2ef4 */
            if (SongDebug()) plat_log("playSong unknown operation %02x", c);
            SongPlaySwitch(ctx, port, 0);
            return;
        }
    } while (R32(s2 + S2_FLAGS) & FL_SONG);
}

/* 0x33ec: from the song start, skip ops (none applied) up to start op number target + 1: play != 0 -> after the
 * target-th start, play == 0 -> at the (target + 1)-th. Returns 1; 0 = end / bad op / missing a1, with 0x400 set and
 * 0x300 cleared (Q-W3h) */
int SongSearch(uint32_t s2, uint32_t env, uint32_t target, uint32_t play)
{
    uint32_t n = 0;
    SongReset(s2, env);
    uint32_t mark = R32(env + E_SG_PTR);
    W8(env + E_SG_STATE, 4);
    for (;;) {
        uint32_t p = R32(env + E_SG_PTR);
        uint32_t op = R8(p);
        W32(env + E_SG_PTR, p + 1);
        if (op != 0xa0) {
            if (SongDebug()) plat_log("searchPlay unknown status %02x", op);
            break;
        }
        uint32_t c = R8(p + 1);
        W32(env + E_SG_PTR, p + 2);
        uint32_t d = R8(p + 2);
        W32(env + E_SG_PTR, p + 3);
        if (c == 0x11) {
            if (R8(p + 3) == 0xa1) {
                W32(env + E_SG_PTR, p + 6);
                continue;
            }
            if (SongDebug()) plat_log("searchPlay loop ofst staus error %02x", R8(p + 3));
            break;
        }
        if (c == 0) {
            n++;
            if (target < n) {
                W32(env + E_SG_PTR, play ? mark : p);
                W8(env + E_SG_STATE, 0);
                return 1;
            }
            mark = p + 3;
            continue;
        }
        if (c == 0x7f && d == 0x7f) break;
    }
    W32(s2 + S2_FLAGS, (R32(s2 + S2_FLAGS) & ~0x300u) | FL_SONGEND);
    W8(env + E_SG_STATE, 0);
    return 0;
}

/* 0x30f0 (from ATick while 0x100): one fade step, then (unless a song sequence is playing) the deferred search of
 * export 13 and the song ops */
void SongStep(uint32_t ctx, uint32_t port, uint32_t s2)
{
    if (!(R32(s2 + S2_FLAGS) & FL_SONG)) return;
    uint32_t env = s2 + S2_ENV;
    uint32_t rem = R32(env + E_FADE_REM);
    if (rem) {
        rem--;
        W32(env + E_FADE_REM, rem);
        if (!rem) W8(env + E_SG_VOL, R8(env + E_FADE_TGT));
        SongVolUpdate(ctx, port, env);
    }
    uint32_t fl = R32(s2 + S2_FLAGS);
    if ((fl & (FL_SONGACT | FL_PLAY)) == (FL_SONGACT | FL_PLAY)) return;
    uint32_t st = R8(env + E_SG_STATE);
    if (st & 1) {
        uint32_t keep = fl & 0x700;
        if (!SongSearch(s2, env, R8(env + E_SG_SRCH), st & 2)) return;
        W32(s2 + S2_FLAGS, R32(s2 + S2_FLAGS) | keep);
    }
    PlaySong(ctx, port, s2);
}

/* ---------------------------------------------------------------- exports 7 .. 13 */

/* 0x26a8 export 7: select song idx (the Song chunk's +0xc word is the LAST index); a running song is stopped first
 * (only with 0x100 set, Q-W3j). Fade target and search target are not reset */
int SongSelect(uint32_t ctx, uint32_t port, uint32_t idx)
{
    uint32_t s2, env;
    if (!PortOf(ctx, port, &s2, &env)) return -1;
    uint32_t song = R32(env + E_SONGCH);
    if (!song) return -1;
    if (R32(song + 0xc) < idx) return -1;
    if (R32(song + 0x10 + idx * 4) == 0xffffffffu) return -1;
    if (R32(s2 + S2_FLAGS) & FL_SONG) SongPlaySwitch(ctx, port, 0);
    song = R32(env + E_SONGCH);
    uint32_t off = R32(song + 0x10 + idx * 4);
    W32(env + E_FADE_LEN, 0);
    W32(env + E_FADE_REM, 0);
    W8(env + E_SG_VOL, 0x80);
    W32(env + E_SG_START, song + off);
    W8(env + E_SG_VOLF, 0x80);
    W8(env + E_SG_LAST, 0xff);
    W8(env + E_SG_STATE, 0);
    W8(env + E_SG_PAN, 0x40);
    W8(env + E_SG_TEMPO, 0x78);
    W16(env + E_SG_REL, 0x100);
    W32(s2 + S2_SONG, idx);
    SongReset(s2, env);
    return 0;
}

/* 0x2f44 export 8: 0 stop (song reset, fade cancelled), 1 pause, 2 go (resume a paused sequence / start fresh /
 * nothing when finished); -1 for other modes or with no song selected */
int SongPlaySwitch(uint32_t ctx, uint32_t port, uint32_t mode)
{
    uint32_t s2, env;
    if (!PortOf(ctx, port, &s2, &env)) return -1;
    if (R32(s2 + S2_SONG) == 0xffffffffu) return -1;
    if (mode == 1) {
        W32(s2 + S2_FLAGS, R32(s2 + S2_FLAGS) & ~(uint32_t)FL_SONG);
        W8(env + E_SG_STATE, 0);
        MidiPlaySwitch(ctx, port, 0);
        return 0;
    }
    if (mode == 0) {
        W32(s2 + S2_FLAGS, R32(s2 + S2_FLAGS) & ~0x700u);
        W8(env + E_SG_STATE, 0);
        MidiPlaySwitch(ctx, port, 0);
        if (R32(env + E_FADE_REM)) {
            uint8_t t = R8(env + E_FADE_TGT);
            W32(env + E_FADE_REM, 0);
            W8(env + E_SG_VOL, t);
        }
        SongReset(s2, env);
        return 0;
    }
    if (mode != 2) return -1;
    if (R32(s2 + S2_FLAGS) & FL_SONG) return 0;
    W8(env + E_SG_STATE, R8(env + E_SG_STATE) & 0xfc);
    uint32_t fl = R32(s2 + S2_FLAGS);
    if (fl & FL_SONGEND) {
        W32(s2 + S2_FLAGS, fl & ~0x300u);
        return 0;
    }
    if (fl & FL_SONGACT) {
        W32(s2 + S2_FLAGS, fl | FL_SONG);
        MidiPlaySwitch(ctx, port, 1);
        return 0;
    }
    W32(s2 + S2_FLAGS, fl | FL_SONG);
    return 0;
}

/* 0x31dc export 9: song volume factor (master update only while a song sequence is active, 0x200) */
int SongSetVolumeFactor(uint32_t ctx, uint32_t port, uint32_t v)
{
    uint32_t s2, env;
    if (!PortOf(ctx, port, &s2, &env)) return -1;
    W8(env + E_SG_VOLF, (uint8_t)v);
    if (R32(s2 + S2_FLAGS) & FL_SONGACT) SongVolUpdate(ctx, port, env);
    return 0;
}

/* 0x3260 export 10: song volume (clamped 0 .. 0x80) */
int SongSetVolume(uint32_t ctx, uint32_t port, uint32_t v)
{
    uint32_t s2, env;
    if (!PortOf(ctx, port, &s2, &env)) return -1;
    ClampVol(env, (int32_t)(v & 0xff));
    if (R32(s2 + S2_FLAGS) & FL_SONGACT) SongVolUpdate(ctx, port, env);
    return 0;
}

/* 0x32e4 export 11: song tempo bpm (clamped 10 .. 255) */
int SongSetTempo(uint32_t ctx, uint32_t port, uint32_t v)
{
    uint32_t s2, env;
    if (!PortOf(ctx, port, &s2, &env)) return -1;
    ClampTempo(env, (int32_t)(v & 0xff));
    if (R32(s2 + S2_FLAGS) & FL_SONGACT) SetTempoBpm(ctx, port, R8(env + E_SG_TEMPO));
    return 0;
}

/* 0x3370 export 12: song relative tempo (u16) */
int SongSetRelTempo(uint32_t ctx, uint32_t port, uint32_t v)
{
    uint32_t s2, env;
    if (!PortOf(ctx, port, &s2, &env)) return -1;
    W16(env + E_SG_REL, (uint16_t)v);
    if (R32(s2 + S2_FLAGS) & FL_SONGACT) MidiSetRelativeTempo(ctx, port, v & 0xffff);
    return 0;
}

/* 0x3588 export 13: position the song at start op n (mode bit 1: after the n-th start / at the (n + 1)-th, see
 * SongSearch). In song mode, mode bit 0 defers the search to SongStep (after the running sequence); otherwise the
 * sequence is stopped, a fade cancelled, and the song goes on (SongPlaySwitch 2) */
int SongSearchPlay(uint32_t ctx, uint32_t port, uint32_t n, uint32_t mode)
{
    uint32_t s2, env;
    if (!PortOf(ctx, port, &s2, &env)) return -1;
    if (R32(s2 + S2_SONG) == 0xffffffffu) return -1;
    uint32_t fl = R32(s2 + S2_FLAGS);
    uint32_t keep = fl & 0x700;
    if (fl & FL_SONG) {
        if (mode & 1) {
            W8(env + E_SG_STATE, (mode & 2) ? 1 : 3);
            W8(env + E_SG_SRCH, (uint8_t)n);
            return 0;
        }
        MidiPlaySwitch(ctx, port, 0);
        if (R32(env + E_FADE_REM)) {
            uint8_t t = R8(env + E_FADE_TGT);
            W32(env + E_FADE_REM, 0);
            W8(env + E_SG_VOL, t);
        }
    }
    if (!SongSearch(s2, env, n, ((mode >> 1) ^ 1) & 1)) return -1;
    if (!(keep & FL_SONG)) return 0;
    return SongPlaySwitch(ctx, port, 2);
}

}  // namespace midi
