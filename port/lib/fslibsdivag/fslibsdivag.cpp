/* lib/fslibsdivag/fslibsdivag.cpp - EE front end of FSSDIVAG (IOP module 10: multi-stream BGM / voice tracks read from
 * BMSS.DAT) (whole file). Each wrapper is one fslibsd RPC, fno 0xa0000000 | cmd << 16; fssdivagInit also registers
 * _IopToEeRpcDispatch as the module-10 handler of the IOP -> EE server (per-track play start / end notifications).
 * UpdateTrackStat pulls the 0x48-byte state pack, GetTrackStat reads one track byte from it.
 * Quirks kept: SetTrackReverb is the one non-blocking call (wait 0 -> SIF mode 1); the play start / end callbacks are
 * written by no code (always NULL), so a notification only clears s_iRpcEeToIopRet; a non-notification command returns the
 * old value. The original (-O0 style) spills its arguments into the frame that fssdCallRpc then reads as the unnamed
 * payload words; natively those are 0 (fslibsd.cpp). */
#include "acnb/port.h"
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/local/lib/fslibsdivag/fslibsdivag.h"

#ifdef ACNB_SOURCEPORT
char fssdivag_msg_init[57] = "fssdivagInit : ERROR!!! IOP module can't initialized...\n";
char fssdivag_msg_quit[61] = "fssdivagQuit : ERROR!!! IOP module didn't be initialized...\n";
#endif

/* s_psrcFunc[10]: called by IopToEeRpcDispatch with (cmd, pData, iSize (unused), &iRetSize) */
static void *_IopToEeRpcDispatch(unsigned int uiCmd, void *pData, int iSize, int *piRetSize)
{
    PS2_FRAME(0x50);
    (void)iSize;
    if ((uiCmd & 0x0fff0000u) == 0x01000000u) {
        int *d = (int *)pData;
        if (s_pfPlayStartCB && d[0]) gcall(s_pfPlayStartCB, d[0]);
        if (s_pfPlayEndCB && d[1]) gcall(s_pfPlayEndCB, d[1]);
        s_iRpcEeToIopRet = 0;
    }
    *piRetSize = 4;
    return &s_iRpcEeToIopRet;
}

int fssdivagInit(void)
{
    PS2_FRAME(0x10);
    if (!fssdCallRpcVoid(0xa, 1, 0x10000)) {
        ps2_printf(fssdivag_msg_init);
        return 0;
    }
    fssdRegister(0xa, GFN(_IopToEeRpcDispatch));
    fssdRequireIopToEeRpc();
    return 1;
}

void fssdivagQuit(void)
{
    PS2_FRAME(0x10);
    if (!fssdCallRpcVoid(0xa, 1, 0x20000)) ps2_printf(fssdivag_msg_quit);
}

void fssdivagForceReset(void)
{
    PS2_FRAME(0x10);
    fssdCallRpcVoid(0xa, 1, 0x150000);
}

int fssdivagReserveSetup(int iTrackId, int iIndex)
{
    PS2_FRAME(0x40);
    int iop = fsmfstrm_GetIopIndex(iIndex);
    return fssdCallRpc(0xa, 1, 0x40000, iTrackId, iop, 0, 0, 0, 0, 0, 0);
}

int fssdivagSetPacket(int iSize)
{
    PS2_FRAME(0x20);
    return fssdCallRpc(0xa, 1, 0x50000, iSize, 0, 0, 0, 0, 0, 0, 0);
}

int fssdivagSetSpuMemory(int iSpuAddr, int iSize)
{
    PS2_FRAME(0x30);
    return fssdCallRpc(0xa, 1, 0x60000, iSpuAddr, iSize, 0, 0, 0, 0, 0, 0);
}

int fssdivagPlay(int iTrackID)
{
    PS2_FRAME(0x20);
    return fssdCallRpc(0xa, 1, 0x70000, iTrackID, 0, 0, 0, 0, 0, 0, 0);
}

int fssdivagStop(int iTrackID)
{
    PS2_FRAME(0x20);
    return fssdCallRpc(0xa, 1, 0x80000, iTrackID, 0, 0, 0, 0, 0, 0, 0);
}

int fssdivagSetChVolume(int iTrackID, int iCh, unsigned short usVolL, unsigned short usVolR)
{
    PS2_FRAME(0x50);
    return fssdCallRpc(0xa, 1, 0xa0000, iTrackID, iCh, usVolL & 0xffff, usVolR & 0xffff, 0, 0, 0, 0);
}

void fssdivagUpdateTrackStat(void)
{
    PS2_FRAME(0x20);
    fssdCallRpcStruct(0xa, 1, 0, 0x48, 0xe0000, nullptr);
    unsigned int *p = fssdGetCalledRpcResult();
    ps2_memcpy(&s_StatPack, p, 0x48);
}

int fssdivagGetTrackStat(int iTrackID)
{
    if (iTrackID < 0 || iTrackID >= 8) return -1;
    return s_StatPack.TrackStat[iTrackID];
}

void fssdivagSetCoreVoice(int iVoice)
{
    PS2_FRAME(0x20);
    fssdCallRpc(0xa, 1, 0xf0000, iVoice, 0, 0, 0, 0, 0, 0, 0);
}

void fssdivagSetTrackReverb(int iTrackID, int bOn)
{
    PS2_FRAME(0x30);
    fssdCallRpc(0xa, 0, 0x140000, iTrackID, bOn, 0, 0, 0, 0, 0, 0);   /* wait 0: SIF_RPCM_NOWAIT */
}

void fssdivagSetDmaChannel(int iCh)
{
    PS2_FRAME(0x20);
    fssdCallRpc(0xa, 1, 0x100000, iCh, 0, 0, 0, 0, 0, 0, 0);
}
