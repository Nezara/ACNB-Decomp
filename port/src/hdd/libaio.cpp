/* src/hdd/libaio.cpp - EE-side asynchronous file I/O. AioInit creates two semaphores (ssid = "start", esid = "end"), a
 * kernel thread running threadmain on the 8 KB AioInit::stack and clears the shared command block aio. A caller posts
 * one command with an AioXxx function: test-and-set of aio.ccmd with interrupts disabled (a busy block prints
 * "overlapped operation" and returns -ccmd), copy the arguments into aio, SignalSema(ssid). The thread wakes, runs
 * the matching libkernl fileio call (or sceCdSearchFile), stores the result, moves ccmd to lcmd and signals esid;
 * AioCmdSync waits (mode 0) or polls (mode 1) for that.
 * In this game only AioInit runs (File_Init at boot); the thread then blocks in its first WaitSema for good. The
 * posters, AioCmdSync and the thread's switch are unreached (cdvd's async arms, hdd.c's tasks: no HDD).
 *
 * Hybrid: threadmain is a native kernel-thread entry (runtime/kernel.c host fiber per PS2 thread, as main/thretask.cpp
 * __TaskThread); the CreateSema / CreateThread parameter blocks are PS2_STACK at the original offsets, so the kernel
 * reads the same never-written words (sema currentCount / numWaitThreads / attr, thread status / currentPriority /
 * attr / option / wait fields). Pointer arguments are kept in aio.a[] as 32-bit words, like the original. */
#include "acnb/port.h"
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
struct struct_9d7548ea aio;
int ssid;
int esid;
int tid;
unsigned char AioInit__stack[8192];
char aio_msg_unknown[29] = "aio: unknown i/o operation.\n";
char aio_msg_overlap[40] = "aio: overlapped operation %d, while %d\n";
#endif

/* argument word i as a pointer (the posters store pointers as 32-bit ints) */
#define AIO_P(i) ((void *)(uintptr_t)(unsigned int)aio_p->a[i])
#define AIO_WORD(p) ((int)PS2PTR(p))

/* claims the command block for cmd; < 0 (= -busy command) if another command is still in flight */
static inline int aio_claim(int cmd)
{
    int r = 0;
    DIntr();
    if (aio.ccmd != AIOC_NONE) {
        int busy = (unsigned char)aio.ccmd;
        r = -busy;
        ps2_scePrintf_dd(aio_msg_overlap, cmd, busy);
    } else {
        aio.ccmd = (enum_9011f914)cmd;
    }
    EIntr();
    return r;
}

int AioInit(int priority)
{
    PS2_FRAME(0xa0);
    PS2_STACK(struct SemaParam, sema, 0x80);
    PS2_STACK(struct ThreadParam, thread, 0x50);
    sema.initCount = 0;
    sema.maxCount = 1;
    sema.option = 0;
    ssid = ps2_CreateSema(&sema);
    sema.initCount = 0;
    sema.maxCount = 1;
    sema.option = 0;
    esid = ps2_CreateSema(&sema);
    thread.entry = GFN(threadmain);
    thread.initPriority = priority;
    thread.stack = AioInit__stack;
    thread.stackSize = 0x2000;
    thread.gpReg = &_gp;
    tid = ps2_CreateThread(&thread);
    ps2_memset(&aio, 0, 0x1230);   /* after CreateThread, before StartThread */
    ps2_StartThread(tid, &aio);
    return 0;
}

static void threadmain(void *param)
{
    PS2_FRAME(0x30);
    struct struct_9d7548ea *aio_p = (struct struct_9d7548ea *)param;
    char *name0 = aio_p->name0;
    char *name1 = aio_p->name1;
    int *a = aio_p->a;
    for (;;) {
        if (ps2_WaitSema(ssid) < 0) ps2_ExitDeleteThread();
        int r;
        switch ((unsigned char)aio_p->ccmd) {
        case AIOC_NONE: r = 0; break;
        case AIOC_OPEN: r = sceOpen(name0, a[0]); break;   /* a[1] (mode) is loaded into a2 too; sceOpen takes 2 */
        case AIOC_CLOSE: r = sceClose(a[0]); break;
        case AIOC_READ: r = sceRead(a[0], AIO_P(1), a[2]); break;
        case AIOC_WRITE: r = sceWrite(a[0], AIO_P(1), a[2]); break;
        case AIOC_LSEEK: r = sceLseek(a[0], a[1], a[2]); break;
        case AIOC_LSEEK64: r = (int)sceLseek64(a[0], aio_p->al, a[1]); break;
        case AIOC_CHSTAT: r = sceChstat(name0, AIO_P(0), (unsigned int)a[1]); break;
        case AIOC_GETSTAT: r = sceGetstat(name0, AIO_P(0)); break;
        case AIOC_RENAME: r = sceRename(name0, name1); break;
        case AIOC_REMOVE: r = sceRemove(name0); break;
        case AIOC_READLINK: r = sceReadlink(name0, (char *)AIO_P(0), (unsigned int)a[1]); break;
        case AIOC_SYMLINK: r = sceSymlink(name0, name1); break;
        case AIOC_CHDIR: r = sceChdir(name0); break;
        case AIOC_MKDIR: r = sceMkdir(name0, a[0]); break;
        case AIOC_RMDIR: r = sceRmdir(name0); break;
        case AIOC_DOPEN: r = sceDopen(name0); break;
        case AIOC_DCLOSE: r = sceDclose(a[0]); break;
        case AIOC_DREAD: r = sceDread(a[0], AIO_P(1)); break;
        case AIOC_SYNC: r = sceSync(name0, a[0]); break;
        case AIOC_FORMAT: r = sceFormat(name0, (const char *)AIO_P(0), AIO_P(1), a[2]); break;
        case AIOC_MOUNT: r = sceMount(name0, (const char *)AIO_P(0), a[1], AIO_P(2), a[3]); break;
        case AIOC_UMOUNT: r = sceUmount(name0); break;
        case AIOC_DEVCTL:
            r = sceDevctl(name0, a[0], AIO_P(1), (unsigned int)a[2], AIO_P(3), (unsigned int)a[4]);
            break;
        case AIOC_IOCTL2:
            r = sceIoctl2(a[0], a[1], AIO_P(2), (unsigned int)a[3], AIO_P(4), (unsigned int)a[5]);
            break;
        case AIOC_CD_SEARCHFILE: r = sceCdSearchFile(AIO_P(0), name0); break;
        default:
            ps2_scePrintf(aio_msg_unknown);
            r = -1;
            break;
        }
        aio_p->result = r;
        aio_p->lcmd = aio_p->ccmd;
        aio_p->ccmd = AIOC_NONE;
        ps2_SignalSema(esid);
    }
}

int AioCdSearchFile(char *filename, struct struct_9b06e75c *buf)
{
    PS2_FRAME(0x40);
    int r = aio_claim(AIOC_CD_SEARCHFILE);
    if (r < 0) return r;
    ps2_strcpy(aio.name0, filename);
    aio.a[0] = AIO_WORD(buf);
    ps2_SignalSema(ssid);
    return 0;
}

int AioDevctl(char *devname, int request, void *wbuf, int wlen, void *rbuf, int rlen)
{
    PS2_FRAME(0x80);
    int r = aio_claim(AIOC_DEVCTL);
    if (r < 0) return r;
    ps2_strcpy(aio.name0, devname);
    aio.a[0] = request;
    if (wbuf) {
        ps2_memcpy(aio.buf, wbuf, (unsigned int)wlen);
        aio.a[1] = AIO_WORD(aio.buf);
    } else {
        aio.a[1] = 0;
    }
    aio.a[2] = wlen;
    aio.a[3] = AIO_WORD(rbuf);
    aio.a[4] = rlen;
    ps2_SignalSema(ssid);
    return 0;
}

int AioFormat(char *fsname, char *devname, void *arg, int len)
{
    PS2_FRAME(0x60);
    int r = aio_claim(AIOC_FORMAT);
    if (r < 0) return r;
    ps2_strcpy(aio.name0, fsname);
    if (devname) {
        ps2_strcpy(aio.name1, devname);
        aio.a[0] = AIO_WORD(aio.name1);
    } else {
        aio.a[0] = 0;
    }
    if (arg) {
        ps2_memcpy(aio.buf, arg, (unsigned int)len);
        aio.a[1] = AIO_WORD(aio.buf);
    } else {
        aio.a[1] = 0;
    }
    aio.a[2] = len;
    ps2_SignalSema(ssid);
    return 0;
}

int AioGetstat(char *filename, struct sce_stat *buf)
{
    PS2_FRAME(0x40);
    int r = aio_claim(AIOC_GETSTAT);
    if (r < 0) return r;
    ps2_strcpy(aio.name0, filename);
    aio.a[0] = AIO_WORD(buf);
    ps2_SignalSema(ssid);
    return 0;
}

int AioWrite(int fd, void *buf, int len)
{
    PS2_FRAME(0x50);
    int r = aio_claim(AIOC_WRITE);
    if (r < 0) return r;
    aio.a[0] = fd;
    aio.a[1] = AIO_WORD(buf);
    aio.a[2] = len;
    ps2_SignalSema(ssid);
    return 0;
}

int AioRead(int fd, void *buf, int len)
{
    PS2_FRAME(0x50);
    int r = aio_claim(AIOC_READ);
    if (r < 0) return r;
    aio.a[0] = fd;
    aio.a[1] = AIO_WORD(buf);
    aio.a[2] = len;
    ps2_SignalSema(ssid);
    return 0;
}

int AioClose(int fd)
{
    PS2_FRAME(0x30);
    int r = aio_claim(AIOC_CLOSE);
    if (r < 0) return r;
    aio.a[0] = fd;
    ps2_SignalSema(ssid);
    return 0;
}

int AioOpen(char *filename, int flag, unsigned int mode)
{
    PS2_FRAME(0x50);
    int r = aio_claim(AIOC_OPEN);
    if (r < 0) return r;
    ps2_strcpy(aio.name0, filename);
    aio.a[0] = flag;
    aio.a[1] = (int)mode;
    ps2_SignalSema(ssid);
    return 0;
}

/* mode 0: wait for the command to finish; mode 1: poll (0 = still running). *cmd = the running command, else the last
 * one; *result = its result. Other modes: -1 */
int AioCmdSync(int mode, int *cmd, int *result)
{
    PS2_FRAME(0x30);
    if (mode == 1) {
        int r = ps2_PollSema(esid);
        if (cmd) {
            if (aio.ccmd != AIOC_NONE) *cmd = (unsigned char)aio.ccmd;
            else *cmd = (unsigned char)aio.lcmd;
        }
        if (result) *result = aio.result;
        return r == -1 ? 0 : 1;
    }
    if (mode != 0) return -1;
    ps2_WaitSema(esid);
    if (cmd) *cmd = (unsigned char)aio.lcmd;
    if (result) *result = aio.result;
    return 1;
}
