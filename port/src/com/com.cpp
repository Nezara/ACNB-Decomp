/* src/com/com.cpp — COM (AI pilot) core (whole file). All state lives in com_info[4]; comp / com_acp / com_enp select
 * the COM being processed. ComInit sets a COM up (AI data, extension / launcher / eye init, pad timing, behaviour
 * tree, target list); DF_ComInKey is one think step: own / enemy info, the behaviour tree (comtree.cpp) picks the
 * operation and weapon, the operation steers through the compad_* helpers (turn, move, boost / fly, view pitch),
 * launcher / extension / EO callbacks and shoot_weapon press keys, and every pad channel goes through a delay ring
 * (ComPad_SendPad) before the bits come back as the COM's virtual pad. DF_ComDamage feeds damage events and the
 * weapon preference learning, DF_COM_* the missile alert buffer. com_extinit_* are the extension AIs
 * (com_extinit_tbl by PartsParamExtensionSt.prog_no).
 * "airborne" = com_acp ? actstat & 1 : !enemy landing; legtype = com_acp ? param.prog_no : edat->leg_type. */
#include <bit>
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/nodwarf.h"

static const float COM_PI = 3.14159274f;    /* 0x40490fdb */
static const float COM_PI2 = 6.28318548f;   /* 0x40c90fdb */

static inline int com_airborne(void) { return com_acp ? (com_acp->actstat & 1) : !com_enp->sw.landing; }
static inline int com_legtype(void) { return com_acp ? com_acp->param.prog_no : com_enp->edat->leg_type; }
static inline int com_act(void) { return com_acp ? (int)com_acp->actno : (int)com_enp->bst; }

/* the weapon slot can fire now: not empty, ammo left, a charge weapon (effect 0x20) not while the AC charges */
static int com_wep_usable(AcPlayerSt *acp, _weapon_st *wp)
{
    if (wp->statid == 5) return 0;
    if (wp->blt_cnt == 0) return 0;
    if (!(wp->dstat.effect & 0x20)) return 1;
    return AcCtrl_IsCharging(acp) ? 0 : 1;
}

/* n (n + 1) / 2 the way the original computes it (odd / even split, low 32 bits of the product) */
static inline int com_tri_sum(int n)
{
    if (n & 1) return (int)((unsigned int)((n + 1) >> 1) * (unsigned int)n);
    return (int)((unsigned int)(n + 1) * (unsigned int)(n >> 1));
}

/* Nearest live missile of the COM's alert buffer (dead entries are dropped); aimed at the body centre. */
static _spell2 *Com_GetNearestMissileSpell(stCom *pCom)
{
    float best = 9999999.0f;   /* 0x4b18967f */
    _spell2 *res = 0;
    for (unsigned int i = 0; i < 16; i++) {
        struct_f6a33bd0 *m = &pCom->missile_buff[i];
        _spell2 *p = m->magp;
        if (!p) continue;
        if (p->on <= 0) {
            m->magp = 0;
            continue;
        }
        float v[3];
        v[0] = p->pos[0] - pCom->loc[0];
        v[1] = p->pos[1] - pCom->loc[1];
        v[2] = p->pos[2] - pCom->loc[2];
        v[1] = v[1] + 0.5f * pCom->tall;
        float x2 = v[0] * v[0];
        float z2 = v[2] * v[2];
        float y2 = v[1] * v[1];
        float d = z2 + (x2 + y2);
        if (d < best) {
            res = m->magp;
            best = d;
        }
    }
    return res;
}

int DF_COM_ExistMissile(int com_id, _spell2 *pSpell)
{
    for (unsigned int i = 0; i < 16; i++)
        if (com_info[com_id].missile_buff[i].magp == pSpell) return 1;
    return 0;
}

/* Remember an incoming missile once (first free slot), dropping later duplicates. */
void DF_COM_AlertMissile(int com_id, _spell2 *pSpell)
{
    int found = 0;
    for (unsigned int i = 0; i < 16; i++) {
        struct_f6a33bd0 *m = &com_info[com_id].missile_buff[i];
        if (found) {
            if (m->magp == pSpell) m->magp = 0;
        } else {
            if (!m->magp) m->magp = pSpell;
            if (m->magp == pSpell) found = 1;
        }
    }
}

void *AC3_Com_GetComInfo(void)
{
    return com_info;
}

/* com_no / ac_id: bits 28-31 type (3 = enemy), bits 0-27 index (AC slot). */
static inline StModel *dmg_model(int type, unsigned int id) { return type == 3 ? Enemy[id].mp : g_ac[id].mp; }

void DF_ComDamage(int com_no, int ac_id, int damage)
{
    PS2_FRAME(0x80);
    /* the event lives at its original stack slot: its unused tail (dead, cdummy) is stale PS2 stack */
    PS2_STACK(struct_e2063795, event, 0x60);
    struct_e2063795 *ev = &event;
    int stype = ac_id >> 28, ttype = com_no >> 28;
    unsigned int sid = ac_id & 0xfffffff, tid = com_no & 0xfffffff;
    ev->stype = (char)stype;
    ev->sid = (int)sid;
    ev->ttype = (char)ttype;
    ev->tid = (int)tid;
    ev->smp = dmg_model(stype, sid);
    ev->tmp = dmg_model(ttype, tid);
    ev->damage = damage;
    Com_Dmg_AddEvent(ev);
    /* the attacking COM likes the weapon / op that hit (capped at the weapon's value_max, revision +100) */
    if (stype != 3 && (int)sid >= NowUserPlayerNum) {
        stCom *c = &com_info[ac_id - NowUserPlayerNum];
        int e = c->wepmng_equip;
        if (e >= 0 && c->op_sel >= 0) {
            stComWepMng *w = c->wepmng;
            if (c->ene_mp) {
                w[e].value++;
                unsigned char vmax = w[e].datap->value_max;
                if (vmax < w[e].value) w[e].value = vmax;
                int op = c->op_sel;
                w[e].revision_op[op]++;
                if (w[e].revision_op[op] > 100) w[e].revision_op[op] = 100;
            }
        }
    }
    /* the COM that was hit: same counter, clamped at -100 (it is incremented as well in the original) */
    if (ttype != 3 && (int)tid >= NowUserPlayerNum) {
        stCom *c = &com_info[tid - NowUserPlayerNum];
        int ws = c->wep_sel;
        stComWepMng *w = c->wepmng;
        int op = c->op_sel;
        w[ws].revision_op[op]++;
        if (w[ws].revision_op[op] < -100) w[ws].revision_op[op] = -100;
    }
}

/* Runs the equipped weapon's shoot logic (comwp.cpp main) once the scored weapon is in hand and usable; enemy COMs
 * (missions) fire their current weapon when the range and the enemy's own shot test allow. */
static void shoot_weapon(void)
{
    PS2_FRAME(0x30);
    stCom *c = comp;
    if (!c->ene_mp) return;
    int equip = c->wepmng_equip;
    if (equip < 0) return;
    if (com_acp) {
        int ws = c->wep_sel;
        stComWepMng *wm = &c->wepmng[equip];
        _weapon_st *wp = &com_acp->param.weapons[ws];
        if (ws != com_acp->equip_weapon) return;
        if (!com_wep_usable(com_acp, wp)) return;
        if ((wp->dstat.effect & 0x20) && com_acp->energyReal < (wp->blt_price << 1)) return;
        if (wp->lockmax) {
            if (com_acp->lockmng.lock[0].ofp && (com_acp->lockmng.lock[0].lock_stat & 2)) wm->time_lock++;
            else wm->time_lock = 0;
        }
        if (!wm->main) return;
        if (wm->ShotDelayTime < wm->datap->shot_delay) {
            wm->ShotDelayTime++;
            return;
        }
        if (gcall(wm->main, wm, wm->datap, wp, (void *)&wm->wk)) {
            wm->time_lock = 0;
            wm->ShotDelayTime = 0;
            comp->opmng[(int)comp->op_sel].f_shot_wep = 1;
        }
        return;
    }
    /* an enemy COM (missions only) */
    EnemySt *ep = com_enp;
    int cw = ep->cur_wep;
    if (cw < 0 || cw >= 5) return;
    EneWepSt *ed = &ep->edat->wep_tab[cw];
    stComWepData *wd = c->wepmng[equip].datap;
    if (wd->dist_min && c->elen < (float)wd->dist_min) return;
    if (wd->dist_max && !(c->elen <= (float)wd->dist_max)) return;
    if ((ed->_type & 0x3f) == 1) {   /* missile */
        if (!DFene_CheckShootEx(ep, comp->ctoe_ang_x, comp->ctoe_ang_y, cw)) return;
        if (ENE_mis_reload > 0) return;
        comp->pad |= 0x8000;
        ENE_mis_reload = ed->reload >> 1;
    } else {
        if (!DFene_CheckShootEx(ep, comp->ctoe_ang_x, comp->ctoe_ang_y, cw)) return;
        if (!ene_blindshot_enable(com_enp, com_enp->cur_wep)) return;
        comp->pad |= 0x8000;
    }
}

/* Target info: position / angles of the enemy (refreshed every sense_delay + 1 chase frames), line of sight (every
 * frame), the enemy weapon's range and the nearest incoming missile. */
static void com_update_eneinfo(void)
{
    PS2_FRAME(0x40);
    stCom *c = comp;
    if (!c->ene_mp) {
        c->f_can_see_enemy = 0;
        comp->lose_time++;
        return;
    }
    if (ps2_mod32(c->chase_time, c->datap->sense_delay + 1) == 0) {
        memcpy(c->ene_info.loc, c->ene_mp->loc, 16);
        memcpy(comp->ene_info.ang, comp->ene_mp->ang, 16);
        StModel *em = comp->ene_mp;
        if (em && em->type == 4) comp->ene_info.ang[0] = ((AcPlayerSt *)comp->ene_mp->arg0p)->view.ang[0];
        else comp->ene_info.ang[0] = 0.0f;
        comp->ene_info.ang[1] = inverse_angf(comp->ene_info.ang[1]);
        c = comp;
        /* vsub.xyz into a full store: w is ene_info.loc's */
        c->eloc[0] = c->ene_info.loc[0] - c->loc[0];
        c->eloc[1] = c->ene_info.loc[1] - c->loc[1];
        c->eloc[2] = c->ene_info.loc[2] - c->loc[2];
        c->eloc[3] = c->ene_info.loc[3];
        float x2 = c->eloc[0] * c->eloc[0];
        float z2 = c->eloc[2] * c->eloc[2];
        c->elenxz = ps2_sqrt(x2 + z2);
        x2 = c->eloc[0] * c->eloc[0];
        float y2 = c->eloc[1] * c->eloc[1];
        z2 = c->eloc[2] * c->eloc[2];
        c->elen = ps2_sqrt(z2 + (x2 + y2));
        c->eang[1] = inverse_angf(ps2_atan2f(c->eloc[0], c->eloc[2]));
        c->eang[0] = ps2_atan2f(-c->eloc[1], c->elenxz);
        c->ctoe_ang_y = sub_angf(c->eang[1], c->ang[1]);
        c->ctoe_ang_x = sub_angf(c->eang[0], c->viewang[0]);
        c->etoc_ang_y = sub_angf(c->eang[1], c->ene_info.ang[1]);
        c->etoc_ang_x = sub_angf(-c->eang[0], c->ene_info.ang[0]);
    }
    /* line of sight between the two bodies, 3/4 of the height below the model origins */
    PS2_STACK(float[4], a, 0x30);
    PS2_STACK(float[4], b, 0x20);
    PS2_STACK(float[4], dest, 0x10);
    memcpy(a, comp->loc, 16);
    memcpy(b, comp->ene_mp->loc, 16);
    a[1] = a[1] - 0.75f * comp->tall;
    b[1] = b[1] - 0.75f * comp->ene_mp->tall;
    if (HetProcPoint2Point(a, b, dest, 1) == 0) {
        comp->f_can_see_enemy = 1;
        comp->lose_time = 0;
    } else {
        comp->f_can_see_enemy = 0;
        comp->lose_time++;
    }
    c = comp;
    StModel *em = c->ene_mp;
    if (em->type == 3) {
        EnemySt *e = (EnemySt *)em->arg0p;
        comp->ene_wp_dist = (float)e->edat->fcs_tab[(int)e->cur_wep].range;
    } else if (em->type == 4) {
        AcPlayerSt *a4 = (AcPlayerSt *)em->arg0p;
        c->ene_wp_dist = a4->param.n_weapon ? a4->param.weapons[a4->equip_weapon].range : 8.0f;
    } else {
        c->ene_wp_dist = 8.0f;
    }
    if (com_acp) {
        _spell2 *p = Com_GetNearestMissileSpell(comp);
        comp->nearest_misp = p;
        comp->nearest_bltp = p;
    } else {
        comp->nearest_bltp = 0;
        comp->nearest_misp = 0;
    }
}

/* Own state: position / angles (with the previous frame's), velocity, view angles, size, lock-on time, energy rate
 * (0 while charging, 1 while the OB limiter is off). */
static void com_update_selfinfo(void)
{
    PS2_FRAME(0x20);
    if (com_acp) {
        memcpy(comp->loc_old, comp->loc, 16);
        memcpy(comp->loc, com_acp->mp->loc, 16);
        memcpy(comp->ang_old, comp->ang, 16);
        memcpy(comp->ang, com_acp->mp->ang, 16);
        memcpy(comp->velocity, com_acp->velocity, 16);
        memcpy(comp->viewang, com_acp->view.ang, 16);
        comp->tall = com_acp->tall;
        comp->R = com_acp->R;
        if (com_acp->stat & 0x40) comp->locked_time++;
        else comp->locked_time = 0;
    } else {
        memcpy(comp->loc_old, comp->loc, 16);
        memcpy(comp->loc, com_enp->mp->loc, 16);
        memcpy(comp->ang_old, comp->ang, 16);
        memcpy(comp->ang, com_enp->mp->ang, 16);
        memcpy(comp->velocity, com_enp->vel, 16);
        comp->viewang[0] = com_enp->view.angx;
        comp->viewang[1] = com_enp->view.angy;
        comp->tall = com_enp->mp->tall;
        comp->R = com_enp->mp->R;
        comp->locked_time = 0;
    }
    AcPlayerSt *a = com_acp;
    float num = a ? (float)a->energyReal : (float)com_enp->booster;
    float den = a ? (float)a->param.boost_max : (float)(com_enp->edat->booster << 3);
    comp->energy_rate = ps2_div(num, den);
    if (com_acp) {
        AcPartsObSt *ob = AcCtrl_GetPartsOb(com_acp);
        if (AcCtrl_IsCharging(com_acp)) comp->energy_rate = 0.0f;
        else if (ob && ob->limitoff_time) comp->energy_rate = 1.0f;
    }
}

/* EO (orbit pod): toggled on (pad 4) after seeing the enemy for a while with heat, energy, range and ammo to spare,
 * toggled off when one of the alerts trips, the enemy leaves the range or the ammo runs out. */
static void ComEo_MainProc(void)
{
    PS2_FRAME(0x10);
    if (!com_acp) return;
    stCom *c = comp;
    if (c->eomng.canuse != 1) return;
    c->pad &= ~4u;
    AcPartsEoSt *eo = AcCtrl_GetPartsEo(com_acp);
    c = comp;
    if (!c->ene_mp) {
        if (eo && eo->stat == 3) {
            c->pad |= 4;
            comp->eomng.onuse = 0;
        }
        return;
    }
    if (c->f_can_see_enemy) {
        if (c->eomng.onsight < 6000) c->eomng.onsight++;
    } else {
        c->eomng.onsight = 0;
    }
    if (eo && eo->stat == 0 && eo->blt_cnt != 0) {
        short bc = eo->blt_cnt;
        c = comp;
        if (!(c->eomng.onsight < c->eomng.sight) && com_acp->temperature < (float)c->eomng.heat &&
            !(c->energy_rate <= (float)c->eomng.energy / 100.0f) && c->elenxz <= (float)c->eomng.dist_max &&
            !(bc < c->eomng.ammo)) {
            c->pad |= 4;
            comp->eomng.onuse = 1;
            return;
        }
    }
    if (eo && eo->stat == 3) {
        c = comp;
        stComData *d = c->datap;
        if (!(com_acp->temperature <= (float)d->alert_heat) || c->energy_rate < (float)d->alert_energy / 100.0f ||
            !(comp->elenxz <= (float)comp->eomng.dist_max) || eo->blt_cnt == 0) {
            c->pad |= 4;
            comp->eomng.onuse = 0;
        }
    }
}

/* Pops the next key of one pad channel's delayed key ring into comp->pad. */
static inline void pad_key_pop(struct_6272fea9 *k)
{
    unsigned short rid = k->rid;
    unsigned short key = k->keybuff[rid].key;
    short fr = --k->keybuff[rid].frame;
    if (fr < 1) {
        unsigned int r = k->rid + 1u;
        if (r > 7) r = 0;
        k->rid = (unsigned short)r;
    }
    comp->pad |= key;
}

unsigned int DF_ComInKey(int com_no)
{
    PS2_FRAME(0x40);
    g_NoMoveFlag = 0;
    if (!(com_acp ? AcCtrl_IsCharging(com_acp) : 0)) s_MovePadNoChangeFrame = 0x3c;
    /* (sic) tested on the previous COM's data, before comp is switched to this one */
    if (!comp->datap->tree_buff[0x22]) return 0;
    comp = &com_info[com_no];
    if (com_info[com_no].type == 1) {
        com_acp = 0;
        com_enp = com_info[com_no].body.com_enp;
        DFene_SetThink(com_info[com_no].chrid, -1);
        if (com_enp->cns_flag.thinkoff) {
            comp->pad_old = comp->pad;
            comp->pad = 0;
            return 0;
        }
    } else if (com_info[com_no].type == 0) {
        com_acp = com_info[com_no].body.com_acp;
        com_enp = 0;
        if (com_acp->comthinkoff) {
            com_info[com_no].pad_old = com_info[com_no].pad;
            comp->pad = 0;
            return 0;
        }
    }
    if (ps2_mod32((int)FRAME, 6) == 0 && comp->trgmng.proc_hatelist)
        gcall(comp->trgmng.proc_hatelist, &comp->trgmng, comp->trgmng.work);

    /* target: re-pick when none, after select_min frames, or when the current one is gone */
    stCom *c = comp;
    StModel *prev = comp->ene_mp;
    int repick = 0;
    if (!prev) repick = 1;
    else {
        unsigned short t = comp->trgmng.select_time;
        comp->trgmng.select_time = t + 1;
        if (c->trgmng.select_min < t) repick = 1;
        else if (comp->ene_mp && (comp->ene_mp->use & 4)) repick = 1;
    }
    if (repick) {
        comp->ene_mp = Com_Tg_GetTarget(&comp->trgmng);
        if (com_enp) {
            com_enp->trg_mp = comp->ene_mp;
            if (!com_enp->trg_mp) com_enp->trg_mp = g_ac[0].mp;
        }
    }
    if (prev == comp->ene_mp) comp->chase_time++;
    else comp->chase_time = 0;
    comp->pad_old = comp->pad;
    comp->pad = 0;
    com_update_selfinfo();
    com_update_eneinfo();
    comp->f_setcannon_dis = 0;
    comp->f_no_avoid = 0;
    comp->f_lose_dis = 0;
    ComTree_SelMainProc(ComData[com_no].tree_buff);

    /* run the selected op, then steer to its move target */
    int sel = comp->op_sel;
    if (sel < comp->op_num && sel >= 0) {
        stComOpMng *op = &comp->opmng[sel];
        if ((op->datap->f_enable_void || comp->ene_mp) && op->main) {
            gcall(op->main, op, &op->wk);
            if (!comp->f_op_pad) {
                compad_turn_to_vec(comp->move_ang);
                float dx = comp->move_loc[0] - comp->loc[0];
                float dz = comp->move_loc[2] - comp->loc[2];
                float d2 = dx * dx;
                d2 = d2 + dz * dz;
                if (!(d2 < 9.0f)) compad_ang_move(inverse_angf(ps2_atan2f(dx, dz)));
                compad_select_move(op);
            }
            comp->f_op_pad = 0;
        }
    }
    if (com_acp) {
        c = comp;
        if (comp->laumng.type != 4) {
            auto fn = comp->laumng.actfunc;
            unsigned int t = comp->laumng.time_act;
            unsigned short st = comp->laumng.step;
            if (fn) gcall(fn, &comp->laumng, &comp->laumng.wk);
            if (c->laumng.step == st && c->laumng.time_act == t) c->laumng.time_act = t + 1;
        }
        c = comp;
        unsigned short st = comp->extmng.step;
        unsigned int t = comp->extmng.time_act;
        stComExtMng *em = &comp->extmng;
        AcPartsExtSt *ext = AcCtrl_GetPartsExt(com_acp);
        auto fn = c->extmng.actfunc;
        if (fn && ext) gcall(fn, em, ext);
        if (em->step == st && c->extmng.time_act == t) c->extmng.time_act = t + 1;
        ComEo_MainProc();
    }
    shoot_weapon();
    /* locked on and charging a blade attack in range: hold still (no blade / boost / move keys this frame).
     * com_acp is always set for versus COMs; the original reads through NULL for enemy COMs. */
    AcPlayerSt *a = com_acp;
    if (a && a->lockmng.lock[0].ofp && (a->lockmng.lock[0].lock_stat & 2) &&
        comp->opmng[(int)comp->op_sel].time_sel < comp->opmng[(int)comp->op_sel].datap->time_max && a->actno == 0x10 &&
        a->weaponctrl[(int)comp->wep_sel].busytime < 1) {
        comp->padmng.blade.status = 0;
        comp->padmng.boost.status = 0;
        comp->padmng.move.status = 0;
    }
    ComPad_SendPad(&comp->padmng.move, (unsigned short)comp->pad & 0xc50);
    ComPad_SendPad(&comp->padmng.turn, (unsigned short)comp->pad & 0xa0);
    ComPad_SendPad(&comp->padmng.boost, (unsigned short)comp->pad & 0x4000);
    ComPad_SendPad(&comp->padmng.shot, (unsigned short)comp->pad & 0x8000);
    ComPad_SendPad(&comp->padmng.blade, (unsigned short)comp->pad & 0x2000);
    comp->pad &= 0xffff130f;
    pad_key_pop(&comp->padmng.move);
    pad_key_pop(&comp->padmng.turn);
    pad_key_pop(&comp->padmng.boost);
    pad_key_pop(&comp->padmng.shot);
    pad_key_pop(&comp->padmng.blade);
    if (g_NoMoveFlag) comp->pad &= 0xffff93af;

    /* purge (drop) empty weapons / launcher / extension */
    if (com_acp) {
        if (comp->f_wep_purge) {
            if (comp->pad & 0x1000) {
                comp->pad &= 0xffffdfff;
                comp->pad |= 0x1f00;
                comp->f_wep_purge = 0;
                comp->purged_state[com_acp->equip_weapon] |= 1;
            }
        } else if (comp->f_lau_purge == 1) {
            comp->pad = 0;
            comp->pad |= 0x2f00;
            comp->f_lau_purge = 0;
            comp->purged_state[3] |= 1;
        } else {
            if (comp->purge_count) comp->purge_count--;
            if (!comp->purge_count && !(comp->pad_old & 0x1000)) {
                if (!(comp->purged_state[3] & 1) && (comp->purged_state[3] & 2)) {
                    comp->pad = 0;
                    comp->pad |= 0x2f00;
                    comp->purged_state[3] |= 1;
                    comp->purge_count = 300;
                }
                if (!(comp->purged_state[com_acp->equip_weapon] & 1) && (comp->purged_state[com_acp->equip_weapon] & 2)) {
                    comp->pad = 0;
                    comp->pad |= 0x1f00;
                    comp->purged_state[com_acp->equip_weapon] |= 1;
                    comp->purge_count = 300;
                }
                if (comp->f_ext_purge) {
                    comp->pad = 0;
                    comp->pad |= 0xf02;
                    comp->purge_count = 300;
                }
            }
        }
    }
    if (com_enp) DFene_SetPad(comp->chrid, (unsigned short)comp->pad);
    if ((com_acp ? AcCtrl_IsCharging(com_acp) : 0) && !g_NoMoveFlag) comp->pad |= s_MovePad;
    return comp->pad;
}

/* Falling fast enough that boosting now would not stop the drop before the floor ("dossun" = a hard landing):
 * the height lost while the fly-up acceleration cancels vy exceeds the height above the floor. */
int com_is_dossun_speed(void)
{
    AcPlayerSt *a = com_acp;
    if (!com_airborne()) return 0;
    float vy = comp->velocity[1];
    if (vy <= 0.0f) return 0;
    if (a && a->param.abl_dossunoff) return 0;
    float acc = a ? a->param.flyup_accel : com_enp->edat->f_accel;
    int n = ps2_ftoi(ps2_div(vy, acc));
    int sum = com_tri_sum(n);
    float acc2 = a ? a->param.flyup_accel : com_enp->edat->f_accel;
    float fl = a ? a->floor_high : com_enp->floor_Y;
    float p1 = vy * (float)n;          /* mula vy, n */
    float p2 = acc2 * (float)sum;      /* msub acc, sum */
    float h = p1 - p2;
    return !(h <= fl - comp->loc[1]);
}

/* Boost while moving, for leg types that can boost on the ground (not while already flying / boost-landing). */
static void com_leg_boost(void)
{
    int lt = com_legtype();
    if (!(lt == 0 || lt == (com_acp ? 2 : 1) || lt == 3)) return;
    if (!(comp->pad & 0xc50)) return;
    if (com_act() != (com_acp ? 8 : 3)) comp->pad |= 0x4000;
}

/* Movement style for the running operation: candidate weights from the op data (0 stay, 1 boost-dash, 2 jump /
 * hop, 3 fly to the target height, 4 OB / pad-4 action), scaled by an energy factor and switched off by region /
 * heat / leg limits; the heaviest wins (second = the previous best, not the true runner-up). */
void compad_select_move(stComOpMng *opmngp)
{
    PS2_FRAME(0xb0);
    stOpData *od = opmngp->datap;
    float best = 0.0f;
    int release = 1, bestIdx = -1, second = -1;
    if (com_acp ? AcCtrl_IsCharging(com_acp) : 0) return;
    float w0 = (float)od->mv_value[0] / 100.0f;
    float w1 = (float)od->mv_value[1] / 100.0f;
    float w2 = (float)od->mv_value[2] / 100.0f;
    float w3 = (float)od->mv_value[3] / 100.0f;
    float w4 = (float)od->mv_value[4] / 100.0f;
    if (com_acp) {
        if (com_acp->param.corep->func_type != 1) w4 = 0.0f;
        stCom *c = comp;
        if (c->pad & 0x40) w4 = 0.0f;
        float (*rg)[4] = CopyOfSysInfo.rgnalert;   /* [0] max corner, [1] min corner */
        int bits = 0;
        if (c->loc[0] < rg[1][0] - -50.0f) bits |= 1;
        if (!(c->loc[0] <= rg[0][0] + -50.0f)) bits |= 2;
        if (c->loc[2] < rg[1][2] - -50.0f) bits |= 4;
        if (!(c->loc[2] <= rg[0][2] + -50.0f)) bits |= 8;
        if (bits) w4 = 0.0f;
        if (com_acp->areastat) w4 = 0.0f;
        /* the -20 region bits: only "below the floor of the region" (0x20) is used */
        if (c->loc[1] < rg[1][1] - -20.0f) w3 = 0.0f;
        if (!(w4 == 0.0f)) {
            float h = AcCtrl_HeatSimulation(com_acp, com_acp->temperature, com_acp->param.cqheatEobFirst, 0x5a);
            h = AcCtrl_HeatSimulation(com_acp, h, com_acp->param.cqheatEobBusy, 0x3c);
            if (!(h <= (float)comp->datap->alert_heat)) w4 = 0.0f;
        }
    } else {
        w4 = 0.0f;
    }
    if (com_legtype() == (com_acp ? 1 : 2)) w1 = 0.0f;
    stCom *s0 = comp;
    float e;
    if (s0->energy_rate < 0.8f) {
        int r = ps2_rand();
        float f2 = (float)(ps2_mod32(r, 20) + 90) / 100.0f;
        float f0 = (float)s0->datap->alert_energy / 100.0f;
        f0 = f0 * f2;
        e = fz_large(s0->energy_rate, 0.1f + f0, 0.1f);
    } else {
        e = 1.0f;
    }
    s0 = comp;
    if (!(s0->eloc[1] <= (float)od->fly_height)) w3 = 0.0f;
    float cand[5];
    cand[1] = w1 * e;
    cand[2] = 0.65f * w2;
    cand[0] = w0;
    cand[4] = w4 * e;
    cand[3] = 2.0f * (w3 * e);
    for (int i = 0; i < 5; i++) {
        if (!(cand[i] <= best)) {
            second = bestIdx;
            best = cand[i];
            bestIdx = i;
        }
    }
    if (com_airborne() && com_is_dossun_speed()) {
        stComData *dp = comp->datap;
        int r = ps2_rand();
        if (ps2_mod32(r, 100) < dp->skl_data[1].rate && !(comp->energy_rate <= (float)dp->skl_data[1].lvl / 100.0f)) {
            compad_fly();
            return;
        }
    }
    if (bestIdx == 4) {
        if (com_acp && com_acp->actno != 0x12 && !(comp->pad_old & 4)) comp->pad |= 4;
        bestIdx = second;
        release = 0;
    }
    switch (bestIdx) {
    case 3: {
        stCom *c = comp;
        if (!(c->loc[1] < c->move_loc[1] - com_acp->mp->tall) &&
            (!(c->loc[1] < c->move_loc[1]) || !(com_acp->velocity[1] < 0.0f)))
            compad_fly();
        break;
    }
    case 2: {
        stCom *c = comp;
        if (c->jump_ct > 0) c->jump_ct--;
        if (com_airborne()) {
            c = comp;
            if (c->jump_stat != 1) c->jump_ct = od->air_delay;
            comp->jump_stat = 1;
            if (e == 0.0f) break;
            if (comp->jump_ct > 0) compad_fly();
            else com_leg_boost();
        } else {
            c = comp;
            if (c->jump_stat != 0) c->jump_ct = od->rand_delay;
            comp->jump_stat = 0;
            if (e == 0.0f) break;
            if (comp->jump_ct == 0) compad_fly();
            else com_leg_boost();
        }
        break;
    }
    case 1:
        com_leg_boost();
        break;
    default:
        break;
    }
    /* release the pad-4 toggle when another candidate won */
    if (release && com_acp && com_acp->actno == 0x12 && !(comp->pad_old & 4)) comp->pad |= 4;
}

/* Vertical view keys (0x100 up / 0x200 down) toward the pitch difference d, unless the camera would overshoot: the
 * threshold is the angle covered while braking from the next speed. (sic) It is compared with ctoe_ang_x (the pitch to
 * the enemy) for every caller. */
static void com_view_pitch(float d)
{
    float acc = AC_CameraParam.view_rotacc;
    if (!(d < 0.0f)) {
        float spd = com_acp ? com_acp->view.angspd[0] : com_enp->look_spd;
        if (spd < -acc) return;
        float v = acc + spd;
        if (!(v <= AC_CameraParam.view_rotmaxspd)) v = AC_CameraParam.view_rotmaxspd;
        float brk = AC_CameraParam.view_rotbrk;
        int n = ps2_ftoi(ps2_div(v, brk));
        int sum = com_tri_sum(n);
        float p1 = v * (float)(n + 1);
        float p2 = brk * (float)sum;
        if (!(comp->ctoe_ang_x <= p1 - p2)) comp->pad |= 0x100;
    } else {
        float spd = com_acp ? com_acp->view.angspd[0] : com_enp->look_spd;
        if (!(spd <= acc)) return;
        float v = spd - acc;
        float mx = -AC_CameraParam.view_rotmaxspd;
        if (v < mx) v = mx;
        float brk = AC_CameraParam.view_rotbrk;
        int n = ps2_ftoi(ps2_div(-v, brk));
        int sum = com_tri_sum(n);
        float p1 = v * (float)(n + 1);
        float p2 = brk * (float)sum;
        if (comp->ctoe_ang_x < p1 + p2) comp->pad |= 0x200;
    }
}

void compad_turn_to_ene(void)
{
    PS2_FRAME(0x10);
    com_view_pitch(sub_angf(comp->eang[0], comp->viewang[0]));
}

/* Turn (yaw keys) and pitch the view toward a direction vector. */
void compad_turn_to_vec(float *ang)
{
    PS2_FRAME(0x20);
    compad_turn_ang(inverse_angf(ps2_atan2f(ang[0], ang[2])));
    float x2 = ang[0] * ang[0];
    float z2 = ang[2] * ang[2];
    float h = ps2_sqrt(x2 + z2);
    float p = ps2_atan2f(-ang[1], h);
    com_view_pitch(sub_angf(p, comp->viewang[0]));
}

/* Yaw after holding the turn key for pushtime frames toward angdif and then braking to a stop. */
static float get_nextrotang(float angdif, float acc, float maxspd, int pushtime)
{
    PS2_FRAME(0x40);
    float a = comp->ang[1];
    float spd = com_acp ? com_acp->turnspd : com_enp->turn_spd;
    if (!com_airborne() && com_act() == 0) spd = 0.0f;
    if (!(angdif <= 0.0f)) {
        do {
            AddF_max(&spd, acc, maxspd);
            a = a + spd;
        } while (--pushtime > 0);
    } else {
        float nacc = -acc, nmax = -maxspd;
        do {
            AddF_min(&spd, nacc, nmax);
            a = a + spd;
        } while (--pushtime > 0);
    }
    if (!(spd <= 0.0f)) {
        while (!(spd <= acc)) {
            spd = spd - acc;
            a = a + spd;
        }
    } else {
        float nacc = -acc;
        while (spd < nacc) {
            spd = spd + acc;
            a = a + spd;
        }
    }
    while (!(a <= COM_PI)) a = a - COM_PI2;
    while (a < -COM_PI) a = a + COM_PI2;
    return a;
}

/* Returns a key still queued in the channel's ring (the slot being written counts only while it is active). */
int ComPad_KeyExist(struct_6272fea9 *keymngp)
{
    struct_6272fea9 *k = keymngp;
    unsigned short w = k->wid, r = k->rid;
    if (k->keybuff[w].key && k->keybuff[w].frame) return k->keybuff[r].key;
    for (unsigned int i = r; i != w;) {
        if (k->keybuff[i].frame && k->keybuff[i].key) return k->keybuff[i].key;
        i = i + 1;
        if (!(i < 8)) i = 0;
    }
    return 0;
}

/* One pad channel: a new key waits push_delay frames (status 1), is held (2) while the request stays, then held
 * push_frame more frames (3); the result is appended to the key ring (same key -> longer entry). */
static void ComPad_SendPad(struct_6272fea9 *keymngp, unsigned short pad)
{
    struct_6272fea9 *k = keymngp;
    unsigned short out = pad;
    switch (k->status) {
    case 3:
        if (k->wait == 0) {
            k->status = 0;
            k->key = 0;
        }
        out = k->key;
        break;
    case 2:
        if (pad != k->key) {
            if (k->push_frame) {
                k->status = 3;
                k->wait = k->push_frame;
                out = k->key;
            } else {
                k->status = 0;
                k->key = 0;
                k->wait = 0;
            }
        }
        break;
    case 1:
        if (k->wait == 0) {
            k->status = 2;
            out = k->key;
        } else {
            out = 0;
        }
        break;
    case 0:
        if (pad != 0) {
            if (k->push_delay) {
                k->status = 1;
                k->wait = k->push_delay;
                k->key = pad;
                out = 0;
            } else {
                k->status = 2;
                k->wait = 0;
                k->key = pad;
            }
        }
        break;
    default:
        break;
    }
    unsigned short w = k->wid;
    if (k->keybuff[w].key == out) {
        k->keybuff[w].frame++;
    } else {
        k->wid = w + 1;
        if (!(k->wid < 8)) k->wid = 0;
        k->keybuff[k->wid].key = out;
        k->keybuff[k->wid].frame = 1;
    }
    if (k->wait) k->wait--;
}

/* Yaw keys toward trgang (0x20 right / 0x80 left), unless holding the key would overshoot or the difference is
 * below the minimum step. */
void compad_turn_ang(float trgang)
{
    PS2_FRAME(0x30);
    struct_6272fea9 *t = &comp->padmng.turn;
    float dif = add_angf(trgang, -comp->ang[1]);
    float mx = com_acp ? com_acp->param.turn_maxspd : com_enp->edat->t_max;
    float ac = com_acp ? com_acp->param.turn_accel : com_enp->edat->t_accel;
    float nx = get_nextrotang(dif, ac, mx, t->push_frame + 1);
    float d = sub_angf(nx, comp->ang[1]);
    float adif = dif < 0.0f ? -dif : dif;
    float ad = d < 0.0f ? -d : d;
    if (!(ad < adif)) return;
    if (!(d * dif < 0.0f) && !(adif < comp->param.yang_delayadd)) {
        /* press */
    } else {
        if (ComPad_KeyExist(t)) return;
        if (adif < comp->param.yang_minadd) return;
    }
    if (!(dif <= 0.0f)) comp->pad |= 0x20;
    else comp->pad |= 0x80;
}

/* Boost (fly) key; for boosting leg types also drops the move keys (keeps them while the AC is in action 0x10), and
 * releases the key in the boost-landing action when it was already held. */
void compad_fly(void)
{
    comp->pad |= 0x4000;
    int lt = com_legtype();
    if (!(lt == 0 || lt == (com_acp ? 2 : 1) || lt == 3)) return;
    if (com_act() == (com_acp ? 5 : 1)) {
        if (comp->pad_old & 0x4000) comp->pad &= ~0x4000u;
    } else if (com_act() != (com_acp ? 8 : 3)) {
        if (com_act() == 0x10) comp->pad &= 0xfffff3af;
        else comp->pad &= 0xfffff30f;
    }
}

void compad_jump(void)
{
    float j = com_acp ? (float)com_acp->param.abl_jump : com_enp->edat->jump;
    if (j == 0.0f) return;
    if (com_airborne()) return;
    if (ComPad_KeyExist(&comp->padmng.move)) return;
    comp->pad |= 0x4000;
}

void compad_loc_move_xyz(float *loc)
{
    PS2_FRAME(0x10);
    float dx = loc[0] - comp->loc[0];
    float dz = loc[2] - comp->loc[2];
    float dy = loc[1] - comp->loc[1];
    float y2 = dy * dy;
    float x2 = dx * dx;
    float s = x2 + y2;   /* adda; madd dz, dz */
    float z2 = dz * dz;
    if (s + z2 < 9.0f) return;
    compad_ang_move(inverse_angf(ps2_atan2f(dx, dz)));
}

void compad_loc_move(float *loc)
{
    PS2_FRAME(0x10);
    float dx = loc[0] - comp->loc[0];
    float dz = loc[2] - comp->loc[2];
    float x2 = dx * dx;
    float z2 = dz * dz;
    if (x2 + z2 < 9.0f) return;
    compad_ang_move(inverse_angf(ps2_atan2f(dx, dz)));
}

/* Move keys toward go_angle relative to the body yaw (0x10 forward, 0x40 back, 0x800 / 0x400 sideways), also kept in
 * s_MovePad; while the AC charges the keys are only re-chosen every 61 frames (DF_ComInKey resets the counter). */
static void compad_ang_move(float go_angle)
{
    PS2_FRAME(0x10);
    float d = sub_angf(go_angle, comp->ang[1]);
    if (s_MovePadNoChangeFrame < 0x3c) {
        s_MovePadNoChangeFrame++;
        return;
    }
    s_MovePad = 0;
    s_MovePadNoChangeFrame = 0;
    float ad = d < 0.0f ? -d : d;
    if (ad <= std::bit_cast<float>(0x3f96cbe4u)) {          /* 3/8 PI */
        comp->pad |= 0x10;
        s_MovePad |= 0x10;
    } else if (!(ad < std::bit_cast<float>(0x3ffb53d1u))) { /* 5/8 PI */
        comp->pad |= 0x40;
        s_MovePad |= 0x40;
    }
    if (!(d <= std::bit_cast<float>(0x3ec90fdbu)) && d < std::bit_cast<float>(0x402feddfu)) {   /* 1/8, 7/8 PI */
        comp->pad |= 0x800;
        s_MovePad |= 0x800;
    } else if (d < std::bit_cast<float>(0xbec90fdbu) && !(d <= std::bit_cast<float>(0xc02feddfu))) {
        comp->pad |= 0x400;
        s_MovePad |= 0x400;
    }
}

/* Region test bits: 1 / 2 x below / above, 0x20 / 0x10 y below / above, 4 / 8 z below / above (minmax[0] = max
 * corner, [1] = min corner, widened by ofsrange). */
int com_check_in_region_xyz(float *loc, float (*minmax)[4], float ofsrange)
{
    int r = 0;
    if (loc[0] < minmax[1][0] - ofsrange) r |= 1;
    if (!(loc[0] <= minmax[0][0] + ofsrange)) r |= 2;
    if (loc[1] < minmax[1][1] - ofsrange) r |= 0x20;
    if (!(loc[1] <= minmax[0][1] + ofsrange)) r |= 0x10;
    if (loc[2] < minmax[1][2] - ofsrange) r |= 4;
    if (!(loc[2] <= minmax[0][2] + ofsrange)) r |= 8;
    return r;
}

int com_check_in_region(float *loc, float (*minmax)[4], float ofsrange)
{
    int r = 0;
    if (loc[0] < minmax[1][0] - ofsrange) r |= 1;
    if (!(loc[0] <= minmax[0][0] + ofsrange)) r |= 2;
    if (loc[2] < minmax[1][2] - ofsrange) r |= 4;
    if (!(loc[2] <= minmax[0][2] + ofsrange)) r |= 8;
    return r;
}

/* EO settings from the AI data (zero = default); cores without an EO (func_type 1) never use it. */
static void ComEo_Init(stComData *pComData)
{
    if (com_acp->param.corep->func_type == 1) {
        comp->eomng.canuse = 0;
        return;
    }
    comp->eomng.dist_max = pComData->eo_data.dist_max;
    comp->eomng.heat = pComData->eo_data.heat;
    comp->eomng.canuse = pComData->eo_data.canuse;
    comp->eomng.energy = pComData->eo_data.energy;
    comp->eomng.ammo = pComData->eo_data.ammo;
    comp->eomng.sight = pComData->eo_data.sight;
    comp->eomng.onuse = 0;
    comp->eomng.onsight = 0;
    if (comp->eomng.heat == 0) comp->eomng.heat = 200;
    if (comp->eomng.sight == 0) comp->eomng.sight = 60;
    if (comp->eomng.energy == 0) comp->eomng.energy = 60;
    if (comp->eomng.ammo == 0) comp->eomng.ammo = 10;
    if (comp->eomng.ammo == 0) comp->eomng.ammo = 10;   /* (sic) twice */
    if (comp->eomng.dist_max == 0) comp->eomng.dist_max = 200;
}

static void pad_key_init(struct_6272fea9 *k, struct_b32d54b7 *kd)
{
    memset(k, 0, 0x30);
    k->push_delay = kd->delay;
    k->push_frame = kd->frame;
    k->status = 0;
    k->wait = 0;
    k->key = 0;
    k->rid = 0;
    k->wid = 1;
    k->keybuff[0].key = 0;
    k->keybuff[0].frame = 0;
}

/* Angle covered while a held key accelerates for n frames and then brakes to a stop. */
static float spin_sum(float acc, float max, float brk, int n)
{
    float f = 0.0f, s = 0.0f;
    for (int i = 0; i < n; i++) {
        AddF_max(&f, acc, max);
        s = s + f;
    }
    do {
        AddF_min(&f, -brk, 0.0f);
        s = s + f;
    } while (0.0f < f);
    return s;
}

static void ComPad_Init(stCom *comp, AcPlayerSt *acp)
{
    PS2_FRAME(0x90);
    pad_key_init(&comp->padmng.move, &ComData[comp->comid].move_keydata);
    pad_key_init(&comp->padmng.view, &ComData[comp->comid].view_keydata);
    pad_key_init(&comp->padmng.turn, &ComData[comp->comid].turn_keydata);
    pad_key_init(&comp->padmng.boost, &ComData[comp->comid].boost_keydata);
    pad_key_init(&comp->padmng.shot, &ComData[comp->comid].shot_keydata);
    pad_key_init(&comp->padmng.blade, &ComData[comp->comid].blade_keydata);
    comp->param.xang_minadd = spin_sum(AC_CameraParam.view_rotacc, AC_CameraParam.view_rotmaxspd,
                                       AC_CameraParam.view_rotbrk, 4);
    float tmax, tacc;
    if (!com_acp) {
        tmax = com_enp->edat->t_max;
        tacc = com_enp->edat->t_accel;
    } else {
        tmax = acp->param.turn_maxspd;
        tacc = acp->param.turn_accel;
    }
    comp->param.yang_minadd = spin_sum(tacc, tmax, tacc, comp->padmng.turn.push_frame + 1);
    comp->param.yang_delayadd =
        spin_sum(tacc, tmax, tacc, comp->padmng.turn.push_delay + 2 + comp->padmng.turn.push_frame);
    float lo = (AC_TempParam.turn_minadd * 3.1415927f) / 180.0f;
    if (comp->param.yang_delayadd < lo) comp->param.yang_delayadd = lo;
    float hi = (AC_TempParam.turn_maxadd * 3.1415927f) / 180.0f;
    if (hi < comp->param.yang_delayadd) comp->param.yang_delayadd = hi;
}

/* --- extension AIs (com_extinit_tbl[prog_no], run every think step through extmng.actfunc; pad 2 = extension key) */

/* The enemy AC carries "KAKU" ammo (weapon id 0x2f) and other prog-1 weapons: 1 = use the anti-KAKU extension weapon
 * (no KAKU / no others, our extension ammo below twice the KAKU ammo, or the enemy holds the KAKU weapon). */
static int comext_vsKAKU_equipchk(void)
{
    StModel *em = comp->ene_mp;
    unsigned int kaku = 0, other = 0;
    if (!em || em->type != 4) return 0;
    AcPlayerSt *e = (AcPlayerSt *)em->arg0p;
    unsigned char n = e->param.n_weapon;
    for (int i = 0; i < n; i++) {
        _weapon_st *w = &e->param.weapons[i];
        if (w->id == 0x2f) kaku += w->blt_cnt;
        else if (w->prog_no == 1) other += w->blt_cnt;
    }
    if (kaku == 0 || other == 0) return 1;
    if (!((int)com_acp->param.extweapon.blt_cnt < (int)(kaku << 1))) return 1;
    if (e->param.weapons[e->equip_weapon].id == 0x2f) return 1;
    return 0;
}

/* toggles the extension toward "on" exactly when equipchk says so */
static void comext_vsKAKUweapon(stComExtMng *extmngp, AcPartsExtSt *extctrlp)
{
    PS2_FRAME(0x20);
    if (comext_vsKAKU_equipchk()) {
        if (!extctrlp->busy) comp->pad |= 2;
    } else {
        if (extctrlp->busy) comp->pad |= 2;
    }
}

/* magazine: reload when the equipped gun (statid 0) is below fparam[0] percent */
static void __com_ExtInit_Magazine(void)
{
    PS2_FRAME(0x20);
    AcPlayerParamSt *pp = &com_acp->param;
    AcPtsResSt *res = AcCtrl_GetPartsRes(com_acp);
    stExtData *ed = &comp->datap->ext_data;
    if (!pp->gunp) return;
    PartsParamWeaponSt *pw = AcPtsRes_GetWeaponPtr(res, 5, 0);
    _weapon_st *w = &com_acp->param.weapons[com_acp->equip_weapon];
    if (w->statid != 0) return;
    float q = (float)w->blt_cnt / (float)pw->blt_sta;
    if (100.0f * q < ed->fparam[0]) comp->pad |= 2;
}

/* energy pack: below fparam[0] percent energy */
static void com_extinit_enepack(void)
{
    stCom *c = comp;
    if (c->energy_rate < c->datap->ext_data.fparam[0] / 100.0f) c->pad |= 2;
}

/* cooler: above fparam[0] heat */
static void com_extinit_cooler(stComExtMng *extmngp, AcPartsExtSt *extctrlp)
{
    stCom *c = comp;
    stExtData *ed = &c->datap->ext_data;
    if (extctrlp->reload_cnt) return;
    if (!(com_acp->temperature <= ed->fparam[0])) c->pad |= 2;
}

/* stealth: low hp, locked on for a while, a rate roll once a second */
static void com_extinit_stealth(stComExtMng *extmngp, AcPartsExtSt *extctrlp)
{
    PS2_FRAME(0x20);
    stCom *c = comp;
    stExtData *ed = &c->datap->ext_data;
    if (extctrlp->reload_cnt) return;
    int maxhp = com_acp ? com_acp->param.hp : com_enp->edat->hp;
    int hp = com_acp ? com_acp->hp : com_enp->nowhp;
    if (!(ps2_div((float)hp, (float)maxhp) <= (float)(unsigned short)ed->sparam[0] / 100.0f)) return;
    if (!((unsigned short)ed->sparam[1] < c->locked_time)) return;
    if (FRAME % 60 != 0) return;
    unsigned short rate = (unsigned short)ed->sparam[2];
    if (ps2_mod32(ps2_rand(), 100) < rate) comp->pad |= 2;
}

/* fz_small(elenxz, f + f/2, f/2) */
static inline float com_shield_fz(float f)
{
    float h = f / 2.0f;
    return fz_small(comp->elenxz, f + h, h);
}

/* PI * deg / 180 */
static inline float com_deg2rad(float deg) { return (COM_PI * deg) / 180.0f; }

/* side shield: on while the enemy is off to the side (fparam[1]..[0] degrees) within range (fparam[2] / [3]) and
 * there is energy (sparam[0]); off on low energy (sparam[1]) or after sparam[2] frames when that no longer holds */
static void com_extinit_sideshield(stComExtMng *extmngp, AcPartsExtSt *extctrlp)
{
    PS2_FRAME(0x20);
    stCom *c = comp;
    stExtData *ed = &c->datap->ext_data;
    if (extctrlp->busy) {
        if (!(c->energy_rate < (float)(unsigned short)ed->sparam[1] / 100.0f)) {
            if (!((unsigned short)ed->sparam[2] < extctrlp->acttime)) return;
            float a = c->ctoe_ang_y < 0.0f ? -c->ctoe_ang_y : c->ctoe_ang_y;
            if (a <= com_deg2rad(ed->fparam[0])) {
                if (!(a < com_deg2rad(ed->fparam[1]))) {
                    if (!(com_shield_fz(ed->fparam[3]) < 0.5f)) return;
                    if (!(com_shield_fz(ed->fparam[2]) < 0.5f)) return;
                }
            }
        }
        comp->pad |= 2;
        return;
    }
    float a = c->ctoe_ang_y < 0.0f ? -c->ctoe_ang_y : c->ctoe_ang_y;
    if (a <= com_deg2rad(ed->fparam[1])) return;
    if (!(a < com_deg2rad(ed->fparam[0]))) return;
    if (c->energy_rate <= (float)(unsigned short)ed->sparam[0] / 100.0f) return;
    if (!(com_shield_fz(ed->fparam[3]) <= 0.8f) || !(com_shield_fz(ed->fparam[2]) <= 0.8f)) comp->pad |= 2;
}

/* hover boost: on while airborne with energy and the enemy higher than the standard fly height above us */
static void com_extinit_hoverboost(stComExtMng *extmngp, AcPartsExtSt *extctrlp)
{
    stCom *c = comp;
    stExtData *ed = &c->datap->ext_data;
    StModel *em = c->ene_mp;
    if (!em) return;
    if (extctrlp->reload_cnt) return;
    if (extctrlp->busy) {
        if (c->energy_rate < (float)(unsigned short)ed->sparam[1] / 100.0f || !(com_acp->actstat & 1)) c->pad |= 2;
        return;
    }
    if (c->energy_rate <= (float)(unsigned short)ed->sparam[0] / 100.0f) return;
    if (!(com_acp->actstat & 1)) return;
    if (!(em->loc[1] - comp->loc[1] <= comp->param.std_fly_height)) c->pad |= 2;
}

/* multi boost: airborne, energy, enemy within fparam[0], view pitch inside fparam[1]..[2] degrees, a rate roll once a
 * second */
static void com_extinit_multiboost(stComExtMng *extmngp, AcPartsExtSt *extctrlp)
{
    PS2_FRAME(0x20);
    stCom *c = comp;
    stExtData *ed = &c->datap->ext_data;
    if (extctrlp->reload_cnt) return;
    if (!(com_acp->actstat & 1)) return;
    if (c->energy_rate <= (float)(unsigned short)ed->sparam[1] / 100.0f) return;
    if (!(c->elenxz < ed->fparam[0])) return;
    float va = c->viewang[0];
    if (va <= (COM_PI * ed->fparam[1]) / 180.0f) return;
    if (!(va < (COM_PI * ed->fparam[2]) / 180.0f)) return;
    if (FRAME % 60 != 0) return;
    unsigned short rate = (unsigned short)ed->sparam[0];
    if (ps2_mod32(ps2_rand(), 100) < rate) comp->pad |= 2;
}

/* turn boost: the enemy is more than fparam[0] degrees off and within fparam[1]: turn keys toward it + extension */
static void com_extinit_turnboost(stComExtMng *extmngp, AcPartsExtSt *extctrlp)
{
    stCom *c = comp;
    stExtData *ed = &c->datap->ext_data;
    if (!c->ene_mp) return;
    if (extctrlp->reload_cnt) return;
    float a = c->ctoe_ang_y < 0.0f ? -c->ctoe_ang_y : c->ctoe_ang_y;
    if (a <= com_deg2rad(ed->fparam[0])) return;
    if (!(c->elenxz < ed->fparam[1])) return;
    if (c->energy_rate <= (float)(unsigned short)ed->sparam[0] / 100.0f) return;
    c->pad &= 0xffffff5f;
    if (!(comp->ctoe_ang_y <= 0.0f)) comp->pad |= 0x80;
    else comp->pad |= 0x20;
    comp->pad |= 2;
}

/* first call only swaps in the anti-KAKU callback */
static void com_extinit_vsweapon(void)
{
    stCom *c = comp;
    c->extmng.step = 0;
    c->extmng.time_act = 0;
    c->extmng.actfunc = GFN(comext_vsKAKUweapon);
}

/* linked weapon: switch the extension on once, after one second */
static void __com_ExtInit_LinkWeapon(stComExtMng *pExtMng, AcPartsExtSt *pExt)
{
    if (pExtMng->step != 0 || pExtMng->time_act < 0x3d) return;
    if (!pExt->busy) comp->pad |= 2;
    pExtMng->step++;
}

/* back booster: every 30 frames a rate roll; enemy AC close (fparam[2]), energy, both facing each other (fparam[0] /
 * [1] degrees), and the landing point 50 back stays inside the region */
static void __com_ExtInit_BackBooster(void)
{
    PS2_FRAME(0x40);
    stCom *c = comp;
    stExtData *ed = &c->datap->ext_data;
    if (!c->ene_mp || c->ene_mp->type != 4) return;
    if (FRAME % 30 != 0) return;
    unsigned short rate = (unsigned short)ed->sparam[0];
    if (!(ps2_mod32(ps2_rand(), 100) < rate)) return;
    c = comp;
    if (!(c->elenxz < ed->fparam[2])) return;
    if (c->energy_rate <= (float)(unsigned short)ed->sparam[1] / 100.0f) return;
    float a = c->ctoe_ang_y < 0.0f ? -c->ctoe_ang_y : c->ctoe_ang_y;
    if (!(a < com_deg2rad(ed->fparam[0]))) return;
    a = c->etoc_ang_y < 0.0f ? -c->etoc_ang_y : c->etoc_ang_y;
    if (!(a < com_deg2rad(ed->fparam[1]))) return;
    AcPlayerSt *acp = com_acp;
    float x = acp->loc[0] + 50.0f * fsmath_SinVU0(acp->ang[1]);
    float s = 50.0f * fsmath_CosVU0(acp->ang[1]);
    float z = com_acp->loc[2] + s;
    float (*rg)[4] = CopyOfSysInfo.rgnalert;
    int bits = 0;
    if (x < rg[1][0] - -50.0f) bits |= 1;
    if (!(x <= rg[0][0] + -50.0f)) bits |= 2;
    if (z < rg[1][2] - -50.0f) bits |= 4;
    if (!(z <= rg[0][2] + -50.0f)) bits |= 8;
    if (!bits) comp->pad |= 2;
}

static void __com_ExtInit_NoExt(void)
{
}

#ifdef ACNB_SOURCEPORT
void (*const com_extinit_tbl[0x12])(stComExtMng *, AcPartsExtSt *) = {
    [](stComExtMng *, AcPartsExtSt *) { __com_ExtInit_BackBooster(); },
    [](stComExtMng *, AcPartsExtSt *) { __com_ExtInit_NoExt(); },
    __com_ExtInit_LinkWeapon,
    [](stComExtMng *, AcPartsExtSt *) { com_extinit_vsweapon(); },
    [](stComExtMng *, AcPartsExtSt *) { com_extinit_vsweapon(); },
    com_extinit_turnboost,
    [](stComExtMng *, AcPartsExtSt *) { __com_ExtInit_NoExt(); },
    com_extinit_multiboost,
    com_extinit_hoverboost,
    com_extinit_sideshield,
    com_extinit_stealth,
    com_extinit_cooler,
    [](stComExtMng *, AcPartsExtSt *) { com_extinit_enepack(); },
    [](stComExtMng *, AcPartsExtSt *) { __com_ExtInit_NoExt(); },
    __com_ExtInit_LinkWeapon,
    __com_ExtInit_LinkWeapon,
    [](stComExtMng *, AcPartsExtSt *) { __com_ExtInit_Magazine(); },
    [](stComExtMng *, AcPartsExtSt *) { __com_ExtInit_NoExt(); },
};
#endif

void ComInit(int com_no, int dummy, AcPlayerSt *acp)
{
    PS2_FRAME(0x60);
    stComData *pComData = &ComData[com_no];
    comp = &com_info[com_no];
    memset(comp, 0, 0x2c00);
    comp->comid = (unsigned char)com_no;
    comp->datap = pComData;
    comp->ene_mp = 0;
    comp->wep_sel = 0;
    comp->wep_sel_arm = 0;
    ZeroMemory(comp->missile_buff, 0x80);
    ZeroMemory(comp->purged_state, 4);
    if (dummy == 0) {   /* an AC */
        com_enp = 0;
        com_acp = acp;
        comp->body.com_acp = acp;
        comp->mp = com_acp->mp;
        comp->type = 0;
        comp->chrid = 0xff;
        if (com_acp->param.plus) com_acp->param.plus = 0xffff;
        AcUidev_SetKeyDeray(AcCtrl_GetUidev(com_acp), 0);
        if (acp->eye_sfx >= 0) {
            /* mono-eye colour from the roster AC's colour set */
            unsigned char *rgb = ComWare[com_no].color[0].color.rgb[3];
            int col[4] = {rgb[0], rgb[1], rgb[2], 0x80};
            DF_EYE2_ChangeColor(acp->eye_sfx, (int(*)[4])col);
        }
        COMLAU_Init(comp, (void *)com_acp->param.swordp, pComData);
        stCom *c = comp;
        PartsParamExtensionSt *ext = com_acp->param.extp;
        memset(&comp->extmng, 0, 0x40);
        if (ext && ext->prog_no < 0x12) {
            c->extmng.step = 0;
            c->extmng.time_act = 0;
            c->extmng.actfunc = com_extinit_tbl[ext->prog_no];
        }
        ComEo_Init(pComData);
    } else {            /* an enemy (mission COMs) */
        int id = dummy - 1;
        com_acp = 0;
        com_enp = &Enemy[id];
        comp->body.com_enp = com_enp;
        comp->mp = com_enp->mp;
        comp->type = 1;
        comp->chrid = (unsigned char)id;
        DFene_SetThink(id, -1);
    }
    comp->pad_old = 0;
    comp->pad = 0;
    ComPad_Init(comp, acp);
    ComTree_AnalyzeComTree(ComData[com_no].tree_buff, comp, pComData);
    Com_Init_Tgmanage(&comp->trgmng, &ComData[com_no].tg_data);
    Com_Tg_AddHlAll(comp);
    comp->wep_sel = -1;
    comp->wep_sel_arm = -1;
    comp->wepmng_equip = -1;
    comp->op_sel = 0;
}
