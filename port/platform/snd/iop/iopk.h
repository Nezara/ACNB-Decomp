/* iopk: native IOP kernel services (phase 3 step 5 NI4) - the layer the decompiled IOP sound modules (sd, hsyn, midi,
 * sein, and the game's own fssd / ads / synth / ivag / mfstrm) run on. No MIPS, no interpreter: the IOP's kernel
 * libraries (sysmem, thbase, thsemap, thevent, intrman, timrman, sifman, sifcmd, cdvdman, ioman) as native calls with
 * the semantics of the hybrid's HLE kernel (port/runtime/iop_kern.c) in its zero-cost mode (ACNB_IOP_CPI0 = the "z"
 * oracle; contract: ).
 *
 * Generic (no game addresses; Last Raven reusable): the host plugs in the SPU2 mixer clock and the EE side through
 * Hooks, and starts each native module through StartModule (a "phantom image": the module's load range is reserved in
 * the IOP heap at the address the hybrid's IRX loader would use, so IOP addresses the EE sees - reply buffers, DMA
 * targets inside a module's .bss - match the hybrid). One kernel per process (the IOP); not thread-safe: every call
 * comes from the sim thread or from an IOP thread running on it.
 *
 * Time (4b): one virtual clock T in IOP cycles (36.864 MHz). Module code costs nothing; T moves only when no IOP
 * thread is ready (idle jump to the next event, capped at the host's target) or when a polling thread calls
 * WaitNextEvent (the native form of the hybrid's spin guard: `while (!cond) iopk::WaitNextEvent();`). Loop step at T:
 * service pass (SPU2 mixed to 16 * floor(T / 12288) samples in 16-sample chunks, Hooks::spu_tick, DelayThread
 * wake-ups in thread-id order; then, if interrupts are enabled: alarms (slot order), hard timers (slot order), pending
 * IRQs (number order), deferred calls (FIFO)), then the highest-priority ready thread (lowest priority number, then
 * the order it became ready) runs until a slice boundary: a blocking call, a wake-up of a higher-priority thread,
 * ChangeThreadPriority, CpuResumeIntr re-enabling interrupts, WaitNextEvent, or exit. Handlers run on the host's
 * context, to completion, with QueryIntrContext() = 1. A host entry (Advance / RpcCall / WaitServer / StartModule)
 * returns when T reaches its target (no service pass at the target itself: that runs at the next entry) or when its
 * predicate holds (checked after the service pass, before a thread is picked).
 *
 * Threads: one native coroutine per IOP thread (Windows fibers; portable fallback: one host thread per IOP thread
 * with a strict hand-off token, so exactly one runs at a time). A thread runs only inside a host entry. Exit,
 * TerminateThread and DeleteThread of a thread that is blocked mid-function unwind its stack with __builtin_longjmp
 * (module code is C-like: no destructors). */
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace sd { struct Kernel; }

namespace iopk {

constexpr uint32_t RAM_SIZE = 0x200000;            /* IOP RAM image (IOP address 0) */
constexpr uint32_t HEAP_START = 0x10000;           /* sysmem heap start, as the hybrid (below: its traps / intr stack) */
constexpr uint64_t HZ = 36864000;                  /* IOP cycles per second */
constexpr uint64_t CYC_PER_SAMPLE = 768;           /* 48 kHz output sample */
constexpr int CHUNK_SAMPLES = 16;                  /* mixer step */
constexpr uint64_t CHUNK_CYC = CHUNK_SAMPLES * CYC_PER_SAMPLE;   /* 12288 */

enum {
    KE_OK = 0, KE_ERROR = -1, KE_CPUDI = -102, KE_INTRDISABLE = -104, KE_NO_MEMORY = -400, KE_ILLEGAL_THID = -406,
    KE_UNKNOWN_THID = -407, KE_UNKNOWN_SEMID = -408, KE_UNKNOWN_EVFID = -409, KE_NOT_WAIT = -413,
    KE_RELEASE_WAIT = -418, KE_SEMA_ZERO = -419, KE_SEMA_OVF = -420, KE_EVF_COND = -421, KE_WAIT_DELETE = -425
};
enum { TS_RUN = 1, TS_READY = 2, TS_WAIT = 4, TS_SUSPEND = 8, TS_DORMANT = 16 };
enum { WT_NONE, WT_SLEEP, WT_DELAY, WT_SEMA, WT_EVENT, WT_RPC, WT_EERPC };
enum { EW_AND = 0, EW_OR = 1, EW_CLEAR = 0x10 };   /* WaitEventFlag / PollEventFlag mode bits */

/* ---- host side */

struct Hooks {
    void *user;
    /* SPU2 clock: spu_run(16) per 16-sample chunk the IOP clock crossed (the mixer raises its DMA-end / IRQ events
     * from here: the host calls sd::DmaEnd + RaiseIrq(36 / 40), RaiseIrq(9)); spu_tick once per service pass after
     * the mixing (hybrid spu2_tick: complete pending sample-memory DMAs); spu_next_event = the mixer's next own event
     * in IOP cycles (~0 = none). All optional. */
    void (*spu_run)(void *u, int samples);
    void (*spu_tick)(void *u);
    uint64_t (*spu_next_event)(void *u);
    /* EE side: IOP -> EE SIF DMA (bytes already rounded up to 16 as the hybrid; nonzero = done), EE RPC server
     * lookup for SifBindRpc / SifCallRpc, and "a request was posted for EE server sid" (wake the EE server thread). */
    int (*ee_write)(void *u, uint32_t ee_addr, const void *src, uint32_t size);
    int (*ee_has_server)(void *u, uint32_t sid);
    void (*ee_rpc_posted)(void *u, uint32_t sid);
};
void SetHooks(const Hooks *h);               /* copied; 0 = none */

enum { BACKEND_FIBER = 0, BACKEND_THREAD = 1 };
int SetBackend(int backend);                 /* only while no IOP thread context exists (after Reset); 0 = ok.
                                                Default: fibers on Windows (ACNB_IOPK_BACKEND=thread overrides) */
int Backend();

void Reset();                                /* power-on: RAM zeroed, heap empty, no threads / objects, T = 0 */
uint64_t Now();                              /* T in IOP cycles */
uint64_t MixedSamples();                     /* samples handed to spu_run so far (16 * floor(T / 12288) after a pass) */
void Advance(uint64_t target);               /* run the IOP until T = target (the hybrid's iop_advance(ee) target ee / 8) */
void SpuSync();                              /* run the mixer to T's chunk boundary (sd::Hw::sync; idempotent) */

/* IOP RAM */
uint8_t *Ram();
uint8_t *Ptr(uint32_t addr);                 /* host pointer of an IOP address (KSEG bits ignored); 0 outside RAM */
uint32_t Addr(const void *p);                /* IOP address of a host pointer into Ram(); 0 outside */

/* EE -> IOP SIF DMA (EE sceSifSetDma with an IOP destination); 0 = bad range (nothing copied) */
int EeDmaToIop(const void *src, uint32_t iop_dst, uint32_t size);

/* EE -> IOP SIF RPC (the EE's sceSifCallRpc): the send bytes go into the server's receive buffer, the server thread
 * runs the server function (zero time), its returned buffer (rsize bytes) is copied to recv. 0 = ok, -1 = no
 * server with that sid. A 10 s IOP timeout is logged. Guard 2 of 4b (64 identical calls at one T -> the IOP runs to
 * its next event) is applied. */
int RpcCall(uint32_t sid, uint32_t fno, const void *send, uint32_t ssize, void *recv, uint32_t rsize);
int HaveServer(uint32_t sid);
int WaitServer(uint32_t sid);                /* runs the IOP up to 0.5 s until the server exists (0 if no module) */

/* IOP -> EE RPC requests posted by SifCallRpc, served by the host's EE side (hybrid iop_ee_serve_one) */
struct EeRpcRequest { int id; uint32_t sid, fno, mode, ssize, rsize; const void *send; };
int EeRpcNext(uint32_t sid, EeRpcRequest *out);   /* oldest pending request for EE server sid (slot order); 0 = none.
                                                     The host copies (ssize + 15) & ~15 send bytes to its buffer. */
void EeRpcComplete(int id, const void *reply);    /* reply (rsize bytes, may be 0) -> recv; wakes the caller or queues
                                                     its end function (deferred call) */

/* sysmem: first fit (type 0 = low, 1 = high, 2 = at addr), sizes rounded up to 256 (0 -> 256), as the hybrid.
 * Also the EE's sceSifAllocIopHeap / sceSifFreeIopHeap (type 0). */
enum { ALLOC_FIRST = 0, ALLOC_LAST = 1, ALLOC_ADDRESS = 2 };
uint32_t AllocSysMemory(int type, uint32_t size, uint32_t addr);   /* IOP address, 0 = no memory */
int FreeSysMemory(uint32_t addr);
uint32_t QueryMaxFreeMemSize();
uint32_t QueryTotalFreeMemSize();

/* ---- native modules: phantom images */

/* IRX header facts the hybrid's loader uses (iopmod segment 0x70000080 + the first PT_LOAD) */
struct IrxInfo { char name[32]; uint32_t total, entry, gp, text, data, bss, ld_vaddr, ld_memsz; };
int ParseIrx(const void *elf, uint32_t size, IrxInfo *out);   /* 0 = ok, -1 = not an IRX */

struct ModuleArgs { int index; uint32_t base, size; int argc; char *argv[16]; void *user; };
typedef int (*ModuleStartFn)(const ModuleArgs *m);   /* module_start; return value = the IRX's start result */
/* Hybrid load_elf order: image = AllocSysMemory(0, image_size) (zeroed), argv block = AllocSysMemory(0, 256 + argl +
 * strlen(path)) with the strings in IOP RAM, a start thread (prio 8, 0x4000-byte phantom stack, attr 0x02000000)
 * runs fn until it exits (up to 10 s of IOP time), then its stack is freed. path = the hybrid's module path
 * ("iopmem:%06x" of the EE's load buffer, or the "cdrom0:\..." file name). Returns the module index (>= 0) or -1;
 * *base = the image address, *result = fn's return value. */
int StartModule(const char *name, const char *path, uint32_t image_size, const char *args, int argl, ModuleStartFn fn,
                void *user, uint32_t *base, int *result);
uint32_t ModuleBase(const char *name);        /* 0 = not loaded */

/* ---- thbase */

typedef void (*ThreadEntry)(void *arg);
struct ThreadParam { uint32_t attr, option; ThreadEntry entry; uint32_t stack_size; int priority; };
struct ThreadInfo { uint32_t attr, option; int status; ThreadEntry entry; uint32_t stack, stack_size; int init_prio, prio,
                    wait_type, wait_id, wakeup; };
struct SysClock { uint32_t lo, hi; };
typedef unsigned int (*AlarmHandler)(void *arg);   /* returns the next interval in cycles (0 = stop; < 100 -> 100) */

int CreateThread(const ThreadParam *p);      /* id > 0; the stack is reserved in the IOP heap (high end) as on the IOP */
int DeleteThread(int id);
int StartThread(int id, void *arg);
int ExitThread();                            /* from a thread: never returns */
int TerminateThread(int id);
int ChangeThreadPriority(int id, int prio);  /* id 0 = self, prio 0 = initial priority */
int ReleaseWaitThread(int id);
int GetThreadId();
int ReferThreadStatus(int id, ThreadInfo *info);
int SleepThread();
int WakeupThread(int id);
int iWakeupThread(int id);
int CancelWakeupThread(int id);
int iCancelWakeupThread(int id);
int DelayThread(uint32_t usec);
int GetSystemTime(SysClock *c);
void USec2SysClock(uint32_t usec, SysClock *c);
void SysClock2USec(const SysClock *c, uint32_t *sec, uint32_t *usec);
int SetAlarm(const SysClock *c, AlarmHandler fn, void *arg);
int iSetAlarm(const SysClock *c, AlarmHandler fn, void *arg);
int CancelAlarm(AlarmHandler fn, void *arg);
int iCancelAlarm(AlarmHandler fn, void *arg);
/* the native form of a hardware / time poll loop: from a thread, T moves to the next event (capped at the host
 * entry's target; T + 1 if none is later), the service pass runs, the thread keeps its place in the ready order */
void WaitNextEvent();

/* ---- thsemap / thevent */

struct SemaParam { uint32_t attr, option; int init_count, max_count; };
struct SemaInfo { uint32_t attr, option; int init_count, max_count, current_count, num_wait_threads; };
int CreateSema(const SemaParam *p);
int DeleteSema(int id);
int SignalSema(int id);
int iSignalSema(int id);
int WaitSema(int id);
int PollSema(int id);
int ReferSemaStatus(int id, SemaInfo *info);

struct EventFlagParam { uint32_t attr, option, init_pattern; };
int CreateEventFlag(const EventFlagParam *p);
int DeleteEventFlag(int id);
int SetEventFlag(int id, uint32_t bits);
int iSetEventFlag(int id, uint32_t bits);
int ClearEventFlag(int id, uint32_t bits);   /* bits &= pattern */
int iClearEventFlag(int id, uint32_t bits);
int WaitEventFlag(int id, uint32_t bits, int mode, uint32_t *res);
int PollEventFlag(int id, uint32_t bits, int mode, uint32_t *res);

/* ---- intrman */

typedef int (*IntrHandler)(void *arg);
int RegisterIntrHandler(int irq, int mode, IntrHandler fn, void *arg);
int ReleaseIntrHandler(int irq);
int EnableIntr(int irq);
int DisableIntr(int irq, int *res);
int CpuSuspendIntr(int *state);
int CpuResumeIntr(int state);
int QueryIntrContext();
void RaiseIrq(int irq);                      /* hardware side (host / mixer events): pending until serviced */
void DeferCall(void (*fn)(void *), void *arg);   /* queue a deferred call (runs after the IRQs of a service pass) */

/* ---- timrman */

typedef unsigned int (*TimerHandler)(void *arg);   /* returns the next compare value (0 = stop the timer) */
int AllocHardTimer(int source, int size, int prescale);
int FreeHardTimer(int id);
int SetTimerHandler(int id, uint32_t compare, TimerHandler fn, void *arg);
int SetupHardTimer(int id, int source, int mode, int prescale);
int StartHardTimer(int id);
int StopHardTimer(int id);
uint32_t GetTimerCounter(int id);

/* ---- sifman (IOP -> EE DMA) */

struct SifDmaData { const void *src; uint32_t dest; uint32_t size; uint32_t attr; };   /* src: host pointer (IOP side) */
uint32_t SifSetDma(const SifDmaData *d, int n);   /* queue id (never 0); copies at once */
int SifDmaStat(uint32_t id);                      /* < 0: done (always) */
uint32_t SifSetDmaIntr(const SifDmaData *d, int n, void (*fn)(void *), void *arg);   /* + deferred fn(arg) */

/* ---- sifcmd: RPC servers (module-owned storage, registered by pointer) and IOP -> EE clients */

typedef void *(*RpcFunc)(int fno, void *buf, int size);   /* returns the reply buffer (host pointer) */
struct RpcQueue { int thread_id; };
struct RpcServer { uint32_t sid; RpcFunc func; void *buf; RpcFunc cfunc; void *cbuf; RpcQueue *qd; };
struct RpcClient { uint32_t sid; uint32_t server; };     /* server != 0 once bound (hybrid: 0x2000 + (sid & 0xff) * 16) */
void SifInitRpc(int mode);
int SifSetRpcQueue(RpcQueue *qd, int thread_id);
int SifRegisterRpc(RpcServer *sd, uint32_t sid, RpcFunc func, void *buf, RpcFunc cfunc, void *cbuf, RpcQueue *qd);
void SifRpcLoop(RpcQueue *qd);               /* serves requests forever (returns only outside a thread) */
int SifBindRpc(RpcClient *cd, uint32_t sid, int mode);
int SifCallRpc(RpcClient *cd, uint32_t fno, uint32_t mode, const void *send, uint32_t ssize, void *recv, uint32_t rsize,
               void (*end)(void *), void *end_arg);   /* mode bit 0 = no wait */
int SifCheckStatRpc(RpcClient *cd);

/* ---- cdvdman / ioman (port/platform ps2_file: synchronous, as the hybrid) */

struct CdlFILE { uint32_t lsn, size; char name[16]; uint8_t date[8]; };
int CdInit(int mode);
int CdRead(uint32_t lsn, uint32_t sectors, void *buf, const void *mode);   /* 1 = ok; + deferred callback(1) */
int CdSync(int mode);
int CdSeek(uint32_t lsn);
int CdGetError();
int CdSearchFile(CdlFILE *fp, const char *name);
int CdDiskReady(int mode);
int CdStatus();
void (*CdCallback(void (*fn)(int)))(int);    /* returns the old callback */
int CdBreak();
int Open(const char *name, int flags);
int Close(int fd);
int Read(int fd, void *buf, int n);
int Lseek(int fd, int off, int whence);
int Ioctl(int fd, int cmd, void *arg);

/* ---- loadcore / stdio */
void FlushDcache();
int Printf(const char *fmt, ...);           /* printf / Kprintf: line-buffered to plat_log "[iop] ..." */

/* ---- adapters */
const sd::Kernel *SdKernel();                /* sd's kernel interface bound to iopk */
void SdHwSync(void *u);                      /* sd::Hw::sync = SpuSync */

}  // namespace iopk

/* --iopk-selftest: unit tests of iopk vs the iop_kern.c semantics (both thread backends) + the phantom-image
 * allocation replay of the hybrid's module loads. 0 = all passed. */
extern "C" int iopk_selftest(void);
