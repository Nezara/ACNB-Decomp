/* src/sound/emsvag.cpp - thin wrappers over the IOP "EmsVag" module (message speech played as VAG from an EE
 * buffer, lib/fs fslibsdemsvag, interpreted until phase 3): header parse, end-of-stream / stop, play with the SE
 * volume applied, the per-frame proc, reset and init. */

int EmsVag_GetVagInfo(void *pHd, void *pBd, int iProgress, void *pviOut)
{
    return fssdemsvagGetVagInfo(pHd, pBd, iProgress, (struct_83eb889b *)pviOut);
}

int EmsVag_VagCheckEos(void)
{
    return fssdemsvagIsEndOfStream();
}

int EmsVag_VagStop(void)
{
    return fssdemsvagStop();
}

int EmsVag_VagPlay(unsigned char *pucBuff, int iSize, int iSample, unsigned short usLeftVol, unsigned short usRightVol)
{
    int sevol = SoundManageData.sevol;
    return fssdemsvagStart(pucBuff, iSize, iSample, (unsigned short)(usLeftVol * sevol / 127),
                           (unsigned short)(usRightVol * sevol / 127));
}

void EmsVag_Proc(void)
{
    fssdemsvagProc();
}

void EmsVag_Reset(void)
{
    fssdemsvagReset();
}

int EmsVag_Init(void)
{
    return fssdemsvagInit();
}
