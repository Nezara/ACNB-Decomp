/* src/main/iop.cpp - IOP (I/O processor) bring-up. Iop_Init(flagInit) powers the HDD / network hardware (dev9) down
 * if a previous init loaded it, reboots the IOP from cdrom0:\IOPRP271.IMG;1, re-initialises SIF RPC / IOP heap / fileio
 * / libcdvd (twice on a full init), loads the IRX modules whose flag matches (flagInit | 0x100) & 0xff00 and starts the
 * USB driver stubs. __LoadPackIopModules reads the module pack \IOP\IRX.BIN;1 (a binder) into the Memm pack buffer,
 * copies each packed module to the IOP heap (sceSifSetDma, no completion wait) and starts it from there; entries with
 * isPack 0 (or a failed heap alloc) load from their own CD path. Called at boot (__InitPs2, 0x200) and from the menu
 * re-run paths (mmanage / mvsr). Every SDK call is a side effect on the IOP: keep their order and count.
 * Original oddities kept: Env_ConvertIrxPath (identity) called twice before the reboot loop and per CD-path module;
 * the IOP heap pointer stays stale for non-packed entries; a dependency counts as loaded only with result 0 / 2 (99 =
 * not attempted); PFS depends on itself (isLoad 0, dead); s_flagLastInit is 0 until an init succeeds. */
#include <string.h>
#include "acnb/port.h"
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
/* the module table and its strings (main ELF rodata 0x25efc0..0x25f500; the hybrid uses the originals through
 * acnb/nodwarf.h). The IOP reads `args` bytes of each argument string, more than the string for most entries (the
 * hybrid gets the ELF bytes behind it); the source port pads with zeros. */
#define IOP_PATH(n) "cdrom0:\\IOP\\" n ".IRX;1"
static char iop_path[36][32] = {
    IOP_PATH("SIO2MAN"), IOP_PATH("PADMAN"), IOP_PATH("MCMAN"), IOP_PATH("MCSERV"), IOP_PATH("CDVDSTM"),
    IOP_PATH("LIBSD"), IOP_PATH("MODMIDI"), IOP_PATH("MODHSYN"), IOP_PATH("MODSEIN"), IOP_PATH("FSSD"),
    IOP_PATH("FSSDADS"), IOP_PATH("FSSDSYTH"), IOP_PATH("SDRDRV"), IOP_PATH("FSSDEVAG"), IOP_PATH("FSSDMVAG"),
    IOP_PATH("FSMFSTRM"), IOP_PATH("FSSDIVAG"), IOP_PATH("USBD"), IOP_PATH("FSIOPUSB"), IOP_PATH("FSIOPUMS"),
    IOP_PATH("ILINK"), IOP_PATH("ILSOCK"), IOP_PATH("ILSAMPLE"), IOP_PATH("ILTRANS"), IOP_PATH("INET"),
    IOP_PATH("NETCNF"), IOP_PATH("INETCTL"), IOP_PATH("DEV9"), IOP_PATH("ATAD"), IOP_PATH("HDD"),
    IOP_PATH("PFS"), IOP_PATH("SMAP"), IOP_PATH("MSIFRPC"), IOP_PATH("LIBNET"), IOP_PATH("NETCNFIF"),
    IOP_PATH("MPINET"),
};
static char iop_arg_none[1] = "";
static char iop_arg_usbd[18] = "dev=3";
static char iop_arg_fsiopusb[20] = "usb=USB_MOUSE dev=2";
static char iop_arg_fsiopums[6] = "dev=1";
static char iop_arg_ilink[13] = "thpri=14,18";
static char iop_arg_ilsock[10] = "thpri=16";
static char iop_arg_inetctl[9] = "-no_auto";
static char iop_arg_hdd[10] = "-o";
static char iop_arg_pfs[16] = "-m";
static char iop_arg_libnet[9] = "-verbose";
static char iop_arg_netcnfif[22] = "thpri=123";
struct StModuleArg iop_module_tbl[36] = {   /* isLoad, isPack, ppathOne, flag, depend, args, parg */
    {1, 1, iop_path[0], 0x100, -1, 0, iop_arg_none},        /*  0 SIO2MAN */
    {1, 1, iop_path[1], 0x100, -1, 0, iop_arg_none},        /*  1 PADMAN */
    {1, 1, iop_path[2], 0x100, -1, 0, iop_arg_none},        /*  2 MCMAN */
    {1, 1, iop_path[3], 0x100, -1, 0, iop_arg_none},        /*  3 MCSERV */
    {1, 1, iop_path[4], 0x100, -1, 0, iop_arg_none},        /*  4 CDVDSTM */
    {1, 1, iop_path[5], 0x100, -1, 0, iop_arg_none},        /*  5 LIBSD */
    {1, 1, iop_path[6], 0x100, 5, 0, iop_arg_none},         /*  6 MODMIDI */
    {1, 1, iop_path[7], 0x100, 5, 0, iop_arg_none},         /*  7 MODHSYN */
    {1, 1, iop_path[8], 0x100, 5, 0, iop_arg_none},         /*  8 MODSEIN */
    {1, 1, iop_path[9], 0x100, 5, 0, iop_arg_none},         /*  9 FSSD */
    {1, 1, iop_path[10], 0x100, 9, 0, iop_arg_none},        /* 10 FSSDADS */
    {1, 1, iop_path[11], 0x100, 9, 0, iop_arg_none},        /* 11 FSSDSYTH */
    {1, 1, iop_path[12], 0x100, 5, 0, iop_arg_none},        /* 12 SDRDRV */
    {1, 1, iop_path[13], 0x100, 9, 0, iop_arg_none},        /* 13 FSSDEVAG */
    {0, 1, iop_path[14], 0x100, 9, 0, iop_arg_none},        /* 14 FSSDMVAG */
    {1, 0, iop_path[15], 0x200, 9, 0, iop_arg_none},        /* 15 FSMFSTRM */
    {1, 0, iop_path[16], 0x200, 15, 0, iop_arg_none},       /* 16 FSSDIVAG */
    {1, 1, iop_path[17], 0x100, -1, 18, iop_arg_usbd},      /* 17 USBD */
    {1, 1, iop_path[18], 0x100, 17, 20, iop_arg_fsiopusb},  /* 18 FSIOPUSB */
    {1, 1, iop_path[19], 0x100, 17, 6, iop_arg_fsiopums},   /* 19 FSIOPUMS */
    {1, 1, iop_path[20], 0x400, -1, 13, iop_arg_ilink},     /* 20 ILINK */
    {1, 1, iop_path[21], 0x400, 20, 10, iop_arg_ilsock},    /* 21 ILSOCK */
    {1, 1, iop_path[22], 0x400, 20, 0, iop_arg_none},       /* 22 ILSAMPLE */
    {1, 1, iop_path[23], 0x400, 20, 0, iop_arg_none},       /* 23 ILTRANS */
    {1, 1, iop_path[24], 0x1800, -1, 0, iop_arg_none},      /* 24 INET */
    {1, 1, iop_path[25], 0x1800, 24, -1, 0},                /* 25 NETCNF (default arg in __LoadModule) */
    {1, 1, iop_path[26], 0x1800, 25, 9, iop_arg_inetctl},   /* 26 INETCTL */
    {1, 0, iop_path[27], 0x100, -1, 0, iop_arg_none},       /* 27 DEV9 */
    {0, 0, iop_path[28], 0x100, 27, 0, iop_arg_none},       /* 28 ATAD */
    {0, 1, iop_path[29], 0x100, 28, 10, iop_arg_hdd},       /* 29 HDD */
    {0, 1, iop_path[30], 0x100, 30, 16, iop_arg_pfs},       /* 30 PFS (depends on itself) */
    {1, 0, iop_path[31], 0x800, 27, 0, iop_arg_none},       /* 31 SMAP */
    {1, 0, iop_path[32], 0x800, 26, 0, iop_arg_none},       /* 32 MSIFRPC */
    {1, 0, iop_path[33], 0x800, 32, 9, iop_arg_libnet},     /* 33 LIBNET */
    {1, 0, iop_path[34], 0x800, 25, 22, iop_arg_netcnfif},  /* 34 NETCNFIF */
    {1, 0, iop_path[35], 0x800, 24, 0, iop_arg_none},       /* 35 MPINET */
};
char iop_rebootimg_path[23] = "cdrom0:\\IOPRP271.IMG;1";
/* 3 NUL-separated module arguments (0x53 bytes, main 0x25f910) */
char iop_netcnf_arg[0x53] = "icon=cdrom0:\\CONF\\SYS_NET.ICO;1\0iconsys=cdrom0:\\CONF\\ICON.SYS;1\0-no_check_capacity";
char iop_irxpack_path[22] = "cdrom0:\\IOP\\IRX.BIN;1";
char iop_dev9x[7] = "dev9x:";
#endif

#define IOP_NMODULE 36
#define IOP_MOD_NETCNF 0x19   /* the only entry without an argument (args -1, parg NULL): gets iop_netcnf_arg */
#define IOP_MOD_DEV9 0x1b
#define IOP_RES_NONE 99       /* resModule: not attempted */

int Iop_GetInitFlag(void) { return s_flagLastInit; }

int Iop_IsLoadDev9(void) { return s_isLoadDev9; }

int Iop_Init(int flagInit)
{
    PS2_FRAME(0x20);
    if (s_isLoadDev9) {
        while (sceDevctl(iop_dev9x, 0x4402, 0, 0, 0, 0) < 0) {
        }
    }
    fseth_RemovePowerOff();
    s_isLoadDev9 = 0;
    s_flagLastInit = 0;
    if (!(flagInit & 1)) {
        sceSifInitRpc(0);
        sceSifInitIopHeap();
        sceSifLoadFileReset();
        sceFsReset();
        Cdvd_Init();
    }
    if (s_flagSifMInitRpc) {
        sceSifMExitRpc();
        s_flagSifMInitRpc = 0;
    }
    Env_ConvertIrxPath(iop_rebootimg_path);
    while (!sceSifRebootIop(Env_ConvertIrxPath(iop_rebootimg_path))) {
    }
    while (!sceSifSyncIop()) {
    }
    sceSifInitRpc(0);
    sceSifInitIopHeap();
    sceSifLoadFileReset();
    sceFsReset();
    Cdvd_Init();
    if (!__LoadPackIopModules((flagInit | 0x100) & 0xff00)) return 0;
    if (s_isLoadDev9) fseth_PreparePowerOff();
    if (flagInit & 0x800) {
        sceSifMInitRpc(0);
        s_flagSifMInitRpc = 1;
    }
    fsusb_UsbInit(0);
    fsusb_UsbSync(0, 0);
    fsusb_UsbInit(1);
    fsusb_UsbSync(0, 1);
    s_flagLastInit = flagInit;
    return 1;
}

/* starts module `mod` from the IOP heap copy (isPack) or its CD path; returns the module's own result word
 * (0 resident, 1 no resident, 2 removable), written by the SDK through a pointer to the original's stack slot */
static int __LoadModule(void *pbufIop, int mod, struct StModuleArg *pma)
{
    PS2_FRAME(0x60);
    PS2_STACK(int, result, 0x5c);
    int len = pma->args;
    char *arg = pma->parg;
    if (len == -1 && arg == 0) {
        if (mod != IOP_MOD_NETCNF) return 0;
        len = 0x53;
        arg = iop_netcnf_arg;
    }
    if (pma->isPack) {
        Env_ConvertIrxPath(pma->ppathOne);
        while (sceSifLoadStartModuleBuffer(pbufIop, len, arg, &result) < 0) {
        }
    } else {
        Env_ConvertIrxPath(pma->ppathOne);
        char *path = Env_ConvertIrxPath(pma->ppathOne);
        while (sceSifLoadStartModule(path, len, arg, &result) < 0) {
        }
    }
    return result;
}

static int __LoadPackIopModules(int flagLoad)
{
    PS2_FRAME(0x570);
    PS2_STACK(struct StModuleArg[IOP_NMODULE], tblModule, 0xa0);   /* modified copy (isPack fallback) */
    PS2_STACK(int[IOP_NMODULE], resModule, 0x490);
    PS2_STACK(struct struct_f14f5dcc, dma, 0x520);                  /* sceSifDmaData, read by SifSetDma */
    PS2_STACK(struct struct_9b06e75c, cdf, 0x530);                  /* sceCdlFILE, written by sceCdSearchFile */
    PS2_STACK(struct StBinderMng, bind, 0x560);

    memcpy(tblModule, iop_module_tbl, sizeof(struct StModuleArg) * IOP_NMODULE);
    for (int i = 0; i < IOP_NMODULE; i++) resModule[i] = IOP_RES_NONE;

    char *pack = (char *)Memm_GetIrxPackFileReadBuffer();
    File_SearchFile(&cdf, iop_irxpack_path);
    File_ReadN(iop_irxpack_path, (int)((cdf.size + 0x7ff) & ~0x7ffu), pack, 1);
    Binder_Init(&bind, pack, cdf.lsn);

    void *heap = 0;   /* kept across iterations (stale for CD-path entries) */
    int off = 0;
    for (int i = 0; i < IOP_NMODULE; i++) {
        struct StModuleArg *pma = &tblModule[i];
        if (!pma->isLoad || !(flagLoad & pma->flag)) continue;
        if (pma->depend != -1) {
            int d = resModule[pma->depend];
            if (d != 0 && d != 2) continue;
        }
        int ok = 0;
        if (pma->isPack) {
            off = Binder_GetOffset(&bind, i);
            heap = sceSifAllocIopHeap((bind.file_infop[i].file_size + 0xf) & ~0xfu);
            ok = heap != 0;
        }
        if (!ok) pma->isPack = 0;
        if (ok) {
            dma.data = PS2PTR(pack + off);
            dma.addr = PS2PTR(heap);
            dma.size = bind.file_infop[i].file_size;
            dma.mode = 0;
            sceSifSetDma(&dma, 1);
        }
        resModule[i] = __LoadModule(heap, i, pma);
        if (ok) sceSifFreeIopHeap(heap);
        if ((resModule[i] == 0 || resModule[i] == 2) && i == IOP_MOD_DEV9) s_isLoadDev9 = 1;
    }
    Memm_ReleaseIrxPackFileReadBuffer();
    return 1;
}
