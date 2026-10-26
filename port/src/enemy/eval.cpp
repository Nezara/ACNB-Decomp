/* src/enemy/eval.cpp (OLSORTIE) - small scoring curves for the enemy operation selector: enefz_time (bonus that
 * falls from 2 to 1 while an operation's minimum time runs out) and eval_near (linear falloff, 1 inside min, 0 at
 * max), eval_far (its mirror, sandbox gE). garage3 layer 2 + gE; the file's other evaluators are not reached yet. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"

float enefz_time(int time_now, int time_all)
{
    if (!(time_now < time_all)) return 1.0f;
    float q = ps2_div((float)(time_all - time_now), (float)time_all);
    return 1.0f + ps2_sqrt(q);
}

float eval_near(float val, float min, float max)
{
    if (max < min) { float t = min; min = max; max = t; }
    if (!(val < max)) return 0.0f;          /* NaN -> 0 */
    if (val <= min) return 1.0f;
    return ps2_div(val - max, min - max);
}

/* Linear rise: 0 up to min, 1 from max on (sandbox gE; the mirror of eval_near, bounds swapped if reversed) */
float eval_far(float val, float min, float max)
{
    if (max < min) { float t = min; min = max; max = t; }
    if (!(val < max)) return 1.0f;          /* NaN -> 1 */
    if (val <= min) return 0.0f;
    return ps2_div(val - min, max - min);
}
