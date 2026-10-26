/* lib/fs/fslibusb.cpp - EE client of the USB IOP modules (lib/fs/fslibusb.c, whole file): one SIF RPC client per
 * device (cdUsb[device], server ids s_mode[] = 0x1020 / 0x1021 / 0x1022). Iop_Init binds devices 0 and 1 at boot and
 * syncs each; fsusb_UsbGetInfo (rpc 1, 16-byte request {port, dev} in s_senddata, 0x40-byte reply) is unreached in
 * Versus. Original quirks kept: UsbInit retries the bind until the client's server pointer is set (no delay, no
 * limit); UsbSync mode 0 spins on sceSifCheckStatRpc, mode 1 returns it, any other mode returns 0.
 * Hybrid: UsbInit writes its prologue's s0-s2 save slots (PS2_SAVED_GPR), a later boot function reads one stale.
 * Source port: lib/sdk/libsif binds unknown servers anyway (non-zero server pointer), so the bind loop ends at once. */
#include "acnb/port.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
int s_mode[3] = {0x1020, 0x1021, 0x1022};
#endif

/* 0x14a6a0 */
int fsusb_UsbSync(int mode, int device)
{
    PS2_FRAME(0x20);
    switch (mode) {
    case 0:
        while (sceSifCheckStatRpc(&cdUsb[device]))
            ;
        return 0;
    case 1:
        return sceSifCheckStatRpc(&cdUsb[device]);
    }
    return 0;
}

/* 0x14a760. Source port: sourceport/main/host_usb.cpp stands in (no USB devices: returns -1, the caller resets the
 * mouse); libsif would answer the unknown server with a zeroed reply = device 0 present. */
#ifndef ACNB_SOURCEPORT
int fsusb_UsbGetInfo(int port_no, int dev_no, struct _USB_INFO_ST *info)
{
    PS2_FRAME(0x20);
    s_senddata[0] = port_no;
    s_senddata[1] = dev_no;
    int ret = sceSifCallRpc(&cdUsb[0], 1, 1, s_senddata, 0x10, info, 0x40, 0, 0);   /* mode 1 = no wait */
    if (ret) return ret;
    return 0;
}
#endif

/* 0x14a7d0 */
int fsusb_UsbInit(int device)
{
    PS2_FRAME(0x40);
    /* the prologue's sq s0 / s1 / s2: a later boot callee reads one of these slots stale (hybrid hrcheck garage6 rq frames
     * 2-3 differ without them, the ra slot alone does not fix it) */
    PS2_SAVED_GPR(16, 0x0);
    PS2_SAVED_GPR(17, 0x10);
    PS2_SAVED_GPR(18, 0x20);
    struct _sif_client_data *cd = &cdUsb[device];
    do {
        if (sceSifBindRpc(cd, s_mode[device], 0) < 0) return -1;
    } while (cd->serve == 0);
    return 0;
}
