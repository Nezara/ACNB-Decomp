/* lib/fs/fslibpa2.cpp - the game's pad manager over libpad (whole file).
 * __PADMNG holds 8 slot records (index = port * 4 + slot) plus open_slot[2] (bit mask of opened slots).
 * fspad_PadUpdate runs once per frame: for every open slot it polls scePadGetState / GetReqState, steps the
 * libpad-sample phase machine (detect id -> digital / analog (/ pressure) mode -> actuator alignment) and, in a
 * stable phase, reads the pad into pdata, inverts the (active-low) button word and derives edge / repeat.
 * The SDK libpad calls are acnb/sdk.h thunks (hybrid: HLE hooks of runtime/pad.c). Their order per frame is replay
 * state (the 5-call warm-up of a fresh slot), so the call sequence follows the asm exactly.
 * Quirks kept: the 36-byte pdata copy reads 4 bytes past scePadRead's 32-byte buffer (overwritten right after);
 * phase 0xc8 fills align[] act_no times with no bound; modechg == 2 (pressure) has no writer, so phases
 * 0x78-0x83 / 0xff are dead; PadInit leaves state / type / act_data / pdata / key counters as they were. */
#include <string.h>
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/local/lib/fs/fslibpa2.h"

static inline struct struct_dedbe94a *fspad_slot(int port, int slot) { return &__PADMNG.sdata[slot + port * 4]; }

int fspad_PadGetCurrentMode(int port, int slot)
{
    return fspad_slot(port, slot)->mode;
}

int fspad_PadModeAnalog(int port, int slot, int lock)
{
    struct struct_dedbe94a *sd = fspad_slot(port, slot);
    if (!(sd->type & 1))
        return 0;
    sd->modechg = 1;
    sd->modelock = (unsigned char)lock;
    return 1;
}

void fspad_PadSetVibra(int port, int slot, unsigned char pow)
{
    struct struct_dedbe94a *sd = fspad_slot(port, slot);
    sd->act_data[0] = (pow > 0) ? 1 : 0;
    sd->act_data[1] = pow;
}

void fspad_PadSetRepeat(int port, int slot, unsigned short key, unsigned short delay, unsigned short interval)
{
    struct struct_dedbe94a *sd = fspad_slot(port, slot);
    int i;
    sd->repeat_state.repeat_key |= key;
    for (i = 0; i < 16; i++) {
        if (key & (1 << i)) {
            struct struct_257fbc9f *k = &sd->repeat_state.key[i];
            k->delay = (unsigned char)delay;
            k->interval = (unsigned char)interval;
            k->frame_ct = 0;
            k->flag = 0;
        }
    }
}

struct struct_af668abd *fspad_PadRead(int port, int slot)
{
    return &fspad_slot(port, slot)->pdata;
}

/* phase numbers (libpad sample stages) */
enum {
    FSPAD_DETECT = 0, FSPAD_ID_DIGITAL = 0x28, FSPAD_ID_ANALOG = 0x46,
    FSPAD_SET_DIGITAL = 0x64, FSPAD_WAIT_DIGITAL = 0x65, FSPAD_SET_ANALOG = 0x6e, FSPAD_WAIT_ANALOG = 0x6f,
    FSPAD_ENTER_PRESS = 0x78, FSPAD_WAIT_ENTER = 0x79, FSPAD_EXIT_PRESS = 0x82, FSPAD_WAIT_EXIT = 0x83,
    FSPAD_SET_ALIGN = 0xc8, FSPAD_WAIT_ALIGN = 0xc9,
    FSPAD_DIGITAL = 0xfd, FSPAD_ANALOG = 0xfe, FSPAD_PRESSURE = 0xff
};

/* the stable phases share this: realign the actuators once, else read. 1 = go on to the read. */
static int fspad_stable(struct struct_dedbe94a *sd)
{
    if (!sd->sent_align && (sd->type & 2)) {
        sd->ret_phase = sd->phase;
        sd->phase = FSPAD_SET_ALIGN;
        return 0;
    }
    return 1;
}

void fspad_PadUpdate(void)
{
    PS2_FRAME(0xc0);
    PS2_STACK(unsigned char[0x24], buf, 0x90);   /* scePadRead's 32 bytes + the 4 the pdata copy reads past them */
    PS2_STACK(unsigned char[6], align, 0xb8);
    int port, slot, i;

    for (port = 0; port < 2; port++) {
        for (slot = 0; slot < 4; slot++) {
            struct struct_dedbe94a *sd;
            int read = 0;
            if (!(__PADMNG.open_slot[port] & (1 << slot)))
                continue;
            sd = fspad_slot(port, slot);
            sd->state = scePadGetState(port, slot);
            if (sd->state == 0)
                sd->phase = FSPAD_DETECT;
            sd->req_state = scePadGetReqState(port, slot);

            switch (sd->phase) {
            case FSPAD_DETECT: {
                int id, ex;
                sd->mode = 0xff;
                if (sd->state == 0)
                    break;
                id = scePadInfoMode(port, slot, 1, 0);
                if (id == 0)
                    break;
                ex = scePadInfoMode(port, slot, 2, 0);
                if (ex > 0)
                    id = ex;
                sd->type = 0;
                sd->sent_align = 0;
                if (id == 7)
                    sd->phase = FSPAD_ID_ANALOG;
                else if (id == 4)
                    sd->phase = FSPAD_ID_DIGITAL;
                break;
            }
            case FSPAD_ID_DIGITAL:
            case FSPAD_ID_ANALOG:
                if (sd->phase == FSPAD_ID_DIGITAL) {
                    if (scePadInfoMode(port, slot, 4, -1) != 0)
                        sd->type |= 1;
                } else {
                    sd->type |= 1;
                }
                sd->act_no = scePadInfoAct(port, slot, -1, 0);
                if (sd->act_no != 0)
                    sd->type |= 2;
                if (scePadInfoPressMode(port, slot) == 1)
                    sd->type |= 4;
                if ((sd->type & 1) && (sd->modechg == 1 || sd->modechg == 2)) {
                    sd->phase = FSPAD_SET_ANALOG;
                    break;
                }
                if (sd->modechg == 0) {
                    sd->phase = FSPAD_SET_DIGITAL;
                    break;
                }
                sd->phase = (sd->phase == FSPAD_ID_DIGITAL) ? FSPAD_DIGITAL : FSPAD_ANALOG;
                break;
            case FSPAD_SET_DIGITAL:
                if (scePadSetMainMode(port, slot, 0, sd->modelock) == 1)
                    sd->phase = FSPAD_WAIT_DIGITAL;
                break;
            case FSPAD_WAIT_DIGITAL:
                if (sd->req_state == 1)
                    sd->phase = FSPAD_SET_DIGITAL;
                if (sd->req_state == 0 && (sd->state == 6 || sd->state == 2)) {
                    sd->sent_align = 0;
                    sd->phase = FSPAD_DIGITAL;
                }
                break;
            case FSPAD_SET_ANALOG:
                if (scePadSetMainMode(port, slot, 1, sd->modelock) == 1)
                    sd->phase = FSPAD_WAIT_ANALOG;
                break;
            case FSPAD_WAIT_ANALOG:
                if (sd->req_state == 1)
                    sd->phase = FSPAD_SET_ANALOG;
                if (sd->req_state == 0 && (sd->state == 6 || sd->state == 2)) {
                    sd->sent_align = 0;
                    if (scePadInfoPressMode(port, slot) == 1)
                        sd->type |= 4;
                    sd->phase = FSPAD_ANALOG;
                }
                break;
            case FSPAD_ENTER_PRESS:
                if (scePadEnterPressMode(port, slot) == 1)
                    sd->phase = FSPAD_WAIT_ENTER;
                break;
            case FSPAD_WAIT_ENTER:
                if (sd->req_state == 1)
                    sd->phase = FSPAD_ENTER_PRESS;
                if (sd->req_state == 0 && (sd->state == 6 || sd->state == 2))
                    sd->phase = FSPAD_PRESSURE;
                break;
            case FSPAD_EXIT_PRESS:
                if (scePadExitPressMode(port, slot) == 1)
                    sd->phase = FSPAD_WAIT_EXIT;
                break;
            case FSPAD_WAIT_EXIT:
                if (sd->req_state == 1)
                    sd->phase = FSPAD_EXIT_PRESS;
                if (sd->req_state == 0 && (sd->state == 6 || sd->state == 2))
                    sd->phase = FSPAD_ANALOG;
                break;
            case FSPAD_SET_ALIGN:
                for (i = 0; i < 6; i++) {
                    align[i] = 0xff;
                    sd->act_data[i] = 0;
                }
                for (i = 0; i < sd->act_no; i++)
                    align[i] = (unsigned char)i;   /* no bound against align[6] (act_no is 2) */
                if (scePadSetActAlign(port, slot, align) == 1)
                    sd->phase++;
                break;
            case FSPAD_WAIT_ALIGN:
                if (sd->req_state == 1)
                    sd->phase = FSPAD_SET_ALIGN;
                if (sd->req_state == 0) {
                    sd->sent_align = 1;
                    sd->phase = sd->ret_phase;
                }
                break;
            case FSPAD_PRESSURE:
                if (sd->modechg == 2)
                    sd->modechg = 0xff;
                if (sd->modechg == 0 || sd->modechg == 1) {
                    sd->mode = 0xff;
                    sd->phase = FSPAD_EXIT_PRESS;
                    break;
                }
                sd->mode = 2;
                read = fspad_stable(sd);
                break;
            case FSPAD_ANALOG:
                if (sd->modechg == 1)
                    sd->modechg = 0xff;
                if (sd->modechg == 0) {
                    sd->mode = 0xff;
                    sd->phase = FSPAD_DETECT;
                    break;
                }
                if (sd->modechg == 2) {
                    sd->mode = 0xff;
                    sd->phase = FSPAD_ENTER_PRESS;
                    break;
                }
                sd->mode = 1;
                read = fspad_stable(sd);
                break;
            case FSPAD_DIGITAL:
                if (sd->modechg == 0)
                    sd->modechg = 0xff;
                if (sd->modechg == 1 || sd->modechg == 2) {
                    sd->mode = 0xff;
                    sd->phase = FSPAD_DETECT;
                    break;
                }
                sd->mode = 0;
                read = fspad_stable(sd);
                break;
            default:
                break;
            }
            if (!read)
                continue;

            {
                unsigned short old = sd->pdata.button.set;
                unsigned char old_term = sd->term_id;
                int v = 0;
                if (scePadRead(port, slot, buf) != 0) {
                    if (buf[0] == 0) {
                        sd->term_id = buf[1] >> 4;
                        if (old_term != 0 && sd->term_id != old_term) {
                            v = -1;
                        } else {
                            memcpy(&sd->pdata, buf, 0x24);   /* 9 x 2 halfwords: reads buf[0x20..0x23] stale */
                            sd->pdata.button.set = (unsigned short)~sd->pdata.button.set;
                            v = 1;
                        }
                    } else {
                        v = 1;
                    }
                }
                if (v == 0)
                    continue;
                if (v == -1)
                    sd->phase = FSPAD_DETECT;
                sd->pdata.edge.set = sd->pdata.button.set & (unsigned short)~old;
                _fspad_SetRepeatData(sd);
                if ((sd->type & 2) && sd->state == 6)
                    scePadSetActDirect(port, slot, sd->act_data);
            }
        }
    }
}

int fspad_PadPortOpen(int port, int slot)
{
    __PADMNG.open_slot[port] |= (unsigned char)(1 << slot);
    return scePadPortOpen(port, slot, fspad_slot(port, slot));
}

int fspad_PadInit(int mode)
{
    int i;
    __PADMNG.open_slot[0] = 0;
    __PADMNG.open_slot[1] = 0;
    for (i = 0; i < 8; i++) {
        struct struct_dedbe94a *sd = &__PADMNG.sdata[i];
        sd->pdata.edge.set = 0;
        sd->pdata.repeat.set = 0;
        sd->repeat_state.repeat_key = 0;
        sd->phase = 0;
        sd->term_id = 0;
        sd->mode = 0xff;
        sd->modechg = 0xff;
        sd->modelock = 0;
    }
    return scePadInit(mode);
}

static void _fspad_SetRepeatData(struct struct_dedbe94a *sdp)
{
    int i;
    sdp->pdata.repeat.set = 0;
    for (i = 0; i < 16; i++) {
        unsigned short m = (unsigned short)(1 << i);
        struct struct_257fbc9f *k = &sdp->repeat_state.key[i];
        if (!(sdp->repeat_state.repeat_key & m))
            continue;
        if (!(sdp->pdata.button.set & m)) {
            k->flag = 0;
        } else if (sdp->pdata.edge.set & m) {
            sdp->pdata.repeat.set |= m;
            k->flag = 1;
            k->frame_ct = k->delay;
        } else {
            k->frame_ct--;
            if (!(k->frame_ct > 0)) {
                sdp->pdata.repeat.set |= m;
                k->flag = 2;
                k->frame_ct = k->interval;
            }
        }
    }
}
