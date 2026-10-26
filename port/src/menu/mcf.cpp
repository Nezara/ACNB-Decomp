/* src/menu/mcf.cpp - the PS2 memory-card engine ("Ac2" card library): a one-command-at-a-time state machine over the
 * interpreted fsmc_* card calls (lib/fs/fslibmc, IOP RPC). A client queues one command (GetList / Load / Save / ...),
 * which only sets gFunc.fn; Ac2_Mcf_Proc -> Ac2_Mcf_Sync then runs one *Proc step per call (at most one card
 * operation each) until it returns 1 (done) or -1 (error through Ac2_Mcf_WrongProc).
 * Native so far (g1): the file table (Ac2_Mcf_GetFileTbl, __Mcf_GetFileData, Ac2_Mcf_GetListInfo), the icon.sys
 * builder Ac2_Mcf_MakeIconSys and the load / save engine (Ac2_Mcf_Load / LoadProc, Ac2_Mcf_Save / SaveProc,
 * Ac2_Mcf_GetList); g2: the game-file listing Ac2_Mcf_GetListProc and the boot title check (Ac2_Mcf_TitleCheck /
 * TitleCheckProc / GetTitleCheckMsg); g3: the engine (Init / Start / Proc / Sync, the GetMcInfo poll GetMcInfoProc)
 * and the accessors. Still interpreted: WrongProc / SetWrongMsg, MakeIconTex, GetEmbListProc, Format / FormatProc.
 * The file names are built with newlib sprintf (ps2_sprintf_*: buffer, format and every %s argument in PS2 memory -
 * the fname / fname_sub statics, the DWARF tables and nodwarf literals); strcpy / memcpy are plain newlib leaves. */
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include <string.h>

#ifdef ACNB_SOURCEPORT
char mcf_fmt_ssss[9] = "%s%s%s%s";
char mcf_slus_dir[14] = "/BASLUS-21200";
char mcf_fmt_iconsys[14] = "%s%s/icon.sys";
char mcf_fmt_ss[5] = "%s%s";
char mcf_fmt_ss_s[8] = "%s%s/%s";

char *mc_icon_name_tbl[3] = {(char *)"ACNINEBREAKERS.ICO", (char *)"ACNINEBREAKERE.ICO", (char *)"ACNEINEBREAKERC.ICO"};
char mc_slps_tbl_chk[3][6][32] = {
    {"/BASLUS-20014S*", "/BASLUS-20249S*", "/BASLUS-20435S*", "/BASLUS-20644S*", "/BASLUS-20986S*", "/BASLUS-21200S*"},
    {"/BASLUS-20014E*", "/BASLUS-20249E*", "/BASLUS-20435E*", "/BASLUS-20644E*", "/BASLUS-20986E*", "/BASLUS-21200E*"},
    {"", "", "/BASLUS-20435C*", "/BASLUS-20644C*", "/BASLUS-20986C*", "/BASLUS-21200C*"},
};
char mc_slps_tbl_f[6][32] = {"/BASLUS-20014", "/BASLUS-20249", "/BASLUS-20435",
                             "/BASLUS-20644", "/BASLUS-20986", "/BASLUS-21200"};
int mc_maxfile_num[3] = {8, 8, 8};
int mc_gamedata_size[6] = {0x290, 0x320, 0x610, 0x13e00, 0x4990, 0x13e00};
int mc_savedata_size[6] = {0x7430, 0x8d20, 0xa250, 0x1e000, 0xd800, 0x1e000};
char mc_name_sub_tbl[3] = {'S', 'E', 'C'};
unsigned int mc_icon_size_tbl[3] = {0x8ab6, 0x9240, 0x9480};
#endif

/* icon.sys titles (SJIS, only strcpy'd): "ＡＣＮＢ　Ｎｏ．１　ＳＯＲＴＩＥ：０００" (0x25ff90), "ＡＣＮＢ　Ｅｍｂｌｅｍ１"
 * (0x25ffc0), "ＡＣＮＢ　ＡＩＡｒｅｎａ１" (0x25ffe0); the digits are bumped in place by MakeIconSys. */
static const char mcf_title_game[] = "\202\140\202\142\202\155\202\141\201\100\202\155\202\217\201\104\202\120\201\100\202\162"
                                     "\202\156\202\161\202\163\202\150\202\144\201\106\202\117\202\117\202\117";
static const char mcf_title_emb[] = "\202\140\202\142\202\155\202\141\201\100\202\144\202\215\202\202\202\214\202\205\202\215"
                                    "\202\120";
static const char mcf_title_ai[] = "\202\140\202\142\202\155\202\141\201\100\202\140\202\150\202\140\202\222\202\205\202\216"
                                   "\202\201\202\120";

/* icon.sys lighting (rodata 0x25fed0..0x25ff7f, copied to MakeIconSys's stack arrays with lq / sq) */
static const unsigned int mcf_bgcolor[16] = {0x32, 0x9e, 0x85, 0, 6, 0x76, 0x83, 0, 6, 0x5d, 0x82, 0, 1, 0x31, 0x6c, 0};
static const unsigned int mcf_lightdir[12] = {0x3f000000, 0x3f000000, 0x3f000000, 0, 0, 0xbecccccd,
                                              0xbdcccccd, 0,          0xbf000000, 0xbf000000, 0x3f000000, 0};
static const unsigned int mcf_lightcol[12] = {0x3f333333, 0x3f266666, 0x3f000000, 0, 0x3ecccccd, 0x3f000000,
                                              0x3f000000, 0,          0x3e4ccccd, 0x3e99999a, 0x3eb33333, 0};
static const unsigned int mcf_ambient[4] = {0x3e19999a, 0x3e19999a, 0x3e19999a, 0};

/* Copies the list fields of a save's game data (layout per game title: 0..5 = mc_convert_f) into a file entry. */
static void __Mcf_GetFileData(struct struct_db08e715 *filep, unsigned int *buff, int title)
{
    PS2_FRAME(0x30);
    unsigned char *b = (unsigned char *)buff;
    switch (title) {
    case 0:
        strcpy(filep->name, (char *)b + 0x30);
        filep->atknum = *(short *)(b + 0xc);
        filep->money = *(int *)b;
        break;
    case 1:
        strcpy(filep->name, (char *)b + 0x10);
        filep->atknum = *(short *)(b + 0xa);
        filep->money = *(int *)b;
        break;
    case 2:
        strcpy(filep->name, (char *)b);
        filep->atknum = *(short *)(b + 0x44);
        filep->money = *(int *)(b + 0x30);
        break;
    case 3:
        strcpy(filep->name, (char *)b);
        filep->atknum = *(short *)(b + 0x44);
        filep->money = *(int *)(b + 0x30);
        strcpy(filep->ai_name, (char *)b + 0x250);
        filep->ai_rank = *(signed char *)(b + 0x211);
        break;
    case 4:
        strcpy(filep->name, (char *)b);
        filep->atknum = 0;
        filep->money = *(int *)(b + 0x30);
        filep->point = Game_GetRavenPointEx((struct StAcigGameData *)b);
        filep->info = b[0x3e];
        break;
    case 5:
        strcpy(filep->name, (char *)b);
        filep->atknum = 0;
        filep->money = *(int *)(b + 0x30);
        filep->point = *(int *)(b + 0x13da4);
        filep->info = b[0x3e];
        break;
    }
}

/* Load step function (Sync, fn 5). Step 0 builds the file name and falls through into step 1 (the read). */
static int Ac2_Mcf_LoadProc(void)
{
    PS2_FRAME(0x60);
    struct struct_87202900 *cp = gCp[gFunc.data[0]];
    int savesize = mc_savedata_size[mc_convert_f];
    int ret = 0;
    int flag = 0;
    if (cp->data_type == 1)
        flag = 1;
    else if (cp->data_type == 2)
        flag = 2;

    switch (gFunc.step) {
    case 0: {
        if (fsmc_FuncCheck() > 0)
            return 0;
        unsigned char *sub = Ac2_Mcf_LoadProc__fname_sub;
        sub[0] = mc_name_sub_tbl[flag];
        sub[1] = '0' + gFunc.data[1] / 10;
        sub[2] = '0' + gFunc.data[1] % 10;
        ps2_sprintf_ssss((char *)Ac2_Mcf_LoadProc__fname, mcf_fmt_ssss, mc_slps_tbl_f[mc_convert_f], (char *)sub,
                         mc_slps_tbl_f[mc_convert_f], (char *)sub);
        gFunc.step++;
    }
        /* fall through */
    case 1: {
        unsigned int size = 0;
        if (flag == 2)
            size = 0x35000;
        else if (flag == 1)
            size = 0x4420;
        else if (flag == 0)
            size = savesize;
        if (fsmc_DataRead(gFunc.data[0], 0, (char *)Ac2_Mcf_LoadProc__fname, cp->datap, size) != 0) {
            if (++gFunc.count == 5)
                ret = Ac2_Mcf_WrongProc();
        } else {
            gFunc.count = 0;
            gFunc.step++;
        }
        break;
    }
    case 2: {
        int r = fsmc_FuncCheck();
        if (r > 0)
            return 0;
        if (fsmc_MessageCheck() == 0x48 && r >= 0)
            ret = 1;
        else
            ret = Ac2_Mcf_WrongProc();
        break;
    }
    }
    return ret;
}

/* Queues a load of file inFileno from port inPort (1-based) into the card info's data buffer. */
int Ac2_Mcf_Load(int inPort, int inFileno)
{
    struct struct_87202900 *cp = gCp[inPort - 1];
    if (gFunc.fn != 0 || inFileno < 0)
        return -1;
    if (cp->data_type == 0 && inFileno >= 8)
        return -1;
    if (cp->data_type == 1 && inFileno >= 8)
        return -1;
    if (cp->data_type == 2 && inFileno >= 8)
        return -1;
    if (cp->datap == NULL)
        return -1;
    gFunc.data[1] = inFileno;
    gFunc.w_step = 0;
    gFunc.data[0] = inPort - 1;
    gFunc.pre_fn = 5;
    gFunc.fn = 5;
    gFunc.step = 0;
    gFunc.w_count = 0;
    gFunc.count = 0;
    return 0;
}

/* Fills a list entry for the save / emblem screens from the file table. -1 = no such file, -2 = broken file. */
int Ac2_Mcf_GetListInfo(struct struct_17022ecb *ioIp, int inPort, int inFileno)
{
    PS2_FRAME(0x40);
    struct struct_87202900 *cp = gCp[inPort - 1];
    if (ioIp == NULL || inFileno < 0 || inFileno >= 8)
        return -1;
    struct struct_db08e715 *fp = Ac2_Mcf_GetFileTbl(inPort - 1, inFileno, 0);
    if (fp == NULL) {
        ioIp->tm2p = 0;
        return -1;
    }
    ioIp->no = fp->fileno;
    if (fp->f_brk_game || fp->f_brk_iconsys) {
        strcpy(ioIp->name, fp->name);
        strcpy(ioIp->ai_name, fp->name); /* original: the name, not ai_name */
        ioIp->tm2p = 0;
        return -2;
    }
    if (cp->data_type == 0) {
        strcpy(ioIp->name, fp->name);
        ioIp->atknum = fp->atknum;
        ioIp->money = fp->money;
        ioIp->enable_ai = !(fp->ai_rank < 0);
        strcpy(ioIp->ai_name, fp->ai_name);
        ioIp->ai_rank = fp->ai_rank;
        ioIp->point = fp->point;
        ioIp->info = fp->info;
    } else if (cp->data_type == 1) {
        ioIp->tm2p = (char *)gTm2Topp + fp->fileno * 0x4420;
    } else if (cp->data_type == 2) {
        strcpy(ioIp->name, fp->name);
        ioIp->atknum = fp->atknum;
    }
    return 0;
}

/* Builds icon.sys (gISp) for file inNum on port inPort (0-based): title with the file number (and, for game saves,
 * the sortie count) bumped into its full-width digits, icon names, lighting; game saves also refresh the file entry. */
static void Ac2_Mcf_MakeIconSys(int inPort, int inNum)
{
    PS2_FRAME(0x100);
    PS2_STACK(unsigned int[16], bgcolor, 0xc0);
    PS2_STACK(unsigned int[12], lightdir, 0x90);
    PS2_STACK(unsigned int[12], lightcol, 0x60);
    PS2_STACK(unsigned int[4], ambient, 0x50);
    memcpy(bgcolor, mcf_bgcolor, sizeof(mcf_bgcolor));
    memcpy(lightdir, mcf_lightdir, sizeof(mcf_lightdir));
    memcpy(lightcol, mcf_lightcol, sizeof(mcf_lightcol));
    memcpy(ambient, mcf_ambient, sizeof(mcf_ambient));
    struct struct_87202900 *cp = gCp[inPort];
    memset(gISp, 0, 0x3c4);
    struct struct_db08e715 *fp = Ac2_Mcf_GetFileTbl(inPort, inNum, 1);

    if (cp->data_type == 0) {
        unsigned char *dp = (unsigned char *)cp->datap;
        int num;
        switch (mc_convert_f) {
        case 0: num = *(short *)(dp + 0xc); break;
        case 1: num = *(short *)(dp + 0xa); break;
        case 2: num = *(short *)(dp + 0x44); break;
        case 3: num = *(short *)(dp + 0x44); break;
        case 4: num = *(unsigned short *)(dp + 0x3b2) + *(unsigned short *)(dp + 0x4566); break;
        case 5: num = *(short *)(dp + 0x46); break;
        default: num = 0; break;
        }
        if (num >= 1000)
            num = 999;
        strcpy((char *)gISp->TitleName, mcf_title_game);
        unsigned char *isp = (unsigned char *)gISp;
        isp[0xd1] += (unsigned char)(inNum % 10);
        isp[0xe3] += (unsigned char)(num / 100);
        isp[0xe5] += (unsigned char)(num % 100 / 10);
        isp[0xe7] += (unsigned char)(num % 10);
        strcpy((char *)gISp->FnameView, mc_icon_name_tbl[0]);
        strcpy((char *)gISp->FnameCopy, mc_icon_name_tbl[0]);
        strcpy((char *)gISp->FnameDel, mc_icon_name_tbl[0]);
        gISp->TransRate = 0x40;
        memcpy(gISp->BgColor, bgcolor, 0x40);
        memcpy(gISp->LightDir, lightdir, 0x30);
        memcpy(gISp->LightColor, lightcol, 0x30);
        memcpy(gISp->Ambient, ambient, 0x10);
        gISp->OffsLF = 0x12;
        __Mcf_GetFileData(fp, (unsigned int *)cp->datap, mc_convert_f);
    } else if (cp->data_type == 1) {
        strcpy((char *)gISp->TitleName, mcf_title_emb);
        ((unsigned char *)gISp)[0xd7] += (unsigned char)(inNum % 10);
        gISp->OffsLF = 0x20;
        strcpy((char *)gISp->FnameView, mc_icon_name_tbl[1]);
        strcpy((char *)gISp->FnameCopy, mc_icon_name_tbl[1]);
        strcpy((char *)gISp->FnameDel, mc_icon_name_tbl[1]);
        gISp->TransRate = 0x40;
        memcpy(gISp->BgColor, bgcolor, 0x40);
        memcpy(gISp->LightDir, lightdir, 0x30);
        memcpy(gISp->LightColor, lightcol, 0x30);
        memcpy(gISp->Ambient, ambient, 0x10);
    } else if (cp->data_type == 2) {
        strcpy((char *)gISp->TitleName, mcf_title_ai);
        ((unsigned char *)gISp)[0xd9] += (unsigned char)(inNum % 10);
        strcpy((char *)gISp->FnameView, mc_icon_name_tbl[2]);
        strcpy((char *)gISp->FnameCopy, mc_icon_name_tbl[2]);
        strcpy((char *)gISp->FnameDel, mc_icon_name_tbl[2]);
        gISp->TransRate = 0x40;
        memcpy(gISp->BgColor, bgcolor, 0x40);
        memcpy(gISp->LightDir, lightdir, 0x30);
        memcpy(gISp->LightColor, lightcol, 0x30);
        memcpy(gISp->Ambient, ambient, 0x10);
        gISp->OffsLF = 0x20;
        unsigned char *dp = (unsigned char *)cp->datap;
        if (mc_convert_f == 5 || mc_convert_f == 4 || mc_convert_f == 3) {
            strcpy(fp->name, (char *)dp);
            fp->atknum = dp[0x30];
        }
    }
    strcpy((char *)gISp->Head, "PS2D");
}

/* Finds the valid entry with file number inFileno among port inPort's (0-based) snum entries; with inFlag == 1 a
 * missing file gets the next free slot (fileno set only: SaveProc sets f_dir and bumps snum later). */
static struct struct_db08e715 *Ac2_Mcf_GetFileTbl(int inPort, int inFileno, int inFlag)
{
    struct struct_87202900 *cp = gCp[inPort];
    struct struct_db08e715 *fp = gFilep;
    int snum = cp->snum;
    int i = 0;
    for (; i < snum; i++, fp++) {
        if (fp->fileno == inFileno && fp->f_dir)
            break;
    }
    if (i >= 8)
        return NULL;
    if (i < snum)
        return fp;
    if (inFlag == 1) {
        fp->fileno = inFileno & 0xf;
        return fp;
    }
    return NULL;
}

/* Save step function (Sync, fn 4): check the card, create the directory (new file), write the data, the icon and
 * icon.sys, then update the file count / next free file number. */
static int Ac2_Mcf_SaveProc(void)
{
    PS2_FRAME(0x70);
    int file = gFunc.data[1];
    struct struct_87202900 *cp = gCp[gFunc.data[0]];
    int maxfile = 0;
    int flag = 0;
    int ret = 0;
    int r;
    if (cp->data_type == 2) {
        flag = 2;
        maxfile = 8;
    } else if (cp->data_type == 1) {
        flag = 1;
        maxfile = 8;
    } else if (cp->data_type == 0) {
        maxfile = 8;
    }
    unsigned char *sub = Ac2_Mcf_SaveProc__fname_sub;
    char *fname = (char *)Ac2_Mcf_SaveProc__fname;

    switch (gFunc.step) {
    case 0:
        if (fsmc_FuncCheck() > 0)
            return 0;
        if (fsmc_GetFileInfo(gFunc.data[0], 0) != 0) {
            if (++gFunc.count == 5) {
                ret = 1;
                cp->stat = 2;
                cp->msg = 1;
            }
        } else {
            gFunc.count = 0;
            gFunc.step++;
        }
        break;
    case 1:
        if (fsmc_FuncCheck() > 0)
            return 0;
        if (fsmc_MessageCheck() != 0)
            return Ac2_Mcf_WrongProc();
        if (cp->type == 2 && cp->stat == 0 && cp->f_exist != 0 && cp->f_fmt != 0) {
            sub[0] = mc_name_sub_tbl[flag];
            Ac2_Mcf_SaveProc__fp = Ac2_Mcf_GetFileTbl(gFunc.data[0], file, 1);
            if (Ac2_Mcf_SaveProc__fp == NULL)
                return Ac2_Mcf_WrongProc();
            gFunc.f_cmn = Ac2_Mcf_SaveProc__fp->f_dir ? 0 : 1;
            sub[1] = '0' + file / 10;
            sub[2] = '0' + file % 10;
            if (gFunc.f_cmn) {
                ps2_sprintf_ss(fname, mcf_fmt_ss, mcf_slus_dir, (char *)sub);
                gFunc.step++;
            } else {
                gFunc.step = 4;
            }
            cp->stat = 1;
        } else {
            ret = Ac2_Mcf_WrongProc();
        }
        break;
    case 2:
        if (fsmc_MkDir(gFunc.data[0], 0, fname) != 0) {
            if (++gFunc.count == 5)
                ret = Ac2_Mcf_WrongProc();
        } else {
            gFunc.step++;
            gFunc.count = 0;
        }
        break;
    case 3:
        r = fsmc_FuncCheck();
        if (r > 0)
            return 0;
        if (fsmc_MessageCheck() == 0x64 || r < 0) {
            Ac2_Mcf_SaveProc__fp->f_dir = 0;
            ret = Ac2_Mcf_WrongProc();
        } else {
            Ac2_Mcf_SaveProc__fp->f_dir = 1;
            gFunc.step++;
        }
        break;
    case 4:
        ps2_sprintf_ssss(fname, mcf_fmt_ssss, mcf_slus_dir, (char *)sub, mcf_slus_dir, (char *)sub);
        gFunc.step++;
        break;
    case 5: {
        unsigned int size = 0;
        if (flag == 2)
            size = 0x35000;
        else if (flag == 1)
            size = 0x4420;
        else if (flag == 0)
            size = 0x1e000;
        if (fsmc_DataWrite(gFunc.data[0], 0, fname, cp->datap, size) != 0) {
            if (++gFunc.count == 5)
                ret = Ac2_Mcf_WrongProc();
        } else {
            gFunc.step++;
            gFunc.count = 0;
        }
        break;
    }
    case 6:
        r = fsmc_FuncCheck();
        if (r > 0)
            return 0;
        if (fsmc_MessageCheck() == 0x3e && r >= 0) {
            Ac2_Mcf_SaveProc__fp->f_brk_game = 0;
            if (flag == 1)
                Ac2_Mcf_MakeIconTex(gFunc.data[0]);
            ps2_sprintf_sss(fname, mcf_fmt_ss_s, mcf_slus_dir, (char *)sub, mc_icon_name_tbl[flag]);
            gFunc.step++;
        } else {
            Ac2_Mcf_SaveProc__fp->f_brk_game = 1;
            ret = Ac2_Mcf_WrongProc();
        }
        break;
    case 7:
        if (fsmc_IconWrite(gFunc.data[0], 0, fname, cp->iconp[0], mc_icon_size_tbl[flag]) != 0) {
            if (++gFunc.count == 5)
                ret = Ac2_Mcf_WrongProc();
        } else {
            gFunc.step++;
            gFunc.count = 0;
        }
        break;
    case 8:
        r = fsmc_FuncCheck();
        if (r > 0)
            return 0;
        if (fsmc_MessageCheck() == 0x3e && r >= 0) {
            ps2_sprintf_ss(fname, mcf_fmt_iconsys, mcf_slus_dir, (char *)sub);
            Ac2_Mcf_MakeIconSys(gFunc.data[0], file);
            gFunc.step++;
        } else {
            ret = Ac2_Mcf_WrongProc(); /* no f_brk_iconsys here (only state 10 sets it) */
        }
        break;
    case 9:
        if (fsmc_IconWrite(gFunc.data[0], 0, fname, gISp, 0x3c4) != 0) {
            if (++gFunc.count == 5)
                ret = Ac2_Mcf_WrongProc();
        } else {
            gFunc.step++;
            gFunc.count = 0;
        }
        break;
    case 10:
        r = fsmc_FuncCheck();
        if (r > 0)
            return 0;
        if (fsmc_MessageCheck() != 0x3e || r < 0) {
            Ac2_Mcf_SaveProc__fp->f_brk_iconsys = 1;
            return Ac2_Mcf_WrongProc();
        }
        Ac2_Mcf_SaveProc__fp->f_brk_iconsys = 0;
        if (gFunc.f_cmn) {
            if (++cp->snum == maxfile) {
                cp->f_new_save = 0;
                cp->new_fileno = -1;
            } else {
                unsigned int used = 0;
                int i;
                Ac2_Mcf_SaveProc__fp = gFilep;
                for (i = 0; i < cp->snum; i++) {
                    if (Ac2_Mcf_SaveProc__fp->f_dir)
                        used |= 1u << Ac2_Mcf_SaveProc__fp->fileno;
                    Ac2_Mcf_SaveProc__fp++;
                }
                i = -1;
                do
                    i++;
                while (used & (1u << (i & 31)));
                cp->new_fileno = i;
            }
        }
        ret = 1;
        break;
    }
    return ret;
}

/* Queues a save of the card info's data buffer as file inFileno on port inPort (1-based); inFileno -1 = the next free
 * file (fails with msg 8 when the card has no room). Emblems are also copied into the list's tm2 buffer. */
int Ac2_Mcf_Save(int inPort, int inFileno)
{
    PS2_FRAME(0x30);
    struct struct_87202900 *cp = gCp[inPort - 1];
    int fileno;
    if (inFileno == -1) {
        if (cp->f_new_save == 0) {
            int maxfile = 0;
            int need = 0;
            if (cp->data_type == 0) {
                maxfile = 8;
                need = 0xa0;
            } else if (cp->data_type == 1) {
                maxfile = 8;
                need = 0x3c;
            } else if (cp->data_type == 2) {
                maxfile = 8;
                need = 0xfe;
            }
            if (cp->snum < maxfile && cp->free_size < need)
                cp->msg = 8;
            return -1;
        }
        fileno = cp->new_fileno;
    } else {
        fileno = inFileno;
    }
    if (gFunc.fn != 0 || fileno < 0)
        return -1;
    if (cp->data_type == 0 && fileno >= 8)
        return -1;
    if (cp->data_type == 1 && fileno >= 8)
        return -1;
    if (cp->data_type == 2 && fileno >= 8)
        return -1;
    if (cp->data_type == 1) {
        if (gTm2Topp == NULL)
            return -1;
        memcpy((char *)gTm2Topp + fileno * 0x4420, cp->datap, 0x4420);
    }
    gFunc.data[1] = fileno;
    gFunc.w_step = 0;
    gFunc.data[0] = inPort - 1;
    gFunc.pre_fn = 4;
    gFunc.fn = 4;
    gFunc.step = 0;
    gFunc.w_count = 0;
    gFunc.count = 0;
    return 0;
}

/* Queues the file listing of port inPort (1-based); ioBuff receives the emblem tm2 images (gTm2Topp). */
void *Ac2_Mcf_GetList(int inPort, void *ioBuff)
{
    if (gFunc.fn != 0)
        return 0;
    gFunc.data[0] = inPort - 1;
    gFunc.pre_fn = 3;
    gTm2Topp = ioBuff;
    gFunc.fn = 3;
    gFunc.w_step = 0;
    gFunc.step = 0;
    gFunc.w_count = 0;
    gFunc.count = 0;
    return ioBuff;
}

/* ---- g2: title check + listing ---- */

/* Boot title-logo message for port inPort (1-based): 2 = not a PS2 card, 8 / 1 = formatted card with no files and
 * less than 0xa0 free (stat 0 / not 0), 0 = nothing to say. */
int Ac2_Mcf_GetTitleCheckMsg(int inPort)
{
    struct struct_87202900 *cp = gCp[inPort - 1];
    if (cp->type != 2)
        return 2; /* v0 = 2 (the compare constant) is live in the branch to the epilogue */
    if (cp->f_fmt == 0 || (unsigned int)cp->free_size >= 0xa0 || cp->snum > 0)
        return 0;
    return cp->stat == 0 ? 8 : 1;
}

/* Title check step function (Sync, fn 6): 9 states per port, step = port * 9 + state. Port 0 always moves on to
 * port 1 (step 9); port 1 ends the command at its last reachable state (absolute step 10 / 11 / 16 / 17). */
static int Ac2_Mcf_TitleCheckProc(void)
{
    PS2_FRAME(0x50);
    int step = gFunc.step;
    int port = step / 9;
    int ret = 0;
    struct struct_87202900 *cp;
    if ((unsigned int)(step % 9) >= 9)
        return 0;
    cp = gCp[port];
    switch (step % 9) {
    case 0:
        if (fsmc_FuncCheck() > 0)
            return 0;
        if (fsmc_GetFileInfo(port, 0) != 0) {
            if (++gFunc.count == 5) {
                ret = 1;
                cp->stat = 2;
                cp->msg = 1;
            }
        } else {
            gFunc.count = 0;
            gFunc.step++;
        }
        break;
    case 1: {
        if (fsmc_FuncCheck() > 0)
            return 0;
        int r = fsmc_MessageCheck();
        if (r == 2 || r == 0) {
            cp->f_exist = 1;
            cp->f_fmt = 1;
            gFunc.step++;
        } else if (r == 4 || r == 3) {
            if (++gFunc.count == 5) { /* count not reset */
                cp->f_exist = 1;
                cp->f_fmt = 0;
                cp->snum = 0;
                cp->f_new_save = 0;
                cp->free_size = 0;
                cp->msg = 3;
                gFunc.step++;
            }
        } else if (r == 0x64) {
            cp->stat = 2;
            cp->msg = 1;
            if (gFunc.step == 10)
                ret = 1;
            else
                gFunc.step = 9;
        } else { /* 0x15, 1 and anything else: no card */
            cp->type = 0;
            cp->f_exist = 0;
            cp->f_fmt = 0;
            cp->snum = 0;
            cp->f_new_save = 0;
            cp->free_size = 0;
            cp->stat = 0;
            cp->msg = 2;
            if (gFunc.step == 10)
                ret = 1;
            else
                gFunc.step = 9;
        }
        break;
    }
    case 2:
        cp->type = fsmc_GetMemoryCardType(port, 0);
        if (cp->type != 2)
            return 0; /* not a PS2 card: the state repeats */
        if (cp->f_fmt) {
            cp->free_size = fsmc_GetFreeMemory(port, 0);
            gFunc.step++;
            if ((unsigned int)cp->free_size >= 0xa0)
                cp->stat = 0;
        } else {
            cp->stat = 0;
            if (gFunc.step == 0xb)
                ret = 1;
            else
                gFunc.step = 9;
        }
        break;
    case 3:
        if (fsmc_GetDir(port, 0, mc_slps_tbl_chk[0][mc_convert_f], 0, 8, gTblp) != 0) {
            if (++gFunc.count == 5) {
                cp->stat = 2;
                gFunc.step++;
            }
        } else {
            cp->snum = 0;
            cp->f_new_save = 0;
            gFunc.count = 0;
            gFunc.step++;
        }
        break;
    case 4:
        if (fsmc_FuncCheck() > 0)
            return 0;
        if (fsmc_MessageCheck() == 0) {
            cp->snum = fsmc_GetDirPoints();
            cp->stat = 0;
        } else {
            cp->stat = 2;
        }
        gFunc.step++;
        break;
    case 5: /* saves of the older titles (rows [0][0] / [0][1]) are summed into gAc2Ac2aaSum */
        if (fsmc_GetDir(port, 0, mc_slps_tbl_chk[0][0], 0, 8, gTblpAc2) != 0) {
            if (++gFunc.count == 5) /* count kept */
                gFunc.step++;
        } else {
            gFunc.count = 0;
            gFunc.step++;
        }
        break;
    case 6:
        if (fsmc_FuncCheck() > 0)
            return 0;
        if (fsmc_MessageCheck() == 0)
            gAc2Ac2aaSum += fsmc_GetDirPoints();
        gFunc.step++;
        break;
    case 7:
        if (fsmc_GetDir(port, 0, mc_slps_tbl_chk[0][1], 0, 8, gTblpAc2) != 0) {
            if (++gFunc.count == 5) {
                if (gFunc.step < 0x10)
                    gFunc.step++;
                else if (gFunc.step == 0x10)
                    ret = 1;
                else
                    gFunc.step = 9;
            }
        } else {
            gFunc.count = 0;
            gFunc.step++;
        }
        break;
    case 8:
        if (fsmc_FuncCheck() > 0)
            return 0;
        if (fsmc_MessageCheck() == 0) {
            gAc2Ac2aaSum += fsmc_GetDirPoints();
            if (gAc2Ac2aaSum > 0) {
                if (gFunc.step == 0x11)
                    ret = 1;
                else
                    gFunc.step = 9;
                break;
            }
        }
        if (gFunc.step < 0x11)
            gFunc.step++;
        else
            ret = 1;
        break;
    }
    return ret;
}

/* Queues the boot title check (both ports). */
int Ac2_Mcf_TitleCheck(void)
{
    if (gFunc.fn != 0)
        return -1;
    gFunc.w_step = 0;
    gFunc.step = 0;
    gAc2Ac2aaSum = 0;
    gFunc.pre_fn = 6;
    gFunc.fn = 6;
    gFunc.w_count = 0;
    gFunc.count = 0;
    return 0;
}

/* File listing step function (Sync, fn 3; data_type 0 / 2 - type 1 goes to GetEmbListProc): reads the directory,
 * numbers the entries from their directory names, then reads every file (state 4-6) and checks its icon.sys
 * (state 7-8); state 9 marks broken entries "Corrupt File". States 4 -> 5 and 8 -> 9 fall through. */
static int Ac2_Mcf_GetListProc(void)
{
    PS2_FRAME(0x80);
    PS2_STACK(char[4], fname_sub, 0x7c); /* sprintf argument: PS2 memory */
    memset(fname_sub, 0, 4);
    struct struct_87202900 *cp = gCp[gFunc.data[0]];
    struct struct_db08e715 *fp;
    int ret = 0;
    int rc = 0;
    int max = 0;
    int flag = 0;
    if (cp->data_type == 0) {
        max = 8;
        fname_sub[0] = 'S';
    } else if (cp->data_type == 1) {
        flag = 1;
        max = 8;
        fname_sub[0] = 'E';
    } else if (cp->data_type == 2) {
        flag = 2;
        max = 8;
        fname_sub[0] = 'C';
    }

    switch (gFunc.step) {
    case 0: {
        if (fsmc_FuncCheck() > 0)
            return 0;
        int kind = cp->data_type & 1;
        for (int i = 0; i < 8; i++) {
            fp = &gFilep[i];
            fp->f_dir = 0;
            fp->fileno = 0;
            fp->f_brk_iconsys = 0;
            fp->f_brk_game = 0;
            fp->name[0] = 0;
            fp->money = 0;
            fp->point = 0;
            fp->atknum = 0;
            fp->info = 0;
            fp->f_kind = kind;
        }
        if (cp->type == 2 && cp->stat == 0 && cp->f_exist && cp->f_fmt) {
            cp->stat = 1;
            gFunc.step++;
        } else {
            ret = Ac2_Mcf_WrongProc();
        }
        break;
    }
    case 1: {
        int type = cp->data_type;
        if (fsmc_GetDir(gFunc.data[0], 0, mc_slps_tbl_chk[type][mc_convert_f], 0, mc_maxfile_num[type], gTblp) != 0) {
            if (++gFunc.count == 5)
                ret = Ac2_Mcf_WrongProc();
        } else {
            cp->snum = 0;
            cp->f_new_save = 0;
            gFunc.count = 0;
            gFunc.step++;
        }
        break;
    }
    case 2:
        if (fsmc_FuncCheck() > 0)
            return 0;
        if (fsmc_MessageCheck() == 0)
            gFunc.step++;
        else
            ret = Ac2_Mcf_WrongProc();
        break;
    case 3: {
        unsigned int bits = 0;
        cp->new_fileno = 0;
        cp->snum = fsmc_GetDirPoints();
        if (cp->snum == 0) {
            if (fsmc_GetInfoCode(gFunc.data[0], 0) != 0) {
                ret = Ac2_Mcf_WrongProc();
                break;
            }
            ret = 1; /* empty card: the command ends here (Sync clears it), no GetCardInfo */
            cp->f_new_save = 1;
        } else {
            cp->f_new_save = cp->snum < max ? 1 : 0;
        }
        /* the file numbers are the two digits at EntryName[13..14] ("/BASLUS-xxxxxS<nn>" without the slash) */
        int n = cp->snum;
        for (int i = 0; i < n; i++) {
            unsigned char *name = gTblp[i].EntryName;
            signed char no = (signed char)((name[13] - '0') * 10 + (name[14] - '0'));
            if (no >= 0 && no < max)
                bits |= 1u << no;
        }
        /* entry k gets the k-th number present (sllv: shift by the low 5 bits) */
        int bit = -1;
        for (int k = 0; k < cp->snum; k++) {
            do
                bit++;
            while (!(bits & (1u << (bit & 31))));
            gFilep[k].fileno = bit & 0xf;
            gFilep[k].f_dir = 1;
        }
        if (cp->snum < max) {
            int b = -1;
            do
                b++;
            while (bits & (1u << (b & 31)));
            cp->new_fileno = b;
        }
        gFunc.step++;
        Ac2_Mcf_GetListProc__files = 0;
        break;
    }
    case 4:
        fp = &gFilep[Ac2_Mcf_GetListProc__files];
        fname_sub[1] = '0' + fp->fileno / 10;
        fname_sub[2] = '0' + fp->fileno % 10;
        ps2_sprintf_ssss((char *)Ac2_Mcf_GetListProc__fname, mcf_fmt_ssss, mc_slps_tbl_f[mc_convert_f], fname_sub,
                         mc_slps_tbl_f[mc_convert_f], fname_sub);
        gFunc.step++;
        /* fall through */
    case 5:
        if (flag == 0)
            rc = fsmc_DataRead(gFunc.data[0], 0, (char *)Ac2_Mcf_GetListProc__fname, gTmpp,
                               mc_gamedata_size[mc_convert_f]);
        else if (flag == 1)
            rc = fsmc_FileChk(gFunc.data[0], 0, (char *)Ac2_Mcf_GetListProc__fname);
        else if (flag == 2)
            rc = fsmc_DataRead(gFunc.data[0], 0, (char *)Ac2_Mcf_GetListProc__fname, gTmpp, 0x40);
        if (rc != 0) {
            if (++gFunc.count == 5)
                ret = Ac2_Mcf_WrongProc();
        } else {
            gFunc.count = 0;
            gFunc.step++;
        }
        break;
    case 6: {
        if (fsmc_FuncCheck() > 0)
            return 0;
        int r = fsmc_MessageCheck();
        if (flag == 0) {
            if (r == 0x48) {
                __Mcf_GetFileData(&gFilep[Ac2_Mcf_GetListProc__files], gTmpp, mc_convert_f);
            } else if (r == 0x47) {
                gFilep[Ac2_Mcf_GetListProc__files].f_brk_game = 1;
                Ac2_Mcf_GetListProc__files++;
                gFunc.step = Ac2_Mcf_GetListProc__files < cp->snum ? 4 : 9;
                break;
            } else {
                ret = Ac2_Mcf_WrongProc();
                break;
            }
        } else if (flag == 1) {
            if (r == 0x2d) {
                gFilep[Ac2_Mcf_GetListProc__files].f_brk_game = 1;
                Ac2_Mcf_GetListProc__files++;
                gFunc.step = Ac2_Mcf_GetListProc__files < cp->snum ? 3 : 8; /* +1 below */
            } else if (r != 0x2e) {
                ret = Ac2_Mcf_WrongProc(); /* and still builds the icon.sys name + step++ below */
            }
        } else if (flag == 2) {
            if (r == 0x48) {
                if (mc_convert_f == 5 || mc_convert_f == 4 || mc_convert_f == 3 || mc_convert_f == 2) {
                    unsigned char *tmp = (unsigned char *)gTmpp;
                    strcpy(gFilep[Ac2_Mcf_GetListProc__files].name, (char *)tmp);
                    gFilep[Ac2_Mcf_GetListProc__files].atknum = tmp[0x30];
                }
            } else if (r == 0x47) {
                gFilep[Ac2_Mcf_GetListProc__files].f_brk_game = 1;
                Ac2_Mcf_GetListProc__files++;
                gFunc.step = Ac2_Mcf_GetListProc__files < cp->snum ? 4 : 9;
                break;
            } else {
                ret = Ac2_Mcf_WrongProc();
                break;
            }
        }
        fname_sub[0] = mc_name_sub_tbl[flag];
        fp = &gFilep[Ac2_Mcf_GetListProc__files];
        fname_sub[1] = '0' + fp->fileno / 10;
        fname_sub[2] = '0' + fp->fileno % 10;
        ps2_sprintf_ss((char *)Ac2_Mcf_GetListProc__fname, mcf_fmt_iconsys, mc_slps_tbl_f[mc_convert_f], fname_sub);
        gFunc.step++;
        break;
    }
    case 7:
        if (fsmc_FileChk(gFunc.data[0], 0, (char *)Ac2_Mcf_GetListProc__fname) != 0) {
            if (++gFunc.count == 5)
                ret = Ac2_Mcf_WrongProc();
        } else {
            gFunc.count = 0;
            gFunc.step++;
        }
        break;
    case 8: {
        if (fsmc_FuncCheck() > 0)
            return 0;
        int r = fsmc_MessageCheck();
        if (r == 0x2d) {
            gFilep[Ac2_Mcf_GetListProc__files].f_brk_iconsys = 1;
            Ac2_Mcf_GetListProc__files++;
        } else if (r == 0x2e) {
            Ac2_Mcf_GetListProc__files++;
        } else {
            return Ac2_Mcf_WrongProc();
        }
        if (Ac2_Mcf_GetListProc__files < cp->snum) {
            gFunc.step = 4;
            break;
        }
        gFunc.step = 9;
    }
        /* fall through */
    case 9:
        for (Ac2_Mcf_GetListProc__files = 0; Ac2_Mcf_GetListProc__files < cp->snum; Ac2_Mcf_GetListProc__files++) {
            fp = &gFilep[Ac2_Mcf_GetListProc__files];
            if (fp->f_brk_game)
                strcpy(fp->name, "Corrupt File");
            if (fp->f_brk_iconsys)
                strcpy(fp->name, "Corrupt File");
        }
        if (fsmc_GetCardInfo(gFunc.data[0], 0) != 0)
            ret = Ac2_Mcf_WrongProc();
        else
            ret = 1;
        break;
    }
    return ret;
}

/* ---- g3: engine + accessors ---- */

/* 1 when the background card poll (gChkFunc) is at step 0, i.e. not mid-way through a GetMcInfo pass. */
int Ac2_Mcf_CheckGetInfoStep(void)
{
    return gChkFunc.step == 0;
}

/* Number of game files on port inPort (1-based). */
int Ac2_Mcf_GetSnum(int inPort)
{
    return gCp[inPort - 1]->snum;
}

/* Forgets the card type of port (0-based) so the next poll re-reads it. */
void ResetMCardType(int port)
{
    gCp[port]->type = 0;
}

/* Forces the card status of port (0-based) (msys: card pulled dialogs). */
void SetForceMCardStatus(int port, int stat)
{
    gCp[port]->stat = (char)stat;
}

/* 1 while a card command is queued / running. */
int CheckMCardProc(void)
{
    return gFunc.fn != 0;
}

/* Card type of port (0-based); 2 = PS2 memory card. */
int GetPortStatus(int port)
{
    return gCp[port]->type;
}

/* Selects the game title (0..5) whose save layout / directory names are used. */
void ChangeConvertMode(int no)
{
    mc_convert_f = no;
}

/* Queues a card info refresh: a port in error / recheck state (stat != 0) is marked for recheck (1). Unlike the other
 * commands it does not test gFunc.fn first; the work is done by Proc's poll (gChkFunc), not by Sync. */
int Ac2_Mcf_GetMcInfo(void)
{
    gFunc.pre_fn = 1;
    gFunc.fn = 1;
    for (int i = 0; i < 2; i++) {
        if (gCp[i]->stat != 0)
            gCp[i]->stat = 1;
    }
    return 0;
}

/* Background card poll (gChkFunc), one card operation per call: 5 states per port, step = port * 5 + state.
 * Returns 1 when both ports are done (step >= 10) or the pass ends early at port 1 (the step == 6 / 7 / 8 / 9 checks;
 * a failing port 0 jumps to port 1, step 5). */
static int Ac2_Mcf_GetMcInfoProc(void)
{
    PS2_FRAME(0x50);
    int ret = 0;
    if (gChkFunc.step >= 10)
        return 1;
    int step = gChkFunc.step;
    int port = step / 5;
    struct struct_87202900 *cp = gCp[port];
    switch (step % 5) {
    case 0:
        if (fsmc_GetFileInfo(port, 0) != 0) {
            if (++gChkFunc.count == 5) {
                ret = 1;
                cp->msg = 1;
            }
        } else {
            gChkFunc.count = 0;
            gChkFunc.step++;
        }
        break;
    case 1: {
        if (fsmc_FuncCheck() > 0)
            break;
        int r = fsmc_MessageCheck();
        if (r == 2 || r == 0) {
            cp->f_exist = 1;
            cp->f_fmt = 1;
            gChkFunc.step++;
        } else if (r == 0x15 || r == 4 || r == 3) {
            if (++gChkFunc.count == 5) {
                cp->f_exist = 1;
                cp->f_fmt = 0;
                cp->snum = 0;
                cp->f_new_save = 0;
                cp->free_size = 0;
                cp->msg = 3;
                gChkFunc.step++;
                gChkFunc.count = 0;
            }
        } else if (r == 0x64) {
            cp->stat = 0;
            cp->msg = 1;
            if (gChkFunc.step == 6)
                ret = 1;
            else
                gChkFunc.step = 5;
        } else { /* includes r == 1 (asm 0x1fbfac branches to this clear block) */
            cp->type = 0;
            cp->f_exist = 0;
            cp->f_fmt = 0;
            cp->snum = 0;
            cp->f_new_save = 0;
            cp->free_size = 0;
            cp->stat = 0;
            cp->msg = 2;
            if (gChkFunc.step == 6)
                ret = 1;
            else
                gChkFunc.step = 5;
        }
        break;
    }
    case 2:
        cp->type = (char)fsmc_GetMemoryCardType(port, 0);
        if (cp->f_fmt != 0 && cp->type == 2) {
            cp->free_size = fsmc_GetFreeMemory(port, 0);
            int dt = cp->data_type;
            if (dt == 0 || dt == 1 || dt == 2) {
                if (dt == 0)
                    cp->f_new_save = (unsigned int)cp->free_size >= 0xa0; /* sltiu */
                else if (dt == 1)
                    cp->f_new_save = cp->free_size >= 0x3c;
                else
                    cp->f_new_save = cp->free_size >= 0xfe;
                gChkFunc.step++;
                cp->stat = 0;
                break;
            }
        }
        cp->stat = 0;
        if (gChkFunc.step == 7)
            ret = 1;
        else
            gChkFunc.step = 5;
        break;
    case 3: {
        int dt = cp->data_type;
        if (fsmc_GetDir(port, 0, mc_slps_tbl_chk[dt][mc_convert_f], 0, mc_maxfile_num[dt], gTblp) != 0) {
            if (++gChkFunc.count == 5) {
                cp->snum = 0;
                cp->f_new_save = 0;
                if (gChkFunc.step == 8)
                    ret = 1;
                else
                    gChkFunc.step = 5;
            }
        } else {
            gChkFunc.count = 0;
            gChkFunc.step++;
        }
        break;
    }
    case 4:
        if (fsmc_FuncCheck() > 0)
            break;
        if (fsmc_MessageCheck() == 0) {
            cp->snum = (char)fsmc_GetDirPoints();
            if (cp->snum >= 8)
                cp->f_new_save = 0;
            gChkFunc.step++;
            if (gChkFunc.step >= 10)
                ret = 1;
        } else {
            cp->snum = 0;
            cp->f_new_save = 0;
            cp->f_exist = 0;
            cp->f_fmt = 0;
            if (gChkFunc.step == 9)
                ret = 1;
            else
                gChkFunc.step = 5;
        }
        break;
    }
    return ret;
}

/* Runs one step of the queued command (gFunc.fn); a finished command (1) clears fn / step / count. */
static int Ac2_Mcf_Sync(void)
{
    PS2_FRAME(0x10);
    int r;
    switch (gFunc.fn) {
    case 0x63: r = Ac2_Mcf_WrongProc(); break;
    case 6: r = Ac2_Mcf_TitleCheckProc(); break;
    case 3: /* quirk: tests port 0's data type whatever the command's port */
        if (gCp[0]->data_type == 0 || gCp[0]->data_type == 2)
            r = Ac2_Mcf_GetListProc();
        else
            r = Ac2_Mcf_GetEmbListProc();
        break;
    case 5: r = Ac2_Mcf_LoadProc(); break;
    case 4: r = Ac2_Mcf_SaveProc(); break;
    case 2: r = Ac2_Mcf_FormatProc(); break;
    default: r = 1; break; /* 0 = idle, 1 = GetMcInfo (run by Proc's poll) */
    }
    if (r == 1) {
        gFunc.fn = 0;
        gFunc.step = 0;
        gFunc.count = 0;
    }
    return r;
}

/* Card engine tick. First the background poll (gChkFunc, armed by Start) unless a port is in error: a pending
 * GetMcInfo completes with it, and with no command pending the poll decides the return value (a finished pass
 * restarts while f_cmn is set, else (after Stop) disarms the poll and drops the work-buffer pointers).
 * Then the queued command: inMode 0 = run Sync until it is not 0 (busy loop; the IOP card advances meanwhile),
 * otherwise one Sync step. */
int Ac2_Mcf_Proc(int inMode)
{
    PS2_FRAME(0x20);
    int r;
    if (gChkFunc.fn == 1 && gCp[0]->stat != 2 && gCp[1]->stat != 2) {
        r = Ac2_Mcf_GetMcInfoProc();
        if (gFunc.fn == 1) {
            if (r != 1)
                return 0;
            gFunc.fn = 0;
            gFunc.step = 0;
            gFunc.count = 0;
            return 1;
        }
        if (gFunc.fn == 0) {
            if (r == 1) {
                if (gChkFunc.f_cmn != 0) {
                    gChkFunc.step = 0;
                } else {
                    gChkFunc.fn = 0;
                    gISp = 0;
                    gTblp = 0;
                    gFilep = 0;
                    gTmpp = 0;
                }
                return 1;
            }
            return gChkFunc.f_cmn != 0 ? 1 : 0;
        }
        if (r == 0)
            return 0;
    }
    if (inMode == 0) {
        do {
            r = Ac2_Mcf_Sync();
        } while (r == 0);
        return r;
    }
    return Ac2_Mcf_Sync();
}

/* Clears a card info (stat 1 = needs a check). */
static int Ac2_Mcf_ResetCardInfo(struct struct_87202900 *ioCardp)
{
    if (ioCardp == 0)
        return -1;
    ioCardp->datap = 0;
    ioCardp->type = 0;
    ioCardp->stat = 1;
    ioCardp->free_size = 0;
    ioCardp->f_fmt = 0;
    ioCardp->f_exist = 0;
    ioCardp->snum = 0;
    ioCardp->new_fileno = 0;
    ioCardp->f_new_save = 0;
    ioCardp->msg = 0;
    ioCardp->iconp[0] = 0;
    ioCardp->iconp[1] = 0;
    ioCardp->iconp[2] = 0;
    return 0;
}

/* Hands the fsmc library its work buffer (a pure tail call in the original: no frame). */
int Ac2_Mcf_SetWorkBuffer(void *pbuffer, unsigned int kbyte)
{
    return fsmc_SetWorkBuffer(pbuffer, kbyte);
}

/* Stops the background poll after its current pass (Proc then disarms it). */
void Ac2_Mcf_Stop(void)
{
    gChkFunc.f_cmn = 0;
}

/* Arms the engine: stores the two card infos, starts the background poll and carves ioBuff into the file table
 * (gFilep 0x220), the two directory tables (0x200 each), the icon.sys (0x3c4) and the data buffer gTmpp (0x40 for the
 * title check, else the title's game-data size), all zeroed. Returns the end of the carved area (NULL when the card
 * library is busy or an argument is NULL).
 * Quirk: the "alignment" adds the low 6 bits instead of rounding up (an unaligned buffer stays unaligned). */
void *Ac2_Mcf_Start(struct struct_87202900 *ioCp1, struct struct_87202900 *ioCp2, int inType, void *ioBuff)
{
    PS2_FRAME(0x50);
    if (fsmc_FuncCheck() > 0 || ioCp1 == 0 || ioCp2 == 0 || ioBuff == 0)
        return 0;
    unsigned char *s0 = (unsigned char *)ioBuff;
    gChkFunc.fn = 1;
    gCp[0] = ioCp1;
    gChkFunc.f_cmn = 1;
    gCp[1] = ioCp2;
    gChkFunc.step = 0;
    gChkFunc.count = 0;
    unsigned int mis = PS2PTR(s0) & 0x3f;
    if (mis != 0)
        s0 += mis;
    gTblp = (struct struct_526bec65 *)(s0 + 0x220);
    gFilep = (struct struct_db08e715 *)s0;
    gISp = (struct struct_5f0defe1 *)(s0 + 0x620);
    gTblpAc2 = (struct struct_526bec65 *)(s0 + 0x420);
    s0 += 0x9e4;
    mis = PS2PTR(s0) & 0x3f;
    if (mis != 0)
        s0 += mis;
    gTmpp = (unsigned int *)s0;
    if (inType == 2)
        s0 += 0x40;
    else
        s0 += mc_gamedata_size[mc_convert_f];

    memset(gFilep, 0, 0x220);
    memset(gTblp, 0, 0x200);
    memset(gTblpAc2, 0, 0x200);
    memset(gISp, 0, 0x3c4);
    if (inType == 2) {
        memset(gTmpp, 0, 0x40);
    } else {
        int n = mc_gamedata_size[mc_convert_f] / 4; /* word count, signed division as in the asm */
        for (int i = 0; i < n; i++)
            gTmpp[i] = 0;
    }
    for (int i = 0; i < 2; i++) {
        Ac2_Mcf_ResetCardInfo(gCp[i]);
        gCp[i]->data_type = (char)inType;
    }
    gTm2Topp = 0;
    return s0;
}

/* Selects the game title 0..5 (mc_convert_f); -1 for anything else. */
int Ac2_Mcf_SetTitle(int inTitle)
{
    if (inTitle == 0 || inTitle == 1 || inTitle == 2 || inTitle == 3 || inTitle == 4 || inTitle == 5) {
        mc_convert_f = inTitle;
        return 0;
    }
    return -1;
}

/* Initialises the card library and the engine state; gChkFunc becomes a copy of the cleared gFunc (data[], pre_fn
 * and w_* copied as they are). */
int Ac2_Mcf_Init(void)
{
    PS2_FRAME(0x10);
    if (fsmc_Init() != 0)
        return -1;
    gFunc.fn = 0;
    gFunc.step = 0;
    gCp[0] = 0;
    gCp[1] = 0;
    gFunc.count = 0;
    memcpy(&gChkFunc, &gFunc, sizeof(gFunc));
    return 0;
}
