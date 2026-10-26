/* src/link/lnkfunc.cpp - game-side link helpers. Native: DF_SyncLink, the per-render-frame lock-step of a linked
 * battle (KeySyncProc): run the link, and when the sync layer lets this console submit, send both local pads (compact
 * data, digital mapped onto the analog sticks); when the next frame of every console is complete, fetch it into
 * ilinkdata (4 consoles x 2 pads). DF_ILinkInit (single-player i.LINK co-op setup, out of scope) stays interpreted.
 * Returns 1 = a frame was fetched (the sim advances), 0 = render only, -1 = link / sync error or timeout,
 * -2 = disconnected (LinkProc -0x10).
 * Quirk (kept): any LinkProc result other than 3 aborts, and the sync timeout bit (0x20) overrides a fetched frame. */
#include "acnb/sdk.h"
#include <string.h>

int DF_SyncLink(void)
{
    PS2_FRAME(0x30);
    int ret = 0;
    int link_stat = fslnk_LinkProc();
    if (link_stat < 0) ret = -1;   /* dead: every non-3 result is decided below */
    if (link_stat == 3) {
        int sync_stat = fslnk_LinkSyncProc();
        if (sync_stat & 4) {
            Pad_GetCompactData(0, (struct StCompactPadData *)&ilinksend.pad[0]);
            Pad_GetCompactData(1, (struct StCompactPadData *)&ilinksend.pad[1]);
            Pad_AssignDigitalToAnalog(0, (short *)&ilinksend.pad[0]);
            Pad_AssignDigitalToAnalog(1, (short *)&ilinksend.pad[1]);
            fslnk_LinkSyncSetData(&ilinksend);
        }
        if (sync_stat & 8) {
            memset(&ilinkdata, 0, 0x20);
            fslnk_LinkSyncGetData(&ilinkdata);
            ret = 1;
        }
        if (sync_stat & 0x20) ret = -1;
    } else {
        ret = link_stat == -0x10 ? -2 : -1;
    }
    return ret;
}
