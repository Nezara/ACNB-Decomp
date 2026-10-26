/* src/lib/pad.cpp - game-side pad layer over the fspad library (whole file). Pad_UpdateData polls fspad and copies
 * both ports' 0x24-byte read buffers into s_padd[2] (sticks forced to neutral 0x80 when the pad is digital - which
 * also requests analog mode - or absent); the accessors return buttons / edges / stick bytes, pack the 4-byte
 * StCompactPadData of the frame-synced key stream (buttons + 4-bit sticks) and turn the left stick into d-pad bits.
 * s_isPadOpened / s_isSplit are only written, never read. */
#include "acnb/port.h"

unsigned char Pad_GetR3v(int slot) { return s_padd[slot].r3v; }
unsigned char Pad_GetR3h(int slot) { return s_padd[slot].r3h; }
unsigned char Pad_GetL3v(int slot) { return s_padd[slot].l3v; }
unsigned char Pad_GetL3h(int slot) { return s_padd[slot].l3h; }
unsigned short Pad_GetTriggerBits(int slot) { return s_padd[slot].edge.set; }
unsigned short Pad_GetOnBits(int slot) { return s_padd[slot].button.set; }

/* buttons + the high nibble of each stick byte */
void Pad_GetCompactData(int slot, StCompactPadData *padd)
{
    struct_af668abd *p = &s_padd[slot];
    padd->bits = p->button.set;
    padd->al3h = p->l3h >> 4;
    padd->al3v = p->l3v >> 4;
    padd->ar3h = p->r3h >> 4;
    padd->ar3v = p->r3v >> 4;
}

int Pad_IsTrigger(int slot, unsigned short button) { return (s_padd[slot].edge.set & button) != 0; }
int Pad_IsOn(int slot, unsigned short button) { return (s_padd[slot].button.set & button) != 0; }

void Pad_SetSplit(int isSplit) { s_isSplit = isSplit; }

/* no buttons, sticks centred (nibble 8) */
void Pad_NeutralDataCompact(StCompactPadData *pcpadd)
{
    pcpadd->bits = 0;
    pcpadd->al3h = 8;
    pcpadd->al3v = 8;
    pcpadd->ar3h = 8;
    pcpadd->ar3v = 8;
}

void Pad_ClearData(void) { ZeroMemory32b(s_padd, 0x12); }

/* left stick beyond +-0x40 -> d-pad bits (up 0x10 / down 0x40 / left 0x80 / right 0x20), unless the opposite
 * direction is already pressed; each test re-reads *pkey */
void Pad_AssignDigitalToAnalog(int slot, short *pkey)
{
    int v = s_padd[slot].l3v - 0x80;
    int h = s_padd[slot].l3h - 0x80;
    if (v < -0x40 && !(*pkey & 0x40)) *pkey |= 0x10;
    if (v > 0x40 && !(*pkey & 0x10)) *pkey |= 0x40;
    if (h < -0x40 && !(*pkey & 0x20)) *pkey |= 0x80;
    if (h > 0x40 && !(*pkey & 0x80)) *pkey |= 0x20;
}

static void pad_read_port(int port)
{
    struct_af668abd *p = &s_padd[port];
    int mode = fspad_PadGetCurrentMode(port, 0);
    CopyMemory(p, fspad_PadRead(port, 0), 0x24);
    if (mode == 0) {                    /* digital: ask for analog, sticks neutral */
        fspad_PadModeAnalog(port, 0, 3);
        p->r3v = 0x80; p->r3h = 0x80; p->l3v = 0x80; p->l3h = 0x80;
    } else if (mode == 0xff) {          /* no pad */
        ZeroMemory32b(p, 9);
        p->r3v = 0x80; p->r3h = 0x80; p->l3v = 0x80; p->l3h = 0x80;
    }
}

void Pad_ReadData(void)
{
    PS2_FRAME(0x20);
    pad_read_port(0);
    pad_read_port(1);
}

void Pad_UpdateData(void)
{
    PS2_FRAME(0x10);
    fspad_PadUpdate();
    Pad_ReadData();
}

void Pad_Init(void)
{
    PS2_FRAME(0x10);
    s_isSplit = 0;
    ZeroMemory32b(s_padd, 0x12);
    fspad_PadInit(0);
    fspad_PadPortOpen(0, 0);
    fspad_PadPortOpen(1, 0);
    fspad_PadSetRepeat(0, 0, 0xfff0, 0x1e, 3);
    fspad_PadSetRepeat(1, 0, 0xfff0, 0x1e, 3);
    s_isPadOpened = 1;
}
