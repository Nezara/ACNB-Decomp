/* src/event/rtrc.cpp - stage reverb tracking. Each map part carries reverb settings (lp_reverb); in single view the
 * reverb volume eases toward the setting of the part the viewed AC (or camera parent) stands on by about a quarter
 * of the difference per frame, and the other reverb parameters follow immediately (stored only). */
#include "acnb/port.h"

void DF_RTRC_Proc(void)
{
    PS2_FRAME(0x30);
    StModel *mp = 0;
    if (ViewNum == 1) {
        int id = ViewCamID[0];
        if (id >= 0 && id < 4) mp = MyAC[id].nowparent;
        else if (id < 6) mp = CAM[id].parent;
    }
    if (!mp || !mp->mlist) return;
    _sound_attr_st *rv = mp->mlist->lp_reverb;
    if (rv->revvol == SoundAttribute.revvol) return;
    int d = rv->revvol - SoundAttribute.revvol;
    d += d > 0 ? 3 : -3;
    SoundAttribute.revvol = SoundAttribute.revvol + (short)(d / 4);
    DF_SND_SetRevVolume(SoundAttribute.revvol);
    rv = mp->mlist->lp_reverb;
    if (SoundAttribute.revtype != rv->revtype) SoundAttribute.revtype = rv->revtype;
    rv = mp->mlist->lp_reverb;
    if (SoundAttribute.revdepth != rv->revdepth) SoundAttribute.revdepth = rv->revdepth;
    rv = mp->mlist->lp_reverb;
    if (SoundAttribute.revdelay != rv->revdelay) SoundAttribute.revdelay = rv->revdelay;
    rv = mp->mlist->lp_reverb;
    if (SoundAttribute.revfback != rv->revfback) SoundAttribute.revfback = rv->revfback;
}

void DF_RTRC_Init(void *reverb)
{
    PS2_FRAME(0x20);
    _sound_attr_st *rv = (_sound_attr_st *)reverb;
    DF_SND_SetReverb(rv->revtype, rv->revdepth, rv->revdepth, rv->revdelay, rv->revfback, rv->revvol, rv->revvol);
    CopyMemory128b(&SoundAttribute, reverb, 0x20);
}
