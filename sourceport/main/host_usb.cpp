/* Platform side: the fsusb / fsums USB SDK stand-ins (emblem editor mouse, menu/mefunc.cpp Ac2_Emb_MouseUpdate). The
 * source port has no USB devices yet: GetInfo reports "no device" (non-zero), so the caller takes its reset path.
 * fsusb_UsbInit / fsusb_UsbSync are native (port/lib/fs/fslibusb.cpp). */
struct _USB_INFO_ST;

extern "C" int fsusb_UsbGetInfo(int, int, struct _USB_INFO_ST *)
{
    return -1;
}

extern "C" int fsums_UsbMouseRead(int, struct _USB_INFO_ST *)
{
    return -1;
}
