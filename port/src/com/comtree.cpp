/* src/com/comtree.cpp - the COM behaviour-tree interpreter (whole file). Each COM's AI data ends in tree_buff: a 0x30-byte
 * header (offsets of the node array and of the operation / weapon / If / command record pools, their counts) followed by
 * the records. ComTree_AnalyzeComTree (once per COM, from ComInit) turns the node indices into pointers in place and
 * sets up the operation / weapon managers. ComTree_SelMainProc (every think step, from DF_ComInKey) walks the tree:
 * operation and weapon nodes are scored (the best one is kept in the s_* statics), If nodes test a condition and form
 * if / else-if / else chains, command nodes run a small variable VM. Then it switches the selected operation / weapon
 * and presses the weapon-change / purge keys.
 * If records (0x10, no DWARF): int dataL, int dataR, s8 cmp, s8 typeL, s8 typeR, s8 link (0 if, 1 else-if, 2 else).
 * Command records (0x10, no DWARF): int dst, int src, s8 op, s8 dstType (3 = purge flag), s8 srcType. */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/local/src/com/comtree.h"

/* Info_GetData returns a union through a hidden pointer: the result slot is the original's stack slot */
static int ifdata_info(int type, int id, int arg1, unsigned int slot)
{
    PS2_STACK(union_9a8d3bef, r, slot);
    gcall_sret(GFN(Info_GetData), &r, type, id, arg1, 0);
    return r.i;
}

/* the enemy model's AC, if the COM's target is an AC */
static inline AcPlayerSt *ifdata_ene_ac(void)
{
    StModel *em = comp->ene_mp;
    return (em && em->type == 4) ? (AcPlayerSt *)em->arg0p : 0;
}

/* (ps2_rand() % (n << 14)) >> 14 for n > 0, else 0 */
static inline int tree_rand(int n)
{
    if (n <= 0) return 0;
    return ps2_mod32(ps2_rand(), (int)((unsigned int)n << 14)) >> 14;
}

/* Operand of an If node: type 0 immediate, 1-3 distance / angles to the target, 4-0xa kill / damage counters
 * (Info_GetData), 0xb random, 0xd variable, 0xe purge state, 0xf lock-on, 0x10-0x16 energy / heat / leg type / state. */
static float __GetIfData(char type, int data)
{
    PS2_FRAME(0x40);
    StModel *em;
    switch ((unsigned int)(int)type) {
    case 0: return (float)data;
    case 1: return comp->elen;
    case 2: return 57.29578f * comp->ctoe_ang_x;
    case 3: return 57.29578f * comp->ctoe_ang_y;
    case 4:
        em = comp->ene_mp;
        if (!em) return 0.0f;
        if (em->type == 4) return (float)ifdata_info(1, 0, 0, 0x3c);
        if (em->type == 3) return (float)ifdata_info(2, 0, em->aid, 0x3c);
        return 0.0f;
    case 5:
        em = comp->ene_mp;
        if (!em) return 0.0f;
        if (em->type == 4) return (float)ifdata_info(1, 1, 0, 0x38);
        if (em->type == 3) return (float)ifdata_info(2, 1, em->aid, 0x38);
        return 0.0f;
    case 6: {
        em = comp->ene_mp;
        if (!em) return 0.0f;
        int a, b;
        if (em->type == 4) {
            a = ifdata_info(1, 1, 0, 0x30);
            b = ifdata_info(1, 0, 0, 0x34);
        } else if (em->type == 3) {
            a = ifdata_info(2, 1, em->aid, 0x30);
            b = ifdata_info(2, 0, comp->ene_mp->aid, 0x34);
        } else {
            return 0.0f;
        }
        return ps2_div(100.0f * (float)a, (float)b);
    }
    case 8: return (float)ifdata_info(1, 0, comp->comid + 1, 0x2c);
    case 9: return (float)ifdata_info(1, 1, comp->comid + 1, 0x28);
    case 0xa: {
        int a = ifdata_info(1, 1, comp->comid + 1, 0x20);
        int b = ifdata_info(1, 0, comp->comid + 1, 0x24);
        return ps2_div(100.0f * (float)a, (float)b);
    }
    case 0xb: return (float)tree_rand(data);
    case 0xd: return (float)comp->variable_short[data];
    case 0xe: return (comp->purged_state[data] & 1) ? 1.0f : 0.0f;
    case 0xf: return COMOP_IsTgtLockON(comp) ? 1.0f : 0.0f;
    case 0x10: return comp->energy_rate;
    case 0x11: {
        AcPlayerSt *a = ifdata_ene_ac();
        if (!a) return -1.0f;
        return ps2_div((float)a->energyReal, (float)a->param.boost_max);
    }
    case 0x12: return com_acp ? com_acp->temperature : 0.0f;
    case 0x13: {
        AcPlayerSt *a = ifdata_ene_ac();
        if (!a) return -1.0f;
        return a->temperature;
    }
    case 0x14: {
        AcPlayerSt *a = ifdata_ene_ac();
        if (!a) return -1.0f;
        unsigned char c = a->param.legsp->basic.category_id;
        if (c == 0xd || c == 9 || c == 5) return 2.0f;
        if (c == 7 || c == 1) return 1.0f;
        if (c == 0xf || c == 0xb || c == 3) return 0.0f;
        return 0.0f;
    }
    case 0x15: return (com_acp->stat & 4) ? 1.0f : 0.0f;   /* com_acp not NULL-checked (always an AC in Versus) */
    case 0x16: {
        AcPlayerSt *a = ifdata_ene_ac();
        if (!a) return -1.0f;
        return (a->stat & 4) ? 1.0f : 0.0f;
    }
    default: return 0.0f;   /* 7, 0xc, >= 0x17 and negative types */
    }
}

/* Command node: dst op= src on the COM's short variables, or set a purge flag. Returns 0. */
static char __ComTree_ValueCommand(char *pTreeBuff, tagNode *pNode, char data)
{
    PS2_FRAME(0x30);
    char *rec = (char *)(uintptr_t)pNode->data;
    if (rec[9] == 3) {
        comp->purged_state[*(int *)rec] |= 2;
        return 0;
    }
    int src = *(int *)(rec + 4);
    short v;
    switch (rec[0xa]) {
    case 3: comp->purged_state[src] |= 2; v = 0; break;
    case 2: v = (short)tree_rand(src); break;
    case 1: v = (short)src; break;
    case 0: v = comp->variable_short[src]; break;
    default: v = 0; break;
    }
    short *vs;
    switch (rec[8]) {
    case 4: vs = &comp->variable_short[*(int *)rec]; *vs = (short)ps2_div32(*vs, v); break;
    case 3: vs = &comp->variable_short[*(int *)rec]; *vs = (short)(*vs * v); break;
    case 2: vs = &comp->variable_short[*(int *)rec]; *vs = (short)(*vs - v); break;
    case 1: vs = &comp->variable_short[*(int *)rec]; *vs = (short)(*vs + v); break;
    case 0: comp->variable_short[*(int *)rec] = v; break;
    default: break;
    }
    return 0;
}

/* If node. data = the previous sibling's result in this chain: an else-if / else (link != 0) after a taken branch
 * returns that result unchanged (2 once the tree walker descended), an else returns 1. The negated compares keep the
 * original's NaN behaviour (cmp 0, 2, 4 are true for NaN). */
static char __ComTree_ValueIf(char *pTreeBuff, tagNode *pNode, char data)
{
    PS2_FRAME(0x30);
    char *rec = (char *)(uintptr_t)pNode->data;
    char link = rec[0xb];
    if (link != 0 && data != 0) return data;
    if (link == 2) return 1;
    float l = __GetIfData(rec[9], *(int *)rec);
    float r = __GetIfData(rec[0xa], *(int *)(rec + 4));
    switch ((unsigned int)(int)rec[8]) {
    case 0: return !(l <= r);
    case 1: return l < r;
    case 2: return !(l < r);
    case 3: return l <= r;
    case 4: return !(l == r);
    case 5: return l == r;
    default: return 0;
    }
}

/* The weapon slot can fire now: not an empty slot, ammo left, and a charge weapon (effect 0x20) not while the AC
 * charges. (The NULL tests of the original's inline copy are dead.) */
static int tree_wep_usable(_weapon_st *wp)
{
    if (wp->statid == 5) return 0;
    if (wp->blt_cnt == 0) return 0;
    if (!(wp->dstat.effect & 0x20)) return 1;
    return AcCtrl_IsCharging(com_acp) ? 0 : 1;
}

/* Weapon node: scores the weapon manager idData; the best score so far goes to s_valueMaxWp / s_idSelWp / s_idEqeWp /
 * s_typeArm. An incoming missile with an anti-missile weapon ready forces that weapon (score untouched). */
static char __ComTree_ValueWeapon(char *pTreeBuff, tagNode *pNode, char data)
{
    PS2_FRAME(0x60);
    if (!comp->ene_mp) return 0;
    stComWepMng *wm = &comp->wepmng[pNode->idData];
    char sel = 0;
    if (!wm || !wm->datap) return 0;
    float f = (float)wm->datap->value_init / 100.0f;
    if (com_acp) {
        sel = wm->id;
        _weapon_st *wp = &com_acp->param.weapons[(int)sel];
        /* an empty slot with weapon id 0 is still scored */
        if (!tree_wep_usable(wp) && !(wp->statid == 5 && wp->id == 0)) return 0;
        if ((wp->dstat.effect & 0x20) && comp->energy_rate < (float)comp->datap->alert_energy / 100.0f &&
            !(wp->statid == 5 && wp->id == 0))
            return 0;
        if (wm->val) f = f * gcall(wm->val, wm, wp, (void *)&wm->wk);
        float h = ((float)comp->datap->alert_bullet / 100.0f) / 2.0f;
        f = f * fz_large(ps2_div((float)wp->blt_cnt, (float)wp->blt_initcnt), h, h);
        if (comp->nearest_misp && wp->statid == 3 && ((unsigned int)(wp->id - 0x67) < 2 || wp->id == 0x18f)) {
            if (comp->datap->skl_data[2].rate == 0 || ps2_mod32(ps2_rand(), 100) < comp->datap->skl_data[2].rate) {
                s_idSelWp = sel;
                s_idEqeWp = (char)pNode->idData;
                s_typeArm = 0;
                return 0;
            }
        }
    } else {
        /* an enemy COM (missions only) */
        char bst = com_enp->bst;
        if (bst == 0x17 || bst == 0x14 || bst == 0x19) return 0;
        if (com_enp->wep_valid & (1 << (pNode->idData + 2))) return 0;
        if (!ene_get_prob(com_enp, pNode->idData)) return 0;
        if (com_enp->am_time) {
            s_idSelWp = 0;
            s_idEqeWp = (char)pNode->idData;
            s_typeArm = 0;
            return 0;
        }
    }
    stComWepData *wd;
    if (comp->wepmng_equip == (int)pNode->idData) {
        wd = wm->datap;
        float d = ((float)wd->time_max - (float)wd->time_min) / 2.0f;
        f = f * fz_small((float)wm->time_sel, (float)wd->time_min + d, d);
        if (com_acp) {
            unsigned short n = com_acp->lockmng.lock_cnt;
            if (n >= 2) f = f * (float)(n >> 1);
        } else if (com_enp->lktime >= 0x3d) {
            f = f * ((float)com_enp->lktime / 60.0f);
        }
    }
    wd = wm->datap;
    float vmax = (float)wd->value_max / 100.0f;
    if (!(f <= vmax)) f = vmax;
    float vmin = (float)wd->value_min / 100.0f;
    if (f < vmin) f = vmin;
    if (!(f <= s_valueMaxWp)) {
        s_valueMaxWp = f;
        s_idSelWp = sel;
        s_idEqeWp = (char)pNode->idData;
        s_typeArm = wm->arm_type;
    }
    return 0;
}

/* Operation node: scores the operation manager idData (a negative score forces it); the best one goes to
 * s_valueMaxOp / s_idSelOp / s_pSelOpMng. The running operation loses score with time and when the COM is stuck. */
static char __ComTree_ValueOperation(char *pTreeBuff, tagNode *pNode, char data)
{
    PS2_FRAME(0x50);
    stComOpMng *om = &comp->opmng[pNode->idData];
    if (!om->datap->f_enable_void && !comp->ene_mp) return 0;
    float f = (float)om->value / 100.0f;
    if (comp->op_sel == (int)pNode->idData) {
        if (om->val_sel) f = f * gcall(om->val_sel, om, (void *)&om->wk);
    } else {
        if (om->val_non) f = f * gcall(om->val_non, om, (void *)&om->wk);
    }
    if (f < 0.0f) {
        s_valueMaxOp = f;
        s_idSelOp = (char)pNode->idData;
        s_pSelOpMng = om;
        s_flagForceOp = 1;
        return 0;
    }
    f = f + COMOP_CalcValue(&om->datap->value_calc[0]);
    if (comp->op_sel == (int)pNode->idData) {
        f = f * fz_small((float)om->time_sel, (float)om->datap->time_max,
                         ((float)om->datap->time_max - (float)om->datap->time_min) / 2.0f);
        if (om->mvchk) {
            /* vsub.xyz into the full vf16 store: w is loc's */
            PS2_STACK(float[4], v, 0x40);
            v[0] = comp->loc[0] - comp->base_move_loc[0];
            v[1] = comp->loc[1] - comp->base_move_loc[1];
            v[2] = comp->loc[2] - comp->base_move_loc[2];
            v[3] = comp->loc[3];
            if (gcall(om->mvchk, om, (float *)v) == 0) {
                unsigned short n = ++comp->cannot_move_time;
                if (n >= 0x1f) f = f * fz_small((float)comp->cannot_move_time, 60.0f, 30.0f);
            } else {
                comp->cannot_move_time = 0;
                memcpy(comp->base_move_loc, comp->loc, 16);
            }
        }
    }
    float vmax = (float)om->datap->value_max / 100.0f;
    if (!(f <= vmax)) f = (float)om->datap->value_max / 100.0f;
    float vmin = (float)om->datap->value_min / 100.0f;
    if (f < vmin) f = (float)om->datap->value_min / 100.0f;
    if (!(f <= s_valueMaxOp)) {
        s_valueMaxOp = f;
        s_idSelOp = (char)pNode->idData;
        s_pSelOpMng = om;
    }
    return 0;
}

static char __ComTree_ValueRoot(char *pTreeBuff, tagNode *pNode, char data)
{
    return 0;
}

/* the original's __ComTree_ValueTree__pValueFuncTable, indexed by typeNode (unchecked: tree data is trusted) */
static char (*const s_valueFunc[5])(char *, tagNode *, char) = {
    __ComTree_ValueRoot, __ComTree_ValueOperation, __ComTree_ValueWeapon, __ComTree_ValueIf, __ComTree_ValueCommand,
};

/* Walks one level of siblings and their children. p carries an If chain's state: the If result, then 2 once the
 * walker descended into a taken branch (so the following else-if / else siblings are skipped). While an operation is
 * forced, the remaining operation nodes are not scored. */
static void __ComTree_ValueTree(char *pTreeBuff, tagNode *pNode)
{
    PS2_FRAME(0x40);
    char p = 0;
    do {
        if (!(s_flagForceOp && pNode->typeNode == 1)) {
            if (pNode->typeNode == 3) p = s_valueFunc[3](pTreeBuff, pNode, p);
            else s_valueFunc[(int)pNode->typeNode](pTreeBuff, pNode, p);
        }
        if (pNode->flags.isParent) {
            if (pNode->typeNode == 3) {
                if (p == 1) {
                    p = 2;
                    __ComTree_ValueTree(pTreeBuff, pNode->pNodeChild);
                }
            } else {
                __ComTree_ValueTree(pTreeBuff, pNode->pNodeChild);
            }
        }
        pNode = pNode->pNodeNext;
    } while (pNode);
}

/* ps2_rand() % 100 < the COM's purge skill rate (skl_data[6]) */
static inline int tree_purge_roll(stCom *c)
{
    int r = ps2_rand();
    return ps2_mod32(r, 100) < c->datap->skl_data[6].rate;
}

void ComTree_SelMainProc(char *pTreeBuff)
{
    PS2_FRAME(0x30);
    s_valueMaxOp = -1.0f;
    s_valueMaxWp = -100.0f;
    s_idSelOp = -1;
    s_idSelWp = -1;
    s_idEqeWp = -1;
    s_pSelOpMng = 0;
    s_flagForceOp = 0;
    s_typeArm = 0;
    __ComTree_ValueTree(pTreeBuff, (tagNode *)(pTreeBuff + *(int *)(pTreeBuff + 0xc)));
    stCom *c = comp;
    if (c->op_sel == -1) return;
    stComOpMng *cur = c->opmng + c->op_sel;
    if (com_acp->actno == 0x10) g_NoMoveFlag = 1;   /* com_acp not NULL-checked (Versus COMs are ACs) */

    /* keep the running operation until its minimum time (unless forced or stuck) */
    if (!(s_valueMaxOp < 0.0f) && cur->time_sel < cur->datap->time_min && c->cannot_move_time < 0x1e) {
        s_idSelOp = c->op_sel;
        s_pSelOpMng = c->opmng + s_idSelOp;
    }
    if (s_idSelOp == -1) return;
    if (s_idSelOp == c->op_sel) {
        s_pSelOpMng->time_sel++;
    } else {
        /* (sic) the old operation's exit function gets the new operation's manager */
        auto ex = c->opmng[(int)c->op_sel].exit;
        if (ex) gcall(ex, s_pSelOpMng, (void *)&s_pSelOpMng->wk);
        if (s_pSelOpMng->sel_init &&
            gcall(s_pSelOpMng->sel_init, s_pSelOpMng, s_pSelOpMng->datap, (void *)&s_pSelOpMng->wk) == 0) {
            s_pSelOpMng = &comp->opmng[(int)comp->op_sel];
            s_pSelOpMng->time_sel++;
            return;
        }
        s_pSelOpMng->time_sel = 0;
        s_pSelOpMng->step = 0;
        s_pSelOpMng->f_shot_wep = 0;
        comp->op_sel = s_idSelOp;
        comp->cannot_move_time = 0;
    }

    if (s_idSelWp == -1 || s_idEqeWp == -1) {
        /* no weapon scored: now and then drop an empty weapon / launcher */
        stCom *s0 = comp;
        if (!(s0->pad_old & 0x1000)) {
            AcPlayerSt *acp = com_acp;
            /* (sic) the slot test uses wep_sel (may be -1), the rest the equipped weapon */
            if (acp && FRAME % 60 == 0 && acp->param.partsid[acp->param.weapons[(int)s0->wep_sel].jpos] != 0) {
                _weapon_st *w = &acp->param.weapons[acp->equip_weapon];
                if (w->statid != 6 && w->statid != 3 && w->blt_cnt == 0 && tree_purge_roll(s0)) {
                    s0->pad |= 0x1000;
                    comp->f_wep_purge = 1;
                }
            }
        }
        if (FRAME % 300 != 0) return;
        if (com_acp->param.Larm_type != 3) return;
        if (AcLgun_GetWeaponCtrl(AcCtrl_GetPartsLgun(com_acp))->wp->blt_cnt != 0) return;
        s0 = comp;
        if (tree_purge_roll(s0)) s0->f_lau_purge = 1;
        return;
    }

    /* the current weapon stays selected until its minimum time while it can fire */
    c = comp;
    if (c->wep_sel >= 0 && c->wepmng_equip >= 0 &&
        c->wepmng[(int)c->wepmng_equip].time_sel < c->wepmng[(int)c->wepmng_equip].datap->time_min) {
        int keep = 1;
        if (com_acp) {
            if (!tree_wep_usable(&com_acp->param.weapons[com_acp->equip_weapon])) keep = 0;
            else if (com_acp->param.weapons[com_acp->equip_weapon].statid == 5) keep = 0;
        }
        if (keep) {
            c = comp;
            s_idSelWp = c->wep_sel;
            s_idEqeWp = c->wepmng_equip;
            s_typeArm = c->wep_sel_arm;
        }
    }
    char old = comp->wepmng_equip;
    comp->wep_sel = s_idSelWp;
    comp->wepmng_equip = s_idEqeWp;
    comp->wep_sel_arm = s_typeArm;
    c = comp;
    char eq = c->wepmng_equip;
    if (eq < 0) return;
    stComWepMng *wm = c->wepmng + eq;
    AcPlayerSt *acp = com_acp;
    if (!acp) {
        if (c->wep_sel == com_enp->cur_wep) {
            wm->time_sel++;
        } else {
            if (!(c->pad_old & 0x1000)) c->pad |= 0x1000;
            wm->time_sel = 0;
        }
        return;
    }
    int ws = c->wep_sel;
    int change = 1;
    if (ws == acp->next_weapon && old == eq) {
        unsigned char st = acp->param.weapons[ws].statid;
        int ok = st != 6 ? 1 : c->wep_sel_arm == acp->aw_type;
        if (ok) {
            change = 0;
            if (ws == acp->equip_weapon) {
                int same = st != 6 ? 1 : com_acp->aw_type == com_acp->next_aw_type;
                if (same) wm->time_sel++;
            }
        }
    }
    if (change) {
        /* press weapon change; an empty equipped weapon may get purged */
        if (!(c->pad_old & 0x1000)) {
            c->pad |= 0x1000;
            _weapon_st *w = &com_acp->param.weapons[com_acp->equip_weapon];
            if (w->statid != 6 && w->statid != 3 && w->blt_cnt == 0) {
                stCom *s1 = comp;
                if (tree_purge_roll(s1)) s1->f_wep_purge = 1;
            }
        }
        wm->time_sel = 0;
    }
    if (FRAME % 300 != 0) return;
    if (com_acp->param.Larm_type == 3 && AcLgun_GetWeaponCtrl(AcCtrl_GetPartsLgun(com_acp))->wp->blt_cnt == 0) {
        stCom *s0 = comp;
        if (tree_purge_roll(s0)) s0->f_lau_purge = 1;
    }
    PartsParamExtensionSt *ext = com_acp->param.extp;
    if (!ext) return;
    unsigned char pn = ext->prog_no;
    if (!((unsigned int)(pn - 2) < 3 || pn == 0xc || pn == 0x10)) return;
    short shot = acext_GetShotCount(AcCtrl_GetPartsExt(com_acp));
    short eb = (short)com_acp->param.extweapon.blt_cnt;
    short cnt = (eb < shot) ? shot : eb;
    int r = ps2_rand();   /* drawn whatever the count */
    if (cnt != 0) return;
    if ((ps2_mod32(r, 0x190000) >> 14) < comp->datap->skl_data[6].rate) comp->f_ext_purge = 1;
}

/* Relocates the tree (node indices -> pointers, record indices -> record pointers), sets up the operation managers and
 * matches each weapon record to the AC's weapon slot by its kind. */
char ComTree_AnalyzeComTree(char *pTreeBuff, stCom *pCom, stComData *comdatp)
{
    PS2_FRAME(0x60);
    for (int i = 0; i < *(short *)(pTreeBuff + 0x20); i++) {
        int offNode = *(int *)(pTreeBuff + 0xc);
        tagNode *n = (tagNode *)(pTreeBuff + offNode + i * 16);
        if (n->pNodeNext) n->pNodeNext = (tagNode *)(pTreeBuff + offNode + (int)(uintptr_t)n->pNodeNext * 16);
        if (n->pNodeChild)
            n->pNodeChild = (tagNode *)(pTreeBuff + *(int *)(pTreeBuff + 0xc) + (int)(uintptr_t)n->pNodeChild * 16);
        switch (n->typeNode) {
        case 4: n->data = PS2PTR(pTreeBuff + *(int *)(pTreeBuff + 0x1c) + n->idData * 16); break;
        case 3: n->data = PS2PTR(pTreeBuff + *(int *)(pTreeBuff + 0x18) + n->idData * 16); break;
        case 2: n->data = PS2PTR(pTreeBuff + *(int *)(pTreeBuff + 0x14) + n->idData * 0x80); break;
        case 1: n->data = PS2PTR(pTreeBuff + *(int *)(pTreeBuff + 0x10) + n->idData * 0x80); break;
        default: break;
        }
    }
    unsigned char *numOp = (unsigned char *)pTreeBuff + 0x22;
    unsigned char *numWep = (unsigned char *)pTreeBuff + 0x23;
    pCom->opmng = (stComOpMng *)pCom->mng_buff;
    pCom->wepmng = (stComWepMng *)(pCom->mng_buff + *numOp * 0x80);
    for (int i = 0; i < *numOp; i++)
        COMOP_InitManage(pCom->opmng + i, (stOpData *)(pTreeBuff + *(int *)(pTreeBuff + 0x10) + i * 0x80));
    pCom->op_num = (char)*numOp;
    if (com_acp && *numWep) {
        for (int i = 0; i < *numWep; i++) {
            stComWepData *wd = (stComWepData *)(pTreeBuff + *(int *)(pTreeBuff + 0x14) + i * 0x80);
            unsigned char n = com_acp->param.n_weapon;
            char want = wd->wep_id;
            int arm = 0;
            int j = 0;
            if (n) {
                for (; j < com_acp->param.n_weapon; j++) {
                    unsigned char st = com_acp->param.weapons[j].statid;
                    if (st == 5 || st >= 7) continue;
                    int hit = 0;
                    switch (st) {
                    case 0: hit = want == 0; break;
                    case 1: case 4: hit = want == 2; break;
                    case 2: hit = want == 3; break;
                    case 3: hit = want == 4; break;
                    case 6:
                        if (want == 0 || want == 1) { arm = want; hit = 1; }
                        break;
                    }
                    if (hit) break;
                }
            }
            if (j != n && wd->progno != -1) {
                pCom->wepmng[i].id = (char)j;
                COM_InitWeapon(comp->wepmng + i, wd, &com_acp->param.weapons[j], arm);
            }
        }
    }
    pCom->wep_num = (char)*numWep;
    pCom->param.blt_reactdist = 300.0f;
    pCom->param.info_delay = comdatp->sense_delay;
    return 0;
}
