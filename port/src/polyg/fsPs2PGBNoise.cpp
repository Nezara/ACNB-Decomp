/* src/polyg/fsPs2PGBNoise.cpp — Perlin-style 1D gradient noise for the polygon-movie ("PolyG") camera / object shake.
 * fsPs2PGBNoise_Init builds the permutation and gradient tables of the _FS_CALC_NOISE_ST inside the movie player
 * (srand(0) + 512 rand() draws: every stage load resets the global newlib RNG - kept), NoiseAtTime evaluates a
 * 3-channel noise vector for a playback time (ramp in / out smoothstep, optional fractal sum via GetNoiseOnFractal,
 * which caches pow(lacunarity^i, -H) through the soft-double libm pow). */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

static_assert(offsetof(_FS_CALC_NOISE_ST, fPrmTbl) == 0x808, "_FS_CALC_NOISE_ST.fPrmTbl");
static_assert(offsetof(_FS_CALC_NOISE_ST, fExponent) == 0x1010, "_FS_CALC_NOISE_ST.fExponent");
static_assert(offsetof(_FS_CALC_NOISE_ST, dwFlag) == 0x10e0, "_FS_CALC_NOISE_ST.dwFlag");
static_assert(offsetof(_FS_NOISEST_NOISE_PARAM, stFgPos) == 0x18, "_FS_NOISEST_NOISE_PARAM.stFgPos");
static_assert(offsetof(_FS_NOISEST_NOISE_PARAM, fStr) == 0x20, "_FS_NOISEST_NOISE_PARAM.fStr");

/* The inlined smooth-noise sample (identical ops at all three sites): lattice index = (u8)(u32)(p + 4096) (MWCC
 * float -> u32), t = p - (float)(int)p, gradients g0 / g1 of the two neighbours,
 * noise = t*g0 + ((3 - 2t) * (t*t)) * ((t - 1)*g1 - t*g0). */
static inline float pgbnoise_smooth(const _FS_CALC_NOISE_ST *pNoise, float p)
{
    float u = 4096.0f + p;
    unsigned int idx;
    if (2147483648.0f <= u) idx = (unsigned int)ps2_ftoi(u - 2147483648.0f) | 0x80000000u;
    else idx = (unsigned int)ps2_ftoi(u);
    idx &= 0xff;
    float t = p - (float)ps2_ftoi(p);
    float g0 = pNoise->fPrmTbl[pNoise->dwPerm[idx]];
    float g1 = pNoise->fPrmTbl[pNoise->dwPerm[(idx + 1) & 0xff]];
    float a = t * g0;
    float b = (t - 1.0f) * g1;
    float d = b - a;
    float w = (3.0f - 2.0f * t) * (t * t);
    return a + w * d;
}

static float fsPs2PGBNoise_GetNoiseOnFractal(_FS_CALC_NOISE_ST *pNoise, float fPt, float fH, float fLacunarity,
                                             float fOctaves)
{
    PS2_FRAME(0x70);
    /* fExponent has 51 slots; an fOctaves >= 51 would run on into fLastH / dwFlag (only called with 6.0f) */
    float *exponent = (float *)((char *)pNoise + 0x1010);
    float fRemainder = fOctaves - (float)ps2_ftoi(fOctaves);
    float value = 0.0f;

    if (!(pNoise->dwFlag & 1) || !(pNoise->fLastH == fH)) {
        pNoise->fLastH = fH;
        pNoise->dwFlag |= 1;   /* marked valid even when the loop below is skipped */
        float f = 1.0f;
        if (0.0f <= fOctaves) {
            float negH = -fH;
            unsigned int i = 0;
            do {
                unsigned long long fd = ps2_extendsfdf2(ps2_fbits(f));
                unsigned long long hd = ps2_extendsfdf2(ps2_fbits(negH));
                unsigned int e = ps2_truncdfsf2(ps2_pow(fd, hd));
                f = f * fLacunarity;
                i++;
                exponent[i - 1] = ps2_bitsf(e);
            } while ((float)i <= fOctaves);
        }
    }

    unsigned int i = 0;
    if (0.0f < fOctaves) {
        do {
            float n = pgbnoise_smooth(pNoise, fPt);
            float e = exponent[i];
            fPt = fPt * fLacunarity;
            i++;
            value = value + e * n;
        } while ((float)i < fOctaves);
    }
    if (!(fRemainder == 0.0f)) {
        float n = pgbnoise_smooth(pNoise, fPt);
        value = value + exponent[i] * (fRemainder * n);
    }
    return value;
}

_HRESULT fsPs2PGBNoise_NoiseAtTime(_FS_CALC_NOISE_ST *pNoise, _D3DXVECTOR4 *vOut, float fNowTime, float fTimeAll,
                                   _FS_NOISEST_NOISE_PARAM *lpPrmAry)
{
    PS2_FRAME(0x90);
    if (!vOut || fTimeAll <= 0.0f || !lpPrmAry) return 1;

    float fRamp = 1.0f;
    float fFreq = lpPrmAry->fFreq;
    float fRough = lpPrmAry->fRough;
    unsigned int dwFractal = lpPrmAry->dwFractal;
    float fRampIn = (float)lpPrmAry->dwRampIn;
    float fRampOut = (float)lpPrmAry->dwRampOut;
    unsigned char fg;
    memcpy(&fg, &lpPrmAry->stFgPos, 1);   /* lbu of the stack copy: bit0 -> [0], bit1 -> [1], bit2 -> [2] */
    float fStr[4] = {lpPrmAry->fStr.x, lpPrmAry->fStr.y, lpPrmAry->fStr.z, lpPrmAry->fStr.w};
    float fValAry[3];

    if (fNowTime < 0.0f) fNowTime = 0.0f;
    if (!(fNowTime <= fTimeAll)) fNowTime = fTimeAll;
    fNowTime = fNowTime * 0x1.333334p+1f;   /* 2.4000001f */
    fTimeAll = fTimeAll * 0x1.333334p+1f;

    if (fNowTime < fRampIn) {
        float t = ps2_div(fNowTime, fRampIn);
        float w = (3.0f - 2.0f * t) * (t * t);
        fRamp = fRamp * w;
    }
    if (!(fNowTime <= fTimeAll - fRampOut)) {
        float t = ps2_div(fTimeAll - fNowTime, fRampOut);
        float w = (3.0f - 2.0f * t) * (t * t);
        fRamp = fRamp * w;
    }

    if (dwFractal != 0) {
        float fH = 1.0f - fRough;
        float x = (0x1.89374cp-7f /* 0.01199f */ * fNowTime) * fFreq;
        for (unsigned int k = 0; k < 3; k++) {
            float p = x + (float)pNoise->dwPerm[(lpPrmAry->dwSeed + k) & 0xff];
            fValAry[k] = fsPs2PGBNoise_GetNoiseOnFractal(pNoise, p, fH, 2.0f, 6.0f);
        }
    } else {
        unsigned int seed = lpPrmAry->dwSeed;
        float x = (0x1.89374cp-7f * fNowTime) * fFreq;
        for (unsigned int k = 0; k < 3; k++, seed++) {
            float p = x + (float)pNoise->dwPerm[seed & 0xff];
            fValAry[k] = pgbnoise_smooth(pNoise, p);
        }
    }

    if (fg & 1) fValAry[0] = fValAry[0] + 0.5f;
    if (fg >> 1 & 1) fValAry[1] = fValAry[1] + 0.5f;
    if (fg >> 2 & 1) fValAry[2] = fValAry[2] + 0.5f;

    vOut->x = fStr[0] * (fValAry[0] * fRamp);
    vOut->y = fStr[1] * (fValAry[1] * fRamp);
    vOut->z = fStr[2] * (fValAry[2] * fRamp);
    return 0;
}

_HRESULT fsPs2PGBNoise_Init(_FS_CALC_NOISE_ST *pNoise)
{
    PS2_FRAME(0x50);
    ps2_srand(0);
    for (unsigned int i = 0; i < 0x100; i++) {
        pNoise->dwPerm[i] = i;
        int r = ps2_rand() % 512;
        pNoise->fPrmTbl[i] = (float)(r - 256) / 256.0f;
    }
    for (unsigned int i = 0x100; i-- != 0;) {
        unsigned int t = pNoise->dwPerm[i];
        int j = ps2_rand() % 256;
        pNoise->dwPerm[i] = pNoise->dwPerm[j];
        pNoise->dwPerm[j] = t;
    }
    /* copy 256 entries (bit copies of the floats), then the two extra entries 512 / 513 = 256 / 257 */
    for (unsigned int k = 0; k < 0x102; k++) {
        pNoise->dwPerm[0x100 + k] = pNoise->dwPerm[k];
        memcpy(&pNoise->fPrmTbl[0x100 + k], &pNoise->fPrmTbl[k], 4);
    }
    pNoise->dwFlag = 0;
    return 0;
}
