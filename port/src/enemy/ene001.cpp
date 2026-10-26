/* src/enemy/ene001.cpp (OLSORTIE) - enemy type 1, the fixed / slow ground turret group (gun turrets e86 / e87,
 * mobile MTs, 60000-hp scenery). e001_main: target, think, pad, control. e001_think aims with target lead
 * (DF_get_deflection on the last-frame center delta) and sets the turn pad bits (e001_turn_to_ene) and the fire bit
 * (DFene_CheckShootEx, always weapon 0). e001_control turns the turret objects (mp->op[1..3], kept in e_wk[1..3] by
 * Ene001_Initial) through e001_turn, or follows another model (cparam[0]), and shoots through ene_shoot,
 * alternating two weapons. The angle wraps keep the original !(a <= PI) / a < -PI tests (NaN never leaves them). */
#include "acnb/port.h"
#include "acnb/sdk.h"

#define E001_PI      0x1.921fb6p+1f    /* 0x40490fdb */
#define E001_TWO_PI  0x1.921fb6p+2f    /* 0x40c90fdb */
#define E001_TH      0x1.657184p-4f    /* 0x3db2b8c2, 5 degrees */
#define E001_DEG     0x1.1df46ap-6f    /* 0x3c8efa35, 1 degree */

static void e001_turn_to_ene(EnemySt *ep, float angx, float angy)
{
    float d = angy - ep->turn_spd;
    if (!(d <= E001_TH)) ep->new_pad |= 0x20;
    else if (d < -E001_TH) ep->new_pad |= 0x80;
    d = angx - ep->look_spd;
    if (!(d <= E001_TH)) ep->new_pad |= 0x100;
    else if (d < -E001_TH) ep->new_pad |= 0x200;
}

void e001_turn(EnemySt *ep, float (*pmat)[4], float *angp, EnemyDataSt *edp, float cw, float brake, float min_ang,
               float max_ang)
{
    PS2_FRAME(0x40);
    float a = *angp + ep->turn_spd;
    while (!(a <= E001_PI)) a -= E001_TWO_PI;
    while (a < -E001_PI) a += E001_TWO_PI;
    if (min_ang < max_ang) {
        if (a <= max_ang) { if (a < min_ang) a = min_ang; }
        else a = max_ang;                                    /* NaN a -> max_ang */
    }
    *angp = a;
    float v = ps2_atan2f(pmat[2][0], pmat[2][2]);
    ep->view.angy = v;
    v = v + E001_PI;
    ep->view.angy = v;
    if (!(v <= E001_PI)) ep->view.angy = v - E001_TWO_PI;   /* one subtraction, not a loop */
    ep->view.angy = ep->view.angy + a;
    while (!(ep->view.angy <= E001_PI)) ep->view.angy -= E001_TWO_PI;
    while (ep->view.angy < -E001_PI) ep->view.angy += E001_TWO_PI;
    ene_turn_speed(&ep->turn_spd, edp->t_accel, edp->t_max, brake, cw);
}

void e001_think(EnemySt *ep, StModel *mp)
{
    PS2_FRAME(0xc0);
    alignas(16) float c[4], tgpos[4], mypos[4], loc[4], vec[4], ang[4];
    int w = ep->cur_wep;
    if (w < 0 || w >= 5) return;
    EneWepSt *wep = &ep->edat->wep_tab[w];
    StModel *t = ep->trg_mp;
    if (t) {
        if (t == ep->lkmp) {
            DF_GetModelCenter(c, t);
            /* vsub.xyz + full sqc2: lane w = c.w */
            ep->lkvec[0] = c[0] - ep->lkpos[0];
            ep->lkvec[1] = c[1] - ep->lkpos[1];
            ep->lkvec[2] = c[2] - ep->lkpos[2];
            ep->lkvec[3] = c[3];
            ep->lkpos[0] = c[0]; ep->lkpos[1] = c[1]; ep->lkpos[2] = c[2]; ep->lkpos[3] = c[3];
            if (ep->lktime < 255) ep->lktime++;
            goto aim;
        }
        DF_GetModelCenter(ep->lkpos, t);                     /* new target: lkvec keeps its old value */
    }
    ep->old_lkmp = ep->lkmp;
    ep->lkmp = t;
    ep->lktime = 0;
aim:
    if (!ep->lkmp) return;
    {
        float speed = SpellData2[wep->spl_id].spell.speed;
        int acc = (wep->func >> 5) & 7;
        DF_GetModelCenter(tgpos, ep->lkmp);
        DF_GetModelCenter(mypos, mp);
        DF_get_deflection(loc, 0, tgpos, ep->lkvec, mypos, speed, acc);
        vec[0] = loc[0] - mypos[0];
        vec[1] = loc[1] - mypos[1];
        vec[2] = loc[2] - mypos[2];
        vec[3] = loc[3];
        DF_CALC_Vector2Angle(ang, vec);
        ang[0] = ene_get_angle_to_angle(ang[0], ep->view.angx);
        StModel *par = ep->parent;
        if (par) {
            float pa = ps2_atan2f(par->matrix2[2][0], par->matrix2[2][2]);
            ang[1] = ene_get_angle_to_angle(ang[1], pa + ep->view.angy);
        } else {
            ang[1] = ene_get_angle_to_angle(ang[1], ep->view.angy);
        }
        e001_turn_to_ene(ep, ang[0], ang[1]);
        if (DFene_CheckShootEx(ep, ang[0], ang[1], 0)) ep->new_pad |= 0x8000;   /* wepno 0, not cur_wep */
    }
}

void e001_control(EnemySt *ep, StModel *mp)
{
    PS2_FRAME(0x40);
    unsigned short pad = ep->new_pad;
    float cw = (pad & 0x80) ? -1.0f : (pad & 0x20) ? 1.0f : 0.0f;
    EnemyDataSt *edp = ep->edat;
    if (edp->cparam[0]) {                                    /* turret slaved to another model */
        StModel *m = STAGE_GetModelPointer(ep->attach_type, ep->attach_id);
        if (m) {
            float a = E001_PI + m->ang[1];
            float k = E001_DEG * ep->edat->fparam[0];
            ep->view.angy = a + k;
            while (!(ep->view.angy <= E001_PI)) ep->view.angy -= E001_TWO_PI;
            while (ep->view.angy < -E001_PI) ep->view.angy += E001_TWO_PI;
        }
    } else {
        ObjectSt *o1 = (ObjectSt *)ep->e_wk[1].p;
        ObjectSt *o2 = (ObjectSt *)ep->e_wk[2].p;
        float lim = edp->lim_angy;
        e001_turn(ep, *o1->wrmatp, &o2->angle[1], edp, cw, 0.75f, -lim, lim);
    }
    ene_change_view_ex(ep, 0x1.12843cp-10f /*0x3a89421e*/, 0x1.c98710p-7f /*0x3c64c388*/, 0x1.4cccccp-1f /*0x3f266666*/);
    ((ObjectSt *)ep->e_wk[3].p)->angle[0] = -ep->view.angx;
    if (ep->edat->cparam[1] && ep->lkmp) {
        float d = DF_CALC_GetDist(ep->lkmp->loc, ep->mp->loc);
        float range = (float)(edp->fcs_tab + ep->cur_wep)->range;   /* cur_wep may be -1: byte arithmetic */
        if (!(d <= range)) return;                           /* out of range: no ene_shoot at all (NaN too) */
    }
    int fire = ep->new_pad & 0x8000;                         /* masked value, not a bool */
    if (ene_shoot(ep, mp, ep->lkmp, 0, fire) && ep->e_wk[0].c[0]) ep->cur_wep = ep->cur_wep ^ 1;
}

static void e001_main(EnemySt *ep, StModel *mp)
{
    PS2_FRAME(0x30);
    ene_select_target(ep, mp);
    if (ep->thinkfunc && ep->sw.think) gcall(ep->thinkfunc, ep, mp);
    enepad_PadManage(ep);
    if (ep->ctrlfunc) gcall(ep->ctrlfunc, ep, mp);
    ep->old_pad = ep->new_pad;
    ep->new_pad = 0;
}

void Ene001_Initial(EnemySt *ep, StModel *mp)
{
    PS2_FRAME(0x30);
    EnemyDataSt *edat = ep->edat;
    ep->animfunc = 0;
    ep->thinkfunc = GFN(e001_think);
    ep->ctrlfunc = GFN(e001_control);
    /* "has a second weapon": wep_tab[1].vtx_id high nibble > 0 */
    ep->e_wk[0].c[0] = ((ep->edat->wep_tab[1].vtx_id >> 4) & 0xf) > 0 ? 1 : 0;
    ep->e_wk[1].p = &mp->op[1];
    ep->e_wk[2].p = &mp->op[2];
    ep->e_wk[3].p = &mp->op[3];
    DFene_SetFunc(ep, (void *)GFN(e001_main));
    if (edat->flag & 0x4000) {
        mp->msgfunc = GFN(e045_msgfunc);                     /* another enemy's message function: original choice */
        mp->use |= 2;
    }
}
