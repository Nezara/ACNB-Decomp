/* src/enemy/enehit.cpp (OLSORTIE) - enemy hit helpers. garage3 layer 2: ene_get_water_high (water surface height
 * above a model, for the walk sound / spark code; loc.y + 1e6 when there is no water). The rest of the file stays
 * interpreted for now. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include <cstring>

float ene_get_water_high(StModel *mp, float *loc)
{
    PS2_FRAME(0x40);
    alignas(16) float pos[4];
    PS2_STACK(float, y, 0x3c);   /* HitBoxHitProcWater's output slot, read only on a hit */
    memcpy(pos, loc, 16);
    pos[1] = pos[1] + 2.0f;
    if (HitBoxHitProcWater(mp, pos, mp->tall, mp->R, &y)) return y;
    return ps2_u2f(0x49742400) /* 1e6 */ + loc[1];
}
