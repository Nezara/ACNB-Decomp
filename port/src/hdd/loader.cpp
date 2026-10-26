/* src/hdd/loader.cpp - the parts loader of the menus. A menu model (".mmd" parts file) comes from the CD (packed
 * AC.BIN id from DF_MPART_ID2LsnID), from the HDD (`/parts/<prefix>/<nnn>/<file>`) or from another console over
 * i.LINK. loader_GetMmdLocation picks the source (CD id >= 0, -1 / -2 = HDD path built into pcPath sync / async,
 * -3 = i.LINK), loader_ReadMmdAsync arms the read and sets s_iLoaderReadMode (1 CD, 2 HDD, 3 i.LINK); the menus poll
 * loader_AsyncProc(1) + loader_IsAsyncBusy + loader_GetAsyncResult. loader_LoadInstallInfo (boot, installer_Init)
 * reads the install table (`/parts/install.dat` on the HDD, else AC.BIN id 0x3c) with the blocking LoadingLoop
 * (flip / black screen / poll) and then marks every part as "on the CD" (ucInfo 1); only the pack name table survives.
 * In this port only the CD arms run: s_iIsReadHdd is 0 for the whole game (loader_Init, loader_SetReadyHdd(0)), so the
 * HDD / i.LINK arms, GetPartsVersionString and GetVersionString are asm-reviewed only.
 * i.LINK relay (asm-only, unreached here): a console that reads a part from its HDD for a peer (s_iwAsync.iStep 1) sends
 * an 8-byte header (size, byte sum) in s_cIlinkBuf and then the file (SendByiLinkS / SendByiLinkL); the receiver
 * (mode 3, RecvFileiLink) checks the sum. Every transfer starts with SyncLink (a 128-byte pattern exchange). */
#include <string.h>
#include "acnb/port.h"
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/local/src/hdd/loader.h"

#ifdef ACNB_SOURCEPORT
char loader_fmt_02d[5] = "%02d";
int loader_ConvKindMenuToLoader__iMenuToLoaderKind[15] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 11, 12, 13};
char *g_pcPartsPrefix[14] = {(char *)"hd", (char *)"cr", (char *)"am", (char *)"lg", (char *)"bo",
                             (char *)"fc", (char *)"ge", (char *)"rd", (char *)"in", (char *)"ex",
                             (char *)"bk", (char *)"gn", (char *)"sw", (char *)"op"};
struct StIlinkWork s_iwAsync;
struct StAllPartsInstallInfo s_aiinfoHdd;
struct StHddDescriptor *s_phddSl;
int s_iIsReadHdd;
int s_iLoaderReadMode;
int s_iIsBusy;
int s_iLastError;
int s_iOutMenuHddError;
int s_iDisposeExtParts;
char s_cIlinkBuf[8];
#endif

/* s_aiinfoHdd.iinfoAll[iKind][iId].ucInfo without a bounds check on iKind (the original indexes the flat table) */
static inline unsigned char *loader_info_at(int iKind, int iId)
{
    return (unsigned char *)&s_aiinfoHdd + iKind * 0x100 + iId;
}

static char loader_path_mptable[] = "/parts/mptable.dat";
static char loader_path_install[] = "/parts/install.dat";

/* arms the read of one mmd file: -3 i.LINK, -2 / -1 HDD (pcPath), anything else = CD id (the switch only tests the
 * three negatives, so ids < -3 go to File_Read too) */
void loader_ReadMmdAsync(char *pcBuf, char *pcPath, int iCdvdId)
{
    PS2_FRAME(0x30);
    switch (iCdvdId) {
    case -3:
        ZeroMemory8b(&s_iwAsync, 0x14);
        s_iwAsync.pcBuf = pcBuf;
        s_iLoaderReadMode = 3;
        s_iwAsync.iStep = 1;
        s_iIsBusy = 1;
        s_iLastError = 0;
        break;
    case -2:
        ZeroMemory8b(&s_iwAsync, 0x14);
        s_iwAsync.pcBuf = pcBuf;
        s_iwAsync.iStep = 1;
        hdd_ReadFileAsync(s_phddSl, pcPath, pcBuf, 0x64000);
        s_iLastError = 0;
        s_iLoaderReadMode = 2;
        s_iIsBusy = 1;
        break;
    case -1:
        hdd_ReadFileAsync(s_phddSl, pcPath, pcBuf, 0x64000);   /* before the clear (order differs from -2) */
        ZeroMemory8b(&s_iwAsync, 0x14);
        s_iLastError = 0;
        s_iLoaderReadMode = 2;
        s_iIsBusy = 1;
        break;
    default:
        File_Read(iCdvdId, -1, pcBuf, 0);
        s_iLastError = 0;
        s_iLoaderReadMode = 1;
        s_iIsBusy = 1;
        break;
    }
}

int loader_ConvKindMenuToLoader(int iMenuKind) { return loader_ConvKindMenuToLoader__iMenuToLoaderKind[iMenuKind]; }

/* where the part file comes from. Register roles differ from the DWARF names: iId = kind index 0..13, iMode = file
 * index 0..255, iIsLocal = 1 / 3 local, 2 remote; iAllowHdd (t2, protofix) lets a remote part load from the HDD.
 * Returns the CD id (DF_MPART_ID2LsnID), -1 / -2 (HDD path in pcPath, local / remote), -3 (i.LINK) or 0. */
int loader_GetMmdLocation(char *pcPath, int iMenuTruId, int iKind, int iId, int iMode, int iIsLocal, int iAllowHdd)
{
    PS2_FRAME(0x90);
    (void)iKind;
    if (iId < 0 || iId >= 0xe) return 0;
    if (iMode < 0 || iMode >= 0x100) return 0;
    int loc = 0;   /* 0 CD, 1 HDD, 2 i.LINK */
    if (iIsLocal == 2) {
        unsigned char info = *loader_info_at(iId, iMode);
        if (info == 1) loc = 0;
        else if (info == 0) loc = 2;
        else if (s_iIsReadHdd == 0) loc = 2;
        else if (iAllowHdd == 0) loc = 2;
        else loc = 1;
    } else if (iIsLocal == 3 || iIsLocal == 1) {
        if (s_iIsReadHdd == 0) loc = 0;
        else loc = *loader_info_at(iId, iMode) == 1 ? 0 : 1;
    }
    if (loc == 2) return -3;
    if (loc == 0) return DF_MPART_ID2LsnID(iMenuTruId);
    /* HDD: "/parts/%s/%03d/" + the part's file format with attribute 0x26 */
    PS2_STACK(char[0x40], cFormat, 0x50);
    memset(cFormat, 0, sizeof(cFormat));
    strcpy(cFormat, "/parts/%s/%03d/");
    struct StPartsFileAtrbInfo *fi = aci_GetPartsFileAtrbInfo(iId);
    for (; fi->pcHddFormat != 0; fi++) {
        if (fi->usAtrb != 0x26) continue;
        strcat(cFormat, fi->pcHddFormat);
        char *prefix = (iId >= 0 && iId < 0xe) ? g_pcPartsPrefix[iId] : 0;
        ps2_sprintf_sdd(pcPath, cFormat, prefix, iMode, iMode);
        break;
    }
    return iIsLocal == 2 ? -2 : -1;
}

/* arms the read of the menu parts table (CD id 0xbd2, or /parts/mptable.dat on the HDD) */
int loader_ReadMenuPartsTableAsync(char *pcBuf)
{
    PS2_FRAME(0x10);
    if (s_iIsReadHdd != 0) {
        hdd_ReadFileAsync(s_phddSl, loader_path_mptable, pcBuf, 0x20000);
        ZeroMemory8b(&s_iwAsync, 0x14);
        s_iLastError = 0;
        s_iLoaderReadMode = 2;
        s_iIsBusy = 1;
        return 0;
    }
    File_Read(0xbd2, -1, pcBuf, 0);
    s_iLoaderReadMode = 1;
    s_iIsBusy = 1;
    s_iLastError = 0;
    Cdvd_GetSizeByte(0xbd2);   /* result dropped (original) */
    s_iLastError = 0;
    s_iLoaderReadMode = 1;
    s_iIsBusy = 1;
    return 0;
}

int loader_IsValidParts(int iKind, int iId)
{
    (void)iKind;
    return (iId >= 0 && iId < 0x100) ? 1 : 0;
}

int loader_GetPartsVersionString(char *cStr, int iKind, int iId)
{
    unsigned char ver = (iId >= 0 && iId < 0x100) ? *loader_info_at(iKind, iId) : 0;
    return loader_GetVersionString(cStr, ver);   /* tail jump */
}

/* "STANDARD" (1), "NO ENTRY" (0), else "<pack short name> ADDnn" (nn = version - 2); returns 1 for a pack */
int loader_GetVersionString(char *cStr, unsigned char ucVersion)
{
    PS2_FRAME(0x80);
    PS2_STACK(char[4], cWork2, 0x7c);
    ZeroMemory8b(cStr, 0x10);
    if (ucVersion == 1) {
        ps2_strcpy(cStr, "STANDARD");
        return 0;
    }
    if (ucVersion == 0) {
        ps2_strcpy(cStr, "NO ENTRY");
        return 0;
    }
    /* snTable[ucVersion] (255 entries: index 255 reads past the table, as the original) */
    const signed char *sn = (const signed char *)&s_aiinfoHdd + 0xe00 + ucVersion * 4;
    signed char c0 = sn[0], c1 = sn[1], c2 = sn[2], c3 = sn[3];
    ps2_sprintf_d(cWork2, loader_fmt_02d, ucVersion - 2);
    int n = 0;
    if (c0 != 0) cStr[n++] = c0;
    if (c1 != 0) cStr[n++] = c1;
    if (c2 != 0) cStr[n++] = c2;
    if (c3 != 0) cStr[n++] = c3;
    if (n > 0) cStr[n++] = ' ';
    cStr[n] = 'A';
    cStr[n + 1] = 'D';
    cStr[n + 2] = 'D';
    cStr[n + 3] = cWork2[0];
    cStr[n + 4] = cWork2[1];
    cStr[n + 5] = 0;
    return 1;
}

unsigned char loader_GetVersion(int iKind, int iId)
{
    if (iId < 0 || iId >= 0x100) return 0;
    return *loader_info_at(iKind, iId);
}

/* blocking read of the install table at boot. Quirk: after an HDD failure (read error) s1 stays -1, so the CD
 * fallback that succeeds still returns -1. Both paths then set every ucInfo to 1. */
int loader_LoadInstallInfo(void)
{
    PS2_FRAME(0x1830);
    PS2_STACK(char[0x1800], cReadBuf, 0x30);   /* its guest address is in the disc trace */
    int ret = 0;
    ZeroMemory8b(cReadBuf, 0x1800);
    for (;;) {
        if (s_iIsReadHdd != 0) {
            int n = (int)hdd_CheckFile(loader_path_install);
            if (n > 0) {
                if (n > 0x1800) n = 0x1800;
                hdd_ReadFileAsync(s_phddSl, loader_path_install, cReadBuf, n);
                s_iLoaderReadMode = 2;
                s_iLastError = 0;
                s_iIsBusy = 1;
                if (LoadingLoop(-1, 0) >= 0) {
                    hdd_CopyMemory(&s_aiinfoHdd, cReadBuf, 0x11fc);
                    ret = 1;
                    break;
                }
                ret = -1;
            }
            s_iIsReadHdd = 0;
            continue;
        }
        File_Read(0x3c, -1, cReadBuf, 0);
        s_iLoaderReadMode = 1;
        s_iIsBusy = 1;
        s_iLastError = 0;
        LoadingLoop(0x3c, 0);
        s_iLoaderReadMode = 0;
        s_iLastError = 0;
        s_iIsBusy = 0;
        hdd_CopyMemory(&s_aiinfoHdd, cReadBuf, 0x11fc);
        break;
    }
    for (int i = 0; i < 14; i++)
        for (int j = 0; j < 0x100; j++) s_aiinfoHdd.iinfoAll[i][j].ucInfo = 1;
    return ret;
}

void loader_SetReadyHdd(int iReady) { s_iIsReadHdd = iReady; }
void loader_ClearDisposeExtPartsFlag(void) { s_iDisposeExtParts = 0; }
int loader_IsDisposeExtPartsFlag(void) { return s_iDisposeExtParts; }
void loader_SetDisposeExtPartsFlag(void) { s_iDisposeExtParts = 1; }
void loader_ClearOutOfMenuError(void) { s_iOutMenuHddError = 0; }
int loader_IsOutOfMenuError(void) { return s_iOutMenuHddError; }
void loader_SetHddErrorAsOutOfMenu(void) { s_iOutMenuHddError = 1; }
int loader_GetAsyncResult(void) { return s_iLastError; }
int loader_IsAsyncBusy(void) { return s_iIsBusy; }

void loader_Init(struct StHddDescriptor *phdd)
{
    PS2_FRAME(0x10);
    s_phddSl = phdd;
    s_iIsReadHdd = 0;
    s_iLoaderReadMode = 0;
    s_iLastError = 0;
    s_iOutMenuHddError = 0;
    s_iDisposeExtParts = 0;
    hdd_ZeroMemory(&s_aiinfoHdd, 0x11fc);
    ZeroMemory8b(&s_iwAsync, 0x14);
}

/* the blocking load loop (LoadInstallInfo): flip, black screen, optional now-loading bar, poll the HDD step machine
 * (iCdvdId -1) or the cdvd state; returns the file size (-1 on an HDD error or when nothing was armed). With an
 * instant CD backend: pass 1 issues the read, pass 2 completes it = two flips. */
static int LoadingLoop(int iCdvdId, int iIsDrawNl)
{
    PS2_FRAME(0x40);
    PS2_STACK(int, iFileSize, 0x3c);   /* hdd_GetResult writes it */
    iFileSize = -1;
    while (s_iLoaderReadMode != 0) {
        Draw_FlipBufferTearing();
        DF_DrawColorScreen_NoSemi(0, 0, 0, 0x80);
        sceGsSyncPath(0, 0);
        if (iIsDrawNl) NL_DrawNowLoading();
        ps2_dma_wait_gif();
        sceGsSyncPath(0, 0);
        if (iCdvdId == -1) hdd_Sync(s_phddSl);
        else File_AsyncProc();
        FlushCache(0);
        ps2_dma_wait_gif();
        sceGsSyncPath(0, 0);
        if (iCdvdId == -1) {
            if (!hdd_IsBusy()) {
                if (hdd_GetResult(&iFileSize)) {
                    s_iLoaderReadMode = 0;
                    s_iLastError = 0;
                    s_iIsBusy = 0;
                } else {
                    s_iLoaderReadMode = 0;
                    s_iIsBusy = 0;
                    s_iLastError = -1;
                    iFileSize = -1;
                }
            }
        } else if (!File_IsAsyncBusy()) {
            s_iLoaderReadMode = 0;
            s_iLastError = 0;
            s_iIsBusy = 0;
            iFileSize = File_GetSize(iCdvdId);
        }
    }
    return iFileSize;
}

/* plain unsigned byte sum (the original unrolls it by 8; same result) */
static int loader_byte_sum(const char *p, int n)
{
    int sum = 0;
    for (int i = 0; i < n; i++) sum += (unsigned char)p[i];
    return sum;
}

/* the menus poll this every frame (iIsExtCdvdProc 1: the caller runs File_AsyncProc itself) */
void loader_AsyncProc(int iIsExtCdvdProc)
{
    PS2_FRAME(0x40);
    if (!s_iIsBusy) return;
    switch (s_iLoaderReadMode) {
    case 1:
        if (iIsExtCdvdProc == 0) File_AsyncProc();
        if (File_IsAsyncBusy()) return;
        s_iLoaderReadMode = 0;
        s_iLastError = 0;
        s_iIsBusy = 0;
        return;
    case 2: {
        hdd_Sync(s_phddSl);
        if (hdd_IsBusy()) return;
        if (!hdd_GetResult(&s_iwAsync.iSize)) {
            s_iLoaderReadMode = 0;
            s_iIsBusy = 0;
            s_iLastError = -1;
            return;
        }
        if (s_iwAsync.iStep != 1) {
            s_iLoaderReadMode = 0;
            s_iLastError = 0;
            s_iIsBusy = 0;
            return;
        }
        /* relay the HDD part to the peer: header (size, sum), then the data */
        int iSize = s_iwAsync.iSize;
        char *pcBuf = s_iwAsync.pcBuf;
        int sum = loader_byte_sum(pcBuf, iSize);
        int ok = 0;
        if (SyncLink() >= 0) {
            memcpy(s_cIlinkBuf + 4, &sum, 4);
            memcpy(s_cIlinkBuf, &iSize, 4);
            if (SendByiLinkS(s_cIlinkBuf, 8, 1) != 0 && SendByiLinkL(pcBuf, iSize, 1) != 0) ok = 1;
        }
        if (!ok) {
            ZeroMemory8b(&s_iwAsync, 0x14);
            iSize = -1;
        }
        if (iSize == -1) {   /* also a successful relay of size -1 (the original tests s2) */
            s_iLoaderReadMode = 0;
            s_iIsBusy = 0;
            s_iLastError = -2;
        } else {
            s_iLoaderReadMode = 0;
            s_iLastError = 0;
            s_iIsBusy = 0;
        }
        return;
    }
    case 3:
        s_iwAsync.iSize = RecvFileiLink(s_iwAsync.pcBuf, 1);
        if (s_iwAsync.iSize >= 0) {
            s_iLoaderReadMode = 0;
            s_iLastError = 0;
            s_iIsBusy = 0;
        } else {
            s_iLoaderReadMode = 0;
            s_iIsBusy = 0;
            s_iLastError = -2;
        }
        return;
    default:
        return;
    }
}

/* receives one file from the peer: SyncLink, 8-byte header (size, byte sum), data; returns the size or -1 */
static int RecvFileiLink(char *pcBuf, int iIsDrawNl)
{
    PS2_FRAME(0x40);
    if (SyncLink() >= 0 && RecvByiLinkS(s_cIlinkBuf, 8, iIsDrawNl) != 0) {
        int sum, size;
        memcpy(&sum, s_cIlinkBuf + 4, 4);
        memcpy(&size, s_cIlinkBuf, 4);
        if (RecvByiLinkL(pcBuf, size, iIsDrawNl) != 0 && loader_byte_sum(pcBuf, size) == sum) {
            ZeroMemory8b(&s_iwAsync, 0x14);
            return size;
        }
    }
    ZeroMemory8b(&s_iwAsync, 0x14);
    return -1;
}

/* one pass of the black "now loading" screen the i.LINK transfer loops draw while they poll */
static void loader_ilink_frame(int iIsDrawNl)
{
    Draw_FlipBufferTearing();
    DF_DrawColorScreen_NoSemi(0, 0, 0, 0x80);
    sceGsSyncPath(0, 0);
    Pad_UpdateData();
    if (iIsDrawNl) NL_DrawNowLoading();
    Snd_Exec();
    ps2_dma_wait_gif();
    sceGsSyncPath(0, 0);
    FlushCache(0);
}

/* short send (liblnk): 1 done, 0 failed (link error, peer count changed, 0x1518-pass timeout) */
static int SendByiLinkS(char *pcBuf, int iSize, int iIsDrawNl)
{
    PS2_FRAME(0x60);
    int iConnect = fsilk_GetConnectedNum();
    int iTimeOut = 0x1518;
    FlushCache(0);
    fslnk_LinkSendData(pcBuf, iSize);
    for (;;) {
        loader_ilink_frame(iIsDrawNl);
        int v;
        if (fslnk_LinkProc() < 0) v = -1;
        else if (fslnk_LinkSendDataSize() <= 0) v = 1;
        else if (fsilk_GetConnectedNum() != iConnect) v = -1;
        else v = (iTimeOut-- > 0) ? 0 : -1;
        if (v == 1) return 1;
        if (v == -1) return 0;
    }
}

/* short receive: iw.iStep stays uninitialised (nothing reads it) */
static int RecvByiLinkS(char *pcBuf, int iSize, int iIsDrawNl)
{
    PS2_FRAME(0x40);
    struct StIlinkWork iw;
    iw.iTimeOut = 0x1518;
    iw.pcBuf = pcBuf;
    iw.iSize = iSize;
    iw.iConnectNum = fsilk_GetConnectedNum();
    FlushCache(0);
    for (;;) {
        loader_ilink_frame(iIsDrawNl);
        int r = ProcRecvByiLinkS(&iw);
        if (r == 1) return 1;
        if (r == -1) return 0;
    }
}

/* 1 = received from the first peer whose buffer holds iSize bytes, -1 = failed, 0 = keep polling */
static int ProcRecvByiLinkS(struct StIlinkWork *piw)
{
    PS2_FRAME(0x40);
    int iPeer = -1;
    if (fslnk_LinkProc() < 0) return -1;
    for (int i = 0; i < play_info.connected_num; i++) {
        if (fslnk_LinkRecvDataSize(i) >= piw->iSize) {
            iPeer = i;
            break;
        }
    }
    if (iPeer != -1) {
        int iSize = piw->iSize;
        if (fslnk_LinkRecvDataSize(iPeer) >= iSize) {
            fslnk_LinkRecvData(piw->pcBuf, iPeer, iSize);
            return 1;
        }
    }
    if (fsilk_GetConnectedNum() != piw->iConnectNum) return -1;
    int old = piw->iTimeOut;
    piw->iTimeOut = old - 1;
    return old > 0 ? 0 : -1;
}

/* long send (socket): also arms fsilk_socketRecvDataL(NULL, iSize) once before polling (original); iw.iStep / pcBuf /
 * iSize are never written (ProcSendByiLinkL reads only iTimeOut and iConnectNum) */
static int SendByiLinkL(char *pcBuf, int iSize, int iIsDrawNl)
{
    PS2_FRAME(0x60);
    struct StIlinkWork iw;
    iw.iTimeOut = 0x1518;
    iw.iConnectNum = fsilk_GetConnectedNum();
    FlushCache(0);
    fsilk_socketSendDataL(pcBuf, iSize);
    fsilk_socketRecvDataL(0, iSize);
    for (;;) {
        loader_ilink_frame(iIsDrawNl);
        int r = ProcSendByiLinkL(&iw);
        if (r == 1) return 1;
        if (r == -1) return 0;
    }
}

static int ProcSendByiLinkL(struct StIlinkWork *piw)
{
    PS2_FRAME(0x30);
    fsilk_IlinkProc();
    int st = (int)fsilk_GetStatSendDataL();
    fsilk_GetStatRecvDataL();   /* result dropped (original) */
    if (st == 1) return 1;
    if (st == -1) return -1;
    if (fsilk_GetConnectedNum() != piw->iConnectNum) return -1;
    int old = piw->iTimeOut;
    piw->iTimeOut = old - 1;
    return old > 0 ? 0 : -1;
}

/* long receive (socket): 1 done, 0 failed */
static int RecvByiLinkL(char *pcBuf, int iSize, int iIsDrawNl)
{
    PS2_FRAME(0x60);
    int iConnect = fsilk_GetConnectedNum();
    int iTimeOut = 0x1518;
    FlushCache(0);
    fsilk_socketRecvDataL(pcBuf, iSize);
    for (;;) {
        loader_ilink_frame(iIsDrawNl);
        fsilk_IlinkProc();
        int st = (int)fsilk_GetStatRecvDataL();
        int v;
        if (st == -1) v = -1;
        else if (st == 1) v = 1;
        else if (fsilk_GetConnectedNum() != iConnect) v = -1;
        else v = (iTimeOut-- > 0) ? 0 : -1;
        if (v == 1) return 1;
        if (v == -1) return 0;
    }
}

/* restarts liblnk and exchanges a 128-byte pattern with every peer; 0 ok, -1 failed. Quirks: without a peer
 * (connected_num 1) nothing is received, recv_data keeps its "0101.." pre-fill and the strcmp fails; the ready count
 * compares the buffered size unsigned (a negative size counts as ready). */
static int SyncLink(void)
{
    PS2_FRAME(0x170);
    PS2_STACK(char[0x80], send_data, 0xf0);
    PS2_STACK(char[0x80], recv_data, 0x70);
    int iPlayer = play_info.connected_num;
    int iSize = 8 / (play_info.play_mode != 0 ? 1 : 2);
    memset(send_data, 0, sizeof(send_data));
    memcpy(send_data, "ZZZYYYXXXWWWVVVUUU", 18);
    memset(recv_data, 0, sizeof(recv_data));
    memcpy(recv_data, "010101010101010101", 18);
    int iTimeOut = 0x960;
    int ok = 1;
    fslnk_LinkSyncTerm();
    fslnk_LinkStop();
    fsilk_IlinkSetCheckAlive(0);
    fslnk_LinkStart();
    /* phase 1: wait for link state 3 */
    for (;;) {
        Draw_FlipBufferTearing();
        DF_DrawColorScreen_NoSemi(0, 0, 0, 0x80);
        int st = fslnk_LinkProc();
        DF_KickPreDraw();
        sceGsSyncPath(0, 0);
        DF_DrawColorScreen_NoSemi(0, 0, 0, 0x80);
        sceGsSyncPath(0, 0);
        NL_DrawNowLoading();
        sceGsSyncPath(0, 0);
        if (st == 3) break;
        if (st < 0) { ok = 0; break; }
        if (iTimeOut-- > 0) continue;
        ok = 0;
        break;
    }
    if (!ok) return -1;
    fslnk_LinkSyncInit((char)iPlayer, (short)iSize, 0x3c, 0, 2);
    fslnk_LinkSendData(send_data, 0x80);
    /* phase 2: until every peer's pattern is buffered and ours is sent */
    iTimeOut = 0x960;
    for (;;) {
        Draw_FlipBufferTearing();
        int st = fslnk_LinkProc();
        DF_DrawColorScreen_NoSemi(0, 0, 0, 0x80);
        sceGsSyncPath(0, 0);
        DF_KickPreDraw();
        sceGsSyncPath(0, 0);
        NL_DrawNowLoading();
        sceGsSyncPath(0, 0);
        if (st < 0) { ok = 0; break; }
        if (!(iTimeOut-- > 0)) { ok = 0; break; }
        int n = play_info.connected_num - 1;
        int count = 0;
        for (int i = 0; i < n;) {
            if ((unsigned int)fslnk_LinkRecvDataSize(i) >= 0x80) count++;
            i++;
            n = play_info.connected_num - 1;
        }
        if (count != n) continue;
        if (fslnk_LinkSendDataSize() > 0) continue;
        for (int i = 0; i < play_info.connected_num - 1; i++) fslnk_LinkRecvData(recv_data, i, 0x80);
        break;
    }
    if (!ok) return -1;
    if (ps2_strcmp(send_data, recv_data) != 0) return -1;
    fslnk_LinkSyncInit((char)iPlayer, (short)iSize, 0x3c, 0, 2);
    if (fslnk_LinkSyncStart() == -1) return -1;
    return 0;
}
