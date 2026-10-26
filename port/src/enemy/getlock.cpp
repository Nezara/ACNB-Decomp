/* src/enemy/getlock.cpp (OLSORTIE) - per-frame target lock of an enemy: asks the priority-target search
 * (DFene_GetPriTarget, getpritrg.c) for the best target inside the current weapon's sight cone (fcs_tab) and keeps
 * ep->lkmp / lkpos / lkvec / lktime (lock model, its last centre, its per-frame motion, frames locked). */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"
#include <cstring>

void ene_get_lock(EnemySt *ep, StModel *mp, int wep_id)
{
    PS2_FRAME(0x80);
    EnemyDataSt *edat = ep->edat;
    EneFcsSt *fcs = &edat->fcs_tab[wep_id];   /* wep_id unchecked */
    unsigned int flags = mp->cflag & 7;
    if (!(edat->flag & 4)) flags = (flags | 0x1000) & 0xffff;
    if (!(edat->flag & 8)) flags = (flags | 0x2000) & 0xffff;
    const float k = ps2_u2f(0x3cc90fdb);       /* pi / 128 per angle unit */
    float angy = k * (float)fcs->angy;
    float angx = k * (float)fcs->angx;
    float range = (float)fcs->range;
    /* handed to the interpreted DFene_GetPriTarget: at their original slots */
    PS2_STACK(float[4], center, 0x70);
    PS2_STACK(float[4], vec, 0x60);
    DF_GetModelCenter(center, mp);
    vec[0] = ep->view.angx;
    vec[1] = ep->view.angy;
    ((int *)vec)[2] = 0;
    vec[3] = 1.0f;
    StModel *t = DFene_GetPriTarget(center, vec, range, angx, angy, mp, (unsigned short)flags, ep->lkmp, ep->trg_mp, 0);
    if (t) {
        if (t == ep->lkmp) {
            alignas(16) float c2[4];
            DF_GetModelCenter(c2, t);
            vu_sub_xyz(ep->lkvec, c2, ep->lkpos);   /* w = c2.w */
            memcpy(ep->lkpos, c2, 16);
            if (ep->lktime < 0xff) ep->lktime++;
            return;
        }
        DF_GetModelCenter(ep->lkpos, t);            /* new target: lkvec is not recomputed */
    }
    ep->old_lkmp = ep->lkmp;
    ep->lkmp = t;
    ep->lktime = 0;
}
