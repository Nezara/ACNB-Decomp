/* src/event/emsg.cpp - stage script messages (radio text + voice). ScrMsg holds the running message and a queue of
 * 7 waiting ones (8-byte slots, one 0x40 buffer). A new message shows at once when the message window is idle; a
 * negative priority shows at once and flushes the queue; otherwise it pre-empts a running one of lower (positive)
 * priority or is queued by priority (if `wait`). DF_ScrMessage_Proc moves to the next queued message when the
 * window is idle. Nothing happens once the mission status is set (statMission). snd_id 0 = the message's own voice. */
#include "acnb/port.h"

/* slot fields one by one (the original's byte / half stores) */
static void emsg_clear(struct_b7d52db4 *m)
{
    m->on = 0;
    m->use = 0;
    m->mode = 0;
    m->prio = 0;
    m->snd_id = 0;
    m->msg_id = 0;
}

static void emsg_clear_wait(void)
{
    for (int i = 0; i < 7; i++) emsg_clear(ScrMsg->waitmsg[i]);
}

static void emsg_show_run(void)
{
    struct_b7d52db4 *m = ScrMsg->runmsg;
    int snd = m->snd_id, msg = m->msg_id;
    if (!snd) snd = Array->message.sndid[msg];
    MsgWnd_ShowArb(Array->message.onemes[msg].str, snd);
}

/* make the message the running one and show it */
static void emsg_start(int snd_id, int msg_id, int prio, int mode)
{
    struct_b7d52db4 *m = ScrMsg->runmsg;
    m->on = 1;
    m->use = 1;
    m->prio = (char)prio;
    m->mode = (char)mode;
    m->snd_id = (short)snd_id;
    m->msg_id = (short)msg_id;
    emsg_show_run();
}

/* insert before the first waiting slot of lower priority; the last slot is recycled (its message is dropped) */
static void __SetMsgWait(int snd_id, int msg_id, int prio, int mode)
{
    struct_12fb2713 *s = ScrMsg;
    for (int i = 0; i < 7; i++) {
        if (s->waitmsg[i]->prio < prio) {
            struct_b7d52db4 *m = s->waitmsg[6];
            for (int j = 5; j >= i; j--) s->waitmsg[j + 1] = s->waitmsg[j];
            s->waitmsg[i] = m;
            m->on = 0;
            m->use = 1;
            m->prio = (char)prio;
            m->mode = (char)mode;
            m->snd_id = (short)snd_id;
            m->msg_id = (short)msg_id;
            return;
        }
    }
}

/* TargetMsg: 0 = window + queue, 1 = window only, 2 = queue only */
void DF_ScrMessage_CancelMessage(int TargetMsg)
{
    PS2_FRAME(0x10);
    if (TargetMsg == 2) {
        emsg_clear_wait();
    } else if (TargetMsg == 1) {
        MsgWnd_CancelVoice();
        MsgWnd_Clear();
    } else if (TargetMsg == 0) {
        MsgWnd_CancelVoice();
        MsgWnd_Clear();
        emsg_clear_wait();
    }
}

void DF_SetScrMessage(int snd_id, int msg_id, int prio, int mode, int wait)
{
    PS2_FRAME(0x70);
    struct_12fb2713 *s = ScrMsg;
    if (g_sortie.pwork->statMission) return;
    if (!MsgWnd_IsBusy(1)) {
        emsg_start(snd_id, msg_id, prio, mode);
        if (prio < 0) emsg_clear_wait();
        return;
    }
    struct_b7d52db4 *rm = s->runmsg;
    if (!rm->on) {
        if (mode == 0) {
            if (wait) __SetMsgWait(snd_id, msg_id, prio, 0);
        } else {
            emsg_start(snd_id, msg_id, prio, mode);
            if (prio < 0) emsg_clear_wait();
        }
    } else if (prio < 0) {
        emsg_start(snd_id, msg_id, prio, mode);
        emsg_clear_wait();
    } else if (rm->prio < prio && rm->prio > 0) {
        emsg_start(snd_id, msg_id, prio, mode);
    } else if (wait) {
        __SetMsgWait(snd_id, msg_id, prio, mode);
    }
}

void DF_ScrMessage_Proc(void)
{
    PS2_FRAME(0x30);
    struct_12fb2713 *s = ScrMsg;
    if (g_sortie.pwork->statMission) return;
    if (MsgWnd_IsBusy(1)) return;
    struct_b7d52db4 *rm = s->runmsg;
    if (!rm->on) {   /* nothing shown yet: start the first queued one */
        if (rm->use) return;
        if (!s->waitmsg[0]->use) return;
    }
    s->runmsg = s->waitmsg[0];
    for (int i = 0; i < 6; i++) s->waitmsg[i] = s->waitmsg[i + 1];
    s->waitmsg[6] = rm;
    memset(s->waitmsg[6], 0, 8);
    if (s->runmsg->use) {
        s->runmsg->on = 1;
        emsg_show_run();
    }
}

void DF_ScrMessage_Init(void)
{
    PS2_FRAME(0x20);
    ScrMsg = (struct_12fb2713 *)Memm_Alloc(1, 0x20, 0x10);
    struct_b7d52db4 *buf = (struct_b7d52db4 *)Memm_Alloc(1, 0x40, 0x10);
    memset(ScrMsg, 0, 0x20);
    memset(buf, 0, 0x40);
    ScrMsg->runmsg = buf;
    for (int i = 0; i < 7; i++) ScrMsg->waitmsg[i] = buf + 1 + i;
}
