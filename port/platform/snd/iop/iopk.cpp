/* iopk: native IOP kernel services (phase 3 step 5 NI4). Semantics copied from port/runtime/iop_kern.c in its
 * zero-cost mode; see iopk.h for the model. Section order follows iop_kern.c:
 * sysmem, threads + scheduler, events (alarms / hard timers / IRQs / deferred calls), thread HLE, semaphores / event
 * flags, intrman / timrman, cdvdman / ioman, SIF DMA, SIF RPC, modules, adapters. */
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "iopk.h"
#include "sd.h"
#include "../../plat.h"
#include "../../ps2_file.h"
#ifdef _WIN32
#include <process.h>
#else
#include <pthread.h>
#endif
#include <stdarg.h>
#if defined(__SSE__) || defined(_M_IX86) || defined(_M_X64)
#include <xmmintrin.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace iopk {
namespace {

constexpr int MAX_BLK = 512, MAX_TH = 64, MAX_AL = 32, MAX_HT = 8, MAX_DEF = 64, MAX_SEM = 128, MAX_EVF = 64,
              MAX_SRV = 32, MAX_EREQ = 16, MAX_MOD = 32;
constexpr int Z_POLL = 64;                 /* guard 2: identical EE RPCs at one T */

/* ---------------------------------------------------------------- thread contexts (coroutines) */

struct Thread;
struct Ctx {
    Thread *owner;                         /* 0 once the IOP thread slot let go of it (graveyard) */
    void *jb[5];                           /* __builtin_setjmp buffer at the coroutine base */
    volatile int kill, destroy, finished;  /* kill: restart the entry when resumed; destroy: unwind and end */
    void *fiber;
    int has_th;                            /* thread backend: a host thread exists */
    unsigned mxcsr;                        /* thread backend: the creator's SSE control state, set on the new thread */
#ifdef _WIN32
    HANDLE th;
#else
    pthread_t th;
#endif
    Ctx *next;
};

struct Thread {
    int used, status, prio, init_prio;
    ThreadEntry entry;
    void *arg;
    uint32_t stack, stack_size, attr, option;
    int wait_type, wait_id, wakeup, wake_ret, poll, rpc_claimed;
    uint64_t seq, wake_at;
    uint32_t ev_bits, ev_mode;
    uint32_t *ev_res;
    RpcQueue *rpc_qd;
    Ctx *ctx;
};

struct Blk { uint32_t addr, size; int used; };
struct Alarm { int used; uint64_t when; AlarmHandler fn; void *arg; };
struct HTimer { int used, running; uint32_t source, prescale, mode, compare; TimerHandler fn; void *arg; uint64_t t0, next; };
struct Deferred { int kind; void (*vp)(void *); void (*vi)(int); void *arg; int iarg; };
struct IHandler { IntrHandler fn; void *arg; int used, enabled, mode; };
struct Sema { int used, count, max; uint32_t attr, option; int init; };
struct Evf { int used; uint32_t bits, attr, option; };
struct EeReq { int used; uint32_t sid, fno, mode, ssize, rsize; const void *send; void *recv; void (*endf)(void *); void *endp;
               RpcClient *cd; int tid; };
struct ModRec { char name[32]; uint32_t base, size; };

struct State {
    uint64_t T, spu_next, mixed, seq_ctr, target;
    Blk blk[MAX_BLK];
    int nblk;
    Thread th[MAX_TH];
    int cur, in_intr, intr_off, yield_req, in_loop;
    Alarm al[MAX_AL];
    HTimer ht[MAX_HT];
    Deferred defq[MAX_DEF];
    int ndef;
    IHandler ih[64];
    uint64_t irq_pending;
    Sema sem[MAX_SEM];
    Evf evf[MAX_EVF];
    RpcServer *srv[MAX_SRV];
    int nsrv;
    struct { int active, started, done; RpcServer *s; uint32_t fno, size; void *result; } req;
    EeReq ereq[MAX_EREQ];
    ModRec mods[MAX_MOD];
    int nmods;
    uint32_t sifdma_id;
    void (*cd_cb)(int);
    uint32_t g2_key[4];
    uint64_t g2_cyc;
    int g2_same;
    uint32_t wait_sid;
    int start_tid;
    char tty[1024];
    int ntty;
};

uint8_t g_ram[RAM_SIZE];
State g;
Hooks hk;
int g_backend = -1;
Ctx *g_grave;                              /* contexts to unwind and free (reap) */
Ctx *g_running;                            /* context executing now (0 = the host) */

/* ---------------------------------------------------------------- sysmem (iop_kern.c blk_*, sys_alloc) */

void blk_init() { g.nblk = 1; g.blk[0] = Blk{HEAP_START, RAM_SIZE - HEAP_START, 0}; }

void blk_split(int i, uint32_t off, uint32_t size)
{
    Blk b = g.blk[i];
    int ins = (off ? 1 : 0) + (b.size - off - size ? 1 : 0);
    if (g.nblk + ins > MAX_BLK) return;
    memmove(&g.blk[i + 1 + ins], &g.blk[i + 1], (g.nblk - i - 1) * sizeof(Blk));
    g.nblk += ins;
    int k = i;
    if (off) g.blk[k++] = Blk{b.addr, off, 0};
    g.blk[k++] = Blk{b.addr + off, size, 1};
    if (b.size - off - size) g.blk[k] = Blk{b.addr + off + size, b.size - off - size, 0};
}

uint32_t sys_alloc(int type, uint32_t size, uint32_t addr)
{
    size = (size + 255) & ~255u;
    if (!size) size = 256;
    if (type == ALLOC_ADDRESS) {
        for (int i = 0; i < g.nblk; i++)
            if (!g.blk[i].used && addr >= g.blk[i].addr && addr + size <= g.blk[i].addr + g.blk[i].size) {
                blk_split(i, addr - g.blk[i].addr, size);
                return addr;
            }
        return 0;
    }
    if (type == ALLOC_LAST) {
        for (int i = g.nblk - 1; i >= 0; i--)
            if (!g.blk[i].used && g.blk[i].size >= size) {
                uint32_t a = g.blk[i].addr + g.blk[i].size - size;
                blk_split(i, g.blk[i].size - size, size);
                return a;
            }
        return 0;
    }
    for (int i = 0; i < g.nblk; i++)
        if (!g.blk[i].used && g.blk[i].size >= size) { uint32_t a = g.blk[i].addr; blk_split(i, 0, size); return a; }
    return 0;
}

int sys_free(uint32_t addr)
{
    for (int i = 0; i < g.nblk; i++) {
        if (g.blk[i].addr != addr || !g.blk[i].used) continue;
        g.blk[i].used = 0;
        if (i + 1 < g.nblk && !g.blk[i + 1].used) {
            g.blk[i].size += g.blk[i + 1].size;
            memmove(&g.blk[i + 1], &g.blk[i + 2], (g.nblk - i - 2) * sizeof(Blk));
            g.nblk--;
        }
        if (i > 0 && !g.blk[i - 1].used) {
            g.blk[i - 1].size += g.blk[i].size;
            memmove(&g.blk[i], &g.blk[i + 1], (g.nblk - i - 1) * sizeof(Blk));
            g.nblk--;
        }
        return 0;
    }
    return -1;
}

/* ---------------------------------------------------------------- coroutine backends */

int backend()
{
    if (g_backend < 0) {
        const char *e = getenv("ACNB_IOPK_BACKEND");
#ifdef _WIN32
        g_backend = e && !strcmp(e, "thread") ? BACKEND_THREAD : BACKEND_FIBER;
#else
        (void)e;
        g_backend = BACKEND_THREAD;
#endif
    }
    return g_backend;
}

void thread_exit_cur();

/* the coroutine body: entry, exit, and the restart / unwind point. Runs on the IOP thread's own stack. */
void ctx_body(Ctx *c0)
{
    Ctx *volatile c = c0;
    __builtin_setjmp(c0->jb);
    while (!c->destroy) {
        c->kill = 0;
        Thread *t = c->owner;
        t->entry(t->arg);
        thread_exit_cur();
    }
}

#ifdef _WIN32
void *g_sched_fiber;
void WINAPI fiber_proc(void *p)
{
    Ctx *c = (Ctx *)p;
    ctx_body(c);
    c->finished = 1;
    SwitchToFiber(g_sched_fiber);   /* never resumed: reap deletes the fiber */
}
#endif

/* thread backend: a strict hand-off token (exactly one of host / IOP threads runs) - no C++ runtime (game.dll links
 * without it) */
#ifdef _WIN32
SRWLOCK tk_m = SRWLOCK_INIT;
CONDITION_VARIABLE tk_cv = CONDITION_VARIABLE_INIT;
void tk_lock() { AcquireSRWLockExclusive(&tk_m); }
void tk_unlock() { ReleaseSRWLockExclusive(&tk_m); }
void tk_wait() { SleepConditionVariableSRW(&tk_cv, &tk_m, INFINITE, 0); }
void tk_wake() { WakeAllConditionVariable(&tk_cv); }
#else
pthread_mutex_t tk_m = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t tk_cv = PTHREAD_COND_INITIALIZER;
void tk_lock() { pthread_mutex_lock(&tk_m); }
void tk_unlock() { pthread_mutex_unlock(&tk_m); }
void tk_wait() { pthread_cond_wait(&tk_cv, &tk_m); }
void tk_wake() { pthread_cond_broadcast(&tk_cv); }
#endif
const void *volatile tk_token;
char tk_host;                              /* the host's token */

void tk_pass(const void *to, const void *self)
{
    tk_lock();
    tk_token = to;
    tk_wake();
    while (tk_token != self) tk_wait();
    tk_unlock();
}

void thread_proc(Ctx *c)
{
#if defined(__SSE__) || defined(_M_IX86) || defined(_M_X64)
    _mm_setcsr(c->mxcsr);   /* MXCSR is per host thread: FTZ / DAZ (+ ACNB_FPU_RZ) as the EE side (ps2k_reset) */
#endif
    tk_lock();
    while (tk_token != c) tk_wait();
    tk_unlock();
    ctx_body(c);
    c->finished = 1;
    tk_lock();
    tk_token = &tk_host;
    tk_wake();
    tk_unlock();
}
#ifdef _WIN32
unsigned __stdcall thread_start(void *p) { thread_proc((Ctx *)p); return 0; }
#else
void *thread_start(void *p) { thread_proc((Ctx *)p); return 0; }
#endif

void host_prepare()
{
#ifdef _WIN32
    if (backend() == BACKEND_FIBER) g_sched_fiber = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiberEx(0, 0);
#endif
}

/* host -> coroutine; returns when it gives the CPU back */
void ctx_enter(Ctx *c)
{
    Ctx *prev = g_running;
    g_running = c;
#ifdef _WIN32
    if (backend() == BACKEND_FIBER) {
        /* no FIBER_FLAG_FLOAT_SWITCH: a flagged fiber starts with the default MXCSR 0x1f80 and, switching back to the
         * unflagged EE fiber (hostfiber / runtime/kernel.c), leaves it on the host thread - the EE sim lost FTZ / DAZ
         * (and ACNB_FPU_RZ) that way. All fibers share the host thread's float control state, as on the EE side. */
        if (!c->fiber) c->fiber = CreateFiberEx(64 * 1024, 1024 * 1024, 0, fiber_proc, c);
        if (!c->fiber) { plat_log("iopk: CreateFiberEx failed (%lu)", (unsigned long)GetLastError()); c->finished = 1; }
        else SwitchToFiber(c->fiber);
        g_running = prev;
        return;
    }
#endif
    if (!c->has_th) {
#if defined(__SSE__) || defined(_M_IX86) || defined(_M_X64)
        c->mxcsr = _mm_getcsr() & ~0x3fu;   /* control bits only, no sticky flags */
#endif
#ifdef _WIN32
        c->th = (HANDLE)_beginthreadex(0, 1024 * 1024, thread_start, c, 0, 0);
        c->has_th = c->th != 0;
#else
        pthread_attr_t a;
        pthread_attr_init(&a);
        pthread_attr_setstacksize(&a, 1024 * 1024);
        c->has_th = pthread_create(&c->th, &a, thread_start, c) == 0;
        pthread_attr_destroy(&a);
#endif
        if (!c->has_th) { plat_log("iopk: cannot create a host thread"); c->finished = 1; g_running = prev; return; }
    }
    tk_pass(c, &tk_host);
    g_running = prev;
}

/* coroutine -> host; returns when the scheduler picks it again (or unwinds for a restart / destroy) */
void slice_end()
{
    Ctx *c = g_running;
#ifdef _WIN32
    if (backend() == BACKEND_FIBER) SwitchToFiber(g_sched_fiber);
    else
#endif
        tk_pass(&tk_host, c);
    if (c->kill || c->destroy) __builtin_longjmp(c->jb, 1);
}

void bury(Ctx *c)
{
    if (!c) return;
    c->owner = 0;
    c->next = g_grave;
    g_grave = c;
}

void reap()
{
    if (!g_grave) return;
    host_prepare();
    while (g_grave) {
        Ctx *c = g_grave;
        g_grave = c->next;
        if ((c->fiber || c->has_th) && !c->finished) { c->destroy = 1; ctx_enter(c); }
#ifdef _WIN32
        if (c->fiber) DeleteFiber(c->fiber);
#endif
        if (c->has_th) {
#ifdef _WIN32
            WaitForSingleObject(c->th, INFINITE);
            CloseHandle(c->th);
#else
            pthread_join(c->th, 0);
#endif
        }
        free(c);
    }
}

/* ---------------------------------------------------------------- scheduler (iop_kern.c pick / make_ready / block / run_until) */

Thread *TH(int id) { return id > 0 && id < MAX_TH && g.th[id].used ? &g.th[id] : 0; }
Thread *curth() { return g.cur && !g.in_intr ? &g.th[g.cur] : 0; }

int pick()
{
    int b = 0;
    for (int i = 1; i < MAX_TH; i++) {
        Thread *t = &g.th[i];
        if (!t->used || (t->status != TS_READY && t->status != TS_RUN)) continue;
        if (!b || t->prio < g.th[b].prio || (t->prio == g.th[b].prio && t->seq < g.th[b].seq)) b = i;
    }
    return b;
}

void make_ready(Thread *t, int v)
{
    t->status = TS_READY;
    t->wait_type = WT_NONE;
    t->wake_ret = v;
    t->seq = ++g.seq_ctr;
    if (!g.in_intr && g.cur && t->prio < g.th[g.cur].prio) g.yield_req = 1;   /* preempt at the end of the call */
}

/* end of a kernel call made from a thread: the hybrid's iop_break after a preempting wake-up */
void slice_point()
{
    if (g.yield_req && curth()) { g.yield_req = 0; slice_end(); }
}

const char *const wtname[] = {"-", "sleep", "delay", "sema", "evf", "rpcloop", "eerpc"};

/* park the current thread; 1 = it blocked and was woken (wake_ret holds the result), 0 = not a thread context */
int block(int wt, int id)
{
    Thread *t = curth();
    if (!t) { plat_log("iopk: blocking call (%s) outside an IOP thread ignored", wtname[wt]); return 0; }
    t->status = TS_WAIT;
    t->wait_type = wt;
    t->wait_id = id;
    t->wake_ret = 0;
    slice_end();
    return 1;
}

void thread_exit_cur()
{
    Thread *t = &g.th[g.cur];
    if (g.cur) t->status = TS_DORMANT;
    slice_end();
    __builtin_longjmp(g_running->jb, 1);   /* not reached: a dormant thread is resumed only to restart / unwind */
}

void call_deferred(const Deferred &d)
{
    if (d.kind) d.vi(d.iarg);
    else d.vp(d.arg);
}

void defer(const Deferred &d)
{
    if (g.ndef < MAX_DEF) g.defq[g.ndef++] = d;
    else plat_log("iopk: deferred call queue full");
}

uint64_t next_event()
{
    uint64_t n = g.spu_next;
    if (hk.spu_next_event) { uint64_t s = hk.spu_next_event(hk.user); if (s < n) n = s; }
    for (int i = 0; i < MAX_AL; i++) if (g.al[i].used && g.al[i].when < n) n = g.al[i].when;
    for (int i = 0; i < MAX_HT; i++) if (g.ht[i].used && g.ht[i].running && g.ht[i].fn && g.ht[i].next < n) n = g.ht[i].next;
    for (int i = 1; i < MAX_TH; i++)
        if (g.th[i].used && g.th[i].status == TS_WAIT && g.th[i].wait_type == WT_DELAY && g.th[i].wake_at < n) n = g.th[i].wake_at;
    return n;
}

uint64_t ht_div(const HTimer *t) { return t->prescale ? t->prescale : 1; }

void spu_catch_up()
{
    while (g.T >= g.spu_next) {
        if (hk.spu_run) hk.spu_run(hk.user, CHUNK_SAMPLES);
        g.mixed += CHUNK_SAMPLES;
        g.spu_next += CHUNK_CYC;
    }
}

void service()
{
    spu_catch_up();
    if (hk.spu_tick) hk.spu_tick(hk.user);
    for (int i = 1; i < MAX_TH; i++) {
        Thread *t = &g.th[i];
        if (t->used && t->status == TS_WAIT && t->wait_type == WT_DELAY && g.T >= t->wake_at) make_ready(t, 0);
    }
    if (g.intr_off) return;
    for (int i = 0; i < MAX_AL; i++) {
        Alarm *a = &g.al[i];
        if (!a->used || g.T < a->when) continue;
        g.in_intr++;
        uint32_t next = a->fn(a->arg);
        g.in_intr--;
        if (next) a->when += next < 100 ? 100 : next;
        else a->used = 0;
    }
    for (int i = 0; i < MAX_HT; i++) {
        HTimer *t = &g.ht[i];
        if (!t->used || !t->running || !t->fn || g.T < t->next) continue;
        g.in_intr++;
        uint32_t nc = t->fn(t->arg);
        g.in_intr--;
        if (nc) { t->compare = nc; t->t0 = t->next; t->next += (uint64_t)nc * ht_div(t); }
        else t->running = 0;
    }
    while (g.irq_pending) {
        uint64_t p = g.irq_pending;
        g.irq_pending = 0;
        for (int irq = 0; irq < 64; irq++) {
            if (!(p & (1ull << irq))) continue;
            IHandler *h = &g.ih[irq];
            if (!h->used || !h->enabled) { g.irq_pending |= 1ull << irq; continue; }
            g.in_intr++;
            h->fn(h->arg);
            g.in_intr--;
        }
        if (g.irq_pending == p) break;   /* only masked ones left */
    }
    while (g.ndef) {
        Deferred d = g.defq[0];
        memmove(g.defq, g.defq + 1, --g.ndef * sizeof *g.defq);
        g.in_intr++;
        call_deferred(d);
        g.in_intr--;
    }
}

void run_thread(int id)
{
    Thread *t = &g.th[id];
    g.cur = id;
    t->status = TS_RUN;
    g.yield_req = 0;
    if (!t->ctx) { t->ctx = (Ctx *)calloc(1, sizeof(Ctx)); t->ctx->owner = t; }
    ctx_enter(t->ctx);
}

void run_until(uint64_t target, int (*done)())
{
    if (g.in_loop || g_running) { plat_log("iopk: host entry from inside the IOP ignored"); return; }
    host_prepare();
    g.in_loop = 1;
    g.target = target;
    while (g.T < target) {
        reap();
        service();
        if (done && done()) break;
        int id = pick();
        uint64_t ne = next_event();
        if (ne > target) ne = target;
        if (!id) { g.T = ne > g.T ? ne : g.T + 1; continue; }
        Thread *t = &g.th[id];
        run_thread(id);
        if (t->poll) {   /* WaitNextEvent = guard 1: T to the next event, the thread keeps its place */
            t->poll = 0;
            ne = next_event();
            if (ne > target) ne = target;
            g.T = ne > g.T ? ne : g.T + 1;
        }
        if (t->status == TS_RUN) t->status = TS_READY;
        g.cur = 0;
    }
    reap();
    g.in_loop = 0;
}

int new_thread(ThreadEntry entry, uint32_t stack_size, int prio, uint32_t attr, uint32_t option)
{
    int id;
    for (id = 1; id < MAX_TH && g.th[id].used; id++) {}
    if (id >= MAX_TH) return -1;
    Thread *t = &g.th[id];
    bury(t->ctx);
    memset(t, 0, sizeof *t);
    stack_size = (stack_size + 255) & ~255u;
    t->stack = sys_alloc(ALLOC_LAST, stack_size, 0);   /* phantom stack: the IOP heap layout of the hybrid */
    if (!t->stack) return KE_NO_MEMORY;
    t->used = 1;
    t->status = TS_DORMANT;
    t->entry = entry;
    t->stack_size = stack_size;
    t->prio = t->init_prio = prio;
    t->attr = attr;
    t->option = option;
    return id;
}

void start_thread(Thread *t, void *arg)
{
    t->arg = arg;
    t->prio = t->init_prio;
    t->wakeup = 0;
    t->poll = 0;
    if (t->ctx) t->ctx->kill = 1;   /* parked at its exit or terminated mid-function: restart from the entry */
    make_ready(t, 0);
}

void free_thread(Thread *t)
{
    bury(t->ctx);
    t->ctx = 0;
    t->used = 0;
}

Thread *first_waiter(int wt, int id)
{
    Thread *b = 0;
    for (int i = 1; i < MAX_TH; i++) {
        Thread *t = &g.th[i];
        if (t->used && t->status == TS_WAIT && t->wait_type == wt && t->wait_id == id && (!b || t->seq < b->seq)) b = t;
    }
    return b;
}

int evf_ok(uint32_t bits, uint32_t want, uint32_t mode) { return (mode & EW_OR) ? (bits & want) != 0 : (bits & want) == want; }

void evf_check_waiters(int id)
{
    for (int i = 1; i < MAX_TH; i++) {
        Thread *t = &g.th[i];
        if (!t->used || t->status != TS_WAIT || t->wait_type != WT_EVENT || t->wait_id != id) continue;
        if (!evf_ok(g.evf[id].bits, t->ev_bits, t->ev_mode)) continue;
        if (t->ev_res) *t->ev_res = g.evf[id].bits;
        if (t->ev_mode & EW_CLEAR) g.evf[id].bits &= ~t->ev_bits;
        make_ready(t, 0);
    }
}

int sema_ok(int id) { return id > 0 && id < MAX_SEM && g.sem[id].used; }
int evf_valid(int id) { return id > 0 && id < MAX_EVF && g.evf[id].used; }
HTimer *HT(int id) { return id > 0 && id < MAX_HT && g.ht[id].used ? &g.ht[id] : 0; }

/* RPC */
int try_claim(RpcQueue *qd)
{
    if (!g.req.active || g.req.started || g.req.s->qd != qd) return 0;
    g.req.started = 1;
    return 1;
}
void rpc_wake()
{
    for (int i = 1; i < MAX_TH; i++) {
        Thread *t = &g.th[i];
        if (t->used && t->status == TS_WAIT && t->wait_type == WT_RPC && try_claim(t->rpc_qd)) {
            t->rpc_claimed = 1;
            make_ready(t, 0);
            return;
        }
    }
}
int req_done() { return g.req.done; }
int server_up() { return HaveServer(g.wait_sid); }
int start_done() { Thread *t = TH(g.start_tid); return !t || t->status == TS_DORMANT; }

void tty_out(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n' || g.ntty >= (int)sizeof g.tty - 1) {
            g.tty[g.ntty] = 0;
            if (g.ntty) plat_log("[iop] %s", g.tty);
            g.ntty = 0;
        }
        if (*s != '\n' && *s != '\r') g.tty[g.ntty++] = *s;
    }
}

void dump_threads()
{
    plat_log("iopk: threads at T %llu (cur %d, intr %d/%d, irq pending %llx)", (unsigned long long)g.T, g.cur, g.in_intr, g.intr_off,
             (unsigned long long)g.irq_pending);
    for (int i = 1; i < MAX_TH; i++) {
        Thread *t = &g.th[i];
        if (t->used) plat_log("  %2d st %2d prio %3d wait %s/%d", i, t->status, t->prio, wtname[t->wait_type], t->wait_id);
    }
}

struct StartCtx { ModuleStartFn fn; ModuleArgs args; int result; };
void start_entry(void *p)
{
    StartCtx *s = (StartCtx *)p;
    s->result = s->fn(&s->args);
}

}  // namespace

/* ---------------------------------------------------------------- host side */

void SetHooks(const Hooks *h)
{
    if (h) hk = *h;
    else memset(&hk, 0, sizeof hk);
}

int SetBackend(int b)
{
    for (int i = 1; i < MAX_TH; i++) if (g.th[i].ctx) return -1;
    if (g_grave) return -1;
#ifndef _WIN32
    if (b == BACKEND_FIBER) return -1;
#endif
    if (b != BACKEND_FIBER && b != BACKEND_THREAD) return -1;
    g_backend = b;
    return 0;
}
int Backend() { return backend(); }

void Reset()
{
    if (g.in_loop || g_running) { plat_log("iopk: Reset from inside the IOP ignored"); return; }
    for (int i = 1; i < MAX_TH; i++) { bury(g.th[i].ctx); g.th[i].ctx = 0; }
    reap();
    memset(&g, 0, sizeof g);
    memset(g_ram, 0, sizeof g_ram);
    blk_init();
    g.spu_next = CHUNK_CYC;
    g.g2_cyc = ~0ull;
}

uint64_t Now() { return g.T; }
uint64_t MixedSamples() { return g.mixed; }
void Advance(uint64_t target) { if (target > g.T) run_until(target, 0); }
void SpuSync() { spu_catch_up(); }

uint8_t *Ram() { return g_ram; }
uint8_t *Ptr(uint32_t addr)
{
    addr &= 0x1fffffffu;
    return addr < RAM_SIZE ? g_ram + addr : 0;
}
uint32_t Addr(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return b >= g_ram && b < g_ram + RAM_SIZE ? (uint32_t)(b - g_ram) : 0;
}

int EeDmaToIop(const void *src, uint32_t iop_dst, uint32_t size)
{
    uint32_t a = iop_dst & 0x1fffffffu;
    if (!src || a + (uint64_t)size > RAM_SIZE) { plat_log("iopk: EE->IOP dma -> %08x (%x) bad range", iop_dst, size); return 0; }
    memcpy(g_ram + a, src, size);
    return 1;
}

int HaveServer(uint32_t sid)
{
    for (int i = 0; i < g.nsrv; i++) if (g.srv[i]->sid == sid) return 1;
    return 0;
}

int WaitServer(uint32_t sid)
{
    if (HaveServer(sid)) return 1;
    if (!g.nmods) return 0;
    g.wait_sid = sid;
    run_until(g.T + HZ / 2, server_up);
    return HaveServer(sid);
}

int RpcCall(uint32_t sid, uint32_t fno, const void *send, uint32_t ssize, void *recv, uint32_t rsize)
{
    RpcServer *s = 0;
    for (int i = 0; i < g.nsrv; i++) if (g.srv[i]->sid == sid) s = g.srv[i];
    if (!s) return -1;
    if (ssize && send && s->buf) {
        uint32_t a = Addr(s->buf);
        if (a && a + (uint64_t)ssize <= RAM_SIZE) memcpy(s->buf, send, ssize);
        else plat_log("iopk: RPC %08x: receive buffer outside IOP RAM", sid);
    }
    g.req.active = 1; g.req.started = 0; g.req.done = 0; g.req.s = s; g.req.fno = fno; g.req.size = ssize; g.req.result = 0;
    rpc_wake();
    run_until(g.T + HZ * 10, req_done);
    if (!g.req.done) { plat_log("iopk: RPC %08x fno %x timed out", sid, fno); dump_threads(); }
    g.req.active = 0;
    /* guard 2 (EE busy-poll), iop_kern.c iop_rpc_call */
    uint32_t k[4] = {sid, fno, ssize && s->buf ? disc_crc32(s->buf, ssize) : 0,
                     rsize && g.req.result ? disc_crc32(g.req.result, rsize) : 0};
    g.g2_same = g.T == g.g2_cyc && !memcmp(k, g.g2_key, sizeof k) ? g.g2_same + 1 : 0;
    memcpy(g.g2_key, k, sizeof k);
    g.g2_cyc = g.T;
    if (g.g2_same >= Z_POLL) {
        uint64_t ne = next_event();
        run_until(ne > g.T ? ne : g.T + 1, 0);
        g.g2_same = 0;
        g.g2_cyc = g.T;
    }
    if (rsize && recv && g.req.result) memcpy(recv, g.req.result, rsize);
    return 0;
}

int EeRpcNext(uint32_t sid, EeRpcRequest *out)
{
    for (int k = 0; k < MAX_EREQ; k++) {
        EeReq *r = &g.ereq[k];
        if (!r->used || r->sid != sid) continue;
        if (out) *out = EeRpcRequest{k, r->sid, r->fno, r->mode, r->ssize, r->rsize, r->send};
        return 1;
    }
    return 0;
}

void EeRpcComplete(int id, const void *reply)
{
    if (id < 0 || id >= MAX_EREQ || !g.ereq[id].used) return;
    EeReq *r = &g.ereq[id];
    if (r->rsize && r->recv && reply) memcpy(r->recv, reply, r->rsize);
    r->used = 0;
    Thread *t = TH(r->tid);
    if (!(r->mode & 1) && t && t->status == TS_WAIT && t->wait_type == WT_EERPC && t->wait_id == id) make_ready(t, 0);
    else if (r->endf) defer(Deferred{0, r->endf, 0, r->endp, 0});
}

uint32_t AllocSysMemory(int type, uint32_t size, uint32_t addr)
{
    uint32_t a = sys_alloc(type, size, addr);
    if (!a) plat_log("iopk: AllocSysMemory(%d, %x) failed (max free %x)", type, size, QueryMaxFreeMemSize());
    return a;
}
int FreeSysMemory(uint32_t addr) { return sys_free(addr); }
uint32_t QueryMaxFreeMemSize()
{
    uint32_t m = 0;
    for (int i = 0; i < g.nblk; i++) if (!g.blk[i].used && g.blk[i].size > m) m = g.blk[i].size;
    return m;
}
uint32_t QueryTotalFreeMemSize()
{
    uint32_t s = 0;
    for (int i = 0; i < g.nblk; i++) if (!g.blk[i].used) s += g.blk[i].size;
    return s;
}

/* ---------------------------------------------------------------- modules (iop_kern.c load_elf, minus code) */

int ParseIrx(const void *elf, uint32_t esize, IrxInfo *out)
{
    const uint8_t *e = (const uint8_t *)elf;
    if (!e || esize < 0x34 || memcmp(e, "\x7f" "ELF", 4)) return -1;
    uint32_t phoff, shoff_unused;
    uint16_t phentsize, phnum;
    memcpy(&phoff, e + 0x1c, 4);
    memcpy(&shoff_unused, e + 0x20, 4);
    memcpy(&phentsize, e + 0x2a, 2);
    memcpy(&phnum, e + 0x2c, 2);
    IrxInfo r;
    memset(&r, 0, sizeof r);
    int have_mod = 0, have_ld = 0;
    for (int i = 0; i < phnum; i++) {
        uint64_t po = phoff + (uint64_t)i * phentsize;
        if (po + 0x20 > esize) return -1;
        const uint8_t *ph = e + po;
        uint32_t type, off;
        memcpy(&type, ph, 4);
        memcpy(&off, ph + 4, 4);
        if (type == 0x70000080u) {
            if ((uint64_t)off + 0x1a + 1 > esize) return -1;
            const uint8_t *m = e + off;
            memcpy(&r.entry, m + 4, 4); memcpy(&r.gp, m + 8, 4); memcpy(&r.text, m + 12, 4);
            memcpy(&r.data, m + 16, 4); memcpy(&r.bss, m + 20, 4);
            size_t n = 0;
            while (n < sizeof r.name - 1 && off + 0x1a + n < esize && m[0x1a + n]) { r.name[n] = (char)m[0x1a + n]; n++; }
            have_mod = 1;
        } else if (type == 1 && !have_ld) {
            memcpy(&r.ld_vaddr, ph + 8, 4);
            memcpy(&r.ld_memsz, ph + 20, 4);
            have_ld = r.ld_memsz != 0;
        }
    }
    if (!have_mod) return -1;
    r.total = r.text + r.data + r.bss;
    if (r.ld_vaddr + r.ld_memsz > r.total) r.total = r.ld_vaddr + r.ld_memsz;
    if (out) *out = r;
    return 0;
}

int StartModule(const char *name, const char *path, uint32_t image_size, const char *args, int argl, ModuleStartFn fn,
                void *user, uint32_t *base_out, int *result)
{
    if (g.in_loop || g_running) { plat_log("iopk: StartModule from inside the IOP ignored"); return -1; }
    uint32_t base = sys_alloc(ALLOC_FIRST, image_size, 0);
    if (!base) { plat_log("iopk: no memory for %s (%x)", path, image_size); return -1; }
    memset(g_ram + base, 0, image_size);
    int index = g.nmods;
    if (g.nmods < MAX_MOD) {
        ModRec *m = &g.mods[g.nmods++];
        snprintf(m->name, sizeof m->name, "%s", name ? name : path);
        m->base = base;
        m->size = image_size;
    }
    StartCtx sc;
    memset(&sc, 0, sizeof sc);
    sc.fn = fn;
    sc.args.index = index;
    sc.args.base = base;
    sc.args.size = image_size;
    sc.args.user = user;
    /* argv block as the hybrid: pointer words at av, strings from av + 64 */
    uint32_t plen = (uint32_t)strlen(path);
    uint32_t av = sys_alloc(ALLOC_FIRST, 256 + (uint32_t)argl + plen, 0);
    uint32_t strp = av + 64, argc = 0;
    memcpy(g_ram + strp, path, plen + 1);
    memcpy(g_ram + av, &strp, 4);
    sc.args.argv[0] = (char *)g_ram + strp;
    argc = 1;
    strp += plen + 1;
    for (int o = 0; args && o < argl && argc < 15;) {
        int l = (int)strnlen(args + o, argl - o);
        memcpy(g_ram + strp, args + o, l);
        g_ram[strp + l] = 0;
        memcpy(g_ram + av + argc * 4, &strp, 4);
        sc.args.argv[argc] = (char *)g_ram + strp;
        argc++;
        strp += l + 1;
        o += l + 1;
    }
    memset(g_ram + av + argc * 4, 0, 4);
    sc.args.argc = (int)argc;
    int tid = new_thread(start_entry, 0x4000, 8, 0x02000000, 0);
    Thread *t = TH(tid);
    if (!t) return -1;
    start_thread(t, &sc);
    g.start_tid = tid;
    run_until(g.T + HZ * 10, start_done);
    if (t->status != TS_DORMANT) plat_log("iopk: %s: module_start did not finish", path);
    sys_free(t->stack);
    free_thread(t);
    reap();
    if (base_out) *base_out = base;
    if (result) *result = sc.result;
    return index;
}

uint32_t ModuleBase(const char *name)
{
    for (int i = g.nmods - 1; i >= 0; i--) if (!strcmp(g.mods[i].name, name)) return g.mods[i].base;
    return 0;
}

/* ---------------------------------------------------------------- thbase */

int CreateThread(const ThreadParam *p) { return new_thread(p->entry, p->stack_size, p->priority, p->attr, p->option); }

int DeleteThread(int id)
{
    Thread *t = TH(id);
    if (!t) return KE_UNKNOWN_THID;
    sys_free(t->stack);
    free_thread(t);
    return 0;
}

int StartThread(int id, void *arg)
{
    Thread *t = TH(id);
    if (!t) return KE_UNKNOWN_THID;
    start_thread(t, arg);
    slice_point();
    return 0;
}

int ExitThread()
{
    if (!curth()) return KE_ERROR;
    thread_exit_cur();
    return 0;
}

int TerminateThread(int id)
{
    Thread *t = TH(id);
    if (!t) return KE_UNKNOWN_THID;
    t->status = TS_DORMANT;
    return 0;
}

int ChangeThreadPriority(int id, int prio)
{
    Thread *t = id ? TH(id) : (g.cur ? &g.th[g.cur] : 0);
    if (!t) return KE_UNKNOWN_THID;
    if (prio == 0) prio = t->init_prio;
    t->prio = prio;
    t->seq = ++g.seq_ctr;
    if (curth()) { g.yield_req = 0; slice_end(); }   /* always a slice boundary */
    return 0;
}

int ReleaseWaitThread(int id)
{
    Thread *t = TH(id);
    if (!t) return KE_UNKNOWN_THID;
    if (t->status != TS_WAIT) return KE_NOT_WAIT;
    make_ready(t, KE_RELEASE_WAIT);
    slice_point();
    return 0;
}

int GetThreadId() { return g.cur; }

int ReferThreadStatus(int id, ThreadInfo *info)
{
    Thread *t = id ? TH(id) : (g.cur ? &g.th[g.cur] : 0);
    if (!t) return KE_UNKNOWN_THID;
    if (info) *info = ThreadInfo{t->attr, t->option, t->status, t->entry, t->stack, t->stack_size, t->init_prio, t->prio,
                                 t->wait_type, t->wait_id, t->wakeup};
    return 0;
}

int SleepThread()
{
    if (!g.cur) return 0;
    Thread *t = &g.th[g.cur];
    if (t->wakeup > 0) { t->wakeup--; return 0; }
    return block(WT_SLEEP, 0) ? t->wake_ret : 0;
}

int WakeupThread(int id)
{
    Thread *t = TH(id);
    if (!t) return KE_UNKNOWN_THID;
    if (t->status == TS_WAIT && t->wait_type == WT_SLEEP) make_ready(t, 0);
    else t->wakeup++;
    slice_point();
    return 0;
}
int iWakeupThread(int id) { return WakeupThread(id); }

int CancelWakeupThread(int id)
{
    Thread *t = id ? TH(id) : (g.cur ? &g.th[g.cur] : 0);
    if (!t) return KE_UNKNOWN_THID;
    int n = t->wakeup;
    t->wakeup = 0;
    return n;
}
int iCancelWakeupThread(int id) { return CancelWakeupThread(id); }

static uint64_t usec2clk(uint64_t us) { return us * HZ / 1000000ull; }

int DelayThread(uint32_t usec)
{
    if (!g.cur) return 0;
    Thread *t = &g.th[g.cur];
    uint64_t d = usec2clk(usec);
    t->wake_at = g.T + (d ? d : 1);
    return block(WT_DELAY, 0) ? t->wake_ret : 0;
}

int GetSystemTime(SysClock *c)
{
    c->lo = (uint32_t)g.T;
    c->hi = (uint32_t)(g.T >> 32);
    return 0;
}
void USec2SysClock(uint32_t usec, SysClock *c)
{
    uint64_t v = usec2clk(usec);
    c->lo = (uint32_t)v;
    c->hi = (uint32_t)(v >> 32);
}
void SysClock2USec(const SysClock *c, uint32_t *sec, uint32_t *usec)
{
    uint64_t v = c->lo | (uint64_t)c->hi << 32;
    uint64_t us = v * 1000000ull / HZ;
    *sec = (uint32_t)(us / 1000000);
    *usec = (uint32_t)(us % 1000000);
}

int SetAlarm(const SysClock *c, AlarmHandler fn, void *arg)
{
    uint64_t v = c->lo | (uint64_t)c->hi << 32;
    for (int i = 0; i < MAX_AL; i++)
        if (!g.al[i].used) { g.al[i] = Alarm{1, g.T + (v ? v : 1), fn, arg}; return 0; }
    return -1;
}
int iSetAlarm(const SysClock *c, AlarmHandler fn, void *arg) { return SetAlarm(c, fn, arg); }
int CancelAlarm(AlarmHandler fn, void *arg)
{
    for (int i = 0; i < MAX_AL; i++)
        if (g.al[i].used && g.al[i].fn == fn && g.al[i].arg == arg) { g.al[i].used = 0; return 0; }
    return -1;
}
int iCancelAlarm(AlarmHandler fn, void *arg) { return CancelAlarm(fn, arg); }

void WaitNextEvent()
{
    Thread *t = curth();
    if (!t) return;
    t->poll = 1;
    g.yield_req = 0;
    slice_end();
}

/* ---------------------------------------------------------------- thsemap / thevent */

int CreateSema(const SemaParam *p)
{
    for (int i = 1; i < MAX_SEM; i++)
        if (!g.sem[i].used) { g.sem[i] = Sema{1, p->init_count, p->max_count, p->attr, p->option, p->init_count}; return i; }
    return -1;
}

int DeleteSema(int id)
{
    if (!sema_ok(id)) return KE_UNKNOWN_SEMID;
    Thread *t;
    while ((t = first_waiter(WT_SEMA, id))) make_ready(t, KE_WAIT_DELETE);
    g.sem[id].used = 0;
    slice_point();
    return 0;
}

int SignalSema(int id)
{
    if (!sema_ok(id)) return KE_UNKNOWN_SEMID;
    int r = 0;
    Thread *t = first_waiter(WT_SEMA, id);
    if (t) make_ready(t, 0);
    else if (g.sem[id].count < g.sem[id].max) g.sem[id].count++;
    else r = KE_SEMA_OVF;
    slice_point();
    return r;
}
int iSignalSema(int id) { return SignalSema(id); }

int WaitSema(int id)
{
    if (!sema_ok(id)) return KE_UNKNOWN_SEMID;
    if (g.sem[id].count > 0) { g.sem[id].count--; return 0; }
    Thread *t = curth();
    if (t) t->seq = ++g.seq_ctr;
    return block(WT_SEMA, id) ? t->wake_ret : 0;
}

int PollSema(int id)
{
    if (!sema_ok(id)) return KE_UNKNOWN_SEMID;
    if (g.sem[id].count > 0) { g.sem[id].count--; return 0; }
    return KE_SEMA_ZERO;
}

int ReferSemaStatus(int id, SemaInfo *info)
{
    if (!sema_ok(id)) return KE_UNKNOWN_SEMID;
    int w = 0;
    for (int i = 1; i < MAX_TH; i++) w += g.th[i].used && g.th[i].status == TS_WAIT && g.th[i].wait_type == WT_SEMA && g.th[i].wait_id == id;
    Sema *s = &g.sem[id];
    if (info) *info = SemaInfo{s->attr, s->option, s->init, s->max, s->count, w};
    return 0;
}

int CreateEventFlag(const EventFlagParam *p)
{
    for (int i = 1; i < MAX_EVF; i++)
        if (!g.evf[i].used) { g.evf[i] = Evf{1, p->init_pattern, p->attr, p->option}; return i; }
    return -1;
}
int DeleteEventFlag(int id)
{
    if (id > 0 && id < MAX_EVF) g.evf[id].used = 0;   /* as the hybrid: waiters stay blocked */
    return 0;
}
int SetEventFlag(int id, uint32_t bits)
{
    if (!evf_valid(id)) return KE_UNKNOWN_EVFID;
    g.evf[id].bits |= bits;
    evf_check_waiters(id);
    slice_point();
    return 0;
}
int iSetEventFlag(int id, uint32_t bits) { return SetEventFlag(id, bits); }
int ClearEventFlag(int id, uint32_t bits)
{
    if (!evf_valid(id)) return KE_UNKNOWN_EVFID;
    g.evf[id].bits &= bits;
    return 0;
}
int iClearEventFlag(int id, uint32_t bits) { return ClearEventFlag(id, bits); }
int WaitEventFlag(int id, uint32_t bits, int mode, uint32_t *res)
{
    if (!evf_valid(id)) return KE_UNKNOWN_EVFID;
    if (evf_ok(g.evf[id].bits, bits, (uint32_t)mode)) {
        if (res) *res = g.evf[id].bits;
        if (mode & EW_CLEAR) g.evf[id].bits &= ~bits;
        return 0;
    }
    Thread *t = curth();
    if (!t) return 0;
    t->ev_bits = bits;
    t->ev_mode = (uint32_t)mode;
    t->ev_res = res;
    return block(WT_EVENT, id) ? t->wake_ret : 0;
}
int PollEventFlag(int id, uint32_t bits, int mode, uint32_t *res)
{
    if (!evf_valid(id)) return KE_UNKNOWN_EVFID;
    if (!evf_ok(g.evf[id].bits, bits, (uint32_t)mode)) return KE_EVF_COND;
    if (res) *res = g.evf[id].bits;
    if (mode & EW_CLEAR) g.evf[id].bits &= ~bits;
    return 0;
}

/* ---------------------------------------------------------------- intrman */

int RegisterIntrHandler(int irq, int mode, IntrHandler fn, void *arg)
{
    irq &= 63;
    g.ih[irq] = IHandler{fn, arg, 1, g.ih[irq].enabled, mode};
    return 0;
}
int ReleaseIntrHandler(int irq) { g.ih[irq & 63].used = 0; return 0; }
int EnableIntr(int irq) { g.ih[irq & 63].enabled = 1; return 0; }
int DisableIntr(int irq, int *res)
{
    irq &= 63;
    if (res) *res = g.ih[irq].enabled ? irq : 0;
    int r = g.ih[irq].enabled ? 0 : KE_INTRDISABLE;
    g.ih[irq].enabled = 0;
    return r;
}
int CpuSuspendIntr(int *state)
{
    int old = g.in_intr ? 1 : g.intr_off;   /* in a handler interrupts are already disabled */
    if (state) *state = old;
    if (!g.in_intr) g.intr_off = 1;
    return old ? KE_CPUDI : 0;
}
int CpuResumeIntr(int state)
{
    int was = g.intr_off;
    g.intr_off = g.in_intr ? g.intr_off : (state != 0);
    if (was && !g.intr_off && curth()) { g.yield_req = 0; slice_end(); }   /* z: held IRQs / deferred calls run now */
    return 0;
}
int QueryIntrContext() { return g.in_intr ? 1 : 0; }
void RaiseIrq(int irq) { if (irq >= 0 && irq < 64) g.irq_pending |= 1ull << irq; }
void DeferCall(void (*fn)(void *), void *arg) { if (fn) defer(Deferred{0, fn, 0, arg, 0}); }

/* ---------------------------------------------------------------- timrman */

int AllocHardTimer(int source, int size, int prescale)
{
    (void)size;
    for (int i = 1; i < MAX_HT; i++)
        if (!g.ht[i].used) { g.ht[i] = HTimer{1, 0, (uint32_t)source, (uint32_t)prescale, 0, 0, 0, 0, g.T, ~0ull}; return i; }
    return -1;
}
int FreeHardTimer(int id) { HTimer *t = HT(id); if (t) t->used = 0; return t ? 0 : -1; }
int SetTimerHandler(int id, uint32_t compare, TimerHandler fn, void *arg)
{
    HTimer *t = HT(id);
    if (!t) return -1;
    t->compare = compare; t->fn = fn; t->arg = arg;
    if (t->running) t->next = t->t0 + (uint64_t)t->compare * ht_div(t);
    return 0;
}
int SetupHardTimer(int id, int source, int mode, int prescale)
{
    HTimer *t = HT(id);
    if (!t) return -1;
    t->source = (uint32_t)source; t->mode = (uint32_t)mode; t->prescale = (uint32_t)prescale;
    return 0;
}
int StartHardTimer(int id)
{
    HTimer *t = HT(id);
    if (!t) return -1;
    t->running = 1;
    t->t0 = g.T;
    t->next = t->compare ? g.T + (uint64_t)t->compare * ht_div(t) : ~0ull;
    return 0;
}
int StopHardTimer(int id) { HTimer *t = HT(id); if (t) t->running = 0; return t ? 0 : -1; }
uint32_t GetTimerCounter(int id) { HTimer *t = HT(id); return t ? (uint32_t)((g.T - t->t0) / ht_div(t)) : 0; }

/* ---------------------------------------------------------------- sifman */

static void iop_to_ee(const void *src, uint32_t dest, uint32_t size)
{
    uint32_t n = (size + 15) & ~15u;
    uint32_t a = Addr(src);
    uint8_t pad[16];
    int ok;
    if (a && a + (uint64_t)n <= RAM_SIZE) ok = hk.ee_write && hk.ee_write(hk.user, dest, src, n);
    else {   /* host buffer outside the IOP RAM image: the 16-byte tail is zero filled */
        ok = hk.ee_write && hk.ee_write(hk.user, dest, src, size & ~15u);
        if (ok && n != (size & ~15u)) {
            memset(pad, 0, sizeof pad);
            memcpy(pad, (const uint8_t *)src + (size & ~15u), size & 15);
            ok = hk.ee_write(hk.user, dest + (size & ~15u), pad, 16);
        }
    }
    if (!ok) plat_log("iopk: IOP->EE dma -> %08x (%x) failed", dest, n);
}

uint32_t SifSetDma(const SifDmaData *d, int n)
{
    for (int i = 0; i < n; i++) iop_to_ee(d[i].src, d[i].dest, d[i].size);
    return ++g.sifdma_id;
}
int SifDmaStat(uint32_t id) { (void)id; return -1; }
uint32_t SifSetDmaIntr(const SifDmaData *d, int n, void (*fn)(void *), void *arg)
{
    for (int i = 0; i < n; i++) iop_to_ee(d[i].src, d[i].dest, d[i].size);
    DeferCall(fn, arg);
    return ++g.sifdma_id;
}

/* ---------------------------------------------------------------- sifcmd */

void SifInitRpc(int mode) { (void)mode; }
int SifSetRpcQueue(RpcQueue *qd, int thread_id) { qd->thread_id = thread_id; return 0; }
int SifRegisterRpc(RpcServer *sd, uint32_t sid, RpcFunc func, void *buf, RpcFunc cfunc, void *cbuf, RpcQueue *qd)
{
    *sd = RpcServer{sid, func, buf, cfunc, cbuf, qd};
    if (g.nsrv < MAX_SRV) g.srv[g.nsrv++] = sd;
    return 0;
}

void SifRpcLoop(RpcQueue *qd)
{
    Thread *t = curth();
    if (!t) return;
    t->rpc_qd = qd;
    for (;;) {
        if (!try_claim(qd)) {
            t->rpc_claimed = 0;
            block(WT_RPC, 0);
            if (!t->rpc_claimed) continue;   /* released without a request */
        }
        RpcServer *s = g.req.s;
        void *r = s->func((int)g.req.fno, s->buf, (int)g.req.size);
        g.req.result = r;
        g.req.done = 1;
    }
}

int SifBindRpc(RpcClient *cd, uint32_t sid, int mode)
{
    (void)mode;
    cd->sid = sid;
    cd->server = hk.ee_has_server && hk.ee_has_server(hk.user, sid) ? 0x2000u + (sid & 0xff) * 16 : 0;
    return 0;
}

int SifCallRpc(RpcClient *cd, uint32_t fno, uint32_t mode, const void *send, uint32_t ssize, void *recv, uint32_t rsize,
               void (*end)(void *), void *end_arg)
{
    if (!hk.ee_has_server || !hk.ee_has_server(hk.user, cd->sid)) { plat_log("iopk: SifCallRpc to unknown EE server %08x", cd->sid); return -1; }
    int k;
    for (k = 0; k < MAX_EREQ && g.ereq[k].used; k++) {}
    if (k == MAX_EREQ) { plat_log("iopk: IOP->EE request queue full"); return -1; }
    g.ereq[k] = EeReq{1, cd->sid, fno, mode, ssize, rsize, send, recv, end, end_arg, cd, g.cur};
    if (hk.ee_rpc_posted) hk.ee_rpc_posted(hk.user, cd->sid);
    if (!(mode & 1)) {
        Thread *t = curth();
        return block(WT_EERPC, k) ? t->wake_ret : 0;
    }
    return 0;
}

int SifCheckStatRpc(RpcClient *cd)
{
    for (int k = 0; k < MAX_EREQ; k++) if (g.ereq[k].used && g.ereq[k].cd == cd) return 1;
    return 0;
}

/* ---------------------------------------------------------------- cdvdman / ioman */

int CdInit(int mode) { (void)mode; return 1; }
int CdRead(uint32_t lsn, uint32_t sectors, void *buf, const void *mode)
{
    (void)mode;
    if (!ps2f_iop_cd_read(lsn, sectors, buf, Addr(buf), g.cd_cb != 0)) return 0;
    if (g.cd_cb) defer(Deferred{1, 0, g.cd_cb, 0, 1});   /* SCECdFuncRead */
    return 1;
}
int CdSync(int mode) { return ps2f_iop_cd_sync((uint32_t)mode); }
int CdSeek(uint32_t lsn) { (void)lsn; return 1; }
int CdGetError() { return 0; }
int CdSearchFile(CdlFILE *fp, const char *name)
{
    uint32_t lsn, size;
    if (!ps2f_iop_cd_search_file(name, &lsn, &size)) return 0;
    fp->lsn = lsn;
    fp->size = size;
    const char *b = strrchr(name, '\\');
    memset(fp->name, 0, sizeof fp->name);
    strncpy(fp->name, b ? b + 1 : name, 15);
    return 1;
}
int CdDiskReady(int mode) { (void)mode; return 2; }
int CdStatus() { return 0x0a; }
void (*CdCallback(void (*fn)(int)))(int)
{
    void (*old)(int) = g.cd_cb;
    g.cd_cb = fn;
    return old;
}
int CdBreak() { return 1; }
int Open(const char *name, int flags) { return ps2f_iop_open(name, (uint32_t)flags); }
int Close(int fd) { return ps2f_iop_close((uint32_t)fd); }
int Read(int fd, void *buf, int n) { return ps2f_iop_read((uint32_t)fd, buf, (uint32_t)n, Addr(buf)); }
int Lseek(int fd, int off, int whence) { return ps2f_iop_lseek((uint32_t)fd, off, (uint32_t)whence); }
int Ioctl(int fd, int cmd, void *arg) { (void)fd; (void)cmd; (void)arg; return 0; }

void FlushDcache() {}
int Printf(const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    tty_out(buf);
    return n;
}

/* ---------------------------------------------------------------- sd adapters */

namespace {
int k_suspend(void *, int *s) { return CpuSuspendIntr(s); }
int k_resume(void *, int s) { return CpuResumeIntr(s); }
int k_query(void *) { return QueryIntrContext(); }
int k_reg(void *, int irq, int mode, sd::KIntrHandler fn, void *arg) { return RegisterIntrHandler(irq, mode, fn, arg); }
int k_rel(void *, int irq) { return ReleaseIntrHandler(irq); }
int k_en(void *, int irq) { return EnableIntr(irq); }
int k_dis(void *, int irq, int *res) { return DisableIntr(irq, res); }
int k_cevf(void *, const sd::EventFlagParam *p) { EventFlagParam q{p->attr, p->option, p->init}; return CreateEventFlag(&q); }
int k_devf(void *, int id) { return DeleteEventFlag(id); }
int k_sevf(void *, int id, uint32_t b) { return SetEventFlag(id, b); }
int k_isevf(void *, int id, uint32_t b) { return iSetEventFlag(id, b); }
int k_clevf(void *, int id, uint32_t b) { return ClearEventFlag(id, b); }
int k_iclevf(void *, int id, uint32_t b) { return iClearEventFlag(id, b); }
int k_wevf(void *, int id, uint32_t b, int mode, uint32_t *res) { return WaitEventFlag(id, b, mode, res); }
void k_flush(void *) { FlushDcache(); }
int k_sifdma(void *, const sd::SifDmaTransfer *t, int n)
{
    SifDmaData d[64];
    if (n > 64) { plat_log("iopk: sd SifSetDma with %d transfers (max 64)", n); n = 64; }
    for (int i = 0; i < n; i++) d[i] = SifDmaData{t[i].src, t[i].dest, t[i].size, t[i].attr};
    return (int)SifSetDma(d, n);
}
int k_sifstat(void *, int id) { return SifDmaStat((uint32_t)id); }
const sd::Kernel k_sd = {0, k_suspend, k_resume, k_query, k_reg, k_rel, k_en, k_dis, k_cevf, k_devf, k_sevf, k_isevf, k_clevf,
                         k_iclevf, k_wevf, k_flush, k_sifdma, k_sifstat};
}  // namespace

const sd::Kernel *SdKernel() { return &k_sd; }
void SdHwSync(void *u) { (void)u; SpuSync(); }

}  // namespace iopk
