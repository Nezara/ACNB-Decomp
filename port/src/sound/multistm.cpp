/* src/sound/multistm.cpp - "multi stream": BGM and message speech streamed from cdrom0:\BMSS.DAT through the IOP
 * (lib fslibsdivag / fslibmfstrm, interpreted until phase 3) on up to 4 tracks. Play / change requests go into a
 * task queue (s_task[8]); MultiStm_Exec refreshes the track states from the IOP every frame, starts tracks whose
 * setup finished, runs the BGM cross-fade (59 steps, one every 5 frames) and issues queued tasks. s_flagThis: bit 0
 * initialised, bit 1 binder header loaded, bit 2 started. Versus plays BGM through ADS and never starts this
 * module (only Exec's early-out runs). */
#include <string.h>
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

static const char s_pathBmss[] = "cdrom0:\\BMSS.DAT;1";

/* stop the BGM tracks that are playing (states 1..5) */
void MultiStm_StopBgm(void)
{
    PS2_FRAME(0x30);
    for (int i = 0; i < 4; i++) {
        StMultiStmTrack *t = &s_track[i];
        if (t->kindVag != 2) continue;
        if (t->state == 5 || t->state == 4 || t->state == 3 || t->state == 2 || t->state == 1) {
            fssdivagStop(i);
            t->isPlay = 0;
        }
    }
}

/* stop the voice tracks (a reserved one is stopped when its setup finishes) and drop queued voice tasks */
void MultiStm_StopVoice(void)
{
    PS2_FRAME(0x30);
    for (int i = 0; i < 4; i++) {
        StMultiStmTrack *t = &s_track[i];
        if (t->kindVag != 1) continue;
        if (t->state == 100) {
            t->isStop = 1;
        } else if (t->state == 5 || t->state == 4 || t->state == 3 || t->state == 2 || t->state == 1) {
            fssdivagStop(i);
            t->isPlay = 0;
        }
    }
    for (int i = 0; i < 8; i++) {
        if (s_task[i].task && s_task[i].kindVag == 1) s_task[i].task = 0;
    }
}

static inline StMultiStmTask *multistm_free_task(void)
{
    for (int i = 0; i < 8; i++)
        if (!s_task[i].task) return &s_task[i];
    return 0;
}

int MultiStm_PlayVoice(int idVoice)
{
    StMultiStmTask *t = multistm_free_task();
    if (!t) return 0;
    if (!(s_flagThis & 4)) return 0;
    t->task = 1;
    t->kindVag = 1;
    t->id = (short)idVoice;
    int v = SoundManageData.sevol << 7;
    if (!(v < 0x4000)) v = 0x3fff;
    t->volL[0] = (unsigned short)v;
    t->volR[0] = (unsigned short)v;
    t->isLoop = 0;
    t->isVoidAutoPlay = 0;
    return 1;
}

/* BGM task (looping stereo pair): volume = scene volume x option BGM volume, 0..0x3fff */
static inline int multistm_bgm_task(int task, int idBgm)
{
    StMultiStmTask *t = multistm_free_task();
    if (!t) return 0;
    if (!(s_flagThis & 4)) return 0;
    t->task = (unsigned char)task;
    t->kindVag = 2;
    t->id = (short)idBgm;
    float f = (float)SoundManageData.scene_bgmvol / 127.0f;
    f = f * ((float)SoundManageData.bgmvol / 127.0f);
    f = 16383.0f * f;
    unsigned int v = !(2147483648.0f <= f) ? (unsigned int)ps2_ftoi(f) & 0xffff
                                           : ((unsigned int)ps2_ftoi(f - 2147483648.0f) | 0x80000000u) & 0xffff;
    if (!(v < 0x4000)) v = 0x3fff;
    t->volL[0] = (unsigned short)v;
    t->volR[0] = (unsigned short)v;
    t->volL[1] = 0;
    t->volR[1] = 0;
    t->isLoop = 1;
    t->isVoidAutoPlay = 0;
    return 1;
}

/* cross-fade to the new BGM */
int MultiStm_ChangeBgm(int idBgm)
{
    return multistm_bgm_task(3, idBgm);
}

int MultiStm_PlayBgm(int idBgm)
{
    return multistm_bgm_task(1, idBgm);
}

/* stop everything, wait up to 60 vblanks for the IOP to report all tracks idle (else force a reset) */
void MultiStm_End(void)
{
    PS2_FRAME(0x30);
    for (int i = 0; i < 4; i++) {
        fssdivagStop(i);
        s_track[i].isPlay = 0;
    }
    int n = 0;
    do {
        fssdivagUpdateTrackStat();
        int t = 0;
        while (t < 4 && fssdivagGetTrackStat(t) == 0) t++;
        if (t == 4) break;
        sceGsSyncV(0);
        n++;
    } while (n < 0x3c);
    if (!(n < 0x3c)) fssdivagForceReset();
    s_flagThis &= ~4;
}

void MultiStm_Start(void)
{
    PS2_FRAME(0x10);
    if ((s_flagThis & 2) && fssdivagSetPacket(0x1000) && fssdivagSetSpuMemory(0x1e0000, 0x10000)) s_flagThis |= 4;
}

void MultiStm_Exec(void)
{
    PS2_FRAME(0x30);
    if (!(s_flagThis & 4)) return;
    s_stateMod = 0;
    fssdivagUpdateTrackStat();
    for (int i = 0; i < 4; i++) {
        s_track[i].state = fssdivagGetTrackStat(i);
        s_track[i].sizeRead = fsmfstrm_GetIopReadSizeF(s_track[i].idxLib);
    }
    __ExecStateUpdate();
    __ExecCrossFade();
    __ExecTask();
}

void MultiStm_Cleanup(void)
{
    PS2_FRAME(0x10);
    if (!(s_flagThis & 1)) return;
    fssdivagQuit();
    fsmfstrm_DmaQuit();
    fsmfstrm_Quit();
    s_flagThis &= ~1;
}

/* load the BMSS.DAT binder header (each failure converts the path once more for the original's log message) */
static inline int multistm_setup_file(void)
{
    ZeroMemory8b(&s_bmngStm, 0xc);
    if (!File_IsExistNM(Env_ConvertPath((char *)s_pathBmss), 0)) {
        Env_ConvertPath((char *)s_pathBmss);
        return 0;
    }
    if (!File_SearchFileM(&s_cdfileStm, Env_ConvertPath((char *)s_pathBmss), 0)) {
        Env_ConvertPath((char *)s_pathBmss);
        return 0;
    }
    if (!File_ReadNM(Env_ConvertPath((char *)s_pathBmss), 0xa800, s_cHeaderData, 1, 0)) {
        Env_ConvertPath((char *)s_pathBmss);
        return 0;
    }
    Binder_Init(&s_bmngStm, s_cHeaderData, s_cdfileStm.lsn);
    s_flagThis |= 2;
    return 1;
}

int MultiStm_Init(void)
{
    PS2_FRAME(0x40);
    PS2_STACK(struct_f853a5d6, cdmode, 0x3c);
    ZeroMemory8b(&s_cross, 0x34);
    ZeroMemory8b(s_task, 0x70);
    s_stateMod = -1;
    memset(&cdmode, 0, 4);
    if (!(Iop_GetInitFlag() & 0x200)) return 1;
    if (!fssdivagInit()) return 0;
    fsmfstrm_Init();
    if (!fsmfstrm_AllocDmaBuffer(0x4000)) return 0;
    fsmfstrm_DmaInit();
    fsmfstrm_SetReadPacketSize(0x10000);
    for (int i = 0; i < 4; i++) {
        s_track[i].idxLib = fsmfstrm_AllocStream(0x20000);
        fsmfstrm_SetReloadSize(s_track[i].idxLib, 0x10000);
        fsmfstrm_SetCdRMode(s_track[i].idxLib, &cdmode);
    }
    fssdivagSetDmaChannel(1);
    fssdivagSetCoreVoice(0x26);
    if (s_bmngStm.lsn == 0 && !(s_flagThis & 1)) multistm_setup_file();
    s_flagThis |= 1;
    return 1;
}

int MultiStm_SetupFile(void)
{
    PS2_FRAME(0x10);
    if (s_flagThis & 1) return 0;
    return multistm_setup_file();
}

/* start queued tasks on idle tracks: 1 / 2 play (one track / a stereo pair), 3 / 4 the same as a cross-fade (one
 * at a time). A BGM task becomes the fade-in side of the cross-fade state. */
static void __ExecTask(void)
{
    PS2_FRAME(0x50);
    for (int i = 0; i < 8; i++) {
        StMultiStmTask *t = &s_task[i];
        int a, b;
        switch (t->task) {
        case 4:
            if (s_cross.cross) break;
            if (!__GetIdleTrack(&a, &b)) break;
            __PlayTask(i, a, b);
            t->task = 0;
            s_cross.cross = 1;
            if (t->kindVag == 2) {
                s_cross.trackIn[0] = a;
                s_cross.trackIn[1] = b;
                s_cross.volInTrueL[0] = t->volL[0];
                s_cross.volInTrueR[0] = t->volR[0];
                s_cross.volInTrueL[1] = t->volL[1];
                s_cross.volInTrueR[1] = t->volR[1];
            }
            break;
        case 3:
            if (s_cross.cross) break;
            if (!__GetIdleTrack(&a, 0)) break;
            __PlayTask(i, a, -1);
            t->task = 0;
            s_cross.cross = 1;
            if (t->kindVag == 2) {
                s_cross.trackIn[1] = -1;
                s_cross.trackIn[0] = a;
                s_cross.volInTrueL[0] = t->volL[0];
                s_cross.volInTrueR[0] = t->volR[0];
                s_cross.volInTrueL[1] = 0;
                s_cross.volInTrueR[1] = 0;
            }
            break;
        case 2:
            if (!__GetIdleTrack(&a, &b)) break;
            if (__PlayTask(i, a, b) && t->kindVag == 2) {
                s_cross.trackIn[0] = a;
                s_cross.trackIn[1] = b;
                s_cross.volInTrueL[0] = t->volL[0];
                s_cross.volInTrueR[0] = t->volR[0];
                s_cross.volInTrueL[1] = t->volL[1];
                s_cross.volInTrueR[1] = t->volR[1];
            }
            t->task = 0;
            break;
        case 1:
            if (!__GetIdleTrack(&a, 0)) break;
            if (__PlayTask(i, a, -1) && t->kindVag == 2) {
                s_cross.trackIn[1] = -1;
                s_cross.trackIn[0] = a;
                s_cross.volInTrueL[0] = t->volL[0];
                s_cross.volInTrueR[0] = t->volR[0];
                s_cross.volInTrueL[1] = 0;
                s_cross.volInTrueR[1] = 0;
            }
            t->task = 0;
            break;
        }
    }
}

/* state 6 (ended): stop and release the sectors; state 3 (set up): set the volumes and play, unless a stop came in
 * meanwhile */
static void __ExecStateUpdate(void)
{
    PS2_FRAME(0x30);
    for (int i = 0; i < 4; i++) {
        StMultiStmTrack *t = &s_track[i];
        if (t->state == 6) {
            if (t->isPlay) {
                fssdivagStop(i);
                fsmfstrm_DetachFileSector(t->idxLib);
                t->isPlay = 0;
            }
        } else if (t->state == 3) {
            if (t->isStop) {
                fssdivagStop(i);
                t->isPlay = 0;
            } else {
                if (t->kindVag == 2) {
                    fssdivagSetChVolume(i, 0, t->VolL, 0);
                    fssdivagSetChVolume(i, 1, 0, t->VolR);
                } else {
                    fssdivagSetChVolume(i, 0, t->VolL, t->VolR);
                }
                fssdivagSetTrackReverb(i, 0);
                fssdivagPlay(i);
                t->isPlay = 1;
            }
        }
    }
}

/* float -> u16 through the unsigned conversion */
static inline unsigned short multistm_u16(float f)
{
    return !(2147483648.0f <= f) ? (unsigned short)ps2_ftoi(f)
                                 : (unsigned short)((unsigned int)ps2_ftoi(f - 2147483648.0f) | 0x80000000u);
}

static void __ExecCrossFade(void)
{
    PS2_FRAME(0x50);
    int cross = s_cross.cross;
    if (!cross) return;
    s_cross.cntSub++;
    if (s_cross.cntSub < 5) return;
    float f_in = (float)(cross + 1) / 60.0f;
    float f_out = 1.0f - f_in;
    for (int i = 0; i < 2; i++) {
        s_cross.volOutL[i] = multistm_u16(0.5f + (float)s_cross.volOutTrueL[i] * f_out);
        s_cross.volOutR[i] = multistm_u16(0.5f + (float)s_cross.volOutTrueR[i] * f_out);
        s_cross.volInL[i] = multistm_u16(0.5f + (float)s_cross.volInTrueL[i] * f_in);
        s_cross.volInR[i] = multistm_u16(0.5f + (float)s_cross.volInTrueR[i] * f_in);
        if (s_cross.trackOut[i] != -1) {
            fssdivagSetChVolume(s_cross.trackOut[i], 0, s_cross.volOutL[i], 0);
            fssdivagSetChVolume(s_cross.trackOut[i], 1, 0, s_cross.volOutR[i]);
        }
        if (s_cross.trackIn[i] != -1) {
            fssdivagSetChVolume(s_cross.trackIn[i], 0, s_cross.volInL[i], 0);
            fssdivagSetChVolume(s_cross.trackIn[i], 1, 0, s_cross.volInR[i]);
        }
    }
    s_cross.cntSub = 0;
    s_cross.cross++;
    if (s_cross.cross < 0x3c) return;
    for (int i = 0; i < 2; i++) {
        int tr = s_cross.trackOut[i];
        if (tr != -1) {
            fssdivagStop(tr);
            s_track[tr].isPlay = 0;
        }
    }
    s_cross.cross = 0;
    s_cross.trackOut[0] = s_cross.trackIn[0];
    s_cross.trackOut[1] = s_cross.trackIn[1];
    s_cross.volOutTrueL[0] = s_cross.volInTrueL[0];
    s_cross.volOutTrueR[0] = s_cross.volInTrueR[0];
    s_cross.volOutTrueL[1] = s_cross.volInTrueL[1];
    s_cross.volOutTrueR[1] = s_cross.volInTrueR[1];
}

/* one idle track (ptr2 NULL) or two; *ptr1 is written as soon as the first is found */
static int __GetIdleTrack(int *ptr1, int *ptr2)
{
    int want = (ptr2 != 0) + 1;
    int found = 0;
    for (int i = 0; i < 4; i++) {
        if (s_track[i].state != 0) continue;
        if (found) {
            *ptr2 = i;
            return 1;
        }
        found++;
        *ptr1 = i;
        if (!(found < want)) return 1;
    }
    return 0;
}

/* attach the binder entries id / id + 1 of the task to track1 / track2 (-1 = none) and reserve their setup. The
 * callers pass a 4th argument (cross-fade flag) that is never read. */
static int __PlayTask(int task, int track1, int track2)
{
    PS2_FRAME(0xa0);
    StMultiStmTask *t = &s_task[task];
    for (int k = 0; k < 2; k++) {
        int tr = k == 0 ? track1 : track2;
        if (tr == -1) return 1;
        int id = t->id + k;
        if (Binder_IsExist(&s_bmngStm, id, 0) == -1) return 0;
        int sec = Binder_GetOffset(&s_bmngStm, id) / 0x800;
        int size = Binder_GetSize(&s_bmngStm, id);
        StMultiStmTrack *trk = &s_track[tr];
        if (!fsmfstrm_AttachSector(trk->idxLib, s_cdfileStm.lsn + sec, size, 2)) return 0;
        if (!fssdivagReserveSetup(tr, trk->idxLib)) return 0;
        trk->VolL = t->volL[k];
        trk->VolR = t->volR[k];
        trk->state = 100;
        trk->kindVag = t->kindVag;
        trk->isStop = 0;
    }
    return 1;
}
