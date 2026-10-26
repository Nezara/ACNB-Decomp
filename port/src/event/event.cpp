/* src/event/event.cpp — stage event glue. DF_StageEventProcInit sets the stage up (hit / het / hitbox structures, enemies,
 * squads, sprite and destructible model lists, gimmicks, doors, lights, items, conditions, rain, sounds, messages,
 * thunder, the stage's own init function from g_StageEventInitFunc, spawn locations, COM setup = one ComInit per
 * AI-driven AC / mission enemy in AC slot order, the event manager and the stage script); DF_StageEventProc runs them
 * every frame. Models flagged destructible (mflag 8) are destroyed when their hp reaches 0 (death script, sound,
 * relation message, replacement model = the next entry), sprite models (mflag 0x4004 or a relation) follow their
 * parent character's object matrix. DF_EVT_TermProc ends the sortie: versus end conditions (free-for-all = battle
 * royal, 2-team annihilation, leader mode = exactly 2 leaders, MyI.leader[2]), win/lose bookkeeping and the
 * end-of-battle sequence, or the mission success / failure sequence. */
#include "acnb/sdk.h"
#include "acnb/vu0m.h"

static inline int sortie_mode(void) { return g_sortie.pconstant->modeSortie; }
static inline int ac_alive(int i) { return !g_ac[i].dead && !g_ac[i].areaovered; }

/* Enables / disables thinking, hit checks and drawing (and enemy control) of the friendly NPC ACs and characters.
 * < 0 = leave unchanged. */
void DF_EVT_SetNPCFlag(int think, int hit, int draw, int ctrl)
{
    for (int i = 0; i < StgInfo.num_fac; i++) {
        AcPlayerSt *a = &g_ac[StgInfo.top_fac + i];
        if (a->dead) continue;
        if (think >= 0) a->comthinkoff = think == 0;
        if (hit >= 0) {
            if (hit == 0) a->mp->use |= 2;
            else a->mp->use &= 0xfffd;
        }
        if (draw >= 0) {
            if (draw == 0) a->mp->use |= 1;
            else a->mp->use &= 0xfffe;
        }
    }
    EnemySt *e = Enemy + StgInfo.top_fchr;
    for (int i = 0; i < StgInfo.num_fchr; i++, e++) {
        if (e->use & 0x80) continue;
        if (think >= 0) {
            e->cns_flag.thinkoff = think == 0;
            e->sw.think = think != 0;
        }
        if (hit >= 0) {
            if (hit == 0) e->mp->use |= 2;
            else e->mp->use &= 0xfffd;
        }
        if (draw >= 0) {
            if (draw == 0) e->mp->use |= 1;
            else e->mp->use &= 0xfffe;
        }
        if (ctrl >= 0) {
            e->sw.control = ctrl;
            e->initialize = (unsigned char)ctrl;
        }
    }
}

/* Versus: the COM hates every AC that shares no team bit with it (FFA ACs without team bits: all but itself). */
static void __event_AcSetTarget2(int comno)
{
    PS2_FRAME(0x70);
    comp = &com_info[comno];
    AcPlayerSt *acp = comp->body.com_acp;
    if (comp->type != 0 || !acp || !acp->mp) return;
    unsigned int team = acp->mp->cflag & 0xf;
    for (int i = 0; i < NowPlayerNum; i++) {
        if (i == acp->id || (team & g_ac[i].mp->cflag)) continue;
        Com_Tg_AddHl(&com_info[comno], DF_SCR_GetModelPointer(3, i), 0x40, 0, 0x80);
    }
}

/* One ComInit per AI: memory card AIs on user slots, mission ACs / enemies, friend NPCs, arena ACs, AI ACs. COM numbers
 * count up in that order; ComKeyIndex maps each COM that drives an AC to its slot (whose pad it writes). */
static void __event_ComSetup(void)
{
    PS2_FRAME(0x60);
    DF_NPCTAB_GetACNum();
    DF_NPCTAB_GetChrNum();
    ComNum = 0;
    int slot = 0;
    memset(&ComKeyIndex[0], 0, 4);
    for (int i = 0; i < NowUserPlayerNum; i++, slot++) {
        if (!UseAI[i]) continue;
        ComInit(ComNum, 0, &g_ac[slot]);
        ComKeyIndex[ComNum] = (char)slot;
        ComNum++;
    }
    for (int i = 0; i < Array->head.acnum + Array->head.mtnum; i++) {
        struct_441fa347 *mi = Stage_GetMissionAcInfo(ComNum);
        if (mi->chrid > 0) {
            ComInit(ComNum, mi->chrid, 0);   /* enemy character */
        } else {
            ComInit(ComNum, 0, &g_ac[slot]);
            ComKeyIndex[ComNum] = (char)slot;
            slot++;
        }
        ComNum++;
    }
    int nfrnd = DF_NPCTAB_GetFrndNum();
    unsigned int chr = Array->head.cnum;   /* enemy characters are numbered after the stage's own */
    for (int i = 0; i < nfrnd; i++) {
        if (DF_NPCTAB_IsForce(i)) continue;
        int type = UseAI[slot] ? 0 : DF_NPCTAB_GetMechType(i);
        if (type == 0) {
            ComInit(ComNum, 0, &g_ac[slot]);
            ComKeyIndex[ComNum] = (char)slot;
            slot++;
            ComNum++;
        } else {
            if (type == 1) {
                ComInit(ComNum, chr + 1, 0);
                ComNum++;
            }
            chr += type == 2 ? 3 : 1;
        }
    }
    for (int i = 0; i < NowArenaACNum; i++) {
        ComInit(ComNum, 0, &g_ac[slot]);
        ComKeyIndex[ComNum] = (char)slot;
        slot++;
        ComNum++;
    }
    for (int i = 0; i < NowAIACNum; i++) {
        ComInit(ComNum, 0, &g_ac[slot]);
        ComKeyIndex[ComNum] = (char)slot;
        slot++;
        ComNum++;
    }
    Com_Dmg_Init();
    int m = sortie_mode();
    if (m == 5 || m == 4 || m == 3 || m == 2)
        for (int i = 0; i < ComNum; i++) __event_AcSetTarget2(i);
}

/* Win / lose / draw (0 / 1 / 2) into the versus records (human player slots only) and the sortie result. */
static void __event_UpdateVsCount(int player, int win)
{
    PS2_FRAME(0x10);
    if (MyI.replaymode || player < 0 || player >= 4) return;
    int vs = sortie_mode() == 4 || sortie_mode() == 5;
    if (win == 2 || win == 1) {
        if (vs) vs_ac_info[player].losenum++;
        Sortie_SetVsWinner(player, 0, g_ac[player].areaovered);
    } else if (win == 0) {
        if (vs) vs_ac_info[player].winnum++;
        Sortie_SetVsWinner(player, 1, g_ac[player].areaovered);
    }
}

/* Surviving ACs of team 0 / 1 (cflag bit 0 / 1) and their summed AP. */
static int __event_GetTeamAp(int team, int *app)
{
    unsigned int bit = team ? 2 : 1;
    int n = 0, ap = 0;
    for (int i = 0; i < NowPlayerNum; i++) {
        AcPlayerSt *a = &g_ac[i];
        if ((a->mp->cflag & bit) && !a->dead && !a->areaovered) {
            n++;
            ap += a->hp;
        }
    }
    if (app) *app = ap;
    return n;
}

static int __event_GetSurvivorNum(int *idp)
{
    int n = 0, last = 0xff;
    for (int i = 0; i < NowPlayerNum; i++)
        if (ac_alive(i)) {
            n++;
            last = i;
        }
    if (idp) *idp = n == 1 ? last : 0xff;
    return n;
}

/* Every AC of a team: signal (win/lose/draw) and record. */
static void team_signal(unsigned int bit, int win)
{
    for (int i = 0; i < NowPlayerNum; i++)
        if (g_ac[i].mp->cflag & bit) DF_SGNL_StartWinLose(i, win);
}

static void team_record(unsigned int bit, int win, int is_win)
{
    for (int i = 0; i < NowPlayerNum; i++) {
        AcPlayerSt *a = &g_ac[i];
        if (a->mp->cflag & bit) {
            __event_UpdateVsCount(i, win);
            Sortie_SetVsWinner(i, is_win, a->areaovered);
        }
    }
}

/* Marks the fallen ACs (signal 3) while the battle goes on. */
static void signal_fallen(void)
{
    for (int i = 0; i < NowPlayerNum; i++)
        if (g_ac[i].dead || g_ac[i].areaovered) DF_SGNL_StartWinLose(i, 3);
}

/* team 0 (cflag bit 0) vs team 1 (bit 1): last team standing, or more total AP when the time runs out. */
static int __event_EndConditionsForTeamAnn(void)
{
    PS2_FRAME(0x40);
    PS2_STACK(int[2], ap, 0x38);   /* ap[0], ap[1] at their original stack slots */
    int n0 = __event_GetTeamAp(0, &ap[0]);
    int n1 = __event_GetTeamAp(1, &ap[1]);
    int winner;   /* 0 / 1 = team, 2 = draw */
    if (n0 == 0 && n1 == 0) winner = 2;
    else if (n0 == 0) winner = 1;
    else if (n1 == 0) winner = 0;
    else {
        if (AC_VS_RESTTIME != 0) {
            signal_fallen();
            return 0;
        }
        Sortie_SetVsTimeOver(1);
        winner = ap[0] > ap[1] ? 0 : ap[1] > ap[0] ? 1 : 2;
    }
    if (winner == 2) {
        team_signal(1, 2);
        team_signal(2, 2);
        team_record(1, 2, 0);
        team_record(2, 2, 0);
        return 1;
    }
    if (winner == 0) {
        team_signal(1, 0);
        team_signal(2, 1);
        team_record(1, 0, 1);
        team_record(2, 1, 0);
    } else {
        team_signal(1, 1);
        team_signal(2, 0);
        team_record(1, 1, 0);
        team_record(2, 0, 1);
    }
    if (!MyI.replaymode) MyI.vswinner = (unsigned char)winner;
    return 1;
}

/* Free-for-all: last AC standing, or the most AP when the time runs out (a tie for the most AP = draw for those
 * ACs, loss for the rest). */
static int __event_EndConditionsForBattleRoyal(void)
{
    PS2_FRAME(0x50);
    PS2_STACK(int[2], sv, 0x48);   /* survivor id (0x48), AP leader (0x4c) */
    int best = -1;
    for (int i = 0; i < NowPlayerNum; i++)
        if (ac_alive(i) && best < g_ac[i].hp) {
            best = g_ac[i].hp;
            sv[1] = i;
        }
    int ntop = 0;
    for (int i = 0; i < NowPlayerNum; i++)
        if (ac_alive(i) && !(g_ac[i].hp < best)) ntop++;

    int n = __event_GetSurvivorNum(&sv[0]);
    if (n == 0) {
        for (int i = 0; i < NowPlayerNum; i++)
            if (DF_SGNL_StartWinLose(i, 2)) __event_UpdateVsCount(i, 2);
        return 1;
    }
    if (n == 1) {
        for (int i = 0; i < NowPlayerNum; i++) {
            int w = i == sv[0] ? 0 : 1;
            DF_SGNL_StartWinLose(i, w);
            __event_UpdateVsCount(i, w);
        }
        if (!MyI.replaymode) MyI.vswinner = (unsigned char)sv[0];
        return 1;
    }
    if (AC_VS_RESTTIME != 0) {
        signal_fallen();
        return 0;
    }
    if (ntop == 0) {
        for (int i = 0; i < NowPlayerNum; i++) {
            DF_SGNL_StartWinLose(i, 2);
            __event_UpdateVsCount(i, 2);
        }
    } else if (ntop == 1) {
        for (int i = 0; i < NowPlayerNum; i++) {
            int w = i == sv[1] ? 0 : 1;
            DF_SGNL_StartWinLose(i, w);
            __event_UpdateVsCount(i, w);
        }
        if (!MyI.replaymode) MyI.vswinner = (unsigned char)sv[1];
    } else {
        for (int i = 0; i < NowPlayerNum; i++) {
            int w = (g_ac[i].hp < best || g_ac[i].areaovered) ? 1 : 2;
            DF_SGNL_StartWinLose(i, w);
            __event_UpdateVsCount(i, w);
        }
    }
    Sortie_SetVsTimeOver(1);
    return 1;
}

static inline void hide_gauge(void)
{
    if (ViewNum > 1 || !ViewMode_UseCockpit(MyI.myplayerno[0])) MyI.gauge = 0;
}

/* Result pause after the win/lose signals: free-battle losers get the retry menu (2), everyone else 3. */
static void result_pause(int from_signals)
{
    int m = sortie_mode();
    if (m == 5) {   /* (sic) only the signal path locks the system keys */
        DF_FADE_SetProc(0, 3, 0x3c);
        s_TermFrame = 0x1e0;
        s_TermPhase = 2;
        if (from_signals) MyI.syskeyoff = 1;
        return;
    }
    if (m == 6 || m == 3 || m == 2)
        LOOP_SetPause(!g_sortie.pconstant->flagFree || !MyI.vswinner ? 2 : 3);
    else
        LOOP_SetPause(3);
    DF_PAUSE_Init();
    s_TermFrame = 0x1e0;
    s_TermPhase = 4;
}

/* Versus end-of-battle sequence: end condition of the form -> stop all ACs -> signals -> result / fade out. */
static void __event_TermProcVS(int vsform)
{
    PS2_FRAME(0x30);
    if ((unsigned int)vsform >= 3 || !__event_TermProcVS__EndCond[vsform]) return;
    switch (s_TermPhase) {
    case 0:
        if (!gcall(__event_TermProcVS__EndCond[vsform])) break;
        MyI.padoff = 1;
        s_TermFrame = 0x1e0;
        {
            int m = sortie_mode();
            if (m == 3 || m == 6 || m == 2 || m == 5 || m == 4) {
                s_TermPhase = 3;
            } else {
                MyI.syskeyoff = 1;
                s_TermPhase = 1;
            }
        }
        for (int i = 0; i < NowPlayerNum; i++) AC_SetControl(&g_ac[i], 0);
        break;
    case 1:
        if (--s_TermFrame < 1) {
            DF_FADE_SetProc(0, 3, 0x3c);
            hide_gauge();
            s_TermPhase = 2;
        }
        break;
    case 2:
        if (DF_FADE_IsStop(0)) MyI.mainproc = 3;
        break;
    case 3:
        if (DF_SGNL_IsEndAll()) {
            hide_gauge();
            result_pause(1);
        }
        break;
    case 4:
        if (!MyI.pause) {
            s_TermFrame = 0x1e0;
            s_TermPhase = 2;
            MyI.syskeyoff = 1;
            DF_FADE_SetProc(0, 3, 0x3c);
        }
        break;
    case 5:
        if (--s_TermFrame < 1) {
            hide_gauge();
            result_pause(0);
        }
        break;
    }
}

/* Ids of the living ACs into tab; returns their count. */
int get_live_ac(int *tab)
{
    if (!tab) return 0;
    int *p = tab;
    for (int i = 0; i < NowPlayerNum; i++)
        if (ac_alive(i)) *p++ = i;
    return p - tab;
}

void Event_SetEvEcm(unsigned char index, unsigned char param) { s_EvEcm[index] = param; }

unsigned char Event_GetEvEcm(unsigned char index) { return s_EvEcm[index]; }

/* Destruction phase recorded for a destroyed object: the array's destPhase, else by the size of its bounding sphere
 * (sum of the sphere's x / y / z words * 0.33 against arrBBSize). */
static int __event_GetDestPhase(StModel *pMdl)
{
    unsigned int phase = pMdl->array->destMem.destPhase;
    if (phase) return (unsigned char)phase;
    float *s = pMdl->mlist->sphere;
    float size = (((0.0f + s[0]) + s[1]) + s[2]) * 0.33f;
    int i;
    for (i = 0; i < 2; i++)
        if (size < __event_GetDestPhase__arrBBSize[i]) break;
    return __event_GetDestPhase__arrPhase[i];
}

/* A destructible model's hp ran out: death script, destruction sound, message 3 to the related character, hide it
 * (unless it stays as wreck, mflag 0x10), count it, and show the replacement model (the next entry of its list) in its
 * place - which is destroyed at once too when it has no hp. */
static void __event_OneModelDestroy(StModel *mp)
{
    PS2_FRAME(0x30);
    _sound_attr_st *snd = mp->mlist->lp_reverb;
    if (mp->array->destscrid) DF_ScriptStart(mp->array->destscrid, mp);
    if (snd->destseid) Voice_CallPoint(snd->destseid, snd->vol, (float (*)[4])mp->mlist->sphere, snd->maxdist);
    if (mp->array->relation) {
        StModel *c = &Chr[mp->array->relation & 0x3ff];
        if (c->msgfunc) gcall_as<void(StModel *, int, StModel *)>(c->msgfunc, c, 3, mp);   /* 3 arguments */
    }
    if (!(mp->mflag & 0x10)) mp->use |= 7;
    int next = mp->aid + 1;
    Event_MdlDestroyCnt(mp);
    StModel *nm;
    switch (mp->type) {
    case 8: nm = &Dobj[next]; break;
    case 2: nm = &Obj[next]; break;
    case 1: nm = &Map[next]; break;
    default: nm = &Map[next]; break;
    }
    if (!(nm->mflag & 0x10)) return;
    nm->use &= 0xfffe;
    nm->use &= 0xfffd;
    if (nm->mflag & 8) nm->use |= 8;
    DF_MDL_SetModel(nm, mp->loc, mp->ang, 0);
    if (nm->kind & 1) {
        nm->use |= 0x10;
        Anim_SetAnimId(nm, 0);
        Anim_SetSfx(nm, 1);
    }
    if ((nm->mflag & 8) && nm->nowhp <= 0) __event_OneModelDestroy(nm);
}

/* Destroys the destructible models whose hp reached 0 (objects with an after-destruction animation only once it ran)
 * and plays the after-destruction animations (mflag 0x40: fade, hidden once the alpha is below 0.01). */
static void __event_MapDestroyProc(void)
{
    PS2_FRAME(0x40);
    for (int i = 0; i < s_DestModelNum; i++) {
        StModel *mp = s_pDestModel[i];
        unsigned int use = mp->use;
        if (use & 4) continue;
        if (mp->type == 2 && (mp->mflag & 0x10) && (!(use & 8) || (use & 0x10))) continue;
        if (mp->nowhp) continue;
        __event_OneModelDestroy(mp);
    }
    for (int i = 0; i < s_AfterDestModelNum; i++) {
        StModel *mp = s_pAfterDestModel[i];
        if (mp->use & 4) continue;
        if (!(mp->use & 0x10)) continue;
        Anim_AnimMi2TodUpdate(mp);
        if (mp->mflag & 0x40) mp->vuparam.mulalpha = mp->op[1].position[1];
        if (!Anim_IsEndTod(mp)) continue;
        mp->use &= 0xffef;
        mp->use |= 4;
        if ((mp->mflag & 0x40) && mp->vuparam.mulalpha < 0.01f) {
            mp->use |= 1;
            mp->use |= 2;
        }
    }
}

/* Lists the destructible map / object / dynamic-object models (mflag 8) and the objects with an after-destruction
 * animation (mflag 0x10). */
static void __event_GetDestroyModel(void)
{
    s_DestModelNum = 0;
    s_AfterDestModelNum = 0;
    StModel *mp = Map;
    for (int i = 0; i < Array->head.mnum; i++, mp++)
        if (mp->mflag & 8) s_pDestModel[s_DestModelNum++] = mp;
    for (int k = 0; k < 2; k++) {
        int n = k ? Array->head.dnum : Array->head.onum;
        mp = k ? Dobj : Obj;
        for (int i = 0; i < n; i++, mp++) {
            if (mp->mflag & 8) s_pDestModel[s_DestModelNum++] = mp;
            if (mp->mflag & 0x10) s_pAfterDestModel[s_AfterDestModelNum++] = mp;
        }
    }
}

/* Sprite models: mflag 0x4004 = own matrix; otherwise attached to object (relation >> 10) of character
 * (relation & 0x3ff): matrix, bounding sphere centre, height range, location and angles follow that object. A dead
 * character takes its destructible attachments' hp. */
static void __event_SpriteModelProc(void)
{
    PS2_FRAME(0xa0);
    for (int i = 0; i < s_SpriteModelNum; i++) {
        StModel *mp = s_pSpriteModel[i];
        unsigned int flag = mp->mflag;
        if (flag & 0x4004) {
            DF_CALC_UpdateModelMatrix(mp);
            continue;
        }
        unsigned int rel = mp->array->relation;
        if (!rel) continue;
        int obj = (int)(rel & 0xfc00) >> 10;
        StModel *c = &Chr[rel & 0x3ff];
        if (c->use & 4) {
            if ((flag & 8) && mp->nowhp) mp->nowhp = 0;
            continue;
        }
        DF_CALC_UpdateModelMatrix_COPY(mp, *c->op[obj].wrmatp);
        float oldy = mp->mlist->sphere[1];
        PS2_STACK(float[4], ctr, 0x90);
        vu_apply(ctr, (const float (*)[4])mp->matrix2, mp->localcenter);
        mp->mlist->sphere[0] = ctr[0];
        mp->mlist->sphere[1] = ctr[1];
        mp->mlist->sphere[2] = ctr[2];
        float dy = mp->mlist->sphere[1] - oldy;
        mp->Ytop = mp->Ytop + dy;
        mp->Ybottom = mp->Ybottom + dy;
        memcpy(mp->loc, (*c->op[obj].wrmatp)[3], 16);
        DFvu0_Matrix2XZYang(mp->ang, *c->op[obj].wrmatp);
    }
}

/* map models with mflag 0x4004, objects / dynamic objects with mflag 0x4004 or a relation */
static void __event_GetSpriteModel(void)
{
    s_SpriteModelNum = 0;
    StModel *mp = Map;
    for (int i = 0; i < Array->head.mnum; i++, mp++)
        if (mp->mflag & 0x4004) s_pSpriteModel[s_SpriteModelNum++] = mp;
    for (int k = 0; k < 2; k++) {
        int n = k ? Array->head.dnum : Array->head.onum;
        mp = k ? Dobj : Obj;
        for (int i = 0; i < n; i++, mp++)
            if ((mp->mflag & 0x4004) || mp->array->relation) s_pSpriteModel[s_SpriteModelNum++] = mp;
    }
}

/* Mission end: phase 0 waits for the player's death (failure, statMission 2) / leaving the area (statMission 1) or the
 * mission result (1 / 3 = success / 5 = abort), phase 1 counts down (gauge off below 240 frames) and fades out,
 * phase 2 picks the next main proc. */
static void __event_TermProc_MISSION(void)
{
    PS2_FRAME(0x30);
    unsigned int stat = g_sortie.pwork->statMission;
    switch (s_TermPhase) {
    case 0:
        if (g_ac[0].areastat != 2 && g_ac[0].dead && !MyI.deadstop) {
            if (g_sortie.pconstant->modeSortie == 1) MsgWnd_ShowCom(0x15, 0);
            else if (!g_sortie.psettle->idGameOver || g_gamedata[0].clearnum) MsgWnd_ShowCom(5, 0);
            g_sortie.pwork->statMission = 2;
            s_TermFrame = 0x1e0;
            MyI.syskeyoff = 1;
            s_TermPhase = 1;
            break;
        }
        if (!g_ac[0].dead && !MyI.padoff && g_ac[0].areastat == 2) {
            if (!g_sortie.psettle->idGameOver || g_gamedata[0].clearnum) MsgWnd_ShowCom(6, 0);
            g_sortie.pwork->statMission = 1;
            s_TermFrame = 0x1e0;
            MyI.syskeyoff = 1;
            s_TermPhase = 1;
            break;
        }
        if (stat == 1) {
            if (!MyI.message) MsgWnd_ShowCom(8, 0);
            MyI.syskeyoff = 1;
            s_TermFrame = 0x1e0;
            s_TermPhase = 1;
        } else if (stat == 3) {
            PS2_STACK(int, frame, 0x2c);   /* the union argument goes through the stack */
            frame = FRAME;
            gcall_as<void(int, int, int, int, int)>(GFN(Info_SetData), 3, 1, 0, 0, frame);
            if (g_sortie.pconstant->modeSortie == 1) MsgWnd_ShowCom(0x14, 0);
            else if (!MyI.message) MsgWnd_ShowCom(4, 0);
            MyI.syskeyoff = 1;
            s_TermFrame = 0x1e0;
            s_TermPhase = 1;
        } else if (stat == 5) {
            PS2_STACK(int, frame, 0x28);
            frame = FRAME;
            gcall_as<void(int, int, int, int, int)>(GFN(Info_SetData), 3, 1, 0, 0, frame);
            s_TermFrame = 5;
            MyI.syskeyoff = 1;
            s_TermPhase = 1;
        }
        break;
    case 1:
        s_TermFrame--;
        AbVal_End();
        if (stat == 5) {
            if (s_TermFrame <= 0) MyI.mainproc = 7;
            break;
        }
        if (!((float)s_TermFrame < 240.0f)) break;
        if (!ViewMode_UseCockpit(0)) MyI.gauge = 0;
        if (s_TermFrame <= 0) {
            DF_FADE_SetProc(0, 3, 0x3c);
            s_TermPhase = 2;
        }
        break;
    case 2:
        if (!DF_FADE_IsStop(0)) break;
        if (stat == 4) MyI.mainproc = 4;
        else if (stat == 5) MyI.mainproc = 7;
        else if (g_sortie.pconstant->modeSortie == 1) MyI.mainproc = 2;
        else if (g_sortie.psettle->idGameOver && stat != 3) MyI.mainproc = 4;
        else if (g_sortie.pwork->idIntermezzo == 2) MyI.mainproc = 10;
        else if (g_sortie.pwork->idIntermezzo == 1) MyI.mainproc = 9;
        else MyI.mainproc = 2;
        break;
    }
}

/* Leader mode: the team whose leader (MyI.leader[0] = team 0, [1] = team 1) falls loses; at time over the leader with
 * more AP wins. The signals / records go to leader 0, the last team-0 member, leader 1, the last team-1 member (-1 =
 * none). */
static int __event_EndConditionsForTeam(void)
{
    PS2_FRAME(0x50);
    int l0 = MyI.leader[0], l1 = MyI.leader[1];
    int m0 = -1, m1 = -1;
    for (int i = 0; i < NowPlayerNum; i++) {
        if (i == l0 || i == l1) continue;
        if (g_ac[i].mp->cflag & 1) m0 = i;
        else m1 = i;
    }
    int w0, w1;   /* signal / record of team 0 / team 1 */
    int down0 = g_ac[l0].dead || g_ac[l0].areaovered;
    if (down0 && (g_ac[l1].dead || g_ac[l1].areaovered)) {
        w0 = w1 = 2;
    } else if (down0) {
        w0 = 1;
        w1 = 0;
    } else if (g_ac[l1].dead || g_ac[l1].areaovered) {
        w0 = 0;
        w1 = 1;
    } else {
        if (AC_VS_RESTTIME != 0) {
            if (m0 >= 0 && ac_alive(m0) == 0) DF_SGNL_StartWinLose(m0, 3);
            if (m1 >= 0 && ac_alive(m1) == 0) DF_SGNL_StartWinLose(m1, 3);
            return 0;
        }
        if (g_ac[l1].hp < g_ac[l0].hp) {
            w0 = 0;
            w1 = 1;
        } else if (g_ac[l0].hp < g_ac[l1].hp) {
            w0 = 1;
            w1 = 0;
        } else {
            w0 = w1 = 2;
        }
        DF_SGNL_StartWinLose(l0, w0);
        DF_SGNL_StartWinLose(m0, w0);
        DF_SGNL_StartWinLose(l1, w1);
        DF_SGNL_StartWinLose(m1, w1);
        __event_UpdateVsCount(l0, w0);
        __event_UpdateVsCount(m0, w0);
        __event_UpdateVsCount(l1, w1);
        __event_UpdateVsCount(m1, w1);
        if (w0 != 2 && !MyI.replaymode) MyI.vswinner = w0 == 0 ? 0 : 1;
        Sortie_SetVsTimeOver(1);
        return 1;
    }
    DF_SGNL_StartWinLose(l0, w0);
    DF_SGNL_StartWinLose(m0, w0);
    DF_SGNL_StartWinLose(l1, w1);
    DF_SGNL_StartWinLose(m1, w1);
    __event_UpdateVsCount(l0, w0);
    __event_UpdateVsCount(m0, w0);
    __event_UpdateVsCount(l1, w1);
    __event_UpdateVsCount(m1, w1);
    if (w0 != 2 && !MyI.replaymode) MyI.vswinner = w0 == 0 ? 0 : 1;
    return 1;
}

void DF_StageEventAfter_xxx(void) {}

void DF_StageEvent_xxx(void) {}

void DF_StageEventInit_xxx(void) {}

#ifdef ACNB_SOURCEPORT
void (*g_StageEventInitFunc[256])(void) = {   /* 0x25aff0 - map no. -> stage event (default DF_StageEventInit_xxx) */
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    StageEventInit_100, StageEventInit_101, StageEventInit_102, StageEventInit_103,
    StageEventInit_104, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, StageEventInit_110, StageEventInit_111,
    StageEventInit_112, StageEventInit_113, StageEventInit_114, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    StageEventInit_120, StageEventInit_121, StageEventInit_122, StageEventInit_123,
    StageEventInit_124, DF_StageEventInit_xxx, StageEventInit_120, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, StageEventInit_130, StageEventInit_131,
    StageEventInit_132, StageEventInit_133, StageEventInit_134, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    StageEventInit_140, StageEventInit_141, StageEventInit_142, StageEventInit_142,
    StageEventInit_142, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, StageEventInit_150, StageEventInit_151,
    StageEventInit_152, StageEventInit_153, StageEventInit_154, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
    DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx, DF_StageEventInit_xxx,
};
void (*g_StageEventFunc[256])(void) = {   /* 0x25b3f0 - map no. -> stage event (default DF_StageEvent_xxx) */
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    StageEvent_100, StageEvent_101, StageEvent_102, StageEvent_103,
    StageEvent_104, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, StageEvent_110, StageEvent_111,
    StageEvent_112, StageEvent_113, StageEvent_114, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    StageEvent_120, StageEvent_121, StageEvent_122, StageEvent_123,
    StageEvent_124, DF_StageEvent_xxx, StageEvent_120, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, StageEvent_130, StageEvent_131,
    StageEvent_132, StageEvent_133, StageEvent_134, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    StageEvent_140, StageEvent_141, StageEvent_142, StageEvent_142,
    StageEvent_142, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, StageEvent_150, StageEvent_151,
    StageEvent_152, StageEvent_153, StageEvent_154, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
    DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx, DF_StageEvent_xxx,
};
void (*DF_StageEventAfter[256])(void) = {   /* 0x25b7f0 - every map: DF_StageEventAfter_xxx */
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
    DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx, DF_StageEventAfter_xxx,
};
#endif

void DF_EVENT_EvStageWkInit(void) { ZeroMemory32b(EvStageWk, 0x40); }

/* stops the replay: all ACs lose control, the player's AC gets stat 0x4000, phase 5 (480 frames) */
void DF_EVT_QuitReplay(void)
{
    PS2_FRAME(0x30);
    if (s_TermPhase) return;
    MyI.padoff = 1;
    s_TermPhase = 5;
    s_TermFrame = 0x1e0;
    for (int i = 0; i < NowPlayerNum; i++) AC_SetControl(&g_ac[i], 0);
    g_ac[0].stat |= 0x4000;
}

/* quits the sortie with mission result missionmode (fade after 120 frames) */
void DF_EVT_GameQuit(int missionmode)
{
    PS2_FRAME(0x30);
    if (s_TermPhase) return;
    g_sortie.pwork->statMission = (unsigned char)missionmode;
    s_TermFrame = 0x78;
    MyI.syskeyoff = 1;
    MyI.padoff = 1;
    s_TermPhase = 1;
    for (int i = 0; i < NowPlayerNum; i++) AC_SetControl(&g_ac[i], 0);
    g_ac[0].stat |= 0x4000;
}

/* Per view: an AC that just left the area switches its view mode back from the cockpit (when its option asks for it)
 * and restores its image; then the end-of-sortie handler of the sortie mode. */
void DF_EVT_TermProc(void)
{
    PS2_FRAME(0x50);
    for (int i = 0; i < ViewNum; i++) {
        int pl = MyI.myplayerno[i];
        if (!g_ac[pl].areaovered) continue;
        unsigned int bit = 1 << i;
        if (MyI.areaovered & bit) continue;
        int m = g_sortie.pconstant->modeSortie;
        if (Sortie_GetAcOption(pl)->view_mode) {
            if (!MyI.replaymode) {
                DFcam_SetViewMode(pl, 0);
                DFcam_SetNoDelay(pl);
                MyI.gauge = m == 0 || m == 1 ? 0 : 1;
            }
            if (!AcOpt.txx && !g_sortie.pconstant->isSplit) AcLoader_RestoreImage();
        }
        MyI.areaovered |= (signed char)bit;
    }
    switch (g_sortie.pconstant->modeSortie) {
    case 4:
    case 5: __event_TermProcVS(MyI.vsform); break;
    case 2:
    case 6: __event_TermProcVS(0); break;
    case 3: __event_TermProcVS(1); break;
    case 8: break;
    default: __event_TermProc_MISSION(); break;
    }
}

void DF_StageEventProcAfter(void)
{
    PS2_FRAME(0x10);
    gcall(DF_StageEventAfter[g_sortie.pconstant->map]);
    WETH_DrawRain();
}

/* Every stage frame: mission time, the stage's event function, models, doors, gimmicks, lights, thunder, items,
 * ropes, conditions, hit effects, "all enemies dead", rain / weather and event sounds. */
void DF_StageEventProc(void)
{
    PS2_FRAME(0x10);
    if (MyI.mov_skip != 1 && MyI.mov_play != 1 && !MyI.pause && g_sortie.pwork->isPlayPolyg != 1)
        g_sortie.psettle->timeMission++;
    gcall(g_StageEventFunc[g_sortie.pconstant->map]);
    __event_SpriteModelProc();
    __event_MapDestroyProc();
    DOOR_DoorProc();
    ExecGimk();
    DF_LIT_Proc();
    EffectThunderLight(StgInfo.light);
    DF_ITM_Exec();
    Rope_CtrlProc();
    Cond_Proc();
    DF_EFF2_HitEffectProc();
    int dead = 1;
    for (int i = 0; i < StgInfo.cnum; i++)
        if (!(Chr[i].cflag & 1) && !(Chr[i].use & 4)) {
            dead = 0;
            break;
        }
    IsAllEnemyDead = dead;
    WETH_RainProc();
    WETH_WeatherProc();
    DF_EventSnd_Proc();
}

/* Stage setup at sortie start (order matters: hit structures before enemies and spawns, COM setup after the ACs). */
void DF_StageEventProcInit(void)
{
    PS2_FRAME(0x90);
    ZeroMemory32b(Evwk, 0x40);
    ZeroMemory32b(Evfwk, 0x40);
    ZeroMemory(s_EvEcm, 8);
    Info_InitVal();
    IsAllEnemyDead = 0;
    MyI.VSM_success[0] = 0;
    MyI.VSM_success[1] = 0;
    MyI.padoff = 0;
    s_TermPhase = 0;
    DF_RTRC_Init(StgInfo.reverb);
    DF_HIT_HitInit();
    HitSpcInit();
    DF_HIT_BuildHitLink(Map, Array->head.mnum);
    DF_HIT_BuildHitLink(Obj, Array->head.onum);
    PS2_STACK(float[4], bbmin, 0x80);
    PS2_STACK(float[4], bbmax, 0x70);
    PS2_STACK(float[4], center, 0x60);
    PS2_STACK(float[4], range, 0x50);
    HitSpcGetMapBB(bbmin, bbmax);
    for (int k = 0; k < 3; k++) range[k] = bbmax[k] - bbmin[k];
    memcpy(&range[3], &bbmax[3], 4);
    for (int k = 0; k < 3; k++) range[k] = range[k] * 0.5f;
    HitSpcGetCenter(center);
    for (int k = 0; k < 3; k++)
        if (range[k] < 160.0f) range[k] = 160.0f;
    HetInit(center, range);
    HitBoxInit();
    Rope_Init();
    ENEMY_AllEnemyInit(StgInfo.cnum, Enemy, Chr);
    EneSq_BuildSquad(Array->squad_id);
    __event_GetSpriteModel();
    __event_GetDestroyModel();
    InitAllGimk();
    DOOR_Create();
    DF_LIT_Init(&LitData);
    DF_ITM_Init();
    Cond_Init();
    WETH_RainInit();
    DF_EventSnd_Init();
    DF_OHVC_Init(&Array->ohvcs);
    DF_MPVC_Init(&Array->mpvcs, Array->marker);
    DF_ScrMessage_Init();
    InitThunder();
    PGP_EnableDeleteSFX(1);
    MResultBridge_Init();
    gcall(g_StageEventInitFunc[g_sortie.pconstant->map]);
    EventJob_SetQuickStart();
    DF_FADE_SetProc(0, 2, 0x32);
    DF_HIT_RegistHitModel();
    for (int i = 0; i < NowUserPlayerNum; i++)
        DF_PLAY_InitLocation(&g_ac[i], Array->head.sysinfo.locInit[i], Array->head.sysinfo.locInit[i][3]);
    for (int i = 0; i < NowPlayerNum; i++) DF_PLAY_DecideParent(i);
    for (int i = 0; i < StgInfo.cnum; i++) {
        StModel *mp = Enemy[i].mp;
        if (!mp->nowparent) {
            PS2_STACK(float[4], loc, 0x40);
            PS2_STACK(float[4], dest, 0x30);
            memcpy(loc, mp->loc, 16);
            loc[1] = loc[1] - 3.0f;
            DF_HIT_DecideParent(mp, loc, 100000.0f, mp->R, dest);
            if (!mp->nowparent) mp->nowparent = Map;
        }
        mp->nowparentbk = 0xff;
    }
    DF_COLL_Create();
    Info_CreateData(0, NowUserPlayerNum);
    Info_CreateData(1, NowPlayerNum);
    Info_CreateData(2, StgInfo.cnum);
    Info_CreateData(3, 1);
    EINFO_SetEnemyCountToSINFO();
    __event_ComSetup();
    EVMNG_Init();
    if (StgConf.event_onoff) DF_EVMNG_Proc();
    if (StgConf.script) {
        DF_ScriptStart(0x7fff, 0);
        DF_ScriptProc();
    }
}

void Event_SgnlCtrl(char flagSgnl)
{
    PS2_FRAME(0x10);
    if (flagSgnl == 0) DF_SGNL_SetControl(0);
    else if (flagSgnl == 1) DF_SGNL_SetControl(1);
}

/* Mission statistics for a destroyed model (by who destroyed it and its kind: 1 map, 2 object, 8 dynamic object) and
 * the destroyed-object memory of the stage. */
void Event_MdlDestroyCnt(StModel *pMdl)
{
    PS2_FRAME(0x20);
    int who = DF_DecideDestroyCountUpID(pMdl);
    int t = pMdl->type;
    if (who == 0) {   /* the player */
        if (t == 1) Info_IncData(0, 0x13, 0, pMdl->on);
        else if (t == 2) Info_IncData(0, 0x14, 0, pMdl->on);
        else if (t == 8) Info_IncData(0, 0x15, 0, pMdl->on);
    } else if (who == 1) {   /* a friend */
        if (t == 1) Info_IncData(3, 8, 0, pMdl->on);
        else if (t == 2) Info_IncData(3, 9, 0, pMdl->on);
        else if (t == 8) Info_IncData(3, 0xa, 0, pMdl->on);
    } else if (who == 2) {   /* an enemy / nobody */
        if (t == 1) Info_IncData(3, 0xd, 0, pMdl->on);
        else if (t == 2) Info_IncData(3, 0xe, 0, pMdl->on);
        else if (t == 8) Info_IncData(3, 0xf, 0, pMdl->on);
    }
    if (!pMdl->array->destMem.destMemID) return;
    int lstage = StageInfo_RealToLogicalStageNo(g_sortie.pconstant->map);
    if (!lstage) return;
    unsigned char phase = (unsigned char)__event_GetDestPhase(pMdl);
    DestObjMem_MemoryDestObj(lstage, pMdl->array->destMem.destMemID, phase);
}

/* who destroyed the model: 0 = the player (AC 0), 1 = a friendly AC / enemy character (cflag bit 0), 2 = other */
int DF_DecideDestroyCountUpID(StModel *mp)
{
    PS2_FRAME(0x10);
    if (mp->shooter == 2) {
        unsigned int id = mp->shooterid;
        if (id >= 4) return 2;
        if (id == 0) return 0;
        if (id == 0x7f) return 2;
        return g_ac[id].mp->cflag & 1 ? 1 : 2;
    }
    if (mp->shooter == 1) return DFene_GetEnemy(mp->shooterid)->mp->cflag & 1 ? 1 : 2;
    return 2;
}

#ifdef ACNB_SOURCEPORT
/* the original's initialized tables (generated by scripts/sp_datagen.py from the PS2 image) */
int (*__event_TermProcVS__EndCond[3])(void) = {   /* main 0x25bbf0 */
    __event_EndConditionsForBattleRoyal, __event_EndConditionsForTeam, __event_EndConditionsForTeamAnn
};
#endif
