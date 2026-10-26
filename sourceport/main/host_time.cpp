/* Platform side: host monotonic clock for the PS2 timer stand-ins of acnb/port.h (EE timer 0). QueryPerformanceCounter
 * (std::chrono pulled libc++.dll into acnb.exe, so it did not start outside the toolchain's PATH). */
#include <windows.h>

extern "C" unsigned long long acnb_host_ns(void)
{
    static LARGE_INTEGER freq;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    unsigned long long q = (unsigned long long)t.QuadPart, f = (unsigned long long)freq.QuadPart;
    return q / f * 1000000000ull + q % f * 1000000000ull / f;
}
