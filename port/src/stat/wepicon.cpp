/* src/stat/wepicon.cpp - weapon icons and names of the status screen (whole file). WEPICON_CreateIcon collects
 * each weapon's icon (0x99c bytes) and name string (0x20 bytes) from the parts files once; the id2icon tables map
 * weapon data / name ids to 1-based slots (0 = not loaded). WEPICON_SetWepName uploads a player's icon / name
 * images into the status texture (one view: icon + name at fixed VRAM pages; split screen: name only, written
 * into the side's status texture). wepid < 0 = all four weapons + the left arm, >= 4 = left arm only.
 * Quirk: load_left_icon leaves the destination psm unset (stale s1) for a player outside 0..3 (cannot happen). */
#include "acnb/sdk.h"

static void load_left_icon(int player)
{
    PS2_FRAME(0x60);
    if (GetPlayerViewID(player) < 0 || ViewNum >= 3) return;
    AcPlayerSt *acp = &g_ac[player];
    int nameid;
    if (acp->param.swordp) nameid = acp->param.swordp->name_id;
    else if (acp->param.armsp && acp->param.armsp->type == 2) nameid = acp->param.armsp->name_id;
    else nameid = -1;
    if (nameid < 0) return;
    int icon = s_mng_p->wepnameid2icon[nameid] - 1;
    if (icon < 0) return;
    if (ViewNum == 1) {
        StatFont_SetupLoadImage(0x3940, 4);
        StatFont_DrawString(0, 0x8a, 0x92, 8, s_mng_p->name_img + icon * 0x20, 0x212);
        return;
    }
    int src_psm = acp->param.corep->basic.maker ? 0x2c : 0x24;
    int dst_psm = 0;
    if (player == 3 || player == 1) dst_psm = 0x2c;
    else if (player == 2 || player == 0) dst_psm = 0x24;
    StatFont_SetupLoadImage(0xe00, 4);
    StatFont_DrawStringEx(0, 0x8a, 0x92, 8, s_mng_p->name_img + icon * 0x20, 0x212, dst_psm, 0xe00, src_psm);
}

static void _set_wep_name(int player, int wepid)
{
    PS2_FRAME(0x60);
    int view = GetPlayerViewID(player);
    if (view < 0) return;
    int n = wepid % 4;
    _weapon_st *wp = &g_ac[player].param.weapons[n];
    if (wp->statid == 5) return;
    int name = s_mng_p->wepnameid2icon[wp->nameid] - 1;
    int icon = s_mng_p->wepdataid2icon[wp->name2id] - 1;
    if (ViewNum == 1) {
        if (icon >= 0) DF_LoadImageEx(s_mng_p->icon_img + icon * 0x99c, 0x1200, 4, 0x1b, 0xae, n * 0x1e, 0x52, 0x1e);
        if (name >= 0) {
            StatFont_SetupLoadImage(0x3940, 4);
            StatFont_DrawString(0, n * 8 + 0x6a, 0x92, 8, s_mng_p->name_img + name * 0x20, 0x212);
        }
    } else {
        int dst_psm = view ? 0x2c : 0x24;
        int src_psm = g_ac[player].param.corep->basic.maker ? 0x2c : 0x24;
        if (name >= 0) {
            StatFont_SetupLoadImage(0xe00, 4);
            StatFont_DrawStringEx(0, n * 8 + 0x6a, 0x92, 8, s_mng_p->name_img + name * 0x20, 0x212, dst_psm, 0xe00,
                                  src_psm);
        }
    }
}

void WEPICON_SetWepName(int player, int wepid)
{
    PS2_FRAME(0x30);
    if (player < 0 || player >= 4) return;
    if (wepid < 0) {
        for (int i = 0; i < 4; i++) _set_wep_name(player, i);
        load_left_icon(player);
    } else if (wepid < 4) {
        _set_wep_name(player, wepid);
    } else {
        load_left_icon(player);
    }
}

void WEPICON_CreateIcon(WepIconFileHdr *hdr)
{
    PS2_FRAME(0x30);
    if (hdr->ofs_wpic >= 0) {
        int id = hdr->wpicid;
        if (s_mng_p->wepdataid2icon[id] == 0) {
            CopyMemory8b(s_mng_p->icon_p, (char *)hdr + hdr->ofs_wpic, 0x99c);
            s_mng_p->icon_p += 0x99c;
            s_mng_p->wepdataid2icon[id] = (char)++s_mng_p->wep_icon_id2;
        }
    }
    int id = hdr->wnamid;
    if (s_mng_p->wepnameid2icon[id] == 0) {
        memcpy(s_mng_p->name_p, (char *)hdr + hdr->ofs_strname, 0x20);
        s_mng_p->name_p[0x1f] = 0;
        s_mng_p->name_p += 0x20;
        s_mng_p->wepnameid2icon[id] = (char)++s_mng_p->wep_icon_id;
    }
}

void WEPICON_Destroy(void) { s_mng_p = 0; }

void WEPICON_Create(void)
{
    PS2_FRAME(0x10);
    s_mng_p = (WepIconMngSt *)Memm_Alloc(1, 0x20, 0x10);
    memset(s_mng_p, 0, 0x20);
    s_mng_p->wepnameid2icon = (char *)Memm_Alloc(1, 0x8c, 0x10);
    s_mng_p->wepdataid2icon = (char *)Memm_Alloc(1, 300, 0x10);
    s_mng_p->icon_img = (char *)Memm_Alloc(1, 0xe6a0, 0x10);
    s_mng_p->name_img = (char *)Memm_Alloc(1, 0x300, 0x10);
    s_mng_p->wep_icon_id = 0;
    ZeroMemory32b(s_mng_p->wepnameid2icon, 0x23);
    s_mng_p->wep_icon_id2 = 0;
    ZeroMemory32b(s_mng_p->wepdataid2icon, 0x4b);
    s_mng_p->icon_p = s_mng_p->icon_img;
    s_mng_p->name_p = s_mng_p->name_img;
}
