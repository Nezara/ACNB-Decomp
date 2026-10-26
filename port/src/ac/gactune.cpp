/* src/ac/gactune.cpp - AC parts tuning (whole file): applies the tune levels of StAcAsmTune (8 parts x 5 slots) to
 * the part parameter structs. Each tuning slot of a part names a parameter (item_id) and an amount (volume); a slot
 * with a non-zero level moves its parameter by __TuneI / __TuneF (sum of (4 - i / 3) over the levels, * volume / 28),
 * clamped to [0, 0x10000]. GAcTune_TuneAll returns 0 if any slot names a parameter its part does not have.
 * original: each tuner also fills a debug block (name, slot, item_id) on its stack that __TuneI never reads (only
 * when s_isDebugOut is set is its address passed, as an extra argument the tune helpers ignore) - not mirrored. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"

/* sum over the levels of 4 - i / 3 (truncating), the asm unrolls this 8x */
static inline int tune_sum(int lv)
{
    int sum = 0;
    for (int i = 0; i < lv; i++) sum += 4 - i / 3;
    return sum;
}

static float __TuneF(float src, float value, int lv, float min, float max)
{
    PS2_FRAME(0x50);
    float sim = src + (float)tune_sum(lv) * value / 28.0f;
    if (sim < min) sim = min;
    if (!(sim <= max)) sim = max;
    return sim;
}

static int __TuneI(int src, int value, int lv, int min, int max)
{
    PS2_FRAME(0x90);
    int sim = src + ps2_ftoi((float)(int)((unsigned int)tune_sum(lv) * (unsigned int)value) / 28.0f);
    if (sim < min) sim = min;
    if (max < sim) sim = max;
    return sim;
}

/* one u16 parameter; the result is stored as a halfword (sic: a clamped 0x10000 becomes 0) */
static inline void tune_u16(unsigned short *field, int value, int lv)
{
    *field = (unsigned short)__TuneI(*field, value, lv, 0, 0x10000);
}

int GAcTune_TuneRadiator(PartsParamRadiatorSt *pparam, StAcAsmTune *ptune)
{
    PS2_FRAME(0x90);
    int isNoError = 1;
    for (int i = 0; i < 5; i++) {
        int lv = ptune->parts[7].lv[i];
        if (!lv) continue;
        PartsParamTuningSt *ptprm = &pparam->basic.tuning[i];
        if (!ptprm->item_id) continue;
        switch (ptprm->item_id) {
        case 1: tune_u16(&pparam->basic.wp, -(int)ptprm->volume, lv); break;
        case 0xf: tune_u16(&pparam->basic.ep, -(int)ptprm->volume, lv); break;
        case 0x10: tune_u16(&pparam->cooling, ptprm->volume, lv); break;
        default: isNoError = 0; break;
        }
    }
    return isNoError;
}

int GAcTune_TuneGenerator(PartsParamGeneratorSt *pparam, StAcAsmTune *ptune)
{
    PS2_FRAME(0x90);
    int isNoError = 1;
    for (int i = 0; i < 5; i++) {
        int lv = ptune->parts[6].lv[i];
        if (!lv) continue;
        PartsParamTuningSt *ptprm = &pparam->basic.tuning[i];
        if (!ptprm->item_id) continue;
        switch (ptprm->item_id) {
        case 1: tune_u16(&pparam->basic.wp, -(int)ptprm->volume, lv); break;
        case 0xd: tune_u16(&pparam->power, ptprm->volume, lv); break;
        case 0xe: tune_u16(&pparam->charge_max, ptprm->volume, lv); break;
        default: isNoError = 0; break;
        }
    }
    return isNoError;
}

int GAcTune_TuneBooster(PartsParamBoosterSt *pparam, StAcAsmTune *ptune)
{
    PS2_FRAME(0x90);
    int isNoError = 1;
    for (int i = 0; i < 5; i++) {
        int lv = ptune->parts[4].lv[i];
        if (!lv) continue;
        PartsParamTuningSt *ptprm = &pparam->basic.tuning[i];
        if (!ptprm->item_id) continue;
        switch (ptprm->item_id) {
        case 1: tune_u16(&pparam->basic.wp, -(int)ptprm->volume, lv); break;
        case 0xb: tune_u16(&pparam->power, ptprm->volume, lv); break;
        case 0xc: tune_u16(&pparam->boost_heat, -(int)ptprm->volume, lv); break;
        default: isNoError = 0; break;
        }
    }
    return isNoError;
}

int GAcTune_TuneLeg(PartsParamLegsSt *pparam, StAcAsmTune *ptune)
{
    PS2_FRAME(0x90);
    int isNoError = 1;
    for (int i = 0; i < 5; i++) {
        int lv = ptune->parts[3].lv[i];
        if (!lv) continue;
        PartsParamTuningSt *ptprm = &pparam->basic.tuning[i];
        if (!ptprm->item_id) continue;
        switch (ptprm->item_id) {
        case 1: tune_u16(&pparam->basic.wp, -(int)ptprm->volume, lv); break;
        case 2: tune_u16(&pparam->defense.cooling, ptprm->volume, lv); break;
        case 3: tune_u16(&pparam->defense.shell, ptprm->volume, lv); break;
        case 4: tune_u16(&pparam->defense.energy, ptprm->volume, lv); break;
        case 0x11: tune_u16(&pparam->max_wp, ptprm->volume, lv); break;
        case 0xa: tune_u16(&pparam->boost_power, ptprm->volume, lv); break;
        default: isNoError = 0; break;
        }
    }
    return isNoError;
}

int GAcTune_TuneArm(PartsParamArmsSt *pparam, StAcAsmTune *ptune)
{
    PS2_FRAME(0xb0);
    int isNoError = 1;
    for (int i = 0; i < 5; i++) {
        int lv = ptune->parts[2].lv[i];
        if (!lv) continue;
        PartsParamTuningSt *ptprm = &pparam->basic.tuning[i];
        if (!ptprm->item_id) continue;
        switch (ptprm->item_id) {
        case 1: tune_u16(&pparam->basic.wp, -(int)ptprm->volume, lv); break;
        case 2: tune_u16(&pparam->defense.cooling, ptprm->volume, lv); break;
        case 3: tune_u16(&pparam->defense.shell, ptprm->volume, lv); break;
        case 4: tune_u16(&pparam->defense.energy, ptprm->volume, lv); break;
        case 7:
            pparam->recoilctrl_accuracy = __TuneF(pparam->recoilctrl_accuracy, 0.001f * -(float)ptprm->volume, lv,
                                                  0.0f, 65536.0f);
            break;
        case 8:
            tune_u16(&pparam->gndbld_wait, -(int)ptprm->volume, lv);
            tune_u16(&pparam->flybld_wait, -(int)ptprm->volume, lv);
            break;
        default: isNoError = 0; break;
        }
    }
    return isNoError;
}

int GAcTune_TuneCore(PartsParamCoreSt *pparam, StAcAsmTune *ptune)
{
    PS2_FRAME(0x90);
    int isNoError = 1;
    for (int i = 0; i < 5; i++) {
        int lv = ptune->parts[1].lv[i];
        if (!lv) continue;
        PartsParamTuningSt *ptprm = &pparam->basic.tuning[i];
        if (!ptprm->item_id) continue;
        switch (ptprm->item_id) {
        case 1: tune_u16(&pparam->basic.wp, -(int)ptprm->volume, lv); break;
        case 2: tune_u16(&pparam->defense.cooling, ptprm->volume, lv); break;
        case 3: tune_u16(&pparam->defense.shell, ptprm->volume, lv); break;
        case 4: tune_u16(&pparam->defense.energy, ptprm->volume, lv); break;
        case 6: tune_u16(&pparam->max_wp, ptprm->volume, lv); break;
        default: isNoError = 0; break;
        }
    }
    return isNoError;
}

int GAcTune_TuneHead(PartsParamHeadSt *pparam, StAcAsmTune *ptune)
{
    PS2_FRAME(0x90);
    int isNoError = 1;
    for (int i = 0; i < 5; i++) {
        int lv = ptune->parts[0].lv[i];
        if (!lv) continue;
        PartsParamTuningSt *ptprm = &pparam->basic.tuning[i];
        if (!ptprm->item_id) continue;
        switch (ptprm->item_id) {
        case 1: tune_u16(&pparam->basic.wp, -(int)ptprm->volume, lv); break;
        case 2: tune_u16(&pparam->defense.cooling, ptprm->volume, lv); break;
        case 3: tune_u16(&pparam->defense.shell, ptprm->volume, lv); break;
        case 4: tune_u16(&pparam->defense.energy, ptprm->volume, lv); break;
        case 5: tune_u16(&pparam->radar_stat.eccm_power, ptprm->volume, lv); break;
        default: isNoError = 0; break;
        }
    }
    return isNoError;
}

int GAcTune_TuneAll(AcPartsDataSt *pparam, StAcAsmTune *ptune)
{
    PS2_FRAME(0x40);
    int isNoError = 1;
    if (!GAcTune_TuneHead(&pparam->head, ptune)) isNoError = 0;
    if (!GAcTune_TuneCore(&pparam->core, ptune)) isNoError = 0;
    if (!GAcTune_TuneArm(&pparam->arms, ptune)) isNoError = 0;
    if (!GAcTune_TuneLeg(&pparam->legs, ptune)) isNoError = 0;
    if (!GAcTune_TuneBooster(&pparam->booster, ptune)) isNoError = 0;
    if (!GAcTune_TuneGenerator(&pparam->gene, ptune)) isNoError = 0;
    if (!GAcTune_TuneRadiator(&pparam->radi, ptune)) isNoError = 0;
    return isNoError;
}
