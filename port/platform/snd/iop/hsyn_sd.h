/* hsyn_sd.h: modhsyn's sd wrapper layer as inline calls into the native sd. Each
 * of the original's 19 wrappers is "take a log slot (f_9bcc), fill it (f_9c24), call sd, store the result (f_9c68)":
 * the log ring D_0b8c0 is 0 unless export 16 (SetLogRing, f_9bb0, no importer) installs one, so in practice only the sd
 * call and the wrappers' argument normalisation stay: registers / values to u16, the VoiceTrans / BlockTrans channel and
 * the status arguments signed 16-bit, Note2Pitch's fine argument signed 16-bit, GetParam / Note2Pitch / Pitch2Note
 * results & 0xffff, GetSwitch the full word. SetParam / SetSwitch / SetAddr / SetCoreAttr / GetEffectAttr log result
 * 0 and return sd's v0 residue, which no caller reads: void here (SetEffectAttr logs and returns sd's result). Every hsyn cluster calls these, not sd:: directly. The log ring is kept (LogBegin is
 * one RAM read when no ring is installed); the attr pointer of Set / GetEffectAttr is logged as 0 (natively a host
 * stack struct, not an IOP address). Wrappers with no caller in the module: SdInit, SdGetAddr, SdProcBatch,
 * SdProcBatchEx, SdBlockTrans, SdBlockTransStatus, SdClearEffectWorkArea (kept for completeness). */
#pragma once
#include <stdint.h>
#include "sd.h"
#include "iopk.h"
#include "hsyn_state.h"

namespace hsyn {

/* unit-test seam (hsyn::UnitTest only; 0 = the native sd): the register wrappers the tests drive go to this mock
 * instead, with the wrapper's normalised arguments. The H3 hooks may be 0: the call is then dropped under the mock. */
struct SdMock {
    void *user;
    void (*setParam)(void *u, uint32_t reg, uint32_t val);
    uint32_t (*getParam)(void *u, uint32_t reg);
    void (*setSwitch)(void *u, uint32_t reg, uint32_t mask);
    uint32_t (*getSwitch)(void *u, uint32_t reg);
    void (*setCoreAttr)(void *u, uint32_t entry, uint32_t val);
    int (*setEffectAttr)(void *u, int core, const sd::EffectAttr *a);
    void (*getEffectAttr)(void *u, int core, sd::EffectAttr *a);
    int (*voiceTrans)(void *u, int ch, uint32_t mode, uint32_t iop, uint32_t spu, uint32_t size);
    int (*voiceTransStatus)(void *u, int ch, int wait);
    void (*setAddr)(void *u, uint32_t reg, uint32_t val);   /* H4a (voice start SSA) */
};
extern const SdMock *g_sdMock;   /* hsyn.cpp */

/* the sd log ring (hsyn_bank.cpp): f_9bcc / f_9c24 / f_9c68 */
uint32_t LogAlloc();
void LogFill(uint32_t e, uint32_t kind, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t f);
void LogDone(uint32_t e, uint32_t r);
inline uint32_t LogBegin(uint32_t kind, uint32_t a, uint32_t b, uint32_t c = 0, uint32_t d = 0, uint32_t f = 0)
{
    if (!g_base || !R32(g_base + D_LOGRING)) return 0;   /* f_9bcc's own null test, inlined */
    uint32_t e = LogAlloc();
    LogFill(e, kind, a, b, c, d, f);
    return e;
}
inline void LogEnd(uint32_t e, uint32_t r)
{
    if (e) LogDone(e, r);
}

inline int SdInit(uint32_t flag)                                                                     /* f_9c8c */
{
    uint32_t e = LogBegin(0, flag, 0);
    int r = sd::Init((int)flag);
    LogEnd(e, (uint32_t)r);
    return r;
}
inline void SdSetParam(uint32_t reg, uint32_t val)                                                   /* f_9cf8 */
{
    reg &= 0xffffu;
    val &= 0xffffu;
    uint32_t e = LogBegin(1, reg, val);
    if (g_sdMock) g_sdMock->setParam(g_sdMock->user, reg, val);
    else sd::SetParam((uint16_t)reg, (uint16_t)val);
    LogEnd(e, 0);
}
inline uint32_t SdGetParam(uint32_t reg)                                                             /* f_9d74 */
{
    reg &= 0xffffu;
    uint32_t e = LogBegin(2, reg, 0);
    uint32_t r = (g_sdMock ? g_sdMock->getParam(g_sdMock->user, reg) : sd::GetParam((uint16_t)reg)) & 0xffffu;
    LogEnd(e, r);
    return r;
}
inline void SdSetSwitch(uint32_t reg, uint32_t mask)                                                 /* f_9de4 */
{
    reg &= 0xffffu;
    uint32_t e = LogBegin(3, reg, mask);
    if (g_sdMock) g_sdMock->setSwitch(g_sdMock->user, reg, mask);
    else sd::SetSwitch((uint16_t)reg, mask);
    LogEnd(e, 0);
}
inline uint32_t SdGetSwitch(uint32_t reg)                                                            /* f_9e5c */
{
    reg &= 0xffffu;
    uint32_t e = LogBegin(4, reg, 0);
    uint32_t r = g_sdMock ? g_sdMock->getSwitch(g_sdMock->user, reg) : sd::GetSwitch((uint16_t)reg);
    LogEnd(e, r);
    return r;
}
inline void SdSetAddr(uint32_t reg, uint32_t val)                                                    /* f_9ecc */
{
    reg &= 0xffffu;
    uint32_t e = LogBegin(5, reg, val);
    if (g_sdMock) {
        if (g_sdMock->setAddr) g_sdMock->setAddr(g_sdMock->user, reg, val);
    } else
        sd::SetAddr((uint16_t)reg, val);
    LogEnd(e, 0);
}
inline uint32_t SdGetAddr(uint32_t reg)                                                              /* f_9f44 */
{
    reg &= 0xffffu;
    uint32_t e = LogBegin(6, reg, 0);
    uint32_t r = sd::GetAddr((uint16_t)reg);
    LogEnd(e, r);
    return r;
}
inline void SdSetCoreAttr(uint32_t entry, uint32_t val)                                              /* f_9fb4 */
{
    entry &= 0xffffu;
    val &= 0xffffu;
    uint32_t e = LogBegin(7, entry, val);
    if (g_sdMock) {
        if (g_sdMock->setCoreAttr) g_sdMock->setCoreAttr(g_sdMock->user, entry, val);
    } else
        sd::SetCoreAttr((uint16_t)entry, (uint16_t)val);
    LogEnd(e, 0);
}
inline uint32_t SdNote2Pitch(uint32_t cnote, uint32_t cfine, uint32_t note, uint32_t fine)           /* f_a030 */
{
    cnote &= 0xffffu;
    cfine &= 0xffffu;
    note &= 0xffffu;
    int16_t f = (int16_t)fine;
    uint32_t e = LogBegin(9, cnote, cfine, note, (uint32_t)(int32_t)f);
    uint32_t r = sd::Note2Pitch((uint16_t)cnote, (uint16_t)cfine, (uint16_t)note, f) & 0xffffu;
    LogEnd(e, r);
    return r;
}
inline uint32_t SdPitch2Note(uint32_t cnote, uint32_t cfine, uint32_t pitch)                          /* f_a0e0 */
{
    cnote &= 0xffffu;
    cfine &= 0xffffu;
    pitch &= 0xffffu;
    uint32_t e = LogBegin(0xa, cnote, cfine, pitch);
    uint32_t r = sd::Pitch2Note((uint16_t)cnote, (uint16_t)cfine, (uint16_t)pitch) & 0xffffu;
    LogEnd(e, r);
    return r;
}
/* batch / ret are IOP addresses */
inline int SdProcBatch(uint32_t batch, uint32_t ret, uint32_t num)                                   /* f_a178 */
{
    uint32_t e = LogBegin(0xb, batch, ret, num);
    int r = sd::ProcBatch(reinterpret_cast<const sd::BatchEntry *>(iopk::Ptr(batch)),
                          reinterpret_cast<uint32_t *>(iopk::Ptr(ret)), num);
    LogEnd(e, (uint32_t)r);
    return r;
}
inline int SdProcBatchEx(uint32_t batch, uint32_t ret, uint32_t num, uint32_t voice)                 /* f_a204 */
{
    uint32_t e = LogBegin(0xc, batch, ret, num, voice);
    int r = sd::ProcBatchEx(reinterpret_cast<const sd::BatchEntry *>(iopk::Ptr(batch)),
                            reinterpret_cast<uint32_t *>(iopk::Ptr(ret)), num, voice);
    LogEnd(e, (uint32_t)r);
    return r;
}
/* f_a2a0: iop = IOP address of the source, size = the caller's 5th (stack) argument */
inline int SdVoiceTrans(uint32_t ch, uint32_t mode, uint32_t iop, uint32_t spu, uint32_t size)
{
    int c = (int16_t)ch;
    mode &= 0xffffu;
    uint32_t e = LogBegin(0xd, (uint32_t)c, mode, iop, spu, size);
    int r;
    if (g_sdMock) r = g_sdMock->voiceTrans ? g_sdMock->voiceTrans(g_sdMock->user, c, mode, iop, spu, size) : 0;
    else r = sd::VoiceTrans(c, (uint16_t)mode, iopk::Ptr(iop), spu, size);
    LogEnd(e, (uint32_t)r);
    return r;
}
/* f_a358: sd's 5th argument (start, mode 3 only) is the stale stack slot that holds the log copy of `iop` (quirk) */
inline int SdBlockTrans(uint32_t ch, uint32_t mode, uint32_t iop, uint32_t size)
{
    int c = (int16_t)ch;
    mode &= 0xffffu;
    uint32_t e = LogBegin(0xe, (uint32_t)c, mode, iop, size);
    int r = sd::BlockTrans(c, (uint16_t)mode, iopk::Ptr(iop), size, iopk::Ptr(iop));
    LogEnd(e, (uint32_t)r);
    return r;
}
inline int SdVoiceTransStatus(uint32_t ch, uint32_t wait)                                            /* f_a400 */
{
    int c = (int16_t)ch, w = (int16_t)wait;
    uint32_t e = LogBegin(0xf, (uint32_t)c, (uint32_t)w);
    int r;
    if (g_sdMock) r = g_sdMock->voiceTransStatus ? g_sdMock->voiceTransStatus(g_sdMock->user, c, w) : 0;
    else r = sd::VoiceTransStatus(c, w);
    LogEnd(e, (uint32_t)r);
    return r;
}
/* f_a48c: the original passes (s16) flag as a 2nd argument; the native sd::BlockTransStatus takes the channel only */
inline uint32_t SdBlockTransStatus(uint32_t ch, uint32_t flag)
{
    int c = (int16_t)ch, f = (int16_t)flag;
    uint32_t e = LogBegin(0x10, (uint32_t)c, (uint32_t)f);
    uint32_t r = sd::BlockTransStatus(c);
    LogEnd(e, r);
    return r;
}
inline int SdSetEffectAttr(int core, const sd::EffectAttr *a)                                       /* f_a518 */
{
    uint32_t e = LogBegin(0x13, (uint32_t)core, 0);
    int r;
    if (g_sdMock) r = g_sdMock->setEffectAttr ? g_sdMock->setEffectAttr(g_sdMock->user, core, a) : 0;
    else r = sd::SetEffectAttr(core, a);
    LogEnd(e, (uint32_t)r);
    return r;
}
inline void SdGetEffectAttr(int core, sd::EffectAttr *a)                                            /* f_a594 */
{
    uint32_t e = LogBegin(0x14, (uint32_t)core, 0);
    if (g_sdMock) {
        if (g_sdMock->getEffectAttr) g_sdMock->getEffectAttr(g_sdMock->user, core, a);
    } else
        sd::GetEffectAttr(core, a);
    LogEnd(e, 0);
}
inline int SdClearEffectWorkArea(uint32_t core, uint32_t ch, uint32_t mode)                         /* f_a608 */
{
    uint32_t e = LogBegin(0x15, core, ch, mode);
    int r = sd::ClearEffectWorkArea((int)core, (int)ch, (int)mode);
    LogEnd(e, (uint32_t)r);
    return r;
}

}  // namespace hsyn
