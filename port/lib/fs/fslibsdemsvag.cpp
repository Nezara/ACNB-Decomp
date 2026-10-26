/* lib/fs/fslibsdemsvag.cpp - front end of FSSDEVAG (IOP module 3: one-shot message voices played from a VAG in EE
 * memory) (whole file). Start allocates an IOP buffer (once), sends buffer / size / sample rate / half buffer and the
 * volumes; the IOP then pulls the data over IOP -> EE requests (cmd 0x204 -> RpcCallBack -> TransVagEeToIop: copy into
 * the 16 KB staging buffer s_ucVagBuff, sceSifSetDma to the IOP buffer). ExecVag (every frame through fssdemsvagProc)
 * steps the state: 1 started -> (IOP status 3) play -> 2 playing -> (status 8) stop; a finished DMA is reported
 * (cmd 0x205). GetVagInfo walks a "SCEI" HD bank header to the VAG of a program.
 * Quirks kept: GetVagInfo's end scan reads 0xf / 0x1f bytes before the VAG and has no length bound, its split scan stops
 * at the first split whose set index is <= maxSampleSetNumber (unbounded); IsEndOfStream returns 1 while the stream is
 * active; Start does not check the IOP allocation; TransVagEeToIop copies with no bound against the staging buffer
 * (s_evmInfo follows it in memory: ee_buff is re-read for every byte, like the original); the Stop / ExecVag DMA waits
 * have no timeout. */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/local/lib/fs/fslibsdemsvag.h"

int fssdemsvagGetVagInfo(void *pHd, void *pBd, int iProgram, struct struct_83eb889b *pviOut)
{
    unsigned char *hd = (unsigned char *)pHd;
    struct struct_d404b84b *head = (struct struct_d404b84b *)(hd + 0x10);   /* after the "SCEIVers" chunk */
    struct struct_5bf6c973 *prog_ck = (struct struct_5bf6c973 *)(hd + head->programChunkAddr);
    struct struct_e983ab9e *set_ck = (struct struct_e983ab9e *)(hd + head->sampleSetChunkAddr);
    struct struct_6d428e4a *vag_ck = (struct struct_6d428e4a *)(hd + head->vagInfoChunkAddr);
    struct struct_cf474ac2 *smpl_ck = (struct struct_cf474ac2 *)(hd + head->sampleChunkAddr);
    if (iProgram < 0 || prog_ck->maxProgramNumber < (unsigned int)iProgram) return 0;
    unsigned int off = prog_ck->programOffsetAddr[iProgram];
    if (off == 0xffffffffu) return 0;
    struct struct_c891a358 *prog = (struct struct_c891a358 *)((unsigned char *)prog_ck + off);
    if (prog->nSplit != 1) return 0;
    struct struct_dd100c38 *split = (struct struct_dd100c38 *)((unsigned char *)prog + prog->splitBlockAddr);
    int max_set = (int)set_ck->maxSampleSetNumber;
    while (max_set < (int)split->sampleSetIndex)
        split = (struct struct_dd100c38 *)((unsigned char *)split + 0x14);
    struct struct_7dee3f2e *set =
        (struct struct_7dee3f2e *)((unsigned char *)set_ck + set_ck->sampleSetOffsetAddr[split->sampleSetIndex]);
    if (set->nSample != 1) return 0;
    unsigned int si = set->sampleIndex[0];
    if ((int)smpl_ck->maxSampleNumber < (int)si) return 0;
    unsigned short *smpl = (unsigned short *)((unsigned char *)smpl_ck + smpl_ck->sampleOffsetAddr[si]);
    unsigned int vi = smpl[0];   /* the sample's vagIndex */
    if ((int)vag_ck->maxVagInfoNumber < (int)vi) return 0;
    struct struct_b3ed689f *vag = (struct struct_b3ed689f *)((unsigned char *)vag_ck + vag_ck->vagInfoOffsetAddr[vi]);
    pviOut->sample = vag->vagSampleRate;
    pviOut->buff = (unsigned char *)pBd + vag->vagOffsetAddr;
    unsigned int vol = (unsigned int)prog->progVolume << 7;
    if (vol >= 0x4000) vol = 0x3fff;
    pviOut->rvol = (unsigned short)vol;
    pviOut->lvol = (unsigned short)vol;
    pviOut->size = 0;
    /* end of the VAG: the ADPCM flag byte (+1) of the block before p is 7 and the one of the block before that has bit 0 */
    for (unsigned char *p = pviOut->buff;; p += 0x10) {
        if (p[-0xf] == 7 && (p[-0x1f] & 1) && pviOut->size > 0) return 1;
        pviOut->size += 0x10;
    }
}

int fssdemsvagIsEndOfStream(void)
{
    PS2_FRAME(0x10);
    if (s_evmInfo.stat == 0) return 0;
    unsigned int st = (unsigned int)fssdCallRpcVoid(3, 1, 0x2070000) & 0xff;
    if (st == 8 || st == 0) return 0;
    return 1;
}

int fssdemsvagStop(void)
{
    PS2_FRAME(0x20);
    volatile struct struct_29a6235f *ev = &s_evmInfo;
    if (ev->stat == 0) return 0;
    fssdCallRpcVoid(3, 1, 0x2060000);
    if (ev->did) {
        while (sceSifDmaStat((unsigned int)ev->did) >= 0) {}
        ev->did = 0;
    }
    if (ev->iop_buff) {
        fssdFreeIopMemory(ev->iop_buff);
        ev->iop_buff = nullptr;
    }
    ev->stat = 0;
    ev->f_played = 0;
    return 1;
}

int fssdemsvagStart(unsigned char *pucBuff, int iSize, int iSample, unsigned short usLeftVol, unsigned short usRightVol)
{
    PS2_FRAME(0x70);
    struct struct_29a6235f *ev = &s_evmInfo;
    if (!pucBuff || !iSize) return 0;
    if (ev->stat == 3 || ev->stat == 2 || ev->stat == 1) fssdemsvagStop();
    if (!ev->iop_buff) ev->iop_buff = (unsigned char *)fssdAllocIopMemory(ev->iop_size + 0x40);
    ev->ee_buff = pucBuff;
    ev->size = iSize;
    ev->sample = iSample;
    if (fssdCallRpc(3, 1, 0x2000000, (int)PS2PTR(ev->iop_buff), iSize, iSample, ev->iop_size >> 1, 0, 0, 0, 0) != 1) {
        fssdCallRpcVoid(3, 1, 0x2060000);
        return 0;
    }
    ev->lvol = usLeftVol;
    ev->rvol = usRightVol;
    fssdCallRpc(3, 1, 0x2080000, ev->lvol, ev->rvol, 0, 0, 0, 0, 0, 0);
    ev->stat = 1;
    ev->f_played = 0;
    return 1;
}

void fssdemsvagProc(void)
{
    ExecVag();
}

void fssdemsvagReset(void)
{
    PS2_FRAME(0x10);
    ps2_memset(&s_evmInfo, 0, 0x20);
    s_evmInfo.iop_size = 0x4000;
}

int fssdemsvagInit(void)
{
    PS2_FRAME(0x10);
    fssdRegister(3, GFN(RpcCallBack));
    if (!fssdCallRpcVoid(3, 1, 0x10000)) return 0;
    fssdRequireIopToEeRpc();
    ps2_memset(&s_evmInfo, 0, 0x20);
    s_evmInfo.iop_size = 0x4000;
    return 1;
}

static void ExecVag(void)
{
    PS2_FRAME(0x20);
    struct struct_29a6235f *ev = &s_evmInfo;
    switch (ev->stat) {
    case 1:
        if (fssdCallRpcVoid(3, 1, 0x2070000) == 3) {
            fssdCallRpcVoid(3, 1, 0x2010000);
            ev->f_played = 1;
            ev->stat = 2;
        }
        break;
    case 2:
        if (fssdCallRpcVoid(3, 1, 0x2070000) == 8) fssdemsvagStop();
        break;
    }
    if (ev->did && sceSifDmaStat((unsigned int)ev->did) < 0) {
        if (ev->stat) fssdCallRpcVoid(3, 1, 0x2050000);
        ev->did = 0;
    }
}

static int TransVagEeToIop(unsigned int uiOffset, unsigned int uiSize)
{
    PS2_FRAME(0x30);
    volatile struct struct_29a6235f *ev = &s_evmInfo;
    if (ev->did != 0) return 0;
    unsigned char *dst = s_ucVagBuff;
    for (unsigned int i = 0; i < uiSize; i++) dst[i] = ev->ee_buff[uiOffset + i];
    s_sddRpc.data = PS2PTR(s_ucVagBuff);
    s_sddRpc.addr = PS2PTR(ev->iop_buff);
    s_sddRpc.size = uiSize;
    s_sddRpc.mode = 0;
    FlushCache(0);
    unsigned int did = sceSifSetDma(&s_sddRpc, 1);
    ev->did = (int)did;
    return did != 0;
}

/* s_psrcFunc[3]: (cmd, pData, iSize (unused), &iRetSize); cmd 0x204 = "send the next block" {offset, size} */
static void *RpcCallBack(unsigned int uiCmd, void *pData, int iSize, int *piRetSize)
{
    PS2_FRAME(0x20);
    (void)iSize;
    if ((uiCmd & 0x0fff0000u) == 0x02040000u) {
        unsigned int *d = (unsigned int *)pData;
        s_iRpcEeToIopRet = TransVagEeToIop(d[0], d[1]);
    }
    *piRetSize = 4;
    return &s_iRpcEeToIopRet;
}
