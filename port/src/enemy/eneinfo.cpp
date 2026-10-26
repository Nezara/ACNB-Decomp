/* src/enemy/eneinfo.cpp (OLSORTIE) - ene_get_eneinfo_basic, the first call of every enemy think function: clears the
 * 0x80-byte head of the EneInfoSt, copies the logic-level parameters (ranges, fly height, move rates, weapon mask)
 * and, when a target model exists, fills the target-relative geometry (vector, distances, yaw / pitch from the enemy
 * to the target and back). Not to be confused with einfo.cpp (EINFO_* counters). */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"
#include "acnb/sdk.h"
#include <cstring>

void ene_get_eneinfo_basic(EnemySt *ep, EneInfoSt *einfo, StModel *trgmp)
{
    PS2_FRAME(0x80);
    const float pi = ps2_u2f(0x40490fdb);
    StModel *mymp = ep->mp;
    ZeroMemory512b(einfo, 2);                /* 0x80 bytes: op_arg1 / op_arg2 keep their values */
    einfo->emp = trgmp;
    einfo->ilenxz = DF_CALC_GetDist_xz(ep->mp->loc, ep->initpos);
    einfo->mid_range = ep->logp[(int)ep->p_lv].mid_range;
    einfo->dif_range = ep->logp[(int)ep->p_lv].mid_width;
    einfo->canwep = (char)ep->logp[(int)ep->p_lv].canwep;
    einfo->fly_height = ep->logp[(int)ep->p_lv].fly_height;
    einfo->fly_dif = ep->logp[(int)ep->p_lv].fly_dif;
    einfo->mv_value[0] = 1.0f;
    einfo->mv_value[1] = ep->logp[(int)ep->p_lv].mv_dash;
    einfo->mv_value[2] = ep->logp[(int)ep->p_lv].mv_fly;
    einfo->mv_value[3] = 0.0f;
    if (!trgmp) return;
    alignas(16) float tloc[4];
    alignas(16) float mloc[4];
    DF_GetModelCenter(tloc, trgmp);
    DF_GetModelCenter(mloc, mymp);
    float *e = einfo->eloc;
    vu_sub_xyz(e, tloc, mloc);               /* sqc2 of the whole quad: eloc.w = tloc.w */
    {
        float z2 = e[2] * e[2], x2 = e[0] * e[0];
        einfo->elenxz = ps2_sqrt(x2 + z2);
    }
    {
        float y2 = e[1] * e[1], x2 = e[0] * e[0], z2 = e[2] * e[2];
        einfo->elen = ps2_sqrt(z2 + (x2 + y2));
    }
    einfo->eang[1] = ps2_atan2f(e[0], e[2]);
    einfo->eang[0] = ps2_atan2f(-e[1], einfo->elenxz);
    float tang;
    switch (trgmp->type) {
    case 3:
        tang = ((EnemySt *)trgmp->arg0p)->view.angx;
        break;
    case 4:
        tang = ((AcPlayerSt *)trgmp->arg0p)->view.ang[0];
        break;
    default:
        tang = trgmp->ang[1] + pi;
        if (!(tang <= pi)) tang = tang - ps2_u2f(0x40c90fdb);   /* 2 pi */
        break;
    }
    einfo->ctoe_ang_x = ene_get_angle_to_angle(einfo->eang[0], ep->view.angx);
    if (ep->parent) {
        float a = ps2_atan2f(ep->parent->matrix2[2][0], ep->parent->matrix2[2][2]);
        einfo->ctoe_ang_y = ene_get_angle_to_angle(einfo->eang[1], a + ep->view.angy);
    } else {
        einfo->ctoe_ang_y = ene_get_angle_to_angle(einfo->eang[1], ep->view.angy);
    }
    float y = einfo->eang[1];
    float a = (y <= 0.0f) ? pi + y : y - pi;
    float b = trgmp->ang[1];
    b = (b <= 0.0f) ? pi + b : b - pi;
    einfo->etoc_ang_y = ene_get_angle_to_angle(a, b);
    einfo->etoc_ang_x = ene_get_angle_to_angle(-einfo->eang[0], tang);
    einfo->can_see_enemy = 1;               /* no visibility test */
    memcpy(einfo->tloc, trgmp->loc, 16);
    einfo->tangy = trgmp->ang[1];
}
