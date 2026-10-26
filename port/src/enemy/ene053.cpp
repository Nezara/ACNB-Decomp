/* src/enemy/ene053.cpp (OLSORTIE) - enemy type 53, missile boxes / missile tanks (box e56 in the training maps).
 * __Ene053_Main is the per-frame main (target, think, pad, weapon change, lock, the weapon-0 missile request gated by
 * distance and the file-shared throttle ENE_mis_reload, control, move sound, weapon manager); __Ene053_Control turns,
 * dashes and moves; __Ene053_ShootControl runs the 4-state launch animation (e_wk[0].c[0] = 0..3) around ene_shoot:
 * the weapon-0 fire request only survives in state 2. */
#include "acnb/port.h"
#include "acnb/sdk.h"

static void __Ene053_ShootControl(EnemySt *ep, StModel *mp)
{
    PS2_FRAME(0x40);
    int fire = 0;
    if (!ep->trg_mp) return;
    if (ep->cur_wep < 0) return;
    if (ep->new_pad & 0x8000) fire = 1;              /* sampled before the pad bit is dropped below */
    ep->new_pad &= 0xfbff;
    ep->new_pad &= 0xf7ff;
    if (ep->cur_wep == 0 && ep->e_wk[0].c[0] != 2) ep->new_pad &= 0x7fff;
    if (ep->cur_wep == 0) {
        char st = ep->e_wk[0].c[0];
        if (st == 1) {
            if (Anim_IsEnd(mp)) { ep->e_wk[0].c[0] = 2; Anim_ChangeAnim(mp, 2, 1, 0); }
        } else if (st == 0) {
            if (Anim_IsEnd(mp)) { ep->e_wk[0].c[0] = 1; Anim_ChangeAnim(mp, 1, 0x28, 0); }
        }
    } else {
        char st = ep->e_wk[0].c[0];
        if (st == 3) {
            if (Anim_IsEnd(mp)) { ep->e_wk[0].c[0] = 0; Anim_ChangeAnim(mp, 0, 1, 0); }
        } else if (st == 2) {
            if (Anim_IsEnd(mp)) { ep->e_wk[0].c[0] = 3; Anim_ChangeAnim(mp, 3, 0x28, 0); }
        }
    }
    if (ep->cur_wep == 0 && fire && ep->e_wk[0].c[0] != 2) fire = 0;
    ene_shoot(ep, mp, ep->lkmp, ep->lkvec, fire);
}

static void __Ene053_Control(EnemySt *ep, StModel *mp)
{
    PS2_FRAME(0x50);
    EnemyDataSt *edp = ep->edat;
    int dir = 8;
    unsigned short pad = ep->new_pad;
    float cw = (pad & 0x80) ? -1.0f : (pad & 0x20) ? 1.0f : 0.0f;
    e091_turn(ep, mp, edp, cw, 0.75f);
    pad = ep->new_pad;                                /* reloaded after e091_turn */
    if (pad & 0x10) dir = 0;
    else if (pad & 0x40) dir = 4;
    sceVu0RotMatrix(mp->matrix2, IdentityMtx, mp->ang);
    ene_dash(ep, mp, edp->d_accel, edp->d_max, edp->brake, dir, 0);
    DFene_MoveH(ep, mp, ep->vel);                     /* the asm also loads f12 = 0.1f (dropped by DWARF) */
    if (ep->thinkfunc && ep->sw.think) __Ene053_ShootControl(ep, mp);
}

static void __Ene053_Main(EnemySt *ep, StModel *mp)
{
    PS2_FRAME(0xf0);
    PS2_STACK(EneInfoSt, eneInfo, 0x60);   /* partly written by ene_get_eneinfo_basic, elenxz read back */
    ene_select_target(ep, mp);
    if (ep->thinkfunc && ep->sw.think) gcall(ep->thinkfunc, ep, mp);
    enepad_PadManage(ep);
    ene_change_wep(ep);
    ene_get_lock(ep, mp, ep->cur_wep);
    int w = ep->cur_wep;
    if (w == 0) {
        if (ep->e_wk[1].us[0] == 0) {
            EnemyDataSt *edat = ep->edat;
            EneWepSt *wp = &edat->wep_tab[w];
            ene_get_eneinfo_basic(ep, &eneInfo, ep->trg_mp);
            float range = (float)edat->fcs_tab[w].range;
            if (eneInfo.elenxz < range && !(ENE_mis_reload > 0)) {
                ep->new_pad |= 0x8000;
                ENE_mis_reload = wp->reload >> 1;    /* staggers every missile enemy */
                ep->e_wk[1].us[0] = wp->reload;
            }
        } else {
            ep->e_wk[1].us[0]--;
        }
    }
    if (ep->ctrlfunc) gcall(ep->ctrlfunc, ep, mp);
    enese_call_loop_on_move(ep, 0, 0, 1);
    if (ep->edat->cparam[0]) ene_wep_mng(ep, mp);
    ep->old_pad = ep->new_pad;
    ep->new_pad = 0;
}

void Ene053_Init(EnemySt *ep, StModel *mp)
{
    PS2_FRAME(0x30);
    ene_set_think(ep);
    DFene_SetFunc(ep, (void *)GFN(__Ene053_Main));
    ep->ctrlfunc = GFN(__Ene053_Control);
    ep->deadfunc = GFN(ene_dead);
    ep->e_wk[0].c[0] = 0;
    ep->e_wk[1].us[0] = 0;
    Anim_ChangeAnim(mp, 0, 1, 0);
}
