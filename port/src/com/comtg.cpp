/* src/com/comtg.cpp — COM target management (whole file). Each COM keeps a hate list (comp->trgmng: a linked list of
 * stComTgHl nodes, Memm_Alloc'd and never freed) of the models it may attack. _tg_general_proc (every 6 frames) decays
 * and clamps the hate values, Com_Tg_GetTarget scores every live candidate (weapon range fit, facing, closeness, a
 * random term, the enemy's HP) and returns the best one. Damage events (DF_ComDamage -> Com_Dmg_AddEvent, a 32-entry
 * buffer) are applied to every COM's hate values once per frame by Com_Dmg_Proc.
 * hate_table indices: [2] human-player bonus, [3] range fit, [4] closeness, [5] facing, [6] / [7] enemy HP high / low,
 * [8] random spread, [9] decay per second, [10] it hit me, [11] it hit my ally, [12] I hit it, [13] my ally hit it. */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

static const float COMTG_PI = 3.14159274f;   /* 0x40490fdb */

void Com_ChangeCFlag(AcPlayerSt *pCom, unsigned short cflag)
{
    pCom->mp->cflag &= 0xfff8;
    pCom->mp->cflag |= cflag;
}

int Com_Dmg_AddEvent(struct_e2063795 *ev)
{
    PS2_FRAME(0x10);
    int n = s_ComtgEvMng.onuse;
    if (n >= 0x20) return -1;
    if (!s_ComtgEvMng.event) return -2;
    memcpy(&s_ComtgEvMng.event[n], ev, 0x20);
    s_ComtgEvMng.onuse++;
    return s_ComtgEvMng.onuse;
}

void Com_Dmg_Init(void)
{
    PS2_FRAME(0x10);
    s_ComtgEvMng.onuse = 0;
    s_ComtgEvMng.event = (struct_e2063795 *)Memm_Alloc(1, 0x400, 0x10);
}

/* hit handler: the first node of the model that was hit gains hate_table[12] */
static int _tg_general_hit(stComTgMng *tgmngp, struct_fe3da112 *wkp)
{
    stComTgHl *hl = tgmngp->hl_first;
    if (!hl) return 0;
    for (; hl; hl = hl->hl_next) {
        if (hl->target == wkp->smp) {
            hl->hate = hl->hate + tgmngp->hate_table[12];
            break;
        }
    }
    return 1;
}

/* every 6 frames from DF_ComInKey: one-time init, then the per-second decay and the clamp to [hate_min, hate_max] */
static int _tg_general_proc(stComTgMng *tgmngp)
{
    PS2_FRAME(0x30);
    stComTgHl *hl = tgmngp->hl_first;
    if (!hl) return 0;
    if (tgmngp->init_hatelist) gcall(tgmngp->init_hatelist, tgmngp, (void *)tgmngp->work);
    for (; hl; hl = hl->hl_next) {
        int h = hl->hate;
        if (FRAME % 60 == 0) h = h - tgmngp->hate_table[9];
        if (hl->hate_max < h) h = hl->hate_max;
        if (h < hl->hate_min) h = hl->hate_min;
        hl->hate = (unsigned short)h;
    }
    return 1;
}

/* once: human players' ACs get the hate_table[2] bonus on every value (hate_max capped at 0xffff) */
static int _tg_general_init(stComTgMng *tgmngp)
{
    stComTgHl *hl = tgmngp->hl_first;
    if (!hl) return 0;
    for (; hl; hl = hl->hl_next) {
        StModel *m = hl->target;
        AcPlayerSt *a = m->type == 4 ? (AcPlayerSt *)m->arg0p : 0;
        if (!a || !(a->id < NowUserPlayerNum)) continue;
        hl->hate = hl->hate + tgmngp->hate_table[2];
        hl->hate_ini = hl->hate_ini + tgmngp->hate_table[2];
        hl->hate_min = hl->hate_min + tgmngp->hate_table[2];
        int v = hl->hate_max + tgmngp->hate_table[2];
        hl->hate_max = v < 0xffff ? (unsigned short)v : 0xffff;
    }
    tgmngp->init_hatelist = 0;
    return 1;
}

/* Scores each live candidate and returns the best (NULL if none). One rand() per scored node, in list order.
 * (sic) The facing term uses atan2 + own yaw (not a difference), and the HP ratio is an integer division (0 below
 * full HP, 1 at full HP) tested against percent thresholds. */
StModel *Com_Tg_GetTarget(stComTgMng *tgmngp)
{
    PS2_FRAME(0xc0);
    stComTgMng *tg = tgmngp;
    stComTgHl *hl = tg->hl_first;
    unsigned char oldsel = tg->select_id;
    int idx = 0, found = 0, best = -1, bestIdx = 0;
    for (; hl; hl = hl->hl_next, idx++) {
        StModel *m = hl->target;
        int h = hl->hate;
        if (m->use & 4) continue;
        AcPlayerSt *a;
        if (m->type == 4) {
            a = (AcPlayerSt *)m->arg0p;
            if (a->dead) continue;
            if (a->comthinkoff == 1) continue;
            if (a->areaovered != 0) continue;
        } else {
            a = 0;
            if (!m->on_layer && !(m->use & 0x200)) continue;
            unsigned short u = hl->target->use;
            if (u & 1) continue;
            if (u & 2) continue;
        }
        float dx = hl->target->loc[0] - comp->loc[0];
        float dz = hl->target->loc[2] - comp->loc[2];
        int e = comp->wepmng_equip;
        stComWepData *wd;
        if (e >= 0 && (wd = comp->wepmng[e].datap) != 0) {
            float dist = ps2_sqrt(dx * dx + dz * dz);
            h += ps2_ftoi((float)tg->hate_table[3] * fz_equal(dist, (float)wd->dist_target, 100.0f));
        }
        if (ps2_sqrt(dx * dx + dz * dz) < 600.0f) {
            float f = ps2_atan2f(dx, dz);
            h += ps2_ftoi((float)tg->hate_table[5] * fz_equal(f + comp->ang[1], 0.0f, COMTG_PI));
        }
        h += ps2_ftoi((float)tg->hate_table[4] * fz_small(ps2_sqrt(dx * dx + dz * dz), 300.0f, 200.0f));
        int r = ps2_rand();
        float fl = fz_large((float)ps2_mod32(r, 100), 50.0f, 25.0f) - 0.5f;
        h += ps2_ftoi(2.0f * (float)tg->hate_table[8] * fl);
        if (a) {
            float q = (float)ps2_div32(a->hp, a->param.hp);
            float t6 = (float)tg->hate_table[6] * fz_large(q, 75.0f, 15.0f);
            float t7 = (float)tg->hate_table[7] * fz_small(q, 25.0f, 15.0f);
            h += ps2_ftoi(t6 + t7);
        }
        if (hl->hate_max < h) h = hl->hate_max;
        else if (h < hl->hate_min) h = hl->hate_min;
        if (best < h) {
            best = h;
            bestIdx = idx;
            found = 1;
        }
    }
    if (!found) return 0;
    hl = tg->hl_first;
    for (int i = 0; i < bestIdx; i++) hl = hl->hl_next;
    tg->select_id = (unsigned char)bestIdx;
    if (tg->select_id != oldsel) comp->trgmng.select_time = 0;   /* through comp, not tg (the same object) */
    return hl ? hl->target : 0;
}

/* Unlinks the model's node (never freed). (sic) Removing the head does not update hl_last. */
int Com_Tg_DelHl(stComTgMng *tgmngp, StModel *mdlp)
{
    stComTgMng *tg = tgmngp;
    if (!tg->nof_hl) return -1;
    stComTgHl *hl = tg->hl_first;
    if (!hl) return -1;
    stComTgHl *prev = 0;
    int n = tg->nof_hl;
    for (int i = 0; i < n && hl; i++) {
        if (hl->target == mdlp) {
            if (!prev) {
                tg->hl_first = hl->hl_next;
            } else if (hl == tg->hl_last) {
                prev->hl_next = 0;
                tg->hl_last = prev;
            } else {
                prev->hl_next = hl->hl_next;
            }
            tg->nof_hl--;
            return 1;
        }
        prev = hl;
        hl = hl->hl_next;
    }
    return -1;
}

/* Puts every AC and enemy that is not on the COM's side (no shared team bit) and not the COM itself on its hate list.
 * FFA ACs beyond the team bits have cflag & 0xf == 0, so they target everyone; the id check excludes the COM. */
int Com_Tg_AddHlAll(stCom *pCom)
{
    PS2_FRAME(0x50);
    unsigned short team = (com_acp ? com_acp->mp->cflag : com_enp->mp->cflag) & 0xf;
    for (int i = 0; i < NowPlayerNum; i++) {
        AcPlayerSt *a = &g_ac[i];
        if ((!com_acp || i != com_acp->id) && !(team & a->mp->cflag)) Com_Tg_AddHl(pCom, a->mp, 0x40, 0, 0x80);
    }
    for (unsigned int u = 0; (int)u < EnemyNum; u++) {
        if ((!com_enp || u != com_enp->id) && !(team & Enemy[u].mp->cflag))
            Com_Tg_AddHl(pCom, Enemy[u].mp, 0x40, 0, 0x80);
    }
    return 1;
}

/* Appends a hate list node (values * 100). Non-AC targets (types 1, 2, 8) take the opposite team of the COM and are
 * registered as lock-on targets. (sic) hate_max is capped by its raw value (< 0x3200), then scaled. */
int Com_Tg_AddHl(stCom *pCom, StModel *mdlp, int hate_ini, int hate_min, int hate_max)
{
    PS2_FRAME(0x80);
    stComTgMng *tg = &pCom->trgmng;
    if (!mdlp) return -1;
    if (tg->nof_hl >= 0x40) return -1;
    stComTgHl *n = (stComTgHl *)Memm_Alloc(1, 0x10, 0x10);
    if (!n) return -1;
    if ((unsigned int)(mdlp->type - 1) < 2 || mdlp->type == 8) {
        unsigned short c = pCom->mp->cflag;
        if (c == 2) mdlp->cflag = 1;
        else if (c == 1) mdlp->cflag = 2;
        DF_SetTargetObjectModel(mdlp);
    }
    n->target = mdlp;
    n->hl_next = 0;
    n->hate_min = (unsigned short)(hate_min > 0 ? hate_min * 100 : 0);
    n->hate_max = (unsigned short)(hate_max < 0x3200 ? hate_max * 100 : 0x3200);
    n->hate_ini = (unsigned short)(hate_ini * 100);
    n->hate = (unsigned short)(hate_ini * 100);
    if (tg->hl_last) tg->hl_last->hl_next = n;
    tg->hl_last = n;
    if (!tg->hl_first) tg->hl_first = n;
    tg->nof_hl++;
    return 1;
}

int Com_Init_Tgmanage(stComTgMng *tgmngp, stComTgData *tgdatap)
{
    PS2_FRAME(0x20);
    stComTgMng *tg = tgmngp;
    tg->hl_first = 0;
    tg->hl_last = 0;
    tg->select_time = 0;
    tg->select_min = tgdatap->select_min;
    tg->select_max = tgdatap->select_max;
    tg->nof_hl = 0;
    tg->select_id = 0;
    memcpy(tg->hate_table, tgdatap, 0x80);
    if (tg->hate_table[8] == 0) tg->hate_table[8] = 10;
    tg->init_hatelist = (int (*)(stComTgMng *, void *))GFN(_tg_general_init);
    tg->proc_hatelist = (int (*)(stComTgMng *, void *))GFN(_tg_general_proc);
    tg->poht_hatelist = (int (*)(stComTgMng *, void *))GFN(_tg_general_hit);
    return 1;
}

/* Applies the frame's damage events to every COM's hate list (several rules can fire per event), then clears them. */
void Com_Dmg_Proc(void)
{
    PS2_FRAME(0x10);
    stCom *c = (stCom *)AC3_Com_GetComInfo();
    if (s_ComtgEvMng.onuse == 0 || !s_ComtgEvMng.event) return;
    for (int k = 0; k < ComNum; k++, c++) {
        StModel *mp = c->type == 0 ? c->body.com_acp->mp : c->body.com_enp->mp;
        if (!mp) continue;
        if (!(c->trgmng.nof_hl > 0)) continue;
        stComTgHl *hl = c->trgmng.hl_first;
        for (int j = 0; j < c->trgmng.nof_hl; j++, hl = hl->hl_next) {
            StModel *t = hl->target;
            unsigned short u = t->use;
            if (u & 4) continue;
            if (t->type == 4 && ((AcPlayerSt *)t->arg0p)->comthinkoff == 1) continue;
            if (t->type != 4) {
                if (!t->on_layer && !(u & 0x200)) continue;
                if (u & 1) continue;
                if (u & 2) continue;
            }
            struct_e2063795 *ev = s_ComtgEvMng.event;
            if (!ev) continue;
            for (int e = 0; e < s_ComtgEvMng.onuse; e++, ev++) {
                if (!ev->smp || !ev->tmp) continue;
                if (ev->smp == mp && ev->tmp == hl->target) hl->hate = hl->hate + c->trgmng.hate_table[12];
                if (ev->smp == hl->target && ev->tmp == mp) hl->hate = hl->hate + c->trgmng.hate_table[10];
                if ((ev->smp->cflag & 7) == (mp->cflag & 7) && ev->tmp == hl->target && ev->smp != mp)
                    hl->hate = hl->hate + c->trgmng.hate_table[13];
                if ((ev->tmp->cflag & 7) == (mp->cflag & 7) && ev->smp == hl->target && ev->tmp != mp)
                    hl->hate = hl->hate + c->trgmng.hate_table[11];
            }
        }
    }
    s_ComtgEvMng.onuse = 0;
    memset(s_ComtgEvMng.event, 0, 0x400);
}
