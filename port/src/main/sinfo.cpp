/* src/main/sinfo.cpp — "Info": the mission / battle statistics store behind the event system and the results screen.
 * Four tables of 4-byte union slots, one per type: 0 Player (18 slots per player), 1 AC (10 per AC), 2 Enemy (2 per
 * enemy), 3 Mission (7). A public info id maps to a slot through s_InfoToData<Type>[id]; -1 marks an "exception id"
 * computed from game state or from the destroy counters (s_DestCnt*, u8, saturating at 0xff; row 0 = player side,
 * rows 1 / 2 = the two mission counters). Info_Get / Set / Inc / DecData dispatch on the type. Sinfo_* are event-
 * condition helpers (alive / broken model counts, model-in-marker-region test). Info_CreateData / Info_InitVal run at
 * stage start.
 *
 * ABI: Info_GetData returns its union through a hidden result pointer (MW sret: a0 = result, a1.. = arguments) and
 * Info_SetData takes the union in t0: both have hand-written bridge wrappers. The static Get helpers are sret too in
 * the original; they are only called from this file, so here they return the union by value. Neither the type nor
 * the info id / index is range-checked (original; see PLAN quirks). */
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
short s_InfoToDataPlayer[24] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, -1, -1, -1, -1, -1, -1};
short s_InfoToDataAC[16] = {-1, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, -1, -1, -1, -1};
short s_InfoToDataEnemy[6] = {-1, -1, 0, 1, -1, -1};
short s_InfoToDataMission[18] = {-1, 0, 1, 2, 3, 4, 5, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
#endif

static_assert(offsetof(AcPlayerSt, param.hp) == 0x7e4, "AcPlayerSt.param.hp");

/* saturating u8 destroy counter */
static inline void sinfo_inc_u8(unsigned char *p)
{
    if (*p < 0xff) *p = *p + 1;
}

/* ---- Dec: plain decrement, no lower bound ---- */

static void __DecMissionData(int info_id, int mission_id)
{
    int s = s_InfoToDataMission[info_id];
    if (s != -1) s_ppMissionData[mission_id][s].i -= 1;
}

static void __DecEnemyData(int info_id, int enemy_id)
{
    int s = s_InfoToDataEnemy[info_id];
    if (s != -1) s_ppEnemyData[enemy_id][s].i -= 1;
}

static void __DecACData(int info_id, int ac_id)
{
    int s = s_InfoToDataAC[info_id];
    if (s == -1) return;
    if (ac_id < 0 || ac_id >= NowPlayerNum) return;
    s_ppACData[ac_id][s].i -= 1;
}

static void __DecPlayerData(int info_id, int player_id)
{
    int s = s_InfoToDataPlayer[info_id];
    if (s != -1) s_ppPlayerData[player_id][s].i -= 1;
}

/* ---- Inc: saturating at 0x7fffffff; exception ids bump the destroy counters ---- */

static void __IncMissionExceptionData(int info_id, int index, int param)
{
    (void)index;
    switch (info_id) {
    case 8: sinfo_inc_u8(&s_DestCntMap[1][param]); break;
    case 9: sinfo_inc_u8(&s_DestCntObj[1][param]); break;
    case 10: sinfo_inc_u8(&s_DestCntDobj[1][s_DobjMid2Did[param]]); break;
    case 11: sinfo_inc_u8(&s_DestCntChr[1][param]); break;
    case 12: sinfo_inc_u8(&s_DestCntAC[1][param]); break;
    case 13: sinfo_inc_u8(&s_DestCntMap[2][param]); break;
    case 14: sinfo_inc_u8(&s_DestCntObj[2][param]); break;
    case 15: sinfo_inc_u8(&s_DestCntDobj[2][s_DobjMid2Did[param]]); break;
    case 16: sinfo_inc_u8(&s_DestCntChr[2][param]); break;
    case 17: sinfo_inc_u8(&s_DestCntAC[2][param]); break;
    }
}

static inline void sinfo_inc_slot(union_9a8d3bef *p)
{
    if (p->i != 0x7fffffff) p->i = p->i + 1;
}

static void __IncMissionData(int info_id, int mission_id, int param)
{
    PS2_FRAME(0x10);
    int s = s_InfoToDataMission[info_id];
    if (s == -1) __IncMissionExceptionData(info_id, mission_id, param);
    else sinfo_inc_slot(&s_ppMissionData[mission_id][s]);
}

static void __IncEnemyData(int info_id, int enemy_id)
{
    int s = s_InfoToDataEnemy[info_id];
    if (s != -1) sinfo_inc_slot(&s_ppEnemyData[enemy_id][s]);
}

static void __IncACData(int info_id, int ac_id)
{
    int s = s_InfoToDataAC[info_id];
    if (s == -1) return;
    if (ac_id < 0 || ac_id >= NowPlayerNum) return;
    sinfo_inc_slot(&s_ppACData[ac_id][s]);
}

static void __IncPlayerExceptionData(int info_id, int index, int param)
{
    (void)index;
    if (info_id == 0x17) sinfo_inc_u8(&s_DestCntAC[0][param]);
    else if (info_id == 0x16) sinfo_inc_u8(&s_DestCntChr[0][param]);
    else if (info_id == 0x15) sinfo_inc_u8(&s_DestCntDobj[0][s_DobjMid2Did[param]]);
    else if (info_id == 0x14) sinfo_inc_u8(&s_DestCntObj[0][param]);
    else if (info_id == 0x13) sinfo_inc_u8(&s_DestCntMap[0][param]);
}

static void __IncPlayerData(int info_id, int player_id, int param)
{
    PS2_FRAME(0x10);
    int s = s_InfoToDataPlayer[info_id];
    if (s == -1) __IncPlayerExceptionData(info_id, player_id, param);
    else sinfo_inc_slot(&s_ppPlayerData[player_id][s]);
}

/* ---- Set: exception ids are not writable. The union arrives in a3 and is copied through sp+0xc ---- */

static void __SetMissionData(int info_id, int mission_id, union_9a8d3bef data)
{
    PS2_FRAME(0x10);
    PS2_STACK(union_9a8d3bef, d, 0xc);
    d = data;
    int s = s_InfoToDataMission[info_id];
    if (s != -1) s_ppMissionData[mission_id][s] = d;
}

static void __SetEnemyData(int info_id, int enemy_id, union_9a8d3bef data)
{
    PS2_FRAME(0x10);
    PS2_STACK(union_9a8d3bef, d, 0xc);
    d = data;
    int s = s_InfoToDataEnemy[info_id];
    if (s != -1) s_ppEnemyData[enemy_id][s] = d;
}

static void __SetACData(int info_id, int ac_id, union_9a8d3bef data)
{
    PS2_FRAME(0x10);
    PS2_STACK(union_9a8d3bef, d, 0xc);
    d = data;
    int s = s_InfoToDataAC[info_id];
    if (s == -1) return;
    if (ac_id < 0 || ac_id >= NowPlayerNum) return;
    s_ppACData[ac_id][s] = d;
}

static void __SetPlayerData(int info_id, int player_id, union_9a8d3bef data)
{
    PS2_FRAME(0x10);
    PS2_STACK(union_9a8d3bef, d, 0xc);
    d = data;
    int s = s_InfoToDataPlayer[info_id];
    if (s != -1) s_ppPlayerData[player_id][s] = d;
}

/* ---- Get: the result word is built in the frame (v at sp+0x2c / 0x5c) and copied out ---- */

/* id 0: mission time; 6 / 7: remaining enemies (never reached: s_InfoToDataMission maps them to slots); 8-17: the
 * mission destroy counters. Others -1. */
static union_9a8d3bef __GetMissionExceptionData(int info_id, int param)
{
    PS2_FRAME(0x30);
    PS2_STACK(union_9a8d3bef, v, 0x2c);
    v.i = -1;
    if ((unsigned int)info_id < 0x12) {
        switch (info_id) {
        case 0: v.i = g_sortie.psettle->timeMission; break;
        case 6: v.i = DF_EINFO_GetEneRemainNum(2); break;
        case 7: v.i = DF_EINFO_GetEneRemainNum(1); break;
        case 8: v.i = s_DestCntMap[1][param]; break;
        case 9: v.i = s_DestCntObj[1][param]; break;
        case 10: v.i = s_DestCntDobj[1][s_DobjMid2Did[param]]; break;
        case 11: v.i = s_DestCntChr[1][param]; break;
        case 12: v.i = s_DestCntAC[1][param]; break;
        case 13: v.i = s_DestCntMap[2][param]; break;
        case 14: v.i = s_DestCntObj[2][param]; break;
        case 15: v.i = s_DestCntDobj[2][s_DobjMid2Did[param]]; break;
        case 16: v.i = s_DestCntChr[2][param]; break;
        case 17: v.i = s_DestCntAC[2][param]; break;
        default: break;
        }
    }
    return v;
}

static union_9a8d3bef __GetMissionData(int info_id, int mission_id, int param)
{
    PS2_FRAME(0x10);
    int s = s_InfoToDataMission[info_id];
    if (s == -1) return __GetMissionExceptionData(info_id, param);
    return s_ppMissionData[mission_id][s];
}

/* the attacker of a destroyed model: shooter (sign-extended, low half) | shooterid << 16 */
static inline void sinfo_shooter(union_9a8d3bef &v, StModel *mp)
{
    v.s[0] = (short)mp->shooter;
    v.us[1] = mp->shooterid;
}

/* id 0: enemy data hp, 1: current hp, 4: attacker once destroyed (use bit 0x40 / 0x80), 5: enemy in marker region */
static union_9a8d3bef __GetEnemyExceptionData(int info_id, int enemy_id, int param)
{
    PS2_FRAME(0x60);
    PS2_STACK(union_9a8d3bef, v, 0x5c);
    v.i = -1;
    EnemySt *e = DFene_GetEnemy(enemy_id);
    if (!e) return v;
    if (info_id == 5) {
        v.i = DF_EINFO_IsEneArriveDmy(enemy_id, param) ? 1 : 0;
    } else if (info_id == 4) {
        unsigned char b = e->use;
        if ((b & 0x40) || (b & 0x80)) sinfo_shooter(v, e->mp);
    } else if (info_id == 1) {
        v.i = e->nowhp;
    } else if (info_id == 0) {
        v.i = e->edat->hp;
    }
    return v;
}

static union_9a8d3bef __GetEnemyData(int info_id, int enemy_id, int param)
{
    PS2_FRAME(0x10);
    int s = s_InfoToDataEnemy[info_id];
    if (s == -1) return __GetEnemyExceptionData(info_id, enemy_id, param);
    return s_ppEnemyData[enemy_id][s];
}

/* id 0: max hp, 1: hp, 0xc: attacker once destroyed, 0xd: AC in marker region, 0xe: damage taken from AC param,
 * 0xf: damage taken from enemies. The original's "ac == NULL" test on &g_ac[ac_id] can never be true. */
static union_9a8d3bef __GetACExceptionData(int info_id, int ac_id, int param)
{
    PS2_FRAME(0x30);
    PS2_STACK(union_9a8d3bef, v, 0x2c);
    v.i = -1;
    AcPlayerSt *ac = &g_ac[ac_id];
    if (info_id == 0xf) v.i = AcCtrl_GetDmgForEnemy(ac_id);
    else if (info_id == 0xe) v.i = AcCtrl_GetDmgForAc(ac_id, param);
    else if (info_id == 0xd) v.i = AcCtrl_IsArriveDmy(ac_id, param, 10) ? 1 : 0;
    else if (info_id == 0xc) {
        if (ac->hp == 0) sinfo_shooter(v, ac->mp);
    } else if (info_id == 1) v.i = ac->hp;
    else if (info_id == 0) v.i = ac->param.hp;
    return v;
}

static union_9a8d3bef __GetACData(int info_id, int ac_id, int param)
{
    PS2_FRAME(0x10);
    int s = s_InfoToDataAC[info_id];
    if (s == -1) return __GetACExceptionData(info_id, ac_id, param);
    return s_ppACData[ac_id][s];   /* no index check here, unlike Set / Inc / Dec */
}

/* id 0x12: marker param in sight, 0x13-0x17: the player-side destroy counters */
static union_9a8d3bef __GetPlayerExceptionData(int info_id, int param)
{
    PS2_FRAME(0x30);
    PS2_STACK(union_9a8d3bef, v, 0x2c);
    v.i = -1;
    switch (info_id) {
    case 0x12: v.i = STAGE_SightInOut(param) ? 1 : 0; break;
    case 0x13: v.i = s_DestCntMap[0][param]; break;
    case 0x14: v.i = s_DestCntObj[0][param]; break;
    case 0x15: v.i = s_DestCntDobj[0][s_DobjMid2Did[param]]; break;
    case 0x16: v.i = s_DestCntChr[0][param]; break;
    case 0x17: v.i = s_DestCntAC[0][param]; break;
    default: break;
    }
    return v;
}

static union_9a8d3bef __GetPlayerData(int info_id, int player_id, int param)
{
    PS2_FRAME(0x10);
    int s = s_InfoToDataPlayer[info_id];
    if (s == -1) return __GetPlayerExceptionData(info_id, param);
    return s_ppPlayerData[player_id][s];
}

/* ---- public dispatchers (s_GetFunc / s_SetFunc / s_IncFunc / s_DecFunc, order Player, AC, Enemy, Mission). The
 * original indexes the tables with an unchecked type (4+ runs into the next table); every caller passes 0-3. ---- */

void Info_DecData(int type, int id, int arg1, int arg2)
{
    (void)arg2;
    switch (type) {
    case 0: __DecPlayerData(id, arg1); break;
    case 1: __DecACData(id, arg1); break;
    case 2: __DecEnemyData(id, arg1); break;
    case 3: __DecMissionData(id, arg1); break;
    }
}

void Info_IncData(int type, int id, int arg1, int arg2)
{
    switch (type) {
    case 0: __IncPlayerData(id, arg1, arg2); break;
    case 1: __IncACData(id, arg1); break;
    case 2: __IncEnemyData(id, arg1); break;
    case 3: __IncMissionData(id, arg1, arg2); break;
    }
}

void Info_SetData(int type, int id, int arg1, int arg2, union_9a8d3bef data)
{
    (void)arg2;
    PS2_FRAME(0x20);
    PS2_STACK(union_9a8d3bef, d, 0x1c);
    d = data;
    switch (type) {
    case 0: __SetPlayerData(id, arg1, d); break;
    case 1: __SetACData(id, arg1, d); break;
    case 2: __SetEnemyData(id, arg1, d); break;
    case 3: __SetMissionData(id, arg1, d); break;
    }
}

union_9a8d3bef Info_GetData(int type, int id, int arg1, int arg2)
{
    union_9a8d3bef r;
    r.i = -1;
    switch (type) {
    case 0: r = __GetPlayerData(id, arg1, arg2); break;
    case 1: r = __GetACData(id, arg1, arg2); break;
    case 2: r = __GetEnemyData(id, arg1, arg2); break;
    case 3: r = __GetMissionData(id, arg1, arg2); break;
    }
    return r;
}

#ifndef ACNB_SOURCEPORT
/* MW sret: a0 = result slot, a1 = type, a2 = id, a3 = arg1, t0 = arg2 */
static void bw__Info_GetData(EECore *c)
{
    union_9a8d3bef r = Info_GetData(c->gpr[A1].sw[0], c->gpr[A2].sw[0], c->gpr[A3].sw[0], c->gpr[T0].sw[0]);
    memcpy(bridge_ptr(c->gpr[A0].uw[0]), &r, 4);
}

/* the union travels in t0 like an int */
static void bw__Info_SetData(EECore *c)
{
    union_9a8d3bef d;
    d.i = c->gpr[T0].sw[0];
    Info_SetData(c->gpr[A0].sw[0], c->gpr[A1].sw[0], c->gpr[A2].sw[0], c->gpr[A3].sw[0], d);
}

/* the static helpers as the dispatchers call them: Set (info_id, index, param, data = a3), Get sret (out = a0,
 * info_id = a1, index = a2, param = a3) */
static inline void sinfo_bw_set(EECore *c, void (*fn)(int, int, union_9a8d3bef))
{
    union_9a8d3bef d;
    d.i = c->gpr[A3].sw[0];
    fn(c->gpr[A0].sw[0], c->gpr[A1].sw[0], d);
}
static inline void sinfo_bw_ret(EECore *c, union_9a8d3bef r) { memcpy(bridge_ptr(c->gpr[A0].uw[0]), &r, 4); }
#define SINFO_A(r) (c->gpr[r].sw[0])
static void bw____SetMissionData(EECore *c) { sinfo_bw_set(c, __SetMissionData); }
static void bw____SetEnemyData(EECore *c) { sinfo_bw_set(c, __SetEnemyData); }
static void bw____SetACData(EECore *c) { sinfo_bw_set(c, __SetACData); }
static void bw____SetPlayerData(EECore *c) { sinfo_bw_set(c, __SetPlayerData); }
static void bw____GetMissionExceptionData(EECore *c) { sinfo_bw_ret(c, __GetMissionExceptionData(SINFO_A(A1), SINFO_A(A3))); }
static void bw____GetMissionData(EECore *c) { sinfo_bw_ret(c, __GetMissionData(SINFO_A(A1), SINFO_A(A2), SINFO_A(A3))); }
static void bw____GetEnemyExceptionData(EECore *c) { sinfo_bw_ret(c, __GetEnemyExceptionData(SINFO_A(A1), SINFO_A(A2), SINFO_A(A3))); }
static void bw____GetEnemyData(EECore *c) { sinfo_bw_ret(c, __GetEnemyData(SINFO_A(A1), SINFO_A(A2), SINFO_A(A3))); }
static void bw____GetACExceptionData(EECore *c) { sinfo_bw_ret(c, __GetACExceptionData(SINFO_A(A1), SINFO_A(A2), SINFO_A(A3))); }
static void bw____GetACData(EECore *c) { sinfo_bw_ret(c, __GetACData(SINFO_A(A1), SINFO_A(A2), SINFO_A(A3))); }
static void bw____GetPlayerExceptionData(EECore *c) { sinfo_bw_ret(c, __GetPlayerExceptionData(SINFO_A(A1), SINFO_A(A3))); }
static void bw____GetPlayerData(EECore *c) { sinfo_bw_ret(c, __GetPlayerData(SINFO_A(A1), SINFO_A(A2), SINFO_A(A3))); }
#undef SINFO_A
#endif

/* ---- event-condition helpers ---- */

/* models of a kind (0 map, 1 obj, 2 chr, 3 AC, 4 dobj) with model id modelId, minus the destroyed ones */
int Sinfo_GetAliveModelNum(unsigned char modelType, int modelId)
{
    PS2_FRAME(0x20);
    int count = 0;
    if (modelType == 4) {
        StModel *m = Dobj;
        for (int i = 0; i < Array->head.dnum; i++, m++) if (m->on == modelId) count++;
    } else if (modelType == 3) {
        for (int i = 0; i < NowPlayerNum; i++) if (g_ac[i].id == modelId) count++;
    } else if (modelType == 2) {
        StModel *m = Chr;
        for (int i = 0; i < Array->head.cnum; i++, m++) if (m->on == modelId) count++;
    } else if (modelType == 1) {
        StModel *m = Obj;
        for (int i = 0; i < Array->head.onum; i++, m++) if (m->on == modelId) count++;
    } else if (modelType == 0) {
        StModel *m = Map;
        for (int i = 0; i < Array->head.mnum; i++, m++) if (m->on == modelId) count++;
    } else {
        return 0;
    }
    return count - Sinfo_GetBreakModelNum(modelType, modelId);
}

/* destroyed count = player-side counter + both mission counters (the original calls through s_GetFunc[0] / [3]) */
int Sinfo_GetBreakModelNum(unsigned char modelType, int modelId)
{
    PS2_FRAME(0x30);
    PS2_STACK(union_9a8d3bef, a, 0x2c);
    PS2_STACK(union_9a8d3bef, b, 0x28);
    PS2_STACK(union_9a8d3bef, c, 0x24);
    int ip, ia, ib;
    switch (modelType) {
    case 0: ip = 0x13; ia = 8; ib = 0xd; break;
    case 1: ip = 0x14; ia = 9; ib = 0xe; break;
    case 2: ip = 0x16; ia = 0xb; ib = 0x10; break;
    case 3: ip = 0x17; ia = 0xc; ib = 0x11; break;
    case 4: ip = 0x15; ia = 0xa; ib = 0xf; break;
    default:
        c.i = 0;
        b.i = 0;
        a.i = 0;
        return a.i + (b.i + c.i);
    }
    a = __GetPlayerData(ip, 0, modelId);
    b = __GetMissionData(ia, 0, modelId);
    c = __GetMissionData(ib, 0, modelId);
    return c.i + (a.i + b.i);
}

/* is the stage model of event event_id inside the box of marker dmy_id (loc + wk lower / upper offsets) */
int Sinfo_IsRegionModel(unsigned char event_id, int dmy_id)
{
    PS2_FRAME(0x20);
    __evmng_event_id_st *ev = EVMNG_GetEventID(event_id);
    StModel *mp = STAGE_GetModelPointer(ev->type, ev->id);
    MarkerSt *mk = STAGE_GetMakerPointer(dmy_id);
    const float *lo = (const float *)&mk->wk[0];
    const float *hi = (const float *)&mk->wk[0x10];
    for (int a = 0; a < 2; a++) {
        if (mp->loc[a] < mk->loc[a] + lo[a]) return 0;
        if (!(mp->loc[a] <= mk->loc[a] + hi[a])) return 0;
    }
    if (mp->loc[2] < mk->loc[2] + lo[2]) return 0;
    return mp->loc[2] <= mk->loc[2] + hi[2] ? 1 : 0;
}

/* ---- stage start ---- */

/* allocate the row-pointer array and num zeroed rows of type; 0 on allocation failure (the arrays allocated so far
 * stay, original) or for an unknown type */
int Info_CreateData(int type, int num)
{
    PS2_FRAME(0x40);
    union_9a8d3bef ***pp;
    int size;
    if (type == 3) { pp = &s_ppMissionData; size = 0x1c; }
    else if (type == 2) { pp = &s_ppEnemyData; size = 8; }
    else if (type == 1) { pp = &s_ppACData; size = 0x28; }
    else if (type == 0) { pp = &s_ppPlayerData; size = 0x48; }
    else return 0;
    *pp = (union_9a8d3bef **)Memm_Alloc(1, num << 2, 4);
    if (!*pp) return 0;
    for (int i = 0; i < num; i++) {
        union_9a8d3bef *row = (union_9a8d3bef *)Memm_Alloc(1, size, 4);
        (*pp)[i] = row;
        if (!row) return 0;
        memset((*pp)[i], 0, size);
    }
    return 1;
}

/* clear the destroy counters and number the dobj model ids densely from 1 (s_DobjMid2Did) */
void Info_InitVal(void)
{
    PS2_FRAME(0x10);
    ZeroMemory32b(s_DestCntMap, 0x300);
    ZeroMemory32b(s_DestCntObj, 0x240);
    ZeroMemory32b(s_DestCntDobj, 0x120);
    ZeroMemory32b(s_DestCntChr, 0xc0);
    ZeroMemory32b(s_DestCntAC, 3);
    ZeroMemory32b(s_DobjMid2Did, 0xc0);
    int n = 0;
    StModel *m = Dobj;
    for (int i = 0; i < Array->head.dnum; i++, m++) {
        unsigned char *p = &s_DobjMid2Did[m->on];
        if (*p == 0) {
            n++;
            *p = (unsigned char)n;
        }
    }
}
