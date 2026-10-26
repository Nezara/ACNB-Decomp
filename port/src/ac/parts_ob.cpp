/* src/ac/parts_ob.cpp - over booster (OB) of cores with ob_num boosters (whole file, OLSORTIE overlay). AcPartsObSt is
 * a small state machine per AC: 0 waiting, 1 open (hatch animation), 2 charge (events at 30 / 60 / 90 frames), 3 busy
 * (OB action 0x12), 4 close. acob_Control runs it from the pad (trigger conf 13) and handles the "limit off" gesture
 * (confs 8-11 held + trigger, abl_limitoff ACs): limit-off time, then a penalty time. Also the OB screen effect colour,
 * the effect-model animations (acp->eobmp), sounds and vibration. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"

/* back to the ground / air action when the OB action runs, stop the jet loop, close the hatch */
static void parts_ob_close(AcPartsObSt *obp)
{
    AcPlayerSt *acp = obp->prvp->acp;
    if (acp->actno == 0x12) {
        if (acp->floor_high < 0.001f + acp->loc[1]) AC_SetAction(acp, 6);
        else AC_SetAction(acp, 8);
    }
    acse_StopLoopSe(acp, 2, 1);
    obp->stat = 4;
    obp->actstep = 0;
    obp->acttime = 0;
}

void acob_Close(AcPartsObSt *obp)
{
    PS2_FRAME(0x30);
    if (!obp) return;
    parts_ob_close(obp);
}

void acob_Init(AcPartsObSt *obp, AcPlayerSt *acp)
{
    PS2_FRAME(0x40);
    if (!obp) return;
    AcPartsObPrivateSt *prv = obp->prvp;
    ZeroMemory8b(obp, 0x10);
    ZeroMemory8b(prv, 0x40);
    obp->prvp = prv;
    obp->prvp->acp = acp;
    obp->obnum = (char)acp->param.corep->ob_num;
    obp->stat = 0;
    obp->actstep = 0;
    obp->acttime = 0;
    obp->limitoff_time = 0;
    obp->penalty_time = 0;
    prv->openframe = -1;
}

/* original: s_oboofunc[stat] is only guarded against stat < 0 (no upper bound) */
void acob_Control(AcPartsObSt *obp)
{
    PS2_FRAME(0x70);
    if (!obp) return;
    AcPartsObPrivateSt *prv = obp->prvp;
    short step = obp->actstep, time = obp->acttime;
    AcPlayerSt *acp = prv->acp;
    int run = 1;
    if (g_sortie.pconstant->modeSortie <= 3) {
        if (AcCtrl_IsMyView(acp)) ScrEff_SetObEffectParam(&prv->rgbaEff);
    }
    if (obp->limitoff_time == 0 && obp->penalty_time == 0) {
        if (!AcCtrl_IsCharging(acp) && acp->param.abl_limitoff && AcUidev_IsPadOnConf(acp->frdp->uidevp, 11) &&
            AcUidev_IsPadOnConf(acp->frdp->uidevp, 10) && AcUidev_IsPadOnConf(acp->frdp->uidevp, 8) &&
            AcUidev_IsPadOnConf(acp->frdp->uidevp, 9) && AcUidev_IsPadTrgConf(acp->frdp->uidevp, 13)) {
            run = 0;
            obp->limitoff_time = (short)acp->param.limitoff_time;
        }
    } else if (obp->limitoff_time > 0) {
        acp->syserr |= 0x40;
        if (--obp->limitoff_time <= 0) {
            acp->energyReal = 0;
            obp->limitoff_time = 0;
            obp->penalty_time = (short)acp->param.limitoff_penatime;
        }
    } else {
        acp->syserr |= 0x80;
        if (--obp->penalty_time <= 0) {
            obp->limitoff_time = 0;
            obp->penalty_time = 0;
            acp->stat |= 4;
        }
    }
    if (run) {
        if (AcUidev_IsPadTrgConf(acp->frdp->uidevp, 13) && obp->stat == 0) {
            obp->stat = 1;
            obp->actstep = 0;
            obp->acttime = 0;
        } else if (obp->stat == 3 && (AcCtrl_IsCharging(acp) || AcUidev_IsPadTrgConf(acp->frdp->uidevp, 13))) {
            parts_ob_close(obp);
        }
        if (obp->stat >= 0) gcall(s_oboofunc[obp->stat], acp, obp);
    }
    if (obp->actstep == step && obp->acttime == time) obp->acttime++;
    if (prv->openframe >= 0) prv->openframe++;
}

/* hatch closes after 90 - openframe frames (at least 1); fades the screen effect */
static void obooctrl_close(AcPlayerSt *acp, AcPartsObSt *obp)
{
    PS2_FRAME(0x40);
    AcPartsObPrivateSt *prv = obp->prvp;
    if (prv->openframe >= 0) {
        prv->rsvhanim = 0x5a - prv->openframe;
        if (prv->rsvhanim <= 0) prv->rsvhanim = 1;
    }
    if (obp->chargeTime > 0 && obp->acttime + obp->chargeTime == 0x1e)
        acse_PutSeVolDist(acp, 7, AC_SEparam.oboo_charge_vol, (float)AC_SEparam.oboo_charge_dist);
    prv->openframe = -1;
    if (obp->prvp->cntEff > 0) {
        obp->prvp->cntEff--;
        obp->prvp->rgbaEff[3] = obp->prvp->cntEff;
    }
    if (prv->rsvhanim > 0) prv->rsvhanim--;
    if (prv->rsvhanim == 0) {
        if (Anim_ChangeAnim(acp->eobmp, 0, 0x3c, 0)) {
            obp->stat = 0;
            obp->actstep = 0;
            obp->acttime = 0;
            obp->prvp->rgbaEff[3] = 0;
        }
    }
}

static void obooctrl_busy(AcPlayerSt *acp, AcPartsObSt *obp)
{
    PS2_FRAME(0x30);
    AcCtrl_AddHeat(acp, acp->param.cqheatEobBusy);
    if (FRAME & 1) {
        if (obp->prvp->cntEff > 0) {
            obp->prvp->cntEff--;
            obp->prvp->rgbaEff[3] = obp->prvp->cntEff;
        }
    }
    acp->effectstat |= 0x20;
}

/* charge: sparks at frame 0, sound at 30, hatch animation at 60, the OB action at 90.
 * original: the AppearMagicOCMPPtr results are dropped and the charge_splp slots cleared */
static void obooctrl_charge(AcPlayerSt *acp, AcPartsObSt *obp)
{
    PS2_FRAME(0x50);
    if (obp->chargeTime < 0xff) obp->chargeTime++;
    if (obp->acttime == 0) {
        _magic2_data_st *a = AcPtsRes_GetMagicPtr(acp->frdp->ptsresp, 0, 2);
        _magic2_data_st *b = AcPtsRes_GetMagicPtr(acp->frdp->ptsresp, 0, 4);
        DF_MAGIC2_AppearMagicOCMPPtr(a, AcPtsRes_GetSfxCombHead(acp->frdp->ptsresp, 0), &acp->ocmats[20], 4);
        obp->prvp->charge_splp[0] = 0;
        DF_MAGIC2_AppearMagicOCMPPtr(b, AcPtsRes_GetSfxCombHead(acp->frdp->ptsresp, 0), &acp->ocmats[21], 4);
        obp->prvp->charge_splp[1] = 0;
        if (obp->obnum == 4) {
            DF_MAGIC2_AppearMagicOCMPPtr(a, AcPtsRes_GetSfxCombHead(acp->frdp->ptsresp, 0), &acp->ocmats[22], 4);
            obp->prvp->charge_splp[2] = 0;
            DF_MAGIC2_AppearMagicOCMPPtr(b, AcPtsRes_GetSfxCombHead(acp->frdp->ptsresp, 0), &acp->ocmats[23], 4);
            obp->prvp->charge_splp[3] = 0;
        }
    }
    if (AcCtrl_IsCharging(acp) || acp->energyReal < acp->param.oboost_1stsub) {
        obp->stat = 4;
        obp->actstep = 0;
        obp->acttime = 0;
        return;
    }
    AcCtrl_AddHeat(acp, acp->param.cqheatEobFirst);
    switch (obp->acttime) {
    case 30:
        acse_PutSeVolDist(acp, 7, AC_SEparam.oboo_charge_vol, (float)AC_SEparam.oboo_charge_dist);
        break;
    case 60:
        Anim_ChangeAnimGroup(acp->eobmp, 0x80, 0x3e, 0x19, 0);
        break;
    case 90:
        if (obp->stat == 2) {
            if (acp->actno == 0x10 && acp->wep_release == 0) AcWep_ReleaseWeapon(acp, acp->equip_weapon);
            AcCtrl_EnergyDrain(acp, acp->param.oboost_1stsub);
            obp->prvp->cntEff = 0x30;
            AC_SetAction(acp, 0x12);
            acse_PutSeVolDist(acp, 2, AC_SEparam.oboo_jet_vol, (float)AC_SEparam.oboo_jet_dist);
            acse_PutLoopSeVolToneDist(acp, 2, 8, AC_SEparam.oboo_fly_vol, 0, (float)AC_SEparam.oboo_fly_dist);
            obp->stat = 3;
            obp->actstep = 0;
            obp->acttime = 0;
            float pw = AC_TempParam.oboo_vibrapow;
            unsigned int power;
            if (2147483648.0f <= pw) power = (unsigned int)ps2_ftoi(pw - 2147483648.0f) | 0x80000000u;
            else power = (unsigned int)ps2_ftoi(pw);
            AcCtrl_SetVibration(acp, (unsigned char)power, (short)ps2_ftoi(AC_TempParam.oboo_vibratime));
        }
        break;
    }
}

static void obooctrl_open(AcPlayerSt *acp, AcPartsObSt *obp)
{
    PS2_FRAME(0x40);
    AcPartsObPrivateSt *prv = obp->prvp;
    if (obp->actstep == 0) {
        acse_PutSeVolDist(acp, 6, AC_SEparam.oboo_open_vol, (float)AC_SEparam.oboo_open_dist);
        obp->actstep++;
        prv->openframe = 0;
        obp->chargeTime = 0;
    }
    if (Anim_ChangeAnim(acp->eobmp, 1, 0x14, 0)) {
        obp->stat = 2;
        obp->actstep = 0;
        obp->acttime = 0;
    }
}

static void obooctrl_waiting(void)
{
}

#ifdef ACNB_SOURCEPORT
void (*s_oboofunc[5])(AcPlayerSt *, AcPartsObSt *) = {
    (void (*)(AcPlayerSt *, AcPartsObSt *))obooctrl_waiting, obooctrl_open, obooctrl_charge, obooctrl_busy, obooctrl_close,
};
#endif
