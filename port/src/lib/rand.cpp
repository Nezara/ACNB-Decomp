/* lib/rand.cpp — float / array helpers on newlib's rand(): a uniform float in [0, 1) from the low 24 bits, a random
 * point in a box (low 16 bits / 65535, so 1.0 is reachable) and a Fisher-Yates permutation of 1..num. */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

/* lo + (hi - lo) * (rand16 / 65535); lo / hi of an axis are read before its rand() call; loc[3] = 1 */
void random_region_fv(float *loc, float *min, float *max)
{
    PS2_FRAME(0x50);
    for (int a = 0; a < 3; a++) {
        float lo = min[a];
        float hi = max[a];
        int r = ps2_rand();
        float q = (float)(r & 0xffff) / 65535.0f;
        loc[a] = lo + (hi - lo) * q;
    }
    loc[3] = 1.0f;
}

/* tab = 1..num shuffled. (The original also does memcpy(tab, tab, num * 4): a no-op, left out.) */
void randperm(int *tab, int num)
{
    for (int i = 0; i < num; i++) tab[i] = i + 1;
    for (int i = num - 1; i > 0; i--) {
        int r = ps2_rand();
        int t = tab[i];
        float f = (float)(r & 0xffffff) / 16777216.0f;
        int k = ps2_ftoi((float)(i + 1) * f);
        tab[i] = tab[k];
        tab[k] = t;
    }
}

float randf(void)
{
    int r = ps2_rand();
    return (float)(r & 0xffffff) / 16777216.0f;
}
