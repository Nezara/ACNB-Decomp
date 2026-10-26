/* src/enemy/seltrg.cpp (OLSORTIE) - per-frame target selection at the head of every enemy main: a countdown
 * ep->trg_cnt runs while ep->trg_mp is alive; at 0 the enemy takes its "base target" (probability base_prob within
 * base_range) or searches its sight cone with DFene_GetPriTarget and re-arms the countdown (180 / 220 / 260 / 300
 * frames, one ps2_rand draw). Damage from a shooter of another team retargets to that shooter (one more draw).
 * Also sets sw.encount (a target exists) and sw.encount2 (hit by another team). The draw order is sim-visible. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"

#define SELT_K 0x1.921fb6p-6f   /* 0x3cc90fdb pi / 128 per angle unit */

/* the original's inlined "target is dead" test (types other than 1 / 2 / 3 count as alive) */
static inline int seltrg_dead(StModel *t)
{
    switch (t->type) {
    case 1:
    case 2:
        return (t->use & 4) != 0;
    case 3:
        return !(((EnemySt *)t->arg0p)->nowhp > 0);
    default:
        return 0;
    }
}

static inline unsigned short seltrg_rand_cnt(void)
{
    unsigned int r = (unsigned int)ps2_rand() & 0xffff;
    return (unsigned short)((r >> 14) * 40 + 0xb4);
}

void ene_select_target(EnemySt *ep, StModel *mp)
{
    PS2_FRAME(0x80);
    EnemyDataSt *edat = ep->edat;
    EnemyLogicSt *lg = (EnemyLogicSt *)((char *)ep->logp + (ep->p_lv << 8));
    if (ep->trg_cnt > 0) {
        ep->trg_cnt = (unsigned short)(ep->trg_cnt - 1);
        if (ep->trg_mp && seltrg_dead(ep->trg_mp)) ep->trg_cnt = 0;   /* trg_mp is kept */
        goto tail;
    }
    if (ep->base_trg && !seltrg_dead(ep->base_trg)) {
        float range = ep->base_range;
        int in;
        if (range < 0.0f) in = 1;
        else in = DF_CALC_GetDist_xz(ep->mp->loc, ep->base_trg->loc) <= range;
        if (in && ps2_rand() % 100 < (int)ep->base_prob) {
            ep->trg_mp = ep->base_trg;
            ep->trg_cnt = seltrg_rand_cnt();
            goto tail;
        }
    }
    {
        PS2_STACK(float[4], ctr, 0x70);
        PS2_STACK(float[4], ang, 0x60);
        DF_GetModelCenter(ctr, mp);
        ang[0] = ep->view.angx;
        ang[1] = ep->view.angy;
        ((int *)ang)[3] = 0x3f800000;
        ((int *)ang)[2] = 0;
        StModel *t = DFene_GetPriTarget(ctr, ang, lg->fnd_range, SELT_K * lg->fnd_angx, SELT_K * lg->fnd_angy, mp,
                                        (unsigned short)(mp->cflag & 7), ep->trg_mp, NULL, (edat->flag & 0x8000) != 0);
        if (t) {
            ep->trg_cnt = seltrg_rand_cnt();
        } else if (ep->base_trg) {
            t = ep->base_trg;
            ep->trg_cnt = seltrg_rand_cnt();
        }
        ep->trg_mp = t;
    }
tail:
    if (!ep->sw.encount && ep->trg_mp) ep->sw.encount = 1;
    if (ep->bmd_dmg > 0) {
        StModel *m = SPELL_Shooter2Model((ep->bmd_id >> 14) & 3, ep->bmd_id & 0x3fff);
        if (m && (m->cflag & (mp->cflag & 7)) == 0) {
            ep->trg_mp = m;
            ep->trg_cnt = seltrg_rand_cnt();
            if (!ep->sw.encount2) ep->sw.encount2 = 1;
        }
    }
}
