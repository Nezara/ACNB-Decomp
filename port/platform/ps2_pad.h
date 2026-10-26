/* PS2 libpad semantics (scePad*) on host pointers (phase 3 step 3 P3), fed by a per-port PadFrame. Generic: no ACNB
 * addresses, no emulator headers (Last Raven reusable). Frozen to the hybrid's libpad HLE as of s93 (runtime/pad.c
 * before P3), byte for byte and call for call - the libpad warm-up is replay state:
 *   - port 0 is an always-connected analog DualShock 2: GetState 6 (Stable), GetReqState 0 (Complete), InfoMode
 *     CurID 7 / CurExID 0 / CurExOffs 0 / IdTable(-1) 2 (0 -> 4, else 7), InfoAct(-1) 2; fspad then reports mode 0xff
 *     for its first 2 PadUpdates and gets data on the 6th;
 *   - port 1 is empty (GetState 0, Read -> d[0] 0xff, 0 bytes); ports >= 2 likewise. P4b may connect port 1 with
 *     ps2pad_set_connected (that changes its warm-up = sim state of 2-pad replays only);
 *   - Init / PortOpen / SetMainMode / InfoPressMode / SetActAlign / SetActDirect / Enter / ExitPressMode return 1;
 *     SetActDirect / SetActAlign keep the 6 bytes (ps2pad_get_act, for rumble) without changing any result.
 *   - Read: 32 bytes, d[0] 0, d[1] 0x73, d[2..3] = ~buttons big-endian, d[4..7] = rx ry lx ly, returns 8; after
 *     EnterPressMode d[1] 0x79 + 12 pressure bytes (0xff / 0 derived from the buttons), returns 20 (never reached).
 * Every call writes its ACNB_PAD_TRACE line (platform/plat.h) here; `ta` = the caller's game-visible address, printed
 * instead of the host pointer (the hybrid passes the guest address). ps2pad_latch writes the "L1" line for port 0
 * ("L1 <buttons> <lx> <ly> <rx> <ry>", first call + every change) and "L1 p2 <connected 0|1> <buttons> <lx> <ly> <rx>
 * <ry>" for port 1 (on a change of the connection or the frame, starting from not connected + neutral: nothing is
 * logged while port 1 stays empty). Hosts call ps2pad_set_connected(1, ..) before ps2pad_latch(1, ..). */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define PS2PAD_PORTS 2

/* one layer-1 sample: buttons active-HIGH in libpad bit order (L2 0, R2 1, L1 2, R1 3, TRI 4, CIR 5, CRO 6, SQU 7,
 * SEL 8, L3 9, R3 10, START 11, UP 12, RIGHT 13, DOWN 14, LEFT 15); sticks 0..255, centre 0x80 */
typedef struct PadFrame { uint16_t buttons; uint8_t lx, ly, rx, ry; } PadFrame;

/* back to the power-on state (neutral frames, port 0 connected / port 1 empty, no pressure mode, act bytes 0, the
 * L1 trace's change detector cleared); the static initial state is the same */
void ps2pad_reset(void);
/* port 0 always stays connected (hard rule); only port 1 may change (P4b). Port 1's connection changes GetState /
 * InfoMode / InfoAct / Read results = sim state: hosts take it from replay data while a replay drives the pad. */
void ps2pad_set_connected(uint32_t port, int on);
/* the frame every later Read of that port sees (latched once per vblank, after all sources are merged) */
void ps2pad_latch(uint32_t port, const PadFrame *f);
/* last 6 bytes received by SetActDirect / SetActAlign for a port (either may be 0); 0 = bad port */
int ps2pad_get_act(uint32_t port, uint8_t direct[6], uint8_t align[6]);

/* ---- libpad entry points (the scePad* of the same name) */
int ps2pad_init(uint32_t mode);
int ps2pad_port_open(uint32_t port, uint32_t slot, uint32_t ta);
int ps2pad_get_state(uint32_t port, uint32_t slot);
int ps2pad_get_req_state(uint32_t port, uint32_t slot);
int ps2pad_info_mode(uint32_t port, uint32_t slot, int32_t term, int32_t offs);
int ps2pad_info_act(uint32_t port, uint32_t slot, int32_t act, int32_t term);
int ps2pad_info_press_mode(uint32_t port, uint32_t slot);
int ps2pad_set_main_mode(uint32_t port, uint32_t slot, int32_t offs, int32_t lock);
int ps2pad_enter_press_mode(uint32_t port, uint32_t slot);
int ps2pad_exit_press_mode(uint32_t port, uint32_t slot);
int ps2pad_set_act_align(uint32_t port, uint32_t slot, const uint8_t *data, uint32_t ta);    /* data may be 0 */
int ps2pad_set_act_direct(uint32_t port, uint32_t slot, const uint8_t *data, uint32_t ta);   /* data may be 0 */
int ps2pad_read(uint32_t port, uint32_t slot, uint8_t *buf32, uint32_t ta);                  /* buf32 may be 0 */

#ifdef __cplusplus
}
#endif
