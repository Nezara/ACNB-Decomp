/* src/enemy/ene093.cpp (OLSORTIE) - enemy type 93, a standing gatling gunner on the ene120 / e094 engine (record
 * e232; sandbox usable roster). Ene093_Initial = Ene120_Initial with its own main / think; e093_main = e120_main that
 * always targets player 0 (no ene_select_target); e093_think = e120_think with a line-of-sight gate
 * (HetProcPoint2Point) on the weapon choice and the attack. Control is e094_control (ene094.cpp). */
#include "acnb/port.h"

void e093_think(EnemySt *ep, StModel *mp)
{
    PS2_FRAME(0xe0);
    PS2_STACK(EneInfoSt, eneinfo, 0x50);   /* only partly written by ene_get_eneinfo_basic (as e120_think) */
    PS2_STACK(float[4], dest, 0x40);
    EnemyDataSt *edat = ep->edat;
    ep->new_pad = 0;
    ene_get_eneinfo_basic(ep, &eneinfo, ep->trg_mp);
    ene_select_operation2(ep, &eneinfo);
    /* trg_mp + 0x10 without a null test: a null target reads PS2 address 0x10 */
    StModel *t = ep->trg_mp;
    float *tloc = t ? t->loc : (float *)((char *)PS2_NULL_MEM() + 0x10);
    if (!HetProcPoint2Point(mp->loc, tloc, dest, 0)) {
        e120_select_weapon(ep, edat, &eneinfo);
        ene_attack(ep, &eneinfo);
    }
    ene_blade(ep, &eneinfo);
    ene_execute_operation(ep, mp, &eneinfo);
}

void e093_main(EnemySt *ep, StModel *mp)
{
    PS2_FRAME(0x40);
    EnemyDataSt *edat = ep->edat;
    ep->trg_mp = g_ac[0].mp;                 /* always player 0, regardless of team / range */
    if (ep->thinkfunc && ep->sw.think) gcall(ep->thinkfunc, ep, mp);
    enepad_PadManage(ep);
    ene_get_lock(ep, mp, ep->cur_wep);
    if (ep->ctrlfunc) gcall(ep->ctrlfunc, ep, mp);
    if (edat->leg_type != 2) common_spark_mng(ep, mp, 2, 0);
    else enesfx_tank_spark(ep);
    if (ep->acttime == 0 && edat->sfx[2] > 0 && ep->h_sfx2[0] == 0)
        ep->h_sfx2[0] = (unsigned short)DFene_SFX_flag(ep, edat->sfx[2], 10, 4);
    ene_wep_mng(ep, mp);
    ep->old_pad = ep->new_pad;
    ep->new_pad = 0;
}

void Ene093_Initial(EnemySt *ep, StModel *mp)
{
    PS2_FRAME(0x40);
    EnemyDataSt *edat = ep->edat;
    ep->dir = 8;
    ep->bst = 0;
    ene_set_think(ep);
    ep->ctrlfunc = GFN(e094_control);    /* another type's control function: original choice */
    ep->deadfunc = GFN(ene_dead);
    DFene_SetFunc(ep, (void *)GFN(e093_main));
    ep->thinkfunc = GFN(e093_think);     /* after DFene_SetFunc: overrides ene_set_think's choice */
    ene_boost_sfx_init(ep, mp, edat->sfx[0], edat->sfx[1]);
    if (edat->flag & 0x80) ep->chkfloor = 0;
}
