/* mfstrm internal state. The comments give the original's image
 * offsets (.data = IRX initial values, .bss = zero at load); the records keep the original field order and widths
 * (`short` list links with -1 terminators, H8), hSBuf is a host pointer to the ring record (not EE-visible). Shared by
 * the mfstrm*.cpp files and the unit test; not for other modules (they use mfstrm.h). */
#pragma once
#include "mfstrm.h"

namespace mfstrm {

/* s_StrmInfo[i] (0x72f0, 8 x 0x44) */
struct StrmBuffInfo;
struct StrmInfo {
    int32_t handle;          /* 0x00 file handle / base LSN (sector variant) */
    int32_t size;            /* 0x04 total bytes */
    int32_t w08;             /* 0x08 */
    int32_t pos;             /* 0x0c read position */
    int32_t w10;             /* 0x10 (host variant offset base) */
    int32_t loop_start;      /* 0x14 */
    int32_t loop_end;        /* 0x18 */
    int32_t loop_count;      /* 0x1c */
    int32_t rsize;           /* 0x20 size of the read in flight (not set by strm_InitInfo) */
    uint16_t prio;           /* 0x24 */
    uint16_t pad26;
    int32_t state;           /* 0x28 0 fresh / opened without ring, 1 idle with ring, 2 / 3 read issued / in flight, 4 done,
                                6 EOF, 7 closed (strm_Close), 8 never stored (compared only) */
    uint32_t flags;          /* 0x2c byte 0: [1:0] source type, bit2 EOF, bit3 attached, bit4 read start, bit5 loop;
                                bits 6..22 error / result */
    StrmBuffInfo *hSBuf;     /* 0x30 */
    uint8_t cdmode[4];       /* 0x34 sceCdRMode (byte 0x37 not cleared by strm_InitInfo) */
    int32_t reload;          /* 0x38 */
    int16_t next, prev;      /* 0x3c / 0x3e */
    int32_t sema;            /* 0x40 per-stream semaphore */
};

/* s_StrmBuffInfo[i] (0x7510, 8 x 0x20) */
struct StrmBuffInfo {
    uint32_t pBuf;           /* 0x00 IOP address */
    uint32_t pBufTop;        /* 0x04 read head */
    uint32_t pBufEnd;        /* 0x08 write head */
    int32_t iBufSize;        /* 0x0c */
    int32_t iReadSize;       /* 0x10 valid bytes */
    uint32_t pBufRead;       /* 0x14 end of the block being read */
    int16_t next, prev;      /* 0x18 / 0x1a */
    int16_t index;           /* 0x1c */
    uint8_t flags;           /* 0x1e bit0 = buffer owned (AllocSysMemory); not cleared by sbuf_Init */
    uint8_t pad1f;
};

/* list heads */
struct ListHead { int16_t top, num; };

/* s_StrmMng (0x72d0, 0x1c) */
struct StrmMng {
    int32_t w00;             /* 0x72d0 */
    uint32_t status;         /* 0x72d4 bit0 inited, bits 1..17 the last served stream's error field */
    int32_t packet_size;     /* 0x72d8 read packet size */
    ListHead free;           /* 0x72dc */
    ListHead alloc;          /* 0x72e0 */
    int16_t last_idx;        /* 0x72e4 last served stream */
    uint16_t last_prio;      /* 0x72e6 last served priority */
    int32_t w18;             /* 0x72e8 */
};

/* s_StrmBuffMng (.data 0x6e20: {free {0, 8}, alloc {-1, 0}, flags 0}) */
struct StrmBuffMng {
    ListHead free;           /* 0x6e20 */
    ListHead alloc;          /* 0x6e24 */
    uint32_t flags;          /* 0x6e28 bit0 inited */
};

/* s_ArbInfo[i] (0x71a0, 8 x 0x20): one SIF DMA request of dma_RequestTransBuffer (map 16.2). States (dma_Update):
 * 0 free / cancelled, 1 queued, 2 continue (same pass), 3 DMA in flight, 4 chunk done, 5 finished. */
struct ArbInfo {
    uint32_t src;            /* 0x00 IOP address of the block being sent (packet buffer or ring block) */
    uint32_t dst;            /* 0x04 EE address (advanced by chunk per block when flags bit0) */
    int32_t remain;          /* 0x08 bytes still to send */
    int32_t chunk;           /* 0x0c length of the block in flight */
    uint16_t prio;           /* 0x10 (lhu: unsigned compares) */
    int16_t next;            /* 0x12 */
    int16_t prev;            /* 0x14 */
    int16_t stream;          /* 0x16 stream index (-1 at init) */
    int32_t state;           /* 0x18 */
    uint32_t flags;          /* 0x1c byte: bit0 advance dst, bit1 continue while the stream has data, bit2 cancel requested */
};

/* s_ArbMng (0x7188, 0x18) */
struct ArbMng {
    ListHead free;           /* 0x7188 */
    ListHead alloc;          /* 0x718c (sorted by prio ascending, equal keys LIFO) */
    int16_t last_idx;        /* 0x7190 last serviced record (-1 after init; not cleared on release) */
    int16_t pad[7];
};

/* s_sdiData (0x72a0): the SIF DMA in flight (_SifTransBuffer / _SifTransSync) */
struct SifDmaInfo {
    uint32_t src;            /* 0x00 IOP address */
    uint32_t dst;            /* 0x04 EE address */
    int32_t remain;          /* 0x08 */
    int32_t chunk;           /* 0x0c */
    uint32_t id;             /* 0x10 SifSetDmaIntr result (never read) */
    int32_t busy;            /* 0x14 */
    int32_t abort;           /* 0x18 */
};

/* s_HostRead (0x7610) */
struct HostRead { int32_t result, fd; uint32_t buf; int32_t size; };

/* ---- module state (mfstrm.cpp ResetState sets the load-time values) */
extern uint32_t g_base;
extern iopk::RpcClient s_scdRpc;        /* 0x7150 */
/* dma.c */
extern int32_t s_DmaSema;               /* .data 0x6dfc = -1 */
extern int32_t g_DmaThID;               /* .data 0x6e00 = -1 */
extern uint32_t s_dma_pkt;              /* 0x7178 IOP address */
extern int32_t s_dma_pkt_size;          /* 0x717c */
extern int32_t s_bInited;               /* 0x7180 */
extern int32_t s_DmaStat;               /* 0x7184 */
extern ArbMng s_ArbMng;                 /* 0x7188 */
extern ArbInfo s_ArbInfo[8];            /* 0x71a0 */
extern SifDmaInfo s_sdiData;            /* 0x72a0 */
extern iopk::SifDmaData s_sddPacket;    /* 0x72c0 (the original's sceSifDmaData; src = host pointer of the IOP address) */
/* mstream.c */
extern int32_t s_SemaID;                /* .data 0x6e0c = -1: the mstrm lock */
extern int32_t s_MstrmThID;             /* .data 0x6e10 = -1 */
extern int32_t g_iUpdateSema;           /* .data 0x6e14 = -1 */
extern StrmMng s_StrmMng;               /* 0x72d0 */
extern int32_t s_iCdvdEndReading;       /* 0x72ec */
extern StrmInfo s_StrmInfo[8];          /* 0x72f0 */
/* sbuf.c */
extern StrmBuffMng s_StrmBuffMng;       /* .data 0x6e20 */
extern StrmBuffInfo s_StrmBuffInfo[8];  /* 0x7510 */
/* stream.c */
extern int32_t s_iHostThreadId;         /* .data 0x6e2c = -1 */
extern int32_t s_iSemaHost;             /* .data 0x6e30 = -1 */
extern int32_t s_iSyncSemaHost;         /* .data 0x6e34 = -1 */
extern HostRead s_HostRead;             /* 0x7610 */

void ResetState();                      /* the load-time image (mfstrm.cpp) */

/* ---- internal functions */
/* mfstrm.cpp */
void *_EtoI_RpcDispatch(int fno, void *buf, int size);
int _ItoE_BindRpc();
int _CallRpcSyncWait();                 /* 1 = client free, 0 = timed out */
int _CallRpc(uint32_t cmd, uint32_t wait, int ssize, int rsize);
int fsmfstrm_CallRpc(uint32_t cmd, uint32_t wait, int a2, int a3);   /* IOP -> EE (SID 0x4020): the original is variadic
                                                    (8 words sent); words 0 / 1 = a2 / a3, 2..7 sent as 0 (GH5-2) */
void _SifDmaEndFunc(void *arg);         /* 0xb48: SifSetDmaIntr end function (s_DmaStat = 0) */
int _SifTransBuffer(ArbInfo *rec);      /* 0 queued / -1 */
int _SifTransSync();                    /* 1 done / 0 busy (next chunk queued) */
/* mfstrm_dma.cpp */
int _GetArbiterInfo();
void _ReleaseArbiterInfo(int idx);
void _dma_SortArbitInfo(int idx);
void dma_ArbiterInit();
void dma_ArbiterQuit();                 /* RPC 0x500 */
int dma_AllocPacketBuffer(int size);
int dma_FreePacketBuffer();             /* RPC 0x700: 1 / 0 (a DMA in flight) */
int dma_RequestTransBuffer(uint32_t stream, uint32_t ee, uint32_t size, int prio, int f0, int f1);   /* RPC 0x100000 */
void dma_CancelTransBuffer(uint32_t stream);   /* RPC 0x300000 */
void dma_Update();                      /* the arbiter pass (_dma_Thread) */
int dma_Sync(uint32_t stream);          /* RPC 0x200000: 1 done / 0 busy */
/* mfstrm_stream.cpp */
int mstrm_SetReloadSize(int idx, int n);
void mstrm_SetReadPacketSize(int n);
int mstrm_SetCdRMode(int idx, const uint8_t *mode);
int _mstrm_GetInfo();
void _mstrm_ReleaseInfo(int idx);
void strm_InitInfo(StrmInfo *p);
int strm_Init();
int strm_AttachStreamBuffer(StrmInfo *p, StrmBuffInfo *h);
int strm_SetReloadSize(StrmInfo *p, int n);
/* G2 (mfstrm_stream.cpp) */
int mstrm_GetValidDataSizeF(int idx);   /* RPC 0x9000: no lock */
int mstrm_SetPriority(int idx, uint16_t prio);
int mstrm_GetPriority(int idx);
int mstrm_SetReadStart(int idx, int on);   /* 1 / 0 / -1 (bad index); the 0xb000 arm ignores it */
int mstrm_GetReadError(int idx);
void _mstrm_SortInfo(int idx);
int strm_OpenSector(StrmInfo *p, int lsn, int size);
int strm_Close(StrmInfo *p);
int strm_SetPriority(StrmInfo *p, uint16_t prio);
int strm_GetPriority(StrmInfo *p);
int strm_SetReadStart(StrmInfo *p, int on);
/* mfstrm_sbuf.cpp */
void sbuf_Init();
StrmBuffInfo *_sbuf_GetStrmBufInfo();
void _sbuf_ReleaseStrmBufInfo(StrmBuffInfo *r);
StrmBuffInfo *sbuf_AllocStreamBuffer(int size);
int sbuf_GetBufferSize(const StrmBuffInfo *r);
int sbuf_ClearData(StrmBuffInfo *r);    /* G2: the old iReadSize */
int sbuf_GetValidDataSize(const StrmBuffInfo *r);   /* G2 */

/* G3 + G4 live (mfstrm_read.cpp / mfstrm_sbuf.cpp) */
struct BlockReq { uint32_t ptr; int32_t len; };   /* the 8-byte stack pair at sp+0x28 of strm_Read / mstrm_GetData:
                                                    ptr = block IOP address, len = requested / granted bytes */
void mstrm_Update();                    /* the update pass (_mstrm_Thread) */
void _mstrm_CdSyncCB(int cause);        /* sceCdCallback target */
int mstrm_IsDevBusy(int arg);           /* RPC 0x1000000 */
int strm_RequiredReading(StrmInfo *p);
int strm_Read(StrmInfo *p, int n, int sync);
int _strm_ReadCdvd(StrmInfo *p, uint32_t dst, int len, int sync);
int strm_Polling(StrmInfo *p);
int strm_Sync(StrmInfo *p);             /* strm_Close, mstrm_Sync */
int strm_Seek(StrmInfo *p, int pos);    /* mstrm_Seek */
int _strm_SeekCdvd(StrmInfo *p, int pos);
uint32_t sbuf_AllocReadBlock(StrmBuffInfo *r, BlockReq *rq);
int sbuf_FreeReadBlock(StrmBuffInfo *r);
int sbuf_CancelReadBlock(StrmBuffInfo *r);
int sbuf_RequiredRead(const StrmBuffInfo *r, int reload);
uint32_t sbuf_GetDataBlock(const StrmBuffInfo *r, BlockReq *rq);
int sbuf_RemoveDataBlock(StrmBuffInfo *r, int n);
uint32_t mstrm_GetBufferData(int idx, int *plen);   /* dma_Update without packet buffer: ring block, nothing removed */
extern int g_update_calls;              /* unit-test seam: mstrm_Update entries since module start */
extern int g_update_locked;             /* s_SemaID was held when the last mstrm_Update was entered */

/* G5 (teardown, file / host variants; map section 17) */
void mstrm_Quit();                      /* RPC 0x200 */
int mstrm_AttachFileOffset(int idx, const char *name, uint16_t prio, int off, int len);   /* RPC 0x13000 */
void sbuf_Quit();
StrmBuffInfo *sbuf_SetStreamBuffer(uint32_t buf, int size);
void sbuf_FreeStreamBuffer(StrmBuffInfo *r);
void strm_Quit();
void strm_DetachStreamBuffer(StrmInfo *p);
int strm_OpenFile(StrmInfo *p, const char *name);
int strm_OpenFileOffset(StrmInfo *p, const char *name, int off, int len);
int _strm_OpenCdvd(StrmInfo *p, const char *name);
int _strm_OpenCdvdOffset(StrmInfo *p, const char *name, int off, int len);
int _strm_OpenHost(StrmInfo *p, const char *name);
int _strm_OpenHostOffset(StrmInfo *p, const char *name, int off, int len);
int _strm_ReadHost(StrmInfo *p, uint32_t dst, int len, int sync);   /* strm_Read, source type 0 / 1 */
int _strm_SeekHost(StrmInfo *p, int pos);                           /* strm_Seek, source type 0 / 1 */
int _read(int fd, uint32_t dst, int len);                           /* 1 = job posted, else the probe's error */
int _read_sync(int wait);                                           /* strm_Polling 0 / strm_Sync 1: 1 busy, 0 done */

extern int g_dma_update_calls;          /* dma_Update entries since module start (unit test: the 4000 us grid) */
extern uint64_t g_dma_update_t;         /* T of the last dma_Update */

}  // namespace mfstrm
