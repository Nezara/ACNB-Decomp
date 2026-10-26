/* src/event/door.cpp - sliding doors of event / mission stages. s_pDoor (32 slots of 0x40 bytes, DOOR_Create) holds the
 * doors that are opening (state 1), waiting (2) or closing (3); each runs its handler s_pDoorProcFunc[pid] every frame
 * (DOOR_DoorProc). DoorData (stage data, DoorDataSt) gives the door parameters: the `num` models stepped by move /
 * move_frame per frame, the wait time, the trigger distance and the sound. The handlers differ in what keeps a door
 * open (ACs, enemies, a linked model within `dist` of the door centre) or whether it opens at all (locked: hack
 * level too low, linked model destroyed). Gimmick scripts open doors through DOOR_SetEventDoorState /
 * DOOR_EventDoorOpen, the player's AC through DF_DOOR_DoorOpen. In Versus only the idle DOOR_DoorProc loop runs. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"

#ifdef ACNB_SOURCEPORT
void (*s_pDoorProcFunc[128])(struct_85242c6c *) = {
    __door_DoorProc_00, __door_DoorProc_01, __door_DoorProc_02, __door_DoorProc_03, __door_DoorProc_04,
    __door_DoorProc_05, __door_DoorProc_06, __door_DoorProc_07, __door_DoorProc_08, __door_DoorProc_09,
    __door_DoorProc_10, __door_DoorProc_11,
#define XX4 __door_DoorProc_xx, __door_DoorProc_xx, __door_DoorProc_xx, __door_DoorProc_xx
#define XX16 XX4, XX4, XX4, XX4
    XX16, XX16, XX16, XX16, XX16, XX16, XX16, XX4,
#undef XX16
#undef XX4
};
#endif

/* light off (dflag 2: light typedata[1] to data 2) and free the slot */
static void door_release(struct_85242c6c *pDoor)
{
    __door_DoorStopSound(pDoor);
    if (pDoor->pDoorData->dflag & 2) DF_LIT_Set((unsigned char)pDoor->mp->array->typedata[1], 2);
    ZeroMemory32b(pDoor, 0x10);
    pDoor->state = 0;
}

/* the model linked to the door (typedata[2] = model type, typedata[4..5] = aid) */
static StModel *door_link(struct_85242c6c *pDoor)
{
    char *td = pDoor->mp->array->typedata;
    return STAGE_GetModelPointer((unsigned char)td[2], *(unsigned short *)(td + 4));
}

/* centre - pos (VU .xyz subtract into the stack temporary d, w = centre's w), |d|^2 = z*z + (x*x + y*y) <= dist^2 */
static int door_near(struct_85242c6c *pDoor, const float *pos, float *d)
{
    for (int k = 0; k < 3; k++) d[k] = pDoor->center[k] - pos[k];
    memcpy(&d[3], &pDoor->center[3], 4);
    float xx = d[0] * d[0], yy = d[1] * d[1], zz = d[2] * d[2];
    return zz + (xx + yy) <= pDoor->pDoorData->dist * pDoor->pDoorData->dist;
}

/* sound by state (2 stop, 1 / 3 move) and the light on at the start of the opening (dflag 2) */
static void door_sound_light(struct_85242c6c *pDoor)
{
    if (pDoor->state == 2) __door_DoorStopSound(pDoor);
    else if (pDoor->state == 3 || pDoor->state == 1) __door_DoorMoveSound(pDoor);
    if ((pDoor->pDoorData->dflag & 2) && pDoor->state == 1 && pDoor->now_frame == 0)
        DF_LIT_Set((unsigned char)pDoor->mp->array->typedata[1], 1);
}

/* the common automatic door: (bAC, bEne, bMdl) = what keeps it open */
static void door_auto(struct_85242c6c *pDoor, int bAC, int bEne, int bMdl)
{
    PS2_FRAME(0x20);
    door_sound_light(pDoor);
    if (pDoor->state == 3) __door_DoorCloseState(pDoor, bAC, bEne, bMdl);
    else if (pDoor->state == 2) __door_DoorWaitState(pDoor, bAC, bEne, bMdl);
    else if (pDoor->state == 1) __door_DoorOpenState(pDoor, 1);
}

/* linked model alive -> handler 1, else 0 (model eid 0x32 whose arg0p byte 0x11 is 2 counts as gone) */
static void __door_DoorProc_11(struct_85242c6c *pDoor)
{
    PS2_FRAME(0x20);
    StModel *m = door_link(pDoor);
    if (!m) pDoor->pid = 0;
    else if (m->array->eid == 0x32 && ((unsigned char *)m->arg0p)[0x11] == 2) pDoor->pid = 0;
    else pDoor->pid = 1;
}

static void __door_DoorProc_10(struct_85242c6c *pDoor) { door_auto(pDoor, 1, 1, 1); }

/* follows the linked door: released unless that one has just started opening */
static void __door_DoorProc_09(struct_85242c6c *pDoor)
{
    PS2_FRAME(0x20);
    StModel *m = door_link(pDoor);
    struct_85242c6c *p = s_pDoor;
    for (int i = 0; i < 32; i++, p++) {
        if (p->mp != m) continue;
        if (p->state != 1 || p->now_frame != 0) {
            door_release(pDoor);
            return;
        }
        break;
    }
    __door_DoorProc_01(pDoor);
}

static void __door_DoorProc_08(struct_85242c6c *pDoor)
{
    PS2_FRAME(0x20);
    StModel *m = door_link(pDoor);
    pDoor->pid = m && (m->use & 4) ? 4 : 0;
}

static void __door_DoorProc_07(struct_85242c6c *pDoor) { door_auto(pDoor, 0, 1, 0); }

static void __door_DoorProc_06(struct_85242c6c *pDoor) { door_auto(pDoor, 1, 0, 0); }

/* handler from typedata[3] (-1 / 5 = release) */
static void __door_DoorProc_05(struct_85242c6c *pDoor)
{
    PS2_FRAME(0x20);
    char v = pDoor->mp->array->typedata[3];
    if (v == -1 || v == 5) door_release(pDoor);
    else pDoor->pid = (unsigned char)v;
}

static void __door_DoorProc_04(struct_85242c6c *pDoor) { door_auto(pDoor, 0, 0, 0); }

static void __door_DoorProc_03(struct_85242c6c *pDoor)
{
    PS2_FRAME(0x20);
    StModel *m = door_link(pDoor);
    pDoor->pid = m && (m->use & 4) ? 1 : 0;
}

/* opens once and stays open */
static void __door_DoorProc_02(struct_85242c6c *pDoor)
{
    PS2_FRAME(0x20);
    door_sound_light(pDoor);
    if (pDoor->state == 1) __door_DoorOpenState(pDoor, 0);
}

static void __door_DoorProc_01(struct_85242c6c *pDoor) { door_auto(pDoor, 1, 1, 0); }

/* locked door: buzz + "locked" message, released after wait_frame */
static void __door_DoorProc_00(struct_85242c6c *pDoor)
{
    PS2_FRAME(0x20);
    if (g_sortie.pwork->statMission) return;
    if (pDoor->state == 2) {
        pDoor->now_frame++;
        if (pDoor->pDoorData->wait_frame < pDoor->now_frame) door_release(pDoor);
    } else if (pDoor->state == 1) {
        DoorDataSt *d = pDoor->pDoorData;
        Voice_CallPoint(0xbe8, d->se_vol | 0x3c0000, (float (*)[4])pDoor->center, (float)d->se_dist);
        if (!MsgWnd_IsBusy(1)) MsgWnd_ShowCom(0x12, 0);
        pDoor->state = 2;
    }
}

static void __door_DoorProc_xx(struct_85242c6c *pDoor)
{
    PS2_FRAME(0x20);
    door_release(pDoor);
}

/* steps the door's models by mvec */
static void door_move(struct_85242c6c *pDoor, float *mvec)
{
    for (int i = 0; i < pDoor->pDoorData->num; i++) {
        StModel *m = STAGE_GetModelPointer(pDoor->mp->type, pDoor->mp->aid + i);
        if ((pDoor->pDoorData->dflag & 1) && m->array->eid == 0x50) continue;
        DF_CALC_MoveModel_Local(m, mvec);
    }
}

/* AC / enemy / linked model within dist of the door (stack temporaries at 0x60 / 0x50 / 0x40) */
static int door_triggered(struct_85242c6c *pDoor, int bAC, int bEne, int bMdl)
{
    if (bAC) {
        PS2_STACK(float[4], d, 0x60);
        for (int i = 0; i < NowPlayerNum; i++) {
            AcPlayerSt *a = &g_ac[i];
            if (a->hp <= 0 || (a->mp->use & 2)) continue;
            if (door_near(pDoor, a->loc, d)) return 1;
        }
    }
    if (bEne) {
        PS2_STACK(float[4], d, 0x50);
        for (int i = 0; i < EnemyNum; i++) {
            EnemySt *e = DFene_GetEnemy(i);
            if (e->use & 0x80) continue;
            if (door_near(pDoor, e->mp->loc, d)) return 1;
        }
    }
    if (bMdl) {
        StModel *m = door_link(pDoor);
        if (m && !(m->use & 4)) {
            PS2_STACK(float[4], d, 0x40);
            if (door_near(pDoor, m->loc, d)) return 1;
        }
    }
    return 0;
}

/* closing: back at the array location -> release (dflag 8: typedata[3] = -1); something in range -> reopen */
static void __door_DoorCloseState(struct_85242c6c *pDoor, int bAC, int bEne, int bMdl)
{
    PS2_FRAME(0x80);
    if (pDoor->now_frame <= 0) {
        memcpy(pDoor->mp->loc, pDoor->mp->array->loc, 16);
        if (pDoor->pDoorData->dflag & 8) pDoor->mp->array->typedata[3] = -1;
        door_release(pDoor);
        return;
    }
    if (door_triggered(pDoor, bAC, bEne, bMdl)) {
        pDoor->state = 1;
        return;
    }
    PS2_STACK(float[4], mv, 0x70);
    DoorDataSt *d = pDoor->pDoorData;
    mv[0] = ps2_div(-d->move[0], (float)d->move_frame);
    mv[1] = ps2_div(-d->move[1], (float)d->move_frame);
    mv[2] = ps2_div(-d->move[2], (float)d->move_frame);
    mv[3] = 1.0f;
    door_move(pDoor, mv);
    pDoor->now_frame--;
}

/* open: wait wait_frame frames, then close unless something is in range */
static void __door_DoorWaitState(struct_85242c6c *pDoor, int bAC, int bEne, int bMdl)
{
    PS2_FRAME(0x70);
    if (pDoor->now_frame < pDoor->pDoorData->wait_frame) {
        pDoor->now_frame++;
        return;
    }
    if (door_triggered(pDoor, bAC, bEne, bMdl)) return;
    pDoor->now_frame = pDoor->pDoorData->move_frame;
    pDoor->state = 3;
}

/* opening: move_frame steps, then wait (bClose) or stay open for good (typedata[0] = 1 unless pattern 5) */
static void __door_DoorOpenState(struct_85242c6c *pDoor, int bClose)
{
    PS2_FRAME(0x40);
    DoorDataSt *d = pDoor->pDoorData;
    if (pDoor->now_frame < d->move_frame) {
        PS2_STACK(float[4], mv, 0x30);
        mv[0] = ps2_div(d->move[0], (float)d->move_frame);
        mv[1] = ps2_div(d->move[1], (float)d->move_frame);
        mv[2] = ps2_div(d->move[2], (float)d->move_frame);
        mv[3] = 1.0f;
        door_move(pDoor, mv);
        pDoor->now_frame++;
    } else if (bClose) {
        pDoor->state = 2;
        pDoor->now_frame = 0;
    } else {
        char *td = pDoor->mp->array->typedata;
        if (d->patern != 5) td[0] = 1;
        td[3] = -1;
        door_release(pDoor);
    }
}

/* door centre: the array location, or (dflag 4) the mean of the door models' array locations */
static void __door_GetDoorCenterBase(DoorDataSt *pData, StModel *pMdl, float *center)
{
    PS2_FRAME(0x60);
    if (!(pData->dflag & 4)) {
        memcpy(center, pMdl->array->loc, 16);
        return;
    }
    center[0] = center[1] = center[2] = center[3] = 0.0f;
    int n = 0;
    for (int i = 0; i < pData->num; i++) {
        StModel *m = STAGE_GetModelPointer(pMdl->type, pMdl->aid + i);
        if ((pData->dflag & 1) && m->array->eid == 0x50) continue;
        for (int k = 0; k < 3; k++) center[k] = center[k] + m->array->loc[k];
        n++;
    }
    float r = ps2_div(1.0f, (float)n);
    for (int k = 0; k < 3; k++) center[k] = center[k] * r;
}

/* stops the moving sound and plays the stop sound (-1 = none playing, -2 = muted) */
static void __door_DoorStopSound(struct_85242c6c *pDoor)
{
    PS2_FRAME(0x20);
    if (pDoor->hSnd == -2 || pDoor->hSnd == -1) return;
    int id;
    switch (pDoor->pDoorData->se_id) {
    case 6: id = 0xbce; break;
    case 5: id = 0xbe5; break;
    case 4: id = 0xbde; break;
    case 3: id = 0xc07; break;
    default: id = 0; break;
    }
    if (id) {
        Voice_Stop(pDoor->hSnd, 0);
        DoorDataSt *d = pDoor->pDoorData;
        Voice_CallPoint(id, d->se_vol | 0x3c0000, (float (*)[4])pDoor->center, (float)d->se_dist);
    }
    pDoor->hSnd = -1;
}

static void __door_DoorMoveSound(struct_85242c6c *pDoor)
{
    PS2_FRAME(0x20);
    if (pDoor->hSnd != -1) return;
    DoorDataSt *d = pDoor->pDoorData;
    int vol = d->se_vol | 0x3c0000;
    float (*loc)[4] = (float (*)[4])pDoor->center;
    switch (d->se_id) {
    case 1: pDoor->hSnd = Voice_CallPoint(0xbe8, vol, loc, (float)d->se_dist); break;
    case 2: pDoor->hSnd = Voice_CallPoint(0xbbc, vol, loc, (float)d->se_dist); break;
    case 3: pDoor->hSnd = Voice_CallTraceLoc(0xc06, vol, loc, (float)d->se_dist); break;
    case 4: pDoor->hSnd = Voice_CallTraceLoc(0xbdd, vol, loc, (float)d->se_dist); break;
    case 5: pDoor->hSnd = Voice_CallTraceLoc(0xbe4, vol, loc, (float)d->se_dist); break;
    case 6: pDoor->hSnd = Voice_CallTraceLoc(0xbcd, vol, loc, (float)d->se_dist); break;
    }
}

/* scripted state of an event door (pattern 5): reuses the door's busy slot or takes a free one */
void DOOR_SetEventDoorState(StModel *pDoorModel, int patern, int state, int bSound)
{
    PS2_FRAME(0x90);
    if (!pDoorModel || (pDoorModel->use & 4)) return;
    DoorDataSt *d = &DoorData[(unsigned char)pDoorModel->array->typedata[0]];
    if (d->patern != 5) return;
    struct_85242c6c *p = s_pDoor;
    int slot, found = 0;
    for (slot = 0; slot < 32; slot++, p++)
        if (p->mp == pDoorModel && p->state) {
            found = 1;
            break;
        }
    if (!found) {
        p = s_pDoor;
        for (slot = 0; slot < 32; slot++, p++)
            if (!p->state) break;
        if (slot == 32) return;
        ZeroMemory32b(p, 0x10);
    }
    p->pDoorData = d;
    if (!found) {
        p->now_frame = state == 1 || state == 0 ? 0 : d->move_frame;
    } else if (p->state == 2 && state == 1) {
        p->now_frame = p->pDoorData->move_frame;
    }
    p->mp = pDoorModel;
    p->pid = (unsigned char)patern;
    p->state = (char)state;
    p->index = (unsigned char)slot;
    p->hSnd = bSound ? -1 : -2;
    __door_GetDoorCenterBase(p->pDoorData, p->mp, p->center);
}

/* opens a door from a script (not when it is busy already) */
void DOOR_EventDoorOpen(StModel *door_mp, int sound_flag)
{
    PS2_FRAME(0x50);
    if (!door_mp || (door_mp->use & 4)) return;
    struct_85242c6c *p = s_pDoor;
    for (int i = 0; i < 32; i++, p++)
        if (p->mp == door_mp) return;
    p = s_pDoor;
    int slot;
    for (slot = 0; slot < 32; slot++, p++)
        if (!p->state) break;
    if (slot == 32) return;
    ZeroMemory32b(p, 0x10);
    DoorDataSt *d = &DoorData[(unsigned char)door_mp->array->typedata[0]];
    p->pid = d->patern;
    p->state = 1;
    p->index = (unsigned char)slot;
    p->mp = door_mp;
    p->now_frame = 0;
    p->pDoorData = d;
    p->hSnd = sound_flag ? -1 : -2;
    __door_GetDoorCenterBase(p->pDoorData, p->mp, p->center);
}

/* the player's AC (event_mp, type 4) opens a door: locked (handler 0, with sound) when its hack level is below the
 * door's; returns -1 when nothing opened or an event door (pattern 5) has no follow-up handler */
int DF_DOOR_DoorOpen(StModel *door_mp, StModel *event_mp, int sound_flag)
{
    PS2_FRAME(0x60);
    if (!door_mp || !event_mp || (door_mp->use & 4) || event_mp->type != 4) return -1;
    struct_85242c6c *p = s_pDoor;
    for (int i = 0; i < 32; i++, p++)
        if (p->mp == door_mp) return -1;
    p = s_pDoor;
    int slot;
    for (slot = 0; slot < 32; slot++, p++)
        if (!p->state) break;
    if (slot == 32) return -1;
    ZeroMemory32b(p, 0x10);
    unsigned int level = ((unsigned char *)event_mp->arg0p)[0x785];
    DoorDataSt *d = &DoorData[(unsigned char)door_mp->array->typedata[0]];
    if ((int)level < d->hack_lv) {
        p->pid = 0;
        sound_flag = 1;
    } else {
        p->pid = d->patern;
    }
    p->state = 1;
    p->index = (unsigned char)slot;
    p->mp = door_mp;
    p->now_frame = 0;
    p->pDoorData = d;
    p->hSnd = sound_flag ? -1 : -2;
    __door_GetDoorCenterBase(p->pDoorData, p->mp, p->center);
    if (p->pid == 5) return p->mp->array->typedata[3] == -1 ? -1 : 0;
    return 0;
}

void DOOR_DoorProc(void)
{
    PS2_FRAME(0x30);
    struct_85242c6c *p = s_pDoor;
    for (int i = 0; i < 32; i++, p++) {
        if (!p->state) continue;
        gcall(s_pDoorProcFunc[p->pid], p);
        if (p->mp && (p->mp->use & 4)) door_release(p);
    }
}

char DOOR_GetDoorState(StModel *pMdl)
{
    struct_85242c6c *p = s_pDoor;
    for (int i = 0; i < 32; i++, p++)
        if (p->state && p->mp == pMdl) return p->state;
    return 0;
}

void DOOR_Destroy(void) { s_pDoor = 0; }

void DOOR_Create(void)
{
    PS2_FRAME(0x10);
    s_pDoor = (struct_85242c6c *)Memm_Alloc(1, 0x800, 0x10);
    ZeroMemory32b(s_pDoor, 0x200);
    for (int i = 0; i < 32; i++) s_pDoor[i].state = 0;
}
