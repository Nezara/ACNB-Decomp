/* src/stage/ecmfield.cpp - ECM field: up to 64 registered electronic counter-measure noise sources (gimmicks,
 * spells, enemies, scripts), each owned by a team ("belonging" 1..6). Every frame each source ramps its level
 * towards its target (down to 0 and freed once unregistered); per team the strongest source counts fully and the
 * rest at half (noiseTotal), and the noise a team suffers (noiseNuisc, read by the AC controller for radar / lock)
 * is the strongest other team plus 0.4 of the remaining ones. The module works through the pointer this_. */
#include "acnb/ps2fp.h"

#ifdef ACNB_SOURCEPORT
StEcmField *this_ = &s_ecmfield;
#endif

/* float -> u16 through the unsigned conversion */
static inline unsigned short ecm_u16(float f)
{
    return !(2147483648.0f <= f) ? (unsigned short)ps2_ftoi(f)
                                 : (unsigned short)((unsigned int)ps2_ftoi(f - 2147483648.0f) | 0x80000000u);
}

/* (belonging is not range-checked: 0 reads noiseTotal[5]) */
unsigned short EcmField_GetEcmNoiseLv(unsigned char belonging)
{
    return this_->noiseNuisc[(signed char)(belonging - 1)];
}

/* only the target drops to 0: the source fades out and frees its slot */
void EcmField_UnRegisterEcm(unsigned char handle)
{
    int i = handle - 1;
    if (handle != 0 && !(i < 0) && i < 0x40) this_->tblecm[i].noiseEnd = 0;
}

/* returns the handle (slot + 1) or 0; the level rises by noise / 40 per frame */
unsigned char EcmField_RegisterEcm(unsigned char type, unsigned char belonging, short param, unsigned short noise)
{
    StEcmField *f = this_;
    int idx = -1;
    for (int i = 0; i < 0x40; i++) {
        if (f->tblecm[i].type == 0) {
            idx = i;
            break;
        }
    }
    if (idx == -1) return 0;
    if (belonging == 0 || !(belonging < 7)) return 0;
    StEcm *e = &f->tblecm[idx];
    e->type = type;
    e->ibelong = (char)(belonging - 1);
    e->param = param;
    e->noiseEnd = noise;
    e->noiseCur = 0;
    e->noiseInc = ecm_u16((float)e->noiseEnd / 40.0f);
    if (!(e->noiseInc > 0)) e->noiseInc = 1;
    return (unsigned char)(idx + 1);
}

int EcmField_Exec(void)
{
    PS2_FRAME(0x10);
    __CalcTotalNoiseLv();
    return 1;
}

void EcmField_Cleanup(void)
{
    ZeroMemory(&s_ecmfield, 0x298);
}

int EcmField_Init(void)
{
    PS2_FRAME(0x10);
    ZeroMemory(&s_ecmfield, 0x298);
    return 1;
}

static void __CalcTotalNoiseLv(void)
{
    PS2_FRAME(0x50);
    int max[6], sum[6];
    ZeroMemory(max, 0x18);
    ZeroMemory(sum, 0x18);
    for (int i = 0; i < 0x40; i++) {
        StEcm *e = &this_->tblecm[i];
        if (!e->type) continue;
        if (e->noiseEnd == 0) {
            int d = (int)e->noiseCur - (int)e->noiseInc;
            if (d <= 0) e->type = 0;   /* (freed, but its old level still counts this frame) */
            else e->noiseCur = (unsigned short)d;
        } else {
            int s = (int)e->noiseCur + (int)e->noiseInc;
            if (s < (int)e->noiseEnd) e->noiseCur = (unsigned short)s;
            else e->noiseCur = e->noiseEnd;
        }
        int g = e->ibelong;
        if (max[g] < (int)e->noiseCur) max[g] = e->noiseCur;
        sum[g] += e->noiseCur;
    }
    for (int g = 0; g < 6; g++) {
        int m = max[g];
        sum[g] -= m;
        this_->noiseTotal[g] = ecm_u16((float)m + 0.5f * (float)sum[g]);
    }
    for (int i = 0; i < 6; i++) {
        int max2 = 0, sum2 = 0;
        for (int j = 0; j < 6; j++) {
            if (i == j) continue;
            int v = this_->noiseTotal[j];
            if (max2 < v) max2 = v;
            sum2 += this_->noiseTotal[j];
        }
        sum2 -= max2;
        this_->noiseNuisc[i] = ecm_u16((float)max2 + ps2_u2f(0x3ecccccd) * (float)sum2);   /* 0.4f */
    }
}
