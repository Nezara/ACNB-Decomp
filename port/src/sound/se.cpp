/* src/sound/se.cpp - EE-side sound-effect voice manager. g_erv[64] is the voice table; g_erv[63] (g_vw) is the call
 * mailbox the Voice_Call* functions (voice.c) fill and Se_KickAssemble turns into a voice: free slot, bank / program
 * from the smart-SS tables or the stage's sound table, 3D volume / pan / Doppler bend relative to the listener (the
 * view camera pushed forward by param.zofs), then fssdsynthSeOn. Every frame DF_SND2_VoiceManagementProc recomputes
 * the positional voices (job 2), fades job 100, expires one-shots by frame count and pushes the changes through the
 * interpreted fssdsynth* command-buffer layer (phase 3 replaces it). Jobs: 0 one-shot, 1 positioned once (becomes
 * 0), 2 tracked every frame, 100 fade-out. Types: 0 expires after 0x28 frames, 1 loop, 2 after 0x294, 3 after 0x400. */
#include <string.h>
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

static inline int se_sevol(int vol) { return vol * SoundManageData.sevol / 127; }

/* listener: the view camera's position pushed param.zofs forward along its heading (VU vadd.xyz: w stays the
 * camera's); written to Sm->lloc / Sm->langy. Returns the heading. */
static inline float se_listener(float *lloc)
{
    memcpy(lloc, CAM[ViewCamID[0]].vec, 16);
    float a = CAM[ViewCamID[0]].ang[1];
    SoundManagerSt *sm = Sm;
    float ofs[4];
    ofs[0] = sm->param.zofs * fsmath_SinVU0(a);
    ofs[1] = 0.0f;
    ofs[2] = Sm->param.zofs * fsmath_CosVU0(a);
    ofs[3] = 1.0f;
    lloc[0] = lloc[0] + ofs[0];
    lloc[1] = lloc[1] + ofs[1];
    lloc[2] = lloc[2] + ofs[2];
    sm->langy = a;
    memcpy(Sm->lloc, lloc, 16);
    return a;
}

/* split screen: volume = sum of both views' linear falloffs (at most 0x7f), pan from the distance ratio. The callers
 * pass a 5th constant in a3 that MWCC dropped from the DWARF; it is never read. */
static void GetSplitVolume(short *val, float *vloc, short mvol, float maxdist)
{
    PS2_FRAME(0xa0);
    float dist[2];
    int vol = 0;
    for (int ii = 0; ii < 2; ii++) {
        int camid = ViewCamID[ii];
        if (camid < 0) {
            dist[ii] = 100000.0f;
        } else {
            dist[ii] = DF_CALC_GetDist(vloc, CAM[camid].vec);
            if (dist[ii] < maxdist) vol += ps2_ftoi((float)mvol * (maxdist - dist[ii]) / maxdist);
        }
    }
    if (!(vol < 0x80)) vol = 0x7f;
    val[0] = (short)vol;
    float sum = dist[0] + dist[1];
    if (0.0f == sum) val[1] = 0x40;
    else val[1] = (short)ps2_ftoi(128.0f * dist[0] / sum);
}

/* volume / pan / Doppler bend of a source at loc for a listener at myloc facing myangy. ret[3] holds the previous
 * distance * 1000 on entry (bend) and the new one on exit; out of range returns 0 and writes only ret[3]. mode =
 * Sm->stereo: 2 (surround) pans by the unwrapped angle tracked in *angy_nc (initialised on the voice's first frame),
 * else by sin(atan(tan(angle) * range)) * panlim around 64. */
int DF_SND2_GetSEParam(float *myloc, float myangy, float *angy_nc, int frm, float *loc, float maxdist, int mvol, int *ret,
                       int mode)
{
    PS2_FRAME(0xd0);
    int max = ps2_ftoi(maxdist);
    int fp = ret[3];
    float d = DF_CALC_GetDist(myloc, loc);
    int dist = ps2_ftoi(d);
    int dist2 = ps2_ftoi(1000.0f * d);
    if (!(dist < max)) {
        ret[3] = dist2;
        return 0;
    }

    float f = (DF_CALC_GetAngY(myloc, loc) - myangy) * ps2_u2f(0x42652ee1);   /* 57.29578: degrees */
    if (!(f <= 360.0f)) f -= 360.0f;
    if (f < 0.0f) f += 360.0f;
    f -= 180.0f;
    int pan;
    if (mode == 2) {
        if (frm == 0) {
            while (!(f <= 180.0f)) f -= 360.0f;
            while (f < -180.0f) f += 360.0f;
            *angy_nc = f;
        }
        f -= *angy_nc;
        while (!(f <= 180.0f)) f -= 360.0f;
        while (f < -180.0f) f += 360.0f;
        f = *angy_nc + f;
        *angy_nc = f;
        float g = (90.0f + f) / 360.0f;
        g = 1024.0f * g;
        while (!(g <= 2048.0f)) g -= 4096.0f;
        while (g < -2048.0f) g += 4096.0f;
        pan = ps2_ftoi(-g);
    } else {
        /* (a source dead ahead / behind (0 degrees) gets the 64-degree pan) */
        if (0.0f == f) f = 64.0f;
        else if (!(f <= 90.0f)) f = 180.0f - f;
        else if (f < -90.0f) f = -180.0f - f;
        float t = ps2_tanf(f * ps2_u2f(0x3c8efa35));   /* pi / 180 */
        t = ps2_atanf(t * Sm->param.range);
        float s = fsmath_SinVU0(t);
        pan = ps2_ftoi(s * Sm->param.panlim + 64.0f);
        if (!(pan < 0x80)) pan = 0x7f;
        if (pan < 0) pan = 0;
    }

    int b;
    if (Sm->FrmDopplerOff == 0) {
        if (Sm->param.dopp == 0) {
            b = (int)(((unsigned)fp - (unsigned)dist2) << 1) / 3;
            if (b > 0) b /= 3;
        } else {
            int x = (int)(((unsigned)fp - (unsigned)dist2) * (unsigned)Sm->DopplerCoef) >> 8;
            if (!(x < 0x1622)) b = 0x1fff;
            else if (x < -0x1621) b = -0x1fff;
            else b = (int)((unsigned)x << 12) / (0x1622 - x);
        }
    } else {
        b = (int)PS2_STALE_GPR(22);   /* s6 is never set: the caller's (both callers ignore ret[2] then) */
    }
    if (!(b < 0x2000)) b = 0x1fff;
    if (b < -0x1fff) b = -0x1fff;

    int vol = mvol;
    if (!(max < 11)) {
        if (!(dist < max)) {
            vol = 0;
        } else if (!(dist < 10)) {
            int d2 = max - 10;
            if (d2 == 0) d2 = 1;
            vol = mvol - mvol * (dist - 10) / d2;
            if (vol < 0) vol = 0;
        }
    }
    ret[0] = vol;
    ret[1] = pan;
    ret[2] = b;
    ret[3] = dist2;
    return vol;
}

/* (vh is a signed index: -1 would read DF_SND_Mute's table below g_erv) */
void DF_SettingPitchBend(int vh, int p_bend)
{
    if (vh < 0x3f && g_erv[vh].on) g_erv[vh].p_bend = (short)p_bend;
}

int DF_SND2_VolumeFilter_SE(int vol)
{
    if (MyI.pause) return 0;
    return se_sevol(vol);
}

/* start the voice described by the mailbox g_vw: returns the slot, 0x51 (no program / no free slot), 0x52 (call
 * budget of the frame used up / the IOP layer refused) or 0x54 (positioned one-shot out of range) */
int Se_KickAssemble(void)
{
    PS2_FRAME(0x90);
    PS2_STACK(int[4], ret, 0x70);   /* GetSEParam leaves ret[1..2] unwritten out of range; they are stored anyway */
    int sss = g_vw->sss;
    int q = sss / 1000;
    int lv;
    if (q == 1) lv = (&s_smartSs[1][0])[sss] - 1;
    else if (q == 0) lv = (&s_smartSs[0][0])[sss] - 1;
    else lv = Array->stable[sss];
    if (lv < 0) return 0x51;

    int ismessage = g_vw->ismessage;
    int n = ismessage ? 0x3f : 0x38;
    int idx = -1;
    struct_186b36a4 *vp = 0;
    for (int i = 0; i < n; i++) {
        if (g_erv[i].on == 0) {
            idx = i;
            vp = &g_erv[i];
            break;
        }
    }
    if (idx == -1) return 0x51;
    if (!ismessage && g_vw->type != 1) {
        OneFrameVoiceCallNum++;
        if (!(OneFrameVoiceCallNum < 12)) return 0x52;
    }

    vp->wk[3] = 0;
    vp->wk[2] = 0;
    vp->wk[1] = 0;
    vp->wk[0] = 0;
    vp->flm = 0;
    vp->jobs = 0;
    vp->tone = g_vw->tone;
    vp->type = g_vw->type;
    vp->mvol = g_vw->mvol;
    vp->ismessage = g_vw->ismessage;
    q = g_vw->sss / 1000;
    if (q == 1) vp->bd_kind = (short)Sm->semap;
    else if (q == 0) vp->bd_kind = (short)Sm->secom;
    else if (lv < 20000) vp->bd_kind = (short)Sm->semap;
    else vp->bd_kind = (short)Sm->mapt;
    vp->bd_prog = (short)(lv % 10000);
    vp->p_bend = 0;

    float lloc[4] __attribute__((aligned(16)));
    float a = se_listener(lloc);

    int job = g_vw->job;
    vp->job = (char)job;
    if (job == 2) {
        if (!g_vw->loc) {
            vp->loc = (float (*)[4])vp->locSCR;
            vp->locSCR[0] = g_vw->locSCR[0];
            vp->locSCR[1] = g_vw->locSCR[1];
            vp->locSCR[2] = g_vw->locSCR[2];
        } else {
            vp->loc = g_vw->loc;
        }
        if (!(ViewNum < 2)) {
            short sv[2];
            vp->maxdist = g_vw->maxdist;
            GetSplitVolume(sv, *vp->loc, vp->mvol, vp->maxdist);
            vp->vol = sv[0];
            vp->pan = sv[1];
            vp->bend = 0;
        } else {
            vp->cdist = ps2_ftoi(1000.0f * DF_CALC_GetDist(*vp->loc, lloc));
            vp->maxdist = g_vw->maxdist;
            ret[3] = vp->cdist;
            int v = DF_SND2_GetSEParam(lloc, a, &vp->last_angy, vp->flm, *vp->loc, vp->maxdist, vp->mvol, ret, Sm->stereo);
            vp->vol = (short)(MyI.pause ? 0 : se_sevol(v));
            vp->pan = (short)ret[1];
            if (!Sm->FrmDopplerOff) vp->bend = (short)ret[2];
            vp->cdist = ret[3];
        }
        vp->vol = (short)se_sevol(vp->vol);   /* (sevol a second time) */
        if (Sm->stereo == 2)
            vp->vid = (short)fssdsynthSeStreamOn_forSurround(idx, vp->bd_kind, 0, (char)vp->bd_prog, (char)vp->tone);
        else
            vp->vid = (short)fssdsynthSeOn(idx, vp->bd_kind, 0, (char)vp->bd_prog, (char)vp->tone, (char)vp->vol, vp->bend,
                                           (char)vp->pan);
        if (vp->vid < 0) return 0x52;
        vp->on = 1;
        vp->isuse = 1;
    } else if (job == 1) {
        if (!g_vw->loc) {
            vp->loc = (float (*)[4])vp->locSCR;
            vp->locSCR[0] = g_vw->locSCR[0];
            vp->locSCR[1] = g_vw->locSCR[1];
            vp->locSCR[2] = g_vw->locSCR[2];
        } else {
            vp->loc = g_vw->loc;
        }
        if (!(ViewNum < 2)) {
            short sv[2];
            GetSplitVolume(sv, *vp->loc, vp->mvol, g_vw->maxdist);
            vp->vol = sv[0];
            vp->pan = sv[1];
            vp->bend = 0;
        } else {
            vp->cdist = ps2_ftoi(1000.0f * DF_CALC_GetDist(*vp->loc, lloc));
            ret[3] = vp->cdist;
            vp->vol = (short)DF_SND2_GetSEParam(lloc, a, &vp->last_angy, vp->flm, *vp->loc, g_vw->maxdist, vp->mvol, ret,
                                                Sm->stereo);
            vp->pan = (short)ret[1];
            if (!Sm->FrmDopplerOff) vp->bend = (short)ret[2];
            vp->cdist = (short)ret[3];   /* (lh: 16 bits) */
        }
        vp->vol = (short)se_sevol(vp->vol);
        if (vp->vol == 0) return 0x54;
        vp->job = 0;
        if (Sm->stereo == 2)
            vp->vid = (short)fssdsynthSeStreamOn_forSurround(idx, vp->bd_kind, 0, (char)vp->bd_prog, (char)vp->tone);
        else
            vp->vid = (short)fssdsynthSeOn(idx, vp->bd_kind, 0, (char)vp->bd_prog, (char)vp->tone, (char)vp->vol, vp->bend,
                                           (char)vp->pan);
        if (vp->vid < 0) return 0x52;
        vp->on = 1;
        vp->isuse = 1;
    } else if (job == 0) {
        vp->vol = g_vw->mvol;
        vp->vol = (short)se_sevol(vp->vol);
        vp->pan = -0x100;
        if (Sm->stereo == 2)
            vp->vid = (short)fssdsynthSeStreamOn_forSurround(idx, vp->bd_kind, 0, (char)vp->bd_prog, (char)vp->tone);
        else
            vp->vid = (short)fssdsynthSeOn(idx, vp->bd_kind, 0, (char)vp->bd_prog, (char)vp->tone, (char)vp->vol, 0, 0x3f);
        if (vp->vid < 0) return 0x52;
        vp->on = 1;
        vp->isuse = 1;
    }
    if (g_vw->sss == 0xc58) ps2_printf_d("this %d\n", idx);   /* debug leftover (0x260848) */
    return idx;
}

int Se_IsFullAssemble(void)
{
    return !(OneFrameVoiceCallNum + 1 < 12);
}

void DF_SND2_AllVoiceQueryOff(void)
{
    PS2_FRAME(0x30);
    DF_MPVCS_Stop();
    DF_OHVCS_Stop();
    for (int i = 0; i < 0x3f; i++) {
        struct_186b36a4 *vp = &g_erv[i];
        if (!vp->on) continue;
        if (vp->on < 0) {
            vp->on = 0;
            continue;
        }
        if (Sm->stereo == 2) fssdsynthSeStreamOff(vp->vid);
        else fssdsynthSeOff(vp->vid);
        vp->on = 0;
        vp->isuse = 0;
        vp->flm = 0;
    }
}

/* area map variant (Sm->vcmode 1): positional voices are muted, one-shots expire */
void DF_SND2_VoiceManagementProcAmap(void)
{
    PS2_FRAME(0x10);
    for (int i = 0; i < 0x3f; i++) {
        struct_186b36a4 *vp = &g_erv[i];
        if (!vp->on) continue;
        if (vp->job != 0) {
            vp->vol = 0;
            vp->pan = -0x100;
        } else if (vp->type != 1) {
            if (vp->type == 2 ? !(vp->flm < 0x295) : !(vp->flm < 0x19)) {
                vp->on = 0;
                vp->isuse = 0;
            }
        }
        if (Sm->stereo == 2) {
            s_strsrpk[i].vol = vp->vol;
            s_strsrpk[i].pan = vp->pan;
        }
        if (vp->on) vp->flm++;
    }
    if (Sm->stereo == 2) fssdsynthPackSeStreamSurroundOn(4, 0x3f, s_strsrpk);
}

void DF_SND2_VoiceManagementProc(void)
{
    PS2_FRAME(0xd0);
    PS2_STACK(int[4], ret, 0xb0);
    DF_MPVCS_Proc();
    DF_OHVCS_Proc();
    float lloc[4] __attribute__((aligned(16)));
    float a = se_listener(lloc);
    s_cntVoice = 0;
    for (int i = 0; i < 0x3f; i++) {
        struct_186b36a4 *vp = &g_erv[i];
        if (!vp->on) continue;
        s_cntVoice++;
        int job = vp->job;
        if (job == 100) {
            vp->vol -= 4;
            if (vp->vol < 4) vp->vol = 0;
            vp->vol = (short)se_sevol(vp->vol);
            fssdsynthSeVolume(vp->vid, vp->vol);
            if (vp->vol == 0) {
                vp->on = 0;
                if (Sm->stereo == 2) fssdsynthSeStreamOff(vp->vid);
                else fssdsynthSeOff(vp->vid);
                vp->isuse = 0;
                vp->flm = 0;
            }
        } else if (job == 2) {
            if (!(ViewNum < 2)) {
                short sv[2];
                GetSplitVolume(sv, *vp->loc, vp->mvol, vp->maxdist);
                vp->vol = sv[0];
                vp->pan = sv[1];
                vp->vol = (short)(MyI.pause ? 0 : se_sevol(vp->vol));
                vp->bend = 0;
            } else {
                ret[3] = vp->cdist;
                if (MyI.pause) {
                    vp->vol = 0;
                } else {
                    vp->vol = (short)DF_SND2_GetSEParam(lloc, a, &vp->last_angy, vp->flm, *vp->loc, vp->maxdist, vp->mvol,
                                                        ret, Sm->stereo);
                    if (vp->vol > 0) {
                        vp->vol = (short)(MyI.pause ? 0 : se_sevol(vp->vol));
                        vp->pan = (short)ret[1];
                        if (!Sm->FrmDopplerOff) vp->bend = (short)ret[2];
                    }
                    vp->cdist = ret[3];
                }
            }
            vp->vol = (short)se_sevol(vp->vol);   /* (sevol once more) */
            if (!(vp->vol > 0)) vp->vol = 0;
            if (vp->isuse == 1) {
                if (vp->vol == 0) {
                    if (Sm->stereo == 2) fssdsynthSeStreamOff(vp->vid);
                    else fssdsynthSeOff(vp->vid);
                    vp->isuse = 0;
                } else {
                    fssdsynthSeVolume(vp->vid, vp->vol);
                    fssdsynthSePan(vp->vid, vp->pan);
                    int b = vp->bend + vp->p_bend;
                    if (!(b < 0x1fff)) b = 0x1fff;
                    if (b < -0x1ffe) b = -0x1fff;
                    fssdsynthSePitchBend(vp->vid, b);
                }
            } else if (vp->vol > 0) {
                if (Sm->stereo == 2)
                    vp->vid = (short)fssdsynthSeStreamOn_forSurround(i, vp->bd_kind, 0, (char)vp->bd_prog, (char)vp->tone);
                else
                    vp->vid = (short)fssdsynthSeOn(i, vp->bd_kind, 0, (char)vp->bd_prog, (char)vp->tone, (char)vp->vol,
                                                   vp->bend, (char)vp->pan);
                if (!(vp->vid < 0)) vp->isuse = 1;
            }
        } else if (job == 0) {
            int lim = vp->type == 2 ? 0x295 : vp->type == 3 ? 0x401 : 0x29;
            if (vp->type != 1 && !(vp->flm < lim)) {
                vp->on = 0;
                vp->isuse = 0;
            }
        }
        if (Sm->stereo == 2) {
            s_strsrpk[i].vol = vp->vol;
            s_strsrpk[i].pan = vp->pan;
        }
        if (vp->on) vp->flm++;
    }
    if (Sm->stereo == 2) fssdsynthPackSeStreamSurroundOn(4, 0x3f, s_strsrpk);
    if (Sm->FrmDopplerOff > 0) Sm->FrmDopplerOff--;
}

void DF_SND2_ClearVoiceData(void)
{
    memset(g_erv, 0, 0x2000);
}

void DF_SND2_Initialize(void)
{
    g_vw = &g_erv[63];
}

void DF_SND_ReadSmartSSTable(void)
{
    PS2_FRAME(0x10);
    if (!s_isSmartSsRead) {
        File_Read(0x9c6, -1, (char *)s_smartSs, 1);
        s_isSmartSsRead = 1;
    }
}
