/* src/enemy/enejcon.cpp (OLSORTIE) - enemy "joint control": while a weapon of type 2 is in use, ene_wep_mng calls
 * ene_jcon_mng(on = 1) every frame and the head / turret objects of the _jcon_data2 list are turned toward the lock
 * target (or 10000 units along the view vector when there is none) with ObjLookWorldTarget2_Xofs, blended in over
 * jcon_time frames (rate = frm / jcon_time of the FIRST entry); on = 0 hands the objects back to the animation.
 * State: ep->e_jcon[0] (lkmp, frm, flag). common_jcon_on2 takes the (DWARF-dropped, unused) ep in a0. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"
#include <cstring>

#define EJC_ROTSPD 0.25f        /* 0x3e800000 */
#define EJC_FAR    10000.0f     /* 0x461c4000 */

static void common_jcon_on2(EnemySt *ep, StModel *mp, _jcon_data2 *jdp, float *loc, float jcon_rate, int flag)
{
    PS2_FRAME(0x70);
    (void)ep;
    float rate = jcon_rate;
    if (flag) {
        Anim_MakeGroup(mp, jdp->trg_oid, 1 << jdp->anm_grp);
        mp->op[jdp->trg_oid].noanim = 1;
        mp->op[jdp->trg_oid].hokanframe = 0;
        mp->op[jdp->trg_oid].hokantime = 0x1e;
        mp->op[jdp->trg_oid].todid = 0xff;
    }
    alignas(16) float ang0[4];
    alignas(16) float lim[4];
    memcpy(ang0, mp->op[jdp->trg_oid].angle, 16);
    lim[0] = -jdp->lim_angx;
    lim[1] = jdp->lim_angx;
    lim[2] = -jdp->lim_angy;
    lim[3] = jdp->lim_angy;
    ObjectSt *par = &mp->op[jdp->par_oid];
    float (*pm)[4] = (float (*)[4])par->wrmatp;
    ObjLookWorldTarget2_Xofs(&mp->op[jdp->trg_oid], pm, (float *)pm + 4, loc, lim, EJC_ROTSPD, jdp->xofs);
    for (int i = 0; i < 3; i++) {
        ObjectSt *t = &mp->op[jdp->trg_oid];
        float diff = t->angle[i] - ang0[i];
        float m = rate * diff;
        t->angle[i] = ang0[i] + m;
    }
    Anim_UpdateChildObjMatrix(&mp->op[jdp->trg_oid], (float (*)[4])mp->op[jdp->par_oid].wrmatp);
}

void ene_jcon_mng(EnemySt *ep, StModel *mp, _jcon_data2 *jdp, int jd_cnt, int on)
{
    PS2_FRAME(0x90);
    if (jdp == NULL) return;
    struct_8dbe177a *e = &ep->e_jcon[0];
    int n = jd_cnt;
    if (on) {
        int fresh;
        if (e->flag != 1) {
            e->flag = 1;
            e->frm = 0;
            fresh = 1;
        } else {
            fresh = 0;
        }
        StModel *lk = ep->lkmp;
        if (lk == NULL || e->lkmp != lk) {
            e->lkmp = ep->lkmp;
            e->frm = 0;
        }
        float rate;
        int t = jdp->jcon_time;
        if (t == 0 || !(e->frm < t)) {
            rate = 1.0f;
        } else {
            e->frm = (unsigned char)(e->frm + 1);
            float a = (float)(int)e->frm;
            float b = (float)(int)jdp->jcon_time;
            rate = a / b;                               /* div.s, b > 0 */
        }
        alignas(16) float ctr[4];
        if (ep->lkmp) {
            DF_GetModelCenter(ctr, ep->lkmp);
        } else {
            DF_GetModelCenter(ctr, mp);
            /* vmulx.xyz / vadd.xyz: ctr.xyz += view.vec.xyz * 10000 (w = centre w) */
            for (int i = 0; i < 3; i++) {
                float v = ep->view.vec[i] * EJC_FAR;
                ctr[i] = ctr[i] + v;
            }
        }
        while (n-- != 0) {
            common_jcon_on2(ep, mp, jdp, ctr, rate, fresh);
            jdp++;
        }
    } else {
        if (e->flag != 0) {
            e->flag = 0;
            while (n-- != 0) {
                Anim_MakeGroup(mp, jdp->trg_oid, (1 << jdp->anm_grp) | 1);
                mp->op[jdp->trg_oid].noanim = 0;
                mp->op[jdp->trg_oid].todid = 0xff;
                ObjectSt *p = &mp->op[jdp->par_oid];
                Anim_ChangeAnimGroupXframe(mp, 1 << jdp->anm_grp, p->todid, p->frame, 0x1e, p->animloop);
                jdp++;
            }
        }
    }
}
