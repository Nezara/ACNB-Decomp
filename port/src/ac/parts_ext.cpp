/* src/ac/parts_ext.cpp - AC extension slot (back / shoulder extension, param.extp; models modeltbl[2] / [3]; OLSORTIE
 * overlay). 17 kinds by PartsParamExtensionSt.prog_no: boosters (0 / 1 / 5 / 7 / 8), link weapon (2), vs-weapons
 * (3 / 4 / 14 / 15), always-on NE shield (6), shield (9), stealth (10), coolant (11), energy pack (12), cure-status (13)
 * and add-magazine (16); 0x11 = none. Each kind installs open / busy / close / act function pointers in the private
 * struct (called (prvp->acp, extp)); acext_Control runs them from the pad every frame. Also item pickup / removal of
 * the extension, its weight / vs-weapon parameters, destroy info and the shot count.
 *
 * The kinds' init functions are reached through extension_initfunc (acext_Init); every slot function has the uniform
 * (acp, extp[, partsp]) prototype (protofix: MWCC dropped the unused parameters from DWARF). */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"

typedef int (*ExtSlotFn)(void *, AcPartsExtSt *);
typedef void (*ExtActFn)(void *, AcPartsExtSt *);
#define EXT_SLOT(f) ((ExtSlotFn)GFN(f))
#define EXT_ACT(f) ((ExtActFn)GFN(f))

static const float EXT_PI = ps2_u2f(0x40490fdb);

/* item record of a dropped extension (DF_ITM_GetDataAddr, no DWARF type) */
struct AcExtItemRec {
    unsigned char kind, sub, count, _pad3;   /* 0x0: kind 7, sub 8; count = shots / bullets left */
    short partsid;                           /* 0x4 */
    short _pad6;
    int hp;                                  /* 0x8 */
    PartsParamExtensionSt *extp;             /* 0xc */
    _stSfxCombHead *comb;                    /* 0x10 */
};

/* prv->actfunc = f, new step and time */
static inline void ext_set_act(AcPartsExtSt *extp, ExtActFn f)
{
    extp->prvp->actfunc = f;
    extp->prvp->actstep = 0;
    extp->acttime = 0;
}

/* the kind's close function (busy = 0 when it returns nonzero), or the default close animation */
static inline void ext_close_or_default(AcPartsExtSt *extp)
{
    AcPartsExtPrivateSt *prv = extp->prvp;
    if (prv->closefunc) {
        if (gcall(prv->closefunc, (void *)prv->acp, extp)) extp->busy = 0;
    } else {
        prv->actfunc = EXT_ACT(Default_ExtensionClose);
        extp->prvp->actstep = 0;
        extp->acttime = 0;
        extp->busy = 0;
    }
}

/* one use: the 9999 "infinite" test is dead (shot_cnt > 0 already holds), so the counter always decrements */
static inline void ext_use_shot(AcPartsExtSt *extp)
{
    extp->shot_cnt = (short)(extp->shot_cnt - 1);   /* original: if (n > 0 || n < 9999) n--; (always true) */
}

/* open (anim 1) / close (anim 2) animation of both extension models; 1 when model 3 finished (or is missing) */
static int ext_models_anim(AcPlayerSt *acp, int animid)
{
    Anim_ChangeAnim(acp->modeltbl[2], animid, 10, 0);
    Anim_ChangeAnim(acp->modeltbl[3], animid, 10, 0);
    return acp->modeltbl[3] ? Anim_IsEnd(acp->modeltbl[3]) : 1;
}

/* the extension's magic effect at every muzzle matrix */
static void ext_magic_muzzles(AcPartsExtSt *extp)
{
    if (!extp->prvp->magcp || !extp->prvp->cmbp) return;
    for (int i = 0; i < extp->nmuzzle; i++)
        DF_MAGIC2_AppearMagicOCMPPtr(extp->prvp->magcp, extp->prvp->cmbp, &extp->prvp->ocmats[i], 4);
}

/* vs machine gun parameters of the extension (acext_SetParam, inlined in acext_PickupExtension) */
static inline void ext_set_vsmgun(AcPlayerParamSt *paramp, PartsParamExtensionSt *extpramp)
{
    paramp->exvs_mgun_shotrate = extpramp->vs_mgun_sr;
    paramp->exvs_mgun_errrate = extpramp->vs_mgun_er;
    int r = extpramp->vs_mgun_range;
    paramp->exvs_mgun_range2J = (float)(r * r);
    paramp->exvs_mgun_angy = (EXT_PI * (float)extpramp->vs_mgun_angy) / 180.0f;
    if (paramp->plus & 0x100) {
        paramp->exvs_mgun_shotrate += 10;
        if (paramp->exvs_mgun_shotrate >= 0x65) paramp->exvs_mgun_shotrate = 100;
    }
}

short acext_GetShotCount(AcPartsExtSt *pExt)
{
    return pExt->shot_cnt;
}

AcDptsSt *acext_GetDestroyInfo(AcPartsExtSt *extp)
{
    return &extp->prvp->dpts;
}

int acext_Remove(AcPartsExtSt *extp, int lost)
{
    return __RemoveExtension(extp->prvp->acp, extp, lost);
}

/* puts a dropped extension item onto an AC without one */
int acext_PickupExtension(AcPlayerSt *acp, int itmid)
{
    PS2_FRAME(0x80);
    AcPlayerParamSt *param = &acp->param;
    AcPartsExtSt *ep = AcCtrl_GetPartsExt(acp);
    _weapon_st *wp = &acp->param.extweapon;
    if (acp->modeltbl[2] || acp->modeltbl[3] || acp->param.extp) return 0;
    AcExtItemRec *d = (AcExtItemRec *)DF_ITM_GetDataAddr(itmid);
    if (!d) return 0;
    acp->param.partsid[9] = d->partsid;
    PartsParamExtensionSt *extparam = d->extp;
    acp->param.extp = extparam;
    AcPtsRes_ReRegisterWsfx(acp->frdp->ptsresp, 9, (char *)d->comb);
    AcAsm_CalcWeightEx(param);
    AcAsm_GetWeaponParamEx(&param->extweapon, 9, 0, extparam->weapon_id, 7, 0, 0, param);
    param->extweapon.jpos = 8;
    AcAsm_GetExboostParam(param);
    ext_set_vsmgun(param, extparam);
    float r = param->vs_mgun_range2J < param->exvs_mgun_range2J ? param->exvs_mgun_range2J : param->vs_mgun_range2J;
    acp->mp->vs_range = r;
    acp->mp->vs_range = ps2_sqrt(acp->mp->vs_range);
    StModel *m2 = (StModel *)DF_ITM_GetModel(itmid, 0);
    StModel *m3 = (StModel *)DF_ITM_GetModel(itmid, 1);
    acp->modeltbl[2] = m2;
    acp->modeltbl[3] = m3;
    if (m2) m2->scale[0] = 1.0f;
    if (m3) m3->scale[0] = 1.0f;
    acext_Init(AcCtrl_GetPartsExt(acp), extparam, acp);
    wp->jpos = 8;
    /* ep is the slot read before acext_Init; its prog_no is the new one (read without the null test) */
    switch (ep->prog_no) {
    case 0xd: case 0xc: case 0xb: case 0xa:
        if (ep) ep->shot_cnt = d->count;
        break;
    default:
        wp->blt_cnt = d->count;
        break;
    }
    AcDptsSt *dp = AcPtsDstry_GetExtentionDestroyInfo(acp);
    if (dp && extparam) {
        dp->hp = d->hp;
        dp->maxhp = extparam->basic.hp;
        dp->price = extparam->basic.price;
    }
    DF_ITM_Delete(itmid);
    Voice_CallPoint(0x25, 0x7f, &acp->loc, 2500.0f);
    return 1;
}

void acext_SetParam(AcPlayerParamSt *paramp, PartsParamExtensionSt *extpramp)
{
    PS2_FRAME(0x30);
    AcAsm_GetWeaponParamEx(&paramp->extweapon, 9, 0, extpramp->weapon_id, 7, 0, 0, paramp);
    paramp->extweapon.jpos = 8;
    AcAsm_GetExboostParam(paramp);
    ext_set_vsmgun(paramp, extpramp);
}

/* takes the extension off: drops it as an item (lost == 0) or marks its models for deletion */
static int __RemoveExtension(AcPlayerSt *acp, AcPartsExtSt *extp, int lost)
{
    PS2_FRAME(0x80);
    PS2_STACK(float[4], vec, 0x70);
    vec[0] = 0.0f, vec[1] = 0.0f, vec[2] = 0.0f, vec[3] = 1.0f;
    _weapon_st *wp = &acp->param.extweapon;
    AcEffSt *effp = AcCtrl_GetEffect(acp);
    if (extp->busy && extp->prog_no != 6) return 0;
    if (!lost) Voice_CallPoint(0x24, 0x7f, &acp->loc, 2500.0f);
    if (!lost) {
        vec[1] = acp->ang[1];
        int id = DF_ITM_New(NULL, vec, acp->modeltbl[2], acp->modeltbl[3], acp->floor_high, NULL);
        AcExtItemRec *d = (AcExtItemRec *)DF_ITM_GetDataAddr(id);
        if (d) {
            d->hp = AcPtsDstry_GetExtentionDestroyInfo(acp)->hp;
            d->kind = 7;
            d->sub = 8;
            d->partsid = acp->param.partsid[9];
            d->extp = acp->param.extp;
            d->comb = AcPtsRes_GetSfxCombHead(acp->frdp->ptsresp, 9);
            switch (extp->prog_no) {
            case 0xd: case 0xc: case 0xb: case 0xa:
                if (extp) d->count = (unsigned char)extp->shot_cnt;
                break;
            default:
                d->count = (unsigned char)wp->blt_cnt;
                break;
            }
        }
        DF_ITM_SetString(id, 0xf);
    } else {
        if (acp->modeltbl[2]) acp->modeltbl[2]->use |= 1;
        if (acp->modeltbl[3]) acp->modeltbl[3]->use |= 1;
    }
    acp->modeltbl[2] = NULL;
    acp->modeltbl[3] = NULL;
    acp->param.extp = NULL;
    AcPtsRes_ReRegisterWsfx(acp->frdp->ptsresp, 9, NULL);
    acp->param.partsid[9] = 0;
    DF_MAGIC2_AppearMagicOCMP(2, &acp->ocmats[2], 4);
    DF_MAGIC2_AppearMagicOCMP(2, &acp->ocmats[3], 4);
    switch (extp->prog_no) {
    case 0: case 1: case 5: case 7: case 8:
        for (int i = 0; i < extp->nmuzzle; i++) aceff_FreeBoost(effp, i + 8);
        break;
    case 6:
        extp->sshield = 0;
        extp->busy = 0;
        break;
    case 9: case 10:
        for (int i = 0; i < extp->nmuzzle; i++) aceff_FreeShield(effp, i + 1);
        extp->sshield = 0;
        extp->busy = 0;
        break;
    default:
        break;
    }
    AcWep_SettleShell(acp, wp);
    extp->prog_no = 0x11;
    extp->prvp->openfunc = NULL;
    extp->prvp->busyfunc = NULL;
    extp->prvp->closefunc = NULL;
    extp->prvp->actfunc = NULL;
    extp->prvp->actstep = 0;
    extp->acttime = 0;
    AcAsm_CalcWeightEx(&acp->param);
    return 1;
}

/* ---- kind 16: add magazine (refills the right hand weapon) ---- */

static int __CloseExtAddMagazine(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    extp->busy = 0;
    extp->reload_cnt = (short)extp->reload_time;
    ext_set_act(extp, EXT_ACT(__WaitingExtAddMagazine));
    return 0;
}

static int __BusyExtAddMagazine(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    PS2_FRAME(0x30);
    _weapon_st *wp = acp->weaponctrl[0].wp;
    const _weapon_st *rwp = wp ? wp : (const _weapon_st *)PS2_NULL_MEM();   /* both reads happen with wp NULL */
    unsigned int before = rwp->blt_cnt;
    int keep = 1;
    if (extp->use_time < extp->busy_time) {
        if (acp->param.partsid[0xc] != 0 && wp && wp->enable_addmgzn && before != 9999) {
            if (wp->dstat.effect & 0x10) wp->blt_cnt = (unsigned short)(before + extp->prvp->addmagShellPf);
            if (wp->dstat.effect & 0x20) wp->blt_cnt = (unsigned short)(wp->blt_cnt + extp->prvp->addmagEnergyPf);
            if (wp->blt_initcnt < wp->blt_cnt) {
                wp->blt_cnt = wp->blt_initcnt;
                keep = 0;
            }
        }
    } else {
        keep = 0;
    }
    SetTrainingTrendExt((int)(rwp->blt_cnt - before));
    if (keep) extp->use_time++;
    else ext_close_or_default(extp);
    return 1;
}

static int __OpenExtAddMagazine(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    if (extp->shot_cnt <= 0) return 0;
    if (extp->reload_cnt > 0) return 0;
    ext_use_shot(extp);
    extp->use_time = 0;
    ext_set_act(extp, EXT_ACT(__BusyExtAddMagazine));
    extp->busy = 1;
    return 1;
}

static void __WaitingExtAddMagazine(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    if (extp->reload_cnt > 0) extp->reload_cnt--;
}

static void __InitExtAddMagazine(AcPlayerSt *acp, AcPartsExtSt *extp, PartsParamExtensionSt *partsp)
{
    extp->prvp->enegydrain = acp->param.eboost_sub;
    extp->prvp->addmagShellPf = partsp->def_shell;
    extp->prvp->addmagEnergyPf = partsp->def_energy;
    extp->shot_cnt = partsp->use_cnt;
    extp->busy_time = partsp->boost_busytime;
    extp->reload_time = partsp->reload_time;
    extp->use_time = 0;
    extp->prvp->openfunc = EXT_SLOT(__OpenExtAddMagazine);
    extp->prvp->busyfunc = EXT_SLOT(__BusyExtAddMagazine);
    extp->prvp->closefunc = EXT_SLOT(__CloseExtAddMagazine);
    ext_set_act(extp, EXT_ACT(__WaitingExtAddMagazine));
}

/* ---- kind 13: cure status (shortens the no-lock / no-radar / generator-down times) ---- */

static int __CloseExtCurestat(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    extp->busy = 0;
    extp->reload_cnt = (short)extp->reload_time;
    ext_set_act(extp, EXT_ACT(__WaitingExtCurestat));
    return 0;
}

static inline void curestat_cut(short *t)
{
    if (*t > 0) {
        float f = (float)*t;
        if (f <= g_asmparam.ext_curestat_time) *t = 0;
        else *t = (short)ps2_ftoi(f - g_asmparam.ext_curestat_time);
    }
}

static int __BusyExtCurestat(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    PS2_FRAME(0x20);
    if (extp->use_time == 0) {
        curestat_cut(&acp->nolock_time);
        curestat_cut(&acp->noradar_time);
        curestat_cut(&acp->genedown_time);
        AcPartsExtPrivateSt *prv = extp->prvp;
        DF_MAGIC2_AppearMagicOCMPPtr(prv->magcp, prv->cmbp, &prv->ocmats[0], 4);
        prv = extp->prvp;
        DF_MAGIC2_AppearMagicOCMPPtr(prv->magcp, prv->cmbp, &prv->ocmats[1], 4);
    }
    if (!(extp->use_time < extp->busy_time)) ext_close_or_default(extp);
    extp->use_time++;
    return 1;
}

static int __OpenExtCurestat(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    PS2_FRAME(0x20);
    if (extp->shot_cnt <= 0) return 0;
    if (extp->reload_cnt > 0) return 0;
    ext_use_shot(extp);
    extp->use_time = 0;
    if (!AcCtrl_EnergyDrain(acp, extp->prvp->enegydrain)) return 0;
    ext_set_act(extp, EXT_ACT(__BusyExtCurestat));
    extp->busy = 1;
    return 1;
}

static void __WaitingExtCurestat(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    if (extp->reload_cnt > 0) extp->reload_cnt--;
}

static void __InitExtCurestat(AcPlayerSt *acp, AcPartsExtSt *extp, PartsParamExtensionSt *partsp)
{
    extp->prvp->enegydrain = acp->param.eboost_sub;
    extp->shot_cnt = partsp->use_cnt;
    extp->busy_time = partsp->boost_busytime;
    extp->reload_time = partsp->reload_time;
    extp->use_time = 0;
    extp->prvp->openfunc = EXT_SLOT(__OpenExtCurestat);
    extp->prvp->busyfunc = EXT_SLOT(__BusyExtCurestat);
    extp->prvp->closefunc = EXT_SLOT(__CloseExtCurestat);
    ext_set_act(extp, EXT_ACT(__WaitingExtCurestat));
}

/* ---- kind 12: energy pack (extra boost energy while busy) ---- */

static int EXTenepack_close(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    return 0;
}

static int EXTenepack_busy(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    PS2_FRAME(0x40);
    AcCtrl_AddHeat(acp, acp->param.cqheatExt);
    if (extp->prvp->actstep == 0) {
        if (ext_models_anim(acp, 1)) {
            extp->prvp->actstep++;
            ext_magic_muzzles(extp);
        }
    } else {
        acp->param.exboost_add = acp->param.extp->power;
        unsigned short t = extp->use_time++;
        if (!(t < extp->busy_time)) {
            acp->param.exboost_add = 0;
            extp->reload_cnt = (short)extp->reload_time;
            Anim_ChangeAnim(acp->modeltbl[2], 2, 10, 0);
            Anim_ChangeAnim(acp->modeltbl[3], 2, 10, 0);
            ext_set_act(extp, EXT_ACT(EXTenepack_standby));
            extp->busy = 0;
        }
    }
    return 1;
}

static int EXTenepack_open(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    if (extp->shot_cnt <= 0) return 0;
    if (extp->reload_cnt > 0) return 0;
    ext_use_shot(extp);
    extp->use_time = 0;
    ext_set_act(extp, EXT_ACT(EXTenepack_busy));
    return 1;
}

static void EXTenepack_standby(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    if (extp->reload_cnt > 0) extp->reload_cnt--;
}

static void EXTinit_enepack(AcPlayerSt *acp, AcPartsExtSt *extp, PartsParamExtensionSt *partsp)
{
    extp->shot_cnt = partsp->use_cnt;
    extp->busy_time = partsp->boost_busytime;
    extp->reload_time = partsp->reload_time;
    extp->prvp->openfunc = EXT_SLOT(EXTenepack_open);
    extp->prvp->busyfunc = EXT_SLOT(EXTenepack_busy);
    extp->prvp->closefunc = EXT_SLOT(EXTenepack_close);
    ext_set_act(extp, EXT_ACT(EXTenepack_standby));
}

/* ---- kind 11: coolant (core heat cooldown while busy) ---- */

static int EXTcoolant_close(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    return 0;
}

static int EXTcoolant_busy(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    PS2_FRAME(0x30);
    if (extp->prvp->actstep == 0) {
        if (ext_models_anim(acp, 1)) extp->prvp->actstep++;
    } else {
        acp->cqheatExCooldown = extp->cooldown * g_asmparam.cool_to_cqheat;
        unsigned short t = extp->use_time++;
        if (!(t < extp->busy_time)) {
            acp->cqheatExCooldown = 0.0f;
            extp->reload_cnt = (short)extp->reload_time;
            Anim_ChangeAnim(acp->modeltbl[2], 2, 10, 0);
            Anim_ChangeAnim(acp->modeltbl[3], 2, 10, 0);
            ext_set_act(extp, EXT_ACT(EXTcoolant_standby));
            extp->busy = 0;
        }
    }
    return 1;
}

static int EXTcoolant_open(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    PS2_FRAME(0x40);
    if (extp->shot_cnt <= 0) return 0;
    if (extp->reload_cnt > 0) return 0;
    if (!AcCtrl_EnergyDrain(acp, extp->prvp->enegydrain)) return 0;
    ext_use_shot(extp);
    extp->use_time = 0;
    ext_set_act(extp, EXT_ACT(EXTcoolant_busy));
    ext_magic_muzzles(extp);
    return 1;
}

static void EXTcoolant_standby(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    if (extp->reload_cnt > 0) extp->reload_cnt--;
}

static void EXTinit_coolant(AcPlayerSt *acp, AcPartsExtSt *extp, PartsParamExtensionSt *partsp)
{
    PS2_FRAME(0x20);
    extp->shot_cnt = partsp->use_cnt;
    extp->busy_time = partsp->boost_busytime;
    extp->reload_time = partsp->reload_time;
    extp->prvp->enegydrain = acp->param.eboost_sub;
    extp->cooldown = AcAsm_GetQuitCooldown(partsp->power);
    extp->prvp->openfunc = EXT_SLOT(EXTcoolant_open);
    extp->prvp->busyfunc = EXT_SLOT(EXTcoolant_busy);
    extp->prvp->closefunc = EXT_SLOT(EXTcoolant_close);
    ext_set_act(extp, EXT_ACT(EXTcoolant_standby));
}

/* ---- kind 10: stealth (no lock-on while busy) ---- */

static int EXTstealth_close(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    PS2_FRAME(0x30);
    acp->effectstat &= 0xffbf;   /* andi: also clears the upper 16 bits */
    acp->mp->cflag &= 0xfbff;
    if (acp->unlocked_time > 0x1e) acp->unlocked_time = 0x1e;
    extp->reload_cnt = (short)extp->use_time;
    Anim_ChangeAnim(acp->modeltbl[2], 2, 10, 0);
    Anim_ChangeAnim(acp->modeltbl[3], 2, 10, 0);
    ext_set_act(extp, EXT_ACT(EXTstealth_standby));
    return 1;
}

static int EXTstealth_busy(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    PS2_FRAME(0x30);
    if (extp->prvp->actstep == 0) {
        if (ext_models_anim(acp, 1)) extp->prvp->actstep++;
    } else {
        acp->effectstat |= 0x40;
        acp->mp->cflag |= 0x400;
        unsigned short t = extp->use_time++;
        if (!(t < extp->busy_time)) ext_close_or_default(extp);
    }
    return 1;
}

static int EXTstealth_open(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    PS2_FRAME(0x30);
    if (extp->shot_cnt <= 0) return 0;
    if (extp->reload_cnt > 0) return 0;
    if (!AcCtrl_EnergyDrain(acp, extp->prvp->enegydrain)) return 0;
    acp->unlocked_time = (short)extp->busy_time;
    extp->use_time = 0;
    ext_use_shot(extp);
    ext_set_act(extp, EXT_ACT(EXTstealth_busy));
    return 1;
}

static void EXTstealth_standby(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    if (extp->reload_cnt > 0) extp->reload_cnt--;
}

static void EXTinit_stealth(AcPlayerSt *acp, AcPartsExtSt *extp, PartsParamExtensionSt *partsp)
{
    PS2_FRAME(0x60);
    AcEffSt *effp = AcCtrl_GetEffect(acp);
    extp->prvp->enegydrain = acp->param.eboost_sub;
    for (int i = 0; i < extp->nmuzzle; i++) {
        ObjCMatSt *ocmp = &extp->prvp->ocmats[i];
        memcpy(ocmp->mat, IdentityMtx, 0x30);   /* rows 0..2; row 3 unchanged */
        if (i == 1 || i == 2) extp->prvp->ocmats[i].mat[0][0] = -1.0f;
        if (i == 0 || i == 1) aceff_RegisterShield(effp, i + 1, 9, 1, &extp->prvp->ocmats[i]);
        else if (partsp->magdataid > 0) aceff_RegisterShield(effp, i + 1, 9, 3, &extp->prvp->ocmats[i]);
    }
    extp->shot_cnt = partsp->use_cnt;
    extp->busy_time = partsp->boost_busytime;
    extp->reload_time = partsp->reload_time;
    extp->prvp->openfunc = EXT_SLOT(EXTstealth_open);
    extp->prvp->busyfunc = EXT_SLOT(EXTstealth_busy);
    extp->prvp->closefunc = EXT_SLOT(EXTstealth_close);
    ext_set_act(extp, EXT_ACT(EXTstealth_standby));
}

/* ---- kind 6: NE shield (always on, nothing to do) ---- */

static int EXTneshield_close(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    return 0;
}

static int EXTneshield_busy(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    return 0;
}

static int EXTneshield_open(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    return 0;
}

static void EXTinit_neshield(AcPlayerSt *acp, AcPartsExtSt *extp, PartsParamExtensionSt *partsp)
{
    extp->prvp->openfunc = EXT_SLOT(EXTneshield_open);
    extp->prvp->busyfunc = EXT_SLOT(EXTneshield_busy);
    extp->prvp->closefunc = EXT_SLOT(EXTneshield_close);
    ext_set_act(extp, EXT_ACT(EXTneshield_busy));
    extp->busy = 1;
    extp->sshield = 1;
}

/* ---- kind 9: shield (energy shield models, drains energy while busy) ---- */

static int EXTshield_close(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    acp->effectstat &= 0xffbf;   /* andi: also clears the upper 16 bits */
    ext_set_act(extp, EXT_ACT(EXTshield_standby));
    return 1;
}

static int EXTshield_busy(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    PS2_FRAME(0x20);
    acp->effectstat |= 0x40;
    if (!AcCtrl_EnergyDrain(acp, extp->prvp->enegydrain)) ext_close_or_default(extp);
    return 1;
}

static int EXTshield_open(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    PS2_FRAME(0x20);
    if (extp->reload_cnt > 0) return 0;
    if (!AcCtrl_EnergyDrain(acp, extp->prvp->enegydrain)) return 0;
    ext_set_act(extp, EXT_ACT(EXTshield_busy));
    return 1;
}

static void EXTshield_standby(AcPlayerSt *acp, AcPartsExtSt *extp)
{
}

static void EXTinit_shield(AcPlayerSt *acp, AcPartsExtSt *extp, PartsParamExtensionSt *partsp)
{
    PS2_FRAME(0x50);
    AcEffSt *effp = AcCtrl_GetEffect(acp);
    extp->prvp->enegydrain = acp->param.eboost_sub;
    for (int i = 0; i < extp->nmuzzle; i++) {
        memcpy(extp->prvp->ocmats[i].mat, IdentityMtx, 0x30);   /* rows 0..2; row 3 unchanged */
        if (i & 1) extp->prvp->ocmats[i].mat[0][0] = -1.0f;
        aceff_RegisterShield(effp, i + 1, 9, 1, &extp->prvp->ocmats[i]);
    }
    extp->prvp->openfunc = EXT_SLOT(EXTshield_open);
    extp->prvp->busyfunc = EXT_SLOT(EXTshield_busy);
    extp->prvp->closefunc = EXT_SLOT(EXTshield_close);
    ext_set_act(extp, EXT_ACT(EXTshield_standby));
    extp->sshield = 1;
}

/* ---- default open / close (kinds without own slots): extension model animations ---- */

static void Default_ExtensionClose(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    PS2_FRAME(0x30);
    acp->mislink_func = NULL;
    acp->misalert_func = NULL;
    acp->funcOrbLink = NULL;
    acp->funcCounter = NULL;
    if (ext_models_anim(acp, 2)) {
        extp->prvp->actfunc = NULL;
        extp->prvp->actstep = 0;
        extp->acttime = 0;
    }
}

static void Default_ExtensionOpen(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    PS2_FRAME(0x30);
    if (ext_models_anim(acp, 1)) {
        extp->prvp->actfunc = (ExtActFn)extp->prvp->busyfunc;
        extp->prvp->actstep = 0;
        extp->acttime = 0;
    }
}

/* ---- vs-weapons (kinds 3 / 4 / 14 / 15): counter gun (type 1, kind 15) or missile alert / interceptor (type 0) ---- */

/* acp->funcCounter: the counter gun fires at a stage object (stype 1 / 2) that hit the AC */
static int EXTvswep_auto_counter(AcPlayerSt *acp, int stype, int sid)
{
    PS2_FRAME(0xb0);
    _weapon_st *wp = &acp->param.extweapon;
    int type = -1;
    AcPartsExtSt *extp = AcCtrl_GetPartsExt(acp);
    if (!extp) return 0;
    if (!AcWep_DecBullet(acp, wp)) return 0;
    if (extp->reload_cnt > 0) return 0;
    int k = extp->idMuzzle & 1;
    extp->idMuzzle = (short)((k + 1) & 1);
    if (stype == 1) type = 3;
    else if (stype == 2) type = 4;
    if (type == -1) return 0;
    StModel *m = STAGE_GetModelPointer(type, sid);
    if (!m) return 0;
    DF_SPELL2_AppearSpellPtr(wp->spelp, 2, (char)acp->id, &wp->dstat, wp->actp, wp->combp, nullptr, nullptr, nullptr,
                             &extp->prvp->ocmats[k], m, 0x900, (unsigned short)wp->id);
    extp->reload_cnt = (short)extp->reload_time;
    return 1;
}

/* acp->misalert_func: shoots down (or, kind 14, just breaks the lock of) an incoming missile */
static int EXTvswep_missile_alert(AcPlayerSt *acp, _spell2 *trgsplp)
{
    PS2_FRAME(0x70);
    _weapon_st *wp = &acp->param.extweapon;
    AcPartsExtSt *extp = AcCtrl_GetPartsExt(acp);
    if (wp->blt_cnt == 0) return 0;
    if (!AcWep_DecBullet(acp, wp)) return 0;
    if (!extp) return 0;
    int k = extp->idMuzzle & 1;
    extp->idMuzzle = (short)((k + 1) & 1);
    int err = acp->param.exvs_mgun_errrate;
    int sr = trgsplp->spell->stealth_rate;
    if (sr > 0 && !(acp->param.radar_stat & 8)) err += sr;
    _stSpell2Data *intr = AcPtsRes_GetWeaponSpellPtr(acp->frdp->ptsresp, 9, wp->spelldata_id);
    _stSfxCombHead *comb = AcPtsRes_GetSfxCombHead(acp->frdp->ptsresp, 9);
    if (extp->prog_no == 0xe) {
        if (ps2_mod32(ps2_rand(), 0x190000) >> 14 < err) return 0;
        trgsplp->argp = nullptr;
        trgsplp->dat.missile.decoy = nullptr;
        return 1;
    }
    return AcCtrl_InterceptSpell(acp, intr, comb, &extp->prvp->ocmats[k], trgsplp, err);
}

static void EXTvswep_busy(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    PS2_FRAME(0x30);
    if (extp->reload_cnt > 0) extp->reload_cnt--;
    if (extp->prvp->actstep == 0) {
        if (extp->prvp->type == 1) acp->funcCounter = GFN(EXTvswep_auto_counter);
        else if (extp->prvp->type == 0) acp->misalert_func = GFN(EXTvswep_missile_alert);
        extp->idMuzzle = 0;
        extp->prvp->actstep++;
        if (extp->prog_no == 0xe) {
            DF_MAGIC2_AppearMagicOCMPPtr(extp->prvp->magcp, extp->prvp->cmbp, &extp->prvp->ocmats[0], 4);
            DF_MAGIC2_AppearMagicOCMPPtr(extp->prvp->magcp, extp->prvp->cmbp, &extp->prvp->ocmats[1], 4);
        }
    }
    if (extp->prog_no == 0xe && !AcCtrl_EnergyDrain(acp, extp->prvp->enegydrain)) ext_close_or_default(extp);
}

static void EXTvswep_init(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    acp->misalert_func = nullptr;
    acp->funcCounter = nullptr;
    extp->idMuzzle = 0;
    extp->prvp->enegydrain = acp->param.eboost_sub;
    extp->prvp->busyfunc = EXT_SLOT(EXTvswep_busy);
}

static void EXTinit_vsweapon(AcPlayerSt *acp, AcPartsExtSt *extp, PartsParamExtensionSt *partsp)
{
    extp->reload_time = partsp->reload_time;
    extp->prvp->type = extp->prog_no == 0xf ? 1 : 0;
    extp->prvp->busyfunc = EXT_SLOT(EXTvswep_busy);
    ext_set_act(extp, EXT_ACT(EXTvswep_init));
}

/* ---- link weapon (kind 2): fires one missile per muzzle at the locked targets when the main missiles fire ---- */

/* acp->mislink_func / funcOrbLink */
static void EXTlinkwep_missile_callback(AcPlayerSt *acp)
{
    PS2_FRAME(0xc0);
    StNextLock nl;   /* sp+0xb0, written by DF_init_next_lock before any read */
    AcPartsExtSt *extp = AcCtrl_GetPartsExt(acp);
    _weapon_st *wp = &acp->param.extweapon;
    void *argp = nullptr;
    if (!extp) return;
    for (int i = 0; i < extp->nmuzzle; i++) {
        if (!argp) {
            DF_init_next_lock(&nl, &acp->lockmng);
            if (!DF_get_next_lock(&nl)) break;
            argp = nl.lkp->ofp;
        }
        if (!AcWep_DecBullet(acp, wp)) break;
        DF_SPELL2_AppearSpellPtr(wp->spelp, 2, (char)acp->id, &wp->dstat, wp->actp, extp->prvp->cmbp, nullptr, nullptr,
                                 nullptr, &extp->prvp->ocmats[i], argp, 0x900, (unsigned short)wp->id);
        AbVal_AddShootRangeCount(acp, wp);
        CombatRecord_OnShoot(acp, (unsigned short)wp->id);
        argp = DF_get_next_lock(&nl) ? nl.lkp->ofp : nullptr;
    }
    acp->mislink_func = nullptr;
    acp->funcOrbLink = nullptr;
    ext_set_act(extp, EXT_ACT(EXTlinkwep_reload));
}

static void EXTlinkwep_reload(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    if ((int)acp->param.extweapon.reload_time < extp->acttime) ext_set_act(extp, EXT_ACT(EXTlinkwep_standby));
}

static void EXTlinkwep_standby(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    if (extp->prvp->actstep != 0) return;
    if (extp->prvp->type == 1) acp->funcOrbLink = GFN(EXTlinkwep_missile_callback);
    else if (extp->prvp->type == 0) acp->mislink_func = GFN(EXTlinkwep_missile_callback);
    extp->prvp->actstep++;
}

static void EXTlinkwep_init(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    acp->mislink_func = nullptr;
    acp->funcOrbLink = nullptr;
    extp->prvp->busyfunc = EXT_SLOT(EXTlinkwep_standby);
}

static void EXTinit_linkweapon(AcPlayerSt *acp, AcPartsExtSt *extp, PartsParamExtensionSt *partsp)
{
    PS2_FRAME(0x20);
    extp->prvp->cmbp = AcPtsRes_GetSfxCombHead(acp->frdp->ptsresp, 9);
    extp->prvp->type = 0;
    extp->prvp->busyfunc = EXT_SLOT(EXTlinkwep_standby);
    ext_set_act(extp, EXT_ACT(EXTlinkwep_init));
}

/* ---- boosters (kinds 0 / 1 / 5 / 7 / 8): extension boost, vertical boost, turn boost, flight boost, hover ---- */

static const float EXT_HALF_PI = ps2_u2f(0x3fc90fdb);

/* the energy cost of a boost (free under spflag 4, halved under spflag 8) */
static inline int ext_boost_drain(AcPlayerSt *acp, int e)
{
    if (acp->spflag & 4) e = 0;
    else if (acp->spflag & 8) e = e / 2;
    return AcCtrl_EnergyDrain(acp, e);
}

/* the boost ends: effect off, back to waiting */
static inline void ext_boost_stop(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    extp->boostfrm = 0;
    acp->effectstat &= 0xffbf;   /* andi: also clears the upper 16 bits */
    ext_set_act(extp, EXT_ACT(__WaitingExtBooster));
}

static int HOVboost_close(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    ext_boost_stop(acp, extp);
    return 0;
}

/* hover: damps the vertical speed (always on the first frame, later unless the boost button is held) */
static int HOVboost_busy(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    PS2_FRAME(0x50);
    acp->effectstat |= 0x40;
    AcCtrl_AddHeat(acp, acp->param.cqheatExt);
    float r = ps2_div(ps2_u2f(0x3dcccccd), acp->param.exboost_acc);
    if (!(r <= 1.0f)) r = 1.0f;
    int e = extp->prvp->enegydrain;
    if (extp->prvp->actstep == 0) {
        extp->hover_ht = acp->loc[1];
        acse_PutSeVolDist(acp, 0x16, AC_SEparam.ex_boost_vol, (float)AC_SEparam.ex_boost_dist);
        acp->velocity[1] *= r;
        extp->prvp->actstep++;
    } else if (!AcUidev_IsPadOnConf(acp->frdp->uidevp, 5))
        acp->velocity[1] *= r;
    if (!ext_boost_drain(acp, e)) ext_boost_stop(acp, extp);
    return 0;
}

static int HOVboost_open(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    PS2_FRAME(0x30);
    int e = extp->prvp->enegydrain;
    if (extp->reload_cnt != 0) return 0;
    if (!ext_boost_drain(acp, e)) return 0;
    extp->reload_cnt = acp->param.exvs_mgun_shotrate;
    if (acp->actno == 2) AC_SetAction(acp, 0);
    ext_set_act(extp, EXT_ACT(HOVboost_busy));
    return 1;
}

static int EXTboost_close(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    return 0;
}

static int EXTboost_busy(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    PS2_FRAME(0x40);
    AcPlayerParamSt *pp = &acp->param;
    float acc, gacc;
    acp->effectstat |= 0x40;
    AcCtrl_AddHeat(acp, pp->cqheatExt);
    if (extp->prvp->actstep == 0) {
        acc = pp->exboost_maxspd;
        gacc = pp->exboost_flyup_maxspd;
        acse_PutSeVolDist(acp, 0x16, AC_SEparam.ex_boost_vol, (float)AC_SEparam.ex_boost_dist);
        if (extp->prog_no == 5) {
            acc = pp->exboost_maxspd / 16.0f;
            if (extp->isTurnLr == 0) {
                acp->turnspd = ps2_u2f(0x322bcc77);   /* 1e-8: sign of the turn direction */
                extp->boostangy = ps2_u2f(0x322bcc77);
                extp->isTurnLr = 0;
            } else {
                acp->turnspd = ps2_u2f(0xb22bcc77);
                extp->boostangy = ps2_u2f(0xb22bcc77);
                extp->isTurnLr = 1;
            }
            extp->boostfrm = 0;
        }
        extp->prvp->actstep++;
    } else if ((int)pp->exboost_acctime < extp->acttime) {
        acc = 0.0f;
        gacc = 0.0f;
    } else {
        acc = pp->exboost_acc / 16.0f;
        gacc = pp->exboost_flyup_acc / 16.0f;
    }
    int prog = extp->prog_no;
    if (prog == 7) {
        float p = acp->view.ang[0];
        if (p < ps2_u2f(0xbf490fdb)) {   /* looking down more than 45 degrees: dive */
            AcCtrl_AccelFall(acp, gacc, pp->exboost_flyup_maxspd);
            AcCtrl_Accel8Dir(acp, 0, acc, pp->exboost_maxspd * g_asmparam.exboost_std_fall_angle);
        } else if (!(p <= ps2_u2f(0x3f060a92))) {   /* looking up more than 30 degrees: climb */
            AcCtrl_AccelFall(acp, gacc, -pp->exboost_flyup_maxspd);
            AcCtrl_Accel8Dir(acp, 0, acc, pp->exboost_maxspd * g_asmparam.exboost_std_flyup_angle);
            if (acp->actno == 0x12) acob_Close(AcCtrl_GetPartsOb(acp));
            else if (acp->actno == 0x10) AcWep_ReleaseWeapon(acp, acp->equip_weapon);
            AC_SetAction(acp, 8);
        } else
            AcCtrl_Accel8Dir(acp, 0, acc, pp->exboost_maxspd);
    } else if (prog == 5) {
        extp->boostfrm++;
        TurnAccel(acp, acc, pp->exboost_maxspd / 16.0f);
        float a = extp->boostangy;
        if (a < EXT_HALF_PI && !(a <= -EXT_HALF_PI)) {
            AcUidev_SetFakeTurn(acp->frdp->uidevp, !(acp->turnspd <= 0.0f));
            AcCtrl_TurnExBoost(acp, 0.0f, pp->exboost_maxspd / 16.0f);
            a = extp->boostangy + acp->turnspd;
            extp->boostangy = a;
            if (!(a <= EXT_HALF_PI)) {   /* turned 90 degrees: stop exactly there */
                acp->turnspd = EXT_HALF_PI - a;
                AcCtrl_TurnExBoost(acp, 0.0f, pp->exboost_maxspd / 16.0f);
                extp->acttime = (short)(pp->exboost_busytime + 1);
                acp->turnspd = ps2_u2f(0x322bcc77);
                extp->boostfrm = 0;
            }
            a = extp->boostangy;
            if (a < -EXT_HALF_PI) {
                acp->turnspd = -EXT_HALF_PI - a;
                AcCtrl_TurnExBoost(acp, 0.0f, pp->exboost_maxspd / 16.0f);
                extp->acttime = (short)(pp->exboost_busytime + 1);
                acp->turnspd = ps2_u2f(0xb22bcc77);
                extp->boostfrm = 0;
            }
        }
    } else if (prog == 1)
        AcCtrl_AccelFall(acp, acc, pp->exboost_maxspd);
    else if (prog == 0)
        AcCtrl_Accel8Dir(acp, 5, acc, pp->exboost_maxspd);
    if ((int)pp->exboost_busytime < extp->acttime) ext_boost_stop(acp, extp);
    return 0;
}

static int TURNboost_open(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    PS2_FRAME(0x30);
    int e = extp->prvp->enegydrain;
    if (extp->reload_cnt != 0) return 0;
    if (!ext_boost_drain(acp, e)) return 0;
    extp->reload_cnt = acp->param.exvs_mgun_shotrate;
    extp->isTurnLr = acp->turnrightleft == 0 ? 1 : 0;
    if (acp->actno == 2) AC_SetAction(acp, 0);
    ext_set_act(extp, EXT_ACT(EXTboost_busy));
    return 1;
}

static int __OpenExtBooster(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    PS2_FRAME(0x30);
    int e = extp->prvp->enegydrain;
    if (extp->reload_cnt > 0) return 0;
    if (!ext_boost_drain(acp, e)) return 0;
    extp->reload_cnt = acp->param.exvs_mgun_shotrate;
    if (acp->actno == 2) AC_SetAction(acp, 0);
    ext_set_act(extp, EXT_ACT(EXTboost_busy));
    return 1;
}

static void __WaitingExtBooster(AcPlayerSt *acp, AcPartsExtSt *extp)
{
    if (extp->reload_cnt > 0) extp->reload_cnt--;
    extp->busy = 0;
}

static void __InitExtBooster(AcPlayerSt *acp, AcPartsExtSt *extp, PartsParamExtensionSt *partsp)
{
    PS2_FRAME(0x60);
    AcEffSt *effp = AcCtrl_GetEffect(acp);
    extp->prvp->enegydrain = acp->param.eboost_sub;
    aceff_SetBoostParam(effp, 2);
    for (int i = 0; i < extp->nmuzzle; i++)
        aceff_RegisterBoost(effp, i + 8, extp->prvp->ocmats[i].parentmatp, &extp->prvp->ocmats[i].mat, 8, 0);
    if (extp->prog_no == 7) {
        acp->modeltbl[2]->op->noanim = 1;
        acp->modeltbl[3]->op->noanim = 1;
    }
    if (extp->prog_no == 8) {
        extp->prvp->openfunc = EXT_SLOT(HOVboost_open);
        extp->prvp->busyfunc = EXT_SLOT(HOVboost_busy);
        extp->prvp->closefunc = EXT_SLOT(HOVboost_close);
    } else if (extp->prog_no == 5) {
        extp->prvp->openfunc = EXT_SLOT(TURNboost_open);
        extp->prvp->busyfunc = EXT_SLOT(EXTboost_busy);
        extp->prvp->closefunc = EXT_SLOT(EXTboost_close);
    } else {
        extp->prvp->openfunc = EXT_SLOT(__OpenExtBooster);
        extp->prvp->busyfunc = EXT_SLOT(EXTboost_busy);
        extp->prvp->closefunc = EXT_SLOT(EXTboost_close);
    }
    ext_set_act(extp, EXT_ACT(__WaitingExtBooster));
}

/* turn boost: accelerates the turn speed towards +-maxspd (sign of boostangy); returns the change */
static float TurnAccel(AcPlayerSt *acp, float turn, float maxspd)
{
    PS2_FRAME(0x30);
    AcPartsExtSt *ep = AcCtrl_GetPartsExt(acp);
    float old = acp->turnspd, n;
    if (!ep || ep->boostangy <= 0.0f) {
        float m = -maxspd;
        if (old < m - ps2_u2f(0x3a83126f)) {
            n = old + turn;
            if (!(n <= m)) n = m;
        } else {
            n = old - turn;
            if (n < m) n = m;
        }
    } else if (!(old <= ps2_u2f(0x3a83126f) + maxspd)) {
        n = old - turn;
        if (n < maxspd) n = maxspd;
    } else {
        n = old + turn;
        if (!(n <= maxspd)) n = maxspd;
    }
    acp->turnspd = n;
    return n - old;
}

/* ---- the extension slot ---- */

typedef void (*ExtInitFn)(AcPlayerSt *, AcPartsExtSt *, PartsParamExtensionSt *);

/* by prog_no (OLSORTIE 0x1e6a270) */
static const ExtInitFn extension_initfunc[17] = {
    GFN(__InitExtBooster),     GFN(__InitExtBooster),  GFN(EXTinit_linkweapon), GFN(EXTinit_vsweapon),
    GFN(EXTinit_vsweapon),     GFN(__InitExtBooster),  GFN(EXTinit_neshield),   GFN(__InitExtBooster),
    GFN(__InitExtBooster),     GFN(EXTinit_shield),    GFN(EXTinit_stealth),    GFN(EXTinit_coolant),
    GFN(EXTinit_enepack),      GFN(__InitExtCurestat), GFN(EXTinit_vsweapon),   GFN(EXTinit_vsweapon),
    GFN(__InitExtAddMagazine),
};

/* every frame (__ExecAc): the open / close button, the throw-away gesture, then the current act function */
void acext_Control(AcPartsExtSt *extp)
{
    PS2_FRAME(0x50);
    short oldtime = extp->acttime;
    AcPlayerSt *acp = extp->prvp->acp;
    short oldstep = extp->prvp->actstep;
    if (AcUidev_IsPadTrgConf(acp->frdp->uidevp, 0xe)) {
        int g = AcUidev_IsAnalog(acp->frdp->uidevp) ? AcUidev_IsPadOnConf(acp->frdp->uidevp, 0x10)
                                                    : AcUidev_IsPadOnMaskedEqual(acp->frdp->uidevp, 0xf00, 0xf00);
        if (g) {
            /* button + all four directions: throw the extension away; kinds that cannot be thrown away skip the
               whole frame (no act function, no time step) */
            if (__RemoveExtension(acp, extp, 0)) return;
            switch (extp->prog_no) {
            case 0xd: case 0xc: case 0xb: case 7: case 5: case 1: case 0: return;
            }
        }
        AcPartsExtPrivateSt *prv = extp->prvp;
        if (extp->busy == 0) {
            if (prv->openfunc) {
                if (gcall(prv->openfunc, (void *)prv->acp, extp)) extp->busy = 1;
            } else {
                prv->actfunc = EXT_ACT(Default_ExtensionOpen);
                extp->prvp->actstep = 0;
                extp->acttime = 0;
                extp->busy = 1;
            }
        } else
            ext_close_or_default(extp);
    }
    if (extp->prvp->actfunc) gcall(extp->prvp->actfunc, (void *)acp, extp);
    if (extp->prvp->actstep == oldstep && extp->acttime == oldtime) extp->acttime++;
    if (extp->prog_no == 7) {
        acp->modeltbl[2]->op->angle[0] = -acp->view.ang[0];
        acp->modeltbl[3]->op->angle[0] = -acp->view.ang[0];
    }
}

int acext_Close(AcPartsExtSt *extp)
{
    PS2_FRAME(0x20);
    if (!extp || !extp->busy) return 0;
    ext_close_or_default(extp);
    return 1;
}

/* muzzle matrix of extension model m at object id i + 12 (at most 4 in all) */
static inline void ext_init_muzzle(AcPartsExtSt *extp, StModel *m, int i)
{
    float r = (float)(i + 12);
    if (m && extp->nmuzzle < 4 && GAcUtil_GetObjCmat(&extp->prvp->ocmats[extp->nmuzzle], m, r, 0) != -1)
        extp->nmuzzle++;
}

int acext_Init(AcPartsExtSt *extp, PartsParamExtensionSt *paramp, AcPlayerSt *acp)
{
    PS2_FRAME(0x50);
    AcPartsExtPrivateSt *prv = extp->prvp;
    ZeroMemory8b(extp, 0x28);
    ZeroMemory8b(prv, 0x180);
    extp->prvp = prv;
    extp->prvp->acp = acp;
    if (!paramp) {
        extp->prog_no = 0x11;
        return 0;
    }
    extp->prog_no = paramp->prog_no;
    if (paramp->prog_no >= 0x11) {
        extp->prog_no = 0x11;
        return 0;
    }
    extp->nmuzzle = 0;
    int n = paramp->prog_no == 5 ? 4 : 2;
    for (int i = 0; i < n; i++) {
        ext_init_muzzle(extp, acp->modeltbl[2], i);
        ext_init_muzzle(extp, acp->modeltbl[3], i);
    }
    extp->prvp->magcp = AcPtsRes_GetMagicPtr(acp->frdp->ptsresp, 9, 1);
    if (extp->prvp->magcp) extp->prvp->cmbp = AcPtsRes_GetSfxCombHead(acp->frdp->ptsresp, 9);
    extp->sshield = 0;
    gcall(extension_initfunc[paramp->prog_no], acp, extp, paramp);
    return 1;
}
