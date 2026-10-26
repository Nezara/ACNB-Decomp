/* lib/overlay.cpp - loads an overlay image (OLMENU.BIN / OLSORTIE.BIN) with the fileio SDK into the overlay area and
 * clears the overlay's .bss (the image header's word at +0x14 = bss size; the bss follows the bytes read). On the PS2
 * every load re-initialises that overlay's .data and zeroes its .bss.
 * Hybrid: the image really lands in PS2 memory (pAddress = _overlay_group_addresses[1]). Source port: all overlays'
 * code is linked in and there is no overlay area: the same SDK calls read the image into a scratch buffer (same disc
 * trace), then the overlay's globals get their post-load state from port/platform ovlreg (initial-value snapshot of
 * the overlay's .data range restored, its .bss range zeroed; pAddress unused). */
#include "acnb/port.h"
#include "acnb/sdk.h"
#ifdef ACNB_SOURCEPORT
#include <stdlib.h>
#include "../../platform/ovlreg.h"
#endif

/* returns 1 when sceRead returned > 0 bytes (a short read counts as loaded, the bss clear then starts after the bytes
 * read); a failed read returns 0 with nothing restored (original) */
#ifdef ACNB_SOURCEPORT
int Overlay_Load(char *pFilePath, void *pAddress)
{
    PS2_FRAME(0x50);   /* no-op in the source port; keeps framecheck on the first definition */
    (void)pAddress;
    int ok = 0;
    int size = 0;
    int fd = sceOpen(pFilePath, 1);
    if (fd >= 0) {
        size = sceLseek(fd, 0, 2);
        sceLseek(fd, 0, 0);
    }
    if (size > 0) {
        void *img = malloc((size_t)size);
        size = img ? sceRead(fd, img, size) : 0;
        free(img);
    }
    if (fd >= 0) sceClose(fd);
    if (size > 0) {
        ovl_restore_path(pFilePath);
        ok = 1;
    }
    return ok;
}
#else
int Overlay_Load(char *pFilePath, void *pAddress)
{
    PS2_FRAME(0x50);
    int ok = 0;
    int size = 0;
    int fd = sceOpen(pFilePath, 1);
    if (fd >= 0) {
        size = sceLseek(fd, 0, 2);
        sceLseek(fd, 0, 0);
    }
    if (size > 0) size = sceRead(fd, pAddress, size);
    if (fd >= 0) sceClose(fd);
    if (size > 0) {
        unsigned int bss = *(unsigned int *)((char *)pAddress + 0x14);
        FlushCache(2);
        if (bss) ps2_memset((char *)pAddress + size, 0, bss);
        ok = 1;
    }
    return ok;
}
#endif
