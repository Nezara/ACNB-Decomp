/* src/enemy/getpritrg.cpp (OLSORTIE) - "get priority target": the enemy sight-cone target search. search_lock
 * collects every candidate (the forced object objmp, the Chr[] character models, the live ACs of g_ac[]) that passes
 * chk_in_sight (a box, then a horizontal range, then yaw / pitch against the sight angles) into lock_buf;
 * DFene_GetPriTarget keeps the one with the lowest exp_std_rader score (angle + distance weights, the current lock
 * scaled by cmp_val_prev). Callers: ene_get_lock (every frame), ene_select_target (retarget). */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"
#include "acnb/sdk.h"

#define GPT_2PI   0x1.45f306p-1f    /* 0x3f22f983 2 / pi */
#define GPT_DIST  0x1.0624dep-11f   /* 0x3a03126f 1 / 2000 */

typedef struct struct_28555966 EneLockEl;

static_assert(sizeof(AcPlayerSt) == 0x2600, "g_ac stride");

#ifdef ACNB_SOURCEPORT
float cmp_val_angx = 1.0f;
float cmp_val_angy = 1.0f;
float cmp_val_dist = 1.0f;
float cmp_val_prev = 0.5f;
#endif

static float exp_std_rader(EneLockEl *elp, StModel *plkmp)
{
    float ax = elp->dangx;
    if (ax < 0.0f) ax = -ax;
    float ay = elp->dangy;
    if (ay < 0.0f) ay = -ay;
    ax = ax * GPT_2PI;
    if (!(ax <= 1.0f)) ax = 1.0f;
    float d = elp->dist;
    ay = ay * GPT_2PI;
    if (!(ax <= 1.0f)) ax = 1.0f;          /* original: re-tests ax, ay is never clamped */
    d = d * GPT_DIST;
    if (!(d <= 1.0f)) d = 1.0f;
    float px = cmp_val_angx * ax;
    float py = cmp_val_angy * ay;
    float acc = px + py;                   /* adda.s */
    float pd = cmp_val_dist * d;
    float r = acc + pd;                    /* madd.s */
    if (elp->mp == plkmp) r = r * cmp_val_prev;
    return r;
}

static unsigned int chk_in_sight(float *tloc, float *loc, float *vang, float wrange, float wangx, float wangy,
                                 EneLockEl *elp)
{
    PS2_FRAME(0x50);
    alignas(16) float d[4];
    d[3] = tloc[3];
    vu_sub_xyz(d, tloc, loc);
    float nr = -wrange;
    if (!(d[0] <= wrange)) return 1;
    if (d[0] < nr) return 1;
    if (!(d[1] <= wrange)) return 1;
    if (d[1] < nr) return 1;
    if (!(d[2] <= wrange)) return 1;
    if (d[2] < nr) return 1;
    float r2 = wrange * wrange;
    float xx = d[0] * d[0];
    float zz = d[2] * d[2];
    float h2 = xx + zz;                    /* mula.s / madd.s */
    if (!(h2 <= r2)) return 2;
    float ay = sub_angf(ps2_atan2f(d[0], d[2]), vang[1]);
    if (ay < -wangy) return 4;
    if (!(ay <= wangy)) return 4;
    float len = ps2_sqrt(h2);
    float ax = sub_angf(ps2_atan2f(-d[1], len), vang[0]);
    if (ax < -wangx) return 8;
    if (!(ax <= wangx)) return 8;
    if (elp) {
        elp->dist = len;
        elp->dangx = ax;
        elp->dangy = ay;
    }
    return 0;
}

static int search_lock(EneLockEl *bufp, int elcnt, float *sloc, float *sang, float range, float angx, float angy,
                       StModel *mymp, unsigned short cmask, StModel *objmp, unsigned int option)
{
    PS2_FRAME(0x160);
    if (elcnt <= 0) return 0;
    EneLockEl *p = bufp;
    bufp->mp = NULL;
    unsigned int team = (unsigned short)(cmask | 0x8000);
    alignas(16) float c[4];
    alignas(16) float hv[4];
    if (objmp && (objmp->type == 1 || objmp->type == 2) && !(objmp->use & 4) && !(objmp->cflag & team)) {
        DF_GetModelCenter(c, objmp);
        int ok = chk_in_sight(c, sloc, sang, range, angx, angy, bufp) == 0;
        if (ok && (option & 1) && DF_HIT_HitProc_2Point(sloc, c, hv)) ok = 0;
        if (ok) {
            elcnt--;
            p = bufp + 1;
            bufp->mp = objmp;
            if (elcnt > 0) p->mp = NULL;
        }
    }
    int n = StgInfo.cnum;
    StModel *mp = Chr;
    for (int i = 0; i < n; i++, mp = (StModel *)((char *)mp + 0x180)) {
        if (elcnt <= 0) break;
        if (mp == mymp) continue;
        if (mp->use & 4) continue;
        if (mp->cflag & team) continue;
        DF_GetModelCenter(c, mp);
        int ok = chk_in_sight(c, sloc, sang, range, angx, angy, p) == 0;
        if (ok && (option & 1) && DF_HIT_HitProc_2Point(sloc, c, hv)) ok = 0;
        if (ok) {
            p->mp = mp;
            elcnt--;
            p++;
            if (elcnt > 0) p->mp = NULL;
        }
    }
    for (int i = 0; i < NowPlayerNum; i++) {   /* bound re-read every iteration */
        if (elcnt <= 0) break;
        AcPlayerSt *ac = &g_ac[i];
        if (ac->hp <= 0) continue;
        mp = ac->mp;
        if (mp == mymp) continue;
        if (mp->cflag & team) continue;
        if (mp->use & 1) continue;
        if (mp->use & 2) continue;
        DF_GetModelCenter(c, mp);
        int ok = chk_in_sight(c, sloc, sang, range, angx, angy, p) == 0;
        if (ok && (option & 1) && DF_HIT_HitProc_2Point(sloc, c, hv)) ok = 0;
        if (ok) {
            p->mp = mp;
            elcnt--;
            p++;
            if (elcnt > 0) p->mp = NULL;
        }
    }
    return (int)(p - bufp);
}

StModel *DFene_GetPriTarget(float *sloc, float *sang, float range, float angx, float angy, StModel *mymp,
                            unsigned short cflag, StModel *plkmp, StModel *objmp, unsigned int option)
{
    PS2_FRAME(0x20);
    lock_num = search_lock(lock_buf, 0x109, sloc, sang, range, angx, angy, mymp, cflag, objmp, option);
    int num = lock_num;
    if (num <= 0) return NULL;
    EneLockEl *best = lock_buf;
    EneLockEl *cand = lock_buf;
    for (int n = num - 1; n != 0; n--) {
        cand++;
        float fb = exp_std_rader(best, plkmp);
        float fc = exp_std_rader(cand, plkmp);
        int cmp = (fb < fc) ? -1 : (fb <= fc) ? 0 : 1;
        if (cmp > 0) best = cand;              /* lowest score wins, ties keep the earlier, NaN replaces */
    }
    return best->mp;
}
