/* src/hdd/hdd.cpp - the title's PS2 HDD (hdd0: / pfs0:) access layer. An async file read runs as a step machine:
 * hdd_ReadFileAsync starts task 3 (TaskReadFile) and hdd_Sync, polled by loader.c every frame, runs the current
 * task's step; each step posts one libaio command (AioGetstat / AioOpen / AioRead / AioClose) and the next one waits
 * for it with AioCmdSync. TaskWriteFile (task 2) and TaskCreatePartition (task 1, installer partitions with
 * scrambled passwords: hdd_MakeScrambleString + RotateShift) are in the task table but nothing starts them.
 * hdd_CheckFile stats a pfs0: file directly (sceGetstat). The work blocks s_ucWorkBuf / s_ucSectorBuf are handed to
 * the aio functions, so they stay the DWARF globals (guest memory in the hybrid).
 * Nothing here runs in this game: s_ucIsInitHdd has no writer, so hdd_ReadFileAsync always returns 0 and the step
 * machine stays idle (all ten functions unreached, asm-reviewed only).
 * The reached leaves: hdd_CopyMemory / hdd_ZeroMemory (loader helpers; the originals are `j memcpy` / `j memset`
 * tails, so v0 = pDst although they are void: hand bw__ wrappers keep that) and hdd_GetStatus (sceDevctl hdd0:
 * 0x4807; -19 = no HDD in this port, installer_Start's "no HDD" branch). */
#include <string.h>
#include "acnb/port.h"
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
char hdd_dev_hdd0[6] = "hdd0:";
char hdd_dev_pfs0[6] = "pfs0:";
char hdd_prtt_fmt[29] = "%sP%c.%s.%s.%s,%s,%s,%dM,PFS";
unsigned char s_ucWorkBuf[384];
unsigned char s_ucSectorBuf[512];
unsigned char s_ucIsInitHdd;
unsigned char s_ucIsCreatePrtt;
int s_iTask;
int s_iHddCurStep;
int s_iHddNextStep;
int s_iResult;
int s_iPropResult;
int (*s_pfuncHddTask[4])(StHddDescriptor *, void *) = {0, TaskCreatePartition, TaskWriteFile, TaskReadFile};
#endif

/* scrambles / unscrambles the 8 bytes of pcSrc with the key string pcScramble: byte i & 3 = 0 xor key, 1 rotate,
 * 2 xor (0x7f - key), 3 rotate the other way; the key index wraps at strlen(pcScramble) */
void hdd_MakeScrambleString(char *pcSrc, char *pcScramble, int iIsDecode)
{
    PS2_FRAME(0x90);
    /* the only callers pass NULL: the original reads the key from PS2 address 0 */
    const char *scr = pcScramble ? pcScramble : (const char *)PS2_NULL_MEM();
    int n = (int)ps2_strlen(scr);
    if (n <= 0) return;
    for (int i = 0, j = 0; i < 8; i++) {
        unsigned char key = (unsigned char)scr[j];
        unsigned char *p = (unsigned char *)pcSrc + i;
        switch (i & 3) {
        case 0: *p = *p ^ key; break;
        case 1: *p = RotateShift(*p, key, iIsDecode); break;
        case 2: *p = *p ^ (unsigned char)(0x7f - key); break;
        case 3: *p = RotateShift(*p, key, iIsDecode == 0); break;
        }
        if (++j >= n) j = 0;
    }
}

void hdd_CopyMemory(void *pDst, void *pSrc, int iSize) { ps2_memcpy(pDst, pSrc, (unsigned int)iSize); }

void hdd_ZeroMemory(void *pDst, int iSize) { ps2_memset(pDst, 0, (unsigned int)iSize); }

#ifndef ACNB_SOURCEPORT
/* oracle: the tail calls leave memcpy's / memset's v0 = a0 (all 64 bits, read before the call) */
static void bw__hdd_CopyMemory(EECore *c)
{
    u64 d = c->gpr[A0].ud[0];
    hdd_CopyMemory(bridge_ptr(c->gpr[A0].uw[0]), bridge_ptr(c->gpr[A1].uw[0]), c->gpr[A2].sw[0]);
    c->gpr[V0].ud[0] = d;
}

static void bw__hdd_ZeroMemory(EECore *c)
{
    u64 d = c->gpr[A0].ud[0];
    hdd_ZeroMemory(bridge_ptr(c->gpr[A0].uw[0]), c->gpr[A1].sw[0]);
    c->gpr[V0].ud[0] = d;
}
#endif

int hdd_ReadFileAsync(StHddDescriptor *phdd, char *pcPath, char *pcBuf, int iBufLen)
{
    PS2_FRAME(0x20);
    if (!s_ucIsInitHdd) return 0;
    struct_aab8e2c9 hfs;   /* sp+0x10 */
    hfs.pcPath = pcPath;
    hfs.pcBuf = pcBuf;
    hfs.iBufLen = iBufLen;
    s_iTask = 3;
    s_iHddCurStep = 0;
    s_iHddNextStep = 0;
    s_iResult = 0;
    s_iPropResult = 0;
    TaskReadFile(phdd, &hfs);
    return 1;
}

int hdd_GetResult(int *piProperty)
{
    if (piProperty) *piProperty = s_iPropResult;
    return s_iResult;
}

/* runs one step of the current task; 1 while a task exists (also on the call it finishes), 0 without one */
int hdd_Sync(StHddDescriptor *phdd)
{
    PS2_FRAME(0x10);
    int (*task)(StHddDescriptor *, void *) = s_pfuncHddTask[s_iTask];
    if (task == 0) return 0;
    if (s_iHddCurStep != s_iHddNextStep) s_iHddCurStep = s_iHddNextStep;
    if (gcall(task, phdd, (void *)0) == 0) {
        s_iHddCurStep = 0;
        s_iHddNextStep = 0;
        s_iTask = 0;
    }
    return 1;
}

int hdd_IsBusy(void) { return s_iHddCurStep != 0 || s_iHddNextStep != 0; }

/* size of the regular file pfs0:<pcPath>, 0 if missing or not a regular file */
unsigned int hdd_CheckFile(char *pcPath)
{
    PS2_FRAME(0x150);
    PS2_STACK(char[256], cBuf, 0x10);
    PS2_STACK(sce_stat, statFileSys, 0x110);
    memset(cBuf, 0, 256);   /* the 0x100-byte initialiser image (0x25c7a0): "pfs0:" + zeros */
    strcpy(cBuf, "pfs0:");
    strcat(cBuf, pcPath);
    memset(&statFileSys, 0, 0x40);
    int r = sceGetstat(cBuf, &statFileSys);
    int iFileMode = (int)((statFileSys.st_mode & 0xf000) >> 12);
    if (r == -2 || !(iFileMode & 2)) return 0;
    return statFileSys.st_size;
}

/* HDD status (sceDevctl hdd0: 0x4807); 0 = ready */
int hdd_GetStatus(void) { return sceDevctl(hdd_dev_hdd0, 0x4807, 0, 0, 0, 0); }

/* task 3: pfs0:<path> -> pcBuf (at most iBufLen bytes). Steps 1-2 (hdd0: devctl) are skipped by step 0 */
static int TaskReadFile(StHddDescriptor *phdd, void *pParam)
{
    PS2_FRAME(0x30);
    (void)phdd;
    struct_df70361e *pWork = (struct_df70361e *)s_ucWorkBuf;
    struct_aab8e2c9 *hfs = (struct_aab8e2c9 *)pParam;
    switch ((unsigned int)s_iHddCurStep) {
    case 0:
        s_iResult = 0;
        s_iPropResult = 0;
        memset(pWork, 0, 0x154);
        strcpy(pWork->cPath, hdd_dev_pfs0);
        strcat(pWork->cPath, hfs->pcPath);
        pWork->pcBuf = hfs->pcBuf;
        pWork->iBufLen = hfs->iBufLen;
        s_iHddNextStep = 3;
        return 1;
    case 1:
        AioDevctl(hdd_dev_hdd0, 0x4807, 0, 0, 0, 0);
        s_iHddNextStep = 2;
        /* fall through */
    case 2:
        if (AioCmdSync(1, &pWork->iAioCmd, &pWork->iAioRes)) {
            if (pWork->iAioRes < 0) break;
            s_iHddNextStep = 3;
        }
        return 1;
    case 3:
        AioGetstat(pWork->cPath, &pWork->statFileSys);
        s_iHddNextStep = 4;
        /* fall through */
    case 4: {
        if (!AioCmdSync(1, &pWork->iAioCmd, &pWork->iAioRes)) return 1;
        if (pWork->iAioRes < 0) break;
        unsigned int size = pWork->statFileSys.st_size;
        if (size == 0) {
            s_iResult = 0;
            s_iHddNextStep = 11;
            s_iPropResult = 0;
            return 0;
        }
        if (size < (unsigned int)pWork->iBufLen) pWork->iBufLen = (int)size;
        s_iHddNextStep = 5;
        return 1;
    }
    case 5:
        AioOpen(pWork->cPath, 1, 0);
        s_iHddNextStep = 6;
        /* fall through */
    case 6:
        if (!AioCmdSync(1, &pWork->iAioCmd, &pWork->iAioRes)) return 1;
        if (pWork->iAioRes < 0) break;
        pWork->iFd = pWork->iAioRes;
        s_iHddNextStep = 7;
        return 1;
    case 7:
        AioRead(pWork->iFd, pWork->pcBuf, pWork->iBufLen);
        s_iHddNextStep = 8;
        /* fall through */
    case 8:
        if (!AioCmdSync(1, &pWork->iAioCmd, &pWork->iAioRes)) return 1;
        s_iPropResult = pWork->iAioRes;
        s_iResult = pWork->iAioRes < 0 ? 0 : 1;
        s_iHddNextStep = 9;
        return 1;
    case 9:
        AioClose(pWork->iFd);
        s_iHddNextStep = 10;
        /* fall through */
    case 10:
        if (!AioCmdSync(1, &pWork->iAioCmd, &pWork->iAioRes)) {
            FlushCache(0);
            return 1;
        }
        FlushCache(0);
        if (pWork->iAioRes < 0) break;
        s_iHddNextStep = 11;
        FlushCache(0);
        return 1;
    case 11:
        return 0;
    default:
        return 1;
    }
    /* failed aio command (steps 2, 4, 6, 10) */
    s_iPropResult = pWork->iAioRes;
    s_iResult = 0;
    s_iHddNextStep = 11;
    return 0;
}

/* task 2: pcBuf (iBufLen bytes) -> pfs0:<path> (created / truncated); no starter */
static int TaskWriteFile(StHddDescriptor *phdd, void *pParam)
{
    PS2_FRAME(0x30);
    (void)phdd;
    struct_2fe479ef *pWork = (struct_2fe479ef *)s_ucWorkBuf;
    struct_aab8e2c9 *hfs = (struct_aab8e2c9 *)pParam;
    switch ((unsigned int)s_iHddCurStep) {
    case 0:
        s_iResult = 0;
        s_iPropResult = 0;
        memset(pWork, 0, 0x114);
        strcpy(pWork->cPath, hdd_dev_pfs0);
        strcat(pWork->cPath, hfs->pcPath);
        pWork->pcBuf = hfs->pcBuf;
        pWork->iBufLen = hfs->iBufLen;
        s_iHddNextStep = 1;
        return 1;
    case 1:
        AioOpen(pWork->cPath, 0x602, 0x1ff);   /* O_WRONLY | O_CREAT | O_TRUNC, 0777 */
        s_iHddNextStep = 2;
        /* fall through */
    case 2:
        if (!AioCmdSync(1, &pWork->iAioCmd, &pWork->iAioRes)) return 1;
        if (pWork->iAioRes < 0) break;
        pWork->iFd = pWork->iAioRes;
        s_iHddNextStep = 3;
        return 1;
    case 3:
        AioWrite(pWork->iFd, pWork->pcBuf, pWork->iBufLen);
        s_iHddNextStep = 4;
        /* fall through */
    case 4:
        if (!AioCmdSync(1, &pWork->iAioCmd, &pWork->iAioRes)) return 1;
        if (pWork->iAioRes < 0) {
            s_iPropResult = pWork->iAioRes;
            s_iResult = 0;
        } else {
            s_iPropResult = 0;
            s_iResult = 1;
        }
        s_iHddNextStep = 5;
        return 1;
    case 5:
        AioClose(pWork->iFd);
        s_iHddNextStep = 6;
        /* fall through */
    case 6:
        if (!AioCmdSync(1, &pWork->iAioCmd, &pWork->iAioRes)) {
            FlushCache(0);
            return 1;
        }
        FlushCache(0);
        if (pWork->iAioRes < 0) break;
        s_iHddNextStep = 7;
        FlushCache(0);
        return 1;
    case 7:
        return 0;
    default:
        return 1;
    }
    /* failed aio command (steps 2, 6) */
    s_iPropResult = pWork->iAioRes;
    s_iResult = 0;
    s_iHddNextStep = 7;
    return 0;
}

/* task 1 (installer, no starter): per partition of pParam (StHddMakePartition) creates hdd0:<name> with the scrambled
 * passwords, writes a 0x5a-filled sector and formats it as pfs; on success copies the title partition table to phdd */
static int TaskCreatePartition(StHddDescriptor *phdd, void *pParam)
{
    PS2_FRAME(0xa0);
    struct_edfbf441 *pWork = (struct_edfbf441 *)s_ucWorkBuf;
    switch ((unsigned int)s_iHddCurStep) {
    case 0:
        s_iResult = 0;
        s_iPropResult = 0;
        memset(pWork, 0, 0x5c);
        pWork->iCurPrtt = 0;
        pWork->pmkprtt = (StHddMakePartition *)pParam;
        pWork->php = &pWork->pmkprtt->tprtt.prtt[pWork->iCurPrtt];
        s_iHddNextStep = 1;
        return 1;
    case 1: {
        StHddPartition *php = pWork->php;
        StHddMakePartition *mk = pWork->pmkprtt;
        int zone = (int)php->uiSize;
        unsigned char isParent = php->ucIsParent;
        PS2_STACK(char[9], cPswdFull, 0x90);
        PS2_STACK(char[9], cPswdRead, 0x80);
        memset(cPswdFull, 0, 9);
        memset(cPswdRead, 0, 9);
        memcpy(cPswdFull, php->cPswdFull, 8);
        memcpy(cPswdRead, php->cPswdRead, 8);
        hdd_MakeScrambleString(cPswdFull, 0, 1);
        hdd_MakeScrambleString(cPswdRead, 0, 1);
        ps2_sprintf_scsssssd(pWork->cBuf, hdd_prtt_fmt, hdd_dev_hdd0, isParent ? 'P' : 'C', mk->tprtt.cSlps,
                             php->cMagic, php->cName, cPswdFull, cPswdRead, zone);
        AioOpen(pWork->cBuf, 0x203, 0);
        s_iHddNextStep = 2;
    }
        /* fall through */
    case 2:
        if (!AioCmdSync(1, &pWork->iAioCmd, &pWork->iAioRes)) return 1;
        if (pWork->iAioRes < 0) break;
        pWork->iFd = pWork->iAioRes;
        s_iHddNextStep = 3;
        return 1;
    case 3:
        memset(s_ucSectorBuf, 0x5a, 0x200);
        AioWrite(pWork->iFd, s_ucSectorBuf, 0x200);
        s_iHddNextStep = 4;
        /* fall through */
    case 4:
        if (!AioCmdSync(1, &pWork->iAioCmd, &pWork->iAioRes)) return 1;
        if (pWork->iAioRes < 0) break;
        s_iHddNextStep = 5;
        return 1;
    case 5:
        AioClose(pWork->iFd);
        s_iHddNextStep = 6;
        /* fall through */
    case 6:
        if (!AioCmdSync(1, &pWork->iAioCmd, &pWork->iAioRes)) return 1;
        if (pWork->iAioRes < 0) break;
        s_iHddNextStep = 7;
        return 1;
    case 7:
        pWork->iZonesz = 0x2000;
        AioFormat(hdd_dev_pfs0, pWork->cBuf, &pWork->iZonesz, 4);
        s_iHddNextStep = 8;
        /* fall through */
    case 8:
        if (!AioCmdSync(1, &pWork->iAioCmd, &pWork->iAioRes)) return 1;
        if (pWork->iAioRes < 0) break;
        s_iHddNextStep = 9;
        return 1;
    case 9: {
        pWork->iCurPrtt++;
        StHddMakePartition *mk = pWork->pmkprtt;
        if (pWork->iCurPrtt < (int)mk->tprtt.ucPrttNum) {
            pWork->php = &mk->tprtt.prtt[pWork->iCurPrtt];
            if (pWork->php->cName[0] != 0) {
                s_iHddNextStep = 1;
                return 1;
            }
        }
        s_iPropResult = 0;
        s_iResult = 1;
        s_iHddNextStep = 10;
        return 1;
    }
    case 10:
        if (s_iResult) {
            memcpy(phdd, pWork->pmkprtt, 0xdc);
            s_ucIsCreatePrtt = 1;
        }
        return 0;
    default:
        return 1;
    }
    /* failed aio command (steps 2, 4, 6, 8) */
    s_iPropResult = pWork->iAioRes;
    s_iResult = 0;
    s_iHddNextStep = 10;
    return 0;
}

/* 8-bit rotate of ucSrc by the key's low 3 bits (left if iIsLeft); the high-nibble term (key & 0xf0) >> 8 is always 0 */
static unsigned char RotateShift(unsigned char ucSrc, unsigned char ucKey, int iIsLeft)
{
    int k = ucKey;
    int iRotate1 = ((k & 0xf) ^ ((k & 0xf0) >> 8)) & 7;
    int iRotate2 = 8 - iRotate1;
    int src = ucSrc;
    int lo, hi;
    if (iIsLeft) {
        lo = ((src & ((0xff >> iRotate1) & 0xff)) << iRotate1) & 0xff;
        hi = ((src & ((0xff << iRotate2) & 0xff)) >> iRotate2) & 0xff;
    } else {
        lo = ((src & ((0xff >> iRotate2) & 0xff)) << iRotate2) & 0xff;
        hi = ((src & ((0xff << iRotate1) & 0xff)) >> iRotate1) & 0xff;
    }
    return (unsigned char)(lo | hi);
}
