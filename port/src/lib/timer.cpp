/* src/lib/timer.cpp - EE timer 0 as the game's tick counter: init_timer0 runs the counter at BUSCLK/16 with a 60000
 * target whose interrupt counts in `timer0`, so get_timer0 = count + timer0 * 60000 increases monotonically (frame
 * pacing: Draw_FlipBuffer*, DF_WAIT_Sync, __DoStepSortie measure with it). */
#include "acnb/sdk.h"

static int timer0_handler(void)
{
    timer0 += 1;
    ps2_timer0_set_mode(ps2_timer0_mode() | 0x400);   /* acknowledge the compare interrupt (EQUF) */
    ps2_ei();
    return 0;
}

int get_timer0(void)
{
    int n = timer0;
    return (int)ps2_timer0_count() + n * 60000;
}

void clear_timer0(void)
{
    ps2_timer0_set_count(0);
    timer0 = 0;
}

void init_timer0(void)
{
    PS2_FRAME(0x10);
    ps2_timer0_set_mode(0x1c1);   /* BUSCLK/16, zero return on target, compare interrupt, counting */
    ps2_timer0_set_target(60000);
    AddIntcHandler(9, GFN(timer0_handler), -1);
    EnableIntc(9);
}
