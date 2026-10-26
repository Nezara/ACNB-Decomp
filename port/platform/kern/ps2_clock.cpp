/* Virtual EE clock + timer 0 (see ps2_clock.h). "hw:N" = port/runtime/hw.c line N, whose arithmetic this copies. */
#include "ps2_clock.h"

namespace {

constexpr uint64_t T_NEVER = ~0ull;
struct Timer { uint64_t t0, next; uint32_t mode, comp; };

uint64_t g_now;
Timer tim = {0, T_NEVER, 0, 0};
unsigned g_field;

uint64_t tdiv(uint32_t mode)   /* hw:43-52 */
{
    switch (mode & 3) {
    case 0: return 2;
    case 1: return 32;
    case 2: return 512;
    default: return PS2C_HBLANK_CYCLES;
    }
}

uint32_t tcount()   /* hw:54-61 */
{
    if (!(tim.mode & 0x80)) return (uint32_t)(tim.t0 & 0xffff);   /* stopped: t0 holds the frozen count */
    uint64_t ticks = (g_now - tim.t0) / tdiv(tim.mode);
    if ((tim.mode & 0x40) && tim.comp) ticks %= (uint64_t)tim.comp + 1;
    return (uint32_t)(ticks & 0xffff);
}

void tsched()   /* hw:63-75 */
{
    tim.next = T_NEVER;
    if (!(tim.mode & 0x80) || !(tim.mode & 0x300)) return;
    uint64_t d = tdiv(tim.mode), now = g_now;
    uint64_t ticks = (now - tim.t0) / d;
    uint64_t period = (tim.mode & 0x40) ? (uint64_t)tim.comp + 1 : 0x10000;
    uint64_t k = ticks / period;
    uint64_t tgt = (tim.mode & 0x100) ? (uint64_t)tim.comp : 0xffff;
    uint64_t when = tim.t0 + (k * period + tgt) * d;
    if (when <= now) when += period * d;
    tim.next = when;
}

/* a due compare / overflow (hw:134-140): flag, reschedule, then the interrupt (its handler runs at g_now) */
void tevent(void (*raise)(int))
{
    tim.mode |= (tim.mode & 0x100) ? 0x400 : 0x800;
    tsched();
    if (raise) raise(PS2C_INTC_TIM0);
}

}  // namespace

extern "C" uint64_t ps2c_now(void) { return g_now; }

extern "C" unsigned ps2c_timer0_read(unsigned reg)
{
    switch (reg) {
    case 0x00: return tcount();
    case 0x10: return tim.mode;
    case 0x20: return tim.comp;
    }
    return 0;
}

extern "C" void ps2c_timer0_write(unsigned reg, unsigned v)   /* hw:77-97 */
{
    switch (reg) {
    case 0x00:   /* COUNT */
        if (tim.mode & 0x80) tim.t0 = g_now - (uint64_t)(v & 0xffff) * tdiv(tim.mode);
        else tim.t0 = v & 0xffff;
        break;
    case 0x10: { /* MODE: writing 1 to EQUF / OVFF clears them */
        uint32_t cnt = tcount();
        uint32_t flags = (tim.mode & 0xc00) & ~(v & 0xc00);
        tim.mode = (v & 0x3ff) | flags;
        if (tim.mode & 0x80) tim.t0 = g_now - (uint64_t)cnt * tdiv(tim.mode);
        else tim.t0 = cnt;
        break;
    }
    case 0x20: tim.comp = v & 0xffff; break;
    }
    tsched();
}

extern "C" void ps2c_advance_to(uint64_t t, int busy, void (*raise)(int cause))
{
    if (t < g_now) t = g_now;
    if (!busy) {   /* kernel.c schedule: vtime_skip_to(hw_next_event()) + service_irqs, one event at a time */
        while (tim.next != T_NEVER && tim.next <= t) {
            g_now = tim.next;
            tevent(raise);
        }
        g_now = t;
        return;
    }
    g_now = t;   /* gs_priv_read: vtime_skip_to(next_vbs); hw_update() - each timer checked once */
    if (tim.next != T_NEVER && g_now >= tim.next) tevent(raise);
}

extern "C" unsigned ps2c_gs_field(void) { return g_field; }
extern "C" void ps2c_toggle_field(void) { g_field ^= 1; }

extern "C" void ps2c_reset(void)
{
    g_now = 0;
    tim = Timer{0, T_NEVER, 0, 0};
    g_field = 0;
}
