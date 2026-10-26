/* src/enemy/enefight.cpp (OLSORTIE) - per-frame update of the (single-player) enemy array: DF_FIGHT_FightProc runs
 * each active enemy's logic, collects the enemies processed this frame in CurFrameEnemy[] and every 600 frames
 * removes enemies outside the stage's vertical bound; DF_FIGHT_UpdateAllEnemyMatrix calls their matrix callbacks.
 * Versus has no enemies (StgInfo.cnum == 0): only the loop heads run. */
#include <cfloat>
#include "acnb/port.h"

void DF_FIGHT_UpdateAllEnemyMatrix(void)
{
    PS2_FRAME(0x30);
    if (g_sortie.pwork->isPlayPolyg == 1) return;
    EnemySt **pp = CurFrameEnemy;
    for (int n = CurFrameEnemyNum; n != 0; n--) {
        EnemySt *e = *pp++;
        if (e->updmatrix) gcall(e->updmatrix, e->mp);
    }
}

static inline void enefight_hide(EnemySt *e)
{
    /* three separate halfword read-modify-writes in the original */
    e->mp->use |= 4;
    e->mp->use |= 2;
    e->mp->use |= 1;
}

void DF_FIGHT_FightProc(void)
{
    PS2_FRAME(0x60);
    DFene_DecMisReload();
    CurFrameEnemyNum = 0;
    int n = StgInfo.cnum;
    if (g_sortie.pwork->isPlayPolyg) return;
    EnemySt *e = Enemy;
    for (int i = 0; i < n; i++, e++) {
        e->mp->yGround = FLT_MAX;
        if (e->use & 0x80) continue;
        e->mp->on_layer = e->mp->nowparent->on_layer;
        if (e->mp->on_layer == 0) {
            DFene_SEStop(e, -1);
            if (!(e->mp->use & 0x200) && !(e->use & 0x40)) continue;
        }
        if (!e->initialize) continue;
        if (!(e->use & 4)) DF_EnemyProc(e);
        CurFrameEnemy[CurFrameEnemyNum] = e;
        CurFrameEnemyNum++;
        if (e->use & 0x80) enefight_hide(e);
    }
    if (FRAME % 600 != 0) return;
    float lim = Array->head.sysinfo.rgnover[0][1];
    {
        /* dead VU add of marker 0's location and a wk vector (vadd.xyz + sqc2 to sp+0x50), never read */
        PS2_STACK(float[4], dead, 0x50);
        MarkerSt *mk = &Array->marker[0];
        const float *w = (const float *)&mk->wk[0x10];
        float v[4] = { mk->loc[0] + w[0], mk->loc[1] + w[1], mk->loc[2] + w[2], mk->loc[3] };
        PS2_VF_MIRROR(17, w);
        PS2_VF_MIRROR(16, v);
        memcpy(dead, v, 16);
    }
    e = Enemy;
    for (int i = 0; i < n; i++, e++) {
        if (e->use & 0x80) continue;
        if (e->mp->use & 4) continue;
        if (e->mp->loc[1] <= lim) continue;   /* c.ole: NaN kills */
        e->use |= 0x80;
        enefight_hide(e);
        if (!(e->mp->cflag & 1)) Info_DecData(3, 7, 0, 0);
    }
}
