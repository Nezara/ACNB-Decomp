/* src/main/thretask.cpp - a tiny worker-thread pool (main ELF, at most 2 EE kernel threads). ThreTask_Init clears
 * the command table and starts the workers; each worker (__TaskThread) waits on its semaphore, looks its slot's
 * command id up in s_cmdTable and calls func(param, arg). ThreTask_RegisterCommand / CallCommand / IsBusy (not
 * reached in Versus: no command is ever registered, so the workers block in their first WaitSema for good) stay
 * interpreted.
 *
 * Hybrid: __TaskThread is a native kernel-thread entry. runtime/kernel.c runs every PS2 thread on its own host fiber
 * with its own EECore (ee_cur), StartThread sets the core's pc to the entry and the bridge enters the native there;
 * WaitSema blocks inside the bridge_call on that fiber. Original quirks kept: the CreateSema / CreateThread parameter
 * blocks are stack structs with fields the original never writes (sema currentCount / numWaitThreads / attr, thread
 * status / currentPriority / attr / option ...): PS2_STACK at the original offsets so the kernel reads the same stale
 * words. */
#include "acnb/sdk.h"

#define THRETASK_MAX_THREAD 2
#define THRETASK_MAX_CMD 32
#define THRETASK_STACK_SIZE 0x1e00
#define THRETASK_PRIORITY 0x36

int ThreTask_Init(int numThread)
{
    PS2_FRAME(0x30);
    ZeroMemory8b(s_cmdTable, sizeof s_cmdTable);
    ZeroMemory8b(s_ttArray, sizeof s_ttArray);
    if (numThread >= THRETASK_MAX_THREAD + 1) numThread = THRETASK_MAX_THREAD;
    for (int i = 0; i < numThread; i++) __CreateTaskThread(i);
    return 1;
}

void __TaskThread(void *pArg)
{
    PS2_FRAME(0x20);
    struct StTaskThread *tt = (struct StTaskThread *)pArg;
    for (;;) {
        ps2_WaitSema(tt->idSema);
        tt->isBusy = 1;
        int cmd = tt->cmd;
        if (cmd != 0 && cmd >= 0) {
            int i;
            for (i = 0; i < THRETASK_MAX_CMD; i++)
                if (cmd == s_cmdTable[i].cmd) break;
            if (i >= THRETASK_MAX_CMD) i = -1;
            if (i != -1) gcall(s_cmdTable[i].pFunc, s_cmdTable[i].pParam, tt->paramLast);
        }
        tt->cmd = 0;
        tt->isBusy = 0;
    }
}

static int __CreateTaskThread(int idxThread)
{
    PS2_FRAME(0x80);
    PS2_STACK(struct SemaParam, sema, 0x30);
    PS2_STACK(struct ThreadParam, thread, 0x50);
    sema.initCount = 2;
    sema.maxCount = 2;
    sema.option = 0;
    int sid = ps2_CreateSema(&sema);
    if (sid < 0) return 0;
    s_ttArray[idxThread].idSema = sid;
    thread.entry = GFN(__TaskThread);
    thread.gpReg = &_gp;
    thread.stackSize = THRETASK_STACK_SIZE;
    thread.initPriority = THRETASK_PRIORITY;
    thread.stack = s_stackTaskThread[idxThread];
    int tid = ps2_CreateThread(&thread);
    if (tid < 0) return 0;
    s_ttArray[idxThread].idThread = tid;
    ps2_StartThread(tid, &s_ttArray[idxThread]);
    return 1;
}
