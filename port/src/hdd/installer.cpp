/* src/hdd/installer.cpp - what is left of the HDD installer front end: a step counter pair, the four dialog callbacks
 * the menu registers (__PlayFsLogoSystemMcCheck: installer_SetDialogFunc, then installer_Start) and installer_Init,
 * which sets the loader up on s_hddSl and reads the install info at boot (__InitPs2).
 * The procedure that would run the steps and call the dialogs is not in this build: nothing reads the step statics or
 * the callbacks (write-only state). installer_Start's only effect is hdd_GetStatus (no HDD here: next step 7).
 * Hybrid: installer_Init's 0x10 frame is load-bearing - LoadInstallInfo's buffer sits on the PS2 stack below it and
 * its address shows in the disc trace (line 20, buf 01ffe4f0). */
#include "acnb/port.h"

#ifdef ACNB_SOURCEPORT
struct StHddDescriptor s_hddSl;
int s_iInstlCurStep;
int s_iInstlNextStep;
int (*s_pfuncDlgOpen)(void *, int);
int (*s_pfuncDlgProc)(void *, int);
int (*s_pfuncDlgClose)(void *, int);
void *s_pMenuDlgArg;
#endif

/* the callbacks are guest code addresses in the hybrid, stored back as they came (never called) */
void installer_SetDialogFunc(int (*pfuncOpen)(void *, int), int (*pfuncProc)(void *, int),
                             int (*pfuncClose)(void *, int), void *pArg)
{
    s_pfuncDlgOpen = pfuncOpen;
    s_pfuncDlgProc = pfuncProc;
    s_pfuncDlgClose = pfuncClose;
    s_pMenuDlgArg = pArg;
}

int installer_Start(void)
{
    PS2_FRAME(0x10);
    s_iInstlCurStep = 0;
    if (hdd_GetStatus() == 0) s_iInstlNextStep = 1;
    else s_iInstlNextStep = 7;
    return 1;
}

int installer_Init(void)
{
    PS2_FRAME(0x10);
    loader_Init(&s_hddSl);
    loader_SetReadyHdd(0);
    s_iInstlCurStep = 0;
    s_iInstlNextStep = 0;
    loader_LoadInstallInfo();   /* result ignored */
    return 1;
}
