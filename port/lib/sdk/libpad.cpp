/* lib/sdk/libpad.cpp - libpad (scePad*) for the source port: thin shims over port/platform/ps2_pad (the libpad
 * semantics frozen to the hybrid HLE: always-connected analog pad on port 0, port 1 empty; phase 3 step 3 P3). The
 * hybrid calls the original SDK code through runtime/bridge_sdk.c thunks instead (HLE hooks in runtime/pad.c over the
 * same ps2_pad), so its ACNB_PAD_TRACE lines stay those of the original call path; nothing here is built there. */
#include "acnb/port.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
#include <stdint.h>
#include "../../platform/ps2_pad.h"

/* trace address of a host buffer (the hybrid prints the guest address) */
static uint32_t pad_trace_addr(const void *p) { return (uint32_t)(uintptr_t)p; }

int scePadInit(int mode) { return ps2pad_init((uint32_t)mode); }
int scePadPortOpen(int port, int slot, void *addr) { return ps2pad_port_open((uint32_t)port, (uint32_t)slot, pad_trace_addr(addr)); }
int scePadGetState(int port, int slot) { return ps2pad_get_state((uint32_t)port, (uint32_t)slot); }
int scePadGetReqState(int port, int slot) { return ps2pad_get_req_state((uint32_t)port, (uint32_t)slot); }
int scePadInfoMode(int port, int slot, int term, int offs) { return ps2pad_info_mode((uint32_t)port, (uint32_t)slot, term, offs); }
int scePadInfoAct(int port, int slot, int actno, int term) { return ps2pad_info_act((uint32_t)port, (uint32_t)slot, actno, term); }
int scePadInfoPressMode(int port, int slot) { return ps2pad_info_press_mode((uint32_t)port, (uint32_t)slot); }
int scePadSetMainMode(int port, int slot, int offs, int lock) { return ps2pad_set_main_mode((uint32_t)port, (uint32_t)slot, offs, lock); }
int scePadEnterPressMode(int port, int slot) { return ps2pad_enter_press_mode((uint32_t)port, (uint32_t)slot); }
int scePadExitPressMode(int port, int slot) { return ps2pad_exit_press_mode((uint32_t)port, (uint32_t)slot); }
int scePadSetActAlign(int port, int slot, const unsigned char *data)
{
    return ps2pad_set_act_align((uint32_t)port, (uint32_t)slot, data, pad_trace_addr(data));
}
int scePadRead(int port, int slot, unsigned char *rdata)
{
    return ps2pad_read((uint32_t)port, (uint32_t)slot, rdata, pad_trace_addr(rdata));
}
int scePadSetActDirect(int port, int slot, const unsigned char *data)
{
    return ps2pad_set_act_direct((uint32_t)port, (uint32_t)slot, data, pad_trace_addr(data));
}
#endif
