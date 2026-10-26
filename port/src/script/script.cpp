/* src/script/script.cpp — stage / event script interpreter.
 *
 * A stage script is a word program (s_SCRP): op = word >> 16, n = word & 0xffff. Labels 0x7d0 | num start script num;
 * 0x7d1 ends it, 0x7d2 IF, 0x7d4 / 0x7d5 continue / yield, 0x7d6 calc, 0x7d7 relative jump, 0x7dd block; any other op
 * is a library command (kind = op) followed by n two-word value descriptors. Up to 16 scripts run at once
 * (s_ScrData); each has a 12-slot command FIFO. __SetScript decodes words until it queues one command, __CallScriptFifo
 * runs the queued commands every frame (built-ins s_ScriptFunc for kind < 100, else EVMNG_CallScript ->
 * SCRxxx_CallScript), a command finishes by clearing its fifo kind. DF_ScriptProc drives all slots once per frame.
 * The value accessors (__Get/__Set*Value), __CheckScriptIf and __CalcScriptProc are not used in Versus and stay
 * interpreted. */
#include "acnb/ps2fp.h"

/* kind: 0 map, 1 object, 2 character, 3 AC, 4 item, -1 the running script's own model. */
StModel *DF_SCR_GetModelPointer(int kind, int num)
{
    switch (kind) {
    case 0: return Map + num;
    case 1: return Obj + num;
    case 2: return Chr + num;
    case 3: return &MyAC[num];
    case 4: return Itm + num;
    case -1: return s_ScrWk->ft;
    default: return 0;
    }
}

/* Reads one two-word value descriptor: type = low byte; 0 = immediate (kind = hi16), 0x11..0x13 = raw words,
 * 1 / 2 / 3 = game / auto / static variable nn. */
static void __GetValueData(__scr_data_st *sp, unsigned int *np, __scr_rval_st *rv)
{
    PS2_FRAME(0x10);
    unsigned int w0 = np[0];
    int w1 = (int)np[1];
    unsigned int hi = (w0 & 0xffff0000u) >> 16;
    int nn = (int)((w0 & 0xffffff00u) >> 8);
    switch (w0 & 0xff) {
    case 0x13:
    case 0x12:
    case 0x11:
        rv->kind = (int)w0;
        rv->val = w1;
        break;
    case 0:
        rv->kind = (int)hi;
        rv->val = w1;
        break;
    case 3: __GetStaticValue(rv, nn, w1); break;
    case 2: __GetAutoValue(rv, sp, nn, w1); break;
    case 1: __GetGameValue(rv, nn, w1); break;
    default: MyI.KANEBIT[2] = 1; break;   /* debug error flag: bad value type */
    }
}

/* Runs the program of one slot until it queued a command that does not continue (returns 1) or it yields / ends
 * (returns 0 unless a command was queued). Quirk: op 0x7dd does not advance pc here (endless loop if reached). */
static int __SetScript(__scr_data_st *sp)
{
    PS2_FRAME(0x50);
    int go = 1, cont = 0, queued = 0;
    unsigned int *pc = sp->ScrNow;
    do {
        unsigned int op = (*pc & 0xffff0000u) >> 16;
        unsigned int n = *pc & 0xffff;
        if (op == 0x7dd) {
            /* original: pc unchanged */
        } else if (op == 0x7d6) {
            pc = __CalcScriptProc(sp, pc);
        } else if (op == 0x7d7) {
            pc += 1 + (short)n;
        } else if (op == 0x7d2) {
            int r = __CheckScriptIf(sp, pc);
            pc = sp->ScrNow;
            if (r) pc += r;
        } else if (op == 0x7d5) {
            go = 0;
            cont = 0;
            pc++;
        } else if (op == 0x7d4) {
            cont = 1;
            pc++;
        } else if (op == 0x7d1) {
            sp->on = 0;
            go = 0;
        } else if (op == 0x7d0) {
            pc++;
        } else {
            pc = __SetNormalLibFuncScript(sp, pc);
            go = cont;
            queued = 1;
        }
    } while (go);
    sp->ScrNow = pc;
    return queued;
}

/* Queues the library command at now into the first free FIFO slot (quirk: slot 0 is overwritten when all 12 are
 * busy). Commands of kind >= 100 get every argument converted to float (kind 6). Returns the next program word. */
static unsigned int *__SetNormalLibFuncScript(__scr_data_st *sp, unsigned int *now)
{
    PS2_FRAME(0x30);
    PS2_STACK(int, tmp, 0x2c);
    unsigned int kind = (now[0] & 0xffff0000u) >> 16;
    int nargs = now[0] & 0xffff;
    int i;
    for (i = 0; i < 12; i++)
        if (sp->fifo[i].kind == 0) break;
    if (i == 12) i = 0;
    __scr_fifo_st *f = &sp->fifo[i];
    unsigned int *p = now + 1;
    f->kind = (int)kind;
    for (int k = 0; k < nargs; k++, p += 2) {
        __GetValueData(sp, p, &f->arg[k]);
        if ((int)kind >= 100 && f->arg[k].kind != 6) {
            tmp = (int)ps2_f2u((float)f->arg[k].val);   /* cvt.s.w, round trip through sp+0x2c */
            f->arg[k].kind = 6;
            f->arg[k].val = tmp;
        }
    }
    for (int k = 0; k < 7; k++) f->wkL[k] = 0;
    for (int k = 0; k < 7; k++) f->wkF[k] = 0;
    return p;
}

/* Executes the queued commands; returns how many are pending (held commands, kind bit 0x8000, count but never run). */
static int __CallScriptFifo(__scr_data_st *sp)
{
    PS2_FRAME(0x50);
    PS2_STACK(int, type, 0x4c);
    PS2_STACK(int, id, 0x48);
    int cnt = 0;
    for (int i = 0; i < 12; i++) {
        __scr_fifo_st *f = &sp->fifo[i];
        int kind = f->kind;
        if (!kind) continue;
        if (kind & 0x8000) {
            cnt++;
            continue;
        }
        __GetScriptTypeID(kind, &type, &id);   /* quirk: kind >= 1200 leaves both stale */
        if (type == 0) gcall(s_ScriptFunc[id], f);
        else EVMNG_CallScript(type, id, f);
        cnt++;
    }
    return cnt;
}

/* Built-in 1: wait arg[0] frames (raw int). */
void DF_SCR_Wait(__scr_fifo_st *ss)
{
    if (--ss->arg[0].val <= 0) ss->kind = 0;
}

/* Runs every active script slot for this frame; returns s_RetValue (set by script commands). */
int DF_ScriptProc(void)
{
    PS2_FRAME(0x30);
    s_RetValue = 0;
    for (int i = 0; i < 16; i++) {
        __scr_data_st *sp = &s_ScrData[i];
        if (!sp->on) continue;
        s_ScrWk = sp;
        do {
            if (__CallScriptFifo(sp)) break;
        } while (__SetScript(sp));
    }
    return s_RetValue;
}

/* Starts script num on model scrm: -1 if it already runs, 0 if no slot is free, else slot + 1. The program is scanned
 * from the start for its label without an end bound. */
int DF_ScriptStart(unsigned int num, StModel *scrm)
{
    for (unsigned int i = 0; i < 16; i++)
        if (num == s_ScrData[i].on) return -1;
    unsigned int slot;
    __scr_data_st *sp = 0;
    for (slot = 0; slot < 16; slot++)
        if (s_ScrData[slot].on == 0) {
            sp = &s_ScrData[slot];
            break;
        }
    if (!sp) return 0;
    sp->on = (unsigned short)num;
    sp->ft = scrm;
    sp->ScrNow = (unsigned int *)s_SCRP;
    for (;;) {
        unsigned int *p = sp->ScrNow;
        unsigned int w = *p;
        unsigned int op = (w & 0xffff0000u) >> 16;
        unsigned int n = w & 0xffff;
        if (op == 0x7d0 && n == num) break;
        p++;
        switch (op) {
        case 0x7dd: p += n + 1; break;
        case 0x7d6: p += n; break;
        case 0x7d2: p += ((int)n >> 8) * 5 + 1; break;
        case 0x7d7: case 0x7d5: case 0x7d4: case 0x7d1: case 0x7d0: break;
        default: p += n * 2; break;
        }
        sp->ScrNow = p;
    }
    for (int k = 0; k < 12; k++) sp->fifo[k].kind = 0;
    return (int)slot + 1;
}

/* Resets the interpreter for a new stage program and fills the global-value pointer table read by __GetGameValue.
 * Quirks: only fifo[0] of each slot is cleared; [1..4] / [8] are first set to the model arrays and then overwritten. */
void ScriptInit(unsigned char *startp)
{
    PS2_FRAME(0x10);
    s_SCRP = startp;
    s_RetValue = 0;
    ZeroMemory32b(s_ScrStaticMem, 0x80);
    for (int i = 0; i < 16; i++) {
        __scr_data_st *sp = &s_ScrData[i];
        sp->ScrNow = (unsigned int *)s_SCRP;
        unsigned int *w = (unsigned int *)&sp->fifo[0];
        for (int k = 0; k < 0x30; k++) w[k] = 0;
        sp->on = 0;
    }
    g_NowScrCtrlACID = 0;
    s_SCR_GValue[1] = Map;
    s_SCR_GValue[2] = Obj;
    s_SCR_GValue[3] = Chr;
    s_SCR_GValue[4] = Itm;
    s_SCR_GValue[8] = Enemy;
    DF_ADSVC_Init();
    s_SCR_GValue[5] = 0;
    s_SCR_GValue[0] = CAM;
    s_SCR_GValue[1] = s_ScrStaticMem;
    s_SCR_GValue[10] = g_ac;
    s_SCR_GValue[11] = MapAmapString;
    s_SCR_GValue[12] = &NowAmapString;
    s_SCR_GValue[14] = &IsAllEnemyDead;
    s_SCR_GValue[15] = MyI.VSM_success;
    s_SCR_GValue[17] = &g_NowScrCtrlACID;
    s_SCR_GValue[18] = MapString;
    s_SCR_GValue[19] = &NowMapString;
    s_SCR_GValue[20] = FriendID;
    s_SCR_GValue[21] = CopyOfSysInfo.rgnalert;
    s_SCR_GValue[22] = CopyOfSysInfo.rgnover;
    s_SCR_GValue[23] = &g_gamedata[0].rv_rank;
    s_SCR_GValue[24] = &MyI.deadstop;
    s_SCR_GValue[25] = &StgInfo;
    s_SCR_GValue[26] = &StgConf;
    s_SCR_GValue[27] = AcSubInfo;
    s_SCR_GValue[30] = Evwk;
    s_SCR_GValue[31] = Evfwk;
    s_SCR_GValue[32] = EvStageWk;
    s_SCR_GValue[2] = s_ScrStaticMem;
    s_SCR_GValue[3] = s_ScrStaticMem;
    s_SCR_GValue[4] = s_ScrStaticMem;
    s_SCR_GValue[8] = s_ScrStaticMem;
    s_SCR_GValue[6] = 0;
    s_SCR_GValue[7] = 0;
    s_SCR_GValue[9] = 0;
    StSettlement *settle = g_sortie.psettle;
    s_SCR_GValue[13] = &g_sortie.pwork->statMission;
    s_SCR_GValue[28] = &settle->timeActest;
    s_SCR_GValue[16] = settle;
    s_SCR_GValue[29] = &settle->desttimeActest;
}

/* command kind -> (group, id): kind < 100 = built-in (group 0), else kind / 100, kind % 100 for kinds below 1200.
 * Quirk (kept): kind >= 1200 writes neither output (the caller's slots stay stale). */
static void __GetScriptTypeID(int kind, int *pType, int *pID)
{
    if (kind < 100) {
        *pType = 0;
        *pID = kind;
        return;
    }
    int t = 1;
    do {
        kind -= 100;
        if (kind < 100) {
            *pType = t;
            *pID = kind;
            return;
        }
        t++;
    } while (t < 12);
}
