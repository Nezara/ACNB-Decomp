/* lib/file.cpp - the game's file front end over lib/cdvd: File_Read / File_GetSize address the packed archives by file
 * id, File_ReadN / ReadNM / SearchFile / SearchFileM / IsExistNM plain disc files by path, File_IsAsyncBusy /
 * AsyncProc / GetResult expose the async read state machine, File_NlRead forwards to NL_ReadData and File_Init starts
 * the aio thread. File_CorrectPath / AddDeviceString build "cdrom0:\PATH;1" (device prefix, upper case after the
 * ':', '/' -> '\', ";1" appended). `lmode` 0 = CD, 1 = host (a dead dev-kit stub), anything else = no-op.
 * Original oddities kept: File_SearchFile / SearchFileM build the corrected path and then search with the original
 * name, ignoring the search result (1 whenever the fix-up fit); lmode != 0 makes ReadNM / SearchFileM report 1 without
 * doing anything; File_ReadN / ReadNM floor size >> 11 where File_Read rounds up; AddDeviceString ignores a ':' at
 * index 0 / 1; CorrectPath detects a trailing ";1" with a state flag. */
#include <string.h>
#include "acnb/port.h"

/* "cdrom0:" (main sdata 0x27adf8, 8 bytes with the NUL; the original copies it to its stack with ld / sd) */
static const char file_dev_cdrom0[8] = "cdrom0:";

int File_NlRead(int idFile, int size, char *pbuf)
{
    return NL_ReadData(idFile, pbuf, size, 0);
}

int File_CorrectPath(char *pfnameDst, int lenDst, char *pfnameSrc, int lmode)
{
    PS2_FRAME(0x40);
    if (!File_AddDeviceString(pfnameDst, lenDst, pfnameSrc, lmode)) return 0;
    if (lmode != 0) return 1;

    int i = 0, colon = 0, semi1 = 0;
    if (pfnameDst[0] != 0) {
        do {
            char *p = &pfnameDst[i];
            if (!colon && *p == ':') colon = 1;
            if (colon && *p >= 0x61 && *p < 0x7b) *p = (char)(*p - 0x20);
            if (*p == '/') *p = '\\';
            if (semi1 && !(p[0] == '1' && p[1] == 0)) semi1 = 0;
            if (p[0] == ';' && p[1] == '1') semi1 = 1;
            i++;
        } while (pfnameDst[i] != 0);
    }
    if (semi1) return 1;
    if (!(i < lenDst)) return 0;
    pfnameDst[i] = ';';
    if (!(i + 1 < lenDst)) return 0;
    pfnameDst[i + 1] = '1';
    if (!(i + 2 < lenDst)) return 0;
    pfnameDst[i + 2] = 0;
    return 1;
}

int File_AddDeviceString(char *pfnameDst, int lenDst, char *pfnameSrc, int lmode)
{
    PS2_FRAME(0x10);
    PS2_STACK(char[8], dev, 8);
    int o = 0, s = 0;

    if (lmode == 0) {
        memcpy(dev, file_dev_cdrom0, 8);
        for (const char *d = dev; *d; ) {
            pfnameDst[o] = *d;
            o++;
            d++;
            if (!(o < lenDst)) return 0;
        }
    }
    /* device colon of the source (a ':' at index 0 / 1 does not count) */
    while (pfnameSrc[s]) {
        if (s >= 2 && pfnameSrc[s] == ':') break;
        s++;
    }
    if (pfnameSrc[s] == ':') s++;
    else s = 0;
    while (pfnameSrc[s]) {
        pfnameDst[o] = pfnameSrc[s];
        o++;
        s++;
        if (!(o < lenDst)) return 0;
    }
    pfnameDst[o] = 0;
    return 1;
}

int File_GetResult(void)
{
    PS2_FRAME(0x10);
    return Cdvd_GetResult() > 0;
}

int File_IsAsyncBusy(void)
{
    return Cdvd_IsBusy();
}

void File_AsyncProc(void)
{
    Cdvd_Exec();
}

int File_ReadN(char *pfname, int size, char *pbuf, int isBlock)
{
    PS2_FRAME(0x10);
    int sec = size > 0 ? size / 2048 : 0;
    return Cdvd_ReadN(pfname, sec, pbuf, 0, isBlock != 0) != 0;
}

int File_ReadNM(char *pfname, int size, char *pbuf, int isBlock, int lmode)
{
    PS2_FRAME(0x10);
    if (lmode != 0) return 1;
    int sec = size > 0 ? size / 2048 : 0;
    return Cdvd_ReadN(pfname, sec, pbuf, 0, isBlock != 0) != 0;
}

int File_Read(int idFile, int size, char *pbuf, int isBlock)
{
    PS2_FRAME(0x10);
    if (size > 0) size = (size + 0x7ff) & ~0x7ff;
    return Cdvd_Read(idFile, 0, size / 2048, pbuf, 0, isBlock != 0) != 0;   /* size <= 0: C division (sec <= 0) */
}

int File_GetRealSize(int idFile)
{
    return Cdvd_GetSizeByte(idFile);
}

int File_GetSize(int idFile)
{
    PS2_FRAME(0x10);
    return (Cdvd_GetSizeByte(idFile) + 0x7ff) & ~0x7ff;
}

int File_SearchFile(struct struct_9b06e75c *pcf, char *pfname)
{
    PS2_FRAME(0xb0);
    PS2_STACK(char[0x80], buf, 0x30);
    if (!File_CorrectPath(buf, 0x80, pfname, 0)) return 0;
    Cdvd_SearchFile(pcf, pfname, 1, 1);   /* the original name, result ignored */
    return 1;
}

int File_SearchFileM(struct struct_9b06e75c *pcf, char *pfname, int lmode)
{
    PS2_FRAME(0xb0);
    PS2_STACK(char[0x80], buf, 0x30);
    if (lmode != 0) return 1;
    if (!File_CorrectPath(buf, 0x80, pfname, 0)) return 0;
    Cdvd_SearchFile(pcf, pfname, 1, 1);   /* the original name, result ignored */
    return 1;
}

int File_IsExistNM(char *pfname, int lmode)
{
    PS2_FRAME(0x10);
    if (lmode == 1) return Host_IsFileExist();
    if (lmode == 0) return Cdvd_SearchFile(0, pfname, 1, 1);
    return 0;
}

int File_IsExist(int idFile)
{
    return Cdvd_IsFileExist(idFile);
}

int File_Init(void)
{
    PS2_FRAME(0x10);
    AioInit(0x14);
    return 1;
}
