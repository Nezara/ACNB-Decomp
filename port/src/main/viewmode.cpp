/* src/main/viewmode.cpp - per-player cockpit (first person) vs third-person camera switching in battle (whole file).
 * ViewMode_UseCockpit is the predicate (the player's view_mode option, off in replays and after area-over),
 * ViewMode_Check / CheckAll apply it to the views' cameras, ViewMode_HideAcObject hides the AC's own model parts
 * (and its booster effects) in one view so the cockpit camera sees out of it. */
#include "acnb/port.h"

/* original quirk: the per-view hide masks are stored as whole bytes, so hiding in one view clears the other views'
 * bits (split screen); the AC model's last object is never touched by the 14.. loop. */
void ViewMode_HideAcObject(int player, int view, int hide)
{
    PS2_FRAME(0x50);
    PS2_STACK(int, cnt, 0x4c);
    PS2_STACK(_magic2_st *, map, 0x48);
    unsigned char mask = hide ? (unsigned char)(1 << view) : 0;
    AcPlayerSt *acp = &g_ac[player];
    StModel *mp = acp->mp;
    mp->op[4].o_hide = mask;
    mp->op[3].o_hide = mask;
    for (int i = 14; i < mp->nobj - 1; i++) mp->op[i].o_hide = mask;
    StModel *parts[3] = {&MyBST[player], &MyEOB[player], &MySLG[player]};
    for (int p = 0; p < 3; p++) {
        mask = hide ? (unsigned char)(1 << view) : 0;
        for (int k = 0; k < parts[p]->nobj; k++) parts[p]->op[k].o_hide = mask;
    }
    cnt = 0;
    while (AcCtrl_EnumHideBooster(acp, &cnt, &map)) DF_MAGIC2_BoostSetHide(map, view, hide);
}

void ViewMode_CheckAll(int ang_reset)
{
    PS2_FRAME(0x30);
    for (int i = 0; i < ViewNum; i++) ViewMode_Check(i, ang_reset);
}

void ViewMode_Check(int view, int ang_reset)
{
    PS2_FRAME(0x40);
    int player = ViewCamID[view];
    if (player < 0 || player >= 4) return;
    int cockpit = 0;
    int v;
    if (MyI.replaymode != 0 || g_ac[player].areaovered != 0) v = 0;
    else v = Sortie_GetAcOption(player)->view_mode;
    if (v) cockpit = 1;
    if (cockpit) {
        DFcam_SetViewMode(player, 1);
        return;
    }
    if (ang_reset) {
        g_ac[player].view.ang[0] = 0.0f;
        g_ac[player].view.angspd[0] = 0.0f;
    }
    DFcam_SetViewMode(player, 0);
    if (ang_reset) DFcam_SetNoDelay(player);
}

/* returns the option byte itself (not a bool) */
int ViewMode_UseCockpit(int player)
{
    PS2_FRAME(0x10);
    if (MyI.replaymode != 0 || g_ac[player].areaovered != 0) return 0;
    return Sortie_GetAcOption(player)->view_mode;
}
