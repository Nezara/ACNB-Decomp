/* src/event/ptemp.cpp - point heat sources (PTemp[64], set up by stage events and effects). A model's air temperature
 * is the base temperature of the map part it stands on plus each active source's temperature, weighted by
 * (range^2 - dist^2) / range^2 clamped to [0, 1], then smoothed 30/70 with the previous value. Slot 0 is never used. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"

void DF_PTEMP_CalcAirTemp(StModel *mp)
{
    _map_model_list_st *ml = mp->nowparent->mlist;
    if (!ml) return;
    PS2_FRAME(0x60);   /* the original's loc / vec / temp live on the PS2 stack (dead-stack footprint) */
    float *temp_tab = StgInfo.temp;
    PS2_STACK(float, temp, 0x5c);
    PS2_STACK(float[4], loc, 0x30);
    PS2_STACK(float[4], vec, 0x40);
    temp = temp_tab[ml->tempid];
    memcpy(loc, mp->loc, 16);
    for (int i = 1; i < 64; i++) {
        struct_5eb2b8e4 *tp = &PTemp[i];
        if (!tp->on) continue;
        vec[0] = loc[0] - tp->loc[0];   /* vsub.xyz + sqc2: w keeps loc's w */
        vec[1] = loc[1] - tp->loc[1];
        vec[2] = loc[2] - tp->loc[2];
        vec[3] = loc[3];
        float R = tp->range * tp->range;
        float fwk = ps2_div(R - ((vec[0] * vec[0] + vec[1] * vec[1]) + vec[2] * vec[2]), R);
        if (fwk < 0.0f) fwk = 0.0f;
        if (!(fwk <= 1.0f)) fwk = 1.0f;
        temp = temp + fwk * tp->temp;
        /* SyncDCache(&temp, &temp + 1): no-op on the host */
    }
    float bk = 0.7f * mp->airtempbk;   /* mula.s / madd.s: products rounded, then added */
    float t = bk + 0.3f * temp;
    mp->airtemp = t;
    mp->airtempbk = t;
}

void PTEMP_Reset(void)
{
    ZeroMemory32b(PTemp, 0x400);
}
