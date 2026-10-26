/* fssd - see fssd.h. Function comments give the original's module offset. */
#include "fssd.h"
#include "../../platform/snd/iop/sd.h"
#include <string.h>

namespace fssd {

/* ---------------------------------------------------------------- module state (.bss: zero at load) */

static uint32_t s_base;                     /* image base: g_ucIrrBuf / g_ucEeToIopRpcBuff / g_ucIopToEeRpcBuff live there */
static int32_t _fdata;                      /* 0x9e0 "SPU initialised" */
static int32_t s_iIsInitHsyn;               /* 0x9e4 */
static uint32_t s_uiHsynReserveVoice[2];    /* 0x9e8 [0] core 0 voices 0..23, [1] core 1 */
static ModuleFunc s_psrcFunc[16];           /* 0xcf0 */
static iopk::RpcClient s_scdRpc;            /* 0xd30 (the 0x30-byte SifRpcClientData; not EE-visible) */
static Hsyn s_hsyn;                         /* host side: the not yet native hsyn library */

static uint8_t *g_ucIrrBuf() { return iopk::Ptr(s_base + OFS_IRR); }
static uint8_t *g_ucEeToIopRpcBuff() { return iopk::Ptr(s_base + OFS_EE2IOP); }
static uint8_t *g_ucIopToEeRpcBuff() { return iopk::Ptr(s_base + OFS_IOP2EE); }

static uint32_t rd32(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const void *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static void wr32(void *p, uint32_t v) { memcpy(p, &v, 4); }

static int sceHSyn_Init(uint32_t ctx, uint32_t a1) { return s_hsyn.Init ? s_hsyn.Init(s_hsyn.user, ctx, a1) : 0; }
static int sceHSyn_SetReservVoice(const uint32_t mask[2])
{
    return s_hsyn.SetReservVoice ? s_hsyn.SetReservVoice(s_hsyn.user, mask) : 0;
}

void SetHsyn(const Hsyn *h)
{
    if (h) s_hsyn = *h;
    else memset(&s_hsyn, 0, sizeof s_hsyn);
}
uint32_t Base() { return s_base; }
void ReserveMask(uint32_t out[2]) { out[0] = s_uiHsynReserveVoice[0]; out[1] = s_uiHsynReserveVoice[1]; }

/* ---------------------------------------------------------------- internals */

/* 0x198: byte loop, n signed (n <= 0 copies nothing), no bound */
static void MemCopy(void *dst, const void *src, int n)
{
    for (int i = 0; i < n; i++) ((uint8_t *)dst)[i] = ((const uint8_t *)src)[i];
}

/* 0x1cc */
static void InitModule()
{
    for (int i = 0; i < 16; i++) s_psrcFunc[i] = 0;
    _fdata = 0;
    s_iIsInitHsyn = 0;
    s_uiHsynReserveVoice[0] = 0;
    s_uiHsynReserveVoice[1] = 0;
}

/* 0x490: once per boot (HsynInit also sets the flag) */
static int InitSpu()
{
    if (_fdata != 1) {
        sd::Init(0);
        sd::SetParam(0x980, 0x3fff);
        sd::SetParam(0xa80, 0x3fff);
        sd::SetParam(0x981, 0x3fff);
        sd::SetParam(0xa81, 0x3fff);
        _fdata = 1;
    }
    return 0;   /* v0 = junk in the original; the dispatcher discards it */
}

/* 0x514: bind the IOP -> EE client; the original re-binds in a tight loop until the EE server exists (iopk's bind
 * returns 0 with server = 0 then): the native form of that poll waits for the next event between tries (Guard 1) */
static int BindIopToEeRpc()
{
    for (;;) {
        if (iopk::SifBindRpc(&s_scdRpc, SID_IOP2EE, 0) < 0) return 0;
        if (s_scdRpc.server != 0) return 1;
        iopk::WaitNextEvent();
    }
}

/* 0x570 */
static uint32_t GetFreeMemory() { return iopk::QueryMaxFreeMemSize(); }

/* 0x590: core is the whole word w0, ORed unmasked */
static int SetMasterVolume(uint32_t core, uint16_t l, uint16_t r)
{
    sd::SetParam((uint16_t)(core | 0x980), l);
    sd::SetParam((uint16_t)(core | 0xa80), r);
    return 1;
}

/* 0x5e8 */
static void SetReservVoiceFlag(uint32_t lo, uint32_t hi, uint32_t set)
{
    if (set) {
        s_uiHsynReserveVoice[0] |= lo;
        s_uiHsynReserveVoice[1] |= hi;
    } else {
        s_uiHsynReserveVoice[0] &= ~lo;
        s_uiHsynReserveVoice[1] &= ~hi;
    }
}

/* 0x310: the SID 0x101a server function. Word 0 of the shared reply buffer is cleared, nothing else (H1); module 0
 * replies ignore the handlers' return values except GetFreeMemory (Q7); an unregistered module replies the zero word */
static void *EeToIopRpcDispatch(int fno, void *buf, int size)
{
    uint8_t *irr = g_ucIrrBuf();
    uint32_t m = ((uint32_t)fno & 0xf0000000u) >> 28;
    wr32(irr, 0);
    if (m == 0) {
        const uint8_t *a = (const uint8_t *)buf;
        switch ((uint32_t)fno & 0x0fffffffu) {
        case 0x10000: InitSpu(); break;
        case 0x20000: BindIopToEeRpc(); break;
        case 0x30000: wr32(irr, GetFreeMemory()); break;
        case 0x40000: SetMasterVolume(rd32(a), rd16(a + 4), rd16(a + 8)); break;
        case 0x50000: SetReservVoiceFlag(rd32(a), rd32(a + 4), rd32(a + 8)); break;
        case 0x60000: HsynValidateReservVoice(); break;
        default: wr32(irr, 0xffffffffu); break;
        }
    } else {
        ModuleFunc cb = s_psrcFunc[m];
        if (cb) {
            int outsize = 0;
            void *r = cb((uint32_t)fno & 0x0fffffffu, buf, size, &outsize);
            MemCopy(irr, r, outsize);
        }
    }
    return irr;
}

/* 0x298: the server thread (ServerData / QueueData are its locals; SifRpcLoop never returns) */
static void EeToIopRpcThread(void *)
{
    iopk::RpcServer sd;
    iopk::RpcQueue qd;
    iopk::SifInitRpc(0);
    iopk::SifSetRpcQueue(&qd, iopk::GetThreadId());
    iopk::SifRegisterRpc(&sd, SID, EeToIopRpcDispatch, g_ucEeToIopRpcBuff(), 0, 0, &qd);
    iopk::SifRpcLoop(&qd);
}

/* 0x224 */
static int MakeEeToIopRpcThread()
{
    iopk::ThreadParam p;
    p.attr = 0x02000000;
    p.entry = EeToIopRpcThread;
    p.priority = 0x19;
    p.stack_size = 0x800;
    p.option = 0;
    int id = iopk::CreateThread(&p);
    if (id > 0) {
        iopk::StartThread(id, 0);
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- module start + exports */

/* 0x668. The host statics are cleared first: the state of a freshly loaded (zeroed) image. RegisterLibraryEntries
 * (its -1 failure return) vanishes: the exports are the functions below. */
int ModuleStart(const iopk::ModuleArgs *m)
{
    s_base = m->base;
    _fdata = 0;
    s_iIsInitHsyn = 0;
    memset(s_uiHsynReserveVoice, 0, sizeof s_uiHsynReserveVoice);
    memset(s_psrcFunc, 0, sizeof s_psrcFunc);
    memset(&s_scdRpc, 0, sizeof s_scdRpc);
    InitModule();
    iopk::SifInitRpc(0);
    return !(MakeEeToIopRpcThread() > 0);
}

/* 0x6c8 */
void Register(int module, ModuleFunc cb) { s_psrcFunc[module] = cb; }

/* 0x6e4 */
uint32_t CallRpc(int module, int wait, uint32_t cmd, const uint32_t w[8])
{
    uint8_t *buf = g_ucIopToEeRpcBuff();
    for (int i = 0; i < 8; i++) wr32(buf + i * 4, w[i]);
    iopk::SifCallRpc(&s_scdRpc, ((uint32_t)module << 28 & 0xf0000000u) | (cmd & 0x0fffffffu), wait == 0, buf, 0x20, buf, 4,
                     0, 0);
    return rd32(buf);
}

/* 0x7b0 */
int HsynInit(uint32_t ctx, uint32_t a1)
{
    if (s_iIsInitHsyn) return 0;
    int r = sceHSyn_Init(ctx, a1);
    s_iIsInitHsyn = 1;
    _fdata = 1;
    return r;
}

/* 0x7fc: no CpuSuspendIntr here (the caller EVAG holds it) */
int HsynSetReservVoice(const uint32_t mask[2])
{
    s_uiHsynReserveVoice[0] |= mask[0];
    s_uiHsynReserveVoice[1] |= mask[1];
    if (s_iIsInitHsyn == 1) {
        sceHSyn_SetReservVoice(s_uiHsynReserveVoice);
        return 1;
    }
    return 0;
}

/* 0x880: only the mask; hsyn is not touched (Q8) */
void HsynSetReservVoiceFlag(int voice, int on)
{
    if (voice < 0 || voice >= 0x30) return;
    uint32_t *w = &s_uiHsynReserveVoice[voice / 24];
    if (on) *w |= 1u << (voice % 24);
    else *w &= ~(1u << (voice % 24));
}

/* 0x91c */
void HsynValidateReservVoice()
{
    if (s_iIsInitHsyn == 1) {
        int st;
        iopk::CpuSuspendIntr(&st);
        sceHSyn_SetReservVoice(s_uiHsynReserveVoice);
        iopk::CpuResumeIntr(st);
    }
}

/* 0x970 (NOT RUN) */
void HsynClearReservVoice()
{
    if (s_iIsInitHsyn == 1) {
        s_uiHsynReserveVoice[0] = 0;
        s_uiHsynReserveVoice[1] = 0;
        int st;
        iopk::CpuSuspendIntr(&st);
        sceHSyn_SetReservVoice(s_uiHsynReserveVoice);
        iopk::CpuResumeIntr(st);
    }
}

}  // namespace fssd
