/* PS2 libpad semantics over PadFrame (phase 3 step 3 P3; contract in ps2_pad.h). Moved from the hybrid HLE
 * (runtime/pad.c) unchanged: same results, same trace lines. */
#include "ps2_pad.h"
#include "plat.h"
#include <string.h>

struct PadPort {
    PadFrame f;
    uint8_t pressure[12];   /* derived from f.buttons at latch */
    int connected, pressure_mode;
    uint8_t act_direct[6], act_align[6];
};

static PadPort pads[PS2PAD_PORTS] = {
    {{0, 0x80, 0x80, 0x80, 0x80}, {0}, 1, 0, {0}, {0}},
    {{0, 0x80, 0x80, 0x80, 0x80}, {0}, 0, 0, {0}, {0}},
};
static int l1_have;
static PadFrame l1_last;
static int p2_conn;         /* port-1 trace change detector: starts at (not connected, neutral) = nothing logged */
static PadFrame p2_last = {0, 0x80, 0x80, 0x80, 0x80};

#define TR(...) do { if (pad_trace_on) pad_trace("PAD", __VA_ARGS__); } while (0)

static PadPort *live(uint32_t port) { return port < PS2PAD_PORTS && pads[port].connected ? &pads[port] : 0; }

extern "C" void ps2pad_reset(void)
{
    for (int i = 0; i < PS2PAD_PORTS; i++) {
        PadPort *p = &pads[i];
        memset(p, 0, sizeof *p);
        p->f.lx = p->f.ly = p->f.rx = p->f.ry = 0x80;
        p->connected = i == 0;
    }
    l1_have = 0;
    memset(&l1_last, 0, sizeof l1_last);
    p2_conn = 0;
    p2_last.buttons = 0;
    p2_last.lx = p2_last.ly = p2_last.rx = p2_last.ry = 0x80;
}

extern "C" void ps2pad_set_connected(uint32_t port, int on)
{
    if (port == 1) pads[1].connected = on != 0;
}

extern "C" void ps2pad_latch(uint32_t port, const PadFrame *f)
{
    if (port >= PS2PAD_PORTS || !f) return;
    PadPort *p = &pads[port];
    p->f = *f;
    /* pressure order RIGHT LEFT UP DOWN TRI CIR CRO SQU L1 R1 L2 R2 (button bit numbers) */
    static const int pmap[12] = {13, 15, 12, 14, 4, 5, 6, 7, 2, 3, 0, 1};
    for (int i = 0; i < 12; i++) p->pressure[i] = (f->buttons >> pmap[i]) & 1 ? 0xff : 0;
    if (!pad_trace_on || !pad_trace_enabled()) return;
    if (port == 1) {   /* "L1 p2 <connected> <frame>": on any change of (connected, frame) from (0, neutral), so a run
                        * whose port 1 is never connected / always neutral logs nothing (1-pad baselines unchanged) */
        int c = p->connected;
        if (c == p2_conn && f->buttons == p2_last.buttons && f->lx == p2_last.lx && f->ly == p2_last.ly &&
            f->rx == p2_last.rx && f->ry == p2_last.ry)
            return;
        p2_conn = c;
        p2_last = *f;
        pad_trace("L1", "p2 %d %04x %02x %02x %02x %02x", c, f->buttons, f->lx, f->ly, f->rx, f->ry);
        return;
    }
    /* layer-1 trace, port 0: first call always logs, then on any change (like the replay recorder) */
    if (port != 0) return;
    if (l1_have && f->buttons == l1_last.buttons && f->lx == l1_last.lx && f->ly == l1_last.ly && f->rx == l1_last.rx &&
        f->ry == l1_last.ry)
        return;
    l1_have = 1;
    l1_last = *f;
    pad_trace("L1", "%04x %02x %02x %02x %02x", f->buttons, f->lx, f->ly, f->rx, f->ry);
}

extern "C" int ps2pad_get_act(uint32_t port, uint8_t direct[6], uint8_t align[6])
{
    if (port >= PS2PAD_PORTS) return 0;
    if (direct) memcpy(direct, pads[port].act_direct, 6);
    if (align) memcpy(align, pads[port].act_align, 6);
    return 1;
}

extern "C" int ps2pad_init(uint32_t mode) { TR("Init mode=%u -> 1", mode); return 1; }

extern "C" int ps2pad_port_open(uint32_t port, uint32_t slot, uint32_t ta)
{
    TR("PortOpen port=%u slot=%u addr=%08x -> 1", port, slot, ta);
    return 1;
}

extern "C" int ps2pad_set_main_mode(uint32_t port, uint32_t slot, int32_t offs, int32_t lock)
{
    TR("SetMainMode port=%u slot=%u offs=%d lock=%d -> 1", port, slot, offs, lock);
    return 1;
}

extern "C" int ps2pad_info_press_mode(uint32_t port, uint32_t slot)
{
    TR("InfoPressMode port=%u slot=%u -> 1", port, slot);
    return 1;
}

static int act_store(const char *api, uint32_t port, uint32_t slot, const uint8_t *a, uint32_t ta, int direct)
{
    if (a && port < PS2PAD_PORTS) memcpy(direct ? pads[port].act_direct : pads[port].act_align, a, 6);
    if (!pad_trace_on || !pad_trace_enabled()) return 1;
    if (a) pad_trace("PAD", "%s port=%u slot=%u data=%08x -> 1 act %02x%02x%02x%02x%02x%02x crc %08x", api, port, slot,
                     ta, a[0], a[1], a[2], a[3], a[4], a[5], disc_crc32(a, 6));
    else pad_trace("PAD", "%s port=%u slot=%u data=%08x -> 1", api, port, slot, ta);
    return 1;
}

extern "C" int ps2pad_set_act_direct(uint32_t port, uint32_t slot, const uint8_t *data, uint32_t ta)
{
    return act_store("SetActDirect", port, slot, data, ta, 1);
}

extern "C" int ps2pad_set_act_align(uint32_t port, uint32_t slot, const uint8_t *data, uint32_t ta)
{
    return act_store("SetActAlign", port, slot, data, ta, 0);
}

extern "C" int ps2pad_get_state(uint32_t port, uint32_t slot)   /* Stable / Disconnected */
{
    int r = live(port) ? 6 : 0;
    TR("GetState port=%u slot=%u -> %d", port, slot, r);
    return r;
}

extern "C" int ps2pad_get_req_state(uint32_t port, uint32_t slot)   /* Complete */
{
    TR("GetReqState port=%u slot=%u -> 0", port, slot);
    return 0;
}

extern "C" int ps2pad_read(uint32_t port, uint32_t slot, uint8_t *d, uint32_t ta)
{
    int r;
    if (!d) { TR("Read port=%u slot=%u buf=%08x -> 0", port, slot, ta); return 0; }
    memset(d, 0, 32);
    PadPort *p = live(port);
    if (!p) { d[0] = 0xff; r = 0; }
    else {
        uint16_t b = (uint16_t)~p->f.buttons;
        d[0] = 0;
        d[1] = p->pressure_mode ? 0x79 : 0x73;
        d[2] = (uint8_t)(b >> 8);   /* SELECT L3 R3 START UP RIGHT DOWN LEFT (bit 0 = SELECT) */
        d[3] = (uint8_t)b;          /* L2 R2 L1 R1 TRIANGLE CIRCLE CROSS SQUARE (bit 0 = L2) */
        d[4] = p->f.rx; d[5] = p->f.ry; d[6] = p->f.lx; d[7] = p->f.ly;
        if (p->pressure_mode) { memcpy(d + 8, p->pressure, 12); r = 20; }
        else r = 8;
    }
    TR("Read port=%u slot=%u buf=%08x -> %d crc %08x", port, slot, ta, r, disc_crc32(d, 32));
    return r;
}

extern "C" int ps2pad_info_mode(uint32_t port, uint32_t slot, int32_t term, int32_t offs)
{
    int r;
    if (!live(port)) r = 0;
    else switch (term) {
    case 1: r = 7; break;                                   /* InfoModeCurID: analog */
    case 2: r = 0; break;                                   /* CurExID */
    case 3: r = 0; break;                                   /* CurExOffs */
    case 4: r = offs == -1 ? 2 : offs == 0 ? 4 : 7; break;  /* IdTable */
    default: r = 0; break;
    }
    TR("InfoMode port=%u slot=%u term=%d offs=%d -> %d", port, slot, term, offs, r);
    return r;
}

extern "C" int ps2pad_info_act(uint32_t port, uint32_t slot, int32_t act, int32_t term)
{
    static const int tab[2][4] = {{0, 1, 0, 1}, {1, 1, 0, 1}};   /* func, sub, size, current (roughly) */
    int r;
    if (!live(port)) r = 0;
    else if (act < 0) r = 2;
    else r = act < 2 && term >= 0 && term < 4 ? tab[act][term] : 0;   /* the HLE had no term >= 0 guard (never hit) */
    TR("InfoAct port=%u slot=%u act=%d term=%d -> %d", port, slot, act, term, r);
    return r;
}

/* the flag is per port & 1 (as the HLE), read by Read only for a connected port */
extern "C" int ps2pad_enter_press_mode(uint32_t port, uint32_t slot)
{
    pads[port & 1].pressure_mode = 1;
    TR("EnterPressMode port=%u slot=%u -> 1", port, slot);
    return 1;
}

extern "C" int ps2pad_exit_press_mode(uint32_t port, uint32_t slot)
{
    pads[port & 1].pressure_mode = 0;
    TR("ExitPressMode port=%u slot=%u -> 1", port, slot);
    return 1;
}
