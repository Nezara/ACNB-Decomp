/* src/sound/voice.cpp - the game's voice-call API. Voice_Call (one-shot), Voice_CallLoop (loop), Voice_CallPoint
 * (positioned once), Voice_CallTraceLoc (tracks a location pointer), Voice_CallTraceScript (fixed script position)
 * fill the call mailbox g_vw and start it with Se_KickAssemble (se.cpp), returning the voice slot or an error code
 * (0x51 / 0x52 / 0x54). `vol` is packed: low 16 bits volume, high 16 bits tone (0 = default 0x3c). Voice_Stop cuts
 * a voice or starts its fade (job 100); the message / commentary calls drive the speech streams (multi-stream or
 * VAG). Voice handles are signed slot indices: a negative one is not rejected. */

static inline int voice_tone(int vol)
{
    int tone = vol >> 16;
    return tone ? tone : 0x3c;
}

/* the gate of every positional / plain call */
static inline int voice_refused(void)
{
    if (Sm->calloff) return 1;
    if (!g_vw) return 1;
    return Se_IsFullAssemble() != 0;
}

int Voice_IsFullCall(void)
{
    return Se_IsFullAssemble();
}

void Voice_SetLoopVoiceParamVmt(int vh, int vol, float maxdist)
{
    if (!(vh < 0x3f)) return;
    struct_186b36a4 *e = &g_erv[vh];
    if (!e->on) return;
    if (vol != -1) e->mvol = (short)vol;
    if (!(maxdist <= 0.0f)) e->maxdist = maxdist;
    int tone = (vol >> 16) & 0xffff;   /* (vol -1 "keep" gives tone 0xffff) */
    if (tone == 0) tone = 0x3c;
    if (tone > 0) e->tone = (short)tone;
}

void Voice_SetLoopVoiceParamVm(int vh, int vol, float maxdist)
{
    if (!(vh < 0x3f)) return;
    struct_186b36a4 *e = &g_erv[vh];
    if (!e->on) return;
    if (vol != -1) e->mvol = (short)vol;
    if (!(maxdist <= 0.0f)) e->maxdist = maxdist;
}

void Voice_StopComt(void)
{
    PS2_FRAME(0x10);
    if (StgConf.use_mstm) MultiStm_StopVoice();
    else EmsVag_VagStop();
}

void Voice_StopMessage(void)
{
    PS2_FRAME(0x10);
    if (StgConf.use_mstm) MultiStm_StopVoice();
    else EmsVag_VagStop();
}

/* mode 0 cuts the voice, else it fades out (job 100, DF_SND2_VoiceManagementProc) */
void Voice_Stop(int vh, int mode)
{
    PS2_FRAME(0x10);
    if (!(vh < 0x3f)) return;
    struct_186b36a4 *e = &g_erv[vh];
    if (!e->on) return;
    if (mode == 0) {
        e->on = 0;
        e->flm = 0;
        e->isuse = 0;
        if (Sm->stereo == 2) fssdsynthSeStreamOff(e->vid);
        else fssdsynthSeOff(e->vid);
    } else {
        e->job = 100;
        e->flm = 0;
    }
}

/* tracked loop at a fixed position (the tone is a separate argument here) */
int Voice_CallTraceScript(int sss_id, int vol, int tone, float x, float y, float z, float maxdist)
{
    PS2_FRAME(0x50);
    if (voice_refused()) return 0x51;
    g_vw->job = 2;
    g_vw->mvol = (short)vol;
    g_vw->sss = sss_id;
    if (tone == 0) tone = 0x3c;
    g_vw->tone = (short)tone;
    g_vw->type = 1;
    g_vw->ismessage = 0;
    g_vw->loc = 0;
    g_vw->locSCR[0] = x;
    g_vw->locSCR[1] = y;
    g_vw->locSCR[2] = z;
    g_vw->maxdist = maxdist;
    return Se_KickAssemble();
}

int Voice_CallMessage(int sss_id)
{
    PS2_FRAME(0x20);
    if (Sm->calloff) return 0x51;
    if (StgConf.use_mstm) {
        MultiStm_PlayVoice(sss_id);
    } else {
        Vags_Stop();
        Vags_Play(sss_id, 0, 0);
    }
    return 0;
}

int Voice_CallComt(int sss_id)
{
    PS2_FRAME(0x10);
    if (Sm->calloff) return 0x51;
    if (StgConf.use_mstm) MultiStm_PlayVoice(sss_id);
    return 0;
}

/* positional loop tracking *loc */
int Voice_CallTraceLoc(int sss_id, int vol, float (*loc)[4], float maxdist)
{
    PS2_FRAME(0x60);
    int tone = voice_tone(vol);
    if (voice_refused()) return 0x51;
    g_vw->job = 2;
    g_vw->mvol = (short)vol;
    g_vw->sss = sss_id;
    g_vw->tone = (short)tone;
    g_vw->type = 1;
    g_vw->ismessage = 0;
    g_vw->loc = loc;
    g_vw->maxdist = maxdist;
    return Se_KickAssemble();
}

/* positional one-shot (placed once) */
int Voice_CallPoint(int sss_id, int vol, float (*loc)[4], float maxdist)
{
    PS2_FRAME(0x60);
    int tone = voice_tone(vol);
    if (voice_refused()) return 0x51;
    g_vw->job = 1;
    g_vw->mvol = (short)vol;
    g_vw->sss = sss_id;
    g_vw->tone = (short)tone;
    g_vw->type = 0;
    g_vw->ismessage = 0;
    g_vw->loc = loc;
    g_vw->maxdist = maxdist;
    return Se_KickAssemble();
}

int Voice_CallLoop(int sss_id, int vol)
{
    PS2_FRAME(0x40);
    int tone = voice_tone(vol);
    if (voice_refused()) return 0x51;
    g_vw->job = 0;
    g_vw->mvol = (short)vol;
    g_vw->sss = sss_id;
    g_vw->tone = (short)tone;
    g_vw->type = 1;
    g_vw->ismessage = 0;
    return Se_KickAssemble();
}

int Voice_Call(int sss_id, int vol)
{
    PS2_FRAME(0x40);
    int tone = voice_tone(vol);
    if (voice_refused()) return 0x51;
    g_vw->job = 0;
    g_vw->mvol = (short)vol;
    g_vw->sss = sss_id;
    g_vw->tone = (short)tone;
    g_vw->type = 0;
    g_vw->ismessage = 0;
    return Se_KickAssemble();
}
