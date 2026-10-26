/* lib/fs/fslibsdads.cpp - front end of the FSSDADS IOP module (module 5: ADS streaming of the battle BGM, \ADS\ADSnnnn)
 * (whole file). Each call is one fslibsd RPC, fno 0x5CCC0000. All but fssdadsSetupWithLsn are `j` tail calls in the
 * original, so fssdadsVolume / fssdadsPlay send the caller's t-registers and stack words as the unnamed payload words:
 * their hand bw__ wrappers forward them for MIPS callers (OLSORTIE __scrsnd_ADSFadeOut -> fssdadsVolume), native callers
 * pass 0 (fslibsd.cpp). fsadsGetPcm sends a pointer truncated to an int (dead: ads.c's s_pFilter is never set). */
#include "acnb/port.h"

int fsadsGetPcm(short *pPcm)
{
    return fssdCallRpcInt(5, 1, 0x3100000, (int)PS2PTR(pPcm));
}

int fssdadsSync(void)
{
    return fssdCallRpcVoid(5, 1, 0x3030000);
}

int fssdadsClose(int iCh)
{
    return fssdCallRpcInt(5, 1, 0x3040000, iCh);
}

int fssdadsStop(int iCh)
{
    return fssdCallRpcInt(5, 1, 0x3070000, iCh);
}

int fssdadsVolume(int iCh, unsigned short usLeftVol, unsigned short usRightVol)
{
    return fssdCallRpc(5, 1, 0x3090000, iCh, usLeftVol & 0xffff, usRightVol & 0xffff, 0, 0, 0, 0, 0);
}

int fssdadsPlay(int iCh, int iMode)
{
    return fssdCallRpc(5, 1, 0x3060000, iCh, iMode, 0, 0, 0, 0, 0, 0);
}

#ifndef ACNB_SOURCEPORT
/* the tail calls from MIPS: t1.. / t2.., t3 and the caller's sp + 0 / 8 / 0x10 go on as w2.. / w3..w7 */
static void bw__fssdadsVolume(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    int r = fssdCallRpc(5, 1, 0x3090000, c->gpr[A0].sw[0], (int)(c->gpr[A1].uw[0] & 0xffff), (int)(c->gpr[A2].uw[0] & 0xffff),
                        c->gpr[T2].sw[0], c->gpr[T3].sw[0], (int)rd32(sp), (int)rd32(sp + 8), (int)rd32(sp + 0x10));
    c->gpr[V0].sd[0] = r;
}

static void bw__fssdadsPlay(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    int r = fssdCallRpc(5, 1, 0x3060000, c->gpr[A0].sw[0], c->gpr[A1].sw[0], c->gpr[T1].sw[0], c->gpr[T2].sw[0],
                        c->gpr[T3].sw[0], (int)rd32(sp), (int)rd32(sp + 8), (int)rd32(sp + 0x10));
    c->gpr[V0].sd[0] = r;
}
#endif

int fssdadsSetupWithLsn(int iCh, int iLsn, int iSize)
{
    PS2_FRAME(0x10);
    return fssdCallRpc(5, 1, 0x3020000, iCh, iLsn, iSize, 0, 0, 0, 0, 0) != 0;
}
