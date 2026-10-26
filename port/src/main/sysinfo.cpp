/* src/main/sysinfo.cpp - the memory layout main state 0 asks for (main() discards both results): ps2GetSystemInfo
 * fills end of the program image / stack size / heap size from the linker symbols (_end 0x1efc500, _stack_size
 * 0x80000, _stack 0x1f80000; the "_stack < 0" arm = stack at the top of 128 MB is dead), FullAllocAndFree mallocs
 * everything between _end + stack + 2 pages and 128 MB once and frees it (newlib heap coalescing).
 * Original quirk kept: FullAllocAndFree sizes from 128 MB (the devkit) - on a 32 MB console the 0x6081af0-byte malloc
 * fails and the free(NULL) does nothing. Hybrid: the PS2 newlib heap; source port: the host heap (no effect). */
#include "acnb/port.h"
#include "acnb/sdk.h"

#define SYSINFO_END        0x1efc500   /* _end */
#define SYSINFO_STACK_SIZE 0x80000     /* _stack_size */
#define SYSINFO_STACK      0x1f80000   /* _stack */

int ps2GetSystemInfo(struct struct_6f8e3b2c *info)
{
    if (info == 0) return -1;
    int stack = SYSINFO_STACK;
    if (stack < 0) stack = 0x8000000 - (SYSINFO_STACK_SIZE + 0x1000);
    info->end = SYSINFO_END;
    info->stack_size = SYSINFO_STACK_SIZE;
    info->heap_size = stack - SYSINFO_END;
    return 0;
}

int FullAllocAndFree(void)
{
    PS2_FRAME(0x20);
    int size = 0x8000000 - (SYSINFO_END + (SYSINFO_STACK_SIZE + 0x1000) + 0x1000);
    ps2_free(ps2_malloc((unsigned int)(size - 0x10)));
    return size - 0x10;
}
