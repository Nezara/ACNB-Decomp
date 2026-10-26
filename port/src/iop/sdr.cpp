/* sdr - see sdr.h. Offsets = the original's module offsets (heuristic function names f_xxxx). */
#include "sdr.h"
#include "../../platform/plat.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

namespace sdr {

static uint32_t s_base;
int32_t B_01370, B_01374;   /* main / callback thread priority */
int32_t B_01378;            /* server thread id */
int32_t B_019e4;            /* callback thread id */
static uint32_t s_cbAddr[16];    /* host shadow of B_012b0: the IOP address a host Callback was registered for */
static Callback s_cbFn[16];
static int s_loggedCb, s_loggedHandler;
static const Io *g_io;

uint32_t Base() { return s_base; }
void SetIo(const Io *io) { g_io = io; }

static void wr32(uint32_t addr, uint32_t v) { memcpy(iopk::Ptr(addr), &v, 4); }
static uint32_t rd32(uint32_t addr) { uint32_t v; memcpy(&v, iopk::Ptr(addr), 4); return v; }
static uint32_t w32(const uint8_t *b, int off) { uint32_t v; memcpy(&v, b + off, 4); return v; }
static uint16_t hu16(const uint8_t *b, int off) { uint16_t v; memcpy(&v, b + off, 2); return v; }   /* lhu */
static int16_t h16(const uint8_t *b, int off) { int16_t v; memcpy(&v, b + off, 2); return v; }      /* lh */
static void *iop(uint32_t addr) { return iopk::Ptr(addr); }

/* sd call through the test seam */
#define IOV(m, f, ...) do { if (!g_io) sd::f(__VA_ARGS__); else if (g_io->m) g_io->m(g_io->user, __VA_ARGS__); } while (0)
#define IOR(T, m, f, ...) (!g_io ? (T)sd::f(__VA_ARGS__) : g_io->m ? (T)g_io->m(g_io->user, __VA_ARGS__) : (T)0)

/* ---------------------------------------------------------------- interrupt handlers (sd interrupt context) */

/* 0xd4c: channel 0 transfer end (channel argument not looked at) */
static int TransIntr0(int, void *)
{
    wr32(s_base + OFS_FLAGS, rd32(s_base + OFS_FLAGS) | 0x100);
    iopk::iWakeupThread(B_019e4);   /* SDR Q6: B_019e4 may be 0 */
    return 1;
}

/* f_0d8c: channel 1 transfer end */
static int TransIntr1(int, void *)
{
    wr32(s_base + OFS_FLAGS, rd32(s_base + OFS_FLAGS) | 0x200);
    iopk::iWakeupThread(B_019e4);
    return 1;
}

/* f_0dcc: SPU2 IRQ (mask = core mask) */
static int Spu2Intr(int mask, void *)
{
    wr32(s_base + OFS_FLAGS, rd32(s_base + OFS_FLAGS) | 0x400);
    wr32(s_base + OFS_FLAGS + 4, (uint32_t)mask);
    iopk::iWakeupThread(B_019e4);
    return 1;
}

sd::IntrHandler TransIntrHandler(int ch) { return ch ? TransIntr1 : TransIntr0; }
sd::IntrHandler Spu2IntrHandler() { return Spu2Intr; }

/* SDR Q5: sd returns the previous handler as a host pointer; the original replies an IOP code address */
static uint32_t handler_addr(sd::IntrHandler h)
{
    if (!h) return 0;
    if (h == TransIntr0) return s_base + OFS_TRANSINTR0;
    if (h == TransIntr1) return s_base + OFS_TRANSINTR1;
    if (h == Spu2Intr) return s_base + OFS_SPU2INTR;
    if (!s_loggedHandler++) plat_log("sdr: previous interrupt handler is not SDR's (replied as 0)");
    return 0;
}

/* ---------------------------------------------------------------- callback thread */

/* f_0c30: forward the interrupt flags to the EE (never returns) */
static void CbLoop()
{
    iopk::RpcClient *cd = (iopk::RpcClient *)iop(s_base + OFS_CLIENT);
    const uint32_t flags = s_base + OFS_FLAGS, send = s_base + OFS_SEND;
    for (;;) {
        if (rd32(flags) != 0) {
            for (;;) {
                int st;
                iopk::CpuSuspendIntr(&st);
                memcpy(iop(send), iop(flags), 0x40);
                iopk::CpuResumeIntr(st);
                iopk::SifCallRpc(cd, 0, 0, iop(send), 0x40, 0, 0, 0, 0);
                iopk::CpuSuspendIntr(&st);
                if (rd32(flags) == rd32(send)) {   /* SDR Q7: nothing new (a re-set bit is dropped) */
                    wr32(flags, 0);
                    iopk::iCancelWakeupThread(0);
                    iopk::CpuResumeIntr(st);
                    break;
                }
                wr32(flags, rd32(flags) & ~rd32(send));
                iopk::CpuResumeIntr(st);   /* asm 0xd20: back to the copy, flags not re-tested */
            }
        }
        iopk::SleepThread();
    }
}

/* f_0e10: bind SID 0x80000704, then the loop */
static void CbThread(void *)
{
    wr32(s_base + OFS_FLAGS, 0);
    iopk::RpcClient *cd = (iopk::RpcClient *)iop(s_base + OFS_CLIENT);
    for (;;) {
        if (iopk::SifBindRpc(cd, SID_IOP2EE, 0) < 0) {
            iopk::Printf("error \n");
            for (;;) iopk::WaitNextEvent();   /* SDR Q3: `j .` at 0xe50 */
        }
        /* 0x2710-iteration busy delay: free natively */
        if (cd->server != 0) break;
        iopk::WaitNextEvent();   /* the original rebinds without sleeping */
    }
    CbLoop();
}

/* ---------------------------------------------------------------- exports */

/* f_0278 (SDR Q4) */
int SetThreadPriority(int main, int cb)
{
    if ((uint32_t)(main - 9) >= 0x73 || (uint32_t)(cb - 9) >= 0x73) return -0x193;
    if (cb < main) {
        iopk::Printf(" SDR driver ERROR:\n");
        iopk::Printf("   callback th. priority is higher than main th. priority.\n");
        cb = main;
    }
    iopk::ThreadInfo ti;
    iopk::ReferThreadStatus(0, &ti);
    iopk::ChangeThreadPriority(0, 8);
    if (B_01378 > 0) {
        int r = iopk::ChangeThreadPriority(B_01378, main);
        if (r < 0) return r;
    }
    if (B_019e4 > 0) {
        int r = iopk::ChangeThreadPriority(B_019e4, cb);
        if (r < 0) return r;
    } else {
        B_01374 = cb;
    }
    iopk::ChangeThreadPriority(0, ti.prio);
    return 0;
}

/* f_0bec (SDR Q9) */
int SetCallback(uint32_t fno, uint32_t fn)
{
    if (fno - 0x9000 >= 0xf1) return -1;
    uint32_t a = s_base + OFS_CBTAB + ((fno & 0xf0) >> 4) * 4;
    uint32_t old = rd32(a);
    wr32(a, fn);
    return (int)old;
}

int SetCallbackFn(uint32_t fno, uint32_t addr, Callback fn)
{
    int r = SetCallback(fno, addr);
    if (fno - 0x9000 < 0xf1) {
        s_cbAddr[(fno & 0xf0) >> 4] = addr;
        s_cbFn[(fno & 0xf0) >> 4] = fn;
    }
    return r;
}

/* ---------------------------------------------------------------- RPC server */

/* f_0410: cmd = fno & 0xfff0 (the original's binary-search ladder = this switch) */
static void *Dispatch(int fno, void *buf, int size)
{
    (void)size;
    const uint8_t *b = (const uint8_t *)buf;
    const uint32_t cmd = (uint32_t)fno & 0xfff0, core = (uint32_t)fno & 0xf;
    const uint32_t D = s_base + OFS_RET;
    wr32(D, 0);   /* SDR Q1 */
    switch (cmd) {
    case 0x8000: wr32(D, (uint32_t)IOR(int, init, Init, (int)w32(b, 4))); break;   /* sceSdRemoteInit */
    case 0x8010: IOV(setParam, SetParam, hu16(b, 4), hu16(b, 8)); break;
    case 0x8020: wr32(D, IOR(uint16_t, getParam, GetParam, hu16(b, 4))); break;
    case 0x8030: IOV(setSwitch, SetSwitch, hu16(b, 4), w32(b, 8)); break;
    case 0x8040: wr32(D, IOR(uint32_t, getSwitch, GetSwitch, hu16(b, 4))); break;
    case 0x8050: IOV(setAddr, SetAddr, hu16(b, 4), w32(b, 8)); break;
    case 0x8060: wr32(D, IOR(uint32_t, getAddr, GetAddr, hu16(b, 4))); break;
    case 0x8070: (void)IOR(int, setCoreAttr, SetCoreAttr, hu16(b, 4), hu16(b, 8)); break;   /* result dropped */
    case 0x8080: wr32(D, IOR(uint16_t, getCoreAttr, GetCoreAttr, hu16(b, 4))); break;
    case 0x8090: wr32(D, IOR(uint16_t, note2Pitch, Note2Pitch, hu16(b, 4), hu16(b, 8), hu16(b, 0xc), h16(b, 0x10))); break;
    case 0x80a0: wr32(D, IOR(uint16_t, pitch2Note, Pitch2Note, hu16(b, 4), hu16(b, 8), hu16(b, 0xc))); break;
    case 0x80b0:
        wr32(D, (uint32_t)IOR(int, procBatch, ProcBatch, (const sd::BatchEntry *)iop(w32(b, 4)), (uint32_t *)iop(w32(b, 8)),
                              w32(b, 0xc)));
        break;
    case 0x80c0:
        wr32(D, (uint32_t)IOR(int, procBatchEx, ProcBatchEx, (const sd::BatchEntry *)iop(w32(b, 4)),
                              (uint32_t *)iop(w32(b, 8)), w32(b, 0xc), w32(b, 0x10)));
        break;
    case 0x80d0:
        wr32(D, (uint32_t)IOR(int, voiceTrans, VoiceTrans, h16(b, 4), hu16(b, 8), iop(w32(b, 0xc)), w32(b, 0x10),
                              w32(b, 0x14)));
        break;
    case 0x80e0: {
        uint32_t start = w32(b, 0x14);
        wr32(D, (uint32_t)IOR(int, blockTrans, BlockTrans, h16(b, 4), hu16(b, 8), iop(w32(b, 0xc)), w32(b, 0x10),
                              start ? iop(start) : (void *)0));
        break;
    }
    case 0x80f0: wr32(D, (uint32_t)IOR(int, voiceTransStatus, VoiceTransStatus, h16(b, 4), h16(b, 8))); break;
    case 0x8100: wr32(D, IOR(uint32_t, blockTransStatus, BlockTransStatus, h16(b, 4))); break;   /* a1 = lh 8: unused */
    case 0x8130: wr32(D, (uint32_t)IOR(int, setEffectAttr, SetEffectAttr, (int)core, (const sd::EffectAttr *)buf)); break;
    case 0x8140: (void)IOR(int, getEffectAttr, GetEffectAttr, (int)core, (sd::EffectAttr *)iop(s_base + OFS_EFFECT)); break;
    case 0x8150:
        wr32(D, (uint32_t)IOR(int, clearEffectWorkArea, ClearEffectWorkArea, (int)w32(b, 4), (int)w32(b, 8), (int)w32(b, 0xc)));
        break;
    case 0x8160: {   /* SDR H4: any nonzero w1 = channel 1; w2 picks null / SDR's handler; argument 0 */
        int ch = w32(b, 4) != 0;
        sd::IntrHandler h = w32(b, 8) ? TransIntrHandler(ch) : 0;
        wr32(D, handler_addr(IOR(sd::IntrHandler, setTransIntrHandler, SetTransIntrHandler, ch, h, (void *)0)));
        break;
    }
    case 0x8170: {
        sd::IntrHandler h = w32(b, 4) ? Spu2Intr : 0;
        wr32(D, handler_addr(IOR(sd::IntrHandler, setSpu2IntrHandler, SetSpu2IntrHandler, h, (void *)0)));
        break;
    }
    case 0x8180: wr32(D, (uint32_t)IOR(int, stopTrans, StopTrans, (int)w32(b, 4))); break;
    case 0x8190:
        wr32(D, (uint32_t)IOR(int, cleanEffectWorkArea, CleanEffectWorkArea, (int)w32(b, 4), (int)w32(b, 8), (int)w32(b, 0xc)));
        break;
    case 0x81a0: wr32(D, (uint32_t)IOR(int, setEffectMode, SetEffectMode, (int)core, (const sd::EffectAttr *)buf)); break;
    case 0x81b0:
        wr32(D, (uint32_t)IOR(int, setEffectModeParams, SetEffectModeParams, (int)core, (const sd::EffectAttr *)buf));
        break;
    case 0x81c0: {   /* SDR Q10: entries inline at +8, num = the high half of w0 */
        int r = IOR(int, procBatch, ProcBatch, (const sd::BatchEntry *)(b + 8), (uint32_t *)iop(s_base + OFS_BATCHRET),
                    hu16(b, 2));
        wr32(D, (uint32_t)r);
        wr32(s_base + OFS_BATCHRES, (uint32_t)r);
        break;
    }
    case 0x81d0: {
        int r = IOR(int, procBatchEx, ProcBatchEx, (const sd::BatchEntry *)(b + 8), (uint32_t *)iop(s_base + OFS_BATCHRET),
                    hu16(b, 2), w32(b, 4));
        wr32(D, (uint32_t)r);
        wr32(s_base + OFS_BATCHRES, (uint32_t)r);
        break;
    }
    case 0x8f10: wr32(D, (uint32_t)SetThreadPriority((int)w32(b, 4), (int)w32(b, 8))); break;
    case 0x9000: case 0x9010: case 0x9020: case 0x9030: case 0x9040: case 0x9050: case 0x9060: case 0x9070:
    case 0x9080: case 0x9090: case 0x90a0: case 0x90b0: case 0x90c0: case 0x90d0: case 0x90e0: case 0x90f0: {
        uint32_t idx = ((uint32_t)fno & 0xf0) >> 4;
        uint32_t e = rd32(s_base + OFS_CBTAB + idx * 4);
        if (!e) break;
        if (s_cbAddr[idx] == e && s_cbFn[idx]) wr32(D, s_cbFn[idx]((uint32_t)fno, buf));
        else if (!s_loggedCb++) plat_log("sdr: callback %04x at IOP address %x has no host function (replied 0)", cmd, e);
        break;
    }
    case 0xe620: {   /* SDR Q8: no guard against a second thread */
        iopk::ThreadParam p;
        p.attr = 0x02000000;
        p.option = 0;
        p.entry = CbThread;
        p.stack_size = 0x800;
        p.priority = B_01374;
        B_019e4 = iopk::CreateThread(&p);
        iopk::StartThread(B_019e4, 0);
        iopk::Printf("SDR callback thread created\n");
        break;
    }
    default: iopk::Printf("SDR driver ERROR: unknown command %x \n", cmd); break;
    }
    /* the shared tail 0xb78: reply pointer by command */
    if (cmd == 0x8140) return iop(s_base + OFS_EFFECT);
    if (cmd == 0x81c0 || cmd == 0x81d0) return iop(s_base + OFS_BATCHRES);
    return iop(D);
}

/* f_0278+0x108 (0x380): the server thread */
static void ServerThread(void *)
{
    iopk::RpcQueue qd;
    iopk::RpcServer sd;
    iopk::SifInitRpc(0);
    iopk::SifSetRpcQueue(&qd, iopk::GetThreadId());
    iopk::SifRegisterRpc(&sd, SID, Dispatch, iopk::Ptr(s_base + OFS_RECV), 0, 0, &qd);
    for (int i = 0; i < 16; i++) wr32(s_base + OFS_CBTAB + i * 4, 0);   /* B_012b0 */
    iopk::SifRpcLoop(&qd);
}

/* f_0000 (thpri= parsing NOT RUN: no module arguments in ACNB) */
static int parse_prio(const char *p)
{
    int v = (int)strtol(p, 0, 10);
    if ((uint32_t)(v - 9) >= 0x73) {
        iopk::Printf(" SDR driver error: invalid priority %d\n", v);
        v = 0x18;
    }
    return v;
}

int ModuleStart(const iopk::ModuleArgs *m)
{
    s_base = m->base;
    s_loggedCb = s_loggedHandler = 0;
    memset(s_cbAddr, 0, sizeof s_cbAddr);
    memset(s_cbFn, 0, sizeof s_cbFn);
    int st;
    iopk::CpuSuspendIntr(&st);
    /* RegisterLibraryEntries("sdr_driver") vanishes (its failure return 1) */
    iopk::CpuResumeIntr(st);
    iopk::Printf("SDR driver version 4.0.1 (C) SCEI\n");
    B_01370 = B_01374 = 0x18;
    B_01378 = 0;
    B_019e4 = 0;
    for (int i = 1; i < m->argc; i++) {
        const char *a = m->argv[i];
        if (!a || strncmp("thpri=", a, 6)) continue;
        const char *p = a + 6;
        if (isdigit((unsigned char)*p)) B_01370 = parse_prio(p);
        while (isdigit((unsigned char)*p)) p++;
        if (*p == ',' && isdigit((unsigned char)p[1])) B_01374 = parse_prio(p + 1);
        if (B_01374 < B_01370) {
            iopk::Printf(" SDR driver ERROR:\n");
            iopk::Printf("   callback th. priority is higher than main th. priority.\n");
            B_01374 = B_01370;
        }
        iopk::Printf(" SDR driver: thread priority: main=%d, callback=%d\n", B_01370, B_01374);
    }
    iopk::ThreadParam p;
    p.attr = 0x02000000;
    p.option = 0;
    p.entry = ServerThread;
    p.stack_size = 0x800;
    p.priority = B_01370;
    B_01378 = iopk::CreateThread(&p);
    if (B_01378 <= 0) return 1;
    iopk::StartThread(B_01378, 0);
    iopk::Printf(" Exit rsd_main \n");
    return 0;
}

}  // namespace sdr
