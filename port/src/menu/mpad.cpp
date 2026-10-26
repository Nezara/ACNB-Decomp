/* src/menu/mpad.cpp — the menu's pad layer (OLMENU). Once per menu frame DF_MPAD_Update reads both pads, turns
 * the analog sticks into -1..1 floats with a dead zone, folds a pushed stick into 8-direction key bits and runs
 * __UpdateBtn: per-button hold counters (saturating, 0xff wraps to 0xf9) and repeat flags (first frame, then every
 * 6 frames from frame 18). A disconnected pad (mode 0xff) or one that came back less than 10 frames ago reads as
 * all released. */
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"

/* clears the record (ClearMemory takes words: 0x12 = the whole 0x48 bytes) keeping the pad mode */
static void mpad_clear(struct struct_2d84b07b *pad)
{
    unsigned char mode = pad->mode;
    DF_MFUNC_ClearMemory(pad, 0x12);
    memset(&pad->stick_l3, 0, 0x20);
    pad->mode = mode;
}

/* 8-direction key bits of a stick angle (atan2(h, -v): 0 = up, pi/2 = right); dir = {up, up-right, right,
 * right-down, down, down-left, left, left-up} */
static unsigned int mpad_dir8(float a, const unsigned int *dir)
{
    const float p1 = ps2_u2f(0x3ec90fdb), p3 = ps2_u2f(0x3f96cbe4);   /* pi/8, 3pi/8 */
    const float p5 = ps2_u2f(0x3ffb53d1), p7 = ps2_u2f(0x402feddf);   /* 5pi/8, 7pi/8 */
    if (-p1 <= a && a < p1) return dir[0];
    if (p1 <= a && a < p3) return dir[1];
    if (p3 <= a && a < p5) return dir[2];
    if (p5 <= a && a < p7) return dir[3];
    if (p7 <= a || a < -p7) return dir[4];
    if (-p7 <= a && a < -p5) return dir[5];
    if (-p5 <= a && a < -p3) return dir[6];
    return dir[7];
}

void DF_MPAD_Update(void)
{
    static const unsigned int dir_l[8] = {0x10, 0x30, 0x20, 0x60, 0x40, 0xc0, 0x80, 0x90};
    static const unsigned int dir_r[8] = {0x10000, 0x90000, 0x80000, 0xa0000, 0x20000, 0x60000, 0x40000, 0x50000};
    int port;

    PS2_FRAME(0x60);
    Pad_ReadData();
    for (port = 0; port < 2; port++) {
        struct struct_2d84b07b *pad = &MPad[port];
        short *wait = &DF_MPAD_Update__wait[port];
        struct struct_af668abd *rd;
        unsigned int btn;

        pad->mode = (unsigned char)fspad_PadGetCurrentMode(port, 0);
        if (pad->mode == 0) fspad_PadModeAnalog(port, 0, 3);
        if (pad->mode == 0xff) {
            *wait = 10;
            mpad_clear(pad);
            continue;
        }
        if (*wait > 0) {
            (*wait)--;
            mpad_clear(pad);
            continue;
        }
        rd = fspad_PadRead(port, 0);
        btn = rd->button.set;
        if (pad->mode == 1 || pad->mode == 2) {
            __UpdateStick(&pad->stick_l3, rd->l3h, rd->l3v);
            __UpdateStick(&pad->stick_r3, rd->r3h, rd->r3v);
            if (!(pad->stick_l3.pow <= 0.25f) && pad->stickToKey_off == 0) btn |= mpad_dir8(pad->stick_l3.direct, dir_l);
            if (!(pad->stick_r3.pow <= 0.25f) && pad->stickToKey_off == 0) btn |= mpad_dir8(pad->stick_r3.direct, dir_r);
        } else {
            memset(&pad->stick_l3, 0, 0x20);
        }
        __UpdateBtn(pad, btn);
    }
}

/* One button: hold counter cnt (+1 while held, saturating; 0xff wraps to 0xf9 so the repeat cadence goes on) and
 * its repeat flag (bit of the byte rep) on frame 1 and every 6 frames from 18. */
static inline void mpad_btn(unsigned char *cnt, unsigned char *rep, int bit, unsigned int held)
{
    int c, r;
    if (held) *cnt = (unsigned char)(*cnt + (*cnt < 0xff));
    else *cnt = 0;
    if (*cnt == 0xff) *cnt = 0xf9;
    c = *cnt;
    if (c >= 0x12 && (c - 0x12) % 6 == 0) r = 1;
    else r = c == 1;
    *rep = (unsigned char)((*rep & ~(1 << bit)) | (r << bit));
}

/* Two held directions (a = up / down, b = left / right): both counters take the larger one and both repeat flags
 * the flag of the larger (a tie copies a to b), so a diagonal repeats in step. */
static inline void mpad_sync(unsigned char *a, unsigned char *b, unsigned char *rep, int abit, int bbit)
{
    if (*a == 0 || *b == 0) return;
    if (*a < *b) {
        *a = *b;
        *rep = (unsigned char)((*rep & ~(1 << abit)) | (((*rep >> bbit) & 1) << abit));
    } else {
        *b = *a;
        *rep = (unsigned char)((*rep & ~(1 << bbit)) | (((*rep >> abit) & 1) << bbit));
    }
}

static void __UpdateBtn(struct struct_2d84b07b *inPadp, unsigned int inBtn)
{
    /* button mask, counter offset, repeat byte offset, repeat bit (original order) */
    static const struct { unsigned int mask; unsigned char cnt, rep, bit; } tab[20] = {
        {0x10, 0x00, 0x30, 0},    {0x40, 0x01, 0x30, 1},    {0x80, 0x02, 0x30, 2},    {0x20, 0x03, 0x30, 3},
        {0x1000, 0x04, 0x30, 4},  {0x4000, 0x05, 0x30, 5},  {0x8000, 0x06, 0x30, 6},  {0x2000, 0x07, 0x30, 7},
        {0x400, 0x08, 0x31, 0},   {0x100, 0x09, 0x31, 1},   {0x2, 0x0a, 0x31, 2},     {0x800, 0x0b, 0x31, 3},
        {0x200, 0x0c, 0x31, 4},   {0x4, 0x0d, 0x31, 5},     {0x8, 0x0e, 0x31, 6},     {0x1, 0x0f, 0x31, 7},
        {0x10000, 0x44, 0x32, 0}, {0x20000, 0x45, 0x32, 1}, {0x40000, 0x46, 0x32, 2}, {0x80000, 0x47, 0x32, 3},
    };
    unsigned char *p = (unsigned char *)inPadp;
    int i;

    for (i = 0; i < 20; i++) mpad_btn(p + tab[i].cnt, p + tab[i].rep, tab[i].bit, inBtn & tab[i].mask);
    /* diagonals: d-pad up / down x left / right, then the right-stick directions */
    mpad_sync(p + 0x00, p + 0x02, p + 0x30, 0, 2);
    mpad_sync(p + 0x00, p + 0x03, p + 0x30, 0, 3);
    mpad_sync(p + 0x01, p + 0x02, p + 0x30, 1, 2);
    mpad_sync(p + 0x01, p + 0x03, p + 0x30, 1, 3);
    mpad_sync(p + 0x44, p + 0x46, p + 0x32, 0, 2);
    mpad_sync(p + 0x44, p + 0x47, p + 0x32, 0, 3);
    mpad_sync(p + 0x45, p + 0x46, p + 0x32, 1, 2);
    mpad_sync(p + 0x45, p + 0x47, p + 0x32, 1, 3);
}

/* raw stick axis (0..255) -> -1..1: dead zone 0.3, rescaled by 1/0.7, snapped to +-1 above 0.98 */
static float mpad_axis(unsigned char raw)
{
    float x = (float)((int)raw - 128) * 0.0078125f, a, r;
    a = x < 0.0f ? -x : x;
    if (a <= 0.3f) {
        r = 0.0f;
    } else {
        a = x < 0.0f ? -x : x;
        r = (float)(x < 0.0f ? -1 : 1) * (a - 0.3f) / 0.7f;
    }
    a = r < 0.0f ? -r : r;
    if (!(a <= 0.98f)) r = (float)(r < 0.0f ? -1 : 1);
    return r;
}

static void __UpdateStick(struct struct_be1f8c58 *outStick, unsigned char inH, unsigned char inV)
{
    float h, v, pow;

    PS2_FRAME(0x30);
    h = mpad_axis(inH);
    v = mpad_axis(inV);
    outStick->h = h;
    outStick->v = v;
    outStick->direct = ps2_ieee754_atan2f(h, -v);
    pow = ps2_sqrt(h * h + v * v);
    if (!(pow <= 1.0f)) pow = 1.0f;
    outStick->pow = pow;
}

void DF_MPAD_Init(void)
{
    int port;

    PS2_FRAME(0x40);
    for (port = 0; port < 2; port++) {
        MPad[port].mode = 0;
        mpad_clear(&MPad[port]);
    }
    MPadUser = 0;
}
