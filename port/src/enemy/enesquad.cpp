/* src/enemy/enesquad.cpp - enemy squads (whole file, OLSORTIE overlay). The stage's squad table (32 squad-data ids)
 * becomes up to 32 EnemySqSt managers (s_EneSqMng), each with up to 8 members (the enemies whose StModel.groupid is
 * the squad id) and a 0x600-byte record read from file 0x7ce. EneSq_Proc re-evaluates every frame which members are
 * close enough to the leader to run the squad logic, scores the three formations (move / attack / defence) with
 * condition operands and assigns formation slots; EneSq_Think is the members' think function (op table "MOVE POS" /
 * "TURN TO ENE"). Versus has no squads: only the table build (zero squads) and the per-frame loop run.
 * Every function is a literal translation of the asm; everything but BuildSquad / ReadSqData / CreateEneSqMng /
 * SetSquadLogic / Proc is verified against the asm only. */
#include <float.h>
#include <string.h>
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

static const float ENESQ_DEG2RAD = 0.017453292f;   /* 0x3c8efa35 */

/* the inlined formation-data switch (9 sites): NULL for a type outside 0..2 (dereferenced by most callers) */
static inline struct_f947d863 *enesq_form_data(EnemySqSt *pEneSq, int type)
{
    switch (type) {
    case 0: return &pEneSq->pEneSqData->movFormData;
    case 1: return &pEneSq->pEneSqData->atcFormData;
    case 2: return &pEneSq->pEneSqData->defFormData;
    }
    return NULL;
}

/* |a - b|^2 of the xyz lanes in the original's order: z2 + (x2 + y2) */
static inline float enesq_dist2(const float *a, const float *b)
{
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    float x2 = x * x, y2 = y * y, z2 = z * z;
    return z2 + (x2 + y2);
}

/* nearest free, living, squad-controlled member to pos (the leader, slot 0, is never chosen); -1 if none */
static int __GetNearMembID(EnemySqSt *pEneSq, float *pos, int *emptyMembID)
{
    float best = FLT_MAX;
    int ret = -1;
    for (int i = 1; i < pEneSq->sqNum; i++) {
        EnemySqMembSt *m = pEneSq->pSqMemb[i];
        if (emptyMembID[i] != 0) continue;
        if (m->pEne->use & 0x80) continue;
        if (!(m->membFlag & 1)) continue;
        float d2 = enesq_dist2(pos, m->pEne->mp->loc);
        if (d2 < best) {
            best = d2;
            ret = i;
        }
    }
    return ret;
}

/* places formation slot i (1..nowSqNum-1) around the centre model and gives it to the nearest free member */
static void __SetFormID(EnemySqSt *pEneSq)
{
    PS2_FRAME(0x100);
    float ofs[4] __attribute__((aligned(16)));
    float pos[4] __attribute__((aligned(16)));
    int ids[8];
    struct_f947d863 *formData = enesq_form_data(pEneSq, pEneSq->nowFormType);
    memset(ids, 0, sizeof(ids));
    if (pEneSq->nowSqNum < 2) return;
    for (int i = 1; i < pEneSq->nowSqNum; i++) {
        struct_c5c000c9 *form = &formData->form[i];
        struct_f947d863 *fd = enesq_form_data(pEneSq, pEneSq->nowFormType);
        StModel *center = fd ? __GetCenterMdlPointer(pEneSq, fd->centerType) : NULL;
        float ang = add_angf(center->ang[1], ENESQ_DEG2RAD * (form->angY - 180.0f));
        ofs[0] = fsmath_SinVU0(ang);
        ofs[1] = 0.0f;
        ofs[2] = fsmath_CosVU0(ang);
        ofs[3] = 1.0f;
        ofs[0] *= form->distXZ;
        ofs[1] *= form->distXZ;
        ofs[2] *= form->distXZ;
        ofs[1] = ofs[1] - form->distY;
        pos[0] = center->loc[0] + ofs[0];
        pos[1] = center->loc[1] + ofs[1];
        pos[2] = center->loc[2] + ofs[2];
        pos[3] = center->loc[3];
        int k = __GetNearMembID(pEneSq, pos, ids);
        /* original: k == -1 stores ids[-1] (= pos.w, never read) and writes through pSqMemb[-1] (the nowAtcNum /
         * nowAtcFrame word as a pointer); the port skips both stores */
        if (k < 0) continue;
        ids[k] = 1;
        pEneSq->pSqMemb[k]->nowFormID = (unsigned char)i;
    }
}

static int __GetOPCompareFloat(struct_53b59429 *pOP, float fParam)
{
    float p = pOP->param.f;
    switch (pOP->compareType) {
    case 0: return !(fParam <= p);
    case 1: return fParam < p;
    case 2: return fParam == p;
    case 3: return !(fParam == p);
    }
    return 0;
}

static int __GetOPCompareInt(struct_53b59429 *pOP, int iParam)
{
    int p = pOP->param.i;
    switch (pOP->compareType) {
    case 0: return p < iParam;
    case 1: return iParam < p;
    case 2: return iParam == p;
    case 3: return iParam != p;
    }
    return 0;
}

static float __GetTargetDist(EnemySqSt *pEneSq)
{
    EnemySt *leader = pEneSq->pSqMemb[0]->pEne;
    StModel *t = leader->trg_mp;
    if (!t) return FLT_MAX;
    return ps2_sqrt(enesq_dist2(t->loc, leader->mp->loc));
}

/* original: the rates are never summed - returns the last member's rate / nowSqNum (0 / 0 with no member) */
static float __GetAllMemberHPRate(EnemySqSt *pEneSq)
{
    PS2_FRAME(0x40);
    float r = 0.0f;
    for (int i = 0; i < pEneSq->nowSqNum; i++) r = DF_EINFO_GetEneHPRate(pEneSq->pSqMemb[i]->pEne->id);
    return ps2_div(r, (float)pEneSq->nowSqNum);
}

static float __GetLeaderHPRate(EnemySqSt *pEneSq)
{
    return DF_EINFO_GetEneHPRate(pEneSq->pSqMemb[0]->pEne->id);
}

static float __GetOPVal(EnemySqSt *pEneSq, struct_53b59429 *pOP)
{
    PS2_FRAME(0x20);
    if (pOP->cof == 0.0f) return 0.0f;
    int r;
    switch (pOP->type) {
    case 0: r = __GetOPCompareInt(pOP, ps2_ftoi(100.0f * __GetLeaderHPRate(pEneSq))); break;
    case 1: r = __GetOPCompareInt(pOP, ps2_ftoi(100.0f * __GetAllMemberHPRate(pEneSq))); break;
    case 2: r = __GetOPCompareFloat(pOP, __GetTargetDist(pEneSq)); break;
    default: r = 0; break;
    }
    return r ? pOP->cof : 0.0f;
}

static float __GetFormVal(EnemySqSt *pEneSq, int formID)
{
    PS2_FRAME(0x50);
    struct_53b59429 *ops;
    switch (formID) {
    case 0: ops = pEneSq->pEneSqData->movOP; break;
    case 1: ops = pEneSq->pEneSqData->atcOP; break;
    case 2: ops = pEneSq->pEneSqData->defOP; break;
    default: ops = NULL; break;
    }
    float sum = 0.0f;
    for (int j = 0; j < 8; j++) sum = sum + __GetOPVal(pEneSq, &ops[j]);
    return sum;
}

/* every formData->frame calls: recount the living members and switch to the best-scoring formation */
static void __SelectForm(EnemySqSt *pEneSq)
{
    PS2_FRAME(0x50);
    float best = -1.0f;
    int cur = pEneSq->nowFormType;
    struct_f947d863 *formData = enesq_form_data(pEneSq, cur);
    if (pEneSq->nowOPFrame < formData->frame) {
        pEneSq->nowOPFrame++;
        return;
    }
    EnemySqSt *sq = s_EneSqMng.pEneSq[pEneSq->sqID];
    int n = 0;
    if (sq->sqFlag & 1) {
        int num = sq->sqNum;
        for (int i = 0; i < num; i++) {
            EnemySqMembSt *m = sq->pSqMemb[i];
            if (!(m->pEne->use & 0x80) && (m->membFlag & 1)) n++;
        }
    }
    pEneSq->nowSqNum = (unsigned char)n;
    for (int t = 0; t < 3; t++) {
        float v = __GetFormVal(pEneSq, t);
        if (!(v <= best)) {
            best = v;
            cur = t;
        }
    }
    pEneSq->nowFormType = (unsigned char)cur;
    pEneSq->nowOPFrame = 0;
    if (pEneSq->pEneSqData->ctrl.sw.keepFormation) __SetFormID(pEneSq);
}

/* member e (squad slot i) gets the logic id, a pilot level and the think function */
static void enesq_set_logic(EnemySqSt *pEneSq, EnemySqMembSt *m, int i, int logic)
{
    char p_lv = (char)__GetPilotLV(pEneSq, i);
    m->pEne->p_lv = p_lv;
    m->pEne->logp = Enemy_GetLogicPointer(logic);
    ene_set_think(m->pEne);
}

/* members within squadDist of the leader (re)join squad control, the others leave it */
static void __SelectSquadCtrl(EnemySqSt *pEneSq)
{
    PS2_FRAME(0x60);
    for (int i = 0; i < pEneSq->sqNum; i++) {
        EnemySqMembSt *m = pEneSq->pSqMemb[i];
        EnemySt *e = m->pEne;
        if (e->use & 0xc0) continue;
        EnemySt *leader = e->sqp ? e->sqp->pSqMemb[0]->pEne : NULL;
        struct_f947d863 *formData = enesq_form_data(pEneSq, pEneSq->nowFormType);
        struct_c5c000c9 *slot = (struct_c5c000c9 *)((char *)formData + 0x20 + 0x20 * e->sqmid);
        int near_;
        if (leader == NULL || slot == NULL) {
            near_ = 1;
        } else {
            float d2 = enesq_dist2(leader->mp->loc, e->mp->loc);
            near_ = d2 < slot->squadDist * slot->squadDist;
        }
        if (near_) {
            if (m->membFlag & 1) continue;
            EnemySqMembSt *mm = pEneSq->pSqMemb[i];
            if (!mm) continue;
            mm->membFlag |= 1;
            int logic = pEneSq->pEneSqData->member[i].logicID;
            enesq_set_logic(pEneSq, mm, i, logic);
        } else {
            if (!(m->membFlag & 1)) continue;
            EnemySqMembSt *mm = pEneSq->pSqMemb[i];
            if (!mm) continue;
            mm->membFlag &= ~1;
            struct_8bad5591 *data = pEneSq->pEneSqData;
            int logic = data->ctrl.sw.returnDefLogic ? mm->pEne->edat->logic_id : data->member[i].logicID;
            enesq_set_logic(pEneSq, mm, i, logic);
        }
    }
}

static StModel *__GetCenterMdlPointer(EnemySqSt *pEneSq, int centerType)
{
    switch (centerType) {
    case 0: return pEneSq->pSqMemb[0]->pEne->mp;
    case 1: return pEneSq->pSqMemb[0]->pEne->trg_mp;
    }
    return NULL;
}

/* op "MOVE POS": face the enemy, side-step when blocked, else move to the formation slot (dmytrg_loc) */
static void __EneopTraceForm(EnemySt *pEne, EneInfoSt *pInfo)
{
    PS2_FRAME(0x40);
    struct_f947d863 *formData = enesq_form_data(pEne->sqp, pEne->sqp->nowFormType);
    enepad_turn_to_ene(pEne, pInfo);
    if (pEne->op_wk[0] > 0) {
        pEne->op_wk[0]--;
        if (pEne->op_wk[1] > 0) pEne->new_pad |= 0x400;
        else if (pEne->op_wk[1] < 0) pEne->new_pad |= 0x800;
        pEne->new_pad |= 0x40;
    } else if (pEne->cvf < -0.1f) {
        if (!(pEne->cvl <= 0.0f)) {
            pEne->op_wk[0] = 0x50;
            pEne->op_wk[1] = 1;
        } else if (pEne->cvl < 0.0f) {
            pEne->op_wk[0] = 0x50;
            pEne->op_wk[1] = -1;
        }
    } else {
        enepad_loc_move(pEne, pEne->dmytrg_loc, formData->epsXZ);
        enepad_height_move(pEne, pEne->dmytrg_loc, formData->epsY);
    }
    enepad_value_walk_dash_fly(pEne, pInfo);
}

/* op score: how close the member is to its slot (XZ and Y within eps) */
static float __EnevlTraceForm(EnemySt *pEne)
{
    PS2_FRAME(0x50);
    EnemySqSt *pSq = pEne->sqp;
    struct_f947d863 *fd = enesq_form_data(pSq, pSq->nowFormType);
    if (fd) __GetCenterMdlPointer(pSq, fd->centerType);   /* result unused */
    struct_f947d863 *formData = enesq_form_data(pSq, pSq->nowFormType);
    float dx = pEne->dmytrg_loc[0] - pEne->mp->loc[0];
    float dy = pEne->dmytrg_loc[1] - pEne->mp->loc[1];
    float dz = pEne->dmytrg_loc[2] - pEne->mp->loc[2];
    float x2 = dx * dx, z2 = dz * dz;
    float dxz = ps2_sqrt(x2 + z2);
    float r = eval_far(dxz, dxz - formData->epsXZ, dxz + formData->epsXZ);
    if (dy < 0.0f) dy = -dy;
    return r + eval_far(dy, dy - formData->epsY, dy + formData->epsY);
}

/* the member's formation slot position (centre model + rotated offset) */
static void __EneSqGetPosition(EnemySt *pEne, float *pos)
{
    PS2_FRAME(0x70);
    float v[4] __attribute__((aligned(16)));
    EnemySqSt *pSq = pEne->sqp;
    int id = pSq->pSqMemb[(int)pEne->sqmid]->nowFormID;
    struct_f947d863 *formData = enesq_form_data(pSq, pSq->nowFormType);
    struct_c5c000c9 *slot = (struct_c5c000c9 *)((char *)formData + 0x20 + 0x20 * id);
    struct_f947d863 *fd = enesq_form_data(pSq, pSq->nowFormType);
    StModel *center = fd ? __GetCenterMdlPointer(pSq, fd->centerType) : NULL;
    float ang = add_angf(center->ang[1], ENESQ_DEG2RAD * (slot->angY - 180.0f));
    v[0] = fsmath_SinVU0(ang);
    v[1] = 0.0f;
    v[2] = fsmath_CosVU0(ang);
    v[3] = 1.0f;
    v[0] *= slot->distXZ;
    v[1] *= slot->distXZ;
    v[2] *= slot->distXZ;
    v[1] = v[1] - slot->distY;
    float w = center->loc[3];
    pos[0] = center->loc[0] + v[0];
    pos[1] = center->loc[1] + v[1];
    pos[2] = center->loc[2] + v[2];
    pos[3] = w;
}

static int __GetCannonID(EnemySt *pEne)
{
    for (int i = 0; i < 4; i++)
        if ((pEne->edat->wep_tab[i]._type & 0x3f) == 2) return i;
    return -1;
}

/* the member follows the leader's cannon choice and fires 0..29 frames after the leader */
static void __LeaderAtcFollowCtrl(EnemySt *pEne, EnemySqSt *pEneSq)
{
    PS2_FRAME(0x40);
    EnemySt *leader = pEne->sqp ? pEne->sqp->pSqMemb[0]->pEne : NULL;
    EnemySqMembSt *own = pEneSq->pSqMemb[(int)pEne->sqmid];
    if ((leader->edat->wep_tab[(int)leader->cur_wep]._type & 0x3f) == 2) {
        int c = __GetCannonID(pEne);
        if (c >= 0) {
            pEne->old_wep = pEne->cur_wep;
            pEne->cur_wep = (char)c;
        }
    }
    if (pEneSq->readerPad & 0x8000) {
        if (own->atcFrame < FRAME) own->atcFrame = FRAME + ((ps2_rand() % 0x78000) >> 14);
    }
    if (own->atcFrame == FRAME) pEne->new_pad |= 0x8000;
    else pEne->new_pad &= 0x7fff;
}

/* pilot level 0..4 drawn with the member's LV1..LV5 weights (squad / no-squad set by membFlag bit 0); 2 when all 0 */
static int __GetPilotLV(EnemySqSt *pEneSq, int memberID)
{
    PS2_FRAME(0x30);
    struct_11d5689b *mem = &pEneSq->pEneSqData->member[memberID];
    struct_2facadbd *w = (pEneSq->pSqMemb[memberID]->membFlag & 1) ? &mem->squadPilotLV : &mem->noSquadPilotLV;
    int lv1 = w->LV1, lv2 = w->LV2, lv3 = w->LV3, lv4 = w->LV4, lv5 = w->LV5;
    int total = lv5 + (lv4 + (lv3 + (lv1 + lv2)));
    if (total == 0) return 2;
    int r = 0;
    if (total > 0) r = (ps2_rand() % (total << 14)) >> 14;
    int s = lv1;
    if (r < s) return 0;
    s += lv2;
    if (r < s) return 1;
    s += lv3;
    if (r < s) return 2;
    s += lv4;
    if (r < s) return 3;
    return 4;
}

static void __SetSquadLogic(void)
{
    PS2_FRAME(0x90);
    for (int i = 0; i < 32; i++) {
        EnemySqSt *sq = s_EneSqMng.pEneSq[i];
        if (!sq) continue;
        struct_8bad5591 *data = sq->pEneSqData;
        for (int j = 0; j < sq->sqNum; j++) {
            int id = data->member[j].logicID;
            EnemySt *e = sq->pSqMemb[j]->pEne;
            if (!Enemy_GetLogicPointer(id)) Enemy_AddLogicData(id);
            e->logp = Enemy_GetLogicPointer(id);
            ene_set_think(e);
        }
    }
}

/* original: with all 8 slots taken it works on pSqMemb[8] (= the pEneSqData pointer) */
static void __AddEnemyForSquad(EnemySt *pEne, int sqID)
{
    PS2_FRAME(0x50);
    EnemySqSt *sq = s_EneSqMng.pEneSq[sqID];
    int i;
    for (i = 0; i < 8; i++) {
        if (sq->pSqMemb[i] == NULL) {
            sq->pSqMemb[i] = (EnemySqMembSt *)Memm_Alloc(1, 0x20, 0x10);
            break;
        }
    }
    EnemySqMembSt *m = sq->pSqMemb[i];
    memset(m, 0, 0x20);
    m->membFlag |= 1;
    m->pEne = pEne;
    m->nowFormID = (unsigned char)i;
    pEne->sqp = sq;
    pEne->sqmid = (char)i;
    sq->sqNum++;
}

static void __CreateEneSqMng(short *sqDataIDTbl)
{
    PS2_FRAME(0x50);
    memset(&s_EneSqMng, 0, 0x88);
    s_EneSqMng.pSqDataIDTbl = sqDataIDTbl;
    for (int i = 0; i < 32; i++) {
        int id = sqDataIDTbl[i];
        if (id > 0 && s_EneSqID2SqDataID[id] >= 0) {
            s_EneSqMng.pEneSq[i] = (EnemySqSt *)Memm_Alloc(1, 0x40, 0x10);
            EnemySqSt *p = s_EneSqMng.pEneSq[i];
            memset(p, 0, 0x40);
            p->sqID = (unsigned char)i;
            p->sqFlag |= 1;
            p->nowFormType = 0;
            p->pEneSqData = s_pEneSqData[(int)s_EneSqID2SqDataID[sqDataIDTbl[i]]];
            s_EneSqMng.nowStageSquadNum++;
        }
    }
}

/* reads file 0x7ce (a header + 0x600-byte records) and copies the records the table uses.
 * original: the record counter is a signed byte; > 16 distinct ids run past s_pEneSqData[16] */
static void __ReadSqData(short *sqDataIDTbl)
{
    PS2_FRAME(0x60);
    for (int i = 0; i < 128; i++) s_EneSqID2SqDataID[i] = -1;
    for (int i = 0; i < 16; i++) s_pEneSqData[i] = NULL;
    char *buf = g_pbufRead;
    File_Read(0x7ce, -1, buf, 1);
    struct_8bad5591 **pp = s_pEneSqData;
    signed char n = 0;
    for (int i = 0; i < 32; i++) {
        int id = sqDataIDTbl[i];
        if (id > 0 && s_EneSqID2SqDataID[id] < 0 && !(((int *)buf)[1] < id)) {
            *pp = (struct_8bad5591 *)Memm_Alloc(1, 0x600, 0x10);
            memcpy(*pp, buf + 0x10 + (sqDataIDTbl[i] - 1) * 0x600, 0x600);
            s_EneSqID2SqDataID[sqDataIDTbl[i]] = n;
            pp++;
            n = (signed char)(n + 1);
        }
    }
}

/* think function of a squad member */
void EneSq_Think(EnemySt *pEne, StModel *mp)
{
    PS2_FRAME(0xd0);
    PS2_STACK(EneInfoSt, info, 0x40);
    if (pEne->sqp->pEneSqData->ctrl.sw.leaderTgtFollow) {
        EnemySt *leader = pEne->sqp ? pEne->sqp->pSqMemb[0]->pEne : NULL;
        StModel *t = leader ? leader->trg_mp : NULL;
        if (t) pEne->trg_mp = t;
    }
    ene_get_eneinfo_basic(pEne, &info, pEne->trg_mp);
    __EneSqGetPosition(pEne, pEne->dmytrg_loc);
    ene_select_operation(pEne, &info, EneSq_Think__opTab, 2);
    e120_select_weapon(pEne, pEne->edat, &info);
    ene_attack(pEne, &info);
    ene_blade(pEne, &info);
    ene_execute_operation(pEne, mp, &info);   /* (ep, mp, eip): protofix, DWARF drops mp */
    EnemySqSt *sq = pEne->sqp;
    struct_8bad5591 *data = sq->pEneSqData;
    if (data->ctrl.sw.leaderAtcFollow) __LeaderAtcFollowCtrl(pEne, sq);
    if (data->ctrl.sw.atcRateCtrl && (pEne->new_pad & 0x8000)) {
        if (sq->nowAtcNum < data->ctrl.maxAtcNum) sq->nowAtcNum++;
        else pEne->new_pad &= 0x7fff;
    }
}

/* the squad is wiped out / disbanded: every living controlled member returns to its own logic */
void EneSq_SquadDestroy(int sqID)
{
    PS2_FRAME(0x70);
    EnemySqSt *sq = s_EneSqMng.pEneSq[sqID];
    sq->sqFlag &= ~1;
    for (int i = 0; i < sq->sqNum; i++) {
        EnemySqMembSt *m = sq->pSqMemb[i];
        if (m->pEne->use & 0x80) continue;
        if (!(m->membFlag & 1)) continue;
        if (!m) continue;
        m->membFlag &= ~1;
        struct_8bad5591 *data = sq->pEneSqData;
        int logic = data->ctrl.sw.returnDefLogic ? m->pEne->edat->logic_id : data->member[i].logicID;
        enesq_set_logic(sq, m, i, logic);
    }
}

int EneSq_IsSquadCtrlNum(int sqID)
{
    EnemySqSt *sq = s_EneSqMng.pEneSq[sqID];
    if (!(sq->sqFlag & 1)) return 0;
    int n = 0;
    for (int i = 0; i < sq->sqNum; i++) {
        EnemySqMembSt *m = sq->pSqMemb[i];
        if (!(m->pEne->use & 0x80) && (m->membFlag & 1)) n++;
    }
    return n;
}

void EneSq_Proc(void)
{
    PS2_FRAME(0x50);
    if (g_sortie.pwork->isPlayPolyg) return;
    for (int i = 0; i < 32; i++) {
        EnemySqSt *sq = s_EneSqMng.pEneSq[i];
        if (!sq || !sq->sqNum || !(sq->sqFlag & 1)) continue;
        struct_8bad5591 *data = sq->pEneSqData;
        sq->readerPad = sq->pSqMemb[0]->pEne->old_pad;
        __SelectSquadCtrl(sq);
        __SelectForm(sq);
        if (data->ctrl.sw.atcRateCtrl) {
            sq->nowAtcFrame++;
            if (!(sq->nowAtcFrame < data->ctrl.atcFrame)) {
                sq->nowAtcNum = 0;
                sq->nowAtcFrame = 0;
            }
        }
    }
}

/* original: a groupid > 0 without a squad manager (or > 31) is not checked */
void EneSq_BuildSquad(short *sqDataIDTbl)
{
    PS2_FRAME(0x30);
    __ReadSqData(sqDataIDTbl);
    __CreateEneSqMng(sqDataIDTbl);
    EnemySt *e = DFene_GetEnemy(0);
    for (int i = 0; i < EnemyNum; i++) {
        int sid = e->mp->groupid;
        if (sid > 0) __AddEnemyForSquad(e, sid);
        e = (EnemySt *)((char *)e + 0x200);
    }
    __SetSquadLogic();
}

#ifdef ACNB_SOURCEPORT
EnemyOpSt EneSq_Think__opTab[2] = {
    {(char *)"MOVE POS", NULL, (float (*)(EnemySt *, EneInfoSt *))__EnevlTraceForm, __EneopTraceForm, 60, 120, 1.0f, 0.0f,
     0.0f},
    {(char *)"TURN TO ENE", NULL, (float (*)(EnemySt *, EneInfoSt *))enevl_stay, eneop_stay, 0, 10, 1.0f, 0.0f, 0.0f},
};
#endif
