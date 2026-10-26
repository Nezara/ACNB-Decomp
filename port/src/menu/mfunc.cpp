/* src/menu/mfunc.cpp — menu math / format helpers (OLMENU): angle wrapping to (-pi, pi], Euler rotation
 * matrices for the garage AC model, integer / vector / angle interpolation for the window movers, fade-in /
 * fade-out rects, black window frame rects, a fixed-width itoa, AC sample colours and body-part CLUTs, and
 * word / byte copy and clear. quick_match.c calls DF_MFUNC_Aligned by name. */
#include <string.h>
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"

#define MF_PI 3.14159274f      /* 0x40490fdb */
#define MF_2PI 6.28318548f     /* 0x40c90fdb */

/* DF_MFUNC_AngForm's wrap (inlined three times in RotMatrix / RotMatrixOffY in the original). The
 * quotient is truncated (cvt.w.s): -pi maps to pi, pi stays pi. */
static float mfunc_wrap(float a)
{
    if (!(a <= MF_PI))
        a = a - MF_2PI * (float)ps2_ftoi(ps2_div(MF_PI + a, MF_2PI));
    else if (a <= -MF_PI)
        a = a + MF_2PI * (float)ps2_ftoi(ps2_div(MF_PI - a, MF_2PI));
    return a;
}

/* (prev * (max - t) + next * t) / max per component; t < 0 -> prev, t >= max or max == 0 -> next
 * (t == 0 interpolates, unlike the rect versions). */
void DF_MFUNC_CalcInterIVec2(struct struct_214acd28 *pout, struct struct_214acd28 *pprev,
                             struct struct_214acd28 *pnext, int nT, int nMax)
{
    if (nT < 0) {
        *pout = *pprev;
    } else if (!(nT < nMax) || nMax == 0) {
        *pout = *pnext;
    } else {
        pout->x = (pprev->x * (nMax - nT) + pnext->x * nT) / nMax;
        pout->y = (pprev->y * (nMax - nT) + pnext->y * nT) / nMax;
    }
}

/* Cursor blink alpha 64..228, period 40 frames. */
int DF_MFUNC_GetBlinkAlpha(void)
{
    int a = (int)(MenuMng.frame % 40u);

    if (a / 20 == 1) a = 40 - a;
    return (a * 164) / 20 + 64;
}

/* The five colour rows of AC sample inIdx (menu data 2), in a function-static buffer. */
struct struct_098975f3 *DF_MFUNC_GetACSampleColor(int inIdx)
{
    PS2_FRAME(0x30);
    unsigned char *src = (unsigned char *)DF_MDATA_GetData(MenuMng.dataTbl[0], 2) + 0x10 + inIdx * 0x50;
    unsigned char *dst = (unsigned char *)&DF_MFUNC_GetACSampleColor__ac_col;

    ZeroMemory(dst, 0xb0);
    for (int row = 0; row < 5; row++)
        memcpy(dst + row * 0x10, src + row * 0x10, 0x10);
    return &DF_MFUNC_GetACSampleColor__ac_col;
}

/* Screen distance for a view angle: 640 / (2 tan(rad / 2)); 1000 outside (0, pi/2). */
float DF_MFUNC_CalcScrzFromViewRad(float inZ)
{
    PS2_FRAME(0x10);
    if (inZ <= 0.0f) return 1000.0f;
    if (!(inZ < ps2_u2f(0x3fc90fdb))) return 1000.0f;
    return ps2_div(640.0f, 2.0f * ps2_tanf(ps2_div(inZ, 2.0f)));
}

/* Body-part CLUT from five RGB colours (alpha 0x80 from the constant template). */
void DF_MFUNC_MakeCluts(unsigned int *ioDatap, struct struct_c4910010 *inPColp)
{
    PS2_FRAME(0x30);
    PS2_STACK(unsigned char[20], col, 0x10);
    static const unsigned char tmpl[20] = {0, 0, 0, 0x80, 0, 0, 0, 0x80, 0, 0, 0, 0x80, 0, 0, 0, 0x80, 0, 0, 0, 0x80};
    const unsigned char *in = (const unsigned char *)inPColp;

    if (!ioDatap || !inPColp) return;
    memcpy(col, tmpl, sizeof tmpl);
    for (int i = 0; i < 5; i++) {
        col[i * 4 + 0] = in[i * 3 + 0];
        col[i * 4 + 1] = in[i * 3 + 1];
        col[i * 4 + 2] = in[i * 3 + 2];
    }
    GAcUtil_MakeBodyPartsClut(ioDatap, (struct StColorU8 *)col);
}

/* Window frame rects: ioRectp[0] = inner rect, [1..3] right edge (top / middle / bottom), [4..6] left
 * edge; inShape 0 = none, 1 = right, 2 = left, 3 = both. ioFlagp[i] = 1 for every rect in use. */
void DF_MFUNC_SetBlackWindowAttr(int inShape, struct stMenuRect *inSrcRectp, struct stMenuRect *ioRectp,
                                 unsigned char *ioFlagp)
{
    struct stMenuRect *r = ioRectp;

    if (inShape < 0 || inShape > 3) return;
    r[0] = *inSrcRectp;
    if (inShape == 0) {
        ioFlagp[0] = 1;
        return;
    }
    if (inShape != 2) {
        if (r[0].w >= 9) r[0].w -= 8;
        r[1].x = r[2].x = r[3].x = r[0].x + r[0].w;
        r[1].w = r[2].w = r[3].w = 8;
        r[1].y = r[0].y;
        r[2].y = r[0].y + 8;
        r[3].y = r[0].y + r[0].h - 8;
        r[3].h = 8;
        r[1].h = 8;
        r[2].h = r[0].h - 0x10;
        ioFlagp[0] = ioFlagp[3] = ioFlagp[2] = ioFlagp[1] = 1;
    }
    if (inShape != 1) {
        r[4].x = r[5].x = r[6].x = r[0].x;
        r[4].w = r[5].w = r[6].w = 8;
        r[4].y = r[0].y;
        r[5].y = r[0].y + 8;
        r[6].y = r[0].y + r[0].h - 8;
        r[6].h = 8;
        r[4].h = 8;
        r[5].h = r[0].h - 0x10;
        ioFlagp[0] = ioFlagp[6] = ioFlagp[5] = ioFlagp[4] = 1;
        if (r[0].w >= 9) {
            r[0].x += 8;
            r[0].w -= 8;
        }
    }
}

/* Relocates the three pointer fields (+0x30, +0x38, +0x3c) of every 0x40-byte MI2 entry. */
void DF_MFUNC_RemapMI2(void *ioBuff)
{
    unsigned char *base = (unsigned char *)ioBuff;
    unsigned int n = *(unsigned int *)(base + 8);

    for (unsigned int i = 0; i < n; i++) {
        unsigned int *e = (unsigned int *)(base + 0x10 + i * 0x40);
        if (e[0x30 / 4]) e[0x30 / 4] += PS2PTR(base);
        if (e[0x38 / 4]) e[0x38 / 4] += PS2PTR(base);
        if (e[0x3c / 4]) e[0x3c / 4] += PS2PTR(base);
        n = *(unsigned int *)(base + 8);
    }
}

/* Z, X, then Y rotated by pi (the model faces the other way). */
void DF_MFUNC_RotMatrixOffY(float (*outMat)[4], float (*inMat)[4], float *inAng)
{
    PS2_FRAME(0x30);
    sceVu0RotMatrixZ(outMat, inMat, mfunc_wrap(inAng[2]));
    sceVu0RotMatrixX(outMat, outMat, mfunc_wrap(inAng[0]));
    sceVu0RotMatrixY(outMat, outMat, mfunc_wrap(MF_PI + inAng[1]));
}

/* X, Z, then Y. */
void DF_MFUNC_RotMatrix(float (*outMat)[4], float (*inMat)[4], float *inAng)
{
    PS2_FRAME(0x30);
    sceVu0RotMatrixX(outMat, inMat, mfunc_wrap(inAng[0]));
    sceVu0RotMatrixZ(outMat, outMat, mfunc_wrap(inAng[2]));
    sceVu0RotMatrixY(outMat, outMat, mfunc_wrap(inAng[1]));
}

float DF_MFUNC_AngForm(float inAng)
{
    return mfunc_wrap(inAng);
}

/* Fade-out start rect: a 2-pixel high line through the middle. */
void DF_MFUNC_GetFadeOutRect(struct stMenuRect *outDstp, struct stMenuRect *inSrcp)
{
    outDstp->x = inSrcp->x - inSrcp->w / 2;
    outDstp->y = inSrcp->y + inSrcp->h / 2;
    outDstp->w = inSrcp->w << 1;
    outDstp->h = 2;
}

/* Fade-in start rect: a 2-pixel wide column through the middle. */
void DF_MFUNC_GetFadeInRect(struct stMenuRect *outDstp, struct stMenuRect *inSrcp)
{
    outDstp->x = inSrcp->x + inSrcp->w / 2 - 1;
    outDstp->y = inSrcp->y - inSrcp->h / 2;
    outDstp->w = 2;
    outDstp->h = inSrcp->h << 1;
}

/* VU0: xyz = (prev * (max - t) + next * t) * (1 / max); w = next.w (kept by the .xyz ops). */
void DF_MFUNC_CalcInterVector(float *outV, float *inPV, float *inNV, int inT, int inMax)
{
    PS2_FRAME(0x20);
    PS2_STACK(float[4], a, 0x10);
    PS2_STACK(float[4], b, 0x00);

    if (inT <= 0) {
        memcpy(outV, inPV, 0x10);
        return;
    }
    if (!(inT < inMax) || inMax == 0) {
        memcpy(outV, inNV, 0x10);
        return;
    }
    float fp = (float)(inMax - inT), fn = (float)inT, r;
    memcpy(a, inPV, 0x10);
    for (int i = 0; i < 3; i++) a[i] = a[i] * fp;
    memcpy(b, inNV, 0x10);
    for (int i = 0; i < 3; i++) b[i] = b[i] * fn;
    for (int i = 0; i < 3; i++) a[i] = b[i] + a[i];   /* vadd.xyz B + A into B's register, stored at sp+0x10 */
    a[3] = b[3];
    r = ps2_div(1.0f, (float)inMax);
    float o[4] = {a[0] * r, a[1] * r, a[2] * r, a[3]};
    memcpy(outV, o, 0x10);
}

/* Only y is interpolated (the AC window's bottom edge); t <= 0 -> prev, t >= max -> next. */
void DF_MFUNC_CalcInterRectAcWinBtm(struct stMenuRect *outRectp, struct stMenuRect *inPrevRectp,
                                    struct stMenuRect *inNextRectp, int inT, int inMax)
{
    if (inT <= 0)
        *outRectp = *inPrevRectp;
    else if (!(inT < inMax) || inMax == 0)
        *outRectp = *inNextRectp;
    else
        outRectp->y = (inPrevRectp->y * (inMax - inT) + inNextRectp->y * inT) / inMax;
}

/* Only h is interpolated. */
void DF_MFUNC_CalcInterRectAcWinMdl(struct stMenuRect *outRectp, struct stMenuRect *inPrevRectp,
                                    struct stMenuRect *inNextRectp, int inT, int inMax)
{
    if (inT <= 0)
        *outRectp = *inPrevRectp;
    else if (!(inT < inMax) || inMax == 0)
        *outRectp = *inNextRectp;
    else
        outRectp->h = (inPrevRectp->h * (inMax - inT) + inNextRectp->h * inT) / inMax;
}

/* Fast start, slow end: s = (1 - t/max)^2, edges = prev * s + next * (1 - s) (+ 0.49 for rounding). */
void DF_MFUNC_CalcInterRectQuickSlow(struct stMenuRect *outRectp, struct stMenuRect *inPrevRectp,
                                     struct stMenuRect *inNextRectp, int inT, int inMax)
{
    if (inT <= 0) {
        *outRectp = *inPrevRectp;
        return;
    }
    if (!(inT < inMax) || inMax == 0) {
        *outRectp = *inNextRectp;
        return;
    }
    const float c = ps2_u2f(0x3efae148);
    struct stMenuRect p = *inPrevRectp, n = *inNextRectp;
    float u = 1.0f - ps2_div((float)inT, (float)inMax);
    float s = u * u;
    float s1 = 1.0f - s;
    float l = ((float)p.x + c) * s + ((float)n.x + c) * s1;
    float t = ((float)p.y + c) * s + ((float)n.y + c) * s1;
    float rr = (c + (float)(p.w + p.x)) * s + (c + (float)(n.w + n.x)) * s1;
    float bb = (c + (float)(p.h + p.y)) * s + (c + (float)(n.h + n.y)) * s1;
    outRectp->x = ps2_ftoi(l - c);
    outRectp->y = ps2_ftoi(t - c);
    outRectp->w = ps2_ftoi((rr - l) - c);
    outRectp->h = ps2_ftoi((bb - t) - c);
}

void DF_MFUNC_CalcInterRect(struct stMenuRect *outRectp, struct stMenuRect *inPrevRectp,
                            struct stMenuRect *inNextRectp, int inT, int inMax)
{
    if (inT <= 0) {
        *outRectp = *inPrevRectp;
    } else if (!(inT < inMax) || inMax == 0) {
        *outRectp = *inNextRectp;
    } else {
        struct stMenuRect p = *inPrevRectp, n = *inNextRectp;
        int u = inMax - inT;
        outRectp->x = (p.x * u + n.x * inT) / inMax;
        outRectp->y = (p.y * u + n.y * inT) / inMax;
        outRectp->w = (p.w * u + n.w * inT) / inMax;
        outRectp->h = (p.h * u + n.h * inT) / inMax;
    }
}

void DF_MFUNC_CalcInterColor(struct struct_76b810ec *outColp, struct struct_76b810ec *inPrevColp,
                             struct struct_76b810ec *inNextColp, int inT, int inMax)
{
    GAcUtil_CalcInterColor((struct StColorU8 *)outColp, (struct StColorU8 *)inPrevColp, (struct StColorU8 *)inNextColp,
                           inT, inMax);
}

/* Angle lerp the short way round (both ends wrapped first). */
float DF_MFUNC_CalcInterAng(float inPrev, float inNext, int inT, int inMax)
{
    float prev = mfunc_wrap(inPrev), next = mfunc_wrap(inNext), d;

    if (inT <= 0) return prev;
    if (!(inT < inMax) || inMax == 0) return next;
    d = next - prev;
    if (d < 0.0f) d = -d;
    if (!(d <= MF_PI)) {
        if (next <= prev)
            next = next + MF_2PI;
        else
            next = next - MF_2PI;
    }
    return ps2_div(prev * (float)(inMax - inT) + next * (float)inT, (float)inMax);
}

int DF_MFUNC_CalcInterValue(int inPrev, int inNext, int inT, int inMax)
{
    if (inT <= 0) return inPrev;
    if (!(inT < inMax) || inMax == 0) return inNext;
    return (inPrev * (inMax - inT) + inNext * inT) / inMax;
}

void *DF_MFUNC_Aligned(void *ioBuff, int inAlign)
{
    unsigned int p = PS2PTR(ioBuff);
    unsigned int r = ps2_modu32(p, (unsigned int)inAlign);

    if (r) p += (unsigned int)inAlign - r;
    return (void *)(uintptr_t)p;
}

/* Right-aligned decimal in a field of inPlace characters. The sign (inSign forces '+') goes to ioBuf[0],
 * left of the padding; 0 is printed without a sign (and with inPlace 0 writes '0' at ioBuf[-1]);
 * INT_MIN prints just the sign. */
void DF_MFUNC_Itoa(char *ioBuf, int inVal, int inSign, int inPlace)
{
    int neg = 0, len, i;

    if (inVal < 0) {
        inVal = (int)(0u - (unsigned int)inVal);
        neg = 1;
    }
    if (inVal == 0) {
        for (i = 0; i < inPlace - 1; i++) ioBuf[i] = ' ';
        ioBuf[inPlace - 1] = '0';
        ioBuf[inPlace] = 0;
        return;
    }
    len = 0;
    for (int v = inVal; v > 0; v /= 10) len++;
    if (inSign || neg) len++;
    if (len < inPlace) {
        len = inPlace;
        for (i = 0; i < inPlace; i++) ioBuf[i] = ' ';
    }
    if (inSign || neg) ioBuf[0] = neg ? '-' : '+';
    if (len <= 0) {
        ioBuf[0] = 0;
        return;
    }
    i = len - 1;
    for (int v = inVal; v > 0; v /= 10) ioBuf[i--] = (char)(v % 10 + '0');
    ioBuf[len] = 0;
}

/* Byte copy, front to back (overlapping ranges replicate exactly like the original loop). */
void DF_MFUNC_CopyMemoryC(void *outDst, void *inSrc, int inSize)
{
    unsigned char *d = (unsigned char *)outDst, *s = (unsigned char *)inSrc;
    if (inSize <= 0) return;
    if (d <= s || d >= s + inSize) { memcpy(d, s, inSize); return; }
    while (inSize--) *d++ = *s++;
}

/* Word copy: inSize is a count of 32-bit words. */
void DF_MFUNC_CopyMemory(void *outDst, void *inSrc, int inSize)
{
    unsigned int *d = (unsigned int *)outDst, *s = (unsigned int *)inSrc;
    if (inSize <= 0) return;
    if (d <= s || d >= s + inSize) { memcpy(d, s, inSize * 4u); return; }
    while (inSize--) *d++ = *s++;
}

void DF_MFUNC_ClearMemoryC(void *outBuff, int inSize)
{
    if (inSize > 0) memset(outBuff, 0, inSize);
}

/* Word clear: inSize is a count of 32-bit words. */
void DF_MFUNC_ClearMemory(void *outBuff, int inSize)
{
    if (inSize > 0) memset(outBuff, 0, inSize * 4u);
}
