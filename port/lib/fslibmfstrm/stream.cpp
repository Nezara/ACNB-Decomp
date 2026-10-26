/* lib/fslibmfstrm/stream.cpp - per-stream state machine of the FsMFStrm client (whole file). A _STREAM_INFO_ST (the
 * first 0x120 bytes of an mstream.c record) is the EE view of one IOP file stream: uiStatus 0 idle, 1 ready (buffer
 * attached and cleared), 2 / 3 reading, 4 block done, 5 end of file, 6 closed, 7 busy (tested only, never set here);
 * flags.fin = EOF reached, flags.attach = a sector range is attached; name = debug string. RPCs to the IOP module:
 * 0x4000 attach sector range (OpenSector), 0x5000 close, 0xa000 reload size.
 *
 * Quirks kept: hSBuf is never set (sbuf.c has no allocator), so the sbuf_* calls return at their first guard and
 * strm_EndReading would print its error on every call (the path is never reached); strm_Close clears flags.fin but
 * not flags.attach; strm_DetachStreamBuffer tests status 1 / 2 / 7 (not 3); strm_InitInfo leaves iReadSize / pad0.
 * strm_Close / strm_SetReloadSize pass only the payload words they name: the original leaves the remaining
 * fsmfstrm_CallRpc words (a3 / t0..t3 / stack) as stale registers, the native passes 0 (blanked by strace -NormScrc). */
#include "acnb/port.h"
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/local/lib/fslibmfstrm/stream.h"

#ifdef ACNB_SOURCEPORT
char strm_OpenSector____FUNCTION__[16] = "strm_OpenSector";
char strm_msg_error[15] = "%s : ERROR!!! ";
char strm_msg_busy[30] = "\x8f\x88\x97\x9d\x92\x86\x82\xcd\x83" "A" "\x83" "^" "\x83" "b" "\x83" "`"
                         "\x82\xc5\x82\xab\x82\xdc\x82\xb9\x82\xf1\x82\xe6\n";   /* Shift-JIS */
char strm_msg_name[27] = "[SECTOR] : %d, [SIZE] : %d";
char strm_msg_endread[60] = "strm_EndReading : ERROR!!! sbuf_FreeReadBlock is failed...\n";
#endif

/* 0x4000 request (local at sp+0x30 in the original) */
struct strm_open_req { int index; int lsn; int size; unsigned int prio; };

void strm_InitInfo(struct _STREAM_INFO_ST *pInfo)
{
    if (!pInfo) return;
    pInfo->iSize = 0;
    pInfo->iNow = 0;
    pInfo->usPrio = 0x4000;
    pInfo->uiStatus = 0;
    pInfo->flags.uiPack = 0;
    pInfo->hSBuf = 0;
    pInfo->index = -1;
    pInfo->name[0] = 0;
}

/* attach the sector range [lsn, lsn + size) to the IOP stream; returns the IOP reply (nonzero = ok) */
int strm_OpenSector(struct _STREAM_INFO_ST *pInfo, int lsn, int size, unsigned short prio)
{
    PS2_FRAME(0x80);
    if (!pInfo) return 0;
    if (pInfo->uiStatus == 2 || pInfo->uiStatus == 3 || pInfo->uiStatus == 7) {
        ps2_printf_s(strm_msg_error, strm_OpenSector____FUNCTION__);
        ps2_printf(strm_msg_busy);
        return 0;
    }
    PS2_STACK(strm_open_req, req, 0x30);   /* send buffer (zeroed byte-wise, then every word written) */
    req.index = pInfo->index;
    req.lsn = lsn;
    req.size = size;
    req.prio = (unsigned int)(prio & 0xffff);
    int r = fsmfstrm_CallRpcStruct(0x4000, 1, 0x10, 4, &req);
    if (r != 0) {
        pInfo->iSize = size;
        pInfo->iNow = 0;
        pInfo->usPrio = prio;
        pInfo->flags.b.attach = 1;
        pInfo->flags.b.fin = 0;
        pInfo->uiStatus = 0;
        ps2_sprintf_ddd(pInfo->name, strm_msg_name, lsn, size, 0);
        if (pInfo->hSBuf) {
            sbuf_ClearData(pInfo->hSBuf);
            pInfo->uiStatus = 1;
        }
    }
    return r;
}

int strm_Close(struct _STREAM_INFO_ST *pInfo)
{
    PS2_FRAME(0x20);
    if (!pInfo) return 0;
    if (pInfo->uiStatus == 6) return 1;
    if (pInfo->uiStatus == 2 || pInfo->uiStatus == 3 || pInfo->uiStatus == 7) strm_ReadSync(pInfo, 1);
    fsmfstrm_CallRpc(0x5000, 0, pInfo->index, 0, 0, 0, 0, 0, 0, 0);
    pInfo->iSize = 0;
    pInfo->iNow = 0;
    pInfo->flags.b.fin = 0;
    pInfo->uiStatus = 6;
    pInfo->name[0] = 0;
    return 1;
}

void strm_DetachStreamBuffer(struct _STREAM_INFO_ST *pInfo)
{
    if (!pInfo) return;
    if (!pInfo->hSBuf) return;
    if (pInfo->uiStatus == 2 || pInfo->uiStatus == 1 || pInfo->uiStatus == 7) return;
    pInfo->hSBuf = 0;
    pInfo->uiStatus = 0;
}

/* the IOP finished a block: advance iNow, status 5 (end of file) or 4, release the block in the EE buffer */
void strm_EndReading(struct _STREAM_INFO_ST *pInfo)
{
    PS2_FRAME(0x20);
    if (!pInfo) return;
    if (pInfo->uiStatus != 2 && pInfo->uiStatus != 3) return;
    pInfo->iNow = pInfo->iNow + pInfo->iReadSize;
    pInfo->iReadSize = 0;
    if (!(pInfo->iNow < pInfo->iSize)) {
        pInfo->flags.b.fin = 1;
        pInfo->uiStatus = 5;
    } else {
        pInfo->uiStatus = 4;
    }
    if (!sbuf_FreeReadBlock(pInfo->hSBuf)) ps2_printf(strm_msg_endread);
}

int strm_ReadSync(struct _STREAM_INFO_ST *pInfo, int block)
{
    PS2_FRAME(0x30);
    if (!pInfo) return 0;
    return sbuf_SyncReadBlock(pInfo->hSBuf, block);
}

int strm_SetReloadSize(struct _STREAM_INFO_ST *pInfo, int iSize)
{
    PS2_FRAME(0x30);
    if (!pInfo) return 0;
    return fsmfstrm_CallRpc(0xa000, 1, pInfo->index, iSize, 0, 0, 0, 0, 0, 0);
}
