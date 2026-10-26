/* src/event/event_job.cpp (OLSORTIE) - stage event job helpers. Only EventJob_SetQuickStart runs in Versus (stage
 * event setup): it forwards the next stage's quick-start flag into event work slot 0x3f. */
#include "acnb/port.h"

void EventJob_SetQuickStart(void)
{
    PS2_FRAME(0x10);
    if (g_nextstage.quickStart) EVMNG_SetEvwkData(0x3f, 1);
}
