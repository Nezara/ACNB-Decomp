/* src/stat/lock.cpp - weapon lock-on and the lock sight HUD (whole file but the paths Versus never runs:
 * Lock_GetGazeLock). The two GIF packet writers
 * DF_SIGHT_SpriteDraw / draw2dsprite are at the end of the file (phase 2 g9a).
 * An AC's StWeaponLockManage keeps 13 lock entries: lock[0..11] for the main sight (multi-lock missiles use all 12)
 * and lock[12] for the sub sight of a two-sight unit. Per frame DF_get_lock collects every stage character, target
 * object and AC inside the sight range into s_bufLock, then Lock_GetLockStd / Lock_GetLockMis (chosen by the weapon's
 * prog_no through s_pfuncGetLock) pick the target with the best priority (pri_exp_*), age its lock timers and aim
 * ahead of it (DF_get_deflection: straight line; DF_get_deflection_db: from a history of target positions).
 * DF_draw_lock_sight draws the sight brackets and the per-target lock markers. */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"

#define A16 __attribute__((aligned(16)))

#ifdef ACNB_SOURCEPORT
void (*s_pfuncGetLock[5])(StWeaponLockManage *, float *, float *, _weapon_st *) = {
    Lock_GetLockStd, Lock_GetLockMis, Lock_GetLockStd, 0, Lock_GetLockStd};
int col_tab[4][4] = {{255, 255, 255, 128}, {208, 143, 24, 128}, {240, 50, 35, 128}, {50, 240, 35, 128}};
float (*pri_exp_tab[2][2])(StWepLock *, void *) = {{pri_exp_center_single, pri_exp_near_single},
                                                   {pri_exp_center_multi, pri_exp_near_multi}};
struct_ee27d1c8 numb_spr = {{0xff, 0xff, 0xff, 0x80}, {0, 0, 0, 0}, {{0, 0, 0, 0}, {0x80, 0x38, 0, 0}}, 0, 0};
int s_iTrgObjMdlNum;
#endif

/* GS TEX0 register image of a sprite table entry */
static struct_1f802d8d tex0_of(unsigned long long v)
{
    struct_1f802d8d t;
    memcpy(&t, &v, 8);
    return t;
}

/* ---------------------------------------------------------------- statistics, setup */

unsigned int GetMissLockTime(void) { return dummyMissLockTime; }
unsigned int GetLockTime(void) { return dummyLockTime; }

/* per-frame statistic of the player's AC: frames with a target locked vs frames without one (or with the missile
 * weapon or the left-arm weapon busy) */
void SetLockTime(void)
{
    PS2_FRAME(0x20);
    AcPlayerSt *acp = g_ac;
    _ac_weaponctrl *wc = &acp->weaponctrl[acp->equip_weapon];
    if (wc->busytime != 0 && wc->wp->prog_no == 1) {
        dummyMissLockTime++;
        return;
    }
    wc = AcLgun_GetWeaponCtrl(AcCtrl_GetPartsLgun(acp));
    if (wc->busytime != 0 && wc->wp->prog_no == 1) {
        dummyMissLockTime++;
        return;
    }
    for (int i = 0; i < 13; i++)
        if (acp->lockmng.lock[i].ofp) {
            dummyLockTime++;
            return;
        }
    dummyMissLockTime++;
}

void Lock_UnifyTwoSight(StWeaponLockManage *pWepLockMng) { __UnifyTwoSight(pWepLockMng); }

/* installs the sight of weapon slot order (0 main, 1 sub) and sizes the deflection history: 8 + 8 entries when
 * both sights are standard (prog 0) ones, 32 for a single one */
void Lock_SetSight(StWeaponLockManage *plockm, int order, WepSightSt *psight)
{
    PS2_FRAME(0x20);
    plockm->psight[order] = psight;
    __UnifyTwoSight(plockm);
    StLockDeflectionDb *db = plockm->pdefldb;
    if (!db) return;
    WepSightSt *p0 = plockm->psight[0], *p1 = plockm->psight[1];
    int std0 = p0 && p0->prog == 0, std1 = p1 && p1->prog == 0;
    int max_main = 0, max_sub = 0;
    if (std0 && std1) max_main = max_sub = 8;
    else if (std0) max_main = 0x20;
    else if (std1) max_sub = 0x20;
    db->max_db_main = max_main;
    db->max_db_sub = max_sub;
    db->num_db_main = 0;
    db->num_db_sub = 0;
}

/* registers a lockable target object (at most 4) */
int DF_SetTargetObjectModel(StModel *pModel)
{
    if (s_iTrgObjMdlNum >= 4) return -1;
    s_pTrgObjMdl[s_iTrgObjMdlNum++] = pModel;
    return 1;
}

/* ---------------------------------------------------------------- sight HUD */

/* keyframe chain (entries linked by next, -1 = end) at frame t: the entry holding t and the frame inside it, or the
 * last entry and its length when t is past the end */
template <class K> static const K *sight_key(const K *base, int t, int *in)
{
    const K *k = base;
    int start = 0;
    for (;;) {
        if (start <= t && t < (int)(start + k->frame)) {
            *in = t - start;
            return k;
        }
        start += k->frame;
        if (k->next == -1) {
            *in = k->frame;
            return k;
        }
        k = base + k->next;
    }
}

static int offscreen(const int *s)
{
    return s[0] < 0 || s[0] > 0x10010 || s[1] < 0 || s[1] > 0x10010 || s[2] < 0 || s[2] > 0xffe3d0;
}

/* one lock marker (sight_company[prog][time]) at screen position sloc, animated by sight_data: each part slides
 * from xy0 to xy1 and fades alpha0 -> alpha1; time0 is scaled by lock_speed / sight_lock_speed (capped at 1) so
 * faster locks play the animation faster; col_no = row of sight_col */
static int DF_SIGHT_DrawSight(int *sloc, int company, int prog, int time, int time0, int lock_speed, int col_no)
{
    PS2_FRAME(0x210);
    (void)company;
    int off[10][4] A16, rgb[10][4] A16, xyz[2][4] A16;
    float rate = ps2_div((float)lock_speed, (float)sight_lock_speed[prog][time]);
    if (!(rate <= 1.0f)) rate = 1.0f;
    int t = ps2_ftoi(ps2_div((float)time0, rate));
    const struct_6218d071 *sd = &sight_data[prog][time];
    for (int i = 0; i < sight_anim_max[prog][time]; i++) {
        int mt, at;
        const _sight_move_st_ *mv = sight_key(sd->move + sd->n_move[i], t, &mt);
        const _sight_alpha_st_ *al = sight_key(sd->alpha + sd->n_alpha[i], t, &at);
        float x0 = 16.0f * (float)mv->xy0[0], y0 = 8.0f * (float)mv->xy0[1];
        float dx = ps2_div(16.0f * (float)mv->xy1[0] - x0, (float)mv->frame);
        off[i][0] = ps2_ftoi((float)mt * dx + x0);
        float dy = ps2_div(8.0f * (float)mv->xy1[1] - y0, (float)mv->frame);
        off[i][1] = ps2_ftoi((float)mt * dy + y0);
        memcpy(rgb[i], sight_col[col_no], 16);
        float da = ps2_div(al->alpha1 - al->alpha0, (float)al->frame);
        rgb[i][3] = ps2_ftoi(al->alpha0 + (float)at * da);
    }
    int num = sight_company[prog][time].num;
    struct_ee27d1c8 *spr = sight_company[prog][time].spr;
    for (int i = 0; i < num; i++, spr++) {
        xyz[0][0] = sloc[0] + spr->xyz[0][0] + off[i][0];
        xyz[0][1] = sloc[1] + spr->xyz[0][1] + off[i][1];
        xyz[0][2] = -1;
        xyz[0][3] = 0;
        xyz[1][0] = sloc[0] + spr->xyz[1][0] + off[i][0];
        xyz[1][1] = sloc[1] + spr->xyz[1][1] + off[i][1];
        xyz[1][2] = -1;
        xyz[1][3] = 0;
        draw2dsprite(rgb[i], xyz, tex0_of(spr->tex0), spr->uv);
    }
    return 0;
}

/* the lock range brackets in front of the AC (sight bit 0: 8 line segments at 1000 units, half-width
 * 1000 sin(lock_ang)) and the range scan lines (sight bit 1: 4 short lines running away from the AC) */
static void draw_wep_sight(StWeaponLockManage *lockm, float *loc, float vangx, float vangy, float (*ws)[4])
{
    PS2_FRAME(0xb0);
    if (!lockm->sightUni.enable) return;
    StWepLock *pri = lockm->pri_lock;
    float m[4][4] A16;
    sceVu0RotMatrixX(m, IdentityMtx, vangx);
    sceVu0RotMatrixY(m, m, vangy);
    sceVu0TransMatrix(m, m, loc);
    vu_mul_matrix(m, (const float (*)[4])ws, (const float (*)[4])m);
    for (int i = 0; i < 8; i++) wr_vtx[i][3] = 1.0f;

    if (lockm->sightUni.sight & 1) {
        int *rgb = (pri && (pri->lock_stat & 8)) ? col_tab[2] : col_tab[1];
        float w = 1000.0f * fsmath_SinVU0(lockm->sightUni.lock_angy);
        float h = 1000.0f * fsmath_SinVU0(lockm->sightUni.lock_angx);
        for (int i = 0; i < 8; i++) wr_vtx[i][2] = 1000.0f;
        wr_vtx[0][0] = w - 25.0f;  wr_vtx[0][1] = -h;
        wr_vtx[1][0] = w;          wr_vtx[1][1] = 25.0f - h;
        wr_vtx[2][0] = w;          wr_vtx[2][1] = h - 25.0f;
        wr_vtx[3][0] = w - 25.0f;  wr_vtx[3][1] = h;
        wr_vtx[4][0] = 25.0f - w;  wr_vtx[4][1] = h;
        wr_vtx[5][0] = -w;         wr_vtx[5][1] = h - 25.0f;
        wr_vtx[6][0] = -w;         wr_vtx[6][1] = 25.0f - h;
        wr_vtx[7][0] = 25.0f - w;  wr_vtx[7][1] = -h;
        int out = 0;
        for (int i = 0; i < 8; i++) {
            sceVu0RotTransPers(wr_scr[i], m, wr_vtx[i], 0);
            if (offscreen(wr_scr[i])) out++;
            wr_scr[i][2] = -1;
        }
        if (!out) {
            memcpy(wr_scr[8], wr_scr[0], 16);
            for (int i = 0; i < 8; i++) DF_Draw2DLine_Color(rgb, &wr_scr[i], 0x2060c000);
        }
    }

    if (lockm->sightUni.sight & 2) {
        int col[4] A16;
        memcpy(col, col_tab[2], 16);
        unsigned int z = FRAME * 7u % 0xafu;
        wr_vtx[0][0] = -15.0f;
        wr_vtx[1][0] = 15.0f;
        wr_vtx[1][1] = 0.0f;
        wr_vtx[0][1] = 0.0f;
        for (int i = 0; i < 4; i++, z += 0xaf) {
            float d = (float)(int)(z + 100);
            wr_vtx[1][2] = d;
            wr_vtx[0][2] = d;
            if (100.0f <= d && d < 190.0f) col[3] = ps2_ftoi(80.0f * ((d - 100.0f) / 90.0f));
            else if (190.0f <= d && d < 310.0f) col[3] = 0x50;
            else if (310.0f <= d && d < 800.0f) col[3] = ps2_ftoi(80.0f * (1.0f - (d - 310.0f) / 490.0f));
            sceVu0RotTransPers(wr_scr[0], m, wr_vtx[0], 0);
            sceVu0RotTransPers(wr_scr[1], m, wr_vtx[1], 0);
            if (offscreen(wr_scr[0]) || offscreen(wr_scr[1])) continue;
            wr_scr[1][2] = -1;
            wr_scr[0][2] = -1;
            ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 4), 4); GS_NewList();
            GS_SetALPHA_1(0, 1, 0, 1, 0);
            GS_SetTEST_1(1, 1, 0, 0, 0, 0, 1, 1);
            GS_Begin(0x4041 /* line, Gouraud, alpha blend */, 2, 0x41, 2);
            GS_Color4iv(col);
            GS_Vertex4iv(wr_scr[0]);
            GS_Color4iv(col);
            GS_Vertex4iv(wr_scr[1]);
            GS_End();
            GS_FlushList(); ps2_rq_tag_end();
        }
    }
}

/* number in the sight font, 0x80 (8 px) per character; non-digits leave a gap */
static void draw_numb_fnt(int ulx, int uly, char *fmt, int numb, int *col)
{
    PS2_FRAME(0x1c0);
    char buf[256];
    int xyz[2][4] A16;
    memcpy(numb_spr.rgb, col, 16);
    ps2_sprintf_d(buf, fmt, numb);
    for (const char *p = buf; *p; ulx += 0x80) {
        int d = *p++ - '0';
        if (d < 0 || d >= 10) continue;
        memcpy(numb_spr.uv, sight_numb_fnt[d].uv, 16);
        numb_spr.tex0 = sight_numb_fnt[d].tex0;
        xyz[0][0] = ulx + numb_spr.xyz[0][0];
        xyz[0][1] = uly + numb_spr.xyz[0][1];
        xyz[0][2] = -1;
        xyz[0][3] = 0;
        xyz[1][0] = ulx + numb_spr.xyz[1][0];
        xyz[1][1] = uly + numb_spr.xyz[1][1];
        xyz[1][2] = -1;
        xyz[1][3] = 0;
        draw2dsprite(numb_spr.rgb, xyz, tex0_of(numb_spr.tex0), numb_spr.uv);
    }
}

/* draws the markers of the first ln lock entries that are on screen. Marker state ("time"): the entry's prog for
 * the main sight (4 when the sub sight holds the same target), 3 for the sub sight (5 = same target as the main).
 * Colour row: 1 searching, 2 locked, 3 locked but the line of sight is blocked, 4 entry flag 0x100. Each marker
 * also gets the weapon's reload gauge, the distance and one icon per missile locked on the target. */
static void draw_wep_lock(StWeaponLockManage *lockmngp, int player, int ln, float (*ws)[4])
{
    PS2_FRAME(0x1f0);
    StWepLock *company = lockmngp->lock;
    int prog = lockmngp->sightUni.type;
    StModel *main_ofp = 0, *sub_ofp = 0, *col_ofp = 0;
    int col = 0;
    int scr[4] A16, xyz[2][4] A16;
    float hit[4] A16;
    while (ln--) {
        if (!company->ofp || !(company->lock_stat & 0xe)) {
            company++;
            continue;
        }
        DF_CALC_ScreenPointEx(scr, ws, company->loc);
        if (scr[0] < 0 || scr[0] > 0xfff0 || scr[1] < 0 || scr[1] > 0xfff0 || scr[2] < 0) {
            company++;
            continue;
        }
        int time;
        if (company->sorder == 1) {
            sub_ofp = company->ofp;
            time = sub_ofp == main_ofp ? 5 : 3;
        } else if (company->sorder == 0) {
            time = company->prog;
            if (time == 0 || time == 2) {
                main_ofp = company->ofp;
                if (main_ofp == sub_ofp) time = 4;
            }
        } else {
            time = company->prog;
        }
        /* snap to whole pixels, then ease the marker a quarter of the way per frame */
        scr[0] &= ~0xf;
        scr[1] &= ~0xf;
        for (int k = 0; k < 2; k++) {
            int d = (int)((unsigned int)scr[k] - (unsigned int)company->sloc[k]) >> 2;
            if (d == 0) {
                company->sloc[k] = scr[k];
            } else {
                scr[k] = company->sloc[k] + d;
                company->sloc[k] = scr[k];
            }
        }
        if (company->lock_stat & 0x100) {
            col = 4;
        } else if (!(company->lock_stat & 8)) {
            col = 1;
        } else {
            StModel *prev = col_ofp;
            col_ofp = company->ofp;
            if (prev != col_ofp) {
                if (Sortie_GetAcOption(player)->level == 1) col = 2;
                else col = HetProcPoint2Point(company->loc, g_ac[player].lockmng.gloc, hit, 3) ? 3 : 2;
            }
        }
        if (time == 0 || (unsigned int)(time - 2) < 3 || time == 5)
            DF_SIGHT_DrawSight(scr, (int)PS2PTR(company), prog, time, company->lock_lrtime, 0xf, col);
        else
            DF_SIGHT_DrawSight(scr, (int)PS2PTR(company), prog, time,
                               (company->lock_stat & 8) ? lockmngp->sightUni.lock_speed : company->lock_artime2,
                               lockmngp->sightUni.lock_speed, col);
        /* quirk: an entry with flag 0x100 is not stepped over, so it is drawn again for the remaining count */
        if (company->lock_stat & 0x100) continue;

        struct_ee27d1c8 *spr = sight_company[prog + 3][time].spr;
        int n = (col == 2 || col == 3) ? sight_company[prog + 3][time].num : sight_company[prog + 3][time].num - 1;
        for (int k = 0; k < n; k++) memcpy(spr[k].rgb, sight_col[col], 16);
        int xoff = 0, yoff = 0;
        if (lockmngp->pri_lock && lockmngp->pri_lock->prog == 1) {
            xoff = -0x100;
            yoff = 0x30;
        }
        int sub = time == 3 || time == 5;
        int bx = sub ? scr[0] + xoff : scr[0], by = sub ? scr[1] + yoff : scr[1];
        for (int k = 0; k < n; k++) {
            xyz[0][0] = bx + spr[k].xyz[0][0];
            xyz[0][1] = by + spr[k].xyz[0][1];
            xyz[0][2] = -1;
            xyz[0][3] = 0;
            xyz[1][0] = bx + spr[k].xyz[1][0];
            xyz[1][1] = by + spr[k].xyz[1][1];
            xyz[1][2] = -1;
            xyz[1][3] = 0;
            draw2dsprite(spr[k].rgb, xyz, tex0_of(spr[k].tex0), spr[k].uv);
        }
        /* reload gauge: the left-arm weapon for the sub sight, the equipped weapon otherwise */
        AcPlayerSt *acp = &g_ac[player];
        if (sub) {
            _ac_weaponctrl *wc = AcLgun_GetWeaponCtrl(AcCtrl_GetPartsLgun(acp));
            int busy = wc->busytime, rt = wc->wp->reload_time;
            if (rt < busy) busy = rt;
            float r = ps2_div((float)(rt - busy), (float)rt);
            DF_SIGHT_SpriteDraw(xoff + (scr[0] + spr->xyz[0][0] + 0x350), yoff + (scr[1] + spr->xyz[0][1] + 0x40),
                                -ps2_ftoi(52.0f * r), 1, sight_col[col]);
        } else {
            _ac_weaponctrl *wc = &acp->weaponctrl[acp->equip_weapon];
            int busy = wc->busytime, rt = wc->wp->reload_time;
            if (rt < busy) busy = rt;
            float r = ps2_div((float)(rt - busy), (float)rt);
            DF_SIGHT_SpriteDraw(scr[0] + spr->xyz[0][0] + 0xc0, scr[1] + spr->xyz[0][1] + 0x40, ps2_ftoi(50.0f * r), 1,
                                sight_col[col]);
        }
        const int (*doff)[2] = prog == 2 ? dist_offsetC : prog == 1 ? dist_offsetB : dist_offsetA;
        if (sub)
            draw_numb_fnt(xoff + (scr[0] + doff[time][0]), yoff + (scr[1] + doff[time][1]), (char *)"%-4d",
                          ps2_ftoi(company->dist), sight_col[col]);
        else
            draw_numb_fnt(scr[0] + doff[time][0], scr[1] + doff[time][1], (char *)"%4d", ps2_ftoi(company->dist),
                          sight_col[col]);
        if (company->lock_count > 0) {
            for (int k = 0; k < company->lock_count; k++) memcpy(mis_spr[k].rgb, sight_col[col], 16);
            int cnt = company->lock_count;
            for (int k = 0; k < cnt; k++) {
                xyz[0][0] = scr[0] + mis_spr[k].xyz[0][0];
                xyz[0][1] = scr[1] + mis_spr[k].xyz[0][1];
                xyz[0][2] = -1;
                xyz[0][3] = 0;
                xyz[1][0] = scr[0] + mis_spr[k].xyz[1][0];
                xyz[1][1] = scr[1] + mis_spr[k].xyz[1][1];
                xyz[1][2] = -1;
                xyz[1][3] = 0;
                draw2dsprite(mis_spr[k].rgb, xyz, tex0_of(mis_spr[k].tex0), mis_spr[k].uv);
            }
        }
        company++;
    }
}

/* the lock HUD of player's AC: sight texture page, markers of the weapon and gaze locks, missile seeker, brackets */
void DF_draw_lock_sight(int player, float (*ws)[4])
{
    PS2_FRAME(0x50);
    if (player < 0 || player >= 4) return;
    AcPlayerSt *acp = &g_ac[player];
    if (!acp->lockmng.sightUni.enable) return;
    if (CurSightType != acp->lockmng.sightUni.type) {
        StatTex_LoadSightTexture(acp->lockmng.sightUni.type);
        CurSightType = acp->lockmng.sightUni.type;
        ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 4), 4); GS_NewList();
        GS_SetTEXFLUSH();
        struct_f386fb29 tex0;
        memcpy(&tex0, &STA2_TEXTURE[0], 8);
        GS_SetTEX0_1(tex0);
        GS_FlushList(); ps2_rq_tag_end();
    }
    ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 4), 4); GS_NewList();
    GS_SetALPHA_1(0, 1, 0, 1, 0);
    GS_SetTEST_1(1, 7, 0, 0, 0, 1, 1, 1);
    GS_FlushList(); ps2_rq_tag_end();
    draw_wep_lock(&acp->lockmng, player, 13, ws);
    StWeaponLockManage *gaze = AcCtrl_GetGazeLockMng(acp);
    if (gaze && gaze->sightUni.enable) draw_wep_lock(gaze, player, 1, ws);
    StWepLock *pri = acp->lockmng.pri_lock;
    if (pri && pri->prog == 1 && (pri->lock_stat & 2) && (acp->lockmng.flag & 1))
        draw_mis_seeker(&acp->lockmng, ws);
    float loc[4] A16 = {acp->loc[0], acp->loc[1], acp->loc[2], 1.0f};
    float vangy = inverse_angf(acp->ang[1]);
    draw_wep_sight(&acp->lockmng, loc, acp->view.ang[0], vangy, ws);
}

/* ---------------------------------------------------------------- lock entries */

/* iterator over the locked entries (lock_count > 0) of a lock manager, for multi-lock weapons */
int DF_get_next_lock(StNextLock *nlp)
{
    while (nlp->cnt-- != 0) {
        StWepLock *l = nlp->next_lkp;
        if (l->ofp && l->lock_count) {
            nlp->ofp = l->ofp;
            nlp->lkp = l;
            nlp->next_lkp = l + 1;
            return 1;
        }
        nlp->next_lkp = l + 1;
    }
    return 0;
}

void DF_init_next_lock(StNextLock *nlp, StWeaponLockManage *lockm)
{
    nlp->cnt = 0xc;
    nlp->next_lkp = lockm->lock;
    nlp->lkp = 0;
    nlp->ofp = 0;
}

void DF_discard_lock(StWepLock *lkp)
{
    if (lkp->lock_count > 0 && --lkp->lock_count == 0) lkp->lock_stat &= ~8;
}

void Lock_GetLockX(StWeaponLockManage *lockm, float *loc, float *ang, _weapon_st *wp)
{
    PS2_FRAME(0x10);
    if (s_pfuncGetLock[wp->prog_no % 5]) gcall(s_pfuncGetLock[wp->prog_no % 5], lockm, loc, ang, wp);
}

/* game voice se_id_tab[sight type][k] for the AC's own view (k: 0 searching, 1 locked, 2 missile scanning,
 * 3 missile locked, 4 missile lock full) */
static void lock_voice(StWeaponLockManage *lockm, int k)
{
    if (!lockm->option_nose && GetPlayerViewID(g_ac[lockm->id].id) >= 0)
        Voice_Call(se_id_tab[lockm->sightUni.type][k], 0x80);
}

/* tells a locked AC that it is locked on (lock warning) */
static void mark_locked(StModel *m)
{
    if (m && m->type == 4) g_ac[m->aid].stat |= 0x40;
}

static int in_angle(float a, float lim) { return !(a < -lim) && a <= lim; }

/* multi-lock (missiles): keeps the entries whose targets are still inside the sight, adds the new targets inside
 * the lock angle, then sweeps the scan line to the best not-yet-full target and adds one lock to it after
 * lock_speed + fcs_err_time frames on it */
void Lock_GetLockMis(StWeaponLockManage *lockm, float *loc, float *ang, _weapon_st *wep)
{
    PS2_FRAME(0xb0);
    lockm->vangx = ang[0];
    lockm->vangy = ang[1];
    if (lockm->sightUni.lock_max <= 0 || (lockm->flag & 4)) return;
    int lock_max = wep->sight_order == 0 ? lockm->psight[0]->lock_max : lockm->psight[1]->lock_max;
    memcpy(lockm->gloc, loc, 16);
    float range = wep->range;
    if (lockm->sightUni.range < range) range = lockm->sightUni.range;
    int n = DF_get_lock(s_bufLock, loc, ang, (unsigned short)(lockm->ulock_mask | 0x2000), range,
                        lockm->sightUni.lock_angx, lockm->sightUni.lock_angy, lockm->type, lockm->id);
    if (n <= 0) {
        Lock_ClearMng(lockm, wep->sight_order);
        return;
    }
    StWepLock *l = lockm->lock;
    for (int k = 12; k--; l++) {
        if (!l->ofp) continue;
        if (!in_angle(l->ang[0], lockm->sightUni.angx) || !in_angle(l->ang[1], lockm->sightUni.angy)) {
            l->ofp = 0;
            l->lock_count = 0;
        }
    }
    l = lockm->lock;
    for (int k = 12; k--; l++) {
        if (!l->ofp) continue;
        StWepLock *b = s_bufLock, *f = 0;
        for (int j = n; j--; b++)
            if (b->ofp == l->ofp) {
                f = b;
                break;
            }
        if (!f) {
            l->ofp = 0;
            l->lock_artime2 = 0;
            continue;
        }
        l->dist = f->dist;
        memcpy(l->loc, f->loc, 16);
        memcpy(l->ang, f->ang, 16);
        l->lock_stat = (short)((l->lock_stat & ~3) | (f->lock_stat & 3));
        if (l->lock_stat & 2) l->lock_artime2++;
        else l->lock_artime2 = 0;
        f->ofp = 0;
    }
    unsigned char prog = wep->prog_no;
    StWepLock *b = s_bufLock;
    for (int j = n; j--; b++) {
        if (!b->ofp || !(b->lock_stat & 2)) continue;
        l = lockm->lock;
        for (int k = 12; k--; l++) {
            if (l->ofp) continue;
            l->ofp = b->ofp;
            l->dist = b->dist;
            l->lock_lrtime = 0;
            l->lock_artime = 0;
            l->lock_lktime = 0;
            l->lock_count = 0;
            l->lock_stat = b->lock_stat;
            memcpy(l->loc, b->loc, 16);
            memcpy(l->ang, b->ang, 16);
            l->sloc[1] = 0x8000;
            l->sloc[0] = 0x8000;
            l->prog = prog;
            break;
        }
    }
    lockm->lock_cnt = (unsigned short)count_lockon(lockm);
    for (int i = 0; i < lockm->lock_cnt; i++) mark_locked(lockm->lock[i].ofp);
    if (!(lockm->lock_cnt < lock_max) || !(lockm->lock_cnt < wep->blt_cnt)) {
        lockm->flag &= ~1;
        return;
    }
    unsigned char fl = lockm->sightUni.flag;
    float (*pri_exp)(StWepLock *, void *) = (&pri_exp_tab[0][0])[(fl >> 4 & 0xf) * 2 + (fl & 0xf)];
    StWepLock *best = lockm->lock, *c = lockm->lock;
    for (int k = 12; --k;) {
        c++;
        if (cmp_std_lock(best, c, 0, pri_exp) > 0) best = c;
    }
    lockm->pri_lock = best;
    if (!best->ofp || !(best->lock_stat & 2) || !(best->lock_count < 12)) {
        lockm->flag &= ~1;
        Lock_ClearMng(lockm, wep->sight_order);
        return;
    }
    lockm->flag |= 1;
    float step = (0.2f * (float)lockm->sightUni.lock_time) / 256.0f;
    lockm->scanl_angx = go_angf(lockm->scanl_angx, best->ang[0], step);
    lockm->scanl_angy = go_angf(lockm->scanl_angy, best->ang[1], step);
    if (!(lockm->scanl_angy < ps2_u2f(0x40490fdb))) lockm->scanl_angy = lockm->scanl_angy - ps2_u2f(0x40c90fdb);
    float dx = sub_angf(lockm->scanl_angx, best->ang[0]);
    float dy = sub_angf(lockm->scanl_angy, best->ang[1]);
    if (dx < 0.0f) dx = -dx;
    if (!(dx < 0.1f)) return;
    if (dy < 0.0f) dy = -dy;
    if (!(dy < 0.1f)) return;
    if (lockm->sightUni.lock_speed + lockm->fcs_err_time < best->lock_artime) {
        best->lock_stat |= 8;
        best->lock_artime = 0;
        best->lock_count++;
        lockm->flag &= ~1;
        lockm->rest_lock = (unsigned short)(lock_max - lockm->lock_cnt - 1);
        lock_voice(lockm, lockm->rest_lock == 0 ? 4 : 3);
        lockm->scanl_angx = ang[0];
        lockm->scanl_angy = 0.0f;
    } else {
        best->lock_artime++;
        if (FRAME % 10 == 0) lock_voice(lockm, 2);
    }
}

/* single lock: lock[0] (main sight) or lock[12] (sub sight) follows the best target in the sight; inside the lock
 * angle it locks after secondlock_time * system_update_speed / 100 + fcs_err_time frames, and once locked the aim
 * point (dloc / ang) leads the target */
void Lock_GetLockStd(StWeaponLockManage *lockm, float *loc, float *ang, _weapon_st *wep)
{
    PS2_FRAME(0x130);
    int lock_wait = (int)((unsigned int)wep->secondlock_time * lockm->sightUni.system_update_speed) / 100;
    lockm->vangx = ang[0];
    lockm->vangy = ang[1];
    if (lockm->sightUni.lock_max <= 0 || (lockm->flag & 4)) return;
    StLockDeflectionDb *db = lockm->pdefldb;
    float (*tloc_db)[4] = 0;
    float *tang_db = 0;
    int *num_db = 0, max_db = 0;
    if (db) {
        if (wep->sight_order == 0) {
            tloc_db = db->loc_db;
            tang_db = db->angy_db;
            num_db = &db->num_db_main;
            max_db = db->max_db_main;
        } else {
            max_db = db->max_db_sub;
            num_db = &db->num_db_sub;
            tloc_db = db->loc_db + db->max_db_main;
            tang_db = db->angy_db + db->max_db_main;
        }
    }
    StWepLock *l;
    float range;
    if (wep->sight_order == 0) {
        l = lockm->lock;
        lockm->pri_lock = l;
        range = lockm->psight[0]->range;
    } else {
        l = &lockm->lock[12];
        range = lockm->psight[1]->range;
    }
    short prev_stat = l->lock_stat;
    memcpy(lockm->gloc, loc, 16);
    if (!(range <= lockm->sightUni.range)) range = lockm->sightUni.range;
    int n = DF_get_lock(s_bufLock, loc, ang, (unsigned short)(lockm->ulock_mask | 0x2000), range, lockm->sightUni.angx,
                        lockm->sightUni.angy, lockm->type, lockm->id);
    if (n <= 0) {
        l->ofp = 0;
        l->lock_stat = 0;
        return;
    }
    StModel *prev_ofp = l->ofp;
    float (*pri_exp)(StWepLock *, void *) = (&pri_exp_tab[0][0])[lockm->sightUni.flag & 0xf];
    StWepLock *best = s_bufLock, *c = s_bufLock;
    while (--n) {
        c++;
        if (cmp_std_lock(best, c, prev_ofp, pri_exp) > 0) best = c;
    }
    if (l->ofp != best->ofp) {
        /* new target: take the whole entry, restart the timers */
        memcpy(l, best, sizeof *l);
        memset(l->mvec, 0, 16);
        memset(l->dloc, 0, 16);
        l->sloc[1] = 0x8000;
        l->sloc[0] = 0x8000;
        l->lock_stat &= ~8;
        l->lock_lrtime = 0;
        l->lock_artime = 0;
        l->lock_lktime = 0;
        l->prog = wep->prog_no;
        l->sorder = wep->sight_order;
        if (lockm->pdefldb) *num_db = 0;
    } else {
        for (int k = 0; k < 3; k++) best->mvec[k] = best->loc[k] - l->loc[k];
        if (lockm->pdefldb) {
            for (int i = max_db - 1; i > 0; i--) {
                memcpy(tloc_db[i], tloc_db[i - 1], 16);
                tang_db[i] = tang_db[i - 1];
            }
            memcpy(tloc_db[0], l->loc, 16);
            tloc_db[0][3] = 1.0f;
            tang_db[0] = l->ofp->ang[1];
            *num_db = *num_db + 1;
            if (max_db < *num_db) *num_db = max_db;
        }
        memcpy(l->loc, best->loc, 16);
        memcpy(l->dloc, best->loc, 16);
        memcpy(l->ang, best->ang, 16);
        l->lock_stat = best->lock_stat;
        l->dist = best->dist;
        if (l->lock_lrtime < 0x4000) l->lock_lrtime++;
        if ((l->lock_stat & 2) && in_angle(l->ang[0], lockm->sightUni.lock_angx) &&
            in_angle(l->ang[1], lockm->sightUni.lock_angy)) {
            l->lock_stat |= 4;
            if (!(prev_stat & 8)) {
                float v[4] A16;
                memcpy(l->dloc, l->loc, 16);
                for (int k = 0; k < 3; k++) v[k] = l->dloc[k] - loc[k];
                v[3] = l->dloc[3];
                vec2ang(l->ang, v);
            } else if (!lockm->pdefldb) {
                int acc = ps2_div32(l->lock_lktime, lockm->sightUni.lock_time);
                int defl = wep->deflect;
                if (lockm->flag & 0x10) defl >>= 1;
                if (defl < acc) acc = defl;
                DF_get_deflection(l->dloc, l->ang, l->loc, best->mvec, loc, wep->shotspeed, acc);
            } else {
                int acc = 8 - wep->deflect;
                if (acc < 0) acc = 0;
                DF_get_deflection_db(l->dloc, l->ang, tloc_db, tang_db, *num_db, loc, wep->shotspeed, acc, best->ofp);
            }
            l->ang[0] = sub_angf(l->ang[0], ang[0]);
            l->ang[1] = sub_angf(l->ang[1], ang[1]);
            if (l->lock_lktime < 0x4000) l->lock_lktime++;
            if (l->lock_lktime > 0 && lock_wait + lockm->fcs_err_time < l->lock_artime) {
                if (!(prev_stat & 8)) lock_voice(lockm, 1);
                l->lock_stat |= 8;
                mark_locked(best->ofp);
            } else {
                if (l->lock_artime < 0x4000) l->lock_artime++;
                l->lock_lktime = 0;
                if (lockm->pdefldb) *num_db = 0;
            }
        } else {
            l->lock_lktime = 0;
            l->lock_artime = 0;
            if (lockm->pdefldb) *num_db = 0;
        }
        if (FRAME % 10 == 0 && !(l->lock_stat & 8)) lock_voice(lockm, 0);
    }
    if (l->lock_stat & 2) {
        lockm->jangx = ang[0] + l->ang[0];
        lockm->jangy = ang[1];
    } else {
        l->ofp = 0;
    }
}

/* aim point for a target that keeps its recent motion: velocity / acceleration (in the target's frame, from the
 * last num_tlocdb positions and yaws, newest first) averaged over up to 4 samples, damped per axis by the number of
 * direction changes, speed capped at the fastest recent speed (reduced at random by the weapon's inaccuracy acc).
 * The target is stepped one frame at a time until the bullet (speed per frame from wloc) is closest to it. */
static void DF_get_deflection_db(float *loc, float *ang, float (*tloc_dbp)[4], float *tang_dbp, int num_tlocdb,
                                 float *wloc, float speed, int acc, StModel *mp)
{
    PS2_FRAME(0x690);
    static const float V0[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float vel[32][4], dvel[32][4], dang[32], ddang[32];
    for (int i = 0; i < 32; i++) {
        memcpy(vel[i], V0, 16);
        memcpy(dvel[i], V0, 16);
        dang[i] = 0.0f;
        ddang[i] = 0.0f;
    }
    for (int i = 0; i < num_tlocdb - 1; i++) {
        float d[3];
        for (int k = 0; k < 3; k++) d[k] = tloc_dbp[i][k] - tloc_dbp[i + 1][k];
        float t = d[0] * fsmath_CosVU0(-tang_dbp[i + 1]);
        vel[i][0] = t - d[2] * fsmath_SinVU0(-tang_dbp[i + 1]);
        vel[i][1] = d[1];
        t = d[0] * fsmath_SinVU0(-tang_dbp[i + 1]);
        vel[i][2] = t + d[2] * fsmath_CosVU0(-tang_dbp[i + 1]);
        vel[i][3] = 1.0f;
        dang[i] = tang_dbp[i] - tang_dbp[i + 1];
        if (i > 0) {
            for (int k = 0; k < 3; k++) dvel[i - 1][k] = vel[i - 1][k] - vel[i][k];
            dvel[i - 1][3] = vel[i - 1][3];
            ddang[i - 1] = dang[i - 1] - dang[i];
        }
    }
    /* smooth out single-sample acceleration spikes */
    for (int i = 1; i < num_tlocdb - 2; i++)
        for (int k = 0; k < 3; k++)
            if ((dvel[i - 1][k] - dvel[i][k]) * (dvel[i][k] - dvel[i + 1][k]) < -0.01f) {
                dvel[i][k] = 0.5f * (dvel[i + 1][k] + dvel[i - 1][k]);
                vel[i][k] = 0.5f * (vel[i + 1][k] + vel[i - 1][k]);
            }
    float avel[4], aacc[4], adang = 0.0f, addang = 0.0f;
    memcpy(avel, V0, 16);
    if (num_tlocdb >= 2) {
        float sum[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        int n = num_tlocdb < 5 ? num_tlocdb - 1 : 4;
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                for (int k = 0; k < 3; k++) sum[k] = sum[k] + vel[i][k];
                adang = adang + dang[i];
            }
            float r = 1.0f / (float)n;
            for (int k = 0; k < 3; k++) avel[k] = sum[k] * r;
            avel[3] = sum[3];
            adang = adang / (float)n;
        }
    }
    memcpy(aacc, V0, 16);
    if (num_tlocdb >= 3) {
        float sum[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        int n = num_tlocdb < 6 ? num_tlocdb - 2 : 4;
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                for (int k = 0; k < 3; k++) sum[k] = sum[k] + dvel[i][k];
                addang = addang + ddang[i];
            }
            float r = 1.0f / (float)n;
            for (int k = 0; k < 3; k++) aacc[k] = sum[k] * r;
            aacc[3] = sum[3];
            addang = addang / (float)n;
        }
    }
    float maxsq = 0.0f, maxspd = 0.0f, maxdang = 0.0f;
    if (num_tlocdb > 0) {
        for (int i = 0; i < num_tlocdb - 2; i++) {
            float sq = vel[i][0] * vel[i][0] + vel[i][1] * vel[i][1];
            sq = sq + vel[i][2] * vel[i][2];
            if (!(sq <= maxsq)) maxsq = sq;
            float a = dang[i];
            if (a < 0.0f) a = -a;
            if (!(a <= maxdang)) {
                maxdang = dang[i];
                if (maxdang < 0.0f) maxdang = -maxdang;
            }
        }
        maxspd = ps2_sqrt(maxsq);
    }
    for (int k = 0; k < 3; k++) {
        int c = 0;
        for (int i = 0; i < num_tlocdb - 2; i++)
            if (vel[i][k] * vel[i + 1][k] < 0.0f) c++;
        if (c > 0) {
            avel[k] = avel[k] / (float)c;
            aacc[k] = aacc[k] / (float)(c * c);
        }
    }
    int rnd = ps2_rand();
    maxspd = maxspd * (1.0f - ((float)acc * (0.1f * (float)rnd)) / 2147483648.0f);

    float pos[4] A16, dir[4] A16, v[4] A16;
    memcpy(pos, tloc_dbp[0], 16);
    float yaw = tang_dbp[0], best = ps2_u2f(0x7f7fffff);
    for (int step = 0;; step++) {
        float rot[3];
        float t = avel[0] * fsmath_CosVU0(yaw);
        rot[0] = t - avel[2] * fsmath_SinVU0(yaw);
        rot[1] = avel[1];
        t = avel[0] * fsmath_SinVU0(yaw);
        rot[2] = t + avel[2] * fsmath_CosVU0(yaw);
        for (int k = 0; k < 3; k++) pos[k] = pos[k] + rot[k];
        for (int k = 0; k < 3; k++) avel[k] = avel[k] + aacc[k];
        float sq = avel[0] * avel[0] + avel[1] * avel[1];
        sq = avel[2] * avel[2] + sq;
        if (!(sq <= maxsq)) {
            float s = ps2_div(maxspd, ps2_sqrt(sq));
            for (int k = 0; k < 3; k++) avel[k] = avel[k] * s;
        }
        yaw = yaw + adang;
        adang = adang + addang;
        if (!(adang <= maxdang)) adang = maxdang;
        if (adang < -maxdang) adang = -maxdang;
        /* where the bullet is after step frames when fired at the target's current position */
        for (int k = 0; k < 3; k++) dir[k] = pos[k] - wloc[k];
        dir[3] = pos[3];
        acvu0_Normalize_ni(dir, dir);
        float s = (float)step * speed;
        for (int k = 0; k < 3; k++) dir[k] = dir[k] * s;
        for (int k = 0; k < 3; k++) v[k] = pos[k] - (dir[k] + wloc[k]);
        float dsq = v[0] * v[0] + v[1] * v[1];
        dsq = v[2] * v[2] + dsq;
        if (best <= dsq) break;
        best = dsq;
    }
    if (mp) {
        float lim = mp->yGround - 0.5f * mp->tall;
        if (!(pos[1] <= lim)) pos[1] = lim;
    }
    memcpy(loc, pos, 16);
    if (ang) {
        for (int k = 0; k < 3; k++) v[k] = pos[k] - wloc[k];
        v[3] = pos[3];
        vec2ang(ang, v);
    }
}

/* -1 when v1p ranks before v0p (empty entries last, then lower pri_exp), 1 after, 0 tie */
static int cmp_std_lock(void *v1p, void *v0p, void *infop, float (*pri_exp)(StWepLock *, void *))
{
    PS2_FRAME(0x50);
    StWepLock *a = (StWepLock *)v1p, *b = (StWepLock *)v0p;
    if (!b->ofp) return -(a->ofp != 0);
    if (!a->ofp) return 1;
    float pb = gcall(pri_exp, b, infop);
    float pa = gcall(pri_exp, a, infop);
    if (pa < pb) return -1;
    if (pa <= pb) return 0;
    return 1;
}

/* target priority (lower = better): weighted |yaw|, |pitch| (in units of 45 degrees) and distance (per 1000),
 * x0.25 inside the lock angle, x0.6 for the current target; the multi-lock variants also favour targets with
 * fewer missiles on them (x0.75 per free lock) */
static float pri_exp_calc(StWepLock *wlp, void *prev, float kx, float ky, float kd)
{
    const float rad45 = ps2_u2f(0x3fa2f983);   /* 4 / pi */
    float ax = wlp->ang[0];
    if (ax < 0.0f) ax = -ax;
    float ay = wlp->ang[1];
    if (ay < 0.0f) ay = -ay;
    ax = ax * rad45;
    ay = ay * rad45;
    float d = wlp->dist * 0.001f;
    float p = ax * kx + ay * ky;
    p = p + d * kd;
    if (wlp->lock_stat & 2) p = p * 0.25f;
    if (wlp->ofp == prev) p = p * 0.6f;
    return p;
}

static float pri_exp_multi(float p, StWepLock *wlp)
{
    for (int n = 12 - wlp->lock_count; n != 0; n--) p = p * 0.75f;
    return p;
}

static float pri_exp_near_multi(StWepLock *wlp, void *prev) { return pri_exp_multi(pri_exp_calc(wlp, prev, 2.0f, 4.0f, 8.0f), wlp); }
static float pri_exp_center_multi(StWepLock *wlp, void *prev) { return pri_exp_multi(pri_exp_calc(wlp, prev, 2.0f, 4.0f, 1.0f), wlp); }
static float pri_exp_near_single(StWepLock *wlp, void *prev) { return pri_exp_calc(wlp, prev, 1.0f, 2.0f, 8.0f); }
static float pri_exp_center_single(StWepLock *wlp, void *prev) { return pri_exp_calc(wlp, prev, 2.0f, 4.0f, 1.0f); }

/* aim point for a target moving at tvel per frame: acc refinements of "where is it when a bullet at speed arrives" */
void DF_get_deflection(float *loc, float *ang, float *tloc, float *tvel, float *wloc, float speed, int acc)
{
    PS2_FRAME(0x30);
    float p[4] A16, d[4] A16;
    memcpy(p, tloc, 16);
    for (;;) {
        for (int k = 0; k < 3; k++) d[k] = p[k] - wloc[k];
        d[3] = p[3];
        if (acc-- == 0) break;
        float sq = d[0] * d[0] + d[1] * d[1];
        float t = ps2_div(ps2_sqrt(sq + d[2] * d[2]), speed);
        p[0] = tloc[0] + tvel[0] * t;
        p[1] = tloc[1] + tvel[1] * t;
        p[2] = tloc[2] + tvel[2] * t;
    }
    memcpy(loc, p, 16);
    if (ang) vec2ang(ang, d);
}

void Lock_SetDeflectionDb(StWeaponLockManage *plockm, StLockDeflectionDb *pdb) { plockm->pdefldb = pdb; }

/* order 0: clears the 12 main entries and the scan line; 1: the sub sight entry */
void Lock_ClearMng(StWeaponLockManage *lockm, int order)
{
    if (order == 1) {
        lockm->lock[12].ofp = 0;
        lockm->lock[12].sloc[1] = 0x8000;
        lockm->lock[12].sloc[0] = 0x8000;
        lockm->lock[12].lock_stat = 0;
        if (lockm->pdefldb) lockm->pdefldb->num_db_sub = 0;
    } else if (order == 0) {
        for (int i = 0; i < 12; i++) {
            lockm->lock[i].ofp = 0;
            lockm->lock[i].sloc[1] = 0x8000;
            lockm->lock[i].sloc[0] = 0x8000;
            lockm->lock[i].lock_stat = 0;
        }
        lockm->pri_lock = 0;
        lockm->scanl_angx = lockm->vangx;
        lockm->scanl_angy = 0.0f;
        if (lockm->pdefldb) lockm->pdefldb->num_db_main = 0;
    }
}

void Lock_InitMng(StWeaponLockManage *plockm) { memset(plockm, 0, sizeof *plockm); }

/* every lockable model in range (stage characters, target objects, live ACs; minus the caller s_type / s_id and
 * models with cflag & cfmask) into wlp; returns the count */
static int DF_get_lock(StWepLock *wlp, float *locp, float *angp, unsigned short cfmask, float dist, float angx,
                       float angy, int s_type, int s_id)
{
    PS2_FRAME(0xd0);
    StWepLock *w = wlp;
    float c[4] A16;
    c[3] = 1.0f;
    unsigned short mask = cfmask | 0x1000;
    int cnum = StgInfo.cnum;
    StModel *m = Chr;
    for (int i = 0; i < cnum; i++, m++) {
        if ((s_type == 1 && i == s_id) || (m->use & 4) || (m->cflag & mask)) continue;
        DF_GetModelCenter(c, m);
        if (get_wep_lock(w, locp, angp, dist, angx, angy, m, c)) w++;
    }
    for (int i = 0; i < s_iTrgObjMdlNum; i++) {
        m = s_pTrgObjMdl[i];
        if ((s_type == 1 && i == s_id) || (m->use & 4) || (m->cflag & mask)) continue;
        DF_GetModelCenter(c, m);
        if (get_wep_lock(w, locp, angp, dist, angx, angy, m, c)) w++;
    }
    for (int i = 0; i < NowPlayerNum; i++) {
        AcPlayerSt *a = &g_ac[i];
        if ((s_type == 2 && i == s_id) || a->dead) continue;
        m = a->mp;
        if (a->hp <= 0 || a->areaovered || !m || (m->use & 1) || (m->use & 4) || (m->cflag & mask)) continue;
        DF_GetModelCenter(c, m);
        if (get_wep_lock(w, locp, angp, dist, angx, angy, m, c)) w++;
    }
    return (int)(w - wlp);
}

/* one candidate at loc: inside the range box and the horizontal range circle -> entry with distance and the
 * yaw / pitch relative to angp (lock_stat 1, | 2 when inside angx / angy) */
static int get_wep_lock(StWepLock *wlp, float *locp, float *angp, float dist, float angx, float angy, void *mp,
                        float *loc)
{
    PS2_FRAME(0x60);
    float d[4] A16, a[4] A16;
    memcpy(wlp->loc, loc, 16);
    d[0] = wlp->loc[0] - locp[0];
    d[1] = wlp->loc[1] - locp[1];
    d[2] = wlp->loc[2] - locp[2];
    d[3] = 1.0f;
    if (!in_angle(d[0], dist) || !in_angle(d[1], dist) || !in_angle(d[2], dist)) return 0;
    float h = d[0] * d[0] + d[2] * d[2];
    if (!(h <= dist * dist)) return 0;
    wlp->ofp = (StModel *)mp;
    wlp->dist = ps2_sqrt(h + d[1] * d[1]);
    wlp->lock_stat = 1;
    vec2ang(a, d);
    float ax = sub_angf(a[0], angp[0]), ay = 0.0f;
    if (in_angle(ax, angx)) {
        ay = sub_angf(a[1], angp[1]);
        if (in_angle(ay, angy)) wlp->lock_stat |= 2;
    }
    wlp->ang[0] = ax;
    wlp->ang[1] = ay;
    return 1;
}

void Lock_Init(void)
{
    s_pTrgObjMdl[0] = 0;
    s_iTrgObjMdlNum = 0;
    s_pTrgObjMdl[1] = 0;
    CurSightType = -1;
    s_pTrgObjMdl[2] = 0;
    dummyLockTime = 0;
    s_pTrgObjMdl[3] = 0;
    dummyMissLockTime = 0;
}

/* missiles locked in total; a locked entry on a target with cflag 0x800 (jamming) loses its locks with chance
 * 547/32768 per frame */
static int count_lockon(StWeaponLockManage *lockm)
{
    PS2_FRAME(0x40);
    int sum = 0;
    StWepLock *l = lockm->lock;
    for (int n = 12; n--; l++) {
        if (!l->ofp) continue;
        if (ps2_rand() % 0x8000 < 0x223 && l->ofp && l->ofp_type == 0 && (l->ofp->cflag & 0x800)) {
            l->lock_count = 0;
            l->lock_stat &= ~8;
        }
        if (l->lock_stat & 8) sum += l->lock_count;
    }
    return sum;
}

static void copy_sight(StWeaponLockManage *plockm, WepSightSt *s)
{
    CopyMemory8b(&plockm->sightUni, s, 0x30);
    plockm->sightUni.range = plockm->sightUni.org_range;
    plockm->sightUni.enable = 1;
}

/* sightUni = the main / sub sight, or both combined: two lock sights -> averaged ranges and angles scaled by
 * paraeffic, two non-lock sights -> the larger of each */
static void __UnifyTwoSight(StWeaponLockManage *plockm)
{
    PS2_FRAME(0x50);
    WepSightSt *p0 = plockm->psight[0], *p1 = plockm->psight[1];
    if (!p0 || !p1) {
        if (p0) copy_sight(plockm, p0);
        else if (plockm->psight[1]) copy_sight(plockm, plockm->psight[1]);
        else plockm->sightUni.enable = 0;
        return;
    }
    WepSightSt *u = &plockm->sightUni;
    short m0 = p0->lock_max, m1 = p1->lock_max;
    if ((m0 > 0 && m1 > 0) || (m0 == 0 && m1 == 0)) {
        if (m0 > 0 && m1 > 0) {
            float pe = plockm->paraeffic;
            u->range = 0.5f * (pe * (p0->org_range + p1->org_range));
            u->angx = 0.5f * (pe * (p0->angx + p1->angx));
            u->angy = 0.5f * (pe * (p0->angx + p1->angy));   /* quirk: angx of the main sight */
            u->lock_angx = 0.5f * (pe * (p0->lock_angx + p1->lock_angx));
            u->lock_angy = 0.5f * (pe * (p0->lock_angy + p1->lock_angy));
        } else {
            u->range = p0->org_range <= p1->org_range ? p1->org_range : p0->org_range;
            u->angx = p0->angx <= p1->angx ? p1->angx : p0->angx;
            u->angy = p0->angx <= p1->angy ? p1->angy : p0->angx;   /* quirk: angx of the main sight */
            u->lock_angx = p0->lock_angx <= p1->lock_angx ? p1->lock_angx : p0->lock_angx;
            u->lock_angy = p0->lock_angy <= p1->lock_angy ? p1->lock_angy : p0->lock_angy;
        }
        u->lock_time = p1->lock_time < p0->lock_time ? p0->lock_time : p1->lock_time;
        u->lock_max = (short)(p0->lock_max + p1->lock_max);
        u->lock_speed = p1->lock_speed < p0->lock_speed ? p0->lock_speed : p1->lock_speed;
        u->org_lock_max = p1->org_lock_max < p0->org_lock_max ? p0->org_lock_max : p1->org_lock_max;
        u->enable = 1;
    } else if (m0 > 0) {
        copy_sight(plockm, p0);
    } else if (p1->lock_max > 0) {
        copy_sight(plockm, p1);
    }
    u->type = p0->type;
    u->sight = p0->sight | p1->sight;
    u->flag = p0->flag;
}

/* ---------------------------------------------------------------- GIF packet writers (phase 2 g9a) */

/* Sight / lock-box sprite x0..x1 (12.4) with half size {1, 1} (an initialised local: {sx << 4, sy << 3} added around
 * the box) from the static packet gp0, sent on PATH3 by a D2 chain kick (FlushCache, sync, QWC 0, MADR 0, TADR gp0,
 * FlushCache, CHCR 0x145); no trailing sync. Off the 0x7200..0x9400 x 0x7200..0x8e00 window nothing is drawn. */
static void DF_SIGHT_SpriteDraw(int x, int y, int w, int h, int *color)
{
    PS2_FRAME(0x80);
    PS2_STACK(unsigned long long, s_init, 0x78);
    s_init = 0x0000000100000001ull;   /* sd of the {1, 1} initialiser (ld gp-0x7a88) */
    (void)s_init;
    const int s[2] = {1, 1};
    if (x >= 0x9401) return;
    if (y >= 0x8e01) return;
    int x1 = x + (w << 4);
    if (x1 < 0x7200) return;
    int y1 = y + (h << 3);
    if (y1 < 0x7200) return;
    unsigned int *p = MakeDMA(DF_SIGHT_SpriteDraw__gp0, 4, 7);
    p = MakeGIF(p, 1, 1, 0xc6, 0, 3, 0x551);   /* SPRITE ABE AA1; RGBAQ, XYZ2, XYZ2 */
    if (color)
        p = MakePACKET(p, color[0], color[1], color[2], color[3]);
    else
        p = MakePACKET(p, 0x80, 0x80, 0x80, 0x80);
    p = MakePACKET(p, x - (s[0] << 4), y - (s[1] << 3), 0x100000, 0);
    MakePACKET(p, (s[0] << 4) + x1, (s[1] << 3) + y1, 0x100000, 0);
    FlushCache(0);
    sceGsSyncPath(0, 0);
    FlushCache(0);
    ps2_dma_kick_qmt(2, 0x145, 0, 0, PS2PTR(DF_SIGHT_SpriteDraw__gp0));
}

/* Textured sprite (UV, FST, ABE, AA1) as an 11-qword packet in the scratchpad PrimitiveBuffer[PBuffID], kicked by
 * GifPath3KickSPR. The 128-bit copies go through VU0 (vf18 = rgb, vf16 = xyz[0], vf17 = xyz[1] stay set). Vertex 0's
 * RGBAQ qword (q + 0x10) and the upper halves of both UV qwords are never written: the GS gets stale scratchpad
 * content there (original behaviour, kept; SPRITE uses vertex 1's colour). */
static void draw2dsprite(int *rgb, int (*xyz)[4], struct_1f802d8d tex, int *UV)
{
    PS2_FRAME(0x20);
    auto *v = VU0();
    unsigned long long t;
    memcpy(&t, &tex, 8);
    PS2_STACK(unsigned long long, tex_spill, 0x18);
    tex_spill = t;
    unsigned int *p = (unsigned int *)PrimitiveBuffer[PBuffID];
    p[0] = 0x8003;          /* NLOOP 3, EOP */
    p[1] = 0x10004000;      /* PRE, NREG 1 */
    p[2] = 0xe;             /* A+D */
    p[3] = 0;
    unsigned long long *d = (unsigned long long *)(p + 4);
    d[0] = 0x60;
    d[1] = 0x14;            /* TEX1_1 */
    d[2] = tex_spill;
    d[3] = 6;               /* TEX0_1 */
    d[4] = 0;
    d[5] = 8;               /* CLAMP_1 */
    p[16] = 0x8002;         /* NLOOP 2, EOP */
    p[17] = 0x30eb4000;     /* PRE, PRIM 0x1d6, NREG 3 */
    p[18] = 0x413;          /* UV, RGBAQ, XYZF2 */
    p[19] = 0;
    unsigned int *q = p + 20;
    memcpy(v->vf[18].sw, PS2_LQ_SRC(rgb), 16);
    memcpy(v->vf[16].sw, PS2_LQ_SRC(xyz[0]), 16);
    memcpy(v->vf[17].sw, PS2_LQ_SRC(xyz[1]), 16);
    unsigned long long uv01, uv23;
    memcpy(&uv01, UV, 8);
    memcpy(&uv23, UV + 2, 8);
    memcpy(q + 8, v->vf[16].sw, 16);
    memcpy(q + 20, v->vf[17].sw, 16);
    memcpy(q + 16, v->vf[18].sw, 16);
    memcpy(q, &uv01, 8);
    memcpy(q + 12, &uv23, 8);
    int qwc = (int)((char *)(q + 24) - PrimitiveBuffer[PBuffID]) >> 4;
    GifPath3KickSPR((unsigned int *)PrimitiveBuffer[PBuffID], qwc);
    PBuffID = PBuffID == 0;
}

/* ---------------------------------------------------------------- s87 leftovers */

/* The missile seeker marker: a sprite of the sight type's seeker set at the point on the scan line (view angles +
 * scan-line angles) as far away as the primary target, alpha by the target's lock time. */
static void draw_mis_seeker(StWeaponLockManage *lockm, float (*ws)[4])
{
    PS2_FRAME(0xb0);
    PS2_STACK(int[2][4], rect, 0x50);
    PS2_STACK(int[4], col, 0x70);
    PS2_STACK(int[4], sc, 0x80);
    PS2_STACK(float[4], pt, 0x90);
    PS2_STACK(float[4], dir, 0xa0);
    struct_ee27d1c8 *spr = seeker_company[lockm->sightUni.type].spr;
    float ax = add_angf(lockm->vangx, lockm->scanl_angx);
    float ay = add_angf(lockm->vangy, lockm->scanl_angy);
    ang2vec(dir, ay, ax);
    StWepLock *pri = lockm->pri_lock;
    float dx = pri->loc[0] - lockm->gloc[0];
    float dy = pri->loc[1] - lockm->gloc[1];
    float dz = pri->loc[2] - lockm->gloc[2];
    float len = ps2_sqrt((dx * dx + dy * dy) + dz * dz);   /* mul, mul, adda / madd */
    pt[0] = lockm->gloc[0] + len * dir[0];
    pt[1] = lockm->gloc[1] + len * dir[1];
    pt[3] = 1.0f;
    pt[2] = lockm->gloc[2] + len * dir[2];
    memcpy(col, col_tab[2], 16);
    float a = ps2_div((float)pri->lock_artime, 2.0f);
    if (!(a <= 1.0f)) a = 1.0f;
    col[3] = ps2_ftoi((float)col_tab[2][3] * a);
    DF_CALC_ScreenPointEx(sc, ws, pt);
    rect[0][0] = sc[0] + spr->xyz[0][0];
    rect[0][1] = sc[1] + spr->xyz[0][1];
    rect[0][2] = -1;
    rect[0][3] = 0;
    rect[1][0] = sc[0] + spr->xyz[1][0];
    rect[1][1] = sc[1] + spr->xyz[1][1];
    rect[1][2] = -1;
    rect[1][3] = 0;
    draw2dsprite(col, rect, tex0_of(spr->tex0), spr->uv);
}

/* the nearest target (pri_exp_near_single) inside the cone (range, angx, angy) around loc / ang, NULL if none
 * (missile salvo spell, sfx/spell.c __spell_MainProg10_missile_fire) */
StModel *DF_get_nearest(float *loc, float *ang, float range, float angx, float angy, unsigned short ulock_mask,
                        int s_type, int s_id)
{
    PS2_FRAME(0x40);
    int n = DF_get_lock(s_bufLock, loc, ang, ulock_mask, range, angx, angy, s_type, s_id);
    if (n <= 0) return 0;
    StWepLock *best = s_bufLock, *c = s_bufLock;
    for (int k = n - 1; k != 0; k--) {
        c++;
        if (cmp_std_lock(best, c, 0, GFN(pri_exp_near_single)) > 0) best = c;
    }
    return best->ofp;
}

/* Blade lock (acblade_ActFlyBlade / ActGndBlade): the nearest target in the weapon's sight cone becomes lock[0]
 * (only when it is in lock range, lock_stat bit 1); lock[0] is cleared first either way. */
void Lock_GetLockBld(StWeaponLockManage *lockm, float *loc, float *ang, WepSightSt *wep)
{
    PS2_FRAME(0x60);
    if (lockm->flag & 4) return;
    int n = DF_get_lock(s_bufLock, loc, ang, (unsigned short)(lockm->ulock_mask | 0x2000), wep->range, wep->angx,
                        wep->angy, lockm->type, lockm->id);
    Lock_ClearMng(lockm, 0);
    StWepLock *l0 = &lockm->lock[0];
    lockm->pri_lock = l0;
    lockm->sightUni.enable = 0;
    l0->ofp = 0;
    if (n <= 0) return;
    void *infop = lockm->pri_lock;
    StWepLock *best = s_bufLock, *c = s_bufLock;
    for (int k = n - 1; k != 0; k--) {
        c++;
        if (cmp_std_lock(best, c, infop, GFN(pri_exp_near_single)) > 0) best = c;
    }
    if (best->lock_stat & 2) {
        l0->ofp = best->ofp;
        l0->lock_stat = best->lock_stat;
        memcpy(l0->loc, best->loc, 16);
        l0->ang[0] = 0.0f;
        l0->ang[1] = 0.0f;
        l0->ang[2] = 0.0f;
        l0->ang[3] = 1.0f;
    }
}
