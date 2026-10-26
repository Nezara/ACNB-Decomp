/* src/ac/parts_gaze.cpp - "gaze" core special part (core_type 3, whole file, OLSORTIE overlay).
 * While gaze lock is on the AC runs a private copy of its lock manager with a widened sight (range and angles x1.5)
 * through Lock_GetGazeLock; gaze lock stays on only while that holds a locked target. The pad (trigger conf 13)
 * toggles it; charging cancels it. No Versus COM roster carries this core (s59 survey of rosters 1-18). */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/local/src/ac/parts_gaze.h"

#define GAZE_PI 3.14159274f    /* 0x40490fdb */
#define GAZE_2PI 6.28318548f   /* 0x40c90fdb */

StWeaponLockManage *AcGaze_GetLockMng(AcPartsGazeSt *gzcp)
{
    if (!gzcp) return NULL;
    return &gzcp->prvp->lockmng;
}

void AcGaze_Control(AcPartsGazeSt *gzcp)
{
    PS2_FRAME(0x40);
    if (!gzcp) return;
    AcPartsGazePrivateSt *prv = gzcp->prvp;
    AcPlayerSt *acp = prv->acp;
    if (AcCtrl_IsGazeLock(acp)) {
        acp->effectstat |= 0x20;
        int locked = __LockReinforce(gzcp, prv, acp);
        if (!AcCtrl_IsCharging(acp) && !AcUidev_IsPadTrgConf(acp->frdp->uidevp, 0xd) && locked) return;
        AcCtrl_SetGazeLock(acp, 0);
    } else {
        if (AcCtrl_IsCharging(acp)) return;
        if (AcUidev_IsPadTrgConf(acp->frdp->uidevp, 0xd)) AcCtrl_SetGazeLock(acp, 1);
    }
}

void AcGaze_Init(AcPartsGazeSt *gzcp, AcPlayerSt *acp)
{
    PS2_FRAME(0x40);
    if (!gzcp) return;
    AcPartsGazePrivateSt *prv = gzcp->prvp;
    ZeroMemory8b(gzcp, 4);
    ZeroMemory8b(prv, 0x6e0);
    gzcp->prvp = prv;
    gzcp->prvp->acp = acp;
}

static void gaze_wrap(float *x)
{
    if (!(*x <= GAZE_PI)) do { *x = *x - GAZE_2PI; } while (!(*x <= GAZE_PI));
    if (*x < -GAZE_PI) do { *x = *x + GAZE_2PI; } while (*x < -GAZE_PI);
}

/* original quirks: ang[2..3] are never written (stale stack words reach Lock_GetGazeLock); on a lock the final
 * CopyMemory8b copies 0x630 bytes (sizeof StWeaponLockManage) from the sight copy (prv+0x680) to prv+0x60: it
 * overwrites lockmng from sightUni on and part of wep, and reads 0x5d0 bytes past the end of the private struct. */
static int __LockReinforce(AcPartsGazeSt *gzcp, AcPartsGazePrivateSt *prvp, AcPlayerSt *acp)
{
    PS2_FRAME(0x50);
    PS2_STACK(float[4], ang, 0x30);
    PS2_STACK(float[4], loc, 0x40);
    (void)gzcp;
    if (acp->lockmng.sightUni.enable == 0) {
        prvp->lockmng.sightUni.enable = 0;
        return 0;
    }
    CopyMemory8b(&prvp->lockmng, &acp->lockmng, 0x630);
    CopyMemory8b(&prvp->wep.sight, &acp->lockmng.sightUni, 0x30);
    memcpy(loc, acp->loc, 16);
    loc[1] = loc[1] - 0.5f * acp->mp->tall;
    ang[0] = acp->view.ang[0];
    ang[1] = Add_Angle(acp->ang[1], GAZE_PI);
    WepSightSt *sp = &prvp->wep.sight;
    sp->range = sp->range * 1.5f;
    sp->angx = sp->angx * 1.5f;
    sp->angy = sp->angy * 1.5f;
    gaze_wrap(&sp->angx);
    gaze_wrap(&sp->angy);
    sp->lock_time = 1;
    Lock_GetGazeLock(&prvp->lockmng, loc, ang, sp);
    StWepLock *t = prvp->lockmng.pri_lock;
    if (!t) return 0;
    if (!t->ofp) return 0;
    if (!(t->lock_stat & 2)) return 0;
    t->lock_stat |= 0x100;
    CopyMemory8b((char *)prvp + 0x60, sp, 0x630);
    return 1;
}
