/* src/enemy/enereg.cpp (OLSORTIE) - enemy patrol regions: tests a position against an xz rectangle. garage3 layer 2:
 * DFene_CheckInRegionEx (which sides the position is outside of); the region setters stay interpreted for now. */
#include "acnb/port.h"

int DFene_CheckInRegionEx(float *loc, float xmin, float xmax, float zmin, float zmax)
{
    int r = 0;
    float x = loc[0];
    if (x < xmin) r |= 1;
    if (!(x <= xmax)) r |= 2;               /* NaN sets the 2 and 8 bits */
    float z = loc[2];
    if (z < zmin) r |= 4;
    if (!(z <= zmax)) r |= 8;
    return r;
}
