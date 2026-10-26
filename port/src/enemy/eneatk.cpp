/* src/enemy/eneatk.cpp (OLSORTIE) - enemy attack decision. garage3 layer 1: ene_attack tests the target against the
 * selected weapon's angle window (logic table angle + 5 degrees), asks DFene_AttackFreq whether this frame may fire
 * and presses 0x8000 (missiles also wait for the global ENE_mis_reload); ene_blindshot_enable reads the logic
 * table's blind-shot flag. Layer 2: DFene_AttackFreq (one rand per call for 0 < freq < 100, scaled by the enemy's
 * fake_value_freq). DFene_CheckShoot / ene_get_prob stay interpreted for now. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"

int ene_blindshot_enable(EnemySt *ep, int wepid)
{
    struct_f8da75e1 *at = ep->logp[(int)ep->p_lv].atk_tab + wepid;   /* wepid unchecked */
    return (at->flag & 0x10) != 0;
}

void ene_attack(EnemySt *ep, EneInfoSt *eip)
{
    PS2_FRAME(0x30);
    if (!eip->emp) return;
    if (!(ep->wep_valid & 1)) return;
    int w = ep->cur_wep;
    if (w < 0 || w >= 5) return;
    EneWepSt *wp = &ep->edat->wep_tab[w];
    struct_f8da75e1 *at = &ep->logp[(int)ep->p_lv].atk_tab[w];
    const float d5 = ps2_u2f(0x3db2b8c2);   /* 5 degrees */
    float hy = d5 + at->angy;
    float hx = d5 + at->angx;
    float ay = eip->ctoe_ang_y, ax = eip->ctoe_ang_x;
    /* window test in the asm's order; DFene_AttackFreq (random draws) only runs inside the window */
    int fire = (ay < hy) && !(ay <= -hy) && (ax < hx) && !(ax <= -hx) && DFene_AttackFreq(ep, w);
    if (!fire) return;
    if ((wp->_type & 0x3f) == 1) {           /* missile */
        if (ENE_mis_reload > 0) return;
        ep->new_pad |= 0x8000;
        ENE_mis_reload = wp->reload >> 1;
    } else {
        ep->new_pad |= 0x8000;
    }
}

int DFene_AttackFreq(EnemySt *ep, int atkno)
{
    PS2_FRAME(0x30);
    if (g_sortie.pwork->statMission != 0) return 0;   /* mission decided */
    if (ep->use & 0xc0) return 0;                     /* dying / dead */
    if (atkno < 0 || atkno >= 5) return 0;
    int f = ep->logp[(int)ep->p_lv].atk_tab[atkno].freq;
    if (f >= 100) return 1;
    if (f <= 0) return 0;
    int r = ps2_mod32(ps2_rand(), 0x190000) >> 14;    /* 0..99; drawn before fake_value_freq is read */
    float q = ((float)f * (float)ep->fake_value_freq) / 100.0f;
    unsigned int u;
    const float two31 = ps2_u2f(0x4f000000);
    if (two31 <= q) u = (unsigned int)ps2_ftoi(q - two31) | 0x80000000u;
    else u = (unsigned int)ps2_ftoi(q);
    return r < (int)(u & 0xff);                       /* the u8 mask wraps values >= 256 */
}

/* fire check against the logic table's angle window of weapon `wepno` (+ 5 degrees), then DFene_AttackFreq */
int DFene_CheckShootEx(EnemySt *ep, float ctoe_ang_x, float ctoe_ang_y, int wepno)
{
    PS2_FRAME(0x10);
    const float th = 0x1.657184p-4f;                       /* 0x3db2b8c2, 5 degrees */
    struct_f8da75e1 *at = ep->logp[(int)ep->p_lv].atk_tab + wepno;
    float ay = th + at->angy;
    float ax = th + at->angx;
    if (!(ctoe_ang_y < ay)) return 0;
    if (ctoe_ang_y <= -ay) return 0;
    if (!(ctoe_ang_x < ax)) return 0;
    if (ctoe_ang_x <= -ax) return 0;
    return DFene_AttackFreq(ep, wepno) ? 1 : 0;
}
