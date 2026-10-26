/* lib/fs/fslibmc.cpp - game-side wrapper over libmc (lib/fs/fslibmc.c; boot subset B5, S1 load path,
 * S2 save path). A public fsmc_* call queues a command in __MCMNG (funcno = command, transno = state 0); the caller
 * then polls fsmc_FuncCheck once per tick, which runs that command's static state machine (*Func: issue the sceMc call,
 * poll sceMcSync(1, ..), retry up to count 2, map the result into messno for fsmc_MessageCheck). Native here: the
 * title check's card info / directory listing (GetFileInfo / GetDir + their machines + _fsmc_InfoErrMess), the
 * accessors, fsmc_Init / SetWorkBuffer / GetInfoCode / GetCardInfo. S1 (load path): FileChk / DataRead + their
 * machines + _fsmc_GetInfo / _fsmc_OpenErrMess / _fsmc_GetReadData. S2 (save path + card admin): DataWrite / MkDir /
 * Format + their machines, _fsmc_SetWriteData, and the dead (never queued) ChDirFunc / UnFormatFunc. FuncCheck keeps
 * all 12 dispatch arms. S3 (card icon): IconWrite + IconWriteFunc (live: ICO + icon.sys in Ac2_Mcf_SaveProc), the
 * dead (never queued) IconWriteFileFunc / IconReadFunc: every fsmc_* is native now.
 * Card file format (written by DataWriteFunc / _fsmc_SetWriteData, read by DataReadFunc): [u32 datasize][block 0:
 * min(datasize, 0x3fb) bytes + checksum][blocks of up to 0x3ff bytes + checksum]..[marker 1], each block padded to
 * 32; checksum = byte sum of the complemented bytes.
 * Original quirks kept: FuncCheck reads funcno after the machine returned (an error arm that already cleared it gives
 * -0x64, not -funcno); GetFileInfoFunc's state 0 falls into state 1, GetDirFunc's does not; the queue functions accept
 * ports 0..2 / slots 0..4 although the tables are [2][4] (port 2 reads the next table); fsmc_Init clears only funcno /
 * transno / result[][]; GetFreeMemory keeps a 2-cluster reserve; GetCardInfo returns -result unless messno is 1.
 * S1 quirks (keep): DataReadFunc's retry budget carries over (count is cleared only in some arms, so a refused issue
 * leaves count at 1 into the next Sync state); state 4 reads the 4-byte header with a length that is the dispatch
 * chain's stale a2 = 4; state 0xd's Sync retry does transno++ (back to the read issue 0xe) where every other retry does
 * transno--; state 0xd falls into 0xe on "more blocks" (the next read is issued in the same tick); the marker probe
 * (state 0xa) reads rddata without clearing it; errorno is _fsmc_GetInfo's sub-state and, in error arms, a code
 * nothing reads (-0x18f / res); return paths differ in whether funcno is cleared first (FuncCheck -0x64 vs -6);
 * state 3's / 0x11's final failure returns the Sync result itself. HANG: a payload of 0x3fb + k * 0x3ff bytes ends
 * on a full block, GetReadData returns 0 (not 1) and every later block has remain 0 -> 0xd / 0xe loop for ever
 * (FuncCheck keeps returning 6; not met by the stock saves). _fsmc_GetReadData: n == 0 still advances pbuff by 1;
 * the copy length is dataui - rdofs when dataui < datasize (unclamped). FileChkFunc / GetInfo poll states return
 * the negative Sync result as is.
 * S2 quirks (keep): DataWriteFunc state 0x17 stores funcno = 0 before testing the close result, so a failed close is
 * abandoned (FuncCheck idles, messno stays 0x3c, the file stays open); only the third failure returns -8. fd starts at
 * 0 (not -1), so state 0x64's fd < 0 arm is dead. State 5 jumps to 9 (6..8 do not exist); 0x12 jumps to 0x14; the
 * 0x13 retry re-issues the write (0x15) without rebuilding the buffer (wtofs already advanced); 9 -> 0xa, 0xe -> 0xf,
 * 0x13 -> 0x14 -> 0x15 run in the same tick; count is cleared only in some arms (budget carries over). Written byte
 * counts are not checked (s_WriteNum += res). An existing file is opened mode 3 (no truncate: a shorter save keeps
 * the old tail). Error returns vary (-1 / -4 / -5 / -6 / -8 / -10 / -999 / the Sync result). _fsmc_SetWriteData builds
 * s_WkKByte - 1 blocks per call (the memset clears the whole buffer); a payload ending on a full block gets an extra
 * marker-only block. The admin machines (MkDir / Format / UnFormat / ChDir) retry any nonzero Sync result (either
 * sign) and return it as is on the third failure; a refused issue keeps funcno (FuncCheck -funcno).
 * S3 quirks (keep): IconWriteFunc is not a DataWriteFunc copy: state 0 does not clear count (the budget carries over
 * from the previous command), state 9 neither; the free-space check is unsigned (sltu) with messno 0x3d / -0x65
 * (IconWriteFileFunc's is signed); a failed open's third Sync failure keeps funcno and returns 0 into the cleanup;
 * write / close failures set errorno -1 and the cleanup state with no messno (stays 0x3c until 0x66 sets 0x3d);
 * state 0xe clears funcno before the result test (a failed close is abandoned, as DataWriteFunc 0x17); the whole
 * file goes out in one sceMcWrite (wtofs advances by the returned count, a short write loops 0xa..0xc); the cleanup
 * 0x64 has no Sync. IconWriteFileFunc (dead): state 0x64 never advances transno (re-closes the host file every tick
 * for ever, 0x65 / 0x66 unreachable); the -0x65 space exit leaves the host file open; wtsize counts the requested
 * chunk size, not the written count; hostName[64] is an unbounded strcpy of name2; state 0xf retries (funcno still
 * set) where IconWriteFunc 0xe abandons. IconReadFunc (dead): state 0 falls into 1 (no branch at 0x13faec); a non-PS2
 * card goes to the cleanup with messno 0; 0x65's third refused close clears funcno, sets 0x47 and falls into count++
 * (count 3, returns 0); a read returning 0 before dataui loops 4..6 for ever; state 9 clears funcno before the test.
 * Hybrid: the sceMcSync result word is a PS2 stack slot (sp+0x3c / 0x1c / 0x2c / 0x6c; IconWriteFileFunc's hostName
 * at sp+0x20 too), the SDK calls go to the HLE-hooked
 * originals (runtime/mc.c). */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include <string.h>

/* type / free / format / result [port][slot] with the original's flat index (port * 16 + slot * 4 bytes): port 2
 * reaches into the next table like the asm does */
#define MC_TYPE(p, s)   (*(int *)((char *)&__MCMNG + 0x00 + (p) * 16 + (s) * 4))
#define MC_FREE(p, s)   (*(int *)((char *)&__MCMNG + 0x20 + (p) * 16 + (s) * 4))
#define MC_FORMAT(p, s) (*(int *)((char *)&__MCMNG + 0x40 + (p) * 16 + (s) * 4))
#define MC_RESULT(p, s) (*(int *)((char *)&__MCMNG + 0x60 + (p) * 16 + (s) * 4))

/* 0x13e8c0 */
int fsmc_SetWorkBuffer(void *pbuffer, unsigned int kbyte)
{
    if (__MCMNG.funcno != 0) return 1;
    if (pbuffer != 0 && kbyte < 2) return -1;
    if (pbuffer == 0) {
        s_pWkBuff = 0;
        s_WkKByte = 0;
    } else {
        s_pWkBuff = (unsigned char *)pbuffer;
        s_WkKByte = kbyte;
    }
    return 0;
}

#ifndef ACNB_SOURCEPORT
/* MIPS callers: the buffer / name / table pointers are stored into guest state, so they go through as the raw
 * register (bridge_ptr would fold an uncached mirror) */
static void bw__fsmc_SetWorkBuffer(EECore *c)
{
    c->gpr[V0].sd[0] = (s32)fsmc_SetWorkBuffer((void *)(uintptr_t)c->gpr[A0].uw[0], c->gpr[A1].uw[0]);
}
#endif

/* 0x13e920 */
int fsmc_MessageCheck(void) { return __MCMNG.messno; }

/* 0x13e930 */
int fsmc_GetDirPoints(void) { return __MCMNG.getdir; }

/* 0x13e940 (no bounds check) */
int fsmc_GetMemoryCardType(int port, int slot) { return MC_TYPE(port, slot); }

/* 0x13e960: free clusters minus a 2-cluster reserve (0 when <= 2); no card -> messno 1, -1 */
int fsmc_GetFreeMemory(int port, int slot)
{
    int f = MC_FREE(port, slot);
    if (f == 0 && MC_TYPE(port, slot) == 0) {
        __MCMNG.messno = 1;
        return -1;
    }
    int v = f - 2;
    if (v <= 0) {
        __MCMNG.messno = 0x1e;   /* dead: overwritten below */
        v = 0;
    }
    __MCMNG.messno = 0;
    return v;
}

/* 0x13e9e0 */
int fsmc_GetInfoCode(int port, int slot) { return MC_RESULT(port, slot); }

/* 0x13ea00: called by every Ac2_Mcf_Init (several times per boot); type / free / format / messno / count persist */
int fsmc_Init(void)
{
    PS2_FRAME(0x10);
    int r = sceMcInit();
    if (r == 0) {
        __MCMNG.funcno = 0;
        __MCMNG.transno = 0;
        __MCMNG.result[0][0] = 0;
        __MCMNG.result[1][0] = 0;
        __MCMNG.result[0][1] = 0;
        __MCMNG.result[1][1] = 0;
        __MCMNG.result[0][2] = 0;
        __MCMNG.result[1][2] = 0;
        __MCMNG.result[0][3] = 0;
        __MCMNG.result[1][3] = 0;
    }
    return r;
}

/* 0x13ea80: card state from the last GetInfo (no sceMc call) -> messno; returns -result unless messno ends up 1 */
int fsmc_GetCardInfo(int port, int slot)
{
    int type = MC_TYPE(port, slot);
    if (type == 1) {
        __MCMNG.messno = 1;
        return -1;
    }
    int r = MC_RESULT(port, slot);
    if (r < -2) {
        __MCMNG.messno = 1;
        return -2;
    }
    if (type != 2 && type != 3) {
        __MCMNG.messno = 1;
        return -5;
    }
    int nr = -r;
    if (type == 3) {   /* PS1 card */
        switch (nr) {
        case 5: __MCMNG.messno = 0x15; break;
        case 4: __MCMNG.messno = 0x16; break;
        case 3: __MCMNG.messno = 0x14; break;
        default: __MCMNG.messno = 1; break;
        }
    } else {
        switch (nr) {
        case 2: __MCMNG.messno = 3; break;
        case 1: __MCMNG.messno = 2; break;
        case 0: __MCMNG.messno = 0; break;
        default: __MCMNG.messno = 1; break;
        }
    }
    if (__MCMNG.messno == 1) return -6;
    return nr;
}

/* 0x13ec00: the per-tick poll. 0 = idle / just finished, > 0 = busy (funcno), < 0 = failed (-funcno, or -0x64 when
 * the machine already cleared funcno) */
int fsmc_FuncCheck(void)
{
    PS2_FRAME(0x10);
    int v;
    switch ((unsigned int)__MCMNG.funcno) {   /* jump table 0x271600 (15 arms) */
    case 0: return 0;
    case 3: v = fsmc_GetFileInfoFunc(); break;
    case 4: v = fsmc_GetDirFunc(); break;
    case 5: v = fsmc_DataWriteFunc(); break;
    case 6: v = fsmc_DataReadFunc(); break;
    case 7: v = fsmc_IconWriteFunc(); break;
    case 8: v = fsmc_IconWriteFileFunc(); break;
    case 9: v = fsmc_FormatFunc(); break;
    case 10: v = fsmc_ChDirFunc(); break;
    case 11: v = fsmc_MkDirFunc(); break;
    case 12: v = fsmc_IconReadFunc(); break;
    case 13: v = fsmc_FileChkFunc(); break;
    case 14: v = fsmc_UnFormatFunc(); break;
    default: v = -1; break;   /* 1, 2, >= 15 */
    }
    if (v < 0) {
        __MCMNG.transno = 0;
        int t = -__MCMNG.funcno;   /* read after the machine ran */
        __MCMNG.funcno = 0;
        return t != 0 ? t : -0x64;
    }
    if (v > 0) {
        __MCMNG.funcno = 0;
        __MCMNG.transno = 0;
        return 0;
    }
    return __MCMNG.funcno;
}

/* 0x13ed70: queue a file existence check (funcno 13); messno / errorno / count untouched */
int fsmc_FileChk(int port, int slot, char *filename)
{
    if (__MCMNG.funcno != 0) return 1;
    if (port >= 3 || port < 0) return -1;
    if (slot >= 5 || slot < 0) return -1;
    __MCMNG.slotno = slot;
    __MCMNG.portno = port;
    __MCMNG.funcno = 13;
    __MCMNG.name = filename;
    __MCMNG.transno = 0;
    return 0;
}

#ifndef ACNB_SOURCEPORT
static void bw__fsmc_FileChk(EECore *c)
{
    c->gpr[V0].sd[0] = (s32)fsmc_FileChk(c->gpr[A0].sw[0], c->gpr[A1].sw[0], (char *)(uintptr_t)c->gpr[A2].uw[0]);
}
#endif

/* 0x13ee00: queue a directory create (funcno 11); messno / count / errorno untouched (messno keeps the previous
 * command's value until the machine ends) */
int fsmc_MkDir(int port, int slot, char *dirname)
{
    if (__MCMNG.funcno != 0) return 1;
    if (port >= 3 || port < 0) return -1;
    if (slot >= 5 || slot < 0) return -1;
    __MCMNG.slotno = slot;
    __MCMNG.portno = port;
    __MCMNG.funcno = 11;
    __MCMNG.name = dirname;
    __MCMNG.transno = 0;
    return 0;
}

#ifndef ACNB_SOURCEPORT
static void bw__fsmc_MkDir(EECore *c)
{
    c->gpr[V0].sd[0] = (s32)fsmc_MkDir(c->gpr[A0].sw[0], c->gpr[A1].sw[0], (char *)(uintptr_t)c->gpr[A2].uw[0]);
}
#endif

/* 0x13ee90: queue a card format (funcno 9); messno 0x33 = queued; count / errorno untouched */
int fsmc_Format(int port, int slot)
{
    if (__MCMNG.funcno != 0) return 1;
    if (port >= 3 || port < 0) return -1;
    if (slot >= 5 || slot < 0) return -1;
    __MCMNG.portno = port;
    __MCMNG.slotno = slot;
    __MCMNG.funcno = 9;
    __MCMNG.messno = 0x33;
    __MCMNG.transno = 0;
    return 0;
}

/* 0x13ef30: queue an icon file write (funcno 7) of size bytes from dthead (ICO / icon.sys); messno 0x3c = queued
 * (count not cleared: IconWriteFunc state 0 does not clear it either) */
int fsmc_IconWrite(int port, int slot, char *fname, void *dthead, unsigned int size)
{
    if (__MCMNG.funcno != 0) return 1;
    if (port >= 3 || port < 0) return -1;
    if (slot >= 5 || slot < 0) return -1;
    __MCMNG.portno = port;
    __MCMNG.slotno = slot;
    __MCMNG.funcno = 7;
    __MCMNG.name = fname;
    __MCMNG.datahdc = dthead;
    __MCMNG.dataui = size;
    __MCMNG.messno = 0x3c;
    __MCMNG.errorno = 0;
    __MCMNG.transno = 0;
    return 0;
}

#ifndef ACNB_SOURCEPORT
static void bw__fsmc_IconWrite(EECore *c)
{
    c->gpr[V0].sd[0] = (s32)fsmc_IconWrite(c->gpr[A0].sw[0], c->gpr[A1].sw[0], (char *)(uintptr_t)c->gpr[A2].uw[0],
                                           (void *)(uintptr_t)c->gpr[A3].uw[0], c->gpr[T0].uw[0]);
}
#endif

/* 0x13eff0: queue a data file load (funcno 6) of up to size bytes into dthead; messno 0x46 = queued */
int fsmc_DataRead(int port, int slot, char *fname, void *dthead, unsigned int size)
{
    if (__MCMNG.funcno != 0) return 1;
    if (port >= 3 || port < 0) return -1;
    if (slot >= 5 || slot < 0) return -1;
    __MCMNG.portno = port;
    __MCMNG.slotno = slot;
    __MCMNG.funcno = 6;
    __MCMNG.name = fname;
    __MCMNG.datahd = dthead;
    __MCMNG.dataui = size;
    __MCMNG.messno = 0x46;
    __MCMNG.errorno = 0;
    __MCMNG.transno = 0;
    return 0;
}

#ifndef ACNB_SOURCEPORT
static void bw__fsmc_DataRead(EECore *c)
{
    c->gpr[V0].sd[0] = (s32)fsmc_DataRead(c->gpr[A0].sw[0], c->gpr[A1].sw[0], (char *)(uintptr_t)c->gpr[A2].uw[0],
                                          (void *)(uintptr_t)c->gpr[A3].uw[0], c->gpr[T0].uw[0]);
}
#endif

/* 0x13f0b0: queue a data file save (funcno 5) of size bytes from dthead; messno 0x3c = queued (count not cleared:
 * state 0 does it) */
int fsmc_DataWrite(int port, int slot, char *fname, void *dthead, unsigned int size)
{
    if (__MCMNG.funcno != 0) return 1;
    if (port >= 3 || port < 0) return -1;
    if (slot >= 5 || slot < 0) return -1;
    __MCMNG.portno = port;
    __MCMNG.slotno = slot;
    __MCMNG.funcno = 5;
    __MCMNG.name = fname;
    __MCMNG.datahdc = dthead;
    __MCMNG.dataui = size;
    __MCMNG.messno = 0x3c;
    __MCMNG.transno = 0;
    __MCMNG.errorno = 0;
    return 0;
}

#ifndef ACNB_SOURCEPORT
static void bw__fsmc_DataWrite(EECore *c)
{
    c->gpr[V0].sd[0] = (s32)fsmc_DataWrite(c->gpr[A0].sw[0], c->gpr[A1].sw[0], (char *)(uintptr_t)c->gpr[A2].uw[0],
                                           (void *)(uintptr_t)c->gpr[A3].uw[0], c->gpr[T0].uw[0]);
}
#endif

/* 0x13f170: queue a directory listing (funcno 4) */
int fsmc_GetDir(int port, int slot, char *name, unsigned int mode, unsigned int maxent, struct struct_526bec65 *table)
{
    if (__MCMNG.funcno != 0) return 1;
    if (port >= 3 || port < 0) return -1;
    if (slot >= 5 || slot < 0) return -1;
    __MCMNG.portno = port;
    __MCMNG.slotno = slot;
    __MCMNG.funcno = 4;
    __MCMNG.name = name;
    __MCMNG.dataui = mode;
    __MCMNG.dataui2 = maxent;
    __MCMNG.table = table;
    __MCMNG.transno = 0;
    return 0;
}

#ifndef ACNB_SOURCEPORT
static void bw__fsmc_GetDir(EECore *c)
{
    c->gpr[V0].sd[0] = (s32)fsmc_GetDir(c->gpr[A0].sw[0], c->gpr[A1].sw[0], (char *)(uintptr_t)c->gpr[A2].uw[0], c->gpr[A3].uw[0],
                                        c->gpr[T0].uw[0], (struct struct_526bec65 *)(uintptr_t)c->gpr[T1].uw[0]);
}
#endif

/* 0x13f220: queue a card info check (funcno 3) */
int fsmc_GetFileInfo(int port, int slot)
{
    if (__MCMNG.funcno != 0) return 1;
    if (port >= 3 || port < 0) return -1;
    if (slot >= 5 || slot < 0) return -1;
    __MCMNG.portno = port;
    __MCMNG.slotno = slot;
    __MCMNG.funcno = 3;
    __MCMNG.transno = 0;
    return 0;
}

/* 0x13f2b0: funcno 13. State 0 arms (no fall-through); 1 issues sceMcGetDir(name, maxent 1) into flchk (3 tries);
 * 2 polls: entry count > 0 -> messno 0x2e (found), 0 -> 0x2d (not found), 1; < 0 retried twice (back to 1), then
 * messno 3 (-2) / 0x64 and the Sync result is returned (funcno kept: FuncCheck -13) */
static int fsmc_FileChkFunc(void)
{
    PS2_FRAME(0x20);
    PS2_STACK(int, res, 0x1c);
    int port = __MCMNG.portno;
    int slot = __MCMNG.slotno;
    switch (__MCMNG.transno) {
    case 0:
        __MCMNG.transno = __MCMNG.transno + 1;
        __MCMNG.count = 0;
        return 0;
    case 1:
        if (sceMcGetDir(port, slot, __MCMNG.name, 0, 1, &flchk) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.funcno = 0;
                __MCMNG.messno = 0x64;
                return -1;
            }
            __MCMNG.count++;
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 2: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        if (r < 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.messno = r == -2 ? 3 : 0x64;
                return r;
            }
            __MCMNG.count++;
            __MCMNG.transno--;
            return 0;
        }
        if (r > 0) __MCMNG.messno = 0x2e;
        if (r == 0) __MCMNG.messno = 0x2d;
        __MCMNG.funcno = 0;
        return 1;
    }
    default:
        return 0;
    }
}

/* The four card-admin machines (MkDir / ChDir / UnFormat / Format) share one shape: state 0 arms (no fall-through);
 * 1 issues the call (refused 3x -> fail message, -10); 2 polls: 0 = done (1), any nonzero result retried twice (back
 * to 1), then funcno cleared, fail message, the Sync result returned (FuncCheck -0x64). */
/* 0x13f470: funcno 11; sceMcMkdir = sceMcOpen mode 0x40 (stand-in: -4 when the dir exists); ok messno 0, failed 0x64 */
static int fsmc_MkDirFunc(void)
{
    PS2_FRAME(0x20);
    PS2_STACK(int, res, 0x1c);
    int port = __MCMNG.portno;
    int slot = __MCMNG.slotno;
    switch (__MCMNG.transno) {
    case 0:
        __MCMNG.transno = __MCMNG.transno + 1;
        __MCMNG.count = 0;
        return 0;
    case 1:
        if (sceMcMkdir(port, slot, __MCMNG.name) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.messno = 0x64;
                return -10;   /* funcno kept: FuncCheck -funcno */
            }
            __MCMNG.count++;
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 2: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        if (r == 0) {
            __MCMNG.messno = 0;
            __MCMNG.funcno = 0;
            return 1;
        }
        if (__MCMNG.count == 2) {   /* any nonzero result, either sign */
            __MCMNG.funcno = 0;
            __MCMNG.messno = 0x64;
            return r;   /* the Sync result */
        }
        __MCMNG.count++;
        __MCMNG.transno--;
        return 0;
    }
    default:
        return 0;
    }
}

/* 0x13f5d0: funcno 10 (dead: nothing queues it); old dir -> name2; failed: 3 for -2 (unformatted), else 0x64 */
static int fsmc_ChDirFunc(void)
{
    PS2_FRAME(0x20);
    PS2_STACK(int, res, 0x1c);
    int port = __MCMNG.portno;
    int slot = __MCMNG.slotno;
    switch (__MCMNG.transno) {
    case 0:
        __MCMNG.transno = __MCMNG.transno + 1;
        __MCMNG.count = 0;
        return 0;
    case 1:
        if (sceMcChdir(port, slot, __MCMNG.name, __MCMNG.name2) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.messno = 0x64;
                return -10;   /* funcno kept: FuncCheck -funcno */
            }
            __MCMNG.count++;
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 2: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        if (r == 0) {
            __MCMNG.funcno = 0;
            __MCMNG.messno = 0;
            return 1;
        }
        if (__MCMNG.count == 2) {   /* any nonzero result, either sign */
            __MCMNG.funcno = 0;
            __MCMNG.messno = r == -2 ? 3 : 0x64;
            return r;   /* the Sync result */
        }
        __MCMNG.count++;
        __MCMNG.transno--;
        return 0;
    }
    default:
        return 0;
    }
}

/* 0x13f760: funcno 14 (dead: nothing queues it); ok messno 0x39, failed 0x37 */
static int fsmc_UnFormatFunc(void)
{
    PS2_FRAME(0x20);
    PS2_STACK(int, res, 0x1c);
    int port = __MCMNG.portno;
    int slot = __MCMNG.slotno;
    switch (__MCMNG.transno) {
    case 0:
        __MCMNG.transno = __MCMNG.transno + 1;
        __MCMNG.count = 0;
        return 0;
    case 1:
        if (sceMcUnformat(port, slot) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.messno = 0x37;
                return -10;   /* funcno kept: FuncCheck -funcno */
            }
            __MCMNG.count++;
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 2: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        if (r == 0) {
            __MCMNG.funcno = 0;
            __MCMNG.messno = 0x39;
            return 1;
        }
        if (__MCMNG.count == 2) {   /* any nonzero result, either sign */
            __MCMNG.funcno = 0;
            __MCMNG.messno = 0x37;
            return r;   /* the Sync result */
        }
        __MCMNG.count++;
        __MCMNG.transno--;
        return 0;
    }
    default:
        return 0;
    }
}

/* 0x13f8c0: funcno 9; ok messno 0x34, failed 0x32 */
static int fsmc_FormatFunc(void)
{
    PS2_FRAME(0x20);
    PS2_STACK(int, res, 0x1c);
    int port = __MCMNG.portno;
    int slot = __MCMNG.slotno;
    switch (__MCMNG.transno) {
    case 0:
        __MCMNG.transno = __MCMNG.transno + 1;
        __MCMNG.count = 0;
        return 0;
    case 1:
        if (sceMcFormat(port, slot) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.messno = 0x32;
                return -10;   /* funcno kept: FuncCheck -funcno */
            }
            __MCMNG.count++;
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 2: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        if (r == 0) {
            __MCMNG.funcno = 0;
            __MCMNG.messno = 0x34;
            return 1;
        }
        if (__MCMNG.count == 2) {   /* any nonzero result, either sign */
            __MCMNG.funcno = 0;
            __MCMNG.messno = 0x32;
            return r;   /* the Sync result */
        }
        __MCMNG.count++;
        __MCMNG.transno--;
        return 0;
    }
    default:
        return 0;
    }
}

/* 0x13fa20: funcno 12 (dead: nothing queues it), read a card file into datahd. 0 arms and falls into 1 (card check,
 * PS2 card required, else the cleanup 0x64 with messno 0), 2/3 open read-only, 4..6 read dataui - rdofs bytes until
 * rdofs reaches dataui (a read returning 0 loops for ever), 7..9 close -> messno 0x48, 1. 0x64..0x66 cleanup close,
 * messno 0x47, -0x64. Compare chain: any other transno returns 0 for ever. */
static int fsmc_IconReadFunc(void)
{
    PS2_FRAME(0x30);
    PS2_STACK(int, res, 0x2c);
    int port = __MCMNG.portno;
    int slot = __MCMNG.slotno;
    switch (__MCMNG.transno) {
    case 0:
        __MCMNG.transno = __MCMNG.transno + 1;
        s_ReadNum = 0;
        fd = 0;
        fsmc_IconReadFunc__rdofs = 0;
        __MCMNG.count = 0;
        /* fall through (0x13faec: no branch) */
    case 1:
        __MCMNG.errorno = _fsmc_GetInfo(port, slot, &MC_TYPE(port, slot), &MC_FREE(port, slot), &MC_FORMAT(port, slot), __MCMNG.errorno);
        if (__MCMNG.errorno > 0) return 0;
        __MCMNG.messno = _fsmc_InfoErrMess(__MCMNG.errorno);   /* no 0x64 -> 0x47 mapping */
        if (__MCMNG.messno != 0) {
            __MCMNG.funcno = 0;
            return -10;
        }
        if (MC_TYPE(port, slot) != 2) {
            __MCMNG.transno = 0x64;   /* messno stays 0 */
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 2:
        if (sceMcOpen(port, slot, __MCMNG.name, 1) != 0) {
            if (__MCMNG.count == 2) __MCMNG.transno = 0x64;
            else __MCMNG.count++;
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 3: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        if (r < 0) {
            if (__MCMNG.count == 2) {
                _fsmc_OpenErrMess(r);
                if (__MCMNG.messno == 0x64) __MCMNG.messno = 0x47;
                __MCMNG.funcno = 0;
                __MCMNG.errorno = -1;
                __MCMNG.transno = 0x64;
                return res;
            }
            __MCMNG.count++;
            __MCMNG.transno--;
            return 0;
        }
        fd = r;   /* count not cleared */
        __MCMNG.transno++;
        return 0;
    }
    case 4:
        __MCMNG.transno = __MCMNG.transno + 1;
        __MCMNG.count = 0;
        return 0;
    case 5:
        if (sceMcRead(fd, (char *)__MCMNG.datahd + fsmc_IconReadFunc__rdofs,
                      (int)(__MCMNG.dataui - (unsigned int)fsmc_IconReadFunc__rdofs)) != 0) {
            if (__MCMNG.count == 2) __MCMNG.transno = 0x64;
            else __MCMNG.count++;
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 6: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        if (r < 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.errorno = r;
                __MCMNG.transno = 0x64;
            } else {
                __MCMNG.count++;
                __MCMNG.transno--;
            }
            return 0;
        }
        __MCMNG.count = 0;
        fsmc_IconReadFunc__rdofs += r;
        s_ReadNum += (unsigned int)r;
        if ((unsigned int)fsmc_IconReadFunc__rdofs < __MCMNG.dataui) __MCMNG.transno = 4;   /* next chunk */
        else __MCMNG.transno++;
        return 0;
    }
    case 7:
        __MCMNG.transno = __MCMNG.transno + 1;
        __MCMNG.count = 0;
        return 0;
    case 8:
        if (sceMcClose(fd) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.funcno = 0;
                __MCMNG.messno = 0x47;
                return -1;
            }
            __MCMNG.count++;
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 9: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        __MCMNG.funcno = 0;   /* delay slot 0x13fecc, before the result test: a failed close is abandoned */
        if (r < 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.messno = 0x47;
                return r;
            }
            __MCMNG.count++;
            __MCMNG.transno--;
            return 0;
        }
        __MCMNG.messno = 0x48;
        return 1;
    }
    case 0x64:
        if (fd < 0) {   /* dead: fd starts at 0 */
            __MCMNG.funcno = 0;
            __MCMNG.messno = 0x47;
            return __MCMNG.errorno;
        }
        __MCMNG.transno = __MCMNG.transno + 1;
        __MCMNG.count = 0;
        return 0;
    case 0x65:
        if (sceMcClose(fd) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.funcno = 0;
                __MCMNG.messno = 0x47;
                /* no return (0x13ffa4): falls into the count++ below, count 3, returns 0 */
            }
            __MCMNG.count++;
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 0x66:   /* res ignored */
        if (sceMcSync(1, 0, &res) == 0) return 0;
        __MCMNG.funcno = 0;
        __MCMNG.messno = 0x47;
        return -0x64;
    default:
        return 0;
    }
}

/* 0x140020: funcno 8 (dead: nothing queues it), copy a host file (path in name2, e.g. host0:..) onto the card. 1/2
 * card check, 3/4 look the card file up, 5 open the host file (sceOpen) + size (dataui2 or lseek END) + free-space
 * check, 8/9 open the card file, 0xa..0xc read 0x400-byte chunks into rddata and write them, 0xd close the host
 * file, 0xe/0xf close the card file -> messno 0x3e, 1. 0x64 is the error cleanup and never advances (see header). */
static int fsmc_IconWriteFileFunc(void)
{
    PS2_FRAME(0x70);
    PS2_STACK(char[64], hostName, 0x20);
    PS2_STACK(int, res, 0x6c);
    int port = __MCMNG.portno;
    int slot = __MCMNG.slotno;
    switch (__MCMNG.transno) {
    case 0:
        fd = 0;
        __MCMNG.transno = __MCMNG.transno + 1;
        fsmc_IconWriteFileFunc__allsize = 0;
        fsmc_IconWriteFileFunc__wtsize = 0;
        __MCMNG.count = 0;
        return 0;
    case 1:
        __MCMNG.errorno = _fsmc_GetInfo(port, slot, &MC_TYPE(port, slot), &MC_FREE(port, slot), &MC_FORMAT(port, slot), __MCMNG.errorno);
        if (__MCMNG.errorno > 0) return 0;
        __MCMNG.messno = _fsmc_InfoErrMess(__MCMNG.errorno);   /* no 0x64 -> 0x3d mapping */
        if (__MCMNG.messno != 0) {
            __MCMNG.funcno = 0;
            return -10;
        }
        __MCMNG.transno++;
        return 0;
    case 2:
        if (MC_TYPE(port, slot) != 2) {
            __MCMNG.funcno = 0;
            __MCMNG.messno = 0x3d;
            return -4;
        }
        __MCMNG.transno = __MCMNG.transno + 1;   /* count not cleared */
        return 0;
    case 3:
        if (sceMcGetDir(port, slot, __MCMNG.name, 0, 1, &flchk) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.funcno = 0;
                __MCMNG.messno = 0x64;
                return -1;
            }
            __MCMNG.count++;
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 4: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        if (r < 0) {
            if (__MCMNG.count == 2) {
                if (r == -2) {
                    __MCMNG.messno = 3;
                    return r;
                }
                if (r == -4) {
                    flchk.FileSizeByte = 0;
                    __MCMNG.transno++;
                    return 0;
                }
                __MCMNG.messno = 0x64;
                return r;
            }
            __MCMNG.count++;
            __MCMNG.transno--;
            return 0;
        }
        if (r == 0) flchk.FileSizeByte = 0;
        __MCMNG.transno++;
        return 0;
    }
    case 5: {
        unsigned int old = flchk.FileSizeByte;
        int avail = (int)((old + 0x3ff) >> 10) + MC_FREE(port, slot);
        if (old == 0 && avail < 3) {
            __MCMNG.funcno = 0;
            __MCMNG.messno = 0x1f;
            return -6;
        }
        ps2_strcpy(hostName, __MCMNG.name2);   /* no bound: a name2 of 64+ chars overruns the frame */
        fsmc_IconWriteFileFunc__src = sceOpen(hostName, 1);
        if (fsmc_IconWriteFileFunc__src < 0) {
            __MCMNG.funcno = 0;
            __MCMNG.messno = 0x3d;
            return fsmc_IconWriteFileFunc__src;
        }
        if (__MCMNG.dataui2 != 0) {
            fsmc_IconWriteFileFunc__allsize = (int)__MCMNG.dataui2;
        } else {
            fsmc_IconWriteFileFunc__allsize = sceLseek(fsmc_IconWriteFileFunc__src, 0, 2);
            sceLseek(fsmc_IconWriteFileFunc__src, 0, 0);
        }
        /* signed (slt, sra = round toward zero) unlike IconWriteFunc's unsigned check */
        if (avail - (flchk.FileSizeByte != 0 ? 0 : 2) < (fsmc_IconWriteFileFunc__allsize + 0x3ff) / 1024) {
            __MCMNG.funcno = 0;   /* the host file stays open */
            __MCMNG.messno = 0x3d;
            return -0x65;
        }
        __MCMNG.count = 0;
        __MCMNG.transno = 8;
        return 0;
    }
    case 8:
        if (sceMcOpen(port, slot, __MCMNG.name, flchk.FileSizeByte != 0 ? 3 : 0x203) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.funcno = 0;
                __MCMNG.messno = 0x3d;
                return -0xb;
            }
            __MCMNG.count++;
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 9: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        if (r < 0) {
            if (__MCMNG.count == 2) {
                _fsmc_OpenErrMess(r);
                if (__MCMNG.messno == 0x64) __MCMNG.messno = 0x3d;
                __MCMNG.errorno = -1;
                __MCMNG.transno = 0x64;   /* funcno kept */
                return 0;
            }
            __MCMNG.count++;
            __MCMNG.transno--;
            return 0;
        }
        fd = r;   /* count not cleared */
        __MCMNG.transno++;
        return 0;
    }
    case 10:
        fsmc_IconWriteFileFunc__size = sceRead(fsmc_IconWriteFileFunc__src, rddata, 0x400);
        if (fsmc_IconWriteFileFunc__size <= 0) {   /* EOF or error: close the host file */
            __MCMNG.transno = 0xd;
            return 0;
        }
        __MCMNG.count = 0;
        __MCMNG.transno++;
        return 0;
    case 11:
        if (sceMcWrite(fd, rddata, fsmc_IconWriteFileFunc__size) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.errorno = -1;
                __MCMNG.transno = 0x64;
            } else {
                __MCMNG.count++;
            }
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 12: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        if (r < 0) {
            if (__MCMNG.count == 2 || r == -8) {
                __MCMNG.errorno = -1;
                __MCMNG.transno = 0x64;
            } else {
                __MCMNG.count++;
                __MCMNG.transno--;
            }
            return 0;
        }
        __MCMNG.count = 0;
        fsmc_IconWriteFileFunc__wtsize += fsmc_IconWriteFileFunc__size;   /* requested size, not r */
        if (fsmc_IconWriteFileFunc__wtsize < fsmc_IconWriteFileFunc__allsize) __MCMNG.transno -= 2;   /* next chunk (0xa) */
        else __MCMNG.transno++;
        return 0;
    }
    case 13:
        sceClose(fsmc_IconWriteFileFunc__src);
        __MCMNG.transno++;
        return 0;
    case 14:
        if (sceMcClose(fd) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.errorno = -1;
                __MCMNG.transno = 0x64;
            } else {
                __MCMNG.count++;
            }
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 15: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        if (r < 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.funcno = 0;
                __MCMNG.messno = 0x3d;
                return r;
            }
            __MCMNG.count++;
            __MCMNG.transno--;   /* funcno still set: the retry runs */
            return 0;
        }
        __MCMNG.funcno = 0;
        __MCMNG.messno = 0x3e;
        return 1;
    }
    case 0x64:
        if (fsmc_IconWriteFileFunc__src != 0) sceClose(fsmc_IconWriteFileFunc__src);
        if (fd < 0) {   /* dead: fd starts at 0 */
            __MCMNG.funcno = 0;
            __MCMNG.messno = 0x3d;
            return -0x3e7;
        }
        __MCMNG.count = 0;   /* transno not advanced (0x1408d0): stays in 0x64 for ever, 0x65 / 0x66 unreachable */
        return 0;
    case 0x65:
        if (sceMcClose(fd) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.funcno = 0;
                __MCMNG.messno = 0x3d;
                return -0x3e7;
            }
            __MCMNG.count++;
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 0x66:
        if (sceMcSync(1, 0, &res) == 0) return 0;
        __MCMNG.funcno = 0;
        __MCMNG.messno = 0x3d;
        return -0x3e7;
    default:
        return 0;
    }
}

/* 0x140990: funcno 7, the icon file write (ICO / icon.sys from Ac2_Mcf_SaveProc). 1/2 card check (PS2 card
 * required), 3/4 look the file up (flchk.FileSizeByte = existing size, 0 = new), 5 free-space check, 8/9 open (0x203
 * create when new, 3 = no truncate), 0xa..0xc write datahdc + wtofs (the whole rest in one sceMcWrite; a short write
 * loops), 0xd/0xe close -> messno 0x3e, 1. 0x64..0x66: error cleanup close, -999. Compare chain: transno 6, 7 or any
 * other value returns 0 for ever. */
static int fsmc_IconWriteFunc(void)
{
    PS2_FRAME(0x20);
    PS2_STACK(int, res, 0x1c);
    int port = __MCMNG.portno;
    int slot = __MCMNG.slotno;
    switch (__MCMNG.transno) {
    case 0:   /* count not cleared, wtsize not cleared */
        s_WriteNum = 0;
        __MCMNG.transno = __MCMNG.transno + 1;
        fd = 0;
        fsmc_IconWriteFunc__wtofs = 0;
        return 0;
    case 1:
        __MCMNG.errorno = _fsmc_GetInfo(port, slot, &MC_TYPE(port, slot), &MC_FREE(port, slot), &MC_FORMAT(port, slot), __MCMNG.errorno);
        if (__MCMNG.errorno > 0) return 0;
        __MCMNG.messno = _fsmc_InfoErrMess(__MCMNG.errorno);
        if (__MCMNG.messno != 0) {
            if (__MCMNG.messno == 0x64) __MCMNG.messno = 0x3d;
            __MCMNG.funcno = 0;
            return -10;
        }
        __MCMNG.transno++;
        return 0;
    case 2:
        if (MC_TYPE(port, slot) != 2) {
            __MCMNG.funcno = 0;
            __MCMNG.messno = 0x3d;
            return -4;
        }
        __MCMNG.transno = __MCMNG.transno + 1;
        __MCMNG.count = 0;
        return 0;
    case 3:
        if (sceMcGetDir(port, slot, __MCMNG.name, 0, 1, &flchk) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.funcno = 0;
                __MCMNG.messno = 0x64;
                return -1;
            }
            __MCMNG.count++;
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 4: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        if (r < 0) {
            if (__MCMNG.count == 2) {
                if (r == -2) {
                    __MCMNG.messno = 3;
                    return r;   /* funcno kept */
                }
                if (r == -4) {   /* no such file: a new icon */
                    flchk.FileSizeByte = 0;
                    __MCMNG.transno++;
                    return 0;
                }
                __MCMNG.messno = 0x64;
                return r;
            }
            __MCMNG.count++;
            __MCMNG.transno--;
            return 0;
        }
        if (r == 0) flchk.FileSizeByte = 0;
        __MCMNG.transno++;
        return 0;
    }
    case 5: {
        unsigned int old = flchk.FileSizeByte;
        int avail = (int)((old + 0x3ff) >> 10) + MC_FREE(port, slot);
        if (old == 0 && avail < 3) {
            __MCMNG.funcno = 0;
            __MCMNG.messno = 0x1f;
            return -6;
        }
        unsigned int need = (__MCMNG.dataui + 0x3ff) >> 10;
        if ((unsigned int)(avail - (old != 0 ? 0 : 2)) < need) {   /* sltu */
            __MCMNG.funcno = 0;
            __MCMNG.messno = 0x3d;
            return -0x65;
        }
        __MCMNG.count = 0;
        __MCMNG.transno = 8;   /* 6, 7 do not exist */
        return 0;
    }
    case 8:
        if (sceMcOpen(port, slot, __MCMNG.name, flchk.FileSizeByte != 0 ? 3 : 0x203) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.funcno = 0;
                __MCMNG.messno = 0x3d;
                return -0xb;
            }
            __MCMNG.count++;
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 9: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        if (r < 0) {
            if (__MCMNG.count == 2) {
                _fsmc_OpenErrMess(r);
                if (__MCMNG.messno == 0x64) __MCMNG.messno = 0x3d;
                __MCMNG.errorno = -1;
                __MCMNG.transno = 0x64;   /* funcno kept, returns 0 */
                return 0;
            }
            __MCMNG.count++;
            __MCMNG.transno--;
            return 0;
        }
        fd = r;   /* count not cleared */
        __MCMNG.transno++;
        return 0;
    }
    case 10:
        __MCMNG.transno = __MCMNG.transno + 1;
        __MCMNG.count = 0;
        fsmc_IconWriteFunc__wtsize = (int)(__MCMNG.dataui - (unsigned int)fsmc_IconWriteFunc__wtofs);
        return 0;
    case 11:
        if (sceMcWrite(fd, (char *)__MCMNG.datahdc + fsmc_IconWriteFunc__wtofs, fsmc_IconWriteFunc__wtsize) != 0) {
            if (__MCMNG.count == 2) {   /* no messno */
                __MCMNG.errorno = -1;
                __MCMNG.transno = 0x64;
            } else {
                __MCMNG.count++;
            }
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 12: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        if (r < 0) {
            if (__MCMNG.count == 2 || r == -8) {
                __MCMNG.errorno = -1;
                __MCMNG.transno = 0x64;
            } else {
                __MCMNG.count++;
                __MCMNG.transno--;   /* re-issue the same write */
            }
            return 0;
        }
        __MCMNG.count = 0;
        fsmc_IconWriteFunc__wtofs += r;
        s_WriteNum += (unsigned int)r;
        if ((unsigned int)fsmc_IconWriteFunc__wtofs < __MCMNG.dataui) __MCMNG.transno = 0xa;   /* short write */
        else __MCMNG.transno++;
        return 0;
    }
    case 13:
        if (sceMcClose(fd) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.errorno = -1;
                __MCMNG.transno = 0x64;
            } else {
                __MCMNG.count++;
            }
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 14: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        __MCMNG.funcno = 0;   /* delay slot 0x1410d4, before the result test: a failed close is abandoned */
        if (r < 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.messno = 0x3d;
                return r;
            }
            __MCMNG.count++;
            __MCMNG.transno--;
            return 0;
        }
        __MCMNG.messno = 0x3e;
        return 1;
    }
    case 0x64:   /* error cleanup (no Sync here, unlike DataWriteFunc) */
        if (fd < 0) {   /* dead: fd starts at 0 */
            __MCMNG.funcno = 0;
            __MCMNG.messno = 0x3d;
            return -0x3e7;
        }
        __MCMNG.transno = __MCMNG.transno + 1;
        __MCMNG.count = 0;
        return 0;
    case 0x65:
        if (sceMcClose(fd) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.funcno = 0;
                __MCMNG.messno = 0x3d;
                return -0x3e7;
            }
            __MCMNG.count++;
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 0x66:   /* res and errorno ignored */
        if (sceMcSync(1, 0, &res) == 0) return 0;
        __MCMNG.funcno = 0;
        __MCMNG.messno = 0x3d;
        return -0x3e7;
    default:
        return 0;
    }
}

/* DataReadFunc's shared retry arms. Issue refused (the sceMc call returned nonzero): third failure -> messno 0x47 and
 * the cleanup state 0x64, else count++. Sync failed (res < 0): third failure -> 0x47 (-5: 0x64) and 0x64, else count++
 * and transno-- (re-issue the previous state). Both return 0. */
static int fsmc_dr_issue_fail(void)
{
    if (__MCMNG.count == 2) {
        __MCMNG.messno = 0x47;
        __MCMNG.transno = 0x64;
    } else {
        __MCMNG.count++;
    }
    return 0;
}

static int fsmc_dr_sync_fail(int r)
{
    if (__MCMNG.count == 2) {
        __MCMNG.messno = 0x47;
        if (r == -5) __MCMNG.messno = 0x64;
        __MCMNG.transno = 0x64;
    } else {
        __MCMNG.count++;
        __MCMNG.transno--;
    }
    return 0;
}

/* 0x141230: funcno 6, the card file load. 0/1 card check (_fsmc_GetInfo x3, PS2 card required), 2/3 open read-only,
 * 4/5 read the u32 payload size, 6 file length, 7/8 seek to the last 32 bytes, 9/0xa read them and test the marker
 * byte, 0xb/0xc seek back to 4, 0xe/0xd block reads (checksummed, copied out by _fsmc_GetReadData) until the short
 * block, 0xf/0x10/0x11 close -> messno 0x48, 1. 0x64..0x66: error cleanup (close the open file), -200. A compare
 * chain in the original (no jump table); any other transno returns 0. */
static int fsmc_DataReadFunc(void)
{
    PS2_FRAME(0x40);
    PS2_STACK(int, res, 0x3c);
    int port = __MCMNG.portno;
    int slot = __MCMNG.slotno;
    switch (__MCMNG.transno) {
    case 0:
        s_ReadNum = 0;
        __MCMNG.transno = __MCMNG.transno + 1;
        fd = -1;
        __MCMNG.count = 0;
        fsmc_DataReadFunc__rdofs = 0;
        /* fall through */
    case 1: {
        int *type = &MC_TYPE(port, slot);
        __MCMNG.errorno = _fsmc_GetInfo(port, slot, type, &MC_FREE(port, slot), &MC_FORMAT(port, slot), __MCMNG.errorno);
        if (__MCMNG.errorno > 0) return 0;   /* GetInfo sub-state still running */
        __MCMNG.messno = _fsmc_InfoErrMess(__MCMNG.errorno);
        if (__MCMNG.messno != 0) {
            __MCMNG.funcno = 0;
            return -10;
        }
        if (*type != 2) {
            __MCMNG.funcno = 0;
            __MCMNG.messno = 1;
            return -10;
        }
        __MCMNG.count = 0;
        __MCMNG.transno++;
        return 0;
    }
    case 2:
        if (sceMcOpen(port, slot, __MCMNG.name, 1) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.funcno = 0;
                __MCMNG.messno = 0x47;
                return -10;
            }
            __MCMNG.count++;
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 3: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        if (r < 0) {
            if (__MCMNG.count == 2) {
                _fsmc_OpenErrMess(r);
                if (__MCMNG.messno == 0x64) __MCMNG.messno = 0x47;
                if (res == -5) __MCMNG.messno = 0x64;
                __MCMNG.funcno = 0;
                return res;   /* the Sync result (0x1414e0 reloads it), not -10 */
            }
            __MCMNG.count++;
            __MCMNG.transno--;
            return 0;
        }
        __MCMNG.count = 0;
        fd = r;
        __MCMNG.transno++;
        return 0;
    }
    case 4:
        /* length 4: the dispatch chain's compare constant left in a2 (0x1412f4); the call site sets no a2 */
        if (sceMcRead(fd, &fsmc_DataReadFunc__datasize, 4) != 0) return fsmc_dr_issue_fail();
        __MCMNG.transno++;
        return 0;
    case 5:
        if (sceMcSync(1, 0, &res) == 0) return 0;
        if (res < 0) return fsmc_dr_sync_fail(res);
        __MCMNG.transno++;
        return 0;
    case 6: {
        int ds = fsmc_DataReadFunc__datasize;
        int t = (int)((unsigned int)ds + 6u);
        if ((int)((unsigned int)ds - 0x3fbu) > 0) t += (int)((unsigned int)ds + 3u) / 1023;
        fsmc_DataReadFunc__allsize = t;
        __MCMNG.count = 0;
        fsmc_DataReadFunc__wtchk = fsmc_DataReadFunc__allsize - 1;
        fsmc_DataReadFunc__allsize = (fsmc_DataReadFunc__allsize + 0x1f) & -0x20;
        __MCMNG.transno++;
        fsmc_DataReadFunc__wtchk = fsmc_DataReadFunc__wtchk - (fsmc_DataReadFunc__allsize - 0x20);
        return 0;
    }
    case 7:
        if (sceMcSeek(fd, fsmc_DataReadFunc__allsize - 0x20, 0) != 0) return fsmc_dr_issue_fail();
        __MCMNG.transno++;
        return 0;
    case 8:
        if (sceMcSync(1, 0, &res) == 0) return 0;
        if (res < 0) return fsmc_dr_sync_fail(res);
        __MCMNG.count = 0;
        __MCMNG.transno++;
        return 0;
    case 9:
        if (sceMcRead(fd, rddata, 0x20) != 0) return fsmc_dr_issue_fail();
        __MCMNG.transno++;
        return 0;
    case 10:
        if (sceMcSync(1, 0, &res) == 0) return 0;
        if (res < 0) return fsmc_dr_sync_fail(res);
        if (rddata[fsmc_DataReadFunc__wtchk] != 1) {   /* rddata not cleared before the probe read */
            __MCMNG.messno = 0x47;
            __MCMNG.transno = 0x64;
            return 0;
        }
        __MCMNG.count = 0;
        __MCMNG.transno++;
        return 0;
    case 11:
        if (sceMcSeek(fd, 4, 0) != 0) return fsmc_dr_issue_fail();
        __MCMNG.transno++;
        return 0;
    case 12:
        if (sceMcSync(1, 0, &res) == 0) return 0;
        if (res < 0) return fsmc_dr_sync_fail(res);
        __MCMNG.transno = 0xe;
        memset(rddata, 0, 0x400);
        __MCMNG.count = 0;
        return 0;
    case 13: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        if (r < 0) {
            if (__MCMNG.count == 2) {
                if (r == -2) __MCMNG.messno = 3;
                else if (r < -10) __MCMNG.messno = 1;
                else __MCMNG.messno = 0x47;
                if (r == -5) __MCMNG.messno = 0x64;
                __MCMNG.errorno = r;
                __MCMNG.transno = 0x64;
            } else {
                __MCMNG.count++;
                __MCMNG.transno++;   /* forward to the read issue 0xe (every other retry goes back) */
            }
            return 0;
        }
        s_ReadNum += r;
        int g = _fsmc_GetReadData(&fsmc_DataReadFunc__rdofs, fsmc_DataReadFunc__datasize);
        if (g < 0) {
            __MCMNG.errorno = -0x18f;
            __MCMNG.messno = 0x47;
            __MCMNG.transno = 0x64;
            return 0;
        }
        if (g > 0) {
            __MCMNG.transno = 0xf;
            return 0;
        }
        __MCMNG.count = 0;
        __MCMNG.transno++;
    }
        /* fall through: the next block read is issued in the same tick (0x141bf4 -> 0x141bf8) */
    case 14: {
        unsigned char *buf;
        int len;
        if (s_pWkBuff != 0) {
            buf = s_pWkBuff;
            len = (int)(s_WkKByte << 10);
        } else {
            buf = rddata;
            len = 0x400;
        }
        memset(HOSTPTR(buf), 0, (unsigned int)len);
        if (fsmc_DataReadFunc__rdofs == 0) len -= 4;   /* block 0: the 4-byte size header was read in state 4 */
        if (sceMcRead(fd, buf, len) != 0) return fsmc_dr_issue_fail();
        __MCMNG.transno = 0xd;
        return 0;
    }
    case 15:
        __MCMNG.transno = __MCMNG.transno + 1;
        __MCMNG.count = 0;
        return 0;
    case 16:
        if (sceMcClose(fd) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.funcno = 0;
                __MCMNG.messno = 0x47;
                return -0x64;
            }
            __MCMNG.count++;
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 17: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        if (r < 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.funcno = 0;
                __MCMNG.messno = 0x47;
                if (r == -5) __MCMNG.messno = 0x64;
                return r;
            }
            __MCMNG.count++;
            __MCMNG.transno--;
            return 0;
        }
        __MCMNG.funcno = 0;
        __MCMNG.messno = 0x48;
        return 1;
    }
    case 0x64:   /* error cleanup: messno already set */
        if (fd < 0) {
            __MCMNG.funcno = 0;
            return -0x64;
        }
        __MCMNG.transno = __MCMNG.transno + 1;
        __MCMNG.count = 0;
        return 0;
    case 0x65:
        if (sceMcClose(fd) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.messno = 0x47;
                return -1;   /* funcno kept: FuncCheck -6 */
            }
            __MCMNG.count++;
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 0x66:
        if (sceMcSync(1, 0, &res) != 0) {
            __MCMNG.funcno = 0;
            return -200;
        }
        return 0;
    default:
        return 0;
    }
}

/* DataWriteFunc's shared retry arms. Issue refused: third failure -> messno 0x3d and the cleanup state 0x64, else
 * count++. Sync failed (res < 0; m8: -8 also ends at once): third failure -> 0x3d / 0x64, else count++ and transno =
 * retry. Both return 0. */
static int fsmc_dw_issue_fail(void)
{
    if (__MCMNG.count == 2) {
        __MCMNG.messno = 0x3d;
        __MCMNG.transno = 0x64;
    } else {
        __MCMNG.count++;
    }
    return 0;
}

static int fsmc_dw_sync_fail(int r, int m8, int retry)
{
    if (__MCMNG.count == 2 || (m8 && r == -8)) {
        __MCMNG.messno = 0x3d;
        __MCMNG.transno = 0x64;
    } else {
        __MCMNG.count++;
        __MCMNG.transno = retry;
    }
    return 0;
}

/* 0x141eb0: funcno 5, the card file save. 1/2 card check (_fsmc_GetInfo x3, PS2 card required), 3/4 look the file up
 * (flchk.FileSizeByte = existing size, 0 = new), 5 free-space check and allsize, 9/0xa/0xb open (0x203 create when
 * new, 3 = no truncate), 0xc/0xd seek to allsize - 0x20, 0xe/0xf/0x10 write a 32-byte zero tail block (extends the
 * file), 0x11/0x12 seek to 0, then 0x14 (_fsmc_SetWriteData builds the next blocks) / 0x15 write / 0x13 sync until
 * dataui < wtofs, 0x16/0x17 close -> messno 0x3e, 1. 0x64..0x66: error cleanup (close), -999. A compare chain in the
 * original; transno 6..8 or any other value returns 0 for ever. */
static int fsmc_DataWriteFunc(void)
{
    PS2_FRAME(0x20);
    PS2_STACK(int, res, 0x1c);
    int port = __MCMNG.portno;
    int slot = __MCMNG.slotno;
    switch (__MCMNG.transno) {
    case 0:
        s_WriteNum = 0;
        __MCMNG.transno = __MCMNG.transno + 1;
        fd = 0;   /* not -1: state 0x64's fd < 0 arm is dead */
        fsmc_DataWriteFunc__wtofs = 0;
        __MCMNG.count = 0;
        fsmc_DataWriteFunc__wsize = 0;
        return 0;   /* no fall-through */
    case 1:
        __MCMNG.errorno = _fsmc_GetInfo(port, slot, &MC_TYPE(port, slot), &MC_FREE(port, slot), &MC_FORMAT(port, slot), __MCMNG.errorno);
        if (__MCMNG.errorno > 0) return 0;   /* GetInfo sub-state still running */
        __MCMNG.messno = _fsmc_InfoErrMess(__MCMNG.errorno);
        if (__MCMNG.messno != 0) {
            __MCMNG.funcno = 0;
            return -1;
        }
        __MCMNG.transno++;
        return 0;
    case 2:
        if (MC_TYPE(port, slot) != 2) {
            __MCMNG.funcno = 0;
            __MCMNG.messno = 1;
            return -4;
        }
        __MCMNG.transno = __MCMNG.transno + 1;
        __MCMNG.count = 0;
        return 0;
    case 3:
        if (sceMcGetDir(port, slot, __MCMNG.name, 0, 1, &flchk) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.funcno = 0;
                __MCMNG.messno = 0x64;
                return -1;
            }
            __MCMNG.count++;
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 4: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        if (r < 0) {
            if (__MCMNG.count == 2) {
                if (r == -2) {
                    __MCMNG.messno = 3;
                    return r;   /* funcno kept: FuncCheck -5 */
                }
                if (r == -4) {   /* no such file: a new save */
                    flchk.FileSizeByte = 0;
                    __MCMNG.transno++;
                    return 0;
                }
                __MCMNG.messno = 0x64;
                return r;
            }
            __MCMNG.count++;
            __MCMNG.transno--;
            return 0;
        }
        if (r == 0) flchk.FileSizeByte = 0;   /* > 0: the entry the GetDir filled stays */
        __MCMNG.transno++;
        return 0;
    }
    case 5: {
        unsigned int old = flchk.FileSizeByte;
        int avail = (int)((old + 0x3ff) >> 10) + MC_FREE(port, slot);
        if (old == 0 && avail < 3) {
            __MCMNG.funcno = 0;
            __MCMNG.messno = 0x1f;
            return -6;
        }
        unsigned int size = __MCMNG.dataui;
        int t = (int)(size + 6u);
        if ((int)(size - 0x3fbu) > 0) t += (int)(size + 3u) / 1023;
        unsigned int all = (unsigned int)(t + 0x1f) & ~0x1fu;
        fsmc_DataWriteFunc__allsize = all;
        if (avail - (old != 0 ? 0 : 2) < (int)((all + 0x3ff) >> 10)) {
            __MCMNG.funcno = 0;
            __MCMNG.messno = 0x1f;
            return -6;
        }
        __MCMNG.transno = 9;   /* 6..8 do not exist */
        return 0;
    }
    case 9:
        __MCMNG.transno = __MCMNG.transno + 1;
        __MCMNG.count = 0;
        /* fall through */
    case 10:
        if (sceMcOpen(port, slot, __MCMNG.name, flchk.FileSizeByte != 0 ? 3 : 0x203) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.funcno = 0;
                __MCMNG.messno = 0x3d;
                return -10;
            }
            __MCMNG.count++;
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 11: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        if (r < 0) {
            if (__MCMNG.count == 2) {
                _fsmc_OpenErrMess(r);
                if (__MCMNG.messno == 0x64) __MCMNG.messno = 0x3d;
                __MCMNG.funcno = 0;
                return -5;
            }
            __MCMNG.count++;
            __MCMNG.transno--;
            return 0;
        }
        __MCMNG.count = 0;
        fd = r;
        __MCMNG.transno++;
        return 0;
    }
    case 12:   /* pre-extend the file to allsize */
        if (sceMcSeek(fd, (int)fsmc_DataWriteFunc__allsize - 0x20, 0) != 0) return fsmc_dw_issue_fail();
        __MCMNG.transno++;
        return 0;
    case 13:
        if (sceMcSync(1, 0, &res) == 0) return 0;
        if (res < 0) return fsmc_dw_sync_fail(res, 0, __MCMNG.transno - 1);
        __MCMNG.transno++;
        return 0;
    case 14:
        __MCMNG.count = 0;
        memset(rddata, 0, 0x20);
        __MCMNG.transno++;
        /* fall through */
    case 15:   /* the zero tail block */
        if (sceMcWrite(fd, rddata, 0x20) != 0) return fsmc_dw_issue_fail();
        __MCMNG.transno++;
        return 0;
    case 16:
        if (sceMcSync(1, 0, &res) == 0) return 0;
        if (res < 0) return fsmc_dw_sync_fail(res, 1, __MCMNG.transno - 1);
        __MCMNG.count = 0;
        __MCMNG.transno++;
        return 0;
    case 17:
        if (sceMcSeek(fd, 0, 0) != 0) return fsmc_dw_issue_fail();
        __MCMNG.transno++;
        return 0;
    case 18:
        if (sceMcSync(1, 0, &res) == 0) return 0;
        if (res < 0) return fsmc_dw_sync_fail(res, 0, __MCMNG.transno - 1);
        __MCMNG.transno = 0x14;   /* skips 0x13 (count not cleared) */
        return 0;
    case 19: {   /* completion of a data write */
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        /* retry re-issues the same write (0x15): the buffer is not rebuilt, wtofs is already advanced */
        if (r < 0) return fsmc_dw_sync_fail(r, 1, 0x15);
        __MCMNG.count = 0;
        s_WriteNum += (unsigned int)r;   /* not checked against wsize */
        __MCMNG.transno++;
    }
        /* fall through */
    case 20:
        if (__MCMNG.dataui < (unsigned int)fsmc_DataWriteFunc__wtofs) {   /* all written (wtofs counts the marker) */
            __MCMNG.transno = 0x16;
            return 0;
        }
        fsmc_DataWriteFunc__wsize = (int)_fsmc_SetWriteData((unsigned int *)&fsmc_DataWriteFunc__wtofs);
        __MCMNG.count = 0;
        __MCMNG.transno++;
        /* fall through */
    case 21:
        if (sceMcWrite(fd, s_pWkBuff != 0 ? (void *)s_pWkBuff : (void *)rddata, fsmc_DataWriteFunc__wsize) != 0)
            return fsmc_dw_issue_fail();
        __MCMNG.transno = 0x13;
        return 0;
    case 22:
        if (sceMcClose(fd) != 0) return fsmc_dw_issue_fail();
        __MCMNG.transno++;
        return 0;
    case 23: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        __MCMNG.funcno = 0;   /* before the result test (0x1429bc): a failed close is abandoned below */
        if (r < 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.messno = 0x3d;
                return -8;
            }
            __MCMNG.count++;
            __MCMNG.transno--;
            return 0;   /* funcno is 0: FuncCheck idles, the retry never runs */
        }
        __MCMNG.messno = 0x3e;
        return 1;
    }
    case 0x64:   /* error cleanup: messno already set */
        if (sceMcSync(1, 0, &res) == 0) return 0;
        if (fd < 0) {   /* dead: fd starts at 0 */
            __MCMNG.funcno = 0;
            __MCMNG.messno = 0x3d;
            return __MCMNG.errorno < 0 ? __MCMNG.errorno : -0x3e7;
        }
        __MCMNG.count = 0;
        __MCMNG.transno++;
        return 0;
    case 0x65:
        if (sceMcClose(fd) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.funcno = 0;
                __MCMNG.messno = 0x3d;
                return -0x3e7;
            }
            __MCMNG.count++;
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 0x66:   /* res ignored */
        if (sceMcSync(1, 0, &res) == 0) return 0;
        __MCMNG.funcno = 0;
        __MCMNG.messno = 0x3d;
        return __MCMNG.errorno < 0 ? __MCMNG.errorno : -0x3e7;
    default:
        return 0;
    }
}

/* 0x142b70: funcno 4. State 0 only arms the machine (returns 0, no fall-through); 1 issues sceMcGetDir (3 tries);
 * 2 polls the result: >= 0 = entry count (getdir), < 0 retried twice, then messno 3 (-2 = unformatted) / 0x64 */
static int fsmc_GetDirFunc(void)
{
    PS2_FRAME(0x20);
    PS2_STACK(int, res, 0x1c);
    int port = __MCMNG.portno;
    int slot = __MCMNG.slotno;
    switch (__MCMNG.transno) {
    case 0:
        __MCMNG.transno = __MCMNG.transno + 1;
        __MCMNG.count = 0;
        return 0;
    case 1:
        if (sceMcGetDir(port, slot, __MCMNG.name, __MCMNG.dataui, (int)__MCMNG.dataui2, __MCMNG.table) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.funcno = 0;
                __MCMNG.messno = 0x64;
                return -1;
            }
            __MCMNG.count++;
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 2: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        if (r >= 0) {
            __MCMNG.funcno = 0;
            __MCMNG.messno = 0;
            __MCMNG.getdir = r;
            return 1;
        }
        if (__MCMNG.count == 2) {
            __MCMNG.messno = r == -2 ? 3 : 0x64;
            return r;   /* funcno kept: FuncCheck returns -4 */
        }
        __MCMNG.count++;
        __MCMNG.transno--;
        return 0;
    }
    default:   /* transno >= 3: never set */
        if (sceMcSync(1, 0, &res) != 0) {
            __MCMNG.funcno = 0;
            __MCMNG.messno = 0x64;
            return -1;
        }
        return 0;
    }
}

/* 0x142d50: funcno 3. State 0 arms the machine and falls into state 1 (sceMcGetInfo into type / free / format, 3
 * tries); 2 polls: result[port][slot] = result, >= 0 -> messno = _fsmc_InfoErrMess (0 = usable card -> 1, else -10);
 * results below -10 retried twice (back to state 1); -1 .. -10 (and the third failure): messno from _fsmc_InfoErrMess,
 * -10 */
static int fsmc_GetFileInfoFunc(void)
{
    PS2_FRAME(0x40);
    PS2_STACK(int, res, 0x3c);
    int port = __MCMNG.portno;
    int slot = __MCMNG.slotno;
    switch (__MCMNG.transno) {
    case 0:
        __MCMNG.transno = __MCMNG.transno + 1;
        __MCMNG.count = 0;
        /* fall through */
    case 1:
        if (sceMcGetInfo(port, slot, &MC_TYPE(port, slot), &MC_FREE(port, slot), &MC_FORMAT(port, slot)) != 0) {
            if (__MCMNG.count == 2) {
                __MCMNG.messno = 0x64;
                return -10;   /* funcno kept */
            }
            __MCMNG.count++;
            return 0;
        }
        __MCMNG.transno++;
        return 0;
    case 2: {
        if (sceMcSync(1, 0, &res) == 0) return 0;
        int r = res;
        MC_RESULT(port, slot) = r;
        if (r >= 0) {
            __MCMNG.funcno = 0;
            __MCMNG.messno = _fsmc_InfoErrMess(r);
            return __MCMNG.messno != 0 ? -10 : 1;
        }
        if (r < -10 && __MCMNG.count != 2) {   /* only results below -10 retry (0x142e74; -1 .. -10 do not) */
            __MCMNG.count++;
            __MCMNG.transno--;
            return 0;
        }
        __MCMNG.messno = _fsmc_InfoErrMess(r);
        return -10;
    }
    default:   /* transno >= 3: never set */
        if (sceMcSync(1, 0, &res) != 0) {
            __MCMNG.funcno = 0;
            __MCMNG.messno = 0x64;
            return -10;
        }
        return 0;
    }
}

/* 0x142f50: checks and copies the blocks of the last read (work buffer: s_WkKByte blocks, else one in rddata) to
 * datahd + rdofs. Block = payload (0x3fb in block 0, 0x3ff after) + checksum byte. -1 = checksum mismatch, 1 = short
 * block (finished), 0 = more blocks (rdofs advanced) */
static int _fsmc_GetReadData(int *prdofs, int datasize)
{
    PS2_FRAME(0x90);
    unsigned char *pbuff;
    int num;
    if (s_pWkBuff != 0) {
        pbuff = (unsigned char *)HOSTPTR(s_pWkBuff);
        num = (int)s_WkKByte;
    } else {
        pbuff = rddata;
        num = 1;
    }
    int rdofs = *prdofs;
    for (int i = 0; i < num; i++) {
        int readmax = rdofs == 0 ? 0x3ff - 4 : 0x3ff;
        int remain = (int)((unsigned int)datasize - (unsigned int)rdofs);
        int n = remain > readmax ? readmax : remain;
        if (n != 0) {
            unsigned char chk = 0;
            for (int j = 0; j < n; j++) chk = (unsigned char)(chk + (unsigned char)~pbuff[j]);
            if (pbuff[n] != chk) return -1;
            if (__MCMNG.dataui < (unsigned int)datasize) n = (int)(__MCMNG.dataui - (unsigned int)rdofs);
            else n = remain;
            if (n > readmax) n = readmax;
            memcpy((char *)HOSTPTR(__MCMNG.datahd) + rdofs, pbuff, (unsigned int)n);
            if (n < readmax) return 1;
        }
        pbuff += n + 1;   /* n == 0 still skips one byte */
        rdofs += n;
    }
    *prdofs = rdofs;
    return 0;
}

/* 0x1431c0: builds the next blocks of the save into the work buffer (s_WkKByte - 1 blocks) or rddata (one block):
 * block 0 = [u32 dataui][<= 0x3fb payload][checksum], later blocks [<= 0x3ff payload][checksum]; a short (last) block
 * is followed by the marker byte 1 (which *pwtofs counts); every block padded to 32 bytes. Returns the bytes built. */
static unsigned int _fsmc_SetWriteData(unsigned int *pwtofs)
{
    PS2_FRAME(0xb0);
    unsigned int total = 0;
    unsigned char *buf;
    unsigned int len;
    int nblk;
    if (s_pWkBuff != 0) {
        buf = (unsigned char *)HOSTPTR(s_pWkBuff);
        len = s_WkKByte << 10;
        nblk = (int)s_WkKByte - 1;   /* one block less than the buffer holds (0x143208) */
    } else {
        buf = rddata;
        len = 0x420;
        nblk = 1;
    }
    memset(buf, 0, len);
    unsigned int ofs = *pwtofs;
    for (int i = 0; i < nblk; i++) {
        unsigned int size = __MCMNG.dataui;
        int n = (int)(size - ofs);   /* signed: ofs > dataui would copy a huge length (unreachable) */
        int hdr;
        if (ofs == 0) {
            if (n >= 0x3fb) n = 0x3fb;
            *(unsigned int *)buf = size;
            hdr = 4;
        } else {
            if (n >= 0x3ff) n = 0x3ff;
            hdr = 0;
        }
        int pos = hdr + n;
        if (n != 0) {
            unsigned char *p = buf + hdr;
            memcpy(p, (char *)HOSTPTR(__MCMNG.datahdc) + ofs, (unsigned int)n);   /* datahdc re-read every block */
            unsigned char chk = 0;
            for (int j = 0; j < n; j++) chk = (unsigned char)(chk + (unsigned char)~p[j]);
            buf[pos++] = chk;
        }
        if (n < (ofs == 0 ? 0x3fb : 0x3ff)) {   /* short block: marker, ends the loop */
            buf[pos++] = 1;
            ofs++;
            i = nblk;
        }
        if (pos & 0x1f) pos = (pos & ~0x1f) + 0x20;
        ofs += (unsigned int)n;
        total += (unsigned int)pos;
        buf += pos;
    }
    *pwtofs = ofs;
    return total;
}

/* 0x143480: sceMcOpen / Sync result -> messno (only messno is written) */
static void _fsmc_OpenErrMess(int result)
{
    if (result >= 0) __MCMNG.messno = 0;
    else if (result == -2) __MCMNG.messno = 3;
    else if (result == -3) __MCMNG.messno = 0x1e;
    else if (result == -4) __MCMNG.messno = 0x64;
    else if (result == -5) __MCMNG.messno = 0x64;
    else if (result == -7) __MCMNG.messno = 0x64;
    else if (result < -10) __MCMNG.messno = 1;
    else __MCMNG.messno = 0x64;
}

/* 0x143540: GetInfo result -> messno for the queued port / slot. 0 = formatted PS2 card, 1 = no / PS1 card,
 * 2 = new formatted card, 3 / 4 = unformatted, 0x64 = error */
static int _fsmc_InfoErrMess(int result)
{
    int port = __MCMNG.portno;
    int slot = __MCMNG.slotno;
    if (result >= 0) {
        if (MC_FORMAT(port, slot) == 0) return MC_TYPE(port, slot) == 3 ? 1 : 3;
        return MC_TYPE(port, slot) == 3 ? 1 : 0;
    }
    if (result == -1) return MC_TYPE(port, slot) == 3 ? 1 : 2;
    if (result == -2) return MC_TYPE(port, slot) == 3 ? 1 : 4;
    if (result < -10 || result == -5) return 1;
    return 0x64;
}

/* 0x143680: card info as a 6-step sub-machine driven by its caller (prno = step, returns the next step; 0 = done OK).
 * 0 / 2 / 4 issue sceMcGetInfo (refused: skip to the next issue step, step 4 -> -0x64); 1 / 3 poll (nonzero result
 * returned as is); 5 polls and stores result[port][slot]. Jump table 0x271640; prno >= 6 (unsigned) is returned */
static int _fsmc_GetInfo(int port, int slot, int *type, int *free, int *format, int prno)
{
    PS2_FRAME(0x50);
    PS2_STACK(int, res, 0x4c);
    switch ((unsigned int)prno) {
    case 0:
    case 2:
    case 4:
        if (sceMcGetInfo(port, slot, type, free, format) == 0) return prno + 1;
        if (prno == 4) return -0x64;
        return prno + 2;
    case 1:
    case 3:
        if (sceMcSync(1, 0, &res) == 0) return prno;
        if (res == 0) return prno + 1;
        return res;
    case 5:
        if (sceMcSync(1, 0, &res) == 0) return 5;
        MC_RESULT(port, slot) = res;
        return res;
    default:
        return prno;
    }
}
