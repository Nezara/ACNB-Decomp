/* src/event/evmi2.cpp - MI2 keyframe animation players for stage events (s_Evmi2[16]). The event PGC file holds a
 * table of MI2 tracks at +0x10 (__mi2_bind_info_st: offset of the track from the file start); a track's keys
 * (__mi2_tod_st, 0x80 bytes) start at +0x50 and run until a key with frame == 0xffff. Position / angle between key
 * n and n+1 are cubic splines over the key's frame count; EVMI2_Proc advances every active player by one frame. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"

/* key now_key of the player's track */
static __mi2_tod_st *evmi2_key(__evmi2_st *p)
{
    unsigned int id = p->mi2_id;
    char *pgc = (char *)EVMNG_GetPgcPtr();
    __mi2_bind_info_st *bind = (__mi2_bind_info_st *)(pgc + 0x10);
    if ((int)id > 0) bind += id;
    __mi2_tod_st *key = (__mi2_tod_st *)(pgc + bind->offset + 0x50);
    return key + p->now_key;
}

/* (frame - key) / frame as unsigned conversions (the key length is never 0 here) */
static float evmi2_rate(__evmi2_st *p, __mi2_tod_st *k)
{
    return (float)(unsigned int)(p->frame - k->key) / (float)k->frame;
}

void EVMI2_GetMI2InterpolateAng(float *ang, __evmi2_st *pEvmi2)
{
    PS2_FRAME(0x40);
    __mi2_tod_st *k = evmi2_key(pEvmi2);
    if (k->frame == 0xffff) {
        acvu0_CopyVectorF_ni(ang, k->angle);
        return;
    }
    float t = k->frame ? evmi2_rate(pEvmi2, k) : 1.0f;
    Anim_CubicSpline(ang, k->angle, k[1].angle, k->ang_outtan, k[1].ang_intan, t);
}

void EVMI2_GetMI2InterpolatePos(float *pos, __evmi2_st *pEvmi2)
{
    PS2_FRAME(0x40);
    __mi2_tod_st *k = evmi2_key(pEvmi2);
    if (k->frame == 0xffff) {
        acvu0_CopyVectorF_ni(pos, k->position);
        return;
    }
    float t = k->frame ? evmi2_rate(pEvmi2, k) : 0.0f;
    Anim_CubicSpline(pos, k->position, k[1].position, k->pos_outtan, k[1].pos_intan, t);
}

int EVMI2_IsMI2AnimEnd(__evmi2_st *pEvmi2)
{
    if (!pEvmi2->use) return 1;
    PS2_FRAME(0x30);
    return evmi2_key(pEvmi2)->frame == 0xffff;
}

/* No free check: with all 16 players busy the original fills in PS2 address 0 and returns 0 (never in practice;
 * the port returns 0 without the stores). */
__evmi2_st *EVMI2_Entry(unsigned char anim_id, unsigned char loop)
{
    int i;
    for (i = 0; i < 16; i++)
        if (!s_Evmi2[i].use) break;
    if (i == 16) return 0;
    __evmi2_st *p = &s_Evmi2[i];
    p->use = 1;
    p->loop = loop;
    p->mi2_id = anim_id;
    p->frame = 0;
    p->now_key = 0;
    return p;
}

void EVMI2_Proc(void)
{
    PS2_FRAME(0x40);
    for (int i = 0; i < 16; i++) {
        __evmi2_st *p = &s_Evmi2[i];
        if (p->use != 1) continue;
        if (evmi2_key(p)->frame == 0xffff) {   /* EVMI2_IsMI2AnimEnd, inlined */
            if (p->loop == 1) {
                p->frame = 0;
                p->now_key = 0;
            } else {
                ZeroMemory32b(p, 4);
            }
        } else {
            p->frame++;
            __mi2_tod_st *k = evmi2_key(p);
            if (p->frame >= (unsigned int)(k->key + k->frame)) p->now_key++;
        }
    }
}

void EVMI2_Init(void)
{
    ZeroMemory32b(s_Evmi2, 0x40);
}
