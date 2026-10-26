/* src/ac/acctrl.cpp — per-AC control: AcCtrl_Init / AcCtrl_BootupStage (__InitPlayer: param assembly, model tables,
 * team bits, lock manager, core parts), AcCtrl_Exec (one frame: pad -> view -> __ExecAc: actions, weapons, recoil,
 * floor/water damage, gravity, ECM, energy / heat, area check, anim, vibration, death), AcCtrl_HitAttack (shield
 * guard, heat, damage + kill bookkeeping), AcCtrl_UpdateMatrix (main model + attached booster / weapon / extension
 * matrices), plus the accessors / flags used by parts, HUD, COM and scripts, player control on / off, heat and
 * energy helpers, the AC message handler (incoming missiles), the per-frame anim / cockpit-warning steps, the
 * movement helpers of the action code (turn / accel / walk / brake / fall, action button) and view / gaze / carry /
 * radar / missile interception, shield guards, damage (AP, vibration, knock-back, damage actions) and the per-frame
 * collision (ac_hitcheck -> __ACHitControl: character / wall / floor / ceiling / water, landing) and the model /
 * effect init (attach matrices, boosters / blades / eye, tank-track corners). Whole file native. */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/nodwarf.h"
#include "acnb/vu0m.h"

/* __ExecAc's function statics without DWARF entries */
#define EXECAC_DOWNVEC     execac_down_vec        /* {0, -1, 0, 1}: floor / water damage direction */
#define EXECAC_FLARE_DSTAT (&execac_flare_dstat)   /* flare burn; .damage set per AC before use */

static inline int sortie_mode(void) { return g_sortie.pconstant->modeSortie; }

/* cvt.w.s of a float that MWCC converts to unsigned (values >= 2^31 go through the subtract-and-or path) */
static inline unsigned int ps2_ftou(float f)
{
    if (f >= 2147483648.0f) return (unsigned int)ps2_ftoi(f - 2147483648.0f) | 0x80000000u;
    return (unsigned int)ps2_ftoi(f);
}

/* Inlined heat helper: external cockpit heat, scaled by the AC's heat save and the mode's heat rate. */
static inline void add_cqheat(AcPlayerSt *acp, float heat)
{
    if (MyI.padoff != 1 && !(acp->stat & 0x4000)) {
        float rate = sortie_mode() < 2 ? g_asmparam.p1_heatrate : g_asmparam.vs_heatrate;
        acp->prvp->cqheatAddExternal = acp->prvp->cqheatAddExternal + heat * acp->param.default_heatsave * rate;
    }
}

void DF_PLAY_ResetCF(int acid, unsigned short cflag)
{
    AcPlayerSt *acp = &g_ac[acid];
    acp->mp->cflag &= 0xfff0;
    acp->mp->cflag |= cflag & 0xf;
    acp->lockmng.ulock_mask &= 0xfff0;
    acp->lockmng.ulock_mask |= cflag & 0xf;
}

void DF_PLAY_DecideParent(int player)
{
    PS2_FRAME(0x50);
    StModel *mp = &MyAC[player];
    float loc[4] __attribute__((aligned(16))), dest[4] __attribute__((aligned(16)));
    memcpy(loc, g_ac[player].loc, 16);
    loc[1] = loc[1] - 3.0f;
    DF_HIT_DecideParent(mp, loc, 100000.0f, mp->R, dest);
    if (!mp->nowparent) mp->nowparent = Map;
    mp->nowparentbk = 0xff;
    for (int i = 0; i < 8; i++) {
        StModel *sub = g_ac[player].modeltbl[i];
        if (sub) sub->nowparent = mp->nowparent;
    }
}

void DF_PLAY_InitLocation(AcPlayerSt *acp, float *loc, float angy)
{
    StModel *mp = acp->mp;
    memcpy(acp->loc, loc, 16);
    acp->loc[3] = 1.0f;
    acp->ang[2] = 0.0f;
    acp->ang[0] = 0.0f;
    acp->ang[1] = angy;
    memcpy(mp->loc, acp->loc, 16);
    memcpy(mp->ang, acp->ang, 16);
}

/* wrap into [-PI, PI] in 2PI steps (`!(a <= PI)` / `a < -PI`) */
static inline void acctrl_wrap_ang(float *a)
{
    const float PI = 3.14159274f, PI2 = 6.28318548f;   /* 0x40490fdb, 0x40c90fdb */
    if (!(*a <= PI)) {
        do *a = *a - PI2; while (!(*a <= PI));
    }
    if (*a < -PI) {
        do *a = *a + PI2; while (*a < -PI);
    }
}

/* the sub models follow the main model's parent / lights; then the effect state machine */
static void acctrl_copy_parent_models(AcPlayerSt *acp)
{
    for (int i = 0; i < 8; i++) {
        StModel *dst = acp->modeltbl[i];
        if (!dst) continue;
        GAcUtil_CopyParentModel(dst, acp->mp);
        dst->vuparam.plightnum = acp->mp->vuparam.plightnum;
        dst->plid[0] = acp->mp->plid[0];
        dst->plid[1] = acp->mp->plid[1];
        dst->plid[2] = acp->mp->plid[2];
        dst->plid[3] = acp->mp->plid[3];
    }
    if (acp->mp && !(acp->mp->use & 1)) acsfx_Control(acp, acp->effectstat, acp->prvp->effectstatPrev);
}

void AcCtrl_UpdateMatrixForPolyG(AcPlayerSt *acp)
{
    PS2_FRAME(0x60);
    if (acp->prvp->isPolyG) acctrl_copy_parent_models(acp);
}

/* main model from loc / ang, then the attached models (booster, eob, subleg, weapon parts, extensions) */
void AcCtrl_UpdateMatrix(AcPlayerSt *acp)
{
    PS2_FRAME(0x60);
    if (acp->prvp->isPolyG) return;
    memcpy(acp->mp->loc, acp->loc, 16);
    acctrl_wrap_ang(&acp->ang[0]);
    acctrl_wrap_ang(&acp->ang[1]);
    acctrl_wrap_ang(&acp->ang[2]);
    memcpy(acp->mp->ang, acp->ang, 16);
    AcAnim_UpdateAllObjMatrix(acp->mp);
    if (acp->managefunc) gcall(acp->managefunc, acp);
    AcCtrl_UpdateBoosterMatrix(acp);
    AcCtrl_UpdateObMatrix(acp);
    AcCtrl_UpdateSublegMatrix(acp);
    AcCtrl_UpdatePartsMatrix(acp, 6);
    AcCtrl_UpdatePartsMatrix(acp, 7);
    if (acp->param.partsid[10]) AcCtrl_UpdatePartsMatrix(acp, 1);
    if (acp->param.partsid[11]) AcCtrl_UpdatePartsMatrix(acp, 0);
    if (acp->param.partsid[9]) {
        AcCtrl_UpdateExtMatrix(acp, 2);
        AcCtrl_UpdateExtMatrix(acp, 3);
    }
    AcCtrl_UpdatePartsMatrix(acp, 4);
    AcCtrl_UpdatePartsMatrix(acp, 5);
    acctrl_copy_parent_models(acp);
}

/* model follows an object's world matrix */
static inline void acctrl_follow_obj(StModel *mp, ObjectSt *op)
{
    sceVu0CopyMatrix(mp->matrix2, *op->wrmatp);
    memcpy(mp->loc, mp->matrix2[3], 16);
    Anim_UpdateChildObjMatrix(mp->op, mp->matrix2);
}

void AcCtrl_UpdateSublegMatrix(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    if (acp->sublegmp) acctrl_follow_obj(acp->sublegmp, &acp->op[14]);
}

void AcCtrl_UpdateObMatrix(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    if (acp->eobmp) acctrl_follow_obj(acp->eobmp, &acp->op[3]);
}

void AcCtrl_UpdateBoosterMatrix(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    acctrl_follow_obj(acp->boostmp, &acp->op[3]);
}

/* VU0 footprint of the original's matrix idioms: later interpreted code reads these registers stale.
 * ACC after vmulax / vmadday / vmaddaz of m by v (the last row of a matrix product / apply) */
static inline void acctrl_acc_mirror(const float (*m)[4], const float *v)
{
    float acc[4];
    for (int i = 0; i < 4; i++) {
        float a = m[0][i] * v[0];
        a = a + m[1][i] * v[1];
        acc[i] = a + m[2][i] * v[2];
    }
    PS2_ACC_MIRROR(acc);
    (void)acc;
}

/* vf1-4 = a, vf22-25 = a * b, ACC (vu_mul_matrix as the original's inline asm leaves it) */
static inline void acctrl_mul_mirror(const float (*a)[4], const float (*b)[4], const float (*res)[4])
{
    for (int i = 0; i < 4; i++) {
        PS2_VF_MIRROR(1 + i, a[i]);
        PS2_VF_MIRROR(22 + i, res[i]);
    }
    acctrl_acc_mirror(a, b[3]);
}

#ifdef ACNB_SOURCEPORT
float AcCtrl_UpdateExtMatrix__xrot90mat[4][4] = {{1, 0, 0, 0}, {0, 0, 1, 0}, {0, -1, 0, 0}, {0, 0, 0, 1}};
#endif

/* extension: x axis = parent's x axis, z axis = the core's z axis (xz only) minus its x component, y = z cross x,
 * then rotated 90 degrees about x; position from the parent matrix (VU0 normalise / cross / matrix idioms) */
void AcCtrl_UpdateExtMatrix(AcPlayerSt *acp, int modelid)
{
    PS2_FRAME(0x30);
    StModel *mp = acp->modeltbl[modelid];
    if (!mp) return;
    ObjCMatSt *ocm = &acp->ocmats[modelid];
    if (!ocm->parentmatp) return;
    float (*m)[4] = mp->matrix2;
    PS2_STACK(float[4], t, 0x10);    /* the temporaries live on the stack (stale reads elsewhere) */
    PS2_STACK(float[4], zv, 0x20);
    const float *p0 = (*ocm->parentmatp)[0];
    float q = vu_rcp_len3(p0);
    m[0][0] = p0[0] * q; m[0][1] = p0[1] * q; m[0][2] = p0[2] * q; m[0][3] = 0.0f;
    zv[0] = (*acp->op[3].wrmatp)[2][0]; zv[1] = 0.0f;
    zv[2] = (*acp->op[3].wrmatp)[2][2]; zv[3] = 0.0f;
    float d = zv[0] * m[0][0] + zv[1] * m[0][1];
    d = zv[2] * m[0][2] + d;
    t[0] = m[0][0] * d; t[1] = m[0][1] * d; t[2] = m[0][2] * d; t[3] = m[0][3];   /* vmulx.xyz */
    m[2][0] = zv[0] - t[0];   /* vsub.xyz; w = zv.w */
    m[2][1] = zv[1] - t[1];
    m[2][2] = zv[2] - t[2];
    m[2][3] = zv[3];
    q = vu_rcp_len3(m[2]);
    m[2][0] = m[2][0] * q; m[2][1] = m[2][1] * q; m[2][2] = m[2][2] * q; m[2][3] = 0.0f;
    vu_cross(m[1], m[2], m[0]);
    m[1][3] = m[2][3];
    float vf31[4] = {d, ps2_u2f(ps2_f2u(d) & 0x80000000u ? 0xffffffffu : 0u), PS2_STALE_VF(31, 2), PS2_STALE_VF(31, 3)};
    float pre[4][4];   /* rows before the xrot90 product (vf17 = row 0) */
    memcpy(pre, m, sizeof pre);
    vu_mul_matrix(m, (const float (*)[4])m, (const float (*)[4])AcCtrl_UpdateExtMatrix__xrot90mat);
    float xr[4][4];
    memcpy(xr, m, sizeof xr);
    vu_apply(m[3], (const float (*)[4])*ocm->parentmatp, ocm->mat[3]);
    /* VU0 footprint (read stale later): vf1-4 parent, vf16 = new row 3, vf17 = row 0, vf18 = normalised z,
     * vf22-25 = xrot90 product, vf31 = d (qmtc2 of the sign-extended GPR), Q, ACC of the last apply */
    for (int i = 0; i < 4; i++) {
        PS2_VF_MIRROR(1 + i, (*ocm->parentmatp)[i]);
        PS2_VF_MIRROR(22 + i, xr[i]);
    }
    float vf18[4] = {m[2][0], m[2][1], m[2][2], 0.0f};
    PS2_VF_MIRROR(16, m[3]);
    PS2_VF_MIRROR(17, pre[0]);
    PS2_VF_MIRROR(18, vf18);
    PS2_VF_MIRROR(31, vf31);
    PS2_Q_MIRROR(q);
    acctrl_acc_mirror((const float (*)[4])*ocm->parentmatp, ocm->mat[3]);
    (void)vf18; (void)vf31; (void)pre; (void)xr;
    memcpy(mp->loc, m[3], 16);
    Anim_UpdateChildObjMatrix(mp->op, m);
}

/* parts model = parent matrix * local matrix, x row scaled by scale.x */
void AcCtrl_UpdatePartsMatrix(AcPlayerSt *acp, int modelid)
{
    PS2_FRAME(0x10);
    StModel *mp = acp->modeltbl[modelid];
    if (!mp) return;
    ObjCMatSt *ocm = &acp->ocmats[modelid];
    if (!ocm->parentmatp) return;
    vu_mul_matrix(mp->matrix2, (const float (*)[4])*ocm->parentmatp, (const float (*)[4])ocm->mat);
    acctrl_mul_mirror((const float (*)[4])*ocm->parentmatp, (const float (*)[4])ocm->mat, (const float (*)[4])mp->matrix2);
    mp->matrix2[0][0] = mp->matrix2[0][0] * mp->scale[0];
    mp->matrix2[0][1] = mp->matrix2[0][1] * mp->scale[0];
    mp->matrix2[0][2] = mp->matrix2[0][2] * mp->scale[0];
    mp->matrix2[0][3] = mp->matrix2[0][3] * mp->scale[0];
    memcpy(mp->loc, mp->matrix2[3], 16);
    Anim_UpdateChildObjMatrix(mp->op, mp->matrix2);
}

int in_region(float *loc, float (*minmax)[4])
{
    if (loc[0] < minmax[1][0] || minmax[0][0] < loc[0] || loc[1] < minmax[1][1] || minmax[0][1] < loc[1] ||
        loc[2] < minmax[1][2] || minmax[0][2] < loc[2])
        return 0;
    return 1;
}

static void __UpdateTemperature(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    if (acp->fcntBoost > 0) {
        if (acp->fcntBoost < 4) add_cqheat(acp, acp->param.cqheatBoost * g_asmparam.bstrpenalty_heatrate);
        else add_cqheat(acp, acp->param.cqheatBoost);
    }
    AcCtrlPrivateSt *prv = acp->prvp;
    float outer = g_asmparam.outtmprt_to_cqheat * (acp->mp->airtemp - acp->temperature);
    float heat = outer + prv->cqheatAccumulate + acp->param.cqheatFixed + prv->cqheatAddExternal;
    if ((acp->stat & 0x8000) && !(acp->stat & 4)) heat = heat - acp->param.cqheatQuickCooldown;
    else heat = heat - acp->param.cqheatCooldown;
    heat = heat - acp->cqheatExCooldown;
    float add = heat * g_asmparam.addcqheat_per_frame;
    acp->temperature = acp->temperature + add * g_asmparam.cqheat_to_tmprt;
    if (acp->temperature < g_asmparam.min_actmprt) acp->temperature = g_asmparam.min_actmprt;
    float mintmp = g_asmparam.min_actmprt;
    heat = heat - outer * (1.0f - g_asmparam.addcqheat_per_frame);
    heat = heat - add;
    prv->cqheatAccumulate = heat <= 0.0f ? 0.0f : heat;
    prv->cqheatAddExternal = 0.0f;
    if (acp->stat & 0x8000) {
        if (acp == &g_ac[0]) TrainingTrendIncreaseOverheat();
        acp->syserr |= 0x20;
        unsigned short limit = acp->param.tmprtLimit;
        if (!(acp->temperature <= (float)limit)) {
            __UpdateTemperature__dstat.damage = (unsigned short)ps2_ftou(g_asmparam.qcool_damage_ratio *
                                                                         (acp->temperature - (float)limit));
            AC_add_damage(acp, &__UpdateTemperature__dstat, zerovec, acp->loc, __UpdateTemperature__dstat.damage);
        }
        if (acp->temperature <= 0.5f * (float)acp->param.tmprtLimit) acp->stat &= 0x7fff;
    } else if (!(acp->temperature <= (float)acp->param.tmprtLimit)) {
        acp->stat |= 0x8000;
    } else if (!(acp->temperature <= (float)acp->param.tmprtWarning)) {
        acp->syserr |= 0x10;
    }
    if (acp->dead && sortie_mode() == 7) acp->temperature = mintmp;
}

static void __UpdateEnergy(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    int sub = 0, add;
    if (acp->actno == 0x12) {
        sub = acp->param.oboost_sub;
        if (AcUidev_IsPadOnConf(acp->frdp->uidevp, 5))
            sub = ps2_ftoi((float)sub + 0.5f * (float)acp->param.boost_sub);
    } else if (acp->fcntBoost > 0) {
        sub = acp->param.boost_sub;
        if (acp->actno == 5) sub = ps2_ftoi((float)sub * 0.5f);
        if (acp->fcntBoost < 4) sub = ps2_ftoi((float)sub * g_asmparam.bstrpenalty_enerate);
    }
    if (!sub) add = __CalcEnergyAdd(acp);
    else if (acp->stat & 4) add = __CalcEnergyAdd(acp);
    else add = 0;
    unsigned short stat = acp->stat;
    if (stat & 0x8000) {
        sub += acp->param.drainQuickCool;
        add = 0;
    } else sub += acp->param.drainCool;
    unsigned char core = acp->param.core_type;
    if (core == 2 && acp->prvp->isCallSlow) sub += acp->param.oboost_sub;
    if (core == 3 && acp->prvp->isGazeLock) sub += acp->param.oboost_sub;
    add += acp->param.exboost_add;
    if (acp->shieldstat == 1) sub += acp->param.shield_sub;
    if (acp->spflag & 4) sub = 0;
    else if (acp->spflag & 8) sub = sub / 2;
    if (stat & 4) {
        acp->energyRedF += add / 2;
        if (acp->energyRedF >= acp->param.boost_max) {
            acp->energyReal = acp->energyRealMax;
            acp->stat &= 0xfffb;
        }
        return;
    }
    acp->energyReal += add;
    if (acp->energyReal >= acp->energyRealMax) acp->energyReal = acp->energyRealMax;
    acp->energyReal -= sub;
    if (acp->energyReal < 0) {
        acp->energyReal = 0;
        acp->stat |= 4;
        if (acp == &g_ac[0]) TrainingTrendIncreaseCharge();
        Info_IncData(1, 9, acp->id, 0);
    }
    int e = acp->energyReal;
    if (acp->energyRedzone < e) acp->energyRedF = acp->param.boost_redarea + (e - acp->energyRedzone);
    else acp->energyRedF = e / 4;
}

static void __ExecAc(AcPlayerSt *acp)
{
    PS2_FRAME(0x40);
    AcCtrlPrivateSt *prv = acp->prvp;
    prv->effectstatPrev = acp->effectstat;
    acp->effectstat = prv->effectstatNext;
    prv->effectstatNext = 0;
    acp->hover_effectlv = 0;
    acp->soundstat = 0;
    acp->syserr = 0;
    acp->exestat = 0;
    AcAct_Exec(acp);
    if (MyI.padoff != 1) AcWep_SelectWeapon(acp);
    AcWep_Exec(acp);
    AcLgun_Control(AcInst_GetPartsLgun(acp));
    if (acp->shieldstat == 1) {
        if (acp->hp <= 0 || ((acp->stat & 4) && acp->param.shield_sub)) ACsetact_shieldoff(acp);
        else add_cqheat(acp, acp->param.cqheatShield);
    }
    if (acp->param.extp) acext_Control(AcInst_GetPartsExt(acp));
    if (prv->ctrlCore.func) gcall(prv->ctrlCore.func, prv->ctrlCore.pparts);

    /* recoil recovery */
    float step = 0.0f;
    int rt = acp->param.recoil_timecnt;
    if (rt >= 0) {
        acp->param.recoil_timecnt = rt - 1;
        float span = acp->param.recoil_size - acp->param.gap_angle_by_recoil_min;
        if (rt < acp->param.recoilctrl_time / 2) step = ps2_div(0.5f * span, (float)acp->param.recoilctrl_time);
        else step = ps2_div(1.5f * span, (float)acp->param.recoilctrl_time);
    }
    if (!(acp->param.gap_angle_by_recoil_l <= acp->param.gap_angle_by_recoil_min)) {
        acp->param.gap_angle_by_recoil_l = acp->param.gap_angle_by_recoil_l - step;
        if (acp->param.gap_angle_by_recoil_l < acp->param.gap_angle_by_recoil_min)
            acp->param.gap_angle_by_recoil_l = acp->param.gap_angle_by_recoil_min;
    }
    if (!(acp->param.gap_angle_by_recoil_r <= acp->param.gap_angle_by_recoil_min)) {
        acp->param.gap_angle_by_recoil_r = acp->param.gap_angle_by_recoil_r - step;
        if (acp->param.gap_angle_by_recoil_r < acp->param.gap_angle_by_recoil_min)
            acp->param.gap_angle_by_recoil_r = acp->param.gap_angle_by_recoil_min;
    }
    if (MyI.padoff != 1) ac_vsmissile_proc(acp);
    if (acp->hp <= 0) {
        unsigned short a = acp->actno;
        if (a == 10 || a == 9 || a == 0x10 || a == 2 || a == 0) ACsetact_dead(acp);
    }
    ac_hitcheck(acp);

    /* floor / water attribute damage */
    MapAttributeInfoSt *ai = mapatrb_GetInfo(acp->mp->yukaattribute);
    if (ai && ai->dstat.damage) {
        add_cqheat(acp, (float)ai->dstat.heat);
        AC_add_damage(acp, &ai->dstat, EXECAC_DOWNVEC, acp->loc, ai->dstat.damage);
    }
    if (!(acp->loc[1] - 0.1f <= acp->water_high)) {
        ai = mapatrb_GetInfo(acp->mp->mizuattribute);
        if (ai && ai->dstat.damage) {
            add_cqheat(acp, (float)ai->dstat.heat);
            AC_add_damage(acp, &ai->dstat, EXECAC_DOWNVEC, acp->loc, ai->dstat.damage);
        }
    }
    if (acp->water_high < 0.001f + acp->loc[1] && !(acp->water_high <= acp->loc[1] - acp->tall))
        acp->effectstat |= 0x400;

    /* gravity / fall speed limit */
    unsigned short a = acp->actno;
    if (a != 8 && a != 0xd && a != 0xf && a != 0x12 && a != 7) {
        float maxspd = acp->param.fall_maxspd, g = acp->param.gravity, vy = acp->velocity[1];
        float nv = vy + g, v;
        if (vy <= 0.001f + maxspd) v = nv <= maxspd ? nv : maxspd;
        else {
            v = vy - g;
            if (v < maxspd) v = maxspd;
        }
        acp->velocity[1] = nv * ps2_div(v, nv);
    }

    /* ECM */
    acp->ecmnoise = EcmField_GetEcmNoiseLv(g_sortie.pconstant->player[acp->id].belonging);
    int noise = acp->ecmnoise - acp->param.total_eccm;
    noise = noise > 0 ? noise & 0xffff : 0;
    acp->rateEcmError = ps2_div((float)noise, g_asmparam.ecmerr_lvrate);
    if (!(acp->rateEcmError <= 1.0f)) acp->rateEcmError = 1.0f;
    acp->lockmng.fcs_err_time = 0;
    if (!(acp->rateEcmError <= 0.0f)) {
        acp->syserr |= 2;
        acp->lockmng.fcs_err_time = (unsigned short)ps2_ftou(g_asmparam.ecmerr_fcs_delay * acp->rateEcmError);
        if (acp->nolock_time == 0) {
            float vanish = g_asmparam.ecmerr_fcs_vanishrate * acp->rateEcmError;
            if (!(vanish <= ps2_div((float)ps2_rand(), 2147483648.0f)))
                acp->nolock_time = (short)ps2_ftoi(acp->rateEcmError * g_asmparam.ecmerr_fcs_vanishtime);
        }
    }
    __UpdateEnergy(acp);
    __UpdateTemperature(acp);
    __UpdateRadar(acp);
    if (acp->unlocked_time > 0) acp->unlocked_time--;
    if (acp->genedown_time > 0) acp->genedown_time--;
    if (acp->param.radar_stat & 0x20) acp->nolock_time = 0;
    if (acp->nolock_time > 0) {
        acp->syserr |= 8;
        if (!(acp->lockmng.flag & 4)) {
            acp->lockmng.flag |= 4;
            Lock_ClearMng(&acp->lockmng, 0);
            Lock_ClearMng(&acp->lockmng, 1);
        }
        acp->nolock_time--;
    } else acp->lockmng.flag &= 0xfffb;
    if (acp->flare_time > 0) {
        EXECAC_FLARE_DSTAT->damage = acp->flare_damage;
        add_cqheat(acp, g_asmparam.flareheat);
        AC_add_damage(acp, EXECAC_FLARE_DSTAT, zerovec, acp->loc, EXECAC_FLARE_DSTAT->damage);
        acp->flare_time--;
    }

    /* stage area: 0 inside, 1 warning, 2 out */
    if (!g_sortie.pwork->isPlayPolyg) {
        if (in_region(acp->loc, CopyOfSysInfo.rgnalert)) acp->areastat = 0;
        else if (in_region(acp->loc, CopyOfSysInfo.rgnover)) acp->areastat = 1;
        else acp->areastat = 2;
        if (acp->areastat == 2) {
            if (acp->id == 0 && !MyI.padoff && !acp->areaovered) EVMNG_AllDelete();
            acp->areaovered = 1;
            if (acp->actno == 0x12) acob_Close(AcInst_GetPartsOb(acp));
        }
    }
    if (acp->stifftime > 0) acp->stifftime--;
    else __ExecAcAnim(acp);
    acse_CtrlSe(acp);

    /* vibration only for a pad that views this AC (user players) */
    int nview = ViewNum > 2 ? 2 : ViewNum, view = -1;
    for (int i = 0; i < nview; i++)
        if (MyI.myplayerno[i] == acp->id) { view = i; break; }
    if (MyI.replaymode || MyI.mov_play || view < 0 || !g_gamedata[view].option.vibration ||
        g_ac[acp->id].areaovered)
        Vibra_SetPower(&prv->vibramng, 0, 0);
    Vibra_Update(&prv->vibramng);

    if (acp->dying_cnt && ++acp->dying_cnt >= 0x96) {
        acp->dead = 1;
        unsigned int m = sortie_mode();
        if (m - 4 < 2 || ((m == 0 || m == 1) && acp->id >= NowUserPlayerNum && !prv->dead_pause)) {
            acp->mp->use |= 1;
            acp->mp->use |= 2;
        }
        if (sortie_mode() == 7) {
            if (!acp->wep_release) AcWep_ReleaseWeapon(acp, acp->equip_weapon);
            ACsetact_shieldoff(acp);
            Lock_ClearMng(&acp->lockmng, 0);
            Lock_ClearMng(&acp->lockmng, 1);
            acp->mp->use |= 1;
            acp->mp->use |= 2;
            acp->stat &= 0xff8c;
            acp->syserr &= 0xff00;
        }
    }
}

void AcCtrl_Exec(int acid, StCompactPadData *ppadd)
{
    PS2_FRAME(0x40);
    AcPlayerSt *acp = &g_ac[acid];
    __CarryAc(acp);
    memcpy(acp->oloc, acp->loc, 16);
    memcpy(acp->oang, acp->ang, 16);
    AcUidev_Update(acp->frdp->uidevp, ppadd);
    DF_PTEMP_CalcAirTemp(acp->mp);
    __GazeLock(acp);
    __ExecViewAng(acp);
    acblade_UpdateTime(AcInst_GetPartsBlade(acp));
    __ExecAc(acp);
    AcPlayerParamSt *pp = &acp->param;
    if (pp->radar_range < 0.001f) acp->syserr &= 0xfffb;
    if (pp->legsp->max_wp < pp->real_total_weight - pp->legsp->basic.wp) acp->syserr |= 0x100;
    if (pp->corep->max_wp < pp->core_weight) acp->syserr |= 0x400;
    if (pp->genep->power < pp->total_ep + pp->radip->basic.ep) acp->syserr |= 0x200;
    if (acp->syserr) acp->syserr |= 1;
    if (acp->unlocked_time > 0) acp->mp->cflag |= 0x1000;
    else acp->mp->cflag &= 0xefff;
    __UpdateSelfComMessage(acp);
    /* speed meter: min(horizontal velocity, actual horizontal move), smoothed */
    float dx = acp->loc[0] - acp->oloc[0], dz = acp->loc[2] - acp->oloc[2];
    float moved = ps2_sqrt(dx * dx + dz * dz);
    float vel = ps2_sqrt(acp->velocity[0] * acp->velocity[0] + acp->velocity[2] * acp->velocity[2]);
    float spd = vel <= moved ? vel : moved;
    acp->spdmeter_speed = acp->spdmeter_speed + 0.1f * (spd - acp->spdmeter_speed);
}

static void __InitPlayer(int acid)
{
    PS2_FRAME(0xb0);
    AcPlayerSt *acp = &g_ac[acid];
    AcCtrlPrivateSt *prv = acp->prvp;
    AcPartsObSt *ob = AcInst_GetPartsOb(acp);
    AcPartsEoSt *eo = AcInst_GetPartsEo(acp);
    AcPartsGazeSt *gaze = AcInst_GetPartsGaze(acp);
    AcInst_GetPartsSlow(acp);
    AcPartsOpcSt *opc = AcInst_GetPartsOpc(acp);
    AcPartsDataSt *pdata = AcInst_GetAllPartsParam(acp);
    AcAssemblyPartsSt *asmbly = Sortie_GetAcAssembly(acid);
    MyOptionSt *opt = Sortie_GetAcOption(acid);
    int anim = AcLoader_GetAnimIdFromCoreArmLeg(pdata);
    acsfx_InitMapMagicTable();
    GAcTune_TuneAll(pdata, &asmbly->tune);
    AcAsm_Interface(&acp->param, asmbly, pdata, opt, anim);
    AcUidev_SetKeyConfig(acp->frdp->uidevp, &acp->param.pacopt->keyconf);
    AcUidev_ClearKeybuff(acp->frdp->uidevp);
    AcUidev_SetKeyDeray(acp->frdp->uidevp, 3);
    acp->dead = 0;
    acp->nextact = 0;
    acp->mp = &MyAC[acid];
    acp->boostmp = &MyBST[acid];
    acp->eobmp = acp->param.corep->func_type < 2 ? &MyEOB[acid] : 0;
    acp->inside_mp = acp->param.partsid[8] ? &MyINS[acid] : 0;
    acp->modeltbl[0] = &MyLBW[acid];
    acp->modeltbl[1] = &MyRBW[acid];
    acp->modeltbl[2] = &MyLEX[acid];
    acp->modeltbl[3] = &MyREX[acid];
    acp->modeltbl[4] = &MyLHW[acid];
    acp->modeltbl[5] = &MyRHW[acid];
    acp->modeltbl[6] = &MyLCV[acid];
    acp->modeltbl[7] = &MyRCV[acid];
    acp->sublegmp = (acp->param.prog_no == 1 || acp->param.prog_no == 3) ? &MySLG[acid] : 0;
    InitAcModel(acp);

    StModel *mp = acp->mp;
    mp->arg0p = acp;
    mp->msgfunc = (decltype(mp->msgfunc))GFN(AC_msgfunc);
    /* team bits: user players get their own bit; mission / NPC ACs take the stage array's flags */
    if (acid < NowUserPlayerNum) mp->cflag |= (unsigned short)(1 << (acid & 31));
    else mp->cflag |= Stage_GetArrayPointer(3, Array->head.cnum + acid - NowUserPlayerNum)->cflag;
    if (acp->param.abl_jammer) mp->cflag |= 0x800;
    if (acp->param.abl_stealth) mp->cflag |= 0x400;
    Lock_InitMng(&acp->lockmng);
    Lock_SetDeflectionDb(&acp->lockmng, &acp->lock_defldb);
    Lock_ClearMng(&acp->lockmng, 0);
    Lock_ClearMng(&acp->lockmng, 1);
    acp->lockmng.type = 2;
    acp->lockmng.id = acp->id;
    if (!(acp->param.head_addon & 4)) acp->lockmng.ulock_mask |= 0x200;
    if (acp->param.plus & 4) acp->lockmng.ulock_mask &= 0xfdff;
    acp->lockmng.ulock_mask |= mp->cflag & 7;
    acp->lockmng.paraeffic = acp->param.fcs_paraeffic;
    float range2 = acp->param.vs_mgun_range2J < acp->param.exvs_mgun_range2J ? acp->param.exvs_mgun_range2J
                                                                           : acp->param.vs_mgun_range2J;
    mp->vs_range = range2;
    mp->vs_range = ps2_sqrt(range2);
    acse_Init(acp);
    acp->nolock_time = 0;
    acp->noradar_time = 0;
    prv->radar_nodirtime = 0;

    int padno;
    switch (sortie_mode()) {
    case 0: case 1: case 2: case 3: case 6:
        padno = acid != 0 ? -1 : 0;
        break;
    case 4: case 5:
        if (!g_sortie.pconstant->isSplit) padno = acid != MyI.myplayerno[0] ? -1 : 0;
        else if (acid == MyI.myplayerno[0]) padno = 0;
        else if (acid == MyI.myplayerno[1]) padno = 1;
        else padno = -1;
        break;
    default:
        padno = -1;
    }
    Vibra_Init(&prv->vibramng, padno);
    acp->view.mode = 0;
    acp->view.fixtime = 0;
    acp->view.ang[0] = 0.0f; acp->view.ang[1] = 0.0f; acp->view.ang[2] = 0.0f; acp->view.ang[3] = 1.0f;
    acp->view.angspd[0] = 0.0f; acp->view.angspd[1] = 0.0f; acp->view.angspd[2] = 0.0f; acp->view.angspd[3] = 1.0f;
    acp->radar_range = acp->param.radar_range;
    prv->radar_dirdisp = !(acp->radar_range <= 0.0f);
    acp->mislink_func = 0;
    acp->misalert_func = 0;
    acp->funcOrbLink = 0;
    acp->funcCounter = 0;
    AcAct_Init(acp->frdp->actp, acp);
    AcWep_Init(acp->frdp->wepp);
    acblade_Init(AcInst_GetPartsBlade(acp), acp);
    /* the core's special part (overed boost, extension EO, gaze, orbit cannon) runs every frame from __ExecAc */
    switch (acp->param.core_type) {
    case 5:
        prv->ctrlCore.func = 0;
        prv->ctrlCore.pparts = 0;
        break;
    case 4:
        AcOpc_Init(opc, acp);
        prv->ctrlCore.func = (decltype(prv->ctrlCore.func))GFN(AcOpc_Control);
        prv->ctrlCore.pparts = opc;
        break;
    case 3:
        AcGaze_Init(gaze, acp);
        prv->ctrlCore.func = (decltype(prv->ctrlCore.func))GFN(AcGaze_Control);
        prv->ctrlCore.pparts = gaze;
        break;
    case 0:
        aceo_Init(eo, acp);
        prv->ctrlCore.func = (decltype(prv->ctrlCore.func))GFN(aceo_Control);
        prv->ctrlCore.pparts = eo;
        break;
    case 1:
        acob_Init(ob, acp);
        prv->ctrlCore.func = (decltype(prv->ctrlCore.func))GFN(acob_Control);
        prv->ctrlCore.pparts = ob;
        break;
    }
    acp->stat = 0;
    acp->effectstat = 0;
    acp->areastat = 0;
    acp->shieldstat = 0;
    acp->dying_cnt = 0;
    acp->spflag = 0;
    acext_Init(AcInst_GetPartsExt(acp), acp->param.extp, acp);
    acp->defence = acp->param.normal_def;
    acp->hp = acp->param.hp;
    acp->semng_objid = (unsigned char)(mp->nobj - 1);
    acp->energyRedF = acp->param.boost_max;
    acp->energyRedzone = acp->param.boost_redarea * 4;
    acp->energyRealMax = acp->energyRedzone + (acp->param.boost_max - acp->param.boost_redarea);
    acp->energyReal = acp->energyRealMax;
    AcWep_InitAllWeapons(acp);
    AcPtsDstry_Init(acp->prvp->ptsdstryp, acp);
    acp->loc[0] = 0.0f; acp->loc[1] = -300.0f; acp->loc[2] = 0.0f; acp->loc[3] = 1.0f;
    acp->ang[0] = 0.0f; acp->ang[1] = 0.0f; acp->ang[2] = 0.0f; acp->ang[3] = 1.0f;
    prv->old_parent = 0;
    prv->world_velocity[0] = 0.0f; prv->world_velocity[1] = 0.0f; prv->world_velocity[2] = 0.0f;
    prv->world_velocity[3] = 1.0f;
    prv->parent_movevec[0] = 0.0f; prv->parent_movevec[1] = 0.0f; prv->parent_movevec[2] = 0.0f;
    prv->parent_movevec[3] = 1.0f;
    acp->parent_oldpos[0] = 0.0f; acp->parent_oldpos[1] = 0.0f; acp->parent_oldpos[2] = 0.0f;
    acp->parent_oldpos[3] = 1.0f;
    prv->parent_oldang[0] = 0.0f; prv->parent_oldang[1] = 0.0f; prv->parent_oldang[2] = 0.0f;
    prv->parent_oldang[3] = 1.0f;
    sceVu0CopyMatrix(prv->parent_oldmat, IdentityMtx);
    acp->R = 7.0f;
    mp->R = 7.0f;
    Anim_SetAnim(mp, 0, 0);
    Anim_Update(mp);
    AcAnim_UpdateAllObjMatrix(mp);
    float tall = acp->param.tall + 1.5f;
    acp->mp->tall = tall;
    acp->tall = tall;
    acp->cam_height = tall - 1.6f;
    prv->climb_high = 2.5f;
    acp->temperature = 0.0f;
    acp->gravity = acp->param.gravity;
    acp->water_high = ps2_u2f(0x7f7fffffu);
    acp->velocity[2] = 0.0f;
    acp->velocity[1] = 0.0f;
    acp->velocity[0] = 0.0f;
    acp->key_enable = 1;
    acp->tank_norm[0] = 0.0f; acp->tank_norm[1] = 1.0f; acp->tank_norm[2] = 0.0f; acp->tank_norm[3] = 1.0f;
    /* back weapons of type 1 (unfolding cannons) start folded */
    for (int side = 0; side < 2; side++) {
        PartsParamBackSt *back = side ? acp->param.Rbackp : acp->param.Lbackp;
        StModel *bm = acp->modeltbl[side];
        if (!back || back->type != 1) continue;
        bm->loc[0] = 0.0f; bm->loc[1] = 0.0f; bm->loc[2] = 0.0f; bm->loc[3] = 1.0f;
        bm->ang[0] = 0.0f; bm->ang[1] = 0.0f; bm->ang[2] = 0.0f; bm->ang[3] = 1.0f;
        for (int i = 0; i < bm->nobj; i++) {
            bm->op[i].group = 1;
            bm->op[i].todid = 0xff;
        }
        Anim_SetAnim(bm, 0, 1);
    }
    InitAcSfx(acp);
    acse_PutLoopSeVolToneDist(acp, 0, acp->param.typedatap->motor_seinfo.id, 0, 0, 2500.0f);
    acse_PutLoopSeVolToneDist(acp, 5, acp->param.typedatap->turn_seinfo.id, 0, 0, 2500.0f);
    acse_PutLoopSeVolToneDist(acp, 1, 3, 0, 0, 2500.0f);
    AC_SetAction(acp, 0);
    AcAct_Exec(acp);
}

void AcCtrl_BootupStage(int acid)
{
    PS2_FRAME(0x70);
    __InitPlayer(acid);
    AC_SetControl(&g_ac[acid], 1);
    StModel *models[15] = {
        &MyAC[acid], &MyRCV[acid], &MyLCV[acid], &MyBST[acid],
        &MyEOB[acid], &MySLG[acid], &MyRHW[acid],
        &MyLHW[acid], &MyRBW[acid], &MyLBW[acid],
        &MyREX[acid], &MyLEX[acid], &MyINS[acid],
        &MyRSW[acid], &MyLSW[acid],
    };
    for (int i = 0; i < 15; i++)
        if (models[i] && models[i]->nobj) Model_DeleteDiscernVertex(models[i]);
}

int AcCtrl_Init(int acid)
{
    PS2_FRAME(0x40);
    AcInst_Init(acid);
    AcPlayerSt *acp = &g_ac[acid];
    AcCtrlPrivateSt *prv = acp->prvp;
    acp->frdp = &prv->frd;
    acp->frdp->effp = prv->effp;
    acp->frdp->uidevp = prv->uidevp;
    acp->frdp->sep = prv->sep;
    acp->frdp->sfxp = prv->sfxp;
    acp->frdp->actp = prv->actp;
    acp->frdp->wepp = prv->wepp;
    acp->frdp->ptsresp = prv->ptsresp;
    aceff_Init(acp->frdp->effp, acp);
    AcUidev_Init(acp->frdp->uidevp, acp);
    AcPtsRes_Init(prv->ptsresp);
    acp->param.ptsresp = prv->ptsresp;
    acp->id = acid;
    return 1;
}

/* Where a hit position lies seen from the AC's body centre: |pitch| and |yaw relative to the AC's facing| (the
 * shield guard tests; MWCC inlined this in HitAttack and DamageReaction). */
static void acctrl_hit_angle(AcPlayerSt *acp, const float *pos, float *ax, float *ay)
{
    float vec[4] __attribute__((aligned(16))), ang[4] __attribute__((aligned(16)));
    vec[0] = pos[0] - acp->loc[0];
    vec[1] = pos[1] - acp->loc[1];
    vec[2] = pos[2] - acp->loc[2];
    vec[3] = pos[3];
    vec[1] = vec[1] + 0.5f * acp->tall;
    DF_CALC_Vector2Angle(ang, vec);
    ang[1] = Add_Angle(ang[1], 3.1415927f - acp->ang[1]);
    *ax = ang[0] < 0.0f ? -ang[0] : ang[0];
    *ay = ang[1] < 0.0f ? -ang[1] : ang[1];
}

/* the arm shield (shieldstat 1) covers the hit position */
static int acctrl_shield_guard(AcPlayerSt *acp, const float *pos)
{
    float ax, ay;
    acctrl_hit_angle(acp, pos, &ax, &ay);
    return ax <= acp->param.shield_angx && ay <= acp->param.shield_angy;
}

/* Hit by a shot / blade / map damage. stype: 0 none, 1 enemy (sid = enemy id), 2 AC (sid = attacker's AC id). */
void AcCtrl_HitAttack(AcPlayerSt *acp, SfxDstat *dstp, float *hitvec, float *hitpos, int stype, int sid)
{
    PS2_FRAME(0xd0);
    int guard = 0;
    AcCtrlPrivateSt *prv = acp->prvp;
    if (stype == 2 && sid == 0x7f) { stype = 0; sid = 0; }
    if (!dstp) dstp = &AcCtrl_HitAttack__dummy_dstat;
    if (MyI.padoff == 1 || (acp->stat & 0x4000)) return;

    int shield = acp->shieldstat == 1 && acctrl_shield_guard(acp, hitpos);
    float cut;
    if (shield) {
        acp->defence = acp->param.shield_def;
        cut = acp->param.cqheatShieldCut;
        acp->effectstat |= 0x800;
        guard = 1;
        if (sid != acp->id && stype == 2 && (unsigned int)sid < 4 && g_ac[sid].mp->msgfunc)
            gcall_as<void(StModel *, int, int)>(g_ac[sid].mp->msgfunc, g_ac[sid].mp, 2, 0);   /* 3 arguments; the DWARF type has 2 */
    } else if (acsideshld_IsGuardEnable(acp, hitpos)) {
        acp->defence = acp->param.exshield_def;
        cut = acp->param.cqheatExShieldCut;
        guard = 2;
        if (sid != acp->id && stype == 2 && (unsigned int)sid < 4 && g_ac[sid].mp->msgfunc)
            gcall_as<void(StModel *, int, int)>(g_ac[sid].mp->msgfunc, g_ac[sid].mp, 2, 0);   /* 3 arguments; the DWARF type has 2 */
    } else {
        acp->defence = acp->param.normal_def;
        cut = 0.0f;
    }
    float heat = (float)dstp->heat * g_asmparam.bulletheat_to_cqheat - cut;
    if (!(heat <= 0.0f)) add_cqheat(acp, heat);

    int damage = ac_damage_calc(acp, dstp, (char)stype);
    if (stype == 1) prv->dmgcnt_ene += damage;
    else if (stype == 2 && sid >= 0 && sid < (int)(sizeof prv->dmgcnt_ac / sizeof prv->dmgcnt_ac[0]))
        prv->dmgcnt_ac[sid] += damage;   /* per-attacker damage table inside the private struct: 4 entries */
    if (stype == 2) DF_ComDamage(acp->id, sid, damage);
    else if (stype == 1) DF_ComDamage(acp->id, sid | 0x30000000, damage);
    DF_TALK2_Damaged(acp->mp, stype, sid);
    if (dstp->effect & 0x40) {   /* FCS disturbance */
        int t = ps2_ftoi((float)dstp->damage * g_asmparam.dmg2fcserrtime);
        if (acp->param.head_addon & 2) t >>= 1;
        if (t > 0x4b0) t = 0x4b0;
        acp->nolock_time = (short)t;
    }
    if (dstp->effect & 0x80) {   /* flare */
        acp->flare_damage = (short)ps2_ftoi((float)damage * g_asmparam.flare_damagerate);
        if (!acp->flare_damage) acp->flare_damage = 1;
        acp->flare_time = (short)ps2_ftoi(g_asmparam.flaretime);
    }
    if (dstp->effect & 0x100) acp->genedown_time = (short)ps2_ftoi(g_asmparam.genedown_time);
    unsigned short dying = acp->dying_cnt;
    AcPtsDstry_HitAttack(acp->prvp->ptsdstryp, dstp, hitvec, hitpos, guard);
    AC_add_damage(acp, dstp, hitvec, hitpos, damage);
    if (acp->funcCounter) gcall(acp->funcCounter, acp, stype, sid);
    if (!dying && acp->dying_cnt == 1) {   /* just destroyed */
        prv->killmetype = (char)stype;
        prv->killmeid = (short)sid;
        acp->mp->shooter = (char)stype;
        acp->mp->shooterid = (unsigned char)sid;
        if (stype == 2) {
            AcPlayerSt *killer = &g_ac[sid];
            Info_IncData(1, 6, sid, 0);
            if (acp->mp->cflag & killer->mp->cflag & 0xf) Info_IncData(1, 7, sid, 0);
            else Info_IncData(1, 8, sid, 0);
            if (sid == 0) Info_IncData(0, 0x17, 0, acp->id);
            else if (killer->mp->cflag & 1) Info_IncData(3, 0xc, 0, acp->id);
            else Info_IncData(3, 0x11, 0, acp->id);
            if (acp->mp->cflag & 1) Info_IncData(3, 4, 0, 0);
            else Info_IncData(3, 5, 0, 0);
            CombatRecord_OnKill(killer, 1);
        }
        DF_TALK2_Destroyed(acp->mp, stype, sid);
    }
    if (acp->mp) {
        acp->mp->shooter = (char)stype;
        acp->mp->shooterid = (unsigned char)sid;
    }
}

/* ---- small accessors and flags (parts, HUD, COM, stage and script code) ---- */

/* supply truck: +60 AP per frame up to the maximum; the "AP < 10%" bit clears once AP is back above 10% */
void AcCtrl_APRestore(AcPlayerSt *acp)
{
    if (acp->param.hp - 60 < acp->hp) acp->hp = acp->param.hp;
    else acp->hp = acp->hp + 60;
    unsigned short st = acp->stat;
    if ((st & 8) && acp->param.hp < acp->hp * 10) acp->stat = st & 0xfff7;
}

/* refill every magazine: arm / back weapons, the extension (unless it has none, prog 0x11), the EO and the laser gun */
static void acctrl_refill(AcPlayerSt *acp, AcPartsLgunSt *lg, AcPartsExtSt *ext, AcPartsEoSt *eo, int eo_shell_test)
{
    for (int i = 0; i < acp->param.n_weapon; i++) acp->param.weapons[i].blt_cnt = acp->param.weapons[i].blt_initcnt;
    if (ext && ext->prog_no != 0x11) acp->param.extweapon.blt_cnt = acp->param.extweapon.blt_initcnt;
    if (eo && (!eo_shell_test || aceo_IsShellType(eo))) {
        _weapon_st *w = aceo_GetWeapon(eo);
        if (w) {
            w->blt_cnt = w->blt_initcnt;
            eo->blt_cnt = w->blt_cnt;
        }
    }
    if (lg) {
        _weapon_st *w = AcLgun_GetWeaponData(lg);
        if (w) w->blt_cnt = w->blt_initcnt;
    }
}

void AcCtrl_MagazineSupply(AcPlayerSt *acp)
{
    PS2_FRAME(0x50);
    AcPartsLgunSt *lg = AcInst_GetPartsLgun(acp);
    AcPartsExtSt *ext = AcInst_GetPartsExt(acp);
    AcPartsEoSt *eo = AcInst_GetPartsEo(acp);
    acctrl_refill(acp, lg, ext, eo, 1);
}

AcPtsResSt *AcCtrl_GetPartsRes(AcPlayerSt *acp) { return acp->prvp->ptsresp; }
AcActionSt *AcCtrl_GetAct(AcPlayerSt *acp) { return acp->prvp->actp; }
AcSfxSt *AcCtrl_GetSfx(AcPlayerSt *acp) { return acp->prvp->sfxp; }
AcSeSt *AcCtrl_GetSe(AcPlayerSt *acp) { return acp->prvp->sep; }
AcUidevSt *AcCtrl_GetUidev(AcPlayerSt *acp) { return acp->prvp->uidevp; }
AcEffSt *AcCtrl_GetEffect(AcPlayerSt *acp) { return acp->prvp->effp; }
AcPartsBladeSt *AcCtrl_GetPartsBlade(AcPlayerSt *acp) { return AcInst_GetPartsBlade(acp); }
AcPartsRbladeSt *AcCtrl_GetPartsRblade(AcPlayerSt *acp) { return AcInst_GetPartsRblade(acp); }
AcPartsLgunSt *AcCtrl_GetPartsLgun(AcPlayerSt *acp) { return AcInst_GetPartsLgun(acp); }
AcPartsExtSt *AcCtrl_GetPartsExt(AcPlayerSt *acp) { return AcInst_GetPartsExt(acp); }
AcPartsSlowSt *AcCtrl_GetPartsSlow(AcPlayerSt *acp) { return AcInst_GetPartsSlow(acp); }
AcPartsEoSt *AcCtrl_GetPartsEo(AcPlayerSt *acp) { return AcInst_GetPartsEo(acp); }
AcPartsObSt *AcCtrl_GetPartsOb(AcPlayerSt *acp) { return AcInst_GetPartsOb(acp); }

AcDptsSt *acbkwep_GetDestroyInfo(AcPlayerSt *acp, int right)
{
    return right ? &acp->prvp->dpts_rback : &acp->prvp->dpts_lback;
}
AcDptsSt *acrgun_GetDestroyInfo(AcPlayerSt *acp) { return &acp->prvp->dpts_rhand; }
AcDptsSt *acshield_GetDestroyInfo(AcPlayerSt *acp) { return &acp->prvp->dpts_shield; }

/* shield arm objects 5-8 into animation group 8 (shield up) or 9 */
void acshield_SetAnimGroup(AcPlayerSt *acp, int equip)
{
    PS2_FRAME(0x20);
    int group = equip ? 8 : 9;
    for (int obj = 5; obj <= 8; obj++) Anim_MakeGroup(acp->mp, obj, group);
}

void AcCtrl_SetSpecialFlag(AcPlayerSt *acp, unsigned short spflag, int isEnable)
{
    if (isEnable) acp->spflag |= spflag;
    else acp->spflag &= (unsigned short)~spflag;
}

/* script: fire the weapon mounted at an equipment position (8 / 0xb / 0xa / 0xd / 0xc = joint 7 / 5 / 4 / 2 / 1;
 * 0xd is the laser-gun arm) */
void AcCtrl_ForceAppearSpell(AcPlayerSt *acp, int acpeq)
{
    PS2_FRAME(0x90);
    int jpos;
    switch (acpeq) {
    case 8: jpos = 7; break;
    case 0xb: jpos = 5; break;
    case 0xa: jpos = 4; break;
    case 0xd: jpos = 2; break;
    case 0xc: jpos = 1; break;
    default: jpos = -1; break;
    }
    _ac_weaponctrl *wc = nullptr;
    _weapon_st *wp = nullptr;
    if (acpeq == 0xd) {
        AcPartsLgunSt *lg = AcInst_GetPartsLgun(acp);
        if (lg) {
            wc = AcLgun_GetWeaponCtrl(lg);
            if (wc->wp && wc->wp->jpos == jpos) wp = wc->wp;
        }
    } else
        for (int i = 0; i < 4; i++) {
            wc = &acp->weaponctrl[i];
            if (wc->wp && wc->wp->jpos == jpos) {
                wp = wc->wp;
                break;
            }
        }
    if (!wp) return;
    unsigned short flag = acpeq == 0xd ? 0x200 : AcWep_GetAmmoFlag(wp->statid);
    DF_SPELL2_AppearSpellPtr(wp->spelp, 2, (char)acp->id, &wp->dstat, wp->actp, wp->combp, nullptr, nullptr, nullptr,
                             &wc->ocmats[0], nullptr, flag, (unsigned short)wp->id);
}

void AcCtrl_ForceAppearEffect(AcPlayerSt *acp, unsigned int effectstat) { acp->prvp->effectstatNext |= effectstat; }
void AcCtrl_NotifyControlPolyG(AcPlayerSt *acp, int isPolyG) { acp->prvp->isPolyG = isPolyG; }

/* the gaze core's own lock manager while gaze lock is on */
StWeaponLockManage *AcCtrl_GetGazeLockMng(AcPlayerSt *acp)
{
    PS2_FRAME(0x10);
    if (acp->param.core_type != 3) return nullptr;
    if (!acp->prvp->isGazeLock) return nullptr;
    return AcGaze_GetLockMng(AcInst_GetPartsGaze(acp));
}

void AcCtrl_SetDeadPause(AcPlayerSt *acp, int flag) { acp->prvp->dead_pause = flag != 0; }
void AcCtrl_ClearAreaoveredFlag(int acid) { g_ac[acid].areaovered = 0; }

/* enumerates the booster effects (0..0x10: boost n / 2, left / right by parity) and blade effects (0x11..0x15) that
 * a view mode hides; 0 when done */
int AcCtrl_EnumHideBooster(AcPlayerSt *acp, int *piCnt, _magic2_st **map2pp)
{
    PS2_FRAME(0x30);
    AcEffSt *eff = acp->prvp->effp;
    int n = *piCnt;
    if (n >= 0 && n < 0x11) {
        _aceff_boostmng *b = aceff_GetBoost(eff, n / 2);
        *map2pp = (*piCnt & 1) ? b->mag2p[2] : b->mag2p[0];
    } else if (n >= 0x11 && n < 0x16)
        *map2pp = aceff_GetBlade(eff, n - 0xf)->mag2p;
    else
        return 0;
    (*piCnt)++;
    return 1;
}

/* script: bring a destroyed AC back with full AP and magazines */
int AcCtrl_ReviveForEnemy(int acid)
{
    PS2_FRAME(0x50);
    AcPlayerSt *acp = &g_ac[acid];
    AcPartsLgunSt *lg = AcInst_GetPartsLgun(acp);
    AcPartsExtSt *ext = AcInst_GetPartsExt(acp);
    AcPartsEoSt *eo = AcInst_GetPartsEo(acp);
    acp->hp = acp->param.hp;
    acp->dead = 0;
    acp->mp->use &= 0xfffe;
    acp->mp->use &= 0xfffd;
    acp->stat = 0;
    acp->effectstat = 0;
    acp->areastat = 0;
    acp->shieldstat = 0;
    acp->dying_cnt = 0;
    AcWep_InitAllWeapons(acp);
    AC_SetAction(acp, 0);
    AcAct_Exec(acp);
    acctrl_refill(acp, lg, ext, eo, 0);
    return 1;
}

int AcCtrl_GetDmgForEnemy(int acid) { return g_ac[acid].prvp->dmgcnt_ene; }
int AcCtrl_GetDmgForAc(int acid, int attack_acid) { return g_ac[acid].prvp->dmgcnt_ac[attack_acid]; }

/* the AC stands inside stage marker dmyid's box (marker type rtype; min / max offsets in its work area) */
int AcCtrl_IsArriveDmy(int acid, int dmyid, char rtype)
{
    AcPlayerSt *acp = &g_ac[acid];
    MarkerSt *m = &Array->marker[dmyid];
    if (m->type != rtype) return 0;
    for (int k = 0; k < 3; k++) {
        float mn, mx;
        memcpy(&mn, m->wk + 4 * k, 4);
        memcpy(&mx, m->wk + 0x10 + 4 * k, 4);
        if (acp->loc[k] < m->loc[k] + mn) return 0;
        if (!(acp->loc[k] <= m->loc[k] + mx)) return 0;
    }
    return 1;
}

void AcCtrl_SetHalfBoostEnergy(AcPlayerSt *acp, int on)
{
    if (on) acp->spflag |= 8;
    else acp->spflag &= 0xfff7;
}

void AcCtrl_SetNoBoostEnergy(AcPlayerSt *acp, int on)
{
    if (on) acp->spflag |= 4;
    else acp->spflag &= 0xfffb;
}

void AcCtrl_SetNoDeestroyParts(AcPlayerSt *acp, int on)
{
    if (on) acp->spflag |= 1;
    else acp->spflag &= 0xfffe;
}

int AcCtrl_IsAlive(int acid) { return g_ac[acid].hp > 0; }
float AcCtrl_GetApRate(int acid) { return ps2_div((float)g_ac[acid].hp, (float)g_ac[acid].param.hp); }

/* a spare weapon exists for equipment slot eq (0xf / 0xd sword side, 0xe / 0xc gun side) */
int AcCtrl_IsExistSpareWep(AcPlayerSt *acp, int eq)
{
    if (eq == 0xf || eq == 0xd) return acp->param.spswordp != nullptr;
    if (eq == 0xe || eq == 0xc) return acp->param.spgunp != nullptr;
    return 0;
}

int AcCtrl_IsSpareWepFuncCore(AcPlayerSt *acp) { return (acp->param.corep->addon & 4) != 0; }
int AcCtrl_IsAnalog(AcPlayerSt *acp) { return AcUidev_IsAnalog(acp->frdp->uidevp); }
int AcCtrl_IsSelfExceptSlow(AcPlayerSt *acp) { return acp->prvp->isCallSlow; }
int AcCtrl_IsCharging(AcPlayerSt *acp) { return (acp->stat & 4) != 0; }
void AcCtrl_SetGazeLock(AcPlayerSt *acp, int isGazeLock) { acp->prvp->isGazeLock = isGazeLock; }
int AcCtrl_IsGazeLock(AcPlayerSt *acp) { return acp->prvp->isGazeLock; }

/* vibration is only felt on a pad that views this AC (user players, not in replays / movies / after area-over);
 * the same gate as __ExecAc's */
static int acctrl_vibra_view(AcPlayerSt *acp)
{
    int nview = ViewNum >= 3 ? 2 : ViewNum, view = -1;
    int id = acp->id;
    for (int i = 0; i < nview; i++)
        if (MyI.myplayerno[i] == id) {
            view = i;
            break;
        }
    return !MyI.replaymode && !MyI.mov_play && view >= 0 && g_gamedata[view].option.vibration && !acp->areaovered;
}

void AcCtrl_SetVibration(AcPlayerSt *acp, unsigned char power, short time)
{
    PS2_FRAME(0x10);
    if (acctrl_vibra_view(acp)) Vibra_SetPower(&acp->prvp->vibramng, power, time);
}

/* the AC is shown in a view: modes 2-6 with a split screen have two cameras */
static int acctrl_my_view(AcPlayerSt *acp)
{
    StSortieConstant *sc = g_sortie.pconstant;
    unsigned int mode = sc->modeSortie;
    if (mode >= 2 && mode <= 6 && sc->isSplit) return acp->id == ViewCamID[0] || acp->id == ViewCamID[1];
    return acp->id == ViewCamID[0];
}

int AcCtrl_IsMyView(AcPlayerSt *acp) { return acctrl_my_view(acp); }

/* hover legs (abl_onwater) can stand on water, except during parts of jumps / landing for leg program 3 */
static int __IsEnableWalkOnWater(AcPlayerSt *acp)
{
    if (!acp->param.abl_onwater) return 0;
    if (acp->param.prog_no != 3) return 1;
    if (acp->actno == 0x10) return acp->acttime < 0x2e;
    if (acp->actno == 0x11) return acp->acttime >= 0x14;
    if (acp->actno == 2 && acp->op[14].todid == 0x2f && acp->op[14].frame >= 0x1f) return 0;
    return 1;
}

void AC_SetAng(AcPlayerSt *acp, float *ang)
{
    memcpy(acp->oang, acp->ang, 16);
    memcpy(acp->ang, ang, 16);
}

void AC_ClearKeybuff(AcPlayerSt *acp) { AcUidev_ClearKeybuff(acp->frdp->uidevp); }

/* player control on (re-equip the held weapon) / off (put weapons away, close core / extension parts, shield off,
 * locks cleared) */
void AC_SetControl(AcPlayerSt *acp, int control_on)
{
    PS2_FRAME(0x30);
    if (control_on) {
        acp->stat &= 0xdfff;
        if (MyI.padoff != 1 && acp->wep_release) AcWep_EquipWeapon(acp, acp->equip_weapon);
        return;
    }
    acp->stat |= 0x2000;
    if (!acp->wep_release) AcWep_ReleaseWeapon(acp, acp->equip_weapon);
    if (acp->param.swordp && acp->param.swordp->type == 3) {
        AcPartsLgunSt *lg = AcInst_GetPartsLgun(acp);
        if (lg) AcLgun_ReleaseWeapon(lg);
    }
    if (acp->param.gunp && acp->param.gunp->type == 3) {
        AcPartsRbladeSt *rb = AcInst_GetPartsRblade(acp);
        if (rb) {
            acrb_NoRbAnimGroup(rb);
            acrb_HitForcedTermination(rb);
        }
    }
    if (acp->param.core_type == 1) acob_Close(AcInst_GetPartsOb(acp));
    else if (acp->param.core_type == 0) aceo_Close(AcInst_GetPartsEo(acp));
    ACsetact_shieldoff(acp);
    acext_Close(AcInst_GetPartsExt(acp));
    Lock_ClearMng(&acp->lockmng, 0);
    Lock_ClearMng(&acp->lockmng, 1);
}

/* temperature after `frames` frames of extra heat `add` (the __UpdateTemperature step without quick cooling) */
float AcCtrl_HeatSimulation(AcPlayerSt *acp, float tmprtStart, float cqheatAdd, int frameCont)
{
    float t = tmprtStart;
    float acc = acp->prvp->cqheatAccumulate;
    if (frameCont <= 0) return t;
    float k = g_asmparam.outtmprt_to_cqheat, fixed = acp->param.cqheatFixed, cool = acp->param.cqheatCooldown;
    float air = acp->mp->airtemp, perf = g_asmparam.addcqheat_per_frame, tot = g_asmparam.cqheat_to_tmprt;
    for (int i = 0; i < frameCont; i++) {
        float h = air - t;
        h = k * h;
        h = acc + h;
        h = fixed + h;
        h = cqheatAdd + h;
        acc = h - cool;
        float d = acc * perf;
        float dt = d * tot;
        acc = acc - d;
        t = t + dt;
        if (acc <= 0.0f) acc = 0.0f;
    }
    return t;
}

void AcCtrl_AddHeat(AcPlayerSt *acp, float heat) { add_cqheat(acp, heat); }

int AcCtrl_EnergyDrain(AcPlayerSt *acp, int sub)
{
    if (acp->stat & 4) return 0;
    if (acp->energyReal < sub) return 0;
    acp->energyReal -= sub;
    return 1;
}

void AcCtrl_DispBlade(AcPlayerSt *acp, int bladeid, int frame)
{
    PS2_FRAME(0x30);
    if (bladeid == 0) {
        aceff_GetBlade(acp->prvp->effp, 0)->frame = frame;
        acp->effectstat |= 8;
    } else if (bladeid == 1) {
        aceff_GetBlade(acp->prvp->effp, 1)->frame = frame;
        acp->effectstat |= 0x10;
    }
}

void AcCtrl_DispFootSpark(AcPlayerSt *acp)
{
    if (!acp->param.prog_no) acp->effectstat |= 4;
}

void AcCtrl_DispLegBoostFire(AcPlayerSt *acp)
{
    acp->effectstat |= 2;
    acp->hover_effectlv = 2;
}

void AcCtrl_DispBackBoostFire(AcPlayerSt *acp)
{
    acp->effectstat |= 1;
    acp->hover_effectlv = 2;
}

/* ---- per-frame helpers of AcCtrl_Exec / __ExecAc / __UpdateEnergy ---- */

/* energy regained this frame: none during the OB core's penalty; legs other than programs 0 / 2 regain the move
 * rate only while actually moving; generator-down hits subtract (a negative result becomes 1) */
static int __CalcEnergyAdd(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    AcPartsObSt *ob = AcInst_GetPartsOb(acp);
    if (ob && ob->penalty_time > 0) return 0;
    unsigned int a = acp->actno;
    int v;
    if (a == 0) v = acp->param.boost_add;
    else if (acp->param.prog_no == 2 || acp->param.prog_no == 0)
        v = (a == 3 || a == 5) ? acp->param.move_boost_add : acp->param.boost_add;
    else if (a == 5) {
        float vx = acp->velocity[0] < 0.0f ? -acp->velocity[0] : acp->velocity[0];
        float vz = acp->velocity[2] < 0.0f ? -acp->velocity[2] : acp->velocity[2];
        const float eps = ps2_u2f(0x3a83126f);
        v = (vx < eps && vz < eps) ? acp->param.boost_add : acp->param.move_boost_add;
    } else
        v = acp->param.boost_add;
    if (acp->genedown_time > 0) {
        v = ps2_ftoi((float)v - g_asmparam.genedown_rate * g_asmparam.prog_boostadd_rate);
        if (v < 0) v = 1;
    }
    return v;
}

/* player 1's cockpit voice warnings: AP below 10%, AP below 50% (story / arena / 1P modes only) */
static void __UpdateSelfComMessage(AcPlayerSt *pac)
{
    PS2_FRAME(0x30);
    StAcSelfComMessage *m = &pac->prvp->selfcommsg;
    unsigned int mode = g_sortie.pconstant->modeSortie;
    if (mode > 3) return;
    if (pac->id || g_sortie.pwork->isPlayPolyg) return;
    if (!m->isAp10perWarn && (pac->stat & 8) && pac->hp > 0 && !MsgWnd_IsBusy(1) && acctrl_my_view(pac)) {
        MsgWnd_ShowCom(10, nullptr);
        m->isAp10perWarn = 1;
        m->isAp50perWarn = 1;
    }
    if (!m->isAp50perWarn && pac->hp * 2 < pac->param.hp && pac->hp > 0 && !MsgWnd_IsBusy(1) && acctrl_my_view(pac)) {
        MsgWnd_ShowCom(9, nullptr);
        m->isAp50perWarn = 1;
    }
}

/* animation of the AC and its attached models, arm joint control, laser-gun arm / blade animation, unfolding back
 * cannons and the extension */
static void __ExecAcAnim(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    Anim_Update(acp->mp);
    if (acp->boostmp) Anim_Update(acp->boostmp);
    if (acp->eobmp) Anim_Update(acp->eobmp);
    if (acp->sublegmp) Anim_Update(acp->sublegmp);
    acjcon_JointControl(acp, &acp->Warm_ctrl);
    acjcon_JointControl(acp, &acp->Rarm_ctrl);
    if (acp->param.swordp) {
        if (acp->param.swordp->type == 3) {
            AcPartsLgunSt *lg = AcInst_GetPartsLgun(acp);
            if (lg && (*(unsigned char *)lg & 1) == 1) acjcon_JointControl(acp, AcLgun_GetJointCtrl(lg));
        } else {
            AcPartsBladeSt *bl = AcInst_GetPartsBlade(acp);
            if (bl) acblade_Animation(bl);
        }
    }
    if (!acp->dead) {
        if (acp->param.Lbackp && acp->param.Lbackp->type == 1) Anim_Update(acp->modeltbl[0]);
        if (acp->param.Rbackp && acp->param.Rbackp->type == 1) Anim_Update(acp->modeltbl[1]);
    }
    if (acp->param.extp) {
        Anim_Update(acp->modeltbl[2]);
        Anim_Update(acp->modeltbl[3]);
    }
}

/* incoming-missile list (16 entries): a known missile keeps its slot (later duplicates are dropped), a new one takes
 * the first free slot before it; alert is cleared when the entry had none */
static void add_vsmissile(AcPlayerSt *acp, _spell2 *magp, int alert)
{
    struct_f6a33bd0 *buf = acp->prvp->missile_buff, *slot = nullptr;
    int found = 0;
    for (unsigned int i = 0; i < 16; i++) {
        struct_f6a33bd0 *e = &buf[i];
        if (found) {
            if (e->magp == magp) {
                if (!e->alert) alert = 0;
                e->magp = nullptr;
            }
        } else if (e->magp == magp) {
            slot = e;
            found = 1;
            if (!e->alert) alert = 0;
        } else if (!e->magp) {
            e->magp = magp;
            slot = e;
            found = 1;
        }
    }
    if (slot) slot->alert = alert;
}

/* mp->msgfunc of every AC (variadic in the original): msg 1 = a missile targets this AC (arg0 = the spell), msg 2 =
 * the AC's shot hit an enemy shield */
static void AC_msgfunc(StModel *mp, int msg, void *arg0)
{
    AcPlayerSt *acp = (AcPlayerSt *)mp->arg0p;
    if (msg == 2) acp->stat |= 0x20;
    else if (msg == 1) add_vsmissile(acp, (_spell2 *)arg0, 0);
}

/* extension interceptor: fires at the incoming spell, which it misses errrate% of the time (no target) */
int AcCtrl_InterceptSpell(AcPlayerSt *acp, _stSpell2Data *intrsdp, _stSfxCombHead *combp, ObjCMatSt *matp,
                          _spell2 *trgsplp, int errrate)
{
    PS2_FRAME(0x70);
    if (ps2_mod32(ps2_rand(), 0x190000) >> 14 < errrate) trgsplp = nullptr;
    return DF_SPELL2_AppearSpell2Ptr(intrsdp, 2, (char)acp->id, nullptr, combp, matp, nullptr, trgsplp) != nullptr;
}

/* ---- movement helpers of the action code (acact.c, leg / tank / cannon overlay code) ---- */

/* turn speed decaying towards 0 by accel */
static inline float acctrl_turn_decay(float t, float accel)
{
    if (!(t <= 0.0f)) return t <= accel ? 0.0f : t - accel;
    return t < -accel ? t + accel : 0.0f;
}

static inline float acctrl_clamp(float t, float m)
{
    if (!(t <= m)) t = m;
    if (t < -m) t = -m;
    return t;
}

/* extension boost turning: decays from the side the boost angle points to */
void AcCtrl_TurnExBoost(AcPlayerSt *acp, float accel, float maxspd)
{
    PS2_FRAME(0x30);
    AcPartsExtSt *ext = AcInst_GetPartsExt(acp);
    float t = acp->turnspd;
    if (ext && !(ext->boostangy <= 0.0f)) t = t <= accel ? 0.0f : t - accel;
    else t = t < -accel ? t + accel : 0.0f;
    t = acctrl_clamp(t, maxspd);
    acp->turnspd = t;
    acp->ang[1] = acp->ang[1] + t;
}

/* pad turning (stick x beyond +-59 scaled to 61, or the turn buttons); 1 right, -1 left, 0 none. Gaze lock turns
 * instead; an extension boost freezes the turn. */
int AcCtrl_TurnPad(AcPlayerSt *acp, float accel, float maxspd)
{
    PS2_FRAME(0x50);
    int ret = 0;
    short boostfrm = 0;
    AcPartsExtSt *ext = AcInst_GetPartsExt(acp);
    if (ext) boostfrm = ext->boostfrm;
    float t = acp->turnspd;
    if (acp->prvp->gazelock != -1) return 0;
    AcUidevSt *u = acp->frdp->uidevp;
    if (AcUidev_IsAnalog(u)) {
        int s = (signed char)AcUidev_GetPadStickConf(acp->frdp->uidevp, 2);
        if (s >= 0x3c) {
            ret = 1;
            acp->turnrightleft = 1;
            t = t + accel * ((float)(s - 0x3b) / 61.0f);
        } else if (s < -0x3b) {
            acp->turnrightleft = 0;
            ret = -1;
            t = t + accel * ((float)(s + 0x3b) / 61.0f);
        } else {
            t = acctrl_turn_decay(t, accel);
            ret = 0;
        }
    } else if (AcUidev_IsPadOnConf(acp->frdp->uidevp, 3)) {
        t = t + accel;
        ret = 1;
        acp->turnrightleft = 1;
    } else if (AcUidev_IsPadOnConf(acp->frdp->uidevp, 2)) {
        t = t - accel;
        acp->turnrightleft = 0;
        ret = -1;
    } else {
        t = acctrl_turn_decay(t, accel);
        ret = 0;
    }
    t = acctrl_clamp(t, maxspd);
    if (!boostfrm) {
        acp->turnspd = t;
        acp->ang[1] = acp->ang[1] + t;
    }
    return ret;
}

/* xz speed reduced by accel (stops at 0); returns the new speed */
float AcCtrl_BrakeXz(AcPlayerSt *acp, float accel)
{
    float vx = acp->velocity[0];
    float len = ps2_sqrt(vx * vx + acp->velocity[2] * acp->velocity[2]);
    float s = len - accel;
    if (s <= 0.0f) {
        acp->velocity[2] = 0.0f;
        acp->velocity[0] = 0.0f;
        return 0.0f;
    }
    acp->velocity[0] = ps2_div(vx * s, len);
    acp->velocity[2] = ps2_div(acp->velocity[2] * s, len);
    return s;
}

/* gravity with a fall speed limit (as __ExecAc's own block); returns the limited speed */
float AcCtrl_AccelFall(AcPlayerSt *acp, float gravity, float maxspd)
{
    float vy = acp->velocity[1];
    float nv = vy + gravity, v;
    if (vy <= 0.001f + maxspd) v = nv <= maxspd ? nv : maxspd;
    else {
        v = vy - gravity;
        if (v < maxspd) v = maxspd;
    }
    acp->velocity[1] = nv * ps2_div(v, nv);
    return v;
}

/* accvec (the shared static) = accel * dir2xzvec[dir] turned by the AC's heading (VU apply, all four lanes) */
static void acctrl_accel_dir(AcPlayerSt *acp, const float *dir, float accel)
{
    float *av = AcCtrl_Accel8Dir__accvec;
    av[0] = accel * dir[0];
    av[2] = accel * dir[1];
    float m[4][4] __attribute__((aligned(16)));
    sceVu0RotMatrixY(m, IdentityMtx, acp->ang[1]);
    acctrl_acc_mirror((const float (*)[4])m, av);
    vu_apply(av, (const float (*)[4])m, av);
    for (int i = 0; i < 4; i++) PS2_VF_MIRROR(1 + i, m[i]);
    PS2_VF_MIRROR(16, av);
}

float AcCtrl_Accel8Dir(AcPlayerSt *acp, int dir, float accel, float maxspd)
{
    PS2_FRAME(0x70);
    acctrl_accel_dir(acp, AcCtrl_Accel8Dir__dir2xzvec[dir], accel);
    return AcCtrl_AccelXz(acp, AcCtrl_Accel8Dir__accvec, maxspd);
}

/* adds accvec to the xz velocity: below the speed limit the new speed is capped at maxspd, above it the speed
 * only decays by flymv_accel (down to maxspd); returns the speed the velocity is scaled to */
float AcCtrl_AccelXz(AcPlayerSt *acp, float *accvec, float maxspd)
{
    float lim = 0.001f + maxspd;
    float vx = acp->velocity[0], vz = acp->velocity[2];
    acp->velocity[0] = vx + accvec[0];
    float old = ps2_sqrt(vx * vx + vz * vz);
    acp->velocity[2] = acp->velocity[2] + accvec[2];
    float nx = acp->velocity[0], nz = acp->velocity[2];
    float nl = ps2_sqrt(nx * nx + nz * nz);
    float s;
    if (old <= lim) s = nl <= maxspd ? nl : maxspd;
    else {
        s = old - acp->param.flymv_accel;
        if (s < maxspd) s = maxspd;
    }
    if (!(nl <= 0.0f)) {
        float q = ps2_div(s, nl);
        acp->velocity[0] = acp->velocity[0] * q;
        acp->velocity[2] = acp->velocity[2] * q;
    }
    return s;
}

/* walking: walk accel / max speed scaled by back-step rate (dirs 5-7), the case rate, the analog stick and the
 * overweight rate; a diagonal step right after the opposite diagonal adds the cutback push (dir2xzvec[3] / [4]) */
void AcCtrl_WalkAsParameter(AcPlayerSt *acp, int dir, float rateCase)
{
    PS2_FRAME(0x210);
    float acc = acp->param.walkmv_accel, max = acp->param.walkmv_maxspd;
    if (dir == 7 || dir == 5 || dir == 6) {
        acc = acc * acp->param.legsp->bkspdrate;
        max = max * acp->param.legsp->bkspdrate;
    }
    acc = acc * rateCase;
    max = max * rateCase;
    if (AcUidev_IsAnalog(acp->frdp->uidevp)) {
        char h = AcUidev_GetPadStickConf(acp->frdp->uidevp, 0);
        char v = AcUidev_GetPadStickConf(acp->frdp->uidevp, 1);
        float r = AcUidev_ApplyAnalogLegRate(dir, h, v, 0.1f, nullptr);
        acc = acc * r;
        max = max * r;
    }
    float ow = dir < 5 ? acp->param.legsp->ow_rate_min_fw : acp->param.legsp->ow_rate_min_bk;
    if (ow == 0.0f) ow = 0.5f;
    float r2 = AcAsm_GetSpeedOwRate(&acp->param);
    float f = ow + (1.0f - ow) * r2;
    acc = acc * f;
    max = max * f;
    float (*vec)[2] = AcCtrl_Accel8Dir__dir2xzvec;
    unsigned int prev = acp->dirCutbackPrev;
    int cut = 0, cutvec = 0, first = -1;
    if (dir == 1 || dir == 3 || dir == 6) {
        if (prev == 7 || prev == 4 || prev == 2) {
            cut = 1;
            cutvec = 3;
            first = dir == 6 ? 5 : dir == 1 ? 0 : -1;
        }
    } else if (dir == 2 || dir == 4 || dir == 7) {
        if (prev == 6 || prev == 3 || prev == 1) {
            cut = 1;
            cutvec = 4;
            first = dir == 7 ? 5 : dir == 2 ? 0 : -1;
        }
    }
    if (!cut) {
        acctrl_accel_dir(acp, vec[dir], acc);
        AcCtrl_AccelXz(acp, AcCtrl_Accel8Dir__accvec, max);
        return;
    }
    if (first >= 0) {
        acctrl_accel_dir(acp, vec[first], acc);
        AcCtrl_AccelXz(acp, AcCtrl_Accel8Dir__accvec, max);
    }
    acctrl_accel_dir(acp, vec[cutvec], acp->param.cutback_accel);
    AcCtrl_AccelXz(acp, AcCtrl_Accel8Dir__accvec, acp->param.cutback_maxspd);
}

/* action button: drop the left-hand part / pick up a weapon / open a door / start the model's script / its gimmick
 * event. The result adds the raw ids (callers only test != 0). */
int AcCtrl_PadEvent(AcPlayerSt *acp)
{
    PS2_FRAME(0x70);
    int pressed;
    if (AcUidev_IsAnalog(acp->frdp->uidevp)) pressed = AcUidev_IsPadOnConf(acp->frdp->uidevp, 0x10);
    else pressed = AcUidev_IsPadOnMaskedEqual(acp->frdp->uidevp, 0xf00, 0xf00);
    if (pressed) {
        unsigned int a = acp->actno;
        if (a == 0xb || a == 0xc || a == 0xd || a == 0xe || acp->rb_stat) return 0;
        AcWep_RemoveLeftHandParts(acp, 0);
        return 1;
    }
    if (!(acp->id < NowUserPlayerNum) || UseAI[acp->id]) return 0;
    int item = 0;
    if (AcWep_IsPickUpWeapon(acp)) {
        PS2_STACK(float[4], pos, 0x60);
        PS2_STACK(float[4], ang, 0x50);
        memcpy(pos, acp->loc, 16);
        pos[1] = pos[1] - 0.5f * acp->tall;
        ang[0] = acp->view.ang[0];
        float a = 3.14159274f + acp->ang[1];
        acctrl_wrap_ang(&a);
        ang[1] = a;
        ang[3] = 1.0f;
        ang[2] = 0.0f;
        item = DF_ITM_PickUp(pos, ang);
        if (item > 0) {
            AcWep_PickUpWeapon(acp, item);
            return 1;
        }
    }
    if (acp->actno == 0x12) return 0;
    int door = 0;
    StModel *smp = acp->smp;
    if (smp && smp->array->eid == 0x50 && DF_DOOR_DoorOpen(smp, acp->mp, 1) >= 0) door = 1;
    smp = acp->smp;
    int scr = smp ? smp->array->scrid : 0;
    if (scr) {
        if (scr >= 1000) {
            if (DF_ScriptStart(scr, smp) < 0) scr = 0;
        } else
            EVMNG_Entry(scr);
    }
    int gim = 0;
    smp = acp->smp;
    if (smp) {
        gim = smp->array->eid;
        if (gim && ExecGimkEvent((GimkSt *)smp->arg0p) < 0) gim = 0;
    }
    return item + (gim + (scr + door));
}

/* ---- per-frame helpers of AcCtrl_Exec / __ExecAc ---- */

#ifdef ACNB_SOURCEPORT
struct_801365ec AC_CameraParam = {27.0f, 0.00136f, 0.009f, 0.027f, -1.1781f, 1.1781f, 0.146f, 0.267f};
#endif

/* view pitch from the right stick / L2 R2 (both: re-centre and hold for view_L2R2fixframe frames) */
static void __ExecViewAng(AcPlayerSt *acp)
{
    PS2_FRAME(0x50);
    const struct_801365ec *cp = &AC_CameraParam;
    auto *v = &acp->view;
    if (acp->prvp->gazelock != -1) return;
    auto decay = [&]() {
        if (!(v->angspd[0] <= 0.0f)) {
            if (AddF_min(&v->angspd[0], -cp->view_rotbrk, 0.0f)) v->angspd[0] = 0.0f;
        } else if (AddF_max(&v->angspd[0], cp->view_rotbrk, 0.0f))
            v->angspd[0] = 0.0f;
    };
    if (AcUidev_IsAnalog(acp->frdp->uidevp)) {
        int s = (signed char)AcUidev_GetPadStickConf(acp->frdp->uidevp, 3);
        if (s >= 0x3c || s < -0x3b) v->fixtime = 0;
        if (v->fixtime > 0) return;
        if (AcUidev_IsPadTrgConf(acp->frdp->uidevp, 0xc)) {
            v->fixtime = ps2_ftoi(cp->view_L2R2fixframe);
            v->ang[0] = 0.0f;
            v->angspd[0] = 0.0f;
        } else if (s >= 0x3c) {
            float maxspd = cp->view_rotmaxspd;
            float a = ((0.7f * maxspd) * ((float)s - 59.0f)) / 61.0f;
            if (s >= 0x74) {
                AddF_max(&v->angspd[0], 0.1f * cp->view_rotacc, maxspd);
                if (v->angspd[0] < a) v->angspd[0] = a;
            } else
                v->angspd[0] = a;
        } else if (s < -0x3b) {
            float maxspd = cp->view_rotmaxspd;
            float a = ((0.7f * maxspd) * (59.0f + (float)s)) / 61.0f;
            if (s < -0x73) {
                AddF_min(&v->angspd[0], 0.1f * -cp->view_rotacc, -maxspd);
                if (!(v->angspd[0] <= a)) v->angspd[0] = a;
            } else
                v->angspd[0] = a;
        } else
            decay();
    } else {
        if (AcUidev_IsPadTrgConf(acp->frdp->uidevp, 0xb) || AcUidev_IsPadTrgConf(acp->frdp->uidevp, 0xa))
            v->fixtime = 0;
        if (v->fixtime > 0) {
            v->fixtime--;
            return;
        }
        if (AcUidev_IsPadOnConf(acp->frdp->uidevp, 0xa) && AcUidev_IsPadOnConf(acp->frdp->uidevp, 0xb)) {
            v->fixtime = ps2_ftoi(cp->view_L2R2fixframe);
            v->ang[0] = 0.0f;
            v->angspd[0] = 0.0f;
        } else if (AcUidev_IsPadOnConf(acp->frdp->uidevp, 0xa))
            AddF_max(&v->angspd[0], cp->view_rotacc, cp->view_rotmaxspd);
        else if (AcUidev_IsPadOnConf(acp->frdp->uidevp, 0xb))
            AddF_min(&v->angspd[0], -cp->view_rotacc, -cp->view_rotmaxspd);
        else
            decay();
    }
    if (AddF_minmax(&v->ang[0], v->angspd[0], cp->view_minx, cp->view_maxx)) v->angspd[0] = 0.0f;
}

/* gaze core: while gaze lock holds a target and no turn / look input is given, the AC and the view follow the
 * lock's angle offsets (clamped to the turn / view speeds) */
static void __GazeLock(AcPlayerSt *acp)
{
    PS2_FRAME(0x30);
    signed char old = acp->prvp->gazelock;
    acp->prvp->gazelock = -1;
    if (!acp->prvp->isGazeLock) return;
    if (AcUidev_IsPadOnConf(acp->frdp->uidevp, 3) || AcUidev_IsPadOnConf(acp->frdp->uidevp, 2)) return;
    if (AcUidev_IsPadOnConf(acp->frdp->uidevp, 0xa) || AcUidev_IsPadOnConf(acp->frdp->uidevp, 0xb)) return;
    if (AcInst_GetPartsExt(acp)->boostfrm) return;
    StWeaponLockManage *lm = nullptr;
    if (acp->param.core_type == 3 && acp->prvp->isGazeLock) lm = AcGaze_GetLockMng(AcInst_GetPartsGaze(acp));
    /* the original reads through lm unchecked (PS2 address 0x80 when NULL) */
    const StWeaponLockManage *l = lm ? lm : (const StWeaponLockManage *)PS2_NULL_MEM();
    if (!l->lock[0].ofp || !(l->lock[0].lock_stat & 2)) {
        if (old != -1) {
            acp->view.fixtime = 0;
            acp->view.ang[0] = 0.0f;
            acp->view.angspd[0] = 0.0f;
        }
        return;
    }
    acp->prvp->gazelock = 0;
    float a = l->lock[0].ang[1];
    if (!(a <= 0.0f)) acp->prvp->gazelock = 1;
    else if (a < 0.0f) acp->prvp->gazelock = 2;
    else acp->prvp->gazelock = 0;
    a = acctrl_clamp(a, acp->param.turn_maxspd);
    acp->ang[1] = acp->ang[1] + a;
    float b = acctrl_clamp(l->lock[0].ang[0], AC_CameraParam.view_rotmaxspd);
    acp->view.ang[0] = acp->view.ang[0] + b;
}

/* standing on a moving / turning parent model: parent_movevec = this frame's carry (the AC's position relative to
 * the parent's old matrix, re-applied with the new one), heading turned with the parent */
static void __CarryAc(AcPlayerSt *acp)
{
    PS2_FRAME(0xb0);
    AcCtrlPrivateSt *prv = acp->prvp;
    StModel *np = acp->mp->nowparent;
    if ((acp->actstat & 1) || !np || np != prv->old_parent) {
        prv->parent_movevec[0] = 0.0f;
        prv->parent_movevec[1] = 0.0f;
        prv->parent_movevec[2] = 0.0f;
        prv->parent_movevec[3] = 1.0f;
        return;
    }
    if (np->ang[0] == prv->parent_oldang[0] && np->ang[1] == prv->parent_oldang[1] &&
        np->ang[2] == prv->parent_oldang[2]) {
        /* translation only */
        for (int k = 0; k < 3; k++) prv->parent_movevec[k] = np->matrix2[3][k] - prv->parent_oldmat[3][k];
        prv->parent_movevec[3] = np->matrix2[3][3];
        PS2_VF_MIRROR(16, prv->parent_movevec);
        PS2_VF_MIRROR(17, prv->parent_oldmat[3]);
        return;
    }
    PS2_STACK(float[4][4], t, 0x70);
    PS2_STACK(float[4], d, 0x60);
    PS2_STACK(float[4], l, 0x50);
    PS2_STACK(float[4], w, 0x40);
    sceVu0TransposeMatrix(t, prv->parent_oldmat);
    memcpy(d, acp->loc, 16);
    for (int k = 0; k < 3; k++) d[k] = acp->loc[k] - acp->parent_oldpos[k];
    d[3] = 0.0f;
    vu_apply(l, (const float (*)[4])t, d);
    l[3] = 0.0f;
    acctrl_acc_mirror((const float (*)[4])np->matrix2, l);
    vu_apply(w, (const float (*)[4])np->matrix2, l);
    for (int k = 0; k < 3; k++) w[k] = w[k] + np->loc[k];
    for (int k = 0; k < 3; k++) prv->parent_movevec[k] = w[k] - acp->loc[k];
    prv->parent_movevec[3] = w[3];
    for (int i = 0; i < 4; i++) PS2_VF_MIRROR(1 + i, np->matrix2[i]);
    PS2_VF_MIRROR(16, prv->parent_movevec);
    PS2_VF_MIRROR(17, acp->loc);
    acp->ang[1] = Add_Angle(np->ang[1], acp->ang[1] - prv->parent_oldang[1]);
}

/* radar: ECM delays the update interval and can blank it for a while; every interval the range eases towards
 * 1.2 x zoom range when an enemy is within it, else the normal range */
static void __UpdateRadar(AcPlayerSt *acp)
{
    PS2_FRAME(0x60);
    AcCtrlPrivateSt *prv = acp->prvp;
    int iv;
    if (!(acp->rateEcmError <= 0.0f)) {
        acp->syserr |= 4;
        float rate = acp->rateEcmError;
        iv = ps2_ftoi((float)acp->param.rd_intvl + rate * g_asmparam.ecmerr_radar_delay);
        if (!acp->noradar_time) {
            float th = g_asmparam.ecmerr_radar_vanishrate * rate;
            if (!(th <= (float)ps2_rand() / 2147483648.0f))
                acp->noradar_time = (short)ps2_ftoi(acp->rateEcmError * g_asmparam.ecmerr_radar_vanishtime);
        }
    } else
        iv = acp->param.rd_intvl;
    if (prv->radar_nodirtime > 0) prv->radar_nodirtime--;
    prv->radar_dirdisp = prv->radar_nodirtime == 0;
    if (acp->param.radar_stat & 0x10) acp->noradar_time = 0;
    if (acp->noradar_time > 0) acp->noradar_time--;
    if (acp->noradar_time) {
        acp->radar_range = 0.0f;
        acp->syserr |= 4;
        return;
    }
    if (iv <= 0 || FRAME % (unsigned int)iv) return;
    memcpy(acp->rangeCenter, acp->loc, 16);
    memcpy(acp->rangeAng, acp->ang, 16);
    PS2_STACK(float, dist, 0x5c);
    float t;
    if (DF_CALC_GetNearestEnemy(acp->loc, acp->param.zoom_range, &dist, acp->id) && dist <= acp->param.zoom_range)
        t = 1.2f * acp->param.zoom_range;
    else
        t = acp->param.radar_range;
    acp->radar_range = acp->radar_range + 0.1f * (t - acp->radar_range);
    DF_RADER_SetRaderStruct(acp->radar_range, acp->id);
}

/* versus missile interception: for each incoming missile in range and angle, the core's machine gun fires at it
 * (shot rate % or every other missile, error rate % misses), else the extension's interceptor (misalert_func) */
static void ac_vsmissile_proc(AcPlayerSt *acp)
{
    PS2_FRAME(0xd0);
    AcCtrlPrivateSt *prv = acp->prvp;
    PS2_STACK(float[4], v, 0xb0);
    PS2_STACK(float[2], ang, 0xc8);
    for (unsigned int i = 0; i < 16; i++) {
        struct_f6a33bd0 *e = &prv->missile_buff[i];
        _spell2 *m = e->magp;
        if (!m) continue;
        if (m->on <= 0) {
            e->magp = nullptr;
            continue;
        }
        memcpy(v, m->pos, 16);
        for (int k = 0; k < 3; k++) v[k] = m->pos[k] - acp->loc[k];
        v[1] = v[1] + 0.5f * acp->tall;
        float xx = v[0] * v[0], zz = v[2] * v[2], yy = v[1] * v[1];
        float d2 = zz + (xx + yy);
        DF_CALC_Vector2Angle(ang, v);
        ang[0] = Add_Angle(ang[0], -acp->view.ang[0]);
        ang[1] = Add_Angle(ang[1], 3.14159274f - acp->ang[1]);
        AcPlayerParamSt *pp = &acp->param;
        float ax = ang[0] < 0.0f ? -ang[0] : ang[0];
        float ay = ang[1] < 0.0f ? -ang[1] : ang[1];
        if (!(pp->vs_mgun_range2J < d2) && !(pp->vs_mgun_angx < ax) && !(pp->vs_mgun_angy < ay) &&
            ((ps2_mod32(ps2_rand(), 0x190000) >> 14) < pp->vs_mgun_shotrate || (prv->vs_mgun_cnt & 1))) {
            e->magp = nullptr;
            prv->vs_mgun_cnt++;
            _stSpell2Data *spl = AcPtsRes_GetSpellPtr(acp->prvp->ptsresp, 0, 1);
            _stSfxCombHead *comb = AcPtsRes_GetSfxCombHead(acp->prvp->ptsresp, 0);
            acp->exestat |= 1;
            int err = pp->vs_mgun_errrate;
            _spell2 *tgt = m;
            if ((ps2_mod32(ps2_rand(), 0x190000) >> 14) < err) tgt = nullptr;
            if (DF_SPELL2_AppearSpell2Ptr(spl, 2, (char)acp->id, nullptr, comb, &acp->ocmats[8], nullptr, tgt)) return;
        }
        if (!acp->misalert_func) continue;
        if (pp->exvs_mgun_range2J < d2) continue;
        ay = ang[1] < 0.0f ? -ang[1] : ang[1];
        if (pp->exvs_mgun_angy < ay) continue;
        if (!((ps2_mod32(ps2_rand(), 0x190000) >> 14) < pp->exvs_mgun_shotrate || (prv->exvs_mgun_cnt & 1))) continue;
        e->magp = nullptr;
        prv->exvs_mgun_cnt++;
        if (gcall(acp->misalert_func, acp, m)) return;
    }
}

/* ---- guards, damage and knock-back ---- */

/* the core extension's side shield (busy and a shield type) covers the hit position: either angle inside its range
 * is enough (the arm shield needs both) */
int acsideshld_IsGuardEnable(AcPlayerSt *acp, float *loc)
{
    PS2_FRAME(0x60);
    PartsParamExtensionSt *extp = acp->param.extp;
    AcPartsExtSt *ext = AcInst_GetPartsExt(acp);
    if (!extp || !ext || !ext->sshield || !ext->busy) return 0;
    float ax, ay;
    acctrl_hit_angle(acp, loc, &ax, &ay);
    return ax <= 0.017453292f * (float)extp->def_angx || ay <= 0.017453292f * (float)extp->def_angy;
}

int acshield_IsGuardEnable(AcPlayerSt *acp, float *wshotpos)
{
    PS2_FRAME(0x40);
    if (acp->shieldstat != 1) return 0;
    return acctrl_shield_guard(acp, wshotpos);
}

/* stagger frames of a hit: 64 * react * dmgreact2frame / the AC's reaction defence, 0..60 (60 without defence) */
static int acctrl_react_time(unsigned char react, int defreact)
{
    int t = defreact == 0 ? 0x3c : ps2_ftoi(64.0f * ((float)react * g_asmparam.dmgreact2frame) / (float)defreact);
    if (t < 0) t = 0;
    if (t > 0x3c) t = 0x3c;
    return t;
}

/* pad vibration of a hit (only on a pad that views this AC); returns the power */
static unsigned char ac_add_damagevibra(AcPlayerSt *acp, SfxDstat *dstp, int defreact)
{
    PS2_FRAME(0x20);
    if (MyI.padoff == 1 || (acp->stat & 0x4000)) return 0;
    int t = acctrl_react_time(dstp->react, defreact);
    if (!t) return 0;
    int r = dstp->react;
    /* defreact 0 reaches the division too (t = 60): the EE's divide-by-zero result */
    unsigned char pow = (unsigned char)ps2_div32((unsigned char)(r * 255 / 48) << 7, defreact);
    int time = t + 10;
    if (time < 0xf) time = 0xf;
    if (time > 0x3c) time = 0x3c;
    if (acctrl_vibra_view(acp)) Vibra_SetPower(&acp->prvp->vibramng, pow, (short)time);
    return pow;
}

/* Knock-back velocity of a hit -> reactvec (w 0): away from the blast centre for explosions (effect 0x200), else
 * along the shot, bent 20% away from the hit position, scaled by the hit's react, limited per axis together with
 * the AC's own move this frame. Returns the knock-back direction relative to the AC's facing. */
static float get_std_reactvec(float *reactvec, AcPlayerSt *acp, SfxDstat *dstp, float *shotvec, float *hitpos,
                              float reactrate)
{
    PS2_FRAME(0x80);
    float t[4] __attribute__((aligned(16))), d[4] __attribute__((aligned(16)));
    if (dstp->effect & 0x200) {
        memcpy(t, acp->loc, 16);
        t[1] = t[1] - 0.5f * acp->tall;
        t[0] = t[0] - hitpos[0];
        t[1] = t[1] - hitpos[1];
        t[2] = t[2] - hitpos[2];
        vu_normalize(reactvec, t);
    } else {
        const float eps = 1e-7f;   /* 0x33d6bf95: near-zero shot components count as 0 */
        for (int k = 0; k < 3; k++)
            t[k] = (shotvec[k] - eps <= 0.0f && 0.0f <= eps + shotvec[k]) ? 0.0f : shotvec[k];
        t[3] = 1.0f;
        vu_normalize(reactvec, t);
    }
    d[0] = acp->loc[0] - hitpos[0];
    d[1] = acp->loc[1] - hitpos[1];
    d[2] = acp->loc[2] - hitpos[2];
    vu_normalize(d, d);
    for (int k = 0; k < 3; k++) reactvec[k] = 0.8f * reactvec[k] + 0.2f * d[k];
    float dang = ps2_atan2f(reactvec[0], reactvec[2]) - acp->ang[1];
    acctrl_wrap_ang(&dang);

    float s = reactrate * ((g_asmparam.reactspdstep / 100.0f) * (float)dstp->react);
    float h = 0.5f * s;
    reactvec[0] = reactvec[0] * h;
    reactvec[1] = reactvec[1] * s;
    reactvec[2] = reactvec[2] * h;
    float lim = g_asmparam.react_maxxzspd;
    if (acp->R - 0.01f <= lim) lim = acp->R - 0.01f;
    AddF_minmax(&reactvec[0], acp->loc[0] - acp->oloc[0], -lim, lim);
    AddF_minmax(&reactvec[2], acp->loc[2] - acp->oloc[2], -lim, lim);
    lim = g_asmparam.react_maxyspd;
    if (0.5f * acp->tall - 0.01f <= lim) lim = 0.5f * acp->tall - 0.01f;
    AddF_minmax(&reactvec[1], acp->loc[1] - acp->oloc[1], -lim, lim);
    if (dstp->effect & 0x200) reactvec[1] = reactvec[1] * g_asmparam.yreact_sphereexp_rate;
    return dang;
}

/* Reaction to a hit: vibration, the "hit this frame" flag, then (unless the arm shield guards it) a short stiffness
 * for light hits, or an air / ground damage action in one of four directions with the knock-back velocity. */
void AcCtrl_DamageReaction(AcPlayerSt *acp, SfxDstat *dstp, float *hitvec, float *hitpos, int damage)
{
    PS2_FRAME(0xa0);
    PS2_STACK(float[4], react, 0x80);   /* handed to acact_SetGndDamage */
    ac_add_damagevibra(acp, dstp, acp->defence.react);
    if (MyI.padoff == 1 || (acp->stat & 0x4000)) {
        if (acp->hp) acp->hp = acp->hp + damage;
    } else if (damage > 0) acp->stat |= 2;
    if (acp->actno == 0x12) return;
    if (acp->shieldstat == 1 && acctrl_shield_guard(acp, hitpos)) return;
    int t = acctrl_react_time(dstp->react, acp->defence.react);
    if (t <= 0) return;
    float dang = get_std_reactvec(react, acp, dstp, hitvec, hitpos, 0.8f);
    if (acp->actno == 0x10) {
        if (!(dstp->effect & 0x200)) {
            acp->velocity[0] = react[0];
            acp->velocity[2] = react[2];
            if (dstp->effect & 0x200) acp->velocity[1] = react[1];   /* dead: excluded above */
            return;
        }
        if (!acp->wep_release) AcWep_ReleaseWeapon(acp, acp->equip_weapon);
    }
    if ((float)t < g_asmparam.react_kind_threshold_shock) {
        acp->stifftime = (short)t;
        if (acp->stifftime >= 6) acp->stifftime = 5;
        return;
    }
    int q = ps2_ftoi(4.0f + (0.7853982f + dang) / 1.5707964f) & 3;   /* quadrant of the knock-back direction */
    if ((acp->actstat & 1) || react[1] <= ps2_u2f(0xbf999999u) /* -1.2 */) {
        static const unsigned char air[4] = {0x2c, 0x2a, 0x27, 0x2b};
        int anim = air[q];
        AcAnim_ChangeAnim(acp, anim, 0x14, 0);
        acp->velocity[0] = react[0];
        acp->velocity[1] = 0.5f * react[1];
        acp->velocity[2] = react[2];
        acact_SetAirDamage(acp, anim, t);
    } else {
        static const unsigned char gnd[4] = {0x34, 0x37, 0x35, 0x36};
        acact_SetGndDamage(acp, gnd[q], react, t);
    }
}

/* Takes AP (unless control is off, frozen or no-damage): at 0 AP the AC starts dying (an "undying" AC stays at 1),
 * else flags AP < 10% and reacts. */
void AC_add_damage(AcPlayerSt *acp, SfxDstat *dstp, float *hitvec, float *hitpos, int damage)
{
    PS2_FRAME(0x20);
    if (MyI.padoff == 1 || (acp->stat & 0x4000) || (acp->spflag & 2)) return;
    int h = acp->hp - damage;
    if ((acp->spflag & 0x10) && h <= 0) h = 1;
    if (h <= 0) {
        acp->hp = 0;
        if (!acp->dying_cnt) {
            if (acp->id == 0 && !MyI.deadstop) EVMNG_AllDelete();
            acp->view.ang[0] = 0.0f;
            acp->dying_cnt = 1;
            SFX_Dying(acp->mp, 0x96);
        }
        if (acp->actno == 0x12) acob_Close(AcInst_GetPartsOb(acp));
        acext_Close(AcInst_GetPartsExt(acp));
        return;
    }
    acp->hp = h;
    if (acp->hp * 10 < acp->param.hp) acp->stat |= 8;
    AcCtrl_DamageReaction(acp, dstp, hitvec, hitpos, damage);
}

/* AP damage of a hit: shell / energy defence (per 64), 2/3 / 50, the player / AC damage rate, then the mode's
 * rate (versus damage, or the mission rescue reduction) */
static int ac_damage_calc(AcPlayerSt *acp, SfxDstat *dstp, char stype)
{
    int d = dstp->damage;
    int k = dstp->effect & 0x30;
    if (k == 0x20) d = (int)((unsigned int)d * (unsigned int)acp->defence.energy) >> 6;
    else if (k == 0x10) d = (int)((unsigned int)d * (unsigned int)acp->defence.shell) >> 6;
    d = d * 2 / 3 / 50;
    int v = ps2_ftoi((float)d * (stype == 2 ? g_asmparam.ac_damagerate : g_asmparam.p1_damagerate));
    switch (sortie_mode()) {
    case 4: case 5:
        v = ps2_ftoi((float)v * g_asmparam.versus_damage);
        break;
    case 0: case 2: case 3:
        v = ps2_ftoi((float)v * (1.0f - g_sortie.pconstant->rateRescue));
        break;
    }
    return v;
}

/* ---- floor / wall / water collision ---- */

#define HIT_INFO ((HitInfoWkSt *)PS2_SPR(0))             /* collision result in scratchpad (collision/hitwk.h) */
#define HIT_KEEP (*(unsigned char *)PS2_SPR(0x67))      /* bit 0: DF_HIT_HitProc_YUKA keeps parent fallback */

/* remember the floor model (or none) the AC stands on, for the next frame's carry (__CarryAc) */
static void acctrl_parent_snapshot(AcPlayerSt *acp, StModel *np)
{
    AcCtrlPrivateSt *prv = acp->prvp;
    if (np) {
        prv->old_parent = np;
        memcpy(acp->parent_oldpos, np->loc, 16);
        memcpy(prv->parent_oldang, np->ang, 16);
        sceVu0CopyMatrix(prv->parent_oldmat, np->matrix2);
    } else {
        prv->old_parent = 0;
        memcpy(acp->parent_oldpos, acp->loc, 16);
        memcpy(prv->parent_oldang, acp->ang, 16);
        sceVu0CopyMatrix(prv->parent_oldmat, IdentityMtx);
    }
}

/* tank legs: ground height under one of the four track corners (FLT_MAX if none) */
float AcCtrl_GetGndHigh(float *dstvec, AcPlayerSt *acp, int vertid)
{
    PS2_FRAME(0x40);
    float p[4] __attribute__((aligned(16))), q[4] __attribute__((aligned(16)));
    vu_apply(p, *acp->op[13].wrmatp, acp->tank_verts[vertid]);
    memcpy(q, p, 16);
    q[1] = q[1] - acp->tall;
    HIT_KEEP |= 1;   /* never cleared here (ac_get_floor_high clears it) */
    if (!DF_HIT_HitProc_YUKA(acp->mp, q, 10.0f + acp->tall, 0.001f, dstvec)) {
        dstvec[0] = q[0];
        dstvec[1] = ps2_u2f(0x7f7fffffu);
        dstvec[2] = q[2];
        dstvec[3] = 1.0f;
    }
    return dstvec[1];
}

/* floor height under the AC's model (stage floor or another character), floor normal -> norm; also takes the
 * parent snapshot. loc / yofs are dead: the probe always starts 3 above the model. */
static float ac_get_floor_high(AcPlayerSt *acp, float *loc, float yofs, float *norm)
{
    PS2_FRAME(0xc0);
    StModel *mp = acp->mp;
    float R = acp->R - 0.02f;
    float p[4] __attribute__((aligned(16))), fy[4] __attribute__((aligned(16))), cf[4] __attribute__((aligned(16)));
    memcpy(p, loc, 16);
    p[1] = p[1] + yofs;
    HIT_KEEP &= ~1;
    memcpy(p, mp->loc, 16);
    p[1] = p[1] + 3.0f;
    int s0 = HitBoxHitProcYuka(mp, p, 3.0f + mp->tall, R, &fy[1]);
    if (s0 == 1) memcpy(norm, PS2_SPR(0x20), 16);
    if (DF_HIT_HitProc_YUKA_Char(acp->id, p, 100000.0f, R, cf)) {
        cf[1] = cf[1] - 0.01f;
        if (cf[1] < fy[1]) {
            mp->yukaattribute = 0xb;
            s0 = 0;
            fy[1] = cf[1];
        }
        memcpy(norm, PS2_SPR(0x20), 16);
    } else if (!s0) {
        fy[0] = p[0];
        fy[1] = 100000.0f + p[1];
        fy[2] = p[2];
    }
    acctrl_parent_snapshot(acp, s0 ? mp->nowparent : 0);
    norm[3] = 1.0f;
    return fy[1];
}

/* place the AC (script / elevator): zero velocity, floor / water heights, fall or stand */
void AC_SetPosition(AcPlayerSt *acp, float *pos)
{
    PS2_FRAME(0x80);
    float dest[4] __attribute__((aligned(16))), norm[4] __attribute__((aligned(16))), p[4] __attribute__((aligned(16)));
    float wy;
    acp->velocity[0] = 0.0f;
    acp->velocity[1] = 0.0f;
    acp->velocity[2] = 0.0f;
    acp->velocity[3] = 1.0f;
    memcpy(acp->loc, pos, 16);
    memcpy(acp->oloc, pos, 16);
    DF_HIT_DecideParent(acp->mp, acp->loc, 100000.0f, acp->mp->R, dest);
    acp->floor_high = ac_get_floor_high(acp, pos, -acp->prvp->climb_high, norm);
    memcpy(p, acp->mp->loc, 16);
    p[1] = p[1] + 3.0f;
    HitBoxHitProcWater(acp->mp, p, 3.0f + acp->mp->tall, acp->R, &wy);
    acp->water_high = wy;
    if (acp->loc[1] < acp->floor_high) AC_SetAction(acp, 8);
    else ACsetact_stop(acp);
}

/* After the move (HitFlag from ac_hitcheck: 1 floor, 2 wall, 4 ceiling, 8 standing on a character, 0x10 pushed
 * sideways by a character, 0x20 character above, 0x40 in water): wall speed loss, ceiling / floor velocity, water
 * standing, falling off an edge or landing (land actions, ride scripts / messages to the floor model), floor light. */
static void __ACHitControl(AcPlayerSt *pAC, float *MoveVec, unsigned int HitFlag, int isWaterStand)
{
    PS2_FRAME(0x70);
    float y = pAC->floor_high;
    if (HitFlag & 0x12) {   /* speed lost to the push: keep the speed minus 30% of the lost part */
        float vx = pAC->velocity[0], vz = pAC->velocity[2];
        float m2 = MoveVec[2] * MoveVec[2] + MoveVec[0] * MoveVec[0];
        float lost = (vx * vx + vz * vz) - m2;
        if (!(lost <= 0.0f)) {
            float sl = ps2_sqrt(lost), sp = ps2_sqrt(vx * vx + vz * vz);
            float s = sp - 0.3f * sl;
            if (s <= 0.0f) {
                pAC->velocity[2] = 0.0f;
                pAC->velocity[0] = 0.0f;
            } else {
                pAC->velocity[0] = vx * s / sp;
                pAC->velocity[2] = pAC->velocity[2] * s / sp;
            }
            if (pAC->actno == 0x12 && !(sl < 2.0f * pAC->param.oboost_acc)) acob_Close(AcInst_GetPartsOb(pAC));
        }
    }
    if ((HitFlag & 8) && !(HitFlag & 1)) {
        pAC->mp->yukaattribute = 0xb;
        pAC->velocity[1] = 0.0f;
    }
    if (HitFlag & 1) acctrl_parent_snapshot(pAC, pAC->mp->nowparent);
    if ((HitFlag & 4) && pAC->velocity[1] < -0.01f) pAC->velocity[1] = -0.01f;
    if (HitFlag & 0x40) {
        pAC->mp->WatY = pAC->water_high;
        if (isWaterStand) {   /* note: y becomes loc.y even below the surface */
            y = pAC->loc[1];
            if (!(y <= pAC->water_high)) y = 0.7f * pAC->loc[1] + 0.3f * (0.001f + pAC->water_high);
        }
    }
    if (!(y - pAC->loc[1] <= 2.9f)) {   /* floor far below: start falling */
        if (!(pAC->actstat & 1)) {
            if (pAC->actno == 0x10) AcWep_ReleaseWeapon(pAC, pAC->equip_weapon);
            AC_SetAction(pAC, 8);
            pAC->velocity[1] = 0.5f * ps2_sqrt(MoveVec[0] * MoveVec[0] + MoveVec[2] * MoveVec[2]);
        }
    } else if (!(pAC->velocity[1] < 0.0f)) {
        float dy = pAC->loc[1] - y;
        if ((HitFlag & 9) || (!(pAC->actstat & 1) && !(HitFlag & 4) && (dy < 0.0f ? -dy : dy) < 1.0f) ||
            (isWaterStand && (HitFlag & 0x40))) {
            pAC->loc[1] = y;
            if (pAC->actno == 0x12) {
                if (pAC->param.prog_no == 0) pAC->effectstat |= 4;
            } else {
                unsigned short a = pAC->actno;
                if (a == 8 || a == 0xd || a == 0xf || (a == 0x16 && (pAC->actstat & 1))) {
                    unsigned char pn = pAC->param.prog_no;
                    if (pn != 1 && pn != 3 && !(pAC->velocity[1] <= pAC->param.land_dmgspd)) acact_SetDamageLand(pAC);
                    else if (pn == 3) ACsetact_stop(pAC);
                    else acact_SetLand(pAC);
                    if (pAC->param.prog_no == 3) pAC->effectstat |= 0x180;
                    else pAC->effectstat |= 0x200;
                    if (MyI.padoff != 1 && pAC->wep_release) AcWep_EquipWeapon(pAC, pAC->equip_weapon);
                }
                StModel *mp = pAC->mp, *np;
                if (!(HitFlag & 8) && !(mp->use & 2) && (np = mp->nowparent)) {
                    if (np->array->rideid) DF_ScriptStart(np->array->rideid, np);
                    /* first argument arg0p (gimmick convention), not the model */
                    if (np->arg0p && np->msgfunc)
                        gcall_as<void(void *, int, StModel *)>(np->msgfunc, np->arg0p, 1, pAC->mp);
                }
            }
            pAC->velocity[1] = 0.0f;
        }
    }
    StModel *mp = pAC->mp;
    mp->LocY = pAC->floor_high;
    mp->yGround = y;
    /* the floor model's light list (nowparent is not tested for NULL) */
    const StModel *np = mp->nowparent ? mp->nowparent : (const StModel *)PS2_NULL_MEM();
    if (np->mlist && np->mlist->lp_chrlight != mp->llist) {
        mp->llistbk = mp->llist;
        mp->llist = np->mlist->lp_chrlight;
        mp->ylfrm = 0;
    } else if (++mp->ylfrm >= 0xd) mp->ylfrm = 0xc;
}

/* One frame's collision for the move velocity + floor carry: character push, walls, floor / ceiling (with a line
 * probe down for floors further below), water, a second character pass, then the new position. */
static void ac_hitcheck(AcPlayerSt *acp)
{
    PS2_FRAME(0x130);
    static const float down[4] __attribute__((aligned(16))) = {0.0f, 1.0f, 0.0f, 1.0f};   /* 0x25ab30, no DWARF name */
    StModel *mp = acp->mp;
    float R = acp->R, tall = acp->tall;
    unsigned int flag = 0;
    int ws = 0;
    StModel *wpar = 0;
    float y23 = 0.0f;   /* top of the character stood on / below: only read when the first push set flag 8 / 0x20 */
    if (mp->use & 2) return;
    float base[4] __attribute__((aligned(16))), mv[4] __attribute__((aligned(16))), pos[4] __attribute__((aligned(16)));
    float out[4] __attribute__((aligned(16))), ls[4] __attribute__((aligned(16))), dir[4] __attribute__((aligned(16)));
    float lh[4] __attribute__((aligned(16)));
    float yw;
    memcpy(base, acp->loc, 16);
    for (int k = 0; k < 3; k++) mv[k] = acp->velocity[k] + acp->prvp->parent_movevec[k];
    mv[3] = acp->velocity[3];
    auto addv = [&] {   /* pos = base + mv (xyz), w = base.w */
        for (int k = 0; k < 3; k++) pos[k] = base[k] + mv[k];
        pos[3] = base[3];
    };
    /* character push: mostly vertical -> standing on (8) / under (0x20) a character (returns true), else sideways */
    auto chr_push = [&]() -> bool {
        if (!HitChrHitProcChr(mp, pos, tall, R, out)) return false;
        if (out[0] * out[0] + out[2] * out[2] < out[1] * out[1]) {
            flag |= 0x10;
            mv[0] = mv[0] + out[0];
            mv[2] = mv[2] + out[2];
            return false;
        }
        if (out[1] <= 0.0f) {
            flag |= 8;
            mp->nowparent = HIT_INFO->pHitMdl->nowparent;
        } else flag |= 0x20;
        mv[1] = mv[1] + out[1];
        return true;
    };

    addv();
    if (chr_push()) y23 = base[1] + mv[1];

    addv();
    pos[1] = pos[1] - (0.1f + mv[1]);
    if (HitBoxHitProcKabe(mp, pos, tall - 0.1f, R, out)) {
        flag |= 2;
        mv[0] = mv[0] + out[0];
        mv[2] = mv[2] + out[2];
    }

    addv();
    pos[1] = pos[1] + 3.0f;
    HitBoxHitProcYuka(mp, pos, 6.0f + tall, R - 0.1f, &yw);
    if (!(base[1] - 3.0f <= yw) && 3.0f + (base[1] - tall) < yw) {
        mv[1] = 0.0f;   /* the floor is above the AC's feet: no vertical move (water probe skipped, pos.y keeps +3) */
    } else {
        if (!(yw <= pos[1] - (3.0f + 0.4f * tall))) {   /* a floor */
            acp->tenjo_high = pos[1] - (3.0f + tall);
            if ((flag & 8) && y23 < yw) acp->floor_high = y23;
            else {
                flag &= ~8u;
                acp->floor_high = yw;
            }
        } else {   /* a ceiling */
            if ((flag & 0x20) && !(y23 <= yw)) acp->tenjo_high = y23;
            else {
                acp->tenjo_high = yw;
                flag &= ~0x20u;
            }
            acp->floor_high = pos[1];
        }

        addv();
        if (HitBoxHitProcWater(mp, pos, tall, R, &yw)) {
            wpar = HIT_INFO->pHitMdl;
            acp->water_high = yw;
        } else acp->water_high = ps2_u2f(0x7f7fffffu);

        if (!(flag & 8)) {
            if (acp->floor_high <= pos[1]) {
                mv[1] = acp->floor_high - pos[1];
                flag |= 1;
            } else {   /* in the air: probe 50 down from the body centre for the floor model and other characters */
                memcpy(dir, down, 16);
                memcpy(ls, pos, 16);
                ls[1] = ls[1] - 0.5f * tall;
                mp->nowparent = HitBoxLineDecideParent(ls, 0, lh);
                if (HitChrHitProcLine(ls, dir, 50.0f, 4, acp->id) && HIT_INFO->hitpos[1] < lh[1]) {
                    acp->floor_high = lh[1];
                    mp->yukaattribute = 0xb;
                } else {
                    if (!(lh[1] <= 3.0f + (2.9f + acp->floor_high))) acp->floor_high = lh[1];
                    mp->yukaattribute = 0;
                }
            }
        }
    }

    if (!(acp->tenjo_high <= pos[1] - tall) && !(flag & 1)) {
        float c = acp->tenjo_high - (pos[1] - tall);
        if (mv[1] < c) {
            mv[1] = c;
            flag |= 4;
        }
    }
    if (acp->water_high < pos[1]) {
        flag |= 0x40;
        ws = __IsEnableWalkOnWater(acp);
        if (wpar) mp->nowparent = wpar;
    } else acp->water_high = acp->water_high + 75.0f;

    if ((flag & 0x21) == 0x21 || (flag & 0xc) == 0xc) {
        addv();
        if (chr_push() && acp->floor_high < base[1] + mv[1]) mv[1] = acp->floor_high - base[1];
    }
    for (int k = 0; k < 3; k++) acp->loc[k] = base[k] + mv[k];
    acp->loc[3] = base[3];
    __ACHitControl(acp, mv, flag, ws);
}

/* ---- model / effect init (once per sortie, from __InitPlayer) ---- */

/* Attach matrices: ocmats[i] follows object `id` of a model (GAcUtil_GetObjCmat returns -1 if it has none).
 * Fixed slots: 0-3 body points, 4 / 5 the arm weapons, 6 / 7 hand points, 8-12 core / legs, 13 eye, 14 / 15 back
 * boosters, 16-19 leg boosters, 20-23 core options (EO / OB), 24 / 25 subleg or OB sub boosters, 26 / 27 the arm
 * blades / shield. Counts the boosters found into the sfx state. */
static void GetObjChildMatrix(AcPlayerSt *acp)
{
    PS2_FRAME(0x40);
    StModel *mp = acp->mp;
    AcSfxSt *sfx = acp->frdp->sfxp;
    auto get = [&](int i, StModel *m, float id) { return GAcUtil_GetObjCmat(&acp->ocmats[i], m, id, 1); };
    get(0, mp, 0.0f);
    get(1, mp, 2.0f);
    get(2, mp, 4.0f);
    get(3, mp, 6.0f);
    get(4, mp, (float)acp->param.Larm_equipcolor);
    get(5, mp, (float)acp->param.Rarm_equipcolor);
    get(6, mp, 40.0f);
    get(7, mp, 41.0f);
    get(8, mp, 61.0f);
    get(9, mp, 63.0f);
    get(10, mp, 64.0f);
    get(11, mp, 65.0f);
    get(12, mp, 66.0f);
    sfx->nbackboost = acp->param.boosterp ? 2 : 0;
    get(13, mp, 69.0f);
    get(14, acp->boostmp, 72.0f);
    get(15, acp->boostmp, 73.0f);
    Model_DeleteDiscernVertex(acp->boostmp);
    sfx->nlegboost = 0;
    for (int k = 0; k < 4; k++)
        if (get(16 + k, mp, (float)(74 + k)) != -1) sfx->nlegboost++;
    /* sub boosters: on the sub legs for leg program 1, else on the core option model (nobsubboost not reset here) */
    StModel *sub = acp->param.prog_no == 1 ? acp->sublegmp : acp->eobmp;
    for (int k = 0; k < 2; k++)
        if (get(24 + k, sub, (float)(100 + k)) != -1) sfx->nobsubboost++;
    if (acp->param.prog_no == 1) Model_DeleteDiscernVertex(acp->sublegmp);
    get(20, acp->eobmp, 12.0f);
    get(21, acp->eobmp, 13.0f);
    get(22, acp->eobmp, 14.0f);
    get(23, acp->eobmp, 15.0f);
    Model_DeleteDiscernVertex(acp->eobmp);
    if (acp->param.armsp->type == 2) {   /* weapon arms: blades on the arm model */
        get(26, mp, 12.0f);
        get(27, mp, 13.0f);
    } else get(26, acp->modeltbl[4], 12.0f);
    /* rows 0-2 reset to identity (row 3 = the position found above stays) */
    static const unsigned char ident[8] = {0, 1, 4, 5, 26, 27, 6, 7};
    for (int i = 0; i < 8; i++) memcpy(acp->ocmats[ident[i]].mat, IdentityMtx, 48);
}

/* boosters / blades / shield / eye effects of the AC's parts and the camera shake handle */
static void InitAcSfx(AcPlayerSt *acp)
{
    PS2_FRAME(0xa0);
    PS2_STACK(int[4], col, 0x90);   /* handed to DF_EYE2_AppearEye */
    AcCtrlPrivateSt *prv = acp->prvp;
    AcEffSt *eff = prv->effp;
    int nob = 0;
    switch (acp->param.core_type) {
    case 1:
        nob = AcInst_GetPartsOb(acp)->obnum;
        break;
    case 2: case 3:
        AcInst_GetPartsSlow(acp);
        prv->sfxp->nobsubboost = 0;
        break;
    }
    aceff_SetBoostParam(eff, 0);
    aceff_SetBoostParam(eff, 1);
    auto reg_boost = [&](int boostm, ObjCMatSt *oc, unsigned int mask, int setcolor) {
        aceff_RegisterBoost(eff, boostm, oc->parentmatp, &oc->mat, mask, setcolor);
    };
    if (acp->param.prog_no != 1 && acp->param.prog_no != 3) {
        if (prv->sfxp->nbackboost > 0) aceff_CopyBoostColor(eff, 1, 0);
        reg_boost(0, &acp->ocmats[14], 8, 0);
        reg_boost(1, &acp->ocmats[15], 8, 0);
    }
    if (acp->param.abl_shbooster) {   /* shoulder boosters on the back weapons */
        ObjCMatSt *r = &acp->weaponctrl[2].ocmats[0], *l = &acp->weaponctrl[1].ocmats[0];
        StModel *rm = acp->modeltbl[1], *lm = acp->modeltbl[0];
        aceff_SetBoostParam(eff, 3);
        GAcUtil_GetObjCmat(r, rm, 12.0f, 0);
        GAcUtil_GetObjCmat(l, lm, 12.0f, 0);
        if (prv->sfxp->nbackboost > 0) aceff_CopyBoostColor(eff, 3, 0);
        reg_boost(0xc, r, 8, 1);
        reg_boost(0xd, l, 8, 1);
    }
    unsigned int mask = (acp->param.prog_no == 1 || acp->param.prog_no == 3) ? 8 : 0xc;
    for (int i = 0; i < prv->sfxp->nlegboost; i++) reg_boost(i + 2, &acp->ocmats[16 + i], mask, 1);
    for (int i = 0; i < nob; i++) aceff_RegisterBlade(eff, i + 2, 0, 6, &acp->ocmats[20 + i]);
    for (int i = 0; i < prv->sfxp->nobsubboost; i++) reg_boost(i + 6, &acp->ocmats[24 + i], 8, 0);
    if (acp->param.prog_no == 1) {
        acsfx_ChangeMagicTable(0xd, 0x198);
        acsfx_ChangeMagicTable(0xe, 0x198);
        acsfx_ChangeMagicTable(0xf, 0x198);
    }
    int shake = DF_SHAKE_AppearShake(acp->loc, (unsigned int)-1);
    prv->sfxp->h_shake[0] = shake;
    switch (acp->param.Larm_type) {
    case 2:
        aceff_RegisterShield(eff, 0, 6, 2 /* leftover a3 */, &acp->ocmats[26]);
        break;
    case 0: case 1:
        if (acp->param.swordp) aceff_RegisterBlade(eff, 0, 6, 2, &acp->ocmats[26]);
        else if (acp->param.armsp->type == 2) {
            aceff_RegisterBlade(eff, 0, 2, 3, &acp->ocmats[26]);
            aceff_RegisterBlade(eff, 1, 2, 3, &acp->ocmats[27]);
        }
        break;
    }
    acp->eye_sfx = -1;
    if (acp->param.headp->with_monoeye) {
        col[0] = acp->param.colorEye[0];
        col[1] = acp->param.colorEye[1];
        col[2] = acp->param.colorEye[2];
        col[3] = 0x80;
        unsigned char *tp2 = AcPtsRes_GetMonoeyeTp2(prv->ptsresp);
        unsigned char *tm2 = AcPtsRes_GetMonoeyeTm2(prv->ptsresp);
        if (tp2 && tm2) acp->eye_sfx = DF_EYE2_AppearEye(tp2, tm2, acp->mp, &acp->op[4], &acp->ocmats[13], &col);
    }
}

/* Model setup: object table, anims and anim groups of the AC's models (unused weapon / extension models dropped),
 * the attach matrices, and the four tank-track corners in the main model's local space. */
static void InitAcModel(AcPlayerSt *acp)
{
    PS2_FRAME(0x1d0);
    StModel *mp = acp->mp;
    acp->smp = 0;
    acp->op = mp->op;
    AcAnim_InitModelAnimation(mp);
    AcAnim_InitModelAnimation(acp->boostmp);
    AcAnim_InitModelAnimation(acp->eobmp);
    if (acp->sublegmp) AcAnim_InitModelAnimation(acp->sublegmp);
    Anim_MakeGroup(mp, 4, 3);
    Anim_MakeGroup(mp, 3, 3);
    int g1, g2;
    if (acp->param.armsp->type == 1) g1 = g2 = 0x20;
    else {
        g1 = 9;
        g2 = 5;
    }
    for (int o = 5; o <= 8; o++) Anim_MakeGroup(mp, o, g1);
    for (int o = 9; o <= 0xc; o++) Anim_MakeGroup(mp, o, g2);
    short *pid = acp->param.partsid;
    if (!pid[11]) acp->modeltbl[0] = 0;
    if (!pid[10]) acp->modeltbl[1] = 0;
    if (acp->param.armsp->type == 0) {
        if (!pid[13]) acp->modeltbl[4] = 0;
        if (!pid[12]) acp->modeltbl[5] = 0;
    }
    if (!pid[9]) {
        acp->modeltbl[2] = 0;
        acp->modeltbl[3] = 0;
    }
    for (unsigned int i = 0; i < 8; i++) AcAnim_InitModelAnimation(acp->modeltbl[i]);   /* NULL entries too */
    if (acp->param.spswordp) AcAnim_InitModelAnimation(&MyLSW[acp->id]);
    if (acp->param.spgunp) AcAnim_InitModelAnimation(&MyRSW[acp->id]);
    GetObjChildMatrix(acp);
    for (int k = 0; k < 4; k++) {
        ObjCMatSt *oc = &acp->ocmats[9 + k];
        if (!oc->parentmatp) continue;
        float w[4] __attribute__((aligned(16))), d[4] __attribute__((aligned(16)));
        float t[4][4] __attribute__((aligned(16)));
        vu_apply(w, *oc->parentmatp, oc->mat[3]);
        sceVu0TransposeMatrix(t, *acp->op[13].wrmatp);
        const float *org = (*acp->op[13].wrmatp)[3];
        d[0] = w[0] - org[0];
        d[1] = w[1] - org[1];
        d[2] = w[2] - org[2];
        d[3] = w[3];
        vu_apply(acp->tank_verts[k], t, d);
        acp->tank_verts[k][3] = 1.0f;
    }
    acjcon_Init(&acp->Warm_ctrl);
    acjcon_Init(&acp->Rarm_ctrl);
}
