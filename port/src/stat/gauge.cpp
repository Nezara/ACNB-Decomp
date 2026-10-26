/* src/stat/gauge.cpp - HUD gauge state and the gauge screens (the functions Versus runs; the training-mode counter
 * displays, Gauge_DrawEnemyInfo and the cockpit-view path are not).
 * DF_ACST2_DrawStat updates one StGaugeInfo per view (update_ginfo: AP, boost / red-zone energy, heat, timers,
 * warning blink counters with their voice cues, extension / back-unit colours, locked target's weapon state) and
 * draws the overlook (third-person) gauge screen: the StatType command list (draw_cmdlist), the friend status list,
 * radar, altimeter. */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

#define A16 __attribute__((aligned(16)))

static const float PI_F = ps2_u2f(0x40490fdb), TWO_PI_F = ps2_u2f(0x40c90fdb);

void DF_ACST2_GetMyCol(int *rgba, int player)
{
    PS2_FRAME(0x30);
    struct_76b810ec c;
    DF_MFUNC_GetCockpitColor(&c, Sortie_GetAcOption(player)->cockpitcol);
    rgba[0] = c.r;
    rgba[1] = c.g;
    rgba[2] = c.b;
    rgba[3] = c.a;
}

/* the other ACs of a versus match (all but playerno): name code by form (0 player number, 1 team leaders / team,
 * 2 team), AP, AP rate and state (2 below 10%, 3 destroyed) */
static int DF_GetFriendStat_VS(struct_0e8f2d22 *frp, int num, int playerno, int form)
{
    (void)num;
    int n = 0;
    StModel *m = MyAC;
    for (int i = 0; i < NowPlayerNum; i++, m++) {
        if (i == playerno) continue;
        frp->type = (unsigned char)i;
        if (form == 2) {
            if (m->cflag & 1) frp->cdp = code_str[4];
            else if (m->cflag & 2) frp->cdp = code_str[6];
        } else if (form == 1) {
            if (i == MyI.leader[0]) frp->cdp = code_str[3];
            else if (i == MyI.leader[1]) frp->cdp = code_str[5];
            else if (m->cflag & 1) frp->cdp = code_str[4];
            else frp->cdp = code_str[6];
        } else if (form == 0) {
            frp->cdp = code_str[7 + i];
        }
        AcPlayerSt *acp = (AcPlayerSt *)m->arg0p;
        frp->ap = acp->hp;
        int max = ((AcPlayerSt *)m->arg0p)->param.hp;
        frp->per = max > 0 ? (float)frp->ap / (float)max : 0.0f;
        frp->stat = 0;
        if (frp->ap <= 0) frp->stat = 3;
        else if (frp->per < 0.1f) frp->stat = 2;
        frp++;
        n++;
    }
    return n;
}

/* mission friend list: the other player ACs from NowUserPlayerNum on (all but playerno; name code_str[2]), then the
 * StStage characters (Chr; code_str[0] for model ids 0x50 / 0x51 / 0x59 / 0x5a, else code_str[1]); only models with
 * cflag bit 0 and disp_ap. AP, AP rate and state as in the versus list. Quirk: the first loop starts at
 * MyAC[NowUserPlayerNum], and the characters' AP / max come from raw offsets of their work struct (u16 @0xc, max
 * through the pointer @0x5c, +4). */
static int DF_GetFriendStat_MISSION(struct_0e8f2d22 *frp, int num, int playerno)
{
    (void)num;
    int n = 0;
    int ii = NowUserPlayerNum;
    StModel *mp = &MyAC[ii];
    for (; ii < NowPlayerNum; ii++, mp++) {
        if (ii == playerno) continue;
        if (!(mp->cflag & 1) || mp->disp_ap == 0) continue;
        frp->type = 0;
        frp->stat = 0;
        frp->cdp = code_str[2];
        frp->ap = ((AcPlayerSt *)mp->arg0p)->hp;
        int max = ((AcPlayerSt *)mp->arg0p)->param.hp;
        frp->per = max > 0 ? (float)frp->ap / (float)max : 0.0f;
        if (frp->ap <= 0) frp->stat = 3;
        else if (frp->per < 0.1f) frp->stat = 2;
        frp++;
        n++;
    }
    int cnum = StgInfo.cnum;
    mp = Chr;
    for (ii = 0; ii < cnum; ii++, mp++) {
        if (!(mp->cflag & 1) || mp->disp_ap == 0) continue;
        frp->type = 1;
        frp->stat = 0;
        unsigned int id = mp->on;
        if (id == 0x5a || id == 0x59 || id == 0x51 || id == 0x50) frp->cdp = code_str[0];
        else frp->cdp = code_str[1];
        char *wk = (char *)mp->arg0p;
        frp->ap = *(unsigned short *)(wk + 0xc);
        int max = *(int *)(*(char **)(wk + 0x5c) + 4);
        frp->per = max > 0 ? (float)frp->ap / (float)max : 0.0f;
        if (frp->ap <= 0) frp->stat = 3;
        else if (frp->per < 0.1f) frp->stat = 2;
        frp++;
        n++;
    }
    return n;
}

/* friend status list at the top left: name, AP bar, AP / 4 */
static void DF_DrawFriendStat(int myplayerno)
{
    PS2_FRAME(0x60);
    char buf[16];
    int n;
    unsigned char mode = g_sortie.pconstant->modeSortie;
    if (mode == 4 || mode == 5) n = DF_GetFriendStat_VS(frstat, 4, myplayerno, MyI.vsform);
    else if (mode == 2 || mode == 6) n = DF_GetFriendStat_VS(frstat, 4, myplayerno, 0);
    else if (mode == 3) n = DF_GetFriendStat_VS(frstat, 4, myplayerno, 1);
    else n = DF_GetFriendStat_MISSION(frstat, 4, myplayerno);
    ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 3), 3); GS_NewList();
    GS_SetALPHA_1(0, 1, 0, 1, 0);
    GS_FlushList(); ps2_rq_tag_end();
    struct_0e8f2d22 *fr = frstat;
    int y = 0x40;
    for (int i = 0; i < n; i++, fr++) {
        ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 3), 3); GS_NewList();
        GS_SetRGBA(STA2_RGBA[18]);
        DF_ACSTAT_DrawSpriteC(0x24, y + 1, 0x500, 0x50);
        GS_SetRGBA(ginfop->mycol);
        struct_f386fb29 tex0;
        memcpy(&tex0, &STA2_TEXTURE[5], 8);
        GS_SetTEX0_1(tex0);
        ps2_sprintf_d(buf, "%04d", (fr->ap + 3) / 4);
        DF_ACSTAT_DrawNumberOffset(0x77, y, buf, 4, 9);
        DF_ACSTAT_DrawSpriteC(0x24, y + 1, ps2_ftoi(1280.0f * fr->per), 0x50);
        StatFont_DrawString(0xe, y, 0x12, 8, fr->cdp, 0x11);
        y += 0xc;
        GS_FlushList(); ps2_rq_tag_end();
    }
}

/* share of the stage's enemies and the COM ACs still alive */
static float get_remnants(void)
{
    int alive = 0, total = 0;
    EnemySt *e = Enemy;
    for (int i = 0; i < StgInfo.cnum; i++, e++) {
        if (!e->initialize || (e->mp->cflag & 1)) continue;
        total++;
        if (!(e->use & 0x80) && !(e->mp->use & 4)) alive++;
    }
    for (int i = NowUserPlayerNum; i < NowPlayerNum; i++) {
        AcPlayerSt *acp = &g_ac[i];
        if (acp->mp->cflag & 1) continue;
        if ((acp->mp->use & 2) && acp->hp != 0) continue;
        total++;
        if (acp->hp != 0) alive++;
    }
    return total > 0 ? (float)alive / (float)total : 0.0f;
}

/* area-over warning blink interval: 18..42 frames, shorter towards the area edge (alert -> over band per axis).
 * Quirk: y and z are tested against rgnalert[0][0] (the x bound) on the upper side. */
static void leavin_alert(StGaugeInfo *gip, AcPlayerSt *acp)
{
    const float (*ra)[4] = CopyOfSysInfo.rgnalert, (*ro)[4] = CopyOfSysInfo.rgnover;
    float r[3];
    for (int k = 0; k < 3; k++) {
        float v = acp->loc[k];
        if (v < ra[1][k]) r[k] = (ro[1][k] - v) / (ro[1][k] - ra[1][k]);
        else if (!(v <= ra[0][0])) r[k] = (ro[0][k] - v) / (ro[0][k] - ra[0][k]);
        else r[k] = 1.0f;
        if (r[k] < 0.0f) r[k] = 0.0f;
    }
    float m = r[1] < r[2] ? r[1] : r[2];
    float t = r[0];
    if (!(r[0] < m)) t = m;
    gip->leavin_intvl = (short)ps2_ftoi(18.0f + 24.0f * t);
}

static void clear_all_frm(StGaugeInfo *gip)
{
    gip->leavin_frm = 0;
    gip->leavin_sfrm = 0;
    for (int i = 0; i < 7; i++) {
        gip->acstat_frm[i] = 0;
        gip->acstat_sfrm[i] = 0;
    }
    for (int i = 0; i < 11; i++) gip->syserr_sfrm[i] = 0;
    gip->syserr_sfrm_nx[0] = 0;
    gip->syserr_sfrm_nx[1] = 0;
}

/* gauge layout (StatType) for the current view count / maker / extension; reloads its textures on change */
void DF_ACST2_ReloadTMX(int force)
{
    PS2_FRAME(0x50);
    int type = gcall_as<int(int, int, int, int)>(GFN(StatTex_MakeType), ViewNum >= 2, (int)ginfo[0].maker,
                                                  ginfo[0].statf & 0xf, ginfo[0].statf >> 4 & 0xf);
    if (type < 0 || type >= 7) {
        StatType = -1;
        return;
    }
    if (StatType == type && !force) return;
    StatTex_LoadVariousTexture(type);
    int n = ViewNum;
    if (n > 2) n = 2;
    for (int i = 0; i < n; i++) WEPICON_SetWepName(ViewCamID[i], -1);
    StatType = type;
    CurSightType = -1;
}

/* weapon slots (from the next weapon on) and left-arm ammo of a locked AC */
static void get_eninfo(StGaugeInfo *gip, StModel *tgt)
{
    PS2_FRAME(0x20);
    if (!tgt || tgt->type != 4) return;
    AcPlayerSt *acp = (AcPlayerSt *)tgt->arg0p;
    int w = acp->next_weapon;
    for (int k = 0; k < 4; k++) {
        _weapon_st *wp = &acp->param.weapons[w];
        w = (w + 1) % 4;
        unsigned char st = wp->statid;
        unsigned short cnt = 0;
        if (st == 6) st = 0;
        if (st != 5) cnt = wp->blt_cnt;
        gip->en_wepstat[k] = st;
        gip->en_blt_cnt[k] = cnt;
    }
    unsigned char lt = acp->param.Larm_type;
    gip->en_l_type = lt;
    if (lt == 4) return;
    if (lt == 3) {
        _ac_weaponctrl *wc = AcLgun_GetWeaponCtrl(AcCtrl_GetPartsLgun(acp));
        if (wc && wc->wp) gip->en_l_blt = wc->wp->blt_cnt;
    } else {
        gip->en_l_blt = 0;
    }
}

/* STA2_RGBA[4] -> [5] at t */
static void lerp_col(int *out, float t)
{
    const int *a = STA2_RGBA[4], *b = STA2_RGBA[5];
    int c[4];
    for (int k = 0; k < 4; k++) c[k] = ps2_ftoi((float)a[k] + t * (float)(b[k] - a[k]));
    memcpy(out, c, 16);
}

static float pulse(void) { return 1.0f + 0.5f * Sintable[(FRAME << 6) & 0xfff]; }

/* countdown blink counter: f - 1 (if running), restarted at restart while the condition bit is set */
static short blink(short f, int on, short restart)
{
    if (f > 0) f--;
    if (f <= 0 && on) f = restart;
    return f;
}

static void update_ginfo(StGaugeInfo *gip, int player)
{
    PS2_FRAME(0xf0);
    AcPlayerSt *acp = &g_ac[player];
    if (!acp->param.corep) return;
    AcPartsExtSt *ext = AcCtrl_GetPartsExt(acp);
    if (gip->acid != player) {
        clear_all_frm(gip);
        int cockpit = ViewMode_UseCockpit(player);
        gip->acid = player;
        gip->maker = acp->param.headp->basic.maker;
        gip->ablob = acp->param.core_type;
        gip->radar = acp->param.radar_type;
        gip->statf = (char)(cockpit << 4 | (acp->param.core_type != 1));
        gip->flags &= ~0xffu;
        gip->flags |= (unsigned char)Sortie_GetAcOption(player)->cockpit;
        unsigned char mode = g_sortie.pconstant->modeSortie;
        if (mode >= 2 && mode <= 6) gip->flags &= ~0x10u;
        else gip->flags &= ~0xfu;
        if (StatType == 6 || StatType == 5) {
            InitCkpt_GetPanelColor(gip->mycol, Sortie_GetAcOption(player)->cockpitcol);
            change_cockpit_screen_color(Sortie_GetAcOption(player)->cockpitcol);
        } else {
            struct_76b810ec c;
            DF_MFUNC_GetCockpitColor(&c, Sortie_GetAcOption(player)->cockpitcol);
            gip->mycol[0] = c.r;
            gip->mycol[1] = c.g;
            gip->mycol[2] = c.b;
            gip->mycol[3] = c.a;
        }
    }
    float a = PI_F - acp->mp->ang[1];
    if (!(a <= PI_F)) a = a - TWO_PI_F;
    if (a < -PI_F) a = a + TWO_PI_F;
    gip->angy = a;
    gip->myap = (acp->hp + 3) / 4;

    /* energy: the boost level above the red zone, the red zone (whole gauge while overheated) */
    int bmax = acp->param.boost_max;
    if (bmax == 0) bmax = 1;
    int en = acp->energyRedF;
    int red = (acp->stat & 4) ? bmax : acp->param.boost_redarea;
    if (red < en) {
        gip->bstrate = (float)en / (float)bmax;
        gip->redback = en;
    } else {
        gip->bstrate = -1.0f;
        if (gip->redback < en) gip->redback = en;
        if (gip->redback - en > 800) {
            gip->redback = en;
            Voice_Call(0xb, 0x7f);
        }
        red = en;
    }
    gip->redrate = (float)red / (float)bmax;
    gip->myspd = (10.0f * (60.0f * (60.0f * (60.0f * acp->spdmeter_speed)))) / 1000.0f;
    gip->core_tmprt_limit = acp->param.tmprtLimit;
    gip->is_cooling = (acp->stat & 0x8000) != 0;
    gip->is_radar_func = 1;
    unsigned short noise = acp->ecmnoise;
    if (noise >= 10000) noise = 9999;
    gip->ecmnoise = noise;
    gip->ecmerror_rate = acp->noradar_time > 0 ? 1.0f : 0.0f;
    if (acp->param.corep->vs_type == 0) gip->corei = -1;
    else if (acp->exestat & 1) gip->corei = 30;
    else if (gip->corei > 0) gip->corei--;
    if (AcCtrl_IsSpareWepFuncCore(acp) == 1) {
        if (acp->exestat & 2) gip->unit_hanger = 60;
        else if (gip->unit_hanger > 0) gip->unit_hanger--;
    } else {
        gip->unit_hanger = -1;
    }
    int heat = ps2_ftoi(acp->temperature);
    if (heat >= 10000) heat = 9999;
    if (heat < 0) heat = 0;
    gip->myheat = heat;

    /* training statistics */
    if (!MyI.padoff && !gip->prohibit) {
        gip->lockTime = GetMissLockTime();
        gip->countUp++;
        gip->countDown = AC_VS_RESTTIME;
        gip->numHit = (unsigned short)GetTrainingTrend()->damageNum;
        gip->destBlade = (unsigned short)GetBladeKill();
        unsigned int n = gip->numTemp;
        unsigned int sum = (unsigned int)heat + gip->aveTemp * n;
        gip->numTemp = n + 1;
        gip->aveTemp = (unsigned short)(sum / gip->numTemp);
        float t = acp->temperature;
        if ((float)gip->maxTemp < t) {
            if (!(2147483648.0f <= t)) gip->maxTemp = (unsigned short)ps2_ftoi(t);
            else gip->maxTemp = (unsigned short)(ps2_ftoi(t - 2147483648.0f) | 0x80000000u);
        }
    }
    if (gip->countUp > 360000) gip->countUp = 360000;
    unsigned short *const cnt[7][2] = {{&gip->totalObj, &gip->destObj},     {&gip->totalEne, &gip->destEne},
                                       {&gip->totalBlade, &gip->destBlade}, {&gip->totalLight, &gip->numLight},
                                       {&gip->totalAlarm, &gip->numAlarm},  {&gip->totalMine, &gip->numMine},
                                       {&gip->totalHit, &gip->numHit}};
    for (int i = 0; i < 7; i++)
        if (*cnt[i][0] && *cnt[i][0] < *cnt[i][1]) *cnt[i][1] = *cnt[i][0];
    unsigned short *const cap[6] = {&gip->destObj, &gip->destEne, &gip->numLight,
                                    &gip->numAlarm, &gip->numMine, &gip->numHit};
    for (int i = 0; i < 6; i++)
        if (*cap[i] >= 1000) *cap[i] = 999;
    int ot = ps2_ftoi(0.1f + acp->mp->airtemp);
    if (ot >= 1000) ot = 999;
    if (ot < -99) ot = -99;
    gip->outtemp = ot;
    gip->rem_rate = get_remnants();
    gip->stat = acp->stat;

    /* AC status / system error / area-over warnings: blink counters and their voice cues */
    unsigned short bits = acp->dead ? 0 : acp->stat;
    for (int i = 0; i < 7; i++) {
        const struct_b7df6eb2 *an = &acstat_anim[i];
        int on = bits & 1;
        gip->acstat_frm[i] = blink(gip->acstat_frm[i], on, (short)an->cont_time);
        short s = gip->acstat_sfrm[i];
        if (s > 0) s--;
        if (s <= 0 && on) {
            s = (short)an->snd_time;
            if (an->snd_time > 0 && an->snd_id > 0) Voice_Call(an->snd_id, 0x3c0050);
        }
        bits >>= 1;
        gip->acstat_sfrm[i] = s;
    }
    unsigned short err = acp->dead ? 0 : acp->syserr;
    short err_any = (short)(err & 0xff2e), err_10 = (short)(err & 0x10);
    if (FRAME != 0) {
        for (int i = 0; i < 11; i++) {
            gip->syserr_frm[i] = blink(gip->syserr_frm[i], err & 1, (short)syserr_anim[i].cont_time);
            err >>= 1;
        }
        short s = gip->syserr_sfrm_nx[0];
        if (s > 0) s--;
        if (s <= 0 && err_any) {
            s = 0x1e;
            Voice_Call(0xd, 0x3c0050);
        }
        gip->syserr_sfrm_nx[0] = s;
        s = gip->syserr_sfrm_nx[1];
        if (s > 0) s--;
        if (s <= 0 && err_10) {
            s = 0x1e;
            Voice_Call(0xb, 0x3c0050);
        }
        gip->syserr_sfrm_nx[1] = s;
    }
    leavin_alert(gip, acp);
    unsigned short area = acp->dead ? 0 : acp->areastat;
    gip->leavin_frm = blink(gip->leavin_frm, area & 1, gip->leavin_intvl);
    short s = gip->leavin_sfrm;
    short intvl = gip->leavin_intvl;
    if (s > 0) s--;
    if (s <= 0 && (area & 1)) {
        s = intvl;
        if (intvl > 0) Voice_Call(9, 0x3c0050);
    }
    gip->leavin_sfrm = s;

    int tm = 0, ts = 0;
    if (AC_VS_RESTTIME >= 0) {
        int sec = (AC_VS_RESTTIME + 0x3b) / 0x3c;
        tm = sec / 60;
        ts = sec % 60;
    }
    gip->timem = tm;
    gip->times = ts;

    /* colours: extension in use / back unit active pulse, core intercept and hanger unit fade back after use */
    if (!ext || !ext->busy) memcpy(gip->excol, STA2_RGBA[4], 16);
    else lerp_col(gip->excol, pulse());
    if (get_eob_stat(acp) == 3) lerp_col(gip->obcol, pulse());
    else memcpy(gip->obcol, STA2_RGBA[4], 16);
    if (gip->corei >= 0) {
        float t = 3.0f + -(float)(30 - gip->corei) / 10.0f;
        if (!(t <= 1.0f)) t = 1.0f;
        lerp_col(gip->coreicol, t);
    }
    if (gip->unit_hanger >= 0) {
        float t = 6.0f + -(float)(60 - gip->unit_hanger) / 10.0f;
        if (!(t <= 1.0f)) t = 1.0f;
        lerp_col(gip->unit_hanger_col, t);
    }

    /* extension ammo (-1 unlimited, -2 none), back unit ammo */
    if (!acp->param.extp || !ext) {
        gip->exblt = -2;
    } else {
        unsigned char p = ext->prog_no;
        if (p == 0x10 || p == 0xd || p == 0xc || p == 0xb || p == 0xa) gip->exblt = ext->shot_cnt;
        else if (acp->param.extweapon.blt_initcnt) gip->exblt = acp->param.extweapon.blt_cnt;
        else gip->exblt = -1;
    }
    if (acp->param.corep->eo_num) {
        AcPartsEoSt *eo = AcCtrl_GetPartsEo(acp);
        if (eo) gip->eoblt = eo->blt_cnt;
    }

    /* locked target info, refreshed every 25 frames (kept 30 frames after the lock is lost) */
    StWepLock *pri = acp->lockmng.pri_lock;
    StModel *tgt = pri ? pri->ofp : acp->lockmng.lock[12].ofp;
    if (!tgt) {
        if (++gip->null_tgcnt >= 31) gip->plkp = 0;
    } else {
        gip->null_tgcnt = 0;
        if (gip->plkp != tgt) gip->upd_intvl = 0;
        gip->plkp = tgt;
        if (gip->upd_intvl == 0) get_eninfo(gip, tgt);
        gip->upd_intvl++;
        gip->upd_intvl = gip->upd_intvl % 25;
    }
}

/* 3 while the core's back unit (EO / OB / slow / gaze lock) is active */
static int get_eob_stat(AcPlayerSt *acp)
{
    PS2_FRAME(0x10);
    switch (acp->param.core_type) {
    case 0: {
        AcPartsEoSt *eo = AcCtrl_GetPartsEo(acp);
        return eo ? eo->stat : 0;
    }
    case 1: {
        AcPartsObSt *ob = AcCtrl_GetPartsOb(acp);
        return ob ? ob->stat : 0;
    }
    case 2: return AcCtrl_IsSelfExceptSlow(acp) ? 3 : 0;
    case 3: return AcCtrl_IsGazeLock(acp) ? 3 : 0;
    default: return 0;
    }
}

void DF_ACST2_DrawStat(int pause, int gauge)
{
    PS2_FRAME(0x80);
    if (!gauge) return;
    sceGsSyncPath(0, 0);
    if (!pause && gauge) {
        int n = 0;
        StGaugeInfo *gip = ginfo;
        for (int i = 0; i < ViewNum; i++, gip++) {
            int cam = ViewCamID[i];
            if (cam < 0 || cam >= 4 || DF_SGNL_IsGaugeOff(cam)) {
                unsigned int keep = gip->flags & 0x100;
                memset(gip, 0, sizeof *gip);
                gip->acid = -1;
                gip->maker = -1;
                gip->flags = keep;
            } else {
                update_ginfo(gip, ViewCamID[i]);
                n++;
            }
        }
        if (n == 0) return;
    }
    DF_ACST2_ReloadTMX(0);
    ginfop = ginfo;
    if (StatType == 6 || StatType == 5) __DrawCockpitGauge(gauge);
    else if (!pause && gauge) __DrawOverlookGauge();
}

void DF_ACST2_Open(int vstime, int type, int player)
{
    PS2_FRAME(0x30);
    AC_VS_RESTTIME = vstime;
    ginfop = 0;
    StatType = type;
    ZeroMemory32b(RedBack, 4);
    ZeroMemory32b(ginfo, 0xb8);
    ginfo[0].acid = -1;
    ginfo[0].maker = -1;
    ginfo[1].acid = -1;
    ginfo[1].maker = -1;
    StatTex_ReadTextureFile(type);
    StatTex_LoadVariousTexture(type);
    StatTex_LoadSightTexture(0);
    init_cockpit(player, type != 5);
}

static void set_tex1(int i)
{
    struct_d42f65aa t;
    memcpy(&t, &STA2_TEX1[i], 8);
    GS_SetTEX1_1(t);
}

/* third-person gauge screen; StatType 4 = two-player split screen (weapons and radar per half) */
static void __DrawOverlookGauge(void)
{
    PS2_FRAME(0x10);
    PS2_RQ_SCOPE(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 12), 12);   /* render tag (host only): cmdlist / weapons / radar */
    ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 3), 3); GS_NewList();
    set_tex1(StatTex_SetDrawConfigure(1) ? 1 : 0);
    GS_FlushList(); ps2_rq_tag_end();
    GSX_SetWindowSize(0x100, 0xe0, 4, 3);
    draw_cmdlist(draw_stat_tab[StatType]);   /* + 1 in a1, unused */
    if (StatType == 4) {
        if ((ginfo[0].is_radar_func == 1 || ginfo[1].is_radar_func == 1) &&
            (!(ginfo[0].ecmerror_rate < 0.5f) || !(ginfo[1].ecmerror_rate < 0.5f)))
            Radar_MakeNoiseTex(1);
        const int ulx[2] = {0x6a, 0x162}, rx[2] = {0xb5, 0x1ad};
        for (int i = 0; i < 2; i++) {
            if (ViewCamID[i] < 0 || ViewCamID[i] >= 4 || DF_SGNL_IsGaugeOff(ViewCamID[i])) continue;
            gcall_as<void(StGaugeInfo *, int, int, int)>(GFN(DF_DrawWeapon_Split), &ginfo[i], ulx[i], 0x3c, i);
            if (ginfo[i].is_radar_func == 1)
                gcall_as<void(int, StGaugeInfo *, int, int, int, int, int *, int)>(
                    GFN(DF_RADAR_Proc), ginfo[i].radar, &ginfo[i], ViewCamID[i], 1, rx[i], 0x14c, (int *)0, 0);
        }
    } else {
        DF_DrawFriendStat(ViewCamID[0]);   /* + ginfop->maker in a1, unused */
        if (ginfop->is_radar_func == 1 && !(ginfop->ecmerror_rate < 0.5f)) Radar_MakeNoiseTex(0);
        gcall_as<void(int, StGaugeInfo *, int, int, int, int, int *, int)>(
            GFN(DF_RADAR_Proc), ginfop->radar, ginfop, ViewCamID[0], 0, 0x1a8, 0x14, (int *)0, 0);
        if (ginfop->flags & 0x80)
            Altimeter_TypeA(ViewCamID[0], STA2_RGBA[0], 0);
    }
    ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 3), 3); GS_NewList();
    set_tex1(0);
    GS_FlushList(); ps2_rq_tag_end();
}

/* sets / clears HUD display flags of the first gauge */
void DF_ACST2_DispSwitch(unsigned int flag, int set)
{
    if (set) ginfo->flags |= flag;
    else ginfo->flags &= ~flag;
}

/* enemy destroyed (DFene_Kill): count it on view 0's gauge unless the pad is off or the counter is prohibited */
void GaugeFEIncreaseDestEne(void)
{
    if (MyI.padoff != 0) return;
    if (ginfo[0].prohibit != 0) return;
    ginfo[0].destEne = (unsigned short)(ginfo[0].destEne + 1);
}
