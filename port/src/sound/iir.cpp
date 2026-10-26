/* src/sound/iir.cpp - IIR filter reset for the ADS PCM filter: clears both delay lines. Only Ads_Play calls it,
 * behind a filter pointer this build never sets. */
#include <string.h>

int IIR_FilterInitialize(struct_1e9bef24 *pFilter)
{
    PS2_FRAME(0x10);
    memset(pFilter->aDelayLine, 0, 0x20);
    return 0;
}
