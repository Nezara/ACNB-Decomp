/* src/main/missres.cpp - end-of-stage settlement for the results screen: fills the StMissionResult from the
 * StSettlement the mission script wrote and from the battle state: base / success pay, special add / sub, ammo and
 * repair costs (always player 0's AC, g_ac[0]), the rank ladder (ammo cost, AP rate, mission time) and the ranker
 * points. Arena / VS matches (isArena) settle through __SettleVs (winner, timer-over / escape result, base pay only). */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/local/src/main/missres.h"

void MissRes_Settle(StMissionResult *pmissres, StSettlement *psettle, int isArena, int isSuccess)
{
    PS2_FRAME(0x50);
    pmissres->idGameOver = psettle->idGameOver;
    pmissres->idRanker = g_sortie.pconstant->idRanker[0];
    pmissres->idMission = g_sortie.pconstant->idMission;
    pmissres->modeSortie = g_sortie.pconstant->modeSortie;
    pmissres->modeFree = g_sortie.pconstant->flagFree != 0;
    pmissres->timeCombat = Sortie_GetCombatTime();
    EVMNG_SetMissionResultData(pmissres);
    if (isArena) __SettleVs(pmissres, psettle);
    else __SettleIncomeAndOutlays(pmissres, psettle, isSuccess);
}

/* 4 points when the value is within the first limit, down to 0 past the fourth. */
static int missres_point_le(int v, const unsigned short *limit)
{
    for (int i = 0; i < 4; i++)
        if (v <= limit[i]) return 4 - i;
    return 0;
}

static void __SettleIncomeAndOutlays(StMissionResult *pmissres, StSettlement *psettle, int isSuccess)
{
    PS2_FRAME(0x80);
    AcPlayerSt *pac = &g_ac[0];
    int money = g_sortie.pconstant->player[0].money;
    pmissres->moneyBefore = money;
    pmissres->moneyAfter = money;
    pmissres->payAdd = psettle->payBase;
    pmissres->moneyAfter += psettle->payBase;
    if (isSuccess) {
        pmissres->payAdd += psettle->paySuccess;
        pmissres->moneyAfter += psettle->paySuccess;
    }
    if (isSuccess) {
        pmissres->paySpecialAdd = ps2_ftoi((float)psettle->paySpecialAdd * psettle->cntSpecialAdd);
        pmissres->moneyAfter += pmissres->paySpecialAdd;
    }
    if (!psettle->isFreeExpend) {
        pmissres->costAmm = psettle->costRemoveAmm + __CalcAmmExpense(pac);
        pmissres->moneyAfter -= pmissres->costAmm;
    }
    if (!psettle->isFreeExpend) {
        /* repair: price / 100 per AP lost, / 8000; x1.5 (toward zero) when the AC was destroyed */
        int cost = (int)((unsigned)(g_sortie.pconstant->player[0].price / 100) * (unsigned)(pac->param.hp - pac->hp)) / 8000;
        if (pac->hp <= 0) cost = (int)((unsigned)cost * 3u) / 2;
        pmissres->costBody = cost;
        pmissres->moneyAfter -= pmissres->costBody;
    }
    pmissres->costSpecialSub = ps2_ftoi((float)psettle->paySpecialSub * psettle->cntSpecialSub);
    pmissres->moneyAfter -= pmissres->costSpecialSub;

    if (!isSuccess) {
        pmissres->rrankTotal = 1;
    } else {
        int epointAmm = missres_point_le(pmissres->costAmm, psettle->rrvAmm);
        int rateAp = ps2_ftoi(ps2_div(100.0f * (float)pac->hp, (float)pac->param.hp));
        int epointApRate = 0;
        for (int i = 0; i < 4; i++)
            if (rateAp >= psettle->rrvApRate[i]) { epointApRate = 4 - i; break; }
        int epointTime = missres_point_le(psettle->timeMission, psettle->rrvTime);
        int epointTotal = epointTime + (epointAmm + epointApRate);
        if (epointTotal >= 11) pmissres->rrankTotal = 6;
        else if (epointTotal >= 8) pmissres->rrankTotal = 5;
        else if (epointTotal >= 5) pmissres->rrankTotal = 4;
        else if (epointTotal >= 2) pmissres->rrankTotal = 3;
        else pmissres->rrankTotal = 2;
    }
    if (!g_sortie.pconstant->flagFree) {
        pmissres->rpntAdd = isSuccess ? psettle->rpntSuccess : psettle->rpntFail;
        pmissres->rpntAfter = psettle->rpntCurrent + pmissres->rpntAdd;
    }
}

/* price * rounds spent for one weapon that costs ammo (dstat.effect bit 4); u16 fields, signed int arithmetic. */
static int missres_wp_cost(_weapon_st *wp)
{
    return (int)((unsigned)wp->blt_price * (unsigned)(wp->blt_initcnt - wp->blt_cnt));
}

static int __CalcAmmExpense(AcPlayerSt *pac)
{
    PS2_FRAME(0x30);
    int cost = 0;
    for (int i = 0; i < pac->param.n_weapon; i++) {
        _weapon_st *wp = &pac->param.weapons[i];
        if (wp->statid != 5 && (wp->dstat.effect & 0x10)) cost += missres_wp_cost(wp);
    }
    if (pac->param.extp) {
        AcPartsExtSt *extp = AcCtrl_GetPartsExt(pac);
        if (extp && extp->prog_no != 0x11 && pac->param.extweapon.blt_initcnt != 0 &&
            (pac->param.extweapon.dstat.effect & 0x10))
            cost += missres_wp_cost(&pac->param.extweapon);
    }
    if (pac->param.Larm_type == 3) {
        AcPartsLgunSt *lgp = AcCtrl_GetPartsLgun(pac);
        if (lgp) {
            _ac_weaponctrl *lgwp = AcLgun_GetWeaponCtrl(lgp);
            if (lgwp && lgwp->wp && (lgwp->wp->dstat.effect & 0x10)) cost += missres_wp_cost(lgwp->wp);
        }
    }
    if (pac->param.corep->func_type == 0) {
        AcPartsEoSt *eop = AcCtrl_GetPartsEo(pac);
        if (eop && aceo_IsShellType(eop)) {
            _weapon_st *wp = aceo_GetWeapon(eop);
            if (wp && (wp->dstat.effect & 0x10)) cost += missres_wp_cost(wp);
        }
    }
    return cost;
}

/* VS / arena: winnerVs = the last player with the win bit (-1 = none), vsresm 1 timer over / 3 escape / 0, then 2
 * if any player has the areaover bit; success = player 0 won. No costs, no rank. */
static void __SettleVs(StMissionResult *pmissres, StSettlement *psettle)
{
    if (psettle->isVsTimerOver) pmissres->vsresm = 1;
    else if (psettle->isEscape) pmissres->vsresm = 3;
    else pmissres->vsresm = 0;
    pmissres->winnerVs = -1;
    for (int i = 0; i < 4; i++) {
        if (psettle->playerVs[i].win) pmissres->winnerVs = (char)i;
        if (psettle->playerVs[i].areaover) pmissres->vsresm = 2;
    }
    int isSuccess = pmissres->winnerVs == 0;
    int money = g_sortie.pconstant->player[0].money;
    pmissres->moneyBefore = money;
    pmissres->moneyAfter = money;
    pmissres->payAdd = psettle->payBase;
    pmissres->moneyAfter += psettle->payBase;
    if (isSuccess) {
        pmissres->payAdd += psettle->paySuccess;
        pmissres->moneyAfter += psettle->paySuccess;
    }
    if (!g_sortie.pconstant->flagFree) {
        pmissres->rpntAdd = isSuccess ? psettle->rpntSuccess : psettle->rpntFail;
        pmissres->rpntAfter = psettle->rpntCurrent + pmissres->rpntAdd;
    }
}
