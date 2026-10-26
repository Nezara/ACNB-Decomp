/* src/enemy/enewep.cpp (OLSORTIE) - enemy weapon bookkeeping. ene_wep_mng runs once per frame: when the selected
 * weapon changed (or a type-2 weapon is held outside its firing states 0xa / 0xb / 0xd) it switches the old weapon's
 * joint control off and calls the old / new weapon's mode callbacks (wep_func_tab: 2 = leave, 1 = enter); every
 * frame it runs the current weapon's joint control and per-frame callback (mode 0). The wep_func_* callbacks (gatling
 * / ibis EO) stay interpreted. */
#include "acnb/port.h"

/* the original's unnamed table at 0x1e6bcd0 (8 entries, indexed by EneWepSt.func & 0x1f) */
static void *ene_wep_func(int k)
{
    switch (k) {
    case 2: return (void *)GFN(wep_func_gatling);
    case 3: return (void *)GFN(wep_func_gatling2);
    case 4: return (void *)GFN(wep_func_ibis_eo);
    default: return 0;
    }
}

static void ene_wep_callback(EnemySt *ep, StModel *mp, EneWepSt *w, int msg)
{
    int k = w->func & 0x1f;
    if (k <= 0 || k >= 8) return;
    void *fn = ene_wep_func(k);
    if (fn) gcall_as<void(EnemySt *, StModel *, EneWepSt *, int)>(fn, ep, mp, w, msg);
}

void ene_wep_mng(EnemySt *ep, StModel *mp)
{
    PS2_FRAME(0x50);
    int cur = ep->cur_wep;
    EnemyDataSt *edat = ep->edat;
    if (cur < 0 || cur >= 5) return;
    int old = ep->old_wep;
    EneWepSt *w = &edat->wep_tab[cur];
    int change = cur != old;
    if (!change && (w->_type & 0x3f) == 2) {
        char b = ep->bst;
        if (b != 0xb && b != 0xa && b != 0xd) change = 1;   /* held type-2 weapon: toggled every such frame */
    }
    if (change) {
        EneWepSt *prev = &edat->wep_tab[old];               /* old may be -1: reads before the table */
        _jdat_tab_tag_ *jc = DFene_GetJConTab(prev->jcon);
        if (jc) {                                            /* no table: callbacks and old_wep update skipped */
            ene_jcon_mng(ep, mp, jc->jdat, jc->num, 0);
            ep->old_wep = ep->cur_wep;
            ene_wep_callback(ep, mp, prev, 2);
            ene_wep_callback(ep, mp, w, 1);
        }
    }
    _jdat_tab_tag_ *jc = DFene_GetJConTab(w->jcon);
    if (jc) {
        if ((w->_type & 0x3f) != 2 || ep->bst == 0xb || ep->bst == 0xa) ene_jcon_mng(ep, mp, jc->jdat, jc->num, 1);
    }
    ene_wep_callback(ep, mp, w, 0);
}
