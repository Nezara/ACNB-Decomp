/* src/sound/snd.cpp - top-level sound driver: boot init / cleanup of the IOP sound modules and the synth, the
 * per-frame Snd_Exec (BGM sequencer, voice info from the IOP, the SE voice manager, SE command flush, message VAG and
 * multi-stream procs), per-stage begin / end, volume / mode setters and the BGM front-end, which plays through the
 * ADS stream or the multi-stream module according to StgConf.use_ads / use_mstm (Versus: ADS). The IOP side is the
 * interpreted fssd* layer (phase 3 replaces it). */
#include "acnb/sdk.h"

void Snd_StopStageBgm(void)
{
    PS2_FRAME(0x10);
    if (StgConf.use_mstm) MultiStm_StopBgm();
    if (StgConf.use_ads) Ads_Stop();
}

/* (these four store into SoundManageData directly, so they work before Snd_Init sets Sm) */
void DF_SND_SetDopplerDisable(int bOff)
{
    SoundManageData.FrmDopplerOff = (unsigned char)bOff;
}

void DF_SND_ChangeVCMode(int mode)
{
    SoundManageData.vcmode = mode;
}

void DF_SND_SetBGMVolume(int vol)
{
    SoundManageData.bgmvol = (unsigned char)vol;
}

void DF_SND_SetSEVolume(int vol)
{
    SoundManageData.sevol = (unsigned char)vol;
}

int DF_SND_CallOff(int off)
{
    if (!Sm) return -1;
    int old = Sm->calloff;
    Sm->calloff = (unsigned char)off;
    return old;
}

void DF_SND_SetMVolume(unsigned short vol)
{
    fssdsynthSetMVolume(vol);
}

/* 0 mono, 1 stereo, 2 surround (the SE banks also get stream definitions) */
void DF_SND_SetMode(int mode)
{
    PS2_FRAME(0x20);
    if (!Sm) return;
    switch (mode) {
    case 2:
        fssdsynthSetStereo();
        if (0 <= Sm->secom && Sm->secom < 16) fssdsynthDefineSeStream(4, Sm->secom);
        if (0 <= Sm->semap && Sm->semap < 16) fssdsynthDefineSeStream(6, Sm->semap);
        break;
    case 1:
        fssdsynthSetStereo();
        break;
    case 0:
        fssdsynthSetMono();
        break;
    default:
        return;
    }
    Sm->stereo = (unsigned char)mode;
}

/* mute / unmute the non-message voices; the volumes are kept in a static, so unmuting without muting first restores
 * whatever was saved last */
void DF_SND_Mute(int mute)
{
    PS2_FRAME(0x40);
    if (mute) {
        for (int i = 0; i < 0x3f; i++) {
            if (g_erv[i].on && !g_erv[i].ismessage) {
                DF_SND_Mute__vol[i] = g_erv[i].vol;
                fssdsynthSeVolume(g_erv[i].vid, 0);
            }
        }
    } else {
        for (int i = 0; i < 0x3f; i++) {
            if (g_erv[i].on && !g_erv[i].ismessage) fssdsynthSeVolume(g_erv[i].vid, DF_SND_Mute__vol[i]);
        }
    }
    fssdsynthSeFlush();
}

/* (a negative `random` gives a negative ADS index) */
int Snd_PlayStageBgmRandom(int random, int isLoop)
{
    PS2_FRAME(0x10);
    if (!StgConf.use_ads) return 1;
    return Ads_Play(random % 8, isLoop);
}

int Snd_ChangeStageBgm(int idBgm)
{
    PS2_FRAME(0x10);
    if (!StgConf.use_mstm) return 1;
    return MultiStm_ChangeBgm(idBgm);
}

int Snd_PlayStageBgm(int idBgm, int isLoop)
{
    PS2_FRAME(0x10);
    if (StgConf.use_mstm) return MultiStm_PlayBgm(idBgm);
    if (StgConf.use_ads) return Ads_Play(idBgm, isLoop);
    return 1;
}

void Snd_EndStage(void)
{
    PS2_FRAME(0x10);
    if (Sm) Sm->calloff = 0;
    Ads_Stop();
    EmsVag_VagStop();
    if (StgConf.use_mstm) MultiStm_End();
    DF_SND_RemoveStageSound();
    Vags_CleanupStage();
}

void Snd_BeginStage(void)
{
    PS2_FRAME(0x10);
    Ads_InitStage();
    Ads_Enable(StgConf.use_ads == 1);
    Vags_InitStage();
    Vags_Enable(StgConf.use_vag == 1);
    if (StgConf.use_mstm) {
        MultiStm_Start();
        Ads_Enable(0);
    }
}

/* the player's option master volume (0..0x7f) scaled to 0..0x3fff */
static inline unsigned short snd_master_volume(void)
{
    int mv = g_gamedata[(int)MyI.myplayerno[0]].option.mastervol;
    return (unsigned short)((mv * 0x3fff) / 0x7f);
}

void Snd_SetMasterVolume(void)
{
    fssdsynthSetMVolume(snd_master_volume());
}

int Snd_Exec(void)
{
    PS2_FRAME(0x10);
    Bgm_Exec();
    fssdsynthUpdateVoiceInfo();
    if (Sm->vcmode == 1) DF_SND2_VoiceManagementProcAmap();
    else DF_SND2_VoiceManagementProc();
    fssdsynthSeFlush();
    EmsVag_Proc();
    MultiStm_Exec();
    OneFrameVoiceCallNum = 0;   /* (the per-frame budget of Voice_Call* starts) */
    Sm->flm++;
    return 0;
}

void Snd_Cleanup(void)
{
    PS2_FRAME(0x10);
    MultiStm_Cleanup();
    fssdsynthQuit();
    if (fssdIsInited()) fssdCleanup();
}

int Snd_Init(void)
{
    PS2_FRAME(0x10);
    fssdInit();
    sceSdRemoteInit();
    MultiStm_Init();
    fssdsynthInit();
    fssdSetMasterVolume(0, 0x3fff, 0x3fff);
    fssdSetMasterVolume(1, 0x3fff, 0x3fff);
    fssdsynthAddSpu2MemoryBlock(0, 0x5010, 0x1daf70);
    fssdsynthAddSpu2MemoryBlock(1, 0x1f0000, 0x9020);
    fssdsynthAssociateSpu2MemBlockIdWithPort(0, 0xffff);
    fssdsynthSetCoreAttr(10, 0x800);
    DF_SND2_Initialize();
    DF_SND2_ClearVoiceData();
    Vags_Init();
    if (FirstSoundInitCalled) {
        fssdsynthSetMVolume(snd_master_volume());
    } else {
        fssdsynthSetMVolume(0x3fff);
        g_gamedata[0].option.mastervol = 0x7f;
        FirstSoundInitCalled = 1;
        g_gamedata[1].option.mastervol = 0x7f;
    }
    Sm = &SoundManageData;
    Sm->sq_now = -1;
    Sm->sid[0] = -1;
    Sm->sid[1] = -1;
    Sm->sid[2] = -1;
    Sm->sid[3] = -1;
    Sm->bid[0] = -1;
    Sm->bid[1] = -1;
    Sm->bid[2] = -1;
    Sm->bid[3] = -1;
    Sm->param.panlim = 20.0f;
    Sm->param.zofs = 0.0f;
    Sm->param.dopp = 1;
    Sm->param.range = 1.0f;
    Sm->calloff = 0;
    Sm->vcmode = 0;
    Sm->semap = -1;
    Sm->secom = -1;
    Sm->DopplerCoef = 0x50;
    Sm->FrmDopplerOff = 0;
    OneFrameVoiceCallNum = 0;
    for (unsigned int i = 0; i < 8; i++) Sm->rid[i] = -1;
    fssdsynthSystemStart();
    return 0;
}
