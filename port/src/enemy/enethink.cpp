/* src/enemy/enethink.cpp (OLSORTIE) - shared enemy think helpers. garage3 layer 1: ene_blade, the melee decision:
 * when the target is inside the blade's distance / height window it presses 0x4000 (boost-dash in, from the blade
 * states 7 / 0x12) or 0x2000 (swing, if DFene_CheckShoot allows). The rest of the file stays interpreted. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"

void ene_blade(EnemySt *ep, EneInfoSt *eip)
{
    PS2_FRAME(0x20);
    if (!eip->emp) return;
    unsigned int t = ep->edat->wep_tab[4]._type & 0x3f;
    if (t != 4 && t != 5 && t != 8) return;
    /* (unsigned)elenxz, MWCC idiom: values >= 2^31 are converted after subtracting 2^31 */
    float fl = eip->elenxz;
    unsigned int du;
    if (ps2_u2f(0x4f000000) <= fl) du = (unsigned int)ps2_ftoi(fl - ps2_u2f(0x4f000000)) | 0x80000000u;
    else du = (unsigned int)ps2_ftoi(fl);
    int d = (int)(du & 0xffff);
    int h = (short)ps2_ftoi(eip->eloc[1]);
    if (h < 0) h = -h;
    struct_f8da75e1 *at = &ep->logp[(int)ep->p_lv].atk_tab[4];
    if (d < at->range_min) return;
    if (at->range_max < d) return;
    if (at->range_max < h) return;           /* the height uses range_max too */
    int n = (t == 8) ? 10 : 5;
    if (ep->bst == 7 || ep->bst == 0x12) {
        if (d < 0x35) return;
        if (n < ep->bld_time) ep->new_pad |= 0x4000;
    } else if (DFene_CheckShoot(ep, eip, 4)) {
        ep->new_pad |= 0x2000;
    }
}

/* TRACE ROUTE score (sandbox gE): follow the stage's marker chain. With no current marker take the nearest one
 * (1, or 0 if the stage has none); while the model centre is outside the current marker's box (loc + wk extents,
 * tested x, z, y) keep tracing (1); inside it step to the next marker (signed byte at +0x11, times 0x40, unchecked)
 * and clear op_wk[0] (1), or score 0 at the end of the chain (dmyp stays). */
float enevl_trace_route(EnemySt *ep)
{
    PS2_FRAME(0x60);
    MarkerSt *m = (MarkerSt *)ep->dmyp;
    if (!m) {
        m = STAGE_GetNearestDmyReg(ep->mp->loc, NULL);
        if (!m) return 0.0f;
        ep->dmyp = m;
        return 1.0f;
    }
    const float *ext_lo = (const float *)&m->wk[0];
    const float *ext_hi = (const float *)&m->wk[16];
    /* the original's sqc2 slots (w = marker loc.w, kept by vadd.xyz): mirrored for later stale stack reads */
    PS2_STACK(float[4], c, 0x30);
    PS2_STACK(float[4], hi, 0x40);
    PS2_STACK(float[4], lo, 0x50);
    for (int i = 0; i < 3; i++) lo[i] = m->loc[i] + ext_lo[i];   /* vadd.xyz */
    lo[3] = m->loc[3];
    for (int i = 0; i < 3; i++) hi[i] = m->loc[i] + ext_hi[i];
    hi[3] = m->loc[3];
    DF_GetModelCenter(c, ep->mp);
    int out;
    if (c[0] < lo[0]) out = 1;
    else if (!(c[0] <= hi[0])) out = 2;
    else if (c[2] < lo[2]) out = 0x100;
    else if (!(c[2] <= hi[2])) out = 0x200;
    else if (c[1] < lo[1]) out = 0x10;
    else if (!(c[1] <= hi[1])) out = 0x20;
    else out = 0;
    if (out) return 1.0f;
    int n = m->cdmy[0];
    if (n == 0) return 0.0f;
    ep->dmyp = (char *)m + n * 0x40;
    ep->op_wk[0] = 0;
    return 1.0f;
}
