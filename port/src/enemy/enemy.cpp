/* src/enemy/enemy.cpp (OLSORTIE) - enemy (non-AC NPC) setup for a stage, Versus part. ENEMY_New allocates the
 * enemy array and the per-enemy joint matrices during stage memory setup; ENEMY_AllEnemyInit records the arrays,
 * loads one EnemyDataSt (file 0x578 + eid) and one EnemyLogicSt (file 0x6a4 + logic id) per distinct id, reads the
 * joint-control table (edata.cpp) and initialises every enemy (ECM, operation, fake values, initial proc from
 * DF_ProcTab). DFene_DecMisReload counts the enemy missile reload down once per fight frame.
 * Versus stages have no enemies (StgInfo.cnum == 0), so only the table fills / globals run there; the per-enemy
 * loops are asm-verified only.
 * garage3 layer 1 (test-room enemies): enemy_initialize (per-enemy setup at stage start), enemy_get_objcmat (24
 * discern-vertex matrices per enemy), ene_set_think (think function from the logic record), DF_EnemyProc (per-frame
 * enemy update) and Enemy_SFXHit (damage / kill credit / knockback of a hit).
 * garage3 layer 2: Enemy_CalcTemp (heat / temperature per frame, overheat damage), enemy_dying_proc (death after the
 * dying countdown: kill credit, squad, death spell), __SetDestroyInfo (kill statistics), DFene_Kill (start dying),
 * enemy_init_object, default_select_weapon (weighted random weapon pick), ene_dead, e091_turn (yaw turn + wrap),
 * DFene_GetOBJCMAT, chr_damage_calc (defence-scaled damage), DFene_SetFunc, Enemy_AllEnemyThinkOnOff.
 * chr_damage_calc2 / 3 (bmd_type 1 / 2) and the think / proc callbacks stay interpreted (thunks). */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"
#include "acnb/sdk.h"
#include <cstring>

static inline float ene_fbits(unsigned int u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static void __ReadEnemyLogic(void)
{
    PS2_FRAME(0x70);
    for (int i = 0; i < 0x100; i++) s_EneID2EneLogicID[i] = -1;
    for (int i = 0; i < 0x10; i++) s_pEnemyLogic[i] = 0;
    int n = 0;
    EnemyLogicSt **slot = s_pEnemyLogic;
    EnemySt *e = _EnemyTop;
    for (int i = 0; i < EnemyNum; i++, e++) {
        unsigned int id = e->edat->logic_id;
        char *t = &s_EneID2EneLogicID[id];
        if (*t < 0) {
            *slot = (EnemyLogicSt *)Memm_Alloc(1, 0x500, 0x10);
            File_Read(id + 0x6a4, -1, (char *)*slot, 1);
            *t = (char)n;
            slot++;
            n++;
        }
    }
    e = _EnemyTop;
    for (int i = 0; i < EnemyNum; i++, e++)
        e->logp = s_pEnemyLogic[(int)s_EneID2EneLogicID[e->edat->logic_id]];
}

/* (the id table is indexed by the raw u16 eid, not bounded to its 256 entries) */
static void __ReadEnemyData(void)
{
    PS2_FRAME(0x50);
    for (int i = 0; i < 0x100; i++) s_EneID2EneDataID[i] = -1;
    for (int i = 0; i < 0x10; i++) s_pEnemyData[i] = 0;
    int k = 0;
    EnemyDataSt **slot = s_pEnemyData;
    StModel *m = _ModelTop;
    for (int i = 0; i < EnemyNum; i++, m++) {
        if (s_EneID2EneDataID[m->array->eid] < 0) {
            *slot = (EnemyDataSt *)Memm_Alloc(1, 0x200, 0x10);
            File_Read(m->array->eid + 0x578, -1, (char *)*slot, 1);
            s_EneID2EneDataID[m->array->eid] = (char)k;
            slot++;
            k++;
        }
    }
    m = _ModelTop;
    EnemySt *e = _EnemyTop;
    for (int i = 0; i < EnemyNum; i++, m++, e++)
        e->edat = s_pEnemyData[(int)s_EneID2EneDataID[m->array->eid]];
}

void DFene_DecMisReload(void)
{
    if (ENE_mis_reload > 0) ENE_mis_reload--;
}

void ENEMY_AllEnemyInit(int enecnt, EnemySt *enebuff, StModel *modelbuff)
{
    PS2_FRAME(0x60);
    _ModelTop = modelbuff;
    _EnemyLast = enebuff + (enecnt - 1);   /* enebuff - 1 when there are no enemies */
    EnemyNum = enecnt;
    _EnemyTop = enebuff;
    __ReadEnemyData();
    __ReadEnemyLogic();
    Edata_ReadJconData();
    for (int i = 0; i < enecnt; i++) {
        enemy_initialize(i, &enebuff[i], &modelbuff[i], enebuff[i].edat->pid, 0);
        enemy_get_objcmat(i, &modelbuff[i]);
    }
    EnemySt *e = enebuff;
    for (int i = 0; i < enecnt; i++, e++) {
        e->ecm_level = e->edat->ecm_level;
        e->ecm_resist = e->edat->ecm_resist;
        e->ecm_register_id = 0;
        e->ecm_belonging = e->mp->array->belonging;
        if (!e->ecm_belonging) e->ecm_belonging = 2;
        EneCtrl_RegistECM(e);
        ene_operation_init(e);   /* the original also passes e->mp in a1; ene_operation_init never reads it */
        e->fake_value_prec = EneLgic_GetFakePrec(e->logp[(int)e->p_lv].fake_id, g_sortie.pconstant->lvFake);
        e->fake_value_freq = EneLgic_GetFakeFreq(e->logp[(int)e->p_lv].fake_id, g_sortie.pconstant->lvFake);
        if (!(e->use & 0x80)) {
            e->mainfunc = DF_ProcTab[e->pid].init;
            e->acttime = 0;
            if (e->mainfunc) gcall(e->mainfunc, e, e->mp);
        }
    }
    DF_EINFO_Init();
}

void ENEMY_New(int cnum)
{
    PS2_FRAME(0x20);
    Enemy = cnum > 0 ? (EnemySt *)Memm_Alloc(1, cnum << 9, 0x10) : 0;
    ene_vtx = cnum > 0 ? (ObjCMatSt(*)[24])Memm_Alloc(1, cnum * 0x780, 0x80) : 0;
}

/* enemy by index; NULL outside 0 .. EnemyNum - 1 (always NULL in Versus: no enemies) */
EnemySt *DFene_GetEnemy(int id)
{
    if (id < 0 || id > EnemyNum - 1) return NULL;
    return (EnemySt *)((char *)_EnemyTop + (id << 9));
}

/* think function by the logic record's think_id; floorH = hbase & 7 */
void ene_set_think(EnemySt *ep)
{
    switch (ep->logp[(int)ep->p_lv].think_id) {
    case 0x7f: ep->thinkfunc = GFN(EneSq_Think); break;
    case 0x23: ep->thinkfunc = GFN(e035_think); break;
    case 0xf:  ep->thinkfunc = GFN(e015_think); break;
    case 5:    ep->thinkfunc = GFN(e005_think); break;
    case 1:    ep->thinkfunc = GFN(e001_think); break;
    default:   ep->thinkfunc = GFN(e120_think); break;
    }
    ep->sw2.floorH = ep->logp[(int)ep->p_lv].hbase & 7;
    ep->op_wk[0] = 0;
    ep->op_wk[1] = 0;
}

/* Damage from a hit (dstat NULL = instant kill), kill credit for an AC shooter (s_type 2), knockback.
 * Quirks kept: an attached enemy whose attach_id >= EnemyNum recurses with ep == NULL (reads PS2 address 0);
 * the hp test is damage >= (int)hp and negative damage heals with a 16-bit wrap; g_ac[s_id] is not range-checked;
 * s_type 1 tags the AC id with 0x30000000, s_type 2 passes it raw; the atan2f block before ang[1] = 0 is dead. */
void Enemy_SFXHit(EnemySt *ep, SfxDstat *dstat, float *vel, float *loc, int s_type, int s_id)
{
    PS2_FRAME(0xf0);
    StModel *mp = ep->mp;
    EnemyDataSt *edat = ep->edat;
    int nowhp0 = 0;
    int dying_cnt = edat->dying_cnt;
    if (s_type == 2 && s_id == 0x7f) {
        s_type = 0;
        s_id = 0;
    }
    if (ep->sw2.attach_dmg && ep->attach_type == 3) {
        int id = ep->attach_id;
        EnemySt *parent = (id > EnemyNum - 1) ? (EnemySt *)PS2_NULL_MEM()
                                              : (EnemySt *)((char *)_EnemyTop + (id << 9));
        Enemy_SFXHit(parent, dstat, vel, loc, s_type, s_id);
    }
    if (ep->sw.undead || ep->eff.nodamage) return;
    nowhp0 = ep->nowhp;
    if (!dstat) {
        ep->nowhp = 0;
    } else {
        int def_shell, def_energy;
        if (ep->bmd_type == 5 && (ep->bst == 0xd || ep->bst == 0xb)) {
            def_shell = 0;
            def_energy = 0;
        } else {
            def_shell = edat->def_shell;
            def_energy = edat->def_energy;
        }
        int damage = chr_damage_calc(def_energy, def_shell, dstat);
        switch (ep->bmd_type) {
        case 1:
            damage = chr_damage_calc2(damage, edat->cparam[0], ene_fbits(0x3c8efa35) * edat->fparam[0], mp, loc);
            break;
        case 2:
            damage = chr_damage_calc3(damage, edat->cparam[0], ene_fbits(0x3c8efa35) * edat->fparam[0], mp, loc);
            break;
        case 3:
            if (damage > 0) {
                int d = edat->cparam[0];
                if (d < -8) d = -8;
                else if (d >= 8) d = 7;
                damage = ((damage * (8 - d)) >> 3) + 1;
            }
            break;
        }
        int hp = ep->nowhp;
        if (damage < hp) {
            ep->nowhp = (unsigned short)(hp - (damage & 0xffff));
        } else {
            ep->nowhp = 0;
            if (edat->hp > 0) {
                if (ep->bmd_type != 4 && (int)((unsigned int)damage << 6) / edat->hp > 0x60) dying_cnt = 1;
            } else {
                dying_cnt = 1;
            }
        }
        if (s_type == 1) {
            DF_ComDamage(ep->id | 0x30000000, s_id | 0x30000000, damage);
        } else if (s_type == 2) {
            DF_ComDamage(ep->id | 0x30000000, s_id, damage);
            if (ep->nowhp == 0 && !(ep->use & 0x40) && !(ep->use & 0x80)) {
                g_ac[s_id].stat |= 0x10;
                Info_IncData(1, 6, s_id, 0);
                CombatRecord_OnKill(&g_ac[s_id], ep->edat->kind);
                if (g_ac[s_id].mp->cflag & ep->mp->cflag & 0xf) Info_IncData(1, 7, s_id, 0);
                else                                             Info_IncData(1, 8, s_id, 0);
            }
        }
        DF_TALK2_Damaged(mp, s_type, s_id);   /* the asm also passes damage in a3; the callee takes 3 */
        if (ep->bmd_dmg < damage && !ep->sw2.straight) {
            ep->bmd_dmg = (short)damage;
            ep->bmd_id = (unsigned short)(((s_type & 3) << 14) | (s_id & 0x3fff));
        }
        if ((dstat->effect & 0x40) && !ep->sw.noecm) ep->nolock_time = 0x13b;
        if ((dstat->effect & 0x80) && !ep->sw.noflare && ep->nowhp > 0) ep->flare_time = 100;
    }
    if (mp && nowhp0 > 0) {
        mp->shooter = (char)s_type;
        mp->shooterid = (unsigned char)s_id;
    }
    if (ep->nowhp == 0 && !(ep->use & 0x40)) {
        DFene_Kill(ep, dying_cnt);
        return;
    }
    if (ep->nowhp == 0 || (ep->use & 0x40) || dstat->react == 0) return;
    int k = ps2_ftoi(ep->edat->react) - 1;
    if (k < 0 || !(k < (int)dstat->react)) return;

    /* knockback */
    const float pi = ene_fbits(0x40490fdb), pi2 = ene_fbits(0x40c90fdb), npi = ene_fbits(0xc0490fdb);
    alignas(16) float center[4];
    alignas(16) float ang[4];
    alignas(16) float vec[4];
    const float *lq_loc = (const float *)PS2_LQ_SRC(loc);
    DF_GetModelCenter(center, mp);
    vu_sub_xyz(vec, lq_loc, center);
    DF_CALC_Vector2Angle(ang, vec);
    float f = pi + mp->ang[1];
    while (!(f <= pi)) f = f - pi2;
    while (f < npi) f = f + pi2;
    ang[1] = ang[1] - f;
    while (!(ang[1] <= pi)) ang[1] = ang[1] - pi2;
    while (ang[1] < npi) ang[1] = ang[1] + pi2;
    ep->react_time = (unsigned char)(dstat->react - (ps2_ftoi(ep->edat->react) - 1));
    float a = ang[1];
    if (a < 0.0f) a = -a;
    ep->react_type = (a <= ene_fbits(0x3fc90fdb)) ? 1 : 2;
    if (ep->react_time < 5) ep->react_time = 5;
    vu_sub_xyz(vec, center, lq_loc);
    vec[1] = 0.0f;        /* both stored before the normalise reloads vec: horizontal push */
    vec[3] = 1.0f;
    vu_normalize(vec, vec);
    vu_normalize(ang, (const float *)PS2_LQ_SRC(vel));
    ang[0] = ang[0] + vec[0] / 16.0f;
    ang[2] = ang[2] + vec[2] / 16.0f;
    {   /* dead in the original: the wrapped angle is never read */
        float d = ps2_atan2f(ang[0], ang[2]) - ep->mp->ang[1];
        while (!(d <= pi)) d = d - pi2;
        while (d < npi) d = d + pi2;
        (void)d;
    }
    ang[1] = 0.0f;
    float t = (float)ep->react_time;
    ang[0] = ang[0] * (0.25f * (0.5f * t));
    t = (float)ep->react_time;
    ang[2] = ang[2] * (0.25f * (0.5f * t));
    ep->vel[0] = ep->vel[0] + ang[0];   /* vadd.xyz: vel.w kept */
    ep->vel[1] = ep->vel[1] + ang[1];
    ep->vel[2] = ep->vel[2] + ang[2];
}

/* R of the 24 discern vertices (word 3 of the original 16-byte rows at 0x1e6d360; words 0-2 are not read) */
static const float s_ene_vtx_r[24] = {
    12.0f, 13.0f, 14.0f, 15.0f, 16.0f, 17.0f, 18.0f, 19.0f, 20.0f, 21.0f, 67.0f, 68.0f,
    69.0f, 70.0f, 61.0f, 60.0f, 72.0f, 73.0f, 74.0f, 75.0f, 63.0f, 64.0f, 65.0f, 66.0f,
};

/* 24 object-attached matrices per enemy (ene_vtx[id]) from the model's discern vertices; the normal is negated */
static void enemy_get_objcmat(int id, StModel *mp)
{
    PS2_FRAME(0xa0);
    PS2_STACK(float[4], norm, 0x80);
    PS2_STACK(float[4], loc, 0x90);
    ObjCMatSt *slot = ene_vtx[id];
    for (int i = 0; i < 24; i++, slot++) {
        ZeroMemory32b(slot, 0x14);
        int oid = Model_GetDiscernVertex(mp, loc, norm, s_ene_vtx_r[i], 0.0f, 0.0f);
        if (oid >= 0) {
            norm[3] = 0.0f;
            norm[0] = norm[0] * -1.0f;   /* vmulx.xyz by vf31x = -1.0f (f20 = 0xbf800000) */
            norm[1] = norm[1] * -1.0f;
            norm[2] = norm[2] * -1.0f;
            Zvec2Matrix(slot->mat, norm);
            memcpy(slot->mat[3], loc, 16);
            slot->parentmatp = *(float (**)[4][4])((char *)mp->op + (oid << 7) + 0x60);
        }
    }
}

/* MWCC's float -> unsigned conversion (values >= 2^31 go through f - 2^31, then | 0x80000000) */
static inline unsigned int ene_ftou(float f)
{
    const float two31 = ene_fbits(0x4f000000);
    if (two31 <= f) return (unsigned int)ps2_ftoi(f - two31) | 0x80000000u;
    return (unsigned int)ps2_ftoi(f);
}

/* Heat / temperature update once per frame; above the heat limit the excess temperature hurts (Enemy_SFXHit). */
static void Enemy_CalcTemp(EnemySt *ep)
{
    PS2_FRAME(0x40);
    alignas(16) float vel[4];
    alignas(16) SfxDstat dstat;
    memset(vel, 0, 16);
    if (ep->edat->cooling <= 0.0f) return;   /* c.ole: a NaN cooling goes on */
    memset(&dstat, 0, 16);
    const float A = g_asmparam.addcqheat_per_frame;
    float net = 0.0f;
    net = net + (float)ep->heat;
    net = net + (float)ep->hit_heat;
    net = net - (float)ps2_ftoi(ep->edat->cooling);   /* cooling truncated to an integer */
    float f3 = net * A;
    ep->temperature = (short)ps2_ftoi((float)ep->temperature + f3 * g_asmparam.cqheat_to_tmprt);
    if ((float)ep->temperature < g_asmparam.min_actmprt) ep->temperature = (short)ps2_ftoi(g_asmparam.min_actmprt);
    const float z = 0.0f;
    float heat = net - z * (1.0f - g_asmparam.addcqheat_per_frame);   /* dead product kept as the original has it */
    heat = heat - f3;
    if (heat <= z) heat = z;
    ep->heat = ps2_ftoi(heat);
    ep->hit_heat = 0;
    int reg = ep->edat->heatReg;
    int tmp = ep->temperature;
    if (reg < tmp) {
        float f = g_asmparam.qcool_damage_ratio * (float)(tmp - reg);
        dstat.damage = (unsigned short)ene_ftou(f);
        Enemy_SFXHit(ep, &dstat, vel, ep->mp->loc, 0, 0);
    }
}

void Enemy_AllEnemyThinkOnOff(int flag)
{
    for (int i = 0; i < EnemyNum; i++) {
        /* the original's out-of-range arm (NULL) is dead: i is always in range */
        EnemySt *ep = _EnemyTop + i;
        ep->sw.think = flag & 1;
    }
}

/* kill statistics for the mission info store */
static void __SetDestroyInfo(EnemySt *pEne)
{
    PS2_FRAME(0x30);
    StModel *mp = pEne->mp;
    Info_IncData(3, (mp->cflag & 1) ? 4 : 5, 0, 0);
    if (mp->shooter == 2) {                         /* killed by an AC */
        unsigned int id = mp->shooterid;
        if (id == 0x7f) return;                     /* no DF_EINFO_GrpDestroyCnt on these returns */
        if (id >= 4) return;
        if (id == 0) {
            Info_IncData(0, 0x16, 0, mp->on);
        } else {
            Info_IncData(3, (g_ac[id].mp->cflag & 1) ? 0xb : 0x10, 0, mp->on);
        }
    } else if (mp->shooter == 1) {                  /* killed by an enemy: no NULL guard in the original */
        int id = mp->shooterid;
        const EnemySt *parent = (id > EnemyNum - 1) ? (const EnemySt *)PS2_NULL_MEM() : _EnemyTop + id;
        Info_IncData(3, (parent->mp->cflag & 1) ? 0xb : 0x10, 0, mp->on);
    } else {
        Info_IncData(3, 0x10, 0, mp->on);
    }
    DF_EINFO_GrpDestroyCnt(pEne);
}

/* Death after the dying countdown (DF_EnemyProc, every frame). */
static void enemy_dying_proc(EnemySt *ep, StModel *mp)
{
    PS2_FRAME(0x60);
    if (!(ep->use & 0x40)) return;
    if (ep->use & 0x80) return;
    if (ep->sw.killed) return;
    if (ep->dying_cnt > 0) {
        ep->dying_cnt--;
        return;
    }
    if (!(ep->mp->cflag & 1)) Info_DecData(3, 7, 0, 0);
    EneCtrl_UnRegistECM(ep);
    ep->dmyp = 0;
    if (ep->edat->flag & 0x20) {                    /* never dies: stays as a corpse */
        ep->sw.undead = 1;
        mp->cflag |= 0x5001;
        ep->sw.killed = 1;
    } else {
        if (!(ep->use & 0x80) && ep->deadfunc) gcall(ep->deadfunc, ep);
        ep->use |= 0x80;
        if (ep->child) DFene_Kill(ep->child, 0);
        if (ep->sibling) DFene_Kill(ep->sibling, 0);
    }
    if (ep->sqp) {
        if (ep->sqmid == 0 || !EneSq_IsSquadCtrlNum(*(unsigned char *)ep->sqp))
            EneSq_SquadDestroy(*(unsigned char *)ep->sqp);
    }
    if (!(ep->use & 8) && ep->edat->sfxid[1] > 0) {
        PS2_STACK(float[4], center, 0x50);
        DF_GetModelCenter(center, mp);
        PS2_STACK(unsigned long long[4], sargs, 0);   /* the original's stack arguments 9-12 */
        for (int i = 0; i < 4; i++) sargs[i] = 0;
        DF_SPELL2_AppearSpell(ep->edat->sfxid[1], 0, 0, 0, 0, center, 0, 0, 0, 0, 0, 0);
    }
    DFene_SEStop(ep, -1);
    __SetDestroyInfo(ep);
}

static void enemy_init_object(StModel *mp)
{
    for (int i = 0; i < mp->nobj; i++) {
        ObjectSt *o = mp->op + i;
        o->noanim = 0;
        o->now_key = 0;
        o->todid = 0xff;
        o->frame = 0;
        o->group = 1;
        o->hokantype = 2;
        o->onoff = 0;
        o->flag = 0;
    }
    for (int i = 0; i < 8; i++) mp->grptodid[i] = 0xff;
}

/* Weighted random pick among the weapons whose range window holds the target (edat unused). */
int default_select_weapon(EnemySt *ep, EnemyDataSt *edat, EneInfoSt *eip)
{
    PS2_FRAME(0x70);
    (void)edat;
    int dy = ps2_ftoi(eip->eloc[1]);
    int dxz = ps2_ftoi(eip->elenxz);
    if (dy < 0) dy = -dy;
    struct_f8da75e1 *atk = ep->logp[(int)ep->p_lv].atk_tab;
    int idx[4], cum[4];
    int sum = 0, n = 0;
    for (int i = 0; i < 4; i++, atk++) {
        if (ep->wep_valid & (1 << (i + 2))) continue;   /* locked weapon */
        int prob = atk->prob;
        if (prob == 0) continue;
        if (dxz < atk->range_min) continue;
        if (atk->range_max < dxz) continue;
        if (atk->range_max < dy) continue;
        sum += prob;
        cum[n] = sum;
        idx[n] = i;
        n++;
    }
    if (n == 0) return -1;
    if (n == 1) return idx[0];
    int r = 0;
    if (sum > 0) r = ps2_mod32(ps2_rand(), sum << 14) >> 14;
    for (int j = 0; j < n; j++)
        if (r < cum[j]) return idx[j];
    return -1;
}

void ene_dead(EnemySt *ep)
{
    PS2_FRAME(0x20);
    ene_boost_sfx_delete(ep);
    DF_MAGIC2_Kill_ID(ep->h_sfx2[0]);
    ep->h_sfx2[0] = 0;
    DF_MAGIC2_Kill_ID(ep->h_sfx2[1]);
    ep->h_sfx2[1] = 0;
}

/* yaw turn: wrap the angle into [-pi, pi], view.angy = the opposite direction, then the turn speed update */
void e091_turn(EnemySt *ep, StModel *mp, EnemyDataSt *edp, float cw, float brake)
{
    const float PI = ene_fbits(0x40490fdb), PI2 = ene_fbits(0x40c90fdb), NPI = ene_fbits(0xc0490fdb);
    float a = mp->ang[1] + ep->turn_spd;
    if (!(a <= PI)) {
        do a = a - PI2;
        while (!(a <= PI));
    }
    if (a < NPI) {
        do a = a + PI2;
        while (a < NPI);
    }
    mp->ang[1] = a;
    float b = a + PI;
    if (!(b <= PI)) b = b - PI2;   /* one subtraction */
    ep->view.angy = b;
    ene_turn_speed(&ep->turn_spd, edp->t_accel, edp->t_max, brake, cw);   /* tail call */
}

ObjCMatSt *DFene_GetOBJCMAT(int id, int vtx)
{
    if (id < 0 || id > EnemyNum - 1) return 0;
    if (vtx < 0 || vtx >= 24) return 0;
    return &ene_vtx[id][vtx];
}

/* Start dying: weapon joint control off, boost / sounds off, the dead proc, dying countdown. */
void DFene_Kill(EnemySt *ep, int dying_cnt)
{
    PS2_FRAME(0x30);
    if (ep->use & 0x40) return;
    if (ep->use & 0x80) return;
    if (ep->mp->use & 4) return;
    _jdat_tab_tag_ *jc = DFene_GetJConTab(ep->edat->wep_tab[(int)ep->cur_wep].jcon);
    if (jc) ene_jcon_mng(ep, ep->mp, jc->jdat, jc->num, 0);
    ep->eff.nowbst = 0;   /* delay slot of the ene_boost_sfx jal: before the call */
    ene_boost_sfx(ep, 0);
    DFene_SEStop(ep, -1);
    ep->use |= 0x40;
    void (*f)(EnemySt *, StModel *) = DF_ProcTab[ep->pid].dying;
    if (f != ep->mainfunc) {
        ep->mainfunc = f;
        ep->acttime = 0;
    }
    ep->dying_cnt = (unsigned short)dying_cnt;
    if (dying_cnt > 0) {
        StModel *m = ep->mp;
        SFX_Dying(m, ep->dying_cnt);
        GaugeFEIncreaseDestEne();
        if (ep->mainfunc == 0) Anim_ChangeAnim(m, 0x1c + ((ps2_rand() % 0x8000) >> 14), 0x1e, 0);   /* one rand */
    }
    ep->nowhp = 0;
}

/* damage scaled by the shell (0x10) / energy (0x20) defence, clamped to -8..7 */
static int chr_damage_calc(int en_def, int sh_def, SfxDstat *dstp)
{
    int dmg = dstp->damage;
    int d;
    switch (dstp->effect & 0x30) {
    case 0x10:
        if (!dmg) return 0;
        d = sh_def;
        break;
    case 0x20:
        if (!dmg) return 0;
        d = en_def;
        break;
    default:
        return dmg;
    }
    if (d < -8) d = -8;
    else if (d >= 8) d = 7;
    return (((8 - d) * dmg) >> 3) + 1;
}

int DFene_SetFunc(EnemySt *ep, void *func)
{
    if (func == (void *)ep->mainfunc) return 0;
    ep->mainfunc = (void (*)(EnemySt *, StModel *))func;
    ep->acttime = 0;
    return 1;
}

/* per-frame enemy update (DF_FIGHT_FightProc) */
void DF_EnemyProc(EnemySt *ep)
{
    PS2_FRAME(0x60);
    PS2_STACK(char[40], t, 0x30);   /* DWARF's int t[10]: cleared, never read */
    for (int i = 0; i < 40; i++) t[i] = 0;
    StModel *mp0 = ep->mp;
    void (*oldfunc)(EnemySt *, StModel *) = ep->mainfunc;
    if (ep->sw.regene) {
        ep->mp->use &= 0xfffb;
        ep->sw.regene = 0;
    }
    memcpy(ep->loc0, mp0->loc, 16);
    if (ep->sw.control && ep->mainfunc) gcall(ep->mainfunc, ep, ep->mp);
    unsigned short noise = EcmField_GetEcmNoiseLv(ep->ecm_belonging);
    float f = (float)((int)noise - ep->ecm_resist);
    if (0.0f < f) {
        f = f / 1000.0f;
        if (1.0f < f) f = 1.0f;
        ep->ecm_affected = f * 50.0f;
    } else {
        ep->ecm_affected = 0.0f;
    }
    if (0.0f < ep->ecm_affected && ((ps2_rand() % 0x14000) >> 14) == 0) DFene_SFX_ECM(ep->mp);
    if (ep->flare_time > 0) {
        ep->flare_time--;
        DFene_SFX_NAP(ep->mp);
    }
    Enemy_CalcTemp(ep);
    if (ep->chkfloor) gcall(ep->chkfloor, ep, ep->mp);
    else ep->mp->yGround = ene_fbits(0x7f7fffff);
    if (ep->animfunc && ep->sw.control) gcall(ep->animfunc, ep->mp);
    if (oldfunc == ep->mainfunc && ep->acttime < 0x7fff) ep->acttime++;
    enemy_dying_proc(ep, ep->mp);
    if ((ep->edat->flag & 0x800) && DFene_ChkRingout(ep)) DFene_Kill(ep, ep->edat->dying_cnt);
    ep->bmd_dmg = 0;
    ep->bmd_id = 0;
}

/* Per-enemy setup at stage start (wipes the EnemySt but keeps edat / logp / dmyp). Quirks kept: pilot_lv 0 -> p_lv 2;
 * the cflag merge keeps the old low three bits when any is set. */
static void enemy_initialize(int id, EnemySt *ep, StModel *mp, int pid, float *loc)
{
    PS2_FRAME(0x90);
    EnemyDataSt *edatp = ep->edat;
    EnemyLogicSt *logicp = ep->logp;
    void *dmyp = ep->dmyp;
    ZeroMemory512b(ep, 8);
    ep->edat = edatp;
    ep->logp = logicp;
    ep->dmyp = dmyp;
    mp->arg0p = ep;
    if (mp->array->pilot_lv) ep->p_lv = (char)(mp->array->pilot_lv - 1);
    else ep->p_lv = 2;
    ArraySt *arr = mp->array;
    mp->R = edatp->R * mp->scale[0];
    mp->tall = edatp->tall * mp->scale[1];
    if (!loc) loc = (float *)arr;
    memcpy(mp->loc, PS2_LQ_SRC(loc), 16);
    mp->nowparent = HitBoxLineDecideParent(mp->loc, 5, 0);   /* sw v0, 0x154(s4): s4 = mp (StModel.nowparent) */
    mp->nowparentbk = 0xff;
    if (mp->cflag & 7) {
        mp->cflag = mp->cflag | (edatp->cflag & 0xfff8);
    } else {
        mp->cflag = edatp->cflag;
        if (!(edatp->cflag & 1)) mp->cflag |= 2;
    }
    if (edatp->flag & 0x1000) mp->use |= 0x200;
    if (edatp->flag & 0x4000) mp->use |= 2;
    enemy_init_object(mp);
    ep->initialize = 1;
    if (pid >= 0x80) pid = 0;
    ep->pid = (unsigned short)pid;
    ep->id = (unsigned char)id;
    ep->mp = mp;
    ep->use = 0;
    for (int i = 0; i < 3; i++) ep->vh[i] = 0x50;
    ep->gravity = ene_fbits(0x3d2c0831);
    ep->fallmax = ene_fbits(0x3fdeb852);
    if (edatp->gravt != 0.0f) {
        ep->gravity = ep->gravity * (1.0f + edatp->gravt);
        ep->fallmax = ep->fallmax * (1.0f + edatp->gravt);
    }
    if (!(edatp->flag & 0x40)) ep->sw.flier = 1;
    if (edatp->flag & 0x200) ep->sw.noarea = 1;
    if (!(edatp->flag & 0x400)) ep->sw.shadow = 1;
    if (edatp->flag & 1) ep->sw.noecm = 1;
    if (edatp->flag & 2) ep->sw.noflare = 1;
    ep->sw.control = 1;
    ep->sw.think = 1;
    ep->mainfunc = 0;
    ep->animfunc = GFN(DFene_AnimUpdate);
    ep->chkfloor = GFN(DFene_Falling);
    ep->updmatrix = GFN(DFene_UpdateMatrix);
    ep->use &= ~2;
    ep->use &= ~1;
    memcpy(mp->ang, arr->ang, 16);
    memcpy(mp->scale, arr->scale, 16);
    mp->scale[3] = 0.0f;
    memcpy(ep->initpos, ep->mp->loc, 16);
    memcpy(ep->loc0, ep->mp->loc, 16);
    ep->vel[0] = 0.0f;
    ep->vel[1] = 0.0f;
    ep->vel[2] = 0.0f;
    ep->vel[3] = 0.0f;
    const float pi = ene_fbits(0x40490fdb);
    ep->view.angy = pi + mp->ang[1];
    if (!(ep->view.angy <= pi)) ep->view.angy = ep->view.angy - ene_fbits(0x40c90fdb);
    ep->region[0][0] = Array->head.sysinfo.rgnover[0][0];
    ep->region[0][1] = Array->head.sysinfo.rgnover[0][2];
    ep->region[1][0] = Array->head.sysinfo.rgnover[1][0];
    ep->region[1][1] = Array->head.sysinfo.rgnover[1][2];
    ep->mrange = ep->logp[(int)ep->p_lv].mov_range;
    ep->attach[0] = ep->id;
    ep->attach[1] = ep->id;
    ep->nowhp = (edatp->hp < 0xffff) ? (unsigned short)edatp->hp : 0xffff;
    ep->bmd_type = (unsigned char)edatp->bmd_type;
    ep->booster = (short)(edatp->booster << 3);
    ep->boostcost = (edatp->flag & 0x2000) ? 0 : 8;
    for (int i = 0; i < 4; i++) ep->h_sfx[i] = 0;
    for (int i = 0; i < 4; i++) ep->h_sfx2[i] = 0;
    ep->op_time_min = 0x14;
    Anim_SetAnim(mp, 0, 1);
    if (edatp->flag & 0x80) ep->chkfloor = 0;
    if (ep->chkfloor) gcall(ep->chkfloor, ep, ep->mp);
    else if (ep->sw.shadow) mp->use &= 0xff7f;
    else mp->use |= 0x80;
    if (ep->animfunc) gcall(ep->animfunc, ep->mp);
    ep->sw.water = 1;
    ep->base_prob = 100;
    ep->base_range = -1.0f;
}
