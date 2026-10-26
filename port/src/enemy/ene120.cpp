/* src/enemy/ene120.cpp (OLSORTIE) - enemy type 120, the generic "standard" enemy (also set up by Ene004_Init /
 * Ene121_Initial; the garage3 test-room targets are this type). Ene120_Initial installs the control / dead / main
 * callbacks and the boost effects; e120_select_weapon (shared by many think functions) re-chooses the weapon every
 * 200 frames; e120_think (target info, operation, weapon, attack, blade, move) and e120_main (the per-frame main
 * proc: target, think, pad, lock, control, sparks, weapon) are garage3 layer 2. */
#include "acnb/port.h"

void e120_think(EnemySt *ep, StModel *mp)
{
    PS2_FRAME(0xd0);
    PS2_STACK(EneInfoSt, eip, 0x40);   /* handed to interpreted code, only partly written */
    EnemyDataSt *edat = ep->edat;
    ep->new_pad = 0;
    ene_get_eneinfo_basic(ep, &eip, ep->trg_mp);
    ene_select_operation2(ep, &eip);
    e120_select_weapon(ep, edat, &eip);
    ene_attack(ep, &eip);
    ene_blade(ep, &eip);
    ene_execute_operation(ep, mp, &eip);
}

void e120_main(EnemySt *ep, StModel *mp)
{
    PS2_FRAME(0x40);
    EnemyDataSt *edat = ep->edat;
    ene_select_target(ep, mp);
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

void e120_select_weapon(EnemySt *ep, EnemyDataSt *edat, EneInfoSt *eip)
{
    PS2_FRAME(0x20);
    if (!eip->emp) return;
    char b = ep->bst;
    if (b == 0x17 || b == 0x14 || b == 0x19) return;
    if (ep->wep_time > 0) { ep->wep_time--; return; }
    if (ep->am_time > 0) return;
    int w = default_select_weapon(ep, edat, eip);
    if (w == -1) { ep->wep_valid &= 0xfe; return; }   /* wep_time stays 0: asked again next frame */
    ep->wep_valid |= 1;
    char c = ep->cur_wep;
    if (c != w) { ep->old_wep = c; ep->cur_wep = (char)w; }
    ep->wep_time = 200;
}

void Ene120_Initial(EnemySt *ep, StModel *mp)
{
    PS2_FRAME(0x40);
    EnemyDataSt *edat = ep->edat;
    ep->dir = 8;
    ep->bst = 0;
    ene_set_think(ep);
    ep->ctrlfunc = GFN(e094_control);    /* another type's control function: original choice */
    ep->deadfunc = GFN(ene_dead);
    DFene_SetFunc(ep, (void *)GFN(e120_main));
    ene_boost_sfx_init(ep, mp, edat->sfx[0], edat->sfx[1]);
    if (edat->flag & 0x80) ep->chkfloor = 0;
}
