/* lib/fslibmpg2_dd/fslibmpg2.cpp - FromSoftware's MPEG2 wrapper over the PSS player (only the sizing call is native;
 * the rest is phase 3 step 6). */
#include "acnb/port.h"
#include "acnb/sdk.h"

int fsmpg_GetRequiredBufferSize(unsigned short video, unsigned short digital)
{
    PS2_FRAME(0x30);
    return mpg_GetRequiredBufferSize(video, digital);
}
