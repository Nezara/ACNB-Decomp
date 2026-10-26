/* Host coroutines for the EE kernel (port/platform/kern/ps2_kern.cpp): one fiber per PS2 thread, all on one host thread.
 * Four calls so a port can swap the backend (Win32 fibers today; ucontext on POSIX; assembly switch later).
 * Floating-point control state is shared by all fibers (no FIBER_FLAG_FLOAT_SWITCH, as runtime/kernel.c: the FTZ / DAZ
 * setting of the host thread applies to every PS2 thread). port/platform/snd/iop/iopk.cpp keeps its own fiber code
 * (it also has a thread backend and re-reads GetCurrentFiber at each host entry, so it nests inside any EE fiber). */
#pragma once
#include <stddef.h>

namespace hostfiber {

struct Fiber;

Fiber *current();                                            /* the running fiber (the host thread on first use) */
Fiber *create(size_t stack, void (*fn)(void *), void *arg);  /* 0 = failed; fn must never return */
void switch_to(Fiber *f);                                    /* returns when someone switches back to the caller */
void destroy(Fiber *f);                                      /* never the running fiber; its stack is dropped unwound */

}  // namespace hostfiber
