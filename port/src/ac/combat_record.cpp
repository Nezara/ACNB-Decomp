/* src/ac/combat_record.cpp - per-sortie statistics for the training / ranking records. s_CombatRecord collects the
 * player's (AC 0) combat time, move / fly distances, kills and up to 10 weapon records (shots, hits, damage);
 * s_TrainingTrend is the training result record (move-mode frame counts, per-weapon shot counts and lock distances,
 * damage / overheat / charge counters). Nothing here affects gameplay.
 * At the end of the sortie CopyCombatRecordToTrainingTrend folds the combat record into the trend (percentages);
 * the menu side keeps 150 trends / training records per save (g_gamedata[0]) indexed by category / session / level. */
#include "acnb/ps2fp.h"

/* the recording gate of OnKill / OnHit / OnShoot / OnFrameMove: quick-match style sortie modes 0 / 2 / 3, the
 * player's AC (id 0), not paused, game running forward, not a replay (signed byte flags) */
static inline int combat_record_gate(const AcPlayerSt *acp)
{
    unsigned char mode = g_sortie.pconstant->modeSortie;
    if (mode != 0 && mode != 2 && mode != 3) return 0;
    if (!acp || acp->id != 0) return 0;
    if (MyI.pause != 0 || MyI.forward == 0 || MyI.replaymode != 0) return 0;
    return 1;
}

/* the weapon record of `wid`: an exact match wins, otherwise the first free slot (filled in from the part data);
 * NULL when the table is full or the weapon is not on the AC */
static StWeaponRecord *combat_record_find(AcPlayerSt *acp, unsigned short wid)
{
    StWeaponRecord *rec = NULL;
    for (int i = 0; i < 10; i++) {
        StWeaponRecord *r = &s_CombatRecord.weaponRecord[i];
        if (r->weapon_id == wid) { rec = r; break; }
        if (!rec && r->weapon_id == 0) rec = r;
    }
    if (!rec) return NULL;
    if (rec->weapon_id == 0) {
        PartsParamWeaponSt *w = __CombatRecord_GetWeaponData(acp, wid);
        if (!w) { rec->weapon_id = 0; return NULL; }
        rec->weapon_id = wid;
        rec->category_id = w->category_id;
        rec->isSolid = (w->dstat.effect & 0x10) != 0;
    }
    return rec;
}

/* shots fired so far with weapon `w` (ammo used) */
static inline unsigned short combat_record_used(const struct _weapon_st *w)
{
    return (unsigned short)(w->blt_initcnt - w->blt_cnt);
}

/* training-trend shot counter of a hand-gun slot: shot count for category 1, ammo used otherwise */
static inline void trend_count_shot(_training_trend_weapon_st *t, const struct _weapon_st *w)
{
    if (t->cate_id == 1) t->numShoot = (unsigned short)(t->numShoot + 1);
    else t->numShoot = combat_record_used(w);
}

/* lock distance / time of the weapon in training-trend slot `t` when it is weapon `id` */
static inline void trend_add_dist(_training_trend_weapon_st *t, int id, float dist)
{
    if (id != t->weapon_id) return;
    t->total_dist = t->total_dist + dist;
    t->total_time = t->total_time + 1.0f;
}

static void TrainingTrendWeaponDist(struct AcPlayerSt *acp)
{
    if (MyI.padoff == 1) return;
    StWepLock *p = acp->lockmng.lock;
    StModel *a = NULL;   /* last sorder 0 target with prog 0 / 2 */
    StModel *b = NULL;   /* last sorder 1 target */
    for (int i = 0; i < 13; i++, p++) {
        StModel *ofp = p->ofp;
        if (!ofp || !(p->lock_stat & 0xe)) continue;
        int kind;
        if (p->sorder == 1) {
            b = ofp;
            kind = (ofp == a) ? 5 : 3;
        } else if (p->sorder == 0) {
            kind = p->prog;
            if (kind == 0 || kind == 2) {
                a = ofp;
                if (ofp == b) kind = 4;
            }
        } else {
            kind = p->prog;
        }
        _training_trend_weapon_st *T = s_TrainingTrend.weapon;
        if (kind == 3 || kind == 5) {
            /* swordp is not null-checked (a blade lock implies a blade) */
            PartsParamSwordSt *sw = acp->param.swordp;
            unsigned short id = sw ? sw->weapon_id : *(const unsigned short *)((const char *)PS2_NULL_MEM() + 0x104);
            trend_add_dist(&T[1], id, p->dist);
            sw = acp->param.swordp;
            id = sw ? sw->weapon_id : *(const unsigned short *)((const char *)PS2_NULL_MEM() + 0x104);
            trend_add_dist(&T[3], id, p->dist);
        } else {
            switch (acp->equip_weapon) {
            case 3: trend_add_dist(&T[6], acp->param.weapons[3].id, p->dist); break;
            case 2: trend_add_dist(&T[4], acp->param.weapons[2].id, p->dist); break;
            case 1: trend_add_dist(&T[5], acp->param.weapons[1].id, p->dist); break;
            case 0:
                trend_add_dist(&T[0], acp->param.weapons[0].id, p->dist);
                trend_add_dist(&T[2], acp->param.weapons[0].id, p->dist);
                break;
            }
        }
    }
}

void SetTrainingTrendInitAP(unsigned int ap) { s_TrainingTrend.initAP = ap; }

void TrainingTrendIncreaseDamage(void)
{
    if (!MyI.padoff) s_TrainingTrend.damageNum++;
}

void TrainingTrendIncreaseOverheat(void)
{
    if (!MyI.padoff) s_TrainingTrend.overheatTime++;
}

void TrainingTrendIncreaseCharge(void)
{
    if (!MyI.padoff) s_TrainingTrend.chargeNum++;
}

void SetTrainingTrendWeapon(struct AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    AcPlayerParamSt *prm = &acp->param;
    _training_trend_weapon_st *T = s_TrainingTrend.weapon;
    AcPtsResSt *res = AcCtrl_GetPartsRes(acp);
    PartsParamWeaponSt *w = AcPtsRes_GetWeaponPtr(res, 2, 0);
    if (!w) w = AcPtsRes_GetWeaponPtr(res, 2, 1);
    for (int i = 0; i < 9; i++) T[i].cate_id = 0xff;
    PartsParamWeaponSt *g;
    if (!w) {
        if (prm->gunp) {
            if ((g = AcPtsRes_GetWeaponPtr(res, 5, 0))) T[0].cate_id = g->category_id;
            T[0].id = prm->pasmbly->idParts[12];
            T[0].weapon_id = prm->gunp->weapon_id;
        }
        if (prm->swordp) {
            if ((g = AcPtsRes_GetWeaponPtr(res, 6, 0))) T[1].cate_id = g->category_id;
            T[1].id = prm->pasmbly->idParts[13];
            T[1].weapon_id = prm->swordp->weapon_id;
        }
        if (prm->spgunp) {
            if ((g = AcPtsRes_GetWeaponPtr(res, 0xb, 0))) T[2].cate_id = g->category_id;
            T[2].id = prm->pasmbly->idParts[14];
            T[2].weapon_id = prm->spgunp->weapon_id;
        }
        if (prm->spswordp) {
            if ((g = AcPtsRes_GetWeaponPtr(res, 0xc, 0))) T[3].cate_id = g->category_id;
            T[3].id = prm->pasmbly->idParts[15];
            T[3].weapon_id = prm->spswordp->weapon_id;
        }
    } else {
        /* arm weapon: one slot (7) for both arm weapons */
        T[7].cate_id = w->category_id;
        T[7].id = prm->pasmbly->idParts[2];
        T[7].weapon_id = prm->armsp->weapon_id;
        s_TrainingTrend.weapon2_id = prm->armsp->weapon2_id;
    }
    if (prm->Rbackp) {
        if ((g = AcPtsRes_GetWeaponPtr(res, 7, 0))) T[4].cate_id = g->category_id;
        T[4].id = prm->pasmbly->idParts[10];
        T[4].weapon_id = prm->Rbackp->weapon_id;
    }
    if (prm->Lbackp) {
        if ((g = AcPtsRes_GetWeaponPtr(res, 8, 0))) T[5].cate_id = g->category_id;
        T[5].id = prm->pasmbly->idParts[11];
        T[5].weapon_id = prm->Lbackp->weapon_id;
    }
    if (prm->insidep) {
        if ((g = AcPtsRes_GetWeaponPtr(res, 0xa, 0))) T[6].cate_id = g->category_id;
        T[6].id = prm->pasmbly->idParts[8];
        T[6].weapon_id = prm->insidep->weapon_id;
    }
    if (prm->extp) {
        g = AcPtsRes_GetWeaponPtr(res, 9, 0);
        T[8].cate_id = g ? g->category_id : 0xff;
        T[8].id = prm->pasmbly->idParts[9];
        T[8].weapon_id = prm->extp->weapon_id;
    }
}

void SetTrainingTrendExt(int num)
{
    s_TrainingTrend.weapon[8].numShoot = (unsigned short)(s_TrainingTrend.weapon[8].numShoot + (unsigned short)num);
    if (num != 0) s_TrainingTrend.weapon[0].numShoot = combat_record_used(&g_ac[0].param.weapons[0]);
}

struct _training_trend_st *GetTrainingTrend(void) { return &s_TrainingTrend; }
int GetBladeKill(void) { return dummyBladeKill; }
void SetBladeKillIncrease(void) { dummyBladeKill++; }

/* the PartsParamWeaponSt of weapon `wid` on the AC's parts, searched in a fixed part order */
static struct PartsParamWeaponSt *__CombatRecord_GetWeaponData(struct AcPlayerSt *acp, int wid)
{
    PS2_FRAME(0xc0);
    AcPlayerParamSt *prm = &acp->param;
    PartsParamCoreSt *core = prm->corep;
    PartsParamArmsSt *arms = prm->armsp;
    PartsParamInsideSt *inside = prm->insidep;
    PartsParamExtensionSt *ext = prm->extp;
    PartsParamBackSt *lback = prm->Lbackp;
    PartsParamBackSt *rback = prm->Rbackp;
    PartsParamSwordSt *sword = prm->swordp;
    PartsParamGunSt *gun = prm->gunp;
    PartsParamSwordSt *spsword = prm->spswordp;
    PartsParamGunSt *spgun = prm->spgunp;
    AcPtsResSt *res = AcCtrl_GetPartsRes(acp);
    if (core && core->weapon_id == wid) return AcPtsRes_GetWeaponPtr(res, 0, 0);
    if (arms) {
        if (arms->weapon_id == wid) return AcPtsRes_GetWeaponPtr(res, 2, 0);
        if (arms->weapon2_id == wid) return AcPtsRes_GetWeaponPtr(res, 2, 1);
    }
    if (inside && inside->weapon_id == wid) return AcPtsRes_GetWeaponPtr(res, 0xa, 0);
    if (ext && ext->weapon_id == wid) return AcPtsRes_GetWeaponPtr(res, 9, 0);
    if (lback && lback->weapon_id == wid) return AcPtsRes_GetWeaponPtr(res, 8, 0);
    if (rback && rback->weapon_id == wid) return AcPtsRes_GetWeaponPtr(res, 7, 0);
    if (sword && sword->weapon_id == wid) return AcPtsRes_GetWeaponPtr(res, 6, 0);
    if (gun && gun->weapon_id == wid) return AcPtsRes_GetWeaponPtr(res, 5, 0);
    if (spsword && spsword->weapon_id == wid) return AcPtsRes_GetWeaponPtr(res, 0xc, 0);
    if (spgun && spgun->weapon_id == wid) return AcPtsRes_GetWeaponPtr(res, 0xb, 0);
    return NULL;
}

struct StCombatRecord *CombatRecord_GetRecord(void) { return &s_CombatRecord; }

void CombatRecord_Save(void) {}

void CombatRecord_OnKill(struct AcPlayerSt *acp, int kind)
{
    if (!combat_record_gate(acp)) return;
    if (kind >= 0 && kind < 7) s_CombatRecord.killCnt[kind]++;
}

void CombatRecord_OnHit(struct AcPlayerSt *acp, unsigned short wid, struct SfxDstat *dstat)
{
    PS2_FRAME(0x40);
    if (!combat_record_gate(acp)) return;
    if (wid == 0) return;
    StWeaponRecord *rec = combat_record_find(acp, wid);
    if (!rec) return;
    rec->numHit++;
    rec->damage += dstat->damage;
}

void CombatRecord_OnShoot(struct AcPlayerSt *acp, unsigned short wid)
{
    PS2_FRAME(0x50);
    if (!combat_record_gate(acp)) return;
    if (wid == 0) return;
    StWeaponRecord *rec = combat_record_find(acp, wid);
    if (!rec) return;
    rec->numShoot++;
    AcPlayerParamSt *prm = &acp->param;
    _training_trend_weapon_st *T = s_TrainingTrend.weapon;
    if (wid == prm->weapons[0].id) {
        if (T[0].weapon_id == wid) trend_count_shot(&T[0], &prm->weapons[0]);
        if (T[2].weapon_id == wid) trend_count_shot(&T[2], &prm->weapons[0]);
        if (T[7].weapon_id == wid || s_TrainingTrend.weapon2_id == wid) trend_count_shot(&T[7], &prm->weapons[0]);
    } else if (wid == prm->weapons[1].id) {
        T[5].numShoot = combat_record_used(&prm->weapons[1]);
    } else if (wid == prm->weapons[2].id) {
        T[4].numShoot = combat_record_used(&prm->weapons[2]);
    } else if (wid == prm->weapons[3].id) {
        T[6].numShoot = combat_record_used(&prm->weapons[3]);
    } else if (wid == prm->extweapon.id) {
        T[8].numShoot = combat_record_used(&prm->extweapon);
    } else {
        /* left hand: a laser-blade-gun arm (Larm_type 3) counts its own ammo, a blade counts swings */
        unsigned char lt = prm->Larm_type;
        int n = 0;
        if (lt != 4 && lt == 3) {
            _ac_weaponctrl *c = AcLgun_GetWeaponCtrl(AcCtrl_GetPartsLgun(acp));
            if (c && c->wp) n = (short)c->wp->blt_initcnt - (short)c->wp->blt_cnt;
        }
        if (T[1].weapon_id == wid) T[1].numShoot = (lt == 3) ? (unsigned short)n : (unsigned short)(T[1].numShoot + 1);
        if (T[3].weapon_id == wid) T[3].numShoot = (lt == 3) ? (unsigned short)n : (unsigned short)(T[3].numShoot + 1);
    }
}

void CombatRecord_OnFrameMove(struct AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    PS2_STACK(float[4], dv, 0x20);   /* own move this frame */
    PS2_STACK(float[4], dv1, 0x10);  /* vs AC 1 */
    if (!combat_record_gate(acp)) return;
    if (acp->stat & 0x2000) return;
    s_CombatRecord.combatTime++;
    /* vsub.xyz: w keeps loc.w */
    dv[0] = acp->loc[0] - acp->oloc[0];
    dv[1] = acp->loc[1] - acp->oloc[1];
    dv[2] = acp->loc[2] - acp->oloc[2];
    dv[3] = acp->loc[3];
    float len = ps2_sqrt(dv[2] * dv[2] + (dv[0] * dv[0] + dv[1] * dv[1]));
    s_CombatRecord.moveDistance = s_CombatRecord.moveDistance + len;
    if (acp->actstat & 1) {
        s_CombatRecord.flyTime++;
        s_CombatRecord.flyDistance = s_CombatRecord.flyDistance + len;
    }
    /* distance to AC 1 (read even when there is no AC 1) */
    dv1[0] = acp->loc[0] - g_ac[1].loc[0];
    dv1[1] = acp->loc[1] - g_ac[1].loc[1];
    dv1[2] = acp->loc[2] - g_ac[1].loc[2];
    dv1[3] = acp->loc[3];
    s_CombatRecord.standDistance =
        s_CombatRecord.standDistance + ps2_sqrt(dv1[2] * dv1[2] + (dv1[0] * dv1[0] + dv1[1] * dv1[1]));
    int m = -1;
    switch (acp->actno) {
    case 0: case 1: case 2: case 4: case 10: case 12: case 16: case 17: case 19: case 21: m = 0; break;
    case 3: m = 1; break;
    case 5: case 6: case 11: case 14: m = 2; break;
    case 7: case 8: case 9: case 13: case 15: case 20: m = 3; break;
    case 18: m = 4; break;
    }
    if (m >= 0 && MyI.padoff == 0) s_TrainingTrend.moveRecord[m]++;
    TrainingTrendWeaponDist(&g_ac[0]);
}

void CombatRecord_OnWin(int bWin)
{
    if (MyI.replaymode) return;
    if (bWin) s_CombatRecord.numWin++;
    else s_CombatRecord.numLose++;
}

void CombatRecord_OnSortie(void)
{
    if (!MyI.replaymode) s_CombatRecord.numCombat++;
}

void CombatRecord_Init(void)
{
    PS2_FRAME(0x10);
    ZeroMemory(&s_CombatRecord, 0xe0);
    ZeroMemory(&s_TrainingTrend, 0x110);
    dummyBladeKill = 0;
}

/* ---- end of sortie / menu side ---- */

void CopyCombatRecordToTrainingTrend(void)
{
    PS2_FRAME(0x30);
    _training_trend_st *T = &s_TrainingTrend;
    T->combatTime = s_CombatRecord.combatTime;
    T->flyTime = s_CombatRecord.flyTime;
    T->moveDistance = s_CombatRecord.moveDistance;
    T->flyDistance = s_CombatRecord.flyDistance;
    T->standDistance = s_CombatRecord.standDistance;
    T->landTime = s_CombatRecord.combatTime - T->flyTime;
    if (T->combatTime == 0) {
        T->lockonPer = 0;
        T->averageSpeed = 0.0f;
    } else {
        unsigned int lt = GetLockTime();
        unsigned int ct = T->combatTime;
        T->lockonPer = (lt * 100) / ct;
        /* u32 -> float: MWCC's halve / convert / double sequence rounds exactly like (float) */
        T->averageSpeed = (216000.0f * T->moveDistance) / (1000.0f * (float)ct);
    }
    T->restAP = g_ac[0].hp / 4;
    float sum = 0.0f;
    for (int i = 0; i < 5; i++) sum = sum + (float)T->moveRecord[i];
    if (!(sum <= 0.0f)) {
        for (int i = 0; i < 5; i++)
            T->movePer[i] = (unsigned int)ps2_ftoi((float)(T->moveRecord[i] * 100) / sum);
    }
    AcPlayerParamSt *prm = &g_ac[0].param;
    if (prm->Rbackp && prm->Lbackp && (prm->Rbackp->w_use == 1 || prm->Lbackp->w_use == 1)) {
        T->weapon[5].cate_id = 0xff;
        unsigned short n4 = T->weapon[4].numShoot, n5 = T->weapon[5].numShoot;
        T->weapon[5].numShoot = 0;
        T->weapon[4].numShoot = (unsigned short)(n4 + n5);
    }
    if (prm->extp && prm->extp->prog_no == 0x10) {
        unsigned short n0 = T->weapon[0].numShoot, n8 = T->weapon[8].numShoot;
        T->weapon[8].numShoot = 0;
        T->weapon[0].numShoot = (unsigned short)(n0 + n8);
    }
    float tot = 0.0f;
    for (int k = 0; k < 9; k++) tot = tot + (float)T->weapon[k].numShoot;
    if (!(tot <= 0.0f)) {
        for (int k = 0; k < 9; k++)
            T->weapon[k].usePer = (unsigned char)ps2_ftoi(100.0f * (float)T->weapon[k].numShoot / tot + 0.5f);
    }
}

void CopyTrainingTrend(int Cate, int Session, int Lv)
{
    PS2_FRAME(0x50);
    _training_trend_st *p = &dummyTrainingTrend[Lv + (Session + Cate * 5) * 5];
    unsigned int c = p->numCombat, w = p->numWin, l = p->numLose;
    memcpy(p, &s_TrainingTrend, 0x110);
    p->numCombat = c + 1;
    p->numWin = p->numWin + w;
    p->numLose = p->numLose + l;
}

_training_trend_st *GetTrainingTrendLv(int Cate, int Session, int Lv)
{
    return &dummyTrainingTrend[Lv + (Session + Cate * 5) * 5];
}

void SetTrainingTrendPrize(unsigned int prize) { s_TrainingTrend.prize = prize; }
void SetTrainingTrendPoint(unsigned int point) { s_TrainingTrend.point = point; }

void TrainingTrendInit(void)
{
    dummyTrainingTrend = g_gamedata[0].TrainingTrend;
}

int *GetTrainingRecordLv(int Cate, int Session, int Lv)
{
    return dummyCombatRecord + (Lv + (Session + Cate * 5) * 5) * 0x40;
}

/* one level row = 0x40 ints (0x100 bytes) */
void SetTrainingRecordLv(int Cate, int Session, int Lv, int *Value)
{
    int row = (Lv + (Session + Cate * 5) * 5) * 0x40;
    for (int i = 0; i < 0x40; i++) dummyCombatRecord[row + i] = Value[i];
}

void TrainingRecordReset(void)
{
    dummyCombatRecord = g_gamedata[0].TrainingRecord;
}
