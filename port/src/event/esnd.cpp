/* src/event/esnd.cpp - stage ambient sound sources (EventSnd, up to 64). Stage models whose map part names a
 * sound (Array->sound[esndid]) become sources at init; every frame the squared distances to player 1's AC go to
 * the scratchpad, and the nearest source (plus the previous nearest seen during the scan - not a true second
 * nearest) is kept playing as a 3D voice; the others are stopped. A source whose model is destroyed (use & 4) is
 * killed. */
#include "acnb/port.h"

static void df_set_mdl_esnd_one(int type, int num)
{
    StModel *mp = STAGE_GetModelPointer(type, 0);
    for (int i = 0; i < num; i++, mp++) {
        _sound_attr_st *snd = &Array->sound[mp->mlist->esndid];
        if (!snd->destseid) continue;
        if (EventSndNum >= 0x40) continue;
        struct_2c434764 *es = &EventSnd[EventSndNum];
        es->snd_id = (unsigned short)snd->destseid;
        es->voice = (snd->vol & 0xffff) | 0x3c0000;
        es->dist = snd->maxdist;
        es->mp = mp;
        memcpy(es->loc, mp->loc, 16);
        EventSndNum++;
    }
}

static void df_set_mdl_esnd(void)
{
    PS2_FRAME(0x20);
    int n = Array->head.mnum;
    if (n) df_set_mdl_esnd_one(1, n);
    n = Array->head.onum;
    if (n) df_set_mdl_esnd_one(2, n);
}

void DF_EventSnd_Proc(void)
{
    if (EventSndNum <= 0) return;
    PS2_FRAME(0x50);
    float *dist = (float *)PS2_SPR(0);
    PS2_STACK(float[4], d, 0x30);
    PS2_STACK(int[2], near, 0x48);
    struct_2c434764 *es = EventSnd;
    for (int i = 0; i < EventSndNum; i++, es++) {
        if (es->kill) {
            dist[i] = 1e10f;
            continue;
        }
        const float *a = es->mp ? es->mp->loc : es->loc, *b = g_ac[0].mp->loc;
        d[0] = a[0] - b[0];   /* vsub.xyz + sqc2: w = a.w */
        d[1] = a[1] - b[1];
        d[2] = a[2] - b[2];
        d[3] = a[3];
        dist[i] = (d[0] * d[0] + d[1] * d[1]) + d[2] * d[2];
    }
    near[0] = 0;
    near[1] = -1;
    int n = EventSndNum;
    float *best = dist;
    for (int i = 1; i < n; i++) {
        if (!(*best <= dist[i])) {
            near[1] = near[0];
            near[0] = i;
            best = &dist[i];
        }
    }
    for (int k = 0; k < 2; k++) {
        if (near[k] < 0) continue;
        es = &EventSnd[near[k]];
        if (es->kill) continue;
        if (es->mp && (es->mp->use & 4)) {
            es->kill = 1;
            continue;
        }
        if (es->on) continue;
        if (es->mp)
            es->hsnd = (unsigned char)Voice_CallTraceLoc(es->snd_id, es->voice, (float (*)[4])es->mp->loc, es->dist);
        else
            es->hsnd = (unsigned char)Voice_CallTraceScript(es->snd_id, es->voice & 0xffff,
                                                            (unsigned int)(es->voice & 0xffff0000) >> 16,
                                                            es->loc[0], es->loc[1], es->loc[2], es->dist);
        if (es->hsnd < 0x51) es->on = 1;
    }
    es = EventSnd;
    for (int i = 0; i < EventSndNum; i++, es++) {
        if (es->on && es->kill) {
            Voice_Stop(es->hsnd, 0);
            es->on = 0;
            es->hsnd = 0;
        }
        if (i != near[0] && i != near[1] && es->on) {
            Voice_Stop(es->hsnd, 0);
            es->on = 0;
            es->hsnd = 0;
        }
    }
}

void DF_EventSnd_Init(void)
{
    PS2_FRAME(0x10);
    EventSnd = (struct_2c434764 *)Memm_Alloc(1, 0x800, 0x10);
    memset(EventSnd, 0, 0x800);
    EventSndNum = 0;
    df_set_mdl_esnd();
}
