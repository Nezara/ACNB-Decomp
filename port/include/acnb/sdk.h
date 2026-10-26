/* Sony SDK / newlib functions and data of the main ELF (no DWARF) that decompiled code uses. Addresses:
 * symbols/sdk_names.tsv. Natives live in port/lib/sdk/ (one file per library); in the hybrid build
 * runtime/bridge_sdk.c registers them at their PS2 addresses (so interpreted callers use them too) and provides
 * thunks into the original code for the rest. In the source port the rest are missing until the platform layer
 * (phase 3) or the render detour (phase 2) replaces them. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/* libvu0 (lib/sdk/libvu0.cpp, native) */
void  sceVu0ApplyMatrix(float *v0, float (*m0)[4], float *v1);
void  sceVu0MulMatrix(float (*m0)[4], float (*m1)[4], float (*m2)[4]);
void  sceVu0OuterProduct(float *v0, float *v1, float *v2);
float sceVu0InnerProduct(float *v0, float *v1);
void  sceVu0Normalize(float *v0, float *v1);
void  sceVu0TransposeMatrix(float (*m0)[4], float (*m1)[4]);
void  sceVu0InversMatrix(float (*m0)[4], float (*m1)[4]);
void  sceVu0DivVector(float *v0, float *v1, float q);
void  sceVu0DivVectorXYZ(float *v0, float *v1, float q);
void  sceVu0InterVector(float *v0, float *v1, float *v2, float t);
void  sceVu0AddVector(float *v0, float *v1, float *v2);
void  sceVu0SubVector(float *v0, float *v1, float *v2);
void  sceVu0MulVector(float *v0, float *v1, float *v2);
void  sceVu0ScaleVector(float *v0, float *v1, float s);
void  sceVu0TransMatrix(float (*m0)[4], float (*m1)[4], float *tv);
void  sceVu0CopyVector(float *v0, float *v1);
void  sceVu0CopyMatrix(float (*m0)[4], float (*m1)[4]);
void  sceVu0FTOI4Vector(int *v0, float *v1);
void  sceVu0FTOI0Vector(int *v0, float *v1);
void  sceVu0ITOF4Vector(float *v0, int *v1);
void  sceVu0ITOF0Vector(float *v0, int *v1);
void  sceVu0UnitMatrix(float (*m0)[4]);
void  sceVu0RotMatrixZ(float (*m0)[4], float (*m1)[4], float rz);
void  sceVu0RotMatrixX(float (*m0)[4], float (*m1)[4], float rx);
void  sceVu0RotMatrixY(float (*m0)[4], float (*m1)[4], float ry);
void  sceVu0RotMatrix(float (*m0)[4], float (*m1)[4], float *rot);
void  sceVu0RotTransPers(int *v0, float (*m0)[4], float *v1, int mode);
void  sceVu0ViewScreenMatrix(float (*m)[4], float scrz, float ax, float ay, float cx, float cy, float zmin, float zmax,
                             float nearz, float farz);   /* farz: MW passes the 9th float at the caller's sp + 0 */

/* newlib libm (lib/sdk/libm.cpp, native, bit-exact; ps2_ prefix avoids the host <math.h> names) */
float ps2_fabsf(float x);
float ps2_atanf(float x);
float ps2_atan2f(float y, float x);
float ps2_ieee754_atan2f(float y, float x);
float ps2_tanf(float x);   /* runtime/bridge_sdk.c: direct call of ps2_native_tanf (MIPS 0x12c9d8) */
float ps2_sinf(float x);   /* ps2_native_sinf (MIPS 0x12c8f0) */
float ps2_cosf(float x);   /* ps2_native_cosf (MIPS 0x12c718) */
float ps2_native_sinf(float x);   /* lib/sdk/libm.cpp natives (the source port's ps2_sinf / cosf / tanf) */
float ps2_native_cosf(float x);
float ps2_native_tanf(float x);
/* double libm (lib/sdk/libm.cpp, fdlibm over the soft-double routines below; raw IEEE bits in / out) */
unsigned long long ps2_pow(unsigned long long x, unsigned long long y);           /* 0x12ca50 pow (tail call) */
unsigned long long ps2_ieee754_pow(unsigned long long x, unsigned long long y);   /* 0x129f08 */
unsigned long long ps2_ieee754_sqrt(unsigned long long x);                        /* 0x12ae58 (pow, y == 0.5) */
unsigned long long ps2_fabs(unsigned long long x);                                /* 0x12c450 */
unsigned long long ps2_scalbn(unsigned long long x, int n);                       /* 0x118908 */
unsigned long long ps2_copysign(unsigned long long x, unsigned long long y);      /* 0x118838 */
/* libgcc soft double (lib/sdk/libgcc.cpp, native, bit-exact port of fp-bit: round toward zero, no denormals,
 * overflow -> Inf). Raw IEEE bit patterns in / out; natives use the ps2_d* wrappers below, never host double
 * operators. */
unsigned long long ps2_adddf3(unsigned long long a, unsigned long long b);   /* 0x11d8c8 */
unsigned long long ps2_subdf3(unsigned long long a, unsigned long long b);   /* 0x11d920 */
unsigned long long ps2_muldf3(unsigned long long a, unsigned long long b);   /* 0x11d988 */
unsigned long long ps2_divdf3(unsigned long long a, unsigned long long b);   /* 0x11dc10 */
int  ps2_cmpdf2(unsigned long long a, unsigned long long b);                 /* 0x11de70: <0 / 0 / >0, NaN -> 1 */
unsigned long long ps2_floatsidf(long long a0);           /* 0x11dec0: int (the 64-bit register) -> double */
unsigned long long ps2_floatdidf(long long a);            /* 0x11d060: int64 -> double */
int  ps2_fixdfsi(unsigned long long a);                   /* 0x11df78: signed, saturating (_dtoa_r only) */
unsigned int ps2_fixunsdfsi(unsigned long long a);        /* 0x11e0a0: what MWCC's (int)d calls; negative -> 0 */
unsigned long long ps2_fixunsdfdi(unsigned long long a);  /* 0x11d108: double -> u64 */
unsigned long long ps2_fixunssfdi(unsigned int fbits);    /* 0x11d220 ("__fixdfdi"): float -> u64, negative -> 0 */
unsigned long long ps2_extendsfdf2(unsigned int fbits);   /* 0x11d490: float -> double */
unsigned int ps2_truncdfsf2(unsigned long long a);        /* 0x11e048: double -> float, truncated */
unsigned long long ps2_udivdi3(unsigned long long n, unsigned long long d);   /* 0x11c578 */
unsigned long long ps2_umoddi3(unsigned long long n, unsigned long long d);   /* 0x11caf8 */
/* host double = storage only (bit patterns: x87 loads quiet a signalling NaN, which fp-bit's pack does anyway) */
static inline unsigned long long ps2_dbits(double d) { unsigned long long u; __builtin_memcpy(&u, &d, 8); return u; }
static inline double ps2_bitsd(unsigned long long u) { double d; __builtin_memcpy(&d, &u, 8); return d; }
static inline unsigned int ps2_fbits(float f) { unsigned int u; __builtin_memcpy(&u, &f, 4); return u; }
static inline float ps2_bitsf(unsigned int u) { float f; __builtin_memcpy(&f, &u, 4); return f; }
static inline double ps2_dadd(double a, double b) { return ps2_bitsd(ps2_adddf3(ps2_dbits(a), ps2_dbits(b))); }
static inline double ps2_dsub(double a, double b) { return ps2_bitsd(ps2_subdf3(ps2_dbits(a), ps2_dbits(b))); }
static inline double ps2_dmul(double a, double b) { return ps2_bitsd(ps2_muldf3(ps2_dbits(a), ps2_dbits(b))); }
static inline double ps2_ddiv(double a, double b) { return ps2_bitsd(ps2_divdf3(ps2_dbits(a), ps2_dbits(b))); }
static inline int    ps2_dcmp(double a, double b) { return ps2_cmpdf2(ps2_dbits(a), ps2_dbits(b)); }   /* a < b: dcmp < 0 */
static inline double ps2_dfabs(double a) { return ps2_bitsd(ps2_dbits(a) & ~(1ull << 63)); }   /* newlib fabs */
static inline double ps2_dpow(double x, double y) { return ps2_bitsd(ps2_pow(ps2_dbits(x), ps2_dbits(y))); }   /* libm pow */
static inline double ps2_f2d(float f) { return ps2_bitsd(ps2_extendsfdf2(ps2_fbits(f))); }
static inline float  ps2_d2f(double d) { return ps2_bitsf(ps2_truncdfsf2(ps2_dbits(d))); }
static inline double ps2_i2d(int i) { return ps2_bitsd(ps2_floatsidf(i)); }
static inline int    ps2_d2i(double d) { return (int)ps2_fixunsdfsi(ps2_dbits(d)); }   /* MWCC (int)d: negative -> 0 */
static inline unsigned int ps2_d2u(double d) { return ps2_fixunsdfsi(ps2_dbits(d)); }
static inline unsigned long long ps2_f2u64(float f) { return ps2_fixunssfdi(ps2_fbits(f)); }

void ps2_srand(unsigned int seed);          /* newlib srand / rand (lib/sdk/newlib.cpp) */
int  ps2_rand(void);

/* libdma (lib/sdk/libdma.cpp, native). A sceDmaChan * is the channel's DMAC register block (see port.h ps2_dma_chan_*). */
void *sceDmaGetChan(int id);               /* sceDmaChan * of DMA channel id (0..9), NULL otherwise */
int  sceDmaSend(void *d, void *addr);      /* chain-mode transfer from the tag at addr; returns the CHCR written */
int  sceDmaSendN(void *d, void *addr, int size);   /* normal-mode transfer of size qwords; returns the CHCR written */
int  sceDmaSync(void *d, int mode, int timeout);   /* mode 1: CHCR.STR busy flag; any other mode: wait, 0. timeout ignored */
typedef struct sceDmaEnv {               /* D_CTRL / D_PCR / D_SQWC / D_RBOR / D_RBSR setup (0x14 bytes) */
    unsigned char sts, std, mfd, rcycle;   /* channel ids (< 10) / release cycle (< 7, 0 = off) */
    unsigned short express, notify;        /* D_PCR: CPCOND enables (hi), CDE (lo) */
    unsigned short sqwc, tqwc;             /* D_SQWC: SQWC (lo), TQWC (hi) */
    void *rbadr;                           /* D_RBOR */
    unsigned int rbmsk;                    /* D_RBSR */
} sceDmaEnv;
extern unsigned char sdk_dma_env[0x14];    /* 0x280360 (runtime/nodwarf_abs.s): PutEnv's copy, read by sceDmaGetEnv */
int  sceDmaReset(int mode);                /* clears the channels + D_STAT, default env; mode 1 = DMAE on; returns the old DMAE */
int  sceDmaPutEnv(sceDmaEnv *env);         /* 0, or -1..-4 for an out-of-range sts / std / mfd / rcycle */

/* libgraph (lib/sdk/libgraph.cpp, native) */
typedef struct sceGsGParam {
    short inter, omode, ffmode, version;   /* ResetGraph's arguments; version = GS revision */
    unsigned int vsync_flag, vsync_id;     /* SetVSyncFlag handler (never set by the game) */
} sceGsGParam;
extern sceGsGParam sdk_gparam;             /* 0x257c70 (runtime/nodwarf_abs.s): shared with the interpreted SDK code */
sceGsGParam *sceGsGetGParam(void);
int  sceGsSyncPath(int mode, unsigned short timeout);   /* 0: wait for the drawing paths; else their busy mask */
int  sceGsSetDefLoadImage(void *lp, short dbp, short dbw, short dpsm, short x, short y, short w, short h);
int  sceGsExecLoadImage(void *lp, void *srcaddr);   /* lp: 0x60-byte sceGsLoadImage packet */
int  sceGsSetDefDrawEnv(void *draw, short psm, short w, short h, short ztest, short zpsm);   /* draw: sceGsDrawEnv1 */
int  sceGsPutDrawEnv(void *giftag);         /* sends the GIF tag + the draw env after it (DMA channel 2) */
void sceGsSetHalfOffset(void *draw, short centerx, short centery, short field);   /* draw: sceGsDrawEnv1 */
void FlushCache(int operation);             /* libkernl syscall (no data cache to flush: empty) */
void sceGsPutDispEnv(void *disp);           /* sceGsDispEnv (0x28) -> PMODE / SMODE2 / DISPFB2 / DISPLAY2 / BGCOLOR */
int  sceGsSwapDBuff(void *db, int id);      /* sceGsDBuff: PutDispEnv(disp[id & 1]) + PutDrawEnv of that buffer */
int  sceGsSetDefStoreImage(void *sp, short sbp, short sbw, short spsm, short x, short y, short w, short h);
int  sceGsExecStoreImage(void *sp, void *dst);   /* sp: 0x70-byte sceGsStoreImage; GS -> dst readback, 0 / -1 */
void sceGsSetDefDispEnv(void *disp, short psm, short w, short h, short dx, short dy);   /* disp: sceGsDispEnv */
int  sceGsSetDefClear(void *clear, short ztest, short x, short y, short w, short h, int r, int g, int b, int a,
                      unsigned int z);       /* clear: sceGsClear (6 A+D qwords); b / a / z on the PS2 stack */
void sceGsSetDefDBuff(void *db, short psm, short w, short h, short ztest, short zpsm, short clear);   /* sceGsDBuff */
/* vblank wait; returns the field (inter == 1). Native in the source port only: the hybrid calls the original
 * (runtime/bridge_sdk.c), whose wait depends on the interpreter's interrupt delivery */
int  sceGsSyncV(int mode);

/* GS bring-up (native): mode 0 = reset + GParam + display mode, 1 = CSR FLUSH, 5 = GParam + display mode only */
void sceGsResetGraph(short mode, short inter, short omode, short ffmode);
void sceGsResetPath(void);                 /* VIF1 / VU1 / GIF path reset + VIF1 default codes */
/* libkernl syscall wrappers ResetGraph calls (hybrid: thunks into the original; source port: SetGsCrt no-op in
 * libgraph.cpp, the Intc pair only in ResetGraph's dead vsync branch) */
int  SetGsCrt(short inter, short omode, short ffmode);
int  DisableIntc(int cause);
int  RemoveIntcHandler(int cause, int id);

/* libvifpk (lib/sdk/libvifpk.cpp, native): VIF1 packet writer (FMV player disp.c only) */
typedef struct struct_3497d839 sceVif1Packet;   /* pCurrent +0, pBase +4, pDmaTag +8, pGifTag +0xc */
void sceVif1PkInit(sceVif1Packet *pk, void *base);   /* base: 16-aligned packet buffer */
void sceVif1PkReset(sceVif1Packet *pk);
unsigned int *sceVif1PkTerminate(sceVif1Packet *pk);
void sceVif1PkCnt(sceVif1Packet *pk, unsigned int vif0, unsigned int vif1, unsigned int qwc);
void sceVif1PkRef(sceVif1Packet *pk, void *addr, unsigned int qwc, unsigned int vif0, unsigned int vif1, unsigned int flags);
void sceVif1PkEnd(sceVif1Packet *pk, unsigned int vif0, unsigned int vif1, unsigned int qwc);
void sceVif1PkReserve(sceVif1Packet *pk, unsigned int n);
void sceVif1PkOpenGifTag(sceVif1Packet *pk, const void *tag);   /* the original takes the GIF tag by value (128-bit a1) */
void sceVif1PkCloseGifTag(sceVif1Packet *pk);
void sceVif1PkAddGsAD(sceVif1Packet *pk, unsigned int reg, unsigned long long data);

/* not native yet */
int  sceSdRemote(int arg, int command, int a2);
int  sceSdRemoteInit(void);
int  sceSifInitIopHeap(void);   /* libkernl (mvsr IOP re-init) */
int  sceCdSearchFile(void *fp, const char *name);   /* fp: sceCdlFILE (0x24 bytes) */
int  sceCdGetError(void);
/* libcdvd / fileio calls of lib/cdvd.c and lib/overlay.c (hybrid: thunks into the HLE-hooked originals, so the
 * ACNB_DISC_TRACE lines stay those of the original; source port: lib/sdk/libcdvd.cpp over port/platform ps2_file) */
int  sceCdRead(unsigned int lsn, unsigned int sectors, void *buf, void *mode);   /* mode: sceCdRMode (4 bytes); 1 = ok */
int  sceCdSync(int mode);                    /* 0 = idle (frozen HLE: always 0) */
int  sceCdInit(int mode);
int  sceCdMmode(int media);
int  sceOpen(const char *name, int flags);
int  sceRead(int fd, void *buf, int n);
int  sceLseek(int fd, int offset, int whence);
int  sceClose(int fd);
int  sceGetstat(const char *name, void *buf);   /* buf: sce_stat (0x40); hdd.c hdd_CheckFile (unreached: no HDD) */
/* libmc calls of lib/fs/fslibmc.c (hybrid: thunks into the HLE-hooked originals, runtime/mc.c; source port:
 * lib/sdk/libmc.cpp over port/platform ps2_mc). table: sceMcTblGetDir[maxent] (64 bytes each) */
int  sceMcInit(void);                                                              /* 0x12ca80 */
int  sceMcGetInfo(int port, int slot, int *type, int *free, int *format);          /* 0x12d5d8 */
int  sceMcSync(int mode, int *cmd, int *result);                                   /* 0x12d4a0 */
int  sceMcGetDir(int port, int slot, const char *name, unsigned int mode, int maxent, void *table);   /* 0x12d7c8 */
int  sceMcOpen(int port, int slot, const char *name, int mode);                    /* 0x12ce50 */
int  sceMcClose(int fd);                                                           /* 0x12cfb0 */
int  sceMcSeek(int fd, int offset, int origin);                                    /* 0x12d068 */
int  sceMcRead(int fd, void *buff, int size);                                      /* 0x12d1d0 */
int  sceMcWrite(int fd, const void *buff, int size);                               /* 0x12d2e8 */
int  sceMcMkdir(int port, int slot, const char *name);                             /* 0x12cf78 (= sceMcOpen mode 0x40) */
int  sceMcFormat(int port, int slot);                                              /* 0x12dad8 */
int  sceMcUnformat(int port, int slot);                                            /* 0x12e098 */
int  sceMcChdir(int port, int slot, const char *newDir, char *currentDir);         /* 0x12d998 */
/* libpad calls of lib/fs/fslibpa2.c (hybrid: thunks into the HLE-hooked originals of runtime/pad.c, so the
 * ACNB_PAD_TRACE lines stay those of the original; source port: missing until the platform libpad, phase 3 step 3) */
int  scePadInit(int mode);
int  scePadPortOpen(int port, int slot, void *addr);   /* addr: 0x100-byte DMA work area (64-aligned) */
int  scePadGetState(int port, int slot);
int  scePadGetReqState(int port, int slot);
int  scePadInfoMode(int port, int slot, int term, int offs);
int  scePadInfoAct(int port, int slot, int actno, int term);
int  scePadInfoPressMode(int port, int slot);
int  scePadSetMainMode(int port, int slot, int offs, int lock);
int  scePadEnterPressMode(int port, int slot);
int  scePadExitPressMode(int port, int slot);
int  scePadSetActAlign(int port, int slot, const unsigned char *data);   /* 6 bytes */
int  scePadRead(int port, int slot, unsigned char *rdata);                /* 32 bytes; returns the length read */
int  scePadSetActDirect(int port, int slot, const unsigned char *data);  /* 6 bytes */
/* libkernl SIF / IOP boot calls of main/iop.c (hybrid: thunks into the HLE-hooked originals; source port: boot-slice
 * stubs). Pointer arguments are guest addresses in the hybrid (the hooks read / write PS2 memory). */
int  sceDevctl(const char *name, int cmd, void *arg, unsigned int arglen, void *buf, unsigned int buflen);
void sceSifInitRpc(unsigned int mode);
int  sceSifLoadFileReset(void);
int  sceFsReset(void);
int  sceSifRebootIop(const char *img);       /* 1 = ok */
int  sceSifSyncIop(void);                    /* 1 = done */
int  sceSifMInitRpc(unsigned int mode);
void sceSifMExitRpc(void);
void *sceSifAllocIopHeap(unsigned int size); /* IOP address, 0 = failed */
int  sceSifFreeIopHeap(void *addr);
unsigned int sceSifSetDma(void *sdd, int len);   /* sdd: sceSifDmaData[len] (struct_f14f5dcc) */
int  sceSifLoadStartModuleBuffer(const void *ptr, int arg_len, const char *args, int *result);
int  sceSifLoadStartModule(const char *path, int arg_len, const char *args, int *result);
/* libkernl SIF RPC (lib/fs/fslibsd.c sound transport). cd / sd / qd / rd: the guest _sif_client_data / _sif_serve_data /
 * _sif_queue_data objects (nodwarf fssd_cd / fssd_sd / fssd_qd). sceSifCallRpc's 9th argument goes to the caller's sp + 0
 * (the thunk stores it there, the original's `sd`: the caller needs its PS2_FRAME). sceSifRpcLoop never returns (hle.c
 * serves the queue and sleeps the calling thread). */
int  sceSifBindRpc(void *cd, unsigned int rpc_number, unsigned int mode);
int  sceSifCallRpc(void *cd, unsigned int fno, unsigned int mode, void *send, int ssize, void *recv, int rsize,
                   void *endfunc, void *endpara);
int  sceSifCheckStatRpc(void *rd);
void sceSifSetRpcQueue(void *qd, int tid);
void sceSifRegisterRpc(void *sd, unsigned int sid, void *(*func)(unsigned int, void *, int), void *buf,
                       void *(*cfunc)(unsigned int, void *, int), void *cbuf, void *qd);
void *sceSifRemoveRpc(void *sd, void *qd);
void *sceSifRemoveRpcQueue(void *qd);
void sceSifRpcLoop(void *qd);
int  sceSifDmaStat(unsigned int id);         /* < 0 = transfer done */
int  AddIntcHandler(int cause, int (*handler)(void), int next);   /* libkernl syscalls 0x10 / 0x14 */
int  EnableIntc(int cause);
void SyncDCache(void *start, void *end);   /* libkernl (no-op for the source port) */
/* libkernl thread / semaphore syscall wrappers (main/main.c, main/thretask.c; ps2_ prefix avoids the Win32 names).
 * param: the kernel's SemaParam / ThreadParam block in PS2 memory (the kernel reads it, stale words included). */
int  ps2_GetThreadId(void);                          /* syscall 0x2f */
int  ps2_ChangeThreadPriority(int tid, int prio);    /* syscall 0x29 */
int  ps2_CreateSema(void *param);                    /* syscall 0x40 */
int  ps2_CreateThread(void *param);                  /* syscall 0x20 */
int  ps2_StartThread(int tid, void *arg);            /* syscall 0x22 */
int  ps2_WaitSema(int sid);                          /* syscall 0x44 (blocks this thread) */
int  ps2_SignalSema(int sid);                        /* syscall 0x42 */
int  ps2_PollSema(int sid);                          /* syscall 0x45 */
int  ps2_DeleteSema(int sid);                         /* syscall 0x41 */
void ps2_ExitDeleteThread(void);                     /* syscall 0x24 (does not return) */
int  ps2_TerminateThread(int tid);                   /* syscall 0x25 */
int  ps2_DeleteThread(int tid);                      /* syscall 0x21 */
int  DIntr(void);                                    /* libkernl: disable interrupts (returns the old state) */
int  EIntr(void);                                    /* libkernl: enable interrupts */
/* scePrintf (libkernl debug output) with no / two int arguments; fmt must be a guest address in the hybrid */
int  ps2_scePrintf(const char *fmt);
int  ps2_scePrintf_dd(const char *fmt, int v1, int v2);
int  ps2_scePrintf_sd(const char *fmt, const char *s, int v);   /* s: guest address too in the hybrid */
int  ps2_iSignalSema(int sid);                       /* syscall -0x43 (interrupt context) */
/* libcdvd: power-off callback (link/fseth.c). Hybrid: hle h_ret0 (never fires); source port: host_sdk.cpp, no power button */
void *sceCdPOffCallback(void (*func)(void *), void *addr);   /* 0x1100f0 */
/* libcdvd S-commands of the fseth power-off thread (unreached: no power-button event). sceCdPowerOff = S-cmd 0x21;
 * 0x111590 = S-cmd 0x1a (int, u_int *result), named sceCdAutoAdjustCtrl after the libcdvd command number (no symbol) */
int  sceCdPowerOff(int *stat);                                 /* 0x111408 */
int  sceCdAutoAdjustCtrl(int mode, unsigned int *result);      /* 0x111590 */
/* the rest of the libkernl fileio layer (hdd/libaio.c threadmain; unreached: no aio command is ever posted).
 * Hybrid: thunks into the HLE-hooked originals; source port: missing (boot-slice stubs) */
int  sceWrite(int fd, const void *buf, int n);
long long sceLseek64(int fd, long long offset, int whence);
int  sceChstat(const char *name, void *buf, unsigned int cbit);
int  sceRename(const char *oldname, const char *newname);
int  sceRemove(const char *name);
int  sceReadlink(const char *path, char *buf, unsigned int bufsize);
int  sceSymlink(const char *existing, const char *newname);
int  sceChdir(const char *name);
int  sceMkdir(const char *name, int mode);
int  sceRmdir(const char *name);
int  sceDopen(const char *name);
int  sceDclose(int fd);
int  sceDread(int fd, void *buf);
int  sceSync(const char *path, int flag);
int  sceFormat(const char *path, const char *blkdevname, void *arg, int arglen);
int  sceMount(const char *fsdevname, const char *blkdevname, int flag, void *arg, int arglen);
int  sceUmount(const char *fsdevname);
int  sceIoctl2(int fd, int request, void *arg, unsigned int arglen, void *buf, unsigned int buflen);
/* libdev VIF / VU resets and the boot-time IRX loader (main/main.c __InitPs2) */
int  sceDevVif0Reset(void);
int  sceDevVu0Reset(void);
int  sceDevVif1Reset(void);
int  sceDevVu1Reset(void);
long long _LoadEmbeddedIrx(int *result);   /* loads the embedded IOP modules; <0 = error, *result = module result */
void ps2_exit(int status);                 /* newlib exit */
/* misc region 0x13e740-0x13e8c0 (game code without a symbol): the Arena passcode generator. Picks one of four
 * encoders (pointer table 0x2597f0) by (k0 + k1 * k2 + k3) % 4 of the keycode bytes and returns its (s8) result.
 * Interpreted (menu/marena_result.c __MArenaResult_PlayPhaseLoad); in = stPasscodeInputData, out = stPasscodeOutputData */
signed char misc_0013e760(void *in, void *out);
/* newlib sprintf with one int argument (HUD number formats). Both builds format with lib/sdk/libc.cpp's port of
 * newlib's sprintf (ps2_native_sprintf; newlib's stdio state is set up lazily by the first call), called directly in
 * both builds: the arguments go into EABI vararg slots (hybrid: runtime/bridge_sdk.c, at the caller's sp - 0x30 like
 * the original; source port: lib/sdk/newlib.cpp). rcheck -Check sprintf sees only MIPS callers. */
int  ps2_sprintf_d(char *buf, const char *fmt, int v);
int  ps2_sprintf_s(char *buf, const char *fmt, const char *s);   /* newlib sprintf with one string argument */
int  ps2_sprintf_ss(char *buf, const char *fmt, const char *s1, const char *s2);   /* ... with two string arguments */
int  ps2_sprintf_dss(char *buf, const char *fmt, int v, const char *s1, const char *s2);   /* ... int + two strings */
int  ps2_sprintf_sd(char *buf, const char *fmt, const char *s, int v);   /* ... string + int */
int  ps2_sprintf_sdd(char *buf, const char *fmt, const char *s, int v1, int v2);   /* ... string + two ints */
int  ps2_sprintf_ddd(char *buf, const char *fmt, int v1, int v2, int v3);   /* ... three ints */
int  ps2_sprintf_sss(char *buf, const char *fmt, const char *s1, const char *s2, const char *s3);   /* ... three strings */
int  ps2_sprintf_ssss(char *buf, const char *fmt, const char *s1, const char *s2, const char *s3, const char *s4);   /* ... four strings */
/* ... 8 arguments (string, char, 5 strings, int; hdd.c TaskCreatePartition): slots 7-8 are the caller's stack
 * arguments (hybrid: written at the caller's sp + 0 / + 8, inside its PS2_FRAME) */
int  ps2_sprintf_scsssssd(char *buf, const char *fmt, const char *s1, int c, const char *s2, const char *s3,
                          const char *s4, const char *s5, const char *s6, int v);
int  ps2_printf_d(const char *fmt, int v);   /* newlib printf with one int argument (debug output) */
int  ps2_printf_dd(const char *fmt, int v1, int v2);   /* ... two int arguments */
int  ps2_printf_dddd(const char *fmt, int v1, int v2, int v3, int v4);   /* ... four int arguments */
int  ps2_printf_s(const char *fmt, const char *s);   /* ... one string argument (hybrid: s is a guest address) */
int  ps2_printf(const char *fmt);            /* ... with no argument (fmt: guest address in the hybrid) */
/* newlib malloc / free (main/sysinfo.c FullAllocAndFree). Hybrid: the originals (PS2 heap); source port: the host heap */
void *ps2_malloc(unsigned int size);
void ps2_free(void *p);
/* newlib qsort (cmp = GFN(comparator), called through gcall): the result order of an inconsistent comparator (mpart's
 * __MPART_TableQsort) depends on newlib's BSD algorithm, so both builds use lib/sdk/libc.cpp's port of it
 * (ps2_native_qsort, registered at 0x117d68); ps2_qsort calls it directly in both builds. */
void ps2_qsort(void *base, unsigned int n, unsigned int size, int (*cmp)(void *, void *));
void ps2_native_qsort(void *base, unsigned int n, unsigned int size, int (*cmp)(void *, void *));
/* newlib string / memory leaves (lib/sdk/libc.cpp, registered at their MIPS addresses for interpreted callers):
 * results bit-identical to the EE versions incl. overlapping memcpy / strcpy (forward copy) and strcmp / strncmp's
 * byte difference (host strcmp only guarantees the sign). Natives may keep the host functions where only the effect
 * on non-overlapping memory / the sign / == 0 is used. */
void *ps2_memcpy(void *dst, const void *src, unsigned int n);   /* 0x116938 */
void *ps2_memset(void *dst, int c, unsigned int n);             /* 0x116af0 */
unsigned int ps2_strlen(const char *s);                         /* 0x1193f8 */
int   ps2_strcmp(const char *s1, const char *s2);               /* 0x119198: (u8)*s1 - (u8)*s2 */
int   ps2_strncmp(const char *s1, const char *s2, unsigned int n);   /* 0x119530 */
char *ps2_strcpy(char *dst, const char *src);                   /* 0x1192e0 */
char *ps2_strncpy(char *dst, const char *src, unsigned int n);  /* 0x1196e8 */
int   ps2_toupper(int c);                                       /* 0x119b10 (negative c: bytes before _ctype_) */
int   ps2_abs(int x);                                           /* 0x1134a8 */
/* newlib sprintf chain (lib/sdk/libc.cpp, registered at the MIPS addresses for interpreted callers; the float
 * conversion cvt / _dtoa_r and the real-FILE paths are still the original): ap = the EABI vararg slots, 8 bytes each
 * (int / pointer in the low word, long / double 64-bit) */
struct ps2_FILE;
struct ps2_suio;
int   ps2_native_sprintf(char *str, const char *fmt, char *ap);                                 /* 0x118cd8 */
int   ps2_native_vsprintf(char *str, const char *fmt, char *ap);                                /* 0x11b5b8 */
int   ps2_native_vfprintf(struct ps2_FILE *fp, const char *fmt0, char *ap);                     /* 0x119c30 */
int   ps2_native__vfprintf_r(void *data, struct ps2_FILE *fp, const char *fmt0, char *ap);      /* 0x119ca8 */
void  ps2_native___sinit(void *s);                                                               /* 0x114eb0 */
void  ps2_native_std(struct ps2_FILE *fp, int flags, int file, void *ptr);                       /* 0x114ca0 */
void *ps2_native_localeconv(void);                                                               /* 0x115960 */
void *ps2_native__localeconv_r(void *data);                                                      /* 0x115928 */
int   ps2_native__mbtowc_r(void *r, int *pwc, const char *s, unsigned int n, void *state);        /* 0x116788 */
int   ps2_native___sprint(struct ps2_FILE *fp, struct ps2_suio *uio);                            /* 0x119b30 */
int   ps2_native___sfvwrite(struct ps2_FILE *fp, struct ps2_suio *uio);                          /* 0x115408 */
void *ps2_memmove(void *dst, const void *src, unsigned int n);                                   /* 0x1169e8 */

/* newlib data */
extern void *ps2_impure_ptr;               /* _impure_ptr (struct _reent *): rand's state is the u64 at +0xa8 */
extern const unsigned char ps2_ctype_[];    /* _ctype_ (+1 is the table indexed by the character): _U 1, _L 2, _N 4, _P 0x10 */

/* fontlib (DWARF-less FromSoftware font renderer "fslibfont", symbols/sdk_regions.tsv 0x1001e0-0x104ab0; names =
 * symtab fontlib_<addr>, proposed real names ). All groups (F1a, F1b, F2, F3) are
 * native (lib/sdk/fontlib.cpp, registered in runtime/bridge_sdk.c); drawFunc / char readers / tag ops are also stored
 * as code addresses (GFN). */
struct _FS_FONT_ST;
struct _FS_FONT_DATA_ST;
struct _FS_CCM_HEADER_ST;
struct _FS_FONT_RECT_ST;
struct _FS_FONT_DRAW_ST;
struct _FS_FONT_FORM_ST;
struct _FS_CCM_GLYPH_ST;
/* F1a natives */
int   fontlib_001004c0(void);                                          /* fsfont_GetTexNum */
int   fontlib_00100590(float *out);                                    /* fsfont_GetFontHeight (-5: no CCM) */
int   fontlib_001005e0(void);                                          /* fsfont_GetCharCode (-5: no CCM) */
int   fontlib_00100630(int slot, void *tex);                           /* FsFontSetTex */
int   fontlib_00100650(void *ccm);                                     /* FsFontSetCCM (_FS_CCM_HEADER_ST *) */
struct _FS_FONT_DATA_ST *fontlib_001006a0(struct _FS_FONT_DATA_ST *fd); /* fsfont_SetFontData: returns the old one */
int   fontlib_001006b0(struct _FS_FONT_DATA_ST *fd, struct _FS_CCM_HEADER_ST *ccm, int texNum);   /* fsfont_InitFontData */
unsigned int fontlib_00100760(unsigned int flags, int n);              /* fsfont_SizeFontData */
void *fontlib_00100780(void *font);                                    /* FsFontSelect (_FS_FONT_ST *): returns the previous font */
int   fontlib_001007f0(struct _FS_FONT_ST *font, struct _FS_CCM_HEADER_ST *ccm, int texNum);   /* FsFontInit */
void *fontlib_00100880(void);                                          /* FsFontEnd: returns the packet cursor */
int   fontlib_001009c0(void);                                          /* FsFontBegin */
int   fontlib_00100c10(unsigned int opt, int on);                      /* FsFontSetOption (ps2Info bit opt) */
int   fontlib_00100d30(void *dmabuf, unsigned int size);               /* FsFontSetDmaBuffer */
unsigned int fontlib_00100d90(int n, int flagA);                       /* FsFontGetDmaBufferSize */
void  fontlib_00101d70(void);                                          /* fsfont_KickDma (D2) */
void  fontlib_001020a0(void);                                          /* fsfont_BeginPacket */
int   fontlib_00102950(float *sx, float *sy);                          /* fsfont_GetScale */
int   fontlib_00102990(float *x, float *y);                            /* fsfont_GetPos */
int   fontlib_00102a00(float h);                                       /* fsfont_SetLineHeight */
int   fontlib_00102b30(unsigned int color);                            /* FsFontSetColor */
int   fontlib_00102b60(unsigned int flag);                             /* FsFontSetAlign (b4-5 x, b6-7 y) */
int   fontlib_00102bc0(float sx, float sy);                            /* FsFontSetScale */
int   fontlib_00102c60(unsigned int mode, float x, float y);           /* FsFontSetPos (b0 relative, b1 x, b2 y) */
int   fontlib_00102cc0(void (*fn)(struct _FS_FONT_DRAW_ST *));         /* fsfont_SetDrawFunc */
int   fontlib_00102cd0(void);                                          /* FsFontSetCharFunc */
struct _FS_FONT_DATA_ST *fontlib_00102d60(struct _FS_FONT_ST *font);   /* fsfont_SelectDrawState */
int   fontlib_00102e10(struct _FS_FONT_ST *font, struct _FS_CCM_HEADER_ST *ccm, int texNum);   /* fsfont_InitDrawState */
unsigned int fontlib_00103020(unsigned int flags);                     /* fsfont_SizeDrawState */
void  fontlib_00103130(void);                                          /* fsfont_InitCharClass */
void  fontlib_00103280(struct _FS_FONT_RECT_ST *rect, float left, float top, float right, float bottom);   /* fsfont_SetRect (F1b, ported with F1a) */
int   fontlib_00103640(int a0);                                        /* FsFontSetRubyAlign */
int   fontlib_00103670(unsigned int mask);                             /* FsFontAddTagFunc */
struct _FS_FONT_DATA_ST *fontlib_00103840(struct _FS_FONT_ST *font);   /* fsfont_SelectDefTag */
int   fontlib_001038b0(struct _FS_FONT_ST *font, struct _FS_CCM_HEADER_ST *ccm, int texNum);   /* fsfont_Setup */
unsigned int fontlib_00103a00(unsigned int flags);                     /* fsfont_SizePs2Info */
/* F1b natives */
int   fontlib_001001e0(unsigned short *dst, const unsigned char *src);  /* fsfont_Utf8ToUtf16 */
int   fontlib_00100340(unsigned char *dst, const unsigned short *src);  /* fsfont_Utf16ToUtf8 */
const struct _FS_CCM_GLYPH_ST *fontlib_001004d0(unsigned int code);    /* fsfont_GetGlyph */
int   fontlib_00102300(unsigned short *out, const unsigned short *fmt, char *ap);   /* fsfont_VsprintfWide */
int   fontlib_001025e0(char *buf, const char *fmt, void *ap);          /* FsFontVsprintf; ap = MW EABI 8-byte arg slots */
unsigned short fontlib_00102670(unsigned char *p);                     /* fsfont_CharCheck (charCheckFP) */
unsigned short fontlib_00102680(unsigned char **pp);                   /* fsfont_CharNext (charNextFP) */
int   fontlib_00102690(unsigned int code, const struct _FS_FONT_FORM_ST *form);   /* fsfont_DrawGlyph */
struct _FS_FONT_FORM_ST *fontlib_00102860(void);                       /* fsfont_GetStdForm */
struct _FS_FONT_RECT_ST *fontlib_00102870(void);                       /* fsfont_GetRect */
unsigned short fontlib_00102880(void);                                 /* fsfont_GetDummyCode */
int   fontlib_00102890(float *out);                                    /* fsfont_GetTotalHeight */
int   fontlib_001028b0(float *out);                                    /* fsfont_GetLineHeight */
int   fontlib_00102910(unsigned char *ax, unsigned char *ay);          /* fsfont_GetAlign */
int   fontlib_00102c20(float left, float top, float right, float bottom);   /* fsfont_SetPosRect */
unsigned short fontlib_00103040(unsigned char *s);                     /* fsfont_CharCheck8 */
unsigned short fontlib_00103090(unsigned char *s);                     /* fsfont_CharCheck16 */
unsigned short fontlib_001030a0(unsigned char **s);                    /* fsfont_CharNext8 */
unsigned short fontlib_00103110(unsigned char **s);                    /* fsfont_CharNext16 */
int   fontlib_001032a0(int fitRect, char *str);                        /* FsFontDraw (str = UTF-16 / byte text) */
int   fontlib_00103a20(unsigned int code);                             /* fsfont_DrawChar */
int   fontlib_00103b80(unsigned char *str, const float *lineWidths);   /* fsfont_DrawLines */
int   fontlib_00103ea0(float *maxWidth, float *totalHeight, float *lineWidths, unsigned char *str,
                       unsigned int endCode, unsigned int endCode2, const struct _FS_FONT_RECT_ST *rect);   /* fsfont_Measure */
/* F2 natives */
unsigned char *fontlib_00100610(int slot);                             /* fsfont_GetTex */
void  fontlib_00100b40(struct _FS_FONT_DRAW_ST *d);                    /* fsfont_DrawFunc (drawFunc; v0 = 0 in bw) */
int   fontlib_00100e20(const void *src, unsigned int tbp0, int tbw, int psm, int w, int h);   /* fsfont_UploadImage */
int   fontlib_00101550(const unsigned char *tex);                      /* fsfont_UploadTex */
int   fontlib_00101690(int texId, const float *pos, const unsigned int *color, const float *uv, float skew);   /* fsfont_PutGlyph */
int   fontlib_00101b90(int texId);                                     /* fsfont_PutTexState */
int   fontlib_00101f80(unsigned int n);                                /* fsfont_ReserveQwords */
int   fontlib_00103580(float *out);                                    /* fsfont_GetItalicSkew */
/* F3 natives (unreached) */
int   fontlib_001034d0(float *maxWidth, float *totalHeight, float *lineWidths, unsigned char *str,
                       unsigned int endCode, unsigned int endCode2, const struct _FS_FONT_RECT_ST *rect);   /* FsFontCalcSize */
int   fontlib_001028d0(unsigned int *out);                             /* fsfont_GetColor */
int   fontlib_001029d0(unsigned int ch, unsigned char *out);           /* fsfont_GetCharClass */
int   fontlib_00102a10(const unsigned int *src, unsigned int mask);    /* fsfont_SetColorMasked */
int   fontlib_00102a90(unsigned int v, unsigned int mask);             /* fsfont_SetColorAll */
int   fontlib_00102af0(const unsigned int *src);                       /* fsfont_SetColor4 */
int   fontlib_00104370(unsigned char **s, int draw);                   /* tag 'i' */
int   fontlib_001043b0(unsigned char **s, int draw);                   /* tag 'b' */
int   fontlib_001043f0(unsigned char **s, int draw);                   /* tag 'r' */
int   fontlib_00104630(unsigned char **s, int draw);                   /* tag 'a' */
int   fontlib_001047d0(unsigned char **s, int draw);                   /* tag 'c' */

#ifdef __cplusplus
}
#endif
