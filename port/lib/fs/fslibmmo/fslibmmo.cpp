/* lib/fs/fslibmmo/fslibmmo.cpp - "MMO" movie-text overlay player (lib/fs/lib/fslibmmo/fslibmmo.c): plays the text
 * sequences of the opening prologue / ending staff roll from a data blob (0x40-byte header, version 0x30) that
 * main/loop_mmo reads whole into a buffer. The blob holds resources (u16 text strings or sprites), movement tables
 * (x / y / scale keys), colour tables (ARGB keys), 0x30-byte objects tying one resource + movement + colour to a frame
 * window, and up to 3 fonts (CCM glyph map + TIM2 texture). fsmmo_Exec (once per frame) walks the objects, lerps the
 * keys with an easing per key (linear, ease-out, ease-in, smoothstep-like, cubic) for the current frame and draws each
 * live one: text through the DWARF-less fontlib (acnb/sdk.h fontlib_*, interpreted in the hybrid) with uiBlur extra
 * passes 0.125 px lower, sprites through fsmmo_DrawSprite (fslibmmoDraw.c). Text objects have a typewriter (visible
 * characters = elapsed frames / dwTypeWriter; the sound callback fires when a new character appears).
 * All s_System pointers are guest addresses into the blob / work memory (32-bit values).
 * Original quirks kept: _DrawText copies the string into a 512-entry stack buffer without a bound and hands it to
 * FsFontDrawText as a FORMAT string; the typewriter's newline test reads the character after the last copied one;
 * the callback gets {0x3ea, 0x7f} (StaffRollSE ignores it); _CalcMovement divides by dwTypeWriter, dwTWInterval is
 * never read; fsmmo_Exec's "finished" test reads only the last object; the PosX == 0 measure arm writes the centred
 * x back through pCalcResult (= _Draw's copy); _CalcColor / _CalcMovement read pObject->dwFrameOffset before their
 * null checks (done after them here: same result for non-null). */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "../include/fslibfont_Wrap.cpp"   /* FsFontDrawText: static from the header, same TU as the original */

#ifdef ACNB_SOURCEPORT
void (*s_pSoundPlay_Callback)(struct struct_575dbb50 *) = GFN(_SoundPlay_CallbackDummy);   /* 0x27ac9c */
unsigned int TW_NumChar;            /* 0x27c634 */
struct struct_945fb8b8 s_System;    /* 0x28a8e0 */
struct _FS_FONT_ST s_Font;          /* 0x28a920 */
#endif

/* (u32)fFrame: cvt.w.s with the >= 2^31 arm (sub 2^31, convert, or the sign bit) */
static unsigned int mmo_ftou(float f)
{
    if (f >= 2147483648.0f) return (unsigned int)ps2_ftoi(f - 2147483648.0f) | 0x80000000u;
    return (unsigned int)ps2_ftoi(f);
}

/* easing by key type (low byte of dwHokan); 0 / > 4 = linear */
static float mmo_ease(float t, unsigned int type)
{
    switch (type) {
    case 1: return t * (2.0f - t);
    case 2: return t * t;
    case 3: return t * (3.0f + t * (t - 3.0f));
    case 4: { float t2 = t * t; return t * t2; }
    default: return t;
    }
}

/* mula.s a, e; madd.s b * (1 - e): two rounded products, rounded sum */
static float mmo_lerp(float a, float b, float e)
{
    float p = a * e;
    float q = b * (1.0f - e);
    return p + q;
}

static void _SoundPlay_CallbackDummy(struct struct_575dbb50 *pVInfo)
{
    Voice_Call(pVInfo->sss_id, pVInfo->vol);
}

static void _DrawSetAlignment(struct struct_91dc2df0 *pObject)
{
    PS2_FRAME(0x20);
    unsigned int uiFlag = 0;
    char pos = pObject->ResourceInfo.text.PosX;
    switch (pos) {
    case 2: uiFlag |= 0x10; break;
    case 1: break;
    case 0: uiFlag |= 0x20; break;
    }
    if (pObject->dwTypeWriter != 0 && pos == 0) uiFlag = 0;
    fontlib_00102b60(uiFlag);
    fontlib_00103640((int)(uiFlag & 0x100) >> 8);
}

static void _DrawText(struct struct_91dc2df0 *pObject, struct struct_a7fa1686 *pCalcResult, void *pResource)
{
    PS2_FRAME(0x490);
    PS2_STACK(unsigned short[512], pResource2, 0x80);
    PS2_STACK(struct struct_575dbb50, vinfo, 0x480);
    PS2_STACK(float, width, 0x488);
    PS2_STACK(float, height, 0x48c);
    if (!pObject || !pCalcResult || !pResource) return;
    unsigned int ft = (unsigned char)pObject->ResourceInfo.text.FontType;
    if ((int)ft < 3) {
        fontlib_00100650(s_System.pCcm[ft]);
        fontlib_00102cd0();
        s_System.pCurCcm = s_System.pCcm[ft];
        fontlib_00100630(0, s_System.pTex[ft]);
    }
    void *pOld = fontlib_00100780(&s_Font);
    fontlib_001009c0();
    _DrawSetAlignment(pObject);
    fontlib_00102bc0(pCalcResult->fScale, pCalcResult->fScale);
    fontlib_00102b30(pCalcResult->uiColor);
    if (pCalcResult->NumDispChar != -1) {
        unsigned short *pstring = (unsigned short *)pResource;
        unsigned int i = 0, numchar = 0;
        unsigned short src = *pstring;
        while (src != 0) {
            if (!(numchar < (unsigned int)pCalcResult->NumDispChar)) break;
            pResource2[i] = src;
            unsigned short c = *pstring;
            if (c != 0x3000 && c != 0x20 && c != 0xa && c != 0x2a) numchar++;
            pstring++;
            src = *pstring;
            i++;
        }
        if ((unsigned int)pCalcResult->NumDispChar == numchar && numchar != TW_NumChar) {
            vinfo.vol = 0x7f;
            vinfo.sss_id = 0x3ea;
            gcall(s_pSoundPlay_Callback, &vinfo);
            TW_NumChar = numchar;
            if (*pstring == 0xa) TW_NumChar = 0;
        }
        pResource2[i] = 0;
        if (pObject->ResourceInfo.text.PosX == 0) {
            fontlib_001034d0(&width, &height, 0, (unsigned char *)pResource, 0, 0, 0);
            pCalcResult->fX = pCalcResult->fX - ps2_div(width, 2.0f);
        }
        FsFontDrawText(pCalcResult->fX, pCalcResult->fY, (char *)pResource2);
    } else {
        FsFontDrawText(pCalcResult->fX, pCalcResult->fY, (char *)pResource);
    }
    fontlib_00100880();
    fontlib_00100780(pOld);
}

static void _Draw(struct struct_91dc2df0 *pObject, struct struct_a7fa1686 *pCalcResult)
{
    PS2_FRAME(0xa0);
    PS2_STACK(float[2][2], St, 0x60);
    PS2_STACK(float[2][2], Xy, 0x70);
    PS2_STACK(struct struct_a7fa1686, CopyCalcResult, 0x80);
    if (!pObject || !pCalcResult) return;
    struct struct_3c22a7cd *h = s_System.pDataHead;
    char *pRes0 = h->dwResourceOffset ? (char *)h + h->dwResourceOffset : 0;
    if (!pRes0) return;
    char *pResource = pRes0 + pObject->dwResourceOffset;
    if (!pResource) return;
    if (pObject->ResourceType == 0) {
        if (pCalcResult->fY < -100.0f || 548.0f < pCalcResult->fY) return;
        if (pCalcResult->fX < -1280.0f || 1280.0f < pCalcResult->fX) return;
        fontlib_001009c0();
        for (unsigned int i = 0; i < s_System.uiBlur + 1; i++) {
            CopyCalcResult = *pCalcResult;
            if (i != 0) CopyCalcResult.fY = CopyCalcResult.fY + 0.125f * (float)i;
            _DrawText(pObject, &CopyCalcResult, pResource);
        }
        fontlib_00100880();
    } else {
        if (pCalcResult->fY < -224.0f || 672.0f < pCalcResult->fY) return;
        memset(Xy, 0, sizeof(Xy));
        Xy[0][0] = pCalcResult->fX - 64.0f;
        Xy[0][1] = pCalcResult->fY - 64.0f;
        Xy[1][0] = 64.0f + pCalcResult->fX;
        Xy[1][1] = 64.0f + pCalcResult->fY;
        St[0][0] = 0.0f;   /* lq of the rodata quad at 0x259b60 */
        St[0][1] = 0.0f;
        St[1][0] = 1.0f;
        St[1][1] = 1.0f;
        fsmmo_DrawSprite(Xy, St, pResource, pCalcResult);
    }
}

static int _CalcColor(struct struct_369fd237 *pColor, struct struct_91dc2df0 *pObject, struct struct_a7fa1686 *pCalcResult)
{
    if (!pColor || !pObject || !pCalcResult) return 1;
    unsigned int uiFrame = mmo_ftou(s_System.fFrame) - pObject->dwFrameOffset;
    struct struct_30917a17 *pKey = (struct struct_30917a17 *)(pColor + 1);
    unsigned int n = pColor->dwKeyNum - 1;
    unsigned int uiCnt;
    for (uiCnt = 0; uiCnt < n; uiCnt++, pKey++) {
        if (uiFrame < pKey[0].dwFrame) continue;
        if (uiFrame < pKey[1].dwFrame) break;
    }
    if (!(uiCnt < n)) return 1;
    unsigned int uiAllTime = pKey[1].dwFrame - pKey[0].dwFrame;
    float t = ps2_div((float)uiAllTime - (float)(uiFrame - pKey[0].dwFrame), (float)uiAllTime);
    unsigned int type = (unsigned char)pKey[0].dwHokan;
    unsigned int R = (unsigned int)ps2_ftoi(mmo_lerp((float)pKey[0].R, (float)pKey[1].R, mmo_ease(t, type))) & 0xff;
    unsigned int G = (unsigned int)ps2_ftoi(mmo_lerp((float)pKey[0].G, (float)pKey[1].G, mmo_ease(t, type))) & 0xff;
    unsigned int B = (unsigned int)ps2_ftoi(mmo_lerp((float)pKey[0].B, (float)pKey[1].B, mmo_ease(t, type))) & 0xff;
    unsigned int A = (unsigned int)ps2_ftoi(mmo_lerp((float)pKey[0].A, (float)pKey[1].A, mmo_ease(t, type))) & 0xff;
    pCalcResult->uiColor = B | (G << 8) | (A << 24) | (R << 16);
    return 0;
}

static int _CalcMovement(struct struct_369fd237 *pMovement, struct struct_91dc2df0 *pObject, struct struct_a7fa1686 *pCalcResult)
{
    if (!pMovement || !pObject || !pCalcResult) return 1;
    unsigned int uiFrame = mmo_ftou(s_System.fFrame) - pObject->dwFrameOffset;
    struct struct_a09951cf *pKey = (struct struct_a09951cf *)(pMovement + 1);
    unsigned int n = pMovement->dwKeyNum - 1;
    unsigned int uiCnt;
    for (uiCnt = 0; uiCnt < n; uiCnt++, pKey++) {
        if (uiFrame < pKey[0].dwFrame) continue;
        if (uiFrame < pKey[1].dwFrame) break;
    }
    if (!(uiCnt < n)) return 1;
    unsigned int uiAllTime = pKey[1].dwFrame - pKey[0].dwFrame;
    float t = ps2_div((float)uiAllTime - (float)(uiFrame - pKey[0].dwFrame), (float)uiAllTime);
    unsigned int type = (unsigned char)pKey[0].dwHokan;
    pCalcResult->fX = pObject->ResourceInfo.text.fOffsetX + mmo_lerp(pKey[0].fX, pKey[1].fX, mmo_ease(t, type));
    pCalcResult->fY = pObject->ResourceInfo.text.fOffsetY + mmo_lerp(pKey[0].fY, pKey[1].fY, mmo_ease(t, type));
    pCalcResult->fScale = mmo_lerp(pKey[0].fScale, pKey[1].fScale, mmo_ease(t, type));
    if (pObject->dwTypeWriter != 0) {
        unsigned int twFrame = uiFrame - pObject->ResourceInfo.text.FrameOffset;
        pCalcResult->NumDispChar = (int)twFrame > 0 ? (int)(twFrame / pObject->dwTypeWriter) : 0;
    } else {
        pCalcResult->NumDispChar = -1;
    }
    return 0;
}

static int _Calc(struct struct_369fd237 *pMovement, struct struct_369fd237 *pColor, struct struct_91dc2df0 *pObject,
                 struct struct_a7fa1686 *pCalcResult)
{
    PS2_FRAME(0x10);
    if (!pMovement || !pColor || !pObject || !pCalcResult) return 1;
    if (_CalcMovement(pMovement, pObject, pCalcResult)) return 1;
    return _CalcColor(pColor, pObject, pCalcResult) != 0;
}

int fsmmo_Exec(void)
{
    PS2_FRAME(0x70);
    PS2_STACK(struct struct_a7fa1686, CalcResult, 0x50);
    struct struct_3c22a7cd *h = s_System.pDataHead;
    if (!h) return -1;
    if (!(0 < h->usResourceNum)) return -1;
    if (!(0 < h->usMovementNum)) return -1;
    if (!(0 < h->usColorNum)) return -1;
    if (!(0 < h->usObjectNum)) return -1;
    if (!(s_System.uiBlur < 0xffffffffu)) return -1;
    struct struct_91dc2df0 *pObject = h->dwObjectOffset ? (struct struct_91dc2df0 *)((char *)h + h->dwObjectOffset) : 0;
    if (!pObject) return -1;
    char *pMovement0 = h->dwMovementOffset ? (char *)h + h->dwMovementOffset : 0;
    if (!pMovement0) return -1;
    char *pColor0 = h->dwColorOffset ? (char *)h + h->dwColorOffset : 0;
    if (!pColor0) return -1;
    for (unsigned int uiCnt = 0; uiCnt < h->usObjectNum; ) {
        unsigned int frame = mmo_ftou(s_System.fFrame);
        if (!(frame < pObject->dwFrameOffset) && !(pObject->dwFrameOffset + pObject->dwAllFrame < frame)) {
            struct struct_369fd237 *pMovement = (struct struct_369fd237 *)(pMovement0 + pObject->dwMovementOffset);
            if (!pMovement) return -1;
            if (pMovement->dwKeyNum < 2) return -1;
            struct struct_369fd237 *pColor = (struct struct_369fd237 *)(pColor0 + pObject->dwColorOffset);
            if (!pColor) return -1;
            if (pColor->dwKeyNum < 2) return -1;
            if (_Calc(pMovement, pColor, pObject, &CalcResult) == 0) _Draw(pObject, &CalcResult);
        }
        uiCnt++;
        pObject++;
        h = s_System.pDataHead;   /* loop bound re-read every pass */
    }
    unsigned int frame = mmo_ftou(s_System.fFrame);
    if (pObject[-1].dwFrameOffset + pObject[-1].dwAllFrame < frame) return 1;
    s_System.fFrame = s_System.fFrame + s_System.fFramePace;
    return 0;
}

static int _Analyze(void)
{
    if (!s_System.pDataHead) return -1;
    return s_System.pDataHead->usVersion != 0x30 ? -1 : 0;
}

/* bump allocator over the work memory (guest addresses, 32-bit arithmetic) */
static void *GetNewMemory(unsigned int uiSize, unsigned int uiAlignSize)
{
    unsigned int p0 = PS2PTR(s_System.pWorkMemory);
    if (!p0) return 0;
    unsigned int pMem = p0;
    if (uiAlignSize >= 2) pMem = (p0 + uiAlignSize - 1) / uiAlignSize * uiAlignSize;
    unsigned int need = uiSize + (pMem - p0);
    unsigned int avail = s_System.uiWorkMemorySize;
    if (avail < need) return 0;
    s_System.pWorkMemory = (void *)(uintptr_t)(p0 + need);
    s_System.uiWorkMemorySize = avail - need;
    return (void *)(uintptr_t)pMem;
}

void fsmmo_SetWorkMemory(void *pAddress, unsigned int uiSize)
{
    if (pAddress && uiSize) {
        s_System.pWorkMemory = pAddress;
        s_System.uiWorkMemorySize = uiSize;
    }
}

static void _InitFont(void)
{
    PS2_FRAME(0x10);
    if (!s_System.uiFontInited) {
        ps2_memset(&s_Font, 0, 0x1ac);
        /* the original tests &s_Font == NULL here (-> rc -2, return): a constant address, never true */
        fontlib_001007f0(&s_Font, 0, 0x10);
        fontlib_00103670(0x1f);
        s_System.uiFontInited = 1;
    }
    fontlib_00102b30(0x7f7f7f7fu);
    fontlib_00100c10(0, 1);
    fontlib_00100c10(2, 1);
    fontlib_00100c10(1, 0);
    fontlib_00100c10(3, 1);
    fontlib_00100c10(4, 1);
    s_System.uiDmaBufferSize = fontlib_00100d90(0x100, 0);
    s_System.pDmaBuffer = GetNewMemory(s_System.uiDmaBufferSize, 0x10);
    if (s_System.pDmaBuffer) fontlib_00100d30(s_System.pDmaBuffer, s_System.uiDmaBufferSize);
}

void fsmmo_SetBlurNum(unsigned int uiBlurNum)
{
    s_System.uiBlur = uiBlurNum;
}

int fsmmo_Initialize(void *pMmoData)
{
    PS2_FRAME(0x10);
    if (!pMmoData) return -1;
    s_System.pDataHead = (struct struct_3c22a7cd *)pMmoData;
    s_System.pCurCcm = 0;
    s_System.fFrame = 0.0f;
    switch (s_System.pDataHead->Fps) {
    case 3: s_System.fFramePace = 0x1.3ffffep+5f; break;    /* 0x421fffff 39.999996 */
    case 2: s_System.fFramePace = 0x1.0aaaaap+5f; break;    /* 0x42055555 33.333332 */
    case 1: s_System.fFramePace = 0x1.3ffffep+4f; break;    /* 0x419fffff 19.999998 */
    default: s_System.fFramePace = 0x1.0aaaaap+4f; break;   /* 0x41855555 16.666666 */
    }
    s_System.uiBlur = 0;
    if (s_System.pDataHead->dwFontOffset) {
        struct struct_841791ab *pFont = (struct struct_841791ab *)((char *)s_System.pDataHead + s_System.pDataHead->dwFontOffset);
        for (int i = 0; i < 3; i++) {
            if (pFont->dwCcm[i] && pFont->dwTex[i]) {
                s_System.pCcm[i] = pFont->dwCcm[i] ? (char *)s_System.pDataHead + pFont->dwCcm[i] : 0;
                s_System.pTex[i] = pFont->dwTex[i] ? (char *)s_System.pDataHead + pFont->dwTex[i] : 0;
            }
        }
        _InitFont();
    }
    TW_NumChar = 0;
    return _Analyze();
}

void fsmmo_SetSoundPlayCallback(void (*pCallback)(void))
{
    s_pSoundPlay_Callback = (void (*)(struct struct_575dbb50 *))pCallback;
}

#ifndef ACNB_SOURCEPORT
/* MIPS callers: the pointers are stored into guest state, so they go through as the raw register */
static void bw__fsmmo_SetWorkMemory(EECore *c)
{
    fsmmo_SetWorkMemory((void *)(uintptr_t)c->gpr[A0].uw[0], c->gpr[A1].uw[0]);
}
static void bw__fsmmo_Initialize(EECore *c)
{
    c->gpr[V0].sd[0] = (s32)fsmmo_Initialize((void *)(uintptr_t)c->gpr[A0].uw[0]);
}
static void bw__fsmmo_SetSoundPlayCallback(EECore *c)
{
    fsmmo_SetSoundPlayCallback((void (*)(void))(uintptr_t)c->gpr[A0].uw[0]);
}
#endif
