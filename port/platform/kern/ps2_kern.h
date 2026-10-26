/* Native EE kernel for the source port. Source port only (the hybrid keeps
 * port/runtime/kernel.c); the behaviour copies kernel.c so both builds schedule the game's threads identically.
 *
 * Model: one host fiber per PS2 thread (port/platform/kern/fiber.h), all on the host thread that runs the game; the
 * caller of ps2k_init / the game's main() is thread 1. Strict priority, no time slicing: lower number wins, FIFO by
 * seq within a priority (seq = order of becoming ready; ChangeThreadPriority bumps it; a preempted thread keeps its
 * seq). Switches only at: block (WaitSema / SleepThread / ps2k_wait_vblank), exit, and the end of a kernel call that
 * made a better thread ready (SignalSema / WakeupThread / StartThread / ChangeThreadPriority / EIntr ...), exactly where
 * kernel.c's ee_syscall runs maybe_preempt. A SignalSema that wakes a better ThreTask / aio worker runs the worker
 * inside the call, to its next block, before the call returns.
 *
 * Interrupt / host context (ps2k_irq_*): host code (sndhost's posted hook, the idle hook) wakes threads with
 * ps2k_irq_wakeup / ps2k_irq_signal; those never switch fibers (sndhost.h hazard). The switch happens at the next
 * kernel call or ps2k_preempt_point. DIntr / EIntr track Status.EIE;
 * EIE off defers only ps2k_preempt_point (the asynchronous point), kernel calls preempt regardless, as kernel.c.
 *
 * Idle (for B3): when no thread is ready the kernel steps virtual time one vblank: it calls the idle hook (B3: present,
 * sndhost advance, EE RPC wakeups), then readies every thread blocked in ps2k_wait_vblank. If no thread can ever become
 * ready (no vblank waiter and: no hook, or the hook returns 0, or 3600 idle vblanks without a wakeup) the kernel prints
 * a deadlock report (every thread's state / prio / wait object) and exits with code 3.
 * ACNB_KERNEL_LOG=1: one line per thread switch (as kernel.c's "kthread: a -> b vb N"). */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* the kernel's parameter blocks as the game fills them (acnb/types.h ThreadParam / SemaParam layout). Read fields:
 * entry, initPriority (thread); initCount, maxCount (sema). status / stack / attr / option / wait words: stale stack
 * in the original callers, never used. */
struct __attribute__((packed, aligned(4))) Ps2kThreadParam {
    int status;
    void (*entry)(void *);
    void *stack;
    int stackSize;
    void *gpReg;
    int initPriority;
    int currentPriority;
    unsigned int attr;
    unsigned int option;
    int waitType;
    int waitId;
    int wakeupCount;
};
struct __attribute__((packed, aligned(4))) Ps2kSemaParam {
    int currentCount;
    int maxCount;
    int initCount;
    int numWaitThreads;
    unsigned int attr;
    unsigned int option;
};

/* ---- the game-facing names (acnb/sdk.h; return values = kernel.c's) */
int  ps2_CreateThread(void *param);              /* id > 0, -1 = no slot */
int  ps2_StartThread(int tid, void *arg);        /* 0; -1 = no such thread / not dormant. Re-arms prio and wakeups */
void ps2_ExitThread(void);
void ps2_ExitDeleteThread(void);                 /* = ExitThread in kernel.c: the slot stays (ids are not reused) */
int  ps2_TerminateThread(int tid);               /* 0 (self: no-op) */
int  ps2_DeleteThread(int tid);                  /* 0 (frees the slot of a dormant thread that is not the caller) */
int  ps2_ChangeThreadPriority(int tid, int prio);/* old prio; tid 0 = self; -1 = no such thread */
int  ps2_GetThreadId(void);
int  ps2_SleepThread(void);                      /* 0 */
int  ps2_WakeupThread(int tid);                  /* 0, -1 = no such thread */
int  ps2_iWakeupThread(int tid);
int  ps2_CancelWakeupThread(int tid);            /* the pending wakeup count (tid 0 = self) */
int  ps2_iCancelWakeupThread(int tid);
int  ps2_CreateSema(void *param);                /* id > 0 (0 is reserved), -1 = no slot */
int  ps2_DeleteSema(int sid);                    /* 0; waiters stay blocked (kernel.c) */
int  ps2_SignalSema(int sid);                    /* sid; -1 = bad id. No maxCount clamp (kernel.c) */
int  ps2_iSignalSema(int sid);
int  ps2_WaitSema(int sid);                      /* sid; -1 = bad id */
int  ps2_PollSema(int sid);                      /* sid; -419 = count 0; -1 = bad id */
int  DIntr(void);                                /* 1 if interrupts were enabled */
int  EIntr(void);                                /* 1 if interrupts were already enabled */
int  AddIntcHandler(int cause, int (*handler)(void), int next);   /* handler id (stored, never called) */
int  RemoveIntcHandler(int cause, int id);
int  EnableIntc(int cause);                      /* 1 if it was disabled */
int  DisableIntc(int cause);                     /* 1 if it was enabled */
void SyncDCache(void *start, void *end);         /* no-op: no caches between EE code and host memory */

/* ---- host side */
void ps2k_init(void);                            /* idempotent; also done lazily by the first kernel call */
typedef int (*Ps2kIdleFn)(void *user);           /* one vblank of virtual time; 0 = no time source at all */
void ps2k_set_idle_hook(Ps2kIdleFn fn, void *user);
int  ps2k_wait_vblank(void);                     /* block the caller until the next idle vblank step; 0 */
unsigned ps2k_vblank_count(void);                /* vblank steps so far (idle + busy) */
void ps2k_busy_vblank(void);                     /* the caller spins one vblank step (vsync wait); no other thread runs */
void ps2k_intc_raise(int cause);                 /* host context: run the enabled INTC handlers of cause (no switch) */
void ps2k_irq_wakeup(int tid);                   /* iWakeupThread from host / IOP context: never switches */
void ps2k_irq_signal(int sid);                   /* iSignalSema from host / IOP context: never switches */
void ps2k_preempt_point(void);                   /* switch now if an irq wakeup readied a better thread (EIE on) */
void ps2k_irq_enter(void);                       /* host code running as an interrupt handler: no switches */
void ps2k_irq_leave(void);
int  ps2k_dump(char *buf, int size);             /* thread table text (the deadlock report body); returns length */
typedef void (*Ps2kDeadlockFn)(const char *report);
void ps2k_set_deadlock_hook(Ps2kDeadlockFn fn);  /* default 0: print the report to stderr, exit(3) */

/* ---- selftest support */
void ps2k_reset(void);                           /* the calling fiber becomes thread 1 (prio 0); all else dropped */
int  ps2k_thread_state(int tid, int *prio, int *wait_type, int *wait_id, int *wakeup);  /* status, 0 = no thread */
int  ps2k_sema_count(int sid);                   /* count, -1 = no sema */
enum { PS2K_RUN = 1, PS2K_READY = 2, PS2K_WAIT = 4, PS2K_DORMANT = 16 };
enum { PS2K_WT_NONE = 0, PS2K_WT_SLEEP = 1, PS2K_WT_SEMA = 2, PS2K_WT_VBLANK = 3 };

#ifdef __cplusplus
}
#endif
