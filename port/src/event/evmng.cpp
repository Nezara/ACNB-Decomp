/* src/event/evmng.cpp — stage event manager. The stage's command buffer (Stage_GetCmdBuf) holds event commands
 * (__cmd_data_st: trigger terms + timed jobs). Up to 64 commands are tracked in s_pEvmng: "entry" (use) commands test
 * their mission / model terms every frame; when the terms hold for cmd->frame frames the command runs (run) and its
 * jobs fire on their frame (as script FIFO entries in s_pEventScr, executed by the job-kind handler table
 * s_pJobExecFunc), repeated exec_num times. __evmng_InitEventID maps the stage's event ids to models and reserves ids
 * 1..n for the ACs (n = STAGE_ModelNum[4]), which index per-AC data by number. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"

/* command n of a command buffer: int count at +4, commands from +0x10, each 0x100 + job_num * 0x40 bytes */
static __cmd_data_st *evmng_cmd(void *buf, int n)
{
    if (!buf || n < 0 || n >= *(int *)((char *)buf + 4)) return 0;
    char *p = (char *)buf + 0x10;
    while (n--) p += ((__cmd_data_st *)p)->job_num * 0x40 + 0x100;
    return (__cmd_data_st *)p;
}

/* first entry that is neither entered nor running; when all 64 are busy the original keeps the last one (quirk) */
static __evmng_st *evmng_free_slot(void)
{
    __evmng_st *e = s_pEvmng;
    for (int i = 0; i < 64; i++, e++)
        if (!e->flag.use && !e->flag.run) return e;
    return e - 1;
}

static __evmng_st *evmng_find(int cmd_num)
{
    __evmng_st *e = s_pEvmng;
    for (int i = 0; i < 64; i++, e++)
        if (e->cmd_num == cmd_num) return e;
    return 0;
}

static void evmng_reset(__evmng_st *e)
{
    e->cmd_num = 0xff;
    e->frame = 0;
    e->flag.use = 0;
    e->flag.run = 0;
    e->flag.wait = 0;
    e->flag.neglect = 0;
    e->admin.exec_frame = 0;
    e->admin.exec_count = 0;
    e->pCmdData = 0;
    e->admin.ep_flag = 0;
}

void EVMNG_EvwkIncrease(int index, int data) { Evwk[index] += data; }
void EVMNG_SetEvwkData(int index, int data) { Evwk[index] = data; }
int EVMNG_GetEvwkData(int index) { return Evwk[index]; }
int EVMNG_GetEventSndBuf(char index) { return s_EventSndBuf[(signed char)index]; }
void EVMNG_SetEventSndBuf(int handle, char index) { s_EventSndBuf[(signed char)index] = handle; }
void *EVMNG_GetPgcPtr(void) { return s_pPgcBuf; }

/* runs polygon-animation command polyg_id (from the PGV buffer) at once; ep_flag 1 keeps it from EVMNG_Delete */
void EVMNG_PolygExec(int polyg_id)
{
    __evmng_st *e = evmng_free_slot();
    __cmd_data_st *cmd = evmng_cmd(s_pPgvBuf, polyg_id);
    if (!cmd) return;
    e->cmd_num = (unsigned char)polyg_id;
    e->flag.use = 0;
    e->flag.run = 1;
    e->flag.wait = 0;
    e->flag.neglect = 0;
    e->admin.exec_frame = 0;
    e->admin.exec_count = 0;
    e->pCmdData = cmd;
    e->admin.ep_flag = 1;
}

/* queues one job as a script FIFO entry; kind 10 jobs run their script immediately */
static void __evmng_JobExec(__job_data_st *pJobData)
{
    PS2_FRAME(0x30);
    __scr_fifo_st *tab = s_pEventScr;
    int i;
    for (i = 0; i < 64; i++)
        if (!tab[i].kind) break;
    __scr_fifo_st *p = &tab[i];   /* none free: i = 64, past the 0x3000-byte buffer (original quirk) */
    memset(p, 0, 0xc0);
    p->kind = pJobData->job_type + 1;
    p->id = pJobData->ctrl_type;
    for (int j = 0; j < pJobData->data_num; j++) {
        p->arg[j].kind = 6;
        memcpy(&p->arg[j].val, &pJobData->data[j], 4);   /* raw float bits */
    }
    if (p->kind == 10) {
        SCRCMD_CallScript((unsigned char)(p->id + 1), p);
        p->kind = 0;
    }
}

/* one frame of a running command: fire the jobs due at exec_frame; at kill_frame repeat or retire it */
static void __evmng_CmdExec(__evmng_st *pEvmng)
{
    PS2_FRAME(0x60);
    if (pEvmng->cmd_num == 0xff) return;
    __cmd_data_st *cmd = pEvmng->pCmdData;
    __job_data_st *job = (__job_data_st *)((char *)cmd + 0x100);
    int n = cmd->job_num;
    for (int i = 0; i < n; i++, job++)
        if (pEvmng->admin.exec_frame == job->frame) __evmng_JobExec(job);
    if (cmd->kill_frame == pEvmng->admin.exec_frame) {
        pEvmng->admin.exec_count++;
        if (pEvmng->admin.exec_count < cmd->exec_num) {
            pEvmng->admin.exec_frame = 0;
            return;
        }
        evmng_reset(pEvmng);
    }
    pEvmng->admin.exec_frame++;   /* a retired entry ends with exec_frame 1 */
}

/* the EventTerm_* tests (OLSORTIE) clear *pRun_flag when their condition does not hold */
static void __evmng_SearchCmdModelTerms(__cmd_model_st *pCmdModel, int *pRun_flag)
{
    PS2_FRAME(0x30);
    if (pCmdModel->ap_ratio.flag.onoff == 1) EventTerm_ModelApRate(pCmdModel, pRun_flag);
    if (pCmdModel->area.flag.onoff == 1) EventTerm_IsInRegion(pCmdModel, pRun_flag);
    if (pCmdModel->sight.flag.onoff == 1) EventTerm_IsRegionInSight(pCmdModel, pRun_flag);
    if (pCmdModel->from_damage.flag.onoff == 1) EventTerm_FromDamage(pCmdModel, pRun_flag);
    if (pCmdModel->touchdown.flag.onoff == 1) EventTerm_TouchDown(pCmdModel, pRun_flag);
    if (pCmdModel->distance.flag.onoff == 1) EventTerm_Distance(pCmdModel, pRun_flag);
    if (pCmdModel->ac_param.flag.onoff == 1) EventTerm_AcParam(pCmdModel, pRun_flag);
    if (pCmdModel->multi_area.flag.onoff == 1) EventTerm_MultiArea(pCmdModel, pRun_flag);
    if (pCmdModel->door_state.flag.onoff == 1) EventTerm_DoorState(pCmdModel, pRun_flag);
}

static void __evmng_SearchCmdMissionTerms(__cmd_mission_st *pCmdMission, int *pRun_flag)
{
    PS2_FRAME(0x30);
    /* mission_start is not a run-time term (EVMNG_Init enters those commands) */
    if (pCmdMission->mission_end.flag.onoff == 1) EventTerm_IsMissionEnd(pCmdMission, pRun_flag);
    if (pCmdMission->exec_frame.flag.onoff == 1) EventTerm_ExecFrame(pCmdMission, pRun_flag);
    if (pCmdMission->obliterate_num.flag.onoff == 1) EventTerm_EnemyDestNum(pCmdMission, pRun_flag);
    if (pCmdMission->survivor_num.flag.onoff == 1) EventTerm_EnemyRemainNum(pCmdMission, pRun_flag);
    if (pCmdMission->group_obliterate_num.flag.onoff == 1) EventTerm_EnemyGrmDestNum(pCmdMission, pRun_flag);
    if (pCmdMission->group_survivor_num.flag.onoff == 1) EventTerm_EnemyGrmRemainNum(pCmdMission, pRun_flag);
    if (pCmdMission->evwk_data.flag.onoff == 1) EventTerm_RefEvwkData(pCmdMission, pRun_flag);
    if (pCmdMission->model_crush_num.flag.onoff == 1) EventTerm_ModelDestNum(pCmdMission, pRun_flag);
    if (pCmdMission->timer.flag.onoff == 1) EventTerm_CmpVsTimer(pCmdMission, pRun_flag);
    if (pCmdMission->evwk_cmp.flag.onoff == 1) EventTerm_CmpEvwkData(pCmdMission, pRun_flag);
}

/* an entered command: once its terms hold (inverted when !authenticity) they stop being tested (neglect); after
 * cmd->frame more frames the command starts running */
static void __evmng_SearchCmdTerms(__evmng_st *pEvmng)
{
    PS2_FRAME(0x50);
    PS2_STACK(int, run_flag, 0x4c);   /* its address goes to the interpreted EventTerm_* */
    run_flag = 1;
    __cmd_data_st *cmd = pEvmng->pCmdData;
    if (!cmd) return;
    if (!pEvmng->flag.neglect) {
        __evmng_SearchCmdMissionTerms(&cmd->cmd_mission, &run_flag);
        __evmng_SearchCmdModelTerms(&cmd->cmd_model, &run_flag);
    }
    if (cmd->authenticity == 0) run_flag = (run_flag != 0) ^ 1;
    if (run_flag != 1) return;
    pEvmng->flag.neglect = 1;
    if (pEvmng->frame < cmd->frame) {
        pEvmng->frame++;
    } else {
        pEvmng->flag.use = 0;
        pEvmng->flag.run = 1;
    }
}

/* flag 1 kill AC, 2 kill AC 2, 3 break object, 4 damage count (+1, val ignored), 5 AC appears; val >= 2 ignored */
void EVMNG_SetMissionResultFlag(unsigned char flag, unsigned char val)
{
    if (val >= 2) return;
    StMissionResult *r = g_sortie.pmisres;
    switch (flag) {
    case 5: r->flagApeearsAc = val; break;
    case 4: r->numDamage++; break;
    case 3: r->flagBreakObj = val; break;
    case 2: r->flagKillAc2 = val; break;
    case 1: r->flagKillAc = val; break;
    }
}

/* Info_GetData's 4-byte union comes back through a hidden pointer: the original's stack slot */
static int evmng_info_get(int type, int id, unsigned int slot)
{
    PS2_STACK(union_9a8d3bef, r, slot);
    gcall_sret(GFN(Info_GetData), &r, type, id, 0, 0);
    return r.i;
}

/* kill count / kill rate / elapsed seconds / success for the mission result screen */
void EVMNG_SetMissionResultData(StMissionResult *pmisres)
{
    PS2_FRAME(0x30);
    PS2_STACK(int, remain, 0x2c);
    PS2_STACK(int, total, 0x28);
    remain = evmng_info_get(3, 7, 0x24);
    total = evmng_info_get(3, 3, 0x20);
    pmisres->numKillEne = total - remain;
    if (total <= 0) {
        pmisres->rateKillEne = 0;
    } else {
        float f = 100.0f * ((float)(total - remain) / (float)total);
        unsigned int u = f >= 2147483648.0f ? (unsigned int)ps2_ftoi(f - 2147483648.0f) | 0x80000000u : (unsigned int)ps2_ftoi(f);
        pmisres->rateKillEne = (unsigned char)u;
    }
    pmisres->timeLapsed = g_sortie.psettle->timeMission / 60;
    pmisres->flagSuccess = g_sortie.pwork->isSuccess != 0;
}

void EVMNG_CallScript(int type, int id, __scr_fifo_st *pScrFifo)
{
    gcall(s_pJobExecFunc[type - 1], (unsigned char)id, pScrFifo);
}

__evmng_event_id_st *EVMNG_GetEventID(int eventID) { return &s_EventID[eventID]; }

/* switches every enemy's model, regeneration, AI and control on or off */
void EVMNG_AllChrOnOff(int bOnOff)
{
    PS2_FRAME(0x20);
    for (int i = 0; i < EnemyNum; i++) {
        EnemySt *e = DFene_GetEnemy(i);
        if (e->use & 0x80) continue;
        if (bOnOff) {
            e->mp->use &= (e->edat->flag & 0x4000) ? 0xfffa : 0xfffc;
            e->sw.regene = 1;
            e->sw.think = 1;
            e->sw.control = 1;
        } else {
            e->mp->use |= 3;
            e->sw.regene = 0;
            e->sw.think = 0;
            e->sw.control = 0;
        }
    }
}

int EVMNG_IsEventExec(int cmd_num)
{
    __evmng_st *e = evmng_find(cmd_num);
    return e && e->flag.run == 1;
}

int EVMNG_IsEventEntry(int cmd_num)
{
    __evmng_st *e = evmng_find(cmd_num);
    return e && e->flag.use == 1;
}

/* drops an entered command that has not started running */
void EVMNG_ReferDelete(int cmd_num)
{
    __evmng_st *e = evmng_find(cmd_num);
    if (e && !e->flag.run) evmng_reset(e);
}

void EVMNG_Wait(int cmd_num, int bWait)
{
    __evmng_st *e = evmng_find(cmd_num);
    if (e) e->flag.wait = bWait != 0;
}

void EVMNG_AllDelete(void)
{
    for (int i = 0; i < 64; i++) evmng_reset(&s_pEvmng[i]);
    DF_ScrMessage_CancelMessage(0);
}

/* every entry of cmd_num except polygon-animation ones (ep_flag 1) */
void EVMNG_Delete(int cmd_num)
{
    __evmng_st *e = s_pEvmng;
    for (int i = 0; i < 64; i++, e++)
        if (e->admin.ep_flag != 1 && e->cmd_num == cmd_num) evmng_reset(e);
}

/* runs command cmd_num at once */
void EVMNG_Exec(int cmd_num)
{
    __evmng_st *e = evmng_free_slot();
    __cmd_data_st *cmd = evmng_cmd(s_pCmdBuf, cmd_num);
    if (!cmd) return;
    e->cmd_num = (unsigned char)cmd_num;
    e->flag.use = 0;
    e->flag.run = 1;
    e->flag.wait = 0;
    e->flag.neglect = 0;
    e->admin.exec_frame = 0;
    e->admin.exec_count = 0;
    e->pCmdData = cmd;
}

/* enters command cmd_num: its terms are tested every frame */
void EVMNG_Entry(int cmd_num)
{
    if (cmd_num == 0xff) return;
    __evmng_st *e = evmng_free_slot();
    __cmd_data_st *cmd = evmng_cmd(s_pCmdBuf, cmd_num);
    if (!cmd) return;
    e->cmd_num = (unsigned char)cmd_num;
    e->flag.use = 1;
    e->flag.run = 0;
    e->flag.wait = 0;
    e->flag.neglect = 0;
    e->admin.exec_frame = 0;
    e->admin.exec_count = 0;
    e->pCmdData = cmd;
}

void DF_EVMNG_Proc(void)
{
    PS2_FRAME(0x30);
    if (!s_pCmdBuf) return;
    __evmng_st *e = s_pEvmng;
    if (!e) return;
    for (int i = 0; i < 64; i++, e++) {
        if (e->flag.wait == 1) continue;
        if (e->flag.use == 1) __evmng_SearchCmdTerms(e);
        if (e->flag.run == 1) __evmng_CmdExec(e);
    }
    for (int i = 0; i < 64; i++) {
        __scr_fifo_st *p = &s_pEventScr[i];
        if (p->kind) gcall(s_pJobExecFunc[p->kind - 1], (unsigned char)(p->id + 1), p);
    }
    EVMI2_Proc();
    EVPOLYG_Proc();
}

void EVMNG_Init(void)
{
    PS2_FRAME(0x30);
    s_pCmdBuf = 0;
    s_pEvmng = 0;
    s_pEventScr = 0;
    s_pPgvBuf = 0;
    s_pReadBuf = 0;
    s_pEvDatBuf = 0;
    s_pPgcBuf = 0;
    s_pCmdBuf = Stage_GetCmdBuf();
    s_pPgvBuf = Stage_GetPgvBuf();
    s_pPgcBuf = Stage_GetPgcBuf();
    s_pEvmng = (__evmng_st *)Memm_Alloc(1, 0x400, 0x10);
    ZeroMemory32b(s_pEvmng, 0x100);   /* size in words */
    for (int i = 0; i < 64; i++) evmng_reset(&s_pEvmng[i]);
    s_pEventScr = (__scr_fifo_st *)Memm_Alloc(1, 0x3000, 0x10);
    ZeroMemory32b(s_pEventScr, 0xc00);
    for (int i = 0; i < 64; i++) s_pEventScr[i].kind = 0;
    if (!s_pCmdBuf) return;   /* no command buffer: event ids, sound handles, EVPOLYG / EVMI2 stay as they are */
    int n = *(int *)((char *)s_pCmdBuf + 4);
    for (int i = 0; i < n; i++) {
        __cmd_data_st *cmd = evmng_cmd(s_pCmdBuf, i);
        if (!cmd) break;
        if (cmd->cmd_mission.mission_start.flag.onoff == 1) EVMNG_Entry(i);
    }
    __evmng_InitEventID();
    for (int i = 0; i < 64; i++) s_EventSndBuf[i] = 0x51;
    EVPOLYG_Init();
    EVMI2_Init();
}

/* STAGE_ModelNum[kind] = model count per kind (1 map, 2 object, 3 character, 4 AC, 8 destructible object). */
static void map_event_ids(StModel *tbl, int num, int type)
{
    if (!tbl) return;
    for (int i = 0; i < num; i++) {
        unsigned char ev = tbl[i].array->evt_id;
        if (!ev) continue;
        s_EventID[ev].type = type;
        s_EventID[ev].id = i;
    }
}

static void __evmng_InitEventID(void)
{
    for (int i = 0; i < 0x100; i++) {
        s_EventID[i].type = 0;
        s_EventID[i].id = 0;
    }
    map_event_ids(Map, STAGE_ModelNum[1], 1);
    map_event_ids(Obj, STAGE_ModelNum[2], 2);
    for (int i = 1; i <= STAGE_ModelNum[4]; i++) {   /* event id i = AC slot i - 1 */
        s_EventID[i].type = 4;
        s_EventID[i].id = g_ac[i - 1].id;
    }
    map_event_ids(Dobj, STAGE_ModelNum[8], 8);
    map_event_ids(Chr, STAGE_ModelNum[3], 3);
}
