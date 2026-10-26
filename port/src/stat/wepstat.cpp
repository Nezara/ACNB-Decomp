/* src/stat/wepstat.cpp - weapon status list of the overlook gauge screen (whole file): sub-weapon / left-arm /
 * selected weapon blocks (slot number, "4" = left arm, ammo "cnt:max"), the remaining weapons in selection order
 * and the extension's ammo. DF_DrawWeapon is the full-screen layout, DF_DrawWeapon_Split the split-screen one.
 * Ammo is drawn in STA2_RGBA[12] (STA2_RGBA[1] when empty); counts >= 9999 show the "infinite" sprite. */
#include "acnb/sdk.h"
#include "acnb/pf.h"

static inline void set_tex0(int i)
{
    struct_f386fb29 tex0;
    memcpy(&tex0, &STA2_TEXTURE[i], 8);
    GS_SetTEX0_1(tex0);
}

static inline int *ammo_col(int cnt) { return cnt > 0 ? STA2_RGBA[12] : STA2_RGBA[1]; }

/* slot number shown for a weapon (statid 4 / 5 -> 1, 6 -> 0) */
static inline int wep_no(int statid)
{
    if (statid == 4 || statid == 5) return 1;
    if (statid == 6) return 0;
    return statid;
}

void DF_DrawWeapon(StGaugeInfo *gip, int ofs_ulx, int ofs_uly)
{
    PS2_FRAME(0x100);
    char buf[16], s_l[2] = "4", s_colon[2] = ":";
    AcPlayerSt *acp = &g_ac[gip->acid];
    unsigned char larm = acp->param.Larm_type;
    int nwep = acp->param.n_weapon;
    int next = acp->next_weapon;
    _weapon_st *wp = &acp->param.weapons[next];
    AcCtrl_GetPartsExt(acp);
    int analog = AcCtrl_IsAnalog(acp);
    int spare = AcCtrl_IsSpareWepFuncCore(acp);
    GS_NewList();
    GS_SetALPHA_1(0, 1, 0, 1, 0);
    GS_SetTEST_1(1, 1, 0, 0, 0, 0, 1, 1);
    int dy = 0;
    set_tex0(5);
    if (analog && acp->sepins_weappn != -1) {
        _weapon_st *sp = &acp->param.weapons[(int)acp->sepins_weappn];
        int max = sp->blt_initcnt, cnt = sp->blt_cnt;
        GS_NewList();
        GS_SetRGBA(ammo_col(cnt));
        pf_sprintf_d(buf, "%d", sp->statid);
        DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x160, ofs_uly, buf, 7, 9);
        pf_sprintf_d(buf, "%d", 3);
        DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x16e, ofs_uly + 9, buf, 5, 9);
        DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x1c4, ofs_uly + 10, s_colon, 4, 9);
        ACSTAT_SetDrawColor_AlphaDiv(1, ammo_col(cnt), 2);
        pf_sprintf_d(buf, "%04d", cnt);
        DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x1a0, ofs_uly + 10, buf, 4, 9);
        pf_sprintf_d(buf, "%04d", max);
        DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x1cd, ofs_uly + 10, buf, 4, 9);
        ACSTAT_SetDrawColor_AlphaDiv(0, 0, 1);
        GS_FlushList();
        dy += 0x1a;
    }
    if (larm != 4) {
        GS_NewList();
        if (larm == 3) {
            _ac_weaponctrl *wc = AcLgun_GetWeaponCtrl(AcCtrl_GetPartsLgun(acp));
            if (wc && wc->wp) {
                int cnt = wc->wp->blt_cnt, max = wc->wp->blt_initcnt;
                GS_SetRGBA(ammo_col(cnt));
                DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x160, ofs_uly + dy, s_l, 7, 9);
                DF_ACSTAT_DrawSprite(ofs_ulx + 0x16e, ofs_uly + dy + 9, 50.0f, 86.0f, 0x32, 10);
                int y = ofs_uly + dy + 10;
                DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x1c4, y, s_colon, 4, 9);
                ACSTAT_SetDrawColor_AlphaDiv(1, ammo_col(cnt), 2);
                pf_sprintf_d(buf, "%04d", cnt);
                DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x1a0, y, buf, 4, 9);
                pf_sprintf_d(buf, "%04d", max);
                DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x1cd, y, buf, 4, 9);
                ACSTAT_SetDrawColor_AlphaDiv(0, 0, 1);
            }
        } else {
            GS_SetRGBA(STA2_RGBA[12]);
            DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x160, ofs_uly + dy, s_l, 7, 9);
            DF_ACSTAT_DrawSprite(ofs_ulx + 0x16e, ofs_uly + dy + 9, 50.0f, 86.0f, 0x32, 10);
            DF_ACSTAT_DrawSprite(ofs_ulx + 0x1b8, ofs_uly + dy + 10, 160.0f, 68.0f, 0x3a, 8);
        }
        if (spare == 1 && AcCtrl_IsExistSpareWep(acp, 0xd))
            DF_ACSTAT_DrawSprite(ofs_ulx + 0x162, ofs_uly + dy + 9, 132.0f, 96.0f, 0xc, 10);
        GS_FlushList();
        dy += 0x1a;
    }
    /* selected weapon: big slot number, then the same block as above */
    if (wp->statid != 5) {
        int cnt = wp->blt_cnt, max = wp->blt_initcnt;
        GS_NewList();
        GS_SetRGBA(ammo_col(wp->blt_cnt));
        set_tex0(2);
        int no = wep_no(wp->statid);
        pf_sprintf_d(buf, "%d", no);
        DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x1a0, ofs_uly + dy + 0x14, buf, 8, 9);
        set_tex0(5);
        DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x160, ofs_uly + dy, buf, 7, 9);
        pf_sprintf_d(buf, "%d", no);
        int y9 = ofs_uly + dy + 9;
        DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x16e, y9, buf, 5, 9);
        if (wp->blt_cnt < 9999) {
            int y = ofs_uly + dy + 10;
            DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x1c4, y, s_colon, 4, 9);
            ACSTAT_SetDrawColor_AlphaDiv(1, ammo_col(cnt), 2);
            pf_sprintf_d(buf, "%04d", cnt);
            DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x1a0, y, buf, 4, 9);
            pf_sprintf_d(buf, "%04d", max);
            DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x1cd, y, buf, 4, 9);
            ACSTAT_SetDrawColor_AlphaDiv(0, 0, 1);
        } else {
            DF_ACSTAT_DrawSprite(ofs_ulx + 0x1b8, ofs_uly + dy + 10, 160.0f, 68.0f, 0x3a, 8);
        }
        if (wp->statid == 0 && spare == 1 && AcCtrl_IsExistSpareWep(acp, 0xc) == 1)
            DF_ACSTAT_DrawSprite(ofs_ulx + 0x162, y9, 132.0f, 96.0f, 0xc, 10);
        GS_FlushList();
        dy += 0x36;
    }
    /* the other weapons in selection order (analog sub-weapons are shown above) */
    for (int i = 0; i < nwep - 1; i++) {
        wp = &acp->param.weapons[++next % 4];
        if (wp->statid == 5 || (analog && wp->statid == 3)) continue;
        int cnt = wp->blt_cnt;
        GS_NewList();
        GS_SetRGBA(wp->blt_cnt > 0 ? ginfop->mycol : STA2_RGBA[1]);
        pf_sprintf_d(buf, "%d", wep_no(wp->statid));
        int y = ofs_uly + dy;
        DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x19a, y, buf, 5, 9);
        if (cnt < 9999) {
            ACSTAT_SetDrawColor_AlphaDiv(1, wp->blt_cnt > 0 ? ginfop->mycol : STA2_RGBA[1], 2);
            pf_sprintf_d(buf, "%04d", cnt);
            DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x1cd, ofs_uly + dy + 1, buf, 4, 9);
            ACSTAT_SetDrawColor_AlphaDiv(0, 0, 1);
        } else {
            DF_ACSTAT_DrawSprite(ofs_ulx + 0x1ce, ofs_uly + dy + 1, 218.0f, 68.0f, 0x24, 8);
        }
        if (wp->statid == 0 && spare == 1 && AcCtrl_IsExistSpareWep(acp, 0xc) == 1)
            DF_ACSTAT_DrawSprite(ofs_ulx + 0x18e, y, 132.0f, 96.0f, 0xc, 10);
        GS_FlushList();
        dy += 0xc;
    }
    /* extension ammo (-2 = none, < 0 or >= 9999 = no count) */
    int ex = gip->exblt;
    if (ex != -2) {
        GS_NewList();
        set_tex0(5);
        GS_SetRGBA(gip->excol);
        if (ex >= 0 && ex < 9999) {
            DF_ACSTAT_DrawSprite(ofs_ulx + 0x18c, ofs_uly + dy + 4, 82.0f, 76.0f, 0x4a, 10);
            ACSTAT_SetDrawColor_AlphaDiv(1, gip->excol, 2);
            pf_sprintf_d(buf, "%03d", ex);
            DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x1d6, ofs_uly + dy + 5, buf, 4, 9);
            ACSTAT_SetDrawColor_AlphaDiv(0, 0, 1);
        } else {
            DF_ACSTAT_DrawSprite(ofs_ulx + 0x1a8, ofs_uly + dy + 4, 82.0f, 76.0f, 0x4a, 10);
        }
        GS_FlushList();
    }
    GS_GetRest();
}

/* split screen: the slot numbers in a left column, the ammo blocks right of it; status texture by player side */
void DF_DrawWeapon_Split(StGaugeInfo *gip, int ofs_ulx, int ofs_uly)
{
    PS2_FRAME(0x100);
    char buf[16], s_l[2] = "4", s_colon[2] = ":";
    AcPlayerSt *acp = &g_ac[gip->acid];
    unsigned char larm = acp->param.Larm_type;
    int next = acp->next_weapon;
    int nwep = acp->param.n_weapon;
    _weapon_st *wp = &acp->param.weapons[next];
    int analog = AcCtrl_IsAnalog(acp);
    int spare = AcCtrl_IsSpareWepFuncCore(acp);
    int y = ofs_uly;
    GS_NewList();
    GS_SetALPHA_1(0, 1, 0, 1, 0);
    int id = gip->acid;
    if (id == 3 || id == 1) set_tex0(9);
    else if (id == 2 || id == 0) set_tex0(8);
    /* left column */
    if (analog && acp->sepins_weappn != -1) {
        _weapon_st *sp = &acp->param.weapons[(int)acp->sepins_weappn];
        GS_SetRGBA(ammo_col(sp->blt_cnt));
        pf_sprintf_d(buf, "%d", sp->statid);
        DF_ACSTAT_DrawNumberOffset(ofs_ulx, ofs_uly, buf, 7, 9);
        y += 0x1a;
    }
    if (larm != 4) {
        if (larm == 3) {
            _ac_weaponctrl *wc = AcLgun_GetWeaponCtrl(AcCtrl_GetPartsLgun(acp));
            if (wc && wc->wp) {
                GS_SetRGBA(ammo_col(wc->wp->blt_cnt));
                DF_ACSTAT_DrawNumber(ofs_ulx, y, s_l, 7);
            }
        } else {
            GS_SetRGBA(STA2_RGBA[12]);
            DF_ACSTAT_DrawNumber(ofs_ulx, y, s_l, 7);
        }
        y += 0x1a;
    }
    if (wp->statid != 5) {
        GS_SetRGBA(ammo_col(wp->blt_cnt));
        pf_sprintf_d(buf, "%d", wep_no(wp->statid));
        DF_ACSTAT_DrawNumber(ofs_ulx, y, buf, 7);
    }
    GS_FlushList();
    /* ammo blocks */
    GS_NewList();
    set_tex0(gip->maker ? 9 : 8);
    y = ofs_uly;
    if (analog && acp->sepins_weappn != -1) {
        _weapon_st *sp = &acp->param.weapons[(int)acp->sepins_weappn];
        int cnt = sp->blt_cnt, max = sp->blt_initcnt;
        GS_SetRGBA(ammo_col(cnt));
        pf_sprintf_d(buf, "%d", 3);
        DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x25, y + 9, buf, 6, 9);
        DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x64, y + 10, s_colon, 4, 9);
        ACSTAT_SetDrawColor_AlphaDiv(1, ammo_col(cnt), 2);
        pf_sprintf_d(buf, "%04d", cnt);
        DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x40, y + 10, buf, 4, 9);
        pf_sprintf_d(buf, "%04d", max);
        DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x6d, y + 10, buf, 4, 9);
        ACSTAT_SetDrawColor_AlphaDiv(0, 0, 1);
        y += 0x1a;
    }
    GS_FlushList();
    GS_NewList();
    if (larm != 4) {
        if (larm == 3) {
            _ac_weaponctrl *wc = AcLgun_GetWeaponCtrl(AcCtrl_GetPartsLgun(acp));
            if (wc && wc->wp) {
                int cnt = wc->wp->blt_cnt, max = wc->wp->blt_initcnt;
                GS_SetRGBA(ammo_col(cnt));
                DF_ACSTAT_DrawNumber(ofs_ulx + 0x25, y + 9, s_l, 6);
                DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x64, y + 10, s_colon, 4, 9);
                ACSTAT_SetDrawColor_AlphaDiv(1, ammo_col(cnt), 2);
                pf_sprintf_d(buf, "%04d", cnt);
                DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x40, y + 10, buf, 4, 9);
                pf_sprintf_d(buf, "%04d", max);
                DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x6d, y + 10, buf, 4, 9);
                ACSTAT_SetDrawColor_AlphaDiv(0, 0, 1);
            }
        } else {
            GS_SetRGBA(STA2_RGBA[12]);
            DF_ACSTAT_DrawNumber(ofs_ulx + 0x25, y + 9, s_l, 6);
            DF_ACSTAT_DrawSprite(ofs_ulx + 0x58, y + 10, 160.0f, 68.0f, 0x3a, 8);
        }
        if (spare == 1 && AcCtrl_IsExistSpareWep(acp, 0xd))
            DF_ACSTAT_DrawSprite(ofs_ulx + 0x19, y + 9, 132.0f, 96.0f, 0xc, 10);
        y += 0x1a;
    }
    GS_FlushList();
    GS_NewList();
    if (wp->statid != 5) {
        GS_SetRGBA(ammo_col(wp->blt_cnt));
        pf_sprintf_d(buf, "%d", wep_no(wp->statid));
        DF_ACSTAT_DrawNumber(ofs_ulx + 0x25, y + 9, buf, 6);
        if (wp->blt_cnt < 9999) {
            DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x64, y + 10, s_colon, 4, 9);
            ACSTAT_SetDrawColor_AlphaDiv(1, ammo_col(wp->blt_cnt), 2);
            pf_sprintf_d(buf, "%04d", wp->blt_cnt);
            DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x40, y + 10, buf, 4, 9);
            pf_sprintf_d(buf, "%04d", wp->blt_initcnt);
            DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x6d, y + 10, buf, 4, 9);
            ACSTAT_SetDrawColor_AlphaDiv(0, 0, 1);
        } else {
            DF_ACSTAT_DrawSprite(ofs_ulx + 0x58, y + 10, 160.0f, 68.0f, 0x3a, 8);
        }
        if (wp->statid == 0 && spare == 1 && AcCtrl_IsExistSpareWep(acp, 0xc))
            DF_ACSTAT_DrawSprite(ofs_ulx + 0x19, y + 9, 132.0f, 96.0f, 0xc, 10);
    }
    GS_FlushList();
    y += 0x15;
    GS_NewList();
    for (int i = 0; i < nwep - 1; i++) {
        wp = &acp->param.weapons[++next % 4];
        if (wp->statid == 5 || (analog && wp->statid == 3)) continue;
        GS_SetRGBA(wp->blt_cnt > 0 ? gip->mycol : STA2_RGBA[1]);
        pf_sprintf_d(buf, "%d", wep_no(wp->statid));
        DF_ACSTAT_DrawNumber(ofs_ulx + 0x51, y, buf, 6);
        int cnt = wp->blt_cnt;
        if (cnt < 9999) {
            ACSTAT_SetDrawColor_AlphaDiv(1, cnt > 0 ? gip->mycol : STA2_RGBA[1], 2);
            pf_sprintf_d(buf, "%04d", wp->blt_cnt);
            DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x6d, y + 1, buf, 4, 9);
            ACSTAT_SetDrawColor_AlphaDiv(0, 0, 1);
        } else {
            DF_ACSTAT_DrawSprite(ofs_ulx + 0x6d, y + 1, 218.0f, 68.0f, 0x24, 8);
        }
        if (wp->statid == 0 && spare == 1 && AcCtrl_IsExistSpareWep(acp, 0xc))
            DF_ACSTAT_DrawSprite(ofs_ulx + 0x45, y, 132.0f, 96.0f, 0xc, 10);
        y += 0xc;
    }
    GS_FlushList();
    GS_NewList();
    if (gip->exblt != -2) {
        GS_SetRGBA(gip->excol);
        if (gip->exblt >= 0 && gip->exblt < 9999) {
            DF_ACSTAT_DrawSprite(ofs_ulx + 0x2c, y + 2, 82.0f, 76.0f, 0x4a, 10);
            ACSTAT_SetDrawColor_AlphaDiv(1, gip->excol, 2);
            pf_sprintf_d(buf, "%03d", gip->exblt);
            DF_ACSTAT_DrawNumberOffset(ofs_ulx + 0x76, y + 3, buf, 4, 9);
            ACSTAT_SetDrawColor_AlphaDiv(0, 0, 1);
        } else {
            DF_ACSTAT_DrawSprite(ofs_ulx + 0x48, y + 2, 82.0f, 76.0f, 0x4a, 10);
        }
    }
    GS_GetRest();
    GS_FlushList();
}
