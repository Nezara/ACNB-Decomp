/* src/event/cond.cpp - stage "condition" changes: scripted transitions of the stage's lights (StgInfo.light), fog
 * (StgInfo.fog / fogparam), screen effects (StgInfo.pscreff) and background colours (CopyOfSysInfo colBg / colSfx /
 * colLf) from condition set inID to outID of the stage's array file (Array->litCond / fogCond / screff / bgColor).
 * Cond_Set starts element 0..3 (light, fog, screen effect, background) with an interpolation curve (linear, sine,
 * pulse, ease in / out) over intpFrame frames; Cond_Proc (from DF_StageEventProc) steps the running elements
 * (runFlag bit 0) and, with runFlag bit 1, blends each element's current state toward condition blendID by
 * blendRate % every frame. Element order in s_ElmtIntpFunc / s_ElmtBlendFunc: 0 light, 1 fog, 2 pfx, 3 bgc.
 * The VU0 blends are kept lane for lane: .xyz adds pass the first operand's w through (stale stack in the bgc
 * functions, see quirks), colours are scaled by 1.001 before the float -> int conversion. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/nodwarf.h"

#ifdef ACNB_SOURCEPORT
void (*s_ElmtIntpFunc[4])(__cond_element_st *) = {__cond_ChangeLit, __cond_ChangeFog, __cond_ChangePfx, __cond_ChangeBgc};
void (*s_ElmtBlendFunc[4])(__cond_element_st *) = {__cond_BlendLit, __cond_BlendFog, __cond_BlendPfx, __cond_BlendBgc};
unsigned short s_RunFlag[2] = {1, 2};
unsigned short s_ElmtRunFlag[4] = {1, 2, 4, 8};
float (*const cond_intp_func[5])(__cond_element_st *) = {__cond_IntpLinear, __cond_IntpSin, __cond_IntpPulse,
                                                         __cond_IntpEaseIn, __cond_IntpEaseOut};
#endif

/* float -> unsigned conversion as MWCC emits it (cvt.w.s, values >= 2^31 shifted down and bit 31 set) */
static inline unsigned int cond_ftou(float f)
{
    return f >= 2147483648.0f ? (unsigned int)ps2_ftoi(f - 2147483648.0f) | 0x80000000u : (unsigned int)ps2_ftoi(f);
}

/* lane copy that keeps the bits (stale stack words) */
static inline void cond_cpw(float *d, const float *s) { memcpy(d, s, 4); }

void Cond_SetNowCond(unsigned char elmtType, unsigned char condID) { s_NowCond[elmtType] = condID; }

float __cond_IntpEaseOut(__cond_element_st *pElmt)
{
    float t = ps2_div((float)pElmt->nowFrame, (float)pElmt->intpFrame);
    return t * t;
}

float __cond_IntpEaseIn(__cond_element_st *pElmt)
{
    float t = ps2_div((float)pElmt->nowFrame, (float)pElmt->intpFrame);
    return (2.0f - t) * t;
}

float __cond_IntpPulse(__cond_element_st *pElmt)
{
    return pElmt->intpFrame < (int)((unsigned int)pElmt->nowFrame << 1) ? 1.0f : 0.0f;
}

float __cond_IntpSin(__cond_element_st *pElmt)
{
    return fsmath_SinVU0(1.5707964f * ps2_div((float)pElmt->nowFrame, (float)pElmt->intpFrame));
}

float __cond_IntpLinear(__cond_element_st *pElmt) { return ps2_div((float)pElmt->nowFrame, (float)pElmt->intpFrame); }

/* 16 screen effects x 4 parameter sets: equal types blend colours, alpha fix values and values; a type change
 * takes the new set's type values (blending only when neither side is 0) */
static void __cond_IntpPfxCond(StMapScrEffSt *pCngPfx, StMapScrEffSt *pInPfx, StMapScrEffSt *pOutPfx, float rate)
{
    PS2_FRAME(0xf0);
    float inRate = 1.0f - rate;
    for (int i = 0; i < 16; i++, pCngPfx++, pInPfx++, pOutPfx++) {
        for (int j = 0; j < 4; j++) {
            StMapScrEffParamSt *pPfxParam = &pCngPfx->set[j], *pIn = &pInPfx->set[j], *pOut = &pOutPfx->set[j];
            int isBlend;
            if (pIn->type == pOut->type) {
                pPfxParam->type = pIn->type;
                if (!pIn->type) continue;
                isBlend = 1;
            } else if (!pIn->type) {
                __cond_SetEachTypeValue(pPfxParam, pOut);
                isBlend = 0;
            } else if (!pOut->type) {
                __cond_SetEachTypeValue(pPfxParam, pIn);
                isBlend = 0;
            } else {
                __cond_SetEachTypeValue(pPfxParam, pOut);
                isBlend = 1;
            }
            __cond_SetPfxColor(pPfxParam, pIn, pOut, inRate, rate);
            if (!isBlend) continue;
            for (int k = 0; k < 3; k++)
                pPfxParam->alpha[k].fix = (unsigned char)cond_ftou(
                    (float)pIn->alpha[k].fix * inRate + (float)pOut->alpha[k].fix * rate + 0.001f);
            PS2_STACK(float[4], vin, 0xe0);
            PS2_STACK(float[4], vout, 0xd0);
            PS2_STACK(float[4], vsum, 0xc0);
            PS2_STACK(int[4], vi, 0xb0);
            for (int k = 0; k < 4; k++) {
                vin[k] = (float)pIn->value[k] * inRate;
                vout[k] = (float)pOut->value[k] * rate;
            }
            for (int k = 0; k < 4; k++) vsum[k] = vin[k] + vout[k];
            for (int k = 0; k < 4; k++) vsum[k] = vsum[k] * 1.001f;
            for (int k = 0; k < 4; k++) vi[k] = ps2_ftoi(vsum[k]);
            for (int k = 0; k < 4; k++) pPfxParam->value[k] = (unsigned char)vi[k];
        }
    }
}

/* 16 fogs: colour (VU: itof, .xyz scale, .xyz add -> w = the in colour's w, ftoi), near / far / min / max, then
 * the fog coefficients fog[2] / fog[3] from the new parameters */
static void __cond_IntpFogCond(_map_fog_st *pCngFog, _map_fogparam_st *pCngFogParam, _map_fog_st *pInFog,
                               _map_fogparam_st *pInFogParam, _map_fog_st *pOutFog, _map_fogparam_st *pOutFogParam,
                               float rate)
{
    PS2_FRAME(0x30);
    float inRate = 1.0f - rate;
    for (int i = 0; i < 16; i++, pCngFog++, pCngFogParam++, pInFog++, pInFogParam++, pOutFog++, pOutFogParam++) {
        PS2_STACK(float[4], a, 0x20);
        PS2_STACK(float[4], b, 0x10);
        PS2_STACK(float[4], s, 0x00);
        for (int k = 0; k < 4; k++) a[k] = (float)pInFog->fog_color[k];
        for (int k = 0; k < 3; k++) a[k] = a[k] * inRate;
        for (int k = 0; k < 4; k++) b[k] = (float)pOutFog->fog_color[k];
        for (int k = 0; k < 3; k++) b[k] = b[k] * rate;
        for (int k = 0; k < 3; k++) s[k] = a[k] + b[k];
        cond_cpw(&s[3], &a[3]);
        int c[4];
        for (int k = 0; k < 4; k++) c[k] = ps2_ftoi(s[k]);
        memcpy(pCngFog->fog_color, c, sizeof c);
        for (int k = 0; k < 3; k++) pCngFog->fog_color[k] = (unsigned char)pCngFog->fog_color[k];
        pCngFogParam->fnear = pInFogParam->fnear * inRate + pOutFogParam->fnear * rate;
        pCngFogParam->ffar = pInFogParam->ffar * inRate + pOutFogParam->ffar * rate;
        pCngFogParam->min = pInFogParam->min * inRate + pOutFogParam->min * rate;
        pCngFogParam->max = pInFogParam->max * inRate + pOutFogParam->max * rate;
        float fnear = pCngFogParam->fnear, ffar = pCngFogParam->ffar;
        float mn = 255.0f - pCngFogParam->min, mx = 255.0f - pCngFogParam->max;
        float k = ps2_div(mx - mn, ffar - fnear);
        pCngFog->fog[2] = 0.5f * (k * (fnear + ffar) + (mn + mx));
        pCngFog->fog[3] = -(k * (fnear * ffar));
    }
}

/* 16 lights: dir rows 0..2 and col rows 0..3 blended .xyz, w = the in light's w */
static void __cond_IntpLitCond(_map_light_st *pCngLit, _map_light_st *pInLit, _map_light_st *pOutLit, float rate)
{
    PS2_FRAME(0x20);
    float inRate = 1.0f - rate;
    for (int i = 0; i < 16; i++, pCngLit++, pInLit++, pOutLit++) {
        for (int r = 0; r < 7; r++) {
            float *in = r < 3 ? pInLit->dir[r] : pInLit->col[r - 3];
            float *out = r < 3 ? pOutLit->dir[r] : pOutLit->col[r - 3];
            float *cng = r < 3 ? pCngLit->dir[r] : pCngLit->col[r - 3];
            PS2_STACK(float[4], a, 0x10);
            PS2_STACK(float[4], b, 0x00);
            for (int k = 0; k < 3; k++) a[k] = in[k] * inRate;
            cond_cpw(&a[3], &in[3]);
            for (int k = 0; k < 3; k++) b[k] = out[k] * rate;
            cond_cpw(&b[3], &out[3]);
            for (int k = 0; k < 3; k++) cng[k] = a[k] + b[k];
            cond_cpw(&cng[3], &a[3]);
        }
    }
}

static void __cond_SetEachTypeValue(StMapScrEffParamSt *pPfxParam, StMapScrEffParamSt *pRefPfxParam)
{
    pPfxParam->type = pRefPfxParam->type;
    for (int i = 0; i < 3; i++) {
        pPfxParam->alpha[i].a = pRefPfxParam->alpha[i].a;
        pPfxParam->alpha[i].b = pRefPfxParam->alpha[i].b;
        pPfxParam->alpha[i].c = pRefPfxParam->alpha[i].c;
        pPfxParam->alpha[i].d = pRefPfxParam->alpha[i].d;
        pPfxParam->alpha[i].fix = pRefPfxParam->alpha[i].fix;
    }
    for (int i = 0; i < 4; i++) pPfxParam->value[i] = pRefPfxParam->value[i];
}

static void __cond_SetPfxColor(StMapScrEffParamSt *pPfxParam, StMapScrEffParamSt *pInPfxParam,
                               StMapScrEffParamSt *pOutPfxParam, float inRate, float outRate)
{
    PS2_FRAME(0x40);
    for (int c = 0; c < 3; c++) {
        PS2_STACK(float[4], vin, 0x30);
        PS2_STACK(float[4], vout, 0x20);
        PS2_STACK(float[4], vsum, 0x10);
        PS2_STACK(int[4], vi, 0x00);
        for (int k = 0; k < 4; k++) {
            vin[k] = inRate * (float)pInPfxParam->color[c].rgba[k];
            vout[k] = outRate * (float)pOutPfxParam->color[c].rgba[k];
        }
        for (int k = 0; k < 4; k++) vsum[k] = vin[k] + vout[k];
        for (int k = 0; k < 4; k++) vsum[k] = vsum[k] * 1.001f;
        for (int k = 0; k < 4; k++) vi[k] = ps2_ftoi(vsum[k]);
        for (int k = 0; k < 4; k++) pPfxParam->color[c].rgba[k] = (unsigned char)vi[k];
    }
}

/* background colours: frmCol -> colBg (float), sfxCol -> colSfx (rgb), lfCol -> colLf (rgba); `a` / `b` are the
 * original's stack temporaries: colBg[3] takes a[3] before anything wrote it (stale stack), colSfx's ftoi too */
static void cond_bgc(float *a, float *b, float *s, int *v, const unsigned char *in, const unsigned char *frmOut,
                     const unsigned char *sfxOut, const unsigned char *lfOut, float inRate, float outRate, int lfXyzw)
{
    for (int k = 0; k < 3; k++) {
        a[k] = inRate * (float)in[k];
        b[k] = outRate * (float)frmOut[k];
    }
    for (int k = 0; k < 3; k++) CopyOfSysInfo.colBg[k] = a[k] + b[k];
    cond_cpw(&CopyOfSysInfo.colBg[3], &a[3]);
    for (int k = 0; k < 3; k++) {
        a[k] = inRate * (float)in[4 + k];
        b[k] = outRate * (float)sfxOut[k];
    }
    for (int k = 0; k < 3; k++) s[k] = a[k] + b[k];
    cond_cpw(&s[3], &a[3]);
    for (int k = 0; k < 4; k++) v[k] = ps2_ftoi(s[k]);
    for (int k = 0; k < 3; k++) CopyOfSysInfo.colSfx.rgba[k] = (unsigned char)v[k];
    for (int k = 0; k < 4; k++) {
        a[k] = inRate * (float)in[8 + k];
        b[k] = outRate * (float)lfOut[k];
    }
    for (int k = 0; k < 3; k++) s[k] = a[k] + b[k];
    s[3] = lfXyzw ? a[3] + b[3] : a[3];
    for (int k = 0; k < 4; k++) v[k] = ps2_ftoi(s[k]);
    for (int k = 0; k < 4; k++) CopyOfSysInfo.colLf.rgba[k] = (unsigned char)v[k];
    DF_SetDBuffClrCol(&DBUFF, CopyOfSysInfo.colBg);
    DF_SetDBuffClrCol(&DBUFF2, CopyOfSysInfo.colBg);
}

/* blend the current background colours toward bgColor[blendID] (current colours staged in the scratchpad) */
static void __cond_BlendBgc(__cond_element_st *pBgc)
{
    if (!pBgc->blend.isBlend) return;
    PS2_FRAME(0xc0);
    float outRate = 0.01f * (float)pBgc->blend.blendRate;
    float inRate = 1.0f - outRate;
    BgColorSt *bg = &Array->bgColor[pBgc->blend.blendID];
    unsigned char *spr = (unsigned char *)PS2_SPR(0);
    for (int k = 0; k < 3; k++) spr[k] = (unsigned char)cond_ftou(CopyOfSysInfo.colBg[k]);
    memcpy(spr + 4, &CopyOfSysInfo.colSfx, 4);
    memcpy(spr + 8, &CopyOfSysInfo.colLf, 4);
    PS2_STACK(float[4], a, 0xb0);
    PS2_STACK(float[4], b, 0xa0);
    PS2_STACK(float[4], s, 0x90);
    PS2_STACK(int[4], v, 0x80);
    cond_bgc(a, b, s, v, spr, bg->frmCol.rgba, bg->sfxCol.rgba, bg->lfCol.rgba, inRate, outRate, 0);
}

static void __cond_BlendPfx(__cond_element_st *pPfx)
{
    if (!pPfx->blend.isBlend) return;
    PS2_FRAME(0x40);
    float rate = 0.01f * (float)pPfx->blend.blendRate;
    StMapScrEffSt *pCngPfx = StgInfo.pscreff;
    StMapScrEffSt *pOut = Array->screff[pPfx->blend.blendID];
    memcpy(PS2_SPR(0), pCngPfx, 0x600);
    __cond_IntpPfxCond(pCngPfx, (StMapScrEffSt *)PS2_SPR(0), pOut, rate);
}

static void __cond_BlendFog(__cond_element_st *pFog)
{
    if (!pFog->blend.isBlend) return;
    PS2_FRAME(0x60);
    float rate = 0.01f * (float)pFog->blend.blendRate;
    _map_fog_st *pCngFog = StgInfo.fog;
    _map_fogparam_st *pCngFogParam = StgInfo.fogparam;
    int id = pFog->blend.blendID;
    memcpy(PS2_SPR(0), pCngFog, 0x200);
    memcpy(PS2_SPR(0x200), pCngFogParam, 0x100);
    __cond_IntpFogCond(pCngFog, pCngFogParam, (_map_fog_st *)PS2_SPR(0), (_map_fogparam_st *)PS2_SPR(0x200),
                       Array->fogCond[id], Array->fogParamCond[id], rate);
}

static void __cond_BlendLit(__cond_element_st *pLit)
{
    if (!pLit->blend.isBlend) return;
    PS2_FRAME(0x40);
    float rate = 0.01f * (float)pLit->blend.blendRate;
    _map_light_st *pCngLit = StgInfo.light;
    _map_light_st *pOut = Array->litCond[pLit->blend.blendID];
    memcpy(PS2_SPR(0), pCngLit, 0x800);
    __cond_IntpLitCond(pCngLit, (_map_light_st *)PS2_SPR(0), pOut, rate);
}

static void __cond_ChangeBgc(__cond_element_st *pBgc)
{
    PS2_FRAME(0x70);
    float rate = gcall(cond_intp_func[pBgc->intpType], pBgc);
    float inRate = 1.0f - rate;
    BgColorSt *in = &Array->bgColor[pBgc->inID], *out = &Array->bgColor[pBgc->outID];
    unsigned char inCol[12];
    memcpy(inCol, in, 12);
    PS2_STACK(float[4], a, 0x60);
    PS2_STACK(float[4], b, 0x50);
    PS2_STACK(float[4], s, 0x40);
    PS2_STACK(int[4], v, 0x30);
    cond_bgc(a, b, s, v, inCol, out->frmCol.rgba, out->sfxCol.rgba, out->lfCol.rgba, inRate, rate, 1);
}

/* at the last frame only the first screen effect's sets are reset to the out condition (see quirks) */
static void __cond_ChangePfx(__cond_element_st *pPfx)
{
    PS2_FRAME(0x40);
    float rate = gcall(cond_intp_func[pPfx->intpType], pPfx);
    StMapScrEffSt *pCngPfx = StgInfo.pscreff;
    StMapScrEffSt *pOut = Array->screff[pPfx->outID];
    __cond_IntpPfxCond(pCngPfx, Array->screff[pPfx->inID], pOut, rate);
    if (pPfx->nowFrame < pPfx->intpFrame) return;
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 4; j++) __cond_SetEachTypeValue(&pCngPfx->set[j], &pOut->set[j]);
}

static void __cond_ChangeFog(__cond_element_st *pFog)
{
    PS2_FRAME(0x20);
    float rate = gcall(cond_intp_func[pFog->intpType], pFog);
    __cond_IntpFogCond(StgInfo.fog, StgInfo.fogparam, Array->fogCond[pFog->inID], Array->fogParamCond[pFog->inID],
                       Array->fogCond[pFog->outID], Array->fogParamCond[pFog->outID], rate);
}

static void __cond_ChangeLit(__cond_element_st *pLit)
{
    PS2_FRAME(0x20);
    float rate = gcall(cond_intp_func[pLit->intpType], pLit);
    __cond_IntpLitCond(StgInfo.light, Array->litCond[pLit->inID], Array->litCond[pLit->outID], rate);
}

int Cond_Set(int elmtType, int intpType, int inID, int outID, int intpFrame)
{
    if (elmtType < 0 || elmtType >= 4) return 0;
    if (intpType < 0 || intpType >= 5) return 0;
    __cond_element_st *p = &s_CondMng.arrElmt[elmtType];
    p->inID = (unsigned char)inID;
    p->outID = (unsigned char)outID;
    p->intpType = (unsigned char)intpType;
    p->intpFrame = intpFrame;
    p->nowFrame = 0;
    s_CondMng.elmtRunFlag |= s_ElmtRunFlag[elmtType];
    return 1;
}

void Cond_Run(int runType, int bOnOff)
{
    if (bOnOff) s_CondMng.runFlag |= s_RunFlag[runType];
    else s_CondMng.runFlag &= ~s_RunFlag[runType];
}

void Cond_Proc(void)
{
    PS2_FRAME(0x50);
    if (s_CondMng.runFlag & 1) {
        for (int i = 0; i < 4; i++) {
            __cond_element_st *p = &s_CondMng.arrElmt[i];
            if (!(s_CondMng.elmtRunFlag & s_ElmtRunFlag[i])) continue;
            gcall(s_ElmtIntpFunc[i], p);
            p->nowFrame++;
            if (p->intpFrame < p->nowFrame) s_CondMng.elmtRunFlag &= ~s_ElmtRunFlag[i];
        }
    }
    if (s_CondMng.runFlag & 2)
        for (int i = 0; i < 4; i++) gcall(s_ElmtBlendFunc[i], &s_CondMng.arrElmt[i]);
    if (!s_CondMng.elmtRunFlag) s_CondMng.runFlag &= 0xfffe;
}

void Cond_Init(void)
{
    memset(&s_CondMng, 0, 0x50);
    memset(s_NowCond, 0, 4);
}
