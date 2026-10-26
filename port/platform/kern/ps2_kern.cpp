/* Native EE kernel (see ps2_kern.h). "kc:N" = port/runtime/kernel.c line N, whose behaviour this copies. */
#include "ps2_kern.h"
#include "fiber.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#if defined(__SSE__) || defined(_M_IX86) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace {

constexpr int MAX_THREADS = 256, MAX_SEMAS = 256, MAX_HANDLERS = 64;   /* kc:11-13 */
constexpr int IDLE_LIMIT = 3600;                /* idle vblanks without a wakeup before a deadlock is declared (1 min) */
constexpr size_t FIBER_STACK = 1024 * 1024;     /* host stack per PS2 thread (the PS2 stack sizes are ignored) */
constexpr int E_SEMA_ZERO = -419;               /* PollSema on count 0 (kc:535-537) */

struct KThread {
    int used, status, prio, init_prio;
    void (*entry)(void *);
    void *arg;
    int wait_type, wait_id, wakeup, wait_result;
    unsigned long long seq;
    hostfiber::Fiber *fiber;
    int owned;                                  /* fiber created by the kernel (thread 1 runs on the host's own) */
};
struct KSema { int used, count, max, init, waiters; };
struct KHandler { int used, cause, enabled; int (*fn)(void); int next; };

KThread th[MAX_THREADS];
KSema sema[MAX_SEMAS];
KHandler intc_h[MAX_HANDLERS];
int g_inited, cur_tid, need_resched, in_irq, eie, reap_pending, klog = -1;
unsigned long long seq_ctr;
unsigned intc_mask, vblanks;
Ps2kIdleFn idle_fn;
void *idle_user;
Ps2kDeadlockFn deadlock_fn;
char g_report[16384];

void ensure() { if (!g_inited) ps2k_reset(); }
KThread *T(int id) { return id > 0 && id < MAX_THREADS && th[id].used ? &th[id] : 0; }   /* kc:51 */
KThread *cur() { return &th[cur_tid]; }
bool sema_ok(int id) { return (unsigned)id < (unsigned)MAX_SEMAS && sema[id].used; }      /* kc:520 (u32 id) */

const char *tstat(int s)
{
    switch (s) {
    case PS2K_RUN: return "RUN";
    case PS2K_READY: return "READY";
    case PS2K_WAIT: return "WAIT";
    case PS2K_DORMANT: return "DORM";
    }
    return "?";
}

[[noreturn]] void fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "ps2k: FATAL: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    ps2k_dump(g_report, sizeof g_report);
    fputs(g_report, stderr);
    fflush(stderr);
    exit(3);
}

/* ------------------------------------------------------------------ scheduling */

KThread *pick()   /* kc:64-73: lowest prio, then lowest seq, among RUN / READY */
{
    KThread *best = 0;
    for (int i = 1; i < MAX_THREADS; i++) {
        KThread *t = &th[i];
        if (!t->used || (t->status != PS2K_RUN && t->status != PS2K_READY)) continue;
        if (!best || t->prio < best->prio || (t->prio == best->prio && t->seq < best->seq)) best = t;
    }
    return best;
}

void thread_body(void *p);

void reap()   /* fibers of exited threads, dropped once another fiber runs */
{
    reap_pending = 0;
    hostfiber::Fiber *me = hostfiber::current();
    for (int i = 1; i < MAX_THREADS; i++) {
        KThread *t = &th[i];
        if (t->used && t->status == PS2K_DORMANT && t->fiber && t->owned && t->fiber != me) {
            hostfiber::destroy(t->fiber);
            t->fiber = 0;
        }
    }
}

void switch_to(KThread *n)   /* kc:77-96 */
{
    KThread *prev = cur();
    if (prev->status == PS2K_RUN) prev->status = PS2K_READY;   /* keeps its seq: stays first of its priority */
    n->status = PS2K_RUN;
    if (n == prev) return;
    if (klog < 0) klog = getenv("ACNB_KERNEL_LOG") != 0;
    if (klog) fprintf(stderr, "kthread: %d -> %d vb %u\n", cur_tid, (int)(n - th), vblanks);
    cur_tid = (int)(n - th);
    if (!n->fiber) {
        n->fiber = hostfiber::create(FIBER_STACK, thread_body, n);
        n->owned = 1;
        if (!n->fiber) fatal("cannot create a fiber for thread %d\n", cur_tid);
    }
    hostfiber::switch_to(n->fiber);
    /* resumed: cur_tid was set by whoever switched back here */
    if (reap_pending) reap();
}

void make_ready(KThread *t)   /* kc:113-118 */
{
    t->status = PS2K_READY;
    t->seq = ++seq_ctr;
    if (t->prio < cur()->prio) need_resched = 1;
}

void maybe_preempt()   /* kc:121-127 */
{
    need_resched = 0;
    if (in_irq) return;
    KThread *n = pick();
    if (n && n != cur()) switch_to(n);
}

/* the end of every kernel call (kc:604) */
inline int sys_end(int ret)
{
    if (need_resched) maybe_preempt();
    return ret;
}

[[noreturn]] void deadlock(const char *why)
{
    int n = snprintf(g_report, sizeof g_report, "ps2k: DEADLOCK: %s (cur tid %d, vblank %u)\n", why, cur_tid, vblanks);
    ps2k_dump(g_report + n, (int)sizeof g_report - n);
    if (deadlock_fn) deadlock_fn(g_report);   /* the selftest's hook switches away and never comes back */
    fputs(g_report, stderr);
    fflush(stderr);
    exit(3);
}

/* One vblank of virtual time (the idle step below and ps2k_busy_vblank): the hook (B3: the vsync step) in interrupt
 * context, then every thread blocked in ps2k_wait_vblank becomes ready. Returns the hook's result (0 = no time source). */
int vblank_step()
{
    vblanks++;
    int r = 0;
    if (idle_fn) {
        in_irq++;
        r = idle_fn(idle_user);
        in_irq--;
    }
    for (int i = 1; i < MAX_THREADS; i++) {
        KThread *t = &th[i];
        if (t->used && t->status == PS2K_WAIT && t->wait_type == PS2K_WT_VBLANK) {
            t->wait_type = PS2K_WT_NONE;
            make_ready(t);
        }
    }
    return r;
}

/* Pick the next thread; when none is ready, step virtual time one vblank at a time (kc:101-111 skips to the next
 * hw event; in the product the vblank is the only time source). */
void schedule()
{
    int steps = 0;
    for (;;) {
        if (KThread *n = pick()) { switch_to(n); return; }
        int vwait = 0;
        for (int i = 1; i < MAX_THREADS; i++)
            if (th[i].used && th[i].status == PS2K_WAIT && th[i].wait_type == PS2K_WT_VBLANK) vwait = 1;
        if (!vwait && !idle_fn) deadlock("no thread is ready and nothing can wake one (no idle hook)");
        int r = vblank_step();
        if (vwait) continue;
        if (!r) deadlock("no thread is ready and the idle hook has no time source");
        if (++steps >= IDLE_LIMIT) deadlock("no thread is ready and no wakeup in 3600 idle vblanks");
    }
}

void block_current(int wait_type, int wait_id)   /* kc:129-136 */
{
    if (in_irq) fatal("blocking call (wait type %d) in interrupt / idle context\n", wait_type);
    KThread *t = cur();
    t->status = PS2K_WAIT;
    t->wait_type = wait_type;
    t->wait_id = wait_id;
    schedule();
}

[[noreturn]] void exit_current()   /* kc:416-419 (and fiber_main's return path) */
{
    KThread *t = cur();
    if (in_irq) fatal("ExitThread in interrupt / idle context\n");
    t->status = PS2K_DORMANT;
    t->wait_type = PS2K_WT_NONE;
    reap_pending = 1;
    schedule();
    fatal("dormant thread %d resumed\n", cur_tid);
}

void thread_body(void *p)
{
    KThread *t = (KThread *)p;
    if (reap_pending) reap();
    t->entry(t->arg);
    exit_current();   /* the entry returned: ExitThread */
}

void drop_fiber(KThread *t)   /* never the running fiber (callers exclude the current thread) */
{
    if (t->fiber && t->owned && t->fiber != hostfiber::current()) {
        hostfiber::destroy(t->fiber);
        t->fiber = 0;
    }
}

void wake_sleeper(KThread *t)   /* kc:475-481 / kc:266-274 */
{
    if (t->status == PS2K_WAIT && t->wait_type == PS2K_WT_SLEEP) {
        t->wait_type = PS2K_WT_NONE;
        make_ready(t);
    } else t->wakeup++;
}

void sema_signal(int id)   /* kc:315-335: wake the waiter with the lowest seq, else count++ (no max clamp) */
{
    KThread *best = 0;
    for (int i = 1; i < MAX_THREADS; i++) {
        KThread *t = &th[i];
        if (t->used && t->status == PS2K_WAIT && t->wait_type == PS2K_WT_SEMA && t->wait_id == id && (!best || t->seq < best->seq))
            best = t;
    }
    if (!best) { sema[id].count++; return; }
    best->wait_type = PS2K_WT_NONE;
    sema[id].waiters--;
    make_ready(best);
}

}  // namespace

/* ------------------------------------------------------------------ host side */

extern "C" void ps2k_reset(void)
{
    hostfiber::Fiber *me = hostfiber::current();
    for (int i = 1; i < MAX_THREADS; i++) {
        KThread *t = &th[i];
        if (!t->used || !t->fiber || !t->owned) continue;
        if (t->fiber == me) fatal("ps2k_reset from a kernel-created thread\n");
        hostfiber::destroy(t->fiber);
    }
    /* kc:612-615: the PS2 FPU / VU have no denormals; FTZ + DAZ make the native game code (SSE scalar math, both builds
     * compile for pentium4) flush them the same way the hybrid does. Fibers share the host thread's MXCSR. A non-x86
     * port needs the equivalent (AArch64: FPCR.FZ). Rounding stays round-to-nearest (Stage 0: the development version's
     * ACNB_FPU_RZ test switch is gone). */
#if defined(__SSE__) || defined(_M_IX86) || defined(_M_X64)
    _mm_setcsr(_mm_getcsr() | 0x8040);
#endif
    memset(th, 0, sizeof th);
    memset(sema, 0, sizeof sema);
    memset(intc_h, 0, sizeof intc_h);
    KThread *t = &th[1];   /* kc:618-625: main = thread 1, RUN, prio 0 */
    t->used = 1;
    t->status = PS2K_RUN;
    t->fiber = me;
    cur_tid = 1;
    sema[0].used = 1;      /* kc:626-627: ids start at 1 */
    intc_h[0].used = 1;
    seq_ctr = 0;
    need_resched = in_irq = reap_pending = 0;
    eie = 1;               /* Status.EIE set (kc:622 cop0 Status 0x70030c13) */
    intc_mask = vblanks = 0;
    idle_fn = 0;
    idle_user = 0;
    deadlock_fn = 0;
    g_inited = 1;
}

extern "C" void ps2k_init(void) { ensure(); }
extern "C" void ps2k_set_idle_hook(Ps2kIdleFn fn, void *user) { ensure(); idle_fn = fn; idle_user = user; }
extern "C" void ps2k_set_deadlock_hook(Ps2kDeadlockFn fn) { ensure(); deadlock_fn = fn; }
extern "C" unsigned ps2k_vblank_count(void) { return vblanks; }

extern "C" int ps2k_wait_vblank(void)
{
    ensure();
    block_current(PS2K_WT_VBLANK, 0);
    return sys_end(0);
}

/* the running thread spins through one vblank (sceGsSyncV's CSR poll in the original: no other thread runs while it
 * waits, unlike ps2k_wait_vblank); a better thread the step readied runs before this returns (kc: the interrupt's
 * maybe_preempt) */
extern "C" void ps2k_busy_vblank(void)
{
    ensure();
    if (in_irq) fatal("ps2k_busy_vblank in interrupt / idle context\n");
    vblank_step();
    sys_end(0);
}

/* an INTC interrupt from host context (kc:178-190 service_irqs): every handler of the cause, in slot order, when the
 * cause is enabled; interrupt context (no switches, a readied thread runs at the next kernel call) */
extern "C" void ps2k_intc_raise(int cause)
{
    ensure();
    if (!(intc_mask & (1u << (cause & 31)))) return;
    in_irq++;
    for (int i = 1; i < MAX_HANDLERS; i++)
        if (intc_h[i].used && intc_h[i].enabled && intc_h[i].cause == cause && intc_h[i].fn) intc_h[i].fn();
    in_irq--;
}

extern "C" void ps2k_irq_wakeup(int tid) { ensure(); if (KThread *t = T(tid)) wake_sleeper(t); }   /* kc:266 */
extern "C" void ps2k_irq_signal(int sid) { ensure(); if (sema_ok(sid)) sema_signal(sid); }
extern "C" void ps2k_irq_enter(void) { in_irq++; }
extern "C" void ps2k_irq_leave(void) { if (in_irq > 0) in_irq--; }

extern "C" void ps2k_preempt_point(void)
{
    ensure();
    if (in_irq || !eie) return;   /* interrupts off: the asynchronous point waits for EIntr */
    sys_end(0);
}

extern "C" int ps2k_dump(char *buf, int size)
{
    static const char *wt[] = {"-", "sleep", "sema", "vblank"};
    int n = 0;
    auto put = [&](const char *fmt, auto... a) {
        if (n >= size - 1) return;
        int k = snprintf(buf + n, size - n, fmt, a...);
        if (k > 0) n += k < size - n ? k : size - 1 - n;
    };
    put("  threads (cur %d, EIE %d, need_resched %d, in_irq %d, vblank %u):\n", cur_tid, eie, need_resched, in_irq, vblanks);
    for (int i = 1; i < MAX_THREADS; i++) {
        KThread *t = &th[i];
        if (!t->used) continue;
        put("  tid %3d %-5s prio 0x%02x seq %llu wait %s", i, tstat(t->status), t->prio, t->seq,
            t->status == PS2K_WAIT ? wt[t->wait_type & 3] : "-");
        if (t->status == PS2K_WAIT && t->wait_type == PS2K_WT_SEMA)
            put(" %d (count %d, waiters %d)", t->wait_id, sema[t->wait_id].count, sema[t->wait_id].waiters);
        put(" wakeup %d entry ", t->wakeup);
        if (i == 1 && !t->owned) put("%s\n", "main (host thread)");
        else put("%p (see acnb.map)\n", (void *)t->entry);
    }
    return n;
}

extern "C" int ps2k_thread_state(int tid, int *prio, int *wait_type, int *wait_id, int *wakeup)
{
    KThread *t = T(tid);
    if (!t) return 0;
    if (prio) *prio = t->prio;
    if (wait_type) *wait_type = t->status == PS2K_WAIT ? t->wait_type : 0;
    if (wait_id) *wait_id = t->wait_id;
    if (wakeup) *wakeup = t->wakeup;
    return t->status;
}

extern "C" int ps2k_sema_count(int sid) { return sema_ok(sid) ? sema[sid].count : -1; }

/* ------------------------------------------------------------------ threads */

extern "C" int ps2_CreateThread(void *param)   /* kc:387-398 (attr / option / status words not read) */
{
    ensure();
    const Ps2kThreadParam *p = (const Ps2kThreadParam *)param;
    if (!p) return sys_end(-1);
    int i = 1;
    while (i < MAX_THREADS && th[i].used) i++;
    if (i == MAX_THREADS) return sys_end(-1);
    KThread *t = &th[i];
    memset(t, 0, sizeof *t);
    t->used = 1;
    t->status = PS2K_DORMANT;
    t->entry = p->entry;
    t->init_prio = t->prio = p->initPriority;
    return sys_end(i);
}

extern "C" int ps2_StartThread(int tid, void *arg)   /* kc:400-415 */
{
    ensure();
    KThread *t = T(tid);
    if (!t || t->status != PS2K_DORMANT) return sys_end(-1);
    drop_fiber(t);   /* a fresh stack from the entry (an exited / terminated thread's old fiber is dropped) */
    t->arg = arg;
    t->prio = t->init_prio;   /* kc:411-412: re-armed */
    t->wakeup = 0;
    t->wait_type = PS2K_WT_NONE;
    make_ready(t);
    return sys_end(0);
}

extern "C" void ps2_ExitThread(void) { ensure(); exit_current(); }
extern "C" void ps2_ExitDeleteThread(void) { ensure(); exit_current(); }   /* kc:416-419: same as ExitThread */

extern "C" int ps2_TerminateThread(int tid)   /* kc:420-425 */
{
    ensure();
    KThread *t = T(tid);
    if (t && t != cur()) {
        if (t->status == PS2K_WAIT && t->wait_type == PS2K_WT_SEMA) sema[t->wait_id].waiters--;
        t->status = PS2K_DORMANT;
        t->wait_type = PS2K_WT_NONE;
        drop_fiber(t);   /* its stack is abandoned mid-function (game code is C-like: nothing to unwind) */
    }
    return sys_end(0);
}

extern "C" int ps2_DeleteThread(int tid)   /* kc:399 */
{
    ensure();
    KThread *t = T(tid);
    if (t && t->status == PS2K_DORMANT && t != cur()) {
        drop_fiber(t);
        t->used = 0;
    }
    return sys_end(0);
}

extern "C" int ps2_ChangeThreadPriority(int tid, int prio)   /* kc:428-436 */
{
    ensure();
    KThread *t = tid ? T(tid) : cur();
    if (!t) return sys_end(-1);
    int old = t->prio;
    t->prio = prio;
    t->seq = ++seq_ctr;
    need_resched = 1;
    return sys_end(old);
}

extern "C" int ps2_GetThreadId(void) { ensure(); return sys_end(cur_tid); }   /* kc:456 */

extern "C" int ps2_SleepThread(void)   /* kc:469-474 */
{
    ensure();
    KThread *t = cur();
    if (t->wakeup > 0) t->wakeup--;
    else block_current(PS2K_WT_SLEEP, 0);
    return sys_end(0);
}

extern "C" int ps2_WakeupThread(int tid)   /* kc:475-481 */
{
    ensure();
    KThread *t = T(tid);
    if (!t) return sys_end(-1);
    wake_sleeper(t);
    return sys_end(0);
}

extern "C" int ps2_iWakeupThread(int tid) { return ps2_WakeupThread(tid); }   /* kc:475: the same case (0x34) */

extern "C" int ps2_CancelWakeupThread(int tid)   /* kc:482 */
{
    ensure();
    KThread *t = tid ? T(tid) : cur();
    int r = 0;
    if (t) { r = t->wakeup; t->wakeup = 0; }
    return sys_end(r);
}

extern "C" int ps2_iCancelWakeupThread(int tid) { return ps2_CancelWakeupThread(tid); }

/* ------------------------------------------------------------------ semaphores */

extern "C" int ps2_CreateSema(void *param)   /* kc:510-517 */
{
    ensure();
    const Ps2kSemaParam *p = (const Ps2kSemaParam *)param;
    if (!p) return sys_end(-1);
    int i = 1;
    while (i < MAX_SEMAS && sema[i].used) i++;
    if (i == MAX_SEMAS) return sys_end(-1);
    sema[i] = KSema{1, p->initCount, p->maxCount, p->initCount, 0};
    return sys_end(i);
}

extern "C" int ps2_DeleteSema(int sid)   /* kc:518 */
{
    ensure();
    if ((unsigned)sid < (unsigned)MAX_SEMAS) sema[sid].used = 0;
    return sys_end(0);
}

extern "C" int ps2_SignalSema(int sid)   /* kc:519-523 */
{
    ensure();
    if (!sema_ok(sid)) return sys_end(-1);
    sema_signal(sid);
    return sys_end(sid);
}

extern "C" int ps2_iSignalSema(int sid) { return ps2_SignalSema(sid); }   /* kc:519: the same case (0x43) */

extern "C" int ps2_WaitSema(int sid)   /* kc:524-534 */
{
    ensure();
    if (!sema_ok(sid)) return sys_end(-1);
    if (sema[sid].count > 0) { sema[sid].count--; return sys_end(sid); }
    sema[sid].waiters++;
    KThread *t = cur();
    t->wait_result = sid;
    block_current(PS2K_WT_SEMA, sid);
    return sys_end(t->wait_result);
}

extern "C" int ps2_PollSema(int sid)   /* kc:535-538 */
{
    ensure();
    if (!sema_ok(sid)) return sys_end(-1);
    if (sema[sid].count > 0) { sema[sid].count--; return sys_end(sid); }
    return sys_end(E_SEMA_ZERO);
}

/* ------------------------------------------------------------------ interrupts */

extern "C" int DIntr(void) /* : returns the EIE state before the DI */
{
    ensure();
    int old = eie;
    eie = 0;
    return old;
}

extern "C" int EIntr(void) /* : the EIE state read before the EI; a deferred preemption runs now */
{
    ensure();
    int old = eie;
    eie = 1;
    return sys_end(old);
}

extern "C" int AddIntcHandler(int cause, int (*handler)(void), int next)   /* kc:361-369 */
{
    ensure();
    int i = 1;
    while (i < MAX_HANDLERS && intc_h[i].used) i++;
    if (i == MAX_HANDLERS) fatal("out of INTC handler slots\n");
    intc_h[i] = KHandler{1, cause, 1, handler, next};
    return sys_end(i);
}

extern "C" int RemoveIntcHandler(int cause, int id)   /* kc:370-373 */
{
    ensure();
    (void)cause;
    if ((unsigned)id < (unsigned)MAX_HANDLERS) intc_h[id].used = 0;
    return sys_end(0);
}

extern "C" int EnableIntc(int cause)   /* kc:374 */
{
    ensure();
    unsigned b = 1u << (cause & 31);
    int r = !(intc_mask & b);
    intc_mask |= b;
    return sys_end(r);
}

extern "C" int DisableIntc(int cause)   /* kc:375 */
{
    ensure();
    unsigned b = 1u << (cause & 31);
    int r = !!(intc_mask & b);
    intc_mask &= ~b;
    return sys_end(r);
}

extern "C" void SyncDCache(void *start, void *end) { (void)start; (void)end; }
