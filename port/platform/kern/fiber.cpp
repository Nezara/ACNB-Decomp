/* hostfiber backends (see fiber.h). */
#include "fiber.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>

namespace hostfiber {

struct Fiber {
    void *h;
    void (*fn)(void *);
    void *arg;
    int owned;   /* created here (DeleteFiber on destroy); 0 = the converted host thread */
};

static Fiber g_host;
static Fiber *g_cur;

static void WINAPI trampoline(void *p)
{
    Fiber *f = (Fiber *)p;
    f->fn(f->arg);
    fprintf(stderr, "hostfiber: fiber body returned\n");
    abort();
}

Fiber *current()
{
    if (!g_cur) {
        g_host.h = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiber(0);
        if (!g_host.h) { fprintf(stderr, "hostfiber: ConvertThreadToFiber failed (%lu)\n", (unsigned long)GetLastError()); abort(); }
        g_cur = &g_host;
    }
    return g_cur;
}

Fiber *create(size_t stack, void (*fn)(void *), void *arg)
{
    current();
    Fiber *f = (Fiber *)calloc(1, sizeof *f);
    if (!f) return 0;
    f->fn = fn;
    f->arg = arg;
    f->owned = 1;
    f->h = CreateFiberEx(64 * 1024, stack, 0, trampoline, f);
    if (!f->h) { free(f); return 0; }
    return f;
}

void switch_to(Fiber *f)
{
    current();
    if (f == g_cur) return;
    g_cur = f;
    SwitchToFiber(f->h);
}

void destroy(Fiber *f)
{
    if (!f || f == g_cur || !f->owned) return;
    DeleteFiber(f->h);
    free(f);
}

}  // namespace hostfiber

#else
#include <ucontext.h>

namespace hostfiber {

struct Fiber {
    ucontext_t ctx;
    void *stack;
    void (*fn)(void *);
    void *arg;
    int owned;
};

static Fiber g_host;
static Fiber *g_cur;

static void trampoline(unsigned lo, unsigned hi)
{
    Fiber *f = (Fiber *)(size_t)((unsigned long long)hi << 32 | lo);   /* 32-bit hosts: size_t drops hi */
    f->fn(f->arg);
    fprintf(stderr, "hostfiber: fiber body returned\n");
    abort();
}

Fiber *current()
{
    if (!g_cur) g_cur = &g_host;
    return g_cur;
}

Fiber *create(size_t stack, void (*fn)(void *), void *arg)
{
    current();
    Fiber *f = (Fiber *)calloc(1, sizeof *f);
    if (!f) return 0;
    f->stack = malloc(stack);
    if (!f->stack || getcontext(&f->ctx)) { free(f->stack); free(f); return 0; }
    f->fn = fn;
    f->arg = arg;
    f->owned = 1;
    f->ctx.uc_stack.ss_sp = f->stack;
    f->ctx.uc_stack.ss_size = stack;
    f->ctx.uc_link = 0;
    unsigned long long p = (unsigned long long)(size_t)f;
    makecontext(&f->ctx, (void (*)())trampoline, 2, (unsigned)p, (unsigned)(p >> 32));
    return f;
}

void switch_to(Fiber *f)
{
    Fiber *prev = current();
    if (f == prev) return;
    g_cur = f;
    swapcontext(&prev->ctx, &f->ctx);
}

void destroy(Fiber *f)
{
    if (!f || f == g_cur || !f->owned) return;
    free(f->stack);
    free(f);
}

}  // namespace hostfiber
#endif
