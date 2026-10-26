/* src/ac/acse.cpp - per-AC sounds: eight looping voice handles (acp->vh, 0x50 = none; 6 shield, 7 water move, the
 * others per leg type) started / updated / stopped through the IOP voice layer, scaled by the AC's volume rate. Each
 * frame acse_CtrlSe ramps the leg motor / turn loops (volume and pitch follow a 0..10000 rate that accelerates while
 * moving / turning and brakes otherwise), drops the shield loop and plays the water-move loop. */
#include "acnb/ps2fp.h"

/* volume scaled by the AC's volume rate, at most 0x7f (no lower clamp) */
static inline int acse_scale_vol(AcPlayerSt *acp, int vol)
{
    int v = ps2_ftoi((float)vol * AcCtrl_GetSe(acp)->vol_rate);
    if (v >= 0x80) v = 0x7f;
    return v;
}

static inline void acse_stop(AcPlayerSt *acp, int vhid)
{
    if (acp->vh[vhid] < 0x50) {
        Voice_Stop(acp->vh[vhid], 1);
        acp->vh[vhid] = 0x50;
    }
}

void acse_StopLoopSe(AcPlayerSt *acp, int vhid, int fade)
{
    PS2_FRAME(0x20);
    unsigned char *vh = &acp->vh[vhid];
    if (*vh < 0x50) {
        Voice_Stop(*vh, fade != 0);
        *vh = 0x50;
    }
}

int acse_SetLoopSeParamPitch(AcPlayerSt *acp, int vhid, int pitch)
{
    PS2_FRAME(0x20);
    if (acp->vh[vhid] < 0x50) DF_SettingPitchBend(acp->vh[vhid], pitch);
    return acp->vh[vhid];
}

int acse_SetLoopSeParamVolDist(AcPlayerSt *acp, int vhid, int vol, float dist)
{
    PS2_FRAME(0x50);
    unsigned char h = acp->vh[vhid];
    if (h < 0x50) Voice_SetLoopVoiceParamVm(h, acse_scale_vol(acp, vol), dist);
    return acp->vh[vhid];
}

/* start the loop if it is not playing, else update its volume / distance and pitch */
int acse_PutLoopSeVolToneDist(AcPlayerSt *acp, int vhid, int seid, int vol, int tone, float maxdist)
{
    PS2_FRAME(0x70);
    unsigned char *vh = &acp->vh[vhid];
    if (!(*vh < 0x50)) {
        vol = acse_scale_vol(acp, vol);
        *vh = (unsigned char)Voice_CallTraceLoc(seid, (tone << 16) | vol, &acp->mp->loc, maxdist);
    } else {
        unsigned char h = *vh;
        if (h < 0x50) Voice_SetLoopVoiceParamVm(h, acse_scale_vol(acp, vol), maxdist);
        if (*vh < 0x50) DF_SettingPitchBend(*vh, tone);
    }
    return *vh;
}

int acse_PutSeVolDist(AcPlayerSt *acp, int seid, int vol, float dist)
{
    PS2_FRAME(0x50);
    vol = acse_scale_vol(acp, vol);
    return Voice_CallPoint(seid, vol, &acp->mp->loc, dist);
}

/* one leg loop: rate 0..10000 -> volume / pitch between the table's min and max */
static void acse_loop_update(AcPlayerSt *acp, const AcLoopSeInfo *t, int rate)
{
    float r = (float)rate / 10000.0f;
    int vol = ps2_ftoi((float)t->minvol + r * (float)(t->maxvol - t->minvol));
    unsigned char h = acp->vh[t->vhid];
    if (h < 0x50) Voice_SetLoopVoiceParamVm(h, acse_scale_vol(acp, vol), 2500.0f);
    int pitch = ps2_ftoi((float)t->minpitch + r * (float)(t->maxpitch - t->minpitch));
    if (acp->vh[t->vhid] < 0x50) DF_SettingPitchBend(acp->vh[t->vhid], pitch);
}

/* rate += accel (up to 10000) while `up`, else -= brake (down to 0); 0 without a sound */
static int acse_loop_rate(AcPlayerSt *acp, const AcLoopSeInfo *t, int rate, int up)
{
    if (t->id == -1) return 0;
    if (up) {
        rate += t->accel;
        if (rate > 0x2710) rate = 0x2710;
    } else {
        rate -= t->brake;
        if (rate < 0) rate = 0;
    }
    acse_loop_update(acp, t, rate);
    return rate;
}

void acse_CtrlSe(AcPlayerSt *acp)
{
    PS2_FRAME(0x80);
    AcSeSt *sep = AcCtrl_GetSe(acp);
    AcLegTypeDataSt *td = acp->param.typedatap;
    /* turning: the canoturn table only accelerates; both share turn_se_rate */
    if (acp->soundstat & 4) sep->turn_se_rate = acse_loop_rate(acp, &td->canoturn_seinfo, sep->turn_se_rate, 1);
    else sep->turn_se_rate = acse_loop_rate(acp, &td->turn_seinfo, sep->turn_se_rate, acp->soundstat & 2);
    sep->motor_se_rate = acse_loop_rate(acp, &acp->param.typedatap->motor_seinfo, sep->motor_se_rate, acp->soundstat & 1);
    if (!(acp->soundstat & 8)) acse_stop(acp, 6);

    /* moving through water up to the body: the water attribute's hover sound, louder with speed */
    if (acp->water_high < 0.001f + acp->loc[1] && !(acp->water_high <= acp->loc[1] - acp->tall)) {
        float f = ps2_div(acp->spdmeter_speed, acp->param.dash_maxspd);
        if (!(f <= 1.0f)) f = 1.0f;
        int vol = ps2_ftoi((float)AC_SEparam.watermove_vol * f);
        if (vol < 4 && (acp->effectstat & 0x3000)) vol = 4;
        MapAttributeInfoSt *info = mapatrb_GetInfo(acp->mp->mizuattribute);
        int seid;
        if (info && (seid = info->hover_SEid) != 0) {
            int tone = AC_SEparam.watermove_tone;
            float dist = (float)AC_SEparam.watermove_dist;
            if (!(acp->vh[7] < 0x50)) {
                vol = acse_scale_vol(acp, vol);
                acp->vh[7] = (unsigned char)Voice_CallTraceLoc(seid, (tone << 16) | vol, &acp->mp->loc, dist);
            } else {
                unsigned char h = acp->vh[7];
                if (h < 0x50) Voice_SetLoopVoiceParamVm(h, acse_scale_vol(acp, vol), dist);
                if (acp->vh[7] < 0x50) DF_SettingPitchBend(acp->vh[7], tone);
            }
        } else acse_stop(acp, 7);
    } else acse_stop(acp, 7);
}

int acse_Init(AcPlayerSt *acp)
{
    PS2_FRAME(0x20);
    AcSeSt *sep = AcCtrl_GetSe(acp);
    for (unsigned int i = 0; i < 8; i++) acp->vh[i] = 0x50;
    sep->vol_rate = 1.0f;
    sep->motor_se_rate = 0;
    sep->turn_se_rate = 0;
    return 1;
}
