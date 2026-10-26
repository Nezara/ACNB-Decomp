/* src/enemy/enese.cpp (OLSORTIE) - enemy sound effects. garage3 layer 1: DFene_SEStop stops an enemy's looping voices
 * (ep->vh[3], 0x50 = none; vhid < 0 = all three) and DFene_SEStopAll does it for every enemy at the end of a sortie.
 * The asm of DFene_SEStop is a compiler-unrolled nest of loops whose negative-index arms (incl. a self call) are dead;
 * this is the live behaviour. Layer 2: _DFene_SECall plays an EneSeSt sound at a location (mode 0 = Voice_CallPoint,
 * 1 = Voice_CallTraceLoc through the file's funcp table) with the volume scaled. s103: loop-on-move, boost and jet
 * sounds (enese_call_loop_on_move / enese_call_boost / ene_ac_se_jet). The rest stays interpreted. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"

#ifdef ACNB_SOURCEPORT
int (*enese_call_loop_on_move__funcp[2])(int, int, float (*)[4], float) = {Voice_CallPoint, Voice_CallTraceLoc};
#endif

void DFene_SEStopAll(void)
{
    PS2_FRAME(0x30);
    EnemySt *e = Enemy;
    for (int i = 0; i < Array->head.cnum; i++, e++) DFene_SEStop(e, -1);   /* bound re-read every pass */
}

static inline void enese_stop_vh(unsigned char *p)
{
    unsigned int v = *p;
    if (v < 0x50) {
        Voice_Stop((int)v, 0);
        *p = 0x50;
    }
}

void DFene_SEStop(EnemySt *ep, int vhid)
{
    PS2_FRAME(0x60);
    unsigned char *vh = (unsigned char *)ep + 9;     /* ep->vh; vhid > 2 is not checked */
    if (vhid >= 0) {
        enese_stop_vh(vh + vhid);
    } else {
        for (int i = 0; i < 3; i++) enese_stop_vh(vh + i);
    }
}

/* 0x50 = "no sound" (no table entry / silent), 0 = bad mode */
int _DFene_SECall(EneSeSt *sep, float (*locp)[4], float vol_scale, int mode)
{
    PS2_FRAME(0x10);
    if (!sep) return 0x50;
    if (sep->se_id == 0 || sep->se_vol == 0) return 0x50;
    if (mode < 0 || mode >= 2) return 0;
    int (*fn)(int, int, float (*)[4], float) = enese_call_loop_on_move__funcp[mode];
    if (!fn) return 0;
    int v = ps2_ftoi((float)sep->se_vol * vol_scale);
    if (v >= 0x80) v = 0x7f;                           /* signed: a negative v stays */
    return gcall(fn, (int)sep->se_id, (sep->se_ton << 16) | (v & 0xffff), locp, sep->se_maxdist);
}

/* looping move sound: se_cnt ramps 0..30 (up while moving, dir != 8; down when idle) and scales the volume; a free
 * voice slot (vh >= 0x50) starts se_tab[seno] through funcp[1] (Voice_CallTraceLoc at the model), a running voice
 * gets its volume / tone updated - from se_tab[0], not se_tab[seno] (original) */
void enese_call_loop_on_move(EnemySt *ep, int seno, int vhid, char dir)
{
    PS2_FRAME(0x20);
    if ((unsigned int)vhid >= 3) return;
    if (dir != 8) { if (ep->se_cnt < 30) ep->se_cnt++; }
    else if (ep->se_cnt > 0) ep->se_cnt--;
    float rate = ps2_div((float)ep->se_cnt, 30.0f);
    if (!(0.0f < rate)) return;
    unsigned char *p = &ep->vh[vhid];
    unsigned int h = *p;
    if (h >= 0x50) {
        EneSeSt *sep = &ep->edat->se_tab[seno];
        float (*locp)[4] = (float (*)[4])ep->mp->loc;
        int v;
        if (!sep || sep->se_id == 0 || sep->se_vol == 0) v = 0x50;
        else {
            int (*fn)(int, int, float (*)[4], float) = enese_call_loop_on_move__funcp[1];
            if (fn) {
                int vol = ps2_ftoi((float)sep->se_vol * rate);
                if (vol >= 0x80) vol = 0x7f;
                v = gcall(fn, (int)sep->se_id, (sep->se_ton << 16) | (vol & 0xffff), locp, sep->se_maxdist);
            } else v = 0;
        }
        *p = (unsigned char)v;
    } else {
        EneSeSt *sep = &ep->edat->se_tab[0];
        int vol = ps2_ftoi((float)sep->se_vol * rate);
        if (vol >= 0x80) vol = 0x7f;
        int ton = ps2_ftoi((float)sep->se_ton * 1.0f);
        if (ton >= 0x80) ton = 0x7f;
        Voice_SetLoopVoiceParamVmt(h & 0xff, ((ton & 0xffff) << 16) | (vol & 0xffff), sep->se_maxdist);
    }
}

/* boost sound: a free voice slot vh[vhid] (>= 0x50) starts se_tab[vhid] (index = vhid, no bounds check) through
 * funcp[1] (Voice_CallTraceLoc at the model) at full volume; a running voice is left alone */
void enese_call_boost(EnemySt *ep, int vhid)
{
    PS2_FRAME(0x20);
    unsigned char *p = &ep->vh[vhid];
    if (*p < 0x50) return;
    EneSeSt *sep = &ep->edat->se_tab[vhid];
    float (*locp)[4] = (float (*)[4])ep->mp->loc;
    int v;
    if (!sep || sep->se_id == 0 || sep->se_vol == 0) v = 0x50;
    else {
        int (*fn)(int, int, float (*)[4], float) = enese_call_loop_on_move__funcp[1];
        if (fn) {
            int vol = ps2_ftoi((float)sep->se_vol * 1.0f);
            if (vol >= 0x80) vol = 0x7f;
            v = gcall(fn, (int)sep->se_id, (sep->se_ton << 16) | (vol & 0xffff), locp, sep->se_maxdist);
        } else v = 0;
    }
    *p = (unsigned char)v;
}

/* one-shot jet sound when the boost starts: se_tab[3] at mp through funcp[0] (Voice_CallPoint), handle dropped */
void ene_ac_se_jet(EnemySt *ep, StModel *mp)
{
    PS2_FRAME(0x10);
    EneSeSt *sep = &ep->edat->se_tab[3];
    if (!sep || sep->se_id == 0 || sep->se_vol == 0) return;
    int (*fn)(int, int, float (*)[4], float) = enese_call_loop_on_move__funcp[0];
    if (!fn) return;
    int vol = ps2_ftoi((float)sep->se_vol * 1.0f);
    if (vol >= 0x80) vol = 0x7f;
    gcall(fn, (int)sep->se_id, (sep->se_ton << 16) | (vol & 0xffff), (float (*)[4])mp->loc, sep->se_maxdist);
}
