/* evag G2: the play engine. vagStart (RPC 0x201: KOFF, voice attributes, IRQ
 * address, KON after a 63 us wait, the "emergency" restart loop, then the first read request) and the play thread
 * _vagProc (prio 0x54), a state machine over s_ivmData.status woken by one max-1 semaphore (TransFinish, the SPU DMA-end
 * and SPU IRQ handlers): first read -> first SPU DMA (_vagAfterReadFirst), IRQ refills of the other half
 * (_vagIRQProc), DMA-end re-arm + next read (_vagSpu2TransProc), stop / pause (_vagOperateProc). Offsets = module
 * offsets. Versus never sends these commands (REV): checked by evag::UnitTest only. All arithmetic is the R3000's
 * (32-bit, signed compares where the asm uses slt / blez / bgtz). */
#include "evag_state.h"
#include "../../platform/plat.h"
#include "../../platform/snd/iop/iopdiv.h"

namespace evag {

/* the core's sd entry: (lb core | add) & 0xffff */
static uint16_t CoreEntry(uint32_t add) { return (uint16_t)((uint32_t)(int32_t)s_ivmData.core | add); }
/* KON / KOFF bit: sllv 1 by the s8 voice (low 5 bits) */
static uint32_t VoiceBit() { return 1u << ((uint32_t)(int32_t)s_ivmData.voice & 31); }

/* byte poke into the IOP buffer (Ivm.buf = IOP address, E7); offsets wrap as the original's addu does */
static void Poke(int32_t off, uint8_t v) { iopk::Ram()[(s_ivmData.buf + (uint32_t)off) & 0x1fffff] = v; }

/* signed div / mfhi of _vagIRQProc without a zero check (E8): IOP divide-by-zero rule (iopdiv.h: HI = the dividend
 * for a divisor of 0, 0 for INT_MIN / -1, one plat_log per site) */

/* the inline read step (map 9.2; vagStart, _vagSpu2TransProc, _vagProc states 5 / 7). Flagged (0x4000: the request is
 * out): the data arrived -> rd += half, play; tag 0x4007 = the last read: stop after 2 more IRQs. Unflagged: request
 * min(half, rest) when half < rest, else the rest when > 0 (half <= 0 below a positive rest requests half: no `if (n)`),
 * no request when nothing is left; then status = tag. Not guarded by s_ucSifdmaFlag (Q-E2). */
static void ReadStep(int16_t tag)
{
    VagMng &m = s_ivmData;
    if (m.status & 0x4000) {
        s_ucSifdmaFlag = 0;
        m.rd += m.half;
        if (tag == 0x4007) {
            m.op = (int16_t)(m.op | 1);
            m.op = (int16_t)(m.op | 0x1000);
            m.tail = 2;
        }
        m.status = 0xa;
        return;
    }
    int32_t half = (int32_t)m.half;
    int32_t rem = (int32_t)(m.total - m.rd);
    if (half < rem) IoCallRpcRead(m.rd, (uint32_t)half);
    else if (rem > 0) IoCallRpcRead(m.rd, (uint32_t)rem);
    m.status = tag;
}

/* 0x56c: state 2 + SifdmaFlag: the EE delivered the first 2 halves: patch the ADPCM loop flags (byte 1 of each 16-byte
 * block: 2 = loop body, 6 = loop start at block 0, 3 = loop end at the last block of the second half; a stream shorter
 * than 2 halves gets the end form 0 / 1 in its last two blocks), DMA it to the SPU buffer start, status 9 */
void _vagAfterReadFirst()
{
    VagMng &m = s_ivmData;
    IoSetCoreAttr(CoreEntry(4), 0);
    s_ucSafeIttr = 0;
    s_ucSifdmaFlag = 0;
    m.rd += m.half << 1;
    int32_t n;
    if ((int32_t)m.rd < (int32_t)m.total) {
        n = (int32_t)(m.half << 1);
        for (int32_t i = 0; i < n; i += 0x10) Poke(i + 1, 2);
        Poke(1, 6);
        Poke(0x11, 2);
        Poke(n - 0xf, 3);
    } else {
        n = (int32_t)m.total;
        for (int32_t i = 0; i < n; i += 0x10) Poke(i + 1, 2);
        Poke(n - 0x1f, 0);
        Poke(n - 0xf, 1);
    }
    iopk::FlushDcache();
    IoSetTransIntrHandler(0, _IntrTransCB);
    IoVoiceTrans(0, 0, m.buf, m.spu, (uint32_t)n);
    m.status = 9;
}

/* 0x6e0: state 0xa + IrqFlag: the SPU reached the IRQ address = one half played. Refill the half not playing (bit 0 of
 * toggle) from the IOP buffer start; once everything is in the SPU, park the IRQ at the stream end instead */
void _vagIRQProc()
{
    VagMng &m = s_ivmData;
    s_ucIrqFlag = 0;
    m.played += m.half;
    if (!((int32_t)m.played < (int32_t)m.total)) return;
    if (!((int32_t)m.xfer < (int32_t)m.total)) {
        int32_t r = IOP_REM(m.total, m.half << 1, 0x754, "evag _vagIRQProc half 0");
        IoSetAddr(CoreEntry(0x1f00), m.spu + (uint32_t)(r - 0x20));
        IoSetCoreAttr(CoreEntry(4), 1);
        return;
    }
    int32_t half = (int32_t)m.half;
    for (int32_t i = 0; i < half; i += 0x10) Poke(i + 1, 2);
    int32_t n = (int32_t)m.half;
    if (!(m.toggle & 1)) {
        Poke(1, 6);
        Poke(0x11, 2);
    } else {
        Poke(n - 0xf, 3);
    }
    if (!((int32_t)m.rd < (int32_t)m.total)) {
        /* last chunk: its end flags (written after the loop-end 3, which they can overwrite) */
        n = IOP_REM(m.total, m.half, 0x818, "evag _vagIRQProc half 0");
        if (n == 0) n = (int32_t)m.half;
        Poke(n - 0x1f, 0);
        Poke(n - 0xf, 1);
    }
    iopk::FlushDcache();
    IoVoiceTrans(0, 0, m.buf, m.spu + (uint32_t)(m.toggle & 1) * m.half, (uint32_t)n);
    m.status = 4;
}

/* 0x8b0: state 4 + TransFlag: the refill DMA ended: move the IRQ to the half just filled, flip toggle unless the
 * transfer ran past the stream end, IRQ on, then request the next half from the EE */
void _vagSpu2TransProc()
{
    VagMng &m = s_ivmData;
    s_ucTransFlag = 0;
    m.xfer += m.half;
    IoSetAddr(CoreEntry(0x1f00), m.spu + (uint32_t)(m.toggle & 1) * m.half);
    if (!((int32_t)m.total < (int32_t)m.xfer)) m.toggle = (uint8_t)((m.toggle & ~1) | ((1 - (m.toggle & 1)) & 1));
    IoSetCoreAttr(CoreEntry(4), 1);
    int32_t rem = (int32_t)(m.total - m.rd);
    if ((int32_t)m.half < rem) ReadStep(0x4005);
    else if (rem > 0) ReadStep(0x4007);
    else m.status = 0xa;
}

/* 0xb00: an IRQ with a pending operate request (op & 0x1000). f = op & 0xfff taken once at entry: bit 0 stop (after
 * `tail` more IRQs: count down and re-arm 0x1000, bit 0 kept), bit 1 pause; both can run in one call (Q-E6) */
void _vagOperateProc()
{
    VagMng &m = s_ivmData;
    int32_t f = m.op & 0xfff;
    if (f & 1) {
        if (m.tail > 0) {
            m.tail--;
            m.op = (int16_t)(m.op | 0x1000);
        } else {
            /* O2: a1 = 1 from the compare's li survives into the second call (0xb68 .. 0xb74) */
            if (IoVoiceTransStatus(0, 0) != 1) IoVoiceTransStatus(0, 1);
            IoSetSpu2IntrHandler(0);
            IoSetTransIntrHandler(0, 0);
            IoSetCoreAttr(CoreEntry(4), 0);
            Wait63us();
            IoSetParam(VoiceEntry(0), 0);
            IoSetParam(VoiceEntry(0x100), 0);
            IoSetSwitch(CoreEntry(0x1600), VoiceBit());
            m.status = 8;
            m.op = (int16_t)(m.op & 0xfff);
            m.op = (int16_t)(m.op & ~1);
        }
    }
    if (f & 2) {
        Wait63us();
        IoSetSwitch(CoreEntry(0x1600), VoiceBit());
        m.status = 6;
        m.op = (int16_t)(m.op & 0xfff);
        m.op = (int16_t)(m.op & ~2);
    }
}

/* 0xd8c: the play thread (vagSetup, prio 0x54). One wake per sema count (max 1: wakes coalesce, E3); the state
 * handlers re-check their flag bytes, states 5 / 7 do not (Q-E2). Returns (= thread exit) on quit or a 0 sema id. */
void _vagProc(void *arg)
{
    (void)arg;
    VagMng &m = s_ivmData;
    for (;;) {
        if (s_itpData.sema == 0) return;
        iopk::WaitSema(s_itpData.sema);
        if (s_itpData.quit == 1) return;
        if (s_ucIrqFlag == 1 && (m.op & 0x1000)) _vagOperateProc();
        switch (m.status & 0xfff) {
        case 0xa:
            if (s_ucIrqFlag == 1) _vagIRQProc();
            break;
        case 4:
            if (s_ucTransFlag == 1) _vagSpu2TransProc();
            break;
        case 9:
            if (s_ucTransFlag == 1) {
                s_ucTransFlag = 0;
                m.status = 3;
                m.xfer += m.half << 1;
            }
            break;
        case 7:
            ReadStep(0x4007);
            break;
        case 5:
            ReadStep(0x4005);
            break;
        case 2:
            if (s_ucSifdmaFlag == 1) _vagAfterReadFirst();
            break;
        default:
            break;
        }
    }
}

/* 0x12c8: RPC 0x201. Only from status 3 (first DMA done) or 6 (paused); clears bit 1 (`li -3`: 3 -> 1, 6 -> 4). An SPU
 * IRQ before SafeIttr (the emergency counter of _IntrSpu2CB) during the KON sequence restarts it, up to emergency 10
 * (each restart counts too). Then the first read of the play phase; SafeIttr = 1 lets IRQs wake the play thread. */
int vagStart()
{
    VagMng &m = s_ivmData;
    if (m.status != 3 && m.status != 6) return 0;
    m.status = (int16_t)(m.status & ~2);
    for (;;) {
        uint8_t e0 = s_ucEmergency;
        if ((IoGetCoreAttr(CoreEntry(4)) & 0xffff) == 1) IoSetCoreAttr(CoreEntry(4), 0);
        IoSetSwitch(CoreEntry(0x1600), VoiceBit());
        _SetVoiceAttr();
        IoSetSpu2IntrHandler(_IntrSpu2CB);
        if ((int32_t)(m.half << 1) < (int32_t)m.total) IoSetAddr(CoreEntry(0x1f00), m.spu + m.half);
        else IoSetAddr(CoreEntry(0x1f00), m.spu + m.total - 0x20);
        IoGetAddr(CoreEntry(0x1f00));
        IoGetAddr(CoreEntry(VoiceBit() | 0x2040));   /* Q-E1: 1 << v, not v << 1; the andi drops bit 23 */
        IoGetAddr(CoreEntry(VoiceBit() | 0x2240));
        IoSetCoreAttr(CoreEntry(4), 1);
        StampTime();
        Wait63us();
        IoSetSwitch(CoreEntry(0x1500), VoiceBit());
        StampTime();
        uint16_t l = m.volL, r = m.volR;
        IoSetParam(VoiceEntry(0), l);
        IoSetParam(VoiceEntry(0x100), r);
        if (e0 != s_ucEmergency && s_ucEmergency < 10) {
            iopk::Printf("<vagStart> !!!! emergency (%d) !!!! -> restart\n", (int)s_ucEmergency);
            s_ucEmergency++;
            continue;
        }
        break;
    }
    if (s_ucEmergency >= 10) iopk::Printf("<vagStart> !!!! emergency (%d) !!!! giveup -> error\n", (int)s_ucEmergency);
    s_ucEmergency = 0;
    int32_t rem = (int32_t)(m.total - m.rd);
    if (rem <= 0) {
        m.op = (int16_t)(m.op | 1);
        m.op = (int16_t)(m.op | 0x1000);
        m.tail = 0;
        m.status = 0xa;
    } else if ((int32_t)m.half < rem) {
        ReadStep(0x4005);
    } else {
        ReadStep(0x4007);
    }
    s_ucSafeIttr = 1;
    return 1;
}

}  // namespace evag
